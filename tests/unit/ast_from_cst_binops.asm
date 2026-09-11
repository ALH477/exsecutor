; tests/unit/ast_from_cst_binops.asm
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
; ast fixture: the six additive-family operators come out of `ast_from_cst`
; as six DIFFERENT `AST_OP_*` values, not five collapsed onto one flag.
;
; ast/kinds.inc defines `AST_OP_ADD` `AST_OP_ADDW` `AST_OP_ADDS` `AST_OP_SUB`
; `AST_OP_SUBW` `AST_OP_SUBS` (spec §8.4's `+` `+%` `+|` `-` `-%` `-|`, all
; six "already in evidence" in the operator table) and `ast/from_cst.inc`'s
; `__ast_w_op` maps `PUN_MINUSPCT`/`PUN_MINUSBAR` to `AST_OP_SUBW`/
; `AST_OP_SUBS`, exactly as it maps `PUN_PLUSPCT`/`PUN_PLUSBAR` to
; `AST_OP_ADDW`/`AST_OP_ADDS` -- but until this fixture, nothing had ever
; asserted the AST op that comes out the far end for FOUR of those six:
; `tests/unit/ast_from_cst.asm` ("THE SEAM") exercises the walk end to end
; but its one line of source (`redde x;`) contains no binary operator at
; all, and no other `ast_*` fixture mentions `AST_OP_` or `AST_BINARY`.
; `AST_OP_ADD`/`AST_OP_SUB` (plain `+`/`-`) stay covered only implicitly, by
; every other fixture's arithmetic; this one is the first to check an `aux`
; value on an `AST_BINARY` node at all.
;
; `*%`/`*|` are NOT covered here: spec §8.4 says outright, in the same
; sentence that closes the comparison-word discussion, "the `*%`/`*|`
; families remain `[OPEN]`" -- there is no `PUN_STARPCT`/`PUN_STARBAR`
; token, no `AST_OP_MULW`/`AST_OP_MULS` constant (the multiplicative family
; ends at `AST_OP_MUL`; the three after it are the 5a/5b words below), and
; asserting an op that cannot be produced would not be testing anything.
;
; SINCE SPEC §8.6 GAINED LEVELS 5a AND 5b it also checks the three contextual
; operator WORDS `aut` `sursum` `deorsum` -> `AST_OP_AUT` `AST_OP_SURSUM`
; `AST_OP_DEORSUM`. Those reach `__ast_w_op` as `TK_IDENT` leaves under the
; new `XOR_EXPR`/`SHIFT_EXPR` nodes and are mapped by POSITION in the word
; table, so a table whose three new spellings were in the wrong order -- or a
; walker that did not route the two new node kinds to the binary fold at all
; -- fails check 3 here rather than in a later pass.
;
; The source declares two bindings and then one binary expression per
; operator, each assigned to its own binding, so the tree has exactly seven
; `AST_BINARY` nodes and postorder puts them in this exact source order
; (each `BindingStmt`'s initializer is fully built, `AST_BINARY` included,
; before the next statement in the block is even started):
;
;	publica functio f() -> u32 {
;		firma a: u32 = 1;
;		firma b: u32 = 1;
;		firma p: u32 = a -% b;
;		firma q: u32 = a -| b;
;		firma r: u32 = a +% b;
;		firma s: u32 = a +| b;
;		firma t: u32 = a aut b;
;		firma u: u32 = a sursum b;
;		firma v: u32 = a deorsum b;
;		redde p;
;	}
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
	; order, is exactly the four wrapping/saturating additive ops and the
	; three bit operators, in the order they appear in the source above --
	; no fewer, no more, no substitutions. `fx_expect` is that sequence; `fx_found` counts how
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
		db 9, 'firma p: u32 = a -% b;', 10
		db 9, 'firma q: u32 = a -| b;', 10
		db 9, 'firma r: u32 = a +% b;', 10
		db 9, 'firma s: u32 = a +| b;', 10
		db 9, 'firma t: u32 = a aut b;', 10
		db 9, 'firma u: u32 = a sursum b;', 10
		db 9, 'firma v: u32 = a deorsum b;', 10
		db 9, 'redde p;', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src
  fx_expect:	dd AST_OP_SUBW, AST_OP_SUBS, AST_OP_ADDW, AST_OP_ADDS
		dd AST_OP_AUT, AST_OP_SURSUM, AST_OP_DEORSUM
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
