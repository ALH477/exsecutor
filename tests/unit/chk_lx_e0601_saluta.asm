; tests/unit/chk_lx_e0601_saluta.asm
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
; checker-lexicon fixture for pass 5 -- THE LOAD-BEARING NEGATIVE.
; `docs/design/checker.md` finding 16 and spec §3.3, as amended: "`saluta`,
; `construe`, `imprime`, `textus` and `grapha` are all correct Latin and none
; decomposes over them [the fourteen §3.3 roots]." This fixture is the
; measurement that backs the sentence, not an illustration of it: `saluta`
; (§4.2's own example), `imprime_gutenbergio` (§5.1, §10.1's qualifier-
; carrying name) and `initium` (spec §4.1 rule 2's entry-point name, §4.6)
; are asserted to ALL fail `EXS-E0601` under `checker/lexicon/morphemes.inc`
; -- the stand-in table `tools/gen-lexicon.py` generates from §3.3-§3.5 as
; they stand today.
;
; THIS IS WHY THE PASS IS NOT ENABLED. `lexicon.inc`'s own header says so;
; this fixture is where that claim is checked rather than asserted in prose.
; If a future §3.3 amendment adds `salut-`, `imprim-` or a root `initium`
; decomposes over, this fixture's corresponding check starts failing --
; which is the point: it is the tripwire for "the real table changed enough
; to enable the pass," not a fact this project wants to keep true forever.
;
; `saluta` and `imprime_gutenbergio` fail because no root in the table is a
; prefix-or-suffix-stripped match for `salut`/`imprim` at all (§3.3 lacks
; both). `initium` fails differently and is worth separating out: `in-` IS
; one of §3.5's eight prefixes, and it DOES match the start of `initium` --
; but the remainder, `itium`, matches no (root, suffix) pair in the table
; either (no root's present, supine, or -- for `-e` -- imperative form is
; `itium` or anything a suffix could turn into it). So `initium` is not
; merely "no prefix matches"; it is "a prefix matches and the decomposition
; still fails," which is the harder of the two ways `__chk_lx_check_decl`
; reaches `EXS-E0601` (see lexicon.inc's `.try1`/`.e0601` path) and worth
; distinguishing from `saluta`'s simpler "no prefix, no bare match" case.
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

	; ==== 1: saluta -- EXS-E0601 (no root in the table matches at all) =====
	lea	rdi, [fx_names]
	lea	rsi, [fx_saluta]
	mov	rdx, FX_SALUTA_LEN
	call	intern_id		; id 2
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_t1]
	mov	rdx, FX_T1_LEN
	call	ast_load
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	call	fx_run
	cmp	rax, 1
	jne	.fail1
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	cmp	dword [rax + Diag.code_num], 601
	jne	.fail1

	; ==== 2: imprime_gutenbergio -- EXS-E0601 (base `imprime`, no root) ====
	lea	rdi, [fx_chk]
	call	arena_reset
	lea	rdi, [fx_names]
	lea	rsi, [fx_imprime]
	mov	rdx, FX_IMPRIME_LEN
	call	intern_id		; id 3
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_t2]
	mov	rdx, FX_T2_LEN
	call	ast_load
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	call	fx_run
	cmp	rax, 1
	jne	.fail2
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	cmp	dword [rax + Diag.code_num], 601
	jne	.fail2

	; ==== 3: initium -- EXS-E0601 (`in-` matches, `itium` still does not) =
	lea	rdi, [fx_chk]
	call	arena_reset
	lea	rdi, [fx_names]
	lea	rsi, [fx_initium]
	mov	rdx, FX_INITIUM_LEN
	call	intern_id		; id 4
	lea	rdi, [fx_names]
	lea	rsi, [fx_m]
	mov	rdx, FX_M_LEN
	call	intern_id		; id 5
	lea	rdi, [fx_names]
	lea	rsi, [fx_mundus]
	mov	rdx, FX_MUNDUS_LEN
	call	intern_id		; id 6
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_t3]
	mov	rdx, FX_T3_LEN
	call	ast_load
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	call	fx_run
	cmp	rax, 1
	jne	.fail3
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	cmp	dword [rax + Diag.code_num], 601
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

