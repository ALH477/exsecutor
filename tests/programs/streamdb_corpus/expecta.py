#!/usr/bin/env python3
"""Build the expected certificate streams for the StreamDB v3 reader.

VERIFICATION ONLY. Nothing in the compiler's build closure runs this, and
`tests/run.sh` never calls it: the three `expected.out` files it writes are
committed, and the harness compares the reader's stdout with `cmp`. This
script is here so the expectation is re-derivable rather than asserted, the
standing `tests/conformance/entry23/expecta.py` has.

It is an INDEPENDENT reading of the format -- a second implementation, in a
different language, from `docs/design/c-backend.md` section 6 and the
reference reader -- and it cross-checks itself against
`vendor/streamdb-v3/expectation.json` (the embedded reader's own verdicts)
before writing anything. Where the manifest speaks, this script asserts; where
it does not (the older commit's suffix order, the trie node counts), this
script derives and the assertions it does make are what makes the derivation
trustworthy.

    usage: python3 tests/programs/streamdb_corpus/expecta.py        # check
           python3 tests/programs/streamdb_corpus/expecta.py --write

The stream's layout is `examples/streamdb/probatio.exsc`'s header comment.
"""
import json
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
VENDOR = os.path.join(ROOT, "vendor", "streamdb-v3")

SLOT = 128
DATA_START = 256
MAGIC = b"STDB"
VERSION = 3


# ---- the format, read independently of the Exsecutor reader ---------------

def header_parse(b, file_len):
    """One 128-byte slot, or None. Bounds in the non-wrapping form."""
    if b[0:4] != MAGIC:
        return None
    if struct.unpack_from("<I", b, 4)[0] != VERSION:
        return None
    if struct.unpack_from("<I", b, 72)[0] != zlib.crc32(b[0:72]) & 0xFFFFFFFF:
        return None
    h = {
        "seq": struct.unpack_from("<Q", b, 8)[0],
        "trie_off": struct.unpack_from("<Q", b, 16)[0],
        "trie_len": struct.unpack_from("<Q", b, 24)[0],
        "trie_crc": struct.unpack_from("<I", b, 32)[0],
        "index_off": struct.unpack_from("<Q", b, 40)[0],
        "index_len": struct.unpack_from("<Q", b, 48)[0],
        "index_crc": struct.unpack_from("<I", b, 56)[0],
        "data_end": struct.unpack_from("<Q", b, 64)[0],
    }
    for off, ln in (("trie_off", "trie_len"), ("index_off", "index_len")):
        if h[ln] > file_len or h[off] > file_len - h[ln]:
            return None
    if h["data_end"] > file_len or h["index_len"] < 8:
        return None
    return h


def commit(d):
    """The winning slot's offset and header, or (None, None)."""
    if len(d) < DATA_START:
        return None, None
    best = None
    best_off = None
    for i in (0, 1):
        h = header_parse(d[i * SLOT:(i + 1) * SLOT], len(d))
        if h is None:
            continue
        if best is None or h["seq"] > best["seq"]:
            best, best_off = h, i * SLOT
    return best_off, best


def index_parse(d, h):
    n = struct.unpack_from("<Q", d, h["index_off"])[0]
    assert 8 + n * 32 <= h["index_len"]
    out = []
    for i in range(n):
        e = h["index_off"] + 8 + i * 32
        out.append({
            "id": d[e:e + 16],
            "offset": struct.unpack_from("<Q", d, e + 16)[0],
            "size": struct.unpack_from("<I", d, e + 24)[0],
            "crc": struct.unpack_from("<I", d, e + 28)[0],
        })
    return out


class Trie:
    """The flattened trie: siblings adjacent, exactly as the reader builds it."""

    def __init__(self):
        self.first = []
        self.count = []
        self.value = []
        self.keys = []      # one per slot, the key byte its parent reached it by
        self.n = 0


