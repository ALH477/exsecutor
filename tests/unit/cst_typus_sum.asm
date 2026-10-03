; tests/unit/cst_typus_sum.asm
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
; cst + ast fixture: docs/design/sum-types.md D1's grammar, parsed and built.
;
;	typus eventus2<T, E> = casus prosperum(T), casus adversum(E);
;	typus modus = casus ordinata, casus arborea(mensura);
;	typus idem = u32;
;
;   1. Lexes and parses with NO diagnostics, and `ast_from_cst` builds a tree
;      `ast_verify_stage1` accepts -- the alias form on the third line still
;      parses, so the one-token peek at `casus` after `=` broke nothing.
;   2. Exactly two `SumBody` nodes and exactly four `Variant` nodes, and the
;      variants' payload counts in node (= source) order are 1, 1, 0, 1: the
;      payload-less `ordinata` is a `Variant` with an empty list, not a
;      different kind and not an error.
;   3. Exactly four `AST_D_VARIANT` declarations; each is parented to an
;      `AST_D_TYPUS` declaration; the first two share one parent and the last
;      two share another, different one -- a variant is a name in the scope
;      of its `typus` (D2), which is what the parent pointer is for.
;   4. Three `Typus` nodes, and exactly one of them holds a type node in `a`
;      rather than a `SumBody`: the child's KIND is the discriminator
;      (ast/kinds.inc), and the alias form is unchanged by D1.
;   5. `typus x = casus` cut off at end of input: at least one diagnostic,
;      the first of them `EXS-E0203` -- §8.6's end-of-input rule for "inside
;      any other construct", since no bracket is open. (sum-types.md §7 item
;      1 predicted `EXS-E0202`; the parser's own rule says 0203, and the rule
;      is right: 0202 is for an unclosed bracket, block or `<…>`.)
;
; THIS FIXTURE STOPS AT THE AST, like ast_from_cst_newops.asm: no checker
; pass has an arm for a `SumBody` yet (that is the next commit's work, and
; it says so in sum-types.md §7), so the checker is not run.
;
; Exit 0 = all checks passed; 10+N = check N failed (tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 8 * 1024 * 1024
	call	arena_init
	jc	.fail0

	; ---- check 1: the good source, clean, built, verified ----
	lea	rax, [fx_src]
	mov	[fx_sp], rax
	mov	qword [fx_sl], FX_SRC_LEN
	call	fx_parse
	cmp	qword [fx_ndiag], 0
	jne	.fail1
	cmp	qword [fx_root], 0
	je	.fail1

	; the tree, for a reader of this fixture's output
	lea	rdi, [fx_wr]
	lea	rsi, [fx_buf]
	mov	rdx, 65536
	call	diag_out_init
	lea	rdi, [fx_ast]
	lea	rsi, [fx_wr]
	call	ast_dump
	mov	rdi, 1
	lea	rsi, [fx_buf]
	mov	rdx, [fx_wr + DiagOut.len]
	call	sys_write

	; ---- check 2: two SumBody, four Variant, payloads 1 1 0 1 ----
	xor	r12, r12			; SumBody count
	xor	r13, r13			; Variant count
	mov	r14, 1
  .scan2:
	lea	rdi, [fx_ast]
	call	ast_node_count
	cmp	r14, rax
	jg	.done2
	lea	rdi, [fx_ast]
	mov	rsi, r14
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_SUMBODY
	jne	.notsb
	inc	r12
	jmp	.next2
  .notsb:
	cmp	ecx, AST_VARIANT
	jne	.next2
	cmp	r13, 4
	jae	.fail2				; a fifth variant
	mov	edx, [rax + AstNode.c]		; payload count
	lea	rbx, [fx_payloads]
	mov	ecx, [rbx + r13 * 4]
	cmp	edx, ecx
	jne	.fail2
	inc	r13
  .next2:
	inc	r14
	jmp	.scan2
  .done2:
	cmp	r12, 2
	jne	.fail2
	cmp	r13, 4
	jne	.fail2

	; ---- check 3: four variant decls, parented in pairs to two typus ----
	xor	r13, r13			; variants seen
	mov	qword [fx_p1], 0
	mov	qword [fx_p2], 0
	mov	r14, 1
  .scan3:
	lea	rdi, [fx_ast]
	call	ast_decl_count
	cmp	r14, rax
	jg	.done3
	lea	rdi, [fx_ast]
	mov	rsi, r14
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_VARIANT
	jne	.next3
	mov	ecx, [rax + AstDecl.parent]
	mov	[fx_par], rcx
	test	rcx, rcx
	jz	.fail3
	lea	rdi, [fx_ast]
	mov	rsi, rcx
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_TYPUS
	jne	.fail3
	cmp	r13, 2
	jae	.second_pair
	; first pair: record, then both must agree
	cmp	r13, 0
	jne	.p1_cmp
	mov	rax, [fx_par]
	mov	[fx_p1], rax
	jmp	.count3
  .p1_cmp:
	mov	rax, [fx_par]
	cmp	rax, [fx_p1]
	jne	.fail3
	jmp	.count3
  .second_pair:
	cmp	r13, 2
	jne	.p2_cmp
	mov	rax, [fx_par]
	cmp	rax, [fx_p1]
	je	.fail3				; must be a DIFFERENT typus
	mov	[fx_p2], rax
	jmp	.count3
  .p2_cmp:
	mov	rax, [fx_par]
	cmp	rax, [fx_p2]
	jne	.fail3
  .count3:
	inc	r13
	cmp	r13, 4
	ja	.fail3
  .next3:
	inc	r14
	jmp	.scan3
  .done3:
	cmp	r13, 4
	jne	.fail3

	; ---- check 4: three Typus nodes, exactly one an alias ----
	xor	r12, r12			; Typus count
	xor	r13, r13			; alias count
	mov	r14, 1
  .scan4:
	lea	rdi, [fx_ast]
	call	ast_node_count
	cmp	r14, rax
	jg	.done4
	lea	rdi, [fx_ast]
	mov	rsi, r14
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_TYPUS
	jne	.next4
	inc	r12
	mov	ecx, [rax + AstNode.a]
	test	rcx, rcx
	jz	.fail4				; `Typus.a` is AST_R_NODEQ
	lea	rdi, [fx_ast]
	mov	rsi, rcx
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_SUMBODY
	je	.next4
	inc	r13
  .next4:
	inc	r14
	jmp	.scan4
  .done4:
	cmp	r12, 3
	jne	.fail4
	cmp	r13, 1
	jne	.fail4

	; ---- check 5: cut off after `casus` -> EXS-E0203 first ----
	lea	rax, [fx_bad]
	mov	[fx_sp], rax
	mov	qword [fx_sl], FX_BAD_LEN
	call	fx_parse
	cmp	qword [fx_ndiag], 1
	jb	.fail5
	lea	rdi, [fx_diags]
	xor	esi, esi
	call	vec_get
	mov	eax, [rax + Diag.code_num]
	cmp	eax, 203
	jne	.fail5

	xor	edi, edi
	call	sys_exit_group
  .fail0:
	mov	rdi, 10
	call	sys_exit_group
  .fail1:
	mov	rdi, 11
	call	sys_exit_group
  .fail2:
	mov	rdi, 12
	call	sys_exit_group
  .fail3:
	mov	rdi, 13
	call	sys_exit_group
  .fail4:
	mov	rdi, 14
	call	sys_exit_group
  .fail5:
	mov	rdi, 15
	call	sys_exit_group

; ---------------------------------------------------------------------------
; The whole front end over [fx_sp]/[fx_sl], from a reset arena: lex, parse,
; build, verify. [fx_ndiag] = diagnostics from the lexer and parser together;
; [fx_root] = the module node, or 0. A parse that fails outright (`cst_parse`
; CF) counts as one diagnostic and no root.
  fx_parse:
	push	rbp
	mov	qword [fx_root], 0
	mov	qword [fx_ndiag], 0
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 256
	call	intern_init
	lea	rdi, [fx_toks]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 64
	call	vec_init
	lea	rdi, [fx_diags]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_toks]
	lea	r8,  [fx_diags]
	call	lex_init
	lea	rdi, [fx_lx]
	mov	rsi, [fx_sp]
	mov	rdx, [fx_sl]
	lea	rcx, [fx_path]
	mov	r8d, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	lea	rdi, [fx_green]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 64
	call	vec_init
	lea	rdi, [fx_work]
	lea	rsi, [fx_arena]
	mov	rdx, 4
	mov	rcx, 64
	call	vec_init
	lea	rdi, [fx_cmap]
	lea	rsi, [fx_arena]
	mov	rdx, 256
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
	jc	.broken
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	mov	[fx_ndiag], rax
	lea	rdi, [fx_ast]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rcx, [fx_lx]
	mov	edx, [rcx + Lexer.file_id]
	lea	rdi, [fx_ast]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	mov	[fx_root], rax
	test	rax, rax
	jz	.out
	lea	rdi, [fx_ast]
	call	ast_verify_stage1
  .out:
	pop	rbp
	ret
  .broken:
	mov	qword [fx_ndiag], 1
	pop	rbp
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

