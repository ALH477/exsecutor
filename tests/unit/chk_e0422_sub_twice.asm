; tests/unit/chk_e0422_sub_twice.asm
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
; checker fixture -- `EXS-E0422`, a capability bound twice in one scope. spec
; §4.5 ("shadowing is an error"), read by docs/design/checker.md section 2.1 as
; ONE VALUE PER CAPABILITY TYPE PER SCOPE -- rule 1 of the three that keep spec
; §16's kill criterion from firing. Two providers of one atom would make an
; implicit draw CHOOSE, and choosing is the search that criterion forbids.
;
; THE PAIR IS ONE LINE APART:
;
;	sub ambitus = a;  sub ambitus = a;    rejected, exactly E0422
;	sub ambitus = a;                      accepted, no diagnostic
;
; (`a` is `m.ambitus()`'s result, `m: Mundus`.) Both are SOURCE, run through
; the real lexer, parser and checker in-process, like tests/unit/
; chk_directorium.asm. This fixture used to load two hand-kept AST dumps; a
; provider that is a real call needs the interner to hold the method's name,
; which a loaded dump cannot give it.
;
; WHY THE PROVIDER IS A REAL `ambitus` AND NOT A `u32` (ADR 0017, blocker 1).
; This fixture used to write `sub rete = a;` with `a: u32`, which was accepted
; only because `sub` did not check its provider's type, and that unchecked
; provider is what let `sub archivum = d;` mint the raw atom out of a
; `Directorium`. `sub P = e;` now requires `e` to have P's own capability type
; (`EXS-E0303` otherwise), so a provider must be a real value of the atom's
; type. A capability-typed PARAMETER cannot be that value here, because it
; already binds the atom (measured: `f(a: rete) { sub rete = a; }` is E0422 at
; the FIRST `sub`), so the only provider left is `m.ambitus()`, the one
; accessor the prelude has.
;
; ONE SOURCE FACT, ONE CODE. The second `sub` also declares the name `ambitus` a
; second time in the same block, which `__chk_bind` would otherwise report as
; `EXS-E0302`. It does not: two `sub`s for one atom are §4.5's capability rule
; and §13 numbers that `EXS-E0422`, so reporting the same edit again under a
; second code is what spec §8.3's "one class each, never one per message"
; argues against. Check 1 asserting a count of exactly ONE is what holds that.
;
; NON-VACUITY. Check 2 asserts the code is 422 and the caret sits on the SECOND
; `sub` statement; check 3 asserts the fix `diag_fix_required(422)` promises
; and diag/fix.inc names -- "delete the second binding".
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
	jnz	.fail99

	; ---- 1: the rejected twin raises exactly one diagnostic ----
	lea	rdi, [fx_rsrc]
	mov	rsi, FX_RSRC_LEN
	call	fx_run
	cmp	rax, 1
	jne	.fail1

	; ---- 2: exactly EXS-E0422, on the second `sub` statement ----
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	cmp	dword [r12 + Diag.code_num], 422
	jne	.fail2
	cmp	dword [r12 + Diag.span.start], 89
	jne	.fail2
	cmp	dword [r12 + Diag.span.len], 16
	jne	.fail2

	; ---- 3: the fix §8.3 promises -- delete that second binding ----
	mov	rdi, 422
	call	diag_fix_required
	cmp	eax, 1
	jne	.fail3
	cmp	dword [r12 + Diag.fix.kind], DIAG_FIX_DELETE
	jne	.fail3
	cmp	dword [r12 + Diag.fix.edit.start], 89
	jne	.fail3
	cmp	dword [r12 + Diag.fix.edit.len], 16
	jne	.fail3
	mov	rdi, 1
	mov	rsi, r12
	lea	rdx, [fx_buf]
	mov	rcx, 8192
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit

	; ---- 4: the accepted twin is clean ----
	lea	rdi, [fx_asrc]
	mov	rsi, FX_ASRC_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail4

	; ---- 5: the annotations pass 1 exists to make ----
	; The `sub`'s `Path` and its `Seg` both name declaration 12 -- the
	; `AST_D_CAPATOM` for `ambitus`, spec §4.6 ordinal 8, at `ast_cap_base + 7`.
	lea	rdi, [fx_tree]
	call	ast_cap_base
	cmp	rax, 5
	jne	.fail5
	add	rax, AST_CAP_AMBITUS - 1
	mov	r13, rax
	lea	rdi, [fx_tree]
	mov	rsi, 12			; the `Seg` of `ambitus`
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	cmp	rcx, r13
	jne	.fail5
	lea	rdi, [fx_tree]
	mov	rsi, 13			; the `Path`
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	cmp	rcx, r13
	jne	.fail5
	lea	rdi, [fx_tree]
	mov	rsi, r13
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_CAPATOM
	jne	.fail5

	xor	edi, edi
	call	sys_exit_group
  .fail99:
	mov	rdi, 99
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

; ---- harness ---------------------------------------------------------------
; `tests/unit/chk_row_layout_src.asm`'s, with ONE change, and the change is the
; point: `fx_run` does NOT bail out when the lexer or the parser has said
; something, and it returns the TOTAL length of the diagnostic vector rather
; than `chk_run`'s own count. That is what lets a row assert a SET spanning two
; passes -- "exactly {EXS-E0201}, and nothing from pass 4" is the claim spec
; §5.2 rule 3 makes, and a harness that stopped at the parser could not make
; it. Plain labels: a `proc` argument name is an unmangled global
; (docs/asm-conventions.md 4.1). Each helper pushes an odd number of
; registers, so `rsp` is 16-aligned at every call inside it.

; fx_run(rdi = source, rsi = length) -> rax = how many diagnostics the WHOLE
; front end produced (lexer, parser, Stage 1, Stage 2); -1 if the lexer failed
; outright, which no row's source does.
  fx_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
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
	mov	rsi, r12
	mov	rdx, r13
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
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	mov	rax, [fx_diags + Vec.len]
	pop	r13
	pop	r12
	pop	rbx
	ret
  .gate:
	mov	rax, -1
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_code(rdi = i) -- render diagnostic `i` to stdout, so a failing row prints
; what it actually got instead of only a number.
  fx_code:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [fx_diags]
	call	vec_get
	mov	rbx, rax
	mov	rdi, 1
	mov	rsi, rbx
	lea	rdx, [fx_buf]
	mov	rcx, 8192
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit
	pop	rbx
	ret

; fx_is(edi = i, esi = code) -> eax = 1 if diagnostic `i` carries that code.
  fx_is:
	push	rbx
	push	r12
	push	r13
	mov	r12d, esi
	mov	esi, edi
	lea	rdi, [fx_diags]
	call	vec_get
	cmp	[rax + Diag.code_num], r12d
	jne	.no
	mov	eax, 1
	jmp	.out
  .no:
	xor	eax, eax
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_setup -> eax = 0 once the arenas and the interner exist.
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

  fx_path	db 'chk_e0422.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_rsrc	db 'publica functio f(m: Mundus) -> u8 {', 10
		db '    firma a = m.ambitus();', 10
		db '    sub ambitus = a;', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_RSRC_LEN = $ - fx_rsrc
  fx_asrc	db 'publica functio f(m: Mundus) -> u8 {', 10
		db '    firma a = m.ambitus();', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_ASRC_LEN = $ - fx_asrc

segment readable writeable
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
