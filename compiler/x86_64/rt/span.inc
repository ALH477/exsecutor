; compiler/x86_64/rt/span.inc
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
; `Span { file_id: u32, start: u32, len: u32 }` -- 12 bytes. Added this wave
; ahead of any caller, on the coordinator's explicit direction: every
; wave-2 module (lexer, cst, diag) produces, stores, or renders source
; spans, none may write another agent's tree, and spec §8.3 calls a
; diagnostic's span "not retrofittable" ("Every diagnostic carries a
; source span -- not retrofittable, which is why the CST is not
; optional"). Fixing this shape now costs one struct; fixing it after
; three independent trees have already picked their own costs a rewrite
; across all three.
;
; `file_id` is an INTERN ID (rt/intern.inc), not a pointer and not a raw
; file descriptor -- identity that stays a monotonic integer, never
; something that can vary with `mmap` placement (CLAUDE.md, "No ordering
; may depend on a pointer value"; rt/intern.inc's own header makes the
; same argument for content identity, and a source file's own identity is
; exactly the same kind of thing: intern the file's PATH once, per
; compilation, and carry the resulting id everywhere a span needs to name
; which file it came from). `exsc` owns exactly one text buffer per input
; file for the lexer to slice (spec §9.1's pipeline starts at "source"),
; so one process-wide `Interner` mapping path -> file_id, held by whatever
; wave-2 component reads files in, is enough for every span in a
; compilation to share.
;
; BYTE OFFSETS, not line/column. Line/column is a RENDERING concern (spec
; §8.3: diagnostics are the consumer that needs it), not a storage one --
; computing it eagerly would be a per-token cost paid on every span this
; compiler ever creates, for a value most spans never render. A future
; `compiler/x86_64/diag/` module can recover line/column from a byte
; offset by scanning the source text once per diagnostic (or building a
; line-start index once per file, amortized across all of that file's
; diagnostics) -- cheap where it is actually needed, free everywhere else.
; This file does not build that index; it is a rendering-time concern for
; whichever module owns rendering, not a storage-time one for the type
; every module stores spans in.
;
; 32-bit fields, not 64: a `Span` is meant to be embedded directly in
; every AST/CST node this compiler builds, potentially millions of times
; over a large source file -- `u32` keeps it at 12 bytes instead of 24,
; and 4 GiB is already an extravagant ceiling for both a single source
; file's byte length and the number of distinct files/interned strings one
; compilation could plausibly reach. If that ceiling is ever a real
; problem, it is spec's and the wave-2 agents' call to widen it, not a
; silent choice to make here.
;
; NOT rassert-checked for internal consistency (e.g. `len` fitting within
; some file's actual size) -- this file has no access to file sizes or a
; file table; it stores exactly the three numbers it is given. Whichever
; module actually creates spans (the lexer, primarily) is where a bounds
; check against real source text belongs.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3 ("Every diagnostic carries a
; source span -- not retrofittable, which is why the CST is not
; optional").
; -----------------------------------------------------------------------------

; Only `intern.inc` is included directly -- `file_id` being an intern id is
; documentation/convention, not a hard dependency this file's own code
; needs (nothing here calls into rt/intern.inc), but including it keeps
; "what a file_id IS" one `include` away for anyone reading this file, and
; costs nothing extra transitively (map.inc/vec.inc/arena.inc/etc. are
; already brought in by intern.inc; see rt/vec.inc's include comment for
; why nothing here also includes any of those directly).
include 'intern.inc'

struct Span
	field file_id, dd
	field start,   dd
	field len,     dd
end struct

; span_make(dest, file_id, off, len) -- writes the three fields of a
; caller-owned `Span` at `dest`. Plain field assignment; cannot fail.
;
; Parameter named `off`, not `start`: macros/proc.inc's reserved-name
; guard (added this wave specifically because of this exact mistake --
; see that file's header) rejects `start` as a proc argument outright, at
; assemble time, with a clear message, rather than allowing this file to
; repeat rt/str.inc's original error.
proc span_make, dest, file_id, off, len
	locals
	endl
	mov	rdi, [dest]
	mov	eax, [file_id]
	mov	[rdi + Span.file_id], eax
	mov	eax, [off]
	mov	[rdi + Span.start], eax
	mov	eax, [len]
	mov	[rdi + Span.len], eax
	return
endp

; span_end(span) -> eax = start + len (the one-past-the-end byte offset).
proc span_end, span
	locals
	endl
	mov	rdi, [span]
	mov	eax, [rdi + Span.start]
	add	eax, [rdi + Span.len]
	return
endp

; span_contains(span, off) -> eax = 1 if the byte offset `off` falls
; within [start, start+len), else 0. `file_id` is not consulted -- this
; answers "is this offset within this span's [start,end) range", not
; "in this file"; a caller comparing spans across files must check
; `file_id` itself first.
proc span_contains, span, off
	locals
	endl
	mov	rdi, [span]
	mov	eax, [off]
	cmp	eax, [rdi + Span.start]
	jl	.no
	mov	ecx, [rdi + Span.start]
	add	ecx, [rdi + Span.len]
	cmp	eax, ecx
	jge	.no
	mov	eax, 1
	return
  .no:
	mov	eax, 0
	return
endp

; span_union(a, b, dest) -- writes to `dest` the smallest Span covering
; both `a` and `b`: `file_id` (must match -- `rassert`ed, an internal
; contract violation otherwise: a union across two different files is not
; a meaningful span), `start` = min(a.start, b.start),
; `len` = max(a.end, b.end) - start.
; BUG CAUGHT BY RE-READING THIS, not by running it (the fixture below
; would have caught it too -- this was found first): the original draft
; kept `b` in `rdi` and reused `ecx` for both "b.file_id" (needed only at
; the very end, to write `dest`'s field) and "b.start" (needed in the
; min/max arithmetic in between) -- the second use silently clobbered the
; first, so the value finally written to `dest.file_id` was `b.start`, not
; `b.file_id`. Fixed by keeping BOTH `a` and `b` alive in callee-saved
; registers for the whole routine and reloading `file_id` fresh at the
; point of use, rather than trying to carry it through in a
; general-purpose register across unrelated arithmetic.
proc span_union, a, b, dest
	uses	rbx, r12
	locals
	endl
	mov	rbx, [a]
	mov	r12, [b]
	mov	eax, [rbx + Span.file_id]
	mov	ecx, [r12 + Span.file_id]
	rassert	eax eq ecx

	mov	eax, [rbx + Span.start]
	mov	ecx, [r12 + Span.start]
	cmp	eax, ecx
	jle	.a_starts_first
	mov	eax, ecx
  .a_starts_first:
	; eax = min(a.start, b.start)

	mov	edx, [rbx + Span.start]
	add	edx, [rbx + Span.len]		; a.end
	mov	esi, [r12 + Span.start]
	add	esi, [r12 + Span.len]		; b.end
	cmp	edx, esi
	jge	.a_ends_last
	mov	edx, esi
  .a_ends_last:
	; edx = max(a.end, b.end)

	sub	edx, eax			; len = max_end - min_start

	mov	rdi, [dest]
	mov	ecx, [rbx + Span.file_id]	; reloaded fresh -- a.file_id ==
						; b.file_id, rasserted above
	mov	[rdi + Span.file_id], ecx
	mov	[rdi + Span.start], eax
	mov	[rdi + Span.len], edx
	return
endp
