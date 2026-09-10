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
; THREE FIELDS THAT SENTENCE DID NOT USED TO ALLOW, and a correction to how
; strongly it was read. `docs/design/diagnostics-review.md` measured Stage 1
; against §16's kill criterion and found 37 of 46 diagnostics were the
; identical sentence `unexpected token`, because `struct Diag` was
; `code_num`, `Span`, `src_*`, `path_*`, `DiagFix` and had no field that
; could hold anything else. The reasoning above was right about format sites
; and too strong about what follows from it: what §8.3 forbids is a TOOL
; matching on English, and what Fluent stalled on was 400+ FORMATTING sites.
; A record can carry more than its code's one sentence without either.
;
;   - `note_id` + `arg_ptr`/`arg_len` -- a NOTE. The id is permanent and
;     machine-matchable exactly as a code is; the English is a table lookup
;     on it (notes.inc); the argument is a bounded compiler-owned token
;     APPENDED after the sentence, never interpolated into it. Translation
;     stays a lookup, now keyed on (code, note_id).
;   - `rel` + `flags` -- a RELATED SPAN, so `EXS-E0202` can point at the `{`
;     that was left open instead of only at the end of the file (review D6).
;     Rendered as a second gutter block in text and a second member in JSON.
;
; A FIX IS NOT A SUGGESTION, and this file is where the difference is
; enforced. §8.3 promises machine-applicable fixes for "capability and
; lexicon errors" -- `diag_fix_required` enumerates exactly those, with each
; membership cited (fix.inc). The review measured what shipping an edit
; OUTSIDE that set costs: applying the compiler's own payloads made the file
; worse in 2 of 8 multi-fix cases, and one single-error file became
; `redde; per;` plus a surplus brace. Each of those edits is individually
; well formed; the corruption comes from applying a BATCH of advisory edits
; derived from one broken parse.
;
; So the renderers split them. A payload on a code `diag_fix_required`
; names is a `fix` -- mechanically derivable, safe for `exsc emenda` to
; apply in a batch. A payload on any other code renders as a `suggestion`:
; the same structure, the same span, shown to a human, and NEVER offered as
; a batch-applicable edit. Both members are always present in JSON, one of
; them `null`, so a consumer's schema does not change shape. The rule is a
; property of the CODE, so it needs no batch state and no ordering, and it
; renders the same whether a record is rendered once or a hundred times.
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
; 128 bytes. The `Span` is rt/span.inc's, embedded by value, not a re-spelling
; of it: §8.3 makes the span non-optional ("not retrofittable, which is why
; the CST is not optional"), and the CST will hand these out as `Span`s.
; `src_ptr`/`src_len` are the file's text, needed to turn a byte offset into
; a line, a column and an echoed line; `path_ptr`/`path_len` are its name.
; Both may be zero -- a diagnostic with no source attached still renders its
; header, its code and its fix, just without a snippet.
;
; It was 96 bytes and five fields until the Stage 1 review; see this file's
; header for what the three additions are and why they are not the thing §8.3
; warns about. `note_id` and `flags` take over the two explicit pad dwords, so
; the growth is the 32 bytes of `rel` + `arg_*` and nothing else. Everything
; that sizes a buffer from this struct does it symbolically (`sizeof.Diag`),
; which was checked before growing it: driver/cli.inc's `vec_init` stride and
; driver/run.inc's are the only two, and both read the symbol.
;
; `arg_ptr` is compiler-owned text -- notes.inc's tables, never source -- and
; is escaped at the render site anyway, for fix.inc's reason about
; `text_ptr`. `rel` is meaningful only when `flags` has DIAG_F_REL set;
; `diag_init` clears both, so a record nobody called `diag_rel_set` on
; renders `"related":null` and no second gutter block.
DIAG_F_REL = 1			; bit 0 of `flags`: `rel` carries a real span

struct Diag
	field code_num, dd
	field note_id,  dd
	span Span
	field flags,    dd
	field src_ptr,  dq
	field src_len,  dq
	field path_ptr, dq
	field path_len, dq
	rel Span
	field pad2,     dd
	field arg_ptr,  dq
	field arg_len,  dq
	fix DiagFix
end struct

; What `__diag_advise` resolves a record's note down to, so neither renderer
; has to duplicate the derivation and neither has to MUTATE the record it was
; handed. Rendering the same `Diag` twice gives the same bytes twice, which is
; what §9.3 asks of everything else in this compiler.
struct DiagAdvice
	field note_id, dd
	field pad0,    dd
	field arg_ptr, dq
	field arg_len, dq
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
diag_lit_fixmid  db '): bytes '
DIAG_LIT_FIXMID_LEN  = $ - diag_lit_fixmid
diag_lit_dotdot  db '..'
DIAG_LIT_DOTDOT_LEN  = $ - diag_lit_dotdot
diag_lit_arrow   db ' -> "'
DIAG_LIT_ARROW_LEN   = $ - diag_lit_arrow
diag_lit_dquote  db '"'
DIAG_LIT_DQUOTE_LEN  = $ - diag_lit_dquote
diag_lit_note    db 'note: '
DIAG_LIT_NOTE_LEN    = $ - diag_lit_note
diag_lit_space   db ' '
DIAG_LIT_SPACE_LEN   = $ - diag_lit_space
; The two payload labels. `fix` means §8.3 promised it and `exsc emenda` may
; batch it; `suggestion` means a human should read it and no tool should
; apply it unattended. See this file's header.
diag_lit_fixword db 'fix'
DIAG_LIT_FIXWORD_LEN = $ - diag_lit_fixword
diag_lit_sugword db 'suggestion'
DIAG_LIT_SUGWORD_LEN = $ - diag_lit_sugword
diag_lit_paren   db ' ('
DIAG_LIT_PAREN_LEN   = $ - diag_lit_paren

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
diag_j_fixfid    db '","file_id":'
DIAG_J_FIXFID_LEN    = $ - diag_j_fixfid
diag_j_fixtext   db ',"text":"'
DIAG_J_FIXTEXT_LEN   = $ - diag_j_fixtext
diag_j_note      db '","note":'
DIAG_J_NOTE_LEN      = $ - diag_j_note
diag_j_noteid    db ',"note_id":'
DIAG_J_NOTEID_LEN    = $ - diag_j_noteid
diag_j_related   db ',"related":'
DIAG_J_RELATED_LEN   = $ - diag_j_related
diag_j_snip2     db ',"snippet":"'
DIAG_J_SNIP2_LEN     = $ - diag_j_snip2
diag_j_fixmemb   db '","fix":'
DIAG_J_FIXMEMB_LEN   = $ - diag_j_fixmemb
diag_j_suggmemb  db ',"suggestion":'
DIAG_J_SUGGMEMB_LEN  = $ - diag_j_suggmemb
diag_j_null      db 'null'
DIAG_J_NULL_LEN      = $ - diag_j_null
diag_j_kindopen  db '{"kind":"'
DIAG_J_KINDOPEN_LEN  = $ - diag_j_kindopen
diag_j_fidopen   db '{"file_id":'
DIAG_J_FIDOPEN_LEN   = $ - diag_j_fidopen
diag_j_quote     db '"'
DIAG_J_QUOTE_LEN     = $ - diag_j_quote
diag_j_objend    db '}'
DIAG_J_OBJEND_LEN    = $ - diag_j_objend
diag_j_textend   db '"}'
DIAG_J_TEXTEND_LEN   = $ - diag_j_textend

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

; __diag_out_esc_mode(wr, src, n, mode) -> rax = bytes actually appended,
; rdx = the TERMINAL COLUMNS those bytes occupy.
; The single point at which this file reaches escape.inc; both public
; wrappers below go through it, so there is exactly one place where a call
; could have forgotten to escape, and it is this one.
;
; The column count is escape.inc's `rcx`, forwarded because the caret line
; below has to be padded in columns and the source line above it is printed
; in characters. Using the byte count for both is `docs/design/
; diagnostics-review.md` D4: a caret one column too far right for every
; multibyte character before it.
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
	call	__diag_escape_core	; rax = written, rdx = trunc, rcx = columns
	add	[rbx + DiagOut.len], rax
	test	edx, edx
	jz	.ok
	mov	dword [rbx + DiagOut.trunc], 1
  .ok:
	mov	rdx, rcx
	return
endp

; diag_out_esc(wr, src, n) -> rax = bytes appended, rdx = columns occupied.
; Human-readable escaping. Callers that only place text want `rax`; the one
; caller that has to align something under it wants `rdx`.
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

; diag_out_jstr(wr, src, n) -> rax = bytes appended (rdx is a column count
; that means nothing in this mode -- a JSON member is not a terminal). JSON
; string BODY; the caller owns the surrounding quotes.
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

; __diag_col_of(wr, src, n) -> rax = the TERMINAL COLUMNS `src[0..n)` would
; occupy if escaped into `wr` right now -- WITHOUT actually appending
; anything: `wr`'s `len`/`trunc` are read but never written. This is
; `__diag_out_esc_mode`'s own addressing (scratch space between the
; buffer's current `len` and its `cap`), minus the commit, so the text
; header below can ask "how many columns does this span's line-prefix cost"
; using the SAME classifier the caret run is padded with
; (`__diag_escape_core`, via escape.inc) -- one classifier, no second
; implementation of "what counts as a column" (see util.inc's header, and
; `docs/design/diagnostics-review.md` D4).
;
; Reusing the live output buffer as scratch is safe: nothing has been
; committed past `len` yet, so writing bytes there clobbers nothing a caller
; can observe, and everything written here is overwritten byte-for-byte by
; the real appends that follow. If `n` does not fit in the buffer's
; remaining capacity, `__diag_escape_core` truncates and the column count it
; returns is a partial one -- exactly the same degraded-but-not-crashing
; outcome the rest of this file already accepts under truncation (this
; file's header, "TRUNCATION, NEVER OVERRUN"), and the remaining capacity
; here is never smaller than what the later, larger echo of the same line
; will need, so this only under-counts in a diagnostic that was already
; going to render truncated.
proc __diag_col_of, wr, src, n
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
	mov	r8, DIAG_ESC_TEXT
	call	__diag_escape_core	; rax = written (discarded), rdx = trunc
					; (discarded), rcx = columns
	mov	rax, rcx
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
	mov	dword [rbx + Diag.note_id], DIAG_NOTE_NONE
	mov	eax, [file_id]
	mov	[rbx + Diag.span.file_id], eax
	mov	eax, [off]
	mov	[rbx + Diag.span.start], eax
	mov	eax, [len]
	mov	[rbx + Diag.span.len], eax
	mov	dword [rbx + Diag.flags], 0
	mov	qword [rbx + Diag.src_ptr], 0
	mov	qword [rbx + Diag.src_len], 0
	mov	qword [rbx + Diag.path_ptr], 0
	mov	qword [rbx + Diag.path_len], 0
	mov	dword [rbx + Diag.rel.file_id], 0
	mov	dword [rbx + Diag.rel.start], 0
	mov	dword [rbx + Diag.rel.len], 0
	mov	dword [rbx + Diag.pad2], 0
	mov	qword [rbx + Diag.arg_ptr], 0
	mov	qword [rbx + Diag.arg_len], 0
	lea	rdi, [rbx + Diag.fix]
	call	diag_fix_clear
	return
endp

; diag_note_set(diagrec, note_id, arg_ptr, arg_len) -- attach a note. `note_id`
; is one of notes.inc's `DIAG_NOTE_*`; `arg_ptr`/`arg_len` may be zero for a
; note that takes no argument.
;
; THIS IS THE HOOK FOR THE PASSES, and it is deliberately more capable than
; anything currently calls it. A pass knows things the renderer cannot
; recover from a span -- which token was expected, which rule was being
; parsed -- and `docs/design/diagnostics-review.md`'s own remedy is to
; populate this at the construction sites. Until those sites call it,
; `__diag_advise` derives what it can from the span's own bytes; a note set
; here always wins, because a pass that says something specific is never
; improved on by a guess.
proc diag_note_set, diagrec, note_id, arg_ptr, arg_len
	uses	rbx
	locals
	endl
	mov	rbx, [diagrec]
	mov	eax, [note_id]
	mov	[rbx + Diag.note_id], eax
	mov	rax, [arg_ptr]
	mov	[rbx + Diag.arg_ptr], rax
	mov	rax, [arg_len]
	mov	[rbx + Diag.arg_len], rax
	return
endp

; diag_rel_set(diagrec, file_id, off, len) -- attach the RELATED span: the
; place a reader has to look at to understand the one the diagnostic is
; reported at. `EXS-E0202` at end of file and the `{` three lines up are the
; case this exists for (review D6); rustc and clang both do this and it is
; the single thing they do that this renderer could not express.
;
; Sets DIAG_F_REL. There is no unset -- clearing the record is `diag_init`.
proc diag_rel_set, diagrec, file_id, off, len
	uses	rbx
	locals
	endl
	mov	rbx, [diagrec]
	mov	eax, [file_id]
	mov	[rbx + Diag.rel.file_id], eax
	mov	eax, [off]
	mov	[rbx + Diag.rel.start], eax
	mov	eax, [len]
	mov	[rbx + Diag.rel.len], eax
	or	dword [rbx + Diag.flags], DIAG_F_REL
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

; ---- shared rendering helpers ----------------------------------------------

; __diag_dec_width(value) -> rax = the number of decimal digits `value` prints
; as. Measured by formatting it into a throwaway frame buffer rather than by a
; compare chain, so there is exactly one decimal formatter in this module and
; the gutter can never disagree with the number printed in it.
proc __diag_dec_width, value
	locals
		slot dwbuf, 12		; 2^32-1 is 10 digits
	endl
	mov	edi, [value]
	lea	rsi, [dwbuf]
	mov	rdx, 12
	call	diag_fmt_u32
	return
endp

; __diag_echo_line(wr, esrc, eslen, eoff, espan, egut) -- one gutter block:
; the source line containing `eoff`, escaped, and under it a caret run under
; the `espan` bytes starting at `eoff`. `egut` is the gutter width in decimal
; digits, passed in rather than computed here so a primary and a related
; block line up with each other.
;
; THE CARET RUN AND ITS PADDING ARE IN COLUMNS, NOT BYTES. `diag_out_esc`
; returns both counts; this routine keeps `rdx` (columns) and discards `rax`
; (bytes). Getting that backwards is `docs/design/diagnostics-review.md` D4:
; the escaped-codepoint case was already right (an escape sequence is ASCII,
; so its two counts coincide) and the pass-through multibyte case was never
; considered, which put the caret one column right per non-ASCII character
; before it -- on ordinary Latin identifiers, in a language whose §8.1 policy
; exists to admit them.
;
; Both endpoints are clamped into the line: a caret run must fit on one line
; even when the span does not.
proc __diag_echo_line, wr, esrc, eslen, eoff, espan, egut
	uses	rbx
	locals
		slot elin,    dq
		slot elstart, dq
		slot elend,   dq
		slot es0,     dq
		slot es1,     dq
		slot epre,    dq
		slot emid,    dq
		slot elw,     dq
	endl
	mov	rdi, [esrc]
	mov	rsi, [eslen]
	mov	rdx, [eoff]
	call	diag_line_col		; rax = line (rdx = col, unused here)
	mov	[elin], rax

	mov	rdi, [esrc]
	mov	rsi, [eslen]
	mov	rdx, [eoff]
	call	diag_line_bounds	; rax = line start, rdx = line end
	mov	[elstart], rax
	mov	[elend], rdx

	; s0 = clamp(eoff, lstart, lend)
	mov	rax, [eoff]
	cmp	rax, [elstart]
	jae	.s0_lo
	mov	rax, [elstart]
  .s0_lo:
	cmp	rax, [elend]
	jbe	.s0_hi
	mov	rax, [elend]
  .s0_hi:
	mov	[es0], rax

	; s1 = clamp(eoff + espan, s0, lend)
	mov	rax, [eoff]
	add	rax, [espan]
	cmp	rax, [es0]
	jae	.s1_lo
	mov	rax, [es0]
  .s1_lo:
	cmp	rax, [elend]
	jbe	.s1_hi
	mov	rax, [elend]
  .s1_hi:
	mov	[es1], rax

	; ---- ` LINE | ` ----
	mov	rdi, [elin]
	call	__diag_dec_width
	mov	[elw], rax
	mov	rdx, [egut]
	inc	rdx
	cmp	rdx, [elw]
	jae	.padok
	mov	rdx, [elw]		; defensive: never fill a negative count
  .padok:
	sub	rdx, [elw]
	mov	rdi, [wr]
	mov	esi, ' '
	call	diag_out_fill
	mov	rdi, [wr]
	mov	esi, dword [elin]
	call	diag_out_u32
	mov	rdi, [wr]
	lea	rsi, [diag_lit_bar]
	mov	rdx, DIAG_LIT_BAR_LEN
	call	diag_out_bytes

	; ---- the line, escaped in three pieces so the two widths fall out ----
	mov	rdi, [wr]
	mov	rsi, [esrc]
	add	rsi, [elstart]
	mov	rdx, [es0]
	sub	rdx, [elstart]
	call	diag_out_esc
	mov	[epre], rdx		; COLUMNS

	mov	rdi, [wr]
	mov	rsi, [esrc]
	add	rsi, [es0]
	mov	rdx, [es1]
	sub	rdx, [es0]
	call	diag_out_esc
	mov	[emid], rdx		; COLUMNS

	mov	rdi, [wr]
	mov	rsi, [esrc]
	add	rsi, [es1]
	mov	rdx, [elend]
	sub	rdx, [es1]
	call	diag_out_esc

	mov	rdi, [wr]
	lea	rsi, [diag_lit_nl]
	mov	rdx, DIAG_LIT_NL_LEN
	call	diag_out_bytes

	; ---- the caret line ----
	mov	rdi, [wr]
	mov	esi, ' '
	mov	rdx, [egut]
	inc	rdx
	call	diag_out_fill
	mov	rdi, [wr]
	lea	rsi, [diag_lit_bar]
	mov	rdx, DIAG_LIT_BAR_LEN
	call	diag_out_bytes
	mov	rdi, [wr]
	mov	esi, ' '
	mov	rdx, [epre]
	call	diag_out_fill
	mov	rdx, [emid]
	test	rdx, rdx
	jnz	.carets
	mov	rdx, 1			; a zero-length span still points somewhere
  .carets:
	mov	rdi, [wr]
	mov	esi, '^'
	call	diag_out_fill
	mov	rdi, [wr]
	lea	rsi, [diag_lit_nl]
	mov	rdx, DIAG_LIT_NL_LEN
	call	diag_out_bytes
	return
endp

; __diag_advise(diagrec, advout) -- fill a `DiagAdvice` with the note this
; diagnostic should print. A note the record already carries is copied
; through unchanged; otherwise one is DERIVED from the code and the span's
; own bytes.
;
; WHY DERIVE AT ALL, when `diag_note_set` exists. Because the passes that
; construct these records live in other trees, and `docs/design/
; diagnostics-review.md`'s measurement is about what a user sees TODAY: 80%
; of diagnostics were one sentence. Everything below is recoverable from the
; record alone -- the code, the bytes the span covers, and the edit already
; attached -- so it needs no cooperation from the parser and cannot be out of
; date with respect to it. A pass that later calls `diag_note_set` overrides
; all of it, which is the right precedence: a parser knows what it EXPECTED,
; and nothing here can.
;
; PURE. It writes only into `advout`, never into the record, so rendering a
; `Diag` twice yields the same bytes twice and the two renderers cannot
; disagree about what the note was.
;
; DETERMINISTIC. Every branch below is a function of bytes -- no pointer, no
; hash, no iteration order (CLAUDE.md).
;
; ORDER MATTERS AND IS DELIBERATE. An English keyword is checked before the
; near-miss scan, because `if` is one edit from the reserved word `in` and
; two from `si`: a Levenshtein-first order would answer "did you mean `in`"
; to the single most common first-day mistake in the language. The symbolic
; comparison check precedes the fix-derived ones for the same reason -- `a >
; 10` carries an insert-`;` edit, and "spec 8.6 decision 6" is a better
; sentence than "statements end with a semicolon" when the token is `>`.
proc __diag_advise, diagrec, advout
	uses	rbx, r12, r13
	locals
		slot spptr,  dq
		slot splen,  dq
		slot fxtp,   dq
		slot fxtl,   dq
		slot engrow, dd
		slot advpad, dd
	endl
	mov	rbx, [diagrec]
	mov	r12, [advout]
	mov	dword [r12 + DiagAdvice.note_id], DIAG_NOTE_NONE
	mov	dword [r12 + DiagAdvice.pad0], 0
	mov	qword [r12 + DiagAdvice.arg_ptr], 0
	mov	qword [r12 + DiagAdvice.arg_len], 0

	mov	eax, [rbx + Diag.note_id]
	test	eax, eax
	jz	.derive
	mov	[r12 + DiagAdvice.note_id], eax
	mov	rax, [rbx + Diag.arg_ptr]
	mov	[r12 + DiagAdvice.arg_ptr], rax
	mov	rax, [rbx + Diag.arg_len]
	mov	[r12 + DiagAdvice.arg_len], rax
	return

  .derive:
	; the span's own bytes, clamped into whatever source is attached
	mov	qword [spptr], 0
	mov	qword [splen], 0
	cmp	qword [rbx + Diag.src_len], 0
	je	.nospan
	mov	eax, [rbx + Diag.span.start]
	cmp	rax, [rbx + Diag.src_len]
	jbe	.c0
	mov	rax, [rbx + Diag.src_len]
  .c0:
	mov	r13, rax
	mov	eax, [rbx + Diag.span.start]
	mov	ecx, [rbx + Diag.span.len]
	add	rax, rcx
	cmp	rax, [rbx + Diag.src_len]
	jbe	.c1
	mov	rax, [rbx + Diag.src_len]
  .c1:
	cmp	rax, r13
	jae	.c2
	mov	rax, r13
  .c2:
	sub	rax, r13
	mov	[splen], rax
	mov	rax, [rbx + Diag.src_ptr]
	add	rax, r13
	mov	[spptr], rax
  .nospan:

	; the attached edit's replacement text, if there is one
	mov	qword [fxtp], 0
	mov	qword [fxtl], 0
	mov	eax, [rbx + Diag.fix.kind]
	cmp	eax, DIAG_FIX_NONE
	je	.nofix
	mov	rax, [rbx + Diag.fix.text_ptr]
	mov	[fxtp], rax
	mov	rax, [rbx + Diag.fix.text_len]
	mov	[fxtl], rax
  .nofix:

	mov	eax, [rbx + Diag.code_num]
	cmp	eax, 201
	je	.k201
	cmp	eax, 202
	je	.keof
	cmp	eax, 203
	je	.keof
	cmp	eax, 210
	je	.k210
	cmp	eax, 220
	je	.k220
	return

	; ---- EXS-E0201 unexpected token ----
  .k201:
	cmp	qword [splen], 0
	je	.k201_prev

	mov	rdi, [spptr]
	mov	rsi, [splen]
	call	diag_eng_lookup
	cmp	eax, -1
	je	.k201_cmp
	mov	[engrow], eax
	mov	edi, eax
	call	diag_eng_note_id
	mov	[r12 + DiagAdvice.note_id], eax
	mov	edi, [engrow]
	call	diag_eng_kw_word	; rax = ptr, rdx = len (0 if none)
	mov	[r12 + DiagAdvice.arg_ptr], rax
	mov	[r12 + DiagAdvice.arg_len], rdx
	return

  .k201_cmp:
	mov	rdi, [spptr]
	mov	rsi, [splen]
	call	diag_cmp_word		; rax = ptr, rdx = len (0 if none)
	test	rdx, rdx
	jz	.k201_near
	mov	dword [r12 + DiagAdvice.note_id], DIAG_NOTE_CMP_WORD
	mov	[r12 + DiagAdvice.arg_ptr], rax
	mov	[r12 + DiagAdvice.arg_len], rdx
	return

  .k201_near:
	mov	rdi, [spptr]
	mov	rsi, [splen]
	call	diag_span_is_word
	test	eax, eax
	jz	.k201_prev
	mov	rdi, [spptr]
	mov	rsi, [splen]
	call	diag_kw_nearest
	cmp	eax, -1
	je	.k201_prev
	mov	edi, eax
	call	diag_kw_word
	mov	dword [r12 + DiagAdvice.note_id], DIAG_NOTE_DID_YOU_MEAN
	mov	[r12 + DiagAdvice.arg_ptr], rax
	mov	[r12 + DiagAdvice.arg_len], rdx
	return

	; Nothing in the span itself said anything. Look at the word BEFORE the
	; place the parser wants a token inserted: `if a > 10` reports at `a`,
	; because `if` is a legal identifier and only the token after it is
	; unexpected, so the span can never name the mistake and the word
	; before the insertion point always does. diag_prev_word's header
	; carries the bound and the named false positive.
  .k201_prev:
	cmp	qword [rbx + Diag.src_len], 0
	je	.k201_edit
	mov	rdi, [rbx + Diag.src_ptr]
	mov	rsi, [rbx + Diag.src_len]
	cmp	dword [rbx + Diag.fix.kind], DIAG_FIX_INSERT
	jne	.k201_prev_span
	mov	edx, [rbx + Diag.fix.edit.start]
	jmp	.k201_prev_go
  .k201_prev_span:
	mov	edx, [rbx + Diag.span.start]
  .k201_prev_go:
	call	diag_prev_word		; rax = ptr, rdx = len
	test	rdx, rdx
	jz	.k201_edit
	mov	rdi, rax
	mov	rsi, rdx
	call	diag_eng_lookup
	cmp	eax, -1
	je	.k201_edit
	mov	[engrow], eax
	mov	edi, eax
	call	diag_eng_note_id
	mov	[r12 + DiagAdvice.note_id], eax
	mov	edi, [engrow]
	call	diag_eng_kw_word
	mov	[r12 + DiagAdvice.arg_ptr], rax
	mov	[r12 + DiagAdvice.arg_len], rdx
	return

	; Last resort: say what the attached edit says the parser wanted. This
	; is the case that covers a forgotten `;` -- the most common single
	; mistake in the review's corpus and, before this, the one that said
	; nothing at all beyond `unexpected token`.
  .k201_edit:
	cmp	dword [rbx + Diag.fix.kind], DIAG_FIX_INSERT
	jne	.knone
	cmp	qword [fxtl], 1
	jne	.knone
	mov	rcx, [fxtp]
	movzx	eax, byte [rcx]
	cmp	eax, ';'
	je	.k201_semi
	cmp	eax, '}'
	je	.k201_close
	cmp	eax, ')'
	je	.k201_close
	cmp	eax, ']'
	je	.k201_close
	cmp	eax, '>'
	je	.k201_close
	return
  .k201_semi:
	mov	dword [r12 + DiagAdvice.note_id], DIAG_NOTE_STMT_SEMI
	return
  .k201_close:
	mov	dword [r12 + DiagAdvice.note_id], DIAG_NOTE_UNCLOSED
	mov	rax, [fxtp]
	mov	[r12 + DiagAdvice.arg_ptr], rax
	mov	rax, [fxtl]
	mov	[r12 + DiagAdvice.arg_len], rax
	return

	; ---- EXS-E0202 / EXS-E0203, both at or past end of input ----
	; Naming the CONSTRUCT is what this can do from the record alone;
	; naming WHERE it was opened needs `diag_rel_set`, which only the
	; parser can call. Review D6 wanted both; this is the half that does
	; not require the other tree.
  .keof:
	cmp	qword [fxtl], 0
	je	.keof_bare
	mov	dword [r12 + DiagAdvice.note_id], DIAG_NOTE_EOF_OPEN
	mov	rax, [fxtp]
	mov	[r12 + DiagAdvice.arg_ptr], rax
	mov	rax, [fxtl]
	mov	[r12 + DiagAdvice.arg_len], rax
	return
  .keof_bare:
	mov	dword [r12 + DiagAdvice.note_id], DIAG_NOTE_EOF_NOCLOSE
	return

	; ---- EXS-E0210 malformed literal ----
  .k210:
	mov	rdi, [spptr]
	mov	rsi, [splen]
	call	diag_span_is_numsuffix
	test	eax, eax
	jz	.knone
	mov	dword [r12 + DiagAdvice.note_id], DIAG_NOTE_LIT_SUFFIX
	return

	; ---- EXS-E0220 reserved keyword used as identifier ----
  .k220:
	mov	dword [r12 + DiagAdvice.note_id], DIAG_NOTE_RESERVED
	return

  .knone:
	return
endp

; ---- human-readable rendering ----------------------------------------------
; Format, by example (a `|` gutter, rustc-shaped, because it is the shape
; people already read). This is REAL output, from a run of
; tests/unit/diag_render_text.asm's case A, which pins it byte for byte --
; not a sketch:
;
;   exempl\u{202e}um.exs:2:9: error[EXS-E0421]: undeclared capability (atom or row)
;    2 |     sub \u{202e}rete\u{200b} = r
;      |         ^^^^^^^^^^^^^^^^^^^^
;      = fix (replace): bytes 30..40 -> "archivum"
;
; and, with everything the record can carry, from a real run of `exsc` on the
; review's case c02:
;
;   c02.exsc:7:1: error[EXS-E0202]: unterminated construct
;    7 |
;      | ^
;      = note: the file ends inside a construct opened earlier; it wants }
;      = opened here
;    3 |     si a gt 3 {
;      |               ^
;      = suggestion (insert): bytes 108..108 -> "}"
;
; FOUR OPTIONAL BLOCKS, ALWAYS IN THIS ORDER: the source echo, the note, the
; related span, the payload. The order is fixed here in source rather than
; driven by any table, for the reason this file's DETERMINISM paragraph
; gives.
;
; `fix` versus `suggestion` is decided by `diag_fix_required(code_num)` and
; nothing else -- see this file's header for why an advisory edit must never
; be labelled the way a §8.3 fix is.
;
; diag_render_text(diagrec, buf, cap) -> rax = bytes written, rdx = 1 if the
; output was truncated.
proc diag_render_text, diagrec, buf, cap
	uses	rbx
	locals
		slot wr,          32
		slot adv,         24
		slot lin,         dq
		slot col,         dq
		slot hcol_lstart, dq
		slot digits,      dq
		slot idx,         dd
		slot rpad,        dd
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
	call	diag_line_col		; rax = line. rdx (BYTE column) is NOT what
					; the header prints -- see below and
					; util.inc's header.
	mov	[lin], rax

	; ---- header `col` is a 1-based CHARACTER column, the same count the
	; caret run is padded with (D4; util.inc's header, this file's
	; `__diag_col_of`) -- not `diag_line_col`'s byte column above, which
	; `diag_render_json` still reports unchanged. Columns run from this
	; line's own start, so: find that start, then count the terminal
	; columns of the bytes between it and the (clamped) span start with
	; the SAME classifier the caret uses, then +1 for 1-based.
	mov	rdi, [rbx + Diag.src_ptr]
	mov	rsi, [rbx + Diag.src_len]
	mov	edx, [rbx + Diag.span.start]
	call	diag_line_bounds	; rax = this line's start byte offset
	mov	[hcol_lstart], rax

	mov	eax, [rbx + Diag.span.start]	; 32-bit load zero-extends rax --
						; `Diag.span.start` is a `dd`
	cmp	rax, [rbx + Diag.src_len]
	jbe	.hcol_clamped
	mov	rax, [rbx + Diag.src_len]
  .hcol_clamped:
	sub	rax, [hcol_lstart]	; byte length of the line-prefix before
					; the span

	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.src_ptr]
	add	rsi, [hcol_lstart]
	mov	rdx, rax
	call	__diag_col_of		; rax = columns before the span
	inc	rax			; 1-based
	mov	[col], rax

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

	; ---- gutter width: the widest line number this record will print ----
	; A related span three lines above a primary one at line 100 must share
	; a gutter, or the two blocks do not read as one diagnostic.
	mov	rdi, [lin]
	call	__diag_dec_width
	mov	[digits], rax
	test	dword [rbx + Diag.flags], DIAG_F_REL
	jz	.gutter_done
	cmp	qword [rbx + Diag.src_len], 0
	je	.gutter_done
	mov	rdi, [rbx + Diag.src_ptr]
	mov	rsi, [rbx + Diag.src_len]
	mov	edx, [rbx + Diag.rel.start]
	call	diag_line_col
	mov	rdi, rax
	call	__diag_dec_width
	cmp	rax, [digits]
	jbe	.gutter_done
	mov	[digits], rax
  .gutter_done:

	; ---- the source echo (skipped when no source is attached) ----
	cmp	qword [rbx + Diag.src_len], 0
	je	.noteblock
	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.src_ptr]
	mov	rdx, [rbx + Diag.src_len]
	mov	ecx, [rbx + Diag.span.start]
	mov	r8d, [rbx + Diag.span.len]
	mov	r9, [digits]
	call	__diag_echo_line

	; ---- the note ----
  .noteblock:
	mov	rdi, rbx
	lea	rsi, [adv]
	call	__diag_advise
	mov	eax, [adv + DiagAdvice.note_id]
	test	eax, eax
	jz	.relblock

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
	lea	rsi, [diag_lit_note]
	mov	rdx, DIAG_LIT_NOTE_LEN
	call	diag_out_bytes

	mov	edi, [adv + DiagAdvice.note_id]
	call	diag_note_text		; rax = ptr, rdx = len
	mov	rsi, rax
	lea	rdi, [wr]
	call	diag_out_esc

	cmp	qword [adv + DiagAdvice.arg_len], 0
	je	.note_nl
	lea	rdi, [wr]
	lea	rsi, [diag_lit_space]
	mov	rdx, DIAG_LIT_SPACE_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	rsi, [adv + DiagAdvice.arg_ptr]
	mov	rdx, [adv + DiagAdvice.arg_len]
	call	diag_out_esc
  .note_nl:
	lea	rdi, [wr]
	lea	rsi, [diag_lit_nl]
	mov	rdx, DIAG_LIT_NL_LEN
	call	diag_out_bytes

	; ---- the related span ----
  .relblock:
	test	dword [rbx + Diag.flags], DIAG_F_REL
	jz	.fixblock
	cmp	qword [rbx + Diag.src_len], 0
	je	.fixblock

	lea	rdi, [wr]
	mov	esi, ' '
	mov	rdx, [digits]
	inc	rdx
	call	diag_out_fill
	lea	rdi, [wr]
	lea	rsi, [diag_lit_eq]
	mov	rdx, DIAG_LIT_EQ_LEN
	call	diag_out_bytes
	mov	edi, DIAG_NOTE_OPENED_AT
	call	diag_note_text
	mov	rsi, rax
	lea	rdi, [wr]
	call	diag_out_esc
	lea	rdi, [wr]
	lea	rsi, [diag_lit_nl]
	mov	rdx, DIAG_LIT_NL_LEN
	call	diag_out_bytes

	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.src_ptr]
	mov	rdx, [rbx + Diag.src_len]
	mov	ecx, [rbx + Diag.rel.start]
	mov	r8d, [rbx + Diag.rel.len]
	mov	r9, [digits]
	call	__diag_echo_line

	; ---- the payload: a §8.3 fix, or an advisory suggestion ----
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

	mov	edi, [rbx + Diag.code_num]
	call	diag_fix_required
	test	eax, eax
	jz	.label_sugg
	lea	rsi, [diag_lit_fixword]
	mov	rdx, DIAG_LIT_FIXWORD_LEN
	jmp	.label_out
  .label_sugg:
	lea	rsi, [diag_lit_sugword]
	mov	rdx, DIAG_LIT_SUGWORD_LEN
  .label_out:
	lea	rdi, [wr]
	call	diag_out_bytes
	lea	rdi, [wr]
	lea	rsi, [diag_lit_paren]
	mov	rdx, DIAG_LIT_PAREN_LEN
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
;    "note":null,"note_id":0,"related":null,"snippet":"...",
;    "fix":null,"suggestion":null}
;
; and each optional member, when present:
;
;   "note":"did you mean the reserved word functio","note_id":3
;   "related":{"file_id":3,"start":41,"len":1}
;   "fix":{"kind":"replace","file_id":3,"start":30,"len":10,"text":"..."}
;   "suggestion":{"kind":"insert","file_id":3,"start":76,"len":0,"text":";"}
;
; EVERY MEMBER IS ALWAYS PRESENT -- `null` when there is nothing -- so a
; consumer's schema never changes shape between diagnostics. That was already
; true of `fix` and the four members added here keep the rule.
;
; `note_id` IS THE MACHINE HALF OF `note`, and it is what a tool matches on.
; `note` itself is English and, exactly like `message`, may change; §8.3's
; "codes are permanent; text is not" now covers a second permanent integer.
;
; `fix` AND `suggestion` ARE NOT THE SAME MEMBER UNDER TWO NAMES. `fix` is
; §8.3's promise -- capability and lexicon errors, where the edit is
; mechanically derivable -- and is what `exsc emenda` may batch. `suggestion`
; is an edit this compiler offers on any other code; it is shown to a human
; and must not be applied unattended. See this file's header for the
; measurement that forced the split. At most one of the two is ever non-null.
;
; `snippet` is the span's own bytes, escaped; the surrounding line is not
; repeated, because a consumer that wants it has `file`, `start` and `len`
; and can take it.
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
		slot wr,  32
		slot adv, 24
		slot idx, dd
		slot jpad, dd
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

	; ---- note / note_id ----
	mov	rdi, rbx
	lea	rsi, [adv]
	call	__diag_advise

	lea	rdi, [wr]
	lea	rsi, [diag_j_note]
	mov	rdx, DIAG_J_NOTE_LEN
	call	diag_out_bytes
	mov	eax, [adv + DiagAdvice.note_id]
	test	eax, eax
	jnz	.has_note
	lea	rdi, [wr]
	lea	rsi, [diag_j_null]
	mov	rdx, DIAG_J_NULL_LEN
	call	diag_out_bytes
	jmp	.note_done
  .has_note:
	lea	rdi, [wr]
	lea	rsi, [diag_j_quote]
	mov	rdx, DIAG_J_QUOTE_LEN
	call	diag_out_bytes
	mov	edi, [adv + DiagAdvice.note_id]
	call	diag_note_text
	mov	rsi, rax
	lea	rdi, [wr]
	call	diag_out_jstr
	cmp	qword [adv + DiagAdvice.arg_len], 0
	je	.note_close
	lea	rdi, [wr]
	lea	rsi, [diag_lit_space]
	mov	rdx, DIAG_LIT_SPACE_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	rsi, [adv + DiagAdvice.arg_ptr]
	mov	rdx, [adv + DiagAdvice.arg_len]
	call	diag_out_jstr
  .note_close:
	lea	rdi, [wr]
	lea	rsi, [diag_j_quote]
	mov	rdx, DIAG_J_QUOTE_LEN
	call	diag_out_bytes
  .note_done:
	lea	rdi, [wr]
	lea	rsi, [diag_j_noteid]
	mov	rdx, DIAG_J_NOTEID_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [adv + DiagAdvice.note_id]
	call	diag_out_u32

	; ---- related ----
	lea	rdi, [wr]
	lea	rsi, [diag_j_related]
	mov	rdx, DIAG_J_RELATED_LEN
	call	diag_out_bytes
	test	dword [rbx + Diag.flags], DIAG_F_REL
	jnz	.has_rel
	lea	rdi, [wr]
	lea	rsi, [diag_j_null]
	mov	rdx, DIAG_J_NULL_LEN
	call	diag_out_bytes
	jmp	.rel_done
  .has_rel:
	lea	rdi, [wr]
	lea	rsi, [diag_j_fidopen]
	mov	rdx, DIAG_J_FIDOPEN_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.rel.file_id]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_j_start]
	mov	rdx, DIAG_J_START_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.rel.start]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_j_len]
	mov	rdx, DIAG_J_LEN_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	esi, [rbx + Diag.rel.len]
	call	diag_out_u32
	lea	rdi, [wr]
	lea	rsi, [diag_j_objend]
	mov	rdx, DIAG_J_OBJEND_LEN
	call	diag_out_bytes
  .rel_done:

	; ---- snippet: the span's own bytes, clamped into the attached source --
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
	lea	rsi, [diag_j_snip2]
	mov	rdx, DIAG_J_SNIP2_LEN
	call	diag_out_bytes
	lea	rdi, [wr]
	mov	rsi, [rbx + Diag.src_ptr]
	add	rsi, [s0]
	mov	rdx, [s1]
	sub	rdx, [s0]
	call	diag_out_jstr

	; ---- fix and suggestion: at most one of them is non-null ----
	lea	rdi, [wr]
	lea	rsi, [diag_j_fixmemb]
	mov	rdx, DIAG_J_FIXMEMB_LEN
	call	diag_out_bytes
	mov	eax, [rbx + Diag.fix.kind]
	cmp	eax, DIAG_FIX_NONE
	je	.fix_null
	mov	edi, [rbx + Diag.code_num]
	call	diag_fix_required
	test	eax, eax
	jz	.fix_null
	mov	rdi, rbx
	lea	rsi, [wr]
	call	__diag_json_edit
	jmp	.fix_done
  .fix_null:
	lea	rdi, [wr]
	lea	rsi, [diag_j_null]
	mov	rdx, DIAG_J_NULL_LEN
	call	diag_out_bytes
  .fix_done:

	lea	rdi, [wr]
	lea	rsi, [diag_j_suggmemb]
	mov	rdx, DIAG_J_SUGGMEMB_LEN
	call	diag_out_bytes
	mov	eax, [rbx + Diag.fix.kind]
	cmp	eax, DIAG_FIX_NONE
	je	.sugg_null
	mov	edi, [rbx + Diag.code_num]
	call	diag_fix_required
	test	eax, eax
	jnz	.sugg_null
	mov	rdi, rbx
	lea	rsi, [wr]
	call	__diag_json_edit
	jmp	.sugg_done
  .sugg_null:
	lea	rdi, [wr]
	lea	rsi, [diag_j_null]
	mov	rdx, DIAG_J_NULL_LEN
	call	diag_out_bytes
  .sugg_done:

	lea	rdi, [wr]
	lea	rsi, [diag_j_objend]
	mov	rdx, DIAG_J_OBJEND_LEN
	call	diag_out_bytes

	mov	rax, [wr + DiagOut.len]
	mov	edx, [wr + DiagOut.trunc]
	return
