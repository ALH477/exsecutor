; tests/unit/ast_from_cst_newops.asm
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
; ast fixture: §5.4's three 2026-09-25 operators -- `residuum`, `atque` and
; `sive` -- come out of `ast_from_cst` as `AST_OP_REM`, `AST_OP_BAND` and
; `AST_OP_BOR`, and `/` still comes out as `AST_OP_DIV` beside them.
;
; WHY THE NEIGHBOUR IS IN THE LIST. `__ast_w_op` maps a word to an operator by
; DISTANCE along two hand-maintained lists (ast/from_cst.inc's `AST_W_*` pool
; and ast/kinds.inc's `AST_OP_*`), and the three new spellings were appended to
; both after `/`. The assemble-time asserts in from_cst.inc catch a list that
; slipped as a whole; they cannot catch a `db` pool whose three new entries are
; in a different order from the constants, nor the loop bound that decides how
; far along the pool the search runs. So this fixture checks all four values,
; in source order, and `/` is what fails if the pool grew in the wrong place
; rather than at its end.
;
; THE LAST ROW IS THE CONTEXTUAL CHECK AT THIS LAYER. `atque atque sive` is a
; name, the operator, and another name; `__ast_w_binary` walks a flat
; `XOR_EXPR`'s children and asks `__ast_w_op` about every `TK_IDENT` LEAF it
; meets, so a walker that also looked inside the operand `NAME_EXPR`s would
; record `AST_OP_BOR` (the last name it saw) instead of `AST_OP_BAND`. The CST
; is what keeps them apart -- an operand is a node, an operator is a leaf --
; and this row is the evidence that the fold relies on that and nothing else.
;
; The source declares its operands first and then one binary expression per
; operator, each in its own binding, so the tree has exactly five `AST_BINARY`
; nodes and postorder puts them in source order (each `BindingStmt`'s
; initializer is fully built before the next statement is started):
;
;	publica functio f() -> u32 {
;		firma a: u32 = 1;
;		firma b: u32 = 1;
;		firma atque: u32 = 1;
;		firma sive: u32 = 1;
;		firma p: u32 = a residuum b;
;		firma q: u32 = a atque b;
;		firma r: u32 = a sive b;
;		firma s: u32 = a / b;
;		firma t: u32 = atque atque sive;
;		redde p;
;	}
;
; THIS FIXTURE STOPS AT THE AST, like tests/unit/cst_intdiv_rem.asm and
; cst_bitand_or.asm: checker/types/types.inc `rassert`s on an op appended after
; `AST_OP_DIV` that it does not know, deliberately, until its arms are widened.
; `ast_verify_stage1` runs -- it says nothing about operators -- and the
; checker does not.
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
	lea	rsi, [fx_src]
	mov	rdx, FX_SRC_LEN
	lea	rcx, [fx_path]
	mov	r8d, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run

	; ---- check 1: lexes and parses with NO diagnostics ----
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	test	rax, rax
	jnz	.fail1

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
	jc	.fail1
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	test	rax, rax
	jnz	.fail1

	; ---- check 2: the walk builds and verifies a tree ----
	lea	rdi, [fx_ast]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rcx, [fx_lx]
	mov	edx, [rcx + Lexer.file_id]
	lea	rdi, [fx_ast]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	test	rax, rax
	jz	.fail2
	mov	[fx_root], rax
	lea	rdi, [fx_ast]
	call	ast_verify_stage1

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

	; ---- check 3: every AST_BINARY node's aux, in node-index (= source)
	; order, is exactly `residuum`, `atque`, `sive`, `/` and the contextual
	; `atque` again, in the order they appear in the source above -- no
	; fewer, no more, no substitutions. `fx_expect` is that sequence; `fx_found` counts how
	; many have matched so far and doubles as the next expected slot.
	xor	r13, r13		; fx_found
	mov	r14, 1			; node id, 1-based
  .scan:
	lea	rdi, [fx_ast]
	call	ast_node_count
	cmp	r14, rax
	jg	.scandone
	lea	rdi, [fx_ast]
	mov	rsi, r14
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_BINARY
	jne	.next
	mov	rdx, FX_NEXPECT
	cmp	r13, rdx
	jae	.fail3			; one AST_BINARY too many
	movzx	edx, word [rax + AstNode.aux]
	lea	rbx, [fx_expect]
	mov	ecx, [rbx + r13 * 4]
	cmp	edx, ecx
	jne	.fail3
	inc	r13
  .next:
	inc	r14
	jmp	.scan
  .scandone:
	mov	rdx, FX_NEXPECT
	cmp	r13, rdx
	jne	.fail3			; too few: some op was never built

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

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

segment readable
  ; The UCD blobs, emitted exactly once and AFTER every `proc` in this file:
  ; a `segment readable` opened earlier would land the code that follows it in
  ; a non-executable segment, which assembles clean and segfaults on the first
  ; call (tests/unit/ast_from_cst.asm's header records the same fix).
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path:	db 'fixture.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_src:	db 'publica functio f() -> u32 {', 10
		db 9, 'firma a: u32 = 1;', 10
		db 9, 'firma b: u32 = 1;', 10
		db 9, 'firma atque: u32 = 1;', 10
		db 9, 'firma sive: u32 = 1;', 10
		db 9, 'firma p: u32 = a residuum b;', 10
		db 9, 'firma q: u32 = a atque b;', 10
		db 9, 'firma r: u32 = a sive b;', 10
		db 9, 'firma s: u32 = a / b;', 10
		db 9, 'firma t: u32 = atque atque sive;', 10
		db 9, 'redde p;', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src
  fx_expect:	dd AST_OP_REM, AST_OP_BAND, AST_OP_BOR, AST_OP_DIV
		dd AST_OP_BAND
  FX_NEXPECT = ($ - fx_expect) / 4
  fx_root:	rq 1
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
