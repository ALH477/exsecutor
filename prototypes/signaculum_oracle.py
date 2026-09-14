#!/usr/bin/env python3
# prototypes/signaculum_oracle.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# The consumer half, and the oracle examples/signaculum/ is held to byte for
# byte. Reads the fixed-point stream prototypes/signaculum_mesh.py wrote
# ("EXSG": rotation, camera, vertices as integers on the 2^-23 grid; baked
# per-face colours as bytes), rasterises the model at 512x512 with a 1/z
# z-buffer and backface culling, and writes a binary P6 to stdout.
#
# STAGE 5.4 PORT. The program's pixel loop is lane-parallel: the four
# loop-carried f64 accumulators (edge functions w0/w1/w2, depth qd) are
# acies<f64, 8>, each 8-pixel-group step ONE whole-acy vadd vf64.8 per
# accumulator, each row's lane seeds ONE broadcast add per accumulator
# against the per-face offsets lad*step. This file mirrors THAT pipeline's
# f64 operation order exactly:
#
#   per face:  lad = [0.0..7.0]
#              off* = lad * broadcast(step)      (one fmul per lane)
#              g*   = broadcast(step * 8.0)      (scalar mul, then broadcast)
#   per row:   w*s, qds computed SCALAR at the row's left edge exactly as
#              the 256x256 chain did
#              w*v  = broadcast(w*s) + off*      (one fadd per lane)
#   per group: scalar per-lane coverage test and z-test/store (the program
#              has no mask primitive; v[l] indexing), then
#              w*v += g*                          (one fadd per lane)
#   per-lane guard xxl <= bx1 masks the partial last group.
#
# The association is the program's: lane l at group g holds
# broadcast(seed) + l*step, advanced in steps of 8*step -- NOT the old
# scalar chain's successive single fadds. Change either side's association
# and the bytes part ways, which is the test working as designed.
#
# numpy float64 ELEMENTWISE is the mirror's packed op: `w0v + dw0g` on two
# float64(8) arrays is one IEEE round-to-nearest-even add per lane, the
# same semantics the program's vadd vf64.8 gives hardware lanes. Every
# scalar f64 the program computes (decode, projection, face setup, row
# seeds) is a plain Python float here, one IEEE rounding per op. The
# per-lane control flow (coverage test, z-test, conditional store) is
# Python scalar code reading vector lanes -- deliberately NOT vectorised,
# because the program's version is scalar per lane and the store order
# would vanish in a mask. Mirrored against numpy 2.3.4 (system python3;
# run.sh never runs this file -- tests/programs/signaculum/expected.out is
# checked in, per the TEST header).
#
#   python3 prototypes/signaculum_oracle.py > expected.out
#
# Deterministic: no clock, no randomness, one input file, one rounding per
# operation per lane.
# ---------------------------------------------------------------------------
import sys

import numpy as np

BIN = 'tests/data/signaculum_mesh.bin'
W, H = 512, 512
BG = (15, 18, 22)          # the 0x0f1216 ground of logo/exsecutor-logo.png

d = open(BIN, 'rb').read()
if d[0:4] != b'EXSG': sys.exit('bad magic')
o = 4
def u32():
    global o; v = int.from_bytes(d[o:o + 4], 'little'); o += 4; return v
def i32():
    global o; v = int.from_bytes(d[o:o + 4], 'little', signed=True); o += 4; return v
def u16():
    global o; v = int.from_bytes(d[o:o + 2], 'little'); o += 2; return v

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
    sx[i] = (xw * 0.5 + 0.5) * 511.0
    yw = (py * FF) * iz
    sy[i] = (0.5 - yw * 0.5) * 511.0
    izq[i] = iz

FI = []
COL = []
for _ in range(NF):
    FI.append((u16(), u16(), u16()))
    COL.append((d[o], d[o + 1], d[o + 2])); o += 3
if o != len(d): sys.exit('trailing bytes in stream: %d of %d consumed' % (o, len(d)))

zb = np.zeros(W * H, dtype=np.float64)
fb = bytearray(W * H * 3)
for k in range(W * H):
    fb[k * 3] = BG[0]; fb[k * 3 + 1] = BG[1]; fb[k * 3 + 2] = BG[2]

