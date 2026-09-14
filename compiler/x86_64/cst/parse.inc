; compiler/x86_64/cst/parse.inc
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
; THE PARSER. §8.6's phrase grammar, hand-written recursive descent, producing
; the lossless red-green CST of §9.1. §8.6 is `[UNTESTED]` and normative and
; says so of itself: "no parser exists, and nothing below has parsed a single
; file." This file is the first thing to exercise it, so where it and §8.6
; disagree, the disagreement is written down here rather than smoothed over.
;
; WHAT §8.6 GOT WRONG OR LEFT OPEN, found by running rather than by reading.
; Each of these is REPORTED, not silently worked around:
;
;   1. `Annotation ::= '@' IDENT` IS ONE TOKEN, NOT TWO. §8.4's operator table
;      lists `@nomen` as a single "annotation" token and lexer/lex.inc emits
;      exactly that -- one `TOK_ANNOT` whose span covers the `@` and whose
;      `aux` is the intern id of the name. §8.6's grammar line writes two
;      terminals. The lexer follows §8.4; this parser follows the lexer, and
;      `CST_ANNOTATION` holds one leaf. §8.6's line is stale.
;   2. `-%` AND `-|` ARE IN §8.4 AND ARE NOT IN THE LEXER. §8.6's additive
;      level and its `ArithOp` both list `-%` and `-|`, and so does §8.4's own
;      "Operators and sigils" table ("`-` `-%` `-|` -- negation and
;      subtraction, three overflow modes"). lexer/token.inc has `PUN_MINUS`
;      only, and its header states the reason it stopped there: "`%` and `|`
;      exist only inside `+%` and `+|` ... Guessing at `-%`, `/` or `|` would
;      be inventing operator syntax". That reasoning is right in general and
;      wrong in this instance -- §8.4 attests both tokens explicitly, in the
;      half of the table added when the lexer found the first half incomplete.
;      So `a -% b` is today a LEXER error (`EXS-E0201`, unrecognised `%`) and
;      never reaches this file. This parser accepts every subtraction token
;      the lexer can produce and no others; the missing two are the lexer's to
;      add. REPORTED to the lexer agent, not worked around here -- a parser
;      cannot invent a token.
;   3. THE `functio IDENT` FIX IS NOT A SINGLE-SPAN EDIT. §8.6's peek table
;      wants `EXS-E0201` at a nested named function "fix `firma nomen =
;      functio(…) … ;`". That transformation inserts in three places at once
;      (`firma ` before, ` = ` after the name, `;` after the closing brace) and
;      diag/fix.inc's payload is one span and one replacement text -- by
;      design, since `exsc emenda` applies it mechanically. Emitting a partial
;      insertion would produce source that does not compile. So the diagnostic
;      is raised with NO fix payload, and the construct is then parsed as the
;      function declaration the author obviously wrote, which is the recovery
;      that loses least. §8.6 owes either a multi-edit fix payload or a weaker
;      claim.
;   4. NO NODE PER PRECEDENCE LEVEL WHEN THE LEVEL DID NOT APPLY -- see
;      cst/kinds.inc's header, departure 1. Twelve nested nodes per atom is what
;      a literal reading produces.
;   5. `ExprNS` WAS EXACTLY `Expr` UNTIL STRUCT LITERALS WERE SETTLED. §8.6
;      defines `ExprNS` as "the expression grammar with that suffix disabled",
;      where the suffix is the struct literal; while that was `[OPEN]` the two
;      grammars coincided and `__cst_expr_ns` existed only so that admitting
;      the literal later would touch level 1 and nothing else. It did:
;      docs/design/wire-codec.md D3 settled the syntax, `__cst_struct_lit`
;      parses it, and the whole difference between the two grammars is one
;      flag (`CstParser.nolit`) set by `__cst_expr_ns`, cleared by
;      `__cst_expr_lit` inside brackets and blocks, and read by
;      `__cst_postfix` alone.
;   6. `'abi'` IS WRITTEN AS A TERMINAL AND IS NOT ONE. §8.6's `ExternusBlock`
;      spells `'externus' '(' STRING ',' 'abi' ':' IDENT ')'`, but §8.4 lists
;      `abi` under tier 2 -- contextual, "meaningful only in a specific
;      position, and ordinary identifiers everywhere else". This parser reads
;      it as the IDENT it lexes as and does not check its spelling: a phrase
;      grammar that branched on the TEXT of an identifier would be doing
;      lexical work, and §5.3 has to check the abi name against the supported
;      set anyway. A misspelled `abi` is therefore a semantic diagnostic, not
;      `EXS-E0201`. §8.6's notation does not distinguish the two kinds of
;      terminal; that is the gap.
;   7. THE EGO ENTRY POINT IS NOT IMPLEMENTED, and cannot be. §8.6's grammar
;      block includes `EgoFile`, and its own "Not settled here" says why it is
;      unreachable: `fontes` needs a `HASH` token, and "`sha256-1a2b…` is
;      neither identifier nor literal under any settled rule, so `fontes`
;      cannot yet be lexed." A parser for a production whose terminals do not
;      exist would be prose, not code. `cst_parse` is the module entry point
;      only; the `ego` entry point is `[OPEN]` and blocked on §8.4.
;   8. ARRAY LITERALS ARE A PRIMARY, NOT A SUFFIX, and that is the whole of
;      what §8.6 needed to admit them. `__cst_primary` takes a `[` where an
;      OPERAND is expected and `__cst_postfix` takes the same token after one,
;      so `a[0]` is an index and `[a][0]` is a one-element literal indexed --
;      "position decides, never the token", which §8.6 states of `&` and `*`
;      and which this is the third use of. No new peek is owed: §8.6's own
;      `[OPEN]` entry for array literals says as much, and the two forms share
;      their prefix (`'[' Expr`) and are told apart by the token AFTER it,
;      the way `sub … ;` and `sub … {` are. The decisions the productions
;      implement -- two forms, commas mandatory, no trailing comma, `[]`
;      refused, an `INT` count -- are §8.6's; `__cst_array_lit`'s header
;      states each one and what it is refused with.
;
; §8.6 WAS AMENDED WHILE THIS FILE WAS BEING WRITTEN, and this file follows the
; amended text: `ForStmt`'s reduction clause became `['forma' IDENT [INT]]`
; when `docs/design/typed-ast.md` observed that §5.4's `arborea w` could not be
; lowered without the width. `__cst_for_stmt` takes the optional `INT` and
; leaves the arborea/ordinata pairing to the checker, which is what the
; amendment asks for.
;
; TRIVIA IS A GAP, NOT A TOKEN. lexer/lex.inc skips whitespace and comments
; without emitting anything, so this file reconstructs them: the bytes between
; the end of one token's span and the start of the next are, by construction,
; exactly the trivia between them. That reconstruction is total -- every byte
; of the file is inside a token span or inside a gap -- which is what makes
; the round trip in cst/red.inc byte-exact. It also catches the bytes the
; lexer consumed WITHOUT emitting a token (the `EXS-E0210` malformed-literal
; run, the `EXS-E0202` unterminated string): those land in a gap too, and are
; kept as `CST_TK_JUNK` rather than dropped. A lossless tree does not get to
; be selective about which bytes it lost.
;
; §8.1 IS STILL A GATE, AND THAT CONTRADICTS §8.6. §8.6's recovery paragraph
; says "codepoints §8.1 rejects are emitted as error tokens so the tree stays
; lossless". lexer/lex.inc does not tokenize such a file AT ALL -- §8.1 is a
; gate, and its header defends that at length: attaching §8.4 spans to text
; §8.1 has said cannot be read is worse than refusing. Both positions are
; defensible and they cannot both hold. The lexer's is implemented and this
; parser is written against it: a file that fails §8.1 never reaches here, so
; the sentence in §8.6 describes a path that does not exist. REPORTED.
;
; PROGRESS IS AN INVARIANT, NOT A HOPE. Every loop in this file consumes at
; least one token per iteration or exits. The three places where that is not
; automatic -- the module item loop, the block statement loop, and every
; comma-separated list -- go through `__cst_error_skip`, which always eats at
; least one token before it starts looking for a synchronisation point. An
; error-tolerant parser that can spin is not error-tolerant.
;
; ONE DIAGNOSTIC PER TOKEN. `__cst_err` refuses to report twice at the same
; token index. Without it, a single stray token in a nested expression
; produces one `EXS-E0201` per enclosing production, and §16's kill criterion
; is diagnostics quality.
;
; NO SECOND DIAGNOSTIC PATH. Everything here goes through `lex_diag` and
; `lex_kw_as_ident` -- the lexer owns the `Lexer` that carries the source, the
; path and the `diags` vector, and `EXS-E0220`'s machine-applicable fix is
; derived in exactly one place in the compiler, as lexer/lex.inc's header
; asks: "the eventual parser calls the same routine for every other position
; rather than building a second E0220."
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.6 (the whole of it), §8.3, §8.4,
; §9.1; §13 for every code raised here.
; -----------------------------------------------------------------------------

include 'red.inc'

; The leaf kind of a token is its `TOK_*` plus one. Asserted rather than
; commented: cst/kinds.inc assigns the leaf kinds in lexer/token.inc's order
; on purpose, and a renumbering of either file must fail the build here rather
; than silently mislabel every leaf in the tree.
assert CST_TK_EOF     = TOK_EOF     + 1
assert CST_TK_IDENT   = TOK_IDENT   + 1
assert CST_TK_KEYWORD = TOK_KEYWORD + 1
assert CST_TK_STRING  = TOK_STRING  + 1
assert CST_TK_NUMBER  = TOK_NUMBER  + 1
assert CST_TK_ANNOT   = TOK_ANNOT   + 1
assert CST_TK_PUNCT   = TOK_PUNCT   + 1

; ---- synchronisation sets (§8.6, "Recovery") -------------------------------
; "Synchronisation sets are small and fixed." These are those sets, and there
; are no others. Every one of them also stops at `}` and at end of input,
; which is §8.6's "No production consumes a `}` it did not open" made
; operational: recovery inside a body can always give the `}` back to whoever
; opened it.
CST_SYNC_MODULE = 1	; `@`, `publica`, the item-leading keywords
CST_SYNC_STMT   = 2	; `;`, `{`, the statement-leading keywords
CST_SYNC_LIST   = 3	; `,` and the caller's closing bracket
CST_SYNC_MEMBER = 4	; the `IDENT :` peek, `functio`, `publica`
CST_SYNC_CASUS  = 5	; `casus`, `aliter`

; The two-byte contextual operator words (§8.4 tier 2, §8.6 decision 6),
; compared as one 16-bit load rather than through a string table: all six
; comparisons and `et` are exactly two bytes. Written as explicit shifts
; because a two-character literal's byte order is an assembler convention,
; and this file should not depend on one. The words of any OTHER length --
; `vel`, and since spec §8.6 gained levels 5a/5b `aut`, `sursum`, `deorsum` --
; go through `__cst_word` and the spellings in `cst_w_*` below. This comment
; used to say `vel` was "the only three-byte one", and `__cst_vel` hard-coded
; three bytes: true when written, and exactly the assumption a six- and a
; seven-byte word would have broken.
CST_W_LT = 'l' + ('t' shl 8)
CST_W_LE = 'l' + ('e' shl 8)
CST_W_GT = 'g' + ('t' shl 8)
CST_W_GE = 'g' + ('e' shl 8)
CST_W_EQ = 'e' + ('q' shl 8)
CST_W_NE = 'n' + ('e' shl 8)
CST_W_ET = 'e' + ('t' shl 8)

; ---- parser state ----------------------------------------------------------
; The `Lexer` is borrowed, not copied: it already owns the source bytes, the
; path, the file id, the token vector, the arena, the interner AND the
; diagnostic vector, so a parser that carried its own copies would be a second
; place for them to disagree.
; THE OPEN-DELIMITER STACK is `depth` with its history kept. `depth` alone
; answers `EXS-E0202`-or-`EXS-E0203`; it cannot answer WHERE the still-open
; delimiter was written, and `docs/design/diagnostics-review.md` D6 is exactly
; that question -- "`EXS-E0202` never names the construct it thinks is
; unterminated ... the parser knows the opener, since it is what the recovery
; rule *no production consumes a `}` it did not open* is written around". So
; `__cst_bump` now records the span and the `PUN_*` of every delimiter it
; opens, in the same statement that increments `depth`, and pops in the same
; statement that decrements it. The two can therefore not drift: there is
; still exactly one notion of "open" in this file.
;
; BOUNDED, AND HONEST ABOUT IT. Past `CST_OPEN_MAX` the entries are not
; recorded and `__cst_open_top` answers "unknown" rather than a stale entry --
; a diagnostic with no related span is what the record looked like yesterday,
; and a related span pointing at the wrong `{` is worse than none. `depth`
; itself keeps counting, so nothing about the E0202/E0203 decision is capped.
; 64 is chosen against §8.6's own nesting: the deepest construct in the
; corpus is 6, and 64 levels of live brackets is already a file no reader
; will follow.
CST_OPEN_MAX    = 64
CST_OPEN_STRIDE = 16		; (start, len, pun, pad) x dd

struct CstParser
	field lx,        dq	; -> Lexer
	field tree,      dq	; -> CstTree
	field ti,        dq	; index of the current token
	field pos,       dq	; first source byte not yet in the tree
	field depth,     dd	; open (, [, {, < -- decides E0202 vs E0203
	field lasterr,   dd	; token index the last error was reported at
	field ndiag,     dd	; diagnostics THIS parse recorded
	field firstcode, dd	; numeric part of the first one; 0 = none
	field nolit,     dd	; 1 inside `ExprNS`: no struct-literal suffix
	field nlpad,     dd
	ostk dd CST_OPEN_MAX*4 dup (?)	; the open delimiters, innermost last
end struct

; ---- punctuation spelling --------------------------------------------------
; The glyphs, for the §8.3 fix payload a missing token carries (§8.6 decision
; 1: "A missing `;` is EXS-E0201 with the insertion as its fix payload"). Two
; bytes per entry because `..`, `->`, `+%` and `+|` are the longest tokens
; §8.4 admits; the length column is separate because a fix's text is a
; (pointer, length) pair and must not include the pad byte.
cst_pun_txt:
	db	'&',0		; PUN_AMP
	db	'*',0		; PUN_STAR
	db	':',0		; PUN_COLON
	db	'..'		; PUN_DOTDOT
	db	'?',0		; PUN_QUESTION
	db	'<',0		; PUN_LT
	db	'>',0		; PUN_GT
	db	'->'		; PUN_ARROW
	db	'+',0		; PUN_PLUS
	db	'+%'		; PUN_PLUSPCT
	db	'+|'		; PUN_PLUSBAR
	db	'{',0		; PUN_LBRACE
	db	'}',0		; PUN_RBRACE
	db	'(',0		; PUN_LPAREN
	db	')',0		; PUN_RPAREN
	db	'[',0		; PUN_LBRACKET
	db	']',0		; PUN_RBRACKET
	db	',',0		; PUN_COMMA
	db	';',0		; PUN_SEMI
	db	'.',0		; PUN_DOT
	db	'=',0		; PUN_EQ
	db	'-',0		; PUN_MINUS
	db	'-%'		; PUN_MINUSPCT
	db	'-|'		; PUN_MINUSBAR
	db	'/',0		; PUN_SLASH -- the float quotient (this wave)
cst_pun_len:
	db	1,1,1,2,1,1,1,2,1,2,2,1,1,1,1,1,1,1,1,1,1,1
	db	2,2			; PUN_MINUSPCT `-%`, PUN_MINUSBAR `-|`
	db	1			; PUN_SLASH `/`
cst_pun_len_end:
; A PUN_* the table does not spell would produce a fix that inserts the wrong
; bytes -- silently, since a wrong glyph still assembles. Checked here instead.
assert (cst_pun_len_end - cst_pun_len) = PUN_COUNT
assert (cst_pun_len - cst_pun_txt) = PUN_COUNT * 2

; __cst_pun_text(id) -> rax = the glyph's bytes, rdx = its length.
proc __cst_pun_text, id
	locals
	endl
	mov	ecx, [id]
	rassert	ecx ge 1
	rassert	ecx le PUN_COUNT
	dec	ecx
	lea	rax, [cst_pun_txt]
	lea	rax, [rax + rcx*2]
	movzx	edx, byte [cst_pun_len + rcx]
	return
endp

; ---- looking at tokens -----------------------------------------------------

; __cst_tokp(p, n) -> rax = the `Tok` `n` positions ahead of the cursor,
; CLAMPED to the last token -- which lex_tokenize guarantees is a `TOK_EOF`
; whose span is the zero-length range at end of input. Clamping rather than
; bounds-erroring is what lets every peek in §8.6's table be written without a
; length check first: past the end, every question is answered by EOF.
proc __cst_tokp, p, n
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rbx, [rbx + CstParser.lx]
	mov	rbx, [rbx + Lexer.toks]
	mov	rcx, [rbx + Vec.len]
	rassert	rcx ge 1
	mov	rax, [p]
	mov	rax, [rax + CstParser.ti]
	add	rax, [n]
	dec	rcx
	cmp	rax, rcx
	jbe	.ok
	mov	rax, rcx
  .ok:
	mov	rdi, rbx
	mov	rsi, rax
	call	vec_get
	return
endp

; __cst_kindn(p, n) -> eax = the `TOK_*` kind `n` tokens ahead.
proc __cst_kindn, p, n
	locals
	endl
	mov	rdi, [p]
	mov	rsi, [n]
	call	__cst_tokp
	mov	eax, [rax + Tok.kind]
	return
endp

; __cst_is(p, n, kind, aux) -> eax = 1 if the token `n` ahead has that kind and
; that `aux`. `aux` is meaningful for `TOK_KEYWORD` (a `KW_*`) and `TOK_PUNCT`
; (a `PUN_*`); pass -1 to ignore it.
proc __cst_is, p, n, kind, aux
	uses	rbx
	locals
	endl
	mov	rdi, [p]
	mov	rsi, [n]
	call	__cst_tokp
	mov	ecx, [kind]
	cmp	[rax + Tok.kind], ecx
	jne	.no
	mov	ecx, [aux]
	cmp	ecx, -1
	je	.yes
	cmp	[rax + Tok.aux], ecx
	jne	.no
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __cst_pun(p, id) -> eax = 1 if the CURRENT token is that punctuation.
proc __cst_pun, p, id
	locals
	endl
	mov	rdi, [p]
	xor	esi, esi
	mov	edx, TOK_PUNCT
	mov	ecx, [id]
	call	__cst_is
	return
endp

; __cst_kw(p, id) -> eax = 1 if the CURRENT token is that reserved word.
proc __cst_kw, p, id
	locals
	endl
	mov	rdi, [p]
	xor	esi, esi
	mov	edx, TOK_KEYWORD
	mov	ecx, [id]
	call	__cst_is
	return
endp

; __cst_word2(p, n) -> eax = the two bytes of the token `n` ahead as a 16-bit
; value if it is a two-byte identifier, else 0. This is how the contextual
; operator words of §8.6 decision 6 are recognised: `lt le gt ge eq ne et` are
; all exactly two bytes, they are ordinary identifiers everywhere else, and a
; table lookup for a two-byte compare would cost more than it saves.
proc __cst_word2, p, n
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	rsi, [n]
	call	__cst_tokp
	cmp	dword [rax + Tok.kind], TOK_IDENT
	jne	.no
	cmp	dword [rax + Tok.span.len], 2
	jne	.no
	mov	rcx, [rbx + CstParser.lx]
	mov	rcx, [rcx + Lexer.src_ptr]
	mov	edx, [rax + Tok.span.start]
	movzx	eax, word [rcx + rdx]
	return
  .no:
	xor	eax, eax
	return
endp

; The contextual operator words that are not two bytes long. Each is its bytes
; and nothing else; the length is taken from the label distance, so a spelling
; and its length cannot disagree.
cst_w_vel:	db	'vel'
CST_WL_VEL = $ - cst_w_vel
cst_w_aut:	db	'aut'
CST_WL_AUT = $ - cst_w_aut
cst_w_sursum:	db	'sursum'
CST_WL_SURSUM = $ - cst_w_sursum
cst_w_deorsum:	db	'deorsum'
CST_WL_DEORSUM = $ - cst_w_deorsum

; __cst_word(p, n, cwptr, cwlen) -> eax = 1 if the token `n` ahead is an
; IDENTIFIER whose bytes are exactly `cwptr[0..cwlen)`.
;
; Only a `TOK_IDENT` can match. That is the whole of what makes these words
; contextual (§8.4 tier 2): the parser asks this question only in OPERATOR
; position, and in operand position the same bytes are an ordinary name --
; `firma aut = 1;` binds a variable called `aut`, and `aut aut aut` is an
; exclusive or of it with itself. A byte compare, not an interner lookup,
; because the lexer's interner ids are the lexer's, and asking it to intern
; four operator spellings up front would be a second place for the words to
; be listed.
proc __cst_word, p, n, cwptr, cwlen
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	rsi, [n]
	call	__cst_tokp
	cmp	dword [rax + Tok.kind], TOK_IDENT
	jne	.no
	mov	ecx, [rax + Tok.span.len]
	cmp	rcx, [cwlen]
	jne	.no
	mov	rdx, [rbx + CstParser.lx]
	mov	rdx, [rdx + Lexer.src_ptr]
	mov	eax, [rax + Tok.span.start]
	add	rdx, rax		; -> the token's first byte
	mov	rsi, [cwptr]
	xor	eax, eax
  .each:
	cmp	rax, rcx
	jae	.yes
	mov	r8b, [rdx + rax]
	cmp	r8b, [rsi + rax]
	jne	.no
	inc	rax
	jmp	.each
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __cst_vel(p) -> eax = 1 if the current token is the identifier `vel`.
proc __cst_vel, p
	locals
	endl
	mov	rdi, [p]
	xor	esi, esi
	lea	rdx, [cst_w_vel]
	mov	ecx, CST_WL_VEL
	call	__cst_word
	return
endp

; __cst_aut(p) -> eax = 1 if the current token is the identifier `aut`
; (spec §8.6 level 5b, exclusive or).
proc __cst_aut, p
	locals
	endl
	mov	rdi, [p]
	xor	esi, esi
	lea	rdx, [cst_w_aut]
	mov	ecx, CST_WL_AUT
	call	__cst_word
	return
endp

; __cst_is_shiftop(p) -> eax = 1 at `sursum` or `deorsum` (spec §8.6 level
; 5a, `ShiftOp`).
proc __cst_is_shiftop, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	lea	rdx, [cst_w_sursum]
	mov	ecx, CST_WL_SURSUM
	call	__cst_word
	test	eax, eax
	jnz	.out
	mov	rdi, rbx
	xor	esi, esi
	lea	rdx, [cst_w_deorsum]
	mov	ecx, CST_WL_DEORSUM
	call	__cst_word
  .out:
	return
endp

; ---- growing the tree ------------------------------------------------------

; __cst_leaf(p, kind, off, len) -- one leaf covering source bytes [off, off+len).
proc __cst_leaf, p, kind, off, len
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, [kind]
	mov	rdx, [rbx + CstParser.lx]
	mov	rdx, [rdx + Lexer.src_ptr]
	add	rdx, [off]
	mov	rcx, [len]
	call	cst_emit_token
	return
endp

; __cst_trivia(p, upto) -- emit every byte between the cursor position and
; `upto` as trivia leaves. See this file's header, "TRIVIA IS A GAP".
;
; The gap is classified rather than emitted as one blob, because a formatter
; (§12) has to be able to tell a blank line from a comment, and a re-scan of
; the gap later would be a second, divergent lexer. Three classes, and the
; third one is the interesting one: bytes the lexer consumed WITHOUT emitting
; a token are neither whitespace nor a comment, and they are kept.
proc __cst_trivia, p, upto
	uses	rbx, r12, r13, r14
	locals
		slot	sbase, dq	; -> source bytes
		slot	tk,    dq	; kind of the run being scanned
		slot	rend2, dq	; one past the run being emitted
	endl
	mov	rbx, [p]
	mov	r12, [rbx + CstParser.lx]
	mov	r12, [r12 + Lexer.src_ptr]
	mov	[sbase], r12
	mov	r13, [rbx + CstParser.pos]
	mov	r14, [upto]
	rassert	r13 le r14
  .run:
	cmp	r13, r14
	jae	.done
	movzx	eax, byte [r12 + r13]
	cmp	al, 0x20
	je	.space
	cmp	al, 0x09
	je	.space
	cmp	al, 0x0A
	je	.space
	cmp	al, '/'
	jne	.junk
	lea	rax, [r13 + 1]
	cmp	rax, r14
	jae	.junk
	cmp	byte [r12 + rax], '/'
	jne	.junk

	; `//` to end of line (§8.4). The LF itself is whitespace, not part of
	; the comment -- one blank line and one comment then differ in the tree
	; the way they differ in the file.
	mov	qword [tk], CST_TK_COMMENT
	mov	rax, r13
  .cmt:
	inc	rax
	cmp	rax, r14
	jae	.emit
	cmp	byte [r12 + rax], 0x0A
	jne	.cmt
	jmp	.emit

  .space:
	mov	qword [tk], CST_TK_WS
	mov	rax, r13
  .ws:
	inc	rax
	cmp	rax, r14
	jae	.emit
	movzx	ecx, byte [r12 + rax]
	cmp	cl, 0x20
	je	.ws
	cmp	cl, 0x09
	je	.ws
	cmp	cl, 0x0A
	je	.ws
	jmp	.emit

	; Anything else in a gap is source the lexer consumed and did not
	; tokenize: the `EXS-E0210` run and the `EXS-E0202` unterminated
	; literal are the two ways that happens today. It is already
	; diagnosed; this file's only job is to not lose it.
  .junk:
	mov	qword [tk], CST_TK_JUNK
	mov	rax, r13
  .jk:
	inc	rax
	cmp	rax, r14
	jae	.emit
	movzx	ecx, byte [r12 + rax]
	cmp	cl, 0x20
	je	.emit
	cmp	cl, 0x09
	je	.emit
	cmp	cl, 0x0A
	je	.emit
	cmp	cl, '/'
	jne	.jk
	lea	rcx, [rax + 1]
	cmp	rcx, r14
	jae	.jk
	cmp	byte [r12 + rcx], '/'
	jne	.jk

  .emit:
	mov	rdi, rbx
	mov	rsi, [tk]
	mov	rdx, r13
	mov	rcx, rax
	sub	rcx, r13
	mov	[rend2], rax
	call	__cst_leaf
	mov	r13, [rend2]
	jmp	.run
  .done:
	mov	[rbx + CstParser.pos], r14
	return
endp

; __cst_bump(p) -- the current token, with its leading trivia, becomes a leaf,
; and the cursor advances. Traps at `TOK_EOF`: end of input is consumed once,
; by `__cst_eat_eof`, and a loop that bumps it twice would emit two EOF leaves
; and break the round trip. Every loop in this file therefore tests for EOF
; before it bumps -- which the trap is here to enforce rather than trust.
proc __cst_bump, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_tokp
	mov	r12, rax
	mov	ecx, [r12 + Tok.kind]
	rassert	ecx ne TOK_EOF

	mov	rdi, rbx
	mov	esi, [r12 + Tok.span.start]
	call	__cst_trivia

	mov	rdi, rbx
	mov	eax, [r12 + Tok.kind]
	inc	eax
	mov	esi, eax
	mov	edx, [r12 + Tok.span.start]
	mov	ecx, [r12 + Tok.span.len]
	call	__cst_leaf

	mov	eax, [r12 + Tok.span.start]
	add	eax, [r12 + Tok.span.len]
	mov	[rbx + CstParser.pos], rax
	inc	qword [rbx + CstParser.ti]

	; Bracket depth, which is the whole of the E0202-vs-E0203 decision
	; (§8.6, "Recovery"). `<` and `>` are counted because §8.6 decision 6
	; makes them delimiters and never operators.
	cmp	dword [r12 + Tok.kind], TOK_PUNCT
	jne	.done
	mov	ecx, [r12 + Tok.aux]
	cmp	ecx, PUN_LPAREN
	je	.open
	cmp	ecx, PUN_LBRACKET
	je	.open
	cmp	ecx, PUN_LBRACE
	je	.open
	cmp	ecx, PUN_LT
	je	.open
	cmp	ecx, PUN_RPAREN
	je	.close
	cmp	ecx, PUN_RBRACKET
	je	.close
	cmp	ecx, PUN_RBRACE
	je	.close
	cmp	ecx, PUN_GT
	je	.close
  .done:
	return
	; Push and increment in one place, pop and decrement in another, so
	; `depth` and the stack cannot disagree about what is open. Past
	; CST_OPEN_MAX the entry is dropped and only the counter moves --
	; `__cst_open_top` reads the same bound and answers "unknown" there.
  .open:
	mov	eax, [rbx + CstParser.depth]
	cmp	eax, CST_OPEN_MAX
	jae	.open_count
	lea	rdx, [rbx + CstParser.ostk]
	shl	eax, 4
	add	rdx, rax
	mov	eax, [r12 + Tok.span.start]
	mov	[rdx], eax
	mov	eax, [r12 + Tok.span.len]
	mov	[rdx + 4], eax
	mov	eax, [r12 + Tok.aux]
	mov	[rdx + 8], eax
	mov	dword [rdx + 12], 0
  .open_count:
	inc	dword [rbx + CstParser.depth]
	return
  .close:
	cmp	dword [rbx + CstParser.depth], 0
	je	.done
	dec	dword [rbx + CstParser.depth]
	return
endp

; __cst_open_top(p) -> rax = the innermost open delimiter's stack entry
; (start, len, pun), or 0 if nothing is open -- or if more is open than
; CST_OPEN_MAX recorded, in which case the innermost one is genuinely not
; known and saying so is the only honest answer.
proc __cst_open_top, p
	locals
	endl
	mov	rcx, [p]
	mov	eax, [rcx + CstParser.depth]
	test	eax, eax
	jz	.none
	cmp	eax, CST_OPEN_MAX
	ja	.none
	dec	eax
	shl	eax, 4
	lea	rcx, [rcx + CstParser.ostk]
	add	rcx, rax
	mov	rax, rcx
	return
  .none:
	xor	eax, eax
	return
endp

; __cst_open_has(p, closer) -> eax = 1 if some delimiter currently open would
; be closed by `closer`. §8.6's "No production consumes a `}` it did not open"
; generalised to all four bracket kinds: a production may stand a MISMATCHED
; closer in for the one it wanted only when no enclosing production is still
; waiting for that closer.
;
; ANSWERS 1 -- "yes, leave it alone" -- for anything it cannot decide: a token
; that is not a closer at all, and a depth past CST_OPEN_MAX where the stack
; no longer knows what is open. Every caller uses a 0 here to CONSUME a token,
; so the unsafe direction is the one that must never be guessed.
proc __cst_open_has, p, closer
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	ecx, [closer]
	mov	eax, PUN_LPAREN
	cmp	ecx, PUN_RPAREN
	je	.have
	mov	eax, PUN_LBRACKET
	cmp	ecx, PUN_RBRACKET
	je	.have
	mov	eax, PUN_LBRACE
	cmp	ecx, PUN_RBRACE
	je	.have
	mov	eax, PUN_LT
	cmp	ecx, PUN_GT
	je	.have
	mov	eax, 1
	return
  .have:
	mov	edx, eax
	mov	eax, [rbx + CstParser.depth]
	cmp	eax, CST_OPEN_MAX
	ja	.yes
	test	eax, eax
	jz	.no
	lea	rcx, [rbx + CstParser.ostk]
  .scan:
	cmp	[rcx + 8], edx
	je	.yes
	add	rcx, CST_OPEN_STRIDE
	dec	eax
	jnz	.scan
  .no:
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __cst_stray_closer(p) -> eax = the `PUN_*` of a closing bracket at the
; cursor that closes nothing currently open, or 0.
;
; `}` IS DELIBERATELY NOT A CANDIDATE. §8.6 writes one recovery rule about one
; delimiter -- "no production consumes a `}` it did not open" -- and a
; bracket production swallowing a `}` as a stand-in for its own `)` is the
; letter of that sentence broken to save a cascade diagnostic. The three
; bracket closers §8.6 says nothing specific about are enough for the case
; this exists for (`a[0)`), and leaving `}` out costs nothing measured.
proc __cst_stray_closer, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_tokp
	mov	r12, rax
	cmp	dword [r12 + Tok.kind], TOK_PUNCT
	jne	.no
	mov	eax, [r12 + Tok.aux]
	cmp	eax, PUN_RPAREN
	je	.cand
	cmp	eax, PUN_RBRACKET
	je	.cand
	cmp	eax, PUN_GT
	je	.cand
	xor	eax, eax
	return
  .cand:
	mov	rdi, rbx
	mov	esi, eax
	call	__cst_open_has
	test	eax, eax
	jnz	.no
	mov	eax, [r12 + Tok.aux]
	return
  .no:
	xor	eax, eax
	return
endp

; __cst_eat_eof(p) -- the trailing trivia, then the zero-width EOF leaf. Called
; exactly once, by `__cst_module`. Without it the bytes after the last real
; token -- a trailing comment, the final newline -- would not be in the tree.
proc __cst_eat_eof, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_tokp
	mov	r12, rax
	mov	ecx, [r12 + Tok.kind]
	rassert	ecx eq TOK_EOF
	mov	rdi, rbx
	mov	esi, [r12 + Tok.span.start]
	call	__cst_trivia
	mov	rdi, rbx
	mov	esi, CST_TK_EOF
	mov	edx, [r12 + Tok.span.start]
	xor	ecx, ecx
	call	__cst_leaf
	return
endp

; __cst_missing(p, ) -- a zero-width `CST_MISSING` leaf. §8.6: "A required
; token that is absent becomes a zero-width MISSING node." It consumes no
; source and no trivia, so it can be inserted anywhere without disturbing the
; round trip.
proc __cst_missing, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_MISSING
	xor	edx, edx
	xor	ecx, ecx
	call	cst_emit_token
	return
endp

; ---- diagnostics -----------------------------------------------------------

; __cst_note(p, code) -- account for a diagnostic this parse produced. Separate
; from `__cst_diag` because `EXS-E0220` is raised by `lex_kw_as_ident`, in the
; lexer, which knows nothing about this parser's counters.
proc __cst_note, p, code
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	inc	dword [rbx + CstParser.ndiag]
	cmp	dword [rbx + CstParser.firstcode], 0
	jne	.have
	mov	eax, [code]
	mov	[rbx + CstParser.firstcode], eax
  .have:
	return
endp

; __cst_diag(p, code, off, len) -> rax = the `Diag`, for a fix payload to be
; attached to. FOR IMMEDIATE USE ONLY -- the next diagnostic may grow the
; vector out from under it (lexer/source.inc's `lex_diag` says the same of its
; own return value).
proc __cst_diag, p, code, off, len
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.lx]
	mov	esi, [code]
	mov	rdx, [off]
	mov	rcx, [len]
	call	lex_diag
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, [code]
	call	__cst_note
	mov	rax, r12
	return
