; tests/unit/ast_verify_overlap.asm
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
; ast NEGATIVE fixture: compiler/x86_64/ast/verify.inc, scope nesting.
;
; The positive case is tests/unit/ast_block_scopes.asm: nested and sibling
; declaration ranges. This is the third shape, the one no scope structure can
; produce -- two ranges that OVERLAP without either containing the other.
;
; It matters because docs/design/ssa-ir.md 2.8 emits one `release` per
; declaration in a block's range at every scope exit. Two overlapping ranges
; mean some declaration is released twice and some never, and neither shows up
; as anything but a use-after-free in a compiled program much later.
;
; Blocks with ranges [1,3) and [2,4) are built over four declarations.
; `ast_verify`'s stack walk must trap: the second range neither contains the
; first nor is disjoint from it.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 1024 * 1024
	call	arena_init
	jc	.setup
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 64
	call	intern_init
	lea	rdi, [fx_names]
	lea	rsi, [fx_t_p]
	mov	rdx, 1
	call	intern_id
	mov	[fx_nm], rax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_sp]
	mov	rsi, 7
	mov	rdx, 0
	mov	rcx, 1
	call	span_make
	mov	r12, 4
  .mkdecl:
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_BINDING
	xor	rdx, rdx
	mov	rcx, [fx_nm]
	xor	r8, r8
	lea	r9, [fx_sp]
	call	ast_decl
	dec	r12
	jnz	.mkdecl

	mov	rdi, AST_BLOCK
	mov	rsi, 2			; two declarations
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_node
	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, 1			; range [1,3)
	xor	rcx, rcx
	call	ast_node_cd

	mov	rdi, AST_BLOCK
	mov	rsi, 2
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_node
	lea	rdi, [fx_tree]
	mov	rsi, 2
	mov	rdx, 2			; range [2,4) -- overlaps [1,3)
	xor	rcx, rcx
	call	ast_node_cd

	lea	rdi, [fx_tree]
	call	ast_verify
	; Reached only if the check under test did NOT fire. A negative
	; fixture that exits cleanly proves nothing, so this is a FAILURE
	; distinct from the expected trap (exit 132).
	mov	rdi, 20
	call	sys_exit_group
  .setup:
	mov	rdi, 10
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

proc fx_node, kind, aux, a, b
	locals
	endl
	lea	rdi, [fx_tree]
	mov	rsi, [kind]
	mov	rdx, [aux]
	mov	rcx, [a]
	mov	r8, [b]
	lea	r9, [fx_sp]
	call	ast_node
	return
endp

segment readable
  ; ast/ast.inc now reaches cst/, which reaches the lexer, which
  ; references the UCD blobs -- emitted exactly once, in a segment
  ; the consumer chooses (lexer/lexer.inc's header).
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_t_p:	db 'p'
  fx_nm:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_sp:	rb sizeof.Span
