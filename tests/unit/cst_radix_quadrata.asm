; tests/unit/cst_radix_quadrata.asm
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
; -----------------------------------------------------------------------------
; cst fixture: spec §8.6's `Unary` with §5.4's `radix_quadrata` -- the square
; root as a PREFIX RESERVED word, through the real lexer and `cst_parse`.
;
; WHAT THIS FIXTURE IS THE EVIDENCE FOR, and why the word is reserved and not
; contextual like every other operator word here. §8.4's argument for a
; contextual operator is that "operator position is never operand position".
; That is true of an INFIX word and false of a PREFIX one, which sits exactly
; where an operand may start. A first draft made this `radix` and leaned on a
; one-token peek; the peek could not tell `radix ge n` from `radix a`, because
; the contextual comparison words are identifier tokens too, and it broke
; `examples/streamdb/lector_streamdb.exsc`, which binds a variable named
; `radix`. Rows 3 and 4 are that file, and they are the regression rows.
;
;   row  source (inside `publica functio f() -> u8 {` / `}`)   diag   UNARY
;    1   x = radix_quadrata a;                                   0      1
;    2   x = radix_quadrata radix_quadrata a;                    0      2
;    3   firma radix = 2.0;                                      0      0
;    4   lector_streamdb.exsc's own shape:
;        `firma radix = g(a); si radix ge n { } pila[0] = radix;` 0      0
;    5   x = a radix_quadrata b;                                 ?      ?
;    6   x = a + radix_quadrata b;                               0      1
;
; ROWS 3 AND 4 ARE THE POINT. `radix` is an ordinary identifier and stays one:
; reserving `radix_quadrata` spends the thirty-first reserved word and takes
; no name the corpus was using. If a future change reaches for the short
; spelling, row 4 is the file it would break.
;
; ROW 5 IS THE POSITION CHECK. `a radix_quadrata b` must NOT parse as an infix
; operator.
;
; ROW 6 IS THE PRECEDENCE. `a + radix_quadrata b` is `a + (radix_quadrata b)`.
;
; EVERY ROW ALSO ROUND-TRIPS. `cst_text` on the root must give back the source
; byte for byte.
;
; THIS FIXTURE STOPS AT THE CST. The checker's `.u_sqrt` arm and the `fsqrt`
; lowering are pinned by tests/programs/radix_quadrata/, which runs on both
; backends.
;
; Run with any argument to print each row's dump to stdout -- how the expected
; blocks were confirmed.
;
; Exit 0 = every row held; 10+N = row N's counts or diagnostic failed;
; 30+N = row N did not round-trip; 51 = shape A not found; 52 = shape B;
; 99 = setup.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.4, §8.4, §8.6, §9.1, §13.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/cst/cst.inc'

CT_ARENA = 4 shl 20
CT_BUF   = 64 shl 10
CT_ROW   = 40		; dq src, len; dd ndiag, code, off, dlen, nmul, ncall

