; tests/unit/intern_dedup.asm
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
; asm-rt fixture for compiler/x86_64/rt/intern.inc (positive paths). The
; out-of-range `intern_bytes` trap is a separate NEGATIVE fixture --
; tests/unit/intern_bounds.asm -- because it terminates the process.
;
;   1. the first interned string gets id 1
;   2. the SAME content, at a DIFFERENT memory address, gets the SAME id
;      (this is the whole point: identity is content, not a pointer)
;   3. different content gets id 2, then a third distinct string gets id 3
;      (monotonic, first-seen order)
;   4. `intern_bytes(1)` returns a pointer/length whose bytes match the
;      original content exactly
;   5. `intern_bytes(3)` likewise, for the third string
;   6. re-interning already-seen content (id 2's "world") returns 2 again,
;      not a fresh id -- `intern_id` is `map_get`-then-insert-on-miss, not
;      unconditional insert
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/intern.inc'

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 65536
	call	arena_init
	jc	.fail0
	lea	rdi, [it]
	lea	rsi, [ar]
	mov	rdx, 8
	call	intern_init
	jc	.fail0

	; check 1
	lea	rdi, [it]
	lea	rsi, [s_hello]
	mov	rdx, 5
	call	intern_id
	cmp	rax, 1
	jne	.fail1

	; check 2: same bytes, different address -> same id
	lea	rdi, [it]
	lea	rsi, [s_hello2]
	mov	rdx, 5
	call	intern_id
	cmp	rax, 1
	jne	.fail2

	; check 3: monotonic ids for distinct content
	lea	rdi, [it]
	lea	rsi, [s_world]
	mov	rdx, 5
	call	intern_id
	cmp	rax, 2
	jne	.fail3
	lea	rdi, [it]
	lea	rsi, [s_exsecutor]
	mov	rdx, 9
	call	intern_id
	cmp	rax, 3
	jne	.fail3

	; check 4: intern_bytes(1) content round-trips
	lea	rdi, [it]
	mov	rsi, 1
	call	intern_bytes
	cmp	rdx, 5
	jne	.fail4
	mov	rsi, rax
	lea	rdi, [s_hello]
	mov	rcx, 5
	cld
	repe	cmpsb
	jne	.fail4

	; check 5: intern_bytes(3) content round-trips
	lea	rdi, [it]
	mov	rsi, 3
	call	intern_bytes
	cmp	rdx, 9
	jne	.fail5
	mov	rsi, rax
	lea	rdi, [s_exsecutor]
	mov	rcx, 9
	cld
	repe	cmpsb
	jne	.fail5

	; check 6: re-interning "world" returns id 2 again, not a new id
	lea	rdi, [it]
	lea	rsi, [s_world]
	mov	rdx, 5
	call	intern_id
	cmp	rax, 2
	jne	.fail6

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
  .fail6: mov eax,231
	mov edi,16
	syscall

segment readable writeable
  ar: rb sizeof.Arena
  it: rb sizeof.Interner
  s_hello db 'hello'
  s_hello2 db 'hello'
  s_world db 'world'
  s_exsecutor db 'exsecutor'
