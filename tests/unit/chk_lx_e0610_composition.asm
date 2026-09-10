; tests/unit/chk_lx_e0610_composition.asm
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
; checker-lexicon fixture for pass 5: `EXS-E0610` (spec §3.8's two-affix
; ceiling) and, along the way, `con-`'s law (arity >= 2, checkable without
; types per checker.md section 2.2) -- `EXS-E0603` again, non-vacuously,
; through a SEPARATE mutation of the SAME name rather than a different one.
;
; FOUR TREES, THE COMPOSITION DEPTH LADDER:
;
;   1. `conlege(x, y)`      con- + leg- + -e,      2 affixes -- accepted
;   2. `conlege(x)`         same decomposition, arity 1 -- EXS-E0603 (con-'s
;      own law), proving check 1's accept is not vacuous: one argument fewer
;      and the SAME name is rejected, for a DIFFERENT reason than E0610.
;   3. `conlegibilis`       con- + leg- + -ibilis, 2 affixes, declared
;      `interfacies` (matching -ibilis's class) -- accepted. con-'s law does
;      not apply off a functio declaration (spec §3.5), so no E0603 either.
;   4. `reconlege`          re- + con- + leg- + -e, THREE affixes -- exactly
;      one EXS-E0610, and nothing else (checker.md's "one source fact, one
;      code": an over-composed name is not also checked for a suffix or a
;      prefix-law disagreement).
;
; Every tree is the VERBATIM output of
; `build/exsc aedifica --hospes x86_64-linux --emitte ast FILE` on its
; one-line source -- not hand-written.
;
; NOT ENABLED -- see lexicon.inc's header; `chk_lexicon` is called directly.
;
; Exit 0 = every check passed; 10+N = check N failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_scratch]
	mov	rsi, 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_chk]
	mov	rsi, 64 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 64
	call	intern_init
	lea	rdi, [fx_names]
	lea	rsi, [fx_path]
	mov	rdx, FX_PATH_LEN
	call	intern_id		; id 1

	; ==== 1: conlege(x, y) -- accepted ====================================
	lea	rdi, [fx_names]
	lea	rsi, [fx_conlege]
	mov	rdx, FX_CONLEGE_LEN
	call	intern_id		; id 2
	lea	rdi, [fx_names]
	lea	rsi, [fx_x]
	mov	rdx, FX_X_LEN
	call	intern_id		; id 3
	lea	rdi, [fx_names]
	lea	rsi, [fx_u32]
	mov	rdx, FX_U32_LEN
	call	intern_id		; id 4
	lea	rdi, [fx_names]
	lea	rsi, [fx_y]
	mov	rdx, FX_Y_LEN
	call	intern_id		; id 5

	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_ok2]
	mov	rdx, FX_OK2_LEN
	call	ast_load
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	call	fx_run
	test	rax, rax
	jnz	.fail1

	; ==== 2: conlege(x) -- EXS-E0603 (con-'s own arity law) ================
	lea	rdi, [fx_chk]
	call	arena_reset
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_ok1]
	mov	rdx, FX_OK1_LEN
	call	ast_load
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	call	fx_run
	cmp	rax, 1
	jne	.fail2
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	cmp	dword [rax + Diag.code_num], 603
	jne	.fail2

	; ==== 3: conlegibilis -- accepted (interfacies; con-'s law does not
	; apply off a functio declaration) =====================================
	lea	rdi, [fx_chk]
	call	arena_reset
	lea	rdi, [fx_names]
	lea	rsi, [fx_conlegibilis]
	mov	rdx, FX_CONLEGIBILIS_LEN
	call	intern_id		; id 6
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_ibilis]
	mov	rdx, FX_IBILIS_LEN
	call	ast_load
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	call	fx_run
	test	rax, rax
	jnz	.fail3

	; ==== 4: reconlege -- exactly one EXS-E0610 ============================
	lea	rdi, [fx_chk]
	call	arena_reset
	lea	rdi, [fx_names]
	lea	rsi, [fx_reconlege]
	mov	rdx, FX_RECONLEGE_LEN
	call	intern_id		; id 7
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_three]
	mov	rdx, FX_THREE_LEN
	call	ast_load
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	call	fx_run
	cmp	rax, 1
	jne	.fail4
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	cmp	dword [rax + Diag.code_num], 610
	jne	.fail4

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

