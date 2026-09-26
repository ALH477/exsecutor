; tests/unit/lwr_contrahe.asm
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
; lowering fixture: `contrahe` -- spec §5.4's declared reduction, lowered to
; the three IR instructions docs/design/ssa-ir.md 2.6 gives it
; (`redinit`/`contrib`/`redfin`), per docs/design/lowering.md section 2.5. From
; SOURCE: lexed, parsed, walked and checked by the same routines `exsc` runs,
; then `lwr_module`, `bfa_verify_func` over every function -- rule 5 is the one
; that matters here, "the `redinit` dominates every `contrib` and the `redfin`
; post-dominates every `contrib`" -- and `bfa_print_module` compared BYTE FOR
; BYTE against lwr_contrahe.expected.
;
; Five cases, one function each, in the order lowering.md 2.5's text raises
; them:
;
;   summa         ordinata, `+` on `u64`, and THE READ AFTER THE LOOP: `redde
;                 s` resolves to the `redfin` result, because the exit's
;                 `writeVariable(acc, exit, %r)` is what the accumulator's
;                 binding holds from the exit onward (spec §5.4: "After the
;                 loop `acc` is an ordinary binding").
;   ordinata_f32  `forma ordinata` on `f32`: the op word is `fadd`, not `add`
;                 -- the operator is chosen by F, and float addition is not
;                 integer addition.
;   arborea_i64   `forma arborea 8` with `+%` on `i64`: the SHAPE and the
;                 WIDTH both reach the `redinit`, and `+%` is `addw`. On a
;                 `quisque`, so the header also carries `quisque` -- §5.4's
;                 shape plus §8.5's independence claim, which is the pair
;                 §5.5's bit-identity rests on.
;   duo           two accumulators in ONE head: two `redinit`s in clause
;                 order, two `redfin`s in the same order, and each `contrib`
;                 against its own handle. `*` on `u64` is `mul`, the trapping
;                 form.
;   nidus         nested reductions, each with its own `contrahe`: the inner
;                 loop's handle is pushed above the outer's and popped at the
;                 inner exit, so the outer body's second contribution still
;                 finds the outer handle.
;
; ---------------------------------------------------------------------------
; HISTORY: WHY THIS FIXTURE ONCE TOLERATED EXACTLY TWO CODES. Spec §5.4 is
; explicit: inside the body `acc = e;` CONTRIBUTES `e`, and after the loop
; `acc` is an ordinary binding. When this fixture was written Stage 2
; implemented neither sentence and this source was refused by `exsc`:
;
;   C1  `EXS-E0303` on every contribution. `__chk_ty_assign` types the
;       right-hand side with the left-hand side's type as its expectation, and
;       the left-hand side of a contribution is the accumulator, whose
;       `Decl.ty` is `red.F` -- so `s = v[i]` is reported as `f32` against
;       `red`. The contribution is not an assignment and must not be typed as
;       one.
;   C2  `EXS-E0301` on `redde s`. `checker/resolve/resolve.inc`'s `.for` arm
;       binds the accumulator in the LOOP's frame and pops it with the body,
;       so the name does not resolve after the loop at all.
;
; Both were the checker's (docs/design/checker.md finding 27) and both are
; CLOSED on the same day: `red` now carries F in its `b`, the contribution is
; typed against F, and the accumulator is bound in the enclosing scope
; (tests/unit/chk_ty_contrahe.asm pins all five of this fixture's shapes
; clean, and every rejected shape at one code). So this fixture now requires
; what it always should have:
;
;   - `fx_front` requires an EMPTY diagnostic vector. The tolerance for codes
;     303 and 301 that stood between the lowering landing and the checker
;     catching up is gone; any diagnostic is exit 11.
;   - `fx_patch` is now a CHECK, not a patch: it asserts the post-loop read's
;     `Path` already names the accumulator declaration (`d` set by Stage 2).
;     A tree where it is 0 -- C2 regressing -- is exit 14, where it used to
;     be silently patched.
;
; ---------------------------------------------------------------------------
; Exit 0 = the printed module matches. 10 = setup, 11 = the front end raised
; any diagnostic, 12 = `lwr_module` refused, 13 = the verifier
; named a rule, 14 = the post-loop read does not name its accumulator (C2
; regressed) or the tree shape moved, 20 = length
; mismatch, 21 = byte mismatch. Run with any argument to write the produced
; text to stdout instead of comparing.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

; ---- C2's two fields, and the tree they are in -----------------------------
; Node and declaration ids of `summa`'s `redde s`, which is the FIRST function
; in the source below so that editing a later one cannot move them. Every one
; is rassert-guarded in `fx_patch`: a source edit that moves them fails the
; fixture at the guard rather than patching the wrong node.
FX_RD_PATH  = 19		; the `Path` of `redde s`
FX_RD_SEG   = 18		; its one `Seg`
FX_RD_ACC   = 4			; the `AST_D_ACCUM` declaration `s`
FX_RD_TYSRC = 14		; the contribution's right-hand side, whose
				; `Node.ty` is F

segment readable executable
  start:
	mov	rax, [rsp]			; argc, before anything moves rsp
	mov	[fx_argc], rax
	call	fx_setup
	test	eax, eax
	jnz	.bad10

	call	fx_front
	test	rax, rax
	jnz	.bad11

	call	fx_patch
	test	eax, eax
	jnz	.bad14

	lea	rdi, [fx_mod]
	lea	rsi, [fx_arena]
	call	bfa_module_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_mod]
	lea	rdx, [fx_arena]
	lea	rcx, [fx_lscr]
	call	lwr_module
	test	rax, rax
	jnz	.bad12

	; every function with a body, through the verifier
	xor	r12, r12
  .vloop:
	mov	rdi, [fx_mod + BfaModule.funcs]
	cmp	r12, [rdi + Vec.len]
	jae	.vdone
	mov	rsi, r12
	call	vec_get
	mov	rsi, [rax]
	mov	rdi, [rsi + BfaFunc.blocks]
	cmp	qword [rdi + Vec.len], 0
	je	.vnext
	lea	rdi, [fx_mod]
	lea	rdx, [fx_arena]
	lea	rcx, [fx_vb]
	lea	r8,  [fx_vi]
	call	bfa_verify_func
	test	eax, eax
	jnz	.bad13
  .vnext:
	inc	r12
	jmp	.vloop
  .vdone:

	lea	rdi, [fx_mod]
	lea	rsi, [fx_arena]
	call	bfa_print_module
	mov	[fx_outp], rax
	mov	[fx_outl], rdx
	cmp	qword [fx_argc], 1
	ja	.dump

	cmp	qword [fx_outl], FX_EXP_LEN
	jne	.bad20
	mov	rdi, [fx_outp]
	mov	rsi, [fx_outl]
	lea	rdx, [fx_exp]
	mov	rcx, FX_EXP_LEN
	call	__bfa_streq
	test	eax, eax
	jz	.bad21
	xor	edi, edi
	call	sys_exit_group
  .dump:
	mov	edi, 1
	mov	rsi, [fx_outp]
	mov	rdx, [fx_outl]
	call	sys_write
	xor	edi, edi
	call	sys_exit_group
  .bad10:
	mov	edi, 10
	call	sys_exit_group
  .bad11:
	mov	edi, 11
	call	sys_exit_group
  .bad12:
	mov	edi, 12
	call	sys_exit_group
  .bad13:
	mov	edi, 13
	call	sys_exit_group
  .bad14:
	mov	edi, 14
	call	sys_exit_group
  .bad20:
	mov	edi, 2
	mov	rsi, [fx_outp]
	mov	rdx, [fx_outl]
	call	sys_write
	mov	edi, 20
	call	sys_exit_group
  .bad21:
	mov	edi, 2
	mov	rsi, [fx_outp]
	mov	rdx, [fx_outl]
	call	sys_write
	mov	edi, 21
	call	sys_exit_group

