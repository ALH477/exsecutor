# Sum types, constructor patterns, and `eventus`

**Status: design. Nothing here is implemented.** Every decision below is `D`-numbered
so the spec and the fixtures can cite one rather than quote the argument, and every
claim about what the tree does today was measured on 2026-09-26 at `747fa30` plus the
working tree. Prose designs are hypotheses until code runs; this document is a
hypothesis with its refutation conditions written down.

Owner: the checker's (`compiler/x86_64/checker/`), with the CST's share named in D2.

---

## 1. Why this is one document and not three

Spec §8.6's "Not settled here" list carries these as separate bullets:

> - `[OPEN]` Sum types and constructor patterns; `discerne`'s exhaustiveness
>   presupposes an enumeration the language does not yet declare. The natural home
>   is `typus` — keyword-led, LL(1)-harmless — but it is not decided.

and §11 carries this:

> - **I/O reports failure as `eventus`; a count is never silently short.** […]
>   `examples/imprime.exsc` and §14 entry 12 write `-> mensura` and are `[OPEN]`
>   until `eventus` has its syntax.

They are the same hole seen from two ends. `eventus` is a sum type; there is no way
to declare one; so the I/O surface of the standard library is provisional, and has
been since it was written. Four places in the spec are waiting on this single
decision:

| site | what it says today | why it is provisional |
|---|---|---|
| §4.6 | `Lector.lege_octeto() -> u16` | 256 means *end of input* **and** *read error*, undistinguished. The spec says so and calls it provisional. |
| §4.7 | a `rete` derivation "returns `eventus`" | names a type with no syntax |
| §5.1 | `textus` slicing "returns `eventus`, never panics" | same |
| §11 | `Scriptor.scribe` returns `eventus<mensura>` | same, and the arity is wrong under D5 |

So the cost of leaving this open is not one `[OPEN]` marker. It is that **the
standard library cannot report failure at all**, and every I/O signature in the
tree is a placeholder — `lege_octeto`'s 256 being the sharpest case, since it
silently conflates a clean end of input with a failed `read(2)`.

## 2. What the tree actually does today

Measured, not assumed.

