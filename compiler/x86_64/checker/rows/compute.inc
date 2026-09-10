; compiler/x86_64/checker/rows/compute.inc
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
; PASS 3 -- ROW COMPUTATION. `docs/design/checker.md` section 2.2's third
; deliverable: `draws(F)`, `row(F)`, `EXS-E0421`, the §4.4 ceiling
; (`EXS-E0510`), and capability-bearing propagation with `EXS-E0501`.
;
; `[UNTESTED]` ON REAL SOURCE, and that is not a hedge. This pass reads
; `Decl.ty` and `Node.ty`, which the TYPES pass fills; the types pass does not
; exist. Everything below is exercised on hand-built trees with those slots
; pre-filled (tests/unit/chk_row_e0421.asm, chk_row_e0510.asm), which is
; exactly the arrangement checker.md section 6 prescribes -- "hand-built trees
; ... make every pass testable without a parser". No `.exsc` file has reached
; it, and `chk_run` (checker/checker.inc) does not call it yet, for that
; reason.
;
; WHAT THE FIXTURES DO AND DO NOT COVER, because "there is a fixture" is not
; the same claim as "this line has run":
;
;   COVERED, each with a mutant that fails without it -- substitution across a
;   call, `EXS-E0421` at the drawing node in a declared function, the fixpoint
;   ITERATING (a three-function chain built in reverse node order, so a
;   one-round fixpoint gets the wrong answer), the capability-bearing
;   fixpoint through a struct field, `EXS-E0501` restricted to module level,
;   the ceiling at an impl head, at an impl method and at a `sicut dyn` cast,
;   and the generic-operand case that consults the ceiling instead of an impl.
;
;   NOT COVERED: `__chk_row_seed_draws` (it reads `ChkCtx.draws`, which only
;   `chk_resolve` fills, and these fixtures do not run pass 1 -- their trees
;   are resolved by hand); `__chk_row_prov` (no fixture declares a capability
;   PARAMETER or a `sub`); `*T` -> `Crudum`; the `Lambda` path through
;   `__chk_row_encmap`; and re-interning in `__chk_row_commit` (no fixture
;   sets `Decl.ty` on a function, which is the types pass's job). All
;   `[UNTESTED]`.
;
; FIXPOINT ITERATION, NOT SCCs, and checker.md section 2.3 gives the reason
; this file implements: spec §4.1 rule 5 infers private rows TRANSITIVELY and
; a call graph has cycles, so some cycle-tolerant scheme is required. The row
; is a pair of masks, the transfer is monotone union, the lattice height is
; at most 11 + 64 per function, and the least fixpoint is unique WHATEVER the
; visiting order -- so determinism is free, and there is no DFS, no
; index/lowlink array and no explicit stack to get right in assembly. Tarjan
; would buy asymptotic rounds and cost all three. Visiting order is
; declaration order because that is the order the use list is built in; the
; ANSWER does not depend on it, which is the property being bought.
;
; FOUR DEPARTURES FROM checker.md, each because that document designs against
; passes that do not exist yet. Recorded here rather than absorbed:
;
;  D1. PASS 3 DOES NOT MUTATE `TypeNode.b`. Section 2.2's table says pass 3
;      fills "fn/dyn `TypeNode.b`". It cannot: `Ast.types` is interned by
;      content through `Ast.typemap`, whose keys are arena COPIES of the
;      record (ast/types.inc), so writing a new row into a live `AstType`
;      would leave the map answering lookups with a key that no longer
;      matches its entry -- two structurally identical types would then get
;      two ids, and "type compatibility is equality of interned type ids"
;      (checker.md section 1.5) would quietly stop holding. This pass
;      RE-INTERNS instead: the inferred row goes into a fresh `fn` type and
;      the new id is written to `Decl.ty` and to the `Fn` node's `ty`. The
;      old id stays in the table, unreferenced, costing 16 bytes.
;  D2. PROVIDERS FOR A *CALL-INDUCED* DRAW COME FROM THE DECLARATION TABLE.
;      Pass 1 answers "who provides P here" with a walk over `ChkFrame.caps`,
;      and it does so for every DIRECT atom draw -- `chk_resolve` hands this
;      pass only the draws no frame satisfied (`ChkUse`, kind
;      `CHK_USE_DRAW`/`CHK_USE_CAPTURE`), which is why nothing here re-scans
;      `Path` nodes. But the frames are gone by the time pass 3 runs, and a
;      draw induced by a CALL -- an atom in the callee's row after
;      substitution -- was never looked up in them. For those, and only those,
;      this pass reads the declaration table instead: a `PARAM` of capability
;      type and every `SUB` whose `Decl.parent` is `F` provide their atoms for
;      the whole of `F`. That is an OVER-APPROXIMATION -- a `sub` in one block
;      satisfies a call-induced draw in a sibling block -- so it can only
;      ACCEPT what a frame walk would reject, never the reverse. Closing it
;      needs pass 1 to keep a per-function provider mask; requested, not
;      assumed.
;  D3. THE ENCLOSING FUNCTION OF A NODE IS COMPUTED HERE. No slot holds it
;      and no pass records it. It falls out of postorder for free: a
;      subtree's node ids are the contiguous range `[lo(X), X]`, `lo` is one
;      ascending linear pass because every child's id is smaller, and a
;      `Lambda` always has a smaller id than the `Fn` that contains it -- so
;      assigning innermost-first and never overwriting gives the right
;      answer in one more pass. This is a generic descent over `ast_roles`,
;      which section 1.3's "no generic walker exists" says does not exist;
;      it does now, and it is 40 lines because that table was built for it.
;  D4. THE DIAGNOSTIC SINK IS `ChkCtx`, NOT THE THREE ARGUMENTS SECTION 6
;      WRITES. `chk_raise` buffers a `ChkDiag` so that `chk_flush` can order
;      every pass's output by (pass, node index) at once -- section 4's rule,
;      which a pass writing straight into the caller's vector would break.
;      Passes 3 and 4 therefore take the context and nothing else, and each
;      ORs its `CHK_S_*` bit into `ChkCtx.stages` on the way out. `chk_layout`
;      takes ONE more argument, the target's pointer width; see its header.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1 (rules 4-7), §4.2
; (substitution), §4.3 (capability-bearing), §4.4 (the ceiling), §4.6.
; -----------------------------------------------------------------------------

include 'intern.inc'

; ---- what this module raises, and through what -------------------------------
; Five codes -- `E0421` `E0501` `E0510` (pass 3), `E0321` `E0322` (pass 4) --
; all through `chk_raise` / `chk_raise_span` (checker/resolve/resolve.inc), so
; that `chk_flush` can put them in checker.md section 4's order: pass number
; first, node index second. Nothing here writes to an fd, formats a message or
; names a code in English (spec §8.3: tools match codes).
; ---- a use ----------------------------------------------------------------
; checker.md section 2.3: "the walk first builds a use list in walk order --
; `Use { fn, node, kind, target }` for every call, atom name, `*T`, lambda
; capture and `sub` -- and the fixpoint iterates the list, never the tree."
; Captures are D4; a `sub` is a PROVIDER, not a draw (section 2.3's "`sub P`
; does not itself contribute `P` to the row"), so it is in `wprov`, not here.
; An atom NAMED in expression position is NOT here: `chk_resolve` already
; looked it up in the frames, raised `EXS-E0421` itself when the enclosing
; row was declared, and appended a `ChkUse` when it was inferred. Re-scanning
; `Path` nodes would double-count every draw and report every violation twice.
; What is left is exactly the two things resolve.inc's `__chk_draw` header
; says need types:
CHK_ROW_U_RAW	= 1	; a `*T` anywhere -- spec §8.4 requires `Crudum`
CHK_ROW_U_CALL	= 2	; `target` = the `Call` node; the row is recomputed each
			; round, because a callee's own row is still moving

struct ChkRowUse
	field fnode,  dd	; the enclosing `Fn`/`Lambda` NODE
	field node,   dd	; the drawing node -- `EXS-E0421`'s span
	field ukind,  dd
	field target, dd
end struct
assert sizeof.ChkRowUse = 16

; ---- the scratch state -----------------------------------------------------
; checker.md section 5's second arena: "reset after pass 6 and never backing
; anything the tree points at". Every table here is a `Vec` indexed by a node
; or declaration id, sized once from a count that is final before pass 3
; starts, so nothing grows and `rt/arena.inc`'s no-growth rule is never
; tested.
struct ChkRowWork
	field wtree,  dq
	field wctx,   dq	; -> ChkCtx: the tree, the scratch arena, the sink
	field warena, dq
	wlo	Vec		; dd by node id -- the subtree's least node id
	wenc	Vec		; dd by node id -- enclosing `Fn`/`Lambda` node
	wbear	Vec		; dd by decl id -- capability-bearing atom mask
	wprov	Vec		; dd by decl id -- atoms F provides (D2)
	wrat	Vec		; dd by decl id -- F's DECLARED row atoms
	wror	Vec		; dq by decl id -- F's DECLARED row ordinals
	wdra	Vec		; dd by decl id -- F's drawn atoms (the lfp)
	wdro	Vec		; dq by decl id -- F's drawn ordinals (the lfp)
	wdcl	Vec		; db by decl id -- 1 iff F's row is DECLARED
	wlay	Vec		; db by decl id -- pass 4: 0 pending, 1 in
				; progress (a cycle), 2 laid out
	wuse	Vec		; ChkRowUse, in node order
	field wchg,   dq	; the fixpoint's change flag
	field wrnd,   dq	; rounds taken -- evidence, not decoration
	field wptr,   dd	; the target's pointer width in BITS (`--hospes`)
	field wpad,   dd
end struct

; A vec of `chkw_n` zeroed elements of `chkw_w` bytes, in the scratch arena.
proc __chk_row_zvec, chkw_v, chkw_ar, chkw_w, chkw_n
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkw_v]
	mov	r12, [chkw_n]
	mov	r13, [chkw_w]
	mov	rdi, rbx
	mov	rsi, [chkw_ar]
	mov	rdx, r13
	mov	rcx, r12
	call	vec_init
	mov	[rbx + Vec.len], r12
	mov	rdi, [rbx + Vec.data]
	mov	rcx, r12
	imul	rcx, r13
	xor	eax, eax
	cld
	test	rcx, rcx
	jz	.done
	rep	stosb
  .done:
	return
endp

; ---- initialisation --------------------------------------------------------
; The scratch tables live on `ChkCtx.arena` -- the checker's own, which
; `chk_run`'s caller may reset once every `Diag` has been rendered
; (resolve.inc's `ChkCtx` header) -- never on the tree's, because
; `arena_reset` there would invalidate the tree itself (rt/arena.inc).
;
; `chkw_pb` is the target's pointer width in bits, which pass 4 needs to make
; `mensura` concrete (spec §5.2, §9.5). It is an ARGUMENT and not read from
; anywhere: `--hospes` is a TRIPLE STRING in `DrvCtx.hospes` and nothing in
; this tree turns a triple into a width yet. `[OPEN]`, and it is the driver's.
proc chk_row_work_init, chkw_wk, chkw_cx, chkw_pb
	uses	rbx, r12, r13
	locals
		slot chkw_tr, dq
		slot chkw_ar, dq
	endl
	mov	rbx, [chkw_wk]
	mov	rdi, rbx
	mov	rcx, sizeof.ChkRowWork
	xor	eax, eax
	cld
	rep	stosb
	mov	rbx, [chkw_wk]
	mov	rax, [chkw_cx]
	mov	[rbx + ChkRowWork.wctx], rax
	mov	rcx, [rax + ChkCtx.tree]
	mov	[rbx + ChkRowWork.wtree], rcx
	mov	[chkw_tr], rcx
	mov	rcx, [rax + ChkCtx.arena]
	mov	[rbx + ChkRowWork.warena], rcx
	mov	[chkw_ar], rcx
	mov	rax, [chkw_pb]
	rassert	rax ge 8
	mov	rcx, 64
	rassert	rax le rcx
	mov	[rbx + ChkRowWork.wptr], eax

	mov	rdi, [chkw_tr]
	call	ast_node_count
	mov	r12, rax
	inc	r12			; ids are 1-based; index by id directly
	mov	rdi, [chkw_tr]
	call	ast_decl_count
	mov	r13, rax
	inc	r13

	lea	rdi, [rbx + ChkRowWork.wlo]
	mov	rsi, [chkw_ar]
	mov	rdx, 4
	mov	rcx, r12
	call	__chk_row_zvec
	lea	rdi, [rbx + ChkRowWork.wenc]
	mov	rsi, [chkw_ar]
	mov	rdx, 4
	mov	rcx, r12
	call	__chk_row_zvec
	lea	rdi, [rbx + ChkRowWork.wbear]
	mov	rsi, [chkw_ar]
	mov	rdx, 4
	mov	rcx, r13
	call	__chk_row_zvec
	lea	rdi, [rbx + ChkRowWork.wprov]
	mov	rsi, [chkw_ar]
	mov	rdx, 4
	mov	rcx, r13
	call	__chk_row_zvec
	lea	rdi, [rbx + ChkRowWork.wrat]
	mov	rsi, [chkw_ar]
	mov	rdx, 4
	mov	rcx, r13
	call	__chk_row_zvec
	lea	rdi, [rbx + ChkRowWork.wror]
	mov	rsi, [chkw_ar]
	mov	rdx, 8
	mov	rcx, r13
	call	__chk_row_zvec
	lea	rdi, [rbx + ChkRowWork.wdra]
	mov	rsi, [chkw_ar]
	mov	rdx, 4
	mov	rcx, r13
	call	__chk_row_zvec
	lea	rdi, [rbx + ChkRowWork.wdro]
	mov	rsi, [chkw_ar]
	mov	rdx, 8
	mov	rcx, r13
	call	__chk_row_zvec
	lea	rdi, [rbx + ChkRowWork.wdcl]
	mov	rsi, [chkw_ar]
	mov	rdx, 1
	mov	rcx, r13
	call	__chk_row_zvec
	lea	rdi, [rbx + ChkRowWork.wlay]
	mov	rsi, [chkw_ar]
	mov	rdx, 1
	mov	rcx, r13
	call	__chk_row_zvec

	; The use list is the one table whose size is not known up front. One
	; entry per node is the ceiling -- a node contributes at most one use.
	lea	rdi, [rbx + ChkRowWork.wuse]
	mov	rsi, [chkw_ar]
	mov	rdx, sizeof.ChkRowUse
	mov	rcx, r12
	call	vec_init
	return
endp

; ---- raising ---------------------------------------------------------------
; Raise `chkw_cd` for pass `chkw_ps` against node `chkw_nd`'s span.
proc __chk_row_at, chkw_wk, chkw_ps, chkw_cd, chkw_nd
	uses	rbx
	locals
	endl
	mov	rbx, [chkw_wk]
	mov	rdi, [rbx + ChkRowWork.wctx]
	mov	rsi, [chkw_ps]
	mov	rdx, [chkw_cd]
	mov	rcx, [chkw_nd]
	call	chk_raise
	return
endp

; The same, against declaration `chkw_dc`'s span, for the one case where the
; declaring node is absent. Sorts first within its pass (`ord` 0), which is
; where a diagnostic with no node belongs (resolve.inc's `chk_raise_span`).
proc __chk_row_atd, chkw_wk, chkw_ps, chkw_cd, chkw_dc
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkw_wk]
	mov	rdi, [rbx + ChkRowWork.wtree]
	mov	rsi, [chkw_dc]
	call	ast_decl_at
	lea	r12, [rax + AstDecl.span]
	mov	rdi, [rbx + ChkRowWork.wctx]
	mov	rsi, [chkw_ps]
	mov	rdx, [chkw_cd]
	xor	rcx, rcx
	mov	r8, r12
	call	chk_raise_span
	return
endp

; ---- small table accessors -------------------------------------------------
proc __chk_row_g4, chkw_v, chkw_i
	locals
	endl
	mov	rdi, [chkw_v]
	mov	rsi, [chkw_i]
	call	vec_get
	mov	eax, [rax]
	return
endp

proc __chk_row_s4, chkw_v, chkw_i, chkw_x
	locals
	endl
	mov	rdi, [chkw_v]
	mov	rsi, [chkw_i]
	call	vec_get
	mov	rcx, [chkw_x]
	mov	[rax], ecx
	return
endp

proc __chk_row_g8, chkw_v, chkw_i
	locals
	endl
	mov	rdi, [chkw_v]
	mov	rsi, [chkw_i]
	call	vec_get
	mov	rax, [rax]
	return
endp

proc __chk_row_s8, chkw_v, chkw_i, chkw_x
	locals
	endl
	mov	rdi, [chkw_v]
	mov	rsi, [chkw_i]
	call	vec_get
	mov	rcx, [chkw_x]
	mov	[rax], rcx
	return
endp

proc __chk_row_g1, chkw_v, chkw_i
	locals
	endl
	mov	rdi, [chkw_v]
	mov	rsi, [chkw_i]
	call	vec_get
	movzx	eax, byte [rax]
	return
endp

proc __chk_row_s1, chkw_v, chkw_i, chkw_x
	locals
	endl
	mov	rdi, [chkw_v]
	mov	rsi, [chkw_i]
	call	vec_get
	mov	rcx, [chkw_x]
	mov	[rax], cl
	return
endp

; ---- D3: the enclosing function of every node ------------------------------
; `wlo[i]` = the least node id in `i`'s subtree. One ascending pass suffices
; because postorder puts every child before its parent (ast/node.inc), so a
; child's `wlo` is already final when its parent is reached. The children of a
; node are read from `ast_roles` -- the table ast/kinds.inc built so that
; ast/verify.inc "is a table walk rather than a switch with fifty arms", and
; the same argument applies here.
proc __chk_row_lomap, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_lo, dq
		slot chkw_op, 16
		slot chkw_kd, dq
		slot chkw_ax, dq
		slot chkw_sl, dq
		slot chkw_of, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	r12, 1
  .node:
	mov	rdi, r14
	call	ast_node_count
	cmp	r12, rax
	jg	.done
	mov	rdi, r14
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	mov	[chkw_kd], rcx
	movzx	ecx, word [rax + AstNode.aux]
	mov	[chkw_ax], rcx
	mov	ecx, [rax + AstNode.a]
	mov	[chkw_op + 0], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[chkw_op + 4], ecx
	mov	ecx, [rax + AstNode.c]
	mov	[chkw_op + 8], ecx
	mov	ecx, [rax + AstNode.d]
	mov	[chkw_op + 12], ecx
	mov	[chkw_lo], r12
	mov	qword [chkw_sl], 0
  .slot:
	mov	rax, [chkw_sl]
	cmp	rax, 4
	jge	.store
	mov	rdi, [chkw_kd]
	mov	rsi, rax
	call	ast_role_of
	mov	r13, rax			; the role
	mov	rax, [chkw_sl]
	lea	rcx, [chkw_op]
	mov	eax, [rcx + rax*4]
	mov	r8, rax				; the slot's value
	cmp	r13, AST_R_NODE
	je	.child
	cmp	r13, AST_R_NODEQ
	je	.child
	cmp	r13, AST_R_ROWI
	je	.rowi
	cmp	r13, AST_R_LIST
	je	.list
	cmp	r13, AST_R_LIST2
	je	.list
	jmp	.slot_next
  .rowi:
	mov	rax, [chkw_ax]
	test	rax, AST_ROW_SICUT
	jnz	.slot_next			; an interner id, not a node
  .child:
	test	r8, r8
	jz	.slot_next
	mov	rdi, rbx
	add	rdi, ChkRowWork.wlo
	mov	rsi, r8
	call	__chk_row_g4
	mov	rcx, [chkw_lo]
	cmp	rax, rcx
	jge	.slot_next
	mov	[chkw_lo], rax
	jmp	.slot_next
  .list:
	test	r8, r8
	jz	.slot_next
	mov	rax, [chkw_sl]
	inc	rax
	lea	rcx, [chkw_op]
	mov	eax, [rcx + rax*4]		; the count in the NEXT slot
	cmp	r13, AST_R_LIST2
	jne	.have_n
	add	rax, rax			; `Si` stores (cond, block) PAIRS
  .have_n:
	mov	r13, rax			; elements remaining
	mov	[chkw_of], r8			; r8 is caller-saved and the two
						; calls below clobber it
  .elem:
	test	r13, r13
	jz	.slot_next
	mov	rdi, r14
	mov	rsi, [chkw_of]
	call	ast_extra_at
	mov	rdi, rbx
	add	rdi, ChkRowWork.wlo
	mov	rsi, rax
	call	__chk_row_g4
	mov	rcx, [chkw_lo]
	cmp	rax, rcx
	jge	.elem_next
	mov	[chkw_lo], rax
  .elem_next:
	inc	qword [chkw_of]
	dec	r13
	jmp	.elem
  .slot_next:
	inc	qword [chkw_sl]
	jmp	.slot
  .store:
	mov	rdi, rbx
	add	rdi, ChkRowWork.wlo
	mov	rsi, r12
	mov	rdx, [chkw_lo]
	call	__chk_row_s4
	inc	r12
	jmp	.node
  .done:
	return
endp

; `wenc[i]` = the innermost `Fn` or `Lambda` node containing `i`, or 0 at
; module level. Ascending order is innermost-first (a `Lambda` inside a
; function has the smaller id, by postorder), so "first writer wins" is
; "innermost wins" with no comparison.
proc __chk_row_encmap, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_j, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	r12, 1
  .node:
	mov	rdi, r14
	call	ast_node_count
	cmp	r12, rax
	jg	.done
	mov	rdi, r14
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_FN
	je	.fn
	cmp	ecx, AST_LAMBDA
	je	.fn
	jmp	.next
  .fn:
	mov	rdi, rbx
	add	rdi, ChkRowWork.wlo
	mov	rsi, r12
	call	__chk_row_g4
	mov	[chkw_j], rax
  .fill:
	mov	rax, [chkw_j]
	cmp	rax, r12
	jg	.next
	mov	rdi, rbx
	add	rdi, ChkRowWork.wenc
	mov	rsi, rax
	call	__chk_row_g4
	test	rax, rax
	jnz	.fill_next
	mov	rdi, rbx
	add	rdi, ChkRowWork.wenc
	mov	rsi, [chkw_j]
	mov	rdx, r12
	call	__chk_row_s4
  .fill_next:
	inc	qword [chkw_j]
	jmp	.fill
  .next:
	inc	r12
	jmp	.node
  .done:
	return
endp

; ---- spec §4.3: the atoms a TYPE bears -------------------------------------
; rax = the atom mask type `chkw_ty` bears. A struct's contribution is read
; from `wbear`, which is the fixpoint variable -- so this routine never
; recurses through a nominal type and a struct that points at itself
; terminates. The remaining recursion is through type CONSTRUCTORS
; (`ptr` `ref` `refc` `borrow` `acies` `eventus` `red`), which checker.md
; section 2.3 lists as the transitive paths and which the type table builds
; bottom-up; the depth bound is belt and braces.
proc __chk_row_tyatoms, chkw_wk, chkw_ty, chkw_dp
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkw_wk]
	mov	r12, [chkw_ty]
	test	r12, r12
	jz	.none
	mov	rax, [chkw_dp]
	mov	rcx, CHK_ROW_DEPTH_MAX
	rassert	rax le rcx
	mov	r13, [rbx + ChkRowWork.wtree]
	mov	rax, [r13 + Ast.types.len]
	rassert	r12 le rax
	mov	rdi, r13
	mov	rsi, r12
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_CAP
	je	.cap
	cmp	ecx, AST_TY_STRUCT
	je	.struc
	cmp	ecx, AST_TY_FN
	je	.row
	cmp	ecx, AST_TY_DYN
	je	.row
	cmp	ecx, AST_TY_PTR
	je	.inner
	cmp	ecx, AST_TY_REF
	je	.inner
	cmp	ecx, AST_TY_REFC
	je	.inner
	cmp	ecx, AST_TY_BORROW
	je	.inner
	cmp	ecx, AST_TY_ACIES
	je	.inner
	cmp	ecx, AST_TY_EVENTUS
	je	.inner
	cmp	ecx, AST_TY_RED
	je	.inner
  .none:
	xor	eax, eax
	return
  .cap:
	mov	r12d, [rax + AstType.a]		; the atom's DECLARATION
	mov	rdi, r13
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
	return
  .struc:
	mov	esi, [rax + AstType.a]		; the struct's DECLARATION
	mov	rax, [r13 + Ast.decls.len]
	rassert	rsi le rax
	mov	rdi, rbx
	add	rdi, ChkRowWork.wbear
	call	__chk_row_g4
	return
  .row:
	mov	esi, [rax + AstType.b]
	mov	rdi, r13
	call	chk_row_atoms
	return
  .inner:
	mov	esi, [rax + AstType.a]
	mov	rdi, rbx
	mov	rdx, [chkw_dp]
	inc	rdx
	call	__chk_row_tyatoms
	return
endp

; ---- spec §4.3: the capability-bearing fixpoint ----------------------------
; "A type with a capability-typed field, transitively, is capability-bearing;
; the checker computes the mark." Iterated in DECLARATION order until nothing
; changes; the join is union, so the least fixpoint is unique and the order is
; a performance choice, not a semantic one (checker.md section 4).
;
; §4.3's "must be declared so" has no syntax in §8.6 -- checker.md finding 7 --
; so nothing is compared against a written mark; `AST_F_CAPBEAR` is set and
; the `ego` emitter reads it.
proc __chk_row_bear, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_m, dq
		slot chkw_n, dq
		slot chkw_i, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
  .round:
	mov	qword [rbx + ChkRowWork.wchg], 0
	mov	r12, 1
  .decl:
	mov	rdi, r14
	call	ast_decl_count
	cmp	r12, rax
	jg	.round_end
	mov	rdi, r14
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_STRUCT
	jne	.decl_next
	mov	esi, [rax + AstDecl.node]
	test	rsi, rsi
	jz	.decl_next
	mov	rdi, r14
	call	ast_node_at
	mov	ecx, [rax + AstNode.a]
	mov	[chkw_i], rcx			; the field list's offset
	mov	ecx, [rax + AstNode.b]
	mov	[chkw_n], rcx
	mov	qword [chkw_m], 0
  .field:
	cmp	qword [chkw_n], 0
	jle	.have_mask
	mov	rdi, r14
	mov	rsi, [chkw_i]
	call	ast_extra_at			; the `Field` node
	mov	rdi, r14
	mov	rsi, rax
	call	ast_node_at
	mov	esi, [rax + AstNode.d]		; its `Field` declaration
	test	rsi, rsi
	jz	.field_next
	mov	rdi, r14
	call	ast_decl_at
	mov	esi, [rax + AstDecl.ty]
	mov	rdi, rbx
	xor	rdx, rdx
	call	__chk_row_tyatoms
	or	[chkw_m], rax
  .field_next:
	inc	qword [chkw_i]
	dec	qword [chkw_n]
	jmp	.field
  .have_mask:
	mov	rdi, rbx
	add	rdi, ChkRowWork.wbear
	mov	rsi, r12
	call	__chk_row_g4
	mov	rcx, [chkw_m]
	cmp	rax, rcx
	je	.decl_next
	mov	rdi, rbx
	add	rdi, ChkRowWork.wbear
	mov	rsi, r12
	mov	rdx, [chkw_m]
	call	__chk_row_s4
	mov	qword [rbx + ChkRowWork.wchg], 1
  .decl_next:
	inc	r12
	jmp	.decl
  .round_end:
	inc	qword [rbx + ChkRowWork.wrnd]
	cmp	qword [rbx + ChkRowWork.wchg], 0
	jne	.round

	; ---- publish the flag ----
	mov	r12, 1
  .flag:
	mov	rdi, r14
	call	ast_decl_count
	cmp	r12, rax
	jg	.done
	mov	rdi, rbx
	add	rdi, ChkRowWork.wbear
	mov	rsi, r12
	call	__chk_row_g4
	test	rax, rax
	jz	.flag_next
	mov	rdi, r14
	mov	rsi, r12
	mov	rdx, AST_F_CAPBEAR
	call	ast_decl_flag
  .flag_next:
	inc	r12
	jmp	.flag
  .done:
	return
endp

; ---- EXS-E0501 -------------------------------------------------------------
; Spec §4.1 rule 7 with §4.3: a MODULE-LEVEL binding -- `firma` or
; `mutabilis`, both -- whose type bears a capability. `EXS-E0500` (module-level
; `mutabilis`, capability or not) is pass 1's and is not raised here.
; The span is the `Binding` node (checker.md section 2.4's table).
proc __chk_row_e0501, chkw_wk
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	r12, 1
  .decl:
	mov	rdi, r14
	call	ast_decl_count
	cmp	r12, rax
	jg	.done
	mov	rdi, r14
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_BINDING
	jne	.next
	mov	ecx, [rax + AstDecl.parent]
	test	ecx, ecx
	jnz	.next				; not module level
	mov	r13d, [rax + AstDecl.node]
	mov	esi, [rax + AstDecl.ty]
	mov	rdi, rbx
	xor	rdx, rdx
	call	__chk_row_tyatoms
	test	rax, rax
	jz	.next
	test	r13, r13
	jz	.by_decl
	mov	rdi, rbx
	mov	rsi, CHK_P_ROWS
	mov	rdx, 501
	mov	rcx, r13
	call	__chk_row_at
	jmp	.next
  .by_decl:
	mov	rdi, rbx
	mov	rsi, CHK_P_ROWS
	mov	rdx, 501
	mov	rcx, r12
	call	__chk_row_atd
  .next:
	inc	r12
	jmp	.decl
  .done:
	return
endp

; ---- which declarations have a row -----------------------------------------
; rax = 1 for the four declaration kinds that carry an `fn` type and therefore
; a row (checker.md section 2.5: `Decl.ty` != 0 on `FN` `EXTERNFN` `MEMBER`
; `LAMBDA`, "an `fn` type whose `b` is the FINAL row"), else 0.
proc __chk_row_isfn, chkw_k
	locals
	endl
	mov	rax, [chkw_k]
	cmp	rax, AST_D_FN
	je	.yes
	cmp	rax, AST_D_MEMBER
	je	.yes
	cmp	rax, AST_D_EXTERNFN
	je	.yes
	cmp	rax, AST_D_LAMBDA
	je	.yes
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; rax = the `Sig` node of function declaration `chkw_d`, rdx = its `Fn` or
; `Lambda` node; 0/0 if it has neither. Both kinds put the `Sig` in `a`
; (typed-ast.md section 2.3).
proc __chk_row_sig, chkw_wk, chkw_d
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkw_wk]
	mov	rbx, [rbx + ChkRowWork.wtree]
	mov	rdi, rbx
	mov	rsi, [chkw_d]
	call	ast_decl_at
	mov	r12d, [rax + AstDecl.node]
	test	r12, r12
	jz	.none
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_FN
	je	.have
	cmp	ecx, AST_LAMBDA
	je	.have
  .none:
	xor	eax, eax
	xor	edx, edx
	return
  .have:
	mov	eax, [rax + AstNode.a]
	mov	rdx, r12
	return
endp

; ---- seeding: declared or inferred, and what the declaration says ----------
; Spec §4.1 rules 5 and 6, read as checker.md section 2.3 reads them (finding
; 10): a function is DECLARED when it writes a `poscit`, when it is
; `publica`, or when it has no body to infer from -- an `externus` signature
; and an interface member both fall in the third case, and rule 5's "infer
; from their bodies, transitively" cannot mean anything for a function that
; has none. Everything else is inferred.
proc __chk_row_seed, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_sg, dq
		slot chkw_fn, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	r12, 1
  .decl:
	mov	rdi, r14
	call	ast_decl_count
	cmp	r12, rax
	jg	.done
	mov	rdi, r14
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	mov	rdi, rcx
	call	__chk_row_isfn
	test	rax, rax
	jz	.next
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_row_sig
	mov	[chkw_sg], rax
	mov	[chkw_fn], rdx
	test	rax, rax
	jz	.next

	; the `Sig`'s own row node, and the `Fn`'s flags
	mov	rdi, r14
	mov	rsi, [chkw_sg]
	call	ast_node_at
	mov	r13d, [rax + AstNode.d]		; `Row` node, or 0
	mov	rdi, r14
	mov	rsi, [chkw_fn]
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_LAMBDA
	je	.body_known			; a lambda has no `publica` and
						; always a body, so it is
						; inferred unless it writes a
						; `poscit` (spec §8.6 decision 2,
						; checker.md section 2.3)
	movzx	ecx, word [rax + AstNode.aux]
	test	ecx, AST_FN_PUBLICA
	jnz	.declared
	mov	ecx, [rax + AstNode.b]		; the body
	test	ecx, ecx
	jz	.declared			; nothing to infer from
  .body_known:
	test	r13, r13
	jnz	.declared
	jmp	.next				; inferred: the masks stay 0

  .declared:
	mov	rdi, r14
	mov	rsi, r13
	call	chk_row_of_node
	mov	r13, rax
	mov	rdi, r14
	mov	rsi, r13
	call	chk_row_atoms
	mov	rdi, rbx
	add	rdi, ChkRowWork.wrat
	mov	rsi, r12
	mov	rdx, rax
	call	__chk_row_s4
	mov	rdi, r14
	mov	rsi, r13
	call	chk_row_ords
	mov	rdi, rbx
	add	rdi, ChkRowWork.wror
	mov	rsi, r12
	mov	rdx, rax
	call	__chk_row_s8
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdcl
	mov	rsi, r12
	mov	rdx, 1
	call	__chk_row_s1
  .next:
	inc	r12
	jmp	.decl
  .done:
	return
endp

; ---- D2: providers, from the declaration table -----------------------------
; Spec §4.1 rule 4's first two ways -- bound by `sub`, received as a parameter
; -- read off `Ast.decls` rather than off pass 1's frames. The third way (a
; field of the receiver) is deliberately NOT a provider: checker.md section
; 2.1 rule 2 declines it, because reading it as one would make lookup search
; the receiver's fields and that is the kill criterion firing.
proc __chk_row_prov, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_ad, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	r12, 1
  .decl:
	mov	rdi, r14
	call	ast_decl_count
	cmp	r12, rax
	jg	.done
	mov	rdi, r14
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_PARAM
	je	.maybe
	cmp	ecx, AST_D_SUB
	je	.maybe
	jmp	.next
  .maybe:
	mov	r13d, [rax + AstDecl.parent]
	test	r13, r13
	jz	.next
	mov	esi, [rax + AstDecl.ty]
	test	rsi, rsi
	jz	.next
	mov	rdi, r14
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_CAP
	jne	.next
	mov	ecx, [rax + AstType.a]		; the atom's declaration
	mov	[chkw_ad], rcx
	mov	rdi, r14
	call	ast_cap_base
	rassert	rax ge 1
	mov	rcx, [chkw_ad]
	sub	rcx, rax
	rassert	rcx ge 0
	mov	rdx, AST_CAP_COUNT
	dec	rdx
	rassert	rcx le rdx
	mov	rax, 1
	shl	rax, cl
	mov	[chkw_ad], rax			; the atom's BIT, now
	mov	rdi, rbx
	add	rdi, ChkRowWork.wprov
	mov	rsi, r13
	call	__chk_row_g4
	or	rax, [chkw_ad]
	mov	rdi, rbx
	add	rdi, ChkRowWork.wprov
	mov	rsi, r13
	mov	rdx, rax
	call	__chk_row_s4
  .next:
	inc	r12
	jmp	.decl
  .done:
	return