; the whole chain, as compiler/x86_64/exsc.asm includes it
include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'
include '../../compiler/x86_64/lower/lower.inc'

; ---- harness ---------------------------------------------------------------
; Plain labels (a `proc` argument name is an unmangled global); each helper
; pushes an odd number of registers.

; fx_patch -> eax = 0 iff `summa`'s post-loop read already names its
; accumulator (Stage 2 wrote `Path.d`); a 0 there is C2 back, exit 14. The name
; is historical -- it patched those fields once; see the header.
  fx_patch:
	push	rbx
	lea	rdi, [fx_tree]
	mov	rsi, FX_RD_ACC
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_ACCUM
	jne	.bad
	lea	rdi, [fx_tree]
	mov	rsi, FX_RD_TYSRC
	call	ast_node_at
	mov	ebx, [rax + AstNode.ty]		; F, as the checker typed the
	test	ebx, ebx			; contribution
	jz	.bad
	lea	rdi, [fx_tree]
	mov	rsi, FX_RD_PATH
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_PATH
	jne	.bad
	mov	ecx, [rax + AstNode.d]
	cmp	ecx, FX_RD_ACC			; Stage 2 resolved it to the
	jne	.bad				; accumulator, or C2 is back
	cmp	[rax + AstNode.ty], ebx		; and typed it as F
	jne	.bad
  .already:
	xor	eax, eax
	pop	rbx
	ret
  .bad:
	mov	eax, 1
	pop	rbx
	ret