endp

; __diag_json_edit(diagrec, wr) -- the edit payload as one JSON object:
; `{"kind":"replace","file_id":3,"start":30,"len":10,"text":"..."}`.
;
; ONE emitter for both the `fix` and the `suggestion` member, deliberately:
; the two differ in what a consumer is allowed to DO with the edit, never in
; what the edit is, and a second emitter is how the two shapes would
; eventually stop matching.
proc __diag_json_edit, diagrec, wr
	uses	rbx, r12
	locals
	endl
	mov	rbx, [diagrec]
	mov	r12, [wr]

	mov	rdi, r12
	lea	rsi, [diag_j_kindopen]
	mov	rdx, DIAG_J_KINDOPEN_LEN
	call	diag_out_bytes
	mov	edi, [rbx + Diag.fix.kind]
	call	diag_fix_kind_str	; rax = ptr, rdx = len
	mov	rsi, rax
	mov	rdi, r12
	call	diag_out_bytes

	mov	rdi, r12
	lea	rsi, [diag_j_fixfid]
	mov	rdx, DIAG_J_FIXFID_LEN
	call	diag_out_bytes
	mov	rdi, r12
	mov	esi, [rbx + Diag.fix.edit.file_id]
	call	diag_out_u32
	mov	rdi, r12
	lea	rsi, [diag_j_start]
	mov	rdx, DIAG_J_START_LEN
	call	diag_out_bytes
	mov	rdi, r12
	mov	esi, [rbx + Diag.fix.edit.start]
	call	diag_out_u32
	mov	rdi, r12
	lea	rsi, [diag_j_len]
	mov	rdx, DIAG_J_LEN_LEN
	call	diag_out_bytes
	mov	rdi, r12
	mov	esi, [rbx + Diag.fix.edit.len]
	call	diag_out_u32

	mov	rdi, r12
	lea	rsi, [diag_j_fixtext]
	mov	rdx, DIAG_J_FIXTEXT_LEN
	call	diag_out_bytes
	mov	rdi, r12
	mov	rsi, [rbx + Diag.fix.text_ptr]
	mov	rdx, [rbx + Diag.fix.text_len]
	call	diag_out_jstr
	mov	rdi, r12
	lea	rsi, [diag_j_textend]
	mov	rdx, DIAG_J_TEXTEND_LEN
	call	diag_out_bytes
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
