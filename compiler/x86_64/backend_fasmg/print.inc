; compiler/x86_64/backend_fasmg/print.inc
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
; The IR printer: docs/design/ssa-ir.md section 2.11 ("the printer renumbers
; in print order... so text and internal ids never need to agree") and
; section 4's determinism table (textual %N/bN is a function of print order:
; block index, phi list, body).
;
; TWO PASSES, deliberately: pass 1 walks every block in creation order,
; phis then body, and assigns each value/block a FRESH print-order number
; with no text emitted yet; pass 2 walks the identical order and emits text,
; resolving every operand (including a phi's forward reference, which pass 1
; may not have numbered yet at the point the referencing phi itself was
; visited) against the now-COMPLETE tables from pass 1. A single-pass
; printer cannot do this: phi is the one place the text is allowed to name a
; value defined later (docs/design/ssa-ir.md section 2.11), and pass 1 must
; finish before that forward name can be resolved.
;
; Output is byte-at-a-time into an arena-backed `Vec` (stride 1) -- the
; naive, slow-by-construction choice this whole backend is built on (spec
; section 9.2: "slow by construction and correct by construction").
;
; Spec: docs/spec/exsecutor-spec-v0.4.md section 9.2. Design contract:
; docs/design/ssa-ir.md, section 2.11, section 4.
; -----------------------------------------------------------------------------

include 'parse.inc'



; ---- op -> shape table, file scope, `jmp`-guarded like every other inline
; literal in this tree (this file's own header explains why: never a bare
; data declaration with no code flow skipping it). Index 0 (op id 0) is
; never a real opcode; its entry is unused padding.
	jmp	__bfa_op_shape_tab_end
__bfa_op_shape_tab:
	db	0
	db	BFA_SH_BIN		; 1  ADD
	db	BFA_SH_BIN		; 2  SUB
	db	BFA_SH_BIN		; 3  MUL
	db	BFA_SH_BIN		; 4  DIV
	db	BFA_SH_BIN		; 5  REM
	db	BFA_SH_BIN		; 6  ADDW
	db	BFA_SH_BIN		; 7  SUBW
	db	BFA_SH_BIN		; 8  MULW
	db	BFA_SH_BIN		; 9  ADDS
	db	BFA_SH_BIN		; 10 SUBS
	db	BFA_SH_BIN		; 11 MULS
	db	BFA_SH_BIN		; 12 ADDOV
	db	BFA_SH_BIN		; 13 SUBOV
	db	BFA_SH_BIN		; 14 MULOV
	db	BFA_SH_BIN		; 15 AND
	db	BFA_SH_BIN		; 16 OR
	db	BFA_SH_BIN		; 17 XOR
	db	BFA_SH_BIN		; 18 SHL
	db	BFA_SH_BIN		; 19 SHR
	db	BFA_SH_CMP		; 20 CMP
	db	BFA_SH_UN		; 21 ZEXT
	db	BFA_SH_UN		; 22 SEXT
	db	BFA_SH_UN		; 23 TRUNC
	db	BFA_SH_BIN		; 24 FADD
	db	BFA_SH_BIN		; 25 FSUB
	db	BFA_SH_BIN		; 26 FMUL
	db	BFA_SH_BIN		; 27 FDIV
	db	BFA_SH_UN		; 28 FNEG
	db	BFA_SH_FMA		; 29 FMA
	db	BFA_SH_FCMP		; 30 FCMP
	db	BFA_SH_UN		; 31 FEXT
	db	BFA_SH_UN		; 32 FTRUNC
	db	BFA_SH_UN		; 33 ITOF
	db	BFA_SH_UN		; 34 FTOI
	db	BFA_SH_UN		; 35 BITCAST
	db	BFA_SH_REDINIT		; 36 REDINIT
	db	BFA_SH_CONTRIB		; 37 CONTRIB
	db	BFA_SH_REDFIN		; 38 REDFIN
	db	BFA_SH_SLOT		; 39 SLOT
	db	BFA_SH_LOAD		; 40 LOAD
	db	BFA_SH_STORE		; 41 STORE
	db	BFA_SH_LOADBITS		; 42 LOADBITS
	db	BFA_SH_STOREBITS	; 43 STOREBITS
	db	BFA_SH_COPY		; 44 COPY
	db	BFA_SH_ADDR		; 45 ADDR
	db	BFA_SH_INDEX		; 46 INDEX
	db	BFA_SH_GADDR		; 47 GADDR
	db	BFA_SH_CHK		; 48 CHK
	db	BFA_SH_RETAIN		; 49 RETAIN
	db	BFA_SH_RETAIN		; 50 RELEASE
	db	BFA_SH_CALL		; 51 CALL
	db	BFA_SH_CALLIND		; 52 CALLIND
	db	BFA_SH_RET		; 53 RET
	db	BFA_SH_JMP		; 54 JMP
	db	BFA_SH_BR		; 55 BR
	db	BFA_SH_TRAP		; 56 TRAP
	db	BFA_SH_PHI		; 57 PHI
	db	BFA_SH_PARAM		; 58 PARAM
	db	BFA_SH_ICONST		; 59 ICONST
	db	BFA_SH_FCONST		; 60 FCONST
	db	BFA_SH_BIN		; 61 VADD
	db	BFA_SH_BIN		; 62 VSUB
	db	BFA_SH_BIN		; 63 VMUL
	db	BFA_SH_BIN		; 64 VDIV
__bfa_op_shape_tab_end:

; __bfa_op_shape(op) -> eax = BFA_SH_*.
proc __bfa_op_shape, op
	locals
	endl
	lea	rax, [__bfa_op_shape_tab]
	mov	rcx, [op]
	movzx	eax, byte [rax + rcx]
	return
endp

; ---- BfaPrintCtx -- pass-1 tables plus the output Vec.
struct BfaPrintCtx
	field module,    dq
	field obuf,      dq	; -> Vec<db>
	field blk_print, dq	; -> Vec<dd>, index = internal block id - 1
	field val_print, dq	; -> Vec<dd>, index = internal value id - 1; -1 (0xFFFFFFFF) if void
	field curblk,    dd	; emit.inc only: the internal id of the block being
				; emitted, i.e. the PREDECESSOR a `jmp`/`br` edge
				; leaves -- a phi's operand is chosen by it
end struct

; ============================================================================
; output buffer primitives
; ============================================================================

proc __bfa_out_byte, ctx, b
	locals
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.obuf]
	call	vec_push
	mov	ecx, [b]
	mov	[rax], cl
	return
