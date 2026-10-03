; tests/unit/chk_ty_sum_typing.asm
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
; checker fixture: docs/design/sum-types.md D2's TYPING -- constructors and
; constructor patterns (checker/types/sum.inc), from source through the whole
; front end. Every row pins the diagnostics EXACTLY (count, codes, offsets,
; measured with `exsc --diagnostica json` on the row's own source by the
; generator that wrote the table); a refusal row and its accepted twin differ
; in the one thing the rule is about.
;
; D2 REUSES FOUR CODES AND INVENTS NONE: `EXS-E0301` (a pattern naming
; nothing), `EXS-E0302` (a variant colliding with a constant in one scope),
; `EXS-E0303` (a pattern or constructor of the wrong type), `EXS-E0304`
; (a binding or payload count that is not the variant's, a generic
; constructor whose type arguments are missing, a sum written with the wrong
; number of type arguments).
;
;   row 1  constructors of every shape and a complete match: clean
;   row 2  a pattern naming nothing                           E0301 at it
;   row 3  a CONSTANT as a pattern against a sum               E0303
;   row 4  two bindings for a one-element payload              E0304 at the
;          pattern
;   row 5  a constructor with one payload too many             E0304 at the
;          call, and NOTHING for the extra argument
;   row 6  a variant against a SCALAR scrutinee                E0303
;   row 7  bindings on a constant pattern against a scalar     E0303
;   row 8  a variant and a constant of one name               E0302 at the
;          second
;   row 9  a GENERIC sum's constructor with no expectation and no type
;          arguments                                          E0304 at it
;   row 10 ... with explicit type arguments                    clean
;   row 11 ... with the expectation from an annotation         clean
;   row 12 a constructor where a `u8` is wanted                E0303
;   row 13 a non-generic sum written with a type argument      E0304
;   row 14 a binding takes its PAYLOAD's type: `mensura` into
;          `u8`                                               E0303
;   row 15 the SCRUTINEE'S variants first: a LOCAL `firma ordinata`
;          does not capture the pattern `ordinata`             clean
;   row 16 a binding is ASSIGNED by the match: read in the arm,
;          no `EXS-E0307` (tests/unit/chk_pattern_scope.asm row 3 pinned
;          the opposite until this landed)                    clean
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

  fx_path:	db 'sum_typing.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_c1:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio g(x: u8) -> modus { si x eq 0 { redde ordinata; } redde par(x, 7); }', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    firma a: modus = arborea(5);', 10
		db '    discerne g(3) {', 10
		db '        casus ordinata { redde 0; }', 10
		db '        casus arborea(n) { redde 2; }', 10
		db '        casus par(x, y) { redde x; }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c1_LEN = $ - fx_c1
  fx_c2:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m {', 10
		db '        casus nulla { redde 0; }', 10
		db '        aliter { }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c2_LEN = $ - fx_c2
  fx_c3:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'firma TRES: u8 = 3;', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m {', 10
		db '        casus TRES { redde 0; }', 10
		db '        aliter { }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c3_LEN = $ - fx_c3
  fx_c4:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m {', 10
		db '        casus arborea(a, b) { redde 0; }', 10
		db '        aliter { }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c4_LEN = $ - fx_c4
  fx_c5:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    firma y: modus = arborea(1, 2);', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c5_LEN = $ - fx_c5
  fx_c6:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(x: u8) -> u8 {', 10
		db '    discerne x {', 10
		db '        casus ordinata { redde 0; }', 10
		db '        aliter { }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c6_LEN = $ - fx_c6
  fx_c7:	db 'firma TRES: u8 = 3;', 10
		db 'functio f(x: u8) -> u8 {', 10
		db '    discerne x {', 10
		db '        casus TRES(z) { redde 0; }', 10
		db '        aliter { }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c7_LEN = $ - fx_c7
  fx_c8:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'firma ordinata: u8 = 3;', 10
  fx_c8_LEN = $ - fx_c8
  fx_c9:	db 'typus e2<T, E> = casus pr(T), casus ad(E);', 10
		db 'functio f() -> u8 {', 10
		db '    firma y = pr(3);', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c9_LEN = $ - fx_c9
  fx_c10:	db 'typus e2<T, E> = casus pr(T), casus ad(E);', 10
		db 'functio f() -> u8 {', 10
		db '    firma y = pr<u8, u16>(3);', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c10_LEN = $ - fx_c10
  fx_c11:	db 'typus e2<T, E> = casus pr(T), casus ad(E);', 10
		db 'functio f() -> u8 {', 10
		db '    firma y: e2<u8, u16> = ad(300);', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c11_LEN = $ - fx_c11
  fx_c12:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f() -> u8 {', 10
		db '    firma y: u8 = ordinata;', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c12_LEN = $ - fx_c12
  fx_c13:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus<u8>) -> u8 {', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c13_LEN = $ - fx_c13
  fx_c14:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m {', 10
		db '        casus arborea(n) { firma z: u8 = n; }', 10
		db '        aliter { }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c14_LEN = $ - fx_c14
  fx_c15:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    firma ordinata: u8 = 9;', 10
		db '    discerne m {', 10
		db '        casus ordinata { redde ordinata; }', 10
		db '        aliter { }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c15_LEN = $ - fx_c15
  fx_c16:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> mensura {', 10
		db '    discerne m {', 10
		db '        casus arborea(n) { redde n; }', 10
		db '        aliter { }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c16_LEN = $ - fx_c16

  fx_tab:
	; row 1: every constructor shape, a complete match
	dq fx_c1, fx_c1_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 2: a pattern naming nothing
	dq fx_c2, fx_c2_LEN
	dd 1, 301, 133, 0, 0, 0
	; row 3: a constant against a sum
	dq fx_c3, fx_c3_LEN
	dd 1, 303, 153, 0, 0, 0
	; row 4: two bindings, one element
	dq fx_c4, fx_c4_LEN
	dd 1, 304, 133, 0, 0, 0
	; row 5: one payload too many
	dq fx_c5, fx_c5_LEN
	dd 1, 304, 123, 0, 0, 0
	; row 6: a variant against a scalar
	dq fx_c6, fx_c6_LEN
	dd 1, 303, 130, 0, 0, 0
	; row 7: bindings on a constant, scalar
	dq fx_c7, fx_c7_LEN
	dd 1, 303, 76, 0, 0, 0
	; row 8: a variant and a constant of one name
	dq fx_c8, fx_c8_LEN
	dd 1, 302, 74, 0, 0, 0
	; row 9: generic, no expectation, no arguments
	dq fx_c9, fx_c9_LEN
	dd 1, 304, 77, 0, 0, 0
	; row 10: generic, explicit arguments
	dq fx_c10, fx_c10_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 11: generic, from the annotation
	dq fx_c11, fx_c11_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 12: a constructor where u8 is wanted
	dq fx_c12, fx_c12_LEN
	dd 1, 303, 112, 0, 0, 0
	; row 13: a non-generic sum with an argument
	dq fx_c13, fx_c13_LEN
	dd 1, 304, 92, 0, 0, 0
	; row 14: a binding takes the payload type
	dq fx_c14, fx_c14_LEN
	dd 1, 303, 160, 0, 0, 0
	; row 15: a local constant does not capture
	dq fx_c15, fx_c15_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 16: a binding is assigned by the match
	dq fx_c16, fx_c16_LEN
	dd 0, 0, 0, 0, 0, 0
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
