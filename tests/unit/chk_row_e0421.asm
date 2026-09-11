; tests/unit/chk_row_e0421.asm
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
; checker-rows fixture for pass 3: `EXS-E0421`, and `EXS-E0501`.
;
; THE PAIR IS SPEC §4.2's OWN EXAMPLE, and the pairing is what makes it
; evidence: §4.2 says so itself -- "a checker that rejects the violation AND
; the correct version is not a fix, and an earlier attempt at this change did
; exactly that". So the same tree is run twice, differing in ONE dword: the
; row carried by the second argument's TYPE.
;
;     publica functio applica(v: f32, f: functio(f32) -> f32) -> f32
;             poscit alloc, sicut f { ... }
;     publica functio exterior(v: f32) -> f32 poscit alloc {
;         redde applica(v, nocens)          // nocens: poscit rete
;     }
;
; `prototypes/capcheck/cases/bad_laundering.exsc` -> `EXS-E0421`, spec §14
; entry 10; `cases/ok_hof_subset.exsc` (the argument's row is `alloc`, a
; subset of the caller's declared row) -> accepted.
;
;   1. The laundering shape is rejected with exactly one `EXS-E0421`, and the
;      span is THE CALL -- not `nocens`, not `applica` -- because that is the
;      drawing node (checker.md section 2.4's table).
;   2. The subset shape, one dword different, is accepted with no diagnostic.
;      Non-vacuity: the checker is not rejecting every call it sees.
;   3. Substitution is what does it. With the ordinal removed from
;      `applica`'s declared row, the same laundering tree is ACCEPTED -- spec
;      §4.2's "substitution is what makes laundering impossible rather than
;      merely annotated", stated as a difference this fixture can see.
;   4. `EXS-E0501` (spec §4.1 rule 7, §4.3): a module-level binding whose
;      type is capability-BEARING -- a struct with a `rete` field, reached
;      transitively -- and its twin, the same binding of a struct with no
;      capability field, accepted. §14 entry 11.
;   5. `AST_F_CAPBEAR` is set on the bearing struct and not on the other one.
;   6. Spec §4.1 rule 5's "transitively", which is the whole reason pass 3 is
;      a fixpoint and not a walk: a PRIVATE function between the laundering
;      call and the declared caller infers `{alloc, rete}`, and the caller --
;      which names neither `rete` nor the function that needs it -- is the one
;      rejected. Two rounds; one walk of the tree sees nothing.
;   7. Spec §4.2 AS AMENDED -- "what 'the row carried by a type' means, for
;      every type that carries one". `poscit sicut s` with `s` a capability-
;      BEARING `structura` substitutes that type's MARK, so a caller declaring
;      nothing is `EXS-E0421`; and the twin, one dword different, points the
;      same `sicut` at a struct that bears nothing, substitutes the EMPTY row,
;      and is accepted. That pair is the whole amendment: before it both sides
;      were rejected, because a struct's type carried no row at all and the
;      ordinal survived.
;   8. A leftover ordinal is `EXS-E0423`, not `E0421` (checker.md section
;      2.4's class L: "a substituted row still carrying an ordinal"). Third
;      twin of the same tree: the argument's type slot is cleared, so there is
;      nothing to substitute FROM, and the code changes with it.
;   9. A PRELUDE-TAGGED callee `d` -- bit 31 set, `checker/types/prim.inc`'s
;      `CHK_TY_PRELUDE`, which is what `m.ambitus()` carries -- does not reach
;      `ast_decl_at`. Before the test for that bit, this fixture trapped
;      (exit 132) rather than failing a check.
;  10. The same tag in a `struct` TYPE's `a`: the prelude's `Scriptor` has no
;      `AstDecl`, so the §4.3 fixpoint computes no mark for it and `wbear` has
;      no entry to read -- yet `sicut s` over it must still substitute
;      `{ambitus}`. Without the tagged-`a` arm this check traps too; with the
;      arm and an empty mark it silently ACCEPTS, which is what
;      `examples/initium.exsc` cannot tell apart (that file provides `ambitus`
;      with a `sub`, so both answers compile clean).
;
; `[UNTESTED]` on real source: pass 3 reads `Decl.ty` and `Node.ty`, which the
; types pass fills, and `Path.d`, which the resolve pass fills. Neither
; exists, so this fixture fills them by hand -- the arrangement checker.md
; section 6 prescribes.
;
; Exit 0 = all checks passed; 10+N = check N failed (tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 8 * 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_scr]
	mov	rsi, 2 * 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_iar]
	mov	rsi, 256 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_iar]
	mov	rdx, 64
	call	intern_init

	; =================== 1: the laundering shape ===================
	call	fx_build_hof
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail1
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 421
	jne	.fail1
	cmp	rdx, 4242			; the span really is the CALL's
	jne	.fail1

	; =================== 2: one dword, and it is accepted ===========
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg1]
	mov	rdx, [fx_ty_ok]			; `poscit alloc`, not `rete`
	call	ast_node_ty
	call	fx_run
	cmp	qword [fx_ndiag], 0
	jne	.fail2

	; =================== 3: without the ordinal, nothing propagates ==
	; `applica poscit alloc` alone -- no `sicut f`. The laundering tree is
	; then accepted, which is the difference spec §4.2 is about.
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg1]
	mov	rdx, [fx_ty_bad]		; put `rete` back
	call	ast_node_ty
	call	fx_run
	cmp	qword [fx_ndiag], 1		; still rejected, as in check 1
	jne	.fail3
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arow]			; `applica`'s `Row` node
	mov	rdx, 0
	mov	rcx, 1				; ... now holding ONE item
	call	fx_setab
	call	fx_run
	cmp	qword [fx_ndiag], 0
	jne	.fail3

	; =================== 4 and 5: EXS-E0501 =========================
	call	fx_build_bear
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail4
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 501
	jne	.fail4
	cmp	rdx, 909			; the `Binding` node's span
	jne	.fail4

	lea	rdi, [fx_tree]
	mov	rsi, 1				; the bearing struct
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.flags]
	test	ecx, AST_F_CAPBEAR
	jz	.fail5
	lea	rdi, [fx_tree]
	mov	rsi, 2				; the plain struct
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.flags]
	test	ecx, AST_F_CAPBEAR
	jnz	.fail5

	; the twin: bind the PLAIN struct instead
	lea	rdi, [fx_tree]
	mov	rsi, 3				; the binding's declaration
	mov	rdx, [fx_ty_plain]
	call	ast_decl_ty
	call	fx_run
	cmp	qword [fx_ndiag], 0
	jne	.fail4

	; ===== 6: rule 5's "transitively" -- the fixpoint doing real work =====
	;     publica functio applica(v, f) poscit alloc, sicut f
	;     functio medius(v) { redde applica(v, nocens) }      // PRIVATE:
	;                                                         // inferred
	;     publica functio exterior(v) poscit alloc { redde medius(v) }
	;
	; `medius` declares nothing, so spec §4.1 rule 5 infers `{alloc, rete}`
	; for it from the substituted call -- and `exterior`, which declares
	; only `alloc` and never mentions `rete` or `nocens`, is the one that
	; is rejected. Two rounds of the fixpoint: nothing about this is
	; visible in one walk of the tree.
	call	fx_build_chain
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail6
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 421
	jne	.fail6
	cmp	rdx, 6161			; the call to `medius`, in
	jne	.fail6				; `exterior` -- not the inner one

	; the twin: `nocens` becomes a function that only allocates
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg1]
	mov	rdx, [fx_ty_ok]
	call	ast_node_ty
	call	fx_run
	test	rax, rax
	jnz	.fail6

	; ===== 7: spec §4.2 as amended -- the row carried by a BEARING type ==
	;     publica functio imprime(s: Portans) poscit sicut s     (no body)
	;     publica functio vocans() { imprime(s); }               (poscit {})
	;
	; `Portans` is `structura { sock: rete }`, so its mark is `{rete}`
	; (§4.3) and `sicut s` substitutes exactly that. `vocans` declares
	; nothing, so the call is `EXS-E0421` at the call.
	call	fx_build_mark
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail7
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 421
	jne	.fail7
	cmp	rdx, 7171
	jne	.fail7

	; the twin: the same `sicut`, over a struct that bears NOTHING. Its
	; row is the EMPTY row -- a real row, not "no row" -- so the ordinal
	; is substituted away and there is nothing to report. One dword.
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg0]
	mov	rdx, [fx_ty_plain]
	call	ast_node_ty
	call	fx_run
	cmp	qword [fx_ndiag], 0
	jne	.fail7

	; ===== 8: a leftover ordinal is EXS-E0423 ==========================
	; Clear the argument's type slot outright -- "pass 2 recorded no type
	; here", the ONE input for which the amended rule still leaves the
	; ordinal in place. checker.md class L, not `E0421`'s "undeclared
	; capability": there is no capability to name.
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg0]
	xor	rdx, rdx
	call	ast_node_ty
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail8
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 423
	jne	.fail8
	cmp	rdx, 7171
	jne	.fail8

	; ===== 9: a prelude-tagged callee `d` ==============================
	call	fx_build_pre
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail9
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 421
	jne	.fail9
	cmp	rdx, 9191
	jne	.fail9

	; ===== 10: the mark of a PRELUDE struct ============================
	; `structura Scriptor { a: ambitus, descriptor: i32 }` has no
	; `AstDecl` -- its nominal type carries a TAGGED prelude row index in
	; `a` -- so the §4.3 fixpoint never sees its fields and `wbear` has no
	; entry to read. `sicut s` over it must still substitute `{ambitus}`,
	; which is what `examples/imprime.exsc` is for. Same tree, same call,
	; argument 0 retyped.
	call	fx_build_mark
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg0]
	mov	rdx, [fx_ty_pre]
	call	ast_node_ty
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail10
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 421
	jne	.fail10
	cmp	rdx, 7171
	jne	.fail10

	xor	edi, edi
	call	sys_exit_group
  .fail0:
	mov	rdi, 10
	call	sys_exit_group
  .fail1:
	mov	rdi, 11
	call	sys_exit_group
  .fail2:
	mov	rdi, 12
	call	sys_exit_group
  .fail3:
	mov	rdi, 13
	call	sys_exit_group
  .fail4:
	mov	rdi, 14
	call	sys_exit_group
  .fail5:
	mov	rdi, 15
	call	sys_exit_group
  .fail6:
	mov	rdi, 16
	call	sys_exit_group
  .fail7:
	mov	rdi, 17
	call	sys_exit_group
  .fail8:
	mov	rdi, 18
	call	sys_exit_group
  .fail9:
	mov	rdi, 19
	call	sys_exit_group
  .fail10:
	mov	rdi, 20
	call	sys_exit_group

