; tests/unit/ast_cap_atoms.asm
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
; ast fixture for spec §4.6's ELEVEN CAPABILITY ATOMS as declarations, and for
; `AST_TY_CAP`, the type an annotation like `m: Mundus` or a `sub`'s
; `Decl.ty` interns to (docs/design/checker.md section 9, findings 1 and 2).
;
; THE ELEVEN SPELLINGS BELOW ARE WRITTEN OUT AGAIN, HERE, ON PURPOSE. Reading
; them out of `ast_cap_names` would make this fixture agree with any
; permutation of that pool -- the two sides would move together and the test
; would assert nothing. `fx_cap_tab` is an independent copy of §4.6's sentence
; ("`Mundus` (root), `alloc`, `sermo` (human language), `horologium` (clock),
; `archivum` (filesystem), `rete` (network), `fortuna` (randomness), `ambitus`
; (process environment), `Filum` (threads), `machina` ..., `Crudum` ..."), in
; that order, so swapping two entries in ast/kinds.inc fails check 3 naming
; the atom that moved. The order is load-bearing three times over in
; checker.md: a frame's `cap[11]` index, the declaration id `N + k`, and row
; sorting by ascending atom id.
;
; What each check pins:
;   1  a fresh tree has NO atoms -- `ast_cap_base` is 0, and a checker that
;      asks gets an answer rather than eleven decls that happen to be there
;   2  `ast_cap_push` puts them at `N + 1 .. N + 11` for a module of `N`
;      source declarations (checker.md section 2.2 pass 0), and the tree then
;      has exactly `N + 11`
;   3  atom `k` is §4.6's `k`-th, is `AST_D_CAPATOM`, and has NO node, NO
;      parent and NO flags -- it is a declaration with no declaration site
;   4  `ast_cap_of_name` inverts that: an interner id -> the §4.6 ordinal,
;      and 0 for a name that is not an atom's (`p`) or for id 0
;   5  `AST_TY_CAP` interns per ATOM: `rete` twice is one id, `alloc` is
;      another. This is what lets the checker compare capability types by id
;      (checker.md section 1, rule 5) instead of structurally
;   6  `ast_verify` accepts all of it, including its new rules for both
;   7  `CapAtom` and `cap` survive `ast_dump` -> `ast_load` -> `ast_equal`:
;      the dump prints kind NAMES, so both pool entries are load-bearing in
;      two directions
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
	lea	rdi, [fx_sp]
	mov	rsi, 7
	mov	rdx, 0
	mov	rcx, 1
	call	span_make

	; ---- check 1 ----
	lea	rdi, [fx_tree]
	call	ast_cap_base
	test	rax, rax
	jnz	.fail1

	; One ordinary declaration first, so that `N + k` is a claim with a
	; non-zero `N` in it: with an empty tree, "the last eleven" and "the
	; first eleven" would be the same eleven.
	lea	rdi, [fx_names]
	lea	rsi, [fx_t_p]
	mov	rdx, 1
	call	intern_id
	mov	[fx_nm], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FN
	xor	rdx, rdx
	mov	rcx, [fx_nm]
	xor	r8, r8
	lea	r9, [fx_sp]
	call	ast_decl
	cmp	rax, 1
	jne	.fail1

	; ---- check 2 ----
	lea	rdi, [fx_tree]
	call	ast_cap_push
	mov	r13, rax
	cmp	rax, 2			; N = 1, so `Mundus` is declaration 2
	jne	.fail2
	lea	rdi, [fx_tree]
	call	ast_decl_count
	cmp	rax, 1 + AST_CAP_COUNT
	jne	.fail2
	lea	rdi, [fx_tree]
	call	ast_cap_base
	cmp	rax, r13
	jne	.fail2

	; ---- checks 3 and 4, atom by atom ----
	mov	r12, 1
  .atom:
	mov	rcx, AST_CAP_COUNT
	cmp	r12, rcx
	jg	.atoms_done
	mov	rcx, r12
	dec	rcx
	shl	rcx, 4			; 16 bytes per (pointer, length) row
	lea	rax, [fx_cap_tab]
	add	rax, rcx
	lea	rdi, [fx_names]
	mov	rsi, [rax]
	mov	rdx, [rax + 8]
	call	intern_id
	mov	r14, rax		; this fixture's own id for §4.6's k-th

	lea	rdi, [fx_tree]
	lea	rsi, [r13 + r12 - 1]
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_CAPATOM
	jne	.fail3
	mov	ecx, [rax + AstDecl.name]
	cmp	rcx, r14
	jne	.fail3
	movzx	ecx, byte [rax + AstDecl.flags]
	test	ecx, ecx
	jnz	.fail3
	mov	ecx, [rax + AstDecl.node]
	test	ecx, ecx
	jnz	.fail3
	mov	ecx, [rax + AstDecl.parent]
	test	ecx, ecx
	jnz	.fail3

	lea	rdi, [fx_tree]
	mov	rsi, r14
	call	ast_cap_of_name
	cmp	rax, r12
	jne	.fail4
	inc	r12
	jmp	.atom
  .atoms_done:

	; ---- check 4, the two misses ----
	lea	rdi, [fx_tree]
	mov	rsi, [fx_nm]		; `p` -- an ordinary name
	call	ast_cap_of_name
	test	rax, rax
	jnz	.fail4
	lea	rdi, [fx_tree]
	xor	rsi, rsi		; the uninterned sentinel
	call	ast_cap_of_name
	test	rax, rax
	jnz	.fail4

	; ---- check 5 ----
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_CAP
	lea	rdx, [r13 + AST_CAP_RETE - 1]
	xor	rcx, rcx
	call	ast_type_una
	mov	r14, rax
	cmp	rax, 2			; type 1 is the error type; this is the
	jne	.fail5			; first thing this tree interned
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_CAP
	lea	rdx, [r13 + AST_CAP_RETE - 1]
	xor	rcx, rcx
	call	ast_type_una
	cmp	rax, r14		; the same atom is the same type
	jne	.fail5
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_CAP
	lea	rdx, [r13 + AST_CAP_ALLOC - 1]
	xor	rcx, rcx
	call	ast_type_una
	cmp	rax, r14		; a different atom is not
	je	.fail5

	; ---- check 6 ----
	; A trap here exits 132, which is neither 0 nor any of the codes
	; below, so a verifier failure is never mistaken for a check failure.
	lea	rdi, [fx_tree]
	call	ast_verify

	; ---- check 7: both new names survive the textual form ----
	; `CapAtom` and `cap` are entries in ast/kinds.inc's name pools, and
	; the dump prints NAMES rather than kind integers precisely so that a
	; renumbering of that file cannot silently reinterpret a tree someone
	; wrote by hand. That only holds if `ast_load` can read back what
	; `ast_dump` wrote, which is one round trip through both pools.
	lea	rdi, [fx_wr]
	lea	rsi, [fx_buf]
	mov	rdx, 8192
	call	diag_out_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_wr]
	call	ast_dump
	mov	eax, [fx_wr + DiagOut.trunc]
	test	eax, eax
	jnz	.fail7
	mov	rdi, 1
	lea	rsi, [fx_buf]
	mov	rdx, [fx_wr + DiagOut.len]
	call	sys_write

	lea	rdi, [fx_arena2]
	mov	rsi, 1024 * 1024
	call	arena_init
	jc	.setup
	lea	rdi, [fx_tree2]
	lea	rsi, [fx_arena2]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree2]
	lea	rsi, [fx_buf]
	mov	rdx, [fx_wr + DiagOut.len]
	call	ast_load
	lea	rdi, [fx_tree2]
	call	ast_verify
	lea	rdi, [fx_tree2]
	call	ast_cap_base
	cmp	rax, r13
	jne	.fail7
	lea	rdi, [fx_tree]
	lea	rsi, [fx_tree2]
	call	ast_equal
	cmp	rax, 1
	jne	.fail7

	xor	edi, edi
	call	sys_exit_group
  .setup:
	mov	rdi, 99
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
  .fail5:
	mov	rdi, 15
	call	sys_exit_group
  .fail7:
	mov	rdi, 17
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

