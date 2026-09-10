; tests/unit/chk_row_layout.asm
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
; checker-rows fixture for pass 4 -- layout, `EXS-E0321`, `EXS-E0322`.
;
; THE TWO STRUCTS ARE SPEC §5.2's OWN, and they are the pair 4edb704 settled a
; contradiction between: `Capitulum`, written for the spec, and `DeModFrame`,
; which was not -- eleven independent implementations and a 246-vector
; certificate (`vendor/hydramesh-wire/`). checker.md finding 13 showed the two
; could not both be right; §5.2 now says fields are PACKED, alignment 1,
; always, and this fixture is where that becomes a measurement rather than a
; sentence: `cursus` really does land at byte 15, directly after a 24-bit
; field at byte 12, and `DeModFrame` really is 17 bytes.
;
;   1. `Capitulum` as written -- accepted, and every offset is the sum of the
;      preceding widths (spec §14 entry 6's ACCEPTING twin).
;   2. §14 entry 6: one field annotated `:nativus` -- `EXS-E0321` at that
;      field, and nowhere else. §5.2: ":nativus is legal in-process and
;      ILLEGAL in any @transitus type".
;   3. `DeModFrame` as written -- accepted, 17 bytes, `versio`/`genus`
;      sharing byte 1 MSB-first (rule 1), `cursus` at byte 15.
;   4. §14 entry 21: the declared widths no longer sum to a whole byte --
;      `EXS-E0322`, once, against the STRUCT (rule 4, "the load-bearing one").
;   5. Rule 2: a field wider than 8 bits that is not a whole number of bytes
;      -- `EXS-E0322` at the FIELD, and then at everything that follows from
;      it: the next multi-byte field no longer starts on a byte boundary, and
;      the total no longer sums to a whole byte. Three diagnostics from one
;      edit, which is what "byte order is an order over bytes" costs.
;   6. `mensura` is concrete from `--hospes` (spec §9.5): the same struct
;      lays out at 4 bytes for a 32-bit target and 8 for a 64-bit one, and
;      nothing about it comes from the build machine.
;   7. A struct that is NOT `@transitus` is packed by the same rule and
;      raises neither code -- §5.2's "always", read as covering both.
;
; `[UNTESTED]` on real source: pass 4 reads `Decl.ty`, which the types pass
; fills. This fixture fills it by hand -- checker.md section 6's arrangement.
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

	; ---- 1: `Capitulum` as written ----
	mov	qword [fx_bits], 64
	call	fx_run
	test	rax, rax
	jnz	.fail1
	; signum u32:maior 0, versio u8 4, genus u8 5, reserva u16:maior 6,
	; longitudo u32:maior 8 -- and the struct is 12 bytes, alignment 1.
	mov	rsi, 2
	call	fx_lay
	test	rax, rax
	jnz	.fail1
	mov	rsi, 3
	call	fx_lay
	cmp	rax, 4
	jne	.fail1
	mov	rsi, 4
	call	fx_lay
	cmp	rax, 5
	jne	.fail1
	mov	rsi, 5
	call	fx_lay
	cmp	rax, 6
	jne	.fail1
	mov	rsi, 6
	call	fx_lay
	cmp	rax, 8
	jne	.fail1
	mov	rsi, 1
	call	fx_lay
	cmp	rax, 12				; sizeof Capitulum
	jne	.fail1
	cmp	r9, 1				; PACKED: alignment 1
	jne	.fail1

	; ---- 2: §14 entry 6 -- `:nativus` in a wire struct ----
	lea	rdi, [fx_tree]
	mov	rsi, 2
	mov	rdx, [fx_ty_u32n]
	call	ast_decl_ty
	call	fx_run
	cmp	rax, 1
	jne	.fail2
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 321
	jne	.fail2
	cmp	rdx, 602			; `signum`'s own span
	jne	.fail2
	lea	rdi, [fx_tree]
	mov	rsi, 2
	mov	rdx, [fx_ty_u32m]
	call	ast_decl_ty

	; ---- 3: `DeModFrame` as written ----
	call	fx_run
	test	rax, rax
	jnz	.fail3
	mov	rsi, 8				; signum u8
	call	fx_lay
	test	rax, rax
	jne	.fail3
	mov	rsi, 9				; versio u4 -- byte 1, bit 0
	call	fx_lay
	cmp	rax, 1
	jne	.fail3
	test	rdx, rdx
	jnz	.fail3
	cmp	rcx, 4				; four bits wide
	jne	.fail3
	mov	rsi, 10				; genus u4 -- byte 1, bit 4
	call	fx_lay				; MSB-first in declaration order
	cmp	rax, 1
	jne	.fail3
	cmp	rdx, 4
	jne	.fail3
	mov	rsi, 15				; tempus u24:maior -- byte 12
	call	fx_lay
	cmp	rax, 12
	jne	.fail3
	mov	rsi, 16				; cursus u16:maior -- BYTE 15
	call	fx_lay
	cmp	rax, 15
	jne	.fail3
	test	rdx, rdx
	jnz	.fail3
	mov	rsi, 7
	call	fx_lay
	cmp	rax, 17				; the 17-byte quantum
	jne	.fail3

	; ---- 4: §14 entry 21 -- the widths no longer sum to a whole byte ----
	lea	rdi, [fx_tree]
	mov	rsi, 16
	mov	rdx, [fx_ty_u4]
	call	ast_decl_ty
	call	fx_run
	cmp	rax, 1
	jne	.fail4
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 322
	jne	.fail4
	cmp	rdx, 700			; the STRUCT, not any field
	jne	.fail4
	lea	rdi, [fx_tree]
	mov	rsi, 16
	mov	rdx, [fx_ty_u16m]
	call	ast_decl_ty

	; ---- 5: rule 2 -- above 8 bits, whole bytes only ----
	lea	rdi, [fx_tree]
	mov	rsi, 15
	mov	rdx, [fx_ty_u12]
	call	ast_decl_ty
	call	fx_run
	cmp	rax, 3
	jne	.fail5
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 322
	jne	.fail5
	cmp	rdx, 615			; `tempus` is not a whole number
	jne	.fail5				; of bytes (rule 2)
	mov	rsi, 1
	call	fx_diag
	cmp	rax, 322
	jne	.fail5
	cmp	rdx, 616			; ... so `cursus` no longer starts
	jne	.fail5				; on a byte boundary (rule 2 again)
	mov	rsi, 2
	call	fx_diag
	cmp	rax, 322
	jne	.fail5
	cmp	rdx, 700			; ... and the total is 124 bits
	jne	.fail5				; (rule 4)
	lea	rdi, [fx_tree]
	mov	rsi, 15
	mov	rdx, [fx_ty_u24m]
	call	ast_decl_ty

	; ---- 6: `mensura` is the TARGET's, not the build machine's ----
	call	fx_run
	test	rax, rax
	jnz	.fail6
	mov	rsi, 17
	call	fx_lay
	cmp	rax, 9				; mensura(8) + u8(1)
	jne	.fail6
	mov	qword [fx_bits], 32
	call	fx_run
	test	rax, rax
	jnz	.fail6
	mov	rsi, 17
	call	fx_lay
	cmp	rax, 5				; mensura(4) + u8(1)
	jne	.fail6
	mov	qword [fx_bits], 64

	; ---- 7: a non-`@transitus` struct is packed, and raises nothing ----
	; `Mens` is `{ m: mensura, b: u8 }` with no annotation: `b` sits at the
	; byte after `m`, and neither `EXS-E0321` nor `EXS-E0322` applies to it
	; -- `b` is unannotated and multi-byte-free, and `m` is `:nativus` by
	; construction, which is legal off the wire (spec §5.2).
	call	fx_run
	test	rax, rax
	jnz	.fail7
	mov	rsi, 18
	call	fx_lay
	test	rax, rax
	jnz	.fail7
	mov	rsi, 19
	call	fx_lay
	cmp	rax, 8				; packed, not aligned to 8 twice
	jne	.fail7
	mov	rsi, 17
	call	fx_lay
	cmp	rax, 9
	jne	.fail7
	cmp	r9, 1
	jne	.fail7

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

; ---------------------------------------------------------------------------
; Pass 4 alone. Pass 3 is not run: it would need `Path.d` this tree has no
; names for, and the two passes share nothing but the scratch arena.
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

; rsi = a declaration id -> rax = its layout's byte offset (a struct's SIZE),
; rdx = its bit offset, rcx = its bit width, r9 = its alignment.
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

