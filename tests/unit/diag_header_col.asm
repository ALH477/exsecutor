; tests/unit/diag_header_col.asm
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
; THE HEADER's `col` IS THE SAME NUMBER THE CARET IS UNDER. `docs/design/
; diagnostics-review.md` D4 fixed the caret run (tests/unit/diag_caret_
; columns.asm), but `diag_render_text`'s `file:line:col` header kept printing
; `diag_line_col`'s BYTE column, so a diagnostic on `piraña_señor_ñu`'s `<`
; rendered `2:39` over a caret sitting at character 36 -- the header
; disagreed with its own diagnostic. Fixed in render.inc by computing the
; header's `col` with the same classifier the caret uses
; (`__diag_col_of`, over `__diag_escape_core`) instead of `diag_line_col`'s
; `rdx`. See util.inc's header and render.inc's `diag_render_text`.
;
; This fixture reads the header back out of the rendered bytes -- it parses
; the decimal `line` and `col` following the known, fixed path prefix --
; rather than pinning a blob, for the same reason diag_caret_columns.asm
; gives: a change to any other part of the format cannot make this pass or
; fail for the wrong reason.
;
; Two cases, chosen so the fixture fails if the fix is wrong in either
; direction:
;   A -- `piraña_señor_ñu`'s `<`, byte 75 of `tests/diagnostics/
;        c23_caret_byte_vs_char_misalignment.exsc`'s reconstructed source,
;        three 2-byte `ñ` before it. 35 CHARACTERS (38 bytes) precede the
;        `<`, so the 1-based CHARACTER column is 36 -- the review's own
;        number, and the case that was wrong (it rendered 39). Same source
;        bytes tests/unit/diag_caret_columns.asm's case A uses, so the two
;        fixtures agree on the ground truth by construction, not by
;        re-typing it.
;   B -- pure ASCII, no multibyte anywhere on the line. Byte column and
;        character column already coincide, so this is the regression guard:
;        a "fix" that off-by-one'd every column, ASCII included, would break
;        this case while A alone would not catch it if the two errors
;        cancelled.
;
; Exit 0 = every case matched; 50+K = case K's header `col` was wrong;
; 60+K = case K's header `line` was wrong; 70 = the rendered header did not
; have the expected `path:line:col:` shape at all.
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
	cmp	rbx, HCOL_COUNT
	jge	.all_done
	mov	r12, rbx
	imul	r12, HCOL_STRIDE

	lea	rdi, [rec]
	mov	rsi, [htab + r12 + 0]		; code_num
	mov	rdx, 1				; file_id
	mov	rcx, [htab + r12 + 8]		; span start
	mov	r8,  [htab + r12 + 16]		; span len
	call	diag_init

	lea	rdi, [rec]
	lea	rsi, [srcblob]
	add	rsi, [htab + r12 + 24]
	mov	rdx, [htab + r12 + 32]
	lea	rcx, [pathblob]
	mov	r8,  PATH_LEN
	call	diag_attach_source

	lea	rdi, [rec]
	lea	rsi, [outbuf]
	mov	rdx, 4096
	call	diag_render_text
	mov	r13, rax			; rendered length -- unused past
						; this point, kept for a debugger

	; ---- parse `path:line:col:` off the front of the rendered header.
	; `pathblob` is the same PATH_LEN bytes for every case, so the header
	; is known to start `p.exs:`.
	lea	rdi, [outbuf]
	add	rdi, PATH_LEN
	cmp	byte [rdi], ':'
	jne	.badformat
	inc	rdi
	call	parse_u32			; rax = line, rdi advanced
	mov	r14, rax
	cmp	byte [rdi], ':'
	jne	.badformat
	inc	rdi
	call	parse_u32			; rax = col
	mov	r15, rax

	cmp	r14, [htab + r12 + 40]		; expected line
	jne	.badline
	cmp	r15, [htab + r12 + 48]		; expected col
	jne	.badcol

	inc	rbx
	jmp	.case

  .all_done:
	mov	eax, 231
	xor	edi, edi
	syscall

  .badline:
	lea	rdi, [rbx + 60]
	mov	eax, 231
	syscall
  .badcol:
	lea	rdi, [rbx + 50]
	mov	eax, 231
	syscall
  .badformat:
	mov	edi, 70
	mov	eax, 231
	syscall

; parse_u32(rdi=ptr) -> rax = the decimal value of the run of ASCII digits
; starting at `ptr`; rdi = advanced one past the last digit consumed.
; Requires at least one digit at `ptr` -- true of every call site here, since
; both are read immediately after a `:` this fixture already confirmed is
; followed by `diag_out_u32`'s output, never an empty field.
  parse_u32:
	xor	rax, rax
  .loop:
	movzx	ecx, byte [rdi]
	cmp	cl, '0'
	jb	.done
	cmp	cl, '9'
	ja	.done
	imul	rax, rax, 10
	sub	cl, '0'
	movzx	rcx, cl
	add	rax, rcx
	inc	rdi
	jmp	.loop
  .done:
	ret

segment readable writeable
  ; code_num, span.start, span.len, src offset, src len,
  ; expected header line, expected header CHARACTER col
  htab:
	; A: piraña_señor_ñu's `<`, byte 75 of src_a, line 2. 35 characters
	; (38 bytes) precede it, so the 1-based character column is 36 --
	; this is the number D4 says the header got wrong (it printed 39).
	dq	201, 75, 1, 0, SRC_A_LEN, 2, 36
	; B: pure ASCII. `+` in `    redde a + b;` (line 2 of src_b), byte 26
	; of src_b. 12 characters (12 bytes -- no multibyte on this line at
	; all) precede it, so column 13, unchanged from the byte count.
	dq	201, 26, 1, SRC_B_OFF, SRC_B_LEN, 2, 13
  HCOL_STRIDE = 7 * 8
  HCOL_COUNT = ($ - htab) / HCOL_STRIDE

  pathblob db 'p.exs'
  PATH_LEN = $ - pathblob

  srcblob:
  src_a:
	; Identical bytes to tests/unit/diag_caret_columns.asm's case A --
	; `publica functio maxima(a: u64) -> u64 {` / `    firma
	; piraña_señor_ñu: u64 = a < 3;` / `    redde a;` / `}` -- see that
	; fixture for the byte-by-byte accounting; not re-derived here.
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
	; Pure ASCII. Line 1 = `functio x() {` (14 bytes incl. `\n`). Line 2
	; starts at byte 14: `    redde a + b;` -- `+` is the 12th byte of
	; that line (0-based), so at src_b offset 14+12 = 26.
	db	'functio x() {', 10
	db	'    redde a + b;', 10
	db	'}', 10
  SRC_B_OFF = src_b - srcblob
  SRC_B_LEN = $ - src_b

  rec	rb sizeof.Diag
  outbuf rb 4096
