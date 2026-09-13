; compiler/x86_64/checker/types/sig.inc
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
; PASS 2, PART 2 -- TYPE EXPRESSIONS AND THE SIGNATURE LOOP.
; `docs/design/checker.md` section 2.2's pass 2, first half:
;
;	"Pass 2 interns declared types FIRST, in decl order, so bodies may call
;	 functions declared later ... Nominal kinds need only the decl id, so a
;	 field may name a struct declared below it. Then one walk types bodies."
;
; FIVE PHASES, and the split is forced by what each one may read:
;
;   A  STRUCT / IFACE / GENPARAM get their NOMINAL id. Depends on nothing but
;      the declaration's own index, so it runs first and every later phase may
;      name any of them regardless of source order.
;   B  TYPUS. A transparent alias (checker.md section 2.2: "its `Decl.ty` is
;      `T`'s id; the `alias` kind stays unused -- finding 8"), so it needs its
;      right-hand side, which may name another alias. Resolved LAZILY with a
;      three-state byte per declaration, so a cycle is caught as a cycle
;      (`EXS-E0303`, class C's "alias cycle") instead of recursing to the stack
;      guard.
;   C  FIELD / PARAM. One annotation each, spec §8.6 requires both.
;   D  FN / EXTERNFN / MEMBER. The `fn` type, with its row -- which is where
;      `chk_row_of_node` is called and where `sicut f` finally becomes an
;      ordinal.
;   E  IMPL. The impl map of checker.md section 2.7, keyed (interface decl,
;      target type id) in declaration order, second impl `EXS-E0302`.
;
; LAMBDAS ARE NOT IN PHASE D. Spec §8.6 decision 2 requires a lambda's
; parameter types and INFERS its result from its first `redde`, so its `fn`
; type does not exist until its body is typed; types.inc builds it at the
; `Lambda` node and writes `Decl.ty` there.
;
; ROWS: `chk_row_of_node`, NEVER A HAND-BUILT ITEM LIST, and a bare
; declaration gets `chk_row_empty`, never 0 (rows/rowdefs.inc's "THE EMPTY ROW
; IS A REAL ROW WITH A NON-ZERO ID"). An INFERRED function's row is left EMPTY
; here on purpose: pass 3 computes the real one and RE-INTERNS the `fn` type
; (`__chk_row_commit`, that file's departure D1), which is why nothing here
; tries to leave a hole for it.
;
; WHO RESOLVES `sicut IDENT` -> ORDINAL. This file, and that is a finding
; against checker.md section 2.2, whose table assigns `RowItem.d` to pass 1.
; Pass 1 declined it in as many words ("the ordinal needs the enclosing `Sig`'s
; parameter list matched by name ... the rows agent's"), and pass 3 does not do
; it either -- `chk_row_of_node` READS the slot and `compute.inc` never writes
; it. Pass 2 is the only pass that holds both the `Row` node and the `Sig` it
; hangs off, so it does it: a name that is not a parameter of that signature is
; `EXS-E0423` (checker.md class L), and a `sicut` item in a BRACED `TypeRow` is
; the same code for finding 5's reason -- a function type has no parameter
; names for it to refer to.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1, §4.2, §4.3, §4.4, §4.7, §5.1-§5.5,
; §6.4, §7.1, §8.6, §13.
; -----------------------------------------------------------------------------

include 'prim.inc'

; =============================================================================
; type expressions
; =============================================================================

; chk_ty_of_node(w, n) -> rax = the interned type of the `Ty*` subtree at `n`,
; annotating every node in it. `n` = 0 answers `unit`, which is what an omitted
; result type means (spec §8.6: `['->' Type]`).
;
; ONE ENTRY POINT for every type position in the language, so that byte order,
; placement, rows and the primitive table are applied in exactly one place.
proc chk_ty_of_node, chkt_wq, chkt_nq
	uses	rbx, r12, r13, r14
	locals
		slot chkt_kq, dq
		slot chkt_aq, dq
		slot chkt_bq, dq
		slot chkt_cq, dq
		slot chkt_dq, dq
		slot chkt_xq, dq
		slot chkt_iq, dq
		slot chkt_iq2, dq	; a `fn` parameter's interned type
		slot chkt_pnq, dq	; that parameter's type NODE, for a span
		slot chkt_tq, 16
	endl
	mov	rbx, [chkt_wq]
	mov	r12, [chkt_nq]
	test	r12, r12
	jnz	.have
	mov	rdi, rbx
	call	__chk_ty_unit
	return
  .have:
	inc	dword [rbx + ChkTyWork.wdep]
	mov	eax, [rbx + ChkTyWork.wdep]
	mov	rcx, CHK_TY_DEPTH_MAX
	rassert	rax le rcx		; spec §8.6 admits any number of
					; suffixes; a tree deeper than this is
					; a builder bug, not a program
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.kind]
	mov	[chkt_kq], rcx
	movzx	ecx, word [rax + AstNode.aux]
	mov	[chkt_xq], rcx
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_aq], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bq], rcx
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_cq], rcx
	mov	ecx, [rax + AstNode.d]
	mov	[chkt_dq], rcx

	mov	rcx, [chkt_kq]
	cmp	rcx, AST_TYBIT
	je	.bits
	cmp	rcx, AST_TYPATH
	je	.path
	cmp	rcx, AST_TYPTR
	je	.ptr
	cmp	rcx, AST_TYFN
	je	.fn
	cmp	rcx, AST_TYDYN
	je	.dyn
	cmp	rcx, AST_TYSUFFIX
	je	.sfx
	; An `Error` node, or anything the parser put in a type position that is
	; not a type. AST H3: the parser has already spoken; a second diagnostic
	; here would duplicate it.
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.done

  .bits:
	; `uN` -- spec §8.6 decided the width at the parser and `u12` never got
	; here (`EXS-E0201`). `u1` is the boolean (spec §5.1).
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, AST_SIGN_U
	mov	rdx, [chkt_xq]
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	jmp	.done

  .ptr:
	; `&T` (AST_PTR_REF) and `*T` (AST_PTR_RAW). typed-ast.md section 2.5
	; makes `&T` a `borrow` and `*T` a `ptr`; spec §8.4 makes `*T` require
	; `Crudum`, which is pass 3's draw, not a type-level fact.
	mov	rdi, rbx
	mov	rsi, [chkt_aq]
	call	chk_ty_of_node
	mov	r13, rax
	cmp	qword [chkt_xq], AST_PTR_RAW
	je	.ptr_raw
	; `&T` and `&mutabilis T` are both `borrow`, distinguished by the sign
	; byte -- 1 is mutable (ADR 0016 decision 1; ast/node.inc's `sign`
	; comment says why that byte rather than a second kind). They intern as
	; distinct type ids, which is what makes `&mutabilis T` a different type
	; from `&T` rather than an annotation on one.
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	xor	rdx, rdx
	cmp	qword [chkt_xq], AST_PTR_REFMUT
	jne	.ptr_borrow
	mov	rdx, 1
  .ptr_borrow:
	call	ast_type_borrow
	jmp	.done
  .ptr_raw:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, AST_TY_PTR
	mov	rdx, r13
	xor	rcx, rcx
	call	ast_type_una
	jmp	.done

  .fn:
	; `functio(A, B) -> C [poscit {R}]`. `TyFn.a`/`.b` are the parameter
	; list, `.c` the result, `.d` the braced row (0 = the EMPTY row).
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_list_mark
	mov	[chkt_iq], rax
	xor	r13, r13
  .fn_p:
	cmp	r13, [chkt_bq]
	jae	.fn_res
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_aq]
	add	rsi, r13
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	mov	[chkt_pnq], rax		; the parameter's TYPE NODE, for the span
	call	chk_ty_of_node
	; A MUTABLE BORROW MAY NOT APPEAR IN A FUNCTION TYPE (ADR 0016 decision
	; 6). `AstType` for an `AST_TY_FN` records parameters as a run of type
	; ids with no room for a per-parameter bit, so the permission could not
	; be carried here even if it were wanted -- and `__chk_ty_call` reads
	; parameter types off the `fn` type, so admitting one would make the
	; call-site coercion reachable through a higher-order call, where the
	; callee is not statically known. `EXS-E0309`: an annotation not
	; applicable in this position. `[OPEN]`
	mov	[chkt_iq2], rax
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_ismutborrow
	test	eax, eax
	jz	.fn_ok
	mov	rdi, rbx
	mov	rsi, 309
	mov	rdx, [chkt_pnq]
	call	__chk_ty_raise
  .fn_ok:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_iq2]
	call	ast_list_push
	inc	r13
	jmp	.fn_p
  .fn_res:
	mov	rdi, rbx
	mov	rsi, [chkt_cq]
	call	chk_ty_of_node
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_iq]
	call	ast_list_emit
	mov	r13, rax		; the `extra` offset
	mov	rdi, rbx
	mov	rsi, [chkt_dq]
	xor	rdx, rdx
	xor	rcx, rcx
	call	chk_ty_rowid		; a braced row: no parameter names
	mov	r14, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	mov	rdx, [chkt_bq]
	mov	rcx, r14
	call	ast_type_fn
	jmp	.done

  .dyn:
	; `dyn Path [GenericArgs] [poscit {R}]` (spec §4.4). `a` = the `Path`,
	; whose `d` pass 1 resolved; `b` = the bound row `EXS-E0510` is checked
	; against (pass 3's).
	mov	rdi, rbx
	mov	rsi, [chkt_bq]
	xor	rdx, rdx
	xor	rcx, rcx
	call	chk_ty_rowid
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, [chkt_aq]
	call	__chk_ty_ifacedecl
	mov	r13, rax
	test	r13, r13
	jnz	.dyn_ok
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.done
  .dyn_ok:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	mov	rdx, r14
	call	ast_type_dyn
	jmp	.done

  .sfx:
	; `T : IDENT` (byte order, spec §5.2) or `T apud IDENT` (placement,
	; spec §5.5). Both are FIELDS OF THE INTERNED RECORD, so the operand's
	; type is re-interned with one byte changed -- which is what makes
	; `u32:maior` and `u32` different ids with no rule anywhere else
	; (ast/types.inc's header).
	mov	rdi, rbx
	mov	rsi, [chkt_aq]
	call	chk_ty_of_node
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__chk_ty_suffix
	jmp	.done

  .path:
	mov	rdi, rbx
	mov	rsi, [chkt_aq]
	call	__chk_ty_path_type
	; `EXS-E0520` -- spec §5.3, §6.4: a non-atomic `refero` may not cross an
	; `externus` boundary. The span is the type node (checker.md section
	; 2.4's table), and the test is on the INTERNED KIND, so it catches
	; `refero<T>` however it was spelled, including through a `typus` alias.
	mov	r13, rax
	cmp	dword [rbx + ChkTyWork.wext], 0
	je	.done_r13
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_kindof
	cmp	rax, AST_TY_REF
	jne	.done_r13
	mov	rdi, rbx
	mov	rsi, 520
	mov	rdx, r12
	call	__chk_ty_raise
  .done_r13:
	mov	rax, r13

  .done:
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__chk_ty_setty
	dec	dword [rbx + ChkTyWork.wdep]
	mov	rax, r13
	return
endp

; __chk_ty_suffix(w, n, t) -> rax = type `t` with `TySuffix` node `n`'s byte
; order or placement applied.
;
; `EXS-E0309` -- checker.md class I, "repeated or contradictory `TySuffix`;
; byte order on a non-integer" -- is raised here for the three cases that are
; decidable from the type alone: an order suffix on something that is not an
; `int`, a SECOND order suffix (`u32:maior:minor`), and an unknown spelling.
; `apud` on a non-`acies` is the fourth and is checked the same way.
proc __chk_ty_suffix, chkt_wv, chkt_nv, chkt_tv
	uses	rbx, r12, r13
	locals
		slot chkt_tv2, 16
		slot chkt_nmv, dq
		slot chkt_xv,  dq
	endl
	mov	rbx, [chkt_wv]
	mov	r12, [chkt_nv]
	mov	r13, [chkt_tv]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.aux]
	mov	[chkt_xv], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_nmv], rcx

	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_type_at
	mov	rcx, [rax]
	mov	[chkt_tv2], rcx
	mov	rcx, [rax + 8]
	mov	[chkt_tv2 + 8], rcx

	cmp	qword [chkt_xv], AST_SFX_APUD
	je	.apud

	; ---- byte order (spec §5.2) ----
	movzx	eax, byte [chkt_tv2 + AstType.kind]
	cmp	eax, AST_TY_INT
	jne	.bad
	movzx	eax, byte [chkt_tv2 + AstType.order]
	cmp	eax, AST_ORD_NATIVUS
	jne	.bad			; a second, contradictory suffix
	mov	rdi, rbx
	mov	rsi, [chkt_nmv]
	lea	rdx, [chk_ty_order_names]
	mov	rcx, CHK_TY_ORDER_NAMES_LEN
	call	__chk_ty_pool
	test	rax, rax
	jz	.bad
	; `__chk_ty_pool` answers a 1-BASED index (0 is the miss) and
	; `AST_ORD_*` is 0-based, so the enumerator is the index minus one.
	; This stored the index itself until wire-codec.md's M6 lowered the
	; first ordered field from source and printed `load u16 %p 2 minor`
	; for a `u16:maior` -- every order one too high, `:nativus` stored as
	; `maior` (so `EXS-E0321` could not see an explicit one) and `:minor`
	; as 3, which is no order at all. tests/unit/lwr_transitus.asm pins
	; `maior`, `minor` and `nativus` from source.
	dec	rax
	mov	[chkt_tv2 + AstType.order], al
	jmp	.intern

  .apud:
	; ---- placement (spec §5.5) ----
	movzx	eax, byte [chkt_tv2 + AstType.kind]
	cmp	eax, AST_TY_ACIES
	jne	.bad
	movzx	eax, byte [chkt_tv2 + AstType.place]
	cmp	eax, AST_PLACE_HOST
	jne	.bad
	mov	rdi, rbx
	mov	rsi, [chkt_nmv]
	lea	rdx, [chk_ty_place_names]
	mov	rcx, CHK_TY_PLACE_NAMES_LEN
	call	__chk_ty_pool
	cmp	rax, 1
	jne	.bad
	mov	byte [chkt_tv2 + AstType.place], AST_PLACE_MACHINA

  .intern:
	mov	rdi, [rbx + ChkTyWork.wtre]
	lea	rsi, [chkt_tv2]
	call	ast_type_intern
	return

  .bad:
	mov	rdi, rbx
	mov	rsi, 309
	mov	rdx, r12
	call	__chk_ty_raise
	mov	rax, r13		; the operand's type: no cascade
	return
endp

; Spec §5.2's three byte orders, in `AST_ORD_*` order so that the pool index
; MINUS ONE is the enumerator (the pool is 1-based -- `__chk_ty_suffix` says
; what it cost to read that as "is"); and spec §5.5's one placement. Contextual identifiers, not
; keywords (spec §8.6: "`maior` `minor` `nativus` are contextual").
chk_ty_order_names:
	db 7,'nativus'
	db 5,'maior'
	db 5,'minor'
CHK_TY_ORDER_NAMES_LEN = $ - chk_ty_order_names
assert AST_ORD_NATIVUS = 0
assert AST_ORD_MAIOR = 1
assert AST_ORD_MINOR = 2

chk_ty_place_names:
	db 7,'machina'
CHK_TY_PLACE_NAMES_LEN = $ - chk_ty_place_names

; __chk_ty_ifacedecl(w, pathnode) -> rax = the interface DECLARATION a `dyn`
; or an implementation head names, or 0 (already diagnosed).
proc __chk_ty_ifacedecl, chkt_wi, chkt_ni
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wi]
	mov	r12, [chkt_ni]
	test	r12, r12
	jz	.no
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_seg0
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_node
	mov	r12d, [rax + AstNode.d]
	test	r12, r12
	jz	.unres
	mov	rdi, r12
	call	__chk_ty_is_pre
	test	eax, eax
	jnz	.notiface
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_IFACE
	jne	.notiface
	mov	rax, r12
	return
  .unres:
	mov	rdi, rbx
	mov	rsi, 301
	mov	rdx, r13
	call	__chk_ty_raise
	xor	eax, eax
	return
  .notiface:
	; checker.md class N: "impl of a non-interface", `dyn` over one.
	mov	rdi, rbx
	mov	rsi, 511
	mov	rdx, r13
	call	__chk_ty_raise
	xor	eax, eax
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_seg0(w, pathnode) -> rax = the first `Seg` node, rdx = the segment
; count. Mirrors resolve.inc's own `__chk_seg0`; duplicated rather than reached
; across, because that routine is `__`-internal to another agent's file.
proc __chk_ty_seg0, chkt_wg, chkt_ng
	uses	rbx, r12
	locals
		slot chkt_og, dq
	endl
	mov	rbx, [chkt_wg]
	mov	rdi, rbx
	mov	rsi, [chkt_ng]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_og], rcx
	mov	r12d, [rax + AstNode.b]
	rassert	r12 ge 1
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_og]
	call	ast_extra_at
	mov	rdx, r12
	return
