; tests/unit/lwr_aciesops.asm
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 The Exsecutor authors.
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
; lowering fixture: WHOLE-ACY ARITHMETIC (spec §5.4's acies row, stage 5.2b),
; from SOURCE to verified IR -- lexed, parsed, walked and checked by the
; routines `exsc` runs, then `lwr_module`, `bfa_verify_func` over every
; function, and `bfa_print_module` compared BYTE FOR BYTE against the text
; below.
;
; WHAT EACH FUNCTION PINS:
;   summa8     `a + b` over `acies<f32, 8>`: the acies bases are the vector
;              images (bit baggage, tests/ir/vec_mem.ir), so the op is ONE
;              `vadd vf32.8` between two `load vf32.8 %p 0 nativus` and one
;              `store vf32.8 %t 0 nativus` into a `slot 32 4` temp -- and
;              `__lwr_binary` yields the temp's ADDRESS, which the binding
;              then `copy`s into `s`'s own slot, aggregate residence exactly
;              as a call or a literal would stage it.
;   opera8     the other three operators, `- * /` -> `vsub vmul vdiv`, same
;              staging, same width. One row per operator: a lowerer that
;              mapped the wrong AST_OP to a BFA opcode changes this text.
;   gemina8    the f64 spellings end to end: all four ops over `acies<f64,
;              8>` -> `v* vf64.8`, one `slot 64 8` temp each.
;   quattuor   the 4-lane group: `vadd vf32.4` and a `slot 16 4` temp.
;   par        the 2-lane group in f64: `vdiv vf64.2`, `slot 16 8`.
;
; There is NO `acies<i64, 8> + acies<i64, 8>` row, on purpose: the checker's
; stage-5.2a gate (`__chk_ty_aciesbin`) refuses every whole-acy operation
; that is not `+ - * /` on two acies of one FLOAT element type and one lane
; count in {2, 4, 8} -- an integer element, a lane-count mismatch, a scalar
; mix and every other operator are all EXS-E0305 AT THE OPERATOR, pinned
; from source by tests/unit/chk_ty_aciesops.asm -- so such a node never
; reaches `lwr_module` at all, and a lowerer row for it would pin a state
; the checker has already made unreachable. The unary case is likewise the
; checker's (E0305 at the operator; the IR has no vector-negate opcode).
;
; The IR is what the emitter then compiles. This fixture exists because a
; running program cannot say WHICH instruction the lowering chose -- an
; `a + b` that fell back to eight scalar `fadd`s would still print the
; right bytes.
;
; Exit 0 = the printed module matches. 10 = setup, 11 = the front end said
; something, 12 = `lwr_module` refused, 13 = the verifier named a rule,
; 20 = length mismatch, 21 = byte mismatch. Run with any argument to write
; the produced text to stdout instead of comparing.
;
; TEST: run=yes expect-exit=0 audit=pass
include 'format/format.inc'

format ELF64 executable 3
entry start

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

; fx_front -> rax = diagnostics from the lexer, the parser and the checker
; together; the source is fixed, so anything but 0 is a broken fixture.
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

  fx_path	db 'lwr_aciesops.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_src:	db 'functio summa8(a: acies<f32, 8>, b: acies<f32, 8>) -> f32 {', 10
		db 9, 'firma s: acies<f32, 8> = a + b;', 10
		db 9, 'redde s[0];', 10
		db '}', 10
		db 'functio opera8(a: acies<f32, 8>, b: acies<f32, 8>) -> f32 {', 10
		db 9, 'firma d: acies<f32, 8> = a - b;', 10
		db 9, 'firma m: acies<f32, 8> = a * b;', 10
		db 9, 'firma q: acies<f32, 8> = a / b;', 10
		db 9, 'redde d[1] + m[2] + q[3];', 10
		db '}', 10
		db 'functio gemina8(a: acies<f64, 8>, b: acies<f64, 8>) -> f64 {', 10
		db 9, 'firma s: acies<f64, 8> = a + b;', 10
		db 9, 'firma d: acies<f64, 8> = a - b;', 10
		db 9, 'firma m: acies<f64, 8> = a * b;', 10
		db 9, 'firma q: acies<f64, 8> = a / b;', 10
		db 9, 'redde s[0] + d[1] + m[2] + q[3];', 10
		db '}', 10
		db 'functio quattuor(a: acies<f32, 4>, b: acies<f32, 4>) -> f32 {', 10
		db 9, 'firma s: acies<f32, 4> = a + b;', 10
		db 9, 'redde s[3];', 10
		db '}', 10
		db 'functio par(a: acies<f64, 2>, b: acies<f64, 2>) -> f64 {', 10
		db 9, 'firma q: acies<f64, 2> = a / b;', 10
		db 9, 'redde q[0];', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src

  ; ---- what `lwr_module` must print --------------------------------------
  ; One packed op per whole-acy operation: two nativus vector loads off the
  ; operand bases (offset 0 -- the acies base IS the vector image), one
  ; `v*` at the interned `vf*.N` type (the lane count is the type's `ref`;
  ; width 0 canonically, backend_fasmg/ir.inc's BFA_TK_VF32 comment), one
  ; nativus store into a `slot N*elem elem` temp, then the binding's
  ; `copy` into its own slot -- aggregate residence from temp to binding.
  fx_exp:		db 'functio @summa8 (ptr ptr) -> f32 numeri ad_parem vetita explicita conservata {', 10
				db 'b0:', 10
				db '%0 = param ptr 0', 10
				db '%1 = param ptr 1', 10
				db '%2 = slot 32 4', 10
				db '%3 = load vf32.8 %0 0 nativus', 10
				db '%4 = load vf32.8 %1 0 nativus', 10
				db '%5 = vadd vf32.8 %3 %4', 10
				db '%6 = slot 32 4', 10
				db 'store vf32.8 %6 0 nativus %5', 10
				db 'copy 32 %2 %6', 10
				db '%7 = iconst u64 0', 10
				db '%8 = iconst u64 8', 10
				db 'chk %7 %8', 10
				db '%9 = index %2 %7 4', 10
				db '%10 = load f32 %9 0 nativus', 10
				db 'ret %10', 10
				db '}', 10
				db 'functio @opera8 (ptr ptr) -> f32 numeri ad_parem vetita explicita conservata {', 10
				db 'b0:', 10
				db '%0 = param ptr 0', 10
				db '%1 = param ptr 1', 10
				db '%2 = slot 32 4', 10
				db '%3 = load vf32.8 %0 0 nativus', 10
				db '%4 = load vf32.8 %1 0 nativus', 10
				db '%5 = vsub vf32.8 %3 %4', 10
				db '%6 = slot 32 4', 10
				db 'store vf32.8 %6 0 nativus %5', 10
				db 'copy 32 %2 %6', 10
				db '%7 = slot 32 4', 10
				db '%8 = load vf32.8 %0 0 nativus', 10
				db '%9 = load vf32.8 %1 0 nativus', 10
				db '%10 = vmul vf32.8 %8 %9', 10
				db '%11 = slot 32 4', 10
				db 'store vf32.8 %11 0 nativus %10', 10
				db 'copy 32 %7 %11', 10
				db '%12 = slot 32 4', 10
				db '%13 = load vf32.8 %0 0 nativus', 10
				db '%14 = load vf32.8 %1 0 nativus', 10
				db '%15 = vdiv vf32.8 %13 %14', 10
				db '%16 = slot 32 4', 10
				db 'store vf32.8 %16 0 nativus %15', 10
				db 'copy 32 %12 %16', 10
				db '%17 = iconst u64 1', 10
				db '%18 = iconst u64 8', 10
				db 'chk %17 %18', 10
				db '%19 = index %2 %17 4', 10
				db '%20 = load f32 %19 0 nativus', 10
				db '%21 = iconst u64 2', 10
				db '%22 = iconst u64 8', 10
				db 'chk %21 %22', 10
				db '%23 = index %7 %21 4', 10
				db '%24 = load f32 %23 0 nativus', 10
				db '%25 = fadd f32 %20 %24', 10
				db '%26 = iconst u64 3', 10
				db '%27 = iconst u64 8', 10
				db 'chk %26 %27', 10
				db '%28 = index %12 %26 4', 10
				db '%29 = load f32 %28 0 nativus', 10
				db '%30 = fadd f32 %25 %29', 10
				db 'ret %30', 10
				db '}', 10
				db 'functio @gemina8 (ptr ptr) -> f64 numeri ad_parem vetita explicita conservata {', 10
				db 'b0:', 10
				db '%0 = param ptr 0', 10
				db '%1 = param ptr 1', 10
				db '%2 = slot 64 8', 10
				db '%3 = load vf64.8 %0 0 nativus', 10
				db '%4 = load vf64.8 %1 0 nativus', 10
				db '%5 = vadd vf64.8 %3 %4', 10
				db '%6 = slot 64 8', 10
				db 'store vf64.8 %6 0 nativus %5', 10
				db 'copy 64 %2 %6', 10
				db '%7 = slot 64 8', 10
				db '%8 = load vf64.8 %0 0 nativus', 10
				db '%9 = load vf64.8 %1 0 nativus', 10
				db '%10 = vsub vf64.8 %8 %9', 10
				db '%11 = slot 64 8', 10
				db 'store vf64.8 %11 0 nativus %10', 10
				db 'copy 64 %7 %11', 10
				db '%12 = slot 64 8', 10
				db '%13 = load vf64.8 %0 0 nativus', 10
				db '%14 = load vf64.8 %1 0 nativus', 10
				db '%15 = vmul vf64.8 %13 %14', 10
				db '%16 = slot 64 8', 10
				db 'store vf64.8 %16 0 nativus %15', 10
				db 'copy 64 %12 %16', 10
				db '%17 = slot 64 8', 10
				db '%18 = load vf64.8 %0 0 nativus', 10
				db '%19 = load vf64.8 %1 0 nativus', 10
				db '%20 = vdiv vf64.8 %18 %19', 10
				db '%21 = slot 64 8', 10
				db 'store vf64.8 %21 0 nativus %20', 10
				db 'copy 64 %17 %21', 10
				db '%22 = iconst u64 0', 10
				db '%23 = iconst u64 8', 10
				db 'chk %22 %23', 10
				db '%24 = index %2 %22 8', 10
				db '%25 = load f64 %24 0 nativus', 10
				db '%26 = iconst u64 1', 10
				db '%27 = iconst u64 8', 10
				db 'chk %26 %27', 10
				db '%28 = index %7 %26 8', 10
				db '%29 = load f64 %28 0 nativus', 10
				db '%30 = fadd f64 %25 %29', 10
				db '%31 = iconst u64 2', 10
				db '%32 = iconst u64 8', 10
				db 'chk %31 %32', 10
				db '%33 = index %12 %31 8', 10
				db '%34 = load f64 %33 0 nativus', 10
				db '%35 = fadd f64 %30 %34', 10
				db '%36 = iconst u64 3', 10
				db '%37 = iconst u64 8', 10
				db 'chk %36 %37', 10
				db '%38 = index %17 %36 8', 10
				db '%39 = load f64 %38 0 nativus', 10
				db '%40 = fadd f64 %35 %39', 10
				db 'ret %40', 10
				db '}', 10
				db 'functio @quattuor (ptr ptr) -> f32 numeri ad_parem vetita explicita conservata {', 10
				db 'b0:', 10
				db '%0 = param ptr 0', 10
				db '%1 = param ptr 1', 10
				db '%2 = slot 16 4', 10
				db '%3 = load vf32.4 %0 0 nativus', 10
				db '%4 = load vf32.4 %1 0 nativus', 10
				db '%5 = vadd vf32.4 %3 %4', 10
				db '%6 = slot 16 4', 10
				db 'store vf32.4 %6 0 nativus %5', 10
				db 'copy 16 %2 %6', 10
				db '%7 = iconst u64 3', 10
				db '%8 = iconst u64 4', 10
				db 'chk %7 %8', 10
				db '%9 = index %2 %7 4', 10
				db '%10 = load f32 %9 0 nativus', 10
				db 'ret %10', 10
				db '}', 10
				db 'functio @par (ptr ptr) -> f64 numeri ad_parem vetita explicita conservata {', 10
				db 'b0:', 10
				db '%0 = param ptr 0', 10
				db '%1 = param ptr 1', 10
				db '%2 = slot 16 8', 10
				db '%3 = load vf64.2 %0 0 nativus', 10
				db '%4 = load vf64.2 %1 0 nativus', 10
				db '%5 = vdiv vf64.2 %3 %4', 10
				db '%6 = slot 16 8', 10
				db 'store vf64.2 %6 0 nativus %5', 10
				db 'copy 16 %2 %6', 10
				db '%7 = iconst u64 0', 10
				db '%8 = iconst u64 2', 10
				db 'chk %7 %8', 10
				db '%9 = index %2 %7 8', 10
				db '%10 = load f64 %9 0 nativus', 10
				db 'ret %10', 10
				db '}', 10
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
  fx_chk:	rb	sizeof.ChkCtx
  fx_mod:	rb	sizeof.BfaModule
