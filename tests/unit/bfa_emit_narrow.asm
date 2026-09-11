; tests/unit/bfa_emit_narrow.asm
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
; backend_fasmg fixture for emit.inc's narrow-integer lowering (wire-codec.md
; D5, milestone M3; docs/design/ssa-ir.md section 2.2's canonical form).
; Parses three functions and checks the emitted fasmg TEXT exactly, the way
; bfa_emit_tier1.asm does for the 64-bit tier. What each line pins:
;
;   @angustus   u8: `addw` then the normalisation pair (`shl rax, 56` /
;               `shr rax, 56`); a trapping `add` as the raw sum in rax, a
;               normalised copy in rcx, `cmp` / `jne bfausr_trap`; a trapping
;               u8 `mul` by an immediate with NO carry check (at 8 bits the
;               64-bit product is exact); `iconst u64 3735928559` through
;               `mov rax, imm64` (the plan's B4 -- the one-line
;               `mov qword [slot], imm` form cannot hold it); `trunc u8`
;               as a normalisation; `subw`.
;   @latus      i8 and u40: a signed trapping `add` normalised with `sar`;
;               `zext u40` of an i8 -- zero-extend from 8 bits first, then
;               normalise to 40; a trapping u40 `mul` that keeps `jc` AND
;               adds the compare (33..63 bits need both); `mulw u40`.
;   @memoria    arrays: `chk` (unsigned `jae`), `index` (`imul rcx, rcx,
;               stride`), an i16 load (`movsx`), an i16 store (`word`,
;               `cx`), `copy 7` unrolled as 4 + 2 + 1 through rcx with the
;               source in rdx, and `addr`.
;
; The running half is tests/ir/: narrow_wrap, trap_add_*, trap_mul_*,
; mul_u40, conv_roundtrip, memory_narrow, copy_17, iconst_wide, trap_chk.
; Every 64-bit function's text is unchanged by this lowering, which
; bfa_emit_tier1.asm, bfa_emit_tier2.asm, bfa_emit_phi.asm and
; bfa_emit_program.asm, passing unmodified, are the evidence for.
;
; Exit 0 = emitted text matches exactly. 12 = it does not (the emitted text
; goes to stderr first, for inspection).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/span.inc'
include '../../compiler/x86_64/backend_fasmg/emit.inc'

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 16777216
	call	arena_init
	jc	.fail1
	lea	rdi, [scr]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail1

	lea	rdi, [ar]
	lea	rsi, [scr]
	lea	rdx, [srctext]
	mov	rcx, srctext.len
	call	bfa_parse_module
	mov	[modout], rax

	mov	rdi, [modout]
	lea	rsi, [ar]
	call	bfa_emit_module
	mov	[outptr], rax
	mov	[outlen], rdx

	mov	rdi, [outptr]
	mov	rsi, [outlen]
	lea	rdx, [expected]
	mov	rcx, expected.len
	call	__bfa_streq
	test	eax, eax
	jz	.fail2

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1: mov eax,231
	mov edi,11
	syscall
  .fail2:
	mov	edi, 2
	mov	rsi, [outptr]
	mov	rdx, [outlen]
	call	sys_write
	mov	eax,231
	mov edi,12
	syscall

