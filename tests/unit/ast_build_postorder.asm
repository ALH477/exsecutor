; tests/unit/ast_build_postorder.asm
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
; ast NEGATIVE fixture: compiler/x86_64/ast/build.inc's constructor check.
;
; The AST is POSTORDER -- a child's index is strictly less than its parent's
; (docs/design/typed-ast.md section 2.1). `ast_verify` can say a finished tree
; violates that; only the CONSTRUCTOR can say which call made it so, which is
; why `ast_node` range-checks the operands it is handed against ast/kinds.inc's
; role table at the moment it is handed them.
;
; Here the very first node names child 5 in a tree with no nodes at all. The
; expected outcome is `rassert`'s trap inside `__ast_slot_check`, reached from
; `ast_node`, NOT a clean exit and NOT a diagnostic: a malformed tree is a
; compiler bug and docs/asm-conventions.md 1.3 forbids routing one through
; `CF` where a caller could swallow it.
;
; Not executed for its output -- it dies. `expect-exit=132` is SIGILL (128+4),
; `rassert`'s `ud2`; see tests/unit/rassert_trap.asm.
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
	mov	rdx, 5			; child 5 -- of a tree with no nodes
	xor	rcx, rcx
	call	fx_node
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
