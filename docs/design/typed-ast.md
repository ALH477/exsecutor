# Typed AST — design plan

Status: **built for Stage 1.** `compiler/x86_64/ast/` (7988 lines, 233d280)
implements sections 2.1–2.4's tree, `ast_from_cst`, `ast_dump` and
`ast_verify_stage1`; `exsc aedifica --emitte ast` dumps it, and both
`examples/*.exsc` build and verify. Every Stage 2 slot (section 2.2: `Node.ty`,
`Ast.rows`, `Ast.layout`, `konst`, `own`, resolution in `d`) is present and
empty, as designed; the checker that fills them is `docs/design/checker.md`.
Findings 7–10 of the review that followed are recorded and not yet acted on.
This line said "design only; no AST exists" until 2026-09-10.
`spec §N` cites `docs/spec/exsecutor-spec-v0.4.md`; `IR n.m` cites
`docs/design/ssa-ir.md`, which this lowers into. Taken as given: spec §9.1,
spec §8.6 (what the CST holds), `.claude/agents/cst.md`. This is the contract
the `ast` agent builds against and Stage 2 (checker) and Stage 3 (lowering)
consume. One recommendation per question, with its reason. Nothing here is
written into `docs/spec/`.

## 1. The constraints and what they force

1. **Freestanding x86-64 assembly** over `rt/{arena,vec,map,intern,span}`:
   indices into vecs, one append-only pool for every list, no node pointers
   (section 2.1). Nothing below needs a structure `rt/` lacks.
2. **spec §9.3.** Node, declaration, type and row ids are creation indices;
   every order is an index order (section 4). Maps are looked up, never
   walked; the one sort is over declaration indices.
3. **Spans on every node** (spec §8.3, §9.1): `rt/span.inc`'s 12-byte `Span`
   inline in every node and declaration; the constructor takes one.
4. **Stage 1 builds it, Stage 2 types it** (spec §16), so "typed" must mean
   something a tree can be *before* a checker exists (section 2.2).
5. **Built from the CST, lowered to the IR.** IR 2.5 assumes facts "of the
   typed AST" (binding order, address-taken), IR 2.7 that layout is resolved
   before it, IR 2.8 that the lowering knows every scope exit. Each is a slot
   here (section 2.12).

## 2. Decisions

### 2.1 Representation: one node vec, one declaration vec, one pool

    Node { kind u16, aux u16, ty u32, a u32, b u32, c u32, d u32, span Span }   36 bytes
    Decl { kind u8, flags u8, aux u16, name u32, node u32, ty u32, parent u32, span Span }   32 bytes
    Ast  { nodes Vec<Node>, decls Vec<Decl>, extra Vec<u32>,
           types Vec<TypeNode>, rows Vec<Row>, layout Vec<Layout>,   ; Stage 2 (section 2.2)
           own Vec<u8>, konst Vec<u64>,                               ; Stage 2 side tables
           numeri 4 x u8, root u32, names Interner }

Same shape as IR 2.1, for the same reasons, with two differences. First,
**postorder**: children are emitted before their parent, so every child index
is smaller than its parent's, a node is written once, and there is no `next`
chain and no late patching — the AST has no phis. Second, `d`, the
**declaration slot**: on a declaring node the `Decl` it introduces (Stage 1);
on a name-denoting node the `Decl` it resolves to (Stage 2); otherwise a
fourth operand. Lists are `extra` ranges, `a` = start, `b` = count. Ids are
1-based so 0 is `none`, matching `rt/map.inc`'s miss sentinel. The tree lives
in one arena per module; builder scratch (child lists collected before one
append to `extra`) in a second, reset per item. `Vec` growth moves its
backing block: hold indices across a push, never pointers.

### 2.2 What "typed" means at Stage 1: slots present, empty, filled in place

**Decision: one tree, type slots present at Stage 1 and empty, Stage 2 is an
annotation pass.** Not a separate unchecked→checked representation, not a
rebuild. Reasons, in order:

