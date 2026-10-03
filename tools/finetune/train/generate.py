#!/usr/bin/env python3
# tools/finetune/train/generate.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# Greedy generations for the held-out eval, from the base model or the base
# model + a LoRA adapter written by train_qlora.py. One prompt at a time (no
# batch padding to perturb results), fixed seed, explicit generation config
# (the model's own sampling defaults and repetition penalty are NOT used).
#
# Resumable: ids already in --out are skipped; a torn last line (a crash
# mid-write) is dropped and regenerated. A sidecar <out>.config.json records
# how the file was produced, and a resume with different settings is
# refused, so one file never mixes two configurations.
# ---------------------------------------------------------------------------
"""Generate {id, text} JSONL from a prompts JSONL, with or without a LoRA adapter.

Input lines need `id` and `prompt` (tools/finetune/make_eval_prompts.py's
eval_prompts.jsonl); extra keys are ignored. A line with `messages` and no
`prompt` uses its first user message.

With --adapter, --model / --quant / --system default to what the adapter's
run_config.json records, so base and tuned see identical prompts. For the
base-model run, pass the same --model / --quant / --system as training
(defaults match train_qlora.py's).

Examples:
  python generate.py --prompts ../out/eval_prompts.jsonl --out ../out/gen-base.jsonl
  python generate.py --prompts ../out/eval_prompts.jsonl --adapter out/adapter --out ../out/gen-tuned.jsonl
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import train_qlora as tq  # noqa: E402  -- shared loading, dtype and template code


def die(msg, code=2):
    sys.stderr.write("generate: error: %s\n" % msg)
    sys.exit(code)


def log(msg):
    sys.stderr.write("generate: %s\n" % msg)
    sys.stderr.flush()


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prompts", required=True, help="JSONL with id + prompt per line")
    ap.add_argument("--out", required=True, help="generations JSONL ({id, text}); appended to on resume")
    ap.add_argument("--adapter", default=None, help="LoRA adapter dir from train_qlora.py (omit for the base model)")
    ap.add_argument("--model", default=None, help="base model (default: the adapter's, else %s)" % tq.DEFAULT_MODEL)
    ap.add_argument("--quant", choices=("nf4", "none"), default=None,
                    help="default: the adapter's, else nf4 (match training)")
    ap.add_argument("--system", default=None,
                    help="system message (default: the adapter's recorded one, else none)")
    ap.add_argument("--compute-dtype", choices=("auto", "bf16", "fp16", "fp32"), default=None,
                    help="default: the adapter's recorded --compute-dtype, else auto")
    ap.add_argument("--max-new-tokens", type=int, default=512, help="default: %(default)s")
    ap.add_argument("--seed", type=int, default=42, help="default: %(default)s (greedy decoding does not sample; "
                                                         "the seed only pins anything stochastic left)")
    ap.add_argument("--limit", type=int, default=None, help="only the first N prompts (smoke tests)")
    ap.add_argument("--trust-remote-code", action="store_true")
    return ap.parse_args(argv)


def read_prompts(path):
    items, seen = [], set()
    with open(path, "r", encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            if not line.strip():
                continue
            r = json.loads(line)
            if "id" not in r:
                die("%s:%d: no id" % (path, n))
            prompt = r.get("prompt")
            if prompt is None and isinstance(r.get("messages"), list):
                prompt = next((m["content"] for m in r["messages"] if m.get("role") == "user"), None)
            if not isinstance(prompt, str):
                die("%s:%d: no prompt" % (path, n))
            if r["id"] in seen:
                die("%s:%d: duplicate id %s" % (path, n, r["id"]))
            seen.add(r["id"])
            items.append({"id": r["id"], "prompt": prompt})
    return items


def read_done(path):
    """Ids already generated. A torn final line is cut off so it is regenerated."""
    if not os.path.exists(path):
        return set()
    with open(path, "rb") as f:
        data = f.read()
    done, good_end = set(), 0
    pos = 0
    for raw in data.split(b"\n"):
        end = pos + len(raw) + 1
        if raw.strip():
            try:
                done.add(json.loads(raw.decode("utf-8"))["id"])
                good_end = min(end, len(data))
            except (ValueError, KeyError):
                if end < len(data):
                    die("%s: corrupt line in the middle of the file; refusing to resume" % path)
                log("dropping a torn last line in %s" % path)
                break
        pos = end
    if good_end < len(data):
        with open(path, "r+b") as f:
            f.truncate(good_end)
    return done


def main(argv=None):
    args = parse_args(argv)
    rc = {}
    if args.adapter:
        p = os.path.join(args.adapter, "run_config.json")
        if not os.path.exists(os.path.join(args.adapter, "adapter_config.json")):
            die("%s holds no adapter_config.json" % args.adapter)
        if os.path.exists(p):
            with open(p, encoding="utf-8") as f:
                rc = json.load(f).get("args", {})
        else:
            log("WARNING: %s missing; using command-line / default settings" % p)
    model_id = args.model or rc.get("model") or tq.DEFAULT_MODEL
    quant = args.quant or rc.get("quant") or "nf4"
    system = args.system if args.system is not None else rc.get("system")
    cdt = args.compute_dtype or rc.get("compute_dtype") or "auto"
    if rc:
        for k, v in (("model", model_id), ("quant", quant), ("system", system)):
            if rc.get(k) != v:
                log("WARNING: --%s %r differs from the adapter's training value %r" % (k, v, rc.get(k)))

    try:
        import torch
        import transformers
        from transformers import AutoTokenizer, GenerationConfig, set_seed
    except ImportError as e:
        die("missing dependency (%s); see tools/finetune/train/SETUP.md" % e)
    set_seed(args.seed)

    items = read_prompts(args.prompts)
    if args.limit is not None:
        items = items[:args.limit]
    gen_cfg = {"model": model_id, "adapter": os.path.abspath(args.adapter) if args.adapter else None,
               "adapter_sha256": None, "quant": quant, "system": system, "compute_dtype": cdt,
               "max_new_tokens": args.max_new_tokens, "decoding": "greedy", "seed": args.seed}
    if args.adapter:
        st = os.path.join(args.adapter, "adapter_model.safetensors")
        if os.path.exists(st):
            gen_cfg["adapter_sha256"] = tq.sha256_file(st)
    side = args.out + ".config.json"
    if os.path.exists(side) and os.path.exists(args.out):
        with open(side, encoding="utf-8") as f:
            old = json.load(f)
        if old != gen_cfg:
            die("%s was produced with different settings (%s); use a new --out"
                % (args.out, ", ".join(sorted(k for k in gen_cfg if old.get(k) != gen_cfg[k]))))
    done = read_done(args.out)
    todo = [it for it in items if it["id"] not in done]
    log("%d prompts, %d already in %s, %d to generate" % (len(items), len(items) - len(todo), args.out, len(todo)))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(side, "w", encoding="utf-8", newline="\n") as f:
        json.dump(gen_cfg, f, indent=2, sort_keys=True, ensure_ascii=False)
        f.write("\n")
    if not todo:
        return 0

    tok = AutoTokenizer.from_pretrained(model_id, trust_remote_code=args.trust_remote_code)
    on_gpu = torch.cuda.is_available()
    dtype, dtype_name = tq.pick_dtype(torch, cdt, on_gpu)
    margs = argparse.Namespace(model=model_id, quant=quant, trust_remote_code=args.trust_remote_code)
    model = tq.load_base(margs, torch, transformers, dtype, on_gpu)
    if args.adapter:
        from peft import PeftModel
        model = PeftModel.from_pretrained(model, args.adapter)
    model.eval()
    log("model %s quant %s dtype %s adapter %s on %s" % (model_id, quant, dtype_name, args.adapter,
                                                         "gpu" if on_gpu else "cpu"))

    eos = []
    for e in (getattr(model.generation_config, "eos_token_id", None), tok.eos_token_id):
        for x in (e if isinstance(e, (list, tuple)) else [e]):
            if x is not None and x not in eos:
                eos.append(x)
    pad = tok.pad_token_id if tok.pad_token_id is not None else eos[0]
    gc = GenerationConfig(do_sample=False, num_beams=1, max_new_tokens=args.max_new_tokens,
                          repetition_penalty=1.0, temperature=None, top_p=None, top_k=None,
                          eos_token_id=eos, pad_token_id=pad)
    device = next(model.parameters()).device

    with open(args.out, "a", encoding="utf-8", newline="\n") as out:
        for k, it in enumerate(todo, 1):
            msgs = tq.with_system([{"role": "user", "content": it["prompt"]}], system)
            text = tq.render(tok, msgs, True)
            ids = tok(text, add_special_tokens=False, return_tensors="pt").to(device)
            with torch.no_grad():
                gen = model.generate(**ids, generation_config=gc)
            new = gen[0, ids["input_ids"].shape[1]:]
            answer = tok.decode(new, skip_special_tokens=True)
            out.write(json.dumps({"id": it["id"], "text": answer}, ensure_ascii=False) + "\n")
            out.flush()
            os.fsync(out.fileno())
            stop = "eos" if len(new) and int(new[-1]) in eos else "max_new_tokens"
            log("[%d/%d] %s: %d new tokens (%s)" % (k, len(todo), it["id"], len(new), stop))
    return 0


if __name__ == "__main__":
    sys.exit(main())
