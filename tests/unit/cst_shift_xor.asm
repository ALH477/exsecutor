; tests/unit/cst_shift_xor.asm
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
; cst fixture: spec §8.6's levels 5a (`sursum` `deorsum`, non-associative) and
; 5b (`aut`, left-associative), both `[UNTESTED]` in the spec until this runs,
; through the real lexer and `cst_parse`. Four cases:
;
;   A. PRECEDENCE. `x = a + b sursum c aut d .. e;` must group as
;      `(((a + b) sursum c) aut d) .. e` -- additive binds tighter than shift,
;      shift tighter than `aut`, `aut` tighter than `..`. Checked on the
;      tree's own dump (cst/red.inc's `cst_dump`: one `KIND@off+len` line per
;      node, two spaces of indent per level), as FIVE CONSECUTIVE LINES, each
;      one level deeper than the last, with the offsets computed by hand from
;      the source below:
;
;	RANGE_EXPR@32+26   " a + b sursum c aut d .. e"
;	  XOR_EXPR@32+21   " a + b sursum c aut d"
;	    SHIFT_EXPR@32+15 " a + b sursum c"
;	      ADD_EXPR@32+6  " a + b"
;	        NAME_EXPR@32+2 " a"
;
;      A leading space belongs to the node, because the parser emits the gap
;      before a token into whatever is open when it bumps it (cst/parse.inc,
;      "TRIVIA IS A GAP"). Swapping any two of the four levels changes a kind
;      on one of these lines, and a lost byte changes an offset.
;   B. NON-ASSOCIATIVITY. `x = a sursum 1 sursum 2;` is `EXS-E0201` at the
;      SECOND `sursum` (offset 44, length 6), by the mechanism that rejects
;      `a lt b lt c` -- spec §8.6's own sentence. Its twin
;      `x = (a sursum 1) sursum 2;` parses clean, so a parser that rejected
;      every second shift for some other reason fails the twin.
;   C. THE WORDS ARE CONTEXTUAL (spec §8.4 tier 2). Bindings named `aut`,
;      `sursum` and `deorsum`, then `sursum sursum aut` and
;      `aut aut deorsum deorsum sursum`, parse with NO diagnostic; the dump
;      holds exactly one `XOR_EXPR` and two `SHIFT_EXPR`s, so the words were
;      read as operators in operator position and as names everywhere else.
;   D. A WORD IS ITS WHOLE SPELLING. `x = a autem b;` is `EXS-E0201` at
;      `autem` (offset 35, length 5): `autem` begins with `aut` and is not
;      it. `__cst_word` compares the length first; a recogniser that matched
;      a prefix would take `autem` as `aut` and parse this clean.
;
; Run with any argument to print each case's dump to stdout -- how the
; expected block in case A was confirmed.
;
; Exit 0 = every check held. 11 = A had a diagnostic, 12 = A's nesting block
; not found, 21 = B's diagnostic count was 0, 22 = B's first code is not 201,
; 23 = B's span start, 24 = B's span length, 25 = B's twin had a diagnostic,
; 31 = C had a diagnostic, 32 = C's XOR_EXPR count, 33 = C's SHIFT_EXPR
; count, 41 = D had no diagnostic, 42 = D's code, 43 = D's span start,
; 44 = D's span length, 99 = setup failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/cst/cst.inc'

CT_ARENA = 4 shl 20
CT_BUF   = 64 shl 10

