#!/usr/bin/env python3
# examples/dcfs_gate/proba.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
#
# The oracle for dcfs_gate.exsc, driving hosts built from proba.c.
#
#   proba.py [--cases N] [--seed S] HOST [HOST...]
#
# The oracle is a third statement of the DCFS frame grammar, in a style that
# shares nothing with the gate: recursive descent over Python bytes, zlib for
# the CRC, and Python's own strict UTF-8 codec for "valid UTF-8" (it refuses
# overlong forms, surrogates and anything above U+10FFFF). It returns the EXACT
# verdict, not just admit/refuse, following the table at the top of
# dcfs_gate.exsc: the first failing check in scan order.
#
# Cases (deterministic for a seed):
#   * every header byte perturbed, every flags value, every major version,
#     payload_len and n boundaries including n = 2^63 and 2^64-1 (caput);
#   * random structured payloads of every value type, in valid frames, then
#     byte-mutated, with the length and CRC repaired or not (an unrepaired CRC
#     would stop every case at the CRC check and never reach the grammar);
#   * structured families: nesting sweeps, count and length boundaries,
#     UTF-8 tables, varint shapes, struct end-marker lookalikes;
#   * lying lengths: a valid frame passed with every n from 0 to len+30, around
#     the 65557 capacity, and 2^32, 2^63, 2^64-1;
#   * the same cases again with the buffer padded by 0xA5 instead of 0 ("corpusg"):
#     the verdict must not depend on bytes the frame does not own.
# A trap aborts the host, so every case answering means nothing trapped.
import random
import subprocess
import sys
import zlib

CAP = {"caput": 17, "corpus": 65557, "corpusg": 65557}
MAGIC = b"DCFS"
FIXED = {0x01: 1, 0x02: 1, 0x03: 1, 0x04: 2, 0x05: 2, 0x06: 4, 0x07: 4, 0x0A: 4,
         0x08: 8, 0x09: 8, 0x0B: 8, 0x30: 8, 0x31: 8, 0x13: 16}


# ------------------------------------------------------------------ the oracle
class Refuse(Exception):
    def __init__(self, code):
        self.code = code


def be32(b):
    return int.from_bytes(b, "big")


def oracle_caput(buf, n):
    """buf: the 17 bytes the host passes. n: the frame's total length."""
    if n < 17:
        return 1
    if buf[0:4] != MAGIC:
        return 2
    if buf[4] != 5:
        return 3
    if buf[8] & ~0x1C & 0xFF:
        return 4
    plen = be32(buf[9:13])
    if plen > 16 * 1024 * 1024:
        return 5
    if n != 17 + plen + 4:
        return 6
    return 0


def value(p, pos, depth):
    """Parse one value of payload p at pos; depth = containers currently open."""
    end = len(p)
    if pos >= end:
        raise Refuse(10)
    tag = p[pos]
    pos += 1
    if tag == 0x00:
        return pos
    if tag in FIXED:
        if end - pos < FIXED[tag]:
            raise Refuse(10)
        return pos + FIXED[tag]
    if tag == 0x10:
        count = 0
        while True:
            if pos >= end:
                raise Refuse(10)
            b = p[pos]
            pos += 1
            count += 1
            if count == 10 and b > 1:
                raise Refuse(15)
            if not b & 0x80:
                break
        if count >= 2 and b == 0:
            raise Refuse(15)
        return pos
    if tag in (0x11, 0x12):
        if end - pos < 4:
            raise Refuse(10)
        ln = be32(p[pos:pos + 4])
        pos += 4
        if tag == 0x11 and ln > 65536:
            raise Refuse(13)
        if ln > end - pos:
            raise Refuse(10)
        if tag == 0x11:
            try:
                bytes(p[pos:pos + ln]).decode("utf-8")
            except UnicodeDecodeError:
                raise Refuse(14)
        return pos + ln
    if tag == 0x20:
        if end - pos < 5:
            raise Refuse(10)
        count = be32(p[pos + 1:pos + 5])
        pos += 5
        if count > 1048576 or count > end - pos:
            raise Refuse(12)
        if depth >= 32:
            raise Refuse(11)
        for _ in range(count):
            pos = value(p, pos, depth + 1)
        return pos
    if tag == 0x21:
        if end - pos < 6:
            raise Refuse(10)
        count = be32(p[pos + 2:pos + 6])
        pos += 6
        if count > 1048576 or 2 * count > end - pos:
            raise Refuse(12)
        if depth >= 32:
            raise Refuse(11)
        for _ in range(2 * count):
            pos = value(p, pos, depth + 1)
        return pos
    if tag == 0x22:
        if end - pos < 2:
            raise Refuse(10)
        pos += 2
        if depth >= 32:
            raise Refuse(11)
        while True:
            if end - pos < 3:
                raise Refuse(10)
            fid = (p[pos] << 8) | p[pos + 1]
            fty = p[pos + 2]
            pos += 3
            if fid == 0 and fty == 0:
                return pos
            pos = value(p, pos, depth + 1)
    raise Refuse(9)


