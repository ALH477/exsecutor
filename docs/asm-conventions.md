# Assembly conventions

**Status:** Binding, per CLAUDE.md ("The macro dialect is frozen after wave
1... Conventions are in `docs/asm-conventions.md` and are binding."). Last
revised 2026-09-09.

This document governs everything under `compiler/x86_64/` and, where a rule is
not architecture-specific, the eventual `compiler/aarch64/` and
`compiler/riscv64/` trees. It is a normative reference, not a tutorial: it
says what the rules are and why, not how to write assembly.

Two things named below do not exist yet and are marked accordingly. Read the
markers; do not treat proposed syntax as working code. Everything else here —
the calling convention, register discipline, source conventions, the include
idiom, and syscall discipline — is a binding rule as of this revision, even
though very little code exists to follow it yet (`compiler/x86_64/` is empty
directories at time of writing; see the repository root `README.md`).

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

## 3. The macro dialect — **[UNIMPLEMENTED]**

None of the four files below exist. `compiler/x86_64/macros/` is currently an
empty directory. Everything in this section is a **proposed surface syntax**,
written to be exercised and revised, not a working artifact — CLAUDE.md's
evidence discipline ("prose designs are hypotheses until code runs") applies
to this section by name. Treat every example as a statement of intent, not as
verified fasmg source; none of it has been assembled.

CLAUDE.md freezes the macro dialect once the first module depends on it —
until `macros/` exists and something outside it uses it, this proposal is open
to revision without going through ADR-level review. After that point, changing
it is a whole-tree change.

### 3.1 `macros/proc.inc` — procedure declaration

Intent: one macro pair (`proc` / `endp`) that emits a SysV-conforming
prologue and epilogue, binds named locals to stack slots, and threads the
`CF`/`eax` error protocol ("Error protocol: `CF` / `eax`," above) through a
single exit point so a `fail` from anywhere in the body still restores
callee-saved registers correctly.

```fasmg
; proposed — not implemented
proc    lexer_advance, uses rbx r12, ctx, delta
        locals
                saved_pos      dq      ?
                scratch32      dd      ?
        endl

        mov     rbx, [ctx]              ; named argument, spilled to its home slot
        mov     eax, [delta]
        mov     [saved_pos], rbx
        cmp     eax, 0
        jl      .bad_delta
        ; ... body ...
        clc                             ; success
        ret

.bad_delta:
        fail    EXS_E0311               ; sets eax, sets CF, still runs the epilogue
endp
```

- `uses rbx r12` declares which callee-saved registers this routine clobbers;
  `proc` emits the matching push/pop pairs so the caller-facing contract in
  the register table above holds without the author hand-writing
  save/restore code.
- `locals` / `endl` names stack slots instead of hand-computed `[rbp-N]`
  offsets.
- `fail CODE` is the one legal way to set the error path: it is intended to
  set `eax` to `CODE`'s numeric value, set `CF`, and jump to the routine's
  epilogue — never `ret` directly from an error branch, or callee-saved
  registers restored by the epilogue get skipped.
- `r15` is deliberately absent from `uses` lists in every example: per "The
  `r15` pin," above, it is never clobbered, so it is never something a
  routine needs to save.

### 3.2 `macros/flow.inc` — structured control flow

Intent: `.if`/`.elseif`/`.else`/`.endif`, `.while`/`.endw`, `.for`/`.endf`,
`.switch`/`.case`/`.default`/`.endsw`, each expanding to ordinary
`cmp`/`jcc` sequences with fasmg's `local` facility generating a fresh label
per expansion so nested or repeated use never collides.

```fasmg
; proposed — not implemented
.if     rax = 0
        call    diag_emit_empty
.elseif rax < 0
        fail    EXS_E0311
.else
        call    lexer_advance
.endif

.while  ecx > 0
        call    cst_visit_one
        dec     ecx
.endw

.for    ecx, 0, 16                      ; ecx := 0; while ecx < 16; ecx += 1
        call    hash_mix_byte
.endf

.switch eax
.case   EXS_E0101
        call    diag_render_bom
.case   EXS_E0102, EXS_E0103
        call    diag_render_normalization
.default
        call    diag_render_generic
.endsw
```

### 3.3 `macros/struct.inc` — struct definition and field access

Intent: a `struct`/`ends` pair that declares named, typed fields and offset
constants, so field access reads as a name rather than a hand-tracked
integer.

```fasmg
; proposed — not implemented
struct  Span
        field   start,   dd
        field   length,  dd
ends

        mov     eax, [rdi + Span.start]
        mov     [rdi + Span.length], ecx
```

`Span.start` and `Span.length` are assemble-time constant offsets; the macro
is expected to catch a misspelled field name at assembly time, not to
bounds-check the base pointer at runtime — that is what `macros/assert.inc`
is for.

### 3.4 `macros/assert.inc` — assertions, compiled out in release

Intent: an `assert` macro that expands to a runtime check plus a trap in a
debug build, and to nothing at all — not even the condition — when `RELEASE`
is defined before the include.

```fasmg
; proposed — not implemented
        assert  rax <> 0
        assert  [Span.length + rdi] > 0
```

Because a `RELEASE` build is expected to erase both the check *and* its
condition expression, a condition must never contain a side effect (a call, an
increment, a flag a later line depends on) — code that only runs because an
`assert` evaluated it is exactly the kind of debug/release skew this macro
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

- **Every syscall site funnels through `rt/sys.inc`.** That is a source-level
  convention — a single place to read, review, and extend the allowlist —
  distinct from but complementary to the binary-level check below. Do not
  emit a raw `syscall` instruction from any other file.
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