endp

; ---- the use list ----------------------------------------------------------
; checker.md section 2.3's "what draws", MINUS the one pass 1 already
; accounted for. `chk_resolve` looked up every atom named in expression
; position against the frames and appended a `ChkUse` for each that no frame
; provided; `__chk_row_seed_draws`, below, folds those in. What is left, and
; what this list holds, is the two resolve.inc's `__chk_draw` header hands
; over: a call whose callee's row is non-empty after substitution, and a `*T`
; type anywhere (spec §8.4: a raw pointer requires `Crudum`).
;
; Built in NODE ORDER, which is the walk order the design specifies, so the
; list -- and every diagnostic that comes out of it -- is a function of the
; input alone.
proc __chk_row_uses, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_tg, dq
		slot chkw_uk, dq
		slot chkw_en, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	r12, 1
  .node:
	mov	rdi, r14
	call	ast_node_count
	cmp	r12, rax
	jg	.done
	mov	rdi, r14
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_CALL
	je	.call
	cmp	ecx, AST_TYPTR
	je	.rawptr
	jmp	.next
  .rawptr:
	movzx	ecx, word [rax + AstNode.aux]
	cmp	ecx, AST_PTR_RAW
	jne	.next
	mov	rdi, r14
	call	ast_cap_base
	test	rax, rax
	jz	.next
	add	rax, AST_CAP_CRUDUM - 1
	mov	rdx, CHK_ROW_U_RAW
	mov	rcx, rax
	jmp	.push
  .call:
	mov	rdx, CHK_ROW_U_CALL
	mov	rcx, r12
  .push:
	; rdx = the use kind, rcx = its target
	mov	[chkw_tg], rcx
	mov	[chkw_uk], rdx
	mov	rdi, rbx
	add	rdi, ChkRowWork.wenc
	mov	rsi, r12
	call	__chk_row_g4
	test	rax, rax
	jz	.next				; module level: nothing draws
	mov	[chkw_en], rax
	mov	rdi, rbx
	add	rdi, ChkRowWork.wuse
	call	vec_push
	mov	rcx, [chkw_en]
	mov	[rax + ChkRowUse.fnode], ecx
	mov	[rax + ChkRowUse.node], r12d
	mov	rcx, [chkw_uk]
	mov	[rax + ChkRowUse.ukind], ecx
	mov	rcx, [chkw_tg]
	mov	[rax + ChkRowUse.target], ecx
  .next:
	inc	r12
	jmp	.node
  .done:
	return
