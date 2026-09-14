#!/usr/bin/env python3
"""The pictura oracle: the RGB triangle rendered independently, in Python.

Verification-only, the "not our own bug" check for tests/programs/
pictura_triangulum/ -- the same reason entry 23 vendors an external
certificate. It mirrors the Exsecutor rasterizer's ARITHMETIC ORDER
exactly, operation for operation, because both sides are IEEE-754 f64
under round-to-nearest-even: identical operation order means identical
bits, and any difference in the image means one side changed its order.

The algorithm is the one the spec's conformance entry 26 records
(spec 14, the design notes under examples/pictura/):

  * three vertices in homogeneous coordinates, ONE reciprocal per vertex
    (iw = 1/w) so the perspective divide is 3 fdiv, never 6;
  * NDC to screen space, pixel centers at integer coordinates;
  * edge functions stepped by one add per pixel, row seeds recomputed
    per row;
  * det = the sum of the three row-0 seeds -- the SAME expressions the
    loop uses, so the barycentric weights come from one consistent
    definition, never a differently-associated 2*area;
  * one reciprocal for the weights (r = 1/det): 4 fdiv in the whole
    program, none inside the pixel loop (the census the emitted fasmg
    is grepped for);
  * color = (c0*E0 + c1*E1 + c2*E2) * r per channel, clamped to
    [0, 255] and truncated toward zero by ftoi;
  * covered pixels get the interpolated color, uncovered stay black.

Usage: pictura_oracle.py [OUT.ppm]   (default: stdout, P6)
"""

import sys

W = 96
H = 54

# The vertices, exactly as examples/pictura/pictura.exsc spells them:
# clip-space x, y and a positive depth w. Every literal is exact in
# binary (dyadic) so dec754's one rounding is exact too. The order is
# the one that makes det > 0 under the edge convention below (screen
# space is y-DOWN, so "counter-clockwise on paper" is clockwise here):
# red at bottom-left, blue at the apex, green at bottom-right.
VERTEX_X = (-0.75, 0.0, 0.75)
VERTEX_Y = (-0.50, 0.75, -0.50)
VERTEX_W = (1.0, 1.25, 1.5)
# Per-vertex colors, channels in the order the framebuffer stores
# them: R, G, B.
COLOR = ((255.0, 0.0, 0.0), (0.0, 0.0, 255.0), (0.0, 255.0, 0.0))


def render():
    # The perspective divide: one reciprocal per vertex, then two
    # multiplies -- 3 divisions total.
    iw = [1.0 / VERTEX_W[i] for i in range(3)]
    ndcx = [VERTEX_X[i] * iw[i] for i in range(3)]
    ndcy = [VERTEX_Y[i] * iw[i] for i in range(3)]

    # NDC to screen. (W-1) and (H-1) as f64: 95.0, 53.0.
    sx = [(ndcx[i] + 1.0) * 0.5 * 95.0 for i in range(3)]
    sy = [(1.0 - ndcy[i]) * 0.5 * 53.0 for i in range(3)]
    ax, bx, cx = sx[0], sx[1], sx[2]
    ay, by, cy = sy[0], sy[1], sy[2]

    # Edge deltas, per edge, the subtractions the setup performs:
    # edge 0 is B->C, edge 1 is C->A, edge 2 is A->B.
    # dE/dx = (y1 - y2), dE/dy = (x2 - x1) for edge (x1,y1)->(x2,y2).
    d0x, d0y = (by - cy), (cx - bx)
    d1x, d1y = (cy - ay), (ax - cx)
    d2x, d2y = (ay - by), (bx - ax)

    # Row seeds at px = 0.0:
    #   E(px, py) = (x2-x1)*(py-y1) - (y2-y1)*(px-x1)
    #             = (x2-x1)*(py-y1) - (y2-y1)*(0.0-x1) at px = 0
    #             = (x2-x1)*(py-y1) + (y2-y1)*x1
    # The third form is what both sides compute -- one fadd, two fmul,
    # one fsub per edge -- so the constant is folded as written.
    def seeds(py):
        e0 = (cx - bx) * (py - by) + (cy - by) * bx
        e1 = (ax - cx) * (py - cy) + (ay - cy) * cx
        e2 = (bx - ax) * (py - ay) + (by - ay) * ax
        return e0, e1, e2

    # det from the row-0 seeds: the same three values the loop's first
    # row starts from, so weights can never disagree with coverage.
    e0s, e1s, e2s = seeds(0.0)
    det = e0s + e1s + e2s
    if det <= 0.0:
        # The winding is a property of the vertex list above; the oracle
        # and the program share the list, so this fires only if the two
        # sides stopped sharing it.
        raise SystemExit(f"oracle: det = {det!r} -- vertices are not CCW for this edge convention")
    r = 1.0 / det

    fb = bytearray(3 * W * H)
    for yy in range(H):
        py = float(yy)          # itof, once per row
        e0, e1, e2 = seeds(py)  # three fadds of work, per row
        base = yy * (3 * W)
        for xx in range(W):
            # Coverage: three ordered compares. NaN never arises (all
            # finite), but the ordered forms are what both backends
            # lower, so the oracle states them too.
            if e0 >= 0.0 and e1 >= 0.0 and e2 >= 0.0:
                # Per channel: (c0*E0 + c1*E1 + c2*E2) * r, left to
                # right, two fadds then one fmul -- the order the
                # Exsecutor expression writes, and the order fasmg emits.
                for ch in range(3):
                    c = (COLOR[0][ch] * e0 + COLOR[1][ch] * e1
                         + COLOR[2][ch] * e2) * r
                    # Clamp, then ftoi truncates toward zero.
                    if c < 0.0:
                        c = 0.0
                    if c > 255.0:
                        c = 255.0
                    fb[base + 3 * xx + ch] = int(c)   # int(): toward 0
            e0 += d0x
            e1 += d1x
            e2 += d2x
    return fb


def main():
    fb = render()
    out = open(sys.argv[1], "wb") if len(sys.argv) > 1 else sys.stdout.buffer
    out.write(b"P6\n96 54\n255\n")
    out.write(bytes(fb))
    if len(sys.argv) > 1:
        out.close()


if __name__ == "__main__":
    main()