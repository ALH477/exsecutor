# 0016 — `&mutabilis T`: a mutable borrow, and the `firma` hole it closes

**Status:** Accepted as design, 2026-09-12. **Nothing implemented.** The design
is `docs/design/mutable-borrow.md`. This ADR exists because the change is two
things at once — a language feature three milestones have asked for, and the
fix for a soundness defect measured while designing it — and because it
corrects a misattribution that six places in this tree repeat.

**Relates to:** spec §6.3 (decision 3, amended), §8.4, §8.6 (the `Type`
grammar, amended), §13 (`EXS-E0306`, and `EXS-E0310` added);
`docs/design/ssa-ir.md` section 2.9 (amended), `docs/design/typed-ast.md` section 2.5 (an
`[OPEN]` closed); ADR 0015 (which needs this to put a reader in a ROM);
`docs/design/receptor.md` finding 11, `docs/design/c-backend.md` findings 12
and 14.

## Context

### The defect

Measured on `e8de1aa`:

```
publica functio imple(v: acies<u8, 4>) -> u8 { v[0] = 90; redde 0; }

firma b: acies<u8, 4> = [65; 4];   // 'A'
firma i = imple(b);
s.scribe_octeto(b[0]);             // prints 'Z'
```

**It prints `Z`.** A `firma` binding's element was mutated. The direct write
`b[0] = 1` on the same binding is correctly `EXS-E0306`; laundering it through
a call is accepted, compiles, and writes. So immutability is enforced on the
syntax of an assignment and not on the storage, and `firma` is violable by any
function that takes an aggregate.

