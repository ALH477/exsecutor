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
#   somnium_oracle.py png ID PRAETERMITTE SEMEN out.png [ZOOM]
#       one frame as a PNG (zlib + struct only), for eyes, not for tests;
#       ZOOM upscales nearest-neighbour, as the viewer does
#   somnium_oracle.py apng ID PRAETERMITTE TABULAE SEMEN out.png [ZOOM] [FPS]
#       the frames as an animated PNG -- docs/images/somnium_*.png
#
# The request (docs/design/somnium.md section 3):
#   4 bytes  "SOM1"
#   1 byte   somnium: 0 plasma, 1 ignis, 2 vita, 3 pluvia, 4 stellae,
#            5 cuniculus, 6 abyssus, 7 titulus, 8 signum (followed by the
#            44,801-byte EXSG model tests/data/signaculum_mesh.bin),
#            9 fulmen, 10 cruor
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


# 8192 // (z + 1) for z in 0..256: the starfield's perspective divide as a
# table, because the language has no integer division. Frozen like SINUS.
RECIPROCA = [
    8192, 4096, 2730, 2048, 1638, 1365, 1170, 1024, 910, 819, 744, 682, 630, 585, 546, 512,
    481, 455, 431, 409, 390, 372, 356, 341, 327, 315, 303, 292, 282, 273, 264, 256,
    248, 240, 234, 227, 221, 215, 210, 204, 199, 195, 190, 186, 182, 178, 174, 170,
    167, 163, 160, 157, 154, 151, 148, 146, 143, 141, 138, 136, 134, 132, 130, 128,
    126, 124, 122, 120, 118, 117, 115, 113, 112, 110, 109, 107, 106, 105, 103, 102,
    101, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85,
    84, 83, 82, 81, 81, 80, 79, 78, 78, 77, 76, 75, 75, 74, 73, 73,
    72, 71, 71, 70, 70, 69, 68, 68, 67, 67, 66, 66, 65, 65, 64, 64,
    63, 63, 62, 62, 61, 61, 60, 60, 59, 59, 58, 58, 58, 57, 57, 56,
    56, 56, 55, 55, 54, 54, 54, 53, 53, 53, 52, 52, 52, 51, 51, 51,
    50, 50, 50, 49, 49, 49, 49, 48, 48, 48, 47, 47, 47, 47, 46, 46,
    46, 46, 45, 45, 45, 45, 44, 44, 44, 44, 43, 43, 43, 43, 42, 42,
    42, 42, 42, 41, 41, 41, 41, 40, 40, 40, 40, 40, 39, 39, 39, 39,
    39, 39, 38, 38, 38, 38, 38, 37, 37, 37, 37, 37, 37, 36, 36, 36,
    36, 36, 36, 35, 35, 35, 35, 35, 35, 35, 34, 34, 34, 34, 34, 34,
    33, 33, 33, 33, 33, 33, 33, 33, 32, 32, 32, 32, 32, 32, 32, 32,
]

# round(65536 * tan(a * pi / 128)) for a in 0..33: one octant of the
# tunnel's angle, searched by multiplication instead of an arctangent.
TANGENS = [
    0, 1609, 3220, 4834, 6455, 8083, 9721, 11372,
    13036, 14717, 16416, 18136, 19880, 21650, 23449, 25280,
    27146, 29050, 30996, 32988, 35030, 37126, 39281, 41500,
    43790, 46156, 48605, 51145, 53784, 56532, 59398, 62395,
    65536,
]

def tabula_congruit():
    import math
    for i in range(256):
        v = int(math.floor(127.5 + 127.5 * math.sin(2 * math.pi * i / 256) + 0.5))
        if v != SINUS[i]:
            return False
    for i in range(256):
        if RECIPROCA[i] != 8192 // (i + 1):
            return False
    for a in range(33):
        if TANGENS[a] != int(math.floor(65536 * math.tan(a * math.pi / 128) + 0.5)):
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


# ---- pluvia --------------------------------------------------------------------

PLUVIA_COL = 40            # 4-pixel cells
PLUVIA_LIN = 20            # 5-pixel cells


