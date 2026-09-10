; compiler/x86_64/lexer/source.inc
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
; §8.1 SOURCE POLICY, ENFORCED OVER THE RAW BYTE STREAM.
;
; This is the file that decides whether a source file is admissible at all.
; It runs BEFORE tokenization and its rules are not scoped to any token class,
; which is the whole point:
;
;   > Bidi and invisible controls ... anywhere in source, INCLUDING strings
;   > and comments, are `EXS-E0103`.   -- §8.1
;   > §8.1's bidi and invisible-control rule applies INSIDE them -- that is
;   > the Trojan Source class, and a lexer that scans only identifiers is
;   > defeated by an override sitting in a comment.   -- §8.4
;
; §2.2 names the CVEs (CVE-2021-42574 bidi, CVE-2021-42694 homoglyph). The
; scan below therefore decodes and classifies EVERY codepoint in the file,
; with no notion yet of what is a comment, a literal or an identifier -- there
; is nothing for an attacker to hide behind because there is nothing to hide
; behind yet. tests/unit/lexer_trojan_source.asm plants an override inside a
; `//` comment and, separately, inside a `"..."` literal, and both are caught.
;
; THE FIVE RULES, AND WHAT EACH ONE DOES ON A HIT:
;
;   EXS-E0101  a BOM at offset 0        report, skip the three bytes, CONTINUE
;   EXS-E0101  invalid UTF-8            report, STOP (see "fatal", below)
;   EXS-E0106  a CR byte                report, STOP
;   EXS-E0103  a dangerous codepoint    report, CONTINUE -- every occurrence
;   EXS-E0102  a non-NFC run            report ONCE per file, CONTINUE
;
; FATAL VS. CONTINUING is a judgement and is recorded as one. Invalid UTF-8
; means every subsequent byte offset is a guess: resynchronising would emit a
; cascade of spans into text this compiler cannot read, so the first bad byte
; ends the scan. A CR ends it for a different reason -- §8.1 puts the fix
; outside the compiler ("The formatter converts; the compiler does not"), the
; violation is per-line and would otherwise report once per line of the file,
; and every span after the first CR is about a file the user is about to
; rewrite anyway. `EXS-E0103` is the opposite case: an audit wants EVERY
; planted override listed, not the first, so that one never stops the scan.
;
; A LONE CR IS ALSO `EXS-E0106`. §8.1 and §13 both name the code for "CRLF",
; and a lone CR is the same violation of the same rule -- "LF only". Inventing
; a separate code for it is forbidden (CLAUDE.md, "Error codes are permanent":
; §13 is the only source), and silently accepting it would mean the one thing
; §8.1 rules out about line endings is not actually ruled out. The span is two
; bytes for a CRLF pair and one for a lone CR, so the diagnostic still says
; exactly which it found.
;
; ONE UTF-8 DECODER, NOT TWO. This file calls diag/escape.inc's
; `__diag_utf8_decode`. It is `__`-prefixed and is that file's own detail,
; and reaching across for it is a deliberate choice with a stated reason: the
; decoder IS the `EXS-E0101` predicate -- "is this valid UTF-8" -- and it is
; also what the diagnostic renderer uses to decide what to escape. Two
; independently maintained strict decoders is exactly the drift escape.inc's
; own header argues against for the escaping classifier ("two independently
; maintained classifiers is exactly how a call site ends up being the one that
; forgot U+061C"), one level down. It is strict in every case §8.1 needs --
; stray continuation byte, 0xF8-0xFF, all three overlong forms, surrogate
; halves, past U+10FFFF, and truncation at end of input -- verified by reading
; it and exercised by tests/unit/lexer_source_policy.asm's invalid-UTF-8 rows.
; REPORTED to the diag agent: promoting it to a public `diag_utf8_decode` is
; the right follow-up; this file is not permitted to rename it.
;
; SIMILARLY ONE DANGEROUS-CODEPOINT CLASSIFIER. `diag_is_dangerous_cp` is
; already public and its own comment invites this ("the lexer will need this
; exact same class to raise EXS-E0103 in the first place ... prefer including
; this file and calling this proc over re-deriving the ranges"). This file
; contains no copy of the U+202A-202E / U+2066-2069 / U+200B-200F / U+061C
; ranges, on purpose.
;
; `[OPEN]` U+FEFF AWAY FROM OFFSET 0. §8.1 makes a BOM at the start of a file
; `EXS-E0101`, and its dangerous class does not contain U+FEFF, so a ZERO
; WIDTH NO-BREAK SPACE sitting in the middle of an identifier or a comment is
; accepted here. That is what §8.1 says today; whether it is what §8.1 means
; is a spec question, not this file's to decide (CLAUDE.md: never invent a
; code, and the class is enumerated in §8.1). Recorded rather than quietly
; widened.
;
; HOW NFC IS CHECKED WITHOUT A WHOLE-FILE CODEPOINT BUFFER. `nfc_is_nfc` takes
; a u32 array, so a naive implementation would decode the entire file into
; 4 bytes per codepoint plus a 16-bytes-per-codepoint normalisation scratch --
; twenty times the file size, from one non-ASCII character onwards. Instead
; the file is split into segments at NFC-safe boundaries and each segment is
; checked on its own.
;
;   EVERY ASCII CODEPOINT IS A SAFE BOUNDARY. Three facts, each of which has
;   to hold: (1) no composition pair's SECOND element is ASCII -- second
;   elements are combining marks and Hangul V/T jamos -- so nothing before an
;   ASCII character can compose with it or across it; (2) ASCII has ccc 0, so
;   it terminates any run of non-starters the canonical-ordering step could
;   reorder; (3) no ASCII character has a canonical decomposition, so nothing
;   it expands into can reach across either.
;
; A segment is therefore one ASCII codepoint plus the run of non-ASCII
; codepoints that follows it, and a segment with no non-ASCII codepoint in it
; needs no check at all -- so a pure-ASCII source file, which is every example
; in this repository, costs exactly zero NFC work and zero buffer.
;
; The buffers are arena-allocated in `lex_init`, not module-level statics.
; Nothing under `compiler/x86_64/` has module-level mutable state and this
; file does not introduce the first -- which is the same rule §4.1/`EXS-E0500`
; imposes on the language, applied to the compiler.
;
; `[UNIMPLEMENTED]`, stated precisely rather than buried: a run of more than
; LEX_NFC_SEG_CAP-1 consecutive codepoints containing no NFC-safe boundary is
; checked in overlapping windows instead of exactly. UAX #15's Stream-Safe
; Text Format bounds a run of non-starters at 30, so LEX_NFC_OVERLAP = 64
; retained codepoints cover every composition that can cross a window edge in
; stream-safe text; text that is not stream-safe can in principle hide a
; non-NFC sequence from this check. It errs toward accepting (a false
; negative on `EXS-E0102`), never toward a false `EXS-E0102`, and it does not
; touch `EXS-E0103`, which is exact, byte-level and unbuffered.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1 (the rules), §8.3 (spans), §2.2
; (the Trojan Source class), §13 (the codes).
; -----------------------------------------------------------------------------

include 'token.inc'

; Codepoints buffered per NFC segment, and how many are retained when a
; segment overruns with no safe boundary in it. See this file's header.
LEX_NFC_SEG_CAP = 256
LEX_NFC_OVERLAP = 64
; `nfc_normalize` requires dstcap >= srclen * UNI_NFC_MAX_EXPANSION, and
; `nfc_is_nfc` forwards its `scratchcap` to it unchanged.
LEX_NFC_SCR_CAP = LEX_NFC_SEG_CAP * UNI_NFC_MAX_EXPANSION

; ---- the lexer's state -----------------------------------------------------
; Caller-allocated, like every other struct in this compiler (rt/vec.inc's
; `Vec`, diag/render.inc's `Diag`). `src_ptr`/`path_ptr` are BORROWED: `exsc`
; owns one text buffer per input file for the whole compilation (rt/span.inc's
; header), and both the tokens and the diagnostics point into it rather than
; copying it.
;
; `file_id` IS AN INTERN ID, never a file descriptor and never a pointer --
; rt/span.inc's header is explicit about why, and `lex_set_source` obtains it
; by interning the path exactly once.
struct Lexer
	field src_ptr,  dq	; borrowed source bytes
	field src_len,  dq
	field path_ptr, dq	; borrowed path text, for rendering
	field path_len, dq
	field arena,    dq	; -> Arena: NFC buffers, EXS-E0220 fix text
	field interner, dq	; -> Interner: the path id and every identifier
	field toks,     dq	; -> Vec of Tok, stride sizeof.Tok
	field diags,    dq	; -> Vec of Diag, stride sizeof.Diag
	field nfc_cp,   dq	; u32[LEX_NFC_SEG_CAP]  -- current segment
	field nfc_off,  dq	; u32[LEX_NFC_SEG_CAP]  -- its byte offsets
	field nfc_scr,  dq	; u32[LEX_NFC_SCR_CAP]  -- nfc_is_nfc's scratch
	field file_id,  dd	; intern id of path_ptr/path_len
	field ndiag,    dd	; diagnostics recorded this run
	field firstcode, dd	; numeric part of the first one; 0 = none
	field nfcdone,  dd	; 1 once EXS-E0102 has been reported
	field segoff,   dd	; byte offset the current NFC segment starts at
	field segn,     dd	; codepoints buffered in it
	field segwide,  dd	; 1 if it holds any non-ASCII codepoint
	field pad0,     dd
end struct

; ---- construction ----------------------------------------------------------

; lex_init(lx, arena, interner, toks, diags) -- prepare a `Lexer`. `toks` and
; `diags` must already be `vec_init`ed with stride `sizeof.Tok` and
; `sizeof.Diag` respectively; this routine does not initialise them, because a
; caller may legitimately want one vector to outlive several files.
;
; NEVER pass the interner's own arena as `arena`: rt/intern.inc's header is
; explicit that an interned id is valid only as long as the arena backing it
; is not reset, and this arena holds per-file scratch a caller may well want
; to reset between files.
proc lex_init, lx, arena, interner, toks, diags
	uses	rbx
	locals
	endl
	mov	rbx, [lx]
	mov	qword [rbx + Lexer.src_ptr],  0
	mov	qword [rbx + Lexer.src_len],  0
	mov	qword [rbx + Lexer.path_ptr], 0
	mov	qword [rbx + Lexer.path_len], 0
	mov	rax, [arena]
	mov	[rbx + Lexer.arena], rax
	mov	rax, [interner]
	mov	[rbx + Lexer.interner], rax
	mov	rax, [toks]
	mov	[rbx + Lexer.toks], rax
	mov	rax, [diags]
	mov	[rbx + Lexer.diags], rax
	mov	dword [rbx + Lexer.file_id],   0
	mov	dword [rbx + Lexer.ndiag],     0
	mov	dword [rbx + Lexer.firstcode], 0
	mov	dword [rbx + Lexer.nfcdone],   0
	mov	dword [rbx + Lexer.segoff],    0
	mov	dword [rbx + Lexer.segn],      0
	mov	dword [rbx + Lexer.segwide],   0
	mov	dword [rbx + Lexer.pad0],      0

	mov	rdi, [arena]
	mov	rsi, LEX_NFC_SEG_CAP * 4
	call	arena_alloc
	mov	[rbx + Lexer.nfc_cp], rax
	mov	rdi, [arena]
	mov	rsi, LEX_NFC_SEG_CAP * 4
	call	arena_alloc
	mov	[rbx + Lexer.nfc_off], rax
	mov	rdi, [arena]
	mov	rsi, LEX_NFC_SCR_CAP * 4
	call	arena_alloc
	mov	[rbx + Lexer.nfc_scr], rax
	return
endp

; lex_set_source(lx, ptr, len, path, plen) -- point the lexer at one file's
; bytes and its name, and intern the name to obtain this file's `file_id`.
; Both buffers are borrowed and must outlive every token and diagnostic
; produced from them. Cannot fail; leaves CF clear.
proc lex_set_source, lx, ptr, len, path, plen
	uses	rbx
	locals
	endl
	mov	rbx, [lx]
	mov	rax, [ptr]
	mov	[rbx + Lexer.src_ptr], rax
	mov	rax, [len]
	mov	[rbx + Lexer.src_len], rax
	mov	rax, [path]
	mov	[rbx + Lexer.path_ptr], rax
	mov	rax, [plen]
	mov	[rbx + Lexer.path_len], rax
	mov	rdi, [rbx + Lexer.interner]
	mov	rsi, [path]
	mov	rdx, [plen]
	call	intern_id
	mov	[rbx + Lexer.file_id], eax
	return
endp

; ---- recording a diagnostic ------------------------------------------------

; lex_diag(lx, code_num, off, len) -> rax = the new `Diag`, already carrying
; its span and its source attachment, for a caller that wants to add a fix
; payload. FOR IMMEDIATE USE ONLY: the next `lex_diag` may grow the vector,
; which reallocates it and leaves this pointer dangling (rt/vec.inc's growth
; abandons the old block rather than resizing in place).
;
; `code_num` is the NUMERIC part of an `EXS-E` code -- 101, not "EXS-E0101" --
; exactly as docs/asm-conventions.md's "1.3 Error protocol: CF / eax"
; defines it and as diag/codes.inc's `diag_code_numeric` table keys on. Every
; value passed here is a code §13 already registers; this file invents none.
;
; Cannot fail; leaves CF clear. Recording a diagnostic is not itself a
; failure -- `lex_run` is the single place that speaks the CF/eax protocol,
; so that a caller has one `jc` to write rather than one per phase.
proc lex_diag, lx, code_num, off, len
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lx]
	mov	rdi, [rbx + Lexer.diags]
	call	vec_push
	mov	r12, rax

	mov	rdi, r12
	mov	esi, [code_num]
	mov	edx, [rbx + Lexer.file_id]
	mov	rcx, [off]
	mov	r8,  [len]
	call	diag_init

	mov	rdi, r12
	mov	rsi, [rbx + Lexer.src_ptr]
	mov	rdx, [rbx + Lexer.src_len]
	mov	rcx, [rbx + Lexer.path_ptr]
	mov	r8,  [rbx + Lexer.path_len]
	call	diag_attach_source

	inc	dword [rbx + Lexer.ndiag]
	cmp	dword [rbx + Lexer.firstcode], 0
	jne	.have_first
	mov	eax, [code_num]
	mov	[rbx + Lexer.firstcode], eax
  .have_first:
	mov	rax, r12
	return
endp

; ---- NFC segmentation ------------------------------------------------------

; __lex_safe_boundary(cpv) -> eax = 1 if a segment may be cut immediately
; before `cpv` without changing what NFC produces, else 0.
;
; The condition is UAX #15's: `cpv` is a starter (ccc 0) AND its own
; NFC_Quick_Check is Yes. `nfc_quick_check` over a one-element array IS that
; per-codepoint property, so no NFC_QC table of this file's own is needed --
; and would not be wanted, since compiler/shared/unicode/README.md records
; that the quick check derives NFC_QC exactly from the shipped tables.
; The ASCII short-circuit is the fast path, not a separate rule: every ASCII
; codepoint satisfies both halves anyway (this file's header proves it).
; Cannot fail; leaves CF clear.
proc __lex_safe_boundary, cpv
	locals
		slot	one, dd		; a one-element codepoint array
	endl
	mov	eax, [cpv]
	cmp	eax, 0x80
	jb	.yes
	mov	edi, eax
	call	nfc_ccc
	test	eax, eax
	jnz	.no
	mov	eax, [cpv]
	mov	[one], eax
	lea	rdi, [one]
	mov	rsi, 1
	call	nfc_quick_check
	cmp	eax, UNI_NFC_YES
	jne	.no
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __lex_nfc_check(lx, count, endoff) -- run `nfc_is_nfc` over the first
; `count` buffered codepoints, which span source bytes [segoff, endoff), and
; record `EXS-E0102` if they are not in NFC.
;
; REPORT-ONCE PER FILE, and `nfcdone` latches it. §8.1 states NFC as a whole-
; file property and the remedy is to run the file through a normaliser, so a
; second, third and fortieth copy of the same diagnostic tells the reader
; nothing the first did not -- while `EXS-E0103`, three rules up, is reported
; for every occurrence precisely because enumerating them IS the remedy there.
; Cannot fail; leaves CF clear.
proc __lex_nfc_check, lx, count, endoff
	uses	rbx
	locals
	endl
	mov	rbx, [lx]
	cmp	dword [rbx + Lexer.nfcdone], 0
	jne	.done
	mov	rax, [count]
	test	rax, rax
	jz	.done

	mov	rdi, [rbx + Lexer.nfc_cp]
	mov	rsi, [count]
	mov	rdx, [rbx + Lexer.nfc_scr]
	mov	rcx, LEX_NFC_SCR_CAP
	call	nfc_is_nfc
	test	eax, eax
	jnz	.done

	mov	rdi, rbx
	mov	esi, 102
	mov	edx, [rbx + Lexer.segoff]
	mov	rcx, [endoff]
	sub	rcx, rdx
	call	lex_diag
	mov	dword [rbx + Lexer.nfcdone], 1
  .done:
	return
endp

; __lex_nfc_flush(lx, endoff) -- close the current segment, checking it first
; if it holds any non-ASCII codepoint. A segment of pure ASCII is NFC by
; construction and is dropped without a call. Cannot fail; leaves CF clear.
proc __lex_nfc_flush, lx, endoff
	uses	rbx
	locals
	endl
	mov	rbx, [lx]
	cmp	dword [rbx + Lexer.segwide], 0
	je	.reset
	mov	rdi, rbx
	mov	esi, [rbx + Lexer.segn]
	mov	rdx, [endoff]
	call	__lex_nfc_check
  .reset:
	mov	dword [rbx + Lexer.segn],    0
	mov	dword [rbx + Lexer.segwide], 0
	return
endp

; __lex_nfc_split(lx, atoff) -- the buffer is full and another non-ASCII
; codepoint is waiting at byte offset `atoff`. Cut the segment at the LAST
; safe boundary in the buffer, check the part before it, and keep the part
; from the boundary on as the head of the next segment.
;
; The retained tail is always non-ASCII: an ASCII codepoint flushes the
; segment before it is ever buffered, so only index 0 can hold one, and the
; cut is always at index >= 1. With no safe boundary anywhere in the buffer
; the cut falls back to keeping the last LEX_NFC_OVERLAP codepoints -- see
; this file's header for exactly what that costs and why it cannot produce a
; false EXS-E0102. Cannot fail; leaves CF clear.
proc __lex_nfc_split, lx, atoff
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [lx]
	mov	r12, LEX_NFC_SEG_CAP - 1	; scan down for a safe boundary
  .seek:
	cmp	r12, 1
	jb	.no_boundary
	mov	rax, [rbx + Lexer.nfc_cp]
	mov	edi, [rax + r12*4]
	call	__lex_safe_boundary
	test	eax, eax
	jnz	.cut
	dec	r12
	jmp	.seek

  .no_boundary:
	; Keep the last LEX_NFC_OVERLAP codepoints and check everything before
	; them against the codepoint that is waiting, which is where this
	; window genuinely ends.
	mov	r12, LEX_NFC_SEG_CAP - LEX_NFC_OVERLAP
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [atoff]
	call	__lex_nfc_check
	jmp	.shift

  .cut:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rax, [rbx + Lexer.nfc_off]
	mov	edx, [rax + r12*4]
	call	__lex_nfc_check

  .shift:
	; nfc_cp[r12..CAP) -> nfc_cp[0..), and the same for nfc_off.
	mov	r13, LEX_NFC_SEG_CAP
	sub	r13, r12			; retained count
	mov	rax, [rbx + Lexer.nfc_off]
	mov	r14d, [rax + r12*4]		; new segment's byte offset
	mov	[rbx + Lexer.segoff], r14d

	mov	rdi, [rbx + Lexer.nfc_cp]
	mov	rsi, rdi
	lea	rsi, [rsi + r12*4]
	mov	rcx, r13
	shl	rcx, 2
	rep	movsb
	mov	rdi, [rbx + Lexer.nfc_off]
	mov	rsi, rdi
	lea	rsi, [rsi + r12*4]
	mov	rcx, r13
	shl	rcx, 2
	rep	movsb

	mov	[rbx + Lexer.segn], r13d
	mov	dword [rbx + Lexer.segwide], 1
	return
endp

; __lex_nfc_push(lx, cpv, offv) -- add one codepoint, at byte offset `offv`,
; to the current NFC segment, splitting first if the buffer is full. Does
; nothing at all once `EXS-E0102` has been reported for this file.
; Cannot fail; leaves CF clear.
proc __lex_nfc_push, lx, cpv, offv
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lx]
	cmp	dword [rbx + Lexer.nfcdone], 0
	jne	.done

	mov	r12d, [rbx + Lexer.segn]
	test	r12d, r12d
	jnz	.not_first
	mov	eax, [offv]
	mov	[rbx + Lexer.segoff], eax
	jmp	.have_room
  .not_first:
	cmp	r12d, LEX_NFC_SEG_CAP
	jb	.have_room
	mov	rdi, rbx
	mov	rsi, [offv]
	call	__lex_nfc_split
	cmp	dword [rbx + Lexer.nfcdone], 0
	jne	.done
	mov	r12d, [rbx + Lexer.segn]

  .have_room:
	mov	rax, [rbx + Lexer.nfc_cp]
	mov	ecx, [cpv]
	mov	[rax + r12*4], ecx
	mov	rax, [rbx + Lexer.nfc_off]
	mov	ecx, [offv]
	mov	[rax + r12*4], ecx
	inc	r12d
	mov	[rbx + Lexer.segn], r12d
	mov	eax, [cpv]
	cmp	eax, 0x80
	jb	.done
	mov	dword [rbx + Lexer.segwide], 1
  .done:
	return
endp

; ---- the §8.1 pass ---------------------------------------------------------

; lex_source_check(lx) -> eax = the number of diagnostics recorded by THIS
; call. Cannot fail in the CF sense and leaves CF clear: a source-policy
; violation is reported through the `diags` vector, not through the return
; channel -- `lex_run` is the one routine that speaks CF/eax.
;
; One left-to-right pass, so diagnostics come out in source order without a
; sort. That is not only a nicety: sorting them would have to break ties on
; something, and CLAUDE.md forbids anything whose order can depend on a
; pointer value.
proc lex_source_check, lx
	uses	rbx, r12, r13, r14
	locals
		slot	cpv,    dd	; the codepoint just decoded
		slot	nbytes, dq	; how many bytes it took
		slot	before, dd	; ndiag on entry
	endl
	mov	rbx, [lx]
	mov	eax, [rbx + Lexer.ndiag]
	mov	[before], eax
	mov	dword [rbx + Lexer.nfcdone], 0
	mov	dword [rbx + Lexer.segn],    0
	mov	dword [rbx + Lexer.segwide], 0
	mov	dword [rbx + Lexer.segoff],  0

	mov	r12, [rbx + Lexer.src_ptr]
	mov	r13, [rbx + Lexer.src_len]
	xor	r14, r14

	; ---- a BOM is EXS-E0101, not a byte to skip (§8.1) -----------------
	; It is reported and then stepped over, because the rest of the file
	; may be perfectly good UTF-8 and the reader deserves to hear about
	; whatever else is wrong with it in the same run.
	cmp	r13, 3
	jb	.scan
	cmp	byte [r12],   0xEF
	jne	.scan
	cmp	byte [r12+1], 0xBB
	jne	.scan
	cmp	byte [r12+2], 0xBF
	jne	.scan
	mov	rdi, rbx
	mov	esi, 101
	xor	edx, edx
	mov	ecx, 3
	call	lex_diag
	mov	r14, 3

  .scan:
	cmp	r14, r13
	jae	.eof
	mov	rdi, r12
	add	rdi, r14
	mov	rsi, r13
	sub	rsi, r14
	call	__diag_utf8_decode	; rax = codepoint, rdx = bytes consumed
	cmp	eax, DIAG_CP_INVALID
	je	.bad_utf8
	mov	[cpv], eax
	mov	[nbytes], rdx

	cmp	eax, 0x0D
	je	.carriage_return

	mov	edi, eax
	call	diag_is_dangerous_cp
	test	eax, eax
	jz	.not_dangerous
	mov	rdi, rbx
	mov	esi, 103
	mov	rdx, r14
	mov	rcx, [nbytes]
	call	lex_diag
  .not_dangerous:

	mov	eax, [cpv]
	cmp	eax, 0x80
	jae	.wide
	; ASCII: a safe NFC boundary, so close the segment here and open a new
	; one whose first codepoint is this character.
	mov	rdi, rbx
	mov	rsi, r14
	call	__lex_nfc_flush
  .wide:
	mov	rdi, rbx
	mov	esi, [cpv]
	mov	rdx, r14
	call	__lex_nfc_push

	add	r14, [nbytes]
	jmp	.scan

  .carriage_return:
	; Two bytes if this is a CRLF pair, one if it is a lone CR -- the span
	; says which was found. Either way the scan stops here; see the header.
	mov	ecx, 1
	mov	rax, r14
	inc	rax
	cmp	rax, r13
	jae	.cr_report
	cmp	byte [r12 + rax], 0x0A
	jne	.cr_report
	mov	ecx, 2
  .cr_report:
	mov	rdi, rbx
	mov	esi, 106
	mov	rdx, r14
	call	lex_diag
	jmp	.stop

  .bad_utf8:
	; Span one byte: the decoder guarantees it consumed at least one, and
	; the offending byte is the honest thing to point at. The scan ends --
	; every offset past here would be a guess about text this compiler
	; cannot read.
	mov	rdi, rbx
	mov	esi, 101
	mov	rdx, r14
	mov	ecx, 1
	call	lex_diag
	jmp	.stop

  .eof:
	mov	rdi, rbx
	mov	rsi, r13
	call	__lex_nfc_flush
  .stop:
	mov	eax, [rbx + Lexer.ndiag]
	sub	eax, [before]
	return
endp
