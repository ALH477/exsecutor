; tests/unit/span_ops.asm
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
; asm-rt fixture for compiler/x86_64/rt/span.inc (positive paths). The
; cross-file `span_union` trap is a separate NEGATIVE fixture --
; tests/unit/span_union_file_mismatch.asm -- because it terminates the
; process.
;
;   1. `span_make` writes all three fields (`file_id`, `start`, `len`)
;      correctly
;   2. `span_end` = `start + len`
;   3. `span_contains`: inside the span, at `start` (inclusive), at
;      `start+len` (exclusive -- one past the end is NOT contained), and
;      before `start` -- all four cases checked, not just the easy middle
;      one
;   4. `span_union` covers both inputs exactly ([100,120) union [200,210)
;      == [100,210)), preserves `file_id`, and is symmetric (same result
;      with the arguments swapped) -- this specific check is what would
;      have caught a real bug found by re-reading this file before it was
;      ever run: an early draft clobbered `file_id` with `b.start` because
;      both briefly shared one register (see rt/span.inc's header on
;      `span_union`)
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/span.inc'

segment readable executable
  start:
	; check 1
	lea	rdi, [sp1]
	mov	rsi, 7
	mov	rdx, 100
	mov	rcx, 20
	call	span_make
	mov	eax, [sp1 + Span.file_id]
	cmp	eax, 7
	jne	.fail1
	mov	eax, [sp1 + Span.start]
	cmp	eax, 100
	jne	.fail1
	mov	eax, [sp1 + Span.len]
	cmp	eax, 20
	jne	.fail1

	; check 2
	lea	rdi, [sp1]
	call	span_end
	cmp	eax, 120
	jne	.fail2

	; check 3
	lea	rdi, [sp1]
	mov	rsi, 110
	call	span_contains
	cmp	eax, 1
	jne	.fail3
	lea	rdi, [sp1]
	mov	rsi, 100
	call	span_contains
	cmp	eax, 1
	jne	.fail3
	lea	rdi, [sp1]
	mov	rsi, 120
	call	span_contains
	cmp	eax, 0
	jne	.fail3
	lea	rdi, [sp1]
	mov	rsi, 99
	call	span_contains
	cmp	eax, 0
	jne	.fail3

	; check 4
	lea	rdi, [sp2]
	mov	rsi, 7
	mov	rdx, 200
	mov	rcx, 10
	call	span_make
	lea	rdi, [sp1]
	lea	rsi, [sp2]
	lea	rdx, [sp3]
	call	span_union
	mov	eax, [sp3 + Span.file_id]
	cmp	eax, 7
	jne	.fail4
	mov	eax, [sp3 + Span.start]
	cmp	eax, 100
	jne	.fail4
	mov	eax, [sp3 + Span.len]
	cmp	eax, 110
	jne	.fail4

	; check 4, argument order reversed -- must be identical
	lea	rdi, [sp2]
	lea	rsi, [sp1]
	lea	rdx, [sp4]
	call	span_union
	mov	eax, [sp4 + Span.file_id]
	cmp	eax, 7
	jne	.fail4
	mov	eax, [sp4 + Span.start]
	cmp	eax, 100
	jne	.fail4
	mov	eax, [sp4 + Span.len]
	cmp	eax, 110
	jne	.fail4

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

segment readable writeable
  sp1: rb sizeof.Span
  sp2: rb sizeof.Span
  sp3: rb sizeof.Span
  sp4: rb sizeof.Span
