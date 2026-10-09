#!/usr/bin/env python3
# examples/dcf_net_gate/proba.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
#
# The oracle for dcf_net_gate.exsc, driving the host built from proba.c.
#
#   proba.py HOST
#
# What "admitted" means is checked against THREE independent definitions that
# must agree with each other before they are trusted against the gate: a
# regular expression, Python's ipaddress module, and libc's inet_pton (which
# is what nft's own parser sits on). The refusal CODES (which check failed
# first) are checked against a plain left-to-right reading of the table at
# the top of dcf_net_gate.exsc.
#
# Cases: every string over small alphabets (exhaustive, including the bytes
# on either side of '0'..'9' and '.'), every octet value and every port and
# interval boundary, a hostile corpus of the spellings that have actually
# bitten (1.2.3.08, 0x7f.1, 127.1, a trailing newline, a NUL, unicode digits),
# a seeded mutation fuzz of valid addresses, and lying lengths. The no-trap
# sweep is the last: every length from 0 to capacity+5 over random contents,
# plus n = 2^63 and 2^64-1. A trap aborts the host, so the sweep passing means
# the unit answered every one.
import ipaddress
import itertools
import random
import re
import socket
import subprocess
import sys

CAP = {"ipv4": 16, "ordo": 16, "porta": 8, "intervallum": 8}
MAXV = {"porta": 65535, "intervallum": 3600}


# ----------------------------------------------------------------- oracles
def three_way_ipv4(b):
    """admitted? by regex, ipaddress and inet_pton; they must agree."""
    try:
        s = b.decode("ascii")
    except UnicodeDecodeError:
        return False
    rx = re.fullmatch(r"(?:0|[1-9][0-9]{0,2})(?:\.(?:0|[1-9][0-9]{0,2})){3}", s) is not None
    if rx:
        rx = all(int(p) <= 255 for p in s.split("."))
    if "\x00" in s or "\n" in s:
        ia = ip = False
    else:
        try:
            ipaddress.IPv4Address(s)
            ia = True
        except ValueError:
            ia = False
        try:
            socket.inet_pton(socket.AF_INET, s)
            ip = True
        except (OSError, ValueError):
            ip = False
    if not (rx == ia == ip):
        # ipaddress/inet_pton are looser than the grammar in one known way:
        # none, as of the Pythons this runs on. If they ever differ, say so.
        raise SystemExit("oracle disagreement on %r: regex=%s ipaddress=%s inet_pton=%s" % (b, rx, ia, ip))
    return rx


def padded(b, cap, n):
    """what the gate can read: the host's zero-padded buffer of exactly cap bytes."""
    return (b + b"\x00" * cap)[:cap][:n]


def ref_ipv4(b, n):
    if n == 0:
        return 1
    if n > 15:
        return 2
    b = padded(b, 16, n)
    puncta = digiti = valor = 0
    initium = 0
    for c in b:
        if c == 0x2E:
            if digiti == 0:
                return 4
            if puncta >= 3:
                return 4
            puncta += 1
            digiti = valor = initium = 0
        else:
            if c < 0x30 or c > 0x39:
                return 3
            if digiti >= 3:
                return 5
            if digiti == 0:
                initium = 1 if c == 0x30 else 0
            elif initium == 1:
                return 6
            valor = valor * 10 + (c - 0x30)
            if valor > 255:
                return 7
            digiti += 1
    if puncta != 3:
        return 4
    if digiti == 0:
        return 4
    return 0


NETS = [
    (ipaddress.ip_network("0.0.0.0/8"), 1), (ipaddress.ip_network("127.0.0.0/8"), 2),
    (ipaddress.ip_network("169.254.0.0/16"), 3), (ipaddress.ip_network("224.0.0.0/4"), 4),
    (ipaddress.ip_network("240.0.0.0/4"), 5), (ipaddress.ip_network("10.0.0.0/8"), 6),
    (ipaddress.ip_network("172.16.0.0/12"), 6), (ipaddress.ip_network("192.168.0.0/16"), 6),
    (ipaddress.ip_network("100.64.0.0/10"), 7),
]


