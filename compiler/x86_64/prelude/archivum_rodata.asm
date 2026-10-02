; compiler/x86_64/prelude/archivum_rodata.asm
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
; The four `struct open_how` constants archivum.asm's openat2 sites point
; at. A wrapper includes this into `segment readable` -- NOT the executable
; segment, where tools/syscall-audit.sh's linear sweep would decode them as
; instructions, and NOT `segment readable writeable`, where the audit
; refuses them (design section 3, rule A3: a constant the program can
; rewrite proves nothing). No `format`, `entry`, `segment` or `include`
; (runtime.md H1); gated with the code it serves.
;
; THE BYTES ARE A CONTRACT WITH THE AUDIT. tools/syscall-audit.sh's
; OPEN_HOW_ADMITTED table holds the same 24 bytes per constant and compares
; byte for byte (rule A4). tools/syscall-audit.sh --self-test assembles
; tests/unit/audit_openat2.asm, whose correct case points its four sites at
; THESE labels, so a change here without the table fails the self-test.
; Each value is x86-64 Linux's, checked against the UAPI headers and passed
; to the kernel by prototypes/beneath/beneath.c (design section 2.7):
;
;   name        flags                                     mode   resolve
;   how_radix   O_PATH|O_DIRECTORY|O_CLOEXEC   0x290000      0   NO_MAGICLINKS 0x02
;   how_infra   O_PATH|O_DIRECTORY|O_CLOEXEC   0x290000      0   B|M|X         0x0b
;   how_lege    O_RDONLY|O_NOCTTY|O_NONBLOCK|O_CLOEXEC 0x80900  0  B|M|X     0x0b
;   how_crea    O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY|O_CLOEXEC 0x801c1 0o600 B|M|X 0x0b
;
; B = RESOLVE_BENEATH 0x08, M = RESOLVE_NO_MAGICLINKS 0x02, X =
; RESOLVE_NO_XDEV 0x01. Not RESOLVE_NO_SYMLINKS, not RESOLVE_IN_ROOT
; (design D5). Each constant is three little-endian u64: flags, mode,
; resolve.
; -----------------------------------------------------------------------------

if ~ defined EXS_POTESTAS_ARCHIVUM
	err 'archivum_rodata.asm: EXS_POTESTAS_ARCHIVUM is undefined'
end if

if EXS_POTESTAS_ARCHIVUM

exsrt_how_radix:
	dq	0x290000, 0, 0x02
exsrt_how_infra:
	dq	0x290000, 0, 0x0b
exsrt_how_lege:
	dq	0x80900, 0, 0x0b
exsrt_how_crea:
	dq	0x801c1, 0x180, 0x0b		; mode 0o600

assert exsrt_how_infra - exsrt_how_radix = 24
assert exsrt_how_lege - exsrt_how_infra = 24
assert exsrt_how_crea - exsrt_how_lege = 24
assert $ - exsrt_how_crea = 24

end if	; EXS_POTESTAS_ARCHIVUM
