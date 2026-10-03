; tests/unit/lwr_unprovided.asm
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
;
; lowering fixture -- ADR 0017, blocker 2: the lowering's answer to an atom it
; has no carrier for. The lowering used to `rassert` there (SIGILL, "a checker
; bug"); it now records the node (`lwr_unprovided`) and `lwr_module` returns it,
; so the driver can raise the checker's own `EXS-E0421` (driver/run.inc,
; `.lower_refused`) instead of crashing.
;
; The driver never lowers a tree that carries a diagnostic, so this fixture
; does what it never does on purpose: it runs the front end, EXPECTS the
; checker's `EXS-E0421`, and lowers the tree anyway.
;
;   A  a call to an inferred function that draws `archivum`, from a caller
;      holding none -- the carrier staged at the CALL (`__lwr_stage_carriers`)
;   B  `archivum` named in a function declared only `poscit sicut d` -- the
;      atom as a VALUE (`__lwr_path`'s atom arm)
;
; Each row asserts that the front end raised exactly one diagnostic, EXS-E0421,
; that `lwr_module` returned a node, and that the node's span IS the span of
; the checker's diagnostic: the two passes name the same place.
;
; NON-VACUITY, run when this fixture was written: restoring the two
; `rassert eax ne 0` in lower/expr.inc (the atom arm and the staged carrier)
; makes this fixture exit 132 at row A.
;
; NOT COVERED, and not claimed: the driver half. No source reaches
; `.lower_refused` today -- the closure shapes that would are `EXS-E0421` in the
; checker (`__chk_row_lamof`), and a lambda is refused by the lowering before
; its body is read -- so that branch is `[UNTESTED]` end to end.
;
; Exit 0 = both rows held. 10 = setup, 11 = a row's front end did not raise
; exactly one EXS-E0421, 12 = `lwr_module` accepted the tree, 13 = it named a
; node that is not the checker's.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.bad10
	lea	rdi, [fx_srcA]
	mov	rsi, FX_SRCA_LEN
	mov	edx, AST_CALL
	call	fx_row
	test	eax, eax
	jnz	.out
	lea	rdi, [fx_srcB]
	mov	rsi, FX_SRCB_LEN
	mov	edx, AST_PATH
	call	fx_row
  .out:
	mov	edi, eax
	call	sys_exit_group
  .bad10:
	mov	edi, 10
	call	sys_exit_group

; fx_row(rdi = source, rsi = length, edx = the node kind expected) -> eax = 0 if
; the row held, else its exit code.
  fx_row:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	mov	r15d, edx
	call	fx_front
	cmp	rax, 1
	jne	.bad11
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	cmp	dword [rax + Diag.code_num], 421
	jne	.bad11
	mov	r12d, [rax + Diag.span.start]
	mov	r13d, [rax + Diag.span.len]
	lea	rdi, [fx_mod]
	lea	rsi, [fx_arena]
	call	bfa_module_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_mod]
	lea	rdx, [fx_arena]
	lea	rcx, [fx_lscr]
	call	lwr_module
	test	rax, rax
	jz	.bad12
	mov	rsi, rax
	lea	rdi, [fx_tree]
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, r15d
	jne	.bad13
	cmp	[rax + AstNode.span.start], r12d
	jne	.bad13
	cmp	[rax + AstNode.span.len], r13d
	jne	.bad13
	xor	eax, eax
	jmp	.ret
  .bad11:
	mov	eax, 11
	jmp	.ret
  .bad12:
	mov	eax, 12
	jmp	.ret
  .bad13:
	mov	eax, 13
  .ret:
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'
include '../../compiler/x86_64/lower/lower.inc'

; ---- harness ---------------------------------------------------------------
; Plain labels (a `proc` argument name is an unmangled global); each helper
; pushes an odd number of registers.

; fx_front -> rax = diagnostics from the lexer, the parser and the checker
; together; the source is fixed, so anything but 0 is a broken fixture.
  fx_front:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_scratch]
	call	arena_reset
	lea	rdi, [fx_lscr]
	call	arena_reset
	lea	rdi, [fx_toks]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_diags]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 32
	call	vec_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_toks]
	lea	r8,  [fx_diags]
	call	lex_init
	lea	rdi, [fx_lx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	jc	.bad
	lea	rdi, [fx_green]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 512
	call	vec_init
	lea	rdi, [fx_work]
	lea	rsi, [fx_arena]
	mov	rdx, 4
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_cmap]
	lea	rsi, [fx_arena]
	mov	rdx, 1024
	call	map_init
	lea	rdi, [fx_ctree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_green]
	lea	r8,  [fx_work]
	lea	r9,  [fx_cmap]
	call	cst_tree_init
	lea	rdi, [fx_parser]
	lea	rsi, [fx_lx]
	lea	rdx, [fx_ctree]
	call	cst_parse
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	cmp	qword [fx_diags + Vec.len], 0
	jne	.bad
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chk]
	mov	rsi, 64			; --hospes x86_64-linux (spec §9.5)
	call	chk_set_target
	lea	rdi, [fx_chk]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	mov	rax, [fx_diags + Vec.len]	; the WHOLE vector, as chk_e0422's fx_run
	pop	r13
	pop	r12
	pop	rbx
	ret
  .bad:
	mov	rax, -1
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_setup -> eax = 0 once the four arenas and the interner exist.
  fx_setup:
	push	rbx
	lea	rdi, [fx_arena]
	mov	rsi, 32 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_iarena]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_scratch]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_lscr]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_names]
	lea	rsi, [fx_iarena]
	mov	rdx, 1024
	call	intern_init
	xor	eax, eax
	pop	rbx
	ret
  .bad:
	mov	eax, 1
	pop	rbx
	ret

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

  fx_path	db 'lwr_unprovided.exsc'
  FX_PATH_LEN = $ - fx_path

  ; A: `radix` draws `archivum` and has no row, so its row is inferred and it
  ; takes a hidden carrier; `initium` holds none. Checker: EXS-E0421 at `radix()`.
  fx_srcA:	db 'functio radix() -> u8 {', 10
		db 9, 'discerne Directorium.ad_radicem(archivum, "/") {', 10
		db 9, 9, 'casus prosperum(d) { redde 0; }', 10
		db 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, '}', 10
		db '}', 10
		db 'publica functio initium(m: Mundus) -> u8 { redde radix(); }', 10
  FX_SRCA_LEN = $ - fx_srcA

  ; B: declared `poscit sicut d`, which binds no carrier (design R1), so the
  ; `archivum` in expression position is undeclared. Checker: EXS-E0421.
  fx_srcB:	db 'publica functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'discerne Directorium.ad_radicem(archivum, "/") {', 10
		db 9, 9, 'casus prosperum(e) { redde 0; }', 10
		db 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, '}', 10
		db '}', 10
  FX_SRCB_LEN = $ - fx_srcB

segment readable writeable
  fx_arena:	rb sizeof.Arena
  fx_iarena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_lscr:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_toks:	rb sizeof.Vec
  fx_diags:	rb sizeof.Vec
  fx_lx:	rb sizeof.Lexer
  fx_green:	rb sizeof.Vec
  fx_work:	rb sizeof.Vec
  fx_cmap:	rb sizeof.Map
  fx_ctree:	rb sizeof.CstTree
  fx_parser:	rb sizeof.CstParser
  fx_tree:	rb sizeof.Ast
  fx_chk:	rb sizeof.ChkCtx
  fx_mod:	rb sizeof.BfaModule
