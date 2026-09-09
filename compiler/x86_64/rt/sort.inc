; compiler/x86_64/rt/sort.inc
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
; Stable merge sort, in place over a caller-owned array (any `stride`,
; arena-backed scratch). `qsort`-shaped comparator: `cmp_fn(rdi=a, rsi=b)
; -> eax <0 if a<b, 0 if equal, >0 if a>b`, a plain SysV leaf call (no
; special calling convention of its own).
;
; STABLE, not incidentally: on a tie (`eax = 0`), the element from the
; EARLIER run is always taken first during merge, which -- because bottom-
; up merge sort never reorders elements relative to their neighbors within
; a run except by merging -- preserves original relative order among equal
; keys all the way through. This is a design decision (docs/spec's memory
; section neighbors this one), not a happy accident of the algorithm:
; stability pushes the tie-break obligation onto exactly one place (this
; file), instead of onto every call site that would otherwise need its own
; secondary sort key to get deterministic output. Verified, not assumed --
; see tests/unit/sort_stable.asm, which sorts records that share a key and
; checks their original relative order survives.
;
; BOTTOM-UP (iterative), not top-down/recursive: allocates exactly ONE
; scratch buffer, `count * stride` bytes, ONCE, from `arena` -- not one
; per recursion level or merge call. Each pass merges adjacent runs of the
; current width from `base` into the scratch buffer, then copies the
; whole scratch buffer back over `base` before doubling the width. This
; costs one extra `count * stride`-byte copy per pass compared to a
; ping-pong scheme that alternates which buffer is "current" and skips
; the final copy-back when the sorted result already lands in `base` --
; a deliberate simplicity-over-micro-optimization choice for this wave,
; not a limitation load-bearing enough to need a `[UNIMPLEMENTED]` tag.
;
; Comparator parameter named `cmp_fn`, NOT `cmp`: `cmp` is a real x86
; mnemonic, and unlike a proc argument merely colliding with an existing
; GLOBAL SYMBOL (macros/proc.inc's reserved-name guard covers exactly that
; case -- `start`, `main`), naming a proc argument `cmp` fails immediately,
; differently, and unhelpfully: `proc foo, cmp` tries to assemble
; `cmp = rbp - 8` as the CMP INSTRUCTION itself (which needs two operands,
; not `= rbp - 8`), and fails with "missing argument" right there -- loud,
; but not clear. Confirmed empirically before writing this file. This is a
; DIFFERENT failure mode than the `start`/`main` case the reserved-name
; guard targets (that class fails far from its cause, silently, deep
; inside something unrelated; this class fails at its own declaration,
; loudly, just unclearly) and is not added to that guard -- see this
; wave's report for why, and as a general caution: any bare mnemonic
; (`mov`, `add`, `test`, `jmp`, ...) is unsafe as a proc argument or
; `locals` slot name for the same reason, checked by hand here, not
; mechanically.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3 ("Determinism is not
; optional", per CLAUDE.md) -- a stable sort is what makes "sort this by
; some derived key" deterministic without every caller inventing its own
; tie-break.
; -----------------------------------------------------------------------------

; Only `arena.inc` is included directly -- see rt/vec.inc's/rt/str.inc's
; include comments for the same reasoning: this file needs nothing from
; vec/map/intern, so per the build order (`str.inc`/`sort.inc` as siblings
; needing only `arena.inc`), it depends on exactly that.
include 'arena.inc'

; sort_stable(arena, base, count, stride, cmp_fn) -- sorts `count` elements
; of `stride` bytes each, starting at `base`, in place. `rassert`s
; transitively through `arena_alloc` if `arena` is exhausted (rt/arena.inc's
; header); otherwise always succeeds.
;
; All per-merge state (`i`/`j`/`k`/`mid`/`rend`) lives in `locals` slots,
; not registers: `cmp_fn` is an arbitrary caller-supplied function that
; this file cannot assume preserves anything beyond the standard SysV
; callee-saved set, and every one of `proc`'s own arguments (`base`,
; `stride`, `count`, `arena`) is already stack-resident (macros/proc.inc
; spills them immediately) and freely re-readable regardless of what a
; call just clobbered -- so nothing here needs to survive a call inside a
; register at all.
;
; WRITING A COMPARATOR: `cmp_fn` receives POINTERS to two elements in
; `rdi`/`rsi` (the SysV args a `proc a, b` header spills to `[a]`/`[b]`) --
; `[a]` is the pointer ITSELF (the spilled argument), not the element.
; Reading the element needs one more dereference: `mov rcx, [a]` then
; `mov eax, [rcx]`, not `mov eax, [a]` directly. Caught while testing this
; file, not in this file: a first draft of the *test* comparator skipped
; that second dereference and compared the argument-slot pointers instead
; of the array elements they point at -- which, because array elements
; are laid out at strictly increasing addresses, made every comparison
; report "a <= b" regardless of the actual values, so the "sort" silently
; became a no-op merge that reproduced the input's original order exactly
; -- a wrong test passing by coincidence, not sort_stable being broken.
; Worth recording here, in the file callers will read, not only in the
; test that tripped over it.
proc sort_stable, arena, base, count, stride, cmp_fn
	locals
		slot scratch, dq
		slot width,   dq
		slot i,       dq
		slot j,       dq
		slot k,       dq
		slot mid,     dq
		slot rend,    dq
	endl
	mov	rdi, [arena]
	mov	rsi, [count]
	imul	rsi, [stride]
	call	arena_alloc
	mov	[scratch], rax

	mov	qword [width], 1
  .level_loop:
	mov	rax, [width]
	cmp	rax, [count]
	jge	.all_done

	mov	qword [i], 0
  .pair_loop:
	mov	rax, [i]
	cmp	rax, [count]
	jge	.pass_done

	; mid = min(i + width, count)
	mov	rax, [i]
	add	rax, [width]
	mov	rcx, [count]
	cmp	rax, rcx
	jle	.mid_ok
	mov	rax, rcx
  .mid_ok:
	mov	[mid], rax

	; rend = min(mid + width, count)
	add	rax, [width]
	mov	rcx, [count]
	cmp	rax, rcx
	jle	.rend_ok
	mov	rax, rcx
  .rend_ok:
	mov	[rend], rax

	mov	rax, [mid]
	mov	[j], rax
	mov	rax, [i]
	mov	[k], rax

  .merge_loop:
	mov	rax, [i]
	cmp	rax, [mid]
	jge	.copy_right_remainder
	mov	rax, [j]
	cmp	rax, [rend]
	jge	.copy_left_remainder

	; a_ptr = base + i*stride ; b_ptr = base + j*stride
	mov	rdi, [i]
	imul	rdi, [stride]
	add	rdi, [base]
	mov	rsi, [j]
	imul	rsi, [stride]
	add	rsi, [base]
	call	qword [cmp_fn]
	cmp	eax, 0
	jg	.take_right

	; take left (also the tie-break: eax == 0 takes left, for stability)
	mov	rdi, [k]
	imul	rdi, [stride]
	add	rdi, [scratch]
	mov	rsi, [i]
	imul	rsi, [stride]
	add	rsi, [base]
	mov	rcx, [stride]
	cld
	rep	movsb
	mov	rax, [i]
	inc	rax
	mov	[i], rax
	jmp	.advance_k

  .take_right:
	mov	rdi, [k]
	imul	rdi, [stride]
	add	rdi, [scratch]
	mov	rsi, [j]
	imul	rsi, [stride]
	add	rsi, [base]
	mov	rcx, [stride]
	cld
	rep	movsb
	mov	rax, [j]
	inc	rax
	mov	[j], rax

  .advance_k:
	mov	rax, [k]
	inc	rax
	mov	[k], rax
	jmp	.merge_loop

  .copy_left_remainder:
	mov	rax, [i]
	cmp	rax, [mid]
	jge	.merge_pair_done
	mov	rdi, [k]
	imul	rdi, [stride]
	add	rdi, [scratch]
	mov	rsi, [i]
	imul	rsi, [stride]
	add	rsi, [base]
	mov	rcx, [stride]
	cld
	rep	movsb
	mov	rax, [i]
	inc	rax
	mov	[i], rax
	mov	rax, [k]
	inc	rax
	mov	[k], rax
	jmp	.copy_left_remainder

  .copy_right_remainder:
	mov	rax, [j]
	cmp	rax, [rend]
	jge	.merge_pair_done
	mov	rdi, [k]
	imul	rdi, [stride]
	add	rdi, [scratch]
	mov	rsi, [j]
	imul	rsi, [stride]
	add	rsi, [base]
	mov	rcx, [stride]
	cld
	rep	movsb
	mov	rax, [j]
	inc	rax
	mov	[j], rax
	mov	rax, [k]
	inc	rax
	mov	[k], rax
	jmp	.copy_right_remainder

  .merge_pair_done:
	mov	rax, [rend]
	mov	[i], rax
	jmp	.pair_loop

  .pass_done:
	mov	rdi, [base]
	mov	rsi, [scratch]
	mov	rcx, [count]
	imul	rcx, [stride]
	cld
	rep	movsb

	mov	rax, [width]
	add	rax, rax
	mov	[width], rax
	jmp	.level_loop

  .all_done:
	return
endp
