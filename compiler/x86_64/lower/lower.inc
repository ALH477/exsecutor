; compiler/x86_64/lower/lower.inc
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
; The module and the function: docs/design/lowering.md section 2.9, and the
; `lwr_module` entry point the whole pass hangs off.
;
; THREE PASSES OVER THE TREE, and the order is forced by names:
;
;   A  every PRELUDE routine the module calls gets a bodiless `externus`
;      declaration, in `prelude/interface.inc` row order. A `call` names its
;      callee by INDEX into `Module.funcs` (parse.inc's `__bfa_ps_call` looks
;      the `@name` up in `Module.func_by_name` and stores the index), so a
;      callee that has no `Func` cannot be called at all.
;   B  every function this module declares gets its `Func`, its name, its
;      signature and its `numeri` -- before any body is walked, so a forward
;      call resolves.
;   C  the bodies, in item order.
;
; Globals are emitted during C, in literal walk order, and `bfa_print_module`
; writes globals before functions, so the printed order is
; `data`s, then the prelude declarations, then the module's own functions --
; which is exactly the order tests/unit/bfa_emit_program.asm's hand-written
; IR is in.
;
; THE ENTRY POINT TAKES FOUR ARGUMENTS, NOT THREE. The brief writes
; `lwr_module(tree, module_ir, arena)`; lowering.md section 2.1 requires TWO
; arenas -- the module's IR in one, the builder's per-function scratch in a
; second that is RESET per function -- and there is nowhere in a three-argument
; form to put the second. Resetting the arena the IR lives in would free the IR.
; Recorded here rather than quietly re-purposed.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md sections 4.7, 5.3, 5.4, 9.1, 10.1.
; -----------------------------------------------------------------------------

include 'stmt.inc'

; How many synthetic variables one function may create: the short-circuit
; result of every `et`/`vel`, one `terminus` counter per bounded `dum`, and one
; per hidden carrier parameter. Over-running it is `vec_get`'s `rassert`, not
; silent corruption.
LWR_SYNTH_HEADROOM = 256

	jmp	__lwr_words_end
  __lwr_w_adparem	db "ad_parem"
  .len = $ - __lwr_w_adparem
  __lwr_w_vetita	db "vetita"
  .len = $ - __lwr_w_vetita
  __lwr_w_explicita	db "explicita"
  .len = $ - __lwr_w_explicita
  __lwr_w_conservata	db "conservata"
  .len = $ - __lwr_w_conservata
  __lwr_w_sysv		db "sysv_amd64"
  .len = $ - __lwr_w_sysv
  __lwr_words_end:

; __lwr_intern(st, lwrp, lwrn2) -> eax = the id of `lwrn2` bytes at `lwrp` in
; the IR module's own interner.
proc __lwr_intern, lwrsp, lwrp, lwrn2
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.mdl]
	mov	rdi, [rdi + BfaModule.names]
	mov	rsi, [lwrp]
	mov	rdx, [lwrn2]
	call	intern_id
	return
endp

; __lwr_numeri(st, lwrf) -- the four spec section 5.4 words on one function.
;
; `Ast.numeri` is four bytes "the driver's to assign" (ast/kinds.inc) and NO
; table maps a byte to a section 5.4 word (lowering.md finding 17). So an
; all-zero `numeri` prints the spec's defaults and any other value is refused
; rather than guessed at.
proc __lwr_numeri, lwrsp, lwrf
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lwrsp]
	mov	r12, [lwrf]
	mov	eax, [rbx + LwrState.nm0]
	mov	[r12 + BfaFunc.numeri0], eax
	mov	eax, [rbx + LwrState.nm1]
	mov	[r12 + BfaFunc.numeri1], eax
	mov	eax, [rbx + LwrState.nm2]
	mov	[r12 + BfaFunc.numeri2], eax
	mov	eax, [rbx + LwrState.nm3]
	mov	[r12 + BfaFunc.numeri3], eax
	return
endp