; One field of the struct whose declaration is [fx_sd]: type id [fx_ft], span
; start [fx_fsp]. Pushes the declaration, stages the `Field` node.
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
	mov	[fx_fn1], rax
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_fdid]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_fn1]
	call	ast_list_push
	mov	dword [fx_span + Span.start], 0
	pop	rbp
	ret

; The `Struct` node for declaration [fx_sd], over everything staged since
; [fx_smk], with span start [fx_ssp].
  fx_close:
	push	rbp
	lea	rdi, [fx_tree]
	mov	rsi, [fx_smk]
	call	ast_list_emit
	mov	[fx_fn1], rax
	mov	[fx_fn2], rdx
	mov	rax, [fx_ssp]
	mov	[fx_span + Span.start], eax
	mov	rsi, AST_STRUCT
	xor	rdx, rdx
	mov	rcx, [fx_fn1]
	mov	r8, [fx_fn2]
	call	fx_node
	mov	[fx_fn1], rax
	mov	dword [fx_span + Span.start], 0
	mov	rsi, rax
	xor	rdx, rdx
	mov	rcx, [fx_sd]
	call	fx_setcd
	lea	rdi, [fx_tree]
	mov	rsi, [fx_sd]
	mov	rdx, [fx_fn1]
	call	ast_decl_node
	pop	rbp
	ret

