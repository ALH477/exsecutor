; tests/unit/ast_cap_partial.asm
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
; ast NEGATIVE fixture: compiler/x86_64/ast/verify.inc, "all eleven or none".
;
; Spec §4.6 has ELEVEN atoms and `docs/design/checker.md` indexes them by
; ordinal in three separate places -- a scope frame's `cap[11]`, the
; declaration id `N + k`, and row order. A tree carrying ten of them is not a
; smaller capability set: it is a tree in which some ordinal silently names
; the wrong atom, and every row built from it is wrong in a way no later pass
; can detect.
;
; Nothing has to be poked to reach this. `ast_decl` takes a kind, so a single
; `AST_D_CAPATOM` is one ordinary call -- exactly what a checker written to
; push atoms itself would do if it pushed them one at a time and stopped
; early, or what a hand-written `.ast` fixture does if it lists nine of them.
; `ast_verify` must trap.
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
	lea	rsi, [fx_t_m]
	mov	rdx, 6
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

	; One atom, correctly formed in every other way -- the right kind, no
	; node, no parent, no flags, a real name. The only thing wrong with it
	; is that it is alone.
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_CAPATOM
	xor	rdx, rdx
	mov	rcx, [fx_nm]
	xor	r8, r8
	lea	r9, [fx_sp]
	call	ast_decl

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

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_t_m:	db 'Mundus'
  fx_nm:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_sp:	rb sizeof.Span
