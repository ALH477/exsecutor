; tests/unit/prelude_alveus_scribendi.asm
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
; The WRITE BUFFER behind `Scriptor` (docs/design/runtime.md 2.4, as amended;
; compiler/x86_64/prelude/prelude.asm, "the write buffer") -- its boundaries,
; measured by the kernel, run from the prelude blob alone with a
; hand-written `bfausr_initium`, as prelude_scribe_octeto.asm runs the
; per-call contract.
;
; Every offset below is `lseek(1, 0, SEEK_CUR)` on stdout, which tests/run.sh
; points at a regular file (`>$name.runlog`). The offset is what has
; actually left the process, so it measures each flush point by what the
; kernel received rather than by what the routine says.
;
; WHAT IT PROVES, in order:
;   1. BIND: the first `scribe_octeto` goes straight through (+1).
;   2. FILL: 65536 more calls (EXS_ALVEUS_SCRIBENDI) each return 1 and the
;      offset does not move -- the buffer holds exactly its capacity.
;   3. FULL (flush point 1): the next call drains all 65536 at once and its
;      own byte waits: +65536.
;   4. A WHOLE-BUFFER `scribe` (a textus of exactly EXS_ALVEUS_SCRIBENDI
;      bytes -- the prelude's own zeroed read buffer, used as 64 KiB of
;      readable memory): the one pending byte drains, then the request goes
;      straight through, uncopied, and the call returns its full length:
;      +1 +65536.
;   5. A short `scribe` (10 bytes) returns 10 and waits: +0.
;   6. A READ drains it (flush point 3): `lege_octeto` on stdin (/dev/null
;      here, so it answers 256) moves stdout's offset by the 10 pending
;      bytes before the `read` is issued. This is what keeps a
;      request/response pipe from deadlocking on a buffered reply.
;   7. A failure found AT A FLUSH is sticky: a byte is buffered (count 1),
;      stdout is closed under it -- `close(1)`, this fixture's instrument,
;      on the compiler's nine as its `lseek` is -- and the drain fails;
;      the next call on stdout returns 0 and issues no write.
;   8. No callee-saved register is touched: `rbx`, `r15` carry sentinels.
; The exit drain (flush point 4) and the abort drain (5) are not here: the
; first is what every tests/programs/ fixture with stdout= checks, and the
; second is tests/programs/abortus_post_scripturam.
;
; Exit: 0 = all of it. 50 = stdout not seekable (run by hand into a pipe).
; 61 = the bind did not move the offset by 1. 62 = a fill call returned other
; than 1. 63 = the fill moved the offset. 64 = the overflowing call did not
; drain exactly 65536. 65 = the whole-buffer scribe's count. 66 = its offset.
; 67 = the short scribe's count or offset. 68 = the read did not drain.
; 69 = the buffered byte before close was not accepted. 70 = the call after
; the failed drain did not return 0. 53 = a callee-saved register changed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry exsrt_start

; {Mundus, ambitus}: the closure a program that writes to stdout has.
EXS_POTESTAS_MUNDUS	= 1
EXS_POTESTAS_ALLOC	= 0
EXS_POTESTAS_SERMO	= 0
EXS_POTESTAS_HOROLOGIUM	= 0
EXS_POTESTAS_ARCHIVUM	= 0
EXS_POTESTAS_RETE	= 0
EXS_POTESTAS_FORTUNA	= 0
EXS_POTESTAS_AMBITUS	= 1
EXS_POTESTAS_FILUM	= 0
EXS_POTESTAS_MACHINA	= 0
EXS_POTESTAS_CRUDUM	= 0
EXS_MXCSR		= 0x1F80

EXSFX_SENTINEL_B	= 0x0123456789ABCDEF
EXSFX_SENTINEL_F	= 0xFEDCBA9876543210

segment readable executable

include '../../compiler/x86_64/prelude/prelude.asm'
; after the blob, so interface.inc's H4 cross-asserts fire (prelude/README.md)
include '../../compiler/x86_64/prelude/interface.inc'

