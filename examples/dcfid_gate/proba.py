#!/usr/bin/env python3
# examples/dcfid_gate/proba.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
#
# The oracle for dcfid_gate.exsc, driving the host built from proba.c.
#
#   proba.py HOST [README]
#
# What "admitted" means is checked against an independent definition (a regular
# expression over the whole input, written without reference to the table) that
# must agree with the left-to-right reference before either is trusted against
# the gate. The refusal CODES (which check failed first) are checked against a
# plain left-to-right reading of the tables at the top of dcfid_gate.exsc.
#
# The README's "Anchors" table is parsed and every row is checked both against
# the reference and against the gate, so the document the Rust wrapper's tests
# are held to is itself held to the gate.
#
# Cases: every string over small alphabets (including the bytes on either side
# of every character class and the multi-byte spellings of look-alikes), every
# byte value in every position of a valid input, every amount boundary, the
# structured Stripe-Signature headers (every sequence of up to four items drawn
# from the malformed and well-formed spellings, every header of 0..12 items with
# the t in every position), a hostile corpus, a seeded mutation fuzz, lying
# lengths, and the no-trap sweep: every length from 0 to capacity+5 over random
# contents, plus n = 2^63 and 2^64-1. A trap aborts the host, so the sweep
# passing means the unit answered every one.
import itertools
import os
import random
import re
import subprocess
import sys

CAP = {"nomen": 32, "forma": 512}
H64 = b"0123456789abcdef" * 4


# ---------------------------------------------------------------- oracles
def rx_nomen(b):
    return re.fullmatch(rb"[A-Za-z0-9_-]{3,32}", b) is not None


def rx_signum(b, genus):
    if genus == 0:
        return re.fullmatch(rb"[A-Za-z0-9]{64}", b) is not None
    if genus == 1:
        return re.fullmatch(rb"[A-Za-z0-9]{32}", b) is not None
    return False


def rx_forma(b):
    items = b.split(b",")
    if len(items) > 8 or len(b) > 512:
        return False
    ts = vs = 0
    for it in items:
        if re.fullmatch(rb"t=[0-9]{1,12}", it):
            ts += 1
        elif re.fullmatch(rb"v1=[0-9a-f]{64}", it):
            vs += 1
        elif re.fullmatch(rb"v0=[0-9a-f]{64}", it):
            pass
        else:
            return False
    return ts == 1 and vs >= 1


def padded(b, cap, n):
    """what the gate can read: the host's zero-padded buffer of exactly cap bytes."""
    return (b + b"\x00" * cap)[:cap][:n]


def ref_nomen(b, n):
    if n == 0:
        return 1
    if n > 32:
        return 2
    if n < 3:
        return 3
    for c in padded(b, 32, n):
        if not (0x30 <= c <= 0x39 or 0x41 <= c <= 0x5A or 0x61 <= c <= 0x7A or c in (0x5F, 0x2D)):
            return 4
    return 0


def ref_signum(b, n, genus):
    if genus == 0:
        need = 64
    elif genus == 1:
        need = 32
    else:
        return 1
    if n != need:
        return 2
    for c in padded(b, 64, n):
        if not (0x30 <= c <= 0x39 or 0x41 <= c <= 0x5A or 0x61 <= c <= 0x7A):
            return 3
    return 0


def ref_summam(c):
    if c < 250:
        return 1
    if c > 10000:
        return 2
    return 0


def ref_forma(b, n):
    if n == 0:
        return 1
    if n > 512:
        return 2
    data = padded(b, 512, n)
    items = data.split(b",")
    ts = vs = 0
    for k, it in enumerate(items):
        if it.startswith(b"t="):
            if ts >= 1:
                return 7
            ts += 1
            p = it[2:]
            if not (1 <= len(p) <= 12) or any(not (0x30 <= c <= 0x39) for c in p):
                return 4
        elif it.startswith(b"v1=") or it.startswith(b"v0="):
            p = it[3:]
            if len(p) != 64 or any(not (0x30 <= c <= 0x39 or 0x61 <= c <= 0x66) for c in p):
                return 5
            if it[1:2] == b"1":
                vs += 1
        else:
            return 3
        if k == 7 and len(items) > 8:
            return 6
    if ts == 0:
        return 8
    if vs == 0:
        return 9
    return 0


def ref(fn, b, n):
    if fn == "nomen":
        return ref_nomen(b, n)
    if fn.startswith("signum:"):
        return ref_signum(b, n, int(fn[7:]))
    if fn == "summam":
        return ref_summam(int(b.decode()))
    return ref_forma(b, n)


