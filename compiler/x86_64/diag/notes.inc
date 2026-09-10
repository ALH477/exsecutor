; compiler/x86_64/diag/notes.inc
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
; NOTES: the second sentence a diagnostic is allowed to say, and the data it
; is derived from. §8.3.
;
; WHY THIS FILE EXISTS. `docs/design/diagnostics-review.md` measured Stage
; 1's kill criterion (§16) and found 37 of 46 diagnostics were the identical
; sentence `unexpected token`, because `struct Diag` had no field that could
; hold anything else. This file supplies the vocabulary; render.inc supplies
; the field and the derivation.
;
; A NOTE IS AN ID, NOT A STRING, AND THAT IS THE WHOLE DESIGN. §8.3 says
; "codes are permanent; text is not. Tools match codes, never English prose"
; and "translation, if ever, is a lookup keyed on code." A note carries a
; small integer id plus, optionally, ONE argument. The English is a table
; lookup on the id -- there is no format string anywhere in diag/, and the
; argument is APPENDED to the sentence rather than interpolated into it. So:
;
;   - a tool still matches `code` (and now `note_id`, which is equally
;     permanent and equally not-English);
;   - translation stays a lookup, now keyed on (code, note_id), and the one
;     variable part of a note is a token that is never translated anyway --
;     a §8.4 reserved word, or a §8.4 comparison word;
;   - render.inc's original claim, that "no format string in this file
;     interpolates anything into the message text," survives verbatim.
;     Appending a bounded, compiler-owned token after a canonical sentence
;     is not the 400-site formatting entanglement §8.3 cites Rust's Fluent
;     effort for. That failure mode was 400+ SITES; this is one site.
;
; THE ARGUMENT IS ESCAPED ANYWAY. Every note argument in this file is
; compiler-authored (it comes out of §8.4's closed keyword set or out of the
; two-entry comparison table below), so escaping it is a no-op today. It
; still goes through diag_escape at the render site, for exactly the reason
; fix.inc gives about `text_ptr`: "escape everything echoed" is a rule a
; reviewer can check by grep and "escape everything except what we decided
; was ours" is not.
;
; THE §8.4 KEYWORD SET IS BORROWED, NOT COPIED. `diag_kw_nearest` needs the
; thirty reserved words, and §8.4 is normative for them via a generator
; (`lexer/keywords.inc`, checked by `tools/spec-check.sh` check 4). A second
; copy of those thirty words in diag/ would be a second thing to drift, and
; CLAUDE.md's "never invent" discipline applies to a keyword exactly as it
; applies to a code. So this file INCLUDES the generated table -- inside a
; fasmg `namespace`, which prefixes every label it defines.
;
; That is not a trick for its own sake, it is the only shape that works.
; diag/ is processed BEFORE lexer/ (lexer/token.inc includes diag/diag.inc
; and then keywords.inc), so a plain include here would be the FIRST of two,
; and the second would collide on `kw_text`. Inside `namespace diag_kw` the
; labels become `diag_kw.kw_text`, `diag_kw.kw_off`, `diag_kw.kw_len`, and
; lexer/token.inc's own top-level include is untouched. Verified by
; assembling both includes in one translation unit and printing both pools:
; identical bytes, no collision. The cost is 300-odd duplicated BYTES in the
; binary; the benefit is zero duplicated SOURCE, so §8.4 stays the one place
; a keyword is written down.
;
; This is a backward reference in diag/'s include chain -- diag/ reads a
; lexer/ file. It is a READ, not an edit (CLAUDE.md, Scope), and
; keywords.inc is pure data that includes nothing, so it introduces no
; cycle. tests/unit/diag_kw_mirror.asm asserts at RUNTIME that the
; namespaced copy and the top-level one are byte-identical, so if this
; arrangement ever stops holding, a fixture says so rather than a subtle
; wrong suggestion appearing in someone's terminal.
;
; EDIT DISTANCE IS ORDINARY LEVENSHTEIN, DELIBERATELY. No transposition
; term: `funtcio` -> `functio` is two substitutions and lands inside the
; threshold anyway, and a Damerau variant would need a third row of state
; for one extra hit. The candidate scan runs in §8.4's canonical table
; order and keeps the FIRST strict minimum, so a tie resolves the same way
; on every run and on every machine (CLAUDE.md, "Determinism is not
; optional" -- no ordering depends on a pointer or a hash).
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3 (diagnostics), §8.4 (the
; closed keyword set and the contextual comparison words), §8.6 (the
; grammar decisions the notes cite).
; -----------------------------------------------------------------------------

include 'escape.inc'

; ---- §8.4's thirty reserved words, borrowed rather than copied -------------
; See this file's header for why this is a namespaced include of the
; generated table and not a second table.
namespace diag_kw
	include '../lexer/keywords.inc'
end namespace

DIAG_KW_COUNT = diag_kw.KW_COUNT

; ---- note ids --------------------------------------------------------------
; PERMANENT, exactly as an EXS-E code is permanent, and for the same reason:
; they are what a tool matches and what a translation table is keyed on. Text
; below may change; these numbers may not be renumbered or reused. Unlike a
; code, a note id needs no §13 amendment -- §13 registers what class of thing
; broke, and a note says nothing about the class. Adding one here is a local
; change; changing what an existing one MEANS is not.
DIAG_NOTE_NONE       = 0
DIAG_NOTE_ENGLISH_KW = 1	; an English keyword; §8.4's word is <arg>
DIAG_NOTE_CMP_WORD   = 2	; §8.6 decision 6 -- `<`/`>` are not operators
DIAG_NOTE_DID_YOU_MEAN = 3	; near-miss for a reserved word <arg>
DIAG_NOTE_STMT_SEMI  = 4	; §8.6 decision 1 -- simple statements end in `;`
DIAG_NOTE_UNCLOSED   = 5	; a delimiter opened earlier is still open (+arg)
DIAG_NOTE_EOF_OPEN   = 6	; ... and the file ended before it closed (+arg)
DIAG_NOTE_LIT_SUFFIX = 7	; §8.4 -- a numeric literal takes no letter suffix
DIAG_NOTE_NO_IMPORT  = 8	; §8.6 decision 5 -- there is no import statement
DIAG_NOTE_RESERVED   = 9	; §8.4 -- one of thirty words, never an identifier
DIAG_NOTE_OPENED_AT  = 10	; label for a related span
DIAG_NOTE_EOF_NOCLOSE = 11	; end of input inside a construct, no closer known
DIAG_NOTE_COUNT      = 12

; ---- the canonical English, one entry per id -------------------------------
; Pure ASCII. Written with local labels so the offset and length tables below
; are computed by the assembler rather than by hand -- a hand-counted offset
; table is exactly the kind of thing that is wrong once and then wrong
; forever (keywords.inc pays a generator to avoid the same hazard).
;
; A sentence that takes an argument ENDS where the argument begins, and the
; render site inserts one space. That is the whole of the "formatting" this
; module does.
diag_note_pool:
  .n0:
  .n1:	db	'not an Exsecutor keyword; spec 8.4 spells this'
  .n2:	db	'spec 8.6 decision 6: < and > delimit generic arguments and are never operators; comparison is the word'
  .n3:	db	'did you mean the reserved word'
  .n4:	db	'spec 8.6 decision 1: every simple statement ends with a semicolon'
  .n5:	db	'a delimiter opened earlier is still open; this wants'
  .n6:	db	'the file ends inside a construct opened earlier; it wants'
  .n7:	db	'spec 8.4: a numeric literal carries no letter suffix'
  .n8:	db	'spec 8.6 decision 5: there is no import statement; a dotted Path names an item directly'
  .n9:	db	'spec 8.4 reserves thirty words; a reserved word is never an identifier, anywhere'
  .n10:	db	'opened here'
  .n11:	db	'the file ends inside a construct that was opened earlier and never closed'
  .nend:

diag_note_off:
	dd	diag_note_pool.n0  - diag_note_pool
	dd	diag_note_pool.n1  - diag_note_pool
	dd	diag_note_pool.n2  - diag_note_pool
	dd	diag_note_pool.n3  - diag_note_pool
	dd	diag_note_pool.n4  - diag_note_pool
	dd	diag_note_pool.n5  - diag_note_pool
	dd	diag_note_pool.n6  - diag_note_pool
	dd	diag_note_pool.n7  - diag_note_pool
	dd	diag_note_pool.n8  - diag_note_pool
	dd	diag_note_pool.n9  - diag_note_pool
	dd	diag_note_pool.n10 - diag_note_pool
	dd	diag_note_pool.n11 - diag_note_pool

diag_note_len:
	dd	diag_note_pool.n1  - diag_note_pool.n0
	dd	diag_note_pool.n2  - diag_note_pool.n1
	dd	diag_note_pool.n3  - diag_note_pool.n2
	dd	diag_note_pool.n4  - diag_note_pool.n3
	dd	diag_note_pool.n5  - diag_note_pool.n4
	dd	diag_note_pool.n6  - diag_note_pool.n5
	dd	diag_note_pool.n7  - diag_note_pool.n6
	dd	diag_note_pool.n8  - diag_note_pool.n7
	dd	diag_note_pool.n9  - diag_note_pool.n8
	dd	diag_note_pool.n10 - diag_note_pool.n9
	dd	diag_note_pool.n11 - diag_note_pool.n10
	dd	diag_note_pool.nend - diag_note_pool.n11

; A generated-looking table that silently went short would be caught by
; nothing else, so it is caught here, at assembly time, by fasmg's own native
; `assert` (NOT macros/assert.inc's runtime `rassert`) -- the habit
; keywords.inc's own `assert KW_TEXT_LEN` establishes.
assert (diag_note_len - diag_note_off) = DIAG_NOTE_COUNT * 4

; ---- the two comparison words a symbol maps to (§8.4 tier 2) ---------------
; `lt le gt ge eq ne` are CONTEXTUAL keywords, so they are not in §8.4's
; reserved thirty and cannot be borrowed from keywords.inc. Only the two that
; a §8.6-decision-6 diagnostic can name are here; `<=` and `>=` are not
; tokens the lexer ever forms (§8.6 decision 6 says so in as many words), so
; there is nothing for `le`/`ge` to be suggested FROM.
diag_cmp_lt db 'lt'
DIAG_CMP_LT_LEN = $ - diag_cmp_lt
diag_cmp_gt db 'gt'
DIAG_CMP_GT_LEN = $ - diag_cmp_gt

; ---- English habits, and what §8.4 spells them ------------------------------
; The single most likely first-day mistake in a Latin-keyword language is an
; English keyword, and `docs/design/diagnostics-review.md` case c14 measured
; what that costs today: four identical `unexpected token`s and nothing
; naming `si` or `gt`.
;
; THE LATIN SIDE IS A KEYWORD ID, NOT A STRING. Every counterpart here is one
; of §8.4's thirty, so it is stored as a 1-based canonical `KW_*` id and read
; back out of the borrowed table. There is no second copy of `functio` in
; this file, which is the point.
;
; `import`/`use`/`include` have no counterpart -- §8.6 decision 5 removed the
; construct rather than renaming it -- so they carry id 0 and a note of their
; own. `impl`, `for` and `as` are here because §8.6 decision 4 names those
; three specifically as inadmissible spellings the probe fixtures used.
;
; THIS IS ADVICE, NOT CLASSIFICATION. Nothing here is reserved in Exsecutor:
; `if` is a perfectly legal identifier, and a note derived from this table
; only ever appears on a token the parser has ALREADY rejected. It cannot
; turn a good program into a diagnostic; the worst it can do is offer an
; irrelevant sentence under one that was going to be printed anyway.
diag_eng_pool:
  .w0:	db	'if'
  .w1:	db	'else'
  .w2:	db	'elif'
  .w3:	db	'while'
  .w4:	db	'for'
  .w5:	db	'return'
  .w6:	db	'fn'
  .w7:	db	'func'
  .w8:	db	'function'
  .w9:	db	'def'
  .w10:	db	'struct'
  .w11:	db	'pub'
  .w12:	db	'public'
  .w13:	db	'let'
  .w14:	db	'const'
  .w15:	db	'var'
  .w16:	db	'mut'
  .w17:	db	'break'
  .w18:	db	'continue'
  .w19:	db	'match'
  .w20:	db	'switch'
  .w21:	db	'case'
  .w22:	db	'type'
  .w23:	db	'interface'
  .w24:	db	'trait'
  .w25:	db	'as'
  .w26:	db	'impl'
  .w27:	db	'import'
  .w28:	db	'use'
  .w29:	db	'include'
  .wend:

diag_eng_off:
	dd	diag_eng_pool.w0  - diag_eng_pool
	dd	diag_eng_pool.w1  - diag_eng_pool
	dd	diag_eng_pool.w2  - diag_eng_pool
	dd	diag_eng_pool.w3  - diag_eng_pool
	dd	diag_eng_pool.w4  - diag_eng_pool
	dd	diag_eng_pool.w5  - diag_eng_pool
	dd	diag_eng_pool.w6  - diag_eng_pool
	dd	diag_eng_pool.w7  - diag_eng_pool
	dd	diag_eng_pool.w8  - diag_eng_pool
	dd	diag_eng_pool.w9  - diag_eng_pool
	dd	diag_eng_pool.w10 - diag_eng_pool
	dd	diag_eng_pool.w11 - diag_eng_pool
	dd	diag_eng_pool.w12 - diag_eng_pool
	dd	diag_eng_pool.w13 - diag_eng_pool
	dd	diag_eng_pool.w14 - diag_eng_pool
	dd	diag_eng_pool.w15 - diag_eng_pool
	dd	diag_eng_pool.w16 - diag_eng_pool
	dd	diag_eng_pool.w17 - diag_eng_pool
	dd	diag_eng_pool.w18 - diag_eng_pool
	dd	diag_eng_pool.w19 - diag_eng_pool
	dd	diag_eng_pool.w20 - diag_eng_pool
	dd	diag_eng_pool.w21 - diag_eng_pool
	dd	diag_eng_pool.w22 - diag_eng_pool
	dd	diag_eng_pool.w23 - diag_eng_pool
	dd	diag_eng_pool.w24 - diag_eng_pool
	dd	diag_eng_pool.w25 - diag_eng_pool
	dd	diag_eng_pool.w26 - diag_eng_pool
	dd	diag_eng_pool.w27 - diag_eng_pool
	dd	diag_eng_pool.w28 - diag_eng_pool
	dd	diag_eng_pool.w29 - diag_eng_pool

diag_eng_len:
	db	diag_eng_pool.w1  - diag_eng_pool.w0
	db	diag_eng_pool.w2  - diag_eng_pool.w1
	db	diag_eng_pool.w3  - diag_eng_pool.w2
	db	diag_eng_pool.w4  - diag_eng_pool.w3
	db	diag_eng_pool.w5  - diag_eng_pool.w4
	db	diag_eng_pool.w6  - diag_eng_pool.w5
	db	diag_eng_pool.w7  - diag_eng_pool.w6
	db	diag_eng_pool.w8  - diag_eng_pool.w7
	db	diag_eng_pool.w9  - diag_eng_pool.w8
	db	diag_eng_pool.w10 - diag_eng_pool.w9
	db	diag_eng_pool.w11 - diag_eng_pool.w10
	db	diag_eng_pool.w12 - diag_eng_pool.w11
	db	diag_eng_pool.w13 - diag_eng_pool.w12
	db	diag_eng_pool.w14 - diag_eng_pool.w13
	db	diag_eng_pool.w15 - diag_eng_pool.w14
	db	diag_eng_pool.w16 - diag_eng_pool.w15
	db	diag_eng_pool.w17 - diag_eng_pool.w16
	db	diag_eng_pool.w18 - diag_eng_pool.w17
	db	diag_eng_pool.w19 - diag_eng_pool.w18
	db	diag_eng_pool.w20 - diag_eng_pool.w19
	db	diag_eng_pool.w21 - diag_eng_pool.w20
	db	diag_eng_pool.w22 - diag_eng_pool.w21
	db	diag_eng_pool.w23 - diag_eng_pool.w22
	db	diag_eng_pool.w24 - diag_eng_pool.w23
	db	diag_eng_pool.w25 - diag_eng_pool.w24
	db	diag_eng_pool.w26 - diag_eng_pool.w25
	db	diag_eng_pool.w27 - diag_eng_pool.w26
	db	diag_eng_pool.w28 - diag_eng_pool.w27
	db	diag_eng_pool.w29 - diag_eng_pool.w28
	db	diag_eng_pool.wend - diag_eng_pool.w29

; The §8.4 word each one is, as a 1-based canonical keyword id; 0 = none.
; `for` maps to `per` rather than `quisque` because §8.5 makes `per` the
; general form and `quisque` the element form -- a reader who wrote `for`
; almost certainly wanted the one that takes a range.
diag_eng_kwid:
	db	diag_kw.KW_SI		; if
	db	diag_kw.KW_ALITER	; else
	db	diag_kw.KW_SIN		; elif
	db	diag_kw.KW_DUM		; while
	db	diag_kw.KW_PER		; for
	db	diag_kw.KW_REDDE	; return
	db	diag_kw.KW_FUNCTIO	; fn
	db	diag_kw.KW_FUNCTIO	; func
	db	diag_kw.KW_FUNCTIO	; function
	db	diag_kw.KW_FUNCTIO	; def
	db	diag_kw.KW_STRUCTURA	; struct
	db	diag_kw.KW_PUBLICA	; pub
	db	diag_kw.KW_PUBLICA	; public
	db	diag_kw.KW_FIRMA	; let
	db	diag_kw.KW_FIRMA	; const
	db	diag_kw.KW_MUTABILIS	; var
	db	diag_kw.KW_MUTABILIS	; mut
	db	diag_kw.KW_RUMPE	; break
	db	diag_kw.KW_PERGE	; continue
	db	diag_kw.KW_DISCERNE	; match
	db	diag_kw.KW_DISCERNE	; switch
	db	diag_kw.KW_CASUS	; case
	db	diag_kw.KW_TYPUS	; type
	db	diag_kw.KW_INTERFACIES	; interface
	db	diag_kw.KW_INTERFACIES	; trait
	db	diag_kw.KW_SICUT	; as        (§8.6 decision 4)
	db	diag_kw.KW_INTERFACIES	; impl      (§8.6 decision 4)
	db	0			; import    (§8.6 decision 5)
	db	0			; use
	db	0			; include

; The note each one carries. Everything with a counterpart says so; the three
; that have none say what replaced the construct.
diag_eng_note:
	db	DIAG_NOTE_ENGLISH_KW	; if
	db	DIAG_NOTE_ENGLISH_KW	; else
	db	DIAG_NOTE_ENGLISH_KW	; elif
	db	DIAG_NOTE_ENGLISH_KW	; while
	db	DIAG_NOTE_ENGLISH_KW	; for
	db	DIAG_NOTE_ENGLISH_KW	; return
	db	DIAG_NOTE_ENGLISH_KW	; fn
	db	DIAG_NOTE_ENGLISH_KW	; func
	db	DIAG_NOTE_ENGLISH_KW	; function
	db	DIAG_NOTE_ENGLISH_KW	; def
	db	DIAG_NOTE_ENGLISH_KW	; struct
	db	DIAG_NOTE_ENGLISH_KW	; pub
	db	DIAG_NOTE_ENGLISH_KW	; public
	db	DIAG_NOTE_ENGLISH_KW	; let
	db	DIAG_NOTE_ENGLISH_KW	; const
	db	DIAG_NOTE_ENGLISH_KW	; var
	db	DIAG_NOTE_ENGLISH_KW	; mut
	db	DIAG_NOTE_ENGLISH_KW	; break
	db	DIAG_NOTE_ENGLISH_KW	; continue
	db	DIAG_NOTE_ENGLISH_KW	; match
	db	DIAG_NOTE_ENGLISH_KW	; switch
	db	DIAG_NOTE_ENGLISH_KW	; case
	db	DIAG_NOTE_ENGLISH_KW	; type
	db	DIAG_NOTE_ENGLISH_KW	; interface
	db	DIAG_NOTE_ENGLISH_KW	; trait
	db	DIAG_NOTE_ENGLISH_KW	; as
	db	DIAG_NOTE_ENGLISH_KW	; impl
	db	DIAG_NOTE_NO_IMPORT	; import
	db	DIAG_NOTE_NO_IMPORT	; use
	db	DIAG_NOTE_NO_IMPORT	; include

DIAG_ENG_COUNT = (diag_eng_len - diag_eng_off) / 4
assert (diag_eng_kwid - diag_eng_len) = DIAG_ENG_COUNT
assert (diag_eng_note - diag_eng_kwid) = DIAG_ENG_COUNT

; ---- accessors -------------------------------------------------------------

; diag_note_text(note_id) -> rax = pointer, rdx = length. `DIAG_NOTE_NONE`
; returns a zero length, not a trap: a record with no note is the ordinary
; case, unlike diag_lookup's miss (util.inc's header) which can only mean the
; compiler passed a code that was never registered.
proc diag_note_text, note_id
	locals
	endl
	mov	ecx, [note_id]
	cmp	ecx, DIAG_NOTE_COUNT
	jb	.ok
	xor	ecx, ecx
  .ok:
	lea	rax, [diag_note_pool]
	mov	edx, [diag_note_off + rcx*4]
	add	rax, rdx
	mov	edx, [diag_note_len + rcx*4]
	return
endp

; diag_kw_word(idx) -> rax = pointer, rdx = length, for a ZERO-based index
; into §8.4's canonical table order. Out of range returns a zero length
; rather than trapping, for the same reason diag_note_text does.
proc diag_kw_word, idx
	locals
	endl
	mov	ecx, [idx]
	cmp	ecx, DIAG_KW_COUNT
	jb	.ok
	xor	eax, eax
	xor	edx, edx
	return
  .ok:
	lea	rax, [diag_kw.kw_text]
	mov	edx, [diag_kw.kw_off + rcx*4]
	add	rax, rdx
	movzx	edx, byte [diag_kw.kw_len + rcx]
	return
endp

; ---- bounded Levenshtein ---------------------------------------------------

DIAG_LEV_MAX = 16	; longest operand either side; §8.4's longest word is 11
DIAG_LEV_FAR = 99	; the answer for anything longer -- never under a threshold

; __diag_lev(a_ptr, a_len, b_ptr, b_len) -> rax = edit distance, or
; DIAG_LEV_FAR if either operand is longer than DIAG_LEV_MAX.
;
; Two rows of 17 dwords on the frame, no allocation, no arena. The bound is
; what makes that safe: the source-derived operand is a token the caller has
; already length-checked, and refusing rather than growing keeps this off
; escape.inc's "output length is attacker-controlled" footing entirely.
;
; BYTES, NOT CODEPOINTS. §8.4's words are ASCII, so a non-ASCII identifier
; simply scores far away rather than scoring wrong -- which is the correct
; answer for "did you mean a reserved word", since no reserved word contains
; a non-ASCII byte.
proc __diag_lev, a_ptr, a_len, b_ptr, b_len
	uses	rbx, r12, r13, r14
	locals
		slot prevrow, 68	; 17 dwords
		slot currow,  68
	endl
	mov	rax, [a_len]
	cmp	rax, DIAG_LEV_MAX
	ja	.far
	mov	rax, [b_len]
	cmp	rax, DIAG_LEV_MAX
	ja	.far

	xor	ecx, ecx
  .init:
	cmp	rcx, [b_len]
	ja	.init_done
	mov	[prevrow + rcx*4], ecx
	inc	rcx
	jmp	.init
  .init_done:

	mov	r12, 1			; i
  .row:
	cmp	r12, [a_len]
	ja	.rows_done
	mov	[currow], r12d
	mov	rbx, [a_ptr]
	movzx	r14d, byte [rbx + r12 - 1]	; a[i-1]
	mov	r13, 1			; j
  .col:
	cmp	r13, [b_len]
	ja	.row_done
	mov	rbx, [b_ptr]
	movzx	eax, byte [rbx + r13 - 1]	; b[j-1]
	xor	ecx, ecx
	cmp	eax, r14d
	je	.same
	mov	ecx, 1
  .same:
	mov	eax, [prevrow + r13*4]		; deletion
	inc	eax
	mov	edx, [currow + r13*4 - 4]	; insertion
	inc	edx
	cmp	edx, eax
	jae	.keep1
	mov	eax, edx
  .keep1:
	mov	edx, [prevrow + r13*4 - 4]	; substitution
	add	edx, ecx
	cmp	edx, eax
	jae	.keep2
	mov	eax, edx
  .keep2:
	mov	[currow + r13*4], eax
	inc	r13
	jmp	.col
  .row_done:
	xor	ecx, ecx
  .copy:
	cmp	rcx, [b_len]
	ja	.copy_done
	mov	eax, [currow + rcx*4]
	mov	[prevrow + rcx*4], eax
	inc	rcx
	jmp	.copy
  .copy_done:
	inc	r12
	jmp	.row
  .rows_done:
	mov	rcx, [b_len]
	mov	eax, [prevrow + rcx*4]
	return
  .far:
	mov	eax, DIAG_LEV_FAR
	return
endp

; ---- the closed-set suggestion ---------------------------------------------

; diag_kw_nearest(ptr, len) -> eax = the ZERO-based canonical index of the
; nearest §8.4 reserved word within this file's threshold, or -1.
;
; THRESHOLDS. One edit for a short word, two for a word of five bytes or
; more. `funtcio` -> `functio` is a transposition, which plain Levenshtein
; scores 2, and it is the review's own c04 -- so a threshold of 1 everywhere
; would miss the exact case D5 was raised about. Two edits on a two-letter
; word, by contrast, matches everything; hence the split.
;
; A DISTANCE OF ZERO IS NOT A SUGGESTION. An identifier that IS a reserved
; word is `EXS-E0220`, which has its own code, its own message and its own
; fix (lexer/lex.inc); suggesting the word someone already typed would be
; noise on top of it.
;
; Ties resolve to the FIRST candidate in §8.4's canonical order. That order
; is the spec's own table order (keywords.inc's header), so the resolution is
; a property of the spec rather than of a pointer or a hash -- CLAUDE.md's
; determinism rule applies to a suggestion exactly as it applies to symbol
; emission.
DIAG_KW_NEAR_SHORT = 1		; threshold for len < DIAG_KW_NEAR_LONGLEN
DIAG_KW_NEAR_LONG  = 2
DIAG_KW_NEAR_LONGLEN = 5
proc diag_kw_nearest, ptr, len
	uses	rbx, r12, r13, r14
	locals
		slot bestidx, dd
		slot bestdst, dd
		slot thresh,  dd
	endl
	mov	dword [bestidx], -1
	mov	dword [bestdst], DIAG_LEV_FAR
	mov	eax, DIAG_KW_NEAR_SHORT
	cmp	qword [len], DIAG_KW_NEAR_LONGLEN
	jb	.thr
	mov	eax, DIAG_KW_NEAR_LONG
  .thr:
	mov	[thresh], eax

	; A candidate whose length differs from the token's by more than the
	; threshold cannot possibly be within it -- each length difference
	; costs at least one edit. Cheap, and it is what keeps the common case
	; (a long ordinary identifier) from running thirty DP tables.
	xor	r12, r12		; canonical index
  .loop:
	cmp	r12, DIAG_KW_COUNT
	jge	.done
	mov	rdi, r12
	call	diag_kw_word		; rax = ptr, rdx = len
	mov	rbx, rax
	mov	r13, rdx
	mov	rax, r13
	sub	rax, [len]
	jns	.absdone
	neg	rax
  .absdone:
	cmp	eax, [thresh]
	ja	.next
	mov	rdi, [ptr]
	mov	rsi, [len]
	mov	rdx, rbx
	mov	rcx, r13
	call	__diag_lev
	test	eax, eax
	jz	.isakeyword		; distance 0 -- that is EXS-E0220's case
	cmp	eax, [thresh]
	ja	.next
	cmp	eax, [bestdst]
	jae	.next			; strict: first minimum wins the tie
	mov	[bestdst], eax
	mov	[bestidx], r12d
  .next:
	inc	r12
	jmp	.loop
  .done:
	mov	eax, [bestidx]
	return
	; The token IS a reserved word. That is not a near miss, it is
	; `EXS-E0220`, and suggesting the word someone already typed would be
	; noise printed under a diagnostic that already names it. Bail on the
	; whole scan rather than merely skipping this candidate: any OTHER
	; word within the threshold (`si` is one edit from `sin`) would
	; otherwise be offered as the correction for a word that is spelled
	; exactly right.
  .isakeyword:
	mov	eax, -1
	return
