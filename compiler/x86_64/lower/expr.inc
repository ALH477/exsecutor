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
	xor	r8, r8			; BFA_ORDER_NATIVUS
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

; __lwr_lit(st, lwrn) -> eax -- a literal. `Lit` keeps TEXT, not a value
; (AST 2.3), so the VALUE is the checker's `konst` side table, at the
; literal's own type -- and the class in `aux` says which constant op
; carries it:
;   * an INT is `lwr_iconst`, the value verbatim;
;   * a FLOAT is `lwr_fconst`, the IEEE bits verbatim. `__chk_ty_fits`
;     overwrote `konst` with the converted bit pattern before the tree
;     reached the lowering, so what `ast_konst_get` hands back here is
;     the pattern -- the same 64 bits for either width, f32's high word
;     zero -- and NO conversion, rounding or arithmetic happens below the
;     checker. A float that arrives here still holding the checker's
;     packed decimal PAIR would be a checker bug, not a lowering one; the
;     class dispatch keeps it from being silently misemitted as either.
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
	cmp	ecx, AST_LIT_FLOAT
	je	.float
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
  .float:
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
	call	lwr_fconst
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
	xor	r8, r8			; BFA_ORDER_NATIVUS
	call	lwr_load
	return
  .slotagg:
	mov	eax, r12d
	return
  .prelude:
	; a bare prelude NAME in value position -- only a prelude TYPE can be
	; one (`Scriptor`, `Lector`), and only as the receiver of an associated
	; call, which __lwr_call handles without ever lowering the path
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
	xor	r8, r8			; BFA_ORDER_NATIVUS
	call	lwr_load
	return
  .derefagg:
	mov	eax, r12d
	return
endp

