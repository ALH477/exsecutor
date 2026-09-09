; tests/unit/probe_map_order_pad7.asm
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
; Half of the contract `tools/rt-map-order-probe.sh` requires (see that
; file's own header comment) -- the other half is
; tests/unit/probe_map_order_pad0.asm, identical except for `PAD_PAGES`.
; Together they prove rt/map.inc's `map_iterate` order does not move when
; the map's backing address does: this file mmaps 0 throwaway pages before
; creating its arena/map, `pad7` mmaps 7, so the two runs' `Arena`/`Map`
; backing regions land at genuinely different addresses (mmap placement
; shifts deterministically with how many mmap calls preceded it) -- yet
; both must write byte-identical output.
;
; Inserts a fixed, deliberately-unsorted key sequence (fig, date, apple,
; banana, cherry -- alphabetically scrambled, and NOT insertion-sorted by
; length either, so neither sort order could produce this sequence by
; accident), `map_iterate`s, and `sys_write`s each visited key's raw bytes
; to stdout with no separator and no length prefix -- exactly what
; `tools/rt-map-order-probe.sh` diffs.
;
; `nbuckets = 8` -- with 5 keys, small enough that this is a meaningful
; exercise of chaining, not one key per bucket.
;
; Also runnable and auditable on its own, independent of the probe script:
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/map.inc'

PAD_PAGES = 7

; map_iterate callback: writes this entry's key, raw bytes, no separator.
proc write_key_cb, key_ptr, key_len, value
	locals
	endl
	mov	rdi, 1
	mov	rsi, [key_ptr]
	mov	rdx, [key_len]
	call	sys_write
	return
endp

segment readable executable
  start:
	if PAD_PAGES > 0
		xor	rdi, rdi
		mov	rsi, PAD_PAGES*4096
		mov	edx, 3			; PROT_READ|PROT_WRITE
		mov	ecx, 0x22		; MAP_PRIVATE|MAP_ANONYMOUS
		mov	r8d, -1
		xor	r9, r9
		call	sys_mmap
		jc	.fail
	end if

	lea	rdi, [ar]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail
	lea	rdi, [m]
	lea	rsi, [ar]
	mov	rdx, 8
	call	map_init
	jc	.fail

	lea	rdi, [m]
	lea	rsi, [k_fig]
	mov	rdx, k_fig.len
	mov	rcx, 1
	call	map_insert
	lea	rdi, [m]
	lea	rsi, [k_date]
	mov	rdx, k_date.len
	mov	rcx, 2
	call	map_insert
	lea	rdi, [m]
	lea	rsi, [k_apple]
	mov	rdx, k_apple.len
	mov	rcx, 3
	call	map_insert
	lea	rdi, [m]
	lea	rsi, [k_banana]
	mov	rdx, k_banana.len
	mov	rcx, 4
	call	map_insert
	lea	rdi, [m]
	lea	rsi, [k_cherry]
	mov	rdx, k_cherry.len
	mov	rcx, 5
	call	map_insert

	lea	rdi, [m]
	lea	rsi, [write_key_cb]
	call	map_iterate

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail:
	mov	eax, 231
	mov	edi, 1
	syscall

segment readable writeable
  ar: rb sizeof.Arena
  m: rb sizeof.Map
  k_fig    db 'fig'
  .len = $ - k_fig
  k_date   db 'date'
  .len = $ - k_date
  k_apple  db 'apple'
  .len = $ - k_apple
  k_banana db 'banana'
  .len = $ - k_banana
  k_cherry db 'cherry'
  .len = $ - k_cherry
