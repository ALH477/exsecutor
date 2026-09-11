; tests/unit/chk_ty_e0520_refero.asm
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
; checker fixture -- `EXS-E0520`, a non-atomic `refero` in an `externus`
; signature. Spec §5.3, §6.4 and §14 entry 13; `docs/design/checker.md` section
; 6's `checker-types` row, second pair. The rule spec §6.4 states: *"`refero<T>`
; -- non-atomic. **Cannot cross `externus`** ... A foreign thread racing a
; non-atomic refcount is a use-after-free, and the type system cannot see the
; escape."*
;
; THE PAIR IS NINE CHARACTERS APART -- one identifier:
;
;	externus("C", abi: sysv_amd64) { functio nova(x: refero<u32>) -> u8 }
;	externus("C", abi: sysv_amd64) { functio nova(x: refero_communis<u32>) -> u8 }
;
; ---------------------------------------------------------------------------
; WHY THIS ONE FIXTURE IS HAND-BUILT WHEN THE OTHER FOUR `chk_ty_*` ARE NOT.
;
; **`refero<T>` DOES NOT PARSE.** `refero` is a RESERVED WORD (spec §8.4's
; "value flow" row, `lexer/keywords.inc`'s `KW_REFERO`), spec §8.6's `CoreType
; ::= BitType | Path [GenericArgs] | ...` admits only an `IDENT` there, and
; `cst/parse.inc`'s type parser deliberately does not consume a reserved word
; in type position. Feeding the first line above to the real front end gets
; `EXS-E0201` at column 21 and never reaches the checker. Spec §8.6 already
; knows: *"`[OPEN]` `refero` (§6.4) is reserved and has no phrase-level form
; here"*. So the rule is real, the code is real, and the surface form for the
; REJECTED half does not exist yet.
;
; The tree is therefore built through `ast/load.inc` -- checker.md section 6's
; own route, "hand-built trees ... make every pass testable without a parser"
; -- from the VERBATIM `--emitte ast` output of a source that differs from the
; one above in one identifier (`referx`, six bytes, so every span matches), with
; ONE token edited: `n 3 Seg`'s interner id, from that placeholder's to the id
; this fixture interns `refero` at. The accepted twin is the verbatim dump of
; the second line with the same one token pointed at `refero_communis`.
;
; THE INTERNER IS SEEDED ON PURPOSE. Pass 2 recognises `refero` and
; `refero_communis` BY SPELLING (checker/types/prim.inc's primitive table): no
; declaration of either exists anywhere in this repository. A hand-written
; dump names an interner id, so the fixture must put the bytes behind that id
; or the spelling is unreadable -- which is exactly the case prim.inc's
; `__chk_ty_bytes` degrades quietly, and it would make this fixture vacuous.
;
; NON-VACUITY. Check 2 asserts the code is exactly 520 and the caret is the
; `TyPath` node's span (checker.md section 2.4's table: "the type node"), bytes
; 53..59, which is where `refero<u32>` starts. Check 4 asserts the twin is
; silent: `refero_communis<T>` is the atomic one and §6.4 says it MAY cross.
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
	mov	rsi, 8 * 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_scratch]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 64
	call	intern_init

	; id 1 = the file path, as the lexer does; then the two spellings the
	; primitive table has to READ, at the ids the dumps below name.
	lea	rdi, [fx_names]
	lea	rsi, [fx_path]
	mov	rdx, FX_PATH_LEN
	call	intern_id
	cmp	rax, 1
	jne	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_refero]
	mov	rdx, FX_REFERO_LEN
	call	intern_id
	cmp	rax, 2
	jne	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_refc]
	mov	rdx, FX_REFC_LEN
	call	intern_id
	cmp	rax, 3
	jne	.fail0

	; ---- 1: the rejected twin raises exactly one diagnostic ----
	lea	rdi, [fx_rej]
	mov	rsi, FX_REJ_LEN
	lea	rdx, [fx_rsrc]
	mov	rcx, FX_RSRC_LEN
	call	fx_run
	cmp	rax, 1
	jne	.fail1

	; ---- 2: exactly EXS-E0520, on the type node ----
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	mov	rdi, 1
	mov	rsi, r12
	lea	rdx, [fx_buf]
	mov	rcx, 8192
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit
	cmp	dword [r12 + Diag.code_num], 520
	jne	.fail2
	cmp	dword [r12 + Diag.span.start], 53
	jne	.fail2
	cmp	dword [r12 + Diag.span.len], 6
	jne	.fail2

	; ---- 3: the type really is `ref`, not something that happened to fail --
	lea	rdi, [fx_tree]
	mov	rsi, 5
	call	ast_node_at
	mov	esi, [rax + AstNode.ty]
	lea	rdi, [fx_tree]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_REF
	jne	.fail3

	; ---- 4: the accepted twin is silent, and its type is `refc` ----
	lea	rdi, [fx_acc]
	mov	rsi, FX_ACC_LEN
	lea	rdx, [fx_asrc]
	mov	rcx, FX_ASRC_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail4
	lea	rdi, [fx_tree]
	mov	rsi, 5
	call	ast_node_at
	mov	esi, [rax + AstNode.ty]
	lea	rdi, [fx_tree]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_REFC
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

