; compiler/x86_64/checker/types/member.inc
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
; PASS 2, PART 3 -- MEMBERS, CALLS AND LAMBDAS. Included by types.inc; never
; included directly (it is a section of that file, split only for length).
;
; SPEC §8.6 DECISION 8, IMPLEMENTED LITERALLY: `x.m(a)` is the member `m` with
; `x` as its FIRST argument. The tree has no receiver syntax and no `Self`
; (checker.md finding 6), so an interface member's or an impl method's first
; parameter IS the receiver, and the arity a call is checked against is
; `1 + written arguments`.
;
; THE FOUR SHAPES `x.m` CAN HAVE, and how each is told apart:
;
;   1. `x` is a VALUE and `m` a FIELD of its struct -- the field's type, and
;      `Member.d` is the `AST_D_FIELD` declaration.
;   2. `x` is a VALUE and `m` a METHOD -- an `AST_D_MEMBER` of an impl whose
;      target type is `x`'s, or of the interface a `param`/`dyn` receiver is
;      bound to. `Member.d` is that declaration; a call passes `x` first.
;   3. `x` is a TYPE NAME and `m` an ASSOCIATED function -- `Scriptor.ad_exitum(a)`
;      in `examples/initium.exsc`. No receiver value exists, so nothing is
;      prepended. The receiver `Path` is still annotated with the nominal type,
;      because checker.md section 2.5 requires `ty` != 0 on EVERY expression
;      node and that `Path` is one.
;   4. `m` is one of the prelude's (`m.ambitus()`, `Scriptor.ad_exitum`,
;      `s.scribe`). `Member.d` is the TAGGED prelude row (prim.inc) and its
;      type comes from `chk_ty_prelude_type`.
;
; PASS 1'S `Member.d` IS A HINT AND IS OVERWRITTEN. resolve.inc says so in its
; own header -- "The types pass OVERWRITES this slot; it is a hint, not an
; answer" -- and this file uses it only as the LAST resort, when the receiver's
; type yields nothing and the module declares exactly one member of that name.
;
; COST. Finding a method is a scan of `Ast.decls` for `AST_D_MEMBER`s of that
; name, bounded by the declaration count, once per member expression.
; That is not spec §16's kill criterion -- that criterion is about `sub`
; resolution needing a SEARCH, and this is a lookup with a unique answer -- but
; it is O(members x member-expressions) and is recorded as the first thing to
; index if a real module makes it hot. The index would be keyed (target type
; id, name id), which is exactly `ChkCtx.membs` plus a type, and is not built
; today because no module has been measured.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.3, §5.1, §7.1, §8.6; docs/design/
; checker.md sections 2.5, 2.6, 2.7, findings 6 and 14; docs/design/runtime.md 2.2.
; -----------------------------------------------------------------------------

; __chk_ty_pre_assoc(k) -> eax = 1 if prelude row `k` is ASSOCIATED (called as
; `Type.name(...)`, no receiver). `prelude/interface.inc`'s
; `EXS_IFACE_F_ASSOCIATA`, which today is `ad_exitum` and nothing else.
proc __chk_ty_pre_assoc, chkt_kv
	locals
	endl
	xor	eax, eax
	cmp	qword [chkt_kv], CHK_TY_PRE_AD_EXITUM
	jne	.out
	mov	eax, 1
  .out:
	return
endp

