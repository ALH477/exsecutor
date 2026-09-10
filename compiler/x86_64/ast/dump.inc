; compiler/x86_64/ast/dump.inc
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
; THE TEXTUAL DUMP. One record per line, whitespace-separated tokens, dense
; 1-based ids -- `docs/design/ssa-ir.md` 2.11's format applied to this tree,
; for the reason that section gives: a representation with a text form can be
; built, read and compared BY HAND, so it can be tested before the thing that
; produces it exists. The IR can be tested with no frontend; this tree can be
; tested with no CST, which is what makes ast/ verifiable in the same wave it
; is written rather than the next one.
;
; THE FORMAT
;
;	astv 1 NNODES NDECLS NEXTRA NTYPES ROOT N0 N1 N2 N3
;	x I VALUE
;	t I KIND SIGN ORDER PLACE WIDTH A B
;	d I KIND FLAGS AUX NAME NODE TY PARENT FILE START LEN
;	n I KIND AUX TY A B C D FILE START LEN
;
; Every number is unsigned decimal. `KIND` is a NAME from ast/kinds.inc's
; pools, never an integer: renumbering that file must not silently
; reinterpret a dump someone wrote by hand (the same reason ssa-ir.md 2.11
; prints opcodes and not opcode numbers). `I` is the record's own id and is
; always the line's position in its section, so a dump that lost or gained a
; line fails to load rather than loading something subtly different.
;
; SECTION ORDER IS FORCED, not stylistic: `x` then `t` then `d` then `n`.
; ast/build.inc's constructors range-check what they are handed -- a list
; offset against `Ast.extra`'s length, a declaration slot against
; `Ast.decls`' -- so a loader that read nodes first would be handing
; `ast_node` references to things that do not exist yet. Definitions before
; uses. The one cycle, `AstDecl.node` pointing forward at a node, is closed
; by ast/load.inc after both sections are in.
;
; `NEXTRA` COUNTS LIVE ENTRIES, not `Ast.extra`'s length: index 0 is the dead
; sentinel `ast_init` pushes so that a list offset of 0 can mean "no list",
; and it is not dumped.
;
; This file writes through diag/render.inc's `DiagOut` rather than a buffer of
; its own -- the same choice cst/red.inc made and for the same reason: it is
; already the bounded, all-or-nothing byte sink this tree uses everywhere
; else, and a second one would be a second truncation policy to get wrong.
; Nothing here is a diagnostic and no `EXS-E` code is involved.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3 (nothing printed here depends
; on an address or a hash: every number is an index).
; -----------------------------------------------------------------------------

include 'verify.inc'

ast_dump_magic:
	db 'astv'
AST_DUMP_MAGIC_LEN = $ - ast_dump_magic
AST_DUMP_VERSION = 1

; ---- one field -------------------------------------------------------------
; A leading space then the value. Every field on every line is written this
; way, so the separator rule is in one place and a line is "tag, fields,
; newline" with no special case for the first one.
proc __ast_d_num, wr, value
	uses	rbx
	locals
	endl
	mov	rbx, [wr]
	mov	rdi, rbx
	mov	rsi, 32			; ' '
	mov	rdx, 1
	call	diag_out_fill
	mov	rdi, rbx
	mov	rsi, [value]
	call	diag_out_u32
	return
endp

proc __ast_d_name, wr, pool, n
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [wr]
	mov	rdi, [pool]
	mov	rsi, [n]
	call	ast_name_at
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, rbx
	mov	rsi, 32
	mov	rdx, 1
	call	diag_out_fill
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	diag_out_bytes
	return
endp

proc __ast_d_tag, wr, ch
	uses	rbx
	locals
	endl
	mov	rbx, [wr]
	mov	rdi, rbx
	mov	rsi, [ch]
	mov	rdx, 1
	call	diag_out_fill
	return
endp

proc __ast_d_nl, wr
	locals
	endl
	mov	rdi, [wr]
	mov	rsi, 10
	mov	rdx, 1
	call	diag_out_fill
	return
endp

