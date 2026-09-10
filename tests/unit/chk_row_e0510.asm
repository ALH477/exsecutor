; tests/unit/chk_row_e0510.asm
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
; checker-rows fixture for pass 3's §4.4 ceiling: `EXS-E0510`.
;
; SPEC §14 ENTRY 24, and it is the entry spec §4.4 says was found by
; MEASUREMENT rather than by design: "until `prototypes/gendict/`, the
; exceeds-check was stated ONLY for `dyn` construction -- so an implementation
; reached solely through a generic `<T: Trait>` was never checked against
; anything." The fix is to check the mark against the trait's declared ceiling
; at every `interfacies X in T poscit R` DECLARATION, which closes the generic
; path and the `dyn`-laundering path at once.
;
;     interfacies Summable { functio combine(...) -> f32 poscit alloc }
;     interfacies Summable in Right poscit alloc, rete { ... }
;     ... e sicut dyn Summable poscit {alloc}
;
;   1. `gendict/cases/bad_static_impl_exceeds_member_ceiling.exsc`: the head's
;      mark `{alloc, rete}` exceeds the ceiling `{alloc}`, so the DECLARATION
;      is rejected -- with no `dyn` cast and no instantiation anywhere -- and
;      the same mark also fails the `dyn` bound at the cast. Two `EXS-E0510`s,
;      in NODE ORDER (checker.md section 4), each on its own node: the impl
;      head, then the cast.
;   2. `gendict/cases/ok_static_matches_ceiling.exsc`: one edit -- the head's
;      row is `{alloc}` -- and every one of them goes away. Non-vacuity: the
;      check is not rejecting every implementation it sees.
;   3. A member-level `poscit` is bounded by the trait member's, separately
;      from the head's (`bad_impl_exceeds_own_mark.exsc`'s neighbour): with a
;      conforming head, a method writing `poscit rete` is still `EXS-E0510`,
;      at the METHOD.
;   4. `bad_dyn_launder_via_generic.exsc`: when the cast's operand is a
;      GENERIC PARAMETER bound to the interface, nothing is instantiated, so
;      the CEILING is what must fit inside the `dyn` bound -- rejected at the
;      cast, with no instantiation needed (spec §4.4).
;
; `[UNTESTED]` on real source: pass 3 reads `Node.ty`, which the types pass
; fills, and `Path.d`, which the resolve pass fills. This fixture fills them
; by hand -- checker.md section 6's arrangement.
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
	lea	rdi, [fx_scr]
	mov	rsi, 2 * 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_iar]
	mov	rsi, 256 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_iar]
	mov	rdx, 64
	call	intern_init
	call	fx_build

	; ---- 1: the mark exceeds the ceiling ----
	call	fx_run
	cmp	qword [fx_ndiag], 2
	jne	.fail1
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 510
	jne	.fail1
	cmp	rdx, 3131			; the impl HEAD
	jne	.fail1
	mov	rsi, 1
	call	fx_diag
	cmp	rax, 510
	jne	.fail1
	cmp	rdx, 5150			; the `sicut dyn` CAST
	jne	.fail1

	; ---- 2: one edit -- `poscit alloc` -- and it is accepted ----
	lea	rdi, [fx_tree]
	mov	rsi, [fx_irow]
	mov	rdx, 0
	mov	rcx, 1				; drop `rete` from the head row
	call	fx_setab
	call	fx_run
	test	rax, rax
	jnz	.fail2

	; ---- 3: a member-level `poscit` is bounded too ----
	lea	rdi, [fx_tree]
	mov	rsi, [fx_msig]
	call	ast_node_at
	mov	rcx, [fx_mrow]
	mov	[rax + AstNode.d], ecx		; the method writes `poscit rete`
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail3
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 510
	jne	.fail3
	cmp	rdx, 7070			; the METHOD, not the head
	jne	.fail3
	lea	rdi, [fx_tree]
	mov	rsi, [fx_msig]
	call	ast_node_at
	mov	dword [rax + AstNode.d], 0

	; ---- 4: a generic operand is bounded by the CEILING ----
	; Nothing is instantiated, so the impl is never consulted: `dyn
	; Summable poscit {}` cannot hold a `T: Summable` whose ceiling is
	; `{alloc}`.
	lea	rdi, [fx_tree]
	mov	rsi, [fx_expr]
	mov	rdx, [fx_ty_param]
	call	ast_node_ty
	lea	rdi, [fx_tree]
	mov	rsi, [fx_drow]
	mov	rdx, 0
	xor	rcx, rcx			; `poscit {}` at the cast
	call	fx_setab
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail4
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 510
	jne	.fail4
	cmp	rdx, 5150
	jne	.fail4

	; ---- 5: the mark includes the TARGET TYPE's bearing (spec §4.3) ----
	; Restore the accepting state of check 2, then give `Right` a `rete`
	; field. Its head still says `poscit alloc` and its methods still say
	; nothing -- the authority is in the type -- and that is exactly what
	; §4.4's "mark" has to include or a capability-bearing implementation
	; walks past the ceiling untouched.
	lea	rdi, [fx_tree]
	mov	rsi, [fx_expr]
	mov	rdx, [fx_ty_right]
	call	ast_node_ty
	lea	rdi, [fx_tree]
	mov	rsi, [fx_drow]
	mov	rdx, 0
	mov	rcx, 1
	call	fx_setab
	call	fx_run
	test	rax, rax			; still accepted, as in check 2
	jnz	.fail5
	lea	rdi, [fx_tree]
	mov	rsi, 6
	mov	rdx, [fx_ty_rete]
	call	ast_decl_ty
	call	fx_run
	cmp	qword [fx_ndiag], 2
	jne	.fail5
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 510
	jne	.fail5
	cmp	rdx, 3131
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

