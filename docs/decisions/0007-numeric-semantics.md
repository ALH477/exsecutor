# 0007 — Numeric semantics are declared, not ambient

**Status:** Accepted as design, 2026-09-09. **Nothing implemented.**
**Relates to:** spec §5.2, §5.4 (new), §9.6, §10.1, §15 #4; `docs/design/norma-algebra.md`

## Context

The specification retired ambient locale, ambient target, ambient clock and
ambient environment. It did not retire the ambient *floating-point
environment*, which is the same disease:

- **`-ffast-math` is `setlocale` for numbers.** A global switch that changes the
  semantics of code that never asked for it, appears in no interface, and lets
  two libraries with byte-identical headers compute different answers. Enabling
  it in one translation unit changes results in another.
- **MXCSR / FPCR rounding mode is worse.** Thread-local mutable state, settable
  at runtime by any code in the process, silently altering every result
  downstream of a call.

§5.2 already puts byte order in the type and requires `mensura` mixing to name
its target assumption. Rounding, reassociation and FMA formation had no
equivalent. That was an incomplete row in the table, not a missing feature.

Separately, the project intends numerical and linear-algebra work on parallel
hardware, which forces a question the spec had not answered: a parallel
reduction *is* a reassociation, so "no reassociation" and "use all the cores"
appear to conflict.

## Decision

**Numeric semantics are declared in the `ego`, and are interface-affecting.**

A `numeri` block (§10.1) carries `rotundatio`, `reassociatio`, `contractio`,
`subnormales`. Defaults are the conservative ones: nearest-ties-to-even,
reassociation **forbidden**, FMA only where written, subnormals preserved.

Because these change what a call computes, `numeri` sits on the **interface**
side of §9.6's two-hash line: dependents rebuild when it changes, and calling
between modules with incompatible `numeri` requires an explicit coercion that
names the assumption.

**Reduction tree shape is part of the operation, not the optimizer's choice.**
`summa_ordinata` is strict left-to-right; `summa_arborea(v, 8)` is a fixed-width
pairwise tree. This resolves the apparent conflict above rather than trading one
goal for the other: a declared tree is deterministic *and* parallel. The machine
decides how long a reduction takes and never decides what it computes.

**Vector lane count is in the type** (`acies<f32, 8>`), never inferred from the
host. A target-inferred width would make results machine-dependent, which is the
failure this whole document is written against. Lowering to AVX-512, NEON or a
scalar loop changes performance only.

**Integers:** exact arbitrary widths (`u7`, `i23`); no implicit promotion;
overflow behaviour in the operator (`+` traps, `+%` wraps, `+|` saturates, `+?`
yields an optional) rather than in a compiler flag; `@transitus` extended to
explicit bit offsets.

## Consequences

**Positive**

- Byte-identical numerical results become achievable across core counts and
  target ISAs — not merely hoped for. For scientific and financial computing,
  where reproducibility is a chronic and expensive problem, this is the single
  most differentiating property the language could offer.
- The `-ffast-math` disaster class is unrepresentable rather than discouraged. A
  module that wants reassociation must say so where its callers can see it.
- Directed rounding being declarable makes *validated* numerics possible —
  interval arithmetic with provable error bounds, which is what "respecting
  mathematics" actually requires.
- Automatic differentiation gets a free correctness precondition. It also gets a
  free cost annotation: forward-mode AD is pure and needs no `poscit`;
  reverse-mode needs a tape, therefore `alloc`, therefore it is visible in the
  signature. The capability system documents the difference without being asked.

**Negative**

- **Conservative defaults cost performance**, and the cost is not small.
  `reassociatio vetita` forbids optimizations every other toolchain performs by
  default. Exsecutor will lose naive benchmarks against `-ffast-math` C, and the
  honest response is to report the comparison rather than hide it.
- Fixed reduction trees are slower than letting each machine reduce in its
  natural width.
- `numeri` on the interface side means numeric policy changes trigger rebuilds
  across the dependency graph. Correct, and occasionally annoying.
- This is a large addition to a type system that **does not exist yet**. §5.4 is
  `[OPEN]`; the type checker is Stage 2 and the backend Stage 3. Nothing here is
  validated by running code, and it must not be described as though it were.

**Neutral**

- Arbitrary-width integers are well-trodden (LLVM `iN`, Zig, Ada) and carry no
  novel risk.
- Operator-level overflow follows Zig. Familiar to that audience, unfamiliar to
  C programmers, and unambiguous either way.

## Open

- **Root coinage.** §15 #4 already records that there is no governance process
  for coining roots, and no Latin for *hash*, *socket*, *mutex*. Linear algebra
  makes this urgent: *lane*, *tile*, *eigenvalue* and *workgroup* have no usable
  classical roots. `docs/design/norma-algebra.md` is the forcing function for
  that gap.
- Whether a fixed reduction tree is fast enough to be adopted, or merely correct
  enough to be admired, is unmeasured. `[UNTESTED]`