def pluvia_pinge(fb, caput, velocitas, lux, signum, x, t):
    if t == 0:
        for c in range(PLUVIA_COL):
            x = alea(x)
            caput[c] = x >> 24
            x = alea(x)
            velocitas[c] = 1 + (x >> 29)
    # The drops: a head advances velocitas/8 of a cell a frame, lights its
    # cell and gives it a fresh glyph; below the screen it waits a random
    # 0..31 rows before starting again at the top.
    for c in range(PLUVIA_COL):
        h = caput[c] + velocitas[c]
        linea = h >> 3
        if linea < PLUVIA_LIN:
            j = linea * PLUVIA_COL + c
            lux[j] = 255
            x = alea(x)
            signum[j] = x >> 24
        x = alea(x)
        if linea >= PLUVIA_LIN + (x >> 27):
            h = 0
            x = alea(x)
            velocitas[c] = 1 + (x >> 29)
        caput[c] = h
    # One glyph anywhere changes every frame: the rain shimmers.
    x = alea(x)
    j = ((x >> 22) * 800) >> 10
    x = alea(x)
    signum[j] = x >> 24
    for cy in range(PLUVIA_LIN):
        for cx in range(PLUVIA_COL):
            j = cy * PLUVIA_COL + cx
            l = lux[j]
            p = alea((signum[j] << 10) + j + 1)
            r = l >> 3
            g = l
            b = l >> 2
            if l == 255:
                r = 180
                b = 180
            for gy in range(5):
                for gx in range(4):
                    k = ((cy * 5 + gy) * LATITUDO + cx * 4 + gx) * 3
                    on = 0
                    if gx < 3 and gy < 4 and l > 0:
                        if ((p << (gy * 3 + gx)) & M32) >> 31 == 1:
                            on = 1
                    if on == 1:
                        fb[k] = r
                        fb[k + 1] = g
                        fb[k + 2] = b
                    else:
                        fb[k] = 0
                        fb[k + 1] = 0
                        fb[k + 2] = 0
            lux[j] = (l * 15) >> 4
    return x


# ---- stellae -------------------------------------------------------------------

def stellae_nascitur(sx, sy, sz, i, x, z):
    x = alea(x)
    sx[i] = x >> 24
    x = alea(x)
    sy[i] = x >> 24
    sz[i] = z
    return x


def stellae_pinge(fb, sx, sy, sz, x, t):
    if t == 0:
        for i in range(256):
            x = alea(x)
            z = 16 + (((x >> 24) * 239) >> 8)
            x = stellae_nascitur(sx, sy, sz, i, x, z)
    # Trails: every channel keeps three quarters of itself.
    for k in range(TABULA):
        fb[k] = fb[k] - (fb[k] >> 2)
    for i in range(256):
        z = sz[i]
        if z <= 3:
            x = stellae_nascitur(sx, sy, sz, i, x, 255)
            continue
        z = z - 3
        sz[i] = z
        rx = RECIPROCA[z]
        ox = sx[i]
        oy = sy[i]
        dextra = ox >= 128
        mx = ox - 128 if dextra else 128 - ox
        infra = oy >= 128
        my = oy - 128 if infra else 128 - oy
        px = (mx * rx) >> 8
        py = (my * rx) >> 9
        visibilis = (px < 80 if dextra else px <= 80) and (py < 50 if infra else py <= 50)
        if not visibilis:
            x = stellae_nascitur(sx, sy, sz, i, x, 255)
            continue
        X = 80 + px if dextra else 80 - px
        Y = 50 + py if infra else 50 - py
        b = 255 - z
        c = b + 64
        if c > 255:
            c = 255
        k = (Y * LATITUDO + X) * 3
        fb[k] = b
        fb[k + 1] = b
        fb[k + 2] = c
        if z < 96 and X < LATITUDO - 1:
            fb[k + 3] = b
            fb[k + 4] = b
            fb[k + 5] = c
    return x


# ---- cuniculus -----------------------------------------------------------------

def cuniculus_pinge(fb, angulus, profunditas, t):
    s = SINUS
    if t == 0:
        for y in range(ALTITUDO):
            Y = 2 * y + 1
            infra = Y > ALTITUDO
            ay = Y - ALTITUDO if infra else ALTITUDO - Y
            for x in range(LATITUDO):
                X = 2 * x + 1
                dextra = X > LATITUDO
                ax = X - LATITUDO if dextra else LATITUDO - X
                versa = ay > ax
                p = ax if versa else ay
                q = ay if versa else ax
                a = 0
                for k in range(1, 33):
                    if p * 65536 >= q * TANGENS[k]:
                        a = k
                if versa:
                    a = 64 - a
                if dextra and infra:
                    ang = a
                elif infra:
                    ang = 128 - a
                elif dextra:
                    ang = 256 - a
                else:
                    ang = 128 + a
                r2 = ax * ax + ay * ay
                d = 0
                for bit in (128, 64, 32, 16, 8, 4, 2, 1):
                    c = d + bit
                    if c * c * r2 <= 4194304:
                        d = c
                j = y * LATITUDO + x
                angulus[j] = octo(ang)
                profunditas[j] = d
    for j in range(LATITUDO * ALTITUDO):
        d = profunditas[j]
        u = octo(angulus[j] + t)
        v = octo(d + t * 4)
        # 16 tiles round the tunnel, 16 deep, alternating crimson and navy
        # (the logo's two colours); the XOR of the fine coordinates inside
        # a tile is a grain on top. The tile parity is the low bit of the
        # XOR of the coarse coordinates, taken by the shift-discard idiom.
        tegula = ((((u >> 4) ^ (v >> 4)) << 7) & 0xFF) >> 7
        granum = (((u ^ v) << 4) & 0xFF) >> 4
        umbra = 255 - d
        if tegula == 1:
            r, g, b = 200 + granum * 3, 30 + granum * 2, 50 + granum * 2
        else:
            r, g, b = 20 + granum * 2, 40 + granum * 3, 110 + granum * 6
        k = j * 3
        fb[k] = (r * umbra) >> 8
        fb[k + 1] = (g * umbra) >> 8
        fb[k + 2] = (b * umbra) >> 8