1. **The language has no implicit rewrites.** No promotion (spec §5.4), no
   `mensura` coercion (spec §5.2), no autoboxing (spec §6.3 decision 4),
   explicit `sicut`, explicit `&`/`*` (spec §8.6). A checked tree therefore
   has the *same shape* as the unchecked one; a checker only adds facts about
   nodes that exist, and a rewrite would buy nothing. Load-bearing: a future
   implicit conversion must be recorded as a tree rewrite, not smuggled in as
   a lowering (section 5, H2).
2. **Diagnostics, LSP, macros** (spec §9.1) key on node ids and spans. Two
   representations means two id spaces mapped for every Stage 2 diagnostic;
   a rebuild means Stage 2 owns spans, which spec §8.3 says cannot be
   retrofitted.
3. In assembly a second node type is a second set of kinds, accessors and
   walkers with no generics to share them.

Concretely: `Node.ty` is 0 after Stage 1 everywhere and non-zero after Stage
2 on every expression and type node. Type id 1 is the **error type**: a node
whose typing failed gets it and no diagnostic is raised on an operand of it
(cascade suppression). `Node.d` on denoting nodes and `Decl.ty` are 0 until
Stage 2. Facts with no slot go in **side tables indexed by node id** (`own`,
`konst`), which cost nothing until Stage 2 allocates them. IR 2.9's hidden
arguments and IR 2.8's `retain`/`release` are *not* annotations: the lowering
emits them from these slots (section 2.12); they are never nodes.

### 2.3 Node kinds

Terminals are spec §8.6's nonterminals; `id` is an intern id; `T` a type
node; `E` an expression node; `B` a `Block`. Kinds absent from spec §8.6 are
absent here — sum types, struct literals, labelled jumps, `si` as an
expression are all `[OPEN]` there and add kinds only by amending this table.

