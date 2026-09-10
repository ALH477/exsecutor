#!/usr/bin/env python3
# tools/gen-keywords.py
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
# Generates compiler/x86_64/lexer/keywords.inc from spec §8.4's reserved-word
# table (docs/spec/exsecutor-spec-v0.4.md). Exactly the arrangement
# tools/gen-codes.py has with §13, and §8.4 says so in as many words:
# "This section is the normative source: compiler/x86_64/lexer/keywords.inc is
# generated from it and checked against it mechanically, the same arrangement
# §13 has with diag/codes.inc. A keyword that appears in code but not in this
# table, or the reverse, fails the build."
#
# Until this script existed, tools/spec-check.sh's check 4 printed
# "PASSES VACUOUSLY" because its right-hand side did not exist.
#
# VERIFICATION-ONLY. Never on the build path -- `make` needs only fasmg (spec
# §18.1). keywords.inc is committed like any other project source file; a
# human or CI regenerates it and diffs byte-for-byte, and tools/spec-check.sh
# check 4 re-extracts the keyword SET from both sides on every run whether or
# not anyone just re-ran this script.
#
# §8.4 IS THE ONLY SOURCE. This script only ever READS the table. It cannot
# produce a word §8.4 does not list, and it will refuse to run if the table
# parses to something it does not recognise. NEVER hand-add a keyword here or
# in the generated .inc: §3.9.3 requires every proposed morpheme-table root to
# be collision-checked against the reserved set, so a word reserved in code
# but not in the spec silently and permanently removes a root from the
# lexicon's root-space with no amendment recording that it was spent. A new
# reserved word is a §8.4 amendment first, then a re-run of this generator.
#
# Determinism (CLAUDE.md, "Determinism is not optional"): the canonical order
# is §8.4's own table order, top to bottom, left to right within a row -- read
# once into a plain list and never re-sorted. The one derived ordering, the
# length-bucketed lookup index, is sorted by (length, raw bytes) with
# Python's total order on `bytes`, which is LC_ALL=C order by construction and
# cannot vary with the developer's locale. Nothing is routed through a dict or
# a set for anything that reaches output (a set is used only for O(1)
# duplicate detection, never iterated).
#
# Spec: docs/spec/exsecutor-spec-v0.4.md §8.2, §8.4; §3.9.3 for the cost of a
# reservation.
# ---------------------------------------------------------------------------
"""Generate compiler/x86_64/lexer/keywords.inc from spec §8.4's keyword table."""

import argparse
import os
import re
import sys

HEADING_RE = re.compile(r'^##\s+8\.4\s+Tokens\s*$')
NEXT_HEADING_RE = re.compile(r'^#{1,2}\s+\S')
TIER1_RE = re.compile(r'^\*\*1\.\s*Reserved words\*\*')
# The sentence that closes the table and states the count in words.
COUNT_RE = re.compile(r'^([A-Za-z-]+)\s+words\b')
ROW_RE = re.compile(r'^\|\s*([a-z][a-z ]*?)\s*\|\s*(.*?)\s*\|\s*$')
CELL_RE = re.compile(r'`([^`]*)`')
# §8.2: "Non-ASCII identifiers are allowed. Non-ASCII keywords are not."
WORD_RE = re.compile(r'^[a-z][a-z_]*$')

# Spelled-out counts, for cross-checking the table against §8.4's own
# "Thirty words." sentence. A table that grows without its prose count
# growing is exactly the silent drift this file exists to prevent, and the
# spec states the number in words, so reading it costs this table.
NUMBER_WORDS = {
    "twenty": 20, "twenty-one": 21, "twenty-two": 22, "twenty-three": 23,
    "twenty-four": 24, "twenty-five": 25, "twenty-six": 26,
    "twenty-seven": 27, "twenty-eight": 28, "twenty-nine": 29,
    "thirty": 30, "thirty-one": 31, "thirty-two": 32, "thirty-three": 33,
    "thirty-four": 34, "thirty-five": 35, "thirty-six": 36,
    "thirty-seven": 37, "thirty-eight": 38, "thirty-nine": 39,
    "forty": 40, "forty-one": 41, "forty-two": 42, "forty-three": 43,
    "forty-four": 44, "forty-five": 45, "forty-six": 46, "forty-seven": 47,
    "forty-eight": 48, "forty-nine": 49, "fifty": 50,
}