`checker/types/types.inc:1326` anticipated the mechanism — its `[OPEN]` says
"a PARAMETER root is still writable here … `functio f(v: acies<u8, 2>) { v[0]
= 1; }` should be refused too" — but nothing recorded that the consequence was
a violable `firma`, and no fixture exercised it in either direction.

The same accident is, exactly, the feature three milestones have wanted.
Mutation through a borrowed aggregate parameter **already works**: the write
lands in the caller's storage, through both backends, for `acies` elements and
for `structura` fields alike. What is missing is not a mechanism. It is a rule.

### The misattribution

Six places say a function cannot fill an array it was handed *because
parameters are borrowed under spec §6.3 decision 3*:
`docs/design/receptor.md` finding 11; `docs/design/c-backend.md` findings 12
and 14 and the §6.2 table; `examples/streamdb/lector_streamdb.exsc:96-104`;
`examples/hydramodem/recipe.exsc:29-33` and `circuitus.exsc:30-32`; and ADR
0015's Open section.

§6.3 decision 3 is one sentence:

> **Parameters are borrowed by default** (`guaranteed` convention). Retaining
> on each visit costs +85%.

It is about **retains**. §6.3 contains no mention of writing at all. The
read-only character of an aggregate parameter is `docs/design/ssa-ir.md`
section 2.9's — "aggregates by `ptr` to caller-owned storage the callee reads and
never writes" — restated in `lowering.md:106` and `:454`.

That distinction is what makes this change small and legitimate rather than a
reversal. §6.3's measured decision stands untouched: a mutable borrow still
retains nothing. What is amended is an IR design decision, with a reason, which
is what design decisions are for. §7.2's own modes table already says
ownership is *"deferred. ARC does not preclude adding opt-in ownership later,
as Swift did"* — not rejected.

### Why it matters beyond the defect

ADR 0015 measured the StreamDB reader's deepest live stack at **241 KB**
against libultra's 8–16 KB thread stacks, and named three ways out. This is
the first. `exs_arbor_percurre`'s 127,016-byte frame is, measured by slot:

| | bytes |
|---|---|
| a local `Arbor`, built then copied to the caller's result pointer | 53,252 |
| four field temporaries, built then copied into that local `Arbor` | 49,152 |
| genuine traversal working state | 24,612 |

Writing through a caller-supplied `Arbor` removes the 53,252 from the callee.

**Corrected, 2026-09-12, on measurement.** An earlier draft of this section said
"246,300 → 24,940 bytes of stack", which compared a *total* live stack against a
*callee-only* frame. The honest arithmetic, all on the 32-bit-`mensura` row:

| | bytes |
|---|---|
| `arbor_percurre` today | 127,016 |
| ... of which the `Arbor` the borrow removes | 53,252 |
| ... of which six array-literal temporaries **the borrow does not remove** | 53,248 |
| ... genuine traversal working state | ~20,516 |

The 53,248 is a *second* defect, independent of borrowing: a struct literal
lowers each aggregate field to its own slot and then copies it in, so the six
`[0; 2048]` initialisers cost the struct's size twice. Measured on a minimal
case — `structura S { n: mensura, v: acies<u64,2048> }` built from a literal
emits **two** slots, 16,392 and 16,384 — while a bare
`mutabilis x: acies<u64,2048> = [0; 2048]` emits one. So the borrow alone would
*relocate* those temporaries to whichever caller initialises the storage, not
remove them: measured, initialising an `Arbor` costs a caller 139,328.

Both levers together take the callee to ~20,500 and leave the caller paying
53,252 for storage it needs anyway — which is what lets decision 4 stay strict
rather than trading a safety rule for a number. Kiln's own stack is the
callee's, because the engine holds the `Arbor` in its own storage.

## Decision

### 1. The spelling is `&mutabilis T`

`Type ::= ('&' ['mutabilis'] | '*')* CoreType …`. `&T` is an immutable borrow;
`&mutabilis T` is a mutable one.

Three reasons, in the order they decided it:

- **It spends no root-space.** §8.4 is explicit that reserving a word spends a
  morpheme root permanently, which is why most of the vocabulary is
  contextual. `mutabilis` is *already* a tier-1 reserved word for exactly this
  concept. A new word — `exiens`, `implendus` — would spend a root to say what
  an existing word says, against §8.4's own economics and the precedent of
  §8.6 decision 4, which rejected `implet` on that ground.
- **It needs no new peek.** §8.6 requires the grammar to be LL(1) with an
  enumerated set of two-token peeks. A modifier *before the parameter name* —
  `functio f(exiens v: T)` — is ambiguous with the name itself until the token
  after it is seen, so it would add a peek. In **type** position, `IDENT ':'`
  has already committed, and after `&` a reserved word cannot be a type name.
  Zero grammar risk.
- **It closes a standing `[OPEN]` instead of opening one.**
  `typed-ast.md`'s borrow row has said since it was written that "what `&`
  means beyond address-of is not settled". Now it is: address-of, plus a write
  permission when marked. Ownership stays deferred, because neither form
  transfers any.

Rejected: `mutabilis T` without the `&` — it reads as a mutable local copy,
which is the opposite of what happens. Rejected: a second sigil — §8.6
already spends care on keeping `<` unambiguous, and a third sigil convention
reads worse than one reserved word already in the table.

### 2. An unmarked write through a parameter is `EXS-E0306`

This is the defect fix, and it needs no new code: §13's `EXS-E0306` is
"assignment to an immutable or non-lvalue target", and `checker/types/stmt.inc:33`
already lists "or a parameter" among the things it covers. Widening
`__chk_ty_rootmut` to stop at a parameter root closes its own `[OPEN]`.

It is a **breaking change** in principle, and measured, it breaks nothing:
with the refusal in place the suite is 1381 pass / 0 fail. **Nothing in this
tree wrote through a parameter** — every program was written to the rule its
authors believed was in force, which is why the defect went three milestones
without being noticed. (An earlier draft of this ADR guessed "exactly one
construct does". That was written before it was checked, and it was wrong.)

### 3. The call site is implicit, and the argument must be a `mutabilis` binding

`imple(b)`, not `imple(&mutabilis b)`. An aggregate argument is **already**
passed by address today with no sigil, so requiring one only in the mutable
case would make this the single place in the language where an aggregate
argument needs decoration. The signature carries the permission, and there is
no overloading, so the callee is always statically known from the call.

An explicit call-site form is `[OPEN]`: `&mutabilis b` in *operand* position
does not parse (only the `Type` grammar gained the marker), and adding it would
be a second §8.6 amendment for readability rather than for meaning.

The consequence is a coercion rule, which is M3's work: an argument of type `T`
coerces to a `&mutabilis T` parameter when, and only when, its root is a
`mutabilis` binding.

`imple(b)` where the parameter is `&mutabilis` and `b` is `firma` is
`EXS-E0306`. Without this, decision 2 moves the hole rather than closing it:
marking the parameter would grant permission the caller never had. A `firma`
binding may still be passed to an unmarked parameter, which is the ordinary
read-only case and stays free.

### 4. Definite assignment is unchanged, and stays the caller's obligation

Measured: passing an uninitialised `mutabilis b: acies<u8, 4>` to a filling
function is already `EXS-E0307`, because §6.3 decision 5 makes an element
write a read of its base. The callee's writes do **not** discharge the
caller's obligation.

Keeping that is deliberate. Transferring it would mean proving the callee
writes every element on every path — an analysis this language does not have
and §1.1 says it will not grow — and it buys almost nothing: `[0; N]` is one
statement, and static storage is zero before `main`. The cost is one
initialising write the optimiser may not remove; the alternative is a
whole-program analysis or an unsound promise.

### 5. Two arguments of one call may not alias when either is mutable

`f(b, b)` where one parameter is `&mutabilis` is `EXS-E0310`. Today it is
accepted, and the callee then reads through one name storage it is writing
through the other.

This needs a new code, so §13 is amended **first**, per CLAUDE.md — `E0310`,
the next free number in the `03xx` types range, "aliased mutable argument".
Nothing existing fits: it is not immutability (`E0306`), not a type mismatch
(`E0303`), not an operation undefined on a type (`E0305`), and not control
flow (`E0307`).

The rule is **syntactic and conservative**: it compares the root binding of
each argument, and refuses when a mutable one repeats. It does not reason
about offsets, and it does not see aliasing through a `refero` or a `*T`. That
is a deliberate under-approximation, and `docs/design/lowering.md`'s hazard H5
— the same problem for the *hidden* result pointer, `s = f(s)` — stays
`[OPEN]` and is not fixed here.

### 6. Not permitted in a function type

`AST_TY_FN` records parameter types as a run of ids with no room for a
per-parameter bit, so a mutable borrow in a function *type* — and therefore
through a higher-order call or a `dyn` — is refused and stays `[OPEN]`. A
declaration is the only place it may appear. This keeps the change out of the
one structure where it would be a representation change rather than a rule.

### 7. Why not `*T` and `Crudum`

`*T` already exists, already admits a write, and already costs the `Crudum`
capability. A reviewer should ask why that is not the answer, and the answer
is that the two grant different things. `Crudum` is **ambient** authority over
raw memory: a function holding it may fabricate any address. A mutable borrow
is authority over **one object the caller named at one call site**, and it is
visible in the signature. Spending `Crudum` on an output parameter would
over-grant in precisely the way §10.3's audit exists to expose, and would put
every embedded reader — the whole point of the exercise — behind a capability
a ROM has no business holding.

### 8. A dereference of an immutable borrow is not writable

Found while implementing decision 1, and it would have made the whole feature
decoration. `__chk_ty_rootmut` stops its walk at a dereference and answers
"writable", on the correct ground that `*p = v` writes what `p` points at and
`firma p: *u8` is an immutable *pointer* to a mutable place. That is right for
`*T` and wrong for `&T`: an immutable borrow points at a place the callee may
not write. So `(*v)[0] = 1` through a `&acies<u8, 4>` was accepted.

It is now `EXS-E0306`. The walk refuses a dereference through a path whose
declaration is an immutable borrow, and keeps the old answer for a dereference
of anything that is not a plain path — the conservative direction, and `[OPEN]`
because there is no way to write one today that reaches the check.

## Consequences

**Positive**

- `firma` becomes enforceable rather than advisory. That is worth more than
  the feature.
- The reader's stack drops roughly 10×, which is what ADR 0015's `[OPEN]`
  needed, and the `Arbor` becomes movable to static storage.
- Two design documents stop citing the spec for a rule it does not contain.
- `receptor.md` finding 11's duplicated accumulate loop — written twice in
  `recipe.exsc` and `circuitus.exsc` because a function could not fill an
  array — becomes expressible once.
- Nothing in the IR, the lowering or either backend changes. The permission is
  a Stage 2 fact, erased before any backend sees it.

**Negative**

- It is a breaking change: a program that wrote through an unmarked parameter
  stops compiling. Deliberate — that program was violating `firma`.
- The aliasing rule is an under-approximation, and says so.
- One more code is spent, permanently.
- `&` becomes load-bearing in source for the first time; `c-backend.md`
  finding 15 records that `&` is written in no `.exsc` file in the tree, so
  every stage of it is `[UNTESTED]` until this lands with fixtures.
- The six-argument ceiling is untouched and still counts a hidden result
  pointer. A mutable borrow is net-zero words — it removes the hidden pointer
  and adds a declared one — so it does not relieve `suffixum_percurre`, which
  is already at the limit for a different reason.

## Open

- **A mutable borrow in a function type** — decision 6. `[OPEN]`
- **Aliasing through a `refero`, a `*T`, or two different offsets into one
  object** is not detected. `[OPEN]`
- **`lowering.md` hazard H5**, the same aliasing question for the hidden
  result pointer (`s = f(s)`), is untouched and still `[UNTESTED]`.
- **`&T` on a scalar** is not part of this change. `AST_F_ADDRTAKEN` is
  defined, tested once, and set by nothing, so `&scalar_local` lowers to the
  value rather than the address — a separate defect, not reached by an
  aggregate parameter, which is already an address. Recorded so that whoever
  writes `&x` next finds it. `[OPEN]`
- **A borrow type node has `AstType.width == 0`**, so anything that gives a
  borrow its own slot would size it at zero bytes. Not reached while a borrow
  parameter is an aggregate pointer. `[OPEN]`
