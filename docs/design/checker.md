# Checker — design plan (Stage 2)

Status: **partly built.** `compiler/x86_64/checker/` holds passes 0, 1, 3 and 4
(b9c0abc), under nine fixtures; pass 2 (types) and pass 5 (lexicon, built and
not enabled per spec §3.3) are in progress. Section 2.2's range scan was
corrected against the code (d8cd939). This line said "no checker exists"
until 2026-09-10.
`spec §N` cites `docs/spec/exsecutor-spec-v0.4.md`; `AST n.m` cites
`docs/design/typed-ast.md`; `IR n.m` cites `docs/design/ssa-ir.md`; a file
under `compiler/x86_64/ast/` is cited by name where it and AST n.m disagree —
**the code is what is built, and where the two differ this document designs
against the code and says so** (section 9). Probe cases are cited by path;
a claim no probe case exercises is `[UNTESTED]`. Taken as given: spec §16's
Stage 2 line, §4, §5.1–§5.3, §7.1, §3, §8.6, §13; AST 2.4–2.8; IR 2.9–2.10.
This is the contract the four checker agents (section 6) build against and
Stage 3 consumes. One decision per question, with the constraint that forces
it. Nothing here is written into `docs/spec/`, and **no code is chosen that
§13 does not have** — section 2.4 lists what §13 lacks, as a proposal.

## 1. The constraints and what they force

1. **The kill criterion is first** (spec §16): *"if `sub` resolution needs
   a search algorithm, redesign."* Section 2.1 shows resolution on the tree
   as built is a bounded walk over fixed slots. Three rules keep it that way
   and each is named, because dropping any one of them is where a search
   would come from.
2. **Stage 2 is an annotation pass** (AST 2.2, spec §9.1): it fills slots
   that exist and allocates side tables; it never rebuilds. So every pass
   below is a walk over `Ast.nodes` in the same preorder, writing `ty`, `d`,
   `Decl.ty`, `Decl.flags`, `Ast.types`, `Ast.rows`, `Ast.layout`, `own`,
   `konst` — and nothing else.
3. **Freestanding assembly over `rt/`**: each pass is its own descent (no
   generic walker exists), scratch lives on a second arena reset after the
   checker, and every table is a `Vec` indexed by an id. `rt/map.inc` is
   looked up, never walked (section 4).
4. **§13 is closed.** The checker raises exactly the fifteen codes section
   2.4 lists. Every other rule it must enforce is designed here and
   *cannot be raised* until §13 grows a code; section 2.4 says which.
5. **No implicit conversion anywhere** (spec §4.5, §5.2, §5.4, §6.3). So type
   compatibility is **equality of interned type ids** — rows, byte order and
   placement included — and the only subset checks are at *bounds*
   (`EXS-E0510`) and between an effective and a declared row (`EXS-E0421`).
   This is what makes function-type rows part of the calling convention
   (IR 2.9) without an adapter thunk anywhere.

## 2. Decisions

### 2.1 The kill criterion: `sub` resolution is a frame walk over eleven slots

**What the tree gives.** `AstDecl.parent` (ast/node.inc) is the enclosing
*declaration* — the walker changes it at `Fn`, `Lambda`, `Struct`, `Impl`,
`Interface`, `Externus` and nowhere else (from_cst.inc): a block has no
`Decl`, so inside a function every scope is a contiguous range
`[Block.c, Block.c + Block.aux)` with nested blocks owning sub-ranges (AST
2.4, verified by `ast_verify`). Walking up `Decl.parent` therefore goes
function → module in one step; the intra-function structure is the ranges.

**Decision: a scope stack of frames, one per open `Block`, `Fn` root and
`Lambda` root; each frame holds `cap[11]`, one `u32` slot per capability
atom in spec §4.6's order (`Mundus`=1 … `Crudum`=11), holding the `Decl`
that provides that atom in that scope, or 0.**

(A fourth kind was added later — a `TYDECL` root, one per `typus`,
`structura` or `interfacies` that carries `[GenericParams]`; see section 2.2's
pass 1. It holds generic parameters, never a capability provider, so every
`cap` slot in one is 0 and `Lookup(P)` below passes straight through it.)

    Frame { node u32, first_decl u32, seen u32, kind u8, cap[11] u32 }   ~60 bytes

- **Bind.** `sub P = e;` (statement form) sets `cap[P]` in the *current*
  frame to the new `AST_D_SUB` decl; `sub P = e { … }` pushes the block's
  frame with `cap[P]` set. A parameter of capability type sets `cap[P]` in
  the function's root frame to the `AST_D_PARAM` decl. A declared row item
  `poscit P` sets `cap[P]` in the root frame to the atom's own decl
  (section 2.2 pass 0) — meaning *the caller's carrier*. Before any bind,
  **lookup runs; a hit anywhere in the enclosing frames is the shadowing
  error** (spec §4.5, "shadowing is an error"; no §13 code — section 2.4).
- **Lookup(P).** Walk frames from the innermost outward. The first non-zero
  `cap[P]` is the answer. Crossing a `Lambda` root frame records a capture
  in that lambda's inferred row (spec §4.1 rule 4, fourth way) and continues.
  At the `Fn` root with no hit: if the function's row is *declared* the draw
  is `EXS-E0421` at the drawing node; if it is *inferred* (section 2.3) the
  atom joins the function's row and resolves to the atom decl.
- **What draws.** An atom name in expression position (`envia(rete, x)`);
  a call whose callee type's row contains P (after substitution, section
  2.3); a `*T` type anywhere in the body (spec §8.4: raw pointer requires
  `Crudum`). Each is one lookup.

That is the whole algorithm: cost is lexical depth, there is no candidate
set, no unification, no backtracking, and the result of a lookup is a
function of the frames alone. Three rules make it so, and each is a
decision here because the spec states it once and its removal is exactly
where a search would appear:

1. **One value per capability type per scope (spec §4.5).** Two parameters
   `a: rete, b: rete`, or a `sub rete` under a `poscit rete`, are errors.
   If two providers were admitted, an implicit draw of `rete` at a call
   would have to *choose* — a search. (`fabrica` in
   `prototypes/capcheck/cases/bad_closure_capture.exsc` declares
   `poscit rete` *and* binds `sub rete`; under this rule that is the
   shadowing error — section 9, finding 12.)
2. **A field of the receiver is never an implicit provider.** Spec §4.1
   rule 4's third way ("held in a field of the receiver") is a *value*
   path: `s.sock` is used explicitly, or bound by `sub rete = s.sock;`.
   Reading it as implicit would make lookup search the receiver's fields —
   transitively, by §4.3 — for one of type `rete`. That is the criterion
   firing, and this document declines it. Visibility in the interface is
   preserved by the type being capability-bearing (§4.3).
3. **Function types compare by id, rows included; `sicut` is the only row
   polymorphism.** A parameter named by `sicut f` accepts an argument of any
   row and substitutes it (spec §4.2, positionally: `RowItem.d` = ordinal,
   AST 2.6); every other parameter's row must be *equal*. Subset at
   assignment would be an implicit conversion (§4.5) and, since carriers
   are hidden arguments (IR 2.9), a calling-convention change. Consequence:
   a bare `functio(f32) -> f32` is the *empty* row (spec §8.6: `poscit {}`
   is the *explicit* empty row, so the bare form is the implicit one), and
   `prototypes/capcheck/cases/bad_hof_no_row.exsc` / spec §14 entry 9 —
   which need the bare row to be *open* — are not adopted: that attack
   still fails, at the call site, as a type mismatch (finding 11).

**Where a search would be needed, and what is decided instead.**

| construct | would need | decided |
|---|---|---|
| `sicut T`, `T` a type parameter (spec §7.1 `[OPEN]`) | a row for a type | a diagnostic (no code); `prototypes/gendict/cases/bad_generic_sicut_type_param.exsc` already fails closed |
| a method call on `a: T`, `T: Trait` | the impl for the eventual `T` | the trait member's declared row — a lookup by name in the interface's member range (`ok_generic_dictionary_declared.exsc` / `bad_generic_dictionary_undeclared.exsc`) |
| `sicut IDENT` inside a braced `TypeRow` (spec §8.6 admits it) | a parameter name a type does not have | a diagnostic (finding 5) |
| a substituted row that still carries an ordinal (a HOF passed to a `sicut` parameter) | the referent of someone else's ordinal | a diagnostic at that call site; spec §4.2's "name with no referent" `[UNTESTED]` |
| `potestas` name in a row | — | flattened to its atoms at interning; no lookup at draw time |

**Verdict.** The criterion does not fire on §4 as written. It would fire on
rule 4's third way read as implicit resolution, and on any relaxation of
rule 1 above; both are recorded so the next reader does not relax them.

