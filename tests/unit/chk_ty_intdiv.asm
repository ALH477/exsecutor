; tests/unit/chk_ty_intdiv.asm
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
; checker fixture -- the TYPING of integer `/` and `residuum` (spec §5.4 as
; amended 2026-09-25), from REAL SOURCE through the whole front end to
; `chk_run`: checker/types/types.inc's `__chk_ty_divadm`, its `.div` and `.rem`
; arms, and `__chk_ty_expect`'s `.bin_div` for the pending case.
;
; WHAT THIS FIXTURE IS FOR. §5.4 gave `/` an integer reading and `residuum` its
; first one, and the spec cites this file as the evidence for the TYPING half.
; The VALUES are not tested here and are not this pass's business: truncation
; toward zero, the dividend's sign, and the two traps are properties of the
; emitted code (backend_fasmg/emit.inc's `__bfa_emit_divrem`, which ADR 0012
; makes the definition; tests/ir/divrem.ir, rem_sign.ir, trap_div_zero.ir,
; trap_rem_zero.ir, trap_div_minneg1.ir, trap_rem_minneg1.ir), and from source
; by tests/programs/numerus_decimalis/. NOTHING HERE DIAGNOSES A ZERO DIVISOR:
; a divisor is an expression, this pass has no value for one, and a rule that
; caught `x residuum 0` while missing `x residuum (y - y)` would be a rule the
; type system cannot keep.
;
; THE ONE PLACE `/` AND `residuum` DIFFER, and the reason a single fixture
; covers both: `/` admits a float operand pair and `residuum` does not. §5.4
; defines the remainder as the integer one -- "the remainder, of type `T`, with
; the sign of the dividend", in the paragraph that names `uN`, `iN` and
; `mensura` -- the IR has no `frem` opcode for it to lower to, and a float
; remainder that truncated its operands would be an invention rather than a
; reading. So rows d04 and d05 are `f64 residuum f64` and `f32 residuum f32`,
; both EXS-E0305 at the operator, beside e08's accepted `f64 / f64`. That
; asymmetry is `__chk_ty_divadm`'s whole content, and swapping either half
; fails one of those three rows.
;
; EVERY REJECTED ROW PINS THE COUNT, THE CODE AND THE OFFSET, and every rule
; has an accepted twin, so a checker that rejected everything fails the twins
; and one that accepted everything fails the rest. Offsets were read off the
; real compiler's JSON spans (`--diagnostica json`, `span.start`) on these
; exact sources, not counted by hand.
;
;   rule                                       rejected              accepted
;   one type both sides (E0303)                u8 / u16 (d01)        u8 / u8 (e01)
;     ... and for the remainder                u8 residuum u16 (d02) u8 residuum u8 (e02)
;     ... signedness is part of the type       i8 / u8 (d03)         i8 / i8 (e03)
;     ... mensura is its own type              mensura residuum u64 (d11) mensura / mensura (e05)
;     ... A FLOAT AND AN INTEGER DO NOT MIX    f64 / u8 (d07)
;   the result is the operand type             u8 / u8 -> u16 (d09)  u8 / u8 -> u8 (e01)
;     ... and for the remainder                u8 residuum u8 -> u16 (d10)
;   `residuum` is INTEGER-ONLY (E0305)         f64 residuum f64 (d04) f64 / f64 (e08)
;     ... at both float widths                 f32 residuum f32 (d05)
;     ... an EXPECTATION is judged too         1.5 residuum 2.5 -> f64 (d06)
;   neither is defined on a non-number         textus / textus (d08)
;   an `acies` is decided upstream             acies<f64,4> residuum (d13) acies<f64,4> / (d14)
;   a literal takes the other side's type      u16 residuum 65536 (d12) u16 residuum 65535 (e14)
;   both sides pending, settled by the
;     expectation                              --                    250 / 10 -> u8 (e12)
;                                                                    250 residuum 10 -> u8 (e13)
;
; d07 IS §8.4'S "a float and an integer do not mix", at this level. Note WHICH
; code it is: `EXS-E0303`, not `EXS-E0305`. The pairing rule runs before either
; operand arm, and once `f64` and `u8` have failed to be one type neither is
; wrong on its own -- so the diagnostic is the type mismatch, at the right
; operand. `tests/unit/chk_ty_floatops.asm` row 7 is the same claim from the
; other side (two float LITERALS under a `u8` expectation: 2 x E0308, the
; literal-class rule), and between them the refusal §8.4 cites is pinned at
; both the operand and the literal layer. Neither is E0305 any more, and that
; is the amendment: the operand KIND stopped being what is wrong with a `/`.
;
; d13 AND d14 ARE A PAIR, not two rows. `__chk_ty_binary`'s `.settled` routes
; an `acies` operand to `__chk_ty_aciesbin` BEFORE the pairing rule, and that
; routine's operator list is `+ - * /` on a float element. So an acies `/` is
; ADMITTED (d14, clean -- the elementwise quotient stage 5.2 settled) and an
; acies `residuum` is EXS-E0305 AT THE OPERATOR (d13). `__chk_ty_divadm` never
; sees either one, and a checker that widened the acies list to include the
; remainder would fail d13 while leaving every other row green.
;
; e07 IS `u1 / u1`, the narrowest width the language has, accepted -- the same
; row tests/ir/divrem.ir's check 17 computes at the opcode layer.
;
; THE LAST CHECK READS THE TYPES, not the diagnostics: in the accepted
; `c residuum 10` with `c: u16`, the literal's `Node.ty` and the `Binary`'s
; must be one id, and that id must be `u16` -- kind int, unsigned, width 16.
; "No diagnostic" alone would also be the answer of a checker that left the
; literal pending and never looked, and §5.4's "a pending literal takes the
; other side's type" is a claim about the TYPE and not about silence. e10
; (`10 / c`) is the same rule with the pending side on the LEFT, so the
; adoption is pinned in both directions.
;
; WHAT THIS FIXTURE WOULD HAVE SAID BEFORE THE AMENDMENT: every accepted
; integer row here was `EXS-E0305` at the operator, because `.div` demanded
; `AST_TY_FLOAT` and no `.rem` arm existed at all -- a settled `residuum`
; tripped a `rassert` and aborted the compiler with exit 132. Measured on the
; pre-amendment compiler, 2026-09-26, which is why this file could not be
; written when §5.4 first cited it.
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 201 = the literal is not the Binary's
; type, 202 = that type is not u16; 99 = setup.
;
; 201/202 AND NOT 41/42, which is what the twin fixture used until this wave:
; `10+N` over 28 rows reaches 38, and 41 was three rows away from colliding
; with the type check -- so tests/unit/chk_ty_bitops.asm, whose table this
; wave took to 34 rows, actually did collide and both files were renumbered
; together. A fixture's failure code has to name what failed.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.4, §8.4, §8.6, §13.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 40		; dq src, len; dd count, code0, off0, code1, off1, pad
FX_ANY = -1		; "do not check this offset" -- unused by this file's
			; table on purpose: every offset below is a measured
			; span, and a row that could not name one would be a
			; row whose diagnostic nobody had looked at

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

	; ---- the pending literal takes the other side's type ------------------
	; `redde c residuum 10;` with `c: u16`. §5.4's rule for `+` applies to
	; the quotient and the remainder alike, and this reads the TYPES rather
	; than trusting the silence of row e09.
	lea	rdi, [fx_e09]
	mov	rsi, fx_e09_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail41
	mov	edi, AST_LIT
	call	fx_first_ty
	mov	r12, rax
	mov	edi, AST_BINARY
	call	fx_first_ty
	cmp	rax, r12
	jne	.fail41
	lea	rdi, [fx_tree]
	mov	rsi, r12
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_INT
	jne	.fail42
	cmp	byte [rax + AstType.sign], AST_SIGN_U
	jne	.fail42
	cmp	dword [rax + AstType.width], 16
	jne	.fail42

	xor	edi, edi
	call	sys_exit_group
  .fail41:
	mov	edi, 201
	call	sys_exit_group
  .fail42:
	mov	edi, 202
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

  fx_path	db 'chk_ty_intdiv.exsc'
  FX_PATH_LEN = $ - fx_path

  ; Offsets are into each row's own source; line 1 ends with its `\n`, and
  ; `redde` is indented four spaces. Every one below was read off
  ; `exsc --diagnostica json`'s `span.start` on that exact source.
  macro fx_src name, line1, line2
	name: db line1, 10, '    ', line2, 10, '}', 10
	name#_LEN = $ - name
  end macro

  ; ---- refused ----
  fx_src fx_d01, 'publica functio f(a: u8, b: u16) -> u8 {',                       'redde a / b;'
  fx_src fx_d02, 'publica functio f(a: u8, b: u16) -> u8 {',                       'redde a residuum b;'
  fx_src fx_d03, 'publica functio f(a: i8, b: u8) -> i8 {',                        'redde a / b;'
  fx_src fx_d04, 'publica functio f(a: f64, b: f64) -> f64 {',                     'redde a residuum b;'
  fx_src fx_d05, 'publica functio f(a: f32, b: f32) -> f32 {',                     'redde a residuum b;'
  fx_src fx_d06, 'publica functio f() -> f64 {',                                   'redde 1.5 residuum 2.5;'
  fx_src fx_d07, 'publica functio f(a: f64, b: u8) -> f64 {',                      'redde a / b;'
  fx_src fx_d08, 'publica functio f(a: textus, b: textus) -> u8 {',                'redde a / b;'
  fx_src fx_d09, 'publica functio f(a: u8, b: u8) -> u16 {',                       'redde a / b;'
  fx_src fx_d10, 'publica functio f(a: u8, b: u8) -> u16 {',                       'redde a residuum b;'
  fx_src fx_d11, 'publica functio f(n: mensura, x: u64) -> mensura {',             'redde n residuum x;'
  fx_src fx_d12, 'publica functio f(c: u16) -> u16 {',                             'redde c residuum 65536;'
  fx_src fx_d13, 'publica functio f(a: acies<f64, 4>, b: acies<f64, 4>) -> acies<f64, 4> {', 'redde a residuum b;'

  ; ---- accepted ----
  fx_src fx_d14, 'publica functio f(a: acies<f64, 4>, b: acies<f64, 4>) -> acies<f64, 4> {', 'redde a / b;'
  fx_src fx_e01, 'publica functio f(a: u8, b: u8) -> u8 {',                        'redde a / b;'
  fx_src fx_e02, 'publica functio f(a: u8, b: u8) -> u8 {',                        'redde a residuum b;'
  fx_src fx_e03, 'publica functio f(a: i8, b: i8) -> i8 {',                        'redde a / b;'
  fx_src fx_e04, 'publica functio f(a: i8, b: i8) -> i8 {',                        'redde a residuum b;'
  fx_src fx_e05, 'publica functio f(n: mensura, d: mensura) -> mensura {',         'redde n / d;'
  fx_src fx_e06, 'publica functio f(n: mensura, d: mensura) -> mensura {',         'redde n residuum d;'
  fx_src fx_e07, 'publica functio f(a: u1, b: u1) -> u1 {',                        'redde a / b;'
  fx_src fx_e08, 'publica functio f(a: f64, b: f64) -> f64 {',                     'redde a / b;'
  fx_src fx_e09, 'publica functio f(c: u16) -> u16 {',                             'redde c residuum 10;'
  fx_src fx_e10, 'publica functio f(c: u16) -> u16 {',                             'redde 10 / c;'
  fx_src fx_e11, 'publica functio f(a: u64, b: u64) -> u64 {',                     'redde a residuum b;'
  fx_src fx_e12, 'publica functio f() -> u8 {',                                    'redde 250 / 10;'
  fx_src fx_e13, 'publica functio f() -> u8 {',                                    'redde 250 residuum 10;'
  fx_src fx_e14, 'publica functio f(c: u16) -> u16 {',                             'redde c residuum 65535;'

  fx_tab:
	; u8 / u16: one type both sides, E0303 at the right operand
	dq fx_d01, fx_d01_LEN
	dd 1, 303, 55, 0, 0, 0
	; u8 residuum u16: the same rule for the remainder
	dq fx_d02, fx_d02_LEN
	dd 1, 303, 62, 0, 0, 0
	; i8 / u8: signedness is part of the type, so these are two types
	dq fx_d03, fx_d03_LEN
	dd 1, 303, 54, 0, 0, 0
	; f64 residuum f64: INTEGER-ONLY, E0305 at the operator
	dq fx_d04, fx_d04_LEN
	dd 1, 305, 53, 0, 0, 0
	; f32 residuum f32: at both float widths
	dq fx_d05, fx_d05_LEN
	dd 1, 305, 53, 0, 0, 0
	; 1.5 residuum 2.5 under an f64 expectation: the pending case, judged
	; by `__chk_ty_expect`'s `.bin_div` before either literal settles
	dq fx_d06, fx_d06_LEN
	dd 1, 305, 39, 0, 0, 0
	; f64 / u8: §8.4's "a float and an integer do not mix" -- and it is
	; E0303, at the right operand, not E0305 at the operator
	dq fx_d07, fx_d07_LEN
	dd 1, 303, 56, 0, 0, 0
	; textus / textus: not a number, E0305 at the operator
	dq fx_d08, fx_d08_LEN
	dd 1, 305, 58, 0, 0, 0
	; u8 / u8 is u8, so returning it as u16 is E0303 at the expression
	dq fx_d09, fx_d09_LEN
	dd 1, 303, 51, 0, 0, 0
	; and the remainder's result is the operand type too
	dq fx_d10, fx_d10_LEN
	dd 1, 303, 51, 0, 0, 0
	; mensura residuum u64: two unsigned integers, two types
	dq fx_d11, fx_d11_LEN
	dd 1, 303, 72, 0, 0, 0
	; u16 residuum 65536: the literal took u16 and does not fit it
	dq fx_d12, fx_d12_LEN
	dd 1, 308, 56, 0, 0, 0
	; acies<f64,4> residuum: refused upstream by __chk_ty_aciesbin, whose
	; operator list is `+ - * /` -- E0305 at the operator
	dq fx_d13, fx_d13_LEN
	dd 1, 305, 83, 0, 0, 0
	; ---- the accepted twins ----
	; acies<f64,4> / : ADMITTED, elementwise (stage 5.2). The pair with d13
	dq fx_d14, fx_d14_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_e01, fx_e01_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_e02, fx_e02_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_e03, fx_e03_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_e04, fx_e04_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_e05, fx_e05_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_e06, fx_e06_LEN
	dd 0, 0, 0, 0, 0, 0
	; u1 / u1 -- the narrowest width, as tests/ir/divrem.ir check 17
	dq fx_e07, fx_e07_LEN
	dd 0, 0, 0, 0, 0, 0
	; f64 / f64 -- the float quotient the float wave settled, UNCHANGED
	dq fx_e08, fx_e08_LEN
	dd 0, 0, 0, 0, 0, 0
	; u16 residuum 10 -- the literal adopts u16 (also the type check above)
	dq fx_e09, fx_e09_LEN
	dd 0, 0, 0, 0, 0, 0
	; 10 / c -- the same adoption with the pending side on the LEFT
	dq fx_e10, fx_e10_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_e11, fx_e11_LEN
	dd 0, 0, 0, 0, 0, 0
	; both sides pending, settled by the u8 expectation
	dq fx_e12, fx_e12_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_e13, fx_e13_LEN
	dd 0, 0, 0, 0, 0, 0
	; u16 residuum 65535 -- the literal fits
	dq fx_e14, fx_e14_LEN
	dd 0, 0, 0, 0, 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW
  ; a row of the wrong width would shift every row after it
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