endp

proc __bfa_out_bytes, ctx, ptr, len
	locals
		slot i, dq
	endl
	mov	qword [i], 0
  .loop:
	mov	rax, [i]
	cmp	rax, [len]
	jge	.done
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.obuf]
	call	vec_push
	mov	rcx, [ptr]
	mov	rdx, [i]
	movzx	edx, byte [rcx + rdx]
	mov	[rax], dl
	mov	rax, [i]
	inc	rax
	mov	[i], rax
	jmp	.loop
  .done:
	return
endp

; __bfa_out_lit(ctx, ptr, len) -- alias kept distinct from __bfa_out_bytes
; only so call sites read as "print this literal" versus "print these
; already-computed bytes"; identical body.
proc __bfa_out_lit, ctx, ptr, len
	locals
	endl
	mov	rdi, [ctx]
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	__bfa_out_bytes
	return
endp

; __bfa_out_u64(ctx, value) -- unsigned decimal, no sign.
proc __bfa_out_u64, ctx, value
	locals
		slot buf, 24
	endl
	mov	rdi, [value]
	lea	rsi, [buf + 24]
	call	__bfa_fmt_udec
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_bytes
	return
endp

; __bfa_out_i64(ctx, value) -- signed decimal: a leading '-' iff the 64-bit
; two's-complement value is negative, then the unsigned magnitude.
proc __bfa_out_i64, ctx, value
	locals
		slot mag, dq
	endl
	mov	rax, [value]
	test	rax, rax
	jns	.nonneg
	mov	rdi, [ctx]
	mov	esi, '-'
	call	__bfa_out_byte
	mov	rax, [value]
	neg	rax
	mov	[mag], rax
	jmp	.emit
  .nonneg:
	mov	rax, [value]
	mov	[mag], rax
  .emit:
	mov	rdi, [ctx]
	mov	rsi, [mag]
	call	__bfa_out_u64
	return
endp

; ============================================================================
; type printing
; ============================================================================

; __bfa_print_type(ctx, type_id) -- `type_id` 0 means `void` (only legal for
; a function/call return type -- callers elsewhere never pass 0). Recurses
; once for `red.F` (an ordinary `call`, same as the parser's own
; `__bfa_parse_type`).
proc __bfa_print_type, ctx, type_id
	locals
		slot tp, dd
		slot twidth, dd
		slot tref, dd
	endl
	mov	dword [tp], 0
	cmp	dword [type_id], 0
	jne	.nonvoid
	mov	rdi, [ctx]
	jmp	.after_void
  .lit_void db "void"
  .after_void:
	lea	rsi, [.lit_void]
	mov	rdx, .after_void - .lit_void
	call	__bfa_out_lit
	return
  .nonvoid:
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	esi, [type_id]
	call	bfa_type_ptr			; rax = BfaType*
	movzx	ecx, byte [rax + BfaType.kind]
	mov	[tp], ecx
	movzx	ecx, word [rax + BfaType.width]
	mov	[twidth], ecx
	mov	ecx, [rax + BfaType.ref]
	mov	[tref], ecx

	cmp	dword [tp], BFA_TK_U
	jne	.notU
	mov	rdi, [ctx]
	mov	esi, 'u'
	call	__bfa_out_byte
	mov	rdi, [ctx]
	mov	esi, [twidth]
	call	__bfa_out_u64
	return
  .notU:
	cmp	dword [tp], BFA_TK_I
	jne	.notI
	mov	rdi, [ctx]
	mov	esi, 'i'
	call	__bfa_out_byte
	mov	rdi, [ctx]
	mov	esi, [twidth]
	call	__bfa_out_u64
	return
  .notI:
	cmp	dword [tp], BFA_TK_F32
	jne	.notF32
	mov	rdi, [ctx]
	jmp	.after_f32
  .lit_f32 db "f32"
  .after_f32:
	lea	rsi, [.lit_f32]
	mov	rdx, .after_f32 - .lit_f32
	call	__bfa_out_lit
	return
  .notF32:
	cmp	dword [tp], BFA_TK_F64
	jne	.notF64
	mov	rdi, [ctx]
	jmp	.after_f64
  .lit_f64 db "f64"
  .after_f64:
	lea	rsi, [.lit_f64]
	mov	rdx, .after_f64 - .lit_f64
	call	__bfa_out_lit
	return
  .notF64:
	cmp	dword [tp], BFA_TK_PTR
	jne	.notPTR
	mov	rdi, [ctx]
	jmp	.after_ptr
  .lit_ptr db "ptr"
  .after_ptr:
	lea	rsi, [.lit_ptr]
	mov	rdx, .after_ptr - .lit_ptr
	call	__bfa_out_lit
	return
  .notPTR:
	cmp	dword [tp], BFA_TK_DPTR
	jne	.notDPTR
	mov	rdi, [ctx]
	jmp	.after_dptr
  .lit_dptr db "dptr"
  .after_dptr:
	lea	rsi, [.lit_dptr]
	mov	rdx, .after_dptr - .lit_dptr
	call	__bfa_out_lit
	return
  .notDPTR:
	cmp	dword [tp], BFA_TK_REFC
	jne	.notREFC
	mov	rdi, [ctx]
	jmp	.after_refc
  .lit_refc db "refc"
  .after_refc:
	lea	rsi, [.lit_refc]
	mov	rdx, .after_refc - .lit_refc
	call	__bfa_out_lit
	return
  .notREFC:
	cmp	dword [tp], BFA_TK_REF
	jne	.notREF
	mov	rdi, [ctx]
	jmp	.after_ref
  .lit_ref db "ref"
  .after_ref:
	lea	rsi, [.lit_ref]
	mov	rdx, .after_ref - .lit_ref
	call	__bfa_out_lit
	return
  .notREF:
	cmp	dword [tp], BFA_TK_RED
	jne	.notRED
	mov	rdi, [ctx]
	jmp	.after_redp
  .lit_redp db "red."
  .after_redp:
	lea	rsi, [.lit_redp]
	mov	rdx, .after_redp - .lit_redp
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [tref]
	call	__bfa_print_type
	return
  .notRED:
	cmp	dword [tp], BFA_TK_VF32
	jne	.notVF32
	mov	rdi, [ctx]
	jmp	.after_vf32p
  .lit_vf32p db "vf32."
  .after_vf32p:
	lea	rsi, [.lit_vf32p]
	mov	rdx, .after_vf32p - .lit_vf32p
	call	__bfa_out_lit
	jmp	.vflanes
  .notVF32:
	cmp	dword [tp], BFA_TK_VF64
	jne	.notVF64
	mov	rdi, [ctx]
	jmp	.after_vf64p
  .lit_vf64p db "vf64."
  .after_vf64p:
	lea	rsi, [.lit_vf64p]
	mov	rdx, .after_vf64p - .lit_vf64p
	call	__bfa_out_lit
  .vflanes:
	mov	rdi, [ctx]
	mov	esi, [tref]			; `ref` = the lane count
	call	__bfa_out_u64
	return
  .notVF64:
	; BFA_TK_FN
	mov	rdi, [ctx]
	jmp	.after_fnp
  .lit_fnp db "fn."
  .after_fnp:
	lea	rsi, [.lit_fnp]
	mov	rdx, .after_fnp - .lit_fnp
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [tref]
	call	__bfa_out_u64
	return