segment readable executable

  start:
	mov	rax, [rsp]
	mov	[ct_argc], rax
	call	ct_setup

	xor	r12, r12
  .row:
	cmp	r12, CT_NROWS
	jae	.shapes
	mov	rax, r12
	imul	rax, CT_ROW
	lea	r13, [ct_tab]
	add	r13, rax
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	call	ct_run

	; ---- the round trip, before anything else -----------------------------
	; A tree that lost a byte is not a tree this fixture can then count
	; nodes in and believe.
	lea	rdi, [r12 + 31]
	call	ct_text
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	call	ct_same
	test	eax, eax
	jz	.bad_text

	call	ct_dump
	; the diagnostic count, then the first one's code, offset and length
	call	ct_ndiag
	mov	r14, rax
	mov	ecx, [r13 + 16]
	cmp	r14, rcx
	jne	.bad
	test	r14, r14
	jz	.counts
	xor	edi, edi
	call	ct_diag
	mov	ecx, [r13 + 20]
	cmp	[rax + Diag.code_num], ecx
	jne	.bad
	mov	ecx, [r13 + 24]
	cmp	[rax + Diag.span.start], ecx
	jne	.bad
	mov	ecx, [r13 + 28]
	cmp	[rax + Diag.span.len], ecx
	jne	.bad
  .counts:
	lea	rdi, [kw_mul]
	mov	rsi, KW_MUL_LEN
	call	ct_count
	mov	ecx, [r13 + 32]
	cmp	rax, rcx
	jne	.bad
	lea	rdi, [kw_call]
	mov	rsi, KW_CALL_LEN
	call	ct_count
	mov	ecx, [r13 + 36]
	cmp	rax, rcx
	jne	.bad
	inc	r12
	jmp	.row
  .bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .bad_text:
	lea	rdi, [r12 + 31]
	call	sys_exit_group

	; ---- the two shapes ---------------------------------------------------
  .shapes:
	lea	rdi, [ct_s06]
	mov	rsi, ct_s06_LEN
	call	ct_run
	call	ct_dump
	lea	rdi, [exp_a]
	mov	rsi, EXP_A_LEN
	call	ct_count
	cmp	rax, 1
	jne	.bad_a
	lea	rdi, [ct_s02]
	mov	rsi, ct_s02_LEN
	call	ct_run
	call	ct_dump
	lea	rdi, [exp_b]
	mov	rsi, EXP_B_LEN
	call	ct_count
	cmp	rax, 1
	jne	.bad_b

	xor	edi, edi
	call	sys_exit_group
  .bad_a:
	mov	edi, 51
	call	sys_exit_group
  .bad_b:
	mov	edi, 52
	call	sys_exit_group

; ---- harness ---------------------------------------------------------------
; tests/unit/cst_structlit.asm's, plus `ct_text`/`ct_same`. Plain labels, not
; `proc`: a `proc` argument name is an unmangled global (macros/proc.inc's
; header). Every helper pushes an ODD number of registers so `rsp` is
; 16-aligned at the calls inside it.

; ct_setup -- two arenas and an interner. The interner's arena is never reset:
; leaf text lives in it.
  ct_setup:
	push	rbx
	lea	rdi, [ct_arena]
	mov	rsi, CT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [ct_iarena]
	mov	rsi, CT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [ct_intern]
	lea	rsi, [ct_iarena]
	mov	rdx, 1024
	call	intern_init
	pop	rbx
	ret
  .boom:
	mov	edi, 99
	call	sys_exit_group

