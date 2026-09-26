#!/usr/bin/env python3
"""The tempus oracle: examples/tempus/probatio.exsc's stream, computed
independently in Python.

Verification-only, never on the build closure (spec 18). It writes the bytes
tests/programs/tempus/expected.out holds, by different means from the
register's: frames are struct.pack'ed field by field (the codec's are stores
into a @transitus record), the CRC is table-driven (the codec's is the
bitwise division), the unwrap is Punctim's `unwrap_pid` formula verbatim
(the register's is the shift-and-compare form), and the two published frames
are the vendored certificate's own bytes, re-read from the JSON with --wire.

Usage: tempus_oracle.py [--wire vendor/hydramesh-wire/golden_vectors.json] [OUT]
"""

import json
import struct
import sys

M64 = 1 << 64
EXAMPLE_FRAME = "d31312340001ffffdeadbeefab12cd24c0"   # golden_vectors.json anchor
HYDRAMODEM_FRAME = "d310123400a1ffffdeadbeef0a1b2ca961"  # vendor/hydramodem-tx

TABLE = []
for _n in range(256):
    _c = _n << 8
    for _ in range(8):
        _c = ((_c << 1) ^ 0x1021) & 0xFFFF if _c & 0x8000 else (_c << 1) & 0xFFFF
    TABLE.append(_c)


def crc16(data):
    c = 0xFFFF
    for b in data:
        c = ((c << 8) & 0xFFFF) ^ TABLE[((c >> 8) ^ b) & 0xFF]
    return c


def frame(genus, numerus, fons, meta, onus, tempus):
    body = struct.pack(">BBHHHI", 0xD3, 0x10 | genus, numerus, fons, meta, onus)
    body += bytes([(tempus >> 16) & 0xFF, (tempus >> 8) & 0xFF, tempus & 0xFF])
    return body + struct.pack(">H", crc16(body))


def unwrap(prev_abs, raw, mod=1 << 24):
    fwd = (raw + mod - prev_abs % mod) % mod
    return prev_abs + fwd if fwd <= mod // 2 else max(0, prev_abs - (mod - fwd))


def read(w, prev):
    """(verdict, genus, numerus, fons, meta, onus, tempus): the codec's order."""
    if w[0] != 0xD3:
        return (1, 0, 0, 0, 0, 0, 0)
    if w[1] >> 4 != 1:
        return (2, 0, 0, 0, 0, 0, 0)
    if crc16(w[:15]) != struct.unpack(">H", w[15:17])[0]:
        return (3, 0, 0, 0, 0, 0, 0)
    numerus, fons, meta, onus = struct.unpack(">HHHI", w[2:12])
    t24 = int.from_bytes(w[12:15], "big")
    return (0, w[1] & 0xF, numerus, fons, meta, onus, unwrap(prev, t24))


class Out:
    def __init__(self):
        self.b = bytearray()

    def u64(self, v):
        assert 0 <= v < M64, v
        self.b += struct.pack(">Q", v)

    def raw(self, bs):
        assert len(bs) == 17
        self.b += bs

    def receptum(self, r):
        for v in r:
            self.u64(v)


class Reg:
    def __init__(self, fons, meta, genus):
        ok = fons <= 0xFFFF and meta <= 0xFFFF and genus <= 15
        self.held = 1 if ok else 0
        self.genus, self.fons, self.meta = (genus, fons, meta) if ok else (0, 0, 0)
        self.numerus = 0
        self.tempus = 0

    def adeo(self, dt):
        if not self.held or dt > 0xFFFFFFFF:
            return 0
        self.tempus += dt
        return dt

    def signa(self, onus):
        w = frame(self.genus, self.numerus, self.fons, self.meta,
                  onus if onus <= 0xFFFFFFFF else 0, self.tempus % (1 << 24))
        self.numerus = (self.numerus + 1) % 65536
        return w

    def dump(self, o):
        for v in [self.held, self.genus, self.fons, self.meta, self.numerus,
                  self.tempus, self.tempus % (1 << 24), self.tempus >> 24]:
            o.u64(v)