; __lwr_arith_op(st, lwrao, lwrt) -> eax = the BFA opcode for AST operator
; `lwrao` at AST type `lwrt`. Spec section 5.4's three families are three
; opcodes, never a flag: `+` traps, `+%` wraps, `+|` saturates. The wrapping
; and saturating spellings on a FLOAT are the checker's `EXS-E0305` -- which
; WAS A FINDING, not a fact: until the float wave no raise site existed
; anywhere, and this comment's predecessor asserted a refusal that did not
; happen, so `a +% b` on floats silently lowered as plain addition. The
; checker now refuses it (`__chk_ty_binary`'s `.arith` and the pending
; case's `.bin_wchk`), so a float reaching the wrapped arms below is a
; checker bug and traps on the `rassert` that an unknown op traps on.
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
	cmp	eax, AST_OP_DIV
	je	.div
	cmp	eax, AST_OP_ADDW
	je	.addw
	cmp	eax, AST_OP_SUBW
	je	.subw
	cmp	eax, AST_OP_ADDS
	je	.adds
	cmp	eax, AST_OP_SUBS
	je	.subs
	cmp	eax, AST_OP_AUT
	je	.aut
	cmp	eax, AST_OP_SURSUM
	je	.sursum
	cmp	eax, AST_OP_DEORSUM
	je	.deorsum
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
  .div:
	; `/` -- FLOAT ONLY, by construction rather than by test here: the
	; checker's `.div` arm (and `.bin_div` for the pending case) refused
	; every non-float operand type with `EXS-E0305`, so an integer pair
	; cannot arrive. Spec §5.4's integer division and remainder are
	; `[OPEN]`; this arm is deliberately one opcode with no integer
	; twin to grow into.
	mov	eax, BFA_OP_FDIV
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
	jmp	.out
  ; spec §5.4's exclusive or and the two shifts, one IR opcode each
  ; (ssa-ir.md 2.3's bitwise row). The checker admits only unsigned integer
  ; operands of one type (checker/types/types.inc, `.bitop`), so there is no
  ; float form and `shr` is always the logical one; significance, not
  ; direction, is the language's word, and on the IR's integer values "more
  ; significant" IS `shl`. A count >= the width is the IR's trap, not this
  ; pass's (ssa-ir.md 2.3: `shl`/`shr` are side-effecting for that reason).
  .aut:
	mov	eax, BFA_OP_XOR
	jmp	.out
  .sursum:
	mov	eax, BFA_OP_SHL
	jmp	.out
  .deorsum:
	mov	eax, BFA_OP_SHR
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
		; the whole-acy arm (`.vec`): the interned vf*.N type, the two
		; loaded vectors, the result vector and the result temp's slot
		slot lwrvty, dd
		slot lwrva, dd
		slot lwrvb, dd
		slot lwrvr, dd
		slot lwrpt, dd
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
	call	lwr_ty_kind
	cmp	eax, AST_TY_ACIES
	je	.vec
	mov	rdi, rbx
	mov	esi, [lwrt]
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

  .vec:
	; ---- whole-acy arithmetic (spec §5.4's acies row, stage 5.2b) --------
	; The node's type is `acies<f32/f64, N>`, N in {2, 4, 8}, and BOTH
	; operands are that same acies type: `__chk_ty_aciesbin` (stage 5.2a)
	; admits exactly `+ - * /` on such a pair and raises EXS-E0305 at the
	; operator for everything else, so a lane count outside {2, 4, 8}, an
	; integer element, a mismatched pair or a sixth operator here is a
	; checker bug and traps on the `rassert` below. Aggregates are
	; memory-resident, and the acies base IS the vector image (the lanes'
	; raw IEEE bits back to back, bit baggage -- tests/ir/vec_mem.ir's
	; settlement), so the lowering is one nativus load per operand, one
	; packed op, and one nativus store into a fresh temp:
	;
	;	%pa = <left's address>    (lwr_expr_agg: existing bytes or a temp)
	;	%pb = <right's address>
	;	%va = load vf*.N %pa 0 nativus
	;	%vb = load vf*.N %pb 0 nativus
	;	%vr = vadd|vsub|vmul|vdiv vf*.N %va %vb
	;	%pt = slot N*elem elem		(lwr_ty_size's .acies arm)
	;	store vf*.N %pt 0 nativus %vr
	;
	; and the NODE yields %pt, the temp's ADDRESS -- aggregate residence,
	; exactly as a call or a literal yields one (lwr_expr_agg's `.fresh`).
	; The acies type itself stays `ptr` at the language level
	; (`lwr_ty_ir`); the vector types exist only inside this staging, which
	; is why a vector value never crosses a function boundary and stage
	; 5.1's refusal set holds untouched.
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrt]
	call	ast_type_at
	mov	ecx, [rax + AstType.width]	; the lane count N
	mov	[lwrir], ecx			; reused: the arm does no
					;   scalar `lwrir` lookup
	mov	ecx, [rax + AstType.a]		; the element type
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, ecx
	call	ast_type_at
	mov	esi, BFA_TK_VF32
	cmp	dword [rax + AstType.width], 64
	jne	.vkind
	mov	esi, BFA_TK_VF64
  .vkind:
	; kind + lane count carry everything: width is 0 CANONICALLY
	; (backend_fasmg/ir.inc's BFA_TK_VF32 comment -- the parse-vs-lower
	; float-width discrepancy is not repeated here), `ref` = N
	mov	rdi, [rbx + LwrState.mdl]
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [lwrir]
	call	bfa_type_intern
	mov	[lwrvty], eax
	mov	rdi, rbx
	mov	esi, [lwra]
	call	lwr_expr_agg
	mov	r12d, eax			; %pa
	mov	rdi, rbx
	mov	esi, [lwrb2]
	call	lwr_expr_agg
	mov	r13d, eax			; %pb
	mov	rdi, rbx
	mov	esi, [lwrvty]
	mov	edx, r12d
	xor	ecx, ecx			; the base: offset 0
	xor	r8d, r8d			; BFA_ORDER_NATIVUS
	call	lwr_load
	mov	[lwrva], eax
	mov	rdi, rbx
	mov	esi, [lwrvty]
	mov	edx, r13d
	xor	ecx, ecx
	xor	r8d, r8d
	call	lwr_load
	mov	[lwrvb], eax
	mov	eax, [lwrop]
	cmp	eax, AST_OP_ADD
	je	.vadd
	cmp	eax, AST_OP_SUB
	je	.vsub
	cmp	eax, AST_OP_MUL
	je	.vmul
	cmp	eax, AST_OP_DIV
	je	.vdiv
	rassert	eax eq 0		; __chk_ty_aciesbin admits + - * / only
  .vadd:
	mov	eax, BFA_OP_VADD
	jmp	.vemit
  .vsub:
	mov	eax, BFA_OP_VSUB
	jmp	.vemit
  .vmul:
	mov	eax, BFA_OP_VMUL
	jmp	.vemit
  .vdiv:
	mov	eax, BFA_OP_VDIV
  .vemit:
	mov	rdi, rbx
	mov	esi, eax
	xor	rdx, rdx
	mov	ecx, [lwrvty]
	mov	r8d, [lwrva]
	mov	r9d, [lwrvb]
	call	lwr_emit
	mov	[lwrvr], eax
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_slot			; N*elem bytes (ty.inc's .acies arm)
	mov	[lwrpt], eax
	mov	rdi, rbx
	mov	esi, [lwrvty]
	mov	edx, [lwrpt]
	xor	ecx, ecx
	mov	r8d, [lwrvr]
	xor	r9d, r9d			; BFA_ORDER_NATIVUS
	call	lwr_store
	mov	eax, [lwrpt]
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

; __lwr_cast(st, lwrn) -> eax -- a SCALAR `sicut`. Widening is `zext`/`sext`
; BY SOURCE SIGN, narrowing is `trunc` (spec §5.4 as amended in dafb5e2
; defines narrowing and the equal-width sign change as exactly this
; truncation; this comment said the spec's `[OPEN]` stood, and it did until
; then), int<->float is `itof`/`ftoi`,
; `f32`<->`f64` is `fext`/`ftrunc`, and a cast between two types that lower
; to the same IR type emits nothing at all. The aggregate cast (spec §5.2) is
; never here: its type is not scalar, so it reaches `lwr_expr_agg`.
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
	; TWO AST TYPES, ONE IR TYPE: nothing to convert. `u16:maior` and `u16`
	; are different AST ids and the same `u16` in a register (spec §5.2,
	; D2: no value carries an order), and `mensura` is the target's `u64`
	; (ty.inc). Without this the width comparison below picks `trunc` for
	; equal widths and emits a `trunc u16` of a `u16` -- an instruction
	; whose operand and result types are equal, which IR 2.3's `convert`
	; row does not describe.
	mov	rdi, rbx
	mov	esi, [lwrst]
	call	lwr_ty_ir
	cmp	eax, [lwrir]
	je	.same

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
	jnz	.yes			; the only prelude names in module scope
					; are `Scriptor` and `Lector`, and both
					; are types (checker/types/prim.inc's
					; `__chk_ty_pre_is_type`)
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
	cmp	eax, AST_STRUCTLIT
	je	.structlit
	cmp	eax, AST_ARRAYLIT
	je	.arraylit
	cmp	eax, AST_ARRAYREP
	je	.arrayrep
	; a Path, Member, Index or aggregate Cast: its storage exists already,
	; so this is a byte copy plus one `retain` per reference field (IR 2.7)
	; -- the retain half is [UNIMPLEMENTED], see __lwr_copy. For a Cast
	; this copy IS spec §5.2's "binding it copies": `firma b = f sicut
	; acies<u8, 17>` gets its own seventeen bytes, while every READ of the
	; cast goes through `lwr_expr_agg` and copies nothing.
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
  .structlit:
	mov	rdi, rbx
	mov	esi, [lwrn]
	mov	edx, [lwrdst]
	call	__lwr_structlit
	return
  .arraylit:
	mov	rdi, rbx
	mov	esi, [lwrn]
	mov	edx, [lwrdst]
	call	__lwr_arraylit
	return
  .arrayrep:
	mov	rdi, rbx
	mov	esi, [lwrn]
	mov	edx, [lwrdst]
	call	__lwr_arrayrep
	return
endp

; lwr_expr_agg(st, lwrn) -> eax = a `ptr` to the value of non-scalar
; expression `lwrn`. A `Path` to a memory-resident binding passes ITS OWN SLOT
; with no copy -- docs/design/runtime.md section 5's
; `call u64 @imprime_gutenbergio %2 %3` names `s`'s own slot -- and so do a
; field, an element, a dereference and spec §5.2's one aggregate cast, each of
; which names bytes that already exist. Anything else (a call, a literal) is
; lowered into a fresh slot.
;
; THE FOUR ARMS THAT NAME EXISTING BYTES ARE WHAT STOP A RECURSION: before
; them a `Member`, `Index` or `Cast` of aggregate type went to `.fresh`, whose
; `lwr_expr_into` sent a non-literal, non-call node straight back here --
; forever (the wire-codec plan's B5). The `Binary` arm (stage 5.2b) stops the
; SAME recursion for `a + b` over an `acies<f32/f64, N>`: a Binary is none of
; the literal/call shapes `lwr_expr_into` answers, so without this arm a
; whole-acy operation would orbit the two routines forever; `__lwr_binary`'s
; `.vec` arm stages its result in a temp and yields the temp's address, which
; is exactly the contract this routine promises. The `Cast` arm is the pointer
; passthrough spec §5.2 permits "exactly where no write can observe the
; difference": a read through the cast (an index, a call argument, which IR
; 2.9 makes read-only) cannot, and a write THROUGH a cast is `EXS-E0306` in
; the checker, so none reaches here.
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
	je	.path
	cmp	eax, AST_MEMBER
	je	.member
	cmp	eax, AST_INDEX
	je	.index
	cmp	eax, AST_CAST
	je	.cast
	cmp	eax, AST_UNARY
	je	.unary
	cmp	eax, AST_BINARY
	je	.binary
	jmp	.fresh
  .binary:
	; only `+ - * /` on two `acies<f32/f64, N>` ever reach here as a
	; Binary of aggregate type (the checker's stage-5.2a gate); the vector
	; arm of `__lwr_binary` emits the staging and returns the temp
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_binary
	return
  .path:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_path		; a slot or an aggregate `param` -- both
	return				;   are already the address
  .member:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_member_read	; an aggregate field: `addr %p off`
	return
  .index:
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_elem		; `chk` then `index %p %i stride`
	return
  .cast:
	; the checker admitted an aggregate `sicut` only between a `@transitus`
	; struct and `acies<u8, sizeof>` (spec §5.2): the same bytes
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	rdi, rbx
	mov	esi, ecx
	call	lwr_expr_agg
	return
  .unary:
	; `*p` of aggregate type: `p`'s value is the address (__lwr_unary)
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_unary
	return
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

; __lwr_inplace(st, lwrn) -> eax = 1 if aggregate initialiser `lwrn` may be
; built DIRECTLY in its struct literal's destination rather than in a
; temporary that is then copied there.
;
; WHY THIS EXISTS. `__lwr_structlit`'s first walk calls `lwr_expr_arg` for
; every field, and for an aggregate that builds the value in a fresh `slot`
; and stages its address; the second walk `__lwr_copy`s it into the field. So
; a struct literal pays for each aggregate field TWICE. Measured on
; `examples/streamdb/lector_streamdb.exsc`'s `arbor_percurre`, whose `Arbor`
; is six `[0; 2048]` fields: 53,248 of its 127,016-byte frame is those six
; temporaries. A minimal `structura S { n: mensura, v: acies<u64, 2048> }`
; built from a literal emitted TWO slots, 16,392 and 16,384, where a bare
; `mutabilis x: acies<u64, 2048> = [0; 2048]` emits one -- `lwr_expr_into`
; already lowers an array literal straight into a destination; only the
; struct literal's staging stood in the way.
;
; WHY LITERAL ELEMENTS ONLY, and why that makes the change need no aliasing
; argument at all. The two walks exist so that `f = S { a: f.b, b: f.a }` reads
; both old fields before it writes either, and so initialisers are EVALUATED
; in source order while fields are STORED in declaration order. An array
; literal whose every element is a literal has no side effect and reads no
; storage, so neither ordering can observe when it is built. And it is built
; at exactly the point the copy used to happen -- the same bytes, into the same
; field, at the same moment in the second walk. So this is safe for EVERY
; destination: a fresh binding, an assignment to an existing place, and a
; `redde` through the hidden-return pointer alike.
;
; Anything else keeps the two walks: an element that is a call, a path, a
; nested aggregate or even a negated literal (`[-7; N]` is a unary minus, not
; an `AST_LIT`). Widening this to constant-folded elements is `[OPEN]` and
; would need its own argument about side effects.
proc __lwr_inplace, lwrsp, lwrn
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nkind
	cmp	eax, AST_ARRAYREP
	je	.rep
	cmp	eax, AST_ARRAYLIT
	je	.list
	jmp	.no
  .rep:
	; `[e; N]`: its one element must be a literal
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	r12d, [rax + AstNode.a]
	mov	rdi, rbx
	mov	esi, r12d
	call	__lwr_nkind
	cmp	eax, AST_LIT
	jne	.no
	jmp	.yes
  .list:
	; `[e1, ..., en]`: every element must be a literal
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	r12d, [rax + AstNode.a]		; the list offset
	mov	r13d, [rax + AstNode.b]		; the count
	xor	r14d, r14d
  .el:
	cmp	r14d, r13d
	jge	.yes
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, r14d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_nkind
	cmp	eax, AST_LIT
	jne	.no
	inc	r14d
	jmp	.el
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __lwr_structlit(st, lwrn, lwrdst) -- spec §8.6's struct literal, written
; through `lwrdst`: every initialiser EVALUATED IN SOURCE ORDER, then every
; field STORED IN DECLARATION ORDER (docs/design/wire-codec.md D3). So a
; `@transitus` literal writes bytes 0..N-1 in order whatever order its source
; named the fields, the emitted stores are a function of the declaration
; alone, and `f = S { a: f.b, b: f.a }` reads both old fields before it
; writes either.
;
; The values wait in `LwrState.tmpv` between the two walks, the staging
; vector calls already use and for the same reason: an initialiser may itself
; contain a call, whose own staging pushes and pops above ours and leaves our
; entries where they were.
;
; A scalar field is one `store`/`storebits` exactly as `x.f = v` is (stmt.inc's
; `__lwr_assign`); an aggregate field -- nothing in a `@transitus` type, but a
; `textus` in any other struct -- was evaluated to a `ptr` and is copied into
; `addr %dst off`.
proc __lwr_structlit, lwrsp, lwrn, lwrdst
	uses	rbx, r12, r13, r14
	locals
		slot lwroff, dd
		slot lwrcnt, dd
		slot lwrsd, dd
		slot lwrfd, dd
		slot lwrbase, dq
		slot lwrfo, dd
		slot lwrpk, dd
		slot lwrft, dd
		slot lwrv, dd
		slot lwrnd, dq
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.b]
	mov	[lwroff], ecx
	mov	ecx, [rax + AstNode.c]
	mov	[lwrcnt], ecx
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, eax
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	rassert	ecx eq AST_TY_STRUCT
	mov	ecx, [rax + AstType.a]
	mov	[lwrsd], ecx
	and	ecx, LWR_PRELUDE_TAG
	rassert	ecx eq 0		; the checker refuses a prelude struct

	; ---- evaluate, in SOURCE order ----
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [rdi + Vec.len]
	mov	[lwrbase], rax
	xor	r12d, r12d
  .eval:
	cmp	r12d, [lwrcnt]
	jge	.evaled
	mov	rdi, rbx
	mov	esi, [lwroff]
	mov	edx, r12d
	call	__lwr_listat		; the FieldInit
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	r13d, [rax + AstNode.b]	; its initialiser
	; An all-literal array initialiser is built in place during the SECOND
	; walk (`__lwr_inplace` says why that is unobservable). Stage 0 for it:
	; IR value ids are 1-based (backend_fasmg/ir.inc), so 0 is never a real
	; value and cannot be mistaken for one.
	mov	rdi, rbx
	mov	esi, r13d
	call	__lwr_inplace
	test	eax, eax
	jz	.eval_arg
	xor	eax, eax
	jmp	.eval_stage
  .eval_arg:
	mov	rdi, rbx
	mov	esi, r13d
	call	lwr_expr_arg		; a scalar's value, an aggregate's `ptr`
  .eval_stage:
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_stage
	inc	r12d
	jmp	.eval
  .evaled:

	; ---- store, in DECLARATION order ----
	; A struct's fields are the declarations right after it whose parent it
	; is (typed-ast.md 2.4 -- checker/types/member.inc's `__chk_ty_field_of`
	; reads them the same way).
	mov	rdi, [rbx + LwrState.tree]
	call	ast_decl_count
	mov	[lwrnd], rax
	mov	eax, [lwrsd]
	inc	eax
	mov	[lwrfd], eax
  .field:
	mov	eax, [lwrfd]
	cmp	rax, [lwrnd]
	ja	.done
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrfd]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.parent]
	cmp	ecx, [lwrsd]
	jne	.done
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_FIELD
	jne	.next
	mov	ecx, [rax + AstDecl.ty]
	mov	[lwrft], ecx
	; which initialiser names this field -- exactly one does, or the
	; checker raised EXS-E0304 / E0302 and nothing is lowered
	xor	r12d, r12d
  .find:
	cmp	r12d, [lwrcnt]
	jge	.missing
	mov	rdi, rbx
	mov	esi, [lwroff]
	mov	edx, r12d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	ecx, [rax + AstNode.d]
	cmp	ecx, [lwrfd]
	je	.found
	inc	r12d
	jmp	.find
  .missing:
	rassert	r12d eq -1		; unreachable: see above
  .found:
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rsi, [lwrbase]
	add	rsi, r12
	call	vec_get
	mov	eax, [rax]
	mov	[lwrv], eax
	mov	rdi, rbx
	mov	esi, [lwrfd]
	call	__lwr_field_off
	mov	[lwrfo], eax
	mov	[lwrpk], edx
	mov	rdi, rbx
	mov	esi, [lwrft]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.aggfield
	; the STORED type drops the field's order: a register has none, and
	; the order is the access's (spec §5.2, D2)
	mov	rdi, rbx
	mov	esi, [lwrft]
	call	lwr_ty_ir
	mov	r14d, eax
	mov	edi, [lwrpk]
	call	__lwr_field_bits
	test	eax, eax
	jnz	.bits
	mov	rdi, rbx
	mov	esi, r14d
	mov	edx, [lwrdst]
	mov	ecx, [lwrfo]
	mov	r8d, [lwrv]
	mov	r9d, [lwrpk]
	shr	r9d, 16			; the order
	call	lwr_store
	jmp	.next
  .bits:
	mov	rdi, rbx
	mov	esi, r14d
	mov	edx, [lwrdst]
	mov	ecx, [lwrfo]
	mov	r8d, [lwrpk]
	and	r8d, 0xFF		; the bit offset, MSB-first
	mov	r9d, [lwrv]
	call	lwr_storebits
	jmp	.next
  .aggfield:
	mov	rdi, rbx
	mov	esi, BFA_OP_ADDR
	xor	rdx, rdx
	mov	ecx, [rbx + LwrState.tptr]
	mov	r8d, [lwrdst]
	mov	r9d, [lwrfo]
	call	lwr_emit
	cmp	dword [lwrv], 0
	je	.inplace
	mov	rdi, rbx
	mov	esi, [lwrft]
	mov	edx, eax
	mov	ecx, [lwrv]
	call	__lwr_copy
	jmp	.next
  .inplace:
	; the sentinel the first walk staged: build the literal straight into
	; `addr %dst off`. r12d still holds this field's FieldInit index from
	; `.find` -- every call since preserves it.
	mov	r14d, eax
	mov	rdi, rbx
	mov	esi, [lwroff]
	mov	edx, r12d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	rdi, rbx
	mov	esi, [rax + AstNode.b]	; the initialiser
	mov	edx, r14d
	call	lwr_expr_into
  .next:
	inc	dword [lwrfd]
	jmp	.field
  .done:
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [lwrbase]
	mov	[rdi + Vec.len], rax
	return
endp

; ---- array literals (spec §8.6) --------------------------------------------
; `[e1, …, en]` writes its elements into the destination one store at a time,
; ascending; `[e; N]` evaluates its element ONCE and writes that one value N
; times. There is no `acies` value in a register to build first: spec §6.3
; makes an array "a flat buffer", so a literal IS its stores, exactly as a
; struct literal is (`__lwr_structlit`).
;
; THE LIST FORM EVALUATES EVERY ELEMENT BEFORE IT STORES ANY, through
; `LwrState.tmpv`, for the reason `__lwr_structlit` gives: `a = [a[1], a[0]];`
; must read both old elements before it writes either. Source order and
; ascending index order are the same order here, so the separation costs
; nothing but a staging slot per element and buys the aliasing case.
;
; THE REPEAT FORM IS A LOOP, NOT AN UNROLL, PAST `LWR_ARRAY_UNROLL` ELEMENTS,
; and the threshold is a measured frame cost rather than a taste: the emitter
; gives EVERY instruction id its own eight bytes of frame, the void ones
; included (backend_fasmg/emit.inc's frame layout), so an unrolled `[0; 19008]`
; would carve ~152 KB of dead value slots on top of the array's own 152 KB and
; the default 8 MiB `RLIMIT_STACK` would be in sight. The loop is four blocks
; and a counter whatever N is. Below the threshold the unroll is strictly
; smaller -- no counter, no `index`, no branch -- and it is what the small
; `[0; 4]` in a fixture prints.
;
; WHAT BOUNDS AN ARRAY IS THE STACK, AND NOTHING SAYS SO. Measured on
; x86_64-linux with the default 8 MiB `RLIMIT_STACK`: `[0; 1000000]` of `i64`
; -- 8,000,000 bytes of frame -- builds, runs and exits 0, and `[0; 2000000]`
; is a SIGSEGV with no diagnostic at all. The emitter emits `sub rsp, N` and
; no stack probe, and no `EXS-E` code exists for "this frame is too large"
; (spec §13), so the limit is the ulimit's and the failure mode is a crash.
; REPORTED here rather than guessed at: a cap would need a spec code and a
; target-dependent number, and `tests/programs/acies/` runs 20000 elements
; (160 KB), which is the size the modem's buffers are.
LWR_ARRAY_UNROLL = 8

; __lwr_array_ety(st, lwrn) -> eax = the element's AST type id, edx = its size
; in bytes, which is the stride (spec §6.3: unboxed and contiguous, no padding
; between elements -- `lwr_ty_size`'s `acies` arm and `__lwr_elem`'s `index`
; already agree on this).
proc __lwr_array_ety, lwrsp, lwrn
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, eax
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	rassert	ecx eq AST_TY_ACIES
	mov	r12d, [rax + AstType.a]
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_ty_size
	mov	rdx, rax
	mov	rax, r12
	return
endp

; __lwr_elem_store(st, lwrety, lwrp, lwroffs, lwrv) -- one element, written at
; byte offset `lwroffs` from `lwrp`. A scalar element is one `store` in
; NATIVUS order (spec §5.2 D2: an order belongs to a `@transitus` field's
; place, and an array literal has no field); an aggregate element -- a
; `@transitus` struct -- is `addr` plus `copy`, as an aggregate struct field
; is in `__lwr_structlit`.
proc __lwr_elem_store, lwrsp, lwrety, lwrp, lwroffs, lwrv
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrety]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.agg
	mov	rdi, rbx
	mov	esi, [lwrety]
	call	lwr_ty_ir
	mov	rdi, rbx
	mov	esi, eax
	mov	edx, [lwrp]
	mov	ecx, [lwroffs]
	mov	r8d, [lwrv]
	xor	r9, r9			; BFA_ORDER_NATIVUS
	call	lwr_store
	return
  .agg:
	mov	rdi, rbx
	mov	esi, BFA_OP_ADDR
	xor	rdx, rdx
	mov	ecx, [rbx + LwrState.tptr]
	mov	r8d, [lwrp]
	mov	r9d, [lwroffs]
	call	lwr_emit
	mov	rdi, rbx
	mov	esi, [lwrety]
	mov	edx, eax
	mov	ecx, [lwrv]
	call	__lwr_copy
	return
endp

; __lwr_arraylit(st, lwrn, lwrdst) -- `[e1, …, en]`.
proc __lwr_arraylit, lwrsp, lwrn, lwrdst
	uses	rbx, r12, r13, r14
	locals
		slot lwroff, dd
		slot lwrcnt, dd
		slot lwrety, dd
		slot lwrstr, dd
		slot lwrbase, dq
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
	mov	esi, [lwrn]
	call	__lwr_array_ety
	mov	[lwrety], eax
	mov	[lwrstr], edx

	; ---- evaluate, in SOURCE order ----
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [rdi + Vec.len]
	mov	[lwrbase], rax
	xor	r12d, r12d
  .eval:
	cmp	r12d, [lwrcnt]
	jge	.evaled
	mov	rdi, rbx
	mov	esi, [lwroff]
	mov	edx, r12d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_expr_arg		; a scalar's value, an aggregate's `ptr`
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_stage
	inc	r12d
	jmp	.eval
  .evaled:

	; ---- store, ASCENDING ----
	xor	r12d, r12d
  .store:
	cmp	r12d, [lwrcnt]
	jge	.done
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rsi, [lwrbase]
	add	rsi, r12
	call	vec_get
	mov	r13d, [rax]
	mov	eax, r12d
	imul	eax, [lwrstr]
	mov	r14d, eax
	mov	rdi, rbx
	mov	esi, [lwrety]
	mov	edx, [lwrdst]
	mov	ecx, r14d
	mov	r8d, r13d
	call	__lwr_elem_store
	inc	r12d
	jmp	.store
  .done:
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [lwrbase]
	mov	[rdi + Vec.len], rax
	return
endp

; __lwr_arrayrep(st, lwrn, lwrdst) -- `[e; N]`. The count is read off the
; TYPE, not off the count literal: spec §5.4 puts the lane count in the type,
; the checker has already refused a count that is not a literal or is zero,
; and the type is the one place both agree.
;
;	cur:  %v = <e>              ; ONCE, whatever N is
;	      <write i 0> ; jmp hdr
;	hdr:  %i = <read i>         ; the phi
;	      %c = cmp.lt u64 %i N ; br %c body exit
;	body: %p = index %dst %i STRIDE ; store T %p 0 %v nativus
;	      %i1 = addw u64 %i 1 ; <write i %i1> ; jmp hdr
;	exit:
proc __lwr_arrayrep, lwrsp, lwrn, lwrdst
	uses	rbx, r12, r13, r14
	locals
		slot lwrety, dd
		slot lwrstr, dd
		slot lwrcnt, dd
		slot lwrv, dd
		slot lwrctr, dd
		slot lwrhdr, dd
		slot lwrbb, dd
		slot lwrexit, dd
		slot lwri, dd
		slot lwroffs, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	r12d, [rax + AstNode.a]		; the element expression
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, eax
	call	ast_type_at
	mov	ecx, [rax + AstType.width]
	mov	[lwrcnt], ecx
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_array_ety
	mov	[lwrety], eax
	mov	[lwrstr], edx

	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_expr_arg		; the element, evaluated ONCE
	mov	[lwrv], eax

	mov	eax, [lwrcnt]
	cmp	eax, LWR_ARRAY_UNROLL
	ja	.loop

	; ---- unrolled ----
	xor	r12d, r12d
  .one:
	cmp	r12d, [lwrcnt]
	jge	.out
	mov	eax, r12d
	imul	eax, [lwrstr]
	mov	[lwroffs], eax
	mov	rdi, rbx
	mov	esi, [lwrety]
	mov	edx, [lwrdst]
	mov	ecx, [lwroffs]
	mov	r8d, [lwrv]
	call	__lwr_elem_store
	inc	r12d
	jmp	.one
  .out:
	return

	; ---- the loop ----
  .loop:
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tu64]
	call	lwr_synth_var
	mov	[lwrctr], eax
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tu64]
	xor	rdx, rdx
	xor	rcx, rcx
	call	lwr_iconst
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrctr]
	mov	edx, [rbx + LwrState.cur]
	mov	ecx, r13d
	call	lwr_write_var

	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrhdr], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_jmp
	mov	eax, [lwrhdr]
	mov	[rbx + LwrState.cur], eax

	mov	rdi, rbx
	mov	esi, [lwrctr]
	mov	edx, [lwrhdr]
	call	lwr_read_var
	mov	[lwri], eax
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tu64]
	mov	edx, [lwrcnt]
	xor	rcx, rcx
	call	lwr_iconst
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, BFA_OP_CMP
	mov	edx, BFA_PRED_LT
	mov	ecx, [rbx + LwrState.tu64]
	mov	r8d, [lwri]
	mov	r9d, r13d
	call	lwr_emit
	mov	r14d, eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrbb], eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrexit], eax
	mov	rdi, rbx
	mov	esi, r14d
	mov	edx, [lwrbb]
	mov	ecx, [lwrexit]
	call	lwr_br
	mov	rdi, rbx
	mov	esi, [lwrbb]
	call	lwr_seal
	mov	rdi, rbx
	mov	esi, [lwrexit]
	call	lwr_seal
	mov	eax, [lwrbb]
	mov	[rbx + LwrState.cur], eax

	; the element's address: no `chk`, because the counter is this
	; lowering's own and `%i < N` is the branch that got here
	mov	rdi, rbx
	mov	esi, [lwrdst]
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
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrety]
	mov	edx, r13d
	xor	ecx, ecx
	mov	r8d, [lwrv]
	call	__lwr_elem_store

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
	mov	r8d, [lwri]
	mov	r9d, r13d
	call	lwr_emit
	mov	rdi, rbx
	mov	esi, [lwrctr]
	mov	edx, [rbx + LwrState.cur]
	mov	ecx, eax
	call	lwr_write_var
	mov	rdi, rbx
	mov	esi, [lwrhdr]
	call	lwr_goto
	mov	rdi, rbx
	mov	esi, [lwrhdr]
	call	lwr_seal
	mov	eax, [lwrexit]
	mov	[rbx + LwrState.cur], eax
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
	xor	r9, r9			; BFA_ORDER_NATIVUS
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
	xor	r9, r9			; BFA_ORDER_NATIVUS
	call	lwr_store
	return
endp

; __lwr_field_off(st, lwrd) -> eax = the byte offset of field declaration
; `lwrd`, and edx = the rest of its place, packed: bits 0-7 the bit offset
; (MSB-first, `AstLayout.bitoff`), bits 8-15 the width in bits
; (`AstLayout.width`, 0 for an aggregate field too wide for the byte), bits
; 16-23 the byte order (`AST_ORD_*`, which is `BFA_ORDER_*` -- cfg.inc asserts
; the two numberings are one). `__lwr_field_bits` below reads it.
;
; THE M1 GUARD IS GONE, and this is where it stood. Both callers used to get
; the offset alone and hand it to `lwr_load`/`lwr_store`, which wrote
; `nativus` into the IR: a `u16:maior` field was a little-endian read of a
; big-endian place, and the guard (`order == nativus or width <= 8`) made that
; an `rassert` rather than a silent wrong answer until the order could travel.
; It travels now -- `lwr_load`/`lwr_store` take it -- and a sub-byte field
; goes to `loadbits`/`storebits` instead of trapping, so the guard has nothing
; left to guard. tests/unit/lwr_transitus.asm is the positive test that
; replaced tests/programs/ordo_maior_custodia/'s refusal.
proc __lwr_field_off, lwrsp, lwrd
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.tree]
	mov	rsi, [lwrd]
	call	ast_layout_at
	movzx	edx, byte [rax + AstLayout.order]
	shl	edx, 8
	movzx	ecx, byte [rax + AstLayout.width]
	or	edx, ecx
	shl	edx, 8
	movzx	ecx, byte [rax + AstLayout.bitoff]
	or	edx, ecx
	mov	eax, [rax + AstLayout.off]
	return
endp

; __lwr_field_bits(pack) -> eax = 1 if a SCALAR field with this packed place
; needs `loadbits`/`storebits`: fewer than eight bits, or not starting on a
; byte boundary (a `u8` after a `u4` straddles two bytes, and ssa-ir.md 2.7
; gives `loadbits` "any width <= 8 at any bit offset, straddling a byte if the
; declaration did"). Every other scalar field is whole bytes at a byte
; boundary -- spec §5.2 rule 2 -- and is a `load`/`store` with its order.
proc __lwr_field_bits, lwrfpk
	locals
	endl
	mov	eax, [lwrfpk]
	test	eax, 0xFF
	jnz	.yes
	shr	eax, 8
	and	eax, 0xFF
	cmp	eax, 8
	jb	.yes
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __lwr_member_read(st, lwrn) -> eax -- a field read `x.f`. A scalar field is
; `load T %p off ORDER` or, below a byte, `loadbits T %p byte bit`; `T` is
; the Member node's type, which spec §5.2 (D2) makes the field's type WITHOUT
; its order -- the order is on the access, not on the value.
proc __lwr_member_read, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwroff, dd
		slot lwrpk, dd
		slot lwrt, dd
		slot lwrir, dd
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
	mov	[lwrpk], edx
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
	mov	[lwrir], eax
	mov	edi, [lwrpk]
	call	__lwr_field_bits
	test	eax, eax
	jnz	.bits
	mov	rdi, rbx
	mov	esi, [lwrir]
	mov	edx, r12d
	mov	ecx, [lwroff]
	mov	r8d, [lwrpk]
	shr	r8d, 16				; the order
	call	lwr_load
	return
  .bits:
	mov	rdi, rbx
	mov	esi, [lwrir]
	mov	edx, r12d
	mov	ecx, [lwroff]
	mov	r8d, [lwrpk]
	and	r8d, 0xFF			; the bit offset, MSB-first
	call	lwr_loadbits
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
	xor	r8, r8			; BFA_ORDER_NATIVUS
	call	lwr_load
	return
  .agg:
	mov	eax, r12d
	return
endp
