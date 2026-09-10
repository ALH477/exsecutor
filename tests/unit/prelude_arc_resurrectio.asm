; tests/unit/prelude_arc_resurrectio.asm
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
; The other half of docs/design/runtime.md 2.5's abort pair: RESURRECTION.
;
; `exsrt_release` on an object whose count reaches 0 runs its destructor with
; the count left at 0 for the destructor's duration -- that is what makes spec
; 6.6's "re-referencing an object under destruction aborts" one compare rather
; than a second flag word. This destructor does exactly that: it retains the
; object it is destroying. `exsrt_retain` sees rc == 0 and aborts kind 3.
;
; Separate from tests/unit/prelude_arc.asm because a process can only die
; once, and kind 2 and kind 3 are different claims.
;
; Exit: 132 = SIGILL after `abortus 3` on fd 2. 38 = the retain inside the
; destructor RETURNED, which would mean rc == 0 is not detected.
;
; TEST: run=yes expect-exit=132 audit=pass

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
EXS_MXCSR		= 0x1F80

segment readable executable
include '../../compiler/x86_64/prelude/prelude.asm'
include '../../compiler/x86_64/prelude/interface.inc'

bfausr_initium:
	push	rbp
	mov	rbp, rsp
	lea	rdi, [exsfx_obj]
	call	exsrt_release		; 1 -> 0 -> destructor -> resurrection
	mov	eax, 38			; only if the abort did not happen
	pop	rbp
	ret

exsfx_dtor_resurgens:
	push	rbp
	mov	rbp, rsp
	call	exsrt_retain		; rdi still the object; rc is 0 -> abortus 3
	pop	rbp
	ret

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'

exsfx_obj:
	dq	1
	dq	exsfx_dtor_resurgens
