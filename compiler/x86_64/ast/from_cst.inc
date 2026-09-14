; compiler/x86_64/ast/from_cst.inc
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
; THE CST -> AST WALKER. `docs/design/typed-ast.md` section 2.11, and the seam
; where source finally becomes a typed tree: `cst/parse.inc`'s lossless,
; error-tolerant §9.1 tree in, ast/build.inc's postorder `Ast` out.
;
; POSTORDER, BY CONSTRUCTION. Every routine here builds its children before
; itself, because `ast_node` is the only way to make a node and it range-checks
; its operands against the tree so far (ast/build.inc). A walker that tried to
; emit a parent first would trap at the call, not at review.
;
; THE SPAN IS THE LINK, AND IT IS TRIMMED. A green node is shared and
; positionless and a red node is never persisted (cst/green.inc, cst/red.inc),
; so the AST keeps no back-pointer: a consumer finds the CST node by offset.
; But a CST node's own extent INCLUDES its leading trivia -- `SIGNATURE@564+27`
; in examples/saluta.exsc starts on a space -- and a diagnostic pointing at a
; run of whitespace is a worse diagnostic. `__ast_w_bounds` therefore computes
; each AST node's span from the first to the last NON-TRIVIA byte in its
; subtree. Whitespace and comments are trivia; `TK_JUNK` and `CST_MISSING` are
; not (junk is source bytes the lexer rejected and a missing token is
; zero-width where the token should have been -- both need a position).
;
; WHAT IS DROPPED: whitespace, comments, punctuation and keyword tokens,
; parentheses (`CST_PAREN_EXPR` and `CST_PAREN_TYPE` return their content --
; precedence is structure), `CST_TYPE_ARG` and `CST_PATTERN` wrappers, and the
; `ExprNS` distinction, which the CST does not record either. Safe because the
; formatter and the LSP read the CST (spec §9.1) and diagnostics need only
; spans.
;
; WHAT IS KEPT: parse failure. A malformed file still yields a tree and Stage
; 2 still types the rest of it -- spec §16's Stage 1 kill criterion is
; diagnostics, and a checker that stops at the first parse error cannot report
; a second. Exactly how, stated precisely, because typed-ast.md section 2.11's
; "an `ERROR` or `MISSING` CST node becomes an `Error` node" is true of one of
; those two and only sometimes true of the other, and RUNNING IT is what said
; so:
;
;   - `CST_ERROR` wraps the tokens the parser skipped. It is an interior node,
;     every list walk reaches it, and it always becomes an `AST_ERROR` with
;     its span. tests/unit/ast_from_cst_error.asm checks exactly this.
;   - `CST_TK_JUNK` is a source byte the lexer rejected; same treatment when a
;     walk reaches it.
;   - `CST_MISSING` is a zero-width LEAF standing where a required token was
;     absent, and the walks below iterate INTERIOR children, so a missing `;`
;     or `)` is simply not visited -- and should not be. It carries no meaning
;     that an `Error` operand could stand in for, the §8.3 diagnostic already
;     has its position and its insertion fix, and putting an `Error` in
;     `Redde.a` because a semicolon was missing would replace a perfectly good
;     expression with a hole. Where a MISSING stands for something the tree
;     DOES need -- a parameter's type, a `dum`'s body, a cast's target -- the
;     routine that wanted it finds no child of that kind and makes the
;     `AST_ERROR` itself, which is where every `__ast_w_err` call below that
;     is not a `CST_ERROR` comes from.
;
; NO DIAGNOSTICS ARE RAISED HERE. Every §13 code this file could want has
; already been raised by the lexer or the parser into the one diagnostics
; vector (cst/parse.inc reports through `lex_diag`). A second path would be a
; second place for them to disagree, and this file invents no code (CLAUDE.md).
; A construct the walker cannot express becomes an `AST_ERROR` node, never a
; trap and never a silent drop.
;
; WORDS ARE COMPARED, NOT TOKEN KINDS. A green leaf carries an `rt/intern.inc`
; id and nothing else -- the `Tok`'s `KW_*`/`PUN_*` classification does not
; survive into the tree (cst/green.inc: a green token is `(kind, text)` and
; `(` and `)` differ only in the text). So `redde` versus `rumpe`, `+` versus
; `+%`, `per` versus `quisque` are all byte comparisons against
; `ast_w_words` below. Interned ids would be faster but the interner is shared
; with the lexer and an id depends on what has been seen, which is a fact
; about the input and not a constant this file could name.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.6 (the grammar the CST kinds are),
; §9.1, §8.3 (spans), §5.2, §5.4, §5.5.
; -----------------------------------------------------------------------------

include 'load.inc'

; ---- the walk's state ------------------------------------------------------
; Threaded by pointer because six is the dialect's argument limit and a walker
; routine needs the tree, the CST, the interner, the file id and the enclosing
; declaration before it has said anything about the node it is on.
struct AstWalk
	field ast,     dq	; -> Ast
	field cst,     dq	; -> CstTree
	field names,   dq	; -> Interner, shared with the lexer
	field file_id, dd
	field parent,  dd	; enclosing AstDecl id; 0 at module level
end struct

; ---- the words this file has to recognise ----------------------------------
; Same `db LEN, 'text'` pool shape as ast/kinds.inc's, read by `ast_name_at`.
; Order is this list's own; nothing outside this file indexes it.
AST_W_PUBLICA	= 1
AST_W_REDDE	= 2
AST_W_RUMPE	= 3
AST_W_PERGE	= 4
AST_W_FIRMA	= 5
AST_W_MUTABILIS	= 6
AST_W_PER	= 7
AST_W_QUISQUE	= 8
AST_W_SICUT	= 9
AST_W_APUD	= 10
AST_W_NUCLEUS	= 11
AST_W_TRANSITUS	= 12
AST_W_VEL	= 13
AST_W_ET	= 14
AST_W_LT	= 15
AST_W_LE	= 16
AST_W_GT	= 17
AST_W_GE	= 18
AST_W_EQ	= 19
AST_W_NE	= 20
AST_W_DOTDOT	= 21
AST_W_PLUS	= 22
AST_W_PLUSPCT	= 23
AST_W_PLUSBAR	= 24
AST_W_MINUS	= 25
AST_W_MINUSPCT	= 26
AST_W_MINUSBAR	= 27
AST_W_STAR	= 28
AST_W_AUT	= 29	; spec §8.6 level 5b -- see the assert below
AST_W_SURSUM	= 30	; level 5a
AST_W_DEORSUM	= 31	; level 5a
AST_W_SLASH	= 32	; `/`, the float-only quotient (this wave)
AST_W_AMP	= 33
AST_W_COLON	= 34
AST_W_EQUALS	= 35
AST_W_ARROW	= 36
AST_W_ABI	= 37
AST_W_COUNT	= 37

; `__ast_w_op` maps a word to an operator by DISTANCE: the operator words run
; `AST_W_VEL`..`AST_W_SLASH` in exactly the order `AST_OP_VEL`..
; `AST_OP_DIV` do in ast/kinds.inc, and the n-th word is the n-th op. The
; three spec §8.6 5a/5b words, and now `/`, were appended to BOTH lists
; after `*` rather than interleaved at their precedence, so the ops already
; numbered did not move. Two lists kept in step by hand are checked here,
; not trusted.
assert AST_W_STAR - AST_W_VEL = AST_OP_MUL - AST_OP_VEL
assert AST_W_AUT - AST_W_VEL = AST_OP_AUT - AST_OP_VEL
assert AST_W_SURSUM - AST_W_VEL = AST_OP_SURSUM - AST_OP_VEL
assert AST_W_DEORSUM - AST_W_VEL = AST_OP_DEORSUM - AST_OP_VEL
assert AST_W_SLASH - AST_W_VEL = AST_OP_DIV - AST_OP_VEL

ast_w_words:
	db 7,'publica'
	db 5,'redde'
	db 5,'rumpe'
	db 5,'perge'
	db 5,'firma'
	db 9,'mutabilis'
	db 3,'per'
	db 7,'quisque'
	db 5,'sicut'
	db 4,'apud'
	db 8,'@nucleus'	; TK_ANNOT keeps its `@` -- §8.4 lexes `@nomen` as ONE
	db 10,'@transitus'	; token, and cst/kinds.inc's departure 3 records it
	db 3,'vel'
	db 2,'et'
	db 2,'lt'
	db 2,'le'
	db 2,'gt'
	db 2,'ge'
	db 2,'eq'
	db 2,'ne'
	db 2,'..'
	db 1,'+'
	db 2,'+%'
	db 2,'+|'
	db 1,'-'
	db 2,'-%'
	db 2,'-|'
	db 1,'*'
	db 3,'aut'
	db 6,'sursum'
	db 7,'deorsum'
	db 1,'/'			; the float quotient; word pool order MUST
					; track the AST_W_* list above
	db 1,'&'
	db 1,':'
	db 1,'='
	db 2,'->'
	db 3,'abi'
AST_W_WORDS_LEN = $ - ast_w_words

; ---- primitives over a red node --------------------------------------------

proc __ast_w_kind, w, red
	locals
	endl
	mov	rdi, [w]
	mov	rdi, [rdi + AstWalk.cst]
	mov	rsi, [red]
	call	cst_red_kind
	return
endp

proc __ast_w_nkid, w, red
	locals
	endl
	mov	rdi, [w]
	mov	rdi, [rdi + AstWalk.cst]
	mov	rsi, [red]
	call	cst_red_nkid
	return
endp

proc __ast_w_child, w, red, i, dest
	locals
	endl
	mov	rdi, [w]
	mov	rdi, [rdi + AstWalk.cst]
	mov	rsi, [red]
	mov	rdx, [i]
	mov	rcx, [dest]
	call	cst_red_child
	return
endp

; rax = the interner id of a LEAF's text, or 0 for an interior node or an
; empty leaf. Reaches into the green record directly: `CstGreen.text` IS the
; id (cst/green.inc), and there is no accessor for it because nothing before
; this file needed one.
proc __ast_w_text, w, red
	locals
	endl
	mov	rdi, [w]
	mov	rdi, [rdi + AstWalk.cst]
	mov	rsi, [red]
	mov	esi, [rsi + CstRed.green]
	call	cst_green_ptr
	mov	ecx, [rax + CstGreen.nkid]
	mov	edx, CST_GREEN_LEAF
	cmp	ecx, edx
	jne	.interior
	mov	eax, [rax + CstGreen.text]
	return
  .interior:
	xor	eax, eax
	return
endp

; eax = 1 if the leaf at `red` spells word `n` of `ast_w_words`.
proc __ast_w_isw, w, red, n
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	call	__ast_w_text
	test	eax, eax
	jz	.no
	mov	rdi, [rbx + AstWalk.names]
	mov	rsi, rax
	call	intern_bytes		; rax = bytes, rdx = length
	mov	r12, rax
	mov	r13, rdx
	lea	rdi, [ast_w_words]
	mov	rsi, [n]
	call	ast_name_at		; rax = bytes, rdx = length
	cmp	rdx, r13
	jne	.no
	mov	rdi, rax
	mov	rsi, r12
	call	__map_bytes_equal
	return
  .no:
	xor	eax, eax
	return
endp

; ---- spans, trimmed of trivia ----------------------------------------------
; eax = 1 and `dest` filled with (start, len) covering the first through the
; last non-trivia byte of this subtree; eax = 0 and `dest` = (red.off, 0) when
; the subtree is nothing but trivia. `file_id` is not written here -- the
; caller's `__ast_w_span` does that -- so this routine can recurse without
; carrying it.
proc __ast_w_bounds, w, red, dest
	uses	rbx, r12, r13, r14
	locals
		slot wb_sub,   sizeof.CstRed
		slot wb_tmp,   sizeof.Span
		slot wb_start, dq
		slot wb_end,   dq
		slot wb_found, dq
		slot wb_i,     dq
		slot wb_n,     dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	r13, [dest]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_kind
	mov	r14, rax

	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wb_n], rax
	mov	rcx, CST_LEAF_LAST
	cmp	r14, rcx
	jg	.interior

	; A leaf. Whitespace and comments are trivia and contribute nothing;
	; everything else -- including TK_JUNK and CST_MISSING -- is a position
	; this tree is entitled to point at.
	cmp	r14, CST_TK_WS
	je	.trivia
	cmp	r14, CST_TK_COMMENT
	je	.trivia
	mov	rax, [r12 + CstRed.off]
	mov	[r13 + Span.start], eax
	mov	rdi, rbx
	mov	rdi, [rdi + AstWalk.cst]
	mov	rsi, r12
	call	cst_red_len
	mov	[r13 + Span.len], eax
	mov	eax, 1
	return

  .interior:
	mov	qword [wb_found], 0
	mov	qword [wb_i], 0
  .kid:
	mov	rax, [wb_i]
	cmp	rax, [wb_n]
	jae	.kids_done
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wb_i]
	lea	rcx, [wb_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wb_sub]
	lea	rdx, [wb_tmp]
	call	__ast_w_bounds
	test	eax, eax
	jz	.next_kid
	mov	eax, [wb_tmp + Span.start]
	mov	ecx, [wb_tmp + Span.len]
	add	rcx, rax
	cmp	qword [wb_found], 0
	jne	.extend
	mov	[wb_start], rax
	mov	qword [wb_found], 1
  .extend:
	mov	[wb_end], rcx
  .next_kid:
	inc	qword [wb_i]
	jmp	.kid
  .kids_done:
	cmp	qword [wb_found], 0
	je	.trivia
	mov	rax, [wb_start]
	mov	[r13 + Span.start], eax
	mov	rcx, [wb_end]
	sub	rcx, rax
	mov	[r13 + Span.len], ecx
	mov	eax, 1
	return

  .trivia:
	mov	rax, [r12 + CstRed.off]
	mov	[r13 + Span.start], eax
	mov	dword [r13 + Span.len], 0
	xor	eax, eax
	return
endp

; Fill a whole `Span` -- file id included -- for this node.
proc __ast_w_span, w, red, dest
	uses	rbx
	locals
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	mov	rdx, [dest]
	call	__ast_w_bounds
	mov	rbx, [w]
	mov	rdi, [dest]
	mov	eax, [rbx + AstWalk.file_id]
	mov	[rdi + Span.file_id], eax
	return
endp

; ---- child search ----------------------------------------------------------
; eax = the index of the first child of kind `kind` at or after `from`, or -1.
proc __ast_w_findk, w, red, from, kind
	uses	rbx, r12, r13, r14
	locals
		slot wf_sub, sizeof.CstRed
		slot wf_n,   dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	r13, [from]
	mov	r14, [kind]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wf_n], rax
  .loop:
	mov	rax, [wf_n]
	cmp	r13, rax
	jae	.miss
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [wf_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wf_sub]
	call	__ast_w_kind
	cmp	rax, r14
	je	.hit
	inc	r13
	jmp	.loop
  .hit:
	mov	rax, r13
	return
  .miss:
	mov	rax, -1
	return
endp

