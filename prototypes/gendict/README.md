# prototypes/gendict/

Probe for §15 open problem #5 and §7.1's own `[OPEN]` note: the three-way
interaction of **generics × capability rows × dictionary layout**, sharpened
by §4.2's amendment that a capability row is part of a function **type**, so
a witness-table entry is now a method whose *type* carries a row. This
directory answers the question by running code against it, the same way
`prototypes/capcheck/` answered the closure-capture question (§15 #1) it
sits beside — extended, not edited: `prototypes/capcheck/` has its own
dedicated agent and CLAUDE.md's scope rule ("each agent owns its directory")
is followed literally here. Nothing is shared between the two directories
except the small hand-copied lexing helpers every probe here needs
(`strip_comments`, `matching_delim`, `split_top_level_commas`) — duplicated,
not imported, per `prototypes/README.md`.

**Never shipped, never a build dependency.** Not referenced by `Makefile`,
`flake.nix`, or `tests/run.sh`. Python, throwaway, exists to answer the
question cheaply before anything is written in assembly (§18.2).

## The question, precisely

§7.1: *"Capabilities are values, so a required row can travel in the witness
table — but that makes dictionary layout depend on the row."* §15 #5 adds:
*"a witness table now carries rows as well as methods."* §10.2 claims
*"changing a body without changing a signature leaves the `ego` hash
unchanged, so dependents do not rebuild... combined with dictionary-passing
generics, this actually works."* Is that still true once a row is part of
the type occupying a witness-table slot?

Sub-questions asked as separate, runnable cases (per the assignment):

1. Does §4.2's positional substitution work when a call is reached **through
   a dictionary** (a trait-bounded generic parameter) rather than named at a
   call site?
2. What is a witness table's layout when methods carry rows — stable across
   instantiations, or does it vary with the row?
3. Can a dictionary launder authority past a `dyn` bound (`EXS-E0510`)?
4. Is there a row a generic can require of its **type parameter**, rather
   than of a value?

## Method and honesty about invented surface

Same discipline as `capcheck/SYNTAX-PROPOSAL.md`: every case is written in
real §8.6 grammar wherever §8.6 settles the construct, and every place it
doesn't is called out here rather than silently invented.

**Genuine §8.6 grammar used:** `GenericParams`/`GenericParam` (`<T: Trait>`,
production: `IDENT [':' Type]`), `InterfaceDecl`'s two forms (`interfacies X
{ Member* }` and `interfacies X in T poscit R { FunctionDecl* }`), `DeclRow`
(bare row) and `TypeRow` (braced row, only after `dyn`/`functio` types),
`RowItem`'s `sicut IDENT`, the postfix `.` `IDENT` `(...)` method-call chain,
and the `sicut Type` cast. Interface bodies are written **separator-free**
(§8.6 decision 1: struct/`interfacies`/`externus` bodies take no `;`) —
`capcheck`'s own `bad_dyn_escape.exsc` predates §8.6 and is worth noting as
an aside here, not fixed here (another agent's tree, and out of this
change's write scope): it uses `impl X for Y poscit Z {}` and `X{} as dyn Y
poscit Z`, and §8.6 decision 4 explicitly rules both `impl`/`for`/`as`
**inadmissible** (§3.9.2) and struct-literal syntax **`[OPEN]`** — neither
form parses under the grammar that now exists. Every case in *this*
directory avoids both: casts are `EXPR sicut dyn Trait poscit {row}`, and no
case ever needs a struct literal because a parameter already provides a
value of the needed nominal type.

**Incidental, not-in-the-grammar filler, kept to the minimum already
established by `capcheck/`'s own precedent:** an untyped `self` receiver
parameter (`bad_dyn_escape.exsc` already does this — `functio scribe(self)
-> i32`), and nominal types (`Left`, `Right`, `Summable`) referenced without
a `structura` declaration, exactly as `capcheck/`'s cases reference
`Scriptor`/`Mundus`/`lector` without declaring them. No `Self` keyword is
used anywhere (the spec never writes one down); every interface member and
impl method here spells its non-receiver types out explicitly instead.

## The checker

`exsecutor_gendict_check.py` is a tiny, deliberately limited surface parser
(same philosophy as `capcheck`'s), extended with exactly what these
questions need:

- Interface declarations, whose members may each carry a declared `poscit`
  **ceiling** — the one piece of information about an abstract `T: Trait`
  that a generic, compiled once, can actually see.
- Impl blocks (`interfacies X in T poscit R { ... }`), whose `R` is the
  impl's declared **mark**.
- Generic functions with one type parameter bound to one trait.
- Method calls reached through a generic type parameter — resolved against
  the trait's declared ceiling for that member, because there is no
  concrete callee name to resolve against (this **is** the dictionary path).
- `EXPR sicut dyn Trait poscit {row}`, resolved against a concrete impl's
  mark when the operand's type is concrete, and left **unresolved** (not
  silently "sound", genuinely unresolved) when the operand's type is an
  abstract generic parameter.

Two checks are implemented but **only one runs by default**:

- **Default** (`python3 exsecutor_gendict_check.py cases/*.exsc`): faithful
  to §4 as written. An impl's body is checked against its **own** declared
  mark (a straightforward extension of the existing rule-5 check, which
  `capcheck/` never applied to impl bodies at all — see
  `bad_impl_exceeds_own_mark.exsc`). A `dyn` cast is checked against a
  concrete impl's mark when one is visible. **Nothing checks an impl's
  declared mark against the trait's own declared per-member ceiling** —
  §4.4 states that exceeds-check only for `dyn` construction.
- **`--strict-static-ceiling`** (experimental, not the default): also
  checks every impl's mark against the union of its trait's declared
  per-member ceilings, generalizing §4.4's rule from "at a `dyn` cast site"
  to "at every impl declaration." This is **not a spec amendment** — it is
  the shape a fix would need, run here only to show that the shape works
  and is not vacuous.

## Cases and verdicts

Run with `./run.sh`. Ten cases, all producing the verdict predicted below
(`python3 exsecutor_gendict_check.py -v cases/*.exsc` was actually run — see
the transcript in the report this README accompanies).

| case | default verdict | code | strict verdict |
|---|---|---|---|
| `ok_generic_matches_ceiling.exsc` | accept | — | accept |
| `ok_generic_dictionary_declared.exsc` | accept | — | (n/a) |
| `ok_impl_matches_own_mark.exsc` | accept | — | (n/a) |
| `ok_static_matches_ceiling.exsc` | accept | — | accept |
| `bad_generic_dictionary_undeclared.exsc` | reject | `EXS-E0421` | (n/a) |
| `bad_impl_exceeds_own_mark.exsc` | reject | `EXS-E0421` | (n/a) |
| `bad_dyn_escape_direct.exsc` | reject | `EXS-E0510` | (n/a) |
| `bad_generic_sicut_type_param.exsc` | reject | `EXS-E0421` | (n/a) |
| `bad_static_impl_exceeds_member_ceiling.exsc` | **accept (GAP)** | — | reject, `EXS-E0421` |
| `bad_dyn_launder_via_generic.exsc` | **accept (GAP)** | — | reject, `EXS-E0421` |

Every `ok_`/`bad_` pair differs by exactly one mutation (confirmed with
`diff`, not asserted): `ok_impl_matches_own_mark.exsc` /
`bad_impl_exceeds_own_mark.exsc` differ only in whether the impl body calls
the `rete`-costing helper; `ok_generic_dictionary_declared.exsc` /
`bad_generic_dictionary_undeclared.exsc` differ only in whether `total`
declares `poscit alloc`; `ok_static_matches_ceiling.exsc` /
`bad_static_impl_exceeds_member_ceiling.exsc` differ only in `Left` (mark
`alloc`) vs `Right` (mark `alloc, rete`); `bad_dyn_escape_direct.exsc` /
`bad_dyn_launder_via_generic.exsc` differ only in whether the cast's
operand has a concrete type (`Right`) or an abstract one (`T: Summable`) —
same underlying mark/bound violation, and mutating from concrete to
abstract is exactly what flips the verdict from caught to accepted. That
last pair is the sharpest evidence in this probe: it isolates the *generic*
path, not the checker's general willingness to enforce `EXS-E0510`, as what
fails.

The two GAP cases were also run under `--strict-static-ceiling` (see
`run.sh`'s second block): both flip to `reject`. Closing the static-ceiling
gap **transitively closes the generic-dyn-laundering gap too**, because
`bad_dyn_launder_via_generic.exsc`'s laundering only exists because `Right`
is allowed to declare a mark exceeding `Summable`'s ceiling in the first
place; with that declaration itself rejected, the value that would have
laundered `rete` past `box`'s `{alloc}` stamp never compiles.

## Answers

**1. Does substitution work through a dictionary?** Yes, but only because a
second, narrower fact is available to substitute against. §4.2's rule as
literally stated resolves a row from "the actual argument's type" — through
a dictionary there is no concrete actual argument, because §7.1 requires
the generic to compile once, before any `T` is chosen. What **is** available
is the trait's own declared per-member ceiling, and `ok_generic_dictionary_declared.exsc`
/ `bad_generic_dictionary_undeclared.exsc` show that resolving against it
works exactly like resolving against a named function's declared row: the
same shape of check, the same code (`EXS-E0421`), non-vacuously (mutation
flips the verdict). This is a **narrower** mechanism than §4.2's own
wording, not a restatement of it — the sub-question "does §4.2's fix cover
this path too" resolves to: **§4.2's fix (rows travel with types) is
necessary but not sufficient here.** It supplies the *type* of the
dictionary slot; the ceiling supplies what fills that type-shaped hole when
no concrete value is in scope. Nothing in §4.2's text names this second
mechanism.

**2. Witness table layout.** **Stable, physically** — every impl of a given
trait presents the same method names, arities, parameter and return types
regardless of its declared mark; a row never changes representation, which
is exactly what §7.2's own governing rule requires ("Modes may remove
permissions. Modes may never change representation"). What is **not**
stable is a static fact *about* a slot's type: two impls of the same trait
member can legitimately (as far as §4 states) declare different marks,
which means two witness tables for the same trait, differing only in which
impl fills a slot, disagree about what capability that slot's type-level
row actually claims — unless something forces every impl to agree with the
trait's own ceiling. Nothing in §4 states that it must. This is the
concrete form of §7.1's own worry, run instead of asserted:
`bad_static_impl_exceeds_member_ceiling.exsc` is the file where two impls
of `Summable` (`Left`: `alloc`; `Right`: `alloc, rete`) disagree with the
trait's declared ceiling (`alloc`) and nothing catches it by default.

**3. Can a dictionary launder authority past a `dyn` bound?** **Yes**, and
`bad_dyn_launder_via_generic.exsc` demonstrates it directly, contrasted with
`bad_dyn_escape_direct.exsc`'s identical violation caught the moment the
same cast has a concrete operand type. §4.4's `EXS-E0510` check is stated at
a `dyn` **construction** site, against a **concrete** implementation. A cast
inside a generic function has no concrete implementation to name — `T` is
still abstract at the point `box<T: Summable>` is compiled, by §7.1's own
"compiled once" — so the existing check has nothing to compare against,
**not** because it was implemented narrowly here, but because the check as
§4.4 states it structurally cannot see past an abstract type parameter. The
fix is not a smarter cast-site check (there is nothing more to see at that
site); it is the same fix that answers question 2 — see "what a fix would
need," below.

**4. Is there a row a generic can require of its type parameter?**
**No syntax exists for this today**, and attempting the natural one
(`poscit sicut T`, extending §4.2's `sicut IDENT` from a value parameter to
a type parameter) produces exactly the shape of the *original* pre-fix
closure-capture bug, one level up: `bad_generic_sicut_type_param.exsc`'s
`sicut T` is grammatically legal (`RowItem ::= Path | 'sicut' IDENT` does
not distinguish a value-parameter name from a type-parameter name) but
semantically inert — `GenericParam ::= IDENT [':' Type]` carries no row, so
there is nothing for `sicut T` to resolve to, and it silently contributes
nothing to the function's declared row. Unlike the original bug, this fails
**closed**: the file is still rejected (`EXS-E0421`), because the
dictionary-ceiling mechanism (answer to question 1) independently computes
what the call really costs and finds the (empty) declared row insufficient.
So today, a generic wanting "whatever my dictionary needs" has exactly one
expressible option: hand-declare the trait's own ceiling, which is what
every other file in this directory does. That is workable but non-generic
in spirit — it is a fixed, per-trait fact, not something that varies with
the actual bound the way a closure's captured row does.

## The §10.2 question, answered directly

**§10.2's rebuild claim survives for a single impl in isolation, given one
missing check, and does not survive across the impl/generic boundary at
all, given the current spec text.**

- *Within one impl:* if the impl-body-vs-own-mark check
  (`bad_impl_exceeds_own_mark.exsc`) is actually implemented — it is **not**
  implemented in `capcheck/`'s existing nine-case probe; this is a real,
  independent, smaller gap this probe found before generics even enter —
  then a body change that doesn't increase real capability usage leaves the
  impl's declared mark, and hence its `ego` hash, unchanged, and correctly
  skips rebuilding dependents; a body change that *does* increase usage is
  caught at compile time and forces the mark to be bumped, which correctly
  changes the `ego` hash and correctly forces a rebuild. §10.2 holds here.
- *Across the impl/generic boundary:* a generic function `total<T: Summable>`
  is sound only if it can trust that **every** implementation of `Summable`,
  present and future, stays within the ceiling `Summable` itself declares.
  `bad_static_impl_exceeds_member_ceiling.exsc` shows that nothing enforces
  this for the static path. That means a brand-new (or edited) impl,
  compiled in a completely unrelated module, can invalidate `total`'s
  soundness **without ever touching `total`'s own signature, body, or `ego`
  entry** — its hash never changes, so §10.2's own trigger for "rebuild"
  never fires, yet the compiled generic is no longer sound. §10.2's rebuild
  signal (interface hash of the *item that changed*) is the wrong signal
  for this failure mode: the thing that needs re-checking is not `total`,
  it is the global fact "does every impl of `Summable` still fit inside the
  ceiling," and no single `ego` hash currently represents that fact.
- *What would restore it:* the `--strict-static-ceiling` experiment run
  above — generalize §4.4's exceeds-check from "at a `dyn` cast site" to
  "at every `interfacies ... in ... poscit ...` declaration," checked
  against the trait's own declared per-member ceilings. With that in place,
  the non-conforming impl itself fails to compile, at its own definition,
  regardless of which generics might ever use it — turning a
  cross-module, silently-invalidated fact back into a **local**, always
  re-checked, `ego`-hash-tracked one, the same way §9.6 already treats
  `numeri` as interface rather than implementation. This is not proposed
  here as a spec amendment (out of this probe's authority) — only
  demonstrated as a working, non-vacuous shape.

## What remains open

- `[UNTESTED]` as a soundness claim, same caveat `capcheck/` states for
  itself: ten cases in a Python probe are not a proof and not a compiler.
- The `--strict-static-ceiling` experiment is a hypothesis, not a settled
  design. It has not been checked against every construct §4 describes
  (e.g. multiple type-parameter bounds, a trait with member-level rows that
  differ per member, a `dyn` bound narrower than an already-narrow generic
  ceiling) — only against the cases here.
- Whether capabilities are, at runtime, erased compile-time facts or actual
  values that must be threaded as extra arguments (§4.5's `sub alloc = a;`
  reads as the latter) is not settled by §4 and matters for whether "layout
  is stable" (answer 2) is the right frame at all: if a capability is a real
  runtime value, a method whose row varies per impl may need a different
  actual argument list per impl, which is a representation difference §7.2
  forbids — this probe does not reach that question; it only shows that the
  *type-level* row already disagrees across impls before runtime
  representation is even considered.
- No `EXS-E0` code was needed beyond the two already in the registry
  (`EXS-E0421`, `EXS-E0510`) for every check this probe implements,
  including the experimental one — both are reused for a violation of the
  same *shape* they already cover (an actual capability need or mark
  exceeding a declared bound), just checked at an additional site. No code
  was invented.