def trie_flatten(blob):
    """One pass, explicit bump allocator, the reference's shape."""
    t = Trie()
    cap = 4096
    t.first = [0] * cap
    t.count = [0] * cap
    t.value = [None] * cap
    t.keys = [0] * cap
    bump = [1]
    pos = [0]

    def enter(slot, depth):
        assert depth <= 1024
        nch = struct.unpack_from("<Q", blob, pos[0])[0]
        pos[0] += 8
        assert nch <= 256
        first = bump[0]
        bump[0] += nch
        assert bump[0] <= cap
        t.first[slot] = first
        t.count[slot] = nch
        vals = 0
        for i in range(nch):
            t.keys[first + i] = blob[pos[0]]
            pos[0] += 1
            vals += enter(first + i, depth + 1)
        tag = blob[pos[0]]
        pos[0] += 1
        if tag == 1:
            ln = struct.unpack_from("<Q", blob, pos[0])[0]
            pos[0] += 8
            assert ln == 16
            t.value[slot] = blob[pos[0]:pos[0] + 16]
            pos[0] += 16
            vals += 1
        else:
            assert tag == 0
        subtree = struct.unpack_from("<Q", blob, pos[0])[0]
        pos[0] += 8
        # The reader verifies this; the reference discards it.
        assert subtree == vals, (slot, subtree, vals)
        return vals

    enter(0, 0)
    t.n = bump[0]
    return t


def descend(t, node, key):
    lo, hi = 0, t.count[node]
    base = t.first[node]
    while lo < hi:
        mid = lo + ((hi - lo) >> 1)
        k = t.keys[base + mid]
        if k == key:
            return base + mid
        if k < key:
            lo = mid + 1
        else:
            hi = mid
    return None


def walk(t, key):
    """The REVERSED trie: last byte first."""
    node = 0
    for c in reversed(key):
        node = descend(t, node, c)
        if node is None:
            return None
    return node


def index_find(entries, uid):
    lo, hi = 0, len(entries)
    while lo < hi:
        mid = lo + ((hi - lo) >> 1)
        if entries[mid]["id"] == uid:
            return mid
        if entries[mid]["id"] < uid:
            lo = mid + 1
        else:
            hi = mid
    return None


def collect(t, node):
    """Preorder, a node's own value before its children, children ascending."""
    out = []
    stack = [node]
    while stack:
        nd = stack.pop()
        if t.value[nd] is not None:
            out.append(nd)
        k, base = t.count[nd], t.first[nd]
        for j in range(k):
            stack.append(base + (k - 1 - j))
    return out


# ---- the stream -----------------------------------------------------------

def u32(v):
    return struct.pack(">I", v)


def stream(path, keys, suffixes):
    d = open(path, "rb").read()
    off, h = commit(d)
    if off is None:
        return None, 1
    if zlib.crc32(d[h["index_off"]:h["index_off"] + h["index_len"]]) & 0xFFFFFFFF \
            != h["index_crc"]:
        return None, 2
    if zlib.crc32(d[h["trie_off"]:h["trie_off"] + h["trie_len"]]) & 0xFFFFFFFF \
            != h["trie_crc"]:
        return None, 3
    entries = index_parse(d, h)
    t = trie_flatten(d[h["trie_off"]:h["trie_off"] + h["trie_len"]])

    out = bytearray()
    out.append(0)
    out += u32(h["seq"] & 0xFFFFFFFF)
    out += u32(len(entries))
    out += u32(t.n)
    out += u32(off)

    for k in keys:
        node = walk(t, k.encode())
        pos = None
        if node is not None and t.value[node] is not None:
            pos = index_find(entries, t.value[node])
        if pos is None:
            out.append(1)
            out += u32(0)
            continue
        e = entries[pos]
        # the record header lives at offset - 8, and is checked against the
        # index entry -- the documented trap
        rs, rc = struct.unpack_from("<II", d, e["offset"] - 8)
        if e["offset"] < 8 or e["size"] > len(d) \
                or e["offset"] > len(d) - e["size"] \
                or rs != e["size"] or rc != e["crc"]:
            out.append(3)
            out += u32(0)
            continue
        payload = d[e["offset"]:e["offset"] + e["size"]]
        if zlib.crc32(payload) & 0xFFFFFFFF != e["crc"]:
            out.append(2)
            out += u32(0)
            continue
        out.append(0)
        out += u32(e["size"])
        out += payload

    for sfx in suffixes:
        node = walk(t, sfx.encode())
        hits = []
        if node is not None:
            for nd in collect(t, node):
                pos = index_find(entries, t.value[nd])
                if pos is not None:
                    hits.append(entries[pos])
        out.append(len(hits))
        for e in hits:
            out += u32(e["size"])
            out += u32(e["crc"])
    return bytes(out), 0


# ---- cross-checks against the vendored manifest ---------------------------

