; compiler/x86_64/diag/escape.inc
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
; THE ONE ESCAPING PATH. Every byte of source text this compiler ever echoes
; into a diagnostic -- human-readable or machine-readable -- goes through
; `diag_escape`, with no exception for a "trusted" call site (spec §8.3:
; "All source echoed in a diagnostic is escaped. A diagnostic rendering raw
; bidi makes the error message the attack surface"). render.inc's JSON and
; text renderers both call this and nothing else for anything that came from
; source; do not add a second path that formats source bytes directly.
;
; WHAT GETS ESCAPED, and why this list and not a broader one:
;   - The exact §8.1 dangerous class -- U+202A-202E, U+2066-2069,
;     U+200B-200F, U+061C (bidi overrides/isolates and invisible controls) --
;     as `\u{XXXX}`, lowercase hex, always 4 digits. THIS is the
;     security-critical case this file exists for.
;   - C0 controls (0x00-0x1F) and DEL (0x7F): `\n`/`\t`/`\r` short forms,
;     `\u{00XX}` otherwise. Not named by §8.1, but a raw control byte (a
;     literal newline splitting a one-line diagnostic, for instance) breaks
;     the rendering format itself, independent of the security class above.
;   - C1 controls (0x80-0x9F): same reasoning as C0 -- rendering hygiene,
;     not the §8.1 class specifically.
;   - `"` and `\`: escaped (`\"`, `\\`) -- rendering hygiene again, and the
;     precondition the JSON mode below builds on.
;   - EVERYTHING ELSE -- including non-ASCII letters (Cyrillic, CJK,
;     combining marks, accented Latin, ...) -- passes through verbatim, byte
;     for byte. This is deliberate, not an oversight: EXS-E0105 (confusable
;     identifiers) exists specifically to show a user two glyphs that look
;     alike, and escaping every non-ASCII codepoint on principle would
;     defeat that diagnostic's entire point. §8.1 names a specific, narrow
;     dangerous class; this file escapes exactly that class plus the format-
;     safety cases above, not "anything unusual."
;
; TWO OUTPUT MODES, ONE CLASSIFIER. `diag_escape` renders the text form
; (`\u{202e}`); `diag_escape_json` renders the same thing safe to drop
; verbatim into a JSON string body. They are two thin wrappers over ONE
; `__diag_escape_core` -- deliberately, because §8.3's guarantee is that
; every echoed byte goes through the SAME escaping decision, and two
; independently maintained classifiers is exactly how a call site ends up
; being the one that forgot U+061C.
;
; A CORRECTION TO THIS FILE'S EARLIER CLAIM, recorded rather than quietly
; fixed (CLAUDE.md evidence discipline). An earlier revision of this header
; asserted that the text-mode output was "ALSO valid to embed directly
; inside a JSON string body with no second escaping pass." That is FALSE and
; was never run: JSON's only backslash escapes are `\"` `\\` `\/` `\b` `\f`
; `\n` `\r` `\t` and `\uXXXX` with exactly four hex digits and no braces, so
; a literal `\u{202e}` in a JSON string body is a parse error at the `{`,
; and `\x{ff}` is a parse error at the `x`. Caught by writing the JSON
; renderer against it. The fix is the mode flag, not a different escape
; alphabet: JSON mode emits exactly what text mode does with every escape's
; leading backslash DOUBLED, so `JSON.parse` of the field yields the
; text-mode rendering character for character.
;
; A SECOND CORRECTION, this one caught by RUNNING the first attempt rather
; than by reading it. That attempt implemented JSON mode as "prepend one
; backslash to the escape sequence," reasoning that every sequence starts
; with exactly one. Two do not: the text form of a source `\` is `\\` and of
; a source `"` is `\"`, each of which contains a SECOND character JSON also
; escapes. Prepending one backslash produced `\\\` and `\\"` -- three bytes
; where four are required -- and `json.loads` rejected the result 60 bytes
; later, at the next sequence, with a message pointing nowhere near the
; actual mistake. The rule is not "double the leading backslash"; it is the
; general one: JSON mode expands the built sequence byte by byte, doubling
; every `\` and quoting every `"`. That is correct for the two sequences the
; shortcut got wrong, correct for the six it got right, and correct for any
; sequence added later -- which the shortcut would not have been.
;
; Passthrough bytes are never expanded in either mode: a passthrough byte is
; by construction neither `\` nor `"` (both have their own branches above),
; so passthrough output is already JSON-safe.
;
; INVALID UTF-8 IS ESCAPED TOO, byte-by-byte, as `\x{HH}`. This matters
; because EXS-E0101 ("source not UTF-8, or BOM present") exists precisely
; because source CAN be invalid -- a diagnostic about broken encoding must
; still be able to safely echo the broken bytes. `__diag_utf8_decode` below
; decodes STRICTLY: it rejects overlong encodings, surrogate halves
; (U+D800-U+DFFF, never valid in real UTF-8), out-of-range codepoints
; (>U+10FFFF), and truncated trailing sequences. This is a safety property,
; not pedantry: a lenient decoder that accepts overlong encodings could in
; principle be fed a non-shortest-form encoding of a dangerous codepoint
; that a naive byte-level scan for the "canonical" encoding would miss.
; Because this decoder instead treats "not a strictly valid encoding of
; something" as its OWN invalid case (hex-escaped regardless of what it
; might have decoded to under a looser reading), the safety property holds
; either way: a correctly-encoded dangerous codepoint is caught by the
; codepoint check, and anything not correctly encoded is caught by the
; strict-decode check. Whitelist posture throughout, not a blocklist one.
;
; GRACEFUL TRUNCATION ON OVERFLOW, NOT `rassert`. This is the one place in
; this file's design that deliberately differs from rt/'s usual "rassert if
; the caller under-sized a buffer" convention (see util.inc's header for the
; contrasting case, diag_fmt_u32, and why THAT one stays a plain rassert).
; The reason: the amount of OUTPUT this function needs is a function of
; SOURCE CONTENT length, and source content is exactly what an adversarial
; input controls (a single "line" with no newline for megabytes, a huge
; string literal, ...). An `rassert`-trapping compiler would hand an
; attacker a one-line reliable way to crash `exsc` merely by writing source
; that produces a diagnostic -- the "diagnostic becomes the attack surface"
; failure mode §8.3 warns about, one level removed (denial of service
; instead of a rendering exploit, but still the diagnostic path itself being
; the weakness). So `diag_escape` cannot be made to overrun `out_cap` no
; matter what it is fed: on overflow it stops, leaves `rdx` = 1 (truncated),
; and returns whatever prefix it already wrote. No truncation marker is
; appended -- `rdx` is the caller's signal, not a magic string a caller
; would need to detect by scanning.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1, §8.3
; -----------------------------------------------------------------------------

