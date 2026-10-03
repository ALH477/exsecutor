#!/usr/bin/env python3
# prototypes/signaculum_mesh.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
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
# THE TEXTURE RESOLVE IS A FOOTPRINT AVERAGE IN LINEAR LIGHT. The engine
# samples no texture -- it stores three baked bytes per face -- so this file
# is the whole of the project's texture processing, and it used to be one
# nearest-neighbour texel per face taken at the centroid UV, with the
# shading arithmetic done on sRGB-encoded values. A face's UV triangle
# covers a median of 306 of the texture's 4,194,304 texels; 305 of them were
# thrown away. Both halves are fixed below: every covered texel is averaged,
# and every arithmetic step happens in linear light with a single re-encode
# at the u8 quantise. Of the three shading constants only the ambient term
# needed correcting -- it is a light intensity that was stated in the wrong
# space; the two gains are dimensionless and are left untouched.
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
#                 AVERAGED over the face's UV footprint, times
#                 render-logo.py's Lambert term, plus its rim term, the
#                 whole of it in linear light -- all computed HERE (face
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

# ---- the texture, decoded to LINEAR LIGHT once ---------------------------
# A PNG's bytes are sRGB-ENCODED. Dividing by 255 yields a number in [0,1],
# not a light intensity, and the previous bake multiplied that encoded value
# by the Lambert term and added the rim term as though it were one. Both
# operations are only meaningful on linear light, and so is the averaging
# below -- the mean of two sRGB code values is not the code value of the
# mean colour. So decode ONCE here, do every arithmetic step in linear, and
# re-encode at the single point where a u8 is produced.
#
# The piecewise IEC 61966-2-1 transfer, not a 2.2 power approximation: the
# 2.2 shortcut is wrong by up to 4/255 in the darks, which is visible on
# this model's large shadowed faces.
def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1.0 / 2.4) - 0.055)

tex = np.asarray(Image.open(TEX).convert('RGB'), dtype=np.float64) / 255.0
TH, TW = tex.shape[:2]
texl = srgb_to_linear(tex)

cy, sy = math.cos(YAW), math.sin(YAW); cp, sp = math.cos(PITCH), math.sin(PITCH)
Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
Rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
R = Rx @ Ry                      # full precision, for the shading bake
P = V @ R.T                      # rotated positions, for face normals

L = np.array([-0.35, 0.55, 0.75]); L = L / np.linalg.norm(L)
RIMCOL = srgb_to_linear(np.array([0.45, 0.62, 0.85]))   # authored in sRGB

# ---- the shading constants: ONE of them was actually wrong -----------------
# 0.30 / 0.78 / 0.42 were tuned by eye against the OLD gamma-space maths, so
# the obvious worry is that correcting the arithmetic invalidates all three.
# It does not. Only the AMBIENT term is a light intensity stated in the
# wrong space: 0.30 as an sRGB code value is 0.0732 of linear light, and
# carrying the encoded 0.30 into linear arithmetic inflates the model's
# floor enormously -- on its own it accounts for nearly all of the +38%
# brightening that re-running the old constants in linear light produces.
#
# The diffuse and rim GAINS are dimensionless multipliers, and they turn out
# to need no change at all. Refitting them by least squares against the
# previous bake's own output -- weighted by each face's VISIBLE pixel count
# averaged over 16 yaws of the turn, since 1572 of the 2981 faces are hidden
# in any single view and weighting them equally would fit the wrong thing --
# lands on 0.80 and 0.43, within 3% of the untouched 0.78 and 0.42. So they
# are left exactly as they were and only ambient moves, to the exact linear
# reading of its own old value.
#
# Measured against the previous bake, weighted by visible pixels over the
# turn (screen luma, and mean per-channel |delta|):
#
#   filter only, still gamma space : luma +10.7%, |d| 11.44
#   gamma only, still one texel    : luma  -9.4%, |d|  5.92
#   both, ambient NOT corrected    : luma +38.1%, |d| 24.34
#   both, ambient corrected        : luma  +0.6%, |d| 14.37   <- shipped
#
# The residual 14.37 IS the improvement: correct filtering and correct
# blending, with the overall tone held where it was (+0.6%).
AMBIENT = float(srgb_to_linear(np.array(0.30)))   # 0.0732, was a bare 0.30
DIFFUSE = 0.78                                    # unchanged
RIMGAIN = 0.42                                    # unchanged

