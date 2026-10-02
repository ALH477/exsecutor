; compiler/x86_64/lower/sum.inc
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
; SUM VALUES, LOWERED -- docs/design/sum-types.md D6's layout, written and read.
; A sum is an aggregate ("bytes behind a `ptr`", lowering.md section 2.2's
; table): a binding of one is a `slot`, a parameter a `ptr`, a result the
; hidden-return `ptr`. NO NEW IR INSTRUCTION is used -- a constructor is
; `iconst` + `store` of the tag and one `store` (or `storebits`, or `addr` +
; `copy`) per payload element; a match is a `load` of the tag and the existing
; `cmp.eq` chain of `__lwr_discerne`; a binding is a `load` (or `loadbits`, or
; `addr` + `copy` into its own `slot`). So the C backend, which lowers the IR
; and never the tree, needs nothing new, and the two backends agree by
; construction on everything but these opcodes' own lowering, which the
; differential phase already holds to the reference.
;
; THE PLACE OF EVERYTHING: the tag at byte 0, `ast_sum_tagw` bytes wide (the
; same rule pass 4 wrote into every variant's `Ast.layout` entry), as an
; unsigned integer of that width in `:nativus` order (D6); payload element `i`
; at the tag's width plus the running sum of elements `0..i-1`'s LAYOUT widths
; in bits -- `lwr_ty_lbits`, which answers what checker/rows/layout.inc's
; `__chk_lay_ty` answers for every kind that routine sizes, and TRAPS on the
; kinds it gives width 0 (`textus`, `fn`, `cap`, ...). That is deliberate: a
; payload of one of those has a layout that is "wrong rather than absent" (D6
; and layout.inc's header), and writing it would overrun the next element.
; `[OPEN]` until those kinds are sized; no fixture holds one.
;
; THE ELEMENT TYPES OF AN INSTANCE are `ast_sum_subst`'s, the routine pass 4
; sized the instance with, so a `slot` and the stores into it cannot disagree.
;
; WRITE ORDER: every payload expression is EVALUATED, left to right, before the
; tag or any element is STORED -- `__lwr_structlit`'s two walks, for the same
; reason: `r = prosperum(f(r))` must read `r` before it overwrites it.
;
; `?` (D4) is the last section of this file: the same tag test and payload
; read, with an early return between them.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.2, §8.5, §8.6;
; docs/design/sum-types.md D2, D3, D4, D6; docs/design/lowering.md section 2.8.
; -----------------------------------------------------------------------------

; __lwr_isctor(st, lwrn) -> eax = the variant declaration expression `lwrn`
; constructs (a `Path` to a variant, or a `Call` of one), 0 otherwise. The
; checker's `__chk_ty_isctor`, restated: this pass includes no checker file.
proc __lwr_isctor, lwrsp, lwrn
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lwrsp]
	mov	r12d, [lwrn]
	test	r12d, r12d
	jz	.no
	mov	rdi, rbx
	mov	esi, r12d
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_CALL
	jne	.path
	mov	r12d, [rax + AstNode.a]
	mov	rdi, rbx
	mov	esi, r12d
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.kind]
  .path:
	cmp	ecx, AST_PATH
	jne	.no
	mov	r12d, [rax + AstNode.d]
	test	r12d, r12d
	jz	.no
	test	r12d, LWR_PRELUDE_TAG
	jnz	.no
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, r12d
	call	ast_decl_at
	cmp	byte [rax + AstDecl.kind], AST_D_VARIANT
	jne	.no
	mov	eax, r12d
	return
  .no:
	xor	eax, eax
	return
endp

