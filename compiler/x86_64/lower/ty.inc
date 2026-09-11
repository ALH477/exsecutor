; compiler/x86_64/lower/ty.inc
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
; The typed AST's types, mapped to the IR's: docs/design/lowering.md section
; 2.2's table, and the sizes and alignments a `slot` needs.
;
; The table has three columns that matter here -- is it an SSA variable, what
; IR type does it have, and how many bytes does it occupy if it has to live in
; memory -- and this file answers exactly those three questions and nothing
; else. A binding is an SSA variable iff its type is SCALAR and the checker did
; not set `AST_F_MEMRES`; that flag is read, never re-derived (CHK 2.5 sets it
; for "non-scalar type or address taken").
;
; PRELUDE TYPES. `Scriptor` has no `AstDecl` -- `checker/types/prim.inc`
; interns it as an `AST_TY_STRUCT` whose `a` carries the TAGGED prelude
; reference `CHK_TY_PRELUDE or k` instead of a declaration id (that file's own
; header says so). So every size/layout lookup here tests bit 31 first and
; falls back to `prelude/interface.inc`'s constants, which are the same
; constants the blob itself is assembled from.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md sections 5.1, 5.2, 5.4, 6.4, 9.5.
; -----------------------------------------------------------------------------

include 'cfg.inc'

; ---- the prelude interface table, the one source of its facts -------------
; `prelude/interface.inc` part A is constants and part B is the pre-seeded
; declaration table -- the Exsecutor name, the PRELUDE SYMBOL and the IR
; signature of each of `runtime.md` 2.2's routines. docs/design/lowering.md
; finding 8 asks for exactly that symbol column, and this is where the
; lowering reads it: `ambitus` -> `@exsrt_mundus_ambitus` is looked up, never
; written out again here.
;
; Part B emits bytes and this file is included inside a code segment, so the
; table is jumped over the way print.inc's own string literals are.
; `interface.inc` includes nothing, so this is not a second path to anything.
EXS_IFACE_EMIT_DECLS = 1
	jmp	__lwr_iface_end
; NO include of `../prelude/interface.inc` here, though this file reads its
; constants. `checker/types/prim.inc` includes it, fasmg has no include
; guards, and it emits labels rather than only macros -- so a second path
; redefines `exsrt_iface_nomina`. A self-guard is not the answer either: an
; `if ~ defined X` around a file that then defines X oscillates between
; passes and fasmg gives up with "could not generate code within the allowed
; number of passes" (measured). So the CONSUMER includes it exactly once,
; before lower/: exsc.asm gets it through checker/, and each lwr_ fixture
; includes it itself.
  __lwr_iface_end:

; The prelude tag, restated. `checker/types/prim.inc` defines `CHK_TY_PRELUDE`
; and this pass cannot include that file (see ssa.inc's header, D1), so the
; constant is written again here with the same value and the same reason bit
; 31 is free: `Ast.decls` cannot reach 2^31 entries.
LWR_PRELUDE_TAG  = 0x80000000
LWR_PRELUDE_MASK = 0x7FFFFFFF

; lwr_ty_kind(st, lwrt) -> eax = the `AST_TY_*` kind of type id `lwrt`, or 0
; for the absent type 0.
proc lwr_ty_kind, lwrsp, lwrt
	locals
	endl
	cmp	dword [lwrt], 0
	je	.none
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.tree]
	mov	esi, [lwrt]
	call	ast_type_at
	movzx	eax, byte [rax + AstType.kind]
	return
  .none:
	xor	eax, eax
	return
endp

; lwr_ty_scalar(st, lwrt) -> eax = 1 if a value of this type lives in a
; register (and is therefore SSA-renamable), 0 if it lives behind a `ptr`.
; docs/design/lowering.md section 2.2's table, exactly: `unit` answers 0 too,
; because it has no value at all -- callers must test `lwr_ty_void` first
; where that distinction matters.
proc lwr_ty_scalar, lwrsp, lwrt
	locals
	endl
	mov	rdi, [lwrsp]
	mov	esi, [lwrt]
	call	lwr_ty_kind
	cmp	eax, AST_TY_INT
	je	.yes
	cmp	eax, AST_TY_FLOAT
	je	.yes
	cmp	eax, AST_TY_MENSURA
	je	.yes
	cmp	eax, AST_TY_PTR
	je	.yes
	cmp	eax, AST_TY_BORROW
	je	.yes
	cmp	eax, AST_TY_CAP
	je	.yes
	cmp	eax, AST_TY_FN
	je	.yes
	cmp	eax, AST_TY_REF
	je	.yes
	cmp	eax, AST_TY_REFC
	je	.yes
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; lwr_ty_void(st, lwrt) -> eax = 1 for `unit` or the absent type -- a value
; that is not produced at all.
proc lwr_ty_void, lwrsp, lwrt
	locals
	endl
	cmp	dword [lwrt], 0
	je	.yes
	mov	rdi, [lwrsp]
	mov	esi, [lwrt]
	call	lwr_ty_kind
	cmp	eax, AST_TY_UNIT
	je	.yes
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; lwr_ty_ir(st, lwrt) -> eax = the IR type id a VALUE of this type has: the
; exact-width integer or float for a scalar, `ref`/`refc` for a reference,
; `ptr` for everything that travels by address, 0 for `unit`.
proc lwr_ty_ir, lwrsp, lwrt
	uses	rbx, r12
	locals
		slot lwrk, dd
		slot lwrw, dd
		slot lwrs, dd
	endl
	mov	rbx, [lwrsp]
	cmp	dword [lwrt], 0
	je	.void
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrt]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	mov	[lwrk], ecx
	movzx	ecx, byte [rax + AstType.sign]
	mov	[lwrs], ecx
	mov	ecx, [rax + AstType.width]
	mov	[lwrw], ecx

	mov	eax, [lwrk]
	cmp	eax, AST_TY_INT
	je	.int
	cmp	eax, AST_TY_FLOAT
	je	.flt
	cmp	eax, AST_TY_MENSURA
	je	.mensura
	cmp	eax, AST_TY_REF
	je	.ref
	cmp	eax, AST_TY_REFC
	je	.refc
	cmp	eax, AST_TY_UNIT
	je	.void
	; every remaining kind travels as an address: ptr, borrow, cap, fn
	; (a closure ptr), and every aggregate
	mov	eax, [rbx + LwrState.tptr]
	return
  .int:
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, BFA_TK_U
	cmp	dword [lwrs], AST_SIGN_I
	jne	.unsigned
	mov	esi, BFA_TK_I
  .unsigned:
	xor	rdx, rdx
	mov	ecx, [lwrw]
	xor	r8, r8
	call	bfa_type_intern
	return
  .flt:
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, BFA_TK_F32
	cmp	dword [lwrw], 64
	jne	.f32
	mov	esi, BFA_TK_F64
  .f32:
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	call	bfa_type_intern
	return
  .mensura:
	; spec section 9.5 has fixed `--hospes` before any IR exists, so
	; `mensura` is the concrete width the checker recorded (64 on
	; x86_64-linux, docs/design/runtime.md 2.3) and not a distinct IR kind
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, BFA_TK_U
	xor	rdx, rdx
	mov	ecx, [lwrw]
	xor	r8, r8
	call	bfa_type_intern
	return
  .ref:
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, BFA_TK_REF
	xor	rdx, rdx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	return
  .refc:
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, BFA_TK_REFC
	xor	rdx, rdx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	return
  .void:
	xor	eax, eax
	return
