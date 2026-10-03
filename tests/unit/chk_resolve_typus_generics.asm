; tests/unit/chk_resolve_typus_generics.asm
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
; checker fixture: pass 1 SCOPES A TYPE DECLARATION'S OWN GENERIC PARAMETERS
; over its body -- `resolve.inc`'s `.tydecl` arm and the `CHK_FR_TYDECL` frame
; it pushes. From source text, through the whole front end.
;
; WHY IT EXISTS, MEASURED FIRST. Spec §8.6 gives `TypeDecl`, `StructDecl` and
; `InterfaceDecl` an optional `[GenericParams]`, and pass 1 had never put one
; in scope. `__chk_kids`' slot order walks the BODY before the `Generics`
; (`Typus.a` then `.c`; `Struct.a`/`Interface.a` then `.c`), and at module
; level `__chk_bind` has no frame to bind into, so the parameter was dropped
; on the floor and its USE raised `EXS-E0301`. Measured with a fresh `exsc`,
; `--diagnostica json`, one file per case:
;
;   typus box<T> = refero<T>;                            E0301 at `T`
;   typus e2<T, E> = casus prosperum(T), …                E0301 at `T`, at `E`
;   structura S<T> { x: T }                              E0301 at `T`
;   interfacies I<T> { functio acc(self: I, v: T) -> T } E0301 twice, at `T`
;
; `potestas` is absent from that list because its grammar takes no generic
; parameters at all: `Potestas.c` is `AST_R_NONE` and `potestas P<T>` is
; `EXS-E0201` at the `<`, measured the same way.
;
; The interface row is the one that needed more than the arm. `__chk_lookup`'s
; frame walk STOPS at the innermost `CHK_FR_FN` frame, justified by "a function
; is never inside another function's scope" -- but a member's signature naming
; the interface's own `T` has to cross outward past its own `FN` frame into
; something that is a scope and is not a function. Hence a frame KIND of its
; own: the walk continues past an `FN` frame when, and only when, the next
; frame out is a `CHK_FR_TYDECL`, and `__chk_frame_root` skips a `TYDECL` the
; way it skips a `BLOCK` (its `ChkFrame.node` has no `Sig` to insert a row
; into).
;
; Rows (source -> expected diagnostics, exactly; then a read-back):
;   1. `typus box<T> = refero<T>;` -- the ALIAS form. No diagnostic; two
;      `TyPath` nodes (`refero<T>` and its argument `T`), one of them typed
;      `AST_TY_PARAM`.
;   2. `typus e2<T, E> = casus prosperum(T), casus adversum(E);` -- the SUM
;      form, two parameters, one payload each. No diagnostic; both `TyPath`
;      nodes are `AST_TY_PARAM`. (`chk_ty_typus_sum.asm` row 5 is the same
;      source through pass 4's layout; this row is pass 1's half of it.)
;   3. `structura S<T> { x: T }` -- one `TyPath`, `AST_TY_PARAM`.
;   4. `interfacies I<T> { functio acc(self: I, v: T) -> T }` -- three
;      `TyPath` nodes; the two `T`s are `AST_TY_PARAM` and `self: I` is the
;      interface, so two of three. This is the row that pins the frame walk
;      crossing a member's `FN` frame outward.
;   5. A GENERIC PARAMETER IS NOT VISIBLE OUTSIDE ITS DECLARATION: the row 1
;      alias followed by `publica functio usa(v: u32) -> T { redde v; }` --
;      `EXS-E0301`, ONCE, at the `T` in the signature, byte 57. Three
;      `TyPath` nodes and only ONE `AST_TY_PARAM`: the one inside the
;      `typus`. Without this row the arm could bind the parameter into
;      whatever frame happened to be open and row 1 would still pass.
;   6. TWO DECLARATIONS BOTH NAMING `T`: `typus unus<T> = refero<T>;` and
;      `typus duo<T> = casus nihil, casus aliquid(T);`. No diagnostic -- in
;      particular NO `EXS-E0302`, because each `typus` gets a frame of its
;      own and `__chk_bind`'s duplicate scan examines one frame. Three
;      `TyPath` nodes, two of them `AST_TY_PARAM`.
;
; Exit 0 = every row held; 10+N = row N's diagnostics were wrong; 30+N = row
; N's read-back was wrong.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 40		; dq src, len; dd count, code0, off0, ntypath, nparam, pad
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
  .row_ok:
	; the read-back, on the tree `fx_run` left behind: how many `TyPath`
	; nodes there are, and how many of them pass 2 typed `AST_TY_PARAM`.
	mov	edi, AST_TY_PARAM
	call	fx_typaths
	mov	ecx, [r13 + 28]
	cmp	rax, rcx
	jne	.rb_bad
	mov	ecx, [r13 + 32]
	cmp	rdx, rcx
	jne	.rb_bad
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

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; ---- harness ---------------------------------------------------------------
; Plain labels, as in tests/unit/chk_ty_typus_sum.asm, whose harness this is:
; a `proc` argument name is an unmangled global. Each helper pushes an odd
; number of registers, so `rsp` is 16-aligned at every call inside it.

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

