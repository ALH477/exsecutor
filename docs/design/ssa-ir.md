# SSA IR — design plan

Status: `[OPEN]`. Design only; no IR exists and nothing below has run.
`spec §N` cites `docs/spec/exsecutor-spec-v0.4.md`. Taken as given: spec §9.1's
pipeline and construction method (Braun et al., CC 2013), spec §5.4/§5.5,
spec §6, spec §9.2's two backends, `docs/design/phrase-grammar.md`. This is
the contract both backends are written against: each must be buildable and
testable from the text of section 2.11 with no frontend and without the
other. One recommendation per question, with its reason, for the owner to
accept or reject. Nothing here is written into `docs/spec/`.

## 1. The constraints and what they force

1. **Freestanding x86-64 assembly** over `rt/{arena,vec,map,intern,span,str}`
   and nothing else. So the IR is indices into vecs — no node pointers, no
   per-instruction operand vectors, one append-only pool for anything
   variable-length (section 2.1). Nothing needs a structure `rt/` lacks.
2. **spec §9.3.** Every id is a creation index, every order an index order
   (section 4). The one hash table is looked up, never walked.
3. **Braun et al., on the fly.** No dominance or CFG analysis before or during
   construction: blocks seal late, phis fill late (section 2.5). The verifier
   computes dominance afterwards; the builder never.
4. **spec §5.4 survives.** `+`, `+%`, `+|` are three opcodes, never a flag;
   `fma` is the only fused float op; reduction shape is an operand of a
   dedicated instruction whose handle type nothing else can read (section 2.6).
5. **Spans** (spec §8.3): `rt/span.inc`'s 12-byte `Span` inline in every
   32-byte instruction. **ARC is in the IR** (section 2.8). **Layout is
   resolved before the IR** (section 2.7), so no backend chooses one — ADR
   0012's fourth problem. **Rows never reach the IR** (section 2.10).

## 2. Decisions

### 2.1 Representation: three vecs and a pool per function

    Inst  { op u16, aux u16, ty u32, a u32, b u32, next u32, span Span }   32 bytes
    Block { first, last, first_phi, last_phi, first_pred, last_pred, flags: u32; span Span }
    Edge  { pred u32, next u32 }
    Func  { insts Vec<Inst>, blocks Vec<Block>, edges Vec<Edge>, extra Vec<u32>,
            name u32 (intern id), sig u32, numeri 4 x u8, attrs u32, span Span }
    Module{ funcs Vec<Func>, types Vec<TypeNode>, sigs Vec<Sig>, globals Vec<Global>,
            data Vec<u8>, names Interner }

A value *is* an instruction index, 1-based so 0 is `none` (`rt/map.inc`
returns 0 on a miss, so maps store value ids unshifted). `a`/`b` are the
inline operands; more than two (`phi`, `call`, 64-bit immediates) sets `a` to
an `extra` index and `b` to a count. A block's instructions form a singly
linked list through `next` (phis a second list), so a late phi is prepended
in O(1) and nothing moves. A removed instruction becomes `nop` with `a` = its
replacement; reads follow the chain. `Vec` growth moves its backing block, so
the builder holds indices across a push, never pointers. A module's IR lives
in one arena; builder scratch in a second, reset per function (`rt/arena.inc`:
never reset an arena backing live data).

### 2.2 Types

`TypeNode { kind u8, sign u8, width u16, ref u32 }`, interned through a map on
those 8 bytes; a type id is a first-use index.

| type | meaning |
|---|---|
| `uN` `iN`, 1 ≤ N ≤ 64 | exact width (spec §5.4); `u1` is the boolean |
| `f32` `f64` | IEEE binary32/64 |
| `ptr` | host address, width of `mensura` — concrete, spec §9.5 fixes `--hospes` first |
| `dptr` | device address (spec §5.5); reserved, Stage 3 `[OPEN]` |
| `ref` `refc` | `refero<T>` / `refero_communis<T>` (spec §6.4): non-atomic / atomic |
| `red.F` | reduction handle over `F` (section 2.6): cannot be loaded, stored, compared or passed |
| `fn.S` | function value of signature `S` |