; ---------------------------------------------------------------------------
; Run pass 3 over the tree with a fresh `ChkCtx` and a fresh scratch arena.
; `arena_reset` is legal on the scratch and would be a bug on the tree's
; (rt/arena.inc's header).
;
; `chk_resolve` is deliberately NOT run: this tree is hand-built with `Path.d`
; and `Node.ty` already filled -- the arrangement checker.md section 6
; prescribes for testing a pass whose predecessors do not exist -- and running
; pass 1 over it would resolve names against a module table no source
; produced. `ChkCtx.draws` is therefore empty here, which is the right answer:
; the draws in this file are all CALL-induced, and those are pass 3's.
  fx_run:
	push	rbp
	mov	qword [fx_ndiag], 0
	lea	rdi, [fx_scr]
	call	arena_reset
	lea	rdi, [fx_dvec]
	lea	rsi, [fx_scr]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_ctx]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_dvec]
	lea	rcx, [fx_scr]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_ctx]
	call	chk_rows
	mov	[fx_ndiag], rax
	pop	rbp
	ret

; rax = buffered diagnostic rsi's code, rdx = its caret offset.
  fx_diag:
	push	rbp
	lea	rdi, [fx_ctx]
	add	rdi, ChkCtx.dbuf
	call	vec_get
	mov	edx, [rax + ChkDiag.caret.start]
	mov	eax, [rax + ChkDiag.code]
	pop	rbp
	ret

