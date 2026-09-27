; tests/unit/chk_row_layout_sum.asm
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
; checker-rows fixture for pass 4 -- the layout of a SUM TYPE,
; docs/design/sum-types.md D6: tag, then the largest payload, packed.
;
; This is the fixture that document's §6 said had to exist BEFORE the parser
; change: "a `typus` with variants admitted before layout answers a width is
; silently wrong at every field offset after it." So the tree is built by
; hand here -- `AST_SUMBODY`/`AST_VARIANT` nodes, `AST_D_VARIANT`
; declarations, `AST_TY_SUM` types, and `AstNode.ty` on every payload node,
; which pass 2 would write -- exactly as chk_row_layout.asm builds `Decl.ty`
; by hand, and for the same reason: no grammar produces one yet, and the
; layout must be pinned before one does.
;
;   1. `typus modus = casus ordinata, casus arborea(mensura);` -- 9 bytes on
;      a 64-bit target (1 tag + 8), 5 on a 32-bit one (spec §9.5: `mensura`
;      is the TARGET's); both variants' payloads start at byte 1; alignment
;      1.
;   2. `typus color = casus ruber, casus viridis, casus caeruleus;` -- an
;      enumeration is exactly its tag: 1 byte.
;   3. `typus par = casus duo(u8, u16), casus unus(u8);` -- a payload's
;      elements pack in source order (3 bytes) and the sum takes the widest:
;      4 bytes.
;   4. `typus capsa = casus caput(Capitulum), casus nihil;` where `Capitulum`
;      (spec §5.2's 12-byte struct) is declared AFTER the sum -- pulled
;      forward and laid out once: the sum is 13 bytes, the struct 12.
;   5. `structura S { m: modus, b: u8 }`, not `@transitus`: `m` at 0 with
;      its 72 bits recorded, `b` at byte 9, 10 bytes, no diagnostic.
;   6. `@transitus structura W { m: modus }`: exactly one `EXS-E0321`, at
;      the field -- spec §5.2 admits only unsigned integers on the wire, and
;      pass 4's existing kind test says so with no sum-specific rule.
;   7. 257 payload-less variants: the tag is 2 bytes, so the sum is.
;   8. `typus arbor = casus folium, casus nodus(arbor);` -- recursive. Pass 4
;      must not trap: it breaks the cycle with width 0 exactly as it does for
;      a self-containing struct, and the sum lays out as 1 byte. That is the
;      CYCLE-BREAK VALUE, pinned so a change to it is deliberate, and not a
;      size anyone may rely on: the refusal by name is the types pass's
;      (`EXS-E0303`, sum-types.md D1) and does not exist yet.
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

	; ---- 1: modus -- tag + mensura, on two targets ----
	; Every run lays out the whole tree, and the tree holds check 6's
	; `@transitus` struct, so every run buffers exactly ONE diagnostic --
	; that one. Asserted as 1 here and in each later run, and identified
	; in check 6; a second diagnostic anywhere fails the count.
	mov	qword [fx_bits], 64
	call	fx_run
	cmp	rax, 1
	jne	.fail1
	mov	rsi, [fx_d_modus]
	call	fx_lay
	cmp	rax, 9				; 1 + 8
	jne	.fail1
	cmp	r9, 1				; PACKED
	jne	.fail1
	mov	rsi, [fx_d_ordinata]
	call	fx_lay
	cmp	rax, 1				; payload offset = tag width
	jne	.fail1
	mov	rsi, [fx_d_arborea]
	call	fx_lay
	cmp	rax, 1
	jne	.fail1
	mov	qword [fx_bits], 32
	call	fx_run
	cmp	rax, 1
	jne	.fail1
	mov	rsi, [fx_d_modus]
	call	fx_lay
	cmp	rax, 5				; 1 + 4
	jne	.fail1
	mov	qword [fx_bits], 64
	call	fx_run
	cmp	rax, 1
	jne	.fail1

	; ---- 2: color -- an enumeration is its tag ----
	mov	rsi, [fx_d_color]
	call	fx_lay
	cmp	rax, 1
	jne	.fail2

	; ---- 3: par -- elements pack, the sum takes the widest payload ----
	mov	rsi, [fx_d_par]
	call	fx_lay
	cmp	rax, 4				; 1 + (1 + 2)
	jne	.fail3

	; ---- 4: capsa -- a struct payload declared later, pulled forward ----
	mov	rsi, [fx_d_capsa]
	call	fx_lay
	cmp	rax, 13				; 1 + 12
	jne	.fail4
	mov	rsi, [fx_d_capit]
	call	fx_lay
	cmp	rax, 12				; and Capitulum is still 12
	jne	.fail4

	; ---- 5: a sum-typed field in a plain struct ----
	mov	rsi, [fx_d_s_m]
	call	fx_lay
	test	rax, rax			; m at byte 0
	jnz	.fail5
	cmp	rcx, 72				; 9 bytes, recorded in bits
	jne	.fail5
	mov	rsi, [fx_d_s_b]
	call	fx_lay
	cmp	rax, 9				; b directly after it
	jne	.fail5
	mov	rsi, [fx_d_s]
	call	fx_lay
	cmp	rax, 10
	jne	.fail5

	; ---- 6: a sum-typed field on the wire is EXS-E0321, once ----
	; The one diagnostic every run above counted, identified: code 321 at
	; `W.m`'s own caret (600 + its declaration id, `fx_f`'s numbering).
	mov	qword [fx_bits], 64
	call	fx_run
	cmp	rax, 1
	jne	.fail6
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 321
	jne	.fail6
	mov	rcx, [fx_d_w_m]
	add	rcx, 600
	cmp	rdx, rcx			; at the FIELD
	jne	.fail6

	; ---- 7: 257 variants need a two-byte tag ----
	mov	rsi, [fx_d_wide]
	call	fx_lay
	cmp	rax, 2
	jne	.fail7

	; ---- 8: a recursive sum does not trap; the cycle breaks at 0 ----
	mov	rsi, [fx_d_arbor]
	call	fx_lay
	cmp	rax, 1				; tag + 0: the cycle-break value
	jne	.fail8

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

; ---------------------------------------------------------------------------
; Pass 4 alone, as chk_row_layout.asm runs it.
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
	mov	rsi, [fx_bits]
	call	chk_layout
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

; rsi = a declaration id -> rax = its layout's byte offset (an aggregate's
; SIZE), rdx = its bit offset, rcx = its bit width, r9 = its alignment.
  fx_lay:
	push	rbp
	lea	rdi, [fx_tree]
	call	ast_layout_at
	movzx	edx, byte [rax + AstLayout.bitoff]
	movzx	ecx, byte [rax + AstLayout.width]
	movzx	r9d, byte [rax + AstLayout.algn]
	mov	eax, [rax + AstLayout.off]
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

