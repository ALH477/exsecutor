; compiler/x86_64/rt/map.inc
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
; (ptr,len)-keyed map with a STRUCTURAL insertion-order iteration guarantee
; (CLAUDE.md, "Determinism is not optional": "Maps iterate in insertion
; order... do not substitute a faster map with address- or hash-dependent
; iteration"). This is not a discipline this file's callers have to
; maintain -- it is not possible to observe any other order, because there
; is no code path that walks anything BUT the dense array:
;
;   - a `Vec` (rt/vec.inc) of `MapEntry`, append-only, in insertion order --
;     THE data. `map_iterate` walks this and only this, from index 0.
;   - a separate hash-bucket index (a plain array of dense-array INDICES,
;     never pointers -- `vec_push`'s doubling growth can and does move the
;     dense array's backing block, which would silently invalidate a
;     pointer but cannot invalidate an index) used ONLY to accelerate
;     `map_get`/`map_insert`'s lookup. `map_iterate` never touches it.
;
; Collision handling is separate chaining: each `MapEntry` carries a `next`
; field -- the index of the next entry hashing to the same bucket, or
; `MAP_NO_ENTRY`. The bucket array holds each chain's head index. Chain
; order (most-recently-inserted first per bucket) affects lookup speed
; only, never anything observable -- `map_get` returns AT MOST one value
; per key (see `map_insert`), and `map_iterate` never consults chains at
; all.
;
; Keys are (ptr,len) byte buffers -- NOT copied by this file. A key must
; remain valid for as long as the map is queried; `rt/intern.inc` is what
; actually owns key-byte lifetime (it copies into its own arena before
; `map_insert`, per that file's header). Hashing is FNV-1a, 64-bit,
; unseeded: this compiler never touches a network (CLAUDE.md's closed
; syscall allowlist has no socket family in it, checked by `make audit`),
; so there is no hashDoS threat model to seed against, and an unseeded hash
; is itself part of the determinism story -- a seeded one would make
; bucket layout (though never iteration order) vary run to run.
;
; `map_get` on a miss returns the sentinel `0` in `rax`, CF not meaningful
; (the "expected absent result" channel; see rt/sys.inc's header for the
; full failure-channel table this wave synthesized). Consequently **a
; value of exactly 0 cannot be distinguished from "not found"** -- callers
; that need to store 0 must offset or box it. `rt/intern.inc` sidesteps
; this by construction: its IDs are the dense array's length AFTER
; insertion, so the first interned string gets ID 1, never 0.
;
; `[UNIMPLEMENTED]`, both deliberate v1 scope limits, not oversights:
;   - No removal. A compiler's own symbol/intern/type tables are built up
;     and periodically reset (per rt/arena.inc's whole-arena model), never
;     edited one entry at a time.
;   - No automatic bucket-table resize. `map_init`'s caller picks a fixed
;     `nbuckets` (must be a power of 2) for the map's whole lifetime; a
;     bucket with many collisions degrades that bucket's lookups toward
;     O(n), a performance concern, never a correctness one. Rebuilding only
;     the index on resize (this file's own doc comment on `Map`, above)
;     was designed for; wiring an automatic trigger to it is future work
;     with no caller in this wave that needs it yet.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3 ("Determinism is not
; optional", per CLAUDE.md, operationalized here structurally rather than
; by convention).
; -----------------------------------------------------------------------------

; Only `vec.inc` is included directly -- it transitively brings in
; arena.inc, struct.inc, assert.inc, sys.inc and macros/proc.inc. See
; rt/vec.inc's own include comment for why re-including any of those here
; directly is not just redundant but actively breaks the build (a `struct`
; or `proc` processed twice, via two different include paths, redefines a
; real label and fails outright) -- this is the diamond that finding was
; written about.
include 'vec.inc'

MAP_NO_ENTRY = -1

struct MapEntry
	field key_ptr, dq
	field key_len, dq
	field value,   dq
	field next,    dq	; index of the next entry in this bucket's
				; chain, or MAP_NO_ENTRY
end struct

struct Map
	field arena,    dq	; backs both `entries`' growth and `buckets`
	field entries,  dq	; -> Vec of MapEntry, insertion order, append-only
	field buckets,  dq	; -> array of dq: dense-array index, or MAP_NO_ENTRY
	field nbuckets, dq	; bucket count -- fixed for the map's lifetime,
				; MUST be a power of 2 (lookup masks, never
				; divides or mods)
end struct

; map_hash(ptr, len) -> rax = 64-bit FNV-1a hash. Private-ish (no leading
; underscore only because it is a plausibly-reusable primitive, per
; docs/asm-conventions.md's module-prefix rule -- callers outside this file
; should still prefer `map_get`/`map_insert` over hashing keys themselves).
proc map_hash, ptr, len
	locals
	endl
	mov	rax, 0xCBF29CE484222325	; FNV-1a 64-bit offset basis
	mov	r8, 0x100000001B3	; FNV-1a 64-bit prime
	mov	rdi, [ptr]
	mov	rcx, [len]
	test	rcx, rcx
	jz	.done
  .loop:
	movzx	edx, byte [rdi]
	xor	rax, rdx		; 64-bit xor -- a 32-bit `xor eax,edx`
					; would zero rax's upper 32 bits every
					; byte (implicit zero-extend on a
					; 32-bit write), destroying the hash
	imul	rax, r8
	inc	rdi
	dec	rcx
	jnz	.loop
  .done:
	return
endp

; __map_bytes_equal(a, b, len) -> eax = 1 if the `len` bytes at `a` and `b`
; are identical, else 0. Private (leading underscore): a byte-compare
; helper, not a primitive other modules should reach for -- rt/str.inc
; (built after this file, independently, on rt/arena.inc only) is where a
; public byte-compare belongs; duplicating a tiny one here rather than
; introducing an ordering dependency this file does not otherwise need.
proc __map_bytes_equal, a, b, len
	locals
	endl
	mov	rsi, [a]
	mov	rdi, [b]
	mov	rcx, [len]
	test	rcx, rcx
	jz	.equal
	cld
	repe	cmpsb
	jne	.not_equal
  .equal:
	mov	eax, 1
	return
  .not_equal:
	mov	eax, 0
	return
endp

; map_init(map, arena, nbuckets) -- `nbuckets` MUST be a power of 2
; (`rassert`ed). Always succeeds otherwise (or `rassert`s transitively
; through `arena_alloc` if `arena` is exhausted -- see rt/arena.inc).
proc map_init, map, arena, nbuckets
	uses	rbx, r12
	locals
	endl
	mov	rbx, [map]
	mov	rax, [arena]
	mov	[rbx + Map.arena], rax
	mov	r12, [nbuckets]
	mov	rax, r12
	dec	rax
	mov	rcx, r12
	and	rax, rcx
	rassert	rax eq 0		; nbuckets must be a power of 2

	; entries: a heap-allocated Vec of MapEntry, initially empty
	mov	rdi, [arena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + Map.entries], rax
	mov	rdi, rax
	mov	rsi, [arena]
	mov	rdx, sizeof.MapEntry
	xor	rcx, rcx
	call	vec_init

	; buckets: nbuckets qwords, each initialized to MAP_NO_ENTRY
	mov	rdi, [arena]
	mov	rsi, r12
	shl	rsi, 3			; * 8 bytes
	call	arena_alloc
	mov	[rbx + Map.buckets], rax
	mov	[rbx + Map.nbuckets], r12
	mov	rdi, rax
	mov	rcx, r12
	mov	rax, MAP_NO_ENTRY
	cld
	rep	stosq
	return
endp

; map_get(map, key_ptr, key_len) -> rax = the stored value, or 0 if not
; found (the "expected absent result" sentinel channel -- CF not
; meaningful either way; see this file's header for why 0 can never be a
; real stored value).
proc map_get, map, key_ptr, key_len
	uses	rbx, r12
	locals
	endl
	mov	rbx, [map]
	mov	rdi, [key_ptr]
	mov	rsi, [key_len]
	call	map_hash
	mov	rcx, [rbx + Map.nbuckets]
	dec	rcx
	and	rax, rcx
	mov	rdx, [rbx + Map.buckets]
	mov	r12, [rdx + rax*8]	; r12 = current chain index
  .walk:
	cmp	r12, MAP_NO_ENTRY
	je	.miss
	mov	rdi, [rbx + Map.entries]
	mov	rsi, r12
	call	vec_get			; rax = pointer to MapEntry
	mov	rcx, [rax + MapEntry.key_len]
	cmp	rcx, [key_len]
	jne	.next_from_rax
	push	rax
	mov	rdi, [rax + MapEntry.key_ptr]
	mov	rsi, [key_ptr]
	mov	rdx, [key_len]
	call	__map_bytes_equal
	pop	rdx			; rdx = the MapEntry pointer again
	test	eax, eax
	jz	.next_from_rdx
	mov	rax, [rdx + MapEntry.value]
	return
  .next_from_rdx:
	mov	r12, [rdx + MapEntry.next]
	jmp	.walk
  .next_from_rax:
	mov	r12, [rax + MapEntry.next]
	jmp	.walk
  .miss:
	xor	eax, eax
	return
endp

; map_insert(map, key_ptr, key_len, value) -- ALWAYS appends a new entry,
; even if `key_ptr`/`key_len` already has one (this file does not check;
; `map_get`-then-`map_insert`-on-miss, as rt/intern.inc does, is the
; caller's responsibility if "insert or update" semantics are wanted). A
; second entry for the same key shadows the first for `map_get` (chain
; walk finds the more-recently-inserted one first) but NOT for
; `map_iterate`, which shows every entry ever inserted, in order --
; `value` must be non-zero (this file's header explains why).
proc map_insert, map, key_ptr, key_len, value
	uses	rbx, r12
	locals
	endl
	mov	rbx, [map]
	mov	rdi, [key_ptr]
	mov	rsi, [key_len]
	call	map_hash
	mov	rcx, [rbx + Map.nbuckets]
	dec	rcx
	and	rax, rcx		; rax = bucket index
	mov	r12, rax

	mov	rdi, [rbx + Map.entries]
	call	vec_push		; rax = pointer to the new MapEntry slot
	mov	rcx, [key_ptr]
	mov	[rax + MapEntry.key_ptr], rcx
	mov	rcx, [key_len]
	mov	[rax + MapEntry.key_len], rcx
	mov	rcx, [value]
	mov	[rax + MapEntry.value], rcx

	mov	rdx, [rbx + Map.buckets]
	mov	rcx, [rdx + r12*8]	; old chain head
	mov	[rax + MapEntry.next], rcx

	; the new entry's own index = (entries.len - 1), since vec_push just
	; incremented it
	mov	rdi, [rbx + Map.entries]
	mov	rax, [rdi + Vec.len]
	dec	rax
	mov	[rdx + r12*8], rax	; bucket head = the new entry's index
	return
endp

; map_iterate(map, callback) -- calls `callback(key_ptr, key_len, value)`
; (plain SysV: rdi, rsi, rdx) once per entry, STRICTLY in insertion order
; -- walks `Map.entries` (a Vec) from index 0, never `Map.buckets`. This is
; the whole point of this file's data layout: there is no other order this
; loop could produce.
proc map_iterate, map, callback
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [map]
	mov	r12, [rbx + Map.entries]
	mov	r13, 0
  .loop:
	mov	rax, [r12 + Vec.len]
	cmp	r13, rax
	jge	.done
	mov	rdi, r12
	mov	rsi, r13
	call	vec_get
	mov	rdi, [rax + MapEntry.key_ptr]
	mov	rsi, [rax + MapEntry.key_len]
	mov	rdx, [rax + MapEntry.value]
	call	qword [callback]	; explicit size: `call [mem]` alone is
					; ambiguous to fasmg ("operand size not
					; specified") -- this is a 64-bit
					; indirect call in 64-bit mode
	inc	r13
	jmp	.loop
  .done:
	return
endp