segment readable
  ; The UCD blobs, emitted exactly once and AFTER every `proc` in this file
  ; (tests/unit/ast_from_cst.asm's header records why).
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path:	db 'fixture.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_src:	db 'typus eventus2<T, E> = casus prosperum(T), casus adversum(E);', 10
		db 'typus modus = casus ordinata, casus arborea(mensura);', 10
		db 'typus idem = u32;', 10
  FX_SRC_LEN = $ - fx_src
  fx_bad:	db 'typus x = casus'
  FX_BAD_LEN = $ - fx_bad
  fx_payloads:	dd 1, 1, 0, 1
  fx_sp:	rq 1
  fx_sl:	rq 1
  fx_root:	rq 1
  fx_ndiag:	rq 1
  fx_par:	rq 1
  fx_p1:	rq 1
  fx_p2:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_toks:	rb sizeof.Vec
  fx_diags:	rb sizeof.Vec
  fx_lx:	rb sizeof.Lexer
  fx_green:	rb sizeof.Vec
  fx_work:	rb sizeof.Vec
  fx_cmap:	rb sizeof.Map
  fx_ctree:	rb sizeof.CstTree
  fx_parser:	rb sizeof.CstParser
  fx_ast:	rb sizeof.Ast
  fx_wr:	rb sizeof.DiagOut
  fx_buf:	rb 65536
