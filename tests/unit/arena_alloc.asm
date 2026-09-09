; tests/unit/arena_alloc.asm
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
; asm-rt fixture for compiler/x86_64/rt/arena.inc (positive paths). The
; exhaustion `rassert` trap is a separate NEGATIVE fixture --
; tests/unit/arena_exhausted.asm -- because it terminates the process.
;
;   1. arena_init: base/cur start equal and non-null, limit - base ==
;      the requested capacity
;   2. arena_alloc: two allocations are distinct and 8-byte aligned (a
;      10-byte request advances the bump pointer by 16, not 10)
;   3. arena_reset: rewinds cur to base (O(1) -- no syscall), and the next
;      arena_alloc reuses the first allocation's exact address
;   4. arena_destroy succeeds
;   5. arena_init failure is PROPAGATED, not re-encoded: an absurdly large
;      requested capacity makes the underlying sys_mmap fail, and
;      arena_init must return CF set with eax in the small-negative-errno
;      range sys_mmap itself would report -- this is the "host/OS syscall
;      failure, scoped to rt/sys.inc wrappers, forwarded verbatim by
;      callers that merely wrap one" channel this wave's report documents
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/arena.inc'

segment readable executable
  start:
	; check 1: init
	lea	rdi, [a]
	mov	rsi, 4096
	call	arena_init
	jc	.fail1
	mov	rax, [a + Arena.base]
	test	rax, rax
	jz	.fail1
	mov	rcx, [a + Arena.cur]
	cmp	rcx, rax
	jne	.fail1
	mov	rdx, [a + Arena.limit]
	sub	rdx, rax
	cmp	rdx, 4096
	jne	.fail1

	; check 2: alignment
	lea	rdi, [a]
	mov	rsi, 10
	call	arena_alloc
	jc	.fail2
	mov	rbx, rax		; p1
	lea	rdi, [a]
	mov	rsi, 20
	call	arena_alloc
	jc	.fail2
	mov	r12, rax		; p2
	cmp	r12, rbx
	jle	.fail2
	mov	rax, r12
	sub	rax, rbx
	cmp	rax, 16			; 10 rounds up to 16 (8-byte aligned)
	jne	.fail2

	; check 3: reset + reuse
	lea	rdi, [a]
	call	arena_reset
	jc	.fail3
	mov	rax, [a + Arena.cur]
	cmp	rax, [a + Arena.base]
	jne	.fail3
	lea	rdi, [a]
	mov	rsi, 8
	call	arena_alloc
	jc	.fail3
	cmp	rax, rbx
	jne	.fail3

	; check 4: destroy
	lea	rdi, [a]
	call	arena_destroy
	jc	.fail4

	; check 5: init failure is propagated from sys_mmap, not re-encoded
	lea	rdi, [b]
	mov	rsi, 0x7FFFFFFFFFFF0000
	call	arena_init
	jnc	.fail5
	cmp	eax, 0
	jge	.fail5
	cmp	eax, -4096
	jle	.fail5

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

segment readable writeable
  a: rb sizeof.Arena
  b: rb sizeof.Arena
