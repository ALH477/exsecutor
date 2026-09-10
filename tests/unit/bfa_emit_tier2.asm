; tests/unit/bfa_emit_tier2.asm
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
; emit.inc Tier 2: `slot`, `gaddr`, `load`/`store`, `call` under SysV, and
; `iconst`/`ret` at `u8` -- exercised by THE HELLO WORLD'S OWN THREE
; FUNCTIONS, docs/design/runtime.md section 5's IR, hand-written here
; because no lowering exists to produce it. Emitted, and compared to an
; exact expected text: a regression check, exactly like bfa_emit_tier1.asm,
; and with the same limit -- this fixture cannot assemble what it emitted
; and run it, because tests/run.sh has no `execve`. That half was done by
; hand and is in the agent's report, command and output verbatim.
;
; WHAT DIFFERS FROM runtime.md SECTION 5's LISTING, and why:
;
;   * `data $0`, `gaddr 0`. Section 5 writes `data $1` and `gaddr 1`.
;     parse.inc requires global ids to be DENSE FROM 0 ("out-of-sequence
;     global $N" otherwise), so `$1` as the first global does not parse.
;     The code is the older fact and section 5 the newer text; reported as
;     a finding rather than worked around by renumbering the parser.
;
;   * The three prelude routines are declared as `functio ... externus
;     sysv_amd64` so `call @exsrt_scriptor_scribe` resolves. Section 2.11
;     of ssa-ir.md has no bodiless textual form (lowering.md finding 16),
;     so each declaration carries a one-line placeholder body -- which
;     `bfa_emit_module` never emits, because it skips `externus`. The
;     placeholder is the finding made concrete, not a design choice.
;
; The emitted text is checked in full, so every one of these is checked
; with it: the two-region frame (`slot` storage below the value slots,
; `lea rax, [rbp-96]` / `[rbp-112]` for `initium`'s two 16-byte slots,
; frame size 112 and 16-byte aligned so every `call` is aligned); `gaddr 0`
; naming `bfausr_g0`, the label bfa_emit_program emits in the data segment;
; `store ptr` and `store u64` through a slot-held pointer at offsets 0 and
; 8; SysV argument registers in order; a `void` call storing nothing and a
; `u64` call storing `rax`; and `ret` on a `u8`.
;
; Exit 0 = emitted text matches exactly. 12 = it does not (the emitted text
; goes to stderr first, for inspection).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/backend_fasmg/emit.inc'

segment readable executable
  start:
	lea	rdi, [t2ar]
	mov	rsi, 16777216
	call	arena_init
	jc	.fail1
	lea	rdi, [t2scr]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail1

	lea	rdi, [t2ar]
	lea	rsi, [t2scr]
	lea	rdx, [t2src]
	mov	rcx, t2src.len
	call	bfa_parse_module
	mov	[t2mod], rax

	mov	rdi, [t2mod]
	lea	rsi, [t2ar]
	call	bfa_emit_module
	mov	[t2outp], rax
	mov	[t2outl], rdx

	mov	rdi, [t2outp]
	mov	rsi, [t2outl]
	lea	rdx, [t2exp]
	mov	rcx, t2exp.len
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
	mov	rsi, [t2outp]
	mov	rdx, [t2outl]
	call	sys_write
	mov	eax,231
	mov	edi,12
	syscall

segment readable writeable
  t2ar:   rb sizeof.Arena
  t2scr:  rb sizeof.Arena
  t2mod:  dq 0
  t2outp: dq 0
  t2outl: dq 0
  t2src:
  db "data $0 101 1 4176652c206d756e6475732e0a0a45782073696c656e74696f2073757267697420666f726d612e0a4578207369676e6f206e6173636974757220766f782e0a457820636f6469636520666974206c756d656e2e0a0a486f64696520696e636970696d75732e", 10
  db "", 10
  db "functio @exsrt_mundus_ambitus (ptr) -> ptr numeri ad_parem vetita explicita conservata externus sysv_amd64 {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "ret %0", 10
  db "}", 10
  db "", 10
  db "functio @exsrt_scriptor_ad_exitum (ptr ptr) -> void numeri ad_parem vetita explicita conservata externus sysv_amd64 {", 10
  db "b0:", 10
  db "ret", 10
  db "}", 10
  db "", 10
  db "functio @exsrt_scriptor_scribe (ptr ptr) -> u64 numeri ad_parem vetita explicita conservata externus sysv_amd64 {", 10
  db "b0:", 10
  db "%0 = iconst u64 0", 10
  db "ret %0", 10
  db "}", 10
  db "", 10
  db "functio @saluta (ptr) -> void numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = gaddr 0", 10
  db "store ptr %0 0 nativus %1", 10
  db "%2 = iconst u64 101", 10
  db "store u64 %0 8 nativus %2", 10
  db "ret", 10
  db "}", 10
  db "", 10
  db "functio @imprime_gutenbergio (ptr ptr) -> u64 numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = param ptr 1", 10
  db "%2 = call u64 @exsrt_scriptor_scribe %0 %1", 10
  db "ret %2", 10
  db "}", 10
  db "", 10
  db "functio @initium (ptr) -> u8 numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = call ptr @exsrt_mundus_ambitus %0", 10
  db "%2 = slot 16 8", 10
  db "call void @exsrt_scriptor_ad_exitum %2 %1", 10
  db "%3 = slot 16 8", 10
  db "call void @saluta %3", 10
  db "%4 = call u64 @imprime_gutenbergio %2 %3", 10
  db "%5 = iconst u8 0", 10
  db "ret %5", 10
  db "}", 10
  .len = $ - t2src
  t2exp:
  db "bfausr_saluta:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 64", 10
  db "bfausr_saluta_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	lea	rax, [bfausr_g0]", 10
  db "	mov	qword [rbp-16], rax", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-16]", 10
  db "	mov	qword [rax+0], rcx", 10
  db "	mov	qword [rbp-32], 101", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-32]", 10
  db "	mov	qword [rax+8], rcx", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_imprime_gutenbergio:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 48", 10
  db "bfausr_imprime_gutenbergio_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	rdi, [rbp-8]", 10
  db "	mov	rsi, [rbp-16]", 10
  db "	call	bfausr_exsrt_scriptor_scribe", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_initium:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 112", 10
  db "bfausr_initium_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	rdi, [rbp-8]", 10
  db "	call	bfausr_exsrt_mundus_ambitus", 10
  db "	mov	qword [rbp-16], rax", 10
  db "	lea	rax, [rbp-96]", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rdi, [rbp-24]", 10
  db "	mov	rsi, [rbp-16]", 10
  db "	call	bfausr_exsrt_scriptor_ad_exitum", 10
  db "	lea	rax, [rbp-112]", 10
  db "	mov	qword [rbp-40], rax", 10
  db "	mov	rdi, [rbp-40]", 10
  db "	call	bfausr_saluta", 10
  db "	mov	rdi, [rbp-24]", 10
  db "	mov	rsi, [rbp-40]", 10
  db "	call	bfausr_imprime_gutenbergio", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	qword [rbp-64], 0", 10
  db "	mov	rax, [rbp-64]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_trap:", 10
  db "	mov	edi, 1", 10
  db "	jmp	exsrt_abort", 10
  .len = $ - t2exp
