#!/usr/bin/env python3
# tools/gen-lexicon.py
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
# Generates compiler/x86_64/checker/lexicon/morphemes.inc from spec §3.3's
# root table, §3.4's suffix table and §3.5's prefix table
# (docs/spec/exsecutor-spec-v0.4.md). Same arrangement tools/gen-codes.py has
# with §13 and tools/gen-keywords.py has with §8.4: a spec table is the
# normative source, the .inc is generated from it, and a human or CI diffs
# the regenerated file byte-for-byte against what is committed.
#
# VERIFICATION-ONLY. Never on the build path (spec §18.1: fasmg alone builds
# `exsc`). morphemes.inc is committed like any other project source file.
# tools/spec-check.sh does not yet carry a check for this table (a check 5 is
# proposed in this script's own header for the reviewer, deliberately not
# added -- out of this agent's write scope; see checker-lexicon's report).
#
# §3.3-§3.5 ARE ILLUSTRATIVE, NOT THE MORPHEME TABLE (spec §3.3, as amended:
# "This table is illustrative, and it is not the morpheme table ... The
# morpheme table is `lexicon.norma` ... and it does not yet exist"). This
# generator therefore does not claim to produce `lexicon.norma` -- it
# produces a STAND-IN, built the same mechanical way `lexicon.norma` would
# eventually be built, so that `checker/lexicon/lexicon.inc` can be written
# and tested against something real before that dependency exists. It adds
# NOTHING the spec does not list: `applic-`, `salut-`, `imprim-` are absent
# from the emitted root table because §3.3's table does not carry them (its
# own prose names them as roots the table is missing), and that absence is
# measured, not assumed -- it is the reason `docs/design/checker.md` gives for
# why the lexicon pass this table backs is built and tested but not enabled
# in the driver.
#
# THE IMPERATIVE-FORM COLUMN (finding 17, checker.md section 9): §3.3's root
# table has no conjugation column, and §3.4 states that `-e` is realised
# DIFFERENTLY depending on conjugation ("`-e` in the third (`leg-` → `lege`),
# **`-a` in the first** (`plica-` → `plica`, `applica-` → `applica`,
# `saluta-` → `saluta`)") -- information the root table itself does not carry
# per root. Guessing a conjugation from a bare consonant stem like `plic-`
# would be exactly the kind of invention CLAUDE.md's evidence discipline
# forbids ("prose designs are hypotheses until code runs" -- a guessed
# imperative is not code that ran, it is a guess wearing a table's shape).
# So this script does NOT guess: it scans §3.3's and §3.4's own prose for a
# literal "`ROOT-` → `form`" arrow pair (the exact notation the spec itself
# uses for `leg-` → `lege`) and fills the imperative column only where the
# root's own hyphenated citation form appears on the left of such an arrow.
# Measured against the spec text as it stands, this determines exactly ONE
# of the fourteen roots (`leg-` → `lege`) -- `plic-` is NOT `plica-` (a
# different stem spelling; the spec's `-a` examples are `plica-`,
# `applica-`, `saluta-`, none of which is one of the fourteen `§3.3` roots
# verbatim), so it is correctly left `unknown` rather than guessed at "plica"
# on the strength of a family resemblance. See --check-only's report for
# which roots resolved and which did not.
#
# §13 IS THE ONLY SOURCE, applied here to §3.3-§3.5: this script only ever
# READS the three tables. It cannot invent a root, a suffix, or a prefix, and
# it refuses to run rather than guess at a law or a declared-kind word it
# does not recognise (see LAW_TEXT_MAP / KIND_WORD_MAP below) -- an
# unrecognised phrase is a §3 amendment away from being parseable, not a
# reason to emit a plausible-looking wrong answer.
#
# Determinism (CLAUDE.md, "Determinism is not optional"): all three tables
# are kept in a plain list, in the exact order they appear in the spec's
# markdown, and are never routed through a dict or set for anything that
# reaches emission order (a `dict`/`set` is used only for O(1) duplicate or
# membership checks, never iterated for output). Regenerating from an
# unchanged spec reproduces morphemes.inc byte-for-byte -- this is asserted
# by checker-lexicon's own report (run twice, diff), not merely claimed here.
#
# Spec: docs/spec/exsecutor-spec-v0.4.md §3.1, §3.3, §3.4, §3.5, §3.6, §3.8.
# ---------------------------------------------------------------------------
"""Generate compiler/x86_64/checker/lexicon/morphemes.inc from spec
§3.3 (roots), §3.4 (suffixes) and §3.5 (prefixes)."""

import argparse
import os
import re
import sys

