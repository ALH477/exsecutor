<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 The Exsecutor authors.
-->
# Adversarial semantic audit — follow-up work (2026-10-03)

The audit of 2026-10-03 (commit `be2dad3`, merged as PR #16) fixed four
accept-should-reject / backend-divergence defects: the trait per-member
ceiling laundering (`__chk_row_e0510`), the signed-literal positive-overflow
acceptance (`__chk_ty_fits`), the C-backend `ftoi` normalization divergence
(`backend_c/emit_c.inc`), and the missing `EXS-E0511` on `e sicut dyn I` with
no implementation. Each is pinned by a regression that fails under mutation.

This note records what the same audit found but deliberately **did not** fix,
so it is tracked rather than rediscovered. None is a running capability
escape; the ordering below is by how much it weakens a claim, not by how
loud it is. Severity, the shortest reproducer, the root cause, and the
recommended fix are given so the next change starts from the counterexample,
not from this prose.

## 1. [MEDIUM] `sub P = e` masks a call-induced draw of `P` in the checker (TCB)

```exsecutor
functio getatom() -> archivum poscit archivum { redde archivum; }
functio f(d: Directorium) -> u8 poscit sicut d {
    sub archivum = getatom();   // accepted; `firma z = getatom();` is EXS-E0421
    redde 0;
}
```

The call `getatom()` draws `archivum`, which `f` (row only `sicut d`) does not
hold, so it should be `EXS-E0421` — and it is, at **lowering**, on both
backends, so no binary is produced and ADR 0017 R1 holds at the binary level.
But the **checker accepts it**: `compute.inc`'s D2 provider resolution is
function-scoped and flow-insensitive, so a `sub archivum = e` registers `f` as
an `archivum` provider for the whole body, including the draw inside its own
RHS `e` (self-provision) and calls that textually precede the `sub`. The D2
comment documents this as a benign over-approximation ("accepts what a frame
walk would reject, never the reverse"); it is benign for *lowering* but it
means the **checker is not the authority** for this capability guarantee, which
CLAUDE.md says it must be.

**Recommended fix:** at minimum, a `sub P = e` must not provide `P` to a draw
inside its own initializer `e` (self-provision is unambiguously wrong — the
`sub` does not exist when `e` runs). The flow-insensitive "earlier call masked
by a later `sub`" case is a larger change the D2 design argues against;
decide whether the TCB-authority requirement overrides that.

## 2. [MEDIUM] Sub-64 `iN::MIN` cannot be emitted (`-N` literal)

```exsecutor
firma a: i8 = -128;    // type-checks; the emitter refuses iconst i8 128
firma b: i64 = -9223372036854775808;   // type-checks; TRAPS at run (0 - MIN)
```

The 2026-10-03 fix made the **checker** admit a negated `2^(w-1)` as MIN while
refusing a bare positive one. The **lowering** was left as-is: `-e` lowers as
`0 - e`, and `iconst iN 2^(w-1)` is not a canonical value of `iN` (IR §2.2),
so a sub-64 MIN fails at emit, and at width 64 `0 - i64::MIN` overflows and
traps. So a program needing `iN::MIN` from a decimal literal cannot be built.

**Recommended fix:** fold `-`<integer literal> into one `iconst iN` of the
two's-complement value in `__lwr_unary` (i8::MIN → `iconst i8 -128`). Touches
the lowering and both backends' golden text, so it is a change with a wide
diff, not a one-liner. Already noted inline in `lowering.md` (the `Unary -`
row) and spec §5.4.

## 3. [LOW] A `typus` alias of a capability atom misses `EXS-E0422`

```exsecutor
typus Amb = ambitus;
publica functio f(a: Amb) -> u8 poscit alloc { sub ambitus = a; redde 0; }
// direct `a: ambitus` with the same body is correctly EXS-E0422
```

Not an authority escape (the parameter carries the atom visibly either way),
but alias resolution is **non-uniform**: the use path and the `EXS-E0303`
provider-type path resolve `Amb` to `ambitus`, while the `EXS-E0422`
double-bind check reads the written type spelling via `ast_cap_of_name`
(`resolve.inc`) with no alias resolution, so the parameter is never registered
as a provider and the adjacent `sub` binds freely. The sibling-hole soft spot
CLAUDE.md warns about. **Recommended fix:** resolve `typus` aliases before the
double-bind test, so all three paths canonicalise identically.

## 4. [LOW] `EXS-E0342` over-rejects a `rumpe` of an inner loop under `contrahe`

```exsecutor
quisque i in 0..4 contrahe acc: + {
    per j in 0..4 { si j gt 2 { rumpe; } }   // breaks the INNER loop; refused
    acc = i;
}
```

An over-rejection (safe direction): the `rumpe` exits the inner `per`, not the
reduction, so the reduction body still completes. The checker matches §8.5's
*literal* wording ("`rumpe` … inside an iteration carrying a `contrahe`") but
that is broader than its *rationale* (an early exit of the reduction makes the
result depend on which iterations ran). **Recommended fix** is a spec decision:
narrow the rule (and `stmt.inc`) to a `rumpe` that targets the `contrahe`-
carrying loop itself.

## 5. [LOW] Reading a prelude `Scriptor`/`Lector` capability field SIGILLs the compiler

```exsecutor
firma s = Scriptor.ad_exitum(m.ambitus());
firma x = s.a;    // type-checks; exsc exits 132 (SIGILL) in lowering, both backends
```

A compiler-robustness bug, not a capability escape (`ambitus` from a value
that already holds it). `Scriptor`/`Lector` expose their capability fields
(`interface.inc` rows) as readable members; `Directorium` hides its fields
with `EXS-E0305` (ADR 0017 R3). The field read type-checks and then the
lowering has no case for a prelude field member, so it traps.
**Recommended fix:** make the field read `EXS-E0305` for consistency with
`Directorium`, or give the lowering a clean refusal — never a SIGILL.

## 6. [LOW] `poscit {}` is cited by spec §4.1 rule 6 but does not parse

Rule 6 names "any function written `poscit {}`" as the form for an
explicitly-empty (pure) declared row, but the grammar's `DeclRow` has no brace
form (only `TypeRow`, in type position, is braced), so `poscit {}` as a
declaration is `EXS-E0201`. A private function therefore has no way to declare
an explicitly-empty row — it always infers (rule 5). **Recommended fix** is a
spec/grammar decision: add a braced-empty `DeclRow`, or amend rule 6 to stop
citing a form that does not parse.

## What was attacked and held (so a later pass need not re-derive it)

- Capability provenance: an atom cannot be forged by naming it (`EXS-E0421`),
  minted by cast (`EXS-E0305` whitelist — value and reference casts both),
  `sub`-minted from a non-atom type (`EXS-E0303`), or read out of a
  `Directorium` field (`EXS-E0305`). The `sicut`-coverage family is
  conservatively sound (coverage gated by the caller's own `sicut`-parameter
  authority), including reordered members and forwarded capability-bearing
  structs.
- Reference-vs-C differential: arithmetic edges, control flow, `@transitus`
  byte views and the one aggregate cast agree byte-for-byte; the `ftoi` cast
  was the sole divergence and is fixed.
- `@transitus` cast size sums bits, not byte-rounded fields.
- `quisque` is a trusted assertion as labelled: a misdeclared one compiles
  identically to `per` (no miscompile); `contrahe` guards (E0341/E0343) hold.

## Not reachable today, so not yet falsifiable

- The `textus` `{ptr,len}` arena-escape (spec §5.1) has **no running
  counterexample**: the only `textus` values are string literals (static
  storage); arena-backed text is not constructible (the view/slice methods are
  refused by the lowering, there is no runtime text producer). The hazard is a
  future invariant to enforce *before* arena-backed text lands, not a present
  defect.
- ARC double-free / resurrection / use-after-free: `refero<T>` is not
  constructible from source (conformance entry 15 `DEFERRED`), so these are
  exercised only by runtime unit fixtures. The documented aliasing blind spots
  (`refero`/`*T`/hidden return pointer — `lowering.md` H5) stay `[OPEN]`.
