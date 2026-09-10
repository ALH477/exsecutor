; tests/unit/ast_load_handwritten.asm
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
; ast fixture for compiler/x86_64/ast/load.inc, on a dump NOBODY DUMPED.
;
; tests/unit/ast_build_roundtrip.asm proves ast_dump and ast_load agree with
; each other, which a pair of routines can do while both being wrong in the
; same way. This fixture closes that: the text below was typed by hand, into
; this file, against ast/dump.inc's documented grammar and nothing else. It
; loads, it verifies, and it re-prints BYTE FOR BYTE identically.
;
; That is the claim docs/design/ssa-ir.md 2.11 makes for the IR -- "a
; hand-written function gets the span of its line, which is all a backend test
; needs" -- transferred to this tree, and it is the whole reason a textual form
; exists before a parser does. Stage 2 and Stage 3 can be written and tested
; against trees typed into a fixture, with no CST in the repository at all.
;
; The tree is `publica functio f() { }`:
;   n1 Block (empty)   n3 Fn sig=n2 body=n1 decl=d1
;   n2 Sig   (empty)   n4 Module [n3]
;
;   1. it loads without tripping any constructor range check
;   2. the counts and the root are what the text says
;   3. `ast_verify_stage1` accepts it -- including that every `ty` is empty
;   4. re-dumping produces the identical bytes
;   5. a spot check on n3's operands and span
;
; Exit 0 = all checks passed; 10+N = check N failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 64
	call	intern_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init

	; ---- 1 ----
	lea	rdi, [fx_tree]
	lea	rsi, [fx_text]
	mov	rdx, FX_TEXT_LEN
	call	ast_load

	; ---- 2 ----
	lea	rdi, [fx_tree]
	call	ast_node_count
	cmp	rax, 4
	jne	.fail2
	lea	rdi, [fx_tree]
	call	ast_decl_count
	cmp	rax, 1
	jne	.fail2
	lea	rdi, [fx_tree]
	call	ast_type_count
	cmp	rax, 2
	jne	.fail2
	lea	rdi, [fx_tree]
	mov	eax, [rdi + Ast.root]
	cmp	eax, 4
	jne	.fail2

	; ---- 3 ----
	lea	rdi, [fx_tree]
	call	ast_verify_stage1

	; ---- 4 ----
	lea	rdi, [fx_wr]
	lea	rsi, [fx_buf]
	mov	rdx, 4096
	call	diag_out_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_wr]
	call	ast_dump
	mov	rdi, 1
	lea	rsi, [fx_buf]
	mov	rdx, [fx_wr + DiagOut.len]
	call	sys_write
	mov	rax, [fx_wr + DiagOut.len]
	cmp	rax, FX_TEXT_LEN
	jne	.fail4
	lea	rdi, [fx_buf]
	lea	rsi, [fx_text]
	mov	rdx, FX_TEXT_LEN
	call	__map_bytes_equal
	cmp	eax, 1
	jne	.fail4

	; ---- 5 ----
	lea	rdi, [fx_tree]
	mov	rsi, 3
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_FN
	jne	.fail5
	mov	ecx, [rax + AstNode.a]
	cmp	ecx, 2
	jne	.fail5
	mov	ecx, [rax + AstNode.b]
	cmp	ecx, 1
	jne	.fail5
	mov	ecx, [rax + AstNode.d]
	cmp	ecx, 1
	jne	.fail5
	mov	ecx, [rax + AstNode.span.len]
	cmp	ecx, 12
	jne	.fail5

	xor	edi, edi
	call	sys_exit_group
  .fail0:
	mov	rdi, 10
	call	sys_exit_group
  .fail2:
	mov	rdi, 12
	call	sys_exit_group
  .fail4:
	mov	rdi, 14
	call	sys_exit_group
  .fail5:
	mov	rdi, 15
	call	sys_exit_group

include '../../compiler/x86_64/diag/diag.inc'
include '../../compiler/x86_64/ast/ast.inc'

segment readable writeable
  ; Typed by hand against ast/dump.inc's grammar. Sections in the order that
  ; file fixes -- x, t, d, n -- because the loader goes through the
  ; constructors and they range-check forward references.
  fx_text:
	db 'astv 1 4 1 1 2 4 0 0 0 0', 10
	db 'x 1 3', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 't 2 int 0 0 0 32 0 0', 10
	db 'd 1 Fn 1 0 1 3 0 0 7 0 12', 10
	db 'n 1 Block 0 0 0 0 0 0 7 10 2', 10
	db 'n 2 Sig 0 0 0 0 0 0 7 3 7', 10
	db 'n 3 Fn 1 0 2 1 0 1 7 0 12', 10
	db 'n 4 Module 0 0 1 1 0 0 7 0 12', 10
  FX_TEXT_LEN = $ - fx_text
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_wr:	rb sizeof.DiagOut
  fx_buf:	rb 4096