; fx_diags_ok -> eax = 0 iff the front end raised NO diagnostic. (It once
; admitted codes 303 and 301 -- see the header.)
  fx_diags_ok:
	push	rbx
	xor	eax, eax
	cmp	qword [fx_diags + Vec.len], 0
	je	.ok
	mov	eax, 1
  .ok:
	pop	rbx
	ret

; fx_front -> rax = 0 once the lexer, the parser and the checker have run and
; said nothing.
  fx_front:
	push	rbx
	lea	rdi, [fx_toks]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_diags]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 32
	call	vec_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_toks]
	lea	r8,  [fx_diags]
	call	lex_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_src]
	mov	rdx, FX_SRC_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	jc	.bad
	lea	rdi, [fx_green]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 512
	call	vec_init
	lea	rdi, [fx_work]
	lea	rsi, [fx_arena]
	mov	rdx, 4
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_cmap]
	lea	rsi, [fx_arena]
	mov	rdx, 1024
	call	map_init
	lea	rdi, [fx_ctree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_green]
	lea	r8,  [fx_work]
	lea	r9,  [fx_cmap]
	call	cst_tree_init
	lea	rdi, [fx_parser]
	lea	rsi, [fx_lx]
	lea	rdx, [fx_ctree]
	call	cst_parse
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	cmp	qword [fx_diags + Vec.len], 0
	jne	.bad
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chk]
	mov	rsi, 64			; --hospes x86_64-linux (spec §9.5)
	call	chk_set_target
	lea	rdi, [fx_chk]
	lea	rsi, [fx_src]
	mov	rdx, FX_SRC_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	call	fx_diags_ok
	test	eax, eax
	jnz	.bad
	xor	rax, rax
	pop	rbx
	ret
  .bad:
	mov	rax, -1
	pop	rbx
	ret

; fx_setup -> eax = 0 once the four arenas and the interner exist.
  fx_setup:
	push	rbx
	lea	rdi, [fx_arena]
	mov	rsi, 32 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_iarena]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_scratch]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_lscr]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_names]
	lea	rsi, [fx_iarena]
	mov	rdx, 1024
	call	intern_init
	xor	eax, eax
	pop	rbx
	ret
  .bad:
	mov	eax, 1
	pop	rbx
	ret

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

  fx_path	db 'lwr_contrahe.exsc'
  FX_PATH_LEN = $ - fx_path

  ; `summa` FIRST: FX_RD_* above are its node ids.
  fx_src:	db 'functio summa(n: u64) -> u64 {', 10
		db 9, 'per i in 0..n contrahe s: + forma ordinata {', 10
		db 9, 9, 's = i;', 10
		db 9, '}', 10
		db 9, 'redde s;', 10
		db '}', 10
		db 'functio ordinata_f32(v: acies<f32, 4>) {', 10
		db 9, 'per i in 0..4 contrahe s: + forma ordinata {', 10
		db 9, 9, 's = v[i];', 10
		db 9, '}', 10
		db '}', 10
		db 'functio arborea_i64(v: acies<i64, 8>) {', 10
		db 9, 'quisque i in 0..8 contrahe t: +% forma arborea 8 {', 10
		db 9, 9, 't = v[i];', 10
		db 9, '}', 10
		db '}', 10
		db 'functio duo(v: acies<u64, 4>) {', 10
		db 9, 'per i in 0..4 contrahe a: + contrahe b: * forma ordinata {', 10
		db 9, 9, 'a = v[i];', 10
		db 9, 9, 'b = v[i];', 10
		db 9, '}', 10
		db '}', 10
		db 'functio nidus(v: acies<u64, 4>) {', 10
		db 9, 'per i in 0..4 contrahe a: + forma ordinata {', 10
		db 9, 9, 'a = v[i];', 10
		db 9, 9, 'per j in 0..4 contrahe b: + forma ordinata {', 10
		db 9, 9, 9, 'b = v[j];', 10
		db 9, 9, '}', 10
		db 9, '}', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src

  ; ---- what `lwr_module` must print --------------------------------------
  fx_exp:	file 'lwr_contrahe.expected'
  FX_EXP_LEN = $ - fx_exp

segment readable writeable
  fx_argc:	rq 1
  fx_outp:	rq 1
  fx_outl:	rq 1
  fx_vb:	dd 0
  fx_vi:	dd 0
  fx_arena:	rb sizeof.Arena
  fx_iarena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_lscr:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_toks:	rb sizeof.Vec
  fx_diags:	rb sizeof.Vec
  fx_lx:	rb sizeof.Lexer
  fx_green:	rb sizeof.Vec
  fx_work:	rb sizeof.Vec
  fx_cmap:	rb sizeof.Map
  fx_ctree:	rb sizeof.CstTree
  fx_parser:	rb sizeof.CstParser
  fx_tree:	rb sizeof.Ast
  fx_chk:	rb sizeof.ChkCtx
  fx_mod:	rb sizeof.BfaModule
