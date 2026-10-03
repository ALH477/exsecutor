; tests/unit/chk_row_e0510_permember.asm
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
; checker-rows fixture for the PER-MEMBER half of spec §4.4's ceiling, on a
; trait with MORE THAN ONE member -- the case chk_row_e0510.asm cannot reach
; because its single-member trait makes the union ceiling and the member's own
; row the same set.
;
; THE BUG THIS PINS. `__chk_row_e0510`'s per-member loop tested each impl
; member's written row against `__chk_row_ceil` -- the UNION of EVERY trait
; member's declared row -- instead of against the CORRESPONDING trait member's
; row (checker.md section 2.3: "a member-level `poscit` ⊆ THE TRAIT MEMBER'S").
; So one member's declared atom covered a DIFFERENT member's impl draw:
;
;     interfacies Duo {
;         functio a(self: Duo, x) poscit alloc      ; name 1
;         functio b(self: Duo, x) poscit rete        ; name 2
;     }
;     interfacies Duo in Right poscit alloc, rete {
;         functio a(self: Right, x) poscit rete      ; name 1 -- EXCEEDS a's {alloc}
;     }
;
; The union ceiling is {alloc, rete}, so the buggy check accepted impl `a`'s
; `rete`; member `a`'s own ceiling is {alloc}, so the correct check rejects it.
; A generic `<T: Duo>` calling `v.a(x)` attributes only `a`'s row {alloc} to
; the call, so an impl `a` that may draw `rete` launders it undeclared -- the
; §4.4 generic hole, reborn for multi-member traits. Measured ACCEPTED (check
; clean) before the fix; this fixture fails at check 1 if that regresses.
;
; The tree is built by hand (checker.md section 6): pass 3 reads `Node.ty` and
; `Path.d`, which the types and resolve passes fill, and this fixture fills
; them directly. The two trait members are given DISTINCT names (1 and 2) so
; the by-name lookup can tell them apart -- the one fact a real compilation
; always has and chk_row_e0510.asm's single member did not need.
;
;   1  impl `a` writes `poscit rete`, exceeding trait `a`'s {alloc}   E0510 at a
;   2  impl `a` writes `poscit alloc`, within trait `a`'s {alloc}      clean
;   3  impl `a` writes `poscit rete` while trait `a` ALSO declares
;      rete (so even the per-member ceiling admits it)                clean
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
	call	fx_build

	; ---- 1: impl `a` = {rete} exceeds trait `a` = {alloc} ----
	mov	qword [fx_ords], AST_CAP_RETE
	mov	rcx, 1
	call	fx_row
	lea	rdi, [fx_tree]
	mov	rsi, [fx_amsig]
	call	ast_node_at
	mov	ecx, [fx_rowtmp]
	mov	[rax + AstNode.d], ecx
	call	fx_run
	cmp	qword [fx_ndiag], 1
	jne	.fail1
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 510
	jne	.fail1
	cmp	rdx, 7070			; the impl method `a`, not the head
	jne	.fail1

	; ---- 2: impl `a` = {alloc} is within trait `a` = {alloc} ----
	mov	qword [fx_ords], AST_CAP_ALLOC
	mov	rcx, 1
	call	fx_row
	lea	rdi, [fx_tree]
	mov	rsi, [fx_amsig]
	call	ast_node_at
	mov	ecx, [fx_rowtmp]
	mov	[rax + AstNode.d], ecx
	call	fx_run
	test	rax, rax
	jnz	.fail2

	; ---- 3: impl `a` = {rete}, but trait `a` now ALSO declares rete ----
	; The per-member ceiling becomes {alloc, rete}, so the same impl row is
	; admitted -- proving the check reads the MEMBER's row, not a constant.
	mov	qword [fx_ords], AST_CAP_ALLOC
	mov	qword [fx_ords + 8], AST_CAP_RETE
	mov	rcx, 2
	call	fx_row
	lea	rdi, [fx_tree]
	mov	rsi, [fx_atsig]			; trait member a's Sig
	call	ast_node_at
	mov	ecx, [fx_rowtmp]
	mov	[rax + AstNode.d], ecx
	mov	qword [fx_ords], AST_CAP_RETE
	mov	rcx, 1
	call	fx_row
	lea	rdi, [fx_tree]
	mov	rsi, [fx_amsig]
	call	ast_node_at
	mov	ecx, [fx_rowtmp]
	mov	[rax + AstNode.d], ecx
	call	fx_run
	test	rax, rax
	jnz	.fail3

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

  fx_diag:
	push	rbp
	lea	rdi, [fx_ctx]
	add	rdi, ChkCtx.dbuf
	call	vec_get
	mov	edx, [rax + ChkDiag.caret.start]
	mov	eax, [rax + ChkDiag.code]
	pop	rbp
	ret

  fx_node:
	push	rbp
	lea	rdi, [fx_tree]
	lea	r9, [fx_span]
	call	ast_node
	pop	rbp
	ret

  fx_setcd:
	push	rbp
	lea	rdi, [fx_tree]
	call	ast_node_cd
	pop	rbp
	ret

