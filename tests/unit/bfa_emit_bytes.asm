; tests/unit/bfa_emit_bytes.asm
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
; backend_fasmg fixture for emit.inc's byte order and bit field lowering
; (wire-codec.md milestone M4; docs/design/ssa-ir.md section 2.7). Parses
; two functions and checks the emitted fasmg TEXT exactly. What each line
; pins:
;
;   @ordo    `load u16 maior`: the pointer in rax, the most significant
;            byte (offset 0) `movzx`ed into ecx, then `shl rcx, 8` / `movzx
;            edx` / `or rcx, rdx` for the next, and the result stored from
;            rcx; `load i24 minor` at 2: bytes 4, 3, 2 (least significant
;            at the lowest address) and the `shl`/`sar rcx, 40` pair that
;            sign-extends an i24; `load u24 nativus` at 5: the same walk as
;            `minor` (this host is little-endian) and no pair (a uN is
;            zero-extended as assembled); `store u32 maior` at 8: the value
;            in rcx, `mov byte [rax+11], cl` first (least significant at the
;            highest address), `shr rcx, 8` between bytes, none after the
;            last; `store u16 minor` at 12: bytes 12 then 13; `load u8
;            maior`: the one-instruction `movzx eax, byte` a `nativus` u8
;            has always had -- one byte has no order.
;   @campi   `loadbits u4 %p 1 0` (DeModFrame's `versio`): `movzx eax,
;            byte [rax+1]` / `shr eax, 4` / `and eax, 15`; `loadbits u3
;            %p 1 5`: S = 8 - 5 - 3 = 0, so no `shr`; `storebits u4 %p 1 4`
;            (`genus`): the value masked with 15, no `shl` (S = 0), the
;            byte `and`ed with 240 (the bits kept), `or`ed, written back;
;            `storebits u4 %p 1 0`: `shl ecx, 4` and the byte `and`ed with
;            15.
;
; The running half is tests/ir/: byte_order, bits, demodframe_encode,
; demodframe_decode, and the rejections reject_verify_load_width,
; reject_verify_order_ptr, reject_verify_bits_width,
; reject_verify_bits_offset, reject_emit_straddle. Every `nativus` access at
; 8/16/32/64 bits emits the text it did before M4, which bfa_emit_narrow,
; bfa_emit_tier2 and bfa_emit_program, passing unmodified, are the evidence
; for.
;
; Mutation (run): `loadbits` emitting its `shr` when S = 0 (`shr eax, 0`,
; harmless, but not the text pinned here) -> exit 12.
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
  db "functio @ordo (ptr u32) -> u16 {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = param u32 1", 10
  db "%2 = load u16 %0 0 maior", 10
  db "%3 = load i24 %0 2 minor", 10
  db "%4 = load u24 %0 5 nativus", 10
  db "store u32 %0 8 maior %1", 10
  db "store u16 %0 12 minor %2", 10
  db "%5 = load u8 %0 0 maior", 10
  db "ret %2", 10
  db "}", 10
  db "functio @campi (ptr u4) -> u4 {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = param u4 1", 10
  db "%2 = loadbits u4 %0 1 0", 10
  db "%3 = loadbits u3 %0 1 5", 10
  db "storebits u4 %0 1 4 %1", 10
  db "storebits u4 %0 1 0 %2", 10
  db "ret %2", 10
  db "}", 10
  .len = $ - srctext
  expected:
  db "bfausr_ordo:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 80", 10
  db "bfausr_ordo_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	rax, [rbp-8]", 10
  db "	movzx	ecx, byte [rax+0]", 10
  db "	shl	rcx, 8", 10
  db "	movzx	edx, byte [rax+1]", 10
  db "	or	rcx, rdx", 10
  db "	mov	qword [rbp-24], rcx", 10
  db "	mov	rax, [rbp-8]", 10
  db "	movzx	ecx, byte [rax+4]", 10
  db "	shl	rcx, 8", 10
  db "	movzx	edx, byte [rax+3]", 10
  db "	or	rcx, rdx", 10
  db "	shl	rcx, 8", 10
  db "	movzx	edx, byte [rax+2]", 10
  db "	or	rcx, rdx", 10
  db "	shl	rcx, 40", 10
  db "	sar	rcx, 40", 10
  db "	mov	qword [rbp-32], rcx", 10
  db "	mov	rax, [rbp-8]", 10
  db "	movzx	ecx, byte [rax+7]", 10
  db "	shl	rcx, 8", 10
  db "	movzx	edx, byte [rax+6]", 10
  db "	or	rcx, rdx", 10
  db "	shl	rcx, 8", 10
  db "	movzx	edx, byte [rax+5]", 10
  db "	or	rcx, rdx", 10
  db "	mov	qword [rbp-40], rcx", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-16]", 10
  db "	mov	byte [rax+11], cl", 10
  db "	shr	rcx, 8", 10
  db "	mov	byte [rax+10], cl", 10
  db "	shr	rcx, 8", 10
  db "	mov	byte [rax+9], cl", 10
  db "	shr	rcx, 8", 10
  db "	mov	byte [rax+8], cl", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-24]", 10
  db "	mov	byte [rax+12], cl", 10
  db "	shr	rcx, 8", 10
  db "	mov	byte [rax+13], cl", 10
  db "	mov	rax, [rbp-8]", 10
  db "	movzx	eax, byte [rax+0]", 10
  db "	mov	qword [rbp-64], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_campi:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 64", 10
  db "bfausr_campi_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	rax, [rbp-8]", 10
  db "	movzx	eax, byte [rax+1]", 10
  db "	shr	eax, 4", 10
  db "	and	eax, 15", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rax, [rbp-8]", 10
  db "	movzx	eax, byte [rax+1]", 10
  db "	and	eax, 7", 10
  db "	mov	qword [rbp-32], rax", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-16]", 10
  db "	and	ecx, 15", 10
  db "	movzx	edx, byte [rax+1]", 10
  db "	and	edx, 240", 10
  db "	or	edx, ecx", 10
  db "	mov	byte [rax+1], dl", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-24]", 10
  db "	and	ecx, 15", 10
  db "	shl	ecx, 4", 10
  db "	movzx	edx, byte [rax+1]", 10
  db "	and	edx, 15", 10
  db "	or	edx, ecx", 10
  db "	mov	byte [rax+1], dl", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_trap:", 10
  db "	mov	edi, 1", 10
  db "	jmp	exsrt_abort", 10
  .len = $ - expected
