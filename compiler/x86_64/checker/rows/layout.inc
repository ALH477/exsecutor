; compiler/x86_64/checker/rows/layout.inc
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
; PASS 4 -- LAYOUT. `docs/design/checker.md` section 2.2's fourth deliverable:
; `Ast.layout`, indexed by declaration id (typed-ast.md section 2.8), and the
; two `@transitus` diagnostics.
;
; FIELDS ARE PACKED, ALIGNMENT 1, ALWAYS. Spec §5.2, as amended in 4edb704:
; *"offset = sum of the preceding widths"*. That amendment settled a real
; contradiction rather than picking a convention -- checker.md finding 13
; showed §5.2's two examples could not both be right, because `DeModFrame`'s
; `cursus: u16:maior` sits at byte 15 directly after a `u24` at byte 12 and
; that reading is certified against 246 vectors, while `Capitulum`'s `reserva`
; comment implied natural alignment and is certified against nothing. So
; `EXS-E0322` fires on the BIT-WIDTH rules 2 and 4 and on nothing else: under
; packing there is no gap between whole-byte fields to call implicit padding.
;
; The same packing is applied to NON-`@transitus` structs, and that is a
; reading, not a quotation: §5.2's sentence says "always" but sits in a
; section about wire types, and §8.6 gives no other layout rule anywhere. The
; alternative -- natural alignment in process, packing on the wire -- would be
; a second rule with no spec text behind it and a second set of offsets for
; the lowering to get wrong. Recorded so that the owner can overrule it with
; one sentence rather than by reading this file.
;
; `[UNTESTED]` ON REAL SOURCE, for compute.inc's reason: `Decl.ty` is the
; types pass's and no types pass exists. The fixtures build the two structs
; §5.2 itself uses -- `Capitulum` and `DeModFrame` -- by hand.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.2 (byte order, `@transitus`, the
; four bit-width rules), §5.5 (placement), §9.5 (`--hospes` fixes `mensura`).
; -----------------------------------------------------------------------------

include 'compute.inc'

; ---- the width of a type, in BITS ------------------------------------------
; rax = width in bits, rdx = the type's `AST_ORD_*` byte order.
;
; `mensura` and every pointer take the TARGET's pointer width, which is
; `chk_row_work_init`'s `chkw_pb` -- spec §9.5 fixes `--hospes` first,
; precisely so that "`mensura` is concrete" (typed-ast.md section 2.8) is a
; statement about a compilation and never about the build machine.
;
; A kind with no fixed width -- `textus` `octeti` `scalares` `grapha`
; `eventus` `iface` `dyn` `fn` `param` `brand` `red` -- answers 0 and is
; `[UNIMPLEMENTED]`: spec §5.1 gives the four text types no representation,
; §6.5 leaves refcount representation to §6, and a struct that contains one
; therefore gets a layout that is wrong rather than absent. It cannot appear
; in a `@transitus` type (spec §5.2 admits only the integer widths), which is
; where a wrong offset would be observable, so the gap is recorded rather
; than trapped.
proc __chk_lay_ty, chkl_wk, chkl_ty, chkl_dp
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkl_wk]
	mov	r13, [rbx + ChkRowWork.wtree]
	mov	r12, [chkl_ty]
	test	r12, r12
	jz	.none
	mov	rax, [chkl_dp]
	mov	rcx, CHK_ROW_DEPTH_MAX
	rassert	rax le rcx
	mov	rax, [r13 + Ast.types.len]
	rassert	r12 le rax
	mov	rdi, r13
	mov	rsi, r12
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_INT
	je	.plain
	cmp	ecx, AST_TY_FLOAT
	je	.plain
	cmp	ecx, AST_TY_MENSURA
	je	.word
	cmp	ecx, AST_TY_PTR
	je	.word
	cmp	ecx, AST_TY_REF
	je	.word
	cmp	ecx, AST_TY_REFC
	je	.word
	cmp	ecx, AST_TY_BORROW
	je	.word
	cmp	ecx, AST_TY_STRUCT
	je	.struc
	cmp	ecx, AST_TY_ACIES
	je	.acies
  .none:
	xor	eax, eax
	xor	edx, edx
	return
  .plain:
	movzx	edx, byte [rax + AstType.order]
	mov	eax, [rax + AstType.width]
	return
  .word:
	movzx	edx, byte [rax + AstType.order]
	mov	eax, [rbx + ChkRowWork.wptr]
	return
  .struc:
	mov	r12d, [rax + AstType.a]		; the struct's DECLARATION
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [chkl_dp]
	inc	rdx
	call	__chk_lay_struct
	mov	rdi, r13
	mov	rsi, r12
	call	ast_layout_at
	mov	eax, [rax + AstLayout.off]	; a struct's SIZE, in bytes
	shl	rax, 3
	xor	edx, edx
	return
  .acies:
	mov	r12d, [rax + AstType.width]	; the LANE COUNT (spec §5.4)
	mov	esi, [rax + AstType.a]
	mov	rdi, rbx
	mov	rdx, [chkl_dp]
	inc	rdx
	call	__chk_lay_ty
	imul	rax, r12
	return