endp

; ---- pass 1's draws, folded into the fixpoint's starting point -------------
; `chk_resolve` hands over every DIRECT atom draw that reached a function root
; unprovided in a function whose row is INFERRED (`CHK_USE_DRAW`), and every
; `Lambda` root such a draw crossed on its way out (`CHK_USE_CAPTURE` -- spec
; §4.1 rule 4's FOURTH way, the closure-capture hole §4.2 says was found by
; measurement). Both are contributions to `ChkUse.owner`'s row, and both are
; already provider-free, so they go straight into the drawn masks; the
; fixpoint then propagates them through calls.
;
; A draw in a DECLARED function never appears here: resolve raised `E0421` for
; it at the drawing node itself.
proc __chk_row_seed_draws, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_ow, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wctx]
	xor	r12, r12
  .use:
	mov	rdi, r14
	call	chk_use_count
	cmp	r12, rax
	jge	.done
	mov	rdi, r14
	mov	rsi, r12
	call	chk_use_at
	mov	ecx, [rax + ChkUse.owner]
	mov	[chkw_ow], rcx
	test	rcx, rcx
	jz	.next				; module level: no row to join
	mov	r13d, [rax + ChkUse.ord]	; the §4.6 ordinal, 1..11
	rassert	r13 ge 1
	mov	rax, AST_CAP_COUNT
	rassert	r13 le rax
	dec	r13
	mov	rcx, r13
	mov	rax, 1
	shl	rax, cl
	mov	r13, rax
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdra
	mov	rsi, [chkw_ow]
	call	__chk_row_g4
	or	rax, r13
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdra
	mov	rsi, [chkw_ow]
	mov	rdx, rax
	call	__chk_row_s4
  .next:
	inc	r12
	jmp	.use
  .done:
	return