# ---- abyssus -------------------------------------------------------------------

ABYSSUS_CX = -0.743643887037151
ABYSSUS_CY = 0.13182590420533
ABYSSUS_PERIODUS = 720


def abyssus_pinge(fb, ph):
    # Floating point, deliberately: the one somnium that exercises the
    # f64 path (spec 5.4). Every operation below is one IEEE f64 operation
    # under nearest-even, in the order written, which is the order the
    # Exsecutor source writes -- Python floats are the same IEEE doubles,
    # so identical order is identical bits.
    s = SINUS
    scala = 3.0
    for _ in range(ph):
        scala = scala * 0.985
    passus = scala / 160.0
    maximum = 96 + (ph >> 2)
    for y in range(ALTITUDO):
        ci = ABYSSUS_CY + (float(y) - 49.5) * passus
        for x in range(LATITUDO):
            cr = ABYSSUS_CX + (float(x) - 79.5) * passus
            zr = 0.0
            zi = 0.0
            n = 0
            vivus = 1
            while vivus == 1:
                zr2 = zr * zr
                zi2 = zi * zi
                if zr2 + zi2 > 4.0:
                    vivus = 0
                else:
                    zi = 2.0 * zr * zi + ci
                    zr = zr2 - zi2 + cr
                    n = n + 1
                    if n >= maximum:
                        vivus = 0
            k = (y * LATITUDO + x) * 3
            if n >= maximum:
                fb[k] = 0
                fb[k + 1] = 0
                fb[k + 2] = 0
            else:
                # The hue runs with the escape count and drifts with the
                # zoom; the brightness climbs with the count, so the far
                # field is dark and the filaments near the set glow.
                c = octo(n * 7 + ph)
                m = n * 8
                if m > 255:
                    m = 255
                fb[k] = (s[c] * m) >> 8
                fb[k + 1] = (s[octo(c + 85)] * m) >> 8
                fb[k + 2] = (s[octo(c + 170)] * m) >> 8


# ---- the letters ---------------------------------------------------------------

# 5x7, one row per entry, bit 4 the leftmost column. Only the letters the
# three inscriptions use; the Latin is cut in the classical alphabet, so V
# stands for U. Index 0 is the space.
LITTERAE = " ACEGHILMNOPRSTVXY"
FORMAE = [
    0, 0, 0, 0, 0, 0, 0,                              # space
    14, 17, 17, 31, 17, 17, 17,                       # A
    14, 17, 16, 16, 16, 17, 14,                       # C
    31, 16, 16, 30, 16, 16, 31,                       # E
    14, 17, 16, 23, 17, 17, 15,                       # G
    17, 17, 17, 31, 17, 17, 17,                       # H
    14, 4, 4, 4, 4, 4, 14,                            # I
    16, 16, 16, 16, 16, 16, 31,                       # L
    17, 27, 21, 21, 17, 17, 17,                       # M
    17, 25, 21, 19, 17, 17, 17,                       # N
    14, 17, 17, 17, 17, 17, 14,                       # O
    30, 17, 17, 30, 16, 16, 16,                       # P
    30, 17, 17, 30, 20, 18, 17,                       # R
    15, 16, 16, 14, 1, 1, 30,                         # S
    31, 4, 4, 4, 4, 4, 4,                             # T
    17, 17, 17, 17, 17, 10, 4,                        # V
    17, 17, 10, 4, 10, 17, 17,                        # X
    17, 17, 10, 4, 4, 4, 4,                           # Y
]

# The inscriptions as glyph indices into FORMAE (examples/somnium holds the
# same integers; the strings here are only for reading).
NOMEN = [LITTERAE.index(ch) for ch in "OLIGARCHY"]
VERSUS_I = [LITTERAE.index(ch) for ch in "EXSECVTOR PINXIT"]
VERSUS_II = [LITTERAE.index(ch) for ch in "PVNCTIM CECINIT"]


def littera_lucet(g, r, c):
    """Is pixel (row r, column c) of glyph g lit? Bit (4 - c) of the row."""
    v = FORMAE[g * 7 + r]
    return ((v << (3 + c)) & 0xFF) >> 7 == 1


