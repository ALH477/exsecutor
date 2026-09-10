; compiler/x86_64/ast/verify.inc
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
; THE VERIFIER. `docs/design/typed-ast.md` section 3's Stage 1 list, plus what
; the operand-role table (ast/kinds.inc) makes checkable that the prose list
; does not name.
;
; EVERY FAILURE IS AN `rassert`, NEVER AN `EXS-E` CODE. A malformed tree is a
; compiler bug, not a fact about the program being compiled, and
; docs/asm-conventions.md 1.3 is explicit that routing a violated internal
; contract through `CF` invites a caller to swallow it. So this file registers
; no diagnostic and invents no code (CLAUDE.md, "Error codes are permanent").
;
; WHAT IT CHECKS (Stage 1):
;   - every operand obeys its role: a `NODE`/`NODEQ` slot holds a child whose
;     index is strictly less than the parent's (POSTORDER), a `NONE` slot
;     holds 0, a `DECL` slot names an existing declaration;
;   - every list slot is followed by a `COUNT` slot, its offset is inside
;     `Ast.extra`, offset and count are zero together, and every element is a
;     node id smaller than the node holding the list;
;   - `RowItem.a` is a `Path` node unless `aux` says `sicut`, in which case it
;     is an interner id and nothing here can say more (ast/kinds.inc's note 6);
;   - a block's declaration range is inside `Ast.decls`, and ranges are
;     PROPERLY NESTED -- a pair either nests or is disjoint, never overlaps;
;   - every `AstDecl.parent` precedes it, and every `AstDecl.node` points at a
;     node whose `d` slot is that declaration;
;   - spec §4.6's capability atoms, if the tree has them, are ALL ELEVEN, the
;     last eleven declarations, distinctly named, and have neither a node nor
;     a parent nor a flag (`__ast_v_caps`);
;   - every interned `cap` type names one of those declarations and carries
;     no other field (`__ast_v_types`);
;   - every `Externus.aux` is one of spec §5.3's closed ABI enumeration
;     values (0..3, `AST_ABI_*`);
;   - the root, if set, is a `Module`.
;
; WHAT IT DOES NOT CHECK, AND WHY -- both are typed-ast.md section 3 clauses
; this module structurally cannot reach:
;   - "every `Error` node has a diagnostic against its span". The emitted
;     diagnostics are the driver's list, not a field of the tree; an AST
;     handed to this routine has no way to see them. It belongs to whoever
;     owns both, and is `[UNIMPLEMENTED]` here rather than silently dropped.
;   - the Stage 2 list ("`ty` != 0 and never 1 on every expression and type
;     node", "`d` != 0 on every `Seg`, `Member`, `RowItem`", "every `RowItem`
;     ordinal < its `Sig`'s parameter count", "`red` only on `Contrahe`
;     decls", "every `transitus` field's `Layout.order` != `nativus`"). No
;     checker exists, so every one of them would be prose about unwritten
;     code -- the thing CLAUDE.md's evidence discipline forbids. Two of the
;     five (the ordinal bound, the `transitus` rule) also need context a
;     per-node walk does not have: the enclosing `Sig`, and which declaration
;     a layout entry belongs to. `[UNIMPLEMENTED]`.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3 (the orders this checks are
; index orders), §8.6.
; -----------------------------------------------------------------------------

include 'build.inc'

; ---- one operand slot ------------------------------------------------------
proc __ast_v_slot, tree, id, slotno
	uses	rbx, r12, r13, r14
	locals
		slot vs_role,  dq
		slot vs_count, dq
		slot vs_off,   dq
	endl
	mov	rbx, [tree]
	mov	r12, [id]
	mov	r13, [slotno]

	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	movzx	r14d, word [rax + AstNode.kind]
	lea	rcx, [rax + AstNode.a]
	mov	eax, [rcx + r13*4]
	mov	[vs_off], rax		; the slot's raw value

	; Everything a constructor could have checked, re-checked here: a
	; builder that skipped `ast_node_cd` left `c`/`d` at 0, and this is
	; where that is caught.
	mov	rdi, rbx
	mov	rsi, r14
	mov	rdx, r12
	mov	rcx, r13
	mov	r8, [vs_off]
	call	__ast_slot_check

	mov	rdi, r14
	mov	rsi, r13
	call	ast_role_of
	mov	[vs_role], rax
	cmp	eax, AST_R_ROWI
	je	.rowitem
	cmp	eax, AST_R_LIST
	je	.list
	cmp	eax, AST_R_LIST2
	je	.list
	return

  .list:
	; A list slot names the NEXT slot as its count. That pairing is a
	; property of ast_roles, so it is asserted rather than assumed -- an
	; edit to that table that broke it would otherwise show up as a
	; nonsense range a long way from the table.
	rassert	r13 le 2
	mov	rdi, r14
	lea	rsi, [r13 + 1]
	call	ast_role_of
	rassert	rax eq AST_R_COUNT

	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	lea	rcx, [rax + AstNode.a]
	lea	rdx, [r13 + 1]
	mov	eax, [rcx + rdx*4]
	mov	[vs_count], rax
	mov	rax, [vs_role]
	cmp	rax, AST_R_LIST2
	jne	.have_n
	shl	qword [vs_count], 1	; `Si` stores (cond, block) PAIRS
  .have_n:
	mov	rax, [vs_off]
	test	rax, rax
	jnz	.nonempty
	mov	rax, [vs_count]		; no offset means no elements, and
	rassert	rax eq 0		; vice versa -- the two are one fact
	return
  .nonempty:
	mov	rax, [vs_count]
	rassert	rax ge 1
	mov	rcx, [vs_off]
	add	rax, rcx		; one past the last element
	mov	rdx, [rbx + Ast.extra.len]
	rassert	rax le rdx

	mov	r13, [vs_off]
	mov	r14, [vs_count]
  .elem:
	test	r14, r14
	jz	.done
	mov	rdi, rbx
	mov	rsi, r13
	call	ast_extra_at
	rassert	rax ge 1		; a list element is always a node id
	mov	rcx, r12
	rassert	rax lt rcx		; ... and postorder puts it before us
	inc	r13
	dec	r14
	jmp	.elem
  .done:
	return

  .rowitem:
	; `RowItem.a` is a `Path` node, OR -- with AST_ROW_SICUT -- the
	; interner id of a parameter name. Only the first case is checkable.
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.aux]
	test	ecx, AST_ROW_SICUT
	jnz	.rowitem_name
	mov	rax, [vs_off]
	rassert	rax ge 1
	mov	rcx, r12
	rassert	rax lt rcx
	mov	rdi, rbx
	mov	rsi, [vs_off]
	call	ast_node_at
	movzx	eax, word [rax + AstNode.kind]
	rassert	rax eq AST_PATH
  .rowitem_name:
	return
endp

; ---- one block's declaration range ----------------------------------------
; `[Block.c, Block.c + Block.aux)`. The empty case is canonicalised to
; `c == 0`: with `aux == 0` the offset is unread, so leaving whatever the
; builder happened to have there would make two identical trees dump
; differently -- a determinism hazard for no gain (spec §9.3).
proc __ast_v_block, tree, id
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	mov	rsi, [id]
	call	ast_node_at
	movzx	r12d, word [rax + AstNode.aux]	; declaration count
	mov	r13d, [rax + AstNode.c]		; first declaration
	test	r12, r12
	jnz	.nonempty
	rassert	r13 eq 0
	return
  .nonempty:
	rassert	r13 ge 1
	mov	rax, r13
	add	rax, r12			; one past the last
	mov	rcx, [rbx + Ast.decls.len]
	inc	rcx
	rassert	rax le rcx
	return
endp

; ---- block ranges nest -----------------------------------------------------
; A stack walk in node-index order. Postorder puts every child block before
; its parent, so ranges arrive smallest-first: pop every range the incoming
; one CONTAINS, then require whatever is left on top to be DISJOINT from it.
; Anything else is an overlap, which no scope structure can produce.
;
; The stack is `Ast.stage`, the builder's own staging vec, borrowed and
; restored -- verification runs when no list is half-built, and a second
; stack would be a second thing to size against a fixed arena (typed-ast.md
; section 5, H1).
proc __ast_v_nest, tree
	uses	rbx, r12, r13, r14
	locals
		slot vn_base, dq
		slot vn_s,    dq
		slot vn_e,    dq
	endl
	mov	rbx, [tree]
	mov	rax, [rbx + Ast.stage.len]
	mov	[vn_base], rax
	mov	r12, 1
  .node:
	mov	rax, [rbx + Ast.nodes.len]
	cmp	r12, rax
	jg	.done
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_BLOCK
	jne	.next
	movzx	r13d, word [rax + AstNode.aux]
	test	r13, r13
	jz	.next
	mov	r14d, [rax + AstNode.c]
	mov	[vn_s], r14
	add	r14, r13
	mov	[vn_e], r14		; the incoming range [s, e)

  .pop:
	mov	rax, [rbx + Ast.stage.len]
	mov	rcx, [vn_base]
	cmp	rax, rcx
	jle	.push
	; top of stack is (start, end) as two words, end on top
	lea	rdi, [rbx + Ast.stage]
	lea	rsi, [rax - 1]
	call	vec_get
	mov	r14d, [rax]		; te
	lea	rdi, [rbx + Ast.stage]
	mov	rax, [rbx + Ast.stage.len]
	lea	rsi, [rax - 2]
	call	vec_get
	mov	r13d, [rax]		; ts
	mov	rax, [vn_s]
	cmp	r13, rax
	jl	.not_contained
	mov	rax, [vn_e]
	cmp	r14, rax
	jg	.not_contained
	mov	rax, [rbx + Ast.stage.len]
	sub	rax, 2
	mov	[rbx + Ast.stage.len], rax
	jmp	.pop
  .not_contained:
	; Must be disjoint: `te <= s` or `e <= ts`. `rassert` takes exactly one
	; `lhs op rhs`, so the disjunction is computed into rax first and the
	; assertion is on the result. The computation stays in a RELEASE build
	; and the check does not -- a few wasted compares, and the alternative
	; (a raw `ud2` here) would be a trap that RELEASE could not erase while
	; erasing every other check in this file.
	xor	eax, eax
	mov	rcx, [vn_s]
	cmp	r14, rcx
	jle	.disjoint
	mov	rcx, [vn_e]
	cmp	rcx, r13
	jle	.disjoint
	jmp	.checked
  .disjoint:
	mov	eax, 1
  .checked:
	rassert	rax eq 1
  .push:
	mov	rdi, rbx
	mov	rsi, [vn_s]
	call	ast_list_push
	mov	rdi, rbx
	mov	rsi, [vn_e]
	call	ast_list_push
  .next:
	inc	r12
	jmp	.node
  .done:
	mov	rax, [vn_base]
	mov	[rbx + Ast.stage.len], rax
	return
endp

; ---- declarations ----------------------------------------------------------
proc __ast_v_decls, tree
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [tree]
	mov	r12, 1
  .loop:
	mov	rax, [rbx + Ast.decls.len]
	cmp	r12, rax
	jg	.done
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.parent]
	mov	rdx, r12
	rassert	rcx lt rdx		; 0, or an earlier declaration --
					; source order, so a parent is always
					; already pushed
	mov	r13d, [rax + AstDecl.node]
	test	r13, r13
	jz	.next
	mov	rax, [rbx + Ast.nodes.len]
	rassert	r13 le rax
	mov	rdi, rbx
	mov	rsi, r13
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	mov	r13d, [rax + AstNode.d]
	mov	rdi, rcx
	mov	rsi, 3
	call	ast_role_of
	rassert	rax eq AST_R_DECL	; the node it names must HAVE a
					; declaration slot at all
	mov	rax, r13
	mov	rcx, r12
	rassert	rax eq rcx		; ... and it must point back here
  .next:
	inc	r12
	jmp	.loop
  .done:
	return