segment readable writeable
  ar: rb sizeof.Arena
  scr: rb sizeof.Arena
  modout: dq 0
  outptr: dq 0
  outlen: dq 0
  srctext:
  db "functio @angustus (u8 u8) -> u8 {", 10
  db "b0:", 10
  db "%0 = param u8 0", 10
  db "%1 = param u8 1", 10
  db "%2 = addw u8 %0 %1", 10
  db "%3 = add u8 %2 %1", 10
  db "%4 = mul u8 %3 3", 10
  db "%5 = iconst u64 3735928559", 10
  db "%6 = trunc u8 %5", 10
  db "%7 = subw u8 %4 %6", 10
  db "ret %7", 10
  db "}", 10
  db "functio @latus (i8 u40) -> u40 {", 10
  db "b0:", 10
  db "%0 = param i8 0", 10
  db "%1 = param u40 1", 10
  db "%2 = add i8 %0 1", 10
  db "%3 = zext u40 %2", 10
  db "%4 = mul u40 %1 %3", 10
  db "%5 = mulw u40 %4 %1", 10
  db "ret %5", 10
  db "}", 10
  db "functio @memoria (ptr u64) -> i16 {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = param u64 1", 10
  db "%2 = slot 8 8", 10
  db "%3 = iconst u64 4", 10
  db "chk %1 %3", 10
  db "%4 = index %0 %1 2", 10
  db "%5 = load i16 %4 0 nativus", 10
  db "store i16 %2 6 nativus %5", 10
  db "copy 7 %2 %0", 10
  db "%6 = addr %2 6", 10
  db "%7 = load i16 %6 0 nativus", 10
  db "ret %7", 10
  db "}", 10
  .len = $ - srctext
  expected:
  db "bfausr_angustus:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 80", 10
  db "bfausr_angustus_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	rax, [rbp-8]", 10
  db "	add	rax, [rbp-16]", 10
  db "	shl	rax, 56", 10
  db "	shr	rax, 56", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	add	rax, [rbp-16]", 10
  db "	mov	rcx, rax", 10
  db "	shl	rcx, 56", 10
  db "	shr	rcx, 56", 10
  db "	cmp	rcx, rax", 10
  db "	jne	bfausr_trap", 10
  db "	mov	qword [rbp-32], rax", 10
  db "	mov	rax, [rbp-32]", 10
  db "	mov	rcx, 3", 10
  db "	mul	rcx", 10
  db "	mov	rcx, rax", 10
  db "	shl	rcx, 56", 10
  db "	shr	rcx, 56", 10
  db "	cmp	rcx, rax", 10
  db "	jne	bfausr_trap", 10
  db "	mov	qword [rbp-40], rax", 10
  db "	mov	rax, 3735928559", 10
  db "	mov	qword [rbp-48], rax", 10
  db "	mov	rax, [rbp-48]", 10
  db "	shl	rax, 56", 10
  db "	shr	rax, 56", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	rax, [rbp-40]", 10
  db "	sub	rax, [rbp-56]", 10
  db "	shl	rax, 56", 10
  db "	shr	rax, 56", 10
  db "	mov	qword [rbp-64], rax", 10
  db "	mov	rax, [rbp-64]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_latus:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 64", 10
  db "bfausr_latus_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	rax, [rbp-8]", 10
  db "	add	rax, 1", 10
  db "	mov	rcx, rax", 10
  db "	shl	rcx, 56", 10
  db "	sar	rcx, 56", 10
  db "	cmp	rcx, rax", 10
  db "	jne	bfausr_trap", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	shl	rax, 56", 10
  db "	shr	rax, 56", 10
  db "	shl	rax, 24", 10
  db "	shr	rax, 24", 10
  db "	mov	qword [rbp-32], rax", 10
  db "	mov	rax, [rbp-16]", 10
  db "	mul	qword [rbp-32]", 10
  db "	jc	bfausr_trap", 10
  db "	mov	rcx, rax", 10
  db "	shl	rcx, 24", 10
  db "	shr	rcx, 24", 10
  db "	cmp	rcx, rax", 10
  db "	jne	bfausr_trap", 10
  db "	mov	qword [rbp-40], rax", 10
  db "	mov	rax, [rbp-40]", 10
  db "	imul	rax, [rbp-16]", 10
  db "	shl	rax, 24", 10
  db "	shr	rax, 24", 10
  db "	mov	qword [rbp-48], rax", 10
  db "	mov	rax, [rbp-48]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_memoria:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 128", 10
  db "bfausr_memoria_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	lea	rax, [rbp-120]", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	qword [rbp-32], 4", 10
  db "	mov	rax, [rbp-16]", 10
  db "	cmp	rax, [rbp-32]", 10
  db "	jae	bfausr_trap", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-16]", 10
  db "	imul	rcx, rcx, 2", 10
  db "	add	rax, rcx", 10
  db "	mov	qword [rbp-48], rax", 10
  db "	mov	rax, [rbp-48]", 10
  db "	movsx	rax, word [rax+0]", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	rcx, [rbp-56]", 10
  db "	mov	word [rax+6], cx", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	rdx, [rbp-8]", 10
  db "	mov	ecx, dword [rdx+0]", 10
  db "	mov	dword [rax+0], ecx", 10
  db "	mov	cx, word [rdx+4]", 10
  db "	mov	word [rax+4], cx", 10
  db "	mov	cl, byte [rdx+6]", 10
  db "	mov	byte [rax+6], cl", 10
  db "	mov	rax, [rbp-24]", 10
  db "	add	rax, 6", 10
  db "	mov	qword [rbp-80], rax", 10
  db "	mov	rax, [rbp-80]", 10
  db "	movsx	rax, word [rax+0]", 10
  db "	mov	qword [rbp-88], rax", 10
  db "	mov	rax, [rbp-88]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_trap:", 10
  db "	mov	edi, 1", 10
  db "	jmp	exsrt_abort", 10
  .len = $ - expected