endp

; ---- what one use contributes ----------------------------------------------
; rax = atoms, rdx = ordinals. Recomputed every round rather than cached,
; because a CALL's contribution moves: the callee's own row is still being
; inferred, and reading it live is exactly what makes spec §4.1 rule 5's
; "transitively" work without a call-graph traversal.
;
; A DIRECT call reads the callee DECLARATION's current row, not the row frozen
; into its `fn` type by pass 2 -- the type still carries the seed. An
; INDIRECT call (through a value: a parameter of function type, an escaped
; closure) reads the row out of the value's TYPE, which is spec §4.2's whole
; point: "an escaped closure carries its captured authority in its own type."
proc __chk_row_contrib, chkw_wk, chkw_ui
	uses	rbx, r12, r13, r14
	locals
		slot chkw_ab, 4 * 64
		slot chkw_ca, dq
		slot chkw_co, dq
		slot chkw_nx, dq
		slot chkw_ox, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	lea	rdi, [rbx + ChkRowWork.wuse]
	mov	rsi, [chkw_ui]
	call	vec_get
	mov	r12d, [rax + ChkRowUse.ukind]
	mov	r13d, [rax + ChkRowUse.target]
	cmp	r12, CHK_ROW_U_CALL
	je	.call

	; a `*T`: `target` is `Crudum`'s declaration outright (spec §8.4).
	mov	rdi, r14
	call	ast_cap_base
	rassert	rax ge 1
	mov	rcx, r13
	sub	rcx, rax
	rassert	rcx ge 0
	mov	rdx, AST_CAP_COUNT
	dec	rdx
	rassert	rcx le rdx
	mov	rax, 1
	shl	rax, cl
	xor	edx, edx
	return

  .call:
	mov	rdi, r14
	mov	rsi, r13
	call	ast_node_at
	mov	r12d, [rax + AstNode.a]		; the callee expression
	mov	ecx, [rax + AstNode.b]
	mov	[chkw_nx], rcx			; the argument list's offset
	mov	ecx, [rax + AstNode.c]
	mov	[chkw_ox], rcx			; the argument count
	mov	qword [chkw_ca], 0
	mov	qword [chkw_co], 0

	mov	rdi, r14
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_PATH
	je	.named
	cmp	ecx, AST_MEMBER
	je	.named
	jmp	.indirect
  .named:
	mov	r13d, [rax + AstNode.d]
	test	r13, r13
	jz	.indirect
	mov	rdi, r14
	mov	rsi, r13
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	mov	rdi, rcx
	call	__chk_row_isfn
	test	rax, rax
	jz	.indirect
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdcl
	mov	rsi, r13
	call	__chk_row_g1
	test	rax, rax
	jz	.inferred
	mov	rdi, rbx
	add	rdi, ChkRowWork.wrat
	mov	rsi, r13
	call	__chk_row_g4
	mov	[chkw_ca], rax
	mov	rdi, rbx
	add	rdi, ChkRowWork.wror
	mov	rsi, r13
	call	__chk_row_g8
	mov	[chkw_co], rax
	jmp	.have_row
  .inferred:
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdra
	mov	rsi, r13
	call	__chk_row_g4
	mov	[chkw_ca], rax
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdro
	mov	rsi, r13
	call	__chk_row_g8
	mov	[chkw_co], rax
	jmp	.have_row

  .indirect:
	mov	rdi, r14
	mov	rsi, r12
	call	ast_node_at
	mov	esi, [rax + AstNode.ty]
	mov	rdi, rbx
	call	__chk_row_rowofty
	test	rax, rax
	jz	.have_row
	mov	r13, rax
	mov	rdi, r14
	mov	rsi, r13
	call	chk_row_atoms
	mov	[chkw_ca], rax
	mov	rdi, r14
	mov	rsi, r13
	call	chk_row_ords
	mov	[chkw_co], rax

  .have_row:
	cmp	qword [chkw_co], 0
	je	.plain				; no ordinal: nothing to
						; substitute, and no row is
						; created for nothing
	; ---- spec §4.2's positional substitution ----
	mov	rax, [chkw_ox]
	mov	rcx, 64
	rassert	rax le rcx
	xor	r12, r12
  .arg:
	cmp	r12, [chkw_ox]
	jge	.args_done
	mov	rdi, r14
	mov	rsi, [chkw_nx]
	add	rsi, r12
	call	ast_extra_at
	mov	rdi, r14
	mov	rsi, rax
	call	ast_node_at
	mov	esi, [rax + AstNode.ty]
	mov	rdi, rbx
	call	__chk_row_rowofty
	lea	rcx, [chkw_ab]
	mov	[rcx + r12*4], eax
	inc	r12
	jmp	.arg
  .args_done:
	mov	rdi, r14
	mov	rsi, [chkw_ca]
	mov	rdx, [chkw_co]
	call	chk_row_of_masks
	mov	r13, rax
	mov	rdi, r14
	mov	rsi, r13
	lea	rdx, [chkw_ab]
	mov	rcx, [chkw_ox]
	call	chk_row_subst
	mov	r13, rax
	mov	rdi, r14
	mov	rsi, r13
	call	chk_row_atoms
	mov	[chkw_ca], rax
	mov	rdi, r14
	mov	rsi, r13
	call	chk_row_ords
	mov	[chkw_co], rax
  .plain:
	mov	rax, [chkw_ca]
	mov	rdx, [chkw_co]
	return
