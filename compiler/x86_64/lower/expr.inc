; compiler/x86_64/lower/expr.inc
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
; Expressions, calls and literals: docs/design/lowering.md sections 2.7 and
; 2.8, and the scope frames of 2.6 that a call's hidden carriers are looked up
; in.
;
; TWO ENTRY POINTS, because the IR has no aggregate values (IR 2.7):
;   `lwr_expr(node)      -> %v`   a scalar-typed expression
;   `lwr_expr_into(node, %dst)`   a non-scalar one, written through a `ptr` the
;                                 caller supplies -- a binding's slot, a call's
;                                 hidden-return slot, a fresh temporary, or the
;                                 function's own hidden-return `param`
;   `lwr_expr_agg(node)  -> %p`   the address of a non-scalar value: a
;                                 memory-resident binding's OWN slot with no
;                                 copy, otherwise a fresh slot written into
;
; Destination passing is what lets `redde "..."` in `saluta` write straight
; through `%0` with no temporary and no `copy`, which is the IR
; docs/design/runtime.md section 5 writes down.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md sections 4.5, 4.6, 5.4, 8.4, 8.6.
; -----------------------------------------------------------------------------

include 'ty.inc'

; ============================================================================
; scope frames, capability slots, release lists (lowering.md section 2.6)
; ============================================================================

; lwr_frame_push(st) -- a new lexical scope: eleven zeroed capability slots
; (spec section 4.6's atom count) and an empty release list.
proc lwr_frame_push, lwrsp
	uses	rbx, r12
	locals
		slot lwrcb, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.caps]
	mov	rax, [rdi + Vec.len]
	mov	[lwrcb], eax
	xor	r12d, r12d
  .z:
	cmp	r12d, LWR_CAP_N
	jge	.zdone
	mov	rdi, [rbx + LwrState.caps]
	call	vec_push
	mov	dword [rax], 0
	inc	r12d
	jmp	.z
  .zdone:
	mov	rdi, [rbx + LwrState.scps]
	call	vec_push
	mov	ecx, [lwrcb]
	mov	[rax + LwrFrame.fcapb], ecx
	mov	rdi, [rbx + LwrState.rels]
	mov	rcx, [rdi + Vec.len]
	mov	rdi, [rbx + LwrState.scps]
	mov	rax, [rdi + Vec.len]
	dec	rax
	mov	rsi, rax
	call	vec_get
	mov	[rax + LwrFrame.frelb], ecx
	mov	dword [rax + LwrFrame.freln], 0
	mov	dword [rax + LwrFrame.fpad], 0
	return
endp

; lwr_frame_at(st, lwri) -> rax = frame `lwri` (0-based, outermost first).
proc lwr_frame_at, lwrsp, lwri
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.scps]
	mov	rsi, [lwri]
	call	vec_get
	return
endp

; lwr_frame_depth(st) -> rax = how many frames are open.
proc lwr_frame_depth, lwrsp
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.scps]
	mov	rax, [rdi + Vec.len]
	return
endp

; lwr_cap_bind(st, lwratom, lwrvid) -- the innermost frame now provides
; capability atom `lwratom` (1-based, spec section 4.6 order) through variable
; `lwrvid`. `sub P = e;` is what calls this; CHK 2.1 proved the lookup at a
; call site is a frame walk with no search.
proc lwr_cap_bind, lwrsp, lwratom, lwrvid
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	call	lwr_frame_depth
	rassert	rax ge 1
	dec	rax
	mov	rdi, rbx
	mov	rsi, rax
	call	lwr_frame_at
	mov	ecx, [rax + LwrFrame.fcapb]
	mov	eax, [lwratom]
	rassert	eax ge 1
	dec	eax
	add	ecx, eax
	mov	rdi, [rbx + LwrState.caps]
	mov	esi, ecx
	call	vec_get
	mov	ecx, [lwrvid]
	mov	[rax], ecx
	return
endp

; lwr_cap_find(st, lwratom) -> eax = the variable id of the innermost provider
; of atom `lwratom`, or 0. The frame stack walked outward, exactly CHK 2.1's
; rule; the function's own hidden carrier `param` sits in the root frame, put
; there by `lwr_fn` when the row has that atom.
proc lwr_cap_find, lwrsp, lwratom
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	call	lwr_frame_depth
	mov	r12, rax
  .loop:
	test	r12, r12
	jz	.none
	dec	r12
	mov	rdi, rbx
	mov	rsi, r12
	call	lwr_frame_at
	mov	ecx, [rax + LwrFrame.fcapb]
	mov	eax, [lwratom]
	dec	eax
	add	ecx, eax
	mov	rdi, [rbx + LwrState.caps]
	mov	esi, ecx
	call	vec_get
	mov	eax, [rax]
	test	eax, eax
	jnz	.hit
	jmp	.loop
  .none:
	xor	eax, eax
  .hit:
	return
endp

; lwr_rel_add(st, lwrdid) -- record that declaration `lwrdid` has been
; initialised in the innermost frame and must be released when that frame
; exits. The frame keeps its OWN list in initialisation order, not the block's
; `[Block.c, Block.c + Block.aux)` declaration range: that range includes
; nested blocks' sub-ranges, which were released at their own exits, and a
; decl in a dead statement was never initialised at all.
proc lwr_rel_add, lwrsp, lwrdid
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.rels]
	call	vec_push
	mov	ecx, [lwrdid]
	mov	[rax], ecx
	mov	rdi, rbx
	call	lwr_frame_depth
	dec	rax
	mov	rdi, rbx
	mov	rsi, rax
	call	lwr_frame_at
	mov	ecx, [rax + LwrFrame.freln]
	inc	ecx
	mov	[rax + LwrFrame.freln], ecx
	return
endp

; lwr_frame_release(st, lwri) -- emit the releases of frame `lwri` in REVERSE
; initialisation order (spec section 6.6 states no order; AST 2.9 chose
; reverse declaration order and this narrows it to reverse initialisation,
; which coincide except for dead declarations). Emits nothing if `cur` is
; dead. Does NOT pop the frame.
proc lwr_frame_release, lwrsp, lwri
	uses	rbx, r12, r13
	locals
		slot lwrb, dd
		slot lwrn, dd
	endl
	mov	rbx, [lwrsp]
	cmp	dword [rbx + LwrState.cur], 0
	je	.dead
	mov	rdi, rbx
	mov	rsi, [lwri]
	call	lwr_frame_at
	mov	ecx, [rax + LwrFrame.frelb]
	mov	[lwrb], ecx
	mov	ecx, [rax + LwrFrame.freln]
	mov	[lwrn], ecx
	mov	r12d, [lwrn]
  .loop:
	test	r12d, r12d
	jz	.dead
	dec	r12d
	mov	rdi, [rbx + LwrState.rels]
	mov	eax, [lwrb]
	add	eax, r12d
	mov	esi, eax
	call	vec_get
	mov	r13d, [rax]
	mov	rdi, rbx
	mov	esi, r13d
	call	__lwr_release_decl
	jmp	.loop
  .dead:
	return
endp