def cross_check(fn, b):
    """the reference's verdict 0 must be exactly the regular expression's 'yes'"""
    v = ref(fn, b, len(b))
    if fn == "nomen":
        rx = rx_nomen(b)
    elif fn.startswith("signum:"):
        rx = rx_signum(b, int(fn[7:]))
    elif fn == "forma":
        rx = rx_forma(b) if len(b) > 0 else False
    else:
        return
    if (v == 0) != rx:
        raise SystemExit("oracle disagreement on %s %r: reference=%d regex=%s" % (fn, b, v, rx))


# ----------------------------------------------------------------- anchors
ESC = re.compile(rb"\\x([0-9a-f]{2})|\\\\|\{(\\x[0-9a-f]{2}|[^}*\\])\*([0-9]+)\}")


def unescape(s):
    """the README's input spelling: \\xNN, \\\\, and {c*N} for N copies of c"""
    out = bytearray()
    pos = 0
    sb = s.encode()
    for m in ESC.finditer(sb):
        out += sb[pos:m.start()]
        if m.group(1):
            out.append(int(m.group(1), 16))
        elif m.group(0) == b"\\\\":
            out += b"\\"
        else:
            ch = m.group(2)
            if ch.startswith(b"\\x"):
                ch = bytes([int(ch[2:], 16)])
            out += ch * int(m.group(3))
        pos = m.end()
    out += sb[pos:]
    return bytes(out)


def read_anchors(path):
    rows = []
    on = False
    for line in open(path, encoding="utf-8"):
        if "<!-- anchors:begin -->" in line:
            on = True
            continue
        if "<!-- anchors:end -->" in line:
            break
        if not on or not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) != 4 or cells[0] in ("fn", "---") or set(cells[0]) == {"-"}:
            continue
        fn, inp, n, verdict = cells
        assert inp.startswith("`") and inp.endswith("`"), line
        b = unescape(inp[1:-1])
        rows.append((fn, b, len(b) if n == "=" else int(n), int(verdict)))
    return rows


# ------------------------------------------------------------------- cases
cases = []   # (fn, bytes, n)


def add(fn, b, n=None):
    cases.append((fn, bytes(b), len(b) if n is None else n))


def enc(fn, b, n):
    if fn == "summam":
        return "summam %s\n" % b.decode()
    return "%s %s%s\n" % (fn, b.hex() or "-", "" if n == len(b) else " %d" % n)


rng = random.Random(0x44434649)   # fixed: the run is reproducible
GENERA = (0, 1, 2, 3, 255)
SIGNA = ["signum:%d" % g for g in GENERA]


def valid_nomen():
    return bytes(rng.choice(b"abcXYZ019_-") for _ in range(rng.randrange(3, 33)))


def valid_signum(k):
    return bytes(rng.choice(b"abcXYZ019") for _ in range(k))


# ---- username: exhaustive small alphabets, then every byte in every position
for k in range(0, 5):
    for t in itertools.product(b"aZ0_-. \x00\n\x7f\x80\xff/:@[`{", repeat=k):
        add("nomen", bytes(t))
for k in range(0, 42):
    add("nomen", bytes(b"aB3_-"[i % 5] for i in range(k)))
for ln in (3, 32):
    base = bytes(b"aB3_-"[i % 5] for i in range(ln))
    for pos in range(ln):
        for v in range(256):
            add("nomen", base[:pos] + bytes([v]) + base[pos + 1:])
HOMOGLYPHS = ["аdmin", "аdmin".encode("utf-16-le").decode("utf-16-le"), "ａdmin", "ad​min", "admin‮",
              "١٢٣", "alicé", "名前です", "ñandú", "ééé", "ⅠⅡⅢ",
              "a\u0000b", "ab\n", "ab\r\n", " ab", "a b", "a.b", "a@b", "a/b", "a\\b", "a'b", 'a"b', "a;b", "a`b",
              "$(id)", "../..", "-", "--", "---", "___", "AAA", "000", "adm", "ad", "a", "SELECT", "root", "x" * 32,
              "x" * 33, "x" * 31 + "é"]
for h in HOMOGLYPHS:
    add("nomen", h.encode())