; ---- sum types -------------------------------------------------------------
; `fx_sum_open` pushes the `typus` declaration ([fx_td]) and marks the variant
; list; `fx_vopen` pushes a variant's declaration ([fx_vd]) and marks its
; payload list; `fx_pay` (rcx = a type id) stages one payload type node with
; `AstNode.ty` set as pass 2 would set it; `fx_vclose` emits the payload list
; into a `Variant` and stages it; `fx_sum_close` emits the variants into a
; `SumBody`, builds the `Typus` over it, and interns the `AST_TY_SUM` type
; into [fx_st].
  fx_sum_open:
	push	rbp
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_TYPUS
	xor	rdx, rdx
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl
	mov	[fx_td], rax
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_tmk], rax
	pop	rbp
	ret

  fx_vopen:
	push	rbp
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_VARIANT
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, [fx_td]
	lea	r9, [fx_span]
	call	ast_decl
	mov	[fx_vd], rax
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_vmk], rax
	pop	rbp
	ret

  fx_pay:
	push	rbp
	mov	[fx_pt], rcx
	mov	rsi, AST_TYBIT
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	[fx_n1], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	mov	rdx, [fx_pt]
	call	ast_node_ty
	lea	rdi, [fx_tree]
	mov	rsi, [fx_n1]
	call	ast_list_push
	pop	rbp
	ret

  fx_vclose:
	push	rbp
	lea	rdi, [fx_tree]
	mov	rsi, [fx_vmk]
	call	ast_list_emit
	mov	[fx_n1], rax
	mov	[fx_n2], rdx
	mov	rsi, AST_VARIANT
	xor	rdx, rdx
	mov	rcx, 1				; name id
	mov	r8, [fx_n1]			; payload list
	call	fx_node
	mov	[fx_n1], rax
	mov	rsi, rax
	mov	rdx, [fx_n2]			; payload count
	mov	rcx, [fx_vd]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_vd]
	mov	rdx, [fx_n1]
	call	ast_decl_node
	lea	rdi, [fx_tree]
	mov	rsi, [fx_n1]
	call	ast_list_push
	pop	rbp
	ret

  fx_sum_close:
	push	rbp
	lea	rdi, [fx_tree]
	mov	rsi, [fx_tmk]
	call	ast_list_emit
	mov	rsi, AST_SUMBODY
	xor	rcx, rcx
	xchg	rcx, rax			; a = list offset
	mov	r8, rdx				; b = variant count
	xor	rdx, rdx
	call	fx_node
	mov	[fx_n1], rax
	mov	rsi, AST_TYPUS
	xor	rdx, rdx
	mov	rcx, rax			; `Typus.a` = the `SumBody`
	xor	r8, r8
	call	fx_node
	mov	[fx_n1], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_td]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_td]
	mov	rdx, [fx_n1]
	call	ast_decl_node
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_SUM
	mov	rdx, [fx_td]
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_st], rax
	pop	rbp
	ret

