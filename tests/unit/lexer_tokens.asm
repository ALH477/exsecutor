; tests/unit/lexer_tokens.asm
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
; The token stream itself: kinds, `aux` payloads and spans, byte for byte.
;
; Sample A IS `examples/saluta.exsc`, read from the tree at assembly time with
; fasmg's `file` directive rather than copied in -- so this fixture cannot
; drift from the canonical program it claims to lex. Its expectations are the
; token KINDS, the keyword and punctuation ids, the token lengths and the
; count; the byte OFFSETS are left unchecked (`ANY`) precisely so that
; rewording the file's prose comments does not break a fixture that is about
; tokenization. The one length that is pinned is the string literal's 103
; bytes: §8.4 settles that "a string literal may contain raw newlines ...
; There is no separate multiline form and no indentation stripping", and that
; literal is four lines with two blank ones. A lexer that stopped a literal at
; the first newline, or stripped leading whitespace, would produce a different
; number there and nowhere else.
;
; Sample B pins the same rule in isolation, with exact offsets. Sample C is
; every operator, sigil and delimiter token.inc knows, in one line, with each
; PUN_* id and each span checked -- which is what makes the maximal-munch
; claims real: `..` before `.`, `->` before `-`, `+%` and `+|` before `+`.
; Sample D is the one place the escape rule bites: `"a\"b"` is SIX bytes and
; one token, because `\` escapes the next byte for delimiting purposes. This
; fixture asserts nothing about what `\"` MEANS -- §8.4 does not define an
; escape alphabet and lexer/lex.inc deliberately does not invent one.
;
; The eleven error rows are the §8.4 codes this module can raise from the
; token layer, each pinned to its span. Two of them are the `[OPEN]` numeric
; grammar: `0x1G` and `1_000` are `EXS-E0210` rather than a silent
; tokenization -- `1_000` because §8.4 says separators are "not settled and
; deliberately not invented here" and splitting it into two tokens would BE
; a settlement; `0x1G` because D6 (docs/design/wire-codec.md) settled the
; hex PREFIX (`0x[0-9a-fA-F]+`) but left everything past a hex digit run
; that meets an identifier character exactly where a decimal run already
; was. `0x10` itself is no longer an error -- D6 retired that, and
; tests/unit/lex_hex_literal.asm is where the hex grammar itself (accepted
; and rejected forms) is pinned; this row exists only to confirm the
; hex path still defers to the same "runs into an identifier character"
; rule once past its own digits. The last two rows are the other side of
; that: a plain digit run is fine, and `0..n` -- §8.5's own iteration syntax
; -- must lex as three tokens and not as a malformed literal.
;
; Checks 5 and 6 exist because mutation testing said they had to. Making
; `Tok.aux` always 0 for identifiers, and zeroing every span's `file_id`, were
; both mutations of the lexer that NO fixture caught -- the token tables above
; leave IDENT `aux` as ANY, and nothing looked at `file_id` at all. Recorded
; rather than quietly added: an expectation table can be complete about the
; fields it names and blind about the ones it does not.
;
; Exit 0 = all checks passed; 11-14 = the error-case loop (code, count, span
; start, span length); 15 = interning identity, 16 = span file_id; 20+N =
; sample N's token stream disagreed (A=1, B=2, C=3, D=4); 30 = a sample
; produced an unexpected diagnostic.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/lexer/lexer.inc'

LT_ARENA = 1 shl 20
ANY = 0xFFFFFFFF

