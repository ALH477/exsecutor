; tests/unit/prelude_arena_exhausta.asm
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
; The fourth abort kind: an exhausted arena.
;
; docs/design/runtime.md 2.6 gives `exsrt_alloc_da` exactly one failure mode
; and one response -- `exsrt_abort` kind 4 -- because a bump past `limit` is
; not a value the program asked for a way to handle: spec 6.3's arenas are
; preallocated and sized in `initium` (docs/design/profile-certus.md), so
; running out is a sizing bug, not a runtime condition. Growth is
; [UNIMPLEMENTED] for the reasons rt/arena.inc gives.
;
; Separate from tests/unit/prelude_arena.asm, which must survive to check the
; other four operations: a process dies once.
;
; Exit: 132 = SIGILL after `abortus 4` on fd 2. 58 = the over-large `da`
; RETURNED, which would mean the limit check does not fire and a program
; would be writing past its own mapping.
;
; TEST: run=yes expect-exit=132 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry exsrt_start

EXS_POTESTAS_MUNDUS	= 1
EXS_POTESTAS_ALLOC	= 1
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
	sub	rsp, 16
	mov	rsi, 0x1000
	call	bfausr_exsrt_alloc_novum
	mov	[rbp - 8], rax
	mov	rdi, rax
	mov	rsi, 0x100000		; a megabyte out of a four-kilobyte arena
	mov	rdx, 8
	call	bfausr_exsrt_alloc_da
	mov	eax, 58			; only if the abort did not happen
	mov	rsp, rbp
	pop	rbp
	ret

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
