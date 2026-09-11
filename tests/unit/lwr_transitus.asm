; tests/unit/lwr_transitus.asm
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
; lowering fixture: spec §5.2's places (D2), §8.6's struct literal (D3) and
; §5.2's one aggregate cast (D4), from SOURCE to verified IR -- lexed, parsed,
; walked, checked by the same routines `exsc` runs, then `lwr_module`,
; `bfa_verify_func` over every function, and `bfa_print_module` compared BYTE
; FOR BYTE against the text below. docs/design/wire-codec.md's milestone M6.
;
; WHAT EACH FUNCTION PINS:
;   numerus tempus valor   a whole-byte `u16/u24/u64:maior` field read is
;                          `load uN %p off maior`, of type `uN` (D2: the
;                          order is the access's, never the value's). `valor`
;                          is tests/programs/ordo_maior_custodia/'s `u64:maior`
;                          read, which that directory existed to see REFUSED
;                          while the lowering dropped the order; it is the
;                          positive test that replaced it.
;   versio genus           a sub-byte field is `loadbits u4 %p 1 0` and
;                          `loadbits u4 %p 1 4`. THE BIT OPERAND IS MSB-FIRST:
;                          bit 0 is the byte's most significant bit, the
;                          field occupies bits bit..bit+width-1 in that
;                          numbering, so the first-declared nibble (spec §5.2
;                          rule 1: "occupies the high bits") is bit 0.
;                          `AstLayout.bitoff` counts the same way (lower/
;                          cfg.inc's `lwr_loadbits` states it once).
;   parvus locus           `:minor` is `minor` and `:nativus` is `nativus` --
;                          the three spellings, because the checker stored
;                          every order ONE TOO HIGH until this fixture printed
;                          `minor` for a `maior` field (checker/types/sig.inc,
;                          `__chk_ty_suffix`).
;   obsigna                a `mutabilis` struct binding COPIES (`slot 17 1`,
;                          `copy 17`); `g.cursus = x` is `store u16 %p 15
;                          maior`; `g.versio = v` is `storebits u4 %p 1 0`;
;                          the aggregate result goes out through the hidden
;                          `ptr` with one more `copy`.
;   fac                    a literal naming its fields in REVERSE: the
;                          `iconst`s come out in source order (tempus, onus,
;                          meta, fons, genus, versio) and the stores in
;                          declaration order, bytes 0 through 16 -- D3's
;                          "evaluated in source order, stored in declaration
;                          order". `0xab12cd` and `0xdeadbeef` are the hex
;                          values at their field's type, `u24` and `u32`.
;   octetum                `(f sicut acies<u8, 17>)[i]` READS through the cast:
;                          no slot, no copy -- `chk`, `index`, `load u8` on
;                          `f`'s own pointer (D4's passthrough where no write
;                          can observe it).
;   exemplar scribe        a BOUND cast is its own bytes: `slot 17 1` and
;                          `copy 17` (spec §5.2, "binding it copies"), then
;                          `chk` + `index` + `load`/`store` on the copy. `w[j]
;                          = y` is an lvalue on a `mutabilis` `acies`.
;   latitudo               `n sicut u64` with `n: mensura`: two AST types, one
;                          IR `u64`, so the cast emits nothing.
;
; WHY THE IR AND NOT A RUNNING PROGRAM. The emitter does not yet emit a
; `maior` load or store, `loadbits`/`storebits`, `copy`, `chk`, `index`, or any
; width but 64 -- milestones M3 and M4, another agent's, in `backend_fasmg/`.
; The verifier passing is the evidence the IR is well-formed and not merely
; printable; tests/programs/forma/ is the program that runs once they land.
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

  fx_path	db 'lwr_transitus.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_src:	db '@transitus', 10
		db 'publica structura DeModFrame {', 10
		db 9, 'signum: u8', 10
		db 9, 'versio: u4', 10
		db 9, 'genus: u4', 10
		db 9, 'numerus: u16:maior', 10
		db 9, 'fons: u16:maior', 10
		db 9, 'meta: u16:maior', 10
		db 9, 'onus: u32:maior', 10
		db 9, 'tempus: u24:maior', 10
		db 9, 'cursus: u16:maior', 10
		db '}', 10
		db '@transitus', 10
		db 'publica structura Par {', 10
		db 9, 'valor: u64:maior', 10
		db '}', 10
		db '@transitus', 10
		db 'publica structura Minor {', 10
		db 9, 'parvus: u16:minor', 10
		db 9, 'octo: u8', 10
		db '}', 10
		db 'structura Locus {', 10
		db 9, 'n: u32:nativus', 10
		db '}', 10
		db 'functio numerus(f: DeModFrame) -> u16 {', 10
		db 9, 'redde f.numerus;', 10
		db '}', 10
		db 'functio versio(f: DeModFrame) -> u4 {', 10
		db 9, 'redde f.versio;', 10
		db '}', 10
		db 'functio genus(f: DeModFrame) -> u4 {', 10
		db 9, 'redde f.genus;', 10
		db '}', 10
		db 'functio tempus(f: DeModFrame) -> u24 {', 10
		db 9, 'redde f.tempus;', 10
		db '}', 10
		db 'functio valor(p: Par) -> u64 {', 10
		db 9, 'redde p.valor;', 10
		db '}', 10
		db 'functio parvus(p: Minor) -> u16 {', 10
		db 9, 'redde p.parvus;', 10
		db '}', 10
		db 'functio locus(l: Locus) -> u32 {', 10
		db 9, 'redde l.n;', 10
		db '}', 10
		db 'functio obsigna(f: DeModFrame, x: u16, v: u4) -> DeModFrame {', 10
		db 9, 'mutabilis g = f;', 10
		db 9, 'g.cursus = x;', 10
		db 9, 'g.versio = v;', 10
		db 9, 'redde g;', 10
		db '}', 10
		db 'functio fac(a: u8, b: u16) -> DeModFrame {', 10
		db 9, 'redde DeModFrame { cursus: b, tempus: 0xab12cd, onus: 0xdeadbeef, meta: 5, fons: 4, numerus: b, genus: 3, versio: 1, signum: a };', 10
		db '}', 10
		db 'functio octetum(f: DeModFrame, i: mensura) -> u8 {', 10
		db 9, 'redde (f sicut acies<u8, 17>)[i];', 10
		db '}', 10
		db 'functio exemplar(f: DeModFrame) -> u8 {', 10
		db 9, 'firma b = f sicut acies<u8, 17>;', 10
		db 9, 'redde b[16];', 10
		db '}', 10
		db 'functio scribe(f: DeModFrame, j: mensura, y: u8) -> u8 {', 10
		db 9, 'mutabilis w = f sicut acies<u8, 17>;', 10
		db 9, 'w[j] = y;', 10
		db 9, 'redde w[j];', 10
		db '}', 10
		db 'functio latitudo(n: mensura) -> u64 {', 10
		db 9, 'redde n sicut u64;', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src

  ; ---- what `lwr_module` must print --------------------------------------
  fx_exp:
		db 'functio @numerus (ptr) -> u16 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = load u16 %0 2 maior', 10
		db 'ret %1', 10
		db '}', 10
		db 'functio @versio (ptr) -> u4 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = loadbits u4 %0 1 0', 10
		db 'ret %1', 10
		db '}', 10
		db 'functio @genus (ptr) -> u4 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = loadbits u4 %0 1 4', 10
		db 'ret %1', 10
		db '}', 10
		db 'functio @tempus (ptr) -> u24 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = load u24 %0 12 maior', 10
		db 'ret %1', 10
		db '}', 10
		db 'functio @valor (ptr) -> u64 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = load u64 %0 0 maior', 10
		db 'ret %1', 10
		db '}', 10
		db 'functio @parvus (ptr) -> u16 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = load u16 %0 0 minor', 10
		db 'ret %1', 10
		db '}', 10
		db 'functio @locus (ptr) -> u32 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = load u32 %0 0 nativus', 10
		db 'ret %1', 10
		db '}', 10
		db 'functio @obsigna (ptr ptr u16 u4) -> void numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = param ptr 1', 10
		db '%2 = param u16 2', 10
		db '%3 = param u4 3', 10
		db '%4 = slot 17 1', 10
		db 'copy 17 %4 %1', 10
		db 'store u16 %4 15 maior %2', 10
		db 'storebits u4 %4 1 0 %3', 10
		db 'copy 17 %0 %4', 10
		db 'ret ', 10
		db '}', 10
		db 'functio @fac (ptr u8 u16) -> void numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = param u8 1', 10
		db '%2 = param u16 2', 10
		db '%3 = iconst u24 11211469', 10
		db '%4 = iconst u32 3735928559', 10
		db '%5 = iconst u16 5', 10
		db '%6 = iconst u16 4', 10
		db '%7 = iconst u4 3', 10
		db '%8 = iconst u4 1', 10
		db 'store u8 %0 0 nativus %1', 10
		db 'storebits u4 %0 1 0 %8', 10
		db 'storebits u4 %0 1 4 %7', 10
		db 'store u16 %0 2 maior %2', 10
		db 'store u16 %0 4 maior %6', 10
		db 'store u16 %0 6 maior %5', 10
		db 'store u32 %0 8 maior %4', 10
		db 'store u24 %0 12 maior %3', 10
		db 'store u16 %0 15 maior %2', 10
		db 'ret ', 10
		db '}', 10
		db 'functio @octetum (ptr u64) -> u8 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = param u64 1', 10
		db '%2 = iconst u64 17', 10
		db 'chk %1 %2', 10
		db '%3 = index %0 %1 1', 10
		db '%4 = load u8 %3 0 nativus', 10
		db 'ret %4', 10
		db '}', 10
		db 'functio @exemplar (ptr) -> u8 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = slot 17 1', 10
		db 'copy 17 %1 %0', 10
		db '%2 = iconst u64 16', 10
		db '%3 = iconst u64 17', 10
		db 'chk %2 %3', 10
		db '%4 = index %1 %2 1', 10
		db '%5 = load u8 %4 0 nativus', 10
		db 'ret %5', 10
		db '}', 10
		db 'functio @scribe (ptr u64 u8) -> u8 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ptr 0', 10
		db '%1 = param u64 1', 10
		db '%2 = param u8 2', 10
		db '%3 = slot 17 1', 10
		db 'copy 17 %3 %0', 10
		db '%4 = iconst u64 17', 10
		db 'chk %1 %4', 10
		db '%5 = index %3 %1 1', 10
		db 'store u8 %5 0 nativus %2', 10
		db '%6 = iconst u64 17', 10
		db 'chk %1 %6', 10
		db '%7 = index %3 %1 1', 10
		db '%8 = load u8 %7 0 nativus', 10
		db 'ret %8', 10
		db '}', 10
		db 'functio @latitudo (u64) -> u64 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param u64 0', 10
		db 'ret %0', 10
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
