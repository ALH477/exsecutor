; compiler/x86_64/ast/build.inc
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
; THE BUILDER. Constructors for nodes, declarations, `extra` lists and the
; Stage 2 side tables -- `docs/design/typed-ast.md` sections 2.1, 2.2 and 2.4.
;
; EVERY CONSTRUCTOR CHECKS WHAT IT CAN, WHERE IT IS CALLED. `ast_verify` (a
; separate pass) can say "this tree is malformed"; only the constructor can
; say WHICH CALL made it so, because it is the only place the caller's
; instruction pointer is still on the stack. So `ast_node` range-checks the
; two operands it is handed against `ast_roles` at the moment it is handed
; them: a child id that is not smaller than the node being built is a
; postorder violation and traps HERE, not two thousand nodes later.
;
; `c` AND `d` ARE SET AFTERWARDS, by `ast_node_cd`, because six is the
; dialect's argument limit (docs/asm-conventions.md 3.1) and a node needs
; kind, aux, four operands and a span -- seven. A kind whose `c` or `d` is
; AST_R_NODEQ therefore has a window, between `ast_node` and `ast_node_cd`,
; in which it is incomplete; `ast_verify` is what closes it, and a builder
; that forgets the second call is caught there rather than never.
;
; THERE IS NO SECOND ARENA. typed-ast.md section 2.1 puts builder scratch
; -- child lists collected before one append to `extra` -- in a second arena
; "reset per item". That does not work and is not needed. It does not work
; because the scratch structure is a `Vec`, and `arena_reset` invalidates the
; backing block a live `Vec` still points at (rt/arena.inc's header states
; this rule for the interner and it applies identically here). It is not
; needed because the lists nest: a postorder walk finishes every inner list
; before the outer one continues, so one `Vec` used with STACK discipline --
; `ast_list_mark`, pushes, `ast_list_emit` truncating back to the mark -- has
; exactly the reuse the reset was for, with no second region to size and no
; window in which a pointer is stale. Reported as a finding against that
; section.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3, §8.6, §9.3.
; -----------------------------------------------------------------------------

include 'types.inc'

; ---- initialisation --------------------------------------------------------
; `arena` backs the whole tree and MUST NOT be reset while it is live.
; `names` is the interner identifiers and literal text are already in --
; shared with the lexer, not a second one, so an id means the same thing on
; both sides of the CST boundary.
proc ast_init, tree, arena, names
	uses	rbx, r12
	locals
		slot t0, 16
	endl
	mov	rbx, [tree]

	; Zero the record first: `root`, `numeri`, `sides` and the padding all
	; have to start at 0 and none of them is written below.
	mov	rdi, rbx
	mov	rcx, sizeof.Ast
	xor	eax, eax
	cld
	rep	stosb

	mov	rbx, [tree]
	mov	rax, [arena]
	mov	[rbx + Ast.arena], rax
	mov	rax, [names]
	mov	[rbx + Ast.names], rax

	lea	rdi, [rbx + Ast.nodes]
	mov	rsi, [arena]
	mov	rdx, sizeof.AstNode
	mov	rcx, AST_NODES_CAP0
	call	vec_init
	lea	rdi, [rbx + Ast.decls]
	mov	rsi, [arena]
	mov	rdx, sizeof.AstDecl
	mov	rcx, AST_DECLS_CAP0
	call	vec_init
	lea	rdi, [rbx + Ast.extra]
	mov	rsi, [arena]
	mov	rdx, 4
	mov	rcx, AST_EXTRA_CAP0
	call	vec_init
	lea	rdi, [rbx + Ast.types]
	mov	rsi, [arena]
	mov	rdx, sizeof.AstType
	mov	rcx, AST_TYPES_CAP0
	call	vec_init
	lea	rdi, [rbx + Ast.rows]
	mov	rsi, [arena]
	mov	rdx, sizeof.AstRow
	mov	rcx, AST_ROWS_CAP0
	call	vec_init
	lea	rdi, [rbx + Ast.stage]
	mov	rsi, [arena]
	mov	rdx, 4
	mov	rcx, AST_STAGE_CAP0
	call	vec_init

	lea	rdi, [rbx + Ast.typemap]
	mov	rsi, [arena]
	mov	rdx, AST_TYPE_BUCKETS
	call	map_init

	; `Ast.extra` index 0 is a DEAD SENTINEL, never read, so that a list
	; offset of 0 means "no list" the way an id of 0 means "no node".
	; Without it a list starting at the very first word would be
	; indistinguishable from an absent one.
	lea	rdi, [rbx + Ast.extra]
	call	vec_push
	mov	dword [rax], 0

	; Type id 1 is the error type, and it is id 1 because it is interned
	; FIRST -- typed-ast.md section 2.2. Asserted, not assumed: if
	; anything ever interns ahead of this, cascade suppression silently
	; starts suppressing the wrong type.
	lea	rdi, [t0]
	call	ast_type_clear
	mov	byte [t0 + AstType.kind], AST_TY_ERROR
	mov	rdi, [tree]
	lea	rsi, [t0]
	call	ast_type_intern
	rassert	rax eq 1
	return
