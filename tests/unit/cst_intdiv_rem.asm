; tests/unit/cst_intdiv_rem.asm
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 DeMoD LLC.
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
; cst fixture: spec §8.6's level 4 after §5.4's 2026-09-25 amendment -- the
; contextual word `residuum` beside `*` and `/`, through the real lexer and
; `cst_parse`.
;
; WHAT THIS FIXTURE IS THE EVIDENCE FOR. §5.4 settled the remainder as a WORD
; and put it at the multiplicative level, and §8.4 lists it tier 2 -- contextual,
; an ordinary identifier in operand position. Both halves have to hold at once,
; and the second one is not decoration: `examples/hydramodem/receptor.exsc`
; declares a FUNCTION named `residuum`, so `si residuum(d) ne 0` has to stay a
; call after the word became an operator. Row 3 is that program's shape.
;
; THE LEVEL STAYS FLAT AND LEFT-ASSOCIATIVE, which is the other thing to pin:
; `residuum` is the only word at level 4 and mixes with `*` and `/` exactly as
; they mix with each other -- one `MUL_EXPR` with four operands, not a
; sub-level. Level 5b is where a chain of two different WORDS is refused
; (tests/unit/cst_bitand_or.asm); nothing of that rule reaches here, and shape
; A is what says so.
;
;   row  source (inside `publica functio f() -> u8 {` / `}`)   diag   MUL CALL
;    1   x = a / b residuum c * d;                               0      1    0
;    2   x = a + b residuum c sursum d;                          0      1    0
;    3   a function named `residuum`, a binding named
;        `residuum`, `si residuum(x) ne 0`, `x residuum 2`       0      1    1
;    4   x = a residuus b;   (E0201 at 35, length 8)             2      0    0
;    5   firma residuum: u8 = 1;                                 0      0    0
;    6   x = residuum residuum residuum;                         0      1    0
;
; ROW 4 IS THE WHOLE-SPELLING CHECK, and it is stronger than `autem` vs `aut`
; was in tests/unit/cst_shift_xor.asm: `residuus` is the SAME LENGTH as
; `residuum` and differs in one byte, so a recogniser that compared the length
; and stopped -- or that compared a prefix -- parses this row clean.
;
; SHAPE A, the flat level. The dump of row 1's `MUL_EXPR` subtree
; (cst/red.inc's `cst_dump`: one `KIND@off+len` line per node, two spaces of
; indent per level), matched as one exact block, so four operands at ONE level
; is pinned and not merely the count of `MUL_EXPR` nodes.
;
; SHAPE B, the neighbouring levels. Row 2's `SHIFT_EXPR` / `ADD_EXPR` /
; `MUL_EXPR` as three consecutive, deepening lines: 5a looser than 5, 5 looser
; than 4, so `a + b residuum c sursum d` is `((a + (b residuum c)) sursum d)`.
; Swapping any two of the three changes a kind on one of these lines.
;
; EVERY ROW ALSO ROUND-TRIPS. `cst_text` on the root must give back the source
; byte for byte -- an operator word's leading whitespace is part of that, and a
; node that dropped it would still have the right node counts.
;
; THIS FIXTURE STOPS AT THE CST. `AST_OP_REM` exists (ast/kinds.inc) and
; ast/from_cst.inc folds it, but checker/types/types.inc `rassert`s on an op
; appended after `AST_OP_DIV` that it does not know, deliberately, until its
; arms are widened -- so nothing here runs the checker.
;
; Run with any argument to print each row's dump to stdout -- how the two
; expected blocks were confirmed.
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
	lea	rdi, [ct_s01]
	mov	rsi, ct_s01_LEN
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

  ct_stmt ct_s01, 'x = a / b residuum c * d;'
  ct_stmt ct_s02, 'x = a + b residuum c sursum d;'

  ; Row 3 is `examples/hydramodem/receptor.exsc`'s shape: a function named
  ; `residuum`, called in a condition, with the operator used in the same file.
  ct_s03:	db 'publica functio residuum(d: u8) -> u8 {', 10
		db 9, 'redde d;', 10
		db '}', 10
		db 'publica functio f(x: u8) -> u8 {', 10
		db 9, 'firma residuum: u8 = 1;', 10
		db 9, 'si residuum(x) ne 0 {', 10
		db 9, '}', 10
		db 9, 'redde x residuum 2;', 10
		db '}', 10
  ct_s03_LEN = $ - ct_s03

  ct_stmt ct_s04, 'x = a residuus b;'
  ct_stmt ct_s05, 'firma residuum: u8 = 1;'
  ct_stmt ct_s06, 'x = residuum residuum residuum;'

  kw_mul:	db 'MUL_EXPR@'
  KW_MUL_LEN = $ - kw_mul
  kw_call:	db 'CALL_SUFFIX@'
  KW_CALL_LEN = $ - kw_call

  exp_a:	db 'MUL_EXPR@32+21', 10
		db '            NAME_EXPR@32+2', 10
		db '              TK_WS@32+1', 10
		db '              TK_IDENT@33+1', 10
		db '            TK_WS@34+1', 10
		db '            TK_PUNCT@35+1', 10
		db '            NAME_EXPR@36+2', 10
		db '              TK_WS@36+1', 10
		db '              TK_IDENT@37+1', 10
		db '            TK_WS@38+1', 10
		db '            TK_IDENT@39+8', 10
		db '            NAME_EXPR@47+2', 10
		db '              TK_WS@47+1', 10
		db '              TK_IDENT@48+1', 10
		db '            TK_WS@49+1', 10
		db '            TK_PUNCT@50+1', 10
		db '            NAME_EXPR@51+2', 10
		db '              TK_WS@51+1', 10
		db '              TK_IDENT@52+1', 10
  EXP_A_LEN = $ - exp_a
  exp_b:	db 'SHIFT_EXPR@32+26', 10
		db '            ADD_EXPR@32+17', 10
		db '              NAME_EXPR@32+2', 10
		db '                TK_WS@32+1', 10
		db '                TK_IDENT@33+1', 10
		db '              TK_WS@34+1', 10
		db '              TK_PUNCT@35+1', 10
		db '              MUL_EXPR@36+13', 10
  EXP_B_LEN = $ - exp_b

  ct_tab:
	dq ct_s01, ct_s01_LEN
	dd 0, 0, 0, 0, 1, 0
	dq ct_s02, ct_s02_LEN
	dd 0, 0, 0, 0, 1, 0
	dq ct_s03, ct_s03_LEN
	dd 0, 0, 0, 0, 1, 1
	dq ct_s04, ct_s04_LEN
	dd 2, 201, 35, 8, 0, 0
	dq ct_s05, ct_s05_LEN
	dd 0, 0, 0, 0, 0, 0
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
