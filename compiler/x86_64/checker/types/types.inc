; compiler/x86_64/checker/types/types.inc
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
; STAGE 2, PASS 2 -- TYPES. `docs/design/checker.md` section 2.2's second
; deliverable and section 6's `checker-types` row. THE AGGREGATION POINT of
; `checker/types/`: this file is the last link of the module's own chain
; (`prim.inc <- sig.inc <- this file`) and the one `rows/rowdefs.inc` names.
;
; WHAT IT FILLS (checker.md section 2.2's table): `Ast.types`, `Node.ty`,
; `Decl.ty`, `Member.d`, `konst`, `own`, `memory_resident` -- and, on the way
; out, `CHK_S_TYPES` in `ChkCtx.stages`, which is what un-gates passes 3 and 4
; in `checker/checker.inc`.
;
; WHAT IT RAISES: `E0301` `E0302` `E0303` `E0304` `E0305` `E0306` `E0307`
; `E0308` `E0309` `E0311` `E0343` `E0423` `E0424` `E0511` `E0520`, all through
; `chk_raise` with `CHK_P_TYPES` so that `chk_flush` orders the whole
; checker's output by (pass, node index) at once. No code is invented and none
; is renumbered (CLAUDE.md); every one is in spec §13.
;
; ---------------------------------------------------------------------------
; ONE RECURSIVE DESCENT, NOT A LOOP OVER NODE IDS -- and that IS a departure
; from checker.md section 2.2, which says node ids are postorder "so every
; child is typed before its parent without a second visit". True, and not
; sufficient: three of this pass's obligations are properties of the ENCLOSING
; construct, not of the node --
;
;   - an integer literal takes the EXPECTED type (section 2.3), which flows
;     DOWN from a parent an ascending loop has not reached yet;
;   - `redde` is checked against the enclosing function's result, and `rumpe`
;     and `perge` against whether there is an enclosing loop at all (§8.5);
;   - `EXS-E0341`/`EXS-E0342` are about being INSIDE a `contrahe` iteration.
;
; A loop over ids would need a side table for each. A descent carries them in
; four `ChkTyWork` fields, and the postorder guarantee is still what makes the
; descent one visit per node.
;
; ---------------------------------------------------------------------------
; PENDING INTEGER LITERALS. `__chk_ty_expr` answers **0** for an `INT` literal
; whose width nothing has supplied yet -- checker.md section 2.3: "Integer
; literals take the *expected* type: the other operand of a `Binary`, the
; annotation, the parameter, the result at `redde`, the range partner." 0 is
; not a type id, so a caller that has an expectation calls `__chk_ty_expect`
; and a caller that does not propagates the 0 upward. Anything still 0 when
; the expression is finished is `EXS-E0308` ("literal cannot be typed"), which
; is also what a literal that does not FIT its width gets. `konst[node]` holds
; the value once the width is known (typed-ast.md section 2.2).
;
; ---------------------------------------------------------------------------
; WHAT IS `[OPEN]` HERE, stated rather than left to be discovered:
;
;   - `EXS-E0332` IS UNREACHABLE, and checker.md section 2.6 already said it
;     would be: "until §8.6 admits it nothing produces a brand and `E0332` is
;     designed but unreachable". Confirmed against the grammar as built --
;     `'` is not a spec §8.4 token, `cst/parse.inc` has no production that
;     yields a brand, and `AST_TY_BRAND` is interned by nothing in this tree.
;     No brand type is ever constructed, so no two brands can differ, so the
;     rule has no case. The mechanism section 2.6 designs (a `GenericParam`
;     bound to a value parameter) needs one `TypeArg` form the grammar does
;     not have. Fixture deliberately NOT written: a fixture that cannot fail
;     is the vacuous kind tests/run.sh's floor exists to catch.
;   - GENERIC INSTANTIATION. The impl map is built and `chk_ty_findimpl`
;     answers it, but `Call.d` is filled only for a call whose callee is a
;     generic function with an explicit `GenericArgs` list; substitution of
;     `T := Left` through the result type (section 2.7's "recursion over the
;     type table with a small ordinal -> type-id vector") is not implemented.
;     A generic body still type-checks against the interface, which is the
;     half section 2.7 says must never consult an impl.
;   - PATTERNS. `Discerne`/`Casus` type the scrutinee and each arm's block; a
;     pattern is not checked against the scrutinee's type. Spec §8.6's
;     `Pattern` is a `Path`, a literal or `_`, and what a `Path` pattern MEANS
;     (a binding? a constant? a variant of a sum type the language does not
;     have?) is not decided anywhere.
;   - DEFINITE ASSIGNMENT is the straight-line form only: a binding declared
;     with an annotation and no initializer is unassigned until an `Assign`
;     names it, and a read before that is `EXS-E0307`. An assignment inside a
;     `si` arm counts as an assignment, which ACCEPTS programs a path-sensitive
;     analysis would reject. The unsound direction is deliberate for now --
;     rejecting on a path-insensitive approximation would reject correct
;     programs, which is worse -- and it is the shape spec §13's own note
;     ("read on some path before any assignment") asks for.
;   - `own[node]` is set on a `Call` yielding `ref`/`refc` and nowhere else.
;     Spec §6.3's ownership rules are Stage 3's; this is the one case IR 2.9's
;     `retain`/`release` insertion cannot get from the type alone.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1-§4.7, §5.1-§5.5, §6.4, §7.1,
; §8.5, §8.6, §9.1, §9.5, §12, §13.
; -----------------------------------------------------------------------------

include 'sig.inc'

; =============================================================================
; expressions
; =============================================================================

; __chk_ty_expr(w, n) -> rax = the expression's type id, or 0 when it is an
; integer literal still waiting for an expected type. Node 0 answers `unit`.
proc __chk_ty_expr, chkt_wp2, chkt_np2
	uses	rbx, r12, r13, r14
	locals
		slot chkt_kp, dq
		slot chkt_ap, dq
		slot chkt_bp, dq
		slot chkt_cp, dq
		slot chkt_dp, dq
		slot chkt_up, dq
	endl
	mov	rbx, [chkt_wp2]
	mov	r12, [chkt_np2]
	test	r12, r12
	jnz	.have
	mov	rdi, rbx
	call	__chk_ty_unit
	return
  .have:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.kind]
	mov	[chkt_kp], rcx
	movzx	ecx, word [rax + AstNode.aux]
	mov	[chkt_up], rcx
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_ap], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bp], rcx
	mov	ecx, [rax + AstNode.c]
	mov	[chkt_cp], rcx
	mov	ecx, [rax + AstNode.d]
	mov	[chkt_dp], rcx

	mov	rcx, [chkt_kp]
	cmp	rcx, AST_LIT
	je	.lit
	cmp	rcx, AST_PATH
	je	.path
	cmp	rcx, AST_MEMBER
	je	.member
	cmp	rcx, AST_CALL
	je	.call
	cmp	rcx, AST_BINARY
	je	.binary
	cmp	rcx, AST_UNARY
	je	.unary
	cmp	rcx, AST_CAST
	je	.cast
	cmp	rcx, AST_INDEX
	je	.index
	cmp	rcx, AST_TRY
	je	.try
	cmp	rcx, AST_LAMBDA
	je	.lambda
	cmp	rcx, AST_ERROR
	je	.err
	; Anything else in expression position answers `unit`, AND THIS ARM IS A
	; LEAF ON PURPOSE. It used to call `__chk_ty_stmt`, whose own fallback
	; calls back here -- so a node kind NEITHER routine handled made the two
	; call each other until the stack ran out. Measured: `exsc` segfaulted
	; outright on tests/diagnostics/c20_nested_named_functio.exsc, whose
	; block holds an `AST_FN` (spec §8.6's peek makes a nested named
	; `functio` a diagnostic and `cst/parse.inc` parses it anyway, so there
	; is no `Error` node to stop on). `__chk_ty_stmt` now names the item
	; kinds explicitly; this arm no longer completes a cycle.
	mov	rdi, rbx
	call	__chk_ty_unit
	jmp	.out
  .err:
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.out

  ; ---- literals ------------------------------------------------------------
  .lit:
	cmp	qword [chkt_up], AST_LIT_STRING
	jne	.lit_int
	mov	rdi, rbx
	mov	rsi, AST_TY_TEXTUS
	call	__chk_ty_simple
	jmp	.out
  .lit_int:
	; the VALUE now, the WIDTH later -- see this file's header
	mov	rdi, rbx
	mov	rsi, r12
	call	chk_ty_litval
	test	rdx, rdx
	jz	.lit_bad
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	mov	rdx, rax
	call	ast_konst_set
	xor	eax, eax		; PENDING
	return
  .lit_bad:
	mov	rdi, rbx
	mov	rsi, 308
	mov	rdx, r12
	call	__chk_ty_raise
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.out

  ; ---- names ---------------------------------------------------------------
  .path:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_path_expr
	jmp	.out

  .member:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_member
	jmp	.out

  .call:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_call
	jmp	.out

  ; ---- operators -----------------------------------------------------------
  .binary:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_binary
	jmp	.out

  .unary:
	mov	rdi, rbx
	mov	rsi, [chkt_ap]
	call	__chk_ty_expr
	mov	r13, rax
	cmp	qword [chkt_up], AST_U_ADDR
	je	.u_addr
	cmp	qword [chkt_up], AST_U_DEREF
	je	.u_deref
	; `-e` -- the operand's own type, and a pending literal stays pending
	mov	rax, r13
	jmp	.out
  .u_addr:
	; `&e` -- typed-ast.md section 2.5's `borrow`
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r12
	call	__chk_ty_settled
	mov	r13, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, AST_TY_BORROW
	mov	rdx, r13
	xor	rcx, rcx
	call	ast_type_una
	jmp	.out
  .u_deref:
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r12
	call	__chk_ty_settled
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_kindof
	cmp	rax, AST_TY_PTR
	je	.u_elem
	cmp	rax, AST_TY_BORROW
	je	.u_elem
	cmp	rax, AST_TY_REF
	je	.u_elem
	cmp	rax, AST_TY_REFC
	je	.u_elem
	cmp	rax, AST_TY_ERROR
	je	.u_err
	mov	rdi, rbx
	mov	rsi, 305
	mov	rdx, r12
	call	__chk_ty_raise
  .u_err:
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.out
  .u_elem:
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, AstType.a
	call	__chk_ty_field
	jmp	.out

  .cast:
	; `e sicut T` (spec §8.6 decision 4). The target type is the answer;
	; whether the two are RELATED is checker.md class E and `[OPEN]` -- see
	; this file's header. A `sicut dyn I poscit {P}` cast's bound is pass
	; 3's `EXS-E0510`, which reads the interned `dyn` type this builds.
	mov	rdi, rbx
	mov	rsi, [chkt_ap]
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, [chkt_bp]
	call	chk_ty_of_node
	jmp	.out

  .index:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_index
	jmp	.out

  .try:
	; `e?` -- spec §6: the operand must be an `eventus`
	mov	rdi, rbx
	mov	rsi, [chkt_ap]
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r12
	call	__chk_ty_settled
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_kindof
	cmp	rax, AST_TY_EVENTUS
	je	.try_ok
	cmp	rax, AST_TY_ERROR
	je	.try_err
	mov	rdi, rbx
	mov	rsi, 305
	mov	rdx, r12
	call	__chk_ty_raise
  .try_err:
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.out
  .try_ok:
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, AstType.a
	call	__chk_ty_field
	jmp	.out

  .lambda:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_lambda
	jmp	.out

  .out:
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__chk_ty_setty
	mov	rax, r13
	return