| kind | a | b | c | d | aux |
|---|---|---|---|---|---|
| `Module` | items | count | — | — | — |
| `Fn` | `Sig` | `B` or 0 | `Generics` or 0 | decl | `publica` `nucleus` |
| `Sig` | params (`Param`) | count | result `T` or 0 | `Row` or 0 | — |
| `Generics` | `GenericParam` list | count | — | — | — |
| `Param` `GenericParam` `Field` | name id | `T` or 0 (bound) | — | decl | — |
| `Struct` `Interface` `Potestas` | members | count | `Generics` or 0 | decl | `transitus` |
| `Impl` | `ImplHead` | fns | count | decl | — |
| `ImplHead` | interface `Path` | target `T` | `Row` or 0 | — | — |
| `Typus` | `T` | — | `Generics` or 0 | decl | — |
| `Externus` | fns (`Fn`, body 0) | count | library id | decl | abi id |
| `Binding` | `T` or 0 | init `E` or 0 | — | decl | `mutabilis` |
| `Row` | items (`RowItem`) | count | — | — | — |
| `RowItem` | `Path` or param id | — | — | resolved: capability decl or param ordinal | `sicut` |
| `Block` | stmts | count | first decl | — | decl count |
| `Sub` | capability `Path` | `E` | `B` or 0 | decl | — |
| `Redde` `Rumpe` `Perge` `ExprStmt` | `E` or 0 | — | — | — | — |
| `Assign` | lhs `E` | rhs `E` | — | — | — |
| `Si` | (cond, `B`) pairs | count | `aliter` `B` or 0 | — | — |
| `Dum` | cond | `terminus` `E` or 0 | `B` | — | — |
| `For` | range `E` | `B` | `ForHead` | loop-var decl | `quisque` |
| `ForHead` | `Contrahe` list | count | `forma` id or 0 | — | — |
| `Contrahe` | name id | — | — | accumulator decl | op |
| `Discerne` | scrutinee | `Casus` list | count | `aliter` `B` or 0 | — |
| `Casus` | pattern (`Lit` or `Path`) | `B` | — | — | — |
| `Lit` | raw interned text id (INT and STRING alike; quotes included, escapes undecoded while §8.4's literal grammar is `[OPEN]`) | — | — | — | class |
| `Path` | segments (`Seg`) | count | — | resolved decl | — |
| `Seg` | name id | `GenericArgs` or 0 | — | resolved decl | — |
| `Unary` `Binary` | `E` | `E` | — | — | operator |
| `Cast` `Index` `Try` | `E` | `T` / index `E` / — | — | — | — |
| `Call` | callee `E` | args | count | — | — |
| `Member` | `E` | name id | `GenericArgs` or 0 | resolved field or method decl | — |
| `Lambda` | `Sig` | `B` | — | decl | — |
| `TyPath` `TyDyn` | `Path` | `Row` or 0 (`dyn`) | — | — | — |
| `TyBit` `TyPtr` | — / inner `T` | — | — | — | width / `&` or `*` |
| `TyFn` | params (`T`) | count | result `T` | `Row` or 0 | — |
| `TySuffix` | inner `T` | ident id | — | — | `:` or `apud` |
| `GenericArgs` | args (`T` or `Lit`) | count | — | — | — |
| `Error` | — | — | — | — | — |

`ExprNS` is not a kind: the suffix restriction is the parser's, and the tree
does not remember it. Parentheses are not a kind: precedence is structure.
`Lit` keeps text, not value, because the numeric grammar is `[OPEN]` (spec
§8.4) and a value needs a width the checker supplies; Stage 2 writes the
value to `konst[node]`. Operator, width and class enumerations are the
builder's tables, in spec §8.6 order.

### 2.4 Declarations and scopes

Every name-introducing construct pushes one `Decl` in **source order**:
functions, parameters, generic parameters, structs, fields, `typus`,
interfaces, members, implementations, `potestas`, `externus` blocks and their
functions, `firma`/`mutabilis`, `sub`, loop variables, `contrahe`
accumulators, lambdas. `Decl.parent` is the enclosing `Decl` (module = 0),
`Decl.node` the introducing node. `Decl.flags`: `publica`, `mutabilis`,
`transitus`, `nucleus`, `externus`, `capability_bearing` (spec §4.3, Stage
2), `address_taken`, `memory_resident` (Stage 2; the two facts IR 2.5 reads).

So **a block's bindings are the contiguous range `[Block.c, Block.c +
Block.aux)`**, nested blocks owning sub-ranges: what IR 2.5's "var id =
binding order" indexes and IR 2.8's scope-exit release walks (section 2.9).
Resolving a use to its `Decl` is Stage 2's first act: lexical and
non-searching (spec §4.5), but the failing case is a diagnostic whose code
the checker raises, and Stage 1 should not pick one.

### 2.5 Types: a syntactic tree and an interned table

Type *expressions* are nodes (section 2.3, `Ty*`), with spans, as written.
*Types* are `TypeNode`s in `Ast.types`, interned by content through a map on
their bytes, id = first-use index — the same scheme as IR 2.2, richer:

    TypeNode { kind u8, sign u8, order u8, place u8, width u32, a u32, b u32 }   16 bytes

| kind | fields | meaning |
|---|---|---|
| `error` | — | id 1, reserved (section 2.2) |
| `int` | `sign` `width` | `uN` `iN`, 1 ≤ N ≤ 64; `u1` is the boolean |
| `float` | `width` | `f32` `f64` |
| `mensura` | — | spec §5.2; concrete after `--hospes` (spec §9.5) |
| `textus` `octeti` `scalares` `grapha` | — | spec §5.1, distinct, non-coercing |
| `ptr` `ref` `refc` | `a` = T | `*T`, `refero<T>`, `refero_communis<T>` (spec §6.4) |
| `borrow` | `a` = T | `&T` `[OPEN]`: spec §7.2 defers ownership, so what `&` means beyond "address of" is not settled |
| `acies` | `a` = T, `width` = N | spec §5.4, lane count in the type |
| `struct` `alias` `iface` `param` | `a` = decl | nominal; `alias` (`typus`) as alias-or-new-type is `[OPEN]` |
| `fn` | `a` = extra (params… result), `width` = param count, `b` = row | spec §4.2: the row is *in the type* |
| `dyn` | `a` = interface decl, `b` = row | spec §4.4 |
| `eventus` | `a` = T | the `?` operand `[OPEN]`, with `+?` (IR 6) |
| `brand` | `a` = buffer decl | `positio<'t>` (spec §5.1); syntax `[OPEN]` |
| `red` | `a` = F | reduction handle, checker-internal (section 2.7) |
| `unit` | — | no result |

