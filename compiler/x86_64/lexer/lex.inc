; compiler/x86_64/lexer/lex.inc
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
; THE TOKENIZER. §8.4's token vocabulary over a file that lexer/source.inc has
; already found admissible under §8.1.
;
; A TOKEN STREAM AND NOTHING MORE. There is no parser here and none is coming
; from this module: §15 #9 records that there is no phrase grammar yet and the
; CST is deferred, so anything shaped like a production rule would be invented
; syntax. `lex_run` appends `Tok`s to a `Vec` and stops.
;
; §8.1 IS A GATE, NOT A PHASE. `lex_run` runs lexer/source.inc's pass first and
; DOES NOT TOKENIZE AT ALL if it reported anything. A file with a BOM, invalid
; UTF-8, a CR, a bidi override or a non-NFC run is not admissible source, and
; producing a half-token-stream over it would attach §8.4 spans to text §8.1
; has already said cannot be read. On that path the token vector is left
; exactly as the caller passed it -- empty, in the normal case -- and CF is
; set with the first code in eax. §8.2's checks are different in kind: a
; mixed-script identifier still produces its token, because the token stream
; stays well-formed and the diagnostic is the deliverable.
;
; WHAT `EXS-E0220` CAN AND CANNOT BE RAISED FROM HERE, stated plainly because
; the honest answer is narrower than "the lexer raises E0220".
;
; §8.4 tier 1 says a reserved word is "never usable as an identifier,
; anywhere", so a reserved word always lexes as `TOK_KEYWORD` -- there is no
; lexical state in which `forma` is an identifier. `EXS-E0220` is therefore
; raised at the moment something REQUIRED an identifier and got a keyword, and
; almost all of those positions are grammar (§8.5's own example, `discerne
; forma`, needs to know that a `discerne` scrutinee is an expression). This
; file owns the code, the span and the §8.3 fix payload, in
; `lex_kw_as_ident`, and calls it itself in the one identifier position that
; needs no grammar at all: the name inside an `@nomen` annotation, which §8.4
; lists as a single token. The eventual parser calls the same routine for
; every other position rather than building a second E0220.
;
; THE FIX, AND THE FACT THAT §13 DOES NOT SPECIFY IT. §13 says E0220's "fix is
; mechanically derivable" and stops there; nothing in the spec says WHAT the
; derived edit is. This file derives it as "replace the reserved word with the
; same word suffixed `_`" -- `forma` becomes `forma_`. Two reasons and one
; caveat, all recorded here so the choice is visible rather than absorbed:
;   - It is always lexically valid. §8.2 allows `_` in an identifier, and
;     §8.4's reserved set is closed and contains no word ending in `_`, so the
;     result can never be a different reserved word.
;   - It is derivable with no context, which is what makes it applicable by
;     `exsc emenda` without re-running name resolution.
;   - CAVEAT: for a PUBLIC name §3.1 additionally requires the name to
;     decompose into the morpheme table, and `forma_` may well not -- that is
;     `EXS-E0601`, a different diagnostic with its own fix. The E0220 fix
;     makes the file lex; it does not promise to make it compile.
; If §8.4 or §13 later prescribes a convention, `lex_kw_as_ident` is the one
; place that changes.
;
; `EXS-E0220` is deliberately NOT added to diag/fix.inc's `diag_fixreq_codes`.
; That table is derived from §8.3's two named classes -- capability and
; lexicon errors -- and E0220 is neither; it carries a fix because it can, not
; because §8.3 obliges it to. Widening that set is diag's call on a spec
; amendment, not this file's on a convenience. REPORTED, not done.
;
; MAXIMAL MUNCH, and only where §8.4 attests a two-character token: `..`,
; `->`, `+%`, `+|` are matched before `.`, `-`, `+`. `//` is matched before
; `/`, and a bare `/` is then `EXS-E0201` because every `/` in the spec's own
; Exsecutor source is half of a comment marker. token.inc's PUN_* table
; carries the per-token attestation and the reasoning for what is absent.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1, §8.2, §8.4, §8.5; §13.
; -----------------------------------------------------------------------------

include 'ident.inc'

; ---- emitting a token ------------------------------------------------------

; __lex_push(lx, kind, aux, off, len) -> rax = the new `Tok`. FOR IMMEDIATE
; USE ONLY -- the next push may grow the vector and leave this pointer
; dangling (rt/vec.inc abandons the old block rather than resizing it).
;
; The span is written through rt/span.inc's `span_make`, not by three field
; stores here, so that this file has no independent notion of what a `Span`
; is. Cannot fail; leaves CF clear.
proc __lex_push, lx, kind, aux, off, len
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lx]
	mov	rdi, [rbx + Lexer.toks]
	call	vec_push
	mov	r12, rax
	mov	eax, [kind]
	mov	[r12 + Tok.kind], eax
	mov	eax, [aux]
	mov	[r12 + Tok.aux], eax
	lea	rdi, [r12 + Tok.span]
	mov	esi, [rbx + Lexer.file_id]
	mov	rdx, [off]
	mov	rcx, [len]
	call	span_make
	mov	rax, r12
	return