; ---------------------------------------------------------------------------
; A struct declaration: kind flags [fx_sf], and everything after it belongs to
; it until `fx_close`.  -> [fx_sd] = its declaration id
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

; One field of the open struct, of type rcx, whose span start is 600 plus its
; own declaration id -- so a diagnostic's caret names the field it is about
; without a name table.
  fx_f:
	push	rbp
	mov	[fx_ft], rcx
	lea	rdi, [fx_tree]
	call	ast_decl_count
	add	rax, 601
	mov	[fx_fsp], rax
	call	fx_field
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

	; ---- the types spec §5.2's two examples are written in ----
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 32
	mov	rcx, AST_ORD_MAIOR
	call	ast_type_int
	mov	[fx_ty_u32m], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 32
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fx_ty_u32n], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 8
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fx_ty_u8], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 16
	mov	rcx, AST_ORD_MAIOR
	call	ast_type_int
	mov	[fx_ty_u16m], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 4
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fx_ty_u4], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 24
	mov	rcx, AST_ORD_MAIOR
	call	ast_type_int
	mov	[fx_ty_u24m], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 12
	mov	rcx, AST_ORD_MAIOR
	call	ast_type_int
	mov	[fx_ty_u12], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_MENSURA
	call	ast_type_simple
	mov	[fx_ty_mens], rax

	; ---- @transitus publica structura Capitulum (spec §5.2) ----
	mov	qword [fx_sf], AST_F_TRANSITUS
	call	fx_open				; declaration 1
	mov	rcx, [fx_ty_u32m]
	call	fx_f				; 2 signum    u32:maior
	mov	rcx, [fx_ty_u8]
	call	fx_f				; 3 versio    u8
	mov	rcx, [fx_ty_u8]
	call	fx_f				; 4 genus     u8
	mov	rcx, [fx_ty_u16m]
	call	fx_f				; 5 reserva   u16:maior
	mov	rcx, [fx_ty_u32m]
	call	fx_f				; 6 longitudo u32:maior
	mov	qword [fx_ssp], 500
	call	fx_close

	; ---- @transitus publica structura DeModFrame (spec §5.2) ----
	; The DCF quantum: 17 bytes, eleven implementations, 246 vectors.
	mov	qword [fx_sf], AST_F_TRANSITUS
	call	fx_open				; declaration 7
	mov	rcx, [fx_ty_u8]
	call	fx_f				; 8  signum  u8
	mov	rcx, [fx_ty_u4]
	call	fx_f				; 9  versio  u4
	mov	rcx, [fx_ty_u4]
	call	fx_f				; 10 genus   u4
	mov	rcx, [fx_ty_u16m]
	call	fx_f				; 11 numerus u16:maior
	mov	rcx, [fx_ty_u16m]
	call	fx_f				; 12 fons    u16:maior
	mov	rcx, [fx_ty_u16m]
	call	fx_f				; 13 meta    u16:maior
	mov	rcx, [fx_ty_u32m]
	call	fx_f				; 14 onus    u32:maior
	mov	rcx, [fx_ty_u24m]
	call	fx_f				; 15 tempus  u24:maior
	mov	rcx, [fx_ty_u16m]
	call	fx_f				; 16 cursus  u16:maior
	mov	qword [fx_ssp], 700
	call	fx_close

	; ---- structura Mens { m: mensura, b: u8 } -- NOT @transitus ----
	mov	qword [fx_sf], 0
	call	fx_open				; declaration 17
	mov	rcx, [fx_ty_mens]
	call	fx_f				; 18 m mensura
	mov	rcx, [fx_ty_u8]
	call	fx_f				; 19 b u8
	mov	qword [fx_ssp], 800
	call	fx_close

	lea	rdi, [fx_tree]
	call	ast_cap_push
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
  fx_bits:	rq 1
  fx_ndiag:	rq 1
  fx_sd:	rq 1
  fx_sf:	rq 1
  fx_smk:	rq 1
  fx_ssp:	rq 1
  fx_ft:	rq 1
  fx_fsp:	rq 1
  fx_fdid:	rq 1
  fx_fn1:	rq 1
  fx_fn2:	rq 1
  fx_ty_u32m:	rq 1
  fx_ty_u32n:	rq 1
  fx_ty_u8:	rq 1
  fx_ty_u16m:	rq 1
  fx_ty_u4:	rq 1
  fx_ty_u24m:	rq 1
  fx_ty_u12:	rq 1
  fx_ty_mens:	rq 1
