#!/usr/bin/env python3
# prototypes/signaculum_oracle.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# The consumer half, and the oracle examples/signaculum/ is held to byte for
# byte. Reads the fixed-point stream prototypes/signaculum_mesh.py wrote
# ("EXSG": rotation, camera, vertices as integers on the 2^-23 grid; baked
# per-face colours as bytes), rasterises the model at 256x256 with a 1/z
# z-buffer and backface culling, and writes a binary P6 to stdout.
#
# INDEPENDENT in the sense entry 26's oracle is: a second implementation of
# the rasteriser, not shared code with the program -- but deliberately
# MIRRORING the program's f64 operation order, because §9.3's determinism
# law is what turns order agreement into byte agreement. Every arithmetic
# line below names the same operations in the same association as the
# .exsc; change one side's order and the bytes part ways, which is the test
# working as designed. This file is PURE PYTHON on purpose: numpy would
# vectorise the loops and hide the order it evaluated them in.
#
#   python3 prototypes/signaculum_oracle.py > expected.out
#
# Deterministic: no clock, no randomness, one input file, one rounding per
# operation.
# ---------------------------------------------------------------------------
import struct, sys

BIN = 'tests/data/signaculum_mesh.bin'
W, H = 256, 256
BG = (15, 18, 22)          # the 0x0f1216 ground of logo/exsecutor-logo.png

d = open(BIN, 'rb').read()
if d[0:4] != b'EXSG': sys.exit('bad magic')
o = 4
def u32():
    global o; v = struct.unpack_from('<I', d, o)[0]; o += 4; return v
def i32():
    global o; v = struct.unpack_from('<i', d, o)[0]; o += 4; return v
def u16():
    global o; v = struct.unpack_from('<H', d, o)[0]; o += 2; return v

NV = u32(); NF = u32()
# decode: q -> q / 8388608.0. 2^23 is a power of two, so this f64 division
# is EXACT -- the same bits any consumer of the stream holds.
D = i32() / 8388608.0
FF = i32() / 8388608.0
R = [i32() / 8388608.0 for _ in range(9)]
r00, r01, r02, r10, r11, r12, r20, r21, r22 = R

# ---- per-vertex: decode, rotate, project, store screen coords + 1/z ------
# one division per vertex (iz), reused for both screen axes and the depth.
sx = [0.0] * NV; sy = [0.0] * NV; izq = [0.0] * NV
for i in range(NV):
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
    sx[i] = (xw * 0.5 + 0.5) * 255.0
    yw = (py * FF) * iz
    sy[i] = (0.5 - yw * 0.5) * 255.0
    izq[i] = iz

FI = []
COL = []
for _ in range(NF):
    FI.append((u16(), u16(), u16()))
    COL.append((d[o], d[o + 1], d[o + 2])); o += 3
if o != len(d): sys.exit('trailing bytes in stream: %d of %d consumed' % (o, len(d)))

zb = [0.0] * (W * H)
fb = bytearray(W * H * 3)
for k in range(W * H):
    fb[k * 3] = BG[0]; fb[k * 3 + 1] = BG[1]; fb[k * 3 + 2] = BG[2]

covered = 0
for f in range(NF):
    i0, i1, i2 = FI[f]
    x0 = sx[i0]; y0 = sy[i0]; q0 = izq[i0]
    x1 = sx[i1]; y1 = sy[i1]; q1 = izq[i1]
    x2 = sx[i2]; y2 = sy[i2]; q2 = izq[i2]
    cr, cg, cb = COL[f]
    # signed double-area; y-down screen, so FRONT faces are negative (as
    # tools/render-logo.py). One predicate culls backfaces AND the
    # near-degenerate, matching render-logo.py's abs(area) < 1e-12 skip.
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if area >= -1e-12: continue
    # bbox by truncation toward zero (cvttsd2si's rule) + clamp: identical
    # to floor/ceil after clamping for every value in range, and cheaper in
    # a language without floor.
    m = min(x0, x1, x2); bx0 = int(m);  bx0 = 0 if bx0 < 0 else bx0
    m = max(x0, x1, x2); bx1 = int(m) + 1; bx1 = 255 if bx1 > 255 else bx1
    m = min(y0, y1, y2); by0 = int(m);  by0 = 0 if by0 < 0 else by0
    m = max(y0, y1, y2); by1 = int(m) + 1; by1 = 255 if by1 > 255 else by1
    if bx0 > bx1 or by0 > by1: continue
    # q = 1/z is LINEAR in screen space: q = q0 + a*(px-x0) + b*(py-y0).
    # One division per face; the pixel loop pays one add.
    ra = 1.0 / area
    a = ((q1 - q0) * (y2 - y0) - (q2 - q0) * (y1 - y0)) * ra
    b = ((q2 - q0) * (x1 - x0) - (q1 - q0) * (x2 - x0)) * ra
    # edge-function increments per column (the expanded numerator's dx):
    dw0 = y1 - y2; dw1 = y2 - y0; dw2 = y0 - y1
    for yy in range(by0, by1 + 1):
        pyf = yy + 0.5
        pxf = bx0 + 0.5
        w0 = (x1 - pxf) * (y2 - pyf) - (x2 - pxf) * (y1 - pyf)
        w1 = (x2 - pxf) * (y0 - pyf) - (x0 - pxf) * (y2 - pyf)
        w2 = (x0 - pxf) * (y1 - pyf) - (x1 - pxf) * (y0 - pyf)
        tq = q0 + a * (pxf - x0)
        qq = tq + b * (pyf - y0)
        row = yy * W
        for xx in range(bx0, bx1 + 1):
            if w0 <= 0.0 and w1 <= 0.0 and w2 <= 0.0:
                idx = row + xx
                if qq > zb[idx]:
                    zb[idx] = qq
                    fb[idx * 3] = cr; fb[idx * 3 + 1] = cg; fb[idx * 3 + 2] = cb
                    covered += 1
            w0 += dw0; w1 += dw1; w2 += dw2; qq += a

out = sys.stdout.buffer
out.write(b'P6\n256 256\n255\n')
out.write(bytes(fb))
print('oracle: %d covered writes over %d faces' % (covered, NF), file=sys.stderr)
