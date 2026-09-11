; tests/unit/chk_row_layout_kind.asm
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
; PASS 4 -- LAYOUT, `@transitus` FIELD KIND. Spec §5.2, "The one aggregate
; cast": *"Every field of a `@transitus` type is an unsigned integer: `u1`-
; `u7`, or a whole-byte width `u8`-`u64` with its order. Nothing else -- no
; `textus`, no `refero`, no capability-bearing type, no `acies`, and no
; nested struct."*
;
; `checker/rows/chk_row_layout.asm` (`tests/unit/chk_row_layout.asm`) fixes
; §5.2's rules 2-4 for FIELDS THAT ARE `uN`. It never puts a field of a kind
; `__chk_lay_ty` cannot size (`textus`, a capability type, ...) into a
; `@transitus` struct, so it never exercised the bug this fixture pins: such
; a field has width 0, and 0 <= 8 satisfied rule 3's "byte order is not part
; of the grammar at or below eight bits" -- written for a real sub-byte
; INTEGER -- so the field skipped the order check ENTIRELY and `@transitus
; structura T { t: textus }` checked clean. `exsc aedifica` on exactly that
; program exited 0 before this fixture's fix.
;
;   1. `t: textus`, unannotated -- `EXS-E0321` at the field. Spec: "an
;      unannotated non-integer field is `:nativus` by rule 3 and is already
;      `EXS-E0321`."
;   2. `t: textus:maior` -- `EXS-E0309` at the field, and `EXS-E0321` does
;      NOT also fire for it: the field is explicitly ordered (rule 3's
;      default-`:nativus` branch does not apply), so this pass's own
;      `AST_TY_INT`-only check is the one that rejects it, exactly as
;      `checker/types/sig.inc`'s `__chk_ty_suffix` already rejects an order
;      suffix on a non-`AST_TY_INT` type at every OTHER type position (Pass
;      2, real source) -- this pass's fixtures build `Decl.ty` by hand and
;      never run Pass 2 (this file's own header, and `chk_row_layout.asm`'s),
;      so this is that same rule exercised where Pass 2 cannot reach.
;   3. `i: i32`, unannotated -- `EXS-E0321`, same as any other unannotated
;      multi-byte field: the check does not read `sign`.
;   4. `i: i32:maior` -- `[OPEN]`. Spec §5.2's enforcement paragraph gives
;      two codes, "an unannotated non-integer field" (`EXS-E0321`) and "an
;      order annotation on a type that is not an integer" (`EXS-E0309`); a
;      SIGNED integer is still `AST_TY_INT` under both readings, so neither
;      sentence's condition is met, even though the surrounding prose says
;      every field "is an unsigned integer." Whether `iN` belongs in a
;      `@transitus` struct at all is therefore unassigned by the spec text,
;      and this pass raises nothing for it -- checked here so the gap is a
;      recorded, passing row rather than untested.
;   5. `a: acies<u8, 4>`, unannotated element -- `EXS-E0321`. `__chk_lay_ty`'s
;      `.acies` case propagates the ELEMENT's order (here `:nativus`), and
;      an `acies` field is never an admitted kind regardless.
;   6. `r: refero_communis<u8>` (`AST_TY_REFC`; `refero<u8>` is `AST_TY_REF`
;      -- checker/types/sig.inc's `.refero` arm -- and takes the same
;      `.word` path in `__chk_lay_ty`, so one row stands for both; an
;      earlier version of this line labelled the REFC row `refero<u8>`) --
;      `EXS-E0321`. Not to be confused with spec §5.2's `EXS-E0305`, which
;      is about taking `&` of a `@transitus` VALUE -- a different operation
;      this pass does not check.
;   7. `s: Inner` (a nested, non-`@transitus`, two-`u8` struct, so its width
;      is 16 bits and does not fall into rule 3's <= 8 bit exemption) --
;      `EXS-E0321`. `__chk_lay_ty`'s `.struc` case always returns
;      `AST_ORD_NATIVUS` for a struct-typed field -- a struct can never
;      itself carry an order -- so this is the "unannotated" branch, exactly
;      as spec §5.2 says a nested struct is not admitted.
;   8. A capability-bearing type (`AST_TY_CAP`, standing in for `Scriptor`
;      and the other ten atoms of spec §4.6) -- `EXS-E0321`, the same
;      width-0 path as `textus`.
;   9. `structura NonWire { t: textus }`, NOT `@transitus` -- clean. §5.2's
;      field enumeration is a `@transitus`-only rule; nothing here applies
;      off the wire.
;
; `[UNTESTED]` on real source, for `chk_row_layout.asm`'s reason: `Decl.ty`
; is the types pass's and Pass 2 is not run here. `Ast/exsc` end-to-end
; coverage of case 1 and 2 is `tests/programs/` (see the commit message).
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

	mov	qword [fx_bits], 64
	call	fx_run
	cmp	rax, 7				; exactly 7 diagnostics: cases
	jne	.fail1				; 1,2,3,5,6,7,8 -- 4 and 9 are clean

	; ---- 1: `t: textus`, unannotated -- EXS-E0321 ----
	xor	rsi, rsi
	call	fx_diag
	cmp	rax, 321
	jne	.fail1
	mov	rcx, [fx_did_textus]
	add	rcx, 600
	cmp	rdx, rcx
	jne	.fail1

	; ---- 2: `t: textus:maior` -- EXS-E0309, and EXS-E0321 does NOT
	; also fire for this field ----
	mov	rsi, 1
	call	fx_diag
	cmp	rax, 309
	jne	.fail2
	mov	rcx, [fx_did_textus_maior]
	add	rcx, 600
	cmp	rdx, rcx
	jne	.fail2

	; ---- 3: `i: i32`, unannotated -- EXS-E0321 ----
	mov	rsi, 2
	call	fx_diag
	cmp	rax, 321
	jne	.fail3
	mov	rcx, [fx_did_i32n]
	add	rcx, 600
	cmp	rdx, rcx
	jne	.fail3

	; ---- 5: `a: acies<u8, 4>` -- EXS-E0321 ----
	mov	rsi, 3
	call	fx_diag
	cmp	rax, 321
	jne	.fail5
	mov	rcx, [fx_did_acies]
	add	rcx, 600
	cmp	rdx, rcx
	jne	.fail5

	; ---- 6: `r: refero<u8>` -- EXS-E0321 ----
	mov	rsi, 4
	call	fx_diag
	cmp	rax, 321
	jne	.fail6
	mov	rcx, [fx_did_refc]
	add	rcx, 600
	cmp	rdx, rcx
	jne	.fail6

	; ---- 7: `s: Inner` (nested struct) -- EXS-E0321 ----
	mov	rsi, 5
	call	fx_diag
	cmp	rax, 321
	jne	.fail7
	mov	rcx, [fx_did_struct]
	add	rcx, 600
	cmp	rdx, rcx
	jne	.fail7

	; ---- 8: capability-bearing type -- EXS-E0321 ----
	mov	rsi, 6
	call	fx_diag
	cmp	rax, 321
	jne	.fail8
	mov	rcx, [fx_did_cap]
	add	rcx, 600
	cmp	rdx, rcx
	jne	.fail8

	; ---- 4: `i: i32:maior` -- [OPEN], nothing raised against it. Every
	; diagnostic buffered has already been accounted for above (cases
	; 1,2,3,5,6,7,8 are exactly the 7 checked), so this is implicitly
	; verified by the total count at the top; nothing further to read.

	; ---- 9: `structura NonWire { t: textus }` -- clean, same as 4 ----

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
; start [fx_fsp]. Pushes the declaration, stages the `Field` node. Leaves the
; field's own declaration id in [fx_fdid].
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
; without a name table. -> rax = the field's own declaration id.
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

	; ---- the types this fixture's fields use ----
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 8
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fx_ty_u8n], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_TEXTUS
	call	ast_type_simple
	mov	[fx_ty_textus], rax

	; `textus:maior` -- no convenience constructor sets ORDER on a
	; non-`int` kind (spec §5.2 never admits one), so this is built the
	; way the constructors in ast/types.inc build every other type: zero
	; the 16-byte template, fill it, intern it.
	lea	rdi, [fx_t0]
	call	ast_type_clear
	mov	byte [fx_t0 + AstType.kind], AST_TY_TEXTUS
	mov	byte [fx_t0 + AstType.order], AST_ORD_MAIOR
	lea	rdi, [fx_tree]
	lea	rsi, [fx_t0]
	call	ast_type_intern
	mov	[fx_ty_textus_maior], rax

	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_I
	mov	rdx, 32
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	mov	[fx_ty_i32n], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_I
	mov	rdx, 32
	mov	rcx, AST_ORD_MAIOR
	call	ast_type_int
	mov	[fx_ty_i32m], rax

	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_ACIES
	mov	rdx, [fx_ty_u8n]		; element type
	mov	rcx, 4				; lane count (spec §5.4)
	call	ast_type_una
	mov	[fx_ty_acies], rax

	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_REFC		; `refero<u8>` -- `&T` (`AST_TY_REF`)
	mov	rdx, [fx_ty_u8n]		; takes the same `.word` path
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_refc], rax

	; a capability-bearing type (spec §4.6): `AST_TY_CAP`'s `a` is
	; normally one of the eleven atom declarations, but `__chk_lay_ty`'s
	; unsized path never reads it, so a placeholder is enough here.
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_CAP
	xor	rdx, rdx
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_cap], rax

	; ---- structura Inner { b0: u8, b1: u8 } -- NOT @transitus, 16 bits
	; wide so a struct-typed field's width does not fall into rule 3's
	; <= 8 bit exemption by accident ----
	mov	qword [fx_sf], 0
	call	fx_open
	mov	rax, [fx_sd]			; fx_open's own return value is
	mov	[fx_did_inner], rax		; ast_list_mark's, not the decl id
	mov	rcx, [fx_ty_u8n]
	call	fx_f
	mov	rcx, [fx_ty_u8n]
	call	fx_f
	mov	qword [fx_ssp], 100
	call	fx_close

	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_STRUCT
	mov	rdx, [fx_did_inner]
	xor	rcx, rcx
	call	ast_type_una
	mov	[fx_ty_struct], rax

	; ---- one @transitus struct per case, each with a single field, so
	; no field's diagnostics depend on any other field's width ----
	mov	qword [fx_sf], AST_F_TRANSITUS

	call	fx_open
	mov	rcx, [fx_ty_textus]
	call	fx_f
	mov	[fx_did_textus], rax
	mov	qword [fx_ssp], 200
	call	fx_close

	call	fx_open
	mov	rcx, [fx_ty_textus_maior]
	call	fx_f
	mov	[fx_did_textus_maior], rax
	mov	qword [fx_ssp], 300
	call	fx_close

	call	fx_open
	mov	rcx, [fx_ty_i32n]
	call	fx_f
	mov	[fx_did_i32n], rax
	mov	qword [fx_ssp], 400
	call	fx_close

	call	fx_open
	mov	rcx, [fx_ty_i32m]
	call	fx_f
	mov	[fx_did_i32m], rax
	mov	qword [fx_ssp], 500
	call	fx_close

	call	fx_open
	mov	rcx, [fx_ty_acies]
	call	fx_f
	mov	[fx_did_acies], rax
	mov	qword [fx_ssp], 600
	call	fx_close

	call	fx_open
	mov	rcx, [fx_ty_refc]
	call	fx_f
	mov	[fx_did_refc], rax
	mov	qword [fx_ssp], 700
	call	fx_close

	call	fx_open
	mov	rcx, [fx_ty_struct]
	call	fx_f
	mov	[fx_did_struct], rax
	mov	qword [fx_ssp], 800
	call	fx_close

	call	fx_open
	mov	rcx, [fx_ty_cap]
	call	fx_f
	mov	[fx_did_cap], rax
	mov	qword [fx_ssp], 900
	call	fx_close

	; ---- structura NonWire { t: textus } -- NOT @transitus, clean ----
	mov	qword [fx_sf], 0
	call	fx_open
	mov	rcx, [fx_ty_textus]
	call	fx_f
	mov	[fx_did_nonwire], rax
	mov	qword [fx_ssp], 1000
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
  fx_t0:	rb 16
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
  fx_ty_u8n:		rq 1
  fx_ty_textus:		rq 1
  fx_ty_textus_maior:	rq 1
  fx_ty_i32n:		rq 1
  fx_ty_i32m:		rq 1
  fx_ty_acies:		rq 1
  fx_ty_refc:		rq 1
  fx_ty_cap:		rq 1
  fx_ty_struct:		rq 1
  fx_did_inner:		rq 1
  fx_did_textus:	rq 1
  fx_did_textus_maior:	rq 1
  fx_did_i32n:		rq 1
  fx_did_i32m:		rq 1
  fx_did_acies:		rq 1
  fx_did_refc:		rq 1
  fx_did_struct:	rq 1
  fx_did_cap:		rq 1
  fx_did_nonwire:	rq 1