endp

; rax = the row id carried by type `chkw_t2` -- `AstType.b` of an `fn` or a
; `dyn` -- or 0 for every other type. Spec §4.2: "a capability row is part of
; a function TYPE."
proc __chk_row_rowofty, chkw_wk, chkw_t2
	uses	rbx
	locals
	endl
	mov	rbx, [chkw_wk]
	mov	rbx, [rbx + ChkRowWork.wtree]
	mov	rsi, [chkw_t2]
	test	rsi, rsi
	jz	.none
	mov	rax, [rbx + Ast.types.len]
	rassert	rsi le rax
	mov	rdi, rbx
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_FN
	je	.have
	cmp	ecx, AST_TY_DYN
	je	.have
  .none:
	xor	eax, eax
	return
  .have:
	mov	eax, [rax + AstType.b]
	return
endp

; rax = the declaration a `Fn`/`Lambda` node introduces, or 0.
proc __chk_row_fdecl, chkw_wk, chkw_fn2
	uses	rbx
	locals
	endl
	mov	rbx, [chkw_wk]
	mov	rbx, [rbx + ChkRowWork.wtree]
	mov	rsi, [chkw_fn2]
	test	rsi, rsi
	jz	.none
	mov	rdi, rbx
	call	ast_node_at
	mov	eax, [rax + AstNode.d]
	return
  .none:
	xor	eax, eax
	return