; lwr_frame_pop(st) -- drop the innermost frame's eleven capability slots and
; its release list. The releases themselves are `lwr_frame_release`'s, because
; `redde` releases every frame without popping any of them.
proc lwr_frame_pop, lwrsp
	uses	rbx
	locals
		slot lwrcb, dd
		slot lwrrb, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	call	lwr_frame_depth
	rassert	rax ge 1
	dec	rax
	mov	rdi, rbx
	mov	rsi, rax
	call	lwr_frame_at
	mov	ecx, [rax + LwrFrame.fcapb]
	mov	[lwrcb], ecx
	mov	ecx, [rax + LwrFrame.frelb]
	mov	[lwrrb], ecx
	mov	rdi, [rbx + LwrState.caps]
	mov	eax, [lwrcb]
	mov	[rdi + Vec.len], rax
	mov	rdi, [rbx + LwrState.rels]
	mov	eax, [lwrrb]
	mov	[rdi + Vec.len], rax
	mov	rdi, [rbx + LwrState.scps]
	mov	rax, [rdi + Vec.len]
	dec	rax
	mov	[rdi + Vec.len], rax
	return
endp

; __lwr_release_decl(st, lwrdid) -- one `release` for one binding, read out of
; wherever it lives. Atomicity is the type's (`ref` vs `refc`), so one opcode
; serves both and `emit.inc` picks `exsrt_release` or `_c`.
proc __lwr_release_decl, lwrsp, lwrdid
	uses	rbx, r12
	locals
		slot lwrv, dd
		slot lwrt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrdid]
	call	lwr_var_at
	movzx	ecx, byte [rax + LwrVar.vkind]
	mov	r12d, [rax + LwrVar.vval]
	mov	eax, [rax + LwrVar.vty]
	mov	[lwrt], eax
	cmp	ecx, LWR_VK_SLOT
	je	.slot
	mov	rdi, rbx
	mov	esi, [lwrdid]
	mov	edx, [rbx + LwrState.cur]
	call	lwr_read_var
	mov	[lwrv], eax
	jmp	.emit
  .slot:
	mov	rdi, rbx
	mov	esi, [lwrt]
	mov	edx, r12d
	xor	rcx, rcx
	call	lwr_load
	mov	[lwrv], eax
  .emit:
	mov	rdi, rbx
	mov	esi, BFA_OP_RELEASE
	xor	rdx, rdx
	mov	ecx, [lwrt]
	mov	r8d, [lwrv]
	xor	r9, r9
	call	lwr_emit
	return
endp

; ============================================================================
; small accessors
; ============================================================================

; __lwr_node(st, lwrn) -> rax = the AstNode. Valid only until the next push on
; `Ast.nodes`, and nothing here pushes one.
proc __lwr_node, lwrsp, lwrn
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.tree]
	mov	rsi, [lwrn]
	call	ast_node_at
	return
endp

; __lwr_nkind(st, lwrn) -> eax = its `AST_*` kind.
proc __lwr_nkind, lwrsp, lwrn
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rsi, [lwrn]
	call	__lwr_node
	movzx	eax, word [rax + AstNode.kind]
	return
endp

; __lwr_nty(st, lwrn) -> eax = its `Ast.types` id. CHK 2.5 guarantees this is
; non-zero on every node of a tree that reaches this pass, so a zero here is a
; checker bug and not a program error.
proc __lwr_nty, lwrsp, lwrn
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rsi, [lwrn]
	call	__lwr_node
	mov	eax, [rax + AstNode.ty]
	rassert	eax ne 0
	return
endp

; __lwr_listat(st, lwroff, lwri) -> eax = element `lwri` of an `Ast.extra`
; list that starts at `lwroff`.
proc __lwr_listat, lwrsp, lwroff, lwri
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.tree]
	mov	esi, [lwroff]
	add	esi, [lwri]
	call	ast_extra_at
	return
endp

; ============================================================================
; expressions
; ============================================================================

; lwr_expr(st, lwrn) -> eax = the value of a SCALAR-typed expression.
proc lwr_expr, lwrsp, lwrn
	uses	rbx
	locals
		slot lwrk, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nkind
	mov	[lwrk], eax

	cmp	eax, AST_LIT
	je	.lit
	cmp	eax, AST_PATH
	je	.path
	cmp	eax, AST_UNARY
	je	.unary
	cmp	eax, AST_BINARY
	je	.binary
	cmp	eax, AST_CAST
	je	.cast
	cmp	eax, AST_CALL
	je	.call
	cmp	eax, AST_MEMBER
	je	.member
	cmp	eax, AST_INDEX
	je	.index
	; `Try` (spec section 11's `eventus` has no representation, IR 6) and
	; `Lambda` (lowering.md 2.7: closures are [OPEN] in five separate ways)
	; are the two the first slice refuses rather than guesses at
	rassert	eax eq 0
  .lit:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_lit
	return
  .path:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_path
	return
  .unary:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_unary
	return
  .binary:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_binary
	return
  .cast:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_cast
	return
  .call:
	mov	rdi, rbx
	mov	esi, [lwrn]
	xor	rdx, rdx
	call	__lwr_call
	return
  .member:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_member_read
	return
  .index:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_index_read
	return
endp

; __lwr_lit(st, lwrn) -> eax -- an INTEGER literal. `Lit` keeps TEXT, not a
; value (AST 2.3: spec section 8.4's numeric grammar is `[OPEN]`), so the
; VALUE is the checker's `konst` side table, at the literal's own type.
proc __lwr_lit, lwrsp, lwrn
	uses	rbx, r12
	locals
		slot lwrt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.aux]
	rassert	ecx eq AST_LIT_INT
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrn]
	call	ast_konst_get
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_ir
	mov	[lwrt], eax
	mov	rdi, rbx
	mov	esi, [lwrt]
	mov	edx, r12d
	shr	r12, 32
	mov	ecx, r12d
	call	lwr_iconst
	return
endp

; __lwr_path(st, lwrn) -> eax -- a `Path` in value position, reading whatever
; storage its declaration got.
proc __lwr_path, lwrsp, lwrn
	uses	rbx, r12
	locals
		slot lwrd, dd
		slot lwrk, dd
		slot lwrt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.d]
	mov	[lwrd], ecx
	rassert	ecx ne 0
	mov	eax, ecx
	test	eax, LWR_PRELUDE_TAG
	jnz	.prelude
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_ir
	mov	[lwrt], eax

	mov	rdi, rbx
	mov	esi, [lwrd]
	call	lwr_var_at
	movzx	ecx, byte [rax + LwrVar.vkind]
	mov	[lwrk], ecx
	mov	r12d, [rax + LwrVar.vval]

	cmp	dword [lwrk], LWR_VK_SSA
	je	.ssa
	cmp	dword [lwrk], LWR_VK_AGGP
	je	.aggp
	cmp	dword [lwrk], LWR_VK_SLOT
	je	.slot
	; not a local at all: a module-level `firma` with a literal
	; initializer, the only constant a `Path` can name today (spec section
	; 8.6 decision 5)
	mov	rdi, rbx
	mov	esi, [lwrd]
	call	__lwr_module_const
	return
  .ssa:
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, [rbx + LwrState.cur]
	call	lwr_read_var
	return
  .aggp:
	mov	eax, r12d
	return
  .slot:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_scalar
	test	eax, eax
	jz	.slotagg
	mov	rdi, rbx
	mov	esi, [lwrt]
	mov	edx, r12d
	xor	rcx, rcx
	call	lwr_load
	return
  .slotagg:
	mov	eax, r12d
	return
  .prelude:
	; a bare prelude NAME in value position -- only `Scriptor` can be one,
	; and only as the receiver of an associated call, which __lwr_call
	; handles without ever lowering the path
	rassert	eax eq 0
	return
endp

; __lwr_module_const(st, lwrd) -> eax -- a module-level `firma` whose
; initializer is a `Lit`, inlined at the use (lowering.md 2.8). Any other
; module-level initializer is `[OPEN]` and traps.
proc __lwr_module_const, lwrsp, lwrd
	uses	rbx
	locals
		slot lwrnode, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	rassert	ecx eq AST_D_BINDING
	mov	ecx, [rax + AstDecl.node]
	rassert	ecx ne 0
	mov	[lwrnode], ecx
	mov	rdi, rbx
	mov	esi, ecx
	call	__lwr_node
	mov	ecx, [rax + AstNode.b]
	rassert	ecx ne 0
	mov	[lwrnode], ecx
	mov	rdi, rbx
	mov	esi, ecx
	call	__lwr_nkind
	rassert	eax eq AST_LIT
	mov	rdi, rbx
	mov	esi, [lwrnode]
	call	__lwr_lit
	return
endp

; __lwr_unary(st, lwrn) -> eax
proc __lwr_unary, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrop, dd
		slot lwrt, dd
		slot lwrz, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.aux]
	mov	[lwrop], ecx
	mov	r12d, [rax + AstNode.a]

	cmp	dword [lwrop], AST_U_ADDR
	je	.addr
	cmp	dword [lwrop], AST_U_DEREF
	je	.deref

	; `-x`. On an integer this is `sub T 0 x` -- TRAPPING, so `-MIN` traps,
	; which is spec section 5.4's `-`; on a float it is `fneg`.
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, r13d
	call	lwr_ty_ir
	mov	[lwrt], eax
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_expr
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, r13d
	call	lwr_ty_kind
	cmp	eax, AST_TY_FLOAT
	je	.fneg
	mov	rdi, rbx
	mov	esi, [lwrt]
	xor	rdx, rdx
	xor	rcx, rcx
	call	lwr_iconst
	mov	[lwrz], eax
	mov	rdi, rbx
	mov	esi, BFA_OP_SUB
	xor	rdx, rdx
	mov	ecx, [lwrt]
	mov	r8d, [lwrz]
	mov	r9d, r12d
	call	lwr_emit
	return
  .fneg:
	mov	rdi, rbx
	mov	esi, BFA_OP_FNEG
	xor	rdx, rdx
	mov	ecx, [lwrt]
	mov	r8d, r12d
	xor	r9, r9
	call	lwr_emit
	return
  .addr:
	; `&x` -- the checker set AST_F_ADDRTAKEN, so `x` is memory-resident
	; and its slot IS the address
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_expr_agg
	return
  .deref:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_expr
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, r13d
	call	lwr_ty_scalar
	test	eax, eax
	jz	.derefagg
	mov	rdi, rbx
	mov	esi, r13d
	call	lwr_ty_ir
	mov	rdi, rbx
	mov	esi, eax
	mov	edx, r12d
	xor	rcx, rcx
	call	lwr_load
	return
  .derefagg:
	mov	eax, r12d
	return
endp

; __lwr_arith_op(st, lwrao, lwrt) -> eax = the BFA opcode for AST operator
; `lwrao` at AST type `lwrt`. Spec section 5.4's three families are three
; opcodes, never a flag: `+` traps, `+%` wraps, `+|` saturates. The wrapping
; and saturating spellings on a FLOAT are the checker's `EXS-E0305`, so a
; float here can only be the plain form.
proc __lwr_arith_op, lwrsp, lwrao, lwrt
	uses	rbx
	locals
		slot lwrf, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_kind
	xor	ecx, ecx
	cmp	eax, AST_TY_FLOAT
	jne	.notf
	mov	ecx, 1
  .notf:
	mov	[lwrf], ecx
	mov	eax, [lwrao]
	cmp	eax, AST_OP_ADD
	je	.add
	cmp	eax, AST_OP_SUB
	je	.sub
	cmp	eax, AST_OP_MUL
	je	.mul
	cmp	eax, AST_OP_ADDW
	je	.addw
	cmp	eax, AST_OP_SUBW
	je	.subw
	cmp	eax, AST_OP_ADDS
	je	.adds
	cmp	eax, AST_OP_SUBS
	je	.subs
	rassert	eax eq 0
  .add:
	mov	eax, BFA_OP_ADD
	cmp	dword [lwrf], 0
	je	.out
	mov	eax, BFA_OP_FADD
	jmp	.out
  .sub:
	mov	eax, BFA_OP_SUB
	cmp	dword [lwrf], 0
	je	.out
	mov	eax, BFA_OP_FSUB
	jmp	.out
  .mul:
	mov	eax, BFA_OP_MUL
	cmp	dword [lwrf], 0
	je	.out
	mov	eax, BFA_OP_FMUL
	jmp	.out
  .addw:
	mov	eax, BFA_OP_ADDW
	jmp	.out
  .subw:
	mov	eax, BFA_OP_SUBW
	jmp	.out
  .adds:
	mov	eax, BFA_OP_ADDS
	jmp	.out
  .subs:
	mov	eax, BFA_OP_SUBS
  .out:
	return
endp

; __lwr_cmp_pred(lwrco) -> eax = the BFA_PRED_* for a comparison operator,
; or -1 if `lwrco` is not one.
proc __lwr_cmp_pred, lwrco
	locals
	endl
	mov	eax, [lwrco]
	cmp	eax, AST_OP_LT
	je	.lt
	cmp	eax, AST_OP_LE
	je	.le
	cmp	eax, AST_OP_GT
	je	.gt
	cmp	eax, AST_OP_GE
	je	.ge
	cmp	eax, AST_OP_EQ
	je	.eq
	cmp	eax, AST_OP_NE
	je	.ne
	mov	eax, -1
	return
  .lt:
	mov	eax, BFA_PRED_LT
	return
  .le:
	mov	eax, BFA_PRED_LE
	return
  .gt:
	mov	eax, BFA_PRED_GT
	return
  .ge:
	mov	eax, BFA_PRED_GE
	return
  .eq:
	mov	eax, BFA_PRED_EQ
	return
  .ne:
	mov	eax, BFA_PRED_NE
	return
endp