; ct_run(rdi = source bytes, rsi = length) -- lex and parse into a fresh tree.
  ct_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	lea	rdi, [ct_arena]
	call	arena_reset
	lea	rdi, [ct_toks]
	lea	rsi, [ct_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 64
	call	vec_init
	lea	rdi, [ct_diags]
	lea	rsi, [ct_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [ct_lx]
	lea	rsi, [ct_arena]
	lea	rdx, [ct_intern]
	lea	rcx, [ct_toks]
	lea	r8,  [ct_diags]
	call	lex_init
	lea	rdi, [ct_lx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [ct_path]
	mov	r8d, CT_PATH_LEN
	call	lex_set_source
	lea	rdi, [ct_lx]
	call	lex_run
	jc	.boom
	lea	rdi, [ct_green]
	lea	rsi, [ct_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 64
	call	vec_init
	lea	rdi, [ct_work]
	lea	rsi, [ct_arena]
	mov	rdx, 4
	mov	rcx, 64
	call	vec_init
	lea	rdi, [ct_map]
	lea	rsi, [ct_arena]
	mov	rdx, 256
	call	map_init
	lea	rdi, [ct_tree]
	lea	rsi, [ct_arena]
	lea	rdx, [ct_intern]
	lea	rcx, [ct_green]
	lea	r8,  [ct_work]
	lea	r9,  [ct_map]
	call	cst_tree_init
	lea	rdi, [ct_p]
	lea	rsi, [ct_lx]
	lea	rdx, [ct_tree]
	call	cst_parse
	pop	r13
	pop	r12
	pop	rbx
	ret
  .boom:
	; a sample that fails spec §8.1 is a broken fixture, not a parse result
	mov	edi, 99
	call	sys_exit_group

; ct_text(rdi = the exit code to use on truncation) -- the last tree's SOURCE
; TEXT into ct_buf; ct_len = its length.
  ct_text:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	lea	rdi, [ct_out]
	lea	rsi, [ct_buf]
	mov	rdx, CT_BUF
	call	diag_out_init
	lea	rdi, [ct_tree]
	lea	rax, [ct_tree]
	mov	esi, [rax + CstTree.root]
	lea	rdx, [ct_out]
	call	cst_text
	test	eax, eax
	jz	.short
	lea	rax, [ct_out]
	mov	rax, [rax + DiagOut.len]
	mov	[ct_len], rax
	pop	r13
	pop	r12
	pop	rbx
	ret
  .short:
	mov	edi, r12d
	call	sys_exit_group

; ct_same(rdi = bytes, rsi = length) -> eax = 1 if ct_buf[0..ct_len) is exactly
; those bytes.
  ct_same:
	push	rbx
	cmp	rsi, [ct_len]
	jne	.no
	xor	ecx, ecx
  .byte:
	cmp	rcx, rsi
	jae	.yes
	lea	rdx, [ct_buf]
	mov	r8b, [rdx + rcx]
	cmp	r8b, [rdi + rcx]
	jne	.no
	inc	rcx
	jmp	.byte
  .yes:
	mov	eax, 1
	pop	rbx
	ret
  .no:
	xor	eax, eax
	pop	rbx
	ret

; ct_dump -- the last tree's dump into ct_buf; ct_len = its length. Printed to
; stdout as well when the fixture was given an argument.
  ct_dump:
	push	rbx
	lea	rdi, [ct_out]
	lea	rsi, [ct_buf]
	mov	rdx, CT_BUF
	call	diag_out_init
	lea	rdi, [ct_tree]
	lea	rsi, [ct_out]
	xor	edx, edx
	call	cst_dump
	mov	rax, [ct_out + DiagOut.len]
	mov	[ct_len], rax
	cmp	qword [ct_argc], 1
	jbe	.quiet
	mov	rdi, 1
	lea	rsi, [ct_buf]
	mov	rdx, [ct_len]
	call	sys_write
  .quiet:
	pop	rbx
	ret

; ct_count(rdi = needle, rsi = needle length) -> rax = how many times the
; needle occurs in the last dump. A byte search, overlapping matches counted.
  ct_count:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	xor	eax, eax		; matches
	xor	ecx, ecx		; start position
  .pos:
	mov	rdx, rcx
	add	rdx, r13
	cmp	rdx, [ct_len]
	ja	.out
	xor	r8, r8
  .cmp:
	cmp	r8, r13
	jae	.hit
	lea	r9, [ct_buf]
	add	r9, rcx
	mov	r10b, [r9 + r8]
	cmp	r10b, [r12 + r8]
	jne	.miss
	inc	r8
	jmp	.cmp
  .hit:
	inc	rax
  .miss:
	inc	rcx
	jmp	.pos
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; ct_ndiag -> rax = diagnostics from the last ct_run (lexer's and parser's).
  ct_ndiag:
	mov	rax, [ct_diags + Vec.len]
	ret

; ct_diag(rdi = index) -> rax = that `Diag`.
  ct_diag:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [ct_diags]
	call	vec_get
	pop	rbx
	ret

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'
  ct_path:	db 'cst_intdiv_rem.exsc'
  CT_PATH_LEN = $ - ct_path

  ; `publica functio f() -> u8 {` is 27 bytes, so line 2's tab is at 28 and its
  ; first character at 29. Every offset in this file's header is read off that.
  macro ct_stmt name, body
	name: db 'publica functio f() -> u8 {', 10, 9, body, 10, '}', 10
	name#_LEN = $ - name
  end macro

  ct_stmt ct_s01, 'x = radix_quadrata a;'
  ct_stmt ct_s02, 'x = radix_quadrata radix_quadrata a;'
  ct_stmt ct_s03, 'firma radix = 2.0;'

  ; Row 4 is `examples/streamdb/lector_streamdb.exsc`'s own shape, the file a
  ; contextual `radix` broke: the name bound, compared with a contextual
  ; infix word, and stored.
  ct_s04:	db 'publica functio f(n: mensura) -> u8 {', 10
		db 9, 'firma radix = n;', 10
		db 9, 'si radix ge n {', 10
		db 9, '}', 10
		db 9, 'redde 0;', 10
		db '}', 10
  ct_s04_LEN = $ - ct_s04

  ct_stmt ct_s05, 'x = a radix_quadrata b;'
  ct_stmt ct_s06, 'x = a + radix_quadrata b;'

  kw_mul:	db 'UNARY_EXPR@'
  KW_MUL_LEN = $ - kw_mul
  kw_call:	db 'CALL_SUFFIX@'
  KW_CALL_LEN = $ - kw_call

  ; SHAPE A, the precedence. Row 6's `x = a + radix_quadrata b;` as one exact
  ; block: `ADD_EXPR` over a `UNARY_EXPR`, so the unary binds tighter than the
  ; additive level exactly as `-` does. The operator leaf is a TK_KEYWORD and
  ; not a TK_IDENT, which is the lexer half of "reserved, not contextual".
  exp_a:	db 'ADD_EXPR@32+21', 10
		db '            NAME_EXPR@32+2', 10
		db '              TK_WS@32+1', 10
		db '              TK_IDENT@33+1', 10
		db '            TK_WS@34+1', 10
		db '            TK_PUNCT@35+1', 10
		db '            UNARY_EXPR@36+17', 10
		db '              TK_WS@36+1', 10
		db '              TK_KEYWORD@37+14', 10
		db '              NAME_EXPR@51+2', 10
		db '                TK_WS@51+1', 10
		db '                TK_IDENT@52+1', 10
  EXP_A_LEN = $ - exp_a

  ; SHAPE B, the nesting. Row 2's `radix_quadrata radix_quadrata a` as UNARY
  ; over UNARY over the name -- the right-recursion `Unary ::= 'radix_quadrata'
  ; Unary` claims.
  exp_b:	db 'UNARY_EXPR@32+32', 10
		db '            TK_WS@32+1', 10
		db '            TK_KEYWORD@33+14', 10
		db '            UNARY_EXPR@47+17', 10
		db '              TK_WS@47+1', 10
		db '              TK_KEYWORD@48+14', 10
		db '              NAME_EXPR@62+2', 10
		db '                TK_WS@62+1', 10
		db '                TK_IDENT@63+1', 10
  EXP_B_LEN = $ - exp_b

  ct_tab:
	dq ct_s01, ct_s01_LEN
	dd 0, 0, 0, 0, 1, 0
	dq ct_s02, ct_s02_LEN
	dd 0, 0, 0, 0, 2, 0
	dq ct_s03, ct_s03_LEN
	dd 0, 0, 0, 0, 0, 0
	dq ct_s04, ct_s04_LEN
	dd 0, 0, 0, 0, 0, 0
	dq ct_s05, ct_s05_LEN
	dd 2, 201, 35, 14, 0, 0
	dq ct_s06, ct_s06_LEN
	dd 0, 0, 0, 0, 1, 0
  CT_NROWS = ($ - ct_tab) / CT_ROW
  ; a row of the wrong width would shift every row after it
  assert ($ - ct_tab) mod CT_ROW = 0

segment readable writeable
  ct_argc:	rq 1
  ct_len:	rq 1
  ct_arena:	rb sizeof.Arena
  ct_iarena:	rb sizeof.Arena
  ct_intern:	rb sizeof.Interner
  ct_toks:	rb sizeof.Vec
  ct_diags:	rb sizeof.Vec
  ct_lx:	rb sizeof.Lexer
  ct_green:	rb sizeof.Vec
  ct_work:	rb sizeof.Vec
  ct_map:	rb sizeof.Map
  ct_tree:	rb sizeof.CstTree
  ct_p:		rb sizeof.CstParser
  ct_out:	rb sizeof.DiagOut
  ct_buf:	rb CT_BUF