; __lwr_register(st, lwrf, lwrnm) -- put function `lwrf` (already appended to
; `Module.funcs`) into `Module.func_by_name` under interner id `lwrnm`, and
; answer its 1-based index. The key is the INTERNED bytes, which live for the
; module's lifetime, so `map_insert`'s borrow is safe (rt/map.inc's rule).
proc __lwr_register, lwrsp, lwrf, lwrnm
	uses	rbx, r12, r13
	locals
		slot lwridx, dd
		slot lwrp, dq
		slot lwrn2, dq
	endl
	mov	rbx, [lwrsp]
	mov	r12, [rbx + LwrState.mdl]
	mov	rdi, [r12 + BfaModule.funcs]
	mov	rax, [rdi + Vec.len]
	mov	[lwridx], eax
	mov	rdi, [r12 + BfaModule.names]
	mov	rsi, [lwrnm]
	call	intern_bytes
	mov	[lwrp], rax
	mov	[lwrn2], rdx
	mov	rdi, [r12 + BfaModule.func_by_name]
	mov	rsi, [lwrp]
	mov	rdx, [lwrn2]
	mov	ecx, [lwridx]
	call	map_insert
	mov	eax, [lwridx]
	return
endp

; __lwr_iface_ir(st, lwrc) -> eax = the IR type id for one
; `prelude/interface.inc` `EXS_IFACE_T_*` code (0 for void).
proc __lwr_iface_ir, lwrsp, lwrc
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	eax, [lwrc]
	cmp	eax, EXS_IFACE_T_PTR
	je	.ptr
	cmp	eax, EXS_IFACE_T_U8
	je	.u8
	cmp	eax, EXS_IFACE_T_U64
	je	.u64
	cmp	eax, EXS_IFACE_T_I32
	je	.i32
	xor	eax, eax
	return
  .ptr:
	mov	eax, [rbx + LwrState.tptr]
	return
  .u8:
	mov	eax, [rbx + LwrState.tu8]
	return
  .u64:
	mov	eax, [rbx + LwrState.tu64]
	return
  .i32:
	; sign 1, as ty.inc's `.int` and parse.inc give every `iN` -- the sign
	; byte is part of the interning key. No prelude signature uses this code
	; today (interface.inc's part B has no `EXS_IFACE_T_I32` entry), so this
	; line is unreachable and untested; it is kept consistent rather than
	; left to be found the way ty.inc's was.
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, BFA_TK_I
	mov	edx, 1
	mov	ecx, 32
	xor	r8, r8
	call	bfa_type_intern
	return
endp

; __lwr_pre_declare(st, lwrk) -- one bodiless `externus` declaration for
; prelude row `lwrk`, named by its PRELUDE SYMBOL (`ambitus` ->
; `exsrt_mundus_ambitus`), with the IR signature `interface.inc` records --
; which already spells an aggregate return as the hidden leading `ptr` IR 2.9
; passes.
;
; NO BODY. docs/design/ssa-ir.md section 2.11 admits `Function ::= ... ';'`
; for "a declaration whose body is elsewhere"; `parse.inc` still requires a
; placeholder body (`[UNIMPLEMENTED]` there), and `print.inc` writes `{` and
; `}` with nothing between for a function with no blocks. That is what this
; pass emits and what its fixture compares against; the hand-written IR in
; tests/unit/bfa_emit_program.asm gives the same three declarations invented
; placeholder bodies, which a lowering must not do.
proc __lwr_pre_declare, lwrsp, lwrk
	uses	rbx, r12, r13
	locals
		slot lwrrec, dq
		slot lwrsig, dq
		slot lwrnp, dd
		slot lwrpt, dq
		slot lwri, dd
		slot lwrsi, dd
		slot lwrnm, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [lwrk]
	call	__lwr_pre_decl
	mov	[lwrrec], rax
	mov	r13, rax
	mov	ecx, [r13 + 24]
	lea	rax, [exsrt_iface_signa]
	add	rax, rcx
	mov	[lwrsig], rax
	movzx	ecx, byte [rax]
	mov	[lwrnp], ecx

	mov	rdi, [rbx + LwrState.scrar]
	mov	esi, [lwrnp]
	inc	esi
	shl	rsi, 2
	call	arena_alloc
	mov	[lwrpt], rax
	mov	dword [lwri], 0
  .ploop:
	mov	eax, [lwri]
	cmp	eax, [lwrnp]
	jge	.pdone
	mov	r12, [lwrsig]
	mov	eax, [lwri]
	movzx	ecx, byte [r12 + rax + 2]
	mov	rdi, rbx
	mov	esi, ecx
	call	__lwr_iface_ir
	mov	rcx, [lwrpt]
	mov	edx, [lwri]
	mov	[rcx + rdx*4], eax
	mov	eax, [lwri]
	inc	eax
	mov	[lwri], eax
	jmp	.ploop
  .pdone:
	mov	r12, [lwrsig]
	movzx	ecx, byte [r12 + 1]
	mov	rdi, rbx
	mov	esi, ecx
	call	__lwr_iface_ir
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, eax
	mov	rdx, [lwrpt]
	mov	ecx, [lwrnp]
	call	bfa_sig_new
	mov	[lwrsi], eax

	mov	r13, [lwrrec]
	lea	rsi, [exsrt_iface_nomina]
	mov	ecx, [r13 + 12]			; sym_off
	add	rsi, rcx
	mov	edx, [r13 + 16]			; sym_len
	rassert	edx ne 0
	mov	rdi, rbx
	call	__lwr_intern
	mov	[lwrnm], eax

	mov	rdi, [rbx + LwrState.mdl]
	call	bfa_func_new
	mov	r12, rax
	mov	eax, [lwrsi]
	mov	[r12 + BfaFunc.sig], eax
	mov	eax, [lwrnm]
	mov	[r12 + BfaFunc.name], eax
	mov	dword [r12 + BfaFunc.attrs], BFA_FUNC_ATTR_EXTERNUS
	mov	rdi, rbx
	lea	rsi, [__lwr_w_sysv]
	mov	rdx, __lwr_w_sysv.len
	call	__lwr_intern
	mov	[r12 + BfaFunc.extern_abi], eax
	mov	rdi, rbx
	mov	rsi, r12
	call	__lwr_numeri

	mov	rdi, rbx
	mov	rsi, r12
	mov	edx, [lwrnm]
	call	__lwr_register
	; `pref` holds the index BIASED BY ONE, so 1 can keep meaning
	; "referenced but not yet declared"
	inc	eax
	mov	rdi, [rbx + LwrState.pref]
	mov	rsi, [lwrk]
	push	rax
	call	vec_get
	pop	rcx
	mov	[rax], ecx
	return
endp

; __lwr_sig_of(st, lwrd) -> eax = a `Module.sigs` index for the function
; declared by `lwrd`, built from its `Decl.ty` by IR 2.9's four steps: the
; hidden return `ptr` when the declared result is an aggregate; one `ptr` per
; generic parameter (`[UNIMPLEMENTED]` -- a generic function traps); one `ptr`
; per ATOM item of the function's own row; then the declared parameter types,
; aggregates as `ptr`.
;
; The caller and the callee agree by construction because `__lwr_call_user`
; stages its arguments in these same four groups from the same `Decl.ty`.
proc __lwr_sig_of, lwrsp, lwrd
	uses	rbx, r12, r13
	locals
		slot lwrfnty, dd
		slot lwra, dd
		slot lwrnp, dd
		slot lwrrow, dd
		slot lwrres, dd
		slot lwrpt, dq
		slot lwrk, dd
		slot lwri, dd
		slot lwrret, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.ty]
	mov	[lwrfnty], ecx
	rassert	ecx ne 0
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, ecx
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	rassert	ecx eq AST_TY_FN
	mov	ecx, [rax + AstType.a]
	mov	[lwra], ecx
	mov	ecx, [rax + AstType.width]
	mov	[lwrnp], ecx
	mov	ecx, [rax + AstType.b]
	mov	[lwrrow], ecx
	mov	rdi, rbx
	mov	esi, [lwra]
	mov	edx, [lwrnp]
	call	__lwr_listat
	mov	[lwrres], eax

	mov	rdi, [rbx + LwrState.scrar]
	mov	esi, [lwrnp]
	add	esi, LWR_CAP_N + 2
	shl	rsi, 2
	call	arena_alloc
	mov	[lwrpt], rax
	mov	dword [lwrk], 0
	mov	dword [lwrret], 0
	mov	dword [rbx + LwrState.signa], 0

	; ---- 1: the hidden return pointer ----
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_ty_void
	test	eax, eax
	jnz	.afterret
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_ty_scalar
	test	eax, eax
	jz	.hidden
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_ty_ir
	mov	[lwrret], eax
	jmp	.afterret
  .hidden:
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tptr]
	mov	rdx, [lwrpt]
	call	__lwr_sig_put
  .afterret:
	; ---- 2: dictionaries, one per generic parameter ----
	; [UNIMPLEMENTED]: spec section 15 item 5 leaves a dictionary's
	; contents open, so a generic function has no signature this pass can
	; write; `__lwr_gencount` traps on one rather than emitting a
	; signature that is silently one argument short.
	mov	rdi, rbx
	mov	esi, [lwrd]
	call	__lwr_gencount
	; ---- 3: carriers, one per atom item of the row ----
	mov	rdi, rbx
	mov	esi, [lwrrow]
	mov	rdx, [lwrpt]
	call	__lwr_sig_carriers
	; ---- 4: the declared parameters ----
	mov	dword [lwri], 0
  .ploop:
	mov	eax, [lwri]
	cmp	eax, [lwrnp]
	jge	.pdone
	mov	rdi, rbx
	mov	esi, [lwra]
	mov	edx, [lwri]
	call	__lwr_listat
	mov	r12d, eax
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_ty_scalar
	test	eax, eax
	jz	.aggp
	mov	rdi, rbx
	mov	esi, r12d
	call	lwr_ty_ir
	jmp	.putp
  .aggp:
	mov	eax, [rbx + LwrState.tptr]
  .putp:
	mov	rdi, rbx
	mov	esi, eax
	mov	rdx, [lwrpt]
	call	__lwr_sig_put
	mov	eax, [lwri]
	inc	eax
	mov	[lwri], eax
	jmp	.ploop
  .pdone:
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, [lwrret]
	mov	rdx, [lwrpt]
	mov	ecx, [rbx + LwrState.signa]
	call	bfa_sig_new
	return
