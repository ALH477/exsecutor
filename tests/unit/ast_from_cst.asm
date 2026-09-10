; tests/unit/ast_from_cst.asm
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
; ast fixture for compiler/x86_64/ast/from_cst.inc -- THE SEAM. Source bytes
; in, typed tree out, with the whole front end in between: §8.1's policy
; check, §8.4's tokens, §8.6's parse into §9.1's lossless CST, and then the
; walk that turns that CST into §16 Stage 1's typed AST.
;
; The program is one line, chosen so that every layer has to work:
;
;	publica functio f(x: u32) -> u32 { redde x; }
;
;   1. it lexes and parses with NO diagnostics -- if it did not, checks 2-7
;      would be testing recovery instead of the walk
;   2. `ast_from_cst` returns a root and `ast_verify_stage1` accepts the tree:
;      postorder holds, every list is inside `extra`, every declaration's
;      back-link agrees, and every type slot is still empty
;   3. the counts are exactly what the grammar implies -- ten nodes, two
;      declarations
;   4. `publica` reached BOTH places it belongs: `AstDecl.flags` and the
;      `Fn` node's `aux`. It is a child of `Item` in the CST and of neither
;      the signature nor the body, so nothing else would have carried it.
;   5. the parameter's declaration is parented to the function's -- the walk
;      really does nest `AstDecl.parent` rather than emitting a flat list
;   6. `u32` became `TyBit` with width 32 read back out of the identifier
;   7. SPANS ARE TRIMMED OF TRIVIA. The CST's `TYPE` node for `u32` begins at
;      byte 20, which is the space before it; the AST node begins at 21. That
;      is the one place this walker is allowed to disagree with the CST about
;      position, and a diagnostic pointing at whitespace is why.
;
; The tree is also dumped to stdout, so a reader sees what is being asserted.
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

	; ---- check 1 ----
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	test	rax, rax
	jnz	.fail1

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
	jc	.fail1
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	test	rax, rax
	jnz	.fail1

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

	; the tree, for a reader of this fixture's output
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

	; ---- check 3 ----
	lea	rdi, [fx_ast]
	call	ast_node_count
	cmp	rax, 10
	jne	.fail3
	lea	rdi, [fx_ast]
	call	ast_decl_count
	cmp	rax, 2
	jne	.fail3
	mov	rax, [fx_root]
	cmp	rax, 10
	jne	.fail3
	lea	rdi, [fx_ast]
	mov	rsi, 10
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_MODULE
	jne	.fail3

	; ---- check 4 ----
	lea	rdi, [fx_ast]
	mov	rsi, 9
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_FN
	jne	.fail4
	movzx	ecx, word [rax + AstNode.aux]
	and	ecx, AST_FN_PUBLICA
	cmp	ecx, AST_FN_PUBLICA
	jne	.fail4
	mov	ecx, [rax + AstNode.d]
	cmp	ecx, 1
	jne	.fail4
	lea	rdi, [fx_ast]
	mov	rsi, 1
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_FN
	jne	.fail4
	movzx	ecx, byte [rax + AstDecl.flags]
	and	ecx, AST_F_PUBLICA
	cmp	ecx, AST_F_PUBLICA
	jne	.fail4
	mov	ecx, [rax + AstDecl.node]
	cmp	ecx, 9
	jne	.fail4

	; ---- check 5 ----
	lea	rdi, [fx_ast]
	mov	rsi, 2
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_PARAM
	jne	.fail5
	mov	ecx, [rax + AstDecl.parent]
	cmp	ecx, 1
	jne	.fail5

	; ---- checks 6 and 7 ----
	lea	rdi, [fx_ast]
	mov	rsi, 1
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_TYBIT
	jne	.fail6
	movzx	ecx, word [rax + AstNode.aux]
	cmp	ecx, 32
	jne	.fail6
	mov	ecx, [rax + AstNode.span.start]
	cmp	ecx, 21			; the `u` of `u32`, NOT the space at 20
	jne	.fail7
	mov	ecx, [rax + AstNode.span.len]
	cmp	ecx, 3
	jne	.fail7

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
  .fail6:
	mov	rdi, 16
	call	sys_exit_group
  .fail7:
	mov	rdi, 17
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

segment readable
  ; The UCD blobs, emitted exactly once and AFTER every `proc` in this file:
  ; a `segment readable` opened earlier would land the code that follows it in
  ; a non-executable segment, which assembles clean and segfaults on the first
  ; call (found the hard way -- see this wave's report).
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path:	db 'fixture.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_src:	db 'publica functio f(x: u32) -> u32 { redde x; }', 10
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
