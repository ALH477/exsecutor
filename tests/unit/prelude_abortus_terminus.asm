; tests/unit/prelude_abortus_terminus.asm
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
; Abort kind 5, `terminus` overrun, on its own.
;
; Spec 8.5: a `dum COND terminus N` evaluates the bound once, before the first
; iteration, and "the entry that would be the N+1th does not happen -- it is a
; runtime abort (6.6's shape)". docs/design/lowering.md's `dum` lowering emits
; a `trap terminus` for it, and that document's finding 2 records that
; runtime.md 2.5's table had kinds 1-4 and no row for this one, so the trap
; was folding into kind 1 -- a numeric trap, which it is not. Kind 5 is that
; row.
;
; NO LOOP IS LOWERED HERE, because no lowering exists: this fixture calls the
; blob's `exsrt_abort_terminus` entry point directly, which is exactly what
; the emitted `trapb:` block will do. What it proves is the runtime half --
; that kind 5 reaches fd 2 as `abortus 5` and dies by SIGILL like the other
; four, and that it is DISTINCT from kind 1. The loop half belongs to the
; lowering agent's fixtures.
;
; Exit: 132 = SIGILL after `abortus 5` on fd 2. 39 = exsrt_abort returned,
; which it must never do.
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

; The kind the blob and the interface both name, read from the interface so
; that a renumbering has to disagree with two files to get through.
assert EXS_IFACE_ABORTUS_TERMINUS = 5

bfausr_initium:
	push	rbp
	mov	rbp, rsp
	jmp	exsrt_abort_terminus	; what the emitted `trapb:` block does
	mov	eax, 39
	pop	rbp
	ret

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
