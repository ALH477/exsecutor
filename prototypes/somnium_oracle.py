#!/usr/bin/env python3
# prototypes/somnium_oracle.py -- the independent oracle for examples/somnium/.
#
# Never shipped, never on the build closure (spec §18). It recomputes, in
# Python integers, every byte examples/somnium/ writes, mirroring the
# program operation for operation: the same 256-entry sine table (frozen
# below as a literal, NOT recomputed from math.sin at run time -- a libm is
# ambient state), the same xorshift32 draws in the same order, the same
# truncations. Everything is integer arithmetic, so there is no rounding
# mode to agree on: a byte that differs means one side changed its order or
# its formula.
#
# Usage:
#   somnium_oracle.py petitio ID PRAETERMITTE TABULAE SEMEN > request.bin
#       write the 17-byte request the engine reads on stdin
#   somnium_oracle.py < request.bin > frames.rgb
#       render exactly what `somnium` writes for that request; exit status
#       is the engine's (0 written, 1 refused request)
#   somnium_oracle.py png ID PRAETERMITTE SEMEN out.png
#       one frame as a PNG (zlib + struct only), for eyes, not for tests
#
# The request (docs/design/somnium.md section 3):
#   4 bytes  "SOM1"
#   1 byte   somnium: 0 plasma, 1 ignis, 2 vita
#   4 bytes  praetermitte, u32 little-endian: frames rendered, not written
#   4 bytes  tabulae,      u32 little-endian: frames written
#   4 bytes  semen,        u32 little-endian: the only entropy; 0 -> 0x9E3779B9
# and then end of input -- an 18th byte is a refused request, as a short
# one is.

import struct
import sys
import zlib

LATITUDO = 160
ALTITUDO = 100
TABULA = LATITUDO * ALTITUDO * 3          # 48,000 bytes, rgb24, row-major
M32 = 0xFFFFFFFF

# round(127.5 + 127.5 * sin(2*pi*i/256)), frozen. examples/somnium/
# somnium.exsc holds the same 256 integers; tabula_congruit() below is the
# check that the literal is the formula it claims, run by `selftest`.
SINUS = [
    128, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 162, 165, 167, 170, 173,
    176, 179, 182, 185, 188, 190, 193, 196, 198, 201, 203, 206, 208, 211, 213, 215,
    218, 220, 222, 224, 226, 228, 230, 232, 234, 235, 237, 238, 240, 241, 243, 244,
    245, 246, 248, 249, 250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255,
    255, 255, 255, 255, 254, 254, 254, 253, 253, 252, 251, 250, 250, 249, 248, 246,
    245, 244, 243, 241, 240, 238, 237, 235, 234, 232, 230, 228, 226, 224, 222, 220,
    218, 215, 213, 211, 208, 206, 203, 201, 198, 196, 193, 190, 188, 185, 182, 179,
    176, 173, 170, 167, 165, 162, 158, 155, 152, 149, 146, 143, 140, 137, 134, 131,
    128, 124, 121, 118, 115, 112, 109, 106, 103, 100, 97, 93, 90, 88, 85, 82,
    79, 76, 73, 70, 67, 65, 62, 59, 57, 54, 52, 49, 47, 44, 42, 40,
    37, 35, 33, 31, 29, 27, 25, 23, 21, 20, 18, 17, 15, 14, 12, 11,
    10, 9, 7, 6, 5, 5, 4, 3, 2, 2, 1, 1, 1, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 4, 5, 5, 6, 7, 9,
    10, 11, 12, 14, 15, 17, 18, 20, 21, 23, 25, 27, 29, 31, 33, 35,
    37, 40, 42, 44, 47, 49, 52, 54, 57, 59, 62, 65, 67, 70, 73, 76,
    79, 82, 85, 88, 90, 93, 97, 100, 103, 106, 109, 112, 115, 118, 121, 124,
]