endp

; ---- one struct ------------------------------------------------------------
; Idempotent: a struct reached twice (a field naming one declared later, then
; the decl-order loop reaching it) is laid out once. A struct that reaches
; ITSELF is a cycle -- an infinite type, which is a class-C error the types
; pass raises -- and is broken here with a zero width rather than a trap,
; because pass 4 must not be the pass that decides a program is malformed
; when a code exists for it elsewhere.
proc __chk_lay_struct, chkl_wk, chkl_sd, chkl_dp
	uses	rbx, r12, r13, r14
	locals
		slot chkl_bits, dq
		slot chkl_i,    dq
		slot chkl_n,    dq
		slot chkl_tr,   dq
		slot chkl_fd,   dq
		slot chkl_w,    dq
		slot chkl_or,   dq
		slot chkl_sn,   dq
	endl
	mov	rbx, [chkl_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	r12, [chkl_sd]
	mov	rdi, rbx
	add	rdi, ChkRowWork.wlay
	mov	rsi, r12
	call	__chk_row_g1
	test	rax, rax
	jnz	.done				; laid out, or a cycle
	mov	rdi, rbx
	add	rdi, ChkRowWork.wlay
	mov	rsi, r12
	mov	rdx, 1
	call	__chk_row_s1

	mov	rdi, r14
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.flags]
	and	ecx, AST_F_TRANSITUS
	mov	[chkl_tr], rcx
	mov	ecx, [rax + AstDecl.node]
	mov	[chkl_sn], rcx
	mov	qword [chkl_bits], 0
	mov	qword [chkl_i], 0
	mov	qword [chkl_n], 0
	test	rcx, rcx
	jz	.finish
	mov	rdi, r14
	mov	rsi, rcx
	call	ast_node_at
	mov	ecx, [rax + AstNode.a]
	mov	[chkl_i], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkl_n], rcx

  .field:
	cmp	qword [chkl_n], 0
	jle	.finish
	mov	rdi, r14
	mov	rsi, [chkl_i]
	call	ast_extra_at
	mov	rdi, r14
	mov	rsi, rax
	call	ast_node_at
	mov	r13, rax			; the `Field` NODE pointer
	mov	ecx, [r13 + AstNode.d]
	mov	[chkl_fd], rcx			; its declaration
	test	rcx, rcx
	jz	.field_next
	mov	rdi, r14
	mov	rsi, rcx
	call	ast_decl_at
	mov	esi, [rax + AstDecl.ty]
	mov	rdi, rbx
	mov	rdx, [chkl_dp]
	inc	rdx
	call	__chk_lay_ty
	mov	[chkl_w], rax
	mov	[chkl_or], rdx

	; ---- spec §5.2's rules 2 and 3, `@transitus` only ----
	cmp	qword [chkl_tr], 0
	je	.record
	mov	rax, [chkl_w]
	cmp	rax, 8
	jle	.record				; §5.2 rule 3: byte order is
						; not part of the grammar at or
						; below eight bits
	cmp	qword [chkl_or], AST_ORD_NATIVUS
	jne	.rule2
	mov	rdi, rbx
	mov	rsi, 321
	mov	rdx, [chkl_i]
	call	__chk_lay_atfield
  .rule2:
	mov	rax, [chkl_bits]
	and	rax, 7
	jnz	.rule2_bad
	mov	rax, [chkl_w]
	and	rax, 7
	jz	.record
  .rule2_bad:
	mov	rdi, rbx
	mov	rsi, 322
	mov	rdx, [chkl_i]
	call	__chk_lay_atfield

  .record:
	mov	rdi, r14
	mov	rsi, [chkl_fd]
	call	ast_layout_at
	mov	rcx, [chkl_bits]
	shr	rcx, 3
	mov	[rax + AstLayout.off], ecx
	mov	rcx, [chkl_bits]
	and	rcx, 7
	mov	[rax + AstLayout.bitoff], cl
	mov	rcx, [chkl_w]
	cmp	rcx, 255
	jle	.wfits
	xor	rcx, rcx			; an aggregate field: the width
						; is in the TYPE, and typed-ast.md
						; section 2.8's `width u8` cannot
						; hold it (finding, this file's
						; report)
  .wfits:
	mov	[rax + AstLayout.width], cl
	mov	rcx, [chkl_or]
	mov	[rax + AstLayout.order], cl
	mov	byte [rax + AstLayout.algn], 1
	mov	rcx, [chkl_w]
	add	[chkl_bits], rcx
  .field_next:
	inc	qword [chkl_i]
	dec	qword [chkl_n]
	jmp	.field

  .finish:
	; ---- spec §5.2 rule 4: the declared widths must sum to whole bytes --
	cmp	qword [chkl_tr], 0
	je	.size
	mov	rax, [chkl_bits]
	and	rax, 7
	jz	.size
	cmp	qword [chkl_sn], 0
	je	.size
	mov	rdi, rbx
	mov	rsi, CHK_P_LAYOUT
	mov	rdx, 322
	mov	rcx, [chkl_sn]
	call	__chk_row_at
  .size:
	mov	rdi, r14
	mov	rsi, r12
	call	ast_layout_at
	mov	rcx, [chkl_bits]
	add	rcx, 7
	shr	rcx, 3
	mov	[rax + AstLayout.off], ecx	; a struct's SIZE, in bytes
	mov	byte [rax + AstLayout.bitoff], 0
	mov	byte [rax + AstLayout.width], 0
	mov	byte [rax + AstLayout.order], AST_ORD_NATIVUS
	mov	byte [rax + AstLayout.algn], 1	; PACKED (spec §5.2, 4edb704)
	mov	rdi, rbx
	add	rdi, ChkRowWork.wlay
	mov	rsi, r12
	mov	rdx, 2
	call	__chk_row_s1
  .done:
	return
