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
; that figure as its own result, see this wave's report), and this file is
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
; oversight. `arena_alloc` past the capacity given to `arena_init`
; `rassert`s (an internal contract violation: the caller under-sized the
; arena) rather than transparently mapping more. A bump allocator that DID
; grow transparently would need either to move existing allocations
; (invalidating every live pointer into the arena -- unacceptable for an
; allocator every other rt/ module hands pointers out of) or to track a
; chunk list (so `arena_reset` -- and any eventual `arena_destroy` -- has
; more than one region to account for). Both are real designs; neither is
; this wave's to build without a concrete caller that needs it.
; `[UNIMPLEMENTED]`: auto-growth / chunked arenas.
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
end struct

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
	return
  .mmap_failed:
	; propagate sys_mmap's own CF/eax verbatim -- this failure genuinely
	; originated in a rt/sys.inc wrapper, so its channel is honored, not
	; re-encoded (this wave's failure-channel synthesis; see this file's
	; header and this wave's report).
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
; `rassert`s (traps) if the arena's fixed capacity is exhausted -- see this
; file's header for why that is a deliberate v1 design choice, not an
; omission. Never sets CF (there is no recoverable failure path once past
; the rassert); `clc`s for caller uniformity.
proc arena_alloc, arena, size
	locals
	endl
	mov	rdi, [arena]
	mov	rax, [rdi + Arena.cur]
	add	rax, 7			; round up to 8-byte alignment
	and	rax, -8
	mov	rcx, rax
	add	rcx, [size]
	mov	rdx, [rdi + Arena.limit]	; rassert's condition parser wants a
					; plain "lhs op rhs" of exactly 3
					; tokens (macros/assert.inc's header) --
					; load the memory operand first rather
					; than risk `[rdi + Arena.limit]` inside
					; the condition itself
	rassert	rcx le rdx
	mov	[rdi + Arena.cur], rcx
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
