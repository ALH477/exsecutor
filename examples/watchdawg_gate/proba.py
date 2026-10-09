#!/usr/bin/env python3
# examples/watchdawg_gate/proba.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
#
# The oracle for watchdawg_gate.exsc, driving the host built from proba.c.
#
#   proba.py HOST
#
# What "admitted" means is checked against THREE independent definitions that
# must agree with each other before they are trusted against the gate: a
# regular expression, a hand-written recursive-descent reader, and JSON's own
# parser (the document these numbers end up in). The refusal CODES (which check
# failed first) are checked against a plain left-to-right reading of the tables
# at the top of watchdawg_gate.exsc.
#
# Cases: every string over small alphabets (exhaustive), every length of every
# digit-run boundary, a hostile corpus of the spellings that matter in a JSON
# number position (a trailing comma and a forged field, nan, inf, 1e5, 0x1f,
# leading zeros, unicode digits, a newline), a seeded mutation fuzz, lying
# lengths and the no-trap sweep (every length from 0 to capacity+5 over random
# contents, plus n = 2^63 and 2^64-1). A trap aborts the host, so the sweep
# passing means the unit answered every one.
import itertools
import json
import random
import re
import subprocess
import sys

CAP = {"numerus": 20, "onus": 16}


# ----------------------------------------------------------------- oracles
def reader_numerum(s):
    """hand-written: 1..20 digits, a leading zero only if it is the whole text"""
    if not s or len(s) > 20 or any(c not in "0123456789" for c in s):
        return False
    return not (len(s) > 1 and s[0] == "0")


def reader_onus(s):
    """hand-written: int part, optional '.' + 1..6 digits"""
    if not s or len(s) > 13:
        return False
    ip, dot, fp = s.partition(".")
    if not ip or len(ip) > 6 or any(c not in "0123456789" for c in ip):
        return False
    if len(ip) > 1 and ip[0] == "0":
        return False
    if dot and (not fp or len(fp) > 6 or any(c not in "0123456789" for c in fp)):
        return False
    return True


def json_number(s, integer_only):
    """JSON's parser: does the text parse, as exactly one number, and is it the text we mean?"""
    try:
        v = json.loads(s)
    except ValueError:
        return False
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return False
    if integer_only and not isinstance(v, int):
        return False
    # json accepts a leading '-', an exponent, and 'NaN' etc.; the unit must not
    return re.fullmatch(r"[0-9.]+", s) is not None


def three_way(fn, b):
    """admitted? by regex, reader and json; they must agree (on texts within the length cap)."""
    try:
        s = b.decode("ascii")
    except UnicodeDecodeError:
        return False
    if fn == "numerus":
        rx = re.fullmatch(r"(?:0|[1-9][0-9]{0,19})", s) is not None
        rd = reader_numerum(s)
        js = len(s) <= 20 and json_number(s, True) if s else False
    else:
        rx = re.fullmatch(r"(?:0|[1-9][0-9]{0,5})(?:\.[0-9]{1,6})?", s) is not None
        rd = reader_onus(s)
        js = len(s) <= 13 and json_number(s, False) if s else False
        # json accepts "0.000000001" etc.; the cap on digits is the unit's own, so
        # compare only where the text is within the grammar's digit counts
        if js and not rx:
            ip = s.partition(".")[0]
            fp = s.partition(".")[2]
            js = len(ip) <= 6 and len(fp) <= 6 and not s.endswith(".")
    if not (rx == rd == js):
        raise SystemExit("oracle disagreement on %s %r: regex=%s reader=%s json=%s" % (fn, b, rx, rd, js))
    return rx


def padded(b, cap, n):
    """what the gate can read: the host's zero-padded buffer of exactly cap bytes."""
    return (b + b"\x00" * cap)[:cap][:n]


def ref_numerum(b, n):
    if n == 0:
        return 1
    if n > 20:
        return 2
    b = padded(b, 20, n)
    for c in b:
        if c < 0x30 or c > 0x39:
            return 3
    if n > 1 and b[0] == 0x30:
        return 4
    return 0