def pinge_punctum(fb, x, y, r, g, b):
    if x < LATITUDO and y < ALTITUDO:
        k = (y * LATITUDO + x) * 3
        fb[k] = r
        fb[k + 1] = g
        fb[k + 2] = b


def scribe_versum(fb, versus, x0, y0, visibiles, r, g, b):
    """One line at scale 1, the first `visibiles` glyphs of it."""
    for n in range(min(visibiles, len(versus))):
        for rr in range(7):
            for cc in range(5):
                if littera_lucet(versus[n], rr, cc):
                    pinge_punctum(fb, x0 + n * 6 + cc, y0 + rr, r, g, b)


# ---- titulus -------------------------------------------------------------------

TITULUS_PERIODUS = 400


def titulus_pinge(fb, ph, semen):
    s = SINUS
    # Trails: three quarters of the last frame survives.
    for k in range(TABULA):
        fb[k] = fb[k] - (fb[k] >> 2)
    # OLIGARCHY at scale 3: 15x21 glyphs, advance 17, 151 wide, from x = 4.
    i = 0
    for n in range(9):
        g = NOMEN[n]
        for rr in range(7):
            for cc in range(5):
                if littera_lucet(g, rr, cc):
                    tx = 4 + n * 17 + cc * 3
                    ty = 20 + rr * 3
                    # Each block's own start and end, from the seed alone.
                    # xorshift's first steps from neighbouring seeds are
                    # correlated in the high bits, so four rounds of mixing
                    # before the first draw is used.
                    x = alea(semen ^ ((i + 1) << 20) ^ (i + 1))
                    x = alea(x)
                    x = alea(x)
                    x = alea(x)
                    x = alea(x)
                    sx = ((x >> 24) * 158) >> 8
                    sy = (octo(x >> 16) * 98) >> 8
                    x = alea(x)
                    ex = ((x >> 24) * 158) >> 8
                    ey = (octo(x >> 16) * 98) >> 8
                    i = i + 1
                    lucet = 1
                    if ph < 40:
                        # the rain: each block drifts down its own column
                        px = sx
                        py = octo(sy + ph) if octo(sy + ph) < 98 else octo(sy + ph) - 98
                        lr, lg, lb = 40, 60 + ph * 4, 40
                    elif ph < 104:
                        k = ph - 40
                        e = 64 - (((64 - k) * (64 - k)) >> 6)
                        px = (sx * (64 - e) + tx * e) >> 6
                        py = (sy * (64 - e) + ty * e) >> 6
                        lr, lg, lb = 40 + k * 3, 220, 40 + k
                    elif ph < 330:
                        px = tx
                        py = ty
                        if ph < 108:
                            lr, lg, lb = 255, 255, 255
                        else:
                            w = s[octo(tx * 3 + ph * 5)]
                            lr = 150 + (w * 105 >> 8)
                            lg = 20 + (w * 150 >> 8)
                            lb = 30 + ((255 - w) * 40 >> 8)
                    elif ph < 394:
                        k = ph - 330
                        e = (k * k) >> 6
                        px = (tx * (64 - e) + ex * e) >> 6
                        py = (ty * (64 - e) + ey * e) >> 6
                        v = (63 - k) * 4
                        lr, lg, lb = v, v >> 2, v >> 1
                    else:
                        lucet = 0
                    if lucet == 1:
                        for by in range(3):
                            for bx in range(3):
                                pinge_punctum(fb, px + bx, py + by, lr, lg, lb)
    # The inscriptions, typed in: four frames a letter.
    if 120 <= ph < 394:
        v = 255 if ph < 330 else (393 - ph) * 4
        scribe_versum(fb, VERSUS_I, 32, 58, (ph - 120) >> 2, (v * 150) >> 8, (v * 180) >> 8, v)
    if 190 <= ph < 394:
        v = 255 if ph < 330 else (393 - ph) * 4
        scribe_versum(fb, VERSUS_II, 35, 70, (ph - 190) >> 2, v, (v * 200) >> 8, (v * 120) >> 8)


# ---- signum: the 3D engine -----------------------------------------------------

SIGNUM_LONGITUDO = 44801