Signedness is in the type: no implicit promotion exists, so a verifier
rejecting `u8` against `i8` needs no flag. Rule: what the *source* puts in the
operator (overflow) is a distinct opcode; what it puts in the type (sign,
atomicity, placement) the type carries. No aggregates (section 2.7), no
size-polymorphic instruction: generic code (spec §7.1) sees a type parameter
as `ptr` plus a dictionary and copies or destroys through it `[OPEN]`.

### 2.3 Instruction set

`T` an integer type, `F` a float type; operands share the named type; result
is that type unless shown. Text form: `%n = op T operands`.

| group | instructions | semantics |
|---|---|---|
| integer, trapping | `add sub mul div rem` | spec §5.4 `+`; overflow or zero divisor traps; **side-effecting**: never removed if unused, never reordered across another side effect |
| integer, wrapping | `addw subw mulw` | `+%`, modulo 2^N |
| integer, saturating | `adds subs muls` | `+\|`, clamped to `T` |
| overflow predicate | `addov subov mulov → u1` | true iff the trapping form would trap; `+?` is `addov` + `addw` |
| bitwise | `and or xor shl shr` | `shr` arithmetic for `iN`; count ≥ N `[OPEN]` |
| compare | `cmp.eq .ne .lt .le .gt .ge → u1` | signedness from `T` |
| convert | `zext sext trunc` | `zext`/`sext` by source sign; `trunc` keeps low bits (narrowing `sicut` `[OPEN]`) |
| float | `fadd fsub fmul fdiv fneg` | one IEEE rounding each, in the function's `rotundatio`, subnormals per `subnormales` |
| fused | `fma` | one rounding; the only contraction that can exist (`contractio explicita`) |
| float misc | `fcmp.* → u1`, `fext ftrunc itof ftoi`, `bitcast` | `fcmp` ordered; unordered forms, `ftoi` on NaN/range `[OPEN]` |
| reduction | `redinit F op shape w → red.F`, `contrib %h %v`, `redfin F %h → F` | section 2.6 |
| memory | `slot n align → ptr`, `load T %p off bo`, `store T %p off bo %v`, `loadbits T %p byte bit`, `storebits T %p byte bit %v`, `copy n %d %s`, `addr %p off`, `index %p %i stride`, `gaddr N`, `chk %i %n` | section 2.7; `chk` traps on `%i ≥ %n` |
| ARC | `retain %r`, `release %r` | section 2.8; atomicity from `ref`/`refc` |
| calls | `call T @f args…`, `callind T %fp args…`, `ret [%v]` | section 2.9 |
| control | `jmp bN`, `br %c bT bF`, `trap kind`, `phi T bP %v …` | section 2.4; `trap` kinds are an IR enum, **not** spec §13 codes (those are compiler diagnostics) |
| leaves | `param T i`, `iconst T imm`, `fconst F bits` | `fconst` takes the raw bit pattern, never decimal text |

Deliberately absent, each `[OPEN]` and added only by amending this table:
`select`, `switch`, a loop construct, aggregate values, an allocation op
(allocation is a `call` into the runtime through the `alloc` carrier).

### 2.4 Blocks, control flow, phi instructions rather than block parameters

A block is a phi list, a body, and exactly one terminator (`jmp`, `br`,
`ret`, `trap`). Predecessors are an append-only edge list in the order their
terminators were emitted; phi operands follow that order, one per predecessor,
and may name values defined later in the text (back edges).

Phis, not block parameters: Braun's `addPhiOperands` runs when a block is
sealed, after its predecessors' terminators exist. Block parameters would
mean extending each terminator's argument list in place; a phi's operands are
one `extra` range written once, when the predecessor set is final. Backends
lower either form to edge copies; the parallel-copy case (a phi reading a phi
of the same block) is each backend's to get right, and ADR 0012's differential
test checks that it did. The source has no `goto` (spec §8.5), so every CFG is
reducible and Braun's irreducible-graph step is not needed. A `quisque` header
carries a flag and its induction phi is its first phi — the independence bit
spec §8.5 says everything downstream consumes; its use is Stage 3 `[OPEN]`.