endp

; Raise `chkl_c2` against the `Field` node at `Ast.extra` index `chkl_ix`.
; The span is the FIELD, which is what checker.md section 2.4's table asks
; for and what makes the diagnostic point at the annotation the programmer
; must change.
proc __chk_lay_atfield, chkl_wk, chkl_c2, chkl_ix
	uses	rbx
	locals
	endl
	mov	rbx, [chkl_wk]
	mov	rdi, [rbx + ChkRowWork.wtree]
	mov	rsi, [chkl_ix]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, CHK_P_LAYOUT
	mov	rdx, [chkl_c2]
	mov	rcx, rax
	call	__chk_row_at
	return
endp

; ---- pass 4 ----------------------------------------------------------------
;     chk_layout(chk_c, ptrbits) -> rax = the number of diagnostics THIS pass
;                                        buffered
;
; `ptrbits` is the TARGET's pointer width, from `--hospes`. It is an argument
; rather than a field of `ChkCtx` because nothing in this tree turns a target
; triple into a width yet -- `DrvCtx.hospes` is the string -- and spec §9.5
; forbids defaulting to the build platform, so the one place that knows must
; say. Requested as a `ChkCtx` field; `[OPEN]` until the driver can answer.
;
; Structs are laid out in DECLARATION order, and a field naming a struct
; declared later pulls that one forward -- so the ORDER of the work is not
; declaration order, but the RESULT is, because each struct is laid out
; exactly once and its answer does not depend on when.
proc chk_layout, chkw_c2, chkw_b2
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	r13, [chkw_c2]
	lea	rax, [r13 + ChkCtx.dbuf]
	mov	r14, [rax + Vec.len]

	mov	rdi, [r13 + ChkCtx.arena]
	mov	rsi, sizeof.ChkRowWork
	call	arena_alloc
	mov	rbx, rax
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, [chkw_b2]
	call	chk_row_work_init

	mov	r13, [rbx + ChkRowWork.wtree]
	mov	eax, [r13 + Ast.sides]
	test	eax, eax
	jnz	.have_sides
	mov	rdi, r13
	call	ast_side_alloc
  .have_sides:
	mov	r12, 1
  .decl:
	mov	rdi, r13
	call	ast_decl_count
	cmp	r12, rax
	jg	.done
	mov	rdi, r13
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_STRUCT
	jne	.next
	mov	rdi, rbx
	mov	rsi, r12
	xor	rdx, rdx
	call	__chk_lay_struct
  .next:
	inc	r12
	jmp	.decl
  .done:
	mov	r13, [rbx + ChkRowWork.wctx]
	or	dword [r13 + ChkCtx.stages], CHK_S_LAYOUT
	lea	rax, [r13 + ChkCtx.dbuf]
	mov	rax, [rax + Vec.len]
	sub	rax, r14
	return
endp
