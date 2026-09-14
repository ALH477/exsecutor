# Assembly conventions

**Status:** Binding, per CLAUDE.md ("The macro dialect is frozen after wave
1... Conventions are in `docs/asm-conventions.md` and are binding."). Last
revised 2026-09-09.

This document governs everything under `compiler/x86_64/` and, where a rule is
not architecture-specific, the eventual `compiler/aarch64/` and
`compiler/riscv64/` trees. It is a normative reference, not a tutorial: it
says what the rules are and why, not how to write assembly.

Everything here is a binding rule, and as of wave 1 every rule in section 3 describes
code that exists and passes: `compiler/x86_64/macros/` and
`compiler/x86_64/rt/` are 2,827 lines under 25 fixtures. Narrower
`[UNIMPLEMENTED]` markers remain on specific gaps (unsigned comparisons,
nesting past six levels, a seventh stack-passed argument) and mean what they
say.

An earlier revision of this document proposed syntax for section 3 that had never
been assembled. Three separate proposals turned out to be wrong, two of them
producing silent memory corruption rather than an error. Where that happened
it is recorded in place rather than quietly overwritten — prose describing
unwritten code is a hypothesis, and section 3 is where this document learned it.

> **Citing sections.** A bare `§N` anywhere in this repository means the
> *spec*, and `tools/spec-check.sh` resolves it there. This document refers to
> its own sections by name in prose — *"Error protocol: `CF` / `eax`," above* —
> and writes `spec §9.3` when it means the spec. Using a bare section mark for
> a section of *this* file is how a dangling citation gets introduced, and the
> check caught exactly that during this revision.
>
> Two further notes, both learned the same way. Most such mistakes are *not*
> caught, because a self-reference like the register-discipline or macro-dialect
> section number happens to resolve to an unrelated spec heading — it is wrong
> and silent. And a section mark inside backticks is still a citation to a
> grep, so this paragraph deliberately describes the mistake rather than
> showing it.

---

## 1. Calling convention

**Internal calls use SysV AMD64**, unmodified, with two additions layered on
top. This is a reversal of an earlier plan and is documented as one.

> **Reversal.** An earlier draft specified: *"internal calls use a custom
> register-heavy convention; SysV shims only at boundaries that must be
> callable from C."* That plan is superseded. The decision is now the
> opposite: stock SysV AMD64 everywhere, including purely internal calls that
> never cross an `externus` boundary. Reasons:
>
> 1. **Debuggability dominates.** gdb, objdump, and perf all understand SysV.
>    A compiler written entirely in assembly is already harder to debug than
>    one written in a host language with source-level tooling (see ADR 0002,
>    Consequences — "debugging degrades further"); a custom convention on top
>    of that forfeits every off-the-shelf tool that could claw some of that
>    back. This is the dominant practical cost and the one that decided it.
> 2. **§5.3 requires implementing the C ABI anyway.** `externus("C", abi:
>    sysv_amd64)` (§5.3) means the frontend implements SysV AMD64 regardless
>    of what internal calls use. A second, different internal convention does
>    not save that work — it duplicates it, once for the boundary and once
>    for everything inside it.
> 3. **§9.2's eventual C-backend interop is free.** A C backend (§9.2, Stage
>    3) generating calls into or out of hand-written runtime routines does not
>    need a translation layer if both sides already agree on SysV.
>
> If you find design notes elsewhere describing a custom internal convention,
> they predate this decision and are wrong. This document is the current
> answer.

### 1.1 Register roles

Standard SysV AMD64, restated precisely because the additions below depend on
exactly which registers are callee-saved:

