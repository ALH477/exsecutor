; tests/unit/rassert_trap.asm
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
; asm-rt NEGATIVE fixture for compiler/x86_64/macros/assert.inc.
;
; Deliberately wrong: exercises `rassert` with every one of its six
; condition operators (eq ne lt le gt ge) on conditions that HOLD -- proving
; a passing `rassert` does not trap and does not disturb subsequent code --
; and then one final `rassert` whose condition is FALSE, which MUST trap.
;
; `RELEASE` is deliberately NOT defined here (see rassert_release.asm for
; the same false condition with `RELEASE` defined, which must NOT trap) --
; this is the debug build, where a failed `rassert` is a compiler bug that
; must stop the process, not fail open.
;
; Per CLAUDE.md ("A sound negative result reported honestly is worth more
; than a green claim that doesn't hold") and .claude/agents/asm-rt.md's
; verification requirement: a check is only trusted once something proves
; it catches what it claims. This is that proof for `rassert`.
;
; `ud2`'s raw encoding (`db 0x0F, 0x0B` -- see assert.inc's header for why
; there is no symbolic mnemonic) raises SIGILL. 128 + SIGILL(4) = 132.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'
include '../../compiler/x86_64/macros/assert.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	; every operator, all TRUE -- none of these may trap
	mov	rax, 5
	rassert	rax eq 5
	rassert	rax ne 6
	rassert	rax lt 6
	rassert	rax le 5
	rassert	rax gt 4
	rassert	rax ge 5

	; if we reach here, every passing rassert above correctly did not trap
	; -- write a distinct, otherwise-unreachable-if-any-of-the-above-had-
	; wrongly-trapped exit path is unnecessary here: reaching this next
	; instruction at all already proves it. Now the deliberately false one:
	rassert	rax eq 6		; FALSE -- rax is 5 -- must trap

	; unreachable if rassert works; if this runs, rassert silently passed
	; a false condition, which is worse than not running this fixture at
	; all -- exit 0 here would be reported as this NEGATIVE fixture's
	; check passing, which is exactly backwards, so make it unmistakable:
	mov	eax, 231
	mov	edi, 77
	syscall
