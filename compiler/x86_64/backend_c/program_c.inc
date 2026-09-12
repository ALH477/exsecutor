; compiler/x86_64/backend_c/program_c.inc
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
; The whole C translation unit -- docs/design/c-backend.md D1 (library mode)
; and D5 (determinism). `bfc_emit_unit` assembles, in this order:
;
;   1. the PROLOGUE and the `exsi_*` helpers, carried VERBATIM from
;      `prologue.c.in` by fasmg's `file` directive at exsc's OWN assembly
;      time -- exactly the way program.inc carries prelude.asm, and for the
;      same two reasons: the text is then a function of exsc's bytes and
;      nothing else (spec 9.3), and it is reviewable as C rather than as a
;      thousand `db` bytes;
;   2. one `_Static_assert` on `sizeof(void *)`, the ONE line the `--hospes`
;      row contributes to the unit (D2). Everything else about the host --
;      byte order above all -- the emitted text asks the C compiler for
;      instead of asserting;
;   3. a `static const` byte array per `data` global, in global-id order;
;   4. the module: a prototype per function, then a definition per bodied
;      one (emit_c.inc's `bfc_emit_module`).
;
; GLOBALS PRECEDE PROTOTYPES, where c-backend.md D5's audit table wrote
; "prologue, prototypes, globals, definitions". Globals must precede the
; DEFINITIONS that name them and are independent of the prototypes, so the
; only constraint is satisfied either way; emitting them here, before one
; single call into the emitter, keeps `bfc_emit_module` a single
; self-contained routine that a unit fixture can pin on its own. D5's table
; is corrected in the same commit as this file.
;
; NO SYSCALL IS ISSUED HERE. This file builds bytes in an arena and returns
; them; opening and writing `OUT` is the driver's, exactly as it is for the
; reference backend.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md section 9.2, 9.3, 9.5, 12; ADR 0012.
; -----------------------------------------------------------------------------

include 'emit_c.inc'

; ---- the closed `--hospes` table, c-backend.md D2 ---------------------------
; One row per accepted value; the value fixes the width of `mensura` and
; therefore of `ptr`, which is the one fact the emitted text must ASSERT
; (ADR 0012's rung (b)) rather than leave to the C compiler.
;
;   x86_64-linux      64   reference backend and C backend
;   riscv64-linux     64   C backend only
;   mips64-none-o64   32   C backend only -- NOT ACCEPTED IN C1, see below
;
; C1 accepts the first two and REFUSES the third. The reason is not the
; driver's: `lower/ty.inc` interns `ref`/`refc` at width 64 unconditionally
; (`mov ecx, 64`, twice) and `bfa_ty_ptr` interns `ptr` at width 64 in
; `ir.inc` itself -- a file this milestone reuses UNCHANGED by contract.
; Both are other trees. `mensura` itself is already parameterised and was
; never the obstacle (see the correction to c-backend.md finding 6), so the
; o64 row is one small change in two files away rather than a redesign; it
; is C4's, as the milestone table says, and until then a value whose row
; this build cannot honour is a usage error rather than a wrong unit.
BFC_HOSPES_NONE     = 0
BFC_HOSPES_X86_64   = 1		; x86_64-linux
BFC_HOSPES_RISCV64  = 2		; riscv64-linux
BFC_HOSPES_MIPS64   = 3		; mips64-none-o64 -- parsed, refused in C1

; ---- the prologue blob ------------------------------------------------------
; `file` reads prologue.c.in at EXSC's assembly time and lays its bytes down
; here; the `jmp` guards them, since this lands in an executable segment.
; The length is a label difference, so the two can never disagree.
	jmp	__bfc_prologue_end
__bfc_prologue:
	file	'prologue.c.in'
__bfc_prologue_end:

; __bfc_prog_hexnib(ctx, nib) -- one UPPERCASE hex digit. print.inc has a
; lowercase one for the IR printer's own purposes; a global's bytes are read
; by a human against a hex dump, and program.inc already chose uppercase for
; that reason on the reference side. The two spellings stay separate and
; each is fixed where it is read.
proc __bfc_prog_hexnib, ctx, nib
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

; __bfc_prog_hexbyte(ctx, val) -- "0xAB".
proc __bfc_prog_hexbyte, ctx, val
	locals
	endl
	__bfc_emtxt "0x"
	mov	rdi, [ctx]
	mov	eax, [val]
	shr	eax, 4
	mov	esi, eax
	call	__bfc_prog_hexnib
	mov	rdi, [ctx]
	mov	esi, [val]
	call	__bfc_prog_hexnib
	return
endp

BFC_PROG_HEXPERLINE = 12

; __bfc_prog_global(ctx, module, gptr2) -- one `data $N` global as
;
;   _Alignas(A) static const unsigned char exsi_gN[SIZE] = {
;   0x41,0x6C,0x69, ... };
;
; `static const`, because the reference puts these in `segment readable`:
; a `store` through a `gaddr` is a wrong program in BOTH backends, and C
; says so out loud. `data_len` may be less than `size` -- ir.inc allows the
; tail to be implicit -- and C zero-fills the rest of an initialised array,
; which is the same thing the reference's zero-filled tail does.
;
; `size == 0` emits `[1]` with a comment: C forbids a zero-length array
; (c-backend.md finding 7). Nothing in the corpus has one; the row exists so
; that the day something does, the unit still compiles and the comment says
; why the array is one byte longer than the IR asked for.
;
; The argument is `gptr2` and not `gptr` because `gptr` is a burned global
; name (docs/asm-conventions.md 4.1) -- program.inc hit exactly this and
; named its own argument the same way.
proc __bfc_prog_global, ctx, module, gptr2
	uses	rbx
	locals
		slot n,    dd
		slot off,  dd
		slot sz,   dd
		slot algn,   dd
		slot gid,  dd
	endl
	mov	rax, [gptr2]
	mov	ecx, [rax + BfaGlobal.id]
	mov	[gid], ecx
	mov	ecx, [rax + BfaGlobal.size]
	mov	[sz], ecx
	mov	ecx, [rax + BfaGlobal.algn]
	test	ecx, ecx
	jnz	.havealign
	mov	ecx, 1
  .havealign:
	mov	[algn], ecx
	mov	ecx, [rax + BfaGlobal.data_len]
	mov	[n], ecx
	mov	ecx, [rax + BfaGlobal.data_off]
	mov	[off], ecx

	__bfc_emtxt "_Alignas("
	mov	rdi, [ctx]
	mov	esi, [algn]
	call	__bfa_out_u64
	__bfc_emtxt ") static const unsigned char exsi_g"
	mov	rdi, [ctx]
	mov	esi, [gid]
	call	__bfa_out_u64
	__bfc_emtxt "["
	cmp	dword [sz], 0
	jne	.realsize
	__bfc_emtxt "1] = { 0 };   /* size 0: C forbids a zero-length array */", 10
	return
  .realsize:
	mov	rdi, [ctx]
	mov	esi, [sz]
	call	__bfa_out_u64
	__bfc_emtxt "] = {"

	cmp	dword [n], 0
	jne	.bytes
	__bfc_emtxt " 0 };   /* no initial data: the whole array is zero */", 10
	return

  .bytes:
	xor	ebx, ebx
  .byteloop:
	mov	eax, ebx
	cmp	eax, [n]
	jae	.bytesdone
	; The comma and the line break are INDEPENDENT: every byte but the
	; first is preceded by a comma, and every twelfth byte additionally
	; starts a new line. Folding the two into one choice is how the first
	; version of this loop dropped the comma at each line break -- caught
	; by gcc, "expected '}' before numeric constant", on the first unit it
	; ever compiled.
	test	ebx, ebx
	jz	.nocomma
	__bfc_emtxt ","
  .nocomma:
	mov	eax, ebx
	xor	edx, edx
	mov	ecx, BFC_PROG_HEXPERLINE
	div	ecx
	test	edx, edx
	jnz	.emitbyte
	mov	rdi, [ctx]
	call	__bfc_nl
  .emitbyte:
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.gdata]
	mov	eax, [off]
	add	eax, ebx
	mov	esi, eax
	call	vec_get
	movzx	ecx, byte [rax]
	mov	rdi, [ctx]
	mov	esi, ecx
	call	__bfc_prog_hexbyte
	inc	ebx
	jmp	.byteloop
  .bytesdone:
	__bfc_emtxt 10, "};", 10
	return
endp

; __bfc_prog_hospes(ctx, hospes) -- the ONE line the `--hospes` row puts in
; the unit. A value this build cannot honour never reaches here: the driver
; refuses it as a usage error (D2's first bullet), and this refusal is the
; belt to that brace for any other caller.
proc __bfc_prog_hospes, ctx, hospes
	locals
	endl
	cmp	dword [hospes], BFC_HOSPES_X86_64
	je	.w64
	cmp	dword [hospes], BFC_HOSPES_RISCV64
	je	.w64
	cmp	dword [hospes], BFC_HOSPES_MIPS64
	je	.w32
	jmp	.after_msg
  .msg db "hospes: no row. This build emits C for x86_64-linux, riscv64-linux and mips64-none-o64"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfc_die
  .w32:
	; mips64-none-o64. This assert is the WHOLE of what the row
	; contributes textually -- but not the whole of what it contributes:
	; the row also sets `mensura` to 32 back in the checker, and that is
	; written through the unit as literal widths in every `exsi_norm_u`,
	; `exsi_add_u` and `exsi_ld_*` call. The o64 unit is a different,
	; larger file than the x86_64 one, and reading this proc alone gives
	; the opposite impression (ADR 0015, "the correction").
	__bfc_emtxt 10, "_Static_assert(sizeof(void *) == 4, "
	__bfc_emtxt 34, "exsecutor: this hospes has 32-bit addresses", 34, ");", 10, 10
	return
  .w64:
	__bfc_emtxt 10, "_Static_assert(sizeof(void *) == 8, "
	__bfc_emtxt 34, "exsecutor: this hospes has 64-bit addresses", 34, ");", 10, 10
	return
endp

; bfc_emit_unit(module, arena, hospes) -> rax = the bytes, rdx = how many.
; The whole translation unit. `hospes` is a BFC_HOSPES_* row.
proc bfc_emit_unit, module, arena, hospes
	uses	rbx
	locals
		slot ctx,  dq
		slot n,    dd
		slot mptr, dq
		slot mlen, dq
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
	mov	rcx, 65536
	call	vec_init

	; ---- 1. the prologue and the helpers, verbatim ----------------------
	mov	rdi, [ctx]
	lea	rsi, [__bfc_prologue]
	mov	rdx, __bfc_prologue_end - __bfc_prologue
	call	__bfa_out_bytes

	; ---- 2. the one per-hospes line --------------------------------------
	mov	rdi, [ctx]
	mov	esi, [hospes]
	call	__bfc_prog_hospes

	; ---- 3. the globals, in id order -------------------------------------
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.globals]
	mov	rax, [rdi + Vec.len]
	mov	[n], eax
	xor	ebx, ebx
  .globloop:
	mov	eax, ebx
	cmp	eax, [n]
	jae	.globsdone
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.globals]
	mov	esi, ebx
	call	vec_get
	push	rax
	mov	rdi, [ctx]
	mov	rsi, [module]
	pop	rdx
	call	__bfc_prog_global
	inc	ebx
	jmp	.globloop
  .globsdone:
	cmp	dword [n], 0
	je	.nogl
	mov	rdi, [ctx]
	call	__bfc_nl
  .nogl:

	; ---- 4. the module ---------------------------------------------------
	; bfc_emit_module builds its own ctx and its own buffer, and its bytes
	; are copied in here -- the same two-buffer shape bfa_emit_program and
	; bfa_emit_module already have, and for the same reason: a unit fixture
	; can call the inner one alone.
	mov	rdi, [module]
	mov	rsi, [arena]
	call	bfc_emit_module
	mov	[mptr], rax
	mov	[mlen], rdx
	mov	rdi, [ctx]
	mov	rsi, [mptr]
	mov	rdx, [mlen]
	call	__bfa_out_bytes

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.obuf]
	mov	rax, [rdi + Vec.data]
	mov	rdx, [rdi + Vec.len]
	return
endp
