; tests/unit/lwr_directorium.asm
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 DeMoD LLC.
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
; lowering fixture -- ADR 0017 stage 2's open question D6 ("whether the
; lowering passes a hidden full-atom carrier beside a `Directorium`"),
; answered from SOURCE to verified IR, printed and compared BYTE FOR BYTE --
; lwr_transitus.asm's harness, unchanged.
;
; WHAT IT PINS:
;   salva   declared `poscit sicut d` over a `Directorium`: its signature is
;           `(ptr) -> u8` -- the Directorium and NOTHING ELSE. No carrier
;           travels beside it: `__lwr_sig_carriers` emits one `ptr` per ATOM
;           item of the row and none for a `sicut` ordinal (runtime.md 5).
;           Its call to `d.crea("x")` is `@exsrt_directorium_crea` with three
;           `ptr`s -- the hidden 17-byte `eventus` slot, the receiver, the
;           `textus` -- and no carrier either (every prelude row is empty).
;   radix   declared `poscit archivum`: its signature is `(ptr ptr) -> u8`,
;           the atom's carrier FIRST and then `v` -- the contrast that shows
;           the line above measures something. `ad_radicem` is passed the
;           carrier as its explicit argument (design R2), and `salva(d)` is
;           called with one argument.
;
; NON-VACUITY, run when this fixture was written, each in a scratch copy:
;   - `__lwr_sig_carriers` emitting a `ptr` for an ordinal as well as for an
;     atom -> exit 20 (salva's signature grows to `(ptr ptr)`);
;   - `__lwr_path` without its atom arm (`archivum` as a value falls to
;     `__lwr_module_const`, as it did before this fixture) -> exit 132, the
;     lowering's `rassert`.
;
; Exit 0 = the printed module matches. 10 = setup, 11 = the front end said
; something, 12 = `lwr_module` refused, 13 = the verifier named a rule,
; 20 = length mismatch, 21 = byte mismatch. Run with any argument to write
; the produced text to stdout instead of comparing.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	mov	rax, [rsp]			; argc, before anything moves rsp
	mov	[fx_argc], rax
	call	fx_setup
	test	eax, eax
	jnz	.bad10

	call	fx_front
	test	rax, rax
	jnz	.bad11

	lea	rdi, [fx_mod]
	lea	rsi, [fx_arena]
	call	bfa_module_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_mod]
	lea	rdx, [fx_arena]
	lea	rcx, [fx_lscr]
	call	lwr_module
	test	rax, rax
	jnz	.bad12

	; every function with a body, through the verifier (lwr_saluta.asm
	; explains why a bodiless declaration is skipped; there is none here)
	xor	r12, r12
  .vloop:
	mov	rdi, [fx_mod + BfaModule.funcs]
	cmp	r12, [rdi + Vec.len]
	jae	.vdone
	mov	rsi, r12
	call	vec_get
	mov	rsi, [rax]
	mov	rdi, [rsi + BfaFunc.blocks]
	cmp	qword [rdi + Vec.len], 0
	je	.vnext
	lea	rdi, [fx_mod]
	lea	rdx, [fx_arena]
	lea	rcx, [fx_vb]
	lea	r8,  [fx_vi]
	call	bfa_verify_func
	test	eax, eax
	jnz	.bad13
  .vnext:
	inc	r12
	jmp	.vloop
  .vdone:

	lea	rdi, [fx_mod]
	lea	rsi, [fx_arena]
	call	bfa_print_module
	mov	[fx_outp], rax
	mov	[fx_outl], rdx
	cmp	qword [fx_argc], 1
	ja	.dump

	cmp	qword [fx_outl], FX_EXP_LEN
	jne	.bad20
	mov	rdi, [fx_outp]
	mov	rsi, [fx_outl]
	lea	rdx, [fx_exp]
	mov	rcx, FX_EXP_LEN
	call	__bfa_streq
	test	eax, eax
	jz	.bad21
	xor	edi, edi
	call	sys_exit_group
  .dump:
	mov	edi, 1
	mov	rsi, [fx_outp]
	mov	rdx, [fx_outl]
	call	sys_write
	xor	edi, edi
	call	sys_exit_group
  .bad10:
	mov	edi, 10
	call	sys_exit_group
  .bad11:
	mov	edi, 11
	call	sys_exit_group
  .bad12:
	mov	edi, 12
	call	sys_exit_group
  .bad13:
	mov	edi, 13
	call	sys_exit_group
  .bad20:
	mov	edi, 2
	mov	rsi, [fx_outp]
	mov	rdx, [fx_outl]
	call	sys_write
	mov	edi, 20
	call	sys_exit_group
  .bad21:
	mov	edi, 2
	mov	rsi, [fx_outp]
	mov	rdx, [fx_outl]
	call	sys_write
	mov	edi, 21
	call	sys_exit_group

