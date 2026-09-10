; tests/unit/diag_caret_columns.asm
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
; THE CARET LANDS ON THE CHARACTER, NOT ON THE BYTE. §8.3, and
; `docs/design/diagnostics-review.md` D4, which measured the failure: the
; source line is printed in characters and the caret was padded in BYTES, so
; a caret sat one column right for every multibyte character before it. The
; review's own case put the caret under the `;` at the end of the line when
; the offending token was the `<` three characters earlier.
;
; This fixture asserts the two numbers D4 is about, directly, rather than
; pinning a blob: WHERE the caret run starts and HOW LONG it is. Both are
; read back out of the rendered bytes by scanning for `^`, so a change to
; any other part of the format cannot make this fixture pass or fail for the
; wrong reason.
;
; The gutter is 5 bytes wide for a one-digit line number -- two spaces, then
; ` | ` -- so every expected column below is 5 plus the number of CHARACTERS
; that precede the span on its line.
;
; Three cases, chosen so the fixture fails if either half of the rule is
; dropped:
;   A -- pass-through multibyte BEFORE the span. `pira\u{00f1}a_se\u{00f1}or_\u{00f1}u`
;        is 15 characters in 18 bytes, so a byte-padded caret lands 3 columns
;        too far right. This is the case that was broken.
;   B -- pass-through multibyte INSIDE the span. `mens\u{016b}ra` is 7
;        characters in 8 bytes, so a byte-sized caret RUN is one `^` too long.
;        This is the second half of D4, and it fails independently of A.
;   C -- an ESCAPED codepoint inside the span. `\u{202e}rete\u{200b}` is 10
;        source bytes and 20 rendered columns, and 20 carets is correct. This
;        case was already right before D4 was fixed, and it is here as the
;        regression guard: a "fix" that counted source characters everywhere
;        would break it.
;
; Exit 0 = every case matched; 20+K = case K (0-based) had the wrong caret
; column; 30+K = case K had the wrong caret run length; 40 = no caret was
; found at all in some case's output.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/diag/diag.inc'

segment readable executable
  start:
	xor	rbx, rbx
  .case:
	cmp	rbx, CARET_COUNT
	jge	.all_done
	mov	r12, rbx
	imul	r12, CARET_STRIDE

	lea	rdi, [rec]
	mov	rsi, [ctab + r12 + 0]		; code_num
	mov	rdx, 1				; file_id
	mov	rcx, [ctab + r12 + 8]		; span start
	mov	r8,  [ctab + r12 + 16]		; span len
	call	diag_init

	lea	rdi, [rec]
	lea	rsi, [srcblob]
	add	rsi, [ctab + r12 + 24]
	mov	rdx, [ctab + r12 + 32]
	lea	rcx, [pathblob]
	mov	r8,  PATH_LEN
	call	diag_attach_source

	lea	rdi, [rec]
	lea	rsi, [outbuf]
	mov	rdx, 4096
	call	diag_render_text
	mov	r13, rax			; rendered length

	; ---- find the first `^` and measure its column and its run ----
	lea	rdi, [outbuf]
	mov	rsi, r13
	call	caret_scan			; rax = column, rdx = run
	test	rax, rax
	js	.nocaret
	cmp	rax, [ctab + r12 + 40]
	jne	.badcol
	cmp	rdx, [ctab + r12 + 48]
	jne	.badrun

	inc	rbx
	jmp	.case

  .all_done:
	mov	eax, 231
	xor	edi, edi
	syscall

  .badcol:
	lea	rdi, [rbx + 20]
	mov	eax, 231
	syscall
  .badrun:
	lea	rdi, [rbx + 30]
	mov	eax, 231
	syscall
  .nocaret:
	mov	edi, 40
	mov	eax, 231
	syscall

; caret_scan(rdi=buf, rsi=len) -> rax = 0-based column of the first `^`
; within its own line, rdx = how many `^` follow it consecutively. rax = -1
; if the buffer holds no `^` at all.
;
; Deliberately independent of the rest of the format: it locates the caret by
; searching for the byte, then walks back to the preceding newline. Nothing
; here knows what a gutter is.
  caret_scan:
	xor	r8, r8				; i
  .find:
	cmp	r8, rsi
	jge	.none
	cmp	byte [rdi + r8], '^'
	je	.found
	inc	r8
	jmp	.find
  .found:
	mov	r9, r8				; walk back to the line start
  .back:
	test	r9, r9
	jz	.at_start
	mov	r10, r9
	dec	r10
	cmp	byte [rdi + r10], 10
	je	.at_start
	mov	r9, r10
	jmp	.back
  .at_start:
	mov	rax, r8
	sub	rax, r9				; column within the line
	xor	rdx, rdx
  .run:
	mov	r10, r8
	add	r10, rdx
	cmp	r10, rsi
	jge	.done
	cmp	byte [rdi + r10], '^'
	jne	.done
	inc	rdx
	jmp	.run
  .done:
	ret
  .none:
	mov	rax, -1
	xor	rdx, rdx
	ret