include 'util.inc'

DIAG_CP_INVALID = 0xFFFFFFFF

; __diag_utf8_decode(ptr, remaining) -> rax = codepoint (0..0x10FFFF), or
; DIAG_CP_INVALID; rdx = bytes consumed (ALWAYS >= 1, even on an invalid
; byte -- a caller can therefore always advance by rdx without risking an
; infinite loop). Private: escape.inc's own implementation detail.
;
; Only two caller-saved registers hold anything across sub-branches here
; that isn't reloaded fresh (rsi = ptr, r9 = remaining); everything else is
; scratch. No `uses` needed -- every register this touches is caller-saved.
proc __diag_utf8_decode, ptr, remaining
	locals
	endl
	mov	rsi, [ptr]
	mov	r9, [remaining]
	test	r9, r9
	jz	.invalid0
	movzx	eax, byte [rsi]		; b0
	cmp	eax, 0x80
	jb	.ascii
	cmp	eax, 0xC0
	jb	.invalid1		; 0x80-0xBF: stray continuation byte
	cmp	eax, 0xE0
	jb	.lead2
	cmp	eax, 0xF0
	jb	.lead3
	cmp	eax, 0xF8
	jb	.lead4
	jmp	.invalid1		; 0xF8-0xFF: never valid in RFC 3629 UTF-8

  .ascii:
	mov	rdx, 1
	return				; rax already = b0 (0..0x7F)

  .lead2:
	cmp	r9, 2
	jb	.invalid1
	movzx	ecx, byte [rsi+1]	; b1
	mov	r8d, ecx
	and	r8d, 0xC0
	cmp	r8d, 0x80
	jne	.invalid1
	and	eax, 0x1F
	shl	eax, 6
	and	ecx, 0x3F
	or	eax, ecx
	cmp	eax, 0x80
	jb	.invalid1		; overlong
	mov	rdx, 2
	return

  .lead3:
	cmp	r9, 3
	jb	.invalid1
	movzx	ecx, byte [rsi+1]	; b1
	movzx	r8d, byte [rsi+2]	; b2
	mov	r10d, ecx
	and	r10d, 0xC0
	cmp	r10d, 0x80
	jne	.invalid1
	mov	r10d, r8d
	and	r10d, 0xC0
	cmp	r10d, 0x80
	jne	.invalid1
	and	eax, 0x0F
	shl	eax, 12
	mov	r10d, ecx
	and	r10d, 0x3F
	shl	r10d, 6
	or	eax, r10d
	mov	r10d, r8d
	and	r10d, 0x3F
	or	eax, r10d
	cmp	eax, 0x800
	jb	.invalid1		; overlong
	cmp	eax, 0xD800
	jb	.lead3_ok
	cmp	eax, 0xDFFF
	ja	.lead3_ok
	jmp	.invalid1		; surrogate half -- never valid in UTF-8
  .lead3_ok:
	mov	rdx, 3
	return

  .lead4:
	cmp	r9, 4
	jb	.invalid1
	movzx	ecx, byte [rsi+1]	; b1
	movzx	r8d, byte [rsi+2]	; b2
	movzx	r10d, byte [rsi+3]	; b3
	mov	r11d, ecx
	and	r11d, 0xC0
	cmp	r11d, 0x80
	jne	.invalid1
	mov	r11d, r8d
	and	r11d, 0xC0
	cmp	r11d, 0x80
	jne	.invalid1
	mov	r11d, r10d
	and	r11d, 0xC0
	cmp	r11d, 0x80
	jne	.invalid1
	and	eax, 0x07
	shl	eax, 18
	mov	r11d, ecx
	and	r11d, 0x3F
	shl	r11d, 12
	or	eax, r11d
	mov	r11d, r8d
	and	r11d, 0x3F
	shl	r11d, 6
	or	eax, r11d
	mov	r11d, r10d
	and	r11d, 0x3F
	or	eax, r11d
	cmp	eax, 0x10000
	jb	.invalid1		; overlong
	cmp	eax, 0x10FFFF
	ja	.invalid1		; out of range
	mov	rdx, 4
	return

  .invalid0:
	mov	eax, DIAG_CP_INVALID
	xor	rdx, rdx		; 0 bytes available AND 0 consumed
	return

  .invalid1:
	mov	eax, DIAG_CP_INVALID
	mov	rdx, 1
	return