def ref_ordo(b, n):
    if ref_ipv4(b, n) != 0:
        return 255
    a = ipaddress.IPv4Address(padded(b, 16, n).decode("ascii"))
    for net, k in NETS:
        if a in net:
            return k
    return 0


def ref_num(b, n, fn):
    if n == 0:
        return 1
    if n > 5:
        return 2
    b = padded(b, 8, n)
    if any(c < 0x30 or c > 0x39 for c in b):
        return 3
    if b[0] == 0x30 and n > 1:
        return 4
    v = int(b.decode("ascii"))
    if v == 0 or v > MAXV[fn]:
        return 5
    rx = re.fullmatch(r"[1-9][0-9]{0,4}", b.decode("ascii")) is not None and 1 <= v <= MAXV[fn]
    assert rx, (b, fn)
    return 0


def ref(fn, b, n):
    if fn == "ipv4":
        return ref_ipv4(b, n)
    if fn == "ordo":
        return ref_ordo(b, n)
    return ref_num(b, n, fn)


# ------------------------------------------------------------------- cases
cases = []   # (fn, bytes, n)


def add(fn, b, n=None):
    cases.append((fn, bytes(b), len(b) if n is None else n))


def enc(fn, b, n):
    return "%s %s%s\n" % (fn, b.hex() or "-", "" if n == len(b) else " %d" % n)


rng = random.Random(0x44434649)   # fixed: the run is reproducible

# exhaustive, small alphabets
for k in range(0, 8):
    for t in itertools.product(b"0159.", repeat=k):
        add("ipv4", bytes(t))
for k in range(0, 5):
    for t in itertools.product(b"0123456789./:@a -\n", repeat=k):
        add("ipv4", bytes(t))
for fn in ("porta", "intervallum"):
    for k in range(0, 7):
        for t in itertools.product(b"01259a:/", repeat=k):
            add(fn, bytes(t))

# structured: 1..6 dot-joined tokens drawn from the boundary values, so an
# address with too many octets is followed by something that would earn a
# DIFFERENT verdict if the too-many-dots check were not made at the dot
TOK = [b"0", b"1", b"9", b"255", b"256", b"08", b"999"]
for k in range(1, 7):
    for t in itertools.product(TOK, repeat=k):
        add("ipv4", b".".join(t))
for k in range(1, 5):
    for t in itertools.product(TOK, repeat=k):
        add("ordo", b".".join(t))

# every octet value, in every position, and the ordo class boundaries
for v in range(0, 300):
    for pos in range(4):
        parts = ["1", "2", "3", "4"]
        parts[pos] = str(v)
        add("ipv4", ".".join(parts).encode())
for a in range(0, 256):
    for s in (0, 1, 15, 16, 31, 32, 63, 64, 100, 127, 128, 167, 168, 169, 253, 254, 255):
        add("ordo", ("%d.%d.9.9" % (a, s)).encode())
for v in list(range(0, 70010)) + [99999]:
    add("porta", str(v).encode())
for v in range(0, 4010):
    add("intervallum", str(v).encode())

