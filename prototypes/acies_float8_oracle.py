#!/usr/bin/env python3
"""The acies_float8 oracle: whole-acy arithmetic computed independently.

Verification-only, the "not our own bug" check for
tests/programs/acies_float8/ -- the same reason entry 23 vendors an
external certificate and prototypes/pictura_oracle.py mirrors the
rasterizer. It mirrors the .exsc program's ARITHMETIC exactly: numpy
float32/float64 ELEMENTWISE, one IEEE-754 rounding per lane per
operation, the same association as written (mix is `(a + b) / c`, the
nested whole-acy expression), so under round-to-nearest-even the two
sides either agree byte for byte or one side's lowering changed
something -- an op dropped, lanes permuted, a chunk of a 32-byte
vf32.8 silently not moved, a scalarized fallback that rounded
differently.

The grids and the encoding are the program's own (its header lists
them): a/b dyadic-exact, c small integers so the quotient lanes are
INEXACT and rounding shows, then each lane written as
floor(v * 2**k) -- exact power-of-two scaling, truncation of a
non-negative value -- in eight explicit big-endian bytes, k = 20 for
the f32 section and 32 for the f64 one. 640 bytes in all:
2 sections x 5 results x 8 lanes x 8 bytes.

Run with the system python3 (numpy is not in the devShell, and need
not be: run.sh never runs this file -- expected.out is checked in,
regeneration is a maintainer act):

    python3 prototypes/acies_float8_oracle.py \\
        tests/programs/acies_float8/expected.out
"""

import sys

import numpy as np

# ---- the f32 section (acies<f32, 8>) -----------------------------------
A32 = np.array([1.0, 2.5, 3.0, 4.75, 6.0, 7.25, 9.5, 12.0], dtype=np.float32)
B32 = np.full(8, np.float32(0.5), dtype=np.float32)          # the [0.5; 8] broadcast
C32 = np.array([3.0, 11.0, 13.0, 7.0, 5.0, 9.0, 17.0, 23.0], dtype=np.float32)

SUMA = A32 + B32
DIFA = A32 - B32
PRDA = A32 * B32
QUOA = A32 / C32
MIXA = (A32 + B32) / C32                                     # association as written

SCALE32 = np.float32(1048576.0)                              # 2**20

# ---- the f64 section (acies<f64, 8>) ------------------------------------
A64 = np.array([1.0, 2.5, 3.0, 4.75, 6.0, 7.25, 9.5, 12.0], dtype=np.float64)
B64 = np.full(8, 0.25, dtype=np.float64)                     # the [0.25; 8] broadcast
C64 = np.array([3.0, 7.0, 11.0, 13.0, 17.0, 19.0, 23.0, 29.0], dtype=np.float64)

SUMB = A64 + B64
DIFB = A64 - B64
PRDB = A64 * B64
QUOB = A64 / C64
MIXB = (A64 + B64) / C64

SCALE64 = np.float64(4294967296.0)                           # 2**32


def lane_bytes(v, scale):
    """floor(v * scale) as eight big-endian bytes -- v is one lane, the
    scaling an exact power of two, the truncation ftoi's (toward zero,
    and every lane here is non-negative, so floor == trunc)."""
    w = int(v * scale)              # int() of a numpy scalar truncates
    assert 0 <= w < 1 << 64
    return w.to_bytes(8, "big")


def main():
    out = bytearray()
    for result in (SUMA, DIFA, PRDA, QUOA, MIXA):
        for k in range(8):
            out += lane_bytes(result[k], SCALE32)
    for result in (SUMB, DIFB, PRDB, QUOB, MIXB):
        for k in range(8):
            out += lane_bytes(result[k], SCALE64)
    assert len(out) == 640
    if len(sys.argv) > 1:
        with open(sys.argv[1], "wb") as fh:
            fh.write(out)
    else:
        sys.stdout.buffer.write(out)


if __name__ == "__main__":
    main()