endp

; ---- operand checking ------------------------------------------------------
; What `ast_roles` lets a constructor check about one slot, knowing only the
; node being built. Range checks that need TWO slots (a list offset against
; its count, which may live in a slot not yet written) belong to ast/verify.inc.
proc __ast_slot_check, tree, kind, id, slotno, value
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [tree]
	mov	r12, [id]
	mov	r13, [value]
	mov	rdi, [kind]
	mov	rsi, [slotno]
	call	ast_role_of
	cmp	eax, AST_R_NONE
	je	.must_be_zero
	cmp	eax, AST_R_NODE
	je	.child
	cmp	eax, AST_R_NODEQ
	je	.child_req
	cmp	eax, AST_R_LIST
	je	.list
	cmp	eax, AST_R_LIST2
	je	.list
	cmp	eax, AST_R_DECL
	je	.decl
	return				; COUNT, NAME, ANY, ROWI: nothing this
					; routine can say
  .must_be_zero:
	rassert	r13 eq 0
	return
  .child_req:
	rassert	r13 ge 1
  .child:
	; POSTORDER: a child's index is strictly less than its parent's.
	rassert	r13 lt r12
	return
  .list:
	test	r13, r13
	jz	.list_empty		; 0 = no list; the count must be 0 too,
					; which ast/verify.inc checks
	mov	rax, [rbx + Ast.extra.len]
	rassert	r13 lt rax		; a live offset is >= 1 and inside the
					; pool -- index 0 is the sentinel
	rassert	r13 ge 1
  .list_empty:
	return
  .decl:
	mov	rax, [rbx + Ast.decls.len]
	rassert	r13 le rax
	return
endp

; ---- nodes -----------------------------------------------------------------
; rax = the new node's 1-based id. `span` points at an `rt/span.inc` `Span`,
; copied in by value: spec §8.3 makes the span non-optional and
; non-retrofittable, so there is no constructor that omits it.
proc ast_node, tree, kind, aux, a, b, span
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [tree]
	mov	rax, [kind]
	rassert	rax ge 1
	mov	rcx, AST_KIND_MAX
	rassert	rax le rcx

	lea	rdi, [rbx + Ast.nodes]
	call	vec_push
	mov	r12, rax

	mov	rax, [kind]
	mov	[r12 + AstNode.kind], ax
	mov	rax, [aux]
	mov	[r12 + AstNode.aux], ax
	mov	dword [r12 + AstNode.ty], 0
	mov	rax, [a]
	mov	[r12 + AstNode.a], eax
	mov	rax, [b]
	mov	[r12 + AstNode.b], eax
	mov	dword [r12 + AstNode.c], 0
	mov	dword [r12 + AstNode.d], 0
	mov	rsi, [span]
	mov	eax, [rsi + Span.file_id]
	mov	[r12 + AstNode.span.file_id], eax
	mov	eax, [rsi + Span.start]
	mov	[r12 + AstNode.span.start], eax
	mov	eax, [rsi + Span.len]
	mov	[r12 + AstNode.span.len], eax

	mov	r13, [rbx + Ast.nodes.len]	; new length == the new id
	mov	rdi, rbx
	mov	rsi, [kind]
	mov	rdx, r13
	mov	rcx, 0
	mov	r8, [a]
	call	__ast_slot_check
	mov	rdi, rbx
	mov	rsi, [kind]
	mov	rdx, r13
	mov	rcx, 1
	mov	r8, [b]
	call	__ast_slot_check
	mov	rax, r13
	return