| role | registers |
|---|---|
| integer / pointer arguments, 1–6 | `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` |
| additional arguments (7th onward) | stack, pushed right-to-left, 8-byte slots |
| vector / float arguments | `xmm0`–`xmm7` |
| integer / pointer return | `rax` (`rdx:rax` for 128-bit) |
| vector / float return | `xmm0` (`xmm1:xmm0` for 128-bit) |
| **caller-saved** (a call may clobber these; save first if you need the value after) | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`, `r9`, `r10`, `r11` |
| **callee-saved** (a call must return these unchanged) | `rbx`, `rbp`, `r12`, `r13`, `r14`, `r15`, `rsp` |
| stack pointer | `rsp` — 16-byte aligned immediately before every `call` |

### 1.2 The `r15` pin

**`r15` is pinned as the compilation-context pointer** — arena, interner,
diagnostic sink — across every internal call. It is established once, at
process entry, before control reaches any internal routine, and from that
point on any internal routine may dereference it without receiving it as an
argument.

**This changes nothing in the table above.** `r15` is already callee-saved
under stock SysV; that row does not move. The pin adds a *usage* rule on top
of an *unchanged* ABI rule: within code that participates in internal calls,
`r15` is never repurposed as a scratch register and never used as a seventh
argument slot. Nothing about argument passing, return values, or the
caller/callee-saved partition is different from plain SysV AMD64. A reader who
already knows SysV AMD64 needs exactly one additional fact to read this
codebase: `r15` is always the context pointer, never anything else.

One consequence worth stating explicitly: because the pin rides on a
callee-saved register rather than a new one, it is **safe across the
`externus` boundary for free**. A foreign SysV-conforming C function does not
know or care that `r15` means anything special here — but stock SysV already
obligates it to preserve `r15` across the call, so it does, and the context
pointer survives a round trip through foreign code with no extra save/restore
code at the call site. If that C function itself calls back into Exsecutor
code, `r15` is exactly where it left it.

If the compiler ever becomes multi-threaded, each thread needs its own
context and its own `r15` established at thread entry. Nothing in the spec or
in CLAUDE.md currently describes a concurrency model for `exsc` itself, so
that case is unaddressed here rather than guessed at.

### 1.3 Error protocol: `CF` / `eax`

**`CF` set on return = error, with the numeric part of the `EXS-E` code in
`eax`. `CF` clear = success.**

- On the success path, the routine's return value, if any, is wherever stock
  SysV puts it (the register table above) — `rax`/`rdx:rax` or
  `xmm0`/`xmm1:xmm0`. Nothing about this protocol changes where a
  *successful* return value lives.
- On the failure path, `eax` holds the numeric part of the `EXS-E` code (for
  example, `0311` for `EXS-E0311`) and no other return register is defined —
  callers must not read `rdx`, `xmm0`, or the high 32 bits of `rax` on this
  path. The mapping from that number to the full code and its diagnostic text
  lives in `compiler/x86_64/diag/codes.inc`, generated from spec §13 — out of
  scope for this document; see CLAUDE.md, "Error codes are permanent."
- Callers check `CF` (`jc .error`) immediately after the call, before doing
  anything else with `rax`. This keeps the common (success) path branch-free —
  no comparison against a sentinel value, no reserved "impossible" return
  value to carve out of the result space — and it avoids spending a return
  register on status the way an `(value, error)` pair convention would.
- This is a status side-channel layered on top of stock SysV, not a
  replacement for it. A routine that cannot fail is not obligated to touch
  `CF` either way, but is encouraged to `clc` before returning if it shares an
  error-checked call site with routines that can fail, so callers can use one
  uniform `jc` regardless of which routine they just called.

#### The four failure channels

`CF`/`eax` above is one channel of four. The other three existed in practice
before they were written down here — this table was synthesized during wave 1
while `rt/` was being built, lived in `rt/sys.inc`'s header, and was
cross-referenced from four other files that each had to explain the same rule
again. It belongs here.

| the failure is | the channel is |
|---|---|
| diagnosable about the source being compiled | `CF` set, `eax` = the `EXS-E` numeric part |
| a host/OS syscall failure, for which no `EXS-E` code exists | `CF` set, `eax` = the kernel's own negative return (already `-errno`) |
| an expected "absent" result | a sentinel; `CF` is not meaningful |
| an internal contract violation — a compiler bug | `rassert` trap; **never** `CF` |

Four rules follow, and the reasons matter more than the table:

- **The errno channel is scoped to `rt/sys.inc`'s wrappers**, plus callers that
  propagate one verbatim without reinterpreting it (`arena_init` forwarding
  `sys_mmap`'s value, for instance). Nothing else may put a kernel errno in
  `eax`. Without that scope the two `CF` channels are indistinguishable to a
  caller, since both set `CF` and both leave a number in `eax`.
- **A syscall wrapper does not `rassert` its own arguments.** A bad fd or
  pointer is the kernel's to report, through the errno channel. Pre-validating
  would duplicate the check and diverge from it.
- **The sentinel channel is a design smell where it is ambiguous.**
  `map_get` returns 0 for both "the value is 0" and "no such key," and those
  are not distinguishable — recorded in `rt/map.inc`'s header as a known
  limitation, not defended.
- **`rassert` never sets `CF`.** A violated internal contract is not a value a
  caller can handle; it traps (`ud2`, exit 132). Routing it through `CF` would
  invite a caller to swallow a compiler bug as an ordinary error.

**This is synthesis, not derivation.** Neither CLAUDE.md nor spec §13 states
it; spec §13 registers codes for the *program being compiled* and has nothing to say
about `exsc`'s own host failures. It is recorded here because it was applied
consistently across every `rt/` module in wave 1 and held, which is evidence
but not proof.

---

## 2. Register discipline

- **`r15` is never clobbered without a matching restore**, in any routine
  that makes or receives internal calls. Since it is callee-saved under
  ordinary SysV, "never clobbered" is the same obligation every callee-saved
  register already carries — this is not a special case to remember, just a
  reminder that `r15` is not exempt from the rule that applies to `rbx`,
  `rbp`, and `r12`–`r14` too.
- **`r15` is established exactly once per context**, at process entry (or, if
  concurrency is ever added, at thread entry — see "The `r15` pin," above).
  No routine other than that entry point may assign to `r15`; every other
  routine treats it as a live, valid pointer for the duration of the call.
- **Stack alignment.** SysV requires `rsp ≡ 0 (mod 16)` immediately before a
  `call` instruction. Concretely: at the first instruction of a called
  routine, `rsp ≡ 8 (mod 16)`, because `call` itself pushed an 8-byte return
  address. A routine that sets up a frame (`push rbp` / `mov rbp, rsp` /
  `sub rsp, N`) must account for that extra 8 bytes, plus the size of any
  register pushes it does before the `sub`, when choosing `N`, or a callee
  that uses aligned SSE loads/stores (`movaps` and similar) will fault. This
  applies to internal calls exactly as it applies to `externus` calls — one
  convention, not two.
- **Unaligned packed moves are the vector baseline.** The vector float group
  (`vadd`/`vsub`/`vmul`/`vdiv` over `vf32.N`/`vf64.N`, `docs/design/ssa-ir.md`
  2.6) moves vector results through `movups`/`movupd` only: alignment is
  never load-bearing, so no `movaps`/`movapd` appears in the backends. `ymm`
  register discipline and `vzeroupper` at ABI boundaries are written here the
  day an AVX lowering exists (`vaddps ymm`), not before.
- **Red zone.** The 128 bytes below `rsp` are usable as scratch space by a
  leaf routine (one that makes no further calls before returning) without
  adjusting `rsp` to reserve them. Two independent reasons this is safe here:
  - The SysV ABI itself specifies that asynchronous signal delivery must not
    clobber the red zone, so even a process that *does* install a handler
    does not corrupt red-zone scratch data by receiving a signal.
  - More simply: this is a freestanding binary with no libc signal machinery,
    and nothing in the current design installs a signal handler at all (see
    the syscall allowlist under Syscall discipline, below — `rt_sigaction` is
    not in it). There is currently nothing asynchronous that could run on
    this stack regardless of what the ABI promises.
  
  What the red zone does **not** survive is an actual `call` made by the same
  routine: `call` pushes its return address at `[rsp-8]`, which is inside the
  red zone the routine may just have written scratch data into. A non-leaf
  routine must never assume red-zone contents survive a call it makes itself.
  If a future diagnostic-on-crash feature ever installs a signal handler, this
  section must be revisited — at minimum, evaluate `sigaltstack` for the
  handler rather than assuming it may run on the interrupted routine's red
  zone.

---

## 3. The macro dialect

All four files below exist, under `compiler/x86_64/macros/`, and every
construct in this section is exercised by at least one passing fixture
under `tests/unit/` (`struct_fields.asm`, `proc_calling_convention.asm`,
`flow_constructs.asm`, `rassert_trap.asm`, `rassert_release.asm` — see
`tests/README.md`). CLAUDE.md's evidence discipline ("prose designs are
hypotheses until code runs") governed how this section was written, not how
it now reads: two of the four files' original proposed surface syntax
turned out not to work once actually assembled (the flow-control section,
just below, records why in full), and were revised before anything outside
`macros/` could depend on them.

**The dialect is now frozen, per CLAUDE.md**: `macros/` is no longer empty,
and from this point on a change to any file in it is a whole-tree change
that goes through review, not a local edit. The `macros/proc.inc` section's
own example, just below, predates that validation pass and is not yet
reconciled with the real, tested syntax `compiler/x86_64/macros/proc.inc`'s
own header comment documents (inline `uses` mixed with the argument list,
and a bare `ret` on the success path, are both narrower than what actually
shipped) — flagged here rather than silently left to mislead a reader, and
left as a named follow-up rather than rewritten, since amending that
section was outside this revision's authorized scope. The struct and
assert sections were reconciled with their real
implementations in this same pass and are current.

### 3.1 `macros/proc.inc` — procedure declaration

**Implemented and verified** (asm-rt, wave 1) — and **not** with the syntax
this section originally proposed. `proc`/`endp` emit a SysV-conforming
prologue and epilogue, bind named arguments and locals to stack slots, and
thread the `CF`/`eax` protocol ("Error protocol: `CF` / `eax`," above) through
a single exit point, so a `fail`
from anywhere in the body still restores callee-saved registers.

The example below is lifted from `tests/unit/proc_calling_convention.asm`,
which passes.

```fasmg
proc safe_div, a, b
        uses    rbx, r12                ; separate statement, comma-separated
        locals
                slot tmp, dd            ; `slot NAME, DECL` -- not `tmp dd ?`
        endl

        mov     eax, [b]
        mov     [tmp], eax
        cmp     dword [tmp], 0
        jne     .nonzero
        fail    99                      ; sets eax, sets CF, runs the epilogue
  .nonzero:
        mov     eax, [a]
        cdq
        idiv    dword [tmp]
        return                          ; success -- NOT `clc` + `ret`