def ref_onus(b, n):
    if n == 0:
        return 1
    if n > 13:
        return 2
    b = padded(b, 16, n)
    integri = fracti = punctum = initium = 0
    for c in b:
        if c == 0x2E:
            if punctum == 1:
                return 7
            if integri == 0:
                return 4
            punctum = 1
        else:
            if c < 0x30 or c > 0x39:
                return 3
            if punctum == 0:
                if integri == 0:
                    initium = 1 if c == 0x30 else 0
                elif initium == 1:
                    return 5
                if integri >= 6:
                    return 6
                integri += 1
            else:
                if fracti >= 6:
                    return 7
                fracti += 1
    if punctum == 1 and fracti == 0:
        return 7
    return 0


def ref(fn, b, n):
    return ref_numerum(b, n) if fn == "numerus" else ref_onus(b, n)


# ------------------------------------------------------------------- cases
cases = []   # (fn, bytes, n)


def add(fn, b, n=None):
    cases.append((fn, bytes(b), len(b) if n is None else n))


def enc(fn, b, n):
    return "%s %s%s\n" % (fn, b.hex() or "-", "" if n == len(b) else " %d" % n)


rng = random.Random(0x57444754)   # fixed: the run is reproducible

# exhaustive, small alphabets
for k in range(0, 8):
    for t in itertools.product(b"0159.", repeat=k):
        add("numerus", bytes(t))
        add("onus", bytes(t))
for k in range(0, 5):
    for t in itertools.product(b"0123456789./:@a -\n,eE+", repeat=k):
        add("numerus", bytes(t))
        add("onus", bytes(t))

# every length of the digit runs that matter, with and without a leading zero
for n in range(0, 26):
    for first in b"0129":
        for rest in b"09":
            body = bytes([first]) + bytes([rest]) * max(n - 1, 0)
            add("numerus", body[:n])
for ip in range(0, 9):
    for fp in range(-1, 9):
        for lead in b"019":
            for rest in b"09":
                s = bytes([lead]) + bytes([rest]) * max(ip - 1, 0)
                s = s[:ip]
                if fp >= 0:
                    s += b"." + bytes([rest]) * fp
                add("onus", s)

# the hostile corpus: what could be pasted into a number position
HOSTILE = [
    b"0", b"00", b"007", b"1", b"10", b"18446744073709551615", b"18446744073709551616", b"99999999999999999999",
    b"100000000000000000000", b"0.42", b"12.5", b"104.50", b"100", b"0.0", b"0.", b".5", b".", b"..", b"1.2.3",
    b"1,", b"1, ", b'0.42,"injected":true', b"0.42,", b'1}', b"1\n", b" 1", b"1 ", b"\t1", b"1\r", b"1\x00",
    b"\x00", b"-1", b"+1", b"-0", b"1e5", b"1E5", b"1e+5", b"0x1f", b"0b1", b"0o7", b"1_000", b"1'000",
    b"NaN", b"nan", b"-nan", b"inf", b"-inf", b"Infinity", b"null", b"true", b"false", b"[1]", b'"1"',
    "٣".encode(), "１２".encode(), "1２".encode(), b"1\xff", b"\xff", b"1.5\xff", b"/proc", b"$(id)", b"`id`",
    b"1;", b"1|", b"1 2", b"0.4200000", b"1234567.1", b"123456.1234567", b"123456.123456", b"1234567",
    b"000.1", b"00.1", b"01.1", b"0.01", b"0.10", b"1.", b"1.e5", b"1.5.5", b"1..5",
    b'1,"x":2', b"1.1234567", b"1.123456", b"999999.999999", b"0.000001",
]
for h in HOSTILE:
    for fn in ("numerus", "onus"):
        add(fn, h)