; the whole chain, as compiler/x86_64/exsc.asm includes it
include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'
include '../../compiler/x86_64/lower/lower.inc'

; ---- harness ---------------------------------------------------------------
; Plain labels (a `proc` argument name is an unmangled global); each helper
; pushes an odd number of registers.

; fx_front -> rax = diagnostics from the lexer, the parser and the checker
; together; the source is fixed, so anything but 0 is a broken fixture.
  fx_front:
	push	rbx
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
	lea	rsi, [fx_src]
	mov	rdx, FX_SRC_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	jc	.bad
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
	cmp	qword [fx_diags + Vec.len], 0
	jne	.bad
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
	lea	rsi, [fx_src]
	mov	rdx, FX_SRC_LEN
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	pop	rbx
	ret
  .bad:
	mov	rax, -1
	pop	rbx
	ret

; fx_setup -> eax = 0 once the four arenas and the interner exist.
  fx_setup:
	push	rbx
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
	lea	rdi, [fx_lscr]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_names]
	lea	rsi, [fx_iarena]
	mov	rdx, 1024
	call	intern_init
	xor	eax, eax
	pop	rbx
	ret
  .bad:
	mov	eax, 1
	pop	rbx
	ret

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

  fx_path	db 'lwr_directorium.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_src:	db 'publica functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'discerne d.crea("x") {', 10
		db 9, 9, 'casus prosperum(w) { redde 0; }', 10
		db 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, '}', 10
		db '}', 10
		db 'publica functio radix(v: textus) -> u8 poscit archivum {', 10
		db 9, 'discerne Directorium.ad_radicem(archivum, v) {', 10
		db 9, 9, 'casus prosperum(d) { redde salva(d); }', 10
		db 9, 9, 'casus adversum(e) { redde 2; }', 10
		db 9, '}', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src

  fx_exp:	db 'data $0 1 1 78', 10
		db 'functio @exsrt_directorium_ad_radicem (ptr ptr ptr) -> void numeri ad_parem vetita explicita conservata externus sysv_amd64 {', 10
		db '}', 10
		db 'functio @exsrt_directorium_crea (ptr ptr ptr) -> void numeri ad_parem vetita explicita conservata externus sysv_amd64 {', 10
		db '}', 10
		db 'functio @salva (ptr) -> u8 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = slot 17 1', 10
		db '%2 = slot 16 8', 10
		db '%3 = gaddr 0', 10
		db 'store ptr %2 0 nativus %3', 10
		db '%4 = iconst u64 1', 10
		db 'store u64 %2 8 nativus %4', 10
		db 'call void @exsrt_directorium_crea %1 %0 %2', 10
		db '%5 = load u8 %1 0 nativus', 10
		db '%6 = iconst u8 0', 10
		db '%7 = cmp.eq u8 %5 %6', 10
		db 'br %7 b1 b2', 10
		db 'b1:', 10
		db '%8 = slot 16 8', 10
		db '%9 = addr %1 1', 10
		db 'copy 16 %8 %9', 10
		db '%10 = iconst u8 0', 10
		db 'ret %10', 10
		db 'b2:', 10
		db 'jmp b3', 10
		db 'b3:', 10
		db '%11 = slot 2 1', 10
		db '%12 = addr %1 1', 10
		db 'copy 2 %11 %12', 10
		db '%13 = iconst u8 1', 10
		db 'ret %13', 10
		db '}', 10
		db 'functio @radix (ptr ptr) -> u8 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = param ptr 1', 10
		db '%2 = slot 17 1', 10
		db 'call void @exsrt_directorium_ad_radicem %2 %0 %1', 10
		db '%3 = load u8 %2 0 nativus', 10
		db '%4 = iconst u8 0', 10
		db '%5 = cmp.eq u8 %3 %4', 10
		db 'br %5 b1 b2', 10
		db 'b1:', 10
		db '%6 = slot 16 8', 10
		db '%7 = addr %2 1', 10
		db 'copy 16 %6 %7', 10
		db '%8 = call u8 @salva %6', 10
		db 'ret %8', 10
		db 'b2:', 10
		db 'jmp b3', 10
		db 'b3:', 10
		db '%9 = slot 2 1', 10
		db '%10 = addr %2 1', 10
		db 'copy 2 %9 %10', 10
		db '%11 = iconst u8 2', 10
		db 'ret %11', 10
		db '}', 10
  FX_EXP_LEN = $ - fx_exp

segment readable writeable
  fx_argc:	rq 1
  fx_outp:	rq 1
  fx_outl:	rq 1
  fx_vb:	dd 0
  fx_vi:	dd 0
  fx_arena:	rb sizeof.Arena
  fx_iarena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_lscr:	rb sizeof.Arena
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
  fx_mod:	rb sizeof.BfaModule