; eax = 1 if a child of kind `kind` exists, and `dest` is its red view.
proc __ast_w_kidk, w, red, kind, dest
	uses	rbx
	locals
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	xor	rdx, rdx
	mov	rcx, [kind]
	call	__ast_w_findk
	cmp	rax, 0
	jl	.no
	mov	rdi, rbx
	mov	rsi, [red]
	mov	rdx, rax
	mov	rcx, [dest]
	call	__ast_w_child
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; ---- the two nodes every walker needs --------------------------------------
; An `AST_ERROR` with this CST node's span. The one node this file makes for a
; construct it cannot express, and for every `CST_ERROR` / `CST_MISSING` /
; `CST_TK_JUNK` the parser left behind.
proc __ast_w_err, w, red
	uses	rbx
	locals
		slot we_sp, sizeof.Span
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	lea	rdx, [we_sp]
	call	__ast_w_span
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, AST_ERROR
	xor	rdx, rdx
	xor	rcx, rcx
	xor	r8, r8
	lea	r9, [we_sp]
	call	ast_node
	return
endp

; `ast_node` with this node's span, saving every caller the same six lines.
proc __ast_w_mk, w, red, kind, aux, a, b
	uses	rbx
	locals
		slot wm_sp, sizeof.Span
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	lea	rdx, [wm_sp]
	call	__ast_w_span
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [kind]
	mov	rdx, [aux]
	mov	rcx, [a]
	mov	r8, [b]
	lea	r9, [wm_sp]
	call	ast_node
	return
endp

; A declaration with this node's span, parented to the walk's current one.
proc __ast_w_decl, w, red, kind, flags, nameid
	uses	rbx
	locals
		slot wd_sp, sizeof.Span
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	lea	rdx, [wd_sp]
	call	__ast_w_span
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [kind]
	mov	rdx, [flags]
	mov	rcx, [nameid]
	mov	r8d, [rbx + AstWalk.parent]
	lea	r9, [wd_sp]
	call	ast_decl
	return
endp

; ---- paths -----------------------------------------------------------------
; `Path` -> `AST_PATH` over `AST_SEG`s, one per IDENT, dots dropped. `gargs`
; is an already-built `AST_GENARGS` node (or 0) and lands on the LAST segment,
; which is typed-ast.md section 2.3's `Seg.b`. It has to be built before the
; segments are, because `Seg.b` is an `ast_node` operand and postorder means an
; operand's index is already fixed when its user is created.
proc __ast_w_pathg, w, red, gargs
	uses	rbx, r12, r13, r14
	locals
		slot wp_sub,  sizeof.CstRed
		slot wp_mark, dq
		slot wp_n,    dq
		slot wp_i,    dq
		slot wp_last, dq
		slot wp_off,  dq
		slot wp_cnt,  dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wp_n], rax

	; Which child is the last IDENT? The generic arguments belong to that
	; one and to no other, so it is found before anything is built.
	mov	qword [wp_last], -1
	mov	qword [wp_i], 0
  .scan:
	mov	rax, [wp_i]
	cmp	rax, [wp_n]
	jae	.scanned
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wp_i]
	lea	rcx, [wp_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wp_sub]
	call	__ast_w_kind
	cmp	eax, CST_TK_IDENT
	jne	.scan_next
	mov	rax, [wp_i]
	mov	[wp_last], rax
  .scan_next:
	inc	qword [wp_i]
	jmp	.scan
  .scanned:
	cmp	qword [wp_last], 0
	jl	.empty

	mov	rdi, [rbx + AstWalk.ast]
	call	ast_list_mark
	mov	[wp_mark], rax
	mov	qword [wp_i], 0
  .seg:
	mov	rax, [wp_i]
	cmp	rax, [wp_n]
	jae	.segs_done
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wp_i]
	lea	rcx, [wp_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wp_sub]
	call	__ast_w_kind
	cmp	eax, CST_TK_IDENT
	jne	.seg_next
	mov	rdi, rbx
	lea	rsi, [wp_sub]
	call	__ast_w_text
	mov	r13, rax		; the segment's interner id
	xor	r14, r14
	mov	rax, [wp_i]
	cmp	rax, [wp_last]
	jne	.seg_mk
	mov	r14, [gargs]		; only the last segment takes them
  .seg_mk:
	mov	rdi, rbx
	lea	rsi, [wp_sub]
	mov	rdx, AST_SEG
	xor	rcx, rcx
	mov	r8, r13
	mov	r9, r14
	call	__ast_w_mk
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, rax
	call	ast_list_push
  .seg_next:
	inc	qword [wp_i]
	jmp	.seg
  .segs_done:
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wp_mark]
	call	ast_list_emit
	mov	[wp_off], rax
	mov	[wp_cnt], rdx
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_PATH
	xor	rcx, rcx
	mov	r8, [wp_off]
	mov	r9, [wp_cnt]
	call	__ast_w_mk
	return
  .empty:
	; A `Path` with no identifier in it is a recovery artefact, not a path.
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	return
endp

; ---- bit widths ------------------------------------------------------------
; `BitType` is spelled `u` followed by digits and the PARSER already decided it
; is admissible (spec §8.6: "In type position an identifier of the form `u`
; followed by digits is a `BitType`, admitted only for widths 1-7 and 8, 16,
; 24, 32, 40, 48, 56, 64" -- `u12` DOES NOT PARSE). So this only has to read
; the number back out; a width it cannot read is 0, which ast/verify.inc does
; not check and Stage 2 will.
proc __ast_w_width, w, red
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	call	__ast_w_text
	test	eax, eax
	jz	.none
	mov	rdi, [rbx + AstWalk.names]
	mov	rsi, rax
	call	intern_bytes		; rax = bytes, rdx = length
	mov	r12, rax
	mov	r13, rdx
	cmp	r13, 2
	jl	.none
	movzx	eax, byte [r12]
	cmp	eax, 'u'
	jne	.none
	inc	r12
	dec	r13
	xor	r14, r14
  .digit:
	movzx	eax, byte [r12]
	sub	eax, 48
	cmp	eax, 0
	jl	.none
	cmp	eax, 9
	jg	.none
	imul	r14, r14, 10
	add	r14, rax
	inc	r12
	dec	r13
	jnz	.digit
	mov	rax, r14
	return
  .none:
	xor	eax, eax
	return
endp

; ---- types -----------------------------------------------------------------
; `Type ::= ('&' | '*')* CoreType (':' IDENT | 'apud' IDENT)*` (spec §8.6).
; Built from the inside out, which is also postorder: the core, then each
; suffix left to right, then each prefix sigil RIGHT TO LEFT -- the sigil
; nearest the core binds tightest, so `*&T` is a raw pointer to a reference.
;
; Every wrapper carries the whole `Type`'s span rather than its own fragment.
; A `TyPtr` has no CST node of its own (the sigil is a bare `TK_PUNCT`), and
; giving a `TySuffix` only the suffix's extent would point a diagnostic at
; `:maior` with the type it qualifies off-screen.
proc __ast_w_type, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wt_sub,  sizeof.CstRed
		slot wt_n,    dq
		slot wt_i,    dq
		slot wt_core, dq
		slot wt_cur,  dq
		slot wt_ga,   dq
		slot wt_mut,  dq	; a `mutabilis` seen in the prefix scan,
					;   waiting for the `&` it belongs to
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wt_n], rax
	mov	qword [wt_ga], 0
	mov	qword [wt_mut], 0

	; The generic arguments, if any, before the core that owns them.
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_GENERIC_ARGS
	lea	rcx, [wt_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_gargs
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	call	__ast_w_node
	mov	[wt_ga], rax
  .no_gargs:

	; The core. Exactly one child is one of the five core kinds.
	mov	qword [wt_core], 0
	mov	qword [wt_i], 0
  .find:
	mov	rax, [wt_i]
	cmp	rax, [wt_n]
	jae	.found
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wt_i]
	lea	rcx, [wt_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	call	__ast_w_kind
	cmp	eax, CST_PATH
	je	.core_path
	cmp	eax, CST_BIT_TYPE
	je	.core_bit
	cmp	eax, CST_DYN_TYPE
	je	.core_other
	cmp	eax, CST_FN_TYPE
	je	.core_other
	cmp	eax, CST_PAREN_TYPE
	je	.core_other
	cmp	eax, CST_ERROR
	je	.core_other
	cmp	eax, CST_MISSING
	je	.core_other
	inc	qword [wt_i]
	jmp	.find

  .core_path:
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	mov	rdx, [wt_ga]
	call	__ast_w_pathg
	mov	r13, rax
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	mov	rdx, AST_TYPATH
	xor	rcx, rcx
	mov	r8, r13
	xor	r9, r9
	call	__ast_w_mk
	mov	[wt_core], rax
	jmp	.found
  .core_bit:
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	mov	rdx, CST_TK_IDENT
	lea	rcx, [wt_sub]
	call	__ast_w_kidk		; the `uN` ident, in place
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	call	__ast_w_width
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_TYBIT
	mov	rcx, r13
	xor	r8, r8
	xor	r9, r9
	call	__ast_w_mk
	mov	[wt_core], rax
	jmp	.found
  .core_other:
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	call	__ast_w_node
	mov	[wt_core], rax
  .found:
	cmp	qword [wt_core], 0
	jne	.have_core
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wt_core], rax
  .have_core:
	mov	rax, [wt_core]
	mov	[wt_cur], rax

	; The suffixes, left to right.
	mov	qword [wt_i], 0
  .sfx:
	mov	rax, [wt_i]
	cmp	rax, [wt_n]
	jae	.sfx_done
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wt_i]
	lea	rcx, [wt_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	call	__ast_w_kind
	mov	r14, rax
	cmp	eax, CST_ORDER_SUFFIX
	je	.sfx_do
	cmp	eax, CST_PLACE_SUFFIX
	jne	.sfx_next
  .sfx_do:
	; `aux` says which: `:` byte order (spec §5.2) or `apud` placement
	; (spec §5.5). The identifier is the suffix's last child.
	mov	r13, AST_SFX_ORDER
	cmp	r14, CST_PLACE_SUFFIX
	jne	.sfx_kind_ok
	mov	r13, AST_SFX_APUD
  .sfx_kind_ok:
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	mov	rdx, CST_TK_IDENT
	lea	rcx, [wt_sub]
	call	__ast_w_kidk
	xor	r14, r14
	test	eax, eax
	jz	.sfx_noid
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	call	__ast_w_text
	mov	r14, rax
  .sfx_noid:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_TYSUFFIX
	mov	rcx, r13
	mov	r8, [wt_cur]
	mov	r9, r14
	call	__ast_w_mk
	mov	[wt_cur], rax
  .sfx_next:
	inc	qword [wt_i]
	jmp	.sfx
  .sfx_done:

	; The prefix sigils, RIGHT to LEFT.
	mov	r13, [wt_n]
  .pfx:
	test	r13, r13
	jz	.pfx_done
	dec	r13
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [wt_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	call	__ast_w_kind
	cmp	eax, CST_TK_KEYWORD
	jne	.pfx_notkw
	; The prefix sigils are walked RIGHT TO LEFT, and `mutabilis` sits
	; immediately after its `&`, so it is met one step BEFORE that `&`.
	; Remember it and let the `&` below consume it (ADR 0016).
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	mov	rdx, AST_W_MUTABILIS
	call	__ast_w_isw
	test	eax, eax
	jz	.pfx
	mov	qword [wt_mut], 1
	jmp	.pfx
  .pfx_notkw:
	cmp	eax, CST_TK_PUNCT
	jne	.pfx
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	mov	rdx, AST_W_AMP
	call	__ast_w_isw
	test	eax, eax
	jnz	.pfx_ref
	mov	rdi, rbx
	lea	rsi, [wt_sub]
	mov	rdx, AST_W_STAR
	call	__ast_w_isw
	test	eax, eax
	jz	.pfx
	mov	r14, AST_PTR_RAW
	jmp	.pfx_mk
  .pfx_ref:
	mov	r14, AST_PTR_REF
	cmp	qword [wt_mut], 0
	je	.pfx_mk
	mov	r14, AST_PTR_REFMUT
	mov	qword [wt_mut], 0
  .pfx_mk:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_TYPTR
	mov	rcx, r14
	mov	r8, [wt_cur]
	xor	r9, r9
	call	__ast_w_mk
	mov	[wt_cur], rax
	jmp	.pfx
  .pfx_done:
	mov	rax, [wt_cur]
	return
endp

; ---- a list of children of one kind ----------------------------------------
; Builds every child of kind `kind`, in order, onto the staging stack and
; emits it. rax = the `Ast.extra` offset, rdx = the count -- exactly the pair a
; node's AST_R_LIST / AST_R_COUNT slots take.
proc __ast_w_list, w, red, kind
	uses	rbx, r12, r13, r14
	locals
		slot wl_sub,  sizeof.CstRed
		slot wl_mark, dq
		slot wl_n,    dq
		slot wl_i,    dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	r14, [kind]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wl_n], rax
	mov	rdi, [rbx + AstWalk.ast]
	call	ast_list_mark
	mov	[wl_mark], rax
	mov	qword [wl_i], 0
  .kid:
	mov	rax, [wl_i]
	cmp	rax, [wl_n]
	jae	.done
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wl_i]
	lea	rcx, [wl_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wl_sub]
	call	__ast_w_kind
	cmp	rax, r14
	jne	.next
	mov	rdi, rbx
	lea	rsi, [wl_sub]
	call	__ast_w_node
	test	rax, rax
	jz	.next
	mov	r13, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	call	ast_list_push
  .next:
	inc	qword [wl_i]
	jmp	.kid
  .done:
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wl_mark]
	call	ast_list_emit
	return
endp

; ---- interior children -----------------------------------------------------
; rax = the index of the first INTERIOR child strictly after `after` (pass -1
; to start), or -1. Interior means "not a leaf", which in this grammar means
; "carries meaning of its own": every operator, keyword, bracket and piece of
; trivia is a leaf, so this is the iterator the expression folds want.
proc __ast_w_nexti, w, red, after
	uses	rbx, r12, r13
	locals
		slot wn_sub, sizeof.CstRed
		slot wn_n,   dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	r13, [after]
	inc	r13
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wn_n], rax
  .loop:
	mov	rax, [wn_n]
	cmp	r13, rax
	jae	.miss
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [wn_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wn_sub]
	call	__ast_w_kind
	mov	rcx, CST_LEAF_LAST
	cmp	rax, rcx
	jg	.hit
	inc	r13
	jmp	.loop
  .hit:
	mov	rax, r13
	return
  .miss:
	mov	rax, -1
	return
endp

; Build the first interior child, or 0 if there is none.
proc __ast_w_inner, w, red
	uses	rbx
	locals
		slot wi_sub, sizeof.CstRed
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	mov	rdx, -1
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.none
	mov	rdi, rbx
	mov	rsi, [red]
	mov	rdx, rax
	lea	rcx, [wi_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wi_sub]
	call	__ast_w_node
	return
  .none:
	xor	eax, eax
	return
endp

; Every interior child, as a list. rax = offset, rdx = count.
proc __ast_w_inners, w, red
	uses	rbx, r12, r13
	locals
		slot wq_sub,  sizeof.CstRed
		slot wq_mark, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, [rbx + AstWalk.ast]
	call	ast_list_mark
	mov	[wq_mark], rax
	mov	r13, -1
  .kid:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.done
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [wq_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wq_sub]
	call	__ast_w_node
	test	rax, rax
	jz	.kid
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, rax
	call	ast_list_push
	jmp	.kid
  .done:
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wq_mark]
	call	ast_list_emit
	return
endp