endp

; __chk_ty_path_type(w, pathnode) -> rax = the type a `TyPath`'s `Path` names.
;
; ORDER: the DECLARATION first, the primitive table second, the prelude third.
; A module's own declaration therefore shadows both, which is spec §12's rule
; for the prelude and the only reading of finding 14 that lets a program
; declare `structura Mensura` without the primitive table stealing the name.
proc __chk_ty_path_type, chkt_wh, chkt_nh
	uses	rbx, r12, r13, r14
	locals
		slot chkt_ch, dq
		slot chkt_mh, dq
	endl
	mov	rbx, [chkt_wh]
	mov	r12, [chkt_nh]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_seg0
	mov	r13, rax		; the first `Seg`
	mov	[chkt_ch], rdx

	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_node
	mov	r14d, [rax + AstNode.d]
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_mh], rcx

	cmp	qword [chkt_ch], 1
	je	.one
	; `A.B` in type position needs `A`'s meaning before `B` can be looked
	; up in it, and nothing in this language yet gives a name a namespace:
	; spec §8.6 decision 5 has no imports and §8.6 has no module type. So a
	; multi-segment type path names nothing. `[OPEN]`.
	mov	rdi, rbx
	mov	rsi, 301
	mov	rdx, r13
	call	__chk_ty_raise
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.out

  .one:
	test	r14, r14
	jz	.undecl
	mov	rdi, r14
	call	__chk_ty_is_pre
	test	eax, eax
	jnz	.pre
	mov	rdi, rbx
	mov	rsi, r14
	call	__chk_ty_decl_type
	jmp	.out
  .pre:
	mov	rdi, rbx
	mov	rsi, r14
	and	rsi, CHK_TY_PRE_MASK
	call	chk_ty_prelude_type
	jmp	.out

  .undecl:
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, [chkt_mh]
	call	__chk_ty_prim
	test	rax, rax
	jnz	.out
	; the prelude, last (spec §12)
	mov	rdi, rbx
	mov	rsi, [chkt_mh]
	lea	rdx, [chk_ty_pre_names]
	mov	rcx, CHK_TY_PRE_NAMES_LEN
	call	__chk_ty_pool
	mov	r14, rax		; the row, before the type test
	mov	rdi, rax
	call	__chk_ty_pre_is_type	; `Scriptor` or `Lector` (prim.inc): a
					; MEMBER row is not a type name
	test	eax, eax
	jz	.unknown
	mov	rdi, rbx
	mov	rsi, r13
	mov	edx, r14d
	or	edx, CHK_TY_PRELUDE
	call	__chk_ty_setd
	mov	rdi, rbx
	mov	rsi, r12
	mov	edx, r14d
	or	edx, CHK_TY_PRELUDE
	call	__chk_ty_setd
	mov	rdi, rbx
	mov	rsi, r14
	call	chk_ty_prelude_type
	jmp	.out

  .unknown:
	; A name the INTERNER does not have is not a name this pass can judge:
	; the third and last case of `__chk_ty_bytes`'s bargain (prim.inc), and
	; like the other two it is reachable only from a hand-written `.ast`
	; dump. Silent, with the error type, so a fixture built that way is not
	; told that `i32` does not resolve.
	mov	rdi, rbx
	mov	rsi, [chkt_mh]
	call	__chk_ty_bytes
	test	rdx, rdx
	jz	.quiet_unknown
	; checker.md finding 14: pass 1 left this silent BECAUSE it could not
	; tell `i32` from a typo. Pass 2 can, and this is the one place in the
	; compiler that says so.
	mov	rdi, rbx
	mov	rsi, 301
	mov	rdx, r13
	call	__chk_ty_raise
  .quiet_unknown:
	mov	rdi, rbx
	call	__chk_ty_err

  .out:
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r14
	call	__chk_ty_setty
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__chk_ty_setty
	mov	rax, r14
	return