; `ast_node`, with the fixture's span and no `c`/`d`.
;   rsi = kind, rdx = aux, rcx = a, r8 = b  ->  rax = the node id
  fx_node:
	push	rbp
	lea	rdi, [fx_tree]
	lea	r9, [fx_span]
	call	ast_node
	pop	rbp
	ret

; `ast_node_cd`, positionally: rsi = node, rdx = c, rcx = d.
  fx_setcd:
	push	rbp
	lea	rdi, [fx_tree]
	call	ast_node_cd
	pop	rbp
	ret

; Overwrite a node's `a` and `b` AFTER the fact -- the one thing ast/build.inc
; has no constructor for, used by check 3 to shorten a `Row` by one item
; without rebuilding the tree. rsi = node, rdx = a, rcx = b.
  fx_setab:
	push	rbp
	mov	[fx_t5], rdx
	mov	[fx_t6], rcx
	lea	rdi, [fx_tree]
	call	ast_node_at
	mov	rdx, [fx_t5]
	test	rdx, rdx
	jz	.keep_a
	mov	[rax + AstNode.a], edx
  .keep_a:
	mov	rcx, [fx_t6]
	mov	[rax + AstNode.b], ecx
	pop	rbp
	ret

; A `Path` node with no segments resolving to declaration rcx.
  fx_path:
	push	rbp
	mov	[fx_t1], rcx
	mov	rsi, AST_PATH
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_t2], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_t1]
	call	fx_setcd
	mov	rax, [fx_t2]
	pop	rbp
	ret

; A `RowItem` naming the atom whose §4.6 ordinal is rcx, staged onto the
; builder's list stack.
  fx_ri_atom:
	push	rbp
	mov	rax, [fx_base]
	add	rax, rcx
	dec	rax
	mov	[fx_t3], rax
	mov	rcx, rax
	call	fx_path
	mov	rsi, AST_ROWITEM
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	[fx_t4], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_t3]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t4]
	call	ast_list_push
	pop	rbp
	ret