endp
```

- **Up to six arguments**, positional, no gaps. A seventh (stack-passed)
  argument is `[UNIMPLEMENTED]`; nothing has needed one.
- **`uses` is its own statement**, comma-separated, after `proc`. Not part of
  the argument list.
- **`locals` / `endl` is a required pair, even when empty.** Fields inside it
  must use `slot NAME, DECL`.
- `fail CODE` sets `eax`, sets `CF`, and jumps to the epilogue. Never `ret`
  from an error branch — the epilogue's pops get skipped.
- `return` is the success path.
- `r15` never appears in a `uses` list: per "The `r15` pin," above, it is
  never clobbered.

**Argument and local names are unmangled globals.** fasmg has one flat
namespace, so `proc f, start` defines a top-level `start` and silently
redefines anything else by that name. `proc.inc` rejects `start` and `main`
outright for this reason; the list grows only when a real collision is found.
Beware bare instruction mnemonics too — an argument named `cmp` or a struct
named `Str` collides with `CMP`/`STR` and fails immediately but unhelpfully.

> **What the original proposal got wrong.** Recorded because two of the three
> were not stylistic preferences but memory corruption, and because prose that
> was never assembled is exactly how they survived:
>
> 1. `proc name, uses rbx r12, ctx, delta` — inline `uses` mixed into the
>    argument list. Does not work; `uses` is a separate statement.
> 2. `saved_pos dq ?` as a `locals` field. Assembles clean and **emits live
>    data bytes into the middle of the prologue**, then segfaults. Unlike
>    `assert`, native directives cannot be shadowed, so `slot` is mandatory.
> 3. `clc` + `ret` on the success path. A bare `ret` **skips the epilogue's
>    pops**, so every `uses`-saved register is left unrestored — the exact
>    failure `proc` exists to prevent, recommended by the document describing
>    it.
>
> Full empirical notes, including four further bugs found by running rather
> than by inspection, are in `macros/proc.inc`'s own header.

### 3.2 `macros/flow.inc` — structured control flow

**Implemented and verified** (asm-rt, wave 1) — but not with the syntax
originally proposed here. `flow_if`/`flow_elseif`/`flow_else`/`flow_endif`,
`flow_while`/`flow_endw`, `flow_for`/`flow_endf`,
`flow_switch`/`flow_case`/`flow_default`/`flow_endsw`, each expanding to
ordinary `cmp`/`jcc` sequences with fasmg's `local` facility generating a
fresh label per expansion so nested or repeated use never collides.
Examples below are lifted verbatim (modulo surrounding setup) from
`tests/unit/flow_constructs.asm`, which passes:

```fasmg
mov	rax, 5
mov	rbx, 0
flow_if rax eq 5
	mov	rbx, 1
