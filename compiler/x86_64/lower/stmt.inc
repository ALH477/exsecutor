; compiler/x86_64/lower/stmt.inc
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
; Statements: every spec section 8.5 construct, its block diagram and its seal
; order, plus scope exit and ARC -- docs/design/lowering.md sections 2.5 and
; 2.6.
;
; ONE CORRECTION TO THAT DOCUMENT, found by running it. Section 2.5 writes
; `seal(body1) at creation` for a `si` arm and `seal(test2) at creation` for
; its next test. A block whose predecessor list is still EMPTY cannot be
; sealed: `readVariableRecursive`'s sealed arm has a case for one predecessor
; and a case for several, and none for none. Every block here is therefore
; sealed IMMEDIATELY AFTER the terminator that created the edge into it --
; the same point in the walk, and not the same sentence. tests/unit/lwr_ssa.asm
; records it too.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md sections 5.4, 6.3, 6.6, 8.5.
; -----------------------------------------------------------------------------

include 'expr.inc'

; ============================================================================
; the loop stack (`rumpe` / `perge`)
; ============================================================================

; lwr_loop_push(st, lwrex, lwrco, lwrdp) -- `lwrdp` is the frame depth at loop
; entry: `rumpe` and `perge` release every frame from the current one down to
; and including the loop body's, which is frame `lwrdp`.
proc lwr_loop_push, lwrsp, lwrex, lwrco, lwrdp
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.lps]
	call	vec_push
	mov	ecx, [lwrex]
	mov	[rax + LwrLoop.lexit], ecx
	mov	ecx, [lwrco]
	mov	[rax + LwrLoop.lcont], ecx
	mov	ecx, [lwrdp]
	mov	[rax + LwrLoop.ldep], ecx
	mov	dword [rax + LwrLoop.lpad], 0
	return
endp

proc lwr_loop_pop, lwrsp
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.lps]
	mov	rax, [rdi + Vec.len]
	rassert	rax ge 1
	dec	rax
	mov	[rdi + Vec.len], rax
	return
endp

; lwr_loop_top(st) -> rax = the innermost LwrLoop. Labelled `rumpe`/`perge`
; are `[OPEN]` in spec section 8.6, so the innermost loop is the only target
; there is; a `rumpe` outside a loop is `EXS-E0307`, the checker's.
proc lwr_loop_top, lwrsp
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.lps]
	mov	rax, [rdi + Vec.len]
	rassert	rax ge 1
	dec	rax
	mov	rsi, rax
	call	vec_get
	return
endp

; __lwr_release_to(st, lwrdp) -- release every open frame from the innermost
; down to frame `lwrdp` inclusive, without popping any of them. `redde` passes
; 0 (every frame, to the function root); `rumpe`/`perge` pass the loop's.
proc __lwr_release_to, lwrsp, lwrdp
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	call	lwr_frame_depth
	mov	r12, rax
  .loop:
	cmp	r12, [lwrdp]
	jbe	.done
	dec	r12
	mov	rdi, rbx
	mov	rsi, r12
	call	lwr_frame_release
	jmp	.loop
  .done:
	return
endp

; ============================================================================
; blocks and the statement dispatch
; ============================================================================

; lwr_block(st, lwrn) -- an `AST_BLOCK`: one scope frame, the statements in
; order, then the frame's releases on the fall-through path and the pop.
proc lwr_block, lwrsp, lwrn
	uses	rbx, r12
	locals
		slot lwroff, dd
		slot lwrcnt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwroff], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrcnt], ecx

	mov	rdi, rbx
	call	lwr_frame_push
	xor	r12d, r12d
  .loop:
	cmp	r12d, [lwrcnt]
	jge	.done
	mov	rdi, rbx
	mov	esi, [lwroff]
	mov	edx, r12d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_stmt
	inc	r12d
	jmp	.loop
  .done:
	; fall-through at `}` releases this block's own frame and nothing else;
	; a dead `cur` emits nothing and the frame is popped unreleased
	mov	rdi, rbx
	call	lwr_frame_depth
	dec	rax
	mov	rdi, rbx
	mov	rsi, rax
	call	lwr_frame_release
	mov	rdi, rbx
	call	lwr_frame_pop
	return
endp

; lwr_stmt(st, lwrn) -- one statement. A statement on a DEAD `cur` is skipped
; entirely and its expressions are never evaluated, which is what makes a join
; with no predecessor impossible rather than merely unlikely.
proc lwr_stmt, lwrsp, lwrn
	uses	rbx
	locals
		slot lwrk, dd
	endl
	mov	rbx, [lwrsp]
	cmp	dword [rbx + LwrState.cur], 0
	je	.dead
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nkind
	mov	[lwrk], eax

	cmp	eax, AST_BINDING
	je	.binding
	cmp	eax, AST_SUB
	je	.sub
	cmp	eax, AST_REDDE
	je	.redde
	cmp	eax, AST_EXPRSTMT
	je	.exprstmt
	cmp	eax, AST_ASSIGN
	je	.assign
	cmp	eax, AST_BLOCK
	je	.block
	cmp	eax, AST_SI
	je	.si
	cmp	eax, AST_DUM
	je	.dum
	cmp	eax, AST_FOR
	je	.for
	cmp	eax, AST_DISCERNE
	je	.discerne
	cmp	eax, AST_RUMPE
	je	.rumpe
	cmp	eax, AST_PERGE
	je	.perge
	rassert	eax eq 0
  .binding:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_binding
	return
  .sub:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_sub
	return
  .redde:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_redde
	return
  .exprstmt:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_exprstmt
	return
  .assign:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_assign
	return
  .block:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	lwr_block
	return
  .si:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_si
	return
  .dum:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_dum
	return
  .for:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_for
	return
  .discerne:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_discerne
	return
  .rumpe:
	mov	rdi, rbx
	call	lwr_loop_top
	mov	ecx, [rax + LwrLoop.lexit]
	mov	edx, [rax + LwrLoop.ldep]
	push	rcx
	mov	rdi, rbx
	mov	rsi, rdx
	call	__lwr_release_to
	pop	rcx
	mov	rdi, rbx
	mov	esi, ecx
	call	lwr_jmp
	return
  .perge:
	mov	rdi, rbx
	call	lwr_loop_top
	mov	ecx, [rax + LwrLoop.lcont]
	mov	edx, [rax + LwrLoop.ldep]
	push	rcx
	mov	rdi, rbx
	mov	rsi, rdx
	call	__lwr_release_to
	pop	rcx
	mov	rdi, rbx
	mov	esi, ecx
	call	lwr_jmp
	return
  .dead:
	return