**`discerne` exists and runs.** `tests/programs/discerne/` compiles, lowers and runs
thirty checks through `-o` on both backends: literal patterns, module and local
`firma` patterns, `aliter`, empty arms, nesting, `discerne` inside `per` and
`dum`, `redde` from an arm, and phis at the joins. The lowering is a comparison
chain (`lower/stmt.inc`'s `__lwr_discerne`, `BFA_PRED_EQ` per arm).

**It is not exhaustive, and §8.5 says it is.** §8.5:

> **Exhaustive, with no fallthrough.** A missing arm is a diagnostic that *lists
> the missing cases* and ships the edit.

The fixture's own header contradicts this, in writing:

> NO `aliter` AND NO MATCH RUNS NOTHING (checks 6, 13, 15, 25): the chain's last
> false edge goes to the join. Spec 8.5 says `discerne` is exhaustive, but 8.6
> lists exhaustiveness as presupposing an enumeration that is `[OPEN]`, and 13 has
> no code for a missing arm, so nothing refuses these. Pinned here as what the
> compiler does, not as what the spec settles.

**This is the contradiction to resolve, and the fixture is right.** Per CLAUDE.md the
question is which of the two is wrong, and it is §8.5: it states a property the
language has never had and had no code to report. D3 makes the sentence true; §6
below states what making it true costs, because four of that fixture's checks exist
precisely to pin the behaviour D3 outlaws.

**`?` is lexed, parsed, named, used — and checked, by a rule the spec never
wrote.** `PUN_QUESTION` (`compiler/x86_64/lexer/token.inc`, emitted by
`compiler/x86_64/lexer/lex.inc`), spelled in the CST's punctuation table
(`compiler/x86_64/cst/parse.inc`), admitted by §8.6's grammar as

```
Suffix ::= '.' IDENT | '(' [Expr (',' Expr)*] ')' | '[' Expr ']' | '?'
```

listed in §8.4's sigil table as `` `E?` | error propagation | §5.1 ``, and
**used twice in §5.1's own illustrative block** (`abassus.quaere("/")?`,
`via.sectio(0..ubi)?`). No section of the spec says what it does. The checker
does: `compiler/x86_64/checker/types/types.inc`'s `.try:` arm settles the
operand, requires its kind to be `AST_TY_EVENTUS` — `EXS-E0305` otherwise — and
answers the type's first argument (`AstType.a`). That is an **unconditional
unwrap with no error branch**: no enclosing-function check, no early return,
nothing that could carry an `adversum` anywhere. The lowering refuses the node
by name (`compiler/x86_64/lower/expr.inc`'s dispatcher `rassert`s on `Try`), so
no program containing `?` has ever run. So the language has a postfix operator
with a reserved sigil, a name in a table, two uses in the normative text, no
definition — and an implementation whose meaning is not the one D4 gives it.
That is the contradiction to record: the spec relies on `?` and does not define
it, and the checker defines it differently from this document. D4 settles the
meaning; the checker's arm is what changes to match it.

**`eventus` exists too, as an uninhabited builtin.** It is pool row
`CHK_TY_P_EVENTUS` in `compiler/x86_64/checker/types/prim.inc`, interned by
`compiler/x86_64/checker/types/sig.inc`'s `.eventus:` arm as a one-argument
type (`AST_TY_EVENTUS`, `compiler/x86_64/ast/kinds.inc`) exactly as `refero<T>`
is, and `compiler/x86_64/checker/rows/layout.inc` answers width 0 for it. A
program can spell `eventus<mensura>` and the checker accepts it; nothing can
construct one, match one, or lay one out. Four normative spec sections name
the type (§4.6, §4.7, §5.1, §11); the two that state an obstacle (§4.6, §11:
"until `eventus` has syntax") named the wrong one — the syntax is there, the
*inhabitants* are not — and are respelled to say so.

An earlier version of this section said `?` was "undefined" and its checker
behaviour "unmeasured", inferring the second from the corpus containing no `?`.
Absence of a fixture is not absence of an implementation; the sites above were
there to be read. Recorded rather than silently corrected, per this project's
evidence note.

**No code exists for any of this.** §13 has nothing for a non-exhaustive
`discerne`. That is not an oversight to route around: CLAUDE.md forbids inventing
one, and §13 is the only source, so **the amendment in §5 is a prerequisite to
implementing D3, not a follow-up to it.** ADR 0010's profile codes were held up by
exactly this and §13 records that they "went unwritten for some time; the profile
checker could not have been built without it."

---

## 3. Decisions

### D1 — Sum types are declared with `typus`, and their variants are led by `casus`

```exsecutor
typus eventus<T, E> =
    casus prosperum(T),
    casus adversum(E);

typus modus =
    casus ordinata,
    casus arborea(mensura);
```

Grammar, replacing §8.6's `TypeDecl`:

```
TypeDecl   ::= 'typus' IDENT [GenericParams] '=' (SumBody | Type) ';'
SumBody    ::= Variant (',' Variant)*
Variant    ::= 'casus' IDENT [ '(' Type (',' Type)* ')' ]
```

**Why `typus`.** §8.6 predicted this home and called it "keyword-led,
LL(1)-harmless." It is: after `typus IDENT [GenericParams] '='` the parser peeks one
token, and `casus` chooses `SumBody` while anything else is the existing alias form.
`casus` is a delimiter-like reserved word in this position — no enclosing production
can want it there — so the peek is decision 6's shape exactly, the same argument
that gave `refero <` its production.

**Why `casus` and not a new word.** It is already reserved (§8.4, the selection
group), it already means *case*, and it is the word the matching arm uses, so the
declaration and the pattern that destructures it read with the same vocabulary.
This spends **no root-space**, which §8.4 is explicit is a permanent cost: "Reserving
a word spends root-space, permanently."

**Why `,` and not `|`.** `|` is not a §8.4 token and would have to become one. This
project has refused that trade before for exactly this reason — shifts stayed
`sursum`/`deorsum` and did not become `<<`/`>>`, and the bitwise words stayed words.
A comma-separated list terminated by `;` is punctuation the language already has,
and a variant list is a list.

**Payload-less variants are admitted** (`casus ordinata` above): the parenthesised
type list is optional, peeked on `(`. An enumeration with no payloads is the common
case and should not have to write `()`.

**Recursive sum types are `[OPEN]`.** `typus arbor = casus folium, casus nodus(arbor,
arbor);` is infinitely sized and needs `refero` or a pointer to be representable.
Refusing it needs a reachability check the checker does not have, and admitting it
needs a layout rule §6 does not give. **Not decided here**, and the first
implementation must refuse it by name rather than compute a size for it.

**What replacing the builtin costs, stated because it is easy to underestimate.**
`eventus` is not a name waiting to be declared; it is a shipped primitive with
five sites (§2): the pool row and `CHK_TY_P_EVENTUS` in
`compiler/x86_64/checker/types/prim.inc`; the `.eventus:`/`.una:` arm in
`compiler/x86_64/checker/types/sig.inc` that interns it with one argument; the
`.try:` arm in `compiler/x86_64/checker/types/types.inc`, both its kind test and
its `AstType.a` projection; the width-0 row in
`compiler/x86_64/checker/rows/layout.inc`; and `AST_TY_EVENTUS` itself in
`compiler/x86_64/ast/kinds.inc`, with the by-name refusal in
`compiler/x86_64/lower/expr.inc` keyed off `Try`. Landing D1 as a prelude
`typus` turns every one of those into a redirect or a deletion, and the order
matters: **the builtin is not retired until its replacement is inhabited** —
has variants, a constructor, a pattern and a layout — because four normative
spec sections (§4.6, §4.7, §5.1, §11) name `eventus`, and a tree where the name
resolves to nothing is further from the spec than one where it resolves to an
uninhabited type. Retirement is the last commit of this design, not the first.

### D2 — Constructor patterns, flat and irrefutable-bound

Replacing §8.6's `Pattern ::= Literal | Path`:

```
Pattern ::= Literal | Path | Path '(' IDENT (',' IDENT)* ')'
```

```exsecutor
discerne r {
    casus prosperum(n) { … }      // n is the payload, bound for this arm
    casus adversum(e)  { … }
    }
```

**LL(1):** after the path, one-token peek on `(`. A payload-less variant pattern is a
bare `Path`, which the grammar already admits.

**Consequence, stated rather than discovered: a variant name and a constant name are
syntactically identical in pattern position.** `casus TRES` is today a module `firma`
pattern and would be a variant pattern under D1, and the grammar cannot tell them
apart — resolution does. Two consequences follow. First, a variant and a constant with
the same name in one scope collide, which `EXS-E0302` (duplicate declaration in one
scope) already covers, and no new code is needed. Second, the checker must resolve
the path *against the scrutinee's type first* and only then against the value
namespace, or a constant would shadow a variant silently. That ordering is a
checker rule, not a grammar rule, and it is the subtle part of implementing D2.

**Bindings are plain names, and nested patterns are `[OPEN]`.** `casus
prosperum(alius(x))` is not admitted. This is the decision that keeps D3 cheap:
coverage over a flat variant list is a set-cover over a set the declaration
enumerates, computable with a bitmask, while coverage over nested patterns is the
usefulness-of-a-match-matrix problem and a different order of work. Flat first; if a
program needs nesting, it can `discerne` twice, and the cost of that is one extra
block rather than an unbounded algorithm.

**Binding arity must equal the variant's payload arity**, and a mismatch is
`EXS-E0304` (wrong number of arguments) — the same code a call with the wrong count
gets, because it is the same mistake about the same declaration. A pattern naming a
path that is not a variant of the scrutinee's type is `EXS-E0303` (type mismatch) if
it resolves to something else and `EXS-E0301` (name does not resolve) if it resolves
to nothing. **Three reuses, no inventions.**

### D3 — Exhaustiveness, by two rules, one per scrutinee class

**A sum-typed scrutinee.** Coverage is the variant set the declaration enumerates.
The `discerne` is exhaustive iff every variant is covered by some arm, or `aliter`
is present. A missing variant is the new code of §5, and the diagnostic **lists the
missing variants by name** — which is what §8.5 already promises and what makes it
`exsc emenda`-applicable, the class §8.3 says is suited to a machine-applied fix.

**A scalar scrutinee** (`uN`, `iN`, `mensura`, `u1`). The value space is `2^N` and
enumerating it is not what anyone means by exhaustive. **`aliter` is required.**
Without it the same code fires, saying that a scalar `discerne` needs `aliter`
rather than listing 2^64 absent cases.

That second rule is the whole resolution of §2's contradiction. §8.5's "Exhaustive,
with no fallthrough" becomes true — for a sum type by covering the variants, for a
scalar by requiring the catch-all — and `discerne` stops having a silent
fall-through path to its join.

**`aliter` when coverage is already complete is an unreachable arm**, and this
document deliberately does **not** assign it a code. Today two arms may match the
same value and the first wins (`tests/programs/discerne/`'s `duplex` check pins
it), so refusing unreachable arms is a second behaviour change, independent of D3
and with its own migration. It stays `[OPEN]`. One new code, not two — §8.3 makes
codes permanent, and a code invented for a rule that is not yet decided cannot be
taken back.

**No exhaustiveness over integer *ranges*.** `casus 0..9` is not admitted; the range
operator is not a pattern. Admitting it would make coverage an interval-arithmetic
problem, and D2's flatness argument applies again.

### D4 — `?` gets its meaning, and until it does it should be refused

`e?` where `e : eventus<T, E>`, inside a function whose return type is
`eventus<U, E>` with the **same** `E`, evaluates to `e`'s `prosperum` payload of
type `T`, and otherwise returns `e`'s `adversum` value from the enclosing function
unchanged. It is exactly the `discerne` that D2 admits, written as one token, and it
is why the sigil is worth its permanent place in §8.4.

- The enclosing function's error type must match. A `?` that would need to convert
  `E` to some other error type is **`[OPEN]`**: conversion needs a trait and §7's
  dictionary passing to carry it, and inventing that here would be inventing a
  second feature to justify the first.
- `?` on a non-`eventus` value is **already `EXS-E0305`** — the checker's
  `.try:` arm raises it today (§2). Codes are permanent, so that half is settled
  by the tree and is not renumbered here. The other half — `?` in a function
  that does not return `eventus` — is the one nothing checks; the proposal is
  `EXS-E0307` (control flow misuse), reusing the code that already covers
  `rumpe` outside a loop, since it is the same class of mistake: a construct
  that transfers control used where that transfer has nowhere to go. That
  needs no §13 amendment; §13 has the code.
- **Until D4 is implemented, the checker's present arm should refuse `?` rather
  than unwrap it.** Measured (§2): the arm accepts `e?` on any `eventus`,
  answers `T`, and has no error branch; only the lowering's by-name `rassert`
  keeps such a program from being emitted. A program that type-checks and then
  traps the compiler is the `SIGILL`-on-unsettled-node defect class §8.4
  already records for literal casts, reached a different way. The honest
  interim state is a diagnostic at the `?`; which existing code carries it is
  the implementation commit's question, and §13 must not gain one for a
  temporary state.

### D5 — `eventus` takes two type parameters, and the spec's three spellings are wrong

```exsecutor
typus eventus<T, E> =
    casus prosperum(T),
    casus adversum(E);
```

The prelude's I/O then reads `-> eventus<mensura, erratum>`, where `erratum` is a
prelude `structura` carrying the syscall's own error number — a `u16`, the shape the
`ambitus` atom's syscalls actually return, and nothing ambient (§9.3: no errno
global, the value is the return the syscall gave).

