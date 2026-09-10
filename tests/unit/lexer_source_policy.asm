; tests/unit/lexer_source_policy.asm
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
; §8.1's source policy, one case per rule, each asserted against the EXACT
; `EXS-E` code, the exact diagnostic COUNT and the exact SPAN -- not merely
; "an error happened". A lexer that answers `EXS-E0101` to everything would
; pass a code-only check on eleven of these fourteen rows.
;
; The span assertions are the half that catches the subtle bugs. `EXS-E0106`
; on a CRLF pair spans two bytes and on a lone CR spans one, which is how the
; diagnostic says WHICH it found without a second code (§8.1 and §13 name only
; "CRLF", and CLAUDE.md forbids inventing a code for the lone-CR case). The
; `EXS-E0102` span is (9, 3), not (6, 5): lexer/source.inc cuts NFC segments at
; ASCII boundaries, so the reported run is the ASCII `e` plus the combining
; acute that could compose with it -- exactly the two codepoints at fault, not
; the whole identifier.
;
; Two rows encode a POLICY, not just a rule, and would silently regress
; without them:
;   - "BOM then bidi" expects TWO diagnostics. §8.1 says a BOM "is EXS-E0101,
;     not a skipped byte", but the rest of the file may be perfectly readable,
;     so the scan reports and steps over it. If it stopped, the override on
;     the next line would go unreported.
;   - "two non-NFC runs, one diagnostic" pins EXS-E0102 as report-once per
;     file. NFC is a whole-file property whose remedy is to normalise the
;     file; a copy of the same diagnostic per identifier tells the reader
;     nothing. EXS-E0103 is the opposite and is pinned that way in
;     tests/unit/lexer_trojan_source.asm.
;
; Six of the fourteen rows are invalid UTF-8, one per way to be invalid, because
; `EXS-E0101` is only as strong as the decoder behind it: a stray 0xFF, an
; overlong two-byte 'A', a lone surrogate half, a sequence truncated at end of
; input, a stray continuation byte, a 0xF8 lead, and a codepoint past U+10FFFF.
; A decoder that accepted overlongs would let a dangerous codepoint through in
; non-shortest form and defeat EXS-E0103 entirely.
;
; Check 5 pins the CF half of the return protocol. The loop reads `eax` only,
; so a `lex_run` that reported the right code and never touched CF would pass
; all fourteen rows and break every caller that writes `jc` after the call --
; which is exactly what docs/asm-conventions.md tells callers to write.
;
; Exit 0 = all checks passed; 11 = wrong code, 12 = wrong count, 13 = wrong
; span start, 14 = wrong span length, 15 = the CF/eax contract was violated.
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

	; ---- check 5: the CF/eax protocol itself ---------------------------
	; docs/asm-conventions.md, "1.3 Error protocol: CF / eax": CF set means
	; a diagnosable failure with the code in eax, CF clear means success.
	; The loop above reads eax alone, so a `lex_run` that never touched CF
	; would pass all fourteen rows. `lt_run` returns whatever CF `lex_run`
	; left, and nothing between the `call` and the `jc` disturbs it.
	lea	rdi, [case_text]
	mov	esi, CLEAN_LEN
	call	lt_run
	jc	.fail5			; a clean file must leave CF clear
	test	eax, eax
	jnz	.fail5
	lea	rdi, [case_text + BOM_OFF]
	mov	esi, BOM_LEN
	call	lt_run
	jnc	.fail5			; a rejected file must set CF
	cmp	eax, 101
	jne	.fail5

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
  .fail5:
	mov	eax, 231
	mov	edi, 15
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

  ; Every case's bytes, concatenated.
  case_text:
	db	0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x61, 0x20, 0x66, 0x75, 0x6E, 0x63
	db	0x74, 0x69, 0x6F, 0x20, 0x66, 0x28, 0x29, 0x20, 0x7B, 0x7D, 0x0A, 0xEF
	db	0xBB, 0xBF, 0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x61, 0x0A, 0x66, 0x69
	db	0x72, 0x6D, 0x61, 0x20, 0x78, 0x20, 0x3D, 0x20, 0x31, 0x0D, 0x0A, 0x66
	db	0x69, 0x72, 0x6D, 0x61, 0x0D, 0x78, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61
	db	0x20, 0x63, 0x61, 0x66, 0x65, 0xCC, 0x81, 0x20, 0x3D, 0x20, 0x31, 0x0A
	db	0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0xFF, 0x0A, 0x66, 0x69, 0x72, 0x6D
	db	0x61, 0x20, 0xC1, 0x81, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0xED
	db	0xA0, 0x80, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0xE2, 0x82, 0x66
	db	0x69, 0x72, 0x6D, 0x61, 0x20, 0x80, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61
	db	0x20, 0xF8, 0x88, 0x80, 0x80, 0x80, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61
	db	0x20, 0xF4, 0x90, 0x80, 0x80, 0x0A, 0xEF, 0xBB, 0xBF, 0x2F, 0x2F, 0x20
	db	0xE2, 0x80, 0xAE, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x63, 0x61
	db	0x66, 0x65, 0xCC, 0x81, 0x20, 0x3D, 0x20, 0x63, 0x61, 0x66, 0x65, 0xCC
	db	0x81, 0x0A

  ; (offset, length, expected diagnostic count, expected first code,
  ;  expected first span start, expected first span length)
  case_tab:
	dd	0, 23, 0, 0, 0, 0	; clean ASCII
	dd	23, 11, 1, 101, 0, 3	; BOM at offset 0
	dd	34, 13, 1, 106, 11, 2	; CRLF
	dd	47, 8, 1, 106, 5, 1	; lone CR
	dd	55, 17, 1, 102, 9, 3	; non-NFC identifier
	dd	72, 8, 1, 101, 6, 1	; stray 0xFF
	dd	80, 9, 1, 101, 6, 1	; overlong 2-byte A
	dd	89, 10, 1, 101, 6, 1	; surrogate half
	dd	99, 8, 1, 101, 6, 1	; truncated at EOF
	dd	107, 8, 1, 101, 6, 1	; stray continuation
	dd	115, 12, 1, 101, 6, 1	; 5-byte lead 0xF8
	dd	127, 11, 1, 101, 6, 1	; past U+10FFFF
	dd	138, 10, 2, 101, 0, 3	; BOM then bidi
	dd	148, 22, 1, 102, 9, 3	; two non-NFC runs, one diagnostic
  case_tab_end:
  CASE_COUNT = (case_tab_end - case_tab) / 24
  ; A table that parsed to zero rows would make the loop above
  ; pass by never running. fasmg's native assemble-time `assert`
  ; (NOT macros/assert.inc's runtime `rassert`) refuses that.
  assert CASE_COUNT = 14

  CLEAN_LEN = 23
  BOM_OFF   = 23
  BOM_LEN   = 11

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  lt_arena	rb sizeof.Arena
  lt_iarena	rb sizeof.Arena
  lt_intern	rb sizeof.Interner
  lt_toks	rb sizeof.Vec
  lt_diags	rb sizeof.Vec
  lt_lx		rb sizeof.Lexer