endp

; ============================================================================
; opcode / predicate / keyword name printing
; ============================================================================

; __bfa_opname_case? op, opc, text -- inside a `proc ..., ctx, op, ...`
; ONLY: if [op] == opc, prints `text` and returns from the enclosing proc.
; Falls through otherwise. Mirrors parse.inc's `bfa_opdef?` in spirit (a
; literal string, `jmp`-guarded, one macro invocation per spelling) but
; drives a comparison chain instead of a map, since printing already has a
; concrete integer opcode to switch on.
macro __bfa_opname_case? op, opc, text
	local skip, s, e
	cmp	dword [op], opc
	jne	skip
	mov	rdi, [ctx]
	jmp	e
	s db	text
	e:
	lea	rsi, [s]
	mov	rdx, e - s
	call	__bfa_out_lit
	return
  skip:
end macro

; __bfa_print_opname(ctx, op, aux) -- prints the opcode's text spelling,
; including the `.pred` suffix for cmp/fcmp (aux's low 3 bits).
proc __bfa_print_opname, ctx, op, aux
	locals
	endl
	__bfa_opname_case op, BFA_OP_ADD, 'add'
	__bfa_opname_case op, BFA_OP_SUB, 'sub'
	__bfa_opname_case op, BFA_OP_MUL, 'mul'
	__bfa_opname_case op, BFA_OP_DIV, 'div'
	__bfa_opname_case op, BFA_OP_REM, 'rem'
	__bfa_opname_case op, BFA_OP_ADDW, 'addw'
	__bfa_opname_case op, BFA_OP_SUBW, 'subw'
	__bfa_opname_case op, BFA_OP_MULW, 'mulw'
	__bfa_opname_case op, BFA_OP_ADDS, 'adds'
	__bfa_opname_case op, BFA_OP_SUBS, 'subs'
	__bfa_opname_case op, BFA_OP_MULS, 'muls'
	__bfa_opname_case op, BFA_OP_ADDOV, 'addov'
	__bfa_opname_case op, BFA_OP_SUBOV, 'subov'
	__bfa_opname_case op, BFA_OP_MULOV, 'mulov'
	__bfa_opname_case op, BFA_OP_AND, 'and'
	__bfa_opname_case op, BFA_OP_OR, 'or'
	__bfa_opname_case op, BFA_OP_XOR, 'xor'
	__bfa_opname_case op, BFA_OP_SHL, 'shl'
	__bfa_opname_case op, BFA_OP_SHR, 'shr'
	__bfa_opname_case op, BFA_OP_ZEXT, 'zext'
	__bfa_opname_case op, BFA_OP_SEXT, 'sext'
	__bfa_opname_case op, BFA_OP_TRUNC, 'trunc'
	__bfa_opname_case op, BFA_OP_FADD, 'fadd'
	__bfa_opname_case op, BFA_OP_FSUB, 'fsub'
	__bfa_opname_case op, BFA_OP_FMUL, 'fmul'
	__bfa_opname_case op, BFA_OP_FDIV, 'fdiv'
	__bfa_opname_case op, BFA_OP_FNEG, 'fneg'
	__bfa_opname_case op, BFA_OP_FMA, 'fma'
	__bfa_opname_case op, BFA_OP_FEXT, 'fext'
	__bfa_opname_case op, BFA_OP_FTRUNC, 'ftrunc'
	__bfa_opname_case op, BFA_OP_ITOF, 'itof'
	__bfa_opname_case op, BFA_OP_FTOI, 'ftoi'
	__bfa_opname_case op, BFA_OP_BITCAST, 'bitcast'
	__bfa_opname_case op, BFA_OP_REDINIT, 'redinit'
	__bfa_opname_case op, BFA_OP_CONTRIB, 'contrib'
	__bfa_opname_case op, BFA_OP_REDFIN, 'redfin'
	__bfa_opname_case op, BFA_OP_SLOT, 'slot'
	__bfa_opname_case op, BFA_OP_LOAD, 'load'
	__bfa_opname_case op, BFA_OP_STORE, 'store'
	__bfa_opname_case op, BFA_OP_LOADBITS, 'loadbits'
	__bfa_opname_case op, BFA_OP_STOREBITS, 'storebits'
	__bfa_opname_case op, BFA_OP_COPY, 'copy'
	__bfa_opname_case op, BFA_OP_ADDR, 'addr'
	__bfa_opname_case op, BFA_OP_INDEX, 'index'
	__bfa_opname_case op, BFA_OP_GADDR, 'gaddr'
	__bfa_opname_case op, BFA_OP_CHK, 'chk'
	__bfa_opname_case op, BFA_OP_RETAIN, 'retain'
	__bfa_opname_case op, BFA_OP_RELEASE, 'release'
	__bfa_opname_case op, BFA_OP_CALL, 'call'
	__bfa_opname_case op, BFA_OP_CALLIND, 'callind'
	__bfa_opname_case op, BFA_OP_RET, 'ret'
	__bfa_opname_case op, BFA_OP_JMP, 'jmp'
	__bfa_opname_case op, BFA_OP_BR, 'br'
	__bfa_opname_case op, BFA_OP_TRAP, 'trap'
	__bfa_opname_case op, BFA_OP_PHI, 'phi'
	__bfa_opname_case op, BFA_OP_PARAM, 'param'
	__bfa_opname_case op, BFA_OP_ICONST, 'iconst'
	__bfa_opname_case op, BFA_OP_FCONST, 'fconst'
	__bfa_opname_case op, BFA_OP_VADD, 'vadd'
	__bfa_opname_case op, BFA_OP_VSUB, 'vsub'
	__bfa_opname_case op, BFA_OP_VMUL, 'vmul'
	__bfa_opname_case op, BFA_OP_VDIV, 'vdiv'

	cmp	dword [op], BFA_OP_CMP
	je	.docmp
	cmp	dword [op], BFA_OP_FCMP
	je	.dofcmp
	; unreachable: every real opcode is one of the cases above
	db	0x0F, 0x0B

  .docmp:
	mov	rdi, [ctx]
	jmp	.after_cmp
  .lit_cmp db "cmp"
  .after_cmp:
	lea	rsi, [.lit_cmp]
	mov	rdx, .after_cmp - .lit_cmp
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [aux]
	and	esi, 7
	call	__bfa_print_predname
	return
  .dofcmp:
	mov	rdi, [ctx]
	jmp	.after_fcmp
  .lit_fcmp db "fcmp"
  .after_fcmp:
	lea	rsi, [.lit_fcmp]
	mov	rdx, .after_fcmp - .lit_fcmp
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [aux]
	and	esi, 7
	call	__bfa_print_predname
	return
endp

; __bfa_print_predname(ctx, pred) -- prints ".eq"/".ne"/.../".ge".
proc __bfa_print_predname, ctx, pred
	locals
	endl
	mov	rdi, [ctx]
	mov	esi, '.'
	call	__bfa_out_byte

	cmp	dword [pred], BFA_PRED_EQ
	jne	.notEQ
	mov	rdi, [ctx]
	jmp	.after_eq
  .lit_eq db "eq"
  .after_eq:
	lea	rsi, [.lit_eq]
	mov	rdx, .after_eq - .lit_eq
	call	__bfa_out_lit
	return
  .notEQ:
	cmp	dword [pred], BFA_PRED_NE
	jne	.notNE
	mov	rdi, [ctx]
	jmp	.after_ne
  .lit_ne db "ne"
  .after_ne:
	lea	rsi, [.lit_ne]
	mov	rdx, .after_ne - .lit_ne
	call	__bfa_out_lit
	return
  .notNE:
	cmp	dword [pred], BFA_PRED_LT
	jne	.notLT
	mov	rdi, [ctx]
	jmp	.after_lt
  .lit_lt db "lt"
  .after_lt:
	lea	rsi, [.lit_lt]
	mov	rdx, .after_lt - .lit_lt
	call	__bfa_out_lit
	return
  .notLT:
	cmp	dword [pred], BFA_PRED_LE
	jne	.notLE
	mov	rdi, [ctx]
	jmp	.after_le
  .lit_le db "le"
  .after_le:
	lea	rsi, [.lit_le]
	mov	rdx, .after_le - .lit_le
	call	__bfa_out_lit
	return
  .notLE:
	cmp	dword [pred], BFA_PRED_GT
	jne	.notGT
	mov	rdi, [ctx]
	jmp	.after_gt
  .lit_gt db "gt"
  .after_gt:
	lea	rsi, [.lit_gt]
	mov	rdx, .after_gt - .lit_gt
	call	__bfa_out_lit
	return
  .notGT:
	; BFA_PRED_GE
	mov	rdi, [ctx]
	jmp	.after_ge
  .lit_ge db "ge"
  .after_ge:
	lea	rsi, [.lit_ge]
	mov	rdx, .after_ge - .lit_ge
	call	__bfa_out_lit
	return
endp

; ============================================================================
; value / block reference printing (looks up the pass-1 renumbering tables)
; ============================================================================

proc __bfa_out_sp, ctx
	locals
	endl
	mov	rdi, [ctx]
	mov	esi, ' '
	call	__bfa_out_byte
	return
endp

proc __bfa_print_val, ctx, valueid
	locals
		slot n, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.val_print]
	mov	esi, [valueid]
	dec	esi
	call	vec_get
	mov	ecx, [rax]
	mov	[n], ecx
	mov	rdi, [ctx]
	mov	esi, '%'
	call	__bfa_out_byte
	mov	rdi, [ctx]
	mov	esi, [n]
	call	__bfa_out_u64
	return