; lwr_ty_lbits(st, lwrt) -> rax = type `lwrt`'s LAYOUT width in bits: what
; `__chk_lay_ty` (checker/rows/layout.inc) answers, for the kinds it sizes. A
; pointer-sized kind is 64 bits: `ref`/`refc` are already 64 in `lwr_ty_ir`,
; and every `--hospes` row this tree has is a 64-bit target.
proc lwr_ty_lbits, lwrsp, lwrt
	uses	rbx, r12
	locals
		slot lwrn, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrt]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_INT
	je	.width
	cmp	ecx, AST_TY_FLOAT
	je	.width
	cmp	ecx, AST_TY_MENSURA
	je	.width
	cmp	ecx, AST_TY_PTR
	je	.word
	cmp	ecx, AST_TY_REF
	je	.word
	cmp	ecx, AST_TY_REFC
	je	.word
	cmp	ecx, AST_TY_BORROW
	je	.word
	cmp	ecx, AST_TY_ACIES
	je	.acies
	cmp	ecx, AST_TY_STRUCT
	je	.bytes
	cmp	ecx, AST_TY_SUM
	je	.bytes
	rassert	ecx eq 0		; [OPEN]: a kind pass 4 sizes as 0 --
					; this file's header
  .width:
	mov	eax, [rax + AstType.width]
	return
  .word:
	mov	eax, 64
	return
  .acies:
	mov	ecx, [rax + AstType.width]
	mov	[lwrn], ecx
	mov	esi, [rax + AstType.a]
	mov	rdi, rbx
	call	lwr_ty_lbits
	mov	ecx, [lwrn]
	imul	rax, rcx
	return
  .bytes:
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_size
	shl	rax, 3
	return
endp

; __lwr_sum_info(st, lwrt) -> rax = the `SumBody` node of sum type `lwrt`'s
; declaration, rdx = its variant count.
proc __lwr_sum_info, lwrsp, lwrt
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrt]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	rassert	ecx eq AST_TY_SUM
	mov	esi, [rax + AstType.a]
	mov	rdi, [rbx + LwrState.tree]
	call	ast_sum_body
	rassert	eax ne 0
	return
endp

; lwr_sum_size(st, lwrt) -> rax = the byte size of sum INSTANCE `lwrt` (a
; generic sum applied to arguments; a non-generic sum reads its declaration's
; `Ast.layout` entry in `lwr_ty_size`). D6 over the substituted elements:
; the tag, then the widest payload, each payload packed and rounded up to
; whole bytes -- checker/rows/layout.inc's `__chk_lay_suminst`, here.
proc lwr_sum_size, lwrsp, lwrt
	uses	rbx, r12, r13, r14
	locals
		slot lwrvl, dd
		slot lwrvn, dd
		slot lwrpl, dd
		slot lwrpn, dd
		slot lwrmx, dq
		slot lwrpb, dq
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	__lwr_sum_info
	mov	[lwrvn], edx
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwrvl], ecx
	mov	qword [lwrmx], 0
	xor	r12d, r12d
  .variant:
	cmp	r12d, [lwrvn]
	jae	.done
	mov	rdi, rbx
	mov	esi, [lwrvl]
	mov	edx, r12d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	ecx, [rax + AstNode.b]
	mov	[lwrpl], ecx
	mov	ecx, [rax + AstNode.c]
	mov	[lwrpn], ecx
	mov	qword [lwrpb], 0
	xor	r13d, r13d
  .elem:
	cmp	r13d, [lwrpn]
	jae	.elems
	mov	rdi, rbx
	mov	esi, [lwrpl]
	mov	edx, r13d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_nty
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrt]
	mov	edx, eax
	call	ast_sum_subst
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_lbits
	add	[lwrpb], rax
	inc	r13d
	jmp	.elem
  .elems:
	mov	rax, [lwrpb]
	add	rax, 7
	shr	rax, 3
	cmp	rax, [lwrmx]
	jbe	.next
	mov	[lwrmx], rax
  .next:
	inc	r12d
	jmp	.variant
  .done:
	mov	edi, [lwrvn]
	call	ast_sum_tagw
	add	rax, [lwrmx]
	mov	edx, 1
	return
endp

; __lwr_sum_tag(st, lwrt) -> eax = the IR type of sum type `lwrt`'s tag (`u8`,
; `u16` or `u32`), edx = its width in bytes.
proc __lwr_sum_tag, lwrsp, lwrt
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	__lwr_sum_info
	mov	edi, edx
	call	ast_sum_tagw
	mov	r12d, eax
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, BFA_TK_U
	xor	edx, edx
	mov	ecx, r12d
	shl	ecx, 3
	xor	r8, r8
	call	bfa_type_intern
	mov	edx, r12d
	return
endp

