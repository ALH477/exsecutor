; tests/unit/ast_block_scopes.asm
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
; ast fixture for the declaration-range invariant of
; compiler/x86_64/ast/verify.inc.
;
; docs/design/typed-ast.md section 2.4: "a block's bindings are the contiguous
; range [Block.c, Block.c + Block.aux), nested blocks owning sub-ranges". That
; is not decoration -- docs/design/ssa-ir.md 2.5 indexes it ("var id = binding
; order") and 2.8 walks it in reverse at every scope exit to emit `release`.
; If the ranges do not nest, both are wrong, silently.
;
; The tree is three blocks and four bindings:
;
;   {                 outer, decls [1,5)   -- n7
;     firma p;          d1                 -- n1
;     { firma q; }      d2, block [2,3)    -- n2, n3
;     { firma r; }      d3, block [3,4)    -- n4, n5
;     firma s;          d4                 -- n6
;   }
;
; so the two inner ranges are DISJOINT from each other and both CONTAINED in
; the outer one -- the two shapes ast/verify.inc's stack walk distinguishes.
; The overlapping case is a separate NEGATIVE fixture,
; tests/unit/ast_verify_overlap.asm, because it terminates the process.
;
;   1. every constructor call returns the id creation order says it should
;   2. `ast_verify_stage1` accepts nested and sibling ranges
;   3. the ranges read back exactly as written
;   4. an empty block's range is canonically (0, 0)
;
; Exit 0 = all checks passed; 10+N = check N failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 64
	call	intern_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_names]
	lea	rsi, [fx_t_p]
	mov	rdx, 1
	call	intern_id
	mov	[fx_nm], rax

	; four bindings, in source order: p, q, r, s
	mov	rdi, AST_D_BINDING
	call	fx_decl
	cmp	rax, 1
	jne	.fail1
	mov	rdi, AST_D_BINDING
	call	fx_decl
	cmp	rax, 2
	jne	.fail1
	mov	rdi, AST_D_BINDING
	call	fx_decl
	cmp	rax, 3
	jne	.fail1
	mov	rdi, AST_D_BINDING
	call	fx_decl
	cmp	rax, 4
	jne	.fail1

	; n1 Binding (p)
	mov	rdi, AST_BINDING
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_node
	cmp	rax, 1
	jne	.fail1
	lea	rdi, [fx_tree]
	mov	rsi, 1
	xor	rdx, rdx
	mov	rcx, 1
	call	ast_node_cd
	; n2 Binding (q)
	mov	rdi, AST_BINDING
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_node
	cmp	rax, 2
	jne	.fail1
	lea	rdi, [fx_tree]
	mov	rsi, 2
	xor	rdx, rdx
	mov	rcx, 2
	call	ast_node_cd
	; n3 inner block A: stmts [n2], decls [2,3)
	mov	rdi, 2
	call	fx_list1
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, AST_BLOCK
	mov	rsi, 1			; aux = one declaration
	mov	rdx, r12
	mov	rcx, r13
	call	fx_node
	cmp	rax, 3
	jne	.fail1
	lea	rdi, [fx_tree]
	mov	rsi, 3
	mov	rdx, 2			; c = first declaration
	xor	rcx, rcx
	call	ast_node_cd
	; n4 Binding (r)
	mov	rdi, AST_BINDING
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_node
	cmp	rax, 4
	jne	.fail1
	lea	rdi, [fx_tree]
	mov	rsi, 4
	xor	rdx, rdx
	mov	rcx, 3
	call	ast_node_cd
	; n5 inner block B: stmts [n4], decls [3,4)
	mov	rdi, 4
	call	fx_list1
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, AST_BLOCK
	mov	rsi, 1
	mov	rdx, r12
	mov	rcx, r13
	call	fx_node
	cmp	rax, 5
	jne	.fail1
	lea	rdi, [fx_tree]
	mov	rsi, 5
	mov	rdx, 3
	xor	rcx, rcx
	call	ast_node_cd
	; n6 Binding (s)
	mov	rdi, AST_BINDING
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_node
	cmp	rax, 6
	jne	.fail1
	lea	rdi, [fx_tree]
	mov	rsi, 6
	xor	rdx, rdx
	mov	rcx, 4
	call	ast_node_cd
	; n7 outer block: stmts [n1, n3, n5, n6], decls [1,5)
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	r14, rax
	lea	rdi, [fx_tree]
	mov	rsi, 1
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, 3
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, 5
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, 6
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, r14
	call	ast_list_emit
	mov	r12, rax
	mov	r13, rdx
	cmp	r13, 4
	jne	.fail1
	mov	rdi, AST_BLOCK
	mov	rsi, 4			; aux = four declarations
	mov	rdx, r12
	mov	rcx, r13
	call	fx_node
	cmp	rax, 7
	jne	.fail1
	lea	rdi, [fx_tree]
	mov	rsi, 7
	mov	rdx, 1
	xor	rcx, rcx
	call	ast_node_cd

	; back-links, so __ast_v_decls has something to check
	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, 1
	call	ast_decl_node
	lea	rdi, [fx_tree]
	mov	rsi, 2
	mov	rdx, 2
	call	ast_decl_node
	lea	rdi, [fx_tree]
	mov	rsi, 3
	mov	rdx, 4
	call	ast_decl_node
	lea	rdi, [fx_tree]
	mov	rsi, 4
	mov	rdx, 6
	call	ast_decl_node

	; ---- 2 ----
	lea	rdi, [fx_tree]
	call	ast_verify_stage1

	; ---- 3 ----
	lea	rdi, [fx_tree]
	mov	rsi, 7
	call	ast_node_at
	mov	ecx, [rax + AstNode.c]
	cmp	ecx, 1
	jne	.fail3
	movzx	ecx, word [rax + AstNode.aux]
	cmp	ecx, 4
	jne	.fail3
	lea	rdi, [fx_tree]
	mov	rsi, 5
	call	ast_node_at
	mov	ecx, [rax + AstNode.c]
	cmp	ecx, 3
	jne	.fail3

	; ---- 4: an empty block ----
	mov	rdi, AST_BLOCK
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_node
	cmp	rax, 8
	jne	.fail4
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	lea	rdi, [fx_tree]
	mov	rsi, 8
	call	ast_node_at
	mov	ecx, [rax + AstNode.c]
	test	ecx, ecx
	jnz	.fail4

	xor	edi, edi
	call	sys_exit_group
  .fail0:
	mov	rdi, 10
	call	sys_exit_group
  .fail1:
	mov	rdi, 11
	call	sys_exit_group
  .fail3:
	mov	rdi, 13
	call	sys_exit_group
  .fail4:
	mov	rdi, 14
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

