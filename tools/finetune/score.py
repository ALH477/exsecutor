#!/usr/bin/env python3
# tools/finetune/score.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
# ---------------------------------------------------------------------------
# Step 2 of the held-out evaluation: judge a model's generations WITH THE
# REAL COMPILER (build/exsc, then fasmg, then the binary) and nothing else.
# No string similarity to the reference, no model-as-judge.
#
# Levels are reported separately and never merged:
#   check         the extracted program type-checks clean (exsc exit 0, no
#                 diagnostic). For write/translate and for a fix's corrected
#                 program.
#   run           only where the case declares exit=N / abort=N: the program
#                 was lowered, assembled, executed, and its exit status (or
#                 `abortus N` + SIGILL) and stdout matched.
#   named_primary fix only: the answer's text names the case's primary
#                 EXS-E code. Separate from `check`; it is about the prose.
#   answer        trace only: the exit status stated in the prose equals N.
#   pass          one headline per item: run if the item has a run level,
#                 else check (fix: check and run); trace: answer.
#
# A generation that is missing counts as a failure (`missing_generation`), so
# a generator that skips hard items cannot raise its own score.
# ---------------------------------------------------------------------------
"""Score generations.jsonl ({id, text}) against eval_prompts.jsonl with the real compiler.

Code extraction: the first ```exsecutor fenced block, falling back to the
first fenced block of any language; none at all is the failure
`no_code_block`. EXCEPTION, on purpose: for `fix` items the LAST block is
judged, because an answer to "what is wrong and how do I fix it?" commonly
quotes the broken program before the corrected one, and judging the quote
would penalise exactly the answers that are correct (--fix-block first
restores the literal rule).

Writes one JSON result per item (input order) and prints a summary with
Wilson 95% intervals. The sample is tiny; the summary says so.
"""

import argparse
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness  # noqa: E402

PROG = "score"


def score_program(j, text, expect, stdout, which):
    res = {"check": False, "run": None, "reason": None}
    code, src = harness.extract_code(text, which)
    res["code_source"] = src
    if code is None:
        res["reason"] = "no_code_block"
        if expect != "ok":
            res["run"] = False
        return res
    c = j.check(code)
    res["exsc_rc"], res["codes"], res["diagnostics"] = c["exsc_rc"], c["codes"], c["diagnostics"]
    res["check"] = c["clean"]
    if not c["clean"]:
        res["reason"] = c["error"] or "check_failed"
    if expect != "ok":
        if not c["clean"]:
            res["run"] = False
        else:
            r = j.run(code, expect, stdout)
            res["run"], res["run_exit"], res["run_stdout"] = r["pass"], r["exit"], r["stdout"]
            if r["pass"] is False:
                res["reason"] = r["reason"]
            elif r["pass"] is None:
                res["run_skipped"] = r["reason"]
    return res


def score_item(j, item, text, fix_block):
    s = item["score"]
    out = {"id": item["id"], "group": item["group"], "task": item["task"]}
    if text is None:
        out.update({"reason": "missing_generation", "pass": False})
        if s["kind"] == "trace_exit":
            out["answer"] = False
        else:
            out["check"] = False
            if (s.get("expect") or s.get("fixed_expect")) != "ok":
                out["run"] = False
            if s["kind"] == "fix":
                out["named_primary"] = False
        return out
    if s["kind"] == "program":
        out.update(score_program(j, text, s["expect"], s["stdout"], "first"))
        if s["expect"] == "ok":
            out["pass"] = out["check"]
        else:  # the headline needs the run; under --no-run a clean check is n/a, not a pass
            out["pass"] = out["run"] if out["run"] is not None else (None if out["check"] else False)
    elif s["kind"] == "fix":
        r = score_program(j, text, s["fixed_expect"], s["fixed_stdout"], fix_block)
        out.update(r)
        named = sorted(set(harness.CODE_RX.findall(text)))
        out["codes_named"] = named
        out["named_primary"] = s["primary"] in named
        out["pass"] = bool(r["check"]) and (r["run"] is not False)
        if out["run"] is None and s["fixed_expect"] != "ok":
            out["pass"] = None  # --no-run on an item whose headline needs a run
    elif s["kind"] == "trace_exit":
        n, method = harness.extract_exit_number(text)
        out["answer_number"], out["answer_method"] = n, method
        out["answer"] = n == s["exit"]
        out["reason"] = None if out["answer"] else ("no_number" if n is None else "wrong_exit")
        out["pass"] = out["answer"]
    else:
        raise SystemExit("unknown score kind %r" % s["kind"])
    if out.get("pass") is False and not out.get("reason"):
        out["reason"] = "failed"
    if out.get("check") and out.get("pass"):
        out["reason"] = None
    return out