segment readable executable
  start:
	call	lt_setup

	; ---- samples A..D: exact token streams -----------------------------
	lea	rdi, [sample_a]
	mov	esi, SAMPLE_A_LEN
	lea	rdx, [tok_a]
	mov	ecx, TOK_A_N
	mov	r8d, 21
	call	lt_check

	lea	rdi, [sample_b]
	mov	esi, SAMPLE_B_LEN
	lea	rdx, [tok_b]
	mov	ecx, TOK_B_N
	mov	r8d, 22
	call	lt_check

	lea	rdi, [sample_c]
	mov	esi, SAMPLE_C_LEN
	lea	rdx, [tok_c]
	mov	ecx, TOK_C_N
	mov	r8d, 23
	call	lt_check

	lea	rdi, [sample_d]
	mov	esi, SAMPLE_D_LEN
	lea	rdx, [tok_d]
	mov	ecx, TOK_D_N
	mov	r8d, 24
	call	lt_check

	; ---- check 5: `aux` really is an intern id -------------------------
	; The four token tables above leave IDENT `aux` as ANY, because an
	; intern id is an allocation order rather than a fact about the source.
	; What IS a fact -- and what §8.2's "comparison is byte equality after
	; NFC" turns into an obligation -- is that the same name interns to the
	; same id and a different name does not. A lexer that never interned at
	; all would leave every `aux` at 0 and pass every table above.
	lea	rdi, [sample_e]
	mov	esi, SAMPLE_E_LEN
	call	lt_run
	test	eax, eax
	jnz	.fail5
	call	lt_ntok
	cmp	rax, 9
	jne	.fail5
	mov	rdi, 1			; `alpha`
	call	lt_tok
	mov	r12d, [rax + Tok.aux]
	test	r12d, r12d
	jz	.fail5			; 0 is rt/intern.inc's "not interned"
	mov	rdi, 5			; `beta`
	call	lt_tok
	mov	r13d, [rax + Tok.aux]
	test	r13d, r13d
	jz	.fail5
	cmp	r12d, r13d
	je	.fail5			; two different names, one id
	mov	rdi, 7			; `alpha` again
	call	lt_tok
	cmp	[rax + Tok.aux], r12d
	jne	.fail5			; the same name, two ids

	; ---- check 6: every span names the file -----------------------------
	; rt/span.inc: `file_id` is an INTERN ID of the path, not a descriptor
	; and not a pointer. Interning the path again must give the same id, and
	; every token of this run must carry it -- otherwise a diagnostic in a
	; multi-file compilation would point into the wrong file, which is
	; precisely the §8.3 property that is "not retrofittable".
	lea	rdi, [lt_intern]
	lea	rsi, [lt_path]
	mov	edx, LT_PATH_LEN
	call	intern_id
	mov	r12d, eax
	test	r12d, r12d
	jz	.fail6
	xor	rbx, rbx
  .fid:
	call	lt_ntok
	cmp	rbx, rax
	jge	.fid_done
	mov	rdi, rbx
	call	lt_tok
	cmp	[rax + Tok.span.file_id], r12d
	jne	.fail6
	inc	rbx
	jmp	.fid
  .fid_done:

	; ---- the error cases -----------------------------------------------
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
  .fail5:
	mov	eax, 231
	mov	edi, 15
	syscall
  .fail6:
	mov	eax, 231
	mov	edi, 16
	syscall

; lt_check(rdi = source, rsi = length, rdx = expected table, rcx = expected
;          token count, r8d = the exit code to use if it disagrees)
; Exits the process on any mismatch; returns normally otherwise. Exits 30 if
; the sample produced a diagnostic at all -- these four samples are all
; supposed to be clean §8.1/§8.2 source.
  lt_check:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	push	rbp
	mov	r13, rdx
	mov	r14, rcx
	mov	rbp, r8
	sub	rsp, 8			; six pushes leave rsp 8 (mod 16); SysV
					; wants 0 immediately before a `call`
	call	lt_run
	test	eax, eax
	jnz	.dirty
	call	lt_ntok
	cmp	rax, r14
	jne	.bad
	xor	rbx, rbx
  .one:
	cmp	rbx, r14
	jge	.done
	mov	rdi, rbx
	call	lt_tok
	mov	r12, rax
	mov	rax, rbx
	shl	rax, 4			; 4 dd per expected row
	lea	r15, [r13 + rax]
	mov	ecx, [r15]
	cmp	[r12 + Tok.kind], ecx
	jne	.bad
	mov	ecx, [r15 + 4]
	cmp	ecx, ANY
	je	.skip_aux
	cmp	[r12 + Tok.aux], ecx
	jne	.bad
  .skip_aux:
	mov	ecx, [r15 + 8]
	cmp	ecx, ANY
	je	.skip_start
	cmp	[r12 + Tok.span.start], ecx
	jne	.bad
  .skip_start:
	mov	ecx, [r15 + 12]
	cmp	ecx, ANY
	je	.skip_len
	cmp	[r12 + Tok.span.len], ecx
	jne	.bad
  .skip_len:
	inc	rbx
	jmp	.one
  .done:
	add	rsp, 8
	pop	rbp
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	ret
  .bad:
	mov	edi, ebp
	mov	eax, 231
	syscall
  .dirty:
	mov	eax, 231
	mov	edi, 30
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

  ; THE canonical program, read from the tree rather than copied -- see this
  ; file's header.
  sample_a:
	file '../../examples/saluta.exsc'
  sample_a_end:
  SAMPLE_A_LEN = sample_a_end - sample_a