def signaculum_pingue(exsg):
    """examples/signaculum/forma.exsc's signaculum_pingue, as a function.

    The arithmetic is prototypes/signaculum_oracle.py's, operation for
    operation, with the numpy lanes written out as scalars: lane l of a
    whole-acy add is one f64 add, so the bits are the same. Returns the
    512x512 rgb24 framebuffer, or None for a stream the engine refuses."""
    if len(exsg) != SIGNUM_LONGITUDO or exsg[0:4] != b"EXSG":
        return None
    o = [4]

    def u32():
        v = int.from_bytes(exsg[o[0]:o[0] + 4], "little")
        o[0] += 4
        return v

    def i32():
        v = int.from_bytes(exsg[o[0]:o[0] + 4], "little", signed=True)
        o[0] += 4
        return v

    def u16():
        v = int.from_bytes(exsg[o[0]:o[0] + 2], "little")
        o[0] += 2
        return v

    nv = u32()
    nf = u32()
    if nv != 1493 or nf != 2981:
        return None
    D = i32() / 8388608.0
    FF = i32() / 8388608.0
    r00, r01, r02, r10, r11, r12, r20, r21, r22 = [i32() / 8388608.0 for _ in range(9)]
    sxv = [0.0] * nv
    syv = [0.0] * nv
    izq = [0.0] * nv
    for i in range(nv):
        fx = i32() / 8388608.0
        fy = i32() / 8388608.0
        fz = i32() / 8388608.0
        t0 = r00 * fx + r01 * fy
        px = t0 + r02 * fz
        t1 = r10 * fx + r11 * fy
        py = t1 + r12 * fz
        t2 = r20 * fx + r21 * fy
        pz = t2 + r22 * fz
        zv = pz + D
        iz = 1.0 / zv
        xw = (px * FF) * iz
        sxv[i] = (xw * 0.5 + 0.5) * 511.0
        yw = (py * FF) * iz
        syv[i] = (0.5 - yw * 0.5) * 511.0
        izq[i] = iz
    W = 512
    zb = [0.0] * (W * W)
    fb = bytearray(b"\x0f\x12\x16" * (W * W))
    lad = [0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0]
    for _ in range(nf):
        i0 = u16()
        i1 = u16()
        i2 = u16()
        cr = exsg[o[0]]
        cg = exsg[o[0] + 1]
        cb = exsg[o[0] + 2]
        o[0] += 3
        x0 = sxv[i0]; y0 = syv[i0]; q0 = izq[i0]
        x1 = sxv[i1]; y1 = syv[i1]; q1 = izq[i1]
        x2 = sxv[i2]; y2 = syv[i2]; q2 = izq[i2]
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        if area >= -1e-12:
            continue
        bx0 = int(min(x0, x1, x2)); bx0 = 0 if bx0 < 0 else bx0
        bx1 = int(max(x0, x1, x2)) + 1; bx1 = 511 if bx1 > 511 else bx1
        by0 = int(min(y0, y1, y2)); by0 = 0 if by0 < 0 else by0
        by1 = int(max(y0, y1, y2)) + 1; by1 = 511 if by1 > 511 else by1
        if bx0 > bx1 or by0 > by1:
            continue
        ra = 1.0 / area
        a = ((q1 - q0) * (y2 - y0) - (q2 - q0) * (y1 - y0)) * ra
        b = ((q2 - q0) * (x1 - x0) - (q1 - q0) * (x2 - x0)) * ra
        dw0 = y1 - y2
        dw1 = y2 - y0
        dw2 = y0 - y1
        off0 = [l * dw0 for l in lad]
        off1 = [l * dw1 for l in lad]
        off2 = [l * dw2 for l in lad]
        offq = [l * a for l in lad]
        g0 = dw0 * 8.0
        g1 = dw1 * 8.0
        g2 = dw2 * 8.0
        ga = a * 8.0
        for yy in range(by0, by1 + 1):
            pyf = yy + 0.5
            pxf = bx0 + 0.5
            w0s = (x1 - pxf) * (y2 - pyf) - (x2 - pxf) * (y1 - pyf)
            w1s = (x2 - pxf) * (y0 - pyf) - (x0 - pxf) * (y2 - pyf)
            w2s = (x0 - pxf) * (y1 - pyf) - (x1 - pxf) * (y0 - pyf)
            tq = q0 + a * (pxf - x0)
            qds = tq + b * (pyf - y0)
            w0v = [w0s + v for v in off0]
            w1v = [w1s + v for v in off1]
            w2v = [w2s + v for v in off2]
            qdv = [qds + v for v in offq]
            row = yy * W
            base = bx0
            while base <= bx1:
                for l in range(8):
                    xxl = base + l
                    if xxl <= bx1:
                        if w0v[l] <= 0.0 and w1v[l] <= 0.0 and w2v[l] <= 0.0:
                            idx = row + xxl
                            if qdv[l] > zb[idx]:
                                zb[idx] = qdv[l]
                                fb[idx * 3] = cr
                                fb[idx * 3 + 1] = cg
                                fb[idx * 3 + 2] = cb
                w0v = [v + g0 for v in w0v]
                w1v = [v + g1 for v in w1v]
                w2v = [v + g2 for v in w2v]
                qdv = [v + ga for v in qdv]
                base = base + 8
    if o[0] != SIGNUM_LONGITUDO:
        return None
    return fb


