# 0012 — Two backends: fasmg is the reference, C is reach

**Status:** Accepted as design, 2026-09-09. **Nothing implemented.**
**Relates to:** spec §5.2, §5.3, §5.4, §5.5, §9.2 (amended to match), §9.3,
§9.5, §9.6, §16, §18.1; ADR 0007, ADR 0009; `docs/design/ssa-ir.md`

## Context

Spec §9.2 said "C backend first" and that it "reaches RISC-V and embedded
Linux immediately". Spec §5.4 makes four guarantees that "C" cannot carry
unaided, and nothing in the repo addressed any of them:

1. `reassociatio vetita` / `contractio explicita` hold only if the downstream
   compiler is run with `-ffp-contract=off` and without `-ffast-math`.
2. `subnormales conservata` is MXCSR/FPCR runtime state, not expressible in C
   source at all.
3. Trapping `+` versus `+%` versus `+|`: signed overflow is undefined
   behaviour in C; the direct expression of a trap is `__builtin_add_overflow`,
   a GCC/Clang extension.
4. Spec §5.2's bit-width `@transitus` fields: C bitfield layout is
   implementation-defined (allocation order, straddling, unit alignment).

The forcing constraint is spec §9.3: `exsc` is a pure function of (source,
`ego`, lockfile, flags). A guarantee that depends on how someone later invokes
gcc has leaked out of the artifact, and spec §9.3 is weaker than it claims.

A second cost was never priced. Spec §18.1 makes the build closure `{fasmg}`
so that the purity contract is a property of the artifact; a C-only backend
then puts a C toolchain into every *user's* closure and into the self-hosting
path (spec §16, Stage 5). And the deepest cost: through C alone, spec §5.4 is
not hard to honour but **untestable** — there is nothing to diff a C result
against, so a criterion that cannot fail is not a criterion.

## Decision

**Both backends. The fasmg backend is the reference and defines the
semantics; the C backend is reach.** Recorded in the amended spec §9.2.

| backend | role |
|---|---|
| fasmg | semantic reference; self-hosting path; keeps the closure `{fasmg}` end to end; **deliberately naive** — every value in a stack slot, no register allocator, no scheduling; x86-64 only until someone writes another instruction selector |
| C | every target a C compiler supports — spec §9.2's RISC-V and embedded Linux on day one; pairs with `zig cc`; inherits register allocation, scheduling and SysV aggregate classification (spec §5.3) |

**Normativity.** Where the two disagree, the fasmg output *is* Exsecutor and
the C output is a port. A module declaring `subnormales conservata` whose C
build cannot deliver it does not get a slightly different Exsecutor; it gets a
**failed build** — spec §5.5's rule for devices ("a target that cannot honour
the declared `numeri` fails the build") extended to backends, as the amended
spec §9.2 now states. The C target is a documented weaker target, never a
second dialect.

**The differential test.** Both backends compile every module of the
conformance suite (spec §14); observable results — output bytes, exit status,
trap-or-not — must be identical, and the comparison runs in CI beside
`proba-reproducibilitatem` (spec §16, Stage 3). It is two-way: on the common
subset (integer wrap, comparisons, memory, control flow) the C build checks
the reference's instruction templates; on the numeric subset the reference
checks the C build. This is the house pattern's third instance, not a new
idea: `vendor/hydramesh-wire/` certifies eleven implementations against one
reference (spec §5.2), spec §5.5 requires CPU and GPU to agree bit for bit,
and `docs/design/amdgpu-backend.md` requires every kernel to match a scalar
reference.

**The C backend's own rule.** Each guarantee is defended by the emitted C
itself, in this order of preference: (a) expressed in ISO C with exact
semantics; (b) if not expressible, refused at C-compile time by `#error` or
`_Static_assert` on the properties it needs; (c) if not detectable then,
asserted by the runtime at process start; (d) if none of those, `exsc`
refuses the target. Every emitted translation unit opens with a prologue that
does (b): `CHAR_BIT == 8`, two's complement, IEEE binary32/64 by
`<float.h>`'s `FLT_RADIX`/`FLT_MANT_DIG`/`DBL_MANT_DIG` (freestanding-safe,
unlike `__STDC_IEC_559__`, which glibc's headers define and bare-metal
targets lack), `__FAST_MATH__` and `__FINITE_MATH_ONLY__` undefined. What this gives up: the
emitted C is C11 plus a short, enumerated set of GCC/Clang extensions, each
with a portable fallback or a refusal; it will not build with a compiler
outside that family, and that is not a goal. The spec's stated targets
(RISC-V, embedded Linux, `none-eabi`, `zig cc`) are all GCC or Clang.

**The four problems, both backends.**

