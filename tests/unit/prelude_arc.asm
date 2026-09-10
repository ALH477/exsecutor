; tests/unit/prelude_arc.asm
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
; -----------------------------------------------------------------------------
; The ARC routines of docs/design/runtime.md 2.5, assembled alone and run.
;
; The happy path first, then the abort, because a fixture that only dies
; proves nothing about the code that was supposed to run before it. In order:
;
;   1. retain 1 -> 2, release 2 -> 1, no destructor.
;   2. release 1 -> 0 runs the destructor EXACTLY ONCE, and the destructor
;      observes rc == 0 while it runs. That is runtime.md 2.5's whole
;      mechanism: rc == 0 means UNDER DESTRUCTION, which is what makes spec
;      6.6's resurrection detectable at one compare.
;   3. the atomic pair, exsrt_retain_c / exsrt_release_c, round-trips the
;      same count (`lock xadd`, old value tested).
;   4. rc is fast-forwarded to 2^64-1 and retained once. Spec 6.5 as amended:
;      the retain that would carry out of 64 bits ABORTS -- saturation is the
;      event, not a state the program continues in. So: `abortus 2` on fd 2
;      and SIGILL, which tests/run.sh's shell reports as 132 (the same 132
;      tests/unit/rassert_trap.asm and arena_exhausted.asm already expect).
;
; If step 4's retain ever RETURNED, this fixture would exit 37 instead, so
; expect-exit=132 is a claim about the abort and not about any old crash.
; Steps 1-3 have their own exit codes (31-36) for the same reason.
;
; THE MESSAGE IS NOT CHECKED HERE. tests/run.sh compares an exit status and
; nothing else, and 132 alone cannot tell `ud2` from `redde 132;` -- which is
; exactly why runtime.md 2.5 puts a line on fd 2. The `abortus 2` line is
; verified by hand and quoted in the prelude agent's report; spec 14 entry
; 15's shape=abort is what will check it in the suite.
;
; AMBITUS IS 0 HERE, deliberately. The ARC routines carry no syscall and are
; always assembled; with no `ambitus` in the closure the `if` blocks drop
; `scribe`'s `write` and this binary's syscall surface is exactly runtime.md
; 2.6's CORE row -- `write` (from exsrt_abort) and `exit_group`, nothing else.
; That is hazard H2 checked rather than promised.
;
; Exit codes: 132 = the saturation abort (the point). 31-37 = one of the
; steps above did not hold; see the labels.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry exsrt_start

EXS_POTESTAS_MUNDUS	= 1
EXS_POTESTAS_ALLOC	= 0
EXS_POTESTAS_SERMO	= 0
EXS_POTESTAS_HOROLOGIUM	= 0
EXS_POTESTAS_ARCHIVUM	= 0
EXS_POTESTAS_RETE	= 0
EXS_POTESTAS_FORTUNA	= 0
EXS_POTESTAS_AMBITUS	= 0
EXS_POTESTAS_FILUM	= 0
EXS_POTESTAS_MACHINA	= 0
EXS_POTESTAS_CRUDUM	= 0
EXS_MXCSR		= 0x1F80

segment readable executable
include '../../compiler/x86_64/prelude/prelude.asm'
include '../../compiler/x86_64/prelude/interface.inc'

bfausr_initium:
	push	rbp
	mov	rbp, rsp

	; 1. retain / release without a destructor.
	lea	rdi, [exsfx_obj_b]
	call	exsrt_retain
	cmp	qword [exsfx_obj_b + EXS_IFACE_OBJ_RC], 2
	jne	.e31
	lea	rdi, [exsfx_obj_b]
	call	exsrt_release
	cmp	qword [exsfx_obj_b + EXS_IFACE_OBJ_RC], 1
	jne	.e32

	; 2. release to zero: the destructor runs once, with rc == 0.
	lea	rdi, [exsfx_obj_a]
	call	exsrt_release
	cmp	qword [exsfx_dtor_ictus], 1
	jne	.e33
	cmp	qword [exsfx_dtor_rc], 0
	jne	.e34
	cmp	qword [exsfx_obj_a + EXS_IFACE_OBJ_RC], 0
	jne	.e35

	; 3. the atomic pair round-trips.
	lea	rdi, [exsfx_obj_b]
	call	exsrt_retain_c
	lea	rdi, [exsfx_obj_b]
	call	exsrt_release_c
	cmp	qword [exsfx_obj_b + EXS_IFACE_OBJ_RC], 1
	jne	.e36

	; 4. fast-forward to 2^64-1 and retain once. This must not return.
	mov	rax, -1
	mov	[exsfx_obj_b + EXS_IFACE_OBJ_RC], rax
	lea	rdi, [exsfx_obj_b]
	call	exsrt_retain

	mov	eax, 37			; reached only if the retain returned
	pop	rbp
	ret
  .e31:
	mov	eax, 31
	pop	rbp
	ret
  .e32:
	mov	eax, 32
	pop	rbp
	ret
  .e33:
	mov	eax, 33
	pop	rbp
	ret
  .e34:
	mov	eax, 34
	pop	rbp
	ret
  .e35:
	mov	eax, 35
	pop	rbp
	ret
  .e36:
	mov	eax, 36
	pop	rbp
	ret

; The destructor exsrt_release dispatches to. SysV, rdi = the object, and it
; runs while the object's count is 0 (IR 2.8 has it release its own fields;
; this one only records what it saw).
exsfx_dtor:
	push	rbp
	mov	rbp, rsp
	inc	qword [exsfx_dtor_ictus]
	mov	rax, [rdi + EXS_IFACE_OBJ_RC]
	mov	[exsfx_dtor_rc], rax
	pop	rbp
	ret

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'

; ExsObj { rc u64 @0, dtor ptr @8 } -- runtime.md 2.5, offsets read through
; interface.inc above (runtime.md H4).
exsfx_obj_a:
	dq	1
	dq	exsfx_dtor
exsfx_obj_b:
	dq	1
	dq	0
exsfx_dtor_ictus:
	dq	0
exsfx_dtor_rc:
	dq	0
