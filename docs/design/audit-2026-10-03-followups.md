<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2026 The Exsecutor authors.
-->
# Adversarial semantic audit — follow-up work (2026-10-03)

The first audit of 2026-10-03 (commit `be2dad3`, merged as PR #16) fixed four
accept-should-reject / backend-divergence defects: the trait per-member
ceiling laundering (`__chk_row_e0510`), the signed-literal positive-overflow
acceptance (`__chk_ty_fits`), the C-backend `ftoi` normalization divergence
(`backend_c/emit_c.inc`), and the missing `EXS-E0511` on `e sicut dyn I` with
no implementation.

This note originally recorded six further findings the same audit surfaced but
did not fix. **All six are now fixed** (this branch). Each landed as one
commit with a mutation-verified regression; the three that touch the
capability system or the prelude's capability types were additionally broken
against with running exploit programs by a separate `fable` adversary and
merged only on a CONFIRMED verdict, per CLAUDE.md. The pre-existing hazards
those adversary passes surfaced — none a running capability escape — are
recorded at the end so the next pass starts from the counterexample, not from
this prose.

## Resolved

### 1. [MEDIUM] `sub P = e` provided `P` to a draw in its own scope — checker was not the TCB authority (`53d3bd1`)

```exsecutor
functio getatom() -> archivum poscit archivum { redde archivum; }
functio f(d: Directorium) -> u8 poscit sicut d {
    sub archivum = getatom();   // was accepted at the checker; now EXS-E0421
    redde 0;
}
```

`compute.inc`'s D2 provider resolution was function-scoped and flow-insensitive:
a `sub archivum = e` registered `f` as an `archivum` provider for the whole
body, so the draw inside its own initializer `e` (self-provision), draws in
sibling branches, and calls textually before the `sub` were all covered. The
lowering's frame walk rejected them (`EXS-E0421`, both backends), so no binary
ever escaped — but the checker, which CLAUDE.md requires to be the authority,
did not.

**Fix (full closure):** a `sub`'s provision is now its own **lexical scope**,
mirroring the §4.5 frame walk. `wprov` holds only capability *parameters*; a
new `wblk` map gives each node its innermost enclosing block; `__chk_row_eff`
covers a node with a `sub` only when the node lies in that `sub`'s scope
(statement form: after the `sub` to the end of its block; block form: the block
body; never its own `Sub.b`, never a sibling branch, never a nested lambda).
Built additively, so a draw covered by another in-scope provider is not
over-rejected. A 43-program differential (checker vs both lowerings) showed
**zero over-rejections** and checker/lowering `EXS-E0421` equality on every
non-SIGILL case — D2 now holds at equality, not one-sided over-acceptance.
Adversary verdict: **CONFIRMED** (regression and sibling/earlier over-accept
both closed; no over-rejection across 78 programs; no new crash).

### 2. [MEDIUM] Sub-64 `iN::MIN` could not be emitted (`c238a2c`)

```exsecutor
firma a: i8 = -128;                    // was: emitter refused iconst i8 128
firma b: i64 = -9223372036854775808;   // was: TRAPS at run (0 - i64::MIN)
```

`-e` lowered as `0 - e`, and `iconst iN 2^(w-1)` is not a canonical value of
`iN` (IR §2.2), so a sub-64 MIN failed at emit and i64::MIN overflow-trapped.
**Fix:** `__lwr_unary` folds `-`<integer literal> into one canonical `iconst iN`
(signed-literal only — unsigned `-V` still traps on underflow; non-literal `-x`
still lowers as `0 - x`). Regression `tests/programs/integri_minimi` constructs
i8/i16/i32/i64 MIN and agrees across the reference and C backends. `lowering.md`
(`Unary -`) and spec §5.4 updated; the `[OPEN]` notes resolved.

### 3. [LOW] A `typus` alias of a capability atom missed `EXS-E0422` (`70bc067`)

```exsecutor
typus Amb = ambitus;
publica functio f(a: Amb) -> u8 poscit alloc { sub ambitus = a; redde 0; }
```

Alias resolution was non-uniform: pass 2 resolved `Amb` to `ambitus`
everywhere, but pass 1's provider registration read the written spelling via
`ast_cap_of_name` with no alias resolution, so the parameter was never a
provider and the adjacent `sub` bound freely (and a draw mirrored the defect
with a spurious `EXS-E0421`). **Fix:** `__chk_type_atom` canonicalises through
`typus` aliases, bounded only by `ast_decl_count` (pigeonhole cycle detection,
the structural equivalent of pass 2's visited-set) so the two passes cannot
diverge at any depth. Adversary verdict: **CONFIRMED** (an earlier 32-hop
bound was itself walked past at depth 33 — INSUFFICIENT — and closed; depth
33/40/300/2000, cycles one `EXS-E0303`, no over-fire, no over-rejection).

### 4. [LOW] `EXS-E0342` over-rejected a `rumpe` of an inner loop under `contrahe` (`3fc9e03`)

```exsecutor
quisque i in 0..4 contrahe acc: + {
    per j in 0..4 { si j gt 2 { rumpe; } }   // was refused; now accepted
    acc = i;
}
```

The checker fired on any `rumpe` with a `contrahe` loop anywhere on the stack,
broader than §8.5's rationale (an early exit of the *reduction* makes the result
order-dependent). **Fix:** `EXS-E0342` now fires only when the `rumpe`'s nearest
enclosing loop is the `contrahe`-carrier (labels are unsupported, so target
resolution is nearest-enclosing). A `rumpe` of a strictly-inner loop is
accepted and runs; a `rumpe` of the reduction loop itself, and a reduction
nested in a plain loop, stay `EXS-E0342`. Spec §5.4/§13 narrowed to match the
rationale (text narrowed, code moved to match; the number is permanent).

### 5. [LOW] A prelude `Scriptor`/`Lector` capability-field member SIGILLed the compiler (`165e7a4`)

```exsecutor
firma s = Scriptor.ad_exitum(m.ambitus());
firma x = s.a;    // was: exsc exits 132 (SIGILL) in lowering, both backends
```

`Scriptor`/`Lector` exposed their capability fields as readable members; the
read type-checked and then lowering had no case for a prelude field, so it
trapped. **Fix:** a member access on a prelude capability-bearing receiver
(through any `&`/`refero`/pointer layers) is now a *closed set* — only the
type's own real methods resolve; any other name, read or call, is `EXS-E0305`
at the checker, never reaching the impl scan or the module-wide member hint.
This is the uniform rule `Directorium` already followed (ADR 0017 R3). Adversary
verdict: **CONFIRMED** (a first attempt dropped only the field rows, which let
`s.a()` fall to the last-resort hint and SIGILL with a colliding user member —
REGRESSED — and the closed-set guard closed it; real prelude methods still
compile byte-identically, even under a module redeclaring all their names).

### 6. [LOW] `poscit {}` is cited by spec §4.1 rule 6 but did not parse (`be90dcd`)

Rule 6 named `poscit {}` as the explicitly-empty declared row, but `DeclRow`
had no brace form, so it was `EXS-E0201` — a private function could not declare
an empty row, only infer one. The checker's pass 1 and pass 3 already read a
present `Row` node as "declared"; only the parser was missing. **Fix (option
A):** a minimal braced-empty `DeclRow` in `cst/parse.inc` so `poscit {}` parses
to an empty row (the spec was honest, the grammar/parser were behind it). Spec
§4.1/§8.6 updated. A corner the new form exposed and fixed: `EXS-E0421`'s
machine-applicable fix must replace an empty written row wholesale, not append
`, atom` to `{}`.

## What was attacked and held (so a later pass need not re-derive it)

- Capability provenance: an atom cannot be forged by naming it (`EXS-E0421`),
  minted by cast (`EXS-E0305` whitelist — value and reference casts both),
  `sub`-minted from a non-atom type (`EXS-E0303`), or read out of a
  `Directorium` field (`EXS-E0305`). The `sicut`-coverage family is
  conservatively sound (coverage gated by the caller's own `sicut`-parameter
  authority), including reordered members and forwarded capability-bearing
  structs.
- `sub`-provision now matches the lowering's frame walk at equality (finding 1):
  self-provision, sibling-branch and earlier-call shapes are all `EXS-E0421` at
  the checker, with no over-rejection across 78 adversary programs.
- `typus`-alias provider resolution matches pass 2 at every depth (finding 3):
  no finite alias-chain depth evades `EXS-E0422`, no non-capability alias is
  wrongly registered as a provider.
- Prelude capability receivers are a closed member set (finding 5): no
  field/unknown/colliding-name access reaches the lowering; all eleven atoms
  and all five record types are covered through reference/`refero`/pointer
  layers.
- Reference-vs-C differential: arithmetic edges, control flow, `@transitus`
  byte views and the one aggregate cast agree byte-for-byte; the `ftoi` cast
  was the sole divergence and is fixed.
- `@transitus` cast size sums bits, not byte-rounded fields.
- `quisque` is a trusted assertion as labelled: a misdeclared one compiles
  identically to `per` (no miscompile); `contrahe` guards (E0341/E0343) hold.

## Pre-existing hazards surfaced by the adversary passes (not fixed here)

None is a running capability escape, and none was introduced by the six fixes
above (each verified identical on the pre-fix binary). They are recorded as the
counterexamples for a later pass. Most are the same lowering soft spot — the
member/value/generic paths that trap in `__lwr_*` instead of a diagnostic —
seen from different angles, consistent with the checker-only status of the
generics × dispatch × closure surface.

- **The member-lowering TCB still traps (`ud2`/exit 132) on well-formed-ish
  input**, independent of finding 5's closed-set guard:
  - a prelude *method used as a first-class value* — `firma f = s.scribe;`,
    `m.ambitus` uncalled, bare `firma x = Scriptor;`, `Mundus.ambitus` as a
    value — traps at `__lwr_member_read` (`Member.d == 0`, unimplemented).
  - the last-resort module-wide member hint is still reachable for any
    *non*-prelude-capability receiver: a user impl-method **call** on a plain
    user struct, and an `eventus<Scriptor, erratum>` receiver with a colliding
    member, both trap. Restricting the hint for all receivers is a wider
    checker change than finding 5 took.
  Recommended: give each a `[UNIMPLEMENTED]`→diagnostic refusal; never a SIGILL.
- **Capacity / depth `rassert`s SIGILL on large or awkward modules:** a module
  with ≥~2500 chained (or ≥~4000 unchained) `typus` declarations, or ~5000
  plain functions; and pass 2's `CHK_TY_DEPTH_MAX` (~32) on a *reverse-ordered*
  (use-before-definition) `typus` chain deeper than ~32 or a pure alias cycle
  of ~40. Pre-existing capacity limits, not a soundness hole (no program is
  accepted); worth a graceful diagnostic.
- **`&ambitus` reference parameter is not registered as a provider:**
  `f(a: &ambitus) poscit alloc { sub ambitus = *a; }` is accepted with no
  `EXS-E0422`. Cosmetic non-uniformity only — the deref already yields the atom
  the holder has — but the same class finding 3 fixed for value-typed aliases.
- **Lambda in a `mutabilis` local launders an atom through `sub`:**
  `mutabilis g = functio() -> archivum { redde getatom(); }; sub archivum = g();`
  is accepted by the checker (the §4.2/§4.6 `[OPEN]` "a `mutabilis` local lambda
  is read at its type's row"); the lowering traps rather than refusing. No
  running escape (no binary is produced).
- **A lambda body does not reset the loop context:** a `rumpe`/`perge` inside a
  lambda that sits in a loop is accepted where it should be `EXS-E0307`
  (lowering of any lambda traps regardless).
- **Spec §8.6 decision 7 is stale (or the code is):** the spec says a `sicut`
  item in a braced type row is `EXS-E0201`, but both backends give `EXS-E0423`
  for `functio(f32) -> f32 poscit {sicut g}`. A spec/implementation divergence
  to reconcile.
- **A named function used as a value** is `EXS-E0303` against any function type
  even with no `poscit` — part of the checker-only named-fn-as-value surface.

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