def oracle_corpus(buf, n):
    """buf: the 65557 bytes the host passes."""
    v = oracle_caput(buf[:17], n)
    if v:
        return v
    if n > 65557:
        return 7
    plen = n - 21
    if zlib.crc32(buf[:17 + plen]) != be32(buf[17 + plen:21 + plen]):
        return 8
    payload = buf[17:17 + plen]
    pos = 0
    try:
        while pos < len(payload):
            pos = value(payload, pos, 0)
    except Refuse as r:
        return r.code
    return 0


def oracle(fn, data, n, pad=0):
    cap = CAP[fn]
    buf = bytearray([pad]) * cap
    k = min(len(data), cap)
    buf[:k] = data[:k]
    buf = bytes(buf)
    return oracle_caput(buf, n) if fn == "caput" else oracle_corpus(buf, n)


# ------------------------------------------------------------------ the corpus
rng = random.Random(0x44434653)
cases = []          # (fn, data, n)


def add(fn, data, n=None):
    cases.append((fn, bytes(data), len(data) if n is None else n))


def u16(v): return v.to_bytes(2, "big")
def u32(v): return v.to_bytes(4, "big")


def frame(payload, flags=0, version=0x0520, plen=None, crc=True, msg=None, seq=None):
    body = (MAGIC + u16(version) + u16(rng.randrange(65536) if msg is None else msg) + bytes([flags])
            + u32(len(payload) if plen is None else plen) + u32(rng.randrange(2**32) if seq is None else seq)
            + bytes(payload))
    return body + u32(zlib.crc32(body)) if crc else body


def repair(f, fix_len=True, fix_crc=True):
    f = bytearray(f)
    if len(f) >= 21:
        if fix_len:
            f[9:13] = u32(len(f) - 21)
        if fix_crc:
            f[-4:] = u32(zlib.crc32(bytes(f[:-4])))
    return bytes(f)


SCALARS = [0, 1, 0x7F, 0x80, 0x7FF, 0x800, 0xD7FF, 0xE000, 0xFFFF, 0x10000, 0x10FFFF]


def utf8_of(cp):
    return chr(cp).encode("utf-8", "surrogatepass")


def varint(v, extra=0):
    out = bytearray()
    while True:
        byte = v & 0x7F
        v >>= 7
        out.append(byte | (0x80 if v else 0))
        if not v:
            break
    for _ in range(extra):
        out[-1] |= 0x80
        out.append(0)
    return bytes(out)


