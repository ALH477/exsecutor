; tests/unit/bfa_emit_phi.asm
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
; backend_fasmg fixture for emit.inc's phi lowering ("phi -- parallel copies
; on edges" in that file): the exact TEXT emitted for one small function
; that has each of the three shapes --
;
;   * a `jmp` into a block with phis (b0 -> b1): the parallel copy precedes
;     the `jmp`, pushes in phi order, pops reversed;
;   * a `br` whose TRUE target has phis (b1's self-loop): `jmp` to the stub
;     `bfausr_rota_b2e0`, which copies and jumps to the block. The two phis
;     name each other on that edge -- a swap -- and the text shows why it is
;     right: both slots are read before either is written;
;   * a `br` whose FALSE target has phis (b1 -> b2): `jz` to the stub
;     `bfausr_rota_b2e1`.
;
; Stub labels are `<block prefix><predecessor block id>e<edge ordinal>`,
; internal (1-based) block ids: text `b1` is internal block 2. The phis
; emit nothing in their own block; each only owns a slot (%2 is value id 4,
; `[rbp-32]`; the `jmp` and `br` take ids too, as every void instruction
; does). The phi-free text around them is what bfa_emit_tier1.asm already
; pins, and that fixture passing unchanged is the evidence a phi-free `br`
; is byte-identical to before.
;
; Text only, like bfa_emit_tier1.asm: running phis is tests/ir/phi_*.ir and
; tests/programs/phi_loops/.
;
; Exit 0 = emitted text matches exactly. 12 = it does not (the emitted text
; goes to stderr first, for inspection). 11 = arena init failed.
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
  db "functio @rota (u64) -> u64 {", 10
  db "b0:", 10
  db "%0 = param u64 0", 10
  db "%1 = iconst u64 1", 10
  db "jmp b1", 10
  db "b1:", 10
  db "%2 = phi u64 b0 %0 b1 %3", 10
  db "%3 = phi u64 b0 %1 b1 %2", 10
  db "%4 = cmp.lt u64 %2 %3", 10
  db "br %4 b1 b2", 10
  db "b2:", 10
  db "%5 = phi u64 b1 %3", 10
  db "ret %5", 10
  db "}", 10
  .len = $ - srctext
  expected:
  db "bfausr_rota:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 80", 10
  db "bfausr_rota_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], 1", 10
  db "	push	qword [rbp-8]", 10
  db "	push	qword [rbp-16]", 10
  db "	pop	qword [rbp-40]", 10
  db "	pop	qword [rbp-32]", 10
  db "	jmp	bfausr_rota_b2", 10
  db "bfausr_rota_b2:", 10
  db "	mov	rax, [rbp-32]", 10
  db "	cmp	rax, [rbp-40]", 10
  db "	setb	al", 10
  db "	movzx	rax, al", 10
  db "	mov	qword [rbp-48], rax", 10
  db "	mov	rax, [rbp-48]", 10
  db "	test	rax, rax", 10
  db "	jz	bfausr_rota_b2e1", 10
  db "	jmp	bfausr_rota_b2e0", 10
  db "bfausr_rota_b2e0:", 10
  db "	push	qword [rbp-40]", 10
  db "	push	qword [rbp-32]", 10
  db "	pop	qword [rbp-40]", 10
  db "	pop	qword [rbp-32]", 10
  db "	jmp	bfausr_rota_b2", 10
  db "bfausr_rota_b2e1:", 10
  db "	push	qword [rbp-40]", 10
  db "	pop	qword [rbp-64]", 10
  db "	jmp	bfausr_rota_b3", 10
  db "bfausr_rota_b3:", 10
  db "	mov	rax, [rbp-64]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_trap:", 10
  db "	mov	edi, 1", 10
  db "	jmp	exsrt_abort", 10
  .len = $ - expected
