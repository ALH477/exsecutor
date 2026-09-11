; tests/unit/chk_ty_acies.asm
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
; checker fixture -- spec §8.6's ARRAY LITERALS, through the whole front end:
; checker/types/types.inc's `__chk_ty_arraylit`, `__chk_ty_elemat` and
; `__chk_ty_elemadm`, reached from `__chk_ty_expr` (no expectation) and from
; `__chk_ty_want` (with one).
;
; EVERY REJECTED ROW PINS THE COUNT, THE CODE AND THE OFFSET, and every rule
; has an ACCEPTED TWIN, so a checker that rejected every array literal fails
; the twins and one that accepted every array literal fails the rest. The
; offsets are the diagnostic's span start in each row's own source, computed
; from the source text by the generator that wrote this table and confirmed
; against `exsc aedifica --diagnostica json`.
;
; Every row's function is `functio f(x: u8, y: u16, n: mensura, t: textus)`,
; so the four names below mean the same thing in every row.
;
;   1  firma a: acies<u8, 3> = [1, y, 3];                       E0303@85
;      the expected element type reaches the elements: E0303 AT `y`
;   2  firma a: acies<u8, 3> = [1, x, 3];                       clean
;      the twin: every element is the expected type
;   3  firma a: acies<u8, 3> = [1, 2];                          E0303@81
;      the count is part of the type: three expected, two written
;   4  firma a: acies<u8, 3> = [0; 2];                          E0303@81
;      the same, through the repeat form
;   5  firma a: acies<u8, 3> = [0; 3];                          clean
;      the twin: `[0; 3]` is `acies<u8, 3>`
;   6  firma a = [x, y];                                        E0303@71
;      no expectation: the first SETTLED element decides, and `y` is not it
;   7  firma a = [x, 2];                                        clean
;      the twin: the pending `2` takes `x`'s type
;   8  firma a = [1, 2];                                        E0308@67
;      a list of only pending literals, and nothing to settle it with
;   9  firma a = [0; 4];                                        E0308@67
;      the same for the repeat form
;  10  firma a: acies<u8, 2> = [1, 256];                        E0308@85
;      an element literal wider than the element type
;  11  firma a: u8 = [1, 2];                                    E0303@71
;      `u8` expected, an array written: E0303 at the literal, not E0308
;  12  firma a: acies<u8, 2> = [1; 0];                          E0308@85
;      a count of zero is not a count
;  13  firma a: acies<u8, 16> = [1; 0x10];                      clean
;      the count may be hexadecimal (spec 8.4's other INT form)
;  14  firma a: acies<mensura, 2> = [n, 2];                     clean
;      `mensura` is an admitted element type
;  15  firma a: acies<textus, 2> = [t, t];                      E0305@85
;      `textus` is not: E0305 at the literal
;  16  firma a: acies<acies<u8, 2>, 2> = [[1, 2], [3, 4]];      E0305@91
;      an `acies` element is E0305 today -- nested literals parse and do not type
;  17  firma a: acies<Par, 2> = [Par { primum: 1, secundum: 2 }, Par { primum: x, secundum: 4 }]; clean
;      a `@transitus` struct IS an admitted element type
;  18  firma a: acies<Q, 2> = [Q { n: 1 }, Q { n: x }];         E0305@103
;      a struct that is not `@transitus` is not
;  19  mutabilis a: acies<u16, 4> = [0; 4];                     clean
;      `a[i] = v` on a `mutabilis` array
;  20  firma a: acies<u16, 4> = [0; 4];                         E0306@91
;      a `firma` array is read-only: E0306
;  21  firma r = g([1, 2]);                                     clean
;      a call argument takes the parameter's type
;  22  firma r = g([1, 2, 3]);                                  E0303@119
;      and its count with it
;  23  redde [1, 2];                                            clean
;      `redde` supplies the expectation too
;  24  mutabilis a: acies<u8, 2> = [0; 2];                      clean
;      the other side of `=` supplies it
;
; THE LAST CHECK READS A TYPE, not a diagnostic: `firma a = [x, 2, x];` must
; give the literal `acies<u8, 3>` -- the count from the elements written, the
; element type from the one element that had one, and the pending `2` taking
; it. "No diagnostic" alone would also be the answer of a checker that left
; the literal untyped.
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 41 = the type source did not check
; clean or has no array literal, 42 = its type is not an `acies` of three,
; 43 = its element type is not `u8`; 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 40		; dq src, len; dd count, code0, off0, code1, off1, pad
FX_ANY = -1		; "do not check this offset"

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
  .row_ok:
	inc	r12
	jmp	.row
  .row_bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .rows_done:

	; ---- the literal's own type: `[x, 2, x]` is `acies<u8, 3>` -------------
	lea	rdi, [fx_d2]
	mov	rsi, fx_d2_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail41
	mov	edi, AST_ARRAYLIT
	call	fx_first_ty
	test	rax, rax
	jz	.fail41
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_ACIES
	jne	.fail42
	cmp	dword [rax + AstType.width], 3
	jne	.fail42
	mov	esi, [rax + AstType.a]
	lea	rdi, [fx_tree]
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_INT
	jne	.fail43
	cmp	byte [rax + AstType.sign], AST_SIGN_U
	jne	.fail43
	cmp	dword [rax + AstType.width], 8
	jne	.fail43

	xor	edi, edi
	call	sys_exit_group
  .fail41:
	mov	edi, 41
	call	sys_exit_group
  .fail42:
	mov	edi, 42
	call	sys_exit_group
  .fail43:
	mov	edi, 43
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

  fx_path	db 'chk_ty_acies.exsc'
  FX_PATH_LEN = $ - fx_path

  ; the type check's source: the only array literal in it is `[x, 2, x]`
  fx_d2:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a = [x, 2, x];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_d2_LEN = $ - fx_d2

  ; ==== ROWS ====
  fx_c01:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<u8, 3> = [1, y, 3];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c01_LEN = $ - fx_c01
  fx_c02:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<u8, 3> = [1, x, 3];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c02_LEN = $ - fx_c02
  fx_c03:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<u8, 3> = [1, 2];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c03_LEN = $ - fx_c03
  fx_c04:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<u8, 3> = [0; 2];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c04_LEN = $ - fx_c04
  fx_c05:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<u8, 3> = [0; 3];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c05_LEN = $ - fx_c05
  fx_c06:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a = [x, y];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c06_LEN = $ - fx_c06
  fx_c07:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a = [x, 2];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c07_LEN = $ - fx_c07
  fx_c08:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a = [1, 2];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c08_LEN = $ - fx_c08
  fx_c09:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a = [0; 4];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c09_LEN = $ - fx_c09
  fx_c10:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<u8, 2> = [1, 256];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c10_LEN = $ - fx_c10
  fx_c11:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: u8 = [1, 2];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c11_LEN = $ - fx_c11
  fx_c12:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<u8, 2> = [1; 0];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c12_LEN = $ - fx_c12
  fx_c13:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<u8, 16> = [1; 0x10];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c13_LEN = $ - fx_c13
  fx_c14:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<mensura, 2> = [n, 2];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c14_LEN = $ - fx_c14
  fx_c15:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<textus, 2> = [t, t];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c15_LEN = $ - fx_c15
  fx_c16:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<acies<u8, 2>, 2> = [[1, 2], [3, 4]];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c16_LEN = $ - fx_c16
  fx_c17:	db '@transitus', 10
		db 'structura Par {', 10
		db 9, 'primum: u8', 10
		db 9, 'secundum: u8', 10
		db '}', 10
		db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<Par, 2> = [Par { primum: 1, secundum: 2 }, Par { primum: x, secundum: 4 }];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c17_LEN = $ - fx_c17
  fx_c18:	db 'structura Q {', 10
		db 9, 'n: u8', 10
		db '}', 10
		db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<Q, 2> = [Q { n: 1 }, Q { n: x }];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c18_LEN = $ - fx_c18
  fx_c19:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'mutabilis a: acies<u16, 4> = [0; 4];', 10
		db 9, 'a[2] = y;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c19_LEN = $ - fx_c19
  fx_c20:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma a: acies<u16, 4> = [0; 4];', 10
		db 9, 'a[2] = y;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c20_LEN = $ - fx_c20
  fx_c21:	db 'functio g(v: acies<u8, 2>) -> u8 {', 10
		db 9, 'redde v[0];', 10
		db '}', 10
		db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma r = g([1, 2]);', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c21_LEN = $ - fx_c21
  fx_c22:	db 'functio g(v: acies<u8, 2>) -> u8 {', 10
		db 9, 'redde v[0];', 10
		db '}', 10
		db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma r = g([1, 2, 3]);', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c22_LEN = $ - fx_c22
  fx_c23:	db 'functio h() -> acies<u8, 2> {', 10
		db 9, 'redde [1, 2];', 10
		db '}', 10
		db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'firma r = h()[1];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c23_LEN = $ - fx_c23
  fx_c24:	db 'functio f(x: u8, y: u16, n: mensura, t: textus) -> u8 {', 10
		db 9, 'mutabilis a: acies<u8, 2> = [0; 2];', 10
		db 9, 'a = [x, 3];', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c24_LEN = $ - fx_c24
  ; ==== END ROWS ====

  fx_tab:
	dq fx_c01, fx_c01_LEN
	dd 1, 303, 85, 0, 0, 0
	dq fx_c02, fx_c02_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c03, fx_c03_LEN
	dd 1, 303, 81, 0, 0, 0
	dq fx_c04, fx_c04_LEN
	dd 1, 303, 81, 0, 0, 0
	dq fx_c05, fx_c05_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c06, fx_c06_LEN
	dd 1, 303, 71, 0, 0, 0
	dq fx_c07, fx_c07_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c08, fx_c08_LEN
	dd 1, 308, 67, 0, 0, 0
	dq fx_c09, fx_c09_LEN
	dd 1, 308, 67, 0, 0, 0
	dq fx_c10, fx_c10_LEN
	dd 1, 308, 85, 0, 0, 0
	dq fx_c11, fx_c11_LEN
	dd 1, 303, 71, 0, 0, 0
	dq fx_c12, fx_c12_LEN
	dd 1, 308, 85, 0, 0, 0
	dq fx_c13, fx_c13_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c14, fx_c14_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c15, fx_c15_LEN
	dd 1, 305, 85, 0, 0, 0
	dq fx_c16, fx_c16_LEN
	dd 1, 305, 91, 0, 0, 0
	dq fx_c17, fx_c17_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c18, fx_c18_LEN
	dd 1, 305, 103, 0, 0, 0
	dq fx_c19, fx_c19_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c20, fx_c20_LEN
	dd 1, 306, 91, 0, 0, 0
	dq fx_c21, fx_c21_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c22, fx_c22_LEN
	dd 1, 303, 119, 0, 0, 0
	dq fx_c23, fx_c23_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c24, fx_c24_LEN
	dd 0, 0, 0, 0, 0, 0
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
