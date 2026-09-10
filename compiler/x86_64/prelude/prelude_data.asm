; compiler/x86_64/prelude/prelude_data.asm
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
;
; [OPEN] LICENSING: see prelude.asm's header. Same question, same answer:
; Form 1, because designation is not this agent's to make.
; -----------------------------------------------------------------------------
; The prelude's mutable state -- the SECOND blob (docs/design/runtime.md 2.1).
;
; WHY IT IS A SEPARATE FILE. A `postpone` block or a second `segment` inside
; prelude.asm would put a `segment` directive in the middle of the text `exsc`
; appends the module to, and backend_fasmg/emit.inc's header says its output
; expects to sit inside a segment the harness opened (runtime.md H1). So the
; blob is executable text only and its state is here, written by the wrapper
; after `segment readable writeable`.
;
; THE TWO FILES AGREE ON NAMES ONLY, NEVER ON OFFSETS. Every field offset is
; a named constant in prelude.asm; this file lays out storage of the right
; size and asserts the sizes match. Nothing here may be `db`'d into the
; executable segment: putting data between instructions is what desynchronises
; tools/syscall-audit.sh's linear sweep, and the abort message and its digit
; scratch are the only reason this file has any bytes at all beyond the two
; carrier records.
;
; WRITABLE, INCLUDING THE MESSAGE. `exsrt_abortus_linea` is constant in
; practice and sits in the writeable segment only because splitting it into a
; third blob would buy an MMU guarantee nothing depends on. Literals -- the
; program's `textus` bytes -- DO get MMU-enforced immutability: they are the
; emitter's `bfausr_g*` globals in `segment readable` (runtime.md 2.1).
;
; The wrapper must have defined the eleven EXS_POTESTAS_* constants before
; including this file, the same as for prelude.asm; the gate below is the same
; gate, so a routine and the record it reads are present or absent together.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md 4.6, 4.7, 6.6; docs/design/runtime.md
; 2.1, 2.2, 2.5.
; -----------------------------------------------------------------------------

; ---- ExsMundus { rsp0 ptr @0 } ----------------------------------------------
; The one carrier every program has. Written once by exsrt_start; read only by
; an `ambitus` derivation. Not gated: the entry stub is core.
exsrt_mundus:
	dq	0				; rsp0 -- where the initial stack IS
assert $ - exsrt_mundus = EXS_MUNDUS_SIZE

; ---- ExsAmbitus { in i32 @0, out i32 @4, err i32 @8, ------------------------
;                   argc u64 @16, argv ptr @24, envp ptr @32 }
if EXS_POTESTAS_AMBITUS
exsrt_ambitus:
	dd	0				; in         @0
	dd	0				; out        @4
	dd	0				; err        @8
	dd	0				; padding    @12
	dq	0				; argc       @16
	dq	0				; argv       @24
	dq	0				; envp       @32
assert $ - exsrt_ambitus = EXS_AMBITUS_SIZE
end if

; ---- the abort line ---------------------------------------------------------
; Laid out so exsrt_abort can issue ONE write: the fixed prefix, then the
; digits written in place, then a newline. `exsrt_abortus_numerus` must follow
; `exsrt_abortus_linea` with nothing between them.
exsrt_abortus_linea:
	db	'exsecutor: abortus '
exsrt_abortus_numerus:
	; up to 20 decimal digits (2^64-1 is 20) plus the newline exsrt_abort
	; stores after the last digit.
	repeat 21
	db	0
	end repeat
exsrt_abortus_calc:
	; scratch: the digits are generated backwards into the END of this
	; 20-byte area, then copied forward into exsrt_abortus_numerus.
	repeat 20
	db	0
	end repeat

; The pair prelude.asm cannot check for itself, checked here: if the English
; prefix is ever reworded, this fails at assembly rather than truncating or
; running past the end of the line.
assert exsrt_abortus_numerus - exsrt_abortus_linea = EXS_ABORTUS_PRAEFIXUM_LEN

; ---- end of the prelude data blob -------------------------------------------
