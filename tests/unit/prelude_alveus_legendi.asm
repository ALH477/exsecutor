; tests/unit/prelude_alveus_legendi.asm
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
; The READ BUFFER behind `Lector.lege_octeto` (docs/design/runtime.md 2.4, as
; amended; compiler/x86_64/prelude/prelude.asm) -- refills measured by the
; kernel, every byte checked, run from the prelude blob alone with a
; hand-written `bfausr_initium`.
;
; THE INPUT DESCRIBES ITSELF. tests/data/alveus_legendi.bin is 135,169 bytes
; (two whole 64 KiB buffers and 4,097 more, so the last refill is short and
; odd), and byte i is `(i ^ (i >> 8) ^ (i >> 16)) & 0xff`. The fixture
; computes the expected byte from its own index, so a lost, duplicated or
; reordered block fails at the first wrong byte -- the `i >> 16` term is what
; makes a whole lost 64 KiB block visible, which `i & 0xff` alone would not.
; Regenerate with:
;   python3 -c "open('tests/data/alveus_legendi.bin','wb').write(bytes(((i^(i>>8)^(i>>16))&0xff) for i in range(135169)))"
;
; WHAT IT PROVES:
;   1. The first call takes a WHOLE BUFFER in one `read`: stdin's offset,
;      `lseek(0, 0, SEEK_CUR)`, is 65536 after it, not 1.
;   2. The buffer is drained before the next `read`: after byte 65535 the
;      offset is still 65536; after byte 65536 it is 131072.
;   3. A call naming ANOTHER descriptor while bytes are pending (a Lector
;      with descriptor -1, after byte 999) reads that descriptor unbuffered
;      -- 256, EBADF -- and the pending bytes survive: byte 1000 is next.
;   4. Every one of the 135,169 bytes comes back, in order, and then 256,
;      and 256 again (end of input is not cached, and is sticky on a file
;      because the file is).
;   5. No callee-saved register is touched: `rbx`, `r15` carry sentinels.
;
; Exit: 0 = all of it. 21 = Lector.descriptor was not 0. 30 = a byte came back
; wrong (the index is lost; rerun by hand and count). 31 = the first call's
; offset was not 65536. 32 = the offset moved before the buffer was empty.
; 33 = the second refill's offset was not 131072. 34 = the foreign-descriptor
; call did not answer 256. 35 = the count was not 135,169. 36 = the call
; after end of input was not 256. 50 = stdin not seekable. 53 = a
; callee-saved register changed.
;
; TEST: run=yes expect-exit=0 audit=pass stdin=tests/data/alveus_legendi.bin

include 'format/format.inc'

format ELF64 executable 3
entry exsrt_start

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
EXSFX_N			= 135169

segment readable executable

include '../../compiler/x86_64/prelude/prelude.asm'
include '../../compiler/x86_64/prelude/interface.inc'

; Frame: [rbp-8..-40] five callee-saved registers, [rbp-64] the Lector over
; stdin, [rbp-80] a copy with descriptor -1. r12 = index of the next byte.
bfausr_initium:
	push	rbp
	mov	rbp, rsp
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	sub	rsp, 56

	mov	rbx, EXSFX_SENTINEL_B
	mov	r15, EXSFX_SENTINEL_F

	call	bfausr_exsrt_mundus_ambitus
	lea	rdi, [rbp - 64]
	mov	rsi, rax
	call	bfausr_exsrt_lector_ab_introitu
	mov	eax, [rbp - 64 + EXS_IFACE_SCRIPTOR_DESCRIPTOR]
	test	eax, eax
	jnz	.mala_21
	mov	rax, [rbp - 64]
	mov	[rbp - 80], rax
	mov	rax, [rbp - 56]
	mov	[rbp - 72], rax
	mov	dword [rbp - 80 + EXS_IFACE_SCRIPTOR_DESCRIPTOR], -1

	call	exsfx_positio
	cmp	rax, -4096
	ja	.non_quaeribilis

	xor	r12d, r12d
  .lege:
	lea	rdi, [rbp - 64]
	call	bfausr_exsrt_lector_lege_octeto
	cmp	eax, 256
	je	.finis
	mov	r13d, eax			; the byte
	; expected: (i ^ i>>8 ^ i>>16) & 0xff
	mov	rax, r12
	mov	rdx, r12
	shr	rdx, 8
	xor	rax, rdx
	mov	rdx, r12
	shr	rdx, 16
	xor	rax, rdx
	and	eax, 0xff
	cmp	eax, r13d
	jne	.mala_30
	; the offsets at the refill boundaries
	cmp	r12, 0
	je	.prima
	cmp	r12, 65535
	je	.ultima_primi
	cmp	r12, 65536
	je	.secunda
	cmp	r12, 999
	je	.aliena
	jmp	.proximus
  .prima:
	call	exsfx_positio
	cmp	rax, 65536
	jne	.mala_31
	jmp	.proximus
  .ultima_primi:
	call	exsfx_positio
	cmp	rax, 65536
	jne	.mala_32
	jmp	.proximus
  .secunda:
	call	exsfx_positio
	cmp	rax, 131072
	jne	.mala_33
	jmp	.proximus
  .aliena:
	lea	rdi, [rbp - 80]
	call	bfausr_exsrt_lector_lege_octeto
	cmp	eax, 256
	jne	.mala_34
  .proximus:
	inc	r12
	jmp	.lege

  .finis:
	cmp	r12, EXSFX_N
	jne	.mala_35
	lea	rdi, [rbp - 64]
	call	bfausr_exsrt_lector_lege_octeto
	cmp	eax, 256
	jne	.mala_36

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
  .mala_21:
	mov	eax, 21
	jmp	.exi
  .mala_30:
	mov	eax, 30
	jmp	.exi
  .mala_31:
	mov	eax, 31
	jmp	.exi
  .mala_32:
	mov	eax, 32
	jmp	.exi
  .mala_33:
	mov	eax, 33
	jmp	.exi
  .mala_34:
	mov	eax, 34
	jmp	.exi
  .mala_35:
	mov	eax, 35
	jmp	.exi
  .mala_36:
	mov	eax, 36
	jmp	.exi
  .mala_53:
	mov	eax, 53
  .exi:
	add	rsp, 56
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	pop	rbp
	ret

exsfx_positio:
	xor	edi, edi			; stdin
	xor	esi, esi
	mov	edx, 1				; SEEK_CUR
	mov	eax, 8				; lseek
	syscall
	ret

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