endp

; __lwr_sig_put(st, lwrt2, lwrpt) -- append one parameter type to the array
; being built; the running count lives in `LwrState.signa` so this proc and
; `__lwr_sig_carriers` share it without a fifth argument.
proc __lwr_sig_put, lwrsp, lwrt2, lwrpt
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	eax, [rbx + LwrState.signa]
	mov	rcx, [lwrpt]
	mov	edx, [lwrt2]
	mov	[rcx + rax*4], edx
	inc	eax
	mov	[rbx + LwrState.signa], eax
	return
endp

; __lwr_sig_carriers(st, lwrrow, lwrpt) -- one `ptr` per ATOM item of `lwrrow`.
; A `sicut` ordinal contributes nothing (runtime.md section 5).
proc __lwr_sig_carriers, lwrsp, lwrrow, lwrpt
	uses	rbx, r12
	locals
		slot lwritems, dd
		slot lwrcnt, dd
	endl
	mov	rbx, [lwrsp]
	cmp	dword [lwrrow], 0
	je	.done
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrrow]
	call	ast_row_at
	mov	ecx, [rax + AstRow.items]
	mov	[lwritems], ecx
	mov	ecx, [rax + AstRow.count]
	mov	[lwrcnt], ecx
	xor	r12d, r12d
  .loop:
	cmp	r12d, [lwrcnt]
	jge	.done
	mov	rdi, rbx
	mov	esi, [lwritems]
	mov	eax, r12d
	shl	eax, 1
	mov	edx, eax
	call	__lwr_listat
	test	eax, eax
	jnz	.next
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tptr]
	mov	rdx, [lwrpt]
	call	__lwr_sig_put
  .next:
	inc	r12d
	jmp	.loop
  .done:
	return
