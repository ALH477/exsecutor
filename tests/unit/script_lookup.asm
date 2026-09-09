; tests/unit/script_lookup.asm
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
; Consumer fixture for compiler/shared/unicode/script.inc (spec §8.2, the
; data behind UTS #39 Moderately Restrictive / `EXS-E0104`).
;
; Two phases. Every expected value was read independently out of the same
; checked-in blobs with Python, not recalled.
;
;   Phase 1, `script_of`: the primary Script trie. Covers Latin, Cyrillic,
;   Greek, Han, Katakana, Common (digits, `_`, tatweel, an emoji), Inherited
;   (a combining mark), and Unknown (unassigned, and the last codepoint).
;
;   Phase 2, `script_resolve`: the Script_Extensions sparse array plus the
;   fall-back-to-primary path. Deliberately includes the FIRST key in the
;   table (U+00B7, 16 ids), the LAST key (U+1F251, 1 id) and the LONGEST
;   entry (U+0965, 23 ids) -- the three rows a binary search or a copy loop
;   is most likely to get wrong -- plus four codepoints with no explicit
;   extensions at all, which must come back as the single-element set holding
;   their primary script.
;
;   Every call passes dstcap = UNI_SCRIPTEXT_MAX_COUNT exactly, so the
;   23-element row also proves script_resolve's capacity `rassert` does not
;   fire at its own boundary.
;
; Exit 0 = everything passed; 10+i = script_of row i; 40+j = script_resolve
; row j returned the wrong count; 70+j = row j wrote the wrong ids.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/shared/unicode/script.inc'

segment readable executable
  start:
	; ---- phase 1: script_of ------------------------------------------
	xor	ebx, ebx
  .of_loop:
	cmp	ebx, OF_ROWS
	jge	.resolve
	mov	r12d, ebx
	shl	r12d, 3			; 8 bytes per row
	mov	edi, [of_cases + r12]
	call	script_of
	cmp	eax, [of_cases + r12 + 4]
	jne	.of_fail
	inc	ebx
	jmp	.of_loop
  .of_fail:
	lea	rdi, [rbx + 10]
	jmp	.exit

	; ---- phase 2: script_resolve -------------------------------------
  .resolve:
	xor	ebx, ebx
  .rs_loop:
	cmp	ebx, RS_ROWS
	jge	.done
	mov	r12d, ebx
	shl	r12d, 5			; 32 bytes per row
	mov	edi, [rs_cases + r12]
	lea	rsi, [resbuf]
	mov	edx, UNI_SCRIPTEXT_MAX_COUNT
	call	script_resolve
	cmp	eax, [rs_cases + r12 + 4]
	jne	.rs_count_fail
	; compare eax bytes of resbuf against the row's expected id list
	xor	ecx, ecx
  .rs_cmp:
	cmp	ecx, eax
	jae	.rs_next
	movzx	r8d, byte [resbuf + rcx]
	mov	r9d, r12d
	add	r9d, ecx
	movzx	r10d, byte [rs_cases + r9 + 8]
	cmp	r8d, r10d
	jne	.rs_byte_fail
	inc	ecx
	jmp	.rs_cmp
  .rs_next:
	inc	ebx
	jmp	.rs_loop
  .rs_count_fail:
	lea	rdi, [rbx + 40]
	jmp	.exit
  .rs_byte_fail:
	lea	rdi, [rbx + 70]
	jmp	.exit
  .done:
	xor	edi, edi
  .exit:
	mov	eax, 231
	syscall

segment readable writeable
  resbuf:	rb 32

			; cp        expected script id
  of_cases:
	dd	0x00041,    UNI_SCRIPT_LATN	; row 0
	dd	0x00416,    UNI_SCRIPT_CYRL	; row 1  CYRILLIC ZHE
	dd	0x003B1,    UNI_SCRIPT_GREK	; row 2
	dd	0x04E2D,    UNI_SCRIPT_HANI	; row 3
	dd	0x030A2,    UNI_SCRIPT_KANA	; row 4  KATAKANA A
	dd	0x00030,    UNI_SCRIPT_ZYYY	; row 5  DIGIT ZERO -- Common
	dd	0x0005F,    UNI_SCRIPT_ZYYY	; row 6  LOW LINE -- Common
	dd	0x00640,    UNI_SCRIPT_ZYYY	; row 7  ARABIC TATWEEL -- Common
	dd	0x1F600,    UNI_SCRIPT_ZYYY	; row 8  GRINNING FACE -- Common
	dd	0x0064B,    UNI_SCRIPT_ZINH	; row 9  ARABIC FATHATAN -- Inherited
	dd	0x0E0000,   UNI_SCRIPT_ZZZZ	; row 10 unassigned -- Unknown
	dd	0x10FFFF,   UNI_SCRIPT_ZZZZ	; row 11 last codepoint
  of_cases_end:
  OF_ROWS = (of_cases_end - of_cases) / 8

			; cp, count, then `count` ids padded to a 24-byte tail
  rs_cases:
	; row 0 -- FIRST key in uni_scriptext_keys, 16 ids
	dd	0x000B7, 16
	db	6,21,25,33,35,39,40,41,43,45,50,74,81,82,115,129, 0,0,0,0,0,0,0,0
	; row 1 -- LONGEST entry in the table, 23 ids (== UNI_SCRIPTEXT_MAX_COUNT)
	dd	0x00965, 23
	db	11,29,31,41,42,44,46,47,48,69,76,82,91,99,107,109,133,134,141,144,147,151,157, 0
	; row 2 -- 21 ids
	dd	0x00964, 21
	db	11,29,31,41,42,44,46,48,69,82,91,99,107,109,133,134,141,144,147,151,157, 0,0,0
	; row 3 -- ARABIC TATWEEL, 9 ids
	dd	0x00640, 9
	db	0,3,84,85,112,118,123,135,142, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	; row 4 -- ARABIC FATHATAN, 2 ids (Arab, Syrc)
	dd	0x0064B, 2
	db	3,142, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	; row 5 -- KATAKANA-HIRAGANA PROLONGED SOUND MARK, 2 ids (Hira, Kana)
	dd	0x030FC, 2
	db	54,63, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	; row 6 -- IDEOGRAPHIC CLOSING MARK: Common primary, {Hani} extension
	dd	0x03006, 1
	db	50, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	; row 7 -- LAST key in uni_scriptext_keys, 1 id
	dd	0x1F251, 1
	db	50, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	; rows 8..11 -- NO explicit extensions: must fall back to {primary}
	dd	0x00041, 1
	db	UNI_SCRIPT_LATN, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dd	0x00416, 1
	db	UNI_SCRIPT_CYRL, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dd	0x00030, 1
	db	UNI_SCRIPT_ZYYY, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dd	0x10FFFF, 1
	db	UNI_SCRIPT_ZZZZ, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
  rs_cases_end:
  RS_ROWS = (rs_cases_end - rs_cases) / 32
  ; Every row above is 4+4+24 = 32 bytes. Getting one `db` list's length
  ; wrong shifts every later row by that much and turns this fixture into
  ; noise -- which is exactly what happened while it was being written:
  ; four rows carried 23 zeros instead of 24, the search read misaligned
  ; keys, and script_resolve's own capacity `rassert` trapped on the
  ; resulting garbage count (SIGILL, exit 132) instead of the fixture
  ; reporting a row number. fasmg's NATIVE assemble-time `assert` (not
  ; macros/assert.inc's runtime `rassert`) turns that class of typo into
  ; an assembly error at the point of the mistake.
  assert (rs_cases_end - rs_cases) mod 32 = 0
  assert (of_cases_end - of_cases) mod 8 = 0

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'
