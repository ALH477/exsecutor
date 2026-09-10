; tests/unit/diag_related_span.asm
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
; A SECOND SPAN, AND THE TWO THINGS IT CHANGES. `docs/design/
; diagnostics-review.md` D6: `EXS-E0202` reports at or past end of input with
; an empty source line and never mentions the `{` that was left open, which
; is "what rustc and clang both do best and the case where a second span
; matters most."
;
; `diag_rel_set` is the record's half of that. The input below is the
; review's own case c02 -- an `si` block whose `}` is missing three lines up
; -- and the assertion is byte-exact in text mode and substring-exact on the
; JSON member, because the JSON `related` object is a machine contract and
; the text block is a layout that has to line its two gutters up.
;
; WHAT THIS FIXTURE DOES NOT PROVE. Nothing in the parser calls
; `diag_rel_set` yet -- `cst/` is another agent's tree -- so this exercises
; the RECORD and the RENDERER, not the end-to-end diagnostic. Running `exsc`
; on c02 today still produces the note ("it wants }") without the second
; span. That is stated here rather than left for a reader to discover,
; because a fixture that passes is not evidence for a claim it does not make.
;
; The expected text is written out as readable literals rather than a hex
; blob: this format is meant to be read by a person, and an expectation a
; reviewer cannot read is an expectation nobody checks.
;
; Exit 0 = both modes matched; 11 = the text rendering differed; 12 = the
; text rendering was truncated; 13 = the JSON lacked the `related` member;
; 14 = the JSON still said `"related":null`.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/diag/diag.inc'

segment readable executable
  start:
	; ---- EXS-E0202 at end of input, with the opening `{` attached -------
	lea	rdi, [rec]
	mov	rsi, 202
	mov	rdx, 1				; file_id
	mov	rcx, SRC_LEN			; span: one past the last byte
	mov	r8,  0
	call	diag_init

	lea	rdi, [rec]
	lea	rsi, [srcblob]
	mov	rdx, SRC_LEN
	lea	rcx, [pathblob]
	mov	r8,  PATH_LEN
	call	diag_attach_source

	; the `{` that opened the `si` block, at byte 77 (line 3, column 15)
	lea	rdi, [rec]
	mov	rsi, 1
	mov	rdx, 77
	mov	rcx, 1
	call	diag_rel_set

	; the edit the parser would attach: insert `}` at the end
	lea	rdi, [rec]
	call	diag_fix_ptr
	mov	rdi, rax
	mov	rsi, 1
	mov	rdx, SRC_LEN - 1
	lea	rcx, [fixtext]
	mov	r8,  1
	call	diag_fix_insert

	; ---- text ----------------------------------------------------------
	lea	rdi, [rec]
	lea	rsi, [outbuf]
	mov	rdx, 4096
	call	diag_render_text
	mov	r13, rax
	test	rdx, rdx
	jnz	.truncated

	lea	rdi, [outbuf]
	mov	rsi, r13
	lea	rdx, [exptext]
	mov	rcx, EXPTEXT_LEN
	call	diagt_eq
	test	eax, eax
	jz	.textbad

	; ---- JSON ----------------------------------------------------------
	lea	rdi, [rec]
	lea	rsi, [outbuf]
	mov	rdx, 4096
	call	diag_render_json
	mov	r13, rax

	lea	rdi, [outbuf]
	mov	rsi, r13
	lea	rdx, [expjrel]
	mov	rcx, EXPJREL_LEN
	call	diagt_find
	test	eax, eax
	jz	.jsonbad

	; and it must NOT have fallen back to null
	lea	rdi, [outbuf]
	mov	rsi, r13
	lea	rdx, [expjnull]
	mov	rcx, EXPJNULL_LEN
	call	diagt_find
	test	eax, eax
	jnz	.jsonnull

	mov	eax, 231
	xor	edi, edi
	syscall

  .textbad:
	mov	edi, 11
	mov	eax, 231
	syscall
  .truncated:
	mov	edi, 12
	mov	eax, 231
	syscall
  .jsonbad:
	mov	edi, 13
	mov	eax, 231
	syscall
  .jsonnull:
	mov	edi, 14
	mov	eax, 231
	syscall

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

; diagt_find(rdi=hay, rsi=hlen, rdx=needle, rcx=nlen) -> eax = 1 if found.
  diagt_find:
	test	rcx, rcx
	jz	.no
	cmp	rsi, rcx
	jb	.no
	mov	r8, rsi
	sub	r8, rcx
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
  pathblob db 'c.exs'
  PATH_LEN = $ - pathblob
  fixtext  db '}'

  ; publica functio una(a: u64) -> u64 {
  ;     mutabilis t: u64 = 0;
  ;     si a gt 3 {
  ;         t = 1;
  ;     redde t;
  ; }
  srcblob:
	db	0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x61, 0x20, 0x66, 0x75, 0x6E, 0x63
	db	0x74, 0x69, 0x6F, 0x20, 0x75, 0x6E, 0x61, 0x28, 0x61, 0x3A, 0x20, 0x75
	db	0x36, 0x34, 0x29, 0x20, 0x2D, 0x3E, 0x20, 0x75, 0x36, 0x34, 0x20, 0x7B
	db	0x0A, 0x20, 0x20, 0x20, 0x20, 0x6D, 0x75, 0x74, 0x61, 0x62, 0x69, 0x6C
	db	0x69, 0x73, 0x20, 0x74, 0x3A, 0x20, 0x75, 0x36, 0x34, 0x20, 0x3D, 0x20
	db	0x30, 0x3B, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x73, 0x69, 0x20, 0x61, 0x20
	db	0x67, 0x74, 0x20, 0x33, 0x20, 0x7B, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20
	db	0x20, 0x20, 0x20, 0x74, 0x20, 0x3D, 0x20, 0x31, 0x3B, 0x0A, 0x20, 0x20
	db	0x20, 0x20, 0x72, 0x65, 0x64, 0x64, 0x65, 0x20, 0x74, 0x3B, 0x0A, 0x7D
	db	0x0A
  SRC_LEN = $ - srcblob

  ; The whole diagnostic, exactly as a terminal shows it. The empty line 7 is
  ; the file's zero-length final line: the span is one past the last byte,
  ; which is where end-of-input is, and the related block below is what makes
  ; that answerable.
  exptext:
	db	'c.exs:7:1: error[EXS-E0202]: unterminated construct',10
	db	' 7 | ',10
	db	'   | ^',10
	db	'   = note: the file ends inside a construct opened earlier; it wants }',10
	db	'   = opened here',10
	db	' 3 |     si a gt 3 {',10
	db	'   |               ^',10
	db	'   = suggestion (insert): bytes 108..108 -> "}"',10
  EXPTEXT_LEN = $ - exptext

  expjrel  db '"related":{"file_id":1,"start":77,"len":1}'
  EXPJREL_LEN = $ - expjrel
  expjnull db '"related":null'
  EXPJNULL_LEN = $ - expjnull

  rec	rb sizeof.Diag
  outbuf rb 4096