def gen_value(depth=0, budget=6):
    k = rng.randrange(100 if budget > 0 and depth < 36 else 55)
    if k < 35:
        tag = rng.choice([0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0A, 0x08, 0x09, 0x0B, 0x30, 0x31, 0x13])
        size = 0 if tag == 0 else FIXED[tag]
        return bytes([tag]) + rng.randbytes(size)
    if k < 45:
        v = rng.getrandbits(64) >> rng.randrange(64)
        return b"\x10" + varint(v, rng.choice([1, 2]) if rng.randrange(10) == 0 else 0)
    if k < 52:
        s = bytearray()
        for _ in range(rng.randrange(8)):
            r = rng.randrange(12)
            if r < 2:
                s += utf8_of(rng.choice(SCALARS))
            elif r < 5:
                s.append(0x80 + rng.randrange(0x80))
            else:
                s += utf8_of(0x20 + rng.randrange(0x5F))
        return b"\x11" + u32(len(s)) + bytes(s)
    if k < 55:
        n = rng.randrange(12)
        return b"\x12" + u32(n) + rng.randbytes(n)
    if k < 70:
        n = rng.randrange(5)
        return b"\x20" + bytes([rng.randrange(256)]) + u32(n) + b"".join(gen_value(depth + 1, budget - 1) for _ in range(n))
    if k < 80:
        n = rng.randrange(4)
        return (b"\x21" + bytes([rng.randrange(256), rng.randrange(256)]) + u32(n)
                + b"".join(gen_value(depth + 1, budget - 1) for _ in range(2 * n)))
    if k < 95:
        out = b"\x22" + u16(rng.randrange(65536))
        for _ in range(rng.randrange(5)):
            out += u16(1 + rng.randrange(65535)) + bytes([rng.randrange(256)]) + gen_value(depth + 1, budget - 1)
        return out + b"\x00\x00\x00"
    return bytes([rng.choice([0x23, 0x32, 0x33, 0xFE, 0xFF, 0x0C, 0x14, 0x24, 0x40, 0x7F])])


def gen_payload():
    return b"".join(gen_value() for _ in range(rng.randrange(9)))


INTERESTING8 = [0x00, 0x01, 0x02, 0x7F, 0x80, 0x81, 0xBF, 0xC0, 0xC2, 0xDF, 0xE0, 0xED, 0xEF, 0xF0, 0xF4, 0xF5, 0xFE, 0xFF]
INTERESTING32 = [0, 1, 2, 3, 4, 5, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0xFFFF0, 0x100000, 0x100001, 0x7FFFFFFF,
                 0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF, 0x1000000, 0x1000001, 65536, 65537]


def mutate(f):
    f = bytearray(f)
    for _ in range(1 + rng.randrange(3)):
        if not f:
            f.append(rng.randrange(256))
            continue
        pos = rng.randrange(len(f))
        op = rng.randrange(10)
        if op == 0:
            f[pos] ^= 1 << rng.randrange(8)
        elif op == 1:
            f[pos] = rng.randrange(256)
        elif op == 2:
            f[pos] = rng.choice(INTERESTING8)
        elif op == 3:
            f.insert(pos, rng.choice(INTERESTING8) if rng.randrange(2) else rng.randrange(256))
        elif op == 4:
            del f[pos]
        elif op == 5 and len(f) >= 4:
            pos = rng.randrange(len(f) - 3)
            f[pos:pos + 4] = u32(rng.choice(INTERESTING32))
        elif op == 6:
            ln = 1 + rng.randrange(min(16, len(f) - pos))
            f[pos:pos] = f[pos:pos + ln]
        elif op == 7:
            del f[pos:]
        elif op == 8:
            f += rng.randbytes(1 + rng.randrange(6))
        else:
            o = rng.randrange(len(f))
            f[pos], f[o] = f[o], f[pos]
    return bytes(f)


FLAGS = [0x00, 0x00, 0x00, 0x04, 0x08, 0x10, 0x1C, 0x0C, 0x14, 0x18]