**Why not `eventus<T>` with a defaulted second parameter.** Defaulted type
parameters do not exist in §7 and are not on any open list. Writing
`eventus<mensura>` requires inventing them as a side effect of declaring a sum type,
which is the kind of accidental second feature D4's error-conversion bullet also
refuses. Two parameters, always written.

**So three spellings in the spec are one parameter short**, and this is a finding
rather than a nuance: §11 writes `eventus<mensura>` twice, §4.6 writes
`eventus<u8>`, and §5.1 says slicing "returns `eventus`" with no parameters at all.
Under D5 all four must be respelled. They are marked `[OPEN]` today, which is why
this is a respelling and not a broken promise — but the arity is worth fixing in
the same change that lands D1, so that no fixture is written against the short form.

**The checker already accepts the two-parameter spelling — and the
zero-parameter one — because it counts generic arguments from one side only.**
`compiler/x86_64/checker/types/sig.inc`'s `__chk_ty_genarg` compares the
argument count against the *index it is asked for* (`jbe .bad`), a lower
bound: `eventus<mensura, erratum>` type-checks today and `erratum` is silently
discarded, as are `textus<u8>` and `refero<A, B>` — the `.una:` arm reads
argument 0 only, and the `.simple:` arm never reads the list at all. Bare
`eventus` with no `<…>` takes `.none:`, which answers the error type **without
raising**, so §5.1's spelling passes too. §7's item 4 (`eventus<mensura>` as
`EXS-E0304`) therefore cannot be made to pass by anything in this design: the
count must first be checked at both ends, at the `GenericArgs` node when there
is one and at the path segment when there is not, raising the `EXS-E0304` the
site already raises. That is a prerequisite owed by the checker independently
of sum types — a real silent-acceptance bug today — not part of D5.

