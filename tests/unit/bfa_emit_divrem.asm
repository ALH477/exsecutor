; tests/unit/bfa_emit_divrem.asm
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
; backend_fasmg fixture for emit.inc's `div`/`rem` lowering (spec 5.4 as
; decided 2026-09-25; docs/design/ssa-ir.md sections 2.2 and 2.3). Parses two
; functions and checks the emitted fasmg TEXT exactly. What each line pins:
;
;   @dividus  `div u8` between two slots and `rem u8` by an inline
;             immediate -- `mov rcx, <b>` / `test rcx, rcx` / `jz
;             bfausr_trap` / `xor edx, edx` / `div rcx`, and for `rem` the
;             one extra `mov rax, rdx`. NO normalisation after either, at a
;             narrow width: with the zero divisor excluded a uN quotient and
;             remainder are already canonical (emit.inc's __bfa_emit_divrem
;             states the proof). Then `div i64` and `rem i64`: the signed
;             arm, whose T_MIN is INT64_MIN, printed as
;             `mov rdx, -9223372036854775808`.
;   @modulus  `div i8` and `rem i8` -- the SAME signed arm at a narrow
;             width, where T_MIN is the TYPE's own -128 and not INT64_MIN.
;             That one literal is the fixture's whole reason for having a
;             second function: a lowering that used INT64_MIN here would
;             pass every check in @dividus. Then `div u64` by an inline
;             immediate and `rem u64` between slots.
;
; The four-instruction overflow test -- `mov rdx, T_MIN` / `xor rdx, rax` /
; `not rcx` / `or rdx, rcx` / `jz bfausr_trap` / `not rcx` -- is branchless
; and needs no per-instruction label, which is why `div` emits no `.Lx` of
; its own the way a large `copy` does. It appears for iN only; a uN has no
; overflow case (see __bfa_emit_divrem).
;
; The running half is tests/ir/: divrem, rem_sign, trap_div_zero,
; trap_rem_zero, trap_div_minneg1, trap_rem_minneg1.
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
  db "functio @dividus (u8 u8 i64) -> u8 {", 10
  db "b0:", 10
  db "%0 = param u8 0", 10
  db "%1 = param u8 1", 10
  db "%2 = param i64 2", 10
  db "%3 = div u8 %0 %1", 10
  db "%4 = rem u8 %3 7", 10
  db "%5 = div i64 %2 %2", 10
  db "%6 = rem i64 %2 100", 10
  db "%7 = trunc u8 %6", 10
  db "ret %7", 10
  db "}", 10
  db "functio @modulus (i8 i8 u64) -> u64 {", 10
  db "b0:", 10
  db "%0 = param i8 0", 10
  db "%1 = param i8 1", 10
  db "%2 = div i8 %0 %1", 10
  db "%3 = rem i8 %0 -1", 10
  db "%4 = param u64 2", 10
  db "%5 = div u64 %4 1000", 10
  db "%6 = rem u64 %5 %4", 10
  db "ret %6", 10
  db "}", 10
  .len = $ - srctext
  expected:
  db "bfausr_dividus:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 80", 10
  db "bfausr_dividus_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	qword [rbp-24], rdx", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-16]", 10
  db "	test	rcx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	xor	edx, edx", 10
  db "	div	rcx", 10
  db "	mov	qword [rbp-32], rax", 10
  db "	mov	rax, [rbp-32]", 10
  db "	mov	rcx, 7", 10
  db "	test	rcx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	xor	edx, edx", 10
  db "	div	rcx", 10
  db "	mov	rax, rdx", 10
  db "	mov	qword [rbp-40], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	rcx, [rbp-24]", 10
  db "	test	rcx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	mov	rdx, -9223372036854775808", 10
  db "	xor	rdx, rax", 10
  db "	not	rcx", 10
  db "	or	rdx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	not	rcx", 10
  db "	cqo", 10
  db "	idiv	rcx", 10
  db "	mov	qword [rbp-48], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	rcx, 100", 10
  db "	test	rcx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	mov	rdx, -9223372036854775808", 10
  db "	xor	rdx, rax", 10
  db "	not	rcx", 10
  db "	or	rdx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	not	rcx", 10
  db "	cqo", 10
  db "	idiv	rcx", 10
  db "	mov	rax, rdx", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	rax, [rbp-56]", 10
  db "	shl	rax, 56", 10
  db "	shr	rax, 56", 10
  db "	mov	qword [rbp-64], rax", 10
  db "	mov	rax, [rbp-64]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_modulus:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 80", 10
  db "bfausr_modulus_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-16]", 10
  db "	test	rcx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	mov	rdx, -128", 10
  db "	xor	rdx, rax", 10
  db "	not	rcx", 10
  db "	or	rdx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	not	rcx", 10
  db "	cqo", 10
  db "	idiv	rcx", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, -1", 10
  db "	test	rcx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	mov	rdx, -128", 10
  db "	xor	rdx, rax", 10
  db "	not	rcx", 10
  db "	or	rdx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	not	rcx", 10
  db "	cqo", 10
  db "	idiv	rcx", 10
  db "	mov	rax, rdx", 10
  db "	mov	qword [rbp-32], rax", 10
  db "	mov	qword [rbp-40], rdx", 10
  db "	mov	rax, [rbp-40]", 10
  db "	mov	rcx, 1000", 10
  db "	test	rcx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	xor	edx, edx", 10
  db "	div	rcx", 10
  db "	mov	qword [rbp-48], rax", 10
  db "	mov	rax, [rbp-48]", 10
  db "	mov	rcx, [rbp-40]", 10
  db "	test	rcx, rcx", 10
  db "	jz	bfausr_trap", 10
  db "	xor	edx, edx", 10
  db "	div	rcx", 10
  db "	mov	rax, rdx", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	rax, [rbp-56]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_trap:", 10
  db "	mov	edi, 1", 10
  db "	jmp	exsrt_abort", 10
  .len = $ - expected
