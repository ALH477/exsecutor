; tests/unit/ast_verify_decl.asm
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
; ast NEGATIVE fixture: compiler/x86_64/ast/verify.inc, declaration back-links.
;
; docs/design/typed-ast.md section 3: "every `Decl.node` points at a node whose
; `d` is that decl". The two directions are written at different times -- a
; node's `d` when the node is built, a declaration's `node` afterwards, because
; in postorder the introducing node does not exist when the name is seen (see
; ast/build.inc) -- so nothing but this check keeps them in agreement.
;
; No poking is needed: `ast_decl_node` deliberately does not verify the reverse
; link (it would have to walk the role table on every call), so pointing a
; declaration at the wrong node is reachable through the ordinary API. Two
; `Contrahe` nodes are built, only the first declaring anything, and the
; declaration is pointed at the second. `ast_verify` must trap.
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
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_ACCUM
	xor	rdx, rdx
	mov	rcx, [fx_nm]
	xor	r8, r8
	lea	r9, [fx_sp]
	call	ast_decl

	mov	rdi, AST_CONTRAHE
	xor	rsi, rsi
	mov	rdx, [fx_nm]
	xor	rcx, rcx
	call	fx_node
	lea	rdi, [fx_tree]
	mov	rsi, 1
	xor	rdx, rdx
	mov	rcx, 1			; node 1 declares declaration 1
	call	ast_node_cd
	mov	rdi, AST_CONTRAHE
	xor	rsi, rsi
	mov	rdx, [fx_nm]
	xor	rcx, rcx
	call	fx_node			; node 2 declares nothing

	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, 2			; ... but the declaration says node 2
	call	ast_decl_node

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