; ---- structs, as chk_row_layout.asm builds them ---------------------------
  fx_field:
	push	rbp
	mov	rax, [fx_fsp]
	mov	[fx_span + Span.start], eax
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FIELD
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, [fx_sd]
	lea	r9, [fx_span]
	call	ast_decl
	mov	[fx_fdid], rax
	lea	rdi, [fx_tree]
	mov	rsi, rax
	mov	rdx, [fx_ft]
	call	ast_decl_ty
	mov	rsi, AST_TYBIT
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	fx_node
	mov	rsi, AST_FIELD
	xor	rdx, rdx
	mov	rcx, 1
	mov	r8, rax
	call	fx_node
	mov	[fx_n1], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_fdid]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_n1]
	call	ast_list_push
	mov	dword [fx_span + Span.start], 0
	pop	rbp
	ret

  fx_close:
	push	rbp
	lea	rdi, [fx_tree]
	mov	rsi, [fx_smk]
	call	ast_list_emit
	mov	[fx_n1], rax
	mov	[fx_n2], rdx
	mov	rsi, AST_STRUCT
	xor	rdx, rdx
	mov	rcx, [fx_n1]
	mov	r8, [fx_n2]
	call	fx_node
	mov	[fx_n1], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_sd]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_sd]
	mov	rdx, [fx_n1]
	call	ast_decl_node
	pop	rbp
	ret

  fx_open:
	push	rbp
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_STRUCT
	mov	rdx, [fx_sf]
	mov	rcx, 1
	xor	r8, r8
	lea	r9, [fx_span]
	call	ast_decl
	mov	[fx_sd], rax
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	[fx_smk], rax
	pop	rbp
	ret