; Frame: [rbp-8..-40] five callee-saved registers, [rbp-64] the Scriptor over
; stdout, [rbp-80] the Lector over stdin, [rbp-96] a textus.
bfausr_initium:
	push	rbp
	mov	rbp, rsp
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	sub	rsp, 72

	mov	rbx, EXSFX_SENTINEL_B
	mov	r15, EXSFX_SENTINEL_F

	call	bfausr_exsrt_mundus_ambitus	; rdi is already the Mundus carrier
	mov	r13, rax			; a
	lea	rdi, [rbp - 64]
	mov	rsi, r13
	call	bfausr_exsrt_scriptor_ad_exitum
	lea	rdi, [rbp - 80]
	mov	rsi, r13
	call	bfausr_exsrt_lector_ab_introitu

	call	exsfx_positio
	cmp	rax, -4096
	ja	.non_quaeribilis
	mov	r12, rax			; r12 = the offset expected next

	; 1. bind: straight through
	lea	rdi, [rbp - 64]
	mov	esi, 0x41
	call	bfausr_exsrt_scriptor_scribe_octeto
	inc	r12
	call	exsfx_positio
	cmp	rax, r12
	jne	.mala_61

	; 2. fill: exactly the capacity, nothing leaves
	xor	r14d, r14d
  .imple:
	lea	rdi, [rbp - 64]
	mov	esi, r14d
	call	bfausr_exsrt_scriptor_scribe_octeto
	cmp	rax, 1
	jne	.mala_62
	inc	r14
	cmp	r14, EXS_ALVEUS_SCRIBENDI
	jb	.imple
	call	exsfx_positio
	cmp	rax, r12
	jne	.mala_63

	; 3. one more: the full buffer drains, this byte waits
	lea	rdi, [rbp - 64]
	mov	esi, 0x42
	call	bfausr_exsrt_scriptor_scribe_octeto
	add	r12, EXS_ALVEUS_SCRIBENDI
	call	exsfx_positio
	cmp	rax, r12
	jne	.mala_64

	; 4. a whole-buffer scribe: drain 1, then straight through
	lea	rax, [exsrt_lector_alveus]
	mov	[rbp - 96 + EXS_IFACE_TEXTUS_PTR], rax
	mov	qword [rbp - 96 + EXS_IFACE_TEXTUS_LEN], EXS_ALVEUS_SCRIBENDI
	lea	rdi, [rbp - 64]
	lea	rsi, [rbp - 96]
	call	bfausr_exsrt_scriptor_scribe
	cmp	rax, EXS_ALVEUS_SCRIBENDI
	jne	.mala_65
	add	r12, 1 + EXS_ALVEUS_SCRIBENDI
	call	exsfx_positio
	cmp	rax, r12
	jne	.mala_66

	; 5. a short scribe waits
	lea	rax, [exsfx_decem]
	mov	[rbp - 96 + EXS_IFACE_TEXTUS_PTR], rax
	mov	qword [rbp - 96 + EXS_IFACE_TEXTUS_LEN], 10
	lea	rdi, [rbp - 64]
	lea	rsi, [rbp - 96]
	call	bfausr_exsrt_scriptor_scribe
	cmp	rax, 10
	jne	.mala_67
	call	exsfx_positio
	cmp	rax, r12
	jne	.mala_67

	; 6. a read drains it first
	lea	rdi, [rbp - 80]
	call	bfausr_exsrt_lector_lege_octeto
	add	r12, 10
	call	exsfx_positio
	cmp	rax, r12
	jne	.mala_68

	; 7. a failure at a flush is sticky
	lea	rdi, [rbp - 64]
	mov	esi, 0x43
	call	bfausr_exsrt_scriptor_scribe_octeto
	cmp	rax, 1
	jne	.mala_69
	mov	edi, 1
	mov	eax, 3				; close -- the fixture's instrument
	syscall
	call	exsrt_scriptor_purga		; write(1) -> EBADF: FRACTUS
	lea	rdi, [rbp - 64]
	mov	esi, 0x44
	call	bfausr_exsrt_scriptor_scribe_octeto
	test	rax, rax
	jnz	.mala_70

	; 8. callee-saved
	mov	rax, EXSFX_SENTINEL_B
	cmp	rbx, rax
	jne	.mala_53
	mov	rax, EXSFX_SENTINEL_F
	cmp	r15, rax
	jne	.mala_53
	xor	eax, eax
	jmp	.exi

  .non_quaeribilis:
	mov	eax, 50
	jmp	.exi
  .mala_53:
	mov	eax, 53
	jmp	.exi
  .mala_61:
	mov	eax, 61
	jmp	.exi
  .mala_62:
	mov	eax, 62
	jmp	.exi
  .mala_63:
	mov	eax, 63
	jmp	.exi
  .mala_64:
	mov	eax, 64
	jmp	.exi
  .mala_65:
	mov	eax, 65
	jmp	.exi
  .mala_66:
	mov	eax, 66
	jmp	.exi
  .mala_67:
	mov	eax, 67
	jmp	.exi
  .mala_68:
	mov	eax, 68
	jmp	.exi
  .mala_69:
	mov	eax, 69
	jmp	.exi
  .mala_70:
	mov	eax, 70
  .exi:
	add	rsp, 72
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	pop	rbp
	ret

exsfx_positio:
	mov	edi, 1
	xor	esi, esi
	mov	edx, 1				; SEEK_CUR
	mov	eax, 8				; lseek
	syscall
	ret

segment readable

exsfx_decem:
	db	'decem byte'

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
