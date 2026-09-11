; tests/unit/chk_row_layout_src.asm
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
; checker fixture -- PASS 4, LAYOUT, ON REAL SOURCE: every struct field kind
; `checker/rows/layout.inc` can be handed, in and out of `@transitus`, run
; through the whole front end (lexer, parser, AST, `chk_run`'s passes 0-4).
;
; WHY THIS FILE EXISTS. `exsc aedifica --hospes x86_64-linux` on
;
;     structura T { x: Scriptor }
;
; with or without `@transitus`, died with SIGILL (exit 132): `Scriptor` has
; no `AstDecl`, so `checker/types/prim.inc` interns it as an `AST_TY_STRUCT`
; whose `a` is the TAGGED prelude reference `0x80000001`, and
; `__chk_lay_ty`'s `.struc` arm handed that to `__chk_lay_struct` as a
; declaration id -- `__chk_row_g1`'s `vec_get` bounds `rassert` fired. A
; compiler must never crash on user source. `tests/unit/chk_row_layout_kind.asm`
; could not see it: it builds `Decl.ty` BY HAND and never runs pass 2, and its
; "capability-bearing type" row is an `AST_TY_CAP` atom standing in for
; `Scriptor`, never the prelude struct pass 2 actually produces.
;
; WHAT THE SPEC SAYS EACH ROW MEANS:
;
;   - Out of `@transitus`, a struct may hold a `Scriptor`: spec §4.3, "a type
;     with a capability-typed field, TRANSITIVELY, is capability-bearing; the
;     checker computes the mark". `Scriptor` is itself capability-bearing
;     (its `a: ambitus`), so `T` is, with mark {ambitus}. Rows c01, c09
;     check clean; c11/c12 show the mark is really computed -- a caller of
;     `poscit sicut t` with `t: T` that declares nothing is `EXS-E0421`, its
;     twin declaring `ambitus` is clean; and the layout check at the end
;     reads the flag and the offsets themselves.
;   - In `@transitus`, spec §5.2 "The one aggregate cast": *"Every field of a
;     `@transitus` type is an unsigned integer... no `textus`, no `refero`,
;     no capability-bearing type, no `acies`, and no nested struct"*, and
;     *"an unannotated non-integer field is `:nativus` by rule 3 and is
;     `EXS-E0321`"*. So a `Scriptor` field is exactly one `EXS-E0321` (c02),
;     as are `textus`, `acies<u8, 4>` and a nested struct (c03-c05) -- the
;     codes chk_row_layout_kind.asm asserts by hand, here from source.
;   - c06, c07: a nested ONE-`u8` struct and `acies<u8, 1>`. Both CHECKED
;     CLEAN before this fixture: `__chk_lay_struct` exempted any field of
;     width 1-8 from the order check (rule 3's "at or below eight bits"),
;     testing only `width == 0` for "not an integer". Rule 3 is about an
;     integer; the kind now decides, and both are `EXS-E0321`.
;   - c08: `textus:maior` is TWO diagnostics, `EXS-E0309` (pass 2, at the
;     type) then `EXS-E0321` (pass 4, at the field) -- the measurement spec
;     §5.2 records, whose suppression is `[OPEN]` there; pinned, not judged.
;   - c23: the forging direction, `acies<u8, 16> sicut T` with `T` a
;     `@transitus` struct holding a `Scriptor`: `EXS-E0305` (pass 2's own
;     cast condition, spec §5.2: "a struct that could hold a reference or a
;     capability would let `acies<u8, N> sicut S` forge one from bytes"),
;     then pass 4's `EXS-E0321` at the field.
;
; THE CRASH CLASS, every other type kind with no layout of its own, as a
; field: a capability atom (c13/c14), `dyn Iface` (c15/c16), `functio(...)`
; (c17/c18), `eventus<T>` (c19/c20), a generic parameter (c21/c22). Each is
; clean out of `@transitus` and `EXS-E0321` in it, except the generic
; parameter, which is `EXS-E0301` at `X` in both: the resolver binds no
; generic parameter anywhere (`functio f<X>(x: X) -> X` is two `EXS-E0301`
; too), so what a generic field's layout is cannot be asked yet -- `[OPEN]`,
; pinned so the day it resolves this row is revisited rather than silently
; changed. `refero<u8>` is not a row: in type position it is `EXS-E0201`,
; a parse error, before any pass here runs.
;
; EVERY ROW PINS THE COUNT, THE CODE AND THE OFFSET (up to two diagnostics,
; in the order the checker buffered them). The offsets were computed by the
; generator that wrote the table and each one confirmed by `exsc aedifica
; --diagnostica json` on the row's own source.
;
; MUTATIONS (CONTRIBUTING), run against this fixture:
;   1. `layout.inc` `__chk_lay_ty`: delete `bt r12, 31` / `jc .prestruc`, so
;      the tagged `Scriptor` reaches `__chk_lay_struct` again -- this file
;      dies with SIGILL on row c01, exit 132.
;   2. `layout.inc` `__chk_lay_struct`: restore the `width == 0` test in
;      place of the `AST_TY_INT` kind test -- row c06 fails, exit 16.
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 41 = the layout source did not check
; clean, 42 = `T` is not `AST_F_CAPBEAR` or `U` is, 43 = `T`'s size is not 17
; bytes, 44 = `T.x` is not 128 bits at byte 0, 45 = `T.y` is not at byte 16;
; 99 = setup.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.2, §4.3, §5.2, §13.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 40		; dq src, len; dd count, code0, off0, code1, off1, pad

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

	; ---- the layout itself, read back -------------------------------------
	; `T { x: Scriptor, y: u8 }` then `U { n: u8 }`, both clean. Packed
	; (layout.inc's header), so `x` is `prelude/interface.inc`'s
	; `EXS_IFACE_SCRIPTOR_SIZE` = 16 bytes at byte 0, `y` is at byte 16 and
	; `T` is 17 bytes: the size `lower/ty.inc` reads for a `slot`. A layout
	; that sized `Scriptor` as 0 -- what every other unsizable kind gets --
	; would put `y` on top of it.
	lea	rdi, [fx_lay]
	mov	rsi, fx_lay_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail41

	mov	edi, AST_D_STRUCT
	xor	esi, esi
	call	fx_nth_decl		; T
	test	rax, rax
	jz	.fail42
	mov	rbx, rax
	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_decl_at
	test	byte [rax + AstDecl.flags], AST_F_CAPBEAR
	jz	.fail42
	mov	edi, AST_D_STRUCT
	mov	esi, 1
	call	fx_nth_decl		; U -- the twin: not capability-bearing
	test	rax, rax
	jz	.fail42
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_decl_at
	test	byte [rax + AstDecl.flags], AST_F_CAPBEAR
	jnz	.fail42

	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_layout_at
	cmp	dword [rax + AstLayout.off], 17
	jne	.fail43

	mov	edi, AST_D_FIELD
	xor	esi, esi
	call	fx_nth_decl		; T.x
	test	rax, rax
	jz	.fail44
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_layout_at
	cmp	dword [rax + AstLayout.off], 0
	jne	.fail44
	cmp	byte [rax + AstLayout.width], 128
	jne	.fail44

	mov	edi, AST_D_FIELD
	mov	esi, 1
	call	fx_nth_decl		; T.y
	test	rax, rax
	jz	.fail45
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_layout_at
	cmp	dword [rax + AstLayout.off], 16
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
; tests/unit/chk_ty_structlit.asm's, unchanged but for `fx_nth_decl`. Plain
; labels: a `proc` argument name is an unmangled global. Each helper pushes
; an odd number of registers, so `rsp` is 16-aligned at every call inside it.

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

; fx_diag_is(edi = i, esi = code, edx = span start) -> eax = 1 if diagnostic
; `i` has that code at that offset.
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
	cmp	[rax + Diag.span.start], r13d
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

; fx_nth_decl(edi = `AST_D_*` kind, esi = n) -> rax = the id of the n-th
; (0-based) declaration of that kind in the last tree, in declaration order;
; 0 if there is none.
  fx_nth_decl:
	push	rbx
	push	r12
	push	r13
	mov	r12d, edi
	mov	ebx, esi
	mov	r13, 1
  .scan:
	lea	rdi, [fx_tree]
	call	ast_decl_count
	cmp	r13, rax
	ja	.none
	lea	rdi, [fx_tree]
	mov	rsi, r13
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, r12d
	jne	.next
	test	ebx, ebx
	jz	.hit
	dec	ebx
  .next:
	inc	r13
	jmp	.scan
  .hit:
	mov	rax, r13
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

  fx_path	db 'chk_row_layout_src.exsc'
  FX_PATH_LEN = $ - fx_path

  ; the layout check's source: `T` holds a `Scriptor` and a `u8`, `U` only
  ; a `u8`
  fx_lay:	db 'structura T {', 10
		db 9, 'x: Scriptor', 10
		db 9, 'y: u8', 10
		db '}', 10
		db 'structura U {', 10
		db 9, 'n: u8', 10
		db '}', 10
  fx_lay_LEN = $ - fx_lay

  ; ==== ROWS (generated; offsets confirmed by exsc --diagnostica json) ====
  fx_c01:	db 'structura T {', 10
		db 9, 'x: Scriptor', 10
		db '}', 10
  fx_c01_LEN = $ - fx_c01
  fx_c02:	db '@transitus', 10
		db 'structura T {', 10
		db 9, 'x: Scriptor', 10
		db '}', 10
  fx_c02_LEN = $ - fx_c02
  fx_c03:	db '@transitus', 10
		db 'structura T {', 10
		db 9, 't: textus', 10
		db '}', 10
  fx_c03_LEN = $ - fx_c03
  fx_c04:	db '@transitus', 10
		db 'structura T {', 10
		db 9, 'a: acies<u8, 4>', 10
		db '}', 10
  fx_c04_LEN = $ - fx_c04
  fx_c05:	db 'structura Inner {', 10
		db 9, 'a: u8', 10
		db 9, 'b: u8', 10
		db '}', 10
		db '@transitus', 10
		db 'structura T {', 10
		db 9, 's: Inner', 10
		db '}', 10
  fx_c05_LEN = $ - fx_c05
  fx_c06:	db 'structura Inner {', 10
		db 9, 'a: u8', 10
		db '}', 10
		db '@transitus', 10
		db 'structura T {', 10
		db 9, 's: Inner', 10
		db '}', 10
  fx_c06_LEN = $ - fx_c06
  fx_c07:	db '@transitus', 10
		db 'structura T {', 10
		db 9, 'a: acies<u8, 1>', 10
		db '}', 10
  fx_c07_LEN = $ - fx_c07
  fx_c08:	db '@transitus', 10
		db 'structura T {', 10
		db 9, 't: textus:maior', 10
		db '}', 10
  fx_c08_LEN = $ - fx_c08
  fx_c09:	db 'structura A {', 10
		db 9, 'x: Scriptor', 10
		db '}', 10
		db 'structura B {', 10
		db 9, 'a: A', 10
		db '}', 10
  fx_c09_LEN = $ - fx_c09
  fx_c10:	db 'structura A {', 10
		db 9, 'x: Scriptor', 10
		db '}', 10
		db '@transitus', 10
		db 'structura B {', 10
		db 9, 'a: A', 10
		db '}', 10
  fx_c10_LEN = $ - fx_c10
  fx_c11:	db 'structura T {', 10
		db 9, 'x: Scriptor', 10
		db '}', 10
		db 'functio g(t: T, s: textus) -> mensura poscit sicut t {', 10
		db 9, 'redde t.x.scribe(s);', 10
		db '}', 10
		db 'publica functio h(t: T) -> mensura {', 10
		db 9, 'redde g(t, "a");', 10
		db '}', 10
  fx_c11_LEN = $ - fx_c11
  fx_c12:	db 'structura T {', 10
		db 9, 'x: Scriptor', 10
		db '}', 10
		db 'functio g(t: T, s: textus) -> mensura poscit sicut t {', 10
		db 9, 'redde t.x.scribe(s);', 10
		db '}', 10
		db 'publica functio h(t: T) -> mensura poscit ambitus {', 10
		db 9, 'redde g(t, "a");', 10
		db '}', 10
  fx_c12_LEN = $ - fx_c12
  fx_c13:	db 'structura T {', 10
		db 9, 'a: ambitus', 10
		db '}', 10
  fx_c13_LEN = $ - fx_c13
  fx_c14:	db '@transitus', 10
		db 'structura T {', 10
		db 9, 'a: ambitus', 10
		db '}', 10
  fx_c14_LEN = $ - fx_c14
  fx_c15:	db 'interfacies Pingo {', 10
		db 9, 'functio pinge(x: u8) -> u8', 10
		db '}', 10
		db 'structura T {', 10
		db 9, 'd: dyn Pingo', 10
		db '}', 10
  fx_c15_LEN = $ - fx_c15
  fx_c16:	db 'interfacies Pingo {', 10
		db 9, 'functio pinge(x: u8) -> u8', 10
		db '}', 10
		db '@transitus', 10
		db 'structura T {', 10
		db 9, 'd: dyn Pingo', 10
		db '}', 10
  fx_c16_LEN = $ - fx_c16
  fx_c17:	db 'structura T {', 10
		db 9, 'f: functio(u8) -> u8', 10
		db '}', 10
  fx_c17_LEN = $ - fx_c17
  fx_c18:	db '@transitus', 10
		db 'structura T {', 10
		db 9, 'f: functio(u8) -> u8', 10
		db '}', 10
  fx_c18_LEN = $ - fx_c18
  fx_c19:	db 'structura T {', 10
		db 9, 'e: eventus<u8>', 10
		db '}', 10
  fx_c19_LEN = $ - fx_c19
  fx_c20:	db '@transitus', 10
		db 'structura T {', 10
		db 9, 'e: eventus<u8>', 10
		db '}', 10
  fx_c20_LEN = $ - fx_c20
  fx_c21:	db 'structura T<X> {', 10
		db 9, 'x: X', 10
		db '}', 10
  fx_c21_LEN = $ - fx_c21
  fx_c22:	db '@transitus', 10
		db 'structura T<X> {', 10
		db 9, 'x: X', 10
		db '}', 10
  fx_c22_LEN = $ - fx_c22
  fx_c23:	db '@transitus', 10
		db 'structura T {', 10
		db 9, 'x: Scriptor', 10
		db '}', 10
		db 'functio f(b: acies<u8, 16>) -> u8 {', 10
		db 9, 'firma t = b sicut T;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_c23_LEN = $ - fx_c23

  fx_tab:
	; c01: a struct holding a Scriptor: clean (spec 4.3)
	dq fx_c01, fx_c01_LEN
	dd 0, 0, 0, 0, 0, 0
	; c02: @transitus, a Scriptor field: E0321
	dq fx_c02, fx_c02_LEN
	dd 1, 321, 26, 0, 0, 0
	; c03: @transitus, textus: E0321
	dq fx_c03, fx_c03_LEN
	dd 1, 321, 26, 0, 0, 0
	; c04: @transitus, acies<u8, 4>: E0321
	dq fx_c04, fx_c04_LEN
	dd 1, 321, 26, 0, 0, 0
	; c05: @transitus, a nested two-u8 struct: E0321
	dq fx_c05, fx_c05_LEN
	dd 1, 321, 60, 0, 0, 0
	; c06: @transitus, a nested ONE-u8 struct: E0321 (was clean)
	dq fx_c06, fx_c06_LEN
	dd 1, 321, 53, 0, 0, 0
	; c07: @transitus, acies<u8, 1>: E0321 (was clean)
	dq fx_c07, fx_c07_LEN
	dd 1, 321, 26, 0, 0, 0
	; c08: @transitus, textus:maior: E0309 (pass 2), then E0321 (pass 4)
	dq fx_c08, fx_c08_LEN
	dd 2, 309, 29, 321, 26, 0
	; c09: capability-bearing transitively, through a struct: clean
	dq fx_c09, fx_c09_LEN
	dd 0, 0, 0, 0, 0, 0
	; c10: @transitus, a field whose struct holds a Scriptor: E0321
	dq fx_c10, fx_c10_LEN
	dd 1, 321, 55, 0, 0, 0
	; c11: T's mark is {ambitus}: a pure caller of `poscit sicut t` is E0421
	dq fx_c11, fx_c11_LEN
	dd 1, 421, 152, 0, 0, 0
	; c12: ... and one declaring ambitus is clean (c11's twin)
	dq fx_c12, fx_c12_LEN
	dd 0, 0, 0, 0, 0, 0
	; c13: a capability atom field: clean
	dq fx_c13, fx_c13_LEN
	dd 0, 0, 0, 0, 0, 0
	; c14: @transitus, a capability atom field: E0321
	dq fx_c14, fx_c14_LEN
	dd 1, 321, 26, 0, 0, 0
	; c15: a dyn field: clean
	dq fx_c15, fx_c15_LEN
	dd 0, 0, 0, 0, 0, 0
	; c16: @transitus, a dyn field: E0321
	dq fx_c16, fx_c16_LEN
	dd 1, 321, 76, 0, 0, 0
	; c17: a functio field: clean
	dq fx_c17, fx_c17_LEN
	dd 0, 0, 0, 0, 0, 0
	; c18: @transitus, a functio field: E0321
	dq fx_c18, fx_c18_LEN
	dd 1, 321, 26, 0, 0, 0
	; c19: an eventus field: clean
	dq fx_c19, fx_c19_LEN
	dd 0, 0, 0, 0, 0, 0
	; c20: @transitus, an eventus field: E0321
	dq fx_c20, fx_c20_LEN
	dd 1, 321, 26, 0, 0, 0
	; c21: a generic-parameter field: E0301 at X (generics do not resolve)
	dq fx_c21, fx_c21_LEN
	dd 1, 301, 21, 0, 0, 0
	; c22: @transitus, a generic-parameter field: E0301, then E0321
	dq fx_c22, fx_c22_LEN
	dd 2, 301, 32, 321, 29, 0
	; c23: forging a Scriptor from bytes through a @transitus struct: E0305 (pass 2), E0321
	dq fx_c23, fx_c23_LEN
	dd 2, 305, 87, 321, 26, 0
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