# ---- per face: resolve the texture over the face's REAL UV footprint ------
# The previous bake read ONE texel per face, nearest-neighbour, at the mean
# of the three vertex UVs. The texture is 2048x2048 = 4,194,304 texels and
# the model has 2981 faces, so a face's UV triangle covers a median of 306
# texels and 305 of them were discarded. That is a point sample of a signal
# with far more detail than the sample rate -- aliasing, and it is why the
# baked colours looked noisier than the texture they came from.
#
# Now: average every texel whose CENTRE falls inside the face's UV triangle.
#
# Centre sampling rather than exact clipped-area weighting, decided on a
# measurement and not a guess: exact weighting means clipping a polygon
# against every texel it touches, and at a median footprint of 306 texels
# the weighting error is far below a u8 step. The degenerate case that would
# justify the complexity -- a footprint so small it catches no texel centre
# -- does not occur on this model: the SMALLEST face resolves 4 covered
# texels and NO face reaches the fallback. The fallback is kept anyway,
# because a future model is not this one.
#
# DETERMINISM IS A HARD REQUIREMENT: this output is a checked-in golden, so
# the same input must give the same bytes on any machine, any numpy. The
# accumulation therefore goes through math.fsum, which is exactly rounded
# and so independent of numpy's pairwise-summation block size, SIMD width
# and version. Everything else here is elementwise float64. Nothing threads.
def footprint_mean(ti):
    # UV -> texel space. The v flip is applied ONCE, here. Note the scale is
    # TW/TH, not TW-1/TH-1 as before: a texel's centre is at index + 0.5, so
    # the [0,1] UV range maps onto [0,TW], and scaling by TW-1 shifted every
    # sample by up to half a texel.
    t = VT[ti] * np.array([TW, TH], dtype=np.float64)
    t[:, 1] = TH - t[:, 1]

    x0 = max(int(math.floor(t[:, 0].min())), 0); x1 = min(int(math.ceil(t[:, 0].max())), TW)
    y0 = max(int(math.floor(t[:, 1].min())), 0); y1 = min(int(math.ceil(t[:, 1].max())), TH)
    if x1 > x0 and y1 > y0:
        gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        def edge(a, b):
            return (b[0] - a[0]) * (gy - a[1]) - (b[1] - a[1]) * (gx - a[0])
        w0 = edge(t[0], t[1]); w1 = edge(t[1], t[2]); w2 = edge(t[2], t[0])
        # either winding: the OBJ is not guaranteed consistent in UV space,
        # and a face whose UV triangle is wound the other way is still that
        # face's footprint.
        m = ((w0 >= 0) & (w1 >= 0) & (w2 >= 0)) | ((w0 <= 0) & (w1 <= 0) & (w2 <= 0))
        k = int(m.sum())
        if k:
            sel = texl[y0:y1, x0:x1][m]
            return np.array([math.fsum(sel[:, ch]) / k for ch in range(3)])

    # Fallback: bilinear at the centroid. Unreachable on this model (min
    # coverage 4 texels); present so a denser mesh degrades gracefully
    # instead of dividing by zero.
    cx = float(np.clip(t[:, 0].mean(), 0.0, TW - 1.0))
    cyf = float(np.clip(t[:, 1].mean(), 0.0, TH - 1.0))
    x = int(cx); y = int(cyf); fx = cx - x; fy = cyf - y
    xa = min(x + 1, TW - 1); ya = min(y + 1, TH - 1)
    return ((1 - fx) * (1 - fy) * texl[y, x] + fx * (1 - fy) * texl[y, xa]
            + (1 - fx) * fy * texl[ya, x] + fx * fy * texl[ya, xa])

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
        base = footprint_mean(ti)
    else:
        base = np.full(3, float(srgb_to_linear(np.array(0.7))))
    shade = (AMBIENT + DIFFUSE * lam) * base + (rim * RIMGAIN) * RIMCOL
    colours[fi] = (linear_to_srgb(shade) * 255).astype(np.uint8)

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