endp

proc __bfa_print_blk, ctx, blockid
	locals
		slot n, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.blk_print]
	mov	esi, [blockid]
	dec	esi
	call	vec_get
	mov	ecx, [rax]
	mov	[n], ecx
	mov	rdi, [ctx]
	mov	esi, 'b'
	call	__bfa_out_byte
	mov	rdi, [ctx]
	mov	esi, [n]
	call	__bfa_out_u64
	return
endp

; __bfa_print_valorimm(ctx, raw, isimm) -- `raw` is either an internal value
; id or (iff `isimm`) a 32-bit two's-complement immediate to sign-extend and
; print in decimal. The one place this file's operand model departs from
; "every operand is a value" -- see `__bfa_val_or_imm` in parse.inc for why.
proc __bfa_print_valorimm, ctx, raw, isimm
	locals
	endl
	cmp	dword [isimm], 0
	jne	.imm
	mov	rdi, [ctx]
	mov	esi, [raw]
	call	__bfa_print_val
	return
  .imm:
	mov	rdi, [ctx]
	movsxd	rsi, dword [raw]
	call	__bfa_out_i64
	return
endp

; __bfa_print_order(ctx, ord) -- BFA_ORDER_* -> nativus/maior/minor.
proc __bfa_print_order, ctx, ord
	locals
	endl
	cmp	dword [ord], BFA_ORDER_NATIVUS
	jne	.notnat
	mov	rdi, [ctx]
	jmp	.after_nat
  .lit_nat db "nativus"
  .after_nat:
	lea	rsi, [.lit_nat]
	mov	rdx, .after_nat - .lit_nat
	call	__bfa_out_lit
	return
  .notnat:
	cmp	dword [ord], BFA_ORDER_MAIOR
	jne	.notmaior
	mov	rdi, [ctx]
	jmp	.after_maior
  .lit_maior db "maior"
  .after_maior:
	lea	rsi, [.lit_maior]
	mov	rdx, .after_maior - .lit_maior
	call	__bfa_out_lit
	return
  .notmaior:
	mov	rdi, [ctx]
	jmp	.after_minor
  .lit_minor db "minor"
  .after_minor:
	lea	rsi, [.lit_minor]
	mov	rdx, .after_minor - .lit_minor
	call	__bfa_out_lit
	return
endp

