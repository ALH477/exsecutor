; tests/unit/ast_from_cst_abi_overflow.asm
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
; ast NEGATIVE fixture: finding 10 of `ast/ast.inc`'s findings list (also
; `docs/design/typed-ast.md`'s "Findings 7-10 ... recorded and not yet acted
; on") -- `Externus.aux` is SIXTEEN BITS but an ABI id is an interner id, so
; a module that has interned more than 65535 distinct strings before its
; `externus` block cannot store the ABI id there. `ast/from_cst.inc`'s
; `__ast_w_externus` already, deliberately, `rassert`s rather than silently
; truncating -- the finding's own words are "it traps rather than
; truncating" -- but nothing had ever driven the walker past 65535 interned
; strings to prove that. This fixture is that proof.
;
; It is a NEGATIVE fixture, same shape as tests/unit/ast_build_postorder.asm:
; it preseeds the shared interner with 65536 distinct 4-byte entries (ids 1
; through 65536) before lexing anything, then lexes and parses one minimal
; `externus` block --
;
;	externus("libc", abi: C) {
;	}
;
; -- whose `abi: C` identifier is necessarily a BRAND NEW token (nothing in
; the fixture's own source was preseeded), so it is assigned some id
; strictly greater than 65535. `ast_from_cst` reaching that block's walker
; must therefore trap: `expect-exit=132` is SIGILL (128+4), `rassert`'s
; `ud2` (see tests/unit/rassert_trap.asm). Reaching the clean-exit path
; below the call would mean the id fit after all, or the check silently
; stopped firing -- both are FAILURES, distinct from the expected trap, and
; use the same "proves nothing if it exits cleanly" reasoning
; ast_build_postorder.asm's header explains.
;
; WHAT THIS FIXTURE DOES NOT DO: fix `Externus.aux`'s width. That is a
; `struct` layout change (`ast/node.inc`'s `AstNode.aux` is `dw`, shared by
; every kind, not just `Externus`), out of scope for this agent by
; CLAUDE.md's own instruction to this wave ("Do not change any `struct`
; layout in `ast/`"). The finding's own honest fix -- an ABI enumeration
; small enough to always fit, replacing the raw interner id -- additionally
; needs spec §5.3 to name more than one ABI first (today it names exactly
; one, `"C"`), which is a spec amendment, not an `ast/` change. This fixture
; only proves the INTERIM behavior (trap, not corruption) still holds.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 64 * 1024 * 1024
	call	arena_init
	jc	.setup
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 131072		; power of 2, comfortably > 65536 ids
	call	intern_init

	; ---- preseed: 65536 distinct 4-byte entries, ids 1..65536 ----
	xor	r12, r12
  .preseed:
	mov	[fx_ctr], r12d
	lea	rdi, [fx_names]
	lea	rsi, [fx_ctr]
	mov	rdx, 4
	call	intern_id
	inc	r12
	cmp	r12, 65536
	jl	.preseed

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
	jc	.setup

	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	test	rax, rax
	jnz	.setup

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
	jc	.setup
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	test	rax, rax
	jnz	.setup

	lea	rdi, [fx_ast]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rcx, [fx_lx]
	mov	edx, [rcx + Lexer.file_id]
	lea	rdi, [fx_ast]
	lea	rsi, [fx_ctree]
	call	ast_from_cst		; expected to `rassert`-trap in here

	; Reached only if the ABI id fit after all, or the check silently
	; stopped firing. A negative fixture that exits cleanly proves
	; nothing, so this is a FAILURE distinct from the expected trap.
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
  fx_path:	db 'fixture.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_src:	db 'externus("libc", abi: C) {', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src
  fx_ctr:	dd 0
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
