; tests/unit/chk_ty_cast_literal.asm
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
; checker fixture: a PENDING LITERAL on the left of `sicut`
; (checker/types/types.inc's `.cast_pending`, spec §8.4's cast rule).
;
; THE DEFECT THIS PINS, measured at `cf18cc8` before the arm existed: the
; `.cast` arm answered its TARGET and never settled its SOURCE, so a literal
; cast kept `Node.ty` = 0 -- and every one of `7 sicut u8`, `7 sicut f32`,
; `1.5 sicut u8`, `300 sicut u8` and `7 sicut textus` type-checked CLEAN.
; `exsc aedifica ... -o` on the first of them then died with SIGILL, because
; `lower/expr.inc`'s `__lwr_lit` rasserts through `__lwr_nty` on an untyped
; node. A program the checker accepts and the compiler traps on is the class
; docs/design/sum-types.md D4 names for `?`; this is the same class reached
; through the cast, and row 1's second half is the check that fails again if
; the settling is ever removed.
;
; THE RULE: the target is an expectation like any other. A pending literal
; whose CLASS the target shares settles to the target (an identity cast, and
; the answer is still the target); a target of another class, or a value that
; does not fit the target's width, is `EXS-E0308` AT THE LITERAL -- the same
; code, from the same `__chk_ty_fits`, that `firma x: f32 = 1;` already drew.
; No code is invented (CLAUDE.md, spec §13).
;
; Rows (source -> the diagnostics, exactly, in order):
;   1. `7 sicut u8`       -- nothing. Plus: the first `Lit` node's `ty` is
;                            non-zero, which is the SIGILL's cause and the
;                            half a diagnostic count cannot see.
;   2. `7 sicut f32`      -- `EXS-E0308` at byte 34, the literal, not at the
;                            cast: an INT literal is not a float value (spec
;                            §8.4 settled no suffixes and no coercion between
;                            literal classes).
;   3. `1.5 sicut u8`     -- `EXS-E0308`, the same rule the other way.
;   4. `300 sicut u8`     -- `EXS-E0308`: the class agrees and the value does
;                            not fit. The cast is not a permission to
;                            truncate a literal silently.
;   5. `7 sicut mensura`  -- nothing: `mensura` takes an INT literal.
;   6. `(1 + 2) sicut u8` -- nothing, and it is the RECURSION that earns it:
;                            `__chk_ty_expect` descends through a pending
;                            `Binary`, so both literals settle to `u8`.
;   7. `7 sicut textus`   -- `EXS-E0305`: a literal is not cast to a `textus`
;                            (§5.2's whitelist admits numeric scalars, the
;                            same type, and `dyn` only). Settling the literal
;                            to `textus` first would draw `EXS-E0308` from
;                            `__chk_ty_fits`, but the cast's verdict is the
;                            relational one -- the span names the whole cast
;                            (`7 sicut textus`), not just the literal, and the
;                            code says what is wrong (operation not defined).
;                            The numeric-target rows above still draw `E0308`
;                            because for THOSE the target IS an expectation
;                            the literal can take; `textus` is not one, so the
;                            cast is refused as a cast, not as a literal.
;
; Exit 0 = every row held; 10+N = row N's diagnostics were wrong; 9 = row 1's
; literal came back untyped (the SIGILL is reachable again).
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
	inc	r12
	jmp	.row
  .row_bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .rows_done:
	; ROW 1'S SECOND HALF, and the one a diagnostic count cannot state: the
	; literal itself must come back TYPED. `7 sicut u8` raised nothing
	; before this arm existed either -- what it did was leave `Node.ty` = 0
	; on the `Lit`, which is what the lowering rasserts on. The first `Lit`
	; of row 1's tree is the `7` (node ids are postorder, and the `0` of
	; `redde 0;` is later in the block).
	mov	rdi, [fx_tab]
	mov	rsi, [fx_tab + 8]
	call	fx_run
	cmp	rax, 0
	jne	.fail_ty
	mov	edi, AST_LIT
	call	fx_first_ty
	test	eax, eax
	jz	.fail_ty
	xor	edi, edi
	call	sys_exit_group
  .fail_ty:
	mov	edi, 9
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

  fx_path:	db 'cast_literal.exsc'
  FX_PATH_LEN = $ - fx_path

  ; Every row is the same four lines with one expression changed, so the
  ; literal always starts at byte 34 ('functio f() -> u8 {' + LF = 20, plus
  ; '    firma y = ' = 14) -- which is what makes the offsets below a check
  ; that `EXS-E0308` lands on the LITERAL and not on the `Cast` node.
  fx_c1:	db 'functio f() -> u8 {', 10
		db '    firma y = 7 sicut u8;', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c1_LEN = $ - fx_c1
  fx_c2:	db 'functio f() -> u8 {', 10
		db '    firma y = 7 sicut f32;', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c2_LEN = $ - fx_c2
  fx_c3:	db 'functio f() -> u8 {', 10
		db '    firma y = 1.5 sicut u8;', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c3_LEN = $ - fx_c3
  fx_c4:	db 'functio f() -> u8 {', 10
		db '    firma y = 300 sicut u8;', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c4_LEN = $ - fx_c4
  fx_c5:	db 'functio f() -> u8 {', 10
		db '    firma y = 7 sicut mensura;', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c5_LEN = $ - fx_c5
  fx_c6:	db 'functio f() -> u8 {', 10
		db '    firma y = (1 + 2) sicut u8;', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c6_LEN = $ - fx_c6
  fx_c7:	db 'functio f() -> u8 {', 10
		db '    firma y = 7 sicut textus;', 10
		db '    redde 0;', 10
		db '}', 10
  fx_c7_LEN = $ - fx_c7

  fx_tab:
	dq fx_c1, fx_c1_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c2, fx_c2_LEN
	dd 1, 308, 34, 0, 0, 0
	dq fx_c3, fx_c3_LEN
	dd 1, 308, 34, 0, 0, 0
	dq fx_c4, fx_c4_LEN
	dd 1, 308, 34, 0, 0, 0
	dq fx_c5, fx_c5_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c6, fx_c6_LEN
	dd 0, 0, 0, 0, 0, 0
 dq fx_c7, fx_c7_LEN
	dd 1, 305, FX_ANY, 0, 0, 0
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