def family_headers():
    pl = b"\x02\x09\x11" + u32(2) + b"hi"
    good = frame(pl, seq=7, msg=1)
    for flags in range(256):
        add("corpus", frame(pl, flags=flags))
        add("corpus", frame(pl, flags=flags, crc=False))
        add("caput", frame(pl, flags=flags)[:17], 21 + len(pl))
    for major in range(256):
        add("corpus", frame(pl, version=(major << 8) | 0x20))
        add("caput", frame(pl, version=(major << 8) | rng.randrange(256))[:17], 21 + len(pl))
    for i in range(17):
        for d in (1, -1, 0x40, -0x40, 0x80, 0xFF):
            f = bytearray(good)
            f[i] = (f[i] + d) & 0xFF
            add("corpus", bytes(f))
            add("corpus", repair(f, False, True))
            add("caput", bytes(f[:17]), len(good))
    for plen in (0, 1, len(pl), 65535, 65536, 65537, 0xFFFFFF, 0x1000000, 0x1000001, 0x7FFFFFFF, 0x80000000,
                 0xFFFFFFFF, 0xFFFFFFFB, 0xFFFFFFEB):
        for n in (plen + 20, plen + 21, plen + 22, 0, 21, 17, 65557, 65558):
            add("caput", frame(pl, plen=plen)[:17], n)
        for n in (plen + 21, plen + 22):
            add("corpus", frame(pl, plen=plen), n)
    for n in list(range(0, 40)) + [2**31, 2**32 - 1, 2**32, 2**32 + 21, 2**63 - 1, 2**63, 2**64 - 22, 2**64 - 21,
                                    2**64 - 2, 2**64 - 1]:
        add("caput", good[:17], n)
        add("caput", bytes(rng.randrange(256) for _ in range(17)), n)
        add("corpus", good, n)
        add("corpus", bytes(rng.randrange(256) for _ in range(40)), n)
    # a frame that claims a payload too big for the whole-frame gate
    for plen in (65537, 65538, 70000, 1 << 20, 16 * 1024 * 1024):
        add("corpus", frame(b"\x00" * 64, plen=plen)[:17] + b"\x00" * 64, plen + 21)


def build_nest(kind, depth):
    out = bytearray()
    for i in range(depth):
        inner = i + 1 == depth
        if kind == "array":
            out += b"\x20\x20" + u32(0 if inner else 1)
        elif kind == "map":
            out += b"\x21\x21\x00" + u32(0 if inner else 1)
        else:
            out += b"\x22" + u16(1) + (b"" if inner else u16(1) + b"\x22")
    if kind == "map":
        out += b"\x00" * max(depth - 1, 0)
    if kind == "struct":
        out += b"\x00\x00\x00" * depth
    return bytes(out)


def family_structured():
    for kind in ("array", "map", "struct"):
        for d in range(0, 41):
            add("corpus", frame(build_nest(kind, d)))
            add("corpus", frame(build_nest(kind, d) * 2))
    for count in (0, 1, 2, 3, 9, 10, 11, 12, 100, 1048575, 1048576, 1048577, 0x7FFFFFFF, 0x80000000, 0x80000001,
                  0xFFFFFFFF, 0x40000000):
        for fill in range(0, 25):
            add("corpus", frame(b"\x20\x00" + u32(count) + b"\x00" * fill))
            add("corpus", frame(b"\x21\x00\x00" + u32(count) + b"\x00" * fill))
    for n in range(40):
        for extra in (-1, 0, 1):
            add("corpus", frame(b"\x20\x06" + u32(max(n + extra, 0)) + b"".join(b"\x06" + u32(k) for k in range(n))))
    bad = [b"\x80", b"\xbf", b"\xc0\x80", b"\xc1\xbf", b"\xc2", b"\xc2\x7f", b"\xc2\xc0", b"\xdf\xbf", b"\xe0\x9f\xbf",
           b"\xe0\xa0\x80", b"\xe0\x80\x80", b"\xed\xa0\x80", b"\xed\x9f\xbf", b"\xee\x80\x80", b"\xe2\x82",
           b"\xe2\x82\x41", b"\xf0\x8f\xbf\xbf", b"\xf0\x90\x80\x80", b"\xf4\x8f\xbf\xbf", b"\xf4\x90\x80\x80",
           b"\xf5\x80\x80\x80", b"\xf8\x88\x80\x80\x80", b"\xff", b"\xfe", b"\xf0\x9f\x98", b"\xf0\x9f\x98\x80\x80"]
    for s in bad:
        for pre in (b"", b"a", b"ab"):
            for post in (b"", b"a", b"\x80"):
                body = pre + s + post
                add("corpus", frame(b"\x11" + u32(len(body)) + body))
    for cp in SCALARS:
        s = utf8_of(cp)
        add("corpus", frame(b"\x11" + u32(len(s)) + s))
    for lead in range(0x80, 0x100):
        for c1 in range(0x00, 0x100, 5):
            body = bytes([lead, c1, 0x80, 0x80])
            add("corpus", frame(b"\x11" + u32(4) + body))
    for ln in (0, 1, 65530, 65531, 65532, 65533, 65535, 65536, 65537, 0xFFFF, 0x10000, 0xFFFFFFFF):
        for tag in (0x11, 0x12):
            fill = 65530 if ln > 65535 else ln
            add("corpus", frame(bytes([tag]) + u32(ln) + b"x" * fill))
    for ln in range(1, 13):
        for last in list(range(0, 5)) + [0x7E, 0x7F, 0x80, 0x81, 0xFE, 0xFF]:
            for fill in (0x80, 0xFF):
                body = bytes([fill]) * (ln - 1) + bytes([last])
                add("corpus", frame(b"\x10" + body))
                add("corpus", frame(b"\x10" + body + b"\x02\x55"))
    for sh in (
        b"\x22\x00\x01\x00\x00\x00", b"\x22\x00\x01\x00\x00\x02\x02\x05\x00\x00\x00",
        b"\x22\x00\x01\x00\x05\x00\x00\x00\x00\x00", b"\x22\x00\x01\x00\x05\x00", b"\x22\x00\x01", b"\x22\x00\x01\x00\x00",
        b"\x22\x00", b"\x22\x00\x01\x00\x01\x02\x02\x05", b"\x20\x00\x00\x00\x00\x02\x22\x00\x01\x00\x00\x00",
        b"\x20\x00\x00\x00\x00\x02\x22\x00\x01\x00\x00\x00\x00", b"\x00\x00\x00", b"",
        b"\x22\x00\x01\xff\xff\x00\x00\x00\x00\x00",
    ):
        add("corpus", frame(sh))
        add("corpus", frame(sh, plen=len(sh) + 1))
        add("corpus", frame(sh, plen=max(len(sh) - 1, 0)))


