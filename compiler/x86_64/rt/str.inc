; compiler/x86_64/rt/str.inc
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
; `Slice = {ptr: dq, len: dq}` -- length-prefixed, NOT NUL-terminated. The
; compiler works on spans into shared source text, which cannot assume
; termination at an interior boundary (a slice's end is rarely a string's
; end), and UTF-8 source can carry an interior U+0000 after escape
; processing -- a NUL-terminated representation cannot even represent that
; input faithfully, let alone slice it. `str_slice` is therefore O(1): pure
; pointer/length arithmetic, no scan for a terminator, no copy.
;
; BYTE-ORIENTED, NOT CODEPOINT-AWARE. Every function here treats `len` as a
; byte count and every offset as a byte offset. Decoding UTF-8, validating
; it, or reasoning about codepoints/graphemes belongs to the lexer and
; compiler/shared/unicode, not here.
;
; Most functions take a `Slice` as two plain arguments (`ptr, len`) rather
; than a pointer to a heap `Slice` struct, and multi-value results come back
; as `rax`/`rdx` (this file's own convention -- SysV has no wider native
; return for a 16-byte, two-field value walked through macros/proc.inc's
; plain arg registers) -- consistent with every other rt/ module's (ptr,
; len) key convention (rt/map.inc, rt/intern.inc). The `Slice` struct itself
; exists for STORAGE (an AST node holding a `Slice` field, for instance), not
; as an opaque handle every function must take by pointer.
;
; `str_to_cstr` is the ONE narrow exception, and it is exactly that:
; syscall-boundary-only, for `openat`'s pathname argument, which requires a
; NUL-terminated string because the kernel ABI does. **Never use its result
; for identity or comparison** -- two different `Slice`s can produce
; identical C-string output if either contains an interior NUL (the
; C-string reads as truncated at the first one), and nothing here detects
; that; it is an accepted, unavoidable property of the one boundary that
; forces a C string to exist at all, not something this function can fix.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.1 (text and branded offsets --
; `Slice` is this compiler's own internal span type, not the language's
; `textus`, but is shaped by the same "no silent truncation, no assumed
; termination" reasoning that section starts from).
; -----------------------------------------------------------------------------

; Only `arena.inc` is included directly. See rt/vec.inc's include comment
; for why: this file needs nothing from map.inc/vec.inc, so it depends on
; exactly what it uses, per the build order (`sys.inc -> arena.inc ->
; vec.inc -> map.inc -> intern.inc`, with `str.inc`/`sort.inc` as siblings
; needing only `arena.inc`) -- not because including more would break
; anything here (str.inc is a leaf; nothing after it in this wave includes
; it, so there is no diamond to create), simply because it is not needed.
include 'arena.inc'

; Named `Slice`, not `Str`: `Str` collides case-insensitively with the real
; x86 `STR` instruction mnemonic (Store Task Register -- a real, if
; obscure and privileged, opcode in the vendored instruction set) --
; `struct Str ... end struct` failed with "symbol 'Str' is undefined or
; out of scope" from deep inside struct.inc's own `struc`-instantiation
; step, not a message that named the actual collision. Found by running
; this file's own test fixture, the same way the `Arena.end` field name
; (rt/arena.inc's header) and the `start` proc argument (above) were both
; found -- a third instance of the same category of mistake (reusing a
; name the assembler itself already gives meaning to), recorded here for
; the same reason those were.
struct Slice
	field ptr, dq
	field len, dq
end struct

; str_slice(ptr, len, off, sublen) -> rax = ptr + off, rdx = sublen.
; O(1) -- pure arithmetic, no copy, no scan. `rassert`s that the requested
; sub-span actually fits within [0, len) -- an out-of-range slice from this
; compiler's own code is an internal contract violation, not a source-level
; condition (rt/sys.inc's header carries the full failure-channel table).
;
; Parameter named `off`, NOT `start`: `macros/proc.inc` binds each argument
; to a plain, unmangled, global `=` symbol (documented there), and `start`
; is the ELF entry label every fixture in this tree uses (`entry start` /
; `start:`) -- a proc argument named `start` silently redefines that global
; symbol to a stack-relative expression, which fails far from the cause,
; inside `entry`'s own address resolution, with a message about
; `e_entry`/`DYNAMIC` that says nothing about a naming collision. Caught by
; running this file's own test fixture, not by inspection. `macros/
; proc.inc` now rejects this class of name outright (see its "reserved
; names" section) -- this file is renamed to `off` on top of that guard,
; not instead of it, since the guard was added specifically because this
; exact mistake is easy to make again.
proc str_slice, ptr, len, off, sublen
	locals
	endl
	mov	rax, [off]
	add	rax, [sublen]
	mov	rcx, [len]
	rassert	rax le rcx
	mov	rax, [ptr]
	add	rax, [off]
	mov	rdx, [sublen]
	return
endp

; str_eq(a_ptr, a_len, b_ptr, b_len) -> eax = 1 if the two spans are
; byte-identical (same length AND same bytes), else 0.
proc str_eq, a_ptr, a_len, b_ptr, b_len
	locals
	endl
	mov	rax, [a_len]
	cmp	rax, [b_len]
	jne	.not_equal
	mov	rsi, [a_ptr]
	mov	rdi, [b_ptr]
	mov	rcx, rax
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

; str_copy(arena, ptr, len) -> rax = pointer to a fresh `len`-byte copy,
; allocated from `arena`. NOT NUL-terminated (this is a `Slice` body copy,
; not a C string -- see str_to_cstr for that). `rassert`s transitively
; through `arena_alloc` if `arena` is exhausted (rt/arena.inc's header).
proc str_copy, arena, ptr, len
	locals
	endl
	mov	rdi, [arena]
	mov	rsi, [len]
	call	arena_alloc
	push	rax
	mov	rdi, rax
	mov	rsi, [ptr]
	mov	rcx, [len]
	cld
	rep	movsb
	pop	rax
	return
endp

; str_to_cstr(arena, ptr, len) -> rax = pointer to a fresh NUL-terminated
; copy (`len + 1` bytes: the `len` original bytes, then one `0` byte),
; allocated from `arena`. See this file's header: syscall-boundary-only
; (`openat`'s pathname), never for identity or comparison.
proc str_to_cstr, arena, ptr, len
	locals
	endl
	mov	rdi, [arena]
	mov	rsi, [len]
	inc	rsi
	call	arena_alloc
	push	rax
	mov	rdi, rax
	mov	rsi, [ptr]
	mov	rcx, [len]
	cld
	rep	movsb
	mov	byte [rdi], 0
	pop	rax
	return
endp
