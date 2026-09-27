; tests/unit/ast_from_cst_impl_noname.asm
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
; ast fixture -- `AST_D_IMPL`'s `name` field must be 0.
;
; `checker/resolve/resolve.inc`'s `__chk_mods` (`.scoped:`) states the
; invariant in a comment immediately above the check that enforces it: "an
; `Impl` head introduces no name". `__ast_w_iface`'s implementation branch
; (`from_cst.inc`) once gave `AST_D_IMPL` the TRAIT's own last-segment name
; instead -- a leftover `mov r8, r13` copied from the sibling `.declaration`
; branch a few lines below, where `r13` genuinely IS the name being declared
; (a plain `interfacies P { ... }` names `P`). In the implementation form,
; `interfacies P in T { ... }`, that same `r13` is `P`'s own name, computed
; only so `.impl_noname:` could (wrongly) hand it to `__ast_w_decl` as the
; new declaration's name. The result: `interfacies Scriptor in Retis { ... }`
; put a SECOND module-scope declaration named `Scriptor` into the tree,
; colliding with the trait's own -- a spurious `EXS-E0302` that had nothing
; to do with any real duplicate (spec §14 entries 12 and 24 both hit it).
;
; NON-VACUITY. Check 3 requires exactly one `AST_D_IMPL` declaration in the
; tree (so the scan cannot pass by finding nothing) and requires its `name`
; field to be exactly 0. Before the fix this fixture's tree carried `name` =
; the interned id of `Scriptor` (2, in this source) on that very declaration
; -- confirmed against `exsc aedifica --emitte ast` on the reverted source
; before writing this assertion, not assumed from the diagnosis alone.
;
;	interfacies Scriptor {
;	    functio scribe(s: Scriptor) -> u32
;	}
;
;	structura Retis { n: u32 }
;
;	interfacies Scriptor in Retis {
;	    functio scribe(s: Retis) -> u32 {
;	        redde s.n;
;	    }
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
	lea	rdi, [fx_ast]
	call	ast_verify_stage1

	; ---- check 3: exactly one AST_D_IMPL declaration, name field 0 ----
	xor	r13, r13		; how many AST_D_IMPL declarations seen
	mov	r14, 1
  .scan:
	lea	rdi, [fx_ast]
	call	ast_decl_count
	cmp	r14, rax
	jg	.scandone
	lea	rdi, [fx_ast]
	mov	rsi, r14
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_IMPL
	jne	.next
	inc	r13
	mov	edx, 2
	cmp	r13, rdx
	jae	.fail3			; a second AST_D_IMPL: not expected
	mov	ecx, [rax + AstDecl.name]
	test	ecx, ecx
	jnz	.fail3			; "an Impl head introduces no name"
  .next:
	inc	r14
	jmp	.scan
  .scandone:
	mov	rdx, 1
	cmp	r13, rdx
	jne	.fail3			; not exactly one AST_D_IMPL found

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
  fx_src:	db 'interfacies Scriptor {', 10
		db 9, 'functio scribe(s: Scriptor) -> u32', 10
		db '}', 10
		db 10
		db 'structura Retis { n: u32 }', 10
		db 10
		db 'interfacies Scriptor in Retis {', 10
		db 9, 'functio scribe(s: Retis) -> u32 {', 10
		db 9, 9, 'redde s.n;', 10
		db 9, '}', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src
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
