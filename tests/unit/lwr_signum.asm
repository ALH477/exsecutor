; tests/unit/lwr_signum.asm
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
; lowering fixture: a signed integer type the LOWERING builds is the same IR
; type the IR PARSER builds from the same spelling -- `iN` is (BFA_TK_I,
; sign 1), not (BFA_TK_I, sign 0).
;
; WHY IT NEEDS A FIXTURE OF ITS OWN. `bfa_type_intern` keys on all eight
; bytes of a `BfaType`, the sign byte included, so lower/ty.inc's `.int`
; interning `iN` with sign 0 made a SECOND `i8`: printed identically to the
; parser's (print.inc spells the type from its KIND), compared unequal to it,
; and read as unsigned by everything that asks `BfaType.sign` -- print.inc's
; `iconst`, emit.inc's `__bfa_ty_is_signed`. The M3 emitter work found it and
; made the emitter read the kind instead; the lowering's types now match the
; parser's, and this is what says so. Printed text alone cannot: the spelling
; is the same either way, which is how the bug went unseen.
;
; THE CHECK. Lower the source below, print it and compare byte for byte (so
; the lowering still produces what it did), then ask the module's own type
; map: interning (BFA_TK_I, 1, 8) and (BFA_TK_I, 1, 32) must ADD NOTHING --
; the lowering already made both -- and interning (BFA_TK_I, 0, 8) must add
; one, because nothing should have made that type.
;
;	functio f(a: i8, b: i32) -> i8 {
;		firma c: i32 = b + 1;
;		redde a -% 1;
;	}
;
; Exit 0 = the printed module matches and the three type-map checks hold.
; 10 = setup, 11 = the front end said something, 12 = `lwr_module` refused,
; 13 = the verifier named a rule, 20 = length mismatch, 21 = byte mismatch,
; 30 = the lowering's `i8` is not the parser's, 31 = its `i32` is not,
; 32 = a sign-0 `i8` already existed. Run with any argument to write the
; produced text to stdout instead of comparing.
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

	; ---- the lowering's signed types are the parser's --------------------
	mov	rdi, [fx_mod + BfaModule.types]
	mov	r12, [rdi + Vec.len]
	lea	rdi, [fx_mod]
	mov	esi, BFA_TK_I
	mov	edx, 1
	mov	ecx, 8
	xor	r8, r8
	call	bfa_type_intern
	mov	rdi, [fx_mod + BfaModule.types]
	cmp	[rdi + Vec.len], r12
	jne	.bad30
	lea	rdi, [fx_mod]
	mov	esi, BFA_TK_I
	mov	edx, 1
	mov	ecx, 32
	xor	r8, r8
	call	bfa_type_intern
	mov	rdi, [fx_mod + BfaModule.types]
	cmp	[rdi + Vec.len], r12
	jne	.bad31
	lea	rdi, [fx_mod]
	mov	esi, BFA_TK_I
	xor	edx, edx
	mov	ecx, 8
	xor	r8, r8
	call	bfa_type_intern
	mov	rdi, [fx_mod + BfaModule.types]
	inc	r12
	cmp	[rdi + Vec.len], r12
	jne	.bad32
	xor	edi, edi
	call	sys_exit_group
  .bad30:
	mov	edi, 30
	call	sys_exit_group
  .bad31:
	mov	edi, 31
	call	sys_exit_group
  .bad32:
	mov	edi, 32
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

  fx_path	db 'lwr_signum.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_src:	db 'functio f(a: i8, b: i32) -> i8 {', 10
		db 9, 'firma c: i32 = b + 1;', 10
		db 9, 'redde a -% 1;', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src

  ; ---- what `lwr_module` must print --------------------------------------
  fx_exp:
		db 'functio @f (i8 i32) -> i8 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param i8 0', 10
		db '%1 = param i32 1', 10
		db '%2 = iconst i32 1', 10
		db '%3 = add i32 %1 %2', 10
		db '%4 = iconst i8 1', 10
		db '%5 = subw i8 %0 %4', 10
		db 'ret %5', 10
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