def scribe_i32(buf, pos, q):
    """q (an int in i32 range) into buf[pos:pos+4], little-endian."""
    u = q + 4294967296 if q < 0 else q
    for n in range(4):
        buf[pos + n] = (u >> (8 * n)) & 0xFF


def signum_pinge(fb, sx, sy, sz, exsg, x, t, ph):
    """The Exsecutor logo, turning, over the starfield, with its captions.

    The engine is used unmodified: each frame rewrites the stream's rotation
    matrix to R = Rx(pitch) @ Ry(yaw) for this frame's yaw and hands the
    whole stream to signaculum_pingue. The pitch terms cos p = R11 and
    sin p = R21 are read back out of the stream itself, so the model keeps
    the tilt it was baked with; the yaw's sine and cosine come off SINUS."""
    s = SINUS
    x = stellae_pinge(fb, sx, sy, sz, x, t)
    q = octo(ph + 7)
    sn = (float(s[q]) - 127.5) / 127.5
    cs = (float(s[octo(q + 64)]) - 127.5) / 127.5
    cp = int.from_bytes(exsg[36:40], "little", signed=True) / 8388608.0
    sp = int.from_bytes(exsg[48:52], "little", signed=True) / 8388608.0
    m = [cs, 0.0, sn, sp * sn, cp, 0.0 - sp * cs, 0.0 - cp * sn, sp, cp * cs]
    for n in range(9):
        v = m[n]
        neg = v < 0.0
        mag = int((0.0 - v if neg else v) * 8388608.0)
        scribe_i32(exsg, 20 + n * 4, 0 - mag if neg else mag)
    big = signaculum_pingue(bytes(exsg))
    # The model fills the middle of the engine's 512x512 frame; rows 80..400,
    # square becomes an 80x80 box at (40, 2), each output pixel the mean of
    # the 4x4 engine pixels under it -- an exact box filter, a shift and no
    # division. A sample in the engine's ground colour takes the star behind
    # it instead, so the logo sits in the sky rather than on a square.
    for y in range(80):
        sy0 = 80 + y * 4
        for xx in range(80):
            sx0 = 96 + xx * 4
            k = ((y + 2) * LATITUDO + xx + 40) * 3
            br, bg, bb = fb[k], fb[k + 1], fb[k + 2]
            ar = ag = ab = 0
            for jy in range(4):
                for jx in range(4):
                    kk = ((sy0 + jy) * 512 + sx0 + jx) * 3
                    cr, cg, cb = big[kk], big[kk + 1], big[kk + 2]
                    if cr == 15 and cg == 18 and cb == 22:
                        cr, cg, cb = br, bg, bb
                    ar += cr
                    ag += cg
                    ab += cb
            fb[k] = ar >> 4
            fb[k + 1] = ag >> 4
            fb[k + 2] = ab >> 4
    # Captions under it: the name, then the two inscriptions in turn.
    scribe_versum(fb, NOMEN, 53, 84, 9, 220, 40, 60)
    if octo(ph) < 128:
        scribe_versum(fb, VERSUS_I, 32, 93, 16, 150, 180, 255)
    else:
        scribe_versum(fb, VERSUS_II, 35, 93, 15, 255, 200, 120)
    return x


# ---- fulmen ------------------------------------------------------------------

