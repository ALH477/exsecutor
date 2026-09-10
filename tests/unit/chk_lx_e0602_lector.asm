; tests/unit/chk_lx_e0602_lector.asm
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
; checker-lexicon fixture for pass 5: `EXS-E0602`. Spec §14 entry 14, verbatim:
; "`-or` name declared as `functio` -> `EXS-E0602`". `lector` is §3.7's own
; worked derivation (root `leg-`, supine `lect-`, suffix `-or`, agent,
; declared `structura`).
;
; THE PAIR IS ONE EDIT APART, AND THE EDIT IS THE KEYWORD:
;
;	publica functio lector() {}      rejected, exactly EXS-E0602
;	publica structura lector { }     accepted, no diagnostic
;
; Both trees below are the VERBATIM output of
; `build/exsc aedifica --hospes x86_64-linux --emitte ast FILE`
; on those two one-line sources -- not hand-written.
;
; NOT ENABLED. `chk_lexicon` is called directly, exactly as
; `checker/rows/`'s fixtures call `chk_rows`/`chk_layout` directly --
; `lexicon.inc`'s own header explains why (spec §3.3, as amended: the table
; is illustrative and this pass is built, tested, and not wired into
; `chk_run`).
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
	call	intern_id		; id 1
	lea	rdi, [fx_names]
	lea	rsi, [fx_lector]
	mov	rdx, FX_LECTOR_LEN
	call	intern_id		; id 2

	; ---- 1: `lector` declared `functio` -> exactly one EXS-E0602 ----
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
	lea	rsi, [fx_scratch]
	mov	rdx, 8 * 1024
	call	arena_init		; a SEPARATE checker arena, reset per run
	jc	.fail0
	lea	rdi, [fx_chkctx]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_chk]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chkctx]
	lea	rsi, [fx_rsrc]
	mov	rdx, FX_RSRC_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chkctx]
	call	chk_lexicon
	cmp	rax, 1
	jne	.fail1
	lea	rdi, [fx_chkctx]
	call	chk_flush
	cmp	rax, 1
	jne	.fail1
	cmp	qword [fx_diags + Vec.len], 1
	jne	.fail1

	; ---- 2: it is EXACTLY EXS-E0602, on the declaration ----
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 602
	jne	.fail2

	; ---- 3: the accepted twin, one edit away, is clean -- non-vacuity:
	; this is NOT a checker that rejects every functio-suffixed name ----
	; `fx_arena` is NOT reset here: it backs `fx_names` too, and
	; rt/intern.inc's header is explicit that resetting the arena behind a
	; LIVE interner is never safe. Both trees share the one arena and the
	; one interner, exactly as tests/unit/chk_e0500_module_mutabilis.asm
	; does across its two checks.
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
	call	arena_reset
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
	lea	rsi, [fx_asrc]
	mov	rdx, FX_ASRC_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chkctx]
	call	chk_lexicon
	test	rax, rax
	jnz	.fail3
	lea	rdi, [fx_chkctx]
	call	chk_flush
	test	rax, rax
	jnz	.fail3
	cmp	qword [fx_diags + Vec.len], 0
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
  fx_path	db 'chk_lx_e0602_lector.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_lector	db 'lector'
  FX_LECTOR_LEN = $ - fx_lector

  ; `publica functio lector() {}`, one line, trailing LF
  fx_rsrc	db 'publica functio lector() {}', 10
  FX_RSRC_LEN = $ - fx_rsrc
  ; `publica structura lector { }`, one line, trailing LF
  fx_asrc	db 'publica structura lector { }', 10
  FX_ASRC_LEN = $ - fx_asrc

  ; VERBATIM `--emitte ast` output for fx_rsrc
  fx_rej:
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
  FX_REJ_LEN = $ - fx_rej

  ; VERBATIM `--emitte ast` output for fx_asrc
  fx_acc:
	db 'astv 1 2 12 1 1 2 0 0 0 0', 10
	db 'x 1 1', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'd 1 Struct 1 0 2 1 0 0 1 8 20', 10
	db 'd 2 CapAtom 0 0 9 0 0 0 0 0 0', 10
	db 'd 3 CapAtom 0 0 10 0 0 0 0 0 0', 10
	db 'd 4 CapAtom 0 0 11 0 0 0 0 0 0', 10
	db 'd 5 CapAtom 0 0 12 0 0 0 0 0 0', 10
	db 'd 6 CapAtom 0 0 13 0 0 0 0 0 0', 10
	db 'd 7 CapAtom 0 0 14 0 0 0 0 0 0', 10
	db 'd 8 CapAtom 0 0 15 0 0 0 0 0 0', 10
	db 'd 9 CapAtom 0 0 16 0 0 0 0 0 0', 10
	db 'd 10 CapAtom 0 0 17 0 0 0 0 0 0', 10
	db 'd 11 CapAtom 0 0 18 0 0 0 0 0 0', 10
	db 'd 12 CapAtom 0 0 19 0 0 0 0 0 0', 10
	db 'n 1 Struct 0 0 0 0 0 1 1 8 20', 10
	db 'n 2 Module 0 0 1 1 0 0 1 0 29', 10
  FX_ACC_LEN = $ - fx_acc

  fx_arena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_chk:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_tree2:	rb sizeof.Ast
  fx_diags:	rb sizeof.Vec
  fx_chkctx:	rb sizeof.ChkCtx
