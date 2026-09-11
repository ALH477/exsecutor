; tests/unit/chk_e0301_taint.asm
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
; SIBLING of chk_e0301_unresolved.asm -- that fixture proves an ordinary miss
; still raises `EXS-E0301`; this one proves the RECOVERY-ARTIFACT SUPPRESSION
; `checker/resolve/resolve.inc`'s header calls "E0301 AND RECOVERY ARTIFACTS":
; pass 1 withholds `E0301` for a name inside a subtree tainted by a parser
; recovery artifact, but a genuinely identical miss OUTSIDE one still raises
; it. Two taint sources, two mutation-proved pairs:
;
;   T1/T2 -- a nested, NAMED `functio` (spec §8.6's peek, `EXS-E0201`, no
;   `Error` node at all -- `cst/parse.inc`'s `.functio` "parses it anyway").
;   Trees are VERBATIM `--emitte ast` output of
;   `tests/diagnostics/c20_nested_named_functio.exsc` (measured before pass 2
;   existed to add its own, unrelated diagnostics to that file -- this
;   fixture calls `chk_resolve` directly, never `chk_run`, precisely so it
;   stays independent of passes 2-4). T1 is that dump unmodified: `interior`
;   (node 9's `Seg`, called from node 12's `Redde`) never resolves --
;   nothing binds a nested `Fn`'s own name -- but node 8 (the nested `Fn`
;   itself, decl 2, whose OWN declaration's parent is decl 1, ALSO kind `Fn`)
;   taints Block 13 (the exterior body, per `__chk_is_nested_fn`), so pass 1
;   raises nothing. T2 is the SAME bytes with ONE FIELD changed: decl 1's
;   KIND, `Fn` -> `Struct` (ast/verify.inc's `__ast_v_decls` rasserts every
;   `Decl.parent` is an earlier declaration id, and decl 1 is the only one
;   decl 2 has to point at, so the mutation moves what decl 1 IS rather
;   than which decl is named). `interior` still cannot resolve (nothing
;   else changed, and node 14 is still a `Fn` node naming decl 1 exactly as
;   in T1 -- nothing checks that a `Fn` node's own declaration has decl-kind
;   `Fn`), but `__chk_is_nested_fn`'s "parent decl kind == AST_D_FN" is now
;   false, the taint is gone, and `E0301` returns.
;
;   T3/T4 -- an `Error` node as a sibling statement, spec §8.6's other
;   documented shape (`tests/diagnostics/README.md`, "The number moved when
;   the checker joined the pipeline"). Trees are VERBATIM `--emitte ast`
;   output of `tests/diagnostics/c14_english_habits_if_gt.exsc`: `if`
;   (node 5/6/7) and `return` (node 12/13/14) are ordinary identifiers that
;   do not resolve, node 11 is the `Error` node the parser left behind for
;   the abandoned `> 10`, and all four sit in or under Block 19 (the
;   function body), whose subtree contains node 11 -- so T3 (unmodified)
;   raises nothing for either. T4 changes ONLY node 11's kind, `Error` ->
;   `Rumpe` (chosen because typed-ast.md section 2.3's table gives `Rumpe`
;   the identical all-`AST_R_NONE` operand row, so the swap changes nothing
;   else about the tree's shape) -- the taint source is gone, and BOTH
;   `E0301`s return.
;
; WHY `chk_resolve`, NOT `chk_run`: `chk_run` (checker/checker.inc) always
; calls `chk_types` next, unconditionally. This fixture's trees have Stage 1
; shape only (`Node.ty` 0 throughout, as `ast_load` leaves it) -- exactly
; what every pass-1-only fixture in this suite already assumes -- so this
; calls `chk_resolve` then `chk_flush` itself, the two steps of `chk_run`
; that are actually this pass's, and stops there.
;
; NON-VACUITY. T1 checks BOTH that the diagnostic count is 0 AND that node
; 9's `d` stays 0 (a genuine miss, not silently forced to resolve -- this
; file's own header: "The name must still be resolved if it can be ...
; only the diagnostic is withheld"). T2/T4 check the code (301) and the
; exact caret span, so a suppression that fired unconditionally (never
; raising 301 at all, taint or not) fails here even though it would pass a
; count-only check on T1/T3 alone.
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

	; ================================================================
	; T1 -- nested named `functio`, tainted: 0 diagnostics, `interior`
	; genuinely unresolved (Seg 9's `d` stays 0)
	; ================================================================
	lea	rdi, [fx_tree1]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree1]
	lea	rsi, [fx_t1]
	mov	rdx, FX_T1_LEN
	call	ast_load
	lea	rdi, [fx_tree1]
	call	ast_verify_stage1
	lea	rdi, [fx_diags]
	lea	rsi, [fx_scratch]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree1]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chk]
	call	chk_resolve
	lea	rdi, [fx_chk]
	call	chk_flush
	test	rax, rax
	jnz	.fail1

	lea	rdi, [fx_tree1]
	mov	rsi, 9			; the `Seg` naming `interior`
	call	ast_node_at
	cmp	dword [rax + AstNode.d], 0
	jne	.fail1
	lea	rdi, [fx_tree1]
	mov	rsi, 10			; the `Path` wrapping it
	call	ast_node_at
	cmp	dword [rax + AstNode.d], 0
	jne	.fail1

	; ================================================================
	; T2 -- same tree, decl 2's `parent` moved off a `Fn` decl: taint
	; gone, `E0301` returns on the SAME name
	; ================================================================
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
	lea	rsi, [fx_t2]
	mov	rdx, FX_T2_LEN
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
	call	chk_resolve
	lea	rdi, [fx_chk]
	call	chk_flush
	cmp	rax, 1
	jne	.fail2

	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 301
	jne	.fail2
	cmp	dword [r12 + Diag.span.start], 743
	jne	.fail2
	cmp	dword [r12 + Diag.span.len], 8
	jne	.fail2

	; ================================================================
	; T3 -- `Error` node as a sibling statement, tainted: 0 diagnostics
	; for either `if` or `return`
	; ================================================================
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
	lea	rsi, [fx_t3]
	mov	rdx, FX_T3_LEN
	call	ast_load
	lea	rdi, [fx_tree3]
	call	ast_verify_stage1
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree3]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chk]
	call	chk_resolve
	lea	rdi, [fx_chk]
	call	chk_flush
	test	rax, rax
	jnz	.fail3

	; ================================================================
	; T4 -- same tree, node 11's kind moved off `Error` (to `Rumpe`,
	; identical operand shape): taint gone, BOTH names raise `E0301`
	; ================================================================
	lea	rdi, [fx_scratch]
	call	arena_reset
	lea	rdi, [fx_diags]
	lea	rsi, [fx_scratch]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_tree4]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree4]
	lea	rsi, [fx_t4]
	mov	rdx, FX_T4_LEN
	call	ast_load
	lea	rdi, [fx_tree4]
	call	ast_verify_stage1
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree4]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chk]
	call	chk_resolve
	lea	rdi, [fx_chk]
	call	chk_flush
	cmp	rax, 2
	jne	.fail4

	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 301
	jne	.fail4
	cmp	dword [r12 + Diag.span.start], 1849	; `if`
	jne	.fail4
	cmp	dword [r12 + Diag.span.len], 2
	jne	.fail4
	lea	rdi, [fx_diags]
	mov	rsi, 1
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 301
	jne	.fail4
	cmp	dword [r12 + Diag.span.start], 1861	; `return`
	jne	.fail4
	cmp	dword [r12 + Diag.span.len], 6
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
  fx_path	db 'chk_e0301_taint.exsc'
  FX_PATH_LEN = $ - fx_path

  ; ---- T1/T2: tests/diagnostics/c20_nested_named_functio.exsc, verbatim
  ; `--emitte ast` (measured before pass 2 existed) -----------------------
  fx_t1:
	db 'astv 1 15 13 5 1 15 0 0 0 0', 10
	db 'x 1 6', 10
	db 'x 2 9', 10
	db 'x 3 8', 10
	db 'x 4 12', 10
	db 'x 5 14', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 14 0 0 1 650 106', 10
	db 'd 2 Fn 0 0 4 8 0 1 1 682 50', 10
	db 'd 3 CapAtom 0 0 32 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 33 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 34 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 35 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 36 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 37 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 38 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 39 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 40 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 41 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 42 0 0 0 0 0 0', 10
	db 'n 1 TyBit 64 0 0 0 0 0 1 672 3', 10
	db 'n 2 Sig 0 0 0 0 1 0 1 650 25', 10
	db 'n 3 TyBit 64 0 0 0 0 0 1 704 3', 10
	db 'n 4 Sig 0 0 0 0 3 0 1 682 25', 10
	db 'n 5 Lit 1 0 29 0 0 0 1 724 1', 10
	db 'n 6 Redde 0 0 5 0 0 0 1 718 8', 10
	db 'n 7 Block 0 0 1 1 0 0 1 708 24', 10
	db 'n 8 Fn 0 0 4 7 0 2 1 682 50', 10
	db 'n 9 Seg 0 0 4 0 0 0 1 743 8', 10
	db 'n 10 Path 0 0 2 1 0 0 1 743 8', 10
	db 'n 11 Call 0 0 10 0 0 0 1 743 10', 10
	db 'n 12 Redde 0 0 11 0 0 0 1 737 17', 10
	db 'n 13 Block 1 0 3 2 2 0 1 676 80', 10
	db 'n 14 Fn 1 0 2 13 0 1 1 650 106', 10
	db 'n 15 Module 0 0 5 1 0 0 1 642 115', 10
  FX_T1_LEN = $ - fx_t1

  ; T2 = T1 with ONE field changed: decl 1's KIND, `Fn` -> `Struct`.
  ; `__ast_v_decls` (ast/verify.inc) rasserts every `Decl.parent` is an
  ; EARLIER declaration id -- decl 2 is only the second declaration, so
  ; decl 1 is the only legal `parent` value there is, and the mutation has
  ; to move what decl 1 IS rather than which decl is named. `Fn` node 14
  ; keeps naming decl 1 exactly as `n 14 Fn ...` did in T1 (nothing checks
  ; that a `Fn` node's own declaration has decl-kind `Fn`); only
  ; `__chk_is_nested_fn`'s "parent decl kind == AST_D_FN" reads decl 1's
  ; *kind*, so this is still the one fact the mechanism turns on.
  fx_t2:
	db 'astv 1 15 13 5 1 15 0 0 0 0', 10
	db 'x 1 6', 10
	db 'x 2 9', 10
	db 'x 3 8', 10
	db 'x 4 12', 10
	db 'x 5 14', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Struct 1 0 2 14 0 0 1 650 106', 10	; <- kind: Fn -> Struct
	db 'd 2 Fn 0 0 4 8 0 1 1 682 50', 10
	db 'd 3 CapAtom 0 0 32 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 33 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 34 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 35 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 36 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 37 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 38 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 39 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 40 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 41 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 42 0 0 0 0 0 0', 10
	db 'n 1 TyBit 64 0 0 0 0 0 1 672 3', 10
	db 'n 2 Sig 0 0 0 0 1 0 1 650 25', 10
	db 'n 3 TyBit 64 0 0 0 0 0 1 704 3', 10
	db 'n 4 Sig 0 0 0 0 3 0 1 682 25', 10
	db 'n 5 Lit 1 0 29 0 0 0 1 724 1', 10
	db 'n 6 Redde 0 0 5 0 0 0 1 718 8', 10
	db 'n 7 Block 0 0 1 1 0 0 1 708 24', 10
	db 'n 8 Fn 0 0 4 7 0 2 1 682 50', 10
	db 'n 9 Seg 0 0 4 0 0 0 1 743 8', 10
	db 'n 10 Path 0 0 2 1 0 0 1 743 8', 10
	db 'n 11 Call 0 0 10 0 0 0 1 743 10', 10
	db 'n 12 Redde 0 0 11 0 0 0 1 737 17', 10
	db 'n 13 Block 1 0 3 2 2 0 1 676 80', 10
	db 'n 14 Fn 1 0 2 13 0 1 1 650 106', 10
	db 'n 15 Module 0 0 5 1 0 0 1 642 115', 10
  FX_T2_LEN = $ - fx_t2

  ; ---- T3/T4: tests/diagnostics/c14_english_habits_if_gt.exsc, verbatim
  ; `--emitte ast` (measured before pass 2 existed) -----------------------
  fx_t3:
	db 'astv 1 21 13 12 1 21 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 5', 10
	db 'x 3 8', 10
	db 'x 4 12', 10
	db 'x 5 15', 10
	db 'x 6 14', 10
	db 'x 7 17', 10
	db 'x 8 7', 10
	db 'x 9 10', 10
	db 'x 10 11', 10
	db 'x 11 18', 10
	db 'x 12 20', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 20 0 0 1 1813 61', 10
	db 'd 2 Param 0 0 3 2 0 1 1 1828 6', 10
	db 'd 3 CapAtom 0 0 50 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 51 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 52 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 53 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 54 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 55 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 56 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 57 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 58 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 59 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 60 0 0 0 0 0 0', 10
	db 'n 1 TyBit 64 0 0 0 0 0 1 1831 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 1828 6', 10
	db 'n 3 TyBit 64 0 0 0 0 0 1 1839 3', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 1813 29', 10
	db 'n 5 Seg 0 0 5 0 0 0 1 1849 2', 10
	db 'n 6 Path 0 0 2 1 0 0 1 1849 2', 10
	db 'n 7 ExprStmt 0 0 6 0 0 0 1 1849 2', 10
	db 'n 8 Seg 0 0 3 0 0 2 1 1852 1', 10
	db 'n 9 Path 0 0 3 1 0 2 1 1852 1', 10
	db 'n 10 ExprStmt 0 0 9 0 0 0 1 1852 1', 10
	db 'n 11 Error 0 0 0 0 0 0 1 1854 4', 10
	db 'n 12 Seg 0 0 6 0 0 0 1 1861 6', 10
	db 'n 13 Path 0 0 4 1 0 0 1 1861 6', 10
	db 'n 14 ExprStmt 0 0 13 0 0 0 1 1861 6', 10
	db 'n 15 Seg 0 0 3 0 0 2 1 1868 1', 10
	db 'n 16 Path 0 0 5 1 0 2 1 1868 1', 10
	db 'n 17 ExprStmt 0 0 16 0 0 0 1 1868 2', 10
	db 'n 18 Block 0 0 6 2 0 0 1 1859 13', 10
	db 'n 19 Block 0 0 8 4 0 0 1 1843 31', 10
	db 'n 20 Fn 1 0 4 19 0 1 1 1813 61', 10
	db 'n 21 Module 0 0 12 1 0 0 1 1805 70', 10
  FX_T3_LEN = $ - fx_t3

  ; T4 = T3 with ONE field changed: node 11's kind, `Error` -> `Rumpe`
  ; (identical all-AST_R_NONE operand row -- typed-ast.md section 2.3).
  fx_t4:
	db 'astv 1 21 13 12 1 21 0 0 0 0', 10
	db 'x 1 2', 10
	db 'x 2 5', 10
	db 'x 3 8', 10
	db 'x 4 12', 10
	db 'x 5 15', 10
	db 'x 6 14', 10
	db 'x 7 17', 10
	db 'x 8 7', 10
	db 'x 9 10', 10
	db 'x 10 11', 10
	db 'x 11 18', 10
	db 'x 12 20', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 20 0 0 1 1813 61', 10
	db 'd 2 Param 0 0 3 2 0 1 1 1828 6', 10
	db 'd 3 CapAtom 0 0 50 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 51 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 52 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 53 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 54 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 55 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 56 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 57 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 58 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 59 0 0 0 0 0 0', 10
	db 'd 13 CapAtom 0 0 60 0 0 0 0 0 0', 10
	db 'n 1 TyBit 64 0 0 0 0 0 1 1831 3', 10
	db 'n 2 Param 0 0 3 1 0 2 1 1828 6', 10
	db 'n 3 TyBit 64 0 0 0 0 0 1 1839 3', 10
	db 'n 4 Sig 0 0 1 1 3 0 1 1813 29', 10
	db 'n 5 Seg 0 0 5 0 0 0 1 1849 2', 10
	db 'n 6 Path 0 0 2 1 0 0 1 1849 2', 10
	db 'n 7 ExprStmt 0 0 6 0 0 0 1 1849 2', 10
	db 'n 8 Seg 0 0 3 0 0 2 1 1852 1', 10
	db 'n 9 Path 0 0 3 1 0 2 1 1852 1', 10
	db 'n 10 ExprStmt 0 0 9 0 0 0 1 1852 1', 10
	db 'n 11 Rumpe 0 0 0 0 0 0 1 1854 4', 10	; <- kind: Error -> Rumpe
	db 'n 12 Seg 0 0 6 0 0 0 1 1861 6', 10
	db 'n 13 Path 0 0 4 1 0 0 1 1861 6', 10
	db 'n 14 ExprStmt 0 0 13 0 0 0 1 1861 6', 10
	db 'n 15 Seg 0 0 3 0 0 2 1 1868 1', 10
	db 'n 16 Path 0 0 5 1 0 2 1 1868 1', 10
	db 'n 17 ExprStmt 0 0 16 0 0 0 1 1868 2', 10
	db 'n 18 Block 0 0 6 2 0 0 1 1859 13', 10
	db 'n 19 Block 0 0 8 4 0 0 1 1843 31', 10
	db 'n 20 Fn 1 0 4 19 0 1 1 1813 61', 10
	db 'n 21 Module 0 0 12 1 0 0 1 1805 70', 10
  FX_T4_LEN = $ - fx_t4

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree1:	rb sizeof.Ast
  fx_tree2:	rb sizeof.Ast
  fx_tree3:	rb sizeof.Ast
  fx_tree4:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chk:	rb sizeof.ChkCtx
