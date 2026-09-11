; compiler/x86_64/checker/types/stmt.inc
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
; PASS 2, PART 4 -- STATEMENTS. Included by types.inc; a section of that file,
; split only for length.
;
; WHAT EACH STATEMENT OWES, and the §13 code when it does not pay:
;
;   Block      nothing; it types its own statements in source order, which is
;              also definite-assignment order.
;   Binding    neither annotation nor initializer -> `E0309`; both, unequal ->
;              `E0303`; an initializer that is a bare literal with no
;              annotation -> `E0308`.
;   Assign     target not an lvalue, or `firma`, or a parameter -> `E0306`;
;              value not the target's type -> `E0303`.
;   Redde      against the enclosing function's result -> `E0303`; and it is
;              what clears `E0307`'s fall-off check.
;   Rumpe      outside a loop -> `E0307`; inside a `contrahe` iteration ->
;   Perge      `E0342` (spec §5.4). `perge` outside a loop -> `E0307`.
;   Si Dum     the condition is `u1` -> `E0303`.
;   For        the iterable is a range or an `acies` -> `E0305`; the `forma`
;              shape and its width -> `E0343` (spec §5.4, §8.6).
;   Contrahe   the operator must be ASSOCIATIVE -> `E0343` (spec §5.4).
;   Sub        the provider's type must be the atom's `cap` type -> `E0303`.
;
; `EXS-E0342` IS ON `rumpe` AND NOT ON `perge`, because spec §13 says exactly
; that ("`rumpe` inside an iteration carrying a `contrahe`") and §5.4's reason
; -- a reduction that exits early has no defined value -- does not apply to
; `perge`, which completes the iteration.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1, §4.5, §5.4, §8.5, §8.6, §13.
; -----------------------------------------------------------------------------

; __chk_ty_defsetup(w) -- seed the definite-assignment state.
;
; A binding declared WITH an initializer, and everything that is not a
; binding, starts assigned; a binding with an annotation and no initializer
; starts unassigned. Spec §13: "a binding declared without an initializer and
; read on some path before any assignment".
proc __chk_ty_defsetup, chkt_wDS
	uses	rbx, r12, r13
	locals
		slot chkt_nDS, dq
	endl
	mov	rbx, [chkt_wDS]
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_decl_count
	mov	[chkt_nDS], rax
	mov	r12, 1
  .decl:
	cmp	r12, [chkt_nDS]
	ja	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_BINDING
	jne	.next
	mov	r13d, [rax + AstDecl.node]
	test	r13, r13
	jz	.next
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]		; `Binding.b` = the initializer
	test	ecx, ecx
	jnz	.next
	lea	rdi, [rbx + ChkTyWork.wdef]
	mov	rsi, r12
	call	vec_get
	mov	byte [rax], CHK_TY_D_UNSET
  .next:
	inc	r12
	jmp	.decl
  .out:
	return
endp

