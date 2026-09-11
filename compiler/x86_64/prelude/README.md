# `compiler/x86_64/prelude/` — the runtime prelude

The executable text every compiled Exsecutor program carries, plus the table
the checker resolves its names against.

Design: `docs/design/runtime.md` sections 2.1–2.7 (decisions), 3 (fixtures),
6 (the agent split), 7 (hazards). Spec sections cited bare below are
`docs/spec/exsecutor-spec-v0.4.md`.

| file | what it is | who reads it |
|---|---|---|
| `prelude.asm` | the blob: entry stub, carriers, `Scriptor`, ARC, arena, abort | `exsc` carries it as data (`file`) and copies it verbatim into `OUT` |
| `prelude_data.asm` | the blob's mutable state, a second verbatim blob | ditto, written after the data segment |
| `interface.inc` | the pre-seeded `Decl` table and the layout facts | `checker/resolve/`, and every `tests/unit/prelude_*.asm` |
| `README.md` | this file | you |

Only the first two are emitted. `interface.inc` is `include`d into `exsc`
itself and never reaches `OUT`.

## Status

`prelude.asm` and `prelude_data.asm` assemble and run, under eleven fixtures
(below). `interface.inc` part A — the layout constants — is checked against
the blob every time both are assembled together. `interface.inc` part B — the
serialized `Decl` rows — is `[UNTESTED]`: `checker/resolve/` does not exist,
nothing has consumed it, and when the checker lands its own `Decl` record
wins.

## What a wrapper must define before including the blob

`prelude.asm` opens no segment and defines no `format`, no `entry` and no
`include` (runtime.md H1). The wrapper — `backend_fasmg/program.inc` for a
compiled program, a `tests/unit/prelude_*.asm` harness for a fixture — must:

1. `include 'format/format.inc'`, `format ELF64 executable 3`,
   `entry exsrt_start`;
2. define twelve constants;
3. open `segment readable executable` and `include` `prelude.asm` into it;
4. append the module (`bfausr_*`), which must contain `bfausr_initium`;
5. open `segment readable` for the literals (`bfausr_g*`);
6. open `segment readable writeable` and `include` `prelude_data.asm`.

The twelve constants, in spec 4.6's order, each 0 or 1:

```
EXS_POTESTAS_MUNDUS  EXS_POTESTAS_ALLOC     EXS_POTESTAS_SERMO
EXS_POTESTAS_HOROLOGIUM  EXS_POTESTAS_ARCHIVUM  EXS_POTESTAS_RETE
EXS_POTESTAS_FORTUNA  EXS_POTESTAS_AMBITUS  EXS_POTESTAS_FILUM
EXS_POTESTAS_MACHINA  EXS_POTESTAS_CRUDUM
EXS_MXCSR
```

They come from the checker's computed capability closure of `initium` and
from `initium`'s `Func.numeri` — never from a path, a clock or a host (spec
9.3; spec 12 as amended settles the ego-less case). A missing one is a fasmg
error at the top of the blob, not a silently absent routine.

`EXS_MXCSR` is runtime.md 2.7's image: `0x1F80` for `rotundatio ad_parem` with
`subnormales conservata`, `0x3F80` for `ad_inferius`, `0x5F80` for
`ad_superius`. Toward-zero (RC = 11) has no spec 5.4 name (runtime.md finding
11) and therefore no image here.

**Order matters when including `interface.inc` in a fixture:** include it
*after* `prelude.asm`. Its H4 cross-asserts are guarded by
`if defined EXS_SCRIPTOR_A`; included first, they are silently skipped and the
check reports green while seeing nothing.

## The `exsrt_` ABI