for _ in range(2500):
    b = bytearray(valid_nomen())
    for _ in range(rng.choice([1, 1, 2, 3])):
        op = rng.randrange(4)
        if op == 0 and b:
            del b[rng.randrange(len(b))]
        elif op == 1:
            b.insert(rng.randrange(len(b) + 1), rng.choice(b"-_aZ09 .@\x00\n\x7f\x80\xc3\xff"))
        elif op == 2 and b:
            b[rng.randrange(len(b))] = rng.choice(b"-_aZ09 .@\x00\n\x7f\x80\xc3\xff")
        elif op == 3 and b:
            i = rng.randrange(len(b))
            b[i:i] = bytes(b[i:i + 1]) * 2
    add("nomen", bytes(b))

# ---- session id / access token
for g in SIGNA:
    for k in range(0, 72):
        add(g, valid_signum(k))
for g, ln in (("signum:0", 64), ("signum:1", 32)):
    base = valid_signum(ln)
    for pos in range(ln):
        for v in range(256):
            add(g, base[:pos] + bytes([v]) + base[pos + 1:])
for g in SIGNA:
    for h in (b"", b"\x00" * 32, b"\x00" * 64, b"A" * 32 + b"\n", b"A" * 31 + b"-", b"A" * 63 + b"_", b"A" * 64,
              b"A" * 32, b"A" * 31 + "é".encode(), b"A" * 62 + "é".encode(), b" " + b"A" * 31, b"A" * 65, b"A" * 33,
              b"\xff" * 64, b"\xff" * 32, b"../" + b"A" * 29, b"A" * 30 + b"\r\n"):
        add(g, h)
for _ in range(2500):
    ln = rng.choice([31, 32, 33, 63, 64, 65, rng.randrange(0, 70)])
    b = bytearray(valid_signum(ln))
    for _ in range(rng.choice([1, 2])):
        op = rng.randrange(3)
        if op == 0 and b:
            del b[rng.randrange(len(b))]
        elif op == 1:
            b.insert(rng.randrange(len(b) + 1), rng.choice(b"-_ +/=\x00\n\x7f\x80\xff"))
        elif b:
            b[rng.randrange(len(b))] = rng.choice(b"-_ +/=\x00\n\x7f\x80\xff")
    for g in SIGNA:
        add(g, bytes(b))

# ---- amount in cents
for v in range(0, 10120):
    add("summam", str(v).encode())
for v in (2**16, 2**31 - 1, 2**31, 2**32 - 1, 2**32, 2**32 + 250, 2**53, 2**63 - 1, 2**63, 2**63 + 250,
          2**64 - 1, 2**64 - 250, 10**18, 99999999999):
    add("summam", str(v).encode())
for _ in range(200):
    add("summam", str(rng.randrange(0, 2**64)).encode())

# ---- Stripe-Signature shape
V = H64                                  # a valid 64-hex payload
TOKF = [b"t=1", b"t=123456789012", b"t=1234567890123", b"t=", b"t=x", b"t=1x", b"v1=" + V, b"v1=" + V[:63],
        b"v1=" + V + b"0", b"v1=" + V.upper(), b"v0=" + V, b"v2=" + V, b"", b"x", b"t", b"v", b"v1", b"v1=", b"tt=1",
        b"v0=" + V[:10]]
for k in range(1, 5):
    toks = TOKF if k <= 3 else TOKF[:14]
    for t in itertools.product(toks, repeat=k):
        add("forma", b",".join(t))
# well-formed headers of 0..12 items with the t in every position
for total in range(0, 13):
    for tpos in range(-1, total):
        for vkind in (b"v1=", b"v0="):
            items = []
            for i in range(total):
                items.append(b"t=1492774577" if i == tpos else vkind + V)
            add("forma", b",".join(items))
            if total >= 2:
                items2 = list(items)
                items2[-1] = b"v1=" + V
                add("forma", b",".join(items2))
# eight items with the t in every position, then a short ninth (the 8th ',' must answer 6)
for tpos in range(8):
    for ninth in (b"x", b"", b"t=2", b"v1=", b"v0=" + V[:5], b"zz=1"):
        items = [b"t=1" if i == tpos else b"v1=" + V for i in range(8)]
        add("forma", b",".join(items) + b"," + ninth)
        items = [b"t=1" if i == tpos else (b"v0=" + V if i % 2 else b"v1=" + V) for i in range(8)]
        add("forma", b",".join(items))
# every byte value in every position of the payloads: 64-hex v1 and v0, 12-digit and 1-digit t
for kind in (b"v1=", b"v0="):
    for pos in range(64):
        for v in range(256):
            p = bytearray(V)
            p[pos] = v
            add("forma", b"t=1," + kind + bytes(p) + (b",v1=" + V if kind == b"v0=" else b""))
