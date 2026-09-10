; tests/unit/lexer_ident_scripts.asm
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
; §8.2's UTS #39 Moderately Restrictive level, from both directions.
;
; TWELVE OF THE EIGHTEEN ROWS MUST PRODUCE NO DIAGNOSTIC AT ALL, and they come
; first because they are the failure this file is most likely to catch. §8.2
; says "Non-ASCII identifiers are allowed. Non-ASCII keywords are not", so a
; lexer that rejects `café` has not implemented §8.2 -- it has broken it, and
; it would still pass a fixture that only checked that mixed-script input is
; rejected. Latin-with-a-combining-free-acute, Greek, Cyrillic, Han,
; Hiragana+Han, Arabic and `_priv1` all tokenize clean.
;
; Three of the clean rows are specifically the LEVEL 3 combinations, which a
; naive "all characters share one script" test gets wrong: Latin+Katakana
; (Jpan), Han+Bopomofo (Hanb) and Han+Hangul (Kore) are each legitimately
; mixed-script and each must be accepted. Latin+Devanagari is the LEVEL 4 row
; -- Latin plus one other script that is not Cyrillic, Greek or Cherokee.
;
; SIX ROWS MUST PRODUCE EXACTLY `EXS-E0104`, with the span covering the whole
; identifier and nothing else. Three of them are the level-4 exclusions named
; in UTS #39 -- Latin+Cyrillic, Latin+Greek, Latin+Cherokee -- which is what
; makes those three rows different from the Latin+Devanagari row above rather
; than redundant with the others. The last row is the CVE-2021-42694 shape
; itself: `paypal` with a Cyrillic `р` and `а` spliced in.
;
; What this fixture does NOT test is `EXS-E0105`. §8.2 scopes confusable
; detection to the IMPORT CLOSURE, which a single-file lexer cannot see, and
; compiler/shared/unicode/README.md records that the UTS #39 confusables data
; is not hermetically available. It lands with the `ego` reader.
;
; Exit 0 = all checks passed; 11 = wrong code, 12 = wrong count, 13 = wrong
; span start, 14 = wrong span length.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/lexer/lexer.inc'

LT_ARENA = 1 shl 20

segment readable executable
  start:
	call	lt_setup
	xor	rbx, rbx
  .case:
	cmp	rbx, CASE_COUNT
	jge	.case_done
	lea	rcx, [rbx + rbx*2]	; 6 dd per row -- *24 needs a legal scale
	lea	rdi, [case_text]
	mov	eax, [case_tab + rcx*8]
	add	rdi, rax
	mov	esi, [case_tab + rcx*8 + 4]
	call	lt_run
	lea	rcx, [rbx + rbx*2]
	mov	r12d, [case_tab + rcx*8 + 12]
	cmp	eax, r12d
	jne	.fail1			; wrong first code
	call	lt_ndiag
	lea	rcx, [rbx + rbx*2]
	mov	r12d, [case_tab + rcx*8 + 8]
	cmp	eax, r12d
	jne	.fail2			; wrong diagnostic COUNT
	test	eax, eax
	jz	.case_next
	xor	rdi, rdi
	call	lt_diag
	lea	rcx, [rbx + rbx*2]
	mov	r12d, [case_tab + rcx*8 + 16]
	cmp	[rax + Diag.span.start], r12d
	jne	.fail3			; wrong span start
	lea	rcx, [rbx + rbx*2]
	mov	r12d, [case_tab + rcx*8 + 20]
	cmp	[rax + Diag.span.len], r12d
	jne	.fail4			; wrong span length
  .case_next:
	inc	rbx
	jmp	.case
  .case_done:

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