NEXT_HEADING_RE = re.compile(r'^#{1,2}\s+\S')
CELL_RE = re.compile(r'`([^`]*)`')
ARROW_RE = re.compile(r'`([a-z]+)-`\s*→\s*`([a-z]+)`')

ROOT_HEADING_RE = re.compile(r'^##\s+3\.3\s+Roots\s*$')
SUFFIX_HEADING_RE = re.compile(r'^##\s+3\.4\s+Suffixes carry type contracts\s*$')
PREFIX_HEADING_RE = re.compile(r'^##\s+3\.5\s+Prefixes carry signature laws\s*$')

ROOT_ROW_RE = re.compile(
    r'^\|\s*`([a-z]+)-`\s*\|\s*`([a-z]+)-`\s*\|\s*([a-z]+)\s*\|\s*$')
SUFFIX_ROW_RE = re.compile(
    r'^\|\s*`(-[a-z]+)`\s*\|\s*(present|supine)\s*\|\s*([A-Za-z\- ]+?)\s*\|\s*`([a-z]+)`\s*\|\s*$')
# Prefix rows: first two cells may hold more than one backticked/plain value
# (the `prae-`, `sub-` row), so this captures the three raw cells and the
# per-cell splitting happens in parse_prefixes.
PREFIX_ROW_RE = re.compile(r'^\|\s*(.+?)\s*\|\s*(.+?)\s*\|\s*(.+?)\s*\|\s*$')

# §3.5's law sentences, verbatim, mapped to a stable symbolic law. An
# unrecognised law text is refused rather than guessed (see module header).
LAW_TEXT_MAP = {
    "same signature as the bare form": "SAME_SIG",
    "base's return type becomes first parameter": "RETURN_TO_PARAM",
    "first parameter is the source type": "SRC_PARAM",
    "first parameter is the destination type": "DST_PARAM",
    "parameter and return types differ": "DIFFER",
    "takes two or more of the base's operand type": "ARITY2",
    "positional only": "POSITIONAL",
}

# §3.4's "must declare" column, mapped to the declaration-kind class the
# checker compares a decl's actual AST_D_* kind against. Also refused rather
# than guessed if a new word appears.
KIND_WORD_MAP = {
    "functio": "FN",
    "structura": "STRUCT",
    "interfacies": "IFACE",
    "typus": "TYPUS",
}

LAW_CODES = ["NONE", "SAME_SIG", "RETURN_TO_PARAM", "SRC_PARAM", "DST_PARAM",
             "DIFFER", "ARITY2", "POSITIONAL"]
KIND_CODES = ["NONE", "FN", "STRUCT", "IFACE", "TYPUS"]


# --------------------------------------------------------------------------
# §3 parsing
# --------------------------------------------------------------------------

def _section_lines(lines, heading_re):
    start = None
    for i, line in enumerate(lines):
        if heading_re.match(line):
            start = i
            break
    if start is None:
        raise SystemExit(f"gen-lexicon: heading matching {heading_re.pattern!r} "
                          "not found in spec")
    end = len(lines)
    for i in range(start + 1, len(lines)):
        if NEXT_HEADING_RE.match(lines[i]):
            end = i
            break
    return lines[start:end]


def parse_roots(lines, all_text):
    section = _section_lines(lines, ROOT_HEADING_RE)
    entries = []
    seen = set()
    for line in section:
        m = ROOT_ROW_RE.match(line)
        if not m:
            continue
        present, supine, gloss = m.group(1), m.group(2), m.group(3)
        if present in seen:
            raise SystemExit(f"gen-lexicon: duplicate root `{present}-` in §3.3")
        seen.add(present)
        entries.append({"present": present, "supine": supine, "gloss": gloss})
    if not entries:
        raise SystemExit("gen-lexicon: parsed zero roots from §3.3 -- table "
                          "shape changed, or this parser is wrong")
    if len(entries) != 14:
        raise SystemExit(f"gen-lexicon: §3.3's root table has {len(entries)} rows; "
                          "the spec's own prose (\"Fourteen roots cannot be a "
                          "language's lexicon\") says 14. One of the two changed "
                          "without the other -- refusing to generate")

    # The imperative-form column (finding 17): fill ONLY where the root's own
    # hyphenated stem appears literally on the left of a "`X-` → `Y`" arrow
    # anywhere in §3 (see module header -- never guessed from conjugation).
    arrows = {}
    for root_text, form in ARROW_RE.findall(all_text):
        if root_text in arrows and arrows[root_text] != form:
            raise SystemExit(f"gen-lexicon: `{root_text}-` maps to two different "
                              f"imperative forms in §3's prose ({arrows[root_text]!r} "
                              f"and {form!r}) -- refusing to guess which")
        arrows[root_text] = form
    for e in entries:
        e["imperative"] = arrows.get(e["present"])  # None = unknown
    return entries


