; compiler/x86_64/checker/types/sum.inc
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
; PASS 2, PART 5 -- SUM TYPES: constructors, constructor patterns,
; `discerne`'s exhaustiveness, and `?`. docs/design/sum-types.md D2's typing,
; D3 and D4 (the last at the end of this file).
; Included by types.inc; a section of that file, split for length, as
; member.inc and stmt.inc are.
;
; CONSTRUCTORS (D2). A variant is a module-scope name (pass 1's `__chk_mods`
; makes it visible, which is also why it collides with a constant as
; `EXS-E0302`), so `prosperum(n)` is a `Call` whose callee `Path` resolves to
; an `AST_D_VARIANT` declaration, and a payload-less `ordinata` is a bare
; `Path` to one. Its type is the variant's sum:
;
;   - a NON-GENERIC sum is its own type, whatever position the constructor is
;     in;
;   - a GENERIC sum needs its arguments. They come from an explicit
;     `prosperum<mensura, erratum>(n)` when written -- counted at both ends,
;     `EXS-E0304` otherwise, by `__chk_ty_decl_inst` exactly as in type
;     position -- and otherwise FROM THE EXPECTATION: a position that wants
;     an instance of this same sum (an annotation, a parameter, `redde`
;     against the function's result, an assignment target) supplies it,
;     through `__chk_ty_want`. A generic constructor with neither is
;     `EXS-E0304` at its name: the type arguments it needs are missing, which
;     is the same mistake as a bare `eventus` in type position. Nothing is
;     inferred from the payload: `prosperum(5)` alone says nothing about `E`,
;     and inferring half an instance would be a second feature (D4's
;     error-conversion bullet refuses the same trade).
;   - the payload count must equal the variant's (`EXS-E0304` at the call),
;     and each payload is typed against the declaration's element type with
;     the instance's arguments substituted (`ast_sum_subst`).
;
; PATTERNS (D2). Against a SUM scrutinee a `Path` pattern is looked up among
; THAT SUM'S variants FIRST, by name, and only when none matches does pass 1's
; value-namespace answer matter -- `EXS-E0303` if it named something (a
; constant, a variant of another sum), `EXS-E0301` if it named nothing (pass 1
; resolves patterns silently, `CHK_POS_PATTERN`). A local constant with a
; variant's name therefore does not capture the pattern: the ordering D2 calls
; "the subtle part". The binding count must equal the payload count
; (`EXS-E0304`); each binding takes its element's type and is ASSIGNED by the
; match (it is a `Binding` with no initializer, which `__chk_ty_defsetup`
; marks unassigned -- the read that was `EXS-E0307` in
; tests/unit/chk_pattern_scope.asm row 3). Against a SCALAR scrutinee a
; pattern is typed as before (as the right operand of `s eq p`), a variant
; there is `EXS-E0303`, a name that resolves to nothing is `EXS-E0301` (raised
; here now that pass 1 is silent on patterns), and a pattern with bindings
; that is not a variant is `EXS-E0303`.
;
; EXHAUSTIVENESS (D3), `EXS-E0351` at the scrutinee, and only when no `aliter`
; is written: a sum scrutinee must cover every variant, and the diagnostic
; carries a machine-applicable fix -- the missing arms, in declaration order,
; inserted before the `discerne`'s closing brace -- whose text NAMES them, so
; the rendered diagnostic lists them (§8.5); a scalar scrutinee needs
; `aliter`, and there is no fix, since the body is the author's. Duplicate and
; unreachable arms are NOT diagnosed (D3: `[OPEN]`, no code).
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.5, §8.6, §13;
; docs/design/sum-types.md D2, D3, D4, D5.
; -----------------------------------------------------------------------------

; __chk_ty_isctor(w, n) -> rax = the `AST_D_VARIANT` declaration expression `n`
; constructs -- a `Path` to a variant, or a `Call` whose callee is one -- or 0.
proc __chk_ty_isctor, chkt_wIC, chkt_nIC
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkt_wIC]
	mov	r12, [chkt_nIC]
	test	r12, r12
	jz	.no
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_CALL
	jne	.path
	mov	r12d, [rax + AstNode.a]
	test	r12, r12
	jz	.no
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.kind]
  .path:
	cmp	ecx, AST_PATH
	jne	.no
	mov	r12d, [rax + AstNode.d]
	test	r12d, r12d
	jz	.no
	test	r12d, CHK_TY_PRELUDE
	jnz	.no
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	cmp	byte [rax + AstDecl.kind], AST_D_VARIANT
	jne	.no
	mov	rax, r12
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_findvar(w, sumdecl, nameid) -> rax = the variant of sum `typus`
; `sumdecl` named `nameid`, rdx = its 0-based index (= its tag); rax = 0 when
; the sum has no variant of that name.
proc __chk_ty_findvar, chkt_wFV, chkt_sFV, chkt_mFV
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [chkt_wFV]
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_sFV]
	call	ast_sum_body
	test	rax, rax
	jz	.no
	mov	r13, rdx			; the variant count
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	r12d, [rax + AstNode.a]		; the `Variant` list
	xor	r14d, r14d
  .scan:
	cmp	r14, r13
	jae	.no
	mov	rdi, [rbx + ChkTyWork.wtre]
	lea	rsi, [r12 + r14]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	cmp	rcx, [chkt_mFV]
	je	.hit
	inc	r14
	jmp	.scan
  .hit:
	mov	eax, [rax + AstNode.d]
	mov	rdx, r14
	return
  .no:
	xor	eax, eax
	xor	edx, edx
	return
