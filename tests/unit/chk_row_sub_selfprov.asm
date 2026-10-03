; tests/unit/chk_row_sub_selfprov.asm
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
; checker-rows fixture for pass 3: a `sub`'s capability provision is its own
; LEXICAL SCOPE, mirroring the §4.5 frame walk the lowering does. The checker
; is required to be the authority for this guarantee (CLAUDE.md), so a draw the
; lowering refuses EXS-E0421 the checker must refuse too, and -- the other half,
; the one an earlier version of this change got wrong -- a draw the lowering
; ACCEPTS the checker must accept.
;
; `__chk_row_eff` provides a `sub P = e`'s atom:
;   - STATEMENT form (`sub P = e;`): from just AFTER the `sub` to the end of
;     its enclosing block; NOT to its own initializer `e`, NOT to a draw
;     textually before it, NOT past the block.
;   - BLOCK form (`sub P = e { body }`): WITHIN `body` only; still not to `e`.
;
; Four twins of one hand-built tree -- `getatom()` draws `archivum`; `f` is
; `publica` with the empty declared row, so it holds `archivum` only through
; whatever `sub` is in scope -- differing in where the draw sits:
;
;   1. SELF-PROVISION. `sub archivum = getatom();` (statement form) with a
;      SECOND `getatom()` as a sibling statement after it. Exactly one
;      EXS-E0421, at the call in the `sub`'s own initializer (span 4242); the
;      sibling draw (span 5151) stays covered. The `sub` does not provide to
;      its own `e`.
;   2. AFTER, COVERED (positive control). Initializer is an inert path; the one
;      `getatom()` is the sibling statement AFTER the `sub`. Zero diagnostics:
;      the `sub` provides `archivum` to the rest of its block.
;   3. BEFORE, REFUSED. The `getatom()` is a statement BEFORE the `sub` (span
;      4242). One EXS-E0421: a `sub` provides nothing textually before itself,
;      exactly as the lowering's frame walk binds only from the `sub` onward.
;   4. BLOCK FORM, COVERED (positive control, the regression this twin guards).
;      `sub archivum = <inert> { getatom(); }` -- the draw is inside the block
;      the `sub` binds for. Zero diagnostics. An earlier version excluded the
;      block (`Sub.c`) as if it were the initializer and wrongly refused this;
;      `Sub.c` is where the binding is LIVE.
;
; Mutation guards: revert the fix (the original function-scoped `sub` provider)
; and twin 1's self-draw is accepted (twin 1 fails); the earlier interim fix
; (exclude both `Sub.b` and `Sub.c`) refuses twin 4's block draw (twin 4 fails)
; and accepts twin 3's earlier draw (twin 3 fails). Each invariant has a twin
; that fails without it.
;
; `[UNTESTED]` on real source for the same reason `chk_row_e0421.asm` is: pass
; 3 reads `Decl.ty`, `Node.ty` and `Path.d`, which the types and resolve passes
; fill, and this fixture fills them by hand. (Measured end to end too: the
; four-line `sub archivum = getatom()` reproducer is EXS-E0421 at the checker
; and the lowering both; `tests/programs/sub_blockform/` and archivum_profundum
; -- block-form and statement-form legitimate draws -- build and run clean.)
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
	lea	rdi, [fx_scr]
	mov	rsi, 2 * 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_iar]
	mov	rsi, 256 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_iar]
	mov	rdx, 64
	call	intern_init

	; ========= 1: self-provision -- the sub's own initializer draw =========
	mov	qword [fx_mode], 0
	call	fx_build
	call	fx_run
	cmp	qword [fx_ndiag], 1		; exactly one -- the sibling is covered
	jne	.fail1
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 421
	jne	.fail1
	cmp	rdx, 4242			; the initializer call, NOT the sibling
	jne	.fail1

	; ========= 2: a draw AFTER the sub is covered (positive control) =======
	mov	qword [fx_mode], 1
	call	fx_build
	call	fx_run
	cmp	qword [fx_ndiag], 0
	jne	.fail2

	; ========= 3: a draw BEFORE the sub is refused =========================
	mov	qword [fx_mode], 2
	call	fx_build
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail3
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 421
	jne	.fail3
	cmp	rdx, 4242			; the earlier call
	jne	.fail3

	; ========= 4: a draw inside the BLOCK form's block is covered ==========
	mov	qword [fx_mode], 3
	call	fx_build
	call	fx_run
	cmp	qword [fx_ndiag], 0
	jne	.fail4

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

