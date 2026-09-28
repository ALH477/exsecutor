#!/usr/bin/env python3
# tools/harvest-lexicon.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
#
# Harvest public FN/STRUCT/IFACE/TYPUS names from examples/ and
# tests/programs/ (and optionally tests/conformance/) into loan lines for
# lexicon.norma. initium is reserved (spec §4.7) and is never a loan.
# Conformance entry 14's `lector` as functio must keep decomposing as -or
# so it is NOT listed as a FN loan even if it appears.
"""Emit L-lines for lexicon.norma from publica declarations."""

import argparse
import os
import re
import sys

DECL_RE = re.compile(
    r"publica\s+(functio|structura|interfacies|typus)\s+"
    r"([A-Za-z_][A-Za-z0-9_]*)",
)

KIND = {
    "functio": "FN",
    "structura": "STRUCT",
    "interfacies": "IFACE",
    "typus": "TYPUS",
}


def walk(paths):
    files = []
    for p in paths:
        if os.path.isfile(p) and (p.endswith(".exsc") or p.endswith(".asm")):
            files.append(p)
            continue
        for root, _dirs, names in os.walk(p):
            for n in names:
                if n.endswith(".exsc") or n.endswith(".asm"):
                    files.append(os.path.join(root, n))
    files.sort()
    return files


def harvest(paths):
    # (base, kind) -> first path, insertion order
    seen = {}
    order = []
    for path in walk(paths):
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read().replace("\r", "")
        for kind_word, name in DECL_RE.findall(text):
            if not name.isascii():
                continue
            base = name.split("_", 1)[0]
            if base == "initium":
                continue
            kind = KIND[kind_word]
            key = (base, kind)
            if key in seen:
                continue
            seen[key] = path
            order.append(key)
    return order


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paths", nargs="+")
    a = ap.parse_args()
    for base, kind in harvest(a.paths):
        print(f"L {base} {kind}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
