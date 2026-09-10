; tests/unit/ast_cap_not_last.asm
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
; ast NEGATIVE fixture: compiler/x86_64/ast/verify.inc, the atoms are the LAST
; eleven declarations.
;
; `ast_cap_base` (ast/build.inc) answers "does this tree have atoms, and at
; which id" with one bounds check and one kind test, because the atoms are
; always the final eleven -- `ast_cap_push` runs at the end of the walk
; (ast/from_cst.inc) and nothing pushes a declaration afterwards. That is a
; cheap answer resting on an invariant, and this is the invariant.
;
; A declaration pushed after the eleven does not merely make the lookup slow:
; it makes `ast_cap_base` return 0 on a tree that HAS atoms, so every later
; lookup of a capability silently misses and `docs/design/checker.md`'s
; `N + k` stops being true. `ast_verify` must trap rather than let the tree
; travel.
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

	; All eleven, pushed the one way that is meant to push them ...
	lea	rdi, [fx_tree]
	call	ast_cap_push
	; ... and then one more source declaration, as a walk that kept going
	; after the atoms were in would produce.
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_BINDING
	xor	rdx, rdx
	mov	rcx, [fx_nm]
	xor	r8, r8
	lea	r9, [fx_sp]
	call	ast_decl

	lea	rdi, [fx_tree]
	call	ast_verify
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
  fx_t_p:	db 'p'
  fx_nm:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_sp:	rb sizeof.Span