def tabula_congruit():
    import math
    for i in range(256):
        v = int(math.floor(127.5 + 127.5 * math.sin(2 * math.pi * i / 256) + 0.5))
        if v != SINUS[i]:
            return False
    return True


def octo(v):
    # `v sicut u8`: the low eight bits (spec §5.4, narrowing is truncation).
    return v & 0xFF


def alea(x):
    # xorshift32 (Marsaglia 2003, 13/17/5). `sursum` discards what passes
    # bit 31 (spec §5.4), which is the & M32; there is no multiply, because
    # `*%` is [OPEN].
    x ^= (x << 13) & M32
    x ^= x >> 17
    x ^= (x << 5) & M32
    return x


def distantia(a, b):
    return a - b if a > b else b - a


# ---- plasma ------------------------------------------------------------------

def plasma_pinge(fb, t, semen):
    s = SINUS
    pa = octo(semen)
    pb = octo(semen >> 8)
    pc = octo(semen >> 16)
    pd = octo(semen >> 24)
    cx = 16 + (s[octo(t + pa)] >> 1)
    cy = 18 + (s[octo(t * 2 + pb + 64)] >> 2)
    for y in range(ALTITUDO):
        v2 = s[octo(y * 4 + t * 2 + pd)]
        dy = distantia(y, cy)
        for x in range(LATITUDO):
            v1 = s[octo(x * 3 + t + pc)]
            v3 = s[octo((x + y) * 2 + t * 3)]
            dx = distantia(x, cx)
            v4 = s[octo(((dx * dx + dy * dy) >> 5) + t * 252)]
            p = octo(((v1 + v2 + v3 + v4) >> 1) + t)
            k = (y * LATITUDO + x) * 3
            fb[k] = s[p]
            fb[k + 1] = s[octo(p + 85)] >> 1
            fb[k + 2] = s[octo(p + 170)]


# ---- ignis -------------------------------------------------------------------

def ignis_pinge(fb, calor, x):
    # Two seed rows under the picture, 100 and 101.
    for i in range(LATITUDO * 2):
        x = alea(x)
        v = x >> 24
        h = 255 if v > 140 else v >> 2
        calor[ALTITUDO * LATITUDO + i] = h
    for y in range(ALTITUDO):
        for i in range(LATITUDO):
            xl = LATITUDO - 1 if i == 0 else i - 1
            xr = 0 if i == LATITUDO - 1 else i + 1
            r1 = (y + 1) * LATITUDO
            r2 = (y + 2) * LATITUDO
            summa = calor[r1 + xl] + calor[r1 + i] + calor[r1 + xr] + calor[r2 + i]
            n = summa >> 2
            if n > 1:
                n = n - 2
            else:
                n = 0
            calor[y * LATITUDO + i] = n
            h3 = n * 3
            r = 255 if h3 > 255 else h3
            g = 0
            if h3 > 255:
                g = h3 - 255
                if g > 255:
                    g = 255
            b = 0
            if h3 > 510:
                b = h3 - 510
            k = (y * LATITUDO + i) * 3
            fb[k] = r
            fb[k + 1] = g
            fb[k + 2] = b
    return x


# ---- vita --------------------------------------------------------------------

