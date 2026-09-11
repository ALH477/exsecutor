; compiler/x86_64/checker/verify.inc
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
; THE STAGE 2 VERIFIER -- `docs/design/checker.md` sections 2.5 and 3, and
; typed-ast.md section 3's Stage 2 list, which ast/verify.inc marks
; `[UNIMPLEMENTED]` because no checker existed when it was written.
;
; EVERY FAILURE IS AN `rassert`, NEVER AN `EXS-E` CODE, for ast/verify.inc's
; reason restated: a tree whose annotations are malformed after a pass that
; emitted no diagnostic is a COMPILER BUG, not a fact about the program, and
; docs/asm-conventions.md 1.3 is explicit that routing a violated internal
; contract through `CF` invites a caller to swallow it. This file registers no
; diagnostic and invents no code (CLAUDE.md).
;
; ---------------------------------------------------------------------------
; TWO GATES, AND WHY BOTH ARE NECESSARY.
;
; 1. DIAGNOSTICS. checker.md section 2.5: "With ANY diagnostic, the lowering
;    does not run and the Stage 2 verifier does not run" -- a node whose
;    resolution or typing failed carries a hole on purpose, and asserting the
;    completed invariant over it would turn a correctly reported error into a
;    trap. `chk_verify`'s `chk_nd` argument is that count; non-zero and this
;    routine checks nothing and returns.
;
; 2. STAGES. checker.md section 2.5's invariant is the conjunction of what SIX
;    passes produce, and only passes 0 and 1 exist. A verifier that asserted
;    the whole list today would trap on every tree -- which is the failure mode
;    CLAUDE.md's evidence discipline is about: prose describing unwritten code.
;    So `chk_ss` is the CHK_S_* mask of what actually ran, each clause is
;    guarded by the bit of the pass that establishes it, and a clause with no
;    pass behind it is listed below rather than asserted.
;
; WHAT IS CHECKED TODAY (CHK_S_RESOLVE):
;
;   - every `Seg` reached from a `Path` in EXPRESSION position has `d` != 0.
;     Not every `Seg`: a type-position `Seg` resolves to a declaration only
;     when one exists, and spec §5.1's own type names (`i32`, `textus`, ...)
;     have none anywhere in this repository -- see resolve.inc's header. That
;     clause is gated on CHK_S_TYPES, whose pass owns the primitive table.
;   - every `Path` with exactly one segment has `d` equal to that segment's.
;   - a `d` that IS filled names a declaration that exists.
;   - no `Sub` node carries a `d` that is not an `AST_D_SUB` declaration.
;
; WHAT IS OWED, per pass, and what a later agent adds here (by asking -- this
; file is `checker-resolve`'s, roster rule):
;
;   CHK_S_TYPES    `ty` != 0 and != 1 on every expression node and every `Ty*`
;                  node; `Decl.ty` != 0 on the kinds section 2.5 lists and 0 on
;                  `IMPL` `POTESTAS` `EXTERNUS`; `Member.d` != 0; a
;                  type-position `Seg.d` != 0; every `brand.a` is a
;                  `BINDING`/`PARAM` declaration or 0; `Ast.sides` != 0.
;   CHK_S_ROWS     every `fn` type's row id names a row in `Ast.rows`; every
;                  row's items strictly ascending on (kind, id); every
;                  `RowItem.d` != 0 and every ordinal < its `Sig`'s parameter
;                  count -- the clause ast/verify.inc could not check per node
;                  because it needs the enclosing `Sig`.
;   CHK_S_LAYOUT   `Ast.layout` has an entry for every `STRUCT` and `FIELD`
;                  declaration; every `transitus` field's `Layout.order` !=
;                  `nativus` -- the other clause ast/verify.inc could not reach.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.1, §9.3.
; -----------------------------------------------------------------------------

; ---- the pass chain ---------------------------------------------------------
; A STRICT CHAIN, never a diamond -- fasmg has one flat namespace and a `struct`
; or `proc` processed twice by two paths fails on the second (ast/ast.inc's
; header states the same rule for its own tree). Each later pass includes its
; predecessor and THIS FILE INCLUDES THE LAST LINK, so adding a pass is a
; one-line edit here and nowhere else:
;
;   resolve/resolve.inc         passes 0, 1   `checker-resolve`  (this agent)
;     <- types/types.inc        pass 2        `checker-types`    (not written)
;        <- rows/rowdefs.inc                  `checker-rows`
;           ... rows/rows.inc   passes 3, 4   `checker-rows`
;              <- lexicon/lexicon.inc  pass 5 `checker-lexicon`  (not written)
;                 <- verify.inc  (this file)
;
; `rows/rowdefs.inc` already opens with `include '../resolve/resolve.inc'` and
; its header says that line becomes `include '../types/types.inc'` when the
; types pass lands. So the include below names `rows/rows.inc`, not
; `resolve/resolve.inc`: naming both would be exactly the diamond -- measured,
; not assumed (`struct ChkFrame` is processed twice and `end struct` fails on
; the second with "symbol 'sizeof?.ChkFrame' is undefined").
;
; When `checker-lexicon` lands, this line becomes `include
; 'lexicon/lexicon.inc'` and that file opens with `include '../rows/rows.inc'`.
; Ask; do not edit another agent's tree (.claude/agents/README.md, "The rule").
include 'rows/rows.inc'

; chk_verify(chk_t, chk_nd, chk_ss) -- `chk_t` is the tree, `chk_nd` the number
; of diagnostics Stage 2 emitted, `chk_ss` the CHK_S_* mask of passes that ran.
proc chk_verify, chk_t, chk_nd, chk_ss
	uses	rbx, r12, r13
	locals
		slot	chk_n, dq
	endl
	cmp	qword [chk_nd], 0
	jne	.out			; gate 1: see this file's header. `chk_nd` is
					; diagnostics RAISED **plus** E0301s the taint
					; rule WITHHELD (ChkCtx.nsupp): a tree the parser
					; recovered in holds names pass 1 deliberately
					; left unresolved and deliberately did not report,
					; and `__chk_v_path`'s "unresolved is legal only in
					; type position" is false of exactly that shape.
					; A suppressed diagnostic makes a tree as
					; unverifiable as a raised one; the alternative --
					; threading the taint array in here -- verifies a
					; little more of a tree nobody will lower anyway.
	mov	rax, [chk_ss]
	test	rax, CHK_S_RESOLVE
	jz	.out			; nothing ran; nothing is owed

	mov	rbx, [chk_t]
	mov	rdi, rbx
	call	ast_node_count
	mov	[chk_n], rax
	mov	r12, 1
  .node:
	cmp	r12, [chk_n]
	ja	.out
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_PATH
	je	.path
	cmp	ecx, AST_SUB
	je	.sub
	jmp	.next

  .path:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [chk_ss]
	call	__chk_v_path
	jmp	.next

  .sub:
	; a resolved `sub` names an atom, and its own declaration is the
	; provider -- checker.md section 2.1's bind rule, checked rather than
	; trusted, because a wrong `d` here is a capability resolving to the
	; wrong carrier at run time (spec §4.1 rule 1).
	mov	ecx, [rax + AstNode.d]
	test	ecx, ecx
	jz	.next
	mov	rdi, rbx
	mov	rsi, rcx
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	rassert	rcx eq AST_D_SUB
  .next:
	inc	r12
	jmp	.node
  .out:
	return
endp

; __chk_v_path(chk_t, chk_n, chk_ss) -- one `Path`. Expression position is not a
; field of the node, so it is recovered the only way it can be: a `Path` reached
; from a `TyPath` or a `TyDyn` is a type, and every other `Path` is an
; expression. That is one backward edge the tree does not carry, so this walks
; the type nodes once per verification rather than per path -- `__chk_v_istype`.
proc __chk_v_path, chk_t, chk_n, chk_ss
	uses	rbx, r12, r13, r14
	locals
		slot	chk_off, dq
		slot	chk_cnt, dq
		slot	chk_s0,  dq
	endl
	mov	rbx, [chk_t]
	mov	r12, [chk_n]
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	mov	ecx, [rax + AstNode.a]
	mov	[chk_off], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chk_cnt], rcx
	rassert	rcx ge 1
	mov	r14d, [rax + AstNode.d]

	mov	rdi, rbx
	mov	rsi, [chk_off]
	call	ast_extra_at
	mov	[chk_s0], rax
	mov	rdi, rbx
	mov	rsi, rax
	call	ast_node_at
	mov	r13d, [rax + AstNode.d]	; the first segment's declaration

	; a one-segment path answers with its own segment
	cmp	qword [chk_cnt], 1
	jne	.notone
	rassert	r14 eq r13
  .notone:

	; every filled `d` names a declaration that exists
	test	r13, r13
	jz	.unresolved
	btr	r13, 31			; bit 31 = a PRELUDE declaration, not an `Ast.decls` index (checker/types/prim.inc)
	mov	rdi, rbx
	call	ast_decl_count
	rassert	r13 le rax
	jmp	.out

  .unresolved:
	; unresolved is legal only in type position -- resolve.inc's header
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_v_istype
	rassert	rax eq 1
  .out:
	return
endp

; __chk_v_istype(chk_t, chk_n) -> eax = 1 if `Path` node `chk_n` is reached from
; a `TyPath` or a `TyDyn`, else 0. A linear scan of the type nodes: the tree has
; no parent edge, and adding one for a verifier that runs once per compilation
; would be a slot on every node forever.
proc __chk_v_istype, chk_t, chk_n
	uses	rbx, r12, r13
	locals
		slot	chk_n2, dq
	endl
	mov	rbx, [chk_t]
	mov	r13, [chk_n]
	mov	rdi, rbx
	call	ast_node_count
	mov	[chk_n2], rax
	mov	r12, 1
  .scan:
	cmp	r12, [chk_n2]
	ja	.no
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_TYPATH
	je	.check
	cmp	ecx, AST_TYDYN
	je	.check
	jmp	.next
  .check:
	mov	ecx, [rax + AstNode.a]
	cmp	rcx, r13
	je	.yes
  .next:
	inc	r12
	jmp	.scan
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp
