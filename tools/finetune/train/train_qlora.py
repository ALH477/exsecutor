#!/usr/bin/env python3
# tools/finetune/train/train_qlora.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# QLoRA (or plain LoRA) supervised fine-tuning on datasets/finetune/, sized
# for a laptop GPU with ~8 GB of usable VRAM. transformers + peft, plain
# Trainer (no trl), bitsandbytes NF4 when --quant nf4.
#
# Order of work, on purpose:
#   1. tokenize every record with the REAL tokenizer and chat template, and
#      report the token-length distribution. An over-long record is never
#      truncated: --on-overlength fail (default) stops, drop drops it and
#      names it. --measure-only stops here.
#   2. load the model, attach LoRA, train with loss on the assistant tokens
#      only (every prompt / template token has label -100).
#   3. save the LoRA adapter only, plus run_config.json (every hyperparameter
#      and library version), length_report.json and trainer_log.json.
#
# Deterministic where the hardware lets it be: seeded, data order a function
# of --seed, no clock in run_config.json. GPU kernels are not bit-exact
# across runs; --full-determinism asks for more and may fail on ROCm.
# ---------------------------------------------------------------------------
"""QLoRA fine-tune on the Exsecutor chat dataset, loss masked to assistant tokens.

Examples:
  # measure token lengths only (needs the tokenizer, not the weights' GPU)
  python train_qlora.py --measure-only

  # the default 8 GB recipe: Qwen2.5-Coder-3B-Instruct, NF4, 3 epochs
  python train_qlora.py --out out/qwen3b-nf4

  # fallback (a): no bitsandbytes on this card -> bf16/fp16 LoRA on 1.5B
  python train_qlora.py --quant none --model Qwen/Qwen2.5-Coder-1.5B-Instruct --out out/qwen1.5b-lora

  # a tiny run that exercises every step (a few optimizer steps)
  python train_qlora.py --smoke --quant none --model Qwen/Qwen2.5-0.5B-Instruct --out out/smoke

Outputs in --out: adapter_model.safetensors, adapter_config.json,
run_config.json, length_report.json, trainer_log.json.
"""

import argparse
import hashlib
import json
import math
import os
import platform
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))  # tools/finetune/train -> repo
DATASET = os.path.join(ROOT, "datasets", "finetune")
DEFAULT_MODEL = "Qwen/Qwen2.5-Coder-3B-Instruct"
LORA_TARGETS = ["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"]
# Llama 3.x templates read the clock (strftime_now) unless date_string is
# given. A fixed value keeps the rendered prompt, and so training, a pure
# function of the data. Ignored by templates that do not use it (Qwen).
FIXED_DATE_STRING = "26 Jul 2024"
# End-of-turn markers we accept right after the assistant content, so that the
# model is trained to stop. tokenizer.eos_token is tried first.
END_OF_TURN = ("<|im_end|>", "<|eot_id|>", "<|end|>", "</s>", "<|endoftext|>")
SMOKE_TRAIN, SMOKE_VAL, SMOKE_STEPS = 8, 4, 6


def die(msg, code=2):
    sys.stderr.write("train_qlora: error: %s\n" % msg)
    sys.exit(code)


