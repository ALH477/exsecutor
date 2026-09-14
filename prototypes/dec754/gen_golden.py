#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
"""
gen_golden.py -- golden-table generator for tests/unit/dec754_golden.asm.

Computes the expected IEEE-754 bits for every (mantissa, exponent, width)
row of the dec754_bits fixture's table. The assembly fixture is handwritten
-- this script never feeds it, per prototypes/README.md -- but its table is
REGENERABLE: run this, diff its stdout against the fixture table's `dq`
lines (comments stripped -- the fixture annotates every row), and any
difference is a conversion bug somewhere (or a deliberately added case
that was never generated). The CASES list below is ordered to match the
fixture's grouping, so the diff is line-for-line. Python's float() parses
a decimal string with one correctly-rounded step, which is exactly the
property dec754_bits claims, so agreement is evidence and disagreement is
a finding.

Two traps this exists to remember:

  * The expected bits come from PARSING the decimal string --
    float(f"{m}e{e}") -- never from m*10.0**e. The latter computes an exact
    product and then divides, double-rounding: 5e-324 comes out 0 instead
    of the smallest subnormal, 1.

  * struct refuses to pack a finite f64 into f32 when it overflows
    (OverflowError). That refusal IS the answer: the correctly rounded
    f32 of anything beyond 3.4028234e38 is Inf, 0x7F800000.

The corpus respects the packing contract the checker will enforce: a
POSITIVE mantissa of at most FIFTEEN significant decimal digits (< 2^50)
and a decimal exponent in [-4096, 4095]. Values whose shortest decimal
form needs 16-17 digits (f64 max, 0.9999999999999999, the shortest f64
tie 2^53+1) are therefore not expressible as corpus rows; the nearest
contract-legal pairs pin the same boundaries instead, and the f32 rows
16777217 / 16777219 carry the round-half-to-even duty alone (the shortest
f64 tie would need sixteen digits).

Exit 0 and the rows on stdout; a case whose hand-entered expectation does
not match what Python computes is a hard error (the case table's `None`
cells are "record what Python computes" -- used once, then frozen).
"""

import struct
import sys

M15 = 10**15 - 1


def f64_bits(m, e):
    v = float(f"{m}e{e}")
    if v == float("inf"):
        return 0x7FF0000000000000
    return struct.unpack("<Q", struct.pack("<d", v))[0]


def f32_bits(m, e):
    v = float(f"{m}e{e}")
    try:
        return struct.unpack("<I", struct.pack("<f", v))[0]
    except OverflowError:
        # struct refuses to turn a finite f64 into f32 Inf; that is
        # exactly what Inf means here
        return 0x7F800000


def row(m, e, w, bits):
    assert 1 <= m <= M15, f"m={m} breaks the fifteen-digit contract"
    assert -4096 <= e <= 4095, f"e={e} outside the pack range"
    if w == 64:
        exp = f64_bits(m, e)
    else:
        exp = f32_bits(m, e)
    if bits is not None and exp != bits:
        print(
            f"GENERATOR MISMATCH m={m} e={e} w={w}: "
            f"computed {exp:#018x} but case expected {bits:#018x}",
            file=sys.stderr,
        )
        return None
    return (m, e, w, exp)