; __lwr_binary(st, lwrn) -> eax
proc __lwr_binary, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrop, dd
		slot lwra, dd
		slot lwrb2, dd
		slot lwrt, dd
		slot lwrir, dd
		slot lwrpr, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.aux]
	mov	[lwrop], ecx
	mov	ecx, [rax + AstNode.a]
	mov	[lwra], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrb2], ecx

	cmp	dword [lwrop], AST_OP_ET
	je	.sc
	cmp	dword [lwrop], AST_OP_VEL
	je	.sc
	; `..` is only ever `For.a`, and the `For` lowering reads its two ends
	; directly; anywhere else the checker rejected it
	mov	eax, [lwrop]
	rassert	eax ne AST_OP_RANGE

	mov	edi, [lwrop]
	call	__lwr_cmp_pred
	mov	[lwrpr], eax
	cmp	eax, -1
	jne	.compare

	; ---- arithmetic: operands and result share the node's type ----
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	[lwrt], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_ir
	mov	[lwrir], eax
	mov	rdi, rbx
	mov	esi, [lwra]
	call	lwr_expr
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrb2]
	call	lwr_expr
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrop]
	mov	edx, [lwrt]
	call	__lwr_arith_op
	mov	rdi, rbx
	mov	esi, eax
	xor	rdx, rdx
	mov	ecx, [lwrir]
	mov	r8d, r12d
	mov	r9d, r13d
	call	lwr_emit
	return

  .compare:
	; the TYPE TOKEN of a comparison is its OPERANDS' type (ir.inc's
	; `bfa_value_type`: `ty` records the operand type and the produced
	; value is always u1), so it is read off the left operand and not off
	; the node, whose own type is `u1`
	mov	rdi, rbx
	mov	esi, [lwra]
	call	__lwr_nty
	mov	[lwrt], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_ir
	mov	[lwrir], eax
	mov	rdi, rbx
	mov	esi, [lwra]
	call	lwr_expr
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrb2]
	call	lwr_expr
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_kind
	mov	ecx, BFA_OP_CMP
	cmp	eax, AST_TY_FLOAT
	jne	.icmp
	mov	ecx, BFA_OP_FCMP
  .icmp:
	mov	rdi, rbx
	mov	esi, ecx
	mov	edx, [lwrpr]
	mov	ecx, [lwrir]
	mov	r8d, r12d
	mov	r9d, r13d
	call	lwr_emit
	return

  .sc:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_shortcircuit
	return
endp

; __lwr_shortcircuit(st, lwrn) -> eax -- `et` / `vel`.
;
; Spec section 8.6's precedence table lists both operators and no sentence
; gives their evaluation rule (lowering.md finding 13). Short-circuit is what
; the whole lineage does and what a condition with a call in its right operand
; needs, so: a `br` into a block that evaluates the right operand, a join, and
; a phi over a SYNTHETIC variable written on both paths. `et` takes the right
; operand when the left is true; `vel` when it is false.
proc __lwr_shortcircuit, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrop, dd
		slot lwra, dd
		slot lwrb2, dd
		slot lwrv, dd
		slot lwrrhs, dd
		slot lwrjoin, dd
		slot lwrcb, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.aux]
	mov	[lwrop], ecx
	mov	ecx, [rax + AstNode.a]
	mov	[lwra], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrb2], ecx

	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tu1]
	call	lwr_synth_var
	mov	[lwrv], eax

	mov	rdi, rbx
	mov	esi, [lwra]
	call	lwr_expr
	mov	r12d, eax
	mov	ecx, [rbx + LwrState.cur]
	mov	[lwrcb], ecx
	mov	rdi, rbx
	mov	esi, [lwrv]
	mov	edx, ecx
	mov	ecx, r12d
	call	lwr_write_var

	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrrhs], eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrjoin], eax
	cmp	dword [lwrop], AST_OP_ET
	jne	.vel
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, [lwrrhs]
	mov	ecx, [lwrjoin]
	call	lwr_br
	jmp	.built
  .vel:
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, [lwrjoin]
	mov	ecx, [lwrrhs]
	call	lwr_br
  .built:
	mov	rdi, rbx
	mov	esi, [lwrrhs]
	call	lwr_seal

	mov	eax, [lwrrhs]
	mov	[rbx + LwrState.cur], eax
	mov	rdi, rbx
	mov	esi, [lwrb2]
	call	lwr_expr
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrv]
	mov	edx, [rbx + LwrState.cur]
	mov	ecx, r13d
	call	lwr_write_var
	mov	rdi, rbx
	mov	esi, [lwrjoin]
	call	lwr_goto

	mov	rdi, rbx
	mov	esi, [lwrjoin]
	call	lwr_seal
	mov	eax, [lwrjoin]
	mov	[rbx + LwrState.cur], eax
	mov	rdi, rbx
	mov	esi, [lwrv]
	mov	edx, [lwrjoin]
	call	lwr_read_var
	return
endp

; __lwr_cast(st, lwrn) -> eax -- `sicut`. Widening is `zext`/`sext` BY SOURCE
; SIGN, narrowing is `trunc` (IR 2.3's `[OPEN]` on narrowing semantics stands;
; the opcode does not), int<->float is `itof`/`ftoi`, `f32`<->`f64` is
; `fext`/`ftrunc`, and a cast to the same id emits nothing at all.
proc __lwr_cast, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrsrc, dd
		slot lwrst, dd
		slot lwrdt, dd
		slot lwrop, dd
		slot lwrir, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwrsrc], ecx
	mov	rdi, rbx
	mov	esi, [lwrsrc]
	call	__lwr_nty
	mov	[lwrst], eax
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	[lwrdt], eax
	mov	rdi, rbx
	mov	esi, [lwrsrc]
	call	lwr_expr
	mov	r12d, eax
	mov	eax, [lwrst]
	cmp	eax, [lwrdt]
	je	.same

	mov	rdi, rbx
	mov	esi, [lwrdt]
	call	lwr_ty_ir
	mov	[lwrir], eax

	mov	rdi, rbx
	mov	esi, [lwrst]
	call	lwr_ty_kind
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrdt]
	call	lwr_ty_kind
	cmp	r13d, AST_TY_FLOAT
	je	.fromf
	cmp	eax, AST_TY_FLOAT
	je	.itof
	; integer (or `mensura`) to integer: widen or narrow by bit width
	mov	rdi, rbx
	mov	esi, [lwrst]
	call	__lwr_ty_bits
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrdt]
	call	__lwr_ty_bits
	cmp	eax, r13d
	jbe	.trunc
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrst]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.sign]
	mov	eax, BFA_OP_ZEXT
	cmp	ecx, AST_SIGN_I
	jne	.gotop
	mov	eax, BFA_OP_SEXT
	jmp	.gotop
  .trunc:
	mov	eax, BFA_OP_TRUNC
	jmp	.gotop
  .itof:
	mov	eax, BFA_OP_ITOF
	jmp	.gotop
  .fromf:
	cmp	eax, AST_TY_FLOAT
	je	.ftof
	mov	eax, BFA_OP_FTOI
	jmp	.gotop
  .ftof:
	mov	rdi, rbx
	mov	esi, [lwrst]
	call	__lwr_ty_bits
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrdt]
	call	__lwr_ty_bits
	cmp	eax, r13d
	ja	.fext
	mov	eax, BFA_OP_FTRUNC
	jmp	.gotop
  .fext:
	mov	eax, BFA_OP_FEXT
  .gotop:
	mov	rdi, rbx
	mov	esi, eax
	xor	rdx, rdx
	mov	ecx, [lwrir]
	mov	r8d, r12d
	xor	r9, r9
	call	lwr_emit
	return
  .same:
	mov	eax, r12d
	return
endp

; __lwr_ty_bits(st, lwrt) -> eax = the declared bit width of a scalar type.
proc __lwr_ty_bits, lwrsp, lwrt
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.tree]
	mov	esi, [lwrt]
	call	ast_type_at
	mov	eax, [rax + AstType.width]
	return
endp

; ============================================================================
; the prelude interface: name -> row -> symbol, signature and `Func`
; ============================================================================

; __lwr_pre_decl(lwrk) -> rax = the 36-byte `prelude/interface.inc` part B
; record for 1-based row `lwrk`.
proc __lwr_pre_decl, lwrk
	locals
	endl
	mov	rax, [lwrk]
	rassert	rax ge 1
	mov	rcx, EXS_IFACE_DECL_COUNT
	rassert	rax le rcx
	dec	rax
	imul	rax, EXS_IFACE_DECL_SIZE
	lea	rcx, [exsrt_iface_decls]
	add	rax, rcx
	return
