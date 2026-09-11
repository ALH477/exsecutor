; compiler/x86_64/cst/kinds.inc
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
; CST NODE KINDS. §8.6: "CST node kinds are these nonterminals plus `ERROR`
; and `MISSING`." This file is that sentence, made into dense integers, plus
; the leaf kinds §8.6 does not name because §8.4 already did -- a lossless
; tree has a green node per TOKEN and per piece of TRIVIA too, and those need
; kinds as much as the nonterminals do.
;
; THREE DEPARTURES FROM A LITERAL READING OF §8.6'S GRAMMAR, each recorded
; here rather than absorbed, because the grammar is `[UNTESTED]` and this file
; is the first thing to exercise it:
;
;   1. NO NODE PER PRECEDENCE LEVEL WHEN THE LEVEL DID NOT APPLY. Read
;      literally, `Or ::= And ('vel' And)*` makes every atom in the language
;      twelve nodes deep (Or > And > Cmp > Range > Xor > Shift > Add > Mul >
;      Cast > Unary > Postfix > Primary) before it reaches an identifier --
;      ten before spec §8.6 gained levels 5a and 5b. The kinds below are
;      §8.6's own names, one per level, but a level emits its node ONLY when
;      it actually consumed an operator; otherwise it passes its single child
;      through. This is what every red-green tree does (rust-analyzer's
;      `Marker::abandon`), it changes no parse, and it is the difference
;      between a 12-node and a 90-node tree for `redde "...";`. REPORTED as an
;      underspecification of §8.6, not a deviation from it: the section says
;      which kinds exist, never that a kind must be emitted where its
;      production matched nothing.
;   2. THREE PRODUCTIONS §8.6 SPELLS INLINE GET NAMES HERE, because a CST
;      cannot hold an unnamed repetition group: `('casus' Pattern Block)*` is
;      `CST_CASUS_ARM`, `('contrahe' IDENT ':' ArithOp)*` is
;      `CST_CONTRAHE_CLAUSE`, and `Expr ['=' Expr] ';'` splits into
;      `CST_EXPR_STMT` and `CST_ASSIGN_STMT` -- decision 5 of §8.6 makes
;      assignment a statement rather than an operator, so the two cases are
;      different statements and not one with an optional tail.
;   3. `Annotation ::= '@' IDENT` IS ONE TOKEN, NOT TWO. §8.4's table lists
;      `@nomen` as a single "annotation" token and lexer/lex.inc emits it as
;      one `TOK_ANNOT`. §8.6's grammar writes it as two terminals. The lexer
;      is right and §8.6's line is stale -- see cst/parse.inc's header.
;
; Kinds are an INTERNAL representation and carry none of §8.3's permanence
; guarantee: nothing outside this repository consumes them, and unlike an
; `EXS-E` code a kind number never appears in output. Renumbering this file is
; a local edit; renumbering §13 is not.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.4, §8.6, §9.1.
; -----------------------------------------------------------------------------

; ---- leaf kinds ------------------------------------------------------------
; Every leaf carries its own text, so `CST_TK_PUNCT` does not need one kind per
; glyph: a green token is (kind, text) and `(` and `)` differ in the text. The
; three trivia kinds and `CST_MISSING` are the ones with no `TOK_*` counterpart
; -- lexer/lex.inc skips whitespace and comments rather than emitting them, so
; cst/parse.inc reconstructs them from the gaps between token spans. That
; reconstruction is exact and it is what makes the tree lossless; see
; cst/parse.inc's header, "TRIVIA IS A GAP, NOT A TOKEN".

CST_TK_EOF          = 1
CST_TK_IDENT        = 2
CST_TK_KEYWORD      = 3
CST_TK_STRING       = 4
CST_TK_NUMBER       = 5
CST_TK_ANNOT        = 6
CST_TK_PUNCT        = 7
CST_TK_WS           = 8
CST_TK_COMMENT      = 9
CST_TK_JUNK         = 10
CST_MISSING         = 11
CST_LEAF_LAST = 11	; every kind at or below this is a leaf