; __chk_ty_pre_member(w, recvty, nameid) -> rax = the 1-based prelude row a
; member of `recvty` named `nameid` resolves to, or 0.
;
; Three rows are reachable this way: `Scriptor.a`, `Scriptor.descriptor` and
; `Scriptor.scribe` on a `Scriptor` value, and `Mundus.ambitus` on the
; capability `Mundus` -- which is spec §4.7's "derivation is by method on the
; root, `m.ambitus()`".
proc __chk_ty_pre_member, chkt_wPM, chkt_tPM, chkt_mPM
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wPM]
	mov	r12, [chkt_tPM]
	mov	rdi, rbx
	mov	rsi, [chkt_mPM]
	lea	rdx, [chk_ty_pre_names]
	mov	rcx, CHK_TY_PRE_NAMES_LEN
	call	__chk_ty_pool
	mov	r13, rax
	test	r13, r13
	jz	.no

	; `Mundus` -> `ambitus`
	mov	rdi, rbx
	mov	rsi, AST_CAP_MUNDUS
	call	__chk_ty_atom
	cmp	rax, r12
	jne	.scriptor
	cmp	r13, CHK_TY_PRE_AMBITUS
	jne	.no
	mov	rax, r13
	return

  .scriptor:
	mov	rdi, rbx
	mov	rsi, CHK_TY_PRE_SCRIPTOR
	call	chk_ty_prelude_type
	cmp	rax, r12
	jne	.no
	cmp	r13, CHK_TY_PRE_SCR_A
	je	.yes
	cmp	r13, CHK_TY_PRE_SCR_FD
	je	.yes
	cmp	r13, CHK_TY_PRE_SCRIBE
	je	.yes
	cmp	r13, CHK_TY_PRE_AD_EXITUM
	je	.yes
	jmp	.no
  .yes:
	mov	rax, r13
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_typedecl_of(w, n) -> rax = the declaration or tagged prelude row a
; `Path` in expression position names WHEN THAT NAME IS A TYPE, else 0.
; This is how `Scriptor.ad_exitum(a)` is told from `s.scribe(t)`.
proc __chk_ty_typedecl_of, chkt_wTD, chkt_nTD
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wTD]
	mov	r12, [chkt_nTD]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_kind
	cmp	rax, AST_PATH
	jne	.no
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_seg0
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.d]
	test	r13, r13
	jz	.no
	mov	rdi, r13
	call	__chk_ty_is_pre
	test	eax, eax
	jnz	.pre
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_STRUCT
	je	.yes
	cmp	ecx, AST_D_IFACE
	je	.yes
	cmp	ecx, AST_D_TYPUS
	je	.yes
	jmp	.no
  .pre:
	mov	rax, r13
	and	rax, CHK_TY_PRE_MASK
	cmp	rax, CHK_TY_PRE_SCRIPTOR
	jne	.no
  .yes:
	mov	rax, r13
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_field_of(w, structdecl, nameid) -> rax = the `AST_D_FIELD`
; declaration of that name, or 0. Fields are pushed in source order
; immediately after their struct (typed-ast.md section 2.4), so this is a scan
; of a contiguous range and not of the table.
proc __chk_ty_field_of, chkt_wFO, chkt_dFO, chkt_mFO
	uses	rbx, r12, r13
	locals
		slot chkt_nFO, dq
	endl
	mov	rbx, [chkt_wFO]
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_decl_count
	mov	[chkt_nFO], rax
	mov	r12, [chkt_dFO]
	inc	r12
  .scan:
	cmp	r12, [chkt_nFO]
	ja	.no
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.parent]
	cmp	rcx, [chkt_dFO]
	jne	.no			; the range ended
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_FIELD
	jne	.next
	mov	ecx, [rax + AstDecl.name]
	cmp	rcx, [chkt_mFO]
	je	.yes
  .next:
	inc	r12
	jmp	.scan
  .yes:
	mov	rax, r12
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_member_of(w, ownerdecl, nameid) -> rax = the `AST_D_MEMBER` of that
; name declared directly under `ownerdecl` (an interface or an impl), or 0.
proc __chk_ty_member_of, chkt_wMO, chkt_dMO, chkt_mMO
	uses	rbx, r12
	locals
		slot chkt_nMO, dq
	endl
	mov	rbx, [chkt_wMO]
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_decl_count
	mov	[chkt_nMO], rax
	mov	r12, 1
  .scan:
	cmp	r12, [chkt_nMO]
	ja	.no
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_MEMBER
	jne	.next
	mov	ecx, [rax + AstDecl.parent]
	cmp	rcx, [chkt_dMO]
	jne	.next
	mov	ecx, [rax + AstDecl.name]
	cmp	rcx, [chkt_mMO]
	je	.yes
  .next:
	inc	r12
	jmp	.scan
  .yes:
	mov	rax, r12
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_impl_target(w, impldecl) -> rax = the interned TARGET type of an
; implementation, read off its head's `Type` node (phase E typed it).
proc __chk_ty_impl_target, chkt_wIT, chkt_dIT
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkt_wIT]
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_dIT]
	call	ast_decl_at
	mov	r12d, [rax + AstDecl.node]
	test	r12, r12
	jz	.no
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]		; `Impl.a` = `ImplHead`
	test	rcx, rcx
	jz	.no
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]		; the target `Type`
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_ty_ty
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_method(w, recvty, nameid) -> rax = the `AST_D_MEMBER` declaration a
; method call on a receiver of type `recvty` resolves to, or 0.
;
; CHECKED AGAINST THE INTERFACE, NEVER AN IMPL, WHEN THE RECEIVER IS GENERIC --
; checker.md section 2.7: "No impl is ever consulted in a generic body (spec
; §7.1 compiles once)". A `param` receiver goes to its bound interface's member
; range and stops there; a `dyn I` receiver likewise; only a CONCRETE receiver
; consults the impl map.
proc __chk_ty_method, chkt_wME, chkt_tME, chkt_mME
	uses	rbx, r12, r13, r14
	locals
		slot chkt_nME, dq
	endl
	mov	rbx, [chkt_wME]
	mov	r12, [chkt_tME]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_kindof
	mov	r13, rax
	cmp	r13, AST_TY_PARAM
	je	.bound
	cmp	r13, AST_TY_DYN
	je	.iface

	; ---- a concrete receiver: every impl whose target is this type ------
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_decl_count
	mov	[chkt_nME], rax
	mov	r14, 1
  .scan:
	cmp	r14, [chkt_nME]
	ja	.no
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r14
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_IMPL
	jne	.next
	mov	rdi, rbx
	mov	rsi, r14
	call	__chk_ty_impl_target
	cmp	rax, r12
	jne	.next
	mov	rdi, rbx
	mov	rsi, r14
	mov	rdx, [chkt_mME]
	call	__chk_ty_member_of
	test	rax, rax
	jnz	.out
  .next:
	inc	r14
	jmp	.scan

  .bound:
	; `T: Trait` -- `param.a` is the `GenericParam` declaration and its
	; `GenericParam.b` is the bound (ast/kinds.inc narrowing 1: optional).
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AstType.a
	call	__chk_ty_field
	test	rax, rax
	jz	.no
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_decl_at
	mov	r14d, [rax + AstDecl.node]
	test	r14, r14
	jz	.no
	mov	rdi, rbx
	mov	rsi, r14
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_ty_ty
	mov	r12, rax
	; fall through with the bound's type
  .iface:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AstType.a
	call	__chk_ty_field		; the interface DECLARATION
	test	rax, rax
	jz	.no
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_mME]
	call	__chk_ty_member_of
	jmp	.out
  .no:
	xor	eax, eax
  .out:
	return