---

## 4. What this unblocks, and what each site becomes

| site | today | after D1–D5 |
|---|---|---|
| §4.6 `Lector.lege_octeto` | `-> u16`, 256 for end-of-input **and** error | `-> eventus<u8, erratum>`; the two become distinct values |
| §11 `Scriptor.scribe` | `-> mensura` in `examples/imprime.exsc` | `-> eventus<mensura, erratum>` |
| §4.7 `rete` derivation | "returns `eventus`" | a type that exists |
| §5.1 `textus` slicing | "returns `eventus`, never panics" | same |
| §8.5 "Exhaustive" | false | true, by D3's two rules |
| §8.6 `TypeDecl`/`Pattern` | two `[OPEN]` bullets | D1 and D2 |
| §8.4's `E?` sigil | named, undefined | D4 |
| §14 entry 12 | `-> mensura`, `[OPEN]` | respelled |

---

## 5. The §13 amendment, which comes first

Implementing D3 requires one code that does not exist. Per CLAUDE.md a new code is a
spec amendment to §13 **before** any implementation, so it is landed with this
document and not with the checker work:

| code | meaning |
|---|---|
| `EXS-E0351` | `discerne` is not exhaustive |

**Why `035x` and not `0344`.** §13's `03xx` range groups by the construct the rule
belongs to — `0301`–`0311` general typing, `0321`–`0322` `@transitus`, `0332`
branded offsets, `0341`–`0343` the reductions. Patterns and exhaustiveness are a
fourth construct and take a fourth slot, leaving `0344`–`0350` free so the
reductions can grow into the decade they started.