def parse_suffixes(lines):
    section = _section_lines(lines, SUFFIX_HEADING_RE)
    entries = []
    seen = set()
    for line in section:
        m = SUFFIX_ROW_RE.match(line)
        if not m:
            continue
        suffix, stem, kind_desc, declare = m.groups()
        if suffix in seen:
            raise SystemExit(f"gen-lexicon: duplicate suffix `{suffix}` in §3.4")
        seen.add(suffix)
        if declare not in KIND_WORD_MAP:
            raise SystemExit(f"gen-lexicon: §3.4's suffix `{suffix}` must-declare "
                              f"word {declare!r} is not one this script recognises "
                              f"({sorted(KIND_WORD_MAP)}) -- refusing to guess")
        entries.append({
            "suffix": suffix, "stem": stem, "kind_desc": kind_desc,
            "declare": declare, "kind_class": KIND_WORD_MAP[declare],
        })
    if not entries:
        raise SystemExit("gen-lexicon: parsed zero suffixes from §3.4 -- table "
                          "shape changed, or this parser is wrong")
    if len(entries) != 6:
        raise SystemExit(f"gen-lexicon: §3.4's suffix table has {len(entries)} rows, "
                          "expected 6 -- refusing to generate")

    # finding 17, applied at the suffix side: `-e`'s "stem" column literally
    # says "present", but its true target is the per-root IMPERATIVE form
    # (see the module header's long explanation and §3.4's own correction:
    # "-e is a morphological category, not a spelling"). This is the one
    # place this script overrides a table cell with a documented, spec-cited
    # reason rather than reading it verbatim -- flagged loudly rather than
    # silently, exactly because it is the one such override.
    e_count = 0
    for e in entries:
        if e["suffix"] == "-e":
            e["stem_sel"] = "IMPERATIVE"
            e_count += 1
        elif e["stem"] == "present":
            e["stem_sel"] = "PRESENT"
        elif e["stem"] == "supine":
            e["stem_sel"] = "SUPINE"
        else:
            raise SystemExit(f"gen-lexicon: suffix `{e['suffix']}` has stem "
                              f"{e['stem']!r}, neither present nor supine")
    if e_count != 1:
        raise SystemExit("gen-lexicon: expected exactly one `-e` suffix row in "
                          f"§3.4, found {e_count} -- the finding-17 override "
                          "targets a row this parser cannot uniquely find")
    return entries


def parse_prefixes(lines):
    section = _section_lines(lines, PREFIX_HEADING_RE)
    entries = []
    seen = set()
    for line in section:
        m = PREFIX_ROW_RE.match(line)
        if not m:
            continue
        cell1, cell2, cell3 = m.groups()
        prefixes = CELL_RE.findall(cell1)
        if not prefixes:
            continue  # the header/separator rows
        glosses = [g.strip() for g in cell2.split(",")]
        law_text = " ".join(cell3.split())
        if len(prefixes) != len(glosses):
            raise SystemExit(
                f"gen-lexicon: §3.5 row {cell1!r} has {len(prefixes)} prefixes "
                f"but {len(glosses)} glosses -- cannot zip them 1:1")
        if law_text not in LAW_TEXT_MAP:
            raise SystemExit(f"gen-lexicon: §3.5 law text {law_text!r} is not one "
                              "this script recognises -- refusing to guess a law "
                              f"code (known: {sorted(LAW_TEXT_MAP)})")
        law = LAW_TEXT_MAP[law_text]
        for p, g in zip(prefixes, glosses):
            if not p.endswith("-"):
                raise SystemExit(f"gen-lexicon: §3.5 prefix `{p}` does not end "
                                  "in '-' -- table shape assumption violated")
            stem = p[:-1]
            if stem in seen:
                raise SystemExit(f"gen-lexicon: duplicate prefix `{p}` in §3.5")
            seen.add(stem)
            entries.append({"prefix": stem, "gloss": g, "law_text": law_text,
                             "law": law})
    if not entries:
        raise SystemExit("gen-lexicon: parsed zero prefixes from §3.5 -- table "
                          "shape changed, or this parser is wrong")
    if len(entries) != 8:
        raise SystemExit(f"gen-lexicon: §3.5 table yields {len(entries)} prefixes, "
                          "expected 8 (six single-prefix rows plus the "
                          "`prae-`,`sub-` row's two) -- refusing to generate")
    return entries