endp

; __lwr_is_refty(st, lwrt) -> eax = 1 for `refero<T>` / `refero_communis<T>`,
; the two kinds scope exit has to release.
proc __lwr_is_refty, lwrsp, lwrt
	locals
	endl
	mov	rdi, [lwrsp]
	mov	esi, [lwrt]
	call	lwr_ty_kind
	cmp	eax, AST_TY_REF
	je	.yes
	cmp	eax, AST_TY_REFC
	je	.yes
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __lwr_retain_if_borrowed(st, lwrinit, lwrt, lwrv) -- spec section 6.3's
; rule, as CHK 2.5 recorded it: `own[init]` says whether the initializer
; YIELDS an owned reference (a call result, IR 2.9) or a borrowed one (a
; parameter, another binding). Owned: the binding takes the reference, no
; retain. Borrowed: retain first.
proc __lwr_retain_if_borrowed, lwrsp, lwrinit, lwrt, lwrv
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	__lwr_is_refty
	test	eax, eax
	jz	.no
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrinit]
	call	ast_own_get
	test	eax, eax
	jnz	.no
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_ir
	mov	rdi, rbx
	mov	esi, BFA_OP_RETAIN
	xor	rdx, rdx
	mov	ecx, eax
	mov	r8d, [lwrv]
	xor	r9, r9
	call	lwr_emit
  .no:
	return
endp

; ============================================================================
; declarations and simple statements
; ============================================================================

; __lwr_binding(st, lwrn) -- `firma x [: T] [= e];` / `mutabilis ...`.
;
; A binding is an SSA variable iff its type is scalar and the checker did NOT
; set `AST_F_MEMRES`; that flag is READ, never re-derived (CHK 2.5 sets it for
; "non-scalar type or address taken"). A memory-resident binding gets its
; `slot` at its declaration point in the current block -- runtime.md section 5
; shows exactly that, not a hoist to the entry block.
proc __lwr_binding, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrd, dd
		slot lwrt, dd
		slot lwrinit, dd
		slot lwrfl, dd
		slot lwrs, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.b]
	mov	[lwrinit], ecx
	mov	ecx, [rax + AstNode.d]
	mov	[lwrd], ecx
	rassert	ecx ne 0

	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.ty]
	mov	[lwrt], ecx
	rassert	ecx ne 0
	movzx	ecx, byte [rax + AstDecl.flags]
	mov	[lwrfl], ecx

	test	ecx, AST_F_MEMRES
	jnz	.memres

	; ---- an SSA variable ----
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_ir
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, LWR_VK_SSA
	mov	ecx, eax
	xor	r8, r8
	call	lwr_var_set
	cmp	dword [lwrinit], 0
	je	.reg
	mov	rdi, rbx
	mov	esi, [lwrinit]
	call	lwr_expr
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrinit]
	mov	edx, [lwrt]
	mov	ecx, r12d
	call	__lwr_retain_if_borrowed
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, [rbx + LwrState.cur]
	mov	ecx, r12d
	call	lwr_write_var
	jmp	.reg

  .memres:
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_slot
	mov	[lwrs], eax
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_ir
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, LWR_VK_SLOT
	mov	ecx, r13d
	mov	r8d, [lwrs]
	call	lwr_var_set
	cmp	dword [lwrinit], 0
	je	.reg
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.intoslot
	mov	rdi, rbx
	mov	esi, [lwrinit]
	call	lwr_expr
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrinit]
	mov	edx, [lwrt]
	mov	ecx, r12d
	call	__lwr_retain_if_borrowed
	mov	rdi, rbx
	mov	esi, r13d
	mov	edx, [lwrs]
	xor	rcx, rcx
	mov	r8d, r12d
	xor	r9, r9			; BFA_ORDER_NATIVUS
	call	lwr_store
	jmp	.reg
  .intoslot:
	mov	rdi, rbx
	mov	esi, [lwrinit]
	mov	edx, [lwrs]
	call	lwr_expr_into

  .reg:
	; only an INITIALISED reference binding joins the frame's release list
	cmp	dword [lwrinit], 0
	je	.out
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	__lwr_is_refty
	test	eax, eax
	jz	.out
	mov	rdi, rbx
	mov	esi, [lwrd]
	call	lwr_rel_add
  .out:
	return
endp

