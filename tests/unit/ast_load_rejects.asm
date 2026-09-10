; tests/unit/ast_load_rejects.asm
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
; ast NEGATIVE fixture: compiler/x86_64/ast/load.inc goes through the
; constructors, so a hand-written dump gets the same checks a walker would.
;
; That is a claim in load.inc's header, and it is the difference between a
; textual form that is a debugging convenience and one that can be trusted as
; test input for Stage 2. The dump below is well-formed as TEXT -- correct
; magic, correct section order, correct field count on every line -- and names
; a child with a larger index than its parent, which postorder forbids.
;
; The expected outcome is `rassert`'s trap inside `__ast_slot_check`, reached
; through `ast_node` from `ast_load`. A loader that wrote straight into the
; vecs would accept this silently and hand Stage 2 a tree with a cycle in it.
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
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init

	lea	rdi, [fx_tree]
	lea	rsi, [fx_text]
	mov	rdx, FX_TEXT_LEN
	call	ast_load

	; Reached only if the check under test did NOT fire.
	mov	rdi, 20
	call	sys_exit_group
  .setup:
	mov	rdi, 10
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

segment readable
  ; ast/ast.inc now reaches cst/, which reaches the lexer, which
  ; references the UCD blobs -- emitted exactly once, in a segment
  ; the consumer chooses (lexer/lexer.inc's header).
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_text:
	db 'astv 1 1 0 0 1 0 0 0 0 0', 10
	db 't 1 error 0 0 0 0 0 0', 10
	db 'n 1 Redde 0 0 2 0 0 0 7 0 1', 10
  FX_TEXT_LEN = $ - fx_text
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
