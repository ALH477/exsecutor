; tests/unit/bfa_ir_basics.asm
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
; asm-rt fixture for compiler/x86_64/backend_fasmg/ir.inc: type interning
; (dedup by 8-byte key, distinct ids for distinct kinds), and the
; function/block/instruction plumbing (bfa_func_new, bfa_block_new,
; bfa_inst_push, bfa_block_link_body, bfa_extra_push/get/set,
; bfa_value_type) with no parser involved.
;
;   1. interning u64 twice returns the SAME id; interning f32 returns a
;      DIFFERENT id; bfa_ty_u1 returns a third, distinct id
;   2. bfa_type_ptr round-trips kind/width for the interned u64
;   3. a function's two `param` instructions link into the block's body
;      list in push order (BfaInst.nextp chains %1 -> %2)
;   4. the extra pool: push three words, overwrite the middle one via
;      bfa_extra_set, read all three back in order
;   5. bfa_value_type(%1) (a `param u64`) returns the u64 type id
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

; backend_fasmg/ir.inc no longer includes rt/ (the consumer brings it, as
; every other module's does), so this fixture brings the chain itself (span.inc includes intern.inc).
include '../../compiler/x86_64/rt/span.inc'
include '../../compiler/x86_64/backend_fasmg/ir.inc'

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail1

	lea	rdi, [mdl]
	lea	rsi, [ar]
	call	bfa_module_init

	lea	rdi, [mdl]
	mov	esi, BFA_TK_U
	mov	edx, 0
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	cmp	eax, 1
	jne	.fail2
	mov	[ty_u64], eax

	lea	rdi, [mdl]
	mov	esi, BFA_TK_F32
	mov	edx, 0
	mov	ecx, 32
	xor	r8, r8
	call	bfa_type_intern
	cmp	eax, 2
	jne	.fail2

	lea	rdi, [mdl]
	mov	esi, BFA_TK_U
	mov	edx, 0
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	cmp	eax, 1
	jne	.fail2

	lea	rdi, [mdl]
	call	bfa_ty_u1
	cmp	eax, 3
	jne	.fail2

	lea	rdi, [mdl]
	mov	esi, [ty_u64]
	call	bfa_type_ptr
	movzx	ecx, byte [rax + BfaType.kind]
	cmp	ecx, BFA_TK_U
	jne	.fail3
	movzx	ecx, word [rax + BfaType.width]
	cmp	ecx, 64
	jne	.fail3

	lea	rdi, [mdl]
	call	bfa_func_new
	mov	[fn], rax

	mov	rdi, [fn]
	call	bfa_block_new
	cmp	eax, 1
	jne	.fail4
	mov	[b0], eax

	mov	rdi, [fn]
	mov	esi, BFA_OP_PARAM
	xor	edx, edx
	mov	ecx, [ty_u64]
	xor	r8, r8
	xor	r9, r9
	call	bfa_inst_push
	cmp	eax, 1
	jne	.fail4
	mov	rdi, [fn]
	mov	esi, [b0]
	mov	rdx, 1
	call	bfa_block_link_body

	mov	rdi, [fn]
	mov	esi, BFA_OP_PARAM
	xor	edx, edx
	mov	ecx, [ty_u64]
	mov	r8d, 1
	xor	r9, r9
	call	bfa_inst_push
	cmp	eax, 2
	jne	.fail4
	mov	rdi, [fn]
	mov	esi, [b0]
	mov	rdx, 2
	call	bfa_block_link_body

	mov	rdi, [fn]
	mov	esi, [b0]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first]
	cmp	ecx, 1
	jne	.fail4
	mov	ecx, [rax + BfaBlock.last]
	cmp	ecx, 2
	jne	.fail4
	mov	rdi, [fn]
	mov	esi, 1
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	cmp	ecx, 2
	jne	.fail4

	mov	rdi, [fn]
	mov	esi, 111
	call	bfa_extra_push
	cmp	eax, 0
	jne	.fail5
	mov	rdi, [fn]
	mov	esi, 222
	call	bfa_extra_push
	cmp	eax, 1
	jne	.fail5
	mov	rdi, [fn]
	mov	esi, 333
	call	bfa_extra_push
	cmp	eax, 2
	jne	.fail5
	mov	rdi, [fn]
	mov	esi, 1
	mov	edx, 999
	call	bfa_extra_set
	mov	rdi, [fn]
	mov	esi, 0
	call	bfa_extra_get
	cmp	eax, 111
	jne	.fail5
	mov	rdi, [fn]
	mov	esi, 1
	call	bfa_extra_get
	cmp	eax, 999
	jne	.fail5
	mov	rdi, [fn]
	mov	esi, 2
	call	bfa_extra_get
	cmp	eax, 333
	jne	.fail5

	mov	rdi, [fn]
	mov	esi, 1
	call	bfa_value_type
	cmp	eax, [ty_u64]
	jne	.fail6

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1: mov eax,231
	mov edi,11
	syscall
  .fail2: mov eax,231
	mov edi,12
	syscall
  .fail3: mov eax,231
	mov edi,13
	syscall
  .fail4: mov eax,231
	mov edi,14
	syscall
  .fail5: mov eax,231
	mov edi,15
	syscall
  .fail6: mov eax,231
	mov edi,16
	syscall

segment readable writeable
  ar: rb sizeof.Arena
  mdl: rb sizeof.BfaModule
  fn: dq 0
  b0: dd 0
  ty_u64: dd 0