endp

; diag_is_dangerous_cp(cp) -> eax = 1 if `cp` is in spec §8.1's dangerous
; class (bidi overrides/isolates, invisible controls), else 0. PUBLIC and
; reusable on purpose: the lexer will need this exact same class to raise
; EXS-E0103 in the first place (this proc only classifies; it does not
; detect-and-report). Keeping the list in exactly one place means the lexer
; and this renderer cannot silently drift apart on what counts as
; dangerous -- when the lexer is built, prefer including this file and
; calling this proc over re-deriving the ranges.
proc diag_is_dangerous_cp, cp
	locals
	endl
	mov	eax, [cp]
	cmp	eax, 0x202A
	jb	.not_range1
	cmp	eax, 0x202E
	ja	.not_range1
	mov	eax, 1
	return
  .not_range1:
	cmp	eax, 0x2066
	jb	.not_range2
	cmp	eax, 0x2069
	ja	.not_range2
	mov	eax, 1
	return
  .not_range2:
	cmp	eax, 0x200B
	jb	.not_range3
	cmp	eax, 0x200F
	ja	.not_range3
	mov	eax, 1
	return
  .not_range3:
	cmp	eax, 0x061C
	jne	.no
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

diag_hexdigits db '0123456789abcdef'

; __diag_build_x2(buf, value) -- writes the fixed 6-byte sequence
; `\x{HH}` (value's low BYTE, 2 lowercase hex digits) to `buf`.
proc __diag_build_x2, buf, value
	locals
	endl
	mov	rdi, [buf]
	mov	byte [rdi+0], 0x5C	; '\'
	mov	byte [rdi+1], 'x'
	mov	byte [rdi+2], '{'
	mov	eax, [value]
	mov	ecx, eax
	shr	ecx, 4
	and	ecx, 0xF
	movzx	edx, byte [diag_hexdigits + ecx]
	mov	[rdi+3], dl
	mov	ecx, eax
	and	ecx, 0xF
	movzx	edx, byte [diag_hexdigits + ecx]
	mov	[rdi+4], dl
	mov	byte [rdi+5], '}'
	return
endp

; __diag_build_u4(buf, value) -- writes the fixed 8-byte sequence
; `\u{XXXX}` (value's low 16 bits, 4 lowercase hex digits) to `buf`. Every
; codepoint this file ever escapes this way (the §8.1 class tops out at
; U+2069; C0/C1/DEL top out at U+00FF) fits in 16 bits, so a fixed 4-digit
; width needs no variable-width logic.
proc __diag_build_u4, buf, value
	locals
	endl
	mov	rdi, [buf]
	mov	byte [rdi+0], 0x5C	; '\'
	mov	byte [rdi+1], 'u'
	mov	byte [rdi+2], '{'
	mov	eax, [value]
	mov	ecx, eax
	shr	ecx, 12
	and	ecx, 0xF
	movzx	edx, byte [diag_hexdigits + ecx]
	mov	[rdi+3], dl
	mov	ecx, eax
	shr	ecx, 8
	and	ecx, 0xF
	movzx	edx, byte [diag_hexdigits + ecx]
	mov	[rdi+4], dl
	mov	ecx, eax
	shr	ecx, 4
	and	ecx, 0xF
	movzx	edx, byte [diag_hexdigits + ecx]
	mov	[rdi+5], dl
	mov	ecx, eax
	and	ecx, 0xF
	movzx	edx, byte [diag_hexdigits + ecx]
	mov	[rdi+6], dl
	mov	byte [rdi+7], '}'
	return
endp

; __diag_append(out_ptr, out_cap, o_ptr, src_ptr, n) -> eax = 1 and
; *o_ptr += n if the n bytes at src_ptr fit in [out_ptr+*o_ptr, out_ptr+
; out_cap); eax = 0 and *o_ptr UNCHANGED otherwise. `o_ptr` is a pointer to
; the caller's running-length local (not the value itself), so this proc
; can update it in place without needing a second return register.
proc __diag_append, out_ptr, out_cap, o_ptr, src_ptr, n
	locals
	endl
	mov	r8, [o_ptr]
	mov	r8, [r8]		; current *o_ptr
	mov	r9, [n]
	mov	rax, r8
	add	rax, r9			; candidate new length
	cmp	rax, [out_cap]
	ja	.no_fit
	mov	rdi, [out_ptr]
	add	rdi, r8
	mov	rsi, [src_ptr]
	mov	rcx, r9
	cld
	rep	movsb
	mov	rdx, [o_ptr]
	mov	[rdx], rax
	mov	eax, 1
	return
  .no_fit:
	xor	eax, eax
	return
endp

DIAG_ESC_TEXT = 0
DIAG_ESC_JSON = 1

; __diag_escape_core(src_ptr, src_len, out_ptr, out_cap, mode) -> rax = bytes
; written to out_ptr, rdx = 1 if truncated (out_cap was insufficient for the
; full escaped result -- see this file's header for why truncation, not a
; trap), 0 if the entire input was consumed, rcx = the number of TERMINAL
; COLUMNS those bytes occupy. `mode` is DIAG_ESC_TEXT or DIAG_ESC_JSON.
;
; RCX IS WHY THE CARET LANDS ON THE RIGHT CHARACTER. `rax` counts bytes and
; `rcx` counts columns, and they differ for exactly one class of input: a
; passthrough codepoint outside ASCII, which is 2-4 bytes and 1 column. Every
; escape SEQUENCE is ASCII, so for those two the counts coincide -- which is
; why the original code, which used the byte count for both, was correct on
; `\u{202e}` and wrong on `pira\u{00f1}a`. `docs/design/diagnostics-review.md`
; D4 measured the wrong half: a caret one column right per multibyte
; character before it, on ordinary Latin identifiers, in a language whose §8.1
; policy exists to admit them.
;
; In JSON mode `rcx` is computed the same way and means nothing, because a
; JSON member is not a terminal; the caller that wants it is the text
; renderer, and the caller that does not simply ignores the register.
;
; WHAT IT STILL DOES NOT COUNT: East Asian width (a wide character is two
; terminal columns and is counted here as one) and combining marks (zero
; columns, counted as one). Both are `[UNTESTED]` and both are named as open
; in the review's own "what I could not evaluate" list. §8.4's keywords and
; §3's lexicon are Latin, so neither shape occurs in a keyword or a fix; they
; occur only in a user's own identifiers and string literals.
;
; The mode is applied at exactly ONE point (`.emit_seq`, below) and only to
; escape SEQUENCES, never to passthrough bytes: every branch builds its
; text-mode sequence in `hexbuf` and jumps to `.emit_seq`, which either
; sends it as-is or expands it into `jsonbuf`. Nothing else in this routine
; knows a mode exists -- which is the point: the classification (dangerous /
; control / quote / backslash / invalid / passthrough) has exactly one
; implementation serving both consumers.
proc __diag_escape_core, src_ptr, src_len, out_ptr, out_cap, mode
	uses	rbx, r12, r13, r14
	locals
		slot o,         dq
		slot hexbuf,    16	; the text-mode sequence, max 8 bytes
		slot jsonbuf,   24	; its JSON expansion, max 2x
		slot emit_len,  dd
		slot cp,        dd
		slot consumed,  dd
		slot truncated, dd
		slot cols,      dq
		slot colinc,    dd
	endl
	mov	rbx, [src_ptr]
	mov	r12, [src_len]
	xor	r13, r13		; i = 0
	mov	qword [o], 0
	mov	qword [cols], 0

  .loop:
	cmp	r13, r12
	jge	.done_full

	lea	rdi, [rbx + r13]
	mov	rsi, r12
	sub	rsi, r13
	call	__diag_utf8_decode	; rax = cp, rdx = consumed
	mov	[cp], eax
	mov	[consumed], edx

	cmp	dword [cp], DIAG_CP_INVALID
	jne	.not_invalid_byte
	jmp	.emit_hex_byte

  .not_invalid_byte:
	mov	edi, [cp]
	call	diag_is_dangerous_cp
	test	eax, eax
	jnz	.emit_hex_cp4

	mov	eax, [cp]
	cmp	eax, 0x20
	jae	.not_c0
	cmp	eax, 10
	je	.emit_lf
	cmp	eax, 9
	je	.emit_tab
	cmp	eax, 13
	je	.emit_cr
	jmp	.emit_hex_cp4		; other C0 control

  .not_c0:
	cmp	eax, 0x22
	je	.emit_quote
	cmp	eax, 0x5C
	je	.emit_backslash
	cmp	eax, 0x7F
	je	.emit_hex_cp4		; DEL
	cmp	eax, 0x80
	jb	.passthrough
	cmp	eax, 0x9F
	ja	.passthrough
	jmp	.emit_hex_cp4		; C1 control

  .passthrough:
	lea	r14, [rbx + r13]
	mov	eax, [consumed]
	mov	[emit_len], eax
	mov	dword [colinc], 1	; ONE codepoint, ONE column -- see the
					; `cols` note in this file's header
	jmp	.do_append

  .emit_quote:
	mov	byte [hexbuf+0], 0x5C
	mov	byte [hexbuf+1], 0x22
	mov	dword [emit_len], 2
	jmp	.emit_seq

  .emit_backslash:
	mov	byte [hexbuf+0], 0x5C
	mov	byte [hexbuf+1], 0x5C
	mov	dword [emit_len], 2
	jmp	.emit_seq

  .emit_lf:
	mov	byte [hexbuf+0], 0x5C
	mov	byte [hexbuf+1], 'n'
	mov	dword [emit_len], 2
	jmp	.emit_seq

  .emit_tab:
	mov	byte [hexbuf+0], 0x5C
	mov	byte [hexbuf+1], 't'
	mov	dword [emit_len], 2
	jmp	.emit_seq

  .emit_cr:
	mov	byte [hexbuf+0], 0x5C
	mov	byte [hexbuf+1], 'r'
	mov	dword [emit_len], 2
	jmp	.emit_seq

  .emit_hex_byte:
	movzx	eax, byte [rbx + r13]
	lea	rdi, [hexbuf]
	mov	esi, eax
	call	__diag_build_x2
	mov	dword [emit_len], 6
	jmp	.emit_seq

  .emit_hex_cp4:
	mov	eax, [cp]
	lea	rdi, [hexbuf]
	mov	esi, eax
	call	__diag_build_u4
	mov	dword [emit_len], 8
	jmp	.emit_seq

	; The ONE place the mode is consulted, and the only code in this routine
	; that knows a mode exists. Text mode sends the sequence exactly as
	; built. JSON mode expands it byte by byte -- `\` -> `\\`, `"` -> `\"`,
	; everything else copied -- which is the general rule, not the
	; leading-backslash shortcut that got `\\` and `\"` wrong (this file's
	; header records that in full).
  .emit_seq:
	; The column width of an escape sequence is the width of its TEXT-mode
	; form, taken here before the JSON expansion below rewrites `emit_len`.
	; Every byte of that form is ASCII, so bytes and columns coincide.
	mov	eax, [emit_len]
	mov	[colinc], eax
	cmp	qword [mode], DIAG_ESC_TEXT
	jne	.emit_seq_json
	lea	r14, [hexbuf]
	jmp	.do_append
  .emit_seq_json:
	xor	ecx, ecx		; read index into hexbuf
	xor	edx, edx		; write index into jsonbuf
  .esj_loop:
	cmp	ecx, [emit_len]
	jge	.esj_done
	movzx	eax, byte [hexbuf + rcx]
	cmp	eax, 0x5C
	je	.esj_escape
	cmp	eax, 0x22
	je	.esj_escape
	mov	[jsonbuf + rdx], al
	inc	edx
	jmp	.esj_next
  .esj_escape:
	mov	byte [jsonbuf + rdx], 0x5C
	inc	edx
	mov	[jsonbuf + rdx], al
	inc	edx
  .esj_next:
	inc	ecx
	jmp	.esj_loop
  .esj_done:
	lea	r14, [jsonbuf]
	mov	[emit_len], edx
	jmp	.do_append

  .do_append:
	mov	rdi, [out_ptr]
	mov	rsi, [out_cap]
	lea	rdx, [o]
	mov	rcx, r14
	mov	r8d, dword [emit_len]	; NOT `movzx r8, dword [...]` -- there is no
					; movzx r64, r/m32 encoding (fasmg: "invalid
					; operand size"); a 32-bit `mov` already
					; zero-extends into the full 64-bit register.
	call	__diag_append
	test	eax, eax
	jz	.overflow
	mov	eax, [colinc]
	add	[cols], rax
	mov	eax, [consumed]
	add	r13, rax
	jmp	.loop

  .overflow:
	mov	dword [truncated], 1
	jmp	.finish

  .done_full:
	mov	dword [truncated], 0

  .finish:
	mov	rax, [o]
	mov	edx, [truncated]
	mov	rcx, [cols]
	return
endp

; diag_escape(src_ptr, src_len, out_ptr, out_cap) -> rax = bytes written,
; rdx = 1 if truncated, rcx = TERMINAL COLUMNS written. THE escaping entry point for human-readable
; rendering. See this file's header: every code path that quotes user
; source into a rendered diagnostic goes through this or its JSON sibling,
; with no exception for a "trusted" call site.
proc diag_escape, src_ptr, src_len, out_ptr, out_cap
	locals
	endl
	mov	rdi, [src_ptr]
	mov	rsi, [src_len]
	mov	rdx, [out_ptr]
	mov	rcx, [out_cap]
	mov	r8d, DIAG_ESC_TEXT
	call	__diag_escape_core
	return
endp

; diag_escape_json(src_ptr, src_len, out_ptr, out_cap) -> rax = bytes
; written, rdx = 1 if truncated, rcx = columns (meaningless in this mode; see
; `__diag_escape_core`). Identical classification to `diag_escape`;
; the result is a JSON string BODY (the bytes that go between the quotes),
; so `JSON.parse` of it yields exactly what `diag_escape` would have
; produced. Emits no surrounding quotes -- the caller owns those.
proc diag_escape_json, src_ptr, src_len, out_ptr, out_cap
	locals
	endl
	mov	rdi, [src_ptr]
	mov	rsi, [src_len]
	mov	rdx, [out_ptr]
	mov	rcx, [out_cap]
	mov	r8d, DIAG_ESC_JSON
	call	__diag_escape_core
	return
endp