endp

; __chk_ty_payload(w, vdecl) -> rax = the `extra` offset of variant `vdecl`'s
; payload type nodes, rdx = their count.
proc __chk_ty_payload, chkt_wPY, chkt_vPY
	uses	rbx
	locals
	endl
	mov	rbx, [chkt_wPY]
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_vPY]
	call	ast_decl_at
	mov	esi, [rax + AstDecl.node]
	test	esi, esi
	jz	.none
	mov	rdi, rbx
	call	__chk_ty_node
	mov	edx, [rax + AstNode.c]
	mov	eax, [rax + AstNode.b]
	return
  .none:
	xor	eax, eax
	xor	edx, edx
	return
endp

; __chk_ty_elemty(w, sty, off, i) -> rax = payload element `i`'s type read
; through instance `sty`: the payload type node's own type (pass 2 typed it
; in the declaration, `__chk_ty_sumbody`) with the instance's arguments
; substituted.
proc __chk_ty_elemty, chkt_wET, chkt_tET, chkt_oET, chkt_iET
	uses	rbx
	locals
	endl
	mov	rbx, [chkt_wET]
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_oET]
	add	rsi, [chkt_iET]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_ty
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_tET]
	mov	rdx, rax
	call	ast_sum_subst
	return
endp

; __chk_ty_instok(w, sty, sdecl) -> eax = 1 if `sty` is a sum type through
; which payload element types can be read: a non-generic sum, or an INSTANCE
; of a generic one. The bare declaration type of a generic sum (`b` = 0) is
; not -- its elements are generic parameters, and typing a payload against one
; would raise a mismatch the program did not make.
proc __chk_ty_instok, chkt_wIO, chkt_tIO, chkt_dIO
	uses	rbx
	locals
	endl
	mov	rbx, [chkt_wIO]
	mov	rdi, rbx
	mov	rsi, [chkt_tIO]
	call	__chk_ty_kindof
	cmp	rax, AST_TY_SUM
	jne	.no
	mov	rdi, rbx
	mov	rsi, [chkt_tIO]
	mov	rdx, AstType.b
	call	__chk_ty_field
	test	rax, rax
	jnz	.yes
	mov	rdi, rbx
	mov	rsi, [chkt_dIO]
	call	__chk_ty_ngen
	test	rax, rax
	jnz	.no
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_ctor(w, n, want) -> rax = the type of constructor expression `n`
; (`__chk_ty_isctor` said it is one). `want` is the expected type, 0 when the
; position supplies none. This file's header gives the rules.
proc __chk_ty_ctor, chkt_wCT, chkt_nCT, chkt_qCT
	uses	rbx, r12, r13, r14
	locals
		slot chkt_pCT,  dq	; the callee `Path`
		slot chkt_aoCT, dq	; the argument list
		slot chkt_anCT, dq	; the argument count
		slot chkt_vdCT, dq	; the variant declaration
		slot chkt_sdCT, dq	; its sum `typus`
		slot chkt_t0CT, dq	; the sum's declaration type
		slot chkt_ngCT, dq	; its generic parameter count
		slot chkt_sgCT, dq	; the path's first `Seg`
		slot chkt_tyCT, dq	; the answer
		slot chkt_okCT, dq	; payloads can be typed through it
		slot chkt_poCT, dq	; the payload type nodes
		slot chkt_pnCT, dq	; their count
		slot chkt_iCT,  dq
	endl
	mov	rbx, [chkt_wCT]
	mov	r12, [chkt_nCT]
	mov	qword [chkt_aoCT], 0
	mov	qword [chkt_anCT], 0
	mov	[chkt_pCT], r12
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_CALL
	jne	.havepath
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_pCT], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_aoCT], rcx
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_anCT], rcx
  .havepath:
	mov	rdi, rbx
	mov	rsi, [chkt_pCT]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.d]
	mov	[chkt_vdCT], rcx
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rcx
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.parent]
	mov	[chkt_sdCT], rcx
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_ty_typus
	mov	[chkt_t0CT], rax
	mov	rdi, rbx
	mov	rsi, [chkt_sdCT]
	call	__chk_ty_ngen
	mov	[chkt_ngCT], rax
	mov	rdi, rbx
	mov	rsi, [chkt_pCT]
	call	__chk_ty_seg0
	mov	[chkt_sgCT], rax

	; ---- which instance ----
	mov	rdi, rbx
	mov	rsi, [chkt_sgCT]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]		; `Seg.b`: explicit arguments
	test	ecx, ecx
	jz	.noexplicit
	mov	rdi, rbx
	mov	rsi, [chkt_sdCT]
	mov	rdx, [chkt_sgCT]
	call	__chk_ty_decl_inst
	jmp	.have
  .noexplicit:
	mov	rax, [chkt_t0CT]
	cmp	qword [chkt_ngCT], 0
	je	.have
	mov	rax, [chkt_qCT]
	cmp	rax, 1
	je	.have				; the error type: already said
	test	rax, rax
	jz	.missing
	mov	rdi, rbx
	mov	rsi, [chkt_qCT]
	call	__chk_ty_kindof
	cmp	rax, AST_TY_SUM
	jne	.other
	mov	rdi, rbx
	mov	rsi, [chkt_qCT]
	mov	rdx, AstType.a
	call	__chk_ty_field
	cmp	rax, [chkt_sdCT]
	jne	.other
	mov	rax, [chkt_qCT]
	jmp	.have
  .other:
	; an expectation of some other type: answer the sum's own type and let
	; the caller's comparison say `EXS-E0303`, once
	mov	rax, [chkt_t0CT]
	jmp	.have
  .missing:
	mov	rdi, rbx
	mov	rsi, 304
	mov	rdx, [chkt_sgCT]
	call	__chk_ty_raise
	mov	rdi, rbx
	call	__chk_ty_err
  .have:
	mov	[chkt_tyCT], rax
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_sdCT]
	call	__chk_ty_instok
	mov	[chkt_okCT], rax

	; ---- the payload ----
	mov	rdi, rbx
	mov	rsi, [chkt_vdCT]
	call	__chk_ty_payload
	mov	[chkt_poCT], rax
	mov	[chkt_pnCT], rdx
	mov	rax, [chkt_anCT]
	cmp	rax, [chkt_pnCT]
	je	.args
	mov	rdi, rbx
	mov	rsi, 304
	mov	rdx, r12
	call	__chk_ty_raise
  .args:
	mov	qword [chkt_iCT], 0
  .arg:
	mov	rax, [chkt_iCT]
	cmp	rax, [chkt_anCT]
	jae	.done
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_aoCT]
	add	rsi, [chkt_iCT]
	call	ast_extra_at
	mov	r13, rax			; the argument node
	cmp	qword [chkt_okCT], 0
	je	.quiet
	mov	rax, [chkt_iCT]
	cmp	rax, [chkt_pnCT]
	jae	.quiet
	mov	rdi, rbx
	mov	rsi, [chkt_tyCT]
	mov	rdx, [chkt_poCT]
	mov	rcx, [chkt_iCT]
	call	__chk_ty_elemty
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, rax
	call	__chk_ty_want
	jmp	.next
  .quiet:
	; no element type to judge it by -- the arity or the instance has
	; already been reported -- so it is typed, and a literal with no width
	; is given the error type SILENTLY: one diagnostic per mistake, not an
	; `EXS-E0308` for a width nobody could have supplied
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_expr
	test	rax, rax
	jnz	.next
	mov	rdi, rbx
	call	__chk_ty_err
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, rax
	call	__chk_ty_setty
  .next:
	inc	qword [chkt_iCT]
	jmp	.arg
  .done:
	mov	rdi, rbx
	mov	rsi, [chkt_sgCT]
	mov	rdx, [chkt_tyCT]
	call	__chk_ty_setty
	mov	rdi, rbx
	mov	rsi, [chkt_pCT]
	mov	rdx, [chkt_tyCT]
	call	__chk_ty_setty
	mov	rax, [chkt_tyCT]
	return