; __lwr_sum_vidx(st, lwrt, lwrvd) -> eax = variant `lwrvd`'s index in sum type
; `lwrt`'s declaration -- its tag.
proc __lwr_sum_vidx, lwrsp, lwrt, lwrvd
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	__lwr_sum_info
	mov	r13d, edx
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	r12d, [rax + AstNode.a]
	xor	r14d, r14d
  .scan:
	cmp	r14d, r13d
	jae	.miss
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, r14d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	ecx, [rax + AstNode.d]
	cmp	ecx, [lwrvd]
	je	.hit
	inc	r14d
	jmp	.scan
  .miss:
	rassert	r14d eq -1		; the checker resolved it to this sum
  .hit:
	mov	eax, r14d
	return
endp

; __lwr_sum_place(st, lwrt, lwrvd, lwri) -> eax = payload element `lwri` of
; variant `lwrvd`'s type, read through sum type `lwrt`; edx = its BIT offset
; from the start of the sum (the tag's width, then the preceding elements').
proc __lwr_sum_place, lwrsp, lwrt, lwrvd, lwri
	uses	rbx, r12, r13, r14
	locals
		slot lwrpl, dd
		slot lwret, dd
		slot lwrbo, dq
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	__lwr_sum_info
	mov	edi, edx
	call	ast_sum_tagw
	shl	rax, 3
	mov	[lwrbo], rax
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrvd]
	call	ast_decl_at
	mov	esi, [rax + AstDecl.node]
	mov	rdi, rbx
	call	__lwr_node
	mov	ecx, [rax + AstNode.b]
	mov	[lwrpl], ecx
	mov	ecx, [rax + AstNode.c]
	rassert	ecx gt 0
	xor	r12d, r12d
  .elem:
	mov	rdi, rbx
	mov	esi, [lwrpl]
	mov	edx, r12d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_nty
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrt]
	mov	edx, eax
	call	ast_sum_subst
	mov	[lwret], eax
	cmp	r12d, [lwri]
	je	.found
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_lbits
	add	[lwrbo], rax
	inc	r12d
	jmp	.elem
  .found:
	mov	eax, [lwret]
	mov	rdx, [lwrbo]
	return
endp

; __lwr_sum_store(st, lwret, lwrp, lwrbo, lwrv) -- write value `lwrv` of type
; `lwret` at bit offset `lwrbo` of the sum at `lwrp`: a `store` when it is a
; whole number of bytes at a byte boundary, a `storebits` below a byte (the
; MSB-first bit operand of cfg.inc, D6's packing being §5.2's), and `addr` +
; `copy` for an aggregate. An element that is neither -- a scalar wider than
; a byte that does not start on one, which only a payload holding a sub-byte
; integer before it can produce -- traps: `[OPEN]`, no fixture holds one.
proc __lwr_sum_store, lwrsp, lwret, lwrp, lwrbo, lwrv
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwret]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.agg
	mov	rdi, rbx
	mov	esi, [lwret]
	call	lwr_ty_ir
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwret]
	call	lwr_ty_lbits
	mov	r13, rax
	mov	rax, [lwrbo]
	or	rax, r13
	test	eax, 7
	jnz	.bits
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, [lwrp]
	mov	ecx, [lwrbo]
	shr	ecx, 3
	mov	r8d, [lwrv]
	xor	r9, r9			; BFA_ORDER_NATIVUS (D6)
	call	lwr_store
	return
  .bits:
	rassert	r13 le 8
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, [lwrp]
	mov	ecx, [lwrbo]
	shr	ecx, 3
	mov	r8d, [lwrbo]
	and	r8d, 7
	mov	r9d, [lwrv]
	call	lwr_storebits
	return
  .agg:
	mov	eax, [lwrbo]
	and	eax, 7
	rassert	eax eq 0		; whole bytes: see above
	mov	rdi, rbx
	mov	esi, BFA_OP_ADDR
	xor	rdx, rdx
	mov	ecx, [rbx + LwrState.tptr]
	mov	r8d, [lwrp]
	mov	r9d, [lwrbo]
	shr	r9d, 3
	call	lwr_emit
	mov	rdi, rbx
	mov	esi, [lwret]
	mov	edx, eax
	mov	ecx, [lwrv]
	call	__lwr_copy
	return
endp