endp

; __chk_ty_settled(w, t, n) -> rax = `t`, or -- when `t` is 0, a literal
; nothing gave a width -- `EXS-E0308` at `n` and the error type.
; The one place a PENDING literal becomes a diagnostic.
proc __chk_ty_settled, chkt_wS2, chkt_tS2, chkt_nS2
	uses	rbx
	locals
	endl
	mov	rbx, [chkt_wS2]
	mov	rax, [chkt_tS2]
	test	rax, rax
	jnz	.out
	mov	rdi, rbx
	mov	rsi, 308
	mov	rdx, [chkt_nS2]
	call	__chk_ty_raise
	mov	rdi, rbx
	call	__chk_ty_err
	mov	rdi, rbx
	mov	rsi, [chkt_nS2]
	mov	rdx, rax
	call	__chk_ty_setty
  .out:
	return
endp

; __chk_ty_expect(w, n, t) -> rax = node `n`'s type once expectation `t` has
; been pushed into it.
;
; RECURSIVE, because a pending literal can be arbitrarily deep inside an
; expression every one of whose leaves is pending: `1 + 2` has no type of its
; own until its context supplies one, and `redde 1 + 2;` in a `-> u8` function
; must give BOTH literals `u8`. So expectation descends through `Binary` and
; `Unary` nodes that are themselves still pending, and stops at anything with
; a type.
;
; A literal that does not FIT the expected width is `EXS-E0308` -- the second
; half of checker.md class H, and the reason `konst` is written before the
; width is known rather than after.
proc __chk_ty_expect, chkt_wX, chkt_nX, chkt_tX
	uses	rbx, r12, r13
	locals
		slot chkt_kX, dq
		slot chkt_bX, dq
	endl
	mov	rbx, [chkt_wX]
	mov	r12, [chkt_nX]
	test	r12, r12
	jz	.out0
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_ty
	test	rax, rax
	jnz	.out			; already typed: expectation is checked
					; by the caller, never imposed
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_kind
	mov	[chkt_kX], rax
	cmp	rax, AST_LIT
	je	.lit
	cmp	rax, AST_BINARY
	je	.bin
	cmp	rax, AST_UNARY
	je	.un
	jmp	.out0

  .lit:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [chkt_tX]
	call	__chk_ty_fits
	test	eax, eax
	jz	.nofit
	mov	rax, [chkt_tX]
	jmp	.set
  .nofit:
	mov	rdi, rbx
	mov	rsi, 308
	mov	rdx, r12
	call	__chk_ty_raise
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.set

  .bin:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.a]
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bX], rcx
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, [chkt_tX]
	call	__chk_ty_expect
	mov	rdi, rbx
	mov	rsi, [chkt_bX]
	mov	rdx, [chkt_tX]
	call	__chk_ty_expect
	mov	rax, [chkt_tX]
	jmp	.set

  .un:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	rdi, rbx
	mov	rsi, rcx
	mov	rdx, [chkt_tX]
	call	__chk_ty_expect
	mov	rax, [chkt_tX]

  .set:
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__chk_ty_setty
	mov	rax, r13
	return
  .out0:
	xor	eax, eax
	return
  .out:
	return
