#!/usr/bin/env python3
# tools/identify.py
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
# A SIGNATURE IDENTIFIER for binaries `exsc` built: given a static ELF, say
# which known library it contains -- or say plainly that it matches none. It
# is the first slice of a decompiler, kept to the one question a verifier can
# check: "is this the library I think it is?" It recovers no source.
#
# Usage (python3, stdlib only; objdump from binutils; for build-db and
# --self-test also build/exsc and fasmg -- FASMG=/path or fasmg on PATH):
#
#   tools/identify.py build-db DIR --out DB.json
#       Compile every <name>.exsc in DIR as ONE unit with DIR's shared
#       contract and driver (default contractus.exsc first, probatio.exsc
#       last -- tests/utf16/matrix.py's build_reference order), and record each
#       library's signature. DIR is snapshotted before anything is compiled;
#       the sha256 of every SOURCE used, of each BINARY made, of exsc and of
#       fasmg are recorded. A library that does not compile is listed under
#       "skipped" with exsc's exit status and first diagnostic line, never
#       silently dropped.
#   tools/identify.py BINARY [--db DB.json] [--exsc PATH] [--verbose]
#       Identify one binary. Prints tier 1, then tier 2's best match, its
#       score, the runner-up and the MARGIN, then one VERDICT line.
#       --db defaults to build/identify-db.json.
#   tools/identify.py --self-test [--src DIR]
#       Build the DB from examples/utf16 (a snapshot, in a temp dir) and run
#       every check below; non-zero exit on any failure. Prints what each
#       check proved, including the full tier-2 confusion matrix.
#
# Exit status: 0 = identified (or build-db / self-test passed). 1 = no match
# (or self-test failed). 2 = usage or environment error: missing tool, not
# an ELF64 x86-64 file, unreadable DB, or a DB made by a DIFFERENT exsc or
# objdump than the one in hand (see "compiler-version sensitivity" below).
#
# Deterministic: same inputs, byte-identical stdout. No clock, no network,
# no $HOME; children (exsc, fasmg, objdump) get an environment of exactly
# INCLUDE=<repo>/vendor/fasmg-x86 and LC_ALL=C. Output and the DB are
# ordered by name, never by a hash or an address.
#
# ---------------------------------------------------------------------------
# HOW IT WORKS
#
# TIER 1 -- exact. The sha256 of the whole file against every DB binary.
# exsc is deterministic (spec 9.3), so the same library + same driver + same
# exsc + same fasmg is the same bytes. It recognises a known library only in
# the known driver, and says nothing about anything else.
#
# TIER 2 -- a known LIBRARY in an UNKNOWN driver/prelude context. A built
# binary has no symbol table and no section headers, so the signature comes
# from the code bytes:
#
#   1. Disassemble exactly as tools/syscall-audit.sh does: carve each
#      executable PT_LOAD out of the file, `objdump -D -b binary -m
#      i386:x86-64 -M intel --adjust-vma`, the sweep anchored at the entry
#      point (fasmg overlaps the ELF header with that segment).
#   2. Normalise every instruction: branch/call targets become T, every
#      rip-relative displacement becomes rip+D, every immediate or
#      displacement that falls inside the image's own address range becomes
#      A. These move whenever any code before or after them changes size.
#      KEPT: mnemonics, registers, operand shapes, every other immediate --
#      0x80, 0xc2, 0xe0, 0xf4, 0x10000, 0xd800 ARE the library -- and the
#      [rbp-N] stack-slot offsets. Masking those offsets was measured once,
#      in a scratch harness this file does not carry, on whole-binary
#      signatures: unrelated programs then scored up to 0.68 (n=8) and 0.82
#      (n=4), and triplex-held-out scored 0.99 as duplex [UNREPRODUCED].
#   3. A library's SIGNATURE is the n-grams (n = NGRAM = 8 instructions) of
#      its OWN functions only, each taken inside one function. At build-db
#      time the symbols still exist: the fasmg text exsc emits has labels,
#      and a second assembly with `display` lines appended reads each
#      function's address out of fasmg itself (the second binary is checked
#      byte-identical to the first, so the labels describe the bytes of
#      record). The functions are those the library's source declares
#      (`functio NAME`). The shared prelude and driver are thus subtracted by
#      symbol, not guessed, and no signature n-gram straddles the library
#      and its driver -- one that did would exist only in the DB's driver
#      (the scratch harness's whole-binary variant lost 1.7% on the held-out
#      driver that way [UNREPRODUCED]).
#   4. A QUERY has no symbols. Its stream is cut at every `push rbp` / `mov
#      rbp,rsp` (exsc opens every function with that frame) and its features
#      are the n-grams inside each piece plus each piece's prefixes shorter
#      than n, so a library function shorter than n -- one whole-function
#      shingle -- still matches at the start of its piece.
#   5. Score = IDF-weighted CONTAINMENT of a library's signature in the
#      query: sum of w(s) over the library's n-grams present in the binary,
#      divided by sum of w(s) over all of them, with w(s) = ln(N / df(s)),
#      N the number of libraries in the DB and df(s) how many signatures
#      contain s. An n-gram every library has weighs nothing; one only this
#      library has weighs most. Containment, not Jaccard: an unknown driver
#      ADDS code and must not lower the score.
#   6. Rule: identified iff the best score >= T_IDENT (0.960). If two or more
#      libraries clear it, the one whose matched code weighs most wins and
#      the others are named as "also present" (a library whose code is a
#      subset of another's). Otherwise: no match, closest candidate shown.
#
# MEASURED -- reproduce with `tools/identify.py --self-test --sweep
# 2,4,6,8,10,12,16,24`. On 2026-10-03, 27 libraries of examples/utf16 (sha256
# of each source printed by that run), "true" = each library in the second
# driver + one single-constant mutant per library; "wrong" = every off-
# diagonal score in the second driver, both hold-one-out queries per library,
# and 9 negative controls:
#   n             2      4      6      8      10     12     16     24
#   true min idf  .9943  .9925  .9898  .9867  .9835  .9803  .9737  .9604
#   wrong max idf .9528  .9447  .9394  .9354  .9334  .9321  .9302  .9262
#   gap idf       .0415  .0478  .0504  .0513  .0501  .0482  .0435  .0342
#   gap flat      .0331  .0403  .0429  .0437  .0424  .0403  .0365  .0285
#   negative max  .1622  .0649  .0362  .0143  .0038  .0019  .0000  .0000
# Larger n tolerates a changed constant less (it spoils n shingles); smaller
# n tells near-duplicates apart less. n=8 with IDF has the widest gap; its
# midpoint is 0.9610 and T_IDENT = 0.960. Every "wrong max" is the same
# pair: triplex held out of the DB, scored as duplex -- triplex carries
# duplex's visum_a, visum_b, congruunt and inscribe verbatim. A scratch run
# with 108 mutants (4 per library) agreed: true min 0.9859, gap 0.0506 at
# n=8 [UNREPRODUCED].
#
# WHAT THIS DOES NOT DO -- said plainly:
#   - It is not a decompiler. No source, no names, no types are recovered.
#     It answers "which known library's code is in here", nothing more.
#   - exsc/fasmg output only. The normalisation assumes exsc's code shape
#     (frame prologue per function, unoptimised stack-slot code); anything
#     else simply scores low. A non-exsc binary is "no match", not an error.
#   - Compiler-version sensitivity. A different exsc changes code generation
#     (and the prelude), so signatures from one exsc are not valid for
#     another. The DB records exsc's sha256 and identify REFUSES (exit 2)
#     when the exsc it is pointed at (--exsc, default build/exsc) differs.
#     It cannot check which exsc built the BINARY -- nothing in a stripped
#     ELF says -- it assumes the binary came from that same exsc. objdump's
#     version is recorded and checked too: the features are its text.
#   - Position/size sensitivity is removed only for what step 2 masks. Stack
#     offsets are kept, so an edit that adds or removes a local variable
#     renumbers a function's slots and lowers the score sharply: tier 2
#     tolerates a changed constant (measured), not a refactor ([UNTESTED]
#     beyond single-constant mutants).
#   - "Contains" is not "is". A binary that contains library X's code plus
#     anything else -- a bigger driver, or a different library that copied
#     X's functions -- scores X high. When the extra code is another known
#     library, the weight tie-break picks it; when it is unknown, nothing in
#     the bytes tells a driver from a library, and the threshold is the only
#     guard. The self-test's hold-one-out check measures exactly that risk.
#   - Calibration is corpus-specific: T_IDENT came from 27 UTF-8 -> UTF-16
#     libraries sharing one contract. Another corpus needs its own
#     --self-test-style measurement before its verdicts mean anything.
# ---------------------------------------------------------------------------

import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DB = os.path.join(REPO, 'build', 'identify-db.json')
DEFAULT_EXSC = os.path.join(REPO, 'build', 'exsc')
FASMG_INCLUDE = os.path.join(REPO, 'vendor', 'fasmg-x86')
HOSTIS = 'x86_64-linux'

DB_FORMAT = 'exsecutor-identify/1'
NORMALISATION = ('v1: objdump -M intel; branch targets -> T; rip+disp -> '
                 'rip+D; in-image values -> A; other immediates and stack '
                 'offsets kept; functions cut at push rbp/mov rbp,rsp')
NGRAM = 8
T_IDENT = 0.960

CHILD_ENV = {'INCLUDE': FASMG_INCLUDE, 'LC_ALL': 'C'}


class EnvError(Exception):
    """Usage or environment error: exit status 2."""


# ---------------------------------------------------------------------------
# ELF: read the program headers ourselves (no section headers exist).

def read_elf(path):
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError as e:
        raise EnvError('cannot read %s: %s' % (path, e.strerror))
    if len(data) < 64 or data[:4] != b'\x7fELF':
        raise EnvError('%s: not an ELF file' % path)
    if data[4] != 2 or data[5] != 1:
        raise EnvError('%s: not a little-endian ELF64 file' % path)
    e_machine = struct.unpack_from('<H', data, 18)[0]
    if e_machine != 62:
        raise EnvError('%s: not x86-64 (e_machine %d)' % (path, e_machine))
    entry = struct.unpack_from('<Q', data, 24)[0]
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', data, 54)
    loads = []
    for i in range(phnum):
        off = phoff + i * phentsize
        if off + 56 > len(data):
            raise EnvError('%s: program header %d runs past the file' % (path, i))
        (p_type, p_flags, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz,
         _p_align) = struct.unpack_from('<IIQQQQQQ', data, off)
        if p_type == 1:
            loads.append((p_flags, p_offset, p_vaddr, p_filesz, p_memsz))
    if not loads:
        raise EnvError('%s: no PT_LOAD segment' % path)
    lo = min(s[2] for s in loads)
    hi = max(s[2] + s[4] for s in loads)
    regions = []   # (vaddr, bytes): executable bytes, entry-anchored if possible
    for flags, offset, vaddr, filesz, _memsz in loads:
        if not flags & 1 or filesz == 0:
            continue
        if vaddr <= entry < vaddr + filesz:
            start = offset + (entry - vaddr)
            regions.append((entry, data[start:offset + filesz]))
        else:
            regions.append((vaddr, data[offset:offset + filesz]))
    regions.sort()
    return {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data),
            'entry': entry, 'lo': lo, 'hi': hi, 'regions': regions}


# ---------------------------------------------------------------------------
# Disassembly and normalisation.

LINE_RE = re.compile(r'^\s*([0-9a-f]+):\t([0-9a-f]{2}(?: [0-9a-f]{2})*)\s*\t(.*)$')
HEX_RE = re.compile(r'0x[0-9a-f]+')
RIP_RE = re.compile(r'rip[+-]0x[0-9a-f]+')
BRANCH_RE = re.compile(r'^(call|jmp|j[a-z]+|loop[a-z]*|xbegin)$')
PREFIXES = {'lock', 'rep', 'repz', 'repnz', 'repe', 'repne', 'data16',
            'addr32', 'notrack', 'bnd', 'cs', 'ds', 'es', 'ss', 'fs', 'gs'}


def objdump_version(objdump):
    try:
        r = subprocess.run([objdump, '--version'], capture_output=True,
                           text=True, env=CHILD_ENV)
    except OSError:
        raise EnvError('objdump not found: %s' % objdump)
    return (r.stdout.splitlines() or ['?'])[0].strip()


def disassemble_region(objdump, vaddr, blob, workdir):
    fd, path = tempfile.mkstemp(suffix='.text', dir=workdir)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(blob)
        r = subprocess.run([objdump, '-D', '-b', 'binary', '-m', 'i386:x86-64',
                            '-M', 'intel', '--adjust-vma=0x%x' % vaddr, path],
                           capture_output=True, text=True, env=CHILD_ENV)
    except OSError:
        raise EnvError('objdump not found: %s' % objdump)
    finally:
        if os.path.exists(path):
            os.unlink(path)
    if r.returncode != 0:
        raise EnvError('objdump failed: %s' % r.stderr.strip()[:300])
    return r.stdout


def normalise(asm, lo, hi):
    """One objdump instruction text -> its normalised form, or None."""
    asm = asm.split('#', 1)[0]
    asm = re.sub(r'<[^>]*>', '', asm).strip()
    if not asm:
        return None
    words = asm.split()
    mnem = []
    while words and words[0] in PREFIXES and len(words) > 1:
        mnem.append(words.pop(0))
    mnem.append(words.pop(0))
    ops = ''.join(words)
    if BRANCH_RE.match(mnem[-1]) and HEX_RE.fullmatch(ops or '-'):
        ops = 'T'
    else:
        ops = RIP_RE.sub('rip+D', ops)

        def mask(m):
            v = int(m.group(0), 16)
            return 'A' if lo <= v < hi else m.group(0)
        ops = HEX_RE.sub(mask, ops)
    text = ' '.join(mnem)
    return text + ' ' + ops if ops else text


def instructions(objdump, elf, workdir):
    """[(address, normalised text)] over every executable region."""
    out = []
    for vaddr, blob in elf['regions']:
        for line in disassemble_region(objdump, vaddr, blob, workdir).splitlines():
            m = LINE_RE.match(line)
            if not m:
                continue
            norm = normalise(m.group(3), elf['lo'], elf['hi'])
            if norm is not None:
                out.append((int(m.group(1), 16), norm))
    return out


# ---------------------------------------------------------------------------
# Shingles.

def shingle_hash(lines):
    return hashlib.blake2b('\n'.join(lines).encode(), digest_size=8).hexdigest()


def function_shingles(seq, n=NGRAM):
    """A DB function's n-grams; a function shorter than n is one shingle."""
    if len(seq) < n:
        return {shingle_hash(seq)} if seq else set()
    return {shingle_hash(seq[i:i + n]) for i in range(len(seq) - n + 1)}


def query_features(insns, n=NGRAM):
    """A query's features: n-grams inside each prologue-delimited group, plus
    each group's prefixes shorter than n (so a short DB function, which
    starts a group, still matches)."""
    texts = [t for _, t in insns]
    groups, cur = [], []
    for i, t in enumerate(texts):
        if (t == 'push rbp' and i + 1 < len(texts)
                and texts[i + 1] == 'mov rbp,rsp' and cur):
            groups.append(cur)
            cur = []
        cur.append(t)
    if cur:
        groups.append(cur)
    feats = set()
    for g in groups:
        for k in range(1, min(n, len(g) + 1)):
            feats.add(shingle_hash(g[:k]))
        for i in range(len(g) - n + 1):
            feats.add(shingle_hash(g[i:i + n]))
    return feats


# ---------------------------------------------------------------------------
# Toolchain and building.

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 16), b''):
            h.update(chunk)
    return h.hexdigest()


class Toolchain:
    def __init__(self, exsc, fasmg, objdump):
        self.exsc, self.fasmg, self.objdump = exsc, fasmg, objdump

    @classmethod
    def find(cls, exsc=None, need_build=True, objdump=None):
        exsc = os.path.abspath(exsc or DEFAULT_EXSC)
        if not os.path.isfile(exsc):
            raise EnvError('exsc not found at %s (run `make`, or pass --exsc)' % exsc)
        objdump = objdump or shutil.which('objdump')
        if not objdump:
            raise EnvError('objdump (binutils) not found on PATH')
        fasmg = None
        if need_build:
            fasmg = os.environ.get('FASMG') or shutil.which('fasmg')
            if not fasmg:
                raise EnvError('fasmg not found: put it on PATH or set FASMG')
            fasmg = os.path.abspath(fasmg)
        return cls(exsc, fasmg, objdump)


class BuildError(Exception):
    def __init__(self, stage, rc, msg):
        Exception.__init__(self, '%s exit %d: %s' % (stage, rc, msg))
        self.stage, self.rc, self.msg = stage, rc, msg


def first_line(raw):
    for line in raw.decode('utf-8', 'replace').splitlines():
        if line.strip():
            return line.strip()[:200]
    return '(no output)'


def compile_unit(tc, sources, out_base):
    """exsc aedifica -> fasmg -> ELF. Returns (bin_path, asm_path)."""
    asm, binp = out_base + '.asm', out_base + '.bin'
    r = subprocess.run([tc.exsc, 'aedifica', '--hospes', HOSTIS] + sources +
                       ['-o', asm], capture_output=True, env=CHILD_ENV)
    if r.returncode != 0:
        rc = r.returncode if r.returncode >= 0 else 128 - r.returncode
        raise BuildError('exsc', rc, first_line(r.stderr or r.stdout))
    r = subprocess.run([tc.fasmg, asm, binp], capture_output=True, env=CHILD_ENV)
    if r.returncode != 0:
        lines = (r.stdout + r.stderr).decode('utf-8', 'replace').strip().splitlines()
        raise BuildError('fasmg', r.returncode, ' | '.join(lines[-3:])[:300])
    os.chmod(binp, 0o755)
    return binp, asm


FUNC_LABEL_RE = re.compile(r'^(bfausr_[A-Za-z0-9_]+):$')
DISPLAY_RE = re.compile(r'^exsident (\S+) (\d+)$')
DECL_RE = re.compile(r'^\s*(?:publica\s+)?functio\s+([A-Za-z_][A-Za-z0-9_]*)', re.M)


def label_addresses(tc, asm_path, bin_path):
    """Address of every function label (a label whose next line is `push
    rbp`) and of bfausr_trap, read from fasmg itself by a second assembly
    with `display` lines appended. The second binary must equal the first."""
    with open(asm_path, encoding='utf-8') as f:
        lines = f.read().split('\n')
    names = []
    for i in range(len(lines) - 1):
        m = FUNC_LABEL_RE.match(lines[i])
        if m and (lines[i + 1].split()[:2] == ['push', 'rbp']
                  or m.group(1) == 'bfausr_trap'):
            names.append(m.group(1))
    extra = ''.join('\nrepeat 1, a:%s\n\tdisplay "exsident %s ", `a, 10\nend repeat'
                    % (nm, nm) for nm in names)
    lab_asm = asm_path + '.labels.asm'
    lab_bin = asm_path + '.labels.bin'
    with open(lab_asm, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + extra + '\n')
    try:
        r = subprocess.run([tc.fasmg, lab_asm, lab_bin], capture_output=True,
                           env=CHILD_ENV)
        if r.returncode != 0:
            raise BuildError('fasmg(labels)', r.returncode,
                             first_line(r.stdout + r.stderr))
        if sha256_file(lab_bin) != sha256_file(bin_path):
            raise BuildError('fasmg(labels)', 0,
                             'appending display lines changed the bytes')
        out = {}
        for line in (r.stdout + r.stderr).decode('utf-8', 'replace').splitlines():
            m = DISPLAY_RE.match(line.strip())
            if m:
                out[m.group(1)[len('bfausr_'):]] = int(m.group(2))
        return out
    finally:
        for p in (lab_asm, lab_bin):
            if os.path.exists(p):
                os.unlink(p)


def library_entry(tc, name, lib_src, unit_sources, out_base, workdir):
    """Build one library in the DB's driver and extract its signature.
    Returns (entry, binary path, [(function, normalised sequence)])."""
    binp, asm = compile_unit(tc, unit_sources, out_base)
    labels = label_addresses(tc, asm, binp)
    with open(lib_src, encoding='utf-8') as f:
        declared = DECL_RE.findall(f.read())
    found = [d for d in declared if d in labels]
    if not found:
        raise BuildError('signature', 0, 'none of the declared functions %s is '
                         'among the emitted labels' % (declared or '(none)'))
    elf = read_elf(binp)
    insns = instructions(tc.objdump, elf, workdir)
    bounds = sorted(set(labels.values()) | {insns[-1][0] + 1})
    functions, seqs = [], []
    for fn in found:
        start = labels[fn]
        end = min(b for b in bounds if b > start)
        seq = [t for a, t in insns if start <= a < end]
        seqs.append((fn, seq))
        functions.append({
            'name': fn,
            'instructions': len(seq),
            'prologue': seq[:2] == ['push rbp', 'mov rbp,rsp'],
            'shingles': sorted(function_shingles(seq)),
        })
    os.unlink(asm)
    return {'name': name, 'binary_sha256': elf['sha256'],
            'binary_size': elf['size'], 'functions': functions,
            'not_emitted': [d for d in declared if d not in labels]}, binp, seqs


def snapshot(src_dir, dest):
    """Copy every *.exsc in src_dir to dest; return {file: sha256}."""
    os.makedirs(dest, exist_ok=True)
    out = {}
    for fn in sorted(os.listdir(src_dir)):
        if fn.endswith('.exsc') and os.path.isfile(os.path.join(src_dir, fn)):
            shutil.copyfile(os.path.join(src_dir, fn), os.path.join(dest, fn))
            out[fn] = sha256_file(os.path.join(dest, fn))
    return out


def build_db_from_snapshot(tc, snap, hashes, contract, driver, workdir,
                           exclude=(), jobs=None, keep_bins=None):
    """Build the DB dict from an already-snapshotted directory."""
    for fn in (contract, driver):
        if fn not in hashes:
            raise EnvError('shared file %s not found in the source directory' % fn)
    names = [fn[:-5] for fn in hashes
             if fn not in (contract, driver) and fn[:-5] not in exclude]
    bdir = os.path.join(workdir, 'db-build')
    os.makedirs(bdir, exist_ok=True)

    def one(name):
        srcs = [os.path.join(snap, contract), os.path.join(snap, name + '.exsc'),
                os.path.join(snap, driver)]
        try:
            entry, binp, seqs = library_entry(tc, name, srcs[1], srcs,
                                              os.path.join(bdir, name), workdir)
            entry['source_sha256'] = hashes[name + '.exsc']
            return name, entry, (binp, seqs), None
        except BuildError as e:
            return name, None, None, e

    libs, skipped = [], []
    with concurrent.futures.ThreadPoolExecutor(jobs or os.cpu_count() or 2) as ex:
        results = list(ex.map(one, names))
    for name, entry, kept, err in sorted(results, key=lambda r: r[0]):
        if err is None:
            libs.append(entry)
            if keep_bins is not None:
                keep_bins[name] = kept
        else:
            skipped.append({'name': name, 'source_sha256': hashes[name + '.exsc'],
                            'stage': err.stage, 'exit': err.rc, 'reason': err.msg})
    return {
        'format': DB_FORMAT,
        'ngram': NGRAM,
        'normalisation': NORMALISATION,
        'threshold': T_IDENT,
        'host': HOSTIS,
        'exsc_sha256': sha256_file(tc.exsc),
        'fasmg_sha256': sha256_file(tc.fasmg),
        'objdump': objdump_version(tc.objdump),
        'unit': [contract, '<library>.exsc', driver],
        'shared': [{'file': contract, 'sha256': hashes[contract]},
                   {'file': driver, 'sha256': hashes[driver]}],
        'libraries': libs,
        'skipped': skipped,
    }


def dump_db(db):
    return json.dumps(db, sort_keys=True, indent=1, separators=(',', ': ')) + '\n'


def load_db(path):
    try:
        with open(path, encoding='utf-8') as f:
            db = json.load(f)
    except OSError as e:
        raise EnvError('cannot read DB %s: %s (build one with `build-db`)'
                       % (path, e.strerror))
    except ValueError as e:
        raise EnvError('DB %s is not valid JSON: %s' % (path, e))
    if not isinstance(db, dict) or db.get('format') != DB_FORMAT:
        raise EnvError('DB %s: format is %r, this tool reads %r'
                       % (path, db.get('format') if isinstance(db, dict) else None,
                          DB_FORMAT))
    if db.get('ngram') != NGRAM or db.get('normalisation') != NORMALISATION:
        raise EnvError('DB %s was built with ngram=%r / normalisation %r; this '
                       'tool uses ngram=%d / %r -- rebuild the DB'
                       % (path, db.get('ngram'), db.get('normalisation'),
                          NGRAM, NORMALISATION))
    if not db.get('libraries'):
        raise EnvError('DB %s holds no libraries' % path)
    return db


# ---------------------------------------------------------------------------
# Scoring.

class Scorer:
    """IDF-weighted containment of each library's signature in a query."""

    def __init__(self, libraries):
        self.libs = sorted(libraries, key=lambda l: l['name'])
        self.n = len(self.libs)
        self.sig = {}
        df = {}
        for lib in self.libs:
            s = set()
            for fn in lib['functions']:
                s.update(fn['shingles'])
            self.sig[lib['name']] = sorted(s)
            for h in s:
                df[h] = df.get(h, 0) + 1
        if self.n > 1:
            self.w = {h: math.log(self.n / d) for h, d in df.items()}
        else:
            self.w = {h: 1.0 for h in df}
        self.den = {nm: sum(self.w[h] for h in hs) for nm, hs in self.sig.items()}
        self.exact = {lib['binary_sha256']: lib['name'] for lib in self.libs}

    def scores(self, feats):
        """[(name, score, matched_weight)], best first; ties by name."""
        out = []
        for nm in sorted(self.sig):
            num = sum(self.w[h] for h in self.sig[nm] if h in feats)
            den = self.den[nm]
            out.append((nm, num / den if den > 0 else 0.0, num))
        out.sort(key=lambda r: (-r[1], r[0]))
        return out

    def functions_present(self, name, feats):
        lib = next(l for l in self.libs if l['name'] == name)
        rows = []
        for fn in lib['functions']:
            hs = fn['shingles']
            frac = sum(1 for h in hs if h in feats) / len(hs) if hs else 0.0
            rows.append((fn['name'], frac, fn['instructions']))
        return rows

    def decide(self, feats):
        """The tier-2 rule. Returns a dict describing the decision."""
        ranked = self.scores(feats)
        above = [r for r in ranked if r[1] >= T_IDENT]
        if above:
            best = sorted(above, key=lambda r: (-r[2], r[0]))[0]
        else:
            best = ranked[0]
        others = [r for r in ranked if r[0] != best[0]]
        runner = others[0] if others else (None, 0.0, 0.0)
        return {
            'ranked': ranked,
            'best': best,
            'runner': runner,
            'margin': best[1] - runner[1],
            'identified': bool(above),
            'also': [r for r in above if r[0] != best[0]],
        }


class FlatScorer(Scorer):
    """Unweighted containment: every n-gram weighs 1. Not the shipped rule;
    kept so --self-test --sweep can show what IDF buys."""

    def __init__(self, libraries):
        Scorer.__init__(self, libraries)
        self.w = {h: 1.0 for h in self.w}
        self.den = {nm: float(len(hs)) for nm, hs in self.sig.items()}


# ---------------------------------------------------------------------------
# Identify.

def query(tc, path, workdir):
    elf = read_elf(path)
    insns = instructions(tc.objdump, elf, workdir)
    return elf, insns, query_features(insns)


def identify_text(scorer, db_label, path_label, elf, insns, feats,
                  tier1=True, verbose=False):
    """Returns (exit status, report text). Pure: same inputs, same text."""
    out = []
    out.append('identify: %s' % path_label)
    out.append('  sha256 %s  %d bytes  %d instructions'
               % (elf['sha256'], elf['size'], len(insns)))
    out.append('  db: %s (%d libraries, ngram %d, threshold %.3f)'
               % (db_label, scorer.n, NGRAM, T_IDENT))
    if tier1:
        hit = scorer.exact.get(elf['sha256'])
        if hit:
            out.append('tier 1 (exact sha256): MATCH %s -- byte-identical to '
                       "the DB's build of it" % hit)
            out.append('VERDICT: identified: %s (tier 1, exact)' % hit)
            return 0, '\n'.join(out) + '\n'
        out.append('tier 1 (exact sha256): no exact match')
    d = scorer.decide(feats)
    best, runner = d['best'], d['runner']
    out.append('tier 2 (IDF-weighted containment of each library\'s own code):')
    out.append('  best       %-22s %.4f' % (best[0], best[1]))
    out.append('  runner-up  %-22s %.4f' % (runner[0], runner[1]))
    out.append('  margin     %.4f' % d['margin'])
    for r in d['also']:
        out.append('  also >= threshold: %s %.4f -- its code is (nearly) all '
                   'here too; %s was chosen as explaining more weighted code'
                   % (r[0], r[1], best[0]))
    fns = scorer.functions_present(best[0], feats)
    out.append('  functions of %s present: %s' % (best[0], ', '.join(
        '%s %.3f/%d' % f for f in fns)))
    if verbose:
        out.append('  all scores:')
        for nm, sc, wt in d['ranked']:
            out.append('    %-22s %.4f  (matched weight %.1f)' % (nm, sc, wt))
    if d['identified']:
        out.append('VERDICT: identified: %s (tier 2, score %.4f, margin %.4f)'
                   % (best[0], best[1], d['margin']))
        return 0, '\n'.join(out) + '\n'
    out.append('VERDICT: no match (closest: %s %.4f < %.3f)'
               % (best[0], best[1], T_IDENT))
    return 1, '\n'.join(out) + '\n'


def check_provenance(db, tc):
    have = sha256_file(tc.exsc)
    if have != db['exsc_sha256']:
        raise EnvError(
            'the DB was built by exsc sha256 %s; the exsc at %s is %s. '
            'Signatures from one exsc are not valid for another (code '
            'generation and the prelude change) -- rebuild the DB with this '
            'exsc, or pass --exsc the one that built the binary.'
            % (db['exsc_sha256'], tc.exsc, have))
    ver = objdump_version(tc.objdump)
    if ver != db['objdump']:
        raise EnvError('the DB was built with objdump "%s"; this one is "%s". '
                       'The features are its disassembly text -- rebuild the DB.'
                       % (db['objdump'], ver))


def cmd_identify(argv):
    ap = argparse.ArgumentParser(prog='identify.py BINARY')
    ap.add_argument('binary')
    ap.add_argument('--db', default=DEFAULT_DB)
    ap.add_argument('--exsc', default=None, help='the exsc the binary was built '
                    'with (default build/exsc); must be the DB\'s')
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args(argv)
    db = load_db(a.db)
    tc = Toolchain.find(a.exsc, need_build=False)
    check_provenance(db, tc)
    scorer = Scorer(db['libraries'])
    work = tempfile.mkdtemp(prefix='identify.')
    try:
        elf, insns, feats = query(tc, a.binary, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    rc, text = identify_text(scorer, os.path.basename(a.db), a.binary, elf,
                             insns, feats, verbose=a.verbose)
    sys.stdout.write(text)
    return rc


def cmd_build_db(argv):
    ap = argparse.ArgumentParser(prog='identify.py build-db')
    ap.add_argument('dir')
    ap.add_argument('--out', required=True)
    ap.add_argument('--contract', default='contractus.exsc')
    ap.add_argument('--driver', default='probatio.exsc')
    ap.add_argument('--exclude', default='', help='comma-separated library names')
    ap.add_argument('--exsc', default=None)
    ap.add_argument('--jobs', type=int, default=None)
    a = ap.parse_args(argv)
    if not os.path.isdir(a.dir):
        raise EnvError('%s is not a directory' % a.dir)
    tc = Toolchain.find(a.exsc)
    work = tempfile.mkdtemp(prefix='identify-db.')
    try:
        snap = os.path.join(work, 'src')
        hashes = snapshot(a.dir, snap)
        exclude = [x for x in a.exclude.split(',') if x]
        db = build_db_from_snapshot(tc, snap, hashes, a.contract, a.driver,
                                    work, exclude, a.jobs)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for lib in db['libraries']:
        print('built    %-22s source %s  binary %s  %6d bytes  %d functions'
              % (lib['name'], lib['source_sha256'][:16], lib['binary_sha256'][:16],
                 lib['binary_size'], len(lib['functions'])))
        for fn in lib['functions']:
            if not fn['prologue']:
                print('  WARNING %s.%s does not open with push rbp/mov rbp,rsp: '
                      'its first shingles may not align in a query'
                      % (lib['name'], fn['name']))
        if lib['not_emitted']:
            print('  NOTE %s declares %s, which exsc emitted no code for; not '
                  'part of its signature' % (lib['name'], ', '.join(lib['not_emitted'])))
    for s in db['skipped']:
        print('SKIPPED  %-22s source %s  %s exit %d: %s'
              % (s['name'], s['source_sha256'][:16], s['stage'], s['exit'], s['reason']))
    if not db['libraries']:
        raise EnvError('no library in %s compiled' % a.dir)
    if len(db['libraries']) < 2:
        print('WARNING: one library -- IDF cannot weigh shared idioms; every '
              'n-gram weighs 1')
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(dump_db(db))
    print('wrote %s: %d libraries, %d skipped, exsc %s'
          % (a.out, len(db['libraries']), len(db['skipped']), db['exsc_sha256'][:16]))
    return 0


# ---------------------------------------------------------------------------
# Self-test.

# The held-out driver. Not examples/utf16/probatio.exsc and sharing no code
# with it: reads ALL of stdin (at most 4096 bytes) into one buffer, calls
# `converte` ONCE in modus_substitue, writes the units little endian. It is
# placed BEFORE the library on the command line, so the library lands at a
# different offset, after different code, and is the LAST code before the
# trap stub -- every layout fact probatio fixes is changed.
ALTER_EXSC = """\
// alter.exsc -- written by tools/identify.py --self-test: a second driver for
// the UTF-8 -> UTF-16 libraries, the held-out context the identifier never
// saw. Reads all of stdin (at most 4096 bytes), calls `converte` once in
// modus_substitue, writes the units little endian. Exit: the Exitus status;
// 9 input over 4096 bytes; 8 a short write.

functio imple(l: Lector, fons: &mutabilis acies<u8, 4096>) -> mensura poscit sicut l {
    mutabilis n: mensura = 0;
    mutabilis c: u16 = l.lege_octeto();
    dum c ne 256 {
        si n ge 4096 {
            redde 4097;
        }
        (*fons)[n] = c sicut u8;
        n = n + 1;
        c = l.lege_octeto();
    }
    redde n;
}

publica functio initium(m: Mundus) -> u8 {
    firma a = m.ambitus();
    sub ambitus = a;
    firma l = Lector.ab_introitu(a);
    firma s = Scriptor.ad_exitum(a);
    mutabilis fons: acies<u8, 4096> = [0; 4096];
    mutabilis scopus: acies<u16, 4096> = [0; 4096];
    firma n: mensura = imple(l, fons);
    si n gt 4096 {
        redde 9;
    }
    firma e: Exitus = converte(fons, n, scopus, modus_substitue);
    mutabilis k: mensura = 0;
    dum k lt e.unitates {
        firma u: u16 = scopus[k];
        si s.scribe_octeto((u atque 0xFF) sicut u8) ne 1 {
            redde 8;
        }
        si s.scribe_octeto((u deorsum 8) sicut u8) ne 1 {
            redde 8;
        }
        k = k + 1;
    }
    redde e.status;
}
"""

# A stub that satisfies the contract and converts nothing: the shared
# prelude and driver with no library to speak of. A negative control.
VACUA_EXSC = """\
// vacua.exsc -- written by tools/identify.py --self-test: converts nothing.
publica functio converte(fons: acies<u8, 4096>, n: mensura,
                         scopus: &mutabilis acies<u16, 4096>, modus: u8) -> Exitus {
    redde Exitus { unitates: 0, status: status_bene, positio: 0 };
}
"""

# Unrelated programs, built from the repository as tests/run.sh builds them.
NEGATIVES = [
    ('saluta', ['examples/saluta.exsc', 'examples/imprime.exsc',
                'examples/initium.exsc']),
    ('basis64', ['tests/programs/basis64/basis64.exsc']),
    ('lector', ['tests/programs/lector/lector.exsc']),
    ('numerus_decimalis', ['tests/programs/numerus_decimalis/numerus_decimalis.exsc']),
    ('forma', ['tests/programs/forma/forma.exsc']),
    ('streamdb', ['examples/streamdb/lector_streamdb.exsc',
                  'examples/streamdb/probatio.exsc']),
    ('hydramodem_basis', ['examples/hydramodem/quantum.exsc',
                          'examples/hydramodem/modulator.exsc',
                          'examples/hydramodem/basis.exsc']),
]

# One boundary constant per library is moved by one: the first pair below
# whose left side occurs outside a comment.
MUTATIONS = [('0x9F', '0xA0'), ('0xBF', '0xBE'), ('0xF4', '0xF3'),
             ('0xE0', '0xE1'), ('0xC2', '0xC1'), ('0xED', '0xEC'),
             ('0xF0', '0xF1'), ('0x80', '0x81'), ('0x3F', '0x1F'),
             ('0xFFFD', '0xFFFC'), ('0xD800', '0xD801')]
TOKEN_RE = re.compile(r'(?<![0-9A-Za-z_])0x[0-9A-Fa-f]+(?![0-9A-Za-z_])')

PROBE_INPUT = (b'h\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80 \xe0\x80\x80 \xed\xa0\x80 '
               b'\xf4\x90\x80\x80 \xc0\xaf \xf0\x90\x80 \x80 z')


def mutate(text):
    for old, new in MUTATIONS:
        for m in TOKEN_RE.finditer(text):
            if m.group(0).lower() != old.lower():
                continue
            line_start = text.rfind('\n', 0, m.start()) + 1
            before = text[line_start:m.start()]
            if '//' in before:
                continue
            line = text.count('\n', 0, m.start()) + 1
            return (text[:m.start()] + new + text[m.end():],
                    '%s->%s at line %d' % (m.group(0), new, line))
    return None, None


class Check:
    def __init__(self):
        self.failures = []

    def ok(self, cond, what):
        if not cond:
            self.failures.append(what)
        return cond


def fmt_matrix(names, rows):
    """rows: {query: {candidate: score}} -> lines; 2-digit percentages."""
    idx = {nm: i for i, nm in enumerate(names)}
    width = max(len('%d %s' % (i, n)) for i, n in enumerate(names))
    lines = ['  legend: ' + '  '.join('%d=%s' % (i, n) for i, n in enumerate(names))]
    lines.append('  %-*s ' % (width, 'query \\ library') +
                 ''.join('%3d' % i for i in range(len(names))))
    for q in names:
        if q not in rows:
            lines.append('  %-*s  (not built)' % (width, q))
            continue
        cells = []
        for c in names:
            v = rows[q][c]
            cell = '%3d' % min(99, int(v * 100)) if v < 1.0 else ' **'
            if c == q:
                cell = cell.replace(' ', '[', 1) if cell.startswith(' ') else cell
            cells.append(cell)
        lines.append('  %-*s ' % (width, '%d %s' % (idx[q], q)) + ''.join(cells))
    lines.append('  (cell = score x 100, floored; ** = 1.0000; [ marks the true library)')
    return lines


def self_test(argv):
    ap = argparse.ArgumentParser(prog='identify.py --self-test')
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('--src', default=os.path.join(REPO, 'examples', 'utf16'))
    ap.add_argument('--exsc', default=None)
    ap.add_argument('--jobs', type=int, default=None)
    ap.add_argument('--sweep', default='', help='comma-separated n-gram sizes: '
                    're-measure the gap at each (and flat vs idf weighting)')
    a = ap.parse_args(argv)
    try:
        a.sweep = [int(x) for x in a.sweep.split(',') if x.strip()]
    except ValueError:
        raise EnvError('--sweep takes comma-separated integers')
    if any(n < 1 for n in a.sweep):
        raise EnvError('--sweep sizes must be >= 1')
    tc = Toolchain.find(a.exsc)
    chk = Check()
    work = tempfile.mkdtemp(prefix='identify-selftest.')
    try:
        return run_self_test(a, tc, chk, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def run_self_test(a, tc, chk, work):
    jobs = a.jobs or os.cpu_count() or 2
    P = print

    # -- 1. snapshot and DB -------------------------------------------------
    P('### 1. snapshot %s and build the DB (driver probatio.exsc)'
      % os.path.relpath(a.src, REPO))
    snap = os.path.join(work, 'src')
    hashes = snapshot(a.src, snap)
    kept = {}
    db = build_db_from_snapshot(tc, snap, hashes, 'contractus.exsc',
                                'probatio.exsc', work, (), jobs, keep_bins=kept)
    bins = {nm: v[0] for nm, v in kept.items()}
    seqs = {nm: v[1] for nm, v in kept.items()}
    P('  exsc sha256 %s' % db['exsc_sha256'])
    P('  fasmg sha256 %s' % db['fasmg_sha256'])
    P('  objdump: %s' % db['objdump'])
    for fn in sorted(hashes):
        P('  source %-26s %s' % (fn, hashes[fn]))
    for lib in db['libraries']:
        P('  built   %-22s %6d bytes  %s  %d functions%s'
          % (lib['name'], lib['binary_size'], lib['binary_sha256'][:16],
             len(lib['functions']),
             ('  (no code emitted for: %s)' % ', '.join(lib['not_emitted'])
              if lib['not_emitted'] else '')))
    for s in db['skipped']:
        P('  SKIPPED %-22s %s exit %d: %s' % (s['name'], s['stage'], s['exit'],
                                               s['reason']))
    names = [lib['name'] for lib in db['libraries']]
    if not chk.ok(len(names) >= 3, 'fewer than 3 libraries compiled'):
        P('SELF-TEST: FAIL -- fewer than 3 libraries compiled')
        return 1
    for lib in db['libraries']:
        for fn in lib['functions']:
            chk.ok(fn['prologue'], '%s.%s lacks the frame prologue' % (lib['name'], fn['name']))
    text = dump_db(db)
    chk.ok(dump_db(json.loads(text)) == text, 'DB serialisation not stable')
    scorer = Scorer(db['libraries'])

    feats_cache = {}

    def feats_of(path):
        if path not in feats_cache:
            feats_cache[path] = query(tc, path, work)
        return feats_cache[path]

    # -- 2. determinism -------------------------------------------------------
    P('\n### 2. determinism')
    probe = names[0]
    other = os.path.join(work, 'elsewhere', 'deeper')
    os.makedirs(other)
    srcs2 = []
    for fn in ('contractus.exsc', probe + '.exsc', 'probatio.exsc'):
        shutil.copyfile(os.path.join(snap, fn), os.path.join(other, fn))
        srcs2.append(os.path.join(other, fn))
    b2, _ = compile_unit(tc, srcs2, os.path.join(other, probe))
    same = sha256_file(b2) == sha256_file(bins[probe])
    chk.ok(same, '%s rebuilt in another directory differs' % probe)
    P('  %s rebuilt from a copy in another directory: %s'
      % (probe, 'byte-identical' if same else 'DIFFERS'))
    e1 = identify_text(scorer, 'db', probe, *feats_of(bins[probe]), tier1=False)
    feats_cache.pop(bins[probe])
    e2 = identify_text(scorer, 'db', probe, *feats_of(bins[probe]), tier1=False)
    chk.ok(e1 == e2, 'identify output not deterministic')
    P('  identify %s twice (fresh disassembly each time): %s'
      % (probe, 'identical output' if e1 == e2 else 'OUTPUT DIFFERS'))
    P('  DB JSON: dump(load(dump(db))) == dump(db): %s'
      % ('yes' if dump_db(json.loads(text)) == text else 'NO'))

    # -- 3. tier 1 -----------------------------------------------------------
    P('\n### 3. tier 1: every DB binary by exact sha256')
    t1 = 0
    for nm in names:
        rc, out = identify_text(scorer, 'db', nm, *feats_of(bins[nm]))
        good = rc == 0 and ('VERDICT: identified: %s (tier 1' % nm) in out
        t1 += good
        chk.ok(good, 'tier 1 missed %s' % nm)
    P('  %d/%d identified exactly as themselves' % (t1, len(names)))

    # -- 4. tier 2 on the DB's own binaries (tier 1 off) ----------------------
    P('\n### 4. tier 2 on the DB binaries themselves (tier 1 disabled)')
    self_rows, self_ok = {}, 0
    for nm in names:
        d = scorer.decide(feats_of(bins[nm])[2])
        self_rows[nm] = {r[0]: r[1] for r in d['ranked']}
        good = d['identified'] and d['best'][0] == nm
        self_ok += good
        chk.ok(good, 'tier 2 (probatio) missed %s' % nm)
    P('  diagonal: %d/%d' % (self_ok, len(names)))
    for line in fmt_matrix(names, self_rows):
        P(line)

    # -- 5. held-out driver ----------------------------------------------------
    P('\n### 5. held-out driver: every library linked with a second driver '
      '(alter.exsc, written by this test; never in the DB)')
    adir = os.path.join(work, 'alter')
    os.makedirs(adir)
    with open(os.path.join(adir, 'alter.exsc'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(ALTER_EXSC)
    with open(os.path.join(adir, 'vacua.exsc'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(VACUA_EXSC)

    def build_alt(nm):
        src = os.path.join(adir, 'vacua.exsc') if nm == '<vacua>' else \
            os.path.join(snap, nm + '.exsc')
        tag = 'vacua' if nm == '<vacua>' else nm
        try:
            b, _ = compile_unit(tc, [os.path.join(snap, 'contractus.exsc'),
                                     os.path.join(adir, 'alter.exsc'), src],
                                os.path.join(adir, tag))
            return nm, b, None
        except BuildError as e:
            return nm, None, e
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        alt = {nm: (b, e) for nm, b, e in ex.map(build_alt, names + ['<vacua>'])}
    want_probe = PROBE_INPUT.decode('utf-8', 'replace').encode('utf-16-le')
    alt_rows, alt_ok, true_alt, wrong_alt, margins = {}, 0, [], [], []
    for nm in names:
        b, e = alt[nm]
        if b is None:
            P('  %-22s NOT BUILT with alter.exsc: %s' % (nm, e))
            chk.ok(False, '%s did not build with the second driver' % nm)
            continue
        r = subprocess.run([b], input=PROBE_INPUT, capture_output=True, env={})
        runs = r.returncode == 0 and r.stdout == want_probe
        d = scorer.decide(feats_of(b)[2])
        alt_rows[nm] = {x[0]: x[1] for x in d['ranked']}
        good = d['identified'] and d['best'][0] == nm
        alt_ok += good
        chk.ok(good, 'held-out driver: %s identified as %s (%.4f)'
               % (nm, d['best'][0], d['best'][1]))
        t = alt_rows[nm][nm]
        w = max((x for x in d['ranked'] if x[0] != nm), key=lambda x: x[1])
        true_alt.append((t, nm))
        wrong_alt.append((w[1], nm, w[0]))
        margins.append((t - w[1], nm, w[0]))
        P('  %-22s %s  true %.4f  best wrong %-20s %.4f  margin %.4f  '
          'runs: %s' % (nm, 'ok  ' if good else 'MISS', t, w[0], w[1], t - w[1],
                        'output = Python codec' if runs else
                        'exit %d / output differs' % r.returncode))
    P('  accuracy: %d/%d' % (alt_ok, len(names)))
    for line in fmt_matrix(names, alt_rows):
        P(line)
    if true_alt:
        P('  worst true score %.4f (%s); best wrong score %.4f (%s scored as %s); '
          'smallest margin %.4f (%s vs %s)'
          % (min(true_alt) + max(wrong_alt) + min(margins)))

    # -- 6. negatives ----------------------------------------------------------
    P('\n### 6. negative controls: unrelated programs and the stub library')
    ndir = os.path.join(work, 'neg')
    os.makedirs(ndir)
    neg_bins = []
    for tag, rel in NEGATIVES:
        paths = [os.path.join(REPO, r) for r in rel]
        if not all(os.path.isfile(p) for p in paths):
            P('  %-26s skipped: source missing' % tag)
            continue
        try:
            b, _ = compile_unit(tc, paths, os.path.join(ndir, tag))
            neg_bins.append((tag, b))
        except BuildError as e:
            P('  %-26s skipped: %s' % (tag, e))
    try:
        b, _ = compile_unit(tc, [os.path.join(snap, 'contractus.exsc'),
                                 os.path.join(adir, 'vacua.exsc'),
                                 os.path.join(snap, 'probatio.exsc')],
                            os.path.join(ndir, 'vacua_probatio'))
        neg_bins.append(('vacua+probatio', b))
    except BuildError as e:
        P('  vacua+probatio skipped: %s' % e)
    if alt['<vacua>'][0]:
        neg_bins.append(('vacua+alter', alt['<vacua>'][0]))
    neg_best = []
    for tag, b in neg_bins:
        rc, out = identify_text(scorer, 'db', tag, *feats_of(b))
        d = scorer.decide(feats_of(b)[2])
        neg_best.append((d['best'][1], tag, d['best'][0]))
        chk.ok(rc == 1, 'negative %s was identified as %s' % (tag, d['best'][0]))
        P('  %-26s %s  closest %-20s %.4f' % (tag, 'no match' if rc == 1 else
                                             'IDENTIFIED', d['best'][0], d['best'][1]))
    chk.ok(len(neg_bins) >= 3, 'fewer than 3 negative controls built')

    # -- 7. hold-one-out ---------------------------------------------------------
    P('\n### 7. hold-one-out: DB without X, identify X (both drivers); the '
      'right answer is no match')
    hoo_best = []
    for nm in names:
        sub = Scorer([l for l in db['libraries'] if l['name'] != nm])
        cells = []
        for tag, b in (('probatio', bins[nm]), ('alter', alt[nm][0])):
            if b is None:
                continue
            d = sub.decide(feats_of(b)[2])
            hoo_best.append((d['best'][1], '%s/%s' % (nm, tag), d['best'][0]))
            good = not d['identified']
            chk.ok(good, 'hold-one-out: %s (%s) confidently named %s (%.4f)'
                   % (nm, tag, d['best'][0], d['best'][1]))
            cells.append('%s: %s %-18s %.4f m%.4f' % (
                tag, 'no match' if good else 'WRONG   ', d['best'][0],
                d['best'][1], d['margin']))
        P('  %-20s %s' % (nm, ' | '.join(cells)))

    # -- 8. mutation -------------------------------------------------------------
    P('\n### 8. mutation: one boundary constant per library moved by one, '
      'rebuilt with probatio')
    mdir = os.path.join(work, 'mut')
    os.makedirs(mdir)
    plan = []
    for nm in names:
        with open(os.path.join(snap, nm + '.exsc'), encoding='utf-8') as f:
            mt, site = mutate(f.read())
        if mt is None:
            P('  %-20s no mutation site' % nm)
            continue
        with open(os.path.join(mdir, nm + '.exsc'), 'w', encoding='utf-8',
                  newline='\n') as f:
            f.write(mt)
        plan.append((nm, site))

    def build_mut(item):
        nm, site = item
        try:
            b, _ = compile_unit(tc, [os.path.join(snap, 'contractus.exsc'),
                                     os.path.join(mdir, nm + '.exsc'),
                                     os.path.join(snap, 'probatio.exsc')],
                                os.path.join(mdir, nm))
            return nm, site, b, None
        except BuildError as e:
            return nm, site, None, e
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        muts = list(ex.map(build_mut, plan))
    true_mut = []
    for nm, site, b, e in muts:
        if b is None:
            P('  %-20s %-22s not built: %s' % (nm, site, e))
            continue
        rc, out = identify_text(scorer, 'db', nm, *feats_of(b))
        d = scorer.decide(feats_of(b)[2])
        t = {x[0]: x[1] for x in d['ranked']}[nm]
        true_mut.append((t, nm, site))
        exact = 'tier 1' in out.split('VERDICT')[-1]
        good = rc == 0 and d['best'][0] == nm and not exact
        chk.ok(good, 'mutant %s (%s): %s' % (nm, site, out.strip().splitlines()[-1]))
        P('  %-20s %-22s %s  score %.4f  runner-up %-18s %.4f'
          % (nm, site, 'identified' if good else 'FAIL      ', t,
             d['runner'][0], d['runner'][1]))

    # -- 9. calibration ----------------------------------------------------------
    P('\n### 9. calibration of T_IDENT = %.3f against what this run measured' % T_IDENT)
    trues = [(s, 'held-out driver ' + n) for s, n in true_alt] + \
            [(s, 'mutant %s %s' % (n, st)) for s, n, st in true_mut]
    wrongs = [(s, 'held-out driver %s as %s' % (n, w)) for s, n, w in wrong_alt] + \
             [(s, 'hold-one-out %s as %s' % (n, w)) for s, n, w in hoo_best] + \
             [(s, 'negative %s as %s' % (n, w)) for s, n, w in neg_best]
    if trues and wrongs:
        lo_t, hi_w = min(trues), max(wrongs)
        P('  lowest true score   %.4f  (%s)' % lo_t)
        P('  highest wrong score %.4f  (%s)' % hi_w)
        gap = lo_t[0] - hi_w[0]
        P('  gap %.4f -- %s' % (gap, 'the distributions are separated and '
                                'T_IDENT lies inside the gap'
                                if hi_w[0] < T_IDENT <= lo_t[0] else
                                'T_IDENT does NOT separate them'
                                if gap > 0 else 'the distributions OVERLAP'))
        chk.ok(hi_w[0] < T_IDENT <= lo_t[0], 'T_IDENT does not separate the '
               'measured distributions')
        P('  wrong scores above 0.5: %s' % (', '.join(
            '%.4f %s' % w for w in sorted(wrongs, reverse=True) if w[0] > 0.5) or 'none'))

    # -- 10. sweep (opt-in) --------------------------------------------------------
    if a.sweep:
        P('\n### 10. sweep: n-gram size and weighting, re-measured on THIS run\'s '
          'binaries (the shipped choice is n=%d, idf)' % NGRAM)
        P('  true = held-out driver + mutants (own library); wrong = held-out '
          'off-diagonal + hold-one-out + negatives')
        P('  %4s %-5s %9s %9s %9s %9s %9s  %s'
          % ('n', 'w', 'true min', 'wrong max', 'neg max', 'gap', 'midpoint',
             'worst wrong'))
        queries_t = [(nm, alt[nm][0]) for nm in names if alt[nm][0]] + \
                    [(nm, b) for nm, _site, b, _e in muts if b]
        queries_alt = [(nm, alt[nm][0]) for nm in names if alt[nm][0]]
        for n in a.sweep:
            libs_n = [{'name': nm, 'binary_sha256': '', 'functions': [
                {'name': f, 'instructions': len(s),
                 'shingles': sorted(function_shingles(s, n))}
                for f, s in seqs[nm]]} for nm in names]
            qcache = {}

            def qf(path):
                if path not in qcache:
                    qcache[path] = query_features(feats_of(path)[1], n)
                return qcache[path]
            for wmode in ('idf', 'flat'):
                mk = Scorer if wmode == 'idf' else FlatScorer
                sc = mk(libs_n)
                tmin = min(dict((r[0], r[1]) for r in sc.scores(qf(b)))[nm]
                           for nm, b in queries_t)
                wr = []
                for nm, b in queries_alt:
                    r = [x for x in sc.scores(qf(b)) if x[0] != nm][0]
                    wr.append((r[1], 'driver %s as %s' % (nm, r[0])))
                for nm in names:
                    s2 = mk([l for l in libs_n if l['name'] != nm])
                    for b in (bins[nm], alt[nm][0]):
                        if b:
                            r = s2.scores(qf(b))[0]
                            wr.append((r[1], 'hold-one-out %s as %s' % (nm, r[0])))
                negw = max(sc.scores(qf(b))[0][1] for _t, b in neg_bins)
                wr.append((negw, 'a negative'))
                hw = max(wr)
                P('  %4d %-5s %9.4f %9.4f %9.4f %9.4f %9.4f  %s'
                  % (n, wmode, tmin, hw[0], negw, tmin - hw[0],
                     (tmin + hw[0]) / 2, hw[1]))

    P('')
    if chk.failures:
        P('SELF-TEST: FAIL -- %d check(s):' % len(chk.failures))
        for f in chk.failures:
            P('  - %s' % f)
        return 1
    P('SELF-TEST: PASS -- tier 1 %d/%d; tier 2 on the DB binaries %d/%d; '
      'held-out driver %d/%d; %d negatives and %d hold-one-out queries all '
      'no match; %d mutants identified; T_IDENT inside the measured gap'
      % (t1, len(names), self_ok, len(names), alt_ok, len(names), len(neg_bins),
         len(hoo_best), len(true_mut)))
    return 0


# ---------------------------------------------------------------------------

USAGE = ('usage: identify.py build-db DIR --out DB.json [--exsc PATH]\n'
         '       identify.py BINARY [--db DB.json] [--exsc PATH] [--verbose]\n'
         '       identify.py --self-test [--src DIR] [--sweep 2,4,8,16]\n')


def main(argv):
    try:
        if not argv or argv[0] in ('-h', '--help'):
            sys.stdout.write(USAGE)
            return 0 if argv else 2
        if '--self-test' in argv:
            return self_test(argv)
        if argv[0] == 'build-db':
            return cmd_build_db(argv[1:])
        return cmd_identify(argv)
    except EnvError as e:
        sys.stderr.write('identify: %s\n' % e)
        return 2
    except SystemExit as e:   # argparse usage errors exit 2 already
        return e.code if isinstance(e.code, int) else 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
