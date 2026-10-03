#!/usr/bin/env python3
# tools/finetune/selftest.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
# ---------------------------------------------------------------------------
# Proves the harness, not a model. No model is involved:
#
#  (i)  the dataset's own reference answers, fed in as generations, must pass
#       every metric (they were compiler-verified when the dataset was built;
#       if one fails here, either the scorer or the dataset is wrong, and this
#       says which item);
#  (ii) deliberately broken generations must FAIL, each with the expected
#       failure reason -- a scorer that passes everything is worse than none;
#  (iii) scoring is deterministic: the same input scores to identical bytes;
#  (iv) compare.py runs on (reference, broken) and prints its paired table.
#
# It drives the three scripts through their command lines, exactly as a
# user would. Outputs go to tools/finetune/out/selftest/ (gitignored).
# Exit 0 only if every check holds.
# ---------------------------------------------------------------------------
"""Self-test of the eval harness with no model: reference answers must pass, broken ones must fail."""

import argparse
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness  # noqa: E402

PY = sys.executable or "python3"
WRONG_PROGRAM = "publica functio initium(m: Mundus) -> u8 {\n    redde 7;\n}\n"


def sh(args):
    r = subprocess.run([PY, "-B"] + args, capture_output=True, text=True, cwd=harness.ROOT)
    if r.returncode != 0:
        sys.stdout.write(r.stdout + r.stderr)
        raise SystemExit("selftest: command failed: %s" % " ".join(args))
    return r.stdout


def fenced_spans(text, which):
    ms = list(re.finditer(r"```exsecutor\n(.*?)```", text, re.S))
    if not ms:
        return None
    return ms[0] if which == "first" else ms[-1]


def replace_block(text, which, fn):
    """Apply fn to the judged code block of a reference answer; None if fn declines."""
    m = fenced_spans(text, which)
    if not m:
        return None
    new = fn(m.group(1))
    if new is None:
        return None
    return text[:m.start(1)] + new + text[m.end(1):]


def m_strip_semicolon(code):
    return code.replace(";", "", 1) if ";" in code else None


def m_si_to_if(code):
    return re.sub(r"\bsi\b", "if", code, count=1) if re.search(r"\bsi\b", code) else None


def m_drop_last_brace(code):
    i = code.rfind("}")
    return code[:i] + code[i + 1:] if i >= 0 else None


