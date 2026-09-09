; tests/unit/vec_push_grow.asm
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
; asm-rt fixture for compiler/x86_64/rt/vec.inc (positive paths). The
; out-of-bounds `vec_get` trap is a separate NEGATIVE fixture --
; tests/unit/vec_bounds.asm -- because it terminates the process.
;
;   1. `vec_init` with `initial_cap = 0`, then ten `vec_push`es: doubling
;      growth from empty (0 -> 1 -> 2 -> 4 -> 8 -> 16), `len` ends at 10,
;      `cap` at 16, and every value written through a returned slot pointer
;      survives every intervening growth-copy
;   2. `vec_get` returns a pointer to the right element for every pushed
;      index
;   3. `vec_init` with a non-zero `initial_cap`, confirming the
;      backing-buffer-already-allocated path (distinct code path from
;      check 1's empty start) also works
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/vec.inc'

segment readable executable
  start:
	; ---- checks 1-2: grow from empty, values survive, get() is correct
	lea	rdi, [ar]
	mov	rsi, 65536
	call	arena_init
	jc	.fail1
	lea	rdi, [v]
	lea	rsi, [ar]
	mov	rdx, 8			; stride = 8 (one qword per element)
	xor	rcx, rcx		; initial_cap = 0
	call	vec_init
	jc	.fail1

	mov	r13, 0
  .pushloop:
	lea	rdi, [v]
	call	vec_push
	jc	.fail1
	mov	rcx, r13
	add	rcx, 100
	mov	[rax], rcx
	inc	r13
	cmp	r13, 10
	jl	.pushloop

	mov	rax, [v + Vec.len]
	cmp	rax, 10
	jne	.fail1
	mov	rax, [v + Vec.cap]
	cmp	rax, 16
	jne	.fail1

	mov	r13, 0
  .checkloop:
	lea	rdi, [v]
	mov	rsi, r13
	call	vec_get
	jc	.fail2
	mov	rcx, r13
	add	rcx, 100
	cmp	[rax], rcx
	jne	.fail2
	inc	r13
	cmp	r13, 10
	jl	.checkloop

	; ---- check 3: vec_init with a non-zero initial_cap
	lea	rdi, [v2]
	lea	rsi, [ar]
	mov	rdx, 8
	mov	rcx, 4
	call	vec_init
	jc	.fail3
	lea	rdi, [v2]
	call	vec_push
	jc	.fail3
	mov	qword [rax], 111
	lea	rdi, [v2]
	call	vec_push
	jc	.fail3
	mov	qword [rax], 222
	mov	rax, [v2 + Vec.len]
	cmp	rax, 2
	jne	.fail3
	mov	rax, [v2 + Vec.cap]
	cmp	rax, 4			; unchanged -- no growth needed for 2 <= 4
	jne	.fail3
	lea	rdi, [v2]
	xor	rsi, rsi
	call	vec_get
	jc	.fail3
	cmp	qword [rax], 111
	jne	.fail3

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

segment readable writeable
  ar: rb sizeof.Arena
  v: rb sizeof.Vec
  v2: rb sizeof.Vec