endp

; __cst_eofcode(p) -> eax = which end-of-input code applies here. §8.6:
; "EXS-E0202 for a bracket, block, or `<…>` still open at end of input;
; EXS-E0203 for end of input inside any other construct." The bracket depth
; `__cst_bump` maintains is the entire decision -- there is no second notion of
; "open" anywhere in this file.
proc __cst_eofcode, p
	locals
	endl
	mov	rax, [p]
	mov	eax, 203
	mov	rcx, [p]
	cmp	dword [rcx + CstParser.depth], 0
	je	.done
	mov	eax, 202
  .done:
	return
endp

; __cst_err(p, code) -> rax = the `Diag`, or 0 if this token already carries
; one. `code` 0 means "whatever unexpected-token code fits here": `EXS-E0201`
; at a real token, and `__cst_eofcode`'s answer at end of input.
;
; ONE DIAGNOSTIC PER TOKEN. A stray token inside a nested expression is seen by
; every enclosing production on the way out; without this suppression each one
; reports it, and the first error in a file buries the rest. §16's kill
; criterion for this stage is diagnostics quality.
proc __cst_err, p, code
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rax, [rbx + CstParser.ti]
	mov	ecx, [rbx + CstParser.lasterr]
	cmp	eax, ecx
	je	.suppress
	mov	[rbx + CstParser.lasterr], eax

	mov	rdi, rbx
	xor	esi, esi
	call	__cst_tokp
	mov	r12, rax
	mov	r13d, [code]
	test	r13d, r13d
	jnz	.have_code
	mov	r13d, 201
	cmp	dword [r12 + Tok.kind], TOK_EOF
	jne	.have_code
	mov	rdi, rbx
	call	__cst_eofcode
	mov	r13d, eax
  .have_code:
	mov	rdi, rbx
	mov	esi, r13d
	mov	edx, [r12 + Tok.span.start]
	mov	ecx, [r12 + Tok.span.len]
	call	__cst_diag
	; `EXS-E0202` is by definition "something is still open" -- it IS
	; `__cst_eofcode`'s only reason for choosing 202 over 203 -- so the
	; opener span is available exactly when this code is the one raised.
	; `EXS-E0203` gets none: `depth` is 0 there, and a related span
	; pointing at nothing is what `__cst_open_top` refuses to invent.
	cmp	r13d, 202
	jne	.ret
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, rax
	call	__cst_relate
	mov	rax, r13
  .ret:
	return
  .suppress:
	xor	eax, eax
	return
