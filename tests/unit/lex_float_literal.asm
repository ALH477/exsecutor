; tests/unit/lex_float_literal.asm
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
; FLOAT LITERALS (spec §8.4 as amended by the float wave), the LEXER'S half:
;
;   FloatLit ::= DIGITS '.' DIGITS [ 'e' Exp ]
;              | DIGITS 'e' Exp
;   Exp      ::= ('+'|'-')? DIGITS{1,3}
;
; The `e` is LOWERCASE ONLY -- the hex prefix's one-spelling discipline
; (D6) applied to the exponent -- and a fraction needs at least one digit
; after the '.', so `1.` is `1` then `.` and there is no leading-dot form.
; A digit run followed by `.` takes the fraction path ONLY over a ONE-BYTE
; PEEK that confirms a digit, which is what makes `0..n`, `1..5` and
; `0.5..2.5` three-token range sources and not one giant literal.
;
; ACCEPTED -- exact token streams, clean (no diagnostics), the float rows
; carrying aux = 1 (TOK_NUMBER's float class; integers stay aux = 0):
;   `1.5;`      the fraction form.
;   `1e-3;`     the exponent-only form, sign present.
;   `1.5e-3;`   both, with `0.5..2.5`'s cousin shape in acc_c.
;   `0.5..2.5;` THE RANGE PIN: float, `..`, float -- the peek leaves the
;               first `.` of `..` unconsumed and PUN_DOTDOT still lexes.
;   `1..5;`     integer, `..`, integer: `1.` is `1` then `.` because the
;               byte after the `.` is not a digit.
;   `0..n;`     integer, `..`, IDENT: the bound shape the range grammar
;               actually writes (§5.1).
;   `1.;`       `1` then PUN_DOT then `;` -- a trailing-dot literal is NOT
;               a float, and the `.` survives as field-access punctuation.
;   `x.y;`      IDENT `.` IDENT -- field access is untouched by the number
;               path.
;   `0x1e5;`    hex stays hex: `e` after a hex run is a hex DIGIT, and the
;               hex path exits before any float machinery exists.
;   `a / b;`    the lone `/` is PUN_SLASH (this wave's other half, §5.4's
;               float-only quotient) -- PUNCT, not an error.
;   `1.5 // 2.5` a `/` still OPENS A COMMENT when a second `/` follows; the
;    + newline + `3.5;` tokens after the newline prove the comment ate the
;               rest of the line, `2.5` never lexed, and `3.5` did.
;
; REJECTED, all EXS-E0210, the whole literal consumed and reported once
; (lex.inc's `.num_bad`, the same path a digit run into `_` always took):
;   `1e`      the exponent marker with no digits.
;   `1e+`     the sign with no digits after it.
;   `1e1234`  four exponent digits; the grammar caps at three.
;   `1.5f32`  the fraction running into an identifier character -- there are
;             NO SUFFIXES (§8.4), so this is not `1.5` followed by a type.
;   `1E5`     capital `E`: never the exponent path (XID_Continue, so it is
;             the identifier-character rule that reports it).
;   `1e3f32`  a well-formed exponent that then runs into an identifier
;             character; the whole literal is refused, not truncated.
;
; Exit 0 = all checks passed. Accepted-sample failures are 21-31 (A-K,
; token stream disagreed) and 30 (a clean sample produced an unexpected
; diagnostic -- acc_k reuses it, being the 11th). Rejected-sample failures
; are 11 (wrong first code), 12 (wrong diagnostic count), 13 (wrong span
; start), 14 (wrong span length).
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

	; ---- accepted: exact token streams, clean (no diagnostics) --------
	lea	rdi, [acc_a]
	mov	esi, ACC_A_LEN
	lea	rdx, [tok_a]
	mov	ecx, TOK_A_N
	mov	r8d, 21
	call	lt_check

	lea	rdi, [acc_b]
	mov	esi, ACC_B_LEN
	lea	rdx, [tok_b]
	mov	ecx, TOK_B_N
	mov	r8d, 22
	call	lt_check

	lea	rdi, [acc_c]
	mov	esi, ACC_C_LEN
	lea	rdx, [tok_c]
	mov	ecx, TOK_C_N
	mov	r8d, 23
	call	lt_check

	lea	rdi, [acc_d]
	mov	esi, ACC_D_LEN
	lea	rdx, [tok_d]
	mov	ecx, TOK_D_N
	mov	r8d, 24
	call	lt_check

	lea	rdi, [acc_e]
	mov	esi, ACC_E_LEN
	lea	rdx, [tok_e]
	mov	ecx, TOK_E_N
	mov	r8d, 25
	call	lt_check

	lea	rdi, [acc_f]
	mov	esi, ACC_F_LEN
	lea	rdx, [tok_f]
	mov	ecx, TOK_F_N
	mov	r8d, 26
	call	lt_check

	lea	rdi, [acc_g]
	mov	esi, ACC_G_LEN
	lea	rdx, [tok_g]
	mov	ecx, TOK_G_N
	mov	r8d, 27
	call	lt_check

	lea	rdi, [acc_h]
	mov	esi, ACC_H_LEN
	lea	rdx, [tok_h]
	mov	ecx, TOK_H_N
	mov	r8d, 28
	call	lt_check

	lea	rdi, [acc_i]
	mov	esi, ACC_I_LEN
	lea	rdx, [tok_i]
	mov	ecx, TOK_I_N
	mov	r8d, 29
	call	lt_check

	lea	rdi, [acc_k]
	mov	esi, ACC_K_LEN
	lea	rdx, [tok_k]
	mov	ecx, TOK_K_N
	mov	r8d, 31
	call	lt_check

	; ---- rejected: EXS-E0210, one diagnostic, exact span ---------------
	; `rcx` is caller-saved (docs/asm-conventions.md 1.1) and every helper
	; below is a real `call`, so the row-index-to-byte-offset multiply is
	; RECOMPUTED after each one rather than trusted to survive it --
	; lex_hex_literal.asm's loop hit exactly this and does the same.
	xor	rbx, rbx
  .case:
	cmp	rbx, CASE_COUNT
	jge	.case_done
	lea	rcx, [rbx + rbx*3]	; 4 dd per row -- *16 needs a legal scale
	lea	rdi, [case_ptrs]
	mov	rax, [rdi + rbx*8]	; case_ptrs[rbx] -- this row's source
	mov	rdi, rax
	mov	esi, [case_tab + rcx*4]	; row.len
	call	lt_run
	lea	rcx, [rbx + rbx*3]
	mov	r12d, [case_tab + rcx*4 + 4]	; row.code
	cmp	eax, r12d
	jne	.fail1
	call	lt_ndiag
	lea	rcx, [rbx + rbx*3]
	mov	r12d, [case_tab + rcx*4 + 8]	; row.ndiag (always 1 here)
	cmp	eax, r12d
	jne	.fail2
	xor	rdi, rdi
	call	lt_diag
	lea	rcx, [rbx + rbx*3]
	mov	r12d, [case_tab + rcx*4 + 12]	; row.span_len
	cmp	dword [rax + Diag.span.start], 0
	jne	.fail3			; every rejected sample's bad run starts at 0
	cmp	[rax + Diag.span.len], r12d
	jne	.fail4
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
; (macros/proc.inc's header), and this fixture wants no part of that
; namespace. Every helper pushes an ODD number of registers so that `rsp` is
; 16-aligned at the `call`s inside it -- at a helper's first instruction
; `rsp` is 8 (mod 16), because `call` pushed the return address.
;
; NOTE, found by running (recorded in lex_hex_literal.asm and
; lexer_tokens.asm too): the sample path label here is `lt_path`, NOT
; `pathname` -- `rt/sys.inc` has `proc sys_openat, dirfd, pathname, ...` and
; every proc argument name is a global `= rbp - K`, so a data label named
; `pathname` silently resolves to a register expression instead.

; lt_setup -- two arenas and an interner. The interner's arena is separate and
; is NEVER reset: rt/intern.inc's header makes an interned id valid only for
; as long as the arena behind it lives, and lt_run rewinds the other one per
; case.
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

; lt_run(rdi = source bytes, rsi = byte length) -> CF/eax exactly as
; `lex_run` left them (docs/asm-conventions.md's CF/eax protocol; lex_run's
; own header: CF clear + eax = first diagnostic code, 0 if none, once the
; file is admissible §8.1 source -- every case here is plain ASCII, so every
; case in this fixture takes that path). Fresh vectors and a rewound scratch
; arena every call, so cases cannot leak into one another.
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

; lt_check(rdi = source, rsi = length, rdx = expected table, rcx = expected
;          token count, r8d = the exit code to use if it disagrees)
; Exits the process on any mismatch; returns normally otherwise. Exits 30 if
; the sample produced a diagnostic at all -- every sample here is supposed
; to be clean.
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

segment readable
  lt_path:	db 'fixture.exsc'
  lt_path_end:
  LT_PATH_LEN = lt_path_end - lt_path

  ; ---- accepted samples -------------------------------------------------
  acc_a:
	db	"1.5;", 10
  acc_a_end:
  ACC_A_LEN = acc_a_end - acc_a
tok_a:
	dd	TOK_NUMBER, 1, 0, 3	; 1.5 -- aux 1: the FLOAT class
	dd	TOK_PUNCT, PUN_SEMI, 3, 1	; ;
	dd	TOK_EOF, 0, 5, 0	; end of input
  tok_a_end:
  TOK_A_N = (tok_a_end - tok_a) / 16
  assert TOK_A_N = 3

  acc_b:
	db	"1e-3;", 10
  acc_b_end:
  ACC_B_LEN = acc_b_end - acc_b
tok_b:
	dd	TOK_NUMBER, 1, 0, 4	; 1e-3 -- the exponent-only form
	dd	TOK_PUNCT, PUN_SEMI, 4, 1	; ;
	dd	TOK_EOF, 0, 6, 0
  tok_b_end:
  TOK_B_N = (tok_b_end - tok_b) / 16
  assert TOK_B_N = 3

  acc_c:
	db	"0.5..2.5;", 10
  acc_c_end:
  ACC_C_LEN = acc_c_end - acc_c
tok_c:
	dd	TOK_NUMBER, 1, 0, 3	; 0.5
	dd	TOK_PUNCT, PUN_DOTDOT, 3, 2	; .. -- the peek left it whole
	dd	TOK_NUMBER, 1, 5, 3	; 2.5
	dd	TOK_PUNCT, PUN_SEMI, 8, 1	; ;
	dd	TOK_EOF, 0, 10, 0
  tok_c_end:
  TOK_C_N = (tok_c_end - tok_c) / 16
  assert TOK_C_N = 5

  acc_d:
	db	"1..5;", 10
  acc_d_end:
  ACC_D_LEN = acc_d_end - acc_d
tok_d:
	dd	TOK_NUMBER, 0, 0, 1	; 1 -- `1.` does not join; aux 0: still INT
	dd	TOK_PUNCT, PUN_DOTDOT, 1, 2	; ..
	dd	TOK_NUMBER, 0, 3, 1	; 5
	dd	TOK_PUNCT, PUN_SEMI, 4, 1	; ;
	dd	TOK_EOF, 0, 6, 0
  tok_d_end:
  TOK_D_N = (tok_d_end - tok_d) / 16
  assert TOK_D_N = 5

  acc_e:
	db	"0..n;", 10
  acc_e_end:
  ACC_E_LEN = acc_e_end - acc_e
tok_e:
	dd	TOK_NUMBER, 0, 0, 1	; 0
	dd	TOK_PUNCT, PUN_DOTDOT, 1, 2	; ..
	dd	TOK_IDENT, ANY, 3, 1	; n
	dd	TOK_PUNCT, PUN_SEMI, 4, 1	; ;
	dd	TOK_EOF, 0, 6, 0
  tok_e_end:
  TOK_E_N = (tok_e_end - tok_e) / 16
  assert TOK_E_N = 5

  acc_f:
	db	"1.;", 10
  acc_f_end:
  ACC_F_LEN = acc_f_end - acc_f
tok_f:
	dd	TOK_NUMBER, 0, 0, 1	; 1 -- a trailing dot does not make a float
	dd	TOK_PUNCT, PUN_DOT, 1, 1	; .
	dd	TOK_PUNCT, PUN_SEMI, 2, 1	; ;
	dd	TOK_EOF, 0, 4, 0
  tok_f_end:
  TOK_F_N = (tok_f_end - tok_f) / 16
  assert TOK_F_N = 4

  acc_g:
	db	"x.y;", 10
  acc_g_end:
  ACC_G_LEN = acc_g_end - acc_g
tok_g:
	dd	TOK_IDENT, ANY, 0, 1	; x
	dd	TOK_PUNCT, PUN_DOT, 1, 1	; . -- field access is untouched
	dd	TOK_IDENT, ANY, 2, 1	; y
	dd	TOK_PUNCT, PUN_SEMI, 3, 1	; ;
	dd	TOK_EOF, 0, 5, 0
  tok_g_end:
  TOK_G_N = (tok_g_end - tok_g) / 16
  assert TOK_G_N = 5

  acc_h:
	db	"0x1e5;", 10
  acc_h_end:
  ACC_H_LEN = acc_h_end - acc_h
tok_h:
	dd	TOK_NUMBER, 0, 0, 5	; 0x1e5 -- 'e' is a hex DIGIT; aux 0
	dd	TOK_PUNCT, PUN_SEMI, 5, 1	; ;
	dd	TOK_EOF, 0, 7, 0
  tok_h_end:
  TOK_H_N = (tok_h_end - tok_h) / 16
  assert TOK_H_N = 3

  acc_i:
	db	"a / b;", 10
  acc_i_end:
  ACC_I_LEN = acc_i_end - acc_i
tok_i:
	dd	TOK_IDENT, ANY, 0, 1	; a
	dd	TOK_PUNCT, PUN_SLASH, 2, 1	; / -- the lone slash is a token
	dd	TOK_IDENT, ANY, 4, 1	; b
	dd	TOK_PUNCT, PUN_SEMI, 5, 1	; ;
	dd	TOK_EOF, 0, 7, 0
  tok_i_end:
  TOK_I_N = (tok_i_end - tok_i) / 16
  assert TOK_I_N = 5

  acc_k:
	db	"1.5 // 2.5", 10
	db	"3.5;", 10
  acc_k_end:
  ACC_K_LEN = acc_k_end - acc_k
tok_k:
	dd	TOK_NUMBER, 1, 0, 3	; 1.5
	dd	TOK_NUMBER, 1, 11, 3	; 3.5 -- `// 2.5` was comment, never lexed
	dd	TOK_PUNCT, PUN_SEMI, 14, 1	; ;
	dd	TOK_EOF, 0, 16, 0
  tok_k_end:
  TOK_K_N = (tok_k_end - tok_k) / 16
  assert TOK_K_N = 4

  ; ---- rejected samples: each its own buffer, bad run always at offset 0 -
  rej_a:
	db	"1e", 10			; exponent marker, no digits
  rej_a_end:
  REJ_A_LEN = rej_a_end - rej_a

  rej_b:
	db	"1e+", 10			; sign, no digits after it
  rej_b_end:
  REJ_B_LEN = rej_b_end - rej_b

  rej_c:
	db	"1e1234", 10			; four exponent digits
  rej_c_end:
  REJ_C_LEN = rej_c_end - rej_c

  rej_d:
	db	"1.5f32", 10			; fraction into an identifier char
  rej_d_end:
  REJ_D_LEN = rej_d_end - rej_d

  rej_e:
	db	"1E5", 10			; capital E -- never the exponent
  rej_e_end:
  REJ_E_LEN = rej_e_end - rej_e

  rej_f:
	db	"1e3f32", 10			; exponent into an identifier char
  rej_f_end:
  REJ_F_LEN = rej_f_end - rej_f

  case_ptrs:
	dq	rej_a, rej_b, rej_c, rej_d, rej_e, rej_f

  ; row: len, code, ndiag, span_len (span_start is always 0 -- checked
  ; directly in the loop above, not from this table)
  case_tab:
	dd	REJ_A_LEN, 210, 1, 2	; 1e
	dd	REJ_B_LEN, 210, 1, 3	; 1e+
	dd	REJ_C_LEN, 210, 1, 6	; 1e1234
	dd	REJ_D_LEN, 210, 1, 6	; 1.5f32
	dd	REJ_E_LEN, 210, 1, 3	; 1E5
	dd	REJ_F_LEN, 210, 1, 6	; 1e3f32
  case_tab_end:
  CASE_COUNT = (case_tab_end - case_tab) / 16
  ; A table that parsed to zero rows would make the loop above pass by never
  ; running. fasmg's native assemble-time `assert` (NOT macros/assert.inc's
  ; runtime `rassert`) refuses that.
  assert CASE_COUNT = 6

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  lt_arena	rb sizeof.Arena
  lt_iarena	rb sizeof.Arena
  lt_intern	rb sizeof.Interner
  lt_toks	rb sizeof.Vec
  lt_diags	rb sizeof.Vec
  lt_lx		rb sizeof.Lexer