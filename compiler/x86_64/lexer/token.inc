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
;   TOK_NUMBER   aux = 0        -- an INTEGER literal: digits, or `0x` hex
;   TOK_NUMBER   aux = 1        -- a FLOAT literal: `1.5`, `1e-3` (this wave's
;                  grammar, below); same span discipline, text still undecoded
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
; `[OPEN]` TOK_NUMBER, AND THE PART THAT IS NOW SETTLED. §8.4: "Numeric
; literal grammar -- bases, separators, and float syntax -- is not settled and
; is deliberately not invented here." Integer bases beyond decimal/hex and
; separators stay `[OPEN]`; the FLOAT part is this wave's grammar, landing
; with the runtime it needs (rt/dec754.inc) and the checker that consumes it:
;
;   FloatLit ::= DIGITS '.' DIGITS [ 'e' Exp ]     -- fraction form; the
;                                                    fraction needs at least
;                                                    one digit after the `.`
;              | DIGITS 'e' Exp                     -- exponent-only: 1e-3
;   Exp      ::= ('+'|'-')? DIGITS{1,3}             -- `e` LOWERCASE ONLY
;
; The rules the lexer's `.number` path enforces, each with a reason:
;   * a `.` joins a literal only when ONE MORE byte (§8.6's peek table) is a
;     digit, so `0..n` is still three tokens, `1.` is `1` then `.` (field
;     access is `x.y`), and there is no leading-dot form;
;   * `e` is lowercase only -- the hex prefix's "one spelling" precedent (D6,
;     docs/design/wire-codec.md): a capital `E` is XID_Continue, so `1E5`
;     still reports EXS-E0210 through the unchanged digit-run rule;
;   * a literal that runs into an identifier character (`1.5f32`, `1e5x`) or
;     an exponent that does not cleanly end (`1e`, `1e+`, more than three
;     exponent digits) is EXS-E0210 -- consumed whole, reported once, the
;     same discipline the hex prefix landed with;
;   * NO SUFFIXES. `1.5` has no type of its own any more than `1` does: the
;     expectation settles it (checker/types/types.inc, `__chk_ty_fits`), and
;     a suffix spelling would be a second grammar this file would be
;     inventing.
; What is NOT settled stays refused: `_` separators and any base beyond
; decimal and `0x` still hit the digit-run rule's EXS-E0210.
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
; `-` is here (tier B, §10.1's `x86_64-linux`); a bare `/` WAS not (every `/`
; in the spec's Exsecutor source was half of a `//` comment), and `%` and `|`
; exist only inside `+%` and `+|`. The float wave changes `/`: it is the
; float-only quotient operator (§5.4's `/` on floats, grammar in the TOK_NUMBER
; note above), so `a / b` must reach the parser as a token and it is tier A of
; this wave's §8.4 amendment, not a guess. `%` and `|` alone stay
; `EXS-E0201` -- guessing at `-%`-style spellings for a float remainder is
; still §8.4's to settle, and integer `/` and remainder stay `[OPEN]`.
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
PUN_MINUS    = 22                        ; A     -      §8.4, §5.4   trapping
PUN_MINUSPCT = 23                        ; A     -%     §8.4, §5.4   wrapping
PUN_MINUSBAR = 24                        ; A     -|     §8.4, §5.4   saturating
PUN_SLASH    = 25                        ; A     /      §5.4's float-only quotient
PUN_COUNT    = 25

; PUN_MINUS was tier B and justified only by §10.1's `x86_64-linux` hyphen;
; -% and -| did not exist here at all. §8.4's operator table lists all three
; as tier A -- `- -% -|`, "negation and subtraction, three overflow modes" --
; and §8.6's additive level and ArithOp both use them. So `a -% b` lexed as
; `-` followed by a bare `%`, which is EXS-E0201, and the expression silently
; parsed as `a - b`. Found by cst/parse.inc implementing §8.6's additive
; level and discovering the tokens it needed were not producible.
;
; Appended rather than inserted: cst/parse.inc and lexer_tokens.asm both
; match on these ids, and renumbering a referenced set is the mistake §8.3
; forbids for error codes, for the same reason.

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
