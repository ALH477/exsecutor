; tests/unit/bfa_verify_rule6_bad.asm
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
; verify.inc fixture: rule 6 (ARC): retain on a plain u64 value
; Parses the IR text below through parse.inc, runs bfa_verify_func, and
; checks the verdict is exactly BFA_VERIFY_R6_ARC (6).
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
	lea	rdi, [scr]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail1
	lea	rdi, [var]
	mov	rsi, 4194304
	call	arena_init
	jc	.fail1

	lea	rdi, [ar]
	lea	rsi, [scr]
	lea	rdx, [srctext]
	mov	rcx, srctext.len
	call	bfa_parse_module
	mov	[modout], rax

	mov	rdi, [modout]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	rax, [rdi + Vec.len]
	dec	rax
	mov	rdi, [modout]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	rsi, rax
	call	vec_get
	mov	rax, [rax]
	mov	[fn], rax

	mov	rdi, [modout]
	mov	rsi, [fn]
	lea	rdx, [var]
	lea	rcx, [outblk]
	lea	r8, [outinst]
	call	bfa_verify_func
	cmp	eax, BFA_VERIFY_R6_ARC
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
  scr: rb sizeof.Arena
  var: rb sizeof.Arena
  modout: dq 0
  fn: dq 0
  outblk: dd 0
  outinst: dd 0
  srctext:
  db "functio @r6bad (u64) -> void {", 10
  db "b0:", 10
  db "%0 = param u64 0", 10
  db "retain %0", 10
  db "ret", 10
  db "}", 10
  .len = $ - srctext
