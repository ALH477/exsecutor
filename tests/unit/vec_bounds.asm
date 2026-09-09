; tests/unit/vec_bounds.asm
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
; asm-rt NEGATIVE fixture for compiler/x86_64/rt/vec.inc.
;
; `vec_get` past `len` is an internal contract violation (this compiler's
; own code indexing its own Vec out of bounds), not a source-level
; condition -- it `rassert`s (rt/vec.inc's header; rt/sys.inc's header
; carries the full failure-channel table). Two elements pushed (valid
; indices 0-1), then `vec_get` index 2 -- must trap.
;
; 128 + SIGILL(4) = 132.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/vec.inc'

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 4096
	call	arena_init
	jc	.setup_failed
	lea	rdi, [v]
	lea	rsi, [ar]
	mov	rdx, 8
	mov	rcx, 4
	call	vec_init
	jc	.setup_failed

	lea	rdi, [v]
	call	vec_push
	jc	.setup_failed
	mov	qword [rax], 111
	lea	rdi, [v]
	call	vec_push
	jc	.setup_failed
	mov	qword [rax], 222

	lea	rdi, [v]
	mov	rsi, 2			; len is 2 -- valid indices are 0,1 only
	call	vec_get

	; unreachable if vec_get's rassert works
	mov	eax, 231
	mov	edi, 77
	syscall

  .setup_failed:
	mov eax, 231
	mov edi, 66
	syscall

segment readable writeable
  ar: rb sizeof.Arena
  v: rb sizeof.Vec