endp

; __lwr_pre_row(st, lwrnm) -> eax = the 1-based `prelude/interface.inc` row
; whose Exsecutor name is interner id `lwrnm`, or 0.
;
; THE LOOKUP IS BY NAME, AND THAT IS A FINDING, NOT A CHOICE.
; `checker/types/prim.inc` keeps `Member.d` at ZERO for a prelude member and
; puts the row index in `ChkTyWork.wpre`, a PASS-2 SCRATCH table that is gone
; by the time this pass runs -- its header says so and says why (a
; one-line fix in `checker/rows/compute.inc` would let `Member.d` carry the
; tag, and then this proc disappears). Until that lands the lowering
; re-derives what the checker already knew. The cost is precision: a USER
; method spelled `scribe` on a type with no member of that name would be
; mistaken for the prelude's -- which the checker would have rejected as
; `EXS-E0301` first, so it cannot reach here, but the coupling is real and is
; in the report.
proc __lwr_pre_row, lwrsp, lwrnm
	uses	rbx, r12, r13
	locals
		slot lwrp, dq
		slot lwrl, dq
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.tree]
	mov	rdi, [rdi + Ast.names]
	mov	rsi, [lwrnm]
	call	intern_bytes
	mov	[lwrp], rax
	mov	[lwrl], rdx
	mov	r12, 1
  .loop:
	mov	rax, EXS_IFACE_DECL_COUNT
	cmp	r12, rax
	ja	.none
	mov	rdi, r12
	call	__lwr_pre_decl
	mov	r13, rax
	movzx	ecx, byte [r13]
	cmp	ecx, EXS_IFACE_K_FN
	jne	.next
	mov	rdi, [lwrp]
	mov	rsi, [lwrl]
	lea	rdx, [exsrt_iface_nomina]
	mov	ecx, [r13 + 4]
	add	rdx, rcx
	mov	ecx, [r13 + 8]
	call	__bfa_streq
	test	eax, eax
	jz	.next
	mov	eax, r12d
	return
  .next:
	inc	r12
	jmp	.loop
  .none:
	xor	eax, eax
	return
endp

; __lwr_pre_use(st, lwrk) -- mark prelude row `lwrk` as referenced by this
; module, so `lwr_module`'s first pass emits a declaration for it.
proc __lwr_pre_use, lwrsp, lwrk
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.pref]
	mov	rsi, [lwrk]
	call	vec_get
	mov	dword [rax], 1
	return
endp

; __lwr_pre_func(st, lwrk) -> eax = the 1-based index in `Module.funcs` of the
; declaration emitted for prelude row `lwrk`. Traps if the pre-pass did not
; find the reference this call is making -- the two walks must agree.
proc __lwr_pre_func, lwrsp, lwrk
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.pref]
	mov	rsi, [lwrk]
	call	vec_get
	mov	eax, [rax]
	rassert	eax ge 2		; 1 = "referenced", >= 2 = a func index
					;     biased by one (see lwr_module)
	dec	eax
	return
endp

; ============================================================================
; calls (lowering.md section 2.7)
; ============================================================================

; lwr_expr_arg(st, lwrn) -> eax = the value to pass for argument node `lwrn`:
; a scalar by value, an aggregate by `ptr` to storage the callee reads and
; never writes (IR 2.9).
proc lwr_expr_arg, lwrsp, lwrn
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_scalar
	test	eax, eax
	jz	.agg
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	lwr_expr
	return
  .agg:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	lwr_expr_agg
	return
endp

; __lwr_stage(st, lwrv) -- push one staged call operand.
proc __lwr_stage, lwrsp, lwrv
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.tmpv]
	call	vec_push
	mov	ecx, [lwrv]
	mov	[rax], ecx
	return
endp

; __lwr_call_finish(st, lwrbase, lwrir) -> eax -- copy the staged operands
; [lwrbase, tmpv.len) into one contiguous `extra` run and emit the `call`.
; `lwrir` is the IR result type, 0 for a void callee.
;
; STAGED, NOT APPENDED AS THEY ARE EVALUATED, for the same reason
; `__lwr_add_phi_ops` stages: an argument may itself be a call, and a nested
; call's own `extra` run would otherwise land in the middle of this one.
proc __lwr_call_finish, lwrsp, lwrbase, lwrir
	uses	rbx, r12
	locals
		slot lwrfirst, dd
		slot lwrcnt, dd
		slot lwri, dq
	endl
	mov	rbx, [lwrsp]
	mov	dword [lwrfirst], 0
	mov	dword [lwrcnt], 0
	mov	rax, [lwrbase]
	mov	[lwri], rax
  .loop:
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [rdi + Vec.len]
	cmp	[lwri], rax
	jae	.done
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rsi, [lwri]
	call	vec_get
	mov	r12d, [rax]
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	call	bfa_extra_push
	cmp	dword [lwrfirst], 0
	jne	.notfirst
	inc	eax
	mov	[lwrfirst], eax
  .notfirst:
	mov	eax, [lwrcnt]
	inc	eax
	mov	[lwrcnt], eax
	mov	rax, [lwri]
	inc	rax
	mov	[lwri], rax
	jmp	.loop
  .done:
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [lwrbase]
	mov	[rdi + Vec.len], rax
	mov	eax, [lwrfirst]
	rassert	eax ge 1
	dec	eax
	mov	[lwrfirst], eax
	mov	rdi, rbx
	mov	esi, BFA_OP_CALL
	xor	rdx, rdx
	mov	ecx, [lwrir]
	mov	r8d, [lwrfirst]
	mov	r9d, [lwrcnt]
	call	lwr_emit
	return
endp

; __lwr_call(st, lwrn, lwrdst) -> eax = the call's result value, or the
; hidden-return `ptr` when the declared result is an aggregate, or 0 for a
; `unit` result. `lwrdst` is a caller-supplied destination for an aggregate
; result, 0 to allocate one.
proc __lwr_call, lwrsp, lwrn, lwrdst
	uses	rbx, r12, r13
	locals
		slot lwrcal, dd
		slot lwrk, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwrcal], ecx
	mov	rdi, rbx
	mov	esi, ecx
	call	__lwr_nkind
	mov	[lwrk], eax
	cmp	eax, AST_MEMBER
	je	.member
	rassert	eax eq AST_PATH
	mov	rdi, rbx
	mov	esi, [lwrn]
	mov	edx, [lwrdst]
	xor	rcx, rcx
	call	__lwr_call_user
	return
  .member:
	mov	rdi, rbx
	mov	esi, [lwrn]
	mov	edx, [lwrdst]
	call	__lwr_call_member
	return
endp

; __lwr_func_index(st, lwrd) -> eax = the 1-based `Module.funcs` index of the
; user function declared by `lwrd`. `lwr_module`'s second pass created every
; one of them before any body was walked, so a forward call resolves.
proc __lwr_func_index, lwrsp, lwrd
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.fidx]
	mov	rsi, [lwrd]
	call	vec_get
	mov	eax, [rax]
	rassert	eax ge 1
	return
endp

