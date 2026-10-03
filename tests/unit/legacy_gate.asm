; tests/unit/legacy_gate.asm
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 DeMoD LLC.
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
; The i386 legacy syscall gates, which tools/syscall-audit.sh refuses
; outright in both modes. Before this fixture the audit swept only `syscall`
; (0F 05), so a binary entering the kernel by `int 0x80` (CD 80) or
; `sysenter` (0F 34) PASSED every allowlist and every atom row -- and the
; i386 table numbers syscalls differently (102 is socketcall, 11 execve), so
; no x86-64 row could have judged one anyway. NEVER RUN: only audited.
;
; ONE SOURCE, ONE SYMBOL, as tests/unit/audit_openat2.asm: --self-test
; assembles it with `fasmg -i 'CASUS := N'`.
;
;   case  body                                              audit must say
;   0     `mov eax,1` / `int 0x80` (i386 exit)              FAIL, both modes
;   1     `sysenter`                                        FAIL, both modes
;   2     the bytes CD 80 and 0F 34 inside two immediates,
;         no gate at all                                    PASS, both modes
;
; Case 2 is the false-positive guard: a byte grep would refuse it, and the
; audit DECODES (the same linear sweep that resolves `syscall` sites), so it
; must not. Cases 0 and 1 are non-vacuous against case 2: the same frame
; and exit, one instruction different.
;
; Default build (no -i): case 0, which tests/run.sh audits against the
; compiler's nine: `audit=fail`.
;
; TEST: run=no audit=fail

include 'format/format.inc'

format ELF64 executable 3
entry start

if ~ definite CASUS
	CASUS := 0
end if

segment readable executable
  start:
if CASUS = 0
	mov	eax, 1
	xor	ebx, ebx
	int	0x80
else if CASUS = 1
	sysenter
else if CASUS = 2
	mov	eax, 0x80CD			; b8 cd 80 00 00
	mov	ecx, 0x340F			; b9 0f 34 00 00
else
	err 'legacy_gate.asm: no such CASUS'
end if
	mov	eax, 231
	xor	edi, edi
	syscall