for tp in (b"123456789012", b"7", b"1492774577"):
    for pos in range(len(tp)):
        for v in range(256):
            p = bytearray(tp)
            p[pos] = v
            add("forma", b"t=" + bytes(p) + b",v1=" + V)
# over-long runs of hex / digits inside one payload (a counter that is not bounded would wrap or trap)
for run in (64, 65, 66, 100, 127, 128, 255, 256, 257, 300, 400, 505):
    add("forma", b"t=1,v1=" + b"a" * run)
    add("forma", b"t=1,v0=" + b"f" * run + b",v1=" + V[:10])
for run in (12, 13, 14, 255, 256, 257, 300, 505):
    add("forma", b"t=" + b"7" * run + b",v1=" + V)
    add("forma", b"t=" + b"7" * run)
# eight items, nine items: the ninth is malformed or not, the t is late or twice
for ninth in (b"", b"v1=" + V, b"zz", b"t=2", b"v1=" + V[:3]):
    base = [b"t=1"] + [b"v1=" + V] * 7
    add("forma", b",".join(base + [ninth]))
    add("forma", b",".join(base) + b",")
    add("forma", b",".join([b"t=1"] + [b"v0=" + V] * 7 + [ninth]))
# the shape Stripe documents, and the ways it goes wrong in transit
REAL = b"t=1492774577,v1=" + V + b",v0=" + V
add("forma", REAL)
for h in [REAL + b" ", b" " + REAL, REAL + b"\n", REAL + b"\r\n", REAL + b",", b"," + REAL, REAL.replace(b",", b", "),
          REAL.replace(b",", b";"), REAL.replace(b"=", b":"), REAL.upper(), REAL.replace(b"t=", b"T="),
          REAL.replace(b"v1=", b"V1="), b"t=1492774577", b"v1=" + V, b"t=1492774577,v0=" + V, b"t=,v1=" + V,
          b"t=0,v1=" + V, b"t=000000000001,v1=" + V, b"t=-1,v1=" + V, b"t=+1,v1=" + V, b"t=1.5,v1=" + V,
          b"t=\xd9\xa1,v1=" + V, b"t=1,v1=" + V + b"\x00", b"t=1,v1=" + V[:-1] + b"G", b"t=1,v1=" + V[:-1] + b"\xff",
          b"v1=" + V + b",t=1", b"v0=" + V + b",t=1,v1=" + V, b"t=1,t=2,v1=" + V, b"t=1,v1=" + V + b",t=2",
          b"t=1,v1=" + V + b",v1=" + V, b"t=1,v1=" + V + b",v3=" + V, b"t=1,v1=" + V + b",,v1=" + V,
          b"t=1,v1=" + V + b",v1", b"t=1,v1=" + V + b",t", b"t=1,v1=" + V + b",v", b"t=1,v1=" + V + b",v1=",
          b"tv", b"vt", b"v1=t=1", b"t=v1=", b",", b",,", b"=", b"==", b"t==1,v1=" + V]:
    add("forma", h)
for ln in (509, 510, 511, 512, 513, 514, 1000):
    add("forma", b"t=1,v1=" + V + b"," + b"x" * 600, ln)
    add("forma", (b"t=1,v1=" + V + b",v0=" + V + b",")[:ln].ljust(ln, b"a"))
for _ in range(2500):
    items = [b"t=%d" % rng.randrange(10**rng.randrange(1, 13))] + [b"v%d=" % rng.choice([0, 1]) + V for _ in range(rng.randrange(1, 4))]
    rng.shuffle(items)
    b = bytearray(b",".join(items))
    for _ in range(rng.choice([1, 1, 2, 3])):
        op = rng.randrange(4)
        if op == 0 and b:
            del b[rng.randrange(len(b))]
        elif op == 1:
            b.insert(rng.randrange(len(b) + 1), rng.choice(b"tv01=,9afAFgGzZ:/@` x\x00\xff"))
        elif op == 2 and b:
            b[rng.randrange(len(b))] = rng.choice(b"tv01=,9afAFgGzZ:/@` x\x00\xff")
        elif op == 3 and b:
            i = rng.randrange(len(b))
            b[i:i] = bytes(b[i:i + 1]) * 2
    add("forma", bytes(b))

