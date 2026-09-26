; tests/unit/bfa_emit_contrahe.asm
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
; backend_fasmg fixture for emit.inc's REDUCTION lowering -- `redinit`,
; `contrib`, `redfin` in both shapes (docs/design/ssa-ir.md 2.6, the
; definition as decided 2026-09-25; spec 5.4). One function, two reductions,
; and the emitted fasmg TEXT checked exactly, because for `arborea` the text
; IS the definition (ADR 0012) and a change to it is a change to the
; language.
;
; WHAT IT PINS, line by line:
;
;   THE ORDINATA HALF (`redinit u64 add ordinata 0`)
;     `mov rax, 0` / `mov qword [rbp-24], rax` -- the accumulator starts at
;     the op's identity, and it lives in the REDINIT'S OWN VALUE SLOT
;     ([rbp-24] is value 3, the handle). Nothing else can read that slot
;     (`red.F` admits three opcodes; verifier rule 7), so the slot region
;     the naive layout already paid for is the whole of an `ordinata`
;     reduction's state -- an `ordinata` reduction adds NOTHING to the frame.
;     Each `contrib` is then `mov rax, [acc]` / `add rax, [value]` /
;     `jc bfausr_trap` / `mov qword [acc], rax` -- the standalone `add u64`,
;     trap included, because it is literally __bfa_emit_addsub_core.
;     `redfin` is a copy out of the accumulator into its own slot.
;
;   THE ARBOREA HALF (`redinit f32 fadd arborea 2`)
;     `mov qword [rbp-136], 0` -- the group count, the first word of the
;     frame's FOURTH region (__bfa_frame_layout), which exists only because
;     this reduction is `arborea`; slots[0] and slots[1] follow at [rbp-128]
;     and [rbp-120].
;     Each `contrib`: the count into rax, the value into rcx, and
;     `mov qword [rbp+rax*8-128], rcx` -- the one RUNTIME-COMPUTED frame
;     address in this whole backend, because slots[count] is not a literal
;     offset. Then `add rax, 1`, the count back to memory, `cmp rax, 2`, and
;     `jne <prefix>r<instid>` past the group.
;     The group, when it is full: `movss xmm0, [slots[0]]` /
;     `movss xmm1, [slots[1]]` / `addss` -- the pairwise tree, in place, over
;     the cells themselves -- then the same shape again folding slots[0] into
;     the accumulator, and `mov qword [count], 0`.
;     `redfin`: `mov rax, [count]` / `cmp rax, 1` / `je ...t1` / `jmp ...z`,
;     one unrolled tail tree per possible live count (here only k = 1, since
;     w = 2), each ending in a `jmp` to the join, and the join copying the
;     accumulator out.
;
;   THE LABELS: `<per-function prefix>r<instid>` for a `contrib`'s group
;   skip, and `r<instid>t<k>` / `r<instid>z` for `redfin`'s tail dispatch.
;   `r` is not a digit and not `c`/`e`, so these collide with neither a block
;   label (`<prefix><n>`), an edge stub (`<prefix><n>e<k>`) nor a `copy` loop
;   (`<prefix>c<n>`).
;
;   THE FLOAT CELLS carry the canonical slot form of an f32: `movd eax, xmm0`
;   zero-extends and the store is a `qword`, so the upper four bytes of every
;   cell are zero (emit.inc's __bfa_emit_fstore_cell, and ssa-ir.md 2.2).
;
; The running half is tests/ir/: red_ordinata_f32, red_ordinata_i64,
; red_arborea_f32 (the twenty f32 contributions whose two shapes differ by 9
; ulps, with the expected bits computed from the definition),
; red_arborea_int_trap, red_empty, red_w1, red_mul, and
; reject_verify_red_width.
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
  db "functio @contrahe (ptr u64) -> u64 {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = param u64 1", 10
  db "%2 = redinit u64 add ordinata 0", 10
  db "contrib %2 %1", 10
  db "contrib %2 %1", 10
  db "%3 = redfin u64 %2", 10
  db "%4 = redinit f32 fadd arborea 2", 10
  db "%5 = fconst f32 1065353216", 10
  db "contrib %4 %5", 10
  db "contrib %4 %5", 10
  db "contrib %4 %5", 10
  db "%6 = redfin f32 %4", 10
  db "ret %3", 10
  db "}", 10
  .len = $ - srctext
  expected:
  db "bfausr_contrahe:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 144", 10
  db "bfausr_contrahe_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	rax, 0", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	add	rax, [rbp-16]", 10
  db "	jc	bfausr_trap", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	add	rax, [rbp-16]", 10
  db "	jc	bfausr_trap", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	qword [rbp-48], rax", 10
  db "	mov	rax, 0", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	qword [rbp-136], 0", 10
  db "	mov	rax, 1065353216", 10
  db "	mov	qword [rbp-64], rax", 10
  db "	mov	rax, [rbp-136]", 10
  db "	mov	rcx, [rbp-64]", 10
  db "	mov	qword [rbp+rax*8-128], rcx", 10
  db "	add	rax, 1", 10
  db "	mov	qword [rbp-136], rax", 10
  db "	cmp	rax, 2", 10
  db "	jne	bfausr_contrahe_br9", 10
  db "	movss	xmm0, dword [rbp-128]", 10
  db "	movss	xmm1, dword [rbp-120]", 10
  db "	addss	xmm0, xmm1", 10
  db "	movd	eax, xmm0", 10
  db "	mov	qword [rbp-128], rax", 10
  db "	movss	xmm0, dword [rbp-56]", 10
  db "	movss	xmm1, dword [rbp-128]", 10
  db "	addss	xmm0, xmm1", 10
  db "	movd	eax, xmm0", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	qword [rbp-136], 0", 10
  db "bfausr_contrahe_br9:", 10
  db "	mov	rax, [rbp-136]", 10
  db "	mov	rcx, [rbp-64]", 10
  db "	mov	qword [rbp+rax*8-128], rcx", 10
  db "	add	rax, 1", 10
  db "	mov	qword [rbp-136], rax", 10
  db "	cmp	rax, 2", 10
  db "	jne	bfausr_contrahe_br10", 10
  db "	movss	xmm0, dword [rbp-128]", 10
  db "	movss	xmm1, dword [rbp-120]", 10
  db "	addss	xmm0, xmm1", 10
  db "	movd	eax, xmm0", 10
  db "	mov	qword [rbp-128], rax", 10
  db "	movss	xmm0, dword [rbp-56]", 10
  db "	movss	xmm1, dword [rbp-128]", 10
  db "	addss	xmm0, xmm1", 10
  db "	movd	eax, xmm0", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	qword [rbp-136], 0", 10
  db "bfausr_contrahe_br10:", 10
  db "	mov	rax, [rbp-136]", 10
  db "	mov	rcx, [rbp-64]", 10
  db "	mov	qword [rbp+rax*8-128], rcx", 10
  db "	add	rax, 1", 10
  db "	mov	qword [rbp-136], rax", 10
  db "	cmp	rax, 2", 10
  db "	jne	bfausr_contrahe_br11", 10
  db "	movss	xmm0, dword [rbp-128]", 10
  db "	movss	xmm1, dword [rbp-120]", 10
  db "	addss	xmm0, xmm1", 10
  db "	movd	eax, xmm0", 10
  db "	mov	qword [rbp-128], rax", 10
  db "	movss	xmm0, dword [rbp-56]", 10
  db "	movss	xmm1, dword [rbp-128]", 10
  db "	addss	xmm0, xmm1", 10
  db "	movd	eax, xmm0", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	qword [rbp-136], 0", 10
  db "bfausr_contrahe_br11:", 10
  db "	mov	rax, [rbp-136]", 10
  db "	cmp	rax, 1", 10
  db "	je	bfausr_contrahe_br12t1", 10
  db "	jmp	bfausr_contrahe_br12z", 10
  db "bfausr_contrahe_br12t1:", 10
  db "	movss	xmm0, dword [rbp-56]", 10
  db "	movss	xmm1, dword [rbp-128]", 10
  db "	addss	xmm0, xmm1", 10
  db "	movd	eax, xmm0", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	jmp	bfausr_contrahe_br12z", 10
  db "bfausr_contrahe_br12z:", 10
  db "	mov	rax, [rbp-56]", 10
  db "	mov	qword [rbp-96], rax", 10
  db "	mov	rax, [rbp-48]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_trap:", 10
  db "	mov	edi, 1", 10
  db "	jmp	exsrt_abort", 10
  .len = $ - expected