def family_random(count):
    for _ in range(count):
        pl = gen_payload()
        f = frame(pl, flags=rng.choice(FLAGS), version=0x0500 | rng.randrange(256))
        if rng.randrange(4) == 0:
            add("corpus", f)
        f = mutate(f)
        r = rng.randrange(10)
        if r == 0:
            pass
        elif r < 3:
            f = repair(f, False, True)
        else:
            f = repair(f, True, True)
        add("corpus", f)


def family_lies():
    for _ in range(40):
        f = frame(gen_payload())
        for n in set(list(range(0, min(len(f) + 30, 140))) + [len(f) - 1, len(f), len(f) + 1, 2**32, 2**63, 2**64 - 1]):
            add("corpus", f, n)
    # the no-trap sweep around the capacity: a valid header for each n, real payload bytes
    for n in list(range(21, 120)) + list(range(65520, 65564)):
        plen = n - 21
        pl = (gen_payload() * 4000)[:plen] if plen > 200 else gen_payload()[:plen]
        pl = pl + b"\x00" * (plen - len(pl))
        add("corpus", frame(pl, plen=plen), n)
        add("corpus", frame(pl, plen=plen)[:65557], n)
    for n in (2**31, 2**32, 2**40, 2**63, 2**64 - 1, 65558, 70000, 16 * 1024 * 1024 + 21, 16 * 1024 * 1024 + 22):
        for _ in range(3):
            add("corpus", rng.randbytes(rng.randrange(0, 80)), n)
            add("caput", rng.randbytes(17), n)


# ---------------------------------------------------------------------- driver
def enc(fn, data, n):
    d = data[:CAP[fn]]
    line = "%s %s" % (fn, d.hex() if d else "-")
    if n != len(d):
        line += " %d" % n
    return line + "\n"


