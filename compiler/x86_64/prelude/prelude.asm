; compiler/x86_64/prelude/prelude.asm
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
;
; [OPEN] LICENSING. This file is Form 1 (plain GPL, no Exception B) because
; docs/asm-conventions.md section 4 says of Form 2: "No such file exists yet,
; and none is yours to designate." This file is exactly the class
; LICENSE.EXCEPTION B3 reserves for designation -- reference-counting support
; (spec section 6) that the compiler emits into a compiled program -- and its
; header travels verbatim into every OUT. Whether it should carry the Form 2
; designation line is the owner of LICENSE.EXCEPTION's decision, requested in
; this agent's report and NOT made here.
; -----------------------------------------------------------------------------
; The runtime prelude: the executable half of the blob every compiled program
; carries. `docs/design/runtime.md` sections 2.1-2.7 is the design; this file
; is that design assembled.
;
; WHAT THIS FILE IS. Static fasmg TEXT. `exsc` carries it as data (fasmg's
; `file` directive, exactly as compiler/shared/unicode/tables/tables.inc
; carries the Unicode tables) and copies it byte for byte into `OUT`
; (runtime.md 2.1). It is never generated, so the largest single component of
; `OUT` is a function of `exsc`'s own bytes and of nothing else -- spec 9.3 by
; construction rather than by discipline.
;
; WHAT IT MUST NEVER CONTAIN (runtime.md H1). No `format`, no `entry`, no
; `segment`, no `include`. The wrapper -- backend_fasmg/program.inc for a
; compiled program, a tests/unit/prelude_*.asm harness for a fixture -- opens
; `segment readable executable` and includes this file into it. A stray
; `segment` here breaks every emitted program at once and names neither file.
;
; WHAT THE WRAPPER MUST DEFINE FIRST. The eleven capability constants of spec
; 4.6, in 4.6 order, each 0 or 1, plus the MXCSR image. They are checked
; below and a missing one is a fasmg error rather than a silently absent
; routine. See prelude/README.md.
;
; NAMES (runtime.md 2.2, H6). Two prefixes, and the difference is the ABI:
;   `bfausr_exsrt_<x>`  IR-callable. backend_fasmg/emit.inc prints every IR
;                       `@name` as `bfausr_<name>`, so an IR `call
;                       @exsrt_scriptor_scribe` assembles against a label
;                       spelled with the emitter's prefix. A routine carries
;                       this prefix IF AND ONLY IF prelude/interface.inc
;                       declares it -- that keeps H6's collision protection
;                       (a user function named `exsrt_...` is a class-B
;                       redefinition at the checker) exactly coextensive with
;                       the set of prefixed labels.
;   `exsrt_<x>`         private, or reached by a label the emitter HARDCODES
;                       rather than resolving from IR: `retain`/`release` are
;                       IR instructions, not names (IR 2.8), and
;                       `bfausr_trap` jumps here.
; No other prefix appears in this file. `exsrt_` is unburned:
; docs/asm-conventions.md section 4.1's list has no such name.
;
; CALLING CONVENTION. Plain SysV AMD64, hand-rolled prologues. NOT this
; compiler's internal convention: no macros/proc.inc, no `CF`/`eax` error
; protocol, no r15 pin (asm-conventions section 1.2 pins r15 for `exsc`'s own
; internals; a compiled program is not `exsc`'s internals). Scratch is
; restricted to `rax rcx rdx rsi rdi r8-r11` -- the SysV caller-saved set --
; so these routines are callable from emit.inc's output, which preserves
; nothing across a call and expects nothing preserved but the callee-saved
; set. `rbp` is pushed and popped where a frame is wanted; nothing else
; callee-saved is touched.
;
; AND: the x86-64 Linux `syscall` INSTRUCTION destroys `rax`, `rcx` and `r11`
; (it puts the return address in rcx and RFLAGS in r11) and preserves every
; other register, argument registers included. No routine here may hold a
; live value in rcx or r11 across a syscall. This is not a style rule: it is
; the defect the first run of tests/unit/prelude_scribe.asm found in
; runtime.md 2.4's own published code.
;
; SYSCALLS. This file is the SECOND closed syscall set in the repository
; (docs/asm-conventions.md section 6, as amended). It cannot include rt/ --
; runtime.md constraint 3 -- so it issues `syscall` directly, and every
; syscall-bearing routine sits inside `if EXS_POTESTAS_<atom>`, so that a
; program's BINARY holds only what its capability closure admits and
; `tools/syscall-audit.sh` can check spec 10.3 on the artifact. Per atom,
; runtime.md 2.6:
;   core (always)  exit_group(231); write(1) to fd 2, from exsrt_abort only
;   ambitus        write(1), read(0)
;   alloc          mmap(9), munmap(11)
;   everything else is [OPEN] and has no routine here.
; Every syscall site loads `rax` with an IMMEDIATE in the instruction
; immediately preceding its `syscall`. That is not style: it is the one form
; tools/syscall-audit.sh's linear sweep resolves, and a syscall it cannot
; resolve is a FAILURE, not a skip.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md 4.1, 4.5-4.7, 5.1, 5.4, 6.1, 6.3,
; 6.5, 6.6, 9.3, 10.3, 12, 18.1; docs/design/runtime.md 2.1-2.7.
; -----------------------------------------------------------------------------

; ---- the wrapper's contract, checked ---------------------------------------
; Eleven atoms in spec 4.6 order, then the MXCSR image. Undefined is an error
; here rather than a routine that silently is not there: an absent
; `EXS_POTESTAS_AMBITUS` would assemble a program with no `scribe` and no
; message saying why.

if ~ defined EXS_POTESTAS_MUNDUS
	err 'prelude: EXS_POTESTAS_MUNDUS is undefined -- the wrapper must define all eleven spec 4.6 atoms before including prelude.asm'
end if
if ~ defined EXS_POTESTAS_ALLOC
	err 'prelude: EXS_POTESTAS_ALLOC is undefined'
end if
if ~ defined EXS_POTESTAS_SERMO
	err 'prelude: EXS_POTESTAS_SERMO is undefined'
end if
if ~ defined EXS_POTESTAS_HOROLOGIUM
	err 'prelude: EXS_POTESTAS_HOROLOGIUM is undefined'
end if
if ~ defined EXS_POTESTAS_ARCHIVUM
	err 'prelude: EXS_POTESTAS_ARCHIVUM is undefined'
end if
if ~ defined EXS_POTESTAS_RETE
	err 'prelude: EXS_POTESTAS_RETE is undefined'
end if
if ~ defined EXS_POTESTAS_FORTUNA
	err 'prelude: EXS_POTESTAS_FORTUNA is undefined'
end if
if ~ defined EXS_POTESTAS_AMBITUS
	err 'prelude: EXS_POTESTAS_AMBITUS is undefined'
end if
if ~ defined EXS_POTESTAS_FILUM
	err 'prelude: EXS_POTESTAS_FILUM is undefined'
end if
if ~ defined EXS_POTESTAS_MACHINA
	err 'prelude: EXS_POTESTAS_MACHINA is undefined'
end if
if ~ defined EXS_POTESTAS_CRUDUM
	err 'prelude: EXS_POTESTAS_CRUDUM is undefined'
end if
if ~ defined EXS_MXCSR
	err 'prelude: EXS_MXCSR is undefined -- spec 5.4 state is SET at entry, never assumed (runtime.md 2.7)'
end if

; ---- carrier record layouts -------------------------------------------------
; NORMATIVE COPY. prelude/interface.inc restates these offsets for the
; checker, and runtime.md H4 names that pair as the only place they can
; disagree. The storage itself is in prelude_data.asm; only the offsets are
; here, and only these names are used below.

EXS_MUNDUS_RSP0		= 0		; ExsMundus.rsp0     ptr
EXS_MUNDUS_SIZE		= 8

EXS_AMBITUS_IN		= 0		; ExsAmbitus.in      i32
EXS_AMBITUS_OUT		= 4		; ExsAmbitus.out     i32
EXS_AMBITUS_ERR		= 8		; ExsAmbitus.err     i32
EXS_AMBITUS_ARGC	= 16		; ExsAmbitus.argc    u64
EXS_AMBITUS_ARGV	= 24		; ExsAmbitus.argv    ptr
EXS_AMBITUS_ENVP	= 32		; ExsAmbitus.envp    ptr
EXS_AMBITUS_SIZE	= 40

EXS_SCRIPTOR_A		= 0		; Scriptor.a          ambitus (ptr)
EXS_SCRIPTOR_DESCRIPTOR	= 8		; Scriptor.descriptor i32
EXS_SCRIPTOR_SIZE	= 16		; align 8

EXS_TEXTUS_PTR		= 0		; textus.ptr          ptr    (runtime.md 2.3)
EXS_TEXTUS_LEN		= 8		; textus.len          u64
EXS_TEXTUS_SIZE		= 16

EXS_OBJ_RC		= 0		; ExsObj.rc           u64    (runtime.md 2.5)
EXS_OBJ_DTOR		= 8		; ExsObj.dtor         ptr; 0 = none
EXS_OBJ_HEADER		= 16		; payload starts here, 16-byte aligned

EXS_ARENA_BASE		= 0		; ExsArena.base       ptr    (runtime.md 2.6)
EXS_ARENA_CUR		= 8		; ExsArena.cur        ptr
EXS_ARENA_LIMIT		= 16		; ExsArena.limit      ptr
EXS_ARENA_SIZE		= 24
EXS_ARENA_PAYLOAD	= 32		; the record lives at the head of its own
					; mapping; the bump region starts here, so
					; the first payload byte is 16-byte aligned

; ---- abort kinds ------------------------------------------------------------
; runtime.md 2.5's table, permanent small integers. Spec 6.6 (as amended) owns
; the SHAPE -- `abortus N` on fd 2, then SIGILL -- and points at runtime.md for
; the enum. These are NOT EXS-E codes: a trap is not a diagnostic, and no
; section 13 code is invented here (CLAUDE.md).

EXS_ABORTUS_NUMERICUS	= 1		; numeric trap (spec 5.4); today bfausr_trap
EXS_ABORTUS_SATURATIO	= 2		; refcount carried out of 64 bits (spec 6.5)
EXS_ABORTUS_RESURRECTIO	= 3		; resurrection or double release (spec 6.6)
EXS_ABORTUS_ARENA	= 4		; arena exhausted (runtime.md 2.6)
EXS_ABORTUS_TERMINUS	= 5		; a `dum ... terminus N` tried to enter its
					; N+1th iteration (spec 8.5; requested by
					; docs/design/lowering.md finding 2, which
					; records that kinds 1-4 had no row for it
					; and `trap terminus` was folding into 1)

; The fixed prefix of the abort line, in bytes. The string itself is in
; prelude_data.asm (this file must define no data -- runtime.md H1); that file
; `assert`s the two agree, so the pair cannot drift silently.
EXS_ABORTUS_PRAEFIXUM_LEN = 19		; len('exsecutor: abortus ')

; =============================================================================
; ENTRY STUB -- runtime.md 2.2
; =============================================================================
; MUST BE THE FIRST ROUTINE IN THIS FILE and therefore the first byte of the
; executable segment. compiler/x86_64/exsc.asm's own header records, from two
; measured runs, that tools/syscall-audit.sh's linear sweep is reliable only
; from the entry point and that code before it is swept "with no such
; guarantee". The prelude leads; the module follows.
;
; Linux hands us: [rsp] = argc, then argv[0..argc-1], NULL, envp..., NULL,
; auxv. `rsp` is 16-byte aligned here (SysV), so the `call` below leaves the
; callee with rsp == 8 (mod 16), which is what emit.inc's prologue expects.
; `rdx` holds glibc's atexit hook by convention; there is no libc and it is
; ignored. Nothing here READS envp or auxv -- the stub records where the
; initial stack IS, and only an `ambitus` derivation ever looks (spec 9.3: no
; ambient environment).

exsrt_start:
	mov	rax, rsp
	mov	[exsrt_mundus + EXS_MUNDUS_RSP0], rax

	; Spec 5.4 is SET, not assumed. Linux happens to initialise MXCSR to
	; 0x1F80 on execve; relying on that is exactly the ambient-state class
	; spec 5.4 exists to remove (runtime.md 2.7). The red zone is free
	; here -- nothing is below rsp at process entry, and this is a leaf
	; use with no intervening call.
	mov	dword [rsp - 4], EXS_MXCSR
	ldmxcsr	dword [rsp - 4]

	lea	rdi, [exsrt_mundus]	; the one carrier: Mundus as a ptr (CHK 2.8)
	call	bfausr_initium		; IR 2.9: carriers first; initium has one
					; carrier and no declared parameters
	movzx	edi, al			; u8 result -> process exit status (spec 4.7)
	mov	eax, 231		; exit_group -- immediate, adjacent to its
	syscall				; syscall, so the audit resolves it

; =============================================================================
; ambitus -- runtime.md 2.2, 2.4; spec 4.6 (the standard streams are ambitus)
; =============================================================================
; Gated as one block. runtime.md 2.1 requires the gate around every
; SYSCALL-BEARING routine; gating the whole atom is a superset of that and is
; the reading runtime.md 2.6 argues for -- "the binary contains only the
; routines the program's authority admits". A program whose closure lacks
; `ambitus` cannot derive one, so `exsrt_mundus_ambitus` has no business in
; its binary either.

if EXS_POTESTAS_AMBITUS

; bfausr_exsrt_mundus_ambitus(m: ptr) -> ptr
;   `m.ambitus()`. IR: `%a = call ptr @exsrt_mundus_ambitus %m`. Total on a
;   host that has the atom (spec 4.7), so it cannot fail and returns no
;   `eventus`. Idempotent by construction rather than by a first-time flag:
;   it recomputes the same six words from the same `rsp0` every time, which
;   is one fewer piece of mutable state to get wrong.
bfausr_exsrt_mundus_ambitus:
	mov	rax, [rdi + EXS_MUNDUS_RSP0]
	lea	r8, [exsrt_ambitus]
	mov	dword [r8 + EXS_AMBITUS_IN], 0
	mov	dword [r8 + EXS_AMBITUS_OUT], 1
	mov	dword [r8 + EXS_AMBITUS_ERR], 2
	mov	rcx, [rax]			; argc
	mov	[r8 + EXS_AMBITUS_ARGC], rcx
	lea	rdx, [rax + 8]			; argv
	mov	[r8 + EXS_AMBITUS_ARGV], rdx
	lea	rsi, [rdx + rcx*8 + 8]		; envp = argv + 8*argc + 8 (the NULL)
	mov	[r8 + EXS_AMBITUS_ENVP], rsi
	mov	rax, r8
	ret

; bfausr_exsrt_scriptor_ad_exitum(ret: ptr, a: ptr) -> void
;   `Scriptor.ad_exitum(a)`. `Scriptor` is an aggregate, so IR 2.9 passes a
;   hidden pointer to caller-owned storage FIRST and the receiver-less
;   argument `a` second. It cannot fail: spec 4.7 makes the derivation total,
;   and a closed descriptor is discovered by the write, not by the
;   constructor (runtime.md 2.4).
bfausr_exsrt_scriptor_ad_exitum:
	mov	[rdi + EXS_SCRIPTOR_A], rsi
	mov	eax, [rsi + EXS_AMBITUS_OUT]	; read the stream, do not hardcode 1
	mov	[rdi + EXS_SCRIPTOR_DESCRIPTOR], eax
	ret

; bfausr_exsrt_scriptor_scribe(s: ptr, t: ptr) -> u64
;   `s.scribe(t)`. Both parameters are aggregates and travel by pointer:
;   `s` is a Scriptor, `t` a two-word textus view (runtime.md 2.3).
;
;   Returns the count written. Spec 11 (as amended) says I/O reports failure
;   as `eventus` and never a silently short count, and that `scribe` returns
;   `eventus<mensura>` -- which is [OPEN] until `eventus` has syntax. Until
;   then this returns the COUNT, which is less than `t`'s length exactly when
;   something failed, and that is the whole of what it can say. Stated here
;   because the difference is observable: `examples/initium.exsc` discards
;   this result, so the hello world exits 0 even with stdout closed.
;
;   Partial writes are looped. EINTR is retried. Any other -errno ends the
;   loop. EAGAIN on a non-blocking descriptor would spin here [OPEN];
;   SIGPIPE on a closed pipe kills the process under the ambient disposition
;   and the prelude does not change it -- rt_sigaction is on no allowlist
;   [OPEN]. Neither a partial write nor an EINTR can be produced by a fixture
;   without a second process, which tests/run.sh does not have: both paths
;   are [UNTESTED] (runtime.md 3).
;
;   DEFECT FOUND BY RUNNING THIS. runtime.md 2.4 publishes this routine with
;   the total request held in `r11` across the `syscall`. The x86-64 Linux
;   syscall ABI destroys `rax`, `rcx` and `r11` and preserves everything else
;   (syscall(2)), so `r11` comes back holding the saved RFLAGS and the routine
;   returns that instead of a byte count: the first run of this fixture wrote
;   all 101 bytes correctly and exited 6. The total lives in the frame here
;   instead, which is what the routine has a frame for. THE DESIGN IS WRONG
;   AND THIS IS RIGHT; an amendment to runtime.md 2.4's code block is in this
;   agent's report. Nothing else in this file holds a live value in `rcx` or
;   `r11` across a `syscall`.
bfausr_exsrt_scriptor_scribe:
	push	rbp
	mov	rbp, rsp
	sub	rsp, 16
	mov	r8d, [rdi + EXS_SCRIPTOR_DESCRIPTOR]
	mov	r9, [rsi + EXS_TEXTUS_PTR]
	mov	r10, [rsi + EXS_TEXTUS_LEN]	; remaining
	mov	[rbp - 8], r10			; total requested -- NOT a register
  .loop:
	test	r10, r10
	jz	.done
	mov	edi, r8d
	mov	rsi, r9
	mov	rdx, r10
	mov	eax, 1				; write
	syscall
	cmp	rax, -4096			; the Linux raw-syscall error range
	ja	.err
	add	r9, rax				; partial write: advance, go again
	sub	r10, rax
	jmp	.loop
  .err:
	cmp	eax, -4				; -EINTR: nothing was written, retry
	je	.loop
  .done:
	mov	rax, [rbp - 8]
	sub	rax, r10			; bytes actually written
	mov	rsp, rbp
	pop	rbp
	ret

; bfausr_exsrt_scriptor_scribe_octetum(s: ptr, b: u8) -> u64
;   `s.scribe_octetum(b)`. Writes exactly ONE byte -- the low 8 bits of `b`
;   -- to the Scriptor's descriptor. docs/design/wire-codec.md D7: `scribe`
;   takes a `textus`, which spec 5.1 makes UTF-8, so no `scribe` call can
;   put 0xFF or a lone 0x80 on a stream. A wire frame is arbitrary bytes and
;   this is how one reaches stdout. `s` is an aggregate and travels by
;   pointer; `b` is a scalar and travels by value in `sil`.
;
;   THE BYTE IS `sil` AND NOTHING ABOVE IT. docs/design/ssa-ir.md 2.2 holds a
;   `u8` zero-extended in its 64-bit slot and emit.inc passes that whole slot
;   in `rsi`, so bits 8-63 are zero from emitted code -- but SysV leaves them
;   unspecified for a narrow argument, and this routine does not need them
;   to be anything: it stores one byte and never reads `rsi` wider.
;
;   Provisional exactly as `scribe` is (interface.inc marks the row
;   EXS_IFACE_F_APERTUM): returns the COUNT written, 1 on success and 0 on
;   failure, because spec 11 (as amended) wants `eventus<mensura>` and
;   `eventus` has no syntax yet. SAME ERROR CONVENTION AS `scribe`: EINTR is
;   retried, a zero-byte return is retried (it is `scribe`'s loop with
;   `remaining` = 1, and `scribe` goes round again on 0), and any other
;   -errno ends the call with nothing written. EAGAIN and SIGPIPE are
;   `scribe`'s [OPEN] items, unchanged. The EINTR and zero-return paths need
;   a second process to produce and are [UNTESTED], as `scribe`'s are.
;
;   The byte lives in the frame, not in a register, because `write` takes a
;   buffer ADDRESS. `rdi`, `rsi` and `rdx` are syscall arguments and the
;   kernel preserves them, so the retry re-issues the same call without
;   reloading; nothing is held in `rcx` or `r11` (this file's header).
;
;   SAME GATE, SAME SYSCALL. It sits inside `if EXS_POTESTAS_AMBITUS` next to
;   `scribe` and issues `write(1)` with `edi` loaded from the Scriptor field,
;   which tools/syscall-audit.sh admits under `ambitus` and under nothing
;   else. No syscall is added to either closed set.
bfausr_exsrt_scriptor_scribe_octetum:
	push	rbp
	mov	rbp, rsp
	sub	rsp, 16
	mov	[rbp - 8], sil			; the byte: `b`'s low 8 bits only
	mov	edi, [rdi + EXS_SCRIPTOR_DESCRIPTOR]
	lea	rsi, [rbp - 8]
	mov	edx, 1
  .loop:
	mov	eax, 1				; write
	syscall
	cmp	rax, -4096			; the Linux raw-syscall error range
	ja	.err
	test	rax, rax			; 0 bytes and no error: go again, as
	jz	.loop				; `scribe`'s loop does with 1 remaining
	jmp	.done				; rax = 1: the byte is written
  .err:
	cmp	eax, -4				; -EINTR: nothing was written, retry
	je	.loop
	xor	eax, eax			; any other -errno: 0 bytes written
  .done:
	mov	rsp, rbp
	pop	rbp
	ret

end if	; EXS_POTESTAS_AMBITUS

; =============================================================================
; ARC -- runtime.md 2.5; spec 6.5, 6.6
; =============================================================================
; Always assembled. These routines carry no syscall, and gating in this file
; is by syscall surface, not by code size (runtime.md 2.5).
;
; Header: ExsObj { rc u64 @0, dtor ptr @8 }, payload at +16.
;
; rc >= 1 is live. **rc == 0 means UNDER DESTRUCTION**, which is what makes
; spec 6.6's resurrection detectable at one compare. Spec 6.5 as amended:
; the retain that would carry out of 64 bits aborts -- saturation is the
; EVENT, not a state the program continues in.
;
; runtime.md H5: do not reuse 0 for a future "immortal" object. It would need
; a third state and would reopen 6.5's semantics.
;
; Reached by a label emit.inc HARDCODES (`retain %r` lowers to
; `mov rdi, [slot]; call exsrt_retain`), so these keep the plain prefix.
; The `_c` forms are for `refc` values (IR 2.3); the emitter chooses by type.

; exsrt_retain(o: ptr) -> void   -- non-atomic (spec 6.3 decision 2: `ref`)
exsrt_retain:
	cmp	qword [rdi + EXS_OBJ_RC], 0
	je	exsrt_abort_resurrectio		; retain of an object under destruction
	add	qword [rdi + EXS_OBJ_RC], 1
	jc	exsrt_abort_saturatio		; carried out of 64 bits
	ret

; exsrt_release(o: ptr) -> void  -- non-atomic
exsrt_release:
	sub	qword [rdi + EXS_OBJ_RC], 1
	jc	exsrt_abort_resurrectio		; was already 0: double release, or
						; a release from inside its own dtor
	jz	.destroy
	ret
  .destroy:
	; rc is now 0 == "under destruction" for the destructor's duration, so
	; a retain from inside it aborts 3.
	mov	rax, [rdi + EXS_OBJ_DTOR]
	test	rax, rax
	jz	.nodtor
	push	rbp				; rsp == 8 (mod 16) on entry; this
	mov	rbp, rsp			; makes it 0 (mod 16) for the call
	call	rax				; dtor(o), SysV; it releases its own
						; fields (IR 2.8)
	pop	rbp
  .nodtor:
	; MEMORY IS NOT FREED. runtime.md 2.6, H3: the count governs WHEN
	; destruction happens (spec 6.6, deterministic and observable); the
	; arena governs when memory returns (`reconde`). A free list is [OPEN].
	ret

; exsrt_retain_c(o: ptr) -> void  -- atomic, for `refero_communis` / `refc`
exsrt_retain_c:
	mov	rax, 1
	lock xadd qword [rdi + EXS_OBJ_RC], rax	; rax = the OLD value
	test	rax, rax
	jz	exsrt_abort_resurrectio		; old == 0: under destruction
	cmp	rax, -1
	je	exsrt_abort_saturatio		; old == 2^64-1: this retain carried
	ret

; exsrt_release_c(o: ptr) -> void -- atomic
exsrt_release_c:
	mov	rax, -1
	lock xadd qword [rdi + EXS_OBJ_RC], rax	; rax = the OLD value
	test	rax, rax
	jz	exsrt_abort_resurrectio		; old == 0: released twice
	cmp	rax, 1
	je	.destroy			; old == 1: this release destroys
	ret
  .destroy:
	mov	rax, [rdi + EXS_OBJ_DTOR]
	test	rax, rax
	jz	.nodtor
	push	rbp
	mov	rbp, rsp
	call	rax
	pop	rbp
  .nodtor:
	ret

; =============================================================================
; alloc -- runtime.md 2.6; spec 6.1, 6.3
; =============================================================================
; An mmap-backed bump arena, fixed capacity, O(1) reset, no brk, no libc:
; rt/arena.inc's design RE-STATED for the program rather than reused, because
; runtime.md constraint 3 forbids this file from including rt/.
;
; The ExsArena record lives at the head of its own mapping, so an arena is one
; mmap and one pointer and there is no static arena storage to make
; `m.alloc(n)` non-reentrant. `dimitte` recovers the length as limit - record.
;
; The surface spelling of the derivation -- `m.alloc(n)`? a capacity in what
; unit? -- is [OPEN] (runtime.md 8); spec 4.5 says only that `sub alloc = a`
; binds an arena. Growth is [UNIMPLEMENTED], for the reasons rt/arena.inc
; gives.

if EXS_POTESTAS_ALLOC

; bfausr_exsrt_alloc_novum(m: ptr, n: u64) -> ptr
;   A new arena with room for at least `n` payload bytes. Returns the
;   ExsArena*. `m` is the Mundus carrier and is unread today: the authority it
;   represents was checked at compile time (spec 4.1 rule 1), and the runtime
;   record carries no magic word and performs no check (runtime.md 2.2).
;   The mapping length is carried in `rsi` -- which is also mmap's `len`
;   argument -- and read back after the syscall, because the kernel preserves
;   every register except rax, rcx and r11. See the note on `scribe` above:
;   an `r11` used the same way is exactly the defect this file was built to
;   find.
bfausr_exsrt_alloc_novum:
	add	rsi, EXS_ARENA_PAYLOAD + 4095
	jc	exsrt_abort_arena		; the requested size wrapped
	and	rsi, -4096			; whole pages; rsi = total = len
	xor	edi, edi			; addr = 0: kernel chooses
	mov	edx, 3				; PROT_READ|PROT_WRITE
	mov	r10d, 0x22			; MAP_PRIVATE|MAP_ANONYMOUS
	mov	r8d, -1				; fd
	xor	r9d, r9d			; offset
	mov	eax, 9				; mmap
	syscall
	cmp	rax, -4096
	ja	exsrt_abort_arena		; no arena is not a value a program
						; can handle; runtime.md 2.6 gives
						; exhaustion kind 4 and this is the
						; same failure one step earlier
	lea	r10, [rax + EXS_ARENA_PAYLOAD]
	mov	[rax + EXS_ARENA_BASE], r10
	mov	[rax + EXS_ARENA_CUR], r10
	add	rsi, rax
	mov	[rax + EXS_ARENA_LIMIT], rsi
	ret

; bfausr_exsrt_alloc_da(a: ptr, n: u64, align: u64) -> ptr
;   Bump. `align` is a precondition, not a check: it must be a power of two
;   and at least 1. The emitter passes a constant computed from the type's
;   layout, so a runtime test would price every allocation for a mistake only
;   the compiler can make. A `refero<T>` is this call plus the ExsObj header.
bfausr_exsrt_alloc_da:
	mov	rax, [rdi + EXS_ARENA_CUR]
	mov	r8, rdx
	dec	r8
	add	rax, r8
	jc	exsrt_abort_arena
	not	r8
	and	rax, r8				; cur rounded up to `align`
	mov	r9, rax
	add	r9, rsi
	jc	exsrt_abort_arena
	cmp	r9, [rdi + EXS_ARENA_LIMIT]
	ja	exsrt_abort_arena
	mov	[rdi + EXS_ARENA_CUR], r9
	ret

; bfausr_exsrt_alloc_reconde(a: ptr) -> void
;   Spec 6.3 decision 1: reset in O(1). Destructors are NOT run here -- the
;   count runs them (spec 6.6); this returns the bytes.
bfausr_exsrt_alloc_reconde:
	mov	rax, [rdi + EXS_ARENA_BASE]
	mov	[rdi + EXS_ARENA_CUR], rax
	ret

; bfausr_exsrt_alloc_dimitte(a: ptr) -> void
;   munmap the whole mapping, record included. `a` is dangling afterwards.
bfausr_exsrt_alloc_dimitte:
	mov	rsi, [rdi + EXS_ARENA_LIMIT]
	sub	rsi, rdi			; length = limit - record address
	mov	eax, 11				; munmap
	syscall
	ret

end if	; EXS_POTESTAS_ALLOC

; =============================================================================
; abort -- runtime.md 2.5; spec 6.6 (as amended)
; =============================================================================
; One routine, one shape, for every runtime abort. CORE: always assembled, and
; its `write` to fd 2 is the one syscall every program has besides
; `exit_group` (runtime.md 2.6's core row).
;
; Why SIGILL and not exit_group(N): `initium` returns u8, so every status
; 0-255 is a legitimate program result and none can mean "aborted". A signal
; death is the only channel a parent can tell apart, and it is the convention
; every trap in this tree already uses (macros/assert.inc, bfausr_trap).
;
; Why a message: tests/run.sh is a shell and sees 132 for both `ud2` and
; `redde 132;`, so the discriminator spec 14 entry 15's `shape=abort` needs is
; the line on fd 2.
;
; Why the kind is a number: spec 8.3's rule -- codes permanent, text not,
; tools match codes -- applied to the runtime. `abortus N` is the code; the
; English before it is not promised.
;
; THE MESSAGE NEVER CONTAINS PROGRAM DATA -- no source, no bytes from a
; textus, no address -- so spec 8.3's escaping rule holds vacuously rather
; than by an escaper this file would have to carry.

exsrt_abort_saturatio:
	mov	edi, EXS_ABORTUS_SATURATIO
	jmp	exsrt_abort

exsrt_abort_resurrectio:
	mov	edi, EXS_ABORTUS_RESURRECTIO
	jmp	exsrt_abort

exsrt_abort_arena:
	mov	edi, EXS_ABORTUS_ARENA
	jmp	exsrt_abort

; The bound of a `dum ... terminus N` reached. Spec 8.5: "the entry that would
; be the N+1th does not happen -- it is a runtime abort (6.6's shape)". The
; emitter's `trap terminus` (docs/design/lowering.md, the `dum` lowering)
; jumps here rather than folding into kind 1.
exsrt_abort_terminus:
	mov	edi, EXS_ABORTUS_TERMINUS
	jmp	exsrt_abort

; exsrt_abort(kind: u32 in edi) -- never returns.
;   Writes `exsecutor: abortus <kind>\n` to fd 2 in ONE write, then ud2.
;   One write rather than three (runtime.md 2.5 sketched three) so that the
;   line cannot interleave with another writer's and so that the binary
;   carries one syscall site here instead of three; the bytes on fd 2 are
;   identical either way. Clobbers freely: it never returns.
exsrt_abort:
	mov	r8, rdi				; kind
	lea	r9, [exsrt_abortus_calc + 20]	; one past the scratch digits
	mov	rax, r8
	mov	r10, 10
	xor	ecx, ecx			; digit count
  .digit:
	xor	edx, edx
	div	r10				; rax = quotient, rdx = remainder
	add	dl, '0'
	dec	r9
	mov	[r9], dl
	inc	rcx
	test	rax, rax
	jnz	.digit
	lea	r11, [exsrt_abortus_numerus]	; immediately after the prefix
	mov	rdx, rcx
  .copy:
	mov	al, [r9]
	mov	[r11], al
	inc	r9
	inc	r11
	dec	rdx
	jnz	.copy
	mov	byte [r11], 10			; the newline
	lea	rdx, [rcx + EXS_ABORTUS_PRAEFIXUM_LEN + 1]
	mov	edi, 2				; fd 2 -- see runtime.md 2.6's core row
	lea	rsi, [exsrt_abortus_linea]
	mov	eax, 1				; write
	syscall
	; ud2 -> SIGILL -> the shell sees 132. There is no symbolic mnemonic for
	; it in vendor/fasmg-x86; `db 0x0F, 0x0B` is what macros/assert.inc and
	; emit.inc's own bfausr_trap already emit.
	db	0x0F, 0x0B

; ---- end of the prelude blob ------------------------------------------------