tok_a:
	dd	TOK_KEYWORD, KW_PUBLICA, ANY, 7	; publica
	dd	TOK_KEYWORD, KW_FUNCTIO, ANY, 7	; functio
	dd	TOK_IDENT, ANY, ANY, 6	; saluta
	dd	TOK_PUNCT, PUN_LPAREN, ANY, 1	; (
	dd	TOK_PUNCT, PUN_RPAREN, ANY, 1	; )
	dd	TOK_PUNCT, PUN_ARROW, ANY, 2	; ->
	dd	TOK_IDENT, ANY, ANY, 6	; textus
	dd	TOK_PUNCT, PUN_LBRACE, ANY, 1	; {
	dd	TOK_KEYWORD, KW_REDDE, ANY, 5	; redde
	dd	TOK_STRING, 0, ANY, 103	; the four-line literal
	dd	TOK_PUNCT, PUN_SEMI, ANY, 1	; ;  -- §8.6 made this mandatory
	dd	TOK_PUNCT, PUN_RBRACE, ANY, 1	; }
	dd	TOK_EOF, 0, ANY, 0	; end of input
  tok_a_end:
  TOK_A_N = (tok_a_end - tok_a) / 16
  ; 12 before §8.6 landed. The count is pinned so that a change to the
  ; canonical program is a deliberate edit here rather than a silent drift --
  ; which is exactly what happened: §8.6 made `;` mandatory on every simple
  ; statement, saluta.exsc gained one, and this assert caught it.
  assert TOK_A_N = 13

  sample_b:
	db	0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x73, 0x20, 0x3D, 0x20, 0x22, 0x6F
	db	0x6E, 0x65, 0x0A, 0x0A, 0x74, 0x77, 0x6F, 0x22, 0x0A
  sample_b_end:
  SAMPLE_B_LEN = sample_b_end - sample_b
tok_b:
	dd	TOK_KEYWORD, KW_FIRMA, 0, 5	; firma
	dd	TOK_IDENT, ANY, 6, 1	; s
	dd	TOK_PUNCT, PUN_EQ, 8, 1	; =
	dd	TOK_STRING, 0, 10, 10	; "one\n\ntwo"
	dd	TOK_EOF, 0, 21, 0	; 
  tok_b_end:
  TOK_B_N = (tok_b_end - tok_b) / 16
  assert TOK_B_N = 5

  sample_c:
	db	0x61, 0x20, 0x2E, 0x2E, 0x20, 0x62, 0x20, 0x2D, 0x3E, 0x20, 0x63, 0x20
	db	0x2B, 0x25, 0x20, 0x64, 0x20, 0x2B, 0x7C, 0x20, 0x65, 0x20, 0x2B, 0x20
	db	0x66, 0x20, 0x26, 0x20, 0x2A, 0x67, 0x20, 0x3C, 0x54, 0x3E, 0x20, 0x68
	db	0x3F, 0x20, 0x69, 0x2C, 0x20, 0x6A, 0x3B, 0x20, 0x6B, 0x2E, 0x6C, 0x20
	db	0x3D, 0x20, 0x6D, 0x20, 0x2D, 0x20, 0x6E, 0x20, 0x5B, 0x6F, 0x5D, 0x0A
  sample_c_end:
  SAMPLE_C_LEN = sample_c_end - sample_c