# --------------------------------------------------------------------------
# §8.4 parsing
# --------------------------------------------------------------------------

def parse_reserved(spec_path):
    """Return ([(group, word), ...] in §8.4 table order, stated_count|None).

    Scoped to the "## 8.4 Tokens" section and, inside it, to the rows
    between the "**1. Reserved words**" marker and the "<N> words." sentence
    that closes the table. Tier 2 (contextual keywords) and tier 3
    (capability atoms) are deliberately NOT collected: §8.4 says they are
    "ordinary identifiers everywhere else" and "Identifiers in the
    capability namespace, not reserved words." Reserving them here would be
    the exact §3.9.3 cost this file's header warns about, taken silently.
    """
    with open(spec_path, encoding="utf-8") as fh:
        lines = fh.read().split("\n")

    start = None
    for i, line in enumerate(lines):
        if HEADING_RE.match(line):
            start = i
            break
    if start is None:
        raise SystemExit('gen-keywords: no "## 8.4 Tokens" heading found in spec')

    end = len(lines)
    for i in range(start + 1, len(lines)):
        if NEXT_HEADING_RE.match(lines[i]):
            end = i
            break

    tier1 = None
    for i in range(start, end):
        if TIER1_RE.match(lines[i]):
            tier1 = i
            break
    if tier1 is None:
        raise SystemExit('gen-keywords: §8.4 has no "**1. Reserved words**" marker '
                         "-- the section's shape changed; this parser is not "
                         "measuring what it claims and will not guess")

    entries = []
    seen = set()
    stated = None
    for line in lines[tier1 + 1:end]:
        m = COUNT_RE.match(line)
        if m:
            stated = NUMBER_WORDS.get(m.group(1).lower())
            break
        row = ROW_RE.match(line)
        if not row:
            continue
        group, cell = row.group(1), row.group(2)
        words = CELL_RE.findall(cell)
        if not words:
            continue                      # the "| group | words |" header row
        for word in words:
            if not WORD_RE.match(word):
                raise SystemExit(
                    f"gen-keywords: {word!r} in §8.4's reserved table is not an "
                    "ASCII lowercase word. §8.2: \"Non-ASCII identifiers are "
                    "allowed. Non-ASCII keywords are not.\" Refusing to generate")
            if word in seen:
                raise SystemExit(
                    f"gen-keywords: {word!r} appears in more than one §8.4 group "
                    "-- one word, one group; refusing to generate")
            seen.add(word)
            entries.append((group, word))

    if not entries:
        raise SystemExit("gen-keywords: parsed zero reserved words from §8.4 -- "
                         "the table shape changed, or this parser is wrong; "
                         "either way this is not a green result")
    return entries, stated


# --------------------------------------------------------------------------
# fasmg emission
# --------------------------------------------------------------------------

def fasmg_str(s):
    """Quote `s` as a single-quoted fasmg string literal. A reserved word is
    ASCII lowercase by the WORD_RE check above, so no character in it can
    need escaping -- this asserts that rather than assuming it."""
    if not WORD_RE.match(s):
        raise SystemExit(f"gen-keywords: refusing to emit {s!r} as a literal")
    return "'" + s + "'"