endp

; __chk_ty_fits(w, litnode, t) -> eax = 1 if the literal's value fits type `t`.
;
; The expected type must be an `int` or a `mensura`: a literal is not a
; `textus` and not a struct, and answering 0 here is what turns
; `firma s: textus = 3;` into `EXS-E0308` at the literal rather than a silent
; retype. Floats are admitted and the value is not range-checked against the
; mantissa -- spec §8.4's literal grammar has no float form yet (`[OPEN]`).
proc __chk_ty_fits, chkt_wG, chkt_nG, chkt_tG
	uses	rbx, r12, r13, r14
	locals
		slot chkt_sG, dq
	endl
	mov	rbx, [chkt_wG]
	mov	r12, [chkt_nG]
	mov	r13, [chkt_tG]
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_kindof
	cmp	rax, AST_TY_INT
	je	.int
	cmp	rax, AST_TY_MENSURA
	je	.mens
	cmp	rax, AST_TY_FLOAT
	je	.yes
	cmp	rax, AST_TY_ERROR
	je	.yes			; no cascade (typed-ast.md section 2.2)
	xor	eax, eax
	return

  .mens:
	mov	r14d, [rbx + ChkTyWork.wpb]
	xor	ecx, ecx		; unsigned
	jmp	.range
  .int:
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, AstType.width
	call	__chk_ty_field
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_signkind
	mov	rcx, rax

  .range:
	; `konst` already holds the value (`__chk_ty_expr`'s `.lit_int`).
	; A `push` across a `call` would leave `rsp` 8 off SysV's sixteen
	; (docs/asm-conventions.md 2), so the sign travels in a slot.
	mov	[chkt_sG], rcx
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_konst_get
	mov	rcx, [chkt_sG]
	test	rcx, rcx
	jnz	.signed
	; unsigned: value < 2^width. 64 is handled first, because `shl` by 64
	; is a no-op on x86 rather than a zero.
	cmp	r14, 64
	jae	.yes
	mov	rcx, r14
	mov	rdx, 1
	shl	rdx, cl
	cmp	rax, rdx
	jb	.yes
	jmp	.no
  .signed:
	; signed: value < 2^(width-1). A negation is a `Unary` above the
	; literal, so the magnitude is what is checked here and `-128: i8` is
	; admitted by the same bound as `127: i8` -- a known, recorded
	; approximation of spec §5.2's range.
	cmp	r14, 64
	jae	.yes
	mov	rcx, r14
	dec	rcx
	mov	rdx, 1
	shl	rdx, cl
	cmp	rax, rdx
	ja	.no
	jmp	.yes
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_signkind(w, t) -> eax = 1 if type `t` is a SIGNED integer.
proc __chk_ty_signkind, chkt_wH, chkt_tH
	locals
	endl
	mov	rsi, [chkt_tH]
	test	rsi, rsi
	jz	.no
	mov	rdi, [chkt_wH]
	mov	rdi, [rdi + ChkTyWork.wtre]
	mov	rax, [rdi + Ast.types.len]
	cmp	rsi, rax
	ja	.no
	call	ast_type_at
	movzx	eax, byte [rax + AstType.sign]
	cmp	eax, AST_SIGN_I
	jne	.no
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_want(w, n, t) -- type expression `n` and require it to be `t`.
; `EXS-E0303` at `n` on a mismatch. Compatibility is EQUALITY OF INTERNED IDS
; (checker.md section 1.5: "No implicit conversion anywhere"), with the ERROR
; type compatible with everything so that one failure does not cascade.
proc __chk_ty_want, chkt_wW, chkt_nW, chkt_tW
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wW]
	mov	r12, [chkt_nW]
	mov	r13, [chkt_tW]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_expr
	test	rax, rax
	jnz	.have
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__chk_ty_expect
  .have:
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r12
	call	__chk_ty_settled
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r13
	mov	rcx, r12
	call	__chk_ty_same
	return