endp

; __lwr_gencount(st, lwrd) -- traps if declaration `lwrd` has generic
; parameters. Scanning `Ast.decls` for children of kind `AST_D_GENPARAM` is
; the only way to ask: the `fn` TYPE carries no generic arity.
proc __lwr_gencount, lwrsp, lwrd
	uses	rbx, r12, r13
	locals
		slot lwrn3, dq
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.tree]
	call	ast_decl_count
	mov	[lwrn3], rax
	mov	r12, 1
  .loop:
	cmp	r12, [lwrn3]
	ja	.done
	mov	rdi, [rbx + LwrState.tree]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_GENPARAM
	jne	.next
	mov	ecx, [rax + AstDecl.parent]
	cmp	ecx, [lwrd]
	jne	.next
	rassert	ecx eq 0		; a generic function needs a dictionary
					; per parameter and spec section 15 item
					; 5 has not said what is in one
  .next:
	inc	r12
	jmp	.loop
  .done:
	return
endp

; ============================================================================
; one function
; ============================================================================

; __lwr_fn(st, lwrn) -- lower the body of `Fn` node `lwrn` into the `Func`
; pass B already created for it.
proc __lwr_fn, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrd, dd
		slot lwrsg, dd
		slot lwrbody, dd
		slot lwrfnty, dd
		slot lwra, dd
		slot lwrnp, dd
		slot lwrrow, dd
		slot lwrres, dd
		slot lwrpos, dd
		slot lwrb0, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwrsg], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrbody], ecx
	mov	ecx, [rax + AstNode.d]
	mov	[lwrd], ecx
	rassert	ecx ne 0
	cmp	dword [lwrbody], 0
	je	.nobody

	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.ty]
	mov	[lwrfnty], ecx
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, ecx
	call	ast_type_at
	mov	ecx, [rax + AstType.a]
	mov	[lwra], ecx
	mov	ecx, [rax + AstType.width]
	mov	[lwrnp], ecx
	mov	ecx, [rax + AstType.b]
	mov	[lwrrow], ecx
	mov	rdi, rbx
	mov	esi, [lwra]
	mov	edx, [lwrnp]
	call	__lwr_listat
	mov	[lwrres], eax

	; ---- the builder's per-function state ----
	mov	rdi, [rbx + LwrState.tree]
	call	ast_decl_count
	mov	r12, rax
	mov	[rbx + LwrState.nbase], r12d
	mov	rdi, rbx
	mov	esi, [lwrd]
	call	__lwr_func_index
	mov	rdi, [rbx + LwrState.mdl]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, eax
	dec	esi
	call	vec_get
	mov	r13, [rax]
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r12
	add	rdx, LWR_SYNTH_HEADROOM
	call	lwr_fn_begin
	mov	eax, [lwrd]
	mov	[rbx + LwrState.fnd], eax

	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrb0], eax
	mov	[rbx + LwrState.cur], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_seal		; `b0` has no predecessors: the one
					; block for which sealing at creation
					; is right
	mov	rdi, rbx
	call	lwr_frame_push
	mov	dword [lwrpos], 0

	; ---- group 1: the hidden return `ptr` ----
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_ty_void
	test	eax, eax
	jnz	.nohid
	mov	rdi, rbx
	mov	esi, [lwrres]
	call	lwr_ty_scalar
	test	eax, eax
	jnz	.nohid
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tptr]
	mov	edx, [lwrpos]
	call	__lwr_param
	mov	[rbx + LwrState.hidret], eax
	mov	eax, [lwrpos]
	inc	eax
	mov	[lwrpos], eax
  .nohid:
	; ---- group 3: one `param ptr` per atom item of this function's row,
	; bound into the ROOT frame so a call inside the body finds it ----
	mov	rdi, rbx
	mov	esi, [lwrrow]
	lea	rdx, [lwrpos]
	call	__lwr_row_params

	; ---- group 4: the declared parameters ----
	mov	rdi, rbx
	mov	esi, [lwrsg]
	lea	rdx, [lwrpos]
	mov	ecx, [lwrb0]
	call	__lwr_sig_params

	; ---- the body ----
	mov	rdi, rbx
	mov	esi, [lwrbody]
	call	lwr_block

	; A function whose final block is still live at `}` gets the root
	; frame's releases and a valueless `ret`. A NON-unit function whose end
	; is reachable is `EXS-E0307`, the checker's, so this path is only ever
	; taken by a `unit` result.
	cmp	dword [rbx + LwrState.cur], 0
	je	.popframe
	mov	rdi, rbx
	xor	rsi, rsi
	call	__lwr_release_to
	mov	rdi, rbx
	xor	rsi, rsi
	call	lwr_ret
  .popframe:
	mov	rdi, rbx
	call	lwr_frame_pop
	mov	rdi, rbx
	call	lwr_fn_end
  .nobody:
	return
