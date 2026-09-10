; tests/unit/chk_row_intern.asm
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
; checker-rows fixture for compiler/x86_64/checker/rows/intern.inc -- the ONE
; API `docs/design/checker.md` section 6 gives two agents to share, so it is
; tested before anything is built on it.
;
; Every claim here is a SPEC claim, not an implementation detail:
;
;   1. `ast_init` leaves `Ast.rows` empty. Stage 1 fills no row table
;      (typed-ast.md section 2.6).
;   2. `{alloc, rete}` and `{rete, alloc}` are ONE row id -- typed-ast.md
;      section 2.6's "sorted on (kind, id) before interning", which is what
;      makes a row a set and not a list.
;   3. The stored items come back in (kind, id) ascending order, and for
;      atoms that is spec §4.6 order -- checker.md section 3's verifier
;      clause, and section 2.8's hidden-argument order.
;   4. The EMPTY row is a real row with a NON-ZERO id, distinct from "no
;      row". Spec §8.6 makes `poscit {}` explicit and spec §4.1 rule 6 gives
;      it a meaning; checker.md section 2.1 rule 3 makes a bare
;      `functio(A) -> B` the same row. If it were id 0 a bare fn type and one
;      with an unset row slot would intern to the same TYPE id (ast/
;      types.inc keys on `AstType.b`), and IR 2.9's hidden-argument count
;      would stop being a function of the type.
;   5. Duplicate items collapse; a row is a set.
;   6. `potestas Hospes = { alloc, archivum, horologium, ambitus }` (spec
;      §4.5, verbatim) FLATTENS to its four atoms at interning -- checker.md
;      section 2.1's table, last row, which is what keeps `sub` resolution
;      free of a lookup at draw time.
;   7. Substitution (spec §4.2): a callee row `{alloc, sicut 2}` with
;      argument 2 carrying `{rete}` becomes `{alloc, rete}`; `sicut 2` alone
;      with `{rete}` becomes exactly `{rete}`.
;   8. A substituted row that still carries an ordinal is RETURNED, not
;      trapped -- the three ways checker.md section 2.1's table names (no
;      such argument, argument with no row, argument whose own row names its
;      own parameter). `chk_row_ords` non-zero is the finding.
;   9. `chk_row_subset` in both directions -- the §4.4 ceiling test that
;      `EXS-E0510` is raised on.
;  10. `chk_row_union` is the monotone join pass 3's fixpoint iterates.
;  11. Row 0 reads as the empty SET while remaining a distinct id.
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
	mov	rsi, 4 * 1024 * 1024
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

	; ---- the tree: one `potestas` declaration, then the eleven atoms ----
	; `ast_cap_push` requires the atoms to be the LAST eleven declarations
	; (ast/build.inc), so every source declaration is pushed first.
	lea	rdi, [fx_span]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	lea	rdi, [fx_names]
	lea	rsi, [fx_hospes]
	mov	rdx, 6
	call	intern_id
	mov	[fx_nm], rax

	lea	rdi, [fx_tree]
	mov	rsi, AST_D_POTESTAS
	xor	rdx, rdx
	mov	rcx, [fx_nm]
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl
	mov	[fx_pot], rax			; the `potestas` declaration

	lea	rdi, [fx_tree]
	call	ast_cap_push
	mov	[fx_base], rax			; `Mundus`'s declaration id

	; ---- 1: Stage 1 fills no rows ----
	lea	rdi, [fx_tree]
	mov	rax, [rdi + Ast.rows.len]
	test	rax, rax
	jnz	.fail1

	; ---- 2: {alloc, rete} == {rete, alloc} ----
	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_ALLOC - 1
	mov	[fx_items + 4], eax
	mov	rax, [fx_base]
	mov	dword [fx_items + 8], CHK_ROW_ATOM
	add	rax, AST_CAP_RETE - 1
	mov	[fx_items + 12], eax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 2
	call	chk_row_intern
	cmp	rax, 1
	jne	.fail2
	mov	[fx_r1], rax

	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_RETE - 1
	mov	[fx_items + 4], eax
	mov	rax, [fx_base]
	mov	dword [fx_items + 8], CHK_ROW_ATOM
	add	rax, AST_CAP_ALLOC - 1
	mov	[fx_items + 12], eax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 2
	call	chk_row_intern
	mov	rcx, [fx_r1]
	cmp	rax, rcx
	jne	.fail2
	lea	rdi, [fx_tree]
	mov	rax, [rdi + Ast.rows.len]
	cmp	rax, 1				; still ONE row, not two
	jne	.fail2

	; ---- 3: items ascending, atoms in §4.6 order ----
	lea	rdi, [fx_tree]
	mov	rsi, [fx_r1]
	call	chk_row_items
	cmp	rdx, 2
	jne	.fail3
	mov	ecx, [rax + ChkRowItem.kind]
	cmp	ecx, CHK_ROW_ATOM
	jne	.fail3
	mov	ecx, [rax + ChkRowItem.id]
	mov	rdx, [fx_base]
	add	rdx, AST_CAP_ALLOC - 1		; `alloc` is §4.6 ordinal 2
	cmp	rcx, rdx
	jne	.fail3
	mov	ecx, [rax + 8 + ChkRowItem.id]
	mov	rdx, [fx_base]
	add	rdx, AST_CAP_RETE - 1		; `rete` is ordinal 6, so it
	cmp	rcx, rdx			; is SECOND, whichever order
	jne	.fail3				; the caller wrote them in
	lea	rdi, [fx_tree]
	mov	rsi, [fx_r1]
	call	chk_row_atoms
	cmp	rax, 0x22			; bits 1 and 5
	jne	.fail3

	; ---- 4: the empty row is real, and is not "no row" ----
	lea	rdi, [fx_tree]
	xor	rsi, rsi
	xor	rdx, rdx
	call	chk_row_intern
	cmp	rax, CHK_ROW_NONE
	je	.fail4
	cmp	rax, 2
	jne	.fail4
	mov	[fx_re], rax
	lea	rdi, [fx_tree]
	call	chk_row_empty
	mov	rcx, [fx_re]
	cmp	rax, rcx			; idempotent
	jne	.fail4
	lea	rdi, [fx_tree]
	mov	rsi, [fx_re]
	call	chk_row_len
	test	rax, rax
	jnz	.fail4
	lea	rdi, [fx_tree]
	mov	rsi, [fx_re]
	call	chk_row_items
	test	rax, rax			; no list; offset 0 (ast/node.inc)
	jnz	.fail4
	test	rdx, rdx
	jnz	.fail4

	; ---- 5: duplicates collapse ----
	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_RETE - 1
	mov	[fx_items + 4], eax
	mov	dword [fx_items + 8], CHK_ROW_ATOM
	mov	[fx_items + 12], eax
	mov	dword [fx_items + 16], CHK_ROW_ATOM
	mov	[fx_items + 20], eax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 3
	call	chk_row_intern
	mov	[fx_rr], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	chk_row_len
	cmp	rax, 1
	jne	.fail5

	; ---- 6: `potestas Hospes = { alloc, archivum, horologium, ambitus }` ----
	; The declaration's node is an `AST_POTESTAS` whose list holds four
	; `Path` nodes, each already resolved (`Path.d`) as pass 1 leaves them.
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_mark], rax
	mov	rcx, AST_CAP_ALLOC
	call	fx_path
	mov	rcx, AST_CAP_ARCHIVUM
	call	fx_path
	mov	rcx, AST_CAP_HOROLOGIUM
	call	fx_path
	mov	rcx, AST_CAP_AMBITUS
	call	fx_path
	lea	rdi, [fx_tree]
	mov	rsi, [fx_mark]
	call	ast_list_emit
	mov	[fx_off], rax
	mov	[fx_cnt], rdx
	lea	rdi, [fx_tree]
	mov	rsi, AST_POTESTAS
	xor	rdx, rdx
	mov	rcx, [fx_off]
	mov	r8, [fx_cnt]
	lea	r9, [fx_span]
	call	ast_node
	mov	[fx_pn], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_pot]
	call	ast_node_cd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_pot]
	mov	rdx, [fx_pn]
	call	ast_decl_node

	mov	dword [fx_items + 0], CHK_ROW_ATOM
	mov	rax, [fx_pot]
	mov	[fx_items + 4], eax		; the `potestas` DECLARATION
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 1
	call	chk_row_intern
	mov	[fx_rh], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	chk_row_len
	cmp	rax, 4				; one item in, four items out
	jne	.fail6
	lea	rdi, [fx_tree]
	mov	rsi, [fx_rh]
	call	chk_row_atoms
	cmp	rax, 0x9A			; bits 1, 3, 4, 7
	jne	.fail6
	lea	rdi, [fx_tree]
	mov	rsi, [fx_rh]
	call	chk_row_ords
	test	rax, rax
	jnz	.fail6

	; ---- 7: substitution (spec §4.2) ----
	; `{rete}` is the argument's row; `{sicut 2}` is the callee's.
	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_RETE - 1
	mov	[fx_items + 4], eax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 1
	call	chk_row_intern
	mov	[fx_rr], rax			; {rete}

	mov	dword [fx_items + 0], CHK_ROW_ORD
	mov	dword [fx_items + 4], 2
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 1
	call	chk_row_intern
	mov	[fx_rs], rax			; {sicut 2}

	mov	dword [fx_args + 0], 0
	mov	dword [fx_args + 4], 0
	mov	rax, [fx_rr]
	mov	[fx_args + 8], eax		; argument 2 carries {rete}
	lea	rdi, [fx_tree]
	mov	rsi, [fx_rs]
	lea	rdx, [fx_args]
	mov	rcx, 3
	call	chk_row_subst
	mov	rcx, [fx_rr]
	cmp	rax, rcx			; {sicut 2} -> exactly {rete}
	jne	.fail7

	; `{alloc, sicut 2}` -> `{alloc, rete}` == the row interned in check 2
	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_ALLOC - 1
	mov	[fx_items + 4], eax
	mov	dword [fx_items + 8], CHK_ROW_ORD
	mov	dword [fx_items + 12], 2
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 2
	call	chk_row_intern
	mov	[fx_rm], rax

	; A MIXED row is (kind, id) ascending too: kind 0 before kind 1, which
	; is what makes checker.md section 2.8's "the hidden arguments are the
	; row's ATOM items in row order" a prefix rather than a filter.
	lea	rdi, [fx_tree]
	mov	rsi, [fx_rm]
	call	chk_row_items
	cmp	rdx, 2
	jne	.fail7
	mov	ecx, [rax + ChkRowItem.kind]
	cmp	ecx, CHK_ROW_ATOM
	jne	.fail7
	mov	ecx, [rax + ChkRowItem.id]
	mov	rdx, [fx_base]
	add	rdx, AST_CAP_ALLOC - 1
	cmp	rcx, rdx
	jne	.fail7
	mov	ecx, [rax + 8 + ChkRowItem.kind]
	cmp	ecx, CHK_ROW_ORD
	jne	.fail7
	mov	ecx, [rax + 8 + ChkRowItem.id]
	cmp	ecx, 2
	jne	.fail7

	lea	rdi, [fx_tree]
	mov	rsi, [fx_rm]
	lea	rdx, [fx_args]
	mov	rcx, 3
	call	chk_row_subst
	mov	rcx, [fx_r1]
	cmp	rax, rcx
	jne	.fail7

	; ---- 8: an unsubstitutable ordinal survives, and is visible ----
	; (a) no such argument
	lea	rdi, [fx_tree]
	mov	rsi, [fx_rs]
	lea	rdx, [fx_args]
	mov	rcx, 1				; only one argument
	call	chk_row_subst
	mov	[fx_rx], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	chk_row_ords
	cmp	rax, 4				; bit 2 -- `sicut 2` intact
	jne	.fail8
	; (b) the argument's type carries no row
	mov	dword [fx_args + 8], 0
	lea	rdi, [fx_tree]
	mov	rsi, [fx_rs]
	lea	rdx, [fx_args]
	mov	rcx, 3
	call	chk_row_subst
	mov	rcx, [fx_rx]
	cmp	rax, rcx
	jne	.fail8
	; (c) the argument's own row names its own parameter
	mov	rax, [fx_rs]
	mov	[fx_args + 8], eax		; argument 2 carries {sicut 2}
	lea	rdi, [fx_tree]
	mov	rsi, [fx_rs]
	lea	rdx, [fx_args]
	mov	rcx, 3
	call	chk_row_subst
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	chk_row_ords
	cmp	rax, 4
	jne	.fail8

	; ---- 9: subset, both directions (spec §4.4) ----
	lea	rdi, [fx_tree]
	mov	rsi, [fx_rr]			; {rete}
	mov	rdx, [fx_r1]			; {alloc, rete}
	call	chk_row_subset
	cmp	rax, 1
	jne	.fail9
	lea	rdi, [fx_tree]
	mov	rsi, [fx_r1]
	mov	rdx, [fx_rr]
	call	chk_row_subset
	test	rax, rax			; {alloc, rete} </= {rete}
	jnz	.fail9
	lea	rdi, [fx_tree]
	mov	rsi, [fx_re]			; {} is a subset of everything
	mov	rdx, [fx_rr]
	call	chk_row_subset
	cmp	rax, 1
	jne	.fail9
	lea	rdi, [fx_tree]
	mov	rsi, [fx_rh]			; Hospes </= {alloc, rete}
	mov	rdx, [fx_r1]
	call	chk_row_subset
	test	rax, rax
	jnz	.fail9
	lea	rdi, [fx_tree]
	mov	rsi, [fx_rs]			; ordinals count too
	mov	rdx, [fx_r1]
	call	chk_row_subset
	test	rax, rax
	jnz	.fail9

	; ---- 10: union ----
	mov	rax, [fx_base]
	mov	dword [fx_items + 0], CHK_ROW_ATOM
	add	rax, AST_CAP_ALLOC - 1
	mov	[fx_items + 4], eax
	lea	rdi, [fx_tree]
	lea	rsi, [fx_items]
	mov	rdx, 1
	call	chk_row_intern
	lea	rdi, [fx_tree]
	mov	rsi, rax			; {alloc}
	mov	rdx, [fx_rr]			; {rete}
	call	chk_row_union
	mov	rcx, [fx_r1]
	cmp	rax, rcx			; == {alloc, rete}
	jne	.fail10
	lea	rdi, [fx_tree]
	mov	rsi, [fx_r1]
	mov	rdx, [fx_r1]
	call	chk_row_union			; idempotent
	mov	rcx, [fx_r1]
	cmp	rax, rcx
	jne	.fail10

	; ---- 11: row 0 reads as the empty set, and is not the empty row ----
	lea	rdi, [fx_tree]
	xor	rsi, rsi
	call	chk_row_atoms
	test	rax, rax
	jnz	.fail11
	lea	rdi, [fx_tree]
	xor	rsi, rsi
	call	chk_row_len
	test	rax, rax
	jnz	.fail11
	lea	rdi, [fx_tree]
	xor	rsi, rsi
	mov	rdx, [fx_rr]
	call	chk_row_subset
	cmp	rax, 1
	jne	.fail11
	lea	rdi, [fx_tree]
	xor	rsi, rsi
	xor	rdx, rdx
	call	chk_row_union			; union of two nothings is the
	mov	rcx, [fx_re]			; EMPTY ROW, which has an id
	cmp	rax, rcx
	jne	.fail11

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
  .fail5:
	mov	rdi, 15
	call	sys_exit_group
  .fail6:
	mov	rdi, 16
	call	sys_exit_group
  .fail7:
	mov	rdi, 17
	call	sys_exit_group
  .fail8:
	mov	rdi, 18
	call	sys_exit_group
  .fail9:
	mov	rdi, 19
	call	sys_exit_group
  .fail10:
	mov	rdi, 20
	call	sys_exit_group
  .fail11:
	mov	rdi, 21
	call	sys_exit_group