endp

; The other half of a node, for the kinds that use more than two operands.
; Checked the same way `a` and `b` were.
proc ast_node_cd, tree, id, c, d
	uses	rbx, r12
	locals
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	mov	rsi, [id]
	call	ast_node_at
	mov	r12, rax
	movzx	r12d, word [r12 + AstNode.kind]

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [id]
	mov	rcx, 2
	mov	r8, [c]
	call	__ast_slot_check
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [id]
	mov	rcx, 3
	mov	r8, [d]
	call	__ast_slot_check

	mov	rdi, rbx
	mov	rsi, [id]
	call	ast_node_at
	mov	rcx, [c]
	mov	[rax + AstNode.c], ecx
	mov	rcx, [d]
	mov	[rax + AstNode.d], ecx
	return
endp

; Stage 2's annotation. Separate from `ast_node` because Stage 1 writes 0
; here on every node and only a checker may write anything else -- the whole
; content of typed-ast.md section 2.2's "slots present, empty, filled in
; place".
proc ast_node_ty, tree, id, ty
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [id]
	call	ast_node_at
	mov	rcx, [ty]
	mov	[rax + AstNode.ty], ecx
	return
endp

; OR bits into a node's `aux` after the fact. `ast_node` takes `aux` as an
; operand, but two facts about a declaration arrive from OUTSIDE the node that
; carries them -- `publica` and an annotation are children of `CST_ITEM`, not
; of the `FunctionDecl` under it (spec §8.6: `Item ::= Annotation* ['publica']
; ItemBody`). The walker builds the body first and folds them in here rather
; than threading two flags through every item constructor.
proc ast_node_aux_or, tree, id, bits
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [id]
	call	ast_node_at
	mov	rcx, [bits]
	or	word [rax + AstNode.aux], cx
	return
endp

proc ast_set_root, tree, id
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [id]
	call	ast_node_at		; range-checks it
	mov	rdi, [tree]
	mov	rax, [id]
	mov	[rdi + Ast.root], eax
	return
endp

; ---- declarations ----------------------------------------------------------
; Pushed in SOURCE order. `parent` is the enclosing declaration (0 at module
; level) and must already exist, which -- with source order -- is what makes
; a block's declarations a contiguous range with nested blocks owning
; sub-ranges (typed-ast.md section 2.4). `node` is filled later by
; `ast_decl_node`: in postorder the introducing node does not exist yet when
; the name is seen.
proc ast_decl, tree, kind, flags, nameid, parent, span
	uses	rbx, r12
	locals
	endl
	mov	rbx, [tree]
	mov	rax, [kind]
	rassert	rax ge 1
	mov	rcx, AST_DECL_MAX
	rassert	rax le rcx
	mov	rax, [parent]
	mov	rcx, [rbx + Ast.decls.len]
	rassert	rax le rcx		; 0, or an already-pushed declaration

	lea	rdi, [rbx + Ast.decls]
	call	vec_push
	mov	r12, rax
	mov	rax, [kind]
	mov	[r12 + AstDecl.kind], al
	mov	rax, [flags]
	mov	[r12 + AstDecl.flags], al
	mov	word [r12 + AstDecl.aux], 0
	mov	rax, [nameid]
	mov	[r12 + AstDecl.name], eax
	mov	dword [r12 + AstDecl.node], 0
	mov	dword [r12 + AstDecl.ty], 0
	mov	rax, [parent]
	mov	[r12 + AstDecl.parent], eax
	mov	rsi, [span]
	mov	eax, [rsi + Span.file_id]
	mov	[r12 + AstDecl.span.file_id], eax
	mov	eax, [rsi + Span.start]
	mov	[r12 + AstDecl.span.start], eax
	mov	eax, [rsi + Span.len]
	mov	[r12 + AstDecl.span.len], eax

	mov	rax, [rbx + Ast.decls.len]
	return