; __bfa_print_rshape(ctx, shp) -- BFA_RSHAPE_* -> ordinata/arborea.
proc __bfa_print_rshape, ctx, shp
	locals
	endl
	cmp	dword [shp], BFA_RSHAPE_ORDINATA
	jne	.notord
	mov	rdi, [ctx]
	jmp	.after_ord
  .lit_ord db "ordinata"
  .after_ord:
	lea	rsi, [.lit_ord]
	mov	rdx, .after_ord - .lit_ord
	call	__bfa_out_lit
	return
  .notord:
	mov	rdi, [ctx]
	jmp	.after_arb
  .lit_arb db "arborea"
  .after_arb:
	lea	rsi, [.lit_arb]
	mov	rdx, .after_arb - .lit_arb
	call	__bfa_out_lit
	return
endp

; ============================================================================
; per-instruction operand printing
; ============================================================================

; __bfa_print_operands(ctx, func, instptr) -- prints everything after the
; opcode name on the line (a leading space, then the operands themselves,
; in text order; no trailing space or newline).
proc __bfa_print_operands, ctx, func, instptr
	locals
		slot shp, dd
		slot opv, dd
		slot auxv, dd
		slot tyv, dd
		slot av, dd
		slot bv, dd
		slot i, dd
		slot n, dd
		slot lo, dd
		slot hi, dd
		slot tmp, dd
	endl
	mov	rax, [instptr]
	movzx	ecx, word [rax + BfaInst.op]
	mov	[opv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[auxv], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[bv], ecx

	mov	edi, [opv]
	call	__bfa_op_shape
	mov	[shp], eax

	cmp	dword [shp], BFA_SH_BIN
	je	.dBIN
	cmp	dword [shp], BFA_SH_CMP
	je	.dBIN
	cmp	dword [shp], BFA_SH_FCMP
	je	.dBIN
	cmp	dword [shp], BFA_SH_UN
	je	.dUN
	cmp	dword [shp], BFA_SH_FMA
	je	.dFMA
	cmp	dword [shp], BFA_SH_REDINIT
	je	.dREDINIT
	cmp	dword [shp], BFA_SH_CONTRIB
	je	.dCONTRIB
	cmp	dword [shp], BFA_SH_REDFIN
	je	.dREDFIN
	cmp	dword [shp], BFA_SH_SLOT
	je	.dSLOT
	cmp	dword [shp], BFA_SH_LOAD
	je	.dLOAD
	cmp	dword [shp], BFA_SH_STORE
	je	.dSTORE
	cmp	dword [shp], BFA_SH_LOADBITS
	je	.dLOADBITS
	cmp	dword [shp], BFA_SH_STOREBITS
	je	.dSTOREBITS
	cmp	dword [shp], BFA_SH_COPY
	je	.dCOPY
	cmp	dword [shp], BFA_SH_ADDR
	je	.dADDR
	cmp	dword [shp], BFA_SH_INDEX
	je	.dINDEX
	cmp	dword [shp], BFA_SH_GADDR
	je	.dGADDR
	cmp	dword [shp], BFA_SH_CHK
	je	.dCHK
	cmp	dword [shp], BFA_SH_RETAIN
	je	.dRETAIN
	cmp	dword [shp], BFA_SH_CALL
	je	.dCALL
	cmp	dword [shp], BFA_SH_CALLIND
	je	.dCALLIND
	cmp	dword [shp], BFA_SH_RET
	je	.dRET
	cmp	dword [shp], BFA_SH_JMP
	je	.dJMP
	cmp	dword [shp], BFA_SH_BR
	je	.dBR
	cmp	dword [shp], BFA_SH_TRAP
	je	.dTRAP
	cmp	dword [shp], BFA_SH_PHI
	je	.dPHI
	cmp	dword [shp], BFA_SH_PARAM
	je	.dPARAM
	cmp	dword [shp], BFA_SH_ICONST
	je	.dICONST
	cmp	dword [shp], BFA_SH_FCONST
	je	.dFCONST
	db	0x0F, 0x0B

  .dBIN:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [av]
	mov	edx, [auxv]
	and	edx, 0x100
	call	__bfa_print_valorimm
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [bv]
	mov	edx, [auxv]
	and	edx, 0x200
	call	__bfa_print_valorimm
	return

  .dUN:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_print_val
	return

  .dFMA:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	return

  .dREDINIT:
	; tyv is red.F; print the INNER type F, not red.F itself (the leading
	; "red." was never part of the source token here -- redinit's own
	; grammar is "redinit F op shape w", F alone).
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	esi, [tyv]
	call	bfa_type_ptr
	mov	ecx, [rax + BfaType.ref]
	mov	[tmp], ecx
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [auxv]
	call	intern_bytes			; rax=ptr, rdx=len
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_print_rshape
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [bv]
	call	__bfa_out_u64
	return

  .dCONTRIB:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [bv]
	call	__bfa_print_val
	return

  .dREDFIN:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_print_val
	return

  .dSLOT:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [bv]
	call	__bfa_out_u64
	return

  .dLOAD:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_print_order
	return

  .dSTORE:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_order
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, 3
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	return

  .dLOADBITS:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	return

  .dSTOREBITS:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, 3
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	return

  .dCOPY:
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	return

  .dADDR:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [bv]
	call	__bfa_out_u64
	return

  .dINDEX:
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	return

  .dGADDR:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_out_u64
	return

  .dCHK:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [bv]
	call	__bfa_print_val
	return

  .dRETAIN:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_print_val
	return

  .dCALL:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, '@'
	call	__bfa_out_byte
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get		; eax = 1-based func index
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, [tmp]
	dec	esi
	call	vec_get			; rax = ptr to a BfaFunc* slot
	mov	rax, [rax]		; deref -> BfaFunc*
	mov	eax, [rax + BfaFunc.name]
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [tmp]
	call	intern_bytes		; rax=ptr, rdx=len
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit
	mov	dword [i], 1
  .callargloop:
	mov	eax, [i]
	cmp	eax, [bv]
	jge	.callargdone
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, [i]
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.callargloop
  .callargdone:
	return

  .dCALLIND:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	dword [i], 1
  .cindargloop:
	mov	eax, [i]
	cmp	eax, [bv]
	jge	.cindargdone
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, [i]
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.cindargloop
  .cindargdone:
	return

  .dRET:
	cmp	dword [av], 0
	je	.retvoiddone
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_print_val
  .retvoiddone:
	return

  .dJMP:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_print_blk
	return

  .dBR:
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_blk
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_blk
	return

  .dTRAP:
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [auxv]
	call	intern_bytes
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit
	return

  .dPHI:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	dword [i], 0
  .philoop:
	mov	eax, [i]
	cmp	eax, [bv]
	jge	.phidone
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, [i]
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_blk
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	eax, [av]
	add	eax, [i]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[tmp], eax
	mov	rdi, [ctx]
	mov	esi, [tmp]
	call	__bfa_print_val
	mov	eax, [i]
	add	eax, 2
	mov	[i], eax
	jmp	.philoop
  .phidone:
	return

  .dPARAM:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_out_u64
	return

  .dICONST:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	[lo], eax
	mov	rdi, [func]
	mov	eax, [av]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[hi], eax
	; reconstruct the 64-bit value into n:lo pair via shl/or on rax
	mov	eax, [hi]
	shl	rax, 32
	mov	ecx, [lo]
	or	rax, rcx
	mov	[tmp], eax			; low 32 (unused further)
	push	rax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	esi, [tyv]
	call	bfa_type_ptr
	; the KIND, not BfaType.sign: the lowering interns an iN with sign 0
	; (emit.inc's __bfa_ty_is_signed has the whole story)
	movzx	ecx, byte [rax + BfaType.kind]
	pop	rax
	cmp	ecx, BFA_TK_I
	jne	.iconst_unsigned
	mov	rdi, [ctx]
	mov	rsi, rax
	call	__bfa_out_i64
	return
  .iconst_unsigned:
	mov	rdi, [ctx]
	mov	rsi, rax
	call	__bfa_out_u64
	return

  .dFCONST:
	mov	rdi, [ctx]
	mov	esi, [tyv]
	call	__bfa_print_type
	mov	rdi, [ctx]
	call	__bfa_out_sp
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_extra_get
	mov	[lo], eax
	mov	rdi, [func]
	mov	eax, [av]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[hi], eax
	mov	eax, [hi]
	shl	rax, 32
	mov	ecx, [lo]
	or	rax, rcx
	mov	rdi, [ctx]
	mov	rsi, rax
	call	__bfa_out_u64
	return
endp

; ============================================================================
; void-opcode test, hex-byte printing (for `data` globals)
; ============================================================================

; __bfa_op_is_void(op, ty) -> eax = 1 iff this opcode never produces a
; value. `call`/`callind` are conditional on `ty` (0 = void callee).
proc __bfa_op_is_void, op, ty
	locals
	endl
	mov	eax, [op]
	cmp	eax, BFA_OP_CONTRIB
	je	.yes
	cmp	eax, BFA_OP_STORE
	je	.yes
	cmp	eax, BFA_OP_STOREBITS
	je	.yes
	cmp	eax, BFA_OP_COPY
	je	.yes
	cmp	eax, BFA_OP_CHK
	je	.yes
	cmp	eax, BFA_OP_RETAIN
	je	.yes
	cmp	eax, BFA_OP_RELEASE
	je	.yes
	cmp	eax, BFA_OP_RET
	je	.yes
	cmp	eax, BFA_OP_JMP
	je	.yes
	cmp	eax, BFA_OP_BR
	je	.yes
	cmp	eax, BFA_OP_TRAP
	je	.yes
	cmp	eax, BFA_OP_CALL
	je	.maybe
	cmp	eax, BFA_OP_CALLIND
	je	.maybe
	mov	eax, 0
	return
  .yes:
	mov	eax, 1
	return
  .maybe:
	cmp	dword [ty], 0
	jne	.no2
	mov	eax, 1
	return
  .no2:
	mov	eax, 0
	return
endp

proc __bfa_out_hexnibble, ctx, nib
	locals
	endl
	mov	eax, [nib]
	cmp	eax, 10
	jl	.digit
	add	eax, 'a' - 10
	jmp	.emit
  .digit:
	add	eax, '0'
  .emit:
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_byte
	return
endp

proc __bfa_out_hexbyte, ctx, val
	locals
	endl
	mov	eax, [val]
	shr	eax, 4
	and	eax, 0xF
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_hexnibble
	mov	eax, [val]
	and	eax, 0xF
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_hexnibble
	return
endp

; ============================================================================
; global printing: `data $ID SIZE ALIGN HEXBYTES`
; ============================================================================

proc __bfa_print_global, ctx, gptr
	locals
		slot i, dd
		slot n, dd
		slot off, dd
	endl
	mov	rdi, [ctx]
	jmp	.after_data
  .lit_data db "data $"
  .after_data:
	lea	rsi, [.lit_data]
	mov	rdx, .after_data - .lit_data
	call	__bfa_out_lit

	mov	rax, [gptr]
	mov	ecx, [rax + BfaGlobal.id]
	mov	rdi, [ctx]
	mov	esi, ecx
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_out_sp

	mov	rax, [gptr]
	mov	ecx, [rax + BfaGlobal.size]
	mov	rdi, [ctx]
	mov	esi, ecx
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_out_sp

	mov	rax, [gptr]
	mov	ecx, [rax + BfaGlobal.algn]
	mov	rdi, [ctx]
	mov	esi, ecx
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_out_sp

	mov	rax, [gptr]
	mov	ecx, [rax + BfaGlobal.data_len]
	mov	[n], ecx
	mov	rax, [gptr]
	mov	ecx, [rax + BfaGlobal.data_off]
	mov	[off], ecx
	mov	dword [i], 0
  .hexloop:
	mov	eax, [i]
	cmp	eax, [n]
	jge	.hexdone
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.gdata]
	mov	eax, [off]
	add	eax, [i]
	mov	esi, eax
	call	vec_get
	movzx	ecx, byte [rax]
	mov	rdi, [ctx]
	mov	esi, ecx
	call	__bfa_out_hexbyte
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.hexloop
  .hexdone:
	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte
	return
endp

; ============================================================================
; pass 1: renumbering. blk_print is filled by plain append (block visitation
; order via the array IS internal-id order, always -- bfa_block_new's
; return is exactly index+1). val_print is pre-sized to func.insts.len and
; filled by INDEXED set, not append: a phi's internal id is not guaranteed
; less than an earlier body instruction's id unless phis truly precede the
; body in the source text (the common, spec-intended case, but not one this
; file's parser rejects if violated -- ssa-ir.md section 3's "phis first"
; rule is the VERIFIER's job, not parsed-order's). Indexed set is correct
; regardless of which order text happened to declare things in.
; ============================================================================

proc __bfa_print_pass1, ctx, func
	locals
		slot nblocks, dd
		slot ninsts, dd
		slot bi, dd
		slot blkid, dd
		slot bnctr, dd
		slot valnctr, dd
		slot cur, dd
		slot opv, dd
		slot tyv, dd
		slot voidflag, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.arena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	rcx, rax
	mov	rdi, [ctx]
	mov	[rdi + BfaPrintCtx.blk_print], rcx
	mov	rdi, rcx
	mov	rsi, [ctx]
	mov	rsi, [rsi + BfaPrintCtx.module]
	mov	rsi, [rsi + BfaModule.arena]
	mov	rdx, 4
	xor	rcx, rcx
	call	vec_init

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.arena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	rcx, rax
	mov	rdi, [ctx]
	mov	[rdi + BfaPrintCtx.val_print], rcx
	mov	rdi, rcx
	mov	rsi, [ctx]
	mov	rsi, [rsi + BfaPrintCtx.module]
	mov	rsi, [rsi + BfaModule.arena]
	mov	rdx, 4
	xor	rcx, rcx
	call	vec_init

	; pre-size val_print to func.insts.len, every slot = sentinel
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	mov	[ninsts], eax
	mov	dword [bi], 0			; reused as a plain loop counter here
  .presize:
	mov	eax, [bi]
	cmp	eax, [ninsts]
	jge	.presized
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.val_print]
	call	vec_push
	mov	dword [rax], 0xFFFFFFFF
	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.presize
  .presized:

	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.blocks]
	mov	rax, [rdi + Vec.len]
	mov	[nblocks], eax

	mov	dword [bi], 0
	mov	dword [bnctr], 0
	mov	dword [valnctr], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [nblocks]
	jge	.alldone
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.blk_print]
	call	vec_push
	mov	ecx, [bnctr]
	mov	[rax], ecx
	inc	ecx
	mov	[bnctr], ecx

	mov	rdi, [func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_phi]
	mov	[cur], ecx
  .philoop:
	cmp	dword [cur], 0
	je	.phidone
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.val_print]
	mov	eax, [cur]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [valnctr]
	mov	[rax], ecx
	inc	ecx
	mov	[valnctr], ecx

	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	eax, [cur]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.philoop
  .phidone:

	mov	rdi, [func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first]
	mov	[cur], ecx
  .bodyloop:
	cmp	dword [cur], 0
	je	.bodydone
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	eax, [cur]
	dec	eax
	mov	esi, eax
	call	vec_get
	movzx	ecx, word [rax + BfaInst.op]
	mov	[opv], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx

	mov	edi, [opv]
	mov	esi, [tyv]
	call	__bfa_op_is_void
	mov	[voidflag], eax

	cmp	dword [voidflag], 0
	jne	.bodyadvance
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.val_print]
	mov	eax, [cur]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [valnctr]
	mov	[rax], ecx
	inc	ecx
	mov	[valnctr], ecx

  .bodyadvance:
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	eax, [cur]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.bodyloop
  .bodydone:

	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.blockloop
  .alldone:
	return
