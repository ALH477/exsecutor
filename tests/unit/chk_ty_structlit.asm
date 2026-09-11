; tests/unit/chk_ty_structlit.asm
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
; checker fixture -- spec §8.6's struct literal (docs/design/wire-codec.md D3;
; the spec cites this fixture as what exercises its rules), through the whole front end:
; checker/types/member.inc's `__chk_ty_structlit`.
;
; EVERY REJECTED ROW PINS THE COUNT, THE CODE AND THE OFFSET, and the rules
; have accepted twins, so a checker that rejected every literal fails the
; twins and one that accepted every literal fails the rest:
;
;   rule                                           rejected          accepted
;   a field the struct does not declare (E0301)    S { a, z, b }     S { a, b }
;   a field named twice (E0302)                    S { a, a, b }     S { b, a }
;   a field left out (E0304), ONCE per literal     S { a }, S { }
;   the value is typed against the field (E0303)   S { b: q: u32 }
;   ... a literal takes the field's width (E0308)  S { a: 256 }
;   the path must name a structura (E0305)         f { } (a function),
;                                                  x { } (a binding),
;                                                  mensura { } (a type
;                                                  that is not one)
;   an unresolved path is E0301, once              Nemo { }
;   D2: a `u16:maior` field takes a `u16`          W { n: q: u32 }   W { n: y: u16 }
;   ... and a `u4` field's literal fits `u4`       W { h: 16 }       W { h: 15, n: 0x1234 }
;   two faults in one literal: both, in order      S { a, z, a, b }
;   a literal is an expression (a member of one)                     (S { .. }).a
;
; The offsets are the diagnostic's span start in each row's own source,
; computed by the generator that wrote the table (the needle in each row's
; comment) and confirmed by `exsc aedifica --diagnostica json`.
;
; THE LAST CHECK READS A TYPE, not a diagnostic: see `fx_d2`.
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 41 = the D2 source did not check clean
; or has no literal, 42 = the literal is not a sixteen-bit unsigned integer,
; 43 = it carries a byte order; 99 = setup.
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

	; ---- D2 at the literal: `n: 0x1234` into a `u16:maior` field ----------
	; The literal must come out `u16` -- kind int, unsigned, sixteen bits and
	; NO ORDER -- not `u16:maior`: a value has no byte order (spec §5.2), and
	; a checker that typed the initialiser against the declaration's own type
	; would give the literal the ordered id. "No diagnostic" alone would also
	; be the answer of one that left the literal pending.
	lea	rdi, [fx_d2]
	mov	rsi, fx_d2_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail41
	mov	edi, AST_LIT
	call	fx_first_ty
	test	rax, rax
	jz	.fail41
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_INT
	jne	.fail42
	cmp	byte [rax + AstType.sign], AST_SIGN_U
	jne	.fail42
	cmp	dword [rax + AstType.width], 16
	jne	.fail42
	cmp	byte [rax + AstType.order], AST_ORD_NATIVUS
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

  fx_path	db 'chk_ty_structlit.exsc'
  FX_PATH_LEN = $ - fx_path

  ; the D2 check's source: the ONLY literal before `redde 0` is `0x1234`
  fx_d2:	db '@transitus', 10
		db 'structura W {', 10
		db 9, 'h: u4', 10
		db 9, 'l: u4', 10
		db 9, 'n: u16:maior', 10
		db 9, 'm: u8', 10
		db '}', 10
		db 'functio f(x: u4, z: u8) -> u8 {', 10
		db 9, 'firma v = W { h: x, l: x, n: 0x1234, m: z };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_d2_LEN = $ - fx_d2

  ; ==== ROWS (rows.py) ====
  fx_c01:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(x: u8, y: u16) -> u8 {', 10
		db 9, 'firma s = S { a: x, z: 1, b: y };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c01_LEN = $ - fx_c01
  fx_c02:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(x: u8, y: u16) -> u8 {', 10
		db 9, 'firma s = S { a: x, a: 2, b: y };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c02_LEN = $ - fx_c02
  fx_c03:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(x: u8) -> u8 {', 10
		db 9, 'firma s = S { a: x };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c03_LEN = $ - fx_c03
  fx_c04:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(x: u8) -> u8 {', 10
		db 9, 'firma s = S { };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c04_LEN = $ - fx_c04
  fx_c05:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(x: u8, q: u32) -> u8 {', 10
		db 9, 'firma s = S { a: x, b: q };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c05_LEN = $ - fx_c05
  fx_c06:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(y: u16) -> u8 {', 10
		db 9, 'firma s = S { a: 256, b: y };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c06_LEN = $ - fx_c06
  fx_c07:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(y: u16) -> u8 {', 10
		db 9, 'firma s = f { a: 1, b: y };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c07_LEN = $ - fx_c07
  fx_c08:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(x: u8, y: u16) -> u8 {', 10
		db 9, 'firma s = x { a: 1, b: y };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c08_LEN = $ - fx_c08
  fx_c09:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(y: u16) -> u8 {', 10
		db 9, 'firma s = mensura { a: 1, b: y };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c09_LEN = $ - fx_c09
  fx_c10:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(y: u16) -> u8 {', 10
		db 9, 'firma s = Nemo { a: 1, b: y };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c10_LEN = $ - fx_c10
  fx_c11:	db '@transitus', 10
		db 'structura W {', 10
		db 9, 'h: u4', 10
		db 9, 'l: u4', 10
		db 9, 'n: u16:maior', 10
		db 9, 'm: u8', 10
		db '}', 10
		db 'structura L {', 10
		db 9, 'x: u16', 10
		db 9, 'z: u16', 10
		db '}', 10
		db 'functio f(q: u32) -> u8 {', 10
		db 9, 'firma v = W { h: 1, l: 2, n: q, m: 3 };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c11_LEN = $ - fx_c11
  fx_c12:	db '@transitus', 10
		db 'structura W {', 10
		db 9, 'h: u4', 10
		db 9, 'l: u4', 10
		db 9, 'n: u16:maior', 10
		db 9, 'm: u8', 10
		db '}', 10
		db 'structura L {', 10
		db 9, 'x: u16', 10
		db 9, 'z: u16', 10
		db '}', 10
		db 'functio f(y: u16) -> u8 {', 10
		db 9, 'firma v = W { h: 16, l: 2, n: y, m: 3 };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c12_LEN = $ - fx_c12
  fx_c13:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(x: u8, y: u16) -> u8 {', 10
		db 9, 'firma s = S { a: x, z: 1, a: 2, b: y };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c13_LEN = $ - fx_c13
  fx_a01:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(x: u8, y: u16) -> u8 {', 10
		db 9, 'firma s = S { a: x, b: y };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a01_LEN = $ - fx_a01
  fx_a02:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(x: u8, y: u16) -> u8 {', 10
		db 9, 'firma s = S { b: y, a: x };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a02_LEN = $ - fx_a02
  fx_a03:	db '@transitus', 10
		db 'structura W {', 10
		db 9, 'h: u4', 10
		db 9, 'l: u4', 10
		db 9, 'n: u16:maior', 10
		db 9, 'm: u8', 10
		db '}', 10
		db 'structura L {', 10
		db 9, 'x: u16', 10
		db 9, 'z: u16', 10
		db '}', 10
		db 'functio f(y: u16) -> u8 {', 10
		db 9, 'firma v = W { h: 1, l: 2, n: y, m: 3 };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a03_LEN = $ - fx_a03
  fx_a04:	db '@transitus', 10
		db 'structura W {', 10
		db 9, 'h: u4', 10
		db 9, 'l: u4', 10
		db 9, 'n: u16:maior', 10
		db 9, 'm: u8', 10
		db '}', 10
		db 'structura L {', 10
		db 9, 'x: u16', 10
		db 9, 'z: u16', 10
		db '}', 10
		db 'functio f() -> u8 {', 10
		db 9, 'firma v = W { h: 15, l: 0, n: 0x1234, m: 255 };', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a04_LEN = $ - fx_a04
  fx_a05:	db 'structura S {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u16', 10
		db '}', 10
		db 'functio f(x: u8, y: u16) -> u8 {', 10
		db 9, 'redde (S { a: x, b: y }).a;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a05_LEN = $ - fx_a05

  fx_tab:
	; c01: 301 at 'z: 1'
	dq fx_c01, fx_c01_LEN
	dd 1, 301, 85, 0, 0, 0
	; c02: 302 at 'a: 2'
	dq fx_c02, fx_c02_LEN
	dd 1, 302, 85, 0, 0, 0
	; c03: 304 at 'S { a'
	dq fx_c03, fx_c03_LEN
	dd 1, 304, 67, 0, 0, 0
	; c04: 304 at 'S { }'
	dq fx_c04, fx_c04_LEN
	dd 1, 304, 67, 0, 0, 0
	; c05: 303 at 'q }'
	dq fx_c05, fx_c05_LEN
	dd 1, 303, 88, 0, 0, 0
	; c06: 308 at '256'
	dq fx_c06, fx_c06_LEN
	dd 1, 308, 75, 0, 0, 0
	; c07: 305 at 'f {'
	dq fx_c07, fx_c07_LEN
	dd 1, 305, 68, 0, 0, 0
	; c08: 305 at 'x {'
	dq fx_c08, fx_c08_LEN
	dd 1, 305, 75, 0, 0, 0
	; c09: 305 at 'mensura {'
	dq fx_c09, fx_c09_LEN
	dd 1, 305, 68, 0, 0, 0
	; c10: 301 at 'Nemo'
	dq fx_c10, fx_c10_LEN
	dd 1, 301, 68, 0, 0, 0
	; c11: 303 at 'q, m'
	dq fx_c11, fx_c11_LEN
	dd 1, 303, 150, 0, 0, 0
	; c12: 308 at '16, l'
	dq fx_c12, fx_c12_LEN
	dd 1, 308, 138, 0, 0, 0
	; c13: 301 at 'z: 1'; 302 at 'a: 2'
	dq fx_c13, fx_c13_LEN
	dd 2, 301, 85, 302, 91, 0
	; a01: accepted
	dq fx_a01, fx_a01_LEN
	dd 0, 0, 0, 0, 0, 0
	; a02: accepted
	dq fx_a02, fx_a02_LEN
	dd 0, 0, 0, 0, 0, 0
	; a03: accepted
	dq fx_a03, fx_a03_LEN
	dd 0, 0, 0, 0, 0, 0
	; a04: accepted
	dq fx_a04, fx_a04_LEN
	dd 0, 0, 0, 0, 0, 0
	; a05: accepted
	dq fx_a05, fx_a05_LEN
	dd 0, 0, 0, 0, 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW
  assert ($ - fx_tab) mod FX_ROW = 0
  ; ==== END ROWS ====

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