endp

; ---- the fixpoint ----------------------------------------------------------
; Iterate the use list, never the tree; union each use's contribution, minus
; the drawing function's providers, into that function's drawn masks; repeat
; until a round changes nothing. A DECLARED function's row does not grow --
; its declared row IS its row (spec §4.1 rule 5 governs only inferred ones) --
; so a declared function contributes nothing to its own masks here and the
; excess becomes `EXS-E0421` in the check loop below.
;
; Termination: the join is union over a lattice of height 11 + 64 per
; function, so the number of rounds is bounded by that times the function
; count, and in practice by the longest chain of new atoms. `wrnd` records
; what it actually took; checker.md section 2.3 guessed "two rounds on the
; probe cases by inspection", which the fixtures can now check instead of
; guess.
proc __chk_row_fix, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_f, dq
		slot chkw_na, dq
		slot chkw_no, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
  .round:
	mov	qword [rbx + ChkRowWork.wchg], 0
	xor	r12, r12
  .use:
	lea	rax, [rbx + ChkRowWork.wuse]
	mov	rax, [rax + Vec.len]
	cmp	r12, rax
	jge	.round_end
	lea	rdi, [rbx + ChkRowWork.wuse]
	mov	rsi, r12
	call	vec_get
	mov	esi, [rax + ChkRowUse.fnode]
	mov	rdi, rbx
	call	__chk_row_fdecl
	mov	r13, rax
	test	r13, r13
	jz	.use_next
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdcl
	mov	rsi, r13
	call	__chk_row_g1
	test	rax, rax
	jnz	.use_next			; declared rows do not grow

	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_row_contrib
	mov	[chkw_na], rax
	mov	[chkw_no], rdx
	mov	rdi, rbx
	add	rdi, ChkRowWork.wprov
	mov	rsi, r13
	call	__chk_row_g4
	not	rax
	and	[chkw_na], rax			; a provided atom is not drawn

	mov	rdi, rbx
	add	rdi, ChkRowWork.wdra
	mov	rsi, r13
	call	__chk_row_g4
	mov	rcx, [chkw_na]
	or	rcx, rax
	cmp	rcx, rax
	je	.ords
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdra
	mov	rsi, r13
	mov	rdx, rcx
	call	__chk_row_s4
	mov	qword [rbx + ChkRowWork.wchg], 1
  .ords:
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdro
	mov	rsi, r13
	call	__chk_row_g8
	mov	rcx, [chkw_no]
	or	rcx, rax
	cmp	rcx, rax
	je	.use_next
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdro
	mov	rsi, r13
	mov	rdx, rcx
	call	__chk_row_s8
	mov	qword [rbx + ChkRowWork.wchg], 1
  .use_next:
	inc	r12
	jmp	.use
  .round_end:
	inc	qword [rbx + ChkRowWork.wrnd]
	cmp	qword [rbx + ChkRowWork.wchg], 0
	jne	.round
	return
endp

; ---- EXS-E0421 -------------------------------------------------------------
; "A draw with no provider in a declared function", spec §4.1-§4.2, AFTER the
; fixpoint (checker.md section 2.3) -- before it, a callee's inferred row is
; still incomplete and the diagnostic would depend on visiting order, which
; is exactly what the fixpoint exists to prevent. The span is the DRAWING
; node, so `applica(v, nocens)` is reported at the call, not at `nocens`.
;
; An ordinal that survived substitution is reported here too, under the same
; code: it is a draw whose provider cannot be named. checker.md section 2.4
; puts the malformed-row case in class L (`EXS-E0423`), which is the TYPES
; pass's to raise at the argument; this one is the caller's undeclared
; authority and `E0421` is what §14 entry 10 expects.
proc __chk_row_e0421, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_ea, dq
		slot chkw_eo, dq
		slot chkw_nd, dq
	endl
	mov	rbx, [chkw_wk]
	xor	r12, r12
  .use:
	lea	rax, [rbx + ChkRowWork.wuse]
	mov	rax, [rax + Vec.len]
	cmp	r12, rax
	jge	.done
	lea	rdi, [rbx + ChkRowWork.wuse]
	mov	rsi, r12
	call	vec_get
	mov	ecx, [rax + ChkRowUse.node]
	mov	[chkw_nd], rcx
	mov	esi, [rax + ChkRowUse.fnode]
	mov	rdi, rbx
	call	__chk_row_fdecl
	mov	r13, rax
	test	r13, r13
	jz	.next
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdcl
	mov	rsi, r13
	call	__chk_row_g1
	test	rax, rax
	jz	.next				; inferred: it absorbed the draw

	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_row_contrib
	mov	[chkw_ea], rax
	mov	[chkw_eo], rdx
	mov	rdi, rbx
	add	rdi, ChkRowWork.wprov
	mov	rsi, r13
	call	__chk_row_g4
	not	rax
	and	[chkw_ea], rax
	mov	rdi, rbx
	add	rdi, ChkRowWork.wrat
	mov	rsi, r13
	call	__chk_row_g4
	not	rax
	and	[chkw_ea], rax			; ... nor is a declared one
	mov	rdi, rbx
	add	rdi, ChkRowWork.wror
	mov	rsi, r13
	call	__chk_row_g8
	not	rax
	and	[chkw_eo], rax
	mov	rax, [chkw_ea]
	or	rax, [chkw_eo]
	test	rax, rax
	jz	.next
	mov	rdi, rbx
	mov	rsi, CHK_P_ROWS
	mov	rdx, 421
	mov	rcx, [chkw_nd]
	call	__chk_row_at
  .next:
	inc	r12
	jmp	.use
  .done:
	return