; ---------------------------------------------------------------------------
  fx_run:
	push	rbp
	mov	qword [fx_ndiag], 0
	lea	rdi, [fx_scr]
	call	arena_reset
	lea	rdi, [fx_dvec]
	lea	rsi, [fx_scr]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_ctx]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_dvec]
	lea	rcx, [fx_scr]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_ctx]
	call	chk_rows
	mov	[fx_ndiag], rax
	pop	rbp
	ret

  fx_diag:
	push	rbp
	lea	rdi, [fx_ctx]
	add	rdi, ChkCtx.dbuf
	call	vec_get
	mov	edx, [rax + ChkDiag.caret.start]
	mov	eax, [rax + ChkDiag.code]
	pop	rbp
	ret

  fx_node:
	push	rbp
	lea	rdi, [fx_tree]
	lea	r9, [fx_span]
	call	ast_node
	pop	rbp
	ret

  fx_setcd:
	push	rbp
	lea	rdi, [fx_tree]
	call	ast_node_cd
	pop	rbp
	ret

; rsi = node, rdx = a (0 keeps it), rcx = b.
  fx_setab:
	push	rbp
	mov	[fx_t5], rdx
	mov	[fx_t6], rcx
	lea	rdi, [fx_tree]
	call	ast_node_at
	mov	rdx, [fx_t5]
	test	rdx, rdx
	jz	.keep_a
	mov	[rax + AstNode.a], edx
  .keep_a:
	mov	rcx, [fx_t6]
	mov	[rax + AstNode.b], ecx
	pop	rbp
	ret

; A `Path` node with no segments resolving to declaration rcx.
  fx_path:
	push	rbp
	mov	[fx_p1], rcx
	mov	rsi, AST_PATH
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_p2], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_p1]
	call	fx_setcd
	mov	rax, [fx_p2]
	pop	rbp
	ret

; A `RowItem` naming the atom of §4.6 ordinal rcx, staged.
  fx_ri_atom:
	push	rbp
	mov	rax, [fx_base]
	add	rax, rcx
	dec	rax
	mov	[fx_r1], rax
	mov	rcx, rax
	call	fx_path
	mov	rsi, AST_ROWITEM
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	[fx_r2], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_r1]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_r2]
	call	ast_list_push
	pop	rbp
	ret