endp

; __cst_note_at(dg, nid, apt, aln) -- attach one of diag/notes.inc's
; `DIAG_NOTE_*` to a diagnostic, tolerating the 0 that `__cst_err` returns
; when this token already carries one.
;
; WHEN THE PARSER SHOULD SAY SOMETHING AND WHEN IT SHOULD NOT. `__diag_advise`
; (diag/render.inc) derives a note from the code, the span's own bytes and the
; attached edit, and an explicit note set here OVERRIDES that derivation
; entirely. That makes a blanket "the parser always knows best" wrong, and
; measurement says so: on `if a > 10 { return a; }` the parser's own knowledge
; is "a `;` was expected" and the derivation's is "`if` is not an Exsecutor
; keyword; §8.4 spells this `si`" -- the second is the sentence the author
; needs, and setting the first here would delete it. So a note is set at a
; construction site only where the parser knows something no reading of the
; bytes can recover, and the "expected `;`" family is deliberately left to the
; derivation. `docs/design/diagnostics-review.md` c14 is that measurement.
proc __cst_note_at, dg, nid, apt, aln
	uses	rbx
	locals
	endl
	mov	rbx, [dg]
	test	rbx, rbx
	jz	.none
	mov	rdi, rbx
	mov	esi, [nid]
	mov	rdx, [apt]
	mov	rcx, [aln]
	call	diag_note_set
  .none:
	return
endp

; __cst_relate(p, dg) -- point the diagnostic at the innermost delimiter that
; is still open, so the renderer prints a second gutter block reading "opened
; here" (diag/render.inc, `DIAG_F_REL`).
;
; This is the half of `docs/design/diagnostics-review.md` D6 that the diag
; tree could not do: `diag_rel_set` has existed since the record grew the
; field, and nothing called it, because the opener's position is knowledge
; only a parser holds. c02, c13 and c17 are the three cases the review names.
;
; WHAT IT NAMES IS THE STILL-OPEN DELIMITER, NOT THE ONE A HUMAN WOULD GUESS.
; On the review's c02 -- a file whose only `}` is consumed by the inner `si`
; block -- rustc points at the inner `{` on the strength of an INDENTATION
; heuristic. This points at the outer `{`, which is the one the parser is
; genuinely still inside at end of input. Both are useful; only one of them is
; something this file knows rather than guesses.
proc __cst_relate, p, dg
	uses	rbx, r12
	locals
	endl
	mov	r12, [dg]
	test	r12, r12
	jz	.none
	mov	rbx, [p]
	mov	rdi, rbx
	call	__cst_open_top
	test	rax, rax
	jz	.none
	mov	rcx, [rbx + CstParser.lx]
	mov	esi, [rcx + Lexer.file_id]
	mov	edx, [rax]
	mov	ecx, [rax + 4]
	mov	rdi, r12
	call	diag_rel_set
  .none:
	return
endp

; __cst_fix_insert(p, dg, txt, tlen) -- attach "insert this text here" to a
; diagnostic, at the end of the last token consumed rather than at the start of
; the offending one. That is the difference between `redde "x"[;]` and
; `redde "x"\n\n[;]}` when trivia sits between them, and it is what §8.6
; decision 1 means by "the insertion as its fix payload".
proc __cst_fix_insert, p, dg, txt, tlen
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [p]
	mov	r12, [dg]
	test	r12, r12
	jz	.none
	mov	r13, [txt]
	mov	r14, [tlen]
	mov	rdi, r12
	call	diag_fix_ptr
	mov	rdi, rax
	mov	rcx, [rbx + CstParser.lx]
	mov	esi, [rcx + Lexer.file_id]
	mov	rdx, [rbx + CstParser.pos]
	mov	rcx, r13
	mov	r8,  r14
	call	diag_fix_insert
  .none:
	return
endp

; ---- expecting a token -----------------------------------------------------
; Each returns eax = 1 if the token was there and was consumed, 0 if it was
; absent -- in which case a diagnostic has been recorded and a zero-width
; `CST_MISSING` leaf has been added, so the tree stays shaped and the caller
; carries on parsing rather than unwinding.

