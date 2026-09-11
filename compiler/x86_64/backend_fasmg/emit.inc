; compiler/x86_64/backend_fasmg/emit.inc
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
; The naive fasmg emitter: spec section 9.2 -- "every value spilled to a
; stack slot, no register allocation, slow by construction and correct by
; construction." `bfa_emit_module` produces fasmg SOURCE TEXT (assembly, not
; machine code), meant to be `include`d inside a hand-written harness's own
; `segment readable executable` block -- exactly the way `tests/unit/*.asm`
; already includes rt/ library files, so no `format`/`segment`/`entry` of
; its own and no dependency on macros/proc.inc or any rt/ runtime
; convention: emitted functions hand-roll their own SysV prologue/epilogue,
; because the CALLING CONVENTION THIS FILE OWES ITS OUTPUT is plain SysV
; AMD64 (spec section 5.3), not this compiler's own internal r15-pinned
; convention (docs/asm-conventions.md section 1.2 pins r15 for `exsc`'s OWN
; internals; a compiled Exsecutor program is not `exsc`'s internals).
;
; SLOT LAYOUT: one 8-byte stack slot per INTERNAL value id (dense, 1-based,
; including void instructions' unused ids for simplicity) -- slot for value
; v is `[rbp - 8*v]`. Uniform width regardless of the value's IR type is the
; naive choice this whole backend is built on; a `u8` wastes seven bytes of
; its own slot and nothing reads them.
;
; NO REGISTER ALLOCATION: every operand is reloaded from its slot
; immediately before use and every result is stored back immediately after
; -- the literal naive contract. `rax`/`rcx`/`rdx` are scratch, never
; preserved across an instruction boundary, because nothing needs them to
; be.
;
; TIER 1 ONLY, reported honestly rather than half-covering everything:
; `param`, `iconst` (64-bit magnitude only), `add`/`sub`/`mul` (trapping,
; `i64`/`u64` only -- signed overflow via OF, unsigned via CF, exactly as
; x86 ADD/SUB/IMUL/MUL already distinguish), `addw`/`subw`/`mulw` (wrapping,
; `i64`/`u64` only -- natural 64-bit wraparound, no check needed), `cmp.*`
; (`i64`/`u64` only, signed vs unsigned SETcc chosen from the type's own
; sign bit), `jmp`, `br`, `ret`. Every other opcode this file's own
; `__bfa_emit_die` refuses by name rather than emitting something wrong --
; see the report for exactly what is not here and why (mainly: no floats,
; no memory ops, no calls, no reduction, no ARC, no phi/parallel-copy, no
; narrower-than-64-bit integer width). ADR 0012 already prices "a naive
; backend still needs an instruction selector for every IR op" as real,
; unscheduled engineering; this is the first slice of it, not all of it.
;
; TRAP STUB: every trapping/overflow-checked lowering jumps to one shared
; `bfausr_trap` label (`ud2`, exactly `macros/assert.inc`'s own `rassert`
; encoding: `db 0x0F, 0x0B` -- there is no symbolic mnemonic for it in
; vendor/fasmg-x86 either). SIGILL, exit 132, the same convention this
; whole tree already uses for a violated internal contract -- here, for a
; violated NUMERIC one (spec section 5.4's trapping `+`).
;
; Spec: docs/spec/exsecutor-spec-v0.4.md section 9.2, section 5.3 (SysV is
; the ABI this output owes its callers), section 5.4 (the numeric
; guarantees this backend exists to make testable at all -- ADR 0012).
; -----------------------------------------------------------------------------

; `verify.inc`, not `print.inc`: both of them included print.inc, which made
; this directory's own graph a DIAMOND -- reachable only if no consumer ever
; pulled emit.inc and verify.inc on separate paths, which stopped being true
; the moment the lowering wanted the verifier and the driver wanted the
; emitter. The chain is now strictly linear, top to bottom:
;   program.inc -> emit.inc -> verify.inc -> print.inc -> parse.inc -> ir.inc
include 'verify.inc'


; __bfa_emit_die(msg_ptr, msg_len) -- never returns. An opcode/width this
; pass does not lower is reported by name and the process exits nonzero;
; emitting text for it anyway -- guessing -- is exactly what this file must
; never do (never silently miscompile).
proc __bfa_emit_die, msg_ptr, msg_len
	locals
	endl
	jmp	.after_pfx
  .pfx db "bfa: emitter: "
  .after_pfx:
	mov	edi, 2
	lea	rsi, [.pfx]
	mov	edx, .after_pfx - .pfx
	call	sys_write
	mov	edi, 2
	mov	rsi, [msg_ptr]
	mov	rdx, [msg_len]
	call	sys_write
	jmp	.after_nl
  .nl db 10
  .after_nl:
	mov	edi, 2
	lea	rsi, [.nl]
	mov	edx, 1
	call	sys_write
	mov	edi, 4
	call	sys_exit_group
endp

; __bfa_slot(ctx, valueid) -- prints "[rbp-N]" for this value's stack slot.
proc __bfa_slot, ctx, valueid
	locals
	endl
	mov	rdi, [ctx]
	jmp	.after_open
  .lit_open db "[rbp-"
  .after_open:
	lea	rsi, [.lit_open]
	mov	rdx, .after_open - .lit_open
	call	__bfa_out_lit
	mov	eax, [valueid]
	shl	eax, 3			; * 8
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	mov	rdi, [ctx]
	mov	esi, ']'
	call	__bfa_out_byte
	return
endp

proc __bfa_emit_nl, ctx
	locals
	endl
	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte
	return
endp

; __bfa_emit_line3(ctx, mnem, valueid) -- "MNEM [rbp-N]\n" (e.g. "mov rax, ").
; A tiny handful of shared textual patterns, factored out because every
; per-opcode lowering below is built from them.
proc __bfa_out_movq_rax_slot, ctx, valueid
	locals
	endl
	mov	rdi, [ctx]
	jmp	.after_lit
  .lit db "	mov	rax, "
  .after_lit:
	lea	rsi, [.lit]
	mov	rdx, .after_lit - .lit
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [valueid]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	return
endp

proc __bfa_out_movq_slot_rax, ctx, valueid
	locals
	endl
	mov	rdi, [ctx]
	jmp	.after_lit
  .lit db "	mov	qword "
  .after_lit:
	lea	rsi, [.lit]
	mov	rdx, .after_lit - .lit
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [valueid]
	call	__bfa_slot
	mov	rdi, [ctx]
	jmp	.after_lit2
  .lit2 db ", rax", 10
  .after_lit2:
	lea	rsi, [.lit2]
	mov	rdx, .after_lit2 - .lit2
	call	__bfa_out_lit
	return
endp

; __bfa_check_width64(ty, module) -- dies unless `ty` is exactly u64/i64.
; Every Tier-1 arithmetic/compare lowering below calls this first.
proc __bfa_check_width64, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	bfa_type_ptr
	movzx	ecx, word [rax + BfaType.width]
	cmp	ecx, 64
	je	.ok
	jmp	.after_msg
  .msg db "unsupported width (Tier 1 is i64/u64 only)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .ok:
	return
endp

; __bfa_ty_is_signed(module, ty) -> eax = 1 iff `ty`'s BfaType.sign is set.
proc __bfa_ty_is_signed, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	bfa_type_ptr
	movzx	eax, byte [rax + BfaType.sign]
	return
endp

; __bfa_emit_operand_text(ctx, raw, isimm) -- "[rbp-N]" or a signed decimal
; literal, whichever this operand actually is (parse.inc's `__bfa_val_or_imm`
; -- see that proc's header for why a bare integer operand exists at all).
proc __bfa_emit_operand_text, ctx, raw, isimm
	locals
	endl
	cmp	dword [isimm], 0
	jne	.imm
	mov	rdi, [ctx]
	mov	esi, [raw]
	call	__bfa_slot
	return
  .imm:
	mov	rdi, [ctx]
	movsxd	rsi, dword [raw]
	call	__bfa_out_i64
	return
endp

proc __bfa_emit_load_rax, ctx, raw, isimm
	locals
	endl
	mov	rdi, [ctx]
	jmp	.after
  .lit db "	mov	rax, "
  .after:
	lea	rsi, [.lit]
	mov	rdx, .after - .lit
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [raw]
	mov	edx, [isimm]
	call	__bfa_emit_operand_text
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	return
endp

; ============================================================================
; Tier-1 per-opcode lowering (all i64/u64 only -- __bfa_check_width64 dies
; otherwise)
; ============================================================================

; __bfa_emit_addsub(ctx, module, instptr, trap, issub) -- add/sub (trap=1)
; and addw/subw (trap=0). Signed overflow is OF (`jo`); unsigned overflow
; (a borrow, for subtraction) is CF (`jc`) -- the same ADD/SUB encoding
; either way, only the flag checked differs, exactly what x86 already gives
; for free.
proc __bfa_emit_addsub, ctx, module, func, instid
	locals
		slot av, dd
		slot bv, dd
		slot auxv, dd
		slot tyv, dd
		slot issub, dd
		slot issigned, dd
		slot trap, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_ADD
	je	.istrap
	cmp	ecx, BFA_OP_SUB
	je	.istrap
	mov	dword [trap], 0
	jmp	.trapdone
  .istrap:
	mov	dword [trap], 1
  .trapdone:
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[bv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[auxv], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_SUB
	je	.yessub
	cmp	ecx, BFA_OP_SUBW
	je	.yessub
	mov	dword [issub], 0
	jmp	.subdone
  .yessub:
	mov	dword [issub], 1
  .subdone:

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_check_width64

	mov	rdi, [ctx]
	mov	esi, [av]
	mov	edx, [auxv]
	and	edx, 0x100
	call	__bfa_emit_load_rax

	cmp	dword [issub], 0
	jne	.emitsub
	mov	rdi, [ctx]
	jmp	.after_add
  .lit_add db "	add	rax, "
  .after_add:
	lea	rsi, [.lit_add]
	mov	rdx, .after_add - .lit_add
	call	__bfa_out_lit
	jmp	.opdone
  .emitsub:
	mov	rdi, [ctx]
	jmp	.after_sub
  .lit_sub db "	sub	rax, "
  .after_sub:
	lea	rsi, [.lit_sub]
	mov	rdx, .after_sub - .lit_sub
	call	__bfa_out_lit
  .opdone:
	mov	rdi, [ctx]
	mov	esi, [bv]
	mov	edx, [auxv]
	and	edx, 0x200
	call	__bfa_emit_operand_text
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	cmp	dword [trap], 0
	je	.notrap
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_ty_is_signed
	mov	[issigned], eax
	cmp	dword [issigned], 0
	jne	.sigtrap
	mov	rdi, [ctx]
	jmp	.after_jc
  .lit_jc db "	jc	bfausr_trap", 10
  .after_jc:
	lea	rsi, [.lit_jc]
	mov	rdx, .after_jc - .lit_jc
	call	__bfa_out_lit
	jmp	.notrap
  .sigtrap:
	mov	rdi, [ctx]
	jmp	.after_jo
  .lit_jo db "	jo	bfausr_trap", 10
  .after_jo:
	lea	rsi, [.lit_jo]
	mov	rdx, .after_jo - .lit_jo
	call	__bfa_out_lit
  .notrap:

	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_mul(ctx, module, func, instid) -- mul/mulw, i64/u64 only.
; Wrapping mul always uses the signed `imul` encoding regardless of the
; type's own sign: two's-complement wraparound is bit-for-bit identical
; either way, so there is nothing to distinguish -- see this file's header.
; Trapping mul does distinguish: unsigned overflow is `mul`'s CF/OF (either
; flag, they are set identically for MUL); signed is `imul`'s OF.
proc __bfa_emit_mul, ctx, module, func, instid
	locals
		slot av, dd
		slot bv, dd
		slot auxv, dd
		slot tyv, dd
		slot trap, dd
		slot issigned, dd
		slot bisimm, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_MUL
	je	.istrap
	mov	dword [trap], 0
	jmp	.trapdone
  .istrap:
	mov	dword [trap], 1
  .trapdone:
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[bv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[auxv], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	mov	ecx, [auxv]
	and	ecx, 0x200
	mov	[bisimm], ecx

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_check_width64

	mov	rdi, [ctx]
	mov	esi, [av]
	mov	edx, [auxv]
	and	edx, 0x100
	call	__bfa_emit_load_rax

	cmp	dword [trap], 0
	je	.wrapmul
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_ty_is_signed
	mov	[issigned], eax
	cmp	dword [issigned], 0
	je	.unsignedmul

  .wrapmul:
	; signed imul (also used, unconditionally, for wrapping -- see header)
	cmp	dword [bisimm], 0
	jne	.simm
	mov	rdi, [ctx]
	jmp	.after_imulmem
  .lit_imulmem db "	imul	rax, "
  .after_imulmem:
	lea	rsi, [.lit_imulmem]
	mov	rdx, .after_imulmem - .lit_imulmem
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [bv]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	jmp	.mulopdone
  .simm:
	mov	rdi, [ctx]
	jmp	.after_imulimm
  .lit_imulimm db "	imul	rax, rax, "
  .after_imulimm:
	lea	rsi, [.lit_imulimm]
	mov	rdx, .after_imulimm - .lit_imulimm
	call	__bfa_out_lit
	mov	rdi, [ctx]
	movsxd	rsi, dword [bv]
	call	__bfa_out_i64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
  .mulopdone:
	cmp	dword [trap], 0
	je	.mulnotrap
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_ty_is_signed
	test	eax, eax
	jz	.mulnotrap		; unsigned handled below, never reaches here when trap+unsigned
	jmp	.after_jo2
  .lit_jo2 db "	jo	bfausr_trap", 10
  .after_jo2:
	mov	rdi, [ctx]
	lea	rsi, [.lit_jo2]
	mov	rdx, .after_jo2 - .lit_jo2
	call	__bfa_out_lit
	jmp	.mulnotrap

  .unsignedmul:
	cmp	dword [bisimm], 0
	jne	.uimm
	mov	rdi, [ctx]
	jmp	.after_mulmem
  .lit_mulmem db "	mul	qword "
  .after_mulmem:
	lea	rsi, [.lit_mulmem]
	mov	rdx, .after_mulmem - .lit_mulmem
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [bv]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	jmp	.uafterop
  .uimm:
	mov	rdi, [ctx]
	jmp	.after_movrcx
  .lit_movrcx db "	mov	rcx, "
  .after_movrcx:
	lea	rsi, [.lit_movrcx]
	mov	rdx, .after_movrcx - .lit_movrcx
	call	__bfa_out_lit
	mov	rdi, [ctx]
	movsxd	rsi, dword [bv]
	call	__bfa_out_i64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	jmp	.after_mulrcx
  .lit_mulrcx db "	mul	rcx", 10
  .after_mulrcx:
	mov	rdi, [ctx]
	lea	rsi, [.lit_mulrcx]
	mov	rdx, .after_mulrcx - .lit_mulrcx
	call	__bfa_out_lit
  .uafterop:
	jmp	.after_jc2
  .lit_jc2 db "	jc	bfausr_trap", 10
  .after_jc2:
	mov	rdi, [ctx]
	lea	rsi, [.lit_jc2]
	mov	rdx, .after_jc2 - .lit_jc2
	call	__bfa_out_lit

  .mulnotrap:
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_setcc(ctx, pred, issigned) -- the SETcc mnemonic text only
; (e.g. "setl"), no operand -- signed vs unsigned per the type's own sign
; bit (docs/design/ssa-ir.md section 2.3: "signedness from T"); eq/ne need
; no such choice.
proc __bfa_emit_setcc, ctx, pred, issigned
	locals
	endl
	cmp	dword [pred], BFA_PRED_EQ
	jne	.notEQ
	jmp	.after_eq
  .lit_eq db "sete"
  .after_eq:
	mov	rdi, [ctx]
	lea	rsi, [.lit_eq]
	mov	rdx, .after_eq - .lit_eq
	call	__bfa_out_lit
	return
  .notEQ:
	cmp	dword [pred], BFA_PRED_NE
	jne	.notNE
	jmp	.after_ne
  .lit_ne db "setne"
  .after_ne:
	mov	rdi, [ctx]
	lea	rsi, [.lit_ne]
	mov	rdx, .after_ne - .lit_ne
	call	__bfa_out_lit
	return
  .notNE:
	cmp	dword [issigned], 0
	jne	.sig
	cmp	dword [pred], BFA_PRED_LT
	jne	.u_notLT
	jmp	.after_setb
  .lit_setb db "setb"
  .after_setb:
	mov	rdi, [ctx]
	lea	rsi, [.lit_setb]
	mov	rdx, .after_setb - .lit_setb
	call	__bfa_out_lit
	return
  .u_notLT:
	cmp	dword [pred], BFA_PRED_LE
	jne	.u_notLE
	jmp	.after_setbe
  .lit_setbe db "setbe"
  .after_setbe:
	mov	rdi, [ctx]
	lea	rsi, [.lit_setbe]
	mov	rdx, .after_setbe - .lit_setbe
	call	__bfa_out_lit
	return
  .u_notLE:
	cmp	dword [pred], BFA_PRED_GT
	jne	.u_notGT
	jmp	.after_seta
  .lit_seta db "seta"
  .after_seta:
	mov	rdi, [ctx]
	lea	rsi, [.lit_seta]
	mov	rdx, .after_seta - .lit_seta
	call	__bfa_out_lit
	return
  .u_notGT:
	jmp	.after_setae
  .lit_setae db "setae"
  .after_setae:
	mov	rdi, [ctx]
	lea	rsi, [.lit_setae]
	mov	rdx, .after_setae - .lit_setae
	call	__bfa_out_lit
	return
  .sig:
	cmp	dword [pred], BFA_PRED_LT
	jne	.s_notLT
	jmp	.after_setl
  .lit_setl db "setl"
  .after_setl:
	mov	rdi, [ctx]
	lea	rsi, [.lit_setl]
	mov	rdx, .after_setl - .lit_setl
	call	__bfa_out_lit
	return
  .s_notLT:
	cmp	dword [pred], BFA_PRED_LE
	jne	.s_notLE
	jmp	.after_setle
  .lit_setle db "setle"
  .after_setle:
	mov	rdi, [ctx]
	lea	rsi, [.lit_setle]
	mov	rdx, .after_setle - .lit_setle
	call	__bfa_out_lit
	return
  .s_notLE:
	cmp	dword [pred], BFA_PRED_GT
	jne	.s_notGT
	jmp	.after_setg
  .lit_setg db "setg"
  .after_setg:
	mov	rdi, [ctx]
	lea	rsi, [.lit_setg]
	mov	rdx, .after_setg - .lit_setg
	call	__bfa_out_lit
	return
  .s_notGT:
	jmp	.after_setge
  .lit_setge db "setge"
  .after_setge:
	mov	rdi, [ctx]
	lea	rsi, [.lit_setge]
	mov	rdx, .after_setge - .lit_setge
	call	__bfa_out_lit
	return
endp

; __bfa_emit_cmp(ctx, module, func, instid) -- cmp.*/fcmp.* (fcmp dies for
; now -- floats are not in this pass's Tier 1). Naive on purpose: the
; result is computed into its own slot as 0/1, independently of whatever
; `br` later does with it -- no fusing, per this whole backend's "one
; instruction per IR op" rule (ADR 0012).
proc __bfa_emit_cmp, ctx, module, func, instid
	locals
		slot av, dd
		slot bv, dd
		slot auxv, dd
		slot tyv, dd
		slot pred, dd
		slot issigned, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_FCMP
	jne	.notf
	jmp	.after_nf
  .nfmsg db "fcmp: floats are not Tier 1"
  .after_nf:
	lea	rdi, [.nfmsg]
	mov	rsi, .after_nf - .nfmsg
	call	__bfa_emit_die
  .notf:
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[bv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[auxv], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	mov	ecx, [auxv]
	and	ecx, 7
	mov	[pred], ecx

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_check_width64

	mov	rdi, [ctx]
	mov	esi, [av]
	mov	edx, [auxv]
	and	edx, 0x100
	call	__bfa_emit_load_rax

	mov	rdi, [ctx]
	jmp	.after_cmp
  .lit_cmp db "	cmp	rax, "
  .after_cmp:
	lea	rsi, [.lit_cmp]
	mov	rdx, .after_cmp - .lit_cmp
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [bv]
	mov	edx, [auxv]
	and	edx, 0x200
	call	__bfa_emit_operand_text
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_ty_is_signed
	mov	[issigned], eax

	mov	rdi, [ctx]
	jmp	.after_tab
  .lit_tab db "	"
  .after_tab:
	lea	rsi, [.lit_tab]
	mov	rdx, .after_tab - .lit_tab
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [pred]
	mov	edx, [issigned]
	call	__bfa_emit_setcc
	jmp	.after_al
  .lit_al db "	al", 10
  .after_al:
	mov	rdi, [ctx]
	lea	rsi, [.lit_al]
	mov	rdx, .after_al - .lit_al
	call	__bfa_out_lit

	jmp	.after_movzx
  .lit_movzx db "	movzx	rax, al", 10
  .after_movzx:
	mov	rdi, [ctx]
	lea	rsi, [.lit_movzx]
	mov	rdx, .after_movzx - .lit_movzx
	call	__bfa_out_lit

	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_param(ctx, func, instid) -- moves the i'th SysV argument
; register into this value's own slot. Up to 6 (this file's own Tier 1
; scope, matching macros/proc.inc's own limit and every other convention
; in this tree -- see the report).
proc __bfa_emit_param, ctx, func, instid
	locals
		slot iv, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[iv], ecx

	cmp	dword [iv], 0
	jne	.not0
	jmp	.after0
  .lit0 db "	mov	qword "
  .after0:
	lea	rsi, [.lit0]
	mov	rdx, .after0 - .lit0
	mov	rdi, [ctx]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot
	jmp	.after0b
  .lit0b db ", rdi", 10
  .after0b:
	mov	rdi, [ctx]
	lea	rsi, [.lit0b]
	mov	rdx, .after0b - .lit0b
	call	__bfa_out_lit
	return
  .not0:
	cmp	dword [iv], 1
	jne	.not1
	jmp	.after1
  .lit1 db "	mov	qword "
  .after1:
	lea	rsi, [.lit1]
	mov	rdx, .after1 - .lit1
	mov	rdi, [ctx]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot
	jmp	.after1b
  .lit1b db ", rsi", 10
  .after1b:
	mov	rdi, [ctx]
	lea	rsi, [.lit1b]
	mov	rdx, .after1b - .lit1b
	call	__bfa_out_lit
	return
  .not1:
	cmp	dword [iv], 2
	jne	.not2
	jmp	.after2
  .lit2 db "	mov	qword "
  .after2:
	lea	rsi, [.lit2]
	mov	rdx, .after2 - .lit2
	mov	rdi, [ctx]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot
	jmp	.after2b
  .lit2b db ", rdx", 10
  .after2b:
	mov	rdi, [ctx]
	lea	rsi, [.lit2b]
	mov	rdx, .after2b - .lit2b
	call	__bfa_out_lit
	return
  .not2:
	cmp	dword [iv], 3
	jne	.not3
	jmp	.after3
  .lit3 db "	mov	qword "
  .after3:
	lea	rsi, [.lit3]
	mov	rdx, .after3 - .lit3
	mov	rdi, [ctx]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot
	jmp	.after3b
  .lit3b db ", rcx", 10
  .after3b:
	mov	rdi, [ctx]
	lea	rsi, [.lit3b]
	mov	rdx, .after3b - .lit3b
	call	__bfa_out_lit
	return
  .not3:
	cmp	dword [iv], 4
	jne	.not4
	jmp	.after4
  .lit4 db "	mov	qword "
  .after4:
	lea	rsi, [.lit4]
	mov	rdx, .after4 - .lit4
	mov	rdi, [ctx]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot
	jmp	.after4b
  .lit4b db ", r8", 10
  .after4b:
	mov	rdi, [ctx]
	lea	rsi, [.lit4b]
	mov	rdx, .after4b - .lit4b
	call	__bfa_out_lit
	return
  .not4:
	cmp	dword [iv], 5
	jne	.not5
	jmp	.after5
  .lit5 db "	mov	qword "
  .after5:
	lea	rsi, [.lit5]
	mov	rdx, .after5 - .lit5
	mov	rdi, [ctx]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot
	jmp	.after5b
  .lit5b db ", r9", 10
  .after5b:
	mov	rdi, [ctx]
	lea	rsi, [.lit5b]
	mov	rdx, .after5b - .lit5b
	call	__bfa_out_lit
	return
  .not5:
	jmp	.after_msg
  .msg db "param index > 5 (Tier 1's 6-argument limit)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
endp

; __bfa_emit_iconst(ctx, module, func, instid) -- 64-bit magnitude only.
proc __bfa_emit_iconst, ctx, module, func, instid
	locals
		slot av, dd
		slot lo, dd
		slot hi, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx

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

	mov	rdi, [ctx]
	jmp	.after_lit
  .lit db "	mov	qword "
  .after_lit:
	lea	rsi, [.lit]
	mov	rdx, .after_lit - .lit
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot
	mov	rdi, [ctx]
	jmp	.after_comma
  .lit_comma db ", "
  .after_comma:
	lea	rsi, [.lit_comma]
	mov	rdx, .after_comma - .lit_comma
	call	__bfa_out_lit

	mov	eax, [hi]
	shl	rax, 32
	mov	ecx, [lo]
	or	rax, rcx
	mov	rdi, [ctx]
	mov	rsi, rax
	call	__bfa_out_i64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	return
endp

; __bfa_emit_jmp(ctx, func, instid, lblptr, lbllen) -- `lblptr`/`lbllen` is
; this function's own emitted block-label PREFIX (see
; __bfa_emit_function): the label for internal block id K is
; `<prefix><K>`, always -- built once per function, reused by every
; jmp/br it contains.
proc __bfa_emit_jmp, ctx, func, instid, lblptr, lbllen
	locals
		slot btgt, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[btgt], ecx

	mov	rdi, [ctx]
	jmp	.after_lit
  .lit db "	jmp	"
  .after_lit:
	lea	rsi, [.lit]
	mov	rdx, .after_lit - .lit
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [btgt]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	return
endp

; __bfa_emit_br(ctx, func, instid, lblptr, lbllen)
proc __bfa_emit_br, ctx, func, instid, lblptr, lbllen
	locals
		slot cv, dd
		slot btgt, dd
		slot bf, dd
		slot eidx, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx

	mov	rdi, [func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[cv], eax
	mov	rdi, [func]
	mov	eax, [eidx]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[btgt], eax
	mov	rdi, [func]
	mov	eax, [eidx]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[bf], eax

	mov	rdi, [ctx]
	mov	esi, [cv]
	xor	edx, edx
	call	__bfa_emit_load_rax
	jmp	.after_test
  .lit_test db "	test	rax, rax", 10, "	jz	"
  .after_test:
	mov	rdi, [ctx]
	lea	rsi, [.lit_test]
	mov	rdx, .after_test - .lit_test
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [bf]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	jmp	.after_jmp
  .lit_jmp db "	jmp	"
  .after_jmp:
	mov	rdi, [ctx]
	lea	rsi, [.lit_jmp]
	mov	rdx, .after_jmp - .lit_jmp
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [btgt]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	return
endp

; __bfa_emit_ret(ctx, func, instid) -- the ONLY epilogue this file emits;
; there is exactly one per `ret`, inline, rather than a shared tail -- the
; naive choice (spec section 9.2: "do not write an optimizing backend").
proc __bfa_emit_ret, ctx, func, instid
	locals
		slot av, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx

	cmp	dword [av], 0
	je	.novalue
	mov	rdi, [ctx]
	mov	esi, [av]
	xor	edx, edx
	call	__bfa_emit_load_rax
  .novalue:
	jmp	.after_epi
  .lit_epi db "	mov	rsp, rbp", 10, "	pop	rbp", 10, "	ret", 10
  .after_epi:
	mov	rdi, [ctx]
	lea	rsi, [.lit_epi]
	mov	rdx, .after_epi - .lit_epi
	call	__bfa_out_lit
	return
endp

; ============================================================================
; per-function label prefix, instruction dispatch, function/module walking
; ============================================================================

; __bfa_make_label_prefix(ctx, module, funcptr, arena) -> rax = ptr,
; rdx = len -- "bfausr_<name>_b", built once per function and reused for
; every block header and every jmp/br target inside it (block ids restart
; at 1 in every function's own `blocks` Vec, so the name must be part of
; the label or two functions' `b1` would collide). Built with a throwaway
; BfaPrintCtx over its own Vec, reusing `__bfa_out_lit` rather than a
; second string-building primitive.
proc __bfa_make_label_prefix, ctx, module, funcptr, arena
	locals
		slot tctx, dq
		slot nameid, dd
	endl
	mov	rdi, [arena]
	mov	rsi, sizeof.BfaPrintCtx
	call	arena_alloc
	mov	[tctx], rax
	mov	rcx, [module]
	mov	[rax + BfaPrintCtx.module], rcx

	mov	rdi, [arena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	rcx, rax
	mov	rdi, [tctx]
	mov	[rdi + BfaPrintCtx.obuf], rcx
	mov	rdi, rcx
	mov	rsi, [arena]
	mov	rdx, 1
	mov	rcx, 32
	call	vec_init

	mov	rdi, [tctx]
	jmp	.after_pfx
  .lit_pfx db "bfausr_"
  .after_pfx:
	lea	rsi, [.lit_pfx]
	mov	rdx, .after_pfx - .lit_pfx
	call	__bfa_out_lit

	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.name]
	mov	[nameid], ecx
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [nameid]
	call	intern_bytes
	mov	rdi, [tctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit

	mov	rdi, [tctx]
	jmp	.after_b
  .lit_b db "_b"
  .after_b:
	lea	rsi, [.lit_b]
	mov	rdx, .after_b - .lit_b
	call	__bfa_out_lit

	mov	rdi, [tctx]
	mov	rdi, [rdi + BfaPrintCtx.obuf]
	mov	rax, [rdi + Vec.data]
	mov	rdx, [rdi + Vec.len]
	return
endp

; ============================================================================
; Tier 2 -- what the hello world needs, and nothing speculative
; (docs/design/runtime.md sections 5 and 6's `backend_fasmg` row)
; ============================================================================
;
; FRAME LAYOUT, EXTENDED. Tier 1's frame was one 8-byte slot per value id.
; `slot n align` needs storage the emitted code can take the ADDRESS of, so
; the frame now has two regions:
;
;       [rbp - 8]              value slot 1        }  the Tier-1 region,
;       ...                                        }  unchanged: 8 bytes per
;       [rbp - 8*ninsts]       value slot ninsts   }  instruction id
;       ---- align16(8*(ninsts+1)) ----------------
;       ...                    each `slot` instruction's own bytes,
;                              in instruction-id order, each aligned
;       ---- align16(total) = the frame size ------
;
; `rbp` is 16-byte aligned inside every emitted function (`rsp` is 8 mod 16
; at entry, `push rbp` makes it 0 mod 16), so an offset that is a multiple
; of A gives an address aligned to A for every A up to 16. Alignments above
; 16 would need a runtime `and rsp, -A` and are REFUSED rather than
; silently mis-aligned.
;
; The offset of a given `slot` is recomputed by walking the instruction Vec
; from the start each time it is needed -- O(n^2) over a function, which is
; this backend's standing trade (spec section 9.2: "slow by construction and
; correct by construction") and avoids threading a per-function side table
; through a dispatcher that is already at `proc`'s six-argument limit.

; __bfa_align_up(v, a) -> rax = v rounded up to a multiple of `a`, which
; must be a power of two (checked by the caller, __bfa_emit_slot).
proc __bfa_align_up, v, a
	locals
	endl
	mov	rax, [v]
	mov	rcx, [a]
	add	rax, rcx
	dec	rax
	neg	rcx
	and	rax, rcx
	return
endp

; __bfa_frame_layout(func, instid) -> eax.
;   instid == 0: the whole frame size, 16-byte aligned.
;   instid != 0: the byte offset from `rbp` of that `slot` instruction's
;                storage, i.e. the emitted code says `lea rax, [rbp-eax]`.
; A non-`slot` instid, or one this walk never reaches, is a caller bug and
; returns the frame size -- callers only ever pass a `slot`'s own id.
proc __bfa_frame_layout, func, instid
	uses	rbx
	locals
		slot ninsts, dd
		slot v, dd
		slot cur, dq
		slot algn, dq
	endl
	mov	rbx, [func]
	mov	rdi, [rbx + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	mov	[ninsts], eax

	; the Tier-1 value-slot region, unchanged: (ninsts+1) slots, 16-aligned
	mov	eax, [ninsts]
	inc	eax
	shl	eax, 3
	add	eax, 15
	and	eax, -16
	mov	[cur], rax

	mov	dword [v], 1
  .loop:
	mov	eax, [v]
	cmp	eax, [ninsts]
	jg	.done
	mov	rdi, [func]
	mov	esi, [v]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_SLOT
	jne	.next
	mov	ecx, [rax + BfaInst.a]		; size
	mov	edx, [rax + BfaInst.b]		; align
	test	edx, edx
	jnz	.havealign
	mov	edx, 1
  .havealign:
	mov	[algn], rdx
	mov	rdi, [cur]
	add	rdi, rcx
	mov	rsi, [algn]
	call	__bfa_align_up
	mov	[cur], rax
	mov	eax, [v]
	cmp	eax, [instid]
	jne	.next
	mov	rax, [cur]
	return
  .next:
	mov	eax, [v]
	inc	eax
	mov	[v], eax
	jmp	.loop
  .done:
	mov	rax, [cur]
	add	rax, 15
	and	rax, -16
	return
endp

; __bfa_emit_slot_inst(ctx, module, func, instid) -- `%v = slot n align`.
proc __bfa_emit_slot_inst, ctx, module, func, instid
	locals
		slot algn, dd
		slot off, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.b]
	mov	[algn], ecx

	; alignment must be a power of two and at most 16 -- see the note on
	; frame layout above for why 16 is the ceiling.
	mov	eax, [algn]
	test	eax, eax
	jz	.badalign
	cmp	eax, 16
	jg	.badalign
	mov	ecx, eax
	dec	ecx
	and	ecx, eax
	jnz	.badalign

	mov	rdi, [func]
	mov	esi, [instid]
	call	__bfa_frame_layout
	mov	[off], eax

	mov	rdi, [ctx]
	jmp	.after_lea
  .lit_lea db "	lea	rax, [rbp-"
  .after_lea:
	lea	rsi, [.lit_lea]
	mov	rdx, .after_lea - .lit_lea
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	jmp	.after_close
  .lit_close db "]", 10
  .after_close:
	lea	rsi, [.lit_close]
	mov	rdx, .after_close - .lit_close
	call	__bfa_out_lit

	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
  .badalign:
	jmp	.after_msg
  .msg db "slot: alignment must be a power of two, 1..16"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
endp

; __bfa_emit_gaddr(ctx, func, instid) -- `%v = gaddr N` -> the address of
; the data segment label `bfausr_gN`. The label name is this file's half of
; a two-sided convention: `bfa_emit_program` (program.inc) emits the
; matching `bfausr_g<id>:` in `segment readable`, in global id order.
proc __bfa_emit_gaddr, ctx, func, instid
	locals
		slot gn, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[gn], ecx

	mov	rdi, [ctx]
	jmp	.after_lea
  .lit_lea db "	lea	rax, [bfausr_g"
  .after_lea:
	lea	rsi, [.lit_lea]
	mov	rdx, .after_lea - .lit_lea
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [gn]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	jmp	.after_close
  .lit_close db "]", 10
  .after_close:
	lea	rsi, [.lit_close]
	mov	rdx, .after_close - .lit_close
	call	__bfa_out_lit

	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_check_nativus(func, eidx) -- dies unless the byte order at extra
; slot `eidx` is `nativus`. `maior`/`minor` need a `bswap` and a width the
; hello world does not use; refusing by name beats emitting a native-order
; access for a declaration that asked for big-endian.
proc __bfa_check_nativus, func, eidx
	locals
	endl
	mov	rdi, [func]
	mov	esi, [eidx]
	call	bfa_extra_get
	cmp	eax, BFA_ORDER_NATIVUS
	je	.ok
	jmp	.after_msg
  .msg db "load/store: only `nativus` byte order is implemented"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .ok:
	return
endp

; __bfa_emit_load(ctx, module, func, instid) -- `%v = load T %p off order`,
; T at 64 bits (`ptr` or `u64`/`i64`), order `nativus`.
proc __bfa_emit_load, ctx, module, func, instid
	locals
		slot eidx, dd
		slot vp, dd
		slot off, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.ty]
	push	rcx
	mov	rdi, [module]
	pop	rsi
	call	__bfa_check_width64

	mov	rdi, [func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[vp], eax
	mov	rdi, [func]
	mov	eax, [eidx]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[off], eax
	mov	rdi, [func]
	mov	eax, [eidx]
	add	eax, 2
	mov	esi, eax
	call	__bfa_check_nativus

	mov	rdi, [ctx]
	mov	esi, [vp]
	call	__bfa_out_movq_rax_slot		; mov rax, [rbp-8p]

	mov	rdi, [ctx]
	jmp	.after_deref
  .lit_deref db "	mov	rax, qword [rax+"
  .after_deref:
	lea	rsi, [.lit_deref]
	mov	rdx, .after_deref - .lit_deref
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	jmp	.after_close
  .lit_close db "]", 10
  .after_close:
	lea	rsi, [.lit_close]
	mov	rdx, .after_close - .lit_close
	call	__bfa_out_lit

	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_store(ctx, module, func, instid) -- `store T %p off order %v`.
proc __bfa_emit_store, ctx, module, func, instid
	locals
		slot eidx, dd
		slot vp, dd
		slot off, dd
		slot vv, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.ty]
	push	rcx
	mov	rdi, [module]
	pop	rsi
	call	__bfa_check_width64

	mov	rdi, [func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[vp], eax
	mov	rdi, [func]
	mov	eax, [eidx]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[off], eax
	mov	rdi, [func]
	mov	eax, [eidx]
	add	eax, 2
	mov	esi, eax
	call	__bfa_check_nativus
	mov	rdi, [func]
	mov	eax, [eidx]
	add	eax, 3
	mov	esi, eax
	call	bfa_extra_get
	mov	[vv], eax

	mov	rdi, [ctx]
	mov	esi, [vp]
	call	__bfa_out_movq_rax_slot		; mov rax, [rbp-8p]

	mov	rdi, [ctx]
	jmp	.after_movc
  .lit_movc db "	mov	rcx, "
  .after_movc:
	lea	rsi, [.lit_movc]
	mov	rdx, .after_movc - .lit_movc
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [vv]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	mov	rdi, [ctx]
	jmp	.after_st
  .lit_st db "	mov	qword [rax+"
  .after_st:
	lea	rsi, [.lit_st]
	mov	rdx, .after_st - .lit_st
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	jmp	.after_close
  .lit_close db "], rcx", 10
  .after_close:
	lea	rsi, [.lit_close]
	mov	rdx, .after_close - .lit_close
	call	__bfa_out_lit
	return
endp

; __bfa_emit_argreg(ctx, i) -- the SysV integer argument register name for
; argument index `i` (0..5). Six is the limit: a seventh argument is
; docs/design/runtime.md section 8's own `[OPEN]` ("the seventh SysV
; argument in emitted `call`s") and is REFUSED here.
;
; The brief for this tier asked for `rassert` on the seventh. It is
; `__bfa_emit_die` instead, deliberately: `rassert`'s condition is elided
; whole under `RELEASE` (macros/assert.inc's header), so a `RELEASE` build
; of `exsc` would emit a `call` that silently dropped the seventh argument
; -- the exact "never silently miscompile" rule this file's own header
; states. The refusal is unconditional; the reason is recorded here and in
; this pass's report rather than quietly substituted.
proc __bfa_emit_argreg, ctx, i
	locals
	endl
	jmp	.after_tab
  .tab db "rdi", 0, "rsi", 0, "rdx", 0, "rcx", 0, "r8 ", 0, "r9 ", 0
  .after_tab:
	mov	rax, [i]
	cmp	rax, 6
	jge	.toomany
	shl	rax, 2
	lea	rsi, [.tab]
	add	rsi, rax
	mov	rdx, 3
	cmp	byte [rsi + 2], ' '		; r8 / r9 are two characters
	jne	.len3
	mov	rdx, 2
  .len3:
	mov	rdi, [ctx]
	call	__bfa_out_lit
	return
  .toomany:
	jmp	.after_msg
  .msg db "call: a seventh argument is [OPEN] (runtime.md section 8)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
endp

; __bfa_emit_callee_label(ctx, module, idv) -- prints `bfausr_<name>` for
; the 1-based module function index `idv`. The `bfausr_` prefix is the name
; ABI docs/design/runtime.md section 2.2 and prelude/README.md both build
; on: the prelude labels its IR-callable routines `bfausr_exsrt_<x>` for
; exactly this reason, so `call @exsrt_scriptor_scribe` resolves with no
; special case here.
proc __bfa_emit_callee_label, ctx, module, idv
	locals
	endl
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, [idv]
	dec	esi
	call	vec_get
	mov	rax, [rax]
	mov	ecx, [rax + BfaFunc.name]
	push	rcx
	mov	rdi, [ctx]
	jmp	.after_pfx
  .lit_pfx db "bfausr_"
  .after_pfx:
	lea	rsi, [.lit_pfx]
	mov	rdx, .after_pfx - .lit_pfx
	call	__bfa_out_lit
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.names]
	pop	rsi
	call	intern_bytes
	push	rax
	push	rdx
	mov	rdi, [ctx]
	pop	rdx
	pop	rsi
	call	__bfa_out_lit
	return
endp

; __bfa_emit_call(ctx, module, func, instid) -- `[%v =] call T @f args...`.
; extra[base] is the 1-based callee index, extra[base+1..] the arguments.
; Every argument is loaded from its own stack slot into its SysV register
; immediately before the call, in ascending argument order, and the result
; (if the call is not `void`) is stored straight back into the result's
; slot: no register allocation, exactly the Tier-1 contract.
;
; STACK ALIGNMENT: the prologue's `sub rsp, framesz` uses a 16-byte
; multiple and nothing between the prologue and here moves `rsp`, so
; `rsp` is 0 mod 16 at the `call` -- what SysV requires, and what the
; prelude's routines assume (prelude/README.md's `exsrt_` ABI).
proc __bfa_emit_call, ctx, module, func, instid
	uses	rbx
	locals
		slot eidx, dd
		slot cnt, dd
		slot tyv, dd
		slot i, dd
		slot av, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[cnt], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx

	mov	dword [i], 0
  .argloop:
	mov	eax, [i]
	inc	eax
	cmp	eax, [cnt]
	jge	.argsdone
	mov	rdi, [func]
	mov	eax, [eidx]
	add	eax, [i]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[av], eax

	mov	rdi, [ctx]
	jmp	.after_mov
  .lit_mov db "	mov	"
  .after_mov:
	lea	rsi, [.lit_mov]
	mov	rdx, .after_mov - .lit_mov
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [i]
	call	__bfa_emit_argreg
	mov	rdi, [ctx]
	jmp	.after_comma
  .lit_comma db ", "
  .after_comma:
	lea	rsi, [.lit_comma]
	mov	rdx, .after_comma - .lit_comma
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.argloop
  .argsdone:

	mov	rdi, [ctx]
	jmp	.after_call
  .lit_call db "	call	"
  .after_call:
	lea	rsi, [.lit_call]
	mov	rdx, .after_call - .lit_call
	call	__bfa_out_lit
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	push	rcx
	mov	rdi, [func]
	pop	rsi
	call	bfa_extra_get			; eax = 1-based callee index
	push	rax
	mov	rdi, [ctx]
	mov	rsi, [module]
	pop	rdx
	call	__bfa_emit_callee_label
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	cmp	dword [tyv], 0			; `void` -- no result slot
	je	.novalue
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
  .novalue:
	return
endp

; __bfa_emit_arc(ctx, module, func, instid) -- `retain %r` / `release %r`.
; The prelude routine is chosen by the OPERAND'S OWN TYPE, `ref` (non-
; atomic) versus `refc` (atomic) -- docs/design/ssa-ir.md section 2.8's
; "atomicity from `ref`/`refc`", and runtime.md 2.5's request to this file
; verbatim. `exsrt_retain` and friends carry NO `bfausr_` prefix: they are
; not IR names, they are labels this emitter hardcodes (prelude/README.md,
; "Two prefixes, and the difference is the ABI").
proc __bfa_emit_arc, ctx, module, func, instid
	locals
		slot rv, dd
		slot isrel, dd
		slot isatomic, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[rv], ecx
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_RELEASE
	sete	cl
	movzx	ecx, cl
	mov	[isrel], ecx

	mov	rdi, [func]
	mov	esi, [rv]
	call	bfa_value_type
	cmp	eax, BFA_VALUE_TYPE_IS_U1
	je	.badty
	push	rax
	mov	rdi, [module]
	pop	rsi
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_REF
	je	.nonatomic
	cmp	ecx, BFA_TK_REFC
	je	.atomic
  .badty:
	jmp	.after_msg
  .msg db "retain/release: operand is not `ref` or `refc`"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .nonatomic:
	mov	dword [isatomic], 0
	jmp	.emit
  .atomic:
	mov	dword [isatomic], 1
  .emit:
	mov	rdi, [ctx]
	jmp	.after_movrdi
  .lit_movrdi db "	mov	rdi, "
  .after_movrdi:
	lea	rsi, [.lit_movrdi]
	mov	rdx, .after_movrdi - .lit_movrdi
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [rv]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	mov	rdi, [ctx]
	jmp	.after_c
  .lit_c db "	call	exsrt_"
  .after_c:
	lea	rsi, [.lit_c]
	mov	rdx, .after_c - .lit_c
	call	__bfa_out_lit
	cmp	dword [isrel], 0
	jne	.wrel
	mov	rdi, [ctx]
	jmp	.after_ret
  .lit_ret db "retain"
  .after_ret:
	lea	rsi, [.lit_ret]
	mov	rdx, .after_ret - .lit_ret
	call	__bfa_out_lit
	jmp	.suffix
  .wrel:
	mov	rdi, [ctx]
	jmp	.after_rel
  .lit_rel db "release"
  .after_rel:
	lea	rsi, [.lit_rel]
	mov	rdx, .after_rel - .lit_rel
	call	__bfa_out_lit
  .suffix:
	cmp	dword [isatomic], 0
	je	.nosuffix
	mov	rdi, [ctx]
	jmp	.after_c2
  .lit_c2 db "_c"
  .after_c2:
	lea	rsi, [.lit_c2]
	mov	rdx, .after_c2 - .lit_c2
	call	__bfa_out_lit
  .nosuffix:
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	return
endp

; __bfa_emit_trap(ctx, module, func, instid) -- `trap KIND`.
; `Inst.aux` is the interned kind WORD (parse.inc interns whatever text
; appears; docs/design/ssa-ir.md section 2.3 says only that trap kinds "are
; an IR enum" and never names one). The kinds that exist are
; prelude/README.md's, and the only one any design spells today is
; `terminus` -- kind 5, added at docs/design/lowering.md finding 2's
; request precisely so a `dum ... terminus N` overrun stops folding into
; kind 1. Any other word is refused by name rather than guessed at.
proc __bfa_emit_trap, ctx, module, func, instid
	locals
		slot kidv, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[kidv], ecx

	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [kidv]
	call	intern_bytes
	mov	rdi, rax
	mov	rsi, rdx
	jmp	.after_term
  .lit_term db "terminus"
  .after_term:
	lea	rdx, [.lit_term]
	mov	rcx, .after_term - .lit_term
	call	__bfa_streq
	test	eax, eax
	jz	.unknown

	mov	rdi, [ctx]
	jmp	.after_k5
  .lit_k5 db "	mov	edi, 5", 10, "	jmp	exsrt_abort", 10
  .after_k5:
	lea	rsi, [.lit_k5]
	mov	rdx, .after_k5 - .lit_k5
	call	__bfa_out_lit
	return
  .unknown:
	jmp	.after_msg
  .msg db "trap: unknown kind word (only `terminus` has a prelude kind)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
endp

; __bfa_emit_inst(ctx, module, func, instid) -- dispatches one instruction
; to its Tier-1 lowering, or dies naming the opcode. `jmp`/`br` need this
; function's label prefix too; threaded through two file-scope `=` slots
; set once per function by `__bfa_emit_function` rather than as extra
; parameters (`proc` tops out at six, and this dispatcher already needs
; four).
proc __bfa_emit_inst, ctx, module, func, instid, lblptr, lbllen
	locals
		slot op, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx

	cmp	dword [op], BFA_OP_PARAM
	jne	.notparam
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfa_emit_param
	return
  .notparam:
	cmp	dword [op], BFA_OP_ICONST
	jne	.noticonst
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_iconst
	return
  .noticonst:
	cmp	dword [op], BFA_OP_ADD
	je	.dobin
	cmp	dword [op], BFA_OP_SUB
	je	.dobin
	cmp	dword [op], BFA_OP_ADDW
	je	.dobin
	cmp	dword [op], BFA_OP_SUBW
	je	.dobin
	jmp	.notbin
  .dobin:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_addsub
	return
  .notbin:
	cmp	dword [op], BFA_OP_MUL
	je	.domul
	cmp	dword [op], BFA_OP_MULW
	je	.domul
	jmp	.notmul
  .domul:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_mul
	return
  .notmul:
	cmp	dword [op], BFA_OP_CMP
	je	.docmp
	cmp	dword [op], BFA_OP_FCMP
	je	.docmp
	jmp	.notcmp
  .docmp:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_cmp
	return
  .notcmp:
	cmp	dword [op], BFA_OP_JMP
	jne	.notjmp
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	mov	rcx, [lblptr]
	mov	r8, [lbllen]
	call	__bfa_emit_jmp
	return
  .notjmp:
	cmp	dword [op], BFA_OP_BR
	jne	.notbr
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	mov	rcx, [lblptr]
	mov	r8, [lbllen]
	call	__bfa_emit_br
	return
  .notbr:
	cmp	dword [op], BFA_OP_RET
	jne	.notret
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfa_emit_ret
	return
  .notret:
	cmp	dword [op], BFA_OP_SLOT
	jne	.notslot
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_slot_inst
	return
  .notslot:
	cmp	dword [op], BFA_OP_GADDR
	jne	.notgaddr
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfa_emit_gaddr
	return
  .notgaddr:
	cmp	dword [op], BFA_OP_LOAD
	jne	.notload
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_load
	return
  .notload:
	cmp	dword [op], BFA_OP_STORE
	jne	.notstore
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_store
	return
  .notstore:
	cmp	dword [op], BFA_OP_CALL
	jne	.notcall
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_call
	return
  .notcall:
	cmp	dword [op], BFA_OP_RETAIN
	je	.doarc
	cmp	dword [op], BFA_OP_RELEASE
	je	.doarc
	jmp	.notarc
  .doarc:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_arc
	return
  .notarc:
	cmp	dword [op], BFA_OP_TRAP
	jne	.nottrap
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_trap
	return
  .nottrap:
	jmp	.after_msg
  .msg db "opcode not implemented in the Tier-1/Tier-2 emitter"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
endp

; __bfa_emit_function(ctx, module, funcptr, arena)
proc __bfa_emit_function, ctx, module, funcptr, arena
	locals
		slot func, dq
		slot lblptr, dq
		slot lbllen, dq
		slot ninsts, dd
		slot nblocks, dd
		slot framesz, dd
		slot bi, dd
		slot blkid, dd
		slot cur, dd
	endl
	mov	rax, [funcptr]
	mov	[func], rax

	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	rcx, [arena]
	call	__bfa_make_label_prefix
	mov	[lblptr], rax
	mov	[lbllen], rdx

	; function entry label: same prefix minus the trailing "_b", plus ':'
	mov	rax, [funcptr]
	mov	ecx, [rax + BfaFunc.name]
	push	rcx
	mov	rdi, [ctx]
	jmp	.after_lbl
  .lit_lbl db "bfausr_"
  .after_lbl:
	lea	rsi, [.lit_lbl]
	mov	rdx, .after_lbl - .lit_lbl
	call	__bfa_out_lit
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.names]
	pop	rsi
	call	intern_bytes
	mov	rdi, [ctx]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_out_lit
	jmp	.after_colon
  .lit_colon db ":", 10
  .after_colon:
	mov	rdi, [ctx]
	lea	rsi, [.lit_colon]
	mov	rdx, .after_colon - .lit_colon
	call	__bfa_out_lit

	; prologue: one 8-byte slot per instruction id (dense, void ids included
	; for simplicity), then one aligned block per `slot` instruction, the
	; whole frame 16-byte aligned -- __bfa_frame_layout owns the layout and
	; is the single place it is computed (see its header).
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	mov	[ninsts], eax
	mov	rdi, [func]
	xor	esi, esi
	call	__bfa_frame_layout
	mov	[framesz], eax

	jmp	.after_prologue
  .lit_prologue db "	push	rbp", 10, "	mov	rbp, rsp", 10, "	sub	rsp, "
  .after_prologue:
	mov	rdi, [ctx]
	lea	rsi, [.lit_prologue]
	mov	rdx, .after_prologue - .lit_prologue
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [framesz]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	; blocks, in creation order -- see this file's header
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.blocks]
	mov	rax, [rdi + Vec.len]
	mov	[nblocks], eax
	mov	dword [bi], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [nblocks]
	jge	.blocksdone
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [blkid]
	call	__bfa_out_u64
	jmp	.after_colon2
  .lit_colon2 db ":", 10
  .after_colon2:
	mov	rdi, [ctx]
	lea	rsi, [.lit_colon2]
	mov	rdx, .after_colon2 - .lit_colon2
	call	__bfa_out_lit

	mov	rdi, [func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_phi]
	cmp	ecx, 0
	je	.nophi
	jmp	.after_phimsg
  .phimsg db "phi is not implemented in the Tier-1 emitter (needs parallel-copy lowering)"
  .after_phimsg:
	lea	rdi, [.phimsg]
	mov	rsi, .after_phimsg - .phimsg
	call	__bfa_emit_die
  .nophi:

	mov	rdi, [func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first]
	mov	[cur], ecx
  .bodyloop:
	cmp	dword [cur], 0
	je	.bodydone
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [cur]
	mov	r8, [lblptr]
	mov	r9, [lbllen]
	call	__bfa_emit_inst

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
  .blocksdone:
	return
endp

; bfa_emit_module(module, arena) -> rax = ptr, rdx = len -- the whole
; module's Tier-1 fasmg source, plus one shared `bfausr_trap:` stub. Meant
; to be `include`d inside a hand-written harness's own
; `segment readable executable` block (this file's own header explains
; why); the harness supplies `format`/`entry`/segments and calls whichever
; `bfausr_<name>` it wants to test with concrete arguments, exactly the way
; every existing `tests/unit/*.asm` fixture already calls into an
; `include`d rt/ proc directly.
proc bfa_emit_module, module, arena
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
	mov	rcx, 256
	call	vec_init

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
	mov	rax, [rax]
	; An `externus` function is a DECLARATION: spec section 5.3 says its
	; body lives in another object, so emitting one here would define a
	; label something else already defines. This is how the hello world
	; names the prelude's routines -- `call @exsrt_scriptor_scribe`
	; resolves against a `functio ... externus sysv_amd64` declaration and
	; assembles against `bfausr_exsrt_scriptor_scribe` in the blob.
	;
	; docs/design/ssa-ir.md section 2.11 gives no BODILESS textual form
	; (docs/design/lowering.md finding 16), so such a declaration must
	; still carry a body in text today; it is skipped here and never
	; reaches OUT. That finding is now hit in practice, not hypothetically.
	test	dword [rax + BfaFunc.attrs], BFA_FUNC_ATTR_EXTERNUS
	jnz	.skipfunc
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, rax
	mov	rcx, [arena]
	call	__bfa_emit_function
  .skipfunc:
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.funcloop
  .funcsdone:

	jmp	.after_trap
  ; docs/design/runtime.md 2.5, verbatim: "The emitter's `bfausr_trap:`
  ; (`ud2` alone) should become `mov edi, 1 / jmp exsrt_abort` so that a
  ; numeric trap says which kind it was." Kind 1 is prelude/README.md's
  ; "numeric trap: trapping + - * /, zero divisor, failed chk". This makes
  ; the emitted module depend on the prelude for the first time: a harness
  ; that assembles `bfa_emit_module`'s text alone must now supply an
  ; `exsrt_abort`, which `bfa_emit_program`'s OUT always does.
  .lit_trap db "bfausr_trap:", 10, "	mov	edi, 1", 10, "	jmp	exsrt_abort", 10
  .after_trap:
	mov	rdi, [ctx]
	lea	rsi, [.lit_trap]
	mov	rdx, .after_trap - .lit_trap
	call	__bfa_out_lit

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.obuf]
	mov	rax, [rdi + Vec.data]
	mov	rdx, [rdi + Vec.len]
	return
endp
