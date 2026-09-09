#!/usr/bin/env python3
# tools/ucd-gen/gen.py
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
# Generates the binary Unicode tables in compiler/shared/unicode/tables/ from
# a pinned UCD (ADR 0004: nixpkgs unicode-character-database).
#
# VERIFICATION-ONLY. This file is never on the build path: `make` needs only
# fasmg. The blobs it emits are checked in, and CI regenerates and diffs them
# byte-for-byte (spec §11, .claude/agents/unicode.md).
#
# Determinism is a hard requirement, not a nicety (spec §9.3). Nothing here
# may depend on dict ordering, locale, wall clock, or filesystem order. Every
# table is built from sorted input and written little-endian explicitly.
#
# Spec: docs/spec/exsecutor-spec-v0.4.md §8.1, §8.2, §11
# ---------------------------------------------------------------------------
"""Generate Exsecutor's Unicode tables from a pinned UCD."""

import argparse
import os
import struct
import sys

MAX_CP = 0x110000
BLOCK_SHIFT = 7                 # 128-codepoint stage-2 blocks
BLOCK_SIZE = 1 << BLOCK_SHIFT
STAGE1_LEN = MAX_CP >> BLOCK_SHIFT

# Hangul (UAX #15 section 3.12) is algorithmic and deliberately absent from the
# decomposition tables -- storing 11 172 syllables would be a waste.
S_BASE, L_BASE, V_BASE, T_BASE = 0xAC00, 0x1100, 0x1161, 0x11A7
L_COUNT, V_COUNT, T_COUNT = 19, 21, 28
N_COUNT = V_COUNT * T_COUNT
S_COUNT = L_COUNT * N_COUNT


# --------------------------------------------------------------------------
# UCD parsing
# --------------------------------------------------------------------------

def _strip(line):
    return line.split("#", 1)[0].strip()