endp

; ---- English-habit lookup ---------------------------------------------------

; diag_eng_lookup(ptr, len) -> eax = row index into the tables above, or -1.
; An exact, case-sensitive byte match: this is a table of words someone typed
; on purpose, not a fuzzy match, and `IF` is not a keyword in any of the
; languages the table is drawn from.
proc diag_eng_lookup, ptr, len
	uses	rbx, r12, r13
	locals
	endl
	xor	r12, r12
  .loop:
	cmp	r12, DIAG_ENG_COUNT
	jge	.miss
	movzx	eax, byte [diag_eng_len + r12]
	cmp	rax, [len]
	jne	.next
	lea	rbx, [diag_eng_pool]
	mov	ecx, [diag_eng_off + r12*4]
	add	rbx, rcx
	mov	r13, [ptr]
	xor	rcx, rcx
  .cmp:
	cmp	rcx, [len]
	jge	.hit
	mov	al, [rbx + rcx]
	cmp	al, [r13 + rcx]
	jne	.next
	inc	rcx
	jmp	.cmp
  .hit:
	mov	eax, r12d
	return
  .next:
	inc	r12
	jmp	.loop
  .miss:
	mov	eax, -1
	return
endp

; diag_eng_note_id(row) -> eax = the note id that row carries.
proc diag_eng_note_id, row
	locals
	endl
	mov	ecx, [row]
	cmp	ecx, DIAG_ENG_COUNT
	jb	.ok
	xor	eax, eax
	return
  .ok:
	movzx	eax, byte [diag_eng_note + rcx]
	return