; __chk_ty_stmt(w, n) -- type one statement. Every kind that is not a
; statement falls through to `__chk_ty_expr`, which is what makes an
; expression used as a statement (and a statement used as an expression) one
; code path rather than two.
proc __chk_ty_stmt, chkt_wST, chkt_nST
	uses	rbx, r12, r13, r14
	locals
		slot chkt_kST, dq
		slot chkt_aST, dq
		slot chkt_bST, dq
		slot chkt_cST, dq
		slot chkt_dST, dq
		slot chkt_xST, dq
		slot chkt_iST, dq
	endl
	mov	rbx, [chkt_wST]
	mov	r12, [chkt_nST]
	test	r12, r12
	jz	.out
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.kind]
	mov	[chkt_kST], rcx
	movzx	ecx, word [rax + AstNode.aux]
	mov	[chkt_xST], rcx
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_aST], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bST], rcx
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_cST], rcx
	mov	ecx, [rax + AstNode.d]
	mov	[chkt_dST], rcx

	mov	rcx, [chkt_kST]
	cmp	rcx, AST_BLOCK
	je	.block
	cmp	rcx, AST_BINDING
	je	.binding
	cmp	rcx, AST_ASSIGN
	je	.assign
	cmp	rcx, AST_REDDE
	je	.redde
	cmp	rcx, AST_RUMPE
	je	.rumpe
	cmp	rcx, AST_PERGE
	je	.perge
	cmp	rcx, AST_EXPRSTMT
	je	.exprstmt
	cmp	rcx, AST_SI
	je	.si
	cmp	rcx, AST_DUM
	je	.dum
	cmp	rcx, AST_FOR
	je	.for
	cmp	rcx, AST_SUB
	je	.sub
	cmp	rcx, AST_DISCERNE
	je	.discerne
	cmp	rcx, AST_ERROR
	je	.unitty
	cmp	rcx, AST_FN
	je	.item
	cmp	rcx, AST_STRUCT
	je	.item
	cmp	rcx, AST_IFACE
	je	.item
	cmp	rcx, AST_IMPL
	je	.item
	cmp	rcx, AST_TYPUS
	je	.item
	cmp	rcx, AST_EXTERNUS
	je	.item
	cmp	rcx, AST_POTESTAS
	je	.item
	; an expression in statement position
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r12
	call	__chk_ty_settled
	jmp	.out

  .unitty:
	mov	rdi, rbx
	call	__chk_ty_err
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rax
	call	__chk_ty_setty
	jmp	.out

  .item:
	; A DECLARATION in statement position. Spec §8.6 has no nested item, but
	; its *Peeks* table makes `functio IDENT` at statement start a
	; diagnostic and `cst/parse.inc` then parses the whole declaration
	; anyway -- so a `Fn` whose declaration's parent is another `Fn` really
	; does land in a block's statement list, with NO `Error` node anywhere
	; in the subtree (tests/diagnostics/c20_nested_named_functio.exsc,
	; c22_nested_functio_plus_missing_semicolons.exsc).
	;
	; NOTHING IS OWED HERE, and returning WITHOUT annotating is the point:
	; `chk_ty_sigs` already interned the signature and wrote `Fn.ty`, and
	; `__chk_ty_bodies` walks `Ast.decls`, which reaches a nested `AST_D_FN`
	; exactly as it reaches a module-level one. Writing `unit` over `Fn.ty`
	; would erase the type the signature loop just built.
	jmp	.out

  ; ---- Block -------------------------------------------------------------
  .block:
	mov	qword [chkt_iST], 0
  .b_stmt:
	mov	rax, [chkt_iST]
	cmp	rax, [chkt_bST]
	jae	.b_done
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_aST]
	add	rsi, [chkt_iST]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_stmt
	inc	qword [chkt_iST]
	jmp	.b_stmt
  .b_done:
	jmp	.unit

  ; ---- Binding -----------------------------------------------------------
  .binding:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_binding
	jmp	.unit

  ; ---- Assign ------------------------------------------------------------
  .assign:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_assign
	jmp	.unit

  ; ---- Redde -------------------------------------------------------------
  .redde:
	mov	dword [rbx + ChkTyWork.wredd], 1
	cmp	dword [rbx + ChkTyWork.winfer], 0
	je	.r_declared
	; a lambda whose result is being inferred: the FIRST `redde` fixes it,
	; and every later one is checked against that (checker.md section 2.3)
	mov	eax, [rbx + ChkTyWork.winf]
	test	eax, eax
	jnz	.r_declared
	mov	rdi, rbx
	mov	rsi, [chkt_aST]
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_aST]
	call	__chk_ty_settled
	cmp	qword [chkt_aST], 0
	jne	.r_infer
	mov	rdi, rbx
	call	__chk_ty_unit
  .r_infer:
	mov	[rbx + ChkTyWork.winf], eax
	mov	[rbx + ChkTyWork.wres], eax
	jmp	.unit
  .r_declared:
	mov	r13d, [rbx + ChkTyWork.wres]
	cmp	qword [chkt_aST], 0
	jne	.r_value
	; `redde;` -- only in a `unit` function (checker.md class C)
	mov	rdi, rbx
	call	__chk_ty_unit
	cmp	rax, r13
	je	.unit
	cmp	r13, 1
	je	.unit
	mov	rdi, rbx
	mov	rsi, 303
	mov	rdx, r12
	call	__chk_ty_raise
	jmp	.unit
  .r_value:
	mov	rdi, rbx
	mov	rsi, [chkt_aST]
	mov	rdx, r13
	call	__chk_ty_want
	jmp	.unit

  ; ---- Rumpe / Perge -----------------------------------------------------
  .rumpe:
	cmp	dword [rbx + ChkTyWork.wloop], 0
	jne	.r_inloop
	mov	rdi, rbx
	mov	rsi, 307
	mov	rdx, r12
	call	__chk_ty_raise
	jmp	.unit
  .r_inloop:
	; spec §5.4 / `EXS-E0342`: a reduction that exits early has no value
	cmp	dword [rbx + ChkTyWork.wcon], 0
	je	.unit
	mov	rdi, rbx
	mov	rsi, 342
	mov	rdx, r12
	call	__chk_ty_raise
	jmp	.unit
  .perge:
	cmp	dword [rbx + ChkTyWork.wloop], 0
	jne	.unit
	mov	rdi, rbx
	mov	rsi, 307
	mov	rdx, r12
	call	__chk_ty_raise
	jmp	.unit

  ; ---- ExprStmt ----------------------------------------------------------
  .exprstmt:
	mov	rdi, rbx
	mov	rsi, [chkt_aST]
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_aST]
	call	__chk_ty_settled
	jmp	.unit

  ; ---- Si ----------------------------------------------------------------
  ; `Si.a`/`Si.b` are (condition, block) PAIRS (AST_R_LIST2); `Si.c` is the
  ; final `aliter` block.
  .si:
	mov	qword [chkt_iST], 0
  .s_arm:
	mov	rax, [chkt_iST]
	cmp	rax, [chkt_bST]
	jae	.s_else
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_aST]
	mov	rcx, [chkt_iST]
	shl	rcx, 1
	add	rsi, rcx
	call	ast_extra_at
	mov	r13, rax
	mov	rdi, rbx
	call	__chk_ty_u1
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, rax
	call	__chk_ty_want
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_aST]
	mov	rcx, [chkt_iST]
	shl	rcx, 1
	add	rsi, rcx
	inc	rsi
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_stmt
	inc	qword [chkt_iST]
	jmp	.s_arm
  .s_else:
	mov	rdi, rbx
	mov	rsi, [chkt_cST]
	call	__chk_ty_stmt
	jmp	.unit

  ; ---- Dum ---------------------------------------------------------------
  ; `Dum.a` = condition, `Dum.b` = the `terminus` bound (spec §8.5), `Dum.c` =
  ; the body.
  .dum:
	mov	rdi, rbx
	call	__chk_ty_u1
	mov	rdi, rbx
	mov	rsi, [chkt_aST]
	mov	rdx, rax
	call	__chk_ty_want
	cmp	qword [chkt_bST], 0
	je	.d_body
	mov	rdi, rbx
	call	__chk_ty_mensura
	mov	rdi, rbx
	mov	rsi, [chkt_bST]
	mov	rdx, rax
	call	__chk_ty_want
  .d_body:
	inc	dword [rbx + ChkTyWork.wloop]
	mov	rdi, rbx
	mov	rsi, [chkt_cST]
	call	__chk_ty_stmt
	dec	dword [rbx + ChkTyWork.wloop]
	jmp	.unit

  ; ---- For ---------------------------------------------------------------
  .for:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_for
	jmp	.unit

  ; ---- Sub ---------------------------------------------------------------
  .sub:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_sub
	jmp	.unit

  ; ---- Discerne ----------------------------------------------------------
  ; The scrutinee and every arm's block are typed; the PATTERN is not checked
  ; against the scrutinee's type -- types.inc's header says why.
  .discerne:
	mov	rdi, rbx
	mov	rsi, [chkt_aST]
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_aST]
	call	__chk_ty_settled
	mov	qword [chkt_iST], 0
  .di_arm:
	mov	rax, [chkt_iST]
	cmp	rax, [chkt_cST]
	jae	.di_else
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_bST]
	add	rsi, [chkt_iST]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]		; `Casus.b` = the block
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_ty_stmt
	inc	qword [chkt_iST]
	jmp	.di_arm
  .di_else:
	mov	rdi, rbx
	mov	rsi, [chkt_dST]
	call	__chk_ty_stmt

  .unit:
	mov	rdi, rbx
	call	__chk_ty_unit
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rax
	call	__chk_ty_setty
  .out:
	return
