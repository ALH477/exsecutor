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
; checker fixture -- spec §4.1 rule 7, `EXS-E0500`: a module has no mutable
; state. `docs/design/checker.md` section 6's `checker-resolve` row names this
; pair by name.
;
; THE PAIR IS ONE EDIT APART, AND THE EDIT IS THE KEYWORD:
;
;	mutabilis c: i32 = 0;      rejected, exactly EXS-E0500
;	firma     c: i32 = 0;      accepted, no diagnostic
;
; Both trees below are the VERBATIM output of
; `build/exsc aedifica --hospes x86_64-linux --emitte ast FILE` on those two
; sources -- not hand-written -- so what this fixture checks is what the real
; front end produces. Diffing the two dumps shows exactly three differences and
; all three are that one keyword: `AstDecl.flags` 2 -> 0 (AST_F_MUTABILIS),
; `Binding.aux` 1 -> 0 (AST_BIND_MUT), and every span four bytes shorter.
;
; NON-VACUITY. Check 2 asserts the code is 500. Changing that constant to any
; other number fails the fixture, which is what makes "it rejected" a claim
; about `EXS-E0500` and not about the checker having raised something.
;
; `i32` DOES NOT RESOLVE, AND THAT IS DELIBERATE. Spec §5.1's type names are
; not declarations anywhere in this repository -- `u32` is a `BitType` and
; becomes `TyBit`, but `i32` parses as `TyPath -> Path -> Seg 'i32'`. Pass 1
; raises nothing for a type-position miss (compiler/x86_64/checker/resolve/
; resolve.inc's header says why at length), which is exactly why the accepted
; twin below has ZERO diagnostics rather than one about `i32`.
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
	; interner id 1 is the FILE PATH (lexer/source.inc: `Lexer.file_id` is an
	; intern id of the path), which is why every span in the dumps below says
	; file 1. Nothing in this fixture looks a name up by bytes, so no other
	; identifier has to be interned for the ids in the dumps to mean what they
	; meant when `exsc` printed them.
	lea	rdi, [fx_names]
	lea	rsi, [fx_path]
	mov	rdx, FX_PATH_LEN
	call	intern_id

	; ---- 1: the rejected twin loads and is a well-formed Stage 1 tree ----
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
	cmp	qword [fx_diags + Vec.len], 1
	jne	.fail1

	; ---- 2: it is EXACTLY EXS-E0500, on the binding ----
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 500
	jne	.fail2
	cmp	dword [r12 + Diag.span.start], 0
	jne	.fail2
	cmp	dword [r12 + Diag.span.len], 21
	jne	.fail2

	; ---- 3: §8.3 promises this code a fix, and it is `mutabilis` -> `firma` --
	; `diag_fix_required(500)` is 1 (diag/fix.inc), so a diagnostic without one
	; would be this construction site's bug, not the renderer's.
	mov	rdi, 500
	call	diag_fix_required
	cmp	eax, 1
	jne	.fail3
	cmp	dword [r12 + Diag.fix.kind], DIAG_FIX_REPLACE
	jne	.fail3
	cmp	dword [r12 + Diag.fix.edit.start], 0
	jne	.fail3
	cmp	dword [r12 + Diag.fix.edit.len], 9	; len('mutabilis')
	jne	.fail3
	cmp	qword [r12 + Diag.fix.text_len], 5	; len('firma')
	jne	.fail3

	; render it, so that a bad span shows up here rather than in the driver
	mov	rdi, 1
	mov	rsi, r12
	lea	rdx, [fx_buf]
	mov	rcx, 4096
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit

	; ---- 4: the accepted twin, one edit away, is clean ----
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
	cmp	qword [fx_diags + Vec.len], 0
	jne	.fail4

	; ---- 5: the clean tree went through `chk_verify` (chk_run calls it when
	; the count is zero) and the annotation it makes is visible: the binding's
	; type is a `TyPath` whose `Seg` stayed UNRESOLVED, which is what
	; resolve.inc leaves for pass 2 ----
	lea	rdi, [fx_tree2]
	mov	rsi, 1			; n1: the `Seg` of `i32`
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_SEG
	jne	.fail5
	cmp	dword [rax + AstNode.d], 0
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
  fx_path	db 'chk_e0500.exsc'
  FX_PATH_LEN = $ - fx_path

  ; the two sources, byte for byte -- the spans in the dumps index into these
  fx_rsrc	db 'mutabilis c: i32 = 0;', 10
  FX_RSRC_LEN = $ - fx_rsrc
  fx_asrc	db 'firma c: i32 = 0;', 10
  FX_ASRC_LEN = $ - fx_asrc

  ; VERBATIM `--emitte ast` output for fx_rsrc
  fx_rej:
	db 'astv 1 6 12 2 1 6 0 0 0 0', 10
	db 'x 1 1', 10
	db 'x 2 5', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Binding 2 0 2 5 0 0 1 0 21', 10
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
	db 'n 1 Seg 0 0 3 0 0 0 1 13 3', 10
	db 'n 2 Path 0 0 1 1 0 0 1 13 3', 10
	db 'n 3 TyPath 0 0 2 0 0 0 1 13 3', 10
	db 'n 4 Lit 1 0 8 0 0 0 1 19 1', 10
	db 'n 5 Binding 1 0 3 4 0 1 1 0 21', 10
	db 'n 6 Module 0 0 2 1 0 0 1 0 22', 10
  FX_REJ_LEN = $ - fx_rej

  ; VERBATIM `--emitte ast` output for fx_asrc
  fx_acc:
	db 'astv 1 6 12 2 1 6 0 0 0 0', 10
	db 'x 1 1', 10
	db 'x 2 5', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Binding 0 0 2 5 0 0 1 0 17', 10
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
	db 'n 1 Seg 0 0 3 0 0 0 1 9 3', 10
	db 'n 2 Path 0 0 1 1 0 0 1 9 3', 10
	db 'n 3 TyPath 0 0 2 0 0 0 1 9 3', 10
	db 'n 4 Lit 1 0 8 0 0 0 1 15 1', 10
	db 'n 5 Binding 0 0 3 4 0 1 1 0 17', 10
	db 'n 6 Module 0 0 2 1 0 0 1 0 18', 10
  FX_ACC_LEN = $ - fx_acc

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_tree2:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chk:	rb sizeof.ChkCtx
  fx_buf:	rb 4096