endp

proc ast_decl_node, tree, id, node
	uses	rbx
	locals
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	mov	rsi, [node]
	call	ast_node_at		; range-checks it
	mov	rdi, rbx
	mov	rsi, [id]
	call	ast_decl_at
	mov	rcx, [node]
	mov	[rax + AstDecl.node], ecx
	return
endp

proc ast_decl_ty, tree, id, ty
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [id]
	call	ast_decl_at
	mov	rcx, [ty]
	mov	[rax + AstDecl.ty], ecx
	return
endp

proc ast_decl_flag, tree, id, flags
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [id]
	call	ast_decl_at
	mov	rcx, [flags]
	or	byte [rax + AstDecl.flags], cl
	return
endp

; ---- the eleven capability atoms -------------------------------------------
; `docs/design/checker.md` section 2.2's PASS 0, moved DOWN into Stage 1.
;
; Spec §4.6's atoms are "identifiers in the capability namespace" (§8.4 tier
; 3) with no declaration site anywhere in §8.6's grammar -- and yet `Seg.d`,
; `RowItem.d` and a `sub`'s `Decl.ty` all have to name one, and every one of
; those slots holds a DECLARATION id (typed-ast.md sections 2.3, 2.6). So
; eleven declarations have to exist that no source text produced.
;
; WHO PUSHES THEM, and why it is this file rather than the checker. That
; document says "the checker pushes one `Decl` per atom at the end of
; `Ast.decls`"; the ids it specifies -- `N + k` for a module of `N` source
; declarations and §4.6 ordinal `k` -- are the same either way, because this
; runs at the END of `ast_from_cst`'s walk, after every source declaration
; and before anything else can push one. Three reasons to do it here:
;
;   1. THE TREE BECOMES SELF-DESCRIBING. `ast_dump` prints the eleven
;      `CapAtom` lines, `ast_load` reads them back, and `ast_equal` compares
;      them -- so what a checker fixture resolves against is visible in the
;      dump of `exsc aedifica --emitte ast` today, before any checker exists.
;      Pushed by the checker instead, the atoms would be invisible to every
;      tool in this repository that can currently look at a tree.
;   2. ONE PLACE OWNS THE ORDER. The §4.6 ordinal is load-bearing in three
;      separate places in that design (frame slots, row sorting, decl ids);
;      keeping the pool and the push in `ast/` means a checker never restates
;      it, and `ast_verify` can check the tree it is handed rather than
;      trusting the pass that built it.
;   3. IT COSTS ELEVEN DECLARATIONS. Not per function -- per module.
;
; The cost is the one this routine's caller pays and a reader should know:
; every tree built from a CST now has eleven more declarations than its
; source has names, and `ast_decl_count` says so (tests/unit/ast_from_cst.asm
; check 3 went from 2 to 13). A tree built by hand through `ast_load` has
; them only if its text does; `ast_cap_base` is how a later pass asks.
;
; NO SPAN. An atom is not written anywhere, so its `Span` is all zeroes --
; file 0, offset 0, length 0. A diagnostic about a capability points at the
; USE (checker.md section 2.4 puts every capability code's span on a node in
; the source), never at the atom, so there is nothing this span could
; usefully be. Zero is also what makes two dumps of the same module compare
; equal regardless of which file id the module happened to get.
;
; rax = the declaration id of `Mundus`, so atom `k` is `rax + k - 1`.
proc ast_cap_push, tree
	uses	rbx, r12, r13, r14
	locals
		slot cp_span, sizeof.Span
		slot cp_base, dq
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	call	ast_cap_base
	rassert	rax eq 0		; once per tree

	lea	rdi, [cp_span]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	mov	rax, [rbx + Ast.decls.len]
	inc	rax
	mov	[cp_base], rax		; `Mundus`'s id, `N + 1`
	mov	r12, 1
  .atom:
	mov	rcx, AST_CAP_COUNT
	cmp	r12, rcx
	jg	.done
	lea	rdi, [ast_cap_names]
	mov	rsi, r12
	call	ast_name_at		; rax = bytes, rdx = length
	mov	r13, rax
	mov	r14, rdx
	mov	rdi, [rbx + Ast.names]
	mov	rsi, r13
	mov	rdx, r14
	call	intern_id
	mov	r13, rax		; the atom's name id

	mov	rdi, rbx
	mov	rsi, AST_D_CAPATOM
	mov	rdx, 0			; no flag applies to an atom
	mov	rcx, r13
	mov	r8, 0			; module level: `parent` 0
	lea	r9, [cp_span]
	call	ast_decl
	mov	rcx, [cp_base]
	add	rcx, r12
	dec	rcx
	rassert	rax eq rcx		; ids are consecutive from the base --
					; checker.md section 2.2's `N + k`, and
					; section 2.4's "ascending id is
					; ascending §4.6 order"
	inc	r12
	jmp	.atom
  .done:
	mov	rax, [cp_base]
	return