endp

; __chk_ty_prim(w, segnode, nameid) -> rax = the primitive type that spelling
; names, or 0. Generic arguments come off `Seg.b` (typed-ast.md section 2.3).
proc __chk_ty_prim, chkt_wj, chkt_sj, chkt_mj
	uses	rbx, r12, r13, r14
	locals
		slot chkt_gj, dq
		slot chkt_pj, dq
	endl
	mov	rbx, [chkt_wj]
	mov	r12, [chkt_sj]

	; `iN` first: it is a shape, not a pool entry
	mov	rdi, rbx
	mov	rsi, [chkt_mj]
	call	__chk_ty_signed
	test	rax, rax
	jz	.pool
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, AST_SIGN_I
	mov	rdx, rax
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	return

  .pool:
	mov	rdi, rbx
	mov	rsi, [chkt_mj]
	lea	rdx, [chk_ty_prim_names]
	mov	rcx, CHK_TY_PRIM_NAMES_LEN
	call	__chk_ty_pool
	mov	[chkt_pj], rax
	test	rax, rax
	jz	.no

	; the generic arguments, if any
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_gj], rcx

	mov	rax, [chkt_pj]
	cmp	rax, CHK_TY_P_MENSURA
	je	.mensura
	cmp	rax, CHK_TY_P_F32
	je	.f32
	cmp	rax, CHK_TY_P_F64
	je	.f64
	cmp	rax, CHK_TY_P_REFERO
	je	.refero
	cmp	rax, CHK_TY_P_REFC
	je	.refc
	cmp	rax, CHK_TY_P_EVENTUS
	je	.eventus
	cmp	rax, CHK_TY_P_ACIES
	je	.acies

	; the five field-less kinds; the pool order is not `AST_TY_*` order, so
	; the mapping is written out rather than computed from an offset
	mov	rsi, AST_TY_TEXTUS
	cmp	rax, CHK_TY_P_TEXTUS
	je	.simple
	mov	rsi, AST_TY_OCTETI
	cmp	rax, CHK_TY_P_OCTETI
	je	.simple
	mov	rsi, AST_TY_SCALARES
	cmp	rax, CHK_TY_P_SCALARES
	je	.simple
	mov	rsi, AST_TY_GRAPHA
	cmp	rax, CHK_TY_P_GRAPHA
	je	.simple
	mov	rsi, AST_TY_UNIT
	cmp	rax, CHK_TY_P_UNIT
	je	.simple
	jmp	.no
  .simple:
	mov	rdi, rbx
	call	__chk_ty_simple
	return

  .mensura:
	mov	rdi, rbx
	call	__chk_ty_mensura
	return
  .f32:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, 32
	call	ast_type_float
	return
  .f64:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, 64
	call	ast_type_float
	return

  .refero:
	mov	r14, AST_TY_REF
	jmp	.una
  .refc:
	mov	r14, AST_TY_REFC
	jmp	.una
  .eventus:
	mov	r14, AST_TY_EVENTUS
  .una:
	mov	rdi, rbx
	mov	rsi, [chkt_gj]
	xor	rdx, rdx
	call	__chk_ty_genarg
	mov	r13, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r14
	mov	rdx, r13
	xor	rcx, rcx
	call	ast_type_una
	return

  .acies:
	; spec §5.4: the LANE COUNT is in the type, so it can never come from
	; the host. Argument 0 is the element type, argument 1 the count.
	mov	rdi, rbx
	mov	rsi, [chkt_gj]
	xor	rdx, rdx
	call	__chk_ty_genarg
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, [chkt_gj]
	mov	rdx, 1
	call	__chk_ty_genlit
	mov	r14, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, AST_TY_ACIES
	mov	rdx, r13
	mov	rcx, r14
	call	ast_type_una
	return

  .no:
	xor	eax, eax
	return
