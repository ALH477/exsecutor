; tests/unit/str_ops.asm
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
; asm-rt fixture for compiler/x86_64/rt/str.inc (positive paths). The
; out-of-range `str_slice` trap is a separate NEGATIVE fixture --
; tests/unit/str_slice_bounds.asm -- because it terminates the process.
;
;   1. `str_slice`: "hello world"[6..11] == "world" -- pure arithmetic,
;      verified by comparing the sliced bytes against the expected content
;   2. `str_eq`: equal spans (different addresses, same bytes) match;
;      unequal-length spans do not; equal-length-but-different-bytes spans
;      do not
;   3. `str_copy`: the copy lands at a genuinely different address than
;      the source, and its content matches exactly
;   4. `str_to_cstr`: the copy's content matches, AND it carries a `0`
;      byte immediately after the `len` copied bytes
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/str.inc'

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 65536
	call	arena_init
	jc	.fail0

	; check 1: str_slice is pure arithmetic
	lea	rdi, [s_hw]
	mov	rsi, 11
	mov	rdx, 6
	mov	rcx, 5
	call	str_slice
	cmp	rdx, 5
	jne	.fail1
	mov	rsi, rax
	lea	rdi, [s_world]
	mov	rcx, 5
	cld
	repe	cmpsb
	jne	.fail1

	; check 2: str_eq
	lea	rdi, [s_world]
	mov	rsi, 5
	lea	rdx, [s_world2]
	mov	rcx, 5
	call	str_eq
	cmp	eax, 1
	jne	.fail2
	lea	rdi, [s_world]
	mov	rsi, 5
	lea	rdx, [s_wo]
	mov	rcx, 2
	call	str_eq
	cmp	eax, 0
	jne	.fail2
	lea	rdi, [s_world]
	mov	rsi, 5
	lea	rdx, [s_xorld]
	mov	rcx, 5
	call	str_eq
	cmp	eax, 0
	jne	.fail2

	; check 3: str_copy -- independent buffer, identical content
	lea	rdi, [ar]
	lea	rsi, [s_world]
	mov	rdx, 5
	call	str_copy
	mov	rbx, rax
	lea	rcx, [s_world]
	cmp	rbx, rcx
	je	.fail3			; must be a genuinely different address
	mov	rsi, rbx
	lea	rdi, [s_world]
	mov	rcx, 5
	cld
	repe	cmpsb
	jne	.fail3

	; check 4: str_to_cstr -- content matches, NUL-terminated
	lea	rdi, [ar]
	lea	rsi, [s_world]
	mov	rdx, 5
	call	str_to_cstr
	mov	rbx, rax
	mov	rsi, rbx
	lea	rdi, [s_world]
	mov	rcx, 5
	cld
	repe	cmpsb
	jne	.fail4
	cmp	byte [rbx+5], 0
	jne	.fail4

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

segment readable writeable
  ar: rb sizeof.Arena
  s_hw db 'hello world'
  s_world db 'world'
  s_world2 db 'world'
  s_wo db 'wo'
  s_xorld db 'xorld'