; A `RowItem` for `sicut f` with parameter ordinal rcx, staged.
  fx_ri_ord:
	push	rbp
	mov	[fx_t3], rcx
	mov	rsi, AST_ROWITEM
	mov	rdx, AST_ROW_SICUT
	mov	rcx, 1				; an interner id, unread here
	xor	r8, r8
	call	fx_node
	mov	[fx_t4], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_t3]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t4]
	call	ast_list_push
	pop	rbp
	ret

; ---------------------------------------------------------------------------
; Spec §4.2's example, as a tree. Only what pass 3 reads is built: the two
; declared rows, the call, and the type on the second argument. Bodies for
; `applica` and `nocens` are omitted -- a function with no body is DECLARED
; (compute.inc's `__chk_row_seed`), which is what both of them are.
  fx_build_hof:
	push	rbp
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_span]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	; ---- declarations: applica, nocens, exterior, then the atoms ----
	mov	r12, 3
  .decl:
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FN
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl
	dec	r12
	jnz	.decl
	lea	rdi, [fx_tree]
	call	ast_cap_push
	mov	[fx_base], rax

	; ---- types ----
	lea	rdi, [fx_tree]
	mov	rsi, 32
	call	ast_type_float
	mov	[fx_t1], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_extra_push
	mov	[fx_t2], rax			; the `fn` type's operand run
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t1]
	call	ast_extra_push

	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_RETE - 1
	mov	[fx_items + 4], eax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 1
	call	chk_row_intern
	mov	[fx_t3], rax
	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_ALLOC - 1
	mov	[fx_items + 4], eax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 1
	call	chk_row_intern
	mov	[fx_t4], rax

	lea	rdi, [fx_tree]
	mov	rsi, [fx_t2]
	mov	rdx, 1
	mov	rcx, [fx_t3]
	call	ast_type_fn			; `functio(f32) -> f32 poscit rete`
	mov	[fx_ty_bad], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t2]
	mov	rdx, 1
	mov	rcx, [fx_t4]
	call	ast_type_fn			; ... `poscit alloc`
	mov	[fx_ty_ok], rax

	; ---- applica: `poscit alloc, sicut f` ----
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	mov	rcx, AST_CAP_ALLOC
	call	fx_ri_atom
	mov	rcx, 1				; `sicut f` -- parameter 1
	call	fx_ri_ord
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_ROW
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, rdx
	mov	r8, 2
	call	fx_node
	mov	[fx_arow], rax
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_t5], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_arow]
	call	fx_setcd
	mov	rsi, AST_FN
	mov	rdx, AST_FN_PUBLICA
	mov	rcx, [fx_t5]
	xor	r8, r8
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, [fx_t6]
	call	ast_decl_node

	; ---- exterior: `poscit alloc`, with the laundering call ----
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	mov	rcx, AST_CAP_ALLOC
	call	fx_ri_atom
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_ROW
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 1
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_esig], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_t6]
	call	fx_setcd

	xor	rcx, rcx
	inc	rcx				; `applica`'s declaration
	call	fx_path
	mov	[fx_callee], rax		; NOT fx_t1: `fx_path` writes that
	xor	rcx, rcx
	call	fx_path
	mov	[fx_arg0], rax
	xor	rcx, rcx
	call	fx_path
	mov	[fx_arg1], rax			; argument 1 -- `nocens`
	lea	rdi, [fx_tree]
	mov	rsi, rax
	mov	rdx, [fx_ty_bad]
	call	ast_node_ty
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg0]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg1]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	[fx_t6], rax
	mov	dword [fx_span + Span.start], 4242
	mov	rsi, AST_CALL
	xor	rdx, rdx
	mov	rcx, [fx_callee]
	mov	r8, [fx_t6]
	call	fx_node
	mov	[fx_call], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, rax
	mov	rdx, 2
	xor	rcx, rcx
	call	fx_setcd

	mov	rsi, AST_REDDE
	xor	rdx, rdx
	mov	rcx, [fx_call]
	xor	r8, r8
	call	fx_node
	mov	[fx_t1], rax
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t1]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_BLOCK
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 1
	call	fx_node
	mov	[fx_t2], rax
	mov	rsi, rax
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_setcd
	mov	rsi, AST_FN
	mov	rdx, AST_FN_PUBLICA
	mov	rcx, [fx_esig]
	mov	r8, [fx_t2]
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 3
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 3
	mov	rdx, [fx_t6]
	call	ast_decl_node
	pop	rbp
	ret