endp

; lwr_ty_size(st, lwrt) -> rax = size in bytes, rdx = alignment. Only ever
; asked of a type that needs a `slot`.
;
; `textus` is 16/8 and a `structura` reads `Ast.layout` at its declaration
; (`AstLayout.off` holds a struct's SIZE, checker/rows/layout.inc's own
; comment) -- unless the declaration slot carries the prelude tag, in which
; case the size is `prelude/interface.inc`'s. An `acies<T, N>` is `N` elements
; of `T`'s size at `T`'s alignment -- no padding between them, which is what
; `__lwr_elem`'s `index %p %i stride` already assumes when it strides by `T`'s
; size alone, and what the one aggregate cast needs (`acies<u8, 17>` is
; exactly `DeModFrame`'s seventeen bytes, spec §5.2). Anything else is
; `[UNIMPLEMENTED]` and traps rather than guessing a number the emitter would
; then carve a frame from.
proc lwr_ty_size, lwrsp, lwrt
	uses	rbx, r12
	locals
		slot lwrk, dd
		slot lwrd, dd
		slot lwrn, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_kind
	mov	[lwrk], eax
	cmp	eax, AST_TY_TEXTUS
	je	.textus
	cmp	eax, AST_TY_STRUCT
	je	.struc
	cmp	eax, AST_TY_ACIES
	je	.acies
	; scalars can be address-taken and then need a slot too
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_scalar
	test	eax, eax
	jnz	.scalar
	rassert	eax ne 0		; [UNIMPLEMENTED]: dyn, octeti,
					; scalares, grapha, eventus, brand
  .textus:
	mov	rax, EXS_IFACE_TEXTUS_SIZE
	mov	rdx, EXS_IFACE_TEXTUS_ALIGN
	return
  .acies:
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrt]
	call	ast_type_at
	mov	ecx, [rax + AstType.width]	; the element COUNT (spec §5.4)
	mov	[lwrn], ecx
	mov	ecx, [rax + AstType.a]		; the element type
	mov	rdi, rbx
	mov	esi, ecx
	call	lwr_ty_size
	mov	r12, rdx			; the element's alignment
	mov	ecx, [lwrn]
	imul	rax, rcx
	mov	rdx, r12
	return
  .scalar:
	mov	rdi, rbx
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrt]
	call	ast_type_at
	mov	eax, [rax + AstType.width]
	add	eax, 7
	shr	eax, 3
	mov	rdx, rax
	return
  .struc:
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrt]
	call	ast_type_at
	mov	ecx, [rax + AstType.a]
	mov	[lwrd], ecx
	test	ecx, LWR_PRELUDE_TAG
	jnz	.prestruct
	mov	rdi, [rbx + LwrState.tree]
	mov	esi, [lwrd]
	call	ast_layout_at
	mov	ecx, [rax + AstLayout.off]	; a struct's SIZE
	movzx	edx, byte [rax + AstLayout.algn]
	mov	rax, rcx
	return
  .prestruct:
	; the only prelude struct is `Scriptor` (interface.inc row 0)
	mov	eax, [lwrd]
	and	eax, LWR_PRELUDE_MASK
	rassert	eax eq 1
	mov	rax, EXS_IFACE_SCRIPTOR_SIZE
	mov	rdx, EXS_IFACE_SCRIPTOR_ALIGN
	return
endp

; lwr_slot(st, lwrt) -> eax = `%s = slot size align` for a value of AST type
; `lwrt`, emitted in the CURRENT block. docs/design/runtime.md section 5 shows
; the slot placed inline at the declaration, not hoisted to the entry block;
; `emit.inc` carves it wherever it appears and nothing depends on hoisting.
proc lwr_slot, lwrsp, lwrt
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_ty_size
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, rbx
	mov	esi, BFA_OP_SLOT
	xor	rdx, rdx
	mov	ecx, [rbx + LwrState.tptr]
	mov	r8d, r12d
	mov	r9d, r13d
	call	lwr_emit
	return
endp
