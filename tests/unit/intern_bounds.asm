; tests/unit/intern_bounds.asm
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
; asm-rt NEGATIVE fixture for compiler/x86_64/rt/intern.inc.
;
; `intern_bytes` with an id that was never assigned by `intern_id` is an
; internal contract violation (this compiler's own code passing a bad
; intern id), not a source-level condition -- it `rassert`s, transitively,
; through `vec_get`'s bounds check (rt/intern.inc's, rt/vec.inc's headers).
; One string interned (assigning id 1); `intern_bytes(99)` -- must trap.
;
; 128 + SIGILL(4) = 132.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/span.inc'

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 65536
	call	arena_init
	jc	.setup_failed
	lea	rdi, [it]
	lea	rsi, [ar]
	mov	rdx, 8
	call	intern_init
	jc	.setup_failed
	lea	rdi, [it]
	lea	rsi, [s_hello]
	mov	rdx, 5
	call	intern_id		; assigns id 1

	lea	rdi, [it]
	mov	rsi, 99			; never assigned -- must trap
	call	intern_bytes

	; unreachable if intern_bytes' bounds check works
	mov	eax, 231
	mov	edi, 77
	syscall

  .setup_failed:
	mov eax, 231
	mov edi, 66
	syscall

segment readable writeable
  ar: rb sizeof.Arena
  it: rb sizeof.Interner
  s_hello db 'hello'
