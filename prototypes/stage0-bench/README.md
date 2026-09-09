# prototypes/stage0-bench/

The Stage 0 measurement harness behind two evidence tables in the spec:
§6.2's ARC overhead figures and §9.2's compile-speed figures. Both are
already marked `[UNREPRODUCED]` in the spec, for the reason this directory
is empty.

## What is absent

Every source `run.sh` in this directory invokes is missing, named
explicitly here because "the benchmarks are gone" and knowing exactly which
files are gone are different claims:

| file | invoked as | present? |
|---|---|---|
| `bench1_refcount.c` | `gcc -O2 -o bench1 bench1_refcount.c && ./bench1` | absent |
| `bench2b_biquad.c` | `gcc -O2 -o bench2 bench2b_biquad.c -lm && ./bench2` | absent |
| `bench3_ast.c` | `gcc -O2 -o bench3 bench3_ast.c && ./bench3` | absent |
| `gen_c.py` | looped for n in 40, 200, 400, 1000, 2000 | absent |
| `time_cc.py` | run after the `gen_c.py` loop | absent |

None of the five exist anywhere in this repository or its history as
received. `run.sh` here fails on purpose, naming every missing file, until
they exist.

## What depends on them

- **§6.2, "Measured evidence"** (ARC/refcounting overhead) — the raw
  refcount cost table, the biquad DSP inner-loop table, and the 4.19M-node
  AST object-graph table. The section says so itself: *"Sources in
  `prototypes/stage0-bench/` — `[UNREPRODUCED]`, the sources are absent from
  the tree and the figures below are carried forward from v0.2 unverified."*
- **§9.2, "Backend"** (compile speed) — the gcc `-O0` kloc/s table and the
  projected 10k-Exsecutor-LOC → 0.76s full-rebuild estimate. Also
  self-marked: *"Compile speed was measured and is not the constraint I
  previously claimed. `[UNREPRODUCED]` — the measurement harness is absent
  from the tree; figures carried forward from v0.2."*
- **§6.3**, "The six decisions, ordered by measured effect" (arena
  allocation at 2.8× over malloc, non-atomic refcounts, borrowed-by-default
  parameters, and the rest) is read directly off the §6.2 measurements and
  inherits the same `[UNREPRODUCED]` status.

## Out of scope

Reconstructing these five files is **not done here, and not imminent.**
There is no schedule for it and this README should not be read as implying
one. §6.2's and §9.2's figures stay `[UNREPRODUCED]` — carried forward from
v0.2, unverified — until someone writes new benchmark sources, runs them on
a stated machine, and records fresh numbers. CLAUDE.md's evidence discipline
applies here exactly as it does to `capcheck/`: never present a
re-derivation as a restoration, and never report a benchmark you did not
run.

## What each file evidently measured — inferred from filenames, not known

Nobody here has read these files; they are not in this tree. What follows is
inference from the filenames plus which spec table matches their apparent
shape — not a description of the actual lost source. Treat it as a
hypothesis for whoever writes a replacement, not as a spec, and check it
against what the new code actually measures rather than assuming it.

- **`bench1_refcount.c`** — likely the "raw refcount cost" figures in §6.2:
  cycles for a non-atomic vs. an atomic retain/release pair (~2.9 vs. ~25.4
  cycles, 8.74×). The filename names the operation directly and the table is
  the obvious match.
- **`bench2b_biquad.c`**, linked with `-lm` (libm, i.e. floating point) —
  likely §6.2's "DSP inner loop" table: a 4-stage biquad filter over
  512-sample blocks, 51.2M samples total, compared across no-refcounting,
  retain/release per callback, and retain/release per sample (atomic and
  non-atomic). A biquad is a standard DSP numeric-loop kernel and `-lm` is
  consistent with a float-heavy filter.
- **`bench3_ast.c`** — likely §6.2's "object graph" table: a 4.19M-node AST,
  build + 8 traversals + teardown, compared across arena, malloc/free, and
  ARC (atomic and non-atomic, borrowed vs. retaining walks). This table is
  also the evident source of §6.3 decision #1's headline number — arena
  allocation measured at 2.8× over malloc. An AST is the obvious vehicle for
  an allocation-heavy object-graph benchmark, and the filename matches
  directly.
- **`gen_c.py` + `time_cc.py`** — likely §9.2's compile-speed scaling
  harness: `gen_c.py` generating backend-shaped synthetic C sources sized by
  n (40, 200, 400, 1000, 2000 — the spec does not say whether that counts
  functions, statements, or files, and neither does the filename), and
  `time_cc.py` timing `gcc -O0` across them to produce the kloc/s figures
  (40-66 for many small functions, 26 for the pathological
  two-20,000-statement-function case) and the projected 10k-LOC → 0.76s
  full-rebuild estimate.

If these are ever rewritten, the inference above is a starting hypothesis
for what to reproduce — not a guarantee of what the original files did.