; ---------------------------------------------------------------------------
; Run pass 3 over the tree with a fresh `ChkCtx` and a fresh scratch arena.
; `chk_resolve` is NOT run: the tree is hand-built with `Path.d` and `Node.ty`
; already filled, and the draws that matter are CALL-induced, which are
; pass 3's (chk_row_e0421.asm's `fx_run` header gives the full reasoning).
  fx_run:
	push	rbp
	mov	qword [fx_ndiag], 0
	lea	rdi, [fx_scr]
	call	arena_reset
	lea	rdi, [fx_dvec]
	lea	rsi, [fx_scr]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_ctx]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_dvec]
	lea	rcx, [fx_scr]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_ctx]
	call	chk_rows
	mov	[fx_ndiag], rax
	pop	rbp
	ret

; rax = buffered diagnostic rsi's code, rdx = its caret offset.
  fx_diag:
	push	rbp
	lea	rdi, [fx_ctx]
	add	rdi, ChkCtx.dbuf
	call	vec_get
	mov	edx, [rax + ChkDiag.caret.start]
	mov	eax, [rax + ChkDiag.code]
	pop	rbp
	ret

; `ast_node`, with the fixture's span and no `c`/`d`.
;   rsi = kind, rdx = aux, rcx = a, r8 = b  ->  rax = the node id
  fx_node:
	push	rbp
	lea	rdi, [fx_tree]
	lea	r9, [fx_span]
	call	ast_node
	pop	rbp
	ret

; `ast_node_cd`, positionally: rsi = node, rdx = c, rcx = d.
  fx_setcd:
	push	rbp
	lea	rdi, [fx_tree]
	call	ast_node_cd
	pop	rbp
	ret

; A `Path` node with no segments resolving to declaration rcx.
  fx_path:
	push	rbp
	mov	[fx_t1], rcx
	mov	rsi, AST_PATH
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_t2], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_t1]
	call	fx_setcd
	mov	rax, [fx_t2]
	pop	rbp
	ret

; A `RowItem` naming the atom whose §4.6 ordinal is rcx, staged onto the list.
  fx_ri_atom:
	push	rbp
	mov	rax, [fx_base]
	add	rax, rcx
	dec	rax
	mov	[fx_t3], rax
	mov	rcx, rax
	call	fx_path
	mov	rsi, AST_ROWITEM
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	[fx_t4], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_t3]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t4]
	call	ast_list_push
	pop	rbp
	ret

; A `getatom()` call: callee = a path to declaration 1 (`getatom`, declared row
; `{archivum}`), no arguments, span start = rcx. -> rax = the Call node.
  fx_call_getatom:
	push	rbp
	mov	[fx_t7], rcx
	xor	rcx, rcx
	inc	rcx				; `getatom`'s declaration
	call	fx_path
	mov	[fx_callee], rax
	mov	rax, [fx_t7]
	mov	[fx_span + Span.start], eax
	mov	rsi, AST_CALL
	xor	rdx, rdx
	mov	rcx, [fx_callee]
	xor	r8, r8
	call	fx_node
	mov	[fx_t6], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, [fx_t6]
	xor	rdx, rdx			; no arguments
	xor	rcx, rcx
	call	fx_setcd
	mov	rax, [fx_t6]
	pop	rbp
	ret

