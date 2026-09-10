; tests/unit/ast_from_cst_missing.asm
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
; ast fixture: finding 9 of `ast/ast.inc`'s findings list (also
; `docs/design/typed-ast.md`'s "Findings 7-10 ... recorded and not yet acted
; on") -- `typed-ast.md` section 2.11 says flatly "an `ERROR` or `MISSING`
; CST node becomes an `Error` node with its span", which `tests/unit/
; ast_from_cst_error.asm` already checks for `CST_ERROR`. It is only
; SOMETIMES true for `CST_MISSING`: `ast/from_cst.inc`'s walks iterate
; INTERIOR children, so a missing `;` -- a zero-width leaf carrying no
; meaning an `Error` OPERAND could stand in for -- is simply never visited,
; and putting one in `Redde.a` would replace a perfectly good expression
; with a hole. `ast/from_cst.inc`'s header already states this as the real
; rule; this fixture is the part that had never run it.
;
; The source is one function whose `redde` statement is missing its `;`:
;
;	publica functio f() -> u32 {
;		redde 1
;	}
;
; §8.6 requires the `;` (`'redde' [Expr] ';'`), so the parser reports and
; inserts a `CST_MISSING` leaf for it (§8.3's insertion-fix diagnostic) --
; checked here only as "it reports", the same way
; tests/unit/ast_from_cst_error.asm checks its own malformed input, not by
; picking apart which exact `EXS-E` code the parser chose (not this file's
; layer to re-litigate). What this fixture is actually FOR:
;
;   1. the parse reports (CF set, diagnostic vector non-empty) -- otherwise
;      checks 2-3 would be testing a clean file, not recovery
;   2. a root still comes back and `ast_verify_stage1` accepts the tree
;   3. NO node anywhere in the tree is `AST_ERROR` -- the missing `;` did
;      not get promoted into one
;   4. the `Redde` node's operand (`a`) is still the INT `Lit` for `1`,
;      intact -- not replaced by a hole
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
	jc	.fail0			; §8.1 must be clean: the defect under
					; test is a GRAMMAR defect, not a
					; source-policy one

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

	; ---- check 1: it really is malformed ----
	jnc	.fail1
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	test	rax, rax
	jz	.fail1

	; ---- check 2 ----
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

	; ---- check 3: no AST_ERROR node anywhere ----
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
	cmp	ecx, AST_ERROR
	je	.fail3
	inc	r14
	jmp	.scan
  .scandone:

	; ---- check 4: Redde.a is still the intact INT Lit for `1` ----
	xor	r13, r13		; found the Redde node?
	mov	r14, 1
  .scan2:
	lea	rdi, [fx_ast]
	call	ast_node_count
	cmp	r14, rax
	jg	.scan2done
	lea	rdi, [fx_ast]
	mov	rsi, r14
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_REDDE
	jne	.next2
	mov	ecx, [rax + AstNode.a]
	test	ecx, ecx
	jz	.fail4			; the hole finding 9 warns against
	lea	rdi, [fx_ast]
	mov	rsi, rcx
	call	ast_node_at
	movzx	edx, word [rax + AstNode.kind]
	cmp	edx, AST_LIT
	jne	.fail4
	movzx	edx, word [rax + AstNode.aux]
	cmp	edx, AST_LIT_INT
	jne	.fail4
	mov	r13, 1
  .next2:
	inc	r14
	jmp	.scan2
  .scan2done:
	test	r13, r13
	jz	.fail4			; no Redde node found at all

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
		db 9, 'redde 1', 10
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