# seeded mutation fuzz of valid texts
def valid_text(fn):
    if fn == "numerus":
        return str(rng.choice([0, 1, 7, 10, 99, 4096, 2**31, 2**63, 2**64 - 1, rng.randrange(10**rng.randrange(1, 21))])).encode()
    return ("%d.%0*d" % (rng.choice([0, 1, 9, 12, 104, 999999]), rng.randrange(1, 7), rng.randrange(10**3))).encode()


ALPHA = b"0123456789.,:/ \n\x00-+xeEaA\xff"
for fn in ("numerus", "onus"):
    for _ in range(3000):
        b = bytearray(valid_text(fn))
        for _ in range(rng.choice([1, 1, 2, 3])):
            op = rng.randrange(4)
            if op == 0 and b:
                del b[rng.randrange(len(b))]
            elif op == 1:
                b.insert(rng.randrange(len(b) + 1), rng.choice(ALPHA))
            elif op == 2 and b:
                b[rng.randrange(len(b))] = rng.choice(ALPHA)
            elif op == 3 and b:
                i = rng.randrange(len(b))
                b[i:i] = bytes(b[i:i + 1]) * 2
        add(fn, bytes(b))

# lying lengths and the no-trap sweep
for fn, cap in CAP.items():
    for n in (cap - 1, cap, cap + 1, cap + 8, 255, 4096, 2**32, 2**63 - 1, 2**63, 2**64 - 1):
        for _ in range(4):
            add(fn, bytes(rng.randrange(256) for _ in range(rng.randrange(cap + 1))), n)
    for n in range(0, cap + 6):
        for _ in range(60):
            body = bytes(rng.choice(b"0123456789.\xff\x00 ") for _ in range(min(n, cap)))
            add(fn, body, n)
    # n shorter than the bytes supplied: only the first n are the input
    for _ in range(300):
        body = valid_text(fn)
        add(fn, body, rng.randrange(0, len(body) + 1))


# ------------------------------------------------------------------ driver
def main():
    host = sys.argv[1]
    # the oracle's own consistency first: every case whose length is the byte count
    for fn, b, n in cases:
        if n == len(b):
            three_way(fn, b)
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
                print("  [FAIL] %s %r n=%d: gate %s, oracle %d" % (fn, b, n, g, want))
        if g == "0":
            admitted += 1
        else:
            refused += 1
    # the corpus's own anchors, so a vacuous pass cannot hide: these are known
    anchors = {("numerus", b"0"): 0, ("numerus", b"18446744073709551615"): 0, ("numerus", b"99999999999999999999"): 0,
               ("numerus", b"100000000000000000000"): 2, ("numerus", b"007"): 4, ("numerus", b"00"): 4,
               ("numerus", b""): 1, ("numerus", b"1e5"): 3, ("numerus", b"-1"): 3, ("numerus", b"1\n"): 3,
               ("numerus", b'1,"x":2'): 3, ("onus", b"0.42"): 0, ("onus", b"104.50"): 0, ("onus", b"3"): 0,
               ("onus", b".5"): 4, ("onus", b"00.5"): 5, ("onus", b"1234567.1"): 6, ("onus", b"1."): 7,
               ("onus", b"1.2.3"): 7, ("onus", b"1.1234567"): 7, ("onus", b"nan"): 3,
               ("onus", b'0.42,"injected":true'): 2, ("onus", b"0.42,"): 3}
    seen = {}
    for (fn, b, n), g in zip(cases, got):
        if n == len(b):
            seen[(fn, b)] = int(g)
    for k, v in anchors.items():
        if seen.get(k) != v:
            bad += 1
            print("  [FAIL] anchor %s %r: gate %s, expected %d" % (k[0], k[1], seen.get(k), v))
    if bad:
        print("  [FAIL] %d disagreements of %d cases" % (bad, len(cases)))
        return 1
    print("  [ok]   %d cases; %d admitted, %d refused, 0 disagreements" % (len(cases), admitted, refused))
    print("  [ok]   anchors: %d/%d" % (len(anchors), len(anchors)))
    print("  [ok]   no-trap sweep: every length 0..capacity+5, n=2^63 and 2^64-1, answered")
    return 0


sys.exit(main())