; __lwr_sub(st, lwrn) -- `sub P = e;`.
;
; An ordinary SSA variable of type `cap`, written once with `e`'s value, PLUS
; an entry in the current frame's capability slot for atom `P`, which is what
; a later call's hidden carrier for `P` reads. runtime.md 2.2's "lowers to
; nothing" is right about the instruction count for the hello world -- `sub
; ambitus = a;` writes the value `a` already holds -- and the frame entry is
; the something.
;
; The BLOCK form (`sub P = e { ... }`, `Sub.c`) pushes its own frame so the
; binding ends with the block.
proc __lwr_sub, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrd, dd
		slot lwrpath, dd
		slot lwre, dd
		slot lwrbody, dd
		slot lwrt, dd
		slot lwratom, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwrpath], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwre], ecx
	mov	ecx, [rax + AstNode.c]
	mov	[lwrbody], ecx
	mov	ecx, [rax + AstNode.d]
	mov	[lwrd], ecx
	rassert	ecx ne 0

	; which atom: the `Path`'s own declaration is the AST_D_CAPATOM, and
	; ast/build.inc pushed the eleven of spec section 4.6 last, so the
	; atom's 1-based ordinal is its distance from `ast_cap_base`
	mov	rdi, rbx
	mov	esi, [lwrpath]
	call	__lwr_node
	mov	r12d, [rax + AstNode.d]
	rassert	r12d ne 0
	mov	rdi, [rbx + LwrState.tree]
	call	ast_cap_base
	rassert	rax ge 1
	sub	r12d, eax
	inc	r12d
	mov	[lwratom], r12d

	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.ty]
	mov	[lwrt], ecx
	mov	rdi, rbx
	mov	esi, ecx
	call	lwr_ty_ir
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, LWR_VK_SSA
	mov	ecx, eax
	xor	r8, r8
	call	lwr_var_set

	mov	rdi, rbx
	mov	esi, [lwre]
	call	lwr_expr
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, [rbx + LwrState.cur]
	mov	ecx, r13d
	call	lwr_write_var

	cmp	dword [lwrbody], 0
	jne	.scoped
	mov	rdi, rbx
	mov	esi, [lwratom]
	mov	edx, [lwrd]
	call	lwr_cap_bind
	return
  .scoped:
	mov	rdi, rbx
	call	lwr_frame_push
	mov	rdi, rbx
	mov	esi, [lwratom]
	mov	edx, [lwrd]
	call	lwr_cap_bind
	mov	rdi, rbx
	mov	esi, [lwrbody]
	call	lwr_block
	mov	rdi, rbx
	call	lwr_frame_depth
	dec	rax
	mov	rdi, rbx
	mov	rsi, rax
	call	lwr_frame_release
	mov	rdi, rbx
	call	lwr_frame_pop
	return
endp

; __lwr_redde(st, lwrn) -- `redde [e];`.
;
; The expression is evaluated FIRST, then every frame is released innermost to
; the function root, then `ret`. A returned local reference is retained before
; those releases and released by them: the retain/release pair that leaves on
; one value in one block is exactly what IR 2.8 names as the only planned
; pass's target, and leaving it visible is what makes that pass checkable.
proc __lwr_redde, lwrsp, lwrn
	uses	rbx, r12
	locals
		slot lwre, dd
		slot lwrv, dd
		slot lwrt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwre], ecx
	mov	dword [lwrv], 0
	test	ecx, ecx
	jz	.releases

	mov	rdi, rbx
	mov	esi, [lwre]
	call	__lwr_nty
	mov	[lwrt], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_void
	test	eax, eax
	jnz	.releases
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.agg
	mov	rdi, rbx
	mov	esi, [lwre]
	call	lwr_expr
	mov	[lwrv], eax
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	__lwr_is_refty
	test	eax, eax
	jz	.releases
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_ir
	mov	rdi, rbx
	mov	esi, BFA_OP_RETAIN
	xor	rdx, rdx
	mov	ecx, eax
	mov	r8d, [lwrv]
	xor	r9, r9
	call	lwr_emit
	jmp	.releases
  .agg:
	; the result travels through the function's own hidden-return `param`
	mov	eax, [rbx + LwrState.hidret]
	rassert	eax ne 0
	mov	rdi, rbx
	mov	esi, [lwre]
	mov	edx, eax
	call	lwr_expr_into
  .releases:
	mov	rdi, rbx
	xor	rsi, rsi
	call	__lwr_release_to
	mov	rdi, rbx
	mov	esi, [lwrv]
	call	lwr_ret
	return
endp

; __lwr_exprstmt(st, lwrn) -- an expression evaluated for its effects; its
; value, if any, is discarded exactly as the source discards it.
proc __lwr_exprstmt, lwrsp, lwrn
	uses	rbx
	locals
		slot lwre, dd
		slot lwrt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwre], ecx
	mov	rdi, rbx
	mov	esi, ecx
	call	__lwr_nty
	mov	[lwrt], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_void
	test	eax, eax
	jnz	.scalar
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.agg
  .scalar:
	mov	rdi, rbx
	mov	esi, [lwre]
	call	lwr_expr
	return
  .agg:
	mov	rdi, rbx
	mov	esi, [lwre]
	call	lwr_expr_agg
	return
endp

; __lwr_assign(st, lwrn) -- `lhs = rhs;`.
;
; For a reference variable the order is: evaluate `rhs`, retain it if it is
; borrowed, release the OLD value, then write -- so `x = x` is a retain then a
; release and not a use after free.
proc __lwr_assign, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrl, dd
		slot lwrr, dd
		slot lwrk, dd
		slot lwrt, dd
		slot lwrir, dd
		slot lwrd, dd
		slot lwrv, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwrl], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrr], ecx

	mov	rdi, rbx
	mov	esi, [lwrl]
	call	__lwr_nty
	mov	[lwrt], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_ir
	mov	[lwrir], eax
	mov	rdi, rbx
	mov	esi, [lwrl]
	call	__lwr_nkind
	mov	[lwrk], eax

	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.agglhs

	mov	rdi, rbx
	mov	esi, [lwrr]
	call	lwr_expr
	mov	[lwrv], eax
	mov	rdi, rbx
	mov	esi, [lwrr]
	mov	edx, [lwrt]
	mov	ecx, [lwrv]
	call	__lwr_retain_if_borrowed

	cmp	dword [lwrk], AST_PATH
	je	.path
	cmp	dword [lwrk], AST_MEMBER
	je	.member
	cmp	dword [lwrk], AST_INDEX
	je	.index
	cmp	dword [lwrk], AST_UNARY
	je	.deref
	mov	eax, [lwrk]
	rassert	eax eq 0

  .path:
	mov	rdi, rbx
	mov	esi, [lwrl]
	call	__lwr_node
	mov	ecx, [rax + AstNode.d]
	mov	[lwrd], ecx
	rassert	ecx ne 0
	mov	rdi, rbx
	mov	esi, ecx
	call	lwr_var_at
	movzx	ecx, byte [rax + LwrVar.vkind]
	mov	r12d, [rax + LwrVar.vval]
	cmp	ecx, LWR_VK_SLOT
	je	.pslot
	; release the old value of a reference variable before overwriting it
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	__lwr_is_refty
	test	eax, eax
	jz	.pwrite
	mov	rdi, rbx
	mov	esi, [lwrd]
	call	__lwr_release_decl
  .pwrite:
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, [rbx + LwrState.cur]
	mov	ecx, [lwrv]
	call	lwr_write_var
	return
  .pslot:
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	__lwr_is_refty
	test	eax, eax
	jz	.pstore
	push	r12
	mov	rdi, rbx
	mov	esi, [lwrd]
	call	__lwr_release_decl
	pop	r12
  .pstore:
	mov	rdi, rbx
	mov	esi, [lwrir]
	mov	edx, r12d
	xor	rcx, rcx
	mov	r8d, [lwrv]
	xor	r9, r9			; BFA_ORDER_NATIVUS
	call	lwr_store
	return

  .member:
	; a field write: `store T %p off ORDER %v`, or below a byte
	; `storebits T %p byte bit %v` (expr.inc's `__lwr_field_off` and
	; `__lwr_field_bits`; the bit operand is MSB-first, cfg.inc). The
	; checker typed the target as the field's type WITHOUT its order (spec
	; §5.2, D2), so `lwrir` is already the value's type.
	mov	rdi, rbx
	mov	esi, [lwrl]
	call	__lwr_node
	mov	r12d, [rax + AstNode.a]
	mov	r13d, [rax + AstNode.d]
	rassert	r13d ne 0
	mov	rdi, rbx
	mov	esi, r13d
	call	__lwr_field_off
	mov	[lwrd], eax
	mov	r13d, edx			; the packed place
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_expr_agg
	mov	r12d, eax
	mov	edi, r13d
	call	__lwr_field_bits
	test	eax, eax
	jnz	.mbits
	mov	rdi, rbx
	mov	esi, [lwrir]
	mov	edx, r12d
	mov	ecx, [lwrd]
	mov	r8d, [lwrv]
	mov	r9d, r13d
	shr	r9d, 16				; the order
	call	lwr_store
	return
  .mbits:
	mov	rdi, rbx
	mov	esi, [lwrir]
	mov	edx, r12d
	mov	ecx, [lwrd]
	mov	r8d, r13d
	and	r8d, 0xFF			; the bit offset, MSB-first
	mov	r9d, [lwrv]
	call	lwr_storebits
	return

  .index:
	mov	rdi, rbx
	mov	esi, [lwrl]
	call	__lwr_elem
	mov	rdi, rbx
	mov	esi, [lwrir]
	mov	edx, eax
	xor	rcx, rcx
	mov	r8d, [lwrv]
	xor	r9, r9			; BFA_ORDER_NATIVUS
	call	lwr_store
	return

  .deref:
	mov	rdi, rbx
	mov	esi, [lwrl]
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.aux]
	rassert	ecx eq AST_U_DEREF
	mov	r12d, [rax + AstNode.a]
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_expr
	mov	rdi, rbx
	mov	esi, [lwrir]
	mov	edx, eax
	xor	rcx, rcx
	mov	r8d, [lwrv]
	xor	r9, r9			; BFA_ORDER_NATIVUS
	call	lwr_store
	return

  .agglhs:
	; an aggregate assignment is a byte copy (plus one `retain` per
	; reference field, `[UNIMPLEMENTED]` -- see __lwr_copy)
	mov	rdi, rbx
	mov	esi, [lwrl]
	call	lwr_expr_agg
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrr]
	mov	edx, r12d
	call	lwr_expr_into
	return
endp

; ============================================================================
; spec section 8.5's control flow
; ============================================================================

; __lwr_join(st, lwrj) -> eax = `lwrj` if it is already a block, otherwise a
; fresh one. A join is created on the FIRST edge into it, so a construct whose
; arms all end dead never creates one and the statements after it are dead too.
proc __lwr_join, lwrsp, lwrj
	locals
	endl
	cmp	dword [lwrj], 0
	jne	.have
	mov	rdi, [lwrsp]
	call	lwr_blk_new
	return
  .have:
	mov	eax, [lwrj]
	return
endp

; __lwr_si(st, lwrn) -- `si c1 B1 sin c2 B2 ... aliter Bn`.
;
;   cur:   %c1 = <c1> ; br %c1 body1 test2
;   body1: <B1> ; jmp join
;   test2: %c2 = <c2> ; br %c2 body2 test3
;   ...
;   aliter or the last test's false edge -> join
;
; Each body and each test has exactly one predecessor and is sealed the moment
; the `br` that made the edge is emitted, so a read inside an arm never creates
; a phi; the join's phis are Braun's ordinary join phis, filled at its seal.
proc __lwr_si, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwroff, dd
		slot lwrcnt, dd
		slot lwralt, dd
		slot lwrjoin, dd
		slot lwrbody, dd
		slot lwrnext, dd
		slot lwrlast, dd
		slot lwrbb, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwroff], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrcnt], ecx
	mov	ecx, [rax + AstNode.c]
	mov	[lwralt], ecx
	mov	dword [lwrjoin], 0
	xor	r12d, r12d
  .arm:
	cmp	r12d, [lwrcnt]
	jge	.after
	cmp	dword [rbx + LwrState.cur], 0
	je	.after
	; is this the last test, with no `aliter` to take its false edge?
	mov	dword [lwrlast], 0
	mov	eax, r12d
	inc	eax
	cmp	eax, [lwrcnt]
	jne	.nolast
	cmp	dword [lwralt], 0
	jne	.nolast
	mov	dword [lwrlast], 1
  .nolast:
	mov	rdi, rbx
	mov	esi, [lwroff]
	mov	eax, r12d
	shl	eax, 1
	mov	edx, eax
	call	__lwr_listat
	mov	r13d, eax			; the condition
	mov	rdi, rbx
	mov	esi, [lwroff]
	mov	eax, r12d
	shl	eax, 1
	inc	eax
	mov	edx, eax
	call	__lwr_listat
	mov	[lwrbody], eax

	mov	rdi, rbx
	mov	esi, r13d
	call	lwr_expr
	mov	r13d, eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrbb], eax		; the body BLOCK (lwrbody is its NODE)
	cmp	dword [lwrlast], 0
	je	.freshnext
	mov	rdi, rbx
	mov	esi, [lwrjoin]
	call	__lwr_join
	mov	[lwrjoin], eax
	mov	[lwrnext], eax
	jmp	.gotnext
  .freshnext:
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrnext], eax
  .gotnext:
	mov	rdi, rbx
	mov	esi, r13d
	mov	edx, [lwrbb]
	mov	ecx, [lwrnext]
	call	lwr_br
	mov	rdi, rbx
	mov	esi, [lwrbb]
	call	lwr_seal
	cmp	dword [lwrlast], 0
	jne	.nosealnext
	mov	rdi, rbx
	mov	esi, [lwrnext]
	call	lwr_seal
  .nosealnext:
	mov	eax, [lwrbb]
	mov	[rbx + LwrState.cur], eax
	mov	rdi, rbx
	mov	esi, [lwrbody]
	call	lwr_block
	cmp	dword [rbx + LwrState.cur], 0
	je	.armdead
	mov	rdi, rbx
	mov	esi, [lwrjoin]
	call	__lwr_join
	mov	[lwrjoin], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_jmp
  .armdead:
	cmp	dword [lwrlast], 0
	jne	.lastarm
	mov	eax, [lwrnext]
	mov	[rbx + LwrState.cur], eax
	inc	r12d
	jmp	.arm
  .lastarm:
	mov	dword [rbx + LwrState.cur], 0
	inc	r12d
	jmp	.arm

  .after:
	cmp	dword [lwralt], 0
	je	.nojoinalt
	cmp	dword [rbx + LwrState.cur], 0
	je	.nojoinalt
	mov	rdi, rbx
	mov	esi, [lwralt]
	call	lwr_block
  .nojoinalt:
	cmp	dword [rbx + LwrState.cur], 0
	je	.seal
	mov	rdi, rbx
	mov	esi, [lwrjoin]
	call	__lwr_join
	mov	[lwrjoin], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_jmp
  .seal:
	cmp	dword [lwrjoin], 0
	je	.alldead
	mov	rdi, rbx
	mov	esi, [lwrjoin]
	call	lwr_seal
	mov	eax, [lwrjoin]
	mov	[rbx + LwrState.cur], eax
	return
  .alldead:
	mov	dword [rbx + LwrState.cur], 0
	return
endp

; __lwr_dum(st, lwrn) -- `dum c [terminus e] B`.
;
;   cur:    [%N = <e>]  ; jmp header          the bound is evaluated ONCE
;   header: %c = <c> ; br %c (chk | body) exit
;   chk:    %n = <read ctr> ; %t = cmp.lt u64 %n %N ; br %t body trapb
;   trapb:  trap terminus
;   body:   %n1 = addw u64 %n 1 ; <write ctr> ; <B> ; jmp header
;   seal(header) at the back edge; seal(exit) after the body
;
; WHAT `terminus N` DOES WHEN IT IS REACHED IS NOT IN THE SPEC. No sentence in
; spec section 8.5, 5.4 or 13 says what a `dum` does on its N+1'th entry
; (lowering.md finding 1). IR 5 H3 reads it as a counter plus a `chk`-style
; trap and this is that shape: the counter is a synthetic SSA variable, the
; increment is `addw` (it cannot wrap -- the trap fires at N -- and a trapping
; `add` would be a spurious side effect), and the check runs AFTER the
; condition is true, so a loop whose condition goes false exactly at its N'th
; iteration exits normally and only a body that WOULD run an N+1'th time traps.
; `trap terminus` is an IR trap kind (IR 2.3: an IR enum, not a section 13
; code); runtime.md 2.5's abort table has no row for it yet.
proc __lwr_dum, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrcond, dd
		slot lwrbnd, dd
		slot lwrbody, dd
		slot lwrhdr, dd
		slot lwrbb, dd
		slot lwrexit, dd
		slot lwrchk, dd
		slot lwrtrap, dd
		slot lwrctr, dd
		slot lwrnv, dd
		slot lwrdp, dq
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwrcond], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrbnd], ecx
	mov	ecx, [rax + AstNode.c]
	mov	[lwrbody], ecx
	mov	dword [lwrctr], 0

	cmp	dword [lwrbnd], 0
	je	.nobound
	mov	rdi, rbx
	mov	esi, [lwrbnd]
	call	lwr_expr
	mov	[lwrnv], eax
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tu64]
	call	lwr_synth_var
	mov	[lwrctr], eax
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tu64]
	xor	rdx, rdx
	xor	rcx, rcx
	call	lwr_iconst
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrctr]
	mov	edx, [rbx + LwrState.cur]
	mov	ecx, r12d
	call	lwr_write_var
  .nobound:
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrhdr], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_jmp
	mov	eax, [lwrhdr]
	mov	[rbx + LwrState.cur], eax

	mov	rdi, rbx
	mov	esi, [lwrcond]
	call	lwr_expr
	mov	r13d, eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrbb], eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrexit], eax

	cmp	dword [lwrbnd], 0
	je	.plainbr
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrchk], eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrtrap], eax
	mov	rdi, rbx
	mov	esi, r13d
	mov	edx, [lwrchk]
	mov	ecx, [lwrexit]
	call	lwr_br
	mov	rdi, rbx
	mov	esi, [lwrchk]
	call	lwr_seal
	mov	eax, [lwrchk]
	mov	[rbx + LwrState.cur], eax
	mov	rdi, rbx
	mov	esi, [lwrctr]
	mov	edx, eax
	call	lwr_read_var
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, BFA_OP_CMP
	mov	edx, BFA_PRED_LT
	mov	ecx, [rbx + LwrState.tu64]
	mov	r8d, r12d
	mov	r9d, [lwrnv]
	call	lwr_emit
	mov	rdi, rbx
	mov	esi, eax
	mov	edx, [lwrbb]
	mov	ecx, [lwrtrap]
	call	lwr_br
	mov	rdi, rbx
	mov	esi, [lwrbb]
	call	lwr_seal
	mov	rdi, rbx
	mov	esi, [lwrtrap]
	call	lwr_seal
	mov	eax, [lwrtrap]
	mov	[rbx + LwrState.cur], eax
	mov	rdi, [rbx + LwrState.mdl]
	mov	rdi, [rdi + BfaModule.names]
	lea	rsi, [__lwr_lit_terminus]
	mov	rdx, __lwr_lit_terminus.len
	call	intern_id
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_trap
	mov	eax, [lwrbb]
	mov	[rbx + LwrState.cur], eax
	; the increment sits at the top of the body, where an N+1'th entry
	; cannot reach it
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tu64]
	mov	edx, 1
	xor	rcx, rcx
	call	lwr_iconst
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, BFA_OP_ADDW
	xor	rdx, rdx
	mov	ecx, [rbx + LwrState.tu64]
	mov	r8d, r12d
	mov	r9d, r13d
	call	lwr_emit
	mov	rdi, rbx
	mov	esi, [lwrctr]
	mov	edx, [lwrbb]
	mov	ecx, eax
	call	lwr_write_var
	jmp	.body

  .plainbr:
	mov	rdi, rbx
	mov	esi, r13d
	mov	edx, [lwrbb]
	mov	ecx, [lwrexit]
	call	lwr_br
	mov	rdi, rbx
	mov	esi, [lwrbb]
	call	lwr_seal
	mov	eax, [lwrbb]
	mov	[rbx + LwrState.cur], eax

  .body:
	mov	rdi, rbx
	call	lwr_frame_depth
	mov	[lwrdp], rax
	mov	rdi, rbx
	mov	esi, [lwrexit]
	mov	edx, [lwrhdr]
	mov	ecx, [lwrdp]
	call	lwr_loop_push
	mov	rdi, rbx
	mov	esi, [lwrbody]
	call	lwr_block
	mov	rdi, rbx
	call	lwr_loop_pop
	mov	rdi, rbx
	mov	esi, [lwrhdr]
	call	lwr_goto
	mov	rdi, rbx
	mov	esi, [lwrhdr]
	call	lwr_seal
	mov	rdi, rbx
	mov	esi, [lwrexit]
	call	lwr_seal
	mov	eax, [lwrexit]
	mov	[rbx + LwrState.cur], eax
	return
endp

	jmp	__lwr_after_terminus
  __lwr_lit_terminus db "terminus"
  .len = $ - __lwr_lit_terminus
  __lwr_after_terminus:

; __lwr_for(st, lwrn) -- `per x in a..b B` and `quisque x in a..b B`.
;
;   cur:    %a = <a> ; %b = <b> ; <write x %a> ; jmp header
;   header: %i = <read x> ; %c = cmp.lt T %i %b ; br %c body exit
;   body:   <B> ; jmp latch
;   latch:  %i1 = addw T %i 1 ; <write x %i1> ; jmp header
;
; The loop variable is an ordinary SSA variable, written in `cur` and in the
; latch and read first in the header -- so its phi is the header's FIRST phi,
; which IR 2.4 requires of a `quisque` header. `perge` targets the LATCH, not
; the header, so the increment runs on every path to the back edge; `rumpe`
; targets the exit. The range's two ends are evaluated once.
;
; `quisque` lowers to the same CFG with `BFA_BLOCK_FLAG_QUISQUE` on the header
; and nothing else: spec section 8.5 says the keyword "asserts a property of
; the body, not authority to spawn anything", IR 2.3 has no parallel-loop
; instruction, and the flag's consumption is Stage 3 `[OPEN]`. Property 2 -- a
; false `quisque` should be a compile error -- is dependency analysis neither
; this pass nor the checker does, so the flag is a TRUSTED ASSERTION, which
; section 8.5 says must be labelled as such rather than quietly downgraded.
proc __lwr_for, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrrng, dd
		slot lwrbody, dd
		slot lwrhead, dd
		slot lwrd, dd
		slot lwrt, dd
		slot lwrir, dd
		slot lwrhdr, dd
		slot lwrbb, dd
		slot lwrlat, dd
		slot lwrexit, dd
		slot lwrhi, dd
		slot lwrdp, dq
		slot lwraux, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.aux]
	mov	[lwraux], ecx
	mov	ecx, [rax + AstNode.a]
	mov	[lwrrng], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrbody], ecx
	mov	ecx, [rax + AstNode.c]
	mov	[lwrhead], ecx
	mov	ecx, [rax + AstNode.d]
	mov	[lwrd], ecx
	rassert	ecx ne 0
	; `contrahe` accumulators are a `ForHead` this slice does not build:
	; `redinit`/`contrib`/`redfin` are designed in lowering.md 2.5 and have
	; no fixture, and emitting them untested would be worse than refusing
	mov	eax, [lwrhead]
	rassert	eax eq 0

	mov	rdi, rbx
	mov	esi, [lwrrng]
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.aux]
	rassert	ecx eq AST_OP_RANGE
	mov	r12d, [rax + AstNode.a]
	mov	r13d, [rax + AstNode.b]

	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.ty]
	mov	[lwrt], ecx
	mov	rdi, rbx
	mov	esi, ecx
	call	lwr_ty_ir
	mov	[lwrir], eax
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, LWR_VK_SSA
	mov	ecx, eax
	xor	r8, r8
	call	lwr_var_set

	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_expr
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, r13d
	call	lwr_expr
	mov	[lwrhi], eax
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, [rbx + LwrState.cur]
	mov	ecx, r12d
	call	lwr_write_var

	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrhdr], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_jmp
	mov	eax, [lwrhdr]
	mov	[rbx + LwrState.cur], eax
	cmp	dword [lwraux], 0
	je	.noquisque
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrhdr]
	call	bfa_block_ptr
	or	dword [rax + BfaBlock.flags], BFA_BLOCK_FLAG_QUISQUE
  .noquisque:
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, [lwrhdr]
	call	lwr_read_var
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, BFA_OP_CMP
	mov	edx, BFA_PRED_LT
	mov	ecx, [lwrir]
	mov	r8d, r12d
	mov	r9d, [lwrhi]
	call	lwr_emit
	mov	r13d, eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrbb], eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrexit], eax
	mov	rdi, rbx
	mov	esi, r13d
	mov	edx, [lwrbb]
	mov	ecx, [lwrexit]
	call	lwr_br
	mov	rdi, rbx
	mov	esi, [lwrbb]
	call	lwr_seal

	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrlat], eax
	mov	eax, [lwrbb]
	mov	[rbx + LwrState.cur], eax
	mov	rdi, rbx
	call	lwr_frame_depth
	mov	[lwrdp], rax
	mov	rdi, rbx
	mov	esi, [lwrexit]
	mov	edx, [lwrlat]
	mov	ecx, [lwrdp]
	call	lwr_loop_push
	mov	rdi, rbx
	mov	esi, [lwrbody]
	call	lwr_block
	mov	rdi, rbx
	call	lwr_loop_pop
	mov	rdi, rbx
	mov	esi, [lwrlat]
	call	lwr_goto

	mov	rdi, rbx
	mov	esi, [lwrlat]
	call	lwr_seal
	mov	eax, [lwrlat]
	mov	[rbx + LwrState.cur], eax
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, [lwrlat]
	call	lwr_read_var
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrir]
	mov	edx, 1
	xor	rcx, rcx
	call	lwr_iconst
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, BFA_OP_ADDW
	xor	rdx, rdx
	mov	ecx, [lwrir]
	mov	r8d, r12d
	mov	r9d, r13d
	call	lwr_emit
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, [lwrlat]
	mov	ecx, eax
	call	lwr_write_var
	mov	rdi, rbx
	mov	esi, [lwrhdr]
	call	lwr_jmp
	mov	rdi, rbx
	mov	esi, [lwrhdr]
	call	lwr_seal
	mov	rdi, rbx
	mov	esi, [lwrexit]
	call	lwr_seal
	mov	eax, [lwrexit]
	mov	[rbx + LwrState.cur], eax
	return
endp

; __lwr_discerne(st, lwrn) -- `discerne s { casus p1 B1 ... [aliter Bn] }`.
;
; A COMPARE CHAIN, because IR 2.3 lists `switch` as deliberately absent and
; this pass does not add an instruction (a `switch` is proposed in
; lowering.md finding 5; until it exists the naive backend would lower one to
; this chain anyway). A `Lit` pattern is its `iconst`; a `Path` pattern
; resolves to a module-level `firma` whose initializer is a `Lit` -- the only
; constant a `Path` can name today (spec section 8.6 decision 5).
;
; A scrutinee of any type but an integer or `u1` needs a runtime comparison
; this pass has no callee for (`textus` equality is a prelude routine that
; does not exist) and traps: `[OPEN]`, lowering.md section 7.
;
; With no `aliter` the last false edge goes to the join: spec section 8.5
; makes the arms exhaustive and the checker enforces it, so that edge is
; unreachable in fact and needs no `trap`.
proc __lwr_discerne, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrscr, dd
		slot lwroff, dd
		slot lwrcnt, dd
		slot lwralt, dd
		slot lwrjoin, dd
		slot lwrsv, dd
		slot lwrir, dd
		slot lwrbb, dd
		slot lwrnext, dd
		slot lwrlast, dd
		slot lwrbody, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwrscr], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwroff], ecx
	mov	ecx, [rax + AstNode.c]
	mov	[lwrcnt], ecx
	mov	ecx, [rax + AstNode.d]
	mov	[lwralt], ecx
	mov	dword [lwrjoin], 0

	mov	rdi, rbx
	mov	esi, [lwrscr]
	call	__lwr_nty
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_kind
	rassert	eax eq AST_TY_INT
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_ty_ir
	mov	[lwrir], eax
	mov	rdi, rbx
	mov	esi, [lwrscr]
	call	lwr_expr
	mov	[lwrsv], eax

	xor	r12d, r12d
  .arm:
	cmp	r12d, [lwrcnt]
	jge	.after
	cmp	dword [rbx + LwrState.cur], 0
	je	.after
	mov	dword [lwrlast], 0
	mov	eax, r12d
	inc	eax
	cmp	eax, [lwrcnt]
	jne	.nolast
	cmp	dword [lwralt], 0
	jne	.nolast
	mov	dword [lwrlast], 1
  .nolast:
	mov	rdi, rbx
	mov	esi, [lwroff]
	mov	edx, r12d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	r13d, [rax + AstNode.a]		; the pattern
	mov	ecx, [rax + AstNode.b]		; the arm's block
	mov	[lwrbody], ecx

	mov	rdi, rbx
	mov	esi, r13d
	call	__lwr_pattern
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, BFA_OP_CMP
	mov	edx, BFA_PRED_EQ
	mov	ecx, [lwrir]
	mov	r8d, [lwrsv]
	mov	r9d, r13d
	call	lwr_emit
	mov	r13d, eax

	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrbb], eax
	cmp	dword [lwrlast], 0
	je	.freshnext
	mov	rdi, rbx
	mov	esi, [lwrjoin]
	call	__lwr_join
	mov	[lwrjoin], eax
	mov	[lwrnext], eax
	jmp	.gotnext
  .freshnext:
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrnext], eax
  .gotnext:
	mov	rdi, rbx
	mov	esi, r13d
	mov	edx, [lwrbb]
	mov	ecx, [lwrnext]
	call	lwr_br
	mov	rdi, rbx
	mov	esi, [lwrbb]
	call	lwr_seal
	cmp	dword [lwrlast], 0
	jne	.nosealnext
	mov	rdi, rbx
	mov	esi, [lwrnext]
	call	lwr_seal
  .nosealnext:
	mov	eax, [lwrbb]
	mov	[rbx + LwrState.cur], eax
	mov	rdi, rbx
	mov	esi, [lwrbody]
	call	lwr_block
	cmp	dword [rbx + LwrState.cur], 0
	je	.armdead
	mov	rdi, rbx
	mov	esi, [lwrjoin]
	call	__lwr_join
	mov	[lwrjoin], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_jmp
  .armdead:
	cmp	dword [lwrlast], 0
	jne	.lastarm
	mov	eax, [lwrnext]
	mov	[rbx + LwrState.cur], eax
	inc	r12d
	jmp	.arm
  .lastarm:
	mov	dword [rbx + LwrState.cur], 0
	inc	r12d
	jmp	.arm
  .after:
	cmp	dword [lwralt], 0
	je	.nojoinalt
	cmp	dword [rbx + LwrState.cur], 0
	je	.nojoinalt
	mov	rdi, rbx
	mov	esi, [lwralt]
	call	lwr_block
  .nojoinalt:
	cmp	dword [rbx + LwrState.cur], 0
	je	.seal
	mov	rdi, rbx
	mov	esi, [lwrjoin]
	call	__lwr_join
	mov	[lwrjoin], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_jmp
  .seal:
	cmp	dword [lwrjoin], 0
	je	.alldead
	mov	rdi, rbx
	mov	esi, [lwrjoin]
	call	lwr_seal
	mov	eax, [lwrjoin]
	mov	[rbx + LwrState.cur], eax
	return
  .alldead:
	mov	dword [rbx + LwrState.cur], 0
	return
endp

; __lwr_pattern(st, lwrn) -> eax = the constant a `casus` pattern names.
proc __lwr_pattern, lwrsp, lwrn
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nkind
	cmp	eax, AST_LIT
	je	.lit
	rassert	eax eq AST_PATH
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.d]
	rassert	ecx ne 0
	mov	rdi, rbx
	mov	esi, ecx
	call	__lwr_module_const
	return
  .lit:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_lit
	return
endp