endp

; __chk_ty_genarg(w, genargs, i) -> rax = the type of generic argument `i`, or
; the ERROR type when there is none. `EXS-E0304` (wrong number of generic
; arguments) is raised at the `GenericArgs` node, or at nothing when the whole
; clause is missing -- in which case the diagnostic goes on the enclosing type
; expression through the error type it returns.
proc __chk_ty_genarg, chkt_wy, chkt_gy, chkt_iy
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkt_wy]
	mov	r12, [chkt_gy]
	test	r12, r12
	jz	.none
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]
	cmp	rcx, [chkt_iy]
	jbe	.bad
	mov	ecx, [rax + AstNode.a]
	mov	rdi, [rbx + ChkTyWork.wtre]
	lea	rsi, [rcx]
	add	rsi, [chkt_iy]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	chk_ty_of_node
	return
  .bad:
	mov	rdi, rbx
	mov	rsi, 304
	mov	rdx, r12
	call	__chk_ty_raise
  .none:
	mov	rdi, rbx
	call	__chk_ty_err
	return
endp

; __chk_ty_genlit(w, genargs, i) -> rax = the INT value of generic argument
; `i`, 0 when it is absent or is not an integer literal.
proc __chk_ty_genlit, chkt_wz, chkt_gz, chkt_iz
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wz]
	mov	r12, [chkt_gz]
	test	r12, r12
	jz	.bad
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]
	cmp	rcx, [chkt_iz]
	jbe	.bad
	mov	ecx, [rax + AstNode.a]
	mov	rdi, [rbx + ChkTyWork.wtre]
	lea	rsi, [rcx]
	add	rsi, [chkt_iz]
	call	ast_extra_at
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_kind
	cmp	rax, AST_LIT
	jne	.bad
	mov	rdi, rbx
	mov	rsi, r13
	call	chk_ty_litval
	test	rdx, rdx
	jz	.bad
	return
  .bad:
	mov	rdi, rbx
	mov	rsi, 304
	mov	rdx, r12
	call	__chk_ty_raise
	xor	eax, eax
	return