# the hostile corpus
HOSTILE = [
    b"1.2.3.08", b"01.2.3.4", b"1.02.3.4", b"1.2.3.004", b"1.2.3.256", b"256.1.1.1", b"1.2.3.4.5",
    b"1.2.3", b"1..2.3", b".1.2.3", b"1.2.3.", b"1.2.3.4\n", b"1.2.3.4 ", b" 1.2.3.4", b"1.2.3.4\x00",
    b"1.2.3.4/32", b"1.2.3.4,5.6.7.8", b"1.2.3.4;", b"1.2.3.4 accept", b"0x7f.0.0.1", b"127.1",
    b"2130706433", b"0177.0.0.1", b"1e2.1.1.1", b"+1.2.3.4", b"-1.2.3.4", b"1.2.3.-4",
    "١.٢.٣.٤".encode(), "1.2.3.４".encode(), b"1.2.3.4\xff", b"\xff\xff\xff\xff",
    b"255.255.255.255", b"0.0.0.0", b"999.999.999.999", b"192.168.001.001", b"1.1.1.1.",
    b"::1", b"::ffff:1.2.3.4", b"1.2.3.4%eth0", b"1.2.3.4:80", b"[1.2.3.4]", b"a.b.c.d", b"....",
    b"123456789012345", b"1234567890123456", b"255.255.255.2555", b"1.2.3.4$(id)", b"1.2.3.4`id`",
    b"1.2.3.4\r\n", b"1.2.3.4\t", b"\t1.2.3.4", b"1.2.3.4\x0b", b"0", b".", b"0.0.0.00",
]
for h in HOSTILE + [b"127.0.0.1", b"10.1.2.3", b"8.8.8.8", b"169.254.1.1", b"224.0.0.1", b"100.64.0.1"]:
    for fn in ("ipv4", "ordo"):
        add(fn, h)
for h in [b"7777", b"07777", b"7777 ", b" 7777", b"7777\n", b"0", b"00", b"65535", b"65536", b"99999",
          b"100000", b"-1", b"+1", b"1e3", b"0x1f90", b"7777 accept", b"7777;", b"1-2", b"1,2",
          "７７７７".encode(), b"\x00", b"7\x007", b"3600", b"3601", b"10s", b"1.5", b"0.5", b"60"]:
    for fn in ("porta", "intervallum"):
        add(fn, h)

# seeded mutation fuzz of valid addresses
def valid_addr():
    return ".".join(str(rng.choice([0, 1, 9, 10, 99, 100, 127, 169, 172, 192, 224, 254, 255, rng.randrange(256)]))
                    for _ in range(4)).encode()


ALPHA = b"0123456789.:/ \n\x00-+xaA\xff"
for _ in range(3000):
    b = bytearray(valid_addr())
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
    add("ipv4", bytes(b))
    add("ordo", bytes(b))
for _ in range(1500):
    b = bytearray(str(rng.choice([1, 80, 7777, 65535, 3600, rng.randrange(70000)])).encode())
    for _ in range(rng.choice([1, 2])):
        op = rng.randrange(3)
        if op == 0 and b:
            del b[rng.randrange(len(b))]
        elif op == 1:
            b.insert(rng.randrange(len(b) + 1), rng.choice(ALPHA))
        elif b:
            b[rng.randrange(len(b))] = rng.choice(ALPHA)
    add("porta", bytes(b))
    add("intervallum", bytes(b))

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
        body = valid_addr() if fn in ("ipv4", "ordo") else str(rng.randrange(70000)).encode()
        add(fn, body, rng.randrange(0, len(body) + 1))


# ------------------------------------------------------------------ driver
def main():
    host = sys.argv[1]
    # the oracle's own consistency first: every ipv4 case, three ways
    for fn, b, n in cases:
        if fn == "ipv4" and n == len(b) and n <= 15:
            three_way_ipv4(b)
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
    anchors = {("ipv4", b"1.2.3.4"): 0, ("ipv4", b"1.2.3.08"): 6, ("ipv4", b"1.2.3.256"): 7,
               ("ipv4", b"1.2.3.4\n"): 3, ("ipv4", b"255.255.255.255"): 0, ("ordo", b"127.0.0.1"): 2,
               ("ordo", b"10.1.2.3"): 6, ("ordo", b"1.2.3.08"): 255, ("porta", b"7777"): 0,
               ("porta", b"07777"): 4, ("porta", b"65536"): 5, ("intervallum", b"3601"): 5,
               ("intervallum", b"10"): 0}
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