**Why one code and not four.** D2 reuses `EXS-E0301`, `EXS-E0302`, `EXS-E0303` and
`EXS-E0304`; D4 reuses `EXS-E0307`. Codes are permanent (§8.3), so the cheap
direction is to reuse and the expensive direction is to invent — and each of those
five is the same class of mistake the existing code already names, differing only in
where it was written, which is precisely the argument §13 records for `EXS-E0510`
keeping its number when §4.4 broadened.

`EXS-E0351` carries a **machine-applicable fix** for the sum-type case — the missing
`casus` arms, in declaration order — and none for the scalar case, where the edit is
an `aliter` whose body only the author can write.

---

## 6. What this costs, stated before it is paid

**`tests/programs/discerne/` loses four of its thirty checks.** Checks 6, 13, 15 and
25 exist to pin "no `aliter` and no match runs nothing" — the behaviour D3 outlaws.
Landing D3 means that fixture gains `aliter` arms and those four checks are deleted,
and its header's paragraph on the subject becomes a record of a resolved
contradiction rather than a live one. That is a deliberate behaviour change to a
green fixture, and it is the reason this section exists.

**Every existing `discerne` in the corpus must be re-checked.** Any scalar
`discerne` without `aliter` becomes `EXS-E0351`. `tests/programs/discerne/` is the
known one; the sweep is part of the work, not a surprise in it.