endp

; chk_ty_litval(w, litnode) -> rax = the value, rdx = 1 on success.
;
; `[OPEN]`, and checker.md section 2.3 says why: "spec §8.4's literal grammar"
; does not exist, so `Lit` keeps TEXT and not a value (typed-ast.md section
; 2.3). What is implemented is the part every reading of that grammar will
; contain -- decimal digits with `_` admitted as a separator -- and anything
; else answers "not a value", which becomes `EXS-E0308` at the literal.
; Overflow past 64 bits is a failure, not a wrap.
;
; HEXADECIMAL IS THE ONE BASE §8.4 NOW SETTLES (`[UNTESTED]`,
; docs/design/wire-codec.md D6): `0x` then one or more of `0-9 a-f A-F`, and
; nothing else -- no `0X` (the lexer's `EXS-E0210`; here it would simply fail
; to be a digit), no `_` inside a hex run (D6 admits none, and the decimal
; path's `_` is a recorded finding, wire-codec.md section 9 item 6, not a
; precedent), and more than sixteen significant digits is past 64 bits and
; answers "not a value" exactly as a too-long decimal does. The TOKEN is the
; lexer's; this routine reads whatever bytes the `INT` literal's text holds,
; so it is testable before the lexer emits `0x…` as one token
; (tests/unit/chk_ty_hexlit.asm rewrites a literal's text to do so). Too
; wide for the TYPE it lands in is `__chk_ty_fits`'s `EXS-E0308`, as for
; decimal: the value is computed here, the width is checked there.
;
; A LITERAL WHOSE TEXT THE INTERNER DOES NOT HAVE READS AS ZERO, and that is
; the second half of `__chk_ty_bytes`'s bargain. Such an id cannot arise in a
; real compilation -- the lexer interned every literal's text before the
; parser saw it -- and can arise only in a hand-written `.ast` dump, which is
; checker.md section 6's own way of testing a pass without a parser. Failing
; there would make every such fixture report `EXS-E0308` on every literal it
; contains; reading 0 lets the literal take its expected type, which is the
; fact those fixtures are about. The degradation is documented rather than
; silent, and it is `[UNTESTED]` whether any real input can reach it -- by
; construction, none can.
proc chk_ty_litval, chkt_wl, chkt_nl
	uses	rbx, r12, r13, r14
	locals
		slot chkt_vl, dq
		slot chkt_dg, dq
	endl
	mov	rbx, [chkt_wl]
	mov	rdi, rbx
	mov	rsi, [chkt_nl]
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.aux]
	cmp	ecx, AST_LIT_INT
	jne	.no
	mov	ecx, [rax + AstNode.a]
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_ty_bytes
	mov	r12, rax
	mov	r13, rdx
	test	r13, r13
	jz	.unknown		; a hand-written dump -- see the header
	mov	qword [chkt_vl], 0
	xor	r14, r14
	xor	ecx, ecx		; digits actually seen
	cmp	r13, 2
	jb	.digit
	cmp	byte [r12], '0'
	jne	.digit
	cmp	byte [r12 + 1], 'x'
	je	.hex
  .digit:
	cmp	r14, r13
	jae	.done
	movzx	eax, byte [r12 + r14]
	cmp	al, '_'
	je	.skip
	cmp	al, '0'
	jb	.no
	cmp	al, '9'
	ja	.no
	sub	eax, '0'
	mov	[chkt_dg], rax		; the digit; `mul` clobbers rdx:rax
	mov	rax, [chkt_vl]
	mov	rdx, 10
	mul	rdx
	jc	.no			; more than 64 bits is not a wrap
	add	rax, [chkt_dg]
	jc	.no
	mov	[chkt_vl], rax
	inc	rcx
  .skip:
	inc	r14
	jmp	.digit
  .done:
	test	rcx, rcx
	jz	.no
	mov	rax, [chkt_vl]
	mov	rdx, 1
	return

  .hex:
	; `0x` + hex digits. The value accumulates in rax; a digit arriving
	; when the top nibble is already occupied would shift a set bit out
	; past bit 63, and that is "beyond 64 bits", not a wrap.
	mov	r14, 2
	xor	eax, eax
  .hdigit:
	cmp	r14, r13
	jae	.done_hex
	movzx	edx, byte [r12 + r14]
	cmp	dl, '0'
	jb	.no
	cmp	dl, '9'
	jbe	.hdec
	or	dl, 0x20		; 'A'-'F' -> 'a'-'f'; nothing else lands there
	cmp	dl, 'a'
	jb	.no
	cmp	dl, 'f'
	ja	.no
	sub	dl, 'a' - 10
	jmp	.hacc
  .hdec:
	sub	dl, '0'
  .hacc:
	mov	r8, rax
	shr	r8, 60
	jnz	.no			; more than 64 bits is not a wrap
	shl	rax, 4
	or	rax, rdx
	inc	rcx
	inc	r14
	jmp	.hdigit
  .done_hex:
	test	rcx, rcx
	jz	.no			; `0x` with no digit
	mov	rdx, 1
	return

  .unknown:
	xor	eax, eax
	mov	rdx, 1
	return
  .no:
	xor	eax, eax
	xor	edx, edx
	return
endp

; =============================================================================
; declared types
; =============================================================================

; __chk_ty_decl_type(w, d) -> rax = the type declaration `d` NAMES when it is
; used in type position. Nominal for a struct, an interface, a generic
; parameter and a capability atom; TRANSPARENT for a `typus`.
proc __chk_ty_decl_type, chkt_wc, chkt_dc
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wc]
	mov	r12, [chkt_dc]
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_decl_count
	cmp	r12, rax
	ja	.bad
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	r13d, byte [rax + AstDecl.kind]
	cmp	r13d, AST_D_TYPUS
	je	.alias
	cmp	r13d, AST_D_STRUCT
	je	.nominal
	cmp	r13d, AST_D_IFACE
	je	.nominal
	cmp	r13d, AST_D_GENPARAM
	je	.nominal
	cmp	r13d, AST_D_CAPATOM
	je	.nominal
	; A value name in type position -- `x: y` where `y` is a binding.
	; checker.md class I: "type expression or annotation not applicable".
	mov	rdi, rbx
	mov	rsi, 309
	mov	rdx, 0
	call	__chk_ty_raise
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rax
	call	__chk_ty_at_decl
	mov	rdi, rbx
	call	__chk_ty_err
	return
  .nominal:
	mov	eax, [rax + AstDecl.ty]
	test	eax, eax
	jnz	.have
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_nominal
	return
  .have:
	return
  .alias:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_typus
	return
  .bad:
	mov	rdi, rbx
	call	__chk_ty_err
	return
endp