### 2.2 Pass structure and order

Six deliverables, run in this order, each a walk or a loop over an index
range. "Walk" is a recursive descent from `Ast.root` in operand order with
a threaded state struct (the shape of `AstWalk` in from_cst.inc); node ids
are postorder, so every child is typed before its parent without a second
visit.

| # | pass | file | shape | fills | raises |
|---|---|---|---|---|---|
| 0 | atoms | `resolve.inc` | push 11 decls | `Ast.decls` N+1..N+11 in §4.6 order | — |
| 1 | resolve | `resolve.inc` | one walk | `Seg.d` `Path.d` `RowItem.d` `Sub` frames, `Decl.flags` `address_taken` | shadowing, unresolved, duplicate (no code) — **not `E0500`**, see below |
| 2 | types | `types.inc` | signatures loop, then one walk | `Ast.types`, `Node.ty`, `Decl.ty`, `Member.d`, `konst`, `own`, `memory_resident` | `E0311` `E0332` `E0341` `E0342` `E0520`; mismatch etc. (no code) |
| 3 | rows | `rows.inc` | intern API (used by 2); one walk building a use list; **fixpoint over the list**; one check loop | `Ast.rows`, fn/dyn `TypeNode.b`, `Decl.flags` `capability_bearing` | `E0421` `E0500` `E0501` `E0510` |
| 4 | layout | `layout.inc` | loop over struct decls and sum `typus` decls in decl order (sum-types.md D6) | `Ast.layout` | `E0321` `E0322` |
| 5 | lexicon | `lexicon.inc` | loop over public decls in decl order | — | `E0601` `E0602` `E0603` `E0610` |
| 6 | verify | `verify.inc` | AST section 3's Stage 2 list | — | `rassert` only |

**Pass 0** exists because capability atoms are "identifiers in the
capability namespace" (spec §8.4 tier 3) with no declaration site, while
`Seg.d`, `RowItem.d` and row items are declaration ids (AST 2.6). Stage 1
pushes one `Decl` per atom at the end of `Ast.decls` (`ast_cap_push`, called
by `ast_from_cst`; kind `AST_D_CAPATOM`, `parent` 0), so an atom's id is
`N + k` for a module of `N` source decls and §4.6 ordinal `k` —
deterministic, ascending id is ascending §4.6 order, which makes row sorting
a bit scan (pass 3), and `ast_verify` enforces all-eleven-or-none and
last-eleven. Pass 0 therefore *finds* them — `ast_cap_base(tree)` — and
must not push a second set (it traps). This document first said the
checker pushes them; the ast agent decided Stage 1 should, so the tree is
self-describing and `ast_dump`/`ast_load` carry them (finding 2, done).

**Pass 1** resolves what needs no type: a module table (`rt/map.inc`, name
id → decl, filled from `Module.a` in item order, so forward references
between items resolve) and, inside a function, **an explicit bind stack
with a mark per frame** — `ChkCtx.binds`, pushed as the walk passes each
declaring node, popped to the frame's mark when the frame closes, searched
from the top down. This document first said "a scan of each open frame's
decl range from the most recent *seen* decl backwards", which is wrong:
block decl ranges **nest** rather than partition (`ast_verify`'s
`__ast_v_nest` enforces exactly that — an outer block `[3,8)` contains its
inner block's `[7,8)`), so a backwards range scan makes an inner block's
names visible in the enclosing block after it closes. The resolve agent
measured it on a real tree and declined it; `Block.c`/`Block.aux` are not
read by this pass at all. Bindings are visible only after their declaration
(the push happens at the declaring node); duplicates in one block are an
error; an inner block shadowing an ordinary name is allowed (names resolve
by name, so nearest-wins is unambiguous — only capabilities resolve by type
and only they forbid shadowing). `Member.d` is *not* resolved here: a
member needs the receiver's type. `&x` on a `Path` sets `address_taken`.

**A TYPE DECLARATION IS A SCOPE TOO,** and pass 1 did not treat one as a scope
until 2026-09-27. Spec §8.6 gives `TypeDecl`, `StructDecl` and
`InterfaceDecl` an optional `[GenericParams]`; only a *function's* were ever
bound (`Fn.c`, since `6ce455b`), so `typus box<T> = refero<T>;`,
`typus e2<T, E> = casus prosperum(T), casus adversum(E);`,
`structura S<T> { x: T }` and
`interfacies I<T> { functio acc(self: I, v: T) -> T }` each raised
`EXS-E0301` at the parameter's *use* — measured one file at a time, not
reasoned from the grammar. Two causes, both in the default walk: `__chk_kids`
takes slots in order, so the body (`Typus.a`, `Struct.a`/`Interface.a`) was
walked *before* the `Generics` in `.c`; and at module level `__chk_bind` has
no frame to bind into and drops the parameter. All three kinds now share one
`.tydecl` arm — a frame of kind **`CHK_FR_TYDECL`**, the `Generics` walked
into it first, then the body, then pop — which is `.fn`'s shape and is why
none of the three needs an arm of its own. (`potestas` takes no generic
parameters: `Potestas.c` is `AST_R_NONE`, and `potestas P<T>` is `EXS-E0201`
at the `<`.)

The new frame kind exists because `Lookup`'s walk above **stops at the
innermost `FN` frame**, justified by "a function is never inside another
function's scope". An interface member's signature naming the interface's own
`T` has to cross outward past its own `FN` frame into something that is a
scope and is not a function, so the walk now continues past an `FN` frame
when, and only when, the next frame out is a `TYDECL`; and
`__chk_frame_root` skips a `TYDECL` exactly as it skips a `BLOCK`, since both
of its callers read that frame's node as a `Fn`/`Lambda` with a `Sig` to
insert a row into. Pinned by `tests/unit/chk_resolve_typus_generics.asm`
(all three kinds, a parameter not visible outside its declaration, and two
`typus` declarations both naming `T` with no `EXS-E0302` between them) and by
`tests/unit/chk_ty_typus_sum.asm` row 5, which flipped from two `EXS-E0301`s
to none.

**`E0500` is not pass 1's**, and this document said it was until 2026-09-25.
Spec §4.1 rule 7 reads *"No module-level mutable state. (`EXS-E0500`; with a
capability, `EXS-E0501`.)"* — **one** code per offending binding, chosen by
whether the binding's type is capability-bearing (§4.3). Pass 1 cannot choose:
capability-bearing is pass 3's fixpoint over `Decl.ty`, which is pass 2's. So
pass 1 raised `E0500` unconditionally and pass 3 added `E0501`, and
`mutabilis retis_globalis: rete;` — §14 entry 11, which expects exactly
`{EXS-E0501}` — got both. **Decision: the whole of rule 7 is pass 3's**, and
pass 1 says nothing; the two facts pass 3 needs are already on the declaration
(`AST_F_MUTABILIS`, and `AstDecl.parent == 0` for module level), so no ninth
`Decl.flags` bit is spent (hazard H4). `E0500` keeps its span — moved onto the
declaration — and its required fix (`mutabilis` → `firma`), which is why the
fix text stays in `resolve.inc` beside the other `E05xx` literals while the
code that raises it lives in `rows/compute.inc`. Finding 24.

**Pass 2** interns declared types first, in decl order, so bodies may call
functions declared later: for every `Fn`/`ExternFn`/`Member`/`Struct`/
`Typus`/`Iface`/`GenericParam`/`Field`/annotated `Binding`, the `Ty*`
subtree → `Ast.types` id → `Decl.ty` and each `Ty*` node's `ty`. Nominal
kinds (`struct` `iface` `param`, `a` = decl) need only the decl id, so a
field may name a struct declared below it. Then one walk types bodies in
decl order. `typus X = T;` is a **transparent alias** (its `Decl.ty` is
`T`'s id; the `alias` type kind stays unused — finding 8); a cycle is an
error.

**Pass 3** is the one that is not a single walk. Spec §4.1 rule 5 infers
private rows *transitively*, and a call graph has cycles. Two deterministic
choices exist — Tarjan SCCs in declaration order, or iteration to a fixpoint
in declaration order — and **fixpoint iteration is chosen**: the per-function
row is a pair of masks (`u16` atoms, `u64` ordinals), the transfer is
monotone union, the lattice has height ≤ 75 per function, and the least
fixpoint is unique whatever the visiting order, so determinism is free and
there is no DFS, no index/lowlink arrays and no stack to get right in
assembly. Cost is O(rounds × uses) with rounds bounded by the longest
chain of new atoms — two rounds on the probe cases by inspection, `[UNTESTED]`. The walk first builds a
**use list** in walk order — `Use { fn u32, node u32, kind u8, target u32 }`
for every call, atom name, `*T`, lambda capture and `sub` — and the fixpoint
iterates the list, never the tree.

**Passes 4 and 5** are loops over `Ast.decls`; neither needs the walk.

### 2.3 Inference: what is inferred and what is not

- **Binding types.** `firma`/`mutabilis` without an annotation take the
  initializer's type; with neither annotation nor initializer, an error (no
  code). Both present: ids must be equal.
