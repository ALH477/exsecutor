; tests/unit/bfa_emit_tier1.asm
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
; asm-rt fixture for compiler/x86_64/backend_fasmg/emit.inc's Tier-1 naive
; lowering. Parses a small function exercising `param`, `add` (trapping),
; `cmp.lt`, `br`, `addw` (wrapping), `ret`, emits it, and checks the emitted
; fasmg TEXT against an exact expected string.
;
; THIS IS NOT THE WHOLE STORY -- see the report for what actually proves
; the emitter correct. This fixture only proves the text is byte-for-byte
; what this pass has always produced (a regression check); it cannot itself
; feed that text back through `fasmg` and run the result (no `execve` on
; this project's closed syscall allowlist, by design -- CLAUDE.md). That
; second half -- assemble the emitted text with the real `fasmg` and run
; it, with concrete arguments, checking the actual numeric result AND the
; overflow trap -- was done by hand, outside this harness, and is reported
; verbatim (command and output) in the agent's report rather than faked
; here as something this sandboxed process cannot actually do.
;
; Exit 0 = emitted text matches exactly. 12 = it does not (dump both to
; stderr first, for inspection).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

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
  db "functio @combo (u64 u64) -> u64 {", 10
  db "b0:", 10
  db "%0 = param u64 0", 10
  db "%1 = param u64 1", 10
  db "%2 = add u64 %0 %1", 10
  db "%3 = cmp.lt u64 %0 %1", 10
  db "br %3 b1 b2", 10
  db "b1:", 10
  db "%4 = addw u64 %2 1", 10
  db "ret %4", 10
  db "b2:", 10
  db "ret %2", 10
  db "}", 10
  .len = $ - srctext
  expected:
  db "bfausr_combo:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 80", 10
  db "bfausr_combo_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	rax, [rbp-8]", 10
  db "	add	rax, [rbp-16]", 10
  db "	jc	bfausr_trap", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rax, [rbp-8]", 10
  db "	cmp	rax, [rbp-16]", 10
  db "	setb	al", 10
  db "	movzx	rax, al", 10
  db "	mov	qword [rbp-32], rax", 10
  db "	mov	rax, [rbp-32]", 10
  db "	test	rax, rax", 10
  db "	jz	bfausr_combo_b3", 10
  db "	jmp	bfausr_combo_b2", 10
  db "bfausr_combo_b2:", 10
  db "	mov	rax, [rbp-24]", 10
  db "	add	rax, 1", 10
  db "	mov	qword [rbp-48], rax", 10
  db "	mov	rax, [rbp-48]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_combo_b3:", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_trap:", 10
  db "	db	0x0F, 0x0B", 10
  .len = $ - expected