; __chk_ty_at_decl(w, d, dg) -- move a buffered diagnostic's caret onto
; declaration `d`'s own span. A declaring node's span is the whole construct;
; the declaration's is the name-bearing part.
proc __chk_ty_at_decl, chkt_wd2, chkt_dd2, chkt_gd2
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkt_wd2]
	mov	r12, [chkt_gd2]
	test	r12, r12
	jz	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_dd2]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.span.file_id]
	mov	[r12 + ChkDiag.caret.file_id], ecx
	mov	ecx, [rax + AstDecl.span.start]
	mov	[r12 + ChkDiag.caret.start], ecx
	mov	ecx, [rax + AstDecl.span.len]
	mov	[r12 + ChkDiag.caret.len], ecx
  .out:
	return
endp

; __chk_ty_nominal(w, d) -> rax = the nominal type of a struct / interface /
; generic parameter / capability atom declaration, interning it on first use
; and recording it in `Decl.ty`.
proc __chk_ty_nominal, chkt_wn2, chkt_dn2
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wn2]
	mov	r12, [chkt_dn2]
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	r13d, byte [rax + AstDecl.kind]
	cmp	r13d, AST_D_CAPATOM
	je	.atom
	mov	rsi, AST_TY_STRUCT
	cmp	r13d, AST_D_STRUCT
	je	.una
	mov	rsi, AST_TY_IFACE
	cmp	r13d, AST_D_IFACE
	je	.una
	mov	rsi, AST_TY_PARAM
  .una:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rdx, r12
	xor	rcx, rcx
	call	ast_type_una
	jmp	.record
  .atom:
	; the ordinal is the atom's offset from `ast_cap_base`
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_cap_base
	test	rax, rax
	jz	.none
	mov	rsi, r12
	sub	rsi, rax
	inc	rsi
	mov	rdi, rbx
	call	__chk_ty_atom
  .record:
	mov	r13, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	mov	rdx, r13
	call	ast_decl_ty
	mov	rax, r13
	return
  .none:
	mov	rdi, rbx
	call	__chk_ty_err
	return
endp

; __chk_ty_typus(w, d) -> rax = a `typus` declaration's type.
;
; TRANSPARENT (checker.md section 2.2, finding 8): the alias's `Decl.ty` IS the
; right-hand side's id, and `AST_TY_ALIAS` stays unused. A cycle is `EXS-E0303`
; -- class C's "alias cycle" -- reported once, at the declaration that closed
; it, with the error type standing in so nothing cascades.
proc __chk_ty_typus, chkt_wx, chkt_dx
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wx]
	mov	r12, [chkt_dx]
	lea	rdi, [rbx + ChkTyWork.wast]
	mov	rsi, r12
	call	vec_get
	movzx	ecx, byte [rax]
	cmp	ecx, CHK_TY_A_DONE
	je	.done
	cmp	ecx, CHK_TY_A_BUSY
	je	.cycle
	mov	byte [rax], CHK_TY_A_BUSY

	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	mov	r13d, [rax + AstDecl.node]
	test	r13, r13
	jz	.erronly
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]		; `Typus.a` = the aliased type
	mov	rdi, rbx
	mov	rsi, rcx
	call	chk_ty_of_node
	jmp	.record
  .erronly:
	mov	rdi, rbx
	call	__chk_ty_err
  .record:
	mov	r13, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	mov	rdx, r13
	call	ast_decl_ty
	lea	rdi, [rbx + ChkTyWork.wast]
	mov	rsi, r12
	call	vec_get
	mov	byte [rax], CHK_TY_A_DONE
	mov	rax, r13
	return

  .cycle:
	mov	rdi, rbx
	mov	rsi, 303
	xor	rdx, rdx
	call	__chk_ty_raise
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__chk_ty_at_decl
	mov	rdi, rbx
	call	__chk_ty_err
	mov	r13, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	mov	rdx, r13
	call	ast_decl_ty
	lea	rdi, [rbx + ChkTyWork.wast]
	mov	rsi, r12
	call	vec_get
	mov	byte [rax], CHK_TY_A_DONE
	mov	rax, r13
	return

  .done:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	mov	eax, [rax + AstDecl.ty]
	return
endp

; =============================================================================
; rows
; =============================================================================

; chk_ty_rowid(w, rownode, sigoff, nparams) -> rax = the interned row id.
;
; `rownode` 0 is the EMPTY row, not "no row" (rows/rowdefs.inc). `sigoff` /
; `nparams` are the enclosing `Sig`'s parameter list, needed only to turn
; `sicut IDENT` into an ordinal; a braced `TypeRow` passes 0/0 and every
; `sicut` item in it is `EXS-E0423` (checker.md finding 5).
proc chk_ty_rowid, chkt_wr, chkt_nr, chkt_or2, chkt_pr
	uses	rbx, r12, r13, r14
	locals
		slot chkt_lr, dq
	endl
	mov	rbx, [chkt_wr]
	mov	r12, [chkt_nr]
	test	r12, r12
	jz	.empty
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.a]		; the item list
	mov	r14d, [rax + AstNode.b]		; how many
	mov	qword [chkt_lr], 0
  .item:
	mov	rax, [chkt_lr]
	cmp	rax, r14
	jae	.intern
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	add	rsi, [chkt_lr]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_or2]
	mov	rcx, [chkt_pr]
	call	__chk_ty_rowitem
	inc	qword [chkt_lr]
	jmp	.item
  .intern:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	chk_row_of_node
	return
  .empty:
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	chk_row_empty
	return
endp

; __chk_ty_rowitem(w, n, sigoff, nparams) -- resolve one `RowItem`'s `d` when
; it is `sicut IDENT`; a `Path` item was already resolved by pass 1.
proc __chk_ty_rowitem, chkt_ws2, chkt_ns2, chkt_os2, chkt_ps2
	uses	rbx, r12, r13, r14
	locals
		slot chkt_ms2, dq
	endl
	mov	rbx, [chkt_ws2]
	mov	r12, [chkt_ns2]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.aux]
	test	ecx, AST_ROW_SICUT
	jz	.out				; a `Path` item: pass 1's
	mov	ecx, [rax + AstNode.a]		; the parameter's NAME id
	mov	[chkt_ms2], rcx
	cmp	qword [chkt_ps2], 0
	je	.bad				; a braced row has no parameters
	xor	r13, r13
  .scan:
	cmp	r13, [chkt_ps2]
	jae	.bad
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_os2]
	add	rsi, r13
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]		; `Param.a` = the name
	cmp	rcx, [chkt_ms2]
	je	.found
	inc	r13
	jmp	.scan
  .found:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13			; the ZERO-BASED ordinal
	call	__chk_ty_setd
	return
  .bad:
	; checker.md class L, spec §4.2: `sicut` naming something that is not a
	; parameter of this signature, or appearing where there are none.
	mov	rdi, rbx
	mov	rsi, 423
	mov	rdx, r12
	call	__chk_ty_raise
  .out:
	return