`order` (`nativus` `maior` `minor`) and `place` (host, `machina`) are fields
of the type node: that is what makes spec §5.2's "byte order is in the type"
and spec §5.5's "placement is in the type" literally true — `u32:maior` and
`u32` are different ids. A repeated or contradictory `TySuffix` is the
checker's diagnostic under a §13 code this document does not choose.

### 2.6 Capability rows: what the tree must be able to represent

Rows appear in four places (spec §8.6, "Where `poscit` attaches"): a
declaration's bare row, an implementation head, a function type, a `dyn`
type. All four are `Row` nodes as written — items in source order, each with
a span; `sicut f` is an item whose `d` becomes the **ordinal** of parameter
`f`, never its name. That ordinal is spec §4.2's positional substitution: the
checker replaces it with the row in the corresponding argument's *type*
(`TypeNode.b` of a `fn`), where an escaped closure's authority lives.

`Ast.rows` is the checker's table: `Row { items extra, count }`, item =
(kind: capability | parameter ordinal, id), **sorted on (kind, id)** before
interning so `{alloc, rete}` and `{rete, alloc}` are one row id; both keys
are declaration indices, so the sort is deterministic. Stage 1 never fills
it; an inferred row (spec §4.1 rule 5, lambdas) is a row id on a `fn` type,
computed by Stage 2.

For spec §15 item 5 — rows × generics × dictionary layout, unprototyped —
this document claims only that the tree carries what a solution would read:
each `Impl` its head row and each member `Fn` its own `Sig.d`, so a witness
table can be built per implementation with rows beside methods; each `dyn`
type a bound row for `EXS-E0510`; each generic parameter a `Decl` a dictionary
slot can be numbered from. Which capabilities have a runtime carrier is IR
2.10's `[OPEN]`, not decided here either.

### 2.7 `numeri` and reduction shape

`numeri` is the module's (spec §5.4, §10.1), not any node's: `Ast.numeri`
(4 × u8), set by the driver from the parsed `ego`, copied by the lowering
into every `Func` header (IR 2.6). No per-function slot exists because spec
§8.6 gives no syntax for one; the inter-module coercion is a call.

Reduction shape lives on the loop: `ForHead` carries the `contrahe` list and
the `forma` identifier. `summa_ordinata`/`summa_arborea` are ordinary `Call`
nodes; that they lower to `redinit`/`contrib`/`redfin` is the lowering's
recognition of the resolved `Decl`, and whether they are library functions or
intrinsics is `[OPEN]` — spec §5.4 writes them with no declaration site. The
accumulator `Decl` gets type `red.F`, which admits no read (IR 2.6), so "not
readable in the body" and "`rumpe` forbidden with `contrahe`" are checker
rules on this tree; both need a §13 code the sections read do not supply —
**not chosen here; the checker cannot raise them until one exists.**

**Contradiction found, and since fixed.** Spec §8.6 had `'forma' IDENT` with
no width, while spec §5.4 defines `arborea w` and IR 2.6 lowers
`redinit … arborea 8` — so a bare `forma arborea` could never have been
lowered. §8.6 now reads `['forma' IDENT [INT]]`, amended in response to this
document. **`ForHead.d` therefore holds the width**, where an earlier draft of
this paragraph said the tree reserves nothing. The width is required after
`arborea` and forbidden after `ordinata`, which is a checker rule: both parse.

### 2.8 `@transitus` and layout: resolved here, in Stage 2

Yes: layout is resolved on this tree, because IR 2.7 forbids the backend
from choosing one and `EXS-E0321`/`EXS-E0322` are diagnostics with spans
against a field, which only a pass here can raise. Stage 1 records what is
written: `TyBit.aux` (the parser-decided width, spec §8.6), the `TySuffix`
order, the `transitus` flag. Stage 2 fills

    Layout { byte u32, bit u8, width u8, order u8, align u8 }   8 bytes

