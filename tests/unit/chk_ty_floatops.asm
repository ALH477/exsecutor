; tests/unit/chk_ty_floatops.asm
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
; checker fixture -- this wave's OPERATOR half (spec §5.4 as amended by the
; float wave), from REAL SOURCE, through the whole front end to `chk_run`:
;
;   `/` is the IEEE-754 quotient on FLOAT OPERANDS ONLY. Integer div and
;   rem STAY [OPEN] -- an integer `/` is EXS-E0305, and so is a `/` whose
;   operands never settle to floats (a float-typed initializer on an `u8`
;   firma: the literals fit `u8`'s... no, they are refused; the point is
;   the OPERAND rule, not the literal rule).
;
;   The WRAPPED ops (`+%`, `-%`, ...) are integer-only and always were --
;   spec §5.4: floats have no overflow behaviour, so wrapping is not a
;   thing a float can ask for. A `+%` on SETTLED float firmas and a `+%`
;   on still-PENDING unary-negated floats are both E0305, at two different
;   sites (settled: the arith table lookup; pending: the pending-binop
;   cascade guard), and the INTEGER `+%` STAYS CLEAN in the same fixture --
;   the regression pin that the float arm did not swallow the integer one.
;
;   And the element-admission half (§5.2): `f32`/`f64` are now legal
;   `acies` elements -- a repeated `acies<f64, 4>` initializer is clean --
;   while a FLOAT FIELD of a `@transitus` structura is STILL REFUSED
;   (E0321: wire structs are byte-typed, and a float is not), and a
;   non-integer `acies` LENGTH ARGUMENT is E0304 (the arity check fires
;   first; the initializer then also mismatches the broken type, E0303 --
;   two diagnostics, and the FIRST is pinned).
;
; Rows (expected diagnostic count, and the FIRST diagnostic's code when the
; count is nonzero -- every code was pinned by running the real compiler on
; the same source first):
;
;   `firma q: u8 = 6 / 3;`            1 x E0305 -- integer operands
;   `redde a / b;` (f64 firmas)       0          -- the accepted quotient
;   `redde a / b;` (f32 firmas)       0          -- both widths
;   `redde a +% b;` (f64 firmas)      1 x E0305 -- settled, arith table
;   `redde -a +% a;` (f64 firma)      1 x E0305 -- pending, cascade guard
;   `firma q: u8 = 6 +% 3;`           0          -- integer +% UNCHANGED
;   `firma q: u8 = 1.5 / 2.5;`        1 x E0305 -- non-float expectation
;   `@transitus structura {f32}`      1 x E0321 -- wire structs refuse
;   `acies<f64, 4> = [1.5; 4]`        0          -- float elements ADMITTED
;   `acies<f64, 1.5> = [1.5; 2]`      2, first E0304 -- length must be an
;                                                 integer literal
;
; Exit 0 = every row held; 10+N = row N's diagnostic count; 40+N = row N's
; first code; 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 24		; dq src, srclen; dd expected count, expected first code

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
	jz	.next
	xor	edi, edi		; refused: pin the FIRST code too --
	call	fx_diag_code		; acies<f64, 1.5> raises two, and row 8
	cmp	eax, [r13 + 20]		; must lead with E0304, not E0303
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
; The same chain chk_ty_floatlit.asm drives, minus its one-`Lit` scan -- a
; row here can carry any number of literals. Plain labels (a `proc`
; argument name is an unmangled global); each helper pushes an odd number
; of registers.

; fx_run(rdi = source, rsi = length) -> rax = the diagnostics `chk_run`
; appended, -1 if the lexer, the parser or the AST fold said anything (the
; row's source is then wrong: every refusal this fixture pins is a CHECKER
; refusal, and a front-end diagnostic would stop testing the checker).
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

