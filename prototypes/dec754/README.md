# `dec754` — where do the golden bits come from?

A design probe in spec §18's sense: an instrument for answering a design
question. Python, never shipped, never on the build closure.

```sh
python3 prototypes/dec754/gen_golden.py
```

## The question

`compiler/x86_64/rt/dec754.inc` converts a decimal float literal to IEEE-754
bits with ONE correctly-rounded step, in pure integer code (no FPU, no libc --
the freestanding contract). Its unit fixture,
`tests/unit/dec754_golden.asm`, pins that claim against eighty rows. Against
WHAT? The answer needs an independent converter that is itself correctly
rounded, and the only one in the tree is Python's `float()`, which parses a
decimal string in one rounding.

So this probe is the fixture's oracle: it emits the expected-bits column of
the fixture's table, in the fixture's grouped row order, so the two can be
diffed line for line.

## Two traps it exists to remember

Both bit this development, which is why the probe outlived its first use:

1. **The expectation comes from `float(f"{m}e{e}")`, never `m * 10.0**e`.**
   The latter computes an exact product and then divides -- two roundings.
   `5e-324` comes out `0` instead of the smallest subnormal, `1`, and a
   boundary row silently pins the wrong answer.

2. **`struct.pack("<f", v)` raising `OverflowError` for a finite `v` beyond
   f32 range IS the answer**: the correctly rounded f32 is Inf,
   `0x7F800000`. The first draft treated the exception as a generator bug.

## The contract the corpus respects

The checker packs the literal as mantissa `m` (a positive integer of at most
FIFTEEN significant decimal digits, `< 2^50`) and decimal exponent `e` in
`[-4096, 4095]`; the generator asserts both per row. Consequences the corpus
design honors:

- Values whose shortest decimal form needs 16-17 digits (f64 max
  `1.7976931348623157e308`, `0.9999999999999999`) are not expressible; the
  nearest contract-legal pairs pin the same boundaries (see the generator's
  CASES comments for the rewrites).
- The shortest decimal that TIES at f64 width is `2^53+1` -- sixteen digits
  -- so round-half-to-even is pinned by the f32 rows alone:
  `16777217` rounds down to `0x4B800000`, `16777219` rounds up to
  `0x4B800002`.

## What it caught

The first full run of the fixture's ancestor failed five f32 rows with
mantissas truncated to their top bits (`0.1` -> `0x3DCC0000`): the fixture's
quotient-drop step had measured the candidate's bit length from the SLOT'S
ADDRESS (`lea rdi,[d75Q]` where `dec754_bl64` takes a value), so f64 rows
passed by accident -- a stack address is shorter than f64's p+3 = 56 -- and
f32 rows (p+3 = 27) took a fictional drop step. The generator's
correctly-rounded expectations are what made the failure visible as five
specific wrong bit patterns rather than "something is off".