flow_elseif rax eq 6
	mov	rbx, 2
flow_else
	mov	rbx, 3
flow_endif

mov	rax, 0
mov	rcx, 1
flow_while rcx le 5
	add	rax, rcx
	inc	rcx
flow_endw

mov	rax, 0
flow_for ecx, 0, 16              ; ecx := 0; while ecx < 16: body; ecx += 1
	add	eax, ecx
flow_endf

mov	eax, 2
mov	ebx, 0
flow_switch eax
flow_case 1
	mov	ebx, 100
flow_case 2
	mov	ebx, 200
flow_case 3, 4, 5
	mov	ebx, 300
flow_default
	mov	ebx, 999
flow_endsw
```

Comparison operators are the words `eq ne lt le gt ge`, and comparisons are
**signed** (`jl`/`jle`/`jg`/`jge`) — an unsigned variant is
`[UNIMPLEMENTED]`; nothing needed one this wave.

**Two falsifications of the originally proposed syntax, found by assembling
it, not by inspection — both load-bearing limits on the dialect, recorded
here so neither gets re-proposed later without rediscovering why it fails:**

- **No leading dot.** A token beginning with `.` inside a real code segment
  (after `format`/`segment` have run) is claimed by fasmg's core statement
  parser as a local-label reference before macro lookup is ever consulted —
  not a style risk, a hard failure ("illegal instruction" at the call
  site). Confirmed with a minimal pair: a macro named `foo?` invoked bare as
  `foo` inside `segment readable executable` assembles cleanly; the same
  macro renamed `.foo?` and invoked as `.foo`, no other change, fails at
  that exact statement. `?`-suffixing the macro name (the usual
  safe-redefine convention — see the struct and assert sections, just
  below) does not help, because the dot is claimed at the *call site*, not
  the definition site. Bare `if`/`while`/`for` (no dot) were also
  unavailable, for a different reason: they would shadow fasmg's own native
  assemble-time directives — the exact collision the assert section, below,
  explains `rassert` avoids by not reusing `assert`. Hence the
  `flow_`-prefixed names, applying this document's own global-label-naming
  rule to a macro name instead of a data label.
- **Word operators, not symbols.** Any `match` pattern token containing a
  bare `=` character collides with `match`'s own `=identifier?`
  literal-match sigil and fails to resolve, or resolves to the wrong
  branch. `<`, `>` and `<>` survive as literal pattern tokens; `=`, `<=`
  and `>=` do not. Rather than mix conventions (symbols for three
  operators, words for the other three), all six became words.

### 3.3 `macros/struct.inc` — struct definition and field access

**Implemented and verified** (asm-rt, wave 1) — copy-adapted, not `include`d,
from the working `struct`/`end struct` pair already present in
`vendor/fasmg-x86/format/elf64.inc` (built on fasmg's native `struc`/
`virtual at`/`namespace`; every vendored format file that defines it
`purge`s it after use, and none of this project's own fixtures reach that
file — they route through `elfexe.inc` instead — so copy-adapting it here is
not a duplicate include). A `struct`/`end struct` pair declares named, typed
fields and offset constants, so field access reads as a name rather than a
hand-tracked integer:

```fasmg
struct  Span
        field   start,   dd
        length  dd       ?
end struct

        mov     eax, [rdi + Span.start]
        mov     [rdi + Span.length], ecx
```

Both field syntaxes shown above are accepted, freely mixed in one struct
body: the `field name, decl` sugar, and the bare native-`struc` form
(`name decl ?`) that sugar expands to.

Closes with **`end struct`, not `ends`**: zero single-token `ends` exist
anywhere in the vendored `vendor/fasmg-x86/` tree — every block there closes
two-token (`end macro`, `end virtual`, `end struc`), and `end struct` is
what this macro itself expands to underneath.

`Span.start` and `Span.length` are assemble-time constant offsets; a
misspelled field name is caught at assembly time (undefined symbol), not by
this macro bounds-checking the base pointer at runtime — that is what
`macros/rassert`, next, is for.

### 3.4 `macros/assert.inc` — assertions, compiled out in release

**Implemented and verified** (asm-rt, wave 1). The macro is named
**`rassert`, not `assert`**: fasmg has its own native, assemble-time
`assert` directive (confirmed by direct use — `assert 1 = 1` assembles with
no include beyond fasmg itself — and by downstream use in
`vendor/fasmg-x86/format/elf64.inc`/`elf32.inc`), with entirely different
(assemble-time, not runtime) semantics; fasmg lets a macro silently shadow a
native directive with no warning, so reusing the name would make `assert`'s
meaning depend on include order rather than staying fixed. `rassert`
expands to a runtime check plus a trap (`ud2`, emitted as its raw encoding
`db 0x0F, 0x0B` — vendor/fasmg-x86 has no symbolic mnemonic for it) in a
debug build, and to nothing at all — not even the condition — when
`RELEASE` is defined before the include:

```fasmg
        rassert rax ne 0
        rassert [Span.length + rdi] gt 0