def fulmen_pinge(fb, t, semen):
    s = SINUS
    pa = octo(semen)
    # t>>2 holds a pose for 4 frames at 20 fps: under 3 large flashes/s, and
    # there is no full-frame sheet lightning. Photosensitive-safe on purpose.
    tardus = t >> 2
    radius = 8
    for y in range(ALTITUDO):
        for x in range(LATITUDO):
            k = (y * LATITUDO + x) * 3
            caelum = s[octo(y + tardus + pa)] >> 5
            fb[k] = caelum >> 1
            fb[k + 1] = 0
            fb[k + 2] = octo(caelum + 4)
    for b in range(3):
        xg = semen
        xg = alea(xg ^ ((tardus * (b + 3) + pa) & M32))
        bx = octo(xg >> 24)
        if bx < 8:
            bx = 8
        if bx > 151:
            bx = 151
        ramus = 28 + (octo(xg >> 16) >> 2)
        if ramus > 80:
            ramus = 80
        xf = 1
        rx = bx
        for y in range(ALTITUDO):
            xg = alea(xg)
            d = xg >> 29
            if d > 3:
                bx = bx + (d - 3)
            if d < 4:
                recede = 3 - d
                if bx > recede:
                    bx = bx - recede
            if bx > 159:
                bx = 159
            if bx < 1:
                bx = 1
            if y == ramus:
                xf = alea(xg ^ 0xA53A5A5A)
                rx = bx
            if y > ramus:
                xf = alea(xf)
                df = xf >> 29
                if df > 3:
                    rx = rx + (df - 3) + 1
                if df < 4:
                    recede = 4 - df
                    if rx > recede:
                        rx = rx - recede
                if rx > 159:
                    rx = 159
                if rx < 1:
                    rx = 1
            for p in range(2):
                cx = bx
                pingere = 1
                if p == 1:
                    pingere = 0
                    if y >= ramus:
                        pingere = 1
                        cx = rx
                if pingere == 1:
                    for x in range(LATITUDO):
                        dist = distantia(x, cx)
                        if dist < radius:
                            k = (y * LATITUDO + x) * 3
                            if dist == 0:
                                fb[k] = 255
                                fb[k + 1] = 255
                                fb[k + 2] = 255
                            if dist == 1:
                                fb[k] = 255
                                fb[k + 1] = 230
                                fb[k + 2] = 140
                            if dist > 1:
                                additamentum = (radius - dist) * 28
                                r = fb[k] + additamentum
                                g = fb[k + 1] + (additamentum >> 1)
                                fb[k] = 255 if r > 255 else r
                                fb[k + 1] = 255 if g > 255 else g
            if y >= 92:
                for x in range(LATITUDO):
                    dist = distantia(x, bx)
                    if dist < 14:
                        k = (y * LATITUDO + x) * 3
                        additamentum = (14 - dist) * 12
                        r = fb[k] + additamentum
                        g = fb[k + 1] + (additamentum >> 2)
                        fb[k] = 255 if r > 255 else r
                        fb[k + 1] = 255 if g > 255 else g
    return semen


# ---- cruor -------------------------------------------------------------------

def cruor_pinge(fb, t, semen):
    s = SINUS
    pa = octo(semen)
    pb = octo(semen >> 8)
    for y in range(ALTITUDO):
        for x in range(LATITUDO):
            k = (y * LATITUDO + x) * 3
            caput = s[octo(x * 3 + pa + t)] >> 2
            longitudo = 20 + (s[octo(x + pb)] >> 4)
            if y >= 90:
                h = s[octo(x + t + y)] >> 2
                fb[k] = octo(80 + (h >> 1))
                fb[k + 1] = 0
                fb[k + 2] = h >> 3
            if y < 90:
                r, g, b = 4, 0, 2
                if y >= caput:
                    along = y - caput
                    if along < longitudo:
                        cadit = 255 - along * 6
                        if cadit < 40:
                            cadit = 40
                        r = cadit
                        g = along >> 2
                        b = 0
                fb[k] = r
                fb[k + 1] = g
                fb[k + 2] = b
    return semen


# ---- the engine ----------------------------------------------------------------

MODELUM = "tests/data/signaculum_mesh.bin"


def petitio(somnium, praetermitte, tabulae, semen):
    r = b"SOM1" + struct.pack("<BIII", somnium, praetermitte, tabulae, semen)
    if somnium == 8:
        r += open(MODELUM, "rb").read()
    return r


def machina(req, out):
    """Mirror of examples/somnium/machina.exsc: returns the exit status."""
    if len(req) < 17 or req[:4] != b"SOM1":
        return 1
    somnium, praetermitte, tabulae, semen = struct.unpack("<BIII", req[4:17])
    if somnium > 10:
        return 1
    # Somnium 8 carries the 3D engine's EXSG model after the request;
    # every other somnium's request is exactly 17 bytes.
    longitudo = 17 + SIGNUM_LONGITUDO if somnium == 8 else 17
    if len(req) != longitudo:
        return 1
    exsg = bytearray(req[17:])
    if somnium == 8:
        # magic and the two counts, as signaculum_pingue checks them; the
        # rest of the stream is the engine's to refuse.
        if exsg[0:4] != b"EXSG" or exsg[4:12] != struct.pack("<II", 1493, 2981):
            return 1
    if semen == 0:
        semen = 0x9E3779B9
    fb = bytearray(TABULA)
    calor = [0] * (LATITUDO * (ALTITUDO + 2))
    va = [0] * (LATITUDO * ALTITUDO)
    vb = [0] * (LATITUDO * ALTITUDO)
    caput = [0] * 40
    velocitas = [0] * 40
    lux = [0] * 800
    signum = [0] * 800
    sx = [0] * 256
    sy = [0] * 256
    sz = [0] * 256
    x = semen
    phasis = 0
    for f in range(praetermitte + tabulae):
        if somnium == 0:
            if f >= praetermitte:
                plasma_pinge(fb, f, semen)
        if somnium == 1:
            x = ignis_pinge(fb, calor, x)
        if somnium == 2:
            x = vita_pinge(fb, va, vb, x, f)
        if somnium == 3:
            x = pluvia_pinge(fb, caput, velocitas, lux, signum, x, f)
        if somnium == 4:
            x = stellae_pinge(fb, sx, sy, sz, x, f)
        if somnium == 5:
            # va/vb double as the tunnel's two tables: one somnium runs per
            # request, so the storage is never shared.
            cuniculus_pinge(fb, va, vb, f)
        if somnium == 6:
            if f >= praetermitte:
                abyssus_pinge(fb, phasis)
            phasis = phasis + 1
            if phasis >= ABYSSUS_PERIODUS:
                phasis = 0
        if somnium == 7:
            if f >= praetermitte:
                titulus_pinge(fb, phasis, semen)
            phasis = phasis + 1
            if phasis >= TITULUS_PERIODUS:
                phasis = 0
        if somnium == 8:
            if f >= praetermitte:
                x = signum_pinge(fb, sx, sy, sz, exsg, x, f, phasis)
            else:
                x = stellae_pinge(fb, sx, sy, sz, x, f)
            phasis = octo(phasis + 1)
        if somnium == 9:
            if f >= praetermitte:
                fulmen_pinge(fb, f, semen)
        if somnium == 10:
            if f >= praetermitte:
                cruor_pinge(fb, f, semen)
        if f >= praetermitte:
            out.write(bytes(fb))
    return 0

