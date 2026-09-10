; tests/unit/prelude_arena.asm
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
; The `alloc` atom: docs/design/runtime.md 2.6's mmap-backed bump arena, and
; the proof that its two syscalls appear in a binary ONLY when the program's
; capability closure admits them.
;
; EXS_POTESTAS_ALLOC is 1 here and EXS_POTESTAS_AMBITUS is 0, which is the
; opposite gate from tests/unit/prelude_scribe.asm. The audit on this binary
; therefore reports `mmap`, `munmap`, `write` (exsrt_abort's unreached path,
; the core row) and `exit_group` -- and NO second `write`, because `scribe`
; is not in it. Hazard H2 says a syscall-bearing routine added outside its
; `if` silently widens every program's surface; this fixture and prelude_arc
; are the pair that would catch it.
;
; What it checks, in order:
;   1. `novum` returns an ExsArena whose base == cur and whose limit is above
;      base by at least the requested capacity.
;   2. `da` bumps: two allocations are distinct, ordered, and the second is
;      at least the first's size past it.
;   3. `da` honours alignment: a 64-byte-aligned request comes back
;      64-byte-aligned.
;   4. `reconde` is spec 6.3 decision 1's O(1) reset -- cur returns to base
;      and the next allocation reuses the same address. Destructors are NOT
;      run by it; the count runs them (spec 6.6). Release-to-zero reclaims
;      nothing (runtime.md 2.6, H3): the bytes come back here and only here.
;   5. `dimitte` unmaps. Nothing is read afterwards.
;
; The memory is actually written to before reconde, so a mapping that is not
; really readable/writeable faults instead of passing.
;
; Exit: 0 = all five. 51-57 = the numbered check that failed.
;
; TEST: run=yes expect-exit=0 audit=pass

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

EXSFX_CAPACITAS = 0x4000		; 16 KiB of payload

segment readable executable
include '../../compiler/x86_64/prelude/prelude.asm'
include '../../compiler/x86_64/prelude/interface.inc'

bfausr_initium:
	push	rbp
	mov	rbp, rsp
	sub	rsp, 32			; [rbp-8] arena, [rbp-16] p1, [rbp-24] p2

	; 1. novum
	mov	rsi, EXSFX_CAPACITAS
	call	bfausr_exsrt_alloc_novum	; rdi is still the Mundus carrier
	mov	[rbp - 8], rax
	mov	rcx, [rax + EXS_IFACE_ARENA_BASE]
	cmp	rcx, [rax + EXS_IFACE_ARENA_CUR]
	jne	.e51
	mov	rdx, [rax + EXS_IFACE_ARENA_LIMIT]
	sub	rdx, rcx
	cmp	rdx, EXSFX_CAPACITAS
	jb	.e52

	; 2. da bumps
	mov	rdi, [rbp - 8]
	mov	rsi, 100
	mov	rdx, 8
	call	bfausr_exsrt_alloc_da
	mov	[rbp - 16], rax
	mov	rdi, [rbp - 8]
	mov	rsi, 100
	mov	rdx, 8
	call	bfausr_exsrt_alloc_da
	mov	[rbp - 24], rax
	mov	rcx, [rbp - 16]
	add	rcx, 100
	cmp	rax, rcx
	jb	.e53

	; the mapping is real: write to both ends of the second allocation
	mov	rdx, 0x5445535455
	mov	qword [rax], rdx
	mov	rcx, rax
	add	rcx, 92
	mov	qword [rcx], rdx

	; 3. alignment
	mov	rdi, [rbp - 8]
	mov	rsi, 8
	mov	rdx, 64
	call	bfausr_exsrt_alloc_da
	test	rax, 63
	jnz	.e54

	; 4. reconde resets to base and the next allocation reuses it
	mov	rdi, [rbp - 8]
	call	bfausr_exsrt_alloc_reconde
	mov	rcx, [rbp - 8]
	mov	rax, [rcx + EXS_IFACE_ARENA_BASE]
	cmp	rax, [rcx + EXS_IFACE_ARENA_CUR]
	jne	.e55
	mov	rdi, [rbp - 8]
	mov	rsi, 100
	mov	rdx, 8
	call	bfausr_exsrt_alloc_da
	cmp	rax, [rbp - 16]
	jne	.e56

	; 5. dimitte
	mov	rdi, [rbp - 8]
	call	bfausr_exsrt_alloc_dimitte

	xor	eax, eax
	mov	rsp, rbp
	pop	rbp
	ret
  .e51:
	mov	eax, 51
	jmp	.exi
  .e52:
	mov	eax, 52
	jmp	.exi
  .e53:
	mov	eax, 53
	jmp	.exi
  .e54:
	mov	eax, 54
	jmp	.exi
  .e55:
	mov	eax, 55
	jmp	.exi
  .e56:
	mov	eax, 56
  .exi:
	mov	rsp, rbp
	pop	rbp
	ret

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