segment readable writeable
  ; code_num, span.start, span.len, src offset, src len,
  ; expected caret column, expected caret run
  ctab:
	; A: the `<` at byte 75. Line 2 is
	;   `    firma piraña_señor_ñu: u64 = a < 3;`
	; and 35 CHARACTERS precede the `<` (38 bytes). 5 + 35 = 40.
	dq	201, 75, 1,  0, SRC_A_LEN,  40, 1
	; B: `mensūra` at byte 41, 8 bytes and 7 characters. 10 characters
	; precede it on line 2 (`    firma `), so 5 + 10 = 15, and the run is
	; 7 -- NOT the 8 a byte count would give.
	dq	210, 41, 8,  SRC_B_OFF, SRC_B_LEN, 15, 7
	; C: `\u{202e}rete\u{200b}` at byte 30, 10 source bytes rendering as
	; 20 columns. 8 characters precede it (`    sub `), so 5 + 8 = 13, and
	; the run is the RENDERED width, 20.
	dq	201, 30, 10, SRC_C_OFF, SRC_C_LEN, 13, 20
  CARET_STRIDE = 7 * 8
  CARET_COUNT = ($ - ctab) / CARET_STRIDE

  pathblob db 'p.exs'
  PATH_LEN = $ - pathblob

  srcblob:
  src_a:
	db	0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x61, 0x20, 0x66, 0x75, 0x6E, 0x63
	db	0x74, 0x69, 0x6F, 0x20, 0x75, 0x6E, 0x61, 0x28, 0x61, 0x3A, 0x20, 0x75
	db	0x36, 0x34, 0x29, 0x20, 0x2D, 0x3E, 0x20, 0x75, 0x36, 0x34, 0x20, 0x7B
	db	0x0A, 0x20, 0x20, 0x20, 0x20, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x70
	db	0x69, 0x72, 0x61, 0xC3, 0xB1, 0x61, 0x5F, 0x73, 0x65, 0xC3, 0xB1, 0x6F
	db	0x72, 0x5F, 0xC3, 0xB1, 0x75, 0x3A, 0x20, 0x75, 0x36, 0x34, 0x20, 0x3D
	db	0x20, 0x61, 0x20, 0x3C, 0x20, 0x33, 0x3B, 0x0A, 0x20, 0x20, 0x20, 0x20
	db	0x72, 0x65, 0x64, 0x64, 0x65, 0x20, 0x61, 0x3B, 0x0A, 0x7D, 0x0A
  SRC_A_LEN = $ - src_a
  src_b:
	db	0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x61, 0x20, 0x66, 0x75, 0x6E, 0x63
	db	0x74, 0x69, 0x6F, 0x20, 0x75, 0x6E, 0x61, 0x28, 0x29, 0x20, 0x2D, 0x3E
	db	0x20, 0x75, 0x36, 0x34, 0x20, 0x7B, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x66
	db	0x69, 0x72, 0x6D, 0x61, 0x20, 0x6D, 0x65, 0x6E, 0x73, 0xC5, 0xAB, 0x72
	db	0x61, 0x3A, 0x20, 0x75, 0x36, 0x34, 0x20, 0x3D, 0x20, 0x33, 0x3B, 0x0A
	db	0x20, 0x20, 0x20, 0x20, 0x72, 0x65, 0x64, 0x64, 0x65, 0x20, 0x31, 0x3B
	db	0x0A, 0x7D, 0x0A
  SRC_B_OFF = src_b - srcblob
  SRC_B_LEN = $ - src_b
  src_c:
	db	0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x61, 0x20, 0x66, 0x75, 0x6E, 0x63
	db	0x74, 0x69, 0x6F, 0x20, 0x78, 0x28, 0x29, 0x20, 0x7B, 0x0A, 0x20, 0x20
	db	0x20, 0x20, 0x73, 0x75, 0x62, 0x20, 0xE2, 0x80, 0xAE, 0x72, 0x65, 0x74
	db	0x65, 0xE2, 0x80, 0x8B, 0x20, 0x3D, 0x20, 0x72, 0x0A, 0x7D, 0x0A
  SRC_C_OFF = src_c - srcblob
  SRC_C_LEN = $ - src_c

  rec	rb sizeof.Diag
  outbuf rb 4096
