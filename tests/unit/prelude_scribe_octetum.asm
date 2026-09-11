; tests/unit/prelude_scribe_octetum.asm
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
; `Scriptor.scribe_octetum` -- one raw byte to a Scriptor's descriptor
; (docs/design/wire-codec.md D7) -- run from the prelude blob alone, with a
; hand-written `bfausr_initium`, exactly as prelude_scribe.asm runs `scribe`.
;
; WHAT IT PROVES:
;   1. Each call writes EXACTLY ONE byte, and the kernel says so, not the
;      routine: after every call the fixture asks `lseek(1, 0, SEEK_CUR)` for
;      stdout's file offset and requires it to have moved by one. A routine
;      that wrote the byte twice, or not at all, and still returned 1 fails
;      here. tests/run.sh points stdout at a regular file (`>$name.runlog`),
;      so the offset exists; run by hand into a pipe or a terminal the
;      fixture exits 50 and says why rather than guessing.
;   2. Each call RETURNS 1, the count, as `scribe` returns its count.
;   3. The bytes include 0x00, 0xff, 0x80 and 0xd3 -- none of which is UTF-8
;      on its own, which is the point: `scribe` takes a `textus` and spec 5.1
;      makes that UTF-8, so only this routine can write them.
;   4. Only `b`'s LOW BYTE matters. Two calls pass `rsi` with garbage in bits
;      8-63 (SysV leaves them unspecified for a `u8`); the count and the
;      offset are still exactly 1. The program test (tests/programs/octeti)
;      checks the byte VALUES, with `cmp` against a binary expected.out; this
;      fixture cannot read its own stdout back without a syscall the prelude
;      does not have.
;   5. The error convention is `scribe`'s: on a descriptor the kernel refuses
;      (-1, EBADF) the call returns 0 and the offset does not move.
;   6. The routine touches no callee-saved register: `rbx` and `r15` carry
;      sentinels across every call, and `r12`-`r14` carry this fixture's own
;      state, so a clobber shows as a wrong exit, not a crash.
;   7. The binary's syscall surface passes the audit. `write` and
;      `exit_group` are the prelude's; the one `lseek` is THIS FIXTURE's
;      measuring instrument (`exsfx_positio`, below), not the blob's, and is
;      on the compiler's nine that `audit=pass` checks against. The prelude's
;      own surface under {Mundus, ambitus} -- write and exit_group, nothing
;      added -- is what tests/programs/octeti audits with `--potestates`.
;
; WHAT IT CANNOT PROVE: the EINTR retry and the zero-return retry, for
; `scribe`'s reason -- both need a second process. `[UNTESTED]`.
;
; Exit: 7 = every check above (seven bytes written, seven counts of 1).
; 21 = Scriptor.descriptor was not 1. 30+k = call k (0-based) returned a
; count other than 1. 40+k = after call k the offset did not move by exactly
; one. 50 = stdout is not seekable (not run under tests/run.sh). 51 = the
; EBADF call returned non-zero. 52 = the EBADF call moved the offset.
; 53 = a callee-saved register was clobbered.
;
; TEST: run=yes expect-exit=7 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry exsrt_start

; ---- what a wrapper must define (prelude/README.md) -------------------------
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
EXS_MXCSR		= 0x1F80	; ad_parem + conservata (runtime.md 2.7)

EXSFX_SENTINEL_B	= 0x0123456789ABCDEF
EXSFX_SENTINEL_F	= 0xFEDCBA9876543210

segment readable executable

include '../../compiler/x86_64/prelude/prelude.asm'
; after the blob, so interface.inc's H4 cross-asserts fire (prelude/README.md)
include '../../compiler/x86_64/prelude/interface.inc'

; ---- the module -------------------------------------------------------------
; Frame: [rbp-8..-40] the five callee-saved registers this routine uses,
; [rbp-64] the Scriptor over stdout, [rbp-80] a copy with descriptor -1.
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
	call	bfausr_exsrt_scriptor_ad_exitum
	; read through the INTERFACE's offset (runtime.md H4)
	mov	eax, [rbp - 64 + EXS_IFACE_SCRIPTOR_DESCRIPTOR]
	cmp	eax, 1
	jne	.descriptor_malus

	call	exsfx_positio			; the offset before any write
	cmp	rax, -4096
	ja	.non_quaeribilis
	mov	r12, rax			; r12 = the offset expected next
	xor	r13d, r13d			; r13 = the sum of the counts
	xor	r14d, r14d			; r14 = k, the call index

  .proba:
	lea	rdi, [rbp - 64]
	mov	rsi, [exsfx_octeti + r14*8]	; all 64 bits, garbage included
	call	bfausr_exsrt_scriptor_scribe_octetum
	cmp	rax, 1
	jne	.numerus_malus
	add	r13, rax
	inc	r12
	call	exsfx_positio
	cmp	rax, r12
	jne	.positio_mala
	inc	r14
	cmp	r14, EXSFX_OCTETI_N
	jb	.proba

	; the error path: the same Scriptor, a descriptor the kernel refuses
	mov	rax, [rbp - 64]
	mov	[rbp - 80], rax
	mov	rax, [rbp - 56]
	mov	[rbp - 72], rax
	mov	dword [rbp - 80 + EXS_IFACE_SCRIPTOR_DESCRIPTOR], -1
	lea	rdi, [rbp - 80]
	mov	esi, 0x41
	call	bfausr_exsrt_scriptor_scribe_octetum
	test	rax, rax
	jnz	.error_non_nullus
	call	exsfx_positio
	cmp	rax, r12
	jne	.error_movit

	mov	rax, EXSFX_SENTINEL_B
	cmp	rbx, rax
	jne	.conservata_mala
	mov	rax, EXSFX_SENTINEL_F
	cmp	r15, rax
	jne	.conservata_mala

	mov	rax, r13			; 7: the counts, summed
	jmp	.exi

  .descriptor_malus:
	mov	eax, 21
	jmp	.exi
  .numerus_malus:
	lea	eax, [r14 + 30]
	jmp	.exi
  .positio_mala:
	lea	eax, [r14 + 40]
	jmp	.exi
  .non_quaeribilis:
	mov	eax, 50
	jmp	.exi
  .error_non_nullus:
	mov	eax, 51
	jmp	.exi
  .error_movit:
	mov	eax, 52
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

; exsfx_positio() -> rax = stdout's file offset, or -errno.
; `lseek(1, 0, SEEK_CUR)`: the kernel's count of bytes written through this
; open file description. THE FIXTURE'S instrument, not part of the blob.
exsfx_positio:
	mov	edi, 1
	xor	esi, esi
	mov	edx, 1				; SEEK_CUR
	mov	eax, 8				; lseek
	syscall
	ret

segment readable

; `b` as the caller's whole rsi. 0x00 and 0xff are the ends of the byte; 0x7f
; is the last ASCII byte, 0x80 a lone continuation byte, 0xd3 a lone lead
; byte. The last two carry garbage above bit 7: the byte is 0x41, then 0x00.
exsfx_octeti:
	dq	0x00
	dq	0x7f
	dq	0xff
	dq	0x80
	dq	0xd3
	dq	0xDEADBEEFCAFEBA41
	dq	0xFFFFFFFFFFFFFF00
EXSFX_OCTETI_N = ($ - exsfx_octeti) / 8
assert EXSFX_OCTETI_N = 7

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