- **Lambda.** Parameter types are required (spec §8.6 decision 2). The
  result type, if omitted, is the type of its first `redde`; later ones must
  match; none → `unit`. Its row is always inferred (rule 5, §8.6 decision 2)
  = captures ∪ call contributions ∪ ordinals for calls of its own
  function-typed parameters, and lands in its `fn` type's `b`.
- **Private functions with no `poscit`** are inferred; **`publica`** and any
  function with an explicit `poscit` are declared and checked. Spec §4.1
  rule 6 ("no `poscit` … is pure") is read as the meaning of an *explicitly
  empty declared row*; it cannot also govern private functions without
  contradicting rule 5 (finding 10).
- **Integer literals** take the *expected* type: the other operand of a
  `Binary`, the annotation, the parameter, the result at `redde`, the range
  partner. A literal with no expected type is an error `[OPEN]` until spec
  §8.4's literal grammar supplies widths; a literal that does not fit is an
  error (no code). `konst[node]` holds the value once the width is known.
- **A `contrahe` accumulator's element type F.** Nothing annotates one
  (`Contrahe ::= 'contrahe' IDENT ':' ArithOp`, spec §8.6), and the loop's
  element type is not it either — `s = v[i] * 2;` contributes a product, and
  no sentence forbids folding a `u8` array into a `u32`. So **F is the first
  contribution's right-hand type**, in source order, and every later
  contribution in that loop is typed against it: a disagreement is then the
  ordinary `EXS-E0303` at its own right-hand side rather than a rule of its
  own. See "The accumulator's two states", below.
- **Nothing else.** No unification variables, no inference across items,
  none across modules (spec §4.5). Expected types flow *down* into literals
  and lambdas only.

**The accumulator's two states** (2026-09-25, spec §5.4). An `ACCUM`
declaration's type is the `red` **handle** while its loop is open and **F**
after it closes — spec §5.4's "After the loop `acc` is an ordinary binding",
read literally. `red` takes `a` = the accumulator's own declaration and
records F in `AstType.b`, the field that is a row id on an `fn` and means
nothing on a `red`: no new field, no change to `ast/`, no second side table,
and — because all sixteen bytes are the interning key — `red(a, b=0)` and
`red(a, b=F)` are simply two ids, so the id the declaration carries *is* the
state. Nothing else in the tree distinguishes the two, and three rules read
it:

- `acc = e;` is the **contribution** only while the handle is on the
  declaration; `e` is typed against F, never against the handle. After the
  loop the same write is `EXS-E0306`, exactly as a write to a `firma` is —
  "an ordinary binding" and nothing declares it `mutabilis`.
- `EXS-E0341` fires on a read only while the handle is on the declaration.
  The nesting counter alone is wrong: two nested reductions, the outer
  contributing the inner's result, is a read made with two `contrahe` loops
  open and is exactly what §5.4's "after the loop" sentence permits.
- What the lowering is handed: F on `Decl.ty` like any other binding's type,
  and the operator on the `Contrahe` node's `aux`. No `red` survives on a
  declaration in a program that checks clean — measured by
  `chk_ty_contrahe.asm`'s last two checks, which read the tree. Whether that
  is what `lower/` wants is `[UNTESTED]`: its `ForHead` still `rassert`s.

**A `contrahe` whose body never contributes** has no F and keeps the handle.
Neither §5.4 nor §8.5 says what such a loop means and §13 has no code that
fits, so the checker invents neither (CLAUDE.md, "Error codes are
permanent"): the program is accepted, a use of the accumulator afterwards is
one `EXS-E0303` against a type no expression can have, and the lowering still
`rassert`s at the `ForHead` (`lower/stmt.inc`). Deciding it is a spec
amendment. `tests/unit/chk_ty_contrahe.asm` pins the decision as a row so
that changing it is a visible edit.

**Cascade suppression around a bad contribution.** A read that raised
`EXS-E0341` answers the **error** type, and a first contribution that did not
settle records the error type as F. Both are typed-ast.md section 2.2's rule
applied where it had not been: before them, one illegal read was three codes
(the `E0341`, an `E0303` at the operand beside it, and a third at whatever
the enclosing statement wanted), against spec §8.3's "one class each, never
one per message". Every rejected row of `chk_ty_contrahe.asm` is now exactly
one diagnostic.

**Row computation, precisely** (pass 3). For each function `F`:

    draws(F) = { P : name P used | P ∈ row(callee type) after substitution
                 | `*T` in body ⇒ Crudum | capture P in an inner lambda }
    row(F)   = declared(F)                       if F is declared
             = lfp over draws with providers removed   if F is inferred

At a call, the callee's row is `types[callee.ty].b`; each ordinal `sicut k`
is replaced by the *atom* items of argument `k`'s type row (`fn` or `dyn`,
`TypeNode.b`) — the amended spec §4.2, `cases/bad_laundering.exsc` and
`cases/ok_hof_subset.exsc`. A method call on a concrete receiver uses the
impl method's row (the impl mark, or the member's own `poscit` if written);
a method call on a `param`-typed receiver uses the trait member's declared
row (`prototypes/gendict/`, answer 1). A draw whose provider is a `sub`,
parameter or capture is satisfied; the remainder must be in `declared(F)`
or it is `EXS-E0421` at the drawing node, after the fixpoint. **`sub P`
does not itself contribute `P` to the row**: `poscit` means "drawn from the
enclosing scope" (spec §4.1 rule 4) and `sub` is the opposite — a provider.
The probe's `sub_atoms_of` is an over-approximation not adopted (finding
12); no probe case turns on it.

**Ceiling and bounds** (spec §4.4, `EXS-E0510`), all in pass 3's check loop:

- at every `Impl`: `mark ⊆ ceiling`, where `mark` = head row ∪ the
  capability-bearing atoms of the target type, `ceiling` = union of the
  trait's declared member rows (`gendict/cases/bad_static_impl_exceeds_
  member_ceiling.exsc` ← rejected; `ok_static_matches_ceiling.exsc` ←
  accepted; spec §14 entry 24);
- at every impl method: its effective row ⊆ its own row
  (`bad_impl_exceeds_own_mark.exsc`, `EXS-E0421`), and a member-level
  `poscit` ⊆ **the trait member's** — the corresponding member looked up BY
  NAME, never the `ceiling` union of the bullet above. Until 2026-10-03 the
  implementation tested the member against that union, so on a trait with
  more than one member, member *b*'s declared atom covered an impl of member
  *a* that drew it: `interfacies Duo { a poscit alloc; b poscit rete }` with
  an impl `a poscit rete` was accepted, and a `<T: Duo>` calling `v.a`
  (which attributes only `a`'s `{alloc}`) then laundered `rete` undeclared —
  spec §4.4's generic hole, reopened for multi-member traits. The
  single-member fixtures could not see it because there the union and the
  member's own row are the same set. Fixed in `__chk_row_e0510`
  (`__chk_row_memrow` does the by-name lookup); pinned by
  `tests/unit/chk_row_e0510_permember.asm`, which fails if the union is used;
- at `e sicut dyn I poscit {P}`: mark(impl of `I` for `e`'s type) ⊆ `P`;
  when `e`'s type is a `param` bound to `I`, `ceiling(I) ⊆ P`
  (`bad_dyn_launder_via_generic.exsc` — rejected at the cast, no
  instantiation needed).

**Capability-bearing** (spec §4.3): a fixpoint in decl order over struct
decls — a type bears the union of its capability atoms, its fields' bearing
(transitively, through `acies` `ptr` `ref` `refc` `borrow` `eventus`) and
the rows of any `fn`/`dyn` it contains; the result sets `AST_F_CAPBEAR`.
The spec's "must be declared so" has no syntax (finding 7); the flag is
computed and the ego emitter reads it. A module-level binding, `firma` or
`mutabilis`, whose type bears anything is `EXS-E0501`
(`cases/bad_capability_module_state.exsc`, §14 entry 11); one that bears
nothing is `EXS-E0500` if it is `mutabilis` and nothing if it is `firma`. Both
codes, one per binding, in this pass — the roster above says why.

### 2.4 Diagnostics: what §13 has, what it lacks, and a proposal

**Codes the checker raises**, with the pass and the node the span is on:

| code | rule | spec | pass | span |
|---|---|---|---|---|
| `E0311` | integer index on `textus` | §5.1 | 2 | the `Index` node |
| `E0321` | `:nativus` (or unannotated multi-byte) field in `@transitus` | §5.2 r.3 | 4 | the field |
| `E0322` | implicit padding in `@transitus`: partial trailing byte, or a >8-bit field off a byte boundary | §5.2 r.2, r.4 | 4 | the field / the struct |

Pass 4 raises neither `E0321`, `E0309` nor `E0322` for a `@transitus` struct
one of whose fields is an integer of width < 8 carrying an explicit byte order:
that shape cannot be written (§5.2 r.3) and is therefore parser recovery, which
the parser has already diagnosed `EXS-E0201` (finding 23).
| `E0332` | branded offset applied to another buffer | §5.1 | 2 | the argument |
| `E0341` | accumulator read in its own body | §5.4 | 2 | the `Path` |
| `E0342` | `rumpe` in an iteration carrying `contrahe` | §5.4 | 2 | the `Rumpe` |
| `E0421` | a draw with no provider in a declared function | §4.1–§4.2 | 3 | the drawing node |
| `E0500` | module-level `mutabilis` of a type bearing no capability | §4.1 r.7 | 3 | the `Binding` |
| `E0501` | module-level binding of a capability-bearing type | §4.1 r.7, §4.3 | 3 | the `Binding` |

`E0500` and `E0501` are **alternatives**, never both: one binding, one code,
chosen by capability-bearing. That is why `E0500`'s pass column reads 3 and not
1 (finding 28).
| `E0510` | mark exceeds ceiling or `dyn` bound | §4.4 | 3 | the impl head / the cast |
| `E0520` | non-atomic `refero` in an `externus` signature | §5.3, §6.4 | 2 | the type node |
| `E0601` `E0602` `E0603` `E0610` | lexicon | §3.1, §3.4, §3.5, §3.8 | 5 | the declaring name |

`E0105` (confusables over the import closure) needs imports that do not
exist and is not Stage 2's; `E0701` is the backend's; `08xx` is the `certus`
profile checker, a separate pass this document does not design.

**Rules the spec states, or implies, that had no code when this was
written.** §13 was amended in 4edb704 with exactly the codes proposed
below — `E0301`–`E0309`, `E0343`, `E0422`–`E0424`, `E0511` — so every class
here can now be raised; the table stays as the derivation record. The
class column is the proposal's grouping.

| rule | where stated | class |
|---|---|---|
| name resolves to nothing (path, member, method, interface, atom) | §4.5, §8.6 | A |
| two declarations of one name in one scope; two impls of one interface for one type | AST 2.4 | B |
| type mismatch: operand, argument, initializer vs annotation, assignment, `redde`, condition not `u1`, `terminus`/range not integer, lambda vs declared fn type, alias cycle | §5.2, §5.4, §4.5 | C |
| wrong number of arguments or generic arguments | §8.6 | D |
| operation undefined on the type: call of non-function, index of non-indexable, `*` of non-pointer, `?` on non-`eventus`, `sicut` between unrelated types, member on a type without it | §8.6 | E |
| assignment to `firma`, a parameter, or a non-lvalue | §8.6 ("lvalue check semantic") | F |
| `rumpe`/`perge` outside a loop; a non-`unit` function that can fall off its end; `redde e` in a `unit` function | §8.5 | G |
| literal with no determinable type, or out of range for its width | §5.4, AST 2.3 | H |
| repeated or contradictory `TySuffix`; byte order on a non-integer; `apud` on a non-`acies`; `@transitus` on a non-struct; `@nucleus` on a non-function; binding with neither type nor initializer; bound that is not an interface | AST 2.5, §5.2, §5.5 | I |
| `forma` width missing after `arborea` or present after `ordinata`; unknown shape | §8.6, AST 2.7 | J |
| capability bound twice in one scope (shadowing); two providers of one atom | §4.5 | K |
| row item malformed: `sicut` on a non-parameter, on a type parameter, in a `TypeRow`, on a parameter whose type has no row, with a written row; a substituted row still carrying an ordinal; `sub` of a non-atom | §4.2, §7.1, §8.6 | L |
| `initium` absent, or not `initium(m: Mundus) -> u8` (per the §4.6/§12 amendment in progress) | §4.1 r.2 | M |
| interface not implemented by the type (dyn cast, generic argument); impl missing a member or its signature differs; impl of a non-interface | §4.4, §7.1 | N |
| `@nucleus` body allocates, recurses, or observes the host | §5.5 | `[OPEN]`, Stage 3 |
| `externus` signature with a non-FFI-safe type other than `refero` | §5.3 | `[OPEN]` |

**Proposal for the §13 amendment** — the spec owner's to number; these are
one *class* each with a stable meaning, never one per message (spec §8.3):
`03xx` is where §5's semantic rules already live and `0301`–`0310` are
free; `04xx` is capabilities; `05xx` is bounds and interfaces.

| proposed | class | meaning |
|---|---|---|
| `EXS-E0301` | A | name does not resolve |
| `EXS-E0302` | B | duplicate declaration in one scope |
| `EXS-E0303` | C | type mismatch |
| `EXS-E0304` | D | wrong number of arguments |
| `EXS-E0305` | E | operation not defined on the type |
| `EXS-E0306` | F | assignment to an immutable or non-lvalue target |
| `EXS-E0307` | G | control flow misuse |
| `EXS-E0308` | H | literal cannot be typed or does not fit its width |
| `EXS-E0309` | I | type expression or annotation not applicable |
| `EXS-E0343` | J | reduction shape malformed (beside `0341`/`0342`) |
| `EXS-E0422` | K | capability bound twice in one scope |
| `EXS-E0423` | L | capability row item malformed |
| `EXS-E0424` | M | entry point malformed |
| `EXS-E0511` | N | implementation does not match its interface |

Fourteen codes; `E0311` becomes the `textus`-specific instance of class E
and keeps its number (codes are permanent). The lexicon rubric's
"wrong-stem" case (`legor` for `lector`, `prototypes/lexicon/RUBRIC.md`) is
`E0601` — it does not decompose — and needs nothing new. Fix payloads
(`diag/fix.inc`): `E0421` carries the insertion of the missing atom into
the enclosing declaration's row; `E0601`–`E0610` carry the corrected
spelling where the table yields exactly one; nothing else carries one.

### 2.5 What "typed" produces

After Stage 2 with **no diagnostic** the lowering may assume:

- `Node.ty` ≠ 0 and ≠ 1 on every expression node and every `Ty*` node;
  `unit` on statements that have a type slot only for uniformity.
- `Seg.d`, `Path.d`, `Member.d`, `RowItem.d` ≠ 0; `RowItem.d` is an atom
  decl, a `potestas` decl, or an ordinal < its `Sig`'s parameter count
  (distinguished by `RowItem.aux`).
- `Decl.ty` ≠ 0 on `FN` `EXTERNFN` `MEMBER` `LAMBDA` (an `fn` type whose `b`
  is the **final** row — declared or inferred; "the row is in the type" is
  literally where the lowering reads carriers), `PARAM` `GENPARAM` `FIELD`
  `BINDING` `LOOPVAR` `SUB` (the atom type), `ACCUM` (**F** — see below),
  `STRUCT` `TYPUS` `IFACE`; 0 on `IMPL` `POTESTAS` `EXTERNUS`.
- `Ast.rows` holds every row any type names; `Ast.layout` has an entry for
  every `STRUCT` and `FIELD` decl (AST 2.8); `ast_side_alloc` has run
  (`Ast.sides` ≠ 0): `own[node]` on every reference-typed expression,
  `konst[node]` on every `Lit`; `Decl.flags` `address_taken`,
  `memory_resident` (non-scalar type or address taken), `capability_bearing`.
- `Call.d` = `extra` offset of the impl decls chosen for each generic
  argument, in generic-parameter order (section 2.7) — needs a role change
  in `kinds.inc` (finding 4).

With **any** diagnostic, the lowering does not run and the Stage 2 verifier
(section 3) does not run; a node whose typing failed carries type id 1 and
suppresses diagnostics on its operands (AST 2.2, H3 — `[UNTESTED]`).

### 2.6 Text types and brands

`textus` `octeti` `scalares` `grapha` are four `AstType` kinds (kinds.inc)
with no fields, so they are four ids and nothing coerces (spec §5.1);
`t[0]` with `t: textus` is `EXS-E0311` at the `Index` node before the index
is typed. `E0311` needs none of their methods.

**These methods are not resolved through impls, and cannot be.** This section
used to say `octeti()`, `scalares()`, `quaere`, `sectio` and `plica_unicode`
were "resolved as every method is — through impls for the receiver's type id"
and that "no declaration of them exists in this tree". Both sentences are now
false, and the first was never right: an impl lookup keys on a *declaration*,
and these four kinds have none — `__chk_ty_method`'s impl scan had nothing to
scan, so every §5.1 text operation was `EXS-E0305`, "operation not defined on
the type". The lookup that works is **keyed on the receiver's type kind**, in
`__chk_ty_pre_member`'s `.views:` arm, against four prelude rows that
`checker/types/prim.inc` holds and `prelude/interface.inc` does not (rows
12–15; that file's constant block says why, and which three row-count bounds
it leaves intact):

| row | signature | receiver kind |
|---|---|---|
| `plica_unicode` | `(t: textus) -> textus` | `textus` |
| `plica_sermone` | `(t: textus, s: sermo) -> textus` | `textus` |
| `octeti` | `(t: textus) -> octeti` | `textus` |
| `numerus` | `(b: octeti) -> mensura` | `octeti` |

The kind still *decides*, which is the invariant the prelude rows above already
hold: `numerus` on a `textus` is `EXS-E0305`, and so is `octeti` on an
`octeti`. `plica_sermone` takes the capability as parameter 1 rather than in a
row, because spec §5.1's own code block writes `"I".plica_sermone(sermo)` and
§14 entry 1's whole subject is that a function with no `sermo` in scope has no
value to pass. (§10.1 spells the same function `-> textus poscit sermo` with
one parameter. The spec holds two signatures for one name; the checker follows
§5.1 and entry 1, because entry 1 is the fixture that runs against it.
`[OPEN]`, reported rather than reconciled here.) `numerus` returns `mensura`,
not `u64`: §9.5 forbids defaulting to the build platform, and a count of
elements in a host-side view is the target's own width by construction.

**`scalares` and `grapha` still have no methods, and `quaere`/`sectio` none on
any receiver.** §5.1 writes `numerus` on all three views; one prelude row is
one `fn` type, and a concrete receiver is compared by identity at the call
site, so a single `numerus` row cannot serve three receiver types. Three rows
per operation, or a receiver-polymorphic prelude form, is the fix and neither
shipped; `[OPEN]`. `quaere` and `sectio` stay `[OPEN]` for three reasons that
are each independently sufficient — the brand syntax `positio<'t>` does not
parse (below), `?` has no defined meaning in the language (spec §8.6), and
`eventus` is uninhabited (`docs/design/sum-types.md` section 2) so
`eventus<positio<'t>>` is a type nothing can produce a value of. That is why
§14 entry 2 remains deferred while entries 1 and 12 no longer are.

