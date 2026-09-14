# SSA IR — design plan

Status: **partly built.** `compiler/x86_64/backend_fasmg/` (8632 lines, c659ad7)
implements section 2.1's representation, 2.11's parser and printer (round-trip
tested) and the naive emitter of spec §9.2 — its output assembled by real fasmg,
run, and correct, against hand-written IR with no frontend. **Not built:** the
verifier (section 3) and the AST → SSA construction (section 2.5, Stage 3); no
lowering exists, so nothing here has yet run on IR a frontend produced. This
line said "design only; no IR exists" until 2026-09-10, three commits after it
stopped being true.
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

**Canonical form of a narrow integer** (`wire-codec.md` D5; exercised by
`tests/ir/narrow_wrap.ir`, `trap_add_{u4,u8,u24,u32,i8}.ir`,
`trap_sub_u8.ir`, `trap_mul_u8.ir`, `trap_mul_u40_2p64.ir`,
`trap_mul_u40_below_2p64.ir`, `mul_u40.ir`, `conv_roundtrip.ir`,
`tests/unit/bfa_emit_narrow.asm` and `tests/programs/angusta/`). In a
64-bit register or slot a `uN` with N < 64 is held
**zero-extended**: bits N..63 are zero. An `iN` is held **sign-extended**:
bits N..63 equal bit N−1. `u1` is 0 or 1. Every instruction may assume its
operands are canonical and must leave its result canonical. The emitter
normalises after every operation that can produce a non-canonical raw
result — the wrapping group, `shl`, `trunc`, and a `sext` into a `uN` —
with `shl 64−N` then `shr` (`sar` for `iN`); `and`, `or`, `xor`, `shr` and
the compares preserve canonical form on canonical inputs and need nothing.
(The first draft of this sentence left out `sext` into a `uN`: `sext u16`
of `i8` −56 is 65480, and the sign-extended −56 must be cut to 16 bits to
be it. It also said nothing of a `zext` of an `iN` or a `sext` of a `uN`,
which section 2.3's "by source sign" means the lowering never emits but
the verifier does not refuse; the emitter first extends from the source
width, so `zext u16` of `i8` −56 is 200. Both found implementing M3;
`tests/ir/conv_roundtrip.ir`.) A trapping op computes
in 64 bits, normalises, and traps if the normalised value differs from the
raw one — complete for `add`/`sub` at every width below 64 and for `mul` at
widths up to 32, where the 64-bit product is exact. **Not complete for `mul`
at widths 33–63**: in `u40`, 2^32 × 2^32 = 2^64 has raw result 0, which
normalises to 0 and would not trap. So `mul` traps *also* on the 64-bit
`mul`/`imul` overflow flag, and at width 64 `add`/`sub`/`mul` use the flags
alone, as today. (`wire-codec.md`'s review, M2; the M3 tests include a
`u40` product at and just below 2^64. Both of those trap — anything at or
above 2^40 is out of range — and they differ in which check catches it:
2^64 only the flag, 2^64 − 1 only the compare. The products that must not
trap are `tests/ir/mul_u40.ir`'s, up to 2^40 − 1.) Spec §5.4 states only that a `uN` value is an integer in
[0, 2^N); this paragraph is the reference backend's way of holding one, and
the C backend may hold it differently as long as ADR 0012's differential test
cannot tell.

### 2.3 Instruction set

`T` an integer type, `F` a float type; operands share the named type; result
is that type unless shown. Text form: `%n = op T operands`.

| group | instructions | semantics |
|---|---|---|
| integer, trapping | `add sub mul div rem` | spec §5.4 `+`; overflow or zero divisor traps; **side-effecting**: never removed if unused, never reordered across another side effect |
| integer, wrapping | `addw subw mulw` | `+%`, modulo 2^N |
| integer, saturating | `adds subs muls` | `+\|`, clamped to `T` |
| overflow predicate | `addov subov mulov → u1` | true iff the trapping form would trap; `+?` is `addov` + `addw` |
| bitwise | `and or xor shl shr` | `shr` arithmetic for `iN`; bits shifted beyond N discarded, result normalised (section 2.2; `tests/ir/shift_narrow.ir`, `bitwise.ir`, `tests/unit/bfa_emit_bitwise.asm`); **a count ≥ N traps** (spec §5.4; `tests/ir/trap_shl_u8.ir` count 8 in `u8`, `trap_shr_u32.ir` count 32 in `u32`), so `shl`/`shr` are **side-effecting** like the trapping group: never removed if unused, never reordered across another side effect. The `[OPEN]` this row carried is closed by that sentence |
| compare | `cmp.eq .ne .lt .le .gt .ge → u1` | signedness from `T` |
| convert | `zext sext trunc` | `zext`/`sext` by source sign; `trunc` keeps low bits — which is now what spec §5.4 says a narrowing or equal-width `sicut` means (the marker this row carried, "narrowing `sicut` `[OPEN]`", was stale from the day `__lwr_cast` emitted `trunc`: the emitter implemented truncation and the spec caught up) |
| float | `fadd fsub fmul fdiv fneg` | one IEEE rounding each, in the function's `rotundatio`, subnormals per `subnormales` |
| fused | `fma` | one rounding; the only contraction that can exist (`contractio explicita`) |
| float misc | `fcmp.* → u1`, `fext ftrunc itof ftoi`, `bitcast` | `fcmp` ordered; unordered forms, `ftoi` on NaN/range `[OPEN]` |
| reduction | `redinit F op shape w → red.F`, `contrib %h %v`, `redfin F %h → F` | section 2.6 |
| leaves | `faddr @f → ptr` | the code address of a declared function — the only instruction that yields one, and what section 2.9's "a function value is a `ptr` to a closure whose first word is the code address" needs to exist (lowering.md finding 12; proposed by the backend in 612b0c9, added here, **not yet in `ir.inc`/`emit.inc`** `[UNIMPLEMENTED]`) |
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

**How the reference backend lowers a phi list** (`wire-codec.md` D8;
`emit.inc` aborted on any phi until M2, 1b238c1; exercised by
`tests/ir/phi_sum_loop.ir`, `phi_swap.ir`, `phi_lost_copy.ir`,
`phi_same_target.ir`, `tests/unit/bfa_emit_phi.asm` and
`tests/programs/phi_loops/`). Each incoming edge into a block with phis is a **parallel copy**, done
through the stack: push each phi's operand for that edge, in phi order; then
pop into each phi's slot in reverse order. Every read precedes every write,
so a swap (`%a = phi [%b …]`, `%b = phi [%a …]`) and the lost-copy case are
correct with no cycle analysis and no scratch register. A `jmp` emits the
copies before the jump. A `br` has two targets whose copies differ, so each
edge from a `br` into a block with phis goes through a **per-edge stub** —
`jcc stub_T; jmp stub_F`, each stub doing its copies and jumping on. A block
with no phis **must** produce exactly the text it produced before: the
fixtures that pin emitted text are a requirement on the change, and a diff
in one is a defect in the change rather than a fixture to update. M2 met
it: `bfa_emit_tier1/tier2/program.asm` and the hello world through the
publish gate were byte-identical before and after (1b238c1). A `br` whose
two targets are one block reads the phi's second pair naming that
predecessor on its false edge, in edge order (`phi_same_target.ir`).

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
straddling a byte if the declaration did. (An earlier draft said spec §5.2
admitted `u12:maior` and left its packing open; §5.2 rule 2 now says `u12`
does not parse, so there is no such case. The verifier refuses a `load` or
`store` that is not a whole number of bytes —
`tests/ir/reject_verify_load_width.ir` — and admits a straddling
`loadbits`, which the emitter then refuses, the two layers disagreeing on
purpose: `tests/ir/reject_emit_straddle.ir`. `DeModFrame` never
straddles.) A float `load`/`store` — `load f64 %p 8 nativus` — carries the
value as its raw IEEE bit pattern, 8 bytes for `f64` and 4 for `f32`,
`nativus` order only: `maior`/`minor` order an integer's bytes (spec §5.2),
so the verifier refuses a float under them
(`tests/ir/reject_verify_float_load_ord.ir`), and both emitters refuse the
same text for the path that reaches them without the verifier. The nativus
case lowers in both backends since 2026-09-14 (the signaculum stage needed
it; `tests/ir/float_mem.ir` pins the bit-baggage semantics — a NaN payload,
-0.0's sign and a subnormal's bits all survive). `copy n` is bytes only; a
struct holding references is copied as bytes plus one `retain` per reference
field known from the layout. Bounds are explicit `chk` instructions, so both
backends trap at the same point.

The reference emitter gives `copy n` two forms, chosen by
`BFA_COPY_UNROLL_MAX = 128` in `backend_fasmg/emit.inc` (`9ede8bf`): at or
below 128 bytes it is unrolled at compile time into 8-byte steps and a
4/2/1 tail, the text every pinned fixture has always seen (`copy 7`,
`copy 17`); above it the 8-byte steps become a runtime loop with the
counter in the instruction's own stack slot — the emitter keeps no value in
a register across an instruction, so the loop needs no fourth scratch — and
the same tail after, a fixed dozen lines at any `n`. The unrolled form is
~7.35 bytes of fasmg per byte copied, and a struct literal with an
`acies<i64, 18000>` field (six lines of source, a 144,000-byte `copy`) used
to exhaust the compilation arena, which is sized from the *source*
(`docs/design/receptor.md` finding 20). The bytes moved are the same either
way: `tests/ir/copy_magna.ir` (100,003 bytes through the loop, five
positions checked including one past the end) and
`tests/programs/copia_magna/`.

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
`ptr` per row item in row order — ascending atom id; an interned row has no written order (`checker.md` section 2.8) — (section 2.10); (4) declared parameters
in order. Scalars, `ptr`, `ref` by value; aggregates by `ptr` to caller-owned
storage the callee reads and never writes — **unless the parameter is declared
`&mutabilis T`** (spec §6.3 decision 3, §8.6), which is a mutable borrow the
callee may write through and the caller must have definitely assigned. The
IR does not distinguish the two: both are one `ptr` in group (4), and the
permission is a Stage 2 fact erased before any backend sees it, exactly as a
capability row is. Nothing in the IR or either backend changed to admit it.
References: parameters borrowed
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
    Function ::= 'functio' '@' NAME '(' Type* ')' '->' (Type | 'void') Attr* ( '{' Block+ '}' | ';' )
                                            ; ';' — a declaration: the body is elsewhere (an `externus`
                                            ; function, or a prelude routine, runtime.md 2.2); nothing
                                            ; is emitted for it. Added after the hello world needed
                                            ; `call @exsrt_scriptor_scribe` to resolve (612b0c9, finding
                                            ; 2); `parse.inc` still requires a placeholder body
                                            ; `[UNIMPLEMENTED]`.
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

**Predecessor order is not in the text either.** A top-to-bottom parse emits
terminators in block order, so a parsed function's edge order (section 2.4)
always equals its block order; a *constructed* function whose edges were
pushed in a different order — `tests/unit/bfa_ir_builders.asm`'s `@iungo`,
predecessors `(b3, b1)` — prints to text that re-parses into a function that
fails verifier rule 3. Text round-trip is exact for the instructions and not
for the edge order, and cannot be until this format gains a way to spell it
(a `preds` annotation on a phi's block, `[OPEN]`). Measured, not argued
(612b0c9, finding 3).

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
only in its three ops; `numeri` equal across every direct `call`; no `nop`;
and — added once a real `call` path existed (612b0c9, finding 6) — a `call`'s
argument count and types match the callee's signature and its result type the
callee's return type `[UNIMPLEMENTED]`: Tier 2 emits arguments straight from
`extra`, so a lowering that emitted the wrong arity would produce a wrong call
nothing rejects.

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

`arborea w` when `n` is not a multiple of `w`; `/`, `rem`, `ftoi` edges
(narrowing `sicut`'s spec text was on this list — the lowering emitted
`trunc` and `tests/programs/angusta/` ran one while spec §5.2 had only
the widening sentence — and spec §5.4 now defines narrowing and the
equal-width sign change as truncation, matching the implementation);
(downstream of spec §8.4's open operator set — shifts ≥ width were on this
list and are settled in section 2.3, `tests/ir/trap_shl_u8.ir`;
`u12:maior`-class fields were on it and no longer parse); whether a `contrahe` variable is
readable in its body; `rumpe` in a reduction loop; the `+?` optional's
representation; object header, weak references, destructor dispatch;
dictionary layout (spec §15, item 5); which capabilities carry runtime values;
`dptr` and a vector group; `select`/`switch`; whether a constant-folded
trapping op is a compile-time diagnostic (it would need a spec §13 code; none
is proposed).

## 7. Spec amendments this design implies (owner's job, not done here)

1. Closed by spec §5.2 rule 2 the other way: widths above 8 that are not a
   multiple of 8 do not parse, so there is no packing to define. (Was:
   define packing for such widths under both byte orders.)
2. Spec §5.4: define `arborea w` exactly, tail included; say whether the
   accumulator is readable inside the body and what `rumpe` yields.
3. Spec §9.1: the SSA line should point here, as the amended spec §9.2 does.
4. Spec §13: nothing required. Runtime traps are not diagnostics.