endp

; =============================================================================
; the five phases
; =============================================================================

; chk_ty_sigs(w) -- phases A to E. Every loop is over `Ast.decls` in
; DECLARATION ORDER, which is source order (typed-ast.md section 2.4), so
; nothing here can observe a hash or an address (CLAUDE.md).
proc chk_ty_sigs, chkt_wS
	uses	rbx, r12, r13, r14
	locals
		slot chkt_nS, dq
	endl
	mov	rbx, [chkt_wS]

	; ---- A: nominal ids ------------------------------------------------
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_decl_count
	mov	[chkt_nS], rax
	mov	r12, 1
  .a:
	cmp	r12, [chkt_nS]
	ja	.b0
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_STRUCT
	je	.a_do
	cmp	ecx, AST_D_IFACE
	je	.a_do
	cmp	ecx, AST_D_GENPARAM
	je	.a_do
	cmp	ecx, AST_D_CAPATOM
	je	.a_do
	jmp	.a_next
  .a_do:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_nominal
  .a_next:
	inc	r12
	jmp	.a

	; ---- B: aliases ----------------------------------------------------
  .b0:
	mov	r12, 1
  .b:
	cmp	r12, [chkt_nS]
	ja	.c0
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_TYPUS
	jne	.b_next
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_typus
  .b_next:
	inc	r12
	jmp	.b

	; ---- C: fields ------------------------------------------------------
	; `Field.b` is AST_R_NODEQ (ast/kinds.inc's narrowing 1), so the
	; annotation is always present.
	;
	; PARAMETERS ARE NOT HERE. They are interned by phase D, from the `Sig`'s
	; own parameter list, and `Decl.ty` is written there -- because interning
	; a type EXPRESSION can RAISE (`EXS-E0520` on a `refero` inside an
	; `externus` signature, `EXS-E0301` on an unknown name), and doing it
	; twice for one node reports it twice. Measured: this fixture pair
	; (tests/unit/chk_ty_e0520_refero.asm) produced two identical `E0520`s
	; until this loop stopped visiting `AST_D_PARAM`.
  .c0:
	mov	r12, 1
  .c:
	cmp	r12, [chkt_nS]
	ja	.d0
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_FIELD
	jne	.c_next
  .c_do:
	mov	r13d, [rax + AstDecl.node]
	test	r13, r13
	jz	.c_next
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_in_externus
	mov	[rbx + ChkTyWork.wext], eax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]
	mov	rdi, rbx
	mov	rsi, rcx
	call	chk_ty_of_node
	mov	r14, rax
	mov	dword [rbx + ChkTyWork.wext], 0
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	mov	rdx, r14
	call	ast_decl_ty
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r14
	call	__chk_ty_setty
  .c_next:
	inc	r12
	jmp	.c

	; ---- D: function signatures ----------------------------------------
  .d0:
	mov	r12, 1
  .d:
	cmp	r12, [chkt_nS]
	ja	.e0
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_FN
	je	.d_do
	cmp	ecx, AST_D_MEMBER
	je	.d_do
	cmp	ecx, AST_D_EXTERNFN
	je	.d_do
	jmp	.d_next
  .d_do:
	mov	rdi, rbx
	mov	rsi, r12
	call	chk_ty_signature
  .d_next:
	inc	r12
	jmp	.d

	; ---- E: the impl map, and the ABI enumeration ----------------------
  .e0:
	mov	r12, 1
  .e:
	cmp	r12, [chkt_nS]
	ja	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_IMPL
	je	.e_impl
	cmp	ecx, AST_D_EXTERNUS
	je	.e_ext
	jmp	.e_next
  .e_impl:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_impl
	jmp	.e_next
  .e_ext:
	; spec §5.3: the ABI set is CLOSED, and ast/kinds.inc records 0 for a
	; spelling it does not have precisely so that Stage 2 can raise this.
	mov	r13d, [rax + AstDecl.node]
	test	r13, r13
	jz	.e_next
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.aux]
	cmp	ecx, AST_ABI_UNKNOWN
	jne	.e_next
	mov	rdi, rbx
	mov	rsi, 309
	mov	rdx, r13
	call	__chk_ty_raise
  .e_next:
	inc	r12
	jmp	.e
  .out:
	return
endp

; __chk_ty_in_externus(w, d) -> eax = 1 if declaration `d` is inside an
; `externus` block. Spec §5.3's boundary, one `Decl.parent` hop: a parameter's
; parent is its `ExternFn`, whose parent is the `Externus`.
proc __chk_ty_in_externus, chkt_wE, chkt_dE
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkt_wE]
	mov	r12, 3			; at most three hops: param -> fn -> externus
  .hop:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_dE]
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_EXTERNUS
	je	.yes
	mov	ecx, [rax + AstDecl.parent]
	test	ecx, ecx
	jz	.no
	mov	[chkt_dE], rcx
	dec	r12
	jnz	.hop
  .no:
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; chk_ty_signature(w, d) -> rax = the `fn` type of function declaration `d`,
; also written to `Decl.ty` and to its `Fn` node's `ty`.
;
; The parameter types are already in `Decl.ty` (phase C) but are re-derived
; from the `Sig`'s own parameter NODES here, because the `extra` run this
; builds must be in signature order and `Ast.decls` order is source order,
; which for a nested function's parameters is the same thing but is not
; guaranteed to stay so.
proc chk_ty_signature, chkt_wF, chkt_dF
	uses	rbx, r12, r13, r14
	locals
		slot chkt_sF, dq
		slot chkt_oF, dq
		slot chkt_pF, dq
		slot chkt_mF, dq
		slot chkt_nF, dq
	endl
	mov	rbx, [chkt_wF]
	mov	r12, [chkt_dF]
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	mov	r13d, [rax + AstDecl.node]
	mov	[chkt_nF], r13
	test	r13, r13
	jz	.none
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]		; `Fn.a` / `Lambda.a` = `Sig`
	mov	[chkt_sF], rcx
	test	rcx, rcx
	jz	.none

	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_in_externus
	mov	[rbx + ChkTyWork.wext], eax

	mov	rdi, rbx
	mov	rsi, [chkt_sF]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_oF], rcx			; parameter list `extra` offset
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_pF], rcx			; parameter count
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_mF], rcx			; result type node, 0 = `unit`

	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_list_mark
	mov	r14, rax
	xor	r13, r13
  .param:
	cmp	r13, [chkt_pF]
	jae	.result
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_oF]
	add	rsi, r13
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_param
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	inc	r13
	jmp	.param
  .result:
	mov	rdi, rbx
	mov	rsi, [chkt_mF]
	call	chk_ty_of_node
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r14
	call	ast_list_emit
	mov	r13, rax

	; the row -- `Sig.d`, with the parameter list for `sicut f`
	mov	rdi, rbx
	mov	rsi, [chkt_sF]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.d]
	mov	rdi, rbx
	mov	rsi, rcx
	mov	rdx, [chkt_oF]
	mov	rcx, [chkt_pF]
	call	chk_ty_rowid
	mov	r14, rax

	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	mov	rdx, [chkt_pF]
	mov	rcx, r14
	call	ast_type_fn
	mov	r13, rax

	mov	dword [rbx + ChkTyWork.wext], 0
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	mov	rdx, r13
	call	ast_decl_ty
	mov	rdi, rbx
	mov	rsi, [chkt_nF]
	mov	rdx, r13
	call	__chk_ty_setty
	mov	rdi, rbx
	mov	rsi, [chkt_sF]
	mov	rdx, r13
	call	__chk_ty_setty
	mov	rax, r13
	return
  .none:
	mov	dword [rbx + ChkTyWork.wext], 0
	mov	rdi, rbx
	call	__chk_ty_err
	return