# --------------------------------------------------------------------------
# fasmg emission
# --------------------------------------------------------------------------

def fasmg_str(s):
    if not re.match(r'^[a-zA-Z \-]*$', s):
        raise SystemExit(f"gen-lexicon: refusing to emit {s!r} as a literal "
                          "(non [a-zA-Z -] byte)")
    return "'" + s + "'"


def emit_pool(out, label, items):
    """`db LEN,'text'` pool, walkable by ast/node.inc's ast_name_at /
    ast_name_id -- reused as-is rather than duplicated (both are already in
    the build via ast/ast.inc, which every consumer of this file includes
    first per resolve.inc's contract). A zero-length entry ('' as text) is a
    legal, deliberate "unknown" marker -- ast_name_at answers len 0 for it
    and the checker treats that as "this root's imperative form is not in
    this stand-in table", never as a match."""
    out.append(f"{label}:")
    for text in items:
        lit = fasmg_str(text) if text else "''"
        out.append(f"\tdb\t{len(text)},{lit}")
    out.append(f"{label.upper()}_LEN = $ - {label}")
    out.append("")


HEADER = """\
; compiler/x86_64/checker/lexicon/morphemes.inc
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
; GENERATED by tools/gen-lexicon.py from {spec} §3.3-§3.5.
; DO NOT EDIT BY HAND -- hand edits are overwritten by the next regeneration.
;
; THIS IS A STAND-IN, NOT `lexicon.norma`. Spec §3.3, as amended: "This table
; is illustrative, and it is not the morpheme table ... The morpheme table is
; `lexicon.norma` ... and it does not yet exist." §3.3-§3.5's fourteen roots,
; six suffixes and eight prefixes (`prae-`/`sub-` counted separately) are
; everything this generator was given; it adds NOTHING they do not list --
; `applic-`, `salut-` and `imprim-` are absent because §3.3's table lacks
; them, even though its own prose names all three as roots a real table would
; need. THAT ABSENCE IS WHY docs/design/checker.md's lexicon pass (built
; against this file) is not enabled in the driver: `saluta`, `construe`,
; `imprime`, `textus` and `grapha` -- all correct Latin, all used elsewhere in
; this very spec -- do not decompose over these fourteen roots and would be
; rejected by a checker that trusted this table as complete.
;
; THE IMPERATIVE-FORM COLUMN (finding 17) is filled ONLY where §3's own prose
; states a root's imperative literally, as a "`ROOT-` → `form`" arrow pair --
; never guessed from a conjugation pattern. Against the spec text as it
; stands this determines exactly one entry, `leg-` → `lege`; every other
; root's imperative is the zero-length "unknown" marker. See this script's
; own header for the full reasoning and why `plic-` (whose only textual
; neighbour is `plica-`, a DIFFERENT stem spelling, in `-a` examples) is
; correctly left unknown rather than inferred.
;
; Regenerate with:
;   python3 tools/gen-lexicon.py --spec {spec}
;       --out compiler/x86_64/checker/lexicon/morphemes.inc
; and diff -- run twice from an unchanged spec, the output must be
; byte-for-byte identical (checker-lexicon's report asserts this was done).
; No check 5 exists yet in tools/spec-check.sh; checker-lexicon's report
; proposes its exact shell to the spec-check owner rather than adding it
; (CLAUDE.md, Scope: each agent owns its directory).
;
; PURE DATA. No `include` of anything -- same reasoning as diag/codes.inc and
; lexer/keywords.inc: a generated artifact should depend on as little as
; possible. The text pools below are in the exact `db LEN,'text'` shape
; ast/node.inc's `ast_name_at` / `ast_name_id` already walk (ast/kinds.inc's
; own pools are the same shape, for the same reason), so
; `checker/lexicon/lexicon.inc` reuses those two procs directly instead of
; duplicating a pool walker a third time.
;
; ORDER is each table's own row order in the spec (§3.3 top to bottom, §3.4
; top to bottom, §3.5 top to bottom with `prae-`/`sub-` expanded left to
; right) -- CLAUDE.md, "Maps iterate in insertion order" / "No ordering may
; depend on a pointer value", applied here to plain arrays that never had any
; other order to begin with. Regenerating from an unchanged spec reproduces
; this file byte-for-byte.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §3.1, §3.3, §3.4, §3.5, §3.6, §3.8.
; -----------------------------------------------------------------------------
"""


