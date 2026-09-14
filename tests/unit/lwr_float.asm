; tests/unit/lwr_float.asm
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
; lowering fixture: FLOAT LITERALS AND THE `/` OPERATOR (spec §8.4 as amended
; by the float wave; §5.4's float-only quotient), from SOURCE to verified IR
; -- lexed, parsed, walked and checked by the routines `exsc` runs, then
; `lwr_module`, `bfa_verify_func` over every function, and `bfa_print_module`
; compared BYTE FOR BYTE against the text below.
;
; WHAT EACH FUNCTION PINS:
;   constans      `redde 1.5;` is ONE `fconst f64 4609434218613702656` -- the
;                 IEEE-754 bit pattern of 1.5 (0x3FF8000000000000) printed as
;                 an unsigned decimal. THE BITS ARE RAW: the checker's
;                 `__chk_ty_fits` already overwrote the literal's `konst`
;                 with the converted pattern (rt/dec754.inc's one
;                 correctly-rounded step), and `lwr_fconst` moves it without
;                 touching it -- this instruction is the whole evidence that
;                 the pair-to-bits overwrite really happened, since a `konst`
;                 still holding the packed decimal pair (M below bit 50, E
;                 above) would print as a wildly different number.
;   gemina        the f32 width: `0.5` is `0x3F000000` = 1056964608 and
;                 `1.5e2` is `0x43160000` = 1125515264 -- and the fraction
;                 and exponent-only forms both lexed, folded and settled as
;                 FLOAT. `a / b` is `fdiv f32`: §5.4's float-only quotient,
;                 the operator this wave added at multiplicative level.
;   quattuor_opus `+ - *` were already float ops; `/` completes the four and
;                 `-a` is `fneg f64`, NOT `sub 0 a` -- spec §5.4: floats have
;                 no trapping negation, and the lowering's own
;                 `__lwr_unary` picked the instruction by operand kind all
;                 along. The `%`-style wrapped ops are absent because the
;                 checker refuses them (EXS-E0305, pinned from source in
;                 chk_ty_floatops.asm); a float reaching `__lwr_arith_op`'s
;                 wrapped arms is a checker bug and traps on the `rassert`.
;   versura       the scalar casts: `x sicut f32` is `ftrunc f32` (the
;                 WIDTH-NARROWING f64->f32 cast), `lat sicut f64` is
;                 `fext f64`, `i sicut f64` is `itof f64`, and `x sicut u32`
;                 is `ftoi u32` -- the four float convert instructions the
;                 emitters both lower, which is why the checker admits them
;                 and refuses only `fma`, `bitcast` and unordered `fcmp`.
;   series        `acies<f64, 3>` built from a literal: three `fconst`s IN
;                 SOURCE ORDER and three `store f64 %s OFF %v nativus` at
;                 ascending offsets 0, 8, 16 -- a float element lowers
;                 through `__lwr_lit` exactly as an integer element does,
;                 which is what checker/types/types.inc's `__chk_ty_elemadm`
;                 now admits it for.
;
; The IR is what the emitter then compiles. This fixture exists because a
; running program cannot say WHICH instruction the lowering chose -- a
; `ftrunc` that was mis-emitted as nothing (two AST types, one IR type, the
; `.same` shortcut) would still exit 0.
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

  fx_path	db 'lwr_float.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_src:	db 'functio constans() -> f64 {', 10
		db 9, 'redde 1.5;', 10
		db '}', 10
		db 'functio gemina() -> f32 {', 10
		db 9, 'firma a: f32 = 0.5;', 10
		db 9, 'firma b: f32 = 1.5e2;', 10
		db 9, 'redde a / b;', 10
		db '}', 10
		db 'functio quattuor_opus(a: f64, b: f64) -> f64 {', 10
		db 9, 'firma s: f64 = a + b;', 10
		db 9, 'firma d: f64 = a - b;', 10
		db 9, 'firma m: f64 = a * b;', 10
		db 9, 'firma q: f64 = a / b;', 10
		db 9, 'firma n: f64 = -a;', 10
		db 9, 'redde s + d + m + q + n;', 10
		db '}', 10
		db 'functio versura(x: f64, i: u32) -> f64 {', 10
		db 9, 'firma lat: f32 = x sicut f32;', 10
		db 9, 'firma retro: f64 = lat sicut f64;', 10
		db 9, 'firma ex_int: f64 = i sicut f64;', 10
		db 9, 'firma ad_int: u32 = x sicut u32;', 10
		db 9, 'redde retro;', 10
		db '}', 10
		db 'functio series() -> f64 {', 10
		db 9, 'firma v: acies<f64, 3> = [1.5, 2.5e1, 3.5];', 10
		db 9, 'redde v[2];', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src

  ; ---- what `lwr_module` must print --------------------------------------
  ; The decimal operands are IEEE-754 bit patterns: 4609434218613702656 is
  ; 0x3FF8000000000000 (1.5 as f64), 1056964608 is 0x3F000000 (0.5 as f32),
  ; 1125515264 is 0x43160000 (150 as f32), and series' 25.0 and 3.5 are
  ; 0x4039000000000000 and 0x400C000000000000. Verified against Python's
  ; `struct.pack('<d', ...)` / `struct.pack('<f', ...)` and against
  ; rt/dec754.inc's own golden table (tests/unit/dec754_golden.asm).
  fx_exp:	db 'functio @constans () -> f64 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = fconst f64 4609434218613702656', 10
		db 'ret %0', 10
		db '}', 10
		db 'functio @gemina () -> f32 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = fconst f32 1056964608', 10
		db '%1 = fconst f32 1125515264', 10
		db '%2 = fdiv f32 %0 %1', 10
		db 'ret %2', 10
		db '}', 10
		db 'functio @quattuor_opus (f64 f64) -> f64 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param f64 0', 10
		db '%1 = param f64 1', 10
		db '%2 = fadd f64 %0 %1', 10
		db '%3 = fsub f64 %0 %1', 10
		db '%4 = fmul f64 %0 %1', 10
		db '%5 = fdiv f64 %0 %1', 10
		db '%6 = fneg f64 %0', 10
		db '%7 = fadd f64 %2 %3', 10
		db '%8 = fadd f64 %7 %4', 10
		db '%9 = fadd f64 %8 %5', 10
		db '%10 = fadd f64 %9 %6', 10
		db 'ret %10', 10
		db '}', 10
		db 'functio @versura (f64 u32) -> f64 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param f64 0', 10
		db '%1 = param u32 1', 10
		db '%2 = ftrunc f32 %0', 10
		db '%3 = fext f64 %2', 10
		db '%4 = itof f64 %1', 10
		db '%5 = ftoi u32 %0', 10
		db 'ret %3', 10
		db '}', 10
		db 'functio @series () -> f64 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = slot 24 8', 10
		db '%1 = fconst f64 4609434218613702656', 10
		db '%2 = fconst f64 4627730092099895296', 10
		db '%3 = fconst f64 4615063718147915776', 10
		db 'store f64 %0 0 nativus %1', 10
		db 'store f64 %0 8 nativus %2', 10
		db 'store f64 %0 16 nativus %3', 10
		db '%4 = iconst u64 2', 10
		db '%5 = iconst u64 3', 10
		db 'chk %4 %5', 10
		db '%6 = index %0 %4 8', 10
		db '%7 = load f64 %6 0 nativus', 10
		db 'ret %7', 10
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
  fx_chk:	rb sizeof.ChkCtx
  fx_mod:	rb sizeof.BfaModule