; ---------------------------------------------------------------------------
;     structura Portans { sock: rete }      // capability-bearing: [rete]
;     structura Simplex { n:    u32 }
;     firma p: Portans = ...                // EXS-E0501, spec §14 entry 11
  fx_build_bear:
	push	rbp
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_span]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	lea	rdi, [fx_tree]
	mov	rsi, AST_D_STRUCT
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 1 Portans
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_STRUCT
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 2 Simplex
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_BINDING
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 3 the module-level binding
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FIELD
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, 1
	lea	r9, [fx_span]
	call	ast_decl			; 4 Portans.sock
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FIELD
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, 2
	lea	r9, [fx_span]
	call	ast_decl			; 5 Simplex.n
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FN
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 6 some function
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_BINDING
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, 6
	lea	r9, [fx_span]
	call	ast_decl			; 7 a LOCAL binding of the same
						; bearing type -- §4.1 rule 7
						; forbids only MODULE-LEVEL state,
						; so this one is legal and must
						; not be reported
	lea	rdi, [fx_tree]
	call	ast_cap_push
	mov	[fx_base], rax

	mov	rax, [fx_base]
	add	rax, AST_CAP_RETE - 1
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_CAP
	mov	rdx, rax
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_t1], rax			; the type `rete`
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 32
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fx_t2], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_STRUCT
	mov	rdx, 1
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_bad], rax		; `Portans`
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_STRUCT
	mov	rdx, 2
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_plain], rax		; `Simplex`

	lea	rdi, [fx_tree]
	mov	rsi, 4
	mov	rdx, [fx_t1]
	call	ast_decl_ty
	lea	rdi, [fx_tree]
	mov	rsi, 5
	mov	rdx, [fx_t2]
	call	ast_decl_ty
	lea	rdi, [fx_tree]
	mov	rsi, 3
	mov	rdx, [fx_ty_bad]
	call	ast_decl_ty
	lea	rdi, [fx_tree]
	mov	rsi, 7
	mov	rdx, [fx_ty_bad]
	call	ast_decl_ty

	mov	rcx, 4
	call	fx_struct			; Portans, holding field decl 4
	mov	rsi, 1
	mov	rdx, rax
	lea	rdi, [fx_tree]
	call	ast_decl_node
	mov	rcx, 5
	call	fx_struct			; Simplex
	mov	rsi, 2
	mov	rdx, rax
	lea	rdi, [fx_tree]
	call	ast_decl_node

	; the binding, with a span the fixture can recognise
	xor	rcx, rcx
	call	fx_path
	mov	[fx_t3], rax
	mov	rsi, AST_TYPATH
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	[fx_t4], rax
	mov	dword [fx_span + Span.start], 909
	mov	rsi, AST_BINDING
	xor	rdx, rdx
	mov	rcx, [fx_t4]
	xor	r8, r8
	call	fx_node
	mov	[fx_t5], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, [fx_t5]
	xor	rdx, rdx
	mov	rcx, 3
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 3
	mov	rdx, [fx_t5]
	call	ast_decl_node

	; the local binding, with its own span
	xor	rcx, rcx
	call	fx_path
	mov	rsi, AST_TYPATH
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	[fx_t4], rax
	mov	dword [fx_span + Span.start], 808
	mov	rsi, AST_BINDING
	xor	rdx, rdx
	mov	rcx, [fx_t4]
	xor	r8, r8
	call	fx_node
	mov	[fx_t5], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, [fx_t5]
	xor	rdx, rdx
	mov	rcx, 7
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 7
	mov	rdx, [fx_t5]
	call	ast_decl_node
	pop	rbp
	ret

; A one-field `Struct` node whose field's declaration is rcx.  -> rax = node
  fx_struct:
	push	rbp
	mov	[fx_t6], rcx
	xor	rcx, rcx
	call	fx_path
	mov	rsi, AST_TYPATH
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	rsi, AST_FIELD
	xor	rdx, rdx
	mov	rcx, 1				; the name id
	mov	r8, rax
	call	fx_node
	mov	[fx_t7], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_t6]
	call	fx_setcd
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t6], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t7]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t6]
	call	ast_list_emit
	mov	rsi, AST_STRUCT
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 1
	call	fx_node
	mov	[fx_t7], rax
	mov	rsi, rax
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_setcd
	mov	rax, [fx_t7]
	pop	rbp
	ret


