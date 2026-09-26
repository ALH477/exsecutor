; tests/unit/chk_ty_sicut_row.asm
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
; checker fixture -- spec §4.2 and `docs/design/checker.md` section 2.1 rule 3:
; *"A parameter named by `sicut f` accepts an argument of any row and
; substitutes it... every other parameter's row must be EQUAL."* Both halves,
; from opposite ends of the same call.
;
; §14 entries 10 and 9 are the two ends. Entry 10's `applica(v: f32, f:
; functio(f32) -> f32) -> f32 poscit alloc, sicut f` is §4.2's own worked
; example of a HOF written CORRECTLY: passing `nocens` (which needs `rete`)
; into it is fine AT THE CALL'S TYPE, and the violation is that `exterior`
; declares only `poscit alloc` -- `EXS-E0421`, after pass 3's substitution.
; Entry 9's `applica_sine_ordine` writes no `sicut` at all, so its bare
; parameter type is the EMPTY row (§8.6: `poscit {}` is the explicit empty
; row, so the bare form is the implicit one) and the same argument is
; `EXS-E0303` at the call site. Before the fix entry 10 produced BOTH codes,
; because the call-site compare never consulted the callee's `sicut` list;
; measured with `build/exsc aedifica --hospes x86_64-linux --diagnostica json`.
;
; THE FOUR ROWS, each one edit from its neighbour:
;
;	0  `poscit alloc, sicut f`, caller declares `alloc`         {EXS-E0421}
;	1  `poscit alloc`         , caller declares `alloc`         {EXS-E0303}
;	2  `poscit alloc, sicut f`, caller declares `alloc, rete`   clean
;	3  `poscit alloc, sicut f`, argument `functio(u8) -> u8`    {EXS-E0303}
;
; Row 1 is the whole of entry 9 and is why this fixture cannot pass by
; accepting every function argument: dropping `sicut f` must bring `EXS-E0303`
; back. Row 2 is row 0's ACCEPTED twin -- the same `sicut` HOF, the same
; row-carrying argument, and a caller whose declared row admits it -- so the
; substitution is shown to RESOLVE rather than merely to be skipped. Row 3
; pins that the `sicut` comparison ignores the ROW and nothing else: the
; argument's parameter and result types still have to match.
;
; NON-VACUITY. Each row asserts the exact COUNT of diagnostics and then each
; code by number, so a row cannot pass by producing a different code or an
; extra one, and row 2 cannot pass by producing any at all.
;
; Exit 0 = every row passed; 11+i = row i (0-based) did not match, with the
; diagnostics it did produce printed to stdout first.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1, §4.2 (positional substitution),
; §8.6, §13; docs/design/checker.md sections 2.1 rule 3, 2.3, 2.4, findings 10,
; 11 and 25.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

; One row: dq source, length; dd expected count, code 0, code 1, pad.
FX_ROW = 32

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99

	xor	r12, r12		; row index
  .row:
	cmp	r12, FX_NROWS
	jae	.done
	mov	rax, r12
	imul	rax, FX_ROW
	lea	r13, [fx_tab]
	add	r13, rax
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	call	fx_run
	mov	r14, rax
	; render whatever the row produced, so a failure shows it
	xor	ebx, ebx
  .show:
	cmp	rbx, r14
	jae	.shown
	mov	rdi, rbx
	call	fx_code
	inc	rbx
	jmp	.show
  .shown:
	mov	ecx, [r13 + 16]
	cmp	r14, rcx
	jne	.bad			; not the expected NUMBER of diagnostics
	test	r14, r14
	jz	.ok
	xor	edi, edi
	mov	esi, [r13 + 20]
	call	fx_is
	test	eax, eax
	jz	.bad
	cmp	r14, 2
	jb	.ok
	mov	edi, 1
	mov	esi, [r13 + 24]
	call	fx_is
	test	eax, eax
	jz	.bad
  .ok:
	inc	r12
	jmp	.row
  .bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .done:
	xor	edi, edi
	call	sys_exit_group
  .fail99:
	mov	edi, 99
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

  fx_path	db 'chk_ty_sicut_row.exsc'
  FX_PATH_LEN = $ - fx_path

  ; ==== ROWS (each source measured with exsc --diagnostica json) ====
  ; s01: §14 entry 10 verbatim, with the `;`s §8.6 requires. `applica` is
  ; correct; `exterior` forwarding `nocens` while declaring only `alloc` is
  ; EXS-E0421 -- and the call's TYPE is fine, which is what `sicut f` means.
  fx_s01:	db 'publica functio applica(v: f32, f: functio(f32) -> f32) -> f32 poscit alloc, sicut f {', 10
		db 9, 'redde f(v);', 10
		db '}', 10
		db 'publica functio nocens(x: f32) -> f32 poscit rete {', 10
		db 9, 'redde x;', 10
		db '}', 10
		db 'publica functio exterior(v: f32) -> f32 poscit alloc {', 10
		db 9, 'redde applica(v, nocens);', 10
		db '}', 10
  fx_s01_LEN = $ - fx_s01
  ; s02: §14 entry 9's shape -- s01 with `, sicut f` removed. The parameter's
  ; row is then the empty one and `nocens`'s is {rete}: EXS-E0303 at the
  ; argument, by id inequality, exactly as before this fix.
  fx_s02:	db 'publica functio applica(v: f32, f: functio(f32) -> f32) -> f32 poscit alloc {', 10
		db 9, 'redde f(v);', 10
		db '}', 10
		db 'publica functio nocens(x: f32) -> f32 poscit rete {', 10
		db 9, 'redde x;', 10
		db '}', 10
		db 'publica functio exterior(v: f32) -> f32 poscit alloc {', 10
		db 9, 'redde applica(v, nocens);', 10
		db '}', 10
  fx_s02_LEN = $ - fx_s02
  ; s03: s01's ACCEPTED twin -- `exterior` declares `rete` too, so the
  ; substituted row has a provider and nothing is left to raise.
  fx_s03:	db 'publica functio applica(v: f32, f: functio(f32) -> f32) -> f32 poscit alloc, sicut f {', 10
		db 9, 'redde f(v);', 10
		db '}', 10
		db 'publica functio nocens(x: f32) -> f32 poscit rete {', 10
		db 9, 'redde x;', 10
		db '}', 10
		db 'publica functio exterior(v: f32) -> f32 poscit alloc, rete {', 10
		db 9, 'redde applica(v, nocens);', 10
		db '}', 10
  fx_s03_LEN = $ - fx_s03
  ; s04: a `sicut` parameter still compares everything but the row --
  ; `functio(u8) -> u8` where `functio(f32) -> f32` is declared is EXS-E0303.
  fx_s04:	db 'publica functio applica(v: f32, f: functio(f32) -> f32) -> f32 poscit alloc, sicut f {', 10
		db 9, 'redde f(v);', 10
		db '}', 10
		db 'publica functio malus(x: u8) -> u8 poscit rete {', 10
		db 9, 'redde x;', 10
		db '}', 10
		db 'publica functio exterior(v: f32) -> f32 poscit alloc, rete {', 10
		db 9, 'redde applica(v, malus);', 10
		db '}', 10
  fx_s04_LEN = $ - fx_s04

  fx_tab:
	dq fx_s01, fx_s01_LEN
	dd 1, 421, 0, 0
	dq fx_s02, fx_s02_LEN
	dd 1, 303, 0, 0
	dq fx_s03, fx_s03_LEN
	dd 0, 0, 0, 0
	dq fx_s04, fx_s04_LEN
	dd 1, 303, 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW
  assert ($ - fx_tab) mod FX_ROW = 0

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
