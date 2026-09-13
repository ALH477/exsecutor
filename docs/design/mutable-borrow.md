# `&mutabilis T` — design

The decisions are ADR 0016. This is the implementation: what changes, where,
in what order, and what each step is checked by.

Its subject is small and so, measured, is its blast radius. Nothing in the IR,
the lowering or either backend changes — the whole feature is a **permission**,
decided in Stage 2 and erased before any backend sees it, exactly as a
capability row is (`ssa-ir.md` section 2.9, amended). And closing the defect
breaks no program in this tree: 1381 pass / 0 fail with the refusal in place.
Nothing here ever wrote through a parameter, which is precisely why a violable
`firma` survived three milestones.

## 1. What is already true

Measured on `e8de1aa`, before any change:

| construct | today |
|---|---|
| `v[0] = 1` through an `acies` parameter | **compiles, runs, writes the caller's storage** |
| `p.a = 1` through a `structura` parameter | **compiles, runs, writes the caller's storage** |
| `v = other` — the whole aggregate | `EXS-E0306` |
| the same, where the caller's binding is `firma` | **accepted** — the defect |
| the same, where the caller's binding is unassigned | `EXS-E0307` |
| `f(b, b)`, one argument written | **accepted** — no aliasing rule |
| `&T` in a parameter type | parses, checks, lowers through both backends |
| `&mutabilis T` | `EXS-E0201` — the grammar has no such form yet |
| `&` anywhere in any `.exsc` in the tree | **written nowhere** (`c-backend.md` finding 15) |

So the mechanism exists and the rule does not. Two consequences for the plan:
the lowering needs nothing, and every stage of `&` is `[UNTESTED]` in §8.6's
sense until this lands with fixtures.

## 2. The passes, and what each must learn

**Parser — `cst/parse.inc`.** `__cst_type`'s `.sigil` arm reads a leading `&`
(`:2232-2245`). After it, accept an optional `mutabilis` keyword token. No new
peek: the arm has already committed to a type, and after `&` a reserved word
cannot begin a type name. One new CST shape, or one flag on the existing one.

**AST — `ast/`.** `AST_PTR_REF` distinguishes `&T` from `*T`
(`from_cst.inc:893-905`). A third value, or a flag beside it, carries
mutability. `ast/kinds.inc:384` shows `Param.c` is `AST_R_NONE` — a free row
slot — but the mutability belongs on the **type**, not the parameter, so that
slot stays free and `AstType` is where it goes. A borrow type is built by
`ast_type_una` with `width = 0` (`ast/types.inc:202-218`); that zero is
already recorded as an `[OPEN]` hazard and is not disturbed, because a borrow
parameter is an aggregate pointer and never gets a slot of its own.

**Checker, types — `checker/types/sig.inc:158-176`.** The one place that turns
`&T` into `AST_TY_BORROW`. It gains the mutable variant.

**Checker, places — `checker/types/types.inc:1314-1384`, `__chk_ty_rootmut`.**
The whole rule lives here. Today the walk reaches a parameter root and answers
"writable" (`:1374-1376`), which its own `[OPEN]` says is wrong. It must
answer:

- parameter root, type not a mutable borrow → **not writable**, `EXS-E0306`;
- parameter root, type `&mutabilis T` → writable;
- everything else unchanged — including the existing and correct rule that a
  dereference stops the walk, because `*p = v` writes what `p` points at.

**Checker, calls — wherever arguments are typed against a signature.** Two new
rules: an argument bound to a `&mutabilis` parameter must be a `mutabilis`
binding (`EXS-E0306`), and no two arguments of one call may share a root
binding when either is a mutable borrow (`EXS-E0310`). The second is a
syntactic comparison of argument roots, and ADR 0016 decision 5 states exactly
how far it does and does not see.

**Function types.** A mutable borrow in an `AST_TY_FN` parameter list is
refused (`EXS-E0309`, an annotation not applicable in that position), because
`ast/types.inc:224-241` has nowhere to record a per-parameter bit. ADR 0016
decision 6; `[OPEN]`.

**Lowering, both backends — nothing.** `lwr_ty_scalar` already puts
`AST_TY_BORROW` among the register-resident kinds (`lower/ty.inc:113`);
`lwr_ty_ir` already sends it through as `ptr` (`:181-184`); `__lwr_path`'s
`.aggp` arm already returns an aggregate parameter as an address
(`lower/expr.inc:538-540`), and `lwr_store`/`index`/`storebits` are all
address-agnostic. `backend_c`'s `__bfc_emit_ctype` already spells every
address `unsigned char *`. This is the reason the milestone is affordable, and
it is worth checking rather than assuming: M2's gate is that **every existing
emitted unit is byte-identical**.

## 3. Milestones