def summarise(items, results, label, ran=True):
    by_id = {r["id"]: r for r in results}
    lines = []

    def row(name, rs):
        rs = [r for r in rs if r is not None]
        k = sum(1 for r in rs if r is True)
        lines.append("  %-44s %s" % (name, harness.fmt_rate(k, len(rs))))

    def sel(task_set, key, need_run=False):
        out = []
        for i in items:
            if i["task"] not in task_set:
                continue
            s = i["score"]
            if need_run and (s.get("expect") or s.get("fixed_expect")) == "ok":
                continue
            out.append(by_id[i["id"]].get(key))
        return out

    n = len(items)
    lines.append("summary%s: %d items, judged by build/exsc%s" % (
        " [" + label + "]" if label else "", n,
        " (+ fasmg + execution for exit=/abort=)" if ran else " only (--no-run: run level and its headlines n/a)"))
    lines.append("  %-44s %s" % ("metric", "k/n    rate   Wilson 95% interval"))
    row("write+translate: checks clean", sel({"write", "translate"}, "check"))
    row("write+translate: runs, exit+stdout match", sel({"write", "translate"}, "run", need_run=True))
    row("fix: corrected program checks clean", sel({"fix"}, "check"))
    if sel({"fix"}, "run", need_run=True):
        row("fix: corrected program runs, matches", sel({"fix"}, "run", need_run=True))
    row("fix: names the primary EXS-E code (prose)", sel({"fix"}, "named_primary"))
    row("trace: states the right exit status", sel({"trace"}, "answer"))
    row("headline pass (all items)", [by_id[i["id"]].get("pass") for i in items])
    reasons = {}
    for r in results:
        if r.get("reason"):
            reasons[r["reason"]] = reasons.get(r["reason"], 0) + 1
    if reasons:
        lines.append("  failure reasons: " + ", ".join("%s %d" % kv for kv in sorted(reasons.items())))
    lines.append("  NOTE: n is tiny (%d items). An interval this wide cannot separate small effects;" % n)
    lines.append("  read compare.py's paired table, not a difference of two rates. `checks clean` on an")
    lines.append("  expect=ok item means the program is valid Exsecutor, NOT that it does what was asked.")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("generations", help="JSONL of {\"id\": ..., \"text\": ...}, one per eval item")
    ap.add_argument("--prompts", default=os.path.join(harness.OUT, "eval_prompts.jsonl"),
                    help="from make_eval_prompts.py (default: %(default)s)")
    ap.add_argument("-o", "--out", help="per-item results JSONL (default: <generations>.results.jsonl)")
    ap.add_argument("--summary", help="also write the summary text to this file")
    ap.add_argument("--label", default="", help="a name printed in the summary (e.g. base, tuned)")
    ap.add_argument("--fix-block", choices=("first", "last"), default="last",
                    help="which fenced block of a fix answer is the corrected program (default: %(default)s)")
    ap.add_argument("--exsc", help="compiler (default: build/exsc)")
    ap.add_argument("--fasmg", help="assembler (default: PATH, then /nix/store/*fasmg*/bin/fasmg)")
    ap.add_argument("--no-run", action="store_true",
                    help="never execute model programs; run-level metrics become n/a")
    a = ap.parse_args()

    items = harness.read_jsonl(a.prompts)
    known = {i["id"] for i in items}
    gens = {}
    for g in harness.read_jsonl(a.generations):
        gid = g.get("id")
        if gid not in known:
            harness.die(PROG, "generation for unknown id %r (wrong eval_prompts.jsonl?)" % gid)
        if gid in gens:
            harness.die(PROG, "two generations for id %r; score one sample per file" % gid)
        t = g.get("text")
        gens[gid] = t if isinstance(t, str) else ""
    j = harness.Judge(exsc=a.exsc, fasmg=a.fasmg, run=not a.no_run)
    try:
        results = [score_item(j, i, gens.get(i["id"]), a.fix_block) for i in items]
    finally:
        j.close()
    out = a.out or (os.path.splitext(a.generations)[0] + ".results.jsonl")
    harness.write_text(out, "".join(harness.dumps(r) + "\n" for r in results))
    summary = summarise(items, results, a.label, ran=not a.no_run)
    if a.summary:
        harness.write_text(a.summary, summary)
    sys.stdout.write(summary)


if __name__ == "__main__":
    main()