tok_c:
	dd	TOK_IDENT, ANY, 0, 1	; a
	dd	TOK_PUNCT, PUN_DOTDOT, 2, 2	; ..
	dd	TOK_IDENT, ANY, 5, 1	; b
	dd	TOK_PUNCT, PUN_ARROW, 7, 2	; ->
	dd	TOK_IDENT, ANY, 10, 1	; c
	dd	TOK_PUNCT, PUN_PLUSPCT, 12, 2	; +%
	dd	TOK_IDENT, ANY, 15, 1	; d
	dd	TOK_PUNCT, PUN_PLUSBAR, 17, 2	; +|
	dd	TOK_IDENT, ANY, 20, 1	; e
	dd	TOK_PUNCT, PUN_PLUS, 22, 1	; +
	dd	TOK_IDENT, ANY, 24, 1	; f
	dd	TOK_PUNCT, PUN_AMP, 26, 1	; &
	dd	TOK_PUNCT, PUN_STAR, 28, 1	; *
	dd	TOK_IDENT, ANY, 29, 1	; g
	dd	TOK_PUNCT, PUN_LT, 31, 1	; <
	dd	TOK_IDENT, ANY, 32, 1	; T
	dd	TOK_PUNCT, PUN_GT, 33, 1	; >
	dd	TOK_IDENT, ANY, 35, 1	; h
	dd	TOK_PUNCT, PUN_QUESTION, 36, 1	; ?
	dd	TOK_IDENT, ANY, 38, 1	; i
	dd	TOK_PUNCT, PUN_COMMA, 39, 1	; ,
	dd	TOK_IDENT, ANY, 41, 1	; j
	dd	TOK_PUNCT, PUN_SEMI, 42, 1	; ;
	dd	TOK_IDENT, ANY, 44, 1	; k
	dd	TOK_PUNCT, PUN_DOT, 45, 1	; .
	dd	TOK_IDENT, ANY, 46, 1	; l
	dd	TOK_PUNCT, PUN_EQ, 48, 1	; =
	dd	TOK_IDENT, ANY, 50, 1	; m
	dd	TOK_PUNCT, PUN_MINUS, 52, 1	; -
	dd	TOK_IDENT, ANY, 54, 1	; n
	dd	TOK_PUNCT, PUN_LBRACKET, 56, 1	; [
	dd	TOK_IDENT, ANY, 57, 1	; o
	dd	TOK_PUNCT, PUN_RBRACKET, 58, 1	; ]
	dd	TOK_EOF, 0, 60, 0	; end of input
  tok_c_end:
  TOK_C_N = (tok_c_end - tok_c) / 16
  assert TOK_C_N = 34

  sample_d:
	db	0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x73, 0x20, 0x3D, 0x20, 0x22, 0x61
	db	0x5C, 0x22, 0x62, 0x22, 0x20, 0x3D, 0x20, 0x31, 0x0A
  sample_d_end:
  SAMPLE_D_LEN = sample_d_end - sample_d
