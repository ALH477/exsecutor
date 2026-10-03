#!/usr/bin/env python3
# tools/finetune/make_eval_prompts.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# Step 1 of the held-out evaluation: turn the CODE records of
# datasets/finetune/validation.jsonl into eval prompts, each carrying what
# the compiler must see for the answer to count. The reference answer is
# deliberately NOT written out, so a generator reading this file cannot leak
# it into a generation.
#
# Kept: write / translate / fix records whose evidence is exsc-run,
# exsc-check or exsc-diagnostic, and trace records whose case declares
# exit=N. Dropped (listed on stderr): every other task, and trace records for
# abort= cases (the "what happens" question has no single number to compare).
#
# Deterministic: items follow validation.jsonl's own order; no clock.
# ---------------------------------------------------------------------------
"""Build tools/finetune/out/eval_prompts.jsonl from the held-out code records.

Each output line: {id, group, task, evidence, prompt, messages, score, dataset_sha256}.
`prompt` is the dataset's user message verbatim; `messages` is the same as a
one-turn chat (add your own system message if your template wants one).
`score` is what score.py checks:

  write / translate  {"kind": "program", "expect": "ok" | "exit=N" | "abort=N", "stdout": str|null}
  fix                {"kind": "fix", "broken": src, "codes": [...], "primary": code,
                      "fixed_expect": "ok" | "exit=N" | "abort=N", "fixed_stdout": str|null}
  trace              {"kind": "trace_exit", "exit": N}
"""

import argparse
import hashlib
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness  # noqa: E402

PROG = "make_eval_prompts"
CODE_EVIDENCE = {"exsc-run", "exsc-check", "exsc-diagnostic"}
RUNNABLE = re.compile(r"ok|exit=\d+|abort=\d+")


def unescape_stdout(s):
    # gen-finetune.py's rule for the `stdout:` meta value: a literal \n is a newline
    return None if s is None else s.replace("\\n", "\n")


def build(validation, cases_dir):
    gen = harness.load_generator()
    with open(validation, "rb") as f:
        ds_sha = hashlib.sha256(f.read()).hexdigest()
    items, skipped = [], []
    for r in harness.read_jsonl(validation):
        task, ev = r["task"], r["meta"]["evidence"]
        if task not in ("write", "translate", "fix", "trace") or ev not in CODE_EVIDENCE:
            skipped.append((r["id"], "task=%s evidence=%s" % (task, ev)))
            continue
        group = r["meta"]["group"]
        path = os.path.join(cases_dir, group + ".exsc")
        if not os.path.exists(path):
            harness.die(PROG, "%s: group %s has no case file %s" % (r["id"], group, path))
        case = gen.parse_case(path)
        meta = case["meta"]
        if task in ("write", "translate"):
            if meta["task"] == "fix":  # the `.ask` record: its program is the fixed twin
                expect, stdout = meta.get("fixed-expect", "ok"), meta.get("fixed-stdout")
            else:
                expect, stdout = meta["expect"], meta.get("stdout")
            if not RUNNABLE.fullmatch(expect):
                harness.die(PROG, "%s: a %s record whose case expects %r" % (r["id"], task, expect))
            score = {"kind": "program", "expect": expect, "stdout": unescape_stdout(stdout)}
        elif task == "fix":
            if case["fixed"] is None:
                harness.die(PROG, "%s: fix case with no //@ fixed twin" % r["id"])
            codes = r["meta"].get("codes") or []
            score = {"kind": "fix", "broken": case["code"], "codes": codes,
                     "primary": meta.get("primary", codes[0] if codes else None),
                     "fixed_expect": meta.get("fixed-expect", "ok"),
                     "fixed_stdout": unescape_stdout(meta.get("fixed-stdout"))}
        else:  # trace
            m = re.fullmatch(r"exit=(\d+)", meta["expect"])
            if not m:
                skipped.append((r["id"], "trace of a %s case (no exit number to compare)" % meta["expect"]))
                continue
            score = {"kind": "trace_exit", "exit": int(m.group(1))}
        prompt = r["messages"][0]["content"]
        items.append({"id": r["id"], "group": group, "task": task, "evidence": ev,
                      "prompt": prompt, "messages": [{"role": "user", "content": prompt}],
                      "score": score, "dataset_sha256": ds_sha})
    return items, skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--validation", default=harness.VALIDATION, help="held-out records (default: %(default)s)")
    ap.add_argument("--cases", default=harness.CASES, help="case files (default: %(default)s)")
    ap.add_argument("-o", "--out", default=os.path.join(harness.OUT, "eval_prompts.jsonl"),
                    help="output JSONL (default: %(default)s)")
    ap.add_argument("-q", "--quiet", action="store_true", help="do not list skipped records")
    a = ap.parse_args()
    items, skipped = build(a.validation, a.cases)
    harness.write_text(a.out, "".join(harness.dumps(i) + "\n" for i in items))
    by = {}
    for i in items:
        by[i["task"]] = by.get(i["task"], 0) + 1
    if not a.quiet:
        for rid, why in skipped:
            sys.stderr.write("%s: skipped %s (%s)\n" % (PROG, rid, why))
    print("%s: %d eval items (%s) -> %s; %d held-out records skipped" % (
        PROG, len(items), ", ".join("%s %d" % kv for kv in sorted(by.items())),
        os.path.relpath(a.out, harness.ROOT), len(skipped)))


if __name__ == "__main__":
    main()