endp

; rax = the declaration id of `Mundus`, or 0 if this tree has no atoms.
; The atoms are always the LAST eleven declarations (`ast_cap_push` runs
; after the walk and nothing pushes a declaration afterwards), so this is
; one bounds check and one kind test, not a scan -- and `ast_verify` is what
; makes "always the last eleven" true of a tree that came from `ast_load`
; rather than from `ast_cap_push`.
proc ast_cap_base, tree
	uses	rbx, r12
	locals
	endl
	mov	rbx, [tree]
	mov	r12, [rbx + Ast.decls.len]
	mov	rcx, AST_CAP_COUNT
	cmp	r12, rcx
	jl	.none
	sub	r12, rcx
	inc	r12			; the first of the last eleven
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_CAPATOM
	jne	.none
	mov	rax, r12
	return
  .none:
	xor	eax, eax
	return
endp

; An interner id -> the §4.6 ORDINAL of the atom of that name (1..11), or 0
; if the name is not an atom's. This is the whole of "is this identifier a
; capability?" -- checker.md section 2.1's lookup needs the ordinal to index
; a frame's `cap[11]`, and `ast_cap_base(tree) + k - 1` is the declaration.
; Eleven comparisons of two integers; nothing hashes and nothing here can
; depend on an address (CLAUDE.md, "Determinism is not optional").
proc ast_cap_of_name, tree, nameid
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [tree]
	mov	rax, [nameid]
	test	rax, rax
	jz	.none
	mov	rdi, rbx
	call	ast_cap_base
	test	rax, rax
	jz	.none
	mov	r13, rax
	mov	r12, 1
  .atom:
	mov	rcx, AST_CAP_COUNT
	cmp	r12, rcx
	jg	.none
	mov	rdi, rbx
	lea	rsi, [r13 + r12 - 1]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.name]
	mov	rax, [nameid]
	cmp	rcx, rax
	je	.hit
	inc	r12
	jmp	.atom
  .hit:
	mov	rax, r12
	return
  .none:
	xor	eax, eax
	return
endp