def _chunk(kind, data):
    c = struct.pack(">I", len(data)) + kind + data
    return c + struct.pack(">I", zlib.crc32(kind + data) & M32)


def amplia(fb, w, h, z):
    """Nearest-neighbour upscale by z -- what mpv --scale=nearest shows."""
    if z == 1:
        return bytes(fb)
    out = bytearray()
    for y in range(h):
        row = bytearray()
        for x in range(w):
            row += bytes(fb[(y * w + x) * 3:(y * w + x) * 3 + 3]) * z
        out += bytes(row) * z
    return bytes(out)


def _idat(fb, w, h):
    return zlib.compress(b"".join(b"\x00" + fb[y * w * 3:(y + 1) * w * 3] for y in range(h)), 9)


def png(path, fb, w, h):
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(_chunk(b"IDAT", _idat(bytes(fb), w, h)))
        f.write(_chunk(b"IEND", b""))


def apng(path, frames, w, h, fps):
    """Animated PNG (browsers and GitHub play it; others show frame 0)."""
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(_chunk(b"acTL", struct.pack(">II", len(frames), 0)))
        seq = 0
        for i, fr in enumerate(frames):
            f.write(_chunk(b"fcTL", struct.pack(">IIIIIHHBB", seq, w, h, 0, 0, 1, fps, 0, 0)))
            seq += 1
            data = _idat(fr, w, h)
            if i == 0:
                f.write(_chunk(b"IDAT", data))
            else:
                f.write(_chunk(b"fdAT", struct.pack(">I", seq) + data))
                seq += 1
        f.write(_chunk(b"IEND", b""))


class _Tabulae:
    def __init__(self):
        self.omnes = []

    def write(self, b):
        self.omnes.append(b)

def main(argv):
    if len(argv) == 6 and argv[1] == "petitio":
        sys.stdout.buffer.write(petitio(*(int(a, 0) for a in argv[2:6])))
        return 0
    if len(argv) in (6, 7) and argv[1] == "png":
        # png ID SKIP SEED out.png [ZOOM]
        somnium, praetermitte, semen = (int(a, 0) for a in argv[2:5])
        z = int(argv[6]) if len(argv) == 7 else 1
        t = _Tabulae()
        rc = machina(petitio(somnium, praetermitte, 1, semen), t)
        if rc == 0:
            png(argv[5], amplia(t.omnes[0], LATITUDO, ALTITUDO, z), LATITUDO * z, ALTITUDO * z)
        return rc
    if len(argv) in (7, 8, 9) and argv[1] == "apng":
        # apng ID SKIP FRAMES SEED out.png [ZOOM] [FPS]
        somnium, praetermitte, tabulae, semen = (int(a, 0) for a in argv[2:6])
        z = int(argv[7]) if len(argv) >= 8 else 1
        fps = int(argv[8]) if len(argv) == 9 else 20
        t = _Tabulae()
        rc = machina(petitio(somnium, praetermitte, tabulae, semen), t)
        if rc == 0:
            apng(argv[6], [amplia(fr, LATITUDO, ALTITUDO, z) for fr in t.omnes],
                 LATITUDO * z, ALTITUDO * z, fps)
        return rc
    if len(argv) == 2 and argv[1] == "selftest":
        ok = tabula_congruit()
        print("sinus, reciproca and tangentes match their formulas:", "yes" if ok else "NO")
        return 0 if ok else 1
    if len(argv) == 1:
        return machina(sys.stdin.buffer.read(), sys.stdout.buffer)
    sys.stderr.write(__doc__ if __doc__ else "see the header of this file\n")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