; ---- node kinds ------------------------------------------------------------
; §8.6's nonterminals, in the order that section's grammar block writes them,
; plus the three named in departure 2 above and `CST_ERROR` last.
CST_MODULE          = 12
CST_ITEM            = 13
CST_ANNOTATION      = 14
CST_FUNCTION_DECL   = 15
CST_SIGNATURE       = 16
CST_PARAM_LIST      = 17
CST_PARAM           = 18
CST_GENERIC_PARAMS  = 19
CST_GENERIC_PARAM   = 20
CST_DECL_ROW        = 21
CST_TYPE_ROW        = 22
CST_ROW_ITEM        = 23
CST_STRUCT_DECL     = 24
CST_FIELD           = 25
CST_TYPE_DECL       = 26
CST_INTERFACE_DECL  = 27
CST_INTERFACE_BODY  = 28
CST_IMPL_HEAD       = 29
CST_IMPL_BODY       = 30
CST_MEMBER          = 31
CST_POTESTAS_DECL   = 32
CST_EXTERNUS_BLOCK  = 33
CST_EXTERNUS_HEAD   = 34
CST_EXTERNUS_MEMBER = 35
CST_BINDING_STMT    = 36
CST_BLOCK           = 37
CST_JUMP_STMT       = 38
CST_SUB_STMT        = 39
CST_IF_STMT         = 40
CST_SIN_ARM         = 41
CST_ALITER_ARM      = 42
CST_WHILE_STMT      = 43
CST_TERMINUS_CLAUSE = 44
CST_FOR_STMT        = 45
CST_CONTRAHE_CLAUSE = 46
CST_FORMA_CLAUSE    = 47
CST_MATCH_STMT      = 48
CST_CASUS_ARM       = 49
CST_PATTERN         = 50
CST_EXPR_STMT       = 51
CST_ASSIGN_STMT     = 52
CST_OR_EXPR         = 53
CST_AND_EXPR        = 54
CST_CMP_EXPR        = 55
CST_RANGE_EXPR      = 56
CST_XOR_EXPR        = 57
CST_SHIFT_EXPR      = 58
CST_ADD_EXPR        = 59
CST_MUL_EXPR        = 60
CST_CAST_EXPR       = 61
CST_UNARY_EXPR      = 62
CST_POSTFIX_EXPR    = 63
CST_FIELD_SUFFIX    = 64
CST_CALL_SUFFIX     = 65
CST_INDEX_SUFFIX    = 66
CST_TRY_SUFFIX      = 67
CST_STRUCT_LIT_SUFFIX = 68	; spec §8.6 `StructLit`, wire-codec.md D3
CST_FIELD_INIT      = 69
CST_PAREN_EXPR      = 70
CST_LITERAL         = 71
CST_NAME_EXPR       = 72
CST_LAMBDA          = 73
CST_TYPE            = 74
CST_BIT_TYPE        = 75
CST_DYN_TYPE        = 76
CST_FN_TYPE         = 77
CST_PAREN_TYPE      = 78
CST_GENERIC_ARGS    = 79
CST_TYPE_ARG        = 80
CST_ORDER_SUFFIX    = 81
CST_PLACE_SUFFIX    = 82
CST_PATH            = 83
CST_ERROR           = 84
CST_KIND_COUNT = 84