; __lwr_stage_carriers(st, lwrrow) -- one `ptr` per ATOM item of the callee's
; row, in row order, which CHK 2.8 fixed as ascending atom declaration id.
; A `sicut` ordinal contributes NOTHING: docs/design/runtime.md section 5 says
; so in as many words -- its carriers travel inside the argument whose type
; carries them.
proc __lwr_stage_carriers, lwrsp, lwrrow
	uses	rbx, r12, r13
	locals
		slot lwritems, dd
		slot lwrcnt, dd
		slot lwrbase, dd
	endl
	mov	rbx, [lwrsp]
	cmp	dword [lwrrow], 0
	je	.none
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrrow]
	call	ast_row_at
	mov	ecx, [rax + AstRow.items]
	mov	[lwritems], ecx
	mov	ecx, [rax + AstRow.count]
	mov	[lwrcnt], ecx
	test	ecx, ecx
	jz	.none
	mov	rdi, [rbx + LwrState.tree]
	call	ast_cap_base
	mov	[lwrbase], eax
	xor	r12d, r12d
  .loop:
	cmp	r12d, [lwrcnt]
	jge	.none
	mov	rdi, rbx
	mov	esi, [lwritems]
	mov	eax, r12d
	shl	eax, 1
	mov	edx, eax
	call	__lwr_listat
	test	eax, eax		; CHK_ROW_ATOM = 0; an ordinal is 1
	jnz	.next
	mov	rdi, rbx
	mov	esi, [lwritems]
	mov	eax, r12d
	shl	eax, 1
	inc	eax
	mov	edx, eax
	call	__lwr_listat
	sub	eax, [lwrbase]
	inc	eax			; spec section 4.6 atoms are 1-based
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_cap_find
	rassert	eax ne 0		; an unprovided atom at a call site is
					; `EXS-E0421`, the checker's -- a tree
					; that reaches here with one is a
					; checker bug (lowering.md constraint 4)
	mov	rdi, rbx
	mov	esi, eax
	mov	edx, [rbx + LwrState.cur]
	call	lwr_read_var
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_stage
  .next:
	inc	r12d
	jmp	.loop
  .none:
	return
endp

; __lwr_stage_args(st, lwrn) -- the declared arguments of `Call` node `lwrn`,
; left to right, each fully evaluated (including its own nested calls) before
; the next begins.
proc __lwr_stage_args, lwrsp, lwrn
	uses	rbx, r12
	locals
		slot lwroff, dd
		slot lwrcnt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.b]
	mov	[lwroff], ecx
	mov	ecx, [rax + AstNode.c]
	mov	[lwrcnt], ecx
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
	call	lwr_expr_arg
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_stage
	inc	r12d
	jmp	.loop
  .done:
	return
endp

; __lwr_call_user(st, lwrn, lwrdst, lwrrecv) -> eax -- a direct call to a
; function of this module. `lwrrecv` is the receiver NODE for a method call
; (spec section 8.6 decision 8: `x.m(a)` is `m(x, a)`), 0 otherwise.
;
; The four argument groups of IR 2.9, in order: the hidden return `ptr` when
; the declared result is an aggregate; dictionaries, one per generic parameter
; -- `[UNIMPLEMENTED]`, and a generic call traps rather than passing nothing
; and calling it a lowering; capability carriers, one per atom item of the
; CALLEE's row; then the declared parameters.
proc __lwr_call_user, lwrsp, lwrn, lwrdst, lwrrecv
	uses	rbx, r12, r13
	locals
		slot lwrd, dd
		slot lwrfnty, dd
		slot lwrres, dd
		slot lwrrow, dd
		slot lwrbase, dq
		slot lwrhid, dd
		slot lwrir, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]		; the callee node
	mov	r12d, ecx
	; a generic call needs a dictionary per generic parameter; `Call.d`
	; carries the chosen impls and is 0 when there are none (CHK 2.5)
	mov	ecx, [rax + AstNode.d]
	rassert	ecx eq 0
	mov	rdi, rbx
	mov	esi, r12d
	call	__lwr_node
	mov	ecx, [rax + AstNode.d]
	mov	[lwrd], ecx
	rassert	ecx ne 0
	mov	eax, ecx
	and	eax, LWR_PRELUDE_TAG
	rassert	eax eq 0

	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.ty]
	mov	[lwrfnty], ecx
	rassert	ecx ne 0
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrfnty]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	rassert	ecx eq AST_TY_FN
	mov	r12d, [rax + AstType.a]		; the parameter/result run
	mov	r13d, [rax + AstType.width]	; the parameter count
	mov	ecx, [rax + AstType.b]
	mov	[lwrrow], ecx
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, r13d
	call	__lwr_listat			; extra[a + nparams] = result
	mov	[lwrres], eax

	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [rdi + Vec.len]
	mov	[lwrbase], rax
	mov	rdi, rbx
	mov	esi, [lwrd]
	call	__lwr_func_index
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_stage

	mov	dword [lwrhid], 0
	mov	dword [lwrir], 0
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_ty_void
	test	eax, eax
	jnz	.args
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.hidden
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_ty_ir
	mov	[lwrir], eax
	jmp	.args
  .hidden:
	mov	eax, [lwrdst]
	test	eax, eax
	jnz	.gotdst
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_slot
  .gotdst:
	mov	[lwrhid], eax
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_stage
  .args:
	mov	rdi, rbx
	mov	esi, [lwrrow]
	call	__lwr_stage_carriers
	cmp	dword [lwrrecv], 0
	je	.declared
	mov	rdi, rbx
	mov	esi, [lwrrecv]
	call	lwr_expr_arg
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_stage
  .declared:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_stage_args
	mov	rdi, rbx
	mov	rsi, [lwrbase]
	mov	edx, [lwrir]
	call	__lwr_call_finish
	cmp	dword [lwrhid], 0
	jne	.rethid
	cmp	dword [lwrir], 0
	jne	.out
	xor	eax, eax
	return
  .rethid:
	mov	eax, [lwrhid]
  .out:
	return
endp

