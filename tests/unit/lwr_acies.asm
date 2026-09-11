; tests/unit/lwr_acies.asm
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
; lowering fixture: spec §8.6's ARRAY LITERALS, both forms, from SOURCE to
; verified IR -- lexed, parsed, walked and checked by the routines `exsc`
; runs, then `lwr_module`, `bfa_verify_func` over every function, and
; `bfa_print_module` compared BYTE FOR BYTE against the text below.
;
; WHAT EACH FUNCTION PINS:
;   tria          `[1, 2, 3]` is three `iconst`s IN SOURCE ORDER and three
;                 `store u8 %s OFF %v nativus` at ascending offsets 0, 1, 2.
;                 Every element is evaluated before any is stored (the
;                 staging vector), which is what `a = [a[1], a[0]];` needs
;                 and what `__lwr_structlit` does for the same reason.
;   quattuor      `[7; 4]` at or below `LWR_ARRAY_UNROLL` is UNROLLED: ONE
;                 `iconst` -- the element is evaluated once however many
;                 copies are made -- and four stores at 0, 2, 4, 6.
;   duodecim      `[0; 12]` past the threshold is a LOOP, not twelve stores:
;                 a counter phi in the header, `cmp.lt`, `br`, `index %s %i
;                 2` and one `store` in the body, `addw` and the back edge.
;                 The one `iconst 0` for the element sits BEFORE the header,
;                 evaluated once.
;   paria         an element of `@transitus` struct type is `addr %s OFF`
;                 plus `copy 2`, exactly as an aggregate FIELD of a struct
;                 literal is; each element's own literal builds in its own
;                 slot first.
;
; The IR is what the emitter then compiles: tests/programs/acies/ runs the
; same constructs end to end. This fixture exists because a running program
; cannot say WHICH shape the lowering chose -- an unrolled `[0; 12]` and a
; looped one both exit 0.
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

	; every function with a body, through the verifier (lwr_saluta.asm
	; explains why a bodiless declaration is skipped; there is none here)
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

  fx_path	db 'lwr_acies.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_src:	db '@transitus', 10
		db 'publica structura Par {', 10
		db 9, 'primum: u8', 10
		db 9, 'secundum: u8', 10
		db '}', 10
		db 'functio tria() -> u8 {', 10
		db 9, 'firma a: acies<u8, 3> = [1, 2, 3];', 10
		db 9, 'redde a[1];', 10
		db '}', 10
		db 'functio quattuor() -> u16 {', 10
		db 9, 'firma b: acies<u16, 4> = [7; 4];', 10
		db 9, 'redde b[3];', 10
		db '}', 10
		db 'functio duodecim() -> u16 {', 10
		db 9, 'mutabilis c: acies<u16, 12> = [0; 12];', 10
		db 9, 'c[11] = 5;', 10
		db 9, 'redde c[11];', 10
		db '}', 10
		db 'functio paria() -> u8 {', 10
		db 9, 'firma p: acies<Par, 2> = [Par { primum: 1, secundum: 2 }, Par { primum: 3, secundum: 4 }];', 10
		db 9, 'redde p[1].secundum;', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src

  ; ---- what `lwr_module` must print --------------------------------------
  fx_exp:	db 'functio @tria () -> u8 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = slot 3 1', 10
		db '%1 = iconst u8 1', 10
		db '%2 = iconst u8 2', 10
		db '%3 = iconst u8 3', 10
		db 'store u8 %0 0 nativus %1', 10
		db 'store u8 %0 1 nativus %2', 10
		db 'store u8 %0 2 nativus %3', 10
		db '%4 = iconst u64 1', 10
		db '%5 = iconst u64 3', 10
		db 'chk %4 %5', 10
		db '%6 = index %0 %4 1', 10
		db '%7 = load u8 %6 0 nativus', 10
		db 'ret %7', 10
		db '}', 10
		db 'functio @quattuor () -> u16 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = slot 8 2', 10
		db '%1 = iconst u16 7', 10
		db 'store u16 %0 0 nativus %1', 10
		db 'store u16 %0 2 nativus %1', 10
		db 'store u16 %0 4 nativus %1', 10
		db 'store u16 %0 6 nativus %1', 10
		db '%2 = iconst u64 3', 10
		db '%3 = iconst u64 4', 10
		db 'chk %2 %3', 10
		db '%4 = index %0 %2 2', 10
		db '%5 = load u16 %4 0 nativus', 10
		db 'ret %5', 10
		db '}', 10
		db 'functio @duodecim () -> u16 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = slot 24 2', 10
		db '%1 = iconst u16 0', 10
		db '%2 = iconst u64 0', 10
		db 'jmp b1', 10
		db 'b1:', 10
		db '%3 = phi u64 b0 %2 b2 %8', 10
		db '%4 = iconst u64 12', 10
		db '%5 = cmp.lt u64 %3 %4', 10
		db 'br %5 b2 b3', 10
		db 'b2:', 10
		db '%6 = index %0 %3 2', 10
		db 'store u16 %6 0 nativus %1', 10
		db '%7 = iconst u64 1', 10
		db '%8 = addw u64 %3 %7', 10
		db 'jmp b1', 10
		db 'b3:', 10
		db '%9 = iconst u16 5', 10
		db '%10 = iconst u64 11', 10
		db '%11 = iconst u64 12', 10
		db 'chk %10 %11', 10
		db '%12 = index %0 %10 2', 10
		db 'store u16 %12 0 nativus %9', 10
		db '%13 = iconst u64 11', 10
		db '%14 = iconst u64 12', 10
		db 'chk %13 %14', 10
		db '%15 = index %0 %13 2', 10
		db '%16 = load u16 %15 0 nativus', 10
		db 'ret %16', 10
		db '}', 10
		db 'functio @paria () -> u8 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = slot 4 1', 10
		db '%1 = slot 2 1', 10
		db '%2 = iconst u8 1', 10
		db '%3 = iconst u8 2', 10
		db 'store u8 %1 0 nativus %2', 10
		db 'store u8 %1 1 nativus %3', 10
		db '%4 = slot 2 1', 10
		db '%5 = iconst u8 3', 10
		db '%6 = iconst u8 4', 10
		db 'store u8 %4 0 nativus %5', 10
		db 'store u8 %4 1 nativus %6', 10
		db '%7 = addr %0 0', 10
		db 'copy 2 %7 %1', 10
		db '%8 = addr %0 2', 10
		db 'copy 2 %8 %4', 10
		db '%9 = iconst u64 1', 10
		db '%10 = iconst u64 2', 10
		db 'chk %9 %10', 10
		db '%11 = index %0 %9 2', 10
		db '%12 = load u8 %11 1 nativus', 10
		db 'ret %12', 10
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