; ---- kind names ------------------------------------------------------------
; For `cst_dump` (cst/red.inc) and for fixtures. Not output: no rendered
; diagnostic contains a kind name, so this table has no §8.3 obligations. The
; pool and the offset table are written as one ordered block, so a name's
; length is the next entry's offset minus its own -- there is no separately
; maintained length column to drift.
cst_kind_text:
  cst_kn_1:	db	'TK_EOF'
  cst_kn_2:	db	'TK_IDENT'
  cst_kn_3:	db	'TK_KEYWORD'
  cst_kn_4:	db	'TK_STRING'
  cst_kn_5:	db	'TK_NUMBER'
  cst_kn_6:	db	'TK_ANNOT'
  cst_kn_7:	db	'TK_PUNCT'
  cst_kn_8:	db	'TK_WS'
  cst_kn_9:	db	'TK_COMMENT'
  cst_kn_10:	db	'TK_JUNK'
  cst_kn_11:	db	'MISSING'
  cst_kn_12:	db	'MODULE'
  cst_kn_13:	db	'ITEM'
  cst_kn_14:	db	'ANNOTATION'
  cst_kn_15:	db	'FUNCTION_DECL'
  cst_kn_16:	db	'SIGNATURE'
  cst_kn_17:	db	'PARAM_LIST'
  cst_kn_18:	db	'PARAM'
  cst_kn_19:	db	'GENERIC_PARAMS'
  cst_kn_20:	db	'GENERIC_PARAM'
  cst_kn_21:	db	'DECL_ROW'
  cst_kn_22:	db	'TYPE_ROW'
  cst_kn_23:	db	'ROW_ITEM'
  cst_kn_24:	db	'STRUCT_DECL'
  cst_kn_25:	db	'FIELD'
  cst_kn_26:	db	'TYPE_DECL'
  cst_kn_27:	db	'INTERFACE_DECL'
  cst_kn_28:	db	'INTERFACE_BODY'
  cst_kn_29:	db	'IMPL_HEAD'
  cst_kn_30:	db	'IMPL_BODY'
  cst_kn_31:	db	'MEMBER'
  cst_kn_32:	db	'POTESTAS_DECL'
  cst_kn_33:	db	'EXTERNUS_BLOCK'
  cst_kn_34:	db	'EXTERNUS_HEAD'
  cst_kn_35:	db	'EXTERNUS_MEMBER'
  cst_kn_36:	db	'BINDING_STMT'
  cst_kn_37:	db	'BLOCK'
  cst_kn_38:	db	'JUMP_STMT'
  cst_kn_39:	db	'SUB_STMT'
  cst_kn_40:	db	'IF_STMT'
  cst_kn_41:	db	'SIN_ARM'
  cst_kn_42:	db	'ALITER_ARM'
  cst_kn_43:	db	'WHILE_STMT'
  cst_kn_44:	db	'TERMINUS_CLAUSE'
  cst_kn_45:	db	'FOR_STMT'
  cst_kn_46:	db	'CONTRAHE_CLAUSE'
  cst_kn_47:	db	'FORMA_CLAUSE'
  cst_kn_48:	db	'MATCH_STMT'
  cst_kn_49:	db	'CASUS_ARM'
  cst_kn_50:	db	'PATTERN'
  cst_kn_51:	db	'EXPR_STMT'
  cst_kn_52:	db	'ASSIGN_STMT'
  cst_kn_53:	db	'OR_EXPR'
  cst_kn_54:	db	'AND_EXPR'
  cst_kn_55:	db	'CMP_EXPR'
  cst_kn_56:	db	'RANGE_EXPR'
  cst_kn_57:	db	'XOR_EXPR'
  cst_kn_58:	db	'SHIFT_EXPR'
  cst_kn_59:	db	'ADD_EXPR'
  cst_kn_60:	db	'MUL_EXPR'
  cst_kn_61:	db	'CAST_EXPR'
  cst_kn_62:	db	'UNARY_EXPR'
  cst_kn_63:	db	'POSTFIX_EXPR'
  cst_kn_64:	db	'FIELD_SUFFIX'
  cst_kn_65:	db	'CALL_SUFFIX'
  cst_kn_66:	db	'INDEX_SUFFIX'
  cst_kn_67:	db	'TRY_SUFFIX'
  cst_kn_68:	db	'STRUCT_LIT_SUFFIX'
  cst_kn_69:	db	'FIELD_INIT'
  cst_kn_70:	db	'PAREN_EXPR'
  cst_kn_71:	db	'LITERAL'
  cst_kn_72:	db	'NAME_EXPR'
  cst_kn_73:	db	'LAMBDA'
  cst_kn_74:	db	'TYPE'
  cst_kn_75:	db	'BIT_TYPE'
  cst_kn_76:	db	'DYN_TYPE'
  cst_kn_77:	db	'FN_TYPE'
  cst_kn_78:	db	'PAREN_TYPE'
  cst_kn_79:	db	'GENERIC_ARGS'
  cst_kn_80:	db	'TYPE_ARG'
  cst_kn_81:	db	'ORDER_SUFFIX'
  cst_kn_82:	db	'PLACE_SUFFIX'
  cst_kn_83:	db	'PATH'
  cst_kn_84:	db	'ERROR'
  cst_kn_end:

cst_kind_off:
	dd	cst_kn_1 - cst_kind_text
	dd	cst_kn_2 - cst_kind_text
	dd	cst_kn_3 - cst_kind_text
	dd	cst_kn_4 - cst_kind_text
	dd	cst_kn_5 - cst_kind_text
	dd	cst_kn_6 - cst_kind_text
	dd	cst_kn_7 - cst_kind_text
	dd	cst_kn_8 - cst_kind_text
	dd	cst_kn_9 - cst_kind_text
	dd	cst_kn_10 - cst_kind_text
	dd	cst_kn_11 - cst_kind_text
	dd	cst_kn_12 - cst_kind_text
	dd	cst_kn_13 - cst_kind_text
	dd	cst_kn_14 - cst_kind_text
	dd	cst_kn_15 - cst_kind_text
	dd	cst_kn_16 - cst_kind_text
	dd	cst_kn_17 - cst_kind_text
	dd	cst_kn_18 - cst_kind_text
	dd	cst_kn_19 - cst_kind_text
	dd	cst_kn_20 - cst_kind_text
	dd	cst_kn_21 - cst_kind_text
	dd	cst_kn_22 - cst_kind_text
	dd	cst_kn_23 - cst_kind_text
	dd	cst_kn_24 - cst_kind_text
	dd	cst_kn_25 - cst_kind_text
	dd	cst_kn_26 - cst_kind_text
	dd	cst_kn_27 - cst_kind_text
	dd	cst_kn_28 - cst_kind_text
	dd	cst_kn_29 - cst_kind_text
	dd	cst_kn_30 - cst_kind_text
	dd	cst_kn_31 - cst_kind_text
	dd	cst_kn_32 - cst_kind_text
	dd	cst_kn_33 - cst_kind_text
	dd	cst_kn_34 - cst_kind_text
	dd	cst_kn_35 - cst_kind_text
	dd	cst_kn_36 - cst_kind_text
	dd	cst_kn_37 - cst_kind_text
	dd	cst_kn_38 - cst_kind_text
	dd	cst_kn_39 - cst_kind_text
	dd	cst_kn_40 - cst_kind_text
	dd	cst_kn_41 - cst_kind_text
	dd	cst_kn_42 - cst_kind_text
	dd	cst_kn_43 - cst_kind_text
	dd	cst_kn_44 - cst_kind_text
	dd	cst_kn_45 - cst_kind_text
	dd	cst_kn_46 - cst_kind_text
	dd	cst_kn_47 - cst_kind_text
	dd	cst_kn_48 - cst_kind_text
	dd	cst_kn_49 - cst_kind_text
	dd	cst_kn_50 - cst_kind_text
	dd	cst_kn_51 - cst_kind_text
	dd	cst_kn_52 - cst_kind_text
	dd	cst_kn_53 - cst_kind_text
	dd	cst_kn_54 - cst_kind_text
	dd	cst_kn_55 - cst_kind_text
	dd	cst_kn_56 - cst_kind_text
	dd	cst_kn_57 - cst_kind_text
	dd	cst_kn_58 - cst_kind_text
	dd	cst_kn_59 - cst_kind_text
	dd	cst_kn_60 - cst_kind_text
	dd	cst_kn_61 - cst_kind_text
	dd	cst_kn_62 - cst_kind_text
	dd	cst_kn_63 - cst_kind_text
	dd	cst_kn_64 - cst_kind_text
	dd	cst_kn_65 - cst_kind_text
	dd	cst_kn_66 - cst_kind_text
	dd	cst_kn_67 - cst_kind_text
	dd	cst_kn_68 - cst_kind_text
	dd	cst_kn_69 - cst_kind_text
	dd	cst_kn_70 - cst_kind_text
	dd	cst_kn_71 - cst_kind_text
	dd	cst_kn_72 - cst_kind_text
	dd	cst_kn_73 - cst_kind_text
	dd	cst_kn_74 - cst_kind_text
	dd	cst_kn_75 - cst_kind_text
	dd	cst_kn_76 - cst_kind_text
	dd	cst_kn_77 - cst_kind_text
	dd	cst_kn_78 - cst_kind_text
	dd	cst_kn_79 - cst_kind_text
	dd	cst_kn_80 - cst_kind_text
	dd	cst_kn_81 - cst_kind_text
	dd	cst_kn_82 - cst_kind_text
	dd	cst_kn_83 - cst_kind_text
	dd	cst_kn_84 - cst_kind_text
	dd	cst_kn_end - cst_kind_text

; cst_kind_name(kind) -> rax = pointer to the name's bytes, rdx = its length.
; An out-of-range kind is an internal contract violation and traps
; (docs/asm-conventions.md, "1.3 Error protocol: CF / eax" -- the `rassert`
; channel); the only source of a kind is this file's own constants.
proc cst_kind_name, kind
	locals
	endl
	mov	ecx, [kind]
	rassert	ecx ge 1
	rassert	ecx le CST_KIND_COUNT
	dec	ecx
	lea	rax, [cst_kind_text]
	mov	r8d, [cst_kind_off + rcx*4]
	mov	r9d, [cst_kind_off + rcx*4 + 4]
	add	rax, r8
	mov	edx, r9d
	sub	edx, r8d
	return
endp
