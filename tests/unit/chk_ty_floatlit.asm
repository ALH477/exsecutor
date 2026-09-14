; tests/unit/chk_ty_floatlit.asm
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
; checker fixture -- the VALUE of a float literal (spec §8.4 as amended by
; the float wave), from REAL SOURCE: the lexer lexes the spelling, the AST
; fold classifies it FLOAT, `chk_ty_floatval` packs the decimal pair,
; `__chk_ty_fits` calls rt/dec754.inc's `dec754_bits` and OVERWRITES the
; `Lit`'s `konst` with the IEEE-754 pattern, and this fixture reads the
; `konst` back out and compares it with the pattern Python's
; `struct.pack('<d', ...)` / `struct.pack('<f', ...)` gives for the same
; spelling -- the same golden evidence tests/unit/dec754_golden.asm pins for
; the converter in isolation, here pinned through the whole front end.
;
; chk_ty_hexlit.asm needed an indirection (a decimal placeholder re-pointed
; at the hex spelling) because its subject was spellings the LEXER rejected;
; this file's subject is well-formed source, so the literal is written
; directly and no row touches the lexer's refusal paths -- those are
; lex_float_literal.asm's.
;
; Rows, each ONE literal (the scan below refuses a second), the accepted
; ones checking the diagnostic count AND the konst, the refused ones the
; count and the CODE:
;
;   1.5               -> f64  accepted, 0x3FF8000000000000 (the twin of
;                                 lwr_float.asm's `fconst` operand)
;   1.5               -> f32  accepted, 0x3FC00000
;   0.5               -> f32  accepted, 0x3F000000
;   1e-3              -> f64  accepted, 0x3F50624DD2F1A9FC (exp-only form)
;   2.5               -> f64  accepted, 0x4004000000000000
;   3.14159           -> f64  accepted, 0x400921F9F01B866E (a real mantissa)
;   0.0               -> f64  accepted, 0 (a literal that IS zero is legal)
;   1e-320            -> f64  accepted, 0x7E8 (SUBNORMAL, legal: it kept a bit)
;   5e-324            -> f64  accepted, 1 (the minimum subnormal)
;   1.23456789012345  -> f64  accepted, 0x3FF3C0CA428C59DD (15 sig digits:
;                                 the cap, one more is refused)
;   1.2345678901234567-> f64  EXS-E0308 (17 significant digits)
;   1e309             -> f64  EXS-E0308 (Inf: above the finite range)
;   1e-338            -> f64  EXS-E0308 (zero from a NONZERO mantissa:
;                                 underflow ate every digit)
;   1e40              -> f32  EXS-E0308 (f32's own Inf cut)
;   1e-60             -> f32  EXS-E0308 (f32's own underflow-to-zero)
;   1                 -> f32  EXS-E0308 (an INT literal is not a float
;                                 value; §8.4 settled no coercion)
;   1.5               -> u8   EXS-E0308 (a FLOAT literal is not an integer
;                                 value; the mirror of the row above)
;
; Exit 0 = every row held; 10+N = row N's diagnostic count or code; 40+N =
; row N's konst; 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 32		; dq src, srclen; dd count, code; dq konst -- the
			; konst is read only when the expected count is 0

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99

	xor	r12, r12
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
	xor	ebx, ebx
  .show:
	cmp	rbx, r14
	jae	.shown
	mov	rdi, rbx
	call	fx_code
	inc	rbx
	jmp	.show
  .shown:
	mov	ecx, [r13 + 16]		; expected diagnostic count
	cmp	r14, rcx
	jne	.bad_count
	test	r14, r14
	jnz	.code_check
	; accepted: the konst must now hold the IEEE bits, not the pair
	lea	rdi, [fx_tree]
	mov	rsi, [fx_lit]
	call	ast_konst_get
	cmp	rax, [r13 + 24]
	jne	.bad_value
	jmp	.next
  .code_check:
	cmp	r14, 1
	jne	.bad_count		; every refused row here is exactly one
	xor	edi, edi
	call	fx_diag_code
	cmp	eax, [r13 + 20]
	jne	.bad_code
  .next:
	inc	r12
	jmp	.row
  .bad_count:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .bad_code:
	lea	rdi, [r12 + 41]
	call	sys_exit_group
  .bad_value:
	lea	rdi, [r12 + 50]
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
; Plain labels (a `proc` argument name is an unmangled global); each helper
; pushes an odd number of registers.

; fx_run(rdi = source, rsi = length) -> rax = the diagnostics `chk_run`
; appended, -1 if the lexer or the parser said anything (the row's source is
; then wrong, and no count can match it). `fx_lit` is left holding the one
; `Lit` node's id.
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
	cmp	qword [fx_diags + Vec.len], 0
	jne	.gate

	; ---- the one `Lit` --------------------------------------------------
	mov	qword [fx_lit], 0
	mov	rbx, 1
  .scan:
	lea	rdi, [fx_tree]
	call	ast_node_count
	cmp	rbx, rax
	ja	.scanned
	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_node_at
	cmp	word [rax + AstNode.kind], AST_LIT
	jne	.scan_next
	cmp	qword [fx_lit], 0
	jne	.gate			; two literals: the row is malformed
	mov	[fx_lit], rbx
  .scan_next:
	inc	rbx
	jmp	.scan
  .scanned:
	cmp	qword [fx_lit], 0
	je	.gate

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

; fx_code(rdi = i) -- render diagnostic `i` to stdout.
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

; fx_diag_code(rdi = i) -> eax = diagnostic `i`'s code number. NOTE, found
; by running: the load must be INTO EAX -- `mov ecx, [rax + ...]` leaves
; eax holding vec_get's POINTER and the caller compares the pointer's low
; half against the code. vec_get returns in rax; reuse it as the destination
; directly.
  fx_diag_code:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [fx_diags]
	call	vec_get
	mov	eax, [rax + Diag.code_num]
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

  fx_path	db 'chk_ty_floatlit.exsc'
  FX_PATH_LEN = $ - fx_path

  ; one source per row: `functio f() -> WIDTH { redde LIT; }` -- exactly one
  ; literal, so the scan above can name it
  macro fx_src name, width, lit
	name:	db 'functio f() -> '
	db width
	db ' {', 10
	db 9, 'redde '
	db lit
	db ';', 10
	db '}', 10
	name#_LEN = $ - name
  end macro

  fx_src fx_s01, 'f64', '1.5'
  fx_src fx_s02, 'f32', '1.5'
  fx_src fx_s03, 'f32', '0.5'
  fx_src fx_s04, 'f64', '1e-3'
  fx_src fx_s05, 'f64', '2.5'
  fx_src fx_s06, 'f64', '3.14159'
  fx_src fx_s07, 'f64', '0.0'
  fx_src fx_s08, 'f64', '1e-320'
  fx_src fx_s09, 'f64', '5e-324'
  fx_src fx_s10, 'f64', '1.23456789012345'
  fx_src fx_s11, 'f64', '1.2345678901234567'
  fx_src fx_s12, 'f64', '1e309'
  fx_src fx_s13, 'f64', '1e-338'
  fx_src fx_s14, 'f32', '1e40'
  fx_src fx_s15, 'f32', '1e-60'
  fx_src fx_s16, 'f32', '1'
  fx_src fx_s17, 'u8',  '1.5'

  ; row: dq source, its length; dd expected count, expected first code;
  ; dq the expected konst (read only when the count is 0) -- exactly 32
  macro fx_row src, count, code, konst
	dq src, src#_LEN
	dd count, code
	dq konst
  end macro

  fx_tab:
	fx_row fx_s01, 0,   0,   4609434218613702656	; 1.5    f64
	fx_row fx_s02, 0,   0,   1069547520		; 1.5    f32
	fx_row fx_s03, 0,   0,   1056964608		; 0.5    f32
	fx_row fx_s04, 0,   0,   4562254508917369340	; 1e-3   f64
	fx_row fx_s05, 0,   0,   4612811918334230528	; 2.5    f64
	fx_row fx_s06, 0,   0,   4614256650576692846	; 3.14159 f64
	fx_row fx_s07, 0,   0,   0			; 0.0    f64 -- zero is legal
	fx_row fx_s08, 0,   0,   2024			; 1e-320 f64 -- subnormal
	fx_row fx_s09, 0,   0,   1			; 5e-324 f64 -- min subnormal
	fx_row fx_s10, 0,   0,   4608238818662570461	; 15 digits: the cap
	fx_row fx_s11, 1,   308, 0			; 17 digits
	fx_row fx_s12, 1,   308, 0			; 1e309  f64 -- Inf
	fx_row fx_s13, 1,   308, 0			; 1e-338 f64 -- underflow
	fx_row fx_s14, 1,   308, 0			; 1e40   f32 -- f32 Inf
	fx_row fx_s15, 1,   308, 0			; 1e-60  f32 -- f32 underflow
	fx_row fx_s16, 1,   308, 0			; INT literal into a float
	fx_row fx_s17, 1,   308, 0			; FLOAT literal into an int
  FX_NROWS = ($ - fx_tab) / FX_ROW
  assert ($ - fx_tab) mod FX_ROW = 0
  assert FX_NROWS = 17

segment readable writeable
  fx_lit:	rq 1
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