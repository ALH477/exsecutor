; tests/unit/bfa_emit_bitwise.asm
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
; backend_fasmg fixture for emit.inc's bitwise, shift and saturating
; lowering (wire-codec.md milestone M3; docs/design/ssa-ir.md sections 2.2
; and 2.3). Parses two functions and checks the emitted fasmg TEXT exactly.
; What each line pins:
;
;   @bitus   `xor`/`and`/`or` at u8 with no normalisation (canonical in,
;            canonical out), one with an immediate operand; `shl u8` by a
;            variable count: `mov rcx, count` / `cmp rcx, 8` / `jae
;            bfausr_trap` (a count >= N traps, unsigned, kind 1) / `shl rax,
;            cl` / the normalisation; `shr u8` by an immediate (`shr`, no
;            normalisation); `shr i8` as `sar`; `adds u8` clamped with
;            `cmova` to 255; `subs u8` clamped at 0 with `cmovs`.
;   @satus   `adds i64`: the clamp (a sar 63) xor (2^63-1) built before the
;            add and taken with `cmovo`; `subs u64` by an immediate (`sbb`/
;            `not`/`and`); `adds u64` (`sbb`/`or`); `subs i16` clamped to
;            [-32768, 32767]; `shl u64` by 63 -- the count check against
;            64, and no normalisation at 64 bits.
;
; The running half is tests/ir/: shift_narrow, trap_shl_u8, trap_shr_u32,
; sat_narrow, bitwise.
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
  db "functio @bitus (u8 u8 i8) -> u8 {", 10
  db "b0:", 10
  db "%0 = param u8 0", 10
  db "%1 = param u8 1", 10
  db "%2 = param i8 2", 10
  db "%3 = xor u8 %0 %1", 10
  db "%4 = and u8 %3 15", 10
  db "%5 = or u8 %4 %1", 10
  db "%6 = shl u8 %5 %1", 10
  db "%7 = shr u8 %6 3", 10
  db "%8 = shr i8 %2 1", 10
  db "%9 = trunc u8 %8", 10
  db "%10 = adds u8 %7 %9", 10
  db "%11 = subs u8 %10 %0", 10
  db "ret %11", 10
  db "}", 10
  db "functio @satus (i64 u64 i16) -> u64 {", 10
  db "b0:", 10
  db "%0 = param i64 0", 10
  db "%1 = param u64 1", 10
  db "%2 = param i16 2", 10
  db "%3 = adds i64 %0 %0", 10
  db "%4 = subs u64 %1 7", 10
  db "%5 = adds u64 %4 %1", 10
  db "%6 = subs i16 %2 %2", 10
  db "%7 = shl u64 %5 63", 10
  db "ret %7", 10
  db "}", 10
  .len = $ - srctext
  expected:
  db "bfausr_bitus:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 112", 10
  db "bfausr_bitus_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	qword [rbp-24], rdx", 10
  db "	mov	rax, [rbp-8]", 10
  db "	xor	rax, [rbp-16]", 10
  db "	mov	qword [rbp-32], rax", 10
  db "	mov	rax, [rbp-32]", 10
  db "	and	rax, 15", 10
  db "	mov	qword [rbp-40], rax", 10
  db "	mov	rax, [rbp-40]", 10
  db "	or	rax, [rbp-16]", 10
  db "	mov	qword [rbp-48], rax", 10
  db "	mov	rax, [rbp-48]", 10
  db "	mov	rcx, [rbp-16]", 10
  db "	cmp	rcx, 8", 10
  db "	jae	bfausr_trap", 10
  db "	shl	rax, cl", 10
  db "	shl	rax, 56", 10
  db "	shr	rax, 56", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	rax, [rbp-56]", 10
  db "	mov	rcx, 3", 10
  db "	cmp	rcx, 8", 10
  db "	jae	bfausr_trap", 10
  db "	shr	rax, cl", 10
  db "	mov	qword [rbp-64], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	rcx, 1", 10
  db "	cmp	rcx, 8", 10
  db "	jae	bfausr_trap", 10
  db "	sar	rax, cl", 10
  db "	mov	qword [rbp-72], rax", 10
  db "	mov	rax, [rbp-72]", 10
  db "	shl	rax, 56", 10
  db "	shr	rax, 56", 10
  db "	mov	qword [rbp-80], rax", 10
  db "	mov	rax, [rbp-64]", 10
  db "	add	rax, [rbp-80]", 10
  db "	mov	rcx, 255", 10
  db "	cmp	rax, rcx", 10
  db "	cmova	rax, rcx", 10
  db "	mov	qword [rbp-88], rax", 10
  db "	mov	rax, [rbp-88]", 10
  db "	sub	rax, [rbp-8]", 10
  db "	xor	ecx, ecx", 10
  db "	test	rax, rax", 10
  db "	cmovs	rax, rcx", 10
  db "	mov	qword [rbp-96], rax", 10
  db "	mov	rax, [rbp-96]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_satus:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 80", 10
  db "bfausr_satus_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	qword [rbp-24], rdx", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, rax", 10
  db "	sar	rcx, 63", 10
  db "	mov	rdx, 9223372036854775807", 10
  db "	xor	rcx, rdx", 10
  db "	add	rax, [rbp-8]", 10
  db "	cmovo	rax, rcx", 10
  db "	mov	qword [rbp-32], rax", 10
  db "	mov	rax, [rbp-16]", 10
  db "	sub	rax, 7", 10
  db "	sbb	rcx, rcx", 10
  db "	not	rcx", 10
  db "	and	rax, rcx", 10
  db "	mov	qword [rbp-40], rax", 10
  db "	mov	rax, [rbp-40]", 10
  db "	add	rax, [rbp-16]", 10
  db "	sbb	rcx, rcx", 10
  db "	or	rax, rcx", 10
  db "	mov	qword [rbp-48], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	sub	rax, [rbp-24]", 10
  db "	mov	rcx, 32767", 10
  db "	cmp	rax, rcx", 10
  db "	cmovg	rax, rcx", 10
  db "	mov	rcx, -32768", 10
  db "	cmp	rax, rcx", 10
  db "	cmovl	rax, rcx", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	rax, [rbp-48]", 10
  db "	mov	rcx, 63", 10
  db "	cmp	rcx, 64", 10
  db "	jae	bfausr_trap", 10
  db "	shl	rax, cl", 10
  db "	mov	qword [rbp-64], rax", 10
  db "	mov	rax, [rbp-64]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_trap:", 10
  db "	mov	edi, 1", 10
  db "	jmp	exsrt_abort", 10
  .len = $ - expected