def main():
    args = sys.argv[1:]
    ncases = 200000
    seed = None
    while args and args[0].startswith("--"):
        if args[0] == "--cases":
            ncases = int(args[1]); args = args[2:]
        elif args[0] == "--seed":
            seed = int(args[1]); args = args[2:]
        else:
            print("bad option", args[0]); return 2
    hosts = args
    if not hosts:
        print("usage: proba.py [--cases N] [--seed S] HOST [HOST...]"); return 2
    if seed is not None:
        rng.seed(seed)

    family_headers()
    family_structured()
    family_lies()
    family_random(max(ncases - len(cases), 1000))
    # the same cases with a 0xA5 pad: pad bytes are never consulted
    garbage = [("corpusg" if fn == "corpus" else fn, d, n) for fn, d, n in cases[::7] if fn == "corpus"]

    total = cases + garbage
    want = [oracle(fn, d, n, 0xA5 if fn == "corpusg" else 0) for fn, d, n in total]
    hist = {}
    for w in want:
        hist[w] = hist.get(w, 0) + 1

    # anchors: known answers, so a corpus that quietly stopped reaching a check cannot pass
    g = frame(b"\x02\x09\x11" + u32(2) + b"hi", seq=1, msg=1)
    anchors = [("corpus", g, len(g), 0), ("corpus", g[:-1] + bytes([g[-1] ^ 1]), len(g), 8),
               ("corpus", frame(b"\xfe"), None, 9), ("corpus", frame(b"\x20\x00\x00\x00\x00\x05"), None, 12),
               ("corpus", frame(build_nest("array", 33)), None, 11), ("corpus", frame(b"\x11\x00\x00\x00\x01\xc0"), None, 14),
               ("corpus", frame(b"\x10\x80\x00"), None, 15), ("corpus", frame(b"\x11\x00\x01\x00\x01" + b"a" * 10), None, 13),
               ("corpus", frame(b"\x08\x01"), None, 10), ("caput", g[:17], 2**64 - 1, 6), ("caput", g[:17], 16, 1),
               ("caput", b"XCFS" + g[4:17], len(g), 2), ("caput", g[:4] + b"\x06" + g[5:17], len(g), 3),
               ("caput", g[:8] + b"\x20" + g[9:17], len(g), 4), ("corpus", frame(b"\x00" * 3, plen=70000)[:17] + b"\x00" * 40, 70021, 7)]
    bad = 0
    for fn, d, n, w in anchors:
        n = len(d) if n is None else n
        o = oracle(fn, d, n)
        if o != w:
            print("  [FAIL] oracle anchor %s: oracle %d, expected %d" % (fn, o, w)); bad += 1
    if bad:
        return 1

    batch = 1500
    for host in hosts:
        disagreements = 0
        for lo in range(0, len(total), batch):
            chunk = total[lo:lo + batch]
            payload = "".join(enc(fn, d, n) for fn, d, n in chunk)
            r = subprocess.run([host], input=payload.encode(), capture_output=True)
            if r.returncode != 0:
                print("  [FAIL] %s exited %d (a trap, or a sanitizer report): %s"
                      % (host, r.returncode, r.stderr.decode(errors="replace")[:600]))
                return 1
            got = r.stdout.decode().split("\n")[:-1]
            if len(got) != len(chunk):
                print("  [FAIL] host answered %d of %d cases" % (len(got), len(chunk))); return 1
            for (fn, d, n), g_, w in zip(chunk, got, want[lo:lo + batch]):
                if str(w) != g_:
                    disagreements += 1
                    if disagreements <= 8:
                        print("  [FAIL] %s n=%d len=%d: gate %s, oracle %d  %s" % (fn, n, len(d), g_, w, d[:40].hex()))
        if disagreements:
            print("  [FAIL] %s: %d disagreements of %d cases" % (host, disagreements, len(total)))
            return 1
    admitted = hist.get(0, 0)
    print("  [ok]   %d cases; %d admitted, %d refused, 0 disagreements" % (len(total), admitted, len(total) - admitted))
    print("  [ok]   verdicts reached (code:count): " + " ".join("%d:%d" % kv for kv in sorted(hist.items())))
    missing = [v for v in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15) if v not in hist]
    if missing:
        print("  [FAIL] the corpus never reaches verdict(s) %s -- it is too thin" % missing)
        return 1
    print("  [ok]   anchors: %d/%d; every length 0..140 and 65520..65563 and n = 2^32, 2^63, 2^64-1 answered" % (len(anchors), len(anchors)))
    return 0


sys.exit(main())