endp

; __chk_ty_same(w, got, want, n) -> rax = `got`; `EXS-E0303` at `n` when the
; two interned ids differ and neither is the error type.
proc __chk_ty_same, chkt_wY, chkt_gY, chkt_tY, chkt_nY
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkt_wY]
	mov	r12, [chkt_gY]
	mov	rax, [chkt_tY]
	cmp	r12, rax
	je	.out
	cmp	r12, 1
	je	.out			; AST_TY_ERROR: already diagnosed
	cmp	rax, 1
	je	.out
	test	rax, rax
	jz	.out
	mov	rdi, rbx
	mov	rsi, 303
	mov	rdx, [chkt_nY]
	call	__chk_ty_raise
  .out:
	mov	rax, r12
	return
endp

; __chk_ty_path_expr(w, n) -> rax = the type a `Path` in EXPRESSION position
; denotes. `Path.d` is pass 1's answer, or -- for a prelude name -- this pass's
; tagged one.
proc __chk_ty_path_expr, chkt_wA, chkt_nA
	uses	rbx, r12, r13, r14
	locals
		slot chkt_sA, dq
		slot chkt_yA, dq
	endl
	mov	rbx, [chkt_wA]
	mov	r12, [chkt_nA]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_seg0
	mov	[chkt_sA], rax
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_ty_node
	mov	r13d, [rax + AstNode.d]

	test	r13, r13
	jnz	.have
	; pass 1 already raised `EXS-E0301` here (its `__chk_path`, expression
	; position) -- unless the name is the prelude's, which pass 1 could not
	; know. `chk_ty_prelude_lookup` is the hook it left; if the wiring in
	; `chk_prelude_lookup` is in place this branch never sees a prelude
	; name, and if it is not, this is where the difference shows.
	mov	rdi, rbx
	mov	rsi, [chkt_sA]
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	rdi, rbx
	mov	rsi, rcx
	lea	rdx, [chk_ty_pre_names]
	mov	rcx, CHK_TY_PRE_NAMES_LEN
	call	__chk_ty_pool
	cmp	rax, CHK_TY_PRE_SCRIPTOR
	jne	.unres
	mov	r13, CHK_TY_PRELUDE or CHK_TY_PRE_SCRIPTOR
	mov	rdi, rbx
	mov	rsi, [chkt_sA]
	mov	rdx, r13
	call	__chk_ty_setd
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__chk_ty_setd
	jmp	.have
  .unres:
	mov	rdi, rbx
	call	__chk_ty_err
	jmp	.setseg

  .have:
	mov	rdi, r13
	call	__chk_ty_is_pre
	test	eax, eax
	jz	.decl
	mov	rdi, rbx
	mov	rsi, r13
	and	rsi, CHK_TY_PRE_MASK
	call	chk_ty_prelude_type
	jmp	.setseg

  .decl:
	; spec §5.4 / `EXS-E0341`: an accumulator is WRITE-ONLY inside its own
	; iteration. `__chk_ty_assign` types its target without coming through
	; here, so every path that reaches this point is a READ.
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_decl_at
	movzx	r14d, byte [rax + AstDecl.kind]
	mov	eax, [rax + AstDecl.ty]
	mov	[chkt_yA], rax		; a slot, not a `push`: two `call`s follow
					; and SysV wants `rsp` 16-aligned at each
	cmp	r14d, AST_D_ACCUM
	jne	.notacc
	cmp	dword [rbx + ChkTyWork.wcon], 0
	je	.notacc
	mov	rdi, rbx
	mov	rsi, 341
	mov	rdx, r12
	call	__chk_ty_raise
  .notacc:
	; `EXS-E0307`, definite assignment (spec §13's note): a binding declared
	; without an initializer and read before any assignment.
	cmp	r14d, AST_D_BINDING
	jne	.notdef
	lea	rdi, [rbx + ChkTyWork.wdef]
	mov	rsi, r13
	call	vec_get
	movzx	ecx, byte [rax]
	cmp	ecx, CHK_TY_D_UNSET
	jne	.notdef
	mov	rdi, rbx
	mov	rsi, 307
	mov	rdx, r12
	call	__chk_ty_raise
	lea	rdi, [rbx + ChkTyWork.wdef]
	mov	rsi, r13
	call	vec_get
	mov	byte [rax], CHK_TY_D_SET	; report once, not once per read
  .notdef:
	mov	rax, [chkt_yA]
	test	rax, rax
	jnz	.setseg
	mov	rdi, rbx
	call	__chk_ty_err

  .setseg:
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, [chkt_sA]
	mov	rdx, r14
	call	__chk_ty_setty
	mov	rax, r14
	return
endp

; __chk_ty_index(w, n) -> rax = the type of `base[i]`.
;
; `EXS-E0311` IS FIRST AND THE INDEX IS NOT TYPED. checker.md section 2.6:
; "`t[0]` with `t: textus` is `EXS-E0311` at the `Index` node BEFORE the index
; is typed" -- spec §5.1's whole point is that `textus` has no integer index at
; all, so a diagnostic about the index would be about the wrong thing.
proc __chk_ty_index, chkt_wN, chkt_nN
	uses	rbx, r12, r13, r14
	locals
		slot chkt_aN, dq
		slot chkt_bN, dq
	endl
	mov	rbx, [chkt_wN]
	mov	r12, [chkt_nN]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_aN], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bN], rcx

	mov	rdi, rbx
	mov	rsi, [chkt_aN]
	call	__chk_ty_expr
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_aN]
	call	__chk_ty_settled
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_kindof
	mov	r14, rax

	cmp	r14, AST_TY_TEXTUS
	je	.e0311
	cmp	r14, AST_TY_ACIES
	je	.acies
	cmp	r14, AST_TY_ERROR
	je	.err
	; spec §5.1: `octeti` `scalares` `grapha` are VIEWS with methods, not
	; indexable aggregates; nothing else in §5 defines `[]` at all.
	mov	rdi, rbx
	mov	rsi, 305
	mov	rdx, r12
	call	__chk_ty_raise
	jmp	.err

  .e0311:
	mov	rdi, rbx
	mov	rsi, 311
	mov	rdx, r12
	call	__chk_ty_raise
	; and the index is NOT typed -- see this routine's header
	mov	rdi, rbx
	call	__chk_ty_err
	return

  .acies:
	mov	rdi, rbx
	call	__chk_ty_mensura
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, [chkt_bN]
	mov	rdx, r14
	call	__chk_ty_want
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, AstType.a
	call	__chk_ty_field
	return

  .err:
	mov	rdi, rbx
	mov	rsi, [chkt_bN]
	call	__chk_ty_expr
	mov	rdi, rbx
	call	__chk_ty_err
	return