; `ast_node` with a span the caller computed. The fold routines below build
; nodes that have no CST node of their own -- `a + b - c` is ONE `ADD_EXPR`
; and TWO `AST_BINARY`s -- so their spans are unions of their operands', via
; rt/span.inc's `span_union`, not a CST node's extent. Using the enclosing
; node's extent instead would give the inner `a + b` the span of the whole
; expression, which is a worse diagnostic for no saving.
proc __ast_w_mks, w, sp, kind, aux, a, b
	uses	rbx
	locals
	endl
	mov	rbx, [w]
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [kind]
	mov	rdx, [aux]
	mov	rcx, [a]
	mov	r8, [b]
	mov	r9, [sp]
	call	ast_node
	return
endp

; ---- operators -------------------------------------------------------------
; rax = the AST_OP_* for this operator leaf, or 0. Spec §8.6 spells comparison,
; the two logical operators, exclusive or and the shifts as contextual WORDS,
; so an operator is a `TK_IDENT` at five precedence levels and a `TK_PUNCT` at
; the other three; this routine does not care which, it compares bytes.
proc __ast_w_op, w, red
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [w]
	mov	r12, AST_W_VEL		; the operator words, contiguous
	mov	r13, AST_OP_VEL
  .try:
	mov	rdi, rbx
	mov	rsi, [red]
	mov	rdx, r12
	call	__ast_w_isw
	test	eax, eax
	jnz	.hit
	inc	r12
	inc	r13
	mov	rcx, AST_W_SLASH
	cmp	r12, rcx
	jle	.try
	xor	eax, eax
	return
  .hit:
	mov	rax, r13
	return
endp

; ---- n-ary left-associative folds -----------------------------------------
; `OR_EXPR` `AND_EXPR` `CMP_EXPR` `RANGE_EXPR` `XOR_EXPR` `SHIFT_EXPR`
; `ADD_EXPR` `MUL_EXPR` are all FLAT in the CST -- `1 + 2 * 3 - 4` is one
; `ADD_EXPR` with three operands and two operator leaves -- and all
; left-associative, so one routine folds them all. The three NON-associative
; ones (`CMP_EXPR` `RANGE_EXPR` `SHIFT_EXPR`) never hold more than one operator,
; so the fold is the identity on them. `AST_BINARY.aux` is the operator;
; ssa-ir.md 2.3 turns `+` `+%` `+|` into three opcodes, never a flag on one.
proc __ast_w_binary, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wx_sub, sizeof.CstRed
		slot wx_sp,  sizeof.Span
		slot wx_sp2, sizeof.Span
		slot wx_acc, dq
		slot wx_op,  dq
		slot wx_i,   dq
		slot wx_n,   dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wx_n], rax
	mov	qword [wx_acc], 0
	mov	qword [wx_op], 0
	mov	qword [wx_i], 0
  .kid:
	mov	rax, [wx_i]
	cmp	rax, [wx_n]
	jae	.done
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wx_i]
	lea	rcx, [wx_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wx_sub]
	call	__ast_w_kind
	mov	rcx, CST_LEAF_LAST
	cmp	rax, rcx
	jg	.operand

	; A leaf: an operator if it spells one, otherwise trivia or a bracket.
	cmp	eax, CST_TK_IDENT
	je	.maybe_op
	cmp	eax, CST_TK_PUNCT
	jne	.next
  .maybe_op:
	mov	rdi, rbx
	lea	rsi, [wx_sub]
	call	__ast_w_op
	test	rax, rax
	jz	.next
	mov	[wx_op], rax
	jmp	.next

  .operand:
	mov	rdi, rbx
	lea	rsi, [wx_sub]
	call	__ast_w_node
	test	rax, rax
	jz	.next
	mov	r13, rax
	mov	rdi, rbx
	lea	rsi, [wx_sub]
	lea	rdx, [wx_sp2]
	call	__ast_w_span
	cmp	qword [wx_acc], 0
	jne	.combine
	mov	[wx_acc], r13
	mov	eax, [wx_sp2 + Span.file_id]
	mov	[wx_sp + Span.file_id], eax
	mov	eax, [wx_sp2 + Span.start]
	mov	[wx_sp + Span.start], eax
	mov	eax, [wx_sp2 + Span.len]
	mov	[wx_sp + Span.len], eax
	jmp	.next
  .combine:
	lea	rdi, [wx_sp]
	lea	rsi, [wx_sp2]
	lea	rdx, [wx_sp]
	call	span_union
	mov	rdi, rbx
	lea	rsi, [wx_sp]
	mov	rdx, AST_BINARY
	mov	rcx, [wx_op]
	mov	r8, [wx_acc]
	mov	r9, r13
	call	__ast_w_mks
	mov	[wx_acc], rax
	mov	qword [wx_op], 0
  .next:
	inc	qword [wx_i]
	jmp	.kid
  .done:
	mov	rax, [wx_acc]
	test	rax, rax
	jnz	.ok
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
  .ok:
	return
endp

; `Unary ::= ('-' | '&' | '*') Unary` -- one sigil per CST node, nested for
; more than one.
proc __ast_w_unary, w, red
	uses	rbx, r12, r13
	locals
		slot wu_sub, sizeof.CstRed
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	r13, 0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TK_PUNCT
	lea	rcx, [wu_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_op
	mov	rdi, rbx
	lea	rsi, [wu_sub]
	mov	rdx, AST_W_MINUS
	call	__ast_w_isw
	test	eax, eax
	jnz	.neg
	mov	rdi, rbx
	lea	rsi, [wu_sub]
	mov	rdx, AST_W_AMP
	call	__ast_w_isw
	test	eax, eax
	jnz	.addr
	mov	r13, AST_U_DEREF
	jmp	.no_op
  .neg:
	mov	r13, AST_U_NEG
	jmp	.no_op
  .addr:
	mov	r13, AST_U_ADDR
  .no_op:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_inner
	test	rax, rax
	jnz	.have
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
  .have:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_UNARY
	mov	rcx, r13
	mov	r8, rax
	xor	r9, r9
	call	__ast_w_mk
	return
endp

; `Cast ::= Unary ('sicut' Type)*`, folded left. Spec §8.6 decision 4: the
; cast is postfix `sicut Type` and there is no implicit conversion anywhere in
; the language, which is what lets typed-ast.md section 2.2 keep ONE tree.
proc __ast_w_cast, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wc_sub, sizeof.CstRed
		slot wc_sp,  sizeof.Span
		slot wc_sp2, sizeof.Span
		slot wc_acc, dq
		slot wc_i,   dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wc_acc], 0
	mov	r13, -1
  .kid:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.done
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [wc_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wc_sub]
	call	__ast_w_kind
	cmp	eax, CST_TYPE
	je	.a_type
	mov	rdi, rbx
	lea	rsi, [wc_sub]
	call	__ast_w_node
	mov	[wc_acc], rax
	mov	rdi, rbx
	lea	rsi, [wc_sub]
	lea	rdx, [wc_sp]
	call	__ast_w_span
	jmp	.kid
  .a_type:
	mov	rdi, rbx
	lea	rsi, [wc_sub]
	call	__ast_w_type
	mov	r14, rax
	mov	rdi, rbx
	lea	rsi, [wc_sub]
	lea	rdx, [wc_sp2]
	call	__ast_w_span
	cmp	qword [wc_acc], 0
	je	.kid
	lea	rdi, [wc_sp]
	lea	rsi, [wc_sp2]
	lea	rdx, [wc_sp]
	call	span_union
	mov	rdi, rbx
	lea	rsi, [wc_sp]
	mov	rdx, AST_CAST
	xor	rcx, rcx
	mov	r8, [wc_acc]
	mov	r9, r14
	call	__ast_w_mks
	mov	[wc_acc], rax
	jmp	.kid
  .done:
	mov	rax, [wc_acc]
	test	rax, rax
	jnz	.ok
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
  .ok:
	return
endp

; ---- postfix chains --------------------------------------------------------
; `Postfix ::= Primary Suffix*`, folded left. The awkward one is
; `Suffix ::= GenericArgs` (spec §8.6, "after a path segment only"): generic
; arguments belong to the segment BEFORE them, and in postorder an operand's
; index is fixed when its user is built, so they cannot be attached after the
; fact. This routine therefore looks one suffix ahead and builds the arguments
; FIRST, then the segment or member that owns them.
;
; `Ns.q<u32>(3)` is where typed-ast.md section 2.3 runs out: `Seg.b` holds
; generic arguments on a path segment, but `.q` in expression position is an
; `AST_MEMBER` and that row has no slot at all. `Member.c` is that slot here --
; recorded in ast/kinds.inc and in ast/ast.inc's findings list, because the
; alternative was to drop the arguments of a program spec §8.6 admits.
proc __ast_w_postfix, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wo_sub,  sizeof.CstRed
		slot wo_sub2, sizeof.CstRed
		slot wo_sp,   sizeof.Span
		slot wo_sp2,  sizeof.Span
		slot wo_acc,  dq
		slot wo_skip, dq
		slot wo_g,    dq
		slot wo_j,    dq
		slot wo_off,  dq
		slot wo_cnt,  dq
		slot wo_sp3,  sizeof.Span
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wo_skip], -1
	mov	qword [wo_g], 0

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, -1
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.empty
	mov	r13, rax		; the primary's child index
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [wo_sub]
	call	__ast_w_child

	; Does a `GenericArgs` suffix follow the primary directly?
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.plain
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	lea	rcx, [wo_sub2]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wo_sub2]
	call	__ast_w_kind
	cmp	eax, CST_GENERIC_ARGS
	jne	.plain
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	call	__ast_w_kind
	cmp	eax, CST_NAME_EXPR
	jne	.plain
	mov	rdi, rbx
	lea	rsi, [wo_sub2]
	call	__ast_w_node
	mov	[wo_g], rax
	mov	[wo_skip], r14
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	mov	rdx, [wo_g]
	call	__ast_w_pathg
	mov	[wo_acc], rax
	jmp	.primed
  .plain:
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	call	__ast_w_node
	mov	[wo_acc], rax
  .primed:
	cmp	qword [wo_acc], 0
	jne	.have_primary
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	call	__ast_w_err
	mov	[wo_acc], rax
  .have_primary:
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	lea	rdx, [wo_sp]
	call	__ast_w_span
	mov	[wo_j], r13

  .sfx:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wo_j]
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.done
	mov	[wo_j], rax
	mov	rcx, [wo_skip]
	cmp	rax, rcx
	je	.sfx			; already consumed as generic arguments
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wo_j]
	lea	rcx, [wo_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	lea	rdx, [wo_sp2]
	call	__ast_w_span
	lea	rdi, [wo_sp]
	lea	rsi, [wo_sp2]
	lea	rdx, [wo_sp]
	call	span_union
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	call	__ast_w_kind
	mov	r14, rax
	cmp	eax, CST_FIELD_SUFFIX
	je	.field
	cmp	eax, CST_CALL_SUFFIX
	je	.call
	cmp	eax, CST_INDEX_SUFFIX
	je	.index
	cmp	eax, CST_TRY_SUFFIX
	je	.try
	cmp	eax, CST_STRUCT_LIT_SUFFIX
	je	.structlit
	; Anything else here -- a stray `GenericArgs` with no segment to own
	; it, or a recovery node -- becomes an `Error` operand rather than a
	; silently dropped one.
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	call	__ast_w_err
	mov	[wo_acc], rax
	jmp	.sfx

  .field:
	mov	qword [wo_g], 0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wo_j]
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.field_nog
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	lea	rcx, [wo_sub2]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wo_sub2]
	call	__ast_w_kind
	cmp	eax, CST_GENERIC_ARGS
	jne	.field_nog
	mov	rdi, rbx
	lea	rsi, [wo_sub2]
	call	__ast_w_node
	mov	[wo_g], rax
	mov	[wo_skip], r14
  .field_nog:
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	mov	rdx, CST_TK_IDENT
	lea	rcx, [wo_sub2]
	call	__ast_w_kidk
	xor	r13, r13
	test	eax, eax
	jz	.field_mk
	mov	rdi, rbx
	lea	rsi, [wo_sub2]
	call	__ast_w_text
	mov	r13, rax
  .field_mk:
	mov	rdi, rbx
	lea	rsi, [wo_sp]
	mov	rdx, AST_MEMBER
	xor	rcx, rcx
	mov	r8, [wo_acc]
	mov	r9, r13
	call	__ast_w_mks
	mov	[wo_acc], rax
	cmp	qword [wo_g], 0
	je	.sfx
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wo_acc]
	mov	rdx, [wo_g]
	xor	rcx, rcx
	call	ast_node_cd
	jmp	.sfx

  .call:
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	call	__ast_w_inners
	mov	[wo_off], rax
	mov	[wo_cnt], rdx
	mov	rdi, rbx
	lea	rsi, [wo_sp]
	mov	rdx, AST_CALL
	xor	rcx, rcx
	mov	r8, [wo_acc]
	mov	r9, [wo_off]
	call	__ast_w_mks
	mov	[wo_acc], rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wo_acc]
	mov	rdx, [wo_cnt]
	xor	rcx, rcx
	call	ast_node_cd
	jmp	.sfx

  .index:
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	call	__ast_w_inner
	test	rax, rax
	jnz	.index_mk
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	call	__ast_w_err
  .index_mk:
	mov	r13, rax
	mov	rdi, rbx
	lea	rsi, [wo_sp]
	mov	rdx, AST_INDEX
	xor	rcx, rcx
	mov	r8, [wo_acc]
	mov	r9, r13
	call	__ast_w_mks
	mov	[wo_acc], rax
	jmp	.sfx

  .try:
	mov	rdi, rbx
	lea	rsi, [wo_sp]
	mov	rdx, AST_TRY
	xor	rcx, rcx
	mov	r8, [wo_acc]
	xor	r9, r9
	call	__ast_w_mks
	mov	[wo_acc], rax
	jmp	.sfx

  .structlit:
	; `Path { f: e, ... }` (spec §8.6, wire-codec.md D3). The operand built
	; so far is the literal's PATH, and it names a type: it is wrapped in a
	; `TyPath` carrying the path's own span, so that pass 1 resolves it in
	; type position and pass 2 types it with `chk_ty_of_node` like any other
	; type. The parser admits the suffix after `. IDENT` too, where the
	; operand is a `Member` and not a `Path`; that is left unwrapped and
	; pass 2 answers it with `EXS-E0305` ("a path that is not a
	; `structura`") -- this file raises nothing.
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wo_acc]
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	mov	rdx, qword [rax + AstNode.span]
	mov	qword [wo_sp3], rdx
	mov	edx, dword [rax + AstNode.span + 8]
	mov	dword [wo_sp3 + 8], edx
	cmp	ecx, AST_PATH
	jne	.sl_fields
	mov	rdi, rbx
	lea	rsi, [wo_sp3]
	mov	rdx, AST_TYPATH
	xor	rcx, rcx
	mov	r8, [wo_acc]
	xor	r9, r9
	call	__ast_w_mks
	mov	[wo_acc], rax
  .sl_fields:
	mov	rdi, rbx
	lea	rsi, [wo_sub]
	mov	rdx, CST_FIELD_INIT
	call	__ast_w_list
	mov	[wo_off], rax
	mov	[wo_cnt], rdx
	mov	rdi, rbx
	lea	rsi, [wo_sp]
	mov	rdx, AST_STRUCTLIT
	xor	rcx, rcx
	mov	r8, [wo_acc]
	mov	r9, [wo_off]
	call	__ast_w_mks
	mov	[wo_acc], rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wo_acc]
	mov	rdx, [wo_cnt]
	xor	rcx, rcx
	call	ast_node_cd
	jmp	.sfx

  .done:
	mov	rax, [wo_acc]
	return
  .empty:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	return