def main():
    man = json.load(open(os.path.join(VENDOR, "expectation.json")))
    keys = [doc["key"] for doc in man["documents"]]
    suffixes = [".t3dm", ".bin"]
    assert len(keys) == man["document_count"] == 24

    here = os.path.dirname(os.path.abspath(__file__))
    write = "--write" in sys.argv
    fail = 0

    # ---- the good container ----
    good, rc = stream(os.path.join(VENDOR, "corpus.streamdb"), keys, suffixes)
    assert rc == 0, rc
    # every payload byte, against the frozen payloads and the manifest
    p = 17
    for doc in man["documents"]:
        assert good[p] == 0, doc["key"]
        size = struct.unpack_from(">I", good, p + 1)[0]
        assert size == doc["size"], (doc["key"], size)
        body = good[p + 5:p + 5 + size]
        frozen = open(os.path.join(VENDOR, doc["payload_file"]), "rb").read()
        assert body == frozen, doc["key"]
        assert zlib.crc32(body) & 0xFFFFFFFF == int(doc["crc32"], 16)
        p += 5 + size
    for sfx in suffixes:
        want = man["suffix_search"][sfx]
        assert good[p] == len(want), sfx
        p += 1
        for row in want:
            size, crc = struct.unpack_from(">II", good, p)
            assert size == row["size"] and crc == int(row["crc32"], 16), (sfx, row)
            p += 8
    assert p == len(good)
    assert struct.unpack_from(">I", good, 5)[0] == 24      # document count
    assert struct.unpack_from(">I", good, 13)[0] == 128    # slot 1 is newest

    # ---- the payload flip: one CRC mismatch, 23 intact ----
    neg = man["negative_cases"]
    flip, rc = stream(os.path.join(VENDOR, "negative", "corpus.payload-flip.streamdb"),
                      keys, suffixes)
    assert rc == 0
    row = neg["negative/corpus.payload-flip.streamdb"]["reference_reader_result"]
    bad = [i for i, k in enumerate(keys) if k == row["affected_document"]]
    p, nok, nbad = 17, 0, 0
    for i, doc in enumerate(man["documents"]):
        v = flip[p]
        size = struct.unpack_from(">I", flip, p + 1)[0]
        if v == 0:
            nok += 1
        else:
            nbad += 1
            assert v == 2 and i in bad, (doc["key"], v)
        p += 5 + size
    assert (nok, nbad) == (row["n_documents_ok"], row["n_documents_failed"])

    # ---- the header flip: the older commit, 20 documents, 4 absent ----
    hf, rc = stream(os.path.join(VENDOR, "negative", "corpus.header-flip.streamdb"),
                    keys, suffixes)
    assert rc == 0
    row = neg["negative/corpus.header-flip.streamdb"]["reference_reader_result"]
    assert struct.unpack_from(">I", hf, 5)[0] == row["n_documents_ok"] == 20
    assert struct.unpack_from(">I", hf, 9)[0] == 316   # the transcript's node count
    assert struct.unpack_from(">I", hf, 13)[0] == 0    # fell back to slot 0
    assert struct.unpack_from(">I", hf, 1)[0] == 2     # seq=2, the older commit
    p, nfound, nmissing = 17, 0, 0
    for doc in man["documents"]:
        v = hf[p]
        size = struct.unpack_from(">I", hf, p + 1)[0]
        if v == 0:
            nfound += 1
        else:
            assert v == 1, doc["key"]
            nmissing += 1
        p += 5 + size
    assert (nfound, nmissing) == (20, row["n_documents_not_found"] + 0), (nfound, nmissing)
    assert hf[p] == 4, "the transcript's '.t3dm' -> 4 match(es)"

    # ---- the truncation: no valid commit ----
    trunc, rc = stream(os.path.join(VENDOR, "negative", "corpus.trunc300.streamdb"),
                       keys, suffixes)
    assert trunc is None and rc == 1

    out = (("streamdb_corpus", good),
           ("streamdb_onus", flip),
           ("streamdb_caput", hf))
    for name, blob in out:
        path = os.path.join(os.path.dirname(here), name, "expected.out")
        if write:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(blob)
            print("wrote %s (%d bytes)" % (path, len(blob)))
        else:
            have = open(path, "rb").read() if os.path.exists(path) else None
            ok = have == blob
            fail += 0 if ok else 1
            print("%-46s %s (%d bytes)" % (path, "OK" if ok else "DIFFERS",
                                           len(blob)))
    print("streamdb_truncus: no output, exit 1")
    return fail


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
