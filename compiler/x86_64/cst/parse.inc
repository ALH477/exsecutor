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
;      cst/kinds.inc's header, departure 1. Ten nested nodes per atom is what
;      a literal reading produces.
;   5. `ExprNS` IS TODAY EXACTLY `Expr`. §8.6 defines `ExprNS` as "the
;      expression grammar with that suffix disabled", where the suffix is the
;      struct literal -- which is `[OPEN]` and therefore not implemented at
;      all, so the two grammars currently coincide. `__cst_expr_ns` exists as
;      its own entry point anyway, called from exactly the seven positions
;      §8.6 names, so that admitting struct literals later touches level 1 and
;      nothing else. That is what §8.6 asked for; it is recorded here because
;      a reader diffing the two routines would otherwise conclude one of them
;      is dead code.
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
; comparisons and `et` are exactly two bytes, and `vel` is the only three-byte
; one. Written as explicit shifts because a two-character literal's byte order
; is an assembler convention, and this file should not depend on one.
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
struct CstParser
	field lx,        dq	; -> Lexer
	field tree,      dq	; -> CstTree
	field ti,        dq	; index of the current token
	field pos,       dq	; first source byte not yet in the tree
	field depth,     dd	; open (, [, {, < -- decides E0202 vs E0203
	field lasterr,   dd	; token index the last error was reported at
	field ndiag,     dd	; diagnostics THIS parse recorded
	field firstcode, dd	; numeric part of the first one; 0 = none
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
cst_pun_len:
	db	1,1,1,2,1,1,1,2,1,2,2,1,1,1,1,1,1,1,1,1,1,1
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

; __cst_vel(p) -> eax = 1 if the current token is the identifier `vel`, the one
; contextual operator word that is not two bytes long.
proc __cst_vel, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_tokp
	cmp	dword [rax + Tok.kind], TOK_IDENT
	jne	.no
	cmp	dword [rax + Tok.span.len], 3
	jne	.no
	mov	rcx, [rbx + CstParser.lx]
	mov	rcx, [rcx + Lexer.src_ptr]
	mov	edx, [rax + Tok.span.start]
	add	rcx, rdx
	cmp	byte [rcx], 'v'
	jne	.no
	cmp	byte [rcx + 1], 'e'
	jne	.no
	cmp	byte [rcx + 2], 'l'
	jne	.no
	mov	eax, 1
	return
  .no:
	xor	eax, eax
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
  .open:
	inc	dword [rbx + CstParser.depth]
	return
  .close:
	cmp	dword [rbx + CstParser.depth], 0
	je	.done
	dec	dword [rbx + CstParser.depth]
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
	return
  .suppress:
	xor	eax, eax
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
proc __cst_xpun, p, id
	uses	rbx, r12, r13
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
	jnz	.take_sigil
	mov	rdi, rbx
	mov	esi, PUN_STAR
	call	__cst_pun
	test	eax, eax
	jz	.core
  .take_sigil:
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
; the one fact `__cst_postfix` needs: §8.6 restricts generic arguments (and,
; when they exist, struct literals) to "directly after a path segment (`IDENT`,
; `. IDENT`, or `<…>`)".
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
	xor	esi, esi
	call	__cst_err
	mov	rdi, rbx
	call	__cst_missing
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
	call	__cst_expr
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
  .kw_give_back:
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_err
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
	call	__cst_expr
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

; __cst_postfix(p) -- level 1: `.f` `(…)` `[…]` `?` `<…>`, left-associative.
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
	call	__cst_expr
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

; __cst_mul(p) -- level 4: `Cast ('*' Cast)*`. `/`, remainder and the `*%`/`*|`
; overflow forms are `[OPEN]` in §8.6 and are not invented here.
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
	mov	esi, PUN_STAR
	call	__cst_pun
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
; `+` `+%` `+|` `-` and NOT `-%` `-|`: §8.4's table attests all six and
; lexer/token.inc implements four, so the other two cannot reach this file as
; tokens at all. See this file's header, finding 2 -- the gap is the lexer's,
; and a parser that "accepted" `-%` would be accepting a token that does not
; exist.
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

; __cst_range(p) -- level 6: `Add ['..' Add]`, NON-associative, so exactly one.
proc __cst_range, p
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
	mov	esi, PUN_DOTDOT
	call	__cst_pun
	test	eax, eax
	jz	.cancel
	mov	rdi, rbx
	call	__cst_bump
	mov	rdi, rbx
	call	__cst_add
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
; IT IS TODAY THE SAME GRAMMAR AS `Expr`, and that is not an oversight -- see
; this file's header, finding 5. Struct-literal syntax is `[OPEN]`; §8.6
; reserves its position and asks that `ExprNS` be defined now "so admitting it
; later touches nothing outside level 1". This routine is that definition, and
; every one of the seven positions calls it, so the later change is one flag in
; one place instead of seven call sites to find.
proc __cst_expr_ns, p
	locals
	endl
	mov	rdi, [p]
	call	__cst_expr
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
proc __cst_block, p
	uses	rbx, r12
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + CstParser.tree]
	call	cst_open
	mov	r12, rax
	mov	rdi, rbx
	mov	esi, PUN_LBRACE
	call	__cst_xpun
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
	mov	rdi, rbx
	mov	esi, PUN_RBRACE
	call	__cst_xpun
	mov	rdi, [rbx + CstParser.tree]
	mov	esi, CST_BLOCK
	mov	rdx, r12
	call	cst_close
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
