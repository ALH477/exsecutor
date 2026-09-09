#!/usr/bin/env python3
# tools/gen-codes.py
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
# Generates compiler/x86_64/diag/codes.inc from spec §13's error registry
# table (docs/spec/exsecutor-spec-v0.4.md). This is THE mechanism behind a
# claim four documents made before this file existed: "codes.inc is generated
# from §13's registry." Before this script, nothing produced that file, and
# tools/spec-check.sh's check 1 reported so explicitly ("PASSES VACUOUSLY").
#
# VERIFICATION-ONLY. Never on the build path -- `make` needs only fasmg (spec
# §18.1). codes.inc is committed like any other project source file; a human
# or CI regenerates it and diffs byte-for-byte (tools/spec-check.sh check 1
# does the semantic half of that: it re-extracts the EXS-E code SET from both
# sides and diffs them on every run, independent of whether anyone just
# re-ran this script). Same arrangement as tools/ucd-gen/gen.py for
# compiler/shared/unicode/tables/tables.inc.
#
# §13 IS THE ONLY SOURCE (CLAUDE.md, "Error codes are permanent"). This
# script only ever READS the table -- it does not choose, invent, or renumber
# a code. If §13 does not list a code, this script cannot produce one; the
# fix for a missing code is a spec amendment to §13, made by whoever holds
# that scope, never a hand-edit here or to the generated .inc.
#
# Determinism (CLAUDE.md, "Determinism is not optional"): entries are kept in
# a plain list, in the exact order they appear in §13's table, and are never
# routed through a dict or set for anything that affects output order (a
# `set` is used only for O(1) duplicate-code detection, never iterated for
# emission). Regenerating from an unchanged spec reproduces the previous
# codes.inc byte-for-byte.
#
# Spec: docs/spec/exsecutor-spec-v0.4.md §8.3, §13
# ---------------------------------------------------------------------------
"""Generate compiler/x86_64/diag/codes.inc from spec §13's error registry."""

import argparse
import os
import re
import sys

HEADING_RE = re.compile(r'^#\s+13\.\s+Error registry\s*$')
NEXT_HEADING_RE = re.compile(r'^#\s+\S')
ROW_RE = re.compile(r'^\s*\|\s*`(EXS-E(\d{4}))`\s*\|\s*(.*?)\s*\|\s*$')

CODE_STR_LEN = 9  # "EXS-E" (5) + 4 decimal digits -- fixed width, always.


# --------------------------------------------------------------------------
# §13 parsing
# --------------------------------------------------------------------------

def parse_registry(spec_path):
    """Return [(code_str, numeric, message), ...] in §13 table order.

    Table order IS insertion order IS emission order -- never re-sorted
    (see module docstring/header on determinism). Scoped to the "# 13. Error
    registry" section specifically (stops at the next top-level heading) so
    an inline backticked code mention elsewhere in the spec's prose (e.g.
    "§13's own text: ... EXS-E0220 earns a code of its own") is never
    mistaken for a registry row -- only a real `| \\`EXS-E####\\` | ... |`
    table row counts.
    """
    with open(spec_path, encoding="utf-8") as fh:
        lines = fh.read().split("\n")

    start = None
    for i, line in enumerate(lines):
        if HEADING_RE.match(line):
            start = i
            break
    if start is None:
        raise SystemExit('gen-codes: no "# 13. Error registry" heading found in spec')

    end = len(lines)
    for i in range(start + 1, len(lines)):
        if NEXT_HEADING_RE.match(lines[i]):
            end = i
            break

    entries = []
    seen = set()
    for line in lines[start:end]:
        m = ROW_RE.match(line)
        if not m:
            continue
        code_str, numeric_str, message = m.group(1), m.group(2), m.group(3)
        if len(code_str) != CODE_STR_LEN:
            raise SystemExit(f"gen-codes: {code_str!r} is not {CODE_STR_LEN} bytes -- "
                              "the EXS-E#### shape is assumed fixed-width throughout "
                              "codes.inc; this needs a design change, not a silent truncation")
        if code_str in seen:
            raise SystemExit(f"gen-codes: duplicate {code_str} in §13 -- refusing to generate")
        seen.add(code_str)
        # Markdown may legitimately bold/backtick pieces of the meaning cell;
        # keep the canonical text plain. None of today's 24 rows need this,
        # but a future row might.
        message = message.replace("`", "").replace("*", "")
        if not message:
            raise SystemExit(f"gen-codes: {code_str} has an empty meaning cell in §13")
        entries.append((code_str, int(numeric_str), message))

    if not entries:
        raise SystemExit("gen-codes: parsed zero codes from §13 -- table shape changed, "
                          "or this parser is wrong; either way this is not a green result")
    return entries


