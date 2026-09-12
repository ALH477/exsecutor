; tests/unit/bfa_emit_narrow_addr.asm
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
; reference-backend fixture -- `__bfa_mem_access` refuses an ADDRESS narrower
; than 64 bits BY NAME (ADR 0015 decision 2; docs/design/c-backend.md finding
; 18, third bullet).
;
; WHAT WAS THERE BEFORE. finding 18 said the reference emitter "would
; silently miscompile" a module whose `ptr` was interned at 32. It would not:
; `__bfa_mem_access` dispatches on width, a 32-bit type takes the `.narrow`
; arm, and PTR is neither `BFA_TK_U` nor `BFA_TK_I`, so it already died. But
; it died saying "a width that is not a whole number of bytes", which is
; factually WRONG about 32 -- four of them -- and a wrong message is what the
; next person would have had to work back from. This fixture pins the message
; that replaces it, and the `.addr` arm that produces it.
;
; WHY IT IS A UNIT FIXTURE AND NOT A `tests/ir/` ONE. The IR text grammar
; cannot express this input: `parse.inc:446` has `ptr` as a bare literal
; calling `bfa_ty_ptr(module)`, which takes no width. So there is no `.ir`
; file that reaches the arm, and a refusal nothing can reach is a refusal
; nobody has tested. Interning the type directly through `bfa_type_intern` --
; the same door `bfa_ir_builders.asm` uses to build inputs the parser cannot
; -- is what makes it reachable.
;
; WHY THE ARM EXISTS AT ALL, given that ADR 0015 decision 2 interns `ptr`,
; `ref` and `refc` at 64 on EVERY `--hospes` row, so the lowering cannot
; produce this: it is defence in depth for the change that would otherwise be
; unsafe. A future row that made a pointer width a target fact would find a
; backend that names the problem instead of one that mis-describes it. That
; is the difference between a change being possible and a change being safe.
;
; Checks, in order:
;
;   1. `ptr` at 64 answers 0 -- "the 64-bit access Tier 2 always had" -- so
;      the refusal is not too broad. Exit 21 if not.
;   2. `u32` answers 32 -- the WIDTH IN BITS, not a byte count, which is
;      what this routine returns for a narrow integer -- so `.narrow`'s
;      integer arm is untouched.
;      Exit 22 if not.
;   3. `ptr` at 32 DIES: exit 4, and "an address narrower than 64 bits" on
;      stderr.
;
; Check 3 is the last thing the fixture does, because `__bfa_emit_die` exits
; the process. An exit of 4 alone does not distinguish the new arm from the
; old `.bad` one -- both die 4 -- so, exactly as `tests/ir/reject_c_arc.ir`
; does for the same reason, the refusal TEXT is written to stderr and read
; there rather than inferred from a status.
;
; Mutation (run): the three `.addr` compares deleted so a 32-bit `ptr` falls
; through to `.bad` -> still exit 4, and stderr says "a width that is not a
; whole number of bytes", which is the wrong message this fixture exists to
; retire. Caught by reading the line, not by the status.
;
; TEST: run=yes expect-exit=4 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/span.inc'
include '../../compiler/x86_64/backend_fasmg/emit.inc'

segment readable executable
  start:
	lea	rdi, [nar_ar]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.fail20

	lea	rdi, [nar_mod]
	lea	rsi, [nar_ar]
	call	bfa_module_init

	; ---- 1. `ptr` at 64 -> 0, the 64-bit form ---------------------------
	lea	rdi, [nar_mod]
	mov	esi, BFA_TK_PTR
	xor	edx, edx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	mov	esi, eax
	lea	rdi, [nar_mod]
	call	__bfa_mem_access
	test	eax, eax
	jnz	.fail21

	; ---- 2. `u32` -> 32, the integer arm of `.narrow` -------------------
	lea	rdi, [nar_mod]
	mov	esi, BFA_TK_U
	xor	edx, edx
	mov	ecx, 32
	xor	r8, r8
	call	bfa_type_intern
	mov	esi, eax
	lea	rdi, [nar_mod]
	call	__bfa_mem_access
	cmp	eax, 32
	jne	.fail22

	; ---- 3. `ptr` at 32 -> dies, exit 4 ---------------------------------
	lea	rdi, [nar_mod]
	mov	esi, BFA_TK_PTR
	xor	edx, edx
	mov	ecx, 32
	xor	r8, r8
	call	bfa_type_intern
	mov	esi, eax
	lea	rdi, [nar_mod]
	call	__bfa_mem_access

	; reached only if the refusal did not fire
	mov	edi, 23
	call	sys_exit_group
  .fail20:
	mov	edi, 20
	call	sys_exit_group
  .fail21:
	mov	edi, 21
	call	sys_exit_group
  .fail22:
	mov	edi, 22
	call	sys_exit_group

segment readable writeable
  nar_ar:	rb sizeof.Arena
  nar_mod:	rb sizeof.BfaModule
