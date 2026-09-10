; tests/unit/lexer_keywords.asm
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
; §8.4's reserved set, as `compiler/x86_64/lexer/keywords.inc` actually
; compiles it. tools/spec-check.sh check 4 already proves the keyword SET in
; that file matches §8.4's table -- textually, from outside. What it cannot
; prove is that the LOOKUP built over that data answers correctly, and those
; are different claims: a correct table behind an off-by-one bucket index
; passes check 4 and misidentifies every keyword.
;
; Three checks, and check 2 is the one that is deliberately not circular:
;
;   1. ROUND TRIP over the generated tables. For every i in 0..KW_COUNT,
;      `kw_lookup(kw_text + kw_off[i], kw_len[i])` must be exactly i+1, and
;      `kw_text_of(i+1)` must hand back the same bytes. This exercises every
;      length bucket and every entry of the sorted index, including the
;      one-word buckets (length 4, `apud`; length 11, `interfacies`) and the
;      eight-word bucket (length 5) where a linear probe has the most room to
;      go wrong.
;
;   2. SPOT CHECKS SPELLED OUT HERE, not read from the tables. Nine words,
;      written as literal bytes in this fixture, each asserted against its
;      `KW_*` constant: the two shortest (`si`, `in`), the longest
;      (`interfacies`), the `dyn` case §8.4 calls out as the one English
;      abbreviation in the set, and `forma` -- the word §8.5's own example
;      used as an identifier and had to be corrected for. If a future
;      amendment removes one of these from §8.4, this fixture fails, and that
;      is the correct outcome: dropping a reserved word is not a quiet edit.
;
;   3. NEAR MISSES. Ten strings that are one edit away from a reserved word
;      and must all answer KW_NONE, including `Publica` and `SI` -- §8.2 makes
;      identifiers case-sensitive, so a case-insensitive compare would be a
;      real bug that only a cased near-miss can catch.
;
; Exit 0 = all checks passed; 10+N = check N failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/lexer/lexer.inc'

segment readable executable
  start:
	; ---- check 1: every reserved word round-trips ----------------------
	xor	rbx, rbx
  .rt:
	cmp	rbx, KW_COUNT
	jge	.rt_done
	lea	rdi, [kw_text]
	mov	eax, [kw_off + rbx*4]
	add	rdi, rax
	movzx	esi, byte [kw_len + rbx]
	call	kw_lookup
	mov	ecx, ebx
	inc	ecx
	cmp	eax, ecx
	jne	.fail1

	mov	edi, ecx
	call	kw_text_of		; rax = bytes, rdx = length
	movzx	ecx, byte [kw_len + rbx]
	cmp	rdx, rcx
	jne	.fail1
	lea	rsi, [kw_text]
	mov	ecx, [kw_off + rbx*4]
	add	rsi, rcx
	movzx	ecx, byte [kw_len + rbx]
	mov	rdi, rax
	repe	cmpsb
	jne	.fail1
	inc	rbx
	jmp	.rt
  .rt_done:

	; ---- check 2: spot checks, spelled out in this file ----------------
	xor	rbx, rbx
  .spot:
	cmp	rbx, SPOT_COUNT
	jge	.spot_done
	lea	rcx, [rbx + rbx*2]	; 3 dd per row; *12 is not a legal scale
	lea	rdi, [spot_text]
	mov	eax, [spot_tab + rcx*4]
	add	rdi, rax
	mov	esi, [spot_tab + rcx*4 + 4]
	mov	r12d, [spot_tab + rcx*4 + 8]
	call	kw_lookup
	cmp	eax, r12d
	jne	.fail2
	inc	rbx
	jmp	.spot
  .spot_done:

	; ---- check 3: near misses are not keywords -------------------------
	xor	rbx, rbx
  .miss:
	cmp	rbx, MISS_COUNT
	jge	.miss_done
	lea	rcx, [rbx + rbx*2]
	lea	rdi, [miss_text]
	mov	eax, [miss_tab + rcx*4]
	add	rdi, rax
	mov	esi, [miss_tab + rcx*4 + 4]
	mov	r12d, [miss_tab + rcx*4 + 8]
	call	kw_lookup
	cmp	eax, r12d
	jne	.fail3
	inc	rbx
	jmp	.miss
  .miss_done:

	; A zero-length candidate must miss without touching the tables.
	lea	rdi, [miss_text]
	xor	rsi, rsi
	call	kw_lookup
	test	eax, eax
	jnz	.fail3

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

segment readable
  ; The nine spot-checked words, concatenated. Written here rather than read
  ; from keywords.inc on purpose -- see this file's header, check 2.
  spot_text:
	db	0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x61, 0x73, 0x69, 0x69, 0x6E, 0x64
	db	0x79, 0x6E, 0x69, 0x6E, 0x74, 0x65, 0x72, 0x66, 0x61, 0x63, 0x69, 0x65
	db	0x73, 0x66, 0x6F, 0x72, 0x6D, 0x61, 0x70, 0x65, 0x72, 0x67, 0x65, 0x6D
	db	0x75, 0x74, 0x61, 0x62, 0x69, 0x6C, 0x69, 0x73, 0x63, 0x6F, 0x6E, 0x74
	db	0x72, 0x61, 0x68, 0x65
  ; (byte offset into spot_text, length, expected kw_lookup result)
  spot_tab:
	dd	0, 7, KW_PUBLICA	; publica
	dd	7, 2, KW_SI	; si
	dd	9, 2, KW_IN	; in
	dd	11, 3, KW_DYN	; dyn
	dd	14, 11, KW_INTERFACIES	; interfacies
	dd	25, 5, KW_FORMA	; forma
	dd	30, 5, KW_PERGE	; perge
	dd	35, 9, KW_MUTABILIS	; mutabilis
	dd	44, 8, KW_CONTRAHE	; contrahe
  spot_tab_end:
  SPOT_COUNT = (spot_tab_end - spot_tab) / 12
  assert SPOT_COUNT = 9

  miss_text:
	db	0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x62, 0x70, 0x75, 0x62, 0x6C, 0x69
	db	0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x61, 0x61, 0x50, 0x75, 0x62, 0x6C
	db	0x69, 0x63, 0x61, 0x7A, 0x61, 0x6C, 0x69, 0x74, 0x65, 0x72, 0x72, 0x70
	db	0x65, 0x72, 0x67, 0x72, 0x75, 0x6D, 0x70, 0x65, 0x65, 0x53, 0x49, 0x5F
	db	0x73, 0x69
  miss_tab:
	dd	0, 7, KW_NONE	; 'publicb'
	dd	7, 5, KW_NONE	; 'publi'
	dd	12, 8, KW_NONE	; 'publicaa'
	dd	20, 7, KW_NONE	; 'Publica'
	dd	27, 1, KW_NONE	; 'z'
	dd	28, 7, KW_NONE	; 'aliterr'
	dd	35, 4, KW_NONE	; 'perg'
	dd	39, 6, KW_NONE	; 'rumpee'
	dd	45, 2, KW_NONE	; 'SI'
	dd	47, 3, KW_NONE	; '_si'
  miss_tab_end:
  MISS_COUNT = (miss_tab_end - miss_tab) / 12
  assert MISS_COUNT = 10

  include '../../compiler/shared/unicode/tables/tables.inc'
