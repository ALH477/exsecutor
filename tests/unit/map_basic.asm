; tests/unit/map_basic.asm
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
; asm-rt fixture for compiler/x86_64/rt/map.inc.
;
; `nbuckets = 4` throughout -- deliberately small, to force real hash
; collisions among the handful of short keys this fixture uses, so a
; check that passed only because every key landed in its own bucket would
; not actually prove chaining works.
;
;   1. `map_get` on an empty map misses (returns 0)
;   2. three keys inserted, `map_get` returns the right value for each
;   3. `map_get` misses for a key that was never inserted
;   4. `map_get` on a shorter key sharing a stored key's prefix ("fo" vs
;      the stored "foo") does NOT false-match -- length is part of key
;      identity, not just an implementation detail of the byte compare
;   5. `map_iterate` visits every entry in EXACT insertion order --
;      inserted as e, a, d, b, c (deliberately unsorted, and, with
;      `nbuckets = 4`, not bucket order either) and must be observed in
;      exactly that order, proving `map_iterate` walks the dense array and
;      never the hash buckets (CLAUDE.md, "Maps iterate in insertion
;      order")
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/map.inc'

; callback for check 5: appends the first byte of each visited key to
; `observed`, in call order.
proc record_cb, key_ptr, key_len, value
	locals
	endl
	mov	rax, [obs_idx]
	lea	rdi, [observed]
	add	rdi, rax
	mov	rsi, [key_ptr]
	mov	cl, [rsi]
	mov	[rdi], cl
	inc	rax
	mov	[obs_idx], rax
	return
endp

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 65536
	call	arena_init
	jc	.fail0
	lea	rdi, [m]
	lea	rsi, [ar]
	mov	rdx, 4
	call	map_init
	jc	.fail0

	; check 1: miss on empty map
	lea	rdi, [m]
	lea	rsi, [k_foo]
	mov	rdx, 3
	call	map_get
	test	rax, rax
	jnz	.fail1

	; check 2: insert three keys, get each back
	lea	rdi, [m]
	lea	rsi, [k_foo]
	mov	rdx, 3
	mov	rcx, 111
	call	map_insert
	lea	rdi, [m]
	lea	rsi, [k_bar]
	mov	rdx, 3
	mov	rcx, 222
	call	map_insert
	lea	rdi, [m]
	lea	rsi, [k_baz]
	mov	rdx, 3
	mov	rcx, 333
	call	map_insert

	lea	rdi, [m]
	lea	rsi, [k_foo]
	mov	rdx, 3
	call	map_get
	cmp	rax, 111
	jne	.fail2
	lea	rdi, [m]
	lea	rsi, [k_bar]
	mov	rdx, 3
	call	map_get
	cmp	rax, 222
	jne	.fail2
	lea	rdi, [m]
	lea	rsi, [k_baz]
	mov	rdx, 3
	call	map_get
	cmp	rax, 333
	jne	.fail2

	; check 3: miss for a key never inserted
	lea	rdi, [m]
	lea	rsi, [k_qux]
	mov	rdx, 3
	call	map_get
	test	rax, rax
	jnz	.fail3

	; check 4: shorter key sharing a prefix must not false-match
	lea	rdi, [m]
	lea	rsi, [k_foo]
	mov	rdx, 2			; "fo", not "foo"
	call	map_get
	test	rax, rax
	jnz	.fail4

	; check 5: map_iterate is exact insertion order, not bucket order
	lea	rdi, [m2]
	lea	rsi, [ar]
	mov	rdx, 4
	call	map_init
	jc	.fail0
	lea	rdi, [m2]
	lea	rsi, [k_e]
	mov	rdx, 1
	mov	rcx, 1
	call	map_insert
	lea	rdi, [m2]
	lea	rsi, [k_a]
	mov	rdx, 1
	mov	rcx, 2
	call	map_insert
	lea	rdi, [m2]
	lea	rsi, [k_d]
	mov	rdx, 1
	mov	rcx, 3
	call	map_insert
	lea	rdi, [m2]
	lea	rsi, [k_b]
	mov	rdx, 1
	mov	rcx, 4
	call	map_insert
	lea	rdi, [m2]
	lea	rsi, [k_c]
	mov	rdx, 1
	mov	rcx, 5
	call	map_insert

	mov	qword [obs_idx], 0
	lea	rdi, [m2]
	lea	rsi, [record_cb]
	call	map_iterate

	mov	rax, [obs_idx]
	cmp	rax, 5
	jne	.fail5
	mov	al, [observed+0]
	cmp	al, 'e'
	jne	.fail5
	mov	al, [observed+1]
	cmp	al, 'a'
	jne	.fail5
	mov	al, [observed+2]
	cmp	al, 'd'
	jne	.fail5
	mov	al, [observed+3]
	cmp	al, 'b'
	jne	.fail5
	mov	al, [observed+4]
	cmp	al, 'c'
	jne	.fail5

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
  m: rb sizeof.Map
  m2: rb sizeof.Map
  k_foo db 'foo'
  k_bar db 'bar'
  k_baz db 'baz'
  k_qux db 'qux'
  k_a db 'a'
  k_b db 'b'
  k_c db 'c'
  k_d db 'd'
  k_e db 'e'
  obs_idx: dq 0
  observed: rb 8
