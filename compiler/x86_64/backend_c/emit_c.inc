; compiler/x86_64/backend_c/emit_c.inc
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
; The C emitter -- spec section 9.2's REACH backend, ADR 0012, and
; docs/design/c-backend.md (D1-D8), which is this file's specification.
; `bfc_emit_module` produces C SOURCE TEXT for one module's FUNCTIONS, in
; LIBRARY MODE (D1): a definition per `functio` with a body, an `extern`
; prototype per bodiless declaration, and nothing else -- no entry point, no
; runtime, no prelude, no capability gating, no ARC. The prologue, the
; `exsi_*` helpers and the `static const` globals are `program_c.inc`'s;
; this file emits the prototypes and the definitions.
;
; THE REFERENCE DEFINES THE SEMANTICS (spec section 9.2, ADR 0012). Nothing
; here lowers an opcode `backend_fasmg/emit.inc` does not lower: a C
; lowering the differential test cannot check is a hypothesis with no test.
; The reference's `__bfa_emit_inst` lowers 50 of the IR's 64 opcodes -- the
; 35 integer, memory and control ones it always lowered, plus the eleven
; float ones (fadd fsub fmul fdiv fneg fcmp fext ftrunc itof ftoi fconst)
; and the vector float group's four (vadd vsub vmul vdiv);
; this file lowers exactly those 50 and refuses the other 14 BY NAME through
; `__bfc_die` -- "bfc: emitter: ...", exit 4, the same class
; `__bfa_emit_die` is. (`nop`, opcode 0 outside the 1..64 numbering, is a
; 15th refusal; `phi` is lowered by the block walker and never dispatched.)
; The two emitters' refusal sets are ONE SET (spec 9.2): every message
; below names its reason the way the reference's refusal of the same opcode
; does, and a divergence between them is a finding, not a local choice.
;
; THE CARRIER TYPE (D4). Every integer value of any `uN` or `iN` is held in
; a C `uint64_t` in exactly the reference's canonical form (ssa-ir.md 2.2):
; a `uN` zero-extended, an `iN` sign-extended. Every arithmetic operation is
; performed on `uint64_t`, whose overflow C DEFINES as modular. NO SIGNED
; INTEGER TYPE APPEARS IN EMITTED ARITHMETIC: a signed compare flips the
; sign bit and compares unsigned, an arithmetic shift right replicates the
; sign with a mask, a signed multiply traps by magnitude. `u24`, `u40`,
; `u48` and `u56`, which have no C type, are therefore not a special case
; anywhere. A `ptr` is `unsigned char *`, and address arithmetic goes
; through `uintptr_t` rather than C pointer arithmetic, so an out-of-bounds
; index (which the lowering's `chk` precedes) computes the same wild
; address the reference computes instead of being C undefined behaviour.
; A FLOAT value is the C float itself -- `float` for f32, `double` for f64
; -- and there is deliberately no carrier trick to play: one IR op is one C
; operator on the operands' own width (the prologue asserts FLT_EVAL_METHOD
; 0, so `float op float` stays a float operation), and the C compiler emits
; for `A + B` the same host instruction the reference emits, which is what
; makes the differential test over floats a comparison of like with like.
;
; DETERMINISM (spec 9.3, D5). The text is a function of the module (function
; order = declaration order, block order = block index, value/slot/temporary
; names = internal ids, global order = id order), the `--hospes` row (one
; `_Static_assert` line, program_c.inc's) and `exsc`'s own bytes. No path,
; no `--epoch`, no host name, no address, no hash bucket.
;
; THIS FILE INCLUDES NOTHING, and that is a correction to the design.
;
; c-backend.md D1 and section 1 wrote the chain as
;   program_c.inc -> emit_c.inc -> verify.inc -> print.inc -> parse.inc -> ir.inc
; on the reasoning that the chain must stay linear, which is right. But it
; cannot be THAT chain, because `exsc` contains BOTH backends: `--emitte c`
; and a plain `-o` are two flags of one binary, and `lower/ssa.inc` already
; pulls `backend_fasmg/program.inc`, which pulls emit.inc, which pulls
; verify.inc. A second `include 'verify.inc'` from here would process a
; file full of `struct` and `proc` definitions twice and fail -- exactly the
; diamond emit.inc's own header records paying for once already.
;
; So the C backend's chain HANGS OFF the existing one rather than
; duplicating its tail:
;
;   (consumer brings) ... -> verify.inc -> print.inc -> parse.inc -> ir.inc
;                       \
;                        program_c.inc -> emit_c.inc
;
; THE CONSUMER MUST HAVE INCLUDED `backend_fasmg/verify.inc` -- or anything
; that brings it, such as `backend_fasmg/program.inc` or `lower/lower.inc`
; -- BEFORE `backend_c/program_c.inc`. This is the same contract ir.inc
; states for itself ("does not include rt/; the consumer brings it") and for
; the same reason. `tests/ir/emit_c.asm` includes verify.inc explicitly and
; then program_c.inc; `exsc.asm` gets verify.inc through lower/ and includes
; program_c.inc from `driver/driver.inc`. Recorded as finding 14.
;
; `emit.inc` is NOT required and is never read from here: this file is a
; SIBLING of emit.inc, not a consumer. What it does use from above it is
; print.inc's four output helpers and its `BfaPrintCtx` -- see the next note.
;
; ON `__bfa_out_*` (c-backend.md finding 8, open question 3). The five
; output helpers -- `__bfa_out_lit`, `__bfa_out_byte`, `__bfa_out_bytes`,
; `__bfa_out_u64` and the `BfaPrintCtx` they write into -- are `print.inc`'s,
; which this file's chain already includes, and they are USED IN PLACE. The
; design offered two options, promotion (a change in another agent's tree)
; or duplication (~200 lines of copied code with two places to fix a bug).
; A third was available and is taken: neither. `backend_fasmg/` is not
; touched AT ALL by this milestone and is therefore byte-identical in
; behaviour by construction, and no line is duplicated. The cost is that
; this file reads four `__bfa_`-prefixed names from a module it is not part
; of, which docs/asm-conventions.md does not forbid (it has no privacy rule;
; its section 4.1 is about COLLISIONS, and `__bfc_` collides with nothing).
; If those five are ever promoted to `bfa_out_*`, this file changes at five
; call-site names and nowhere else.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md section 9.2 (two backends), 9.3
; (determinism), 9.5 (`--hospes`), 5.2 (bit fields and byte order), 5.4 (the
; numeric guarantees); ADR 0012; docs/design/c-backend.md.
; -----------------------------------------------------------------------------

; No `include` here -- see "THIS FILE INCLUDES NOTHING", above. If the
; consumer has not brought `backend_fasmg/verify.inc`, the first undefined
; symbol is `bfa_inst_ptr` and this is the comment that says why.

; ============================================================================
; Refusal
; ============================================================================

; __bfc_die(msg_ptr, msg_len) -- never returns. An opcode, width or shape
; this pass has no lowering for is reported by name and the process exits 4.
; Emitting C for it anyway -- guessing -- is what this file must never do.
; Deliberately NOT a diagnostic (no EXS-E code): it is a limit of this
; compiler, not a fact about the source. c-backend.md D2's third bullet and
; section 4's table.
proc __bfc_die, msg_ptr, msg_len
	locals
	endl
	jmp	.after_pfx
  .pfx db "bfc: emitter: "
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

; __bfc_emtxt? text& -- inside a `proc` with an argument literally named
; `ctx` ONLY (the rule emit.inc's `__bfa_emtxt?` and print.inc's
; `__bfa_opname_case?` each state for themselves): appends the literal bytes
; `text` to the output buffer. The `jmp`-guarded `db` is the shape every
; hand-written literal in this tree has; this spells it once.
macro __bfc_emtxt? text&
	local s, e
	mov	rdi, [ctx]
	jmp	e
	s db	text
	e:
	lea	rsi, [s]
	mov	rdx, e - s
	call	__bfa_out_lit
end macro

; __bfc_nl(ctx) -- one newline. emit.inc has `__bfa_emit_nl` for this and is
; not on this file's include chain (see the header), so it is spelled again
; here: three lines, against pulling a whole sibling module in.
proc __bfc_nl, ctx
	locals
	endl
	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte
	return
endp

; ============================================================================
; Type queries
; ============================================================================

; __bfc_ty_info(module, ty) -> eax = BfaType.width, edx = BfaType.kind.
; The mirror of emit.inc's `__bfa_ty_info`, for the same reason `__bfc_nl`
; is a mirror of `__bfa_emit_nl`.
proc __bfc_ty_info, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	bfa_type_ptr
	movzx	edx, byte [rax + BfaType.kind]
	movzx	eax, word [rax + BfaType.width]
	return
endp

; __bfc_ty_signed(module, ty) -> eax = 1 iff the type is `iN`.
; THE KIND, NEVER `BfaType.sign`: parse.inc interns `iN` with sign 1 and
; lower/ty.inc interns it with sign 0 (emit.inc:243-249 records the defect
; that cost), so `sign` answers "unsigned" for every signed type `exsc`
; itself built. The kind is the one field both producers agree on.
proc __bfc_ty_signed, module, ty
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

; __bfc_ty_class(module, ty) -> eax: 0 an integer (uN/iN), 1 an address
; (ptr/ref/refc -- every one of them is `unsigned char *` in C), 2 anything
; else with no C spelling in library mode (a reduction handle, a function
; type), 3 a float (f32 -> `float`, f64 -> `double`), 4 a vector float
; (vf32.N -> `exs_vf32_N`, vf64.N -> `exs_vf64_N`). Class 2 is refused by
; name by every caller; class 3 is the float milestone's addition, and the
; one place it reaches is the float lowerings plus the signature and value
; declarations -- float-typed memory, `iconst` and integer `cmp` refuse it.
; Class 4 is the vector wave's addition: it reaches the vadd/vsub/vmul/vdiv
; lowerings, NATIVUS load/store (bit baggage), and the value declarations;
; a scalar float lowering, a boundary (param/phi/ret/call) and `maior`/
; `minor` order all refuse it by name, with the reference's own words.
proc __bfc_ty_class, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_U
	je	.int
	cmp	ecx, BFA_TK_I
	je	.int
	cmp	ecx, BFA_TK_PTR
	je	.addr
	cmp	ecx, BFA_TK_REF
	je	.addr
	cmp	ecx, BFA_TK_REFC
	je	.addr
	cmp	ecx, BFA_TK_F32
	je	.float
	cmp	ecx, BFA_TK_F64
	je	.float
	cmp	ecx, BFA_TK_VF32
	je	.ovec
	cmp	ecx, BFA_TK_VF64
	je	.ovec
	mov	eax, 2
	return
  .int:
	xor	eax, eax
	return
  .addr:
	mov	eax, 1
	return
  .float:
	mov	eax, 3
	return
  .ovec:
	mov	eax, 4
	return
endp

; __bfc_valty(module, func, valueid) -> eax = the type id of the value this
; instruction produces. `bfa_value_type` returns 0 for cmp/fcmp and the
; three overflow predicates, meaning "u1" (ir.inc's BFA_VALUE_TYPE_IS_U1) --
; NOT "void". Substituting the interned `u1` here is what keeps every caller
; from having to know that.
proc __bfc_valty, module, func, valueid
	locals
	endl
	mov	rdi, [func]
	mov	esi, [valueid]
	call	bfa_value_type
	test	eax, eax
	jnz	.have
	mov	rdi, [module]
	call	bfa_ty_u1
  .have:
	return
endp

; __bfc_emit_ctype(ctx, module, ty) -- the C type of a value of `ty`:
; `uint64_t` for every integer width, `unsigned char *` for every address,
; `float` for f32 and `double` for f64, `exs_vf32_N`/`exs_vf64_N` for a
; vector float (the prologue's vector_size typedefs; the lane count N is
; the type's `ref` field, ir.inc's BFA_TK_VF32 comment). A `ty` of 0 is
; `void` (a signature's return type; ir.inc uses 0 for it).
proc __bfc_emit_ctype, ctx, module, ty
	locals
		slot lanes, dd	; the vector arm's lane count across __bfc_emtxt (its call clobbers rcx)
	endl
	cmp	dword [ty], 0
	jne	.nonvoid
	__bfc_emtxt "void"
	return
  .nonvoid:
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_class
	cmp	eax, 1
	je	.addr
	cmp	eax, 2
	je	.bad
	cmp	eax, 3
	je	.float
	cmp	eax, 4
	je	.vec
	__bfc_emtxt "uint64_t"
	return
  .addr:
	__bfc_emtxt "unsigned char *"
	return
  .float:
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_info
	cmp	edx, BFA_TK_F32
	je	.f32
	__bfc_emtxt "double"
	return
  .f32:
	__bfc_emtxt "float"
	return
  .vec:
	mov	rdi, [module]
	mov	esi, [ty]
	call	bfa_type_ptr
	mov	ecx, [rax + BfaType.ref]	; the lane count (2/4/8)
	mov	[lanes], ecx			; out of rcx before __bfc_emtxt's call
	cmp	byte [rax + BfaType.kind], BFA_TK_VF32
	jne	.vf64
	__bfc_emtxt "exs_vf32_"
	jmp	.vlanes
  .vf64:
	__bfc_emtxt "exs_vf64_"
  .vlanes:
	mov	rdi, [ctx]
	mov	esi, [lanes]
	call	__bfa_out_u64
	return
  .bad:
	jmp	.after_msg
  .msg db "unsupported type: library mode holds an integer (uN/iN), an address (ptr/ref/refc), a float (f32/f64) or a vector float (vf32.N/vf64.N) and nothing else (c-backend.md D4, D8)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfc_die
endp

; ============================================================================
; Operand text
; ============================================================================

; __bfc_emit_v(ctx, valueid) -- "vN", the C name of an IR value. N is the
; INTERNAL 1-based id (D5's audit table), so the text `%0` is `v1`.
proc __bfc_emit_v, ctx, valueid
	locals
	endl
	__bfc_emtxt "v"
	mov	rdi, [ctx]
	mov	esi, [valueid]
	call	__bfa_out_u64
	return
endp

; __bfc_emit_u64c(ctx, value) -- "UINT64_C(<unsigned decimal>)". Every
; constant this emitter prints is a 64-bit BIT PATTERN in canonical form, so
; an `i8` of -56 prints as 18446744073709551560: no signed literal, no hex,
; and therefore no C implementation-defined conversion of a negative
; constant to an unsigned type anywhere in the emitted text (D4 row 59).
proc __bfc_emit_u64c, ctx, value
	locals
	endl
	__bfc_emtxt "UINT64_C("
	mov	rdi, [ctx]
	mov	rsi, [value]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	mov	esi, ')'
	call	__bfa_out_byte
	return
endp

; __bfc_emit_operand(ctx, module, ty, raw, isimm) -- "vN", or the canonical
; 64-bit pattern of an inline immediate. parse.inc stores an inline
; immediate sign-extended in the same u32 field a value id would occupy
; (`__bfa_val_or_imm`), so it is widened with `movsxd` and then
; canonicalised to `ty` exactly as emit.inc's `__bfa_check_opimm` does --
; and, as there, an immediate that is not a value of its type is refused by
; name rather than emitted.
proc __bfc_emit_operand, ctx, module, ty, raw, isimm
	locals
		slot imv, dq
	endl
	cmp	dword [isimm], 0
	jne	.imm
	mov	rdi, [ctx]
	mov	esi, [raw]
	call	__bfc_emit_v
	return
  .imm:
	movsxd	rax, dword [raw]
	mov	[imv], rax
	mov	rdi, [module]
	mov	esi, [ty]
	mov	rdx, rax
	call	bfa_type_canon
	cmp	rax, [imv]
	jne	.notcanon
	mov	rdi, [ctx]
	mov	rsi, rax
	call	__bfc_emit_u64c
	return
  .notcanon:
	jmp	.after_msg
  .msg db "immediate operand is not a value of its type (ssa-ir.md 2.2)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfc_die
endp

; __bfc_emit_opa / __bfc_emit_opb(ctx, module, func, instid) -- operand `a`
; (resp. `b`) of a BIN/CMP-shaped instruction, immediate flag and all.
; `aux` bit 0x100 marks `a` an inline immediate and 0x200 marks `b`
; (ir.inc's operand-descriptor note); the two spellings exist so no call
; site has to repeat the mask.
proc __bfc_emit_opa, ctx, module, func, instid
	locals
		slot raw, dd
		slot fl,  dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[raw], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	and	ecx, 0x100
	mov	[fl], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, ecx
	mov	ecx, [raw]
	mov	r8d, [fl]
	call	__bfc_emit_operand
	return
endp

proc __bfc_emit_opb, ctx, module, func, instid
	locals
		slot raw, dd
		slot fl,  dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.b]
	mov	[raw], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	and	ecx, 0x200
	mov	[fl], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, ecx
	mov	ecx, [raw]
	mov	r8d, [fl]
	call	__bfc_emit_operand
	return
endp

; ============================================================================
; Identifier mangling -- c-backend.md D5
; ============================================================================

; __bfc_hexnib(ctx, nib) -- one UPPERCASE hex digit. The mangling's escapes
; are uppercase because the decode rule ("at `_`, two uppercase hex digits
; mean an escaped byte") is only injective if the escape alphabet excludes
; the lowercase letters a Latin name is made of. D5's "Rejected: lowercase
; hex escapes" is that argument.
proc __bfc_hexnib, ctx, nib
	locals
	endl
	mov	eax, [nib]
	and	eax, 15
	cmp	eax, 10
	jb	.digit
	add	eax, 'A' - 10
	jmp	.out
  .digit:
	add	eax, '0'
  .out:
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_byte
	return
endp

; __bfc_is_uhex(c) -> eax = 1 iff `c` is one of 0-9 A-F -- the alphabet a
; mangled escape uses, and therefore the lookahead that decides whether a
; SOURCE underscore has to be escaped.
proc __bfc_is_uhex, c
	locals
	endl
	mov	ecx, [c]
	mov	eax, 1
	cmp	ecx, '0'
	jb	.no
	cmp	ecx, '9'
	jbe	.yes
	cmp	ecx, 'A'
	jb	.no
	cmp	ecx, 'F'
	jbe	.yes
  .no:
	xor	eax, eax
  .yes:
	return
endp

; __bfc_mangle(ctx, ptr, len) -- D5's mangling, emitted.
;
;   [A-Za-z0-9]  kept
;   `_`          kept, UNLESS the next two bytes are both uppercase hex
;                digits, in which case it is escaped as `_5F`
;   anything else  `_` + two UPPERCASE hex digits of the byte
;
; Injective, and decodable left to right: at a `_`, two uppercase hex digits
; mean an escaped byte and anything else means a literal underscore. The
; input is the NFC UTF-8 of the name (spec 8.1 guarantees NFC, so the byte
; sequence is canonical and two names that differ only outside ASCII get two
; different C names). Worked examples, all pinned by tests/unit/bfc_mangle:
;   scribe_octeto -> scribe_octeto      ('o' is not an uppercase hex digit)
;   Type.m        -> Type_2Em
;   lambda$7      -> lambda_247
;   mensura with two macrons -> m_C4_93ns_C5_ABra
;   a_5F          -> a_5F5F             (the escape's own escape)
proc __bfc_mangle, ctx, ptr, len
	uses	rbx
	locals
		slot c, dd
	endl
	xor	ebx, ebx
  .loop:
	cmp	rbx, [len]
	jae	.done
	mov	rax, [ptr]
	movzx	ecx, byte [rax + rbx]
	mov	[c], ecx

	; [0-9A-Za-z] travel verbatim
	cmp	ecx, '0'
	jb	.notalnum
	cmp	ecx, '9'
	jbe	.keep
	cmp	ecx, 'A'
	jb	.notalnum
	cmp	ecx, 'Z'
	jbe	.keep
	cmp	ecx, 'a'
	jb	.notalnum
	cmp	ecx, 'z'
	jbe	.keep
  .notalnum:
	cmp	dword [c], '_'
	jne	.escape

	; a source `_`: escape it only when the two bytes after it would be
	; read back as an escape's hex pair.
	mov	rax, rbx
	add	rax, 2
	cmp	rax, [len]
	jae	.keep			; fewer than two bytes follow: unambiguous
	mov	rax, [ptr]
	movzx	edi, byte [rax + rbx + 1]
	call	__bfc_is_uhex
	test	eax, eax
	jz	.keep
	mov	rax, [ptr]
	movzx	edi, byte [rax + rbx + 2]
	call	__bfc_is_uhex
	test	eax, eax
	jz	.keep
	__bfc_emtxt "_5F"
	jmp	.next

  .keep:
	mov	rdi, [ctx]
	mov	esi, [c]
	call	__bfa_out_byte
	jmp	.next

  .escape:
	mov	rdi, [ctx]
	mov	esi, '_'
	call	__bfa_out_byte
	mov	rdi, [ctx]
	mov	eax, [c]
	shr	eax, 4
	mov	esi, eax
	call	__bfc_hexnib
	mov	rdi, [ctx]
	mov	esi, [c]
	call	__bfc_hexnib

  .next:
	inc	rbx
	jmp	.loop
  .done:
	return
endp

; __bfc_check_cident(ptr, len) -- dies unless the bytes are already a C
; identifier. Applied to an `externus` symbol ONLY, which D5 emits VERBATIM
; with no prefix because it names something the LINKER must find: spec 5.3's
; `strlen` must be `strlen`, not `exs_strlen`. A defined function is
; mangled instead and can never fail this.
proc __bfc_check_cident, ptr, len
	uses	rbx
	locals
	endl
	cmp	qword [len], 0
	je	.bad
	xor	ebx, ebx
  .loop:
	cmp	rbx, [len]
	jae	.ok
	mov	rax, [ptr]
	movzx	ecx, byte [rax + rbx]
	cmp	ecx, '_'
	je	.next
	cmp	ecx, 'A'
	jb	.maybedigit
	cmp	ecx, 'Z'
	jbe	.next
	cmp	ecx, 'a'
	jb	.bad
	cmp	ecx, 'z'
	jbe	.next
	jmp	.bad
  .maybedigit:
	test	rbx, rbx
	jz	.bad			; a leading digit is not an identifier
	cmp	ecx, '0'
	jb	.bad
	cmp	ecx, '9'
	ja	.bad
  .next:
	inc	rbx
	jmp	.loop
  .bad:
	jmp	.after_msg
  .msg db "externus symbol is not a C identifier (c-backend.md D5: a declaration names what the linker must find, so it is emitted verbatim)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfc_die
  .ok:
	return
endp

; __bfc_emit_fname(ctx, module, fptr) -- the C name of a function.
; A DEFINED function is `exs_` + mangle(name): the prefix keeps this
; module's definitions out of the linker's way, and cannot collide with a
; helper (`exsi_`) or a runtime hook (`exsrt_`) because the three prefixes
; differ at their fourth byte. A BODILESS declaration is the symbol
; verbatim, checked to be a C identifier (D5).
proc __bfc_emit_fname, ctx, module, fptr
	uses	rbx
	locals
		slot nm,  dq
		slot nml, dq
	endl
	mov	rbx, [fptr]
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.names]
	mov	esi, [rbx + BfaFunc.name]
	call	intern_bytes
	mov	[nm], rax
	mov	[nml], rdx

	test	dword [rbx + BfaFunc.attrs], BFA_FUNC_ATTR_EXTERNUS
	jnz	.bare

	__bfc_emtxt "exs_"
	mov	rdi, [ctx]
	mov	rsi, [nm]
	mov	rdx, [nml]
	call	__bfc_mangle
	return
  .bare:
	mov	rdi, [nm]
	mov	rsi, [nml]
	call	__bfc_check_cident
	mov	rdi, [ctx]
	mov	rsi, [nm]
	mov	rdx, [nml]
	call	__bfa_out_bytes
	return
endp

; __bfc_emit_calleename(ctx, module, idv) -- `idv` is the 1-BASED function
; index a `call`'s extra[0] holds (ir.inc; emit.inc's
; `__bfa_emit_callee_label` does the same `dec`).
proc __bfc_emit_calleename, ctx, module, idv
	locals
	endl
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, [idv]
	dec	esi
	call	vec_get
	mov	rax, [rax]
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, rax
	call	__bfc_emit_fname
	return
endp

; ============================================================================
; Normalisation -- the canonical form, spelled in C
; ============================================================================

; __bfc_norm_open(ctx, module, ty) / __bfc_norm_close(ctx, module, ty) --
; the two halves of `exsi_norm_u(` ... `, N)`, wrapped around whatever the
; caller emits between them.
;
; AT N == 64 BOTH EMIT NOTHING, because at 64 both helpers are the identity:
; `norm_u(x,64)` is `x & UINT64_MAX`, and `norm_i(x,64)` is `(x ^ 2^63) -
; 2^63`, which is `x` modulo 2^64. Skipping it is therefore exact, not an
; optimisation -- and it is what the reference does too (`__bfa_emit_norm`
; emits nothing at nbits >= 64), so the two backends' texts stay the same
; shape at the same widths. The pair is always called together on the same
; `ty`, so the two can never disagree about whether to fire.
proc __bfc_norm_open, ctx, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_info
	cmp	eax, 64
	jae	.none
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_signed
	test	eax, eax
	jnz	.signed
	__bfc_emtxt "exsi_norm_u("
	return
  .signed:
	__bfc_emtxt "exsi_norm_i("
  .none:
	return
endp

proc __bfc_norm_close, ctx, module, ty
	locals
		slot nbits, dd
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_info
	cmp	eax, 64
	jae	.none
	mov	[nbits], eax
	__bfc_emtxt ", "
	mov	rdi, [ctx]
	mov	esi, [nbits]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	mov	esi, ')'
	call	__bfa_out_byte
  .none:
	return
endp

; __bfc_int_width(module, ty) -> eax = N, having refused anything that is
; not a `uN`/`iN`. Every integer lowering below needs exactly this. A float
; reaching here is a different opcode's operand (`fadd`, not `add`) and an
; address is not an integer either; the message says whose lowering this is.
proc __bfc_int_width, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_class
	test	eax, eax
	jnz	.bad
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_info
	return
  .bad:
	jmp	.after_msg
  .msg db "integer (uN/iN) operands only -- this lowering is the integer one (a float arrives through the float opcodes, and an address is not an integer either)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfc_die
endp

; __bfc_emit_result(ctx, func, instid) -- "    vK = ", the left-hand side
; every value-producing lowering opens with. Four spaces: the emitted C is
; indented, never tabbed, because a tab in C source is a diff hazard and
; nothing about the output is read by a tab-sensitive tool.
proc __bfc_emit_result, ctx, func, instid
	locals
	endl
	__bfc_emtxt "    "
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfc_emit_v
	__bfc_emtxt " = "
	return
endp

; __bfc_semi(ctx) -- ";\n".
proc __bfc_semi, ctx
	locals
	endl
	__bfc_emtxt ";", 10
	return
endp

; ============================================================================
; D4 rows 1-3, 6-10, 15-19: the binary integer opcodes
; ============================================================================

; __bfc_emit_bin(ctx, module, func, instid) -- one statement, three shapes:
;
;   HELPER  vK = exsi_<op>_<u|i>(A, B, N);     add sub mul adds subs shl shr
;   WRAP    vK = exsi_norm_<u|i>(A <op> B, N); addw subw mulw
;   PLAIN   vK = A <op> B;                     and or xor
;
; The WRAP row is exact: the low N bits of a modular 64-bit sum, difference
; or product are the low N bits of the true one, so wrapping needs no helper
; -- only the normalisation that puts the result back in canonical form.
; The PLAIN row needs neither: canonical in, canonical out, for all three.
proc __bfc_emit_bin, ctx, module, func, instid
	uses	rbx
	locals
		slot op,  dd
		slot tyv, dd
		slot nbits, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_int_width
	mov	[nbits], eax

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result

	; ---- PLAIN: and or xor ----------------------------------------------
	cmp	dword [op], BFA_OP_AND
	je	.plain
	cmp	dword [op], BFA_OP_OR
	je	.plain
	cmp	dword [op], BFA_OP_XOR
	je	.plain
	; ---- WRAP: addw subw mulw -------------------------------------------
	cmp	dword [op], BFA_OP_ADDW
	je	.wrap
	cmp	dword [op], BFA_OP_SUBW
	je	.wrap
	cmp	dword [op], BFA_OP_MULW
	je	.wrap
	jmp	.helper

  .plain:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	mov	rdi, [ctx]
	mov	esi, [op]
	call	__bfc_emit_binop
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	jmp	.close

  .wrap:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_norm_open
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	mov	rdi, [ctx]
	mov	esi, [op]
	call	__bfc_emit_binop
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_norm_close
	jmp	.close

  .helper:
	mov	rdi, [ctx]
	mov	esi, [op]
	call	__bfc_emit_helpername
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_signed
	test	eax, eax
	jz	.hu
	__bfc_emtxt "_i("
	jmp	.hargs
  .hu:
	__bfc_emtxt "_u("
  .hargs:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	__bfc_emtxt ", "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	__bfc_emtxt ", "
	mov	rdi, [ctx]
	mov	esi, [nbits]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	mov	esi, ')'
	call	__bfa_out_byte

  .close:
	mov	rdi, [ctx]
	call	__bfc_semi
	return
endp

; __bfc_emit_binop(ctx, op) -- the C operator for a PLAIN or WRAP row.
proc __bfc_emit_binop, ctx, op
	locals
	endl
	cmp	dword [op], BFA_OP_AND
	je	.o_and
	cmp	dword [op], BFA_OP_OR
	je	.o_or
	cmp	dword [op], BFA_OP_XOR
	je	.o_xor
	cmp	dword [op], BFA_OP_SUBW
	je	.o_sub
	cmp	dword [op], BFA_OP_MULW
	je	.o_mul
	__bfc_emtxt " + "
	return
  .o_sub:
	__bfc_emtxt " - "
	return
  .o_mul:
	__bfc_emtxt " * "
	return
  .o_and:
	__bfc_emtxt " & "
	return
  .o_or:
	__bfc_emtxt " | "
	return
  .o_xor:
	__bfc_emtxt " ^ "
	return
endp

; __bfc_emit_helpername(ctx, op) -- "exsi_add" etc.; the caller appends
; "_u(" or "_i(".
proc __bfc_emit_helpername, ctx, op
	locals
	endl
	cmp	dword [op], BFA_OP_SUB
	je	.h_sub
	cmp	dword [op], BFA_OP_MUL
	je	.h_mul
	cmp	dword [op], BFA_OP_ADDS
	je	.h_adds
	cmp	dword [op], BFA_OP_SUBS
	je	.h_subs
	cmp	dword [op], BFA_OP_SHL
	je	.h_shl
	cmp	dword [op], BFA_OP_SHR
	je	.h_shr
	__bfc_emtxt "exsi_add"
	return
  .h_sub:
	__bfc_emtxt "exsi_sub"
	return
  .h_mul:
	__bfc_emtxt "exsi_mul"
	return
  .h_adds:
	__bfc_emtxt "exsi_adds"
	return
  .h_subs:
	__bfc_emtxt "exsi_subs"
	return
  .h_shl:
	__bfc_emtxt "exsi_shl"
	return
  .h_shr:
	__bfc_emtxt "exsi_shr"
	return
endp

; ============================================================================
; D4 row 20: cmp
; ============================================================================

; __bfc_emit_pred(ctx, pred) -- the C relational operator.
proc __bfc_emit_pred, ctx, pred
	locals
	endl
	cmp	dword [pred], BFA_PRED_NE
	je	.p_ne
	cmp	dword [pred], BFA_PRED_LT
	je	.p_lt
	cmp	dword [pred], BFA_PRED_LE
	je	.p_le
	cmp	dword [pred], BFA_PRED_GT
	je	.p_gt
	cmp	dword [pred], BFA_PRED_GE
	je	.p_ge
	__bfc_emtxt " == "
	return
  .p_ne:
	__bfc_emtxt " != "
	return
  .p_lt:
	__bfc_emtxt " < "
	return
  .p_le:
	__bfc_emtxt " <= "
	return
  .p_gt:
	__bfc_emtxt " > "
	return
  .p_ge:
	__bfc_emtxt " >= "
	return
endp

; __bfc_emit_cmp(ctx, module, func, instid) -- `cmp.p T %a %b -> u1` and
; `fcmp.p T %a %b -> u1` (D4 rows 20 and 30).
;
;   cmp     uN    vK = (A OP B);
;   cmp     ptr   vK = ((uintptr_t)A OP (uintptr_t)B);
;   cmp     iN    vK = ((A ^ S64) OP (B ^ S64));   S64 = 1 << 63
;   fcmp    F     vK = (A OP B);                  ne: (A < B) || (A > B)
;
; The `iN` row is the whole reason no signed C type appears in emitted
; arithmetic: flipping the sign bit maps the signed order onto the unsigned
; one exactly, so a signed compare is an unsigned compare of two flipped
; patterns, with no signed overflow to be undefined about.
;
; The `ptr` row goes through `uintptr_t` because C leaves `<` between
; pointers into different objects undefined, while the reference merely
; compares two addresses; comparing the integers is what the reference
; actually does.
;
; THE FCMP ROW IS ORDERED (ssa-ir.md 2.3: "fcmp ordered; unordered forms
; [OPEN]"): every predicate is FALSE on any NaN operand. Five of the six
; map onto C's own relational operators directly, because C's `==`, `<`,
; `<=`, `>`, `>=` are quiet and yield 0 when either operand is NaN -- that
; is the definition of ordered. The exception is `ne`: C's `!=` is the
; NEGATION of `==` and yields 1 on NaN -- unordered-NE -- so ordered-ne is
; spelled `(A < B) || (A > B)`, false on NaN and equal to `!=` on every
; pair of non-NaN operands. The reference reaches the same truth table its
; own way (ucomisd/set<cc> ANDed with setnp); this spelling does not lean
; on the accident that makes a bare `setne` come out ordered too.
;
; A `cmp` (the INTEGER compare) whose `ty` is a float is refused by name:
; the float compare is `fcmp.*`, and an integer predicate on float operands
; has no meaning in either backend.
proc __bfc_emit_cmp, ctx, module, func, instid
	locals
		slot op,   dd
		slot tyv,  dd
		slot pred, dd
		slot cls,  dd
		slot auxv, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[auxv], ecx
	and	ecx, 0xFF		; aux also carries the 0x100/0x200
	mov	[pred], ecx		; immediate flags; low byte = predicate

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	mov	[cls], eax

	cmp	dword [op], BFA_OP_FCMP
	je	.floatcmp

	; ---- the integer compare --------------------------------------------
	cmp	eax, 2
	je	.wrongty
	cmp	eax, 3
	je	.wrongty

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "("

	cmp	dword [cls], 1
	je	.ptrcmp
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_signed
	test	eax, eax
	jnz	.signedcmp

	; ---- unsigned integers -----------------------------------------------
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	mov	rdi, [ctx]
	mov	esi, [pred]
	call	__bfc_emit_pred
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	jmp	.close

	; ---- addresses, as integers ------------------------------------------
  .ptrcmp:
	__bfc_emtxt "(uintptr_t)"
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	mov	rdi, [ctx]
	mov	esi, [pred]
	call	__bfc_emit_pred
	__bfc_emtxt "(uintptr_t)"
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	jmp	.close

	; ---- signed integers, by a sign-bit flip -----------------------------
  .signedcmp:
	__bfc_emtxt "("
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	__bfc_emtxt " ^ UINT64_C(9223372036854775808))"
	mov	rdi, [ctx]
	mov	esi, [pred]
	call	__bfc_emit_pred
	__bfc_emtxt "("
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	__bfc_emtxt " ^ UINT64_C(9223372036854775808))"

  .close:
	__bfc_emtxt ")"
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  ; ---- the float compare ------------------------------------------------
  .floatcmp:
	; `ty` is the OPERAND type here (ir.inc's bfa_value_type: the produced
	; value is u1), and it must be a float: a fcmp on integers is as wrong
	; as a cmp on floats, and the verifier's cmp rule checks only that the
	; operands are values of `ty`, whichever it is.
	cmp	dword [cls], 3
	je	.fltyok
	cmp	dword [cls], 4
	je	.flvty			; a vector takes the vadd group's lowering, never a compare
	jmp	.wrongty
  .fltyok:
	; INLINE IMMEDIATES ARE REFUSED. parse.inc's SH_BIN/SH_FCMP shapes set
	; the aux immediate flags for float lines too, and the VERIFIER passes
	; them: `bfa_type_canon` is the identity on a float's bits (ir.inc), so
	; every 32-bit pattern is "canonical". `fconst` is the leaf -- the raw
	; bit pattern, never decimal text (ssa-ir.md 2.3) -- and a UINT64_C
	; literal in a float expression is exactly that decimal text.
	mov	eax, [auxv]
	and	eax, 0x300
	jnz	.flimm
	; the six ordered predicates; parse.inc cannot produce another, a
	; hand-built module could, and an out-of-range predicate would fall
	; off the emitter's operator table into someone else's text.
	cmp	dword [pred], BFA_PRED_GE
	ja	.flpred

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "("
	cmp	dword [pred], BFA_PRED_NE
	je	.flne
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	mov	rdi, [ctx]
	mov	esi, [pred]
	call	__bfc_emit_fpred
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	jmp	.close
  .flne:
	__bfc_emtxt "("
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	__bfc_emtxt " < "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	__bfc_emtxt ") || ("
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	__bfc_emtxt " > "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	__bfc_emtxt ")"
	jmp	.close

  .flimm:
	jmp	.after_m1
  .m1 db "fcmp: float operands are values only (`fconst` is the leaf -- the raw bit pattern, never decimal text)"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die

  .flvty:
	call	__bfc_die_vecfloat

  .flpred:
	jmp	.after_m2
  .m2 db "fcmp: ordered predicates only (eq ne lt le gt ge; unordered forms are [OPEN], ssa-ir.md 2.3)"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die

  .wrongty:
	jmp	.after_m3
  .m3 db "cmp/fcmp: the compare and its operands must agree -- `cmp.*` is the integer one, `fcmp.*` the float one (spec 5.4)"
  .after_m3:
	lea	rdi, [.m3]
	mov	rsi, .after_m3 - .m3
	call	__bfc_die
endp

; __bfc_emit_fpred(ctx, pred) -- the C relational operator for an ORDERED
; float predicate. Exactly `__bfc_emit_pred`'s table MINUS `ne`, which has a
; spelling of its own above (C's `!=` is unordered-NE); keeping the two
; tables separate is what stops a future edit from reaching for `!=` here.
proc __bfc_emit_fpred, ctx, pred
	locals
	endl
	cmp	dword [pred], BFA_PRED_LT
	je	.p_lt
	cmp	dword [pred], BFA_PRED_LE
	je	.p_le
	cmp	dword [pred], BFA_PRED_GT
	je	.p_gt
	cmp	dword [pred], BFA_PRED_GE
	je	.p_ge
	__bfc_emtxt " == "
	return
  .p_lt:
	__bfc_emtxt " < "
	return
  .p_le:
	__bfc_emtxt " <= "
	return
  .p_gt:
	__bfc_emtxt " > "
	return
  .p_ge:
	__bfc_emtxt " >= "
	return
endp

; ============================================================================
; D4 rows 21-23: zext / sext / trunc
; ============================================================================

; __bfc_emit_conv(ctx, module, func, instid).
;
;   zext   vK = normT(exsi_norm_u(A, m), N);
;   sext   vK = normT(exsi_norm_i(A, m), N);
;   trunc  vK = normT(A, N);
;
; `m` is the SOURCE width and `N` the destination's; `normT` is the
; destination type's own form. Both steps matter and the order is ssa-ir.md
; 2.2's: extend from the source width first, then cut to the destination's
; form -- which is why `sext u16` of an `i8` holding -56 is 65480 and not
; 18446744073709551560.
proc __bfc_emit_conv, ctx, module, func, instid
	locals
		slot op,    dd
		slot av,    dd
		slot tyv,   dd
		slot sty,   dd
		slot sbits, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx

	mov	rdi, [module]
	mov	rsi, [func]
	mov	edx, [av]
	call	__bfc_valty
	mov	[sty], eax
	mov	rdi, [module]
	mov	esi, eax
	call	__bfc_int_width
	mov	[sbits], eax
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_int_width		; refuses a non-integer destination

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result

	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_norm_open

	cmp	dword [op], BFA_OP_TRUNC
	je	.src

	; the source-width step, zero- or sign-extending
	cmp	dword [op], BFA_OP_SEXT
	je	.sx
	__bfc_emtxt "exsi_norm_u("
	jmp	.srcinner
  .sx:
	__bfc_emtxt "exsi_norm_i("
  .srcinner:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v
	__bfc_emtxt ", "
	mov	rdi, [ctx]
	mov	esi, [sbits]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	mov	esi, ')'
	call	__bfa_out_byte
	jmp	.dstclose

  .src:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v

  .dstclose:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_norm_close
	mov	rdi, [ctx]
	call	__bfc_semi
	return
endp

; ============================================================================
; D4 rows 24-34 and 60: the float opcodes
; ============================================================================
; fadd fsub fmul fdiv fneg fcmp fext ftrunc itof ftoi fconst -- the eleven
; float opcodes the reference lowers (emit.inc's float arms), and therefore
; the eleven this emitter lowers too. `fma` and `bitcast` are the two float
; rows the reference refuses, and they fall to `__bfc_refuse` here, which
; names them for the same reasons (spec 9.2: the two emitters' refusal sets
; are ONE SET -- a divergence is a finding, not a local choice).
;
; A FLOAT VALUE IS THE C FLOAT ITSELF. There is deliberately no carrier
; trick to play here: `float` for f32, `double` for f64, one IR op one C
; statement, so no emitted statement ever holds an expression a compiler
; could contract or reassociate, and `float op float` stays a float
; operation because the prologue pins FLT_EVAL_METHOD 0. The C compiler
; emits for `A + B` the same host instruction the reference emits
; (addss/addsd &c); the whole numeric subset (spec 5.4) rides on C doing
; IEEE 754 here, which the prologue asserts with <float.h> rather than
; promising on its own.
;
; What this section therefore REFUSES is exactly what cannot be one C
; statement on the operands' own width:
;   * an inline immediate on a float operand -- `fconst` is the leaf, and
;     it carries RAW BITS (ssa-ir.md 2.3), never decimal text; the
;     verifier passes a float-typed immediate (bfa_type_canon is the
;     identity on a float's bits), so the emitter is the one that stops it.
;   * `iconst` with a float type, float-typed `load`/`store`, `cmp` with a
;     float type, `itof` u64 -> f32, and `fconst f32` with bits at or above
;     2^32 -- each named where its own lowering lives.
; ----------------------------------------------------------------------------

; __bfc_die_vecfloat -- never returns. A vector float (vf32.N/vf64.N) that
; reached a SCALAR float lowering (fadd/fsub/fmul/fdiv, fneg, fcmp, or a
; float conversion's destination) is named for the family it should have
; taken instead, in exactly the words of emit.inc's `__bfa_emit_fwidth`
; `.vec` arm: the two emitters' refusal sets are ONE SET (spec 9.2), and
; the set's texts are the reference's.
proc __bfc_die_vecfloat
	locals
	endl
	jmp	.after_m
  .m db "float lowering: a vector type (`vf32.N`/`vf64.N`) reaches only the vadd/vsub/vmul/vdiv lowerings"
  .after_m:
	lea	rdi, [.m]
	mov	rsi, .after_m - .m
	call	__bfc_die
endp

; __bfc_emit_fbin(ctx, module, func, instid) -- fadd/fsub/fmul/fdiv.
;
;   vK = A + B;   (and - * /)
;
; The operator is chosen by its own four-way chain, NOT by borrowing
; `__bfc_emit_binop`: that table is the integer one and has no " / " row,
; so a future opcode reaching for it would fall through into " + " -- the
; same reason the dispatcher is a flat compare chain.
proc __bfc_emit_fbin, ctx, module, func, instid
	locals
		slot op,   dd
		slot tyv,  dd
		slot auxv, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[auxv], ecx

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 3
	je	.tyok
	cmp	eax, 4
	je	.vty			; a vector takes the vadd group's lowering
	jmp	.badty
  .tyok:
	mov	eax, [auxv]
	and	eax, 0x300		; an immediate on either operand
	jnz	.flimm

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	cmp	dword [op], BFA_OP_FADD
	je	.add
	cmp	dword [op], BFA_OP_FSUB
	je	.sub
	cmp	dword [op], BFA_OP_FMUL
	je	.mul
	__bfc_emtxt " / "
	jmp	.ops
  .add:
	__bfc_emtxt " + "
	jmp	.ops
  .sub:
	__bfc_emtxt " - "
	jmp	.ops
  .mul:
	__bfc_emtxt " * "
  .ops:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  .badty:
	jmp	.after_m1
  .m1 db "fadd/fsub/fmul/fdiv: the operand type must be a float (f32/f64) -- an integer arrives through the integer opcodes"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
  .vty:
	call	__bfc_die_vecfloat
  .flimm:
	jmp	.after_m2
  .m2 db "fadd/fsub/fmul/fdiv: float operands are values only (`fconst` is the leaf -- the raw bit pattern, never decimal text)"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die
endp

; __bfc_emit_varith(ctx, module, func, instid) -- vadd/vsub/vmul/vdiv, the
; vector float group's arithmetic (spec 5.4; the B5.4 plan, settled):
;
;   vK = vA + vB;   (and - * /)
;
; One IR op is one C statement on the prologue's vector_size type, exactly
; __bfc_emit_fbin's shape (the contraction defense -- there is nothing in
; one statement to contract), and C's elementwise lowering of the operator
; is the ONE IEEE rounding PER LANE the reference's per-chunk packed
; addps/subps/mulps/divps (VF32) or addpd &c (VF64) carries --
; `__bfc_emit_fbin`'s own four-way operator chain, for its own four opcodes:
; borrowing the float chain's would be wrong the same way borrowing the
; integer table is, so this chain spells the compare sequence again.
; A vf32.8 (32 bytes) is two of the reference's 16-byte chunks; C's
; vector_size arithmetic covers both with the one operator, which the
; differential run of tests/ir/vec_arith.ir part 3 holds to exit 200.
;
; The two refusals are `__bfa_emit_varith`'s own, word for word and in its
; own order (an inline immediate first, the type second) -- the two
; emitters' refusal sets are one set, and tests/ir/reject_emit_vadd_scalar.ir
; (`vadd f64 %1 %2`) is the parity anchor that pins the type refusal here:
; the verifier PASSES it (two f64 operands with an f64 result agree) and the
; emitter refuses it by name, because one packed op over one lane is the
; scalar group misspelt.
proc __bfc_emit_varith, ctx, module, func, instid
	locals
		slot op,   dd
		slot tyv,  dd
		slot auxv, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[auxv], ecx

	mov	eax, [auxv]
	and	eax, 0x300		; an immediate on either operand
	jnz	.vlimm
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 4
	jne	.notv

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opa
	cmp	dword [op], BFA_OP_VADD
	je	.add
	cmp	dword [op], BFA_OP_VSUB
	je	.sub
	cmp	dword [op], BFA_OP_VMUL
	je	.mul
	__bfc_emtxt " / "
	jmp	.ops
  .add:
	__bfc_emtxt " + "
	jmp	.ops
  .sub:
	__bfc_emtxt " - "
	jmp	.ops
  .mul:
	__bfc_emtxt " * "
  .ops:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_opb
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  .vlimm:
	jmp	.after_m1
  .m1 db "vadd/vsub/vmul/vdiv: vector operands are values only (scalars store lanes; indexing reads them)"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
  .notv:
	jmp	.after_m2
  .m2 db "vadd/vsub/vmul/vdiv: the type must be a vector float (`vf32.N`/`vf64.N`, N = 2, 4 or 8)"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die
endp

; __bfc_emit_fneg(ctx, module, func, instid) -- `vK = -A;`
;
; C's unary minus on a floating operand is IEC negation: the sign bit flips
; and NOTHING else -- payload and exponent go through untouched -- which is
; exactly the reference's xorps row. NaN payloads survive (the ordered
; fcmp predicates stay false on the result either way, but a payload is
; observable through `fconst`'s round-trip), and -0.0 <-> +0.0.
proc __bfc_emit_fneg, ctx, module, func, instid
	locals
		slot av,   dd
		slot tyv,  dd
		slot auxv, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[auxv], ecx

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 3
	je	.tyok
	cmp	eax, 4
	je	.vty			; a vector takes the vadd group's lowering
	jmp	.badty
  .tyok:
	mov	eax, [auxv]
	and	eax, 0x100
	jnz	.flimm

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "-"
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  .badty:
	jmp	.after_m1
  .m1 db "fneg: the operand type must be a float (f32/f64)"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
  .vty:
	call	__bfc_die_vecfloat
  .flimm:
	jmp	.after_m2
  .m2 db "fneg: the operand is a value only (`fconst` is the leaf -- the raw bit pattern, never decimal text)"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die
endp

; __bfc_emit_fconv(ctx, module, func, instid) -- the four float conversions.
;
;   fext    f32 -> f64   vK = (double)A;        exact: widening never rounds
;   ftrunc  f64 -> f32   vK = (float)A;         round-to-nearest-even
;   ftrunc  F    -> F    vK = A;                a move (the reference's
;                                               plain register move too)
;   itof    uN   -> F    vK = (F)A;             correctly rounded by C
;   itof    iN   -> F    vK = (F)exsi_i64_from_bits(A);
;   ftoi    F    -> uN   vK = (uint64_t)A;
;   ftoi    F    -> iN   vK = (uint64_t)(int64_t)A;
;
; `ftrunc` IS THE NARROWING CAST (lowering.md: int<->float is itof/ftoi,
; f32<->f64 is fext/ftrunc) -- NOT a round-to-integral: roundsd is nobody's
; row here, and the C `(float)` cast is the whole lowering. The direction
; constraints are refused by name when violated, because the verifier checks
; only that the operands are values, not which width went which way.
;
; `itof` re-reads a signed source's bits as int64_t (the prologue helper)
; rather than converting the uint64_t carrier: C leaves uint64_t -> int64_t
; out-of-range IMPLEMENTATION-DEFINED, and the canonical carrier of an iN
; IS the two's-complement pattern. u64 -> f32 is refused -- the reference
; refuses it too (no correctly-rounded one-step sequence at its SSE2
; baseline; a naive (float)(double) double-rounds).
;
; `ftoi`'s C-side twin of the reference's [OPEN] NaN/range behaviour
; (ssa-ir.md 2.3): C leaves a float -> integer conversion whose value does
; not fit UNDEFINED, so under the harness's -fsanitize=undefined a
; NaN-or-out-of-range source ABORTS at the cast instead of returning the
; reference's whatever-happened bits. The corpus stays in range by
; construction (a ftoi is written where the value is known to fit, the way
; `add` is written where the addition is known not to overflow), and the
; abort is the C-backend's documented answer to the row the reference marks
; [OPEN]. Truncation is toward zero in both backends -- C's cast and the
; reference's cvtt* agree on that much.
proc __bfc_emit_fconv, ctx, module, func, instid
	locals
		slot op,   dd
		slot av,   dd
		slot tyv,  dd
		slot sty,  dd
		slot tyk,  dd
		slot stk,  dd
		slot swd,  dd
		slot auxv, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[auxv], ecx

	; the source type and its kind/width
	mov	rdi, [module]
	mov	rsi, [func]
	mov	edx, [av]
	call	__bfc_valty
	mov	[sty], eax
	mov	rdi, [module]
	mov	esi, eax
	call	__bfc_ty_info
	mov	[stk], edx
	mov	[swd], eax
	; the destination kind
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_info
	mov	[tyk], edx

	; A VECTOR destination has no scalar conversion lowering: the
	; reference's `__bfa_emit_fwidth` names the family it should have
	; taken (emit.inc's `.vec` arm), and so does this. A vector SOURCE is
	; refused further down by each arm's own source-kind check, the same
	; split the reference keeps (its per-op "the operand is not a
	; float/integer" texts fire before fwidth sees the destination).
	cmp	dword [tyk], BFA_TK_VF32
	je	.vecdst
	cmp	dword [tyk], BFA_TK_VF64
	je	.vecdst

	cmp	dword [op], BFA_OP_FEXT
	je	.fext
	cmp	dword [op], BFA_OP_FTRUNC
	je	.ftrunc
	cmp	dword [op], BFA_OP_ITOF
	je	.itof
	jmp	.ftoi

  ; ---- fext: f32 -> f64 --------------------------------------------------
  .fext:
	mov	eax, [auxv]
	and	eax, 0x100
	jnz	.flimm
	cmp	dword [tyk], BFA_TK_F64
	jne	.fextbad
	cmp	dword [stk], BFA_TK_F32
	jne	.fextbad
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "(double)"
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  ; ---- ftrunc: f64 -> f32, or equal width ---------------------------------
  .ftrunc:
	mov	eax, [auxv]
	and	eax, 0x100
	jnz	.flimm
	cmp	dword [tyk], BFA_TK_F32
	je	.ft_t32
	cmp	dword [tyk], BFA_TK_F64
	jne	.ftruncbad		; widening is fext's row
	cmp	dword [stk], BFA_TK_F64
	je	.ft_move
	cmp	dword [stk], BFA_TK_F32
	je	.ftruncbad		; f32 -> f64 is fext
	jmp	.ftruncsrcbad
  .ft_t32:
	cmp	dword [stk], BFA_TK_F32
	je	.ft_move
	cmp	dword [stk], BFA_TK_F64
	je	.ft_narrow
	jmp	.ftruncsrcbad
  .ft_move:
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v
	mov	rdi, [ctx]
	call	__bfc_semi
	return
  .ft_narrow:
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "(float)"
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  ; ---- itof: an integer source, a float destination -----------------------
  .itof:
	mov	eax, [auxv]
	and	eax, 0x100
	jnz	.flimm
	cmp	dword [tyk], BFA_TK_F32
	je	.it_tyok
	cmp	dword [tyk], BFA_TK_F64
	jne	.itofbad
  .it_tyok:
	cmp	dword [stk], BFA_TK_U
	je	.it_u
	cmp	dword [stk], BFA_TK_I
	jne	.itofsrcbad
	; signed: the bits re-read as int64_t, then one C conversion
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "("
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_emit_ctype
	__bfc_emtxt ")exsi_i64_from_bits("
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v
	__bfc_emtxt ")"
	mov	rdi, [ctx]
	call	__bfc_semi
	return
  .it_u:
	cmp	dword [swd], 64
	jne	.it_go
	cmp	dword [tyk], BFA_TK_F32
	je	.itofu64f32
  .it_go:
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "("
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_emit_ctype
	__bfc_emtxt ")"
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  ; ---- ftoi: a float source, an integer destination -----------------------
  .ftoi:
	mov	eax, [auxv]
	and	eax, 0x100
	jnz	.flimm
	cmp	dword [tyk], BFA_TK_U
	je	.ftoi_tyok
	cmp	dword [tyk], BFA_TK_I
	jne	.ftoibad
  .ftoi_tyok:
	cmp	dword [stk], BFA_TK_F32
	je	.ftoi_srcok
	cmp	dword [stk], BFA_TK_F64
	jne	.ftoisrcbad
  .ftoi_srcok:
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_signed
	test	eax, eax
	jnz	.ft_s
	__bfc_emtxt "(uint64_t)"
	jmp	.ft_op
  .ft_s:
	__bfc_emtxt "(uint64_t)(int64_t)"
  .ft_op:
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  ; ---- the refusals, one reason each --------------------------------------
  .fextbad:
	jmp	.after_m1
  .m1 db "fext: f32 -> f64 only (narrowing and equal width are `ftrunc`)"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
  .ftruncbad:
	jmp	.after_m2
  .m2 db "ftrunc: the source must be the same width or wider (f32 -> f64 is `fext`)"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die
  .ftruncsrcbad:
	jmp	.after_m3
  .m3 db "ftrunc: the source must be a float (f32/f64)"
  .after_m3:
	lea	rdi, [.m3]
	mov	rsi, .after_m3 - .m3
	call	__bfc_die
  .itofbad:
	jmp	.after_m4
  .m4 db "itof: the destination must be a float (f32/f64)"
  .after_m4:
	lea	rdi, [.m4]
	mov	rsi, .after_m4 - .m4
	call	__bfc_die
  .itofsrcbad:
	jmp	.after_m5
  .m5 db "itof: the source must be an integer (uN/iN) -- a float source is `ftoi`'s row"
  .after_m5:
	lea	rdi, [.m5]
	mov	rsi, .after_m5 - .m5
	call	__bfc_die
  .itofu64f32:
	jmp	.after_m6
  .m6 db "itof: u64 -> f32 is not implemented (the reference refuses it too: no correctly-rounded one-step sequence at its baseline -- a naive (float)(double) double-rounds)"
  .after_m6:
	lea	rdi, [.m6]
	mov	rsi, .after_m6 - .m6
	call	__bfc_die
  .ftoibad:
	jmp	.after_m7
  .m7 db "ftoi: the destination must be an integer (uN/iN) -- a float destination is `itof`'s row"
  .after_m7:
	lea	rdi, [.m7]
	mov	rsi, .after_m7 - .m7
	call	__bfc_die
  .ftoisrcbad:
	jmp	.after_m8
  .m8 db "ftoi: the source must be a float (f32/f64)"
  .after_m8:
	lea	rdi, [.m8]
	mov	rsi, .after_m8 - .m8
	call	__bfc_die
  .flimm:
	jmp	.after_m9
  .m9 db "fext/ftrunc/itof/ftoi: the operand is a value only (`fconst` is the leaf -- the raw bit pattern, never decimal text)"
  .after_m9:
	lea	rdi, [.m9]
	mov	rsi, .after_m9 - .m9
	call	__bfc_die
  .vecdst:
	call	__bfc_die_vecfloat
endp

; __bfc_emit_fconst(ctx, module, func, instid) -- `fconst F bits`, the leaf.
;
;   vK = exsi_f32_from_bits(UINT64_C(lo));
;   vK = exsi_f64_from_bits(UINT64_C(lo | hi << 32));
;
; The immediate is two 32-bit extra words, lo then hi, read exactly the way
; `iconst` reads its own. THE PATTERN COMES BACK BIT FOR BIT -- that is the
; whole job: a NaN's payload must survive to `fneg` and the differential
; corpus, -0.0 must stay -0.0, and a subnormal must stay itself
; (subnormales conservata) -- none of which a C floating literal can spell,
; so the pattern goes in as an integer constant and comes back through the
; object representation (the prologue's helpers). Decimal here, like every
; other immediate in the emitter, so no emitted text ever carries a hex
; float whose spelling rules could differ between C libraries.
;
; `f32` with a nonzero hi word is refused by name: the pattern is not a
; value of the type, the same refusal `iconst` makes of a non-canonical
; constant, and the reference's row agrees.
proc __bfc_emit_fconst, ctx, module, func, instid
	locals
		slot base, dd
		slot tyv,  dd
		slot kind, dd
		slot cval, dq
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 3
	jne	.badty

	mov	rdi, [func]
	mov	esi, [base]
	call	bfa_extra_get
	mov	[cval], rax		; lo (zero-extended by the 32-bit load)
	mov	rdi, [func]
	mov	eax, [base]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	shl	rax, 32
	or	[cval], rax

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_info
	mov	[kind], edx

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result

	cmp	dword [kind], BFA_TK_F32
	je	.f32
	__bfc_emtxt "exsi_f64_from_bits("
	mov	rdi, [ctx]
	mov	rsi, [cval]
	call	__bfc_emit_u64c
	jmp	.close
  .f32:
	mov	rax, [cval]
	shr	rax, 32
	jnz	.f32wide
	__bfc_emtxt "exsi_f32_from_bits("
	mov	rdi, [ctx]
	mov	rsi, [cval]
	call	__bfc_emit_u64c
  .close:
	__bfc_emtxt ")"
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  .badty:
	jmp	.after_m1
  .m1 db "fconst: the type must be a float (f32/f64) -- `iconst` is the integer leaf (ssa-ir.md 2.3)"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
  .f32wide:
	jmp	.after_m2
  .m2 db "fconst: the pattern is not a value of f32 (bits at or above 2^32)"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die
endp

; ============================================================================
; D4 rows 39, 45-48: slot / addr / index / gaddr / chk, and row 44 copy
; ============================================================================

; __bfc_emit_slot(ctx, func, instid) -- `vK = sK;`. The storage itself is
; `_Alignas(A) unsigned char sK[N];` at the top of the function, emitted by
; __bfc_emit_decls; here the instruction is only the address of it.
proc __bfc_emit_slot, ctx, func, instid
	locals
	endl
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "s"
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfc_semi
	return
endp

; __bfc_emit_gaddr(ctx, func, instid) -- `vK = (unsigned char *)(uintptr_t)exsi_gN;`
; The cast goes through `uintptr_t` so that the `const` on the array (D4 row
; 47: read-only, the reference's `segment readable`) is discarded by an
; explicit integer round-trip rather than by a cast C would warn about. A
; `store` through a `gaddr` is a wrong program in BOTH backends.
proc __bfc_emit_gaddr, ctx, func, instid
	locals
		slot gn, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[gn], ecx
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "(unsigned char *)(uintptr_t)exsi_g"
	mov	rdi, [ctx]
	mov	esi, [gn]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfc_semi
	return
endp

; __bfc_emit_addr(ctx, func, instid) -- `vK = (unsigned char *)((uintptr_t)P + OFF);`
proc __bfc_emit_addr, ctx, func, instid
	locals
		slot pv,  dd
		slot off, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[pv], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[off], ecx
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "(unsigned char *)((uintptr_t)"
	mov	rdi, [ctx]
	mov	esi, [pv]
	call	__bfc_emit_v
	__bfc_emtxt " + "
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	__bfc_emtxt ")"
	mov	rdi, [ctx]
	call	__bfc_semi
	return
endp

; __bfc_emit_index(ctx, func, instid) --
;   vK = (unsigned char *)((uintptr_t)P + (uintptr_t)I * STRIDE);
; Integer arithmetic on the address, NOT C pointer arithmetic: C leaves
; `p + n` undefined outside the object, where the reference merely computes
; a wrong address, and the differential test compares observables -- of
; which "undefined" has none. The `chk` the lowering puts in front is what
; makes the in-bounds case the only one a correct program reaches.
proc __bfc_emit_index, ctx, func, instid
	locals
		slot base, dd
		slot pv,   dd
		slot iv,   dd
		slot strd,   dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	rdi, [func]
	mov	esi, [base]
	call	bfa_extra_get
	mov	[pv], eax
	mov	rdi, [func]
	mov	eax, [base]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[iv], eax
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[strd], eax

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "(unsigned char *)((uintptr_t)"
	mov	rdi, [ctx]
	mov	esi, [pv]
	call	__bfc_emit_v
	__bfc_emtxt " + (uintptr_t)"
	mov	rdi, [ctx]
	mov	esi, [iv]
	call	__bfc_emit_v
	__bfc_emtxt " * "
	mov	rdi, [ctx]
	mov	esi, [strd]
	call	__bfa_out_u64
	__bfc_emtxt ")"
	mov	rdi, [ctx]
	call	__bfc_semi
	return
endp

; __bfc_emit_chk(ctx, module, func, instid) -- `if (I >= N) exsrt_abortus(1);`
; Unsigned, so `i == n` traps (tests/ir/trap_chk.ir pins exactly that, and
; its own mutation note says `jae` written as `ja` is the bug). Kind 1 is
; prelude/README.md's numeric trap, the same kind the reference's shared
; `bfausr_trap` stub carries.
proc __bfc_emit_chk, ctx, module, func, instid
	locals
		slot iv, dd
		slot nv, dd
		slot fl, dd
		slot u64ty, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[iv], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[nv], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[fl], ecx

	; `chk` carries no type token (ir.inc: `ty` is 0), and both operands are
	; already canonical `u64`-shaped quantities; an inline immediate is
	; canonicalised against `u64`, which is the identity on any 32-bit
	; pattern parse.inc could have stored.
	mov	rdi, [module]
	mov	esi, BFA_TK_U
	xor	edx, edx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	mov	[u64ty], eax

	__bfc_emtxt "    if ("
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [u64ty]
	mov	ecx, [iv]
	mov	r8d, [fl]
	and	r8d, 0x100
	call	__bfc_emit_operand
	__bfc_emtxt " >= "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [u64ty]
	mov	ecx, [nv]
	mov	r8d, [fl]
	and	r8d, 0x200
	call	__bfc_emit_operand
	__bfc_emtxt ") exsrt_abortus(1);", 10
	return
endp

; __bfc_emit_copy(ctx, func, instid) -- `__builtin_memcpy(D, S, n);`
; The builtin, not `memcpy`: freestanding C does not promise `<string.h>`,
; and the builtin is what GCC and Clang inline for a constant `n` (measured:
; GCC inlines 144,000 bytes at -Os, Clang emits a call -- c-backend.md
; finding 13). Non-overlapping is the LOWERING's guarantee (distinct slots,
; borrowed sources, ssa-ir.md 2.9); overlap is undefined by BOTH backends
; and the differential test cannot compare it (finding 11).
proc __bfc_emit_copy, ctx, func, instid
	locals
		slot base, dd
		slot n,    dd
		slot dv,   dd
		slot sv,   dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	rdi, [func]
	mov	esi, [base]
	call	bfa_extra_get
	mov	[n], eax
	mov	rdi, [func]
	mov	eax, [base]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[dv], eax
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[sv], eax

	__bfc_emtxt "    __builtin_memcpy("
	mov	rdi, [ctx]
	mov	esi, [dv]
	call	__bfc_emit_v
	__bfc_emtxt ", "
	mov	rdi, [ctx]
	mov	esi, [sv]
	call	__bfc_emit_v
	__bfc_emtxt ", "
	mov	rdi, [ctx]
	mov	esi, [n]
	call	__bfa_out_u64
	__bfc_emtxt ");", 10
	return
endp

; ============================================================================
; D4 rows 40-41: load / store
; ============================================================================

; __bfc_emit_ldname(ctx, ord) -- `exsi_ld_n` / `_be` / `_le`.
proc __bfc_emit_ldname, ctx, ord
	locals
	endl
	cmp	dword [ord], BFA_ORDER_MAIOR
	je	.be
	cmp	dword [ord], BFA_ORDER_MINOR
	je	.le
	__bfc_emtxt "exsi_ld_n"
	return
  .be:
	__bfc_emtxt "exsi_ld_be"
	return
  .le:
	__bfc_emtxt "exsi_ld_le"
	return
endp

proc __bfc_emit_stname, ctx, ord
	locals
	endl
	cmp	dword [ord], BFA_ORDER_MAIOR
	je	.be
	cmp	dword [ord], BFA_ORDER_MINOR
	je	.le
	__bfc_emtxt "exsi_st_n"
	return
  .be:
	__bfc_emtxt "exsi_st_be"
	return
  .le:
	__bfc_emtxt "exsi_st_le"
	return
endp

; __bfc_mem_k(module, ty) -> eax = the byte count of one access, having
; refused a width that is not a whole number of bytes (the reference
; refuses the same). An address type answers 0, meaning "sizeof(void *)":
; the emitted text never assumes a pointer width, it asks C for it, and the
; per-`--hospes` `_Static_assert` in the prologue is what pins the answer.
proc __bfc_mem_k, module, ty
	locals
		slot nbits, dd
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_class
	cmp	eax, 1
	je	.isaddr
	cmp	eax, 2
	je	.bad
	cmp	eax, 3
	je	.isfloat
	cmp	eax, 4
	je	.isvec
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_info
	mov	[nbits], eax
	xor	edx, edx
	mov	ecx, 8
	div	ecx			; eax = N/8, edx = N%8
	test	edx, edx
	jnz	.notbytes
	return
  .isaddr:
	xor	eax, eax
	return
  .isvec:
	; lanes x element bytes, exactly the reference's `__bfa_vtype_bytes`
	; (8/16/32 for vf32.2/4/8, 16/32/64 for vf64.2/4/8): a vector's width
	; field is 0 CANONICALLY -- kind+ref carry everything (ir.inc's
	; BFA_TK_VF32 comment) -- so the N/8 path cannot answer.
	mov	rdi, [module]
	mov	esi, [ty]
	call	bfa_type_ptr
	mov	ecx, [rax + BfaType.ref]	; the lane count (2/4/8)
	mov	edx, 4
	cmp	byte [rax + BfaType.kind], BFA_TK_VF32
	je	.vmul
	mov	edx, 8
  .vmul:
	mov	eax, ecx
	imul	eax, edx
	return
  .isfloat:
	; a float's width field is 0 (floats are sized by KIND), so the N/8
	; path cannot answer: f64 is 8 bytes, f32 4. The load/store floats are
	; bit baggage to the byte helpers either way.
	mov	rdi, [module]
	mov	esi, [ty]
	call	bfa_type_ptr
	cmp	byte [rax + BfaType.kind], BFA_TK_F64
	je	.isf64
	mov	eax, 4
	return
  .isf64:
	mov	eax, 8
	return
  .notbytes:
	jmp	.after_m1
  .m1 db "load/store: a width that is not a whole number of bytes is not implemented (the reference refuses it too)"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
  .bad:
	jmp	.after_m2
  .m2 db "load/store: the accessed type must be an integer (uN/iN), a float (f32/f64), a vector float (vf32.N/vf64.N) or an address (floats and vectors in memory are bit baggage under the prologue's helpers, nativus only -- byte order is an integer surface, spec 5.2)"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die
endp

; __bfc_emit_memk(ctx, module, ty) -- the access size argument: a literal
; byte count, or `sizeof(void *)` for an address.
proc __bfc_emit_memk, ctx, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_mem_k
	test	eax, eax
	jz	.ptr
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	return
  .ptr:
	__bfc_emtxt "sizeof(void *)"
	return
endp

; __bfc_emit_load(ctx, module, func, instid) -- `load T %p off order`.
;
;   integer   vK = normT(exsi_ld_X(P + off, k), N);
;   ptr       vK = (unsigned char *)(uintptr_t)exsi_ld_n(P + off, sizeof(void *));
;
; The byte helpers are why the emitted text depends on NO assumption about
; the C host's byte order and asserts none: `maior` and `minor` are read
; byte-at-a-time with explicit shifts, and `nativus` is a macro the prologue
; binds once from `__BYTE_ORDER__` at C-compile time. Spec 5.2's case --
; a little-endian format read on a big-endian machine -- is therefore one
; helper call, not a conditional.
proc __bfc_emit_load, ctx, module, func, instid
	locals
		slot base, dd
		slot pv,   dd
		slot off,  dd
		slot ord,  dd
		slot tyv,  dd
		slot lanes, dd	; the vector arm's lane count across __bfc_emtxt (its call clobbers rcx)
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	mov	rdi, [func]
	mov	esi, [base]
	call	bfa_extra_get
	mov	[pv], eax
	mov	rdi, [func]
	mov	eax, [base]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[off], eax
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[ord], eax

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 1
	je	.addr
	cmp	eax, 3
	je	.float
	cmp	eax, 4
	je	.vec

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_norm_open
	jmp	.access
  .addr:
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "(unsigned char *)(uintptr_t)"
  .access:
	mov	rdi, [ctx]
	mov	esi, [ord]
	call	__bfc_emit_ldname
	__bfc_emtxt "("
	mov	rdi, [ctx]
	mov	esi, [pv]
	call	__bfc_emit_v
	__bfc_emtxt " + "
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	__bfc_emtxt ", "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_emit_memk
	mov	rdi, [ctx]
	mov	esi, ')'
	call	__bfa_out_byte

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 1
	je	.done
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_norm_close
  .done:
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  .float:
	; `vK = exsi_f64_from_bits(exsi_ld_n(P + off, 8));` -- the byte helper
	; reads the pattern as on every other host's terms, the bit-cast reads
	; it as the double those bits ARE (the prologue's own byte-order
	; assumption). No FP operation runs: the one-op-per-statement rule has
	; nothing to protect here, and NaN payloads, -0.0 and subnormals come
	; through untouched. `maior`/`minor` on a float dies below with the
	; reference's own words -- byte order is an integer surface (spec 5.2).
	cmp	dword [ord], BFA_ORDER_NATIVUS
	jne	.ford
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	mov	rdi, [module]
	mov	esi, [tyv]
	call	bfa_type_ptr
	cmp	byte [rax + BfaType.kind], BFA_TK_F64
	je	.f64ld
	__bfc_emtxt "exsi_f32_from_bits((uint32_t)"
	jmp	.facc
  .f64ld:
	__bfc_emtxt "exsi_f64_from_bits("
  .facc:
	mov	rdi, [ctx]
	mov	esi, [ord]
	call	__bfc_emit_ldname
	__bfc_emtxt "("
	mov	rdi, [ctx]
	mov	esi, [pv]
	call	__bfc_emit_v
	__bfc_emtxt " + "
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	__bfc_emtxt ", "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_emit_memk
	__bfc_emtxt "))"
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  .vec:
	; `vK = exsi_ld_vf32_4(P + off);` -- the prologue's memcpy helper, the
	; scalar float load's bit-baggage pattern generalised past 64 bits:
	; lanes x element bytes copied as BYTES, no lane read numerically, so
	; a NaN payload, -0.0's sign and a subnormal's bits survive
	; (vec_mem.ir pins all three). `maior`/`minor` on a vector dies with
	; the float arm's own words below (the verifier rules it out first --
	; reject_verify_vec_load_ord.ir; this is the same refusal by name on
	; the `exsc` path that reaches the emitter without the verifier).
	cmp	dword [ord], BFA_ORDER_NATIVUS
	jne	.ford
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	mov	rdi, [module]
	mov	esi, [tyv]
	call	bfa_type_ptr
	mov	ecx, [rax + BfaType.ref]	; the lane count (2/4/8)
	mov	[lanes], ecx			; out of rcx before __bfc_emtxt's call
	cmp	byte [rax + BfaType.kind], BFA_TK_VF32
	jne	.v64ld
	__bfc_emtxt "exsi_ld_vf32_"
	jmp	.vlname
  .v64ld:
	__bfc_emtxt "exsi_ld_vf64_"
  .vlname:
	mov	rdi, [ctx]
	mov	esi, [lanes]
	call	__bfa_out_u64
	__bfc_emtxt "("
	mov	rdi, [ctx]
	mov	esi, [pv]
	call	__bfc_emit_v
	__bfc_emtxt " + "
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	__bfc_emtxt ")"
	mov	rdi, [ctx]
	call	__bfc_semi
	return
  .ford:
	jmp	.after_fordmsg
  .fordmsg db "load/store: `maior`/`minor` order an integer's bytes; this is not a uN/iN"
  .after_fordmsg:
	lea	rdi, [.fordmsg]
	mov	rsi, .after_fordmsg - .fordmsg
	call	__bfc_die
	return
endp

; __bfc_emit_store(ctx, module, func, instid) -- `store T %p off order %v`.
proc __bfc_emit_store, ctx, module, func, instid
	locals
		slot base, dd
		slot pv,   dd
		slot off,  dd
		slot ord,  dd
		slot vv,   dd
		slot tyv,  dd
		slot lanes, dd	; the vector arm's lane count across __bfc_emtxt (its call clobbers rcx)
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	mov	rdi, [func]
	mov	esi, [base]
	call	bfa_extra_get
	mov	[pv], eax
	mov	rdi, [func]
	mov	eax, [base]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[off], eax
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[ord], eax
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, 3
	mov	esi, eax
	call	bfa_extra_get
	mov	[vv], eax

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 4
	je	.vstore

	__bfc_emtxt "    "
	mov	rdi, [ctx]
	mov	esi, [ord]
	call	__bfc_emit_stname
	__bfc_emtxt "("
	mov	rdi, [ctx]
	mov	esi, [pv]
	call	__bfc_emit_v
	__bfc_emtxt " + "
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	__bfc_emtxt ", "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_emit_memk
	__bfc_emtxt ", "
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 1
	jne	.plainval
	__bfc_emtxt "(uint64_t)(uintptr_t)"
	jmp	.value
  .plainval:
	cmp	eax, 3
	jne	.value
	; `exsi_st_n(P + off, 8, exsi_bits_from_f64(V));` -- bits to bytes, the
	; exact inverse of the load arm; `maior`/`minor` on a float dies with
	; the same words the reference uses.
	cmp	dword [ord], BFA_ORDER_NATIVUS
	jne	.sf_ord
	mov	rdi, [module]
	mov	esi, [tyv]
	call	bfa_type_ptr
	cmp	byte [rax + BfaType.kind], BFA_TK_F64
	je	.f64st
	__bfc_emtxt "exsi_bits_from_f32("
	jmp	.fval
  .f64st:
	__bfc_emtxt "exsi_bits_from_f64("
  .fval:
	mov	rdi, [ctx]
	mov	esi, [vv]
	call	__bfc_emit_v
	__bfc_emtxt "));", 10
	return
  .value:
	mov	rdi, [ctx]
	mov	esi, [vv]
	call	__bfc_emit_v
	__bfc_emtxt ");", 10
	return
  .sf_ord:
	jmp	.after_sfordmsg
  .sfordmsg db "load/store: `maior`/`minor` order an integer's bytes; this is not a uN/iN"
  .after_sfordmsg:
	lea	rdi, [.sfordmsg]
	mov	rsi, .after_sfordmsg - .sfordmsg
	call	__bfc_die
	return

  .vstore:
	; `exsi_st_vf32_4(P + off, V);` -- the vector load arm's exact inverse
	; (the prologue's memcpy helper; bit baggage, no lane read). `maior`/
	; `minor` on a vector dies above with the load/store order refusal --
	; byte order is an integer surface (spec 5.2), which is the reference's
	; own words for the same text (reject_verify_vec_store_ord.ir pins the
	; verifier's copy of the rule).
	cmp	dword [ord], BFA_ORDER_NATIVUS
	jne	.sf_ord
	__bfc_emtxt "    "
	mov	rdi, [module]
	mov	esi, [tyv]
	call	bfa_type_ptr
	mov	ecx, [rax + BfaType.ref]	; the lane count (2/4/8)
	mov	[lanes], ecx			; out of rcx before __bfc_emtxt's call
	cmp	byte [rax + BfaType.kind], BFA_TK_VF32
	jne	.v64st
	__bfc_emtxt "exsi_st_vf32_"
	jmp	.vsname
  .v64st:
	__bfc_emtxt "exsi_st_vf64_"
  .vsname:
	mov	rdi, [ctx]
	mov	esi, [lanes]
	call	__bfa_out_u64
	__bfc_emtxt "("
	mov	rdi, [ctx]
	mov	esi, [pv]
	call	__bfc_emit_v
	__bfc_emtxt " + "
	mov	rdi, [ctx]
	mov	esi, [off]
	call	__bfa_out_u64
	__bfc_emtxt ", "
	mov	rdi, [ctx]
	mov	esi, [vv]
	call	__bfc_emit_v
	__bfc_emtxt ");", 10
	return
endp

; ============================================================================
; D4 rows 42-43: loadbits / storebits
; ============================================================================

; __bfc_bits_plan(module, func, instid) -> eax = S (the right-shift amount
; 8 - bit - w), edx = the width w. Refuses, by name and with the reference's
; own three reasons, a signed field, a width outside 1..8, and a field that
; STRADDLES a byte boundary -- which is parity with
; tests/ir/reject_emit_straddle.ir, a fixture whose whole point is that the
; verifier admits such a field (ssa-ir.md 2.7 says a declaration may) and
; the emitter refuses it.
proc __bfc_bits_plan, module, func, instid
	locals
		slot base, dd
		slot bio,  dd
		slot w,    dd
		slot tyv,  dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[bio], eax

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_info		; eax = width, edx = kind
	cmp	edx, BFA_TK_U
	jne	.notu
	mov	[w], eax
	cmp	eax, 1
	jb	.badw
	cmp	eax, 8
	ja	.badw
	mov	eax, [bio]
	add	eax, [w]
	cmp	eax, 8
	ja	.straddle
	mov	eax, 8
	sub	eax, [bio]
	sub	eax, [w]
	mov	edx, [w]
	return

  .notu:
	jmp	.after_m1
  .m1 db "loadbits/storebits: a signed (iN) bit field is not implemented (spec 5.2: wire fields are unsigned)"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
  .badw:
	jmp	.after_m2
  .m2 db "loadbits/storebits: field width must be 1..8"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die
  .straddle:
	jmp	.after_m3
  .m3 db "loadbits/storebits: a field that straddles a byte boundary is not implemented (DeModFrame never straddles)"
  .after_m3:
	lea	rdi, [.m3]
	mov	rsi, .after_m3 - .m3
	call	__bfc_die
endp

; __bfc_emit_byteref(ctx, func, base) -- `P[byte]`, the one byte a bit field
; lives in.
proc __bfc_emit_byteref, ctx, func, base
	locals
		slot pv, dd
		slot by, dd
	endl
	mov	rdi, [func]
	mov	esi, [base]
	call	bfa_extra_get
	mov	[pv], eax
	mov	rdi, [func]
	mov	eax, [base]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[by], eax
	mov	rdi, [ctx]
	mov	esi, [pv]
	call	__bfc_emit_v
	__bfc_emtxt "["
	mov	rdi, [ctx]
	mov	esi, [by]
	call	__bfa_out_u64
	__bfc_emtxt "]"
	return
endp

; __bfc_emit_loadbits(ctx, module, func, instid) --
;   vK = ((unsigned)P[byte] >> S) & Mu;
; The byte is cast to `unsigned` BEFORE the shift, never promoted into
; `int`: ADR 0012 item 4 made "no promotion of a byte into int" a rule, and
; a shift of an `int` whose sign bit could be set is exactly what it is a
; rule about. S is at most 7 and the mask fits in 8 bits, so nothing here
; can shift by a type's width either. MSB-first, spec 5.2 rule 1.
proc __bfc_emit_loadbits, ctx, module, func, instid
	locals
		slot base, dd
		slot s,    dd
		slot w,    dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	rdi, [module]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_bits_plan
	mov	[s], eax
	mov	[w], edx

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "(((unsigned)"
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [base]
	call	__bfc_emit_byteref
	__bfc_emtxt " >> "
	mov	rdi, [ctx]
	mov	esi, [s]
	call	__bfa_out_u64
	__bfc_emtxt ") & "
	mov	rdi, [ctx]
	mov	esi, [w]
	call	__bfc_emit_bitmask
	__bfc_emtxt ")"
	mov	rdi, [ctx]
	call	__bfc_semi
	return
endp

; __bfc_emit_bitmask(ctx, w) -- `(2^w - 1)u` as a decimal literal with the
; `u` suffix, so the whole expression stays in `unsigned` arithmetic.
proc __bfc_emit_bitmask, ctx, w
	locals
	endl
	mov	ecx, [w]
	mov	eax, 1
	shl	eax, cl
	dec	eax
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_u64
	mov	rdi, [ctx]
	mov	esi, 'u'
	call	__bfa_out_byte
	return
endp

; __bfc_emit_storebits(ctx, module, func, instid) --
;   P[byte] = (unsigned char)(((unsigned)P[byte] & ~(Mu << S)) | (((unsigned)V & Mu) << S));
; A read-modify-write of ONE byte, the neighbours untouched -- never a C
; bitfield, whose allocation order, straddling and unit alignment are all
; implementation-defined (ADR 0012 item 4).
proc __bfc_emit_storebits, ctx, module, func, instid
	locals
		slot base, dd
		slot s,    dd
		slot w,    dd
		slot vv,   dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, 3
	mov	esi, eax
	call	bfa_extra_get
	mov	[vv], eax
	mov	rdi, [module]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_bits_plan
	mov	[s], eax
	mov	[w], edx

	__bfc_emtxt "    "
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [base]
	call	__bfc_emit_byteref
	__bfc_emtxt " = (unsigned char)(((unsigned)"
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [base]
	call	__bfc_emit_byteref
	__bfc_emtxt " & ~("
	mov	rdi, [ctx]
	mov	esi, [w]
	call	__bfc_emit_bitmask
	__bfc_emtxt " << "
	mov	rdi, [ctx]
	mov	esi, [s]
	call	__bfa_out_u64
	__bfc_emtxt ")) | (((unsigned)"
	mov	rdi, [ctx]
	mov	esi, [vv]
	call	__bfc_emit_v
	__bfc_emtxt " & "
	mov	rdi, [ctx]
	mov	esi, [w]
	call	__bfc_emit_bitmask
	__bfc_emtxt ") << "
	mov	rdi, [ctx]
	mov	esi, [s]
	call	__bfa_out_u64
	__bfc_emtxt "));", 10
	return
endp

; ============================================================================
; D4 rows 51, 58, 59: call / param / iconst
; ============================================================================

; __bfc_emit_param(ctx, module, func, instid) -- `vK = pI;`. The C
; parameter itself is in the signature; this is the copy into the value's
; own name, which is what keeps every later instruction reading only `vN`.
; The parameter itself is never normalised: a caller of an emitted function
; is emitted code passing canonical slots, or the shim's entry, which
; passes one pointer -- the reference's own reasoning, verbatim
; (emit.inc's `__bfa_emit_call` header, the externus paragraph).
;
; REFUSED, by name, with the reference's two refusals for this instruction:
; the index past the signature's parameters, and ANY parameter at or ahead
; of this one being a vf32.N/vf64.N -- the reference walks the signature
; from 0 because a vector param ahead would disturb the SysV ordinal walk
; (`__bfa_emit_param`'s `.vecparam`), and library mode has exactly the same
; v1 reason: one C parameter is one scalar's home.
proc __bfc_emit_param, ctx, module, func, instid
	uses	rbx
	locals
		slot idx,   dd
		slot sigpf, dd
		slot sigpc, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[idx], ecx

	; the signature walk: types 0..idx of Module.sig_extra, in order
	mov	rax, [func]
	mov	ecx, [rax + BfaFunc.sig]
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sigs]
	mov	esi, ecx			; 0-based, unlike every other id
	call	vec_get
	mov	ecx, [rax + BfaSig.p_first]
	mov	[sigpf], ecx
	mov	ecx, [rax + BfaSig.p_count]
	mov	[sigpc], ecx
	mov	eax, [idx]
	cmp	eax, [sigpc]
	jae	.oor
	xor	ebx, ebx
  .pwalk:
	cmp	ebx, [idx]
	ja	.pwalkdone
	mov	eax, [sigpf]
	add	eax, ebx
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sig_extra]
	mov	esi, eax
	call	vec_get
	mov	esi, [rax]
	mov	rdi, [module]
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_VF32
	je	.vecp
	cmp	ecx, BFA_TK_VF64
	je	.vecp
	inc	ebx
	jmp	.pwalk
  .pwalkdone:

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	__bfc_emtxt "p"
	mov	rdi, [ctx]
	mov	esi, [idx]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  .oor:
	jmp	.after_m1
  .m1 db "param: the index is past the signature's parameters (the verifier does not check one)"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
  .vecp:
	jmp	.after_m2
  .m2 db "param of a vector type: v1 keeps vectors inside a function (scalars store lanes; indexing reads them) [OPEN]"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die
endp

; __bfc_emit_iconst(ctx, module, func, instid) -- `vK = UINT64_C(pattern);`
; The immediate is two 32-bit extra words, lo then hi, and it is refused by
; name if it is not a value of its type -- the same check emit.inc makes,
; here for the same reason: `exsc` reaches the emitter without the verifier.
proc __bfc_emit_iconst, ctx, module, func, instid
	locals
		slot base, dd
		slot tyv,  dd
		slot cval, dq
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx
	mov	rdi, [func]
	mov	esi, [base]
	call	bfa_extra_get
	mov	[cval], rax		; lo (zero-extended by the 32-bit load)
	mov	rdi, [func]
	mov	eax, [base]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	shl	rax, 32
	or	[cval], rax

	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 2
	je	.isfloat
	cmp	eax, 3
	je	.isfloat		; a float iconst would slip the canon check
					; (bfa_type_canon is the identity on a
					; float's bits), so it is stopped HERE

	mov	rdi, [module]
	mov	esi, [tyv]
	mov	rdx, [cval]
	call	bfa_type_canon
	cmp	rax, [cval]
	jne	.notcanon

	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	mov	rdi, [ctx]
	mov	rsi, [cval]
	call	__bfc_emit_u64c
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  .notcanon:
	jmp	.after_m1
  .m1 db "iconst: the constant is not a value of its type (ssa-ir.md 2.2)"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
  .isfloat:
	jmp	.after_m2
  .m2 db "iconst: a float constant is `fconst`, which takes the RAW bit pattern (ssa-ir.md 2.3) -- an `iconst` with a float type is a leaf spelled with the wrong opcode"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die
endp

; __bfc_emit_call(ctx, module, func, instid) -- `[vK =] name(a1, a2);`
;
; THE NARROW-RESULT NORMALISATION is the one place this lowering has to do
; something the C type system would otherwise hide. A BODILESS callee's C
; prototype is written from its DECLARED return type, and what actually
; comes back is whatever the foreign definition returns -- the C mirror of
; SysV leaving the bits of `rax` above a u8/u16/u32 return unspecified. Every
; instruction after the call assumes the canonical form (ssa-ir.md 2.2), so
; the result of a call to an `externus` callee is normalised, exactly where
; and only where emit.inc's `__bfa_emit_call` does it.
; tests/ir/abi_narrow_result.ir is the fixture that catches its absence.
; A callee this emitter WROTE needs none: its `return` hands back a value
; that is canonical already.
proc __bfc_emit_call, ctx, module, func, instid
	uses	rbx
	locals
		slot base, dd
		slot cnt,  dd
		slot tyv,  dd
		slot idv,  dd
		slot ext,  dd
		slot av,   dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[cnt], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[tyv], ecx

	mov	rdi, [func]
	mov	esi, [base]
	call	bfa_extra_get
	mov	[idv], eax

	; is the callee a bodiless declaration?
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, [idv]
	dec	esi
	call	vec_get
	mov	rax, [rax]
	mov	ecx, [rax + BfaFunc.attrs]
	and	ecx, BFA_FUNC_ATTR_EXTERNUS
	mov	[ext], ecx

	; VECTOR refusals, with the reference's own words and its own order
	; (emit.inc's `__bfa_emit_call`: the argument walk first, the result
	; second): a lane group has no v1 crossing of a call boundary, and a
	; C prototype spelled with the vector typedef would compile the
	; miscommunication rather than refuse it -- the refusal is the
	; emitter's, by name.
	mov	ebx, 1
  .vckloop:
	mov	eax, ebx
	cmp	eax, [cnt]
	jae	.vckdone
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, ebx
	mov	esi, eax
	call	bfa_extra_get
	; edx = the argument's value id; classify by its own type
	mov	rdi, [module]
	mov	rsi, [func]
	mov	edx, eax
	call	__bfc_valty
	mov	rdi, [module]
	mov	esi, eax
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_VF32
	je	.vecarg
	cmp	ecx, BFA_TK_VF64
	je	.vecarg
	inc	ebx
	jmp	.vckloop
  .vckdone:
	cmp	dword [tyv], 0
	je	.vresdone
	mov	rdi, [module]
	mov	esi, [tyv]
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_VF32
	je	.vecresult
	cmp	ecx, BFA_TK_VF64
	je	.vecresult
  .vresdone:

	cmp	dword [tyv], 0
	je	.voidcall
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_result
	cmp	dword [ext], 0
	je	.name
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	test	eax, eax
	jnz	.name			; an address result has no narrow form
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_norm_open
	jmp	.name

  .voidcall:
	__bfc_emtxt "    "

  .name:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [idv]
	call	__bfc_emit_calleename
	__bfc_emtxt "("

	; arguments: extra[base+1 .. base+cnt-1], in order
	mov	ebx, 1
  .argloop:
	mov	eax, ebx
	cmp	eax, [cnt]
	jae	.argsdone
	cmp	ebx, 1
	je	.noComma
	__bfc_emtxt ", "
  .noComma:
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, ebx
	mov	esi, eax
	call	bfa_extra_get
	mov	[av], eax
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v
	inc	ebx
	jmp	.argloop
  .argsdone:
	__bfc_emtxt ")"

	cmp	dword [tyv], 0
	je	.fin
	cmp	dword [ext], 0
	je	.fin
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	test	eax, eax
	jnz	.fin
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_norm_close
  .fin:
	mov	rdi, [ctx]
	call	__bfc_semi
	return

  .vecarg:
	jmp	.after_mva
  .mva db "call with a vector argument: v1 keeps vectors inside a function (scalars store lanes; indexing reads them) [OPEN]"
  .after_mva:
	lea	rdi, [.mva]
	mov	rsi, .after_mva - .mva
	call	__bfc_die
  .vecresult:
	jmp	.after_mvr
  .mvr db "call with a vector result type: v1 keeps vectors inside a function (scalars store lanes; indexing reads them) [OPEN]"
  .after_mvr:
	lea	rdi, [.mvr]
	mov	rsi, .after_mvr - .mvr
	call	__bfc_die
endp

; ============================================================================
; D4 rows 53-57: the terminators, and the phi parallel copy
; ============================================================================

; __bfc_phi_operand(func, phiv, pblk, skipn) -> eax = the value id this phi
; takes on the edge from block `pblk`. The operands are `(bP, %v)` word
; pairs; `skipn` matching pairs are passed over first, which is nonzero only
; for a `br` whose two targets are the same block (tests/ir/
; phi_same_target.ir: the true edge takes the first pair naming this
; predecessor and the false edge the second, in edge order).
proc __bfc_phi_operand, func, phiv, pblk, skipn
	uses	rbx, r12
	locals
		slot eidx, dd
		slot ecnt, dd
		slot skips, dd
	endl
	mov	rdi, [func]
	mov	esi, [phiv]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[ecnt], ecx
	mov	ecx, [skipn]
	mov	[skips], ecx

	xor	ebx, ebx
  .loop:
	mov	eax, ebx
	cmp	eax, [ecnt]
	jae	.none
	mov	rdi, [func]
	mov	eax, [eidx]
	add	eax, ebx
	mov	esi, eax
	call	bfa_extra_get
	cmp	eax, [pblk]
	jne	.next
	cmp	dword [skips], 0
	je	.found
	dec	dword [skips]
  .next:
	add	ebx, 2
	jmp	.loop
  .found:
	mov	rdi, [func]
	mov	eax, [eidx]
	add	eax, ebx
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	return
  .none:
	jmp	.after_msg
  .msg db "phi: no operand for this predecessor (verifier rule 3)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfc_die
endp

; __bfc_emit_edge_copies(ctx, module, func, pblk, tblk, skipn) -- D4 row 57's
; PARALLEL COPY, emitted on one CFG edge.
;
;   tj = <operand for this predecessor>;   for every phi j of tblk, in order
;   vj = tj;                                for every phi j, in the same order
;
; EVERY READ PRECEDES EVERY WRITE, so the swap case (tests/ir/phi_swap.ir)
; and the lost-copy case (phi_lost_copy.ir) are both correct with no cycle
; analysis at all -- the temporaries are function-scope and cost one
; declaration each. The reference achieves the same thing with a push/pop
; pair per phi; the shapes differ, the semantics do not, which is exactly
; what the differential test is for.
;
; Emits NOTHING when the target has no phis, so a phi-free program's text
; carries no trace of this machinery.
; `indent` is fixed at four spaces here and eight inside a `br`'s arms;
; the caller passes the text it wants through `__bfc_emit_indent`.
proc __bfc_emit_edge_copies, ctx, module, func, pblk, tblk, skipn
	locals
		slot cur,  dd
		slot pv,   dd
		slot deep, dd
		slot skp,  dd
	endl
	; `proc` tops out at six arguments and this routine already needs six,
	; so the indent depth rides in bit 16 of `skipn` rather than becoming a
	; seventh. Unpacked once, here, into two locals; no caller sees the
	; packing except __bfc_emit_br, which does it.
	mov	ecx, [skipn]
	mov	eax, ecx
	shr	eax, 16
	mov	[deep], eax
	and	ecx, 0xFFFF
	mov	[skp], ecx

	mov	rdi, [func]
	mov	esi, [tblk]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_phi]
	mov	[cur], ecx

	; ---- pass 1: read every operand into its temporary -------------------
  .readloop:
	cmp	dword [cur], 0
	je	.readdone
	; a phi OVER a vector value has no v1 lowering, exactly as the
	; reference's push/pop arm records (emit.inc's edge-copy loop): the
	; parallel copy here is one temporary of the value's type, and a lane
	; group crossing a block boundary is what v1 does not do -- refused by
	; name; scalars store lanes, indexing reads them (ssa-ir.md 2.6).
	mov	rdi, [func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	esi, [rax + BfaInst.ty]
	mov	rdi, [module]
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_VF32
	je	.vecphi
	cmp	ecx, BFA_TK_VF64
	je	.vecphi
  .vecchecked:
	mov	rdi, [func]
	mov	esi, [cur]
	mov	edx, [pblk]
	mov	ecx, [skp]
	call	__bfc_phi_operand
	mov	[pv], eax
	mov	rdi, [ctx]
	mov	esi, [deep]
	call	__bfc_emit_indent
	__bfc_emtxt "t"
	mov	rdi, [ctx]
	mov	esi, [cur]
	call	__bfa_out_u64
	__bfc_emtxt " = "
	mov	rdi, [ctx]
	mov	esi, [pv]
	call	__bfc_emit_v
	mov	rdi, [ctx]
	call	__bfc_semi
	mov	rdi, [func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.readloop
  .readdone:

	; ---- pass 2: write every temporary into its phi ----------------------
	mov	rdi, [func]
	mov	esi, [tblk]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_phi]
	mov	[cur], ecx
  .writeloop:
	cmp	dword [cur], 0
	je	.writedone
	mov	rdi, [ctx]
	mov	esi, [deep]
	call	__bfc_emit_indent
	mov	rdi, [ctx]
	mov	esi, [cur]
	call	__bfc_emit_v
	__bfc_emtxt " = t"
	mov	rdi, [ctx]
	mov	esi, [cur]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfc_semi
	mov	rdi, [func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.writeloop
  .writedone:
	return
  .vecphi:
	jmp	.after_mvp
  .mvp db "phi of a vector value: v1 keeps vectors inside a basic block (scalars store lanes; indexing reads them) [OPEN]"
  .after_mvp:
	lea	rdi, [.mvp]
	mov	rsi, .after_mvp - .mvp
	call	__bfc_die
endp

; __bfc_emit_indent(ctx, deep) -- four spaces, or eight inside a `br` arm.
proc __bfc_emit_indent, ctx, deep
	locals
	endl
	cmp	dword [deep], 0
	jne	.eight
	__bfc_emtxt "    "
	return
  .eight:
	__bfc_emtxt "        "
	return
endp

; __bfc_emit_ret(ctx, func, instid) -- `return vK;` or `return;`.
proc __bfc_emit_ret, ctx, module, func, instid
	locals
		slot av, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[av], ecx
	cmp	ecx, 0
	je	.bare

	; a VECTOR result has no v1 spelling: nothing here can hand a lane
	; group across the function boundary (the reference's SysV two-
	; register contract, library mode's `return vK`), so it is refused by
	; name, with the reference's own words -- emit.inc's `__bfa_emit_ret`
	; `.vec` arm. Scalars store lanes; indexing reads them.
	mov	rdi, [module]
	mov	rsi, [func]
	mov	edx, [av]
	call	__bfc_valty
	mov	rdi, [module]
	mov	esi, eax
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_VF32
	je	.vec
	cmp	ecx, BFA_TK_VF64
	je	.vec

	__bfc_emtxt "    return "
	mov	rdi, [ctx]
	mov	esi, [av]
	call	__bfc_emit_v
	mov	rdi, [ctx]
	call	__bfc_semi
	return
  .bare:
	__bfc_emtxt "    return;", 10
	return
  .vec:
	jmp	.after_m1
  .m1 db "ret of a vector value: v1 keeps vectors inside a function (scalars store lanes; indexing reads them) [OPEN]"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
endp

; __bfc_emit_jmp(ctx, module, func, instid) -- the edge's parallel copy,
; then `goto bT;`. A C `goto` needs no per-edge stub, which is the one place
; the C lowering is structurally simpler than the reference's.
proc __bfc_emit_jmp, ctx, module, func, instid
	locals
		slot btgt, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[btgt], ecx

	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	rax, [ctx]
	mov	ecx, [rax + BfaPrintCtx.curblk]
	mov	r8d, [btgt]
	xor	r9, r9
	call	__bfc_emit_edge_copies

	__bfc_emtxt "    goto b"
	mov	rdi, [ctx]
	mov	esi, [btgt]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfc_semi
	return
endp

; __bfc_emit_br(ctx, module, func, instid) --
;   if (vC) {
;       <copies for the true edge>
;       goto bT;
;   } else {
;       <copies for the false edge>
;       goto bF;
;   }
; When bT == bF the true edge takes the phi's FIRST pair naming this
; predecessor and the false edge the SECOND, in edge order -- the `skipn`
; argument, and the reason tests/ir/phi_same_target.ir exists.
proc __bfc_emit_br, ctx, module, func, instid
	locals
		slot base, dd
		slot cv,   dd
		slot btgt, dd
		slot bf,   dd
		slot pcur, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[base], ecx
	mov	rdi, [func]
	mov	esi, [base]
	call	bfa_extra_get
	mov	[cv], eax
	mov	rdi, [func]
	mov	eax, [base]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[btgt], eax
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[bf], eax
	mov	rax, [ctx]
	mov	ecx, [rax + BfaPrintCtx.curblk]
	mov	[pcur], ecx

	__bfc_emtxt "    if ("
	mov	rdi, [ctx]
	mov	esi, [cv]
	call	__bfc_emit_v
	__bfc_emtxt ") {", 10

	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [pcur]
	mov	r8d, [btgt]
	mov	r9d, 0x10000		; skipn = 0, indent deep
	call	__bfc_emit_edge_copies
	__bfc_emtxt "        goto b"
	mov	rdi, [ctx]
	mov	esi, [btgt]
	call	__bfa_out_u64
	__bfc_emtxt ";", 10, "    } else {", 10

	mov	eax, 0x10000		; indent deep
	mov	ecx, [btgt]
	cmp	ecx, [bf]
	jne	.noskip
	inc	eax			; same target: take the SECOND pair
  .noskip:
	push	rax
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [pcur]
	mov	r8d, [bf]
	pop	r9
	call	__bfc_emit_edge_copies
	__bfc_emtxt "        goto b"
	mov	rdi, [ctx]
	mov	esi, [bf]
	call	__bfa_out_u64
	__bfc_emtxt ";", 10, "    }", 10
	return
endp

; __bfc_emit_trap(ctx, module, func, instid) -- `exsrt_abortus(5);` for
; `terminus`, and a refusal by name for any other kind word. Parity with
; emit.inc, which refuses the same set for the same reason: a kind with no
; runtime meaning must not become a silently different abort.
proc __bfc_emit_trap, ctx, module, func, instid
	locals
		slot kidv, dd
		slot kp,   dq
		slot kl,   dq
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
	mov	[kp], rax
	mov	[kl], rdx

	jmp	.after_word
  .word db "terminus"
  .after_word:
	mov	rdi, [kp]
	mov	rsi, [kl]
	lea	rdx, [.word]
	mov	rcx, .after_word - .word
	call	__bfa_streq
	test	eax, eax
	jz	.unknown
	__bfc_emtxt "    exsrt_abortus(5);", 10
	return
  .unknown:
	jmp	.after_msg
  .msg db "trap: unknown kind word (only `terminus` has a runtime kind)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfc_die
endp

; ============================================================================
; The dispatcher, and the refusals by name
; ============================================================================

; __bfc_refuse(op) -- never returns. One message per REASON, not one per
; opcode: c-backend.md D4's table gives 14 refused opcodes in eight classes
; (nop is the 15th, opcode 0, outside the 1..60 numbering), and a reader who
; meets one wants to know why the backend has no lowering, which is a
; property of the class.
;
;   div rem                  no reference lowering exists (ssa-ir.md 2.3)
;   muls addov subov mulov   no reference lowering exists (spec 5.4 has no
;                            such operator or predicate)
;   fma                      no lowering in EITHER backend (spec 5.4
;                            contractio explicita: contraction must be
;                            asked for, never made)
;   bitcast                  nothing in-tree produces it; its semantics
;                            are [OPEN] (ssa-ir.md 2.3)
;   redinit contrib redfin   reductions have no reference lowering
;   retain release           no object header exists in library mode
;   callind                  a function value's closure layout is [OPEN]
;   nop                      verifier rule 9 refuses it
;
; EVERY ONE OF THEM IS REFUSED BECAUSE THE REFERENCE REFUSES IT, except
; `retain`/`release`, which the reference DOES lower and library mode
; cannot: D1 withholds the runtime that owns an object header. That
; asymmetry is the one place the two backends' verdicts legitimately
; differ, and it is what D6's `c-emit-exit=` key exists for.
proc __bfc_refuse, op
	locals
	endl
	cmp	dword [op], BFA_OP_DIV
	je	.divrem
	cmp	dword [op], BFA_OP_REM
	je	.divrem
	cmp	dword [op], BFA_OP_MULS
	je	.nolowering
	cmp	dword [op], BFA_OP_ADDOV
	je	.nolowering
	cmp	dword [op], BFA_OP_SUBOV
	je	.nolowering
	cmp	dword [op], BFA_OP_MULOV
	je	.nolowering
	cmp	dword [op], BFA_OP_FMA
	je	.fma
	cmp	dword [op], BFA_OP_BITCAST
	je	.bitcast
	cmp	dword [op], BFA_OP_REDINIT
	je	.reduction
	cmp	dword [op], BFA_OP_CONTRIB
	je	.reduction
	cmp	dword [op], BFA_OP_REDFIN
	je	.reduction
	cmp	dword [op], BFA_OP_RETAIN
	je	.arc
	cmp	dword [op], BFA_OP_RELEASE
	je	.arc
	cmp	dword [op], BFA_OP_CALLIND
	je	.callind
	cmp	dword [op], BFA_OP_NOP
	je	.nop
  .other:
	jmp	.after_m0
  .m0 db "opcode not implemented in the C emitter (c-backend.md D4: the reference's refusal set, plus retain/release which library mode has no runtime for)"
  .after_m0:
	lea	rdi, [.m0]
	mov	rsi, .after_m0 - .m0
	call	__bfc_die

  .divrem:
	jmp	.after_m1
  .m1 db "div/rem: no reference lowering exists (ssa-ir.md 2.3); refused rather than guessed"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die

  .nolowering:
	jmp	.after_m2
  .m2 db "muls/addov/subov/mulov: no reference lowering exists (spec 5.4 has no such operator); these enter BOTH backends in one later milestone, with a fixture each"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die

  .fma:
	jmp	.after_m3
  .m3 db "fma: no lowering in EITHER backend -- the reference's SSE2 baseline has no FMA instruction, and the one C spelling would be <math.h>'s `fma`, a library the dialect does not promise; contraction is the thing spec 5.4 says must be asked for, never made"
  .after_m3:
	lea	rdi, [.m3]
	mov	rsi, .after_m3 - .m3
	call	__bfc_die

  .bitcast:
	jmp	.after_m4
  .m4 db "bitcast: nothing in-tree produces it and its semantics are [OPEN] (ssa-ir.md 2.3); `fconst`'s bits-to-float path is a prologue helper, not this opcode"
  .after_m4:
	lea	rdi, [.m4]
	mov	rsi, .after_m4 - .m4
	call	__bfc_die

  .reduction:
	jmp	.after_m5
  .m5 db "redinit/contrib/redfin: reductions have no reference lowering (ssa-ir.md 2.6: the reference's lowering of arborea is the definition)"
  .after_m5:
	lea	rdi, [.m5]
	mov	rsi, .after_m5 - .m5
	call	__bfc_die

  .arc:
	jmp	.after_m6
  .m6 db "retain/release: no object header exists in library mode (c-backend.md D1, D8) -- the one class where the reference DOES lower and this backend cannot"
  .after_m6:
	lea	rdi, [.m6]
	mov	rsi, .after_m6 - .m6
	call	__bfc_die

  .callind:
	jmp	.after_m7
  .m7 db "callind: no reference lowering exists (ssa-ir.md 2.9: a function value's closure layout is [OPEN])"
  .after_m7:
	lea	rdi, [.m7]
	mov	rsi, .after_m7 - .m7
	call	__bfc_die

  .nop:
	jmp	.after_m8
  .m8 db "nop: verifier rule 9 refuses it, so it cannot reach a backend from well-formed IR"
  .after_m8:
	lea	rdi, [.m8]
	mov	rsi, .after_m8 - .m8
	call	__bfc_die
endp

; __bfc_emit_inst(ctx, module, func, instid) -- dispatches one instruction to
; its lowering, or refuses it by name. A flat compare chain, exactly as
; emit.inc's `__bfa_emit_inst` is: a jump table would be faster and would
; make a missing row a silent fallthrough instead of a refusal.
; `phi` never reaches here -- a block's body list never contains one.
proc __bfc_emit_inst, ctx, module, func, instid
	locals
		slot op, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx

	cmp	dword [op], BFA_OP_PARAM
	jne	.n_param
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_param
	return
  .n_param:
	cmp	dword [op], BFA_OP_ICONST
	jne	.n_iconst
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_iconst
	return
  .n_iconst:
	; add sub mul addw subw mulw adds subs and or xor shl shr
	cmp	dword [op], BFA_OP_ADD
	je	.dobin
	cmp	dword [op], BFA_OP_SUB
	je	.dobin
	cmp	dword [op], BFA_OP_MUL
	je	.dobin
	cmp	dword [op], BFA_OP_ADDW
	je	.dobin
	cmp	dword [op], BFA_OP_SUBW
	je	.dobin
	cmp	dword [op], BFA_OP_MULW
	je	.dobin
	cmp	dword [op], BFA_OP_ADDS
	je	.dobin
	cmp	dword [op], BFA_OP_SUBS
	je	.dobin
	cmp	dword [op], BFA_OP_AND
	je	.dobin
	cmp	dword [op], BFA_OP_OR
	je	.dobin
	cmp	dword [op], BFA_OP_XOR
	je	.dobin
	cmp	dword [op], BFA_OP_SHL
	je	.dobin
	cmp	dword [op], BFA_OP_SHR
	jne	.n_bin
  .dobin:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_bin
	return
  .n_bin:
	cmp	dword [op], BFA_OP_CMP
	je	.docmp
	cmp	dword [op], BFA_OP_FCMP
	jne	.n_cmp
  .docmp:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_cmp
	return
  .n_cmp:
	cmp	dword [op], BFA_OP_ZEXT
	je	.doconv
	cmp	dword [op], BFA_OP_SEXT
	je	.doconv
	cmp	dword [op], BFA_OP_TRUNC
	jne	.n_conv
  .doconv:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_conv
	return
  .n_conv:
	; the float opcodes: D4 rows 24-34 and 60. `fma` and `bitcast` reach
	; neither arm and fall to __bfc_refuse, which names them -- the two
	; emitters' refusal sets are ONE SET.
	cmp	dword [op], BFA_OP_FCONST
	jne	.n_fconst
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_fconst
	return
  .n_fconst:
	cmp	dword [op], BFA_OP_FADD
	je	.dofbin
	cmp	dword [op], BFA_OP_FSUB
	je	.dofbin
	cmp	dword [op], BFA_OP_FMUL
	je	.dofbin
	cmp	dword [op], BFA_OP_FDIV
	jne	.n_fbin
  .dofbin:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_fbin
	return
  .dofneg:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_fneg
	return
  .n_fbin:
	cmp	dword [op], BFA_OP_FNEG
	je	.dofneg
	cmp	dword [op], BFA_OP_FEXT
	je	.dofconv
	cmp	dword [op], BFA_OP_FTRUNC
	je	.dofconv
	cmp	dword [op], BFA_OP_ITOF
	je	.dofconv
	cmp	dword [op], BFA_OP_FTOI
	jne	.n_fconv
  .dofconv:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_fconv
	return
  .n_fconv:
	cmp	dword [op], BFA_OP_SLOT
	jne	.n_slot
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_slot
	return
  .n_slot:
	cmp	dword [op], BFA_OP_GADDR
	jne	.n_gaddr
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_gaddr
	return
  .n_gaddr:
	cmp	dword [op], BFA_OP_ADDR
	jne	.n_addr
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_addr
	return
  .n_addr:
	cmp	dword [op], BFA_OP_INDEX
	jne	.n_index
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_index
	return
  .n_index:
	cmp	dword [op], BFA_OP_CHK
	jne	.n_chk
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_chk
	return
  .n_chk:
	cmp	dword [op], BFA_OP_COPY
	jne	.n_copy
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, [instid]
	call	__bfc_emit_copy
	return
  .n_copy:
	cmp	dword [op], BFA_OP_LOAD
	jne	.n_load
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_load
	return
  .n_load:
	cmp	dword [op], BFA_OP_STORE
	jne	.n_store
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_store
	return
  .n_store:
	cmp	dword [op], BFA_OP_LOADBITS
	jne	.n_loadbits
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_loadbits
	return
  .n_loadbits:
	cmp	dword [op], BFA_OP_STOREBITS
	jne	.n_storebits
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_storebits
	return
  .n_storebits:
	cmp	dword [op], BFA_OP_CALL
	jne	.n_call
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_call
	return
  .n_call:
	cmp	dword [op], BFA_OP_RET
	jne	.n_ret
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_ret
	return
  .n_ret:
	cmp	dword [op], BFA_OP_VADD
	je	.dovar
	cmp	dword [op], BFA_OP_VSUB
	je	.dovar
	cmp	dword [op], BFA_OP_VMUL
	je	.dovar
	cmp	dword [op], BFA_OP_VDIV
	jne	.n_var
  .dovar:
	; the vector float group (spec 5.4 the wave D4 called [OPEN]): one IR
	; op, one C statement on the prologue's vector_size types. `vadd` on a
	; SCALAR type reaches neither arm -- __bfc_emit_varith refuses it by
	; name (tests/ir/reject_emit_vadd_scalar.ir pins the wording) --
	; `fma` and `bitcast` still fall to __bfc_refuse below.
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_varith
	return
  .n_var:
	cmp	dword [op], BFA_OP_JMP
	jne	.n_jmp
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_jmp
	return
  .n_jmp:
	cmp	dword [op], BFA_OP_BR
	jne	.n_br
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_br
	return
  .n_br:
	cmp	dword [op], BFA_OP_TRAP
	jne	.n_trap
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [func]
	mov	ecx, [instid]
	call	__bfc_emit_trap
	return
  .n_trap:
	mov	edi, [op]
	call	__bfc_refuse
endp

; ============================================================================
; Declarations, signatures, and the function
; ============================================================================

; __bfc_emit_sig(ctx, module, fptr) -- "T name(A p0, B p1)", with no
; trailing semicolon and no brace, so ONE routine serves both the prototype
; and the definition and the two can never disagree. A function of no
; parameters is spelled `(void)`, never `()`: `()` is an unprototyped
; declaration in C11 and would silence every argument check at every call
; site in the unit.
proc __bfc_emit_sig, ctx, module, fptr
	uses	rbx
	locals
		slot sptr, dq
		slot pn,   dd
		slot pf,   dd
		slot pty,  dd
	endl
	mov	rax, [fptr]
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sigs]
	mov	esi, [rax + BfaFunc.sig]	; 0-based, unlike every other id
	call	vec_get
	mov	[sptr], rax
	mov	ecx, [rax + BfaSig.p_count]
	mov	[pn], ecx
	mov	ecx, [rax + BfaSig.p_first]
	mov	[pf], ecx

	mov	rax, [sptr]
	mov	ecx, [rax + BfaSig.ret_ty]
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, ecx
	call	__bfc_emit_ctype
	; as with a parameter: `unsigned char *` already ends in its star, so
	; the name follows it directly.
	mov	rax, [sptr]
	mov	ecx, [rax + BfaSig.ret_ty]
	test	ecx, ecx
	jz	.retspace		; `void` always takes the space
	mov	rdi, [module]
	mov	esi, ecx
	call	__bfc_ty_class
	cmp	eax, 1
	je	.retnospace
  .retspace:
	__bfc_emtxt " "
  .retnospace:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [fptr]
	call	__bfc_emit_fname
	__bfc_emtxt "("

	cmp	dword [pn], 0
	jne	.params
	__bfc_emtxt "void)"
	return

  .params:
	xor	ebx, ebx
  .ploop:
	mov	eax, ebx
	cmp	eax, [pn]
	jae	.pdone
	test	ebx, ebx
	jz	.nocomma
	__bfc_emtxt ", "
  .nocomma:
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sig_extra]
	mov	eax, [pf]
	add	eax, ebx
	mov	esi, eax
	call	vec_get
	mov	eax, [rax]
	mov	[pty], eax
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [pty]
	call	__bfc_emit_ctype
	; `unsigned char *` already ends in its star, so no space before the
	; name; `uint64_t` needs one.
	mov	rdi, [module]
	mov	esi, [pty]
	call	__bfc_ty_class
	cmp	eax, 1
	je	.nospace
	__bfc_emtxt " "
  .nospace:
	__bfc_emtxt "p"
	mov	rdi, [ctx]
	mov	esi, ebx
	call	__bfa_out_u64
	inc	ebx
	jmp	.ploop
  .pdone:
	__bfc_emtxt ")"
	return
endp

; __bfc_param_used(func, idx) -> eax = 1 iff some `param` instruction reads
; parameter `idx`. A parameter nothing reads gets a `(void)pI;` in the body,
; so `-Wextra` stays quiet WITHOUT `-Wno-unused-parameter` -- which would be
; turning a warning off across the whole unit to silence one line of it.
proc __bfc_param_used, func, idx
	uses	rbx
	locals
		slot n, dd
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	mov	[n], eax
	mov	ebx, 1
  .loop:
	mov	eax, ebx
	cmp	eax, [n]
	ja	.no
	mov	rdi, [func]
	mov	esi, ebx
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_PARAM
	jne	.next
	mov	ecx, [rax + BfaInst.a]
	cmp	ecx, [idx]
	je	.yes
  .next:
	inc	ebx
	jmp	.loop
  .no:
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __bfc_emit_slotdecl(ctx, func, instid) --
;   `    _Alignas(A) unsigned char sK[N];`
; The alignment is validated exactly as the reference validates it: a power
; of two, 1..16. `_Alignas` is C11 and is on D3's extension-free list
; precisely because it replaces a GCC attribute that would otherwise be an
; extension; it was MEASURED accepted, with `&s[0]` 16-aligned, at -O0 and
; -O2 under both compilers. A size of 0 would be a zero-length array, which
; C forbids and the reference admits; nothing produces one, so it is
; refused by name rather than silently widened to 1 (c-backend.md finding 7
; makes the same choice for a `data $N 0` global, where the array is at file
; scope and a `[1]` with a comment is harmless).
proc __bfc_emit_slotdecl, ctx, func, instid
	locals
		slot sz, dd
		slot algn, dd
	endl
	mov	rdi, [func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[sz], ecx
	mov	ecx, [rax + BfaInst.b]
	test	ecx, ecx
	jnz	.havealign
	mov	ecx, 1				; an absent alignment means 1
  .havealign:
	mov	[algn], ecx
	cmp	ecx, 16
	ja	.badalign
	mov	eax, ecx
	dec	eax
	and	eax, ecx
	jnz	.badalign
	cmp	dword [sz], 0
	je	.badsize

	__bfc_emtxt "    _Alignas("
	mov	rdi, [ctx]
	mov	esi, [algn]
	call	__bfa_out_u64
	__bfc_emtxt ") unsigned char s"
	mov	rdi, [ctx]
	mov	esi, [instid]
	call	__bfa_out_u64
	__bfc_emtxt "["
	mov	rdi, [ctx]
	mov	esi, [sz]
	call	__bfa_out_u64
	__bfc_emtxt "];", 10
	return

  .badalign:
	jmp	.after_m1
  .m1 db "slot: alignment must be a power of two, 1..16"
  .after_m1:
	lea	rdi, [.m1]
	mov	rsi, .after_m1 - .m1
	call	__bfc_die
  .badsize:
	jmp	.after_m2
  .m2 db "slot: a zero-size slot has no C spelling (C forbids a zero-length array) and nothing produces one"
  .after_m2:
	lea	rdi, [.m2]
	mov	rsi, .after_m2 - .m2
	call	__bfc_die
endp

; __bfc_val_used(func, valueid) -> eax = 1 iff some instruction in this
; function reads this value. Walked with ir.inc's own operand iterator, which
; is the only thing that knows where a value operand can hide: `a`, `b`, the
; odd words of a phi's pairs, every word of a `call`'s arguments but the
; first, and the flagged words of `load`/`store`/`index`/`copy`/`br`.
; Hand-rolling that per opcode is how a use would get missed.
;
; What it is FOR: a value the IR computes and nothing reads -- `%4 = call
; u64 @f ...` in tests/ir/saluta.ir is one -- is a `uint64_t` assigned and
; never used, which is `-Wall`'s `-Wunused-but-set-variable`. The lowering
; emits `(void)vK;` right after the assignment, so the diagnostic goes away
; without the warning being turned off and without reading an indeterminate
; value (the `(void)` follows the store, never precedes it).
proc __bfc_val_used, func, valueid
	uses	rbx, r12
	locals
		slot n,   dd
		slot cur, dd
		slot got, dd
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	mov	[n], eax
	mov	ebx, 1
  .instloop:
	mov	eax, ebx
	cmp	eax, [n]
	ja	.no
	mov	dword [cur], 1
  .oploop:
	mov	rdi, [func]
	mov	esi, ebx
	mov	edx, [cur]
	call	bfa_inst_operand_next
	test	eax, eax
	jz	.instnext
	mov	[cur], eax
	mov	[got], edx
	mov	eax, [got]
	cmp	eax, [valueid]
	je	.yes
	jmp	.oploop
  .instnext:
	inc	ebx
	jmp	.instloop
  .no:
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __bfc_emit_decls(ctx, module, func) -- every declaration, at the top of the
; function body, in three passes over the instruction ids: the values, then
; the phi temporaries, then the `slot` storage. Three passes rather than one
; because C11 forbids a declaration after a statement label, and because the
; order must be a pure function of the ids (D5's audit table): id order
; within each pass, passes in a fixed order, and nothing depending on what
; an instruction happens to sit next to.
proc __bfc_emit_decls, ctx, module, func
	uses	rbx
	locals
		slot n,   dd
		slot tyv, dd
		slot op,  dd
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	mov	[n], eax

	; ---- pass 1: one declaration per value-producing instruction --------
	mov	ebx, 1
  .vloop:
	mov	eax, ebx
	cmp	eax, [n]
	ja	.vdone
	mov	rdi, [func]
	mov	esi, ebx
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	edi, [op]
	mov	esi, ecx
	call	__bfa_op_is_void
	test	eax, eax
	jnz	.vnext
	mov	rdi, [module]
	mov	rsi, [func]
	mov	edx, ebx
	call	__bfc_valty
	mov	[tyv], eax
	__bfc_emtxt "    "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_emit_ctype
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 1
	je	.vnospace
	__bfc_emtxt " "
  .vnospace:
	mov	rdi, [ctx]
	mov	esi, ebx
	call	__bfc_emit_v
	mov	rdi, [ctx]
	call	__bfc_semi
  .vnext:
	inc	ebx
	jmp	.vloop
  .vdone:

	; ---- pass 2: one temporary per phi, for the parallel copy -----------
	mov	ebx, 1
  .tloop:
	mov	eax, ebx
	cmp	eax, [n]
	ja	.tdone
	mov	rdi, [func]
	mov	esi, ebx
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_PHI
	jne	.tnext
	mov	rdi, [module]
	mov	rsi, [func]
	mov	edx, ebx
	call	__bfc_valty
	mov	[tyv], eax
	__bfc_emtxt "    "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	edx, [tyv]
	call	__bfc_emit_ctype
	mov	rdi, [module]
	mov	esi, [tyv]
	call	__bfc_ty_class
	cmp	eax, 1
	je	.tnospace
	__bfc_emtxt " "
  .tnospace:
	__bfc_emtxt "t"
	mov	rdi, [ctx]
	mov	esi, ebx
	call	__bfa_out_u64
	mov	rdi, [ctx]
	call	__bfc_semi
  .tnext:
	inc	ebx
	jmp	.tloop
  .tdone:

	; ---- pass 3: the storage behind each `slot` -------------------------
	mov	ebx, 1
  .sloop:
	mov	eax, ebx
	cmp	eax, [n]
	ja	.sdone
	mov	rdi, [func]
	mov	esi, ebx
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_SLOT
	jne	.snext
	mov	rdi, [ctx]
	mov	rsi, [func]
	mov	edx, ebx
	call	__bfc_emit_slotdecl
  .snext:
	inc	ebx
	jmp	.sloop
  .sdone:
	return
endp

; __bfc_emit_function(ctx, module, fptr, arena) -- one definition.
;
;   <signature>
;   {
;       <declarations>
;       (void)pI;          for each parameter no `param` instruction reads
;   bK:                    for each block that has a predecessor
;       <statements>
;   }
;
; Blocks in index order; the entry block is the first and is FALLEN INTO, so
; it gets no label -- which also keeps `-Wunused-label` quiet without
; turning the warning off. A block's label is emitted iff it has at least
; one predecessor edge, which is exactly the set some `goto` can name.
proc __bfc_emit_function, ctx, module, fptr, arena
	uses	rbx
	locals
		slot nb,  dd
		slot cur, dd
		slot np,  dd
	endl
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [fptr]
	call	__bfc_emit_sig
	__bfc_emtxt 10, "{", 10

	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [fptr]
	call	__bfc_emit_decls

	; ---- (void)pI; for every parameter no instruction reads -------------
	mov	rax, [fptr]
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sigs]
	mov	esi, [rax + BfaFunc.sig]
	call	vec_get
	mov	ecx, [rax + BfaSig.p_count]
	mov	[np], ecx
	xor	ebx, ebx
  .vloop:
	mov	eax, ebx
	cmp	eax, [np]
	jae	.vdone
	mov	rdi, [fptr]
	mov	esi, ebx
	call	__bfc_param_used
	test	eax, eax
	jnz	.vnext
	__bfc_emtxt "    (void)p"
	mov	rdi, [ctx]
	mov	esi, ebx
	call	__bfa_out_u64
	__bfc_emtxt ";", 10
  .vnext:
	inc	ebx
	jmp	.vloop
  .vdone:

	; ---- the blocks, in index order --------------------------------------
	mov	rdi, [fptr]
	mov	rdi, [rdi + BfaFunc.blocks]
	mov	rax, [rdi + Vec.len]
	mov	[nb], eax
	mov	ebx, 1
  .bloop:
	mov	eax, ebx
	cmp	eax, [nb]
	ja	.bdone

	mov	rax, [ctx]
	mov	[rax + BfaPrintCtx.curblk], ebx

	mov	rdi, [fptr]
	mov	esi, ebx
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_pred]
	test	ecx, ecx
	jz	.nolabel
	__bfc_emtxt "b"
	mov	rdi, [ctx]
	mov	esi, ebx
	call	__bfa_out_u64
	__bfc_emtxt ":", 10
  .nolabel:
	mov	rdi, [fptr]
	mov	esi, ebx
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first]
	mov	[cur], ecx
  .iloop:
	cmp	dword [cur], 0
	je	.idone
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [fptr]
	mov	ecx, [cur]
	call	__bfc_emit_inst
	; A value this instruction produced and nothing reads is discarded
	; explicitly, right after its assignment -- see __bfc_val_used.
	mov	rdi, [fptr]
	mov	esi, [cur]
	call	bfa_inst_ptr
	movzx	edi, word [rax + BfaInst.op]
	mov	esi, [rax + BfaInst.ty]
	call	__bfa_op_is_void
	test	eax, eax
	jnz	.inext
	mov	rdi, [fptr]
	mov	esi, [cur]
	call	__bfc_val_used
	test	eax, eax
	jnz	.inext
	__bfc_emtxt "    (void)"
	mov	rdi, [ctx]
	mov	esi, [cur]
	call	__bfc_emit_v
	mov	rdi, [ctx]
	call	__bfc_semi
  .inext:
	mov	rdi, [fptr]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.iloop
  .idone:
	inc	ebx
	jmp	.bloop
  .bdone:
	__bfc_emtxt "}", 10
	return
endp

; ============================================================================
; The module
; ============================================================================

; bfc_emit_module(module, arena) -> rax = pointer to the bytes, rdx = how
; many. The functions of one module as C text: a prototype for EVERY
; function, defined or not, then a definition for every function with a
; body. The prologue, the `exsi_*` helpers and the `static const` globals
; are program_c.inc's; this routine emits nothing else, so a unit fixture
; can pin exactly the lowerings with no prologue in the way -- the same
; split `bfa_emit_module` and `bfa_emit_program` already have.
;
; Prototypes first, ALL of them, so call order inside the unit is free and a
; mutually recursive pair needs no forward declaration of its own.
proc bfc_emit_module, module, arena
	uses	rbx
	locals
		slot ctx, dq
		slot n,   dd
	endl
	mov	rdi, [arena]
	mov	rsi, sizeof.BfaPrintCtx
	call	arena_alloc
	mov	[ctx], rax
	mov	rcx, [module]
	mov	[rax + BfaPrintCtx.module], rcx
	mov	dword [rax + BfaPrintCtx.curblk], 0

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

	; ---- prototypes ------------------------------------------------------
	xor	ebx, ebx
  .ploop:
	mov	eax, ebx
	cmp	eax, [n]
	jae	.pdone
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, ebx
	call	vec_get
	mov	rax, [rax]
	push	rax
	mov	rdi, [ctx]
	mov	rsi, [module]
	pop	rdx
	call	__bfc_emit_sig
	__bfc_emtxt ";", 10
	inc	ebx
	jmp	.ploop
  .pdone:
	__bfc_emtxt 10

	; ---- definitions -----------------------------------------------------
	xor	ebx, ebx
  .dloop:
	mov	eax, ebx
	cmp	eax, [n]
	jae	.ddone
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, ebx
	call	vec_get
	mov	rax, [rax]
	; An `externus` function is a DECLARATION: spec 5.3 says its body lives
	; in another object, so defining it here would define what something
	; else already defines. Its prototype above is all the unit owes it.
	test	dword [rax + BfaFunc.attrs], BFA_FUNC_ATTR_EXTERNUS
	jnz	.dnext
	push	rax
	mov	rdi, [ctx]
	mov	rsi, [module]
	pop	rdx
	mov	rcx, [arena]
	call	__bfc_emit_function
	__bfc_emtxt 10
  .dnext:
	inc	ebx
	jmp	.dloop
  .ddone:

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.obuf]
	mov	rax, [rdi + Vec.data]
	mov	rdx, [rdi + Vec.len]
	return
endp