segment readable executable

  start:
	mov	rax, [rsp]
	mov	[ct_argc], rax
	call	ct_setup

	; ---- A: precedence ----------------------------------------------------
	lea	rdi, [src_a]
	mov	rsi, SRC_A_LEN
	call	ct_run
	call	ct_ndiag
	test	rax, rax
	jnz	.fail11
	call	ct_dump
	lea	rdi, [exp_a]
	mov	rsi, EXP_A_LEN
	call	ct_count
	cmp	rax, 1
	jne	.fail12

	; ---- B: `a sursum 1 sursum 2` -----------------------------------------
	lea	rdi, [src_b]
	mov	rsi, SRC_B_LEN
	call	ct_run
	call	ct_ndiag
	test	rax, rax
	jz	.fail21
	xor	edi, edi
	call	ct_diag
	cmp	dword [rax + Diag.code_num], 201
	jne	.fail22
	cmp	dword [rax + Diag.span.start], 44
	jne	.fail23
	cmp	dword [rax + Diag.span.len], 6
	jne	.fail24
	lea	rdi, [src_b2]
	mov	rsi, SRC_B2_LEN
	call	ct_run
	call	ct_ndiag
	test	rax, rax
	jnz	.fail25

	; ---- C: the words as names --------------------------------------------
	lea	rdi, [src_c]
	mov	rsi, SRC_C_LEN
	call	ct_run
	call	ct_ndiag
	test	rax, rax
	jnz	.fail31
	call	ct_dump
	lea	rdi, [kw_xor]
	mov	rsi, KW_XOR_LEN
	call	ct_count
	cmp	rax, 1
	jne	.fail32
	lea	rdi, [kw_shift]
	mov	rsi, KW_SHIFT_LEN
	call	ct_count
	cmp	rax, 2
	jne	.fail33

	; ---- D: `autem` is not `aut` ------------------------------------------
	lea	rdi, [src_d]
	mov	rsi, SRC_D_LEN
	call	ct_run
	call	ct_ndiag
	test	rax, rax
	jz	.fail41
	xor	edi, edi
	call	ct_diag
	cmp	dword [rax + Diag.code_num], 201
	jne	.fail42
	cmp	dword [rax + Diag.span.start], 35
	jne	.fail43
	cmp	dword [rax + Diag.span.len], 5
	jne	.fail44

	xor	edi, edi
	call	sys_exit_group
  .fail11:
	mov	edi, 11
	call	sys_exit_group
  .fail12:
	mov	edi, 12
	call	sys_exit_group
  .fail21:
	mov	edi, 21
	call	sys_exit_group
  .fail22:
	mov	edi, 22
	call	sys_exit_group
  .fail23:
	mov	edi, 23
	call	sys_exit_group
  .fail24:
	mov	edi, 24
	call	sys_exit_group
  .fail25:
	mov	edi, 25
	call	sys_exit_group
  .fail31:
	mov	edi, 31
	call	sys_exit_group
  .fail32:
	mov	edi, 32
	call	sys_exit_group
  .fail33:
	mov	edi, 33
	call	sys_exit_group
  .fail41:
	mov	edi, 41
	call	sys_exit_group
  .fail42:
	mov	edi, 42
	call	sys_exit_group
  .fail43:
	mov	edi, 43
	call	sys_exit_group
  .fail44:
	mov	edi, 44
	call	sys_exit_group

; ---- harness ---------------------------------------------------------------
; Plain labels, not `proc`, as in cst_error_tolerance.asm: a `proc` argument
; name is an unmangled global (macros/proc.inc's header). Every helper pushes
; an ODD number of registers so `rsp` is 16-aligned at the calls inside it.

; ct_setup -- two arenas and an interner. The interner's arena is never
; reset: leaf text lives in it.
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
  ct_path:	db 'cst_shift_xor.exsc'
  CT_PATH_LEN = $ - ct_path

  ; line 1 is 28 bytes, so line 2's tab is at 28 and `x` at 29
  src_a:	db 'publica functio f() -> u8 {', 10
		db 9, 'x = a + b sursum c aut d .. e;', 10
		db '}', 10
  SRC_A_LEN = $ - src_a
  exp_a:	db 'RANGE_EXPR@32+26', 10
		db '            XOR_EXPR@32+21', 10
		db '              SHIFT_EXPR@32+15', 10
		db '                ADD_EXPR@32+6', 10
		db '                  NAME_EXPR@32+2', 10
  EXP_A_LEN = $ - exp_a

  ; the second `sursum` is at 29 + 15 = 44
  src_b:	db 'publica functio f() -> u8 {', 10
		db 9, 'x = a sursum 1 sursum 2;', 10
		db '}', 10
  SRC_B_LEN = $ - src_b
  src_b2:	db 'publica functio f() -> u8 {', 10
		db 9, 'x = (a sursum 1) sursum 2;', 10
		db '}', 10
  SRC_B2_LEN = $ - src_b2

  src_c:	db 'publica functio f() -> u8 {', 10
		db 9, 'firma aut: u8 = 1;', 10
		db 9, 'firma sursum: u8 = aut;', 10
		db 9, 'firma deorsum: u8 = sursum sursum aut;', 10
		db 9, 'x = aut aut deorsum deorsum sursum;', 10
		db 9, 'redde deorsum;', 10
		db '}', 10
  SRC_C_LEN = $ - src_c
  kw_xor:	db 'XOR_EXPR@'
  KW_XOR_LEN = $ - kw_xor
  kw_shift:	db 'SHIFT_EXPR@'
  KW_SHIFT_LEN = $ - kw_shift

  ; `autem` is at 29 + 6 = 35
  src_d:	db 'publica functio f() -> u8 {', 10
		db 9, 'x = a autem b;', 10
		db '}', 10
  SRC_D_LEN = $ - src_d

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
