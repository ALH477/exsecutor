; compiler/x86_64/checker/rows/intern.inc
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
; THE INTERNED ROW TABLE -- `Ast.rows`, and `chk_row_intern`, which
; `docs/design/checker.md` section 6 names as the ONE API two checker agents
; share ("rows agent delivers it first, types agent calls it for `TyFn`,
; `TyDyn` and declared `Sig` rows").
;
; A ROW IS A SET, AND THE SET IS TWO MASKS. typed-ast.md section 2.6 asks for
; items "sorted on (kind, id) before interning so `{alloc, rete}` and `{rete,
; alloc}` are one row id". After `potestas` flattening every kind-0 id is one
; of spec §4.6's eleven atom declarations, and `ast_cap_push` makes those ids
; CONSECUTIVE (`N + k` for §4.6 ordinal `k`; 08b4a61) -- so the canonical form
; of a row is an eleven-bit atom mask plus a 64-bit ordinal mask, the sort is
; a bit scan, and duplicate items collapse for free. That is checker.md
; finding 3's "the sort is a bit scan once atom ids are consecutive", built.
;
; INTERNING IS A LINEAR SCAN OVER `Ast.rows`, NOT A MAP, and this is a
; deliberate departure from ast/types.inc's discipline rather than an
; oversight of it. `chk_row_intern(tree, items, count)` takes the tree and
; nothing else -- checker.md section 6 fixes the signature -- so a map would
; have to be reachable FROM THE TREE, and `Ast` has one map slot
; (`Ast.typemap`) which is the type table's. The alternatives were a
; module-global map (wrong the moment two trees are live in one process, which
; ast/load.inc's round trip already does), or a map keyed on a tuple
; containing the tree's ADDRESS (CLAUDE.md: "no ordering may depend on a
; pointer value" -- ids would not, but putting an address in a key to work
; around a missing slot is how that rule gets eroded). What is left is the
; scan, and it is cheap for the right reason: rows are FEW. A row is one
; distinct `poscit` clause or one inferred function row, so the table's size
; is bounded by the module's function count, not its node count -- unlike
; `Ast.types`, which is why that file needed a map and this one does not.
; Cost is O(rows x items) per intern; `[OPEN]`, no real module has been
; interned. If a module ever makes this hot the fix is a `rowmap` slot on
; `Ast` -- ast/'s to add, keyed on the sixteen bytes of the two masks, with
; ids still coming from `Ast.rows.len`.
;
; DETERMINISM. A row id is `Ast.rows` own length after the push -- first-use
; index, a function of the input alone. Nothing here hashes, nothing here
; iterates a map, and the scan runs in ascending row id (CLAUDE.md,
; "Determinism is not optional"; checker.md section 4's "row ids: first-use").
;
; ID 0 IS NOT A ROW AND THE EMPTY ROW IS NOT ID 0 -- the reasoning is in
; rowdefs.inc, because it is a decision, not a mechanism.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1 (rules 3-6), §4.2 (positional
; substitution), §4.4 (the ceiling), §4.6 (the eleven atoms), §8.6 (`poscit
; {}`); docs/design/checker.md sections 2.1, 2.3, 4, 6; docs/design/
; typed-ast.md section 2.6.
; -----------------------------------------------------------------------------

include 'rowdefs.inc'

; ---- masks of an interned row ----------------------------------------------
; rax = the eleven-bit atom mask, rdx = the 64-bit ordinal mask, for row
; `chkr_row`. Row 0 reads as the empty row (rowdefs.inc, "READING a 0 is
; permissive"). The masks are recomputed from `Ast.extra` on every call
; rather than cached: there is nowhere on the tree to cache them, and the
; item count is at most 11 + 64.
proc __chk_row_masks, chkr_tree, chkr_row
	uses	rbx, r12, r13, r14
	locals
		slot chkr_ma, dq
		slot chkr_mo, dq
		slot chkr_k,  dq
	endl
	mov	qword [chkr_ma], 0
	mov	qword [chkr_mo], 0
	mov	rbx, [chkr_tree]
	mov	r12, [chkr_row]
	test	r12, r12
	jz	.done
	mov	rax, [rbx + Ast.rows.len]
	rassert	r12 le rax
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_row_at
	mov	r13d, [rax + AstRow.items]
	mov	r14d, [rax + AstRow.count]
	xor	r12, r12
  .item:
	cmp	r12, r14
	jge	.done
	mov	rdi, rbx
	lea	rsi, [r13 + r12*2]
	call	ast_extra_at			; rax = the item's kind
	mov	[chkr_k], rax
	mov	rdi, rbx
	lea	rsi, [r13 + r12*2 + 1]
	call	ast_extra_at			; rax = the item's id
	mov	rcx, [chkr_k]
	test	rcx, rcx
	jnz	.ord

	; an atom declaration id -> its §4.6 bit
	mov	[chkr_k], rax
	mov	rdi, rbx
	call	ast_cap_base
	rassert	rax ge 1
	mov	rcx, [chkr_k]
	sub	rcx, rax
	rassert	rcx ge 0
	mov	rdx, AST_CAP_COUNT
	dec	rdx
	rassert	rcx le rdx
	mov	rax, 1
	shl	rax, cl
	or	[chkr_ma], rax
	jmp	.next
  .ord:
	rassert	rcx eq CHK_ROW_ORD
	mov	rcx, rax
	mov	rdx, CHK_ROW_ORD_MAX
	rassert	rcx lt rdx
	mov	rax, 1
	shl	rax, cl
	or	[chkr_mo], rax
  .next:
	inc	r12
	jmp	.item
  .done:
	mov	rax, [chkr_ma]
	mov	rdx, [chkr_mo]
	return
endp

; ---- one input item into a mask pair ---------------------------------------
; `chkr_sn` points at two qwords: the atom mask, then the ordinal mask.
; A `potestas` decl expands here, recursively, into the atoms its `Path`
; items resolved to -- checker.md section 2.1's table, last row.
proc __chk_row_add, chkr_tr2, chkr_kd, chkr_id2, chkr_sn, chkr_dp
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [chkr_tr2]
	mov	rax, [chkr_dp]
	mov	rcx, CHK_ROW_DEPTH_MAX
	rassert	rax le rcx
	mov	rax, [chkr_kd]
	cmp	rax, CHK_ROW_ORD
	je	.ord
	rassert	rax eq CHK_ROW_ATOM

	mov	r12, [chkr_id2]
	rassert	r12 ge 1
	mov	rax, [rbx + Ast.decls.len]
	rassert	r12 le rax
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_decl_at
	movzx	eax, byte [rax + AstDecl.kind]
	cmp	eax, AST_D_CAPATOM
	je	.atom
	cmp	eax, AST_D_POTESTAS
	je	.pot
	jmp	.bad

  .atom:
	mov	rdi, rbx
	call	ast_cap_base
	rassert	rax ge 1
	mov	rcx, r12
	sub	rcx, rax
	rassert	rcx ge 0
	mov	rdx, AST_CAP_COUNT
	dec	rdx
	rassert	rcx le rdx
	mov	rax, 1
	shl	rax, cl
	mov	rdi, [chkr_sn]
	or	[rdi], rax
	return

  .ord:
	mov	rcx, [chkr_id2]
	rassert	rcx ge 0
	mov	rdx, CHK_ROW_ORD_MAX
	rassert	rcx lt rdx
	mov	rax, 1
	shl	rax, cl
	mov	rdi, [chkr_sn]
	or	[rdi + 8], rax
	return

  .pot:
	; `potestas Hospes = { alloc, archivum, ... }` -- the node's list holds
	; `Path` nodes (ast/from_cst.inc's `.potestas`), each already resolved
	; to a declaration by pass 1.
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_decl_at
	mov	eax, [rax + AstDecl.node]
	rassert	rax ge 1
	mov	rdi, rbx
	mov	rsi, rax
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	rassert	rcx eq AST_POTESTAS
	mov	r13d, [rax + AstNode.a]
	mov	r14d, [rax + AstNode.b]
	xor	r12, r12
  .pitem:
	cmp	r12, r14
	jge	.pdone
	mov	rdi, rbx
	lea	rsi, [r13 + r12]
	call	ast_extra_at
	rassert	rax ge 1
	mov	rdi, rbx
	mov	rsi, rax
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	rassert	rcx eq AST_PATH
	mov	eax, [rax + AstNode.d]
	rassert	rax ge 1		; pass 1 resolved it, or this is a
					; compiler bug and not a diagnosable
					; program (asm-conventions 1.3)
	mov	rdi, rbx
	mov	rsi, CHK_ROW_ATOM
	mov	rdx, rax
	mov	rcx, [chkr_sn]
	mov	r8, [chkr_dp]
	inc	r8
	call	__chk_row_add
	inc	r12
	jmp	.pitem
  .pdone:
	return

  .bad:
	; A kind-0 item naming something that is neither an atom nor a
	; `potestas` is a caller contract violation -- spec §4.2's "`sub` of a
	; non-atom", which the resolve pass diagnoses (class L) and never
	; forwards here.
	rassert	eax eq AST_D_CAPATOM
	return
endp

; ---- intern a row given its two masks --------------------------------------
; rax = the row id, >= 1. This is the one place a row is created, and the one
; place an id is assigned.
proc chk_row_of_masks, chkr_tree, chkr_ats, chkr_ors
	uses	rbx, r12, r13, r14
	locals
		slot chkr_st, dq
	endl
	mov	rbx, [chkr_tree]
	mov	rax, [chkr_ats]
	mov	rcx, CHK_ROW_ATOM_MASK
	not	rcx
	and	rax, rcx
	rassert	rax eq 0		; only the eleven §4.6 bits exist

	; ---- already interned? ----
	mov	r12, 1
  .scan:
	mov	rax, [rbx + Ast.rows.len]
	cmp	r12, rax
	jg	.miss
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_row_masks
	mov	rcx, [chkr_ats]
	cmp	rax, rcx
	jne	.next
	mov	rcx, [chkr_ors]
	cmp	rdx, rcx
	jne	.next
	mov	rax, r12
	return
  .next:
	inc	r12
	jmp	.scan

  .miss:
	; ---- emit the items, atoms then ordinals, each ascending ----
	; That order IS (kind, id) ascending: kind 0 before kind 1, and within
	; the atoms ascending bit index is ascending declaration id
	; (rowdefs.inc). checker.md section 3's verifier clause "every row's
	; items are strictly ascending (kind, id)" holds by construction.
	mov	qword [chkr_st], 0
	xor	r13, r13		; the item count
	xor	r14, r14		; the bit index
  .atom:
	cmp	r14, AST_CAP_COUNT
	jge	.atoms_done
	mov	rax, [chkr_ats]
	mov	rcx, r14
	shr	rax, cl
	test	rax, 1
	jz	.atom_next
	mov	rdi, rbx
	mov	rsi, CHK_ROW_ATOM
	call	ast_extra_push
	cmp	qword [chkr_st], 0
	jne	.a_started
	mov	[chkr_st], rax		; the run's first index is the offset
  .a_started:
	mov	rdi, rbx
	call	ast_cap_base
	rassert	rax ge 1
	add	rax, r14
	mov	rdi, rbx
	mov	rsi, rax
	call	ast_extra_push
	inc	r13
  .atom_next:
	inc	r14
	jmp	.atom
  .atoms_done:

	xor	r14, r14
  .ordb:
	cmp	r14, CHK_ROW_ORD_MAX
	jge	.ords_done
	mov	rax, [chkr_ors]
	mov	rcx, r14
	shr	rax, cl
	test	rax, 1
	jz	.ord_next
	mov	rdi, rbx
	mov	rsi, CHK_ROW_ORD
	call	ast_extra_push
	cmp	qword [chkr_st], 0
	jne	.o_started
	mov	[chkr_st], rax
  .o_started:
	mov	rdi, rbx
	mov	rsi, r14
	call	ast_extra_push
	inc	r13
  .ord_next:
	inc	r14
	jmp	.ordb
  .ords_done:

	lea	rdi, [rbx + Ast.rows]
	call	vec_push
	mov	rcx, [chkr_st]
	mov	[rax + AstRow.items], ecx
	mov	[rax + AstRow.count], r13d
	mov	rax, [rbx + Ast.rows.len]	; the new length == the new id
	return
endp

; ---- intern a row given its items ------------------------------------------
; `chkr_ip` points at `chkr_np` `ChkRowItem` records; it may be null when the
; count is 0. rax = the row id, >= 1 -- INCLUDING for the empty row.
;
;     chk_row_intern(tree, items, count) -> rax = row id
;
; Items may be in any order, may repeat, and may name a `potestas`; the row
; that comes back is the same one either way (spec §4.4's braced row, §4.5's
; `potestas Hospes`, typed-ast.md section 2.6's sort).
proc chk_row_intern, chkr_tree, chkr_ip, chkr_np
	uses	rbx, r12, r13
	locals
		slot chkr_sk, 16
	endl
	mov	qword [chkr_sk], 0
	mov	qword [chkr_sk + 8], 0
	mov	rbx, [chkr_tree]
	xor	r12, r12
	mov	r13, [chkr_np]
  .item:
	cmp	r12, r13
	jge	.done
	mov	rax, [chkr_ip]
	lea	rax, [rax + r12*8]
	mov	ecx, [rax + ChkRowItem.kind]
	mov	edx, [rax + ChkRowItem.id]
	mov	rdi, rbx
	mov	rsi, rcx
	lea	rcx, [chkr_sk]
	xor	r8, r8
	call	__chk_row_add
	inc	r12
	jmp	.item
  .done:
	mov	rdi, rbx
	mov	rsi, [chkr_sk]
	mov	rdx, [chkr_sk + 8]
	call	chk_row_of_masks
	return
endp

; ---- the empty row ---------------------------------------------------------
; rax = the id of `poscit {}` (spec §8.6), interning it on first use. NOT 0 --
; see rowdefs.inc. A bare `functio(A) -> B` is this row too (checker.md
; section 2.1 rule 3), so the types pass passes THIS to `ast_type_fn`'s `row`
; argument, never 0.
proc chk_row_empty, chkr_tree
	locals
	endl
	mov	rdi, [chkr_tree]
	xor	rsi, rsi
	xor	rdx, rdx
	call	chk_row_of_masks
	return
endp

; ---- accessors -------------------------------------------------------------
; rax = a pointer to the row's first `ChkRowItem` in `Ast.extra`, rdx = the
; item count. rax is 0 when the count is 0. THE POINTER IS VALID UNTIL THE
; NEXT PUSH on `Ast.extra` and no longer -- `vec_push` moves its backing
; block (ast/node.inc's accessors carry the same caveat for the same reason),
; and interning another row is such a push.
proc chk_row_items, chkr_tree, chkr_row
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkr_tree]
	mov	r12, [chkr_row]
	test	r12, r12
	jz	.none
	mov	rax, [rbx + Ast.rows.len]
	rassert	r12 le rax
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_row_at
	mov	r12d, [rax + AstRow.count]
	test	r12, r12
	jz	.none
	mov	esi, [rax + AstRow.items]
	lea	rdi, [rbx + Ast.extra]
	call	vec_get
	mov	rdx, r12
	return
  .none:
	xor	eax, eax
	xor	edx, edx
	return
endp

; rax = the number of items in the row (0 for row 0 and for the empty row).
proc chk_row_len, chkr_tree, chkr_row
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkr_tree]
	mov	r12, [chkr_row]
	test	r12, r12
	jz	.none
	mov	rax, [rbx + Ast.rows.len]
	rassert	r12 le rax
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_row_at
	mov	eax, [rax + AstRow.count]
	return
  .none:
	xor	eax, eax
	return
endp

; rax = the eleven-bit atom mask, bit `k - 1` for §4.6 ordinal `k`. This is
; the row as pass 3's fixpoint and IR 2.10's carrier list want it: the hidden
; arguments of a call are exactly these bits, in ascending order (checker.md
; section 2.8).
proc chk_row_atoms, chkr_tree, chkr_row
	locals
	endl
	mov	rdi, [chkr_tree]
	mov	rsi, [chkr_row]
	call	__chk_row_masks
	return
endp

; rax = the 64-bit ordinal mask. Non-zero after `chk_row_subst` is exactly
; the finding checker.md section 2.1's table calls "a substituted row that
; still carries an ordinal".
proc chk_row_ords, chkr_tree, chkr_row
	locals
	endl
	mov	rdi, [chkr_tree]
	mov	rsi, [chkr_row]
	call	__chk_row_masks
	mov	rax, rdx
	return
endp

; ---- set operations --------------------------------------------------------
; rax = the id of the union. Monotone, which is what makes pass 3's fixpoint
; iteration terminate (checker.md section 2.3).
proc chk_row_union, chkr_tree, chkr_ua, chkr_ub
	uses	rbx
	locals
		slot chkr_ga, dq
		slot chkr_go, dq
	endl
	mov	rbx, [chkr_tree]
	mov	rdi, rbx
	mov	rsi, [chkr_ua]
	call	__chk_row_masks
	mov	[chkr_ga], rax
	mov	[chkr_go], rdx
	mov	rdi, rbx
	mov	rsi, [chkr_ub]
	call	__chk_row_masks
	or	rax, [chkr_ga]
	or	rdx, [chkr_go]
	mov	rdi, rbx
	mov	rsi, rax
	call	chk_row_of_masks
	return
endp

; rax = 1 if every item of row `chkr_sa` is an item of row `chkr_sb`, else 0.
; THE CEILING TEST (spec §4.4): `EXS-E0510` is this returning 0 at an `Impl`
; head, an impl method, or a `sicut dyn` cast.
proc chk_row_subset, chkr_tree, chkr_sa, chkr_sb
	uses	rbx
	locals
		slot chkr_pa, dq
		slot chkr_po, dq
	endl
	mov	rbx, [chkr_tree]
	mov	rdi, rbx
	mov	rsi, [chkr_sa]
	call	__chk_row_masks
	mov	[chkr_pa], rax
	mov	[chkr_po], rdx
	mov	rdi, rbx
	mov	rsi, [chkr_sb]
	call	__chk_row_masks
	not	rax
	and	rax, [chkr_pa]
	jnz	.no
	not	rdx
	and	rdx, [chkr_po]
	jnz	.no
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; ---- positional substitution (spec §4.2) -----------------------------------
;     chk_row_subst(tree, callee_row, arg_rows, n) -> rax = row id
;
; `chkr_ar` points at `chkr_an` dwords, one per argument, each a row id -- the
; row in that argument's TYPE (`AstType.b` of its `fn` or `dyn`), or 0 if its
; type has none. Ordinal `k` in the callee's row is replaced by the ATOM items
; of `arg_rows[k]`.
;
; THREE CASES LEAVE THE ORDINAL IN PLACE, and all three are the same finding:
; the ordinal has no referent in the caller (spec §4.2's "name with no
; referent", checker.md section 2.1's table). They are
;
;   - `k >= n`            -- no such argument (an arity error, class D);
;   - `arg_rows[k] == 0`  -- the argument's type carries no row at all;
;   - `arg_rows[k]` itself carries an ordinal -- a higher-order function
;     passed to a `sicut` parameter, whose row names ITS OWN parameter, which
;     is not a parameter of anything at this call site.
;
; This routine does not diagnose: it has no sink, and the span belongs to the
; call. A caller checks `chk_row_ords` of the result and raises the class-L
; code (`EXS-E0423`) against the argument. Returning the row rather than
; failing is what lets the caller point at the right node.
proc chk_row_subst, chkr_tree, chkr_cr, chkr_ar, chkr_an
	uses	rbx, r12
	locals
		slot chkr_sa, dq
		slot chkr_so, dq
		slot chkr_co, dq
	endl
	mov	rbx, [chkr_tree]
	mov	rdi, rbx
	mov	rsi, [chkr_cr]
	call	__chk_row_masks
	mov	[chkr_sa], rax
	mov	[chkr_co], rdx
	mov	qword [chkr_so], 0
	xor	r12, r12
  .k:
	cmp	r12, CHK_ROW_ORD_MAX
	jge	.done
	mov	rax, [chkr_co]
	mov	rcx, r12
	shr	rax, cl
	test	rax, 1
	jz	.knext
	mov	rax, [chkr_an]
	cmp	r12, rax
	jge	.keep
	mov	rax, [chkr_ar]
	mov	ecx, [rax + r12*4]
	test	rcx, rcx
	jz	.keep
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_row_masks
	or	rax, [chkr_sa]
	mov	[chkr_sa], rax
	test	rdx, rdx
	jnz	.keep
	jmp	.knext
  .keep:
	mov	rcx, r12
	mov	rax, 1
	shl	rax, cl
	or	[chkr_so], rax
  .knext:
	inc	r12
	jmp	.k
  .done:
	mov	rdi, rbx
	mov	rsi, [chkr_sa]
	mov	rdx, [chkr_so]
	call	chk_row_of_masks
	return
endp

; ---- a written row, straight off the tree ----------------------------------
;     chk_row_of_node(tree, rownode) -> rax = row id
;
; `chkw_rn` is an `AST_ROW` node -- what spec §8.6's four `poscit` positions
; (a declaration's bare row, an implementation head, a function type, a `dyn`
; type) all build -- or 0, which is the EMPTY row, not "no row": a `Sig` with
; no `poscit` is `poscit {}` under spec §4.1 rule 6 and checker.md section
; 2.1 rule 3.
;
; Each `RowItem`'s `d` is what pass 1 resolved: an atom declaration, a
; `potestas` declaration, or -- when `aux` carries `AST_ROW_SICUT` -- a
; parameter ORDINAL (typed-ast.md section 2.6: "`sicut f` is an item whose
; `d` becomes the ordinal of parameter `f`, never its name").
;
; This is the routine the TYPES pass calls for `TyFn`, `TyDyn` and declared
; `Sig` rows; it exists so that no caller has to know the item encoding.
proc chk_row_of_node, chkr_tree, chkr_rn
	uses	rbx, r12, r13, r14
	locals
		slot chkr_buf, 8 * 96
		slot chkr_nn,  dq
	endl
	mov	rbx, [chkr_tree]
	mov	r12, [chkr_rn]
	xor	r13, r13			; items collected
	test	r12, r12
	jz	.intern
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	rassert	rcx eq AST_ROW
	mov	r14d, [rax + AstNode.a]		; the item list
	mov	ecx, [rax + AstNode.b]
	mov	[chkr_nn], rcx
  .item:
	cmp	qword [chkr_nn], 0
	jle	.intern
	mov	rax, 96
	rassert	r13 lt rax			; §4.6 has eleven atoms and
						; CHK_ROW_ORD_MAX ordinals
	mov	rdi, rbx
	mov	rsi, r14
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	rassert	rcx eq AST_ROWITEM
	movzx	ecx, word [rax + AstNode.aux]
	mov	eax, [rax + AstNode.d]
	rassert	rax ge 0
	lea	rdx, [chkr_buf]
	lea	rdx, [rdx + r13*8]
	mov	[rdx + ChkRowItem.id], eax
	test	ecx, AST_ROW_SICUT
	jnz	.sicut
	mov	dword [rdx + ChkRowItem.kind], CHK_ROW_ATOM
	jmp	.item_next
  .sicut:
	mov	dword [rdx + ChkRowItem.kind], CHK_ROW_ORD
  .item_next:
	inc	r13
	inc	r14
	dec	qword [chkr_nn]
	jmp	.item
  .intern:
	mov	rdi, rbx
	lea	rsi, [chkr_buf]
	mov	rdx, r13
	call	chk_row_intern
	return
endp
