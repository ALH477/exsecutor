; tests/unit/chk_ty_aciesops.asm
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
; checker fixture -- STAGE 5.2a's whole-acies arithmetic admission, from REAL
; SOURCE, through the whole front end to `chk_run`. The settlement: `a OP b`
; on two `acies<f32, N>` / `acies<f64, N>` of the SAME element type and the
; SAME lane count N in {2, 4, 8} type-checks as the whole-acy elementwise op
; -- result the same acies -- for OP in `+ - * /` and for NOTHING else.
; Everything else is EXS-E0305 AT THE OPERATOR (`__chk_ty_aciesbin`,
; checker/types/types.inc, reached from `__chk_ty_binary`'s `.settled` arm
; BEFORE the §4.5 pairing rule, since a mismatched pair is two interned ids
; and would otherwise report E0303 at the right operand):
;
;   - a lane-count outside {2, 4, 8}             (`acies<f32, 3>`)
;   - a lane-count mismatch                      (f32,4 with f32,8)
;   - an element-type mismatch                   (f32,8 with f64,8)
;   - a non-float element                        (`acies<u8, 4>` `acies<i64, 8>`)
;   - a MIXED acies/scalar pair, NO implicit broadcast -- `[s; N]` is the
;     broadcast story (declared both-settled, and a still-PENDING scalar
;     literal: the latter is refused in the adoption step, before
;     `__chk_ty_expect` could misreport it as E0308 at the literal)
;   - any other OPERATOR on any acies: the wrapped `+%` (which slipped
;     through `.arith`'s float-only `.wchk` as plain addition until this
;     wave) and the relational `lt` are the pins.
;
; A bare `%` never reaches the checker: the lexer has no token for it (its
; `%` arms make the two-character `+%` `-%`), so `a % b` is a FRONT-END
; refusal -- pinned as such after the table, not as an E0305.
;
; Rows pin the diagnostic COUNT and, for refused rows, the code AND the
; span start -- the operator's NODE's span (`a + b`, not the operator
; glyph), which is where a raise at the node lands and which matches
; chk_ty_floatops.asm's E0305 renderings exactly; confirmed by running the
; same rows through `exsc aedifica ... --diagnostica json` and reading the
; `span.start` it reports. Every rule has an accepted twin.
;
;   1-24   the admission table: f32/f64 x N in {2,4,8} x `+ - * /`, clean
;   25     `acies<f32, 3>` + itself                E0305 at `+`
;   26     `acies<f32, 4>` + `acies<f32, 8>`       E0305 at `+`
;   27     `acies<f32, 8>` + `acies<f64, 8>`       E0305 at `+`
;   28     `acies<u8, 4>` + itself                 E0305 at `+`
;   29     `acies<i64, 8>` + itself                E0305 at `+`
;   30     `acies<f32, 8>` + `f32`                 E0305 at `+`
;   31     `acies<f32, 8>` + `1.5` (pending)       E0305 at `+`
;   32     `+%` on `acies<f32, 8>`                 E0305 at `+%`
;   33     `lt` on `acies<f32, 4>`                 E0305 at `lt`
;   34     `firma v: acies<f64, 8> = [0.5; 8];`    clean -- the broadcast
;                                                  literal, untouched
;   35     `redde -a;` on `acies<f32, 4>`          E0305 at `-a` -- unary
;                                                  minus is an operator and
;                                                  the IR has no vector-negate
;                                                  opcode (settlement's "+ - * /
;                                                  only" covers both arms)
;   36     `redde -x;` on a scalar `f32`           clean -- the regression
;                                                  row: the unary gate did not
;                                                  swallow the scalar one
;
; After the table: the RESULT-TYPE check -- `firma r = a + b;` must give the
; binary `acies<f32, 4>`, the same type as its operands (no diagnostic alone
; would also be the answer of a checker that left the node untyped); and the
; `%` pin: `redde a % b;` on two `acies<f32, 8>` must never reach `chk_run`
; (fx_run answers -1 -- the refusal is the parser's).
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics
; the row did produce are rendered first); 41 = the result-type source did
; not check clean or has no binary, 42 = its type is not `acies<f32, 4>`,
; 43 = its element type is not `f32`; 50 = the `%` source reached the
; checker; 99 = setup.
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

	; ---- the result type: `r = a + b` is `acies<f32, 4>` ------------------
	lea	rdi, [fx_res]
	mov	rsi, fx_res_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail41
	mov	edi, AST_BINARY
	call	fx_first_ty
	test	rax, rax
	jz	.fail41
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_ACIES
	jne	.fail42
	cmp	dword [rax + AstType.width], 4
	jne	.fail42
	mov	esi, [rax + AstType.a]
	lea	rdi, [fx_tree]
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_FLOAT
	jne	.fail43
	cmp	dword [rax + AstType.width], 32
	jne	.fail43

	; ---- the `%` pin: refused before the checker --------------------------
	lea	rdi, [fx_mod]
	mov	rsi, fx_mod_LEN
	call	fx_run
	cmp	rax, -1
	jne	.fail50

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
  .fail50:
	mov	edi, 50
	call	sys_exit_group
  .fail99:
	mov	edi, 99
	call	sys_exit_group


include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; ---- harness ---------------------------------------------------------------
; The same chain chk_ty_acies.asm drives. Plain labels (a `proc` argument
; name is an unmangled global); each helper pushes an odd number of
; registers, so `rsp` is 16-aligned at every call inside it.

; fx_run(rdi = source, rsi = length) -> rax = the diagnostics `chk_run`
; appended; -1 if the lexer, the parser or the AST fold said anything (the
; row's refusal is then a FRONT-END refusal -- only the `%` tail check
; expects that; every table row pins a CHECKER answer).
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

  fx_path	db 'chk_ty_aciesops.exsc'
  FX_PATH_LEN = $ - fx_path

  macro fx_src name, str&
	name:	db str
	name#_LEN = $ - name
  end macro

  ; every row's WHOLE source goes through one invocation -- `str&` collects
  ; it all, and `_LEN` then covers it all (chk_ty_floatops.asm, found by
  ; running: a source split across a macro call and bare `db` lines measures
  ; only the macro's part)

  ; ---- the admission table: f32/f64 x N in {2,4,8} x `+ - * /` -------------
  ; f32, 2 lanes
  fx_src fx_s01, 'functio f() -> acies<f32, 2> {', 10, 9, 'firma a: acies<f32, 2> = [1.5; 2];', 10, 9, 'firma b: acies<f32, 2> = [2.5; 2];', 10, 9, 'redde a + b;', 10, '}', 10
  fx_src fx_s02, 'functio f() -> acies<f32, 2> {', 10, 9, 'firma a: acies<f32, 2> = [1.5; 2];', 10, 9, 'firma b: acies<f32, 2> = [2.5; 2];', 10, 9, 'redde a - b;', 10, '}', 10
  fx_src fx_s03, 'functio f() -> acies<f32, 2> {', 10, 9, 'firma a: acies<f32, 2> = [1.5; 2];', 10, 9, 'firma b: acies<f32, 2> = [2.5; 2];', 10, 9, 'redde a * b;', 10, '}', 10
  fx_src fx_s04, 'functio f() -> acies<f32, 2> {', 10, 9, 'firma a: acies<f32, 2> = [1.5; 2];', 10, 9, 'firma b: acies<f32, 2> = [2.5; 2];', 10, 9, 'redde a / b;', 10, '}', 10
  ; f32, 4 lanes
  fx_src fx_s05, 'functio f() -> acies<f32, 4> {', 10, 9, 'firma a: acies<f32, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f32, 4> = [2.5; 4];', 10, 9, 'redde a + b;', 10, '}', 10
  fx_src fx_s06, 'functio f() -> acies<f32, 4> {', 10, 9, 'firma a: acies<f32, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f32, 4> = [2.5; 4];', 10, 9, 'redde a - b;', 10, '}', 10
  fx_src fx_s07, 'functio f() -> acies<f32, 4> {', 10, 9, 'firma a: acies<f32, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f32, 4> = [2.5; 4];', 10, 9, 'redde a * b;', 10, '}', 10
  fx_src fx_s08, 'functio f() -> acies<f32, 4> {', 10, 9, 'firma a: acies<f32, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f32, 4> = [2.5; 4];', 10, 9, 'redde a / b;', 10, '}', 10
  ; f32, 8 lanes
  fx_src fx_s09, 'functio f() -> acies<f32, 8> {', 10, 9, 'firma a: acies<f32, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f32, 8> = [2.5; 8];', 10, 9, 'redde a + b;', 10, '}', 10
  fx_src fx_s10, 'functio f() -> acies<f32, 8> {', 10, 9, 'firma a: acies<f32, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f32, 8> = [2.5; 8];', 10, 9, 'redde a - b;', 10, '}', 10
  fx_src fx_s11, 'functio f() -> acies<f32, 8> {', 10, 9, 'firma a: acies<f32, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f32, 8> = [2.5; 8];', 10, 9, 'redde a * b;', 10, '}', 10
  fx_src fx_s12, 'functio f() -> acies<f32, 8> {', 10, 9, 'firma a: acies<f32, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f32, 8> = [2.5; 8];', 10, 9, 'redde a / b;', 10, '}', 10
  ; f64, 2 lanes
  fx_src fx_s13, 'functio f() -> acies<f64, 2> {', 10, 9, 'firma a: acies<f64, 2> = [1.5; 2];', 10, 9, 'firma b: acies<f64, 2> = [2.5; 2];', 10, 9, 'redde a + b;', 10, '}', 10
  fx_src fx_s14, 'functio f() -> acies<f64, 2> {', 10, 9, 'firma a: acies<f64, 2> = [1.5; 2];', 10, 9, 'firma b: acies<f64, 2> = [2.5; 2];', 10, 9, 'redde a - b;', 10, '}', 10
  fx_src fx_s15, 'functio f() -> acies<f64, 2> {', 10, 9, 'firma a: acies<f64, 2> = [1.5; 2];', 10, 9, 'firma b: acies<f64, 2> = [2.5; 2];', 10, 9, 'redde a * b;', 10, '}', 10
  fx_src fx_s16, 'functio f() -> acies<f64, 2> {', 10, 9, 'firma a: acies<f64, 2> = [1.5; 2];', 10, 9, 'firma b: acies<f64, 2> = [2.5; 2];', 10, 9, 'redde a / b;', 10, '}', 10
  ; f64, 4 lanes
  fx_src fx_s17, 'functio f() -> acies<f64, 4> {', 10, 9, 'firma a: acies<f64, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f64, 4> = [2.5; 4];', 10, 9, 'redde a + b;', 10, '}', 10
  fx_src fx_s18, 'functio f() -> acies<f64, 4> {', 10, 9, 'firma a: acies<f64, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f64, 4> = [2.5; 4];', 10, 9, 'redde a - b;', 10, '}', 10
  fx_src fx_s19, 'functio f() -> acies<f64, 4> {', 10, 9, 'firma a: acies<f64, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f64, 4> = [2.5; 4];', 10, 9, 'redde a * b;', 10, '}', 10
  fx_src fx_s20, 'functio f() -> acies<f64, 4> {', 10, 9, 'firma a: acies<f64, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f64, 4> = [2.5; 4];', 10, 9, 'redde a / b;', 10, '}', 10
  ; f64, 8 lanes
  fx_src fx_s21, 'functio f() -> acies<f64, 8> {', 10, 9, 'firma a: acies<f64, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f64, 8> = [2.5; 8];', 10, 9, 'redde a + b;', 10, '}', 10
  fx_src fx_s22, 'functio f() -> acies<f64, 8> {', 10, 9, 'firma a: acies<f64, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f64, 8> = [2.5; 8];', 10, 9, 'redde a - b;', 10, '}', 10
  fx_src fx_s23, 'functio f() -> acies<f64, 8> {', 10, 9, 'firma a: acies<f64, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f64, 8> = [2.5; 8];', 10, 9, 'redde a * b;', 10, '}', 10
  fx_src fx_s24, 'functio f() -> acies<f64, 8> {', 10, 9, 'firma a: acies<f64, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f64, 8> = [2.5; 8];', 10, 9, 'redde a / b;', 10, '}', 10

  ; row 25 -- a lane count outside {2, 4, 8}
  fx_src fx_s25, 'functio f() -> acies<f32, 3> {', 10, 9, 'firma a: acies<f32, 3> = [1.5; 3];', 10, 9, 'firma b: acies<f32, 3> = [2.5; 3];', 10, 9, 'redde a + b;', 10, '}', 10

  ; row 26 -- a lane-count mismatch: f32,4 with f32,8 (two interned ids;
  ; E0305 at the operator, NOT the pairing rule's E0303)
  fx_src fx_s26, 'functio f() -> acies<f32, 4> {', 10, 9, 'firma a: acies<f32, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f32, 8> = [2.5; 8];', 10, 9, 'redde a + b;', 10, '}', 10

  ; row 27 -- an element-type mismatch
  fx_src fx_s27, 'functio f() -> acies<f32, 8> {', 10, 9, 'firma a: acies<f32, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f64, 8> = [2.5; 8];', 10, 9, 'redde a + b;', 10, '}', 10

  ; row 28 -- a non-float element
  fx_src fx_s28, 'functio f() -> acies<u8, 4> {', 10, 9, 'firma a: acies<u8, 4> = [1; 4];', 10, 9, 'firma b: acies<u8, 4> = [2; 4];', 10, 9, 'redde a + b;', 10, '}', 10

  ; row 29 -- i64 is not spared either
  fx_src fx_s29, 'functio f() -> acies<i64, 8> {', 10, 9, 'firma a: acies<i64, 8> = [1; 8];', 10, 9, 'firma b: acies<i64, 8> = [2; 8];', 10, 9, 'redde a + b;', 10, '}', 10

  ; row 30 -- a MIXED pair, both settled: no implicit broadcast
  fx_src fx_s30, 'functio f() -> acies<f32, 8> {', 10, 9, 'firma a: acies<f32, 8> = [1.5; 8];', 10, 9, 'firma s: f32 = 2.5;', 10, 9, 'redde a + s;', 10, '}', 10

  ; row 31 -- a MIXED pair, the scalar still PENDING: refused in the
  ; adoption step at the operator, not E0308 at the literal
  fx_src fx_s31, 'functio f() -> acies<f32, 8> {', 10, 9, 'firma a: acies<f32, 8> = [1.5; 8];', 10, 9, 'redde a + 1.5;', 10, '}', 10

  ; row 32 -- the wrapped spelling on an acies: `.wchk` judged a bare float
  ; and this slipped through until this wave
  fx_src fx_s32, 'functio f() -> acies<f32, 8> {', 10, 9, 'firma a: acies<f32, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f32, 8> = [2.5; 8];', 10, 9, 'redde a +% b;', 10, '}', 10

  ; row 33 -- no other operator: `lt` (the comparison WORD, spec §8.6
  ; decision 6 -- `<` is not an operator at all) is not elementwise
  ; comparison here
  fx_src fx_s33, 'functio f() -> u1 {', 10, 9, 'firma a: acies<f32, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f32, 4> = [2.5; 4];', 10, 9, 'redde a lt b;', 10, '}', 10

  ; row 34 -- the broadcast literal `[s; N]`, untouched by the gate
  fx_src fx_s34, 'functio f() -> acies<f64, 8> {', 10, 9, 'firma v: acies<f64, 8> = [0.5; 8];', 10, 9, 'redde v;', 10, '}', 10

  ; row 35 -- UNARY minus on an acies: also an operator the settlement does
  ; not admit, raised in `__chk_ty_expr`'s `.unary` arm (an admitted
  ; Unary(acies) would reach the lowerer typed, and the IR has no vneg)
  fx_src fx_s35, 'functio f() -> acies<f32, 4> {', 10, 9, 'firma a: acies<f32, 4> = [1.5; 4];', 10, 9, 'redde -a;', 10, '}', 10

  ; row 36 -- the scalar twin: a `-x` on `f32` still types clean
  fx_src fx_s36, 'functio f() -> f32 {', 10, 9, 'firma x: f32 = 1.5;', 10, 9, 'redde -x;', 10, '}', 10

  ; the result type check -- `r = a + b` must BE `acies<f32, 4>`
  fx_src fx_res, 'functio f() -> u8 {', 10, 9, 'firma a: acies<f32, 4> = [1.5; 4];', 10, 9, 'firma b: acies<f32, 4> = [2.5; 4];', 10, 9, 'firma r = a + b;', 10, 9, 'redde 0;', 10, '}', 10

  ; `%`: the lexer has no bare-percent token, so this never reaches the
  ; checker -- fx_run must answer -1
  fx_src fx_mod, 'functio f() -> acies<f32, 8> {', 10, 9, 'firma a: acies<f32, 8> = [1.5; 8];', 10, 9, 'firma b: acies<f32, 8> = [2.5; 8];', 10, 9, 'redde a % b;', 10, '}', 10

  ; row: dq source, its length; dd count, code0, off0, code1, off1, pad
  macro fx_row src, count, code0, off0
	dq src, src#_LEN
	dd count, code0, off0, 0, 0, 0
  end macro

  fx_tab:
	fx_row fx_s01, 0, 0, FX_ANY		; f32,2 `+`
	fx_row fx_s02, 0, 0, FX_ANY		; f32,2 `-`
	fx_row fx_s03, 0, 0, FX_ANY		; f32,2 `*`
	fx_row fx_s04, 0, 0, FX_ANY		; f32,2 `/`
	fx_row fx_s05, 0, 0, FX_ANY		; f32,4 `+`
	fx_row fx_s06, 0, 0, FX_ANY		; f32,4 `-`
	fx_row fx_s07, 0, 0, FX_ANY		; f32,4 `*`
	fx_row fx_s08, 0, 0, FX_ANY		; f32,4 `/`
	fx_row fx_s09, 0, 0, FX_ANY		; f32,8 `+`
	fx_row fx_s10, 0, 0, FX_ANY		; f32,8 `-`
	fx_row fx_s11, 0, 0, FX_ANY		; f32,8 `*`
	fx_row fx_s12, 0, 0, FX_ANY		; f32,8 `/`
	fx_row fx_s13, 0, 0, FX_ANY		; f64,2 `+`
	fx_row fx_s14, 0, 0, FX_ANY		; f64,2 `-`
	fx_row fx_s15, 0, 0, FX_ANY		; f64,2 `*`
	fx_row fx_s16, 0, 0, FX_ANY		; f64,2 `/`
	fx_row fx_s17, 0, 0, FX_ANY		; f64,4 `+`
	fx_row fx_s18, 0, 0, FX_ANY		; f64,4 `-`
	fx_row fx_s19, 0, 0, FX_ANY		; f64,4 `*`
	fx_row fx_s20, 0, 0, FX_ANY		; f64,4 `/`
	fx_row fx_s21, 0, 0, FX_ANY		; f64,8 `+`
	fx_row fx_s22, 0, 0, FX_ANY		; f64,8 `-`
	fx_row fx_s23, 0, 0, FX_ANY		; f64,8 `*`
	fx_row fx_s24, 0, 0, FX_ANY		; f64,8 `/`
	fx_row fx_s25, 1, 305, 110		; `acies<f32, 3>` lanes
	fx_row fx_s26, 1, 305, 110		; lanes f32,4 with f32,8
	fx_row fx_s27, 1, 305, 110		; elements f32,8 with f64,8
	fx_row fx_s28, 1, 305, 103		; `acies<u8, 4>`
	fx_row fx_s29, 1, 305, 106		; `acies<i64, 8>`
	fx_row fx_s30, 1, 305, 95		; mixed, both settled
	fx_row fx_s31, 1, 305, 74		; mixed, scalar pending
	fx_row fx_s32, 1, 305, 110		; `+%` on `acies<f32, 8>`
	fx_row fx_s33, 1, 305, 99		; `lt` on `acies<f32, 4>`
	fx_row fx_s34, 0, 0, FX_ANY		; `[0.5; 8]` broadcast
	fx_row fx_s35, 1, 305, 74		; unary `-` on `acies<f32, 4>`
	fx_row fx_s36, 0, 0, FX_ANY		; scalar `-x` -- unchanged
  FX_NROWS = ($ - fx_tab) / FX_ROW
  assert ($ - fx_tab) mod FX_ROW = 0
  assert FX_NROWS = 36

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
