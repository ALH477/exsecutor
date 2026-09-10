; tests/unit/ast_verify_postorder.asm
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
; ast NEGATIVE fixture: compiler/x86_64/ast/verify.inc, postorder.
;
; The companion to tests/unit/ast_build_postorder.asm: that one proves the
; CONSTRUCTOR rejects a forward child, this one proves the VERIFIER does. Both
; are needed, because a tree can be corrupted after it is built -- by a Stage 2
; annotation pass writing the wrong slot, for instance -- and the constructor
; is long gone by then.
;
; Two well-formed `Redde` nodes are built, and then node 1's operand is poked
; to name node 2: a child with a LARGER index than its parent, which no
; postorder walk can produce. `ast_verify` must trap.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 1024 * 1024
	call	arena_init
	jc	.setup
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 64
	call	intern_init
	lea	rdi, [fx_names]
	lea	rsi, [fx_t_p]
	mov	rdx, 1
	call	intern_id
	mov	[fx_nm], rax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_sp]
	mov	rsi, 7
	mov	rdx, 0
	mov	rcx, 1
	call	span_make
	mov	rdi, AST_REDDE
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_node
	mov	rdi, AST_REDDE
	xor	rsi, rsi
	mov	rdx, 1
	xor	rcx, rcx
	call	fx_node

	; The corruption. A valid tree, then one wrong word.
	lea	rdi, [fx_tree]
	mov	rsi, 1
	call	ast_node_at
	mov	dword [rax + AstNode.a], 2

	lea	rdi, [fx_tree]
	call	ast_verify
	; Reached only if the check under test did NOT fire. A negative
	; fixture that exits cleanly proves nothing, so this is a FAILURE
	; distinct from the expected trap (exit 132).
	mov	rdi, 20
	call	sys_exit_group
  .setup:
	mov	rdi, 10
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

proc fx_node, kind, aux, a, b
	locals
	endl
	lea	rdi, [fx_tree]
	mov	rsi, [kind]
	mov	rdx, [aux]
	mov	rcx, [a]
	mov	r8, [b]
	lea	r9, [fx_sp]
	call	ast_node
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
