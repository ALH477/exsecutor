#!/usr/bin/env python3
"""The pictura_octonaria oracle: the 960x540 SSAA triangle rendered
independently, in float32, by numpy.

Verification-only, the "not our own bug" check for
tests/programs/pictura_octonaria/ -- the same reason entry 23 vendors an
external certificate and prototypes/pictura_oracle.py (f64) and
prototypes/acies_float8_oracle.py (whole-acy arithmetic) mirror theirs.
It mirrors the ARITHMETIC of examples/pictura/octonaria.exsc exactly,
operation for operation, in IEEE-754 float32 under round-to-nearest-even:
identical order means identical bits, and any difference in the image
means one side changed its order -- an op sequence dropped, lanes
permuted, a chunk of a staging temp not moved, a scalarized fallback
that rounded differently.

THE ALGORITHM (the program's own header lays it out in full):

  * one triangle, the pictura geometry scaled to 960x540: clip-space
    vertices with a positive depth w each, ONE reciprocal per vertex
    (iw = 1/w) and ONE reciprocal for the weights (r = 1/det) -- four
    float divisions in the whole program, all in setup, zero in any
    pixel loop (the census the TEST header greps the emitted fasmg for);
  * 2x2 supersampled COVERAGE: sub-sample lattice at quarter offsets --
    ox, oy in {0.25, 0.75} from the integer pixel center, every offset
    exact in binary. Each sub-sample is an independent yes/no on the
    three edge functions at that point;
  * the three edge accumulators are acies<f32, 8> -- eight pixels of one
    sub-row at a time: initialized per row per sub-sample pass from the
    scalar row seed as [es; 8] + lanes * [dx; 8] (one packed vmul then
    one packed vadd per edge; lanes[k] = ox + k exact), stepped per
    8-pixel group as e = e + [dx * 8; 8] (ONE packed vadd per edge per
    group -- dx * 8.0 is exact, a power-of-two scaling);
  * coverage in the group is SCALAR per lane (there is no mask
    primitive, by design): e0[l] >= 0.0 and e1[l] >= 0.0 and
    e2[l] >= 0.0, one byte store of 1 per covered lane into one row
    buffer per sub-sample;
  * the pixel's coverage weight is al = (s0 + s1 + s2 + s3) * 0.25, in
    EXACTLY that written association (0.25 an exact power of two), s_k
    the four row buffers' bytes as f32;
  * the color is the pictura barycentric per channel,
    (c0*E0 + c1*E1 + c2*E2) * r, evaluated at the INTEGER pixel center
    (the pictura evaluation point, one fadd per pixel per edge from a
    per-row seed), clamped to [0, 255] FIRST, then scaled by al, then
    truncated toward zero by ftoi -- one f32 rounding per operation
    under nearest-even, here as there;
  * pixels with al = 0 stay black.

This file never runs under run.sh: expected.out is checked in and
regeneration is a maintainer act. It writes 15 header bytes plus
960*540*3 pixel bytes = 1555215 bytes of P6 PPM.

    python3 prototypes/pictura_octonaria_oracle.py \\
        tests/programs/pictura_octonaria/expected.out
"""

import sys

import numpy as np

W = 960
H = 540

# The vertices, exactly as examples/pictura/octonaria.exsc spells them --
# the pictura geometry, scaled: the screen map uses W-1 = 959 and
# H-1 = 539. Every constructor constant is dyadic-exact; the only
# roundings before the loops are the four reciprocals' own and each
# binary op's one f32 rounding, mirrored on both sides.
VERTEX_X = (-0.75, 0.0, 0.75)
VERTEX_Y = (-0.50, 0.75, -0.50)
VERTEX_W = (1.0, 1.25, 1.5)

# Sub-sample offsets in the order the program runs its four passes:
# (ox, oy) = (0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75).
SUB = ((0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75))


def seeds(ax, ay, bx, by, cx, cy, py):
    """The row seed at px = 0, one add, two multiplies and one
    subtraction per edge, the folded seed form the .exsc text writes:
    E(0, py) = (x2 - x1)*(py - y1) + (y2 - y1)*x1. Edge 0 is B->C,
    edge 1 C->A, edge 2 A->B. Every operand is float32 and every binary
    op rounds to float32, in exactly this order."""
    e0 = (cx - bx) * (py - by) + (cy - by) * bx
    e1 = (ax - cx) * (py - cy) + (ay - cy) * cx
    e2 = (bx - ax) * (py - ay) + (by - ay) * ax
    return e0, e1, e2


