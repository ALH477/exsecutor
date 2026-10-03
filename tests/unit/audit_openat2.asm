; tests/unit/audit_openat2.asm
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
; The openat2 site rules of tools/syscall-audit.sh --potestates (W, A1-A6;
; ADR 0017 decision 5, docs/design/archivum-beneath.md section 3), one
; binary per case. This is prototypes/beneath/site.asm's three shapes grown
; into the full set the rules have to tell apart. NEVER RUN: the sites point
; at nothing a kernel should be asked to open. Only audited.
;
; ONE SOURCE, ONE SYMBOL. `CASUS` selects the case; tools/syscall-audit.sh
; --self-test assembles this file once per case with `fasmg -i 'CASUS := N'`
; and judges each binary under `--potestates Mundus,archivum`. Every case
; shares every byte of scaffolding with case 0 and differs from it in the ONE
; thing its rule is about. Case 0 passing is what makes each failure below
; non-vacuous: the same frame, segments and exit are admissible, so a case
; that fails, fails for its own difference. The self-test also requires the
; SET of rules each refusal names to be exactly the expected one, so a case
; cannot pass by failing for an unrelated reason.
;
;   case  differs from case 0 in                          audit must say
;   0     -- four sites, one per admitted constant, the
;            PRELUDE's own bytes (archivum_rodata.asm)     PASS
;   1     resolve = 0 (the anti-vacuity case: without it
;         A4 could be checking nothing)                    FAIL [A4]
;   2     resolve = B|M, X missing (a near miss)           FAIL [A4]
;   3     the right bytes, in `segment readable writeable` FAIL [A3]
;   4     rdx copied from rcx, which holds the lea         FAIL [W] [A1]
;   5     r10 = 32 (the kernel would accept a zero tail)   FAIL [A2]
;   6     how_radix with rdi loaded from memory            FAIL [A5]
;   7     how_lege with rdi = the immediate AT_FDCWD       FAIL [A6]
;   8     one unrelated instruction inside the window      FAIL [W]
;   9     a plain openat(257), the right everything else   FAIL (257 is
;                                                          admitted under
;                                                          no program atom)
;   10    an open_how cut short by the segment's end       FAIL [A3]
;
; Default build (no -i): case 0. tests/run.sh assembles that and audits it
; against the COMPILER's nine, where openat2(437) is not admitted at all:
; `audit=fail` is that, the compiler's closed set refusing a program's
; syscall, and it is unchanged by this ADR.
;
; TEST: run=no audit=fail

include 'format/format.inc'

format ELF64 executable 3
entry start

if ~ definite CASUS
	CASUS := 0
end if

; archivum_rodata.asm's gate: it is the prelude's blob and checks its atom.
EXS_POTESTAS_ARCHIVUM = 1

segment readable executable
  start:
	lea	rsi, [via]			; the path: not judged by any rule
if CASUS = 0
	mov	edi, -100			; AT_FDCWD, an immediate: A5
	lea	rdx, [exsrt_how_radix]
	mov	r10d, 24
	mov	eax, 437
	syscall
	mov	edi, [dirfd]			; from memory: A6
	lea	rdx, [exsrt_how_infra]
	mov	r10d, 24
	mov	eax, 437
	syscall
	mov	edi, [dirfd]
	lea	rdx, [exsrt_how_lege]
	mov	r10d, 24
	mov	eax, 437
	syscall
	mov	edi, [dirfd]
	lea	rdx, [exsrt_how_crea]
	mov	r10d, 24
	mov	eax, 437
	syscall
else if CASUS = 1
	mov	edi, [dirfd]
	lea	rdx, [how_resolve_nullum]
	mov	r10d, 24
	mov	eax, 437
	syscall
else if CASUS = 2
	mov	edi, [dirfd]
	lea	rdx, [how_sine_xdev]
	mov	r10d, 24
	mov	eax, 437
	syscall
else if CASUS = 3
	mov	edi, [dirfd]
	lea	rdx, [how_mutabilis]
	mov	r10d, 24
	mov	eax, 437
	syscall
else if CASUS = 4
	mov	edi, [dirfd]
	lea	rcx, [exsrt_how_lege]
	mov	rdx, rcx
	mov	r10d, 24
	mov	eax, 437
	syscall
else if CASUS = 5
	mov	edi, [dirfd]
	lea	rdx, [exsrt_how_lege]
	mov	r10d, 32
	mov	eax, 437
	syscall
else if CASUS = 6
	mov	edi, [dirfd]
	lea	rdx, [exsrt_how_radix]
	mov	r10d, 24
	mov	eax, 437
	syscall
else if CASUS = 7
	mov	edi, -100
	lea	rdx, [exsrt_how_lege]
	mov	r10d, 24
	mov	eax, 437
	syscall
else if CASUS = 8
	mov	edi, [dirfd]
	lea	rdx, [exsrt_how_lege]
	mov	r8, r9
	mov	r10d, 24
	mov	eax, 437
	syscall
else if CASUS = 9
	mov	edi, [dirfd]
	mov	edx, 0x80900			; openat's flags: O_RDONLY|O_NOCTTY|...
	xor	r10d, r10d
	mov	eax, 257
	syscall
else if CASUS = 10
	mov	edi, [dirfd]
	lea	rdx, [how_truncata]
	mov	r10d, 24
	mov	eax, 437
	syscall
else
	err 'audit_openat2.asm: no such CASUS'
end if
	mov	eax, 231			; exit_group: the core row
	xor	edi, edi
	syscall

segment readable
include '../../compiler/x86_64/prelude/archivum_rodata.asm'
how_resolve_nullum:
	dq	0x80900, 0, 0			; how_lege's flags, resolve = 0
how_sine_xdev:
	dq	0x80900, 0, 0x0a		; B|M: X missing
via:
	db	'intus.txt', 0
; LAST in this segment, on purpose: 16 of the 24 bytes exist in the file.
how_truncata:
	dq	0x80900, 0

segment readable writeable
dirfd:
	dd	3, 0
how_mutabilis:
	dq	0x80900, 0, 0x0b		; how_lege's bytes exactly, but writable
