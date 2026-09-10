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
; checker fixture -- `EXS-E0422`, a capability bound twice in one scope. spec
; §4.5 ("shadowing is an error"), read by docs/design/checker.md section 2.1 as
; ONE VALUE PER CAPABILITY TYPE PER SCOPE -- rule 1 of the three that keep spec
; §16's kill criterion from firing. Two providers of one atom would make an
; implicit draw CHOOSE, and choosing is the search that criterion forbids.
;
; THE PAIR IS ONE LINE APART:
;
;	sub rete = a;  sub rete = a;    rejected, exactly E0422
;	sub rete = a;                   accepted, no diagnostic
;
; Both trees are the VERBATIM output of `build/exsc aedifica --hospes
; x86_64-linux --emitte ast FILE` on those two sources.
;
; ONE SOURCE FACT, ONE CODE. The second `sub` also declares the name `rete` a
; second time in the same block, which `__chk_bind` would otherwise report as
; `EXS-E0302`. It does not: two `sub`s for one atom are §4.5's capability rule
; and §13 numbers that `EXS-E0422`, so reporting the same edit again under a
; second code is what spec §8.3's "one class each, never one per message"
; argues against. Check 1 asserting a count of exactly ONE is what holds that.
;
; NON-VACUITY. Check 2 asserts the code is 422 and the caret sits on the SECOND
; `sub` statement; check 3 asserts the fix `diag_fix_required(422)` promises
; and diag/fix.inc names -- "delete the second binding".
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
	call	intern_id

	; ---- 1: the rejected twin raises exactly one diagnostic ----
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

	; ---- 2: exactly EXS-E0422, on the second `sub` statement ----
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 422
	jne	.fail2
	cmp	dword [r12 + Diag.span.start], 56
	jne	.fail2
	cmp	dword [r12 + Diag.span.len], 13
	jne	.fail2

	; ---- 3: the fix §8.3 promises -- delete that second binding ----
	mov	rdi, 422
	call	diag_fix_required
	cmp	eax, 1
	jne	.fail3
	cmp	dword [r12 + Diag.fix.kind], DIAG_FIX_DELETE
	jne	.fail3
	cmp	dword [r12 + Diag.fix.edit.start], 56
	jne	.fail3
	cmp	dword [r12 + Diag.fix.edit.len], 13
	jne	.fail3
	mov	rdi, 1
	mov	rsi, r12
	lea	rdx, [fx_buf]
	mov	rcx, 4096
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit

	; ---- 4: the accepted twin is clean ----
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
	lea	rdi, [fx_chk]
	lea	rsi, [fx_asrc]
	mov	rdx, FX_ASRC_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	test	rax, rax
	jnz	.fail4

	; ---- 5: the annotations pass 1 exists to make ----
	; The `sub`'s `Path` and its `Seg` both name declaration 9 -- the
	; `AST_D_CAPATOM` for `rete`, spec §4.6 ordinal 6, at `ast_cap_base + 5`.
	lea	rdi, [fx_tree2]
	call	ast_cap_base
	cmp	rax, 4
	jne	.fail5
	add	rax, AST_CAP_RETE - 1
	mov	r13, rax
	lea	rdi, [fx_tree2]
	mov	rsi, 5			; the `Seg` of `rete`
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	cmp	rcx, r13
	jne	.fail5
	lea	rdi, [fx_tree2]
	mov	rsi, 6			; the `Path`
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	cmp	rcx, r13
	jne	.fail5
	lea	rdi, [fx_tree2]
	mov	rsi, r13
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_CAPATOM
	jne	.fail5

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
  .fail5:
	mov	rdi, 15
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path	db 'chk_e0422.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_rsrc	db 'publica functio f(a: u32) -> u8 {', 10
		db '    sub rete = a;', 10
		db '    sub rete = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_RSRC_LEN = $ - fx_rsrc
  fx_asrc	db 'publica functio f(a: u32) -> u8 {', 10
		db '    sub rete = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_ASRC_LEN = $ - fx_asrc

  fx_rej:
	db 'astv 1 19 15 9 1 19 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 5', 10
	db 'x 3 7', 10
	db 'x 4 10', 10
	db 'x 5 12', 10
	db 'x 6 9', 10
	db 'x 7 14', 10
	db 'x 8 16', 10
	db 'x 9 18', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 18 0 0 1 8 76', 10
	db 'd 2 Param 0 0 3 2 0 1 1 18 6', 10
	db 'd 3 Sub 0 0 6 9 0 1 1 38 13', 10
	db 'd 4 Sub 0 0 6 14 0 1 1 56 13', 10
	db 'd 5 CapAtom 0 0 23 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 26 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 27 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 6 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 28 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 29 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 30 0 0 0 0 0 0', 10
	db 'd 14 CapAtom 0 0 31 0 0 0 0 0 0', 10
	db 'd 15 CapAtom 0 0 32 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 21 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 18 6', 10
	db 'n 3 TyBit 8 0 0 0 0 0 1 29 2', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 8 23', 10
	db 'n 5 Seg 0 0 6 0 0 0 1 42 4', 10
	db 'n 6 Path 0 0 2 1 0 0 1 42 4', 10
	db 'n 7 Seg 0 0 3 0 0 0 1 49 1', 10
	db 'n 8 Path 0 0 3 1 0 0 1 49 1', 10
	db 'n 9 Sub 0 0 6 8 0 3 1 38 13', 10
	db 'n 10 Seg 0 0 6 0 0 0 1 60 4', 10
	db 'n 11 Path 0 0 4 1 0 0 1 60 4', 10
	db 'n 12 Seg 0 0 3 0 0 0 1 67 1', 10
	db 'n 13 Path 0 0 5 1 0 0 1 67 1', 10
	db 'n 14 Sub 0 0 11 13 0 4 1 56 13', 10
	db 'n 15 Lit 1 0 20 0 0 0 1 80 1', 10
	db 'n 16 Redde 0 0 15 0 0 0 1 74 8', 10
	db 'n 17 Block 2 0 6 3 3 0 1 32 52', 10
	db 'n 18 Fn 1 0 4 17 0 1 1 8 76', 10
	db 'n 19 Module 0 0 9 1 0 0 1 0 85', 10
  FX_REJ_LEN = $ - fx_rej

  fx_acc:
	db 'astv 1 14 14 6 1 14 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 5', 10
	db 'x 3 7', 10
	db 'x 4 9', 10
	db 'x 5 11', 10
	db 'x 6 13', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 13 0 0 1 8 58', 10
	db 'd 2 Param 0 0 3 2 0 1 1 18 6', 10
	db 'd 3 Sub 0 0 6 9 0 1 1 38 13', 10
	db 'd 4 CapAtom 0 0 23 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 26 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 27 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 6 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 28 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 29 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 30 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 31 0 0 0 0 0 0', 10
	db 'd 14 CapAtom 0 0 32 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 21 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 18 6', 10
	db 'n 3 TyBit 8 0 0 0 0 0 1 29 2', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 8 23', 10
	db 'n 5 Seg 0 0 6 0 0 0 1 42 4', 10
	db 'n 6 Path 0 0 2 1 0 0 1 42 4', 10
	db 'n 7 Seg 0 0 3 0 0 0 1 49 1', 10
	db 'n 8 Path 0 0 3 1 0 0 1 49 1', 10
	db 'n 9 Sub 0 0 6 8 0 3 1 38 13', 10
	db 'n 10 Lit 1 0 20 0 0 0 1 62 1', 10
	db 'n 11 Redde 0 0 10 0 0 0 1 56 8', 10
	db 'n 12 Block 1 0 4 2 3 0 1 32 34', 10
	db 'n 13 Fn 1 0 4 12 0 1 1 8 58', 10
	db 'n 14 Module 0 0 6 1 0 0 1 0 67', 10
  FX_ACC_LEN = $ - fx_acc

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_tree2:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chk:	rb sizeof.ChkCtx
  fx_buf:	rb 4096