endp

; __chk_ty_binary(w, n) -> rax = the type of a binary operator application.
proc __chk_ty_binary, chkt_wB, chkt_nB
	uses	rbx, r12, r13, r14
	locals
		slot chkt_aB, dq
		slot chkt_bB, dq
		slot chkt_oB, dq
		slot chkt_lB, dq
		slot chkt_rB, dq
	endl
	mov	rbx, [chkt_wB]
	mov	r12, [chkt_nB]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	movzx	ecx, word [rax + AstNode.aux]
	mov	[chkt_oB], rcx
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_aB], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bB], rcx

	mov	rdi, rbx
	mov	rsi, [chkt_aB]
	call	__chk_ty_expr
	mov	[chkt_lB], rax
	mov	rdi, rbx
	mov	rsi, [chkt_bB]
	call	__chk_ty_expr
	mov	[chkt_rB], rax

	; one side pending, the other settled: the settled one is the expectation
	cmp	qword [chkt_lB], 0
	jne	.l_ok
	cmp	qword [chkt_rB], 0
	je	.both_pending
	mov	rdi, rbx
	mov	rsi, [chkt_aB]
	mov	rdx, [chkt_rB]
	call	__chk_ty_expect
	mov	[chkt_lB], rax
  .l_ok:
	cmp	qword [chkt_rB], 0
	jne	.settled
	mov	rdi, rbx
	mov	rsi, [chkt_bB]
	mov	rdx, [chkt_lB]
	call	__chk_ty_expect
	mov	[chkt_rB], rax
	jmp	.settled

  .both_pending:
	; `1 + 2` in a position that supplies no expectation. The result is
	; PENDING too, so an enclosing `redde` or annotation can still settle
	; both -- `__chk_ty_expect`'s `.bin` arm is the other half of this.
	mov	rcx, [chkt_oB]
	cmp	rcx, AST_OP_VEL
	je	.logical
	cmp	rcx, AST_OP_ET
	je	.logical
	cmp	rcx, AST_OP_LT
	jb	.pend
	cmp	rcx, AST_OP_NE
	jbe	.cmp_bad
  .pend:
	xor	eax, eax
	return
  .cmp_bad:
	mov	rdi, rbx
	mov	rsi, 308
	mov	rdx, r12
	call	__chk_ty_raise
	mov	rdi, rbx
	call	__chk_ty_err
	return

  .settled:
	mov	r13, [chkt_lB]
	mov	r14, [chkt_rB]
	mov	rdi, rbx
	mov	rsi, r14
	mov	rdx, r13
	mov	rcx, [chkt_bB]
	call	__chk_ty_same		; both operands, one type (spec §4.5)

	mov	rcx, [chkt_oB]
	cmp	rcx, AST_OP_VEL
	je	.logical
	cmp	rcx, AST_OP_ET
	je	.logical
	cmp	rcx, AST_OP_LT
	jb	.arith
	cmp	rcx, AST_OP_NE
	jbe	.relational
	cmp	rcx, AST_OP_RANGE
	je	.range
  .arith:
	; `+ +% +| - -% -| *` -- the operand type, unchanged
	mov	rax, r13
	return
  .relational:
	mov	rdi, rbx
	call	__chk_ty_u1
	return
  .range:
	; `a..b` -- spec §8.5's iteration bound; the element type is the
	; operands'. Used by `per i in a..b` and by `sectio(0..ubi)`.
	mov	rax, r13
	return

  .logical:
	; spec §8.6 level 4/5: `vel` and `et` are over `u1`, which spec §5.1
	; makes the boolean.
	mov	rdi, rbx
	call	__chk_ty_u1
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, [chkt_aB]
	mov	rdx, r13
	call	__chk_ty_wantty
	mov	rdi, rbx
	mov	rsi, [chkt_bB]
	mov	rdx, r13
	call	__chk_ty_wantty
	mov	rax, r13
	return
