; compiler/x86_64/diag/render.inc
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
; The diagnostic record and its two renderers. §8.3.
;
; TWO OUTPUT MODES, ONE RECORD.
;   diag_render_text -- for a person reading a terminal.
;   diag_render_json -- for a tool. Stable, code-keyed, machine-readable.
; Both read the same `Diag`. Nothing downstream ever has to regex the
; English: `code` ("EXS-E0421") and `code_num` (421) are the join keys, and
; §8.3's "codes are permanent; text is not" is exactly the promise that makes
; matching on them safe and matching on `message` not. The English message is
; carried in the JSON as a convenience for humans reading raw output; a
; consumer that branches on it is using the one field the spec explicitly
; says may change.
;
; This is also why translation, if it ever happens, lands as a lookup keyed
; on the code and touches nothing here: no format string in this file
; interpolates anything into the message text, so there is no formatting site
; to entangle a translation with. §8.3 cites Rust's Fluent effort stalling on
; 400+ such sites as the thing not to repeat.
;
; EVERY BYTE THAT CAME FROM OUTSIDE THIS COMPILER IS ESCAPED, in both modes,
; through escape.inc and nothing else. That includes the three places it is
; tempting to skip:
;   - the SOURCE LINE and the SPAN's own bytes -- the obvious case, and the
;     one §8.3 is written about;
;   - the FILE PATH -- a path is not source, but it is not ours either: it
;     arrived on a command line or out of a directory, and a file literally
;     named with a U+202E in it renders a diagnostic header that lies about
;     which file failed;
;   - the FIX's replacement TEXT -- this one IS compiler-authored, and it is
;     still escaped, because "escape everything echoed" is a rule a reviewer
;     can check by grep and "escape everything except what we decided was
;     ours" is not.
; The canonical English message is escaped too. It is pure ASCII today (see
; codes.inc), so escaping is a no-op on it -- which is the point: it costs
; nothing and it means there is no un-escaped path through this file at all.
;
; RENDERING INTO A CALLER'S BUFFER, NOT TO A FILE DESCRIPTOR. Both renderers
; fill a caller-supplied buffer and return a length; `diag_emit` is a thin
; wrapper that hands that buffer to rt/sys.inc's `sys_write`. Keeping the
; syscall out of the renderers is what lets a fixture check exact output
; bytes without a pipe, and keeps this module's syscall surface to the single
; `write(1)` the allowlist already carries (docs/asm-conventions.md, "6.
; Syscall discipline").
;
; TRUNCATION, NEVER OVERRUN. `DiagOut` carries `cap`, `len` and a sticky
; `trunc` flag; every append checks and gives up rather than writing past the
; end, and the fill loops stop as soon as an append is refused. escape.inc's
; header argues this at length for the escaper; the same argument applies
; here for the same reason -- diagnostic output length is a function of
; source content, and source content is what an adversarial input controls.
; A compiler that can be made to crash by feeding it source that produces a
; diagnostic has made the diagnostic path the attack surface, which is the
; failure §8.3 exists to prevent.
;
; DETERMINISM. Nothing here orders anything by a pointer value or a hash
; (CLAUDE.md). Field order in both renderers is written out literally, one
; statement per field, so the output byte sequence is a property of this
; source file rather than of an iteration order -- the same reasoning
; codes.inc's header gives for keeping §13's table order.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3; rt/span.inc for `Span`.
; -----------------------------------------------------------------------------

include 'fix.inc'

DIAG_MODE_TEXT = 0
DIAG_MODE_JSON = 1

; ---- the output cursor -----------------------------------------------------
; A bounded, append-only cursor over a caller-owned buffer. `trunc` is sticky:
; once an append has been refused, every later append may still succeed (a
; short one can still fit), but the flag stays set, so a caller learns "this
; output is incomplete" from one bit rather than by comparing lengths.
struct DiagOut
	field ptr,   dq
	field cap,   dq
	field len,   dq
	field trunc, dd
	field pad0,  dd
end struct

; ---- the diagnostic record -------------------------------------------------
; 96 bytes. The `Span` is rt/span.inc's, embedded by value, not a re-spelling
; of it: §8.3 makes the span non-optional ("not retrofittable, which is why
; the CST is not optional"), and the CST will hand these out as `Span`s.
; `src_ptr`/`src_len` are the file's text, needed to turn a byte offset into
; a line, a column and an echoed line; `path_ptr`/`path_len` are its name.
; Both may be zero -- a diagnostic with no source attached still renders its
; header, its code and its fix, just without a snippet.
struct Diag
	field code_num, dd
	field pad0,     dd
	span Span
	field pad1,     dd
	field src_ptr,  dq
	field src_len,  dq
	field path_ptr, dq
	field path_len, dq
	fix DiagFix
end struct

; ---- literal fragments -----------------------------------------------------
; Written out as data with explicit lengths rather than built at runtime.
; Deliberately NOT reached by execution: each sits between two `proc` bodies,
; and every `proc` leaves through `endp`'s `ret`, so control never falls into
; them (escape.inc's `diag_hexdigits` is placed the same way).
diag_lit_colon   db ':'
DIAG_LIT_COLON_LEN   = $ - diag_lit_colon
diag_lit_errbr   db ': error['
DIAG_LIT_ERRBR_LEN   = $ - diag_lit_errbr
diag_lit_brcolon db ']: '
DIAG_LIT_BRCOLON_LEN = $ - diag_lit_brcolon
diag_lit_nl      db 10
DIAG_LIT_NL_LEN      = $ - diag_lit_nl
diag_lit_bar     db ' | '
DIAG_LIT_BAR_LEN     = $ - diag_lit_bar
diag_lit_eq      db ' = '
DIAG_LIT_EQ_LEN      = $ - diag_lit_eq
diag_lit_fixopen db 'fix ('
DIAG_LIT_FIXOPEN_LEN = $ - diag_lit_fixopen
diag_lit_fixmid  db '): bytes '
DIAG_LIT_FIXMID_LEN  = $ - diag_lit_fixmid
diag_lit_dotdot  db '..'
DIAG_LIT_DOTDOT_LEN  = $ - diag_lit_dotdot
diag_lit_arrow   db ' -> "'
DIAG_LIT_ARROW_LEN   = $ - diag_lit_arrow
diag_lit_dquote  db '"'
DIAG_LIT_DQUOTE_LEN  = $ - diag_lit_dquote

diag_j_open      db '{"code":"'
DIAG_J_OPEN_LEN      = $ - diag_j_open
diag_j_num       db '","code_num":'
DIAG_J_NUM_LEN       = $ - diag_j_num
diag_j_file      db ',"file":"'
DIAG_J_FILE_LEN      = $ - diag_j_file
diag_j_line      db '","line":'
DIAG_J_LINE_LEN      = $ - diag_j_line
diag_j_col       db ',"col":'
DIAG_J_COL_LEN       = $ - diag_j_col
diag_j_spanfid   db ',"span":{"file_id":'
DIAG_J_SPANFID_LEN   = $ - diag_j_spanfid
diag_j_start     db ',"start":'
DIAG_J_START_LEN     = $ - diag_j_start
diag_j_len       db ',"len":'
DIAG_J_LEN_LEN       = $ - diag_j_len
diag_j_msg       db '},"message":"'
DIAG_J_MSG_LEN       = $ - diag_j_msg
diag_j_snip      db '","snippet":"'
DIAG_J_SNIP_LEN      = $ - diag_j_snip
diag_j_fixnull   db '","fix":null}'
DIAG_J_FIXNULL_LEN   = $ - diag_j_fixnull
diag_j_fixopen   db '","fix":{"kind":"'
DIAG_J_FIXOPEN_LEN   = $ - diag_j_fixopen
diag_j_fixfid    db '","file_id":'
DIAG_J_FIXFID_LEN    = $ - diag_j_fixfid
diag_j_fixtext   db ',"text":"'
DIAG_J_FIXTEXT_LEN   = $ - diag_j_fixtext
diag_j_close     db '"}}'
DIAG_J_CLOSE_LEN     = $ - diag_j_close

; ---- output cursor operations ----------------------------------------------

; diag_out_init(wr, buf, cap) -- point a `DiagOut` at a caller-owned buffer.
proc diag_out_init, wr, buf, cap
	locals
	endl
	mov	rdi, [wr]
	mov	rax, [buf]
	mov	[rdi + DiagOut.ptr], rax
	mov	rax, [cap]
	mov	[rdi + DiagOut.cap], rax
	mov	qword [rdi + DiagOut.len], 0
	mov	dword [rdi + DiagOut.trunc], 0
	mov	dword [rdi + DiagOut.pad0], 0
	return
endp

; diag_out_bytes(wr, src, n) -> eax = 1 if the n bytes fit and were appended,
; 0 if they did not (in which case NOTHING was appended and `trunc` is set).
; All-or-nothing on purpose: a half-written multi-byte token in the output is
; worse than a missing one, especially in the JSON mode where it would also
; be a syntax error rather than a visibly short line.
proc diag_out_bytes, wr, src, n
	uses	rbx
	locals
	endl
	mov	rbx, [wr]
	mov	rax, [rbx + DiagOut.len]
	add	rax, [n]
	cmp	rax, [rbx + DiagOut.cap]
	ja	.no_fit
	mov	rdi, [rbx + DiagOut.ptr]
	add	rdi, [rbx + DiagOut.len]
	mov	rsi, [src]
	mov	rcx, [n]
	cld
	rep	movsb
	mov	[rbx + DiagOut.len], rax
	mov	eax, 1
	return
  .no_fit:
	mov	dword [rbx + DiagOut.trunc], 1
	xor	eax, eax
	return
endp

; __diag_out_esc_mode(wr, src, n, mode) -> rax = bytes actually appended.
; The single point at which this file reaches escape.inc; both public
; wrappers below go through it, so there is exactly one place where a call
; could have forgotten to escape, and it is this one.
;
; Unlike diag_out_bytes this is NOT all-or-nothing: diag_escape fills what it
; can and reports truncation, and a partially-escaped tail is fine for the
; human mode (the line is visibly cut) and, in the JSON mode, is still inside
; a string body whose closing quote the caller will fail to fit as well --
; either way `trunc` is set and the caller knows the output is incomplete.
proc __diag_out_esc_mode, wr, src, n, mode
	uses	rbx
	locals
	endl
	mov	rbx, [wr]
	mov	rdi, [src]
	mov	rsi, [n]
	mov	rdx, [rbx + DiagOut.ptr]
	add	rdx, [rbx + DiagOut.len]
	mov	rcx, [rbx + DiagOut.cap]
	sub	rcx, [rbx + DiagOut.len]
	mov	r8, [mode]
	call	__diag_escape_core	; rax = written, rdx = truncated
	add	[rbx + DiagOut.len], rax
	test	edx, edx
	jz	.ok
	mov	dword [rbx + DiagOut.trunc], 1
  .ok:
	return
endp

; diag_out_esc(wr, src, n) -> rax = bytes appended. Human-readable escaping.
proc diag_out_esc, wr, src, n
	locals
	endl
	mov	rdi, [wr]
	mov	rsi, [src]
	mov	rdx, [n]
	mov	rcx, DIAG_ESC_TEXT
	call	__diag_out_esc_mode
	return
endp

; diag_out_jstr(wr, src, n) -> rax = bytes appended. JSON string BODY; the
; caller owns the surrounding quotes.
proc diag_out_jstr, wr, src, n
	locals
	endl
	mov	rdi, [wr]
	mov	rsi, [src]
	mov	rdx, [n]
	mov	rcx, DIAG_ESC_JSON
	call	__diag_out_esc_mode
	return
endp

; diag_out_u32(wr, value) -- append `value` as decimal ASCII.
proc diag_out_u32, wr, value
	uses	rbx
	locals
		slot numbuf, 12
	endl
	mov	rbx, [wr]
	mov	edi, [value]
	lea	rsi, [numbuf]
	mov	rdx, 12
	call	diag_fmt_u32		; rax = digits written
	mov	rdi, rbx
	lea	rsi, [numbuf]
	mov	rdx, rax
	call	diag_out_bytes
	return
endp

; diag_out_fill(wr, value, n) -- append `value`'s low byte `n` times. Stops
; early the moment an append is refused, so an adversarially long line costs
; O(buffer), not O(line), once the buffer is full.
proc diag_out_fill, wr, value, n
	uses	rbx, r12
	locals
		slot fillbyte, dd
	endl
	mov	rbx, [wr]
	mov	r12, [n]
	mov	eax, [value]
	mov	[fillbyte], eax
  .loop:
	test	r12, r12
	jz	.done
	mov	rdi, rbx
	lea	rsi, [fillbyte]
	mov	rdx, 1
	call	diag_out_bytes
	test	eax, eax
	jz	.done
	dec	r12
	jmp	.loop
  .done:
	return
endp

; ---- diagnostic construction -----------------------------------------------

; diag_init(diagrec, code_num, file_id, off, len) -- a diagnostic with a span, no
; source attached and no fix. `code_num` is the numeric part of the EXS-E
; code (docs/asm-conventions.md, "1.3 Error protocol: CF / eax" -- the same
; value the protocol puts in `eax`), NOT the full string: §13's registry is
; the only place a code's text comes from, and codes.inc is generated from it.
proc diag_init, diagrec, code_num, file_id, off, len
	uses	rbx
	locals
	endl
	mov	rbx, [diagrec]
	mov	eax, [code_num]
	mov	[rbx + Diag.code_num], eax
	mov	dword [rbx + Diag.pad0], 0
	mov	eax, [file_id]
	mov	[rbx + Diag.span.file_id], eax
	mov	eax, [off]
	mov	[rbx + Diag.span.start], eax
	mov	eax, [len]
	mov	[rbx + Diag.span.len], eax
	mov	dword [rbx + Diag.pad1], 0
	mov	qword [rbx + Diag.src_ptr], 0
	mov	qword [rbx + Diag.src_len], 0
	mov	qword [rbx + Diag.path_ptr], 0
	mov	qword [rbx + Diag.path_len], 0
	lea	rdi, [rbx + Diag.fix]
	call	diag_fix_clear
	return
endp

; diag_attach_source(diagrec, src, slen, path, plen) -- the text the span indexes
; into and the name of the file it came from. Both are borrowed, not copied:
; `exsc` owns one text buffer per input file for the whole compilation
; (rt/span.inc's header), so a diagnostic pointing into it is a pointer, not
; an allocation.
proc diag_attach_source, diagrec, src, slen, path, plen
	uses	rbx
	locals
	endl
	mov	rbx, [diagrec]
	mov	rax, [src]
	mov	[rbx + Diag.src_ptr], rax
	mov	rax, [slen]
	mov	[rbx + Diag.src_len], rax
	mov	rax, [path]
	mov	[rbx + Diag.path_ptr], rax
	mov	rax, [plen]
	mov	[rbx + Diag.path_len], rax
	return
endp

; diag_fix_ptr(diagrec) -> rax = pointer to this diagnostic's embedded `DiagFix`,
; for handing to fix.inc's constructors.
proc diag_fix_ptr, diagrec
	locals
	endl
	mov	rax, [diagrec]
	add	rax, Diag.fix
	return
endp

; ---- human-readable rendering ----------------------------------------------
; Format, by example (a `|` gutter, rustc-shaped, because it is the shape
; people already read). This is REAL output, copied from a run of
; tests/unit/diag_render_text.asm's case A, which pins it byte for byte --
; not a sketch:
;
;   exempl\u{202e}um.exs:2:9: error[EXS-E0421]: undeclared capability (atom or row)
;    2 |     sub \u{202e}rete\u{200b} = r
;      |         ^^^^^^^^^^^^^^^^^^^^
;      = fix (replace): bytes 30..40 -> "archivum"
;
; The caret run is computed from the ESCAPED widths, not the source widths --
; `pre` and `mid` below are what diag_escape actually wrote, so a span
; preceded by a bidi override still gets its carets under the right thing.
; Above, the span is 10 SOURCE bytes and 20 RENDERED ones, and the carets are
; 20 wide. Taking the width from the raw byte offsets instead is the obvious
; mistake and misaligns by six characters per escaped codepoint.
;
; diag_render_text(diagrec, buf, cap) -> rax = bytes written, rdx = 1 if the
; output was truncated.
proc diag_render_text, diagrec, buf, cap
	uses	rbx
	locals
		slot wr,     32
		slot numbuf, 12
		slot idx,    dd
		slot lin,    dq
		slot col,    dq
		slot lstart, dq
		slot lend,   dq
		slot s0,     dq
		slot s1,     dq
		slot pre,    dq
		slot mid,    dq
		slot digits, dq
	endl
	mov	rbx, [diagrec]

	lea	rdi, [wr]
	mov	rsi, [buf]
	mov	rdx, [cap]
	call	diag_out_init

	mov	edi, [rbx + Diag.code_num]
	call	diag_lookup
	mov	[idx], eax

	; ---- header line ----
	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.path_ptr]
	mov	rdx, [rbx + Diag.path_len]
	call	diag_out_esc

	mov	rdi, [rbx + Diag.src_ptr]
	mov	rsi, [rbx + Diag.src_len]
	mov	edx, [rbx + Diag.span.start]
	call	diag_line_col
	mov	[lin], rax
	mov	[col], rdx

	lea	rdi, [wr]
	lea	rsi, [diag_lit_colon]
	mov	rdx, DIAG_LIT_COLON_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, dword [lin]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_lit_colon]
	mov	rdx, DIAG_LIT_COLON_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, dword [col]
	call	diag_out_u32

	lea	rdi, [wr]
	lea	rsi, [diag_lit_errbr]
	mov	rdx, DIAG_LIT_ERRBR_LEN
	call	diag_out_bytes

	mov	eax, [idx]
	imul	eax, DIAG_CODE_STR_STRIDE
	lea	rsi, [diag_code_str]
	add	rsi, rax
	lea	rdi, [wr]
	mov	rdx, DIAG_CODE_STR_STRIDE
	call	diag_out_bytes

	lea	rdi, [wr]
	lea	rsi, [diag_lit_brcolon]
	mov	rdx, DIAG_LIT_BRCOLON_LEN
	call	diag_out_bytes

	mov	eax, [idx]
	lea	rdi, [wr]
	mov	rsi, [diag_code_msg_ptr + rax*8]
	mov	edx, [diag_code_msg_len + rax*4]
	call	diag_out_esc

	lea	rdi, [wr]
	lea	rsi, [diag_lit_nl]
	mov	rdx, DIAG_LIT_NL_LEN
	call	diag_out_bytes

	; gutter width is the line number's decimal width; measure it once
	mov	edi, dword [lin]
	lea	rsi, [numbuf]
	mov	rdx, 12
	call	diag_fmt_u32
	mov	[digits], rax

	; ---- source line and carets (skipped when no source is attached) ----
	cmp	qword [rbx + Diag.src_len], 0
	je	.fixblock

	mov	rdi, [rbx + Diag.src_ptr]
	mov	rsi, [rbx + Diag.src_len]
	mov	edx, [rbx + Diag.span.start]
	call	diag_line_bounds
	mov	[lstart], rax
	mov	[lend], rdx

	; s0 = clamp(span.start, lstart, lend)
	mov	eax, [rbx + Diag.span.start]
	cmp	rax, [lstart]
	jae	.s0_lo
	mov	rax, [lstart]
  .s0_lo:
	cmp	rax, [lend]
	jbe	.s0_hi
	mov	rax, [lend]
  .s0_hi:
	mov	[s0], rax

	; s1 = clamp(span.start + span.len, s0, lend)
	mov	eax, [rbx + Diag.span.start]
	mov	ecx, [rbx + Diag.span.len]
	add	rax, rcx
	cmp	rax, [s0]
	jae	.s1_lo
	mov	rax, [s0]
  .s1_lo:
	cmp	rax, [lend]
	jbe	.s1_hi
	mov	rax, [lend]
  .s1_hi:
	mov	[s1], rax

	lea	rdi, [wr]
	mov	esi, ' '
	mov	rdx, 1
	call	diag_out_fill
	lea	rdi, [wr]
	mov	esi, dword [lin]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_lit_bar]
	mov	rdx, DIAG_LIT_BAR_LEN
	call	diag_out_bytes

	; the line, escaped in three pieces so the two boundary widths fall out
	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.src_ptr]
	add	rsi, [lstart]
	mov	rdx, [s0]
	sub	rdx, [lstart]
	call	diag_out_esc
	mov	[pre], rax

	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.src_ptr]
	add	rsi, [s0]
	mov	rdx, [s1]
	sub	rdx, [s0]
	call	diag_out_esc
	mov	[mid], rax

	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.src_ptr]
	add	rsi, [s1]
	mov	rdx, [lend]
	sub	rdx, [s1]
	call	diag_out_esc

	lea	rdi, [wr]
	lea	rsi, [diag_lit_nl]
	mov	rdx, DIAG_LIT_NL_LEN
	call	diag_out_bytes

	; caret line
	lea	rdi, [wr]
	mov	esi, ' '
	mov	rdx, [digits]
	inc	rdx
	call	diag_out_fill
	lea	rdi, [wr]
	lea	rsi, [diag_lit_bar]
	mov	rdx, DIAG_LIT_BAR_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, ' '
	mov	rdx, [pre]
	call	diag_out_fill
	mov	rdx, [mid]
	test	rdx, rdx
	jnz	.carets
	mov	rdx, 1			; a zero-length span still points somewhere
  .carets:
	lea	rdi, [wr]
	mov	esi, '^'
	call	diag_out_fill
	lea	rdi, [wr]
	lea	rsi, [diag_lit_nl]
	mov	rdx, DIAG_LIT_NL_LEN
	call	diag_out_bytes

	; ---- fix line ----
  .fixblock:
	mov	eax, [rbx + Diag.fix.kind]
	cmp	eax, DIAG_FIX_NONE
	je	.done

	lea	rdi, [wr]
	mov	esi, ' '
	mov	rdx, [digits]
	inc	rdx
	call	diag_out_fill
	lea	rdi, [wr]
	lea	rsi, [diag_lit_eq]
	mov	rdx, DIAG_LIT_EQ_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	lea	rsi, [diag_lit_fixopen]
	mov	rdx, DIAG_LIT_FIXOPEN_LEN
	call	diag_out_bytes

	mov	edi, [rbx + Diag.fix.kind]
	call	diag_fix_kind_str	; rax = ptr, rdx = len
	mov	rsi, rax
	lea	rdi, [wr]
	call	diag_out_bytes

	lea	rdi, [wr]
	lea	rsi, [diag_lit_fixmid]
	mov	rdx, DIAG_LIT_FIXMID_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.fix.edit.start]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_lit_dotdot]
	mov	rdx, DIAG_LIT_DOTDOT_LEN
	call	diag_out_bytes
	mov	eax, [rbx + Diag.fix.edit.start]
	add	eax, [rbx + Diag.fix.edit.len]
	lea	rdi, [wr]
	mov	esi, eax
	call	diag_out_u32

	cmp	qword [rbx + Diag.fix.text_len], 0
	je	.fixdone
	lea	rdi, [wr]
	lea	rsi, [diag_lit_arrow]
	mov	rdx, DIAG_LIT_ARROW_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.fix.text_ptr]
	mov	rdx, [rbx + Diag.fix.text_len]
	call	diag_out_esc
	lea	rdi, [wr]
	lea	rsi, [diag_lit_dquote]
	mov	rdx, DIAG_LIT_DQUOTE_LEN
	call	diag_out_bytes
  .fixdone:
	lea	rdi, [wr]
	lea	rsi, [diag_lit_nl]
	mov	rdx, DIAG_LIT_NL_LEN
	call	diag_out_bytes

  .done:
	mov	rax, [wr + DiagOut.len]
	mov	edx, [wr + DiagOut.trunc]
	return