endp

; __chk_ty_binding(w, n) -- `firma`/`mutabilis`, with or without either half.
proc __chk_ty_binding, chkt_wBD, chkt_nBD
	uses	rbx, r12, r13, r14
	locals
		slot chkt_aBD, dq
		slot chkt_bBD, dq
		slot chkt_dBD, dq
	endl
	mov	rbx, [chkt_wBD]
	mov	r12, [chkt_nBD]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_aBD], rcx			; the annotation
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bBD], rcx			; the initializer
	mov	ecx, [rax + AstNode.d]
	mov	[chkt_dBD], rcx

	cmp	qword [chkt_aBD], 0
	jne	.annotated
	cmp	qword [chkt_bBD], 0
	jne	.inferred
	; checker.md class I: "binding with neither type nor initializer"
	mov	rdi, rbx
	mov	rsi, 309
	mov	rdx, r12
	call	__chk_ty_raise
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.record

  .annotated:
	mov	rdi, rbx
	mov	rsi, [chkt_aBD]
	call	chk_ty_of_node
	mov	r13, rax
	cmp	qword [chkt_bBD], 0
	je	.have_r13
	mov	rdi, rbx
	mov	rsi, [chkt_bBD]
	mov	rdx, r13
	call	__chk_ty_want
  .have_r13:
	mov	rax, r13
	jmp	.record

  .inferred:
	; checker.md section 2.3: "`firma`/`mutabilis` without an annotation
	; take the initializer's type". A bare literal has none, so a binding is
	; not a place an expectation can come from and `EXS-E0308` is right.
	mov	rdi, rbx
	mov	rsi, [chkt_bBD]
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_bBD]
	call	__chk_ty_settled

  .record:
	mov	r13, rax
	cmp	qword [chkt_dBD], 0
	je	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_dBD]
	mov	rdx, r13
	call	ast_decl_ty
  .out:
	return