; ---------------------------------------------------------------------------
;     functio getatom() -> archivum poscit archivum { }    // declared, no body
;     publica functio f() {  <body per fx_mode>  }
;
; getatom has no body -> DECLARED, written row `{archivum}`. f is `publica`
; with no `poscit` -> DECLARED empty row, so it absorbs nothing and an
; uncovered draw surfaces as a diagnostic.
  fx_build:
	push	rbp
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_span]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	; ---- declarations: getatom (1), f (2), the sub (3), then the atoms ----
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FN
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 1 getatom
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FN
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 2 f
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_SUB
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, 2				; parent = f
	lea	r9, [fx_span]
	call	ast_decl			; 3 the `sub archivum`
	lea	rdi, [fx_tree]
	call	ast_cap_push
	mov	[fx_base], rax

	; ---- the type `archivum`, for the `sub`'s declaration ----
	mov	rax, [fx_base]
	add	rax, AST_CAP_ARCHIVUM - 1
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_CAP
	mov	rdx, rax
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_cap], rax
	lea	rdi, [fx_tree]
	mov	rsi, 3
	mov	rdx, [fx_cap]
	call	ast_decl_ty

	; ---- getatom (1): `poscit archivum`, no body ----
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	mov	rcx, AST_CAP_ARCHIVUM
	call	fx_ri_atom
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_ROW
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 1
	call	fx_node
	mov	[fx_grow], rax
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_t5], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_grow]
	call	fx_setcd
	mov	rsi, AST_FN
	xor	rdx, rdx
	mov	rcx, [fx_t5]
	xor	r8, r8				; no body: DECLARED
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, [fx_t6]
	call	ast_decl_node

	; ---- f (2): `publica`, empty declared row, body per mode ----
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_fsig], rax			; no row node: empty declared row

	; mode 2 only: an EARLIER draw, a statement BEFORE the sub (built first so
	; its ids precede the sub's)
	mov	qword [fx_early], 0
	cmp	qword [fx_mode], 2
	jne	.no_early
	mov	rcx, 4242
	call	fx_call_getatom
	mov	[fx_early], rax
  .no_early:

	; the bound pattern -- the LHS `archivum` of `sub archivum = ...`. `Sub`
	; slot `a` is required (`AST_R_NODEQ`); pass 3 does not read it.
	xor	rcx, rcx
	call	fx_path
	mov	[fx_lhs], rax

	; the initializer (`Sub.b`). Mode 0 draws `archivum` in it; the others use
	; an inert path that draws nothing.
	cmp	qword [fx_mode], 0
	jne	.init_inert
	mov	rcx, 4242
	call	fx_call_getatom
	mov	[fx_rhs], rax
	jmp	.init_done
  .init_inert:
	xor	rcx, rcx
	call	fx_path
	mov	[fx_rhs], rax
  .init_done:

	; mode 3 only: the BLOCK form's block (`Sub.c`), a `getatom()` draw inside
	mov	qword [fx_cblk], 0
	cmp	qword [fx_mode], 3
	jne	.no_cblk
	mov	rcx, 5151
	call	fx_call_getatom
	mov	[fx_t3], rax
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t3]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_BLOCK
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 1
	call	fx_node
	mov	[fx_cblk], rax
	mov	rsi, rax
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_setcd
  .no_cblk:

	; the `Sub` node: a = pattern, b = initializer, c = block (mode 3) or 0,
	; d = the sub's declaration (3)
	mov	rsi, AST_SUB
	xor	rdx, rdx
	mov	rcx, [fx_lhs]
	mov	r8, [fx_rhs]
	call	fx_node
	mov	[fx_subn], rax
	mov	rsi, rax
	mov	rdx, [fx_cblk]			; c = the block form's body, or 0
	mov	rcx, 3				; d = the sub's declaration
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 3
	mov	rdx, [fx_subn]
	call	ast_decl_node

	; the sibling draw AFTER the sub (modes 0 and 1), else an inert redde value
	cmp	qword [fx_mode], 1
	je	.sib
	cmp	qword [fx_mode], 0
	je	.sib
	xor	rcx, rcx
	call	fx_path				; inert: no draw after the sub
	mov	[fx_sib], rax
	jmp	.sib_done
  .sib:
	mov	rcx, 5151
	call	fx_call_getatom
	mov	[fx_sib], rax
  .sib_done:

	; `redde <sib>;`
	mov	rsi, AST_REDDE
	xor	rdx, rdx
	mov	rcx, [fx_sib]
	xor	r8, r8
	call	fx_node
	mov	[fx_redde], rax

	; the body block: [ (early;) sub ; redde ]
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	cmp	qword [fx_early], 0
	je	.no_early_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_early]
	call	ast_list_push
  .no_early_push:
	lea	rdi, [fx_tree]
	mov	rsi, [fx_subn]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_redde]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_BLOCK
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 2
	cmp	qword [fx_early], 0
	je	.blk_count
	mov	r8, 3
  .blk_count:
	call	fx_node
	mov	[fx_t2], rax
	mov	rsi, rax
	xor	rdx, rdx
	xor	rcx, rcx
	call	fx_setcd

	mov	rsi, AST_FN
	mov	rdx, AST_FN_PUBLICA
	mov	rcx, [fx_fsig]
	mov	r8, [fx_t2]
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 2
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 2
	mov	rdx, [fx_t6]
	call	ast_decl_node
	pop	rbp
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/rows/rows.inc'

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_arena:	rb sizeof.Arena
  fx_scr:	rb sizeof.Arena
  fx_iar:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_ctx:	rb sizeof.ChkCtx
  fx_dvec:	rb sizeof.Vec
  fx_span:	rb sizeof.Span
  fx_mode:	rq 1
  fx_ndiag:	rq 1
  fx_base:	rq 1
  fx_cap:	rq 1
  fx_grow:	rq 1
  fx_fsig:	rq 1
  fx_lhs:	rq 1
  fx_rhs:	rq 1
  fx_cblk:	rq 1
  fx_early:	rq 1
  fx_sib:	rq 1
  fx_subn:	rq 1
  fx_redde:	rq 1
  fx_callee:	rq 1
  fx_t1:	rq 1
  fx_t2:	rq 1
  fx_t3:	rq 1
  fx_t4:	rq 1
  fx_t5:	rq 1
  fx_t6:	rq 1
  fx_t7:	rq 1