; __lwr_sum_load(st, lwret, lwrp, lwrbo) -> eax = the scalar element of type
; `lwret` at bit offset `lwrbo` of the sum at `lwrp` (`load` or `loadbits`,
; as `__lwr_sum_store` writes it), or -- for an aggregate element -- its
; address, `addr %p off`.
proc __lwr_sum_load, lwrsp, lwret, lwrp, lwrbo
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwret]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.agg
	mov	rdi, rbx
	mov	esi, [lwret]
	call	lwr_ty_ir
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwret]
	call	lwr_ty_lbits
	mov	r13, rax
	mov	rax, [lwrbo]
	or	rax, r13
	test	eax, 7
	jnz	.bits
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, [lwrp]
	mov	ecx, [lwrbo]
	shr	ecx, 3
	xor	r8, r8			; BFA_ORDER_NATIVUS
	call	lwr_load
	return
  .bits:
	rassert	r13 le 8
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, [lwrp]
	mov	ecx, [lwrbo]
	shr	ecx, 3
	mov	r8d, [lwrbo]
	and	r8d, 7
	call	lwr_loadbits
	return
  .agg:
	mov	eax, [lwrbo]
	and	eax, 7
	rassert	eax eq 0
	mov	rdi, rbx
	mov	esi, BFA_OP_ADDR
	xor	rdx, rdx
	mov	ecx, [rbx + LwrState.tptr]
	mov	r8d, [lwrp]
	mov	r9d, [lwrbo]
	shr	r9d, 3
	call	lwr_emit
	return
endp

; __lwr_ctor(st, lwrn, lwrdst) -- constructor `lwrn` (`__lwr_isctor` said it
; is one), written through `lwrdst`: every payload expression evaluated, then
; the tag stored, then every element. This file's header says why that order.
proc __lwr_ctor, lwrsp, lwrn, lwrdst
	uses	rbx, r12, r13, r14
	locals
		slot lwrao, dd
		slot lwran, dd
		slot lwrvd, dd
		slot lwrst, dd
		slot lwrtg, dd
		slot lwrbs, dq
		slot lwret, dd
		slot lwrbo, dd
	endl
	mov	rbx, [lwrsp]
	mov	dword [lwrao], 0
	mov	dword [lwran], 0
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_isctor
	rassert	eax ne 0
	mov	[lwrvd], eax
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	[lwrst], eax
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	cmp	word [rax + AstNode.kind], AST_CALL
	jne	.evaled0
	mov	ecx, [rax + AstNode.b]
	mov	[lwrao], ecx
	mov	ecx, [rax + AstNode.c]
	mov	[lwran], ecx
  .evaled0:
	; ---- evaluate, left to right ----
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [rdi + Vec.len]
	mov	[lwrbs], rax
	xor	r12d, r12d
  .eval:
	cmp	r12d, [lwran]
	jae	.evaled
	mov	rdi, rbx
	mov	esi, [lwrao]
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
	; ---- the tag ----
	mov	rdi, rbx
	mov	esi, [lwrst]
	call	__lwr_sum_tag
	mov	[lwrtg], eax
	mov	rdi, rbx
	mov	esi, [lwrst]
	mov	edx, [lwrvd]
	call	__lwr_sum_vidx
	mov	rdi, rbx
	mov	esi, [lwrtg]
	mov	edx, eax
	xor	ecx, ecx
	call	lwr_iconst
	mov	rdi, rbx
	mov	esi, [lwrtg]
	mov	edx, [lwrdst]
	xor	ecx, ecx
	mov	r8d, eax
	xor	r9, r9			; BFA_ORDER_NATIVUS (D6)
	call	lwr_store
	; ---- the elements ----
	xor	r12d, r12d
  .elem:
	cmp	r12d, [lwran]
	jae	.done
	mov	rdi, rbx
	mov	esi, [lwrst]
	mov	edx, [lwrvd]
	mov	ecx, r12d
	call	__lwr_sum_place
	mov	[lwret], eax
	mov	[lwrbo], edx
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rsi, [lwrbs]
	add	rsi, r12
	call	vec_get
	mov	r13d, [rax]
	mov	rdi, rbx
	mov	esi, [lwret]
	mov	edx, [lwrdst]
	mov	ecx, [lwrbo]
	mov	r8d, r13d
	call	__lwr_sum_store
	inc	r12d
	jmp	.elem
  .done:
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [lwrbs]
	mov	[rdi + Vec.len], rax
	return
