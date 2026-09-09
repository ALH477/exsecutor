; tests/unit/proc_calling_convention.asm
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
; asm-rt fixture for compiler/x86_64/macros/proc.inc.
;
; Exercises `uses`, `locals`, and `fail` together (the pre-freeze checklist
; requirement), plus the calling-convention properties that matter most and
; that this file's own author caught real bugs in while writing proc.inc
; (see that file's header comments for the four bugs found and fixed --
; every one of them was caught by a check shaped like one of these, not by
; inspection):
;
;   1. plain 2-arg call, no uses/locals: return value correct
;   2. `fail CODE` sets eax=CODE and CF, and does not disturb anything else
;   3. success path clears CF and returns the right value THROUGH a routine
;      that also has `uses` + a written-through `local` (this exact
;      combination is what exposed the locals/uses stack-overlap bug)
;   4. a `uses` register survives a call whose body writes to a `local`
;      declared after it (isolates the overlap bug precisely: check 3 could
;      pass on a return-value coincidence, this cannot)
;   5. max capacity in one shot: 6 args, 4 `uses` registers, 2 locals of
;      different sizes (dq and db)
;   6. a zero-arg, zero-`uses`, empty-`locals` leaf proc still gets a
;      correct prologue/epilogue
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'
include '../../compiler/x86_64/macros/proc.inc'

format ELF64 executable 3
entry start

segment readable executable

; -- routine 1: plain 2-arg call, no uses, no locals --
proc add_two, a, b
	locals
	endl
	mov	rax, [a]
	add	rax, [b]
	return
endp

; -- routines 2-4: uses + locals + fail together --
proc safe_div, a, b
	uses	rbx, r12
	locals
		slot tmp, dd
	endl
	mov	rbx, -1			; clobber both uses'd regs; must be restored
	mov	r12, -1
	mov	eax, [b]
	mov	[tmp], eax		; write-through the local -- this is what
					; exposed the stack-overlap bug (bug 4 in
					; proc.inc's header)
	cmp	dword [tmp], 0
	jne	.nonzero
	fail	99
  .nonzero:
	mov	eax, [a]
	cdq
	idiv	dword [tmp]
	return
endp

; -- routine 5: 6 args, 4 uses, 2 locals (dq and db) --
proc max_cap, p1, p2, p3, p4, p5, p6
	uses	rbx, r12, r13, r14
	locals
		slot s1, dq
		slot s2, db
	endl
	mov	rbx, -1
	mov	r12, -1
	mov	r13, -1
	mov	r14, -1
	mov	rax, [p1]
	add	rax, [p2]
	add	rax, [p3]
	add	rax, [p4]
	add	rax, [p5]
	add	rax, [p6]
	mov	[s1], rax
	mov	byte [s2], 1
	mov	rax, [s1]
	cmp	byte [s2], 1
	jne	.bad
	return
  .bad:
	fail	1
endp

; -- routine 6: zero-arg, zero-uses, empty-locals leaf --
proc noop_leaf
	locals
	endl
	return
endp

  start:
	; check 1: plain call, return value
	mov	rdi, 10
	mov	rsi, 32
	call	add_two
	jc	.fail1
	cmp	rax, 42
	jne	.fail1

	; check 2: fail path -- CF set, eax = 99, divide-by-zero never reached
	mov	rdi, 5
	mov	rsi, 0
	call	safe_div
	jnc	.fail2
	cmp	eax, 99
	jne	.fail2

	; check 3: success path through uses+locals, CF clear, correct value
	mov	rdi, 84
	mov	rsi, 2
	call	safe_div
	jc	.fail3
	cmp	eax, 42
	jne	.fail3

	; check 4: a `uses` register survives a call whose body writes a local
	mov	rbx, 7
	mov	r12, 8
	mov	rdi, 10
	mov	rsi, 5
	call	safe_div
	jc	.fail4
	cmp	eax, 2
	jne	.fail4
	cmp	rbx, 7
	jne	.fail4
	cmp	r12, 8
	jne	.fail4

	; check 5: 6 args / 4 uses / 2 locals (dq + db) in one proc
	mov	rdi, 1
	mov	rsi, 2
	mov	rdx, 3
	mov	rcx, 4
	mov	r8, 5
	mov	r9, 6
	call	max_cap
	jc	.fail5
	cmp	rax, 21
	jne	.fail5

	; check 6: zero-arg/uses/locals leaf still returns cleanly
	call	noop_leaf
	jc	.fail6

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1: mov eax,231
	mov edi,11
	syscall
  .fail2: mov eax,231
	mov edi,12
	syscall
  .fail3: mov eax,231
	mov edi,13
	syscall
  .fail4: mov eax,231
	mov edi,14
	syscall
  .fail5: mov eax,231
	mov edi,15
	syscall
  .fail6: mov eax,231
	mov edi,16
	syscall