endp

; ============================================================================
; pass 2: block headers, phi lines, body lines
; ============================================================================

; __bfa_print_one_inst(ctx, func, instid) -- one Line, with its trailing
; newline. `pending_result`-equivalent here is simply "does val_print say
; this instruction got a number" -- looked up once, not tracked as parser
; state, since printing has no lookahead problem to solve.
proc __bfa_print_one_inst, ctx, func, instid
	locals
		slot vn, dd
		slot opv, dd
		slot auxv, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.val_print]
	mov	eax, [instid]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	mov	[vn], ecx

	cmp	dword [vn], 0xFFFFFFFF
	je	.novalue
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_print_val
	jmp	.after_eqlit
  .lit_eqlit db " = "
  .after_eqlit:
	mov	rdi, [ctx]
	lea	rsi, [.lit_eqlit]
	mov	rdx, .after_eqlit - .lit_eqlit
	call	__bfa_out_lit
  .novalue:

	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[opv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[auxv], ecx

	mov	rdi, [ctx]
	mov	esi, [opv]
	mov	edx, [auxv]
	call	__bfa_print_opname

	mov	rdi, [ctx]
	call	__bfa_out_sp

	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	rdx, rax
	call	__bfa_print_operands

	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte
	return
endp

; __bfa_print_body(ctx, func) -- walks blocks in creation order, printing
; `bN:` (with `quisque` if flagged), its phi list, then its body list.
proc __bfa_print_body, ctx, func
	locals
		slot nblocks, dd
		slot bi, dd
		slot blkid, dd
		slot cur, dd
		slot flg, dd
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.blocks]
	mov	rax, [rdi + Vec.len]
	mov	[nblocks], eax
	mov	dword [bi], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [nblocks]
	jge	.done
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [ctx]
	mov	esi, 'b'
	call	__bfa_out_byte
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.blk_print]
	mov	eax, [blkid]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	mov	rdi, [ctx]
	mov	esi, ecx
	call	__bfa_out_u64
	mov	rdi, [ctx]
	mov	esi, ':'
	call	__bfa_out_byte

	mov	rdi, [func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.flags]
	mov	[flg], ecx
	test	dword [flg], BFA_BLOCK_FLAG_QUISQUE
	jz	.noquisque
	mov	rdi, [ctx]
	call	__bfa_out_sp
	jmp	.after_quisque
  .lit_quisque db "quisque"
  .after_quisque:
	mov	rdi, [ctx]
	lea	rsi, [.lit_quisque]
	mov	rdx, .after_quisque - .lit_quisque
	call	__bfa_out_lit
  .noquisque:
	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte

	mov	rdi, [func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_phi]
	mov	[cur], ecx
  .philoop:
	cmp	dword [cur], 0
	je	.phidone
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [cur]
	call	__bfa_print_one_inst
	mov	rdi, [func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.philoop
  .phidone:

	mov	rdi, [func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first]
	mov	[cur], ecx
  .bodyloop:
	cmp	dword [cur], 0
	je	.bodydone
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [cur]
	call	__bfa_print_one_inst
	mov	rdi, [func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.bodyloop
  .bodydone:

	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.blockloop
  .done:
	return
endp

; ============================================================================
; function header (name, params, return type, attrs) and the whole function
; ============================================================================

proc __bfa_print_params, ctx, sigptr
	locals
		slot i, dd
		slot n, dd
		slot pf, dd
		slot tid, dd
	endl
	mov	rdi, [ctx]
	mov	esi, '('
	call	__bfa_out_byte
	mov	rax, [sigptr]
	mov	ecx, [rax + BfaSig.p_count]
	mov	[n], ecx
	mov	ecx, [rax + BfaSig.p_first]
	mov	[pf], ecx
	mov	dword [i], 0
  .loop:
	mov	eax, [i]
	cmp	eax, [n]
	jge	.done
	cmp	dword [i], 0
	je	.nosp
	mov	rdi, [ctx]
	call	__bfa_out_sp
  .nosp:
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.sig_extra]
	mov	eax, [pf]
	add	eax, [i]
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	mov	[tid], ecx
	mov	rdi, [ctx]
	mov	esi, [tid]
	call	__bfa_print_type
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.loop
  .done:
	mov	rdi, [ctx]
	mov	esi, ')'
	call	__bfa_out_byte
	return
endp

proc __bfa_print_attrs, ctx, funcptr
	locals
		slot idv, dd
	endl
	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.numeri0]
	test	ecx, ecx
	jz	.nonumeri
	jmp	.after_numeri
  .lit_numeri db " numeri "
  .after_numeri:
	mov	rdi, [ctx]
	lea	rsi, [.lit_numeri]
	mov	rdx, .after_numeri - .lit_numeri
	call	__bfa_out_lit

	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.numeri0]
	mov	[idv], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [idv]
	call	intern_bytes
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit
	mov	rdi, [ctx]
	call	__bfa_out_sp

	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.numeri1]
	mov	[idv], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [idv]
	call	intern_bytes
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit
	mov	rdi, [ctx]
	call	__bfa_out_sp

	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.numeri2]
	mov	[idv], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [idv]
	call	intern_bytes
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit
	mov	rdi, [ctx]
	call	__bfa_out_sp

	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.numeri3]
	mov	[idv], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [idv]
	call	intern_bytes
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit
  .nonumeri:

	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.attrs]
	test	ecx, BFA_FUNC_ATTR_NUCLEUS
	jz	.nonucleus
	jmp	.after_nuc
  .lit_nuc db " nucleus"
  .after_nuc:
	mov	rdi, [ctx]
	lea	rsi, [.lit_nuc]
	mov	rdx, .after_nuc - .lit_nuc
	call	__bfa_out_lit
  .nonucleus:

	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.attrs]
	test	ecx, BFA_FUNC_ATTR_EXTERNUS
	jz	.noext
	jmp	.after_ext
  .lit_ext db " externus "
  .after_ext:
	mov	rdi, [ctx]
	lea	rsi, [.lit_ext]
	mov	rdx, .after_ext - .lit_ext
	call	__bfa_out_lit
	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.extern_abi]
	mov	[idv], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [idv]
	call	intern_bytes
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit
  .noext:
	return