; __lwr_recv_is_type(st, lwrrecv) -> eax = 1 if the receiver expression names a
; TYPE rather than a value -- `Scriptor.ad_exitum(a)`. Such a receiver
; contributes no argument (lowering.md 2.7).
proc __lwr_recv_is_type, lwrsp, lwrrecv
	uses	rbx
	locals
		slot lwrd, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrrecv]
	call	__lwr_nkind
	cmp	eax, AST_PATH
	jne	.no
	mov	rdi, rbx
	mov	esi, [lwrrecv]
	call	__lwr_node
	mov	ecx, [rax + AstNode.d]
	mov	[lwrd], ecx
	test	ecx, ecx
	jz	.no
	mov	eax, ecx
	test	eax, LWR_PRELUDE_TAG
	jnz	.yes			; the only prelude name in module scope
					; is `Scriptor`, which is a type
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_STRUCT
	je	.yes
	cmp	ecx, AST_D_TYPUS
	je	.yes
	cmp	ecx, AST_D_IFACE
	je	.yes
  .no:
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __lwr_call_member(st, lwrn, lwrdst) -> eax -- `recv.name(args)`.
;
; `Member.d` distinguishes the two cases and is the checker's: a USER member
; carries its declaration there; a PRELUDE member carries ZERO
; (checker/types/prim.inc's header states why, and __lwr_pre_row above records
; what re-deriving it costs).
proc __lwr_call_member, lwrsp, lwrn, lwrdst
	uses	rbx, r12, r13
	locals
		slot lwrm, dd
		slot lwrrecv, dd
		slot lwrnm, dd
		slot lwrk, dd
		slot lwrrec, dq
		slot lwrsig, dq
		slot lwrres, dd
		slot lwrbase, dq
		slot lwrhid, dd
		slot lwrir, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwrm], ecx
	mov	rdi, rbx
	mov	esi, [lwrm]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwrrecv], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrnm], ecx
	mov	r12d, [rax + AstNode.d]

	test	r12d, r12d
	jz	.prelude
	mov	eax, r12d
	test	eax, LWR_PRELUDE_TAG
	jnz	.prelude

	; ---- a member of a type this module declares ----
	; [UNTESTED]: no example in the tree has one, and the receiver's
	; position in the member's own `fn` type is the checker's convention,
	; not something this pass can check.
	mov	rdi, rbx
	mov	esi, [lwrrecv]
	call	__lwr_recv_is_type
	test	eax, eax
	jnz	.userassoc
	mov	rdi, rbx
	mov	esi, [lwrn]
	mov	edx, [lwrdst]
	mov	ecx, [lwrrecv]
	call	__lwr_call_user
	return
  .userassoc:
	mov	rdi, rbx
	mov	esi, [lwrn]
	mov	edx, [lwrdst]
	xor	rcx, rcx
	call	__lwr_call_user
	return

  .prelude:
	mov	rdi, rbx
	mov	esi, [lwrnm]
	call	__lwr_pre_row
	rassert	eax ne 0		; an unresolved member is `EXS-E0301`,
					; raised in Stage 2 -- never here
	mov	[lwrk], eax
	mov	rdi, rax
	call	__lwr_pre_decl
	mov	[lwrrec], rax
	mov	r13, rax
	mov	ecx, [r13 + 24]		; sig_off
	lea	rax, [exsrt_iface_signa]
	add	rax, rcx
	mov	[lwrsig], rax

	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	[lwrres], eax

	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [rdi + Vec.len]
	mov	[lwrbase], rax
	mov	rdi, rbx
	mov	esi, [lwrk]
	call	__lwr_pre_func
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_stage

	mov	dword [lwrhid], 0
	mov	dword [lwrir], 0
	mov	rax, [lwrsig]
	movzx	ecx, byte [rax + 1]	; the IR return type code
	cmp	ecx, EXS_IFACE_T_VOID
	jne	.scalarret
	; a void IR return is either a `unit` result or an aggregate one
	; already spelled as the hidden leading `ptr` (interface.inc's own
	; words), and the AST result type says which
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_ty_void
	test	eax, eax
	jnz	.recv
	mov	eax, [lwrdst]
	test	eax, eax
	jnz	.gotdst
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_slot
  .gotdst:
	mov	[lwrhid], eax
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_stage
	jmp	.recv
  .scalarret:
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_ty_ir
	mov	[lwrir], eax
  .recv:
	mov	r13, [lwrrec]
	movzx	ecx, byte [r13 + 1]	; flags
	test	ecx, EXS_IFACE_F_ASSOCIATA
	jnz	.args
	mov	rdi, rbx
	mov	esi, [lwrrecv]
	call	lwr_expr_arg
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_stage
  .args:
	; every prelude routine's row is empty (runtime.md 2.2's table), so
	; there are no carrier arguments to stage
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_stage_args
	mov	rdi, rbx
	mov	rsi, [lwrbase]
	mov	edx, [lwrir]
	call	__lwr_call_finish
	cmp	dword [lwrhid], 0
	jne	.rethid
	cmp	dword [lwrir], 0
	jne	.out
	xor	eax, eax
	return
  .rethid:
	mov	eax, [lwrhid]
  .out:
	return
endp

; ============================================================================
; aggregates: `lwr_expr_into` and `lwr_expr_agg`
; ============================================================================

; lwr_expr_into(st, lwrn, lwrdst) -- write the value of NON-SCALAR expression
; `lwrn` through the `ptr` `lwrdst`.
proc lwr_expr_into, lwrsp, lwrn, lwrdst
	uses	rbx, r12
	locals
		slot lwrk, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nkind
	mov	[lwrk], eax
	cmp	eax, AST_LIT
	je	.lit
	cmp	eax, AST_CALL
	je	.call
	; a Path, Member or Index of aggregate type: its storage exists
	; already, so this is a byte copy plus one `retain` per reference field
	; (IR 2.7) -- the retain half is [UNIMPLEMENTED], see __lwr_copy
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	lwr_expr_agg
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	rdi, rbx
	mov	esi, eax
	mov	edx, [lwrdst]
	mov	ecx, r12d
	call	__lwr_copy
	return
  .lit:
	mov	rdi, rbx
	mov	esi, [lwrn]
	mov	edx, [lwrdst]
	call	__lwr_lit_textus
	return
  .call:
	mov	rdi, rbx
	mov	esi, [lwrn]
	mov	edx, [lwrdst]
	call	__lwr_call
	return
endp

; lwr_expr_agg(st, lwrn) -> eax = a `ptr` to the value of non-scalar
; expression `lwrn`. A `Path` to a memory-resident binding passes ITS OWN SLOT
; with no copy -- docs/design/runtime.md section 5's
; `call u64 @imprime_gutenbergio %2 %3` names `s`'s own slot -- and anything
; else is lowered into a fresh one.
proc lwr_expr_agg, lwrsp, lwrn
	uses	rbx, r12
	locals
		slot lwrs, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nkind
	cmp	eax, AST_PATH
	jne	.fresh
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_path		; a slot or an aggregate `param` -- both
	return				;   are already the address
  .fresh:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_slot
	mov	[lwrs], eax
	mov	rdi, rbx
	mov	esi, [lwrn]
	mov	edx, [lwrs]
	call	lwr_expr_into
	mov	eax, [lwrs]
	return
endp

; __lwr_copy(st, lwrt, lwrdst, lwrsrc) -- `copy n %d %s`. Bytes only: IR 2.7
; says a struct holding references is copied as bytes PLUS one `retain` per
; reference field known from the layout, and that half is `[UNTESTED]` and
; `[UNIMPLEMENTED]` -- the hello world's `Scriptor` has no reference field, so
; nothing in this tree exercises it, and guessing at it would be worse than
; recording it.
proc __lwr_copy, lwrsp, lwrt, lwrdst, lwrsrc
	uses	rbx, r12
	locals
		slot lwrbse, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_size
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, [lwrdst]
	mov	ecx, [lwrsrc]
	call	lwr_extra3
	mov	[lwrbse], eax
	mov	rdi, rbx
	mov	esi, BFA_OP_COPY
	xor	rdx, rdx
	xor	rcx, rcx
	mov	r8d, [lwrbse]
	mov	r9d, 3
	call	lwr_emit
	return
endp

