; tests/unit/prelude_lege_octetos.asm
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
; `Lector.lege_octetos` -- `n` raw bytes from a Lector's descriptor, READ
; FULLY (spec §4.6's bulk pair) -- run from the prelude blob alone, as
; prelude_lege_octeto.asm runs its one-byte sibling.
;
; THE BYTES COME FROM THE HARNESS, as they do for the sibling: `; TEST:`'s
; `stdin=` points at tests/data/lector_quattuor.bin, four bytes
; `0x00 0x7f 0x80 0xff` -- the ends of the byte, the last ASCII one, and a
; lone continuation byte. Four is deliberately small: this fixture's whole
; subject is what happens when a caller asks for MORE than there is.
;
; WHAT IT PROVES, and check 2 is the one that matters:
;   1. `n == 2` returns 2 and fills exactly two bytes: `0x00 0x7f`. The
;      third byte of the destination is untouched, which is checked -- a
;      routine that read four and reported two would corrupt a caller's
;      buffer past the length it asked for.
;   2. `n == 10` OVER A 2-BYTE REMAINDER RETURNS 2, not 10 and not 256.
;      This is the short-read contract, and it is the entire difference
;      between this row and `lege_octeto`: that one has no count, so it
;      spends the value 256 to say "no byte", and 256 is a sentinel a `u16`
;      can afford and a `mensura` does not need. Here a short count IS the
;      signal. The two bytes are `0x80 0xff`, the rest of the file, so the
;      short read is the file ending and not a truncation.
;   3. At end of input, `n == 4` returns 0. Not 256 -- there is no sentinel
;      in this row and a fixture is the right place to say so, because 256
;      is exactly what someone porting from the sibling would write.
;   4. `n == 0` returns 0 and reads nothing.
;   5. On a descriptor the kernel refuses (-1, EBADF) it returns 0. EOF and
;      error are NOT distinguished -- both are a short count -- which is the
;      sibling's own [OPEN] mirrored rather than improved on.
;   6. No callee-saved register is touched. The routine uses `rbx` for its
;      running total, so the sentinel in it is a real check.
;   7. The syscall surface passes the audit: `read` and `exit_group`, the
;      prelude's own. NO SYSCALL IS ADDED by the bulk row.
;
; WHAT IT CANNOT PROVE: the EINTR retry, and the READ-FULLY LOOP ITSELF --
; proving that a fragmented input is reassembled needs a pipe written in
; pieces by a second process, and a regular file hands over everything at
; once. That loop is the reason this row exists rather than returning one
; read(2)'s worth, and it is `[UNTESTED]` here. Check 2 pins the short count
; at end of input, which is the other half.
;
; Exit: 0 = every check. 21 = Lector.descriptor was not 0. 30 = the 2-byte
; call returned the wrong count. 31 = it delivered the wrong bytes. 32 = it
; wrote past the length asked for. 40 = the over-ask returned the wrong
; count. 41 = it delivered the wrong bytes. 50 = the call at end of input
; did not return 0. 51 = `n == 0` did not return 0. 52 = the EBADF call did
; not return 0. 53 = a callee-saved register was clobbered.
;
; TEST: run=yes expect-exit=0 audit=pass stdin=tests/data/lector_quattuor.bin

include 'format/format.inc'

format ELF64 executable 3
entry exsrt_start

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
EXSFX_INTACTUM		= 0x5A		; the byte the destination is painted
					; with, so "untouched" is checkable

segment readable executable

include '../../compiler/x86_64/prelude/prelude.asm'
; after the blob, so interface.inc's H4 cross-asserts fire (prelude/README.md)
include '../../compiler/x86_64/prelude/interface.inc'

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

	; paint the destination, so an over-long write shows
	xor	rcx, rcx
  .pinge:
	mov	byte [exsfx_destinatio + rcx], EXSFX_INTACTUM
	inc	rcx
	cmp	rcx, EXSFX_DESTINATIO_N
	jb	.pinge

	call	bfausr_exsrt_mundus_ambitus	; rdi is already the Mundus carrier
	lea	rdi, [rbp - 64]			; hidden return ptr first (IR 2.9)
	mov	rsi, rax			; a
	call	bfausr_exsrt_lector_ab_introitu
	; read through the INTERFACE's offset (runtime.md H4)
	mov	eax, [rbp - 64 + EXS_IFACE_LECTOR_DESCRIPTOR]
	test	eax, eax
	jnz	.descriptor_malus

	; ---- 1. n == 2: two bytes, and only two ------------------------------
	lea	rdi, [rbp - 64]
	lea	rsi, [exsfx_destinatio]
	mov	rdx, 2
	call	bfausr_exsrt_lector_lege_octetos
	cmp	rax, 2
	jne	.duo_numerus
	cmp	byte [exsfx_destinatio], 0x00
	jne	.duo_octeti
	cmp	byte [exsfx_destinatio + 1], 0x7f
	jne	.duo_octeti
	cmp	byte [exsfx_destinatio + 2], EXSFX_INTACTUM
	jne	.duo_ultra			; it wrote past what was asked

	; ---- 2. n == 10 over a 2-byte remainder: a SHORT COUNT ---------------
	lea	rdi, [rbp - 64]
	lea	rsi, [exsfx_destinatio + 4]
	mov	rdx, 10
	call	bfausr_exsrt_lector_lege_octetos
	cmp	rax, 2				; not 10, and not 256
	jne	.nimis_numerus
	cmp	byte [exsfx_destinatio + 4], 0x80
	jne	.nimis_octeti
	cmp	byte [exsfx_destinatio + 5], 0xff
	jne	.nimis_octeti

	; ---- 3. the input is spent: 0, and NOT 256 ---------------------------
	lea	rdi, [rbp - 64]
	lea	rsi, [exsfx_destinatio + 16]
	mov	rdx, 4
	call	bfausr_exsrt_lector_lege_octetos
	test	rax, rax
	jnz	.finis_malus

	; ---- 4. n == 0 -------------------------------------------------------
	lea	rdi, [rbp - 64]
	lea	rsi, [exsfx_destinatio + 16]
	xor	rdx, rdx
	call	bfausr_exsrt_lector_lege_octetos
	test	rax, rax
	jnz	.nihil_malus

	; ---- 5. a descriptor the kernel refuses ------------------------------
	mov	rax, [rbp - 64]
	mov	[rbp - 80], rax
	mov	rax, [rbp - 56]
	mov	[rbp - 72], rax
	mov	dword [rbp - 80 + EXS_IFACE_LECTOR_DESCRIPTOR], -1
	lea	rdi, [rbp - 80]
	lea	rsi, [exsfx_destinatio + 16]
	mov	rdx, 4
	call	bfausr_exsrt_lector_lege_octetos
	test	rax, rax
	jnz	.error_malus

	; ---- 6. the callee-saved registers -----------------------------------
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
  .duo_numerus:
	mov	eax, 30
	jmp	.exi
  .duo_octeti:
	mov	eax, 31
	jmp	.exi
  .duo_ultra:
	mov	eax, 32
	jmp	.exi
  .nimis_numerus:
	mov	eax, 40
	jmp	.exi
  .nimis_octeti:
	mov	eax, 41
	jmp	.exi
  .finis_malus:
	mov	eax, 50
	jmp	.exi
  .nihil_malus:
	mov	eax, 51
	jmp	.exi
  .error_malus:
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

segment readable writeable

EXSFX_DESTINATIO_N = 32
exsfx_destinatio:
	rb	EXSFX_DESTINATIO_N

include '../../compiler/x86_64/prelude/prelude_data.asm'