def log(msg):
    sys.stderr.write("train_qlora: %s\n" % msg)
    sys.stderr.flush()


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", default=DEFAULT_MODEL,
                    help="HF hub id or local dir (default: %(default)s; alternative: "
                         "meta-llama/Llama-3.2-3B-Instruct, gated, needs `hf auth login`)")
    ap.add_argument("--quant", choices=("nf4", "none"), default="nf4",
                    help="nf4: 4-bit NF4 + double quant via bitsandbytes (QLoRA). "
                         "none: base weights in the compute dtype (plain LoRA). Default: %(default)s")
    ap.add_argument("--train", default=os.path.join(DATASET, "train.jsonl"), help="default: %(default)s")
    ap.add_argument("--validation", default=os.path.join(DATASET, "validation.jsonl"),
                    help="default: %(default)s; '' disables evaluation")
    ap.add_argument("--out", default=os.path.join(HERE, "out", "adapter"), help="output dir (default: %(default)s)")
    ap.add_argument("--max-seq-len", type=int, default=1024, help="tokens per example, prompt included (default: %(default)s)")
    ap.add_argument("--on-overlength", choices=("fail", "drop"), default="fail",
                    help="a record longer than --max-seq-len: fail the run, or drop it and list it. "
                         "Never truncated. Default: %(default)s")
    ap.add_argument("--epochs", type=float, default=3.0, help="default: %(default)s")
    ap.add_argument("--lr", type=float, default=2e-4, help="peak learning rate, cosine schedule (default: %(default)s)")
    ap.add_argument("--warmup-ratio", type=float, default=0.03, help="default: %(default)s")
    ap.add_argument("--batch-size", type=int, default=1, help="per-device train batch (default: %(default)s)")
    ap.add_argument("--grad-accum", type=int, default=16, help="default: %(default)s")
    ap.add_argument("--lora-r", type=int, default=16, help="default: %(default)s")
    ap.add_argument("--lora-alpha", type=int, default=32, help="default: %(default)s")
    ap.add_argument("--lora-dropout", type=float, default=0.05, help="default: %(default)s")
    ap.add_argument("--optim", default="auto",
                    help="auto: paged_adamw_8bit when bitsandbytes imports and a GPU is visible, "
                         "else adamw_torch. Or any transformers optim name. Default: %(default)s")
    ap.add_argument("--compute-dtype", choices=("auto", "bf16", "fp16", "fp32"), default="auto",
                    help="auto: bf16 if the GPU reports bf16 support, else fp16; fp32 on CPU. Default: %(default)s")
    ap.add_argument("--system", default=None,
                    help="system message prepended to every record (default: none, so the chat "
                         "template's own default applies; generate.py reuses whatever is recorded)")
    ap.add_argument("--no-gradient-checkpointing", action="store_true", help="faster, much more memory")
    ap.add_argument("--seed", type=int, default=42, help="default: %(default)s")
    ap.add_argument("--full-determinism", action="store_true",
                    help="transformers' enable_full_determinism (deterministic kernels; may be slower or "
                         "unsupported on ROCm)")
    ap.add_argument("--logging-steps", type=int, default=1, help="default: %(default)s")
    ap.add_argument("--smoke", action="store_true",
                    help="tiny run: %d train / %d validation records, %d optimizer steps, grad-accum 1, "
                         "then reload the saved adapter and check it reproduces the in-memory loss"
                         % (SMOKE_TRAIN, SMOKE_VAL, SMOKE_STEPS))
    ap.add_argument("--max-steps", type=int, default=None, help="override the step count (debugging)")
    ap.add_argument("--measure-only", action="store_true",
                    help="tokenize, print the length report, write it to --out, and exit (no model load)")
    ap.add_argument("--show-example", type=int, default=0,
                    help="index of the train record whose masking is printed (default: %(default)s; -1: none)")
    ap.add_argument("--trust-remote-code", action="store_true", help="passed to from_pretrained (off by default)")
    return ap.parse_args(argv)


# --------------------------------------------------------------------------- data