**A variant name now collides with a constant name** (D2). No corpus program is
known to be affected, and that is unverified — the sweep above is what would find
it.

**Recursive sum types, nested patterns, range patterns, `?`'s error conversion, and
the unreachable-arm rule are all left `[OPEN]`** by name. Five deferrals is a lot,
and each is deferred for the same reason: it is a second feature that the first one
does not need, and this project's evidence note records three versions that asserted
more than they had built.

**A sum type has to be sized before it can be a local, a field or a parameter,
and nothing in the tree sizes one.** `compiler/x86_64/checker/rows/layout.inc`'s
header lists the kinds that answer width 0 — `eventus` among them — and says
what happens to one inside a plain struct: it "gets a layout that is wrong
rather than absent". The guard that keeps that from being observable is
`@transitus`-only (`EXS-E0321` unannotated, `EXS-E0309` annotated); a plain
struct has no diagnostic for it. And there is no discriminant concept anywhere
— no tag width, no payload union, no `lwr_ty_size` arm. So D1 cannot land at
the CST first and grow a layout later: a `typus` with variants admitted before
layout answers a width is silently wrong at every field offset after it. **The
first commit of this design is layout — tag plus the largest payload, with the
tag's width and the payload alignment decided and pinned by a fixture — and
the parser change comes after it.** This is the sixth cost, and it is the one
that fixes the order.

---

## 7. Evidence plan — what would retire each `[UNTESTED]`

Nothing below exists. This is the refutation condition for §3, in the order it
should be built.

**The five paths below are written without backticks on purpose.** Backticks are
what make a path a citation, and `tools/spec-check.sh`'s check 5 fails the build
over a citation to something absent — correctly, since that is the defect it was
added for. A fixture a design *owes* is not evidence it *has*, so it is named in
plain text until it exists, at which point the backticks go on and the check
starts holding this document to them.

1. **tests/unit/cst_typus_sum.asm** — D1's grammar: the two declarations of D1
   parsed, the alias form still parsed (the one-token peek does not break it), a
   payload-less variant, and `typus x = casus` unterminated as `EXS-E0202`.
2. **tests/unit/cst_pattern_ctor.asm** — D2: `casus prosperum(n)`, the bare-path
   form, and a nested pattern refused by name.
3. **tests/unit/chk_ty_exhaustive.asm** — D3, both rules: a sum-typed `discerne`
   missing one variant raises `EXS-E0351` **and the diagnostic names the missing
   variant**; a scalar `discerne` with no `aliter` raises it; both complete forms
   pass. The code set must *equal* `{EXS-E0351}`, the way `run_conformance_tests`
   checks an entry, so a second unrelated diagnostic cannot hide in it.
4. **tests/unit/chk_ty_eventus_arity.asm** — D5: `eventus<mensura>` with one
   argument is `EXS-E0304`. **Blocked on the checker's one-sided count** (D5):
   until `__chk_ty_genarg`'s test has an upper bound this fixture cannot pass,
   and that fix is owed independently of this design.
5. **tests/programs/eventus/** — D4 end to end on both backends: a program that
   reads with `lege_octeto`, propagates with `?`, and distinguishes end-of-input
   from a read error, which no program in this tree can currently do. This is the
   one that makes §11's claim true rather than promised, and it is the deliverable
   the other four support.
6. **A §14 entry** for the distinction in 5, appended and never inserted (§14's own
   rule), and only once 5 runs on both backends.

Until 1–5 exist, §8.6's bullets cite **this document** and stay `[OPEN]`: a design
is not evidence, and the difference is the whole of CLAUDE.md.
