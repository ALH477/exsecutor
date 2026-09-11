# Lowering — design plan (Stage 3, AST → SSA)

Status: **design only; nothing implemented** — still true: no lowering exists
(the builders it asked of `ir.inc` landed in 612b0c9, with `bfa_edge_push`'s
argument order corrected here to match). No IR
in the tree was produced by one: `backend_fasmg/` has run only on
hand-written text. Every sentence below that describes emitted IR is what
this design *obliges*, `[UNTESTED]` until `compiler/x86_64/lower/` runs.
`spec §N` cites `docs/spec/exsecutor-spec-v0.4.md`; `IR n.m`
`docs/design/ssa-ir.md`; `AST n.m` `docs/design/typed-ast.md`; `CHK n.m`
`docs/design/checker.md`; `RT n.m` `docs/design/runtime.md`. Taken as
given: spec §9.1's construction method (Braun et al., CC 2013), spec §8.5,
§5.4, §6, §4.5; IR 2.5 (the structure mapping this document fills in),
IR 2.8–2.10; CHK 2.5 (the input invariant); RT 2.2, 2.3 and 5 (the exact
IR of the hello world, which is this pass's first output). One
recommendation per question, with its reason. Nothing here is written into
`docs/spec/`; the exclusive write scope is this file.

## 1. The constraints and what they force

1. **Freestanding x86-64 assembly** over `rt/{arena,vec,map,intern,span}`
   and `backend_fasmg/ir.inc`'s builders. Braun's algorithm is written for
   a language with hash maps, use lists and recursion; here `currentDef` is
   one `rt/map.inc` map with an 8-byte key, use lists do not exist
   (section 2.3), and the one recursion — `readVariableRecursive` into
   predecessors — is bounded by loop depth, not by function size.
2. **spec §9.3.** Every value id, block id, global id and phi operand order
   is a function of the AST walk order (section 2.10). `rt/map.inc` is
   looked up, never iterated.
3. **Braun, on the fly.** No CFG or dominance analysis before or during
   the walk (IR constraint 3). What Braun needs that a tree does not carry
   — which bindings are SSA variables, when a block's predecessor set is
   complete — the tree already answers: AST 2.4's contiguous decl ranges,
   `Decl.flags` (CHK 2.5), and the nesting of spec §8.5's constructs, each
   of which knows its own join (section 2.5).
4. **The input invariant is CHK 2.5, and the lowering raises nothing.** It
   runs only when Stage 2 emitted no diagnostic, so every `Node.ty` ≠ 0,
   every `Seg.d`/`Member.d` ≠ 0, `konst` on every `Lit`, `own` on every
   reference expression, `Decl.flags` complete. `EXS-E0341`, `E0342`,
   `E0306`, `E0307` are the checker's; a tree that reaches this pass and
   still needs one is a checker bug and an `rassert` here, never a code.
5. **The output invariant is IR section 3 and RT section 5.** The verifier
   runs on every function this pass emits (section 2.11), and the three
   functions of RT 5 are the first fixture: the text `bfa_print` writes for
   the hello world's trees must equal RT 5's byte for byte (section 3).
6. **No-grow arena** (`rt/arena.inc`). Every per-function structure is
   sized from the AST node count before the walk (section 4), over-run is
   an `rassert`, and the builder holds indices across every `vec_push`,
   never pointers.

## 2. Decisions

### 2.1 Shape of the pass: one walk, one state record, two arenas

One recursive walk from `Module.items` in item order, statements in order,
expression operands left to right, exactly the order `ast_from_cst`
created the nodes in (postorder within a statement, statements in source
order) — so a node's IR is emitted at its parent's visit, after its
operands', and value ids are creation-ordered. Threaded state:

    LowState { tree, module, func, cur u32 (current block, 0 = dead),
               defs Map ((var, block) 8 bytes -> value id),
               incomplete Vec<{block, var, phi}>,
               vars Vec<{kind u8, ty u32, val u32}>   ; indexed by decl - fn_first_decl
               scopes Vec<Frame>, loops Vec<Loop>,
               cap[11] per Frame, fn_first_decl, synth u32 }

`defs`, `incomplete`, `vars`, `scopes`, `loops` live in a **scratch arena
reset per function** (IR 2.1's second arena); the IR itself in the module
arena. The walk never re-visits a node: Braun's late phis are created at
the *read* that needs them (in walk order) and filled at the seal, which is
a fixed point of the construct being walked — never a second pass over the
tree.

### 2.2 Variables: what is SSA-renamed, what gets a `slot`

**A binding is an SSA variable iff its type is scalar and the checker did
not set `AST_F_MEMRES`; its var id is its `Decl` id.** CHK 2.5 sets
`memory_resident` on "non-scalar type or address taken", so the lowering
reads one flag and never re-derives it (AST 2.12's row for `Decl.flags`).

| `TypeNode.kind` | IR type | SSA? |
|---|---|---|
| `int` | `uN`/`iN` | yes |
| `float` | `f32`/`f64` | yes |
| `mensura` | `u64` (`x86_64-linux`, RT 2.3) | yes |
| `ptr` `borrow` `cap` `fn` | `ptr` | yes (`fn` is a closure `ptr`, IR 2.9) |
| `ref` `refc` | `ref`/`refc` | yes; released at scope exit (section 2.6) |
| `textus` (16 bytes, RT 2.3), `struct`, `acies`, `dyn` (two `ptr`s), `octeti`/`scalares`/`grapha`, `eventus`, `brand` | bytes behind a `ptr` | no: `slot size align`, `load`/`store` |
| `red` | `red.F` | neither: the accumulator is a loop-stack record (section 2.5) |
| `unit` | — | — |

A memory-resident binding gets `%s = slot size align` **at its declaration
point in the current block**, sizes from `Ast.layout` for `struct`, 16/8 for
`textus`, `N × elem` for `acies` (element stride of a non-`@transitus`
scalar narrower than 8 bits `[OPEN]`, section 7). RT 5 shows the slot
placed inline (`%2 = slot 16 8 ; firma s: Scriptor`), not hoisted to the
entry block; the naive backend carves it wherever it appears and nothing
here depends on hoisting. Every read of such a binding is a `load` at the
binding's type (scalar address-taken case) or the slot `ptr` itself
(aggregate case); every write a `store` or a `copy`.

Parameters: `%p = param T i` for every position of the IR signature
(section 2.9), emitted first in `b0` in position order; a scalar parameter
is an SSA variable whose initial write is its `param`; an aggregate
parameter is a `ptr` the callee never writes (IR 2.9), so its `vars`
record is kind *aggregate-param* with `val` = the `param` value and no
slot. `param` has no `own`: parameters are borrowed (spec §6.3, decision
3).

Synthetic variables — the `terminus` counter, the short-circuit result of
`et`/`vel` — take var ids `Ast.decls.len + k`, `k` counted per function in
walk order (`LowState.synth`), so they never collide with a decl id and are
deterministic.

`sub P = e;` (statement form) is an `AST_D_SUB` decl of type `cap`: an SSA
variable like any other, written once with `e`'s value, plus an entry in
the current frame's `cap[P]` (mirroring CHK 2.1's frame, 11 slots, so that
a call's hidden carrier for `P` is `readVariable(cap[P])` — section 2.7).
The block form pushes a frame with `cap[P]` set. RT 2.2's "lowers to
nothing" is right for the hello world — `sub ambitus = a;` writes the
variable `a` already holds — and the frame entry is the something.

### 2.3 Braun's six procedures on `rt/`, one to one

IR 2.5 mapped the structures; this fixes the procedures. `V` a var id, `B`
a block id, both `u32`; `key(V, B)` the 8-byte little-endian pair; every
`map_insert` copies its key into the scratch arena first (`rt/map.inc`
does not copy keys — the discipline `bfa_type_intern` already follows).

| Braun et al. | here |
|---|---|
| `writeVariable(V, B, v)` | `map_insert(defs, key(V,B), v)`. `rt/map.inc` has no update: a second write to the same key **appends and shadows** (`map_get` finds the newest). Cost: one 16-byte `MapEntry` plus an 8-byte key per write, never reclaimed until the per-function reset (section 4). |
| `readVariable(V, B)` | `map_get`; hit → follow the `nop` chain (below) and return; miss → `readVariableRecursive`. |
| `readVariableRecursive(V, B)` | `B` unsealed (`BfaBlock.flags & SEALED = 0`): `%p = phi T` with no operands, **prepended** to `B`'s phi list, `incomplete.push({B, V, %p})`, `writeVariable(V, B, %p)`, return `%p`. `B` sealed with exactly one predecessor: return `readVariable(V, pred)`. Otherwise: `%p` as above, `writeVariable` first (so a cycle through `B` reads `%p`), then `addPhiOperands(V, %p)`. |
| `addPhiOperands(V, %p)` | for each edge of `%p`'s block in **edge-list order** (`BfaBlock.first_pred` → `next`): `readVariable(V, pred)` into a scratch vec of `(pred, value)`; then one `bfa_extra_push` run of `2 × npreds` words, `Inst.a` = start, `Inst.b` = npreds; then `tryRemoveTrivialPhi(%p)`. |
| `tryRemoveTrivialPhi(%p)` | scan operands, chains followed: if every operand is `%p` or one value `s` → `Inst.op = NOP`, `Inst.a = s` (Braun's `same`; a phi with no operand but itself has `s = 0`, Braun's *undef* — an `rassert`, since a read-before-write of a scalar is the checker's definite-assignment error, section 7). **No recursion into users** (below). |
| `sealBlock(B)` | for each `incomplete` record with block `B`, in record order: `addPhiOperands`; then set `SEALED`. Records are threaded by `next` from a per-block head, so this is a list walk, not a scan. |

**The `nop` chain.** A removed phi is `op = 0` with `a` = its survivor —
`verify.inc` already reads `op == 0` as `nop` (its finding 2), and
`ir.inc` has no `BFA_OP_NOP`; this pass uses 0 and asks for the constant
(section 8, finding 10). Every operand read follows the chain to a
non-`nop` value before use, so an instruction emitted after a removal never
names a `nop`; an instruction emitted *before* the removal may, and the
final canonicalisation (below) rewrites it.

**Decision: keep IR 2.5's sweep; do not build use lists.** Braun's
recursion into users exists to re-test phis whose operand was just
replaced. Two facts make a deferred sweep equivalent: removing a trivial
phi never makes a non-trivial phi *non*-trivial (an operand `q` becomes
`same(q)`, which can only collapse a set of distinct operands, never grow
it), and triviality of a phi depends only on the final chain-resolved
operand set. So "remove trivial phis until none remain" has one fixed
point whatever the visiting order, and Braun's user recursion and an
index-order sweep to a fixpoint reach it. The on-the-fly local check at
`addPhiOperands` stays (it removes the common case immediately, so most
later reads never see a phi at all); the sweep runs once per function,
after the last seal, over phis in index order, repeating while a pass
removed anything. That equivalence is an argument, not a measurement:
**`[UNTESTED]`** that the sweep's output is minimal wherever Braun's is,
and section 5's second fixture counts what the sweep removes so the claim
is checked on the first nested-loop tree, not assumed. Braun's minimality
theorem itself assumes reducible CFGs; the source has no `goto` (IR 2.4),
so the irreducible-graph step is not needed.

What each costs on the no-grow arena: use lists are one `{user, next}`
record (8 bytes) per operand of every instruction — about twice the
instruction count, in a pool that cannot be freed before the function
resets, plus a head per instruction that the 32-byte `Inst` has no field
for. The sweep costs no memory and `O(phis × rounds)` time, rounds bounded
by the longest chain of phis that become trivial only through another's
removal — one on every tree this document can draw (section 5, second
fixture).

**Canonicalisation, then compaction.** After the sweep: every operand slot
of every instruction (inline `a`/`b`, or the `extra` range, per
`BFA_SH_*` shape) is rewritten to its chain-resolved value; each block's
phi list is rebuilt skipping `nop`s. Then the `insts` vec is **compacted**:
a dense renumbering old → new over the non-`nop` records, applied to every
operand and every block list head. Reason: `verify.inc` rule 9 scans the
whole `insts` vec ("no `nop`", a raw scan — its own words), so an unlinked
record would still fail it, and `emit.inc` gives every internal id a stack
slot, so dead records would cost frame bytes. Compaction is a pure function
of the pre-compaction ids, so determinism (section 2.10) survives it; the
alternative — the verifier walking lists instead of the vec — is offered in
section 8, finding 11, and either closes the gap. Both canonicalisation and
compaction need the per-shape operand-slot table that `print.inc` and
`parse.inc` already encode; this pass asks the backend to export it rather
than copying it (section 5).

### 2.4 Blocks, edges, terminators, sealing

`bfa_block_new` creates; **a terminator is the only thing that creates an
edge**, through two helpers that wrap `bfa_inst_push` + `bfa_block_link_body`
+ one `bfa_edge_push(func, cur, target)` (from, to) per successor in operand order
(`br`: true target then false target). `bfa_edge_push` does not exist —
`verify.inc`'s finding 1 records that nothing populates `first_pred`/
`last_pred`/`edges` — and is the first builder this pass needs (section 5).
After a terminator `cur` is 0 (**dead**): statement lowering on a dead
`cur` is skipped entirely, expressions in it are never evaluated, and a
construct whose join would have no predecessor never creates the join.
**Join and exit blocks are created on the first edge into them**, so an
empty block with no terminator (verifier rule 2) cannot arise, and a block
id is still a function of walk order.

Sealing: a block is sealed the moment the walk knows its predecessor set is
complete — immediately for a block with one predecessor whose terminator
was just emitted; at the join for a join; at the back edge for a loop
header; after the whole body for a loop exit. `b0` is sealed at creation
(no predecessors). Section 2.5 gives the order per construct.

### 2.5 Every §8.5 construct: block diagram and seal order

Blocks are named by role; ids are assigned in creation order, which is the
order the roles appear below. `→` is an edge; `seal(X)` the point in the
walk where `sealBlock` runs. Scope frames (section 2.6) are pushed at
every `Block` node the construct owns and popped at its end.

**`si c1 B1 sin c2 B2 … aliter Bn`** (`Si`: `(cond, B)` pairs, `c` =
`aliter` block or 0).

    cur:   %c1 = <c1> ; br %c1 body1 test2        (test1 is cur itself)
    body1: <B1> ; jmp join                        seal(body1) at creation
    test2: %c2 = <c2> ; br %c2 body2 test3        seal(test2) at creation
    …
    testn / aliter: <Bn> ; jmp join   — or, with no aliter, the last test's
                                        false edge goes to join
    join:  seal(join) after the last arm

Each body and each test has one predecessor and is sealed when created,
so a read inside an arm never creates a phi; the join's phis are Braun's
usual join phis, filled at `seal(join)`. Arms that end dead (a `redde` in
every path) add no edge; a `join` with no edge is never created and the
statements after the `si` are dead.

**`dum c [terminus e] B`** (`Dum`: `a` cond, `b` bound or 0, `c` body).

    cur:    [%N = <e>]  ; jmp header             bound evaluated once, before entry
    header: %c = <c> ; br %c chk exit            unsealed: back edge pending
    chk:    %n = <read ctr> ; %t = cmp.lt u64 %n %N ; br %t body trapb     (terminus only)
    trapb:  trap terminus                                                    (terminus only)
    body:   %n1 = addw u64 %n 1 ; <write ctr %n1> ; <B> ; jmp header
    seal(header) at the back edge; exit: seal(exit) after the body

Loop-carried variables: the header's first read of `x` creates an
operandless phi (header unsealed); the body writes `x`; `seal(header)`
runs `addPhiOperands` over the header's edges in order — entry, then the
body's back edge, then every `perge` edge — so the printed phi of the
fixture in section 5 is `phi u64 b0 %a b2 %b`, two operands, predecessor
order. `perge` targets the header; `rumpe` the exit.

*Where the bound is checked, and what happens when it is reached.* Spec
§8.5 says `terminus N` "buys termination, and `certus` rule 6 becomes
syntax" and **says nothing about the runtime event** — no sentence in §8.5,
§5.4 or §13 states what a `dum` does on its `N+1`th entry (section 8,
finding 1). IR 5 H3 already reads it as "a counter plus a `chk`-style
trap"; this document fixes the shape: the counter is a synthetic SSA
variable initialised to `iconst u64 0` before entry and incremented at the
top of the body with `addw` (it cannot wrap: the trap fires at `N`, and
`add` would be a spurious side effect); the check runs **after the
condition is true** — a loop whose condition becomes false exactly at its
`N`th iteration exits normally, and only a body that *would* run an
`N+1`th time traps. `trap terminus` is an IR trap kind (IR 2.3: "an IR
enum, not spec §13 codes"); RT 2.5's abort table has no row for it, and
until it does the emitter's `bfausr_trap` folds every trap into kind 1
(section 8, finding 2). The bound expression is evaluated once, at loop
entry, in the current block: spec §8.6 makes it an `ExprNS`, nothing says
it is re-evaluated, and a bound that could change under the loop would be
a bound on nothing.

**`per x in a..b B`** (`For`: `a` range `Binary` with `AST_OP_RANGE`, `b`
body, `c` `ForHead`, `d` loop-var decl).

    cur:    %a = <a> ; %b = <b> ; <write x %a> ; [redinit … per Contrahe] ; jmp header
    header: %i = <read x>  ; %c = cmp.lt T %i %b ; br %c body exit
    body:   <B> ; jmp latch
    latch:  %i1 = addw T %i 1 ; <write x %i1> ; jmp header
    seal(body) at creation; seal(latch) after the body; seal(header) at
    latch's jmp; seal(exit) after that

The loop variable is an ordinary SSA variable (`AST_D_LOOPVAR` decl id),
written in `cur` and in `latch`, read first in the header — so its phi is
the header's **first phi**, which IR 2.4 requires of a `quisque` header.
`perge` targets `latch`, not `header`, so the increment runs on every path
to the back edge; `rumpe` targets `exit`. The range's ends are evaluated
once. `T` is the range's type (CHK 2.3 gives the literal the partner's
type). The loop variable is never released: an integer.

**`quisque x in a..b B`** lowers to **the same CFG** with
`BFA_BLOCK_FLAG_QUISQUE` on `header`, and nothing else. Spec §8.5 says the
keyword "asserts a property of the body, not authority to spawn anything"
and that "whether the compiler vectorizes, threads, or does nothing is a
lowering decision"; IR 2.3 has no parallel-loop instruction and IR 2.4
gives the header a flag whose consumption is "Stage 3 `[OPEN]`". So the IR
says exactly what the source said — *these iterations are independent* —
and no more. The naive backend (`emit.inc`) does nothing with the flag;
`print.inc` prints it, `parse.inc` reads it, so the bit round-trips to a
backend that wants it. Spec §8.5's property 2 — a false `quisque` should
be a compile error — is dependency analysis this pass does not do and the
checker does not do either; the flag is therefore a **trusted assertion**,
which §8.5 says must be labelled as such rather than quietly downgraded
(section 8, finding 3).

**`contrahe acc: op … forma ordinata | arborea w`** (`ForHead.a/b` the
`Contrahe` list, `c` the `forma` name id, `d` the width; `Contrahe.aux`
the `ArithOp`, `Contrahe.d` the accumulator decl of type `red.F`).

Before `jmp header`, one `%h = redinit F op shape w` per `Contrahe` in list
order, `F` = `types[acc.ty].a`; the handle goes in the loop-stack record
`{acc decl → %h}`, never in `vars`. In the body, **`acc = e;` is the
contribution** (spec §5.4's amended rule): an `Assign` whose lhs `Path.d`
is an `AST_D_ACCUM` decl lowers to `contrib %h <e>`; any other use of
`acc` inside the body is `EXS-E0341`, the checker's, so the loop stack is
the only place the lowering looks it up. At `exit`, after `seal(exit)`, one
`%r = redfin F %h` per accumulator in the same order and
`writeVariable(acc, exit, %r)`: after the loop `acc` is "an ordinary
binding" (spec §5.4) of type `F`, and reads after the loop find `%r`. The
op word: `+` `-` `*` on a float `F` → `fadd` `fsub` `fmul`; on an integer →
`add` `sub` `mul` (trapping), `+%` `-%` → `addw` `subw`, `+|` `-|` → `adds`
`subs` — the IR mnemonic, interned into `Module.names` as `redinit`'s
`aux` (`parse.inc` does the same). Shape from `ForHead.c` (`ordinata` → 0,
`arborea` → 1), width from `ForHead.d`, 0 for `ordinata` (`E0343` is the
checker's). `-` as a reduction operator is admitted by spec §8.6's
`ArithOp` and is not associative under any shape; passed through, and
recorded (section 8, finding 4). `rumpe` inside such a loop is `E0342`,
the checker's; a tree that has one never reaches here.

**`discerne s { casus p1 B1 … [aliter Bn] }`** (`Discerne`: `a` scrutinee,
`b/c` `Casus` list, `d` `aliter` or 0; `Casus`: `a` pattern `Lit` or
`Path`, `b` body).

    cur:    %s = <s>
    test1:  %k1 = <p1> ; %e1 = cmp.eq T %s %k1 ; br %e1 body1 test2   (test1 = cur)
    body1:  <B1> ; jmp join
    …
    testn:  … br %en bodyn aliter — or join when there is no aliter
    join:   seal(join) after the last arm

A compare chain, because IR 2.3 lists `switch` as deliberately absent and
this document does not add an instruction; a `switch` is proposed in
section 8, finding 5, and until it exists the naive backend would lower one
to this chain anyway. A `Lit` pattern is `iconst T konst[p]`; a `Path`
pattern resolves (`Path.d`) to a module-level `firma` whose initializer is a
`Lit`, and lowers to that literal's `iconst` — the only constant a `Path`
can name today (spec §8.6 decision 5: "module `firma` is a constant"; sum
types and constructors are `[OPEN]` in §8.6). `konst[p]` and `T` exist
because the checker types each pattern as the right operand of `s eq p`
against the scrutinee's type (`checker/types/stmt.inc`, `.discerne`); until
it did, a `Lit` pattern reached this pass with `Node.ty` 0 and every
`discerne` trapped under `-o` (`tests/programs/discerne/`). A local `firma`
with a `Lit` initializer is inlined the same way — the same value at every
point of its scope — and a `mutabilis` binding or a parameter traps: it is
not a constant, inlining its initializer answered the value it was declared
with rather than the one it holds, and spec §13 has no code the checker
could refuse it with (`[OPEN]`). A scrutinee of any type but an integer
(`iN`, `uN`, `u1`) or `mensura` needs a runtime comparison this pass has no
callee for — `textus` equality is a prelude routine that does not exist —
and is `[OPEN]` (section 7). With no `aliter` the last false edge goes to
`join`, so a scrutinee that matches no arm **runs nothing**, as `si`
without `aliter` does. This paragraph said the edge was unreachable because
"the checker enforces" spec §8.5's exhaustiveness; it does not, and cannot
until §8.6's enumeration exists and §13 has a code for a missing arm.

**`rumpe;` / `perge;`** (`Rumpe`, `Perge`, no operands). The loop stack
records `{exit, continue target (header for dum, latch for per), scope
depth at loop entry}`. Lowering: release every frame from the current one
down to the loop body's frame inclusive (section 2.6), then `jmp target`
with its edge, then `cur = 0`. Labelled forms are `[OPEN]` in spec §8.6;
the innermost loop is the only target. A `rumpe` outside a loop is
`EXS-E0307`, the checker's.

### 2.6 Scope exit and ARC

`retain`/`release` are IR instructions (IR 2.8), emitted here and nowhere
else. **Decision: every frame keeps its own list of the decls it has
initialised, in walk order, and releases that list in reverse at every
exit.** Not the block's `[Block.c, Block.c + Block.aux)` range: that range
includes nested blocks' sub-ranges (AST 2.4), which were released at their
own exits, and a decl in a dead statement was never initialised. Reverse
declaration order is AST 2.9's choice for spec §6.6's unstated order
(section 8, finding 6).

What needs a release: a binding whose type kind is `ref` or `refc`, held
either as an SSA value (`release <readVariable>`) or in a slot
(`%v = load ref %slot 0 nativus` then `release %v`). Atomicity is the
type's (IR 2.8: "atomicity from `ref`/`refc`"), so one opcode serves both
and the emitter picks `exsrt_release` or `_c` (RT 2.5). An aggregate
binding whose layout contains reference fields (IR 2.7: "a struct holding
references is copied as bytes plus one `retain` per reference field") is
released field by field through `Ast.layout` — designed, no fixture,
`[UNTESTED]`; the hello world's `Scriptor` has none.

Ownership at a binding: `own[init]` (CHK 2.5) says whether the initializer
yields an owned reference (a call result, IR 2.9: "returned references
owned") or a borrowed one (a parameter, another binding). Owned: the
binding takes the reference, no `retain`. Borrowed: `retain` first. An
assignment `x = e` to a reference variable: evaluate `e`, `retain` if
borrowed, `release` the old value, write — that order, so `x = x` is a
retain then a release and not a use after free.

Exits, in the order the releases run:

| exit | frames released |
|---|---|
| fall-through at `}` | the block's own frame |
| `redde [e]` | evaluate `e` first; then every frame, innermost to the function root |
| `rumpe` / `perge` | every frame down to and including the loop body's |
| `trap` | none (IR 2.8) |
| a dead `cur` | nothing is emitted; the frame is popped without releases |

**`redde e` where `e` is a local `ref` binding — naive, with the pair.**
The function's result must be owned (IR 2.9); `e` names a binding that is
about to be released. Lowering: `%v = <e>`; `retain %v`; releases of every
frame, which include `release %v`; `ret %v`. A `retain`/`release` on one
value in one block with no call, store or release between is exactly the
pair IR 2.8 names as the only planned pass's target; leaving it visible is
what makes that pass checkable. Transferring ownership by *skipping* the
release would need the release list to know which of its entries the
return value aliases — a second mechanism for the same result, decided
against. Parameters are never released by the callee (borrowed).

A reference binding declared without an initializer (`firma r: refero<T>;`)
has no value to release on a path that never assigned it; Braun returns
*undef* for the read. Definite assignment is a checker rule with no §13
code (section 8, finding 7); this pass `rassert`s on the undef, which is
constraint 4's rule for a tree the checker should not have passed.

### 2.7 Calls

Argument order in `extra`, IR 2.9 as amended: (1) hidden return `ptr` when
the declared result is non-scalar; (2) dictionaries, one `ptr` per
`GenericParam` of the callee in declaration order; (3) carriers, one `ptr`
per **atom item of the callee type's row** (`types[callee.ty].b` →
`Ast.rows`, items with kind 0, which are sorted ascending on atom decl id
— CHK 2.8's row order; a `sicut` ordinal contributes nothing); (4)
declared parameters in order. The callee's IR signature is built from its
`Decl.ty` by the same four steps (section 2.9), so caller and callee agree
by construction.

**Callee forms.** `Call.a` is a `Path` whose `d` is an `FN`/`EXTERNFN`/
`MEMBER` decl → `call T @name args`; a `Member` whose `d` is a method decl
→ spec §8.6 decision 8: **`x.m(a)` is `m(x, a)`**, the receiver `Member.a`
is the first declared argument, and a receiver that resolves to a type
(`Scriptor.ad_exitum(a)` — `Member.a`'s `Path.d` is a `STRUCT`/`TYPUS`/
`IFACE` decl) contributes no argument. Any other callee expression has a
`fn` type and is a closure `ptr`: `callind T %fp args` with `%fp` as the
hidden first argument, IR 2.9's code-address-first-word rule, and **no
carrier arguments** — a closure's carriers travel inside it (CHK 2.8). A
method call on a `param`-typed receiver dispatches through the dictionary
`[OPEN]` (spec §15 item 5).

**Argument values.** Scalars, `ptr`, `ref` by value. An aggregate argument
is a `ptr` to storage the callee reads and never writes: a `Path` to a
memory-resident binding passes **its slot directly, no copy** (RT 5:
`call u64 @imprime_gutenbergio %2 %3` names `s`'s own slot); any other
aggregate expression is lowered *into* a fresh `slot` (section 2.8) and
the slot is passed. Order of emission for one call: the hidden-return
slot, then arguments left to right, each fully evaluated (including its
own nested calls) before the next begins — which is why RT 5's
`%3 = slot 16 8 ; call void @saluta %3` sits between `%2` and `%4`.

**Carrier lookup.** The provider of atom `P` at a call site is the current
frame stack's innermost non-zero `cap[P]`: a `sub` decl (read as a
variable), a capability-typed parameter (its `param`), or — at the
function root — the function's own hidden carrier `param` for `P`, which
exists iff `P` is an atom item of the function's own row. CHK 2.1 proved
this lookup is a frame walk with no search; the lowering repeats it
rather than asking the checker to record a provider per call node, because
the tree has no slot for one and the walk costs eleven words per frame.

**Names.** `@name` is the callee decl's interned name. A prelude decl
(RT 2.2's pre-seeded table) has an Exsecutor name (`ambitus`, `scribe`)
and an IR symbol (`exsrt_mundus_ambitus`, `exsrt_scriptor_scribe`) that
differ; `interface.inc` must carry the symbol per decl, and the lowering
reads it (section 8, finding 8). User functions: the plain name, ASCII
today; mangling for non-ASCII and for impl members (`Type.m` is not a
`NAME` token) is `[OPEN]` (RT finding 13).

**Lambdas** (`Lambda`: `a` `Sig`, `b` body, `d` decl). Each lambda becomes
its own `Func` named `@lambda$<decl id>` — deterministic, unique, not a
user-spellable name — with signature `(ptr env, [hidden ret],
[dictionaries], params…)` and **no carrier positions**: its row's carriers
are in the environment. The environment is `{ code ptr @0, captures… }`:
**captured bindings by value**, copied at the point the lambda expression
is evaluated, carriers included (a carrier is a `ptr`, CHK 2.8), each
capture at the natural size of its IR type, 8-byte aligned. Reasons: spec
§4.1 rule 4 and §4.2 say a captured *capability* travels in the closure's
type and value, which by-value copying does literally; a `firma` binding
cannot change after capture, so a copy and a reference are
indistinguishable; and by-reference capture would pin the creating frame,
which the naive backend cannot express without a heap. What by-value
cannot express: a captured `mutabilis` binding written after capture, or
written by the lambda — spec §8.6 says nothing, no code exists, `[OPEN]`
(section 8, finding 9; proposal: the checker rejects it). A captured
`ref` is retained at capture and must be released when the closure dies,
which makes the closure an object with a destructor — a heap allocation
under `alloc` that the lambda's row does not declare; `[OPEN]` in the same
finding. The capture list is computed by the lowering: a walk of the
lambda body collecting every `Path.d` whose decl is outside the lambda's
own decl range and inside the enclosing function's, in first-use order.
The closure's storage: **a `slot` in the creating function**, so a lambda
that escapes its frame is a dangling pointer the checker does not detect;
escape analysis or an `alloc` requirement is what closes it, and both are
`[OPEN]`. A *named* function used as a value (`applica(v, nocens)`) needs
a closure whose first word is a code address — and `data` globals are hex
bytes with no relocation, and IR 2.3 has no instruction that yields a
function's address. `[OPEN]`, and the missing `faddr @f` is proposed in
section 8, finding 12. In the first slice every lambda and every
function-as-value is `[UNIMPLEMENTED]` and an `rassert`; the hello world
has neither.

### 2.8 Expressions, literals, `textus`

**Two lowering entry points.** A scalar-typed expression lowers to a value
(`low_expr(node) → %v`); a non-scalar one lowers *into* a destination
(`low_expr_into(node, %dst)`), where `%dst` is a `ptr` the caller supplies
— a binding's slot, a call's hidden-return slot, a fresh temporary, or the
function's own hidden-return `param`. Destination passing is what lets
`redde "…"` in `saluta` write straight through `%0` with no temporary and
no `copy`, which is the IR RT 5 writes down.

| node | lowering |
|---|---|
| `Lit` INT | `iconst T imm`, `T` = `Node.ty`, `imm` = `konst[node]` (64-bit, two `extra` words) |
| `Lit` STRING | one global `data $N size 1 bytes` per literal node, `N` = next global id in walk order (no de-duplication: simplest and deterministic); then, into `%dst`: `%g = gaddr N ; store ptr %dst 0 nativus %g ; %l = iconst u64 size ; store u64 %dst 8 nativus %l` — **in that order**, pointer word then length word, each emitted then stored, because that is the order RT 5 prints and the fixture compares bytes |
| `Path` to an SSA variable | `readVariable(d, cur)`; to a slot: `load T %slot 0 nativus` (scalar) or the slot `ptr` (aggregate); to a module `firma` with a `Lit` initializer: that `iconst`; to a function: section 2.7's `[OPEN]` |
| `Unary` `-` | integer: `sub T %zero %x` with `%zero = iconst T 0` — trapping, and `-MIN` traps, which is spec §5.4's `-`; float: `fneg` |
| `Unary` `&x` | `x` is address-taken, hence a slot: the slot `ptr` (`addr %s 0` when an offset is needed) |
| `Unary` `*p` | `load T %p 0 nativus` (scalar pointee) or `%p` itself (aggregate) |
| `Binary` `+ - *` and the `%`/`\|` forms | `add sub mul` / `addw subw mulw` / `adds subs muls`; float `+ - *` → `fadd fsub fmul`, the wrapping and saturating forms on a float are the checker's `E0305` |
| `Binary` `lt le gt ge eq ne` | `cmp.pred T` / `fcmp.pred F` by operand type; signedness is the type's |
| `Binary` `et` / `vel` | **short-circuit**: `br` into a second block that evaluates the rhs, a join, and a phi over a synthetic variable written in both — spec §8.6 lists them as operators and gives no evaluation rule (section 8, finding 13); short-circuit is what the whole lineage does and what a condition with a call in its right operand needs |
| `Binary` `..` | only as `For.a`; elsewhere the checker's |
| `Cast` int→int | `zext`/`sext` (by *source* sign) when widening, `trunc` when narrowing or changing sign at equal width (spec §5.4 defines both as truncation; the `[OPEN]` this row once cited is closed); int↔float `itof`/`ftoi`; `f32`↔`f64` `fext`/`ftrunc`; same id → nothing; `sicut dyn` → two `ptr`s `[OPEN]` |
| `Index` | `%n = iconst u64 N` (from the `acies` type's width), `chk %i %n`, `%e = index %p %i stride`, then `load`/the `ptr` by element type |
| `Member` field read | `load T %p off order` / `loadbits T %p byte bit` from `Ast.layout[field decl]` — `loadbits` when the width is not a whole number of bytes at a byte boundary (IR 2.7) |
| `Assign` | lhs `Path` → `writeVariable` or `store`; `Member` → `store`/`storebits`; `Index` → `chk`, `index`, `store`; `*p` → `store`; an `ACCUM` lhs → `contrib` (section 2.5); an aggregate rhs → `copy n %dst %src` plus one `retain` per reference field `[UNTESTED]` |
| `Call` | section 2.7 |
| `Try` `?` | `eventus` representation `[OPEN]` (IR 6); `rassert` in the first slice |
| `Lambda` | section 2.7 |

**Bytes emitted for a string literal today.** `Lit.a` interns the source
text *with its quotes and with escapes undecoded* (AST 2.3), because spec
§8.4's literal grammar is `[OPEN]`. The lowering strips the first and
last byte and emits the rest verbatim. Consequences, stated so nobody
reads them as decisions: a raw newline in the literal is one `0A` byte, as
spec §8.4 says it must be; a backslash sequence is emitted as its source
bytes (`\n` is `5C 6E`), which is *not* what any future escape grammar
will mean, so a literal containing `\` produces bytes that will change
when §8.4 closes — no fixture may depend on one until then, and §8.4's
own sentence "escapes are the only legal way to produce a bidi or
invisible control codepoint" is unimplementable until the grammar exists
(section 8, finding 14). The bytes are NFC because the source is (§8.1).
RT 5's 101-byte literal contains raw newlines and no backslash, so its
bytes are stable under any resolution.

### 2.9 Functions and the module

For each `Fn` item in `Module.items` order (and each `Impl` member, in
member order, after the item that owns it): `bfa_func_new`; name; the IR
signature from `Decl.ty` (a `fn` type: params at `a`, count at `width`,
result after the params, row at `b`) by section 2.7's four steps — hidden
return `ptr` iff the result is non-scalar, `ptr` per generic parameter,
`ptr` per row atom, then the declared parameter types mapped by the table
of section 2.2 (aggregates as `ptr`); `bfa_sig_new` — which does not exist
as a proc, though `ir.inc`'s comment names it (section 5); `numeri`; `b0`;
one `param` per position; then the body block. `initium` under spec §4.7
declares one parameter `m: Mundus` of type `cap` and has an empty row, so
its signature is `(ptr) -> u8` by step 4, not step 3 — the same bytes RT 5
prints, though not the comment RT 5 attaches (section 8, finding 15).

A function whose body's final block is still live at `}` gets the frame
releases and `ret` (unit result) — a non-unit function whose end is
reachable is the checker's (`E0307`). `Externus` functions produce a
`Func` with `BFA_FUNC_ATTR_EXTERNUS` and the `abi` id; IR 2.11's grammar
requires `Block+`, so an `externus` function has no textual form — `[OPEN]`
(section 8, finding 16). `numeri`: `Ast.numeri` is four bytes "the
driver's to assign" (kinds.inc) with no word table; this pass prints spec
§5.4's defaults (`ad_parem vetita explicita conservata`) for an all-zero
`numeri` and treats any other value as `[OPEN]` until a word table exists
in one place (section 8, finding 17). Module-level `firma` bindings with a
`Lit` initializer are inlined at use (section 2.8); with any other
initializer `[OPEN]`. `mutabilis` at module level is `E0500`. Globals are
emitted in the order their literals are walked, functions in item order,
which is the print order (`File ::= (Global | Function)*`; `print.inc`
writes globals first).

### 2.10 Determinism

| this order | is a function of |
|---|---|
| value ids (pre-compaction) | walk order: items, statements, operands left to right; a late phi is created at the read that needs it |
| value ids (final) | the compaction renumbering, a function of the above |
| block ids | creation order in the walk; joins and exits on their first edge |
| edge order = phi operand order | the order terminators were emitted (IR 2.4) — **which is not block-index order** in general: a `si` whose first arm nests a construct emits that arm's `jmp join` before the second test's false edge, so `join`'s edges are `(inner block, test2)` while an index scan gives `(test2, inner block)`. `verify.inc` rule 3 builds its predecessor lists by index scan and will disagree with phis this pass emits; section 8, finding 11 |
| global ids | literal walk order |
| function ids | item order |
| synthetic var ids | `decls.len + k`, `k` per function in walk order |
| `currentDef` | looked up, never iterated |
| trivial-phi sweep | index order to a fixpoint (IR 4) |

The naive backend assumes nothing about *which* order the ids are in, only
that they are dense and 1-based (`emit.inc`: "one 8-byte stack slot per
internal value id, dense, 1-based, including void instructions"); after
compaction they are, and the frame size is the final instruction count.
The printer renumbers in print order (IR 2.11), so `%N` in the text is
block index, then phis, then body — also a function of the walk. No order
above names a pointer, a hash bucket, or a map iteration.

### 2.11 The verifier boundary

IR section 3's verifier runs on every function before any backend
(`bfa_verify_module`, `rassert` on the first verdict). What this
construction guarantees **by shape**, so a verdict there is a lowering
bug:

- rule 1 (operand types): every instruction is emitted with the type
  `Node.ty` says, and the checker has already rejected mismatches;
- rule 2 (one terminator, last): a block is terminated exactly once and
  `cur` goes dead; the only way to add to a terminated block is a bug;
- rule 3 (phis first, one operand per predecessor, in edge order):
  phis are prepended to a separate list; `addPhiOperands` walks the edge
  list and writes one pair per edge — modulo finding 11's index-scan
  disagreement in the verifier itself;
- rule 4 (dominance): Braun's construction defines every value before its
  use on every path or reads it through a phi at the join;
- rules 6, 7 (`retain`/`release` only on references; `red.F` only in its
  three ops): the type table drives both emissions;
- rule 8 (`numeri` equal across calls): one module, one `numeri`;
- rule 9 (no `nop`): compaction.

What it **cannot** guarantee: rule 5's second half — every `redfin`
post-dominating every `contrib` of its handle — across a `rumpe` out of a
reduction loop (IR 3 `[OPEN]`). The checker's `E0342` forbids that
program; if the checker's rule and the verifier's ever disagree, the
verifier is the one that fires, on a compiler bug, which is the right
order.

## 3. The hello world, produced

The claim of RT 5: *the lowering produces this*. Here is the walk that
does, tree by tree; the fixture of section 5 compares its `bfa_print`
output to RT 5's text byte for byte, module order `saluta`,
`imprime_gutenbergio`, `initium`, which is the order RT 5 prints and — for
one module built from three files — spec §10.1's `fontes` order, `[OPEN]`
in AST H4.

**`saluta`** — `Fn{Sig{[], result TyPath textus}, Block{[Redde{Lit STRING}]}}`,
`Decl.ty` = `fn () -> textus`, row empty. Signature: result `textus` is
non-scalar → hidden `ptr`; no generics, no atoms, no params: `(ptr) -> void`.
`b0`: `%0 = param ptr 0`. `Redde.a` is a `Lit` of non-scalar type, so
`low_expr_into(lit, %0)`: global `$1` from the 101 bytes between the
quotes; `%1 = gaddr 1`; `store ptr %0 0 nativus %1`; `%2 = iconst u64 101`;
`store u64 %0 8 nativus %2`. Scope exit: the block initialised nothing.
`ret`. Six instructions, one block, one global — RT 5's function exactly.

**`imprime_gutenbergio`** — params `s: Scriptor` (struct, 16 bytes → `ptr`),
`t: textus` (→ `ptr`), result `mensura` (→ `u64`), declared row
`poscit sicut s`: its type row holds one ordinal item and no atom, so step
3 adds nothing: `(ptr ptr) -> u64`. `%0 = param ptr 0`, `%1 = param ptr 1`.
`Redde{Call{Member{Path s, scribe}, [Path t]}}`: the callee is a method
decl with a value receiver, so `scribe(s, t)`; `scribe`'s own row is empty
(RT 2.2's table), result `u64` scalar: `%2 = call u64 @exsrt_scriptor_scribe %0 %1`
— the two aggregate arguments are the parameters' own `ptr`s, no copy.
`ret %2`.

**`initium`** — `m: Mundus` (`cap` → `ptr`), result `u8`, empty row:
`(ptr) -> u8`; `%0 = param ptr 0`. Statement 1, `firma a = m.ambitus();`:
`a` is `cap`-typed, scalar, SSA; the initializer is a method call with
receiver `m`, callee the prelude decl whose symbol is
`exsrt_mundus_ambitus`, result `cap` → `ptr`:
`%1 = call ptr @exsrt_mundus_ambitus %0`; `writeVariable(a, b0, %1)`.
Statement 2, `sub ambitus = a;`: `writeVariable(sub, b0, %1)`, frame
`cap[ambitus] = sub` — no instruction. Statement 3,
`firma s = Scriptor.ad_exitum(a);`: `s` is a struct, memory-resident:
`%2 = slot 16 8`; the initializer is a static member call (receiver is a
type) whose result is the struct, so `s`'s slot is its hidden return:
`call void @exsrt_scriptor_ad_exitum %2 %1`. Statement 4,
`imprime_gutenbergio(s, saluta());`: result `u64`, no hidden slot; its
type row is `{sicut 0}` — no atom, no carrier; argument 1 is a `Path` to
a memory-resident binding → `%2`; argument 2 is a call returning an
aggregate → `%3 = slot 16 8`, `call void @saluta %3`; then
`%4 = call u64 @imprime_gutenbergio %2 %3`, result discarded. Statement 5,
`redde 0;`: `%5 = iconst u8 0` (the literal takes the result type,
CHK 2.3); scope exit releases nothing (no references); `ret %5`. Ten
instructions, one block — RT 5's `initium`.

Every function prints with `numeri ad_parem vetita explicita conservata`
(section 2.9's default rule). The `data $1 101 1 …` line precedes the
functions (print order). The one place the design *had* to bend to the
text: the pointer-word-then-length-word emission order of section 2.8,
chosen so that `%1`/`%2` come out as RT 5 wrote them rather than with the
two constants first.

## 4. Sizing on the no-grow arena

Per function, from the AST: `N` nodes in the function's subtree, `L` loops,
`S` `si`/`discerne` arms, `D` decls in its range, `C` calls, `R`
reference-typed decls.

| structure | bound | note |
|---|---|---|
| `insts` | ≤ `4N + 6L + 2S + 2R + params` | one to four instructions per expression node (an `Index` is four), loop scaffolding, one `cmp`+`br` per arm, retain/release, `param`s |
| `blocks` | ≤ `1 + 4L + 2S + (et/vel count) × 2` | header, body, latch/chk, exit; test and body per arm |
| `edges` | ≤ `2 × blocks + rumpe/perge count` | `br` emits two |
| `extra` | ≤ `2 × Σ phi operands + Σ call args + 2 × iconst + 3–4 × memory ops` | |
| `defs` map | entries = writes + phis created ≤ `D_ssa × blocks` worst case, `D_ssa + 2L` typical; keys 8 B each in scratch | bucket count: the power of two ≥ 2 × the bound, computed per function from `D_ssa × blocks`, never a constant (CHK 5's lesson) |
| `incomplete` | ≤ `D_ssa × (unsealed blocks at once)` ≤ `D_ssa × (2L + 1)` | |
| `vars` | `D + synth` × 12 B | |
| scopes / loops | lexical depth × frame (≈ 60 B with `cap[11]`) | |

`vec_push` doubles into a fresh block without freeing the old, so every
vec above consumes about twice its final size until the reset (kinds.inc's
own arithmetic for the AST). The `rassert` is `arena_alloc`'s, in `rt/`,
reached through `vec_push`/`map_insert`; the lowering adds one of its own
at function start: the scratch arena's capacity must exceed the sum of the
bounds above, computed from `N`, `D`, `L`, or the function is refused
before any IR is emitted — a bound violated by construction is a bug in
this table, and it should fail at the top of the function, not halfway
through a block. First numbers for a 10 000-line module (CHK 5's
≈ 50 k nodes, 8 k decls): ≈ 200 k instructions × 32 B ≈ 6.4 MiB of IR
doubled ≈ 13 MiB; scratch per function well under 1 MiB except for a
pathological single function. **All `[OPEN]`**: no tree has been lowered,
and every figure is arithmetic on CHK 5's estimates, which are themselves
unmeasured.

## 5. Agent split

Two agents, exclusive subdirectories under `compiler/x86_64/lower/`, an
include chain like `ast/`'s, aggregated by `lower/lower.inc` which the
second agent owns. The split is where Braun's procedures stop knowing what
a tree is: `ssa.inc` takes `(var id, block id)` and the builders and never
reads an `AstNode`, so it is testable by a scripted sequence of writes,
reads and seals with no AST at all — and that fixture is the one that pins
the phi's operand order. Prefix `low_` / `__low_`, struct prefix `Low`;
neither appears in `docs/asm-conventions.md` §4.1's burned list (`lo` does,
as an argument name, and is avoided).

| agent | owns | depends on | non-vacuous fixture |
|---|---|---|---|
| `lower-ssa` | `lower/ssa.inc`: `LowState`'s `defs`/`incomplete`, the six procedures of section 2.3, the sweep, canonicalisation, compaction; `lower/cfg.inc`: block creation, the two terminator helpers, sealing | `backend_fasmg/ir.inc` builders (existing plus the additions below); `rt/map.inc` | `tests/unit/low_ssa_loop.asm`: a hand-driven script — `b0: write x %a; jmp b1; b1 (unsealed): read x → phi; cmp; br b2 b3; b2: write x %b; jmp b1; seal b1; seal b3` — printed through `bfa_print`, and the header's phi line must be exactly `phi u64 b0 %a b2 %b` (two operands, predecessor order); a second script with a variable unchanged across two nested loops, asserting after the sweep that no phi remains and reporting how many the sweep (not the local check) removed — the count is the `[UNTESTED]` claim of section 2.3 made measurable |
| `lower` | `lower/lower.inc` (module, function, signature), `stmt.inc` (section 2.5, 2.6), `expr.inc` (2.7, 2.8), `scope.inc` (frames, `cap[11]`, release lists) | `lower-ssa`; `ast/` accessors and `ast/load.inc`; the checker's tables as CHK 2.5 lists them; `prelude/interface.inc`'s symbol column (finding 8) | `tests/unit/low_saluta.asm`: the three typed trees of section 3 hand-written in `ast/dump.inc`'s format — its `t`, `d`, `n` sections carry `Ast.types`, `Decl.ty` and `Node.ty`, so the dump is typed; `konst` on the two `Lit`s and `own` are set after `ast_side_alloc` by the fixture, since the dump format has no section for side tables — loaded, lowered, verified by `bfa_verify_module`, printed by `bfa_print`, compared byte for byte to RT 5's text embedded in the fixture; exit 0 only on equality |

Order: `lower-ssa` first (its fixture needs no tree), `lower` second.
Neither runs from the driver until `driver/run.inc` calls it after the
checker (the driver agent's tree), and no conformance entry depends on it
before then.

**Builders the backend agent must add to `backend_fasmg/ir.inc`** (this
pass's tree cannot write there): `bfa_edge_push(func, from, to)` — as built in ir.inc; an earlier draft here wrote `(func, block, pred)`, and a caller using that order builds a reversed CFG that still verifies on a symmetric fixture —
append to `block`'s predecessor list (finding 1 of `verify.inc`, now with
a caller); `bfa_block_prepend_phi(func, block, valueid)` — IR 2.1's O(1)
prepend, which `bfa_block_link_phi` does not do (it appends); `bfa_sig_new(module,
ret_ty, ptypes_ptr, count)` — named in `ir.inc`'s `BFA_TK_FN` comment,
absent as a proc; `bfa_global_new(module, size, align, bytes_ptr, len) →
id`; `BFA_OP_NOP = 0` as a named constant; `bfa_func_set_numeri` (or the
four interned ids set by the caller, documented either way); and an
exported operand iterator — `bfa_inst_operand_slots(func, id) → (inline
mask, extra start, extra count)` from the `BFA_SH_*` shape — so that
canonicalisation and compaction do not copy `print.inc`'s shape table.
The parser owns none of this today: `parse.inc` builds sigs and globals by
pushing into `BfaModule.sigs`/`globals`/`gdata` inline, which a second
writer should not repeat.

## 6. Hazards

- **H1 the edge-order disagreement** (section 2.10, finding 11) is not a
  corner case: the first `si … sin` with a nested construct hits it, and
  the verifier will reject correct phis until it reads the edge list. Land
  the verifier change before the first `lower` fixture with two arms.
- **H2 `map_insert` appends.** A variable written in a loop body, in a
  block, k times, is k entries; a walk that re-reads `defs` expecting one
  entry per key gets the newest by chain order, which is correct, but the
  sizing (section 4) must count writes, not variables.
- **H3 undef.** A read that reaches a phi with no operands but itself is a
  definite-assignment failure the checker has no code for (finding 7);
  `rassert` is the honest response, and a fixture that exercises it must
  be marked `status=blocked needs=spec13`, never counted as passing.
- **H4 the `contrib` dominance rule** (section 2.11) is checked by the
  verifier, not guaranteed here; a checker regression on `E0342` shows up
  as a verifier `rassert`, which names no source span. Keep `E0342`'s
  reject/accept pair in the checker's fixtures.
- **H5 destination passing and aliasing.** `s = f(s)` where `s` is an
  aggregate and `f` returns one: the hidden return slot is `s`'s own slot
  while `f` reads `s` through the argument `ptr` — the callee writes its
  result into storage it is reading. Naive fix: a call whose hidden
  destination is also one of its arguments gets a fresh temporary and a
  `copy`. Designed, unfixtured, `[UNTESTED]`.

## 7. Unresolved

The runtime event at a reached `terminus` and its abort kind; `quisque` as
a trusted assertion; `-` under `contrahe`; `switch`; `Path` patterns beyond
module `firma` constants and non-integer scrutinees; module `firma` with a
non-literal initializer; `et`/`vel` evaluation order in the spec; lambdas
altogether (capture of `mutabilis`, captured references, escape,
functions as values, `faddr`); dictionaries' contents (spec §15 item 5)
and dispatch on `param` receivers; `Try`/`eventus`; `dyn` values; element
stride of sub-byte scalars in `acies`; `externus` in the textual IR;
`numeri` words; definite assignment; name mangling; the `fontes` order;
release of aggregates with reference fields; H5's aliasing; every figure
in section 4.

## 8. Findings against the spec and the other designs

Numbered; each names the section and the sentence. Not edited here.

1. **Spec §8.5, the table row "`terminus N` — termination, and `certus`
   rule 6 becomes syntax."** No sentence in §8.5, §5.4 or §13 says what
   happens when a `dum` whose condition is still true has run `N`
   iterations. The brief for this document assumed §8.5 says; it does not.
   Section 2.5 decides *trap* (IR 5 H3's reading) with the check after the
   condition; the spec should state the event, and whether the bound is
   evaluated once (decided here) or per iteration.
2. **RT 2.5's abort-kind table** has kinds 1–4 and none for a `terminus`
   overrun; `trap terminus` folds into kind 1 until a row exists. The
   prelude agent owns the table; this document requests a fifth kind.
3. **Spec §8.5, property 2: "if the body carries a cross-iteration
   dependency … the compiler says so."** Neither the checker design nor
   this one does dependency analysis, so today `quisque` *is* the
   "trusted assertion" §8.5 says "must be labelled as such rather than
   quietly downgraded". The spec should carry the label until an analysis
   exists.
4. **Spec §8.6 `ArithOp ::= '+' | '+%' | '+|' | '-' | '-%' | '-|' | '*'`**
   admits `-` as a `contrahe` operator; spec §5.4's "the reduction tree
   shape is part of the operation" presupposes an associative operator,
   and subtraction under `arborea` has no meaning the section defines.
   Either `Contrahe` excludes `-` (a checker rule under `E0343`) or §5.4
   defines it.
5. **IR 2.3, "deliberately absent … `switch`."** A `discerne` over an
   integer with many arms is a compare chain of length *n* here; a
   `switch T %s (imm bN)+ bDefault` instruction is proposed for IR 2.3's
   table, lowered by the naive backend to the same chain and by any other
   to a table. Not added here.
6. **Spec §6.6 "RAII everywhere"** still states no destruction order at
   scope exit; AST 2.9 chose reverse declaration order and section 2.6
   narrows it to reverse *initialisation* order within a frame, which
   coincide except for dead declarations. AST 7 item 4 remains owed.
7. **Spec §13 has no code for definite assignment.** A scalar or reference
   binding read on a path that never assigned it (`firma r: refero<T>;`
   then `redde r;`) is Braun's *undef*; CHK 2.3 calls "neither annotation
   nor initializer" an error and says nothing about *annotation without
   initializer then use*. Section 2.6 `rassert`s; a class-C code is
   needed, not chosen here.
8. **RT 2.2's prelude interface** gives each pre-seeded decl an Exsecutor
   name and a prelude symbol, and `AstDecl` has one `name`. The lowering
   needs the symbol per decl (`ambitus` → `exsrt_mundus_ambitus`); the
   `interface.inc` table must carry it, and the prelude agent owns that
   column.
9. **Spec §4.1 rule 4 / §4.2 say what a captured *capability* is and
   nothing about a captured *binding*.** Section 2.7 decides by-value
   capture and leaves a captured `mutabilis`, a captured reference's
   release, and an escaping closure `[OPEN]`; the spec should say whether
   a lambda may capture a `mutabilis` at all (proposal: no, a checker rule
   with a class-C code).
10. **IR 2.1 "a removed instruction becomes `nop`"** with no `BFA_OP_NOP`
    in `ir.inc` — `verify.inc` finding 2, seconded: this pass uses 0 and
    needs the constant named.
11. **IR 2.4 "predecessors are an append-only edge list in the order
    their terminators were emitted" versus `verify.inc` rule 3**, which
    builds predecessor lists by scanning blocks in index order and
    terminator operands in order. The two orders differ on the first
    `si … sin` whose first arm contains a nested construct (section 2.10).
    IR 2.4 is the contract; the verifier must read `BfaBlock.first_pred`
    once something populates it (`bfa_edge_push`, section 5). And rule 9's
    whole-vec scan is why section 2.3 compacts; if the verifier walked the
    block lists instead, compaction would be optional.
12. **IR 2.9 "a function value is a `ptr` to a closure whose first word is
    the code address"** — no IR instruction yields a code address and
    `data` carries no relocation, so a named function cannot become a
    value. Proposed: `faddr @f → ptr` in IR 2.3, and a thunk convention
    for a named function's carriers (section 2.7).
13. **Spec §8.6's precedence table lists `et` and `vel`** and no sentence
    gives their evaluation rule. Section 2.8 decides short-circuit; the
    spec should say so, because a call in the right operand makes the
    difference observable.
14. **Spec §8.4 "escapes are the only legal way to produce a bidi or
    invisible control codepoint"** and, six lines later, "`[OPEN]` numeric
    literal grammar" — the escape grammar is equally open (AST 2.3
    "escapes undecoded"), so the first sentence names a mechanism that
    does not exist. Section 2.8 emits backslashes verbatim; the spec
    should mark escapes `[OPEN]` explicitly.
15. **RT 5's comment on `initium`, "`m: Mundus` — the one carrier (IR 2.9
    group 3; no declared params)."** Spec §4.7 says `initium`'s root is
    "received as a parameter — rule 4's second path", and CHK 2.5 gives
    `PARAM` decls of atom type; `m` is a declared parameter of type `cap`
    (group 4), and `initium`'s row is empty (group 3 is empty). Same
    bytes, wrong group; the comment should say so before someone
    generalises it to a function with both.
16. **IR 2.11 `Function ::= … '{' Block+ '}'`** requires a body, so an
    `externus` function (`BFA_FUNC_ATTR_EXTERNUS` exists in `ir.inc`) has
    no textual form and cannot round-trip. The grammar needs a bodiless
    alternative.
17. **kinds.inc: "the VALUES on each axis are the driver's to assign"** —
    no table maps an `Ast.numeri` byte to a spec §5.4 word, and the IR
    header needs the word. One table, in one owner (the driver, which
    parses the `ego`), with 0 = the spec default; until it exists this
    pass prints the defaults and refuses anything else.
18. **AST 2.12's row "`Dum` with `terminus` — counter + trap (IR 5, H3)"**
    and IR 5 H3 both cite spec §8.5 for a runtime trap the spec does not
    state (finding 1). The two designs agreed with each other and not
    with the text; recorded so the next reader checks the spec, not the
    designs.
19. **`ast/dump.inc`'s format** carries `Node.ty`, `Decl.ty` and the type
    table but no `konst`, `own`, `rows` or `layout` section, so a typed
    fixture cannot be written entirely as text; section 5's fixture sets
    the side tables in assembly. A `k`/`o` section is a request to the
    ast agent, not a blocker.