endp

; `FieldInit ::= IDENT ':' Expr` -> `AST_FIELDINIT` (a = the name's interner
; id, b = the initialiser). A missing initialiser (`S { a: }`) has already been
; reported by the parser and becomes an `Error` operand, which is what
; `FieldInit.b` being AST_R_NODEQ requires.
proc __ast_w_fieldinit, w, red
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_name
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_inner
	test	rax, rax
	jnz	.have
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
  .have:
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_FIELDINIT
	xor	rcx, rcx
	mov	r8, r13
	mov	r9, r14
	call	__ast_w_mk
	return
endp

; ---- array literals --------------------------------------------------------
; Spec §8.6. `[e1, …, en]` -> `AST_ARRAYLIT` (a/b = the elements in source
; order and their count); `[e; N]` -> `AST_ARRAYREP` (a = the element, b = the
; count literal).
;
; THE ELEMENTS ARE EXACTLY THE INTERIOR CHILDREN: the brackets, the commas and
; the `;` are leaves, so `__ast_w_inners` is the list and no filtering by kind
; is needed. A `CST_ERROR` the parser left where an element should be comes
; through as an `AST_ERROR` element, which is what keeps `ArrayLit`'s list
; slots honest when recovery ran.
proc __ast_w_arraylit, w, red
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_inners
	mov	r13, rax
	mov	r14, rdx
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_ARRAYLIT
	xor	rcx, rcx
	mov	r8, r13
	mov	r9, r14
	call	__ast_w_mk
	return
endp

; The repeat form. Both operands are `AST_R_NODEQ`, so an absent one -- the
; parser refused the count and put the token in a `CST_ERROR`, or the whole
; thing is `[;`-shaped wreckage -- becomes an `AST_ERROR` rather than a 0 the
; verifier would trap on. The parser has already reported it.
proc __ast_w_arrayrep, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wa_i,   dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, -1
	call	__ast_w_kid_node
	mov	[wa_i], rdx
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wa_i]
	call	__ast_w_kid_node
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_ARRAYREP
	xor	rcx, rcx
	mov	r8, r13
	mov	r9, r14
	call	__ast_w_mk
	return
endp

; __ast_w_kid_node(w, red, after) -> rax = the interior child strictly after
; index `after` built as a node (an `AST_ERROR` if there is none), rdx = the
; index it was found at (or `after` unchanged, so a second call cannot loop).
proc __ast_w_kid_node, w, red, after
	uses	rbx, r12, r13
	locals
		slot wk_sub, sizeof.CstRed
		slot wk_i,   dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	r13, [after]
	mov	[wk_i], r13
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.none
	mov	[wk_i], rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rax
	lea	rcx, [wk_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wk_sub]
	call	__ast_w_node
	test	rax, rax
	jz	.none
	mov	rdx, [wk_i]
	return
  .none:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	rdx, [wk_i]
	return
endp