endp

; ---- the eleven capability atoms -------------------------------------------
; What a tree carrying spec §4.6's atoms must look like, checked as a
; STRUCTURE rather than trusted from whoever built it -- `ast_cap_push`
; (ast/build.inc) is one way in, `ast_load` reading eleven `CapAtom` lines is
; the other, and only this routine sees both.
;
;   - an atom declaration has NO introducing node (`node` 0), NO enclosing
;     declaration (`parent` 0), NO flags, and a real name;
;   - the atoms are ALL ELEVEN or NONE -- ten is not a smaller capability set,
;     it is a tree in which some §4.6 ordinal silently names the wrong atom;
;   - they are the LAST eleven declarations, which is what makes
;     `ast_cap_base` a bounds check instead of a scan and what makes
;     checker.md section 2.2's `N + k` true;
;   - their names are DISTINCT. Two atoms sharing a name id would make
;     `ast_cap_of_name` return whichever came first and lose the other atom
;     entirely.
;
; WHAT IT DELIBERATELY DOES NOT CHECK: that atom `k`'s name is the `k`-th
; entry of `ast_cap_names`. That would mean interning eleven strings from
; inside a verifier -- a side effect on the caller's interner, and a
; guaranteed trap on any hand-built tree whose interner does not happen to
; hold them (`ast_load` restores name IDS from a dump, never bytes). The
; pool-to-decl correspondence is `ast_cap_push`'s own post-condition and is
; asserted, atom by atom, in tests/unit/ast_cap_atoms.asm.
proc __ast_v_caps, tree
	uses	rbx, r12, r13, r14
	locals
		slot vc_first, dq
		slot vc_n,     dq
	endl
	mov	rbx, [tree]
	mov	qword [vc_first], 0
	mov	qword [vc_n], 0
	mov	r12, 1
  .scan:
	mov	rax, [rbx + Ast.decls.len]
	cmp	r12, rax
	jg	.scanned
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_CAPATOM
	jne	.next
	mov	ecx, [rax + AstDecl.parent]
	rassert	rcx eq 0
	mov	ecx, [rax + AstDecl.node]
	rassert	rcx eq 0
	movzx	ecx, byte [rax + AstDecl.flags]
	rassert	rcx eq 0
	mov	ecx, [rax + AstDecl.name]
	rassert	rcx ge 1
	inc	qword [vc_n]
	mov	rax, [vc_first]
	test	rax, rax
	jnz	.next
	mov	[vc_first], r12
  .next:
	inc	r12
	jmp	.scan
  .scanned:
	mov	rax, [vc_n]
	test	rax, rax
	jz	.done			; a tree with no atoms is well-formed
	mov	rcx, AST_CAP_COUNT
	rassert	rax eq rcx		; all eleven, or none
	mov	rax, [rbx + Ast.decls.len]
	sub	rax, AST_CAP_COUNT
	inc	rax
	mov	rcx, [vc_first]
	rassert	rcx eq rax		; ... and they are the last eleven,
					; hence consecutive

	; distinct names: eleven of them, so the quadratic scan is 55 integer
	; comparisons and needs no map (and could not use one: a map here
	; would be a second thing to size against a fixed arena).
	mov	r12, [vc_first]
	mov	r13, 0
  .outer:
	mov	rcx, AST_CAP_COUNT
	cmp	r13, rcx
	jge	.done
	mov	rdi, rbx
	lea	rsi, [r12 + r13]
	call	ast_decl_at
	mov	r14d, [rax + AstDecl.name]
	lea	rax, [r13 + 1]
	mov	[vc_n], rax
  .inner:
	mov	rcx, AST_CAP_COUNT
	mov	rax, [vc_n]
	cmp	rax, rcx
	jge	.inner_done
	mov	rdi, rbx
	mov	rax, [vc_n]
	lea	rsi, [r12 + rax]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.name]
	rassert	rcx ne r14
	inc	qword [vc_n]
	jmp	.inner
  .inner_done:
	inc	r13
	jmp	.outer
  .done:
	return