; ---------------------------------------------------------------------------
; The same laundering, two private functions further away:
;
;     publica functio applica(v, f) -> f32 poscit alloc, sicut f    (1)
;     functio medius(v) { redde applica(v, nocens) }                (2) inferred
;     functio intermedius(v) { redde medius(v) }                    (3) inferred
;     publica functio exterior(v) -> f32 poscit alloc {             (4) declared
;         redde intermedius(v)
;     }
;
; BUILT IN REVERSE, deliberately: `exterior`'s call is the LOWEST node id and
; `medius`'s the highest, so the use list -- which is in node order -- reaches
; the consumer before the producer. One round of the fixpoint therefore leaves
; `intermedius`'s row empty and finds nothing; the answer needs three, and
; that is the whole reason checker.md section 2.3 chose a fixpoint over a
; single walk. A tree built the other way round would pass with one round and
; prove nothing.
  fx_build_chain:
	push	rbp
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_span]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	mov	r12, 4
  .decl:
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FN
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl
	dec	r12
	jnz	.decl
	lea	rdi, [fx_tree]
	call	ast_cap_push
	mov	[fx_base], rax

	; ---- the two `fn` types the laundered argument can carry ----
	lea	rdi, [fx_tree]
	mov	rsi, 32
	call	ast_type_float
	mov	[fx_t1], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_extra_push
	mov	[fx_t2], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t1]
	call	ast_extra_push
	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_RETE - 1
	mov	[fx_items + 4], eax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 1
	call	chk_row_intern
	mov	[fx_t3], rax
	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_ALLOC - 1
	mov	[fx_items + 4], eax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 1
	call	chk_row_intern
	mov	[fx_t4], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t2]
	mov	rdx, 1
	mov	rcx, [fx_t3]
	call	ast_type_fn
	mov	[fx_ty_bad], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t2]
	mov	rdx, 1
	mov	rcx, [fx_t4]
	call	ast_type_fn
	mov	[fx_ty_ok], rax

	; ---- exterior (4): `publica ... poscit alloc`, calls intermedius ----
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	mov	rcx, AST_CAP_ALLOC
	call	fx_ri_atom
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_ROW
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 1
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_esig], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_t6]
	call	fx_setcd
	mov	rcx, 3				; the callee: `intermedius`
	call	fx_path
	mov	[fx_callee], rax
	mov	qword [fx_t7], 6161
	call	fx_call0
	mov	rcx, 0
	call	fx_wrap
	mov	rsi, AST_FN
	mov	rdx, AST_FN_PUBLICA
	mov	rcx, [fx_esig]
	mov	r8, rax
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 4
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 4
	mov	rdx, [fx_t6]
	call	ast_decl_node

	; ---- intermedius (3): private, calls medius ----
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_esig], rax
	mov	rcx, 2				; the callee: `medius`
	call	fx_path
	mov	[fx_callee], rax
	mov	qword [fx_t7], 6262
	call	fx_call0
	mov	rcx, 0
	call	fx_wrap
	mov	rsi, AST_FN
	xor	rdx, rdx			; NOT `publica`
	mov	rcx, [fx_esig]
	mov	r8, rax
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 3
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 3
	mov	rdx, [fx_t6]
	call	ast_decl_node

	; ---- medius (2): private, calls applica(v, nocens) ----
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_esig], rax
	xor	rcx, rcx
	inc	rcx				; the callee: `applica`
	call	fx_path
	mov	[fx_callee], rax
	xor	rcx, rcx
	call	fx_path
	mov	[fx_arg0], rax
	xor	rcx, rcx
	call	fx_path
	mov	[fx_arg1], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	mov	rdx, [fx_ty_bad]
	call	ast_node_ty
	mov	qword [fx_t7], 6363
	call	fx_call2
	mov	rcx, 0
	call	fx_wrap
	mov	rsi, AST_FN
	xor	rdx, rdx			; NOT `publica`
	mov	rcx, [fx_esig]
	mov	r8, rax
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 2
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 2
	mov	rdx, [fx_t6]
	call	ast_decl_node

	; ---- applica (1): `poscit alloc, sicut f`, no body ----
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	mov	rcx, AST_CAP_ALLOC
	call	fx_ri_atom
	mov	rcx, 1
	call	fx_ri_ord
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_ROW
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 2
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_t5], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_t6]
	call	fx_setcd
	mov	rsi, AST_FN
	mov	rdx, AST_FN_PUBLICA
	mov	rcx, [fx_t5]
	xor	r8, r8
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, [fx_t6]
	call	ast_decl_node
	pop	rbp
	ret

; `[fx_callee]()`, span start `[fx_t7]`. -> [fx_call]
  fx_call0:
	push	rbp
	mov	rax, [fx_t7]
	mov	[fx_span + Span.start], eax
	mov	rsi, AST_CALL
	xor	rdx, rdx
	mov	rcx, [fx_callee]
	xor	r8, r8
	call	fx_node
	mov	[fx_call], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, [fx_call]
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_setcd
	pop	rbp
	ret

