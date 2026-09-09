; tests/unit/diag_util_ops.asm
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
; The four primitives in compiler/x86_64/diag/util.inc, each on the inputs
; that are actually hard rather than the ones that are convenient.
;
;   1. `diag_lookup` round-trips EVERY entry in the generated table: for each
;      i, diag_lookup(diag_code_numeric[i]) must return i. This is a coupling
;      test with codes.inc, so it grows automatically when §13 does -- it
;      iterates DIAG_CODE_COUNT rather than naming codes, and a regenerated
;      table with a new row is covered the moment it lands.
;   2. `diag_fmt_u32` on 0 (the one value whose digit loop must run despite
;      the quotient being zero immediately), 7, 42, 1000000, and 4294967295
;      (the 10-digit maximum, which is where a too-small internal buffer or a
;      signed division would show).
;   3. `diag_line_col` across line starts, line ends, the newline byte itself,
;      an EMPTY line, one-past-the-end, and a wildly out-of-range offset that
;      must clamp rather than run off the buffer.
;   4. `diag_line_bounds` on the same shapes, including the empty line (where
;      start == end) and the unterminated final line (where end == src_len).
;
; The source under test is "aa\nbbb\n\nccccc\ndd" -- 16 bytes, five lines,
; the third empty and the fifth unterminated.
;
; Exit 0 = all checks passed; 10+N = check N failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/diag/util.inc'

segment readable executable
  start:
	; ---- check 1: diag_lookup round-trips every generated entry ----
	xor	rbx, rbx
  .lk:
	cmp	rbx, DIAG_CODE_COUNT
	jge	.lk_done
	mov	edi, [diag_code_numeric + rbx*4]
	call	diag_lookup
	cmp	eax, ebx
	jne	.fail1
	inc	rbx
	jmp	.lk
  .lk_done:

	; ---- check 2: diag_fmt_u32 ----
	xor	rbx, rbx
  .fm:
	cmp	rbx, FMT_COUNT
	jge	.fm_done
	mov	r12, rbx
	imul	r12, 24
	mov	edi, [fmt_tab + r12]
	lea	rsi, [scratch]
	mov	rdx, 16
	call	diag_fmt_u32
	mov	r13, rax
	lea	rdi, [scratch]
	mov	rsi, r13
	lea	rdx, [fmt_blob]
	add	rdx, [fmt_tab + r12 + 8]
	mov	rcx, [fmt_tab + r12 + 16]
	call	diagt_eq
	test	eax, eax
	jz	.fail2
	inc	rbx
	jmp	.fm
  .fm_done:

	; ---- check 3: diag_line_col ----
	xor	rbx, rbx
  .lc:
	cmp	rbx, LC_COUNT
	jge	.lc_done
	mov	r12, rbx
	imul	r12, 24
	lea	rdi, [srctext]
	mov	rsi, srctext_len
	mov	rdx, [lc_tab + r12]
	call	diag_line_col
	cmp	rax, [lc_tab + r12 + 8]
	jne	.fail3
	cmp	rdx, [lc_tab + r12 + 16]
	jne	.fail3
	inc	rbx
	jmp	.lc
  .lc_done:

	; ---- check 4: diag_line_bounds ----
	xor	rbx, rbx
  .lb:
	cmp	rbx, LB_COUNT
	jge	.lb_done
	mov	r12, rbx
	imul	r12, 24
	lea	rdi, [srctext]
	mov	rsi, srctext_len
	mov	rdx, [lb_tab + r12]
	call	diag_line_bounds
	cmp	rax, [lb_tab + r12 + 8]
	jne	.fail4
	cmp	rdx, [lb_tab + r12 + 16]
	jne	.fail4
	inc	rbx
	jmp	.lb
  .lb_done:

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1:
	mov	eax, 231
	mov	edi, 11
	syscall
  .fail2:
	mov	eax, 231
	mov	edi, 12
	syscall
  .fail3:
	mov	eax, 231
	mov	edi, 13
	syscall
  .fail4:
	mov	eax, 231
	mov	edi, 14
	syscall

; ---- byte helpers (fixture-local; see this file's header) ------------------
; diagt_eq(rdi=a, rsi=alen, rdx=b, rcx=blen) -> eax = 1 if identical.
  diagt_eq:
	cmp	rsi, rcx
	jne	.ne
	xor	r8, r8
  .l:
	cmp	r8, rsi
	jge	.eq
	mov	al, [rdi + r8]
	cmp	al, [rdx + r8]
	jne	.ne
	inc	r8
	jmp	.l
  .eq:
	mov	eax, 1
	ret
  .ne:
	xor	eax, eax
	ret

; diagt_find(rdi=hay, rsi=hlen, rdx=needle, rcx=nlen) -> eax = 1 if `needle`
; occurs anywhere in `hay`. This is what makes the "never the raw codepoint"
; check independent of the expected-output comparison: even if BOTH the
; renderer and this fixture's expected bytes were wrong in the same way, a
; raw bidi override in the output would still be caught here.
  diagt_find:
	test	rcx, rcx
	jz	.no
	cmp	rsi, rcx
	jb	.no
	mov	r8, rsi
	sub	r8, rcx			; last valid start offset
	xor	r9, r9
  .outer:
	cmp	r9, r8
	jg	.no
	xor	r10, r10
  .inner:
	cmp	r10, rcx
	jge	.yes
	mov	r11, r9
	add	r11, r10
	mov	al, [rdi + r11]
	cmp	al, [rdx + r10]
	jne	.next
	inc	r10
	jmp	.inner
  .next:
	inc	r9
	jmp	.outer
  .yes:
	mov	eax, 1
	ret
  .no:
	xor	eax, eax
	ret

segment readable writeable
  srctext:
	db	0x61, 0x61, 0x0A, 0x62, 0x62, 0x62, 0x0A, 0x0A, 0x63, 0x63, 0x63, 0x63
	db	0x63, 0x0A, 0x64, 0x64
  srctext_len = $ - srctext

  ; (value, offset into fmt_blob, expected length)
  fmt_tab:
	dq	0, 0, 1
	dq	7, 1, 1
	dq	42, 2, 2
	dq	1000000, 4, 7
	dq	4294967295, 11, 10
  FMT_COUNT = ($ - fmt_tab) / 24
  fmt_blob:
	db	0x30, 0x37, 0x34, 0x32, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x34
	db	0x32, 0x39, 0x34, 0x39, 0x36, 0x37, 0x32, 0x39, 0x35

  ; (offset, expected line, expected column)
  lc_tab:
	dq	0, 1, 1
	dq	1, 1, 2
	dq	2, 1, 3
	dq	3, 2, 1
	dq	6, 2, 4
	dq	7, 3, 1
	dq	8, 4, 1
	dq	13, 4, 6
	dq	14, 5, 1
	dq	16, 5, 3
	dq	999, 5, 3
  LC_COUNT = ($ - lc_tab) / 24

  ; (offset, expected line start, expected line end)
  lb_tab:
	dq	0, 0, 2
	dq	2, 0, 2
	dq	3, 3, 6
	dq	7, 7, 7
	dq	8, 8, 13
	dq	13, 8, 13
	dq	14, 14, 16
	dq	16, 14, 16
	dq	999, 14, 16
  LB_COUNT = ($ - lb_tab) / 24

  scratch rb 32
