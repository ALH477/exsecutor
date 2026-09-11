; tests/unit/lwr_saluta.asm
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
; THE ACCEPTANCE TEST: the hello world's three typed trees through
; `lwr_module`, verified, printed, and compared BYTE FOR BYTE against the IR
; docs/design/runtime.md section 5 says the lowering must produce -- the same
; text `tests/unit/bfa_emit_program.asm` writes by hand and takes all the way
; to a running binary.
;
; If this fixture's bytes and that one's differ, either the design is wrong or
; this pass is, and the difference has to be named before anything is changed.
; Three differences exist and all three are named here:
;
;  1. THE PRELUDE DECLARATIONS HAVE NO BODY. `bfa_emit_program.asm`'s
;     hand-written IR gives `@exsrt_mundus_ambitus`, `@exsrt_scriptor_ad_exitum`
;     and `@exsrt_scriptor_scribe` invented placeholder bodies (`ret %0`,
;     `ret`, `%0 = iconst u64 0`) so that its text can be PARSED -- parse.inc
;     requires a body. A lowering must not invent a body for a routine whose
;     code is in the prelude blob, and docs/design/ssa-ir.md section 2.11 now
;     admits `Function ::= ... ';'` for exactly this case. So this pass emits
;     the three declarations with no blocks, and `print.inc` writes `{` and
;     `}` with nothing between. Those six lines are the whole difference in
;     the declarations; the three USER functions are byte-identical to
;     runtime.md section 5.
;  2. runtime.md section 5 does not print the declarations at all. It cannot:
;     a `call` names its callee by INDEX into `Module.funcs`, so a callee with
;     no `Func` cannot be called. The declarations are what make section 5's
;     `call u64 @exsrt_scriptor_scribe` expressible.
;  3. docs/design/lowering.md section 3 writes `global $1` and `%1 = gaddr 1`.
;     Global ids are dense FROM 0 (`bfa_global_new`, and parse.inc's
;     `data $N` line enforces the same), so the first literal is `$0` and
;     `gaddr 0` -- which is what runtime.md section 5 and
;     bfa_emit_program.asm both already say. lowering.md section 3 is the one
;     that is wrong.
;
; ---------------------------------------------------------------------------
; WHY THE TREES ARE BUILT BY HAND AND NOT LEXED, PARSED AND CHECKED. Because
; no fasmg program can contain both `checker/` and `backend_fasmg/`:
; `backend_fasmg/ir.inc` USED TO include `../rt/intern.inc`; `cst/` reaches the
; same file through `diag/` -> `rt/span.inc`, fasmg has no include guards, and the
; second path dies in `struct Arena`'s own `end struct`. `lower/ssa.inc`'s
; header states the defect and the one-line request that closes it; until it
; lands, a fixture that drives this pass CANNOT also run the front end, and
; the trees below are what `chk_run` would leave behind: `Node.ty`,
; `Decl.ty`, `Decl.flags`, `konst`, the interned `Ast.types`, the rows, and
; the tagged prelude references of `checker/types/prim.inc`.
;
; They carry no TYPE-EXPRESSION nodes (`Sig.c`, `Param.b`, `Binding.a` are 0).
; That is deliberate and is stated rather than hidden: Stage 2 replaces every
; type expression with an `Ast.types` id, and this pass reads `Decl.ty`,
; `Node.ty` and `Ast.types` and never a `Ty*` node -- which is precisely
; CHK 2.5's contract. `ast_verify_stage1` would reject these trees; nothing
; here runs it, and the pass under test does not read what is missing.
;
; ---------------------------------------------------------------------------
; Exit 0 = the printed module matches byte for byte. 10 = arena init.
; 11..16 = `bfa_verify_func` returned a non-zero rule id for function 1..6.
; 20 = length mismatch. 21 = byte mismatch (the produced text goes to stderr).
;
; Run with any argument to write the produced text to stdout instead.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