; A `Row` node holding the atoms whose §4.6 ordinals are in [fx_ords], count
; rcx.  -> rax = the node id
  fx_row:
	push	rbp
	mov	[fx_r3], rcx
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_r4], rax
	xor	r10, r10
  .item:
	mov	rax, [fx_r3]
	cmp	r10, rax
	jge	.emit
	mov	[fx_r5], r10
	lea	rax, [fx_ords]
	mov	rcx, [fx_r5]
	mov	rcx, [rax + rcx*8]
	call	fx_ri_atom
	mov	r10, [fx_r5]
	inc	r10
	jmp	.item
  .emit:
	lea	rdi, [fx_tree]
	mov	rsi, [fx_r4]
	call	ast_list_emit
	mov	rsi, AST_ROW
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, [fx_r3]
	call	fx_node
	pop	rbp
	ret

; A `Fn` node with signature row `rcx` (a `Row` node or 0) for declaration
; `[fx_fd]`, span `[fx_fs]`.  -> rax = the node id
  fx_fn:
	push	rbp
	mov	[fx_f1], rcx
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_f2], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_f1]
	call	fx_setcd
	mov	rax, [fx_fs]
	mov	[fx_span + Span.start], eax
	mov	rsi, AST_FN
	xor	rdx, rdx
	mov	rcx, [fx_f2]
	xor	r8, r8
	call	fx_node
	mov	[fx_f3], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_fd]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_fd]
	mov	rdx, [fx_f3]
	call	ast_decl_node
	mov	rax, [fx_f3]
	pop	rbp
	ret