```

Condition operators are the words `eq ne lt le gt ge` (signed), not the
symbols an earlier draft of this section showed (`= <> < > <= >=`): tried
the symbols first, and `=`, `<=` and `>=` do not survive as literal `match`
pattern tokens in fasmg's macro dialect (they collide with `match`'s own
`=identifier?` sigil) — see `macros/flow.inc`'s header, which hit the same
issue and documents it in full; `rassert` duplicates that file's condition
parser rather than depending on it, so each macro file stays independently
assemblable.

Because a `RELEASE` build is expected to erase both the check *and* its
condition expression, a condition must never contain a side effect (a call, an
increment, a flag a later line depends on) — code that only runs because an
`rassert` evaluated it is exactly the kind of debug/release skew this macro
exists to prevent, not a clever use of it.

---

## 4. Source file conventions

- **Tabs, not spaces.** The vendored fasmg source at `vendor/fasmg-x86/` uses
  tabs; project-authored source matches it, for the boring reason that mixing
  the two inside one editor session on one codebase is worse than either
  choice alone.
- **Global label naming.** Every externally callable routine gets one
  non-local label: `snake_case`, prefixed with its owning module,
  underscore-separated — `lexer_advance`, `cst_new_node`, `diag_emit`,
  `arena_alloc`, `arena_reset`. No camelCase, no dots in a name that is meant
  to be a plain global symbol (dots are reserved for the mechanism below).
- **Local label style (`.foo`).** fasmg (like classic fasm) treats a label
  beginning with `.` as local to the nearest preceding non-local label; that
  scope resets at the next non-local label. Use this for a routine's internal
  control-flow targets — `.loop`, `.retry`, `.done`, `.error` — never export
  one, and never jump into another routine's `.label` from outside it. If two
  routines need to share a `.done`-style exit, that is a sign they should be
  one routine or that the shared tail should be its own non-local routine —
  not a reason to reach across the local-label boundary.
- **File header comment.** Every project-authored file opens with a path,
  a one-line purpose, and the spec section(s) it implements, so a reader
  finds the normative text without grepping for it separately:

  ```fasmg
  ; compiler/x86_64/lexer/advance.asm
  ; SPDX-License-Identifier: GPL-3.0-or-later
  ; Copyright (C) 2026 The Exsecutor authors.
  ;
  ; DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
  ;
  ; This code is free software; you can redistribute it and/or modify it under
  ; the terms of the GNU General Public License as published by the Free
  ; Software Foundation, either version 3 of the License, or (at your option)
  ; any later version.
  ;
  ; This code is distributed in the hope that it will be useful, but WITHOUT
  ; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  ; FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
  ; more details.
  ;
  ; You should have received a copy of the GNU General Public License along
  ; with this code. If not, see <https://www.gnu.org/licenses/>.
  ;
  ; Code produced by this compiler is not covered by the GPL --
  ; see Exception A in LICENSE.EXCEPTION.
  ; ---------------------------------------------------------------------------
  ; Single-codepoint lexer advance: UTF-8 decode plus the §8.1 source-hygiene
  ; checks (BOM, NFC, bidi/invisible controls).
  ; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1, §8.2
  ; ---------------------------------------------------------------------------
  ```

  This is **Form 1** in `LICENSE.EXCEPTION`, and it is what every file under
  `compiler/`, `tools/` and `tests/` carries. Everything in this repository
  today uses Form 1.

  **Form 2** adds one paragraph designating a file as subject to the
  Classpath-style linking exception, and changes the SPDX line to
  `GPL-3.0-or-later WITH Classpath-exception-2.0`. It applies only to target-
  runtime files — the reference-counting support of §6 and the `norma.*`
  modules the compiler links into a *compiled program*. **No such file exists
  yet, and none is yours to designate.** Designation is per-file and explicit
  (OpenJDK's mechanism), precisely so that nothing is excepted by category or
  by resemblance.

  Note the trap this avoids: `compiler/x86_64/rt/` is the **compiler's own**
  runtime — arena, map, interner, syscall wrappers — linked into `exsc` and
  never into a user's program. It is Form 1, plain GPL, no exception, despite
  being called "rt". See `docs/decisions/0006-license-gpl3-with-exception.md`.

- **§8.1 self-application (CLAUDE.md).** All *project-authored* source in this
  repository is UTF-8, no BOM, LF line endings, NFC-normalized — the same bar
  §8.1 sets for the programs `exsc` compiles. "The tool that rejects CRLF
  should not ship with CRLF in it."

  **Scope, stated precisely so the rule and the repository do not appear to
  contradict each other:** this applies to source this project writes —
  everything under `compiler/`, `tools/`, `tests/`, `docs/`, and so on — not to
  `vendor/fasmg-x86/`. That tree is deliberately excepted: it is upstream
  fasmg, vendored to be byte-identical to a specific upstream release and
  checkable by hash (ADR 0003, "Supply-chain integrity"), and upstream ships
  CRLF. `.gitattributes` encodes this exception explicitly
  (`vendor/fasmg-x86/** -text`, with `PROVENANCE.md` — which this project
  wrote — carved back out to `text eol=lf`). Normalizing the vendored tree's
  line endings to satisfy this rule would silently break the hash equality
  that makes vendoring it defensible in the first place, which is a bigger
  loss than the inconsistency of having one excepted directory. Do not "fix"
  `vendor/fasmg-x86/`'s line endings; that tree is out of this document's
  jurisdiction (CLAUDE.md, Scope) as well as out of this rule's.

---

### 4.1 Reserved global names — check here before naming anything

fasmg has **one flat namespace**, and `proc` argument names, `slot` names and
`proc` names are all plain unmangled globals ("The macro dialect," above). A
collision does not error where you wrote it — it errors later, somewhere else,
naming neither your label nor the module that took the name.

This has now cost four separate agents real time. The last case:
`rt/sys.inc` declares `proc sys_openat, dirfd, pathname, flags, mode`, so a
data label named `pathname` silently became `rbp-16` and failed at its *use*
site with `variable term used where not expected`, mentioning neither
`pathname` nor `sys.inc`.

**Prefix every label with its module** (`lex_srclen`, not `srclen`). The rule
was always in "Source file conventions" above; this list is why it is not
optional.

Burned as of this revision — every `proc` name, argument and `slot`
under `compiler/`. **668 names**, up from 206 when this section was
written: `lexer/`, `diag/`, `driver/`, `cst/`, `ast/` and
`backend_fasmg/` all landed since. Regenerate rather than trusting the
count — the command below is the source of truth, and it is why this
list is a snapshot rather than a promise.

```
a acc accn addr al a_len a_n
an anyset a_ptr arena arena_alloc arena_destroy arena_init
arena_reset argc argn argp argv ascending atoff
aux auxv av b base before bf
__bfa_blk_ref __bfa_blockhdr_n bfa_block_link_body bfa_block_link_phi bfa_block_new bfa_block_ptr __bfa_check_width64
__bfa_die __bfa_emit_addsub __bfa_emit_br __bfa_emit_cmp __bfa_emit_die __bfa_emit_function __bfa_emit_iconst
__bfa_emit_inst __bfa_emit_jmp __bfa_emit_load_rax bfa_emit_module __bfa_emit_mul __bfa_emit_nl __bfa_emit_operand_text
__bfa_emit_param __bfa_emit_ret __bfa_emit_setcc bfa_extra_get bfa_extra_push bfa_extra_set __bfa_fmt_udec
bfa_func_new __bfa_hexnib bfa_inst_ptr bfa_inst_push bfa_inst_set_span __bfa_is_alldigit __bfa_make_label_prefix
bfa_module_init __bfa_next_blk __bfa_next_imm __bfa_next_type __bfa_next_udec __bfa_next_val __bfa_next_val_or_imm
__bfa_op_is_void __bfa_op_shape __bfa_optab_init __bfa_optab_lookup __bfa_out_byte __bfa_out_bytes __bfa_out_hexbyte
__bfa_out_hexnibble __bfa_out_i64 __bfa_out_lit __bfa_out_movq_rax_slot __bfa_out_movq_slot_rax __bfa_out_sp __bfa_out_u64
__bfa_parse_attrs __bfa_parse_function __bfa_parse_global __bfa_parse_imm64 __bfa_parse_line bfa_parse_module __bfa_parse_order
__bfa_parse_params __bfa_parse_type __bfa_parse_udec __bfa_p_next_nonblank __bfa_p_read_line __bfa_prescan_blocks __bfa_print_attrs
__bfa_print_blk __bfa_print_body __bfa_print_function __bfa_print_global bfa_print_module __bfa_print_one_inst __bfa_print_operands
__bfa_print_opname __bfa_print_order __bfa_print_params __bfa_print_pass1 __bfa_print_predname __bfa_print_rshape __bfa_print_type
__bfa_print_val __bfa_print_valorimm __bfa_ps_addr __bfa_ps_bin __bfa_ps_br __bfa_ps_call __bfa_ps_callind
__bfa_ps_chk __bfa_ps_cmp __bfa_ps_contrib __bfa_ps_copy __bfa_ps_fconst __bfa_ps_finish __bfa_ps_fma
__bfa_ps_gaddr __bfa_ps_iconst __bfa_ps_index __bfa_ps_jmp __bfa_ps_load __bfa_ps_loadbits __bfa_ps_param
__bfa_ps_phi __bfa_ps_redfin __bfa_ps_redinit __bfa_ps_ret __bfa_ps_retain __bfa_ps_slot __bfa_ps_store
__bfa_ps_storebits __bfa_ps_trap __bfa_ps_un __bfa_p_tokenize __bfa_realpass_body __bfa_slot __bfa_streq
__bfa_tok_at __bfa_tok_expect __bfa_tok_is_blockhdr __bfa_tok_next __bfa_tok_peek __bfa_tok_remaining __bfa_ty_is_signed
bfa_type_intern bfa_type_ptr bfa_ty_ptr bfa_ty_u1 __bfa_valmap_push __bfa_val_maybe __bfa_val_now
__bfa_val_or_imm bfa_value_type __bfa_write_stderr bi bio bisimm b_len
blkid blockid b_n bnctr bpid b_ptr bt
btgt buf bufend bv byo c callback
cap capacity clen closer closes cmd cmp_fn
cn cnt code code_num coff col consumed
count cp cpath cpv __cst_add __cst_and __cst_arith_op
__cst_at_type_row __cst_binding_stmt __cst_bitwidth __cst_block __cst_bump __cst_call_args cst_cancel
__cst_cast cst_close __cst_cmp __cst_core_type __cst_decl_row __cst_diag cst_dump
__cst_dump_rec __cst_eat_eof cst_emit_token __cst_eofcode __cst_err __cst_error_skip __cst_expr
__cst_expr_ns __cst_expr_starts __cst_expr_stmt __cst_externus_block cst_finish __cst_fix_insert __cst_for_stmt
__cst_function_decl __cst_generic_args __cst_generic_params cst_green_kid __cst_green_len cst_green_ptr __cst_if_stmt
__cst_insync __cst_interface_decl __cst_intern_green __cst_is __cst_is_addop __cst_is_cmpop __cst_item
__cst_item_starts __cst_jump_stmt __cst_kindn cst_kind_name __cst_kw __cst_kw_leads __cst_lambda
__cst_leaf __cst_match_stmt __cst_member __cst_missing __cst_module __cst_mul __cst_note
cst_open __cst_param __cst_param_list cst_parse __cst_path __cst_pattern __cst_postfix
__cst_potestas_decl __cst_primary __cst_pun __cst_pun_text __cst_range __cst_record cst_red_child
cst_red_kind cst_red_len cst_red_nkid cst_red_root cst_red_span cst_root __cst_row_item
__cst_signature __cst_stmt __cst_struct_decl __cst_sub_stmt cst_text __cst_tokp cst_tree_init
__cst_trivia __cst_type __cst_type_arg __cst_type_decl __cst_type_row __cst_type_starts __cst_unary
__cst_vel __cst_while_stmt __cst_width_ok __cst_word2 __cst_work_push __cst_xident __cst_xkw
__cst_xpun __cst_xtok ctx cur cv dcount dd
decbuf depth dest dg __diag_append diag_attach_source __diag_build_u4
__diag_build_x2 diag_emit diag_escape __diag_escape_core diag_escape_json diag_fix_clear diag_fix_delete
diag_fix_insert diag_fix_kind_str diag_fix_ptr diag_fix_replace diag_fix_required diag_fmt_u32 diag_init
diag_is_dangerous_cp diag_line_bounds diag_line_col diag_lookup diag_out_bytes diag_out_esc __diag_out_esc_mode
diag_out_fill diag_out_init diag_out_jstr diag_out_u32 diagrec diag_render_json diag_render_text
diags __diag_utf8_decode digits dirfd dlen doff dq
drv_aedifica drv_arg_is drvbuf drv_cmd_lookup drv_cstr_dup drv_cstrlen drv_ctx_reset
drvdbuf DRV_DIAG_CAP drv_emit_diag drv_env_add drv_ident_ok drv_main __drv_msg
drv_msg DRV_MSG_CAP drv_msgu drv_parse drv_say drv_slurp DRV_STAT_SIZE
drv_stub drv_u64_parse drv_usage dst dstcap dst_n eidx
emit_len endoff erno expectn fd fidx file_id
fillbyte first fixcnt fixi fixn fixval flags
flg fnidx fpid framesz fsize fty func
funcptr fx got gptr green hexbuf hi
hib hn hp i id idv idx
imap index initial_cap instid instptr internal_id intern_bytes
interner intern_id intern_init ir_arena isimm issigned issub
isvoid iv j jsonbuf k key key_len
keylen key_ptr kid kidv kind kn2 kptr
kwid kw_lookup kw_text_of lastcc lastst lbllen lblptr
len lend __lex_covered_by lex_diag lex_init __lex_is_wildcard lex_kw_as_ident
__lex_nfc_check __lex_nfc_flush __lex_nfc_push __lex_nfc_split __lex_push lex_run __lex_safe_boundary
__lex_scan_continue lex_script_ok LEX_SCRIPT_SET_SLOT __lex_set_keep __lex_set_meets lex_set_source __lex_single_script
lex_source_check lex_tokenize lin lit lit1 lit2 lo
lstart ltlen ltoff lx mag map __map_bytes_equal
map_get map_hash map_init map_insert map_iterate mdl mid
mode module mrk msg_len msg_ptr n nameid
nblocks nbuckets nbytes newid nfc_ccc nfc_compose_pair nfc_decomp_of
nfc_decompose nfc_decomp_rec nfc_is_nfc nfc_normalize nfc_quick_check nib ninsts
nkid nstart nsuf num numbuf numeric o
off offset offv one onen op opid
o_ptr opv ord ordv out_cap out_ptr p
pad pad0 pad1 pair path pathname pcount
pf plen pre pred prevcp prevdec prot
pstart pstart0 ptr raw rec red redty
ref remaining rend rend2 resolved rt2 rty
s0 s1 savedline savedpos sbase scratch scratch_arena
scratchcap script_of script_resolve setid set_n set_ptr shp
shpenum sigidx sign sigptr size slen soff
sort_stable span span_contains span_end span_make span_union src
srcbuf src_len srclen src_ptr st statbuf str_copy
str_eq stride str_slice str_to_cstr sub2 sublen sys_close
sys_exit_group sys_fstat sys_lseek sys_mmap sys_munmap sys_openat sys_read
sys_write sz t tctx text textlen textn
tid tk tl2 tlen tmp toks tokstart
tp tp2 tr trap tref trunc truncated
tstart twidth txt ty type_id tyv upto
va val valnctr value valueid vb vc
vd vec vec_get vec_init vec_push vh vi
vn voidflag vp vr vs vv vvid
w wantnum wcount whence width work wr
xid_continue xid_flags xid_start
```

Also burned: every `KW_* TOK_* PUN_* LEX_* UNI_* MAP_* DIAG_*` constant, the
structs `Arena Vec Map Interner Slice Span Tok Lexer Diag DiagFix DiagOut`,
the macros `proc uses locals slot endl fail return endp rassert struct field
kw` and the whole `flow_*` family, and every `__proc_* __flow_* __map_*
__diag_* __lex_*` internal.

**Bare instruction mnemonics collide case-insensitively.** An argument named
`cmp` assembles as the real `CMP`; a struct named `Str` collides with `STR`
and had to become `Slice`. `proc.inc` hard-rejects only `start` and `main`,
because those two were found the hard way and a speculative list trades this
file's false positives for the bug it exists to catch.

Regenerate this list with:

```sh
grep -rhE '^[[:space:]]*(proc|slot)[[:space:]]' compiler/ \
  | sed -E 's/^[[:space:]]*(proc|slot)[[:space:]]+//' | tr ',' '\n' \
  | sed -E 's/[[:space:]]*(;.*)?$//; s/^[[:space:]]*//' \
  | grep -xE '[A-Za-z_][A-Za-z_0-9]*' | sort -u
