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
; checker fixture -- `EXS-E0424`, a malformed entry point. spec §4.1 rule 2:
; "A second `initium`, one with the wrong signature, or none where a program
; was asked for, is `EXS-E0424`."
;
; THE TWO COUNT CASES ARE CHECKED HERE; THE SIGNATURE CASE IS NOT, and that is
; a division of labour rather than a gap. `initium(m: Mundus) -> u8` is a claim
; about two `Ast.types` ids, which pass 2 interns; checking the shape in pass 1
; would mean a second, structural implementation of type comparison that pass 2
; would then have to be kept in step with. Left to `checker-types`, with the
; code and the span settled here.
;
; THE PAIR IS ONE FUNCTION APART:
;
;	two `publica functio initium(m: Mundus) -> u8 { redde 0; }`   rejected
;	one                                                            accepted
;
; and a third tree -- an EMPTY module, with the same CHK_F_PROGRAM flag -- is
; the other half of the rule: a program was asked for and there is no entry
; point at all. That diagnostic has NO NODE, so its span is the root's file at
; offset 0 with length 0 and it sorts FIRST (checker.md section 4 orders by
; pass, then node index; 0 is before every node).
;
; TWO CODES FOR THE REJECTED TWIN, AND BOTH ARE TRUE. A second `initium` is
; also a second module-scope declaration of one name, which §13 numbers
; `EXS-E0302`; the entry-point rule is `EXS-E0424`. They are two different
; rules about one edit, and §13 numbers them separately, so both are raised --
; unlike the `sub`/`sub` case (tests/unit/chk_e0422_sub_twice.asm), where §4.5
; and §13 give ONE rule one code. Check 2 asserts both, in order.
;
; `initium` IS FOUND BY INTERNER ID, NOT BY BYTES, and it is looked up WITHOUT
; interning: `chk_init` does `map_get` on the tree's interner, so a module that
; never writes the word does not gain it as a side effect of being checked.
; That is why this fixture interns the file path first and `initium` second --
; reproducing exactly the ids the dumps below were printed with.
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
	call	intern_id		; id 1 -- the file path
	cmp	rax, 1
	jne	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_initium]
	mov	rdx, FX_INITIUM_LEN
	call	intern_id		; id 2 -- `initium`, as in the dumps
	cmp	rax, 2
	jne	.fail0

	; ---- 1: two `initium` ----
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
	mov	r8, CHK_F_PROGRAM
	call	chk_init
	lea	rdi, [fx_chk]
	lea	rsi, [fx_rsrc]
	mov	rdx, FX_RSRC_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	cmp	rax, 2
	jne	.fail1

	; ---- 2: EXS-E0302 then EXS-E0424, both on the SECOND `initium` ----
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 302
	jne	.fail2
	lea	rdi, [fx_diags]
	mov	rsi, 1
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 424
	jne	.fail2
	cmp	dword [r12 + Diag.span.start], 67
	jne	.fail2
	cmp	dword [r12 + Diag.span.len], 49
	jne	.fail2
	mov	eax, [r12 + Diag.flags]
	and	eax, DIAG_F_REL
	cmp	eax, DIAG_F_REL
	jne	.fail2
	cmp	dword [r12 + Diag.rel.start], 8	; the FIRST `initium`
	jne	.fail2
	mov	rdi, 1
	mov	rsi, r12
	lea	rdx, [fx_buf]
	mov	rcx, 4096
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit

	; ---- 3: one `initium`, same flag, is clean ----
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
	mov	r8, CHK_F_PROGRAM
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

	; the `Mundus` parameter bound the atom: `Seg.d` on the type path is the
	; `AST_D_CAPATOM` at `ast_cap_base + AST_CAP_MUNDUS - 1`, which is what
	; pass 2 interns `AST_TY_CAP` over.
	lea	rdi, [fx_tree2]
	call	ast_cap_base
	mov	r13, rax
	lea	rdi, [fx_tree2]
	mov	rsi, 1			; the `Seg` of `Mundus`
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	cmp	rcx, r13
	jne	.fail3

	; ---- 4: an EMPTY module, program requested, is EXS-E0424 with no node --
	lea	rdi, [fx_scratch]
	call	arena_reset
	lea	rdi, [fx_diags]
	lea	rsi, [fx_scratch]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_tree3]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree3]
	lea	rsi, [fx_none]
	mov	rdx, FX_NONE_LEN
	call	ast_load
	lea	rdi, [fx_tree3]
	call	ast_verify_stage1
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree3]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	mov	r8, CHK_F_PROGRAM
	call	chk_init
	lea	rdi, [fx_chk]
	call	chk_run
	cmp	rax, 1
	jne	.fail4
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 424
	jne	.fail4
	cmp	dword [r12 + Diag.span.len], 0
	jne	.fail4

	; ---- 5: the SAME empty module, with no program asked for, is a LIBRARY --
	; spec §4.1 rule 2: "A module with no `initium` is a library." The flag is
	; the only difference, which is what makes check 4 a check on the flag and
	; not on the tree.
	lea	rdi, [fx_scratch]
	call	arena_reset
	lea	rdi, [fx_diags]
	lea	rsi, [fx_scratch]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree3]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chk]
	call	chk_run
	test	rax, rax
	jnz	.fail5

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
  fx_path	db 'chk_e0424.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_initium	db 'initium'
  FX_INITIUM_LEN = $ - fx_initium

  fx_rsrc	db 'publica functio initium(m: Mundus) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10, 10
		db 'publica functio initium(m: Mundus) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  FX_RSRC_LEN = $ - fx_rsrc
  fx_asrc	db 'publica functio initium(m: Mundus) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  FX_ASRC_LEN = $ - fx_asrc

  fx_rej:
	db 'astv 1 21 15 8 1 21 0 0 0 0', 10
	db 'x 1 1', 10
	db 'x 2 4', 10
	db 'x 3 8', 10
	db 'x 4 11', 10
	db 'x 5 14', 10
	db 'x 6 18', 10
	db 'x 7 10', 10
	db 'x 8 20', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 10 0 0 1 8 49', 10
	db 'd 2 Param 0 0 3 4 0 1 1 24 9', 10
	db 'd 3 Fn 1 0 2 20 0 0 1 67 49', 10
	db 'd 4 Param 0 0 3 14 0 3 1 83 9', 10
	db 'd 5 CapAtom 0 0 4 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 21 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 22 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 23 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 26 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 27 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 28 0 0 0 0 0 0', 10
	db 'd 14 CapAtom 0 0 29 0 0 0 0 0 0', 10
	db 'd 15 CapAtom 0 0 30 0 0 0 0 0 0', 10
	db 'n 1 Seg 0 0 4 0 0 0 1 27 6', 10
	db 'n 2 Path 0 0 1 1 0 0 1 27 6', 10
	db 'n 3 TyPath 0 0 2 0 0 0 1 27 6', 10
	db 'n 4 Param 0 0 3 3 0 2 1 24 9', 10
	db 'n 5 TyBit 8 0 0 0 0 0 1 38 2', 10
	db 'n 6 Sig 0 0 2 1 5 0 1 8 32', 10
	db 'n 7 Lit 1 0 16 0 0 0 1 53 1', 10
	db 'n 8 Redde 0 0 7 0 0 0 1 47 8', 10
	db 'n 9 Block 0 0 3 1 0 0 1 41 16', 10
	db 'n 10 Fn 1 0 6 9 0 1 1 8 49', 10
	db 'n 11 Seg 0 0 4 0 0 0 1 86 6', 10
	db 'n 12 Path 0 0 4 1 0 0 1 86 6', 10
	db 'n 13 TyPath 0 0 12 0 0 0 1 86 6', 10
	db 'n 14 Param 0 0 3 13 0 4 1 83 9', 10
	db 'n 15 TyBit 8 0 0 0 0 0 1 97 2', 10
	db 'n 16 Sig 0 0 5 1 15 0 1 67 32', 10
	db 'n 17 Lit 1 0 16 0 0 0 1 112 1', 10
	db 'n 18 Redde 0 0 17 0 0 0 1 106 8', 10
	db 'n 19 Block 0 0 6 1 0 0 1 100 16', 10
	db 'n 20 Fn 1 0 16 19 0 3 1 67 49', 10
	db 'n 21 Module 0 0 7 2 0 0 1 0 117', 10
  FX_REJ_LEN = $ - fx_rej

  fx_acc:
	db 'astv 1 11 13 4 1 11 0 0 0 0', 10
	db 'x 1 1', 10
	db 'x 2 4', 10
	db 'x 3 8', 10
	db 'x 4 10', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 10 0 0 1 8 49', 10
	db 'd 2 Param 0 0 3 4 0 1 1 24 9', 10
	db 'd 3 CapAtom 0 0 4 0 0 0 0 0 0', 10
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
	db 'n 1 Seg 0 0 4 0 0 0 1 27 6', 10
	db 'n 2 Path 0 0 1 1 0 0 1 27 6', 10
	db 'n 3 TyPath 0 0 2 0 0 0 1 27 6', 10
	db 'n 4 Param 0 0 3 3 0 2 1 24 9', 10
	db 'n 5 TyBit 8 0 0 0 0 0 1 38 2', 10
	db 'n 6 Sig 0 0 2 1 5 0 1 8 32', 10
	db 'n 7 Lit 1 0 16 0 0 0 1 53 1', 10
	db 'n 8 Redde 0 0 7 0 0 0 1 47 8', 10
	db 'n 9 Block 0 0 3 1 0 0 1 41 16', 10
	db 'n 10 Fn 1 0 6 9 0 1 1 8 49', 10
	db 'n 11 Module 0 0 4 1 0 0 1 0 58', 10
  FX_ACC_LEN = $ - fx_acc

  ; an empty source file, verbatim
  fx_none:
	db 'astv 1 1 11 0 1 1 0 0 0 0', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 CapAtom 0 0 2 0 0 0 0 0 0', 10
	db 'd 2 CapAtom 0 0 3 0 0 0 0 0 0', 10
	db 'd 3 CapAtom 0 0 4 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 5 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 6 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 7 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 8 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 9 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 10 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 11 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 12 0 0 0 0 0 0', 10
	db 'n 1 Module 0 0 0 0 0 0 1 0 0', 10
  FX_NONE_LEN = $ - fx_none

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_tree2:	rb sizeof.Ast
  fx_tree3:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chk:	rb sizeof.ChkCtx
  fx_buf:	rb 4096