; __lwr_lit_textus(st, lwrn, lwrdst) -- a STRING literal, into a 16-byte
; `textus` view (runtime.md 2.3: a pointer word and a length word, no header).
;
; ONE GLOBAL PER LITERAL NODE, NO DE-DUPLICATION: the simplest rule that is a
; function of the walk, and `bfa_global_new` numbers globals densely from 0 in
; creation order, which is literal walk order.
;
; THE BYTES ARE THE SOURCE'S, MINUS THE QUOTES. `Lit.a` interns the token text
; WITH its quotes and with escapes UNDECODED (AST 2.3), because spec section
; 8.4's literal grammar is `[OPEN]`. So a raw newline is one 0A byte, as
; section 8.4 requires, and a backslash sequence comes out as its two source
; bytes -- which is not what any future escape grammar will mean. No fixture
; may depend on a literal containing a backslash until section 8.4 closes;
; runtime.md section 5's 101-byte literal has raw newlines and no backslash,
; so its bytes are stable under any resolution.
;
; POINTER WORD THEN LENGTH WORD, each emitted and stored before the next
; begins: that is the order runtime.md section 5 prints, and the fixture
; compares bytes.
proc __lwr_lit_textus, lwrsp, lwrn, lwrdst
	uses	rbx, r12, r13
	locals
		slot lwrlen, dd
		slot lwrg, dd
		slot lwrv, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.aux]
	rassert	ecx eq AST_LIT_STRING
	mov	r12d, [rax + AstNode.a]
	mov	rdi, [rbx + LwrState.tree]
	mov	rdi, [rdi + Ast.names]
	mov	esi, r12d
	call	intern_bytes
	mov	r12, rax
	mov	r13, rdx
	rassert	r13 ge 2		; the two quotes are always there
	inc	r12
	sub	r13, 2
	mov	[lwrlen], r13d

	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, r13d
	mov	edx, 1
	mov	rcx, r12
	mov	r8d, r13d
	call	bfa_global_new
	mov	[lwrg], eax

	mov	rdi, rbx
	mov	esi, BFA_OP_GADDR
	xor	rdx, rdx
	mov	ecx, [rbx + LwrState.tptr]
	mov	r8d, [lwrg]
	xor	r9, r9
	call	lwr_emit
	mov	[lwrv], eax
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tptr]
	mov	edx, [lwrdst]
	xor	rcx, rcx
	mov	r8d, [lwrv]
	call	lwr_store

	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tu64]
	mov	edx, [lwrlen]
	xor	rcx, rcx
	call	lwr_iconst
	mov	[lwrv], eax
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tu64]
	mov	edx, [lwrdst]
	mov	ecx, EXS_IFACE_TEXTUS_LEN
	mov	r8d, [lwrv]
	call	lwr_store
	return
endp

; __lwr_field_off(st, lwrd) -> eax = the byte offset of field declaration
; `lwrd`, from `Ast.layout`. Traps on a field that is not whole bytes at a
; byte boundary: that needs `loadbits`/`storebits`, which nothing in this
; tree has a fixture for -- `[UNIMPLEMENTED]`, recorded rather than guessed.
;
; ALSO TRAPS ON A MULTI-BYTE FIELD WHOSE ORDER IS NOT `nativus`. Both callers
; (`__lwr_member_read` below, the field store in stmt.inc) hand only the
; OFFSET to `lwr_load`/`lwr_store`, which write `nativus` into the IR
; (lower/cfg.inc) -- the field's own `AstLayout.order` is dropped here. For a
; `u16:maior` field that is a little-endian read of a big-endian place, and
; nothing downstream can tell. It is not hypothetical: before this guard, a
; `u64:maior` field read compiled and emitted `mov rax, qword [rax+0]`, and
; the only reason `u16:maior` did not was that the emitter still refuses
; every width but 64 -- which M3 of the wire-codec plan removes. At or below
; 8 bits there is one byte and no order (spec 5.2 rule 3), so those pass.
; M6 deletes this guard, when field access carries its byte order into
; `load`/`store` (docs/design/ssa-ir.md 2.3 already has the operand).
proc __lwr_field_off, lwrsp, lwrd
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.tree]
	mov	rsi, [lwrd]
	call	ast_layout_at
	movzx	ecx, byte [rax + AstLayout.bitoff]
	rassert	ecx eq 0
	movzx	ecx, byte [rax + AstLayout.width]
	and	ecx, 7
	rassert	ecx eq 0
	; order == nativus, or width <= 8 (AST_ORD_NATIVUS is 0, ast/kinds.inc)
	movzx	ecx, byte [rax + AstLayout.order]
	cmp	ecx, AST_ORD_NATIVUS
	je	.order_ok
	movzx	ecx, byte [rax + AstLayout.width]
	rassert	ecx le 8
  .order_ok:
	mov	eax, [rax + AstLayout.off]
	return
endp

; __lwr_member_read(st, lwrn) -> eax -- a field read `x.f`.
proc __lwr_member_read, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwroff, dd
		slot lwrt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	r12d, [rax + AstNode.a]		; the receiver
	mov	r13d, [rax + AstNode.d]		; the field declaration
	rassert	r13d ne 0
	mov	eax, r13d
	and	eax, LWR_PRELUDE_TAG
	rassert	eax eq 0			; a prelude FIELD read has no
						; fixture; `[UNIMPLEMENTED]`
	mov	rdi, rbx
	mov	esi, r13d
	call	__lwr_field_off
	mov	[lwroff], eax
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_expr_agg
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	[lwrt], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_scalar
	test	eax, eax
	jz	.agg
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_ir
	mov	rdi, rbx
	mov	esi, eax
	mov	edx, r12d
	mov	ecx, [lwroff]
	call	lwr_load
	return
  .agg:
	mov	rdi, rbx
	mov	esi, BFA_OP_ADDR
	xor	rdx, rdx
	mov	ecx, [rbx + LwrState.tptr]
	mov	r8d, r12d
	mov	r9d, [lwroff]
	call	lwr_emit
	return
endp

; __lwr_elem(st, lwrn) -> eax = a `ptr` to element `lwrn.b` of `lwrn.a`, with
; the bounds `chk` IR 2.7 makes explicit so both backends trap at the same
; point.
proc __lwr_elem, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrp, dd
		slot lwri, dd
		slot lwrstr, dd
		slot lwrcnt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	r12d, [rax + AstNode.a]
	mov	r13d, [rax + AstNode.b]

	mov	rdi, rbx
	mov	esi, r12d
	call	__lwr_nty
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, eax
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	rassert	ecx eq AST_TY_ACIES
	mov	ecx, [rax + AstType.width]
	mov	[lwrcnt], ecx
	mov	ecx, [rax + AstType.a]
	mov	rdi, rbx
	mov	esi, ecx
	call	lwr_ty_size
	mov	[lwrstr], eax

	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_expr_agg
	mov	[lwrp], eax
	mov	rdi, rbx
	mov	esi, r13d
	call	lwr_expr
	mov	[lwri], eax
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tu64]
	mov	edx, [lwrcnt]
	xor	rcx, rcx
	call	lwr_iconst
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, BFA_OP_CHK
	xor	rdx, rdx
	xor	rcx, rcx
	mov	r8d, [lwri]
	mov	r9d, r13d
	call	lwr_emit
	mov	rdi, rbx
	mov	esi, [lwrp]
	mov	edx, [lwri]
	mov	ecx, [lwrstr]
	call	lwr_extra3
	mov	rdi, rbx
	mov	esi, BFA_OP_INDEX
	xor	rdx, rdx
	mov	ecx, [rbx + LwrState.tptr]
	mov	r8d, eax
	mov	r9d, 3
	call	lwr_emit
	return
endp

; __lwr_index_read(st, lwrn) -> eax -- `a[i]` in value position.
proc __lwr_index_read, lwrsp, lwrn
	uses	rbx, r12
	locals
		slot lwrt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_elem
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	[lwrt], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_scalar
	test	eax, eax
	jz	.agg
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_ir
	mov	rdi, rbx
	mov	esi, eax
	mov	edx, r12d
	xor	rcx, rcx
	call	lwr_load
	return
  .agg:
	mov	eax, r12d
	return
endp