; eax = AST_LIT_INT or AST_LIT_FLOAT, for the NUMBER leaf at `red`.
;
; THE GREEN TREE DOES NOT CARRY THE TOKEN'S `aux` (cst/green.inc has no field
; for it), so the class is re-derived from the leaf's TEXT -- the same bytes
; the lexer scanned, under the same grammar (token.inc): a decimal digit run,
; then a '.' over a peeked digit or a lowercase 'e' exponent is FLOAT;
; everything else is INT. Every `0x` form reads INT for free: the scan stops
; at the 'x' before any 'e' among the hex digits (`0x1e5`'s 'e' is a digit),
; and no '.' can follow a hex run as one token. The lexer already refused
; every text this scan could misread -- `1.`, `1e`, `1.5f32`, capital `1E5`
; never became tokens -- so on everything that reaches here the scan is
; exact, and it is written to mirror the grammar rather than to guess: if
; token.inc's grammar changes, this scan changes with it or is wrong.
;
; An uninterned text (id 0) reads INT, the same bargain the checker's
; `chk_ty_litval` makes for hand-written `.ast` dumps: such a leaf cannot
; arise in a real compile.
proc __ast_w_numclass, w, red
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	call	__ast_w_text
	test	eax, eax
	jz	.int
	mov	rdi, [rbx + AstWalk.names]
	mov	rsi, rax
	call	intern_bytes		; rax = bytes, rdx = length
	mov	r12, rax
	mov	r13, rdx
	xor	r14, r14
  .digits:
	cmp	r14, r13
	jae	.int			; every byte a digit: a plain integer
	movzx	eax, byte [r12 + r14]
	cmp	al, '0'
	jb	.end
	cmp	al, '9'
	ja	.end
	inc	r14
	jmp	.digits
  .end:
	cmp	r14, r13
	jae	.int			; empty or exhausted: no continuation
	cmp	al, 'e'
	je	.float			; `1e-3`: exponent-only form
	cmp	al, '.'
	jne	.int
	lea	rcx, [r14 + 1]		; the lexer's own one-byte-peek rule:
	cmp	rcx, r13		; a '.' joins only over a digit, so `1.`
	jae	.int			; is INT then PUN_DOT, and `0..n` splits
	cmp	byte [r12 + rcx], '0'
	jb	.int
	cmp	byte [r12 + rcx], '9'
	ja	.int
  .float:
	mov	eax, AST_LIT_FLOAT
	return
  .int:
	mov	eax, AST_LIT_INT
	return
endp

; ---- literals --------------------------------------------------------------
; typed-ast.md section 2.3: a `Lit` keeps TEXT, not value -- spec §8.4's
; numeric grammar is `[OPEN]` and a value needs a width the checker supplies,
; so Stage 2 writes the value to `konst[node]`. `a` is therefore the interner
; id of the token's bytes AS WRITTEN, for a string as much as for a number:
; that document says "decoded bytes id (STRING)", and there is nothing in this
; compiler that decodes an escape yet, so the raw id is what exists. Recorded
; as a finding rather than pretended.
proc __ast_w_lit, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wv_sub, sizeof.CstRed
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	r13, AST_LIT_INT
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TK_NUMBER
	lea	rcx, [wv_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.str
	; INT or FLOAT, decided by the text the lexer actually scanned
	mov	rdi, rbx
	lea	rsi, [wv_sub]
	call	__ast_w_numclass
	mov	r13, rax
	jmp	.got
  .str:
	mov	r13, AST_LIT_STRING
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TK_STRING
	lea	rcx, [wv_sub]
	call	__ast_w_kidk
	test	eax, eax
	jnz	.got
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	return
  .got:
	mov	rdi, rbx
	lea	rsi, [wv_sub]
	call	__ast_w_text
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_LIT
	mov	rcx, r13
	mov	r8, r14
	xor	r9, r9
	call	__ast_w_mk
	return
endp

; A bare `TK_NUMBER` or `TK_STRING` that is its own operand -- `casus 1`'s
; `Pattern` and a `TypeArg`'s value argument both hand one over directly.
proc __ast_w_leaflit, w, red, class
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_text
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_LIT
	mov	rcx, [class]
	mov	r8, r13
	xor	r9, r9
	call	__ast_w_mk
	return
endp

; ---- the first IDENT of a node, as an interner id --------------------------
proc __ast_w_name, w, red
	uses	rbx
	locals
		slot wg_sub, sizeof.CstRed
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	mov	rdx, CST_TK_IDENT
	lea	rcx, [wg_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.none
	mov	rdi, rbx
	lea	rsi, [wg_sub]
	call	__ast_w_text
	return
  .none:
	xor	eax, eax
	return
endp

; The interner id of the LAST `TK_IDENT` child -- a `Path`'s final segment,
; which is the name a `sub` binds (spec §4.5).
proc __ast_w_lastname, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wz_sub, sizeof.CstRed
		slot wz_n,   dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wz_n], rax
	xor	r14, r14
	xor	r13, r13
  .kid:
	mov	rax, [wz_n]
	cmp	r13, rax
	jae	.done
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [wz_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wz_sub]
	call	__ast_w_kind
	cmp	eax, CST_TK_IDENT
	jne	.next
	mov	rdi, rbx
	lea	rsi, [wz_sub]
	call	__ast_w_text
	mov	r14, rax
  .next:
	inc	r13
	jmp	.kid
  .done:
	mov	rax, r14
	return
endp

; rax = a leaf's text read as an unsigned decimal, or 0. Spec §8.4's numeric
; literal grammar is `[OPEN]`, so this reads the one spelling the grammar
; already relies on -- `forma arborea 8` (spec §5.4) and `acies<f32, 1024>`
; -- and answers 0 for anything else rather than guessing a radix.
proc __ast_w_num, w, red
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [w]
	mov	rdi, rbx
	mov	rsi, [red]
	call	__ast_w_text
	test	eax, eax
	jz	.none
	mov	rdi, [rbx + AstWalk.names]
	mov	rsi, rax
	call	intern_bytes
	mov	r12, rax
	mov	r13, rdx
	test	r13, r13
	jz	.none
	xor	r14, r14
  .digit:
	movzx	eax, byte [r12]
	sub	eax, 48
	cmp	eax, 0
	jl	.none
	cmp	eax, 9
	jg	.none
	imul	r14, r14, 10
	add	r14, rax
	inc	r12
	dec	r13
	jnz	.digit
	mov	rax, r14
	return
  .none:
	xor	eax, eax
	return
endp

; ---- blocks ----------------------------------------------------------------
; A block owns the CONTIGUOUS range of declarations made inside it:
; `[Block.c, Block.c + Block.aux)` (typed-ast.md section 2.4). Because
; declarations are pushed in source order and this routine records the length
; before and after walking the statements, the range falls out of the walk --
; nothing counts anything twice, and a nested block's range is a sub-range by
; construction.
proc __ast_w_block, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wk_off, dq
		slot wk_cnt, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, [rbx + AstWalk.ast]
	call	ast_decl_count
	inc	rax
	mov	r13, rax		; the first declaration id this block owns
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_inners
	mov	[wk_off], rax
	mov	[wk_cnt], rdx
	mov	rdi, [rbx + AstWalk.ast]
	call	ast_decl_count
	inc	rax
	sub	rax, r13
	mov	r14, rax		; how many it actually owns
	mov	rcx, 65535
	rassert	r14 le rcx		; typed-ast.md section 5, H1: `aux` is
					; sixteen bits and this is where it runs
					; out
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_BLOCK
	mov	rcx, r14
	mov	r8, [wk_off]
	mov	r9, [wk_cnt]
	call	__ast_w_mk
	mov	r12, rax
	test	r14, r14
	jnz	.have
	xor	r13, r13		; empty range: the offset is canonically
					; 0 (ast/verify.inc's __ast_v_block)
  .have:
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r12
	mov	rdx, r13
	xor	rcx, rcx
	call	ast_node_cd
	mov	rax, r12
	return
endp

; ---- simple statements -----------------------------------------------------
proc __ast_w_jump, w, red
	uses	rbx, r12, r13
	locals
		slot wj_sub, sizeof.CstRed
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	r13, AST_REDDE
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TK_KEYWORD
	lea	rcx, [wj_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.have_kind
	mov	rdi, rbx
	lea	rsi, [wj_sub]
	mov	rdx, AST_W_RUMPE
	call	__ast_w_isw
	test	eax, eax
	jnz	.rumpe
	mov	rdi, rbx
	lea	rsi, [wj_sub]
	mov	rdx, AST_W_PERGE
	call	__ast_w_isw
	test	eax, eax
	jz	.have_kind
	mov	r13, AST_PERGE
	jmp	.have_kind
  .rumpe:
	mov	r13, AST_RUMPE
  .have_kind:
	xor	r12, r12
	cmp	r13, AST_REDDE
	jne	.mk			; spec §8.6: only `redde` takes a value
	mov	rdi, rbx
	mov	rsi, [red]
	call	__ast_w_inner
	mov	r12, rax
  .mk:
	mov	rdi, rbx
	mov	rsi, [red]
	mov	rdx, r13
	xor	rcx, rcx
	mov	r8, r12
	xor	r9, r9
	call	__ast_w_mk
	return
endp

proc __ast_w_assign, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wa_sub, sizeof.CstRed
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, -1
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.bad
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [wa_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wa_sub]
	call	__ast_w_node
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.bad
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rax
	lea	rcx, [wa_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wa_sub]
	call	__ast_w_node
	mov	r13, rax
	test	r14, r14
	jz	.bad
	test	r13, r13
	jz	.bad
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_ASSIGN
	xor	rcx, rcx
	mov	r8, r14
	mov	r9, r13
	call	__ast_w_mk
	return
  .bad:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	return
endp

; `BindingStmt ::= ('firma' | 'mutabilis') IDENT [':' Type] ['=' Expr] ';'`.
; The declaration is pushed BEFORE the initialiser is walked, which is source
; order: a lambda in the initialiser declares its parameters after this
; binding, not before it.
proc __ast_w_binding, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wb2_sub, sizeof.CstRed
		slot wb2_ty,  dq
		slot wb2_in,  dq
		slot wb2_dec, dq
		slot wb2_aux, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wb2_aux], 0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TK_KEYWORD
	lea	rcx, [wb2_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_kw
	mov	rdi, rbx
	lea	rsi, [wb2_sub]
	mov	rdx, AST_W_MUTABILIS
	call	__ast_w_isw
	test	eax, eax
	jz	.no_kw
	mov	qword [wb2_aux], AST_BIND_MUT
  .no_kw:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_name
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_BINDING
	mov	rcx, 0
	cmp	qword [wb2_aux], 0
	je	.dflags
	mov	rcx, AST_F_MUTABILIS
  .dflags:
	mov	r8, r13
	call	__ast_w_decl
	mov	[wb2_dec], rax

	mov	qword [wb2_ty], 0
	mov	qword [wb2_in], 0
	mov	r14, -1
  .kid:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.built
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	lea	rcx, [wb2_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wb2_sub]
	call	__ast_w_kind
	cmp	eax, CST_TYPE
	je	.a_type
	mov	rdi, rbx
	lea	rsi, [wb2_sub]
	call	__ast_w_node
	mov	[wb2_in], rax
	jmp	.kid
  .a_type:
	mov	rdi, rbx
	lea	rsi, [wb2_sub]
	call	__ast_w_type
	mov	[wb2_ty], rax
	jmp	.kid
  .built:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_BINDING
	mov	rcx, [wb2_aux]
	mov	r8, [wb2_ty]
	mov	r9, [wb2_in]
	call	__ast_w_mk
	mov	r13, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	xor	rdx, rdx
	mov	rcx, [wb2_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wb2_dec]
	mov	rdx, r13
	call	ast_decl_node
	mov	rax, r13
	return
endp

; `SubStmt ::= 'sub' Path '=' ExprNS (';' | Block)` -- spec §4.5, §8.6
; decision 3. The declared name is the path's LAST segment: that is the
; capability being bound, and a dotted path names it through a module.
proc __ast_w_sub, w, red
	uses	rbx, r12, r13, r14
	locals
		slot ws_sub, sizeof.CstRed
		slot ws_path, dq
		slot ws_val, dq
		slot ws_blk, dq
		slot ws_dec, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [ws_path], 0
	mov	qword [ws_val], 0
	mov	qword [ws_blk], 0

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_PATH
	lea	rcx, [ws_sub]
	call	__ast_w_kidk
	xor	r13, r13
	test	eax, eax
	jz	.no_path
	mov	rdi, rbx
	lea	rsi, [ws_sub]
	call	__ast_w_lastname
	mov	r13, rax
  .no_path:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_SUB
	xor	rcx, rcx
	mov	r8, r13
	call	__ast_w_decl
	mov	[ws_dec], rax

	mov	r14, -1
  .kid:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.built
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	lea	rcx, [ws_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [ws_sub]
	call	__ast_w_kind
	cmp	eax, CST_PATH
	je	.a_path
	cmp	eax, CST_BLOCK
	je	.a_block
	mov	rdi, rbx
	lea	rsi, [ws_sub]
	call	__ast_w_node
	mov	[ws_val], rax
	jmp	.kid
  .a_path:
	mov	rdi, rbx
	lea	rsi, [ws_sub]
	xor	rdx, rdx
	call	__ast_w_pathg
	mov	[ws_path], rax
	jmp	.kid
  .a_block:
	mov	rdi, rbx
	lea	rsi, [ws_sub]
	call	__ast_w_node
	mov	[ws_blk], rax
	jmp	.kid
  .built:
	cmp	qword [ws_path], 0
	jne	.have_path
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[ws_path], rax
  .have_path:
	cmp	qword [ws_val], 0
	jne	.have_val
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[ws_val], rax
  .have_val:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_SUB
	xor	rcx, rcx
	mov	r8, [ws_path]
	mov	r9, [ws_val]
	call	__ast_w_mk
	mov	r13, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	mov	rdx, [ws_blk]
	mov	rcx, [ws_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [ws_dec]
	mov	rdx, r13
	call	ast_decl_node
	mov	rax, r13
	return
endp

; `IfStmt ::= 'si' ExprNS Block ('sin' ExprNS Block)* ['aliter' Block]`.
; The arms become ONE list of (condition, block) PAIRS -- typed-ast.md section
; 2.3's `Si` row and ast/kinds.inc's AST_R_LIST2 role -- so `si`/`sin` are one
; construct in the tree and not a chain to walk.
proc __ast_w_if, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wf2_sub, sizeof.CstRed
		slot wf2_s2,  sizeof.CstRed
		slot wf2_mark, dq
		slot wf2_cond, dq
		slot wf2_alit, dq
		slot wf2_npair, dq
		slot wf2_blk,  dq
		slot wf2_off,  dq
		slot wf2_cnt,  dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, [rbx + AstWalk.ast]
	call	ast_list_mark
	mov	[wf2_mark], rax
	mov	qword [wf2_cond], 0
	mov	qword [wf2_alit], 0
	mov	qword [wf2_npair], 0
	mov	r14, -1
  .kid:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.built
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	lea	rcx, [wf2_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wf2_sub]
	call	__ast_w_kind
	cmp	eax, CST_ALITER_ARM
	je	.aliter
	cmp	eax, CST_SIN_ARM
	je	.sin
	cmp	eax, CST_BLOCK
	je	.block
	mov	rdi, rbx
	lea	rsi, [wf2_sub]
	call	__ast_w_node
	mov	[wf2_cond], rax
	jmp	.kid
  .block:
	mov	rdi, rbx
	lea	rsi, [wf2_sub]
	call	__ast_w_node
	mov	r13, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wf2_cond]
	call	ast_list_push
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	call	ast_list_push
	inc	qword [wf2_npair]
	mov	qword [wf2_cond], 0
	jmp	.kid
  .sin:
	mov	rdi, rbx
	lea	rsi, [wf2_sub]
	call	__ast_w_inner		; the condition
	mov	[wf2_cond], rax
	mov	rdi, rbx
	lea	rsi, [wf2_sub]
	mov	rdx, CST_BLOCK
	lea	rcx, [wf2_s2]
	call	__ast_w_kidk
	test	eax, eax
	jz	.kid
	mov	rdi, rbx
	lea	rsi, [wf2_s2]
	call	__ast_w_node
	mov	[wf2_blk], rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wf2_cond]
	call	ast_list_push
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wf2_blk]
	call	ast_list_push
	inc	qword [wf2_npair]
	mov	qword [wf2_cond], 0
	jmp	.kid
  .aliter:
	mov	rdi, rbx
	lea	rsi, [wf2_sub]
	call	__ast_w_inner
	mov	[wf2_alit], rax
	jmp	.kid
  .built:
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wf2_mark]
	call	ast_list_emit
	mov	[wf2_off], rax
	mov	[wf2_cnt], rdx
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_SI
	xor	rcx, rcx
	mov	r8, [wf2_off]
	mov	r9, [wf2_npair]
	call	__ast_w_mk
	mov	r13, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	mov	rdx, [wf2_alit]
	xor	rcx, rcx
	call	ast_node_cd
	mov	rax, r13
	return
endp

; `WhileStmt ::= 'dum' ExprNS ['terminus' ExprNS] Block`. The `terminus` bound
; is optional in the grammar so that its ABSENCE is a fact the `certus`
; profile can reject (spec §8.5, §8.6's own open list); it is a slot here for
; the same reason.
proc __ast_w_while, w, red
	uses	rbx, r12, r13, r14
	locals
		slot ww_sub, sizeof.CstRed
		slot ww_c,   dq
		slot ww_t,   dq
		slot ww_b,   dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [ww_c], 0
	mov	qword [ww_t], 0
	mov	qword [ww_b], 0
	mov	r14, -1
  .kid:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.built
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	lea	rcx, [ww_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [ww_sub]
	call	__ast_w_kind
	cmp	eax, CST_TERMINUS_CLAUSE
	je	.term
	cmp	eax, CST_BLOCK
	je	.body
	mov	rdi, rbx
	lea	rsi, [ww_sub]
	call	__ast_w_node
	mov	[ww_c], rax
	jmp	.kid
  .term:
	mov	rdi, rbx
	lea	rsi, [ww_sub]
	call	__ast_w_inner
	mov	[ww_t], rax
	jmp	.kid
  .body:
	mov	rdi, rbx
	lea	rsi, [ww_sub]
	call	__ast_w_node
	mov	[ww_b], rax
	jmp	.kid
  .built:
	cmp	qword [ww_c], 0
	jne	.have_c
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[ww_c], rax
  .have_c:
	cmp	qword [ww_b], 0
	jne	.have_b
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[ww_b], rax
  .have_b:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_DUM
	xor	rcx, rcx
	mov	r8, [ww_c]
	mov	r9, [ww_t]
	call	__ast_w_mk
	mov	r13, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	mov	rdx, [ww_b]
	xor	rcx, rcx
	call	ast_node_cd
	mov	rax, r13
	return
endp

; `ContraheClause ::= 'contrahe' IDENT ':' ArithOp` (spec §8.5). The
; accumulator gets its own declaration: spec §5.4 forbids reading it inside
; the body, and typed-ast.md section 2.7 gives it type `red.F`, which admits
; no read (ssa-ir.md 2.6) -- both are rules ABOUT a declaration, so there has
; to be one.
proc __ast_w_contrahe, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wr_sub, sizeof.CstRed
		slot wr_n,   dq
		slot wr_dec, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_name
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_ACCUM
	xor	rcx, rcx
	mov	r8, r13
	call	__ast_w_decl
	mov	[wr_dec], rax

	; The operator is the LAST punctuation that spells one -- `:` comes
	; first and spells nothing.
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wr_n], rax
	xor	r14, r14
	xor	r13, r13
  .scan:
	mov	rax, [wr_n]
	cmp	r13, rax
	jae	.scanned
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [wr_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wr_sub]
	call	__ast_w_op
	test	rax, rax
	jz	.scan_next
	mov	r14, rax
  .scan_next:
	inc	r13
	jmp	.scan
  .scanned:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_name
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_CONTRAHE
	mov	rcx, r14
	mov	r8, r13
	xor	r9, r9
	call	__ast_w_mk
	mov	r13, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	xor	rdx, rdx
	mov	rcx, [wr_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wr_dec]
	mov	rdx, r13
	call	ast_decl_node
	mov	rax, r13
	return
endp

; `ForStmt ::= ('per' | 'quisque') IDENT 'in' ExprNS ('contrahe' ...)*
;              ['forma' IDENT [INT]] Block`.
;
; THE `forma` WIDTH IS THE POINT OF `ForHead.d`. typed-ast.md section 2.7 says
; spec §8.6 admits no width and so "the tree reserves nothing"; that is stale.
; §8.6 now reads `['forma' IDENT [INT]]` -- amended, by that document's own
; asking -- and ssa-ir.md 2.6 lowers `redinit f32 fadd arborea 8`, so the
; width has to survive to Stage 3 or the reduction shape spec §5.4 calls
; semantics could not be emitted.
proc __ast_w_for, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wy_sub,  sizeof.CstRed
		slot wy_s2,   sizeof.CstRed
		slot wy_mark, dq
		slot wy_rng,  dq
		slot wy_blk,  dq
		slot wy_dec,  dq
		slot wy_fid,  dq
		slot wy_fw,   dq
		slot wy_off,  dq
		slot wy_cnt,  dq
		slot wy_aux,  dq
		slot wy_head, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wy_rng], 0
	mov	qword [wy_blk], 0
	mov	qword [wy_fid], 0
	mov	qword [wy_fw], 0
	mov	qword [wy_aux], 0
	mov	qword [wy_head], 0

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TK_KEYWORD
	lea	rcx, [wy_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_kw
	mov	rdi, rbx
	lea	rsi, [wy_sub]
	mov	rdx, AST_W_QUISQUE
	call	__ast_w_isw
	test	eax, eax
	jz	.no_kw
	mov	qword [wy_aux], AST_FOR_QUISQUE
  .no_kw:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_name		; the loop variable
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_LOOPVAR
	xor	rcx, rcx
	mov	r8, r13
	call	__ast_w_decl
	mov	[wy_dec], rax

	mov	rdi, [rbx + AstWalk.ast]
	call	ast_list_mark
	mov	[wy_mark], rax
	mov	r14, -1
  .kid:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.built
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	lea	rcx, [wy_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wy_sub]
	call	__ast_w_kind
	cmp	eax, CST_CONTRAHE_CLAUSE
	je	.contrahe
	cmp	eax, CST_FORMA_CLAUSE
	je	.forma
	cmp	eax, CST_BLOCK
	je	.body
	cmp	qword [wy_rng], 0
	jne	.kid
	mov	rdi, rbx
	lea	rsi, [wy_sub]
	call	__ast_w_node
	mov	[wy_rng], rax
	jmp	.kid
  .contrahe:
	mov	rdi, rbx
	lea	rsi, [wy_sub]
	call	__ast_w_contrahe
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, rax
	call	ast_list_push
	jmp	.kid
  .forma:
	mov	rdi, rbx
	lea	rsi, [wy_sub]
	call	__ast_w_name
	mov	[wy_fid], rax
	mov	rdi, rbx
	lea	rsi, [wy_sub]
	mov	rdx, CST_TK_NUMBER
	lea	rcx, [wy_s2]
	call	__ast_w_kidk
	test	eax, eax
	jz	.kid
	mov	rdi, rbx
	lea	rsi, [wy_s2]
	call	__ast_w_num
	mov	[wy_fw], rax
	jmp	.kid
  .body:
	; The head is finished before the body, so its node index is smaller.
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wy_mark]
	call	ast_list_emit
	mov	[wy_off], rax
	mov	[wy_cnt], rdx
	mov	rax, [wy_cnt]
	or	rax, [wy_fid]
	jz	.no_head		; no `contrahe`, no `forma`: no head
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_FORHEAD
	xor	rcx, rcx
	mov	r8, [wy_off]
	mov	r9, [wy_cnt]
	call	__ast_w_mk
	mov	[wy_head], rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wy_head]
	mov	rdx, [wy_fid]
	mov	rcx, [wy_fw]
	call	ast_node_cd
  .no_head:
	mov	rdi, rbx
	lea	rsi, [wy_sub]
	call	__ast_w_node
	mov	[wy_blk], rax
	jmp	.kid
  .built:
	cmp	qword [wy_blk], 0
	jne	.have_b
	; No block at all -- a recovery shape. The staged `contrahe` nodes are
	; still on the stack and have to come off it.
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wy_mark]
	call	ast_list_emit
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wy_blk], rax
  .have_b:
	cmp	qword [wy_rng], 0
	jne	.have_r
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wy_rng], rax
  .have_r:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_FOR
	mov	rcx, [wy_aux]
	mov	r8, [wy_rng]
	mov	r9, [wy_blk]
	call	__ast_w_mk
	mov	r13, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	mov	rdx, [wy_head]
	mov	rcx, [wy_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wy_dec]
	mov	rdx, r13
	call	ast_decl_node
	mov	rax, r13
	return
endp

; `MatchStmt ::= 'discerne' ExprNS '{' ('casus' Pattern Block)* ['aliter'
; Block] '}'`. Exhaustiveness is Stage 2's and needs an enumeration the
; language does not declare yet (spec §8.6's open list); the tree only has to
; carry the arms in written order.
proc __ast_w_match, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wm2_sub, sizeof.CstRed
		slot wm2_mark, dq
		slot wm2_scr, dq
		slot wm2_alit, dq
		slot wm2_off, dq
		slot wm2_cnt, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wm2_scr], 0
	mov	qword [wm2_alit], 0
	mov	rdi, [rbx + AstWalk.ast]
	call	ast_list_mark
	mov	[wm2_mark], rax
	mov	r14, -1
  .kid:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.built
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	lea	rcx, [wm2_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wm2_sub]
	call	__ast_w_kind
	cmp	eax, CST_CASUS_ARM
	je	.arm
	cmp	eax, CST_ALITER_ARM
	je	.aliter
	cmp	qword [wm2_scr], 0
	jne	.kid
	mov	rdi, rbx
	lea	rsi, [wm2_sub]
	call	__ast_w_node
	mov	[wm2_scr], rax
	jmp	.kid
  .arm:
	mov	rdi, rbx
	lea	rsi, [wm2_sub]
	call	__ast_w_node
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, rax
	call	ast_list_push
	jmp	.kid
  .aliter:
	mov	rdi, rbx
	lea	rsi, [wm2_sub]
	call	__ast_w_inner
	mov	[wm2_alit], rax
	jmp	.kid
  .built:
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wm2_mark]
	call	ast_list_emit
	mov	[wm2_off], rax
	mov	[wm2_cnt], rdx
	cmp	qword [wm2_scr], 0
	jne	.have_s
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wm2_scr], rax
  .have_s:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_DISCERNE
	xor	rcx, rcx
	mov	r8, [wm2_scr]
	mov	r9, [wm2_off]
	call	__ast_w_mk
	mov	r13, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	mov	rdx, [wm2_cnt]
	mov	rcx, [wm2_alit]
	call	ast_node_cd
	mov	rax, r13
	return
endp

