; tests/unit/chk_ty_transitus.asm
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
; checker fixture -- spec §5.2's places (D2), its one aggregate cast (D4) and
; §8.5's range-of-two-literals rule, all `[UNTESTED]` in the spec until this
; runs (docs/design/wire-codec.md), through the whole front end.
;
;   rule                                              rejected         accepted
;   D2: a `u16:maior` field READS as `u16`            `redde w.n` from `redde w.n` from
;                                                     `-> u8` (E0303)  `-> u16`
;   D2: it is WRITTEN with a `u16` (E0303 for u32)    `v.n = q`        `v.n = y`
;   `&` of a @transitus field is E0305, sub-byte or   `&w.n`, `&w.h`   `&w`, `&l.x`
;     not; of the struct, or of a plain struct's
;     field, it is not
;   D4: `@transitus S` <-> `acies<u8, sizeof S>`      N = 5; `acies    `w sicut acies
;     only, both directions, E0305 otherwise          <u16, 2>`; a     <u8, 4>`, `b sicut
;                                                     plain struct;    W`
;                                                     two structs;
;                                                     the same struct
;   D4's field condition, checked by the cast         a `textus` field,
;                                                     an `i8` field
;   the cast's result is a value (E0306)              `(w sicut ..)[0] `a[0] = 1` on a
;                                                     = 1`             bound copy
;   §8.5: `0..8` types `i` as mensura                 `firma j: u8 = i` `j: mensura = i`
;   a pending non-range iterable: ONE E0308           `per i in 3`
;   an explicit `:nativus` in @transitus is E0321     `v: u32:nativus` `:maior`, `:minor`
;
; Row c14 exists because every byte order was stored ONE TOO HIGH until M6
; (checker/types/sig.inc, `__chk_ty_suffix`): an explicit `:nativus` became
; `maior`, so pass 4 never saw a `nativus` to reject. Row c13 exists because
; `__chk_ty_settled` returned junk after its E0308 and the loop then added an
; E0305 (types.inc, that routine's header).
;
; Row c15 expects TWO diagnostics, not one: `structura T { t: textus }` is
; already malformed under §5.2 regardless of the cast (`EXS-E0321`, pass 4 --
; a `textus` field has no fixed width and used to let a width-0 field skip
; pass 4's order check entirely, `checker/rows/layout.inc`'s fix), and the
; `sicut` on it separately fails the cast's own "every field is an unsigned
; integer" condition (`EXS-E0305`, pass 2, checked first by `chk_run`'s pass
; order). Two different rules, over the same field, neither implied by the
; other -- not a double report of one.
;
; THE LAST CHECKS READ TYPES: the `Member` in `fx_a01` must be an unordered
; sixteen-bit integer, and the range in `fx_a08` must be `mensura`.
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 41-43 = the `w.n` read's type (no
; member / not 16 bits / carries an order); 44-45 = the range's type
; (missing / not mensura); 99 = setup.
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

	; ---- D2 at the read: `w.n` on a `u16:maior` field is a `u16` -------
	lea	rdi, [fx_a01]
	mov	rsi, fx_a01_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail41
	mov	edi, AST_MEMBER
	call	fx_first_ty
	test	rax, rax
	jz	.fail41
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_INT
	jne	.fail42
	cmp	dword [rax + AstType.width], 16
	jne	.fail42
	cmp	byte [rax + AstType.order], AST_ORD_NATIVUS
	jne	.fail43

	; ---- spec §8.5: `per i in 0..8` -- the range, and so `i`, is mensura -
	lea	rdi, [fx_a08]
	mov	rsi, fx_a08_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail44
	mov	edi, AST_BINARY
	call	fx_first_ty
	test	rax, rax
	jz	.fail44
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_MENSURA
	jne	.fail45

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
  .fail44:
	mov	edi, 44
	call	sys_exit_group
  .fail45:
	mov	edi, 45
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

  fx_path	db 'chk_ty_transitus.exsc'
  FX_PATH_LEN = $ - fx_path

  ; ==== ROWS (rows.py) ====
  fx_c01:	db '@transitus', 10
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
		db 'functio f(w: W, q: u32) -> u8 {', 10
		db 9, 'mutabilis v = w; v.n = q;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c01_LEN = $ - fx_c01
  fx_c02:	db '@transitus', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'firma p = &w.n;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c02_LEN = $ - fx_c02
  fx_c03:	db '@transitus', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'firma p = &w.h;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c03_LEN = $ - fx_c03
  fx_c04:	db '@transitus', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'firma a = w sicut acies<u8, 5>;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c04_LEN = $ - fx_c04
  fx_c05:	db '@transitus', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'firma a = w sicut acies<u16, 2>;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c05_LEN = $ - fx_c05
  fx_c06:	db '@transitus', 10
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
		db 'functio f(l: L) -> u8 {', 10
		db 9, 'firma a = l sicut acies<u8, 4>;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c06_LEN = $ - fx_c06
  fx_c07:	db '@transitus', 10
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
		db 'functio f(b: acies<u8, 4>) -> u8 {', 10
		db 9, 'firma v = b sicut L;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c07_LEN = $ - fx_c07
  fx_c08:	db '@transitus', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'firma v = w sicut L;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c08_LEN = $ - fx_c08
  fx_c09:	db '@transitus', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'firma v = w sicut W;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c09_LEN = $ - fx_c09
  fx_c10:	db '@transitus', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, '(w sicut acies<u8, 4>)[0] = 1;', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'redde w.n;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c11_LEN = $ - fx_c11
  fx_c12:	db 'functio f() -> u8 {', 10
		db 9, 'per i in 0..8 { firma j: u8 = i; }', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c12_LEN = $ - fx_c12
  fx_c13:	db 'functio f() -> u8 {', 10
		db 9, 'per i in 3 { }', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c13_LEN = $ - fx_c13
  fx_c14:	db '@transitus', 10
		db 'structura N {', 10
		db 9, 'v: u32:nativus', 10
		db '}', 10
		db 'functio f() -> u8 {', 10
		db 9, 'redde 0;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c14_LEN = $ - fx_c14
  fx_c15:	db '@transitus', 10
		db 'structura T {', 10
		db 9, 't: textus', 10
		db '}', 10
		db 'functio f(t: T) -> u8 {', 10
		db 9, 'firma a = t sicut acies<u8, 16>;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c15_LEN = $ - fx_c15
  fx_c16:	db '@transitus', 10
		db 'structura I {', 10
		db 9, 'v: i8', 10
		db '}', 10
		db 'functio f(t: I) -> u8 {', 10
		db 9, 'firma a = t sicut acies<u8, 1>;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c16_LEN = $ - fx_c16
  fx_a01:	db '@transitus', 10
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
		db 'functio f(w: W) -> u16 {', 10
		db 9, 'redde w.n;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a01_LEN = $ - fx_a01
  fx_a02:	db '@transitus', 10
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
		db 'functio f(w: W, y: u16) -> u8 {', 10
		db 9, 'mutabilis v = w; v.n = y;', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'firma p = &w;', 10
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
		db 'functio f(l: L) -> u8 {', 10
		db 9, 'firma p = &l.x;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a04_LEN = $ - fx_a04
  fx_a05:	db '@transitus', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'firma a = w sicut acies<u8, 4>;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a05_LEN = $ - fx_a05
  fx_a06:	db '@transitus', 10
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
		db 'functio f(b: acies<u8, 4>) -> u8 {', 10
		db 9, 'firma v = b sicut W;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a06_LEN = $ - fx_a06
  fx_a07:	db '@transitus', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'mutabilis a = w sicut acies<u8, 4>; a[0] = 1;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a07_LEN = $ - fx_a07
  fx_a08:	db 'functio f() -> u8 {', 10
		db 9, 'per i in 0..8 { firma j: mensura = i; }', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a08_LEN = $ - fx_a08
  fx_a09:	db '@transitus', 10
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
		db 'functio f(w: W) -> u8 {', 10
		db 9, 'redde w.h sicut u8;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a09_LEN = $ - fx_a09
  fx_a10:	db '@transitus', 10
		db 'structura N {', 10
		db 9, 'v: u32:maior', 10
		db 9, 'r: u16:minor', 10
		db '}', 10
		db 'functio f() -> u8 {', 10
		db 9, 'redde 0;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_a10_LEN = $ - fx_a10

  fx_tab:
	; c01: 303 at 'q;'
	dq fx_c01, fx_c01_LEN
	dd 1, 303, 150, 0, 0, 0
	; c02: 305 at '&w'
	dq fx_c02, fx_c02_LEN
	dd 1, 305, 129, 0, 0, 0
	; c03: 305 at '&w'
	dq fx_c03, fx_c03_LEN
	dd 1, 305, 129, 0, 0, 0
	; c04: 305 at 'w sicut'
	dq fx_c04, fx_c04_LEN
	dd 1, 305, 129, 0, 0, 0
	; c05: 305 at 'w sicut'
	dq fx_c05, fx_c05_LEN
	dd 1, 305, 129, 0, 0, 0
	; c06: 305 at 'l sicut'
	dq fx_c06, fx_c06_LEN
	dd 1, 305, 129, 0, 0, 0
	; c07: 305 at 'b sicut'
	dq fx_c07, fx_c07_LEN
	dd 1, 305, 140, 0, 0, 0
	; c08: 305 at 'w sicut'
	dq fx_c08, fx_c08_LEN
	dd 1, 305, 129, 0, 0, 0
	; c09: 305 at 'w sicut'
	dq fx_c09, fx_c09_LEN
	dd 1, 305, 129, 0, 0, 0
	; c10: 306 at '(w sicut'
	dq fx_c10, fx_c10_LEN
	dd 1, 306, 119, 0, 0, 0
	; c11: 303 at 'w.n'
	dq fx_c11, fx_c11_LEN
	dd 1, 303, 125, 0, 0, 0
	; c12: 303 at 'i; }'
	dq fx_c12, fx_c12_LEN
	dd 1, 303, 51, 0, 0, 0
	; c13: 308 at '3 {'
	dq fx_c13, fx_c13_LEN
	dd 1, 308, 30, 0, 0, 0
	; c14: 321 at 'v: u32'
	dq fx_c14, fx_c14_LEN
	dd 1, 321, 26, 0, 0, 0
	; c15: 305 at 't sicut', AND (`checker/rows/layout.inc`'s own fix)
	; 321 at 't: textus' -- `T` itself is already malformed under §5.2
	; ("every field of a `@transitus` type is an unsigned integer...
	; no textus"), independently of whether anything ever casts it. Pass
	; 2 raises 305 first (the cast's own check), pass 4 raises 321
	; second (`chk_run`'s fixed pass order, checker/checker.inc). This
	; row used to accept 305 alone, from when pass 4 let a width-0 field
	; (textus has none) skip its order check entirely -- the same bug
	; `tests/unit/chk_row_layout_kind.asm` pins directly.
	dq fx_c15, fx_c15_LEN
	dd 2, 305, 73, 321, 26, 0
	; c16: 305 at 't sicut'
	dq fx_c16, fx_c16_LEN
	dd 1, 305, 69, 0, 0, 0
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
	; a06: accepted
	dq fx_a06, fx_a06_LEN
	dd 0, 0, 0, 0, 0, 0
	; a07: accepted
	dq fx_a07, fx_a07_LEN
	dd 0, 0, 0, 0, 0, 0
	; a08: accepted
	dq fx_a08, fx_a08_LEN
	dd 0, 0, 0, 0, 0, 0
	; a09: accepted
	dq fx_a09, fx_a09_LEN
	dd 0, 0, 0, 0, 0, 0
	; a10: accepted
	dq fx_a10, fx_a10_LEN
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
