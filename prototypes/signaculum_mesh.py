#!/usr/bin/env python3
# prototypes/signaculum_mesh.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# Ships the Exsecutor logo's 3D model to a COMPILED EXSECUTOR PROGRAM, as a
# fixed-point binary stream on stdin. This is the producer half;
# prototypes/signaculum_oracle.py is the consumer held against
# examples/signaculum/ byte for byte.
#
# VERIFICATION-ONLY, like tools/render-logo.py (whose OBJ parse, normalise
# and camera this file deliberately shares): run with the system python3
# (numpy + PIL), never on the build path. Deterministic: no clock, no
# randomness, no dict-order dependence; twice in, identical bytes out. logo/
# is read, never written.
#
#   python3 prototypes/signaculum_mesh.py   # writes tests/data/signaculum_mesh.bin
#
# THE FORMAT ("EXSG"), all integers little-endian, and the one fact that makes
# the byte-identity claim possible: EVERY float the program will ever hold is
# delivered here as an integer. The program decodes q -> q / 8388608.0 (2^23,
# a power of two, so the f64 division is EXACT on both sides), and from then
# on oracle and program run the same f64 operations on the same f64 bits.
# Nothing that needs a transcendental (the rotation matrix) or the texture
# (per-face baked colour) is computed on the Exsecutor side at all.
#
#   4B    magic "EXSG"
#   u32   nv, u32 nf
#   i32   camera distance d, scale 2^-23   (3.2, rendered slightly > 3.2)
#   i32   camera focal f,   scale 2^-23    (2.0 exactly)
#   9 x i32  rotation matrix R = Rx(pitch) @ Ry(yaw), row-major, scale 2^-23
#   nv x 3 x i32  vertices, centred and unit-scaled (tools/render-logo.py's
#                 normalisation), scale 2^-23
#   nf x (3 x u16 + 3 x u8)  per face, INTERLEAVED so the consumer can read
#                 a face and rasterise it in one pass: vertex indices
#                 (0-based), then the baked flat colour -- the texture
#                 sampled at the face's centroid UV, times render-logo.py's
#                 Lambert term, plus its rim term -- all computed HERE (face
#                 normals need a square root, which the target language
#                 pointedly lacks).
# ---------------------------------------------------------------------------
import math, struct, sys
import numpy as np
from PIL import Image

OBJ = 'logo/Meshy_AI_Crossed_Ascension_0910024256_texture.obj'
TEX = 'logo/Meshy_AI_Crossed_Ascension_0910024256_texture.png'
OUT = 'tests/data/signaculum_mesh.bin'

SCALE = 1 << 23          # 2^23: every fixed-point field's denominator
YAW, PITCH = 0.16, -0.14 # the hero view, as tools/render-logo.py's PNG row
CAM_D, CAM_F = 3.2, 2.0  # render-logo.py's render(): d=3.2, f=2.0

def q23(x):
    # round-to-nearest-even on the 2^-23 grid, then clamp to i32. Python's
    # round() is banker's -- deterministic, and the grid is coarse enough
    # that no vertex moves a pixel.
    q = int(round(x * SCALE))
    if q < -2147483648 or q > 2147483647:
        sys.exit('fixed-point overflow: %r' % (x,))
    return q

V = []; VT = []; F = []
for ln in open(OBJ):
    p = ln.split()
    if not p: continue
    if p[0] == 'v': V.append([float(x) for x in p[1:4]])
    elif p[0] == 'vt': VT.append([float(x) for x in p[1:3]])
    elif p[0] == 'f':
        idx = []
        for tok in p[1:]:
            a = (tok.split('/') + ['', ''])[:3]
            idx.append((int(a[0]) - 1, int(a[1]) - 1 if a[1] else -1))
        # the model is already triangulated (every f row has 4 fields), so
        # this fan is a no-op; keep it anyway so a quad mesh would not
        # silently render half its surface.
        for i in range(1, len(idx) - 1): F.append([idx[0], idx[i], idx[i + 1]])
V = np.array(V, dtype=np.float64); VT = np.array(VT, dtype=np.float64)

c = (V.max(0) + V.min(0)) / 2; V = V - c
V = V / np.abs(V).max()

tex = np.asarray(Image.open(TEX).convert('RGB'), dtype=np.float64) / 255.0
TH, TW = tex.shape[:2]

cy, sy = math.cos(YAW), math.sin(YAW); cp, sp = math.cos(PITCH), math.sin(PITCH)
Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
R = Rx @ Ry                      # full precision, for the shading bake
P = V @ R.T                      # rotated positions, for face normals

L = np.array([-0.35, 0.55, 0.75]); L = L / np.linalg.norm(L)
RIMCOL = np.array([0.45, 0.62, 0.85])

colours = np.zeros((len(F), 3), dtype=np.uint8)
for fi, tri in enumerate(F):
    vi = [t[0] for t in tri]; ti = [t[1] for t in tri]
    p = P[vi]
    n = np.cross(p[1] - p[0], p[2] - p[0])
    nl = np.linalg.norm(n)
    n = n / nl if nl > 0 else np.array([0.0, 0.0, 1.0])
    lam = float(np.clip(n @ L, 0, 1))
    rim = float(np.clip(1.0 - abs(n[2]), 0, 1)) ** 2.5
    if ti[0] >= 0:
        uv = VT[ti].mean(0)
        u = int(np.clip((uv[0] % 1.0) * (TW - 1), 0, TW - 1))
        vv = int(np.clip(((1.0 - uv[1]) % 1.0) * (TH - 1), 0, TH - 1))
        base = tex[vv, u]
    else:
        base = np.full(3, 0.7)
    shade = (0.30 + 0.78 * lam) * base + (rim * 0.42) * RIMCOL
    colours[fi] = (np.clip(shade, 0, 1) * 255).astype(np.uint8)

# The QUANTISED matrix is what oracle and program both hold; the bake above
# deliberately uses the full-precision one, since colour is data, not
# arithmetic input.
Rq = [q23(R[r, k]) for r in range(3) for k in range(3)]

buf = bytearray()
buf += b'EXSG'
buf += struct.pack('<II', len(V), len(F))
buf += struct.pack('<ii', q23(CAM_D), q23(CAM_F))
buf += struct.pack('<9i', *Rq)
for v in V: buf += struct.pack('<3i', q23(v[0]), q23(v[1]), q23(v[2]))
for tri, col in zip(F, colours):
    # interleaved per face: the consumer reads a face and rasterises it in
    # one pass, no face/colour arrays resident at once
    buf += struct.pack('<3H3B', tri[0][0], tri[1][0], tri[2][0],
                       int(col[0]), int(col[1]), int(col[2]))

open(OUT, 'wb').write(bytes(buf))
print('wrote %s: %d bytes, %d vertices, %d faces'
      % (OUT, len(buf), len(V), len(F)))
print('decode check: d = %.9f, f = %.9f'
      % (struct.unpack('<i', struct.pack('<i', q23(CAM_D)))[0] / SCALE,
         struct.unpack('<i', struct.pack('<i', q23(CAM_F)))[0] / SCALE))
