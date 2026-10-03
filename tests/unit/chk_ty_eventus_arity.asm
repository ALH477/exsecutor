; tests/unit/chk_ty_eventus_arity.asm
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
; checker fixture: the prelude's `eventus<T, E>` and `erratum` --
; docs/design/sum-types.md D5 (compiler/x86_64/prelude/eventus.inc), from
; source through the whole front end. sum-types.md §7 item 4 owed exactly
; this file: "`eventus<mensura>` with one argument is `EXS-E0304`". Every row
; pins the diagnostics EXACTLY (count, codes, offsets, measured with
; `exsc --diagnostica json` on the row's own source).
;
;   row 1  `eventus<mensura>` -- one argument short of D5's two  E0304 at `<`
;   row 2  bare `eventus` (spec §5.1's old spelling)            E0304 at the
;          name: no clause, so no `<` to point at
;   row 3  `eventus<mensura, erratum>`                           clean
;   row 4  three arguments                                       E0304
;   row 5  a module's OWN `eventus` shadows the prelude's -- a function, as
;          examples/onus/onus.exsc declares -- with no `EXS-E0302`  clean
;   row 6  ... and a module's own `typus eventus = u8;`          clean
;   row 7  `?` on an `eventus`: REFUSED, `EXS-E0305` at the `?` (D4's
;          interim state; the operator is not built)
;   row 8  `erratum`: a literal, a field read of `numerus: u16`   clean
;   row 9  `prosperum(5)` with no expectation: `E` cannot come from the
;          payload                                              E0304
;   row 10 a whole round trip: constructed by `redde`, from a parameter, and
;          matched with both arms                               clean
;   row 11 a payload of the wrong type: `prosperum(e)` with `e: erratum`
;          where `T` is `mensura`                               E0303
;
; Exit 0 = every row held; 10+N = row N's diagnostics were wrong; 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 40		; dq src, len; dd count, code0, off0, code1, off1, pad
FX_ANY = -1

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99

	xor	r12, r12		; row index
  .row:
	cmp	r12, FX_NROWS
	jae	.rows_done
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
	mov	ecx, [r13 + 16]
	cmp	r14, rcx
	jne	.row_bad
	test	r14, r14
	jz	.row_ok
	xor	edi, edi
	mov	esi, [r13 + 20]
	mov	edx, [r13 + 24]
	call	fx_diag_is
	test	eax, eax
	jz	.row_bad
	cmp	r14, 2
	jb	.row_ok
	mov	edi, 1
	mov	esi, [r13 + 28]
	mov	edx, [r13 + 32]
	call	fx_diag_is
	test	eax, eax
	jz	.row_bad
	cmp	r14, 3
	jb	.row_ok
	; a third diagnostic: its code is in the pad word, offset unchecked
	mov	edi, 2
	mov	esi, [r13 + 36]
	mov	edx, FX_ANY
	call	fx_diag_is
	test	eax, eax
	jz	.row_bad
  .row_ok:
	; the fix, where the row names one: -1 = there must be none
	mov	rax, r12
	shl	rax, 4
	lea	rcx, [fx_fixes]
	mov	rsi, [rcx + rax]
	test	rsi, rsi
	jz	.fix_ok
	mov	rdx, [rcx + rax + 8]
	call	fx_fix_is
	test	eax, eax
	jz	.fix_bad
  .fix_ok:
	inc	r12
	jmp	.row
  .fix_bad:
	lea	rdi, [r12 + 31]
	call	sys_exit_group
  .row_bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .rows_done:
	xor	edi, edi
	call	sys_exit_group
  .fail99:
	mov	edi, 99
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; ---- harness ---------------------------------------------------------------
; Plain labels, as in tests/unit/cst_error_tolerance.asm: a `proc` argument
; name is an unmangled global. Each helper pushes an odd number of registers,
; so `rsp` is 16-aligned at every call inside it.

; fx_run(rdi = source, rsi = length) -> rax = the diagnostics `chk_run`
; appended; -1 if the lexer or the parser said anything (the row's source is
; then wrong, and no count can match it).
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

; fx_fix_is(rsi = text or -1, rdx = length) -> eax = 1 if diagnostic 0 carries
; an INSERT fix with exactly that text (or, for -1, carries no fix at all).
  fx_fix_is:
	push	rbx
	push	r12
	push	r13
	mov	r12, rsi
	mov	r13, rdx
	lea	rdi, [fx_diags]
	xor	esi, esi
	call	vec_get
	mov	rbx, rax
	cmp	r12, -1
	jne	.text
	cmp	dword [rbx + Diag.fix.kind], DIAG_FIX_NONE
	jne	.no
	jmp	.yes
  .text:
	cmp	dword [rbx + Diag.fix.kind], DIAG_FIX_INSERT
	jne	.no
	cmp	[rbx + Diag.fix.text_len], r13
	jne	.no
	mov	rsi, [rbx + Diag.fix.text_ptr]
	mov	rdi, r12
	mov	rcx, r13
	cld
	repe	cmpsb
	jne	.no
  .yes:
	mov	eax, 1
	jmp	.out
  .no:
	xor	eax, eax
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_diag_is(edi = i, esi = code, edx = span start or FX_ANY) -> eax = 1 if
; diagnostic `i` has that code at that offset.
  fx_diag_is:
	push	rbx
	push	r12
	push	r13
	mov	r12d, esi
	mov	r13d, edx
	mov	esi, edi
	lea	rdi, [fx_diags]
	call	vec_get
	cmp	[rax + Diag.code_num], r12d
	jne	.no
	cmp	r13d, FX_ANY
	je	.yes
	cmp	[rax + Diag.span.start], r13d
	jne	.no
  .yes:
	mov	eax, 1
	jmp	.out
  .no:
	xor	eax, eax
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_first_ty(edi = node kind) -> rax = `Node.ty` of the first node of that
; kind in the last tree, 0 if there is none.
  fx_first_ty:
	push	rbx
	push	r12
	push	r13
	mov	r12d, edi
	mov	r13, 1
  .scan:
	lea	rdi, [fx_tree]
	call	ast_node_count
	cmp	r13, rax
	ja	.none
	lea	rdi, [fx_tree]
	mov	rsi, r13
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, r12d
	je	.hit
	inc	r13
	jmp	.scan
  .hit:
	mov	eax, [rax + AstNode.ty]
	jmp	.out
  .none:
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

  fx_path:	db 'eventus.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_c1:	db 'functio f(r: eventus<mensura>) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c1_LEN = $ - fx_c1
  fx_c2:	db 'functio f(r: eventus) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c2_LEN = $ - fx_c2
  fx_c3:	db 'functio f(r: eventus<mensura, erratum>) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c3_LEN = $ - fx_c3
  fx_c4:	db 'functio f(r: eventus<mensura, erratum, u8>) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c4_LEN = $ - fx_c4
  fx_c5:	db 'functio eventus(x: u8) -> u8 { redde x; }', 10
		db 'functio f() -> u8 {', 10
		db '    redde eventus(3);', 10
		db '}', 10
  fx_c5_LEN = $ - fx_c5
  fx_c6:	db 'typus eventus = u8;', 10
		db 'functio f(x: eventus) -> u8 {', 10
		db '    redde x;', 10
		db '}', 10
  fx_c6_LEN = $ - fx_c6
  fx_c7:	db 'functio g() -> eventus<u8, erratum> { redde prosperum(1); }', 10
		db 'functio f() -> u8 {', 10
		db '    firma x = g()?;', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c7_LEN = $ - fx_c7
  fx_c8:	db 'functio f() -> u16 {', 10
		db '    firma e = erratum { numerus: 22 };', 10
		db '    redde e.numerus;', 10
		db '}', 10
  fx_c8_LEN = $ - fx_c8
  fx_c9:	db 'functio f() -> u8 {', 10
		db '    firma r = prosperum(5);', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c9_LEN = $ - fx_c9
  fx_c10:	db 'functio g(a: mensura) -> eventus<mensura, erratum> {', 10
		db '    si a eq 0 { redde adversum(erratum { numerus: 9 }); }', 10
		db '    redde prosperum(a);', 10
		db '}', 10
		db 'functio f(a: mensura) -> mensura {', 10
		db '    discerne g(a) {', 10
		db '        casus prosperum(n) { redde n; }', 10
		db '        casus adversum(e) { redde e.numerus sicut mensura; }', 10
		db '    }', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c10_LEN = $ - fx_c10
  fx_c11:	db 'functio f(e: erratum) -> eventus<mensura, erratum> {', 10
		db '    redde prosperum(e);', 10
		db '}', 10
  fx_c11_LEN = $ - fx_c11

  fx_tab:
	; row 1: one argument
	dq fx_c1, fx_c1_LEN
	dd 1, 304, 20, 0, 0, 0
	; row 2: no argument
	dq fx_c2, fx_c2_LEN
	dd 1, 304, 13, 0, 0, 0
	; row 3: two
	dq fx_c3, fx_c3_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 4: three
	dq fx_c4, fx_c4_LEN
	dd 1, 304, 20, 0, 0, 0
	; row 5: a module function named eventus
	dq fx_c5, fx_c5_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 6: a module alias named eventus
	dq fx_c6, fx_c6_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 7: ? refused
	dq fx_c7, fx_c7_LEN
	dd 1, 305, 94, 0, 0, 0
	; row 8: erratum
	dq fx_c8, fx_c8_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 9: prosperum without expectation
	dq fx_c9, fx_c9_LEN
	dd 1, 304, 34, 0, 0, 0
	; row 10: a round trip
	dq fx_c10, fx_c10_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 11: a payload of the wrong type
	dq fx_c11, fx_c11_LEN
	dd 1, 303, 73, 0, 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW
  assert ($ - fx_tab) mod FX_ROW = 0

  fx_fixes:
	dq 0, 0
	dq 0, 0
	dq 0, 0
	dq 0, 0
	dq 0, 0
	dq 0, 0
	dq 0, 0
	dq 0, 0
	dq 0, 0
	dq 0, 0
	dq 0, 0
  assert ($ - fx_fixes) = FX_NROWS * 16

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