def mutations(item, ref):
    """Yield (mutation name, generation text, {metric: expected value}) for one item."""
    s = item["score"]
    kind = s["kind"]
    if kind == "trace_exit":
        yield "empty", "", {"answer": False, "reason": "no_number"}
        yield "trace_wrong_number", re.sub(r"\b%d\b" % s["exit"], str(s["exit"] + 1), ref), \
            {"answer": False, "reason": "wrong_exit"}
        return
    which = "last" if kind == "fix" else "first"
    for name, fn in (("strip_semicolon", m_strip_semicolon), ("si_to_if", m_si_to_if),
                     ("drop_last_brace", m_drop_last_brace)):
        t = replace_block(ref, which, fn)
        if t is not None:
            yield name, t, {"check": False, "reason": "check_failed"}
    yield "empty", "", {"check": False, "reason": "no_code_block"}
    yield "no_fence", re.sub(r"```[^\n]*\n?", "", ref), {"check": False, "reason": "no_code_block"}
    expect = s.get("expect") if kind == "program" else s.get("fixed_expect")
    if expect != "ok":
        t = replace_block(ref, which, lambda c: WRONG_PROGRAM)
        want_reason = "wrong_exit" if expect.startswith("exit=") else "wrong_abort"
        yield "plausible_wrong_program", t, {"check": True, "run": False, "reason": want_reason}
    if kind == "fix":
        yield "fix_unfixed", replace_block(ref, "last", lambda c: s["broken"]), \
            {"check": False, "reason": "check_failed"}
        other = "EXS-E0301" if s["primary"] == "EXS-E0201" else "EXS-E0201"
        yield "fix_wrong_code_named", ref.replace(s["primary"], other), \
            {"named_primary": False, "check": True, "pass": True}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=os.path.join(harness.OUT, "selftest"),
                    help="scratch output directory (default: %(default)s)")
    ap.add_argument("--no-run", action="store_true", help="pass --no-run to score.py (skips run-level checks)")
    a = ap.parse_args()
    out = a.out
    os.makedirs(out, exist_ok=True)
    prompts = os.path.join(out, "eval_prompts.jsonl")
    extra = ["--no-run"] if a.no_run else []
    failures = []

    print("== make_eval_prompts")
    sys.stdout.write(sh(["tools/finetune/make_eval_prompts.py", "-q", "-o", prompts]))
    items = harness.read_jsonl(prompts)
    refs = {r["id"]: r["messages"][1]["content"] for r in harness.read_jsonl(harness.VALIDATION)}
    by = {}
    for i in items:
        by[i["task"]] = by.get(i["task"], 0) + 1
    print("items by task: " + ", ".join("%s %d" % kv for kv in sorted(by.items())))

    # (i) reference answers
    print("\n== (i) dataset reference answers as generations")
    gt = os.path.join(out, "gen_reference.jsonl")
    harness.write_text(gt, "".join(harness.dumps({"id": i["id"], "text": refs[i["id"]]}) + "\n" for i in items))
    gt_res = os.path.join(out, "gen_reference.results.jsonl")
    sys.stdout.write(sh(["tools/finetune/score.py", gt, "--prompts", prompts, "-o", gt_res,
                         "--label", "reference"] + extra))
    for r in harness.read_jsonl(gt_res):
        for k in ("pass", "check", "run", "named_primary", "answer"):
            if r.get(k) is False:
                failures.append("reference %s: %s is False (reason %s)" % (r["id"], k, r.get("reason")))

    # (iii) determinism
    gt_res2 = os.path.join(out, "gen_reference.results.again.jsonl")
    sh(["tools/finetune/score.py", gt, "--prompts", prompts, "-o", gt_res2] + extra)
    same = open(gt_res, "rb").read() == open(gt_res2, "rb").read()
    print("\n== (iii) determinism: second scoring byte-identical: %s" % ("yes" if same else "NO"))
    if not same:
        failures.append("scoring the same generations twice gave different bytes")

    # (ii) broken generations
    print("\n== (ii) deliberately broken generations (each must fail as stated)")
    plan = {}
    for i in items:
        for name, text, want in mutations(i, refs[i["id"]]):
            plan.setdefault(name, []).append((i["id"], text, want))
    print("  %-26s %3s %9s  %s" % ("mutation", "n", "as-wanted", "expected"))
    worst = None
    for name in sorted(plan):
        rows = plan[name]
        gpath = os.path.join(out, "gen_%s.jsonl" % name)
        rpath = os.path.join(out, "gen_%s.results.jsonl" % name)
        harness.write_text(gpath, "".join(harness.dumps({"id": rid, "text": t}) + "\n" for rid, t, _ in rows))
        sh(["tools/finetune/score.py", gpath, "--prompts", prompts, "-o", rpath] + extra)
        res = {r["id"]: r for r in harness.read_jsonl(rpath)}
        ok = 0
        for rid, _, want in rows:
            got = res[rid]
            if a.no_run and "run" in want:  # run level not exercised: its verdict and reason are n/a
                want = {k: v for k, v in want.items() if k not in ("run", "reason")}
            bad = {k: (v, got.get(k)) for k, v in want.items() if got.get(k) != v}
            if bad:
                failures.append("%s on %s: wanted/got %s" % (name, rid, bad))
            else:
                ok += 1
        exp = " | ".join(sorted({", ".join("%s=%s" % kv for kv in sorted(w.items())) for _, _, w in rows}))
        print("  %-26s %3d %9s  %s" % (name, len(rows), "%d/%d" % (ok, len(rows)), exp))
        if name == "strip_semicolon":
            worst = rpath

    # (iv) compare.py on reference vs one broken set (missing items count as failures)
    if worst:
        print("\n== (iv) compare.py base=strip_semicolon, tuned=reference (smoke test of the paired report;")
        print("   items with no semicolon to strip are absent from that file and score as missing_generation)")
        sys.stdout.write(sh(["tools/finetune/compare.py", worst, gt_res]))

    print()
    if failures:
        print("SELFTEST FAILED (%d):" % len(failures))
        for f in failures:
            print("  - " + f)
        sys.exit(1)
    print("SELFTEST PASSED: references pass every metric, every mutation failed as expected, scoring is deterministic.")


if __name__ == "__main__":
    main()