endp

; ---- the interned type table -----------------------------------------------
; Only what a NOMINAL type kind makes checkable per entry: `AST_TY_CAP`'s `a`
; is a capability atom's declaration (ast/kinds.inc), and an atom type has no
; other field -- no width, no row, no sign, no byte order, no placement. Two
; `Mundus` annotations therefore intern to ONE id, which is the whole reason
; the checker can compare capability types by id (checker.md section 1, rule
; 5: "type compatibility is equality of interned type ids").
;
; The other nominal kinds (`struct` `alias` `iface` `param` `brand`) are not
; checked here: `brand.a` is admitted to be 0 (checker.md section 2.6's
; anonymous brand) and the other three point at declarations no Stage 1 tree
; interns yet, so a rule about them would be prose about unwritten code.
proc __ast_v_types, tree
	uses	rbx, r12
	locals
	endl
	mov	rbx, [tree]
	mov	r12, 1
  .loop:
	mov	rax, [rbx + Ast.types.len]
	cmp	r12, rax
	jg	.done
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_CAP
	jne	.next
	mov	ecx, [rax + AstType.a]
	rassert	rcx ge 1
	mov	rdx, [rbx + Ast.decls.len]
	rassert	rcx le rdx
	mov	edx, [rax + AstType.width]
	rassert	rdx eq 0
	mov	edx, [rax + AstType.b]
	rassert	rdx eq 0
	movzx	edx, byte [rax + AstType.sign]
	rassert	rdx eq 0
	movzx	edx, byte [rax + AstType.order]
	rassert	rdx eq 0
	movzx	edx, byte [rax + AstType.place]
	rassert	rdx eq 0
	mov	rdi, rbx
	mov	rsi, rcx
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	rassert	rcx eq AST_D_CAPATOM
  .next:
	inc	r12
	jmp	.loop
  .done:
	return
