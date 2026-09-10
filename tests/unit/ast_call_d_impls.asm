; tests/unit/ast_call_d_impls.asm
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
; ast fixture for `Call.d`, the slot Stage 2 fills with the impls a generic
; call chose (docs/design/checker.md sections 2.5 and 2.7, finding 4).
;
; `docs/design/typed-ast.md` section 2.3 leaves `Call`'s `d` column blank, so
; ast/kinds.inc gave it AST_R_NONE -- "unused, must be 0 in a well-formed
; node" -- and `__ast_slot_check` (ast/build.inc) `rassert`ed exactly that at
; every constructor call. checker.md section 2.5 then requires the slot: after
; Stage 2 it holds "the `extra` offset of the impl decls chosen for each
; generic argument, in generic-parameter order", which is what spec §15 item
; 5's dictionary layout is built from. Under the old role a checker writing
; that offset would have trapped, in the builder, with a message about a slot
; that must be zero.
;
; So the role is now AST_R_ANY, and this fixture is the difference: it writes
; a real `extra` offset into `Call.d` through the ordinary API and requires
; both the constructor and `ast_verify` to accept it. Revert the role in
; ast/kinds.inc and check 1 traps (exit 132) at `ast_node_cd`.
;
; AST_R_ANY rather than AST_R_LIST, deliberately: a LIST slot names the NEXT
; slot as its element count, and `Call`'s next slot is already the ARGUMENT
; count. The impl count is the callee's generic-parameter count, which lives
; in the callee's `Generics`, so no per-node pairing the verifier could check
; exists (ast/kinds.inc's note on the table).
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

	; ---- check 0: the role table says so ----
	mov	rdi, AST_CALL
	mov	rsi, 3
	call	ast_role_of
	cmp	rax, AST_R_ANY
	jne	.fail0

	; Two implementation declarations, and an `extra` run naming them --
	; the shape checker.md section 2.7 records per generic call.
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_IMPL
	xor	rdx, rdx
	mov	rcx, [fx_nm]
	xor	r8, r8
	lea	r9, [fx_sp]
	call	ast_decl
	mov	r13, rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_IMPL
	xor	rdx, rdx
	mov	rcx, [fx_nm]
	xor	r8, r8
	lea	r9, [fx_sp]
	call	ast_decl
	mov	r14, rax
	lea	rdi, [fx_tree]
	mov	rsi, r13
	call	ast_extra_push
	mov	r12, rax		; the offset the checker would record
	lea	rdi, [fx_tree]
	mov	rsi, r14
	call	ast_extra_push

	; A callee expression -- any node will do here; `Call.a` is checked as
	; a node id, not as something callable (that is a type rule, and types
	; are Stage 2's).
	lea	rdi, [fx_tree]
	mov	rsi, AST_LIT
	mov	rdx, AST_LIT_INT
	mov	rcx, [fx_nm]
	xor	r8, r8
	lea	r9, [fx_sp]
	call	ast_node
	mov	r13, rax

	lea	rdi, [fx_tree]
	mov	rsi, AST_CALL
	xor	rdx, rdx
	mov	rcx, r13		; callee
	xor	r8, r8			; no arguments
	lea	r9, [fx_sp]
	call	ast_node
	mov	r14, rax

	; ---- check 1 ----
	; Under AST_R_NONE this call traps inside `__ast_slot_check`.
	lea	rdi, [fx_tree]
	mov	rsi, r14
	xor	rdx, rdx		; argument count 0
	mov	rcx, r12		; the impl list's `extra` offset
	call	ast_node_cd

	; ---- check 2 ----
	lea	rdi, [fx_tree]
	mov	rsi, r14
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	cmp	rcx, r12
	jne	.fail2

	; ---- check 3 ----
	lea	rdi, [fx_tree]
	call	ast_verify

	xor	edi, edi
	call	sys_exit_group
  .setup:
	mov	rdi, 99
	call	sys_exit_group
  .fail0:
	mov	rdi, 10
	call	sys_exit_group
  .fail2:
	mov	rdi, 12
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_t_p:	db 'p'
  fx_nm:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_sp:	rb sizeof.Span