**Nothing below the checker can emit any of them.** `lower/expr.inc` refuses
all four by name at `__lwr_call_member`'s `.prelude:` — a `rassert`, not a
diagnostic, because no registered code means "unimplemented" and §8.3 makes
codes permanent (`docs/design/lowering.md`'s expression table carries the row).
A `shape=code` conformance fixture never lowers, so this costs those fixtures
nothing; it does mean no `tests/programs/` directory may call these four until
the runtime has a UTF-8 case folder and a view type.

**Brands.** `brand` is a type kind with `a` = the *declaration* of the
buffer (AST 2.5): two `positio` values are the same type iff they were
produced from the same binding or parameter, and `E0332` is a type mismatch
between two `brand` ids at an argument whose parameter is branded —
detected in pass 2 as ordinary id inequality, reported under `E0332`
rather than class C because the spec assigns it. **Generativity** is by
declaration, not call site: `abassus.quaere("/")` and `abassus.quaere(",")`
yield one brand, `via.quaere("/")` another. A receiver that is not a path
to a binding or parameter (a temporary) yields `a` = 0, an anonymous brand
no `sectio` accepts.

What the checker cannot do: know *which* result is branded and to *which*
parameter. Spec §5.1 writes `positio<'t>`, `'` is not a spec §8.4 token, and
§8.6 lists the syntax `[OPEN]`. Decision: the mechanism above is
implemented against a brand *type parameter* — a `GenericParam` marked as
binding to a value parameter — so that `quaere(self: 't textus, …) ->
eventus<positio<'t>>` needs one `TypeArg` form and nothing in the checker
changes; until §8.6 admits it nothing produces a brand and `E0332` is
designed but unreachable. `[OPEN]`; §14 entry 2 is blocked on the syntax
and on a prelude, both outside this document.

### 2.7 Generics: checked against the interface, never an impl

`GenericParam` decls and `param` types are the representation (AST 2.10);
a bound is an interface decl. Inside a generic body `a.combine(b)` with
`a: T`, `T: Summable`, resolves to `Summable`'s member `combine` — a name
lookup over the interface's member decls, which are the contiguous range
after the interface decl — and its row is the member's declared row. No
impl is ever consulted in a generic body (spec §7.1 compiles once; gendict
answer 1).

At an instantiation `total<Left>(a, b)` the checker looks up the impl of
`Summable` for `Left` in the **impl map** (`rt/map.inc`, key = (iface decl,
target type id), built in decl order in pass 2's signature loop; a second
impl for one key is a class-B error) and records the impl decl in `Call.d`
(section 2.5); the call's type is the callee's result with `T := Left`,
which is a substitution rebuilding types through `ast_type_intern` — a
recursion over the type table with a small ordinal → type-id vector, no
unification. **Method receivers:** an interface member's or impl method's
first parameter is the receiver and `x.m(a…)` passes `x` first; the tree
has no receiver syntax and no `Self` (finding 6), and the probe's untyped
`self` does not parse under §8.6.

**Parameter 0 is not compared at the call site when the receiver is generic,
and that is a narrowing with a named cost.** `__chk_ty_method`'s `.bound:` arm
falls through into `.iface:`, so both a `T: Trait` receiver and a `dyn Trait`
receiver resolve the member on the *interface* declaration — whose parameter 0
is spelled with the interface's own name. `__chk_ty_call`'s `.args:` used to
hand that parameter and the receiver to `__chk_ty_same`, which made `a.combine(b)`
with `a: T`, `T: Summable` an `EXS-E0303` at `a`: a type parameter compared
against the interface that bounds it, on code this section calls correctly
resolved. §14 entry 24 carried it as a blocker and now runs. The comparison is
skipped when the receiver's own type kind is `param` or `dyn`; a concrete
receiver is still compared by identity (`tests/unit/chk_ty_method_bound.asm`).

**Named non-goal.** A *malformed interface member* — `combine(self: u32, …)`
written inside the `interfacies` itself — is no longer caught at the call
site, because the call site is no longer where parameter 0's type is judged.
That check never belonged there: the rule is a declaration-site one, *parameter
0 of an `interfacies` member must be the interface's own name*, and it belongs
in `sig.inc`'s signature loop alongside the other shape rules. It is not
implemented. The candidate code is `EXS-E0303` (§13: type mismatch), which
would make the diagnostic move from the use to the declaration rather than
disappear — but whether a *declaration* shape rule should reuse the type-mismatch
code or whether §13 owes a distinct one is a design question, and CLAUDE.md is
explicit that a new code is a spec amendment first. `[OPEN]`; no code is
invented here.

What Stage 2 records for Stage 3's dictionary layout (spec §15 item 5): per
`Impl`, its methods in **the interface's member order** (stable across
impls — gendict answer 2, and what §7.2's "never change representation"
requires); per `GenericParam`, its ordinal and bound; per `Call`, the chosen
impls. Rows do not enter the table: the ceiling (section 2.3) bounds every
slot's row by the interface, so layout never depends on an impl's mark.
Layout itself — slot width, where carriers of a method's row live — is
Stage 3's and `[OPEN]`. Generic impl heads (`interfacies X<T> in acies<T, N>`)
are `[OPEN]` in spec §8.6 and unsupported here.

### 2.8 Runtime carriers: every atom is one pointer

IR 2.10 leaves per-capability carriers `[OPEN]`. **Decision: every atom has
a runtime carrier of type `ptr`, uniformly.** `Mundus` is the pointer passed
to `initium(m: Mundus) -> u8`; `ambitus` is a pointer derived from it and,
under the §4.6/§12 amendment in progress, owns the standard streams — so
the minimum hello world draws exactly `Mundus` (parameter) and `ambitus`
(derived, `sub`-bound); `alloc` is an arena pointer (spec §4.5). `archivum`
`rete` `horologium` `fortuna` `sermo` `Filum` `machina` are pointers into
runtime state whose contents are the runtime's, `[OPEN]`. `Crudum` carries a
pointer that is never dereferenced — a token — rather than being a
zero-sized marker, so that the calling convention is *row size*, computed
from the type alone, with no per-atom table in the lowering. Hidden
arguments are the row's **atom items in row order** (ascending atom decl id
= §4.6 order); ordinals never reach the IR — a `sicut` parameter's carriers
travel inside that argument's closure. IR 2.9 says "in written order",
which an interned, sorted row does not have (finding 15). A capture in a
lambda is a closure-environment slot, Stage 3's.

