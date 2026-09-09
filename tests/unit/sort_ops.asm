; tests/unit/sort_ops.asm
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
; asm-rt fixture for compiler/x86_64/rt/sort.inc -- general correctness.
; The stability property gets its OWN dedicated fixture,
; tests/unit/sort_stable.asm (required, not optional, per this wave's
; brief), because it is the property this file exists for and deserves to
; be checked on its own, not folded into a general-correctness fixture
; where a regression could hide behind everything else passing.
;
;   1. an unsorted 6-element array sorts to ascending order
;   2. an already-sorted array stays sorted (an easy-to-get-backwards edge
;      case for a merge that assumes "always something to merge")
;   3. a reverse-sorted array sorts to ascending order
;   4. an ODD count (7 -- not a power of 2) sorts correctly: exercises the
;      "final run shorter than `width`" path in the bottom-up merge every
;      power-of-2 count would skip
;   5. count=1 (trivial: already "sorted", must not touch anything wrong)
;      and count=0 (must not crash -- no elements, no comparator calls)
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/sort.inc'

; Elements are single dwords (stride=4), ascending. NOTE the double
; dereference: `a`/`b` are POINTERS TO elements (rt/sort.inc's header
; warns about this specifically) -- `[a]` is the pointer, `[[a]]` (via an
; intermediate register) is the element.
proc cmp_int32, a, b
	locals
	endl
	mov	rcx, [a]
	mov	eax, [rcx]
	mov	rdx, [b]
	mov	ecx, [rdx]
	cmp	eax, ecx
	jl	.lt
	jg	.gt
	xor	eax, eax
	return
  .lt:
	mov	eax, -1
	return
  .gt:
	mov	eax, 1
	return
endp

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail0

	; check 1: unsorted -> ascending
	lea	rdi, [ar]
	lea	rsi, [arr1]
	mov	rdx, 6
	mov	rcx, 4
	lea	r8, [cmp_int32]
	call	sort_stable
	mov	eax, [arr1+0]
	cmp	eax, 1
	jne	.fail1
	mov	eax, [arr1+4]
	cmp	eax, 1
	jne	.fail1
	mov	eax, [arr1+8]
	cmp	eax, 2
	jne	.fail1
	mov	eax, [arr1+12]
	cmp	eax, 3
	jne	.fail1
	mov	eax, [arr1+16]
	cmp	eax, 4
	jne	.fail1
	mov	eax, [arr1+20]
	cmp	eax, 5
	jne	.fail1

	; check 2: already sorted
	lea	rdi, [ar]
	lea	rsi, [arr2]
	mov	rdx, 5
	mov	rcx, 4
	lea	r8, [cmp_int32]
	call	sort_stable
	mov	eax, [arr2+0]
	cmp	eax, 10
	jne	.fail2
	mov	eax, [arr2+16]
	cmp	eax, 50
	jne	.fail2

	; check 3: reverse sorted
	lea	rdi, [ar]
	lea	rsi, [arr3]
	mov	rdx, 5
	mov	rcx, 4
	lea	r8, [cmp_int32]
	call	sort_stable
	mov	eax, [arr3+0]
	cmp	eax, 10
	jne	.fail3
	mov	eax, [arr3+16]
	cmp	eax, 50
	jne	.fail3

	; check 4: odd count (7), exercises the trailing-partial-run path
	lea	rdi, [ar]
	lea	rsi, [arr4]
	mov	rdx, 7
	mov	rcx, 4
	lea	r8, [cmp_int32]
	call	sort_stable
	mov	esi, 1
  .check4loop:
	cmp	esi, 7
	jge	.check4pass
	mov	eax, [arr4 + rsi*4]
	mov	ecx, [arr4 + rsi*4 - 4]
	cmp	ecx, eax
	jg	.fail4
	inc	esi
	jmp	.check4loop
  .check4pass:

	; check 5: count=1, then count=0 (must not crash)
	lea	rdi, [ar]
	lea	rsi, [arr5]
	mov	rdx, 1
	mov	rcx, 4
	lea	r8, [cmp_int32]
	call	sort_stable
	mov	eax, [arr5]
	cmp	eax, 42
	jne	.fail5
	lea	rdi, [ar]
	lea	rsi, [arr5]
	mov	rdx, 0
	mov	rcx, 4
	lea	r8, [cmp_int32]
	call	sort_stable

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail0: mov eax,231
	mov edi,10
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

segment readable writeable
  ar: rb sizeof.Arena
  arr1 dd 5,3,1,4,1,2
  arr2 dd 10,20,30,40,50
  arr3 dd 50,40,30,20,10
  arr4 dd 7,1,9,3,3,5,2
  arr5 dd 42