**M0 — spec, ADR, design.** §13 gains `EXS-E0310` and `codes.inc` is
regenerated (a new code lands *before* any code uses it, CLAUDE.md); §8.6's
`Type` grammar gains `['mutabilis']`; §6.3 decision 3 records what it does and
does not say; `ssa-ir.md` section 2.9 admits the exception; `typed-ast.md`'s borrow
row closes its `[OPEN]`. Gate: `tools/spec-check.sh`, then `spec-guardian`.

**M1 — the defect, fixtured and closed.** A program fixture that mutates a
`firma` binding through a call, pinned as it behaves *today*, so the change is
visible as a diff rather than asserted; then `__chk_ty_rootmut` widened and the
fixture flipped to `EXS-E0306`. Checker unit fixtures for each arm.
**This milestone is a bug fix and is worth landing even if the rest slips.**

**M2 — the grammar and the type.** `&mutabilis T` parses, types, and reaches
the backends. Gate: a CST dump fixture; `&mutabilis` accepted in a parameter
and refused in a function type; and **every existing emitted C unit byte-identical**,
which is what proves the claim in §2 that no backend changed.

**M3 — the call-site rules.** The `mutabilis`-argument rule and `EXS-E0310`.
Fixtures: a `firma` argument refused, a `mutabilis` one accepted, `f(b, b)`
refused, `f(b, c)` accepted, and the aliasing rule's stated blind spots
recorded as fixtures that *pass* so the under-approximation is documented in
runnable form rather than in prose.

**M4 — the reader.** `arbor_percurre` becomes
`arbor_imple(a: &mutabilis Arbor, b: acies<u8, 65536>, s: mensura, n: mensura)`;
`probatio.exsc` supplies the storage. Net argument words are unchanged — the
hidden result pointer goes, a declared one arrives — so the six-register
ceiling is not approached.

Its gate is the one that matters: **the StreamDB certificate stream must be
byte-identical**, all four vendored containers, on both backends and through
the cross phase, while `-fstack-usage` shows the frame fall.

**The gate as first written measured `arbor_percurre` alone, and that was
wrong** — it would pass while the total live stack barely moved, because the
borrow relocates the six array-literal temporaries to whichever caller
initialises the storage rather than removing them. Measure **both** frames,
`exs_arbor_percurre` and `exs_initium`, on the o64 row. Targets: the callee
127,016 → ~20,500, and probatio's total 246,300 → ~139,300. A certificate that
changes means the rewrite changed what the reader computes; a frame that does
not fall means the feature did not do its job. Both are checkable, and neither
is an opinion.

**M5 — the second consumer.** `receptor.md` finding 11's accumulate loop is
written twice, in `recipe.exsc` and `circuitus.exsc`, for exactly this reason.
Collapsing it to one is the feature's second proof and retires that finding.
Gate: every `receptio_*` directory byte-identical.

Dependencies: M0 → M1 → M2 → M3 → M4 → M5. M1 stands alone.

## 4. Error codes, checked against §13

| code | §13 text | used here for | fit |
|---|---|---|---|
| `EXS-E0306` | assignment to an immutable or non-lvalue target | a write through an unmarked parameter; a `firma` argument bound to a mutable borrow | exact. `checker/types/stmt.inc:33` already lists "or a parameter" among what this code covers, and the `[OPEN]` at `types.inc:1326` describes the fix as widening it |
| `EXS-E0307` | control flow misuse (and definite assignment, §13's note) | unchanged — an unassigned argument, by the existing rule | exact, and nothing new is asked of it |
| `EXS-E0309` | type expression or annotation not applicable | `&mutabilis` in a function type | exact: an annotation in a position that cannot record it |
| `EXS-E0310` | aliased mutable argument | two arguments of one call sharing a root when either is mutable | **new.** §13 amended in M0. Nothing existing fits: not immutability, not a type mismatch, not an undefined operation, not control flow |

**Finding on the mapping.** Only one code is new, and only because the class is
new. The two rules that do the real work — the defect fix and the `firma`
argument rule — are both `EXS-E0306`, which is the right answer rather than a
convenient one: both are assignments to a target that may not be assigned to,
which is what the code says.

## 5. What this does not do

- It does not add ownership, lifetimes or a borrow checker. §1.1's non-goals
  and §6.1 stand; ADR 0016 decision 7 gives the argument.
- It does not make `&T` work on a scalar. `AST_F_ADDRTAKEN` is set by nothing,
  so `&scalar_local` lowers to the value rather than the address — a real
  defect, out of reach of an aggregate parameter, recorded in ADR 0016's Open
  section for whoever writes `&x` next.
- It does not relieve the six-argument ceiling, and `suffixum_percurre` is
  still over it for the reason `c-backend.md` finding 14 gives.
- It does not fix `lowering.md` hazard H5 — the same aliasing question for the
  hidden result pointer.
- It does not let a function return a borrow. Nothing here creates a borrow
  that outlives the call, which is the only reason no lifetime rule is needed.
