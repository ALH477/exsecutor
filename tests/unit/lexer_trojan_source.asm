; tests/unit/lexer_trojan_source.asm
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
; THE FIXTURE §2.2 EXISTS FOR. CVE-2021-42574 (bidi) and CVE-2021-42694
; (homoglyph) work because a reviewer reads rendered text while a compiler
; reads codepoints. §8.1 closes the first by banning fifteen codepoints
; "anywhere in source, INCLUDING strings and comments", and §8.4 says in as
; many words what the wrong implementation looks like: "a lexer that scans
; only identifiers is defeated by an override sitting in a comment."
;
; So the property under test is not "the lexer rejects bidi." It is: the lexer
; rejects bidi IN THE PLACES A TOKEN-AWARE SCAN WOULD MISS. The first four
; cases are one override inside a `//` comment, one inside a `"..."` literal,
; one inside an identifier, and a two-byte one (U+061C) inside a comment --
; four different token contexts, one code, four exact spans.
;
; Case 5 carries ALL FIFTEEN codepoints of §8.1's class -- U+061C, U+200B-200F,
; U+202A-202E, U+2066-2069 -- alternating between comments and string literals,
; and asserts FIFTEEN diagnostics with fifteen exact spans, not one. That is
; the deliberate opposite of `EXS-E0102`'s report-once policy
; (tests/unit/lexer_source_policy.asm pins that one): for a planted-override
; audit, enumerating every occurrence IS the remedy, so stopping at the first
; would be a real regression rather than a tidier output.
;
; Case 6 is the guard against overshooting. Eight codepoints sit immediately
; outside the four ranges -- U+061B, U+061D, U+200A, U+2010, U+2029, U+202F,
; U+2065, U+206A -- and the file containing them must be CLEAN. An off-by-one
; at any range edge is caught here and nowhere else.
;
; Check 7 is end-to-end and independent of every expected-value table above:
; it renders case 1's diagnostic through diag/render.inc and searches the
; rendered bytes for the raw UTF-8 encoding of U+202E. §8.3: "All source
; echoed in a diagnostic is escaped. A diagnostic rendering raw bidi makes the
; error message the attack surface." tests/unit/diag_escape_bidi.asm proves the
; escaper does this in isolation; this proves the LEXER's diagnostic actually
; goes through it.
;
; Exit 0 = all checks passed; 11 = wrong code, 12 = wrong count, 13 = wrong
; span start, 14 = wrong span length, 15 = a case-5 span was wrong, 16 = a raw
; override survived into rendered output.
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

	; ---- check 5: every one of the fifteen spans, in order -------------
	; Re-runs case 5 on its own so the fifteen diagnostics are the ones in
	; the vector, then walks them against a table of exact (start, length)
	; pairs -- so a lexer reporting fifteen times at the wrong offsets
	; fails here even though it passed the count check above.
	lea	rdi, [case_text + ALL15_OFF]
	mov	esi, ALL15_LEN
	call	lt_run
	call	lt_ndiag
	cmp	rax, 15
	jne	.fail5
	xor	rbx, rbx
  .fifteen:
	cmp	rbx, 15
	jge	.fifteen_done
	mov	rdi, rbx
	call	lt_diag
	cmp	dword [rax + Diag.code_num], 103
	jne	.fail5
	mov	ecx, [all15_tab + rbx*8]
	cmp	[rax + Diag.span.start], ecx
	jne	.fail5
	mov	ecx, [all15_tab + rbx*8 + 4]
	cmp	[rax + Diag.span.len], ecx
	jne	.fail5
	inc	rbx
	jmp	.fifteen
  .fifteen_done:

	; ---- check 6: the raw override never reaches rendered output -------
	lea	rdi, [case_text]
	mov	esi, C1_LEN
	call	lt_run
	xor	rdi, rdi
	call	lt_diag
	mov	rdi, rax
	lea	rsi, [renderbuf]
	mov	rdx, 4096
	call	diag_render_text
	; rax = bytes written; search them for E2 80 AE
	mov	r12, rax
	xor	rbx, rbx
  .scan:
	lea	rcx, [rbx + 3]
	cmp	rcx, r12
	jg	.scan_done
	cmp	byte [renderbuf + rbx], 0xE2
	jne	.scan_next
	cmp	byte [renderbuf + rbx + 1], 0x80
	jne	.scan_next
	cmp	byte [renderbuf + rbx + 2], 0xAE
	je	.fail6
  .scan_next:
	inc	rbx
	jmp	.scan
  .scan_done:

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
  .fail6:
	mov	eax, 231
	mov	edi, 16
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
	db	0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x78, 0x20, 0x3D, 0x20, 0x31, 0x0A
	db	0x2F, 0x2F, 0x20, 0x68, 0x61, 0x72, 0x6D, 0x6C, 0x65, 0x73, 0x73, 0x20
	db	0xE2, 0x80, 0xAE, 0x20, 0x65, 0x76, 0x69, 0x6C, 0x0A, 0x66, 0x69, 0x72
	db	0x6D, 0x61, 0x20, 0x73, 0x20, 0x3D, 0x20, 0x22, 0x68, 0x69, 0x20, 0xE2
	db	0x80, 0xAE, 0x20, 0x74, 0x68, 0x65, 0x72, 0x65, 0x22, 0x0A, 0x66, 0x69
	db	0x72, 0x6D, 0x61, 0x20, 0x61, 0xE2, 0x80, 0x8B, 0x62, 0x20, 0x3D, 0x20
	db	0x31, 0x0A, 0x2F, 0x2F, 0x20, 0x6E, 0x6F, 0x74, 0x65, 0x20, 0xD8, 0x9C
	db	0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x61, 0x20, 0x3D, 0x20, 0x31
	db	0x0A, 0x2F, 0x2F, 0x20, 0x63, 0x20, 0xD8, 0x9C, 0x0A, 0x66, 0x69, 0x72
	db	0x6D, 0x61, 0x20, 0x73, 0x20, 0x3D, 0x20, 0x22, 0x78, 0xE2, 0x80, 0x8B
	db	0x22, 0x0A, 0x2F, 0x2F, 0x20, 0x63, 0x20, 0xE2, 0x80, 0x8C, 0x0A, 0x66
	db	0x69, 0x72, 0x6D, 0x61, 0x20, 0x73, 0x20, 0x3D, 0x20, 0x22, 0x78, 0xE2
	db	0x80, 0x8D, 0x22, 0x0A, 0x2F, 0x2F, 0x20, 0x63, 0x20, 0xE2, 0x80, 0x8E
	db	0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x73, 0x20, 0x3D, 0x20, 0x22
	db	0x78, 0xE2, 0x80, 0x8F, 0x22, 0x0A, 0x2F, 0x2F, 0x20, 0x63, 0x20, 0xE2
	db	0x80, 0xAA, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x73, 0x20, 0x3D
	db	0x20, 0x22, 0x78, 0xE2, 0x80, 0xAB, 0x22, 0x0A, 0x2F, 0x2F, 0x20, 0x63
	db	0x20, 0xE2, 0x80, 0xAC, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x73
	db	0x20, 0x3D, 0x20, 0x22, 0x78, 0xE2, 0x80, 0xAD, 0x22, 0x0A, 0x2F, 0x2F
	db	0x20, 0x63, 0x20, 0xE2, 0x80, 0xAE, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61
	db	0x20, 0x73, 0x20, 0x3D, 0x20, 0x22, 0x78, 0xE2, 0x81, 0xA6, 0x22, 0x0A
	db	0x2F, 0x2F, 0x20, 0x63, 0x20, 0xE2, 0x81, 0xA7, 0x0A, 0x66, 0x69, 0x72
	db	0x6D, 0x61, 0x20, 0x73, 0x20, 0x3D, 0x20, 0x22, 0x78, 0xE2, 0x81, 0xA8
	db	0x22, 0x0A, 0x2F, 0x2F, 0x20, 0x63, 0x20, 0xE2, 0x81, 0xA9, 0x0A, 0x2F
	db	0x2F, 0x20, 0xD8, 0x9B, 0xD8, 0x9D, 0xE2, 0x80, 0x8A, 0xE2, 0x80, 0x90
	db	0xE2, 0x80, 0xA9, 0xE2, 0x80, 0xAF, 0xE2, 0x81, 0xA5, 0xE2, 0x81, 0xAA
	db	0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x61, 0x20, 0x3D, 0x20, 0x31
	db	0x0A

  case_tab:
	dd	0, 33, 1, 103, 24, 3	; U+202E inside a // comment
	dd	33, 25, 1, 103, 14, 3	; U+202E inside a string literal
	dd	58, 16, 1, 103, 7, 3	; U+200B inside an identifier
	dd	74, 11, 1, 103, 8, 2	; U+061C (2-byte) inside a comment
	dd	85, 202, 15, 103, 17, 2	; all fifteen, comments and literals
	dd	287, 38, 0, 0, 0, 0	; the eight codepoints just outside the ranges
  case_tab_end:
  CASE_COUNT = (case_tab_end - case_tab) / 24
  ; A table that parsed to zero rows would make the loop above
  ; pass by never running. fasmg's native assemble-time `assert`
  ; (NOT macros/assert.inc's runtime `rassert`) refuses that.
  assert CASE_COUNT = 6

  ALL15_OFF = 85
  ALL15_LEN = 202
  C1_LEN = 33

  ; (span start, span length) for each of case 5's fifteen occurrences,
  ; measured in case_text coordinates.
  all15_tab:
	dd	17, 2
	dd	32, 3
	dd	42, 3
	dd	58, 3
	dd	68, 3
	dd	84, 3
	dd	94, 3
	dd	110, 3
	dd	120, 3
	dd	136, 3
	dd	146, 3
	dd	162, 3
	dd	172, 3
	dd	188, 3
	dd	198, 3

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  lt_arena	rb sizeof.Arena
  lt_iarena	rb sizeof.Arena
  lt_intern	rb sizeof.Interner
  lt_toks	rb sizeof.Vec
  lt_diags	rb sizeof.Vec
  lt_lx		rb sizeof.Lexer
  renderbuf	rb 4096