endp

; __lwr_param(st, lwrt2, lwrpos) -> eax = `%p = param T i`.
proc __lwr_param, lwrsp, lwrt2, lwrpos
	locals
	endl
	mov	rdi, [lwrsp]
	mov	esi, BFA_OP_PARAM
	xor	rdx, rdx
	mov	ecx, [lwrt2]
	mov	r8d, [lwrpos]
	xor	r9, r9
	call	lwr_emit
	return
endp

; __lwr_row_params(st, lwrrow, lwrposp) -- one hidden carrier `param` per atom
; item of `lwrrow`, each bound in the root frame as that atom's provider
; through a synthetic variable. This is lowering.md section 2.7's "at the
; function root, the function's own hidden carrier `param` for P, which exists
; iff P is an atom item of the function's own row".
proc __lwr_row_params, lwrsp, lwrrow, lwrposp
	uses	rbx, r12, r13
	locals
		slot lwritems, dd
		slot lwrcnt, dd
		slot lwrbase, dd
		slot lwrv, dd
	endl
	mov	rbx, [lwrsp]
	cmp	dword [lwrrow], 0
	je	.done
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrrow]
	call	ast_row_at
	mov	ecx, [rax + AstRow.items]
	mov	[lwritems], ecx
	mov	ecx, [rax + AstRow.count]
	mov	[lwrcnt], ecx
	test	ecx, ecx
	jz	.done
	mov	rdi, [rbx + LwrState.tree]
	call	ast_cap_base
	mov	[lwrbase], eax
	xor	r12d, r12d
  .loop:
	cmp	r12d, [lwrcnt]
	jge	.done
	mov	rdi, rbx
	mov	esi, [lwritems]
	mov	eax, r12d
	shl	eax, 1
	mov	edx, eax
	call	__lwr_listat
	test	eax, eax
	jnz	.next
	mov	rdi, rbx
	mov	esi, [lwritems]
	mov	eax, r12d
	shl	eax, 1
	inc	eax
	mov	edx, eax
	call	__lwr_listat
	sub	eax, [lwrbase]
	inc	eax
	mov	r13d, eax			; the 1-based atom ordinal
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tptr]
	call	lwr_synth_var
	mov	[lwrv], eax
	mov	rdi, rbx
	mov	esi, [rbx + LwrState.tptr]
	mov	rdx, [lwrposp]
	mov	edx, [rdx]
	call	__lwr_param
	push	rax
	mov	rcx, [lwrposp]
	mov	edx, [rcx]
	inc	edx
	mov	[rcx], edx
	pop	rax
	mov	rdi, rbx
	mov	esi, [lwrv]
	mov	edx, [rbx + LwrState.cur]
	mov	ecx, eax
	call	lwr_write_var
	mov	rdi, rbx
	mov	esi, r13d
	mov	edx, [lwrv]
	call	lwr_cap_bind
  .next:
	inc	r12d
	jmp	.loop
  .done:
	return
endp

