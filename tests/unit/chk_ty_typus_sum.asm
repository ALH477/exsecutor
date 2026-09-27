; tests/unit/chk_ty_typus_sum.asm
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
; checker fixture: a sum `typus` through the WHOLE front end -- lexed, parsed,
; built, resolved, typed (pass 2's `__chk_ty_typus` `.sum` arm) and laid out
; (pass 4's `__chk_lay_sum`), from source text. docs/design/sum-types.md D1
; and D6 meeting; chk_row_layout_sum.asm did the same over hand-built trees.
;
; WHY IT EXISTS, MEASURED FIRST: with the grammar landed and no checker arm,
; `typus modus = casus ordinata, casus arborea(mensura);` followed by
; `structura S { m: modus b: u8 }` compiled CLEAN with `-o` (exit 0, no
; diagnostic) -- `chk_ty_of_node` gave the `typus` the error type silently
; (AST H3: "the parser has spoken"), the field `m` got width 0, and `b` was
; laid out at byte 0 on top of it. Exactly the "wrong rather than absent"
; layout sum-types.md §6 warned of, reachable from source for the first
; time. The arm this fixture pins is what closes it.
;
; Rows (source -> expected diagnostics, exactly):
;   1. the sum and a plain struct holding it: no diagnostic; then, read back
;      from the tree: the `typus` declaration's type is `AST_TY_SUM` over
;      itself, `m` sits at byte 0 with 72 bits recorded, `b` at byte 9, `S`
;      is 10 bytes, and both variant declarations still have `Decl.ty` = 0
;      (a constructor's type is D2's, not written, and said so).
;   2. `typus arbor = casus folium, casus nodus(arbor);` -- `EXS-E0303`, once:
;      an infinite type is class C whichever `typus` form spells it, and
;      this is how D1's "refuse a recursive sum by name" is met without a
;      new code.
;   3. `typus idem = u32;` with `structura T { x: idem }` -- the alias is
;      still transparent: no diagnostic, `x` is 32 bits at byte 0.
;   4. the sum inside a `@transitus` struct -- `EXS-E0321`, once, at the
;      field: §5.2 admits only unsigned integers on the wire, through pass
;      4's existing kind test, now from source.
;   5. a generic sum, `typus e2<T, E> = casus prosperum(T), casus adversum(E);`
;      -- `EXS-E0301` TWICE, at `T` and at `E`. Not a defect of the sum arm:
;      MEASURED on the alias form too, `typus box<T> = refero<T>;` raises the
;      same `EXS-E0301` at its `T`. Pass 1 has never put a `typus`'s generic
;      parameters in scope over its body, although spec §8.6's `TypeDecl`
;      has admitted `[GenericParams]` all along; nothing in the corpus
;      writes one, so nothing had ever asked. Pinned as the measured
;      behaviour so that fixing pass 1 flips this row on purpose -- and it
;      must be fixed before `eventus<T, E>` can become the prelude `typus`
;      sum-types.md D5 wants (recorded there as a prerequisite). The sum
;      still lays out, as 1 byte: an unresolved payload is the error type,
;      width 0, and the tag is never elided (D6).
;
; Exit 0 = every row held; 10+N = row N's diagnostics were wrong; 30+N = row
; N's read-back was wrong.
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
  .row_ok:
	; per-row read-back, on the tree `fx_run` left behind
	cmp	r12, 0
	je	.rb1
	cmp	r12, 2
	je	.rb3
	cmp	r12, 4
	je	.rb5
	jmp	.row_next
  .rb1:
	; the `typus` declaration is decl 1: `AST_TY_SUM` over itself
	lea	rdi, [fx_tree]
	mov	rsi, 1
	call	ast_decl_at
	cmp	byte [rax + AstDecl.kind], AST_D_TYPUS
	jne	.rb_bad
	mov	esi, [rax + AstDecl.ty]
	test	esi, esi
	jz	.rb_bad
	lea	rdi, [fx_tree]
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_SUM
	jne	.rb_bad
	cmp	dword [rax + AstType.a], 1
	jne	.rb_bad
	; decls 2 and 3 are the variants: untyped, deliberately
	lea	rdi, [fx_tree]
	mov	rsi, 2
	call	ast_decl_at
	cmp	byte [rax + AstDecl.kind], AST_D_VARIANT
	jne	.rb_bad
	cmp	dword [rax + AstDecl.ty], 0
	jne	.rb_bad
	lea	rdi, [fx_tree]
	mov	rsi, 3
	call	ast_decl_at
	cmp	byte [rax + AstDecl.kind], AST_D_VARIANT
	jne	.rb_bad
	cmp	dword [rax + AstDecl.ty], 0
	jne	.rb_bad
	; decl 4 is `S`, 5 is `m`, 6 is `b`
	mov	rsi, 5
	call	fx_lay
	test	rax, rax
	jnz	.rb_bad
	cmp	rcx, 72
	jne	.rb_bad
	mov	rsi, 6
	call	fx_lay
	cmp	rax, 9
	jne	.rb_bad
	mov	rsi, 4
	call	fx_lay
	cmp	rax, 10
	jne	.rb_bad
	cmp	r9, 1
	jne	.rb_bad
	jmp	.row_next
  .rb3:
	; decl 1 `idem`, decl 2 `T`, decl 3 `x`: 32 bits at byte 0
	mov	rsi, 3
	call	fx_lay
	test	rax, rax
	jnz	.rb_bad
	cmp	rcx, 32
	jne	.rb_bad
	jmp	.row_next
  .rb5:
	; decl 1 `e2`; its payloads are unresolved (row 5's note), so the sum
	; is its tag alone: 1 byte
	mov	rsi, 1
	call	fx_lay
	cmp	rax, 1
	jne	.rb_bad
	jmp	.row_next
  .row_next:
	inc	r12
	jmp	.row
  .row_bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .rb_bad:
	lea	rdi, [r12 + 31]
	call	sys_exit_group
  .rows_done:
	xor	edi, edi
	call	sys_exit_group
  .fail99:
	mov	edi, 99
	call	sys_exit_group

; rsi = a declaration id -> rax = its layout's byte offset (an aggregate's
; SIZE), rdx = its bit offset, rcx = its bit width, r9 = its alignment.
  fx_lay:
	push	rbp
	lea	rdi, [fx_tree]
	call	ast_layout_at
	movzx	edx, byte [rax + AstLayout.bitoff]
	movzx	ecx, byte [rax + AstLayout.width]
	movzx	r9d, byte [rax + AstLayout.algn]
	mov	eax, [rax + AstLayout.off]
	pop	rbp
	ret

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

  fx_path:	db 'typus_sum.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_c1:	db 'typus modus = casus ordinata, casus arborea(mensura);', 10
		db 'structura S {', 10
		db '    m: modus', 10
		db '    b: u8', 10
		db '}', 10
  fx_c1_LEN = $ - fx_c1
  fx_c2:	db 'typus arbor = casus folium, casus nodus(arbor);', 10
  fx_c2_LEN = $ - fx_c2
  fx_c3:	db 'typus idem = u32;', 10
		db 'structura T {', 10
		db '    x: idem', 10
		db '}', 10
  fx_c3_LEN = $ - fx_c3
  fx_c4:	db 'typus modus = casus ordinata, casus arborea(mensura);', 10
		db '@transitus structura W {', 10
		db '    m: modus', 10
		db '}', 10
  fx_c4_LEN = $ - fx_c4
  fx_c5:	db 'typus e2<T, E> = casus prosperum(T), casus adversum(E);', 10
  fx_c5_LEN = $ - fx_c5

  fx_tab:
	dq fx_c1, fx_c1_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c2, fx_c2_LEN
	dd 1, 303, FX_ANY, 0, 0, 0
	dq fx_c3, fx_c3_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_c4, fx_c4_LEN
	dd 1, 321, FX_ANY, 0, 0, 0
	dq fx_c5, fx_c5_LEN
	dd 2, 301, 33, 301, 52, 0
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
