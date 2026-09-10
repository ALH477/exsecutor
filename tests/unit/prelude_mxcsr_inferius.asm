; tests/unit/prelude_mxcsr_inferius.asm
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
; Spec 5.4's numeric state, SET at process entry rather than assumed.
;
; docs/design/runtime.md 2.7: Linux happens to initialise MXCSR to 0x1F80 on
; execve, and relying on that is the ambient-state class spec 5.4 exists to
; remove. `exsrt_start` therefore executes `ldmxcsr` from the EXS_MXCSR image
; the driver prints from `initium`'s Func.numeri, before any float
; instruction can run. This fixture reads MXCSR back with `stmxcsr` at the
; first instruction of `bfausr_initium` -- one call after the stub -- and
; checks it field by field, not just as a number, so that a wrong bit says
; WHICH bit.
;
; THIS FIXTURE: EXS_MXCSR = 0x3F80 -- `rotundatio ad_inferius`.
;
; runtime.md 2.7's table, checked here at assembly time and again at runtime:
;   bit  6   DAZ  0    `subnormales conservata`
;   bits 7-12     1    all six exception masks: floats do not trap in spec
;                      5.4; only integer operators do
;   bits 13-14 RC 01   `rotundatio ad_inferius` (toward -infinity)
;   bit  15  FTZ  0    `subnormales conservata`
;
; THREE IMAGES, THREE FIXTURES. `EXS_MXCSR` is one constant and the stub runs
; once, so one binary can only prove one image. tests/unit/prelude_mxcsr.asm,
; prelude_mxcsr_inferius.asm and prelude_mxcsr_superius.asm are the same
; fixture over runtime.md 2.7's three rounding images; each goes through the
; REAL entry stub rather than re-executing its two instructions, which is the
; whole point of checking it at all. Toward-zero (RC = 11) has no spec 5.4
; name (runtime.md finding 11) and therefore no fixture.
;
; Exit: 0 = the image is exactly EXS_MXCSR. 41 = the whole word differs.
; 42 = DAZ set. 43 = FTZ set. 44 = the rounding field is wrong. 45 = an
; exception mask is clear.
;
; TEST: run=yes expect-exit=0 audit=pass

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
EXS_POTESTAS_AMBITUS	= 0
EXS_POTESTAS_FILUM	= 0
EXS_POTESTAS_MACHINA	= 0
EXS_POTESTAS_CRUDUM	= 0
EXS_MXCSR		= 0x3F80

; The image is a spec 5.4 statement, so state it as one at assembly time too.
assert (EXS_MXCSR and 0x0040) = 0		; DAZ clear -- subnormales conservata
assert (EXS_MXCSR and 0x8000) = 0		; FTZ clear -- subnormales conservata
assert (EXS_MXCSR and 0x1F80) = 0x1F80		; IM DM ZM OM UM PM all set
assert ((EXS_MXCSR shr 13) and 3) = 1	; rotundatio ad_inferius -- RC = 01, toward -inf
assert (EXS_MXCSR and 0x003F) = 0		; no exception flag preset

EXSFX_RC = 1

segment readable executable
include '../../compiler/x86_64/prelude/prelude.asm'
include '../../compiler/x86_64/prelude/interface.inc'

bfausr_initium:
	push	rbp
	mov	rbp, rsp
	sub	rsp, 16
	stmxcsr	dword [rbp - 8]
	mov	eax, [rbp - 8]

	mov	ecx, eax
	and	ecx, 0x0040
	jnz	.e42			; DAZ
	mov	ecx, eax
	and	ecx, 0x8000
	jnz	.e43			; FTZ
	mov	ecx, eax
	shr	ecx, 13
	and	ecx, 3
	cmp	ecx, EXSFX_RC
	jne	.e44			; rounding control
	mov	ecx, eax
	and	ecx, 0x1F80
	cmp	ecx, 0x1F80
	jne	.e45			; exception masks
	cmp	eax, EXS_MXCSR
	jne	.e41			; and the word as a whole

	xor	eax, eax
	mov	rsp, rbp
	pop	rbp
	ret
  .e41:
	mov	eax, 41
	mov	rsp, rbp
	pop	rbp
	ret
  .e42:
	mov	eax, 42
	mov	rsp, rbp
	pop	rbp
	ret
  .e43:
	mov	eax, 43
	mov	rsp, rbp
	pop	rbp
	ret
  .e44:
	mov	eax, 44
	mov	rsp, rbp
	pop	rbp
	ret
  .e45:
	mov	eax, 45
	mov	rsp, rbp
	pop	rbp
	ret

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