; run `chk_lexicon` + `chk_flush` over `fx_tree` -> rax = diagnostics flushed.
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
	push	rax
	lea	rdi, [fx_chkctx]
	call	chk_flush
	pop	rax
	pop	rbp
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/lexicon/lexicon.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path	db 'chk_lx_e0601_saluta.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_saluta	db 'saluta'
  FX_SALUTA_LEN = $ - fx_saluta
  fx_imprime	db 'imprime_gutenbergio'
  FX_IMPRIME_LEN = $ - fx_imprime
  fx_initium	db 'initium'
  FX_INITIUM_LEN = $ - fx_initium
  fx_m		db 'm'
  FX_M_LEN = $ - fx_m
  fx_mundus	db 'Mundus'
  FX_MUNDUS_LEN = $ - fx_mundus

  fx_srcstub	db 'publica functio placeholder_source_text_0000000000000000000000', 10
  FX_SRCSTUB_LEN = $ - fx_srcstub

  ; VERBATIM `--emitte ast` output for `publica functio saluta() {}`
  fx_t1:
	db 'astv 1 4 12 1 1 4 0 0 0 0', 10
	db 'x 1 3', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 3 0 0 1 8 19', 10
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
	db 'n 1 Sig 0 0 0 0 0 0 1 8 16', 10
	db 'n 2 Block 0 0 0 0 0 0 1 25 2', 10
	db 'n 3 Fn 1 0 1 2 0 1 1 8 19', 10
	db 'n 4 Module 0 0 1 1 0 0 1 0 28', 10
  FX_T1_LEN = $ - fx_t1

  ; VERBATIM `--emitte ast` output for `publica functio imprime_gutenbergio() {}`,
  ; NAME field hand-adjusted from 2 to 3 (id 2 is `saluta` from check 1).
  fx_t2:
	db 'astv 1 4 12 1 1 4 0 0 0 0', 10
	db 'x 1 3', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 3 3 0 0 1 8 32', 10
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
	db 'n 1 Sig 0 0 0 0 0 0 1 8 29', 10
	db 'n 2 Block 0 0 0 0 0 0 1 38 2', 10
	db 'n 3 Fn 1 0 1 2 0 1 1 8 32', 10
	db 'n 4 Module 0 0 1 1 0 0 1 0 41', 10
  FX_T2_LEN = $ - fx_t2

  ; VERBATIM `--emitte ast` output for
  ; `publica functio initium(m: Mundus) -> u8 {}`, NAME fields hand-adjusted
  ; (`initium` 2->4, `m` 3->5, `Mundus` 4->6: ids 2, 3 already spent above).
  fx_t3:
	db 'astv 1 9 13 3 1 9 0 0 0 0', 10
	db 'x 1 1', 10
	db 'x 2 4', 10
	db 'x 3 8', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 4 8 0 0 1 8 35', 10
	db 'd 2 Param 0 0 5 4 0 1 1 24 9', 10
	db 'd 3 CapAtom 0 0 4 0 0 0 0 0 0', 10
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
	db 'n 1 Seg 0 0 6 0 0 3 1 27 6', 10
	db 'n 2 Path 0 0 1 1 0 3 1 27 6', 10
	db 'n 3 TyPath 0 0 2 0 0 0 1 27 6', 10
	db 'n 4 Param 0 0 3 3 0 2 1 24 9', 10
	db 'n 5 TyBit 8 0 0 0 0 0 1 38 2', 10
	db 'n 6 Sig 0 0 2 1 5 0 1 8 32', 10
	db 'n 7 Block 0 0 0 0 0 0 1 41 2', 10
	db 'n 8 Fn 1 0 6 7 0 1 1 8 35', 10
	db 'n 9 Module 0 0 3 1 0 0 1 0 44', 10
  FX_T3_LEN = $ - fx_t3

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_chk:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chkctx:	rb sizeof.ChkCtx