proc fx_node, kind, aux, a, b
	locals
	endl
	lea	rdi, [fx_sp]
	mov	rsi, 7
	mov	rdx, 0
	mov	rcx, 1
	call	span_make
	lea	rdi, [fx_tree]
	mov	rsi, [kind]
	mov	rdx, [aux]
	mov	rcx, [a]
	mov	r8, [b]
	lea	r9, [fx_sp]
	call	ast_node
	return
endp

proc fx_decl, kind
	locals
	endl
	lea	rdi, [fx_sp]
	mov	rsi, 7
	mov	rdx, 0
	mov	rcx, 1
	call	span_make
	lea	rdi, [fx_tree]
	mov	rsi, [kind]
	xor	rdx, rdx
	mov	rcx, [fx_nm]
	xor	r8, r8
	lea	r9, [fx_sp]
	call	ast_decl
	return
endp

proc fx_list1, v
	uses	rbx
	locals
	endl
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	rbx, rax
	lea	rdi, [fx_tree]
	mov	rsi, [v]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_list_emit
	return
endp

segment readable
  ; ast/ast.inc now reaches cst/, which reaches the lexer, which
  ; references the UCD blobs -- emitted exactly once, in a segment
  ; the consumer chooses (lexer/lexer.inc's header).
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_t_p:	db 'p'
  fx_nm:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_sp:	rb sizeof.Span