endp

; ---- machine-readable rendering --------------------------------------------
; One JSON object, no trailing newline (the caller frames records however it
; frames them -- one per line, an array, whatever the consumer wants; that is
; not this function's business). Shape, wrapped here for the margin but
; emitted on ONE line with no whitespace between members:
;
;   {"code":"EXS-E0421","code_num":421,"file":"...","line":2,"col":9,
;    "span":{"file_id":3,"start":30,"len":10},"message":"...",
;    "snippet":"...","fix":null}
;
; and with a fix, the last member instead reads
;
;   "fix":{"kind":"replace","file_id":3,"start":30,"len":10,"text":"..."}
;
; `fix` is ALWAYS present -- `null` when there is none -- so a consumer's
; schema does not change shape between diagnostics. `snippet` is the span's
; own bytes, escaped; the surrounding line is not repeated, because a
; consumer that wants it has `file`, `start` and `len` and can take it.
;
; Every string member's body goes through diag_out_jstr, which is
; escape.inc's JSON mode: the value a parser hands back is exactly the
; text-mode rendering, so a tool that prints `snippet` raw still prints
; `\u{202e}` and not a bidi override.
;
; diag_render_json(diagrec, buf, cap) -> rax = bytes written, rdx = 1 if the
; output was truncated. A truncated JSON record is NOT valid JSON -- there is
; no honest way to close an object whose remaining bytes did not fit -- so
; `rdx` is the contract a consumer must check before parsing, exactly as with
; the text mode.
proc diag_render_json, diagrec, buf, cap
	uses	rbx
	locals
		slot wr, 32
		slot idx, dd
		slot s0,  dq
		slot s1,  dq
	endl
	mov	rbx, [diagrec]

	lea	rdi, [wr]
	mov	rsi, [buf]
	mov	rdx, [cap]
	call	diag_out_init

	mov	edi, [rbx + Diag.code_num]
	call	diag_lookup
	mov	[idx], eax

	lea	rdi, [wr]
	lea	rsi, [diag_j_open]
	mov	rdx, DIAG_J_OPEN_LEN
	call	diag_out_bytes

	mov	eax, [idx]
	imul	eax, DIAG_CODE_STR_STRIDE
	lea	rsi, [diag_code_str]
	add	rsi, rax
	lea	rdi, [wr]
	mov	rdx, DIAG_CODE_STR_STRIDE
	call	diag_out_bytes

	lea	rdi, [wr]
	lea	rsi, [diag_j_num]
	mov	rdx, DIAG_J_NUM_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.code_num]
	call	diag_out_u32

	lea	rdi, [wr]
	lea	rsi, [diag_j_file]
	mov	rdx, DIAG_J_FILE_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.path_ptr]
	mov	rdx, [rbx + Diag.path_len]
	call	diag_out_jstr

	mov	rdi, [rbx + Diag.src_ptr]
	mov	rsi, [rbx + Diag.src_len]
	mov	edx, [rbx + Diag.span.start]
	call	diag_line_col		; rax = line, rdx = col
	mov	[s0], rax
	mov	[s1], rdx

	lea	rdi, [wr]
	lea	rsi, [diag_j_line]
	mov	rdx, DIAG_J_LINE_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, dword [s0]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_j_col]
	mov	rdx, DIAG_J_COL_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, dword [s1]
	call	diag_out_u32

	lea	rdi, [wr]
	lea	rsi, [diag_j_spanfid]
	mov	rdx, DIAG_J_SPANFID_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.span.file_id]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_j_start]
	mov	rdx, DIAG_J_START_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.span.start]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_j_len]
	mov	rdx, DIAG_J_LEN_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.span.len]
	call	diag_out_u32

	lea	rdi, [wr]
	lea	rsi, [diag_j_msg]
	mov	rdx, DIAG_J_MSG_LEN
	call	diag_out_bytes
	mov	eax, [idx]
	lea	rdi, [wr]
	mov	rsi, [diag_code_msg_ptr + rax*8]
	mov	edx, [diag_code_msg_len + rax*4]
	call	diag_out_jstr

	; snippet = the span's own bytes, clamped into the attached source
	mov	eax, [rbx + Diag.span.start]
	cmp	rax, [rbx + Diag.src_len]
	jbe	.sn0
	mov	rax, [rbx + Diag.src_len]
  .sn0:
	mov	[s0], rax
	mov	eax, [rbx + Diag.span.start]
	mov	ecx, [rbx + Diag.span.len]
	add	rax, rcx
	cmp	rax, [rbx + Diag.src_len]
	jbe	.sn1
	mov	rax, [rbx + Diag.src_len]
  .sn1:
	cmp	rax, [s0]
	jae	.sn2
	mov	rax, [s0]
  .sn2:
	mov	[s1], rax

	lea	rdi, [wr]
	lea	rsi, [diag_j_snip]
	mov	rdx, DIAG_J_SNIP_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.src_ptr]
	add	rsi, [s0]
	mov	rdx, [s1]
	sub	rdx, [s0]
	call	diag_out_jstr

	; fix
	mov	eax, [rbx + Diag.fix.kind]
	cmp	eax, DIAG_FIX_NONE
	jne	.has_fix
	lea	rdi, [wr]
	lea	rsi, [diag_j_fixnull]
	mov	rdx, DIAG_J_FIXNULL_LEN
	call	diag_out_bytes
	jmp	.done

  .has_fix:
	lea	rdi, [wr]
	lea	rsi, [diag_j_fixopen]
	mov	rdx, DIAG_J_FIXOPEN_LEN
	call	diag_out_bytes
	mov	edi, [rbx + Diag.fix.kind]
	call	diag_fix_kind_str	; rax = ptr, rdx = len
	mov	rsi, rax
	lea	rdi, [wr]
	call	diag_out_bytes

	lea	rdi, [wr]
	lea	rsi, [diag_j_fixfid]
	mov	rdx, DIAG_J_FIXFID_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.fix.edit.file_id]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_j_start]
	mov	rdx, DIAG_J_START_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.fix.edit.start]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_j_len]
	mov	rdx, DIAG_J_LEN_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.fix.edit.len]
	call	diag_out_u32

	lea	rdi, [wr]
	lea	rsi, [diag_j_fixtext]
	mov	rdx, DIAG_J_FIXTEXT_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.fix.text_ptr]
	mov	rdx, [rbx + Diag.fix.text_len]
	call	diag_out_jstr
	lea	rdi, [wr]
	lea	rsi, [diag_j_close]
	mov	rdx, DIAG_J_CLOSE_LEN
	call	diag_out_bytes

  .done:
	mov	rax, [wr + DiagOut.len]
	mov	edx, [wr + DiagOut.trunc]
	return
