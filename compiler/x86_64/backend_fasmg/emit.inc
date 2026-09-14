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
; WHAT IS LOWERED, reported honestly rather than half-covering everything:
; `param`, `iconst` (any 64-bit constant; `mov rax, imm64` past a simm32),
; `add`/`sub`/`mul` (trapping -- at 64 bits signed overflow via OF,
; unsigned via CF, exactly as x86 ADD/SUB/IMUL/MUL already distinguish;
; below 64 through the canonical form), `addw`/`subw`/`mulw` (wrapping),
; `adds`/`subs` (saturating), `and`/`or`/`xor`, `shl`/`shr` (a count >= N
; traps), `cmp.*` (signed vs unsigned SETcc chosen from the type's kind),
; `zext`/`sext`/`trunc`, every integer width from 1 to 64 (see "The
; canonical form of a narrow integer", below -- wire-codec.md milestone
; M3), `jmp`, `br`, `ret`, `phi` as a parallel copy on each incoming edge
; ("phi -- parallel copies on edges"), and Tier 2's memory, call, ARC and
; `trap` ops, plus `index`/`chk`/`copy`/`addr`, loads and stores at every
; whole-byte width in every byte order (`maior`/`minor` byte by byte), and
; `loadbits`/`storebits` within one byte ("Byte order and bit fields",
; below -- wire-codec.md milestone M4), and the FLOAT opcodes through the
; SSE2 baseline ("Floats -- f32/f64 through the SSE2 baseline", below):
; `fconst`, `fadd`/`fsub`/`fmul`/`fdiv`, `fneg`, the six ordered `fcmp`
; forms, `fext`/`ftrunc`, `itof`/`ftoi`, and F32/F64 parameters, arguments
; and results crossing the SysV registers by class (asm-conventions.md
; section 1.1: xmm0..xmm7 beside rdi..r9, xmm0 beside rax). Every other
; opcode -- `fma` and `bitcast` among the float ones, `div`/`rem`, `muls`,
; the `*ov` predicates, `callind`, the reduction ops -- and a bit field
; that straddles a byte, this file's own `__bfa_emit_die` refuses by name
; rather than emitting something wrong.
; ADR 0012 already prices "a naive backend still needs an instruction
; selector for every IR op" as real, unscheduled engineering; this is a
; slice of it, not all of it.
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

; __bfa_ty_info(module, ty) -> eax = BfaType.width, edx = BfaType.kind.
proc __bfa_ty_info, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	bfa_type_ptr
	movzx	edx, byte [rax + BfaType.kind]
	movzx	eax, word [rax + BfaType.width]
	return
endp

; __bfa_int_width(module, ty) -> eax = the operand width N every integer
; lowering below needs. `uN`/`iN` at any N from 1 to 64 (ssa-ir.md section
; 2.2's canonical form is what makes a narrow width a 64-bit operation plus
; a normalisation); any other 64-bit type -- `ptr`, a `ref` -- as the
; Tier-1 check this replaces (`__bfa_check_width64`) always admitted it; a
; float dies, because these are the INTEGER paths and an `add f64` reaching
; one is a type error the verifier does not cross-check -- the float
; opcodes have their own lowering ("Floats", below), and quietly running
; one through the integer sequence would miscompile by construction.
proc __bfa_int_width, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfa_ty_info
	cmp	edx, BFA_TK_U
	je	.ok
	cmp	edx, BFA_TK_I
	je	.ok
	cmp	edx, BFA_TK_F32
	je	.bad
	cmp	edx, BFA_TK_F64
	je	.bad
	cmp	eax, 64
	je	.ok
  .bad:
	jmp	.after_msg
  .msg db "unsupported type (an integer uN/iN, or a 64-bit address)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .ok:
	return
endp

; __bfa_ty_is_signed(module, ty) -> eax = 1 iff `ty` is an `iN`.
;
; Read off BfaType.KIND, not BfaType.sign, and the difference is a bug this
; replaced: parse.inc interns `iN` as {kind I, sign 1}, but the lowering
; interns it as {kind I, sign 0} (lower/ty.inc, `lwr_ty_ir`'s `.int` arm
; passes sign 0 for both kinds), so for every signed type `exsc` itself
; built this used to answer "unsigned" -- a signed `+` checked CF instead
; of OF, a signed `lt` emitted `setb`. The kind is the one field both
; producers agree on.
proc __bfa_ty_is_signed, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	xor	eax, eax
	cmp	ecx, BFA_TK_I
	sete	al
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
; The canonical form of a narrow integer (docs/design/ssa-ir.md section 2.2;
; docs/design/wire-codec.md D5, milestone M3)
; ============================================================================
;
; A `uN` with N < 64 lives in its 8-byte slot ZERO-extended, an `iN`
; SIGN-extended, a `u1` as 0 or 1. Every lowering below may assume its
; operands are in that form and must leave its result in it, which is what
; lets a narrow operation be the 64-bit one plus, where the 64-bit result
; can leave the form, one NORMALISATION:
;
;       shl     rax, 64-N
;       shr     rax, 64-N               ; `sar` for an iN
;
; Nothing is emitted at N = 64, so every 64-bit function's text is
; byte-identical to what Tier 1 emitted (bfa_emit_tier1/tier2/phi/program
; pass unchanged). Which ops normalise:
;
;   addw subw mulw            the op, then normalise
;   add sub mul (N < 64)      the op into rax, a normalised copy in rcx,
;                             `cmp rcx, rax` / `jne bfausr_trap` -- the
;                             value changed, so it was not a value of T.
;                             Complete for add/sub, and for mul up to 32
;                             bits, where the 64-bit product is exact
;   mul, 33 <= N <= 63        ALSO the 64-bit `mul`'s CF / `imul`'s OF: in
;                             u40, 2^32 * 2^32 = 2^64 leaves raw 0, which
;                             normalises to itself (ssa-ir.md 2.2)
;   add sub mul (N = 64)      the flags alone, exactly as Tier 1
;   adds subs                 a clamp, see __bfa_emit_sat
;   shl                       the count check, the shift, normalise
;   shr and or xor cmp        nothing: canonical in, canonical out
;   zext sext trunc           a copy, then normalise to the destination
;
; THE TRAP KIND. Every check here -- a narrow `+` that left its type, a
; shift count >= N, a `chk` index >= its bound -- jumps to the one shared
; `bfausr_trap`, which is kind 1: prelude/README.md's "numeric trap:
; trapping `+ - * /`, zero divisor, failed `chk`". Spec 5.4 says a count
; >= the width "traps, as `+` traps on overflow", so the shift is kind 1
; too; no new kind, and nothing in spec 13 (a trap is not a diagnostic).

; __bfa_emtxt? text& -- inside a `proc` with a `ctx` argument ONLY (the
; rule print.inc's `__bfa_opname_case?` states for itself): appends the
; literal bytes `text` to the output. The `jmp`-guarded `db` is the same
; shape every hand-written literal in this file has; this spells it once.
macro __bfa_emtxt? text&
	local s, e
	mov	rdi, [ctx]
	jmp	e
	s db	text
	e:
	lea	rsi, [s]
	mov	rdx, e - s
	call	__bfa_out_lit
end macro

; __bfa_emit_regname(ctx, reg) -- `rax` (reg 0) or `rcx` (reg 1).
proc __bfa_emit_regname, ctx, reg
	locals
	endl
	cmp	dword [reg], 0
	jne	.isrcx
	__bfa_emtxt "rax"
	return
  .isrcx:
	__bfa_emtxt "rcx"
	return
endp

; __bfa_emit_norm_bits(ctx, reg, nbits, issigned) -- the normalisation pair
; for an `nbits`-wide value in `reg` (0 rax, 1 rcx), zero- or sign-
; extending; nothing at all when nbits >= 64.
proc __bfa_emit_norm_bits, ctx, reg, nbits, issigned
	locals
		slot shamt, dd
	endl
	mov	eax, 64
	sub	eax, [nbits]
	jle	.done
	mov	[shamt], eax
	__bfa_emtxt 9, "shl", 9
	mov	rdi, [ctx]
	mov	esi, [reg]
	call	__bfa_emit_regname
	__bfa_emtxt ", "
	mov	rdi, [ctx]
	mov	esi, [shamt]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	cmp	dword [issigned], 0
	jne	.dosar
	__bfa_emtxt 9, "shr", 9
	jmp	.second
  .dosar:
	__bfa_emtxt 9, "sar", 9
  .second:
	mov	rdi, [ctx]
	mov	esi, [reg]
	call	__bfa_emit_regname
	__bfa_emtxt ", "
	mov	rdi, [ctx]
	mov	esi, [shamt]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
  .done:
	return
endp

; __bfa_emit_norm(ctx, module, ty, reg) -- normalise `reg` to type `ty`:
; nothing unless `ty` is a `uN`/`iN` with N < 64.
proc __bfa_emit_norm, ctx, module, ty, reg
	locals
		slot nbits, dd
		slot kind, dd
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfa_ty_info
	mov	[nbits], eax
	mov	[kind], edx
	cmp	edx, BFA_TK_U
	je	.isint
	cmp	edx, BFA_TK_I
	jne	.done
  .isint:
	xor	ecx, ecx
	cmp	dword [kind], BFA_TK_I
	sete	cl
	mov	rdi, [ctx]
	mov	esi, [reg]
	mov	edx, [nbits]
	call	__bfa_emit_norm_bits
  .done:
	return
endp

; __bfa_emit_normcheck(ctx, module, ty) -- the trapping form's check: the
; raw 64-bit result is in rax; trap unless normalising it changes nothing.
proc __bfa_emit_normcheck, ctx, module, ty
	locals
	endl
	__bfa_emtxt 9, "mov", 9, "rcx, rax", 10
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [ty]
	mov	ecx, 1
	call	__bfa_emit_norm
	__bfa_emtxt 9, "cmp", 9, "rcx, rax", 10, 9, "jne", 9, "bfausr_trap", 10
	return
endp

; __bfa_check_opimm(module, ty, raw, isimm) -- dies unless an inline
; immediate operand (isimm != 0) is a value of `ty` in canonical form. The
; immediate is emitted as parse.inc stored it, a sign-extended 32-bit
; field (`__bfa_emit_operand_text`'s `movsxd`), so `addw u8 %x -1` would
; add 2^64-1 and `cmp.lt u32 %x 4294967295` compare against -1; refused by
; name instead. The verifier refuses the same text (rule 1); this check is
; here too because `exsc` reaches the emitter without the verifier.
proc __bfa_check_opimm, module, ty, raw, isimm
	locals
		slot imv, dq
	endl
	cmp	dword [isimm], 0
	je	.ok
	movsxd	rax, dword [raw]
	mov	[imv], rax
	mov	rdi, [module]
	mov	esi, [ty]
	mov	rdx, rax
	call	bfa_type_canon
	cmp	rax, [imv]
	je	.ok
	jmp	.after_msg
  .msg db "immediate operand is not a value of its type (ssa-ir.md 2.2)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .ok:
	return
endp

; __bfa_check_opimms(module, ty, a, b, aux) -- __bfa_check_opimm for both
; operands of a BIN/CMP-shaped instruction (aux 0x100: `a` is an
; immediate, 0x200: `b` is).
proc __bfa_check_opimms, module, ty, a, b, aux
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	mov	edx, [a]
	mov	ecx, [aux]
	and	ecx, 0x100
	call	__bfa_check_opimm
	mov	rdi, [module]
	mov	esi, [ty]
	mov	edx, [b]
	mov	ecx, [aux]
	and	ecx, 0x200
	call	__bfa_check_opimm
	return
endp

; ============================================================================
; Integer per-opcode lowering. Tier 1 was i64/u64 only; every width from 1
; to 64 now, through the canonical form above.
; ============================================================================

; __bfa_emit_addsub(ctx, module, func, instid) -- add/sub (trapping) and
; addw/subw (wrapping). At 64 bits: signed overflow is OF (`jo`), unsigned
; overflow (a borrow, for subtraction) is CF (`jc`) -- the same ADD/SUB
; encoding either way, only the flag checked differs, exactly what x86
; already gives for free. Below 64: the normalisation, and for the trapping
; pair the compare against it (the canonical-form section, above).
proc __bfa_emit_addsub, ctx, module, func, instid
	locals
		slot av, dd
		slot bv, dd
		slot auxv, dd
		slot tyv, dd
		slot issub, dd
		slot issigned, dd
		slot trap, dd
		slot nbits, dd
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
	call	__bfa_int_width
	mov	[nbits], eax
	mov	rdi, [module]
	mov	esi, [tyv]
	mov	edx, [av]
	mov	ecx, [bv]
	mov	r8d, [auxv]
	call	__bfa_check_opimms

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

	cmp	dword [nbits], 64
	jb	.narrow
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
	jmp	.notrap

	; N < 64: no 64-bit carry or overflow is possible (|a|, |b| < 2^63), so
	; the flags say nothing; whether the result is still a value of T is
	; whether normalising it changes it.
  .narrow:
	cmp	dword [trap], 0
	je	.wrapnorm
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfa_emit_normcheck
	jmp	.notrap
  .wrapnorm:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	xor	ecx, ecx
	call	__bfa_emit_norm
  .notrap:

	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_mul(ctx, module, func, instid) -- mul/mulw.
; Wrapping mul always uses the signed `imul` encoding regardless of the
; type's own sign: two's-complement wraparound is bit-for-bit identical
; either way, so there is nothing to distinguish -- see this file's header.
; Trapping mul does distinguish: unsigned overflow is `mul`'s CF/OF (either
; flag, they are set identically for MUL); signed is `imul`'s OF. Below 64
; bits the result is normalised (mulw) or checked against its normalised
; copy (mul) -- and at 33..63 bits the flag check STAYS, because the
; 64-bit product itself can overflow there and wrap to a value the compare
; accepts (the canonical-form section, above). At 32 bits and below the
; product of two canonical operands is exact in 64 bits and the flag is
; never set, so it is not emitted.
proc __bfa_emit_mul, ctx, module, func, instid
	locals
		slot av, dd
		slot bv, dd
		slot auxv, dd
		slot tyv, dd
		slot trap, dd
		slot issigned, dd
		slot bisimm, dd
		slot nbits, dd
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
	call	__bfa_int_width
	mov	[nbits], eax
	mov	rdi, [module]
	mov	esi, [tyv]
	mov	edx, [av]
	mov	ecx, [bv]
	mov	r8d, [auxv]
	call	__bfa_check_opimms

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
	cmp	dword [nbits], 33
	jb	.mulnotrap		; <= 32 bits: the 64-bit product is exact
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
	cmp	dword [nbits], 33
	jb	.mulnotrap		; <= 32 bits: the 64-bit product is exact
	jmp	.after_jc2
  .lit_jc2 db "	jc	bfausr_trap", 10
  .after_jc2:
	mov	rdi, [ctx]
	lea	rsi, [.lit_jc2]
	mov	rdx, .after_jc2 - .lit_jc2
	call	__bfa_out_lit

  .mulnotrap:
	cmp	dword [nbits], 64
	jae	.mulstore
	cmp	dword [trap], 0
	je	.mulnorm
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfa_emit_normcheck
	jmp	.mulstore
  .mulnorm:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	xor	ecx, ecx
	call	__bfa_emit_norm
  .mulstore:
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

; __bfa_emit_cmp(ctx, module, func, instid) -- cmp.* only; fcmp.* has its
; own lowering now (__bfa_emit_fcmp, below, dispatched separately). Naive on
; purpose: the result is computed into its own slot as 0/1, independently
; of whatever `br` later does with it -- no fusing, per this whole
; backend's "one instruction per IR op" rule (ADR 0012).
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
  .nfmsg db "fcmp: reached the integer comparison lowering (a dispatcher bug)"
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

	; Any width, and no normalisation: two canonical values compare in 64
	; bits exactly as they do in N, provided the condition code follows the
	; SIGN -- zero-extended u8 200 is above 100, sign-extended i8 -56 is
	; below it. `aux` also carries the immediate flags (0x100/0x200) above
	; the predicate's three bits.
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_int_width
	mov	rdi, [module]
	mov	esi, [tyv]
	mov	edx, [av]
	mov	ecx, [bv]
	mov	r8d, [auxv]
	call	__bfa_check_opimms

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

; ============================================================================
; Floats -- f32/f64 through the SSE2 baseline (ssa-ir.md 2.3; spec 5.4)
; ============================================================================
;
; A float lives in its 8-byte slot as its raw IEEE BIT PATTERN -- an f32
; ZERO-EXTENDED, the same canonical-slot discipline a u8 keeps (ssa-ir.md
; 2.2) -- and every operation loads into xmm0/xmm1, computes, and stores
; back before the next instruction boundary (the header's rule). The
; mnemonics are the SSE2 ones the vendored assembler already carries
; (vendor/fasmg-x86/ext/sse2.inc, on format.inc's include chain); nothing
; here is a later extension -- no FMA3 (fma is refused by name, in the
; dispatcher), no SSE4.1 roundsd (and no need for one: `ftrunc` is the
; float NARROWING opcode, not round-to-integral -- see __bfa_emit_fconv).
;
; NO MXCSR CODE, on purpose. The prelude (exsrt_start, prelude/prelude.asm)
; executes `ldmxcsr` of EXS_MXCSR = 0x1F80 before `call bfausr_initium` --
; `ad_parem` (round to nearest-even), every exception masked, no FTZ/DAZ,
; `subnormales conservata` -- and program.inc writes that image into every
; compiled program, so an `addsd` here already rounds nearest-even and
; NEVER TRAPS (spec 5.4). The `numeri` words parse.inc records on a
; BfaFunc are not re-honoured per function: the image is the program's
; one, and the emitter adds nothing to it.
;
; WHAT THIS SECTION REFUSES, by name:
;   float immediates  `fadd f64 %x 3` PARSES (__bfa_ps_bin takes an
;                    immediate for a float type too) and the verifier
;                    PASSES it (bfa_type_canon is the identity on a
;                    float's bits, so nothing is out of range) -- the
;                    refusal is the emitter's, because `fconst` is the
;                    leaf: "raw bit pattern, never decimal text"
;                    (ssa-ir.md 2.3)
;   fconst f32 >= 2^32   not a canonical f32 slot value
;   itof u64 -> f32      no correctly-rounded SSE2 sequence exists: going
;                    through f64 rounds twice
; An UNORDERED fcmp cannot reach this file at all -- parse.inc's opdef
; table has no spelling for one -- so there is no unordered arm to refuse
; and none to write; the ordered six are everything that exists.

; __bfa_emit_fwidth(module, ty) -> eax = 8 for f64, 4 for f32; anything
; else dies. The float lowerings below are two-instruction families (sd/
; ss) and this is how they pick their half; a non-float reaching one is
; a type error the verifier does not cross-check, and it dies here rather
; than emitting a wrong-width load.
proc __bfa_emit_fwidth, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfa_ty_info
	cmp	edx, BFA_TK_F32
	je	.f32
	cmp	edx, BFA_TK_F64
	je	.f64
	jmp	.after_msg
  .msg db "float lowering: the type is not f32/f64"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .f32:
	mov	eax, 4
	return
  .f64:
	mov	eax, 8
	return
endp

; __bfa_emit_fload(ctx, xreg, width, valueid) -- "movsd/movss xmm<k>, [slot]"
; for value `valueid` into xmm register `xreg` (0 or 1). The `qword`/
; `dword` keyword is not decoration: fasmg's SSE macros take the memory
; operand's width from it (vendor ext/sse2.inc), and without one an f32
; load would be assembled as 8 bytes.
proc __bfa_emit_fload, ctx, xreg, width, valueid
	locals
	endl
	cmp	dword [width], 8
	je	.sd
	__bfa_emtxt 9, "movss", 9, "xmm"
	mov	rdi, [ctx]
	mov	esi, [xreg]
	call	__bfa_out_u64
	__bfa_emtxt ", dword "
	jmp	.slot
  .sd:
	__bfa_emtxt 9, "movsd", 9, "xmm"
	mov	rdi, [ctx]
	mov	esi, [xreg]
	call	__bfa_out_u64
	__bfa_emtxt ", qword "
  .slot:
	mov	rdi, [ctx]
	mov	esi, [valueid]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	return
endp

; __bfa_emit_fstore(ctx, width, valueid) -- xmm0's low value into the
; result slot, THROUGH rax. Not the table's one-line `movsd [slot], xmm0`
; on purpose: an f32 store through memory writes 4 bytes and leaves the
; slot's other four whatever they were, and the canonical f32 slot is
; zero-extended -- `movd eax, xmm0` zero-extends into all of rax, so the
; 8-byte store puts the canonical form in the slot exactly the way every
; narrow integer result does. f64 goes through `movq rax, xmm0` for the
; uniform shape (one helper, two arms).
proc __bfa_emit_fstore, ctx, width, valueid
	locals
	endl
	cmp	dword [width], 8
	je	.sd
	__bfa_emtxt 9, "movd", 9, "eax, xmm0", 10
	jmp	.tostore
  .sd:
	__bfa_emtxt 9, "movq", 9, "rax, xmm0", 10
  .tostore:
	mov	rdi, [ctx]
	mov	esi, [valueid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_fconst(ctx, module, func, instid) -- `%v = fconst T bits`, the
; one way a float constant enters (ssa-ir.md 2.3: "raw bit pattern, never
; decimal text"). Materialised exactly the way a wide `iconst` is --
; `mov rax, <bits>`, then the store -- with the bits printed UNSIGNED:
; every negative f64 has bit 63 set, and a signed print of the same bits
; is a different number. ALWAYS the two-line form: `mov qword [slot], imm`
; takes a SIGN-EXTENDED 32-bit immediate (x86.simm32, the same reason
; __bfa_emit_iconst keeps a wide path), and an f32's canonical slot is
; zero-extended -- 0xBF800000 (-1.0f) through a simm32 would store
; 0xFFFFFFFFBF800000. An f32 pattern at or above 2^32 is refused: the
; verifier's fconst rule checks nothing (d_trivial), so this is the only
; place the canonical slot form is enforced.
proc __bfa_emit_fconst, ctx, module, func, instid
	locals
		slot av, dd
		slot lo, dd
		slot hi, dd
		slot tyv, dd
		slot bits, dq
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx

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
	mov	[bits], rax

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_ty_info
	cmp	edx, BFA_TK_F32
	je	.f32ty
	cmp	edx, BFA_TK_F64
	je	.checkdone
	jmp	.after_notfty
  .notfty db "fconst: the type is not f32/f64"
  .after_notfty:
	lea	rdi, [.notfty]
	mov	rsi, .after_notfty - .notfty
	call	__bfa_emit_die
  .f32ty:
	mov	rax, [bits]
	mov	rcx, 0x100000000
	cmp	rax, rcx
	jb	.checkdone
	jmp	.after_wide32
  .wide32 db "fconst f32: the bit pattern is wider than 32 bits (the canonical f32 slot is zero-extended)"
  .after_wide32:
	lea	rdi, [.wide32]
	mov	rsi, .after_wide32 - .wide32
	call	__bfa_emit_die
  .checkdone:
	__bfa_emtxt 9, "mov", 9, "rax, "
	mov	rdi, [ctx]
	mov	rsi, [bits]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_farith(ctx, module, func, instid) -- fadd/fsub/fmul/fdiv. One
; IEEE rounding each (ssa-ir.md 2.3), delivered by the MXCSR image the
; prelude already loaded -- `reassociatio vetita` is why there is no
; re-forming of a sum here, and `contractio explicita` is why there is no
; fused op either (fma's refusal is the dispatcher's, by name). NaN
; PROPAGATES the way the hardware propagates it, payload and all; there
; is deliberately no canonical-NaN step. Div by zero never traps (masked)
; and produces the IEEE infinity, which is what spec 5.4 asks the default
; `numeri` to do.
proc __bfa_emit_farith, ctx, module, func, instid
	locals
		slot av, dd
		slot bv, dd
		slot auxv, dd
		slot tyv, dd
		slot op, dd
		slot width, dd
	endl
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
	mov	[op], ecx

	test	dword [auxv], 0x300
	jz	.noimm
	jmp	.after_fimm
  .fimm db "fadd/fsub/fmul/fdiv: float operands are values only (`fconst` is the leaf -- bits, never decimal)"
  .after_fimm:
	lea	rdi, [.fimm]
	mov	rsi, .after_fimm - .fimm
	call	__bfa_emit_die
  .noimm:
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_emit_fwidth
	mov	[width], eax

	mov	rdi, [ctx]
	xor	esi, esi
	mov	edx, [width]
	mov	ecx, [av]
	call	__bfa_emit_fload
	mov	rdi, [ctx]
	mov	esi, 1
	mov	edx, [width]
	mov	ecx, [bv]
	call	__bfa_emit_fload

	cmp	dword [width], 8
	jne	.ss
	cmp	dword [op], BFA_OP_FADD
	jne	.n1
	__bfa_emtxt 9, "addsd", 9, "xmm0, xmm1", 10
	jmp	.tostore
  .n1:
	cmp	dword [op], BFA_OP_FSUB
	jne	.n2
	__bfa_emtxt 9, "subsd", 9, "xmm0, xmm1", 10
	jmp	.tostore
  .n2:
	cmp	dword [op], BFA_OP_FMUL
	jne	.n3
	__bfa_emtxt 9, "mulsd", 9, "xmm0, xmm1", 10
	jmp	.tostore
  .n3:
	__bfa_emtxt 9, "divsd", 9, "xmm0, xmm1", 10
	jmp	.tostore
  .ss:
	cmp	dword [op], BFA_OP_FADD
	jne	.m1
	__bfa_emtxt 9, "addss", 9, "xmm0, xmm1", 10
	jmp	.tostore
  .m1:
	cmp	dword [op], BFA_OP_FSUB
	jne	.m2
	__bfa_emtxt 9, "subss", 9, "xmm0, xmm1", 10
	jmp	.tostore
  .m2:
	cmp	dword [op], BFA_OP_FMUL
	jne	.m3
	__bfa_emtxt 9, "mulss", 9, "xmm0, xmm1", 10
	jmp	.tostore
  .m3:
	__bfa_emtxt 9, "divss", 9, "xmm0, xmm1", 10
  .tostore:
	mov	rdi, [ctx]
	mov	esi, [width]
	mov	edx, [instid]
	call	__bfa_emit_fstore
	return
endp

; __bfa_emit_fneg(ctx, module, func, instid) -- the sign bit ALONE, XORed
; in: f64 mask 0x8000000000000000, f32 mask 0x80000000, each materialised
; the way an `iconst` is. The mask form is not just the table's preference:
; XOR touches only bit 63/31, so a NaN operand keeps its PAYLOAD (ssa-ir.md
; 2.3 wants exactly that), where `0 - x` is an arithmetic op that would
; run the (masked, hence silent) invalid path on a signaling NaN.
proc __bfa_emit_fneg, ctx, module, func, instid
	locals
		slot av, dd
		slot tyv, dd
		slot width, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_emit_fwidth
	mov	[width], eax

	mov	rdi, [ctx]
	xor	esi, esi
	mov	edx, [width]
	mov	ecx, [av]
	call	__bfa_emit_fload

	cmp	dword [width], 8
	jne	.ss
	__bfa_emtxt 9, "mov", 9, "rax, 9223372036854775808", 10
	__bfa_emtxt 9, "movq", 9, "xmm1, rax", 10
	__bfa_emtxt 9, "xorpd", 9, "xmm0, xmm1", 10
	jmp	.tostore
  .ss:
	__bfa_emtxt 9, "mov", 9, "ecx, 2147483648", 10
	__bfa_emtxt 9, "movd", 9, "xmm1, ecx", 10
	__bfa_emtxt 9, "xorps", 9, "xmm0, xmm1", 10
  .tostore:
	mov	rdi, [ctx]
	mov	esi, [width]
	mov	edx, [instid]
	call	__bfa_emit_fstore
	return
endp

; __bfa_emit_fcmp(ctx, module, func, instid) -- fcmp.eq/ne/lt/le/gt/ge, the
; ORDERED forms: FALSE on any NaN operand (ssa-ir.md 2.3). `ucomisd`
; latches ZF/CF/PF; unordered (a NaN on either side) raises all three, and
; the emitted pair
;
;	setnp	cl		; 1 iff ORDERED -- PF clear
;	set<cc>	al		; the predicate, the same unsigned SETcc arm an
;				; integer `cmp.*` uses (__bfa_emit_setcc)
;	and	al, cl		; a NaN operand forces the 0
;
; is the short-circuit the design table drew as `jp .false`, drawn WITHOUT
; the branch: a jump needs a label, a block holding two `fcmp`s would
; define `.false` twice, and a per-instruction label is a namespace this
; file spends only where there is no branch-free form (the copy loop's
; `<prefix>c<id>`). Same truth table, no label. For `eq` the `and` is the
; whole point -- unordered latches ZF=1, so a bare `sete` would answer 1;
; for `ne` it is belt to braces (unordered already clears setne); uniform
; across the six is the readable choice. a rides in xmm0 and b in xmm1,
; `ucomisd xmm0, xmm1` compares a AGAINST b, so the conditions are the
; unsigned arm exactly: lt -> setb, le -> setbe, gt -> seta, ge -> setae.
proc __bfa_emit_fcmp, ctx, module, func, instid
	locals
		slot av, dd
		slot bv, dd
		slot auxv, dd
		slot tyv, dd
		slot pred, dd
		slot width, dd
	endl
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
	mov	eax, [auxv]
	and	eax, 7
	mov	[pred], eax

	test	dword [auxv], 0x300
	jz	.noimm
	jmp	.after_fimm
  .fimm db "fcmp: float operands are values only (`fconst` is the leaf -- bits, never decimal)"
  .after_fimm:
	lea	rdi, [.fimm]
	mov	rsi, .after_fimm - .fimm
	call	__bfa_emit_die
  .noimm:
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_emit_fwidth
	mov	[width], eax

	mov	rdi, [ctx]
	xor	esi, esi
	mov	edx, [width]
	mov	ecx, [av]
	call	__bfa_emit_fload
	mov	rdi, [ctx]
	mov	esi, 1
	mov	edx, [width]
	mov	ecx, [bv]
	call	__bfa_emit_fload

	cmp	dword [width], 8
	jne	.ss
	__bfa_emtxt 9, "ucomisd", 9, "xmm0, xmm1", 10
	jmp	.tocc
  .ss:
	__bfa_emtxt 9, "ucomiss", 9, "xmm0, xmm1", 10
  .tocc:
	__bfa_emtxt 9, "setnp", 9, "cl", 10
	__bfa_emtxt 9
	mov	rdi, [ctx]
	mov	esi, [pred]
	xor	edx, edx
	call	__bfa_emit_setcc
	__bfa_emtxt 9, "al", 10
	__bfa_emtxt 9, "and", 9, "al, cl", 10
	__bfa_emtxt 9, "movzx", 9, "rax, al", 10
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_fconv(ctx, module, func, instid) -- fext/ftrunc/itof/ftoi. `T`
; is the DESTINATION and the source is the operand's own type, exactly as
; zext/sext/trunc (verify.inc's finding 3); one proc because the four
; share the source-type dispatch:
;
;   fext   f32 -> f64    cvtss2sd, EXACT -- a widening never rounds
;   ftrunc f64 -> f32    cvtsd2ss, the ONE rounding (ad_parem); a value
;                        outside f32's range gives the IEEE infinity,
;                        which is the only honest narrowing there is
;   ftrunc f32 -> f32, f64 -> f64   a plain slot copy: lower/expr.inc's
;                        `__lwr_cast` emits `ftrunc` for an equal-width
;                        float cast too, and the bits already are the
;                        value (lowering.md 535: "f32 <-> f64 fext/ftrunc")
;   itof   iN/uN -> f64  cvtsi2sd on the canonical 64-bit slot: every
;                        canonical uN below 64 is nonnegative, so the
;                        SIGNED convert is the unsigned one too, and an
;                        iN is sign-extended, which is what it wants
;   itof   u64 -> f64    the exact 32/32 split, below
;   itof   iN/uN -> f32  cvtsi2ss, same reasoning
;   ftoi   -> iN/uN      cvttsd2si/cvttss2si: truncate toward zero, never
;                        trapping (masked), then the integer canonical
;                        normalisation
;
; `ftrunc` is NOT round-to-integral, so no roundsd and no SSE4.1 anywhere.
; `ftoi` on NaN or out of range leaves the hardware's integer-indefinite
; value; ssa-ir.md 2.3 marks that [OPEN] and no fixture pins it.
;
; THE u64 -> f64 SPLIT. `cvtsi2sd` converts SIGNED, so a u64 with bit 63
; set would come back negative. For ANY u64:
;
;	hi = a >> 32		at most 32 significand bits -- an EXACT f64
;	hi * 2^32		an exponent shift, still EXACT
;	lo = a & 0xffffffff	EXACT
;	addsd			the ONE rounding, ad_parem, of the true sum
;
; Both addends being exact is what makes the single rounding the
; CORRECTLY rounded result: the sum is rounded once, on its true value --
; never twice, which is the double-rounding hazard the u64 -> f32 refusal
; is about.
proc __bfa_emit_fconv, ctx, module, func, instid
	locals
		slot av, dd
		slot tyv, dd
		slot op, dd
		slot skind, dd
		slot swidth, dd
		slot dwidth, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx

	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_value_type
	cmp	eax, BFA_VALUE_TYPE_IS_U1
	jne	.havesty
	mov	dword [skind], BFA_TK_U	; a u1 is an integer like any other
	mov	dword [swidth], 1
	jmp	.stydone
  .havesty:
	mov	rdi, [module]
	mov	esi, eax
	call	__bfa_ty_info
	mov	[skind], edx
	; BfaType.width is the float's BIT count (32/64), but every helper in
	; this file speaks the SLOT width (4/8): __bfa_emit_fload's mnemonic,
	; ftrunc's narrow-or-copy test against __bfa_emit_fwidth's eax, ftoi's
	; cvttsd2si/cvttss2si pick. Map the two floats down here and keep the
	; bit width for the integer kinds -- itof's u64 test below compares
	; against 64, the BIT width.
	cmp	edx, BFA_TK_F32
	jne	.notf32w
	mov	dword [swidth], 4
	jmp	.stydone
  .notf32w:
	cmp	edx, BFA_TK_F64
	jne	.notf64w
	mov	dword [swidth], 8
	jmp	.stydone
  .notf64w:
	mov	[swidth], eax
  .stydone:

	cmp	dword [op], BFA_OP_FEXT
	jne	.notfext
	cmp	dword [skind], BFA_TK_F32
	je	.fextf32
	cmp	dword [skind], BFA_TK_F64
	je	.fextbad
	jmp	.after_fextnf
  .fextnf db "fext: the operand is not a float"
  .after_fextnf:
	lea	rdi, [.fextnf]
	mov	rsi, .after_fextnf - .fextnf
	call	__bfa_emit_die
  .fextbad:
	jmp	.after_fextbad
  .fextbadmsg db "fext: not a widening f32 -> f64"
  .after_fextbad:
	lea	rdi, [.fextbadmsg]
	mov	rsi, .after_fextbad - .fextbadmsg
	call	__bfa_emit_die
  .fextf32:
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_emit_fwidth
	cmp	eax, 8
	jne	.fextbad
	mov	[dwidth], eax
	mov	rdi, [ctx]
	xor	esi, esi
	mov	edx, 4
	mov	ecx, [av]
	call	__bfa_emit_fload
	__bfa_emtxt 9, "cvtss2sd", 9, "xmm0, xmm0", 10
	mov	rdi, [ctx]
	mov	esi, [dwidth]
	mov	edx, [instid]
	call	__bfa_emit_fstore
	return
  .notfext:
	cmp	dword [op], BFA_OP_FTRUNC
	jne	.notftrunc
	cmp	dword [skind], BFA_TK_F32
	je	.ftruncok
	cmp	dword [skind], BFA_TK_F64
	je	.ftruncok
	jmp	.after_ftnf
  .ftnf db "ftrunc: the operand is not a float"
  .after_ftnf:
	lea	rdi, [.ftnf]
	mov	rsi, .after_ftnf - .ftnf
	call	__bfa_emit_die
  .ftruncok:
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_emit_fwidth
	mov	[dwidth], eax
	cmp	eax, [swidth]
	je	.ftrunccopy
	jg	.ftruncbad
	mov	rdi, [ctx]
	xor	esi, esi
	mov	edx, [swidth]
	mov	ecx, [av]
	call	__bfa_emit_fload
	__bfa_emtxt 9, "cvtsd2ss", 9, "xmm0, xmm0", 10
	mov	rdi, [ctx]
	mov	esi, [dwidth]
	mov	edx, [instid]
	call	__bfa_emit_fstore
	return
  .ftrunccopy:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_out_movq_rax_slot
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
  .ftruncbad:
	jmp	.after_ftbad
  .ftbad db "ftrunc: not a narrowing (a widening float cast is `fext`)"
  .after_ftbad:
	lea	rdi, [.ftbad]
	mov	rsi, .after_ftbad - .ftbad
	call	__bfa_emit_die
  .notftrunc:
	cmp	dword [op], BFA_OP_ITOF
	jne	.notitof
	cmp	dword [skind], BFA_TK_U
	je	.itofok
	cmp	dword [skind], BFA_TK_I
	je	.itofok
	jmp	.after_itint
  .itint db "itof: the operand is not an integer (uN/iN)"
  .after_itint:
	lea	rdi, [.itint]
	mov	rsi, .after_itint - .itint
	call	__bfa_emit_die
  .itofok:
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_emit_fwidth
	mov	[dwidth], eax
	cmp	dword [skind], BFA_TK_U
	jne	.itofsimple
	cmp	dword [swidth], 64
	jne	.itofsimple
	cmp	dword [dwidth], 4
	je	.itofu32bad
	; u64 -> f64: the exact 32/32 split (the header above proves it)
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_out_movq_rax_slot
	__bfa_emtxt 9, "mov", 9, "rcx, rax", 10
	__bfa_emtxt 9, "shr", 9, "rax, 32", 10
	__bfa_emtxt 9, "cvtsi2sd", 9, "xmm0, rax", 10
	__bfa_emtxt 9, "mov", 9, "rax, 4751297606875873280", 10
	__bfa_emtxt 9, "movq", 9, "xmm1, rax", 10
	__bfa_emtxt 9, "mulsd", 9, "xmm0, xmm1", 10
	__bfa_emtxt 9, "mov", 9, "eax, ecx", 10
	__bfa_emtxt 9, "cvtsi2sd", 9, "xmm1, rax", 10
	__bfa_emtxt 9, "addsd", 9, "xmm0, xmm1", 10
	mov	rdi, [ctx]
	mov	esi, 8
	mov	edx, [instid]
	call	__bfa_emit_fstore
	return
  .itofu32bad:
	jmp	.after_itu32
  .itu32 db "itof: u64 -> f32 has no correctly-rounded SSE2 sequence (through f64 it would round twice)"
  .after_itu32:
	lea	rdi, [.itu32]
	mov	rsi, .after_itu32 - .itu32
	call	__bfa_emit_die
  .itofsimple:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_out_movq_rax_slot
	cmp	dword [dwidth], 8
	jne	.itofss
	__bfa_emtxt 9, "cvtsi2sd", 9, "xmm0, rax", 10
	jmp	.itofstore
  .itofss:
	__bfa_emtxt 9, "cvtsi2ss", 9, "xmm0, rax", 10
  .itofstore:
	mov	rdi, [ctx]
	mov	esi, [dwidth]
	mov	edx, [instid]
	call	__bfa_emit_fstore
	return
  .notitof:
	cmp	dword [op], BFA_OP_FTOI
	jne	.notftoi
	cmp	dword [skind], BFA_TK_F32
	je	.ftoiok
	cmp	dword [skind], BFA_TK_F64
	je	.ftoiok
	jmp	.after_ftonf
  .ftonf db "ftoi: the operand is not a float"
  .after_ftonf:
	lea	rdi, [.ftonf]
	mov	rsi, .after_ftonf - .ftonf
	call	__bfa_emit_die
  .ftoiok:
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_ty_info
	cmp	edx, BFA_TK_U
	je	.ftoidst
	cmp	edx, BFA_TK_I
	je	.ftoidst
	jmp	.after_ftond
  .ftond db "ftoi: the destination is not an integer (uN/iN)"
  .after_ftond:
	lea	rdi, [.ftond]
	mov	rsi, .after_ftond - .ftond
	call	__bfa_emit_die
  .ftoidst:
	mov	rdi, [ctx]
	xor	esi, esi
	mov	edx, [swidth]
	mov	ecx, [av]
	call	__bfa_emit_fload
	cmp	dword [swidth], 8
	jne	.ftoiss
	__bfa_emtxt 9, "cvttsd2si", 9, "rax, xmm0", 10
	jmp	.ftoinorm
  .ftoiss:
	__bfa_emtxt 9, "cvttss2si", 9, "rax, xmm0", 10
  .ftoinorm:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	xor	ecx, ecx
	call	__bfa_emit_norm
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
  .notftoi:
	jmp	.after_fcunr
  .fcunr db "fconv: an opcode the dispatcher does not route here"
  .after_fcunr:
	lea	rdi, [.fcunr]
	mov	rsi, .after_fcunr - .fcunr
	call	__bfa_emit_die
endp

; __bfa_emit_argxmm(ctx, i) -- the SysV FLOAT argument register name for
; float-argument ordinal `i` (0..7), the xmm twin of __bfa_emit_argreg
; below (asm-conventions.md 1.1's table: "vector / float arguments
; xmm0-xmm7"). Eight is the limit: a ninth float argument is refused here
; exactly the way a seventh integer argument is refused there -- by name,
; because silently dropping it is the miscompile this file exists never
; to make.
proc __bfa_emit_argxmm, ctx, i
	locals
	endl
	jmp	.after_tab
  .tab db "xmm0", 0, 0, 0, 0, "xmm1", 0, 0, 0, 0, "xmm2", 0, 0, 0, 0, "xmm3", 0, 0, 0, 0
	db "xmm4", 0, 0, 0, 0, "xmm5", 0, 0, 0, 0, "xmm6", 0, 0, 0, 0, "xmm7", 0, 0, 0, 0
  .after_tab:
	mov	rax, [i]
	cmp	rax, 8
	jge	.toomany
	shl	rax, 3
	lea	rsi, [.tab]
	add	rsi, rax
	mov	rdx, 4
	mov	rdi, [ctx]
	call	__bfa_out_lit
	return
  .toomany:
	jmp	.after_msg
  .msg db "call: a ninth float argument is [OPEN] (eight XMM argument registers, asm-conventions.md 1.1)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
endp

; __bfa_emit_param(ctx, module, func, instid) -- moves the i'th argument
; register into this value's own slot, the register chosen by CLASS
; (asm-conventions.md 1.1): an F32/F64 parameter rides in xmm0..xmm7,
; every other type in rdi..r9, each class with its own ORDINAL counted
; through the signature's parameter list (BfaFunc.sig -> BfaSig ->
; Module.sig_extra, in order -- the SysV rule the table records). A
; function of six integers and eight floats -- fourteen parameters, both
; ordinals ending on their class's last register -- is the most this
; will place. A seventh integer or a ninth float is refused by name, and
; an index past the signature's parameters is refused too: the verifier
; checks nothing about a `param` (d_trivial), so this walk is the only
; place a phantom parameter is caught. Integer parameters emit exactly
; the text they always did -- same register for the same ordinal -- so
; no existing function's text moves. The bits land in the slot through
; rax the way every float result does, which is also what keeps an f32
; parameter's slot canonical (movd's zero-extend).
proc __bfa_emit_param, ctx, module, func, instid
	locals
		slot iv, dd
		slot j, dd
		slot pfirst, dd
		slot pcount, dd
		slot kind, dd
		slot iord, dd
		slot xord, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[iv], ecx

	mov	rax, [func]
	mov	esi, [rax + BfaFunc.sig]
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sigs]
	call	vec_get
	mov	ecx, [rax + BfaSig.p_first]
	mov	[pfirst], ecx
	mov	ecx, [rax + BfaSig.p_count]
	mov	[pcount], ecx

	mov	dword [iord], 0
	mov	dword [xord], 0
	mov	dword [j], 0
  .walk:
	mov	eax, [j]
	cmp	eax, [pcount]
	jge	.outofrange
	mov	eax, [pfirst]
	add	eax, [j]
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sig_extra]
	mov	esi, eax
	call	vec_get
	mov	esi, [rax]
	mov	rdi, [module]
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	mov	eax, [j]
	cmp	eax, [iv]
	je	.mine
	cmp	ecx, BFA_TK_F32
	je	.pre_sse
	cmp	ecx, BFA_TK_F64
	je	.pre_sse
	inc	dword [iord]
	jmp	.next
  .pre_sse:
	inc	dword [xord]
  .next:
	inc	dword [j]
	jmp	.walk
  .mine:
	mov	[kind], ecx
	jmp	.walkdone
  .outofrange:
	jmp	.after_oor
  .oor db "param: the index is past the signature's parameters (the verifier does not check one)"
  .after_oor:
	lea	rdi, [.oor]
	mov	rsi, .after_oor - .oor
	call	__bfa_emit_die
  .walkdone:
	cmp	dword [kind], BFA_TK_F32
	je	.float
	cmp	dword [kind], BFA_TK_F64
	je	.float

	; integer / pointer: rdi..r9 by the integer ordinal
	cmp	dword [iord], 5
	jg	.toomanyint
	__bfa_emtxt 9, "mov", 9, "qword "
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot
	__bfa_emtxt ", "
	mov	rdi, [ctx]
	mov	esi, [iord]
	call	__bfa_emit_argreg
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	return
  .toomanyint:
	jmp	.after_tmi
  .tmi db "param index > 5 (Tier 1's 6-argument limit)"
  .after_tmi:
	lea	rdi, [.tmi]
	mov	rsi, .after_tmi - .tmi
	call	__bfa_emit_die

	; F32/F64: xmm0..xmm7 by the SSE ordinal, into the slot through rax
  .float:
	cmp	dword [xord], 7
	jg	.toomanyflt
	cmp	dword [kind], BFA_TK_F64
	jne	.f32in
	__bfa_emtxt 9, "movq", 9, "rax, "
	jmp	.reg
  .f32in:
	__bfa_emtxt 9, "movd", 9, "eax, "
  .reg:
	mov	rdi, [ctx]
	mov	esi, [xord]
	call	__bfa_emit_argxmm
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
  .toomanyflt:
	jmp	.after_tmf
  .tmf db "param: a ninth float parameter is [OPEN] (eight XMM argument registers, asm-conventions.md 1.1)"
  .after_tmf:
	lea	rdi, [.tmf]
	mov	rsi, .after_tmf - .tmf
	call	__bfa_emit_die
endp

; __bfa_emit_iconst(ctx, module, func, instid) -- `%v = iconst T imm`.
;
; `mov qword [slot], imm` takes a SIGN-EXTENDED 32-bit immediate (fasmg's
; `x86.simm32`, vendor/fasmg-x86/x64.inc), so a constant outside
; [-2^31, 2^31) -- 3735928559, 0xDEADBEEF, is the one the wire codec needs
; -- did not assemble (the wire-codec plan's B4). Such a constant goes
; through `mov rax, imm64` and a store; one that fits keeps the one-line
; text, so no existing function's text moves. The constant must already be
; a value of T in canonical form (`iconst u8 256` or `iconst i8 200` is
; refused by name, as the verifier's rule 1 refuses it).
proc __bfa_emit_iconst, ctx, module, func, instid
	locals
		slot av, dd
		slot lo, dd
		slot hi, dd
		slot tyv, dd
		slot cval, dq
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx

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
	mov	[cval], rax

	mov	rdi, [module]
	mov	esi, [tyv]
	mov	rdx, [cval]
	call	bfa_type_canon
	cmp	rax, [cval]
	je	.canonical
	jmp	.after_msg
  .msg db "iconst: the constant is not a value of its type (ssa-ir.md 2.2)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .canonical:
	mov	rax, [cval]
	movsxd	rcx, eax
	cmp	rcx, rax
	jne	.wide

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

	mov	rdi, [ctx]
	mov	rsi, [cval]
	call	__bfa_out_i64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	return

  .wide:
	__bfa_emtxt 9, "mov", 9, "rax, "
	mov	rdi, [ctx]
	mov	rsi, [cval]
	call	__bfa_out_i64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; ============================================================================
; phi -- parallel copies on edges (docs/design/ssa-ir.md section 2.4)
; ============================================================================
;
; A phi emits NO code in its own block: its stack slot is simply where every
; incoming edge leaves the value. On an edge P -> B where B has phis, each
; phi of B receives the operand its (bP %v) pair names for P, emitted as
;
;       push    qword [rbp-8*v1]        ; v_i = phi_i's operand for P,
;       ...                             ;       in phi-list order
;       push    qword [rbp-8*vn]
;       pop     qword [rbp-8*phi_n]     ; then the phi slots, REVERSED
;       ...
;       pop     qword [rbp-8*phi_1]
;
; Every operand is read before any phi slot is written, so this is a
; PARALLEL copy: a swap (`a = phi [P: b]`, `b = phi [P: a]`) and a phi that
; names itself are both right with no cycle breaking, which a sequence of
; `mov`s would need. It works only because nothing is live in a register
; across an instruction boundary (this file's header) -- the stack is the
; one scratch area -- and the pushes and pops balance before the edge's
; `jmp`, so the `rsp` alignment `__bfa_emit_call` relies on is untouched.
;
; WHERE THE COPIES GO. After a `jmp` the edge is P's only way out, so they
; precede the `jmp`. After a `br` they cannot precede it: the other edge
; would run them too, which is the classic LOST COPY (a loop exit reading
; the header's phi would see the next iteration's value). A `br` into a
; block with phis therefore targets a per-edge STUB, emitted right after it:
;
;       <prefix><P>e0:  copies for P -> bT, jmp <prefix><bT>      true edge
;       <prefix><P>e1:  copies for P -> bF, jmp <prefix><bF>      false edge
;
; A block id and an edge ordinal, so the name is a function of the IR and
; never of an address (CLAUDE.md, determinism). A block label is `<prefix>`
; plus digits and cannot contain the `e`, so a stub never collides with one.
; A `br` neither of whose targets has phis emits exactly the text it always
; did, and so does a `jmp` into a block without phis.

; __bfa_phi_operand(func, phiv, pblk, skipn) -> eax = the value id phi
; `phiv` receives on the edge from block `pblk`. The operands are (bP %v)
; word pairs at extra[a .. a+b) -- ir.inc's BFA_SH_PHI; `b` is the WORD
; count (lower/ssa.inc's __lwr_add_phi_ops says so too). `skipn` matching
; pairs are passed over first: nonzero only for a `br` whose two targets are
; the same block, which gives that block two edges from one predecessor and
; each of its phis two pairs naming it, in edge order (the lowering never
; builds one; tests/ir/phi_same_target.ir does). A phi with no pair
; for `pblk` is refused -- that is verifier rule 3, and picking an operand
; anyway would be a silent miscompile.
proc __bfa_phi_operand, func, phiv, pblk, skipn
	uses	rbx, r12
	locals
		slot eidx, dd
		slot cnt, dd
	endl
	mov	rdi, [func]
	mov	esi, [phiv]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[cnt], ecx
	xor	ebx, ebx			; word offset of the current pair
	mov	r12d, [skipn]
  .loop:
	cmp	ebx, [cnt]
	jge	.none
	mov	rdi, [func]
	mov	esi, [eidx]
	add	esi, ebx
	call	bfa_extra_get			; the pair's predecessor block
	cmp	eax, [pblk]
	jne	.next
	test	r12d, r12d
	jz	.hit
	dec	r12d
  .next:
	add	ebx, 2
	jmp	.loop
  .hit:
	mov	rdi, [func]
	mov	esi, [eidx]
	add	esi, ebx
	inc	esi
	call	bfa_extra_get			; the pair's value
	return
  .none:
	jmp	.after_msg
  .msg db "phi: no operand for this predecessor (verifier rule 3)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
endp

; __bfa_emit_edge_copies(ctx, func, pblk, tblk, skipn) -- the parallel copy
; for the edge pblk -> tblk (see above). Emits NOTHING when tblk has no phis,
; which is what keeps every phi-free function's text byte-identical. The
; pops walk the phi list from its head once per phi -- O(n^2) in the phis of
; one block, this backend's standing trade (spec section 9.2).
proc __bfa_emit_edge_copies, ctx, func, pblk, tblk, skipn
	locals
		slot cur, dd
		slot phiv, dd
		slot nphi, dd
		slot i, dd
		slot j, dd
	endl
	mov	dword [nphi], 0
	mov	rdi, [func]
	mov	esi, [tblk]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_phi]
	mov	[cur], ecx
  .pushloop:
	cmp	dword [cur], 0
	je	.pushdone
	mov	rdi, [func]
	mov	esi, [cur]
	mov	edx, [pblk]
	mov	ecx, [skipn]
	call	__bfa_phi_operand
	mov	[phiv], eax
	mov	rdi, [ctx]
	jmp	.after_push
  .lit_push db "	push	qword "
  .after_push:
	lea	rsi, [.lit_push]
	mov	rdx, .after_push - .lit_push
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [phiv]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	eax, [nphi]
	inc	eax
	mov	[nphi], eax
	mov	rdi, [func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.pushloop
  .pushdone:
	mov	eax, [nphi]
	mov	[i], eax
  .poploop:
	cmp	dword [i], 0
	je	.done
	mov	eax, [i]
	dec	eax
	mov	[i], eax
	; walk to the i'th phi (0-based)
	mov	rdi, [func]
	mov	esi, [tblk]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_phi]
	mov	[cur], ecx
	mov	dword [j], 0
  .walk:
	mov	eax, [j]
	cmp	eax, [i]
	je	.found
	mov	rdi, [func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	mov	eax, [j]
	inc	eax
	mov	[j], eax
	jmp	.walk
  .found:
	mov	rdi, [ctx]
	jmp	.after_pop
  .lit_pop db "	pop	qword "
  .after_pop:
	lea	rsi, [.lit_pop]
	mov	rdx, .after_pop - .lit_pop
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [cur]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	jmp	.poploop
  .done:
	return
endp

; __bfa_emit_blklabel(ctx, lblptr, lbllen, blkid) -- `<prefix><blkid>`, no
; newline: the one spelling of a block's label, for its header and every
; branch to it.
proc __bfa_emit_blklabel, ctx, lblptr, lbllen, blkid
	locals
	endl
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [blkid]
	call	__bfa_out_u64
	return
endp

; __bfa_emit_stublabel(ctx, lblptr, lbllen, blkid, ord) -- `<prefix><blkid>e<ord>`,
; no newline: the edge stub for the `ord`'th successor (0 true, 1 false) of
; block `blkid`'s `br`.
proc __bfa_emit_stublabel, ctx, lblptr, lbllen, blkid, ord
	locals
	endl
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [blkid]
	call	__bfa_emit_blklabel
	mov	rdi, [ctx]
	mov	esi, 'e'
	call	__bfa_out_byte
	mov	rdi, [ctx]
	mov	esi, [ord]
	call	__bfa_out_u64
	return
endp

; __bfa_emit_jmp(ctx, func, instid, lblptr, lbllen) -- `lblptr`/`lbllen` is
; this function's own emitted block-label PREFIX (see
; __bfa_emit_function): the label for internal block id K is
; `<prefix><K>`, always -- built once per function, reused by every
; jmp/br it contains. A target with phis gets its parallel copy first.
proc __bfa_emit_jmp, ctx, func, instid, lblptr, lbllen
	locals
		slot btgt, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[btgt], ecx

	mov	rax, [ctx]
	mov	edx, [rax + BfaPrintCtx.curblk]
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	ecx, [btgt]
	xor	r8d, r8d
	call	__bfa_emit_edge_copies

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

; __bfa_emit_br(ctx, func, instid, lblptr, lbllen) -- a target with phis is
; reached through its edge stub, emitted after the two jumps (see "phi --
; parallel copies on edges", above); without phis the text is unchanged.
proc __bfa_emit_br, ctx, func, instid, lblptr, lbllen
	locals
		slot cv, dd
		slot btgt, dd
		slot bf, dd
		slot eidx, dd
		slot pcur, dd
		slot tphi, dd
		slot fphi, dd
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

	mov	rax, [ctx]
	mov	ecx, [rax + BfaPrintCtx.curblk]
	mov	[pcur], ecx
	mov	rdi, [func]
	mov	esi, [btgt]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_phi]
	mov	[tphi], ecx
	mov	rdi, [func]
	mov	esi, [bf]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_phi]
	mov	[fphi], ecx

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
	cmp	dword [fphi], 0
	jne	.jzstub
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [bf]
	call	__bfa_emit_blklabel
	jmp	.jzdone
  .jzstub:
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [pcur]
	mov	r8d, 1
	call	__bfa_emit_stublabel
  .jzdone:
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	jmp	.after_jmp
  .lit_jmp db "	jmp	"
  .after_jmp:
	mov	rdi, [ctx]
	lea	rsi, [.lit_jmp]
	mov	rdx, .after_jmp - .lit_jmp
	call	__bfa_out_lit
	cmp	dword [tphi], 0
	jne	.jmpstub
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [btgt]
	call	__bfa_emit_blklabel
	jmp	.jmpdone
  .jmpstub:
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [pcur]
	xor	r8d, r8d
	call	__bfa_emit_stublabel
  .jmpdone:
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	; the true edge's stub
	cmp	dword [tphi], 0
	je	.notstub
	jmp	.after_colon
  .lit_colon db ":", 10
  .after_colon:
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [pcur]
	xor	r8d, r8d
	call	__bfa_emit_stublabel
	mov	rdi, [ctx]
	lea	rsi, [.lit_colon]
	mov	rdx, .after_colon - .lit_colon
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [pcur]
	mov	ecx, [btgt]
	xor	r8d, r8d
	call	__bfa_emit_edge_copies
	mov	rdi, [ctx]
	lea	rsi, [.lit_jmp]
	mov	rdx, .after_jmp - .lit_jmp
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [btgt]
	call	__bfa_emit_blklabel
	mov	rdi, [ctx]
	call	__bfa_emit_nl
  .notstub:

	; the false edge's stub. When both targets are one block, this is that
	; block's SECOND edge from here, so its phis' second pair naming this
	; block is the one read (__bfa_phi_operand's `skipn`).
	cmp	dword [fphi], 0
	je	.nofstub
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [pcur]
	mov	r8d, 1
	call	__bfa_emit_stublabel
	mov	rdi, [ctx]
	lea	rsi, [.lit_colon]
	mov	rdx, .after_colon - .lit_colon
	call	__bfa_out_lit
	xor	r8d, r8d
	mov	eax, [bf]
	cmp	eax, [btgt]
	jne	.fskip
	mov	r8d, 1
  .fskip:
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [pcur]
	mov	ecx, [bf]
	call	__bfa_emit_edge_copies
	mov	rdi, [ctx]
	lea	rsi, [.lit_jmp]
	mov	rdx, .after_jmp - .lit_jmp
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [bf]
	call	__bfa_emit_blklabel
	mov	rdi, [ctx]
	call	__bfa_emit_nl
  .nofstub:
	return
endp

; __bfa_emit_ret(ctx, module, func, instid) -- the ONLY epilogue this file
; emits; there is exactly one per `ret`, inline, rather than a shared tail
; -- the naive choice (spec section 9.2: "do not write an optimizing
; backend"). A float-typed result rides in xmm0 (asm-conventions.md 1.1),
; loaded straight from the slot; the epilogue is the same three lines
; either way. The class is read off the OPERAND's own type -- the
; verifier's `ret` rule has already required it to equal the signature's
; return type, so the signature is not re-read here.
proc __bfa_emit_ret, ctx, module, func, instid
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
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_value_type
	cmp	eax, BFA_VALUE_TYPE_IS_U1
	je	.int
	mov	rdi, [module]
	mov	esi, eax
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_F32
	je	.f32
	cmp	ecx, BFA_TK_F64
	je	.f64
	jmp	.int
  .f32:
	__bfa_emtxt 9, "movss", 9, "xmm0, dword "
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	jmp	.epilogue
  .f64:
	__bfa_emtxt 9, "movsd", 9, "xmm0, qword "
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	jmp	.epilogue
  .int:
	mov	rdi, [ctx]
	mov	esi, [av]
	xor	edx, edx
	call	__bfa_emit_load_rax
  .novalue:
  .epilogue:
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

; ============================================================================
; Byte order and bit fields (ssa-ir.md 2.7; wire-codec.md milestone M4)
; ============================================================================
;
; A `load`/`store` is emitted in one of two forms, chosen by __bfa_mem_plan:
;
;   ONE INSTRUCTION -- `nativus` at 8, 16, 32 or 64 bits (and any 64-bit
;       type: `ptr`, a `ref`), and ANY order at 8 bits, where one byte has
;       no order to get wrong. This is the Tier-2/M3 text, byte for byte:
;       nothing M4 added reaches it, and bfa_emit_tier2/narrow/program,
;       passing unmodified, are the evidence.
;
;   BYTE BY BYTE -- `maior` and `minor` at 16, 24, 32, 40, 48, 56 and 64
;       bits, and `nativus` at 24/40/48/56, which on this host IS `minor`:
;       x86-64 is little-endian, so an in-process `u24` is three
;       little-endian bytes, the order a 32-bit `mov` of it would have
;       used. A load walks the bytes from most to least significant,
;       accumulating in rcx:
;
;           mov     rax, [rbp-8p]
;           movzx   ecx, byte [rax+A(n-1)]        ; most significant
;           shl     rcx, 8
;           movzx   edx, byte [rax+A(n-2)]
;           or      rcx, rdx                      ; ... down to A(0)
;           mov     qword [rbp-8v], rcx
;
;       which leaves a `uN` zero-extended -- canonical (ssa-ir.md 2.2) with
;       nothing further -- and an `iN` below 64 bits gets the normalisation
;       pair (`shl`/`sar rcx, 64-N`). A store mirrors it, least significant
;       byte first: `mov byte [rax+A(k)], cl` / `shr rcx, 8`. A(k), the
;       address of the byte of significance k, is `off + n-1-k` for `maior`
;       (big-endian: the most significant byte at the lowest address) and
;       `off + k` for `minor`.
;
; Why no `bswap`: vendor/fasmg-x86's x64.inc has `bswap` but not `movbe`,
; and `bswap` has no 24-, 40-, 48- or 56-bit form. Byte assembly is the one
; path every width shares, so it is the one path to test; a `bswap` fast
; path at 16/32/64 would be a second, and `tests/ir/byte_order.ir` would
; have to cover both.
;
; BIT FIELDS. `loadbits T %p byte bit` reads a `T` (1 to 8 bits wide) from
; byte `byte`, where `bit` counts from that byte's MOST significant bit:
; spec 5.2 rule 1, sub-byte fields pack MSB-first in declaration order, and
; checker/rows/layout.inc records a field's bit offset as the bits declared
; before it in its byte -- so DeModFrame's `versio` is `loadbits u4 %p 1 0`
; (bits 7..4 of byte 1) and `genus` is `loadbits u4 %p 1 4` (bits 3..0).
; With S = 8 - bit - w, the field is bits S..S+w-1 counted from the least
; significant end:
;
;       loadbits                         storebits
;       mov     rax, [rbp-8p]            mov     rax, [rbp-8p]
;       movzx   eax, byte [rax+B]        mov     rcx, [rbp-8v]
;       shr     eax, S                   and     ecx, 2^w-1
;       and     eax, 2^w-1               shl     ecx, S
;       mov     qword [rbp-8v], rax      movzx   edx, byte [rax+B]
;                                        and     edx, 255 xor ((2^w-1) shl S)
;                                        or      edx, ecx
;                                        mov     byte [rax+B], dl
;
; `shr`/`shl` by 0 are omitted. The store masks its value even though a
; canonical `uN` needs no mask (ssa-ir.md 2.2): a store of a non-canonical
; value spills into the NEIGHBOURING field -- the one instruction in this
; file where a broken invariant would damage bytes it was not asked to
; write -- and one `and` is what that costs.
;
; REFUSED, by name: a field that straddles a byte boundary (bit + w > 8).
; ssa-ir.md 2.7 lets `loadbits` straddle "if the declaration did", and the
; verifier admits it; this emitter does not implement it, because nothing
; needs it -- DeModFrame's only sub-byte fields are `versio`/`genus`, which
; share byte 1 exactly. Also refused: an `iN` bit field (spec 5.2 makes
; every `@transitus` field unsigned, so no lowering of a wire format
; produces one).

; __bfa_mem_plan(module, ty, ord) -> eax = the form a `load`/`store` of
; `ty` in byte order `ord` is emitted in, edx = its width in bytes:
;   BFA_ORDER_NATIVUS  the one-instruction form (__bfa_mem_access then picks
;                      the instruction, and refuses a width it has none for)
;   BFA_ORDER_MAIOR    byte by byte, the most significant byte first
;   BFA_ORDER_MINOR    byte by byte, the least significant byte first
; Dies on `maior`/`minor` of anything but a uN/iN.
proc __bfa_mem_plan, module, ty, ord
	locals
		slot width, dd
		slot kind, dd
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfa_ty_info
	mov	[width], eax
	mov	[kind], edx
	cmp	edx, BFA_TK_U
	je	.isint
	cmp	edx, BFA_TK_I
	je	.isint
	cmp	dword [ord], BFA_ORDER_NATIVUS
	je	.native
	jmp	.after_msg
  .msg db "load/store: `maior`/`minor` order an integer's bytes; this is not a uN/iN"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .isint:
	test	dword [width], 7
	jnz	.native			; not whole bytes: __bfa_mem_access refuses
	cmp	dword [width], 8
	je	.native			; one byte has no order
	cmp	dword [ord], BFA_ORDER_NATIVUS
	jne	.ordered
	cmp	dword [width], 16
	je	.native
	cmp	dword [width], 32
	je	.native
	cmp	dword [width], 64
	je	.native
	mov	eax, BFA_ORDER_MINOR	; nativus u24/u40/u48/u56: little-endian
	jmp	.nbytes
  .ordered:
	mov	eax, [ord]
	jmp	.nbytes
  .native:
	mov	eax, BFA_ORDER_NATIVUS
  .nbytes:
	mov	edx, [width]
	shr	edx, 3
	return
endp

; __bfa_mem_access(module, ty) -> eax = 0 for the 64-bit access Tier 2
; always had (any 64-bit type -- floats included, as bit baggage), else
; 8/16/32 for a `uN`/`iN`/`f32` of that width, with edx = 1 iff it is an
; `iN`. Only the one-instruction form reaches it (__bfa_mem_plan sends
; u24/u40/u48/u56 and every ordered access to the byte-by-byte form), so
; what dies here is a width that is not a whole number of bytes -- a bit
; field, which is `loadbits`/`storebits`.
proc __bfa_mem_access, module, ty
	locals
		slot kind, dd
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfa_ty_info
	mov	[kind], edx
	cmp	eax, 64
	jne	.narrow
	xor	eax, eax
	return
  .narrow:
	; An ADDRESS narrower than 64 gets its own refusal, because the
	; message below is factually wrong about it -- 32 IS a whole number
	; of bytes, four of them -- and that wrong message is what a reader
	; would otherwise be handed. ADR 0015 decision 2 interns `ptr`,
	; `ref` and `refc` at 64 on EVERY `--hospes` row, so this cannot
	; arrive from the lowering; it is hand-written IR, or a future row
	; that changed the intern without teaching this backend to lower it.
	; Refusing by name is what makes that change safe rather than merely
	; possible (docs/design/c-backend.md finding 18, third bullet, which
	; called this a silent miscompile -- it is not silent, it just said
	; the wrong thing).
	cmp	edx, BFA_TK_F64
	je	.f64
	cmp	edx, BFA_TK_F32
	je	.f32
	cmp	edx, BFA_TK_PTR
	je	.addr
	cmp	edx, BFA_TK_REF
	je	.addr
	cmp	edx, BFA_TK_REFC
	je	.addr
	cmp	edx, BFA_TK_U
	je	.isint
	cmp	edx, BFA_TK_I
	jne	.bad
  .isint:
	cmp	eax, 8
	je	.ok
	cmp	eax, 16
	je	.ok
	cmp	eax, 32
	je	.ok
  .f64:
	; A float in memory is BAGGAGE to this path: bits, not arithmetic.
	; f64 takes the 64-bit form and f32 the u32 one; the integer movs the
	; callers emit keep NaN payloads, -0.0 and subnormals exactly (no FP
	; unit is involved, so no exception is even possible), and the slot
	; layout floats already have -- fconst's zero-extended f32, the
	; qword f64 -- is the one this loads and stores. `maior`/`minor` on
	; a float still dies in __bfa_mem_plan: byte order is an integer
	; surface (spec 5.2), and a float has no byte-order spelling.
	xor	eax, eax
	xor	edx, edx
	return
  .f32:
	mov	eax, 32
	xor	edx, edx
	return
  .bad:
	jmp	.after_msg
  .msg db "load/store: a width that is not a whole number of bytes (a bit field is `loadbits`/`storebits`)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .addr:
	jmp	.after_amsg
  .amsg db "load/store: an address narrower than 64 bits. This backend is x86-64 only -- spec 9.5 fixes --hospes before any IR reaches a backend, and ADR 0015 interns ptr/ref/refc at 64 on every row, so the C backend is where a 32-bit-address target is served."
  .after_amsg:
	lea	rdi, [.amsg]
	mov	rsi, .after_amsg - .amsg
	call	__bfa_emit_die
  .ok:
	xor	edx, edx
	cmp	dword [kind], BFA_TK_I
	sete	dl
	return
endp

; __bfa_emit_byteref(ctx, off) -- "byte [rax+OFF]".
proc __bfa_emit_byteref, ctx, off
	locals
	endl
	__bfa_emtxt "byte [rax+"
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	__bfa_emtxt "]"
	return
endp

; __bfa_byte_addr(off, n, ord, k) -> eax = the offset of the byte of
; significance k (0 = least significant) in an n-byte access at `off` in
; order `ord`: off + n-1-k for `maior`, off + k for `minor`.
proc __bfa_byte_addr, off, n, ord, k
	locals
	endl
	mov	eax, [off]
	cmp	dword [ord], BFA_ORDER_MAIOR
	jne	.lsbfirst
	add	eax, [n]
	dec	eax
	sub	eax, [k]
	return
  .lsbfirst:
	add	eax, [k]
	return
endp

; __bfa_emit_load_bytes(ctx, vp, off, ord, n) -- the byte-by-byte load's
; body: the pointer into rax, then the n bytes assembled into rcx, most
; significant first. The caller normalises an `iN` and stores rcx.
proc __bfa_emit_load_bytes, ctx, vp, off, ord, n
	locals
		slot k, dd
	endl
	mov	rdi, [ctx]
	mov	esi, [vp]
	call	__bfa_out_movq_rax_slot		; mov rax, [rbp-8p]
	mov	eax, [n]
	dec	eax
	mov	[k], eax
	__bfa_emtxt 9, "movzx", 9, "ecx, "
	jmp	.addr
  .next:
	cmp	dword [k], 0
	je	.done
	dec	dword [k]
	__bfa_emtxt 9, "shl", 9, "rcx, 8", 10, 9, "movzx", 9, "edx, "
  .addr:
	mov	edi, [off]
	mov	esi, [n]
	mov	edx, [ord]
	mov	ecx, [k]
	call	__bfa_byte_addr
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_emit_byteref
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	eax, [n]
	dec	eax
	cmp	eax, [k]
	je	.next				; the first byte has nothing to `or` into
	__bfa_emtxt 9, "or", 9, "rcx, rdx", 10
	jmp	.next
  .done:
	return
endp

; __bfa_emit_store_bytes(ctx, vp, off, ord, n, vv) -- the byte-by-byte
; store: the pointer into rax, the value into rcx, then n bytes written
; least significant first, shifting rcx down a byte between them.
proc __bfa_emit_store_bytes, ctx, vp, off, ord, n, vv
	locals
		slot k, dd
	endl
	mov	rdi, [ctx]
	mov	esi, [vp]
	call	__bfa_out_movq_rax_slot		; mov rax, [rbp-8p]
	__bfa_emtxt 9, "mov", 9, "rcx, "
	mov	rdi, [ctx]
	mov	esi, [vv]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	dword [k], 0
  .loop:
	__bfa_emtxt 9, "mov", 9
	mov	edi, [off]
	mov	esi, [n]
	mov	edx, [ord]
	mov	ecx, [k]
	call	__bfa_byte_addr
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_emit_byteref
	__bfa_emtxt ", cl", 10
	inc	dword [k]
	mov	eax, [k]
	cmp	eax, [n]
	jae	.done
	__bfa_emtxt 9, "shr", 9, "rcx, 8", 10
	jmp	.loop
  .done:
	return
endp

; __bfa_emit_load(ctx, module, func, instid) -- `%v = load T %p off order`.
; The one-instruction form: at 64 bits (`ptr`, `u64`, `i64`) one `mov`; a
; `u8`/`u16`/`u32` is `movzx`/`mov r32` (zero-extending) and an `i8`/`i16`
; /`i32` `movsx`/`movsxd`, which is the canonical form straight from
; memory. Everything else is the byte-by-byte form (see this section's
; header).
proc __bfa_emit_load, ctx, module, func, instid
	locals
		slot eidx, dd
		slot vp, dd
		slot off, dd
		slot mw, dd
		slot msigned, dd
		slot ordv, dd
		slot n, dd
		slot ty, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[ty], ecx

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
	call	bfa_extra_get
	mov	[ordv], eax

	mov	rdi, [module]
	mov	esi, [ty]
	mov	edx, [ordv]
	call	__bfa_mem_plan
	mov	[ordv], eax
	mov	[n], edx
	cmp	eax, BFA_ORDER_NATIVUS
	jne	.bytewise

	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfa_mem_access
	mov	[mw], eax
	mov	[msigned], edx

	mov	rdi, [ctx]
	mov	esi, [vp]
	call	__bfa_out_movq_rax_slot		; mov rax, [rbp-8p]

	mov	eax, [mw]
	test	eax, eax
	jnz	.narrowld
	mov	rdi, [ctx]
	jmp	.after_deref
  .lit_deref db "	mov	rax, qword [rax+"
  .after_deref:
	lea	rsi, [.lit_deref]
	mov	rdx, .after_deref - .lit_deref
	call	__bfa_out_lit
	jmp	.offset
  .narrowld:
	cmp	dword [msigned], 0
	jne	.sld
	cmp	eax, 8
	jne	.u16
	__bfa_emtxt 9, "movzx", 9, "eax, byte [rax+"
	jmp	.offset
  .u16:
	cmp	eax, 16
	jne	.u32
	__bfa_emtxt 9, "movzx", 9, "eax, word [rax+"
	jmp	.offset
  .u32:
	__bfa_emtxt 9, "mov", 9, "eax, dword [rax+"
	jmp	.offset
  .sld:
	cmp	eax, 8
	jne	.i16
	__bfa_emtxt 9, "movsx", 9, "rax, byte [rax+"
	jmp	.offset
  .i16:
	cmp	eax, 16
	jne	.i32
	__bfa_emtxt 9, "movsx", 9, "rax, word [rax+"
	jmp	.offset
  .i32:
	__bfa_emtxt 9, "movsxd", 9, "rax, dword [rax+"
  .offset:
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

  .bytewise:
	mov	rdi, [ctx]
	mov	esi, [vp]
	mov	edx, [off]
	mov	ecx, [ordv]
	mov	r8d, [n]
	call	__bfa_emit_load_bytes
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfa_ty_is_signed
	test	eax, eax
	jz	.canonical			; a uN: zero-extended as assembled
	mov	rdi, [ctx]
	mov	esi, 1				; rcx
	mov	edx, [n]
	shl	edx, 3
	mov	ecx, 1
	call	__bfa_emit_norm_bits		; nothing at 64 bits
  .canonical:
	__bfa_emtxt 9, "mov", 9, "qword "
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot
	__bfa_emtxt ", rcx", 10
	return
endp

; __bfa_emit_store(ctx, module, func, instid) -- `store T %p off order %v`.
; A narrow one-instruction store writes the low 1, 2 or 4 bytes of the
; value's slot, which for a canonical value are the whole value; the
; byte-by-byte form writes the low n bytes one at a time.
proc __bfa_emit_store, ctx, module, func, instid
	locals
		slot eidx, dd
		slot vp, dd
		slot off, dd
		slot vv, dd
		slot mw, dd
		slot ordv, dd
		slot n, dd
		slot ty, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[ty], ecx

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
	call	bfa_extra_get
	mov	[ordv], eax
	mov	rdi, [func]
	mov	eax, [eidx]
	add	eax, 3
	mov	esi, eax
	call	bfa_extra_get
	mov	[vv], eax

	mov	rdi, [module]
	mov	esi, [ty]
	mov	edx, [ordv]
	call	__bfa_mem_plan
	mov	[ordv], eax
	mov	[n], edx
	cmp	eax, BFA_ORDER_NATIVUS
	je	.onestore
	mov	rdi, [ctx]
	mov	esi, [vp]
	mov	edx, [off]
	mov	ecx, [ordv]
	mov	r8d, [n]
	mov	r9d, [vv]
	call	__bfa_emit_store_bytes
	return

  .onestore:
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfa_mem_access
	mov	[mw], eax

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

	cmp	dword [mw], 0
	jne	.narrowst
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

  .narrowst:
	cmp	dword [mw], 8
	jne	.w16
	__bfa_emtxt 9, "mov", 9, "byte [rax+"
	jmp	.stoff
  .w16:
	cmp	dword [mw], 16
	jne	.w32
	__bfa_emtxt 9, "mov", 9, "word [rax+"
	jmp	.stoff
  .w32:
	__bfa_emtxt 9, "mov", 9, "dword [rax+"
  .stoff:
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	cmp	dword [mw], 8
	jne	.r16
	__bfa_emtxt "], cl", 10
	return
  .r16:
	cmp	dword [mw], 16
	jne	.r32
	__bfa_emtxt "], cx", 10
	return
  .r32:
	__bfa_emtxt "], ecx", 10
	return
endp

; __bfa_bits_plan(module, func, instid) -> eax = S (the field's shift from
; the least significant end of its byte, 8 - bit - w), edx = the mask
; 2^w - 1. Dies on what this emitter does not lower: a width outside 1..8
; (the verifier's rule 1 refuses it first), an `iN` field, a bit offset
; past 7, and a field that straddles a byte boundary.
proc __bfa_bits_plan, module, func, instid
	locals
		slot width, dd
		slot eidx, dd
		slot bio, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	rdi, [module]
	mov	esi, ecx
	call	__bfa_ty_info
	mov	[width], eax
	cmp	edx, BFA_TK_U
	jne	.notu
	cmp	eax, 1
	jb	.badw
	cmp	eax, 8
	ja	.badw
	mov	rdi, [func]
	mov	esi, [eidx]
	add	esi, 2
	call	bfa_extra_get
	mov	[bio], eax
	add	eax, [width]
	cmp	eax, 8
	ja	.straddle
	mov	eax, 8
	sub	eax, [bio]
	sub	eax, [width]			; S
	mov	ecx, [width]
	mov	edx, 1
	shl	edx, cl
	dec	edx				; 2^w - 1
	return
  .notu:
	jmp	.after_m1
  .m1 db "loadbits/storebits: a signed (iN) bit field is not implemented (spec 5.2: wire fields are unsigned)"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfa_emit_die
  .badw:
	jmp	.after_m2
  .m2 db "loadbits/storebits: a bit field is 1 to 8 bits wide"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfa_emit_die
  .straddle:
	jmp	.after_m3
  .m3 db "loadbits/storebits: a field that straddles a byte boundary is not implemented (DeModFrame never straddles)"
  .after_m3:
	lea	rdi, [.m3]
	mov	rsi, .after_m3 - .m3
	call	__bfa_emit_die
endp

; __bfa_emit_loadbits(ctx, module, func, instid) -- `%v = loadbits T %p
; byte bit`, MSB-first (this section's header).
proc __bfa_emit_loadbits, ctx, module, func, instid
	locals
		slot shamt, dd
		slot bfmask, dd
		slot vp, dd
		slot byo, dd
		slot eidx, dd
	endl
	mov	rdi, [module]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfa_bits_plan
	mov	[shamt], eax
	mov	[bfmask], edx
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[vp], eax
	mov	rdi, [func]
	mov	esi, [eidx]
	inc	esi
	call	bfa_extra_get
	mov	[byo], eax

	mov	rdi, [ctx]
	mov	esi, [vp]
	call	__bfa_out_movq_rax_slot		; mov rax, [rbp-8p]
	__bfa_emtxt 9, "movzx", 9, "eax, "
	mov	rdi, [ctx]
	mov	esi, [byo]
	call	__bfa_emit_byteref
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	cmp	dword [shamt], 0
	je	.noshift
	__bfa_emtxt 9, "shr", 9, "eax, "
	mov	rdi, [ctx]
	mov	esi, [shamt]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
  .noshift:
	__bfa_emtxt 9, "and", 9, "eax, "
	mov	rdi, [ctx]
	mov	esi, [bfmask]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_storebits(ctx, module, func, instid) -- `storebits T %p byte
; bit %v`: read the byte, clear the field's bits, or in the value shifted
; to them, write the byte back (this section's header).
proc __bfa_emit_storebits, ctx, module, func, instid
	locals
		slot shamt, dd
		slot bfmask, dd
		slot vp, dd
		slot byo, dd
		slot vv, dd
		slot eidx, dd
	endl
	mov	rdi, [module]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfa_bits_plan
	mov	[shamt], eax
	mov	[bfmask], edx
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[vp], eax
	mov	rdi, [func]
	mov	esi, [eidx]
	inc	esi
	call	bfa_extra_get
	mov	[byo], eax
	mov	rdi, [func]
	mov	esi, [eidx]
	add	esi, 3
	call	bfa_extra_get
	mov	[vv], eax

	mov	rdi, [ctx]
	mov	esi, [vp]
	call	__bfa_out_movq_rax_slot		; mov rax, [rbp-8p]
	__bfa_emtxt 9, "mov", 9, "rcx, "
	mov	rdi, [ctx]
	mov	esi, [vv]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "and", 9, "ecx, "
	mov	rdi, [ctx]
	mov	esi, [bfmask]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	cmp	dword [shamt], 0
	je	.noshift
	__bfa_emtxt 9, "shl", 9, "ecx, "
	mov	rdi, [ctx]
	mov	esi, [shamt]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
  .noshift:
	__bfa_emtxt 9, "movzx", 9, "edx, "
	mov	rdi, [ctx]
	mov	esi, [byo]
	call	__bfa_emit_byteref
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "and", 9, "edx, "
	mov	eax, [bfmask]
	mov	ecx, [shamt]
	shl	eax, cl
	not	eax
	and	eax, 0xFF			; the bits of the byte that are kept
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "or", 9, "edx, ecx", 10, 9, "mov", 9
	mov	rdi, [ctx]
	mov	esi, [byo]
	call	__bfa_emit_byteref
	__bfa_emtxt ", dl", 10
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
; immediately before the call -- the register chosen by CLASS, exactly as
; `__bfa_emit_param` chooses on the callee side: an F32/F64 argument goes
; to xmm0..xmm7 by its own SSE ordinal, every other type to rdi..r9 by its
; integer ordinal (asm-conventions.md 1.1). The result (if the call is
; not `void`) is stored straight back into the result's slot: no register
; allocation, exactly the Tier-1 contract. For a well-formed module the
; argument's own type and the callee's signature parameter agree, which
; is why the two walks are two views of one rule; the verifier does not
; cross-check them, so a mismatched call is caught here only by the
; register budget -- the IR text is the contract.
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
		slot iord, dd
		slot xord, dd
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
	mov	dword [iord], 0
	mov	dword [xord], 0
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

	; classify the argument by its own value type
	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_value_type
	cmp	eax, BFA_VALUE_TYPE_IS_U1
	je	.argint
	mov	rdi, [module]
	mov	esi, eax
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_F32
	je	.argflt
	cmp	ecx, BFA_TK_F64
	je	.argflt
	jmp	.argint
  .argflt:
	cmp	ecx, BFA_TK_F64
	jne	.argf32
	__bfa_emtxt 9, "movsd", 9
	mov	rdi, [ctx]
	mov	esi, [xord]
	call	__bfa_emit_argxmm
	__bfa_emtxt ", qword "
	jmp	.argslot
  .argf32:
	__bfa_emtxt 9, "movss", 9
	mov	rdi, [ctx]
	mov	esi, [xord]
	call	__bfa_emit_argxmm
	__bfa_emtxt ", dword "
  .argslot:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	eax, [xord]
	inc	eax
	mov	[xord], eax
	jmp	.nextarg
  .argint:
	__bfa_emtxt 9, "mov", 9
	mov	rdi, [ctx]
	mov	esi, [iord]
	call	__bfa_emit_argreg
	__bfa_emtxt ", "
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	eax, [iord]
	inc	eax
	mov	[iord], eax
  .nextarg:
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

	; A FLOAT RESULT rides in xmm0 (asm-conventions.md 1.1), moved through
	; rax and the same canonicalising store every float result takes:
	; `movq`/`movd` yield exactly the value's bits, so unlike a narrow
	; INTEGER result from a foreign object there is nothing above the
	; value's width left unspecified, and the externus normalisation is
	; integer-only.
	mov	rdi, [module]
	mov	esi, [tyv]
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_F32
	je	.resf32
	cmp	ecx, BFA_TK_F64
	je	.resf64
	jmp	.resint
  .resf32:
	__bfa_emtxt 9, "movd", 9, "eax, xmm0", 10
	jmp	.tostore
  .resf64:
	__bfa_emtxt 9, "movq", 9, "rax, xmm0", 10
	jmp	.tostore
  .resint:
	; A NARROW RESULT FROM ANOTHER OBJECT is normalised before it is
	; stored: SysV AMD64 leaves the bits of `rax` above a `u8`/`u16`/`u32`
	; return value unspecified (C compilers extend to 32 bits by habit,
	; not by rule), and every instruction after this one assumes the
	; canonical form. Only an `externus` callee can hand one back: every
	; other callee is this emitter's own output, whose `ret` loads a slot
	; that is canonical already. For the same reason a `param` is NOT
	; normalised on entry -- every caller of an emitted function is emitted
	; code passing canonical slots, or the prelude's `exsrt_start`, which
	; passes `initium` one `ptr`; nothing foreign can reach a `param`,
	; because an `externus` function is a declaration here and never a
	; definition (bfa_emit_module skips its body).
	mov	rdi, [func]
	mov	esi, [eidx]
	call	bfa_extra_get			; eax = 1-based callee index
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, eax
	dec	esi
	call	vec_get
	mov	rax, [rax]
	test	dword [rax + BfaFunc.attrs], BFA_FUNC_ATTR_EXTERNUS
	jz	.tostore
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	xor	ecx, ecx
	call	__bfa_emit_norm
  .tostore:
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

; ============================================================================
; Saturating, bitwise, shifts, conversions (wire-codec M3)
; ============================================================================

; __bfa_emit_sat(ctx, module, func, instid) -- adds/subs, `+|`/`-|`:
; clamped to T (ssa-ir.md 2.3). Branch-free, through `cmov`:
;
;   uN, N < 64   the exact 64-bit sum or difference (|a|, |b| < 2^63, so
;                neither carries), then: adds `mov rcx, 2^N-1` / `cmp rax,
;                rcx` / `cmova rax, rcx`; subs `xor ecx, ecx` / `test rax,
;                rax` / `cmovs rax, rcx` -- a borrow is a negative result
;   iN, N < 64   the exact result, then `cmovg` down to 2^(N-1)-1 and
;                `cmovl` up to -2^(N-1)
;   u64          the carry is the whole story: adds `sbb rcx, rcx` / `or
;                rax, rcx` (all ones on a carry), subs `sbb rcx, rcx` / `not
;                rcx` / `and rax, rcx` (zero on a borrow)
;   i64          an overflowing `a + b` or `a - b` always leaves on the side
;                of `a`'s sign, so the clamp, (a sar 63) xor (2^63-1), is
;                computed BEFORE the op and taken with `cmovo`
;
; `muls` stays refused by the dispatcher: spec 5.4 has no `*|` yet
; (`*%`/`*|` are still `[OPEN]` in 8.6's list), so nothing lowers to it.
proc __bfa_emit_sat, ctx, module, func, instid
	locals
		slot av, dd
		slot bv, dd
		slot auxv, dd
		slot tyv, dd
		slot issub, dd
		slot issigned, dd
		slot nbits, dd
		slot lim, dq
	endl
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
	xor	edx, edx
	cmp	ecx, BFA_OP_SUBS
	sete	dl
	mov	[issub], edx

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_int_width
	mov	[nbits], eax
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_ty_is_signed
	mov	[issigned], eax
	mov	rdi, [module]
	mov	esi, [tyv]
	mov	edx, [av]
	mov	ecx, [bv]
	mov	r8d, [auxv]
	call	__bfa_check_opimms

	mov	rdi, [ctx]
	mov	esi, [av]
	mov	edx, [auxv]
	and	edx, 0x100
	call	__bfa_emit_load_rax

	cmp	dword [nbits], 64
	jne	.noprep
	cmp	dword [issigned], 0
	je	.noprep
	__bfa_emtxt 9, "mov", 9, "rcx, rax", 10, 9, "sar", 9, "rcx, 63", 10
	__bfa_emtxt 9, "mov", 9, "rdx, 9223372036854775807", 10, 9, "xor", 9, "rcx, rdx", 10
  .noprep:

	cmp	dword [issub], 0
	jne	.dosub
	__bfa_emtxt 9, "add", 9, "rax, "
	jmp	.opnd
  .dosub:
	__bfa_emtxt 9, "sub", 9, "rax, "
  .opnd:
	mov	rdi, [ctx]
	mov	esi, [bv]
	mov	edx, [auxv]
	and	edx, 0x200
	call	__bfa_emit_operand_text
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	cmp	dword [nbits], 64
	jne	.narrow
	cmp	dword [issigned], 0
	jne	.s64
	cmp	dword [issub], 0
	jne	.u64sub
	__bfa_emtxt 9, "sbb", 9, "rcx, rcx", 10, 9, "or", 9, "rax, rcx", 10
	jmp	.tostore
  .u64sub:
	__bfa_emtxt 9, "sbb", 9, "rcx, rcx", 10, 9, "not", 9, "rcx", 10, 9, "and", 9, "rax, rcx", 10
	jmp	.tostore
  .s64:
	__bfa_emtxt 9, "cmovo", 9, "rax, rcx", 10
	jmp	.tostore

  .narrow:
	cmp	dword [issigned], 0
	jne	.snarrow
	cmp	dword [issub], 0
	jne	.unsub
	mov	ecx, [nbits]
	mov	eax, 1
	shl	rax, cl
	dec	rax
	mov	[lim], rax
	__bfa_emtxt 9, "mov", 9, "rcx, "
	mov	rdi, [ctx]
	mov	rsi, [lim]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "cmp", 9, "rax, rcx", 10, 9, "cmova", 9, "rax, rcx", 10
	jmp	.tostore
  .unsub:
	__bfa_emtxt 9, "xor", 9, "ecx, ecx", 10, 9, "test", 9, "rax, rax", 10
	__bfa_emtxt 9, "cmovs", 9, "rax, rcx", 10
	jmp	.tostore
  .snarrow:
	mov	ecx, [nbits]
	dec	ecx
	mov	eax, 1
	shl	rax, cl
	mov	[lim], rax			; 2^(N-1)
	__bfa_emtxt 9, "mov", 9, "rcx, "
	mov	rdi, [ctx]
	mov	rsi, [lim]
	dec	rsi
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "cmp", 9, "rax, rcx", 10, 9, "cmovg", 9, "rax, rcx", 10
	__bfa_emtxt 9, "mov", 9, "rcx, "
	mov	rdi, [ctx]
	mov	rsi, [lim]
	neg	rsi
	call	__bfa_out_i64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "cmp", 9, "rax, rcx", 10, 9, "cmovl", 9, "rax, rcx", 10
  .tostore:
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_bitwise(ctx, module, func, instid) -- and/or/xor, any width.
; Canonical operands give a canonical result with no normalisation: for a
; `uN` both have zeros above bit N-1, and for an `iN` both have copies of
; bit N-1 there, so the result's upper bits are the op applied to copies of
; its own bit N-1.
proc __bfa_emit_bitwise, ctx, module, func, instid
	locals
		slot av, dd
		slot bv, dd
		slot auxv, dd
		slot tyv, dd
		slot op, dd
	endl
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
	mov	[op], ecx

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_int_width
	mov	rdi, [module]
	mov	esi, [tyv]
	mov	edx, [av]
	mov	ecx, [bv]
	mov	r8d, [auxv]
	call	__bfa_check_opimms

	mov	rdi, [ctx]
	mov	esi, [av]
	mov	edx, [auxv]
	and	edx, 0x100
	call	__bfa_emit_load_rax
	cmp	dword [op], BFA_OP_AND
	jne	.notand
	__bfa_emtxt 9, "and", 9, "rax, "
	jmp	.opnd
  .notand:
	cmp	dword [op], BFA_OP_OR
	jne	.notor
	__bfa_emtxt 9, "or", 9, "rax, "
	jmp	.opnd
  .notor:
	__bfa_emtxt 9, "xor", 9, "rax, "
  .opnd:
	mov	rdi, [ctx]
	mov	esi, [bv]
	mov	edx, [auxv]
	and	edx, 0x200
	call	__bfa_emit_operand_text
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_shift(ctx, module, func, instid) -- shl/shr, `sursum`/
; `deorsum`. The count is the second operand, of the same type T (the
; verifier's rule 1 for the BIN shape). A count >= N traps (spec 5.4 as
; amended; ssa-ir.md 2.3), and the compare is UNSIGNED, so a negative `iN`
; count is a huge one and traps too:
;
;       mov     rcx, <count>
;       cmp     rcx, N
;       jae     bfausr_trap
;       shl     rax, cl                 ; shr for uN, sar for iN
;
; With the count below N the x86 masking of `cl` never engages, so the
; hardware's target-dependent answer (spec 5.4's reason for the trap) is
; never asked for. `shl` can push bits past N-1 and is normalised; `shr`
; and `sar` of a canonical value stay canonical.
proc __bfa_emit_shift, ctx, module, func, instid
	locals
		slot av, dd
		slot bv, dd
		slot auxv, dd
		slot tyv, dd
		slot op, dd
		slot nbits, dd
		slot issigned, dd
	endl
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
	mov	[op], ecx

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_int_width
	mov	[nbits], eax
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_ty_is_signed
	mov	[issigned], eax
	mov	rdi, [module]
	mov	esi, [tyv]
	mov	edx, [av]
	mov	ecx, [bv]
	mov	r8d, [auxv]
	call	__bfa_check_opimms

	mov	rdi, [ctx]
	mov	esi, [av]
	mov	edx, [auxv]
	and	edx, 0x100
	call	__bfa_emit_load_rax
	__bfa_emtxt 9, "mov", 9, "rcx, "
	mov	rdi, [ctx]
	mov	esi, [bv]
	mov	edx, [auxv]
	and	edx, 0x200
	call	__bfa_emit_operand_text
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "cmp", 9, "rcx, "
	mov	rdi, [ctx]
	mov	esi, [nbits]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "jae", 9, "bfausr_trap", 10

	cmp	dword [op], BFA_OP_SHL
	jne	.right
	__bfa_emtxt 9, "shl", 9, "rax, cl", 10
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	xor	ecx, ecx
	call	__bfa_emit_norm
	jmp	.tostore
  .right:
	cmp	dword [issigned], 0
	jne	.dosar
	__bfa_emtxt 9, "shr", 9, "rax, cl", 10
	jmp	.tostore
  .dosar:
	__bfa_emtxt 9, "sar", 9, "rax, cl", 10
  .tostore:
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_conv(ctx, module, func, instid) -- zext/sext/trunc. `T` is the
; DESTINATION; the source type is the operand's own (verify.inc FINDING 3).
; A copy, then:
;   zext of an iN source   zero-extend from the source's N first (the
;                          canonical iN has copies of its sign bit there)
;   sext of a uN source    sign-extend from the source's N first
;   every case             normalise to T -- a no-op for a widening, the
;                          truncation itself for `trunc`
; so `zext u16` of i8 -56 is 200, `sext u16` of it 65480, `trunc u8` of
; u32 300 is 44, and `trunc i8` of u8 200 is -56 (lower/expr.inc's
; `__lwr_cast` emits `trunc` for an equal-width sign change).
proc __bfa_emit_conv, ctx, module, func, instid
	locals
		slot av, dd
		slot tyv, dd
		slot op, dd
		slot sty, dd
		slot sbits, dd
		slot skind, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx

	mov	rdi, [func]
	mov	esi, [av]
	call	bfa_value_type
	cmp	eax, BFA_VALUE_TYPE_IS_U1
	jne	.havesty
	mov	rdi, [module]
	call	bfa_ty_u1
  .havesty:
	mov	[sty], eax
	mov	rdi, [module]
	mov	esi, [sty]
	call	__bfa_ty_info
	mov	[sbits], eax
	mov	[skind], edx
	cmp	edx, BFA_TK_U
	je	.srcok
	cmp	edx, BFA_TK_I
	jne	.bad
  .srcok:
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfa_ty_info
	cmp	edx, BFA_TK_U
	je	.dstok
	cmp	edx, BFA_TK_I
	je	.dstok
  .bad:
	jmp	.after_msg
  .msg db "zext/sext/trunc: integer (uN/iN) source and destination only"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .dstok:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfa_out_movq_rax_slot

	cmp	dword [op], BFA_OP_ZEXT
	jne	.notzext
	cmp	dword [skind], BFA_TK_I
	jne	.todest
	mov	rdi, [ctx]
	xor	esi, esi
	mov	edx, [sbits]
	xor	ecx, ecx
	call	__bfa_emit_norm_bits
	jmp	.todest
  .notzext:
	cmp	dword [op], BFA_OP_SEXT
	jne	.todest
	cmp	dword [skind], BFA_TK_U
	jne	.todest
	mov	rdi, [ctx]
	xor	esi, esi
	mov	edx, [sbits]
	mov	ecx, 1
	call	__bfa_emit_norm_bits
  .todest:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	xor	ecx, ecx
	call	__bfa_emit_norm
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; ============================================================================
; Arrays: index, chk, copy, addr (ssa-ir.md 2.7; wire-codec M3)
; ============================================================================

; __bfa_check_imm31(v) -- dies unless `v` (an unsigned 32-bit field) fits a
; sign-extended 32-bit immediate, which is what `add rax, v` and `imul rcx,
; rcx, v` take. 2 GiB strides and offsets are refused by name.
proc __bfa_check_imm31, v
	locals
	endl
	cmp	dword [v], 0x7FFFFFFF
	jbe	.ok
	jmp	.after_msg
  .msg db "index/addr: a stride or offset of 2^31 or more"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .ok:
	return
endp

; __bfa_emit_index(ctx, module, func, instid) -- `%e = index %p %i stride`:
; %p + %i * stride. The index is canonical, so a signed one is already
; sign-extended and `imul` is right for it too; bounds are `chk`'s, before.
proc __bfa_emit_index, ctx, module, func, instid
	locals
		slot eidx, dd
		slot vp, dd
		slot vi, dd
		slot stride, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[vp], eax
	mov	rdi, [func]
	mov	esi, [eidx]
	inc	esi
	call	bfa_extra_get
	mov	[vi], eax
	mov	rdi, [func]
	mov	esi, [eidx]
	add	esi, 2
	call	bfa_extra_get
	mov	[stride], eax
	mov	edi, [stride]
	call	__bfa_check_imm31

	mov	rdi, [ctx]
	mov	esi, [vp]
	call	__bfa_out_movq_rax_slot
	__bfa_emtxt 9, "mov", 9, "rcx, "
	mov	rdi, [ctx]
	mov	esi, [vi]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "imul", 9, "rcx, rcx, "
	mov	rdi, [ctx]
	mov	esi, [stride]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "add", 9, "rax, rcx", 10
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
endp

; __bfa_emit_chk(ctx, func, instid) -- `chk %i %n`: trap unless %i < %n,
; compared UNSIGNED (`jae`), so a negative `iN` index is out of bounds
; rather than below them. Kind 1, "failed `chk`" (prelude/README.md).
proc __bfa_emit_chk, ctx, func, instid
	locals
		slot vi, dd
		slot vn, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[vi], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[vn], ecx
	mov	rdi, [ctx]
	mov	esi, [vi]
	call	__bfa_out_movq_rax_slot
	__bfa_emtxt 9, "cmp", 9, "rax, "
	mov	rdi, [ctx]
	mov	esi, [vn]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "jae", 9, "bfausr_trap", 10
	return
endp

; __bfa_emit_copy_chunk(ctx, off, width) -- one step of `copy`: `width`
; bytes (8, 4, 2 or 1) at `off`, from [rdx] to [rax], through rcx.
proc __bfa_emit_copy_chunk, ctx, off, width
	locals
	endl
	cmp	dword [width], 8
	jne	.n8a
	__bfa_emtxt 9, "mov", 9, "rcx, qword [rdx+"
	jmp	.oa
  .n8a:
	cmp	dword [width], 4
	jne	.n4a
	__bfa_emtxt 9, "mov", 9, "ecx, dword [rdx+"
	jmp	.oa
  .n4a:
	cmp	dword [width], 2
	jne	.n2a
	__bfa_emtxt 9, "mov", 9, "cx, word [rdx+"
	jmp	.oa
  .n2a:
	__bfa_emtxt 9, "mov", 9, "cl, byte [rdx+"
  .oa:
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	__bfa_emtxt "]", 10
	cmp	dword [width], 8
	jne	.n8b
	__bfa_emtxt 9, "mov", 9, "qword [rax+"
	jmp	.ob
  .n8b:
	cmp	dword [width], 4
	jne	.n4b
	__bfa_emtxt 9, "mov", 9, "dword [rax+"
	jmp	.ob
  .n4b:
	cmp	dword [width], 2
	jne	.n2b
	__bfa_emtxt 9, "mov", 9, "word [rax+"
	jmp	.ob
  .n2b:
	__bfa_emtxt 9, "mov", 9, "byte [rax+"
  .ob:
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	cmp	dword [width], 8
	jne	.n8c
	__bfa_emtxt "], rcx", 10
	return
  .n8c:
	cmp	dword [width], 4
	jne	.n4c
	__bfa_emtxt "], ecx", 10
	return
  .n4c:
	cmp	dword [width], 2
	jne	.n2c
	__bfa_emtxt "], cx", 10
	return
  .n2c:
	__bfa_emtxt "], cl", 10
	return
endp

; __bfa_emit_cpylabel(ctx, lblptr, lbllen, instid) -- `<prefix>c<instid>`,
; no newline: the one spelling of a large `copy`'s loop label, for its
; definition and for the `jnz` back to it. Deliberately shaped like
; __bfa_emit_blklabel, which is the same idea for a block.
proc __bfa_emit_cpylabel, ctx, lblptr, lbllen, instid
	locals
	endl
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	call	__bfa_out_lit
	__bfa_emtxt "c"
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_u64
	return
endp

; BFA_COPY_UNROLL_MAX -- the largest `copy n` this file still unrolls, in
; BYTES. At or below it the emitted form is unchanged from what every
; existing fixture pins; above it the 8-byte steps become a runtime loop
; (__bfa_emit_copy, below, states the whole argument).
;
; WHY 128, three reasons, in the order they decided it:
;
;   1. It is five times past break-even in emitted TEXT. An unrolled 8-byte
;      step is two `mov` lines, ~59 bytes of fasmg source; the loop form is
;      a fixed six extra lines, ~135 bytes, and the same 4/2/1 tail. The
;      loop is the shorter text from about 24 bytes up. A threshold at
;      break-even would make every copy near it a coin flip; 128 is far
;      enough above that nothing sits on the fence.
;   2. It BOUNDS the text one `copy` can emit. 128 bytes is 16 steps, about
;      950 bytes of fasmg -- so no single sub-threshold `copy` can be a
;      meaningful fraction of the compilation arena, which is the failure
;      this threshold exists to remove (driver/io.inc's arena is
;      DRV_ARENA_FLOOR + DRV_ARENA_FACTOR x the SOURCE bytes, and a `copy`
;      is emitted text per BYTE COPIED, a quantity the source length does
;      not bound -- `acies<i64, 18000>` is six lines of source and 144,000
;      bytes of copy).
;   3. MEASURED against the tree as it stands: every `copy` any fixture or
;      program in this repository emits today is 40 bytes or smaller --
;      `copy 7` and `copy 17` in tests/unit and tests/ir, 16 bytes in
;      tests/programs/forma and hydramodem_basis, 40 in the three
;      hydramodem_* programs -- with exactly one exception, the 2,528-byte
;      aggregate copy in the five receptio_* receiver programs. So the
;      threshold leaves every fixture whose emitted text is pinned
;      byte-identical, and converts exactly the one case that had already
;      grown past what unrolling is for.
BFA_COPY_UNROLL_MAX = 128

; __bfa_emit_copy(ctx, func, instid, lblptr, lbllen) -- `copy n %d %s`: n
; bytes, destination in rax, source in rdx, every byte through rcx -- the
; three registers this file's header already calls scratch. Ascending
; offsets, so it is a memcpy: overlapping %d and %s are not given a meaning
; here, and ssa-ir.md 2.7 ("`copy n` is bytes only") gives them none
; either. n = 0 loads the two pointers and moves nothing.
;
; TWO FORMS, chosen by BFA_COPY_UNROLL_MAX (above):
;
;   n <= 128   unrolled at compile time into 8-byte steps and a 4/2/1
;              tail. Byte-for-byte what this routine has always emitted.
;   n >  128   the 8-byte steps become a runtime loop over n/8 iterations,
;              then the same 4/2/1 tail at offsets 0/4/6 from the pointers
;              the loop advanced. Nine lines of fasmg whatever n is.
;
; WHY THIS HAD TO CHANGE. Unrolling is ~7.35 bytes of fasmg text per byte
; copied, and a struct literal with a big `acies` field lowers to a
; temporary plus a `copy` of the whole field (lowering, `[e; N]` into an
; aggregate). Six lines of source carrying `acies<i64, 18000>` is a
; 144,000-byte `copy`, which is ~1.06 MB of emitted text -- and the
; compilation arena is sized from the SOURCE, so it ran out and
; `arena_alloc` trapped: SIGILL, exit 132, no message at all
; (docs/design/receptor.md finding 20, measured -- N = 17,000 compiled,
; N = 18,000 did not). The loop makes the emitted text O(1) in n.
;
; THE COUNTER IS IN MEMORY, AND THAT IS THE WHOLE TRICK. This backend keeps
; no value in a register across an instruction boundary, so the loop has
; only rax, rcx and rdx to work with -- and it needs all three for the
; destination pointer, the source pointer, and the 8 bytes in flight. The
; counter therefore lives in the `copy`'s OWN stack slot,
; `__bfa_slot(ctx, instid)`. That slot exists and is free: the prologue
; reserves one 8-byte slot per instruction id, "void ids included for
; simplicity" (this file's header, and __bfa_frame_layout's `ninsts + 1`),
; and `copy` is a void instruction, so nothing else ever writes or reads
; it. No new frame space, no new convention -- just the one slot the naive
; layout was already paying for.
;
; STILL NEVER `rep movsb`, and now for a checkable reason rather than a
; preference. `movsb` needs rdi and rsi, and those are SysV ARGUMENT
; registers that `param i` reads (__bfa_emit_param). Nothing in
; docs/design/ssa-ir.md, and nothing in verify.inc, requires a function's
; `param` instructions to come before every other instruction of its entry
; block -- so a `copy` may legally be emitted ahead of a `param` that still
; has to read `rdi`, and clobbering it would miscompile silently. The
; memory counter assumes nothing about instruction order.
;
; THE ABI IS UNTOUCHED. The loop makes no call, never moves `rsp`, never
; writes a callee-saved register and never touches the red zone; it writes
; rax, rcx, rdx and one frame slot, all of which this backend already
; treats as dead at an instruction boundary.
;
; THE LABEL is `<prefix>c<instid>`, built from the same per-function label
; prefix the block and edge labels use (__bfa_emit_blklabel: `<prefix><n>`,
; and `__bfa_emit_br`'s `<prefix><n>e<k>`). `instid` is unique within the
; function and `c` is not a digit, so it can collide with neither form.
proc __bfa_emit_copy, ctx, func, instid, lblptr, lbllen
	locals
		slot eidx, dd
		slot nbytes, dd
		slot vd, dd
		slot vs, dd
		slot off, dd
		slot rem, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[nbytes], eax
	mov	rdi, [func]
	mov	esi, [eidx]
	inc	esi
	call	bfa_extra_get
	mov	[vd], eax
	mov	rdi, [func]
	mov	esi, [eidx]
	add	esi, 2
	call	bfa_extra_get
	mov	[vs], eax

	mov	rdi, [ctx]
	mov	esi, [vd]
	call	__bfa_out_movq_rax_slot
	__bfa_emtxt 9, "mov", 9, "rdx, "
	mov	rdi, [ctx]
	mov	esi, [vs]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	cmp	dword [nbytes], BFA_COPY_UNROLL_MAX
	ja	.loopform

	mov	dword [off], 0
	mov	eax, [nbytes]
	mov	[rem], eax
  .q:
	cmp	dword [rem], 8
	jb	.tail4
	mov	rdi, [ctx]
	mov	esi, [off]
	mov	edx, 8
	call	__bfa_emit_copy_chunk
	add	dword [off], 8
	sub	dword [rem], 8
	jmp	.q

	; ---- the loop form, n > BFA_COPY_UNROLL_MAX ------------------------
	; rax and rdx are already loaded with the destination and the source.
  .loopform:
	__bfa_emtxt 9, "mov", 9, "qword "
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot			; the copy's own, unread slot
	__bfa_emtxt ", "
	mov	rdi, [ctx]
	mov	eax, [nbytes]
	shr	eax, 3				; whole 8-byte steps
	mov	esi, eax
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [instid]
	call	__bfa_emit_cpylabel
	__bfa_emtxt ":", 10
	__bfa_emtxt 9, "mov", 9, "rcx, qword [rdx]", 10
	__bfa_emtxt 9, "mov", 9, "qword [rax], rcx", 10
	__bfa_emtxt 9, "add", 9, "rax, 8", 10
	__bfa_emtxt 9, "add", 9, "rdx, 8", 10
	__bfa_emtxt 9, "dec", 9, "qword "
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_slot
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	__bfa_emtxt 9, "jnz", 9
	mov	rdi, [ctx]
	mov	rsi, [lblptr]
	mov	rdx, [lbllen]
	mov	ecx, [instid]
	call	__bfa_emit_cpylabel
	mov	rdi, [ctx]
	call	__bfa_emit_nl

	; The tail is the same 4/2/1 sequence, now at offsets 0/4/6 from the
	; pointers the loop left advanced -- which is why `off` restarts at 0
	; here and `rem` is what the loop did not move.
	mov	dword [off], 0
	mov	eax, [nbytes]
	and	eax, 7
	mov	[rem], eax

  .tail4:
	cmp	dword [rem], 4
	jb	.tail2
	mov	rdi, [ctx]
	mov	esi, [off]
	mov	edx, 4
	call	__bfa_emit_copy_chunk
	add	dword [off], 4
	sub	dword [rem], 4
  .tail2:
	cmp	dword [rem], 2
	jb	.tail1
	mov	rdi, [ctx]
	mov	esi, [off]
	mov	edx, 2
	call	__bfa_emit_copy_chunk
	add	dword [off], 2
	sub	dword [rem], 2
  .tail1:
	cmp	dword [rem], 1
	jb	.done
	mov	rdi, [ctx]
	mov	esi, [off]
	mov	edx, 1
	call	__bfa_emit_copy_chunk
  .done:
	return
endp

; __bfa_emit_addr(ctx, func, instid) -- `%q = addr %p off`: %p + off.
proc __bfa_emit_addr, ctx, func, instid
	locals
		slot vp, dd
		slot off, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[vp], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[off], ecx
	mov	edi, [off]
	call	__bfa_check_imm31
	mov	rdi, [ctx]
	mov	esi, [vp]
	call	__bfa_out_movq_rax_slot
	__bfa_emtxt 9, "add", 9, "rax, "
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfa_emit_nl
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_movq_slot_rax
	return
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
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
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
	jmp	.notcmp
  .docmp:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_cmp
	return
  .notcmp:
	cmp	dword [op], BFA_OP_FCMP
	jne	.notfcmp
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_fcmp
	return
  .notfcmp:
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
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
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
	cmp	dword [op], BFA_OP_LOADBITS
	jne	.notloadbits
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_loadbits
	return
  .notloadbits:
	cmp	dword [op], BFA_OP_STOREBITS
	jne	.notstorebits
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_storebits
	return
  .notstorebits:
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
	cmp	dword [op], BFA_OP_ADDS
	je	.dosat
	cmp	dword [op], BFA_OP_SUBS
	je	.dosat
	jmp	.notsat
  .dosat:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_sat
	return
  .notsat:
	cmp	dword [op], BFA_OP_AND
	je	.dobitw
	cmp	dword [op], BFA_OP_OR
	je	.dobitw
	cmp	dword [op], BFA_OP_XOR
	je	.dobitw
	jmp	.notbitw
  .dobitw:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_bitwise
	return
  .notbitw:
	cmp	dword [op], BFA_OP_SHL
	je	.doshift
	cmp	dword [op], BFA_OP_SHR
	je	.doshift
	jmp	.notshift
  .doshift:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_shift
	return
  .notshift:
	cmp	dword [op], BFA_OP_ZEXT
	je	.doconv
	cmp	dword [op], BFA_OP_SEXT
	je	.doconv
	cmp	dword [op], BFA_OP_TRUNC
	je	.doconv
	jmp	.notconv
  .doconv:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_conv
	return
  .notconv:
	cmp	dword [op], BFA_OP_FCONST
	jne	.notfconst
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_fconst
	return
  .notfconst:
	cmp	dword [op], BFA_OP_FADD
	je	.dofarith
	cmp	dword [op], BFA_OP_FSUB
	je	.dofarith
	cmp	dword [op], BFA_OP_FMUL
	je	.dofarith
	cmp	dword [op], BFA_OP_FDIV
	je	.dofarith
	jmp	.notfarith
  .dofarith:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_farith
	return
  .notfarith:
	cmp	dword [op], BFA_OP_FNEG
	jne	.notfneg
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_fneg
	return
  .notfneg:
	cmp	dword [op], BFA_OP_FEXT
	je	.dofconv
	cmp	dword [op], BFA_OP_FTRUNC
	je	.dofconv
	cmp	dword [op], BFA_OP_ITOF
	je	.dofconv
	cmp	dword [op], BFA_OP_FTOI
	je	.dofconv
	jmp	.notfconv
  .dofconv:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_fconv
	return
  .notfconv:
	; fma and bitcast are the two float opcodes with no lowering, and each
	; dies BY NAME here rather than through the generic arm at the bottom:
	; fma because the SSE2 baseline simply has no fused multiply-add
	; (FMA3 is a later extension -- ssa-ir.md 2.3 calls fma "the only
	; contraction that can exist", so the opcode is right and the silicon
	; is what is missing), bitcast because nothing in-tree produces it and
	; its exact semantics are unrecorded ([OPEN], ssa-ir.md 2.3) -- in the
	; slot model a pure copy would be a plausible guess, and a guess is
	; what this file must never emit.
	cmp	dword [op], BFA_OP_FMA
	jne	.notfma
	jmp	.after_fmamsg
  .fmamsg db "fma: the SSE2 baseline has no fused multiply-add (FMA3 is a later extension)"
  .after_fmamsg:
	lea	rdi, [.fmamsg]
	mov	rsi, .after_fmamsg - .fmamsg
	call	__bfa_emit_die
  .notfma:
	cmp	dword [op], BFA_OP_BITCAST
	jne	.notbitcast
	jmp	.after_bcmsg
  .bcmsg db "bitcast: nothing in-tree produces it, and its semantics are [OPEN] (ssa-ir.md 2.3)"
  .after_bcmsg:
	lea	rdi, [.bcmsg]
	mov	rsi, .after_bcmsg - .bcmsg
	call	__bfa_emit_die
  .notbitcast:
	cmp	dword [op], BFA_OP_INDEX
	jne	.notindex
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfa_emit_index
	return
  .notindex:
	cmp	dword [op], BFA_OP_CHK
	jne	.notchk
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfa_emit_chk
	return
  .notchk:
	cmp	dword [op], BFA_OP_COPY
	jne	.notcopy
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	mov	rcx, [lblptr]
	mov	r8, [lbllen]
	call	__bfa_emit_copy
	return
  .notcopy:
	cmp	dword [op], BFA_OP_ADDR
	jne	.notaddr
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfa_emit_addr
	return
  .notaddr:
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

	; The block's phis emit nothing here: each is only a destination slot,
	; filled by the parallel copy on every incoming edge, which the
	; predecessor's `jmp`/`br` emits -- and to do that it has to know which
	; predecessor it is, hence `curblk` ("phi -- parallel copies on edges",
	; above). The body list never contains a phi (ir.inc: two lists).
	mov	rax, [ctx]
	mov	ecx, [blkid]
	mov	[rax + BfaPrintCtx.curblk], ecx

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
