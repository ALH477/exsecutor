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
; ON REAL SOURCE: `tests/unit/chk_row_layout_src.asm` runs every field kind
; this file can be handed, in and out of `@transitus`, through the whole
; front end, and reads the layout back. It exists because the fixtures that
; build `Decl.ty` by hand (`chk_row_layout.asm`, `chk_row_layout_kind.asm`,
; which lay out the two structs §5.2 itself uses -- `Capitulum` and
; `DeModFrame` -- and each field kind once) never saw the type pass 2
; actually builds for `Scriptor`, and `structura T { x: Scriptor }` trapped
; here (`.prestruc`, below).
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
; `eventus` `iface` `dyn` `fn` `param` `brand` `red` -- answers width 0 (and
; its real byte order, for the check below) and is `[UNIMPLEMENTED]`: spec
; §5.1 gives the four text types no representation, §6.5 leaves refcount
; representation to §6, and a NON-`@transitus` struct that contains one
; therefore gets a layout that is wrong rather than absent. In a
; `@transitus` type it is rejected outright -- spec §5.2, "The one aggregate
; cast": "every field of a `@transitus` type is an unsigned integer...
; nothing else" -- so `__chk_lay_struct` below never lets a width-0 field's
; wrong offset become observable: unannotated, it is `EXS-E0321` ("an
; unannotated non-integer field is `:nativus`... already `EXS-E0321`");
; explicitly ordered, `EXS-E0309` ("an annotation not applicable to the
; type"). A width-0 field used to be silently treated as an admitted
; sub-8-bit integer (rule 3's exemption applied to width <= 8 without
; excluding width == 0) and passed unrejected; that was the bug, not the
; rule -- this file's `EXS-E0322` and `EXS-E0321` fixture predates the fix
; and only ever exercised whole-byte-or-wider `uN` fields. The first fix
; excluded width 0 only, which still admitted a NON-integer of width 1-8
; (`acies<u8, 1>`, a nested one-`u8` struct); the exemption is now decided
; by KIND, `AST_TY_INT` alone.
;
; `Scriptor` is the one struct with no `AstDecl`, and it is SIZED, not
; width 0: `.prestruc` below.
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
	; a kind `__chk_lay_ty` cannot size -- `textus` `octeti` `scalares`
	; `grapha` `eventus` `iface` `dyn` `fn` `param` `brand` `red`, and
	; anything else outside this switch -- has no fixed WIDTH, but its
	; declared byte ORDER is real: the `@transitus` field check below
	; (`__chk_lay_struct`) must be able to tell `t: textus` (order
	; `:nativus` by default, `EXS-E0321`) from `t: textus:maior` (order
	; explicit, `EXS-E0309` -- an annotation not applicable to a
	; non-integer type) apart, and could not while this path discarded
	; the order along with the width.
	movzx	edx, byte [rax + AstType.order]
	xor	eax, eax
	return
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
	mov	r12d, [rax + AstType.a]		; the struct's DECLARATION -- or,
						; bit 31 set, a PRELUDE reference
	bt	r12, 31
	jc	.prestruc
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
  .prestruc:
	; `Scriptor`, the one prelude struct. It has NO `AstDecl`
	; (checker/types/prim.inc's `chk_ty_prelude_type` interns it as an
	; `AST_TY_STRUCT` whose `a` is `CHK_TY_PRELUDE or k`), so the `.struc`
	; arm above cannot lay it out: it used to hand the TAGGED value to
	; `__chk_lay_struct` as a declaration id, and `__chk_row_g1`'s bounds
	; `rassert` trapped on `0x80000001` -- `exsc aedifica` on
	; `structura T { x: Scriptor }`, with or without `@transitus`, died
	; with SIGILL. Its size is `prelude/interface.inc`'s, the same constant
	; `lower/ty.inc`'s `.prestruct` reads for a `slot` and the prelude blob
	; is assembled from, so the layout and the lowering cannot disagree.
	; Order `:nativus`, as for every struct: a struct cannot carry one.
	and	r12d, CHK_TY_PRE_MASK
	rassert	r12 eq CHK_TY_PRE_SCRIPTOR
	mov	eax, EXS_IFACE_SCRIPTOR_SIZE * 8
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
		slot chkl_ft,   dq
		slot chkl_kd,   dq
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
	mov	[chkl_ft], rsi			; the field's TYPE id, for the
						; kind check below -- `chkl_or`
						; already carries its order, but
						; `__chk_lay_ty` never returns
						; kind
	mov	rdi, rbx
	mov	rdx, [chkl_dp]
	inc	rdx
	call	__chk_lay_ty
	mov	[chkl_w], rax
	mov	[chkl_or], rdx

	; ---- spec §5.2 rules 2 and 3, and "every field of a `@transitus`
	; type is an unsigned integer... nothing else" (§5.2, "The one
	; aggregate cast"), `@transitus` only ----
	cmp	qword [chkl_tr], 0
	je	.record
	mov	rdi, r14
	mov	rsi, [chkl_ft]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	mov	[chkl_kd], rcx
	cmp	ecx, AST_TY_INT
	jne	.ckorder			; not an integer: never exempt
						; from the order check below --
						; rule 3's "at or below eight
						; bits" describes a real
						; sub-byte or whole-byte
						; INTEGER. This test used to be
						; `width == 0`, which caught
						; `textus` and the atoms but let
						; every non-integer of width 1-8
						; through clean: `acies<u8, 1>`
						; and a nested one-`u8` struct in
						; a `@transitus` struct both
						; checked with no diagnostic,
						; against §5.2's "no `acies`, and
						; no nested struct"
						; (`tests/unit/chk_row_layout_src.asm`)
	mov	rax, [chkl_w]
	cmp	rax, 8
	jle	.record				; §5.2 rule 3: byte order is
						; not part of the grammar at or
						; below eight bits
  .ckorder:
	cmp	qword [chkl_or], AST_ORD_NATIVUS
	jne	.annotated
	; unannotated: rule 3's ":nativus by default" is `EXS-E0321`,
	; whether the field is `u16` (multi-byte, this check's original
	; case) or `textus` / a nested struct / an `acies` (width 0, or
	; order forced to `AST_ORD_NATIVUS` by `__chk_lay_ty`'s `.struc` --
	; a struct-typed field can never itself carry an order). §5.2, "The
	; one aggregate cast": "an unannotated non-integer field is
	; `:nativus` by rule 3 and is `EXS-E0321`."
	mov	rdi, rbx
	mov	rsi, 321
	mov	rdx, [chkl_i]
	call	__chk_lay_atfield
	jmp	.rule2
  .annotated:
	; explicitly ordered. §5.2, "The one aggregate cast": an order
	; annotation applies only to an `AST_TY_INT` field -- `textus:maior`
	; is `EXS-E0309`, "an annotation not applicable to the type."
	; `checker/types/sig.inc`'s `__chk_ty_suffix` already raises this
	; same code for the same reason at every type position in real
	; source (Pass 2, before `Decl.ty` exists); this pass's fixtures
	; build `Decl.ty` by hand and never run Pass 2 (this file's own
	; header), so this is not a second diagnostic for one source
	; annotation -- it is this pass's own check of input Pass 2 never
	; saw. A SIGNED `iN:maior` is still `AST_TY_INT` and is NOT rejected
	; here: the enforcement text ("an order annotation on a type that is
	; not an integer... EXS-E0309") distinguishes only integer from
	; non-integer and never revisits signedness against "every field...
	; is an unsigned integer," so whether a signed, explicitly-ordered
	; field belongs in a `@transitus` struct is `[OPEN]` -- this pass
	; does not invent a code for it.
	cmp	qword [chkl_kd], AST_TY_INT
	je	.rule2
	mov	rdi, rbx
	mov	rsi, 309
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