### 2.5 Construction on `rt/`: Braun's structures, mapped

| Braun et al. | here |
|---|---|
| `currentDef[var][block]` | one map, key = 8 bytes (var id, block id) → value id; looked up, never iterated |
| `sealedBlocks` | a `Block.flags` bit |
| `incompletePhis[block]` | scratch (block, var, phi) records threaded by `next` |
| variable | a scalar-typed binding the checker did not mark memory-resident; var id = binding order |
| unsealed read | create an operandless phi, prepend to the phi list, record it incomplete |
| `addPhiOperands` | collect into a scratch vec, then append once to `extra` |
| `tryRemoveTrivialPhi` | mark `nop`, `a` = survivor; all reads follow the chain |
| recursion into users | **replaced**: no use lists; when the last block is sealed, sweep phis in index order to a fixpoint. Deterministic; `[UNTESTED]` that it is minimal wherever Braun's recursion is |

A binding is memory-resident (a `slot` read by `load`) if its type is not a
scalar or the checker flagged it address-taken when it resolved `&x` — a fact
of the typed AST, not a pre-pass of the builder. Blocks seal exactly when the
walk knows their predecessor set is complete: a join after both arms, a loop
header after the back edge, a loop exit after the whole body. A final
canonicalisation resolves every operand's chain, so no backend sees a `nop`.

### 2.6 Numeric semantics in the IR

- **Overflow**: three opcodes per operation (section 2.3). A backend with no
  lowering for `adds` emits nothing; there is no flag to lose.
- **`numeri`** (spec §5.4, §10.1) is copied into every `Func` and printed in
  every header; a backend reads the function, never the ego. A `call` between
  functions whose `numeri` differ is rejected unless it is the explicit
  coercion spec §5.4 requires, which lowers to a runtime `call` that switches
  and restores the environment `[OPEN]`.
- **Reduction shape**: `redinit f32 fadd arborea 8` opens a reduction,
  `contrib %h %v` delivers one contribution, `redfin f32 %h` closes it.
  `red.F` admits no other instruction, so nothing can read a running total:
  `ordinata` cannot be flattened into `arborea` or back, because a flattened
  form would have to read state the type system does not expose.
  `summa_ordinata`/`summa_arborea` and `contrahe … forma …` (spec §5.4, §8.5)
  both lower to these three. The reference backend's lowering of `arborea w`
  is the definition (ADR 0012); spec §5.4 leaves `n` not a multiple of `w`
  undefined `[OPEN]`.
- **Vectors**: `acies<F, N>` is `N` lanes in memory; lane-wise ops are scalar
  loops here, a vector group is Stage 3 `[OPEN]`. Lane count cannot come from
  the host because the IR has no host-width type.

### 2.7 Memory, layout, `@transitus`

Layout is resolved in Stage 2, before the IR. Reason: `EXS-E0321`/`EXS-E0322`
are diagnostics with spans against a declaration, the checker's business;
spec §9.5 has fixed `--hospes`, so `mensura` and every offset are concrete;
and a backend handed an unresolved struct would choose a layout, the
implementation-defined behaviour ADR 0012 forbids. So the IR has no struct
types. A `structura` is a `slot` or heap bytes behind a `ptr`, and a field is
`(byte, bit, width, byte order)` on the access:

    %v = load u32 %p 8 maior        ; u32:maior at byte 8
    %g = loadbits u4 %p 1 4         ; u4 at byte 1, bits 4..7, MSB-first (spec §5.2 rule 1)
    storebits u4 %p 1 0 %g

`load`/`store` take whole-byte widths at a byte boundary with order `nativus`,
`maior` or `minor`; `loadbits`/`storebits` any width ≤ 8 at any bit offset,
straddling a byte if the declaration did. Spec §5.2 admits `u12:maior` (above
8, byte-aligned, not a multiple of 8) but does not say how it packs under
`:minor` or how its trailing bits share a byte with the next field — a gap
the IR cannot paper over; the verifier rejects the case until it closes
`[OPEN]`. `copy n` is bytes only; a struct holding references is copied as
bytes plus one `retain` per reference field known from the layout. Bounds are
explicit `chk` instructions, so both backends trap at the same point.