segment readable
  ; ast/ast.inc reaches cst/, which reaches the lexer, which references the
  ; UCD blobs -- emitted exactly once, in a segment the consumer chooses.
  include '../../compiler/shared/unicode/tables/tables.inc'

  fx_c_mundus	db 'Mundus'
  fx_c_alloc	db 'alloc'
  fx_c_sermo	db 'sermo'
  fx_c_horol	db 'horologium'
  fx_c_archiv	db 'archivum'
  fx_c_rete	db 'rete'
  fx_c_fortuna	db 'fortuna'
  fx_c_ambitus	db 'ambitus'
  fx_c_filum	db 'Filum'
  fx_c_machina	db 'machina'
  fx_c_crudum	db 'Crudum'
fx_cap_tab:
	dq fx_c_mundus,  6
	dq fx_c_alloc,   5
	dq fx_c_sermo,   5
	dq fx_c_horol,  10
	dq fx_c_archiv,  8
	dq fx_c_rete,    4
	dq fx_c_fortuna, 7
	dq fx_c_ambitus, 7
	dq fx_c_filum,   5
	dq fx_c_machina, 7
	dq fx_c_crudum,  6
fx_cap_tab_end:
  ; A table that measured short would make the loop pass by not running.
  ; fasmg's NATIVE assemble-time `assert`, not `rassert`.
  assert (fx_cap_tab_end - fx_cap_tab) / 16 = AST_CAP_COUNT

segment readable writeable
  fx_t_p:	db 'p'
  fx_nm:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_sp:	rb sizeof.Span
  fx_arena2:	rb sizeof.Arena
  fx_tree2:	rb sizeof.Ast
  fx_wr:	rb sizeof.DiagOut
  fx_buf:	rb 8192