def render():
    f32 = np.float32
    one = f32(1.0)

    # The perspective divide: ONE reciprocal per vertex, then two
    # multiplies -- three of the program's four divisions.
    iw = [f32(1.0) / f32(VERTEX_W[i]) for i in range(3)]
    ndcx = [f32(VERTEX_X[i]) * iw[i] for i in range(3)]
    ndcy = [f32(VERTEX_Y[i]) * iw[i] for i in range(3)]

    # NDC to screen, pixel centers at integer coordinates: left to
    # right, ((n + 1) * 0.5) * 959, as written.
    sx = [(ndcx[i] + one) * f32(0.5) * f32(959.0) for i in range(3)]
    sy = [(one - ndcy[i]) * f32(0.5) * f32(539.0) for i in range(3)]
    ax, bx, cx = sx[0], sx[1], sx[2]
    ay, by, cy = sy[0], sy[1], sy[2]

    # Per-edge x stepping deltas: dE/dx = (y1 - y2) for edge
    # (x1, y1) -> (x2, y2).
    d0x, d1x, d2x = (by - cy), (cy - ay), (ay - by)

    # det from the row-0 seeds at the CENTER lattice (py = 0.0) -- the
    # same setup expression the program computes once, so the weights
    # come from one consistent definition. The fourth and last division.
    e0s, e1s, e2s = seeds(ax, ay, bx, by, cx, cy, f32(0.0))
    det = e0s + e1s + e2s
    if det <= 0.0:
        raise SystemExit(
            f"oracle: det = {det!r} -- vertices are not CCW for this edge convention"
        )
    r = one / det

    # The group steps: dx * 8, exact (a power-of-two scaling, no
    # rounding), broadcast over the eight lanes.
    stp = (d0x * f32(8.0), d1x * f32(8.0), d2x * f32(8.0))
    stp = tuple(np.full(8, v, dtype=np.float32) for v in stp)

    # Per pass, the lane-offset grid lanes[j] = ox + j -- dyadic-exact
    # (0.25 + 7 needs six fraction bits, 7.75 nine mantissa bits).
    lanes = [np.array([oxf + j for j in range(8)], dtype=np.float32)
             for (oxf, _) in SUB]

    # THE STRUCTURE IS THE PROGRAM'S: per pixel row, four sub-row passes
    # into four row buffers, then the color pass over the row's pixels.
    # (A whole-image-per-pass arrangement would need a full coverage
    # field; the program's row buffers are the settlement both sides
    # share.)
    fb = bytearray(3 * W * H)
    black = f32(0.0)
    full = f32(255.0)
    quarter = f32(0.25)
    for yy in range(H):
        c0, c1, c2, c3 = bytearray(W), bytearray(W), bytearray(W), bytearray(W)
        covs = (c0, c1, c2, c3)
        for k, (oxf, oyf) in enumerate(SUB):
            py = f32(yy) + f32(oyf)                # exact: yy + oy < 2**10
            e0s, e1s, e2s = seeds(ax, ay, bx, by, cx, cy, py)
            # Init: [es; 8] + lanes * [dx; 8] -- the packed vmul first,
            # then the packed vadd, per lane.
            e0 = e0s + lanes[k] * d0x
            e1 = e1s + lanes[k] * d1x
            e2 = e2s + lanes[k] * d2x
            row = covs[k]
            for g in range(0, W, 8):
                # Coverage, scalar per lane: three ordered compares and
                # one byte store -- no masks, matching the program's
                # settlement.
                for j in range(8):
                    if e0[j] >= 0.0 and e1[j] >= 0.0 and e2[j] >= 0.0:
                        row[g + j] = 1
                # ONE packed vadd per edge per 8-pixel group.
                e0 = e0 + stp[0]
                e1 = e1 + stp[1]
                e2 = e2 + stp[2]

        # The color pass over the row: the pictura per-pixel stepping at
        # the INTEGER center lattice, the written sum association, clamp
        # first, then the alpha scale, then truncation.
        e0c, e1c, e2c = seeds(ax, ay, bx, by, cx, cy, f32(yy))
        base = yy * W
        for xx in range(W):
            s0, s1 = f32(c0[xx]), f32(c1[xx])
            s2, s3 = f32(c2[xx]), f32(c3[xx])
            al = (s0 + s1 + s2 + s3) * quarter     # association as written
            cr, cg, cb = black, black, black
            if al > 0.0:
                # Per channel: (c0*E0 + c1*E1 + c2*E2) * r, left to
                # right, clamped BEFORE the alpha scale (al <= 1 keeps
                # [0, 255]), then truncated toward zero by int().
                cr = (full * e0c + black * e1c + black * e2c) * r
                if cr < 0.0:
                    cr = black
                if cr > 255.0:
                    cr = full
                cr = cr * al
                cg = (black * e0c + black * e1c + full * e2c) * r
                if cg < 0.0:
                    cg = black
                if cg > 255.0:
                    cg = full
                cg = cg * al
                cb = (black * e0c + full * e1c + black * e2c) * r
                if cb < 0.0:
                    cb = black
                if cb > 255.0:
                    cb = full
                cb = cb * al
            o = (base + xx) * 3
            fb[o] = int(cr)
            fb[o + 1] = int(cg)
            fb[o + 2] = int(cb)
            e0c = e0c + d0x
            e1c = e1c + d1x
            e2c = e2c + d2x
    return fb


def main():
    fb = render()
    out = open(sys.argv[1], "wb") if len(sys.argv) > 1 else sys.stdout.buffer
    out.write(b"P6\n960 540\n255\n")
    out.write(bytes(fb))
    if len(sys.argv) > 1:
        out.close()


if __name__ == "__main__":
    main()
