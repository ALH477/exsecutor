# 0017 — The `refero` expression form: how a program constructs a reference

**Status:** **PROPOSED, NOT SIGNED OFF, NOT IMPLEMENTED.** 2026-09-26. Nothing
in this ADR has been built. It is written to be argued with: the decision it
proposes fixes a surface syntax and an emitted calling sequence, and the
project's rule is that such a thing is recorded before it is written, not
after. The one thing that *was* implemented in the commit that adds this file
is a defect fix that is a prerequisite for any of it —
`tests/unit/lwr_refero_release.asm` and the `lwr_frame_push` clobber it pins —
and that fix needs no decision from this document.

**Relates to:** spec §6.3 (decision 1, decision 6), §6.4, §6.5, §6.6, §8.6 (the
`[OPEN]` at the `refero` EXPRESSION form), §4.5 (`sub alloc = a`), §4.6;
`docs/design/runtime.md` section 2.5, section 2.6 and open question 8;
`docs/design/ssa-ir.md` section 2.2, section 2.3 ("allocation is a `call` into
the runtime through the `alloc` carrier"), section 2.8, section 2.9;
`docs/design/c-backend.md` D1, D8 and the shim paragraph at line 883;
`compiler/x86_64/prelude/interface.inc` row 6 and part C; ADR 0015
(decision 1, `mensura` is 32 on the N64 row; decision 2, the IR's `ref` width is
not a target fact); ADR 0016 (the house pattern for amending an IR decision).

## Context

### What is already settled, and is therefore not this ADR's to choose

The briefing that produced this document said the decision "fixes `ExsObj`'s
layout in the emitted ABI permanently". **It does not.** That layout is already
fixed, in two files, with an assembler-time assertion tying them together:

```
EXS_OBJ_RC   = 0    ; u64      prelude/prelude.asm:182
EXS_OBJ_DTOR = 8    ; ptr      prelude/prelude.asm:183
EXS_OBJ_HEADER = 16 ; payload  prelude/prelude.asm:184
```

mirrored as `EXS_IFACE_OBJ_*` in `prelude/interface.inc:165-167` and checked by
`assert EXS_IFACE_OBJ_RC = EXS_OBJ_RC` and its two siblings under `if defined
EXS_SCRIPTOR_A`. Four routines already read it — `exsrt_retain`,
`exsrt_release`, `exsrt_retain_c`, `exsrt_release_c` — and two fixtures already
pin its behaviour (`tests/unit/prelude_arc.asm`,
`prelude_arc_resurrectio.asm`). A `ref` value points at the **header**, not the
payload, because `exsrt_retain` dereferences `[rdi + EXS_OBJ_RC]` with
`EXS_OBJ_RC = 0`. Spec §6.5 fixes `rc` at 64 bits *on every target including
ilp32*, so the N64 row does not get a narrower count.

So the emitted ABI for a reference is already decided. What is open is
narrower, and it is three things, not one.

### The three missing links

A program cannot reach the heap because the chain from `Mundus` to a usable
payload is broken in three places, only one of which the spec records:

**Link 1 — `Mundus` to an arena.** `interface.inc` row 6 already declares
`functio alloc(m: Mundus, n: mensura) -> alloc`, flagged
`EXS_IFACE_F_APERTUM`, carrying `EXS_IFACE_CAP_ALLOC`, bound to the symbol
`exsrt_alloc_novum`. The row's own comment says why it is APERTUM: "`[OPEN]`
the surface spelling and the unit of `n` (runtime.md 8); spec 4.5 says only
that `sub alloc = a` binds an arena." Measured on `956cec5`: every spelling of
the call is refused. In an `initium` that declares the capability —

```
publica functio initium(m: Mundus) -> u8 poscit alloc {
    firma n: mensura = 4096;
    firma a = alloc(m, n);   // error[EXS-E0305]: operation not defined on the type
    redde 0;
}
```

— and that is the whole of the output: one diagnostic, exit 1. The `poscit
alloc` is load-bearing in the measurement and not decoration. Drop it and the
refusal is TWO diagnostics, `EXS-E0421` ("undeclared capability (atom or
row)", with an insert fix offering `poscit alloc`) before the `EXS-E0305` —
which is the capability system working exactly as §4.6 says and tells you
nothing about this hole. Only with the capability granted does the E0305 stand
alone, and that is the one worth recording: the declaration exists and the
call still does not typecheck. This is a smaller
hole than links 2 and 3 — the row, the signature, the symbol and the capability
are all already written — but it is a hole, and nothing downstream can be
tested without it.

**Link 2 — an arena to a `refero<T>`.** Spec §8.6's `[OPEN]`: "The `refero`
EXPRESSION form — how a program CONSTRUCTS one." `prelude.asm:638` states the
mechanism and nothing implements it: "A `refero<T>` is this call plus the
ExsObj header", where "this call" is `exsrt_alloc_da(a, n, align)`.
`ssa-ir.md` section 2.3 fixes the *shape* — allocation is a `call`,
deliberately not an opcode — so no IR amendment is needed. `interface.inc`
part C records that `exsrt_alloc_da`, `_reconde` and `_dimitte` have no
declaration row "because they have no surface spelling to declare".

**Link 3 — a `refero<T>` to its payload.** **This is recorded nowhere and is
the largest of the three.** There is no dereference syntax, no auto-deref on
`.`, and no member-access path through `AST_TY_REF` anywhere in
`checker/types/member.inc` — its only `AST_TY_REF` test (`:1136`) sets the
`own` bit on a call result and has nothing to do with access. `apud` is a type
residence suffix (spec §5.5) and not a dereference. So even with links 1 and 2
built, a program could allocate a `refero<u64>` and never read or write the
`u64`. The spec's `[OPEN]` list at §8.6 does not mention this, and it should.

### What is already built, and works

Worth stating plainly, because it changes how much of this is design and how
much is typing. `refero<T>` parses in type position (spec §8.6's grammar,
`CST_REFERO_TYPE`, the one-token `refero <` peek), interns as
`(AST_TY_REF, a = T)`, lays out as a word, carries `EXS-E0520` across
`externus`, lowers to `BFA_TK_REF`/`REFC`, and `retain`/`release` exist as IR
opcodes 49/50 that the text parser reads, verifier rule 6 constrains, the
printer round-trips, and the reference backend emits as the four `exsrt_`
routines chosen by operand type. The AST-to-IR lowering already inserts them:
at a borrowed initializer (`__lwr_retain_if_borrowed`), at `redde`
(transferring the +1 per section 2.9), and at every scope exit in reverse
initialisation order (`lwr_frame_release`, `__lwr_release_to`).

As of the commit that adds this ADR, that machinery **runs from source for the
first time.** It did not before: `lwr_frame_push` wrote a frame count into
`LwrFrame.frelb`, which was harmless while every `freln` was 0 and a SIGILL in
the compiler the moment one was not. So the release half of ARC is now
reachable and tested; only *construction* is missing.

### The blocker this ADR cannot design around

**The C backend refuses `retain`/`release` by design, and that is not a shim
problem.** `backend_c/emit_c.inc:4040`:

> `retain/release: no object header exists in library mode (c-backend.md D1,
> D8) -- the one class where the reference DOES lower and this backend cannot`

`tests/ir/reject_c_arc.ir` pins it with `c-emit-exit=4`, and
`emit_c.inc:3957` calls it "the one place the two backends' verdicts
legitimately differ". The three C shims omitting the four `exsrt_alloc_*`
routines (`tests/c/exsrt_shim.c:29` and siblings; `c-backend.md:883`) is a
*second*, weaker wall behind that one: the emitter never gets far enough to
need a linker error, because it refuses the module by name first.

The consequence is sharp and needs to be stated before anyone plans work
against it: **a program that allocates cannot be emitted as C at all.** It
therefore gets no differential coverage (the four-toolchain agreement that is
the phase's whole claim), and it cannot run on the `mips64-none-o64` row at
all, because that row is `--emitte c` only. Extending the shims does not
change this. Reversing it means giving C mode a real object header and the four
allocation routines — which is exactly what `c-backend.md` D1 defers as a
later, undesigned milestone, and it would flip `reject_c_arc.ir`'s expectation.

## Decision (proposed)

### 1. `ExsObj` is not reopened

The layout above stands as written, `rc` at 64 bits on every row per spec §6.5,
a `ref` value pointing at the header. **Nothing in this ADR changes an emitted
ABI that four implemented routines and two passing fixtures already depend on.**
On the `mips64-none-o64` row, where `mensura` and pointers are 32 bits, the
header is `{ u64 rc; ptr dtor; }` — 8 + 4, padded to `EXS_OBJ_HEADER = 16`, so
the payload offset is a constant on every row and does not become a
target-dependent number the shims and the emitter could disagree about. This
follows ADR 0015 decision 2's precedent (the IR's `ref` width is not a target
fact) and ADR 0015 decision 4's (the prelude interface layout is the
compiler's, in both places).

### 2. The constructor is spelled `refero<T>(e)`

**Recommended.** The expression form mirrors the type form exactly, and the
initializer supplies the payload:

```
firma r: refero<u64> = refero<u64>(7);
```

Why this and not the alternatives:

- It needs **no new keyword.** `refero` is already tier-1 reserved (spec §8.4)
  and the parser already has the only lookahead it needs: `__cst_at_refero`'s
  one-token `refero <` peek, which spec §8.6's amendment argues is unambiguous
  because "`<` is a delimiter and never an operator" (decision 6). The same
  peek serves in expression position for the same reason.
- It needs **no new prelude surface type.** An `Arena` type with methods
  (below) would need rows, a receiver, and a name in the interface pool.
- The payload arrives **at construction**, which is what lets link 3 be
  deferred without making the feature untestable: a `refero<T>` whose payload
  was written once at birth is observable through its destructor and through
  arena exhaustion even with no dereference syntax.
- `own[node]` is **already** the right value for it. The checker sets `own` on
  "a `Call` yielding `ref`/`refc` and nowhere else"
  (`checker/types/types.inc:106`), and a constructor is exactly a call that
  yields an owned reference, so `__lwr_retain_if_borrowed` already does the
  right thing — no retain on the binding — with no change.

**Alternatives considered:**

| form | cost |
|---|---|
| `refero<T>(e)` *(recommended)* | reuses the existing peek and the existing `own` rule; adds one CST kind, one AST node, one checker rule, one lowering |
| `novum refero<T>(e)` | a new tier-1 keyword for no parsing benefit; `refero` already peeks |
| `a.novum<T>(e)` on a bound arena | needs an `Arena` surface type, a generic method row, and generic instantiation the interface table has no mechanism for (`ssa-ir.md` section 2.2 marks generics `[OPEN]`) |
| `alloc.refero<T>(e)` on the capability atom | a capability atom is not a value with methods; would make §4.6's atoms into types |

### 3. Link 1 is a repair, not a design: make `alloc(m, n)` callable

Row 6 already says what it is. The proposal is to make the call admissible with
`n` in **bytes of payload capacity**, returning a value of the `alloc`
capability type that `sub alloc = a;` binds per spec §4.5, and to drop
`EXS_IFACE_F_APERTUM` from the row once it is. Bytes because
`exsrt_alloc_novum` already takes bytes and rounds to whole pages itself, so
any other unit is a conversion invented at the surface for no reader's benefit.

### 4. Link 3 is deferred, with a direction, and recorded as `[OPEN]` in the spec

Not decided here. The direction the rest of the language already implies is
**auto-dereference on `.` for a `refero<S>` where `S` is a `structura`**, which
costs no new syntax and covers the case references exist for (object graphs,
spec §6.7), plus **no scalar dereference at all** in the first landing — so
`refero<u64>` stays constructible and releasable but not readable. The spec's
§8.6 `[OPEN]` list should gain this bullet regardless of what is chosen,
because its absence is currently invisible.

### 5. The first landing is reference-backend only, and says so

Given the blocker above, the honest scope is: allocation lowers on the
reference backend, `--emitte c` refuses the module with the message it already
has, and no fixture that allocates enters the differential or cross phases.
Reversing `c-backend.md` D1/D8 is a separate ADR and a separate milestone.

## Consequences

- **`DIFFERENTIAL_*` and `CROSS_PROGRAM_FLOOR` gain nothing** from any of this,
  and any fixture that allocates must declare `c-differentia=` to stay out of
  the differential phase — which means allocation ships with weaker evidence
  than every numeric feature in the tree, because the four-toolchain agreement
  does not apply to it. That is a real reduction in the strength of the rig's
  claim, and it is the strongest argument for doing the C-mode object header
  first and the constructor second.
- `EXS_IFACE_DECL_COUNT` rises from 11 to 12 if `exsrt_alloc_da` gains a row.
  Part C's note that it has "no surface spelling to declare" stops being true
  once decision 2 lands. New rows are **appended**, never inserted, because a
  decl id is an index (`interface.inc:271`).
- The `certus` profile (`docs/design/profile-certus.md`: all arenas sized in
  `initium`, nothing after) is satisfied by decision 3 as stated and is not
  amended.
- Destructor synthesis stays `[UNIMPLEMENTED]`. For a `refero<T>` where `T`
  holds no references, `dtor` is 0 and nothing needs generating; a `T` that
  holds references needs a synthesized routine that releases its fields
  (`ssa-ir.md` section 2.7), which is codegen this ADR does not propose. The
  first landing should therefore **refuse** a `refero<T>` whose `T` holds a
  reference rather than emit a leak — and it must do so with a **shipped**
  code, not a new one. CLAUDE.md's rule is "Never invent a code", and "a new
  code requires a spec amendment to §13 first"; this cycle is additionally
  under a standing instruction to add none at all. `EXS-E0305`
  ("operation not defined on the type", spec §13) is the honest shipped fit:
  the constructor is an operation, and it is not defined on that `T` yet. If a
  measurement later shows `EXS-E0305`'s span or wording genuinely cannot carry
  the case, that is an argument for a new code in some *later* cycle and is
  not settled here.
- Reference cycles still leak (spec §6.7, `[OPEN]`, no fix available). Nothing
  here changes that.

## Open

- **Link 3, the whole of it.** See decision 4.
- **Whether C mode gets an object header** — i.e. whether `c-backend.md` D1/D8
  is reversed. Until it is, decision 5 stands and allocation has no
  differential evidence.
- **A size-class free list** so a released object's bytes can be reused
  (`runtime.md` section 2.6). Today release-to-zero reclaims nothing and only
  `reconde` returns bytes.
- **Arena growth**, `[UNIMPLEMENTED]` for the reasons `rt/arena.inc` gives.
- **Weak references**, named in `ssa-ir.md` section 2.8's `[OPEN]` alongside the
  header layout (the header half is now settled; the weak half is not).
- **No new `EXS-E` code is available to any of this.** A new code needs a
  spec §13 amendment first (CLAUDE.md), and this cycle is instructed to add
  none, so every refusal decisions 2-5 imply must land on a shipped code or on
  a named `rassert`. That constrains the design and not merely the wording: a
  rule that cannot be said with an existing code is a rule this cycle cannot
  ship.
- **`EXS_IFACE_PRAEFIXUM_RESERVATUM`** — `interface.inc` part C's H6 request,
  that a module-level name beginning `exsrt_` be refused by the checker. Adding
  an `exsrt_alloc_da` row makes the declared set larger and the unreserved
  collision surface no smaller. Still `[OPEN]`, still not this ADR's.
- **Spec line 668 is stale** and should be corrected in whichever commit next
  touches §5.2: it says `refero<u8>` "does not parse in type position today",
  which spec line 1980, the grammar at line 2090 and a direct measurement all
  contradict. Recorded here because this ADR is the document that had to
  establish it.
