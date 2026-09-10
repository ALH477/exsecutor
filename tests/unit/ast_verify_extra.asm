; tests/unit/ast_verify_extra.asm
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
; ast NEGATIVE fixture: compiler/x86_64/ast/verify.inc, `extra` ranges.
;
; Lists are ranges in one append-only pool: `a` is the offset and `b` the count
; (docs/design/typed-ast.md section 2.1). An offset that points past the end of
; the pool reads whatever the arena has there and produces child ids out of
; thin air, which is exactly the failure mode indices-instead-of-pointers is
; supposed to make impossible -- so it has to be checked, not assumed.
;
; A `Module` with one real item is built, then its list offset is poked to 99
; with the count left at 1. `ast_verify` must trap on `off + count` exceeding
; `Ast.extra`'s length.
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
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	r12, rax
	lea	rdi, [fx_tree]
	mov	rsi, 1
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, r12
	call	ast_list_emit
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, AST_MODULE
	xor	rsi, rsi
	mov	rdx, r12
	mov	rcx, r13
	call	fx_node

	lea	rdi, [fx_tree]
	mov	rsi, 2
	call	ast_node_at
	mov	dword [rax + AstNode.a], 99	; a pool with two words in it

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

include '../../compiler/x86_64/diag/diag.inc'
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

segment readable writeable
  fx_t_p:	db 'p'
  fx_nm:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_sp:	rb sizeof.Span
