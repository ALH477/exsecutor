; tests/unit/lwr_refero_release.asm
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
; lowering fixture: `retain`/`release` at scope exit, from SOURCE, for the
; first time -- and the register clobber in `lwr_frame_push` that made the
; first such binding trap instead.
;
; WHY IT NEEDS A FIXTURE OF ITS OWN. Every part of ARC was already built and
; separately tested: `refero<T>` parses and checks (cst_refero_type.asm,
; chk_ty_e0520_refero_source.asm), `retain`/`release` are IR opcodes the text
; parser reads and the verifier's rule 6 constrains (bfa_verify_rule6_ok.asm),
; the reference backend emits `exsrt_retain{,_c}`/`exsrt_release{,_c}` chosen
; by operand type, and the prelude implements all four with their aborts
; (prelude_arc.asm). What no test covered was the LOWERING deciding WHERE a
; release goes -- because nothing could put a binding on a frame's release
; list from source, so `LwrFrame.freln` was 0 in every frame ever built.
;
; That is what hid the defect. `lwr_frame_push` read `rels.len` into `rcx`
; and then CALLED `vec_get`, which loads its vector's own length into `rcx`
; for its bounds `rassert` (rt/vec.inc) -- so `frelb` was written with the
; FRAME COUNT, not the release-list base. A wrong base added to nothing is
; still nothing, so 2203 checks passed over it. The first `refero` binding to
; reach a scope exit asks for `rels[frelb + i]` past the end and the compiler
; dies with SIGILL, at every `--emitte` stage that lowers. Measured before the
; fix on `956cec5`: `firma b: refero<u64> = r;` in a function taking a
; `refero<u64>` parameter -> exit 132, `vec_get` <- `lwr_frame_release` <-
; `__lwr_release_to` <- `__lwr_redde`.
;
; THE CHECK is the printed IR, byte for byte, over two functions chosen so
; that every rule the release list carries is falsifiable by one mutation:
;
;   `f` -- two bindings in ONE frame, one `refero<T>` and one
;   `refero_communis<T>`. Pins THREE things: that a borrowed initializer
;   retains at all (spec section 6.3 as CHK 2.5 records it -- a parameter is
;   borrowed, so the binding retains); that releases run in REVERSE
;   initialisation order (spec section 6.6; `release %1` before `release %0`,
;   the two params the bindings were renamed to); and that atomicity comes
;   from the TYPE, not the instruction -- `ref` and `refc` produce the same
;   two opcodes here and different prelude routines in the emitter
;   (ssa-ir.md section 2.8).
;
;   `g` -- an inner block, then a binding in the outer one. Pins that a frame
;   releases ITS OWN list and nothing else: `a` is released at the inner `}`,
;   before `b` is ever retained. This is the function that actually kills the
;   clobber, because it is the one that needs a SECOND frame's `frelb` to be
;   the truncated `rels` length rather than a frame count.
;
;	functio f(r: refero<u64>, s: refero_communis<u64>) -> u8 {
;		firma a: refero<u64> = r;
;		firma b: refero_communis<u64> = s;
;		redde 0;
;	}
;	functio g(r: refero<u64>) -> u8 {
;		{
;			firma a: refero<u64> = r;
;		}
;		firma b: refero<u64> = r;
;		redde 0;
;	}
;
; NO `initium`, and no runtime: this fixture lowers and PRINTS. Executing a
; `retain` needs a `refc` pointing at a real object header, and nothing in the
; language can construct one yet -- the `refero` EXPRESSION form is spec
; section 8.6's `[OPEN]` (see ADR 0017). tests/ir/reject_c_arc.ir makes the
; same choice for the same reason.
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

  fx_path	db 'lwr_refero_release.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_src:	db 'functio f(r: refero<u64>, s: refero_communis<u64>) -> u8 {', 10
		db 9, 'firma a: refero<u64> = r;', 10
		db 9, 'firma b: refero_communis<u64> = s;', 10
		db 9, 'redde 0;', 10
		db '}', 10
		db 'functio g(r: refero<u64>) -> u8 {', 10
		db 9, '{', 10
		db 9, 9, 'firma a: refero<u64> = r;', 10
		db 9, '}', 10
		db 9, 'firma b: refero<u64> = r;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src

  ; ---- what `lwr_module` must print --------------------------------------
  fx_exp:
		db 'functio @f (ref refc) -> u8 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ref 0', 10
		db '%1 = param refc 1', 10
		db 'retain %0', 10
		db 'retain %1', 10
		db '%2 = iconst u8 0', 10
		db 'release %1', 10
		db 'release %0', 10
		db 'ret %2', 10
		db '}', 10
		db 'functio @g (ref) -> u8 numeri ad_parem vetita explicita conservata {', 10
		db 'b0:', 10
		db '%0 = param ref 0', 10
		db 'retain %0', 10
		db 'release %0', 10
		db 'retain %0', 10
		db '%1 = iconst u8 0', 10
		db 'release %0', 10
		db 'ret %1', 10
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
