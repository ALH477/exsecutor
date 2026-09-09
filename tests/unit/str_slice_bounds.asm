; tests/unit/str_slice_bounds.asm
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
; asm-rt NEGATIVE fixture for compiler/x86_64/rt/str.inc.
;
; A `str_slice` request that does not fit within [0, len) is an internal
; contract violation (this compiler's own code slicing past a span it owns
; the bounds of), not a source-level condition -- it `rassert`s
; (rt/str.inc's, rt/sys.inc's headers carry the reasoning and the full
; failure-channel table). "hello" has length 5; a 3-byte slice at offset 4
; would need bytes [4,7), past the end -- must trap.
;
; 128 + SIGILL(4) = 132.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/str.inc'

segment readable executable
  start:
	lea	rdi, [s]
	mov	rsi, 5			; len("hello") = 5
	mov	rdx, 4			; offset 4
	mov	rcx, 3			; want 3 bytes: [4,7) -- past the end
	call	str_slice

	; unreachable if str_slice's bounds rassert works
	mov	eax, 231
	mov	edi, 77
	syscall

segment readable writeable
  s db 'hello'