# ---- lying lengths and the no-trap sweep
for fn, cap in list(CAP.items()) + [(g, 64) for g in SIGNA]:
    for n in (cap - 1, cap, cap + 1, cap + 8, 255, 4096, 2**32, 2**63 - 1, 2**63, 2**64 - 1):
        for _ in range(4):
            add(fn, bytes(rng.randrange(256) for _ in range(rng.randrange(cap + 1))), n)
    for n in range(0, cap + 6):
        for _ in range(40):
            alpha = b"aZ09_-,=tv1\xff\x00 " if fn != "forma" else b"tv01=,9af\xff\x00 "
            body = bytes(rng.choice(alpha) for _ in range(min(n, cap)))
            add(fn, body, n)
    # n shorter than the bytes supplied: only the first n are the input
    for _ in range(300):
        body = {"nomen": valid_nomen, "forma": lambda: REAL}.get(fn, lambda: valid_signum(64))()
        add(fn, body, rng.randrange(0, len(body) + 1))


# ------------------------------------------------------------------ driver
def main():
    host = sys.argv[1]
    readme = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "README.md")
    # the oracle's own consistency first: every whole-input case, two ways
    for fn, b, n in cases:
        if fn != "summam" and n == len(b) and n <= 1500:
            cross_check(fn, b)
    anchors = read_anchors(readme)
    if len(anchors) < 40:
        print("  [FAIL] README anchors table: %d rows parsed, expected at least 40" % len(anchors))
        return 1
    for fn, b, n, want in anchors:
        if ref(fn, b, n) != want:
            print("  [FAIL] README anchor %s %r n=%d says %d, the reference says %d" % (fn, b, n, want, ref(fn, b, n)))
            return 1
    for fn, b, n, want in anchors:
        cases.append((fn, b, n))
    payload = "".join(enc(fn, b, n) for fn, b, n in cases)
    r = subprocess.run([host], input=payload.encode(), capture_output=True)
    if r.returncode != 0:
        print("  [FAIL] host exited %d (a trap, or a sanitizer report): %s"
              % (r.returncode, r.stderr.decode(errors="replace")[:600]))
        return 1
    got = r.stdout.decode().split("\n")[:-1]
    if len(got) != len(cases):
        print("  [FAIL] host answered %d of %d cases" % (len(got), len(cases)))
        return 1
    bad = 0
    admitted = refused = 0
    for (fn, b, n), g in zip(cases, got):
        want = ref(fn, b, n)
        if str(want) != g:
            bad += 1
            if bad <= 10:
                print("  [FAIL] %s %r n=%d: gate %s, oracle %d" % (fn, b[:80], n, g, want))
        if g == "0":
            admitted += 1
        else:
            refused += 1
    # every README anchor, against the GATE's own answer (the last len(anchors) cases)
    tail = list(zip(cases, got))[-len(anchors):]
    for (fn, b, n), g in tail:
        if (fn, b, n) not in {(a[0], a[1], a[2]) for a in anchors}:
            bad += 1
            print("  [FAIL] anchor bookkeeping")
    want_by = {(a[0], a[1], a[2]): a[3] for a in anchors}
    for (fn, b, n), g in tail:
        if str(want_by[(fn, b, n)]) != g:
            bad += 1
            print("  [FAIL] README anchor %s %r n=%d: gate %s, README %d" % (fn, b[:80], n, g, want_by[(fn, b, n)]))
    # every verdict code of every table must occur, so a vacuous pass cannot hide
    seen = {}
    for (fn, b, n), g in zip(cases, got):
        seen.setdefault(fn.split(":")[0], set()).add(int(g))
    need = {"nomen": {0, 1, 2, 3, 4}, "signum": {0, 1, 2, 3}, "summam": {0, 1, 2}, "forma": set(range(10))}
    for fn, codes in need.items():
        if not codes <= seen.get(fn, set()):
            bad += 1
            print("  [FAIL] %s: verdicts never produced: %s" % (fn, sorted(codes - seen.get(fn, set()))))
    if bad:
        print("  [FAIL] %d disagreements of %d cases" % (bad, len(cases)))
        return 1
    print("  [ok]   %d cases; %d admitted, %d refused, 0 disagreements" % (len(cases), admitted, refused))
    print("  [ok]   README anchors: %d/%d, against the reference and against the gate" % (len(anchors), len(anchors)))
    print("  [ok]   every verdict code of every table occurs; admitted is also the regex's 'yes' exactly")
    print("  [ok]   no-trap sweep: every length 0..capacity+5, n=2^63 and 2^64-1, answered")
    return 0


sys.exit(main())
