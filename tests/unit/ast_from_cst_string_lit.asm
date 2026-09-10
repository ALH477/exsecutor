; tests/unit/ast_from_cst_string_lit.asm
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
; ast fixture: finding 8 of `ast/ast.inc`'s findings list (also
; `docs/design/typed-ast.md`'s "Findings 7-10 ... recorded and not yet acted
; on") -- `typed-ast.md` section 2.3 says a STRING `Lit` holds "the decoded
; bytes id", but nothing in this compiler decodes a string escape (spec
; §8.4's literal grammar is `[OPEN]`), so `ast/from_cst.inc`'s `__ast_w_lit`
; already, deliberately, interns the token's RAW bytes -- quotes and all --
; for a string exactly as it does for a number. That is not a bug to fix in
; `ast/`: it is the only thing that exists to do, and the finding says so.
; Decoding is whoever closes §8.4's grammar, not this walker.
;
; What was missing was a fixture PROVING it, rather than a comment asserting
; it (CLAUDE.md, "prose designs are hypotheses until code runs"): no `ast_*`
; fixture builds a string literal at all. This one interns the literal's
; exact source bytes `"hi"` (quotes included) a SECOND time, independently,
; through the same interner the walk used, and checks that `intern_id` -- ids
; assigned by first use (rt/intern.inc) -- hands back the SAME id the `Lit`
; node's `a` slot holds; a walker that decoded the escapes (there are none
; here, but a future change could still strip the quotes) would produce a
; DIFFERENT id and this fixture would catch it.
;
;	publica functio f() -> u32 {
;		firma a = "hi";
;		redde a;
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

	; ---- check 3: intern the exact same bytes independently, and expect
	; the same id the interner already assigned during the walk.
	lea	rdi, [fx_names]
	lea	rsi, [fx_check_str]
	mov	rdx, FX_CHECK_LEN
	call	intern_id
	mov	rbx, rax		; the id `"hi"` (quotes included) resolves to

	; ---- check 4: find the one AST_LIT node and check its class and its
	; `a` (text id) against `rbx`.
	xor	r13, r13		; how many AST_LIT nodes seen
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
	cmp	ecx, AST_LIT
	jne	.next
	mov	r12, rax
	test	r13, r13
	jnz	.fail4			; a second AST_LIT: not expected
	movzx	ecx, word [r12 + AstNode.aux]
	cmp	ecx, AST_LIT_STRING
	jne	.fail4
	mov	ecx, [r12 + AstNode.a]
	cmp	rcx, rbx
	jne	.fail4
	inc	r13
  .next:
	inc	r14
	jmp	.scan
  .scandone:
	test	r13, r13
	jz	.fail4			; no AST_LIT found at all

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
  .fail4:
	mov	rdi, 14
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
		db 9, 'firma a = "hi";', 10
		db 9, 'redde a;', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src
  fx_check_str:	db '"hi"'
  FX_CHECK_LEN = $ - fx_check_str
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