; ---- harness ---------------------------------------------------------------
; Plain labels, not `proc`: a `proc` argument name becomes an unmangled global
; (macros/proc.inc's header), and this fixture wants no part of that namespace.
; Every helper pushes an ODD number of registers so that `rsp` is 16-aligned at
; the `call`s inside it -- at a helper's first instruction `rsp` is 8 (mod 16),
; because `call` pushed the return address.
;
; NOTE, found by running: the sample path label here is `lt_path` and NOT
; `pathname`, because `rt/sys.inc` has `proc sys_openat, dirfd, pathname, ...`
; and every proc argument name is a global `= rbp - K`. A data label named
; `pathname` therefore resolves to a register expression, and the failure
; surfaces as "variable term used where not expected" at the USE site, naming
; neither the label nor sys.inc.

; lt_setup -- two arenas and an interner. The interner's arena is separate and
; is NEVER reset: rt/intern.inc's header makes an interned id valid only for as
; long as the arena behind it lives, and lt_run rewinds the other one per case.
  lt_setup:
	push	rbx
	lea	rdi, [lt_arena]
	mov	rsi, LT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [lt_iarena]
	mov	rsi, LT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [lt_intern]
	lea	rsi, [lt_iarena]
	mov	rdx, 64
	call	intern_init
	pop	rbx
	ret
  .boom:
	mov	eax, 231
	mov	edi, 99
	syscall

; lt_run(rdi = source bytes, rsi = byte length) -> eax = the numeric part of
; the FIRST diagnostic, or 0 if the file is clean. Fresh vectors and a rewound
; scratch arena every call, so cases cannot leak into one another.
  lt_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	lea	rdi, [lt_arena]
	call	arena_reset
	lea	rdi, [lt_toks]
	lea	rsi, [lt_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 8
	call	vec_init
	lea	rdi, [lt_diags]
	lea	rsi, [lt_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 4
	call	vec_init
	lea	rdi, [lt_lx]
	lea	rsi, [lt_arena]
	lea	rdx, [lt_intern]
	lea	rcx, [lt_toks]
	lea	r8,  [lt_diags]
	call	lex_init
	lea	rdi, [lt_lx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [lt_path]
	mov	r8d, LT_PATH_LEN
	call	lex_set_source
	lea	rdi, [lt_lx]
	call	lex_run
	pop	r13
	pop	r12
	pop	rbx
	ret

; lt_ndiag -> rax = how many diagnostics the last lt_run recorded.
  lt_ndiag:
	lea	rax, [lt_diags]
	mov	rax, [rax + Vec.len]
	ret

; lt_diag(rdi = index) -> rax = that `Diag`. Traps through vec_get if the
; index is out of range, which is what a wrong expected-count would produce.
  lt_diag:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [lt_diags]
	call	vec_get
	pop	rbx
	ret

; lt_ntok -> rax = how many tokens the last lt_run produced.
  lt_ntok:
	lea	rax, [lt_toks]
	mov	rax, [rax + Vec.len]
	ret

; lt_tok(rdi = index) -> rax = that `Tok`.
  lt_tok:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [lt_toks]
	call	vec_get
	pop	rbx
	ret

segment readable
  lt_path:	db 'fixture.exsc'
  lt_path_end:
  LT_PATH_LEN = lt_path_end - lt_path

  case_text:
	db	0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x73, 0x61, 0x6C, 0x75, 0x74, 0x61
	db	0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x63
	db	0x61, 0x66, 0xC3, 0xA9, 0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66, 0x69, 0x72
	db	0x6D, 0x61, 0x20, 0xCE, 0xB1, 0xCE, 0xB2, 0xCE, 0xB3, 0x20, 0x3D, 0x20
	db	0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0xD0, 0xB4, 0xD0, 0xBE
	db	0xD0, 0xBC, 0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61
	db	0x20, 0xE6, 0xBC, 0xA2, 0xE5, 0xAD, 0x97, 0x20, 0x3D, 0x20, 0x31, 0x0A
	db	0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0xE3, 0x81, 0xB2, 0xE3, 0x82, 0x89
	db	0xE3, 0x81, 0x8C, 0xE3, 0x81, 0xAA, 0xE6, 0xBC, 0xA2, 0x20, 0x3D, 0x20
	db	0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0xD9, 0x85, 0xD8, 0xB1
	db	0xD8, 0xAD, 0xD8, 0xA8, 0xD8, 0xA7, 0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66
	db	0x69, 0x72, 0x6D, 0x61, 0x20, 0x5F, 0x70, 0x72, 0x69, 0x76, 0x31, 0x20
	db	0x3D, 0x20, 0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x61, 0xE3
	db	0x82, 0xAB, 0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61
	db	0x20, 0xE6, 0xBC, 0xA2, 0xE3, 0x84, 0x85, 0x20, 0x3D, 0x20, 0x31, 0x0A
	db	0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0xE6, 0xBC, 0xA2, 0xED, 0x95, 0x9C
	db	0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x70
	db	0x61, 0xE0, 0xA4, 0x95, 0x6C, 0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66, 0x69
	db	0x72, 0x6D, 0x61, 0x20, 0x70, 0x61, 0xD1, 0x83, 0x6C, 0x20, 0x3D, 0x20
	db	0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x70, 0x61, 0xCF, 0x81
	db	0x6C, 0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20
	db	0x70, 0x61, 0xE1, 0x8F, 0x93, 0x6C, 0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66
	db	0x69, 0x72, 0x6D, 0x61, 0x20, 0xD0, 0xB4, 0xD0, 0xBE, 0xD0, 0xBC, 0xCE
	db	0xB1, 0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20
	db	0xE6, 0xBC, 0xA2, 0xD0, 0xB4, 0xD0, 0xBE, 0xD0, 0xBC, 0x20, 0x3D, 0x20
	db	0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0xD1, 0x80, 0xD0, 0xB0
	db	0x79, 0x70, 0x61, 0x6C, 0x20, 0x3D, 0x20, 0x31, 0x0A

  case_tab:
	dd	0, 17, 0, 0, 0, 0	; ASCII only
	dd	17, 16, 0, 0, 0, 0	; Latin with acute
	dd	33, 17, 0, 0, 0, 0	; Greek
	dd	50, 17, 0, 0, 0, 0	; Cyrillic
	dd	67, 17, 0, 0, 0, 0	; Han
	dd	84, 26, 0, 0, 0, 0	; Hiragana plus Han
	dd	110, 21, 0, 0, 0, 0	; Arabic
	dd	131, 17, 0, 0, 0, 0	; Latin, digits and _
	dd	148, 15, 0, 0, 0, 0	; Latin plus Katakana
	dd	163, 17, 0, 0, 0, 0	; Han plus Bopomofo
	dd	180, 17, 0, 0, 0, 0	; Han plus Hangul
	dd	197, 17, 0, 0, 0, 0	; Latin plus Devanagari
	dd	214, 16, 1, 104, 6, 5	; Latin plus Cyrillic
	dd	230, 16, 1, 104, 6, 5	; Latin plus Greek
	dd	246, 17, 1, 104, 6, 6	; Latin plus Cherokee
	dd	263, 19, 1, 104, 6, 8	; Cyrillic plus Greek
	dd	282, 20, 1, 104, 6, 9	; Han plus Cyrillic
	dd	302, 19, 1, 104, 6, 8	; the paypal homoglyph
  case_tab_end:
  CASE_COUNT = (case_tab_end - case_tab) / 24
  ; A table that parsed to zero rows would make the loop above
  ; pass by never running. fasmg's native assemble-time `assert`
  ; (NOT macros/assert.inc's runtime `rassert`) refuses that.
  assert CASE_COUNT = 18

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  lt_arena	rb sizeof.Arena
  lt_iarena	rb sizeof.Arena
  lt_intern	rb sizeof.Interner
  lt_toks	rb sizeof.Vec
  lt_diags	rb sizeof.Vec
  lt_lx		rb sizeof.Lexer
