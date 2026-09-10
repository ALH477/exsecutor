; tests/unit/ast_from_cst_error.asm
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
; ast fixture: a MALFORMED file still becomes a typed tree.
;
; This is spec §16's Stage 1 kill criterion made checkable. That criterion is
; DIAGNOSTICS, and a front end that stopped at the first parse error could not
; report a second -- so §9.1's CST is error-tolerant, and
; `docs/design/typed-ast.md` section 2.11 requires the AST to inherit that:
; "an `ERROR` or `MISSING` CST node becomes an `Error` node with its span, so
; a malformed file still yields a tree and Stage 2 still types the rest".
;
; The source is one good function and one stray `;` at module level, which
; spec §8.6's `Module ::= Item* EOF` does not admit:
;
;	publica functio f() -> u32 { redde 1; }
;	;
;
;   1. the parse REPORTS -- `cst_parse` sets CF and the diagnostic vector is
;      not empty. Without this the rest would be testing a clean file.
;   2. a root still comes back, and `ast_verify_stage1` accepts the tree:
;      error tolerance does not mean a broken invariant
;   3. the module has TWO items -- the good one and the bad one; the bad one
;      is not silently dropped
;   4. the second item is an `Error` node whose span is the stray `;`
;   5. the GOOD half is intact and fully typed-tree-shaped: the function node,
;      its declaration, and its `publica` flag all survive the file's error
;
; Exit 0 = all checks passed; 10+N = check N failed (tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 8 * 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 256
	call	intern_init
	lea	rdi, [fx_toks]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 64
	call	vec_init
	lea	rdi, [fx_diags]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_toks]
	lea	r8,  [fx_diags]
	call	lex_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_src]
	mov	rdx, FX_SRC_LEN
	lea	rcx, [fx_path]
	mov	r8d, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	jc	.fail0			; §8.1 must be clean: the defect under
					; test is a GRAMMAR defect, not a
					; source-policy one

	lea	rdi, [fx_green]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 64
	call	vec_init
	lea	rdi, [fx_work]
	lea	rsi, [fx_arena]
	mov	rdx, 4
	mov	rcx, 64
	call	vec_init
	lea	rdi, [fx_cmap]
	lea	rsi, [fx_arena]
	mov	rdx, 256
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

	; ---- check 1: it really is malformed ----
	jnc	.fail1
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	test	rax, rax
	jz	.fail1

	; ---- check 2 ----
	lea	rdi, [fx_ast]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rcx, [fx_lx]
	mov	edx, [rcx + Lexer.file_id]
	lea	rdi, [fx_ast]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	test	rax, rax
	jz	.fail2
	mov	[fx_root], rax
	lea	rdi, [fx_ast]
	call	ast_verify_stage1

	lea	rdi, [fx_wr]
	lea	rsi, [fx_buf]
	mov	rdx, 65536
	call	diag_out_init
	lea	rdi, [fx_ast]
	lea	rsi, [fx_wr]
	call	ast_dump
	mov	rdi, 1
	lea	rsi, [fx_buf]
	mov	rdx, [fx_wr + DiagOut.len]
	call	sys_write

	; ---- check 3: two items ----
	lea	rdi, [fx_ast]
	mov	rsi, [fx_root]
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_MODULE
	jne	.fail3
	mov	r8d, [rax + AstNode.a]		; the `extra` offset
	mov	ecx, [rax + AstNode.b]
	cmp	ecx, 2
	jne	.fail3

	; ---- check 4: the second item is the Error, at the stray `;` ----
	lea	rdi, [fx_ast]
	lea	rsi, [r8 + 1]
	call	ast_extra_at
	mov	r12, rax
	lea	rdi, [fx_ast]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_ERROR
	jne	.fail4
	mov	ecx, [rax + AstNode.span.start]
	cmp	ecx, 40
	jne	.fail4
	mov	ecx, [rax + AstNode.span.len]
	cmp	ecx, 1
	jne	.fail4

	; ---- check 5: the good half survived ----
	lea	rdi, [fx_ast]
	mov	rsi, [fx_root]
	call	ast_node_at
	mov	r8d, [rax + AstNode.a]
	lea	rdi, [fx_ast]
	mov	rsi, r8
	call	ast_extra_at
	mov	r12, rax
	lea	rdi, [fx_ast]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_FN
	jne	.fail5
	movzx	ecx, word [rax + AstNode.aux]
	and	ecx, AST_FN_PUBLICA
	cmp	ecx, AST_FN_PUBLICA
	jne	.fail5
	mov	r13d, [rax + AstNode.d]
	test	r13d, r13d
	jz	.fail5
	lea	rdi, [fx_ast]
	mov	rsi, r13
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_FN
	jne	.fail5
	movzx	ecx, byte [rax + AstDecl.flags]
	and	ecx, AST_F_PUBLICA
	cmp	ecx, AST_F_PUBLICA
	jne	.fail5

	xor	edi, edi
	call	sys_exit_group
  .fail0:
	mov	rdi, 10
	call	sys_exit_group
  .fail1:
	mov	rdi, 11
	call	sys_exit_group
  .fail2:
	mov	rdi, 12
	call	sys_exit_group
  .fail3:
	mov	rdi, 13
	call	sys_exit_group
  .fail4:
	mov	rdi, 14
	call	sys_exit_group
  .fail5:
	mov	rdi, 15
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path:	db 'fixture.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_src:	db 'publica functio f() -> u32 { redde 1; }', 10, ';', 10
  FX_SRC_LEN = $ - fx_src
  fx_root:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_toks:	rb sizeof.Vec
  fx_diags:	rb sizeof.Vec
  fx_lx:	rb sizeof.Lexer
  fx_green:	rb sizeof.Vec
  fx_work:	rb sizeof.Vec
  fx_cmap:	rb sizeof.Map
  fx_ctree:	rb sizeof.CstTree
  fx_parser:	rb sizeof.CstParser
  fx_ast:	rb sizeof.Ast
  fx_wr:	rb sizeof.DiagOut
  fx_buf:	rb 65536
