# Runtime prelude — design plan (Stage 3)

Status: `[OPEN]`. Design only; no prelude exists, nothing below has run, and
no file under `compiler/` is touched by this document. `spec §N` cites
`docs/spec/exsecutor-spec-v0.4.md`; `IR n.m` cites `docs/design/ssa-ir.md`;
`AST n.m` cites `docs/design/typed-ast.md`; `CHK n.m` cites
`docs/design/checker.md`. Taken as given: spec §16's Stage 3 line, §4.1,
§4.5–§4.7, §5.1, §5.3, §5.4, §6, §9.2, §9.3, §11, §12 (the two paragraphs
on repeated `SOURCE` and text `OUT`), §18.1–§18.2; ADR 0012; IR 2.8–2.10;
CHK 2.8 (runtime carriers: every atom is one `ptr`); the header comment of
`compiler/x86_64/backend_fasmg/emit.inc`, which is the seam this document
designs against. This is the contract the three agents of section 6 build
against and `tools/publish-gate.sh` exercises. One decision per question,
with the constraint that forces it. Nothing here is written into
`docs/spec/`; section 9 lists what should be.

`docs/design/checker.md` existed when this was written and its section 2.8
is what section 2.2 below designs against: `Mundus` is the `ptr` passed to
`initium`, `ambitus` a `ptr` derived from it, the standard streams under
`ambitus` (spec §4.6), every other atom `[OPEN]`.

## 1. The constraints and what they force

1. **`OUT` is text and `exsc` never assembles it** (spec §12). The prelude
   therefore cannot be a file `fasmg` finds on a path — nothing but
   `vendor/fasmg-x86/` is on the include path, by the same sentence — so
   it is bytes that `exsc` *writes into* `OUT` (section 2.1).
2. **Spec §9.3: byte-identical `OUT`** for identical inputs across
   directory, time and locale. So the prelude is a constant of `exsc`'s
   binary, never generated at compile time from anything but the input,
   and `OUT` names no path, no time and no host (section 4).
3. **The emitter's seam** (`emit.inc`'s header): emitted functions are
   plain SysV AMD64, hand-rolled prologues, `bfausr_<name>` labels, one
   shared `bfausr_trap`, no `format`/`entry`/`segment`, no dependency on
   `macros/proc.inc` or the `r15` pin. The prelude must be callable from
   exactly that and nothing more: **SysV, no `proc` macros, no `r15`**
   (section 2.2). `docs/asm-conventions.md` §1.2 governs `exsc`'s
   internals and not the program `exsc` emits.
4. **`saluta` has an empty row** (`examples/saluta.exsc`). It returns a
   `textus` and may not allocate, so a literal cannot be a heap object and
   `textus` cannot require a refcount (section 2.3).
5. **Spec §4.7 and CHK 2.8**: `initium(m: Mundus) -> u8`, one hidden `ptr`
   per carrier under IR 2.9, the result is the exit status. The entry stub
   is fixed by these three sentences (section 2.2).
6. **Spec §5.4 only the reference backend can honour** (ADR 0012, problem
   2): `subnormales conservata` and `rotundatio` are MXCSR state and must
   be *set*, not assumed, before the first float instruction (section 2.7).
7. **Spec §6.5, §6.6**: 64-bit saturating count, abort on saturation,
   abort on resurrection; IR 2.8 puts `retain`/`release` in the IR and
   leaves the header and destructor dispatch to the runtime (section 2.5).
8. **The program's syscall surface is auditable** the way `exsc`'s is
   (spec §18.1, `tools/syscall-audit.sh`), and spec §10.3's audit is a
   claim about the *binary* only if the binary's syscall set is a function
   of the ego's `potestates` (section 2.6).

## 2. Decisions

### 2.1 The artifact: one static blob, `incbin`'d into `exsc`, written verbatim into `OUT`

**The prelude is a static text blob** — `compiler/x86_64/prelude/prelude.asm`,
pulled into `exsc`'s data segment with fasmg's `file` directive exactly as
`compiler/shared/unicode/tables/tables.inc` pulls in the Unicode tables —
and `exsc` copies it into `OUT` byte for byte. It is not generated.

Reasons, in order. (a) Spec §9.3: a blob's bytes are a function of
`exsc`'s own bytes and of nothing else, so the largest single component of
`OUT` is deterministic by construction rather than by discipline. (b) There
are no format strings in this compiler (spec §8.3's rule for diagnostics,
applied to codegen): a generated prelude is hundreds of `__bfa_out_lit`
calls interleaved with numbers, which is where a stray path or count would
leak in; a blob has one emission site. (c) The blob is assembled and run
*by itself* in a `tests/unit/` fixture before any compiler output exists
(section 6), the same way `emit.inc` was tested against hand-written IR.
(d) It is architecture-neutral in the sense spec §18 uses the word: an
aarch64 prelude is a second blob, not a second generator.

What varies per program is not the blob but **eleven constants printed
ahead of it** — one per capability atom in spec §4.6 order — and one for
the MXCSR image (section 2.7). The blob wraps every syscall-bearing routine
in fasmg `if EXS_POTESTAS_<ATOM> … end if`, so the *text* of `OUT` always
contains the whole prelude and the *binary* contains only the routines the
program's authority admits (section 2.6). Selection is done by fasmg from
constants `exsc` prints, not by `exsc` slicing the blob; the blob stays one
verbatim, content-addressed thing.

**Layout of `OUT`, top to bottom.** Every line is a function of the input
(section 4); nothing else may appear.

