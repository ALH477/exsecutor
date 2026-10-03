; tests/unit/chk_e0501_supersedes_e0500.asm
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
; checker fixture -- spec §4.1 rule 7 is ONE code, chosen by capability:
; *"No module-level mutable state. (`EXS-E0500`; with a capability,
; `EXS-E0501`.)"*
;
; That parenthesis is a CHOICE, not a conjunction, and the checker raised both.
; `mutabilis retis_globalis: rete;` -- §14 entry 11 -- got `EXS-E0500` from
; pass 1 (which sees `AST_F_MUTABILIS` and nothing else) and `EXS-E0501` from
; pass 3 (which sees the capability-bearing type), so one binding produced two
; codes where §14 expects exactly `{EXS-E0501}`. Measured with `build/exsc
; aedifica --hospes x86_64-linux --diagnostica json` before the fix and after
; it. The fix puts the whole rule in pass 3 -- the only pass that knows
; `Decl.ty` and therefore which code applies -- and pass 1 says nothing;
; `checker/resolve/resolve.inc`'s `.binding` arm and
; `checker/rows/compute.inc`'s `__chk_row_e0501` each record their half.
;
; THE FOUR ROWS ARE A 2x2, AND THE TWO AXES ARE THE TWO HALVES OF THE RULE:
;
;	0  `mutabilis x: rete;`      capability, mutable     {EXS-E0501}
;	1  `firma x: rete;`          capability, immutable   {EXS-E0501}
;	2  `mutabilis c: i32 = 0;`   no capability, mutable  {EXS-E0500}
;	3  `firma c: i32 = 0;`       no capability, immutable   clean
;
; Row 1 is why `EXS-E0501` is not "`EXS-E0500` plus a capability": a `firma`
; module-level binding of capability type is still ambient authority (§4.3,
; checker.md section 2.3) and `firma` is not mutable state, so the two codes do
; not nest. Row 2 is the existing `tests/unit/chk_e0500_module_mutabilis.asm`'s
; rejected twin, restated here as a SET rather than as a count, so that a
; regression in which `EXS-E0501` starts firing for `i32` fails here as well.
; Row 3 is the accepted twin of both and is one keyword from row 2.
;
; `mutabilis x: rete;` WITH NO INITIALISER PARSES. §14 entry 11 says `rete`
; "has no literal, no cast and no default... so this binding cannot be given an
; initial value at all," and the grammar agrees: `BindingDecl`'s `= Expr` is
; optional, so rows 0 and 1 reach Stage 2 and are diagnosed for the rule they
; break rather than for a missing initialiser. Confirmed by running, not
; assumed -- the fixture in tests/conformance/ for entry 11 omits the `;` and
; therefore also gets `EXS-E0203`, which is that file's own defect and is not
; this one's to fix (CLAUDE.md, Scope).
;
; NON-VACUITY. Each row asserts the exact COUNT of diagnostics and then the
; code by number; row 3 asserts zero.
;
; Exit 0 = every row passed; 11+i = row i (0-based) did not match, with the
; diagnostics it did produce printed to stdout first.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1 rule 7, §4.3, §8.3 (E0500's
; required fix), §13; docs/design/checker.md sections 2.2, 2.3, 2.4, finding 24.
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

  fx_path	db 'chk_e0501_supersedes_e0500.exsc'
  FX_PATH_LEN = $ - fx_path

  ; ==== ROWS (each source measured with exsc --diagnostica json) ====
  fx_p01:	db 'mutabilis retis_globalis: rete;', 10
  fx_p01_LEN = $ - fx_p01
  fx_p02:	db 'firma retis_globalis: rete;', 10
  fx_p02_LEN = $ - fx_p02
  fx_p03:	db 'mutabilis c: i32 = 0;', 10
  fx_p03_LEN = $ - fx_p03
  fx_p04:	db 'firma c: i32 = 0;', 10
  fx_p04_LEN = $ - fx_p04

  fx_tab:
	dq fx_p01, fx_p01_LEN
	dd 1, 501, 0, 0
	dq fx_p02, fx_p02_LEN
	dd 1, 501, 0, 0
	dq fx_p03, fx_p03_LEN
	dd 1, 500, 0, 0
	dq fx_p04, fx_p04_LEN
	dd 0, 0, 0, 0
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