endp

; __chk_ty_member(w, n) -> rax = the type of a `Member` expression.
proc __chk_ty_member, chkt_wMB, chkt_nMB
	uses	rbx, r12, r13, r14
	locals
		slot chkt_rMB, dq	; the receiver node
		slot chkt_mMB, dq	; the member's name id
		slot chkt_tMB, dq	; the receiver's type
		slot chkt_zMB, dq	; the type declaration, for an associated call
	endl
	mov	rbx, [chkt_wMB]
	mov	r12, [chkt_nMB]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_rMB], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_mMB], rcx
	; shape 3: the receiver is a TYPE name
	mov	rdi, rbx
	mov	rsi, [chkt_rMB]
	call	__chk_ty_typedecl_of
	mov	[chkt_zMB], rax
	test	rax, rax
	jz	.value

	; annotate the receiver `Path` and its `Seg` with the nominal type, so
	; that checker.md section 2.5's "`ty` != 0 on every expression node"
	; holds for a path that denotes a type rather than a value
	mov	rdi, [chkt_zMB]
	call	__chk_ty_is_pre
	test	eax, eax
	jz	.assoc_decl
	mov	rdi, rbx
	mov	rsi, [chkt_zMB]
	and	rsi, CHK_TY_PRE_MASK
	call	chk_ty_prelude_type
	jmp	.assoc_ty
  .assoc_decl:
	mov	rdi, rbx
	mov	rsi, [chkt_zMB]
	call	__chk_ty_decl_type
  .assoc_ty:
	mov	r13, rax
	mov	[chkt_tMB], r13
	mov	rdi, rbx
	mov	rsi, [chkt_rMB]
	mov	rdx, r13
	call	__chk_ty_setty
	mov	rdi, rbx
	mov	rsi, [chkt_rMB]
	call	__chk_ty_seg0
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r13
	call	__chk_ty_setty
	jmp	.lookup

  .value:
	mov	rdi, rbx
	mov	rsi, [chkt_rMB]
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_rMB]
	call	__chk_ty_settled
	mov	[chkt_tMB], rax

  .lookup:
	; the prelude first: its rows are the only members whose owner has no
	; `AstDecl` to scan (prim.inc's header)
	mov	rdi, rbx
	mov	rsi, [chkt_tMB]
	mov	rdx, [chkt_mMB]
	call	__chk_ty_pre_member
	test	rax, rax
	jz	.fields
	mov	r13, rax
	; `Member.d` stays 0 and the row goes in the side table -- prim.inc's
	; header records the pass-3 trap that forced the split.
	mov	rdi, rbx
	mov	rsi, r12
	xor	rdx, rdx
	call	__chk_ty_setd
	lea	rdi, [rbx + ChkTyWork.wpre]
	mov	rsi, r12
	call	vec_get
	mov	[rax], r13b
	mov	rdi, rbx
	mov	rsi, r13
	call	chk_ty_prelude_type
	return

  .fields:
	; a field of a source struct
	mov	rdi, rbx
	mov	rsi, [chkt_tMB]
	call	__chk_ty_kindof
	cmp	rax, AST_TY_STRUCT
	jne	.methods
	mov	rdi, rbx
	mov	rsi, [chkt_tMB]
	mov	rdx, AstType.a
	call	__chk_ty_field
	mov	r13, rax
	mov	rdi, r13
	call	__chk_ty_is_pre
	test	eax, eax
	jnz	.methods		; the prelude's `Scriptor`, already tried
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, [chkt_mMB]
	call	__chk_ty_field_of
	test	rax, rax
	jz	.methods
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__chk_ty_setd
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_decl_at
	mov	eax, [rax + AstDecl.ty]
	test	eax, eax
	jnz	.out
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.out

  .methods:
	mov	rdi, rbx
	mov	rsi, [chkt_tMB]
	mov	rdx, [chkt_mMB]
	call	__chk_ty_method
	test	rax, rax
	jnz	.have_m
	; last resort: pass 1's unique module-wide candidate. It is a hint
	; (resolve.inc's header) and it is used only when the receiver's type
	; yielded nothing, so a program with one member of that name in the
	; whole module still checks while an ambiguous one does not.
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.d]
	test	r13, r13
	jz	.nomember
	mov	rdi, r13
	call	__chk_ty_is_pre
	test	eax, eax
	jnz	.nomember
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_MEMBER
	jne	.nomember
	mov	rax, r13
  .have_m:
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__chk_ty_setd
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_decl_at
	mov	eax, [rax + AstDecl.ty]
	test	eax, eax
	jnz	.out
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.out

  .nomember:
	; checker.md class E: "member on a type without it". The `Member.d`
	; slot is cleared, because pass 1's candidate was wrong and leaving it
	; would tell Stage 3 a lie.
	mov	rdi, rbx
	mov	rsi, r12
	xor	rdx, rdx
	call	__chk_ty_setd
	mov	rdi, rbx
	mov	rsi, [chkt_tMB]
	call	__chk_ty_kindof
	cmp	rax, AST_TY_ERROR
	je	.err_quiet
	mov	rdi, rbx
	mov	rsi, 305
	mov	rdx, r12
	call	__chk_ty_raise
  .err_quiet:
	mov	rdi, rbx
	call	__chk_ty_err
  .out:
	return
endp

; __chk_ty_recv(w, calleenode) -> rax = the receiver node to pass as argument
; 0, or 0 when the call has no implicit receiver.
;
; Spec §8.6 decision 8. A `Member` whose `d` is an `AST_D_MEMBER` or a
; non-associated prelude row takes its receiver first; a FIELD of function
; type, an ASSOCIATED function, and every non-`Member` callee do not.
proc __chk_ty_recv, chkt_wRC, chkt_nRC
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wRC]
	mov	r12, [chkt_nRC]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_kind
	cmp	rax, AST_MEMBER
	jne	.no
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.d]
	mov	ecx, [rax + AstNode.a]
	; the prelude side table first: `Member.d` is 0 for a prelude member
	lea	rdi, [rbx + ChkTyWork.wpre]
	mov	rsi, r12
	mov	r12, rcx
	call	vec_get
	movzx	eax, byte [rax]
	test	eax, eax
	jz	.notpre
	mov	rdi, rax
	call	__chk_ty_pre_assoc
	test	eax, eax
	jnz	.no
	mov	rax, r12
	return
  .notpre:
	test	r13, r13
	jz	.no
  .decl:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_MEMBER
	jne	.no
	mov	rax, r12
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_call(w, n) -> rax = the result type of a call.
proc __chk_ty_call, chkt_wCL, chkt_nCL
	uses	rbx, r12, r13, r14
	locals
		slot chkt_fCL, dq	; the callee node
		slot chkt_oCL, dq	; the argument list `extra` offset
		slot chkt_cCL, dq	; the WRITTEN argument count
		slot chkt_tCL, dq	; the callee's type
		slot chkt_rCL, dq	; the receiver node, or 0
		slot chkt_pCL, dq	; the parameter count
		slot chkt_eCL, dq	; the parameter list `extra` offset
		slot chkt_iCL, dq
	endl
	mov	rbx, [chkt_wCL]
	mov	r12, [chkt_nCL]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_fCL], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_oCL], rcx
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_cCL], rcx

	mov	rdi, rbx
	mov	rsi, [chkt_fCL]
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_fCL]
	call	__chk_ty_settled
	mov	[chkt_tCL], rax

	mov	rdi, rbx
	mov	rsi, [chkt_fCL]
	call	__chk_ty_recv
	mov	[chkt_rCL], rax

	mov	rdi, rbx
	mov	rsi, [chkt_tCL]
	call	__chk_ty_kindof
	cmp	rax, AST_TY_FN
	je	.isfn
	cmp	rax, AST_TY_ERROR
	je	.quiet
	; checker.md class E: "call of non-function"
	mov	rdi, rbx
	mov	rsi, 305
	mov	rdx, r12
	call	__chk_ty_raise
  .quiet:
	mov	qword [chkt_iCL], 0
  .q_arg:
	mov	rax, [chkt_iCL]
	cmp	rax, [chkt_cCL]
	jae	.q_done
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_oCL]
	add	rsi, [chkt_iCL]
	call	ast_extra_at
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r13
	call	__chk_ty_settled
	inc	qword [chkt_iCL]
	jmp	.q_arg
  .q_done:
	mov	rdi, rbx
	call	__chk_ty_err
	return

  .isfn:
	mov	rdi, rbx
	mov	rsi, [chkt_tCL]
	mov	rdx, AstType.width
	call	__chk_ty_field
	mov	[chkt_pCL], rax
	mov	rdi, rbx
	mov	rsi, [chkt_tCL]
	mov	rdx, AstType.a
	call	__chk_ty_field
	mov	[chkt_eCL], rax

	; ---- arity (spec §8.6, `EXS-E0304`) --------------------------------
	mov	r13, [chkt_cCL]
	cmp	qword [chkt_rCL], 0
	je	.have_n
	inc	r13			; the receiver is argument 0
  .have_n:
	cmp	r13, [chkt_pCL]
	je	.args
	mov	rdi, rbx
	mov	rsi, 304
	mov	rdx, r12
	call	__chk_ty_raise

  ; ---- the arguments -------------------------------------------------------
  ; Every written argument is typed whether or not the arity matched, so that
  ; a call with one argument too many still annotates the ones it does have
  ; (checker.md section 2.5's "`ty` != 0 on every expression node" is a
  ; post-condition of a CLEAN run, but a node left untyped after a diagnostic
  ; is still a node a later dump has to print).
  .args:
	; the receiver, if there is one
	cmp	qword [chkt_rCL], 0
	je	.written
	cmp	qword [chkt_pCL], 0
	je	.written
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_eCL]
	call	ast_extra_at
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, [chkt_rCL]
	call	__chk_ty_ty
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r14
	mov	rcx, [chkt_rCL]
	call	__chk_ty_same

  .written:
	mov	qword [chkt_iCL], 0
  .arg:
	mov	rax, [chkt_iCL]
	cmp	rax, [chkt_cCL]
	jae	.result
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_oCL]
	add	rsi, [chkt_iCL]
	call	ast_extra_at
	mov	r13, rax		; the argument node

	; the parameter this argument lands on
	mov	rax, [chkt_iCL]
	cmp	qword [chkt_rCL], 0
	je	.pidx
	inc	rax
  .pidx:
	cmp	rax, [chkt_pCL]
	jae	.arg_untyped
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_eCL]
	add	rsi, rax
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, rax
	call	__chk_ty_want
	jmp	.arg_next
  .arg_untyped:
	; past the end of the parameter list -- `EXS-E0304` is already raised;
	; type it so that nothing downstream meets a `ty` of 0
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r13
	call	__chk_ty_settled
  .arg_next:
	inc	qword [chkt_iCL]
	jmp	.arg

  .result:
	; `Call.d` -- checker.md section 2.5: the chosen impls of a generic
	; instantiation, in generic-parameter order. Filled only when the callee
	; carries explicit `GenericArgs`; see types.inc's header for what is
	; `[OPEN]` about the rest.
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_callimpls

	mov	rdi, rbx
	mov	rsi, [chkt_tCL]
	call	__chk_ty_resultof
	mov	r13, rax

	; `own[node]` -- typed-ast.md section 2.2: does this expression yield an
	; OWNED reference? A call returning `refero`/`refero_communis` does; the
	; rest of spec §6.3's ownership is Stage 3's (types.inc's header).
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_kindof
	cmp	rax, AST_TY_REF
	je	.owned
	cmp	rax, AST_TY_REFC
	jne	.done
  .owned:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	mov	rdx, 1
	call	ast_own_set
  .done:
	mov	rax, r13
	return
endp

; __chk_ty_callimpls(w, n) -- `Call.d` for a generic instantiation.
;
; checker.md section 2.7: "At an instantiation `total<Left>(a, b)` the checker
; looks up the impl of `Summable` for `Left` in the impl map ... and records the
; impl decl in `Call.d`". `Call.d` is `AST_R_ANY` for exactly this (ast/
; kinds.inc, checker.md finding 4). What is recorded is an `Ast.extra` offset
; holding one impl declaration id per generic parameter, IN GENERIC-PARAMETER
; ORDER (section 2.7's "stable across impls"), so that Stage 3's dictionary
; layout reads a run and not a chain.
;
; ONLY AN EXPLICIT `GenericArgs` LIST IS HANDLED. Inferring `T` from the
; argument types is a unification, which checker.md section 2.3 excludes in as
; many words ("No unification variables"), and spec §7.1 does not say whether
; the arguments may be omitted. `[OPEN]`.
proc __chk_ty_callimpls, chkt_wCI, chkt_nCI
	uses	rbx, r12, r13, r14
	locals
		slot chkt_gCI, dq
		slot chkt_dCI, dq
		slot chkt_mCI, dq
		slot chkt_iCI, dq
	endl
	mov	rbx, [chkt_wCI]
	mov	r12, [chkt_nCI]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.a]		; the callee
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_kind
	cmp	rax, AST_PATH
	jne	.out
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_seg0
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r14
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]		; `Seg.b` = `GenericArgs`
	mov	[chkt_gCI], rcx
	test	rcx, rcx
	jz	.out
	mov	r14d, [rax + AstNode.d]		; the callee's declaration
	test	r14, r14
	jz	.out
	mov	rdi, r14
	call	__chk_ty_is_pre
	test	eax, eax
	jnz	.out

	; the callee's `Generics` node -- `Fn.c` (ast/kinds.inc)
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r14
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.node]
	test	rcx, rcx
	jz	.out
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_dCI], rcx
	test	rcx, rcx
	jz	.out
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_mCI], rcx			; the generic-parameter list
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_dCI], rcx			; how many

	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_list_mark
	mov	r13, rax
	mov	qword [chkt_iCI], 0
  .gp:
	mov	rax, [chkt_iCI]
	cmp	rax, [chkt_dCI]
	jae	.emit
	; the bound of generic parameter i, and the type argument at i
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_mCI]
	add	rsi, [chkt_iCI]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]		; `GenericParam.b` = the bound
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_ty_ty
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, AstType.a
	call	__chk_ty_field			; the interface declaration
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, [chkt_gCI]
	mov	rdx, [chkt_iCI]
	call	__chk_ty_genarg
	mov	rdi, rbx
	mov	rsi, r14
	mov	rdx, rax
	call	chk_ty_findimpl
	test	rax, rax
	jnz	.push
	; checker.md class N: the interface is not implemented by the type
	mov	rdi, rbx
	mov	rsi, 511
	mov	rdx, r12
	call	__chk_ty_raise
	xor	eax, eax
  .push:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	inc	qword [chkt_iCI]
	jmp	.gp
  .emit:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_list_emit
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rax
	call	__chk_ty_setd
  .out:
	return