endp

; ---- the whole tree --------------------------------------------------------
proc ast_verify, tree
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [tree]
	mov	r12, 1
  .node:
	mov	rax, [rbx + Ast.nodes.len]
	cmp	r12, rax
	jg	.nodes_done
	xor	r13, r13
  .slot:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__ast_v_slot
	inc	r13
	cmp	r13, 4
	jl	.slot
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_EXTERNUS
	jne	.not_externus
	; Spec §5.3's ABI enumeration is CLOSED, so `Externus.aux` is 0..3 and
	; nothing else. This is the check that used to be impossible: while the
	; slot held an interner id, no bound existed short of 65535, which is
	; why ast/from_cst.inc had to `rassert` at the one site that wrote it.
	movzx	ecx, word [rax + AstNode.aux]
	mov	rdx, AST_ABI_COUNT
	rassert	rcx le rdx
  .not_externus:
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_BLOCK
	jne	.next
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_v_block
  .next:
	inc	r12
	jmp	.node
  .nodes_done:
	mov	rdi, rbx
	call	__ast_v_nest
	mov	rdi, rbx
	call	__ast_v_decls
	mov	rdi, rbx
	call	__ast_v_caps
	mov	rdi, rbx
	call	__ast_v_types

	mov	eax, [rbx + Ast.root]
	test	eax, eax
	jz	.no_root
	mov	r12, rax
	mov	rax, [rbx + Ast.nodes.len]
	rassert	r12 le rax
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	movzx	eax, word [rax + AstNode.kind]
	rassert	rax eq AST_MODULE
  .no_root:
	return
endp

; ---- the Stage 1 flavour ---------------------------------------------------
; Everything above, plus the two facts that are true exactly while no checker
; has run: every type slot is empty, and no side table has been allocated.
; typed-ast.md section 2.2 -- "`Node.ty` is 0 after Stage 1 EVERYWHERE".
proc ast_verify_stage1, tree
	uses	rbx, r12
	locals
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	call	ast_verify
	mov	eax, [rbx + Ast.sides]
	rassert	rax eq 0
	mov	r12, 1
  .loop:
	mov	rax, [rbx + Ast.nodes.len]
	cmp	r12, rax
	jg	.done
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	mov	eax, [rax + AstNode.ty]
	rassert	rax eq 0
	inc	r12
	jmp	.loop
  .done:
	return
endp