; One field of the open struct, of type rcx, span start 600 + its own
; declaration id -> rax = that declaration id.
  fx_f:
	push	rbp
	mov	[fx_ft], rcx
	lea	rdi, [fx_tree]
	call	ast_decl_count
	add	rax, 601
	mov	[fx_fsp], rax
	call	fx_field
	mov	rax, [fx_fdid]
	pop	rbp
	ret

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

	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 8
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fx_ty_u8], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 16
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fx_ty_u16], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 32
	mov	rcx, AST_ORD_MAIOR
	call	ast_type_int
	mov	[fx_ty_u32m], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 16
	mov	rcx, AST_ORD_MAIOR
	call	ast_type_int
	mov	[fx_ty_u16m], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_MENSURA
	call	ast_type_simple
	mov	[fx_ty_mens], rax

	; ---- 1: typus modus = casus ordinata, casus arborea(mensura); ----
	call	fx_sum_open
	mov	rax, [fx_td]
	mov	[fx_d_modus], rax
	call	fx_vopen
	mov	rax, [fx_vd]
	mov	[fx_d_ordinata], rax
	call	fx_vclose
	call	fx_vopen
	mov	rax, [fx_vd]
	mov	[fx_d_arborea], rax
	mov	rcx, [fx_ty_mens]
	call	fx_pay
	call	fx_vclose
	call	fx_sum_close
	mov	rax, [fx_st]
	mov	[fx_ty_modus], rax

	; ---- 2: typus color = casus ruber, casus viridis, casus caeruleus; --
	call	fx_sum_open
	mov	rax, [fx_td]
	mov	[fx_d_color], rax
	call	fx_vopen
	call	fx_vclose
	call	fx_vopen
	call	fx_vclose
	call	fx_vopen
	call	fx_vclose
	call	fx_sum_close

	; ---- 3: typus par = casus duo(u8, u16), casus unus(u8); ----
	call	fx_sum_open
	mov	rax, [fx_td]
	mov	[fx_d_par], rax
	call	fx_vopen
	mov	rcx, [fx_ty_u8]
	call	fx_pay
	mov	rcx, [fx_ty_u16]
	call	fx_pay
	call	fx_vclose
	call	fx_vopen
	mov	rcx, [fx_ty_u8]
	call	fx_pay
	call	fx_vclose
	call	fx_sum_close

	; ---- 4: typus capsa = casus caput(Capitulum), casus nihil; ----
	; `Capitulum`'s declaration id is known before the struct is built:
	; it is the next declaration after `capsa`'s two variants. The type
	; is interned against that id and the struct built afterwards, so the
	; sum names a struct declared later, which is the point of the case.
	call	fx_sum_open
	mov	rax, [fx_td]
	mov	[fx_d_capsa], rax
	lea	rdi, [fx_tree]
	call	ast_decl_count
	add	rax, 3				; caput, nihil, then Capitulum
	mov	[fx_d_capit], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_STRUCT
	mov	rdx, rax
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_capit], rax
	call	fx_vopen
	mov	rcx, [fx_ty_capit]
	call	fx_pay
	call	fx_vclose
	call	fx_vopen
	call	fx_vclose
	call	fx_sum_close
	; @transitus publica structura Capitulum (spec §5.2), 12 bytes
	mov	qword [fx_sf], AST_F_TRANSITUS
	call	fx_open
	mov	rax, [fx_sd]
	cmp	rax, [fx_d_capit]
	jne	.build_bug
	mov	rcx, [fx_ty_u32m]
	call	fx_f
	mov	rcx, [fx_ty_u8]
	call	fx_f
	mov	rcx, [fx_ty_u8]
	call	fx_f
	mov	rcx, [fx_ty_u16m]
	call	fx_f
	mov	rcx, [fx_ty_u32m]
	call	fx_f
	call	fx_close

	; ---- 5: structura S { m: modus, b: u8 } ----
	mov	qword [fx_sf], 0
	call	fx_open
	mov	rax, [fx_sd]
	mov	[fx_d_s], rax
	mov	rcx, [fx_ty_modus]
	call	fx_f
	mov	[fx_d_s_m], rax
	mov	rcx, [fx_ty_u8]
	call	fx_f
	mov	[fx_d_s_b], rax
	call	fx_close

	; ---- 7: 257 payload-less variants ----
	call	fx_sum_open
	mov	rax, [fx_td]
	mov	[fx_d_wide], rax
	mov	qword [fx_i], 257
  .wide:
	call	fx_vopen
	call	fx_vclose
	dec	qword [fx_i]
	jnz	.wide
	call	fx_sum_close

	; ---- 8: typus arbor = casus folium, casus nodus(arbor); ----
	call	fx_sum_open
	mov	rax, [fx_td]
	mov	[fx_d_arbor], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_SUM
	mov	rdx, rax
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_arbor], rax
	call	fx_vopen
	call	fx_vclose
	call	fx_vopen
	mov	rcx, [fx_ty_arbor]
	call	fx_pay
	call	fx_vclose
	call	fx_sum_close

	; ---- 6: @transitus structura W { m: modus } -- built LAST so its
	; field is the last `fx_f`-numbered declaration ----
	mov	qword [fx_sf], AST_F_TRANSITUS
	call	fx_open
	mov	rcx, [fx_ty_modus]
	call	fx_f
	mov	[fx_d_w_m], rax
	call	fx_close

	lea	rdi, [fx_tree]
	call	ast_cap_push
	pop	rbp
	ret
  .build_bug:
	mov	rdi, 10				; the fixture's own arithmetic
	call	sys_exit_group

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
  fx_bits:	rq 1
  fx_ndiag:	rq 1
  fx_i:		rq 1
  fx_sd:	rq 1
  fx_sf:	rq 1
  fx_smk:	rq 1
  fx_ft:	rq 1
  fx_fsp:	rq 1
  fx_fdid:	rq 1
  fx_td:	rq 1
  fx_tmk:	rq 1
  fx_vd:	rq 1
  fx_vmk:	rq 1
  fx_pt:	rq 1
  fx_st:	rq 1
  fx_n1:	rq 1
  fx_n2:	rq 1
  fx_ty_u8:	rq 1
  fx_ty_u16:	rq 1
  fx_ty_u32m:	rq 1
  fx_ty_u16m:	rq 1
  fx_ty_mens:	rq 1
  fx_ty_modus:	rq 1
  fx_ty_capit:	rq 1
  fx_ty_arbor:	rq 1
  fx_d_modus:	rq 1
  fx_d_ordinata: rq 1
  fx_d_arborea:	rq 1
  fx_d_color:	rq 1
  fx_d_par:	rq 1
  fx_d_capsa:	rq 1
  fx_d_capit:	rq 1
  fx_d_s:	rq 1
  fx_d_s_m:	rq 1
  fx_d_s_b:	rq 1
  fx_d_w_m:	rq 1
  fx_d_wide:	rq 1
  fx_d_arbor:	rq 1