endp

; __chk_ty_wantty(w, n, t) -- an already-typed node must be `t`. Used where
; the operand has been typed once already and re-typing it would double every
; diagnostic inside it.
proc __chk_ty_wantty, chkt_wV, chkt_nV, chkt_tV
	uses	rbx
	locals
	endl
	mov	rbx, [chkt_wV]
	mov	rdi, rbx
	mov	rsi, [chkt_nV]
	call	__chk_ty_ty
	test	rax, rax
	jnz	.have
	mov	rdi, rbx
	mov	rsi, [chkt_nV]
	mov	rdx, [chkt_tV]
	call	__chk_ty_expect
  .have:
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chkt_tV]
	mov	rcx, [chkt_nV]
	call	__chk_ty_same
	return
endp

include 'member.inc'
include 'stmt.inc'

; =============================================================================
; the pass
; =============================================================================

; chk_types(chk_c) -- STAGE 2, PASS 2. `checker/checker.inc` calls this
; between `chk_resolve` and `chk_rows`, and the `CHK_S_TYPES` bit this ORs in
; on the way out is what un-gates passes 3 and 4 there.
proc chk_types, chkt_c9
	uses	rbx, r12, r13
	locals
		slot chkt_w9, dq
	endl
	mov	rbx, [chkt_c9]

	mov	rdi, [rbx + ChkCtx.arena]
	mov	rsi, sizeof.ChkTyWork
	call	arena_alloc
	mov	[chkt_w9], rax
	mov	rdi, rax
	mov	rsi, rbx
	call	chk_ty_work_init
	mov	r12, [chkt_w9]

	; the side tables (`own`, `konst`, `layout`) -- typed-ast.md section
	; 2.2. Pass 4 allocates them too if pass 2 did not, and `ast_side_alloc`
	; traps on a second call, so both sites test the flag.
	mov	r13, [r12 + ChkTyWork.wtre]
	mov	eax, [r13 + Ast.sides]
	test	eax, eax
	jnz	.have_sides
	mov	rdi, r13
	call	ast_side_alloc
  .have_sides:

	mov	rdi, r12
	call	chk_ty_sigs		; phases A-E

	mov	rdi, r12
	call	__chk_ty_defsetup

	mov	rdi, r12
	call	__chk_ty_bodies		; the one walk

	mov	rdi, r12
	call	__chk_ty_entry		; spec §4.7's `initium`, signature half

	mov	rdi, r12
	call	__chk_ty_memres		; `AST_F_MEMRES` (ssa-ir.md 2.5)

	or	dword [rbx + ChkCtx.stages], CHK_S_TYPES
	return
