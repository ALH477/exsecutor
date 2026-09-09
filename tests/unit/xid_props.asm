; tests/unit/xid_props.asm
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
; Consumer fixture for compiler/shared/unicode/xid.inc (spec §8.2).
;
; A table with no consumer test is a table nobody has read: this is the
; fixture that actually indexes `xid_stage1`/`xid_stage2` from assembly and
; checks the answers against values read independently out of the same blobs
; with Python. Every expected value below was MEASURED that way, not recalled.
;
; It also proves three toolchain facts the three unicode consumers all rest
; on, none of which had been demonstrated before this wave:
;   - `[bare_label + reg*scale]` addresses `file` (incbin) data correctly;
;   - a FORWARD reference to such a label works -- tables.inc is included at
;     the BOTTOM of this file, after every `proc` that indexes it;
;   - `file 'ccc_stage1.bin'` inside tables/tables.inc resolves relative to
;     tables/, i.e. to the including file's own directory, not to this one.
;
; Rows cover: ASCII letter/digit/space/punctuation, `_` (the §8.2 special
; case -- U+005F carries XID_Continue but NOT XID_Start in UCD 17.0.0, so the
; Start answer here comes from xid.inc's rule, not from the table), Greek,
; CJK from the BMP and from plane 2, a combining mark (Continue but not
; Start), a plane-1 math letter, an emoji, and the last codepoint.
;
; Exit 0 = all rows passed; 10+i = row i mismatched (rows are 0-based, in
; source order below).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/shared/unicode/xid.inc'

segment readable executable
  start:
	xor	ebx, ebx		; row index
  .loop:
	cmp	ebx, ROWS
	jge	.done
	mov	r12d, ebx
	shl	r12d, 4			; 16 bytes per row
	mov	edi, [cases + r12]
	call	xid_flags
	cmp	eax, [cases + r12 + 4]
	jne	.fail
	mov	edi, [cases + r12]
	call	xid_start
	cmp	eax, [cases + r12 + 8]
	jne	.fail
	mov	edi, [cases + r12]
	call	xid_continue
	cmp	eax, [cases + r12 + 12]
	jne	.fail
	inc	ebx
	jmp	.loop
  .done:
	mov	eax, 231
	xor	edi, edi
	syscall
  .fail:
	lea	edi, [rbx + 10]
	mov	eax, 231
	syscall

segment readable writeable
			; cp        flags  start  continue
  cases:
	dd	0x00061,    3,     1,     1	; row 0  LATIN SMALL A
	dd	0x0005F,    2,     1,     1	; row 1  LOW LINE -- §8.2's "plus _"
	dd	0x00030,    2,     0,     1	; row 2  DIGIT ZERO
	dd	0x00020,    0,     0,     0	; row 3  SPACE
	dd	0x0002D,    0,     0,     0	; row 4  HYPHEN-MINUS
	dd	0x00024,    0,     0,     0	; row 5  DOLLAR SIGN
	dd	0x003B1,    3,     1,     1	; row 6  GREEK SMALL ALPHA
	dd	0x00301,    2,     0,     1	; row 7  COMBINING ACUTE ACCENT
	dd	0x04E2D,    3,     1,     1	; row 8  CJK U+4E2D
	dd	0x2070E,    3,     1,     1	; row 9  CJK ext B, plane 2
	dd	0x1D400,    3,     1,     1	; row 10 MATH BOLD CAPITAL A
	dd	0x1F600,    0,     0,     0	; row 11 GRINNING FACE
	dd	0x10FFFF,   0,     0,     0	; row 12 last codepoint
  cases_end:
  ROWS = (cases_end - cases) / 16

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'