def parse_unicode_data(path):
    """UnicodeData.txt -> (ccc, canonical decomposition).

    Ranges are expressed as First>/Last> pairs; neither carries a combining
    class or a decomposition, so range expansion is unnecessary here.
    """
    ccc, decomp = {}, {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            f = line.split(";")
            if len(f) < 6:
                continue
            cp = int(f[0], 16)
            if f[3] != "0":
                ccc[cp] = int(f[3])
            d = f[5].strip()
            # A decomposition beginning with "<" is compatibility, not
            # canonical. NFC uses canonical only.
            if d and not d.startswith("<"):
                decomp[cp] = [int(x, 16) for x in d.split()]
    return ccc, decomp


def parse_props(path, wanted):
    """DerivedCoreProperties.txt -> {property: set(codepoints)}."""
    out = {w: set() for w in wanted}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            s = _strip(line)
            if not s:
                continue
            rng, _, prop = (x.strip() for x in s.partition(";"))
            if prop not in out:
                continue
            if ".." in rng:
                lo, hi = (int(x, 16) for x in rng.split(".."))
            else:
                lo = hi = int(rng, 16)
            out[prop].update(range(lo, hi + 1))
    return out


def parse_exclusions(path):
    excl = set()
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            s = _strip(line)
            if s:
                excl.add(int(s.split(";")[0].strip(), 16))
    return excl


def parse_script_aliases(path):
    """PropertyValueAliases.txt's `sc` (Script) rows -> ({long: short}, {all
    registered short codes}). The short (4-letter, ISO 15924-shaped) code is
    this generator's canonical script id namespace: Scripts.txt spells values
    by their LONG name ("Latin"), ScriptExtensions.txt by the SHORT code
    ("Latn") already -- translating Scripts.txt's long names through this
    table is what lets both files share one id space.

    The full REGISTERED set (176 entries in UCD 17.0.0), not just the subset
    actually assigned to some codepoint this version (174 -- verified
    directly: `Hrkt` and `Zzzz` are registered but carry zero codepoints),
    is what this generator assigns ids over. `Zzzz` (Unknown) is exactly the
    case that matters: it never appears as an explicit Scripts.txt value
    (see that file's own "@missing: 0000..10FFFF; Unknown" header -- it is
    the default for a GAP, not a listed row), but the trie still needs a
    well-defined default value for every codepoint that gap covers.
    """
    long2short, shorts = {}, set()
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            s = _strip(line)
            if not s or not s.startswith("sc"):
                continue
            f = [x.strip() for x in s.split(";")]
            if len(f) < 3 or f[0] != "sc":
                continue
            short, long = f[1], f[2]
            long2short[long] = short
            shorts.add(short)
    return long2short, shorts


def parse_scripts(path, long2short):
    """Scripts.txt -> {cp: short_code}. Expanded to one entry per codepoint
    (not kept as ranges) so the trie builder below (`build_trie`, shared with
    ccc/xid) needs no separate range-aware path -- Scripts.txt is small
    enough (a few hundred ranges over the assigned subset of 0..10FFFF) that
    this costs nothing worth avoiding."""
    out = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            s = _strip(line)
            if not s:
                continue
            rng, _, long = (x.strip() for x in s.partition(";"))
            short = long2short[long]
            if ".." in rng:
                lo, hi = (int(x, 16) for x in rng.split(".."))
            else:
                lo = hi = int(rng, 16)
            for cp in range(lo, hi + 1):
                out[cp] = short
    return out


def parse_script_extensions(path):
    """ScriptExtensions.txt -> {cp: [short_code, ...]}. Already short-coded
    in the source file, so no long2short translation is needed here.
    Expanded to one entry per codepoint for the same reason as
    parse_scripts, above -- verified this creates no ambiguity: no
    codepoint appears in more than one ScriptExtensions.txt range (checked
    directly against UCD 17.0.0: 669 distinct codepoints across 206 ranges,
    zero overlaps)."""
    out = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            s = _strip(line)
            if not s:
                continue
            rng, _, vals = s.partition(";")
            rng = rng.strip()
            codes = vals.split()
            if ".." in rng:
                lo, hi = (int(x, 16) for x in rng.split(".."))
            else:
                lo = hi = int(rng, 16)
            for cp in range(lo, hi + 1):
                out[cp] = codes
    return out


# --------------------------------------------------------------------------
# Two-stage trie with deduplicated blocks
# --------------------------------------------------------------------------

def build_trie(values, default, width):
    """values: {cp: int}. Returns (stage1 bytes, stage2 bytes, n_blocks).

    Blocks are deduplicated by content. Dedup order follows codepoint order,
    never dict iteration order, so the output is reproducible.
    """
    pack = {1: "<B", 2: "<H", 4: "<I"}[width]
    blocks, index, stage1 = [], {}, []
    for base in range(0, MAX_CP, BLOCK_SIZE):
        blk = bytes().join(
            struct.pack(pack, values.get(base + i, default))
            for i in range(BLOCK_SIZE)
        )
        if blk not in index:
            index[blk] = len(blocks)
            blocks.append(blk)
        stage1.append(index[blk])
    if len(blocks) > 0xFFFF:
        raise SystemExit(f"stage-2 block count {len(blocks)} exceeds u16 index")
    return (b"".join(struct.pack("<H", i) for i in stage1),
            b"".join(blocks),
            len(blocks))


# --------------------------------------------------------------------------
# Table emission
# --------------------------------------------------------------------------

def emit_trie(outdir, name, values, default, width, meta):
    s1, s2, nblocks = build_trie(values, default, width)
    _write(outdir, f"{name}_stage1.bin", s1)
    _write(outdir, f"{name}_stage2.bin", s2)
    meta.append((f"{name.upper()}_BLOCKS", nblocks))
    meta.append((f"{name.upper()}_STAGE1_BYTES", len(s1)))
    meta.append((f"{name.upper()}_STAGE2_BYTES", len(s2)))
    return len(s1) + len(s2)


def emit_decomp(outdir, decomp, meta):
    """Canonical decomposition: sorted keys + (offset,len) + a flat pool.

    Sorted so the assembly side can binary-search; a hash would be smaller and
    would make iteration order depend on the hash function, which §9.3 forbids
    for anything reaching output.
    """
    keys, vals, pool = [], [], []
    for cp in sorted(decomp):
        seq = decomp[cp]
        if len(seq) > 2:
            raise SystemExit(f"canonical decomposition of U+{cp:04X} is >2")
        keys.append(cp)
        vals.append((len(pool) << 8) | len(seq))
        pool.extend(seq)
    _write(outdir, "decomp_keys.bin", b"".join(struct.pack("<I", k) for k in keys))
    _write(outdir, "decomp_vals.bin", b"".join(struct.pack("<I", v) for v in vals))
    _write(outdir, "decomp_pool.bin", b"".join(struct.pack("<I", c) for c in pool))
    meta.append(("DECOMP_COUNT", len(keys)))
    meta.append(("DECOMP_POOL_COUNT", len(pool)))


def emit_compose(outdir, ccc, decomp, exclusions, meta):
    """Canonical composition pairs, minus the three classes UAX #15 excludes.

    A pair (a, b) -> c is emitted only when all three hold:

      1. c's canonical decomposition has exactly TWO elements. A singleton
         decomposition never recomposes.
      2. c is not listed in CompositionExclusions.txt.
      3. **ccc(a) == 0** -- the left operand is a starter.

    Rule 3 is the "non-starter decompositions" exclusion class, and it is NOT
    covered by rule 2: `CompositionExclusions.txt` carries only the PRIMARY
    exclusion list (81 rows -- script specifics and post-composition version),
    not the full `Full_Composition_Exclusion` derivation, which lives in
    `DerivedNormalizationProps.txt` and is not among this generator's inputs.
    An earlier version of this function stated rule 3 in its docstring and did
    not implement it, and four pairs survived into the checked-in blobs:

        U+0308 (ccc 230) + U+0301 -> U+0344
        U+0F71 (ccc 129) + U+0F72 -> U+0F73
        U+0F71 (ccc 129) + U+0F74 -> U+0F75
        U+0F71 (ccc 129) + U+0F80 -> U+0F81

    All four composites are Full_Composition_Exclusion=Yes in UCD 17.0.0, and
    none appears in CompositionExclusions.txt -- verified directly, which is
    what identified rule 2's incompleteness as the root cause.

    Their presence did not change any normalization result, because UAX #15's
    composition step only ever offers the last STARTER as a left operand, so
    such a pair is never looked up: `run_normalization_test` below passed
    100 170/0 both with and without them (both runs performed, not assumed).
    They were removed anyway. `compose_keys.bin` is public data behind a public
    entry point -- `compiler/shared/unicode/nfc.inc`'s `nfc_compose_pair` --
    and a caller that hands it a non-starter left operand must get "does not
    compose", not a composition UAX #15 says cannot exist. "No caller we
    currently have would ask" is an argument about callers, not about the
    table being right.

    Rule 3 needs no UCD file this generator does not already read: the
    combining class is the same `ccc` map the trie is built from.
    """
    pairs = []
    for cp in sorted(decomp):
        seq = decomp[cp]
        if len(seq) != 2 or cp in exclusions:
            continue
        if ccc.get(seq[0], 0) != 0:
            continue                    # non-starter decomposition (rule 3)
        pairs.append(((seq[0] << 32) | seq[1], cp))
    pairs.sort(key=lambda p: p[0])
    _write(outdir, "compose_keys.bin", b"".join(struct.pack("<Q", k) for k, _ in pairs))
    _write(outdir, "compose_vals.bin", b"".join(struct.pack("<I", v) for _, v in pairs))
    meta.append(("COMPOSE_COUNT", len(pairs)))


def emit_script(outdir, script_ids, script_map, meta):
    """Primary Script property (UAX #24): two-stage trie, u8 values -- same
    shape as ccc/xid, and for the same reason: 176 possible ids fit a byte
    with room to spare. Default is Zzzz/Unknown's id, applied to every
    codepoint Scripts.txt does not explicitly cover, per that file's own
    "@missing: 0000..10FFFF; Unknown"."""
    values = {cp: script_ids[short] for cp, short in script_map.items()}
    default = script_ids["Zzzz"]
    return emit_trie(outdir, "script", values, default, 1, meta)


def emit_script_ext(outdir, script_ids, ext_map, meta):
    """Script_Extensions (UTS #39 / UAX #24): sparse sorted array, same shape
    as decomp -- binary search, not hashing, for the same §9.3 determinism
    reason a hash table would make pool order depend on the hash function.
    A codepoint absent from this table has no listed extensions; its only
    script is whatever emit_script's trie gives it (EXS-E0104's mixed-script
    check resolves a codepoint's script SET this way -- script.inc's
    script_resolve does exactly this fallback).

    NOT content-deduplicated (decomp_pool-style), even though 118 of the 206
    ScriptExtensions.txt ranges share their script-set content with another
    range: measured at 1,660 bytes without dedup vs 536 with -- a ~1KB
    difference not worth a second dedup path for a table this small.
    """
    keys, vals, pool = [], [], []
    for cp in sorted(ext_map):
        codes = ext_map[cp]
        keys.append(cp)
        vals.append((len(pool) << 8) | len(codes))
        pool.extend(script_ids[c] for c in codes)
    _write(outdir, "scriptext_keys.bin", b"".join(struct.pack("<I", k) for k in keys))
    _write(outdir, "scriptext_vals.bin", b"".join(struct.pack("<I", v) for v in vals))
    _write(outdir, "scriptext_pool.bin", bytes(pool))
    meta.append(("SCRIPTEXT_COUNT", len(keys)))
    meta.append(("SCRIPTEXT_POOL_COUNT", len(pool)))
    meta.append(("SCRIPTEXT_MAX_COUNT", max((len(c) for c in ext_map.values()), default=0)))


def _write(outdir, name, data):
    with open(os.path.join(outdir, name), "wb") as fh:
        fh.write(data)


def emit_inc(outdir, meta, ucd_version, script_ids):
    """Assemble-time constants and incbin directives for the asm side."""
    lines = [
        "; compiler/shared/unicode/tables/tables.inc",
        "; SPDX-License-Identifier: GPL-3.0-or-later",
        "; Copyright (C) 2026 The Exsecutor authors.",
        ";",
        "; GENERATED by tools/ucd-gen/gen.py -- do not edit.",
        f"; UCD {ucd_version}. Regenerate and diff byte-for-byte to verify.",
        "; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1, §8.2, §11",
        "; " + "-" * 73,
        "",
        f"UCD_VERSION_STR equ '{ucd_version}'",
        f"UNI_BLOCK_SHIFT = {BLOCK_SHIFT}",
        f"UNI_BLOCK_SIZE  = {BLOCK_SIZE}",
        f"UNI_STAGE1_LEN  = {STAGE1_LEN}",
        f"UNI_MAX_CP      = 0x{MAX_CP:X}",
        "",
    ]
    for k, v in meta:
        lines.append(f"UNI_{k} = {v}")
    lines += [
        "",
        "; Script property ids -- one per short (ISO 15924-shaped) code",
        "; registered under PropertyValueAliases.txt's `sc` property, UCD",
        f"; {ucd_version}, {len(script_ids)} entries, sorted alphabetically (a fixed,",
        "; hash-free order -- CLAUDE.md \"Determinism is not optional\") and",
        "; assigned 0.. in that order. script.inc's script_of/script_resolve",
        "; return one of these. UNI_SCRIPT_ZZZZ (Unknown) is the primary-script",
        "; trie's default value; UNI_SCRIPT_HRKT (Katakana_Or_Hiragana) and",
        "; UNI_SCRIPT_ZZZZ are both registered aliases with zero codepoints in",
        "; this UCD version -- reserved, not currently reachable from either",
        "; table below except ZZZZ via the trie default.",
        "",
    ]
    for short in sorted(script_ids):
        lines.append(f"UNI_SCRIPT_{short.upper()} = {script_ids[short]}")
    lines += [
        "",
        "; Raw blobs. Architecture-neutral, little-endian, incbin'd so the",
        "; build closure stays {fasmg} (spec §18.1).",
        "",
    ]
    for label, fname in [
        ("uni_ccc_stage1", "ccc_stage1.bin"), ("uni_ccc_stage2", "ccc_stage2.bin"),
        ("uni_xid_stage1", "xid_stage1.bin"), ("uni_xid_stage2", "xid_stage2.bin"),
        ("uni_decomp_keys", "decomp_keys.bin"), ("uni_decomp_vals", "decomp_vals.bin"),
        ("uni_decomp_pool", "decomp_pool.bin"),
        ("uni_compose_keys", "compose_keys.bin"), ("uni_compose_vals", "compose_vals.bin"),
        ("uni_script_stage1", "script_stage1.bin"), ("uni_script_stage2", "script_stage2.bin"),
        ("uni_scriptext_keys", "scriptext_keys.bin"), ("uni_scriptext_vals", "scriptext_vals.bin"),
        ("uni_scriptext_pool", "scriptext_pool.bin"),
    ]:
        lines.append(f"{label}:  file '{fname}'")
    lines.append("")
    with open(os.path.join(outdir, "tables.inc"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))


# --------------------------------------------------------------------------
# Reference NFC, used only to validate the tables we just emitted
# --------------------------------------------------------------------------

class Tables:
    def __init__(self, ccc, decomp, exclusions):
        self.ccc = ccc
        self.decomp = decomp
        # Exactly the three rules `emit_compose` applies -- including
        # ccc(first) == 0. This map is what validates the emitted table, so a
        # filter present in one and absent from the other would make the check
        # weaker than it looks.
        self.comp = {}
        for cp, seq in decomp.items():
            if len(seq) == 2 and cp not in exclusions and ccc.get(seq[0], 0) == 0:
                self.comp[(seq[0], seq[1])] = cp

    def decompose(self, cp, out):
        if S_BASE <= cp < S_BASE + S_COUNT:
            i = cp - S_BASE
            out.append(L_BASE + i // N_COUNT)
            out.append(V_BASE + (i % N_COUNT) // T_COUNT)
            if i % T_COUNT:
                out.append(T_BASE + i % T_COUNT)
            return
        seq = self.decomp.get(cp)
        if seq is None:
            out.append(cp)
            return
        for c in seq:
            self.decompose(c, out)

    def nfc(self, s):
        # 1. canonical decomposition
        d = []
        for ch in s:
            self.decompose(ord(ch), d)
        # 2. canonical ordering (stable bubble on combining class)
        i = 1
        while i < len(d):
            a, b = self.ccc.get(d[i - 1], 0), self.ccc.get(d[i], 0)
            if a > b > 0:
                d[i - 1], d[i] = d[i], d[i - 1]
                i = max(1, i - 1)
            else:
                i += 1
        # 3. canonical composition (UAX #15 recomposition)
        if not d:
            return ""
        out = [d[0]]
        last_starter = 0 if self.ccc.get(d[0], 0) == 0 else -1
        last_ccc = self.ccc.get(d[0], 0)
        for cp in d[1:]:
            c = self.ccc.get(cp, 0)
            composed = None
            if last_starter >= 0 and (last_ccc < c or (last_ccc == 0 and c == 0)):
                composed = self._pair(out[last_starter], cp)
            if composed is not None:
                out[last_starter] = composed
                if c == 0:
                    last_ccc = 0
                continue
            out.append(cp)
            if c == 0:
                last_starter = len(out) - 1
            last_ccc = c
        return "".join(chr(c) for c in out)

    def _pair(self, a, b):
        if L_BASE <= a < L_BASE + L_COUNT and V_BASE <= b < V_BASE + V_COUNT:
            return S_BASE + ((a - L_BASE) * V_COUNT + (b - V_BASE)) * T_COUNT
        if (S_BASE <= a < S_BASE + S_COUNT and (a - S_BASE) % T_COUNT == 0
                and T_BASE < b < T_BASE + T_COUNT):
            return a + (b - T_BASE)
        return self.comp.get((a, b))


def run_normalization_test(tables, path):
    """UCD NormalizationTest.txt: c1..c5. NFC(c1)=NFC(c2)=NFC(c3)=c2,
    NFC(c4)=NFC(c5)=c4."""
    passed = failed = 0
    firstfail = None
    with open(path, encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            s = _strip(line)
            if not s or s.startswith("@"):
                continue
            cols = [c.strip() for c in s.split(";")[:5]]
            if len(cols) < 5:
                continue
            c = ["".join(chr(int(x, 16)) for x in col.split()) for col in cols]
            for src, want in ((c[0], c[1]), (c[1], c[1]), (c[2], c[1]),
                              (c[3], c[3]), (c[4], c[3])):
                if tables.nfc(src) == want:
                    passed += 1
                else:
                    failed += 1
                    if firstfail is None:
                        firstfail = (lineno, s)
    return passed, failed, firstfail


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ucd", required=True, help="directory holding UCD .txt files")
    ap.add_argument("--out", required=True, help="output directory for tables")
    ap.add_argument("--version", default="17.0.0", help="UCD version string")
    ap.add_argument("--check-only", action="store_true",
                    help="run NormalizationTest.txt and exit; write nothing")
    a = ap.parse_args()

    u = lambda n: os.path.join(a.ucd, n)
    ccc, decomp = parse_unicode_data(u("UnicodeData.txt"))
    props = parse_props(u("DerivedCoreProperties.txt"), {"XID_Start", "XID_Continue"})
    exclusions = parse_exclusions(u("CompositionExclusions.txt"))
    long2short, script_shorts = parse_script_aliases(u("PropertyValueAliases.txt"))
    script_map = parse_scripts(u("Scripts.txt"), long2short)
    scriptext_map = parse_script_extensions(u("ScriptExtensions.txt"))
    script_ids = {short: i for i, short in enumerate(sorted(script_shorts))}

    tables = Tables(ccc, decomp, exclusions)
    ntpath = u("NormalizationTest.txt")
    passed, failed, firstfail = run_normalization_test(tables, ntpath)
    print(f"NormalizationTest.txt: {passed} passed, {failed} failed")
    if failed:
        print(f"  first failure, line {firstfail[0]}: {firstfail[1]}", file=sys.stderr)
        return 1
    if a.check_only:
        return 0

    os.makedirs(a.out, exist_ok=True)
    meta = []
    total = emit_trie(a.out, "ccc", ccc, 0, 1, meta)

    xid = {}
    for cp in props["XID_Start"]:
        xid[cp] = xid.get(cp, 0) | 1
    for cp in props["XID_Continue"]:
        xid[cp] = xid.get(cp, 0) | 2
    total += emit_trie(a.out, "xid", xid, 0, 1, meta)

    total += emit_script(a.out, script_ids, script_map, meta)
    emit_script_ext(a.out, script_ids, scriptext_map, meta)

    emit_decomp(a.out, decomp, meta)
    emit_compose(a.out, ccc, decomp, exclusions, meta)
    emit_inc(a.out, meta, a.version, script_ids)

    print(f"ccc entries      {len(ccc)}")
    print(f"decompositions   {len(decomp)}")
    print(f"XID_Start        {len(props['XID_Start'])}")
    print(f"XID_Continue     {len(props['XID_Continue'])}")
    print(f"exclusions       {len(exclusions)}")
    print(f"scripts          {len(script_shorts)} registered, {len(set(script_map.values()))} used as primary")
    print(f"script extensions{len(scriptext_map):>5} codepoints")
    for k, v in meta:
        print(f"  UNI_{k} = {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