endp

; __chk_ty_bindto(w, bnode, t) -- a pattern binding takes type `t` and is
; ASSIGNED by the match: `Decl.ty`, `Node.ty`, and the definite-assignment
; state, which `__chk_ty_defsetup` left unassigned for a `Binding` with no
; initializer.
proc __chk_ty_bindto, chkt_wBT, chkt_nBT, chkt_tBT
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkt_wBT]
	mov	rdi, rbx
	mov	rsi, [chkt_nBT]
	mov	rdx, [chkt_tBT]
	call	__chk_ty_setty
	mov	rdi, rbx
	mov	rsi, [chkt_nBT]
	call	__chk_ty_node
	mov	r12d, [rax + AstNode.d]
	test	r12d, r12d
	jz	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	mov	rdx, [chkt_tBT]
	call	ast_decl_ty
	lea	rdi, [rbx + ChkTyWork.wdef]
	mov	rsi, r12
	call	vec_get
	mov	byte [rax], CHK_TY_D_SET
  .out:
	return
endp

; __chk_ty_pattern(w, casus, sty, cov) -- type one arm's pattern and bindings
; against scrutinee type `sty`; for a sum scrutinee, mark the covered variant
; in the byte array `cov` (one byte per variant, in declaration order).
proc __chk_ty_pattern, chkt_wPT, chkt_cPT, chkt_tPT, chkt_vPT
	uses	rbx, r12, r13, r14
	locals
		slot chkt_pPT,  dq	; the pattern
		slot chkt_boPT, dq	; the bindings
		slot chkt_bnPT, dq	; their count
		slot chkt_sdPT, dq	; the scrutinee's sum declaration, or 0
		slot chkt_sgPT, dq	; the pattern's first `Seg`
		slot chkt_vdPT, dq	; the variant it names
		slot chkt_poPT, dq
		slot chkt_pnPT, dq
		slot chkt_iPT,  dq
	endl
	mov	rbx, [chkt_wPT]
	mov	rdi, rbx
	mov	rsi, [chkt_cPT]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_pPT], rcx
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_boPT], rcx
	mov	ecx, [rax + AstNode.d]
	mov	[chkt_bnPT], rcx
	mov	qword [chkt_sdPT], 0
	mov	rdi, rbx
	mov	rsi, [chkt_tPT]
	call	__chk_ty_kindof
	cmp	rax, AST_TY_SUM
	jne	.kind
	mov	rdi, rbx
	mov	rsi, [chkt_tPT]
	mov	rdx, AstType.a
	call	__chk_ty_field
	mov	[chkt_sdPT], rax
  .kind:
	mov	rdi, rbx
	mov	rsi, [chkt_pPT]
	call	__chk_ty_kind
	cmp	rax, AST_PATH
	jne	.literal
	mov	rdi, rbx
	mov	rsi, [chkt_pPT]
	call	__chk_ty_seg0
	mov	[chkt_sgPT], rax
	mov	r13, rdx			; the segment count
	cmp	qword [chkt_sdPT], 0
	je	.scalar_path

	; ---- a sum scrutinee: the sum's own variants first ----
	cmp	r13, 1
	jne	.notvariant
	mov	rdi, rbx
	mov	rsi, [chkt_sgPT]
	call	__chk_ty_node
	mov	edx, [rax + AstNode.a]		; the name
	mov	rdi, rbx
	mov	rsi, [chkt_sdPT]
	call	__chk_ty_findvar
	test	rax, rax
	jz	.notvariant
	mov	[chkt_vdPT], rax
	mov	r14, rdx			; the variant's index
	mov	rcx, [chkt_vPT]
	test	rcx, rcx
	jz	.nocov
	mov	byte [rcx + r14], 1
  .nocov:
	mov	rdi, rbx
	mov	rsi, [chkt_sgPT]
	mov	rdx, [chkt_vdPT]
	call	__chk_ty_setd
	mov	rdi, rbx
	mov	rsi, [chkt_pPT]
	mov	rdx, [chkt_vdPT]
	call	__chk_ty_setd
	mov	rdi, rbx
	mov	rsi, [chkt_sgPT]
	mov	rdx, [chkt_tPT]
	call	__chk_ty_setty
	mov	rdi, rbx
	mov	rsi, [chkt_pPT]
	mov	rdx, [chkt_tPT]
	call	__chk_ty_setty
	mov	rdi, rbx
	mov	rsi, [chkt_vdPT]
	call	__chk_ty_payload
	mov	[chkt_poPT], rax
	mov	[chkt_pnPT], rdx
	mov	rax, [chkt_bnPT]
	cmp	rax, [chkt_pnPT]
	je	.bind_typed
	mov	rdi, rbx
	mov	rsi, 304
	mov	rdx, [chkt_pPT]
	call	__chk_ty_raise
  .bind_typed:
	mov	qword [chkt_iPT], 0
  .bt_loop:
	mov	rax, [chkt_iPT]
	cmp	rax, [chkt_bnPT]
	jae	.out
	cmp	rax, [chkt_pnPT]
	jae	.bt_err
	mov	rdi, rbx
	mov	rsi, [chkt_tPT]
	mov	rdx, [chkt_poPT]
	mov	rcx, [chkt_iPT]
	call	__chk_ty_elemty
	jmp	.bt_have
  .bt_err:
	mov	rdi, rbx
	call	__chk_ty_err
  .bt_have:
	mov	r14, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_boPT]
	add	rsi, [chkt_iPT]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r14
	call	__chk_ty_bindto
	inc	qword [chkt_iPT]
	jmp	.bt_loop

  .notvariant:
	; not one of the scrutinee's variants: what did pass 1 find?
	mov	rdi, rbx
	mov	rsi, [chkt_pPT]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.d]
	test	ecx, ecx
	jz	.unres
	mov	rdi, rbx
	mov	rsi, 303
	mov	rdx, [chkt_pPT]
	call	__chk_ty_raise
	jmp	.path_err
  .unres:
	mov	rdi, rbx
	mov	rsi, 301
	mov	rdx, [chkt_sgPT]
	call	__chk_ty_raise
  .path_err:
	mov	rdi, rbx
	call	__chk_ty_err
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, [chkt_sgPT]
	mov	rdx, r14
	call	__chk_ty_setty
	mov	rdi, rbx
	mov	rsi, [chkt_pPT]
	mov	rdx, r14
	call	__chk_ty_setty
	jmp	.bind_err

  .scalar_path:
	mov	rdi, rbx
	mov	rsi, [chkt_pPT]
	call	__chk_ty_node
	mov	r14d, [rax + AstNode.d]
	test	r14d, r14d
	jz	.unres
	test	r14d, CHK_TY_PRELUDE
	jnz	.literal
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r14
	call	ast_decl_at
	cmp	byte [rax + AstDecl.kind], AST_D_VARIANT
	jne	.literal
	; a variant against a scalar: a value of a sum is not an integer
	cmp	qword [chkt_tPT], 1
	je	.path_err			; the error type: already said
	mov	rdi, rbx
	mov	rsi, 303
	mov	rdx, [chkt_pPT]
	call	__chk_ty_raise
	jmp	.path_err

  .literal:
	; a literal, or a path to a constant: the right operand of `s eq p`
	mov	rdi, rbx
	mov	rsi, [chkt_pPT]
	mov	rdx, [chkt_tPT]
	call	__chk_ty_want
	cmp	qword [chkt_bnPT], 0
	je	.out
	cmp	qword [chkt_tPT], 1
	je	.bind_err
	; bindings on a pattern that is not a variant: nothing to bind them to
	mov	rdi, rbx
	mov	rsi, 303
	mov	rdx, [chkt_pPT]
	call	__chk_ty_raise
  .bind_err:
	mov	qword [chkt_iPT], 0
  .be_loop:
	mov	rax, [chkt_iPT]
	cmp	rax, [chkt_bnPT]
	jae	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_boPT]
	add	rsi, [chkt_iPT]
	call	ast_extra_at
	mov	r14, rax
	mov	rdi, rbx
	call	__chk_ty_err
	mov	rdi, rbx
	mov	rsi, r14
	mov	rdx, rax
	call	__chk_ty_bindto
	inc	qword [chkt_iPT]
	jmp	.be_loop
  .out:
	return