; ---------------------------------------------------------------------------
  fx_build:
	push	rbp
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_span]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	; ---- declarations ----
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_IFACE
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 1 Summable
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_MEMBER
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, 1
	lea	r9, [fx_span]
	call	ast_decl			; 2 Summable.combine
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_STRUCT
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 3 Right
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_IMPL
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 4 the implementation
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_MEMBER
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, 4
	lea	r9, [fx_span]
	call	ast_decl			; 5 the impl's combine
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FIELD
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, 3
	lea	r9, [fx_span]
	call	ast_decl			; 6 Right's one field
	lea	rdi, [fx_tree]
	call	ast_cap_push
	mov	[fx_base], rax

	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_STRUCT
	mov	rdx, 3
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_right], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_PARAM
	mov	rdx, 3
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_param], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 32
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fx_ty_u32], rax
	mov	rax, [fx_base]
	add	rax, AST_CAP_RETE - 1
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_CAP
	mov	rdx, rax
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_rete], rax
	lea	rdi, [fx_tree]
	mov	rsi, 6
	mov	rdx, [fx_ty_u32]
	call	ast_decl_ty

	; ---- the trait member: `poscit alloc` ----
	mov	qword [fx_ords], AST_CAP_ALLOC
	mov	rcx, 1
	call	fx_row
	mov	qword [fx_fd], 2
	mov	qword [fx_fs], 0
	mov	rcx, rax
	call	fx_fn
	mov	[fx_t1], rax

	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t2], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t1]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t2]
	call	ast_list_emit
	mov	rsi, AST_IFACE
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 1
	call	fx_node
	mov	[fx_t3], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, [fx_t3]
	call	ast_decl_node

	; ---- `Right`, with one field whose type check 5 flips ----
	xor	rcx, rcx
	call	fx_path
	mov	rsi, AST_TYPATH
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	rsi, AST_FIELD
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, rax
	call	fx_node
	mov	[fx_t1], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 6
	call	fx_setcd
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t2], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t1]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t2]
	call	ast_list_emit
	mov	rsi, AST_STRUCT
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 1
	call	fx_node
	mov	[fx_t3], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 3
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 3
	mov	rdx, [fx_t3]
	call	ast_decl_node

	; ---- the implementation head: `poscit alloc, rete` ----
	mov	rcx, 1
	call	fx_path				; the interface `Path`
	mov	[fx_t1], rax
	mov	rcx, 3
	call	fx_path
	mov	rsi, AST_TYPATH
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	[fx_t2], rax			; the target TYPE node
	lea	rdi, [fx_tree]
	mov	rsi, rax
	mov	rdx, [fx_ty_right]
	call	ast_node_ty
	mov	qword [fx_ords], AST_CAP_ALLOC
	mov	qword [fx_ords + 8], AST_CAP_RETE
	mov	rcx, 2
	call	fx_row
	mov	[fx_irow], rax
	mov	dword [fx_span + Span.start], 3131
	mov	rsi, AST_IMPLHEAD
	xor	rdx, rdx
	mov	rcx, [fx_t1]
	mov	r8, [fx_t2]
	call	fx_node
	mov	[fx_t3], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, rax
	mov	rdx, [fx_irow]
	xor	rcx, rcx
	call	fx_setcd

	; ---- the impl's method, and the row check 3 gives it ----
	mov	qword [fx_fd], 5
	mov	qword [fx_fs], 7070
	xor	rcx, rcx
	call	fx_fn
	mov	[fx_t4], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_node_at
	mov	ecx, [rax + AstNode.a]
	mov	[fx_msig], rcx
	mov	qword [fx_ords], AST_CAP_RETE
	mov	rcx, 1
	call	fx_row
	mov	[fx_mrow], rax

	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t4]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_IMPL
	xor	rdx, rdx
	mov	rcx, [fx_t3]
	mov	r8, rax
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	mov	rdx, 1
	mov	rcx, 4
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 4
	mov	rdx, [fx_t6]
	call	ast_decl_node

	; ---- `e sicut dyn Summable poscit {alloc}` ----
	xor	rcx, rcx
	call	fx_path
	mov	[fx_expr], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	mov	rdx, [fx_ty_right]
	call	ast_node_ty
	mov	rcx, 1
	call	fx_path
	mov	[fx_t1], rax
	mov	qword [fx_ords], AST_CAP_ALLOC
	mov	rcx, 1
	call	fx_row
	mov	[fx_drow], rax
	mov	rsi, AST_TYDYN
	xor	rdx, rdx
	mov	rcx, [fx_t1]
	mov	r8, rax
	call	fx_node
	mov	[fx_t2], rax
	mov	dword [fx_span + Span.start], 5150
	mov	rsi, AST_CAST
	xor	rdx, rdx
	mov	rcx, [fx_expr]
	mov	r8, [fx_t2]
	call	fx_node
	mov	dword [fx_span + Span.start], 0
	pop	rbp
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/rows/rows.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_arena:	rb sizeof.Arena
  fx_scr:	rb sizeof.Arena
  fx_iar:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_ctx:	rb sizeof.ChkCtx
  fx_dvec:	rb sizeof.Vec
  fx_span:	rb sizeof.Span
  fx_ords:	rq 8
  fx_ndiag:	rq 1
  fx_base:	rq 1
  fx_irow:	rq 1
  fx_mrow:	rq 1
  fx_drow:	rq 1
  fx_msig:	rq 1
  fx_expr:	rq 1
  fx_ty_right:	rq 1
  fx_ty_param:	rq 1
  fx_ty_u32:	rq 1
  fx_ty_rete:	rq 1
  fx_fd:	rq 1
  fx_fs:	rq 1
  fx_f1:	rq 1
  fx_f2:	rq 1
  fx_f3:	rq 1
  fx_p1:	rq 1
  fx_p2:	rq 1
  fx_r1:	rq 1
  fx_r2:	rq 1
  fx_r3:	rq 1
  fx_r4:	rq 1
  fx_r5:	rq 1
  fx_t1:	rq 1
  fx_t2:	rq 1
  fx_t3:	rq 1
  fx_t4:	rq 1
  fx_t5:	rq 1
  fx_t6:	rq 1