## 3. Verifier

`verify.inc` runs only when Stage 2 emitted nothing, and every failure is an
`rassert` (a malformed annotation is a compiler bug, not an `EXS-E` code):
AST section 3's Stage 2 list verbatim — `ty` ≠ 0 and ≠ 1 on every expression
and type node; `d` ≠ 0 on every `Seg` `Member` `RowItem`; every ordinal <
its `Sig`'s parameter count; `red` only on `ACCUM` decls — and, since
2026-09-25, on none of them in a program that checks clean, because section
2.3 replaces the handle with F when the loop closes; every `transitus`
field's `Layout.order` ≠ `nativus` — plus: every `fn` type's row id names a
row in `Ast.rows`; every row's items are strictly ascending (kind, id);
every `brand.a` is a `BINDING`/`PARAM` decl or 0; `Ast.sides` ≠ 0. The two
clauses ast/verify.inc could not check per node (the ordinal bound, the
`transitus` rule) are checked here with the context this pass has.

## 4. Determinism audit

| this order | is a function of |
|---|---|
| atom decl ids | `N` + spec §4.6 ordinal |
| module table, impl map | item / decl order (map insertion); looked up, never walked |
| scope frames, `seen` | walk order = node index order |
| type ids, row ids | first-use in the signature loop then the walk (map insertion) |
| row item order | ascending (kind, decl id) = ascending §4.6 ordinal, then ordinal |
| use list | walk order |
| fixpoint result | unique least fixpoint; visiting order is decl order but does not matter |
| capability-bearing | fixpoint over struct decls in decl order; unique |
| layout entries | field decl order within struct decl order |
| lexicon | public decl order |
| **diagnostic emission** | pass number, then node index within the pass |
| substitution results | `ast_type_intern` of a structurally determined rebuild |

No order names an address or a bucket; `rt/map.inc` is the only hash and is
never iterated. The last row matters because diagnostics *are* output
(spec §9.3, §8.3): two runs must print the same list in the same order.

## 5. Sizing

Two arenas. The tree's (`AST_ARENA_HINT`, 16 MiB) must absorb what Stage 2
adds to it — 11 decls, `Ast.types` beyond the 64-entry initial capacity
(`vec_push` doubles into a fresh block: a table reaching `T` types costs
≈ `2 × 16 × T` bytes plus 16 per interned key), `Ast.rows`, and
`ast_side_alloc`'s `9 × nodes + 8 × decls`. A checker scratch arena, reset
after pass 6 and never backing anything the tree points at, holds the
frame stack (≈ 60 B × lexical depth), the use list (16 B × uses ≈ number of
`Call` nodes), the row masks (16 B × decls), the substitution vector, the
module table and impl map. The fixed bucket counts bite twice: the type map
has 1024 buckets (kinds.inc) and degrades past a few thousand types; the
module table's bucket count should be the power of two ≥ 2 × item count,
computed from `Ast.decls.len`, not a constant. First estimate for a
10 000-line module (≈ 50 k nodes, 8 k decls, 3 k types): ≈ 0.6 MiB of side
tables and ≈ 0.1 MiB of types on the tree arena, ≈ 0.3 MiB of scratch. All
`[OPEN]`; no real module has been checked, and every figure above the
34 648 bytes ast/node.inc measured for `ast_init` is arithmetic, not
measurement. Over-run is an `rassert`, as everywhere in `rt/`.

## 6. Agent split

Four agents, exclusive subdirectories under `compiler/x86_64/checker/`,
each an include chain like `ast/`'s, aggregated by `checker/checker.inc`
which the first agent owns. Each fixture is a *pair*: a source rejected with
exactly one existing code and its twin, differing by one edit, accepted —
the pattern spec §4.2 calls load-bearing. Fixtures for rules whose code is
only proposed (section 2.4) are written but marked `status=blocked
needs=spec13` in the `; TEST:` directive and never counted as passing.

| agent | owns | depends on | non-vacuous fixture (reject / accept) |
|---|---|---|---|
| `checker-resolve` | `checker/resolve/` (passes 0, 1), `checker/checker.inc`, `checker/verify.inc` | `ast/` | `mutabilis c: i32 = 0;` at module level → `E0500` / `firma c: i32 = 0;`; shadowing pair blocked on `0422` |
| `checker-types` | `checker/types/` (pass 2) | resolve; rows' `chk_row_intern` | `t[0]` with `t: textus` → `E0311` / `redde t;`; `externus` with `refero<N>` → `E0520` / `refero_communis<N>` (§14 entries 8, 13) |
| `checker-rows` | `checker/rows/` (pass 3 **and** pass 4 — both fill per-declaration tables from `Decl.ty`) | types | `capcheck/cases/bad_laundering.exsc` → `E0421` / `ok_hof_subset.exsc`; §14 entry 24 → `E0510` / `gendict/cases/ok_static_matches_ceiling.exsc`; §14 entry 6 → `E0321` / `Capitulum` as written; entry 21 → `E0322` / `DeModFrame` |
| `checker-lexicon` | `checker/lexicon/` (pass 5), `tools/gen-lexicon.py` | types (signatures for §3.5 laws) | §14 entry 14 `lector` as `functio` → `E0602` / `lector` as `structura`; `relege` with `lege`'s signature / with a different one → `E0603` |

