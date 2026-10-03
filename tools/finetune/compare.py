#!/usr/bin/env python3
# tools/finetune/compare.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# Step 3 of the held-out evaluation: a PAIRED comparison of two score.py
# results files over the same eval items (normally: base model, then base +
# adapter). Per item and per metric: both pass, only tuned, only base,
# neither; and the exact McNemar p-value on the discordant pairs, which is
# the right test for two classifiers judged on the same items.
# ---------------------------------------------------------------------------
"""Paired comparison of two score.py results files (base vs tuned), with exact McNemar.

The concordant items (both pass, neither passes) carry no information about
which model is better; only the discordant ones do, and with this eval's
handful of held-out items there are very few of those. With d discordant
items the smallest two-sided exact p-value possible is 2 * 0.5**d: d = 5 can
never reach p < 0.05 however lopsided the split.
"""

import argparse
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness  # noqa: E402

METRICS = ("pass", "check", "run", "named_primary", "answer")


def cell(v):
    return {True: "pass", False: "FAIL", None: "-"}[v]


def pair(b, t):
    return "-" if b is None and t is None else "%s>%s" % (cell(b), cell(t))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base", help="score.py results JSONL for the base model")
    ap.add_argument("tuned", help="score.py results JSONL for the fine-tuned model")
    ap.add_argument("--metric", action="append", choices=METRICS,
                    help="metric(s) to compare (default: all that apply)")
    a = ap.parse_args()

    base = {r["id"]: r for r in harness.read_jsonl(a.base)}
    tuned = {r["id"]: r for r in harness.read_jsonl(a.tuned)}
    if set(base) != set(tuned):
        harness.die("compare", "the two files score different items: only-base %s, only-tuned %s" % (
            sorted(set(base) - set(tuned)), sorted(set(tuned) - set(base))))
    ids = [r["id"] for r in harness.read_jsonl(a.base)]  # base file order
    metrics = a.metric or list(METRICS)

    print("per item (base -> tuned); reason is the tuned run's failure reason")
    print("  %-40s %-9s %-11s %-11s %s" % ("id", "task", "pass", "check", "tuned reason"))
    for i in ids:
        b, t = base[i], tuned[i]
        print("  %-40s %-9s %-11s %-11s %s" % (
            i, t.get("task", "?"),
            pair(b.get("pass"), t.get("pass")),
            pair(b.get("check"), t.get("check")),
            t.get("reason") or ""))
    print()
    print("  %-14s %3s %10s %10s %10s %8s %10s" % ("metric", "n", "both pass", "only tuned", "only base",
                                                 "neither", "McNemar p"))
    for m in metrics:
        pairs = [(base[i].get(m), tuned[i].get(m)) for i in ids]
        pairs = [(x, y) for x, y in pairs if x is not None and y is not None]
        if not pairs:
            continue
        both = sum(1 for x, y in pairs if x and y)
        only_t = sum(1 for x, y in pairs if y and not x)
        only_b = sum(1 for x, y in pairs if x and not y)
        neither = sum(1 for x, y in pairs if not x and not y)
        p = harness.mcnemar_exact(only_t, only_b)
        print("  %-14s %3d %10d %10d %10d %8d %10.4f" % (m, len(pairs), both, only_t, only_b, neither, p))
    print()
    n = len(ids)
    groups = len({base[i].get("group") for i in ids})
    print("CAVEAT: %d held-out items from %d case groups (items of one group share a program, so they" % (n, groups))
    print("are not independent, which McNemar assumes). McNemar uses only the discordant items; with d of them the")
    print("smallest possible two-sided p is 2*0.5^d (d=5 -> 0.0625). A non-significant result here")
    print("means \"this eval is too small to tell\", not \"the adapter did nothing\"; a significant one")
    print("on a handful of items is still a handful of items. Report k/n per item, not a percentage alone.")


if __name__ == "__main__":
    main()