HEADER = """\
; compiler/x86_64/lexer/keywords.inc
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 The Exsecutor authors.
;
; DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
;
; This code is free software; you can redistribute it and/or modify it under
; the terms of the GNU General Public License as published by the Free
; Software Foundation, either version 3 of the License, or (at your option)
; any later version.
;
; This code is distributed in the hope that it will be useful, but WITHOUT
; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
; FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
; more details.
;
; You should have received a copy of the GNU General Public License along
; with this code. If not, see <https://www.gnu.org/licenses/>.
;
; Code produced by this compiler is not covered by the GPL --
; see Exception A in LICENSE.EXCEPTION.
; -----------------------------------------------------------------------------
; GENERATED by tools/gen-keywords.py from {spec} §8.4.
; DO NOT EDIT BY HAND. §8.4 is the normative source and says so itself: "This
; section is the normative source: compiler/x86_64/lexer/keywords.inc is
; generated from it and checked against it mechanically ... A keyword that
; appears in code but not in this table, or the reverse, fails the build."
;
; A hand-added word here would not merely drift -- it would SPEND ROOT-SPACE
; SILENTLY. §3.9.3 requires every proposed morpheme-table root to be
; collision-checked against the reserved set, so a word reserved in code but
; not in §8.4 permanently removes a root from the lexicon with no amendment
; recording the cost. A new reserved word is a §8.4 amendment FIRST, then a
; re-run of this generator; never the reverse.
;
; Regenerate with:
;   python3 tools/gen-keywords.py --spec {spec}
;       --out compiler/x86_64/lexer/keywords.inc
; and diff. tools/spec-check.sh check 4 verifies the §8.4 <-> keywords.inc
; keyword SET mechanically on every run: it extracts the second token of every
; line matching `^[[:space:]]*kw[[:space:]]+[a-z_]+` from this file and diffs
; that against §8.4's table. The `kw` lines below exist in exactly that shape
; for that extractor, and they are also the real emission of the text pool --
; one set of lines, so the checked thing and the compiled thing cannot be
; different things.
;
; PURE DATA. No `include` of anything, deliberately -- same reasoning as
; diag/codes.inc: a generated artifact should depend on as little as possible,
; not on wherever the macro dialect happens to be. The `kw` macro defined here
; is `purge`d at the end of the file, so nothing downstream inherits a
; single-letter-ish global macro name from an include.
;
; ORDER. Two orders, both explicit, neither dependent on a pointer or a hash
; (CLAUDE.md, "Determinism is not optional"):
;   - CANONICAL order is §8.4's own table order, top to bottom and left to
;     right within a row. It fixes `KW_*` -- a keyword id is 1-based so that 0
;     stays free as "not a reserved word", the same sentinel convention
;     rt/intern.inc uses for ids.
;   - LOOKUP order is canonical indices sorted by (length, raw bytes). It is
;     derived, and it exists only so a lookup compares against the handful of
;     reserved words that share the candidate's length instead of all
;     {count} of them. `kw_len_start` gives each length's half-open range.
;
; No hash table. With {count} words the largest length bucket is {maxbucket}, so a
; miss costs at most {maxbucket} short byte compares -- and a hash would make the
; probe sequence depend on a hash function, which §9.3 forbids for anything
; that can reach output. rt/map.inc's header makes the same argument.
;
; This file classifies; it does not diagnose. `EXS-E0220` (reserved keyword
; used as identifier) is raised by lexer/lex.inc, which is also where its
; §8.3 machine-applicable fix is built. This file registers no code and
; invents none (CLAUDE.md, "Error codes are permanent").
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.2, §8.4; §3.9.3 for what a
; reservation costs.
; -----------------------------------------------------------------------------
"""


