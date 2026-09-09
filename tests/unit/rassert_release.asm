; tests/unit/rassert_release.asm
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
; RELEASE counterpart to tests/unit/rassert_trap.asm -- byte-for-byte the
; same false condition (`rax eq 6` where rax is 5), but with `RELEASE`
; defined before compiler/x86_64/macros/assert.inc is included. `rassert`
; must expand to nothing at all here -- not a skipped check, an ELIDED one
; -- so the process reaches the end and exits 0, and the assembled binary
; is smaller than rassert_trap.asm's (no `cmp`/`jcc`/`ud2` bytes emitted).
; The second property isn't asserted by the harness (tests/run.sh only
; checks exit code and audit verdict) but was verified by hand while writing
; this fixture: 136 bytes with RELEASE defined vs 144 without, for the
; single-assertion case this pair was first tried with.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'
RELEASE = 1
include '../../compiler/x86_64/macros/assert.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	mov	rax, 5
	rassert	rax eq 5
	rassert	rax ne 6
	rassert	rax lt 6
	rassert	rax le 5
	rassert	rax gt 4
	rassert	rax ge 5
	rassert	rax eq 6		; FALSE, but RELEASE is defined: elided,
					; must NOT trap

	mov	eax, 231
	xor	edi, edi
	syscall
