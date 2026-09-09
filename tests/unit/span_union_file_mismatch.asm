; tests/unit/span_union_file_mismatch.asm
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
; asm-rt NEGATIVE fixture for compiler/x86_64/rt/span.inc.
;
; `span_union` across two spans with different `file_id`s is not a
; meaningful span -- an internal contract violation (this compiler's own
; code trying to union positions in two different source files), not a
; source-level condition -- it `rassert`s (rt/span.inc's header). Two
; spans, `file_id` 1 and 2; `span_union` between them must trap.
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
	lea	rdi, [sp1]
	mov	rsi, 1
	mov	rdx, 0
	mov	rcx, 10
	call	span_make
	lea	rdi, [sp2]
	mov	rsi, 2			; different file_id -- must trap
	mov	rdx, 5
	mov	rcx, 10
	call	span_make

	lea	rdi, [sp1]
	lea	rsi, [sp2]
	lea	rdx, [sp3]
	call	span_union

	; unreachable if span_union's file_id rassert works
	mov	eax, 231
	mov	edi, 77
	syscall

segment readable writeable
  sp1: rb sizeof.Span
  sp2: rb sizeof.Span
  sp3: rb sizeof.Span
