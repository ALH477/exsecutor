; tests/unit/chk_ty_e0520_refero_source.asm
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
; THE TWIN OF `tests/unit/chk_ty_e0520_refero.asm`, FROM SOURCE. The same pair
; of `externus` signatures, nine characters apart:
;
;	externus("C", abi: sysv_amd64) { functio nova(x: refero<u32>) -> u8 }
;	externus("C", abi: sysv_amd64) { functio nova(x: refero_communis<u32>) -> u8 }
;
; through the WHOLE front end -- lexer, `cst_parse`, `ast_from_cst`,
; `chk_run`'s passes 0-4 -- rather than through `ast/load.inc`.
;
; WHY IT EXISTS. The hand-built fixture's header states its own reason for
; being hand-built: *"`refero<T>` DOES NOT PARSE"*. It does now -- spec §8.6's
; `CoreType` gained the alternative `'refero' '<' TypeArg '>'`
; (`tests/unit/cst_refero_type.asm`) -- and the whole point of adding a surface
; form to a rule that was already implemented is that the two must AGREE. So
; this file asserts the shape rather than only the code:
;
;   - the parameter's type is a `TyPath` over a `Path` of exactly ONE `Seg`;
;   - that `Seg`'s spelling is the six bytes `refero`, because
;     checker/types/prim.inc recognises it BY SPELLING out of one primitive
;     name pool and a `Seg` holding anything else would make the rule
;     unreachable (and `EXS-E0301` reachable instead);
;   - the `Seg` carries a `GenericArgs` of exactly one argument, which is where
;     `T` lives;
;   - the `Seg`, the `Path` and the `TyPath` all span bytes 53..59 -- the word
;     `refero`, not the whole form -- which is the caret the hand-built fixture
;     pins and the column §14 entry 13 reports.
;
; Every one of those is a thing ast/from_cst.inc SYNTHESISES: the CST node for
; this form is a `REFERO_TYPE` holding a `TK_KEYWORD` leaf and a `GENERIC_ARGS`
; (there is no `PATH` in it, and there deliberately is not -- cst/kinds.inc), so
; if the walker's synthesis drifts, the hand-built twin keeps passing and this
; one fails. That asymmetry is the reason both files exist.
;
; NOTHING IS SEEDED HERE. The hand-built twin has to intern `refero` and
; `refero_communis` itself, at the ids its dump names; this one reads them out
; of the source, which is the case that matters in production.
;
; Exit 0 = every check passed; 10+N = check N failed; 99 = setup.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.3, §6.4, §8.6, §13, §14 entry 13.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99

	; ---- 1: the rejected twin raises exactly one diagnostic ---------------
	lea	rdi, [fx_rej]
	mov	rsi, fx_rej_LEN
	call	fx_run
	cmp	rax, 1
	jne	.fail1

	; ---- 2: it is exactly EXS-E0520, on the word `refero` -----------------
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	rbx, rax
	mov	rdi, 1
	mov	rsi, rbx
	lea	rdx, [fx_buf]
	mov	rcx, 8192
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit
	cmp	dword [rbx + Diag.code_num], 520
	jne	.fail2
	cmp	dword [rbx + Diag.span.start], 53
	jne	.fail2
	cmp	dword [rbx + Diag.span.len], 6
	jne	.fail2

	; ---- 3: the shape -- TyPath -> Path -> one Seg spelled `refero` -------
	lea	rdi, [fx_refero]
	mov	rsi, FX_REFERO_LEN
	call	fx_shape
	test	rax, rax
	jz	.fail3

	; ---- 4: its GenericArgs holds exactly one argument --------------------
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_GENARGS
	jne	.fail4
	cmp	dword [rax + AstNode.b], 1
	jne	.fail4

	; ---- 5: pass 2 typed it `ref` -- the non-atomic one -------------------
	lea	rdi, [fx_tree]
	mov	rsi, [fx_tp]
	call	ast_node_at
	mov	esi, [rax + AstNode.ty]
	lea	rdi, [fx_tree]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_REF
	jne	.fail5

	; ---- 6: the accepted twin is silent ----------------------------------
	; `refero_communis<T>` is the ATOMIC one and §6.4 says it MAY cross.
	lea	rdi, [fx_acc]
	mov	rsi, fx_acc_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail6

	; ---- 7: and it is the same shape, one identifier longer ---------------
	lea	rdi, [fx_refc]
	mov	rsi, FX_REFC_LEN
	call	fx_shape
	test	rax, rax
	jz	.fail7
	lea	rdi, [fx_tree]
	mov	rsi, [fx_tp]
	call	ast_node_at
	mov	esi, [rax + AstNode.ty]
	lea	rdi, [fx_tree]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_REFC
	jne	.fail7

	xor	edi, edi
	call	sys_exit_group
  .fail1:
	mov	edi, 11
	call	sys_exit_group
  .fail2:
	mov	edi, 12
	call	sys_exit_group
  .fail3:
	mov	edi, 13
	call	sys_exit_group
  .fail4:
	mov	edi, 14
	call	sys_exit_group
  .fail5:
	mov	edi, 15
	call	sys_exit_group
  .fail6:
	mov	edi, 16
	call	sys_exit_group
  .fail7:
	mov	edi, 17
	call	sys_exit_group
  .fail99:
	mov	edi, 99
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; ---- harness ---------------------------------------------------------------
; tests/unit/chk_row_layout_src.asm's `fx_run`/`fx_setup`, plus `fx_shape`.
; Plain labels, not `proc`: a `proc` argument name is an unmangled global
; (macros/proc.inc's header). Every helper pushes an ODD number of registers so
; `rsp` is 16-aligned at the calls inside it.

; fx_run(rdi = source, rsi = length) -> rax = the diagnostics `chk_run`
; appended; -1 if the lexer or the parser said anything -- the fixture's source
; is then wrong, and no count can match it.
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

; fx_shape(rdi = spelling, rsi = its length) -> rax = the `Seg`'s `b`, which is
; the `GenericArgs` node id, when the last tree holds exactly one `TyPath` and
; it is a `Path` of one `Seg` spelled those bytes, all three spanning exactly
; that word -- byte 53 for `rsi` bytes, not the whole form; 0 otherwise.
; `fx_tp` = that `TyPath`'s id.
;
; The span is checked on all three because that is the part a walker could get
; wrong invisibly: the `REFERO_TYPE` CST node covers `refero<u32>`, eleven
; bytes, and using it would widen every caret this fixture does not look at.
  fx_shape:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	mov	qword [fx_tp], 0
	mov	rbx, 1
  .scan:
	lea	rdi, [fx_tree]
	call	ast_node_count
	cmp	rbx, rax
	ja	.scanned
	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_TYPATH
	jne	.next
	cmp	qword [fx_tp], 0
	jne	.no			; two of them: not this fixture's source
	mov	[fx_tp], rbx
  .next:
	inc	rbx
	jmp	.scan
  .scanned:
	cmp	qword [fx_tp], 0
	je	.no

	; the TyPath's span and its `Path` operand
	lea	rdi, [fx_tree]
	mov	rsi, [fx_tp]
	call	ast_node_at
	cmp	dword [rax + AstNode.span.start], 53
	jne	.no
	cmp	dword [rax + AstNode.span.len], r13d
	jne	.no
	mov	ebx, [rax + AstNode.a]
	test	ebx, ebx
	jz	.no

	; the Path: exactly one segment, same span
	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_PATH
	jne	.no
	cmp	dword [rax + AstNode.b], 1
	jne	.no
	cmp	dword [rax + AstNode.span.start], 53
	jne	.no
	cmp	dword [rax + AstNode.span.len], r13d
	jne	.no
	mov	esi, [rax + AstNode.a]
	lea	rdi, [fx_tree]
	call	ast_extra_at		; the one element: the Seg's node id
	test	eax, eax
	jz	.no
	mov	ebx, eax

	; the Seg: its kind, its span, its spelling, and that it carries args
	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_SEG
	jne	.no
	cmp	dword [rax + AstNode.span.start], 53
	jne	.no
	cmp	dword [rax + AstNode.span.len], r13d
	jne	.no
	mov	ebx, [rax + AstNode.b]	; the GenericArgs node id
	test	ebx, ebx
	jz	.no
	mov	esi, [rax + AstNode.a]	; the interner id of the spelling
	test	esi, esi
	jz	.no
	lea	rdi, [fx_names]
	call	intern_bytes		; rax = bytes, rdx = length
	cmp	rdx, r13
	jne	.no
	xor	ecx, ecx
  .byte:
	cmp	rcx, r13
	jae	.same
	mov	r8b, [rax + rcx]
	cmp	r8b, [r12 + rcx]
	jne	.no
	inc	rcx
	jmp	.byte
  .same:
	mov	rax, rbx
	jmp	.out
  .no:
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
	mov	rsi, 16 * 1024 * 1024
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

  fx_path	db 'chk_ty_e0520_refero_source.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_refero	db 'refero'
  FX_REFERO_LEN = $ - fx_refero
  fx_refc	db 'refero_communis'
  FX_REFC_LEN = $ - fx_refc

  ; Byte for byte the hand-built twin's two sources, so the spans this fixture
  ; pins are literally the ones that file pins: line 1 is 33 bytes, four spaces
  ; of indent, and `refero` starts at byte 53.
  fx_rej:	db 'externus("C", abi: sysv_amd64) {', 10
		db '    functio nova(x: refero<u32>) -> u8', 10
		db '}', 10
  fx_rej_LEN = $ - fx_rej
  fx_acc:	db 'externus("C", abi: sysv_amd64) {', 10
		db '    functio nova(x: refero_communis<u32>) -> u8', 10
		db '}', 10
  fx_acc_LEN = $ - fx_acc

segment readable writeable
  fx_tp:	rq 1
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