; fx_run(dump, dumplen, srcptr, srclen) -> rax = diagnostics `chk_run` appended.
; The tree comes from `ast/load.inc`; the SOURCE is only what a diagnostic's
; snippet points into.
proc fx_run, fxd, fxdn, fxs, fxsn
	locals
	endl
	lea	rdi, [fx_scratch]
	call	arena_reset
	lea	rdi, [fx_diags]
	lea	rsi, [fx_scratch]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	mov	rsi, [fxd]
	mov	rdx, [fxdn]
	call	ast_load
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
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
	mov	rsi, [fxs]
	mov	rdx, [fxsn]
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	return
endp

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path	db 'chk_ty_e0520.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_refero	db 'refero'
  FX_REFERO_LEN = $ - fx_refero
  fx_refc	db 'refero_communis'
  FX_REFC_LEN = $ - fx_refc

  fx_rsrc	db 'externus("C", abi: sysv_amd64) {', 10
		db '    functio nova(x: refero<u32>) -> u8', 10
		db '}', 10
  FX_RSRC_LEN = $ - fx_rsrc
  fx_asrc	db 'externus("C", abi: sysv_amd64) {', 10
		db '    functio nova(x: refero_communis<u32>) -> u8', 10
		db '}', 10
  FX_ASRC_LEN = $ - fx_asrc

  fx_rej:
	db 'astv 1 11 14 5 1 11 0 0 0 0', 10
	db 'x 1 1', 10
	db 'x 2 3', 10
	db 'x 3 6', 10
	db 'x 4 9', 10
	db 'x 5 10', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Externus 16 0 11 10 0 0 1 0 73', 10
	db 'd 2 ExternFn 16 0 4 9 0 1 1 37 34', 10
	db 'd 3 Param 0 0 5 6 0 2 1 50 14', 10
	db 'd 4 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 26 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 27 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 28 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 29 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 30 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 31 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 32 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 33 0 0 0 0 0 0', 10
	db 'd 14 CapAtom 0 0 34 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 60 3', 10
	db 'n 2 GenericArgs 0 0 1 1 0 0 1 59 5', 10
	db 'n 3 Seg 0 0 2 2 0 0 1 53 6', 10
	db 'n 4 Path 0 0 2 1 0 0 1 53 6', 10
	db 'n 5 TyPath 0 0 4 0 0 0 1 53 6', 10
	db 'n 6 Param 0 0 5 5 0 3 1 50 14', 10
	db 'n 7 TyBit 8 0 0 0 0 0 1 69 2', 10
	db 'n 8 Sig 0 0 3 1 7 0 1 37 34', 10
	db 'n 9 Fn 0 0 8 0 0 2 1 37 34', 10
	db 'n 10 Externus sysv_amd64 0 4 1 11 1 1 0 73', 10
	db 'n 11 Module 0 0 5 1 0 0 1 0 74', 10
  FX_REJ_LEN = $ - fx_rej

  fx_acc:
	db 'astv 1 11 14 5 1 11 0 0 0 0', 10
	db 'x 1 1', 10
	db 'x 2 3', 10
	db 'x 3 6', 10
	db 'x 4 9', 10
	db 'x 5 10', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Externus 16 0 11 10 0 0 1 0 82', 10
	db 'd 2 ExternFn 16 0 4 9 0 1 1 37 43', 10
	db 'd 3 Param 0 0 5 6 0 2 1 50 23', 10
	db 'd 4 CapAtom 0 0 24 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 25 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 26 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 27 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 28 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 29 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 30 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 31 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 32 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 33 0 0 0 0 0 0', 10
	db 'd 14 CapAtom 0 0 34 0 0 0 0 0 0', 10
	db 'n 1 TyBit 32 0 0 0 0 0 1 69 3', 10
	db 'n 2 GenericArgs 0 0 1 1 0 0 1 68 5', 10
	db 'n 3 Seg 0 0 3 2 0 0 1 53 15', 10
	db 'n 4 Path 0 0 2 1 0 0 1 53 15', 10
	db 'n 5 TyPath 0 0 4 0 0 0 1 53 15', 10
	db 'n 6 Param 0 0 5 5 0 3 1 50 23', 10
	db 'n 7 TyBit 8 0 0 0 0 0 1 78 2', 10
	db 'n 8 Sig 0 0 3 1 7 0 1 37 43', 10
	db 'n 9 Fn 0 0 8 0 0 2 1 37 43', 10
	db 'n 10 Externus sysv_amd64 0 4 1 11 1 1 0 82', 10
	db 'n 11 Module 0 0 5 1 0 0 1 0 83', 10
  FX_ACC_LEN = $ - fx_acc

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_diags:	rb sizeof.Vec
  fx_tree:	rb sizeof.Ast
  fx_chk:	rb sizeof.ChkCtx
  fx_buf:	rb 8192