; A `Path` node with no segments resolving to declaration rcx.
  fx_path:
	push	rbp
	mov	[fx_p1], rcx
	mov	rsi, AST_PATH
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_p2], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_p1]
	call	fx_setcd
	mov	rax, [fx_p2]
	pop	rbp
	ret

; A `RowItem` naming the atom of §4.6 ordinal rcx, staged.
  fx_ri_atom:
	push	rbp
	mov	rax, [fx_base]
	add	rax, rcx
	dec	rax
	mov	[fx_r1], rax
	mov	rcx, rax
	call	fx_path
	mov	rsi, AST_ROWITEM
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	[fx_r2], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_r1]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_r2]
	call	ast_list_push
	pop	rbp
	ret

; A `Row` node holding the atoms whose §4.6 ordinals are in [fx_ords], count
; rcx.  -> the node id is left in [fx_rowtmp]
  fx_row:
	push	rbp
	mov	[fx_r3], rcx
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_r4], rax
	xor	r10, r10
  .item:
	mov	rax, [fx_r3]
	cmp	r10, rax
	jge	.emit
	mov	[fx_r5], r10
	lea	rax, [fx_ords]
	mov	rcx, [fx_r5]
	mov	rcx, [rax + rcx*8]
	call	fx_ri_atom
	mov	r10, [fx_r5]
	inc	r10
	jmp	.item
  .emit:
	lea	rdi, [fx_tree]
	mov	rsi, [fx_r4]
	call	ast_list_emit
	mov	rsi, AST_ROW
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, [fx_r3]
	call	fx_node
	mov	[fx_rowtmp], rax
	pop	rbp
	ret

; A `Fn` node with signature row `rcx` (a `Row` node or 0) for declaration
; `[fx_fd]`, span `[fx_fs]`.  -> rax = the node id, [fx_lastsig] = its Sig.
  fx_fn:
	push	rbp
	mov	[fx_f1], rcx
	mov	rsi, AST_SIG
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_f2], rax
	mov	[fx_lastsig], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_f1]
	call	fx_setcd
	mov	rax, [fx_fs]
	mov	[fx_span + Span.start], eax
	mov	rsi, AST_FN
	xor	rdx, rdx
	mov	rcx, [fx_f2]
	xor	r8, r8
	call	fx_node
	mov	[fx_f3], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_fd]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_fd]
	mov	rdx, [fx_f3]
	call	ast_decl_node
	mov	rax, [fx_f3]
	pop	rbp
	ret