`chk_row_intern(tree, items, count) -> row id` — items are (kind, id)
pairs, kind 0 = atom or `potestas` decl (flattened), 1 = ordinal — is the
one API two agents share, so it is fixed here: rows agent delivers it first,
types agent calls it for `TyFn`, `TyDyn` and declared `Sig` rows. Order:
resolve → (rows' intern API) → types → rows → lexicon, with `verify` last.
Hand-built trees through `ast/load.inc` make every pass testable without a
parser, which is how each agent's unit fixture is written; the source pairs
above additionally run through `exsc aedifica` once the driver calls the
checker after `ast_verify_stage1` (driver/run.inc, the driver agent's
tree).

## 7. Hazards

- **H1 the three rules of section 2.1.** Any one relaxed reintroduces a
  search; review must treat "let a field provide `rete` implicitly" or
  "accept a smaller row here" as a redesign, not a convenience.
- **H2 bare fn types are the empty row.** Section 2.1 rule 3 contradicts
  §14 entry 9 and `capcheck/cases/bad_hof_no_row.exsc` as written; if the
  owner instead makes the bare form *open*, fn types stop comparing by id
  and IR 2.9's hidden-argument count stops being a function of the type.
- **H3 `Error` operands** (AST H3): the walk gives an `Error` node type id 1
  and says nothing; a pass that diagnoses it duplicates the parser.
  `[UNTESTED]`.
- **H4 `Decl.flags` is full** (kinds.inc): no ninth flag exists; anything
  new goes in `Decl.aux`.
- **H5 fixed capacities** (section 5).

## 8. Unresolved

Literal typing without §8.4's grammar; brand syntax and a prelude that
produces one; new-type `typus` (the ego's `publica typus textus` has no
source form); generic impl heads; multiple bounds; receiver syntax and
`Self`; what a `contrahe` whose body never contributes means (section 2.3;
the contribution syntax itself — finding 9 — is closed: spec §5.4 now carries
"Writing to it is the contribution", and section 2.3 and
`tests/unit/chk_ty_contrahe.asm` implement it); `@nucleus` enforcement
(narrowing `sicut` was on this list and is
settled as truncation in spec §5.4 — the checker already admitted it and
needs nothing); `numeri` coercion
between modules (there are no modules yet); `sub` in loop bodies; what
`sub alloc = a` accepts as `a` (every OTHER atom's provider is settled: it
must have that atom's own `cap` type, `EXS-E0303` otherwise, ADR 0017 D6 —
`sub archivum = d;` over a `Directorium` was a capability escape while it was
not); whether the standard streams' owner is
`ambitus` (amendment in progress); the morpheme table as an `ego`
dependency (§3.8) — the generated `.inc` from §3.3–§3.5 is a stand-in;
`E0105` once imports exist.

## 9. Findings against the spec and the AST

Numbered; each names the section and the sentence. Not edited here.

1. **AST 2.5 / kinds.inc — no type kind for a capability.** `m: Mundus`,
   `sock: rete` (spec §4.3) and `sub`'s `Decl.ty` need a `TypeNode` kind; the
   23 kinds in kinds.inc have none. Needed: `AST_TY_CAP`, `a` = atom decl.
   (ast agent.)
2. **AST 2.4 / kinds.inc — no declaration kind for an atom.** Section 2.2
   pass 0 needs `AST_D_CAPATOM` and a name-pool entry. (ast agent.)
3. **AST 2.6 — "both keys are declaration indices"** is true only with
   finding 2; without it an atom has no index. Also "sorted on (kind, id)
   before interning" — the sort is a bit scan once atom ids are consecutive.
4. **AST 2.3 / kinds.inc — `Call.d` is `AST_R_NONE`** and the verifier
   requires 0; section 2.5 needs it as `AST_R_ANY` for generic-argument
   impls. (ast agent.)
5. **Spec §8.6 `RowItem ::= Path | 'sicut' IDENT` in `TypeRow`.** A function
   type has no parameter names, so `sicut IDENT` in a braced row names
   nothing; it should be `DeclRow`-only or a diagnostic (class L).
6. **Spec §8.6 has no receiver parameter and no `Self`.** `Param ::= IDENT
   ':' Type` rejects the probes' `self`; `tests/conformance/entry12_…exsc`
   declares `scribe(t: textus)` with no receiver and `examples/imprime.exsc`
   calls `s.scribe(t)` — arity 1 vs 2 under any reading. Section 2.7's
   first-parameter rule is a decision the spec must confirm.
7. **Spec §4.3 "must be declared so"** — no declaration syntax exists in
   §8.6; the checker computes the mark instead.
8. **AST 2.5 `alias` kind** is unused under section 2.2's transparent
   `typus`; and the ego's `publica typus textus` (spec §10.1) declares an
   opaque type that `TypeDecl ::= … '=' Type ';'` cannot write.
9. **Spec §5.4 / §8.5 give no syntax for contributing to a `contrahe`
   accumulator**, so `E0341` (any read) has no complement. Section 2.3
   treats `acc = e;` inside the body as the contribution; needs the owner.
   **Closed 2026-09-25**: spec §5.4 now says it — "Writing to it is the
   contribution: inside the body, `acc = e;` contributes `e` under the
   operator `contrahe` declared, and is the only statement that may name
   `acc`" — which is this section's reading, adopted. What the sentence
   left open (what `e` is typed against, and how long `acc` lives) is
   finding 23.
10. **Spec §4.1 rules 5 and 6 conflict** on a private function with no
    `poscit`: rule 5 infers, rule 6 declares it pure. Section 2.3 reads rule
    6 as the meaning of an explicitly empty *declared* row.
11. **Spec §14 entry 9 and `capcheck/cases/bad_hof_no_row.exsc`** assume a
    bare `functio(f32) -> f32` parameter has an *open* row; §8.6 ("`poscit
    {}` is an explicit empty row") and §4.1 rule 3 make it empty. The attack
    is still rejected, at the call site, under a class-C code, not `E0421`.
12. **`capcheck/cases/bad_closure_capture.exsc` — `fabrica`** declares
    `poscit rete` and binds `sub rete`: two providers of one atom in one
    scope, the §4.5 shadowing error. Drop the `poscit rete`; the case still
    rejects at `exterior`. The probe's `sub_atoms_of` rule (a `sub`
    contributes to the row) contradicts §4.1 rule 4's "`poscit` means drawn
    from the enclosing scope, nothing else" and is not adopted.
13. **Spec §5.2 — two layout rules.** `DeModFrame` (certified against 246
    vectors) has `cursus: u16:maior` at byte 15 after `tempus: u24:maior`
    at 12: fields are **packed**, alignment 1. `Capitulum`'s comment
    ("implicit padding is `EXS-E0322`" on the `reserva` between `genus` at
    5 and `longitudo`) implies natural alignment. Only packed is consistent
    with the certificate; under it `Capitulum` without `reserva` is a valid
    10-byte struct and `tests/conformance/entry07_…exsc` **does not
    reject**. `E0322` fires on rules 2 and 4 only. The owner must say which
    example is wrong; section 2.2 pass 4 implements packed.
14. **Spec §5.1's methods have no declaration site** (`octeti` `quaere`
    `sectio` `plica_unicode`) and no import exists; §14 entries 2 and 8's
    fixtures call them undeclared. Entry 8 still reaches `E0311`.
    **Narrowed, not closed.** Four of them now have a declaration site —
    `checker/types/prim.inc` rows 12–15, `plica_unicode`, `plica_sermone`,
    `octeti` and `numerus`, looked up by the receiver's type *kind* (section
    2.6) — which is what let §14 entries 1 and 12 stop reporting a spurious
    `EXS-E0305`. What still has none: `quaere`, `sectio`, `scalares()`,
    `grapha()`, and `numerus` on any view but `octeti`. Nothing below the
    checker can emit even the four; the lowering refuses them by name.
    **Nor do
    its type names**: `i32`, `f32`, `textus`, `mensura`, `octeti`,
    `scalares`, `grapha` all parse as `TyPath → Path → Seg` and nothing in
    the repository declares them — not the module, not
    `prelude/interface.inc`. Raising `E0301` in type position would report
    every `i32` in every program, so pass 1 leaves a type-position miss
    silent and pass 2 owns the primitive table (its `int`/`float`/`textus`
    kinds are that table); `chk_verify` gates its "`Seg.d` ≠ 0" clause on
    the types pass having run. (Found by the resolve agent.)
15. **IR 2.9 "one `ptr` per row item in written order"** — an interned row
    has no written order; section 2.8 uses row (sorted) order.
16. **Spec §3 fails its own examples.** `applica` (§4.2) carries `ap-`, an
    assimilated `ad-` that §3.5 does not list and §3.6 forbids; `saluta`
    (`examples/saluta.exsc`), `construe` (§4.2), `imprime` and `principium`
    (`examples/imprime.exsc`) use roots §3.3 lacks; `plica_unicode`,
    `plica_sermone`, `imprime_gutenbergio` contain `_`, for which §3.1's
    decomposition has no rule; `textus`, `grapha` (§10.1) do not decompose
    over the 20 roots. Pass 5 as specified rejects the canonical program
    with `E0601`. Either §3.3's table is illustrative and the real morpheme
    table is a dependency that does not exist (§3.8), or the examples are
    wrong; until decided, `lexicon.inc` is built and tested against §3.7's
    own derivation table and §14 entry 14, and not enabled in the driver.
17. **Spec §3.3 has no conjugation column**, yet §3.4 now says `-e`
    realises as `-a` in the first conjugation; a checker needs the per-root
    imperative form (`lege`, `plica`, `tene`) in the table, not a rule.
18. **Spec §3.5 prefix laws presuppose a base form** and do not say where it
    must be declared; section 2.2 pass 5 checks a law only when the base is
    a public declaration of the same module, else accepts `[OPEN]`.
19. **Spec §4.5 "shadowing is an error"** forbids `sub alloc = a { … }` in
    any function declaring `poscit alloc` or taking an `alloc` parameter —
    no scoped sub-arena. Followed as written; recorded as a consequence.
20. **`tests/conformance/entry12_…exsc`** returns `s: ScriptorRetis` where
    `dyn Scriptor` is expected with no `sicut` cast (§8.6 decision 4 makes
    the cast explicit) and declares no impl of `Scriptor` for
    `ScriptorRetis`; as written it is a class-C/N error, not `E0510`.
21. **`prototypes/capcheck/cases/bad_dyn_escape.exsc`** uses `impl … for`,
    `as` and a struct literal, all inadmissible under §8.6 (the gendict
    README already notes this); it cannot be a Stage 2 fixture as written.
22. **AST 2.1 status line** says "the checker that fills them is
    `docs/design/checker.md`"; this is that document, and AST section 7's
    item 3 (spec §9.1 should point here) is still owed.

Findings 23–26 were added 2026-09-25, from the cascade-diagnostic wave that
made §14 entries 22, 11 and 10 emit their expected code and nothing else.
Every code set below was measured with
`build/exsc aedifica --hospes x86_64-linux --diagnostica json FILE`, on a
scratch copy where the fixture is missing a `;`, and the before/after pair is
stated in each.

23. **Spec §5.2 rule 3 fires once; pass 4 fired twice, and the cause is not
    hazard H3.** §14 entry 22 (`campus: u4:maior` in a `@transitus` struct)
    emitted `{EXS-E0201, EXS-E0322}` where rule 3's own text — *"`u4:maior`
    does not parse, so it is `EXS-E0201`. Neither needs a new code"* —
    presumes one. H3 says an `Error` node has type id 1 and pass 4 should say
    nothing about it, so the expected fix was to test for that id. **There is
    no `Error` node.** `cst/parse.inc`'s `.order` arm raises `EXS-E0201` and
    *keeps* the suffix in the tree — "the token is still put in the tree —
    losing it would cost the round trip, and the diagnostic is the
    deliverable, not the shape," which is that file's rule for the whole
    bit-width family — so `--emitte cst` shows `BIT_TYPE` + `ORDER_SUFFIX`
    with no `CST_ERROR`, and `--emitte ast` shows a fully formed
    `t 15 int 0 1 0 4 0 0`: an `AST_TY_INT` of width 4 carrying
    `AST_ORD_MAIOR`. Pass 4 was reading a well-formed tree, not debris it
    could recognise as debris.
    **Decision: the unwritable *shape* is the detection.** A `@transitus`
    field whose type is an integer of width < 8 with an explicit byte order
    cannot be written under rule 3, so it came from recovery; pass 4 lays the
    struct out and raises none of `E0321`, `E0309`, `E0322` for it
    (`__chk_lay_debris`, layout.inc). Width < 8 exactly, not ≤ 8, because that
    is the condition `__cst_core_type` raises `EXS-E0201` on; a wider test
    would silence a struct the parser said nothing about. Entry 22 now
    measures `{EXS-E0201}` and entry 21 still measures `{EXS-E0322}`.
    **Two things this finding leaves open.** (a) `u8:maior` — rule 3's "at or
    below" covers it and the parser admits it, so nothing rejects it today;
    reported to the cst agent, not widened here. (b) Whether a `Field` whose
    type is genuinely an `AST_ERROR` node is reachable at all: nothing
    measured produces one. Testing for it was the first version of
    `__chk_lay_debris` and `tests/unit/chk_row_layout_src.asm` case c22
    refuted it — `@transitus structura T<X> { x: X }` asserts
    `{EXS-E0301, EXS-E0321}` deliberately, because `E0301` is a *resolution*
    diagnostic and suppressing `E0321` behind it withholds a rule the program
    really does break. H3 is about the parser's codes, not every code.
    `[UNTESTED]` for the `Error`-node case.

24. **Spec §4.1 rule 7 is one code and the checker raised two.** *"No
    module-level mutable state. (`EXS-E0500`; with a capability,
    `EXS-E0501`.)"* — a parenthesis that *chooses*. Pass 1 raised `E0500` for
    every module-level `mutabilis` and pass 3 added `E0501` when the type bore
    a capability, so §14 entry 11 (`mutabilis retis_globalis: rete;`) measured
    `{EXS-E0500, EXS-E0501}` against an expectation of `{EXS-E0501}`.
    **Decision: the whole of rule 7 moves to pass 3**, the only pass that
    knows capability-bearing; pass 1 says nothing. No new `Decl.flags` bit is
    needed (hazard H4: there is none to spend) because both facts are already
    on the declaration — `AST_F_MUTABILIS` from Stage 1, and
    `AstDecl.parent == 0` for module level. `E0500` keeps its span (moved onto
    the declaration) and its §8.3 required fix, so `resolve.inc` still owns
    the fix *text* while `rows/compute.inc` owns the *choice*. Measured after:
    entry 11 `{EXS-E0501}`; `mutabilis c: i32 = 0;` `{EXS-E0500}` with the
    same span and fix as before; `firma c: i32 = 0;` clean; and
    `firma x: rete;` `{EXS-E0501}` — the case that shows the two codes do not
    nest, since `firma` is not mutable state. Section 2.4's table now reads
    pass 3 for `E0500`.
    **Answering the grammar question this raised:** a module-level
    `mutabilis x: rete;` with no initialiser **does** parse — `BindingDecl`'s
    `= Expr` is optional, confirmed by running — so entry 11 reaches Stage 2
    on its own terms. Without the `;` it also gets `EXS-E0203` ("unexpected
    end of input… it wants `;`"), which is that fixture's own defect and
    `tests/conformance/`'s to fix.

25. **`sicut` was never consulted at a call site, and function types do not
    intern canonically.** Section 2.1 rule 3 is two claims; only the second
    was built. §14 entry 10 — `applica(v: f32, f: functio(f32) -> f32) -> f32
    poscit alloc, sicut f`, §4.2's own worked example of a HOF written
    *correctly* — measured `{EXS-E0303, EXS-E0421}`: the argument compare
    reached `__chk_ty_same` with `nocens`'s `{rete}`-rowed type against the
    parameter's empty-rowed one and reported a mismatch, on top of the
    `E0421` `exterior` has coming. **Decision: at an argument whose parameter
    the callee's declared row names by ordinal, compare everything but the
    row** (`__chk_ty_issicut`, `__chk_ty_fneqnorow`, `__chk_ty_wantsicut`, all
    in types/types.inc, reached from `__chk_ty_call`'s argument loop). The
    argument's row is not compared against anything here — it *travels*, into
    pass 3's substitution, which is where the authority becomes visible to the
    caller's own declared row. Entry 10 now measures `{EXS-E0421}` and entry 9
    — the bare parameter — still measures `{EXS-E0303}`, which is what makes
    the pair non-vacuous.
    **And a second defect found while building it, wider than `sicut` and not
    fixed here.** `ast_type_intern`'s key is all sixteen bytes of `AstType`;
    an `fn` type's `a` is an `Ast.extra` *offset*; `ast_list_emit` appends a
    fresh run for every list it emits and never dedupes. So two structurally
    identical function types written in two places get two ids, and
    "compatibility is equality of interned type ids" (section 1.5) is false
    for every `fn` type. Measured: a bare `functio(f32) -> f32` parameter
    handed an ordinary `publica functio purus(x: f32) -> f32` with no `poscit`
    — both the empty row — is `EXS-E0303` today, and should not be. That is
    why `__chk_ty_fneqnorow` compares element by element instead of
    re-interning with the other's row: substituting a row and comparing ids
    would answer "different" for every pair. The fix belongs either in
    `ast/types.inc` (key an `fn` type on its element ids rather than on an
    offset) or in `ast/build.inc` (dedupe emitted runs), both the ast agent's
    tree, and it is the reason no fixture here asserts that two separately
    written identical function types are one type. `[OPEN]`.

26. **A unit fixture could not assert a code set spanning the parser and the
    checker**, which is what findings 23 and 24 both turn on.
    `tests/unit/chk_row_layout_src.asm`'s harness bails out with −1 the moment
    the lexer or the parser has said anything, so "exactly `{EXS-E0201}`, and
    nothing from pass 4" was unassertable in it. The three fixtures this wave
    adds — `chk_row_layout_debris.asm`, `chk_e0501_supersedes_e0500.asm`,
    `chk_ty_sicut_row.asm` — reuse that harness with the gate removed and
    return the whole diagnostic vector's length, so each row asserts a count
    and then each code by number. `tests/run.sh`'s `UNIT_FIXTURE_FLOOR` should
    rise by three (178 → 181) in the commit that lands them; that file is not
    this agent's tree, and adding fixtures never *trips* the floor (the test
    is `found < floor`), so nothing fails until someone raises it.

27. **Every valid `contrahe` program was refused** (2026-09-25; raised by
    the lowering pass, which could not get one past the checker to lower).
    Two defects, both measured with `build/exsc aedifica --hospes
    x86_64-linux --diagnostica json` on a `per i in 0..8 contrahe s: +
    forma ordinata { s = v[i]; } redde s;` and both now fixed, in the
    checker and not in the spec — the spec was right and the code was
    wrong at both:
    - **C1**, one `EXS-E0303` at the contribution. `__chk_ty_assign` typed
      the right-hand side against the target's own type, which for an
      accumulator is the `red` handle; spec §5.4 makes it F. F was
      recorded nowhere: `__chk_ty_forhead` interned `red` with `a` = the
      accumulator's declaration and left every other field 0. Fixed by
      recording F in the handle's `b` and typing the contribution against
      it (`__chk_ty_contrib`, section 2.3).
    - **C2**, one `EXS-E0301` on a read after the loop. `resolve.inc`'s
      `.for` arm walked the `ForHead` *inside* the frame it pushes for the
      loop variable, so the accumulator died with the body and spec §5.4's
      "After the loop `acc` is an ordinary binding" named a binding that
      was gone. Fixed by walking the head in the enclosing scope, which
      the head's grammar permits: it holds no expression.
    Not defects but consequences settled at the same time and recorded in
    section 2.3: the accumulator's two states, the never-contributing
    loop, and the cascade suppression that makes each rejected row one
    code. `tests/unit/chk_ty_contrahe.asm` is the fixture; the five shapes
    the lowering fixture carries are its accepted rows.
28. **typed-ast.md section 2.5's `red` row says "`a` = F"**, and the
    implementation has always used `a` = the accumulator's *declaration* —
    `__chk_ty_forhead` says so in its own comment, and it must, because two
    accumulators in one head have to be two distinct handles and F cannot
    distinguish them. Finding 27 adds `b` = F. The row should read "`a` =
    the `ACCUM` declaration, `b` = F (0 until the first contribution)".
    (ast agent; this document does not edit that tree.)
