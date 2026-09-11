; tests/unit/prelude_lege_octeto.asm
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
; `Lector.lege_octeto` -- one raw byte from a Lector's descriptor -- run from
; the prelude blob alone, with a hand-written `bfausr_initium`, exactly as
; prelude_scribe_octeto.asm runs the writer.
;
; THE BYTES COME FROM THE HARNESS. `; TEST:`'s `stdin=` (tests/run.sh,
; added with this fixture) opens tests/data/lector_quattuor.bin as the
; process's fd 0, so the descriptor this fixture reads is the one
; `Lector.ab_introitu(a)` derived from `ExsAmbitus.in` -- not a descriptor
; the fixture opened for itself. The alternative, `openat` on a path, would
; have tested a file the prelude never sees and would have put a second
; syscall in the fixture for no gain.
;
; WHAT IT PROVES:
;   1. `Lector.ab_introitu(a)` fills the record `Scriptor.ad_exitum(a)` fills,
;      with `ExsAmbitus.in`: the descriptor read back through
;      `interface.inc`'s OWN offset (runtime.md H4) is 0.
;   2. Every byte comes back EXACTLY, 0x00 0x7f 0x80 0xff -- the ends of the
;      byte, the last ASCII byte, and a lone continuation byte. None of the
;      last three is UTF-8 on its own, which is the point: a `textus` reader
;      could not deliver them (spec 5.1).
;   3. End of input is 256 -- a value no byte has -- and it is STICKY: the
;      call after it answers 256 again rather than blocking or repeating the
;      last byte. A driver loops until 256 and must be able to stop.
;   4. A descriptor the kernel refuses (-1, EBADF) is 256 as well. That is
;      the documented hole and this fixture pins it: EOF and error are NOT
;      distinguished until `eventus` exists (prelude.asm's own comment,
;      interface.inc row 10's APERTUM).
;   5. The routine touches no callee-saved register: `rbx` and `r15` carry
;      sentinels across every call.
;   6. The binary's syscall surface passes the audit. `read` and `exit_group`
;      are the prelude's here, plus `exsrt_abort`'s unreached `write`; all
;      three are on the compiler's nine that `audit=pass` checks against. The
;      per-atom half -- that this `read` needs `ambitus` and nothing else --
;      is what tests/programs/lector audits with `--potestates`.
;
; WHAT IT CANNOT PROVE: the EINTR retry, for `scribe`'s reason -- it needs a
; second process. `[UNTESTED]`.
;
; Exit: 0 = every check above. 21 = Lector.descriptor was not 0. 30+k = byte
; k (0-based) came back wrong. 40 = the read past the last byte was not 256.
; 41 = the read after THAT was not 256. 42 = the EBADF read was not 256.
; 53 = a callee-saved register was clobbered.
;
; TEST: run=yes expect-exit=0 audit=pass stdin=tests/data/lector_quattuor.bin

include 'format/format.inc'

format ELF64 executable 3
entry exsrt_start

; ---- what a wrapper must define (prelude/README.md) -------------------------
; {Mundus, ambitus}: the closure a program that reads stdin has.
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
EXS_MXCSR		= 0x1F80	; ad_parem + conservata (runtime.md 2.7)

EXSFX_SENTINEL_B	= 0x0123456789ABCDEF
EXSFX_SENTINEL_F	= 0xFEDCBA9876543210
EXSFX_FINIS		= 256		; the sentinel, restated so a changed
					; blob and an unchanged fixture disagree

segment readable executable

include '../../compiler/x86_64/prelude/prelude.asm'
; after the blob, so interface.inc's H4 cross-asserts fire (prelude/README.md)
include '../../compiler/x86_64/prelude/interface.inc'

; ---- the module -------------------------------------------------------------
; Frame: [rbp-8..-40] the five callee-saved registers this routine uses,
; [rbp-64] the Lector over stdin, [rbp-80] a copy with descriptor -1.
; push rbp + five pushes + 56 leaves rsp 16-aligned at every call.
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

	call	bfausr_exsrt_mundus_ambitus	; rdi is already the Mundus carrier
	lea	rdi, [rbp - 64]			; hidden return ptr first (IR 2.9)
	mov	rsi, rax			; a
	call	bfausr_exsrt_lector_ab_introitu
	; read through the INTERFACE's offset (runtime.md H4)
	mov	eax, [rbp - 64 + EXS_IFACE_LECTOR_DESCRIPTOR]
	test	eax, eax
	jnz	.descriptor_malus

	xor	r14d, r14d			; r14 = k, the byte index
  .proba:
	lea	rdi, [rbp - 64]
	call	bfausr_exsrt_lector_lege_octeto
	movzx	ecx, byte [exsfx_octeti + r14]
	cmp	rax, rcx
	jne	.octetus_malus
	inc	r14
	cmp	r14, EXSFX_OCTETI_N
	jb	.proba

	; the input is spent: 256, and 256 again
	lea	rdi, [rbp - 64]
	call	bfausr_exsrt_lector_lege_octeto
	cmp	rax, EXSFX_FINIS
	jne	.finis_malus
	lea	rdi, [rbp - 64]
	call	bfausr_exsrt_lector_lege_octeto
	cmp	rax, EXSFX_FINIS
	jne	.iterum_malus

	; the same Lector, a descriptor the kernel refuses
	mov	rax, [rbp - 64]
	mov	[rbp - 80], rax
	mov	rax, [rbp - 56]
	mov	[rbp - 72], rax
	mov	dword [rbp - 80 + EXS_IFACE_LECTOR_DESCRIPTOR], -1
	lea	rdi, [rbp - 80]
	call	bfausr_exsrt_lector_lege_octeto
	cmp	rax, EXSFX_FINIS
	jne	.error_malus

	mov	rax, EXSFX_SENTINEL_B
	cmp	rbx, rax
	jne	.conservata_mala
	mov	rax, EXSFX_SENTINEL_F
	cmp	r15, rax
	jne	.conservata_mala

	xor	eax, eax
	jmp	.exi

  .descriptor_malus:
	mov	eax, 21
	jmp	.exi
  .octetus_malus:
	lea	eax, [r14 + 30]
	jmp	.exi
  .finis_malus:
	mov	eax, 40
	jmp	.exi
  .iterum_malus:
	mov	eax, 41
	jmp	.exi
  .error_malus:
	mov	eax, 42
	jmp	.exi
  .conservata_mala:
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

segment readable

; The bytes tests/data/lector_quattuor.bin holds, in its order. The file is
; the input and this is the expectation; they are two files on purpose, so a
; reader that returned the LAST byte again, or dropped one, shows here.
exsfx_octeti:
	db	0x00
	db	0x7f
	db	0x80
	db	0xff
EXSFX_OCTETI_N = $ - exsfx_octeti
assert EXSFX_OCTETI_N = 4

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