; ---- the `extra` pool and the staging stack --------------------------------
; APPEND-ONLY. `ast_extra_push` is the raw form; the three `ast_list_*`
; routines are how a postorder walk actually builds a list, because it does
; not know the child count until it has walked the children.
proc ast_extra_push, tree, value
	uses	rbx
	locals
	endl
	mov	rbx, [tree]
	lea	rdi, [rbx + Ast.extra]
	call	vec_push
	mov	rcx, [value]
	mov	[rax], ecx
	mov	rax, [rbx + Ast.extra.len]
	dec	rax			; the index just written
	return
endp

proc ast_list_mark, tree
	locals
	endl
	mov	rdi, [tree]
	mov	rax, [rdi + Ast.stage.len]
	return
endp

proc ast_list_push, tree, value
	uses	rbx
	locals
	endl
	mov	rbx, [tree]
	lea	rdi, [rbx + Ast.stage]
	call	vec_push
	mov	rcx, [value]
	mov	[rax], ecx
	return
endp

; Copies everything staged above `mark` into `extra`, in staging order, and
; truncates the stack back to `mark`. rax = the `extra` offset (0 if the list
; was empty), rdx = the element count. The pair goes straight into a node's
; AST_R_LIST / AST_R_COUNT slots.
proc ast_list_emit, tree, mark
	uses	rbx, r12, r13, r14
	locals
		slot le_start, dq
		slot le_val,   dd
	endl
	mov	rbx, [tree]
	mov	r12, [mark]
	mov	r13, [rbx + Ast.stage.len]
	rassert	r12 le r13
	mov	r14, r13
	sub	r14, r12		; r14 = element count
	test	r14, r14
	jz	.empty

	mov	rax, [rbx + Ast.extra.len]
	mov	[le_start], rax		; the run starts at the current end
  .copy:
	lea	rdi, [rbx + Ast.stage]
	mov	rsi, r12
	call	vec_get
	mov	eax, [rax]
	mov	[le_val], eax		; a local, not a push: `vec_push`
					; follows, and an odd push before a
					; call breaks SysV's 16-byte alignment
					; (docs/asm-conventions.md 2)
	lea	rdi, [rbx + Ast.extra]
	call	vec_push
	mov	ecx, [le_val]
	mov	[rax], ecx
	inc	r12
	cmp	r12, r13
	jl	.copy

	mov	rcx, [mark]
	mov	[rbx + Ast.stage.len], rcx	; pop the staged run
	mov	rax, [le_start]
	mov	rdx, r14
	return
  .empty:
	mov	rcx, [mark]
	mov	[rbx + Ast.stage.len], rcx
	xor	eax, eax
	xor	edx, edx
	return
endp

; ---- the Stage 2 side tables ----------------------------------------------
; typed-ast.md section 2.2: facts with no slot in a node go in tables INDEXED
; BY NODE ID, which "cost nothing until Stage 2 allocates them". This is that
; allocation, and it is one call rather than three because all three are
; sized from counts that are final the moment Stage 1 ends.
;
;   own[node]     -- 1 byte: does this expression yield an OWNED reference?
;                    (spec §6.3 decision 3; ssa-ir.md 2.9)
;   konst[node]   -- 8 bytes: the literal's value, once the checker knows the
;                    width the `[OPEN]` numeric grammar could not supply
;   layout[decl]  -- 8 bytes: AstLayout (typed-ast.md section 2.8)
proc ast_side_alloc, tree
	uses	rbx, r12
	locals
	endl
	mov	rbx, [tree]
	mov	eax, [rbx + Ast.sides]
	rassert	rax eq 0		; once only

	mov	r12, [rbx + Ast.nodes.len]
	lea	rdi, [rbx + Ast.own]
	mov	rsi, [rbx + Ast.arena]
	mov	rdx, 1
	mov	rcx, r12
	call	vec_init
	lea	rdi, [rbx + Ast.konst]
	mov	rsi, [rbx + Ast.arena]
	mov	rdx, 8
	mov	rcx, r12
	call	vec_init
	mov	[rbx + Ast.own.len], r12
	mov	[rbx + Ast.konst.len], r12
	mov	rdi, [rbx + Ast.own.data]
	mov	rcx, r12
	xor	eax, eax
	cld
	test	rcx, rcx
	jz	.own_done
	rep	stosb
  .own_done:
	mov	rdi, [rbx + Ast.konst.data]
	mov	rcx, r12
	shl	rcx, 3
	xor	eax, eax
	test	rcx, rcx
	jz	.konst_done
	rep	stosb
  .konst_done:

	mov	r12, [rbx + Ast.decls.len]
	lea	rdi, [rbx + Ast.layout]
	mov	rsi, [rbx + Ast.arena]
	mov	rdx, sizeof.AstLayout
	mov	rcx, r12
	call	vec_init
	mov	[rbx + Ast.layout.len], r12
	mov	rdi, [rbx + Ast.layout.data]
	mov	rcx, r12
	shl	rcx, 3
	xor	eax, eax
	test	rcx, rcx
	jz	.layout_done
	rep	stosb
  .layout_done:
	mov	dword [rbx + Ast.sides], 1
	return
