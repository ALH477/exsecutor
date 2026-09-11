; tests/unit/chk_ty_bitops.asm
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
; checker fixture -- the typing rules of `aut`, `sursum`, `deorsum` (spec
; §5.4, Integers; docs/design/wire-codec.md D1; `[UNTESTED]` in the spec until
; this runs), through the whole front end: checker/types/types.inc's `.bitop`
; arm and `__chk_ty_expect`'s `.bin` arm.
;
; EVERY REJECTED ROW PINS THE COUNT, THE CODE AND THE OFFSET, and every rule
; has an accepted twin, so a checker that rejected everything fails the
; twins and one that accepted everything fails the rest:
;
;   rule                                         rejected         accepted twin
;   unsigned operands only (E0305)               u8 aut i8        u8 aut u8
;     ... a pending literal is not blamed        i8 deorsum 1     u16 sursum 1
;     ... each operand judged on its own         i8 sursum i8 (2)
;     ... a float is not an unsigned integer     u32 aut f32
;     ... an EXPECTATION is judged too           1 aut 2 -> i8    1 aut 2 -> u8
;   one type both sides (E0303)                  u8 aut u16       u1 aut u1
;     ... the shift count included               u16 sursum u8
;     ... mensura is its own type                mensura aut u64  mensura deorsum 1
;   E0305 before E0303                           u8 aut i8 (one diagnostic, the E0305)
;   the result is the operand type               u8 aut u8 -> u16 u8 aut u8 -> u8
;   a literal takes the other side's type        u16 aut 65536    u16 aut 65535
;
; `u8 aut i8` is BOTH the E0305 row and the ordering row: spec §5.4 says it is
; "`EXS-E0305` at the `i8`, not `EXS-E0303`; one diagnostic". A checker that
; paired first would report 303; one that did both would report two.
;
; THE LAST CHECK READS THE TYPES, not the diagnostics: in the accepted
; `c sursum 1` with `c: u16`, the literal's `Node.ty` and the `Binary`'s must
; be one id, and that id must be `u16` -- kind int, unsigned, width 16. "No
; diagnostic" alone would also be the answer of a checker that left the
; literal pending and never looked.
;
; MENSURA. Spec §5.2 calls it "`mensura` (`usize`)" -- an unsigned integer of
; the target's width -- and §5.4's rule is "unsigned operands only", so it is
; admitted (checker/types/types.inc, `__chk_ty_unsigned`, says why at length).
; It remains its own type, so pairing it with `u64` is E0303.
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 41 = the literal is not the Binary's
; type, 42 = that type is not u16; 99 = setup.
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

	; ---- the literal takes the other side's type -------------------------
	lea	rdi, [fx_a01]
	mov	rsi, fx_a01_LEN
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
	mov	edi, 41
	call	sys_exit_group
  .fail42:
	mov	edi, 42
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

  fx_path	db 'chk_ty_bitops.exsc'
  FX_PATH_LEN = $ - fx_path

  ; Offsets are into each row's own source; line 1 ends with its `\n`, and
  ; `redde` is indented four spaces. Computed, then confirmed by the render.
  macro fx_src name, line1, line2
	name: db line1, 10, '    ', line2, 10, '}', 10
	name#_LEN = $ - name
  end macro

  fx_src fx_c01, 'publica functio f(a: u8, b: i8) -> u8 {',            'redde a aut b;'
  fx_src fx_c02, 'publica functio f(a: i8) -> i8 {',                   'redde a deorsum 1;'
  fx_src fx_c03, 'publica functio f(a: i8, b: i8) -> i8 {',            'redde a sursum b;'
  fx_src fx_c04, 'publica functio f(a: u8, b: u16) -> u8 {',           'redde a aut b;'
  fx_src fx_c05, 'publica functio f(a: u16, b: u8) -> u16 {',          'redde a sursum b;'
  fx_src fx_c06, 'publica functio f(x: f32, y: u32) -> u32 {',         'redde y aut x;'
  fx_src fx_c07, 'publica functio f() -> i8 {',                        'redde 1 aut 2;'
  fx_src fx_c08, 'publica functio f(a: u8, b: u8) -> u16 {',           'redde a aut b;'
  fx_src fx_c09, 'publica functio f(n: mensura, x: u64) -> mensura {', 'redde n aut x;'
  fx_src fx_c10, 'publica functio f(c: u16) -> u16 {',                 'redde c aut 65536;'
  fx_src fx_a01, 'publica functio f(c: u16) -> u16 {',                 'redde c sursum 1;'
  fx_src fx_a02, 'publica functio f(c: u16) -> u16 {',                 'redde 1 aut c;'
  fx_src fx_a03, 'publica functio f() -> u8 {',                        'redde 1 aut 2;'
  fx_src fx_a04, 'publica functio f(n: mensura) -> mensura {',         'redde n deorsum 1;'
  fx_src fx_a05, 'publica functio f(c: u16) -> u16 {',                 'redde c aut 65535;'
  fx_src fx_a06, 'publica functio f(a: u1, b: u1) -> u1 {',            'redde a aut b;'
  fx_src fx_a07, 'publica functio f(a: u8, b: u8) -> u8 {',            'redde a aut b;'

  fx_tab:
	; u8 aut i8: E0305 at the `i8` and NOTHING ELSE -- not E0303 (§5.4)
	dq fx_c01, fx_c01_LEN
	dd 1, 305, 56, 0, 0, 0
	; i8 deorsum 1: E0305 at `a`; the literal is not blamed for adopting i8
	dq fx_c02, fx_c02_LEN
	dd 1, 305, 43, 0, 0, 0
	; i8 sursum i8: both operands are wrong on their own -- one each
	dq fx_c03, fx_c03_LEN
	dd 2, 305, 50, 305, 59, 0
	; u8 aut u16: E0303 at the right operand
	dq fx_c04, fx_c04_LEN
	dd 1, 303, 57, 0, 0, 0
	; u16 sursum u8: the shift count is held to the same type
	dq fx_c05, fx_c05_LEN
	dd 1, 303, 61, 0, 0, 0
	; u32 aut f32: a float is not an unsigned integer
	dq fx_c06, fx_c06_LEN
	dd 1, 305, 59, 0, 0, 0
	; 1 aut 2 where i8 is expected: E0305 at the operator's node, once
	dq fx_c07, fx_c07_LEN
	dd 1, 305, 38, 0, 0, 0
	; u8 aut u8 is u8, so returning it as u16 is E0303 at the expression
	dq fx_c08, fx_c08_LEN
	dd 1, 303, 51, 0, 0, 0
	; mensura aut u64: two unsigned integers, two types
	dq fx_c09, fx_c09_LEN
	dd 1, 303, 67, 0, 0, 0
	; u16 aut 65536: the literal took u16 and does not fit it
	dq fx_c10, fx_c10_LEN
	dd 1, 308, 51, 0, 0, 0
	; the accepted twins
	dq fx_a01, fx_a01_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a02, fx_a02_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a03, fx_a03_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a04, fx_a04_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a05, fx_a05_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a06, fx_a06_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a07, fx_a07_LEN
	dd 0, 0, 0, 0, 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW

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