endp

; __chk_ty_bodies(w) -- the walk. Every `Fn` and every module-level `Binding`,
; in DECLARATION order, so that diagnostics come out in an order that is a
; function of the source alone (checker.md section 4).
proc __chk_ty_bodies, chkt_wZ
	uses	rbx, r12, r13, r14
	locals
		slot chkt_nZ, dq
	endl
	mov	rbx, [chkt_wZ]
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_decl_count
	mov	[chkt_nZ], rax
	mov	r12, 1
  .decl:
	cmp	r12, [chkt_nZ]
	ja	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	r14d, byte [rax + AstDecl.kind]
	mov	r13d, [rax + AstDecl.node]
	mov	ecx, [rax + AstDecl.parent]
	cmp	r14d, AST_D_FN
	je	.fn
	cmp	r14d, AST_D_MEMBER
	je	.fn
	cmp	r14d, AST_D_BINDING
	je	.binding
	jmp	.next

  .fn:
	test	r13, r13
	jz	.next
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__chk_ty_body
	jmp	.next

  .binding:
	; only a MODULE-level binding; one inside a function is reached through
	; that function's block, in statement order, which is where definite
	; assignment and shadowing are meaningful.
	test	ecx, ecx
	jnz	.next
	test	r13, r13
	jz	.next
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_stmt
  .next:
	inc	r12
	jmp	.decl
  .out:
	return
endp

; __chk_ty_body(w, d, fnnode) -- one function body, with the enclosing-function
; state saved and restored around it.
proc __chk_ty_body, chkt_wC, chkt_dC, chkt_fC
	uses	rbx, r12, r13, r14
	locals
		slot chkt_sC, dq
		slot chkt_vR, dq
		slot chkt_vF, dq
		slot chkt_vD, dq
		slot chkt_bC, dq
	endl
	mov	rbx, [chkt_wC]
	mov	r12, [chkt_fC]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_node
	mov	ecx, [rax + AstNode.a]
	mov	[chkt_sC], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chkt_bC], rcx
	cmp	qword [chkt_bC], 0
	je	.out			; an interface member or an `externus`
					; signature: no body to type

	mov	eax, [rbx + ChkTyWork.wres]
	mov	[chkt_vR], rax
	mov	eax, [rbx + ChkTyWork.wfnd]
	mov	[chkt_vF], rax
	mov	eax, [rbx + ChkTyWork.wredd]
	mov	[chkt_vD], rax

	mov	rdi, rbx
	mov	rsi, [chkt_dC]
	call	__chk_ty_result
	mov	r13, rax
	mov	[rbx + ChkTyWork.wres], r13d
	mov	eax, [chkt_dC]
	mov	[rbx + ChkTyWork.wfnd], eax
	mov	dword [rbx + ChkTyWork.wredd], 0

	mov	rdi, rbx
	mov	rsi, [chkt_bC]
	call	__chk_ty_stmt

	; `EXS-E0307` -- spec §8.5, checker.md class G: "a non-`unit` function
	; that can fall off its end". The straight-line form: a body that
	; contains no `redde` at all cannot produce a value. A body whose LAST
	; statement is not a `redde` but which returns on every branch is
	; accepted; a full reachability analysis needs the CFG, which is Stage
	; 3's (`docs/design/lowering.md`), and is `[OPEN]`.
	mov	rdi, rbx
	call	__chk_ty_unit
	cmp	r13, rax
	je	.restore
	cmp	dword [rbx + ChkTyWork.wredd], 0
	jne	.restore
	cmp	r13, 1
	je	.restore		; the error type: already diagnosed
	mov	rdi, rbx
	mov	rsi, 307
	mov	rdx, r12
	call	__chk_ty_raise

  .restore:
	mov	rax, [chkt_vR]
	mov	[rbx + ChkTyWork.wres], eax
	mov	rax, [chkt_vF]
	mov	[rbx + ChkTyWork.wfnd], eax
	mov	rax, [chkt_vD]
	mov	[rbx + ChkTyWork.wredd], eax
  .out:
	return