endp

; __chk_ty_covalloc(w, sty) -> rax = a zeroed byte per variant of sum type
; `sty` (from the checker's scratch arena; one `discerne`'s coverage), rdx =
; the variant count. rax = 0 when `sty` is not a sum.
proc __chk_ty_covalloc, chkt_wCA2, chkt_tCA2
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wCA2]
	mov	rdi, rbx
	mov	rsi, [chkt_tCA2]
	call	__chk_ty_kindof
	cmp	rax, AST_TY_SUM
	jne	.no
	mov	rdi, rbx
	mov	rsi, [chkt_tCA2]
	mov	rdx, AstType.a
	call	__chk_ty_field
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_sum_body
	test	rax, rax
	jz	.no
	mov	r12, rdx
	mov	rdi, [rbx + ChkTyWork.wcx]
	mov	rdi, [rdi + ChkCtx.arena]
	lea	rsi, [r12 + 1]
	call	arena_alloc
	mov	r13, rax
	mov	rdi, rax
	lea	rcx, [r12 + 1]
	xor	eax, eax
	cld
	rep	stosb
	mov	rax, r13
	mov	rdx, r12
	return
  .no:
	xor	eax, eax
	xor	edx, edx
	return
endp

; __chk_ty_putdec(p, v) -> rax = `p` advanced past the decimal digits of `v`.
proc __chk_ty_putdec, chkt_pPD, chkt_vPD
	uses	rbx
	locals
		slot chkt_bPD, 24
	endl
	mov	rax, [chkt_vPD]
	lea	rbx, [chkt_bPD + 24]
	mov	ecx, 10
  .dig:
	xor	edx, edx
	div	rcx
	add	dl, '0'
	dec	rbx
	mov	[rbx], dl
	test	rax, rax
	jnz	.dig
	mov	rdi, [chkt_pPD]
	lea	rcx, [chkt_bPD + 24]
	sub	rcx, rbx
	mov	rsi, rbx
	cld
	rep	movsb
	mov	rax, rdi
	return