endp

; __lwr_sum_bind(st, lwrcs, lwrp, lwrt) -- arm `lwrcs` (a `Casus` whose
; pattern names a variant) has matched the sum of type `lwrt` at `lwrp`: give
; each binding its payload element. A scalar binding the checker left in a
; register is written as a Braun variable, exactly as a `firma` with an
; initializer is (stmt.inc's `__lwr_binding`); a memory-resident one --
; every aggregate, and a scalar whose address is taken -- gets its OWN
; `slot`, filled by a load and a store or by a `copy`. A copy and not the
; scrutinee's own bytes, because a binding is a value: an arm that assigns the
; scrutinee must not change what the binding holds.
proc __lwr_sum_bind, lwrsp, lwrcs, lwrp, lwrt
	uses	rbx, r12, r13, r14
	locals
		slot lwrbo2, dd
		slot lwrbn, dd
		slot lwrvd, dd
		slot lwrd, dd
		slot lwret, dd
		slot lwrbit, dd
		slot lwrir, dd
		slot lwrs, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrcs]
	call	__lwr_node
	mov	ecx, [rax + AstNode.c]
	mov	[lwrbo2], ecx
	mov	ecx, [rax + AstNode.d]
	mov	[lwrbn], ecx
	mov	esi, [rax + AstNode.a]
	mov	rdi, rbx
	call	__lwr_node
	mov	ecx, [rax + AstNode.d]
	mov	[lwrvd], ecx
	xor	r12d, r12d
  .bind:
	cmp	r12d, [lwrbn]
	jae	.out
	mov	rdi, rbx
	mov	esi, [lwrbo2]
	mov	edx, r12d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	ecx, [rax + AstNode.d]
	mov	[lwrd], ecx
	rassert	ecx ne 0
	mov	rdi, rbx
	mov	esi, [lwrt]
	mov	edx, [lwrvd]
	mov	ecx, r12d
	call	__lwr_sum_place
	mov	[lwret], eax
	mov	[lwrbit], edx
	mov	rdi, rbx
	mov	esi, [lwret]
	call	lwr_ty_ir
	mov	[lwrir], eax
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.flags]
	test	ecx, AST_F_MEMRES
	jnz	.memres
	mov	rdi, rbx
	mov	esi, [lwret]
	mov	edx, [lwrp]
	mov	ecx, [lwrbit]
	call	__lwr_sum_load
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, LWR_VK_SSA
	mov	ecx, [lwrir]
	xor	r8, r8
	call	lwr_var_set
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, [rbx + LwrState.cur]
	mov	ecx, r13d
	call	lwr_write_var
	jmp	.next
  .memres:
	mov	rdi, rbx
	mov	esi, [lwret]
	call	lwr_slot
	mov	[lwrs], eax
	mov	rdi, rbx
	mov	esi, [lwrd]
	mov	edx, LWR_VK_SLOT
	mov	ecx, [lwrir]
	mov	r8d, [lwrs]
	call	lwr_var_set
	mov	rdi, rbx
	mov	esi, [lwret]
	mov	edx, [lwrp]
	mov	ecx, [lwrbit]
	call	__lwr_sum_load
	mov	r13d, eax		; the value, or the element's address
	mov	rdi, rbx
	mov	esi, [lwret]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.copy
	mov	rdi, rbx
	mov	esi, [lwrir]
	mov	edx, [lwrs]
	xor	ecx, ecx
	mov	r8d, r13d
	xor	r9, r9
	call	lwr_store
	jmp	.next
  .copy:
	mov	rdi, rbx
	mov	esi, [lwret]
	mov	edx, [lwrs]
	mov	ecx, r13d
	call	__lwr_copy
  .next:
	inc	r12d
	jmp	.bind
  .out:
	return
endp