; ---------------------------------------------------------------------------
  fx_build:
	push	rbp
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_span]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	; ---- declarations (name ids chosen so a != b) ----
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_IFACE
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 1 Duo
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_MEMBER
	xor	rdx, rdx
	mov	rcx, 1				; name 1 = `a`
	mov	r8, 1
	lea	r9, [fx_span]
	call	ast_decl			; 2 Duo.a
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_MEMBER
	xor	rdx, rdx
	mov	rcx, 2				; name 2 = `b`
	mov	r8, 1
	lea	r9, [fx_span]
	call	ast_decl			; 3 Duo.b
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_STRUCT
	xor	rdx, rdx
	mov	rcx, 3
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 4 Right
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_IMPL
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl			; 5 the implementation
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_MEMBER
	xor	rdx, rdx
	mov	rcx, 1				; name 1 = impl `a`
	mov	r8, 5
	lea	r9, [fx_span]
	call	ast_decl			; 6 impl.a
	lea	rdi, [fx_tree]
	call	ast_cap_push
	mov	[fx_base], rax

	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_STRUCT
	mov	rdx, 4
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_right], rax

	; ---- trait member a: `poscit alloc` (name 1) ----
	mov	qword [fx_ords], AST_CAP_ALLOC
	mov	rcx, 1
	call	fx_row
	mov	qword [fx_fd], 2
	mov	qword [fx_fs], 0
	mov	rcx, [fx_rowtmp]
	call	fx_fn
	mov	[fx_ta], rax
	mov	rcx, [fx_lastsig]
	mov	[fx_atsig], rcx

	; ---- trait member b: `poscit rete` (name 2) ----
	mov	qword [fx_ords], AST_CAP_RETE
	mov	rcx, 1
	call	fx_row
	mov	qword [fx_fd], 3
	mov	qword [fx_fs], 0
	mov	rcx, [fx_rowtmp]
	call	fx_fn
	mov	[fx_tb], rax

	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t2], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_ta]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_tb]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t2]
	call	ast_list_emit
	mov	rsi, AST_IFACE
	xor	rdx, rdx
	mov	rcx, rax
	mov	r8, 2				; two members
	call	fx_node
	mov	[fx_t3], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 1
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, [fx_t3]
	call	ast_decl_node

	; ---- `Right` (no fields -- its mark is empty) ----
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t2], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t2]
	call	ast_list_emit
	mov	rsi, AST_STRUCT
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	[fx_t3], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, 4
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 4
	mov	rdx, [fx_t3]
	call	ast_decl_node

	; ---- the implementation head: `poscit alloc, rete` ----
	mov	rcx, 1
	call	fx_path				; the interface `Path` -> Duo
	mov	[fx_t1], rax
	mov	rcx, 4
	call	fx_path
	mov	rsi, AST_TYPATH
	xor	rdx, rdx
	mov	rcx, rax
	xor	r8, r8
	call	fx_node
	mov	[fx_t2], rax			; the target TYPE node
	lea	rdi, [fx_tree]
	mov	rsi, rax
	mov	rdx, [fx_ty_right]
	call	ast_node_ty
	mov	qword [fx_ords], AST_CAP_ALLOC
	mov	qword [fx_ords + 8], AST_CAP_RETE
	mov	rcx, 2
	call	fx_row
	mov	rax, [fx_rowtmp]
	mov	[fx_irow], rax
	mov	dword [fx_span + Span.start], 3131
	mov	rsi, AST_IMPLHEAD
	xor	rdx, rdx
	mov	rcx, [fx_t1]
	mov	r8, [fx_t2]
	call	fx_node
	mov	[fx_t3], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, rax
	mov	rdx, [fx_irow]
	xor	rcx, rcx
	call	fx_setcd

	; ---- the impl's method `a` (name 1); its row is set per-check ----
	mov	qword [fx_fd], 6
	mov	qword [fx_fs], 7070
	xor	rcx, rcx
	call	fx_fn
	mov	[fx_t4], rax
	mov	rcx, [fx_lastsig]
	mov	[fx_amsig], rcx

	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_t5], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t4]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_t5]
	call	ast_list_emit
	mov	rsi, AST_IMPL
	xor	rdx, rdx
	mov	rcx, [fx_t3]
	mov	r8, rax
	call	fx_node
	mov	[fx_t6], rax
	mov	rsi, rax
	mov	rdx, 1
	mov	rcx, 5
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, 5
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
  fx_ndiag:	rq 1
  fx_base:	rq 1
  fx_ords:	rq 8
  fx_rowtmp:	rq 1
  fx_lastsig:	rq 1
  fx_atsig:	rq 1
  fx_amsig:	rq 1
  fx_ta:	rq 1
  fx_tb:	rq 1
  fx_ty_right:	rq 1
  fx_irow:	rq 1
  fx_t1:	rq 1
  fx_t2:	rq 1
  fx_t3:	rq 1
  fx_t4:	rq 1
  fx_t5:	rq 1
  fx_t6:	rq 1
  fx_p1:	rq 1
  fx_p2:	rq 1
  fx_r1:	rq 1
  fx_r2:	rq 1
  fx_r3:	rq 1
  fx_r4:	rq 1
  fx_r5:	rq 1
  fx_f1:	rq 1
  fx_f2:	rq 1
  fx_f3:	rq 1
  fx_fd:	rq 1
  fx_fs:	rq 1
