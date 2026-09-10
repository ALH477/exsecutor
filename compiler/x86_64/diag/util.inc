; compiler/x86_64/diag/util.inc
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
; Four small, independent primitives every diag/ renderer needs:
;
;   diag_lookup(numeric)                       -> index into codes.inc's tables
;   diag_fmt_u32(value, out_ptr, out_cap)      -> decimal ASCII
;   diag_line_col(src_ptr, src_len, offset)    -> 1-based line, 1-based BYTE column
;   diag_line_bounds(src_ptr, src_len, offset) -> that line's [start, end)
;
; A NOTE ON `col`, SINCE ONE RENDERER USED TO CONFLATE THIS WITH IT. The
; column `diag_line_col` returns is, and has always been, a 1-based BYTE
; offset within the line -- see the proc's own comment below. That is
; unchanged, and `diag_render_json` (render.inc) still reports exactly this
; value in its `"col"` member: JSON is the machine contract, `span.start`/
; `span.len` are byte offsets, and silently changing what a machine-readable
; field means is worse than a byte column a consumer has to decode.
;
; `diag_render_text`'s HEADER (`file:line:col`) is different as of the D4
; fix (`docs/design/diagnostics-review.md`): the caret run under the echoed
; source line is padded in terminal COLUMNS (escape.inc's
; `__diag_escape_core`, whose `rcx` already counts them for exactly this
; reason), and a header that still printed the byte column disagreed with
; its own caret -- `2:39` over a caret sitting at character 36. The text
; header's `col` is now a 1-based CHARACTER column, computed with the same
; classifier the caret uses (render.inc's `__diag_col_of`, a thin,
; non-committing wrapper over `__diag_escape_core` -- one classifier, no
; second implementation), not this proc's byte count. `diag_line_col` itself
; is unaffected and is still what the text renderer calls first, for the
; line number and for `diag_line_bounds`'s line start; it is simply no
; longer, on its own, "the" column a human reads in `file:line:col`.
;
; DIAG_LOOKUP'S NOT-FOUND PATH IS AN UNCONDITIONAL TRAP, NOT AN `rassert`,
; AND NOT COMPILED OUT UNDER RELEASE -- a deliberate departure from this
; codebase's usual rassert convention, reasoned through explicitly because
; the usual convention did not obviously fit:
;
;   `rassert` (macros/assert.inc) compiles to NOTHING under RELEASE, tokens
;   and all -- correct for a hot-path internal-consistency check where the
;   accepted tradeoff is "trust the debug build caught it." diag_lookup's
;   miss case is different in kind, not just frequency: a numeric code that
;   matches no §13 entry can only mean this compiler's OWN code passed a
;   code that was never registered (CLAUDE.md, "Never invent a code") --
;   and unlike (say) an out-of-bounds `vec_get`, there is no benign
;   "garbage but harmless" value to fall through to, because whatever
;   diag_lookup returns next feeds directly into a diagnostic's CODE and
;   MESSAGE text that gets shown to whoever is reading the compiler's
;   output. Falling through with a stale/undefined index in a RELEASE
;   build would mean the diagnostic renderer's output for this one case is
;   determined by whatever happened to be in a register -- content THIS
;   process did not choose landing in output whose entire job (§8.3) is to
;   be trustworthy and machine-matchable. That is a strictly worse failure
;   mode than a trap, so this path stays live in every build. It costs one
;   two-byte `ud2` on a path that -- if the rest of this compiler is
;   correct -- never executes.
;
; diag_fmt_u32's overflow path, by contrast, IS a plain `rassert` (compiled
; out under RELEASE like everywhere else) -- see that proc's own comment for
; why: unlike diag_escape's source-length-driven output (escape.inc's
; header explains that one at length), the values formatted here (line
; numbers, column numbers, span offsets) are u32 by construction
; (rt/span.inc) and the caller-supplied buffer size is this compiler's own
; bookkeeping, not attacker-controlled -- the ordinary "internal contract"
; case rassert exists for.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3 (diagnostics); rt/span.inc's
; own header ("A future compiler/x86_64/diag/ module can recover line/column
; from a byte offset by scanning the source text once per diagnostic") is
; the explicit brief for diag_line_col below.
; -----------------------------------------------------------------------------

include 'codes.inc'
include '../macros/proc.inc'
include '../macros/assert.inc'

; diag_lookup(numeric) -> eax = 0-based index i such that
; diag_code_numeric[i] == numeric. See this file's header for why a miss
; traps unconditionally instead of going through `rassert`.
proc diag_lookup, numeric
	locals
	endl
	xor	ecx, ecx		; ecx = i
	mov	edx, [numeric]
  .loop:
	cmp	ecx, DIAG_CODE_COUNT
	je	.notfound
	mov	eax, [diag_code_numeric + ecx*4]
	cmp	eax, edx
	je	.found
	inc	ecx
	jmp	.loop
  .found:
	mov	eax, ecx
	return
  .notfound:
	; Same raw encoding macros/assert.inc's `rassert` uses -- no symbolic
	; `ud2` mnemonic exists in vendor/fasmg-x86 (confirmed by that file's
	; own header). Unconditional: see this file's header for why this one
	; check is never compiled out.
	db	0x0F, 0x0B
endp

; diag_fmt_u32(value, out_ptr, out_cap) -> rax = bytes written. Decimal
; ASCII, no leading zeros ("0" for zero itself). `rassert`s if `out_cap` is
; insufficient -- see this file's header for why this one IS a plain
; rassert (caller-bookkeeping bound, not source-length-driven).
proc diag_fmt_u32, value, out_ptr, out_cap
	uses	rbx, r12, r13
	locals
		slot buf, 10		; 2^32-1 = 10 decimal digits, no NUL needed
	endl
	mov	eax, [value]
	lea	r12, [buf + 10]		; write backward from one past the end
	xor	r13, r13		; digit count
	mov	ebx, 10
  .divloop:
	xor	edx, edx
	div	ebx			; eax:=eax/10, edx:=eax mod 10
	add	dl, '0'
	dec	r12
	mov	[r12], dl
	inc	r13
	test	eax, eax
	jnz	.divloop
	; r12 = pointer to first digit, r13 = digit count (1..10)
	mov	rax, [out_cap]
	rassert	r13 le rax
	mov	rdi, [out_ptr]
	mov	rsi, r12
	mov	rcx, r13
	cld
	rep	movsb
	mov	rax, r13
	return
endp

; diag_line_col(src_ptr, src_len, offset) -> rax = 1-based line,
; rdx = 1-based column (a BYTE offset within the line -- see this file's
; header; consistent with rt/str.inc's "byte-oriented, not codepoint-aware"
; convention throughout this wave). This is exactly what `diag_render_json`
; reports as `"col"` (the machine contract; see this file's header); the
; text renderer's header column is a CHARACTER count computed separately
; (render.inc's `__diag_col_of`), not this `rdx`. `offset` past `src_len` is
; clamped, not rejected -- a one-past-the-end span endpoint (e.g. from
; `span_end`) is a legitimate, expected input, not a caller bug.
;
; O(offset): a full linear scan from byte 0 on every call. This is exactly
; the approach rt/span.inc's own header anticipated as acceptable for this
; wave ("scanning the source text once per diagnostic") rather than an
; oversight -- an amortized once-per-file line-start index is named there
; as later, optional work, not a requirement here.
proc diag_line_col, src_ptr, src_len, offset
	locals
	endl
	mov	rdi, [src_ptr]
	mov	rcx, [src_len]
	mov	rax, [offset]
	cmp	rax, rcx
	jbe	.clamped
	mov	rax, rcx
  .clamped:
	mov	r8, 1			; line
	xor	r9, r9			; byte index of the current line's start
	xor	r10, r10		; i
  .loop:
	cmp	r10, rax
	jge	.done
	movzx	edx, byte [rdi + r10]
	cmp	dl, 10			; '\n'
	jne	.notnl
	inc	r8
	lea	r9, [r10 + 1]
  .notnl:
	inc	r10
	jmp	.loop
  .done:
	mov	rdx, rax
	sub	rdx, r9
	inc	rdx			; 1-based column
	mov	rax, r8
	return
endp

; diag_line_bounds(src_ptr, src_len, off) -> rax = the byte offset at which
; the line containing `off` starts, rdx = the byte offset one past its last
; content byte (the position of its terminating '\n', or `src_len` for an
; unterminated final line -- the '\n' itself is NEVER included, so
; [rax, rdx) is exactly the text a renderer should echo).
;
; Split out from diag_line_col rather than folded into it because the two
; answers are wanted at different times and one of them is much more
; expensive to want: every diagnostic needs (line, column) for its header,
; but only a diagnostic that actually echoes source needs the line's extent.
; `off` past `src_len` is clamped, exactly as in diag_line_col and for the
; same reason (a one-past-the-end span endpoint is legitimate input).
;
; O(line length) backward + O(line length) forward from `off`, NOT a scan
; from byte 0 -- unlike diag_line_col, which has to count newlines from the
; start to know WHICH line it is on. Nothing here needs a line number, so
; nothing here pays for one.
proc diag_line_bounds, src_ptr, src_len, off
	locals
	endl
	mov	rdi, [src_ptr]
	mov	rcx, [src_len]
	mov	rax, [off]
	cmp	rax, rcx
	jbe	.clamped
	mov	rax, rcx
  .clamped:
	mov	r8, rax			; candidate line start
  .back:
	test	r8, r8
	jz	.back_done
	mov	r9, r8
	dec	r9
	cmp	byte [rdi + r9], 10	; '\n'
	je	.back_done
	mov	r8, r9
	jmp	.back
  .back_done:
	mov	r10, rax		; candidate line end
  .fwd:
	cmp	r10, rcx
	jge	.fwd_done
	cmp	byte [rdi + r10], 10
	je	.fwd_done
	inc	r10
	jmp	.fwd
  .fwd_done:
	mov	rax, r8
	mov	rdx, r10
	return
endp