```
; exsecutor: reference backend                       ; fixed banner, no version, no path, no time
include 'format/format.inc'                          ; the one external include (spec §18.1's vendored package)
format ELF64 executable 3
entry exsrt_start
EXS_POTESTAS_MUNDUS     = 1                          ; eleven constants, spec §4.6 order, 0 or 1,
EXS_POTESTAS_ALLOC      = 0                          ; from the checker's computed closure of initium
EXS_POTESTAS_SERMO      = 0                          ; (spec §10.3); the ego reader does not exist yet
EXS_POTESTAS_HOROLOGIUM = 0                          ; and the gate runs with no ego at all -- section 9, 8
EXS_POTESTAS_ARCHIVUM   = 0
EXS_POTESTAS_RETE       = 0
EXS_POTESTAS_FORTUNA    = 0
EXS_POTESTAS_AMBITUS    = 1
EXS_POTESTAS_FILUM      = 0
EXS_POTESTAS_MACHINA    = 0
EXS_POTESTAS_CRUDUM     = 0
EXS_MXCSR               = 0x1F80                     ; section 2.7, from initium's Func.numeri
segment readable executable
exsrt_start:                                         ; ---- the prelude blob, verbatim, begins here ----
        …entry stub, carriers, Scriptor, ARC, abort…
                                                     ; ---- blob ends ----
bfausr_saluta:                                       ; ---- the module, exactly as bfa_emit_module
        …                                            ;      produces it, functions in declaration order
bfausr_imprime_gutenbergio:
        …
bfausr_initium:
        …
bfausr_trap:                                         ; emit.inc's shared stub (section 2.5 asks that it
        db      0x0F, 0x0B                           ; become a jump into exsrt_abort; today it is ud2)
segment readable                                     ; ---- data: string literals, IR globals in id order
bfausr_g1:                                           ; `data $1 101 1 …` -> label, then bytes
        db      0x41,0x76,0x65,0x2C,…                ; hex bytes only, never a quoted string
segment readable writeable                           ; ---- the prelude's mutable state: the second blob,
        …exsrt_mundus, exsrt_ambitus, exsrt_arena…   ;      verbatim (see below)
```

Two points in that outline are decisions, not accidents.

*`exsrt_start` is the first byte of the executable segment.* `exsc.asm`'s
header records, from two measured runs, that the audit's linear sweep is
reliable only from the entry point, and that any code *before* it is
swept "with no such guarantee". The prelude therefore leads and the module
follows; the entry stub is the blob's first routine.

*The prelude's writable data is not in the blob.* A `postpone` block or a
second segment inside the blob would put a `segment` directive in the
middle of the text `exsc` then appends to, and the emitter's own header
says its output expects to sit inside a segment the harness opened. So the
blob is executable text only, and its mutable state is a **second, small
blob** — `prelude_data.asm`, five labels — that `exsc` writes after the
data segment. Two files, both verbatim, both `file`'d. The blob and its
data agree on names only, never on offsets.

*Literals are hex `db` lines, never quoted.* A fasmg string cannot hold a
raw newline, and `saluta`'s literal holds six; escaping is a format-string
problem this document refuses to have. 101 bytes is about 500 characters
of hex. The segment is `readable` and not `writeable`, so immutability of
literals is a property the MMU enforces, not one the checker promises.

### 2.2 The entry stub, carriers, and how a runtime fact reaches a program

**`_start` is `exsrt_start`**, in the blob:

```
exsrt_start:
        mov     rax, rsp                        ; initial stack: [rsp]=argc, argv…, 0, envp…, 0, auxv
        mov     [exsrt_mundus + 0], rax         ; ExsMundus.rsp0 -- recorded, not read (spec §11)
        mov     dword [rsp - 4], EXS_MXCSR      ; section 2.7: set, never assumed
        ldmxcsr dword [rsp - 4]
        lea     rdi, [exsrt_mundus]             ; the one carrier: Mundus, as a ptr (CHK 2.8)
        call    bfausr_initium                  ; IR 2.9: carriers first; initium has one and no declared params
        movzx   edi, al                         ; u8 result -> exit status (spec §4.7)
        mov     eax, 231                        ; exit_group -- immediate load, audit-resolvable
        syscall
```

Alignment: `rsp` is 16-byte aligned at process entry (SysV), so a direct
`call` gives the callee the `rsp ≡ 8 (mod 16)` it expects, and
`emit.inc`'s prologue keeps frames 16-byte multiples, so calls out of
emitted code are aligned too. `rdx` at entry holds a libc `atexit` hook
under glibc's convention; there is no libc and it is ignored. Nothing here
reads `envp` or the auxiliary vector: the stub stores where the initial
stack *is* and only an `ambitus` accessor (below) ever looks.

**What a carrier is at runtime: a pointer to a prelude-owned record whose
layout only the prelude knows.** CHK 2.8 fixes the type (`ptr`, uniformly)
and this fixes the pointee:

| carrier | points at | who fills it |
|---|---|---|
| `Mundus` | `ExsMundus { rsp0 ptr @0 }` — one static record in `prelude_data.asm` | the entry stub |
| `ambitus` | `ExsAmbitus { in i32 @0, out i32 @4, err i32 @8, argc u64 @16, argv ptr @24, envp ptr @32 }`, static | `exsrt_mundus_ambitus`, on first derivation, from `rsp0`; descriptors 0/1/2 |
| `alloc` | `ExsArena { base ptr @0, cur ptr @8, limit ptr @16 }`, one per `m.alloc(n)` | section 2.6 |
| the other eight | `[OPEN]` — a pointer to a record the prelude defines when the atom gets a routine; `Crudum` is a never-dereferenced token (CHK 2.8) | — |

Unforgeability (spec §4.1 rule 1) is a static property the checker
delivers; the runtime record has no magic word and no check. A program
holding `Crudum` can fabricate any of these, which spec §10.3 already
prices as `Crudum (⇒ ALL)`.

**Derivation is a call, not an intrinsic.** `m.ambitus()` lowers to
`%a = call ptr @exsrt_mundus_ambitus %m`. The rule that decides this — and
that resolves AST 2.7's `[OPEN]` on `summa_ordinata` the same way — is:

> **A name is an intrinsic iff its lowering has no `call` form. Everything
> else the prelude supplies is a function, called under IR 2.9.**

`summa_ordinata`/`summa_arborea` are intrinsics because they lower to
`redinit`/`contrib`/`redfin` and a `red.F` cannot be passed (IR 2.2), so
there is no function they could be. `m.ambitus()`, `Scriptor.ad_exitum`,
`s.scribe`, `retain`/`release`'s targets, arena creation — all have a
`call` form, so they are prelude routines with a `bfausr_`-callable name.
Reasons: the emitter then knows **no runtime layout** — not the `Mundus`
record, not the object header, not `Scriptor`'s fields — so the blob can
change without touching `emit.inc` and is content-addressed on its own;
and the naive backend needs `call` anyway for `imprime_gutenbergio`, so
intrinsics would be a second mechanism for zero saved instructions.

The prelude's routines are SysV AMD64 with hand-rolled prologues, prefixed
`exsrt_` (unburned — `docs/asm-conventions.md` §4.1's list has no such
name), and use **only `rax rcx rdx rsi rdi r8–r11`** as scratch so that
they are callable from `emit.inc`'s output, which preserves nothing across
a call and expects nothing preserved but the SysV callee-saved set.

**Names.** `emit.inc` prints every `@name` as `bfausr_<name>` and checks
nothing — the assembler resolves it. So the IR calls
`@exsrt_scriptor_scribe`, the emitted text says
`call bfausr_exsrt_scriptor_scribe`, and **the blob labels each
IR-callable routine `bfausr_exsrt_<x>`**, obeying the emitter's prefix as
the name ABI it is. Private routines and every data label stay at plain
`exsrt_<x>` — unreachable from IR by construction, since the IR can only
name things the emitter prefixes. The table below and the rest of this
document write the short `exsrt_<x>` for the IR-callable ones; the label
in the blob carries the prefix.

**The prelude interface** is what the checker resolves these names
against. No source form exists for it — spec §8.6 has no receiver
syntax, no `Self`, no method declaration, and no way to declare `Mundus`
(CHK finding 6; section 9, 1) — so it is a **table** the checker includes,
`compiler/x86_64/prelude/interface.inc`, owned by the prelude agent and
consumed by `checker/resolve/` as a set of pre-seeded `Decl`s. For the
hello world it is exactly:

| Exsecutor signature (first parameter is the receiver, CHK 2.7) | prelude symbol | IR signature |
|---|---|---|
| `functio ambitus(m: Mundus) -> ambitus` | `exsrt_mundus_ambitus` | `(ptr) -> ptr` |
| `structura Scriptor { a: ambitus, descriptor: i32 }` — capability-bearing, mark `{ambitus}` (spec §4.3) | — | 16 bytes, align 8 |
| `functio ad_exitum(a: ambitus) -> Scriptor` (on `Scriptor`) | `exsrt_scriptor_ad_exitum` | `(ptr ptr) -> void` — hidden return `ptr` first (IR 2.9), then `a` |
| `functio scribe(s: Scriptor, t: textus) -> mensura` | `exsrt_scriptor_scribe` | `(ptr ptr) -> u64` — both aggregates by `ptr` |
| `retain`/`release` (IR instructions, not names) | `exsrt_retain` `exsrt_release` `exsrt_retain_c` `exsrt_release_c` | `(ptr) -> void` |
| `functio alloc(m: Mundus, n: mensura) -> alloc` `[OPEN]` name | `exsrt_alloc_novum` | `(ptr u64) -> ptr` |

`sub ambitus = a;` lowers to nothing: it binds a scope-level carrier the
checker already resolved (spec §4.5), and the carrier is the `%a` the
derivation returned.

### 2.3 `textus` at runtime: a two-word view, never a heap object

**`textus` is a 16-byte value `{ ptr @0, len u64 @8 }`** — a pointer to
UTF-8 bytes and a byte count — with no header, no refcount and no
destructor. A literal's bytes are a `data $N` global in the readable
segment (section 2.1) and the literal expression is
`gaddr N` plus `iconst u64 len` stored into the 16-byte result.

What forces it. Spec §5.1 says slicing by byte offset is *O(1)* and
returns `eventus`: a slice of a view is another view — two words, no
allocation, no `poscit alloc` on `sectio`, which §5.1 does not give it. A
header-in-front layout (`{len, bytes…}`) would make every slice a copy or
an allocation. And `saluta`'s empty row (constraint 4) means the literal
must be a value the function can *return without allocating*; a
refcounted literal would need either a heap object or an "immortal" marker
that `retain` must special-case — a third refcount state that spec §6.5
does not describe.