in `Ast.layout`, **indexed by `Decl` id** — a field's offset; a struct's
size and alignment in its own entry; other decls unused, 8 bytes each,
accepted for the absent indirection. Non-`@transitus` structs get a layout
by the same pass with padding permitted; `mensura` is concrete because spec
§9.5 fixes `--hospes` first. The lowering reads `Layout` into `load`/`store`/
`loadbits`/`storebits` operands and nothing else. Spec §5.2's four rules are
the checker's; the tree guarantees only that their facts sit in one place
with spans.

### 2.9 ARC: not in the tree; the tree carries what lowering needs

`retain`/`release` are IR instructions (IR 2.8), never AST nodes. The tree
carries three facts the lowering reads: the **scope structure** (section 2.4
— a block's `Decl` range, released in reverse index order at every exit;
reverse declaration order is this document's choice, spec §6.6 saying only
"deterministic" `[OPEN]`); the **reference kinds** in the type table (`ref` /
`refc`: atomicity is a type fact, spec §6.3 decision 2); and `own[node]`
(Stage 2), whether an expression yields an owned or borrowed reference —
parameters borrowed, returns owned (spec §6.3 decision 3, IR 2.9).
`EXS-E0520` is a check on `fn` types at `Externus` members.

### 2.10 Generics: declared once, instantiated in the type table, never copied

`GenericParam` decls and `param` type nodes are the whole representation.
Instantiation is substitution in `Ast.types` (`acies<T, 4>` with `T := f32` is
another interned id); no instance of a body ever exists here — spec §7.1
compiles generic code once, monomorphises only at link time, and a per-module
tree cannot see its consumers. The lowering counts a callee's `GenericParam`
decls to emit IR 2.9's dictionary `ptr`s; contents are spec §15 item 5.

### 2.11 The CST boundary: what is dropped, and why that is safe

Input: the green root and the file's `file_id`; the builder walks red nodes
in postorder, one `Node` per meaning-bearing nonterminal, with
`Span{file_id, red.text_offset, green.text_len}` minus trivia. No back-link
to a CST node: green nodes are shared and positionless, red nodes are not
persisted, so **the span is the link** — a consumer finds the CST node by
offset.

Dropped: whitespace, comments, punctuation and keyword tokens, parentheses,
the `ExprNS` distinction, literal spelling beyond the interned text. Safe
because the formatter and LSP read the CST (spec §9.1), diagnostics need only
spans, and nothing semantic lives in trivia. **Kept:** parse failure. An
`ERROR` CST node becomes an `Error` node with its span (a `MISSING` node is
never visited — the walks iterate interior children — so a missing `;` leaves
`Redde.a` the intact expression, as `ast/` documents and
`tests/unit/ast_from_cst_missing.asm` proves), so a
malformed file still yields a tree and Stage 2 still types the rest —
spec §16's Stage 1 kill criterion is diagnostics, and a checker that stops at
the first parse error cannot report a second. The `ego` file has its own CST
entry point (spec §8.6) and lowers into `Ast` header fields (`numeri`; the
rest `[OPEN]`), not into `nodes`.

### 2.12 The IR boundary: what the lowering reads

| this tree | becomes (IR section) |
|---|---|
| `Ast.numeri` | every `Func.numeri` (IR 2.6) |
| `Fn` decl, `Sig`, `fn` type | `Func`, `Sig`; hidden args by generic-param count and row carriers (IR 2.9) |
| `Block` decl ranges, `own[]` | `retain`/`release` at every exit (IR 2.8) |
| `Decl.flags` `memory_resident` / `address_taken` | `slot` + `load` vs SSA variable (IR 2.5) |
| `Ast.layout` | `load`/`store`/`loadbits`/`storebits` operands (IR 2.7) |
| `Binary.aux` `+` `+%` `+\|` | three opcodes, never a flag (IR 2.3); `+?` `[OPEN]` |
| `Cast` with two int types | `zext`/`sext`/`trunc` by the types; narrowing `[OPEN]` (IR 2.3) |
| `Index` | `chk` then `index` (IR 2.7) |
| `For` + `Contrahe` | `redinit`/`contrib`/`redfin` (IR 2.6); `quisque` sets the header flag (IR 2.4) |
| `Dum` with `terminus` | counter + trap (IR 5, H3) |
| type table | `struct` → `ptr` to bytes, `acies` → `ptr`, `fn` → `fn.S`, `dyn` → two `ptr`s, `textus` `[OPEN]`, `param` → `ptr` + dictionary (IR 2.2) |