def emit_inc(roots, suffixes, prefixes, out_path, spec_display_path):
    out = [HEADER.format(spec=spec_display_path)]
    L = out.append

    L(f"LEX_ROOT_COUNT = {len(roots)}")
    L(f"LEX_SUFFIX_COUNT = {len(suffixes)}")
    L(f"LEX_PREFIX_COUNT = {len(prefixes)}")
    L("")
    L("; ---- prefix-law codes -------------------------------------------------------")
    L("; 0 is never a real law (NONE is not emitted by any table row); a law field")
    L("; holding 0 would be a bug in this generator, not a a valid \"no law\" answer --")
    L("; `prae-`/`sub-`'s \"positional only\" is its OWN code, POSITIONAL, precisely so")
    L("; that 0 stays free as an internal-only sentinel.")
    for i, name in enumerate(LAW_CODES):
        L(f"LEXLAW_{name} = {i}")
    L("")
    L("; ---- declared-kind classes (§3.4's \"must declare\" column) -----------------")
    for i, name in enumerate(KIND_CODES):
        L(f"LEXKIND_{name} = {i}")
    L("")
    L("; ---- stem selectors ----------------------------------------------------------")
    L("; PRESENT/SUPINE match a suffix's stated stem literally, concatenated with the")
    L("; suffix's own text. IMPERATIVE (finding 17, `-e` only) matches the WHOLE")
    L("; remainder against a root's precomputed imperative form instead -- see")
    L("; lex_root_imperative below and this file's header.")
    L("LEX_STEM_PRESENT = 0")
    L("LEX_STEM_SUPINE = 1")
    L("LEX_STEM_IMPERATIVE = 2")
    L("")

    L("; ---- roots, §3.3 table order --------------------------------------------------")
    emit_pool(out, "lex_root_present", [r["present"] for r in roots])
    emit_pool(out, "lex_root_supine", [r["supine"] for r in roots])
    emit_pool(out, "lex_root_imperative",
              [r["imperative"] or "" for r in roots])
    emit_pool(out, "lex_root_gloss", [r["gloss"] for r in roots])

    L("; ---- suffixes, §3.4 table order ------------------------------------------------")
    emit_pool(out, "lex_suffix_text", [s["suffix"] for s in suffixes])
    L("lex_suffix_stem_sel:")
    for s in suffixes:
        L(f"\tdb\tLEX_STEM_{s['stem_sel']}\t; {s['suffix']}")
    L("")
    L("lex_suffix_kind_class:")
    for s in suffixes:
        L(f"\tdb\tLEXKIND_{s['kind_class']}\t; {s['suffix']} -> {s['declare']}")
    L("")

    L("; ---- prefixes, §3.5 table order (`prae-`,`sub-` expanded) ----------------------")
    emit_pool(out, "lex_prefix_text", [p["prefix"] + "-" for p in prefixes])
    L("lex_prefix_law:")
    for p in prefixes:
        L(f"\tdb\tLEXLAW_{p['law']}\t; {p['prefix']}- ({p['law_text']})")
    L("")

    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out))


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--spec", required=True,
                     help="path to docs/spec/exsecutor-spec-v0.4.md")
    ap.add_argument("--out", required=True,
                     help="output path for "
                          "compiler/x86_64/checker/lexicon/morphemes.inc")
    ap.add_argument("--check-only", action="store_true",
                     help="parse §3.3-§3.5 and print a summary; write nothing")
    a = ap.parse_args()

    with open(a.spec, encoding="utf-8") as fh:
        text = fh.read()
    lines = text.split("\n")

    roots = parse_roots(lines, text)
    suffixes = parse_suffixes(lines)
    prefixes = parse_prefixes(lines)

    print(f"§3.3 roots: {len(roots)}")
    for r in roots:
        imp = r["imperative"] if r["imperative"] else "unknown"
        print(f"  {r['present']:<8} {r['supine']:<10} {r['gloss']:<10} imperative={imp}")
    known = sum(1 for r in roots if r["imperative"])
    print(f"  ({known}/{len(roots)} roots have a determined imperative form)")

    print(f"\n§3.4 suffixes: {len(suffixes)}")
    for s in suffixes:
        print(f"  {s['suffix']:<8} stem_sel={s['stem_sel']:<10} -> {s['declare']}")

    print(f"\n§3.5 prefixes: {len(prefixes)}")
    for p in prefixes:
        print(f"  {p['prefix']+'-':<7} law={p['law']:<16} ({p['law_text']})")

    if a.check_only:
        return 0

    out_dir = os.path.dirname(a.out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    emit_inc(roots, suffixes, prefixes, a.out,
              "docs/spec/exsecutor-spec-v0.4.md")
    print(f"\nwrote {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