endp

chk_sum_lit_casus	db 'casus '
CHK_SUM_LIT_CASUS_LEN = $ - chk_sum_lit_casus
chk_sum_lit_arm		db ' { }', 10
CHK_SUM_LIT_ARM_LEN = $ - chk_sum_lit_arm

; __chk_ty_exhaust(w, n, sty, cov) -- D3 for `Discerne` node `n`, whose
; scrutinee has type `sty`, `cov` that sum's coverage (or 0 for a scalar).
; Called only when no `aliter` was written. `EXS-E0351` at the scrutinee;
; for a sum, only when a variant is uncovered, and with the missing arms as an
; insertion fix, each `casus NAME(v1, ..., vk) { }` -- the binding names are
; placeholders the author renames, numbered so that a payload of any arity
; gets distinct ones, and the arm count matches the payload count, so the fix
; applied is a program that checks (bodies empty).
proc __chk_ty_exhaust, chkt_wEX, chkt_nEX, chkt_tEX, chkt_cEX
	uses	rbx, r12, r13, r14
	locals
		slot chkt_sEX,  dq	; the scrutinee node
		slot chkt_vlEX, dq	; the `Variant` list
		slot chkt_vnEX, dq	; its count
		slot chkt_iEX,  dq
		slot chkt_lnEX, dq	; the fix text's capacity
		slot chkt_bfEX, dq	; ... and its start
		slot chkt_dgEX, dq	; the diagnostic
		slot chkt_kEX,  dq
	endl
	mov	rbx, [chkt_wEX]
	mov	rdi, rbx
	mov	rsi, [chkt_nEX]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_sEX], rcx
	cmp	qword [chkt_tEX], 1
	je	.out				; the error type: already said
	cmp	qword [chkt_tEX], 0
	je	.out
	cmp	qword [chkt_cEX], 0
	jne	.sum
	mov	rdi, rbx
	mov	rsi, 351
	mov	rdx, [chkt_sEX]
	call	__chk_ty_raise
	jmp	.out

  .sum:
	mov	rdi, rbx
	mov	rsi, [chkt_tEX]
	mov	rdx, AstType.a
	call	__chk_ty_field
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_sum_body
	mov	[chkt_vnEX], rdx
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_vlEX], rcx
	; a first pass: is anything missing, and how long is the fix?
	mov	qword [chkt_lnEX], 0
	xor	r12d, r12d
  .size:
	cmp	r12, [chkt_vnEX]
	jae	.sized
	mov	rcx, [chkt_cEX]
	cmp	byte [rcx + r12], 0
	jne	.size_next
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_vlEX]
	add	rsi, r12
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.c]		; the payload count
	mov	esi, [rax + AstNode.a]		; the name
	mov	rdi, rbx
	call	__chk_ty_bytes
	; `casus ` + name + ` { }\n` + `(` `)` + per element `v` digits `, `
	lea	rax, [rdx + CHK_SUM_LIT_CASUS_LEN + CHK_SUM_LIT_ARM_LEN + 2]
	imul	rcx, r13, 16
	add	rax, rcx
	add	[chkt_lnEX], rax
  .size_next:
	inc	r12
	jmp	.size
  .sized:
	cmp	qword [chkt_lnEX], 0
	je	.out				; every variant covered

	mov	rdi, rbx
	mov	rsi, 351
	mov	rdx, [chkt_sEX]
	call	__chk_ty_raise
	mov	[chkt_dgEX], rax
	test	rax, rax
	jz	.out
	; the text, from the TREE's arena: the driver renders after the
	; checker returns (resolve.inc's `__chk_fix_text` says why not scratch)
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rdi, [rdi + Ast.arena]
	mov	rsi, [chkt_lnEX]
	call	arena_alloc
	mov	[chkt_bfEX], rax
	mov	r14, rax			; the write cursor
	xor	r12d, r12d
  .text:
	cmp	r12, [chkt_vnEX]
	jae	.texted
	mov	rcx, [chkt_cEX]
	cmp	byte [rcx + r12], 0
	jne	.text_next
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_vlEX]
	add	rsi, r12
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.c]
	mov	esi, [rax + AstNode.a]
	mov	rdi, rbx
	call	__chk_ty_bytes
	mov	[chkt_kEX], rdx
	mov	rdi, r14
	lea	rsi, [chk_sum_lit_casus]
	mov	rcx, CHK_SUM_LIT_CASUS_LEN
	cld
	rep	movsb
	mov	rsi, rax
	mov	rcx, [chkt_kEX]
	rep	movsb
	mov	r14, rdi
	test	r13, r13
	jz	.arm
	mov	byte [r14], '('
	inc	r14
	mov	qword [chkt_iEX], 1
  .bind:
	mov	byte [r14], 'v'
	inc	r14
	mov	rdi, r14
	mov	rsi, [chkt_iEX]
	call	__chk_ty_putdec
	mov	r14, rax
	mov	rax, [chkt_iEX]
	cmp	rax, r13
	jae	.close
	mov	byte [r14], ','
	mov	byte [r14 + 1], ' '
	add	r14, 2
	inc	qword [chkt_iEX]
	jmp	.bind
  .close:
	mov	byte [r14], ')'
	inc	r14
  .arm:
	mov	rdi, r14
	lea	rsi, [chk_sum_lit_arm]
	mov	rcx, CHK_SUM_LIT_ARM_LEN
	cld
	rep	movsb
	mov	r14, rdi
  .text_next:
	inc	r12
	jmp	.text
  .texted:
	; inserted just before the `discerne`'s closing brace -- the last byte
	; of its span
	mov	r12, [chkt_dgEX]
	mov	rax, [chkt_bfEX]
	mov	[r12 + ChkDiag.ftxt], rax
	mov	rcx, r14
	sub	rcx, rax
	mov	[r12 + ChkDiag.ftxtn], rcx
	mov	dword [r12 + ChkDiag.fkind], DIAG_FIX_INSERT
	mov	rdi, rbx
	mov	rsi, [chkt_nEX]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.span.file_id]
	mov	[r12 + ChkDiag.fx.file_id], ecx
	mov	ecx, [rax + AstNode.span.start]
	add	ecx, [rax + AstNode.span.len]
	dec	ecx
	mov	[r12 + ChkDiag.fx.start], ecx
	mov	dword [r12 + ChkDiag.fx.len], 0
  .out:
	return
