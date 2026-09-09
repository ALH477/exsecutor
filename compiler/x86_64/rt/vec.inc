; compiler/x86_64/rt/vec.inc
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
; Stride-generic growable array. Doubling growth, arena-backed -- no `free`,
; no `realloc` (this is a freestanding binary; there is no libc to have
; either). Growing abandons the old backing block rather than resizing it
; in place: the owning `Arena` reclaims it, in bulk, only when the whole
; arena is `arena_reset` -- an accepted, documented cost (this wave's
; design decision), not an oversight. A `Vec` used across many small
; pushes in a tight loop therefore wastes up to ~2x its final size in
; arena space until reset; callers that care preallocate a sensible
; `initial_cap`.
;
; "Reserve a slot, hand back a pointer" rather than "copy this value in":
; `vec_push` never needs to know its element's internal layout, so this
; file has no `stride`-many special cases and needs no generic `memcpy`
; helper of its own for the common case -- growth is the one place an
; internal copy is needed, and that copies whole already-contiguous
; elements with `rep movsb`, not field-by-field.
;
; rt/map.inc builds its dense, insertion-ordered entry array directly on
; this file (CLAUDE.md, "Maps iterate in insertion order"): `map_iterate`
; walks a `Vec` from index 0, never the hash buckets.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §6.3 (arena-backed allocation
; throughout rt/); CLAUDE.md "Determinism is not optional" -- growth order
; and element order are both purely index-driven, never dependent on where
; the arena happened to place a block.
; -----------------------------------------------------------------------------

; Only `arena.inc` is included directly -- it transitively brings in
; struct.inc, assert.inc, sys.inc and macros/proc.inc. Do NOT also
; `include` those here: fasmg tolerates re-processing a `macro ... end
; macro` definition (later one silently wins -- see macros/assert.inc's
; header for why that is exactly the footgun `rassert` avoids for
; `assert`), but a `struct ... end struct` block defines real, non-
; redefinable labels underneath (fasmg's native `struc`), and a `proc`
; defines a real, non-redefinable global label -- either one, processed
; twice because two included files both pulled in a third, fails outright
; ("symbol already defined" / "out of scope"). Confirmed empirically while
; wiring up rt/map.inc, which has the same diamond (map.inc -> vec.inc ->
; arena.inc, and map.inc -> arena.inc directly) -- fixed there the same
; way: let exactly one path bring in each file, and every other file that
; needs it gets it transitively.
include 'arena.inc'

struct Vec
	field arena,  dq	; owning Arena -- growth allocates from here
	field data,   dq	; current backing buffer
	field len,    dq	; elements in use
	field cap,    dq	; elements the current buffer can hold
	field stride, dq	; bytes per element
end struct

; vec_init(vec, arena, stride, initial_cap) -- always succeeds (or
; `rassert`s transitively, through `arena_alloc`, if `arena` is already
; exhausted -- see rt/arena.inc). `clc`s for caller uniformity.
proc vec_init, vec, arena, stride, initial_cap
	locals
	endl
	mov	rdi, [vec]
	mov	rax, [arena]
	mov	[rdi + Vec.arena], rax
	mov	rax, [stride]
	mov	[rdi + Vec.stride], rax
	mov	qword [rdi + Vec.len], 0
	mov	rax, [initial_cap]
	mov	[rdi + Vec.cap], rax
	cmp	rax, 0
	je	.no_backing
	mov	rdi, [arena]
	mov	rsi, [initial_cap]
	imul	rsi, [stride]
	call	arena_alloc
	mov	rdi, [vec]
	mov	[rdi + Vec.data], rax
	return
  .no_backing:
	mov	rdi, [vec]
	mov	qword [rdi + Vec.data], 0
	return
endp

; vec_push(vec) -> rax = pointer to a new, UNINITIALIZED element slot; the
; caller fills it in. Grows (doubling, minimum 1) when the current buffer
; is full. Always succeeds (or `rassert`s transitively -- see vec_init).
proc vec_push, vec
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [vec]
	mov	rax, [rbx + Vec.len]
	cmp	rax, [rbx + Vec.cap]
	jl	.have_room

	; grow: new_cap = max(cap * 2, 1)
	mov	r12, [rbx + Vec.cap]
	mov	r13, r12
	add	r13, r13
	cmp	r13, 0
	jne	.newcap_ok
	mov	r13, 1
  .newcap_ok:
	mov	rdi, [rbx + Vec.arena]
	mov	rsi, r13
	imul	rsi, [rbx + Vec.stride]
	call	arena_alloc
	mov	r14, rax			; new backing block

	; copy the live elements (len * stride bytes) into the new block
	mov	rsi, [rbx + Vec.data]
	mov	rdi, r14
	mov	rcx, [rbx + Vec.len]
	imul	rcx, [rbx + Vec.stride]
	cmp	rcx, 0
	je	.copy_done
	rep	movsb
  .copy_done:
	mov	[rbx + Vec.data], r14
	mov	[rbx + Vec.cap], r13

  .have_room:
	mov	rax, [rbx + Vec.len]
	mov	rcx, rax
	inc	rcx
	mov	[rbx + Vec.len], rcx
	; rax = old len = the index of the newly reserved slot
	imul	rax, [rbx + Vec.stride]
	add	rax, [rbx + Vec.data]
	return
endp

; vec_get(vec, index) -> rax = pointer to element `index`. `rassert`s
; (traps) if `index >= len` -- an out-of-bounds access from this
; compiler's own code is an internal contract violation, not a source-level
; diagnosable condition (docs/asm-conventions.md, "1.3 Error protocol:
; CF / eax", carries the four failure channels).
proc vec_get, vec, index
	locals
	endl
	mov	rdi, [vec]
	mov	rax, [index]
	mov	rcx, [rdi + Vec.len]
	rassert	rax lt rcx
	imul	rax, [rdi + Vec.stride]
	add	rax, [rdi + Vec.data]
	return
endp