endp

; diag_eng_kw_word(row) -> rax = pointer, rdx = length, for the §8.4 word
; that row's English maps to; a zero length where there is none.
proc diag_eng_kw_word, row
	locals
	endl
	mov	ecx, [row]
	cmp	ecx, DIAG_ENG_COUNT
	jb	.ok
	xor	eax, eax
	xor	edx, edx
	return
  .ok:
	movzx	eax, byte [diag_eng_kwid + rcx]
	test	eax, eax
	jnz	.have
	xor	eax, eax
	xor	edx, edx
	return
  .have:
	dec	eax			; KW_* ids are 1-based; the table is not
	mov	rdi, rax
	call	diag_kw_word
	return
endp

; ---- shape predicates over a span's own bytes -------------------------------

; diag_cmp_word(ptr, len) -> rax = pointer, rdx = length of the §8.4
; comparison word a lone `<` or `>` should have been; a zero length
; otherwise. §8.6 decision 6 is the whole content of this routine.
proc diag_cmp_word, ptr, len
	locals
	endl
	cmp	qword [len], 1
	jne	.no
	mov	rcx, [ptr]
	movzx	eax, byte [rcx]
	cmp	eax, '<'
	je	.lt
	cmp	eax, '>'
	je	.gt
  .no:
	xor	eax, eax
	xor	edx, edx
	return
  .lt:
	lea	rax, [diag_cmp_lt]
	mov	rdx, DIAG_CMP_LT_LEN
	return
  .gt:
	lea	rax, [diag_cmp_gt]
	mov	rdx, DIAG_CMP_GT_LEN
	return
