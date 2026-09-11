; tests/unit/chk_ty_e0303_mismatch.asm
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
; checker fixture -- `EXS-E0303`, a type mismatch. `docs/design/checker.md`
; section 2.4's class C, and section 1.5's rule that makes it decidable:
; *"type compatibility is EQUALITY OF INTERNED TYPE IDS -- rows, byte order and
; placement included"*, because spec §4.5, §5.2, §5.4 and §6.3 admit no
; implicit conversion anywhere.
;
; THE PAIR IS ONE CHARACTER APART:
;
;	publica functio f(a: u32, b: u8) -> u32 { redde b; }   E0303
;	publica functio f(a: u32, b: u8) -> u32 { redde a; }   accepted
;
; `u8` and `u32` are two ids in `Ast.types` because `ast_type_int` puts the
; width in the record (ast/types.inc), so this is one integer comparison and
; not a rule about widening. THAT is the point of the fixture: a language with
; implicit widening would accept the first line.
;
; NON-VACUITY. Check 2 asserts 303 exactly, and check 3 that the twin is
; silent -- a checker that rejected both would pass check 1 and fail check 3.
;
; Exit 0 = every check passed; 10+N = check N failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail0

	; ---- 1: the rejected twin raises exactly one diagnostic ----
	lea	rdi, [fx_rsrc]
	mov	rsi, FX_RSRC_LEN
	call	fx_run
	cmp	rax, 1
	jne	.fail1

	; ---- 2: and it is exactly EXS-E0303 ----
	xor	rdi, rdi
	call	fx_code
	cmp	rax, 303
	jne	.fail2

	; ---- 3: the accepted twin is silent ----
	lea	rdi, [fx_asrc]
	mov	rsi, FX_ASRC_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail3

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

; fx_run(srcptr, srclen) -> rax = the number of diagnostics `chk_run` appended.
; The whole front end over one source: §8.1/§8.4 lexing, the §8.6 parse, the
; §9.1 Stage 1 tree, then Stage 2. Nothing here is hand-built, so every
; interner id the checker meets is the lexer's -- which is what lets pass 2's
; primitive table (checker/types/prim.inc) see real spellings.
proc fx_run, fxsrc, fxlen
	uses	rbx, r12
	locals
	endl
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_scratch]
	call	arena_reset

	lea	rdi, [fx_toks]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_diags]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 32
	call	vec_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_toks]
	lea	r8,  [fx_diags]
	call	lex_init
	lea	rdi, [fx_lx]
	mov	rsi, [fxsrc]
	mov	rdx, [fxlen]
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	jc	.gate

	lea	rdi, [fx_green]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 512
	call	vec_init
	lea	rdi, [fx_work]
	lea	rsi, [fx_arena]
	mov	rdx, 4
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_cmap]
	lea	rsi, [fx_arena]
	mov	rdx, 1024
	call	map_init
	lea	rdi, [fx_ctree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_green]
	lea	r8,  [fx_work]
	lea	r9,  [fx_cmap]
	call	cst_tree_init
	lea	rdi, [fx_parser]
	lea	rsi, [fx_lx]
	lea	rdx, [fx_ctree]
	call	cst_parse

	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	lea	rdi, [fx_tree]
	call	ast_verify_stage1

	; anything the lexer or the parser said is not this fixture's subject
	mov	rbx, [fx_diags + Vec.len]
	test	rbx, rbx
	jnz	.gate

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
	mov	rsi, [fxsrc]
	mov	rdx, [fxlen]
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	return
  .gate:
	; a lex or parse diagnostic means the fixture's source is wrong, not
	; that the checker said something -- answer an impossible count
	mov	rax, -1
	return
endp

; fx_code(i) -> rax = diagnostic `i`'s numeric EXS-E code, and RENDERS it, so a
; failing run shows what was actually raised instead of only a number.
proc fx_code, fxi
	uses	rbx
	locals
	endl
	mov	rax, [fx_diags + Vec.len]
	cmp	rax, [fxi]
	jbe	.none
	lea	rdi, [fx_diags]
	mov	rsi, [fxi]
	call	vec_get
	mov	rbx, rax
	mov	rdi, 1
	mov	rsi, rbx
	lea	rdx, [fx_buf]
	mov	rcx, 8192
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit
	mov	eax, [rbx + Diag.code_num]
	return
  .none:
	xor	eax, eax
	return
endp

; fx_setup -- the arenas and the interner, once.
proc fx_setup
	locals
	endl
	lea	rdi, [fx_arena]
	mov	rsi, 32 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_iarena]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_scratch]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_names]
	lea	rsi, [fx_iarena]
	mov	rdx, 1024
	call	intern_init
	xor	eax, eax
	return
  .bad:
	mov	eax, 1
	return
endp

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path	db 'chk_ty_e0303.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_rsrc:
		db 'publica functio f(a: u32, b: u8) -> u32 {', 10
		db '    redde b;', 10
		db '}', 10
  FX_RSRC_LEN = $ - fx_rsrc
  fx_asrc:
		db 'publica functio f(a: u32, b: u8) -> u32 {', 10
		db '    redde a;', 10
		db '}', 10
  FX_ASRC_LEN = $ - fx_asrc

  fx_arena:	rb sizeof.Arena
  fx_iarena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_toks:	rb sizeof.Vec
  fx_diags:	rb sizeof.Vec
  fx_lx:	rb sizeof.Lexer
  fx_green:	rb sizeof.Vec
  fx_work:	rb sizeof.Vec
  fx_cmap:	rb sizeof.Map
  fx_ctree:	rb sizeof.CstTree
  fx_parser:	rb sizeof.CstParser
  fx_tree:	rb sizeof.Ast
  fx_chk:	rb sizeof.ChkCtx
  fx_buf:	rb 8192