proc __ast_w_casus, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wn2_sub, sizeof.CstRed
		slot wn2_pat, dq
		slot wn2_blk, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wn2_pat], 0
	mov	qword [wn2_blk], 0
	mov	r14, -1
  .kid:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.built
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	lea	rcx, [wn2_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wn2_sub]
	call	__ast_w_kind
	cmp	eax, CST_BLOCK
	je	.blk
	mov	rdi, rbx
	lea	rsi, [wn2_sub]
	call	__ast_w_node
	mov	[wn2_pat], rax
	jmp	.kid
  .blk:
	mov	rdi, rbx
	lea	rsi, [wn2_sub]
	call	__ast_w_node
	mov	[wn2_blk], rax
	jmp	.kid
  .built:
	cmp	qword [wn2_pat], 0
	jne	.have_p
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wn2_pat], rax
  .have_p:
	cmp	qword [wn2_blk], 0
	jne	.have_b
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wn2_blk], rax
  .have_b:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_CASUS
	xor	rcx, rcx
	mov	r8, [wn2_pat]
	mov	r9, [wn2_blk]
	call	__ast_w_mk
	return
endp

; `Pattern ::= Literal | Path` -- constructor patterns are `[OPEN]` in spec
; §8.6 and add no kind here (typed-ast.md section 2.3: "Kinds absent from spec
; §8.6 are absent here").
proc __ast_w_pattern, w, red
	uses	rbx, r12
	locals
		slot wt2_sub, sizeof.CstRed
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_inner
	test	rax, rax
	jnz	.done
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TK_NUMBER
	lea	rcx, [wt2_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.str
	mov	rdi, rbx
	lea	rsi, [wt2_sub]
	mov	rdx, AST_LIT_INT
	call	__ast_w_leaflit
	return
  .str:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TK_STRING
	lea	rcx, [wt2_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.bad
	mov	rdi, rbx
	lea	rsi, [wt2_sub]
	mov	rdx, AST_LIT_STRING
	call	__ast_w_leaflit
	return
  .bad:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
  .done:
	return
endp

; ---- signatures ------------------------------------------------------------
; Also serves a `Lambda`, whose `PARAM_LIST` and result `TYPE` hang directly
; off it (spec §8.6: `Lambda ::= 'functio' ParamList ['->' Type] Block`, with
; no `poscit` -- §4.1 rule 5 infers its row, and the row lives in its TYPE).
; The `Sig` then carries the lambda's own span rather than a narrower one,
; which is the honest extent: there is no CST node covering just the header.
proc __ast_w_sig, w, red, outg
	uses	rbx, r12, r13
	locals
		slot wsg_sub, sizeof.CstRed
		slot wsg_g,   dq
		slot wsg_res, dq
		slot wsg_row, dq
		slot wsg_off, dq
		slot wsg_cnt, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wsg_g], 0
	mov	qword [wsg_res], 0
	mov	qword [wsg_row], 0
	mov	qword [wsg_off], 0
	mov	qword [wsg_cnt], 0

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_GENERIC_PARAMS
	lea	rcx, [wsg_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_g
	mov	rdi, rbx
	lea	rsi, [wsg_sub]
	call	__ast_w_node
	mov	[wsg_g], rax
  .no_g:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_PARAM_LIST
	lea	rcx, [wsg_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_p
	mov	rdi, rbx
	lea	rsi, [wsg_sub]
	mov	rdx, CST_PARAM
	call	__ast_w_list
	mov	[wsg_off], rax
	mov	[wsg_cnt], rdx
  .no_p:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TYPE
	lea	rcx, [wsg_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_r
	mov	rdi, rbx
	lea	rsi, [wsg_sub]
	call	__ast_w_type
	mov	[wsg_res], rax
  .no_r:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_DECL_ROW
	lea	rcx, [wsg_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_row
	mov	rdi, rbx
	lea	rsi, [wsg_sub]
	call	__ast_w_node
	mov	[wsg_row], rax
  .no_row:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_SIG
	xor	rcx, rcx
	mov	r8, [wsg_off]
	mov	r9, [wsg_cnt]
	call	__ast_w_mk
	mov	r13, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	mov	rdx, [wsg_res]
	mov	rcx, [wsg_row]
	call	ast_node_cd
	mov	rdi, [outg]
	mov	rax, [wsg_g]
	mov	[rdi], rax
	mov	rax, r13
	return
endp

; `FunctionDecl`, an interface `Member`, and an `externus` member all become
; `AST_FN`; they differ only in the declaration kind and in whether a body is
; admitted. The walk's `parent` is the function's own declaration while its
; parameters and body are built, which is what makes `AstDecl.parent` a real
; nesting rather than a flat list.
proc __ast_w_fn, w, red, dkind, dflags
	uses	rbx, r12, r13, r14
	locals
		slot wfn_sub, sizeof.CstRed
		slot wfn_g,   dq
		slot wfn_sig, dq
		slot wfn_bod, dq
		slot wfn_dec, dq
		slot wfn_sv,  dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wfn_g], 0
	mov	qword [wfn_bod], 0

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_SIGNATURE
	lea	rcx, [wfn_sub]
	call	__ast_w_kidk
	test	eax, eax
	jnz	.have_sig
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	return
  .have_sig:
	mov	rdi, rbx
	lea	rsi, [wfn_sub]
	call	__ast_w_name
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [dkind]
	mov	rcx, [dflags]
	mov	r8, r13
	call	__ast_w_decl
	mov	[wfn_dec], rax
	mov	eax, [rbx + AstWalk.parent]
	mov	[wfn_sv], rax
	mov	eax, [wfn_dec]
	mov	[rbx + AstWalk.parent], eax

	mov	rdi, rbx
	lea	rsi, [wfn_sub]
	lea	rdx, [wfn_g]
	call	__ast_w_sig
	mov	[wfn_sig], rax

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_BLOCK
	lea	rcx, [wfn_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_body
	mov	rdi, rbx
	lea	rsi, [wfn_sub]
	call	__ast_w_node
	mov	[wfn_bod], rax
  .no_body:
	mov	eax, [wfn_sv]
	mov	[rbx + AstWalk.parent], eax

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_FN
	xor	rcx, rcx
	mov	r8, [wfn_sig]
	mov	r9, [wfn_bod]
	call	__ast_w_mk
	mov	r14, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r14
	mov	rdx, [wfn_g]
	mov	rcx, [wfn_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wfn_dec]
	mov	rdx, r14
	call	ast_decl_node
	mov	rax, r14
	return
endp

proc __ast_w_lambda, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wlm_sub, sizeof.CstRed
		slot wlm_g,   dq
		slot wlm_sig, dq
		slot wlm_bod, dq
		slot wlm_dec, dq
		slot wlm_sv,  dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wlm_g], 0
	mov	qword [wlm_bod], 0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_LAMBDA
	xor	rcx, rcx
	xor	r8, r8			; a lambda introduces no NAME
	call	__ast_w_decl
	mov	[wlm_dec], rax
	mov	eax, [rbx + AstWalk.parent]
	mov	[wlm_sv], rax
	mov	eax, [wlm_dec]
	mov	[rbx + AstWalk.parent], eax

	mov	rdi, rbx
	mov	rsi, r12
	lea	rdx, [wlm_g]
	call	__ast_w_sig
	mov	[wlm_sig], rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_BLOCK
	lea	rcx, [wlm_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_body
	mov	rdi, rbx
	lea	rsi, [wlm_sub]
	call	__ast_w_node
	mov	[wlm_bod], rax
  .no_body:
	mov	eax, [wlm_sv]
	mov	[rbx + AstWalk.parent], eax
	cmp	qword [wlm_bod], 0
	jne	.have_b
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wlm_bod], rax
  .have_b:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_LAMBDA
	xor	rcx, rcx
	mov	r8, [wlm_sig]
	mov	r9, [wlm_bod]
	call	__ast_w_mk
	mov	r14, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r14
	xor	rdx, rdx
	mov	rcx, [wlm_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wlm_dec]
	mov	rdx, r14
	call	ast_decl_node
	mov	rax, r14
	return
endp

; ---- a declaration with a name, generics and a list of members -------------
; `structura`, an `interfacies` declaration body, and `potestas` are the same
; shape: name, optional generics, a list of one child kind. One routine, with
; the differences as arguments.
proc __ast_w_named, w, red, akind, dkind, listkind, listred
	uses	rbx, r12, r13, r14
	locals
		slot wnd_sub, sizeof.CstRed
		slot wnd_g,   dq
		slot wnd_dec, dq
		slot wnd_sv,  dq
		slot wnd_off, dq
		slot wnd_cnt, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wnd_g], 0
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_name
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [dkind]
	xor	rcx, rcx
	mov	r8, r13
	call	__ast_w_decl
	mov	[wnd_dec], rax
	mov	eax, [rbx + AstWalk.parent]
	mov	[wnd_sv], rax
	mov	eax, [wnd_dec]
	mov	[rbx + AstWalk.parent], eax

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_GENERIC_PARAMS
	lea	rcx, [wnd_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_g
	mov	rdi, rbx
	lea	rsi, [wnd_sub]
	call	__ast_w_node
	mov	[wnd_g], rax
  .no_g:
	mov	rdi, rbx
	mov	rsi, [listred]
	mov	rdx, [listkind]
	call	__ast_w_list
	mov	[wnd_off], rax
	mov	[wnd_cnt], rdx
	mov	eax, [wnd_sv]
	mov	[rbx + AstWalk.parent], eax

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [akind]
	xor	rcx, rcx
	mov	r8, [wnd_off]
	mov	r9, [wnd_cnt]
	call	__ast_w_mk
	mov	r14, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r14
	mov	rdx, [wnd_g]
	mov	rcx, [wnd_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wnd_dec]
	mov	rdx, r14
	call	ast_decl_node
	mov	rax, r14
	return
endp

; `Field` and `Param` -- `IDENT ':' Type`, type REQUIRED (spec §8.6). The
; declaration is pushed before the type is walked, so a generic argument that
; is itself a declaration would land after it, in source order.
proc __ast_w_namety, w, red, akind, dkind, required
	uses	rbx, r12, r13, r14
	locals
		slot wpt_sub, sizeof.CstRed
		slot wpt_dec, dq
		slot wpt_ty,  dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wpt_ty], 0
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_name
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [dkind]
	xor	rcx, rcx
	mov	r8, r13
	call	__ast_w_decl
	mov	[wpt_dec], rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TYPE
	lea	rcx, [wpt_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_ty
	mov	rdi, rbx
	lea	rsi, [wpt_sub]
	call	__ast_w_type
	mov	[wpt_ty], rax
  .no_ty:
	cmp	qword [required], 0
	je	.mk
	cmp	qword [wpt_ty], 0
	jne	.mk
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err		; the slot is AST_R_NODEQ; a missing
	mov	[wpt_ty], rax		; type is an `Error`, never a 0
  .mk:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_name
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [akind]
	xor	rcx, rcx
	mov	r8, r13
	mov	r9, [wpt_ty]
	call	__ast_w_mk
	mov	r14, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r14
	xor	rdx, rdx
	mov	rcx, [wpt_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wpt_dec]
	mov	rdx, r14
	call	ast_decl_node
	mov	rax, r14
	return
endp

; `TypeDecl ::= 'typus' IDENT [GenericParams] '=' Type ';'`. Whether `typus`
; makes an alias or a new type is `[OPEN]` (typed-ast.md section 2.5); the
; tree records what was written either way.
proc __ast_w_typus, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wtd_sub, sizeof.CstRed
		slot wtd_g,   dq
		slot wtd_ty,  dq
		slot wtd_dec, dq
		slot wtd_sv,  dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wtd_g], 0
	mov	qword [wtd_ty], 0
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_name
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_TYPUS
	xor	rcx, rcx
	mov	r8, r13
	call	__ast_w_decl
	mov	[wtd_dec], rax
	mov	eax, [rbx + AstWalk.parent]
	mov	[wtd_sv], rax
	mov	eax, [wtd_dec]
	mov	[rbx + AstWalk.parent], eax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_GENERIC_PARAMS
	lea	rcx, [wtd_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_g
	mov	rdi, rbx
	lea	rsi, [wtd_sub]
	call	__ast_w_node
	mov	[wtd_g], rax
  .no_g:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TYPE
	lea	rcx, [wtd_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_ty
	mov	rdi, rbx
	lea	rsi, [wtd_sub]
	call	__ast_w_type
	mov	[wtd_ty], rax
  .no_ty:
	mov	eax, [wtd_sv]
	mov	[rbx + AstWalk.parent], eax
	cmp	qword [wtd_ty], 0
	jne	.mk
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wtd_ty], rax
  .mk:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_TYPUS
	xor	rcx, rcx
	mov	r8, [wtd_ty]
	xor	r9, r9
	call	__ast_w_mk
	mov	r14, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r14
	mov	rdx, [wtd_g]
	mov	rcx, [wtd_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wtd_dec]
	mov	rdx, r14
	call	ast_decl_node
	mov	rax, r14
	return
endp

; `InterfaceDecl` is TWO constructs sharing a keyword (spec §8.6 decision 4):
; `interfacies P { ... }` declares, `interfacies P in T poscit R { ... }`
; implements. One token after the head decides, and the tree keeps them apart
; as `AST_IFACE` and `AST_IMPL`.
proc __ast_w_iface, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wif_sub,  sizeof.CstRed
		slot wif_head, sizeof.CstRed
		slot wif_body, sizeof.CstRed
		slot wif_dec,  dq
		slot wif_sv,   dq
		slot wif_p,    dq
		slot wif_tgt,  dq
		slot wif_row,  dq
		slot wif_hd,   dq
		slot wif_off,  dq
		slot wif_cnt,  dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_IMPL_HEAD
	lea	rcx, [wif_head]
	call	__ast_w_kidk
	test	eax, eax
	jz	.declaration

	; ---- the implementation form ----
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_PATH
	lea	rcx, [wif_sub]
	call	__ast_w_kidk
	xor	r13, r13
	test	eax, eax
	jz	.impl_noname
	mov	rdi, rbx
	lea	rsi, [wif_sub]
	call	__ast_w_lastname
	mov	r13, rax
  .impl_noname:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_IMPL
	xor	rcx, rcx
	mov	r8, r13
	call	__ast_w_decl
	mov	[wif_dec], rax
	mov	eax, [rbx + AstWalk.parent]
	mov	[wif_sv], rax
	mov	eax, [wif_dec]
	mov	[rbx + AstWalk.parent], eax

	mov	qword [wif_p], 0
	mov	qword [wif_tgt], 0
	mov	qword [wif_row], 0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_PATH
	lea	rcx, [wif_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.impl_nopath
	mov	rdi, rbx
	lea	rsi, [wif_sub]
	xor	rdx, rdx
	call	__ast_w_pathg
	mov	[wif_p], rax
  .impl_nopath:
	mov	rdi, rbx
	lea	rsi, [wif_head]
	mov	rdx, CST_TYPE
	lea	rcx, [wif_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.impl_notgt
	mov	rdi, rbx
	lea	rsi, [wif_sub]
	call	__ast_w_type
	mov	[wif_tgt], rax
  .impl_notgt:
	mov	rdi, rbx
	lea	rsi, [wif_head]
	mov	rdx, CST_DECL_ROW
	lea	rcx, [wif_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.impl_norow
	mov	rdi, rbx
	lea	rsi, [wif_sub]
	call	__ast_w_node
	mov	[wif_row], rax
  .impl_norow:
	cmp	qword [wif_p], 0
	jne	.impl_hp
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wif_p], rax
  .impl_hp:
	cmp	qword [wif_tgt], 0
	jne	.impl_ht
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wif_tgt], rax
  .impl_ht:
	mov	rdi, rbx
	lea	rsi, [wif_head]
	mov	rdx, AST_IMPLHEAD
	xor	rcx, rcx
	mov	r8, [wif_p]
	mov	r9, [wif_tgt]
	call	__ast_w_mk
	mov	[wif_hd], rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wif_hd]
	mov	rdx, [wif_row]
	xor	rcx, rcx
	call	ast_node_cd

	mov	qword [wif_off], 0
	mov	qword [wif_cnt], 0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_IMPL_BODY
	lea	rcx, [wif_body]
	call	__ast_w_kidk
	test	eax, eax
	jz	.impl_nobody
	mov	rdi, rbx
	lea	rsi, [wif_body]
	mov	rdx, CST_FUNCTION_DECL
	call	__ast_w_list
	mov	[wif_off], rax
	mov	[wif_cnt], rdx
  .impl_nobody:
	mov	eax, [wif_sv]
	mov	[rbx + AstWalk.parent], eax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_IMPL
	xor	rcx, rcx
	mov	r8, [wif_hd]
	mov	r9, [wif_off]
	call	__ast_w_mk
	mov	r14, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r14
	mov	rdx, [wif_cnt]
	mov	rcx, [wif_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wif_dec]
	mov	rdx, r14
	call	ast_decl_node
	mov	rax, r14
	return

  .declaration:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_INTERFACE_BODY
	lea	rcx, [wif_body]
	call	__ast_w_kidk
	test	eax, eax
	jnz	.decl_body
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	return
  .decl_body:
	; The NAME is the head `Path`'s last segment, not a bare IDENT --
	; `interfacies norma.legibilis` is admitted (spec §8.6, `Path`).
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_PATH
	lea	rcx, [wif_sub]
	call	__ast_w_kidk
	xor	r13, r13
	test	eax, eax
	jz	.decl_noname
	mov	rdi, rbx
	lea	rsi, [wif_sub]
	call	__ast_w_lastname
	mov	r13, rax
  .decl_noname:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_IFACE
	xor	rcx, rcx
	mov	r8, r13
	call	__ast_w_decl
	mov	[wif_dec], rax
	mov	eax, [rbx + AstWalk.parent]
	mov	[wif_sv], rax
	mov	eax, [wif_dec]
	mov	[rbx + AstWalk.parent], eax
	mov	qword [wif_p], 0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_GENERIC_PARAMS
	lea	rcx, [wif_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.decl_nog
	mov	rdi, rbx
	lea	rsi, [wif_sub]
	call	__ast_w_node
	mov	[wif_p], rax
  .decl_nog:
	mov	rdi, rbx
	lea	rsi, [wif_body]
	mov	rdx, CST_MEMBER
	call	__ast_w_list
	mov	[wif_off], rax
	mov	[wif_cnt], rdx
	mov	eax, [wif_sv]
	mov	[rbx + AstWalk.parent], eax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_IFACE
	xor	rcx, rcx
	mov	r8, [wif_off]
	mov	r9, [wif_cnt]
	call	__ast_w_mk
	mov	r14, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r14
	mov	rdx, [wif_p]
	mov	rcx, [wif_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wif_dec]
	mov	rdx, r14
	call	ast_decl_node
	mov	rax, r14
	return
endp

; `ExternusBlock ::= 'externus' '(' STRING ',' 'abi' ':' IDENT ')' '{' ... '}'`
; (spec §5.3). `c` is the library string's interner id; `aux` is the ABI as a
; CLOSED ENUMERATION (`AST_ABI_*`, ast/kinds.inc), which is what §5.3 now
; requires and NOT what typed-ast.md section 2.3 says ("abi id", meaning the
; identifier's interner id).
;
; THE OLD ASSIGNMENT COULD NOT FIT. An interner id is unbounded and `aux` is
; sixteen bits, so this routine used to `rassert` on an id past 65535 rather
; than truncate -- recorded as finding 10 in ast/ast.inc and proved by
; tests/unit/ast_from_cst_abi_overflow.asm, which is retired with this change.
; §5.3 closed the ABI set, so the value is now 1, 2, 3 or 0.
;
; AN UNRECOGNISED SPELLING IS NOT AN ERROR HERE. This walker has no
; diagnostic sink (`AstWalk`, above) and resolves nothing: it writes
; AST_ABI_UNKNOWN and continues, and `EXS-E0309` is Stage 2's, raised at this
; node's span. Nothing keeps the offending spelling -- diag/render.inc quotes
; the source line, which is the spelling.
proc __ast_w_externus, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wex_sub,  sizeof.CstRed
		slot wex_head, sizeof.CstRed
		slot wex_lib,  dq
		slot wex_abi,  dq
		slot wex_dec,  dq
		slot wex_sv,   dq
		slot wex_off,  dq
		slot wex_cnt,  dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wex_lib], 0
	mov	qword [wex_abi], 0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_EXTERNUS_HEAD
	lea	rcx, [wex_head]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_head
	mov	rdi, rbx
	lea	rsi, [wex_head]
	mov	rdx, CST_TK_STRING
	lea	rcx, [wex_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_lib
	mov	rdi, rbx
	lea	rsi, [wex_sub]
	call	__ast_w_text
	mov	[wex_lib], rax
  .no_lib:
	mov	rdi, rbx
	lea	rsi, [wex_head]
	call	__ast_w_lastname	; `abi: IDENT` -- the last identifier
	test	rax, rax
	jz	.no_head		; no head, no identifier: leave it 0
	; The spelling, not its id: `ast_name_id` over `ast_abi_names` answers
	; the 1-based pool index (whose entry 1 is `ignotus`) or 0 on a miss,
	; and `index - 1` is the AST_ABI_* value in both cases.
	mov	rdi, [rbx + AstWalk.names]
	mov	rsi, rax
	call	intern_bytes		; rax = bytes, rdx = length
	mov	rcx, rdx
	lea	rdi, [ast_abi_names]
	mov	rsi, AST_ABI_NAMES_LEN
	mov	rdx, rax
	call	ast_name_id
	test	rax, rax
	jz	.abi_done		; unrecognised -- AST_ABI_UNKNOWN
	dec	rax
  .abi_done:
	mov	[wex_abi], rax
  .no_head:
	mov	rax, [wex_abi]
	mov	rcx, AST_ABI_COUNT
	rassert	rax le rcx
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_EXTERNUS
	mov	rcx, AST_F_EXTERNUS
	mov	r8, [wex_lib]
	call	__ast_w_decl
	mov	[wex_dec], rax
	mov	eax, [rbx + AstWalk.parent]
	mov	[wex_sv], rax
	mov	eax, [wex_dec]
	mov	[rbx + AstWalk.parent], eax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_EXTERNUS_MEMBER
	call	__ast_w_list
	mov	[wex_off], rax
	mov	[wex_cnt], rdx
	mov	eax, [wex_sv]
	mov	[rbx + AstWalk.parent], eax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_EXTERNUS
	mov	rcx, [wex_abi]
	mov	r8, [wex_off]
	mov	r9, [wex_cnt]
	call	__ast_w_mk
	mov	r14, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r14
	mov	rdx, [wex_lib]
	mov	rcx, [wex_dec]
	call	ast_node_cd
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wex_dec]
	mov	rdx, r14
	call	ast_decl_node
	mov	rax, r14
	return
endp

; `RowItem ::= Path | 'sicut' IDENT` (spec §8.6). The two shapes share one
; slot, told apart by `aux` -- which is why ast/kinds.inc gives `RowItem.a`
; its own AST_R_ROWI role and ast/verify.inc special-cases it.
proc __ast_w_rowitem, w, red
	uses	rbx, r12, r13
	locals
		slot wri_sub, sizeof.CstRed
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_PATH
	lea	rcx, [wri_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.sicut
	mov	rdi, rbx
	lea	rsi, [wri_sub]
	xor	rdx, rdx
	call	__ast_w_pathg
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_ROWITEM
	xor	rcx, rcx
	mov	r8, r13
	xor	r9, r9
	call	__ast_w_mk
	return
  .sicut:
	; `sicut f` names a PARAMETER; spec §4.2 replaces it with the row in
	; that argument's TYPE, positionally, so `d` becomes an ordinal and
	; never a name (typed-ast.md section 2.6). Stage 1 records the name.
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_name
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_ROWITEM
	mov	rcx, AST_ROW_SICUT
	mov	r8, r13
	xor	r9, r9
	call	__ast_w_mk
	return
endp

; `TypeArg ::= Type | INT` (spec §8.6): a value argument is a `Lit`, which is
; how `acies<f32, 1024>` puts a lane count in the type (spec §5.4).
proc __ast_w_typearg, w, red
	uses	rbx, r12
	locals
		slot wta_sub, sizeof.CstRed
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TYPE
	lea	rcx, [wta_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.value
	mov	rdi, rbx
	lea	rsi, [wta_sub]
	call	__ast_w_type
	return
  .value:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TK_NUMBER
	lea	rcx, [wta_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.bad
	; The leaf keeps its HONEST class (`__ast_w_numclass`), INT or FLOAT:
	; a float in a value argument is not refused here because the fold has
	; no diagnostic sink -- the checker's `chk_ty_litval` answers "not a
	; value" for every non-INT literal, and `__chk_ty_genlit` raises the
	; `EXS-E0304` it already raises for any other non-literal value
	; argument, at the `GenericArgs` node. Folding it as INT to keep the
	; old shape would have made `1.5` parse as the integer 1 -- a silent
	; wrong answer with no diagnostic, strictly worse.
	mov	rdi, rbx
	lea	rsi, [wta_sub]
	call	__ast_w_numclass
	mov	rdx, rax
	mov	rdi, rbx
	lea	rsi, [wta_sub]
	call	__ast_w_leaflit
	return
  .bad:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	return
endp

; `dyn Path [GenericArgs] [TypeRow]` (spec §4.4, §8.6).
proc __ast_w_dyn, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wdy_sub, sizeof.CstRed
		slot wdy_g,   dq
		slot wdy_p,   dq
		slot wdy_row, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wdy_g], 0
	mov	qword [wdy_p], 0
	mov	qword [wdy_row], 0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_GENERIC_ARGS
	lea	rcx, [wdy_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_g
	mov	rdi, rbx
	lea	rsi, [wdy_sub]
	call	__ast_w_node
	mov	[wdy_g], rax
  .no_g:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_PATH
	lea	rcx, [wdy_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_p
	mov	rdi, rbx
	lea	rsi, [wdy_sub]
	mov	rdx, [wdy_g]
	call	__ast_w_pathg
	mov	[wdy_p], rax
  .no_p:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TYPE_ROW
	lea	rcx, [wdy_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_row
	mov	rdi, rbx
	lea	rsi, [wdy_sub]
	call	__ast_w_node
	mov	[wdy_row], rax
  .no_row:
	cmp	qword [wdy_p], 0
	jne	.mk
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wdy_p], rax
  .mk:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_TYDYN
	xor	rcx, rcx
	mov	r8, [wdy_p]
	mov	r9, [wdy_row]
	call	__ast_w_mk
	return
endp

; `functio '(' [Type (',' Type)*] ')' '->' Type [TypeRow]` (spec §8.6). The
; parameter list and the result are all `CST_TYPE` children of one node, so
; the LAST of them is the result and the rest are parameters.
proc __ast_w_fntype, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wft_sub, sizeof.CstRed
		slot wft_mark, dq
		slot wft_last, dq
		slot wft_res, dq
		slot wft_row, dq
		slot wft_off, dq
		slot wft_cnt, dq
		slot wft_i,   dq
		slot wft_n,   dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wft_res], 0
	mov	qword [wft_row], 0
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wft_n], rax

	mov	qword [wft_last], -1
	mov	qword [wft_i], 0
  .scan:
	mov	rax, [wft_i]
	cmp	rax, [wft_n]
	jae	.scanned
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wft_i]
	lea	rcx, [wft_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wft_sub]
	call	__ast_w_kind
	cmp	eax, CST_TYPE
	jne	.scan_next
	mov	rax, [wft_i]
	mov	[wft_last], rax
  .scan_next:
	inc	qword [wft_i]
	jmp	.scan
  .scanned:
	mov	rdi, [rbx + AstWalk.ast]
	call	ast_list_mark
	mov	[wft_mark], rax
	mov	qword [wft_i], 0
  .kid:
	mov	rax, [wft_i]
	cmp	rax, [wft_n]
	jae	.kids_done
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wft_i]
	lea	rcx, [wft_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wft_sub]
	call	__ast_w_kind
	cmp	eax, CST_TYPE
	jne	.next
	mov	rdi, rbx
	lea	rsi, [wft_sub]
	call	__ast_w_type
	mov	rcx, [wft_i]
	cmp	rcx, [wft_last]
	je	.is_res
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, rax
	call	ast_list_push
	jmp	.next
  .is_res:
	mov	[wft_res], rax
  .next:
	inc	qword [wft_i]
	jmp	.kid
  .kids_done:
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wft_mark]
	call	ast_list_emit
	mov	[wft_off], rax
	mov	[wft_cnt], rdx
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TYPE_ROW
	lea	rcx, [wft_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.no_row
	mov	rdi, rbx
	lea	rsi, [wft_sub]
	call	__ast_w_node
	mov	[wft_row], rax
  .no_row:
	cmp	qword [wft_res], 0
	jne	.mk
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	mov	[wft_res], rax
  .mk:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_TYFN
	xor	rcx, rcx
	mov	r8, [wft_off]
	mov	r9, [wft_cnt]
	call	__ast_w_mk
	mov	r14, rax
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r14
	mov	rdx, [wft_res]
	mov	rcx, [wft_row]
	call	ast_node_cd
	mov	rax, r14
	return
endp

; `Item ::= Annotation* ['publica'] ItemBody` (spec §8.6 decision 5). Neither
; `publica` nor an annotation is a node: both are facts ABOUT the declaration
; under them, so the body is built first and they are folded into its `aux`
; and its `AstDecl.flags` afterwards. Annotation ARGUMENTS are `[OPEN]` in
; §8.6 and typed-ast.md section 6 -- a flag bit is all that exists.
proc __ast_w_item, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wit_sub, sizeof.CstRed
		slot wit_pub, dq
		slot wit_nuc, dq
		slot wit_tra, dq
		slot wit_n,   dq
		slot wit_i,   dq
		slot wit_nd,  dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	qword [wit_pub], 0
	mov	qword [wit_nuc], 0
	mov	qword [wit_tra], 0
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_nkid
	mov	[wit_n], rax
	mov	qword [wit_i], 0
  .scan:
	mov	rax, [wit_i]
	cmp	rax, [wit_n]
	jae	.scanned
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [wit_i]
	lea	rcx, [wit_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wit_sub]
	call	__ast_w_kind
	cmp	eax, CST_TK_KEYWORD
	je	.a_kw
	cmp	eax, CST_ANNOTATION
	jne	.scan_next
	mov	rdi, rbx
	lea	rsi, [wit_sub]
	mov	rdx, CST_TK_ANNOT
	lea	rcx, [wit_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.scan_next
	mov	rdi, rbx
	lea	rsi, [wit_sub]
	mov	rdx, AST_W_NUCLEUS
	call	__ast_w_isw
	test	eax, eax
	jz	.try_tra
	mov	qword [wit_nuc], 1
	jmp	.scan_next
  .try_tra:
	mov	rdi, rbx
	lea	rsi, [wit_sub]
	mov	rdx, AST_W_TRANSITUS
	call	__ast_w_isw
	test	eax, eax
	jz	.scan_next
	mov	qword [wit_tra], 1
	jmp	.scan_next
  .a_kw:
	mov	rdi, rbx
	lea	rsi, [wit_sub]
	mov	rdx, AST_W_PUBLICA
	call	__ast_w_isw
	test	eax, eax
	jz	.scan_next
	mov	qword [wit_pub], 1
  .scan_next:
	inc	qword [wit_i]
	jmp	.scan
  .scanned:
	; The body is the first interior child that is not an annotation.
	mov	r14, -1
  .find:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__ast_w_nexti
	cmp	rax, 0
	jl	.nobody
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	lea	rcx, [wit_sub]
	call	__ast_w_child
	mov	rdi, rbx
	lea	rsi, [wit_sub]
	call	__ast_w_kind
	cmp	eax, CST_ANNOTATION
	je	.find
	mov	rdi, rbx
	lea	rsi, [wit_sub]
	call	__ast_w_node
	test	rax, rax
	jz	.nobody
	mov	r13, rax

	; Where do the flags go? Onto the declaration this node introduces --
	; if it introduces one, which the role table answers.
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	call	ast_node_at
	movzx	r14d, word [rax + AstNode.kind]
	mov	eax, [rax + AstNode.d]
	mov	[wit_nd], rax
	mov	rdi, r14
	mov	rsi, 3
	call	ast_role_of
	cmp	eax, AST_R_DECL
	jne	.done
	cmp	qword [wit_nd], 0
	je	.done

	cmp	qword [wit_pub], 0
	je	.no_pub
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wit_nd]
	mov	rdx, AST_F_PUBLICA
	call	ast_decl_flag
	cmp	r14, AST_FN
	jne	.no_pub
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	mov	rdx, AST_FN_PUBLICA
	call	ast_node_aux_or
  .no_pub:
	cmp	qword [wit_nuc], 0
	je	.no_nuc
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wit_nd]
	mov	rdx, AST_F_NUCLEUS
	call	ast_decl_flag
	cmp	r14, AST_FN
	jne	.no_nuc
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	mov	rdx, AST_FN_NUCLEUS
	call	ast_node_aux_or
  .no_nuc:
	cmp	qword [wit_tra], 0
	je	.done
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, [wit_nd]
	mov	rdx, AST_F_TRANSITUS
	call	ast_decl_flag
	cmp	r14, AST_STRUCT
	jne	.done
	mov	rdi, [rbx + AstWalk.ast]
	mov	rsi, r13
	mov	rdx, AST_ST_TRANSITUS
	call	ast_node_aux_or
  .done:
	mov	rax, r13
	return
  .nobody:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	return
endp

; A node that is nothing but a list of one child kind: `Generics`, a
; capability `Row`, `GenericArgs`, `Module`.
proc __ast_w_listnode, w, red, akind, listkind
	uses	rbx, r12
	locals
		slot wln_off, dq
		slot wln_cnt, dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [listkind]
	call	__ast_w_list
	mov	[wln_off], rax
	mov	[wln_cnt], rdx
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [akind]
	xor	rcx, rcx
	mov	r8, [wln_off]
	mov	r9, [wln_cnt]
	call	__ast_w_mk
	return
endp

; ---- the dispatcher --------------------------------------------------------
; One `cmp`/`je` chain rather than a jump table: a table of code addresses is
; 80 pointers of raw bytes in the executable segment, and tools/syscall-audit.sh
; sweeps that segment linearly. Compares cost a few cycles per node and cost
; nothing to reason about.
;
; rax = the AST node this CST node becomes, or 0 for one that is CONSUMED by
; its parent (a suffix, an arm, a clause, a bracket, a keyword, trivia). 0 is
; not an error: `__ast_w_inners` and `__ast_w_list` both skip it.
proc __ast_w_node, w, red
	uses	rbx, r12, r13, r14
	locals
		slot wd2_sub, sizeof.CstRed
		slot wd2_g,   dq
	endl
	mov	rbx, [w]
	mov	r12, [red]
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_kind
	mov	r13, rax

	mov	rcx, CST_LEAF_LAST
	cmp	r13, rcx
	jg	.interior
	; A leaf. Parse failure keeps a node; everything else is consumed by
	; whichever routine above needed its text.
	cmp	r13, CST_MISSING
	je	.error
	cmp	r13, CST_TK_JUNK
	je	.error
	xor	eax, eax
	return

  .interior:
	cmp	r13, CST_MODULE
	je	.module
	cmp	r13, CST_ITEM
	je	.item
	cmp	r13, CST_FUNCTION_DECL
	je	.fndecl
	cmp	r13, CST_SIGNATURE
	je	.sig
	cmp	r13, CST_PARAM
	je	.param
	cmp	r13, CST_GENERIC_PARAMS
	je	.generics
	cmp	r13, CST_GENERIC_PARAM
	je	.genparam
	cmp	r13, CST_DECL_ROW
	je	.row
	cmp	r13, CST_TYPE_ROW
	je	.row
	cmp	r13, CST_ROW_ITEM
	je	.rowitem
	cmp	r13, CST_STRUCT_DECL
	je	.struct
	cmp	r13, CST_FIELD
	je	.field
	cmp	r13, CST_TYPE_DECL
	je	.typus
	cmp	r13, CST_INTERFACE_DECL
	je	.iface
	cmp	r13, CST_MEMBER
	je	.member
	cmp	r13, CST_POTESTAS_DECL
	je	.potestas
	cmp	r13, CST_EXTERNUS_BLOCK
	je	.externus
	cmp	r13, CST_EXTERNUS_MEMBER
	je	.externfn
	cmp	r13, CST_BINDING_STMT
	je	.binding
	cmp	r13, CST_BLOCK
	je	.block
	cmp	r13, CST_JUMP_STMT
	je	.jump
	cmp	r13, CST_SUB_STMT
	je	.substmt
	cmp	r13, CST_IF_STMT
	je	.ifstmt
	cmp	r13, CST_WHILE_STMT
	je	.whilestmt
	cmp	r13, CST_FOR_STMT
	je	.forstmt
	cmp	r13, CST_MATCH_STMT
	je	.matchstmt
	cmp	r13, CST_CASUS_ARM
	je	.casus
	cmp	r13, CST_PATTERN
	je	.pattern
	cmp	r13, CST_EXPR_STMT
	je	.exprstmt
	cmp	r13, CST_ASSIGN_STMT
	je	.assign
	cmp	r13, CST_OR_EXPR
	je	.binary
	cmp	r13, CST_AND_EXPR
	je	.binary
	cmp	r13, CST_CMP_EXPR
	je	.binary
	cmp	r13, CST_RANGE_EXPR
	je	.binary
	cmp	r13, CST_XOR_EXPR
	je	.binary
	cmp	r13, CST_SHIFT_EXPR
	je	.binary
	cmp	r13, CST_ADD_EXPR
	je	.binary
	cmp	r13, CST_MUL_EXPR
	je	.binary
	cmp	r13, CST_CAST_EXPR
	je	.cast
	cmp	r13, CST_UNARY_EXPR
	je	.unary
	cmp	r13, CST_POSTFIX_EXPR
	je	.postfix
	cmp	r13, CST_PAREN_EXPR
	je	.inner
	cmp	r13, CST_LITERAL
	je	.literal
	cmp	r13, CST_NAME_EXPR
	je	.path
	cmp	r13, CST_LAMBDA
	je	.lambda
	cmp	r13, CST_TYPE
	je	.type
	cmp	r13, CST_BIT_TYPE
	je	.type
	cmp	r13, CST_DYN_TYPE
	je	.dyn
	cmp	r13, CST_FN_TYPE
	je	.fntype
	cmp	r13, CST_PAREN_TYPE
	je	.type
	cmp	r13, CST_GENERIC_ARGS
	je	.genargs
	cmp	r13, CST_TYPE_ARG
	je	.typearg
	cmp	r13, CST_PATH
	je	.path
	cmp	r13, CST_FIELD_INIT
	je	.fieldinit
	cmp	r13, CST_ARRAY_LIT
	je	.arraylit
	cmp	r13, CST_ARRAY_REPEAT
	je	.arrayrep
	cmp	r13, CST_ERROR
	je	.error
	; Consumed by a parent: PARAM_LIST, ANNOTATION, INTERFACE_BODY,
	; IMPL_HEAD, IMPL_BODY, EXTERNUS_HEAD, SIN_ARM, ALITER_ARM,
	; TERMINUS_CLAUSE, CONTRAHE_CLAUSE, FORMA_CLAUSE, the five expression
	; suffixes, ORDER_SUFFIX, PLACE_SUFFIX.
	xor	eax, eax
	return
  .fieldinit:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_fieldinit
	return
  .arraylit:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_arraylit
	return
  .arrayrep:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_arrayrep
	return

  .module:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_inners
	mov	r13, rax
	mov	r14, rdx
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_MODULE
	xor	rcx, rcx
	mov	r8, r13
	mov	r9, r14
	call	__ast_w_mk
	return
  .item:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_item
	return
  .fndecl:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_FN
	xor	rcx, rcx
	call	__ast_w_fn
	return
  .member:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_MEMBER
	xor	rcx, rcx
	call	__ast_w_fn
	return
  .externfn:
	; An `externus` member may carry its own `publica` (spec §8.6:
	; `ExternusBlock ::= ... '{' (['publica'] Signature)* '}'`), and it is
	; not wrapped in an `Item`, so nothing else would see it.
	mov	r14, AST_F_EXTERNUS
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CST_TK_KEYWORD
	lea	rcx, [wd2_sub]
	call	__ast_w_kidk
	test	eax, eax
	jz	.externfn_mk
	mov	rdi, rbx
	lea	rsi, [wd2_sub]
	mov	rdx, AST_W_PUBLICA
	call	__ast_w_isw
	test	eax, eax
	jz	.externfn_mk
	or	r14, AST_F_PUBLICA
  .externfn_mk:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_D_EXTERNFN
	mov	rcx, r14
	call	__ast_w_fn
	return
  .sig:
	mov	rdi, rbx
	mov	rsi, r12
	lea	rdx, [wd2_g]
	call	__ast_w_sig
	return
  .param:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_PARAM
	mov	rcx, AST_D_PARAM
	mov	r8, 1
	call	__ast_w_namety
	return
  .genparam:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_GENPARAM
	mov	rcx, AST_D_GENPARAM
	xor	r8, r8
	call	__ast_w_namety
	return
  .field:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_FIELD
	mov	rcx, AST_D_FIELD
	mov	r8, 1
	call	__ast_w_namety
	return
  .generics:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_GENERICS
	mov	rcx, CST_GENERIC_PARAM
	call	__ast_w_listnode
	return
  .row:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_ROW
	mov	rcx, CST_ROW_ITEM
	call	__ast_w_listnode
	return
  .genargs:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_GENARGS
	mov	rcx, CST_TYPE_ARG
	call	__ast_w_listnode
	return
  .rowitem:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_rowitem
	return
  .typearg:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_typearg
	return
  .struct:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_STRUCT
	mov	rcx, AST_D_STRUCT
	mov	r8, CST_FIELD
	mov	r9, r12
	call	__ast_w_named
	return
  .potestas:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_POTESTAS
	mov	rcx, AST_D_POTESTAS
	mov	r8, CST_PATH
	mov	r9, r12
	call	__ast_w_named
	return
  .typus:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_typus
	return
  .iface:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_iface
	return
  .externus:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_externus
	return
  .binding:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_binding
	return
  .block:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_block
	return
  .jump:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_jump
	return
  .substmt:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_sub
	return
  .ifstmt:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_if
	return
  .whilestmt:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_while
	return
  .forstmt:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_for
	return
  .matchstmt:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_match
	return
  .casus:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_casus
	return
  .pattern:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_pattern
	return
  .exprstmt:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_inner
	test	rax, rax
	jnz	.exprstmt_mk
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
  .exprstmt_mk:
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, AST_EXPRSTMT
	xor	rcx, rcx
	mov	r8, r14
	xor	r9, r9
	call	__ast_w_mk
	return
  .assign:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_assign
	return
  .binary:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_binary
	return
  .cast:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_cast
	return
  .unary:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_unary
	return
  .postfix:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_postfix
	return
  .literal:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_lit
	return
  .lambda:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_lambda
	return
  .path:
	mov	rdi, rbx
	mov	rsi, r12
	xor	rdx, rdx
	call	__ast_w_pathg
	return
  .type:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_type
	return
  .dyn:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_dyn
	return
  .fntype:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_fntype
	return
  .inner:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_inner
	test	rax, rax
	jnz	.inner_ok
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
  .inner_ok:
	return
  .error:
	mov	rdi, rbx
	mov	rsi, r12
	call	__ast_w_err
	return
endp

; ---- the entry point -------------------------------------------------------
; ast_from_cst(tree, t, file_id) -> rax = the root node's id.
;
; `tree` must be freshly `ast_init`ed and share its interner with the lexer
; that produced `t`: an interner id in a green leaf and an interner id in an
; `AstNode` have to mean the same bytes, and they do because there is one
; interner and the driver passes it to both.
;
; A tree is produced on BOTH paths. `cst_parse` sets `CF` on a syntax error
; but leaves a valid root either way (spec §9.1's CST is error-tolerant), and
; this walk turns every `ERROR` and `MISSING` in it into an `AST_ERROR` node,
; so Stage 2 gets a tree to type whatever survived. Nothing here reports a
; diagnostic; the parser already did.
proc ast_from_cst, tree, t, file_id
	uses	rbx, r12
	locals
		slot wr2_w,  sizeof.AstWalk
		slot wr2_rt, sizeof.CstRed
		slot wr2_root, dq
	endl
	mov	rbx, [tree]
	lea	r12, [wr2_w]
	mov	[r12 + AstWalk.ast], rbx
	mov	rax, [t]
	mov	[r12 + AstWalk.cst], rax
	mov	rax, [rbx + Ast.names]
	mov	[r12 + AstWalk.names], rax
	mov	eax, [file_id]
	mov	[r12 + AstWalk.file_id], eax
	mov	dword [r12 + AstWalk.parent], 0

	lea	rdi, [wr2_rt]
	mov	rsi, [t]
	call	cst_red_root
	mov	rdi, r12
	lea	rsi, [wr2_rt]
	call	__ast_w_node
	test	rax, rax
	jz	.done
	mov	rsi, rax
	mov	[wr2_root], rax
	mov	rdi, rbx
	call	ast_set_root

	; PASS 0 OF THE CHECKER, RUN HERE (checker.md section 2.2, and
	; ast/build.inc's `ast_cap_push` for why here). Spec §4.6's eleven
	; atoms have no declaration site in §8.6's grammar, so the eleven
	; declarations that `Seg.d`, `RowItem.d` and `sub`'s `Decl.ty` will
	; name have to be pushed by somebody. Here they land AFTER every
	; source declaration and nothing pushes another, which is what makes
	; the atom of §4.6 ordinal `k` the declaration `N + k` for a module
	; of `N` source declarations.
	;
	; ON THE FAILED PATH THEY ARE NOT PUSHED. A walk that produced no
	; root produced no tree either; `ast_verify` admits a tree with no
	; atoms (a hand-built one usually has none) and a checker asks
	; `ast_cap_base` rather than assuming.
	mov	rdi, rbx
	call	ast_cap_push
	mov	rax, [wr2_root]
  .done:
	return
endp