; fx_typaths(edi = an `AstType` kind) -> rax = how many `AST_TYPATH` nodes the
; last tree holds, rdx = how many of those have `Node.ty` of that kind. Both
; numbers, because a row expecting no diagnostic and reading back only the
; second would also pass on a tree with no `TyPath` in it at all.
  fx_typaths:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	mov	r15d, edi
	xor	ebx, ebx
	xor	r14d, r14d
	mov	r13, 1
  .scan:
	lea	rdi, [fx_tree]
	call	ast_node_count
	cmp	r13, rax
	ja	.out
	lea	rdi, [fx_tree]
	mov	rsi, r13
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_TYPATH
	jne	.next
	inc	rbx
	mov	esi, [rax + AstNode.ty]
	test	esi, esi
	jz	.next
	lea	rdi, [fx_tree]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, r15d
	jne	.next
	inc	r14
  .next:
	inc	r13
	jmp	.scan
  .out:
	mov	rax, rbx
	mov	rdx, r14
	pop	r15
	pop	r14
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

  fx_path:	db 'typus_generics.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_c1:	db 'typus box<T> = refero<T>;', 10
  fx_c1_LEN = $ - fx_c1
  fx_c2:	db 'typus e2<T, E> = casus prosperum(T), casus adversum(E);', 10
  fx_c2_LEN = $ - fx_c2
  fx_c3:	db 'structura S<T> {', 10
		db '    x: T', 10
		db '}', 10
  fx_c3_LEN = $ - fx_c3
  fx_c4:	db 'interfacies I<T> {', 10
		db '    functio acc(self: I, v: T) -> T', 10
		db '}', 10
  fx_c4_LEN = $ - fx_c4
  fx_c5:	db 'typus box<T> = refero<T>;', 10
		db 'publica functio usa(v: u32) -> T {', 10
		db '    redde v;', 10
		db '}', 10
  fx_c5_LEN = $ - fx_c5
  fx_c6:	db 'typus unus<T> = refero<T>;', 10
		db 'typus duo<T> = casus nihil, casus aliquid(T);', 10
  fx_c6_LEN = $ - fx_c6

  fx_tab:
	dq fx_c1, fx_c1_LEN
	dd 0, 0, 0, 2, 1, 0
	dq fx_c2, fx_c2_LEN
	dd 0, 0, 0, 2, 2, 0
	dq fx_c3, fx_c3_LEN
	dd 0, 0, 0, 1, 1, 0
	dq fx_c4, fx_c4_LEN
	dd 0, 0, 0, 3, 2, 0
	dq fx_c5, fx_c5_LEN
	dd 1, 301, 57, 3, 1, 0
	dq fx_c6, fx_c6_LEN
	dd 0, 0, 0, 3, 2, 0
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
