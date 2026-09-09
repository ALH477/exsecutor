; tests/unit/arena_exhausted.asm
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
; asm-rt NEGATIVE fixture for compiler/x86_64/rt/arena.inc.
;
; `arena.inc`'s header records a deliberate v1 design choice: a fixed-
; capacity arena does not auto-grow, and `arena_alloc` past capacity is
; treated as an internal contract violation (the caller under-sized the
; arena) rather than a recoverable error -- it `rassert`s. This fixture
; proves that trap actually fires: a 16-byte arena, then a request for
; 1000 bytes. Must never reach the unreachable-if-correct syscall after the
; `arena_alloc` call.
;
; 128 + SIGILL(4) = 132.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/arena.inc'

segment readable executable
  start:
	lea	rdi, [a]
	mov	rsi, 16
	call	arena_init
	jc	.setup_failed
	lea	rdi, [a]
	mov	rsi, 1000		; far past the 16-byte capacity -- must trap
	call	arena_alloc

	; unreachable if arena_alloc's rassert works
	mov	eax, 231
	mov	edi, 77
	syscall

  .setup_failed:
	; arena_init itself failing is not what this fixture tests; make that
	; failure loud and distinct rather than silently masking the real check
	mov	eax, 231
	mov	edi, 66
	syscall

segment readable writeable
  a: rb sizeof.Arena
