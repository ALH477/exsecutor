; tests/unit/nfc_normalize.asm
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
; Consumer fixture for compiler/shared/unicode/nfc.inc (spec §8.1).
;
; This is the HERMETIC regression fixture. The real conformance evidence comes
; from tools/ucd-gen/nfc_conformance.asm, which drives the same routines over
; all 100,170 assertions in UCD `NormalizationTest.txt` -- that corpus is a
; multi-megabyte generated blob and is not checked in, so it cannot live here.
; What lives here is a small case table whose every expected value was
; computed from the checked-in blobs and cross-checked against the corpus
; result, chosen to cover each distinct code path exactly once:
;
;   ccc               starter, class 230, class 220, class 7, class 1
;   compose_pair      table hit, Hangul L+V, Hangul LV+T, three misses --
;                     including U+0915+U+093C, whose composite U+0958 IS a
;                     composition exclusion and so must NOT compose -- plus
;                     ALL FOUR non-starter-left-operand pairs in UCD 17.0.0,
;                     which UAX #15 excludes and which were wrongly present in
;                     the generated table until this wave
;   decompose         no decomposition, pair, singleton, Hangul LV, Hangul
;                     LVT, and U+1F82 -- the deepest/widest case in the whole
;                     table (4 codepoints, recursion depth 3), which is where
;                     UNI_NFC_MAX_EXPANSION comes from
;   quick_check       YES / NO / MAYBE, with a NO from each of the three
;                     Full_Composition_Exclusion sub-cases (singleton U+2126,
;                     non-starter decomposition U+0344, excluded composite
;                     U+0958) and a NO from an adjacent composable pair
;   normalize         identity, composition, exclusion left decomposed,
;                     reordering (U+1E0A U+0323 -> U+1E0C U+0307, where the
;                     mark that reorders comes from inside the FIRST
;                     character's own decomposition), Hangul round trip, and
;                     the empty sequence
;   is_nfc            both answers, on both the quick and the fall-back path
;
; Exit 0 = everything passed. Otherwise: 10+i ccc row i; 30+i compose row i;
; 50+i decompose row i count; 70+i decompose row i content; 90+i sequence row
; i quick_check; 110+i sequence row i is_nfc; 130+i sequence row i normalize
; length; 150+i sequence row i normalize content.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/shared/unicode/nfc.inc'

segment readable executable
  start:
	; ---- phase 1: nfc_ccc ---------------------------------------------
	xor	ebx, ebx
  .ccc_loop:
	cmp	ebx, CCC_ROWS
	jge	.compose
	mov	r12d, ebx
	shl	r12d, 3
	mov	edi, [ccc_cases + r12]
	call	nfc_ccc
	cmp	eax, [ccc_cases + r12 + 4]
	jne	.ccc_fail
	inc	ebx
	jmp	.ccc_loop
  .ccc_fail:
	lea	rdi, [rbx + 10]
	jmp	.exit

	; ---- phase 2: nfc_compose_pair ------------------------------------
  .compose:
	xor	ebx, ebx
  .cmp_loop:
	cmp	ebx, CMP_ROWS
	jge	.decompose
	mov	r12d, ebx
	shl	r12d, 4
	mov	edi, [cmp_cases + r12]
	mov	esi, [cmp_cases + r12 + 4]
	call	nfc_compose_pair
	cmp	eax, [cmp_cases + r12 + 8]
	jne	.cmp_fail
	inc	ebx
	jmp	.cmp_loop
  .cmp_fail:
	lea	rdi, [rbx + 30]
	jmp	.exit

	; ---- phase 3: nfc_decompose ---------------------------------------
  .decompose:
	xor	ebx, ebx
  .dec_loop:
	cmp	ebx, DEC_ROWS
	jge	.sequences
	mov	r12d, ebx
	shl	r12d, 5			; 32 bytes per row
	mov	edi, [dec_cases + r12]
	lea	rsi, [outbuf]
	mov	edx, UNI_NFC_MAX_EXPANSION
	call	nfc_decompose
	cmp	eax, [dec_cases + r12 + 4]
	jne	.dec_count_fail
	xor	ecx, ecx
  .dec_cmp:
	cmp	ecx, eax
	jae	.dec_next
	mov	r8d, [outbuf + rcx*4]
	mov	r9d, r12d
	shr	r9d, 2			; row byte offset -> dword index
	add	r9d, 2			; skip cp and count
	add	r9d, ecx
	cmp	r8d, [dec_cases + r9*4]
	jne	.dec_byte_fail
	inc	ecx
	jmp	.dec_cmp
  .dec_next:
	inc	ebx
	jmp	.dec_loop
  .dec_count_fail:
	lea	rdi, [rbx + 50]
	jmp	.exit
  .dec_byte_fail:
	lea	rdi, [rbx + 70]
	jmp	.exit

	; ---- phase 4: quick_check / is_nfc / normalize on sequences -------
  .sequences:
	xor	ebx, ebx
  .seq_loop:
	cmp	ebx, SEQ_ROWS
	jge	.done
	mov	r12d, ebx
	shl	r12d, 6			; 64 bytes per row
	lea	r13, [seq_cases + r12]	; row base; src at +16, expected NFC at +40
					; (r15 is deliberately not used as scratch
					;  anywhere here -- docs/asm-conventions.md,
					;  "1.2 The r15 pin")
	lea	rdi, [r13 + 16]
	mov	esi, [r13]		; srclen
	call	nfc_quick_check
	cmp	eax, [r13 + 4]
	jne	.seq_qc_fail

	lea	rdi, [r13 + 16]
	mov	esi, [r13]
	lea	rdx, [scratchbuf]
	mov	ecx, SEQ_CAP
	call	nfc_is_nfc
	cmp	eax, [r13 + 8]
	jne	.seq_isnfc_fail

	lea	rdi, [r13 + 16]
	mov	esi, [r13]
	lea	rdx, [outbuf]
	mov	ecx, SEQ_CAP
	call	nfc_normalize
	cmp	eax, [r13 + 12]
	jne	.seq_len_fail
	xor	ecx, ecx
  .seq_cmp:
	cmp	ecx, eax
	jae	.seq_next
	mov	r8d, [outbuf + rcx*4]
	cmp	r8d, [r13 + rcx*4 + 40]
	jne	.seq_content_fail
	inc	ecx
	jmp	.seq_cmp
  .seq_next:
	inc	ebx
	jmp	.seq_loop
  .seq_qc_fail:
	lea	rdi, [rbx + 90]
	jmp	.exit
  .seq_isnfc_fail:
	lea	rdi, [rbx + 110]
	jmp	.exit
  .seq_len_fail:
	lea	rdi, [rbx + 130]
	jmp	.exit
  .seq_content_fail:
	lea	rdi, [rbx + 150]
	jmp	.exit

  .done:
	xor	edi, edi
  .exit:
	mov	eax, 231
	syscall

segment readable writeable
  SEQ_CAP = 32
  outbuf:	rd SEQ_CAP
  scratchbuf:	rd SEQ_CAP

			; cp        ccc
  ccc_cases:
	dd	0x00041,    0		; row 0 LATIN CAPITAL A -- a starter
	dd	0x00301,    230		; row 1 COMBINING ACUTE ACCENT
	dd	0x00323,    220		; row 2 COMBINING DOT BELOW
	dd	0x0093C,    7		; row 3 DEVANAGARI SIGN NUKTA
	dd	0x00334,    1		; row 4 COMBINING TILDE OVERLAY
	dd	0x0005F,    0		; row 5 LOW LINE
  ccc_cases_end:
  CCC_ROWS = (ccc_cases_end - ccc_cases) / 8
  assert (ccc_cases_end - ccc_cases) mod 8 = 0

			; a          b          composite (0 = does not compose)
  cmp_cases:
	dd	0x00041, 0x0030A, 0x000C5, 0	; row 0 A + ring -> A-with-ring
	dd	0x00044, 0x00307, 0x01E0A, 0	; row 1 D + dot above
	dd	0x01100, 0x01161, 0x0AC00, 0	; row 2 Hangul L + V -> LV
	dd	0x0AC00, 0x011A8, 0x0AC01, 0	; row 3 Hangul LV + T -> LVT
	dd	0x0AC01, 0x011A8, 0,      0	; row 4 LVT already has a T
	dd	0x01100, 0x011A8, 0,      0	; row 5 L + T is not a pair
	dd	0x00041, 0x00042, 0,      0	; row 6 two ordinary letters
	dd	0x00915, 0x0093C, 0,      0	; row 7 composite U+0958 is EXCLUDED
	; Rows 8..11 -- NEGATIVE CONTROL for UAX #15's "non-starter
	; decompositions" exclusion class. These are the complete set of pairs
	; in UCD 17.0.0 whose left operand has a non-zero combining class, and
	; every one of them MUST report "does not compose". They were present
	; in the generated compose table until `gen.py`'s `emit_compose` learned
	; to filter on ccc(left) != 0; normalization never reached them (it only
	; ever offers the last starter as a left operand) but `nfc_compose_pair`
	; is public and a caller can ask directly, which is why these four rows
	; exist rather than a note saying it does not matter.
	dd	0x00308, 0x00301, 0,      0	; row 8  -> U+0344 (ccc 230)
	dd	0x00F71, 0x00F72, 0,      0	; row 9  -> U+0F73 (ccc 129)
	dd	0x00F71, 0x00F74, 0,      0	; row 10 -> U+0F75 (ccc 129)
	dd	0x00F71, 0x00F80, 0,      0	; row 11 -> U+0F81 (ccc 129)
  cmp_cases_end:
  CMP_ROWS = (cmp_cases_end - cmp_cases) / 16
  assert (cmp_cases_end - cmp_cases) mod 16 = 0

			; cp, count, then up to 4 codepoints (32-byte rows)
  dec_cases:
	dd	0x00041, 1, 0x00041, 0, 0, 0, 0, 0		; row 0 no decomposition
	dd	0x000C5, 2, 0x00041, 0x0030A, 0, 0, 0, 0	; row 1 canonical pair
	dd	0x02126, 1, 0x003A9, 0, 0, 0, 0, 0		; row 2 singleton (OHM SIGN)
	dd	0x00958, 2, 0x00915, 0x0093C, 0, 0, 0, 0	; row 3 excluded composite
	dd	0x00344, 2, 0x00308, 0x00301, 0, 0, 0, 0	; row 4 non-starter decomposition
	dd	0x0AC00, 2, 0x01100, 0x01161, 0, 0, 0, 0	; row 5 Hangul LV -- algorithmic
	dd	0x0AC01, 3, 0x01100, 0x01161, 0x011A8, 0, 0, 0	; row 6 Hangul LVT
	dd	0x01F82, 4, 0x003B1, 0x00313, 0x00300, 0x00345, 0, 0	; row 7 the widest entry
	dd	0x00F73, 2, 0x00F71, 0x00F72, 0, 0, 0, 0	; row 8 decomposes but no
								;       longer recomposes
  dec_cases_end:
  DEC_ROWS = (dec_cases_end - dec_cases) / 32
  assert (dec_cases_end - dec_cases) mod 32 = 0

			; srclen, quick_check, is_nfc, nfc_len | src[6] | nfc[6]
  seq_cases:
	; row 0 -- empty sequence: vacuously NFC
	dd	0, UNI_NFC_YES,   1, 0
	dd	0,0,0,0,0,0
	dd	0,0,0,0,0,0
	; row 1 -- plain ASCII
	dd	3, UNI_NFC_YES,   1, 3
	dd	0x00061,0x00062,0x00063,0,0,0
	dd	0x00061,0x00062,0x00063,0,0,0
	; row 2 -- a precomposed letter alone
	dd	1, UNI_NFC_YES,   1, 1
	dd	0x000C5,0,0,0,0,0
	dd	0x000C5,0,0,0,0,0
	; row 3 -- decomposed: MAYBE, and normalization composes it
	dd	2, UNI_NFC_MAYBE, 0, 1
	dd	0x00041,0x0030A,0,0,0,0
	dd	0x000C5,0,0,0,0,0
	; row 4 -- the reordering case: the mark that moves comes from inside
	;          U+1E0A's OWN decomposition, which is why no cheap
	;          look-at-the-characters-as-written rule can answer it
	dd	2, UNI_NFC_MAYBE, 0, 2
	dd	0x01E0A,0x00323,0,0,0,0
	dd	0x01E0C,0x00307,0,0,0,0
	; row 5 -- singleton decomposition: NFC_QC = No
	dd	1, UNI_NFC_NO,    0, 1
	dd	0x02126,0,0,0,0,0
	dd	0x003A9,0,0,0,0,0
	; row 6 -- non-starter decomposition: NFC_QC = No
	dd	1, UNI_NFC_NO,    0, 2
	dd	0x00344,0,0,0,0,0
	dd	0x00308,0x00301,0,0,0,0
	; row 7 -- composition exclusion: stays decomposed after normalizing
	dd	1, UNI_NFC_NO,    0, 2
	dd	0x00958,0,0,0,0,0
	dd	0x00915,0x0093C,0,0,0,0
	; row 8 -- two adjacent starters that compose (Hangul L + V)
	dd	2, UNI_NFC_NO,    0, 1
	dd	0x01100,0x01161,0,0,0,0
	dd	0x0AC00,0,0,0,0,0
	; row 9 -- Hangul LVT round trip. NO, not MAYBE: the first two jamo are
	;          plain non-decomposable starters that compose, which the quick
	;          check settles outright. This row was written expecting MAYBE
	;          and the assembly said NO; the reference computation agreed
	;          with the assembly, so the EXPECTATION was the thing that was
	;          wrong. Recorded rather than quietly corrected -- a fixture
	;          edited until it goes green is not evidence of anything.
	dd	3, UNI_NFC_NO,    0, 1
	dd	0x01100,0x01161,0x011A8,0,0,0
	dd	0x0AC01,0,0,0,0,0
	; row 10 -- a precomposed Hangul syllable is already NFC
	dd	1, UNI_NFC_YES,   1, 1
	dd	0x0AC01,0,0,0,0,0
	dd	0x0AC01,0,0,0,0,0
	; row 11 -- an identifier-shaped run: letter, low line, digit
	dd	3, UNI_NFC_YES,   1, 3
	dd	0x00041,0x0005F,0x00030,0,0,0
	dd	0x00041,0x0005F,0x00030,0,0,0
	; row 12 -- two marks of equal class: no reorder, nothing composes
	dd	2, UNI_NFC_MAYBE, 1, 2
	dd	0x00301,0x00300,0,0,0,0
	dd	0x00301,0x00300,0,0,0,0
	; row 13 -- a trailing jamo BEFORE a letter composes with neither
	dd	2, UNI_NFC_YES,   1, 2
	dd	0x011A8,0x00041,0,0,0,0
	dd	0x011A8,0x00041,0,0,0,0
  seq_cases_end:
  SEQ_ROWS = (seq_cases_end - seq_cases) / 64
  assert (seq_cases_end - seq_cases) mod 64 = 0

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'