; =============================================================================
; `?` -- docs/design/sum-types.md D4
; =============================================================================
; `e?` is the `discerne` D2 already admits, written as one token, and it is
; lowered as exactly that -- NO NEW IR INSTRUCTION, so the C backend, which
; lowers the IR, needs nothing:
;
;   %p  = <e>                          evaluated ONCE (`lwr_expr_agg`)
;   %t  = load uT %p 0                 the tag
;   %c  = cmp.eq uT %t <adversum>
;         br %c bE bK
;   bE: <the function's result> = adversum(<e's E payload>), released, `ret`
;   bK: the `prosperum` payload, read from %p
;
; THE EARLY RETURN BUILDS THE RESULT IN THE RESULT'S OWN LAYOUT. `e` is an
; `eventus<T, E>` and the function returns an `eventus<U, E>`; D6 sizes an
; instance from its payloads, so the two are different sizes whenever `T` and
; `U` are, and copying `e`'s bytes would overrun (or underfill) the hidden
; result. So the tag is stored at the result's tag width and the `E` element
; is placed through `__lwr_sum_place` on EACH type separately -- loaded from
; `e` at its place there, stored into the result at its place there -- the
; constructor's own two halves (`__lwr_ctor`), with the payload read instead of
; evaluated. The load comes before the tag store, `__lwr_ctor`'s order,
; because a caller may hand the operand's own storage in as the hidden result
; (`s = f(s)`, where `f` takes `s?`; measured in the emitted C): a scalar `E`
; is then read before anything is written. An aggregate `E`'s "load" is its
; address and its `copy` comes after the tag store -- still sound, since the
; tag is bytes [0, tag width) and `E` starts at the tag width in both
; layouts (D6: one declaration, two variants, one tag width), so the store
; never touches what the copy reads. With the storage shared the copy is of
; a region onto itself, which `redde s` already emits there; sum-types.md
; F12 records it as `[OPEN]` for both.
; Then every open frame is released to the function root and `ret` -- what
; `redde` emits (stmt.inc's `__lwr_redde`), since that is what this is.
;
; The VALUE is read in `bK`, whose one predecessor is the test: a scalar `T`
; is a `load`/`loadbits`; an aggregate `T` answers the payload's ADDRESS,
; `addr %p off`, which is `lwr_expr_agg`'s contract ("a `ptr` to the value"),
; so a binding initializer or an argument copies from -- or passes -- the
; operand's own bytes, as it would a field's. Evaluation continues in `bK`:
; everything after the `?` in the enclosing expression is lowered there, and
; every value computed before it (an earlier argument, the left operand) is
; defined in a block that dominates `bK`.

; __lwr_fn_result(st) -> eax = the result type of the function being lowered:
; the last entry of its `fn` type's list, as `lwr_fn` reads it.
proc __lwr_fn_result, lwrsp
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [rbx + LwrState.fnd]
	rassert	esi ne 0
	call	ast_decl_at
	mov	esi, [rax + AstDecl.ty]
	mov	rdi, [rbx + LwrState.tree]
	call	ast_type_at
	mov	esi, [rax + AstType.a]
	mov	edx, [rax + AstType.width]
	mov	rdi, rbx
	call	__lwr_listat
	return
endp

; __lwr_try_var(st, lwrt, lwri) -> eax = variant `lwri` (AST_PRE_V_*) of
; `eventus` instance `lwrt`: its declaration.
proc __lwr_try_var, lwrsp, lwrt, lwri
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	__lwr_sum_info
	rassert	edx eq 2			; prelude/eventus.inc's two
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	esi, [rax + AstNode.a]
	mov	rdi, rbx
	mov	edx, [lwri]
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	eax, [rax + AstNode.d]
	return
endp

; __lwr_try(st, lwrn) -> eax = the value of `Try` node `lwrn`: a scalar `T`'s
; value, an aggregate `T`'s address, 0 for a `unit` one. `cur` is left in the
; success block. This section's header gives the shape.
proc __lwr_try, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrot, dd		; the operand's type, eventus<T, E>
		slot lwrop, dd		; ... its address
		slot lwrtg, dd		; ... its tag's IR type
		slot lwrbe, dd		; the early-return block
		slot lwrbk, dd		; the success block
		slot lwrrt, dd		; the function's result, eventus<U, E>
		slot lwrvd, dd		; a variant declaration
		slot lwret, dd		; an element type
		slot lwrbo, dd		; ... and its bit offset
		slot lwrv, dd		; the `E` payload
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	r12d, [rax + AstNode.a]		; the operand
	mov	rdi, rbx
	mov	esi, r12d
	call	__lwr_nty
	mov	[lwrot], eax
	; ---- evaluate once, test the tag ----
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_expr_agg
	mov	[lwrop], eax
	mov	rdi, rbx
	mov	esi, [lwrot]
	call	__lwr_sum_tag
	mov	[lwrtg], eax
	mov	rdi, rbx
	mov	esi, eax
	mov	edx, [lwrop]
	xor	ecx, ecx
	xor	r8, r8			; BFA_ORDER_NATIVUS (D6)
	call	lwr_load
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrtg]
	mov	edx, AST_PRE_V_ADVERSUM
	xor	ecx, ecx
	call	lwr_iconst
	mov	rdi, rbx
	mov	esi, BFA_OP_CMP
	mov	edx, BFA_PRED_EQ
	mov	ecx, [lwrtg]
	mov	r8d, r12d
	mov	r9d, eax
	call	lwr_emit
	mov	r12d, eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrbe], eax
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrbk], eax
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, [lwrbe]
	mov	ecx, [lwrbk]
	call	lwr_br
	mov	rdi, rbx
	mov	esi, [lwrbe]
	call	lwr_seal
	mov	rdi, rbx
	mov	esi, [lwrbk]
	call	lwr_seal

	; ---- bE: return adversum(E) in the result's own layout ----
	mov	eax, [lwrbe]
	mov	[rbx + LwrState.cur], eax
	mov	rdi, rbx
	call	__lwr_fn_result
	mov	[lwrrt], eax
	mov	r13d, [rbx + LwrState.hidret]
	rassert	r13d ne 0		; an `eventus` result is an aggregate
	mov	rdi, rbx
	mov	esi, [lwrot]
	mov	edx, AST_PRE_V_ADVERSUM
	call	__lwr_try_var
	mov	[lwrvd], eax
	mov	rdi, rbx
	mov	esi, [lwrot]
	mov	edx, [lwrvd]
	xor	ecx, ecx
	call	__lwr_sum_place		; E's type, its bit offset in the OPERAND
	mov	[lwret], eax
	mov	[lwrbo], edx
	mov	rdi, rbx
	mov	esi, [lwret]
	mov	edx, [lwrop]
	mov	ecx, [lwrbo]
	call	__lwr_sum_load
	mov	[lwrv], eax
	; the tag, at the RESULT's width
	mov	rdi, rbx
	mov	esi, [lwrrt]
	call	__lwr_sum_tag
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, [lwrrt]
	mov	edx, [lwrvd]
	call	__lwr_sum_vidx
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, eax
	xor	ecx, ecx
	call	lwr_iconst
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, r13d
	xor	ecx, ecx
	mov	r8d, eax
	xor	r9, r9			; BFA_ORDER_NATIVUS (D6)
	call	lwr_store
	; the element, at the RESULT's place for it
	mov	rdi, rbx
	mov	esi, [lwrrt]
	mov	edx, [lwrvd]
	xor	ecx, ecx
	call	__lwr_sum_place
	mov	rdi, rbx
	mov	esi, eax
	mov	ecx, edx
	mov	edx, r13d
	mov	r8d, [lwrv]
	call	__lwr_sum_store
	mov	rdi, rbx
	xor	rsi, rsi
	call	__lwr_release_to
	mov	rdi, rbx
	xor	esi, esi
	call	lwr_ret

	; ---- bK: the prosperum payload ----
	mov	eax, [lwrbk]
	mov	[rbx + LwrState.cur], eax
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_nty
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_ty_void
	test	eax, eax
	jnz	.unit
	mov	rdi, rbx
	mov	esi, [lwrot]
	mov	edx, AST_PRE_V_PROSPERUM
	call	__lwr_try_var
	mov	rdi, rbx
	mov	esi, [lwrot]
	mov	edx, eax
	xor	ecx, ecx
	call	__lwr_sum_place
	mov	rdi, rbx
	mov	esi, eax
	mov	ecx, edx
	mov	edx, [lwrop]
	call	__lwr_sum_load
	return
  .unit:
	xor	eax, eax
	return
endp