tok_d:
	dd	TOK_KEYWORD, KW_FIRMA, 0, 5	; firma
	dd	TOK_IDENT, ANY, 6, 1	; s
	dd	TOK_PUNCT, PUN_EQ, 8, 1	; =
	dd	TOK_STRING, 0, 10, 6	; "a\\"b" -- six bytes
	dd	TOK_PUNCT, PUN_EQ, 17, 1	; =
	dd	TOK_NUMBER, 0, 19, 1	; 1
	dd	TOK_EOF, 0, 21, 0	; 
  tok_d_end:
  TOK_D_N = (tok_d_end - tok_d) / 16
  assert TOK_D_N = 7

  sample_e:
	db	0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x61, 0x6C, 0x70, 0x68, 0x61, 0x20
	db	0x3D, 0x20, 0x31, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x62, 0x65
	db	0x74, 0x61, 0x20, 0x3D, 0x20, 0x61, 0x6C, 0x70, 0x68, 0x61, 0x0A
  sample_e_end:
  SAMPLE_E_LEN = sample_e_end - sample_e

  case_text:
	db	0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x73, 0x20, 0x3D, 0x20, 0x22, 0x6E
	db	0x6F, 0x20, 0x65, 0x6E, 0x64, 0x20, 0x68, 0x65, 0x72, 0x65, 0x0A, 0x66
	db	0x69, 0x72, 0x6D, 0x61, 0x20, 0x73, 0x20, 0x3D, 0x20, 0x22, 0x61, 0x62
	db	0x63, 0x5C, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x6E, 0x20, 0x3D, 0x20
	db	0x30, 0x78, 0x31, 0x47, 0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x6E
	db	0x20, 0x3D, 0x20, 0x31, 0x5F, 0x30, 0x30, 0x30, 0x0A, 0x66, 0x69, 0x72
	db	0x6D, 0x61, 0x20, 0x71, 0x20, 0x3D, 0x20, 0x61, 0x20, 0x2F, 0x20, 0x62
	db	0x0A, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x71, 0x20, 0x3D, 0x20, 0x61
	db	0x20, 0x25, 0x20, 0x62, 0x0A, 0x40, 0x20, 0x78, 0x0A, 0x40, 0x66, 0x69
	db	0x72, 0x6D, 0x61, 0x20, 0xCC, 0x81, 0x20, 0x3D, 0x20, 0x31, 0x0A, 0x66
	db	0x69, 0x72, 0x6D, 0x61, 0x20, 0x6E, 0x20, 0x3D, 0x20, 0x31, 0x32, 0x33
	db	0x34, 0x0A, 0x70, 0x65, 0x72, 0x20, 0x69, 0x20, 0x69, 0x6E, 0x20, 0x30
	db	0x2E, 0x2E, 0x6E, 0x20, 0x7B, 0x7D, 0x0A

  case_tab:
	dd	0, 23, 1, 202, 10, 13	; unterminated string literal
	dd	23, 15, 1, 202, 10, 5	; backslash at end of input
	dd	38, 15, 1, 210, 10, 4	; a hex literal that runs into a non-hex
					; identifier char (D6, wire-codec.md);
					; formerly the "0x10" row -- 0x10 is now
					; a valid TOK_NUMBER, see lex_hex_literal.asm
	dd	53, 16, 1, 210, 10, 5	; 1_000 needs the grammar §8.4 does not have
	dd	69, 16, 1, 201, 12, 1	; a bare slash is not attested
	dd	85, 16, 1, 201, 12, 1	; percent outside +%
	dd	101, 4, 1, 201, 0, 1	; @ with no name after it
	dd	105, 1, 1, 201, 0, 1	; @ at end of input
	dd	106, 13, 1, 201, 6, 2	; a lone combining mark cannot start a token
	dd	119, 15, 0, 0, 0, 0	; a plain digit run is fine
	dd	134, 17, 0, 0, 0, 0	; 0..n lexes as three tokens
  case_tab_end:
  CASE_COUNT = (case_tab_end - case_tab) / 24
  ; A table that parsed to zero rows would make the loop above
  ; pass by never running. fasmg's native assemble-time `assert`
  ; (NOT macros/assert.inc's runtime `rassert`) refuses that.
  assert CASE_COUNT = 11

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  lt_arena	rb sizeof.Arena
  lt_iarena	rb sizeof.Arena
  lt_intern	rb sizeof.Interner
  lt_toks	rb sizeof.Vec
  lt_diags	rb sizeof.Vec
  lt_lx		rb sizeof.Lexer