endp

proc __bfa_print_function, ctx, funcptr
	locals
		slot sigidx, dd
		slot sigptr, dq
		slot nameid, dd
	endl
	mov	rdi, [ctx]
	jmp	.after_functio
  .lit_functio db "functio @"
  .after_functio:
	lea	rsi, [.lit_functio]
	mov	rdx, .after_functio - .lit_functio
	call	__bfa_out_lit

	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.name]
	mov	[nameid], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [nameid]
	call	intern_bytes
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit
	mov	rdi, [ctx]
	call	__bfa_out_sp

	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.sig]
	mov	[sigidx], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.module]
	mov	rdi, [rdi + BfaModule.sigs]
	mov	esi, [sigidx]
	call	vec_get
	mov	[sigptr], rax

	mov	rdi, [ctx]
	mov	rsi, [sigptr]
	call	__bfa_print_params

	jmp	.after_arrow
  .lit_arrow db " -> "
  .after_arrow:
	mov	rdi, [ctx]
	lea	rsi, [.lit_arrow]
	mov	rdx, .after_arrow - .lit_arrow
	call	__bfa_out_lit

	mov	rax, [sigptr]
	mov	ecx, [rax + BfaSig.ret_ty]
	mov	rdi, [ctx]
	mov	esi, ecx
	call	__bfa_print_type

	mov	rdi, [ctx]
	mov	rsi, [funcptr]
	call	__bfa_print_attrs

	jmp	.after_brace
  .lit_brace db " {"
  .after_brace:
	mov	rdi, [ctx]
	lea	rsi, [.lit_brace]
	mov	rdx, .after_brace - .lit_brace
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte

	mov	rdi, [ctx]
	mov	rsi, [funcptr]
	call	__bfa_print_pass1
	mov	rdi, [ctx]
	mov	rsi, [funcptr]
	call	__bfa_print_body

	jmp	.after_close
  .lit_close db "}"
  .after_close:
	mov	rdi, [ctx]
	lea	rsi, [.lit_close]
	mov	rdx, .after_close - .lit_close
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte
	return