; __lwr_sig_params(st, lwrsg, lwrposp, lwrb0) -- the declared parameters, in
; position order. A SCALAR parameter is an SSA variable whose initial write is
; its `param`; an AGGREGATE parameter is a `ptr` the callee reads and never
; writes (IR 2.9), so its record is kind AGGP and it gets no slot and no copy.
; `param` carries no `own`: parameters are BORROWED (spec section 6.3), so
; none of them joins the frame's release list.
proc __lwr_sig_params, lwrsp, lwrsg, lwrposp, lwrb0
	uses	rbx, r12, r13
	locals
		slot lwroff, dd
		slot lwrcnt, dd
		slot lwrpd, dd
		slot lwrt, dd
		slot lwrir, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrsg]
	call	__lwr_node
	mov	ecx, [rax + AstNode.a]
	mov	[lwroff], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrcnt], ecx
	xor	r12d, r12d
  .loop:
	cmp	r12d, [lwrcnt]
	jge	.done
	mov	rdi, rbx
	mov	esi, [lwroff]
	mov	edx, r12d
	call	__lwr_listat
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	mov	ecx, [rax + AstNode.d]
	mov	[lwrpd], ecx
	rassert	ecx ne 0
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, ecx
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.ty]
	mov	[lwrt], ecx
	rassert	ecx ne 0
	mov	rdi, rbx
	mov	esi, ecx
	call	lwr_ty_scalar
	mov	r13d, eax
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_ir
	mov	[lwrir], eax
	test	r13d, r13d
	jnz	.scalar
	mov	eax, [rbx + LwrState.tptr]
  .scalar:
	mov	rdi, rbx
	mov	esi, eax
	mov	rdx, [lwrposp]
	mov	edx, [rdx]
	call	__lwr_param
	push	rax
	mov	rcx, [lwrposp]
	mov	edx, [rcx]
	inc	edx
	mov	[rcx], edx
	pop	rax
	test	r13d, r13d
	jz	.agg
	mov	rdi, rbx
	mov	esi, [lwrpd]
	mov	edx, LWR_VK_SSA
	mov	ecx, [lwrir]
	xor	r8, r8
	push	rax
	call	lwr_var_set
	pop	rax
	mov	rdi, rbx
	mov	esi, [lwrpd]
	mov	edx, [lwrb0]
	mov	ecx, eax
	call	lwr_write_var
	jmp	.next
  .agg:
	mov	rdi, rbx
	mov	esi, [lwrpd]
	mov	edx, LWR_VK_AGGP
	mov	ecx, [rbx + LwrState.tptr]
	mov	r8d, eax
	call	lwr_var_set
  .next:
	inc	r12d
	jmp	.loop
  .done:
	return
endp

; ============================================================================
; the module
; ============================================================================

; __lwr_declare_fn(st, lwrn) -- pass B: the `Func`, its name, its signature,
; its `numeri` and its `Module.func_by_name` entry, for one `Fn` node. No
; block and no instruction: the body is pass C's.
proc __lwr_declare_fn, lwrsp, lwrn
	uses	rbx, r12, r13
	locals
		slot lwrd, dd
		slot lwrsi, dd
		slot lwrnm, dd
		slot lwrbody, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrn]
	call	__lwr_node
	mov	ecx, [rax + AstNode.b]
	mov	[lwrbody], ecx
	mov	ecx, [rax + AstNode.d]
	mov	[lwrd], ecx
	rassert	ecx ne 0

	mov	rdi, rbx
	mov	esi, [lwrd]
	call	__lwr_sig_of
	mov	[lwrsi], eax

	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_decl_at
	mov	r12d, [rax + AstDecl.name]
	mov	rdi, [rbx + LwrState.tree]
	mov	rdi, [rdi + Ast.names]
	mov	esi, r12d
	call	intern_bytes
	mov	rdi, rbx
	mov	rsi, rax
	call	__lwr_intern
	mov	[lwrnm], eax

	mov	rdi, [rbx + LwrState.mdl]
	call	bfa_func_new
	mov	r13, rax
	mov	eax, [lwrsi]
	mov	[r13 + BfaFunc.sig], eax
	mov	eax, [lwrnm]
	mov	[r13 + BfaFunc.name], eax
	mov	rdi, rbx
	mov	rsi, r13
	call	__lwr_numeri
	cmp	dword [lwrbody], 0
	jne	.hasbody
	mov	dword [r13 + BfaFunc.attrs], BFA_FUNC_ATTR_EXTERNUS
	mov	rdi, rbx
	lea	rsi, [__lwr_w_sysv]
	mov	rdx, __lwr_w_sysv.len
	call	__lwr_intern
	mov	[r13 + BfaFunc.extern_abi], eax
  .hasbody:
	mov	rdi, rbx
	mov	rsi, r13
	mov	edx, [lwrnm]
	call	__lwr_register
	mov	rdi, [rbx + LwrState.fidx]
	mov	esi, [lwrd]
	push	rax
	call	vec_get
	pop	rcx
	mov	[rax], ecx
	return