endp

; __chk_ty_param(w, n) -> rax = the type of `Param` node `n`, written to the
; node and to the declaration it introduces. Called ONCE per parameter, from
; the signature that owns it -- see phase C's note on why not twice.
proc __chk_ty_param, chkt_wP2, chkt_nP2
	uses	rbx, r12, r13
	locals
		slot chkt_tP2, dq
	endl
	mov	rbx, [chkt_wP2]
	mov	r12, [chkt_nP2]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.b]		; `Param.b` = the annotation
	mov	r13d, [rax + AstNode.d]		; the declaration it introduces
	mov	rdi, rbx
	mov	rsi, rcx
	call	chk_ty_of_node
	mov	[chkt_tP2], rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [chkt_tP2]
	call	__chk_ty_setty
	test	r13, r13
	jz	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	mov	rdx, [chkt_tP2]
	call	ast_decl_ty
  .out:
	mov	rax, [chkt_tP2]
	return
endp

; __chk_ty_impl(w, d) -- one implementation: type the head, and key the impl
; map on (interface declaration, target type id).
;
; checker.md section 2.7: "a second impl for one key is a class-B error", which
; is `EXS-E0302`. Pass 1 could not do this half of class B because the key
; holds a TYPE id (its own header says so), which is why it lives here.
proc __chk_ty_impl, chkt_wI, chkt_dI
	uses	rbx, r12, r13, r14
	locals
		slot chkt_hI, dq
		slot chkt_fI, dq
		slot chkt_tI, dq
		slot chkt_nI, dq
	endl
	mov	rbx, [chkt_wI]
	mov	r12, [chkt_dI]
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	mov	r13d, [rax + AstDecl.node]
	mov	[chkt_nI], r13
	test	r13, r13
	jz	.out
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]		; `Impl.a` = `ImplHead`
	mov	[chkt_hI], rcx
	test	rcx, rcx
	jz	.out
	mov	rdi, rbx
	mov	rsi, [chkt_hI]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]		; the interface `Path`
	mov	r14, rcx
	mov	ecx, [rax + AstNode.b]		; the target `Type`
	mov	[chkt_tI], rcx

	mov	rdi, rbx
	mov	rsi, [chkt_tI]
	call	chk_ty_of_node
	mov	[chkt_tI], rax
	mov	rdi, rbx
	mov	rsi, r14
	call	__chk_ty_ifacedecl
	mov	[chkt_fI], rax
	test	rax, rax
	jz	.head_row

	; the key: two dwords, in a block that never moves (`rt/map.inc`
	; BORROWS a key -- ast/types.inc's header, resolve.inc's `ChkCtx.keys`)
	mov	r14, [rbx + ChkTyWork.wikn]
	mov	eax, [chkt_fI]
	mov	[r14], eax
	mov	eax, [chkt_tI]
	mov	[r14 + 4], eax
	lea	rdi, [rbx + ChkTyWork.wimpl]
	mov	rsi, r14
	mov	rdx, 8
	call	map_get
	test	rax, rax
	jz	.insert
	mov	r13, rax			; the EARLIER implementation
	mov	rdi, rbx
	mov	rsi, 302
	mov	rdx, [chkt_nI]
	call	__chk_ty_raise
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, rax
	call	__chk_ty_rel_decl
	jmp	.head_row
  .insert:
	add	qword [rbx + ChkTyWork.wikn], 8
	lea	rdi, [rbx + ChkTyWork.wimpl]
	mov	rsi, r14
	mov	rdx, 8
	mov	rcx, r12
	call	map_insert

  .head_row:
	; the head's own row (spec §4.4's mark). Interned here so that pass 3's
	; ceiling check reads a row id and not a node.
	mov	rdi, rbx
	mov	rsi, [chkt_hI]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.c]
	mov	rdi, rbx
	mov	rsi, rcx
	xor	rdx, rdx
	xor	rcx, rcx
	call	chk_ty_rowid
  .out:
	return
endp

; __chk_ty_rel_decl(w, d, dg) -- attach declaration `d`'s span to a buffered
; diagnostic as its RELATED span (spec §8.3's "the other one is here").
proc __chk_ty_rel_decl, chkt_wR, chkt_dR, chkt_gR
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkt_wR]
	mov	r12, [chkt_gR]
	test	r12, r12
	jz	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_dR]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.span.file_id]
	mov	[r12 + ChkDiag.rel.file_id], ecx
	mov	ecx, [rax + AstDecl.span.start]
	mov	[r12 + ChkDiag.rel.start], ecx
	mov	ecx, [rax + AstDecl.span.len]
	mov	[r12 + ChkDiag.rel.len], ecx
	mov	dword [r12 + ChkDiag.hasrel], 1
  .out:
	return
endp

; chk_ty_findimpl(w, ifacedecl, targetty) -> rax = the implementation
; declaration, or 0. The lookup half of the impl map -- checker.md section
; 2.7's "the checker looks up the impl of `Summable` for `Left` in the impl
; map"; one hashed lookup, never a walk (CLAUDE.md).
proc chk_ty_findimpl, chkt_wK, chkt_fK, chkt_tK
	uses	rbx
	locals
		slot chkt_kK, dq
	endl
	mov	rbx, [chkt_wK]
	mov	eax, [chkt_fK]
	mov	[chkt_kK], eax
	mov	eax, [chkt_tK]
	mov	[chkt_kK + 4], eax
	lea	rdi, [rbx + ChkTyWork.wimpl]
	lea	rsi, [chkt_kK]
	mov	rdx, 8
	call	map_get
	return
endp