endp

; diag_span_is_word(ptr, len) -> eax = 1 if every byte could be part of an
; identifier -- ASCII letters, digits, `_`, or any byte >= 0x80 -- and the
; first is not a digit. Deliberately coarser than §8.2's XID rule: this
; decides whether a SUGGESTION is worth computing, and lexer/ident.inc is
; where identifier validity is actually decided. A byte-level approximation
; that says yes to something §8.2 would reject costs one Levenshtein scan
; over thirty short words and nothing else.
proc diag_span_is_word, ptr, len
	uses	rbx
	locals
	endl
	mov	rcx, [len]
	test	rcx, rcx
	jz	.no
	mov	rbx, [ptr]
	movzx	eax, byte [rbx]
	cmp	eax, '0'
	jb	.scan
	cmp	eax, '9'
	jbe	.no
  .scan:
	xor	rcx, rcx
  .loop:
	cmp	rcx, [len]
	jge	.yes
	movzx	eax, byte [rbx + rcx]
	cmp	eax, 0x80
	jae	.ok
	cmp	eax, '_'
	je	.ok
	cmp	eax, '0'
	jb	.no
	cmp	eax, '9'
	jbe	.ok
	cmp	eax, 'A'
	jb	.no
	cmp	eax, 'Z'
	jbe	.ok
	cmp	eax, 'a'
	jb	.no
	cmp	eax, 'z'
	ja	.no
  .ok:
	inc	rcx
	jmp	.loop
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; diag_prev_word(wsrc, wslen, woff) -> rax = pointer, rdx = length of the
; word immediately BEFORE `woff` in `wsrc`, skipping intervening ASCII
; whitespace; a zero length if there is none within the bounds below.
;
; WHY A DIAGNOSTIC EVER LOOKS BACKWARDS. `docs/design/diagnostics-review.md`
; case c14 is `if a > 10 { return a; }`, and the parser reports the missing
; `;` at `a` -- one token PAST the English keyword that caused it, because
; `if` is a perfectly good identifier and only the token after it is
; unexpected. Nothing in the span names `if`, so a span-only derivation is
; structurally unable to say the one useful thing about the single most
; likely first-day mistake in a Latin-keyword language. The word before the
; insertion point is.
;
; BOUNDED, both scans. At most DIAG_PREVW_SKIP bytes of whitespace and at
; most DIAG_PREVW_MAX bytes of word: a longer word is in neither of this
; file's tables, so scanning further could not change any answer, and an
; unbounded backward scan over adversarial source is the shape escape.inc's
; header spends a page refusing.
;
; THE FALSE POSITIVE IS NAMED RATHER THAN DENIED. `match`, `type` and `use`
; are legal Exsecutor identifiers, so a local named one of them, immediately
; before a genuine missing `;`, gets an irrelevant sentence under a real
; diagnostic. That is the whole cost: an extra advisory line, never a wrong
; code, never a wrong span, never a wrong edit. The benefit is naming `si`
; for `if`, and it is not close.
DIAG_PREVW_SKIP = 32
DIAG_PREVW_MAX  = 16
proc diag_prev_word, wsrc, wslen, woff
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [wsrc]
	mov	rax, [woff]
	cmp	rax, [wslen]
	jbe	.clamped
	mov	rax, [wslen]
  .clamped:
	xor	r12, r12		; whitespace bytes skipped
  .skip:
	test	rax, rax
	jz	.none
	cmp	r12, DIAG_PREVW_SKIP
	jae	.none
	movzx	ecx, byte [rbx + rax - 1]
	cmp	ecx, ' '
	je	.skip_one
	cmp	ecx, 9
	je	.skip_one
	cmp	ecx, 10
	je	.skip_one
	cmp	ecx, 13
	je	.skip_one
	jmp	.word_end
  .skip_one:
	dec	rax
	inc	r12
	jmp	.skip
  .word_end:
	mov	r13, rax		; one past the word's last byte
	xor	r12, r12
  .back:
	test	rax, rax
	jz	.word_done
	cmp	r12, DIAG_PREVW_MAX
	jae	.word_done
	movzx	ecx, byte [rbx + rax - 1]
	cmp	ecx, 0x80
	jae	.take
	cmp	ecx, '_'
	je	.take
	cmp	ecx, '0'
	jb	.word_done
	cmp	ecx, '9'
	jbe	.take
	cmp	ecx, 'A'
	jb	.word_done
	cmp	ecx, 'Z'
	jbe	.take
	cmp	ecx, 'a'
	jb	.word_done
	cmp	ecx, 'z'
	ja	.word_done
  .take:
	dec	rax
	inc	r12
	jmp	.back
  .word_done:
	mov	rdx, r13
	sub	rdx, rax
	test	rdx, rdx
	jz	.none
	add	rax, rbx
	return
  .none:
	xor	eax, eax
	xor	edx, edx
	return
endp

; diag_span_is_numsuffix(ptr, len) -> eax = 1 if the span starts with an
; ASCII digit and contains an ASCII letter -- `12ab34`, the shape
; `docs/design/diagnostics-review.md` case c26 measured `EXS-E0210` saying
; nothing about. It does NOT decide that the literal is malformed; the lexer
; already did that, and this only chooses which sentence to print under it.
proc diag_span_is_numsuffix, ptr, len
	uses	rbx
	locals
	endl
	mov	rcx, [len]
	test	rcx, rcx
	jz	.no
	mov	rbx, [ptr]
	movzx	eax, byte [rbx]
	cmp	eax, '0'
	jb	.no
	cmp	eax, '9'
	ja	.no
	xor	rcx, rcx
  .loop:
	cmp	rcx, [len]
	jge	.no
	movzx	eax, byte [rbx + rcx]
	cmp	eax, 'A'
	jb	.next
	cmp	eax, 'Z'
	jbe	.yes
	cmp	eax, 'a'
	jb	.next
	cmp	eax, 'z'
	jbe	.yes
  .next:
	inc	rcx
	jmp	.loop
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp
