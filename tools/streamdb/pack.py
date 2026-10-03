#!/usr/bin/env python3
# tools/streamdb/pack.py
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
# Packs datasets/finetune/ and its provenance into ONE StreamDB v3 container
# (github.com/ALH477/DeMoD-StreamDB), keyed by path:
#
#   /exsecutor/finetune/{all,train,validation}.jsonl, lexicon.json, manifest.json, README.md
#   /exsecutor/finetune/src/{keywords,qa}.json
#   /exsecutor/finetune/src/cases/<id>.exsc        (suffix search ".exsc" lists them)
#   /exsecutor/tools/gen-finetune.py               (how the records were made)
#   /exsecutor/provenance/index.json               (every key's sha256 and size,
#                                                   the exsecutor git commit, and
#                                                   the sha256 of the spec the
#                                                   dataset was generated from)
#
# VERIFICATION-ONLY tooling, never on the build path (spec §18.1). It links
# StreamDB's C library through ctypes (LGPL-3.0, a separate project) and does
# not vendor it: pass --lib PATH to a libstreamdb.so built from the StreamDB
# repo (`make` in its C/ directory).
#
# EVIDENCE DISCIPLINE. After writing, the container is CLOSED and REOPENED,
# every key is read back and its sha256 compared with the source file, and the
# suffix search for ".exsc" must return exactly as many keys as there are case
# files. The script exits non-zero and names the key if anything differs. That
# is what it claims: a faithful container of these bytes. It does not claim the
# records are correct (datasets/finetune/README.md says what is evidenced), and
# it does not claim the container is byte-reproducible: StreamDB assigns each
# document a UUID, so two packs of the same inputs may differ as files while
# holding identical documents. `--check` compares documents, not file bytes.
#
# Determinism of what IS under our control: keys are inserted in sorted order,
# the index is canonical JSON, and no clock, hostname or path is recorded.
#
# Usage:
#   make -C /path/to/DeMoD-StreamDB/C            # -> lib/libstreamdb.so
#   python3 tools/streamdb/pack.py --lib /path/to/libstreamdb.so --out exsecutor-finetune.streamdb
#   python3 tools/streamdb/pack.py --lib ... --check exsecutor-finetune.streamdb
"""Pack datasets/finetune/ into a StreamDB v3 container, verified by read-back."""

import argparse
import ctypes
import hashlib
import json
import os
import subprocess
import sys
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "datasets", "finetune")
PREFIX = "/exsecutor"
STREAMDB_OK = 0


def die(msg):
    sys.stderr.write("pack: " + msg + "\n")
    sys.exit(1)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def collect():
    """[(key, path)] in sorted key order. Only files that exist; nothing generated here."""
    items = []
    for f in ("all.jsonl", "train.jsonl", "validation.jsonl", "lexicon.json", "manifest.json", "README.md"):
        items.append(("%s/finetune/%s" % (PREFIX, f), os.path.join(DATA, f)))
    for f in ("keywords.json", "qa.json"):
        items.append(("%s/finetune/src/%s" % (PREFIX, f), os.path.join(DATA, "src", f)))
    cases = os.path.join(DATA, "src", "cases")
    for n in sorted(os.listdir(cases)):
        if n.endswith(".exsc"):
            items.append(("%s/finetune/src/cases/%s" % (PREFIX, n), os.path.join(cases, n)))
    items.append((PREFIX + "/tools/gen-finetune.py", os.path.join(ROOT, "tools", "gen-finetune.py")))
    for k, p in items:
        if not os.path.isfile(p):
            die("missing input %s" % p)
        if os.path.getsize(p) == 0:
            die("empty input %s (StreamDB refuses a zero-size value)" % p)
    return sorted(items)


def git(*args):
    r = subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def provenance(items):
    spec = os.path.join(ROOT, "docs", "spec", "exsecutor-spec-v0.4.md")
    paths = [os.path.relpath(p, ROOT) for _, p in items]
    dirty = git("status", "--porcelain", "--", *paths)
    idx = {
        "schema": "exsecutor-finetune-streamdb/1",
        "what": "datasets/finetune of github.com/ALH477/exsecutor, packed by tools/streamdb/pack.py",
        "exsecutor_commit": git("rev-parse", "HEAD"),
        "packed_files_modified_since_commit": bool(dirty),
        "spec_sha256": sha(open(spec, "rb").read()),
        "dataset_manifest": "/exsecutor/finetune/manifest.json",
        "licence_note": "Repository is GPL-3.0-or-later with the output exception in LICENSE.EXCEPTION; "
                        "see /exsecutor/finetune/README.md. The container format and library are StreamDB's (LGPL-3.0).",
        "not_included": ["eval results", "training runs", "agent-loop transcripts (none exist yet)"],
        "files": [],
    }
    for k, p in items:
        b = open(p, "rb").read()
        idx["files"].append({"key": k, "sha256": sha(b), "bytes": len(b)})
    return json.dumps(idx, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def load(lib):
    L = ctypes.CDLL(lib)
    c_sz = ctypes.c_size_t
    L.streamdb_init.restype = ctypes.c_void_p
    L.streamdb_init.argtypes = [ctypes.c_char_p, ctypes.c_int]
    L.streamdb_insert.argtypes = [ctypes.c_void_p, ctypes.c_char_p, c_sz, ctypes.c_char_p, c_sz]
    L.streamdb_get.restype = ctypes.c_void_p
    L.streamdb_get.argtypes = [ctypes.c_void_p, ctypes.c_char_p, c_sz, ctypes.POINTER(c_sz)]
    L.streamdb_flush.argtypes = [ctypes.c_void_p]
    L.streamdb_free.argtypes = [ctypes.c_void_p]

    class Result(ctypes.Structure):
        pass
    Result._fields_ = [("key", ctypes.POINTER(ctypes.c_ubyte)), ("key_len", c_sz),
                       ("value", ctypes.c_void_p), ("value_size", c_sz),
                       ("next", ctypes.POINTER(Result))]
    L.streamdb_suffix_search.restype = ctypes.POINTER(Result)
    L.streamdb_suffix_search.argtypes = [ctypes.c_void_p, ctypes.c_char_p, c_sz]
    L.streamdb_free_results.argtypes = [ctypes.POINTER(Result)]
    libc = ctypes.CDLL(None)
    libc.free.argtypes = [ctypes.c_void_p]
    return L, libc


def get(L, libc, db, key):
    kb = key.encode("utf-8")
    n = ctypes.c_size_t()
    p = L.streamdb_get(db, kb, len(kb), ctypes.byref(n))
    if not p:
        return None
    data = ctypes.string_at(p, n.value)
    libc.free(p)
    return data


def suffix_keys(L, db, suffix):
    sb = suffix.encode("utf-8")
    head = L.streamdb_suffix_search(db, sb, len(sb))
    keys, node = [], head
    while node:
        r = node.contents
        keys.append(ctypes.string_at(r.key, r.key_len).decode("utf-8"))
        node = r.next
    if head:
        L.streamdb_free_results(head)
    return sorted(keys)


def verify(L, libc, path, items, index_bytes):
    """Reopen `path` and compare every document with its source. Returns problems."""
    db = L.streamdb_init(path.encode(), 0)
    if not db:
        return ["could not reopen %s" % path]
    bad = []
    try:
        for k, p in items:
            got = get(L, libc, db, k)
            if got is None or sha(got) != sha(open(p, "rb").read()):
                bad.append(k)
        got = get(L, libc, db, PREFIX + "/provenance/index.json")
        if got is None or got != index_bytes:
            bad.append(PREFIX + "/provenance/index.json")
        want = sorted(k for k, _ in items if k.endswith(".exsc"))
        have = suffix_keys(L, db, ".exsc")
        if have != want:
            bad.append("suffix search '.exsc': %d keys, expected %d" % (len(have), len(want)))
    finally:
        L.streamdb_free(db)
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--lib", required=True, help="path to libstreamdb.so built from the StreamDB repo's C/ directory")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--out", help="write a NEW container here (refuses to overwrite)")
    g.add_argument("--check", metavar="FILE", help="verify an existing container against the working tree")
    args = ap.parse_args()

    items = collect()
    for k, _ in items:
        if unicodedata.normalize("NFC", k) != k:
            die("key not NFC: " + k)
    index = provenance(items)
    index_bytes = index.encode("utf-8")
    L, libc = load(args.lib)

    if args.check:
        bad = verify(L, libc, args.check, items, index_bytes)
        if bad:
            die("%s differs from the working tree: %s" % (args.check, ", ".join(bad[:8])))
        print("pack: --check: %d documents identical to the working tree, suffix search '.exsc' agrees" % (len(items) + 1))
        return

    if os.path.exists(args.out):
        die("%s exists; refusing to overwrite" % args.out)
    db = L.streamdb_init(args.out.encode(), 0)
    if not db:
        die("streamdb_init failed")
    try:
        for k, p in items + [(PREFIX + "/provenance/index.json", None)]:
            b = index_bytes if p is None else open(p, "rb").read()
            kb = k.encode("utf-8")
            st = L.streamdb_insert(db, kb, len(kb), b, len(b))
            if st != STREAMDB_OK:
                die("insert %s failed with status %d" % (k, st))
        st = L.streamdb_flush(db)
        if st != STREAMDB_OK:
            die("flush failed with status %d" % st)
    finally:
        L.streamdb_free(db)
    bad = verify(L, libc, args.out, items, index_bytes)
    if bad:
        os.remove(args.out)
        die("read-back failed, container removed: %s" % ", ".join(bad[:8]))
    print("pack: wrote %s: %d documents, %d bytes; reopened, every document read back and matched, "
          "suffix search '.exsc' agrees" % (args.out, len(items) + 1, os.path.getsize(args.out)))


if __name__ == "__main__":
    main()
