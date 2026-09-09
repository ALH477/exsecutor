#!/usr/bin/env python3
# tools/ucd-gen/ntbin.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version. See LICENSE. Code produced by this compiler is not
# covered by the GPL -- see Exception A in LICENSE.EXCEPTION.
# ---------------------------------------------------------------------------
# Turns UCD `NormalizationTest.txt` into a flat binary the ASSEMBLY NFC
# implementation can be driven with, so that compiler/shared/unicode/nfc.inc
# is checked against the real conformance corpus rather than against a
# handful of hand-picked cases.
#
# WHY THIS EXISTS SEPARATELY FROM gen.py. `gen.py --check-only` already runs
# NormalizationTest.txt, but against its own PYTHON reference implementation.
# That validates the tables. It says nothing about the assembly that reads
# them, which is a different program and is the one that ships. This script
# plus `nfc_conformance.asm` beside it close that gap.
#
# VERIFICATION-ONLY, like everything else in this directory. Never on the
# build path (spec §18.1: `make` needs only fasmg), and the blob it writes is
# NOT checked in -- it is several megabytes of derived data with a two-command
# recipe, which is the case ADR 0004 already decided against vendoring.
#
# Usage:
#   python3 tools/ucd-gen/ntbin.py --ucd <ucd-dir> --out <blob path>
#   fasmg -i "NT_BLOB equ '<blob path>'" tools/ucd-gen/nfc_conformance.asm <exe>
#   <exe>; echo $?      # counters are written to stdout as 8 little-endian u64
#
# `nfc_conformance.asm`'s header has the full recipe and decodes the counters.
#
# Blob format, little-endian throughout:
#   u32 record_count
#   then record_count records, each:
#     u32 srclen                 codepoints
#     u32 explen                 codepoints
#     u32 src[srclen]            the input sequence
#     u32 exp[explen]            its NFC
#
# The five assertions NormalizationTest.txt states per line -- NFC(c1) =
# NFC(c2) = NFC(c3) = c2 and NFC(c4) = NFC(c5) = c4 -- become five records, so
# a record count of 5x the line count is expected, not a bug.
#
# Spec: docs/spec/exsecutor-spec-v0.4.md §8.1
# ---------------------------------------------------------------------------
"""Emit NormalizationTest.txt as a flat binary for the assembly NFC driver."""

import argparse
import os
import struct
import sys


def parse(path):
    """NormalizationTest.txt -> [(src, expected), ...], in file order.

    File order, not sorted and not de-duplicated: the driver reports the index
    of its first failing record, and that number is only useful if the same
    input always produces the same numbering. Nothing here may depend on set
    or dict iteration order (CLAUDE.md, "Determinism is not optional").
    """
    out = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            s = line.split("#", 1)[0].strip()
            if not s or s.startswith("@"):
                continue
            cols = [c.strip() for c in s.split(";")[:5]]
            if len(cols) < 5:
                continue
            c = [[int(x, 16) for x in col.split()] for col in cols]
            out.append((c[0], c[1]))
            out.append((c[1], c[1]))
            out.append((c[2], c[1]))
            out.append((c[3], c[3]))
            out.append((c[4], c[3]))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ucd", required=True, help="directory holding UCD .txt files")
    ap.add_argument("--out", required=True, help="path of the blob to write")
    a = ap.parse_args()

    records = parse(os.path.join(a.ucd, "NormalizationTest.txt"))
    longest = max((len(s) for s, _ in records), default=0)
    longest = max(longest, max((len(e) for _, e in records), default=0))

    buf = [struct.pack("<I", len(records))]
    for src, exp in records:
        buf.append(struct.pack("<II", len(src), len(exp)))
        buf.append(b"".join(struct.pack("<I", c) for c in src))
        buf.append(b"".join(struct.pack("<I", c) for c in exp))
    data = b"".join(buf)
    with open(a.out, "wb") as fh:
        fh.write(data)

    print(f"records        {len(records)}")
    print(f"longest seq    {longest} codepoints")
    print(f"blob           {len(data)} bytes -> {a.out}")
    # nfc_conformance.asm sizes its buffers from this number; if the corpus
    # ever grows past it the driver must be widened rather than silently
    # overrun, so say so loudly here rather than leaving it to be discovered.
    if longest * 4 > 1024:
        print("ERROR: longest sequence exceeds nfc_conformance.asm's "
              "NT_CAP (1024 codepoints); widen it before trusting a run",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