def vita_pinge(fb, a, b, x, t):
    n_cells = LATITUDO * ALTITUDO
    if t == 0:
        for i in range(n_cells):
            x = alea(x)
            a[i] = 1 if (x >> 30) == 0 else 0
    # Every eighth frame a 3x3 handful of fresh cells, so the board never
    # settles into still life. `t & 7` is `(t sursum 61) deorsum 61` there.
    if (t & 7) == 0:
        x = alea(x)
        # A byte scaled into range by multiply-and-shift: 0..157 and 0..97,
        # so the 3x3 always fits and every attempt lands.
        px = ((x >> 24) * (LATITUDO - 2)) >> 8
        py = (octo(x >> 16) * (ALTITUDO - 2)) >> 8
        for dy in range(3):
            for dx in range(3):
                x = alea(x)
                if (x >> 31) == 1:
                    j = (py + dy) * LATITUDO + px + dx
                    if a[j] == 0:
                        a[j] = 1
    for y in range(ALTITUDO):
        yu = ALTITUDO - 1 if y == 0 else y - 1
        yd = 0 if y == ALTITUDO - 1 else y + 1
        for i in range(LATITUDO):
            xl = LATITUDO - 1 if i == 0 else i - 1
            xr = 0 if i == LATITUDO - 1 else i + 1
            n = 0
            for rr in (yu, y, yd):
                for cc in (xl, i, xr):
                    if rr == y and cc == i:
                        continue
                    if a[rr * LATITUDO + cc] > 0:
                        n += 1
            c = y * LATITUDO + i
            aetas = a[c]
            nova = 0
            if aetas > 0:
                if n == 2 or n == 3:
                    nova = aetas + 1 if aetas < 255 else 255
            else:
                if n == 3:
                    nova = 1
            b[c] = nova
    for c in range(n_cells):
        a[c] = b[c]
        k = c * 3
        aetas = a[c]
        if aetas == 0:
            fb[k] = fb[k] >> 1
            fb[k + 1] = fb[k + 1] >> 1
            fb[k + 2] = fb[k + 2] >> 1
        else:
            e = 63 if aetas > 63 else aetas
            fb[k] = 255 - e * 3
            fb[k + 1] = 255 - e * 2
            fb[k + 2] = 255
    return x


# ---- the engine ----------------------------------------------------------------

def petitio(somnium, praetermitte, tabulae, semen):
    return b"SOM1" + struct.pack("<BIII", somnium, praetermitte, tabulae, semen)


def machina(req, out):
    """Mirror of examples/somnium/machina.exsc: returns the exit status."""
    if len(req) != 17 or req[:4] != b"SOM1":
        return 1
    somnium, praetermitte, tabulae, semen = struct.unpack("<BIII", req[4:])
    if somnium > 2:
        return 1
    if semen == 0:
        semen = 0x9E3779B9
    fb = bytearray(TABULA)
    calor = [0] * (LATITUDO * (ALTITUDO + 2))
    va = [0] * (LATITUDO * ALTITUDO)
    vb = [0] * (LATITUDO * ALTITUDO)
    x = semen
    for f in range(praetermitte + tabulae):
        if somnium == 0:
            if f >= praetermitte:
                plasma_pinge(fb, f, semen)
        if somnium == 1:
            x = ignis_pinge(fb, calor, x)
        if somnium == 2:
            x = vita_pinge(fb, va, vb, x, f)
        if f >= praetermitte:
            out.write(bytes(fb))
    return 0


def png(path, fb, w, h):
    raw = b"".join(b"\x00" + bytes(fb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & M32)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def main(argv):
    if len(argv) == 6 and argv[1] == "petitio":
        sys.stdout.buffer.write(petitio(*(int(a, 0) for a in argv[2:6])))
        return 0
    if len(argv) == 6 and argv[1] == "png":
        somnium, praetermitte, semen = (int(a, 0) for a in argv[2:5])

        class Ultima:
            data = b""

            def write(self, b):
                self.data = b

        u = Ultima()
        rc = machina(petitio(somnium, praetermitte, 1, semen), u)
        if rc == 0:
            png(argv[5], u.data, LATITUDO, ALTITUDO)
        return rc
    if len(argv) == 2 and argv[1] == "selftest":
        ok = tabula_congruit()
        print("sinus table matches its formula:", "yes" if ok else "NO")
        return 0 if ok else 1
    if len(argv) == 1:
        return machina(sys.stdin.buffer.read(), sys.stdout.buffer)
    sys.stderr.write(__doc__ if __doc__ else "see the header of this file\n")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