def read_jsonl(path):
    out = []
    with open(path, "r", encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            if not line.strip():
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError as e:
                die("%s:%d: not JSON: %s" % (path, n, e))
            msgs = rec.get("messages")
            if not isinstance(msgs, list) or len(msgs) < 2 or msgs[-1].get("role") != "assistant":
                die("%s:%d: expected messages ending in an assistant turn" % (path, n))
            out.append(rec)
    return out


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def with_system(messages, system):
    msgs = [dict(m) for m in messages]
    if system is not None and msgs[0].get("role") != "system":
        msgs.insert(0, {"role": "system", "content": system})
    return msgs


def render(tok, messages, add_generation_prompt):
    return tok.apply_chat_template(messages, tokenize=False, add_generation_prompt=add_generation_prompt,
                                   date_string=FIXED_DATE_STRING)


def encode(tok, rec, system):
    """Return input_ids and labels for one record; labels are -100 except on the
    final assistant message's content and the end-of-turn token after it.

    The boundary is found in characters (the rendered prompt is a prefix of
    the rendered conversation) and mapped to tokens by offsets; a token that
    straddles the boundary, or labels that do not decode back to exactly the
    assistant text, stop the run instead of training on a wrong mask."""
    msgs = with_system(rec["messages"], system)
    prompt_text = render(tok, msgs[:-1], True)
    full_text = render(tok, msgs, False)
    if not full_text.startswith(prompt_text):
        raise ValueError("%s: the chat template's generation prompt is not a prefix of the full "
                         "conversation; cannot locate the assistant turn" % rec["id"])
    start = len(prompt_text)
    content = msgs[-1]["content"]
    tail = full_text[start:]
    if tail.startswith(content):
        used = content
    elif tail.startswith(content.strip()):  # templates that trim (Llama 3.x)
        used = content.strip()
    else:
        raise ValueError("%s: assistant content not found verbatim after the generation prompt" % rec["id"])
    end = start + len(used)
    eot = None
    for cand in ([tok.eos_token] if tok.eos_token else []) + list(END_OF_TURN):
        if cand and full_text.startswith(cand, end):
            eot = cand
            break
    if eot is None:
        raise ValueError("%s: no end-of-turn token after the assistant content (got %r); the model "
                         "would never learn to stop" % (rec["id"], full_text[end:end + 20]))
    end += len(eot)

    enc = tok(full_text, add_special_tokens=False, return_offsets_mapping=True)
    ids, offs = enc["input_ids"], enc["offset_mapping"]
    labels = []
    for tid, (s, e) in zip(ids, offs):
        if e <= start or s >= end:
            if s < end and e > start:  # zero-width token inside the span is impossible here
                raise ValueError("%s: ambiguous token offsets" % rec["id"])
            labels.append(-100)
        elif s >= start and e <= end:
            labels.append(tid)
        else:
            raise ValueError("%s: a token straddles the prompt/answer boundary (%d,%d) vs [%d,%d)"
                             % (rec["id"], s, e, start, end))
    target_ids = [t for t in labels if t != -100]
    if not target_ids:
        raise ValueError("%s: no target tokens" % rec["id"])
    decoded = tok.decode(target_ids, skip_special_tokens=False, clean_up_tokenization_spaces=False)
    if decoded != full_text[start:end]:
        raise ValueError("%s: unmasked labels decode to %r, expected %r"
                         % (rec["id"], decoded[:80], full_text[start:end][:80]))
    return {"id": rec["id"], "task": rec.get("task"), "input_ids": ids, "labels": labels,
            "n_tokens": len(ids), "n_target": len(target_ids), "full_text": full_text,
            "target_text": full_text[start:end]}


def pct(sorted_vals, p):
    # nearest-rank percentile, no interpolation
    if not sorted_vals:
        return None
    k = max(1, math.ceil(p / 100.0 * len(sorted_vals)))
    return sorted_vals[k - 1]


def length_stats(examples, max_len):
    lens = sorted(e["n_tokens"] for e in examples)
    over = [e for e in examples if e["n_tokens"] > max_len]
    by_task = {}
    for e in examples:
        by_task.setdefault(e["task"], []).append(e["n_tokens"])
    return {
        "n": len(lens), "min": lens[0], "median": statistics.median(lens), "p95": pct(lens, 95),
        "max": lens[-1], "mean": round(statistics.mean(lens), 1), "total_tokens": sum(lens),
        "target_tokens": sum(e["n_target"] for e in examples),
        "over_max_seq_len": len(over),
        "over_ids": [{"id": e["id"], "n_tokens": e["n_tokens"]} for e in sorted(over, key=lambda e: e["id"])],
        "max_by_task": {t: max(v) for t, v in sorted(by_task.items())},
    }


def print_stats(name, st, max_len):
    log("%s: n=%d tokens min=%d median=%s p95=%d max=%d mean=%s total=%d (assistant/target %d); "
        "%d over --max-seq-len %d" % (name, st["n"], st["min"], st["median"], st["p95"], st["max"], st["mean"],
                                      st["total_tokens"], st["target_tokens"], st["over_max_seq_len"], max_len))
    for o in st["over_ids"]:
        log("  over: %s (%d tokens)" % (o["id"], o["n_tokens"]))


def show_masking(tok, ex):
    log("masking check for %s: %d tokens, %d unmasked" % (ex["id"], ex["n_tokens"], ex["n_target"]))
    first = next(i for i, l in enumerate(ex["labels"]) if l != -100)
    log("  masked prefix (last 40 chars): %r" % tok.decode(ex["input_ids"][:first])[-40:])
    decoded = tok.decode([t for t in ex["labels"] if t != -100], skip_special_tokens=False,
                         clean_up_tokenization_spaces=False)
    log("  unmasked labels decode to: %r" % decoded)
    log("  equals assistant content + end-of-turn: %s" % (decoded == ex["target_text"]))


# --------------------------------------------------------------------------- model

def versions():
    v = {"python": platform.python_version()}
    for mod in ("torch", "transformers", "peft", "accelerate", "bitsandbytes", "tokenizers", "safetensors"):
        try:
            v[mod] = __import__(mod).__version__
        except Exception as e:  # noqa: BLE001 -- recorded, not fatal
            v[mod] = "unavailable (%s)" % type(e).__name__
    try:
        import torch
        v["torch_hip"] = getattr(torch.version, "hip", None)
        v["torch_cuda"] = torch.version.cuda
    except Exception:  # noqa: BLE001
        pass
    return v


def bnb_usable():
    try:
        import bitsandbytes  # noqa: F401
        return True
    except Exception as e:  # noqa: BLE001
        log("bitsandbytes does not import: %s: %s" % (type(e).__name__, e))
        return False


def pick_dtype(torch, want, on_gpu):
    if want == "auto":
        if not on_gpu:
            return torch.float32, "fp32"
        if torch.cuda.is_bf16_supported():
            return torch.bfloat16, "bf16"
        return torch.float16, "fp16"
    return {"bf16": torch.bfloat16, "fp16": torch.float16, "fp32": torch.float32}[want], want


def dtype_kwarg(transformers, dtype):
    major = int(transformers.__version__.split(".")[0])
    return {"dtype": dtype} if major >= 5 else {"torch_dtype": dtype}


def load_base(args, torch, transformers, dtype, on_gpu):
    from transformers import AutoModelForCausalLM
    kw = dict(dtype_kwarg(transformers, dtype), trust_remote_code=args.trust_remote_code)
    if args.quant == "nf4":
        from transformers import BitsAndBytesConfig
        if not bnb_usable():
            die("--quant nf4 needs bitsandbytes; see SETUP.md fallback (a): --quant none "
                "--model Qwen/Qwen2.5-Coder-1.5B-Instruct")
        kw["quantization_config"] = BitsAndBytesConfig(
            load_in_4bit=True, bnb_4bit_quant_type="nf4", bnb_4bit_use_double_quant=True,
            bnb_4bit_compute_dtype=dtype)
    kw["device_map"] = {"": 0} if on_gpu else {"": "cpu"}
    return AutoModelForCausalLM.from_pretrained(args.model, **kw)


class ListDataset:
    def __init__(self, rows):
        self.rows = rows

    def __len__(self):
        return len(self.rows)

    def __getitem__(self, i):
        r = self.rows[i]
        return {"input_ids": r["input_ids"], "labels": r["labels"]}


class AssistantOnlyCollator:
    """Right-pads to the longest example in the batch. Padding gets label -100
    and attention 0; the -100 labels from encode() are kept as they are."""

    def __init__(self, pad_id):
        self.pad_id = pad_id

    def __call__(self, batch):
        import torch
        n = max(len(b["input_ids"]) for b in batch)
        ids, lab, att = [], [], []
        for b in batch:
            k = n - len(b["input_ids"])
            ids.append(list(b["input_ids"]) + [self.pad_id] * k)
            lab.append(list(b["labels"]) + [-100] * k)
            att.append([1] * len(b["input_ids"]) + [0] * k)
        return {"input_ids": torch.tensor(ids, dtype=torch.long),
                "labels": torch.tensor(lab, dtype=torch.long),
                "attention_mask": torch.tensor(att, dtype=torch.long)}


def example_loss(model, collator, ex, device):
    import torch
    batch = collator([{"input_ids": ex["input_ids"], "labels": ex["labels"]}])
    batch = {k: v.to(device) for k, v in batch.items()}
    model.eval()
    with torch.no_grad():
        return float(model(**batch).loss)


# --------------------------------------------------------------------------- main

def main(argv=None):
    args = parse_args(argv)
    try:
        import torch
        import transformers
        from transformers import AutoTokenizer, set_seed
    except ImportError as e:
        die("missing dependency (%s); see tools/finetune/train/SETUP.md" % e)

    set_seed(args.seed)
    if args.full_determinism:
        from transformers import enable_full_determinism
        enable_full_determinism(args.seed)
    os.makedirs(args.out, exist_ok=True)

    tok = AutoTokenizer.from_pretrained(args.model, trust_remote_code=args.trust_remote_code)
    if not getattr(tok, "is_fast", False):
        die("a fast tokenizer is required (offset mapping is how the assistant span is located)")
    if tok.chat_template is None:
        die("%s has no chat template; use an -Instruct model" % args.model)
    pad_id = tok.pad_token_id if tok.pad_token_id is not None else tok.eos_token_id
    if pad_id is None:
        die("tokenizer has neither a pad nor an eos token")

    train_recs = read_jsonl(args.train)
    val_recs = read_jsonl(args.validation) if args.validation else []
    if args.smoke:
        train_recs, val_recs = train_recs[:SMOKE_TRAIN], val_recs[:SMOKE_VAL]

    # 1. lengths, with the real tokenizer and template, before anything heavy
    try:
        train_ex = [encode(tok, r, args.system) for r in train_recs]
        val_ex = [encode(tok, r, args.system) for r in val_recs]
    except ValueError as e:
        die("masking: %s" % e)
    report = {"tokenizer": args.model, "max_seq_len": args.max_seq_len, "system": args.system,
              "train": length_stats(train_ex, args.max_seq_len)}
    if val_ex:
        report["validation"] = length_stats(val_ex, args.max_seq_len)
    print_stats("train", report["train"], args.max_seq_len)
    if val_ex:
        print_stats("validation", report["validation"], args.max_seq_len)
    with open(os.path.join(args.out, "length_report.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(report, f, indent=2, sort_keys=True, ensure_ascii=False)
        f.write("\n")
    if 0 <= args.show_example < len(train_ex):
        show_masking(tok, train_ex[args.show_example])
    if args.measure_only:
        return 0

    over = [e["id"] for e in train_ex + val_ex if e["n_tokens"] > args.max_seq_len]
    if over and args.on_overlength == "fail":
        die("%d record(s) exceed --max-seq-len %d (listed above). Nothing is truncated: raise "
            "--max-seq-len, or pass --on-overlength drop to train without them." % (len(over), args.max_seq_len))
    dropped = sorted(over)
    if dropped:
        log("dropping %d over-length record(s): %s" % (len(dropped), ", ".join(dropped)))
    train_ex = [e for e in train_ex if e["n_tokens"] <= args.max_seq_len]
    val_ex = [e for e in val_ex if e["n_tokens"] <= args.max_seq_len]
    if not train_ex:
        die("no training records left after dropping over-length ones")

    # 2. model
    on_gpu = torch.cuda.is_available()
    dtype, dtype_name = pick_dtype(torch, args.compute_dtype, on_gpu)
    device_desc = "cpu"
    if on_gpu:
        props = torch.cuda.get_device_properties(0)
        device_desc = "%s (%s, %.1f GiB)" % (props.name, getattr(props, "gcnArchName", "?"),
                                              props.total_memory / 2**30)
    log("device: %s; torch %s hip=%s; compute dtype %s; quant %s"
        % (device_desc, torch.__version__, getattr(torch.version, "hip", None), dtype_name, args.quant))
    if not on_gpu and not args.smoke:
        log("WARNING: no GPU visible; a full run on CPU will take a very long time")

    model = load_base(args, torch, transformers, dtype, on_gpu)
    model.config.use_cache = False
    gc_on = not args.no_gradient_checkpointing
    gc_kwargs = {"use_reentrant": False}
    from peft import LoraConfig, get_peft_model, prepare_model_for_kbit_training
    if args.quant == "nf4":
        model = prepare_model_for_kbit_training(model, use_gradient_checkpointing=gc_on,
                                                gradient_checkpointing_kwargs=gc_kwargs)
    elif gc_on:
        model.enable_input_require_grads()
    lcfg = LoraConfig(r=args.lora_r, lora_alpha=args.lora_alpha, lora_dropout=args.lora_dropout,
                      target_modules=LORA_TARGETS, bias="none", task_type="CAUSAL_LM")
    model = get_peft_model(model, lcfg)
    trainable = sum(p.numel() for p in model.parameters() if p.requires_grad)
    total = sum(p.numel() for p in model.parameters())
    log("trainable params %d of %d (%.3f%%)" % (trainable, total, 100.0 * trainable / total))

    if args.optim == "auto":
        optim = "paged_adamw_8bit" if (on_gpu and bnb_usable()) else "adamw_torch"
    else:
        optim = args.optim
    grad_accum = 1 if args.smoke else args.grad_accum
    steps_per_epoch = math.ceil(len(train_ex) / (args.batch_size * grad_accum))
    if args.max_steps is not None:
        max_steps = args.max_steps
    elif args.smoke:
        max_steps = SMOKE_STEPS
    else:
        max_steps = math.ceil(steps_per_epoch * args.epochs)
    warmup_steps = math.ceil(args.warmup_ratio * max_steps)

    from transformers import Trainer, TrainingArguments
    targs = TrainingArguments(
        output_dir=os.path.join(args.out, "trainer_scratch"),
        per_device_train_batch_size=args.batch_size,
        per_device_eval_batch_size=1,
        gradient_accumulation_steps=grad_accum,
        max_steps=max_steps,
        learning_rate=args.lr,
        lr_scheduler_type="cosine",
        warmup_steps=warmup_steps,
        weight_decay=0.0,
        max_grad_norm=1.0,
        optim=optim,
        bf16=(on_gpu and dtype_name == "bf16"),
        fp16=(on_gpu and dtype_name == "fp16"),
        gradient_checkpointing=gc_on,
        gradient_checkpointing_kwargs=gc_kwargs if gc_on else None,
        logging_steps=args.logging_steps,
        logging_first_step=True,
        eval_strategy=("steps" if args.smoke else "epoch") if val_ex else "no",
        eval_steps=(max_steps if args.smoke else None),
        save_strategy="no",
        report_to="none",
        seed=args.seed,
        data_seed=args.seed,
        remove_unused_columns=False,
        dataloader_num_workers=0,
        dataloader_pin_memory=on_gpu,
        use_cpu=not on_gpu,
        full_determinism=args.full_determinism,
    )
    collator = AssistantOnlyCollator(pad_id)
    trainer = Trainer(model=model, args=targs, data_collator=collator,
                      train_dataset=ListDataset(train_ex),
                      eval_dataset=ListDataset(val_ex) if val_ex else None)
    log("training: %d records, %d optimizer steps (batch %d x accum %d), warmup %d, optim %s"
        % (len(train_ex), max_steps, args.batch_size, grad_accum, warmup_steps, optim))
    result = trainer.train()

    history = trainer.state.log_history
    losses = [h["loss"] for h in history if "loss" in h]
    bad = [x for x in losses if not math.isfinite(x)]
    with open(os.path.join(args.out, "trainer_log.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"log_history": history, "train_metrics": result.metrics}, f, indent=2, sort_keys=True)
        f.write("\n")
    log("train loss per logged step: %s" % ", ".join("%.4f" % x for x in losses))
    if bad:
        die("non-finite training loss (%d of %d logged steps); adapter NOT saved. On fp16 try "
            "--compute-dtype bf16 or fp32, or lower --lr" % (len(bad), len(losses)), code=3)

    # 3. adapter only, plus the record of how it was made
    model.save_pretrained(args.out)
    run_config = {
        "args": vars(args),
        "derived": {"compute_dtype": dtype_name, "device": device_desc, "optim": optim,
                    "grad_accum": grad_accum, "max_steps": max_steps, "warmup_steps": warmup_steps,
                    "steps_per_epoch": steps_per_epoch, "lr_scheduler": "cosine",
                    "lora_target_modules": LORA_TARGETS, "gradient_checkpointing": gc_on,
                    "trainable_params": trainable, "total_params": total,
                    "train_records": len(train_ex), "validation_records": len(val_ex),
                    "dropped_overlength": dropped, "pad_token_id": pad_id,
                    "chat_template_sha256": hashlib.sha256(tok.chat_template.encode("utf-8")).hexdigest(),
                    "fixed_date_string": FIXED_DATE_STRING,
                    "loss_masking": "assistant content + end-of-turn token only; all else -100"},
        "data_sha256": {"train": sha256_file(args.train),
                        "validation": sha256_file(args.validation) if args.validation else None},
        "versions": versions(),
    }
    with open(os.path.join(args.out, "run_config.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(run_config, f, indent=2, sort_keys=True, ensure_ascii=False)
        f.write("\n")
    log("saved adapter + run_config.json to %s" % args.out)

    if args.smoke:
        # the adapter on disk must reproduce the model in memory
        dev = next(model.parameters()).device
        ex = train_ex[0]
        mem = example_loss(model, collator, ex, dev)
        del trainer, model
        from peft import PeftModel
        base = load_base(args, torch, transformers, dtype, on_gpu)
        re_model = PeftModel.from_pretrained(base, args.out)
        disk = example_loss(re_model, collator, ex, dev)
        base2 = load_base(args, torch, transformers, dtype, on_gpu)
        plain = example_loss(base2, collator, ex, dev)
        log("reload check on %s: loss in memory %.6f, reloaded adapter %.6f, base without adapter %.6f"
            % (ex["id"], mem, disk, plain))
        if not math.isclose(mem, disk, rel_tol=1e-4, abs_tol=1e-4):
            die("reloaded adapter does not reproduce the trained model", code=4)
        log("reload check: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