```

## 5. The include idiom

Two different kinds of `include`, resolved two different ways:

- **Project-internal includes** (anything under `compiler/x86_64/` or
  `compiler/shared/` including another file in that same tree) use a path
  relative to the including file. No environment variable needed, no
  ambiguity about which copy of a file is meant — the same discipline §9.3
  asks of the compiler's own behavior applies to how its source finds itself.
- **Vendored third-party includes** — the fasmg x86-64 instruction set and
  the ELF64 object-format writer — are resolved against `vendor/fasmg-x86/`
  via fasmg's `INCLUDE` environment variable, exactly as verified in ADR 0003:

  ```sh
  INCLUDE=vendor/fasmg-x86 fasmg compiler/x86_64/exsc.asm build/exsc
  ```

  with source that opens, e.g., `include 'format/format.inc'` and resolves it
  against `vendor/fasmg-x86/format/format.inc`. Wiring this invocation into
  the actual build (exporting `INCLUDE` where `fasmg` is invoked) is the
  Makefile's responsibility, not this document's — it is out of this file's
  scope (CLAUDE.md, Scope: each agent owns its directory).

  Full provenance, hashes, and the re-vendoring procedure for everything
  under `vendor/fasmg-x86/` are in `vendor/fasmg-x86/PROVENANCE.md` — read
  that file for the supply-chain reasoning; it is cited here, not repeated.

---

## 6. Syscall discipline

The syscall allowlist is closed (CLAUDE.md) and is reproduced here as a table
because a reader reaching for the number is a reader this document should not
send elsewhere:

| syscall | number | typical use in this compiler |
|---|---|---|
| `read` | 0 | reading source file content |
| `write` | 1 | diagnostics to stderr, generated output |
| `close` | 3 | file descriptor cleanup |
| `fstat` | 5 | file size, for read planning |
| `lseek` | 8 | seeking within an opened file |
| `mmap` | 9 | backing store for arenas — this compiler has no libc `malloc` |
| `munmap` | 11 | releasing arena backing store |
| `openat` | 257 | opening source and output files |
| `exit_group` | 231 | process termination |

- **Every syscall site in `exsc` funnels through `rt/sys.inc`.** That is a
  source-level convention — a single place to read, review, and extend the
  allowlist — distinct from but complementary to the binary-level check
  below. Do not emit a raw `syscall` instruction from any other file of the
  compiler.
- **There is a second closed set, and it is not this one.** A *compiled
  program* carries the runtime prelude (`docs/design/runtime.md`), which
  cannot include `rt/` and issues its own syscalls — `write`, `exit_group`,
  `mmap`, and per capability atom the rest — under `if EXS_POTESTAS_…` so
  that the binary holds only what the program's capability closure admits.
  That set is `compiler/x86_64/prelude/`'s, tabulated in runtime.md section 2.6,
  audited by `tools/syscall-audit.sh` on the *program's* binary with the
  program's atoms, and it is what makes spec §10.3's audit a property of
  the artifact. The two sets are reviewed separately; the compiler's never
  grows because a program needs a syscall. **No socket-family syscall,
  ever** still binds the compiler absolutely; a program has one iff its
  closure contains `rete`, and the audit proves the iff.
- **Adding a syscall to the allowlist is a reviewed change with a stated
  reason** (CLAUDE.md). This document does not pre-authorize any addition.
- **No socket-family syscall, ever.** CLAUDE.md's operationalization of §9.3's
  "no network access, ever, at any phase" — `socket`, `connect`, `bind`,
  `sendto`, `recvfrom`, and the rest of that family are not on the allowlist
  above and must never appear.
- **`make audit` checks the binary, not just the source.** Per §18.1, the
  mechanism is to extract every `syscall` instruction site and the `rax`
  value active at it directly from the assembled output and diff that against
  the declared allowlist — `tools/syscall-audit.sh` (invoked by the `audit`
  Makefile target) is what actually does this. Routing every syscall through
  `rt/sys.inc` is what keeps that audit meaningful to a human reader; the
  audit itself trusts the binary, not the convention.

---

## 7. How to add a module

1. **Pick the directory.** One agent owns each subtree of `compiler/x86_64/`
   (CLAUDE.md, Scope). Adding a new top-level subtree, or depending on
   `macros/` before it exists, is a decision for this document and CLAUDE.md,
   not something to do unilaterally from inside a module.
2. **Open with the standard header** (Source file conventions, above) —
   path, purpose, spec section(s).
3. **Make it reachable.** fasmg assembles one entry file
   (`compiler/x86_64/exsc.asm`, per the Makefile) that pulls in everything
   else by `include`; there is no separate link step. A correct file nothing
   `include`s, directly or transitively, from that entry point is invisible
   to the build no matter how correct it is. Add the `include` line to
   whatever file in your subtree aggregates it.
4. **Follow the calling convention for anything callable from outside the
   file:** SysV AMD64 argument and return registers, `r15` already valid on
   entry and never reassigned, the `CF`/`eax` error protocol for anything
   that can fail, and honest `uses` bookkeeping for any callee-saved
   register the routine actually clobbers — all per Calling convention and
   Register discipline, above.
5. **Route every syscall through `rt/sys.inc`** (Syscall discipline, above).
   Nothing else emits `syscall` directly.
6. **Adding a diagnostic requires the spec first.** A new `EXS-E` code needs a
   §13 amendment before `compiler/x86_64/diag/codes.inc` gains an entry for
   it — never invent a code locally, never renumber an existing one
   (CLAUDE.md). If your module needs a code that does not exist yet, that is
   a spec change to propose, not a local workaround to add.
7. **Determinism is not optional** (CLAUDE.md). Nothing you add may order
   output by a pointer value, a raw hash-bucket order, or anything else that
   varies run to run; use `rt/map.inc`'s insertion-ordered map for anything
   that affects emitted bytes, and make symbol/section emission order
   explicit rather than incidental.
8. **Hold the new file to spec §8.1** (Source file conventions, above) —
   UTF-8, no BOM, LF, NFC — unless it lives under `vendor/`, which this rule
   does not reach.
