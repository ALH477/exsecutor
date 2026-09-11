; tests/unit/chk_e0500_module_mutabilis.asm
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
; checker fixture -- `EXS-E0301`, a name that does not resolve. spec §4.5 and
; §8.6 decision 5 ("there is no import statement"): a name resolves lexically
; or it resolves to nothing.
;
; THE PAIR IS ONE CHARACTER APART:
;
;	publica functio f(g: u32) -> u32 { redde h; }  rejected, exactly E0301
;	publica functio f(g: u32) -> u32 { redde g; }  accepted, no diagnostic
;
; Both trees are the VERBATIM output of `build/exsc aedifica --hospes
; x86_64-linux --emitte ast FILE` on those two sources. Diffing the dumps shows
; exactly ONE differing token -- `n 5 Seg`'s interner id, 5 (`h`) against 3
; (`g`) -- which is what makes the accepted twin a control rather than a
; second, unrelated program.
;
; THE RESULT TYPE IS `u32`, NOT `u8`, and it changed when pass 2 landed:
; `redde g;` with `g: u32` in a `-> u8` function is a real `EXS-E0303`, so the
; accepted twin as first written stopped being a control the moment anything
; checked types. One character in each source, and both dumps regenerated
; from them.
;
; NON-VACUITY. Check 2 asserts the code is 301 and the caret sits on the four
; bytes `h` occupies; changing either constant fails the fixture.
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
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 64
	call	intern_init
	lea	rdi, [fx_names]
	lea	rsi, [fx_path]
	mov	rdx, FX_PATH_LEN
	call	intern_id		; id 1 -- the file path, as the lexer does

	; ---- 1: the rejected twin ----
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_rej]
	mov	rdx, FX_REJ_LEN
	call	ast_load
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	lea	rdi, [fx_diags]
	lea	rsi, [fx_scratch]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	; --hospes's pointer width. Required from the moment `chk_types` ORs
	; CHK_S_TYPES in, because that un-gates pass 4 and `chk_run` rasserts
	; a zero here (spec §9.5: there is no default-to-build-platform).
	lea	rdi, [fx_chk]
	mov	rsi, 64
	call	chk_set_target
	lea	rdi, [fx_chk]
	lea	rsi, [fx_rsrc]
	mov	rdx, FX_RSRC_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	cmp	rax, 1
	jne	.fail1

	; ---- 2: exactly EXS-E0301, on the `Seg` that names `h` ----
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 301
	jne	.fail2
	cmp	dword [r12 + Diag.span.start], 45
	jne	.fail2
	cmp	dword [r12 + Diag.span.len], 1
	jne	.fail2
	mov	rdi, 1
	mov	rsi, r12
	lea	rdx, [fx_buf]
	mov	rcx, 4096
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit

	; ---- 3: the accepted twin is clean, and `g` resolved to the parameter --
	lea	rdi, [fx_scratch]
	call	arena_reset
	lea	rdi, [fx_diags]
	lea	rsi, [fx_scratch]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_tree2]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree2]
	lea	rsi, [fx_acc]
	mov	rdx, FX_ACC_LEN
	call	ast_load
	lea	rdi, [fx_tree2]
	call	ast_verify_stage1
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree2]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	; --hospes's pointer width. Required from the moment `chk_types` ORs
	; CHK_S_TYPES in, because that un-gates pass 4 and `chk_run` rasserts
	; a zero here (spec §9.5: there is no default-to-build-platform).
	lea	rdi, [fx_chk]
	mov	rsi, 64
	call	chk_set_target
	lea	rdi, [fx_chk]
	lea	rsi, [fx_asrc]
	mov	rdx, FX_ASRC_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	test	rax, rax
	jnz	.fail3

	; ---- 4: the annotation the pass exists to make ----
	; `Seg.d` and `Path.d` both name declaration 2, the parameter `g`.
	lea	rdi, [fx_tree2]
	mov	rsi, 5
	call	ast_node_at
	cmp	dword [rax + AstNode.d], 2
	jne	.fail4
	lea	rdi, [fx_tree2]
	mov	rsi, 6
	call	ast_node_at
	cmp	dword [rax + AstNode.d], 2
	jne	.fail4
	lea	rdi, [fx_tree2]
	mov	rsi, 2
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_PARAM
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

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path	db 'chk_e0301.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_rsrc	db 'publica functio f(g: u32) -> u32 {', 10, '    redde h;', 10, '}', 10
  FX_RSRC_LEN = $ - fx_rsrc
  fx_asrc	db 'publica functio f(g: u32) -> u32 {', 10, '    redde g;', 10, '}', 10
  FX_ASRC_LEN = $ - fx_asrc

  fx_rej:
	db 'astv 1 10 13 4 1 10 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 5', 10
	db 'x 3 7', 10
	db 'x 4 9', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 9 0 0 1 8 41', 10
	db 'd 2 Param 0 0 3 2 0 1 1 18 6', 10
	db 'd 3 CapAtom 0 0 19 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 20 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 21 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 22 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 23 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 26 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 27 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 28 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 29 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 21 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 18 6', 10
	db 'n 3 TyBit 32 0 0 0 0 0 1 29 3', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 8 24', 10
	db 'n 5 Seg 0 0 5 0 0 0 1 45 1', 10
	db 'n 6 Path 0 0 2 1 0 0 1 45 1', 10
	db 'n 7 Redde 0 0 6 0 0 0 1 39 8', 10
	db 'n 8 Block 0 0 3 1 0 0 1 33 16', 10
	db 'n 9 Fn 1 0 4 8 0 1 1 8 41', 10
	db 'n 10 Module 0 0 4 1 0 0 1 0 50', 10
  FX_REJ_LEN = $ - fx_rej

  fx_acc:
	db 'astv 1 10 13 4 1 10 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 5', 10
	db 'x 3 7', 10
	db 'x 4 9', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 9 0 0 1 8 41', 10
	db 'd 2 Param 0 0 3 2 0 1 1 18 6', 10
	db 'd 3 CapAtom 0 0 18 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 19 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 20 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 21 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 22 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 23 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 26 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 27 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 28 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 21 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 18 6', 10
	db 'n 3 TyBit 32 0 0 0 0 0 1 29 3', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 8 24', 10
	db 'n 5 Seg 0 0 3 0 0 0 1 45 1', 10
	db 'n 6 Path 0 0 2 1 0 0 1 45 1', 10
	db 'n 7 Redde 0 0 6 0 0 0 1 39 8', 10
	db 'n 8 Block 0 0 3 1 0 0 1 33 16', 10
	db 'n 9 Fn 1 0 4 8 0 1 1 8 41', 10
	db 'n 10 Module 0 0 4 1 0 0 1 0 50', 10
  FX_ACC_LEN = $ - fx_acc

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_tree2:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chk:	rb sizeof.ChkCtx
  fx_buf:	rb 4096