endp

; __lwr_items(lwrsp, lwrn, lwrmode, lwroff, lwrcnt) -- walk `Module.items` (or
; an `Impl`'s, or an `externus` block's member list) in order. `lwrmode` 0
; declares (pass B), 1 lowers (pass C). The list is passed as (offset, count)
; rather than re-read from `lwrn`, because the three node kinds that own one
; keep it in different slots.
proc __lwr_items, lwrsp, lwrn, lwrmode, lwroff, lwrcnt
	uses	rbx, r12, r13
	locals
		slot lwrit, dd
		slot lwrk, dd
	endl
	mov	rbx, [lwrsp]
	xor	r12d, r12d
  .loop:
	cmp	r12d, [lwrcnt]
	jge	.done
	mov	rdi, rbx
	mov	esi, [lwroff]
	mov	edx, r12d
	call	__lwr_listat
	mov	[lwrit], eax
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_nkind
	mov	[lwrk], eax
	cmp	eax, AST_FN
	je	.fn
	cmp	eax, AST_IMPL
	je	.impl
	cmp	eax, AST_EXTERNUS
	je	.externus
	; `Struct`, `Interface`, `Potestas`, `Typus` and a module-level
	; `Binding` produce no IR: a type declaration is erased by Stage 2 and
	; a module `firma` with a `Lit` initializer is inlined at each use
	; (lowering.md 2.8); any other module-level initializer is `[OPEN]`
	; and `__lwr_module_const` traps at the use, not here
	jmp	.next
  .fn:
	cmp	dword [lwrmode], 0
	jne	.fnlower
	mov	rdi, rbx
	mov	esi, [lwrit]
	call	__lwr_declare_fn
	jmp	.next
  .fnlower:
	mov	rdi, rbx
	mov	esi, [lwrit]
	call	__lwr_fn
	jmp	.next
  .impl:
	mov	rdi, rbx
	mov	esi, [lwrit]
	call	__lwr_node
	mov	r13d, [rax + AstNode.b]
	mov	ecx, [rax + AstNode.c]
	mov	rdi, rbx
	mov	esi, [lwrit]
	mov	edx, [lwrmode]
	mov	ecx, r13d
	mov	r8d, [rax + AstNode.c]
	call	__lwr_items
	jmp	.next
  .externus:
	mov	rdi, rbx
	mov	esi, [lwrit]
	call	__lwr_node
	mov	r13d, [rax + AstNode.a]
	mov	ecx, [rax + AstNode.b]
	mov	rdi, rbx
	mov	esi, [lwrit]
	mov	edx, [lwrmode]
	mov	ecx, r13d
	mov	r8d, [rax + AstNode.b]
	call	__lwr_items
  .next:
	inc	r12d
	jmp	.loop
  .done:
	return
endp

; __lwr_scan_prelude(st) -- pass A's first half: find every PRELUDE routine
; this module calls. A `Member` whose `d` is ZERO is the checker's
; representation of a prelude member (checker/types/prim.inc's header), and a
; `Member` whose `d` carries bit 31 is the tagged form the same file uses for
; a `Seg`/`Path`; both are looked up by name here.
proc __lwr_scan_prelude, lwrsp
	uses	rbx, r12, r13
	locals
		slot lwrn4, dq
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.tree]
	call	ast_node_count
	mov	[lwrn4], rax
	mov	r12, 1
  .loop:
	cmp	r12, [lwrn4]
	ja	.done
	mov	rdi, [rbx + LwrState.tree]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_MEMBER
	jne	.next
	mov	r13d, [rax + AstNode.b]		; the member NAME
	mov	ecx, [rax + AstNode.d]
	test	ecx, ecx
	jz	.pre
	test	ecx, LWR_PRELUDE_TAG
	jz	.next
  .pre:
	mov	rdi, rbx
	mov	esi, r13d
	call	__lwr_pre_row
	test	eax, eax
	jz	.next
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_pre_use
  .next:
	inc	r12
	jmp	.loop
  .done:
	return
endp

