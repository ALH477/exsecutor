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
; checker fixture -- `EXS-E0302`, two declarations of one name in one scope
; (typed-ast.md section 2.4; docs/design/checker.md section 2.4's class B,
; since numbered in spec §13).
;
; THE PAIR IS ONE CHARACTER APART:
;
;	firma x = a;  firma x = a;    rejected, exactly E0302
;	firma x = a;  firma y = a;    accepted, no diagnostic
;
; Both trees are the VERBATIM output of `build/exsc aedifica --hospes
; x86_64-linux --emitte ast FILE`. The dumps differ in exactly two tokens:
; `d 4 Binding`'s interner id (6 -> 7, `x` -> `y`, which IS the edit) and
; `n 11 Lit`'s (20 -> 21, incidental -- interning `y` shifted the id the
; literal text `0` was given, and nothing in this pass reads a `Lit`'s id).
;
; THE SCOPE THAT IS EXAMINED IS ONE BLOCK'S, AND ONLY ONE. An inner block
; shadowing an outer name is legal (checker.md section 2.2: "nearest-wins is
; unambiguous"), so this check must not walk outward -- and it does not,
; because `__chk_bind` scans only `[frame.first, binds.len)`, the innermost
; frame's own bindings. That is also why the bind stack exists at all rather
; than a scan of `[Block.c, Block.c + Block.aux)`: those ranges NEST, so a
; range scan would see an inner block's declarations from the outer block.
; compiler/x86_64/checker/resolve/resolve.inc's header has the measurement.
;
; NON-VACUITY. Check 2 asserts the code is 302, the caret is on the SECOND
; binding, and the related span is the FIRST. Any of the three constants
; changed fails the fixture.
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

	; ---- 2: exactly EXS-E0302, on the second binding, related to the first --
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 302
	jne	.fail2
	cmp	dword [r12 + Diag.span.start], 55
	jne	.fail2
	cmp	dword [r12 + Diag.span.len], 12
	jne	.fail2
	mov	eax, [r12 + Diag.flags]
	and	eax, DIAG_F_REL
	cmp	eax, DIAG_F_REL
	jne	.fail2
	cmp	dword [r12 + Diag.rel.start], 38
	jne	.fail2
	mov	rdi, 1
	mov	rsi, r12
	lea	rdx, [fx_buf]
	mov	rcx, 4096
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit

	; ---- 3: the accepted twin is clean ----
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
	jnz	.fail3

	; ---- 4: both initializers resolved to the parameter, not to each other --
	lea	rdi, [fx_tree2]
	mov	rsi, 5
	call	ast_node_at
	cmp	dword [rax + AstNode.d], 2
	jne	.fail4
	lea	rdi, [fx_tree2]
	mov	rsi, 8
	call	ast_node_at
	cmp	dword [rax + AstNode.d], 2
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
  fx_path	db 'chk_e0302.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_rsrc	db 'publica functio f(a: u32) -> u8 {', 10
		db '    firma x = a;', 10
		db '    firma x = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_RSRC_LEN = $ - fx_rsrc
  fx_asrc	db 'publica functio f(a: u32) -> u8 {', 10
		db '    firma x = a;', 10
		db '    firma y = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_ASRC_LEN = $ - fx_asrc

  fx_rej:
	db 'astv 1 15 15 7 1 15 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 5', 10
	db 'x 3 8', 10
	db 'x 4 7', 10
	db 'x 5 10', 10
	db 'x 6 12', 10
	db 'x 7 14', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 14 0 0 1 8 74', 10
	db 'd 2 Param 0 0 3 2 0 1 1 18 6', 10
	db 'd 3 Binding 0 0 6 7 0 1 1 38 12', 10
	db 'd 4 Binding 0 0 6 10 0 1 1 55 12', 10
	db 'd 5 CapAtom 0 0 30 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 31 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 32 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 33 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 34 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 35 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 36 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 37 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 38 0 0 0 0 0 0', 10
	db 'd 14 CapAtom 0 0 39 0 0 0 0 0 0', 10
	db 'd 15 CapAtom 0 0 40 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 21 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 18 6', 10
	db 'n 3 TyBit 8 0 0 0 0 0 1 29 2', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 8 23', 10
	db 'n 5 Seg 0 0 3 0 0 0 1 48 1', 10
	db 'n 6 Path 0 0 2 1 0 0 1 48 1', 10
	db 'n 7 Binding 0 0 0 6 0 3 1 38 12', 10
	db 'n 8 Seg 0 0 3 0 0 0 1 65 1', 10
	db 'n 9 Path 0 0 3 1 0 0 1 65 1', 10
	db 'n 10 Binding 0 0 0 9 0 4 1 55 12', 10
	db 'n 11 Lit 1 0 20 0 0 0 1 78 1', 10
	db 'n 12 Redde 0 0 11 0 0 0 1 72 8', 10
	db 'n 13 Block 2 0 4 3 3 0 1 32 50', 10
	db 'n 14 Fn 1 0 4 13 0 1 1 8 74', 10
	db 'n 15 Module 0 0 7 1 0 0 1 0 83', 10
  FX_REJ_LEN = $ - fx_rej

  fx_acc:
	db 'astv 1 15 15 7 1 15 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 5', 10
	db 'x 3 8', 10
	db 'x 4 7', 10
	db 'x 5 10', 10
	db 'x 6 12', 10
	db 'x 7 14', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 14 0 0 1 8 74', 10
	db 'd 2 Param 0 0 3 2 0 1 1 18 6', 10
	db 'd 3 Binding 0 0 6 7 0 1 1 38 12', 10
	db 'd 4 Binding 0 0 7 10 0 1 1 55 12', 10
	db 'd 5 CapAtom 0 0 30 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 31 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 32 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 33 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 34 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 35 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 36 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 37 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 38 0 0 0 0 0 0', 10
	db 'd 14 CapAtom 0 0 39 0 0 0 0 0 0', 10
	db 'd 15 CapAtom 0 0 40 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 21 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 18 6', 10
	db 'n 3 TyBit 8 0 0 0 0 0 1 29 2', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 8 23', 10
	db 'n 5 Seg 0 0 3 0 0 0 1 48 1', 10
	db 'n 6 Path 0 0 2 1 0 0 1 48 1', 10
	db 'n 7 Binding 0 0 0 6 0 3 1 38 12', 10
	db 'n 8 Seg 0 0 3 0 0 0 1 65 1', 10
	db 'n 9 Path 0 0 3 1 0 0 1 65 1', 10
	db 'n 10 Binding 0 0 0 9 0 4 1 55 12', 10
	db 'n 11 Lit 1 0 21 0 0 0 1 78 1', 10
	db 'n 12 Redde 0 0 11 0 0 0 1 72 8', 10
	db 'n 13 Block 2 0 4 3 3 0 1 32 50', 10
	db 'n 14 Fn 1 0 4 13 0 1 1 8 74', 10
	db 'n 15 Module 0 0 7 1 0 0 1 0 83', 10
  FX_ACC_LEN = $ - fx_acc

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_tree2:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chk:	rb sizeof.ChkCtx
  fx_buf:	rb 4096