; __cst_xpun(p, id)
;
; A MISSING CLOSER IS NOT AN ORDINARY MISSING TOKEN, and this is where the two
; part company. When `id` is `)`, `]`, `}` or `>` the parser holds two facts
; the record could not previously carry: WHICH delimiter is unclosed (the
; note) and WHERE it was opened (the related span). Both are attached here,
; from `__cst_bump`'s open-delimiter stack.
;
; `DIAG_NOTE_UNCLOSED` is set EXPLICITLY even though `__diag_advise` derives
; the same id from the insertion's own byte -- because the derivation gets
; there only after an English-keyword lookup, a comparison-word check and a
; near-miss keyword scan have all declined, and any of those can win on the
; token that happens to sit where the bracket should be. A bracket left open
; is not improved by a guess about the token that revealed it. This is the
; one place `__cst_note_at`'s header argues FOR overriding the derivation.
proc __cst_xpun, p, id
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	esi, [id]
	call	__cst_pun
	test	eax, eax
	jz	.absent
	mov	rdi, rbx
	call	__cst_bump
	mov	eax, 1
	return
  .absent:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	r12, rax
	mov	edi, [id]
	call	__cst_pun_text
	mov	r14, rax
	mov	r13, rdx
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	mov	rcx, r13
	call	__cst_fix_insert

	mov	eax, [id]
	cmp	eax, PUN_RPAREN
	je	.closer
	cmp	eax, PUN_RBRACKET
	je	.closer
	cmp	eax, PUN_RBRACE
	je	.closer
	cmp	eax, PUN_GT
	jne	.plain
	; AT END OF INPUT, SAY SO INSTEAD. `__diag_advise` answers
	; `DIAG_NOTE_EOF_OPEN` -- "the file ends inside a construct opened
	; earlier; it wants }" -- which is strictly more specific than
	; "a delimiter opened earlier is still open", and overriding it here
	; would have thrown that away on exactly the two cases (c02, c13) the
	; related span exists for. The related span is attached either way:
	; it is the half no derivation can reach.
  .closer:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.rel_only
	mov	rdi, r12
	mov	esi, DIAG_NOTE_UNCLOSED
	mov	rdx, r14
	mov	rcx, r13
	call	__cst_note_at
  .rel_only:
	mov	rdi, rbx
	mov	rsi, r12
	call	__cst_relate
  .plain:
	mov	rdi, rbx
	call	__cst_missing

	; A MISMATCHED CLOSER STANDS IN FOR THE ONE THAT IS MISSING. `a[0)`
	; reported "expected `]`" and then left the `)` for the enclosing
	; statement to report a second time
	; (`docs/design/diagnostics-review.md` c09: "2 diagnostics for 1
	; typo"). The `)` closes nothing that is open, so nothing else wants
	; it; taking it here is what clang does and costs one diagnostic
	; instead of two. It goes into a `CST_ERROR` node rather than passing
	; as the closer it is not -- §8.6, "skipped tokens go into an ERROR
	; node" -- so the tree still round-trips and no consumer above can
	; mistake a `)` for a `]`.
	;
	; Only a bracket production may do this, and only for a bracket:
	; `__cst_stray_closer` never offers `}`, and a block expecting `}`
	; never absorbs, so §8.6's rule about `}` is untouched in both
	; directions.
	mov	eax, [id]
	cmp	eax, PUN_RPAREN
	je	.absorb_try
	cmp	eax, PUN_RBRACKET
	je	.absorb_try
	cmp	eax, PUN_GT
	jne	.gone
  .absorb_try:
	mov	rdi, rbx
	call	__cst_stray_closer
	test	eax, eax
	jz	.gone
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ERROR
	mov	rdx, r12
	call	cst_close
  .gone:
	xor	eax, eax
	return
endp

; __cst_xkw(p, id)
proc __cst_xkw, p, id
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	esi, [id]
	call	__cst_kw
	test	eax, eax
	jz	.absent
	mov	rdi, rbx
	call	__cst_bump
	mov	eax, 1
	return
  .absent:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	r12, rax
	mov	edi, [id]
	call	kw_text_of
	mov	r13, rdx
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rax
	mov	rcx, r13
	call	__cst_fix_insert
	mov	rdi, rbx
	call	__cst_missing
	xor	eax, eax
	return
endp

; __cst_xtok(p, kind) -- a token class with no fixed spelling: `STRING`,
; `INT`. No fix payload, because there is no text to insert that would be
; right (§8.3's fixes are for edits that are mechanically derivable, and
; "some string literal" is not one).
proc __cst_xtok, p, kind
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, [kind]
	jne	.absent
	mov	rdi, rbx
	call	__cst_bump
	mov	eax, 1
	return
  .absent:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
	xor	eax, eax
	return
endp

; __cst_xident(p) -- IDENT position. A reserved word here is `EXS-E0220`, and
; it is raised by `lex_kw_as_ident` -- the lexer's routine, with the lexer's
; machine-applicable fix -- exactly as lexer/lex.inc's header asks. The word is
; then CONSUMED as if it were the name: the author's intent is not in doubt,
; and dropping it would cascade one misparse into every construct after it.
proc __cst_xident, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_IDENT
	je	.plain
	cmp	eax, TOK_KEYWORD
	je	.reserved

	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
	xor	eax, eax
	return
  .plain:
	mov	rdi, rbx
	call	__cst_bump
	mov	eax, 1
	return
  .reserved:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_tokp
	mov	r12, rax
	mov	rax, [rbx + CstParser.ti]
	mov	[rbx + CstParser.lasterr], eax
	mov	rdi, [rbx + CstParser.lx]
	mov	esi, [r12 + Tok.span.start]
	mov	edx, [r12 + Tok.span.len]
	mov	ecx, [r12 + Tok.aux]
	call	lex_kw_as_ident
	mov	rdi, rbx
	mov	esi, 220
	call	__cst_note
	mov	rdi, rbx
	call	__cst_bump
	mov	eax, 1
	return
endp

; ---- synchronisation (§8.6, "Recovery") ------------------------------------
; The sets, as data. Pairs of (TOK_* kind, aux), `aux` -1 meaning "any";
; terminated by kind -1. Every set additionally admits `}` and end of input,
; which is checked before the table is consulted -- see `__cst_insync`.
cst_sync_module:
	dd	TOK_ANNOT,   -1
	dd	TOK_KEYWORD, KW_PUBLICA
	dd	TOK_KEYWORD, KW_FUNCTIO
	dd	TOK_KEYWORD, KW_STRUCTURA
	dd	TOK_KEYWORD, KW_TYPUS
	dd	TOK_KEYWORD, KW_INTERFACIES
	dd	TOK_KEYWORD, KW_POTESTAS
	dd	TOK_KEYWORD, KW_EXTERNUS
	dd	TOK_KEYWORD, KW_FIRMA
	dd	TOK_KEYWORD, KW_MUTABILIS
	dd	-1, 0
cst_sync_stmt:
	dd	TOK_PUNCT,   PUN_SEMI
	dd	TOK_PUNCT,   PUN_LBRACE
	dd	TOK_KEYWORD, KW_FIRMA
	dd	TOK_KEYWORD, KW_MUTABILIS
	dd	TOK_KEYWORD, KW_SUB
	dd	TOK_KEYWORD, KW_REDDE
	dd	TOK_KEYWORD, KW_RUMPE
	dd	TOK_KEYWORD, KW_PERGE
	dd	TOK_KEYWORD, KW_SI
	dd	TOK_KEYWORD, KW_DUM
	dd	TOK_KEYWORD, KW_PER
	dd	TOK_KEYWORD, KW_QUISQUE
	dd	TOK_KEYWORD, KW_DISCERNE
	dd	-1, 0
cst_sync_list:
	dd	TOK_PUNCT,   PUN_COMMA
	dd	-1, 0
cst_sync_member:
	dd	TOK_KEYWORD, KW_FUNCTIO
	dd	TOK_KEYWORD, KW_PUBLICA
	dd	-1, 0
cst_sync_casus:
	dd	TOK_KEYWORD, KW_CASUS
	dd	TOK_KEYWORD, KW_ALITER
	dd	-1, 0
cst_sync_tab:
	dq	cst_sync_module
	dq	cst_sync_stmt
	dq	cst_sync_list
	dq	cst_sync_member
	dq	cst_sync_casus

; __cst_insync(p, setid, closer) -> eax = 1 if the cursor is at a token this
; set stops on. `closer` is the caller's own closing bracket for
; `CST_SYNC_LIST` (`)`, `]` or `>`), 0 elsewhere.
proc __cst_insync, p, setid, closer
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_tokp
	mov	r12, rax
	cmp	dword [r12 + Tok.kind], TOK_EOF
	je	.yes
	cmp	dword [r12 + Tok.kind], TOK_PUNCT
	jne	.not_punct

	; "No production consumes a `}` it did not open" (§8.6): recovery
	; always stops at one, in every set, so the body that opened it gets it.
	cmp	dword [r12 + Tok.aux], PUN_RBRACE
	je	.yes
	mov	ecx, [closer]
	test	ecx, ecx
	jz	.not_punct
	cmp	[r12 + Tok.aux], ecx
	je	.yes
  .not_punct:
	; the `IDENT :` peek -- §8.6's own two-token peek for a struct,
	; `interfacies` or `externus` body, and the reason those bodies can be
	; separator-free and still recoverable
	cmp	dword [setid], CST_SYNC_MEMBER
	jne	.table
	cmp	dword [r12 + Tok.kind], TOK_IDENT
	jne	.table
	mov	rdi, rbx
	mov	esi, 1
	mov	edx, TOK_PUNCT
	mov	ecx, PUN_COLON
	call	__cst_is
	test	eax, eax
	jnz	.yes
  .table:
	mov	rcx, [setid]
	rassert	rcx ge 1
	rassert	rcx le 5
	dec	rcx
	lea	rax, [cst_sync_tab]
	mov	r13, [rax + rcx*8]
  .probe:
	mov	ecx, [r13]
	cmp	ecx, -1
	je	.no
	cmp	[r12 + Tok.kind], ecx
	jne	.next
	mov	ecx, [r13 + 4]
	cmp	ecx, -1
	je	.yes
	cmp	[r12 + Tok.aux], ecx
	je	.yes
  .next:
	add	r13, 8
	jmp	.probe
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __cst_error_skip(p, setid, closer) -- report the current token and put it,
; plus everything up to the next synchronisation point, into a `CST_ERROR`
; node. §8.6: "skipped tokens go into an ERROR node".
;
; ALWAYS CONSUMES AT LEAST ONE TOKEN. The caller must therefore not call it at
; end of input or at its own closing bracket -- every caller checks -- and in
; exchange every loop in this file is guaranteed to terminate. A recovery that
; can skip nothing is how an error-tolerant parser spins forever.
proc __cst_error_skip, p, setid, closer
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err

	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax

	mov	rdi, rbx
	call	__cst_bump
  .more:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.done
	mov	rdi, rbx
	mov	esi, [setid]
	mov	rdx, [closer]
	call	__cst_insync
	test	eax, eax
	jnz	.done
	mov	rdi, rbx
	call	__cst_bump
	jmp	.more
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ERROR
	mov	rdx, r12
	call	cst_close
	return
endp

; ---- types (§8.6, "Types") -------------------------------------------------

cst_lit_lt	db 'lt'
CST_LIT_LT_LEN = $ - cst_lit_lt

; __cst_bitwidth(p, n) -> eax = the width of a `BitType` spelled by the token
; `n` ahead, 0 if that token is not of the form `u` followed by digits.
;
; §8.6: "In type position an identifier of the form `u` followed by digits is
; a `BitType`, admitted only for widths 1-7 and 8, 16, 24, 32, 40, 48, 56, 64.
; This is what makes §5.2 rule 2 literally true: `u12` does not parse."
; Admissibility is `__cst_width_ok`; this routine only reads the shape, so
; that `u12` is recognised as a BitType and REJECTED rather than silently
; reparsed as an ordinary path -- which is the difference between "does not
; parse" and "means something else".
;
; More than three digits answers 999, which no admitted width equals: the
; point of the number is the table lookup, and `u4294967296` needs no more
; precision than "not one of them".
proc __cst_bitwidth, p, n
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	rsi, [n]
	call	__cst_tokp
	cmp	dword [rax + Tok.kind], TOK_IDENT
	jne	.no
	mov	r12d, [rax + Tok.span.len]
	cmp	r12d, 2
	jb	.no
	mov	rcx, [rbx + CstParser.lx]
	mov	rcx, [rcx + Lexer.src_ptr]
	mov	edx, [rax + Tok.span.start]
	add	rcx, rdx
	cmp	byte [rcx], 'u'
	jne	.no
	cmp	r12d, 4
	ja	.wide
	xor	r13d, r13d		; the value
	mov	r14d, 1
  .digit:
	cmp	r14d, r12d
	jae	.done
	movzx	eax, byte [rcx + r14]
	cmp	al, '0'
	jb	.no
	cmp	al, '9'
	ja	.no
	sub	eax, '0'
	imul	r13d, r13d, 10
	add	r13d, eax
	inc	r14d
	jmp	.digit
  .done:
	mov	eax, r13d
	test	eax, eax
	jz	.no			; `u0` is not a width; treat as a path
	return
  .wide:
	; too long to be an admitted width, but still all digits or not
	mov	r14d, 1
  .wdigit:
	cmp	r14d, r12d
	jae	.wide_yes
	movzx	eax, byte [rcx + r14]
	cmp	al, '0'
	jb	.no
	cmp	al, '9'
	ja	.no
	inc	r14d
	jmp	.wdigit
  .wide_yes:
	mov	eax, 999
	return
  .no:
	xor	eax, eax
	return
endp

; __cst_width_ok(w) -> eax = 1 if `w` is one of §5.2's admitted bit widths.
; One table lookup on the token text, as §8.6 promises: 1-7 sub-byte, then
; whole bytes to 64.
proc __cst_width_ok, w
	locals
	endl
	mov	ecx, [w]
	cmp	ecx, 8
	jbe	.small
	cmp	ecx, 64
	ja	.no
	test	ecx, 7
	jnz	.no
	mov	eax, 1
	return
  .small:
	xor	eax, eax
	test	ecx, ecx
	jz	.no
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __cst_type_starts(p) -> eax = 1 if a `Type` can begin at the cursor.
proc __cst_type_starts, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_IDENT
	je	.yes
	cmp	eax, TOK_PUNCT
	je	.punct
	cmp	eax, TOK_KEYWORD
	je	.kw
	xor	eax, eax
	return
  .punct:
	mov	rdi, rbx
	mov	esi, PUN_AMP
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_STAR
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	xor	eax, eax
	return
  .kw:
	mov	rdi, rbx
	mov	esi, KW_DYN
	call	__cst_kw
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_kw
	test	eax, eax
	jnz	.yes
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __cst_path(p) -- `Path ::= IDENT ('.' IDENT)*`. Callers guard the first
; token: this routine's `__cst_xident` would consume a reserved word as the
; name (`EXS-E0220`), which is right where the grammar says IDENT and wrong
; where a keyword belongs to the enclosing production.
proc __cst_path, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_xident
  .more:
	mov	rdi, rbx
	mov	esi, PUN_DOT
	call	__cst_pun
	test	eax, eax
	jz	.done
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_xident
	jmp	.more
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_PATH
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_type_arg(p) -- `TypeArg ::= Type | INT`. §8.6's peek: "`INT` -> value
; argument (`acies<f32, 1024>`); anything else is a `Type`; a named constant
; parses as `Path` and is resolved semantically."
proc __cst_type_arg, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_NUMBER
	jne	.type
	mov	rdi, rbx
	call	__cst_bump
	jmp	.done
  .type:
	mov	rdi, rbx
	call	__cst_type
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_TYPE_ARG
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_generic_args(p) -- `'<' TypeArg (',' TypeArg)* '>'`.
;
; §8.6 decision 6 is what makes this unambiguous: "`<` after a path segment
; always opens generic arguments, and the lexer never forms `<<` `>>` `<=`
; `>=`". So `acies<acies<f32, 4>, 4>` closes with two separate `>` tokens and
; needs no lexer hack, and a `<` that never finds its `>` is the comparison
; §8.6 says to diagnose: "`a < b` is EXS-E0201 with the message "`<` opens
; generic arguments; comparison is `lt`" and the edit attached".
;
; THE EDIT IS ATTACHED HERE and the message is not: §8.3 makes the code
; permanent and the text not, and diag/codes.inc owns the English. A
; per-site message would be a second, un-keyed message table.
proc __cst_generic_args, p
	uses	rbx, r12, r13, r14
	locals
		slot	ltoff, dq
		slot	ltlen, dq
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_tokp
	mov	ecx, [rax + Tok.span.start]
	mov	[ltoff], rcx
	mov	ecx, [rax + Tok.span.len]
	mov	[ltlen], rcx
	mov	rdi, rbx
	mov	esi, PUN_LT
	call	__cst_xpun
  .item:
	mov	rdi, rbx
	mov	esi, PUN_GT
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_NUMBER
	je	.arg
	mov	rdi, rbx
	call	__cst_type_starts
	test	eax, eax
	jnz	.arg
	mov	rdi, rbx
	mov	esi, CST_SYNC_LIST
	mov	edx, PUN_GT
	call	__cst_error_skip
	jmp	.sep
  .arg:
	mov	rdi, rbx
	call	__cst_type_arg
  .sep:
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_pun
	test	eax, eax
	jz	.close
	mov	rdi, rbx
	call	__cst_bump
	jmp	.item
  .close:
	mov	rdi, rbx
	mov	esi, PUN_GT
	call	__cst_pun
	test	eax, eax
	jz	.unclosed
	mov	rdi, rbx
	call	__cst_bump
	jmp	.done
  .unclosed:
	; No `>`: this was almost certainly a comparison. Reported against the
	; `<` itself rather than against whatever token happens to be here,
	; because that is the token the fix replaces.
	mov	rdi, rbx
	mov	esi, 201
	mov	rdx, [ltoff]
	mov	rcx, [ltlen]
	call	__cst_diag
	mov	r13, rax
	mov	rdi, r13
	call	diag_fix_ptr
	mov	rdi, rax
	mov	rcx, [rbx + CstParser.lx]
	mov	esi, [rcx + Lexer.file_id]
	mov	rdx, [ltoff]
	mov	rcx, [ltlen]
	lea	r8,  [cst_lit_lt]
	mov	r9,  CST_LIT_LT_LEN
	call	diag_fix_replace
	mov	rdi, rbx
	call	__cst_missing
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_GENERIC_ARGS
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_at_type_row(p) -> eax = 1 if the cursor is at `poscit {`.
;
; THE ONE TWO-TOKEN PEEK THAT DECIDES WHERE A ROW BELONGS (§8.6, "Where
; `poscit` attaches"): "A type consumes `poscit` ONLY when the next token is
; `{` -- so a declaration whose result type is a function type still takes a
; bare row without parentheses, and a body is never mistaken for a row."
proc __cst_at_type_row, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	esi, KW_POSCIT
	call	__cst_kw
	test	eax, eax
	jz	.no
	mov	rdi, rbx
	mov	esi, 1
	mov	edx, TOK_PUNCT
	mov	ecx, PUN_LBRACE
	call	__cst_is
	return
  .no:
	xor	eax, eax
	return
endp

; __cst_row_item(p) -- `RowItem ::= Path | 'sicut' IDENT`, the same in a bare
; row and a braced one (§8.6).
proc __cst_row_item, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_SICUT
	call	__cst_kw
	test	eax, eax
	jz	.path
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_xident
	jmp	.done
  .path:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_IDENT
	jne	.bad
	mov	rdi, rbx
	call	__cst_path
	jmp	.done
  .bad:
	; `poscit {}` is an explicit empty row; a bare `poscit` with no item is
	; EXS-E0201 (§8.6, "Where `poscit` attaches").
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ROW_ITEM
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_decl_row(p) -- the BARE row: `'poscit' RowItem (',' RowItem)*`, which
; §8.6's table says "ends at first token that is not `,`".
proc __cst_decl_row, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_POSCIT
	call	__cst_xkw
	mov	rdi, rbx
	call	__cst_row_item
  .more:
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_pun
	test	eax, eax
	jz	.done
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_row_item
	jmp	.more
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_DECL_ROW
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_type_row(p) -- the BRACED row: `'poscit' '{' [RowItem (',' RowItem)*]
; '}'`. §8.6: braced "because these types sit in comma-separated lists where a
; bare row is ambiguous"; `poscit {}` is an explicit empty row.
proc __cst_type_row, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_POSCIT
	call	__cst_xkw
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_xpun
  .item:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	mov	rdi, rbx
	call	__cst_row_item
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_pun
	test	eax, eax
	jz	.close
	mov	rdi, rbx
	call	__cst_bump
	jmp	.item
  .close:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_TYPE_ROW
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_core_type(p) -> eax = 1 if this was a `BitType` of width below 8, which
; is the one fact `__cst_type`'s suffix loop needs: §5.2 rule 3 makes a
; byte-order suffix on a sub-byte field `EXS-E0201`, and §14 entry 22 is
; exactly that file.
proc __cst_core_type, p
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	esi, KW_DYN
	call	__cst_kw
	test	eax, eax
	jnz	.dyn
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_kw
	test	eax, eax
	jnz	.fn
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_pun
	test	eax, eax
	jnz	.paren
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_IDENT
	je	.ident

	; A reserved word is NOT consumed here, deliberately. `EXS-E0220` and
	; "consume it as the name" is right where the grammar writes IDENT and
	; nothing else can appear (`__cst_xident`); in a type position a
	; reserved word is far more often the NEXT construct -- `poscit`, `in`,
	; `sicut` -- and eating it turns one error into a cascade.
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
	xor	eax, eax
	return

  .ident:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_bitwidth
	test	eax, eax
	jnz	.bits
	mov	rdi, rbx
	call	__cst_path
	mov	rdi, rbx
	mov	esi, PUN_LT
	call	__cst_pun
	test	eax, eax
	jz	.plain_done
	mov	rdi, rbx
	call	__cst_generic_args
  .plain_done:
	xor	eax, eax
	return

  .bits:
	mov	r13d, eax			; the width
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	edi, r13d
	call	__cst_width_ok
	mov	r14d, eax
	test	eax, eax
	jnz	.bits_ok
	; §8.6: "`u12` *does not parse*, EXS-E0201." The token is still put in
	; the tree -- losing it would cost the round trip, and the diagnostic
	; is the deliverable, not the shape.
	mov	rdi, rbx
	mov	esi, 201
	call	__cst_err
  .bits_ok:
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_BIT_TYPE
	mov	rdx, r12
	call	cst_close
	xor	eax, eax
	test	r14d, r14d
	jz	.bits_done
	cmp	r13d, 8
	jae	.bits_done
	mov	eax, 1
  .bits_done:
	return

  .paren:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_type
	mov	rdi, rbx
	mov	esi, PUN_RPAREN
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_PAREN_TYPE
	mov	rdx, r12
	call	cst_close
	xor	eax, eax
	return

  .dyn:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_IDENT
	jne	.dyn_nopath
	mov	rdi, rbx
	call	__cst_path
	mov	rdi, rbx
	mov	esi, PUN_LT
	call	__cst_pun
	test	eax, eax
	jz	.dyn_row
	mov	rdi, rbx
	call	__cst_generic_args
	jmp	.dyn_row
  .dyn_nopath:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
  .dyn_row:
	mov	rdi, rbx
	call	__cst_at_type_row
	test	eax, eax
	jz	.dyn_done
	mov	rdi, rbx
	call	__cst_type_row
  .dyn_done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_DYN_TYPE
	mov	rdx, r12
	call	cst_close
	xor	eax, eax
	return

  .fn:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_xpun
  .fn_param:
	mov	rdi, rbx
	mov	esi, PUN_RPAREN
	call	__cst_pun
	test	eax, eax
	jnz	.fn_close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.fn_close
	mov	rdi, rbx
	call	__cst_type_starts
	test	eax, eax
	jz	.fn_bad
	mov	rdi, rbx
	call	__cst_type
	jmp	.fn_sep
  .fn_bad:
	mov	rdi, rbx
	mov	esi, CST_SYNC_LIST
	mov	edx, PUN_RPAREN
	call	__cst_error_skip
  .fn_sep:
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_pun
	test	eax, eax
	jz	.fn_close
	mov	rdi, rbx
	call	__cst_bump
	jmp	.fn_param
  .fn_close:
	mov	rdi, rbx
	mov	esi, PUN_RPAREN
	call	__cst_xpun
	mov	rdi, rbx
	mov	esi, PUN_ARROW
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_type
	mov	rdi, rbx
	call	__cst_at_type_row
	test	eax, eax
	jz	.fn_done
	mov	rdi, rbx
	call	__cst_type_row
  .fn_done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_FN_TYPE
	mov	rdx, r12
	call	cst_close
	xor	eax, eax
	return
endp

; __cst_type(p) -- `Type ::= ('&' | '*')* CoreType (':' IDENT | 'apud' IDENT)*`.
;
; A `CST_TYPE` node is emitted even when it wraps a single `CST_PATH`, unlike
; the expression levels (cst/kinds.inc, departure 1). The asymmetry is
; deliberate: every consumer above this one asks "is there a type here", and a
; node whose presence depends on whether the type happened to have a sigil
; would make that question a walk instead of a look.
proc __cst_type, p
	uses	rbx, r12, r13, r14
	locals
		slot	coff, dq
		slot	clen, dq
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
  .sigil:
	mov	rdi, rbx
	mov	esi, PUN_AMP
	call	__cst_pun
	test	eax, eax
	jnz	.take_amp
	mov	rdi, rbx
	mov	esi, PUN_STAR
	call	__cst_pun
	test	eax, eax
	jz	.core
	mov	rdi, rbx
	call	__cst_bump
	jmp	.sigil
  .take_amp:
	mov	rdi, rbx
	call	__cst_bump
	; `&mutabilis T`, a mutable borrow (ADR 0016 decision 1). Accepted only
	; after `&`: there is no `*mutabilis T`, because `*T` already admits a
	; write and already costs `Crudum`.
	;
	; NO NEW PEEK, which is why the marker is here and not before the
	; parameter's name (spec section 8.6 fixes the peek set). This arm has
	; already committed to a type, and `mutabilis` is a tier-1 reserved word
	; (spec section 8.4), so it can never begin a type name -- one token,
	; decided by one compare, with nothing to back out of.
	mov	rdi, rbx
	mov	esi, KW_MUTABILIS
	call	__cst_kw
	test	eax, eax
	jz	.sigil
	mov	rdi, rbx
	call	__cst_bump
	jmp	.sigil
  .core:
	mov	rdi, rbx
	call	__cst_core_type
	mov	r13d, eax			; 1 = sub-byte BitType
  .suffix:
	mov	rdi, rbx
	mov	esi, PUN_COLON
	call	__cst_pun
	test	eax, eax
	jnz	.order
	mov	rdi, rbx
	mov	esi, KW_APUD
	call	__cst_kw
	test	eax, eax
	jnz	.place
	jmp	.done

	; §5.2 rule 3, via §8.6: "A byte-order suffix on a `BitType` of width
	; below 8 is likewise EXS-E0201." This is §14 entry 22 (`u4:maior`),
	; and it is the whole of what that fixture needs. The span covers the
	; suffix -- `:maior`, not just the `:` -- because that is the construct
	; that is not admissible, and §8.3 spans are what a fix and an editor
	; both key on.
  .order:
	test	r13d, r13d
	jz	.order_node
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_tokp
	mov	ecx, [rax + Tok.span.start]
	mov	[coff], rcx
	mov	ecx, [rax + Tok.span.len]
	mov	[clen], rcx
	mov	rdi, rbx
	mov	esi, 1
	call	__cst_tokp
	cmp	dword [rax + Tok.kind], TOK_IDENT
	jne	.order_report
	mov	ecx, [rax + Tok.span.start]
	add	ecx, [rax + Tok.span.len]
	sub	rcx, [coff]
	mov	[clen], rcx
  .order_report:
	mov	rdi, rbx
	mov	esi, 201
	mov	rdx, [coff]
	mov	rcx, [clen]
	call	__cst_diag
	xor	r13d, r13d		; report the suffix once, not per suffix
  .order_node:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r14, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ORDER_SUFFIX
	mov	rdx, r14
	call	cst_close
	jmp	.suffix
  .place:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r14, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_PLACE_SUFFIX
	mov	rdx, r14
	call	cst_close
	jmp	.suffix
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_TYPE
	mov	rdx, r12
	call	cst_close
	return
endp

; ---- expressions (§8.6, "Expressions") -------------------------------------
; Precedence climbing over §8.6's fixed table, one routine per level, one-token
; peek per step. A level that consumed no operator of its own cancels its mark
; and leaves its operand in place -- cst/kinds.inc, departure 1.

; __cst_expr_starts(p) -> eax = 1 if an expression can begin at the cursor.
; The guard that makes the statement loop terminate: a statement that is
; neither a keyword form nor the start of an expression is skipped, once,
; rather than parsed into a zero-token expression forever.
proc __cst_expr_starts, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_IDENT
	je	.yes
	cmp	eax, TOK_NUMBER
	je	.yes
	cmp	eax, TOK_STRING
	je	.yes
	cmp	eax, TOK_KEYWORD
	je	.kw
	cmp	eax, TOK_PUNCT
	je	.punct
	xor	eax, eax
	return
  .kw:
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_kw
	return
  .punct:
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_MINUS
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_AMP
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_STAR
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	; `[` in OPERAND position opens an array literal (spec §8.6). After an
	; operand the same token is the index suffix, and `__cst_postfix` reads
	; it there; the position decides, never the token -- the rule §8.6
	; already states for `&` and `*`.
	mov	rdi, rbx
	mov	esi, PUN_LBRACKET
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __cst_kw_leads(p) -> eax = 1 if the reserved word at the cursor can begin a
; statement or a module item. Such a word is never eaten as a NAME: giving it
; back lets the enclosing block parse the statement the author actually wrote,
; instead of turning one error into a cascade. Any other reserved word in
; identifier position is `EXS-E0220` and IS eaten -- see `__cst_primary`.
proc __cst_kw_leads, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	esi, CST_SYNC_MODULE
	xor	edx, edx
	call	__cst_insync
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, CST_SYNC_STMT
	xor	edx, edx
	call	__cst_insync
	test	eax, eax
	jnz	.yes
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __cst_lambda(p) -- `Lambda ::= 'functio' ParamList ['->' Type] Block`.
; §8.6 decision 2: parameter types are required and there is no `poscit` --
; "its row is inferred and appears in its *type*". A `poscit` here is
; EXS-E0201, and the row is kept in the tree inside a `CST_ERROR` so the file
; still round-trips.
proc __cst_lambda, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_xkw
	mov	rdi, rbx
	call	__cst_param_list
	mov	rdi, rbx
	mov	esi, PUN_ARROW
	call	__cst_pun
	test	eax, eax
	jz	.row
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_type
  .row:
	mov	rdi, rbx
	mov	esi, KW_POSCIT
	call	__cst_kw
	test	eax, eax
	jz	.body
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	mov	esi, 201
	call	__cst_err
	mov	rdi, rbx
	call	__cst_decl_row
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ERROR
	mov	rdx, r13
	call	cst_close
  .body:
	mov	rdi, rbx
	call	__cst_block
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_LAMBDA
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_primary(p) -> eax = 1 if what was parsed is a PATH SEGMENT, which is
; the one fact `__cst_postfix` needs: §8.6 restricts generic arguments and
; struct literals to "directly after a path segment (`IDENT`, `. IDENT`, or
; `<…>`)".
proc __cst_primary, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	mov	r13d, eax
	cmp	r13d, TOK_NUMBER
	je	.literal
	cmp	r13d, TOK_STRING
	je	.literal
	cmp	r13d, TOK_IDENT
	je	.name
	cmp	r13d, TOK_KEYWORD
	je	.kw
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_pun
	test	eax, eax
	jnz	.paren
	mov	rdi, rbx
	mov	esi, PUN_LBRACKET
	call	__cst_pun
	test	eax, eax
	jnz	.array
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
	xor	eax, eax
	return
  .array:
	; spec §8.6's array literal, in OPERAND position -- see
	; `__cst_array_lit`. It is not a path segment, so no generic arguments
	; and no struct literal may follow it.
	mov	rdi, rbx
	call	__cst_array_lit
	xor	eax, eax
	return
  .literal:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_LITERAL
	mov	rdx, r12
	call	cst_close
	xor	eax, eax
	return
  .name:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_NAME_EXPR
	mov	rdx, r12
	call	cst_close
	mov	eax, 1
	return
  .paren:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_expr_lit
	mov	rdi, rbx
	mov	esi, PUN_RPAREN
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_PAREN_EXPR
	mov	rdx, r12
	call	cst_close
	xor	eax, eax
	return
  .kw:
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_kw
	test	eax, eax
	jz	.kw_other
	; §8.6: "`functio` has three roles that never share a position: item
	; position is a declaration, type position a function type, expression
	; position a lambda."
	mov	rdi, rbx
	call	__cst_lambda
	xor	eax, eax
	return
  .kw_other:
	mov	rdi, rbx
	call	__cst_kw_leads
	test	eax, eax
	jnz	.kw_give_back
	; A reserved word in identifier position: EXS-E0220, raised by the
	; lexer's own routine with the lexer's own fix.
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_NAME_EXPR
	mov	rdx, r12
	call	cst_close
	mov	eax, 1
	return
	; A statement- or item-leading reserved word, in expression position.
	; The token is given back so the enclosing block can parse the
	; statement it leads -- but the record said nothing at all about why
	; (`docs/design/diagnostics-review.md`'s corpus: `EXS-E0201` at `per`
	; with a null note). What the parser knows and the bytes cannot say is
	; that this token is one of §8.4's thirty, in a position that wanted a
	; name: `__diag_advise` reaches its did-you-mean scan here and an EXACT
	; keyword match silences that scan by design, so the derivation is
	; structurally unable to produce this sentence.
  .kw_give_back:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	r12, rax
	mov	rdi, r12
	mov	esi, DIAG_NOTE_RESERVED
	xor	edx, edx
	xor	ecx, ecx
	call	__cst_note_at
	mov	rdi, rbx
	call	__cst_missing
	xor	eax, eax
	return
endp

; __cst_call_args(p) -- `'(' [Expr (',' Expr)*] ')'`, as a `CST_CALL_SUFFIX`.
proc __cst_call_args, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_xpun
  .arg:
	mov	rdi, rbx
	mov	esi, PUN_RPAREN
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	mov	rdi, rbx
	call	__cst_expr_starts
	test	eax, eax
	jz	.bad
	mov	rdi, rbx
	call	__cst_expr_lit
	jmp	.sep
  .bad:
	mov	rdi, rbx
	mov	esi, CST_SYNC_LIST
	mov	edx, PUN_RPAREN
	call	__cst_error_skip
  .sep:
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_pun
	test	eax, eax
	jz	.close
	mov	rdi, rbx
	call	__cst_bump
	jmp	.arg
  .close:
	mov	rdi, rbx
	mov	esi, PUN_RPAREN
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_CALL_SUFFIX
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_postfix(p) -- level 1: `.f` `(…)` `[…]` `?` `<…>` `{…}`,
; left-associative. The last two only after a path segment, and `{…}` only
; outside `ExprNS`.
proc __cst_postfix, p
	uses	rbx, r12, r13, r14
	locals
		slot	nsuf, dq
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	qword [nsuf], 0
	mov	rdi, rbx
	call	__cst_primary
	mov	r13d, eax			; 1 = the operand is a path segment
  .suffix:
	mov	rdi, rbx
	mov	esi, PUN_DOT
	call	__cst_pun
	test	eax, eax
	jnz	.field
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_pun
	test	eax, eax
	jnz	.call
	mov	rdi, rbx
	mov	esi, PUN_LBRACKET
	call	__cst_pun
	test	eax, eax
	jnz	.index
	mov	rdi, rbx
	mov	esi, PUN_QUESTION
	call	__cst_pun
	test	eax, eax
	jnz	.try
	test	r13d, r13d
	jz	.done
	mov	rdi, rbx
	mov	esi, PUN_LT
	call	__cst_pun
	test	eax, eax
	jnz	.generic
	; spec §8.6's `StructLit`, after a path segment only and never in
	; `ExprNS` -- the one place `CstParser.nolit` is read
	cmp	dword [rbx + CstParser.nolit], 0
	jne	.done
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.structlit
  .done:
	cmp	qword [nsuf], 0
	je	.cancel
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_POSTFIX_EXPR
	mov	rdx, r12
	call	cst_close
	return
  .cancel:
	mov	rdi, [rbx + CstParser.tree]
	mov	rsi, r12
	call	cst_cancel
	return
  .field:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r14, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_FIELD_SUFFIX
	mov	rdx, r14
	call	cst_close
	inc	qword [nsuf]
	mov	r13d, 1
	jmp	.suffix
  .call:
	mov	rdi, rbx
	call	__cst_call_args
	inc	qword [nsuf]
	xor	r13d, r13d
	jmp	.suffix
  .index:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r14, rax
	mov	rdi, rbx
	mov	esi, PUN_LBRACKET
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_expr_lit
	mov	rdi, rbx
	mov	esi, PUN_RBRACKET
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_INDEX_SUFFIX
	mov	rdx, r14
	call	cst_close
	inc	qword [nsuf]
	xor	r13d, r13d
	jmp	.suffix
  .try:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r14, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_TRY_SUFFIX
	mov	rdx, r14
	call	cst_close
	inc	qword [nsuf]
	xor	r13d, r13d
	jmp	.suffix
  .generic:
	mov	rdi, rbx
	call	__cst_generic_args
	inc	qword [nsuf]
	mov	r13d, 1
	jmp	.suffix
  .structlit:
	mov	rdi, rbx
	call	__cst_struct_lit
	inc	qword [nsuf]
	xor	r13d, r13d		; a literal is not a path segment
	jmp	.suffix
endp

; __cst_struct_lit(p) -- spec §8.6's `StructLit ::= '{' [FieldInit (','
; FieldInit)*] '}'`, `FieldInit ::= IDENT ':' Expr`, as a
; `CST_STRUCT_LIT_SUFFIX` over `CST_FIELD_INIT`s. Spec §8.6 cites
; tests/unit/cst_structlit.asm as the fixture that exercises it;
; docs/design/wire-codec.md D3.
;
; COMMAS ARE MANDATORY AND A TRAILING ONE IS `EXS-E0201`, at the `}` it runs
; into -- "no trailing comma, as in every other list here" (§8.6). Which
; fields are present, missing or doubled is the CHECKER's (`EXS-E0301`/
; `E0302`/`E0304`): the grammar admits any identifiers, as a call admits any
; argument count.
;
; The initialisers are `__cst_expr_lit`: inside the literal's own braces a
; nested literal is unambiguous even when the literal itself sits in a
; parenthesised `ExprNS` position.
proc __cst_struct_lit, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_xpun
  .field:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	cmp	eax, TOK_IDENT
	jne	.bad
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump		; the field's name
	mov	rdi, rbx
	mov	esi, PUN_COLON
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_expr_lit
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_FIELD_INIT
	mov	rdx, r13
	call	cst_close
	jmp	.sep
  .bad:
	mov	rdi, rbx
	mov	esi, CST_SYNC_LIST
	mov	edx, PUN_RBRACE
	call	__cst_error_skip
  .sep:
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_pun
	test	eax, eax
	jz	.close
	mov	rdi, rbx
	call	__cst_bump
	; `S { a: 1, }` -- the list ended on a comma
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_pun
	test	eax, eax
	jz	.field
	mov	rdi, rbx
	mov	esi, 201
	call	__cst_err
  .close:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_STRUCT_LIT_SUFFIX
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_array_lit(p) -- spec §8.6's array literal, both forms, entered with the
; cursor on the `[` that opens it:
;
;	ArrayLit    ::= '[' Expr (',' Expr)* ']'      -> `CST_ARRAY_LIT`
;	ArrayRepeat ::= '[' Expr ';' INT ']'          -> `CST_ARRAY_REPEAT`
;
; ONE TOKEN DECIDES BETWEEN THEM, and it is not the first: both open `[ Expr`,
; and the token after that first expression -- `,` or `]` for the list, `;` for
; the repeat -- selects the production. That is still LL(1) in §8.6's sense
; (the peek table is about deciding a production from tokens not yet consumed;
; here the parser has already consumed the shared prefix and is choosing a
; TAIL, exactly as `sub … ;` vs `sub … {` does), so no new peek is owed.
;
; POSITION, NOT TOKEN. This routine is reached from `__cst_primary` only, where
; an operand is expected; after an operand `[` is the index suffix and
; `__cst_postfix` takes it. `a[0]` is therefore still an index and `[a][0]` is
; a one-element literal indexed -- the rule §8.6 states for `&` and `*`
; ("position decides, never the token"), applied to a third sigil.
;
; THREE MALFORMED FORMS, all `EXS-E0201` and all left in the tree so the round
; trip stays byte-exact:
;   - `[]`: the empty literal. §6.3 makes an `acies` a flat buffer of a count
;     that is part of its type, and there is no type an empty one could have
;     that the element rule could ever settle -- the diagnostic is at the `]`.
;   - `[1, 2,]`: a trailing comma, at the `]` it runs into, exactly as a struct
;     literal's is ("no trailing comma, as in every other list here").
;   - `[e; n]` with `n` not an `INT`: the count is part of the TYPE (spec §5.4:
;     "the lane count is in the type, so it can never come from the host"), so
;     a non-literal there is refused at the token, which then goes into a
;     `CST_ERROR` by the ordinary list recovery.
;
; The elements are `__cst_expr_lit`: brackets re-admit the struct literal
; `ExprNS` excludes, which is the rule §8.6 already states for `[ ]`.
proc __cst_array_lit, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, PUN_LBRACKET
	call	__cst_xpun

	; `[]`
	mov	rdi, rbx
	mov	esi, PUN_RBRACKET
	call	__cst_pun
	test	eax, eax
	jz	.first
	mov	rdi, rbx
	mov	esi, 201
	call	__cst_err
	jmp	.close_list

  .first:
	mov	rdi, rbx
	call	__cst_expr_starts
	test	eax, eax
	jz	.bad
	mov	rdi, rbx
	call	__cst_expr_lit
	; `;` -- the repeat form
	mov	rdi, rbx
	mov	esi, PUN_SEMI
	call	__cst_pun
	test	eax, eax
	jnz	.repeat
	jmp	.sep

  .bad:
	mov	rdi, rbx
	mov	esi, CST_SYNC_LIST
	mov	edx, PUN_RBRACKET
	call	__cst_error_skip
  .sep:
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_pun
	test	eax, eax
	jz	.close_list
	mov	rdi, rbx
	call	__cst_bump
	; `[1, 2,]` -- the list ended on a comma
	mov	rdi, rbx
	mov	esi, PUN_RBRACKET
	call	__cst_pun
	test	eax, eax
	jz	.elem
	mov	rdi, rbx
	mov	esi, 201
	call	__cst_err
	jmp	.close_list
  .elem:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close_list
	mov	rdi, rbx
	call	__cst_expr_starts
	test	eax, eax
	jz	.bad
	mov	rdi, rbx
	call	__cst_expr_lit
	jmp	.sep

  .close_list:
	mov	rdi, rbx
	mov	esi, PUN_RBRACKET
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ARRAY_LIT
	mov	rdx, r12
	call	cst_close
	return

  .repeat:
	mov	rdi, rbx
	call	__cst_bump		; the `;`
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_NUMBER
	jne	.count_bad
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_LITERAL
	mov	rdx, r13
	call	cst_close
	jmp	.close_rep
  .count_bad:
	mov	rdi, rbx
	mov	esi, CST_SYNC_LIST
	mov	edx, PUN_RBRACKET
	call	__cst_error_skip
  .close_rep:
	mov	rdi, rbx
	mov	esi, PUN_RBRACKET
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ARRAY_REPEAT
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_unary(p) -- level 2: prefix `-` `&` `*`. §8.6: "`&` and `*` are prefix
; in operand position and binary or type sigils elsewhere; the position
; decides, never the token."
proc __cst_unary, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	esi, PUN_MINUS
	call	__cst_pun
	test	eax, eax
	jnz	.prefix
	mov	rdi, rbx
	mov	esi, PUN_AMP
	call	__cst_pun
	test	eax, eax
	jnz	.prefix
	mov	rdi, rbx
	mov	esi, PUN_STAR
	call	__cst_pun
	test	eax, eax
	jnz	.prefix
	mov	rdi, rbx
	call	__cst_postfix
	return
  .prefix:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_unary
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_UNARY_EXPR
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_cast(p) -- level 3: `Unary ('sicut' Type)*`. §8.6 decision 4: "After an
; operand `sicut` is a cast, inside a row a row item -- the positions are
; disjoint."
proc __cst_cast, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	xor	r13d, r13d
	mov	rdi, rbx
	call	__cst_unary
  .more:
	mov	rdi, rbx
	mov	esi, KW_SICUT
	call	__cst_kw
	test	eax, eax
	jz	.done
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_type
	mov	r13d, 1
	jmp	.more
  .done:
	test	r13d, r13d
	jz	.cancel
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_CAST_EXPR
	mov	rdx, r12
	call	cst_close
	return
  .cancel:
	mov	rdi, [rbx + CstParser.tree]
	mov	rsi, r12
	call	cst_cancel
	return
endp

; __cst_is_mulop(p) -> eax = 1 at a multiplicative operator.
;
; `*` and, since the float wave, `/` -- §5.4's float-only quotient, the
; token that used to be unproducible because a bare `/` was EXS-E0201.
; The CST node (CST_MUL_EXPR) stays flat and operator-agnostic on
; purpose: which operator each operand pair carries is a property of the
; TOKENS, preserved losslessly for ast/from_cst.inc to fold into
; AST_OP_* nodes; the float-only admissibility of `/` is the checker's
; question (EXS-E0305 there), not the CST's -- the tree is lossless and
; error-tolerant, so it must not refuse. Remainder and the `*%`/`*|`
; overflow forms stay `[OPEN]` in §8.6 and are not invented here.
proc __cst_is_mulop, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	esi, PUN_STAR
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_SLASH
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __cst_mul(p) -- level 4: `Cast (MulOp Cast)*`.
proc __cst_mul, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	xor	r13d, r13d
	mov	rdi, rbx
	call	__cst_cast
  .more:
	mov	rdi, rbx
	call	__cst_is_mulop
	test	eax, eax
	jz	.done
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_cast
	mov	r13d, 1
	jmp	.more
  .done:
	test	r13d, r13d
	jz	.cancel
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_MUL_EXPR
	mov	rdx, r12
	call	cst_close
	return
  .cancel:
	mov	rdi, [rbx + CstParser.tree]
	mov	rsi, r12
	call	cst_cancel
	return
endp

; __cst_is_addop(p) -> eax = 1 at an additive operator.
;
; All six of §8.4's additive operators: `+` `+%` `+|` `-` `-%` `-|`.
;
; This checked only four until c6645f3. The comment here used to say the gap
; was the lexer's -- correctly, at the time: lexer/token.inc had PUN_MINUS
; alone, so `-%` could not reach this file as a token and a parser accepting
; it would have been accepting something that did not exist. The lexer gained
; PUN_MINUSPCT and PUN_MINUSBAR in that commit and this set was not widened
; with it, so `-%` stayed an EXS-E0201 for a second reason after the first was
; gone. Found by ast/from_cst.inc exercising all six through the real driver.
proc __cst_is_addop, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	mov	esi, PUN_PLUS
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_PLUSPCT
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_PLUSBAR
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_MINUS
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_MINUSPCT
	call	__cst_pun
	test	eax, eax
	jnz	.yes
	mov	rdi, rbx
	mov	esi, PUN_MINUSBAR
	call	__cst_pun
	return
  .yes:
	mov	eax, 1
	return
endp

; __cst_add(p) -- level 5.
proc __cst_add, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	xor	r13d, r13d
	mov	rdi, rbx
	call	__cst_mul
  .more:
	mov	rdi, rbx
	call	__cst_is_addop
	test	eax, eax
	jz	.done
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_mul
	mov	r13d, 1
	jmp	.more
  .done:
	test	r13d, r13d
	jz	.cancel
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ADD_EXPR
	mov	rdx, r12
	call	cst_close
	return
  .cancel:
	mov	rdi, [rbx + CstParser.tree]
	mov	rsi, r12
	call	cst_cancel
	return
endp

; __cst_shift(p) -- level 5a: `Shift ::= Add [ShiftOp Add]`, NON-associative,
; so exactly one operator, like `..` and the comparisons. `a sursum 1 sursum 2`
; is therefore not a special case anywhere: this production returns after the
; first `sursum 1`, no lower level takes `sursum`, and the statement that asked
; for `;` meets an identifier instead -- `EXS-E0201` at the second word, by
; the same path that rejects `a lt b lt c` (spec §8.6, "Shifts are
; non-associative like comparison"). Spec §8.6 cites the fixture that
; exercises it: tests/unit/cst_shift_xor.asm.
proc __cst_shift, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_add
	mov	rdi, rbx
	call	__cst_is_shiftop
	test	eax, eax
	jz	.cancel
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_add
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_SHIFT_EXPR
	mov	rdx, r12
	call	cst_close
	return
  .cancel:
	mov	rdi, [rbx + CstParser.tree]
	mov	rsi, r12
	call	cst_cancel
	return
endp

; __cst_xor(p) -- level 5b: `Xor ::= Shift ('aut' Shift)*`, left-associative
; and FLAT like every other repeated level here (cst/kinds.inc, departure 1):
; `a aut b aut c` is one `XOR_EXPR` with three operands, and
; ast/from_cst.inc's fold makes it `(a aut b) aut c`.
proc __cst_xor, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	xor	r13d, r13d
	mov	rdi, rbx
	call	__cst_shift
  .more:
	mov	rdi, rbx
	call	__cst_aut
	test	eax, eax
	jz	.done
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_shift
	mov	r13d, 1
	jmp	.more
  .done:
	test	r13d, r13d
	jz	.cancel
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_XOR_EXPR
	mov	rdx, r12
	call	cst_close
	return
  .cancel:
	mov	rdi, [rbx + CstParser.tree]
	mov	rsi, r12
	call	cst_cancel
	return
endp

; __cst_range(p) -- level 6: `Xor ['..' Xor]`, NON-associative, so exactly one.
; It read `Add ['..' Add]` until spec §8.6 put levels 5a and 5b between them.
proc __cst_range, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_xor
	mov	rdi, rbx
	mov	esi, PUN_DOTDOT
	call	__cst_pun
	test	eax, eax
	jz	.cancel
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_xor
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_RANGE_EXPR
	mov	rdx, r12
	call	cst_close
	return
  .cancel:
	mov	rdi, [rbx + CstParser.tree]
	mov	rsi, r12
	call	cst_cancel
	return
endp

; __cst_is_cmpop(p) -> eax = 1 at one of `lt le gt ge eq ne`.
proc __cst_is_cmpop, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_word2
	cmp	eax, CST_W_LT
	je	.yes
	cmp	eax, CST_W_LE
	je	.yes
	cmp	eax, CST_W_GT
	je	.yes
	cmp	eax, CST_W_GE
	je	.yes
	cmp	eax, CST_W_EQ
	je	.yes
	cmp	eax, CST_W_NE
	je	.yes
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __cst_cmp(p) -- level 7: `Range [CmpOp Range]`, non-associative.
proc __cst_cmp, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_range
	mov	rdi, rbx
	call	__cst_is_cmpop
	test	eax, eax
	jz	.cancel
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_range
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_CMP_EXPR
	mov	rdx, r12
	call	cst_close
	return
  .cancel:
	mov	rdi, [rbx + CstParser.tree]
	mov	rsi, r12
	call	cst_cancel
	return
endp

; __cst_and(p) -- level 8: `Cmp ('et' Cmp)*`.
proc __cst_and, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	xor	r13d, r13d
	mov	rdi, rbx
	call	__cst_cmp
  .more:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_word2
	cmp	eax, CST_W_ET
	jne	.done
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_cmp
	mov	r13d, 1
	jmp	.more
  .done:
	test	r13d, r13d
	jz	.cancel
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_AND_EXPR
	mov	rdx, r12
	call	cst_close
	return
  .cancel:
	mov	rdi, [rbx + CstParser.tree]
	mov	rsi, r12
	call	cst_cancel
	return
endp

; __cst_expr(p) -- level 9: `Or ::= And ('vel' And)*`, and the entry point.
proc __cst_expr, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	xor	r13d, r13d
	mov	rdi, rbx
	call	__cst_and
  .more:
	mov	rdi, rbx
	call	__cst_vel
	test	eax, eax
	jz	.done
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_and
	mov	r13d, 1
	jmp	.more
  .done:
	test	r13d, r13d
	jz	.cancel
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_OR_EXPR
	mov	rdx, r12
	call	cst_close
	return
  .cancel:
	mov	rdi, [rbx + CstParser.tree]
	mov	rsi, r12
	call	cst_cancel
	return
endp

; __cst_expr_ns(p) -- `ExprNS`: §8.6's expression grammar with the level-1 `{`
; suffix disabled, used "in every position where a `{` block follows an
; expression: `si`, `sin`, `dum`, `terminus`, the `per`/`quisque` range,
; `discerne`, and the right-hand side of `sub`."
;
; THE FLAG IS `CstParser.nolit`, read at level 1 by `__cst_postfix` and
; nowhere else. It was one call site away from existing for as long as struct
; literals were `[OPEN]` (this file's header, finding 5, records the wait);
; `docs/design/wire-codec.md` D3 settled the syntax and this is the flag.
;
; SAVED AND RESTORED, NOT SET AND CLEARED: `si (S { a: 1 }).a eq 1 {` reaches
; `__cst_expr_lit` inside the parentheses, which clears the flag for the
; bracketed sub-expression and must hand it back SET to the rest of the
; condition -- or the `{` of the block would be read as a literal.
proc __cst_expr_ns, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	r12d, [rbx + CstParser.nolit]
	mov	dword [rbx + CstParser.nolit], 1
	mov	rdi, rbx
	call	__cst_expr
	mov	[rbx + CstParser.nolit], r12d
	return
endp

; __cst_expr_lit(p) -- `Expr` with the struct-literal suffix ADMITTED again,
; whatever the enclosing position said. Called where an expression sits
; between brackets of its own -- `( … )`, a call's `( … )`, an index's
; `[ … ]`, a literal's own `{ … }` -- and, through `__cst_block`, for every
; statement of a block. Inside brackets a `{` cannot be the block `ExprNS`
; guards, which is the same rule Rust applies and the one §8.6's "a literal
; in one of those positions is parenthesised" presupposes: parenthesising
; would not help if the flag survived the parenthesis.
proc __cst_expr_lit, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	r12d, [rbx + CstParser.nolit]
	mov	dword [rbx + CstParser.nolit], 0
	mov	rdi, rbx
	call	__cst_expr
	mov	[rbx + CstParser.nolit], r12d
	return
endp

; ---- parameter and generic lists -------------------------------------------

; __cst_param(p) -- `Param ::= IDENT ':' Type`. §8.6 decision 2 makes the type
; mandatory even in a lambda.
proc __cst_param, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_COLON
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_type
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_PARAM
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_param_list(p) -- `'(' [Param (',' Param)*] ')'`.
proc __cst_param_list, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_xpun
  .item:
	mov	rdi, rbx
	mov	esi, PUN_RPAREN
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	cmp	eax, TOK_IDENT
	je	.param
	cmp	eax, TOK_KEYWORD
	je	.param
	mov	rdi, rbx
	mov	esi, CST_SYNC_LIST
	mov	edx, PUN_RPAREN
	call	__cst_error_skip
	jmp	.sep
  .param:
	mov	rdi, rbx
	call	__cst_param
  .sep:
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_pun
	test	eax, eax
	jz	.close
	mov	rdi, rbx
	call	__cst_bump
	jmp	.item
  .close:
	mov	rdi, rbx
	mov	esi, PUN_RPAREN
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_PARAM_LIST
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_generic_params(p) -- `'<' GenericParam (',' GenericParam)* '>'`, where
; `GenericParam ::= IDENT [':' Type]`.
proc __cst_generic_params, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, PUN_LT
	call	__cst_xpun
  .item:
	mov	rdi, rbx
	mov	esi, PUN_GT
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	cmp	eax, TOK_IDENT
	je	.gp
	cmp	eax, TOK_KEYWORD
	je	.gp
	mov	rdi, rbx
	mov	esi, CST_SYNC_LIST
	mov	edx, PUN_GT
	call	__cst_error_skip
	jmp	.sep
  .gp:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_COLON
	call	__cst_pun
	test	eax, eax
	jz	.gp_done
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_type
  .gp_done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_GENERIC_PARAM
	mov	rdx, r13
	call	cst_close
  .sep:
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_pun
	test	eax, eax
	jz	.close
	mov	rdi, rbx
	call	__cst_bump
	jmp	.item
  .close:
	mov	rdi, rbx
	mov	esi, PUN_GT
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_GENERIC_PARAMS
	mov	rdx, r12
	call	cst_close
	return
endp

; ---- statements (§8.6, "Grammar") ------------------------------------------

; __cst_block(p) -- `Block ::= '{' Stmt* '}'`.
;
; A BLOCK WHOSE `{` WAS NEVER THERE DOES NOT CLOSE ON A `}`. §8.6's recovery
; rule is "No production consumes a `}` it did not open", and until this was
; measured, this routine broke it: a block synthesised during recovery -- a
; `per` statement invented out of a reserved word used as a name, say -- ran
; its statement loop to the file's real closing brace and ATE it, after which
; the function body that actually opened that brace reached end of input and
; reported `EXS-E0203` in a file whose braces balance.
; `docs/design/diagnostics-review.md` c05 calls that diagnostic "bogus" and it
; is: it is this file consuming a `}` it did not open, one production removed.
;
; So the opening `__cst_xpun`'s answer is kept, and when it is 0 the block
; ends with a zero-width `CST_MISSING` and NO diagnostic. Silence is the point
; -- the absent `{` was already reported (or already suppressed as the same
; token), and a second "expected `}`" for a brace the author never opened is
; the cascade this rule exists to prevent.
proc __cst_block, p
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [p]
	; a block's statements are full `Expr` positions even when the block
	; itself sits inside an `ExprNS` (a lambda in a `si` head); the flag is
	; the enclosing position's and comes back at the `}`
	mov	r14d, [rbx + CstParser.nolit]
	mov	dword [rbx + CstParser.nolit], 0
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_xpun
	mov	r13d, eax
  .stmt:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	mov	rdi, rbx
	call	__cst_stmt
	jmp	.stmt
  .close:
	test	r13d, r13d
	jz	.synth
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_xpun
	jmp	.fin
  .synth:
	mov	rdi, rbx
	call	__cst_missing
  .fin:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_BLOCK
	mov	rdx, r12
	call	cst_close
	mov	[rbx + CstParser.nolit], r14d
	return
endp

; __cst_binding_stmt(p) -- `('firma' | 'mutabilis') IDENT [':' Type]
; ['=' Expr] ';'`. Module level uses the same routine: §8.6 decision 5 says
; `mutabilis` at module level PARSES and is rejected by the checker
; (`EXS-E0500`, `EXS-E0501`), because "a parse error can neither name the rule
; nor tell the capability-typed case apart".
proc __cst_binding_stmt, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump			; `firma` or `mutabilis`
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_COLON
	call	__cst_pun
	test	eax, eax
	jz	.value
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_type
  .value:
	mov	rdi, rbx
	mov	esi, PUN_EQ
	call	__cst_pun
	test	eax, eax
	jz	.semi
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_expr
  .semi:
	mov	rdi, rbx
	mov	esi, PUN_SEMI
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_BINDING_STMT
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_jump_stmt(p) -- `'redde' [Expr] ';' | 'rumpe' ';' | 'perge' ';'`.
; One-token decision: §8.6's peek list gives "`redde ;` vs `redde Expr ;`".
proc __cst_jump_stmt, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_REDDE
	call	__cst_kw
	mov	r13d, eax
	mov	rdi, rbx
	call	__cst_bump
	test	r13d, r13d
	jz	.semi
	mov	rdi, rbx
	call	__cst_expr_starts
	test	eax, eax
	jz	.semi
	mov	rdi, rbx
	call	__cst_expr
  .semi:
	mov	rdi, rbx
	mov	esi, PUN_SEMI
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_JUMP_STMT
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_sub_stmt(p) -- `'sub' Path '=' ExprNS ( ';' | Block )`. §8.6 decision
; 3: one production, two endings, "identical prefix, one-token decision,
; identical recovery".
proc __cst_sub_stmt, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump			; `sub`
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_IDENT
	jne	.nopath
	mov	rdi, rbx
	call	__cst_path
	jmp	.eq
  .nopath:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
  .eq:
	mov	rdi, rbx
	mov	esi, PUN_EQ
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_expr_ns
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_pun
	test	eax, eax
	jz	.semi
	mov	rdi, rbx
	call	__cst_block
	jmp	.done
  .semi:
	mov	rdi, rbx
	mov	esi, PUN_SEMI
	call	__cst_xpun
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_SUB_STMT
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_if_stmt(p) -- `'si' ExprNS Block ('sin' ExprNS Block)* ['aliter' Block]`.
proc __cst_if_stmt, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump			; `si`
	mov	rdi, rbx
	call	__cst_expr_ns
	mov	rdi, rbx
	call	__cst_block
  .sin:
	mov	rdi, rbx
	mov	esi, KW_SIN
	call	__cst_kw
	test	eax, eax
	jz	.aliter
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_expr_ns
	mov	rdi, rbx
	call	__cst_block
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_SIN_ARM
	mov	rdx, r13
	call	cst_close
	jmp	.sin
  .aliter:
	mov	rdi, rbx
	mov	esi, KW_ALITER
	call	__cst_kw
	test	eax, eax
	jz	.done
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_block
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ALITER_ARM
	mov	rdx, r13
	call	cst_close
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_IF_STMT
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_while_stmt(p) -- `'dum' ExprNS ['terminus' ExprNS] Block`. §8.6 leaves
; `terminus` optional deliberately, "so that its absence is a CST fact the
; profile can reject (§8.5)" -- so an unbounded `dum` parses here and is not
; this file's to refuse.
proc __cst_while_stmt, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump			; `dum`
	mov	rdi, rbx
	call	__cst_expr_ns
	mov	rdi, rbx
	mov	esi, KW_TERMINUS
	call	__cst_kw
	test	eax, eax
	jz	.body
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_expr_ns
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_TERMINUS_CLAUSE
	mov	rdx, r13
	call	cst_close
  .body:
	mov	rdi, rbx
	call	__cst_block
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_WHILE_STMT
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_arith_op(p) -- `ArithOp`, the reduction operator of a `contrahe`
; clause. See `__cst_is_addop` on the two §8.4 tokens the lexer cannot yet
; produce.
proc __cst_arith_op, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	call	__cst_is_addop
	test	eax, eax
	jnz	.take
	mov	rdi, rbx
	mov	esi, PUN_STAR
	call	__cst_pun
	test	eax, eax
	jnz	.take
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
	return
  .take:
	mov	rdi, rbx
	call	__cst_bump
	return
endp

; __cst_for_stmt(p) -- `('per' | 'quisque') IDENT 'in' ExprNS
; ('contrahe' IDENT ':' ArithOp)* ['forma' IDENT [INT]] Block`.
;
; THE `INT` AFTER `forma IDENT` IS OPTIONAL HERE ON PURPOSE, and §8.6 says why:
; "The `INT` is **required after `arborea` and forbidden after `ordinata`**,
; which is a semantic rule rather than a grammatical one: both spellings parse,
; and a missing or surplus width is a checker diagnostic." The shape word is a
; contextual identifier (§8.4 tier 2), so a parser that enforced the pairing
; would be reading a name to decide a production -- which is exactly the kind
; of thing §8.6's LL(1) constraint exists to forbid.
proc __cst_for_stmt, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump			; `per` or `quisque`
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, KW_IN
	call	__cst_xkw
	mov	rdi, rbx
	call	__cst_expr_ns
  .contrahe:
	mov	rdi, rbx
	mov	esi, KW_CONTRAHE
	call	__cst_kw
	test	eax, eax
	jz	.forma
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_COLON
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_arith_op
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_CONTRAHE_CLAUSE
	mov	rdx, r13
	call	cst_close
	jmp	.contrahe
  .forma:
	mov	rdi, rbx
	mov	esi, KW_FORMA
	call	__cst_kw
	test	eax, eax
	jz	.body
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_NUMBER
	jne	.forma_done
	mov	rdi, rbx
	call	__cst_bump
  .forma_done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_FORMA_CLAUSE
	mov	rdx, r13
	call	cst_close
  .body:
	mov	rdi, rbx
	call	__cst_block
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_FOR_STMT
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_pattern(p) -- `Pattern ::= Literal | Path`. Constructor patterns are
; `[OPEN]` in §8.6 and are not invented here.
proc __cst_pattern, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_NUMBER
	je	.lit
	cmp	eax, TOK_STRING
	je	.lit
	cmp	eax, TOK_IDENT
	je	.path
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
	jmp	.done
  .lit:
	mov	rdi, rbx
	call	__cst_bump
	jmp	.done
  .path:
	mov	rdi, rbx
	call	__cst_path
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_PATTERN
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_match_stmt(p) -- `'discerne' ExprNS '{' ('casus' Pattern Block)*
; ['aliter' Block] '}'`. One-token decision throughout: §8.6's peek list ends
; with "`casus` vs `aliter` vs `}`".
proc __cst_match_stmt, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_bump			; `discerne`
	mov	rdi, rbx
	call	__cst_expr_ns
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_xpun
  .arm:
	mov	rdi, rbx
	mov	esi, KW_CASUS
	call	__cst_kw
	test	eax, eax
	jnz	.casus
	mov	rdi, rbx
	mov	esi, KW_ALITER
	call	__cst_kw
	test	eax, eax
	jnz	.aliter
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	mov	rdi, rbx
	mov	esi, CST_SYNC_CASUS
	xor	edx, edx
	call	__cst_error_skip
	jmp	.arm
  .casus:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_pattern
	mov	rdi, rbx
	call	__cst_block
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_CASUS_ARM
	mov	rdx, r13
	call	cst_close
	jmp	.arm
  .aliter:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_block
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ALITER_ARM
	mov	rdx, r13
	call	cst_close
  .close:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_MATCH_STMT
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_expr_stmt(p) -- `Expr ['=' Expr] ';'`, which is two statements rather
; than one (cst/kinds.inc, departure 2): §8.6 decision 5 makes assignment a
; STATEMENT and never an operator, "it is excluded from conditions, and the
; lvalue check is semantic".
proc __cst_expr_stmt, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	r13d, CST_EXPR_STMT
	mov	rdi, rbx
	call	__cst_expr
	mov	rdi, rbx
	mov	esi, PUN_EQ
	call	__cst_pun
	test	eax, eax
	jz	.semi
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_expr
	mov	r13d, CST_ASSIGN_STMT
  .semi:
	mov	rdi, rbx
	mov	esi, PUN_SEMI
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, r13d
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_stmt(p) -- `Stmt`, dispatched entirely on the leading token except for
; the one two-token peek §8.6 lists for it.
;
; PRECONDITION: not at end of input and not at `}` -- the block loop checks
; both, and every branch here consumes at least one token, which is what makes
; that loop terminate.
proc __cst_stmt, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_KEYWORD
	je	.kw
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.block
	jmp	.expr

  .block:
	mov	rdi, rbx
	call	__cst_block
	return

  .kw:
	mov	rdi, rbx
	mov	esi, KW_FIRMA
	call	__cst_kw
	test	eax, eax
	jnz	.binding
	mov	rdi, rbx
	mov	esi, KW_MUTABILIS
	call	__cst_kw
	test	eax, eax
	jnz	.binding
	mov	rdi, rbx
	mov	esi, KW_SUB
	call	__cst_kw
	test	eax, eax
	jnz	.sub
	mov	rdi, rbx
	mov	esi, KW_REDDE
	call	__cst_kw
	test	eax, eax
	jnz	.jump
	mov	rdi, rbx
	mov	esi, KW_RUMPE
	call	__cst_kw
	test	eax, eax
	jnz	.jump
	mov	rdi, rbx
	mov	esi, KW_PERGE
	call	__cst_kw
	test	eax, eax
	jnz	.jump
	mov	rdi, rbx
	mov	esi, KW_SI
	call	__cst_kw
	test	eax, eax
	jnz	.if
	mov	rdi, rbx
	mov	esi, KW_DUM
	call	__cst_kw
	test	eax, eax
	jnz	.while
	mov	rdi, rbx
	mov	esi, KW_PER
	call	__cst_kw
	test	eax, eax
	jnz	.for
	mov	rdi, rbx
	mov	esi, KW_QUISQUE
	call	__cst_kw
	test	eax, eax
	jnz	.for
	mov	rdi, rbx
	mov	esi, KW_DISCERNE
	call	__cst_kw
	test	eax, eax
	jnz	.match
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_kw
	test	eax, eax
	jnz	.functio
	jmp	.expr

  .binding:
	mov	rdi, rbx
	call	__cst_binding_stmt
	return
  .sub:
	mov	rdi, rbx
	call	__cst_sub_stmt
	return
  .jump:
	mov	rdi, rbx
	call	__cst_jump_stmt
	return
  .if:
	mov	rdi, rbx
	call	__cst_if_stmt
	return
  .while:
	mov	rdi, rbx
	call	__cst_while_stmt
	return
  .for:
	mov	rdi, rbx
	call	__cst_for_stmt
	return
  .match:
	mov	rdi, rbx
	call	__cst_match_stmt
	return

	; §8.6's peek: "statement start `functio` | `functio IDENT` | a nested
	; named function: EXS-E0201 ...; otherwise a lambda expression
	; statement". The fix §8.6 asks for is three edits, not one -- see this
	; file's header, finding 3 -- so the diagnostic carries none, and the
	; construct is then parsed as the declaration it plainly is.
  .functio:
	mov	rdi, rbx
	mov	esi, 1
	mov	edx, TOK_IDENT
	mov	rcx, -1
	call	__cst_is
	test	eax, eax
	jz	.expr
	mov	rdi, rbx
	mov	esi, 201
	call	__cst_err
	mov	rdi, rbx
	call	__cst_function_decl
	return

  .expr:
	mov	rdi, rbx
	call	__cst_expr_starts
	test	eax, eax
	jz	.junk
	mov	rdi, rbx
	call	__cst_expr_stmt
	return
  .junk:
	mov	rdi, rbx
	mov	esi, CST_SYNC_STMT
	xor	edx, edx
	call	__cst_error_skip
	return
endp

; ---- items (§8.6, "Grammar") -----------------------------------------------

; __cst_signature(p) -- `'functio' IDENT [GenericParams] ParamList ['->' Type]
; [DeclRow]`. The `DeclRow` here is the BARE row; a braced one belongs to the
; result type and `__cst_type` has already taken it (§8.6, "Where `poscit`
; attaches", and its "one odd corner").
proc __cst_signature, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_xkw
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_LT
	call	__cst_pun
	test	eax, eax
	jz	.params
	mov	rdi, rbx
	call	__cst_generic_params
  .params:
	mov	rdi, rbx
	call	__cst_param_list
	mov	rdi, rbx
	mov	esi, PUN_ARROW
	call	__cst_pun
	test	eax, eax
	jz	.row
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_type
  .row:
	mov	rdi, rbx
	mov	esi, KW_POSCIT
	call	__cst_kw
	test	eax, eax
	jz	.done
	mov	rdi, rbx
	call	__cst_decl_row
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_SIGNATURE
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_function_decl(p) -- `FunctionDecl ::= Signature Block`.
proc __cst_function_decl, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_signature
	mov	rdi, rbx
	call	__cst_block
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_FUNCTION_DECL
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_struct_decl(p) -- `'structura' IDENT [GenericParams] '{' Field* '}'`,
; with a SEPARATOR-FREE body (§8.6 decision 1: "a type cannot be continued by
; an identifier or by `functio`, so the next member is decidable").
proc __cst_struct_decl, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_STRUCTURA
	call	__cst_xkw
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_LT
	call	__cst_pun
	test	eax, eax
	jz	.body
	mov	rdi, rbx
	call	__cst_generic_params
  .body:
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_xpun
  .field:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	cmp	eax, TOK_IDENT
	jne	.bad
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_COLON
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_type
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_FIELD
	mov	rdx, r13
	call	cst_close
	jmp	.field
  .bad:
	mov	rdi, rbx
	mov	esi, CST_SYNC_MEMBER
	xor	edx, edx
	call	__cst_error_skip
	jmp	.field
  .close:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_STRUCT_DECL
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_type_decl(p) -- `'typus' IDENT [GenericParams] '=' Type ';'`. Sum types
; are `[OPEN]`.
proc __cst_type_decl, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_TYPUS
	call	__cst_xkw
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_LT
	call	__cst_pun
	test	eax, eax
	jz	.eq
	mov	rdi, rbx
	call	__cst_generic_params
  .eq:
	mov	rdi, rbx
	mov	esi, PUN_EQ
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_type
	mov	rdi, rbx
	mov	esi, PUN_SEMI
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_TYPE_DECL
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_member(p) -- `Member ::= Signature [Block]`, the separator-free form
; §5.3 and §10.1 write. §8.6's "What this section corrects": "`interfacies`
; declaration bodies now hold `Member`, which is §5.3's and §10.1's
; separator-free form, not a `;`-terminated one."
proc __cst_member, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	call	__cst_signature
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_pun
	test	eax, eax
	jz	.done
	mov	rdi, rbx
	call	__cst_block
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_MEMBER
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_interface_decl(p) -- `'interfacies' Path [GenericParams]
; ( '{' Member* '}' | 'in' Type [DeclRow] '{' FunctionDecl* '}' )`.
;
; §8.6 decision 4: declaration and implementation share the keyword and "one
; token after the head decides" -- `{` or `in`. The alternative `implet` was
; considered and rejected because "a reserved word spends a root forever".
proc __cst_interface_decl, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_INTERFACIES
	call	__cst_xkw
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_IDENT
	jne	.nopath
	mov	rdi, rbx
	call	__cst_path
	jmp	.generics
  .nopath:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
  .generics:
	mov	rdi, rbx
	mov	esi, PUN_LT
	call	__cst_pun
	test	eax, eax
	jz	.decide
	mov	rdi, rbx
	call	__cst_generic_params
  .decide:
	mov	rdi, rbx
	mov	esi, KW_IN
	call	__cst_kw
	test	eax, eax
	jnz	.impl

	; declaration body: Member*
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_xpun
  .member:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.member_close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.member_close
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_kw
	test	eax, eax
	jz	.member_bad
	mov	rdi, rbx
	call	__cst_member
	jmp	.member
  .member_bad:
	mov	rdi, rbx
	mov	esi, CST_SYNC_MEMBER
	xor	edx, edx
	call	__cst_error_skip
	jmp	.member
  .member_close:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_INTERFACE_BODY
	mov	rdx, r13
	call	cst_close
	jmp	.done

	; implementation: `in Type [DeclRow]` then `{ FunctionDecl* }`
  .impl:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump			; `in`
	mov	rdi, rbx
	call	__cst_type
	mov	rdi, rbx
	mov	esi, KW_POSCIT
	call	__cst_kw
	test	eax, eax
	jz	.impl_head_done
	mov	rdi, rbx
	call	__cst_decl_row
  .impl_head_done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_IMPL_HEAD
	mov	rdx, r13
	call	cst_close

	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_xpun
  .fn:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.fn_close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.fn_close
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_kw
	test	eax, eax
	jz	.fn_bad
	mov	rdi, rbx
	call	__cst_function_decl
	jmp	.fn
  .fn_bad:
	mov	rdi, rbx
	mov	esi, CST_SYNC_MEMBER
	xor	edx, edx
	call	__cst_error_skip
	jmp	.fn
  .fn_close:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_IMPL_BODY
	mov	rdx, r13
	call	cst_close
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_INTERFACE_DECL
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_potestas_decl(p) -- `'potestas' IDENT '=' '{' [Path (',' Path)*] '}'`.
; Ends in `}`, so it takes no `;` (§8.6 decision 1).
proc __cst_potestas_decl, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_POTESTAS
	call	__cst_xkw
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_EQ
	call	__cst_xpun
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_xpun
  .item:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	cmp	eax, TOK_IDENT
	jne	.bad
	mov	rdi, rbx
	call	__cst_path
	jmp	.sep
  .bad:
	mov	rdi, rbx
	mov	esi, CST_SYNC_LIST
	xor	edx, edx
	call	__cst_error_skip
  .sep:
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_pun
	test	eax, eax
	jz	.close
	mov	rdi, rbx
	call	__cst_bump
	jmp	.item
  .close:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_POTESTAS_DECL
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_externus_block(p) -- `'externus' '(' STRING ',' 'abi' ':' IDENT ')'
; '{' (['publica'] Signature)* '}'`. `abi` is contextual (§8.4 tier 2), so it
; is an ordinary IDENT here and is not checked for spelling: that is a
; semantic question about a name, not a phrase-level one.
proc __cst_externus_block, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, KW_EXTERNUS
	call	__cst_xkw

	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	mov	esi, PUN_LPAREN
	call	__cst_xpun
	mov	rdi, rbx
	mov	esi, TOK_STRING
	call	__cst_xtok
	mov	rdi, rbx
	mov	esi, PUN_COMMA
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_COLON
	call	__cst_xpun
	mov	rdi, rbx
	call	__cst_xident
	mov	rdi, rbx
	mov	esi, PUN_RPAREN
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_EXTERNUS_HEAD
	mov	rdx, r13
	call	cst_close

	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_xpun
  .member:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_pun
	test	eax, eax
	jnz	.close
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.close
	mov	rdi, rbx
	mov	esi, KW_PUBLICA
	call	__cst_kw
	test	eax, eax
	jnz	.take
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_kw
	test	eax, eax
	jnz	.take
	mov	rdi, rbx
	mov	esi, CST_SYNC_MEMBER
	xor	edx, edx
	call	__cst_error_skip
	jmp	.member
  .take:
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	mov	esi, KW_PUBLICA
	call	__cst_kw
	test	eax, eax
	jz	.take_sig
	mov	rdi, rbx
	call	__cst_bump
  .take_sig:
	mov	rdi, rbx
	call	__cst_signature
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_EXTERNUS_MEMBER
	mov	rdx, r13
	call	cst_close
	jmp	.member
  .close:
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_EXTERNUS_BLOCK
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_item_starts(p) -> eax = 1 if a module item can begin at the cursor.
proc __cst_item_starts, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_ANNOT
	je	.yes
	cmp	eax, TOK_KEYWORD
	jne	.no
	mov	rdi, rbx
	mov	esi, CST_SYNC_MODULE
	xor	edx, edx
	call	__cst_insync
	return
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __cst_item(p) -- `Item ::= Annotation* ['publica'] ItemBody`.
proc __cst_item, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
  .annot:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_ANNOT
	jne	.publica
	; ONE token, not two -- see this file's header, finding 1.
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r13, rax
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ANNOTATION
	mov	rdx, r13
	call	cst_close
	jmp	.annot
  .publica:
	mov	rdi, rbx
	mov	esi, KW_PUBLICA
	call	__cst_kw
	test	eax, eax
	jz	.body
	mov	rdi, rbx
	call	__cst_bump
  .body:
	mov	rdi, rbx
	mov	esi, KW_FUNCTIO
	call	__cst_kw
	test	eax, eax
	jnz	.fn
	mov	rdi, rbx
	mov	esi, KW_STRUCTURA
	call	__cst_kw
	test	eax, eax
	jnz	.struct
	mov	rdi, rbx
	mov	esi, KW_TYPUS
	call	__cst_kw
	test	eax, eax
	jnz	.typus
	mov	rdi, rbx
	mov	esi, KW_INTERFACIES
	call	__cst_kw
	test	eax, eax
	jnz	.iface
	mov	rdi, rbx
	mov	esi, KW_POTESTAS
	call	__cst_kw
	test	eax, eax
	jnz	.potestas
	mov	rdi, rbx
	mov	esi, KW_EXTERNUS
	call	__cst_kw
	test	eax, eax
	jnz	.externus
	mov	rdi, rbx
	mov	esi, KW_FIRMA
	call	__cst_kw
	test	eax, eax
	jnz	.binding
	mov	rdi, rbx
	mov	esi, KW_MUTABILIS
	call	__cst_kw
	test	eax, eax
	jnz	.binding

	; An annotation or `publica` with no item after it.
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.miss
	mov	rdi, rbx
	mov	esi, CST_SYNC_MODULE
	xor	edx, edx
	call	__cst_error_skip
	jmp	.done
  .miss:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
	jmp	.done
  .fn:
	mov	rdi, rbx
	call	__cst_function_decl
	jmp	.done
  .struct:
	mov	rdi, rbx
	call	__cst_struct_decl
	jmp	.done
  .typus:
	mov	rdi, rbx
	call	__cst_type_decl
	jmp	.done
  .iface:
	mov	rdi, rbx
	call	__cst_interface_decl
	jmp	.done
  .potestas:
	mov	rdi, rbx
	call	__cst_potestas_decl
	jmp	.done
  .externus:
	mov	rdi, rbx
	call	__cst_externus_block
	jmp	.done
  .binding:
	mov	rdi, rbx
	call	__cst_binding_stmt
  .done:
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_ITEM
	mov	rdx, r12
	call	cst_close
	return
endp

; __cst_module(p) -- `Module ::= Item* EOF`.
proc __cst_module, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
  .item:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_kindn
	cmp	eax, TOK_EOF
	je	.eof
	mov	rdi, rbx
	call	__cst_item_starts
	test	eax, eax
	jz	.bad
	mov	rdi, rbx
	call	__cst_item
	jmp	.item
  .bad:
	mov	rdi, rbx
	mov	esi, CST_SYNC_MODULE
	xor	edx, edx
	call	__cst_error_skip
	jmp	.item
  .eof:
	mov	rdi, rbx
	call	__cst_eat_eof
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_MODULE
	mov	rdx, r12
	call	cst_close
	return
endp

; ---- the whole parser ------------------------------------------------------

; cst_parse(p, lx, t) -> CF clear and eax = 0 if the token stream parsed with
; no diagnostic; CF set and eax = the numeric part of the FIRST diagnostic
; otherwise. Channel 1 of docs/asm-conventions.md's "1.3 Error protocol: CF /
; eax", and the only routine in this module that speaks it -- `lex_run` is the
; same shape for the same reason.
;
; A TREE IS PRODUCED EITHER WAY. `CF` set does not mean "no tree": §9.1's CST
; is error-tolerant, so `CstTree.root` is valid on both paths and holds every
; byte of the input. The caller decides whether to go on to the next stage.
;
; PRECONDITION: `lex_run` (or `lex_source_check` then `lex_tokenize`) has
; already filled the `Lexer`'s token vector, and §8.1's gate has passed -- a
; file that fails §8.1 is not tokenized at all, so there is nothing here to
; parse (see this file's header).
proc cst_parse, p, lx, t
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rax, [lx]
	mov	[rbx + CstParser.lx], rax
	mov	rax, [t]
	mov	[rbx + CstParser.tree], rax
	mov	qword [rbx + CstParser.ti], 0
	mov	qword [rbx + CstParser.pos], 0
	mov	dword [rbx + CstParser.depth], 0
	mov	dword [rbx + CstParser.lasterr], -1
	mov	dword [rbx + CstParser.ndiag], 0
	mov	dword [rbx + CstParser.firstcode], 0
	mov	dword [rbx + CstParser.nolit], 0

	mov	rdi, rbx
	call	__cst_module
	mov	rdi, [rbx + CstParser.tree]
	call	cst_finish

	cmp	dword [rbx + CstParser.ndiag], 0
	jne	.diagnosed
	xor	eax, eax
	return
  .diagnosed:
	; `fail` with a REGISTER, not a literal: the code is only known at
	; runtime. Still the sanctioned exit -- it routes through `endp`'s
	; single epilogue so the `uses` pop happens.
	mov	edx, [rbx + CstParser.firstcode]
	fail	edx
endp

; cst_root(p) -> eax = the green id of the parsed root.
proc cst_root, p
	locals
	endl
	mov	rax, [p]
	mov	rax, [rax + CstParser.tree]
	mov	eax, [rax + CstTree.root]
	return
endp