endp

; __chk_ty_lvalue(w, n) -> rax = the target's type, and `EXS-E0306` when it
; may not be assigned to.
;
; checker.md class F, spec §8.6's "lvalue check semantic": a `firma` binding, a
; parameter, and anything that is not a place at all. A `mutabilis` binding, an
; accumulator (spec §5.4's contribution), a field of one and an element of one
; are assignable.
proc __chk_ty_lvalue, chkt_wLV, chkt_nLV
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [chkt_wLV]
	mov	r12, [chkt_nLV]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_kind
	mov	r14, rax
	cmp	r14, AST_PATH
	je	.path
	cmp	r14, AST_MEMBER
	je	.within
	cmp	r14, AST_INDEX
	je	.within
	cmp	r14, AST_UNARY
	je	.unary
	; a call result, a literal, an operator application: not a place
	mov	rdi, rbx
	mov	rsi, 306
	mov	rdx, r12
	call	__chk_ty_raise
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r12
	call	__chk_ty_settled
	return

  .within:
	; a field or an element is a place only inside a place: `x.f = e` and
	; `w[j] = e` are; `f().x = e` and spec §5.2's `(f sicut acies<u8,
	; 17>)[0] = 1` are not -- a cast's result is a value (member.inc's
	; `__chk_ty_isplace`)
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_isplace
	test	eax, eax
	jnz	.place
	mov	rdi, rbx
	mov	rsi, 306
	mov	rdx, r12
	call	__chk_ty_raise
	jmp	.place

  .unary:
	; `*p = e` is a place; `-x = e` and `&x = e` are not
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.aux]
	cmp	ecx, AST_U_DEREF
	je	.place
	mov	rdi, rbx
	mov	rsi, 306
	mov	rdx, r12
	call	__chk_ty_raise
  .place:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r12
	call	__chk_ty_settled
	return

  .path:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_seg0
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.d]
	test	r13, r13
	jz	.place
	mov	rdi, r13
	call	__chk_ty_is_pre
	test	eax, eax
	jnz	.notplace
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_decl_at
	movzx	r14d, byte [rax + AstDecl.kind]
	movzx	ecx, byte [rax + AstDecl.flags]
	cmp	r14d, AST_D_ACCUM
	je	.acc
	cmp	r14d, AST_D_BINDING
	jne	.notplace
	test	ecx, AST_F_MUTABILIS
	jz	.notplace		; `firma` -- spec §4.1 rule 7's immutability
	; this write settles the definite-assignment obligation
	lea	rdi, [rbx + ChkTyWork.wdef]
	mov	rsi, r13
	call	vec_get
	mov	byte [rax], CHK_TY_D_SET
	jmp	.pathty
  .acc:
	; spec §5.4: "Writing to it is the contribution" -- the one statement
	; that may name an accumulator inside its own iteration.
	lea	rdi, [rbx + ChkTyWork.wdef]
	mov	rsi, r13
	call	vec_get
	mov	byte [rax], CHK_TY_D_SET
  .pathty:
	; typed WITHOUT `__chk_ty_path_expr`, so that neither `EXS-E0341` nor
	; the definite-assignment read fires on a write
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_decl_at
	mov	r14d, [rax + AstDecl.ty]
	test	r14, r14
	jnz	.setpath
	mov	rdi, rbx
	call	__chk_ty_err
	mov	r14, rax
  .setpath:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__chk_ty_setty
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_seg0
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r14
	call	__chk_ty_setty
	mov	rax, r14
	return

  .notplace:
	mov	rdi, rbx
	mov	rsi, 306
	mov	rdx, r12
	call	__chk_ty_raise
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_path_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r12
	call	__chk_ty_settled
	return
endp

; __chk_ty_assign(w, n) -- `target = value;`
proc __chk_ty_assign, chkt_wAS, chkt_nAS
	uses	rbx, r12, r13
	locals
		slot chkt_aAS, dq
		slot chkt_bAS, dq
	endl
	mov	rbx, [chkt_wAS]
	mov	r12, [chkt_nAS]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_aAS], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bAS], rcx
	mov	rdi, rbx
	mov	rsi, [chkt_aAS]
	call	__chk_ty_lvalue
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, [chkt_bAS]
	mov	rdx, r13
	call	__chk_ty_want
	return
endp

; __chk_ty_sub(w, n) -- `sub P = e;` and `sub P = e { ... }`.
;
; WHAT THE PROVIDER'S TYPE MUST BE IS `[OPEN]`, and checker.md section 8 says so
; in as many words: "what `sub alloc = a` accepts as `a`" is on its unresolved
; list. Spec §4.1 rule 4 makes `sub` the binding of a carrier and checker.md
; section 2.8 makes every atom's carrier one `ptr`, which argues for requiring
; the atom's own `cap` type -- and `sub ambitus = m.ambitus();` satisfies that
; exactly. But spec §4.5's `sub alloc = a` binds "an arena", a value whose
; surface type no section names.
;
; SO THE RULE IS HALF-APPLIED, deliberately: when the provider's type IS a
; capability type, it must be THIS capability's (`sub rete = someAmbitus` is
; `EXS-E0303`); when it is anything else, the provider is typed and accepted.
; That rejects the case the spec does decide -- a carrier for the wrong atom --
; and declines to invent the case it does not. Tightening it is a spec question,
; not a code one.
proc __chk_ty_sub, chkt_wSB, chkt_nSB
	uses	rbx, r12, r13, r14
	locals
		slot chkt_aSB, dq
		slot chkt_bSB, dq
		slot chkt_cSB, dq
		slot chkt_dSB, dq
	endl
	mov	rbx, [chkt_wSB]
	mov	r12, [chkt_nSB]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_aSB], rcx			; the `Path` naming the atom
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bSB], rcx			; the provider expression
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_cSB], rcx			; the scoped block, or 0
	mov	ecx, [rax + AstNode.d]
	mov	[chkt_dSB], rcx

	; the atom's type, from the `Path`'s resolved declaration
	mov	rdi, rbx
	mov	rsi, [chkt_aSB]
	call	__chk_ty_seg0
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r14
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.d]
	test	r13, r13
	jz	.noatom
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_decl_type
	jmp	.have
  .noatom:
	; pass 1 left the `Path` unresolved on purpose: `sub` of a non-atom is
	; checker.md class L (`EXS-E0423`), which that pass deliberately did not
	; raise so that half of one class would not live in two passes.
	mov	rdi, rbx
	mov	rsi, 423
	mov	rdx, r12
	call	__chk_ty_raise
	mov	rdi, rbx
	call	__chk_ty_err
  .have:
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r14
	mov	rdx, r13
	call	__chk_ty_setty
	mov	rdi, rbx
	mov	rsi, [chkt_aSB]
	mov	rdx, r13
	call	__chk_ty_setty
	cmp	qword [chkt_dSB], 0
	je	.provider
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_dSB]
	mov	rdx, r13
	call	ast_decl_ty
  .provider:
	mov	rdi, rbx
	mov	rsi, [chkt_bSB]
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_bSB]
	call	__chk_ty_settled
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r14
	call	__chk_ty_kindof
	cmp	rax, AST_TY_CAP
	jne	.blk			; not a carrier at all -- `[OPEN]`
	mov	rdi, rbx
	mov	rsi, r14
	mov	rdx, r13
	mov	rcx, [chkt_bSB]
	call	__chk_ty_same
  .blk:
	mov	rdi, rbx
	mov	rsi, [chkt_cSB]
	call	__chk_ty_stmt
	return
endp

; __chk_ty_for(w, n) -- `per x in E { ... }`, with its `ForHead`.
proc __chk_ty_for, chkt_wFR, chkt_nFR
	uses	rbx, r12, r13, r14
	locals
		slot chkt_aFR, dq
		slot chkt_bFR, dq
		slot chkt_cFR, dq
		slot chkt_dFR, dq
		slot chkt_eFR, dq
	endl
	mov	rbx, [chkt_wFR]
	mov	r12, [chkt_nFR]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_aFR], rcx			; the iterable
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bFR], rcx			; the body
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_cFR], rcx			; the `ForHead`, or 0
	mov	ecx, [rax + AstNode.d]
	mov	[chkt_dFR], rcx			; the loop variable declaration

	; ---- the iterable, and the loop variable's type ---------------------
	mov	rdi, rbx
	mov	rsi, [chkt_aFR]
	call	__chk_ty_expr
	test	rax, rax
	jnz	.iter_typed
	; SPEC §8.5: "A range both of whose bounds are pending literals (`per i
	; in 0..17`) types them, and the variable, as `mensura` -- the index
	; type, and what anyone writes over an array." Before this rule
	; `__chk_ty_settled` made every such loop `EXS-E0308`. Only a RANGE gets
	; the expectation: any other pending iterable is not iterable at all
	; and keeps its E0308 (a bare `per i in 3`).
	mov	rdi, rbx
	mov	rsi, [chkt_aFR]
	call	__chk_ty_kind
	cmp	rax, AST_BINARY
	jne	.iter_pending
	mov	rdi, rbx
	mov	rsi, [chkt_aFR]
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.aux]
	cmp	ecx, AST_OP_RANGE
	jne	.iter_pending
	mov	rdi, rbx
	call	__chk_ty_mensura
	mov	rdi, rbx
	mov	rsi, [chkt_aFR]
	mov	rdx, rax
	call	__chk_ty_expect
	jmp	.iter_typed
  .iter_pending:
	xor	eax, eax
  .iter_typed:
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_aFR]
	call	__chk_ty_settled
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_kindof
	cmp	rax, AST_TY_ACIES
	je	.elem
	cmp	rax, AST_TY_ERROR
	je	.same
	; `a..b` -- a `Binary` whose type IS the element type (spec §8.5)
	mov	rdi, rbx
	mov	rsi, [chkt_aFR]
	call	__chk_ty_kind
	cmp	rax, AST_BINARY
	jne	.notiter
	mov	rdi, rbx
	mov	rsi, [chkt_aFR]
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.aux]
	cmp	ecx, AST_OP_RANGE
	jne	.notiter
  .same:
	mov	[chkt_eFR], r13
	jmp	.head
  .elem:
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, AstType.a
	call	__chk_ty_field
	mov	[chkt_eFR], rax
	jmp	.head
  .notiter:
	mov	rdi, rbx
	mov	rsi, 305
	mov	rdx, [chkt_aFR]
	call	__chk_ty_raise
	mov	rdi, rbx
	call	__chk_ty_err
	mov	[chkt_eFR], rax

  .head:
	cmp	qword [chkt_dFR], 0
	je	.forma
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_dFR]
	mov	rdx, [chkt_eFR]
	call	ast_decl_ty

  .forma:
	xor	r14, r14			; accumulators in this head
	cmp	qword [chkt_cFR], 0
	je	.body
	mov	rdi, rbx
	mov	rsi, [chkt_cFR]
	mov	rdx, [chkt_eFR]
	call	__chk_ty_forhead
	mov	r14, rax

  .body:
	inc	dword [rbx + ChkTyWork.wloop]
	test	r14, r14
	jz	.no_con
	inc	dword [rbx + ChkTyWork.wcon]
  .no_con:
	mov	rdi, rbx
	mov	rsi, [chkt_bFR]
	call	__chk_ty_stmt
	test	r14, r14
	jz	.no_con2
	dec	dword [rbx + ChkTyWork.wcon]
  .no_con2:
	dec	dword [rbx + ChkTyWork.wloop]
	return
endp

; __chk_ty_forhead(w, n, elemty) -> rax = the number of `contrahe`
; accumulators the head declares.
;
; Two `EXS-E0343` rules, both spec §5.4's and both stated there in one
; sentence each:
;
;   - the operator must be ASSOCIATIVE: `+ +% +| *`, never `- -% -|`
;     ("subtraction under `arborea` has no meaning this section defines");
;   - the `forma` shape is `ordinata` or `arborea`, and the width is
;     REQUIRED after `arborea` and FORBIDDEN after `ordinata` (spec §8.6:
;     "a semantic rule rather than a grammatical one").
proc __chk_ty_forhead, chkt_wFH, chkt_nFH, chkt_tFH
	uses	rbx, r12, r13, r14
	locals
		slot chkt_aFH, dq
		slot chkt_bFH, dq
		slot chkt_cFH, dq
		slot chkt_dFH, dq
		slot chkt_iFH, dq
		slot chkt_kFH, dq
	endl
	mov	rbx, [chkt_wFH]
	mov	r12, [chkt_nFH]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_aFH], rcx			; the `Contrahe` list
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bFH], rcx			; how many
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_cFH], rcx			; the `forma` shape NAME, or 0
	mov	ecx, [rax + AstNode.d]
	mov	[chkt_dFH], rcx			; its width, or 0

	; ---- the shape ------------------------------------------------------
	cmp	qword [chkt_cFH], 0
	je	.accs
	mov	rdi, rbx
	mov	rsi, [chkt_cFH]
	lea	rdx, [chk_ty_forma_names]
	mov	rcx, CHK_TY_FORMA_NAMES_LEN
	call	__chk_ty_pool
	cmp	rax, CHK_TY_FORMA_ORDINATA
	je	.ordinata
	cmp	rax, CHK_TY_FORMA_ARBOREA
	je	.arborea
	jmp	.badshape
  .ordinata:
	cmp	qword [chkt_dFH], 0
	je	.accs
	jmp	.badshape
  .arborea:
	cmp	qword [chkt_dFH], 0
	jne	.accs
  .badshape:
	mov	rdi, rbx
	mov	rsi, 343
	mov	rdx, r12
	call	__chk_ty_raise

	; ---- the accumulators ----------------------------------------------
  .accs:
	mov	qword [chkt_iFH], 0
  .acc:
	mov	rax, [chkt_iFH]
	cmp	rax, [chkt_bFH]
	jae	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_aFH]
	add	rsi, [chkt_iFH]
	call	ast_extra_at
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_node
	movzx	r14d, word [rax + AstNode.aux]	; the `ArithOp`
	mov	ecx, [rax + AstNode.d]

	; `Decl.ty` on an `ACCUM` is the reduction handle (typed-ast.md section
	; 2.5's `red`, `a` = F). F has no other representation in this tree, so
	; `a` is the accumulator's own declaration -- which is what makes two
	; accumulators in one head two distinct handles.
	test	ecx, ecx
	jz	.op
	mov	[chkt_kFH], rcx
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, AST_TY_RED
	mov	rdx, rcx
	xor	rcx, rcx
	call	ast_type_una
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_kFH]
	mov	rdx, rax
	call	ast_decl_ty
  .op:
	cmp	r14, AST_OP_ADD
	je	.acc_next
	cmp	r14, AST_OP_ADDW
	je	.acc_next
	cmp	r14, AST_OP_ADDS
	je	.acc_next
	cmp	r14, AST_OP_MUL
	je	.acc_next
	mov	rdi, rbx
	mov	rsi, 343
	mov	rdx, r13
	call	__chk_ty_raise
  .acc_next:
	mov	rdi, rbx
	call	__chk_ty_unit
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, rax
	call	__chk_ty_setty
	inc	qword [chkt_iFH]
	jmp	.acc
  .out:
	mov	rax, [chkt_bFH]
	return
endp