; lwr_module(lwrtree, lwrmodp, lwriarn, lwrsarn) -> rax = 0 on success.
;
; `lwrtree` is a CHECKED typed AST -- CHK 2.5's invariant, which this pass
; assumes and never re-checks: Stage 2 emitted no diagnostic, so every
; `Node.ty` is non-zero, every `Member.d`/`Seg.d` is filled, `konst` is set on
; every `Lit` and `Decl.flags` is complete. A tree that reaches here and still
; needs a diagnostic is a CHECKER bug and an `rassert`, never an `EXS-E` code:
; this pass raises nothing.
;
; `lwrmodp` is a `BfaModule` `bfa_module_init` has already been called on;
; `lwriarn` backs the IR for the module's lifetime; `lwrsarn` is the builder's
; scratch and is RESET at every function.
proc lwr_module, lwrtree, lwrmodp, lwriarn, lwrsarn
	uses	rbx, r12, r13
	locals
		slot lwrst, dq
		slot lwrroot, dd
		slot lwroff2, dd
		slot lwrcnt2, dd
		slot lwri2, dq
		slot lwrnd, dq
	endl
	mov	rdi, [lwriarn]
	mov	rsi, sizeof.LwrState
	call	arena_alloc
	mov	[lwrst], rax
	mov	rbx, rax
	mov	rdi, rbx
	mov	rsi, [lwrtree]
	mov	rdx, [lwrmodp]
	mov	rcx, [lwriarn]
	mov	r8,  [lwrsarn]
	call	lwr_state_init

	; ---- `numeri`: the spec section 5.4 defaults, and nothing else ----
	mov	rdi, [lwrtree]
	mov	eax, [rdi + Ast.numeri]
	rassert	eax eq 0		; lowering.md finding 17: no table maps
					; an `Ast.numeri` byte to a section 5.4
					; word, so anything but the default is
					; refused rather than guessed
	mov	rdi, rbx
	lea	rsi, [__lwr_w_adparem]
	mov	rdx, __lwr_w_adparem.len
	call	__lwr_intern
	mov	[rbx + LwrState.nm0], eax
	mov	rdi, rbx
	lea	rsi, [__lwr_w_vetita]
	mov	rdx, __lwr_w_vetita.len
	call	__lwr_intern
	mov	[rbx + LwrState.nm1], eax
	mov	rdi, rbx
	lea	rsi, [__lwr_w_explicita]
	mov	rdx, __lwr_w_explicita.len
	call	__lwr_intern
	mov	[rbx + LwrState.nm2], eax
	mov	rdi, rbx
	lea	rsi, [__lwr_w_conservata]
	mov	rdx, __lwr_w_conservata.len
	call	__lwr_intern
	mov	[rbx + LwrState.nm3], eax

	; ---- the two module-lifetime index tables ----
	mov	rdi, [lwriarn]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + LwrState.pref], rax
	mov	rdi, rax
	mov	rsi, [lwriarn]
	mov	rdx, 4
	mov	rcx, EXS_IFACE_DECL_COUNT + 2
	call	vec_init
	xor	r12, r12
  .prezero:
	mov	rax, EXS_IFACE_DECL_COUNT + 1
	cmp	r12, rax
	ja	.prezdone
	mov	rdi, [rbx + LwrState.pref]
	call	vec_push
	mov	dword [rax], 0
	inc	r12
	jmp	.prezero
  .prezdone:

	mov	rdi, [lwrtree]
	call	ast_decl_count
	mov	[lwrnd], rax
	mov	rdi, [lwriarn]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + LwrState.fidx], rax
	mov	rdi, rax
	mov	rsi, [lwriarn]
	mov	rdx, 4
	mov	rcx, [lwrnd]
	inc	rcx
	call	vec_init
	xor	r12, r12
  .fzero:
	cmp	r12, [lwrnd]
	ja	.fzdone
	mov	rdi, [rbx + LwrState.fidx]
	call	vec_push
	mov	dword [rax], 0
	inc	r12
	jmp	.fzero
  .fzdone:

	; ---- pass A ----
	mov	rdi, rbx
	call	__lwr_scan_prelude
	mov	r12, 1
  .preloop:
	mov	rax, EXS_IFACE_DECL_COUNT
	cmp	r12, rax
	ja	.predone
	mov	rdi, [rbx + LwrState.pref]
	mov	rsi, r12
	call	vec_get
	cmp	dword [rax], 1
	jne	.prenext
	mov	rdi, rbx
	mov	rsi, r12
	call	__lwr_pre_declare
  .prenext:
	inc	r12
	jmp	.preloop
  .predone:

	; ---- the module's item list ----
	mov	rdi, [lwrtree]
	mov	eax, [rdi + Ast.root]
	mov	[lwrroot], eax
	rassert	eax ne 0
	mov	rdi, rbx
	mov	esi, eax
	call	__lwr_node
	movzx	ecx, word [rax + AstNode.kind]
	rassert	ecx eq AST_MODULE
	mov	ecx, [rax + AstNode.a]
	mov	[lwroff2], ecx
	mov	ecx, [rax + AstNode.b]
	mov	[lwrcnt2], ecx

	; ---- pass B ----
	mov	rdi, rbx
	mov	esi, [lwrroot]
	xor	rdx, rdx
	mov	ecx, [lwroff2]
	mov	r8d, [lwrcnt2]
	call	__lwr_items
	; ---- pass C ----
	mov	rdi, rbx
	mov	esi, [lwrroot]
	mov	edx, 1
	mov	ecx, [lwroff2]
	mov	r8d, [lwrcnt2]
	call	__lwr_items

	xor	eax, eax
	return
endp