; fx_diag_code(rdi = i) -> eax = diagnostic `i`'s code number. NOTE, found
; by running (the twin fixture hit this first): the load must be INTO EAX --
; `mov ecx, [rax + ...]` leaves eax holding vec_get's POINTER and the caller
; compares the pointer's low half against the code.
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

  fx_path	db 'chk_ty_floatops.exsc'
  FX_PATH_LEN = $ - fx_path

  macro fx_src name, str&
	name:	db str
	name#_LEN = $ - name
  end macro

  ; every row's WHOLE source goes through one invocation -- `str&` collects
  ; it all, and `_LEN` then covers it all (a source split across a macro
  ; call and bare `db` lines measures only the macro's part, and the checker
  ; reads a file truncated to its first line: found by running)

  ; integer operands: the quotient is not defined on them (spec §5.4 [OPEN])
  fx_src fx_s01, 'functio f() -> u8 {', 10, 9, 'firma q: u8 = 6 / 3;', 10, 9, 'redde 0;', 10, '}', 10

  ; the accepted quotient, f64
  fx_src fx_s02, 'functio f() -> f64 {', 10, 9, 'firma a: f64 = 1.5;', 10, 9, 'firma b: f64 = 2.5;', 10, 9, 'redde a / b;', 10, '}', 10

  ; the accepted quotient, f32 -- both widths
  fx_src fx_s03, 'functio f() -> f32 {', 10, 9, 'firma a: f32 = 1.5;', 10, 9, 'firma b: f32 = 2.5;', 10, 9, 'redde a / b;', 10, '}', 10

  ; wrapped on SETTLED floats: the arith table lookup refuses (§5.4)
  fx_src fx_s04, 'functio f() -> f64 {', 10, 9, 'firma a: f64 = 1.5;', 10, 9, 'firma b: f64 = 2.5;', 10, 9, 'redde a +% b;', 10, '}', 10

  ; wrapped on still-PENDING floats (unary minus): the cascade guard refuses
  fx_src fx_s05, 'functio f() -> f64 {', 10, 9, 'firma a: f64 = 1.5;', 10, 9, 'redde -a +% a;', 10, '}', 10

  ; the INTEGER wrapped op is unchanged -- the regression pin
  fx_src fx_s06, 'functio f() -> u8 {', 10, 9, 'firma q: u8 = 6 +% 3;', 10, 9, 'redde 0;', 10, '}', 10

  ; a `/` whose expectation is not float-typed: E0305, not a literal fit
  ; error -- the literals settle to nothing here, and the OPERAND rule fires
  fx_src fx_s07, 'functio f() -> u8 {', 10, 9, 'firma q: u8 = 1.5 / 2.5;', 10, 9, 'redde 0;', 10, '}', 10

  ; a float field of a @transitus structura: still refused (§5.2) -- the
  ; E0321 the wire codec depends on, now with a float as the refused field
  fx_src fx_s08, '@transitus', 10, 'structura Tela {', 10, 9, 'x: f32', 10, '}', 10, 'functio f() -> u8 {', 10, 9, 'redde 0;', 10, '}', 10

  ; float acies elements are ADMITTED (§5.2 as amended): repeated form
  fx_src fx_s09, 'functio f() -> f64 {', 10, 9, 'firma s: acies<f64, 4> = [1.5; 4];', 10, 9, 'redde s[0];', 10, '}', 10

  ; the acies LENGTH argument must be an integer literal: E0304 first,
  ; then the initializer's E0303 -- two diagnostics, first pinned
  fx_src fx_s10, 'functio f() -> f64 {', 10, 9, 'firma s: acies<f64, 1.5> = [1.5; 2];', 10, 9, 'redde s[0];', 10, '}', 10

  ; row: dq source, its length; dd expected count, expected first code
  macro fx_row src, count, code
	dq src, src#_LEN
	dd count, code
  end macro

  fx_tab:
	fx_row fx_s01, 1, 305	; integer `/`
	fx_row fx_s02, 0, 0	; f64 quotient
	fx_row fx_s03, 0, 0	; f32 quotient
	fx_row fx_s04, 1, 305	; `+%` on settled floats
	fx_row fx_s05, 1, 305	; `+%` on pending floats
	fx_row fx_s06, 0, 0	; integer `+%` -- unchanged
	fx_row fx_s07, 1, 305	; `/` under a non-float expectation
	fx_row fx_s08, 1, 321	; @transitus float field
	fx_row fx_s09, 0, 0	; acies<f64, 4> -- admitted
	fx_row fx_s10, 2, 304	; acies<f64, 1.5> -- E0304, then E0303
  FX_NROWS = ($ - fx_tab) / FX_ROW
  assert ($ - fx_tab) mod FX_ROW = 0
  assert FX_NROWS = 10

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