# --------------------------------------------------------------------------
# fasmg emission
# --------------------------------------------------------------------------

def fasmg_str(s):
    """Quote `s` as a single-quoted fasmg string literal, doubling any
    embedded `'` (fasm/fasmg's own escaping convention -- verified directly:
    `db 'it''s a test'` assembles to the 11 bytes "it's a test", no
    include beyond format/format.inc needed). None of §13's current 24
    messages need this, but the registry grows, and a generator that
    silently emits broken fasmg source the day a message picks up an
    apostrophe is a worse failure than the two extra characters this costs
    today.
    """
    if "\n" in s or "\r" in s:
        raise SystemExit("gen-codes: message contains a line break -- "
                          "a markdown table cell cannot legitimately have one; "
                          "this indicates a parser bug, not real spec content")
    return "'" + s.replace("'", "''") + "'"


def emit_inc(entries, out_path, spec_display_path):
    n = len(entries)
    lines = [
        "; compiler/x86_64/diag/codes.inc",
        "; SPDX-License-Identifier: GPL-3.0-or-later",
        "; Copyright (C) 2026 The Exsecutor authors.",
        ";",
        "; DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.",
        ";",
        "; This code is free software; you can redistribute it and/or modify it under",
        "; the terms of the GNU General Public License as published by the Free",
        "; Software Foundation, either version 3 of the License, or (at your option)",
        "; any later version.",
        ";",
        "; This code is distributed in the hope that it will be useful, but WITHOUT",
        "; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or",
        "; FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for",
        "; more details.",
        ";",
        "; You should have received a copy of the GNU General Public License along",
        "; with this code. If not, see <https://www.gnu.org/licenses/>.",
        ";",
        "; Code produced by this compiler is not covered by the GPL --",
        "; see Exception A in LICENSE.EXCEPTION.",
        "; -----------------------------------------------------------------------------",
        "; GENERATED by tools/gen-codes.py from " + spec_display_path + " §13.",
        "; DO NOT EDIT BY HAND -- hand edits are overwritten by the next regeneration",
        "; and, worse, are exactly how codes.inc and §13 would drift silently. §13 is",
        "; the only source (CLAUDE.md, \"Error codes are permanent\"): a new code needs",
        "; a spec amendment to §13 FIRST, then a re-run of this generator -- never the",
        "; reverse, and never a code hand-added here that has no §13 row.",
        ";",
        "; Regenerate with:",
        ";   python3 tools/gen-codes.py --spec " + spec_display_path,
        ";       --out compiler/x86_64/diag/codes.inc",
        "; and diff. tools/spec-check.sh check 1 verifies the §13 <-> codes.inc code",
        "; SET mechanically on every run (extracts every EXS-E#### token from both",
        "; sides and diffs them) -- that check is the thing that actually catches",
        "; drift; regeneration is how a caught drift gets fixed.",
        ";",
        "; PURE DATA. No `include` of anything, deliberately: a generated artifact",
        "; should depend on as little as possible, not on wherever the macro dialect",
        "; happens to be. Three parallel, fixed-order arrays (code string, numeric",
        "; part, message pointer/length) rather than a `struct`-typed record array --",
        "; this needs nothing from macros/struct.inc, so it asks for nothing.",
        ";",
        "; ORDER IS §13's TABLE ORDER, i.e. INSERTION ORDER (CLAUDE.md, \"Maps iterate",
        "; in insertion order\" / \"No ordering may depend on a pointer value\" --",
        "; applied here to a plain array, which has no other order to begin with).",
        "; tools/gen-codes.py never sorts or hashes the rows; it keeps them in exactly",
        "; the sequence its single top-to-bottom read of the markdown table produced,",
        "; so regenerating from an unchanged spec reproduces this file byte-for-byte.",
        ";",
        "; Each code string is fixed-width, 9 ASCII bytes (\"EXS-E\" + 4 decimal",
        "; digits -- spec-check.sh's own extractor regex is literally `EXS-E[0-9]{4}`,",
        "; and every §13 row matches it), so `diag_code_str` is indexable by",
        "; `i * DIAG_CODE_STR_STRIDE` with no length table needed for it specifically.",
        "; The numeric part (e.g. 101 for EXS-E0101) is what",
        "; docs/asm-conventions.md's \"1.3 Error protocol: CF / eax\" puts in `eax` on",
        "; the failure path -- `diag_code_numeric` is this file's lookup key for that",
        "; value, kept as a plain integer (leading zeros are a rendering concern, re-",
        "; padded at render time from `diag_code_str` directly, never recomputed from",
        "; the integer -- so a code like EXS-E0101 round-trips through its own literal",
        "; text, not through zero-pad arithmetic that could get the width wrong).",
        ";",
        "; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3, §13",
        "; -----------------------------------------------------------------------------",
        "",
        f"DIAG_CODE_COUNT = {n}",
        "DIAG_CODE_STR_STRIDE = 9\t; \"EXS-E\" + 4 digits, always",
        "",
        "; ---- fixed-stride code-string table (9 bytes each, no separator, no NUL) ---",
        "diag_code_str:",
    ]
    for code_str, _numeric, _msg in entries:
        lines.append(f"\tdb\t{fasmg_str(code_str)}")
    assert all(len(c) == 9 for c, _, _ in entries)

    lines += [
        "",
        "; ---- parallel numeric-part table (dd each; the CF/eax protocol's value) ----",
        "diag_code_numeric:",
    ]
    for _code_str, numeric, _msg in entries:
        lines.append(f"\tdd\t{numeric}")

    lines += [
        "",
        "; ---- parallel canonical-message length table (dd each, bytes) --------------",
        "diag_code_msg_len:",
    ]
    for _code_str, _numeric, msg in entries:
        lines.append(f"\tdd\t{len(msg.encode('utf-8'))}")

    lines += [
        "",
        "; ---- parallel canonical-message pointer table (dq each, forward refs) ------",
        "diag_code_msg_ptr:",
    ]
    for i in range(n):
        lines.append(f"\tdq\tdiag_msg{i}")

    lines += [
        "",
        "; ---- canonical English message text (§8.3: \"English text is canonical.",
        ";      Translation, if ever, is a lookup keyed on code.\") -----------------",
    ]
    for i, (code_str, _numeric, msg) in enumerate(entries):
        lines.append(f"diag_msg{i} db\t{fasmg_str(msg)}\t; {code_str}")

    lines.append("")
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--spec", required=True,
                     help="path to docs/spec/exsecutor-spec-v0.4.md")
    ap.add_argument("--out", required=True,
                     help="output path for compiler/x86_64/diag/codes.inc")
    ap.add_argument("--check-only", action="store_true",
                     help="parse §13 and print a summary; write nothing")
    a = ap.parse_args()

    entries = parse_registry(a.spec)

    numerics = [numeric for _c, numeric, _m in entries]
    if len(set(numerics)) != len(numerics):
        raise SystemExit("gen-codes: two different EXS-E codes share one numeric part "
                          "-- the CF/eax channel cannot distinguish them; this is a §13 "
                          "problem, not something this generator can paper over")

    print(f"§13 error registry: {len(entries)} codes")
    for code_str, numeric, msg in entries:
        print(f"  {code_str}  ({numeric:>4})  {msg}")

    if a.check_only:
        return 0

    out_dir = os.path.dirname(a.out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    emit_inc(entries, a.out, "docs/spec/exsecutor-spec-v0.4.md")
    print(f"\nwrote {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
