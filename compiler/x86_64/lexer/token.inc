; compiler/x86_64/lexer/token.inc
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
; The token record, the token and punctuation vocabularies, and reserved-word
; lookup over the generated keywords.inc.
;
; A TOKEN IS 20 BYTES AND TWELVE OF THEM ARE THE SPAN. That ratio is the
; point. §8.3: "Every diagnostic carries a source span -- not retrofittable";
; §16 repeats it as this stage's kill criterion. Every token this lexer ever
; produces carries an rt/span.inc `Span` by value -- there is no "span-less
; token" fast path to regret later, and no second coordinate system: a token's
; span, a diagnostic's span (diag/render.inc's `Diag`) and a fix's span
; (diag/fix.inc's `DiagFix`) are all the same 12-byte record, so a token can
; be handed to a diagnostic with a field copy and no translation.
;
; `aux` IS KIND-DEPENDENT AND ITS MEANING IS FIXED PER KIND, never inferred:
;
;   TOK_EOF      aux = 0
;   TOK_IDENT    aux = rt/intern.inc id of the identifier's source bytes
;   TOK_KEYWORD  aux = KW_* (keywords.inc, generated from §8.4)
;   TOK_STRING   aux = 0        -- the value is NOT decoded here; see below
;   TOK_NUMBER   aux = 0        -- see the [OPEN] note below
;   TOK_ANNOT    aux = intern id of the name AFTER the `@`
;   TOK_PUNCT    aux = PUN_*
;
; INTERNING, AND WHY THE RAW SOURCE BYTES ARE THE RIGHT KEY. §8.2:
; "Comparison is byte equality after NFC." §8.1 requires the whole source to
; be NFC and lexer/source.inc rejects it with `EXS-E0102` when it is not --
; and lexer/lex.inc does not tokenize a file that failed the §8.1 pass at all.
; So by the time an identifier reaches `intern_id`, its bytes are already NFC
; and byte equality IS the specified comparison. No re-normalization at intern
; time, and no second normalized copy of every identifier in the arena.
;
; STRING LITERALS ARE DELIMITED, NOT DECODED. `TOK_STRING`'s span covers the
; opening quote through the closing quote inclusive, and nothing here turns
; escape sequences into the characters they denote. §8.4 says escapes exist
; ("Escapes are the only legal way to produce a bidi or invisible control
; codepoint") but does not define the escape alphabet, and inventing one is
; exactly what CLAUDE.md forbids. lexer/lex.inc therefore treats `\` as
; escaping the next codepoint FOR DELIMITING PURPOSES ONLY -- the minimum
; needed to know where the literal ends -- and leaves the value to whatever
; pass §8.4 eventually gives an escape grammar to.
;
; `[OPEN]` TOK_NUMBER. §8.4: "Numeric literal grammar -- bases, separators,
; and float syntax -- is not settled and is deliberately not invented here."
; This lexer therefore recognises only a maximal run of ASCII `0`-`9`, which
; every candidate grammar contains, and refuses (`EXS-E0210`) rather than
; guessing when such a run is immediately followed by an XID_Continue
; codepoint -- `0x10`, `1_000`, `1e5`, `10u32` all need the grammar that does
; not exist, and splitting them into two tokens would BE a grammar choice made
; silently. A following `.` is not an error, because §8.5's `per i in 0..n`
; requires `0..n` to lex as three tokens.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.2, §8.3, §8.4.
; -----------------------------------------------------------------------------

; Include order is a strict CHAIN, never a diamond -- fasmg has one flat
; namespace and a file that emits a label, a `struct` or a `proc` fails the
; second time it is processed (rt/vec.inc's header records the empirical
; finding). `diag/diag.inc` transitively brings in all of diag/ and all of
; rt/ (via fix.inc -> rt/span.inc -> intern -> map -> vec -> arena -> sys),
; so this file must NOT also include any of those directly. The three
; unicode modules include only macros/proc.inc and macros/assert.inc, which
; define macros and nothing else and are safe to reach any number of times.
include '../diag/diag.inc'
include '../../shared/unicode/nfc.inc'
include '../../shared/unicode/xid.inc'
include '../../shared/unicode/script.inc'
include 'keywords.inc'

; ---- token kinds -----------------------------------------------------------
; Small dense integers, assigned here and not derived from anything. These are
; NOT error codes and carry none of §8.3's permanence guarantee -- they are an
; internal representation, and the token stream has no consumer outside this
; repository yet (spec §15 #9: there is no phrase grammar, and the CST is
; deferred).
TOK_EOF     = 0
TOK_IDENT   = 1
TOK_KEYWORD = 2
TOK_STRING  = 3
TOK_NUMBER  = 4
TOK_ANNOT   = 5
TOK_PUNCT   = 6

; ---- punctuation, operators and sigils -------------------------------------
; Each entry records WHERE THE SPEC ATTESTS IT, in two tiers, because the two
; tiers have different standing and collapsing them would hide that:
;
;   tier A -- enumerated in §8.4's own "Operators and sigils" table, or in its
;             "Literals, comments, layout" bullets. Normative.
;   tier B -- appears in real Exsecutor source in the spec or in examples/,
;             but is NOT in §8.4's table. §8.4's table says of itself: "All
;             already in evidence in this document, recorded here rather than
;             introduced" -- so its silence about `(`, `,`, `.` and the rest
;             is an omission in the table, not a claim that Exsecutor lacks
;             them, and the source that uses them cannot be lexed without
;             them. `[OPEN]`: §8.4 owes a complete delimiter list; until it
;             has one, tier B is attestation, not enumeration.
;
; ANYTHING NOT LISTED IS `EXS-E0201` (unexpected token), deliberately. A bare
; `-` is here (tier B, §10.1's `x86_64-linux`); a bare `/` is not (every `/`
; in the spec's Exsecutor source is half of a `//` comment); `%` and `|` exist
; only inside `+%` and `+|`. Guessing at `-%`, `/` or `|` would be inventing
; operator syntax, which is §8.4's to settle and not this file's.
;                                        ; tier  glyph  first shown
PUN_AMP      = 1                         ; A     &      §8.4, §10.1  (&T)
PUN_STAR     = 2                         ; A     *      §8.4, §5.3   (*T)
PUN_COLON    = 3                         ; A     :      §8.4, §5.2   (T:maior)
PUN_DOTDOT   = 4                         ; A     ..     §8.4, §5.1   (A..B)
PUN_QUESTION = 5                         ; A     ?      §8.4, §5.1   (E?)
PUN_LT       = 6                         ; A     <      §8.4, §5.5   (<...>)
PUN_GT       = 7                         ; A     >      §8.4, §5.5   (<...>)
PUN_ARROW    = 8                         ; A     ->     §8.4, §4.2
PUN_PLUS     = 9                         ; A     +      §8.4, §5.4   trapping
PUN_PLUSPCT  = 10                        ; A     +%     §8.4, §5.4   wrapping
PUN_PLUSBAR  = 11                        ; A     +|     §8.4, §5.4   saturating
PUN_LBRACE   = 12                        ; A     {      §8.4 "Blocks are { }"
PUN_RBRACE   = 13                        ; A     }      §8.4 "Blocks are { }"
PUN_LPAREN   = 14                        ; B     (      §4.2, examples/saluta.exsc
PUN_RPAREN   = 15                        ; B     )      §4.2, examples/saluta.exsc
PUN_LBRACKET = 16                        ; B     [      §10.1 (hospites [ ... ])
PUN_RBRACKET = 17                        ; B     ]      §10.1 (hospites [ ... ])
PUN_COMMA    = 18                        ; B     ,      §10.1, examples/imprime.exsc
PUN_SEMI     = 19                        ; B     ;      examples/imprime.exsc
PUN_DOT      = 20                        ; B     .      examples/imprime.exsc (s.scribe)
PUN_EQ       = 21                        ; B     =      examples/imprime.exsc (firma s = ...)
PUN_MINUS    = 22                        ; B     -      §10.1 (x86_64-linux)
PUN_COUNT    = 22

; ---- the token record ------------------------------------------------------
; 20 bytes: two u32 and an embedded 12-byte `Span`. Written `span Span`, not
; `field span, Span` -- the `field` sugar expands to `name decl ?` and a
; trailing `?` is not accepted after a struct name; diag/render.inc's `Diag`
; and diag/fix.inc's `DiagFix` embed a `Span` exactly the same way. Field
; access is nested: `Tok.span.start`, never `Tok.span + Span.start`, so the
; operand size is the FIELD's and not the whole struct's.
struct Tok
	field kind, dd
	field aux,  dd
	span Span
end struct

; ---- reserved-word lookup --------------------------------------------------

; kw_lookup(ptr, len) -> eax = the KW_* id of the reserved word spelled by the
; `len` bytes at `ptr`, or KW_NONE (0) if those bytes are not a reserved word.
; Cannot fail; leaves CF clear.
;
; Length-bucketed, then a byte compare: `kw_len_start[len]` and
; `kw_len_start[len+1]` bound the slice of `kw_sorted` holding every reserved
; word of exactly that length, so a miss costs at most eight short compares
; (keywords.inc's header carries the bucket sizes) instead of thirty. No hash:
; §9.3 forbids anything whose order can depend on a hash function from
; reaching output, and rt/map.inc's header makes the same argument at length.
;
; A length outside [KW_MIN_LEN, KW_MAX_LEN] misses before touching the tables
; -- which is the common case, since most identifiers are not two-to-eleven
; bytes of ASCII lowercase, and every non-ASCII identifier misses here for
; free (§8.2: "Non-ASCII identifiers are allowed. Non-ASCII keywords are
; not." -- a keyword is ASCII by construction, so a candidate containing any
; byte >= 0x80 simply fails the byte compare).
proc kw_lookup, ptr, len
	uses	rbx, r12, r13
	locals
	endl
	mov	rax, [len]
	cmp	rax, KW_MIN_LEN
	jb	.miss
	cmp	rax, KW_MAX_LEN
	ja	.miss

	movzx	ebx,  byte [kw_len_start + rax]		; first index of bucket
	inc	rax
	movzx	r13d, byte [kw_len_start + rax]		; one past its last
  .probe:
	cmp	ebx, r13d
	jae	.miss
	movzx	r12d, byte [kw_sorted + rbx]		; canonical index
	lea	rsi, [kw_text]
	mov	eax, [kw_off + r12*4]
	add	rsi, rax
	mov	rdi, [ptr]
	mov	rcx, [len]
	repe	cmpsb					; DF is clear per SysV
	je	.hit
	inc	rbx
	jmp	.probe
  .hit:
	; canonical index is 0-based; KW_* ids are 1-based so that KW_NONE = 0
	; stays free as the miss value -- the same sentinel convention
	; rt/intern.inc uses for interned ids.
	lea	eax, [r12 + 1]
	return
  .miss:
	mov	eax, KW_NONE
	return
endp

; kw_text_of(id) -> rax = pointer to the reserved word's bytes, rdx = its
; length. `id` must be a KW_* value (1..KW_COUNT); anything else is an
; internal contract violation and traps (docs/asm-conventions.md, "1.3 Error
; protocol: CF / eax" -- the rassert channel), because the only way to obtain
; an id here is from `kw_lookup`, which never returns an out-of-range one.
; The bytes live in this translation unit's own read-only data; they are not
; a copy and must not be written through.
proc kw_text_of, id
	locals
	endl
	mov	ecx, [id]
	rassert	ecx ge 1
	rassert	ecx le KW_COUNT
	dec	ecx
	lea	rax, [kw_text]
	mov	r8d, [kw_off + rcx*4]
	add	rax, r8
	movzx	edx, byte [kw_len + rcx]
	return
endp