endp

; ---- emission --------------------------------------------------------------

; diag_emit(fd, diagrec, buf, cap, mode) -> rax = bytes written by the kernel;
; CF clear on success. On a write failure, CF is set and `eax` carries
; `sys_write`'s own `-errno` verbatim -- the errno channel, propagated
; unreinterpreted exactly as `arena_init` propagates `sys_mmap`'s
; (docs/asm-conventions.md, "1.3 Error protocol: CF / eax"). This routine
; never sets an `EXS-E` code of its own: a failed write to stderr is a host
; failure, not a diagnosable property of the source being compiled.
;
; `buf`/`cap` are the caller's scratch. Truncation is NOT reported through
; `CF`: a short diagnostic is still worth printing, and the renderers'
; `rdx` flag is where a caller that cares learns about it. A caller who
; wants that bit calls the renderer directly and writes the buffer itself.
proc diag_emit, fd, diagrec, buf, cap, mode
	locals
	endl
	mov	rax, [mode]
	cmp	rax, DIAG_MODE_JSON
	je	.json
	mov	rdi, [diagrec]
	mov	rsi, [buf]
	mov	rdx, [cap]
	call	diag_render_text
	jmp	.write
  .json:
	mov	rdi, [diagrec]
	mov	rsi, [buf]
	mov	rdx, [cap]
	call	diag_render_json
  .write:
	mov	rdi, [fd]
	mov	rsi, [buf]
	mov	rdx, rax
	call	sys_write
	jc	.wr_failed
	return
  .wr_failed:
	fail	eax			; propagate sys_write's -errno verbatim
endp