; run `chk_lexicon` + `chk_flush` over `fx_tree` -> rax = diagnostics flushed
; into `fx_diags` (freshly reinitialized). The source text does not matter
; to any check this fixture makes (no diagnostic here carries a rendered
; snippet assertion), so one placeholder span-source suffices for all four.
  fx_run:
	push	rbp
	lea	rdi, [fx_diags]
	lea	rsi, [fx_scratch]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_chkctx]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_chk]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chkctx]
	lea	rsi, [fx_srcstub]
	mov	rdx, FX_SRCSTUB_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chkctx]
	call	chk_lexicon
	test	rax, rax
	jz	.zero
	mov	rdi, 1
	jmp	.done
  .zero:
	xor	rdi, rdi
  .done:
	push	rdi
	lea	rdi, [fx_chkctx]
	call	chk_flush
	pop	rdi
	test	rdi, rdi
	jz	.wantzero
	mov	rax, 1
	pop	rbp
	ret
  .wantzero:
	xor	rax, rax
	pop	rbp
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/lexicon/lexicon.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path	db 'chk_lx_e0610_composition.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_conlege		db 'conlege'
  FX_CONLEGE_LEN = $ - fx_conlege
  fx_x			db 'x'
  FX_X_LEN = $ - fx_x
  fx_u32		db 'u32'
  FX_U32_LEN = $ - fx_u32
  fx_y			db 'y'
  FX_Y_LEN = $ - fx_y
  fx_conlegibilis	db 'conlegibilis'
  FX_CONLEGIBILIS_LEN = $ - fx_conlegibilis
  fx_reconlege		db 'reconlege'
  FX_RECONLEGE_LEN = $ - fx_reconlege

  ; placeholder span source -- long enough for every span these fixtures
  ; raise against; no check here asserts rendered text.
  fx_srcstub	db 'publica functio conlegibilis_placeholder_source_text_00000000000', 10
  FX_SRCSTUB_LEN = $ - fx_srcstub

  ; VERBATIM `--emitte ast` output for `publica functio conlege(x: u32, y: u32) -> u32 {}`
  fx_ok2:
	db 'astv 1 9 14 3 1 9 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 4', 10
	db 'x 3 8', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 8 0 0 1 8 41', 10
	db 'd 2 Param 0 0 3 2 0 1 1 24 6', 10
	db 'd 3 Param 0 0 5 4 0 1 1 32 6', 10
	db 'd 4 CapAtom 0 0 17 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 18 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 19 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 20 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 21 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 22 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 23 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 26 0 0 0 0 0 0', 10
	db 'd 14 CapAtom 0 0 27 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 27 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 24 6', 10
	db 'n 3 TyBit 32 0 0 0 0 0 1 35 3', 10
	db 'n 4 Param 0 0 5 3 0 3 1 32 6', 10
	db 'n 5 TyBit 32 0 0 0 0 0 1 43 3', 10
	db 'n 6 Sig 0 0 1 2 5 0 1 8 38', 10
	db 'n 7 Block 0 0 0 0 0 0 1 47 2', 10
	db 'n 8 Fn 1 0 6 7 0 1 1 8 41', 10
	db 'n 9 Module 0 0 3 1 0 0 1 0 50', 10
  FX_OK2_LEN = $ - fx_ok2

  ; VERBATIM `--emitte ast` output for `publica functio conlege(x: u32) -> u32 {}`
  fx_ok1:
	db 'astv 1 7 13 2 1 7 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 6', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 6 0 0 1 8 33', 10
	db 'd 2 Param 0 0 3 2 0 1 1 24 6', 10
	db 'd 3 CapAtom 0 0 15 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 16 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 17 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 18 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 19 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 20 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 21 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 22 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 23 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 27 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 24 6', 10
	db 'n 3 TyBit 32 0 0 0 0 0 1 35 3', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 8 30', 10
	db 'n 5 Block 0 0 0 0 0 0 1 39 2', 10
	db 'n 6 Fn 1 0 4 5 0 1 1 8 33', 10
	db 'n 7 Module 0 0 2 1 0 0 1 0 42', 10
  FX_OK1_LEN = $ - fx_ok1

  ; VERBATIM `--emitte ast` output for `publica interfacies conlegibilis { }`
  fx_ibilis:
	db 'astv 1 2 12 1 1 2 0 0 0 0', 10
	db 'x 1 1', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Interface 1 0 6 1 0 0 1 8 28', 10
	db 'd 2 CapAtom 0 0 9 0 0 0 0 0 0', 10
	db 'd 3 CapAtom 0 0 10 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 11 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 12 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 13 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 14 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 15 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 16 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 17 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 18 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 19 0 0 0 0 0 0', 10
	db 'n 1 Interface 0 0 0 0 0 1 1 8 28', 10
	db 'n 2 Module 0 0 1 1 0 0 1 0 37', 10
  FX_IBILIS_LEN = $ - fx_ibilis

  ; VERBATIM `--emitte ast` output for `publica functio reconlege() {}`
  fx_three:
	db 'astv 1 4 12 1 1 4 0 0 0 0', 10
	db 'x 1 3', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 7 3 0 0 1 8 22', 10
	db 'd 2 CapAtom 0 0 11 0 0 0 0 0 0', 10
	db 'd 3 CapAtom 0 0 12 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 13 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 14 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 15 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 16 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 17 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 18 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 19 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 20 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 21 0 0 0 0 0 0', 10
	db 'n 1 Sig 0 0 0 0 0 0 1 8 19', 10
	db 'n 2 Block 0 0 0 0 0 0 1 28 2', 10
	db 'n 3 Fn 1 0 1 2 0 1 1 8 22', 10
	db 'n 4 Module 0 0 1 1 0 0 1 0 31', 10
  FX_THREE_LEN = $ - fx_three

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_chk:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chkctx:	rb sizeof.ChkCtx
