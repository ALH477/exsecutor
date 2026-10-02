; tests/unit/chk_ty_exhaustive.asm
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
; checker fixture: `discerne` is EXHAUSTIVE -- docs/design/sum-types.md D3,
; spec §8.5's "Exhaustive, with no fallthrough", `EXS-E0351`
; (checker/types/sum.inc's `__chk_ty_exhaust`), from source through the whole
; front end. Every row pins the diagnostics EXACTLY -- the count, each code
; and each offset, measured with `exsc --diagnostica json` on the row's own
; source by the generator that wrote the table -- so a second, unrelated
; diagnostic cannot hide in a row (sum-types.md §7 item 3: "the code set must
; EQUAL {EXS-E0351}"). Where a row names a fix, diagnostic 0 must carry an
; INSERT fix with exactly that text; "none" means it must carry no fix.
;
; THE TWO RULES, each with accepted twins, so a checker that refused every
; `discerne` fails the twins and one that refused none fails the rest:
;
;   a SUM scrutinee covers every variant, or has `aliter`:
;     row 1  one variant missing          E0351, fix names `arborea`
;     row 2  two missing, in DECLARATION order, binding names one per
;            payload element (`par(v1, v2)`)    E0351, fix names both
;     row 3  every variant covered        clean
;     row 4  one arm and `aliter`         clean
;     row 5  no arm at all                E0351, fix names all three
;     row 6  every variant, one TWICE     clean: the unreachable arm is
;            not diagnosed (D3 leaves it `[OPEN]`, no code)
;     row 7  the prelude's `eventus`, `adversum` missing   E0351
;   a SCALAR scrutinee needs `aliter` -- its value space is 2^N:
;     row 8  `u8`, one arm                E0351, NO fix (the body is the
;            author's to write)
;     row 9  `u1`, BOTH values covered    E0351 all the same: D3 does not
;            enumerate scalars, and `u1` is a scalar
;     row 10 `u8` with `aliter`           clean
;     row 11 no arm at all                E0351
;     row 12 a nested `discerne` missing an arm inside a complete outer one:
;            E0351 once, at the INNER scrutinee (offset 167)
;
; THE CARET IS THE SCRUTINEE; the fix is inserted at the `discerne`'s
; closing brace, its last byte.
;
; Exit 0 = every row held; 10+N = row N's diagnostics were wrong (the
; diagnostics the row did produce are rendered first); 30+N = row N's fix was
; wrong; 99 = setup.
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

  fx_path:	db 'exhaustive.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_c1:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m {', 10
		db '        casus ordinata { redde 0; }', 10
		db '        casus par(a, b) { redde a; }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c1_LEN = $ - fx_c1
  fx_f1:	db 'casus arborea(v1) { }', 10
  fx_f1_LEN = $ - fx_f1
  fx_c2:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m {', 10
		db '        casus ordinata { redde 0; }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c2_LEN = $ - fx_c2
  fx_f2:	db 'casus arborea(v1) { }', 10, 'casus par(v1, v2) { }', 10
  fx_f2_LEN = $ - fx_f2
  fx_c3:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m {', 10
		db '        casus par(a, b) { redde a; }', 10
		db '        casus ordinata { redde 0; }', 10
		db '        casus arborea(n) { redde 2; }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c3_LEN = $ - fx_c3
  fx_c4:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m {', 10
		db '        casus ordinata { redde 0; }', 10
		db '        aliter { redde 3; }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c4_LEN = $ - fx_c4
  fx_c5:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m { }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c5_LEN = $ - fx_c5
  fx_f5:	db 'casus ordinata { }', 10, 'casus arborea(v1) { }', 10, 'casus par(v1, v2) { }', 10
  fx_f5_LEN = $ - fx_f5
  fx_c6:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m {', 10
		db '        casus ordinata { redde 0; }', 10
		db '        casus arborea(n) { redde 2; }', 10
		db '        casus ordinata { redde 4; }', 10
		db '        casus par(a, b) { redde a; }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c6_LEN = $ - fx_c6
  fx_c7:	db 'functio f(r: eventus<u8, erratum>) -> u8 {', 10
		db '    discerne r {', 10
		db '        casus prosperum(v) { redde v; }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c7_LEN = $ - fx_c7
  fx_f7:	db 'casus adversum(v1) { }', 10
  fx_f7_LEN = $ - fx_f7
  fx_c8:	db 'functio f(x: u8) -> u8 {', 10
		db '    discerne x {', 10
		db '        casus 1 { redde 0; }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c8_LEN = $ - fx_c8
  fx_c9:	db 'functio f(x: u1) -> u8 {', 10
		db '    discerne x {', 10
		db '        casus 1 { redde 0; }', 10
		db '        casus 0 { redde 2; }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c9_LEN = $ - fx_c9
  fx_c10:	db 'functio f(x: u8) -> u8 {', 10
		db '    discerne x {', 10
		db '        casus 1 { redde 0; }', 10
		db '        aliter { }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c10_LEN = $ - fx_c10
  fx_c11:	db 'functio f(x: u8) -> u8 {', 10
		db '    discerne x { }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c11_LEN = $ - fx_c11
  fx_c12:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u16);', 10
		db 'functio f(m: modus) -> u8 {', 10
		db '    discerne m {', 10
		db '        casus arborea(n) {', 10
		db '            discerne m {', 10
		db '                casus ordinata { }', 10
		db '            }', 10
		db '        }', 10
		db '        aliter { }', 10
		db '    }', 10
		db '    redde 1;', 10
		db '}', 10
  fx_c12_LEN = $ - fx_c12
  fx_f12:	db 'casus arborea(v1) { }', 10, 'casus par(v1, v2) { }', 10
  fx_f12_LEN = $ - fx_f12

  fx_tab:
	; row 1: a sum, one variant missing
	dq fx_c1, fx_c1_LEN
	dd 1, 351, 115, 0, 0, 0
	; row 2: a sum, two missing
	dq fx_c2, fx_c2_LEN
	dd 1, 351, 115, 0, 0, 0
	; row 3: a sum, every variant
	dq fx_c3, fx_c3_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 4: a sum, one arm and aliter
	dq fx_c4, fx_c4_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 5: a sum, no arm
	dq fx_c5, fx_c5_LEN
	dd 1, 351, 115, 0, 0, 0
	; row 6: a sum, every variant, one twice
	dq fx_c6, fx_c6_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 7: eventus, adversum missing
	dq fx_c7, fx_c7_LEN
	dd 1, 351, 56, 0, 0, 0
	; row 8: u8, one arm
	dq fx_c8, fx_c8_LEN
	dd 1, 351, 38, 0, 0, 0
	; row 9: u1, both values
	dq fx_c9, fx_c9_LEN
	dd 1, 351, 38, 0, 0, 0
	; row 10: u8 with aliter
	dq fx_c10, fx_c10_LEN
	dd 0, 0, 0, 0, 0, 0
	; row 11: u8, no arm
	dq fx_c11, fx_c11_LEN
	dd 1, 351, 38, 0, 0, 0
	; row 12: nested, the inner one incomplete
	dq fx_c12, fx_c12_LEN
	dd 1, 351, 167, 0, 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW
  assert ($ - fx_tab) mod FX_ROW = 0

  fx_fixes:
	dq fx_f1, fx_f1_LEN
	dq fx_f2, fx_f2_LEN
	dq 0, 0
	dq 0, 0
	dq fx_f5, fx_f5_LEN
	dq 0, 0
	dq fx_f7, fx_f7_LEN
	dq -1, 0
	dq -1, 0
	dq 0, 0
	dq -1, 0
	dq fx_f12, fx_f12_LEN
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