endp

; ---- D1: the final row goes into the type, by RE-INTERNING -----------------
; checker.md section 2.5: `Decl.ty` on a function is "an `fn` type whose `b`
; is the FINAL row -- declared or inferred; 'the row is in the type' is
; literally where the lowering reads carriers". Writing `b` in place would
; corrupt `Ast.typemap` (this file's header, D1), so a fresh type is interned
; and the declaration and its node are repointed at it.
proc __chk_row_commit, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_t0, 16
		slot chkw_rw, dq
		slot chkw_nn2, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	r12, 1
  .decl:
	mov	rdi, r14
	call	ast_decl_count
	cmp	r12, rax
	jg	.done
	mov	rdi, r14
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	mov	rdi, rcx
	call	__chk_row_isfn
	test	rax, rax
	jz	.next

	mov	rdi, rbx
	add	rdi, ChkRowWork.wdcl
	mov	rsi, r12
	call	__chk_row_g1
	test	rax, rax
	jnz	.declared
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdra
	mov	rsi, r12
	call	__chk_row_g4
	mov	r13, rax
	mov	rdi, rbx
	add	rdi, ChkRowWork.wdro
	mov	rsi, r12
	call	__chk_row_g8
	jmp	.have
  .declared:
	mov	rdi, rbx
	add	rdi, ChkRowWork.wrat
	mov	rsi, r12
	call	__chk_row_g4
	mov	r13, rax
	mov	rdi, rbx
	add	rdi, ChkRowWork.wror
	mov	rsi, r12
	call	__chk_row_g8
  .have:
	mov	rdi, r14
	mov	rsi, r13
	mov	rdx, rax
	call	chk_row_of_masks
	mov	[chkw_rw], rax

	mov	rdi, r14
	mov	rsi, r12
	call	ast_decl_at
	mov	esi, [rax + AstDecl.ty]
	mov	[chkw_nn2], rsi
	test	rsi, rsi
	jz	.next				; the types pass has not run
	mov	rdi, r14
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_FN
	jne	.next
	mov	ecx, [rax + AstType.b]
	cmp	rcx, [chkw_rw]
	je	.next				; already the final row
	mov	rcx, [rax]			; COPY the record: interning
	mov	[chkw_t0], rcx			; pushes, and a push may move
	mov	rcx, [rax + 8]			; the block this points into
	mov	[chkw_t0 + 8], rcx
	mov	rcx, [chkw_rw]
	mov	[chkw_t0 + AstType.b], ecx
	mov	rdi, r14
	lea	rsi, [chkw_t0]
	call	ast_type_intern
	mov	[chkw_nn2], rax
	mov	rdi, r14
	mov	rsi, r12
	mov	rdx, rax
	call	ast_decl_ty
	mov	rdi, r14
	mov	rsi, r12
	call	ast_decl_at
	mov	esi, [rax + AstDecl.node]
	test	rsi, rsi
	jz	.next
	mov	rdi, r14
	mov	rdx, [chkw_nn2]
	call	ast_node_ty
  .next:
	inc	r12
	jmp	.decl
  .done:
	return
endp

; ---- spec §4.4: the ceiling ------------------------------------------------
; rax = the union of interface `chkw_if`'s members' DECLARED rows, as an atom
; mask. "A trait's declared rows are a ceiling on every implementation of it."
; Read off the `Sig` nodes, not off `Decl.ty`, so this works before the types
; pass exists.
proc __chk_row_ceil, chkw_wk, chkw_if
	uses	rbx, r12, r13, r14
	locals
		slot chkw_cm, dq
		slot chkw_ci, dq
		slot chkw_cn, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	qword [chkw_cm], 0
	mov	rsi, [chkw_if]
	test	rsi, rsi
	jz	.done
	mov	rdi, r14
	call	ast_decl_at
	mov	esi, [rax + AstDecl.node]
	test	rsi, rsi
	jz	.done
	mov	rdi, r14
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_IFACE
	jne	.done
	mov	ecx, [rax + AstNode.a]
	mov	[chkw_ci], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkw_cn], rcx
  .member:
	cmp	qword [chkw_cn], 0
	jle	.done
	mov	rdi, r14
	mov	rsi, [chkw_ci]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_row_fnrow
	or	[chkw_cm], rax
	inc	qword [chkw_ci]
	dec	qword [chkw_cn]
	jmp	.member
  .done:
	mov	rax, [chkw_cm]
	return
endp

; rax = the atom mask of the row WRITTEN on `Fn` node `chkw_f2`'s signature.
; 0 both for "no `poscit`" and for "`poscit {}`", which is the same thing
; under spec §4.1 rule 6.
proc __chk_row_fnrow, chkw_wk, chkw_f2
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkw_wk]
	mov	r12, [rbx + ChkRowWork.wtree]
	mov	rdi, r12
	mov	rsi, [chkw_f2]
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_FN
	jne	.none
	mov	esi, [rax + AstNode.a]		; the `Sig`
	test	rsi, rsi
	jz	.none
	mov	rdi, r12
	call	ast_node_at
	mov	esi, [rax + AstNode.d]		; the `Row`, or 0
	mov	rdi, r12
	call	chk_row_of_node
	mov	rdi, r12
	mov	rsi, rax
	call	chk_row_atoms
	return
  .none:
	xor	eax, eax
	return
endp