endp

; ============================================================================
; module top level
; ============================================================================

; bfa_print_module(module, arena) -> rax = ptr, rdx = len -- the whole
; module's canonical text. `arena` backs both the output buffer and the
; per-function pass-1 tables; nothing here is written back to `module`.
proc bfa_print_module, module, arena
	locals
		slot ctx, dq
		slot i, dd
		slot n, dd
	endl
	mov	rdi, [arena]
	mov	rsi, sizeof.BfaPrintCtx
	call	arena_alloc
	mov	[ctx], rax
	mov	rcx, [module]
	mov	[rax + BfaPrintCtx.module], rcx

	mov	rdi, [arena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	rcx, rax
	mov	rdi, [ctx]
	mov	[rdi + BfaPrintCtx.obuf], rcx
	mov	rdi, rcx
	mov	rsi, [arena]
	mov	rdx, 1
	mov	rcx, 64
	call	vec_init

	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.globals]
	mov	rax, [rdi + Vec.len]
	mov	[n], eax
	mov	dword [i], 0
  .globalloop:
	mov	eax, [i]
	cmp	eax, [n]
	jge	.globalsdone
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.globals]
	mov	esi, [i]
	call	vec_get
	mov	rdi, [ctx]
	mov	rsi, rax
	call	__bfa_print_global
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.globalloop
  .globalsdone:

	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	rax, [rdi + Vec.len]
	mov	[n], eax
	mov	dword [i], 0
  .funcloop:
	mov	eax, [i]
	cmp	eax, [n]
	jge	.funcsdone
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, [i]
	call	vec_get
	mov	rax, [rax]			; deref the pointer-to-Func
	mov	rdi, [ctx]
	mov	rsi, rax
	call	__bfa_print_function
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.funcloop
  .funcsdone:

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.obuf]
	mov	rax, [rdi + Vec.data]
	mov	rdx, [rdi + Vec.len]
	return
endp