endp

; =============================================================================
; `?` -- docs/design/sum-types.md D4
; =============================================================================
; `e?` with `e : eventus<T, E>`, inside a function whose declared result is
; `eventus<U, E>` with the SAME `E`, has type `T`: it is the `prosperum`
; payload, and otherwise the function returns `e`'s `adversum` (the lowering's
; half, lower/sum.inc's `__lwr_try`). "The same `E`" is interned type
; identity -- the id comparison every other type rule here makes; there is no
; conversion (D4: `[OPEN]`, it needs a trait and §7's dictionary passing).
;
; THREE REFUSALS, three registered codes, none new (§13 untouched):
;
;   - the operand is not an `eventus` instance           `EXS-E0305`
;     (D4: the code this arm always raised for that half; "operation not
;     defined on the type" is exactly what it is)
;   - the enclosing function's result is not an `eventus` `EXS-E0307`
;     (D4's proposal, taken: control flow misuse, the code `rumpe` outside a
;     loop gets -- a construct that transfers control used where the
;     transfer has nowhere to go). That includes module level (no function)
;     and a lambda whose result is still being INFERRED with no `redde`
;     before the `?` (`winfer` and no `winf`: there is no result type yet for
;     the `adversum` to be returned as, so `wres` is 0).
;   - both are `eventus`, with different `E`             `EXS-E0303`
;     ("type mismatch", §13's general typing code): the `adversum` value of
;     type `E` would be returned where the function's result carries a
;     different error type. It is the mismatch a `redde adversum(e)` with
;     that `e` would raise, met one token earlier, so it keeps that code --
;     D2's reuse argument, and §13's record for `EXS-E0510`.
;
; Every refusal is AT THE `Try` NODE, and after E0307 or E0303 the answer is
; still `T`: the operand's own payload type is not in doubt, so nothing above
; the `?` cascades. Only E0305 answers the error type -- there is no `T`.
; The ERROR TYPE anywhere (operand, result, either `E`) is silent: already
; said.
;
; `?` does NOT count as a `redde` for "a non-`unit` function that can fall
; off its end" (`EXS-E0307`, `__chk_ty_body`): it returns on one path only,
; and the success path still needs one.

; __chk_ty_try(w, n) -> rax = the type of `Try` node `n`.
proc __chk_ty_try, chkt_wTR, chkt_nTR
	uses	rbx, r12, r13, r14
	locals
		slot chkt_tTR, dq	; T
	endl
	mov	rbx, [chkt_wTR]
	mov	r12, [chkt_nTR]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	esi, [rax + AstNode.a]		; the operand
	mov	rdi, rbx
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r12
	call	__chk_ty_settled
	cmp	rax, 1
	je	.err				; the error type: already said
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_eventus_args
	test	eax, eax
	jz	.notev
	mov	[chkt_tTR], rax
	mov	r13d, edx			; E
	; ---- the enclosing function's result ----
	mov	r14d, [rbx + ChkTyWork.wres]
	cmp	r14, 1
	je	.ok
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r14
	call	ast_eventus_args
	test	eax, eax
	jz	.nowhere
	cmp	edx, r13d
	je	.ok
	cmp	edx, 1
	je	.ok
	cmp	r13d, 1
	je	.ok
	mov	rdi, rbx
	mov	rsi, 303
	mov	rdx, r12
	call	__chk_ty_raise
	jmp	.ok
  .nowhere:
	mov	rdi, rbx
	mov	rsi, 307
	mov	rdx, r12
	call	__chk_ty_raise
  .ok:
	mov	rax, [chkt_tTR]
	return
  .notev:
	mov	rdi, rbx
	mov	rsi, 305
	mov	rdx, r12
	call	__chk_ty_raise
  .err:
	mov	rdi, rbx
	call	__chk_ty_err
	return
endp