; backend_fasmg/ir.inc no longer includes rt/ (the consumer brings it, as
; every other module's does), so this fixture brings the chain itself (cst/ brings lexer/, diag/ and rt/, as exsc.asm does).
include '../../compiler/x86_64/cst/cst.inc'
; lower/ reads prelude/interface.inc's constants and does not include it
; (checker/types/ does, and two paths to a label-emitting leaf collide).
include '../../compiler/x86_64/prelude/interface.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/lower/lower.inc'

segment readable executable
  start:
	mov	rbx, [rsp]			; argc, before anything moves rsp

	lea	rdi, [fxar]
	mov	rsi, 16 * 1024 * 1024
	call	arena_init
	jc	.bad10
	lea	rdi, [fxiar]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad10
	lea	rdi, [fxscr]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad10
	lea	rdi, [fxnames]
	lea	rsi, [fxiar]
	mov	rdx, 256
	call	intern_init

	lea	rdi, [fxtree]
	lea	rsi, [fxar]
	lea	rdx, [fxnames]
	call	ast_init
	call	fx_build

	lea	rdi, [fxmod]
	lea	rsi, [fxar]
	call	bfa_module_init

	lea	rdi, [fxtree]
	lea	rsi, [fxmod]
	lea	rdx, [fxar]
	lea	rcx, [fxscr]
	call	lwr_module
	test	rax, rax
	jnz	.bad10

	; ---- every function this pass built, through the verifier ----
	;
	; EXCEPT a function with NO BLOCKS. `bfa_verify_func` traps on one:
	; `__bfa_verify_rule4`'s dominator initialisation does
	; `vec_get(ctx.idom, 0)` on a vec it sized by `nblocks`, and its
	; convergence bound is `nblocks * nblocks`, so with `nblocks == 0` the
	; first is an out-of-range `rassert` and the second is `rassert 1 le 0`.
	; A bodiless `externus` declaration has nothing to verify -- ssa-ir.md
	; section 2.11's `';'` form says "nothing is emitted for it" -- so the
	; fix is an early `return 0` there. That is another agent's file; this
	; loop skips those three and the request is in the report. The three
	; functions with bodies -- the ones this pass actually built -- all go
	; through the verifier and all must answer 0.
	mov	r12, 0
  .vloop:
	mov	rdi, [fxmod + BfaModule.funcs]
	mov	rax, [rdi + Vec.len]
	cmp	r12, rax
	jae	.vdone
	mov	rdi, [fxmod + BfaModule.funcs]
	mov	rsi, r12
	call	vec_get
	mov	rsi, [rax]
	mov	rdi, [rsi + BfaFunc.blocks]
	cmp	qword [rdi + Vec.len], 0
	je	.vnext
	lea	rdi, [fxmod]
	lea	rdx, [fxar]
	lea	rcx, [fxvb]
	lea	r8,  [fxvi]
	call	bfa_verify_func
	test	eax, eax
	jnz	.badverify
  .vnext:
	inc	r12
	jmp	.vloop
  .vdone:

	lea	rdi, [fxmod]
	lea	rsi, [fxar]
	call	bfa_print_module
	mov	[fxoutp], rax
	mov	[fxoutl], rdx

	cmp	rbx, 1
	jg	.dump

	mov	rax, [fxoutl]
	cmp	rax, fxexp.len
	jne	.bad20
	mov	rdi, [fxoutp]
	mov	rsi, [fxoutl]
	lea	rdx, [fxexp]
	mov	rcx, fxexp.len
	call	__bfa_streq
	test	eax, eax
	jz	.bad21

	xor	edi, edi
	call	sys_exit_group
  .dump:
	mov	edi, 1
	mov	rsi, [fxoutp]
	mov	rdx, [fxoutl]
	call	sys_write
	xor	edi, edi
	call	sys_exit_group
  .bad10:
	mov	rdi, 10
	call	sys_exit_group
  .badverify:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .bad20:
	mov	edi, 2
	mov	rsi, [fxoutp]
	mov	rdx, [fxoutl]
	call	sys_write
	mov	rdi, 20
	call	sys_exit_group
  .bad21:
	mov	edi, 2
	mov	rsi, [fxoutp]
	mov	rdx, [fxoutl]
	call	sys_write
	mov	rdi, 21
	call	sys_exit_group

; ============================================================================
; the tree builders
; ============================================================================

proc fx_name, fxp, fxn
	locals
	endl
	lea	rdi, [fxnames]
	mov	rsi, [fxp]
	mov	rdx, [fxn]
	call	intern_id
	return
endp

proc fx_node, fxk, fxx, fxa, fxb
	locals
		slot fxsp, sizeof.Span
	endl
	lea	rdi, [fxsp]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb
	lea	rdi, [fxtree]
	mov	rsi, [fxk]
	mov	rdx, [fxx]
	mov	rcx, [fxa]
	mov	r8,  [fxb]
	lea	r9,  [fxsp]
	call	ast_node
	return
endp

proc fx_nd, fxid, fxc, fxd
	locals
	endl
	lea	rdi, [fxtree]
	mov	rsi, [fxid]
	mov	rdx, [fxc]
	mov	rcx, [fxd]
	call	ast_node_cd
	return
endp

proc fx_nt, fxid, fxt
	locals
	endl
	lea	rdi, [fxtree]
	mov	rsi, [fxid]
	mov	rdx, [fxt]
	call	ast_node_ty
	return
endp

proc fx_decl, fxk, fxf, fxnm, fxp2
	locals
		slot fxsp2, sizeof.Span
	endl
	lea	rdi, [fxsp2]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb
	lea	rdi, [fxtree]
	mov	rsi, [fxk]
	mov	rdx, [fxf]
	mov	rcx, [fxnm]
	mov	r8,  [fxp2]
	lea	r9,  [fxsp2]
	call	ast_decl
	return
endp

proc fx_dty, fxid, fxt
	locals
	endl
	lea	rdi, [fxtree]
	mov	rsi, [fxid]
	mov	rdx, [fxt]
	call	ast_decl_ty
	return
endp

; fx_seg(fxnm, fxd) -> eax = a one-segment `Path` node, `d` resolved.
; `Seg.a` is the name, `Seg.d` the same declaration -- what Stage 2 fills.
; fx_setd(fxid2, fxd3) -- write a node's declaration slot DIRECTLY.
;
; Not `ast_node_cd`: its `__ast_slot_check` bounds a `AST_R_DECL` slot by
; `Ast.decls.len`, and a TAGGED prelude reference (`CHK_TY_PRELUDE or k`,
; 0x80000001) is far above it, so the check traps on a value the checker
; itself writes. `checker/types/prim.inc`'s `__chk_ty_setd` writes the field
; directly for a related reason ("`ast_node_cd` writes `c` AND `d`"), which is
; the only reason that module does not trip the same `rassert`. Recorded in
; the report as a finding against `ast/build.inc`.
proc fx_setd, fxid2, fxd3
	locals
	endl
	lea	rdi, [fxtree]
	mov	rsi, [fxid2]
	call	ast_node_at
	mov	ecx, [fxd3]
	mov	[rax + AstNode.d], ecx
	return
endp

proc fx_seg, fxnm2, fxd2
	uses	rbx
	locals
		slot fxsg, dd
		slot fxmk, dq
		slot fxof, dd
		slot fxcn, dd
	endl
	mov	rdi, AST_SEG
	xor	rsi, rsi
	mov	rdx, [fxnm2]
	xor	rcx, rcx
	call	fx_node
	mov	[fxsg], eax
	mov	rdi, rax
	mov	rsi, [fxd2]
	call	fx_setd
	lea	rdi, [fxtree]
	call	ast_list_mark
	mov	[fxmk], rax
	lea	rdi, [fxtree]
	mov	esi, [fxsg]
	call	ast_list_push
	lea	rdi, [fxtree]
	mov	rsi, [fxmk]
	call	ast_list_emit
	mov	[fxof], eax
	mov	[fxcn], edx
	mov	rdi, AST_PATH
	xor	rsi, rsi
	mov	edx, [fxof]
	mov	ecx, [fxcn]
	call	fx_node
	push	rax
	mov	rdi, rax
	mov	rsi, [fxd2]
	call	fx_setd
	pop	rax
	return
endp

; fx_ty(fxnm3) -> eax = a `TyPath` naming the type `fxnm3`. `Param.b` is
; AST_R_NODEQ -- spec section 8.6 writes `Param ::= IDENT ':' Type` with the
; type REQUIRED and ast/kinds.inc's role table is narrower than typed-ast.md's
; there on purpose -- so a parameter MUST carry one even though this pass
; never reads it (Stage 2 put the answer in `Decl.ty`). `Seg.d` is left 0: a
; type path's declaration slot is the checker's and nothing here needs it.
proc fx_ty, fxnm3
	locals
		slot fxpp, dd
	endl
	mov	rdi, [fxnm3]
	xor	rsi, rsi
	call	fx_seg
	mov	[fxpp], eax
	mov	rdi, AST_TYPATH
	xor	rsi, rsi
	mov	edx, [fxpp]
	xor	rcx, rcx
	call	fx_node
	return
endp

; fx_lst3(fxe1, fxe2, fxe3, fxn3) -> eax = extra offset, edx = count. A list
; of up to three node ids; `fxn3` says how many are real.
proc fx_lst3, fxe1, fxe2, fxe3, fxn3
	uses	rbx
	locals
		slot fxmk2, dq
	endl
	lea	rdi, [fxtree]
	call	ast_list_mark
	mov	[fxmk2], rax
	cmp	qword [fxn3], 1
	jb	.emit
	lea	rdi, [fxtree]
	mov	rsi, [fxe1]
	call	ast_list_push
	cmp	qword [fxn3], 2
	jb	.emit
	lea	rdi, [fxtree]
	mov	rsi, [fxe2]
	call	ast_list_push
	cmp	qword [fxn3], 3
	jb	.emit
	lea	rdi, [fxtree]
	mov	rsi, [fxe3]
	call	ast_list_push
  .emit:
	lea	rdi, [fxtree]
	mov	rsi, [fxmk2]
	call	ast_list_emit
	return
endp

; fx_types -- `Ast.types` as pass 2 would have interned it, and the one row.
proc fx_types
	uses	rbx, r12
	locals
		slot fxo, dd
	endl
	lea	rdi, [fxtree]
	mov	rsi, AST_TY_TEXTUS
	call	ast_type_simple
	mov	[fxv_tex], eax
	lea	rdi, [fxtree]
	mov	rsi, AST_TY_MENSURA
	xor	rdx, rdx
	mov	rcx, 64			; `mensura` on x86_64-linux (spec 9.5
	call	ast_type_una		;   has fixed --hospes before Stage 3)
	mov	[fxv_men], eax
	lea	rdi, [fxtree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 8
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fxv_u8], eax
	; `Scriptor` has no `AstDecl`, so its nominal `struct` type carries the
	; TAGGED prelude reference in `a` -- checker/types/prim.inc's
	; `chk_ty_prelude_type`, reproduced exactly
	lea	rdi, [fxtree]
	mov	rsi, AST_TY_STRUCT
	mov	rdx, LWR_PRELUDE_TAG or 1
	xor	rcx, rcx
	call	ast_type_una
	mov	[fxv_scr], eax
	return
endp

; fx_captypes -- the two `cap` types, after the atoms exist.
proc fx_captypes
	locals
	endl
	lea	rdi, [fxtree]
	mov	rsi, AST_TY_CAP
	mov	edx, [fxv_capb]
	add	edx, AST_CAP_MUNDUS - 1
	xor	rcx, rcx
	call	ast_type_una
	mov	[fxv_cm], eax
	lea	rdi, [fxtree]
	mov	rsi, AST_TY_CAP
	mov	edx, [fxv_capb]
	add	edx, AST_CAP_AMBITUS - 1
	xor	rcx, rcx
	call	ast_type_una
	mov	[fxv_ca], eax
	return
endp

; fx_row_sicut -> eax = a row holding one ORDINAL item and no atom -- what
; `poscit sicut s` interns to. Items are (kind, id) pairs in `Ast.extra`;
; `CHK_ROW_ORD` is 1 and the id is the zero-based parameter ordinal.
proc fx_row_sicut
	uses	rbx
	locals
		slot fxof2, dd
	endl
	lea	rdi, [fxtree]
	mov	rsi, 1			; CHK_ROW_ORD
	call	ast_extra_push
	mov	[fxof2], eax
	lea	rdi, [fxtree]
	xor	rsi, rsi		; parameter ordinal 0 -- `s`
	call	ast_extra_push
	lea	rdi, [fxtree + Ast.rows]
	call	vec_push
	mov	ecx, [fxof2]
	mov	[rax + AstRow.items], ecx
	mov	dword [rax + AstRow.count], 1
	lea	rdi, [fxtree + Ast.rows]
	mov	rax, [rdi + Vec.len]
	return
endp

; fx_fnty(fxp1, fxp2b, fxnp, fxres, fxrow) -> eax = an `AST_TY_FN`: the
; parameter type ids then the RESULT, one `Ast.extra` run, with the row in `b`.
proc fx_fnty, fxp1, fxp2b, fxnp, fxres, fxrow
	uses	rbx
	locals
		slot fxof3, dd
	endl
	mov	dword [fxof3], 0
	cmp	qword [fxnp], 1
	jb	.res
	lea	rdi, [fxtree]
	mov	rsi, [fxp1]
	call	ast_extra_push
	mov	[fxof3], eax
	cmp	qword [fxnp], 2
	jb	.res
	lea	rdi, [fxtree]
	mov	rsi, [fxp2b]
	call	ast_extra_push
  .res:
	lea	rdi, [fxtree]
	mov	rsi, [fxres]
	call	ast_extra_push
	cmp	dword [fxof3], 0
	jne	.have
	mov	[fxof3], eax
  .have:
	lea	rdi, [fxtree]
	mov	esi, [fxof3]
	mov	rdx, [fxnp]
	mov	rcx, [fxrow]
	call	ast_type_fn
	return
endp

; fx_build -- the whole module: examples/saluta.exsc, examples/imprime.exsc
; and examples/initium.exsc, typed.
proc fx_build
	uses	rbx, r12, r13
	locals
		slot fxof4, dd
		slot fxcn4, dd
		slot fxrow2, dd
	endl
	call	fx_types

	; ---- declarations, in source order; the atoms LAST ----
	lea	rdi, [fxn_saluta]
	mov	rsi, fxn_saluta.len
	call	fx_name
	mov	rdi, AST_D_FN
	mov	rsi, AST_F_PUBLICA
	mov	rdx, rax
	xor	rcx, rcx
	call	fx_decl
	mov	[fxv_dsal], eax

	lea	rdi, [fxn_imp]
	mov	rsi, fxn_imp.len
	call	fx_name
	mov	rdi, AST_D_FN
	mov	rsi, AST_F_PUBLICA
	mov	rdx, rax
	xor	rcx, rcx
	call	fx_decl
	mov	[fxv_dimp], eax

	lea	rdi, [fxn_s]
	mov	rsi, fxn_s.len
	call	fx_name
	mov	rdi, AST_D_PARAM
	mov	rsi, AST_F_MEMRES
	mov	rdx, rax
	mov	ecx, [fxv_dimp]
	call	fx_decl
	mov	[fxv_ds], eax

	lea	rdi, [fxn_t]
	mov	rsi, fxn_t.len
	call	fx_name
	mov	rdi, AST_D_PARAM
	mov	rsi, AST_F_MEMRES
	mov	rdx, rax
	mov	ecx, [fxv_dimp]
	call	fx_decl
	mov	[fxv_dt], eax

	lea	rdi, [fxn_ini]
	mov	rsi, fxn_ini.len
	call	fx_name
	mov	rdi, AST_D_FN
	mov	rsi, AST_F_PUBLICA
	mov	rdx, rax
	xor	rcx, rcx
	call	fx_decl
	mov	[fxv_dini], eax

	lea	rdi, [fxn_m]
	mov	rsi, fxn_m.len
	call	fx_name
	mov	rdi, AST_D_PARAM
	xor	rsi, rsi
	mov	rdx, rax
	mov	ecx, [fxv_dini]
	call	fx_decl
	mov	[fxv_dm], eax

	lea	rdi, [fxn_a]
	mov	rsi, fxn_a.len
	call	fx_name
	mov	rdi, AST_D_BINDING
	xor	rsi, rsi
	mov	rdx, rax
	mov	ecx, [fxv_dini]
	call	fx_decl
	mov	[fxv_da], eax

	lea	rdi, [fxn_amb]
	mov	rsi, fxn_amb.len
	call	fx_name
	mov	rdi, AST_D_SUB
	xor	rsi, rsi
	mov	rdx, rax
	mov	ecx, [fxv_dini]
	call	fx_decl
	mov	[fxv_dsub], eax

	lea	rdi, [fxn_s]
	mov	rsi, fxn_s.len
	call	fx_name
	mov	rdi, AST_D_BINDING
	mov	rsi, AST_F_MEMRES
	mov	rdx, rax
	mov	ecx, [fxv_dini]
	call	fx_decl
	mov	[fxv_dsc], eax

	lea	rdi, [fxtree]
	call	ast_cap_push
	mov	[fxv_capb], eax
	call	fx_captypes

	; ---- the three `fn` types ----
	mov	rdi, 0
	mov	rsi, 0
	mov	rdx, 0
	mov	ecx, [fxv_tex]
	xor	r8, r8
	call	fx_fnty
	mov	edi, [fxv_dsal]
	mov	rsi, rax
	call	fx_dty

	call	fx_row_sicut
	mov	[fxrow2], eax
	mov	edi, [fxv_scr]
	mov	esi, [fxv_tex]
	mov	rdx, 2
	mov	ecx, [fxv_men]
	mov	r8d, [fxrow2]
	call	fx_fnty
	mov	edi, [fxv_dimp]
	mov	rsi, rax
	call	fx_dty

	mov	edi, [fxv_cm]
	mov	rsi, 0
	mov	rdx, 1
	mov	ecx, [fxv_u8]
	xor	r8, r8
	call	fx_fnty
	mov	edi, [fxv_dini]
	mov	rsi, rax
	call	fx_dty

	; ---- the declaration types the walk reads ----
	mov	edi, [fxv_ds]
	mov	esi, [fxv_scr]
	call	fx_dty
	mov	edi, [fxv_dt]
	mov	esi, [fxv_tex]
	call	fx_dty
	mov	edi, [fxv_dm]
	mov	esi, [fxv_cm]
	call	fx_dty
	mov	edi, [fxv_da]
	mov	esi, [fxv_ca]
	call	fx_dty
	mov	edi, [fxv_dsub]
	mov	esi, [fxv_ca]
	call	fx_dty
	mov	edi, [fxv_dsc]
	mov	esi, [fxv_scr]
	call	fx_dty

	; ---- the three functions, then the module ----
	call	fx_saluta
	mov	[fxv_fn1], eax
	call	fx_imprime
	mov	[fxv_fn2], eax
	call	fx_initium
	mov	[fxv_fn3], eax

	mov	edi, [fxv_fn1]
	mov	esi, [fxv_fn2]
	mov	edx, [fxv_fn3]
	mov	rcx, 3
	call	fx_lst3
	mov	[fxof4], eax
	mov	[fxcn4], edx
	mov	rdi, AST_MODULE
	xor	rsi, rsi
	mov	edx, [fxof4]
	mov	ecx, [fxcn4]
	call	fx_node
	mov	r12d, eax
	lea	rdi, [fxtree]
	mov	esi, r12d
	call	ast_set_root

	; ---- the side tables, sized once every node exists ----
	lea	rdi, [fxtree]
	call	ast_side_alloc
	lea	rdi, [fxtree]
	mov	esi, [fxv_lit0]
	xor	rdx, rdx		; `redde 0;` -- konst is the VALUE, the
	call	ast_konst_set		;   `Lit` itself keeps only the text
	return
endp

; ---- examples/saluta.exsc ---------------------------------------------------
; publica functio saluta() -> textus { redde "Ave, mundus. ..."; }
proc fx_saluta
	uses	rbx, r12
	locals
		slot fxlit, dd
		slot fxred, dd
		slot fxblk, dd
		slot fxsig, dd
		slot fxrty, dd
		slot fxof5, dd
		slot fxcn5, dd
	endl
	lea	rdi, [fxlitbytes]
	mov	rsi, FXLIT_LEN
	call	fx_name
	mov	rdi, AST_LIT
	mov	rsi, AST_LIT_STRING
	mov	rdx, rax
	xor	rcx, rcx
	call	fx_node
	mov	[fxlit], eax
	mov	rdi, rax
	mov	esi, [fxv_tex]
	call	fx_nt

	mov	rdi, AST_REDDE
	xor	rsi, rsi
	mov	edx, [fxlit]
	xor	rcx, rcx
	call	fx_node
	mov	[fxred], eax

	mov	edi, [fxred]
	xor	rsi, rsi
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_lst3
	mov	[fxof5], eax
	mov	[fxcn5], edx
	mov	rdi, AST_BLOCK
	xor	rsi, rsi
	mov	edx, [fxof5]
	mov	ecx, [fxcn5]
	call	fx_node
	mov	[fxblk], eax

	lea	rdi, [fxn_textus]
	mov	rsi, fxn_textus.len
	call	fx_name
	mov	rdi, rax
	call	fx_ty
	mov	[fxrty], eax
	mov	rdi, AST_SIG
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_node
	mov	[fxsig], eax
	mov	rdi, rax
	mov	esi, [fxrty]		; `Sig.c` -- the result type expression
	xor	rdx, rdx
	call	fx_nd

	mov	rdi, AST_FN
	mov	rsi, AST_FN_PUBLICA
	mov	edx, [fxsig]
	mov	ecx, [fxblk]
	call	fx_node
	push	rax
	mov	rdi, rax
	xor	rsi, rsi
	mov	edx, [fxv_dsal]
	call	fx_nd
	pop	rax
	return
endp

; ---- examples/imprime.exsc --------------------------------------------------
; publica functio imprime_gutenbergio(s: Scriptor, t: textus) -> mensura
;         poscit sicut s { redde s.scribe(t); }
proc fx_imprime
	uses	rbx, r12
	locals
		slot fxps, dd
		slot fxpt, dd
		slot fxtys, dd
		slot fxsig2, dd
		slot fxpths, dd
		slot fxmem, dd
		slot fxptht, dd
		slot fxcall, dd
		slot fxred2, dd
		slot fxblk2, dd
		slot fxof6, dd
		slot fxcn6, dd
	endl
	lea	rdi, [fxn_scriptor]
	mov	rsi, fxn_scriptor.len
	call	fx_name
	mov	rdi, rax
	call	fx_ty
	mov	[fxtys], eax
	lea	rdi, [fxn_s]
	mov	rsi, fxn_s.len
	call	fx_name
	mov	rdi, AST_PARAM
	xor	rsi, rsi
	mov	rdx, rax
	mov	ecx, [fxtys]
	call	fx_node
	mov	[fxps], eax
	mov	rdi, rax
	xor	rsi, rsi
	mov	edx, [fxv_ds]
	call	fx_nd

	lea	rdi, [fxn_textus]
	mov	rsi, fxn_textus.len
	call	fx_name
	mov	rdi, rax
	call	fx_ty
	mov	[fxtys], eax
	lea	rdi, [fxn_t]
	mov	rsi, fxn_t.len
	call	fx_name
	mov	rdi, AST_PARAM
	xor	rsi, rsi
	mov	rdx, rax
	mov	ecx, [fxtys]
	call	fx_node
	mov	[fxpt], eax
	mov	rdi, rax
	xor	rsi, rsi
	mov	edx, [fxv_dt]
	call	fx_nd

	mov	edi, [fxps]
	mov	esi, [fxpt]
	xor	rdx, rdx
	mov	rcx, 2
	call	fx_lst3
	mov	[fxof6], eax
	mov	[fxcn6], edx
	mov	rdi, AST_SIG
	xor	rsi, rsi
	mov	edx, [fxof6]
	mov	ecx, [fxcn6]
	call	fx_node
	mov	[fxsig2], eax

	; `s.scribe(t)` -- `Member.d` is ZERO for a prelude member, which is
	; checker/types/prim.inc's representation and what the lowering keys on
	lea	rdi, [fxn_s]
	mov	rsi, fxn_s.len
	call	fx_name
	mov	rdi, rax
	mov	esi, [fxv_ds]
	call	fx_seg
	mov	[fxpths], eax
	mov	rdi, rax
	mov	esi, [fxv_scr]
	call	fx_nt

	lea	rdi, [fxn_scribe]
	mov	rsi, fxn_scribe.len
	call	fx_name
	mov	rdi, AST_MEMBER
	xor	rsi, rsi
	mov	edx, [fxpths]
	mov	rcx, rax
	call	fx_node
	mov	[fxmem], eax

	lea	rdi, [fxn_t]
	mov	rsi, fxn_t.len
	call	fx_name
	mov	rdi, rax
	mov	esi, [fxv_dt]
	call	fx_seg
	mov	[fxptht], eax
	mov	rdi, rax
	mov	esi, [fxv_tex]
	call	fx_nt

	mov	edi, [fxptht]
	xor	rsi, rsi
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_lst3
	mov	[fxof6], eax
	mov	[fxcn6], edx
	mov	rdi, AST_CALL
	xor	rsi, rsi
	mov	edx, [fxmem]
	mov	ecx, [fxof6]
	call	fx_node
	mov	[fxcall], eax
	mov	rdi, rax
	mov	esi, [fxcn6]
	xor	rdx, rdx
	call	fx_nd
	mov	edi, [fxcall]
	mov	esi, [fxv_men]
	call	fx_nt

	mov	rdi, AST_REDDE
	xor	rsi, rsi
	mov	edx, [fxcall]
	xor	rcx, rcx
	call	fx_node
	mov	[fxred2], eax
	mov	edi, eax
	xor	rsi, rsi
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_lst3
	mov	[fxof6], eax
	mov	[fxcn6], edx
	mov	rdi, AST_BLOCK
	xor	rsi, rsi
	mov	edx, [fxof6]
	mov	ecx, [fxcn6]
	call	fx_node
	mov	[fxblk2], eax

	mov	rdi, AST_FN
	mov	rsi, AST_FN_PUBLICA
	mov	edx, [fxsig2]
	mov	ecx, [fxblk2]
	call	fx_node
	push	rax
	mov	rdi, rax
	xor	rsi, rsi
	mov	edx, [fxv_dimp]
	call	fx_nd
	pop	rax
	return
endp

; fx_lst5(fxg1, fxg2, fxg3, fxg4, fxg5) -> eax = offset, edx = count.
proc fx_lst5, fxg1, fxg2, fxg3, fxg4, fxg5
	uses	rbx
	locals
		slot fxmk3, dq
	endl
	lea	rdi, [fxtree]
	call	ast_list_mark
	mov	[fxmk3], rax
	lea	rdi, [fxtree]
	mov	rsi, [fxg1]
	call	ast_list_push
	lea	rdi, [fxtree]
	mov	rsi, [fxg2]
	call	ast_list_push
	lea	rdi, [fxtree]
	mov	rsi, [fxg3]
	call	ast_list_push
	lea	rdi, [fxtree]
	mov	rsi, [fxg4]
	call	ast_list_push
	lea	rdi, [fxtree]
	mov	rsi, [fxg5]
	call	ast_list_push
	lea	rdi, [fxtree]
	mov	rsi, [fxmk3]
	call	ast_list_emit
	return
endp

; ---- examples/initium.exsc --------------------------------------------------
; publica functio initium(m: Mundus) -> u8 {
;     firma a = m.ambitus();
;     sub ambitus = a;
;     firma s = Scriptor.ad_exitum(a);
;     imprime_gutenbergio(s, saluta());
;     redde 0;
; }
proc fx_initium
	uses	rbx, r12
	locals
		slot fxpm, dd
		slot fxtym, dd
		slot fxsig3, dd
		slot fxpthm, dd
		slot fxmem1, dd
		slot fxcall1, dd
		slot fxbnda, dd
		slot fxpatom, dd
		slot fxptha, dd
		slot fxsubn, dd
		slot fxpscr, dd
		slot fxmem2, dd
		slot fxptha2, dd
		slot fxcall2, dd
		slot fxbnds, dd
		slot fxpthsc, dd
		slot fxpsal, dd
		slot fxcsal, dd
		slot fxpimp, dd
		slot fxcimp, dd
		slot fxest, dd
		slot fxlit0, dd
		slot fxred3, dd
		slot fxblk3, dd
		slot fxof7, dd
		slot fxcn7, dd
	endl
	; ---- the parameter ----
	lea	rdi, [fxn_mundus]
	mov	rsi, fxn_mundus.len
	call	fx_name
	mov	rdi, rax
	call	fx_ty
	mov	[fxtym], eax
	lea	rdi, [fxn_m]
	mov	rsi, fxn_m.len
	call	fx_name
	mov	rdi, AST_PARAM
	xor	rsi, rsi
	mov	rdx, rax
	mov	ecx, [fxtym]
	call	fx_node
	mov	[fxpm], eax
	mov	rdi, rax
	xor	rsi, rsi
	mov	edx, [fxv_dm]
	call	fx_nd

	; ---- statement 1: firma a = m.ambitus(); ----
	lea	rdi, [fxn_m]
	mov	rsi, fxn_m.len
	call	fx_name
	mov	rdi, rax
	mov	esi, [fxv_dm]
	call	fx_seg
	mov	[fxpthm], eax
	mov	rdi, rax
	mov	esi, [fxv_cm]
	call	fx_nt
	lea	rdi, [fxn_amb]
	mov	rsi, fxn_amb.len
	call	fx_name
	mov	rdi, AST_MEMBER
	xor	rsi, rsi
	mov	edx, [fxpthm]
	mov	rcx, rax
	call	fx_node
	mov	[fxmem1], eax
	mov	rdi, AST_CALL
	xor	rsi, rsi
	mov	edx, [fxmem1]
	xor	rcx, rcx
	call	fx_node
	mov	[fxcall1], eax
	mov	rdi, rax
	mov	esi, [fxv_ca]
	call	fx_nt
	mov	rdi, AST_BINDING
	xor	rsi, rsi
	xor	rdx, rdx
	mov	ecx, [fxcall1]
	call	fx_node
	mov	[fxbnda], eax
	mov	rdi, rax
	xor	rsi, rsi
	mov	edx, [fxv_da]
	call	fx_nd

	; ---- statement 2: sub ambitus = a; ----
	lea	rdi, [fxn_amb]
	mov	rsi, fxn_amb.len
	call	fx_name
	mov	rdi, rax
	mov	esi, [fxv_capb]
	add	esi, AST_CAP_AMBITUS - 1
	call	fx_seg
	mov	[fxpatom], eax
	lea	rdi, [fxn_a]
	mov	rsi, fxn_a.len
	call	fx_name
	mov	rdi, rax
	mov	esi, [fxv_da]
	call	fx_seg
	mov	[fxptha], eax
	mov	rdi, rax
	mov	esi, [fxv_ca]
	call	fx_nt
	mov	rdi, AST_SUB
	xor	rsi, rsi
	mov	edx, [fxpatom]
	mov	ecx, [fxptha]
	call	fx_node
	mov	[fxsubn], eax
	mov	rdi, rax
	xor	rsi, rsi
	mov	edx, [fxv_dsub]
	call	fx_nd

	; ---- statement 3: firma s = Scriptor.ad_exitum(a); ----
	; the receiver is a TYPE, and `Scriptor` is the one prelude name in
	; module scope, so its `Path.d` carries the tag
	lea	rdi, [fxn_scriptor]
	mov	rsi, fxn_scriptor.len
	call	fx_name
	mov	rdi, rax
	mov	rsi, LWR_PRELUDE_TAG or 1
	call	fx_seg
	mov	[fxpscr], eax
	mov	rdi, rax
	mov	esi, [fxv_scr]
	call	fx_nt
	lea	rdi, [fxn_adex]
	mov	rsi, fxn_adex.len
	call	fx_name
	mov	rdi, AST_MEMBER
	xor	rsi, rsi
	mov	edx, [fxpscr]
	mov	rcx, rax
	call	fx_node
	mov	[fxmem2], eax
	lea	rdi, [fxn_a]
	mov	rsi, fxn_a.len
	call	fx_name
	mov	rdi, rax
	mov	esi, [fxv_da]
	call	fx_seg
	mov	[fxptha2], eax
	mov	rdi, rax
	mov	esi, [fxv_ca]
	call	fx_nt
	mov	edi, [fxptha2]
	xor	rsi, rsi
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_lst3
	mov	[fxof7], eax
	mov	[fxcn7], edx
	mov	rdi, AST_CALL
	xor	rsi, rsi
	mov	edx, [fxmem2]
	mov	ecx, [fxof7]
	call	fx_node
	mov	[fxcall2], eax
	mov	rdi, rax
	mov	esi, [fxcn7]
	xor	rdx, rdx
	call	fx_nd
	mov	edi, [fxcall2]
	mov	esi, [fxv_scr]
	call	fx_nt
	mov	rdi, AST_BINDING
	xor	rsi, rsi
	xor	rdx, rdx
	mov	ecx, [fxcall2]
	call	fx_node
	mov	[fxbnds], eax
	mov	rdi, rax
	xor	rsi, rsi
	mov	edx, [fxv_dsc]
	call	fx_nd

	; ---- statement 4: imprime_gutenbergio(s, saluta()); ----
	lea	rdi, [fxn_s]
	mov	rsi, fxn_s.len
	call	fx_name
	mov	rdi, rax
	mov	esi, [fxv_dsc]
	call	fx_seg
	mov	[fxpthsc], eax
	mov	rdi, rax
	mov	esi, [fxv_scr]
	call	fx_nt
	lea	rdi, [fxn_saluta]
	mov	rsi, fxn_saluta.len
	call	fx_name
	mov	rdi, rax
	mov	esi, [fxv_dsal]
	call	fx_seg
	mov	[fxpsal], eax
	mov	rdi, AST_CALL
	xor	rsi, rsi
	mov	edx, [fxpsal]
	xor	rcx, rcx
	call	fx_node
	mov	[fxcsal], eax
	mov	rdi, rax
	mov	esi, [fxv_tex]
	call	fx_nt
	lea	rdi, [fxn_imp]
	mov	rsi, fxn_imp.len
	call	fx_name
	mov	rdi, rax
	mov	esi, [fxv_dimp]
	call	fx_seg
	mov	[fxpimp], eax
	mov	edi, [fxpthsc]
	mov	esi, [fxcsal]
	xor	rdx, rdx
	mov	rcx, 2
	call	fx_lst3
	mov	[fxof7], eax
	mov	[fxcn7], edx
	mov	rdi, AST_CALL
	xor	rsi, rsi
	mov	edx, [fxpimp]
	mov	ecx, [fxof7]
	call	fx_node
	mov	[fxcimp], eax
	mov	rdi, rax
	mov	esi, [fxcn7]
	xor	rdx, rdx
	call	fx_nd
	mov	edi, [fxcimp]
	mov	esi, [fxv_men]
	call	fx_nt
	mov	rdi, AST_EXPRSTMT
	xor	rsi, rsi
	mov	edx, [fxcimp]
	xor	rcx, rcx
	call	fx_node
	mov	[fxest], eax

	; ---- statement 5: redde 0; ----
	lea	rdi, [fxn_zero]
	mov	rsi, fxn_zero.len
	call	fx_name
	mov	rdi, AST_LIT
	mov	rsi, AST_LIT_INT
	mov	rdx, rax
	xor	rcx, rcx
	call	fx_node
	mov	[fxlit0], eax
	mov	[fxv_lit0], eax
	mov	rdi, rax
	mov	esi, [fxv_u8]
	call	fx_nt
	mov	rdi, AST_REDDE
	xor	rsi, rsi
	mov	edx, [fxlit0]
	xor	rcx, rcx
	call	fx_node
	mov	[fxred3], eax

	; ---- the body block and the function ----
	mov	edi, [fxbnda]
	mov	esi, [fxsubn]
	mov	edx, [fxbnds]
	mov	ecx, [fxest]
	mov	r8d, [fxred3]
	call	fx_lst5
	mov	[fxof7], eax
	mov	[fxcn7], edx
	mov	rdi, AST_BLOCK
	xor	rsi, rsi
	mov	edx, [fxof7]
	mov	ecx, [fxcn7]
	call	fx_node
	mov	[fxblk3], eax

	mov	edi, [fxpm]
	xor	rsi, rsi
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_lst3
	mov	[fxof7], eax
	mov	[fxcn7], edx
	mov	rdi, AST_SIG
	xor	rsi, rsi
	mov	edx, [fxof7]
	mov	ecx, [fxcn7]
	call	fx_node
	mov	[fxsig3], eax

	mov	rdi, AST_FN
	mov	rsi, AST_FN_PUBLICA
	mov	edx, [fxsig3]
	mov	ecx, [fxblk3]
	call	fx_node
	push	rax
	mov	rdi, rax
	xor	rsi, rsi
	mov	edx, [fxv_dini]
	call	fx_nd
	pop	rax
	return
endp

segment readable writeable
  fxar:		rb sizeof.Arena
  fxiar:	rb sizeof.Arena
  fxscr:	rb sizeof.Arena
  fxnames:	rb sizeof.Interner
  fxtree:	rb sizeof.Ast
  fxmod:	rb sizeof.BfaModule
  fxvb:		dd 0
  fxvi:		dd 0
  fxoutp:	dq 0
  fxoutl:	dq 0

  ; type ids
  fxv_tex:	dd 0
  fxv_men:	dd 0
  fxv_u8:	dd 0
  fxv_scr:	dd 0
  fxv_cm:	dd 0
  fxv_ca:	dd 0
  ; declaration ids
  fxv_dsal:	dd 0
  fxv_dimp:	dd 0
  fxv_ds:	dd 0
  fxv_dt:	dd 0
  fxv_dini:	dd 0
  fxv_dm:	dd 0
  fxv_da:	dd 0
  fxv_dsub:	dd 0
  fxv_dsc:	dd 0
  fxv_capb:	dd 0
  ; node ids
  fxv_fn1:	dd 0
  fxv_fn2:	dd 0
  fxv_fn3:	dd 0
  fxv_lit0:	dd 0

  fxn_saluta	db 'saluta'
  .len = $ - fxn_saluta
  fxn_imp	db 'imprime_gutenbergio'
  .len = $ - fxn_imp
  fxn_ini	db 'initium'
  .len = $ - fxn_ini
  fxn_s		db 's'
  .len = $ - fxn_s
  fxn_t		db 't'
  .len = $ - fxn_t
  fxn_m		db 'm'
  .len = $ - fxn_m
  fxn_a		db 'a'
  .len = $ - fxn_a
  fxn_amb	db 'ambitus'
  .len = $ - fxn_amb
  fxn_adex	db 'ad_exitum'
  .len = $ - fxn_adex
  fxn_scribe	db 'scribe'
  .len = $ - fxn_scribe
  fxn_scriptor	db 'Scriptor'
  .len = $ - fxn_scriptor
  fxn_zero	db '0'
  .len = $ - fxn_zero
  fxn_textus	db 'textus'
  .len = $ - fxn_textus
  fxn_mundus	db 'Mundus'
  .len = $ - fxn_mundus

  ; The STRING literal exactly as the lexer interns it: the token's bytes,
  ; quotes included, escapes undecoded (AST 2.3). The 101 bytes between the
  ; quotes are `examples/saluta.expected` itself, so this fixture cannot drift
  ; from the example -- edit the example and this either still passes or says
  ; it stopped.
  fxlitbytes:	db '"'
		file '../../examples/saluta.expected'
		db '"'
  FXLIT_LEN = $ - fxlitbytes

  ; ---- what `lwr_module` must print ------------------------------------
  fxexp:
  db "data $0 101 1 4176652c206d756e6475732e0a0a45782073696c656e74696f2073757267697420666f726d612e0a4578207369676e6f206e6173636974757220766f782e0a457820636f6469636520666974206c756d656e2e0a0a486f64696520696e636970696d75732e", 10
  db "functio @exsrt_mundus_ambitus (ptr) -> ptr numeri ad_parem vetita explicita conservata externus sysv_amd64 {", 10
  db "}", 10
  db "functio @exsrt_scriptor_ad_exitum (ptr ptr) -> void numeri ad_parem vetita explicita conservata externus sysv_amd64 {", 10
  db "}", 10
  db "functio @exsrt_scriptor_scribe (ptr ptr) -> u64 numeri ad_parem vetita explicita conservata externus sysv_amd64 {", 10
  db "}", 10
  db "functio @saluta (ptr) -> void numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = gaddr 0", 10
  db "store ptr %0 0 nativus %1", 10
  db "%2 = iconst u64 101", 10
  db "store u64 %0 8 nativus %2", 10
  db "ret ", 10
  db "}", 10
  db "functio @imprime_gutenbergio (ptr ptr) -> u64 numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = param ptr 1", 10
  db "%2 = call u64 @exsrt_scriptor_scribe %0 %1", 10
  db "ret %2", 10
  db "}", 10
  db "functio @initium (ptr) -> u8 numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = call ptr @exsrt_mundus_ambitus %0", 10
  db "%2 = slot 16 8", 10
  db "call void @exsrt_scriptor_ad_exitum %2 %1", 10
  db "%3 = slot 16 8", 10
  db "call void @saluta %3", 10
  db "%4 = call u64 @imprime_gutenbergio %2 %3", 10
  db "%5 = iconst u8 0", 10
  db "ret %5", 10
  db "}", 10
  .len = $ - fxexp

segment readable
  ; cst/ -> lexer/ -> shared/unicode/ needs the UCD blobs, exactly as
  ; exsc.asm and every other fixture on that chain includes them.
  include '../../compiler/shared/unicode/tables/tables.inc'