| problem | fasmg backend | C backend |
|---|---|---|
| 1 reassociation, contraction | one instruction per IR op; `vfmadd` only for the IR's `fma`; nothing downstream can reassociate | one statement per IR op; `#pragma STDC FP_CONTRACT OFF`; `#error` on `__FAST_MATH__`; GCC-specific `#pragma GCC optimize("fp-contract=off")` as the belt to that brace `[UNTESTED]`. Residual: a GCC that ignores the pragma under `-ffp-contract=fast` — caught only by the differential test |
| 2 subnormals, rounding | `ldmxcsr` at process start from the module's `numeri`; re-check after `externus` return `[OPEN]` | inexpressible in source; runtime sets rounding by `fesetround` (C99) and **asserts** FTZ/DAZ clear at start through `_mm_getcsr` / FPCR read (GCC/Clang) `[UNTESTED]`; the base RISC-V F/D extensions define no flush mode `[UNTESTED]`. `-ffast-math`'s `crtfastmath` is what would set FTZ, and it is refused above |
| 3 overflow families | `add` then `jo`/`jc` to a trap stub; `+%` masks to width; `+\|` clamps under `cmovo` | exact portable expressions: widths below 64 computed in a wider type and range-checked, 64-bit by pre-check; `__builtin_*_overflow` under `__has_builtin` for speed only; the emitted C performs no signed operation that can overflow, so `-fwrapv`/`-ftrapv` are irrelevant. **Parity** with the reference |
| 4 bit-width fields | shift and mask from the IR's resolved (byte, bit, width, order) | never a C bitfield: `@transitus` is `uint8_t[N]`, every access explicit shift and mask in `unsigned` arithmetic (no promotion into `int`, no shift by ≥ width, no cross-byte unit). Checked against the four rules of spec §5.2: each is arithmetic on bytes and is expressible exactly. **Parity**; the one gap (`u12:maior` under `:minor`) is the spec's, recorded in `ssa-ir.md` |

Items 3 and 4 hold in C at parity because `docs/design/ssa-ir.md` resolves
layout and overflow behaviour before any backend sees them. Items 1 and 2 are
where C is a weakened port: it refuses what it can detect, asserts what it can
observe, and the differential test is the only proof of the rest. That is the
whole reason a reference exists.

**Spec §9.6 and the C compiler.** The interface hash governs rebuilds; cache
identity is the content hash of the module plus its transitive inputs. The C
compiler is on the **content** side: it is a build-platform tool (spec §9.5)
pinned by content hash in the lockfile, keyed by `hostPlatform`, and it does
**not** enter the `ego`. That placement is only sound *because* the reference
backend fixes semantics: if C defined them, every compiler bump would be
interface-affecting and dependents would rebuild on gcc's release schedule.
`proba-reproducibilitatem` diffs two things and reports them separately, never
conflated: `exsc`'s own artifacts (fasmg text, C text, and the reference
binary, since fasmg is in the closure and deterministic) under spec §9.3, and
the C-target binary, whose reproducibility belongs to the pinned toolchain.

## Consequences

**Positive**

- Spec §5.4 becomes testable: every guarantee has a result to diff against.
- Spec §9.3's purity claim stays whole for the artifact `exsc` owns; the
  guarantees do not leak into gcc's flags because the reference does not use
  gcc and the port refuses the flags that would break it.
- The closure stays `{fasmg}` for users of the reference target and for
  self-hosting (spec §18.1, §16).
- `externus` and the C ABI cost nothing on the C target.

**Negative**

- Two backends are more work than one, and two lowerings of every IR op must
  be kept in agreement forever; the differential test is the price of that,
  paid in CI on every change.
- A naive backend still needs an instruction selector for every IR op, SysV
  scalar calls, a trap stub, MXCSR setup, and the phi parallel-copy lowering.
  "Naive is cheap" is an estimate `[OPEN]`; spec §9.2's Zig warning applies in
  full to anything beyond naive, and a register allocator is real engineering
  whenever it is scheduled.
- Aggregates by value across `externus` need SysV classification the
  reference does not have: `[UNIMPLEMENTED]` there, and until it exists the
  reference cannot check that part of the C build.
- The prologue incantations (pragmas, macros, register reads) are stated from
  memory, not run. Every one is `[UNTESTED]`; the first job of the C backend
  is to run them under both compilers at `-O0` and `-O2` with
  `-fsanitize=undefined` and record what held.
- The reference is slow by construction; users on x86-64 who want speed take
  the C target and lose nothing but the closure argument.

**Neutral**

- The C target's failure mode is a `#error` from the downstream compiler,
  which is spec §5.5's "fails the build" delivered late. Where `exsc` can know
  in advance that a host cannot honour `numeri`, it should refuse itself; that
  needs a spec §13 code, none exists, and **none is invented here** — it is a
  spec §13 amendment for the owner.
- A module's `numeri` is a translation-unit property on the C target, which
  matches its per-module scope (spec §5.4).

## Open

- Whether GCC honours `#pragma STDC FP_CONTRACT OFF`, and whether
  `#pragma GCC optimize("fp-contract=off")` is accepted per translation unit.
  `[UNTESTED]`
- Whether a FTZ/DAZ state check at start is enough, or the runtime must
  re-assert after every `externus` return — the same leak spec §9.3 already
  admits for `setlocale`. `[OPEN]`
- Directed rounding (`ad_superius`, `ad_inferius`) on the C target: constant
  folding under round-to-nearest is a compiler assumption the source cannot
  forbid portably (`FENV_ACCESS` is unimplemented in GCC as far as this ADR
  knows). `[OPEN]`
- The lockfile's shape for pinning a per-host C toolchain is not designed.
  `[OPEN]`
- The one spec gap the four problems exposed — `u12:maior`-class fields under
  `:minor` — needs spec §5.2 to decide before either backend can implement it.