; rax = an implementation's MARK: its head row union the capability-bearing
; atoms of its target type (checker.md section 2.3, "mark = head row ∪ the
; capability-bearing atoms of the target type").
proc __chk_row_implmark, chkw_wk, chkw_im
	uses	rbx, r12, r13
	locals
		slot chkw_mk, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r12, [rbx + ChkRowWork.wtree]
	mov	rdi, r12
	mov	rsi, [chkw_im]
	call	ast_node_at
	mov	esi, [rax + AstNode.a]		; the `ImplHead`
	test	rsi, rsi
	jz	.none
	mov	rdi, r12
	call	ast_node_at
	mov	r13, rax
	mov	esi, [r13 + AstNode.c]		; the head's `Row`, or 0
	mov	ecx, [r13 + AstNode.b]		; the target type node
	mov	[chkw_mk], rcx
	mov	rdi, r12
	call	chk_row_of_node
	mov	rdi, r12
	mov	rsi, rax
	call	chk_row_atoms
	mov	r13, rax
	mov	rsi, [chkw_mk]
	test	rsi, rsi
	jz	.have
	mov	rdi, r12
	call	ast_node_at
	mov	esi, [rax + AstNode.ty]
	mov	rdi, rbx
	xor	rdx, rdx
	call	__chk_row_tyatoms
	or	r13, rax
  .have:
	mov	rax, r13
	return
  .none:
	xor	eax, eax
	return
endp

; rax = the `Impl` NODE implementing interface `chkw_i2` for type id
; `chkw_t3`, or 0. A linear scan of `Ast.decls` in declaration order -- pass
; 2's impl map (checker.md section 2.7) is the types agent's and does not
; exist; the scan gives the same answer, in the same order, more slowly.
proc __chk_row_findimpl, chkw_wk, chkw_i2, chkw_t3
	uses	rbx, r12, r13, r14
	locals
		slot chkw_tn, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	r12, 1
  .decl:
	mov	rdi, r14
	call	ast_decl_count
	cmp	r12, rax
	jg	.none
	mov	rdi, r14
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_IMPL
	jne	.next
	mov	esi, [rax + AstDecl.node]
	test	rsi, rsi
	jz	.next
	mov	r13, rsi
	mov	rdi, r14
	call	ast_node_at
	mov	esi, [rax + AstNode.a]
	test	rsi, rsi
	jz	.next
	mov	rdi, r14
	call	ast_node_at			; the `ImplHead`
	mov	ecx, [rax + AstNode.b]		; the target type node
	mov	[chkw_tn], rcx			; a local, not a push: an odd
						; push before a call breaks
						; SysV's 16-byte alignment
	mov	esi, [rax + AstNode.a]		; the interface `Path`
	mov	rdi, r14
	call	ast_node_at
	mov	eax, [rax + AstNode.d]
	mov	rdx, [chkw_i2]
	cmp	rax, rdx
	jne	.next
	cmp	qword [chkw_tn], 0
	je	.next
	mov	rdi, r14
	mov	rsi, [chkw_tn]
	call	ast_node_at
	mov	eax, [rax + AstNode.ty]
	mov	rdx, [chkw_t3]
	cmp	rax, rdx
	jne	.next
	mov	rax, r13
	return
  .next:
	inc	r12
	jmp	.decl
  .none:
	xor	eax, eax
	return
endp

; ---- EXS-E0510 -------------------------------------------------------------
; Spec §4.4, all three sites checker.md section 2.3 names:
;
;   - at every `Impl`: mark ⊆ ceiling. This is the check §14 entry 24 needs
;     and the one spec §4.4 says was MISSING until `prototypes/gendict/`
;     measured it -- "an implementation reached solely through a generic
;     `<T: Trait>` was never checked against anything", because the rule was
;     stated only for `dyn` construction. Checking it at the declaration
;     closes the generic path and the `dyn`-laundering path at once.
;   - at every impl method that writes its own `poscit`: ⊆ the trait
;     member's.
;   - at `e sicut dyn I poscit {P}`: mark(the impl of `I` for `e`'s type)
;     ⊆ P; and, when `e`'s type is a generic parameter, ceiling(I) ⊆ P --
;     `bad_dyn_launder_via_generic.exsc`, rejected at the cast with no
;     instantiation needed.
proc __chk_row_e0510, chkw_wk
	uses	rbx, r12, r13, r14
	locals
		slot chkw_hd, dq
		slot chkw_id, dq
		slot chkw_bd, dq
		slot chkw_ml, dq
		slot chkw_mn, dq
	endl
	mov	rbx, [chkw_wk]
	mov	r14, [rbx + ChkRowWork.wtree]
	mov	r12, 1
  .node:
	mov	rdi, r14
	call	ast_node_count
	cmp	r12, rax
	jg	.done
	mov	rdi, r14
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_IMPL
	je	.impl
	cmp	ecx, AST_CAST
	je	.cast
	jmp	.next

  ; ---------------- an implementation head ----------------
  .impl:
	mov	ecx, [rax + AstNode.a]
	mov	[chkw_hd], rcx			; the `ImplHead`
	mov	ecx, [rax + AstNode.b]
	mov	[chkw_ml], rcx			; the member list
	mov	ecx, [rax + AstNode.c]
	mov	[chkw_mn], rcx
	cmp	qword [chkw_hd], 0
	je	.next
	mov	rdi, r14
	mov	rsi, [chkw_hd]
	call	ast_node_at
	mov	esi, [rax + AstNode.a]		; the interface `Path`
	test	rsi, rsi
	jz	.next
	mov	rdi, r14
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	mov	[chkw_id], rcx			; the interface's declaration
	test	rcx, rcx
	jz	.next
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_row_implmark
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, [chkw_id]
	call	__chk_row_ceil
	mov	[chkw_bd], rax
	not	rax
	and	rax, r13
	jz	.members
	mov	rdi, rbx
	mov	rsi, CHK_P_ROWS
	mov	rdx, 510
	mov	rcx, [chkw_hd]
	call	__chk_row_at

  .members:
	; each member's own written row against the trait member's
	cmp	qword [chkw_mn], 0
	jle	.next
	mov	rdi, r14
	mov	rsi, [chkw_ml]
	call	ast_extra_at
	mov	r13, rax			; the member `Fn` node
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_row_fnrow
	test	rax, rax
	jz	.member_next			; wrote none: nothing to check
	mov	rcx, [chkw_bd]
	not	rcx
	and	rcx, rax
	jz	.member_next
	mov	rdi, rbx
	mov	rsi, CHK_P_ROWS
	mov	rdx, 510
	mov	rcx, r13
	call	__chk_row_at
  .member_next:
	inc	qword [chkw_ml]
	dec	qword [chkw_mn]
	jmp	.members

  ; ---------------- a `sicut dyn` cast ----------------
  .cast:
	mov	ecx, [rax + AstNode.b]		; the target TYPE node
	mov	[chkw_hd], rcx
	mov	ecx, [rax + AstNode.a]		; the expression
	mov	[chkw_ml], rcx
	test	rcx, rcx
	jz	.next
	cmp	qword [chkw_hd], 0
	je	.next
	mov	rdi, r14
	mov	rsi, [chkw_hd]
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_TYDYN
	jne	.next
	mov	esi, [rax + AstNode.b]		; the bound `Row`, or 0
	mov	ecx, [rax + AstNode.a]		; the interface `Path`
	mov	[chkw_id], rcx
	mov	rdi, r14
	call	chk_row_of_node
	mov	rdi, r14
	mov	rsi, rax
	call	chk_row_atoms
	mov	[chkw_bd], rax
	cmp	qword [chkw_id], 0
	je	.next
	mov	rdi, r14
	mov	rsi, [chkw_id]
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	mov	[chkw_id], rcx
	test	rcx, rcx
	jz	.next

	mov	rdi, r14
	mov	rsi, [chkw_ml]
	call	ast_node_at
	mov	ecx, [rax + AstNode.ty]		; the expression's type
	mov	[chkw_ml], rcx
	test	rcx, rcx
	jz	.next				; the types pass has not run
	mov	rdi, r14
	mov	rsi, rcx
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_PARAM
	je	.dyn_generic
	mov	rdi, rbx
	mov	rsi, [chkw_id]
	mov	rdx, [chkw_ml]
	call	__chk_row_findimpl
	test	rax, rax
	jz	.next				; no impl: class N, not ours
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_row_implmark
	jmp	.dyn_test
  .dyn_generic:
	; `e`'s type is a generic parameter bound to the interface: nothing is
	; instantiated, so the CEILING is what must fit inside the bound.
	mov	rdi, rbx
	mov	rsi, [chkw_id]
	call	__chk_row_ceil
  .dyn_test:
	mov	rcx, [chkw_bd]
	not	rcx
	and	rax, rcx
	jz	.next
	mov	rdi, rbx
	mov	rsi, CHK_P_ROWS
	mov	rdx, 510
	mov	rcx, r12
	call	__chk_row_at
  .next:
	inc	r12
	jmp	.node
  .done:
	return
endp

; ---- pass 3 ----------------------------------------------------------------
;     chk_rows(chk_c) -> rax = the number of diagnostics THIS pass buffered
;
; checker.md section 2.2's order, with the two fixpoints separated because
; they measure different things: capability-bearing is over STRUCT
; declarations and rows are over the use list, and neither reads the other's
; result. The scratch tables come from `ChkCtx.arena` and are abandoned when
; the caller resets it; nothing the tree points at is allocated here.
proc chk_rows, chkw_c2
	uses	rbx, r12, r13
	locals
	endl
	mov	r13, [chkw_c2]
	lea	rax, [r13 + ChkCtx.dbuf]
	mov	r12, [rax + Vec.len]		; the buffer's depth on entry

	mov	rdi, [r13 + ChkCtx.arena]
	mov	rsi, sizeof.ChkRowWork
	call	arena_alloc
	mov	rbx, rax
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, CHK_ROW_PTRBITS
	call	chk_row_work_init

	mov	rdi, rbx
	call	__chk_row_lomap
	mov	rdi, rbx
	call	__chk_row_encmap
	mov	rdi, rbx
	call	__chk_row_bear
	mov	rdi, rbx
	call	__chk_row_e0501
	mov	rdi, rbx
	call	__chk_row_seed
	mov	rdi, rbx
	call	__chk_row_prov
	mov	rdi, rbx
	call	__chk_row_seed_draws
	mov	rdi, rbx
	call	__chk_row_uses
	mov	rdi, rbx
	call	__chk_row_fix
	mov	rdi, rbx
	call	__chk_row_e0421
	mov	rdi, rbx
	call	__chk_row_e0510
	mov	rdi, rbx
	call	__chk_row_commit

	or	dword [r13 + ChkCtx.stages], CHK_S_ROWS
	lea	rax, [r13 + ChkCtx.dbuf]
	mov	rax, [rax + Vec.len]
	sub	rax, r12
	return
endp