endp

proc ast_own_set, tree, id, value
	uses	rbx
	locals
	endl
	mov	rbx, [tree]
	mov	eax, [rbx + Ast.sides]
	rassert	rax ne 0
	lea	rdi, [rbx + Ast.own]
	mov	rsi, [id]
	rassert	rsi ge 1
	dec	rsi
	call	vec_get
	mov	rcx, [value]
	mov	[rax], cl
	return
endp

proc ast_own_get, tree, id
	uses	rbx
	locals
	endl
	mov	rbx, [tree]
	mov	eax, [rbx + Ast.sides]
	rassert	rax ne 0
	lea	rdi, [rbx + Ast.own]
	mov	rsi, [id]
	rassert	rsi ge 1
	dec	rsi
	call	vec_get
	movzx	eax, byte [rax]
	return
endp

proc ast_konst_set, tree, id, value
	uses	rbx
	locals
	endl
	mov	rbx, [tree]
	mov	eax, [rbx + Ast.sides]
	rassert	rax ne 0
	lea	rdi, [rbx + Ast.konst]
	mov	rsi, [id]
	rassert	rsi ge 1
	dec	rsi
	call	vec_get
	mov	rcx, [value]
	mov	[rax], rcx
	return
endp

proc ast_konst_get, tree, id
	uses	rbx
	locals
	endl
	mov	rbx, [tree]
	mov	eax, [rbx + Ast.sides]
	rassert	rax ne 0
	lea	rdi, [rbx + Ast.konst]
	mov	rsi, [id]
	rassert	rsi ge 1
	dec	rsi
	call	vec_get
	mov	rax, [rax]
	return
endp

proc ast_layout_at, tree, id
	uses	rbx
	locals
	endl
	mov	rbx, [tree]
	mov	eax, [rbx + Ast.sides]
	rassert	rax ne 0
	lea	rdi, [rbx + Ast.layout]
	mov	rsi, [id]
	rassert	rsi ge 1
	dec	rsi
	call	vec_get
	return
endp

; ---- `numeri` --------------------------------------------------------------
; Four bytes on the tree, not on any node (typed-ast.md section 2.7): spec
; §8.6 gives no per-function syntax for it, and the inter-module coercion
; spec §5.4 requires is a call, not an annotation. The driver sets these from
; the parsed `ego`; the lowering copies them into every `Func` header
; (ssa-ir.md 2.6).
proc ast_numeri_set, tree, axis, value
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [axis]
	rassert	rsi le 3
	mov	rcx, [value]
	lea	rax, [rdi + Ast.numeri]
	mov	[rax + rsi], cl
	return
endp

proc ast_numeri_get, tree, axis
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [axis]
	rassert	rsi le 3
	lea	rax, [rdi + Ast.numeri]
	movzx	eax, byte [rax + rsi]
	return
endp