def emit_inc(entries, out_path, spec_display_path):
    words = [w for _g, w in entries]
    n = len(words)
    lens = [len(w) for w in words]
    minlen, maxlen = min(lens), max(lens)

    # Derived lookup order: (length, raw bytes). `bytes` compares
    # byte-by-byte, which is LC_ALL=C order and cannot vary with locale.
    order = sorted(range(n), key=lambda i: (lens[i], words[i].encode("ascii")))
    # Half-open [start[L], start[L+1]) range of `order` holding length L.
    start = [0] * (maxlen + 2)
    for L in range(maxlen + 2):
        start[L] = sum(1 for i in order if lens[i] < L)
    maxbucket = max((start[L + 1] - start[L]) for L in range(maxlen + 1))

    offsets = []
    acc = 0
    for w in words:
        offsets.append(acc)
        acc += len(w)
    pool_len = acc

    groupw = max(len(g) for g, _w in entries)

    out = [HEADER.format(spec=spec_display_path, count=n, maxbucket=maxbucket)]
    L = out.append

    L("")
    L(f"KW_COUNT = {n}")
    L(f"KW_MIN_LEN = {minlen}\t; shortest reserved word")
    L(f"KW_MAX_LEN = {maxlen}\t; longest reserved word")
    L("KW_NONE = 0\t; not a reserved word -- kw_lookup's miss value")
    L("")
    L("; ---- canonical ids, §8.4 table order ---------------------------------------")
    for i, (group, word) in enumerate(entries):
        L(f"KW_{word.upper()} = {i + 1}\t; {group}")
    L("")
    L("; ---- the reserved-word text pool, canonical order --------------------------")
    L("; One `kw` line per §8.4 reserved word; see this file's header for why they")
    L("; are shaped exactly like this. The bare first argument is what")
    L("; tools/spec-check.sh check 4 reads; the quoted second is what is emitted.")
    L("; The generator writes both from one source word, so they cannot disagree.")
    L("macro kw? word, text")
    L("\tdb\ttext")
    L("end macro")
    L("")
    L("kw_text:")
    for i, (group, word) in enumerate(entries):
        pad = " " * (11 - len(word)) if len(word) < 11 else ""
        L(f"\tkw\t{word},{pad} {fasmg_str(word)}"
          f"{pad}\t; {i + 1:>2}  {group}")
    L("KW_TEXT_LEN = $ - kw_text")
    L("purge kw?")
    L("")
    L("; A generated file that silently emitted the wrong number of bytes would be")
    L("; caught by nothing else in the tree, so it is caught here, at assembly time,")
    L("; by fasmg's own native `assert` (NOT macros/assert.inc's runtime `rassert`).")
    L(f"assert KW_TEXT_LEN = {pool_len}")
    L("")
    L("; ---- byte offset of each word into kw_text, canonical order ----------------")
    L("kw_off:")
    for i, (_g, word) in enumerate(entries):
        L(f"\tdd\t{offsets[i]}\t; {word}")
    L("")
    L("; ---- byte length of each word, canonical order -----------------------------")
    L("kw_len:")
    for i, (_g, word) in enumerate(entries):
        L(f"\tdb\t{lens[i]}\t; {word}")
    L("")
    L("; ---- lookup index: canonical 0-based indices, sorted by (length, bytes) ----")
    L("kw_sorted:")
    for i in order:
        L(f"\tdb\t{i}\t; {words[i]} ({lens[i]})")
    L("")
    L("; ---- kw_sorted[kw_len_start[L] .. kw_len_start[L+1]) is every reserved -----")
    L(";      word of length L. Indexable directly by a candidate's byte length,")
    L(";      for L in 0..KW_MAX_LEN; the trailing entry closes the last bucket.")
    L("kw_len_start:")
    for Lx in range(maxlen + 2):
        cnt = start[Lx + 1] - start[Lx] if Lx <= maxlen else 0
        note = f"; L={Lx}"
        if Lx <= maxlen:
            note += f"  ({cnt} word{'' if cnt == 1 else 's'})"
        else:
            note += "  (end)"
        L(f"\tdb\t{start[Lx]}\t{note}")
    L(f"KW_LEN_START_COUNT = {maxlen + 2}")
    L("")

    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out))
    return n, maxbucket


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--spec", required=True,
                    help="path to docs/spec/exsecutor-spec-v0.4.md")
    ap.add_argument("--out", required=True,
                    help="output path for compiler/x86_64/lexer/keywords.inc")
    ap.add_argument("--check-only", action="store_true",
                    help="parse §8.4 and print a summary; write nothing")
    a = ap.parse_args()

    entries, stated = parse_reserved(a.spec)
    n = len(entries)

    if stated is None:
        raise SystemExit(
            "gen-keywords: §8.4's reserved table is not followed by a "
            "'<N> words.' sentence this script can read. That sentence is the "
            "spec's own statement of the count and is what catches a row added "
            "without the prose being updated -- refusing to generate rather "
            "than silently dropping the cross-check")
    if stated != n:
        raise SystemExit(
            f"gen-keywords: §8.4's table holds {n} reserved words but the "
            f"section's own closing sentence says {stated}. One of the two is "
            "wrong and this script cannot tell which -- fix §8.4, then re-run")

    print(f"§8.4 reserved words: {n} (§8.4's own count sentence agrees)")
    group = None
    for g, w in entries:
        if g != group:
            group = g
            print(f"  {g}:")
        print(f"      {w}")

    if a.check_only:
        return 0

    out_dir = os.path.dirname(a.out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    _n, maxbucket = emit_inc(entries, a.out, "docs/spec/exsecutor-spec-v0.4.md")
    print(f"\nwrote {a.out} ({n} words, largest length bucket {maxbucket})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
