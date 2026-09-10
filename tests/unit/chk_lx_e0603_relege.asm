; tests/unit/chk_lx_e0603_relege.asm
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
; checker-lexicon fixture for pass 5: `EXS-E0603`, spec §3.5's `re-` law
; ("same signature as the bare form"), checked structurally by
; `__chk_lx_sig_eq` -- finding 18's base lookup (`__chk_lx_find_base`) needs
; `lege` to be a PUBLIC declaration of the same module, so both trees below
; declare it.
;
; THE PAIR IS ONE EDIT APART, AND THE EDIT IS A SECOND PARAMETER:
;
;	publica functio lege(x: u32) -> u32 {}
;	publica functio relege(x: u32) -> u32 {}          accepted, same sig
;
;	publica functio lege(x: u32) -> u32 {}
;	publica functio relege(x: u32, y: u32) -> u32 {}  rejected, EXS-E0603
;
; Both trees below are the VERBATIM output of
; `build/exsc aedifica --hospes x86_64-linux --emitte ast FILE`
; on those two-line sources -- not hand-written.
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
	lea	rdi, [fx_names]
	lea	rsi, [fx_lege]
	mov	rdx, FX_LEGE_LEN
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
	lea	rsi, [fx_relege]
	mov	rdx, FX_RELEGE_LEN
	call	intern_id		; id 5
	lea	rdi, [fx_names]
	lea	rsi, [fx_y]
	mov	rdx, FX_Y_LEN
	call	intern_id		; id 6 -- differ variant only

	; ---- 1: same signature -- accepted, no diagnostic ----
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_same]
	mov	rdx, FX_SAME_LEN
	call	ast_load
	lea	rdi, [fx_tree]
	call	ast_verify_stage1

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
	lea	rsi, [fx_srcsame]
	mov	rdx, FX_SRCSAME_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chkctx]
	call	chk_lexicon
	test	rax, rax
	jnz	.fail1
	lea	rdi, [fx_chkctx]
	call	chk_flush
	test	rax, rax
	jnz	.fail1
	cmp	qword [fx_diags + Vec.len], 0
	jne	.fail1

	; ---- 2: a second parameter -- rejected, exactly EXS-E0603 ----
	lea	rdi, [fx_chk]
	call	arena_reset
	lea	rdi, [fx_tree2]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree2]
	lea	rsi, [fx_diff]
	mov	rdx, FX_DIFF_LEN
	call	ast_load
	lea	rdi, [fx_tree2]
	call	ast_verify_stage1

	lea	rdi, [fx_diags]
	lea	rsi, [fx_scratch]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_chkctx]
	lea	rsi, [fx_tree2]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_chk]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chkctx]
	lea	rsi, [fx_srcdiff]
	mov	rdx, FX_SRCDIFF_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chkctx]
	call	chk_lexicon
	cmp	rax, 1
	jne	.fail2
	lea	rdi, [fx_chkctx]
	call	chk_flush
	cmp	rax, 1
	jne	.fail2
	cmp	qword [fx_diags + Vec.len], 1
	jne	.fail2

	; ---- 3: it is EXACTLY EXS-E0603 ----
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	cmp	dword [rax + Diag.code_num], 603
	jne	.fail3

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

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/lexicon/lexicon.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path	db 'chk_lx_e0603_relege.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_lege	db 'lege'
  FX_LEGE_LEN = $ - fx_lege
  fx_x		db 'x'
  FX_X_LEN = $ - fx_x
  fx_u32	db 'u32'
  FX_U32_LEN = $ - fx_u32
  fx_relege	db 'relege'
  FX_RELEGE_LEN = $ - fx_relege
  fx_y		db 'y'
  FX_Y_LEN = $ - fx_y

  fx_srcsame	db 'publica functio lege(x: u32) -> u32 {}', 10
  		db 'publica functio relege(x: u32) -> u32 {}', 10
  FX_SRCSAME_LEN = $ - fx_srcsame

  fx_srcdiff	db 'publica functio lege(x: u32) -> u32 {}', 10
  		db 'publica functio relege(x: u32, y: u32) -> u32 {}', 10
  FX_SRCDIFF_LEN = $ - fx_srcdiff

  ; VERBATIM `--emitte ast` output for fx_srcsame
  fx_same:
	db 'astv 1 13 15 4 1 13 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 8', 10
	db 'x 3 6', 10
	db 'x 4 12', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 6 0 0 1 8 30', 10
	db 'd 2 Param 0 0 3 2 0 1 1 21 6', 10
	db 'd 3 Fn 1 0 5 12 0 0 1 47 32', 10
	db 'd 4 Param 0 0 3 8 0 3 1 62 6', 10
	db 'd 5 CapAtom 0 0 16 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 17 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 18 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 19 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 20 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 21 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 22 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 23 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 14 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'd 15 CapAtom 0 0 26 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 24 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 21 6', 10
	db 'n 3 TyBit 32 0 0 0 0 0 1 32 3', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 8 27', 10
	db 'n 5 Block 0 0 0 0 0 0 1 36 2', 10
	db 'n 6 Fn 1 0 4 5 0 1 1 8 30', 10
	db 'n 7 TyBit 32 0 0 0 0 0 1 65 3', 10
	db 'n 8 Param 0 0 3 7 0 4 1 62 6', 10
	db 'n 9 TyBit 32 0 0 0 0 0 1 73 3', 10
	db 'n 10 Sig 0 0 2 1 9 0 1 47 29', 10
	db 'n 11 Block 0 0 0 0 0 0 1 77 2', 10
	db 'n 12 Fn 1 0 10 11 0 3 1 47 32', 10
	db 'n 13 Module 0 0 3 2 0 0 1 0 80', 10
  FX_SAME_LEN = $ - fx_same

  ; VERBATIM `--emitte ast` output for fx_srcdiff
  fx_diff:
	db 'astv 1 15 16 5 1 15 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 8', 10
	db 'x 3 10', 10
	db 'x 4 6', 10
	db 'x 5 14', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 6 0 0 1 8 30', 10
	db 'd 2 Param 0 0 3 2 0 1 1 21 6', 10
	db 'd 3 Fn 1 0 5 14 0 0 1 47 40', 10
	db 'd 4 Param 0 0 3 8 0 3 1 62 6', 10
	db 'd 5 Param 0 0 6 10 0 3 1 70 6', 10
	db 'd 6 CapAtom 0 0 18 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 19 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 20 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 21 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 22 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 23 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'd 14 CapAtom 0 0 26 0 0 0 0 0 0', 10
	db 'd 15 CapAtom 0 0 27 0 0 0 0 0 0', 10
	db 'd 16 CapAtom 0 0 28 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 24 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 21 6', 10
	db 'n 3 TyBit 32 0 0 0 0 0 1 32 3', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 8 27', 10
	db 'n 5 Block 0 0 0 0 0 0 1 36 2', 10
	db 'n 6 Fn 1 0 4 5 0 1 1 8 30', 10
	db 'n 7 TyBit 32 0 0 0 0 0 1 65 3', 10
	db 'n 8 Param 0 0 3 7 0 4 1 62 6', 10
	db 'n 9 TyBit 32 0 0 0 0 0 1 73 3', 10
	db 'n 10 Param 0 0 6 9 0 5 1 70 6', 10
	db 'n 11 TyBit 32 0 0 0 0 0 1 81 3', 10
	db 'n 12 Sig 0 0 2 2 11 0 1 47 37', 10
	db 'n 13 Block 0 0 0 0 0 0 1 85 2', 10
	db 'n 14 Fn 1 0 12 13 0 3 1 47 40', 10
	db 'n 15 Module 0 0 4 2 0 0 1 0 88', 10
  FX_DIFF_LEN = $ - fx_diff

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_chk:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_tree2:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chkctx:	rb sizeof.ChkCtx