endp

; __chk_ty_lambda(w, n) -> rax = a lambda's `fn` type.
;
; Spec §8.6 decision 2: parameter types are REQUIRED and the result is
; INFERRED from the first `redde` (checker.md section 2.3), so unlike a
; declared function this cannot be built before its body is typed. The row is
; always inferred too, so the type carries the EMPTY row and pass 3 re-interns
; it (`__chk_row_commit`).
proc __chk_ty_lambda, chkt_wLM, chkt_nLM
	uses	rbx, r12, r13, r14
	locals
		slot chkt_sLM, dq
		slot chkt_bLM, dq
		slot chkt_dLM, dq
		slot chkt_oLM, dq
		slot chkt_pLM, dq
		slot chkt_vR2, dq
		slot chkt_vF2, dq
		slot chkt_vI2, dq
		slot chkt_vN2, dq
		slot chkt_vD2, dq
	endl
	mov	rbx, [chkt_wLM]
	mov	r12, [chkt_nLM]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_sLM], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bLM], rcx
	mov	ecx, [rax + AstNode.d]
	mov	[chkt_dLM], rcx

	mov	rdi, rbx
	mov	rsi, [chkt_sLM]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_oLM], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_pLM], rcx
	mov	ecx, [rax + AstNode.c]
	mov	r14, rcx			; the declared result, or 0

	; the parameter types
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_list_mark
	mov	r13, rax
	xor	rcx, rcx
	mov	[chkt_vN2], rcx
  .param:
	mov	rax, [chkt_vN2]
	cmp	rax, [chkt_pLM]
	jae	.result
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_oLM]
	add	rsi, [chkt_vN2]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_param
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	inc	qword [chkt_vN2]
	jmp	.param

  .result:
	; save the enclosing function's state
	mov	eax, [rbx + ChkTyWork.wres]
	mov	[chkt_vR2], rax
	mov	eax, [rbx + ChkTyWork.wfnd]
	mov	[chkt_vF2], rax
	mov	eax, [rbx + ChkTyWork.winfer]
	mov	[chkt_vI2], rax
	mov	eax, [rbx + ChkTyWork.wredd]
	mov	[chkt_vD2], rax

	mov	eax, [chkt_dLM]
	mov	[rbx + ChkTyWork.wfnd], eax
	mov	dword [rbx + ChkTyWork.wredd], 0
	test	r14, r14
	jz	.infer
	mov	rdi, rbx
	mov	rsi, r14
	call	chk_ty_of_node
	mov	[rbx + ChkTyWork.wres], eax
	mov	dword [rbx + ChkTyWork.winfer], 0
	jmp	.body
  .infer:
	mov	dword [rbx + ChkTyWork.wres], 0
	mov	dword [rbx + ChkTyWork.winfer], 1
	mov	dword [rbx + ChkTyWork.winf], 0

  .body:
	mov	rdi, rbx
	mov	rsi, [chkt_bLM]
	call	__chk_ty_stmt

	; the inferred result: the first `redde`'s type, `unit` if there was none
	cmp	dword [rbx + ChkTyWork.winfer], 0
	je	.have_res
	mov	eax, [rbx + ChkTyWork.winf]
	test	eax, eax
	jnz	.set_res
	mov	rdi, rbx
	call	__chk_ty_unit
  .set_res:
	mov	[rbx + ChkTyWork.wres], eax
  .have_res:
	mov	r14d, [rbx + ChkTyWork.wres]
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r14
	call	ast_list_push
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_list_emit
	mov	r13, rax

	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, [chkt_pLM]
	call	__chk_ty_fn_empty
	mov	r13, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_dLM]
	mov	rdx, r13
	call	ast_decl_ty
	mov	rdi, rbx
	mov	rsi, [chkt_sLM]
	mov	rdx, r13
	call	__chk_ty_setty

	mov	rax, [chkt_vR2]
	mov	[rbx + ChkTyWork.wres], eax
	mov	rax, [chkt_vF2]
	mov	[rbx + ChkTyWork.wfnd], eax
	mov	rax, [chkt_vI2]
	mov	[rbx + ChkTyWork.winfer], eax
	mov	rax, [chkt_vD2]
	mov	[rbx + ChkTyWork.wredd], eax
	mov	rax, r13
	return
endp