### 2.8 ARC: `retain`/`release` are instructions

Emitted by the AST-to-IR lowering at every scope exit on every path (`redde`,
`rumpe`, `perge`, fall-through); `trap` aborts and releases nothing. Reasons,
in order: spec §6.6 makes destruction deterministic and observable, so the
*point* of each release is semantics and must be fixed once, in the IR, for
two backends to agree; the only planned pass — deleting a `retain`/`release`
pair on one value in one block with no call, store or release between — needs
them visible; and spec §6.3's borrowed-parameter rule (section 2.9) is a
statement about where retains are, which a later lowering could not check.
Release-to-zero dispatches the destructor through the object header `[OPEN]`
(header layout, weak references). Spec §6.5's count is the runtime's.

### 2.9 Calls and the IR calling convention

Argument order in `extra`: (1) `ptr` to caller-owned return storage when the
declared return is an aggregate; (2) dictionaries, one `ptr` per generic
parameter in declaration order (spec §7.1); (3) capability carriers, one
`ptr` per row item in written order (section 2.10); (4) declared parameters
in order. Scalars, `ptr`, `ref` by value; aggregates by `ptr` to caller-owned
storage the callee reads and never writes. References: parameters borrowed
(spec §6.3, decision 3 — the callee retains only what it stores); returned
references owned, one retain transferred to the caller. A function value is a
`ptr` to a closure whose first word is the code address; `callind` passes it
as a hidden first argument. Machine ABIs belong to the backends: an
`externus` signature carries its `abi` (spec §5.3); aggregates by value across
`externus` need SysV classification, which the C backend inherits and the
reference backend does not yet have (`[UNIMPLEMENTED]`, ADR 0012).

### 2.10 Capability rows: the boundary

Rows are erased at the end of Stage 2. What survives is that some
capabilities have a runtime carrier — `sub alloc = a;` (spec §4.5) binds an
arena, and a callee declaring `poscit alloc` receives it — and the checker
decides per capability whether one exists `[OPEN]`. The IR sees carriers as
the hidden `ptr` parameters of section 2.9 and nothing else: no row, no atom,
no substitution. A `dyn` value is a data `ptr` plus a witness-table `ptr`;
whatever rows a witness table carries at runtime (spec §15, item 5) is bytes
in `data` to the IR. The audit (spec §10.3) reads the ego, never the IR.

### 2.11 Textual format

One instruction per line; whitespace-separated tokens; `;` to end of line is
a comment; no commas, brackets or nesting. Values are `%N`, blocks `bN`,
functions `@name`, globals `$N`. **`%N` and `bN` are numbered densely in
order of definition in the file**, so a reader is one `Vec` per kind indexed
by N with no symbol table, and an out-of-sequence `%N` is a parse error by
design. The printer renumbers in print order (block index, phis, body), so
text and internal ids never need to agree. Only a phi may name a `%N` defined
later in the same function.

    File     ::= (Global | Function)*
    Global   ::= 'data' '$' INT INT INT HEXBYTES                 ; id size align bytes
    Function ::= 'functio' '@' NAME '(' Type* ')' '->' (Type | 'void') Attr* '{' Block+ '}'
    Attr     ::= 'numeri' IDENT IDENT IDENT IDENT | 'nucleus' | 'externus' IDENT
    Block    ::= 'b' INT ':' ['quisque'] Line*
    Line     ::= ['%' INT '='] OP Tok*      ; arity by table on OP; phi/call/callind read to end of line
    Type     ::= [ui]INT | 'f32' | 'f64' | 'ptr' | 'dptr' | 'ref' | 'refc' | 'red.' Type | 'fn.' INT

    functio @dot (ptr ptr u64) -> f32 numeri ad_parem vetita explicita conservata {
    b0:
      %0 = param ptr 0
      %1 = param ptr 1
      %2 = param u64 2
      %3 = iconst u64 0
      %4 = redinit f32 fadd ordinata 0
      jmp b1
    b1:
      %5 = phi u64 b0 %3 b2 %11
      %6 = cmp.lt u64 %5 %2
      br %6 b2 b3
    b2:
      %7 = index %0 %5 4
      %8 = load f32 %7 0 nativus
      %9 = index %1 %5 4
      %10 = load f32 %9 0 nativus
      %11 = addw u64 %5 1
      %12 = fmul f32 %8 %10          ; two roundings: this fmul, then contrib's fadd
      contrib %4 %12
      jmp b1
    b3:
      %13 = redfin f32 %4
      ret %13
    }

