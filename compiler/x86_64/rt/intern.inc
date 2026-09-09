; compiler/x86_64/rt/intern.inc
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
; String interner. Identity is a MONOTONIC INTEGER ID, assigned in
; first-seen order -- **never a pointer**. A pointer varies run to run on
; byte-identical input (arena placement comes from `mmap`, which shifts
; with unrelated prior allocations -- rt/map.inc's own probe,
; tools/rt-map-order-probe.sh, measures exactly this); an insertion-ordered
; integer is structurally immune, not immune by discipline (CLAUDE.md,
; "Determinism is not optional" -- "No ordering may depend on a pointer
; value").
;
; Built directly on rt/map.inc: `intern_id` is `map_get` the content, and
; on a miss, copy the bytes into the interner's own arena, `map_insert`,
; done -- the ID is the map's own dense-array length (see rt/map.inc's
; header: the array is append-only, insertion-ordered), no separate
; counter to keep in sync. IDs are 1-based (the first interned string gets
; ID 1) specifically so ID 0 stays free as an "uninterned"/invalid
; sentinel, consistent with `map_get`'s own "0 = not found" convention
; (rt/map.inc's header).
;
; LIFETIME: the whole compilation context. **NEVER `arena_reset` the arena
; backing a live interner** -- every `intern_id` this wave's callers ever
; receive stays valid only as long as the bytes `intern_bytes` would hand
; back for it do, and `arena_reset` invalidates all of them at once with no
; way to detect the mistake later (the next allocation from that arena
; simply overwrites the interned bytes). Resettable scratch work -- a
; per-phase temporary buffer, for instance -- must use a *different*
; `Arena` instance than the one passed to `intern_init`.
;
; `intern_bytes(id)` indexes the same dense array `intern_id` appends to.
; The pointer it returns is for IMMEDIATE USE ONLY -- read the bytes, copy
; them, compare them right there -- **never** stash it as a sort key, a
; hash key, or anything else that outlives the call: nothing in this file
; promises the returned address is the same on a later call for the same
; id (nothing currently moves it either, but that is not a promise this
; file is making -- compare by ID, not by the bytes' address).
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3 (identity that cannot vary
; with placement is exactly the "no ordering may depend on a pointer
; value" requirement, applied to identifiers instead of map entries).
; -----------------------------------------------------------------------------

; Only `map.inc` is included directly -- see rt/vec.inc's and rt/map.inc's
; own include comments for why re-including anything it already brings in
; (transitively: arena.inc, struct.inc, assert.inc, sys.inc, proc.inc)
; here directly would break the build (a `struct`/`proc` processed twice).
include 'map.inc'

struct Interner
	field arena, dq		; backs both the copied key bytes AND the
				; Map's own growth -- the same arena,
				; deliberately: everything this file owns
				; shares one "never reset while live" region
	field map,   dq		; -> Map, content bytes -> assigned ID
end struct

; intern_init(interner, arena, nbuckets) -- `nbuckets` MUST be a power of 2
; (rt/map.inc's `map_init` `rassert`s this). Always succeeds otherwise (or
; `rassert`s transitively through `arena_alloc` if `arena` is already
; exhausted).
proc intern_init, interner, arena, nbuckets
	locals
	endl
	mov	rdi, [interner]
	mov	rax, [arena]
	mov	[rdi + Interner.arena], rax
	mov	rdi, [arena]
	mov	rsi, sizeof.Map
	call	arena_alloc
	mov	rdi, [interner]
	mov	[rdi + Interner.map], rax
	mov	rdi, rax
	mov	rsi, [arena]
	mov	rdx, [nbuckets]
	call	map_init
	return
endp

; intern_id(interner, ptr, len) -> rax = the 1-based monotonic ID for the
; `len` bytes at `ptr`. The SAME content always yields the SAME id, for
; the life of `interner`; different content already seen keeps its
; original id even if a byte-identical *duplicate* is interned again later
; (this is `map_get`-then-`map_insert`-on-miss, not "insert
; unconditionally" -- see rt/map.inc's header on the distinction).
proc intern_id, interner, ptr, len
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [interner]
	mov	r12, [rbx + Interner.map]
	mov	rdi, r12
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	map_get
	test	rax, rax
	jnz	.found			; already interned -- rax is already the id

	; miss: copy the bytes into the interner's own arena (they must
	; outlive `ptr`, which the caller owns and may reuse or free)
	mov	rdi, [rbx + Interner.arena]
	mov	rsi, [len]
	call	arena_alloc
	mov	r13, rax		; r13 = destination for the copy
	mov	rdi, r13
	mov	rsi, [ptr]
	mov	rcx, [len]
	cld
	rep	movsb

	; new id = current dense-array length + 1, computed BEFORE this
	; insert (see this file's header: no separate counter)
	mov	r14, [r12 + Map.entries]
	mov	r14, [r14 + Vec.len]
	inc	r14

	mov	rdi, r12
	mov	rsi, r13
	mov	rdx, [len]
	mov	rcx, r14
	call	map_insert
	mov	rax, r14
	return
  .found:
	return
endp

; intern_bytes(interner, id) -> rax = pointer to the interned bytes,
; rdx = their length. `id` must be a value `intern_id` actually returned
; for this `interner` -- an invalid id (0, or past the last one assigned)
; `rassert`s, transitively, through `vec_get`'s own bounds check
; (rt/vec.inc's header) -- an out-of-range intern id from this compiler's
; own code is an internal contract violation, not a source-level condition.
proc intern_bytes, interner, id
	locals
	endl
	mov	rdi, [interner]
	mov	rdi, [rdi + Interner.map]
	mov	rdi, [rdi + Map.entries]
	mov	rsi, [id]
	dec	rsi			; 1-based id -> 0-based dense-array index
	call	vec_get
	mov	rdx, [rax + MapEntry.key_len]
	mov	rax, [rax + MapEntry.key_ptr]
	return
endp
