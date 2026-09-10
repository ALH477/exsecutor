; tests/unit/ast_from_cst_member_generic.asm
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
; ast fixture: finding 7 of `ast/ast.inc`'s findings list (also
; `docs/design/typed-ast.md`'s "Findings 7-10 ... recorded and not yet acted
; on") -- generic arguments after a path segment reached through `.` have
; nowhere in section 2.3's table to go, because `Seg.b` (generic arguments on
; a WRITTEN path segment) belongs to `AST_PATH`/`AST_SEG`, and `.q` in
; expression position is an `AST_MEMBER`, whose row had no fourth slot at
; all.
;
; `ast/kinds.inc` and `ast/from_cst.inc`'s `__ast_w_postfix` already carry the
; fix -- `Member.c` is `AST_R_NODE` (optional) and holds the `AST_GENARGS`
; node when `Suffix ::= GenericArgs` (spec §8.6) follows a `.field` suffix --
; but nothing had ever exercised it: no `ast_*` fixture mentions
; `AST_GENARGS` or builds a member access with a generic argument at all.
; This fixture is that check, run rather than only reasoned about
; (CLAUDE.md, "prose designs are hypotheses until code runs").
;
; Two bindings isolate the slot's OPTIONALITY as well as its content: `a.q`
; (no generics) must leave `Member.c` 0, and `a.q<u32>` (one generic
; argument) must leave it pointing at an `AST_GENARGS` node whose one item is
; the `u32` type.
;
;	publica functio f() -> u32 {
;		firma a: u32 = 1;
;		firma m: u32 = a.q;
;		firma n: u32 = a.q<u32>;
;		redde m;
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

	; ---- check 3: find the two AST_MEMBER nodes, in node-index order.
	; The FIRST (`a.q`) must have `c` = 0; the SECOND (`a.q<u32>`) must
	; have `c` != 0 and pointing at an AST_GENARGS node.
	xor	r13, r13		; how many AST_MEMBER nodes seen
	mov	r14, 1
  .scan:
	lea	rdi, [fx_ast]
	call	ast_node_count
	cmp	r14, rax
	jg	.scandone
	lea	rdi, [fx_ast]
	mov	rsi, r14
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_MEMBER
	jne	.next
	mov	r12, rax		; the Member AstNode*
	test	r13, r13
	jnz	.second
	; ---- first Member: `a.q`, no generics ----
	mov	ecx, [r12 + AstNode.c]
	test	ecx, ecx
	jnz	.fail3
	inc	r13
	jmp	.next
  .second:
	mov	rdx, 2
	cmp	r13, rdx
	jae	.fail3			; a third AST_MEMBER: not expected
	; ---- second Member: `a.q<u32>`, one generic argument ----
	mov	ecx, [r12 + AstNode.c]
	test	ecx, ecx
	jz	.fail3
	lea	rdi, [fx_ast]
	mov	rsi, rcx
	call	ast_node_at
	movzx	edx, word [rax + AstNode.kind]
	cmp	edx, AST_GENARGS
	jne	.fail3
	mov	edx, [rax + AstNode.b]		; count
	cmp	edx, 1
	jne	.fail3
	inc	r13
  .next:
	inc	r14
	jmp	.scan
  .scandone:
	mov	rdx, 2
	cmp	r13, rdx
	jne	.fail3			; fewer than two AST_MEMBER nodes found

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
		db 9, 'firma m: u32 = a.q;', 10
		db 9, 'firma n: u32 = a.q<u32>;', 10
		db 9, 'redde m;', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src
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