Spans are not in the text: a hand-written function gets the span of its line,
which is all a backend test needs. Round-trip is checked by re-parsing the
printer's output and comparing structures, spans excluded.

## 3. Verifier

Runs on complete IR, after construction, before either backend; a failure is
an `rassert` contract violation, not an `EXS-E` code (malformed IR is a
compiler bug). Rules: every operand resolves with the type its op requires;
one terminator per block, last; phis first, one operand per predecessor, in
predecessor order; every use dominated by its definition — dominators by
Cooper–Harvey–Kennedy over reverse postorder, successors in terminator-operand
order, computed only here; each `contrib` dominated by its `redinit` and each
`redfin` post-dominating every `contrib` of its handle (`rumpe` out of a
reduction loop `[OPEN]`); `retain`/`release` only on `ref`/`refc`; `red.F`
only in its three ops; `numeri` equal across every direct `call`; no `nop`.

## 4. Determinism audit

| this order | is a function of |
|---|---|
| value ids, block ids | AST walk order |
| textual `%N`, `bN` | print order: block index, phi list, body |
| phi operand order | predecessor edge order = order terminators were emitted |
| `extra` contents | append order during the walk |
| type ids, signature ids | first-use order (map insertion) |
| function, global ids | declaration order |
| trivial-phi sweep | index order, to a fixpoint |
| dominators (verifier) | reverse postorder over terminator-operand order |

No row names an address or a hash bucket. `rt/map.inc` backs `currentDef` and
type interning and is never iterated.

## 5. Hazards

- **H1 fixed capacities.** `rt/arena.inc` does not grow and `map_init` fixes
  its bucket count: the IR arena is sized per module and `currentDef` needs a
  variables × blocks estimate, both `[OPEN]`; over-run is an `rassert`.
- **H2 trapping ops are side effects.** A future pass treating `add` as pure
  has silently changed spec §5.4; the verifier cannot see it, review must.
- **H3 attributes the verifier does not yet enforce**: `@nucleus` (spec §5.5)
  and `quisque`; `terminus N` (spec §8.5) is a counter plus a `chk`-style trap
  here, and whether the bound is static for `certus` is not the IR's question.
  All `[OPEN]`.

## 6. Unresolved

`arborea w` when `n` is not a multiple of `w`; `u12:maior`-class fields under
`:minor`; narrowing `sicut`; `/`, `rem`, shifts ≥ width, `ftoi` edges (all
downstream of spec §8.4's open operator set); whether a `contrahe` variable is
readable in its body; `rumpe` in a reduction loop; the `+?` optional's
representation; object header, weak references, destructor dispatch;
dictionary layout (spec §15, item 5); which capabilities carry runtime values;
`dptr` and a vector group; `select`/`switch`; whether a constant-folded
trapping op is a compile-time diagnostic (it would need a spec §13 code; none
is proposed).

## 7. Spec amendments this design implies (owner's job, not done here)

1. Spec §5.2: define packing for byte-aligned widths above 8 that are not a
   multiple of 8, under both byte orders.
2. Spec §5.4: define `arborea w` exactly, tail included; say whether the
   accumulator is readable inside the body and what `rumpe` yields.
3. Spec §9.1: the SSA line should point here, as the amended spec §9.2 does.
4. Spec §13: nothing required. Runtime traps are not diagnostics.
