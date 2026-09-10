; tests/unit/chk_lx_e0601_zzz_qualifier.asm
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
; checker-lexicon fixture for pass 5: `EXS-E0601`, spec §3.1's qualifier rule
; ("A public name may carry one qualifier after `_` ... only the part before
; the `_` is decomposed"). `zzz_qualifier` splits to base `zzz` + qualifier
; `qualifier`; `zzz` decomposes over NO root in this table (it isn't even
; close to one), so this is the plain "does not decompose at all" case,
; distinct from the load-bearing negative in
; tests/unit/chk_lx_e0601_saluta.asm (roots the real language needs that
; THIS stand-in table cannot have).
;
; NON-VACUITY, BY MUTATION: `zzz` alone (no qualifier) is asserted to fail
; identically -- the qualifier split is not what rejects this name; the
; base's failure to decompose is. And `leg_qualifier` (`leg` + qualifier,
; base = a real root's present stem with no suffix) is asserted to ALSO
; fail, because a bare root with no suffix at all is not a derived form --
; proving the qualifier-stripping logic runs (it reaches a DIFFERENT base)
; without smuggling in an accidental accept.
;
; Trees are the VERBATIM output of
; `build/exsc aedifica --hospes x86_64-linux --emitte ast FILE` on each
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

	; ==== 1: zzz_qualifier -- EXS-E0601 ====================================
	lea	rdi, [fx_names]
	lea	rsi, [fx_zzzq]
	mov	rdx, FX_ZZZQ_LEN
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

	; ==== 2: zzz alone -- also EXS-E0601 (the qualifier is not why) =======
	lea	rdi, [fx_chk]
	call	arena_reset
	lea	rdi, [fx_names]
	lea	rsi, [fx_zzz]
	mov	rdx, FX_ZZZ_LEN
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

	; ==== 3: leg_qualifier -- also EXS-E0601 (a bare root has no suffix) ===
	lea	rdi, [fx_chk]
	call	arena_reset
	lea	rdi, [fx_names]
	lea	rsi, [fx_legq]
	mov	rdx, FX_LEGQ_LEN
	call	intern_id		; id 4
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
  fx_path	db 'chk_lx_e0601_zzz_qualifier.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_zzzq	db 'zzz_qualifier'
  FX_ZZZQ_LEN = $ - fx_zzzq
  fx_zzz	db 'zzz'
  FX_ZZZ_LEN = $ - fx_zzz
  fx_legq	db 'leg_qualifier'
  FX_LEGQ_LEN = $ - fx_legq

  fx_srcstub	db 'publica functio placeholder_source_text_0000000000000000000000', 10
  FX_SRCSTUB_LEN = $ - fx_srcstub

  ; VERBATIM `--emitte ast` output for `publica functio zzz_qualifier() {}`
  fx_t1:
	db 'astv 1 4 12 1 1 4 0 0 0 0', 10
	db 'x 1 3', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 2 3 0 0 1 8 26', 10
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
	db 'n 1 Sig 0 0 0 0 0 0 1 8 23', 10
	db 'n 2 Block 0 0 0 0 0 0 1 32 2', 10
	db 'n 3 Fn 1 0 1 2 0 1 1 8 26', 10
	db 'n 4 Module 0 0 1 1 0 0 1 0 35', 10
  FX_T1_LEN = $ - fx_t1

  ; VERBATIM `--emitte ast` output for `publica functio zzz() {}`, with the
  ; NAME field hand-adjusted from 2 to 3 to match THIS fixture's cumulative
  ; interning order (id 2 is `zzz_qualifier` from check 1, above).
  fx_t2:
	db 'astv 1 4 12 1 1 4 0 0 0 0', 10
	db 'x 1 3', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 3 3 0 0 1 8 16', 10
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
	db 'n 1 Sig 0 0 0 0 0 0 1 8 13', 10
	db 'n 2 Block 0 0 0 0 0 0 1 22 2', 10
	db 'n 3 Fn 1 0 1 2 0 1 1 8 16', 10
	db 'n 4 Module 0 0 1 1 0 0 1 0 25', 10
  FX_T2_LEN = $ - fx_t2

  ; VERBATIM `--emitte ast` output for `publica functio leg_qualifier() {}`,
  ; NAME field hand-adjusted from 2 to 4 (ids 2, 3 already spent above).
  fx_t3:
	db 'astv 1 4 12 1 1 4 0 0 0 0', 10
	db 'x 1 3', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Fn 1 0 4 3 0 0 1 8 26', 10
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
	db 'n 1 Sig 0 0 0 0 0 0 1 8 23', 10
	db 'n 2 Block 0 0 0 0 0 0 1 32 2', 10
	db 'n 3 Fn 1 0 1 2 0 1 1 8 26', 10
	db 'n 4 Module 0 0 1 1 0 0 1 0 35', 10
  FX_T3_LEN = $ - fx_t3

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_chk:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chkctx:	rb sizeof.ChkCtx
