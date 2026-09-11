; tests/unit/prelude_sine_ambitus.asm
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
; The `ambitus` gate, checked rather than measured once.
;
; prelude.asm puts every `ambitus` routine inside `if EXS_POTESTAS_AMBITUS`,
; so that a program whose capability closure lacks `ambitus` carries no
; stream writer in its BINARY (runtime.md 2.1, 2.6). prelude/README.md
; recorded that as measured -- `scribe`'s `write` disappears when the atom is
; 0 -- but no fixture held it: every AMBITUS=0 fixture passes its audit
; whether the gate is there or not, because the unit phase audits against the
; compiler's nine, which include `write`. A routine moved outside the gate by
; accident would have shipped in every Mundus-only program unnoticed.
;
; THIS FIXTURE: the closure is {Mundus} alone, and after the blob is
; assembled it ASSERTS that none of the gated labels exists -- the four
; IR-callable `ambitus` routines, `Scriptor.scribe_octetum` among them
; (wire-codec.md D7 puts it inside this gate), and the ExsAmbitus record in
; the data blob. Moving any of them out of the gate is an assembly failure
; here, naming the label. The run itself is the entry stub and a `redde 0`.
;
; `defined` in fasmg sees a label defined anywhere in the source, before or
; after the test, so the checks come last only for the reader's sake.
;
; Exit: 0. Everything this fixture proves it proves at assembly time.
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
EXS_MXCSR		= 0x1F80

segment readable executable
include '../../compiler/x86_64/prelude/prelude.asm'
include '../../compiler/x86_64/prelude/interface.inc'

bfausr_initium:
	xor	eax, eax
	ret

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'

; ---- the gate ---------------------------------------------------------------
if defined bfausr_exsrt_mundus_ambitus
	err 'prelude: bfausr_exsrt_mundus_ambitus is assembled with EXS_POTESTAS_AMBITUS = 0 -- it is outside the ambitus gate'
end if
if defined bfausr_exsrt_scriptor_ad_exitum
	err 'prelude: bfausr_exsrt_scriptor_ad_exitum is assembled with EXS_POTESTAS_AMBITUS = 0 -- it is outside the ambitus gate'
end if
if defined bfausr_exsrt_scriptor_scribe
	err 'prelude: bfausr_exsrt_scriptor_scribe is assembled with EXS_POTESTAS_AMBITUS = 0 -- it is outside the ambitus gate'
end if
if defined bfausr_exsrt_scriptor_scribe_octetum
	err 'prelude: bfausr_exsrt_scriptor_scribe_octetum is assembled with EXS_POTESTAS_AMBITUS = 0 -- it is outside the ambitus gate'
end if
if defined exsrt_ambitus
	err 'prelude_data: exsrt_ambitus is assembled with EXS_POTESTAS_AMBITUS = 0 -- it is outside the ambitus gate'
end if

; And the converse, so the checks above cannot pass by testing nothing: the
; core entry stub and the always-assembled abort ARE defined.
if ~ defined exsrt_start | ~ defined exsrt_abort
	err 'prelude: the core routines are missing -- the gate checks above would pass vacuously'
end if