; `[fx_callee]([fx_arg0], [fx_arg1])`, span start `[fx_t7]`. -> [fx_call]
  fx_call2:
	push	rbp
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg0]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg1]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	[fx_t6], rax
	mov	rax, [fx_t7]
	mov	[fx_span + Span.start], eax
	mov	rsi, AST_CALL
	xor	rdx, rdx
	mov	rcx, [fx_callee]
	mov	r8, [fx_t6]
	call	fx_node
	mov	[fx_call], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, [fx_call]
	mov	rdx, 2
	xor	rcx, rcx
	call	fx_setcd
	pop	rbp
	ret

; `{ redde [fx_call]; }` -> rax = the `Block` node
  fx_wrap:
	push	rbp
	mov	rsi, AST_REDDE
	xor	rdx, rdx
	mov	rcx, [fx_call]
	xor	r8, r8
	call	fx_node
	mov	[fx_t1], rax
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t1]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_BLOCK
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 1
	call	fx_node
	mov	[fx_t2], rax
	mov	rsi, rax
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_setcd
	mov	rax, [fx_t2]
	pop	rbp
	ret

; ---------------------------------------------------------------------------
; Spec §4.2 as amended -- the row carried by a CAPABILITY-BEARING type.
;
;     structura Portans { sock: rete }                  // mark {rete}
;     structura Simplex { n:    u32 }                   // mark {}
;     publica functio imprime(s: Portans) poscit sicut s      (1, no body)
;     publica functio vocans() { imprime(s); }               (6, poscit {})
;
; `vocans` is `publica` with no `poscit`, so its row is DECLARED and empty
; (checker.md section 2.1 rule 3) -- it can absorb nothing, which is what makes
; the substituted atom visible as a diagnostic instead of as an inferred row.
; Only argument 0's TYPE differs between the three runs, and the three answers
; are `E0421` / clean / `E0423`.
  fx_build_mark:
	push	rbp
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_span]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FN
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 1 imprime
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_STRUCT
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 2 Portans
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_STRUCT
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 3 Simplex
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FIELD
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, 2
	lea	r9, [fx_span]
	call	ast_decl			; 4 Portans.sock
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FIELD
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, 3
	lea	r9, [fx_span]
	call	ast_decl			; 5 Simplex.n
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FN
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 6 vocans
	lea	rdi, [fx_tree]
	call	ast_cap_push
	mov	[fx_base], rax

	; ---- types ----
	mov	rax, [fx_base]
	add	rax, AST_CAP_RETE - 1
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_CAP
	mov	rdx, rax
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_t1], rax			; the type `rete`
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 32
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fx_t2], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_STRUCT
	mov	rdx, 2
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_bad], rax		; `Portans`
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_STRUCT
	mov	rdx, 3
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_plain], rax		; `Simplex`
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_STRUCT
	mov	edx, CHK_TY_PRELUDE or CHK_TY_PRE_SCRIPTOR
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_pre], rax		; the prelude's `Scriptor` --
						; `a` is a TAGGED row index, not
						; a declaration (prim.inc)
	lea	rdi, [fx_tree]
	mov	rsi, 4
	mov	rdx, [fx_t1]
	call	ast_decl_ty
	lea	rdi, [fx_tree]
	mov	rsi, 5
	mov	rdx, [fx_t2]
	call	ast_decl_ty

	mov	rcx, 4
	call	fx_struct			; Portans, holding field decl 4
	lea	rdi, [fx_tree]
	mov	rsi, 2
	mov	rdx, rax
	call	ast_decl_node
	mov	rcx, 5
	call	fx_struct			; Simplex
	lea	rdi, [fx_tree]
	mov	rsi, 3
	mov	rdx, rax
	call	ast_decl_node

	; ---- imprime (1): `poscit sicut s`, parameter 0, no body ----
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	xor	rcx, rcx			; `sicut s` -- parameter 0
	call	fx_ri_ord
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_ROW
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 1
	call	fx_node
	mov	[fx_arow], rax
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_t5], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_arow]
	call	fx_setcd
	mov	rsi, AST_FN
	mov	rdx, AST_FN_PUBLICA
	mov	rcx, [fx_t5]
	xor	r8, r8
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, [fx_t6]
	call	ast_decl_node

	; ---- vocans (6): `publica`, no `poscit`, one call ----
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_esig], rax
	xor	rcx, rcx
	inc	rcx				; the callee: `imprime`
	call	fx_path
	mov	[fx_callee], rax
	xor	rcx, rcx
	call	fx_path
	mov	[fx_arg0], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	mov	rdx, [fx_ty_bad]		; `s: Portans`
	call	ast_node_ty
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_arg0]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	[fx_t6], rax
	mov	dword [fx_span + Span.start], 7171
	mov	rsi, AST_CALL
	xor	rdx, rdx
	mov	rcx, [fx_callee]
	mov	r8, [fx_t6]
	call	fx_node
	mov	[fx_call], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, [fx_call]
	mov	rdx, 1				; ONE argument
	xor	rcx, rcx
	call	fx_setcd
	xor	rcx, rcx
	call	fx_wrap
	mov	rsi, AST_FN
	mov	rdx, AST_FN_PUBLICA
	mov	rcx, [fx_esig]
	mov	r8, rax
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 6
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 6
	mov	rdx, [fx_t6]
	call	ast_decl_node
	pop	rbp
	ret