What it forces in turn. **`textus` is an aggregate in the IR** (IR 2.2 has
no 16-byte scalar), so a function returning one takes a hidden `ptr` to
caller-owned storage (IR 2.9): `saluta`'s IR signature is
`functio @saluta (ptr) -> void`, and `initium` allocates a 16-byte `slot`
for `saluta()`'s result. `octeti()` is the identity on the view;
`scalares()` and `grapha()` are prelude routines that walk it
(`[UNIMPLEMENTED]`, and `grapha` needs the Unicode tables in the program —
spec §11's "content-addressed dependency of every ego using text", which
is why they are not in the hello world's blob). `mensura` is `u64` on
`x86_64-linux`.

**Ownership.** A view borrows. The bytes behind a literal are immortal;
the bytes behind a runtime-built `textus` live in the arena that built
them and die with it (spec §6.3's preallocate-and-reset, §6.7's "arenas
are the recommended pattern"). Spec §5.1 does not say whether `textus` is
a value or a reference type; this document decides value, and a `textus`
escaping its arena is a lifetime error the checker does not yet detect
`[OPEN]` (section 9, 4). Spec §6's ARC applies to `refero<T>` and not to
`textus`.

### 2.4 `Scriptor`: a concrete capability-bearing struct in the prelude, an interface when `norma` exists

**For the hello world `Scriptor` is a concrete `structura` supplied by the
prelude interface** (section 2.2's table): `{ a: ambitus, descriptor: i32 }`,
capability-bearing with mark `{ambitus}` under spec §4.3, which is exactly
what `imprime_gutenbergio`'s `poscit sicut s` resolves to — the row in
`s`'s *type*, per spec §4.2 after the amendment. §4.3 says such a type
"must be declared so" and gives no syntax; CHK finding 7 has the checker
compute the mark, and for a prelude type the table simply states it.

Reason it is concrete and not `interfacies Scriptor` as
`tests/conformance/entry12_…exsc` writes it: `examples/initium.exsc` writes
`Scriptor.ad_exitum(a)` and `s: Scriptor` as a parameter type, and spec
§8.6 decision 4 makes trait objects explicit `dyn Scriptor` — so under the
grammar as it stands, `s: Scriptor` *is* a concrete type. The two files
disagree (section 9, 9); the runnable one wins for Stage 3.

`Scriptor.ad_exitum(a)` — `exsrt_scriptor_ad_exitum(ret, a)` — stores `a`
at `ret+0` and `ExsAmbitus.out` (1) at `ret+8`. It cannot fail: spec §4.7
makes `ambitus` derivation total on a host that has it, and a closed
descriptor is discovered by the write, not the constructor.

`s.scribe(t)` — `exsrt_scriptor_scribe(s, t) -> u64`:

```
exsrt_scriptor_scribe:                  ; rdi = Scriptor*, rsi = textus*
        push    rbp
        mov     rbp, rsp
        mov     r8d, [rdi + 8]          ; descriptor
        mov     r9, [rsi + 0]           ; bytes
        mov     r10, [rsi + 8]          ; remaining
        mov     r11, r10                ; total requested
  .loop:
        test    r10, r10
        jz      .done
        mov     edi, r8d
        mov     rsi, r9
        mov     rdx, r10
        mov     eax, 1                  ; write -- immediate, adjacent to its syscall
        syscall
        cmp     rax, -4096
        ja      .err
        add     r9, rax                 ; partial write: advance and go again
        sub     r10, rax
        jmp     .loop
  .err:
        cmp     eax, -4                 ; -EINTR: retry, nothing was written
        je      .loop
  .done:
        mov     rax, r11
        sub     rax, r10                ; bytes actually written
        pop     rbp
        ret
```

Partial writes are looped; `EINTR` is retried; any other `-errno` ends the
loop and **the return value is the count written so far**, which is less
than `t`'s length exactly when something failed. That is all a `mensura`
can say. `scribe` should return `eventus<mensura>` (section 9, 10);
`EAGAIN` on a non-blocking descriptor would spin here and is `[OPEN]`;
`SIGPIPE` on a closed pipe kills the process under the ambient disposition
and the prelude does not change it (`rt_sigaction` is not on any
allowlist) `[OPEN]`. `initium.exsc` discards `scribe`'s result, so the
hello world exits 0 even when stdout is closed — a property of the
example, recorded rather than fixed.

**Where `Scriptor` lives, and what changes with `norma`.** Today: prelude
assembly with an interface the checker pre-seeds — there is no module
system, no import (spec §8.6 decision 5), and no `norma` source to compile,
so "Exsecutor source prepended by the driver" is not an option that exists.
When `norma` exists: `Scriptor` becomes `interfacies Scriptor { functio
scribe(s: Self, t: textus) -> mensura }` in `norma.exitus` `[OPEN]` name,
with `structura ScriptorExitus { a: ambitus, descriptor: i32 }` its
standard-stream impl and `ad_exitum` its constructor, and the prelude's
`exsrt_scriptor_scribe` becomes the body of one `externus`-free impl —
the assembly stays, the *declaration* moves from a table to source, and
`imprime.exsc`'s comment ("point it at a file-backed writer and it needs
the filesystem") becomes true, which today it is not (section 9, 9).
`examples/initium.exsc` would then read `ScriptorExitus.ad_exitum(a)` or
the example keeps `Scriptor` as the concrete name and the interface takes
another; that is the owner's, not this document's.

### 2.5 ARC in the prelude: the header, the two aborts, and abort itself

The hello world needs none of this — literals are views (section 2.3),
`Scriptor` is a 16-byte `slot`, `initium` derives a carrier and calls two
functions — and its emitted IR contains no `retain`, no `release`, no
`ref`. It is designed here anyway so that the emitter has one target and
`tests/conformance/entry15_refcount_saturation.exsc` has something to run
against. The ARC routines carry no syscall and are always assembled;
gating (section 2.6) is by syscall surface, not by code size.

**Object header**, at the start of every `refero<T>` / `refero_communis<T>`
allocation, payload at +16 (16-byte aligned, so an `acies<f64>` payload is
aligned for free):

```
ExsObj { rc u64 @0, dtor ptr @8 }       ; dtor = 0: no destructor; weak references [OPEN]
```

**Count semantics.** A live object has `rc ≥ 1`. **`rc = 0` means under
destruction**, which is what makes spec §6.6's resurrection detectable at
one compare. Spec §6.5's "saturating with hard abort" is read as: the
count never wraps, and the retain that would carry it past 2⁶⁴−1 aborts —
saturation is the *event*, not a state the program continues in (section
9, 5).

```
exsrt_retain:                           ; rdi = object (ref: non-atomic, spec §6.3 decision 2)
        cmp     qword [rdi], 0
        je      exsrt_abort_resurrectio ; retain of an object under destruction (spec §6.6)
        add     qword [rdi], 1
        jc      exsrt_abort_saturatio   ; carried out of 64 bits (spec §6.5)
        ret
exsrt_release:
        sub     qword [rdi], 1
        jc      exsrt_abort_resurrectio ; was already 0: released twice, or during its own destructor
        jz      .destroy
        ret
  .destroy:                             ; rc is now 0 == "under destruction" for the dtor's duration
        mov     rax, [rdi + 8]
        test    rax, rax
        jz      .nodtor
        push    rbp                     ; align for the call
        mov     rbp, rsp
        call    rax                     ; dtor(object), SysV; releases its fields itself (IR 2.8)
        pop     rbp
  .nodtor:
        ret                             ; memory is NOT freed -- see section 2.6
```

`exsrt_retain_c`/`exsrt_release_c` are the same with `lock xadd` and the
old value tested (old = 0 → resurrection; old = 2⁶⁴−1 → saturation). The
emitter lowers `retain %r` to `mov rdi, [slot]; call exsrt_retain`, choosing
the `_c` form by the value's type (`ref` vs `refc`, IR 2.3) — a request
to the backend agent (section 6). Destruction order within a scope is
AST 2.9's reverse declaration order; the prelude only dispatches.

**Abort.** One routine, one shape, for every runtime abort — numeric trap
(spec §5.4), saturation, resurrection, arena exhaustion, an unhonoured
`chk`:

```
exsrt_abort:                            ; edi = kind, small integer, permanent
        … write(2, "exsecutor: abortus ", 19)
        … write(2, decimal of edi, n)   ; digits only
        … write(2, "\n", 1)
        db      0x0F, 0x0B              ; ud2: SIGILL, status 132
```

| kind | meaning |
|---|---|
| 1 | numeric trap — trapping `+ - * /`, zero divisor, failed `chk` (spec §5.4; today `bfausr_trap`) |
| 2 | refcount saturation (spec §6.5) |
| 3 | destructor resurrection or double release (spec §6.6) |
| 4 | arena exhausted (section 2.6) |

Why `ud2` and not `exit_group(N)`: `initium` returns `u8`, so *every*
status 0–255 is a legitimate program result and none can mean "aborted";
a signal death is the only channel a parent can tell apart, and it is the
convention every trap in this tree already uses (`macros/assert.inc`,
`bfausr_trap`). Why a message at all: `tests/run.sh` is a shell and sees
132 for both `ud2` and `redde 132;`, so the discriminator §14 entry 15's
`shape=abort` needs is the line on fd 2. Why the kind is a number: spec
§8.3's rule — codes permanent, text not, tools match codes — applied to
the runtime; `abortus N` is the code, and the English before it is not
promised. Runtime aborts are not `EXS-E` diagnostics (IR 7, item 4) and no
§13 code is invented here; the kinds are a prelude enum until the spec
gives them a home (section 9, 6). **The abort message never contains
program data** — no source, no bytes from a `textus`, no address — so
§8.3's escaping rule holds vacuously rather than by an escaper the prelude
would have to carry. `exsrt_abort` clobbers nothing it needs: it never
returns.

The emitter's `bfausr_trap:` (`ud2` alone) should become
`mov edi, 1 / jmp exsrt_abort` so that a numeric trap says which kind it
was — a one-line request to the backend agent, and until it lands the two
are behaviourally identical (SIGILL, 132) minus the message.

### 2.6 Allocation, and the program's syscall surface

**`alloc` is an `mmap`-backed bump arena, fixed capacity, reset in O(1),
no `brk`, no libc** — `rt/arena.inc`'s design re-stated for the program
rather than reused (constraint 3: the prelude cannot include `rt/`):

```
ExsArena { base ptr @0, cur ptr @8, limit ptr @16 }
exsrt_alloc_novum(m: ptr, n: u64) -> ptr    ; mmap(0, n, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
exsrt_alloc_da(a: ptr, n: u64, align: u64) -> ptr   ; bump; exhaustion -> exsrt_abort 4
exsrt_alloc_reconde(a: ptr)                 ; cur = base (spec §6.3 decision 1)
exsrt_alloc_dimitte(a: ptr)                 ; munmap
```

A `refero<T>` is `exsrt_alloc_da` on the bound arena plus a header
(section 2.5). **Release-to-zero runs the destructor and reclaims
nothing**; the bytes return with `reconde`. This is spec §6.1's "ARC.
Arenas via `alloc`" taken literally: the count governs *when destruction
happens* (spec §6.6, deterministic and observable), the arena governs
*when memory returns*. A size-class free list that lets a released object's
bytes be reused is `[OPEN]`, and the `certus` profile
(`docs/design/profile-certus.md`, all arenas sized in `initium`, nothing
after) is satisfied by the design as it stands. Growth is `[UNIMPLEMENTED]`
for the same reasons `rt/arena.inc` gives. The derivation's surface
spelling — `m.alloc(n)`? a capacity in what unit? — is `[OPEN]`; spec
§4.5 says only that `sub alloc = a` binds an arena.

**The syscall surface is a function of the ego's `potestates`, and the
audit checks it on the binary.** Spec §18.1's mechanism — extract every
`syscall` site and its `rax` from the binary and diff against "the
declared allowlist" — is only a check on a *program* if "declared" means
something for a program. It does: the ego's `potestates`. Each atom
carries a fixed syscall set; the blob wraps each syscall-bearing routine
in `if EXS_POTESTAS_<ATOM>`; the binary's syscall set is therefore the
union over declared atoms plus the core, and `tools/syscall-audit.sh BIN`
can be told the atoms and compute the same union:

| atom | syscalls the prelude may use under it | in the hello world |
|---|---|---|
| core (always) | `exit_group(231)`, `write(1)` to fd 2 from `exsrt_abort` only | yes |
| `ambitus` | `write(1)`, `read(0)` | yes |
| `alloc` | `mmap(9)`, `munmap(11)` | no |
| `archivum` | `openat(257)`, `close(3)`, `fstat(5)`, `lseek(8)`, `read(0)`, `write(1)` | no |
| `horologium` | `clock_gettime(228)` `[OPEN]` | no |
| `fortuna` | `getrandom(318)` `[OPEN]` | no |
| `rete` | the socket family `[OPEN]` | no |
| `Filum`, `machina`, `sermo`, `Crudum` | `[OPEN]` | no |

Three consequences, stated so they are not misread. (a) **The hello
world's binary uses `write` and `exit_group` and nothing else**, a strict
subset of `exsc`'s nine, and passes today's audit unchanged. (b) A
compiled program is not `exsc`: **CLAUDE.md's "no socket-family syscall,
ever" is about the compiler and is not weakened here** — the compiler's
binary never contains one; a *program's* binary contains one **iff** its
ego declares `rete`, and the audit run with the ego's atoms proves the
*iff* in both directions, which is what makes spec §10.3's one-line diff a
claim about machine code. (c) `make audit` should run on every compiled
conformance module with its atoms, beside `proba-reproducibilitatem` — a
request to the tool's owner; the tool today has one fixed allowlist, so a
program with `horologium` fails it until it takes `--potestates`.
`clock_gettime` and the rest are added to a program's surface *only* by
the declaration, never by the prelude's presence — that is the whole
point of `if` in the blob rather than a monolithic prelude.

### 2.7 Spec §5.4 at program start: the MXCSR image

The entry stub executes `ldmxcsr` from `EXS_MXCSR`, a constant the driver
prints from **`initium`'s `Func.numeri`** (IR 2.6: every function carries
the module's four words; the entry stub has one function to read). Linux
initialises MXCSR to `0x1F80` on `execve`; the stub does not rely on that,
because relying on it is the ambient-state class spec §5.4 exists to
remove.

The image, bit by bit:

| bits | field | value | from |
|---|---|---|---|
| 0–5 | exception flags IE DE ZE OE UE PE | 0 | cleared at start |
| 6 | DAZ (denormals-are-zero) | **0** | `subnormales conservata` |
| 7–12 | exception masks IM DM ZM OM UM PM | **all 1** | floats do not trap in spec §5.4; only integer operators do. Unmasking would turn IEEE default results into SIGFPE `[OPEN]` if a trapping float mode is ever declared |
| 13–14 | RC | `00` nearest-even | `rotundatio ad_parem` |
| | | `01` toward −∞ | `ad_inferius` |
| | | `10` toward +∞ | `ad_superius` |
| | | `11` toward zero | no spec name (section 9, 12) |
| 15 | FTZ (flush-to-zero) | **0** | `subnormales conservata` |

So `ad_parem` + `conservata` = `0x1F80`; `ad_inferius` = `0x3F80`;
`ad_superius` = `0x5F80`. `reassociatio` and `contractio` have no runtime
bit: they are honoured by the emitter (one instruction per IR op, `fma`
only for `fma`) and need nothing here. The x87 control word is untouched:
the emitter issues SSE only, and an `externus` callee's x87 use is that
callee's `[OPEN]`. Re-asserting MXCSR after every `externus` return is
ADR 0012's open item and stays open; the hook is that the image is a named
constant a call-site epilogue can `ldmxcsr` again.

`subnormales` other than `conservata`, and a module whose `numeri` the
host cannot honour, are `EXS-E0701` at the driver (the code exists in
§13); on `x86_64-linux` every declared value in the table is honourable.

## 3. What the fixtures prove

There is no verifier for a prelude; there are exact bytes. Each routine in
section 2 has a fixture that assembles the blob **alone** — a
`tests/unit/*.asm` harness that `include`s `prelude.asm`, defines the
eleven constants and `EXS_MXCSR` itself, supplies a hand-written
`bfausr_initium`, runs, and is audited — before any emitted program
exists (section 6 names them). `emit.inc` was proven the same way against
hand-written IR. The claims below are `[UNTESTED]` until those run:

- `exsrt_scriptor_scribe` on a 101-byte view writes 101 bytes and returns
  101; on a 0-byte view issues no syscall and returns 0.
- `exsrt_retain` on `rc = 2⁶⁴−1` aborts with `abortus 2`; on `rc = 0` with
  `abortus 3`; `exsrt_release` from 1 calls the destructor exactly once
  with `rc = 0` during it, and a retain inside that destructor aborts 3.
- `stmxcsr` after the stub equals `EXS_MXCSR` for each of the three
  rounding images.
- The audit on each fixture binary reports exactly the union in section
  2.6's table for the constants set.
- A partial write (a pipe with a small buffer) is looped, and `EINTR` is
  retried — **no fixture can produce either without a second process**,
  which `tests/run.sh` does not have; `[UNTESTED]` and stated as such.

## 4. Determinism audit

| this content of `OUT` | is a function of |
|---|---|
| banner, `include`, `format`, `entry` | constants in the driver |
| eleven `EXS_POTESTAS_*` lines | the checker's computed closure of `initium`, in spec §4.6 order |
| `EXS_MXCSR` | `initium`'s `Func.numeri` |
| the prelude blob, the data blob | `exsc`'s own bytes (`file`'d at build) |
| function order | `Module.funcs` index order = declaration order across `SOURCE` files in command-line order (spec §12) |
| labels | `bfausr_` + the function's name; `bfausr_g` + global id; blob labels fixed |
| literal bytes | `Module.data` in global id order, hex |
| segment order | fixed: executable, readable, writeable |

What is deliberately absent: no path (not even remapped), no `--epoch`
(nothing in `OUT` is a timestamp), no host name, no `exsc` version string
(a version would be a function of `exsc`'s bytes and therefore admissible,
but `publish-gate.sh` step 5 diffs two runs of *one* `exsc`, and a banner
that changes per build adds nothing the content hash does not). No label
names an address; no order depends on a pointer or a hash bucket. A source
identifier outside ASCII would reach a label unmangled, and whether fasmg
accepts it is `[OPEN]` — a mangling to ASCII is the backend's (section 6);
the hello world's three names are ASCII.

## 5. The hello world, walked through

**IR the lowering produces** (IR 2.11 text form; `[UNTESTED]` — no
lowering exists, this is what section 2 obliges it to produce):

```
data $1 101 1 4176652C206D756E6475732E0A0A45782073696C656E74696F2073757267697420666F726D612E0A4578207369676E6F206E6173636974757220766F782E0A457820636F6469636520666974206C756D656E2E0A0A486F64696520696E636970696D75732E

functio @saluta (ptr) -> void numeri ad_parem vetita explicita conservata {
b0:
  %0 = param ptr 0                 ; hidden return storage: the caller's 16-byte textus
  %1 = gaddr 1
  store ptr %0 0 nativus %1
  %2 = iconst u64 101
  store u64 %0 8 nativus %2
  ret
}

functio @imprime_gutenbergio (ptr ptr) -> u64 numeri ad_parem vetita explicita conservata {
b0:
  %0 = param ptr 0                 ; s: Scriptor, by ptr (aggregate); its row {ambitus} rides in s.a
  %1 = param ptr 1                 ; t: textus, by ptr
  %2 = call u64 @exsrt_scriptor_scribe %0 %1
  ret %2
}

functio @initium (ptr) -> u8 numeri ad_parem vetita explicita conservata {
b0:
  %0 = param ptr 0                 ; m: Mundus -- the one carrier (IR 2.9 group 3; no declared params)
  %1 = call ptr @exsrt_mundus_ambitus %0      ; firma a = m.ambitus();  sub ambitus = a; -> nothing
  %2 = slot 16 8                   ; firma s: Scriptor
  call void @exsrt_scriptor_ad_exitum %2 %1
  %3 = slot 16 8                   ; temporary for saluta()
  call void @saluta %3
  %4 = call u64 @imprime_gutenbergio %2 %3    ; result discarded, as the source discards it
  %5 = iconst u8 0
  ret %5
}
```

`poscit sicut s` contributes **no hidden argument**: CHK 2.8 passes hidden
`ptr`s for a row's *atom* items and says a `sicut` item's carriers travel
inside that argument — here, in `Scriptor.a`. The substituted row
`{ambitus}` exists for the audit and the checker, not for the call.

**`OUT`, in outline** (section 2.1's layout, with what each piece is):

1. banner, `include 'format/format.inc'`, `format ELF64 executable 3`,
   `entry exsrt_start`;
2. `EXS_POTESTAS_MUNDUS = 1`, `EXS_POTESTAS_AMBITUS = 1`, nine zeros,
   `EXS_MXCSR = 0x1F80`;
3. `segment readable executable`; the blob: `exsrt_start` (section 2.2's
   stub), `exsrt_mundus_ambitus`, `exsrt_scriptor_ad_exitum`,
   `exsrt_scriptor_scribe`, the four ARC routines, `exsrt_abort`; every
   `alloc`/`archivum`/… routine present as text and assembled to nothing;
4. `bfausr_saluta` (frame 3 slots, two stores through `[rbp-8]`),
   `bfausr_imprime_gutenbergio` (loads its two slots into `rdi`/`rsi`,
   `call exsrt_scriptor_scribe`, stores `rax`, returns it),
   `bfausr_initium` (two `sub rsp`-carved 16-byte slots, three calls,
   `mov eax, 0`, epilogue), `bfausr_trap`;
5. `segment readable`; `bfausr_g1:` and 101 hex bytes;
6. `segment readable writeable`; the data blob.

**`fasmg OUT BIN && ./BIN`**, byte for byte: the kernel maps three
segments and jumps to `exsrt_start`; `rsp0` is recorded; `ldmxcsr 0x1F80`;
`bfausr_initium(&exsrt_mundus)`; `exsrt_mundus_ambitus` fills
`ExsAmbitus` with `{0, 1, 2, argc, argv, envp}` and returns its address;
`ad_exitum` writes `{&exsrt_ambitus, 1}` into `s`; `saluta` writes
`{&bfausr_g1, 101}` into the temporary; `imprime_gutenbergio` →
`scribe` → **one `write(1, &bfausr_g1, 101)`** — the whole literal in one
call on a terminal or a file; a pipe may split it and the loop finishes
it — then `exit_group(0)`. Stdout receives the 101 bytes of
`examples/saluta.expected`, `Ave, mundus.\n\nEx silentio … incipimus.`,
**no trailing newline**, because the literal has none and nothing in this
path adds one. Nothing is allocated, nothing is retained, no `mmap` is
issued, and the binary's syscall sites are `write` (twice: `scribe`, and
`exsrt_abort`'s unreached path) and `exit_group`.

`publish-gate.sh` then: step 3 `cmp` against the golden file passes; step
5 compiles twice under `TZ=Asia/Tokyo LC_ALL=tr_TR.UTF-8` from `/tmp` and
from the work directory with absolute source paths — `OUT` names none of
those, so the two are identical; step 6 is unchanged. `[UNTESTED]`: every
sentence of this section is what the design *obliges*; none of it has
run.

## 6. Agent split

Three agents, exclusive trees. Each fixture is an emitted or hand-written
program that must produce **exact bytes**, run under `tests/run.sh`'s
`; TEST:` directive, and pass the audit.

| agent | owns | depends on | non-vacuous fixture |
|---|---|---|---|
| `prelude` | `compiler/x86_64/prelude/` — `prelude.asm` (the blob), `prelude_data.asm`, `interface.inc` (the table of section 2.2, pre-seeded `Decl`s for `checker/resolve/`), `README.md` stating the `exsrt_` ABI | `vendor/fasmg-x86/` only; **no `rt/`, no `macros/`** | `tests/unit/prelude_scribe.asm`: harness + blob + a hand-written `bfausr_initium` that calls `ad_exitum` and `scribe` on a 101-byte `db` literal and `ret`s the count — `expect-exit=101 audit=pass`; `prelude_arc.asm`: fast-forward `rc` to `2⁶⁴−1`, one retain — `expect-exit=132`, `abortus 2` on stderr; `prelude_mxcsr.asm`: `stmxcsr` after the stub under each of three images, `expect-exit=0` |
| `backend_fasmg` (requests — that agent's tree, listed, not done here) | `backend_fasmg/emit.inc`: Tier 2 — `slot`, `gaddr`, `load`/`store` at `ptr`/`u64`, `iconst`/`ret` at `u8`, `call` (SysV: `rdi rsi rdx rcx r8 r9`, seventh-plus argument `[OPEN]`), `retain`/`release` → `call exsrt_retain[_c]`/`exsrt_release[_c]` by type, `bfausr_trap` → `mov edi, 1; jmp exsrt_abort`; **new `backend_fasmg/program.inc`**: `bfa_emit_program(module, arena, potestates, mxcsr) -> ptr, len` producing all of section 2.1's `OUT` around `bfa_emit_module`'s text — header lines, constants, the two `file`'d blobs, the data segment in global id order as hex; non-ASCII label mangling | IR parser (has every op); the blobs as `file`'d data | `tests/unit/bfa_emit_program.asm`: the three IR functions of section 5, hand-written, through `bfa_emit_program`, compared to an exact expected text (regression, like `bfa_emit_tier1.asm`); the round trip through `fasmg` and a run is done by `publish-gate.sh`, not by the fixture — `tests/run.sh` cannot `execve` |
| `driver` (requests) | `driver/run.inc`, `io.inc`: on `-o OUT`, after the checker: lower `[UNIMPLEMENTED]`, call `bfa_emit_program`, `openat(O_WRONLY\|O_CREAT\|O_TRUNC, 0644)`, write loop, `close`; `--hospes` other than `x86_64-linux` → refusal, exit 4, no code invented; a `numeri` the host cannot honour → `EXS-E0701` | backend's `program.inc`, checker's closure | `tests/unit/driver_out.asm`: a hand-built module through the same path, `OUT` bytes compared exactly; and the existing `driver_*` fixtures still green |

Order: prelude first (its fixtures need nothing else), backend
`program.inc` against the `file`'d blobs, driver last. The prelude agent
also owns the request to `docs/asm-conventions.md` §6 (section 9, 14) and
the `tools/syscall-audit.sh --potestates` request (section 2.6),
neither of which it may write itself.

## 7. Hazards

- **H1 the blob is assembled inside someone else's segment.** It must open
  no segment, define no `format`, and use no name outside `exsrt_`. A
  stray `segment` or an unprefixed label breaks every emitted program at
  once and names neither the blob nor the program (asm-conventions §4.1's
  lesson, now across two binaries).
- **H2 `if` around a syscall is the audit's contract.** A syscall-bearing
  routine added outside its `if EXS_POTESTAS_…` silently widens every
  program's surface; the fixture in section 3 that diffs the audit against
  section 2.6's table is what catches it, and it must be kept per atom.
- **H3 the arena reclaims nothing on release.** A long-running program
  that allocates `refero`s in a loop without `reconde` grows without
  bound although every count reaches zero. Documented; not a leak in spec
  §6.7's sense (destructors run); a free list is `[OPEN]`.
- **H4 two copies of the same layout.** `ExsAmbitus`, `Scriptor` and
  `ExsObj` offsets appear in the blob and in `interface.inc`. The emitter
  never sees them (section 2.2), so the pair is the only place they can
  disagree, and the `prelude_scribe` fixture reads `descriptor` through
  the interface's stated offset to check it.
- **H5 `rc = 0` is overloaded** as "under destruction" and as the
  double-release detector. A future "immortal" object (a static `refero`,
  which spec §4.1 rule 7 forbids today) would need a third state and would
  reopen §6.5's semantics; do not reuse 0 for it.
- **H6 a user function named `exsrt_…`** would be emitted as
  `bfausr_exsrt_…` and collide with a blob label at assembly time, naming
  neither file. The pre-seeded `Decl`s of `interface.inc` make the
  IR-callable names a redefinition at the checker (class B, CHK 2.4); the
  private `exsrt_` names have no `Decl` and the collision is fasmg's to
  report `[OPEN]` — reserve the prefix in the checker, or mangle.

## 8. Unresolved

The eight atoms without a routine and their records; `m.alloc(n)`'s
spelling and unit; `scribe`'s `eventus`; `EAGAIN`; `SIGPIPE`; weak
references and `dtor` conventions for fields; a size-class free list; the
seventh SysV argument in emitted `call`s; label mangling for non-ASCII
names; `grapha`/`scalares` and the Unicode tables inside a program; MXCSR
after `externus`; a trapping-float mode; toward-zero rounding's name; the
library artifact for a module without `initium`; where the abort kinds
live in the spec; `--potestates` on the audit tool; whether the banner
should exist at all.

## 9. Findings against the spec

Numbered; each names the section and the sentence. Not edited here.

1. **§4.7 "Derivation is by method on the root — `m.ambitus()`,
   `m.archivum()`."** No declaration site for `Mundus`, `ambitus` or a
   method exists in §8.6 (no receiver, no `Self`, no member declaration —
   CHK finding 6), so the prelude's interface cannot be written in
   Exsecutor and is a checker-side table (section 2.2). The spec should
   either give the prelude a source form or say that the prelude interface
   is pre-seeded.
2. **§4.7 "A module with no `initium` is a library, and `aedifica` on one
   yields a library artifact."** §12 defines `OUT` as self-contained fasmg
   source for an executable; no library artifact format exists for the
   reference backend, and `fasmg` has no link step. Until one is designed
   a module without `initium` is refused with `-o` (no code; CHK class M).
3. **§12 "The emitted text is self-contained."** It contains
   `include 'format/format.inc'` and depends on `vendor/fasmg-x86/` being
   on the include path — the same sentence says so two clauses later. The
   closure is `{fasmg} + the vendored package`, as §18.1 already states;
   "self-contained" should say "modulo the vendored macro package".
4. **§5.1 says "UTF-8 storage. Slicing by byte offset, O(1), returns
   `eventus`"** and nowhere says whether `textus` is a value or a
   reference type. O(1) slicing forces a view; §6.3 decision 4 covers only
   `structura`. This document decides value (section 2.3); the spec should
   state it, and state that a `textus` may not outlive its arena.
5. **§6.5 "saturating with hard abort."** Saturating (clamp and continue)
   and hard abort (stop) are different behaviours; the sentence names
   both. Read here as "abort at the retain that would exceed 2⁶⁴−1"
   (section 2.5). The spec should pick one verb.
6. **§6.5, §6.6 name aborts and give them no observable shape** — no
   status, no channel, no message — while §14 entry 15's `shape=abort`
   needs one to be checkable. Section 2.5 fixes SIGILL plus `abortus N`
   on fd 2 with a permanent small-integer kind; the spec should own that
   enum the way §13 owns codes, without making them `EXS-E` codes (IR 7,
   item 4 is right that runtime traps are not diagnostics).
7. **§9.3 "Runtime: calls `setlocale(LC_ALL, "C")` at startup."** The
   reference runtime has no libc and no `setlocale` to call; the sentence
   is true of the C backend only. The spec is stale here, not the design:
   it predates §9.2's two-backend amendment and should say "the C target's
   runtime".
8. **§10.1/§10.3 make `potestates` an ego property, and §12's minimum
   invocation names no ego.** The gate compiles three files with no
   `ego.exsc`; section 2.1's eleven constants therefore come from the
   checker's computed closure of `initium`, not from a parsed ego, and
   `numeri` from §5.4's defaults. §12 should say what an ego-less build
   means, or the gate needs an ego.
9. **`examples/imprime.exsc` and `examples/README.md` ("Point it at a
   file-backed writer and it needs the filesystem") describe a
   polymorphic `Scriptor`; `tests/conformance/entry12_…exsc` declares
   `interfacies Scriptor`; `examples/initium.exsc` writes `s: Scriptor`
   and `Scriptor.ad_exitum(a)`, which under §8.6 decision 4 is a concrete
   type.** They cannot all be right. Section 2.4 takes the runnable file's
   reading; the comment is false for Stage 3 and entry 12 names a type
   the hello world's prelude makes a struct. One of them changes when
   `norma` exists.
10. **§4.6/§4.7 make `ambitus` derivation total, and `Scriptor.scribe`
    returns `mensura`** (examples, entry 12). A closed fd 1 is discovered
    at the write, which a count cannot report except by being short.
    `scribe` should return `eventus<mensura>`; §11 has no principle for
    I/O failure reporting.
11. **§5.4's rounding table names `ad_parem`, `ad_superius`, `ad_inferius`
    and no toward-zero mode**, which MXCSR has (`RC = 11`) and which
    `ftoi`'s truncation semantics (IR 2.3, `[OPEN]`) would want a name for.
12. **§18.1 "extract every `syscall` site … and diff against the declared
    allowlist"** is stated for `exsc`; for a compiled program "declared"
    must mean the ego's `potestates` (section 2.6), and §10.3 should say
    that its audit is checked on the binary by exactly that diff.
13. **§9.3 "Deterministic symbol emission"** meets identifiers that §8.2
    admits outside ASCII; whether `fasmg` accepts such a label is
    `[OPEN]`, and a deterministic ASCII mangling belongs in the backend's
    contract, which no spec section currently requires.
14. **`docs/asm-conventions.md` §6 "The ONLY file … permitted to emit a raw
    `syscall` instruction" is `rt/sys.inc`.** The prelude cannot include
    `rt/` (constraint 3) and must issue syscalls; it becomes the second
    closed set in the repo — one for `exsc`'s binary, one for the
    program's — each audited on its own binary. Not a spec finding; a
    conventions amendment the prelude agent must request before writing
    `prelude.asm`.