; ---- the whole tree --------------------------------------------------------
proc ast_dump, tree, wr
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [tree]
	mov	r14, [wr]

	mov	rdi, r14
	lea	rsi, [ast_dump_magic]
	mov	rdx, AST_DUMP_MAGIC_LEN
	call	diag_out_bytes
	mov	rdi, r14
	mov	rsi, AST_DUMP_VERSION
	call	__ast_d_num
	mov	rdi, r14
	mov	rsi, [rbx + Ast.nodes.len]
	call	__ast_d_num
	mov	rdi, r14
	mov	rsi, [rbx + Ast.decls.len]
	call	__ast_d_num
	mov	rdi, r14
	mov	rsi, [rbx + Ast.extra.len]
	dec	rsi			; the sentinel at index 0 is not dumped
	call	__ast_d_num
	mov	rdi, r14
	mov	rsi, [rbx + Ast.types.len]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [rbx + Ast.root]
	call	__ast_d_num
	xor	r12, r12
  .num_axis:
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_numeri_get
	mov	rdi, r14
	mov	rsi, rax
	call	__ast_d_num
	inc	r12
	cmp	r12, 4
	jl	.num_axis
	mov	rdi, r14
	call	__ast_d_nl

	; ---- x: the extra pool, live entries only ----
	mov	r12, 1
  .extra:
	mov	rax, [rbx + Ast.extra.len]
	cmp	r12, rax
	jge	.extra_done
	mov	rdi, r14
	mov	rsi, 120		; 'x'
	call	__ast_d_tag
	mov	rdi, r14
	mov	rsi, r12
	call	__ast_d_num
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_extra_at
	mov	rdi, r14
	mov	rsi, rax
	call	__ast_d_num
	mov	rdi, r14
	call	__ast_d_nl
	inc	r12
	jmp	.extra
  .extra_done:

	; ---- t: the interned type table ----
	mov	r12, 1
  .types:
	mov	rax, [rbx + Ast.types.len]
	cmp	r12, rax
	jg	.types_done
	mov	rdi, r14
	mov	rsi, 116		; 't'
	call	__ast_d_tag
	mov	rdi, r14
	mov	rsi, r12
	call	__ast_d_num
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_type_at
	mov	r13, rax
	mov	rdi, r14
	lea	rsi, [ast_tkind_names]
	movzx	edx, byte [r13 + AstType.kind]
	call	__ast_d_name
	mov	rdi, r14
	movzx	esi, byte [r13 + AstType.sign]
	call	__ast_d_num
	mov	rdi, r14
	movzx	esi, byte [r13 + AstType.order]
	call	__ast_d_num
	mov	rdi, r14
	movzx	esi, byte [r13 + AstType.place]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstType.width]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstType.a]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstType.b]
	call	__ast_d_num
	mov	rdi, r14
	call	__ast_d_nl
	inc	r12
	jmp	.types
  .types_done:

	; ---- d: declarations ----
	mov	r12, 1
  .decls:
	mov	rax, [rbx + Ast.decls.len]
	cmp	r12, rax
	jg	.decls_done
	mov	rdi, r14
	mov	rsi, 100		; 'd'
	call	__ast_d_tag
	mov	rdi, r14
	mov	rsi, r12
	call	__ast_d_num
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_decl_at
	mov	r13, rax
	mov	rdi, r14
	lea	rsi, [ast_dkind_names]
	movzx	edx, byte [r13 + AstDecl.kind]
	call	__ast_d_name
	mov	rdi, r14
	movzx	esi, byte [r13 + AstDecl.flags]
	call	__ast_d_num
	mov	rdi, r14
	movzx	esi, word [r13 + AstDecl.aux]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstDecl.name]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstDecl.node]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstDecl.ty]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstDecl.parent]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstDecl.span.file_id]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstDecl.span.start]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstDecl.span.len]
	call	__ast_d_num
	mov	rdi, r14
	call	__ast_d_nl
	inc	r12
	jmp	.decls
  .decls_done:

	; ---- n: nodes, in postorder, which is index order ----
	mov	r12, 1
  .nodes:
	mov	rax, [rbx + Ast.nodes.len]
	cmp	r12, rax
	jg	.nodes_done
	mov	rdi, r14
	mov	rsi, 110		; 'n'
	call	__ast_d_tag
	mov	rdi, r14
	mov	rsi, r12
	call	__ast_d_num
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	mov	r13, rax
	mov	rdi, r14
	lea	rsi, [ast_kind_names]
	movzx	edx, word [r13 + AstNode.kind]
	call	__ast_d_name
	mov	rdi, r14
	movzx	esi, word [r13 + AstNode.aux]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstNode.ty]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstNode.a]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstNode.b]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstNode.c]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstNode.d]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstNode.span.file_id]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstNode.span.start]
	call	__ast_d_num
	mov	rdi, r14
	mov	esi, [r13 + AstNode.span.len]
	call	__ast_d_num
	mov	rdi, r14
	call	__ast_d_nl
	inc	r12
	jmp	.nodes
  .nodes_done:
	return
endp
