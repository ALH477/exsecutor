#!/usr/bin/env python3
"""The contractio oracle: spec 5.4's reduction shapes, implemented from the
paragraph and nothing of the compiler.

Verification-only, the "not our own bug" check for tests/programs/contractio/
(spec 14, entry 28). It implements the 2026-09-25 definition in spec 5.4,
"What a group computes, and how groups combine", literally:

  * ordinata:    fold left to right from the operator's identity; the first
                 contribution is identity op x (0.0 + x for floats).
  * arborea w:   consecutive groups of w in index order, the last holding the
                 remainder; inside a group of k <= w, combine adjacent pairs
                 left to right, an odd trailing element passes up unchanged,
                 repeat until one value; each completed group's value is
                 folded into the running result left to right from the
                 identity.

Every operation is IEEE-754 binary32 under round-to-nearest-even: two f32
operands are added exactly in Python's f64 (the exact sum of two binary32
values with exponents at most 29 apart fits in 53 bits; every value here is
in [2^-23, 2^6) or zero, so it always fits) and rounded once to binary32 by
struct's 'f' pack, which is the correctly rounded f32 sum -- one rounding
per operation, exactly as the compiled program performs it.

THE FORMAT, shared with the program (tests/programs/contractio/):
  stdin:  twenty values, each a little-endian signed 32-bit integer n with
          |n| < 2^24, meaning the f32 value n / 8388608.0 (exact: a power
          of two).
  stdout: three results, ordinata / arborea 8 / arborea 4, each written as
          the little-endian signed 64-bit integer  ftoi(r * 8388608.0)  --
          exact for every r with |r| >= 1 (r's ulp is then >= 2^-23) and
          |r| < 2^40, which the generated input guarantees.

Usage: contractio_oracle.py < input.bin > expected.out
       contractio_oracle.py --gen > input.bin     (the fixed input)
"""
import struct, sys

N = 20
SCALE = 8388608.0  # 2^23


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def add(a, b):
    return f32(a + b)  # exact in f64, one rounding to binary32


def tree(group):
    """Pairwise tree over a group: adjacent pairs left to right, an odd
    trailing element passes up unchanged, until one value remains."""
    level = list(group)
    while len(level) > 1:
        nxt = []
        for i in range(0, len(level) - 1, 2):
            nxt.append(add(level[i], level[i + 1]))
        if len(level) % 2 == 1:
            nxt.append(level[-1])
        level = nxt
    return level[0]


def ordinata(xs):
    acc = 0.0  # the identity of +
    for x in xs:
        acc = add(acc, x)
    return acc


def arborea(xs, w):
    acc = 0.0
    for g in range(0, len(xs), w):
        acc = add(acc, tree(xs[g:g + w]))
    return acc


def gen():
    # The fixed input: found by search (seed 8 of a 20-value mix of values
    # near 1..2, tiny values of a few 2^-23 units, and mid-sized ones) as the
    # first candidate on which the three shapes give three DIFFERENT
    # results -- 121209096 / 121209080 / 121209088 in scaled units, i.e. the
    # sums differ in their last two f32 bits. That disagreement is the point
    # of entry 28: the shape is observable, so it had better be declared.
    return [11148827, 13912432, 9147922, 12247658, 13384382, 12088624, 32,
            -311022, 9964469, 940885, 11419732, -59, 780664, 14830278,
            13202345, -1548153, 22, 32, 10, -3]


def main(argv):
    if len(argv) > 1 and argv[1] == '--gen':
        sys.stdout.buffer.write(b''.join(struct.pack('<i', n) for n in gen()))
        return 0
    raw = sys.stdin.buffer.read()
    if len(raw) != 4 * N:
        sys.stderr.write('contractio_oracle: expected %d bytes, got %d\n' % (4 * N, len(raw)))
        return 2
    ns = [struct.unpack('<i', raw[4 * i:4 * i + 4])[0] for i in range(N)]
    xs = [f32(n / SCALE) for n in ns]   # exact: |n| < 2^24, power-of-two divisor
    out = b''
    for r in (ordinata(xs), arborea(xs, 8), arborea(xs, 4)):
        scaled = f32(r * SCALE)          # exact: power-of-two scaling
        q = int(scaled)                  # ftoi: truncation toward zero; integer-valued here
        out += struct.pack('<q', q)
    sys.stdout.buffer.write(out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