Rows do not cross (IR 2.10); the type table, decls and side tables are
consumed and not carried. The lowering is Stage 3 and `[UNIMPLEMENTED]`.

## 3. Verifier

Two passes, both `rassert` (a malformed tree is a compiler bug, not an
`EXS-E` code). **After Stage 1:** every child index < its parent's; every
list range inside `extra`; every `Decl.node` points at a node whose `d` is
that decl; block decl ranges nest; every `Error` node has a diagnostic
against its span. **After Stage 2, with no diagnostic emitted:** `ty` ≠ 0 on
every expression and type node and never 1; `d` ≠ 0 on every `Seg`,
`Member`, `RowItem`; every `RowItem` ordinal < its `Sig`'s parameter count;
`red` only on `Contrahe` decls; every `transitus` field's `Layout.order` ≠
`nativus`.

## 4. Determinism audit

| this order | is a function of |
|---|---|
| node ids | postorder of the CST walk, files in `ego` `fontes` order `[OPEN]` |
| decl ids | source order within that walk |
| type ids, row ids | first-use order (map insertion) in Stage 2's walk, which is node-index order |
| row item order | sort on (kind, decl index) |
| release order at scope exit | reverse decl index |
| intern ids | first-seen order (`rt/intern.inc`) |

No order names an address, a hash bucket, or a green-node pointer. Green
interning may share subtrees; the AST never observes which.

## 5. Hazards

- **H1 fixed capacities** (IR 5, H1): arena size per module and the
  type-table bucket count are estimates `[OPEN]`; a `Block` with more than
  65535 bindings overflows `aux` — `rassert`.
- **H2 the no-implicit-rewrite premise.** Section 2.2 stands only while spec
  §5.2/§5.4/§6.3 forbid implicit conversion; review must catch a "lowering"
  that is really a rewrite.
- **H3 `Error` nodes in Stage 2.** A checker that diagnoses an `Error`
  operand duplicates the parser's report; the error-type rule (section 2.2)
  prevents it and is `[UNTESTED]`.
- **H4 module = file?** Spec §8.6's `Module ::= Item* EOF` is per file; spec
  §10.1's `fontes` lists several. `file_id` in every span works either way,
  but decl order across files depends on `fontes` order `[OPEN]`.

## 6. Unresolved

`forma` width; whether `summa_*` are intrinsics; the two missing §13 codes
(section 2.7); `&T`'s meaning; `typus` alias vs new type; `?`/`eventus` and
`+?`; brand syntax; `textus` at the IR boundary; annotation arguments (a
`Fn.aux` bit set is all that exists); generic implementation heads (spec
§8.6); runtime carriers and dictionary layout (spec §15 item 5); destruction
order; what a macro system needs beyond spans and `file_id` (spec §9.1 names
macros, nothing specifies them); `ego` header fields beyond `numeri`; sum
types, patterns, struct literals, labelled jumps, `sub` in loop bodies — all
downstream of spec §8.6's own open list.

## 7. Spec amendments this design implies (owner's job, not done here)

1. ~~Spec §8.6: `ForStmt` needs a width operand on `forma`.~~ **Done** —
   §8.6 reads `['forma' IDENT [INT]]`.
2. ~~Spec §13: two codes are needed — reading a `contrahe` accumulator, and
   `rumpe` inside an iteration carrying one (spec §5.4).~~ **Done** —
   `EXS-E0341` and `EXS-E0342`, in `03xx` alongside the other §5.x semantic
   rules. Neither carries a fix; both edits are restructures.
3. Spec §9.1: "Typed AST" should say the type slots exist at Stage 1 and are
   filled at Stage 2, and point here as it points to `ssa-ir.md`.
4. Spec §6.6: state the destruction order at scope exit.