def sectio_certa(o):
    a = Reg(0x0001, 0xFFFF, 3)
    a.numerus = 0x1234
    a.adeo(0xAB12CD)
    fa = a.signa(0xDEADBEEF)
    assert fa.hex() == EXAMPLE_FRAME, fa.hex()
    o.raw(fa)
    o.receptum(read(fa, 0))
    b = Reg(0x00A1, 0xFFFF, 0)
    b.numerus = 0x1234
    b.adeo(0x0A1B2C)
    fb = b.signa(0xDEADBEEF)
    assert fb.hex() == HYDRAMODEM_FRAME, fb.hex()
    o.raw(fb)
    o.receptum(read(fb, 0))


def sectio_registri(o):
    r = Reg(7, 0x0104, 2)
    r.dump(o)
    o.u64(r.adeo(100000))
    o.raw(r.signa(0))
    o.u64(r.adeo(50000))
    o.raw(r.signa(0))
    r.dump(o)
    r.held = 0
    o.u64(0)                        # concede(0)
    o.u64(r.adeo(100000))           # 0: released
    o.raw(r.signa(0))
    r.dump(o)
    r.held = 1
    o.u64(0)                        # concede(1)
    r.meta = 0xFFFF
    o.u64(0)                        # migra(lobby)
    o.raw(r.signa(0x00C0FFEE))
    r.dump(o)
    o.u64(r.adeo((1 << 24) - 150000))
    w = r.signa(0)
    o.raw(w)
    r.dump(o)
    v = bytearray(w)
    v[11] ^= 1
    o.raw(bytes(v))
    o.receptum(read(bytes(v), 0))
    o.receptum(read(w, 0))
    for args in [(0x10000, 0, 2), (0, 0x10000, 2), (0, 0, 16)]:
        o.u64(Reg(*args).held)
    o.u64(1)                        # concede(2) refused
    o.u64(1)                        # migra(0x10000) refused
    o.u64(r.adeo(1 << 32))          # 0: refused
    o.u64(r.adeo(0xFFFFFFFF))
    o.raw(r.signa(1 << 32))         # onus sealed as 0
    r.dump(o)


def sectio_lecturae(o):
    prev = 0
    for t in [5, 0xFFFFF0, 0x1000010, 0x1800000, 0x1800001, 0x0FFFFFFF, 0x10000000, 3,
              0x800003]:
        r = Reg(9, 0xFFFF, 2)
        r.adeo(t & 0xFFFFFFFF)
        r.adeo((t >> 32) << 32)
        x = read(r.signa(0), prev)
        o.receptum(x)
        prev = x[6]
    r = Reg(9, 0x0104, 2)
    good = r.signa(0x12345678)
    b1 = bytearray(good); b1[0] = 0
    b2 = bytearray(good); b2[1] = 0x22
    b3 = bytearray(good); b3[16] ^= 0x80
    for w in [bytes(b1), bytes(b2), bytes(b3), good]:
        o.receptum(read(w, 77))


def main(argv):
    args = list(argv[1:])
    if args[:1] == ["--wire"]:
        with open(args[1]) as f:
            g = json.load(f)
        anchors = g["anchors"]
        assert anchors["exampleFrame_full"] == EXAMPLE_FRAME
        assert crc16(b"123456789") == int(anchors["crc_123456789"], 16)
        assert crc16(bytes(15)) == int(anchors["crc_zero15"], 16)
        # And the codec's whole encode basis: every vector's CRC by this table.
        for v in g["encode_basis"]:
            w = bytes.fromhex(v["frame"])
            assert crc16(w[:15]) == struct.unpack(">H", w[15:])[0], v
        args = args[2:]
    o = Out()
    sectio_certa(o)
    sectio_registri(o)
    sectio_lecturae(o)
    if args:
        with open(args[0], "wb") as f:
            f.write(o.b)
    else:
        sys.stdout.buffer.write(o.b)


if __name__ == "__main__":
    main(sys.argv)