# the lane index as f64, exactly the program's [0.0, 1.0, ..., 7.0]
LAD = np.array([0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0], dtype=np.float64)

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
    m = max(x0, x1, x2); bx1 = int(m) + 1; bx1 = 511 if bx1 > 511 else bx1
    m = min(y0, y1, y2); by0 = int(m);  by0 = 0 if by0 < 0 else by0
    m = max(y0, y1, y2); by1 = int(m) + 1; by1 = 511 if by1 > 511 else by1
    if bx0 > bx1 or by0 > by1: continue
    # q = 1/z is LINEAR in screen space: q = q0 + a*(px-x0) + b*(py-y0).
    # One division per face; the pixel loop pays one lane-add per group.
    ra = 1.0 / area
    a = ((q1 - q0) * (y2 - y0) - (q2 - q0) * (y1 - y0)) * ra
    b = ((q2 - q0) * (x1 - x0) - (q1 - q0) * (x2 - x0)) * ra
    # edge-function increments per column (the expanded numerator's dx):
    dw0 = y1 - y2; dw1 = y2 - y0; dw2 = y0 - y1
    # ---- lane vectors for this face, in the program's order ---------------
    # off*: lane l starts step*l ahead of the row seed (one fmul per lane).
    # g*: a group of 8 advances by step*8; the scalar product comes FIRST
    # (the program broadcasts [step*8.0; 8]), then the broadcast.
    off0 = LAD * np.full(8, dw0, dtype=np.float64)
    off1 = LAD * np.full(8, dw1, dtype=np.float64)
    off2 = LAD * np.full(8, dw2, dtype=np.float64)
    offq = LAD * np.full(8, a, dtype=np.float64)
    dw0g = np.full(8, dw0 * 8.0, dtype=np.float64)
    dw1g = np.full(8, dw1 * 8.0, dtype=np.float64)
    dw2g = np.full(8, dw2 * 8.0, dtype=np.float64)
    ag = np.full(8, a * 8.0, dtype=np.float64)
    for yy in range(by0, by1 + 1):
        pyf = yy + 0.5
        pxf = bx0 + 0.5
        # the row seeds, scalar, exactly as the 256x256 chain computed them
        w0s = (x1 - pxf) * (y2 - pyf) - (x2 - pxf) * (y1 - pyf)
        w1s = (x2 - pxf) * (y0 - pyf) - (x0 - pxf) * (y2 - pyf)
        w2s = (x0 - pxf) * (y1 - pyf) - (x1 - pxf) * (y0 - pyf)
        tq = q0 + a * (pxf - x0)
        qds = tq + b * (pyf - y0)
        # lane seeds: broadcast(seed) + lad*step -- one fadd per lane.
        w0v = np.full(8, w0s, dtype=np.float64) + off0
        w1v = np.full(8, w1s, dtype=np.float64) + off1
        w2v = np.full(8, w2s, dtype=np.float64) + off2
        qdv = np.full(8, qds, dtype=np.float64) + offq
        row = yy * W
        base = bx0
        # terminus 64 in the program: a 512-wide row is at most 64 groups.
        while base <= bx1:
            # coverage and the z-test/store stay SCALAR PER LANE, in the
            # program's lane order, with the partial-group guard first.
            for l in range(8):
                xxl = base + l
                if xxl <= bx1:
                    if w0v[l] <= 0.0 and w1v[l] <= 0.0 and w2v[l] <= 0.0:
                        idx = row + xxl
                        if qdv[l] > zb[idx]:
                            zb[idx] = qdv[l]
                            fb[idx * 3] = cr; fb[idx * 3 + 1] = cg; fb[idx * 3 + 2] = cb
                            covered += 1
            # four whole-acy vadd vf64.8 -- one fadd per lane here.
            w0v = w0v + dw0g
            w1v = w1v + dw1g
            w2v = w2v + dw2g
            qdv = qdv + ag
            base = base + 8

out = sys.stdout.buffer
out.write(b'P6\n512 512\n255\n')
out.write(bytes(fb))
print('oracle: %d covered writes over %d faces' % (covered, NF), file=sys.stderr)