endp

; __chk_ty_result(w, d) -> rax = the RESULT type of function declaration `d`,
; read off its interned `fn` type: `a` is the `extra` run of `width` parameter
; types followed by the result (ast/types.inc's `ast_type_fn`).
proc __chk_ty_result, chkt_wT, chkt_dT
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wT]
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_dT]
	call	ast_decl_at
	mov	r12d, [rax + AstDecl.ty]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_ty_kindof
	cmp	rax, AST_TY_FN
	jne	.none
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AstType.a
	call	__chk_ty_field
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AstType.width
	call	__chk_ty_field
	add	r13, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r13
	call	ast_extra_at
	return
  .none:
	mov	rdi, rbx
	call	__chk_ty_err
	return
endp

; __chk_ty_entry(w) -- spec §4.7 / `EXS-E0424`, the SIGNATURE half.
;
; Pass 1 raised `E0424` for "two `initium`" and for "none where a program was
; asked for"; §4.7 also pins the shape -- `publica functio initium(m: Mundus)
; -> u8` -- and that needs types, so it is here. `ChkCtx.mainid` is the first
; `initium` pass 1 found; 0 means the module has none, which pass 1 has already
; reported or deliberately not (a module with no `initium` is a LIBRARY).
proc __chk_ty_entry, chkt_wU
	uses	rbx, r12, r13, r14
	locals
		slot chkt_tU, dq
	endl
	mov	rbx, [chkt_wU]
	mov	r12, [rbx + ChkTyWork.wcx]
	mov	r12d, [r12 + ChkCtx.mainid]
	test	r12, r12
	jz	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_FN
	jne	.out
	mov	r14d, [rax + AstDecl.node]
	movzx	ecx, byte [rax + AstDecl.flags]
	mov	r13d, [rax + AstDecl.ty]
	test	ecx, AST_F_PUBLICA
	jz	.bad

	; exactly one parameter, of type `Mundus`
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, AstType.width
	call	__chk_ty_field
	cmp	rax, 1
	jne	.bad
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, AstType.a
	call	__chk_ty_field
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_extra_at
	mov	[chkt_tU], rax
	mov	rdi, rbx
	mov	rsi, AST_CAP_MUNDUS
	call	__chk_ty_atom
	cmp	rax, [chkt_tU]
	jne	.bad

	; the result is `u8`
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_resultof
	mov	[chkt_tU], rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, AST_SIGN_U
	mov	rdx, 8
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	cmp	rax, [chkt_tU]
	jne	.bad
	jmp	.out
  .bad:
	mov	rdi, rbx
	mov	rsi, 424
	mov	rdx, r14
	call	__chk_ty_raise
  .out:
	return
endp

; __chk_ty_resultof(w, fnty) -> rax = the result type of an `fn` type id.
proc __chk_ty_resultof, chkt_wQ, chkt_tQ
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkt_wQ]
	mov	rdi, rbx
	mov	rsi, [chkt_tQ]
	call	__chk_ty_kindof
	cmp	rax, AST_TY_FN
	jne	.none
	mov	rdi, rbx
	mov	rsi, [chkt_tQ]
	mov	rdx, AstType.a
	call	__chk_ty_field
	mov	r12, rax
	mov	rdi, rbx
	mov	rsi, [chkt_tQ]
	mov	rdx, AstType.width
	call	__chk_ty_field
	add	r12, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_extra_at
	return
  .none:
	mov	rdi, rbx
	call	__chk_ty_err
	return
endp

; __chk_ty_memres(w) -- `AST_F_MEMRES`, ssa-ir.md 2.5's memory_resident:
; a declaration whose type is NOT a scalar, or whose address was taken (pass
; 1's `AST_F_ADDRTAKEN`). Stage 3 reads it to decide what needs a stack slot.
proc __chk_ty_memres, chkt_wM
	uses	rbx, r12, r13
	locals
		slot chkt_nM, dq
	endl
	mov	rbx, [chkt_wM]
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_decl_count
	mov	[chkt_nM], rax
	mov	r12, 1
  .decl:
	cmp	r12, [chkt_nM]
	ja	.out
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.flags]
	mov	r13d, [rax + AstDecl.ty]
	test	ecx, AST_F_ADDRTAKEN
	jnz	.set
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_ty_kindof
	cmp	rax, AST_TY_INT
	je	.next
	cmp	rax, AST_TY_FLOAT
	je	.next
	cmp	rax, AST_TY_MENSURA
	je	.next
	cmp	rax, AST_TY_PTR
	je	.next
	cmp	rax, AST_TY_REF
	je	.next
	cmp	rax, AST_TY_REFC
	je	.next
	cmp	rax, AST_TY_BORROW
	je	.next
	cmp	rax, AST_TY_CAP
	je	.next
	test	rax, rax
	jz	.next			; no type at all -- nothing to place
	cmp	rax, AST_TY_FN
	je	.next
  .set:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	mov	rdx, AST_F_MEMRES
	call	ast_decl_flag
  .next:
	inc	r12
	jmp	.decl
  .out:
	return
endp
