; tests/unit/bfc_face_vector.asm
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 DeMoD LLC.
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
; backend_c fixture: the FACE's refusal of a type it cannot spell
; (backend_c/face_c.inc's `__bfc_face_refuse`, docs/design/c-backend.md D9).
; A vector float has a C spelling INSIDE a unit (the prologue's
; `exs_vf32_N` typedef) but none a host can share: the header carries no
; prologue, and the C backend refuses a vector at a call boundary by name
; anyway. So a `publica` function with a `vf32.4` parameter is refused --
; `bfc: emitter: face: ...`, exit 4 -- rather than given a prototype naming
; a typedef the header does not define. Exit 0 here means it returned one.
;
; Mutation (run): the class-4 test in `__bfc_face_csig` deleted -> 0.
;
; TEST: run=yes expect-exit=4 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/span.inc'
include '../../compiler/x86_64/backend_fasmg/verify.inc'
include '../../compiler/x86_64/backend_c/program_c.inc'

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
	mov	rdi, rax
	lea	rsi, [ar]
	lea	rdx, [mask]
	mov	ecx, BFC_FACIES_H
	call	bfc_emit_face		; must not return: __bfc_die exits 4
	mov	eax, 231
	xor	edi, edi		; 0 = it returned, which is the failure
	syscall
  .fail1:
	mov	eax, 231
	mov	edi, 11
	syscall

segment readable writeable
  ar:     rb sizeof.Arena
  scr:    rb sizeof.Arena

  mask:
	db BFC_FACE_PUBLICA			; @lanae

  srctext:
  db "functio @lanae (vf32.4) -> u64 {", 10
  db "b0:", 10
  db "%0 = iconst u64 0", 10
  db "ret %0", 10
  db "}", 10
  .len = $ - srctext