; A `Path` node with no segments whose `d` is the atom of §4.6 ordinal `rcx`,
; staged onto the builder's list stack. Not a `proc`: it takes its argument in
; a register and is the fixture's own helper, invoked four times.
  fx_path:
	; rcx = the §4.6 ordinal (1..11). `push rbp` is not bookkeeping: it
	; restores the 16-byte stack alignment SysV requires at every `call`
	; (docs/asm-conventions.md 2), which the `call` that reached here
	; broke.
	push	rbp
	mov	rax, [fx_base]
	add	rax, rcx
	dec	rax
	mov	[fx_dc], rax			; the atom's declaration id
	lea	rdi, [fx_tree]
	mov	rsi, AST_PATH
	xor	rdx, rdx
	xor	rcx, rcx			; no segment list
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_node
	mov	[fx_pn2], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_pn2]
	xor	rdx, rdx
	mov	rcx, [fx_dc]			; `Path.d`, as pass 1 leaves it
	call	ast_node_cd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_pn2]
	call	ast_list_push
	pop	rbp
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/rows/rows.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_span:	rb sizeof.Span
  fx_items:	rb 8 * 16
  fx_args:	rd 8
  fx_nm:	rq 1
  fx_pot:	rq 1
  fx_pn:	rq 1
  fx_base:	rq 1
  fx_mark:	rq 1
  fx_off:	rq 1
  fx_cnt:	rq 1
  fx_r1:	rq 1
  fx_re:	rq 1
  fx_rr:	rq 1
  fx_rs:	rq 1
  fx_rh:	rq 1
  fx_rx:	rq 1
  fx_rm:	rq 1
  fx_pn2:	rq 1
  fx_dc:	rq 1
  fx_hospes:	db 'Hospes'
