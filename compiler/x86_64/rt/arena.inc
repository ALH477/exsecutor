; compiler/x86_64/rt/arena.inc
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
; Bump-pointer arena, backed by one `mmap` region. This compiler is written
; in the language it hosts: spec §6.3 ranks "arena allocation via `alloc`"
; as the single largest measured memory-model effect (2.8x over malloc,
; larger than every refcounting effect combined -- `[UNREPRODUCED]` in the
; spec itself, sources absent from the tree; this file does not re-assert
; that figure as its own result), and this file is
; where that decision lands for `exsc`'s own internals: every other rt/
; module (vec, map, intern, str) allocates through an `Arena`, never
; through a libc `malloc` this freestanding binary does not have.
;
; PREALLOCATE AND RESET is the intended usage, not incidental: `arena_init`
; once, up front, sized for the working set a phase actually needs;
; `arena_reset` between phases (O(1) -- it rewinds one pointer, nothing
; more); `arena_alloc` in the hot path, never `arena_init`. Spec §6.3's own
; words: "creating an arena inside a hot path is itself an allocation."
;
; FIXED CAPACITY, NOT auto-growing -- an accepted v1 scope limit, not an
; oversight. `arena_alloc` past the capacity given to `arena_init` traps (an
; internal contract violation: the caller under-sized the arena) rather than
; transparently mapping more. A bump allocator that DID
; grow transparently would need either to move existing allocations
; (invalidating every live pointer into the arena -- unacceptable for an
; allocator every other rt/ module hands pointers out of) or to track a
; chunk list (so `arena_reset` -- and any eventual `arena_destroy` -- has
; more than one region to account for). Both are real designs; neither is
; this wave's to build without a concrete caller that needs it.
; `[UNIMPLEMENTED]`: auto-growth / chunked arenas.
;
; EXHAUSTION NOW SAYS SO BEFORE IT TRAPS, and this is a fix, not a
; decoration. The trap used to be a bare `rassert rcx le rdx`, so a
; compilation that outgrew its arena died with `SIGILL`, exit 132 and NOT
; ONE BYTE on stderr -- the worst failure mode a compiler has, because
; nothing tells the person whether they hit a compiler limit or wrote a bad
; program. Measured (docs/design/receptor.md finding 20): a six-line source
; with one `acies<i64, 18000>` field did exactly that. `arena_alloc` now
; writes what rt/ itself knows -- the bytes wanted, the bytes used, the
; capacity -- and then calls the owner's `Arena.oom` hook, if it registered
; one, so the owner can add what only IT knows (which arena this is, and
; what its size was computed from). Then it traps, as before: the exit
; status is unchanged at 132, and 132 still means "compiler bug"
; (driver/msg.inc's status table, docs/asm-conventions.md "the four failure
; channels"). Nothing here is an `EXS-E` diagnostic and nothing here
; invented a code: spec section 13 registers codes for the PROGRAM BEING
; COMPILED, and has none for `exsc` running out of its own memory (checked
; against the whole registry; the nearest thing is section 9.3's rule that a
; usage error carries no code either).
;
; TWO CONSEQUENCES OF DROPPING THE `rassert`, both deliberate. The check is
; now UNCONDITIONAL: `rassert` erases both the test and its condition under
; `RELEASE` (macros/assert.inc), which would have left a release build
; bumping `Arena.cur` past `Arena.limit` and handing out addresses outside
; the mapping -- silent corruption where the debug build trapped. This one
; check is not a debug-only contract, so it is no longer written as one --
; the same reasoning, and the same conclusion, as diag/util.inc's own
; unconditional `db 0x0F, 0x0B`. And the comparison is now UNSIGNED, with
; the carry out of `cur + size` treated as exhaustion too, so a nonsensical
; `size` wraps into a trap rather than past the limit.
;
; NEVER `arena_reset` the arena backing a live interner (macros/proc.inc's
; sibling file `intern.inc` depends on this directly: an interned ID is
; permanently valid for the whole compilation context, and `arena_reset`
; invalidates every pointer into the arena it is called on). Resettable
; scratch work uses a *different* `Arena` instance.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §6.3 (the six memory decisions,
; #1: arena allocation).
; -----------------------------------------------------------------------------

include '../macros/struct.inc'
include '../macros/assert.inc'
include 'sys.inc'

struct Arena
	field base, dq		; start of the mmap'd region
	field cur,  dq		; bump pointer -- next allocation starts here
	field limit, dq		; one past the last usable byte (base + capacity)
	field oom,  dq		; optional: `void oom(Arena*, size_t want)`, called
				; once on exhaustion, after rt/ has printed the
				; three numbers it knows and before the trap. 0
				; means "no hook", which is what BSS and
				; `arena_init` both leave here -- so an owner
				; registers it AFTER `arena_init`, never before.
				; It must return; the trap is not its to take.
end struct

; __arena_saylit TEXT -- TEXT on fd 2. The literal rides in the code
; segment behind a `jmp`, the way every other message in this tree does
; (backend_fasmg/emit.inc's `__bfa_emtxt`, driver/msg.inc's literals):
; rt/ has no writable data of its own and needs none for this.
macro __arena_saylit? text&
	local s, e
	jmp	e
    s	db	text
    e:
	mov	edi, 2
	lea	rsi, [s]
	mov	rdx, e - s
	call	sys_write
end macro

; __arena_say_u64(value) -- `value` on fd 2 as unsigned decimal, no
; newline, no prefix. Written here rather than reached for from diag/
; because rt/ is the bottom of this tree and does not depend upward:
; diag/util.inc's `diag_fmt_u32` is the same twenty lines one layer up, and
; a u32 is too narrow for an arena's byte counts anyway.
proc __arena_say_u64, value
	uses	rbx, r12, r13
	locals
		slot arnbuf, 20		; 2^64-1 is 20 decimal digits
	endl
	mov	rax, [value]
	lea	r12, [arnbuf + 20]	; written backward from one past the end
	xor	r13, r13
	mov	rbx, 10
  .div:
	xor	edx, edx
	div	rbx			; rax := rax/10, rdx := rax mod 10
	add	dl, '0'
	dec	r12
	mov	[r12], dl
	inc	r13
	test	rax, rax
	jnz	.div
	mov	edi, 2
	mov	rsi, r12
	mov	rdx, r13
	call	sys_write
	return
endp

; __arena_exhausted(arena, want) -- NEVER RETURNS. Says what rt/ knows,
; gives the owner its one chance to say what it knows, then traps with the
; same raw `ud2` encoding `macros/assert.inc` uses (there is no symbolic
; mnemonic for it in vendor/fasmg-x86). See this file's header for why this
; exists at all and why the exit status is deliberately unchanged.
proc __arena_exhausted, arena, want
	uses	rbx
	locals
	endl
	__arena_saylit "exsc: internal error: an arena is exhausted -- wanted "
	mov	rdi, [want]
	call	__arena_say_u64
	__arena_saylit " bytes, "
	mov	rdi, [arena]
	mov	rax, [rdi + Arena.cur]
	sub	rax, [rdi + Arena.base]
	mov	rdi, rax
	call	__arena_say_u64
	__arena_saylit " of "
	mov	rdi, [arena]
	mov	rax, [rdi + Arena.limit]
	sub	rax, [rdi + Arena.base]
	mov	rdi, rax
	call	__arena_say_u64
	__arena_saylit " bytes already in use.", 10

	mov	rbx, [arena]
	mov	rbx, [rbx + Arena.oom]
	test	rbx, rbx
	jz	.trap
	mov	rdi, [arena]
	mov	rsi, [want]
	call	rbx
  .trap:
	db	0x0F, 0x0B
endp

; arena_init(arena, capacity) -> CF clear on success; CF set, eax = the
;                                underlying sys_mmap's -errno on failure.
; `arena` points at caller-owned `sizeof.Arena` bytes (a global, a
; `locals` slot, anywhere) -- this routine does not allocate the Arena
; struct itself, only the region it describes. `capacity` is rounded up to
; a page by the kernel; callers do not need to pre-round it.
proc arena_init, arena, capacity
	locals
	endl
	xor	rdi, rdi		; addr = NULL -- let the kernel choose
	mov	rsi, [capacity]
	mov	edx, 3			; PROT_READ|PROT_WRITE
	mov	ecx, 0x22		; MAP_PRIVATE|MAP_ANONYMOUS
	mov	r8d, -1			; fd (ignored for anonymous)
	xor	r9, r9			; offset
	call	sys_mmap
	jc	.mmap_failed
	mov	rdi, [arena]
	mov	[rdi + Arena.base], rax
	mov	[rdi + Arena.cur], rax
	add	rax, [capacity]
	mov	[rdi + Arena.limit], rax
	mov	qword [rdi + Arena.oom], 0	; an owner registers AFTER this
	return
  .mmap_failed:
	; propagate sys_mmap's own CF/eax verbatim -- this failure genuinely
	; originated in a rt/sys.inc wrapper, so its channel is honored, not
	; re-encoded -- the errno channel explicitly permits verbatim
	; propagation (docs/asm-conventions.md, "1.3 Error protocol: CF / eax").
	fail	eax
endp

; arena_reset(arena) -- O(1). Rewinds the bump pointer to the start of the
; region; does not unmap or zero it. Cannot fail; `clc`s for a caller that
; uniformly checks CF after any rt/ call.
proc arena_reset, arena
	locals
	endl
	mov	rdi, [arena]
	mov	rax, [rdi + Arena.base]
	mov	[rdi + Arena.cur], rax
	return
endp

; arena_alloc(arena, size) -> rax = pointer to `size` bytes, 8-byte aligned.
; Reports and then traps if the arena's fixed capacity is exhausted -- see
; this file's header for why the fixed capacity is a deliberate v1 design
; choice, why the report exists, and why the check is neither an `rassert`
; nor an `EXS-E` code. Never sets CF (there is no recoverable failure path
; once past the check); `clc`s for caller uniformity.
proc arena_alloc, arena, size
	locals
	endl
	mov	rdi, [arena]
	mov	rax, [rdi + Arena.cur]
	add	rax, 7			; round up to 8-byte alignment
	and	rax, -8
	mov	rcx, rax
	add	rcx, [size]
	jc	.exhausted		; a `size` so large it wrapped is not
					; an allocation, it is exhaustion
	mov	rdx, [rdi + Arena.limit]
	cmp	rcx, rdx		; UNSIGNED: these are addresses
	ja	.exhausted
	mov	[rdi + Arena.cur], rcx
	return
  .exhausted:
	mov	rdi, [arena]
	mov	rsi, [size]
	call	__arena_exhausted	; never returns
	return
endp

; arena_destroy(arena) -> CF clear on success; CF set, eax = the underlying
;                         sys_munmap's -errno on failure. Releases the
;                         backing region; the `Arena` struct itself is
;                         still caller-owned and is left with dangling
;                         base/cur/limit fields -- callers must not
;                         `arena_alloc`/`arena_reset` it again afterward.
proc arena_destroy, arena
	locals
	endl
	mov	rdi, [arena]
	mov	rax, [rdi + Arena.base]
	mov	rcx, [rdi + Arena.limit]
	sub	rcx, rax		; len = end - base
	mov	rdi, rax		; addr = base
	mov	rsi, rcx		; len
	call	sys_munmap
	jc	.munmap_failed
	return
  .munmap_failed:
	fail	eax
endp
