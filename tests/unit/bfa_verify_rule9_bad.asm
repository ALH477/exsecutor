; tests/unit/bfa_verify_rule9_bad.asm
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
; verify.inc fixture: rule 9 (no `nop`), violating twin of
; bfa_verify_rule9_ok.asm. Builds the IDENTICAL IR through ir.inc's own
; procs (not parse.inc's text grammar -- there is no `nop` mnemonic to
; spell in text; this file's own FINDING 2), then pokes the `ret`
; instruction's `BfaInst.op` field to 0 directly -- this file's own
; inference for "nop" (0 is print.inc's documented unused opcode-table
; sentinel; see FINDING 2's full reasoning). Expect verdict
; BFA_VERIFY_R9_NONOP (9).
;
; Exit 0 = verdict matched; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=skip

include 'format/format.inc'

format ELF64 executable 3
entry start

; backend_fasmg/ir.inc no longer includes rt/ (the consumer brings it, as
; every other module's does), so this fixture brings the chain itself (span.inc includes intern.inc).
include '../../compiler/x86_64/rt/span.inc'
include '../../compiler/x86_64/backend_fasmg/verify.inc'

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 16777216
	call	arena_init
	jc	.fail1
	lea	rdi, [var]
	mov	rsi, 4194304
	call	arena_init
	jc	.fail1

	lea	rdi, [r9mdl]
	lea	rsi, [ar]
	call	bfa_module_init

	; %0 = param u64 0 ; ret %0
	lea	rdi, [r9mdl]
	mov	esi, BFA_TK_U
	xor	edx, edx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	mov	[ty_u64], eax

	lea	rdi, [r9mdl]
	call	bfa_func_new
	mov	[fn], rax
	mov	rdi, [fn]
	mov	dword [rdi + BfaFunc.attrs], 0

	mov	rdi, [fn]
	call	bfa_block_new
	mov	[b0], eax

	mov	rdi, [fn]
	mov	esi, BFA_OP_PARAM
	xor	edx, edx
	mov	ecx, [ty_u64]
	xor	r8, r8
	xor	r9, r9
	call	bfa_inst_push
	mov	[v0], eax
	mov	rdi, [fn]
	mov	esi, [b0]
	mov	edx, [v0]
	call	bfa_block_link_body

	mov	rdi, [fn]
	mov	esi, BFA_OP_RET
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [v0]
	xor	r9, r9
	call	bfa_inst_push
	mov	[vret], eax
	mov	rdi, [fn]
	mov	esi, [b0]
	mov	edx, [vret]
	call	bfa_block_link_body

	; THE MUTATION: poke the ret instruction's op field to 0 ("nop") --
	; the one thing that differs from bfa_verify_rule9_ok.asm.
	mov	rdi, [fn]
	mov	esi, [vret]
	call	bfa_inst_ptr
	mov	word [rax + BfaInst.op], 0

	; sig: () -> u64, so ret's operand type matches -- rule 1 checks it
	; since wire-codec M3 (verify.inc's __bfa_verify_pa_ret).
	mov	rdi, [r9mdl + BfaModule.sigs]
	call	vec_push
	mov	ecx, [ty_u64]
	mov	[rax + BfaSig.ret_ty], ecx
	mov	dword [rax + BfaSig.p_first], 0
	mov	dword [rax + BfaSig.p_count], 0
	mov	rdi, [fn]
	mov	dword [rdi + BfaFunc.sig], 0

	lea	rdi, [r9mdl]
	mov	rsi, [fn]
	lea	rdx, [var]
	lea	rcx, [outblk]
	lea	r8, [outinst]
	call	bfa_verify_func
	cmp	eax, BFA_VERIFY_R9_NONOP
	jne	.fail2

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1: mov eax,231
	mov edi,11
	syscall
  .fail2: mov eax,231
	mov edi,12
	syscall

segment readable writeable
  ar: rb sizeof.Arena
  var: rb sizeof.Arena
  r9mdl: rb sizeof.BfaModule
  fn: dq 0
  b0: dd 0
  v0: dd 0
  vret: dd 0
  ty_u64: dd 0
  outblk: dd 0
  outinst: dd 0