endp

; ---- EXS-E0220 -------------------------------------------------------------

; lex_kw_as_ident(lx, off, len, kwid) -> rax = the `Diag`, already carrying
; its span and a `DIAG_FIX_REPLACE` payload. PUBLIC: this is the routine the
; eventual parser calls when a grammar position required an identifier and the
; token there was `TOK_KEYWORD`, so that there is exactly one E0220 in the
; compiler. See this file's header for what the fix is and why the spec does
; not choose it.
;
; `off`/`len` are the byte range of the offending word, `kwid` its KW_* id --
; the caller has a `Tok` and every field comes straight off it
; (`Tok.span.start`, `Tok.span.len`, `Tok.aux`). An out-of-range `kwid` traps
; through `kw_text_of`. Cannot fail; leaves CF clear -- recording a diagnostic
; is not itself a failure (see lex_diag's own comment).
proc lex_kw_as_ident, lx, off, len, kwid
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [lx]

	; Derive the replacement text: the reserved word plus one '_'. Copied
	; into the lexer's arena, not into a shared static buffer -- several
	; E0220 diagnostics can be live in the `diags` vector at once, and a
	; static buffer would leave all of them pointing at the last one.
	mov	edi, [kwid]
	call	kw_text_of		; rax = the word, rdx = its length
	mov	r13, rax
	mov	r14, rdx
	mov	rdi, [rbx + Lexer.arena]
	mov	rsi, r14
	inc	rsi
	call	arena_alloc
	mov	r12, rax
	mov	rdi, r12
	mov	rsi, r13
	mov	rcx, r14
	rep	movsb
	mov	byte [r12 + r14], '_'

	mov	rdi, rbx
	mov	esi, 220
	mov	rdx, [off]
	mov	rcx, [len]
	call	lex_diag
	mov	r13, rax

	mov	rdi, r13
	call	diag_fix_ptr
	mov	rdi, rax
	mov	esi, [rbx + Lexer.file_id]
	mov	rdx, [off]
	mov	rcx, [len]
	mov	r8,  r12
	mov	r9,  r14
	inc	r9
	call	diag_fix_replace

	mov	rax, r13
	return
endp

; ---- the §8.4 pass ---------------------------------------------------------

; lex_tokenize(lx) -> eax = the number of diagnostics recorded by THIS call.
; Appends to `Lexer.toks`, always ending with one `TOK_EOF` whose span is the
; zero-length range at end of input -- so a consumer never has to bounds-check
; the vector to know it has reached the end.
;
; Cannot fail in the CF sense and leaves CF clear; see `lex_diag`. Assumes
; lexer/source.inc's pass has already run and reported nothing, which is what
; `lex_run` guarantees: every `__diag_utf8_decode` below therefore `rassert`s
; on failure rather than reporting `EXS-E0101` a second time.
proc lex_tokenize, lx
	uses	rbx, r12, r13, r14
	locals
		slot	cpv,    dd	; codepoint at the cursor
		slot	pad0,   dd
		slot	nbytes, dq	; its encoded length
		slot	tstart, dq	; byte offset this token began at
		slot	nstart, dq	; ... and, for @nomen, where the name did
		slot	before, dd	; ndiag on entry
		slot	pad1,   dd
	endl
	mov	rbx, [lx]
	mov	eax, [rbx + Lexer.ndiag]
	mov	[before], eax
	mov	r12, [rbx + Lexer.src_ptr]
	mov	r13, [rbx + Lexer.src_len]
	xor	r14, r14

  .next:
	cmp	r14, r13
	jae	.eof
	mov	rdi, r12
	add	rdi, r14
	mov	rsi, r13
	sub	rsi, r14
	call	__diag_utf8_decode
	mov	rcx, DIAG_CP_INVALID
	rassert	rax ne rcx		; §8.1's pass already proved this
	mov	[cpv], eax
	mov	[nbytes], rdx

	; ---- layout is not significant (§8.4) ------------------------------
	; Space, tab and LF only. CR cannot reach here (EXS-E0106), and no
	; other C0 control is attested anywhere in Exsecutor source, so one
	; falls through to EXS-E0201 rather than being guessed at as space.
	cmp	eax, 0x20
	je	.skip
	cmp	eax, 0x09
	je	.skip
	cmp	eax, 0x0A
	je	.skip

	cmp	eax, '/'
	je	.slash
	cmp	eax, '"'
	je	.string
	cmp	eax, '@'
	je	.annot
	cmp	eax, '0'
	jb	.not_digit
	cmp	eax, '9'
	jbe	.number
  .not_digit:
	; §8.2: XID_Start, plus `_`.
	cmp	eax, '_'
	je	.ident
	mov	edi, eax
	call	xid_start
	test	eax, eax
	jnz	.ident
	jmp	.punct

  .skip:
	add	r14, [nbytes]
	jmp	.next

	; ---- `//` to end of line (§8.4), or a lone `/` (PUN_SLASH) ---------
	; §8.1's bidi and invisible-control rule has ALREADY been applied to
	; these bytes, by lexer/source.inc, over the raw stream -- which is the
	; whole reason that pass is not token-aware. Nothing is re-scanned
	; here and nothing needs to be.
	;
	; The two-character-first rule still decides what a `/` IS: `//` is a
	; comment (§8.4) and only a `/` that is not the first half of one is
	; the float-only quotient operator, PUN_SLASH (token.inc's PUN table
	; records why `/` is now attested where it used to be EXS-E0201).
  .slash:
	mov	rax, r14
	inc	rax
	cmp	rax, r13
	jae	.slash_one		; a lone '/' at end of input
	cmp	byte [r12 + rax], '/'
	jne	.slash_one
	add	r14, 2
  .comment:
	cmp	r14, r13
	jae	.next
	cmp	byte [r12 + r14], 0x0A
	je	.next			; leave the LF to the whitespace arm
	inc	r14
	jmp	.comment
  .slash_one:
	mov	edx, PUN_SLASH
	mov	ecx, 1
	jmp	.p_emit

	; ---- `"..."` (§8.4) ------------------------------------------------
	; RAW NEWLINES ARE PART OF THE VALUE. §8.4: "A string literal may
	; contain raw newlines ... There is no separate multiline form and no
	; indentation stripping." examples/saluta.exsc depends on it -- its
	; whole return value is a four-line literal -- and stripping would make
	; the value depend on the source's leading whitespace, which is the
	; ambient state §1 exists to remove.
	;
	; `\` escapes the NEXT BYTE, for delimiting only. Byte-wise is exact
	; here even though escapes are about codepoints: every continuation
	; byte of a multi-byte codepoint is >= 0x80 and so can never equal
	; `"` (0x22) or `\` (0x5C), which are the only two bytes this loop
	; reacts to. No escape ALPHABET is assumed -- see token.inc's header.
  .string:
	mov	[tstart], r14
	inc	r14
  .str_scan:
	cmp	r14, r13
	jae	.str_open
	movzx	eax, byte [r12 + r14]
	cmp	al, 0x5C		; '\'
	je	.str_escape
	cmp	al, '"'
	je	.str_close
	inc	r14
	jmp	.str_scan
  .str_escape:
	add	r14, 2
	cmp	r14, r13
	ja	.str_open		; a trailing backslash at end of input
	jmp	.str_scan
  .str_close:
	inc	r14			; the closing quote is part of the span
	mov	rdi, rbx
	mov	esi, TOK_STRING
	xor	edx, edx
	mov	rcx, [tstart]
	mov	r8,  r14
	sub	r8,  rcx
	call	__lex_push
	jmp	.next
  .str_open:
	; EXS-E0202, unterminated construct. §8.4 assigns this code to exactly
	; this case ("an unterminated one is EXS-E0202"). The span runs from
	; the opening quote to end of input, which is the construct that was
	; left open, and scanning stops there because everything after the
	; opening quote has already been consumed as literal content.
	mov	rdi, rbx
	mov	esi, 202
	mov	rdx, [tstart]
	mov	rcx, r13
	sub	rcx, rdx
	call	lex_diag
	mov	r14, r13
	jmp	.next

	; ---- a run of ASCII digits, or `0x` + hex digits (§8.4) ------------
	; See token.inc's header. Decimal is only `0`-`9`, the one form every
	; candidate numeric grammar contains; a run that does not cleanly END
	; is EXS-E0210 rather than a silent tokenization choice.
	;
	; D6 (docs/design/wire-codec.md) settles hex: `0x[0-9a-fA-F]+`, ONE
	; spelling of the prefix. This is entered only when the byte AFTER the
	; leading `0` is exactly lowercase `x` (0x78) and the byte after THAT
	; is a hex digit -- every other case (bare `0x`, capital `0X`, `0`
	; followed by anything else) falls straight through to the unchanged
	; decimal path below, which already reports EXS-E0210 for all of them
	; via the same "digit run into an identifier character" rule this
	; comment described before D6: `X`, and a lowercase `x` with no hex
	; digit after it, are themselves XID_Continue, so `.num_scan`/
	; `.num_done`/`.num_bad` consume and report them exactly as they did
	; before this change -- nothing about that path is touched.
	;
	; The float wave extends the DECIMAL path only (grammar and every
	; reason in token.inc's header): a '.' over a one-byte-peeked digit
	; starts a fraction, a lowercase 'e' starts an exponent, and a literal
	; that ends as anything but `DIGITS`, `DIGITS '.' DIGITS ['e' Exp]` or
	; `DIGITS 'e' Exp` is EXS-E0210, consumed whole. HEX LITERALS HAVE NO
	; FLOAT CONTINUATION -- `.hex_scan` ends at `.num_hexend`, not here,
	; so `0x1e5` is still the hex integer 0x1e5 (its 'e' is a hex digit)
	; and `0x1.5` is three tokens, `0x1` `.` `5`, exactly as before.
  .number:
	mov	[tstart], r14
	cmp	eax, '0'		; eax/[cpv] still the char AT r14 (unclobbered
	jne	.num_scan		; since the dispatch cmp, above)
	lea	rax, [r14 + 1]
	cmp	rax, r13
	jae	.num_scan		; '0' at EOF -- decimal path (emits "0")
	cmp	byte [r12 + rax], 'x'
	jne	.num_scan		; not lowercase 'x' -- decimal path (handles
					; capital '0X...' too, unchanged)
	lea	rcx, [r14 + 2]
	cmp	rcx, r13
	jae	.num_scan		; bare "0x" at EOF -- decimal path reports
					; EXS-E0210 for it, as today
	movzx	edx, byte [r12 + rcx]
	cmp	dl, '0'
	jb	.num_scan
	cmp	dl, '9'
	jbe	.hex_confirmed
	cmp	dl, 'A'
	jb	.num_scan
	cmp	dl, 'F'
	jbe	.hex_confirmed
	cmp	dl, 'a'
	jb	.num_scan
	cmp	dl, 'f'
	ja	.num_scan		; no hex digit after 'x' -- decimal path
					; reports EXS-E0210, as today
  .hex_confirmed:
	add	r14, 2			; consume '0' 'x'; r14 -> first hex digit,
					; already confirmed valid by the peek above
  .hex_scan:
	inc	r14
	cmp	r14, r13
	jae	.num_hexend
	movzx	eax, byte [r12 + r14]
	cmp	al, '0'
	jb	.num_hexend
	cmp	al, '9'
	jbe	.hex_scan
	cmp	al, 'A'
	jb	.num_hexend
	cmp	al, 'F'
	jbe	.hex_scan
	cmp	al, 'a'
	jb	.num_hexend
	cmp	al, 'f'
	jbe	.hex_scan
	jmp	.num_hexend
	; A hex literal ends HERE, before any of the decimal float machinery:
	; `0x1e5`'s 'e' was a hex digit consumed by the loop above, and `.`/`e`
	; after a hex run are not float syntax (the grammar is decimal-only),
	; so the only question left is the one every numeric literal ends
	; with -- did the run cleanly end, or run into an identifier
	; character?
  .num_hexend:
	cmp	r14, r13
	jae	.num_emit
	mov	rdi, r12
	add	rdi, r14
	mov	rsi, r13
	sub	rsi, r14
	call	__diag_utf8_decode
	mov	edi, eax
	call	xid_continue
	test	eax, eax
	jnz	.num_bad
	jmp	.num_emit
  .num_scan:
	inc	r14
	cmp	r14, r13
	jae	.num_done
	movzx	eax, byte [r12 + r14]
	cmp	al, '0'
	jb	.num_done
	cmp	al, '9'
	jbe	.num_scan
  .num_done:
	; DECIMAL RUNS ONLY (hex ended at .num_hexend). The byte at r14, if
	; there is one, decides between the three shapes a decimal literal
	; can take -- integer, fraction, exponent -- and `1.` stays `1` then
	; `.` because the '.' joins only over a peeked digit.
	cmp	r14, r13
	jae	.num_emit		; an integer at end of input
	movzx	eax, byte [r12 + r14]
	cmp	al, '.'
	jne	.num_notdot
	lea	rcx, [r14 + 1]		; one-byte peek (the loop above proved
	cmp	rcx, r13		; r14 < r13, so only this side is
	jae	.num_emit		; guarded): `0..n` is three tokens
	cmp	byte [r12 + rcx], '0'
	jb	.num_emit		; `1.` is `1` then `.` -- `.` is not
	cmp	byte [r12 + rcx], '9'	; XID_Continue so this is the ONLY
	ja	.num_emit		; place a would-be fraction could rejoin
	jmp	.num_frac
  .num_notdot:
	cmp	al, 'e'
	je	.num_exp		; `1e-3`: exponent-only form
	; Neither `.` nor `e`: the run ends here unless it runs into an
	; identifier character.
	mov	rdi, r12
	add	rdi, r14
	mov	rsi, r13
	sub	rsi, r14
	call	__diag_utf8_decode
	mov	edi, eax
	call	xid_continue
	test	eax, eax
	jnz	.num_bad
	jmp	.num_emit
	; ---- the fraction: DIGITS '.' DIGITS ------------------------------
	; The '.' was consumed by .num_done's peek only when a digit follows,
	; so .num_frac enters pointing at a confirmed digit.
  .num_frac:
	inc	r14			; consume the '.'
  .num_fscan:
	cmp	r14, r13
	jae	.num_flo		; cannot happen (the peek above), but the
	movzx	eax, byte [r12 + r14]	; loop keeps its own bounds check
	cmp	al, '0'
	jb	.num_flo
	cmp	al, '9'
	ja	.num_flo
	inc	r14
	jmp	.num_fscan
  .num_flo:
	cmp	r14, r13
	jae	.num_emitf		; `1.5` at end of input
	cmp	al, 'e'
	je	.num_exp		; `1.5e-3`
	mov	rdi, r12
	add	rdi, r14
	mov	rsi, r13
	sub	rsi, r14
	call	__diag_utf8_decode
	mov	edi, eax
	call	xid_continue
	test	eax, eax
	jnz	.num_bad
	jmp	.num_emitf
	; ---- the exponent: 'e' ('+'|'-')? DIGITS{1,3} ---------------------
	; Arrived at with r14 -> the confirmed lowercase 'e' (from .num_done
	; or .num_flo) -- capital `E` never reaches here; it is
	; XID_Continue, so it takes the identifier-character rule to
	; EXS-E0210, the hex prefix's one-spelling discipline (D6).
	;
	; An exponent that does not cleanly end -- `1e`, `1e+`, more than
	; three digits, or any run into an identifier character -- takes the
	; WHOLE literal to EXS-E0210 via .num_bad, reported once.
  .num_exp:
	inc	r14			; consume the 'e'
	cmp	r14, r13
	jae	.num_bad
	movzx	eax, byte [r12 + r14]
	cmp	al, '+'
	je	.num_esign
	cmp	al, '-'
	jne	.num_edig
  .num_esign:
	inc	r14			; consume the sign
  .num_edig:
	xor	ecx, ecx		; exponent digit count; ecx stays live to
					; .num_echk (no call intervenes)
  .num_eloop:
	cmp	r14, r13
	jae	.num_echk
	movzx	eax, byte [r12 + r14]
	cmp	al, '0'
	jb	.num_echk
	cmp	al, '9'
	ja	.num_echk
	inc	r14
	inc	rcx
	jmp	.num_eloop
  .num_echk:
	test	rcx, rcx
	jz	.num_bad		; `1e`, `1e+`, `1ex` -- no exponent digit
	cmp	rcx, 3
	ja	.num_bad		; `1e1234` -- more than three digits
	; The loop only exits on a non-digit or end of input, so the
	; exponent count is final and the last question is the identifier
	; run.
	cmp	r14, r13
	jae	.num_emitf
	mov	rdi, r12
	add	rdi, r14
	mov	rsi, r13
	sub	rsi, r14
	call	__diag_utf8_decode
	mov	edi, eax
	call	xid_continue
	test	eax, eax
	jnz	.num_bad		; `1e3f32`, `1.5e2x`
  .num_emitf:
	; aux = 1: a FLOAT literal. The bytes are the literal's own source
	; text, undecoded -- token.inc's header records that the checker
	; (not the lexer) reads them, and the expectation settles the width.
	mov	rdi, rbx
	mov	esi, TOK_NUMBER
	mov	edx, 1
	mov	rcx, [tstart]
	mov	r8,  r14
	sub	r8,  rcx
	call	__lex_push
	jmp	.next
	; The literal runs into an identifier character: `0X10` (capital X),
	; `0x1G`, `1_000`, `1E5` (capital `E` -- lowercase is the float
	; exponent now), `10u32`, `1.5f32`. Consume the whole run so this is
	; reported once, then emit no token -- there is no token shape to
	; emit it as.
  .num_bad:
	cmp	r14, r13
	jae	.num_report
	mov	rdi, r12
	add	rdi, r14
	mov	rsi, r13
	sub	rsi, r14
	call	__diag_utf8_decode
	mov	[nbytes], rdx
	cmp	eax, '_'
	je	.num_bad_step
	mov	edi, eax
	call	xid_continue
	test	eax, eax
	jz	.num_report
  .num_bad_step:
	add	r14, [nbytes]
	jmp	.num_bad
  .num_report:
	mov	rdi, rbx
	mov	esi, 210
	mov	rdx, [tstart]
	mov	rcx, r14
	sub	rcx, rdx
	call	lex_diag
	jmp	.next
  .num_emit:
	mov	rdi, rbx
	mov	esi, TOK_NUMBER
	xor	edx, edx
	mov	rcx, [tstart]
	mov	r8,  r14
	sub	r8,  rcx
	call	__lex_push
	jmp	.next

	; ---- `@nomen` (§8.4) -----------------------------------------------
	; One token, as §8.4's table writes it, with `aux` the intern id of the
	; NAME and the span covering the `@` as well.
  .annot:
	mov	[tstart], r14
	inc	r14
	cmp	r14, r13
	jae	.annot_bare
	mov	rdi, r12
	add	rdi, r14
	mov	rsi, r13
	sub	rsi, r14
	call	__diag_utf8_decode
	mov	[cpv], eax
	mov	[nbytes], rdx
	cmp	eax, '_'
	je	.annot_name
	mov	edi, eax
	call	xid_start
	test	eax, eax
	jz	.annot_bare
  .annot_name:
	mov	[nstart], r14
	add	r14, [nbytes]
	mov	rdi, rbx
	call	__lex_scan_continue	; extends r14 over XID_Continue|'_'
	mov	r14, rax
	; A reserved word here IS a reserved word in an identifier position,
	; and it needs no grammar to see -- see this file's header.
	mov	rdi, r12
	add	rdi, [nstart]
	mov	rsi, r14
	sub	rsi, [nstart]
	call	kw_lookup
	test	eax, eax
	jz	.annot_ok
	mov	r8d, eax		; the KW_* id, before rdi/rsi are reused
	mov	rdi, rbx
	mov	rsi, [nstart]
	mov	rdx, r14
	sub	rdx, rsi
	mov	rcx, r8
	call	lex_kw_as_ident
	jmp	.next
  .annot_ok:
	mov	rdi, [rbx + Lexer.interner]
	mov	rsi, r12
	add	rsi, [nstart]
	mov	rdx, r14
	sub	rdx, [nstart]
	call	intern_id
	mov	rdi, rbx
	mov	esi, TOK_ANNOT
	mov	edx, eax
	mov	rcx, [tstart]
	mov	r8,  r14
	sub	r8,  rcx
	call	__lex_push
	jmp	.next
  .annot_bare:
	; `@` with no name after it. EXS-E0201 spans just the sigil; the next
	; codepoint is left for the main loop, which will have its own opinion
	; about it.
	mov	rdi, rbx
	mov	esi, 201
	mov	rdx, [tstart]
	mov	rcx, 1
	call	lex_diag
	mov	r14, [tstart]
	inc	r14
	jmp	.next

	; ---- identifiers and reserved words (§8.2, §8.4) -------------------
  .ident:
	mov	[tstart], r14
	add	r14, [nbytes]
	mov	rdi, rbx
	call	__lex_scan_continue
	mov	r14, rax

	mov	rdi, r12
	add	rdi, [tstart]
	mov	rsi, r14
	sub	rsi, [tstart]
	call	kw_lookup
	test	eax, eax
	jz	.plain_ident
	mov	rdi, rbx
	mov	esi, TOK_KEYWORD
	mov	edx, eax
	mov	rcx, [tstart]
	mov	r8,  r14
	sub	r8,  rcx
	call	__lex_push
	jmp	.next

  .plain_ident:
	; §8.2's UTS #39 restriction level. The token is emitted either way:
	; a mixed-script identifier is a diagnostic about the NAME, not a
	; reason to leave a hole in the token stream.
	mov	rdi, r12
	add	rdi, [tstart]
	mov	rsi, r14
	sub	rsi, [tstart]
	call	lex_script_ok
	test	eax, eax
	jnz	.ident_scripts_ok
	mov	rdi, rbx
	mov	esi, 104
	mov	rdx, [tstart]
	mov	rcx, r14
	sub	rcx, rdx
	call	lex_diag
  .ident_scripts_ok:
	; §8.2: "Comparison is byte equality after NFC." The bytes ARE NFC --
	; lexer/source.inc proved it for the whole file and `lex_run` refuses
	; to reach here otherwise -- so the raw source bytes are the correct
	; interning key. See token.inc's header.
	mov	rdi, [rbx + Lexer.interner]
	mov	rsi, r12
	add	rsi, [tstart]
	mov	rdx, r14
	sub	rdx, [tstart]
	call	intern_id
	mov	rdi, rbx
	mov	esi, TOK_IDENT
	mov	edx, eax
	mov	rcx, [tstart]
	mov	r8,  r14
	sub	r8,  rcx
	call	__lex_push
	jmp	.next

	; ---- operators, sigils and delimiters ------------------------------
	; Two-character forms first, so `..` never lexes as two `.`; see this
	; file's header on maximal munch, and token.inc's PUN_* table for where
	; each of these is attested.
  .punct:
	mov	eax, [cpv]
	cmp	eax, 0x80
	jae	.unexpected		; no non-ASCII operator is attested
	cmp	al, '.'
	je	.p_dot
	cmp	al, '-'
	je	.p_minus
	cmp	al, '+'
	je	.p_plus
	mov	edx, PUN_AMP
	cmp	al, '&'
	je	.p_one
	mov	edx, PUN_STAR
	cmp	al, '*'
	je	.p_one
	mov	edx, PUN_COLON
	cmp	al, ':'
	je	.p_one
	mov	edx, PUN_QUESTION
	cmp	al, '?'
	je	.p_one
	mov	edx, PUN_LT
	cmp	al, '<'
	je	.p_one
	mov	edx, PUN_GT
	cmp	al, '>'
	je	.p_one
	mov	edx, PUN_LBRACE
	cmp	al, '{'
	je	.p_one
	mov	edx, PUN_RBRACE
	cmp	al, '}'
	je	.p_one
	mov	edx, PUN_LPAREN
	cmp	al, '('
	je	.p_one
	mov	edx, PUN_RPAREN
	cmp	al, ')'
	je	.p_one
	mov	edx, PUN_LBRACKET
	cmp	al, '['
	je	.p_one
	mov	edx, PUN_RBRACKET
	cmp	al, ']'
	je	.p_one
	mov	edx, PUN_COMMA
	cmp	al, ','
	je	.p_one
	mov	edx, PUN_SEMI
	cmp	al, ';'
	je	.p_one
	mov	edx, PUN_EQ
	cmp	al, '='
	je	.p_one
	jmp	.unexpected

  .p_dot:
	mov	edx, PUN_DOT
	mov	ecx, 1
	mov	rax, r14
	inc	rax
	cmp	rax, r13
	jae	.p_emit
	cmp	byte [r12 + rax], '.'
	jne	.p_emit
	mov	edx, PUN_DOTDOT
	mov	ecx, 2
	jmp	.p_emit

  .p_minus:
	mov	edx, PUN_MINUS
	mov	ecx, 1
	mov	rax, r14
	inc	rax
	cmp	rax, r13
	jae	.p_emit
	movzx	r8d, byte [r12 + rax]
	cmp	r8b, '>'
	jne	.m_not_arrow
	mov	edx, PUN_ARROW
	mov	ecx, 2
	jmp	.p_emit
  .m_not_arrow:
	cmp	r8b, '%'			; §5.4 wrapping
	jne	.m_not_pct
	mov	edx, PUN_MINUSPCT
	mov	ecx, 2
	jmp	.p_emit
  .m_not_pct:
	cmp	r8b, '|'			; §5.4 saturating
	jne	.p_emit
	mov	edx, PUN_MINUSBAR
	mov	ecx, 2
	jmp	.p_emit

  .p_plus:
	mov	edx, PUN_PLUS
	mov	ecx, 1
	mov	rax, r14
	inc	rax
	cmp	rax, r13
	jae	.p_emit
	movzx	r8d, byte [r12 + rax]
	cmp	r8b, '%'
	jne	.p_not_pct
	mov	edx, PUN_PLUSPCT
	mov	ecx, 2
	jmp	.p_emit
  .p_not_pct:
	cmp	r8b, '|'
	jne	.p_emit
	mov	edx, PUN_PLUSBAR
	mov	ecx, 2
	jmp	.p_emit

  .p_one:
	mov	ecx, 1
  .p_emit:
	; edx = PUN_*, ecx = the token's byte length
	mov	[tstart], rcx		; borrow the slot for the length
	mov	rdi, rbx
	mov	esi, TOK_PUNCT
	mov	rcx, r14
	mov	r8,  [tstart]
	call	__lex_push
	add	r14, [tstart]
	jmp	.next

  .unexpected:
	; EXS-E0201, unexpected token. This is the deliberate landing place for
	; every codepoint §8.4 does not attest -- a bare `%`, `|`, `!`, `~`, a
	; stray C0 control, a lone combining mark at the start of a token. (A
	; bare `/` used to live here; it is PUN_SLASH now -- see .slash.)
	; Inventing a token kind for any of them would be inventing syntax;
	; §13 registers E0201 for exactly this and the span says which
	; codepoint it was.
	mov	rdi, rbx
	mov	esi, 201
	mov	rdx, r14
	mov	rcx, [nbytes]
	call	lex_diag
	add	r14, [nbytes]
	jmp	.next

  .eof:
	mov	rdi, rbx
	mov	esi, TOK_EOF
	xor	edx, edx
	mov	rcx, r13
	xor	r8, r8
	call	__lex_push
	mov	eax, [rbx + Lexer.ndiag]
	sub	eax, [before]
	return
endp

; __lex_scan_continue(lx) -> rax = the byte offset one past the last
; XID_Continue-or-`_` codepoint starting at r14.
;
; TAKES ITS CURSOR IN r14 AND RETURNS IT IN rax, which is not this tree's
; convention and is called out rather than left to be discovered: it exists
; only to be called from `lex_tokenize`'s two identifier arms, both of which
; already hold the source pointer and length in r12/r13 and the cursor in
; r14, and passing four values it can read for itself would be worse. It is
; `__`-prefixed for that reason. r12, r13 and r14 are callee-saved, so they
; are exactly as valid here as at the call site. Cannot fail; leaves CF clear.
proc __lex_scan_continue, lx
	uses	rbx, r12, r13, r14
	locals
		slot	n, dq
	endl
	mov	rbx, [lx]
	mov	r12, [rbx + Lexer.src_ptr]
	mov	r13, [rbx + Lexer.src_len]
  .step:
	cmp	r14, r13
	jae	.done
	mov	rdi, r12
	add	rdi, r14
	mov	rsi, r13
	sub	rsi, r14
	call	__diag_utf8_decode
	mov	rcx, DIAG_CP_INVALID
	rassert	rax ne rcx
	mov	[n], rdx
	cmp	eax, '_'
	je	.take
	mov	edi, eax
	call	xid_continue
	test	eax, eax
	jz	.done
  .take:
	add	r14, [n]
	jmp	.step
  .done:
	mov	rax, r14
	return
endp

; ---- the whole lexer -------------------------------------------------------

; lex_run(lx) -> CF clear and eax = the numeric part of the first diagnostic
; (0 if none) once the file is admissible §8.1 source and has therefore been
; tokenized -- REGARDLESS of whether that tokenize pass itself recorded any
; §8.2/§8.4 diagnostic along the way; CF set and eax = the numeric part of
; the first diagnostic if the file failed the §8.1 GATE and so was never
; tokenized at all -- no `Tok` exists to hand a parser.
;
; THE FIX FOR A BUG FOUND BY RUNNING: an earlier revision set CF (and jumped
; to the same `fail`) whenever `Lexer.ndiag` was nonzero after EITHER phase,
; not just after `lex_source_check`. That collapsed two different facts onto
; one bit. `lex_source_check` failing means what this file's header says --
; "§8.1 IS A GATE, NOT A PHASE" -- no bytes were tokenized and there is no
; stream to parse. `lex_tokenize` recording a diagnostic means something
; smaller: ONE construct was malformed, `lex_tokenize` still ran to EOF (its
; own header: "Cannot fail in the CF sense... Assumes lexer/source.inc's pass
; has already run and reported nothing"), and `Lexer.toks` holds a complete,
; well-formed stream over everything else in the file -- exactly the same
; "the token stream stays well-formed and the diagnostic is the deliverable"
; principle §8.2's mixed-script check already relies on, just not previously
; honoured here for §8.4.
;
; `driver/run.inc` calls this once and does `jc .gate_failed` to decide
; whether to hand the token stream to `cst_parse` at all -- and cst_parse is
; where a reserved word in a general (non-`@nomen`) identifier position
; raises `EXS-E0220`, via this file's own `lex_kw_as_ident`. With the old,
; over-broad CF, a single unrelated `EXS-E0210` (malformed numeric literal)
; anywhere in a file set CF, `driver/run.inc` treated that as "never
; tokenized" and skipped `cst_parse` outright -- silently discarding every
; diagnostic `cst_parse` would otherwise have raised, `EXS-E0220` included,
; regardless of which mistake came first in the file. Confirmed directly: a
; standalone `lex_run`/`lex_tokenize` call over a file with both an `@nomen`
; keyword (§8.4's one lexer-only source of `EXS-E0220`) and a malformed
; numeric literal already recorded BOTH diagnostics in `Lexer.diags`, in
; source order -- `lex_tokenize`'s scan loop was never the problem. Only this
; routine's CF was.
;
; A caller that wants to know whether the run was PERFECTLY clean, not just
; whether a token stream exists, reads `Lexer.ndiag` after a CF-clear return
; rather than trusting `jc` alone -- `driver/cli.inc`'s `drv_ident_ok` is
; exactly such a caller (REPORTED to the driver agent: it currently branches
; on this routine's CF alone to reject a `--env` key with ANY diagnostic,
; §8.2's mixed-script rule included, and needs updating to check
; `Lexer.ndiag` explicitly now that CF no longer covers that case).
;
; This is channel 1 of docs/asm-conventions.md's "1.3 Error protocol: CF /
; eax" -- diagnosable about the source being compiled -- narrowed to mean
; "unreadable", not "imperfect" -- and it is the ONLY routine in this module
; that speaks it, so a caller writes one `jc` rather than one per phase.
;
; Every diagnostic, not just the first, is in the `diags` vector, in source
; order, on EITHER path. The count is `Lexer.ndiag`, and `Vec.len` on that
; vector agrees with it for a single run.
;
; Resets `ndiag`/`firstcode` but NOT the vectors: a caller may deliberately
; accumulate several files' tokens or diagnostics, and rt/vec.inc gives no
; truncate operation to call even if this routine wanted to.
proc lex_run, lx
	uses	rbx
	locals
	endl
	mov	rbx, [lx]
	mov	dword [rbx + Lexer.ndiag],     0
	mov	dword [rbx + Lexer.firstcode], 0

	mov	rdi, rbx
	call	lex_source_check
	cmp	dword [rbx + Lexer.ndiag], 0
	jne	.rejected		; the §8.1 GATE -- no tokens exist at all

	mov	rdi, rbx
	call	lex_tokenize
	; `lex_tokenize` always runs to EOF and leaves a complete token stream,
	; whether or not it recorded a §8.2/§8.4 diagnostic along the way -- see
	; this routine's header. CF reports only the gate outcome above; eax
	; still carries the first code recorded by EITHER phase, so a caller
	; that only inspects eax (never `jc`) sees no change at all.
	mov	eax, [rbx + Lexer.firstcode]
	return
  .rejected:
	; `fail` with a REGISTER rather than a literal: the macro expands to
	; `mov eax, <operand>` and the code is only known at runtime. Still the
	; sanctioned exit -- it routes through `endp`'s single epilogue, so the
	; `uses` pop happens (docs/asm-conventions.md: never `ret` by hand).
	mov	edx, [rbx + Lexer.firstcode]
	fail	edx
endp
