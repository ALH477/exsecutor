; tests/unit/prelude_scribe_octetos.asm
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
; `Scriptor.scribe_octetos` -- `n` raw bytes to a Scriptor's descriptor in as
; few write(2) as the kernel allows (spec §4.6's bulk pair) -- run from the
; prelude blob alone, exactly as prelude_scribe_octeto.asm runs its one-byte
; sibling, and measured the same way.
;
; WHY THE SIBLING IS NOT ENOUGH. That fixture proves one call moves the
; offset by one. The whole claim here is the opposite: that ONE call moves it
; by many, because docs/design/somnium.md §10 measured a program making 48,000
; write(2) a frame at 16.6-17.4 ms and the same program making one at 0.62-1.37.
; A routine that looped internally calling write(2) per byte would pass every
; check the sibling makes and defeat the entire point; check 2 below is what
; refuses it.
;
; WHAT IT PROVES:
;   1. A 7-byte call writes EXACTLY seven bytes and returns 7. The kernel
;      says so, not the routine: `lseek(1, 0, SEEK_CUR)` before and after.
;      The seven include 0x00, 0x80 and 0xff, none of them UTF-8 on its own
;      -- `scribe` takes a `textus` and spec §5.1 makes that UTF-8, so only
;      this routine and its one-byte sibling can write them.
;   2. A 4,096-BYTE CALL MOVES THE OFFSET BY 4,096 AND RETURNS 4,096. This
;      is the bulk claim. A regular file takes 4,096 bytes in one write(2),
;      so the loop in the routine runs its body once; that it CAN is what
;      distinguishes this row from 4,096 calls to `scribe_octeto`.
;   3. `n == 0` writes nothing, returns 0, and does not move the offset. The
;      routine returns before the syscall, which this cannot see directly --
;      but a `write` of zero bytes would not move the offset either, so what
;      is pinned here is the observable, and the early return is the blob's
;      own documented behaviour.
;   4. The error convention: on a descriptor the kernel refuses (-1, EBADF)
;      the call returns 0 and the offset does not move. Note this is the
;      count-so-far convention, not `scribe_octeto`'s flat 0 -- they agree
;      here only because nothing was written before the error.
;   5. The routine touches no callee-saved register: `rbx` and `r15` carry
;      sentinels across every call (the routine itself uses `rbx` for its
;      running total, so this is a real check, not a formality), and
;      `r12`-`r14` carry this fixture's state.
;   6. The binary's syscall surface passes the audit -- `write` and
;      `exit_group` from the prelude, and the one `lseek` that is THIS
;      FIXTURE's instrument. NO SYSCALL IS ADDED by the bulk row: it issues
;      the same `write(1)` the one-byte row does.
;
; WHAT IT CANNOT PROVE: the EINTR retry, the zero-return retry, and THE
; PARTIAL-WRITE LOOP -- all three need a second process or a full pipe, and
; the partial-write path is the one that matters most and is hardest to
; provoke. `[UNTESTED]`, as `scribe`'s equivalents are.
;
; Exit: 5 = every check above. 21 = Scriptor.descriptor was not 1.
; 30 = the 7-byte call returned the wrong count. 31 = it moved the offset
; wrongly. 32/33 = the same for the 4,096-byte call. 34/35 = the same for
; `n == 0`. 50 = stdout is not seekable (not run under tests/run.sh).
; 51 = the EBADF call returned non-zero. 52 = the EBADF call moved the
; offset. 53 = a callee-saved register was clobbered.
;
; TEST: run=yes expect-exit=5 audit=pass

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
EXS_MXCSR		= 0x1F80	; ad_parem + conservata (runtime.md 2.7)

EXSFX_SENTINEL_B	= 0x0123456789ABCDEF
EXSFX_SENTINEL_F	= 0xFEDCBA9876543210
EXSFX_MAGNUM_N		= 4096

segment readable executable

include '../../compiler/x86_64/prelude/prelude.asm'
; after the blob, so interface.inc's H4 cross-asserts fire (prelude/README.md)
include '../../compiler/x86_64/prelude/interface.inc'

; ---- the module -------------------------------------------------------------
; Frame: five callee-saved registers, [rbp-64] the Scriptor over stdout,
; [rbp-80] a copy with descriptor -1.
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
	xor	r13d, r13d			; r13 = checks passed

	; ---- 1. seven bytes, one call ---------------------------------------
	lea	rdi, [rbp - 64]
	lea	rsi, [exsfx_septem]
	mov	rdx, EXSFX_SEPTEM_N
	call	bfausr_exsrt_scriptor_scribe_octetos
	cmp	rax, EXSFX_SEPTEM_N
	jne	.septem_numerus
	add	r12, EXSFX_SEPTEM_N
	call	exsfx_positio
	cmp	rax, r12
	jne	.septem_positio
	inc	r13

	; ---- 2. four thousand and ninety-six, ALSO one call ------------------
	lea	rdi, [rbp - 64]
	lea	rsi, [exsfx_magnum]
	mov	rdx, EXSFX_MAGNUM_N
	call	bfausr_exsrt_scriptor_scribe_octetos
	cmp	rax, EXSFX_MAGNUM_N
	jne	.magnum_numerus
	add	r12, EXSFX_MAGNUM_N
	call	exsfx_positio
	cmp	rax, r12
	jne	.magnum_positio
	inc	r13

	; ---- 3. n == 0 writes nothing ----------------------------------------
	lea	rdi, [rbp - 64]
	lea	rsi, [exsfx_septem]
	xor	rdx, rdx
	call	bfausr_exsrt_scriptor_scribe_octetos
	test	rax, rax
	jnz	.nihil_numerus
	call	exsfx_positio
	cmp	rax, r12
	jne	.nihil_positio
	inc	r13

	; ---- 4. a descriptor the kernel refuses -------------------------------
	mov	rax, [rbp - 64]
	mov	[rbp - 80], rax
	mov	rax, [rbp - 56]
	mov	[rbp - 72], rax
	mov	dword [rbp - 80 + EXS_IFACE_SCRIPTOR_DESCRIPTOR], -1
	lea	rdi, [rbp - 80]
	lea	rsi, [exsfx_septem]
	mov	rdx, EXSFX_SEPTEM_N
	call	bfausr_exsrt_scriptor_scribe_octetos
	test	rax, rax
	jnz	.error_non_nullus
	call	exsfx_positio
	cmp	rax, r12
	jne	.error_movit
	inc	r13

	; ---- 5. the callee-saved registers ------------------------------------
	mov	rax, EXSFX_SENTINEL_B
	cmp	rbx, rax
	jne	.conservata_mala
	mov	rax, EXSFX_SENTINEL_F
	cmp	r15, rax
	jne	.conservata_mala
	inc	r13

	mov	rax, r13			; 5
	jmp	.exi

  .descriptor_malus:
	mov	eax, 21
	jmp	.exi
  .septem_numerus:
	mov	eax, 30
	jmp	.exi
  .septem_positio:
	mov	eax, 31
	jmp	.exi
  .magnum_numerus:
	mov	eax, 32
	jmp	.exi
  .magnum_positio:
	mov	eax, 33
	jmp	.exi
  .nihil_numerus:
	mov	eax, 34
	jmp	.exi
  .nihil_positio:
	mov	eax, 35
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

; 0x00 and 0xff are the ends of the byte; 0x7f is the last ASCII byte, 0x80 a
; lone continuation byte, 0xd3 a lone lead byte. Seven is not a power of two
; and not a multiple of anything the routine uses, so a loop that rounded up
; to a word would show here.
exsfx_septem:
	db	0x00, 0x7f, 0xff, 0x80, 0xd3, 0x41, 0x00
EXSFX_SEPTEM_N = $ - exsfx_septem
assert EXSFX_SEPTEM_N = 7

; 4,096 bytes: one page, and more than any per-byte loop would get away with
; unnoticed in the offset check.
exsfx_magnum:
	repeat EXSFX_MAGNUM_N
		db	0x2E
	end repeat
assert $ - exsfx_magnum = EXSFX_MAGNUM_N

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