# (m, e) pairs, both widths, expected bits verified against Python's
# correctly-rounded conversion. THE ORDER HERE IS THE FIXTURE TABLE'S
# ORDER (grouped: everyday values, range boundaries, tie-to-even, the
# fifteen-digit contract) so that this script's stdout diffs cleanly
# against tests/unit/dec754_golden.asm's table. Cases whose shortest
# decimal form needs more than fifteen digits were REWRITTEN to the
# nearest contract-legal pair that still exercises the same boundary:
#   f64 max: the boundary value 1.7976931348623157e308 is seventeen
#   digits; 179769313486231e293 (fifteen digits) sits just below it and
#   still pins the largest-finite rounding.
#   0.9999999999999999 (sixteen digits) is not expressible; the near-one
#   boundary is pinned instead by 999999999999999e-15.
CASES = [
    # the everyday values: both paths, both widths
    (1, -1, 0x3FB999999999999A, 0x3DCCCCCD),          # 0.1
    (5, -1, 0x3FE0000000000000, 0x3F000000),          # 0.5
    (1, 0, 0x3FF0000000000000, 0x3F800000),           # 1.0
    (123, 0, 0x405EC00000000000, 0x42F60000),         # 123.0
    (1, -3, 0x3F50624DD2F1A9FC, 0x3A83126F),          # 1e-3
    (25, -1, 0x4004000000000000, 0x40200000),         # 2.5
    (35, -1, 0x400C000000000000, 0x40600000),         # 3.5
    (123456, -3, 0x405EDD2F1A9FBE77, 0x42F6E979),     # 123.456
    (1, 23, 0x44B52D02C7E14AF6, 0x65A96816),          # 1e23
    (83886095, -7, 0x4020C6F7D30AD46F, 0x410637BF),   # f32 rounding boundary
    (1, 1, 0x4024000000000000, 0x41200000),          # 10.0
    (999, 0, 0x408F380000000000, 0x4479C000),        # 999.0
    # the range boundaries
    (15, 299, 0x7E41EB2D66005835, 0x7F800000),        # 1.5e300 (f32 Inf)
    (179769313486231, 293, None, None),               # just below f64 max
    (1, 308, 0x7FE1CCF385EBC8A0, 0x7F800000),        # 1e308 (f32 Inf)
    (1, 309, 0x7FF0000000000000, 0x7F800000),        # f64 overflow cut
    (1, 338, 0x7FF0000000000000, 0x7F800000),        # overflow cut
    (1, 99, 0x547D42AEA2879F2E, 0x7F800000),          # 1e99
    (1, 38, 0x47D2CED32A16A1B1, 0x7E967699),          # 1e38
    (34028235, 31, 0x47EFFFFFE54DAFF8, 0x7F7FFFFF),   # just below f32 max
    (117549435, -46, 0x380FFFFFFF9FDBA8, 0x00800000),  # near f32 min normal
    (5, -324, 0x0000000000000001, 0x00000000),        # min subnormal f64
    (3, -324, 0x0000000000000001, 0x00000000),        # just above half, up
    (2, -324, 0x0000000000000000, 0x00000000),       # tie below half, down
    (1, -324, 0x0000000000000000, 0x00000000),       # below half, zero
    (1, -338, 0x0000000000000000, 0x00000000),        # f64 underflow cut
    (1, -339, 0x0000000000000000, 0x00000000),       # deep underflow
    (1, -99, 0x2B617F7D4ED8C33E, 0x00000000),         # 1e-99 (f32 zero)
    (1, -45, 0x3696D601AD376AB9, 0x00000001),        # near f32 subnormal
    (14, -46, 0x369FF868BF4D956A, 0x00000001),
    (7, -46, 0x368FF868BF4D956A, 0x00000000),
    (15, -46, 0x36A1208141E9900B, 0x00000001),
    (2, -45, 0x36A6D601AD376AB9, 0x00000001),
    (1, -40, 0x37A16C262777579C, 0x000116C2),
    # round-half-to-even, f32 only (the f64 tie needs sixteen digits)
    (16777217, 0, 0x4170000010000000, 0x4B800000),   # f32 tie rounds DOWN
    (16777219, 0, 0x4170000030000000, 0x4B800002),   # f32 tie rounds UP
    (16777216, 0, 0x4170000000000000, 0x4B800000),  # 2^24 exact
    # the fifteen-digit packing contract
    (999999999999999, 0, 0x430C6BF52633FFF8, 0x58635FA9),  # the clamp
    (333333333333333, -15, None, None),                     # near 1/3, 15 digits
    (999999999999999, -15, None, None),               # near-one boundary
]


def main():
    lines = []
    n = 0
    for m, e, b64, b32 in CASES:
        r = row(m, e, 64, b64)
        if r is None:
            sys.exit(f"case ({m},{e}) does not round as claimed (f64)")
        lines.append(f"\t\tdq {r[0]}, {r[1]}, 64, {r[3]:#x}")
        n += 1
        r = row(m, e, 32, b32)
        if r is None:
            sys.exit(f"case ({m},{e}) does not round as claimed (f32)")
        lines.append(f"\t\tdq {r[0]}, {r[1]}, 32, {r[3]:#x}")
        n += 1
    print("\n".join(lines))
    print(f"; {n} rows", file=sys.stderr)


if __name__ == "__main__":
    main()