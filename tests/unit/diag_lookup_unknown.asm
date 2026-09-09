; tests/unit/diag_lookup_unknown.asm
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
; NEGATIVE fixture for compiler/x86_64/diag/util.inc.
;
; `diag_lookup` on a numeric code that matches no §13 entry must TRAP, in
; every build, not fall through. CLAUDE.md: "Never invent a code" -- so a
; miss can only mean this compiler's own code passed a code that was never
; registered, and what `diag_lookup` returns next feeds directly into a
; diagnostic's rendered CODE and MESSAGE. Falling through with an undefined
; index would put content this process did not choose into output whose whole
; job (§8.3) is to be trustworthy and machine-matchable.
;
; util.inc's header records this as a deliberate departure from the usual
; `rassert` convention -- it is a bare `ud2`, live under RELEASE too, not a
; macro that compiles out. This fixture is the evidence that it actually
; traps rather than merely being written down as trapping; the assertion
; costs one two-byte instruction on a path that, if the rest of the compiler
; is correct, never runs.
;
; 9999 is not a §13 code and cannot become one: §13's codes are four decimal
; digits in ranges §13 enumerates, and 9999 is outside all of them.
;
; 128 + SIGILL(4) = 132, the same convention tests/unit/rassert_trap.asm and
; tests/unit/span_union_file_mismatch.asm use.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/diag/util.inc'

segment readable executable
  start:
	mov	edi, 9999
	call	diag_lookup

	; unreachable if diag_lookup's miss path traps
	mov	eax, 231
	mov	edi, 77
	syscall