Plain SysV AMD64, hand-rolled prologues. **Not** `exsc`'s internal
convention: no `macros/proc.inc`, no `CF`/`eax` error protocol, no `r15` pin
(`docs/asm-conventions.md` 1.2 pins `r15` for `exsc`'s own internals; a
compiled program is not `exsc`'s internals). Scratch is restricted to the SysV
caller-saved set — `rax rcx rdx rsi rdi r8`–`r11` — so these routines are
callable from `backend_fasmg/emit.inc`'s output, which preserves nothing
across a call. `rbp` is pushed and popped where a frame is wanted; nothing
else callee-saved is touched.

**The Linux `syscall` instruction destroys `rax`, `rcx` and `r11`** and
preserves everything else, argument registers included. No routine here holds
a live value in `rcx` or `r11` across a syscall. This is not a style rule: it
is the defect the first run of `tests/unit/prelude_scribe.asm` found in
runtime.md 2.4's own published `scribe` — see "Defect found", below.

### Two prefixes, and the difference is the ABI

`bfausr_exsrt_<x>` — IR-callable. `emit.inc` prints every IR `@name` as
`bfausr_<name>`, so an IR `call @exsrt_scriptor_scribe` assembles against a
label carrying the emitter's prefix. **A routine carries this prefix if and
only if `interface.inc` declares it**, which keeps runtime.md H6's collision
protection exactly coextensive with the prefixed set.

`exsrt_<x>` — private, or reached by a label the emitter *hardcodes* rather
than resolving from IR.

### Every name

IR-callable (`bfausr_exsrt_…`), with the IR signature the lowering must emit:

| symbol | IR signature | notes |
|---|---|---|
| `exsrt_mundus_ambitus` | `(ptr) -> ptr` | `m.ambitus()`. Total (spec 4.7), idempotent, no `eventus` |
| `exsrt_scriptor_ad_exitum` | `(ptr ptr) -> void` | hidden return `ptr` first (IR 2.9), then `a`. Cannot fail |
| `exsrt_scriptor_scribe` | `(ptr ptr) -> u64` | both aggregates by `ptr`; returns the count. `[OPEN]`: spec 11 as amended wants `eventus<mensura>` |
| `exsrt_scriptor_scribe_octetum` | `(ptr u8) -> u64` | `s.scribe_octetum(b)`: ONE raw byte, `b`'s low 8 bits (wire-codec.md D7). Returns the count, 1 or 0 -- `scribe`'s convention and `scribe`'s `[OPEN]` |
| `exsrt_alloc_novum` | `(ptr u64) -> ptr` | `(Mundus, capacity) -> ExsArena*`. `[OPEN]` surface spelling |
| `exsrt_alloc_da` | `(ptr u64 u64) -> ptr` | `(arena, n, align)`. `align` is a precondition: a power of two, at least 1 |
| `exsrt_alloc_reconde` | `(ptr) -> void` | `cur = base` (spec 6.3 decision 1) |
| `exsrt_alloc_dimitte` | `(ptr) -> void` | `munmap` the whole mapping, record included |

Private, or reached by a hardcoded label:

| symbol | reached from | notes |
|---|---|---|
| `exsrt_start` | the ELF entry point | must be the first routine in the blob |
| `exsrt_retain` `exsrt_release` | `retain`/`release` on a `ref` (IR 2.8) | non-atomic; `mov rdi, [slot]; call exsrt_retain` |
| `exsrt_retain_c` `exsrt_release_c` | ditto on a `refc` | `lock xadd`, old value tested |
| `exsrt_abort` | every abort | `edi` = kind; never returns |
| `exsrt_abort_saturatio` `exsrt_abort_resurrectio` `exsrt_abort_arena` `exsrt_abort_terminus` | kinds 2, 3, 4, 5 | one-instruction entry points |

Data labels, in `prelude_data.asm`: `exsrt_mundus`, `exsrt_ambitus`,
`exsrt_abortus_linea`, `exsrt_abortus_numerus`, `exsrt_abortus_calc`.

`interface.inc` under `EXS_IFACE_EMIT_DECLS`: `exsrt_iface_nomina`,
`exsrt_iface_signa`, `exsrt_iface_decls`.

### Record layouts

Stated once in `prelude.asm` as named constants, mirrored once in
`interface.inc`, and cross-asserted whenever both are assembled together —
which is runtime.md H4 made checkable rather than merely documented.

```
ExsMundus  { rsp0 ptr @0 }                                     8 bytes
ExsAmbitus { in i32 @0, out i32 @4, err i32 @8,
             argc u64 @16, argv ptr @24, envp ptr @32 }       40 bytes
Scriptor   { a ambitus @0, descriptor i32 @8 }                16 bytes, align 8
textus     { ptr @0, len u64 @8 }                             16 bytes, a VALUE view
ExsObj     { rc u64 @0, dtor ptr @8 }, payload at +16
ExsArena   { base ptr @0, cur ptr @8, limit ptr @16 }         24 bytes
```

`ExsArena` lives at the head of its own `mmap`, with the bump region starting
at +32 (so the first payload byte is 16-byte aligned). `dimitte` recovers the
mapping length as `limit - record`. Nothing is statically allocated for an
arena, so `m.alloc(n)` is reentrant.

`rc == 0` means **under destruction** — that is what makes spec 6.6's
resurrection detectable at one compare. runtime.md H5: do not reuse 0 for a
future "immortal" object.

### Abort kinds

One shape for every runtime abort: the line `exsecutor: abortus N` on fd 2,
then `ud2` → `SIGILL` → the shell sees 132. Spec 6.6 as amended owns the
shape; runtime.md 2.5 owns the enum. **These are not `EXS-E` codes** — a trap
is not a diagnostic — and no section 13 code is invented for them.

| kind | meaning | spec |
|---|---|---|
| 1 | numeric trap: trapping `+ - * /`, zero divisor, failed `chk` | 5.4 |
| 2 | refcount saturation — the retain that would carry out of 64 bits | 6.5 |
| 3 | destructor resurrection, or double release | 6.6 |
| 4 | arena exhausted | runtime.md 2.6 |
| 5 | `dum … terminus N` tried to enter its N+1th iteration | 8.5 |

Kind 5 was added at the request of `docs/design/lowering.md` (finding 2),
which records that kinds 1–4 had no row for it and that `trap terminus` was
folding into kind 1 — a numeric trap, which it is not.

The message **never contains program data** — no source, no bytes from a
`textus`, no address — so spec 8.3's escaping rule holds vacuously rather than
by an escaper the blob would have to carry. The number is the code; the
English before it is not promised.

## The syscall table, per atom

This directory is the **second closed syscall set** in the repository
(`docs/asm-conventions.md` section 6, as amended). The blob cannot include
`rt/` (runtime.md constraint 3), so it issues `syscall` directly, and every
syscall-bearing routine sits inside `if EXS_POTESTAS_<atom>`. The *text* of
`OUT` therefore always holds the whole prelude and the *binary* holds only
what the program's capability closure admits — which is what makes spec 10.3's
audit a property of the artifact.

| atom | syscalls | routines |
|---|---|---|
| core (always) | `exit_group(231)`; `write(1)` to fd 2 | `exsrt_start`, `exsrt_abort` |
| `ambitus` | `write(1)`, `read(0)` | `exsrt_scriptor_scribe`, `exsrt_scriptor_scribe_octetum` (`read` is `[UNIMPLEMENTED]` — no reader exists yet) |
| `alloc` | `mmap(9)`, `munmap(11)` | `exsrt_alloc_novum`, `exsrt_alloc_dimitte` |
| `archivum` `horologium` `fortuna` `rete` `Filum` `machina` `sermo` `Crudum` | `[OPEN]` | none |

Measured, not asserted — `tools/syscall-audit.sh` on each fixture binary:

| fixture | atoms | syscall sites in the binary |
|---|---|---|
| `prelude_scribe` | Mundus, ambitus | `exit_group`, `write` ×3 (`scribe`, `scribe_octetum`, and `exsrt_abort`'s unreached path) |
| `prelude_scribe_octetum` | Mundus, ambitus | the same three, plus the fixture's OWN `lseek` -- its instrument for stdout's offset, not a prelude syscall |
| `prelude_sine_ambitus` | Mundus | `exit_group`, `write` (abort only) |
| `prelude_arc`, `prelude_arc_resurrectio`, `prelude_abortus_terminus`, `prelude_mxcsr*` | Mundus | `exit_group`, `write` (abort only) |
| `prelude_arena`, `prelude_arena_exhausta` | Mundus, alloc | `exit_group`, `mmap`, `munmap`, `write` (abort only) |

The gating is therefore checked in both directions: `scribe`'s `write`
disappears when `ambitus` is 0, and `mmap`/`munmap` appear only when `alloc`
is 1. That is runtime.md H2 kept per atom. The `ambitus` half is no longer
only measured: `prelude_sine_ambitus` assembles the blob with the atom at 0
and fails to assemble if any `ambitus` routine or record is still defined.
(The audit alone cannot catch that in the unit phase, which checks against
the compiler's nine, `write` included.)

**No socket-family syscall appears here, and none ever will without a `rete`
routine.** CLAUDE.md's "no socket-family syscall, ever" binds `exsc`'s own
binary absolutely and is not weakened by this directory: a *program* has one
if and only if its closure contains `rete`, and the audit run with the
program's atoms proves the *iff* in both directions.

Every syscall site loads `rax` with an **immediate** in the instruction
immediately before its `syscall`. That is not style: it is the one form
`tools/syscall-audit.sh`'s linear sweep resolves, and a syscall it cannot
resolve is a *failure*, not a skip.

## The fixtures

Eleven, all under `tests/unit/`, all discovered and run by `tests/run.sh` from
their `; TEST:` directive. Each assembles the blob **alone**, with a
hand-written `bfausr_initium` in place of an emitted one — the way
`emit.inc` was proven against hand-written IR before a lowering existed.

| fixture | atoms | expects | proves |
|---|---|---|---|
| `prelude_scribe.asm` | Mundus, ambitus | `exit=101`, `audit=pass` | the entry stub, `m.ambitus()`, `Scriptor.ad_exitum`, `scribe` writing 101 bytes and **returning 101**; the literal byte-identical to `examples/saluta.expected`; `Scriptor.descriptor` read through `interface.inc`'s own offset (H4) |
| `prelude_scribe_octetum.asm` | Mundus, ambitus | `exit=7`, `audit=pass` | `scribe_octetum` writes EXACTLY one byte per call -- stdout's file offset, read back with `lseek`, moves by one each time -- and returns 1, for 0x00 0x7f 0xff 0x80 0xd3 and two arguments with garbage above bit 7; returns 0 and writes nothing on a descriptor the kernel refuses; touches no callee-saved register. The byte VALUES are `tests/programs/octeti`'s, compared with `cmp` |
| `prelude_sine_ambitus.asm` | Mundus | `exit=0` | with `ambitus` at 0, none of the four `ambitus` routines nor `exsrt_ambitus` is assembled -- an assembly-time check, so a routine moved out of the gate fails here by name |
| `prelude_arc.asm` | Mundus | `exit=132`, `abortus 2` | retain/release, the destructor running exactly once with `rc == 0`, the atomic pair, then saturation at 2⁶⁴−1 |
| `prelude_arc_resurrectio.asm` | Mundus | `exit=132`, `abortus 3` | a retain from inside the destructor aborts |
| `prelude_arena.asm` | Mundus, alloc | `exit=0`, `audit=pass` | `novum`/`da`/`reconde`/`dimitte`, alignment, and a mapping that is really readable and writeable |
| `prelude_arena_exhausta.asm` | Mundus, alloc | `exit=132`, `abortus 4` | the bump past `limit` aborts rather than returning |
| `prelude_abortus_terminus.asm` | Mundus | `exit=132`, `abortus 5` | kind 5 is distinct from kind 1 and reaches fd 2 |
| `prelude_mxcsr.asm` | Mundus | `exit=0` | `stmxcsr` after the stub is `0x1F80` — `ad_parem`, `conservata` — checked field by field |
| `prelude_mxcsr_inferius.asm` | Mundus | `exit=0` | the same for `0x3F80`, `ad_inferius` |
| `prelude_mxcsr_superius.asm` | Mundus | `exit=0` | the same for `0x5F80`, `ad_superius` |

Three MXCSR fixtures rather than one because `EXS_MXCSR` is a single constant
and the stub runs once: one binary can prove one image, and re-executing the
stub's two instructions with a different constant would test the instructions
rather than the stub.

Run them:

```sh
nix develop --command bash -c 'tests/run.sh'          # all of tests/unit/
```

or one at a time:

```sh
INCLUDE=vendor/fasmg-x86 fasmg tests/unit/prelude_scribe.asm /tmp/scribe
chmod +x /tmp/scribe && /tmp/scribe; echo $?          # 101
tools/syscall-audit.sh /tmp/scribe                    # write, write, exit_group
```

The `abortus N` line is on fd 2; `tests/run.sh` compares an exit status and
nothing else, so it sees 132 for `ud2` and for `redde 132;` alike. That is
precisely why the line exists, and checking it is spec 14 entry 15's
`shape=abort`, in the conformance suite, not here.

## Defect found

The first run of `prelude_scribe.asm` wrote all 101 bytes correctly and
**exited 6**. runtime.md 2.4 publishes `exsrt_scriptor_scribe` with the total
request held in `r11` across the `write` syscall; the x86-64 Linux syscall ABI
destroys `rax`, `rcx` and `r11`, so `r11` came back holding the saved RFLAGS
and the routine returned that instead of a byte count. The total lives in the
routine's frame here instead. **The design is wrong and this code is right**;
`bfausr_exsrt_alloc_novum` had the same shape and carries its length in `rsi`
(also `mmap`'s `len`, and preserved) for the same reason. An amendment to
runtime.md 2.4's code block is requested in the prelude agent's report.

## What is not here

`[UNIMPLEMENTED]`: readers on `ambitus` (`read(0)` is in the atom's table and
has no routine); `scalares`/`grapha` on `textus`, which need the Unicode
tables inside the program (spec 11's content-addressed dependency); arena
growth; a size-class free list, so a released object's bytes come back only
with `reconde` (runtime.md H3).

`[UNTESTED]`: `scribe`'s partial-write loop and its `EINTR` retry, and
`scribe_octetum`'s `EINTR` and zero-return retries. None can be produced
without a second process, which `tests/run.sh` does not have.
runtime.md 3 says so and this file repeats it rather than quietly implying
coverage.

`[OPEN]`: the eight atoms with no routine and their records; `m.alloc(n)`'s
spelling and unit; `scribe`'s and `scribe_octetum`'s `eventus<mensura>`; `EAGAIN` on a non-blocking
descriptor; `SIGPIPE`, which kills the process under the ambient disposition
because `rt_sigaction` is on no allowlist; weak references and `dtor`
conventions for fields; re-asserting MXCSR after an `externus` return
(ADR 0012's open item — the hook is that the image is a named constant a
call-site epilogue can `ldmxcsr` again); reserving the `exsrt_` prefix at the
checker (runtime.md H6).

`[OPEN]` **licensing.** `prelude.asm` and `prelude_data.asm` are Form 1
(plain GPL) because `docs/asm-conventions.md` section 4 says of Form 2 "no
such file exists yet, and none is yours to designate". They are exactly the
class `LICENSE.EXCEPTION` B3 reserves for designation — reference-counting
support the compiler emits into a compiled program — and their headers travel
verbatim into every `OUT`. Whether they should carry the Form 2 designation
line is the owner of `LICENSE.EXCEPTION`'s decision, requested in the prelude
agent's report and deliberately not made here.