; ---------------------------------------------------------------------------
; A callee whose `d` is PRELUDE-TAGGED, which is what `m.ambitus()` carries
; once `checker/types/` stops keeping `Member.d` at 0 (prim.inc's note: the
; tag is `CHK_TY_PRELUDE or CHK_TY_PRE_*`, and `0x80000004` is `Mundus.ambitus`).
; It names a row of `prelude/interface.inc`, never an `AstDecl`, so pass 3 must
; not hand it to `ast_decl_at`.
;
;     publica functio caller() { m.ambitus(); }
;
; The `Member`'s TYPE carries `poscit {rete}` -- artificial, and deliberately
; not `ambitus`: it makes the `.indirect` path's answer VISIBLE as one
; `EXS-E0421`, so this fixture distinguishes "did not trap" from "took the
; branch and read the type". `caller` declares nothing, so the atom is
; undeclared.
  fx_build_pre:
	push	rbp
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_span]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FN
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 1 caller
	lea	rdi, [fx_tree]
	call	ast_cap_push
	mov	[fx_base], rax

	lea	rdi, [fx_tree]
	mov	rsi, 32
	call	ast_type_float
	mov	[fx_t1], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_extra_push
	mov	[fx_t2], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t1]
	call	ast_extra_push
	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_RETE - 1
	mov	[fx_items + 4], eax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 1
	call	chk_row_intern
	mov	[fx_t3], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t2]
	mov	rdx, 1
	mov	rcx, [fx_t3]
	call	ast_type_fn			; `... poscit {rete}`
	mov	[fx_ty_bad], rax

	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_esig], rax
	xor	rcx, rcx
	call	fx_path				; the receiver `m`
	mov	[fx_t4], rax
	mov	rsi, AST_MEMBER
	xor	rdx, rdx
	mov	rcx, [fx_t4]
	mov	r8, 1				; the member's name id
	call	fx_node
	mov	[fx_callee], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	mov	rdx, [fx_ty_bad]
	call	ast_node_ty
	mov	rsi, [fx_callee]
	mov	edx, CHK_TY_PRELUDE or CHK_TY_PRE_AMBITUS
	call	fx_setd_raw
	mov	dword [fx_span + Span.start], 9191
	mov	rsi, AST_CALL
	xor	rdx, rdx
	mov	rcx, [fx_callee]
	xor	r8, r8
	call	fx_node
	mov	[fx_call], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, [fx_call]
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_setcd
	xor	rcx, rcx
	call	fx_wrap
	mov	rsi, AST_FN
	mov	rdx, AST_FN_PUBLICA
	mov	rcx, [fx_esig]
	mov	r8, rax
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, [fx_t6]
	call	ast_decl_node
	pop	rbp
	ret

; Write a node's `d` DIRECTLY. `ast_node_cd` cannot: `Member.d` is
; `AST_R_DECL`, and `__ast_slot_check` bounds it against `Ast.decls.len` --
; which a tagged value exceeds by construction. rsi = node, rdx = the value.
  fx_setd_raw:
	push	rbp
	mov	[fx_t7], rdx
	lea	rdi, [fx_tree]
	call	ast_node_at
	mov	rcx, [fx_t7]
	mov	[rax + AstNode.d], ecx
	pop	rbp
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/rows/rows.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_arena:	rb sizeof.Arena
  fx_scr:	rb sizeof.Arena
  fx_iar:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_ctx:	rb sizeof.ChkCtx
  fx_dvec:	rb sizeof.Vec
  fx_span:	rb sizeof.Span
  fx_items:	rb 8 * 8
  fx_ndiag:	rq 1
  fx_base:	rq 1
  fx_arow:	rq 1
  fx_esig:	rq 1
  fx_call:	rq 1
  fx_arg1:	rq 1
  fx_arg0:	rq 1
  fx_callee:	rq 1
  fx_ty_bad:	rq 1
  fx_ty_ok:	rq 1
  fx_ty_plain:	rq 1
  fx_ty_pre:	rq 1
  fx_t1:	rq 1
  fx_t2:	rq 1
  fx_t3:	rq 1
  fx_t4:	rq 1
  fx_t5:	rq 1
  fx_t6:	rq 1
  fx_t7:	rq 1
