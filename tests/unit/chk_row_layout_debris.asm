; tests/unit/chk_row_layout_debris.asm
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
; checker fixture -- spec §5.2 rule 3, `EXS-E0201` WITHOUT `EXS-E0322`: the
; wire-layout pass must not evaluate what the parser recovered from.
;
; §14 entry 22 is `campus: u4:maior` in a `@transitus` struct, and §5.2 rule 3
; says exactly one code fires: *"Byte order is required above 8 bits and is not
; part of the grammar at or below it... `u4:maior` does not parse, so it is
; EXS-E0201. Neither needs a new code."* Pass 4 raised `EXS-E0322` as well,
; over the `u4` the parser left behind -- the same partial-trailing-byte
; violation entry 21 raises on purpose -- so one construct produced two codes.
; Measured with `build/exsc aedifica --hospes x86_64-linux --diagnostica json`
; before the fix and after it; this fixture is that measurement, run through
; the real lexer, parser, Stage 1 and Stage 2 rather than a hand-built tree.
;
; THE PAIR IS ONE EDIT APART, AND THE EDIT IS THE ANNOTATION:
;
;	campus: u4:maior          -- rejected, exactly {EXS-E0201}   (row 0)
;	versio: u4 / genus: u3    -- rejected, exactly {EXS-E0322}   (row 1)
;
; Row 1 is §14 entry 21's own shape and is the reason this is not a fixture
; for "pass 4 says nothing about sub-byte fields": it still must. Rows 2 and 3
; separate the two halves of that claim one step further -- row 2's widths DO
; sum to a whole byte and it is still only `EXS-E0201`, so the suppression is
; not an accident of the sum; row 3's first field carries a byte order legally
; (`u16:maior`, above eight bits) and its trailing `u4` is still `EXS-E0322`,
; so the suppression keys on the UNWRITABLE annotation and not on the presence
; of any annotation at all.
;
; NON-VACUITY. Each row asserts the exact COUNT and then each code by number.
; Changing `322` or `201` to any other number, or a count to any other count,
; fails that row -- which is what makes "it rejected" a claim about a code
; rather than about the checker having said something.
;
; Exit 0 = every row passed; 11+i = row i (0-based) did not match, with the
; diagnostics it did produce printed to stdout first.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.2 (the four bit-width rules), §8.6
; (`u12` does not parse), §13; docs/design/checker.md sections 2.2, 2.4,
; hazard H3, finding 23.
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

  fx_path	db 'chk_row_layout_debris.exsc'
  FX_PATH_LEN = $ - fx_path

  ; ==== ROWS (each source measured with exsc --diagnostica json) ====
  ; d01: §14 entry 22 -- a byte order on a SUB-BYTE field. Unwritable under
  ; §5.2 rule 3, so `cst/parse.inc` raises EXS-E0201 and keeps the suffix in
  ; the tree for the round trip; pass 4 must say nothing about the struct.
  fx_d01:	db '@transitus', 10
		db 'publica structura Malum2 {', 10
		db 9, 'campus: u4:maior', 10
		db '}', 10
  fx_d01_LEN = $ - fx_d01
  ; d02: §14 entry 21 -- the SAME partial trailing byte with no parse error.
  ; 4 + 3 = 7 bits, so EXS-E0322 still fires. This is d01's twin.
  fx_d02:	db '@transitus', 10
		db 'publica structura Malum {', 10
		db 9, 'versio: u4', 10
		db 9, 'genus:  u3', 10
		db '}', 10
  fx_d02_LEN = $ - fx_d02
  ; d03: debris whose widths DO sum to a whole byte -- 4 + 4 = 8. Still only
  ; EXS-E0201: the suppression is of the struct, not of rule 4's arithmetic.
  fx_d03:	db '@transitus', 10
		db 'publica structura Tres {', 10
		db 9, 'a: u4:maior', 10
		db 9, 'b: u4', 10
		db '}', 10
  fx_d03_LEN = $ - fx_d03
  ; d04: a byte order written LEGALLY (u16, above eight bits) and a partial
  ; trailing byte after it. No debris, so EXS-E0322 fires as it always did.
  fx_d04:	db '@transitus', 10
		db 'publica structura Quattuor {', 10
		db 9, 'a: u16:maior', 10
		db 9, 'b: u4', 10
		db '}', 10
  fx_d04_LEN = $ - fx_d04

  fx_tab:
	dq fx_d01, fx_d01_LEN
	dd 1, 201, 0, 0
	dq fx_d02, fx_d02_LEN
	dd 1, 322, 0, 0
	dq fx_d03, fx_d03_LEN
	dd 1, 201, 0, 0
	dq fx_d04, fx_d04_LEN
	dd 1, 322, 0, 0
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
