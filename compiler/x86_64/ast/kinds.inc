; compiler/x86_64/ast/kinds.inc
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
; AST KINDS, OPERAND ROLES AND NAMES. `docs/design/typed-ast.md` section 2.3's
; table, made into dense integers, plus the three enumerations that table calls
; "the builder's tables, in spec §8.6 order" (operators, literal classes, byte
; order / placement), plus the type-node kinds of section 2.5 and the
; declaration kinds of section 2.4.
;
; PURE DATA. Includes nothing, deliberately -- same reasoning as
; cst/kinds.inc and lexer/keywords.inc: a table this wide should not be able
; to drag a macro or a label in behind it.
;
; THE OPERAND-ROLE TABLE IS THE PART THAT DOES WORK. `AstNode` has four
; anonymous operand slots (`a b c d`) whose meaning is per-kind, and a
; verifier that does not know which slot holds a node id, which holds an
; `extra` offset and which holds an interner id cannot check anything about
; them. `ast_roles` gives every kind four role bytes, in slot order, so
; ast/verify.inc is a table walk rather than a switch with fifty arms that
; would have to be kept in sync with section 2.3 by hand.
;
; Kinds are INTERNAL and carry none of spec §8.3's permanence guarantee:
; nothing outside this repository consumes them and no kind number ever
; appears in output -- the dump (ast/dump.inc) writes NAMES, from the tables
; at the bottom of this file, for exactly that reason. Renumbering this file
; is a local change; renumbering an `EXS-E` code is not (CLAUDE.md).
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.6 (the grammar these kinds are
; the nonterminals of), §5.1-§5.5 (the type kinds), §4.1-§4.6 (rows).
; -----------------------------------------------------------------------------

; ---- node kinds ------------------------------------------------------------
; 0 is "not a node kind" and never appears in a live `AstNode`. Order matches
; typed-ast.md section 2.3's table top to bottom; `ast_kind_names`, below, is
; in the same order and the two are indexed by the same number.
AST_MODULE	= 1
AST_FN		= 2
AST_SIG		= 3
AST_GENERICS	= 4
AST_PARAM	= 5
AST_GENPARAM	= 6
AST_FIELD	= 7
AST_STRUCT	= 8
AST_IFACE	= 9
AST_POTESTAS	= 10
AST_IMPL	= 11
AST_IMPLHEAD	= 12
AST_TYPUS	= 13
AST_EXTERNUS	= 14
AST_BINDING	= 15
AST_ROW		= 16
AST_ROWITEM	= 17
AST_BLOCK	= 18
AST_SUB		= 19
AST_REDDE	= 20
AST_RUMPE	= 21
AST_PERGE	= 22
AST_EXPRSTMT	= 23
AST_ASSIGN	= 24
AST_SI		= 25
AST_DUM		= 26
AST_FOR		= 27
AST_FORHEAD	= 28
AST_CONTRAHE	= 29
AST_DISCERNE	= 30
AST_CASUS	= 31
AST_LIT		= 32
AST_PATH	= 33
AST_SEG		= 34
AST_UNARY	= 35
AST_BINARY	= 36
AST_CAST	= 37
AST_INDEX	= 38
AST_TRY		= 39
AST_CALL	= 40
AST_MEMBER	= 41
AST_LAMBDA	= 42
AST_TYPATH	= 43
AST_TYDYN	= 44
AST_TYBIT	= 45
AST_TYPTR	= 46
AST_TYFN	= 47
AST_TYSUFFIX	= 48
AST_GENARGS	= 49
AST_ERROR	= 50
AST_KIND_MAX	= 50

; ---- operand roles ---------------------------------------------------------
; What one of `a b c d` holds, for one kind. `AST_R_LIST` and `AST_R_LIST2`
; always name the NEXT slot as their count, which is why the table is stored
; in slot order and not as a set.
AST_R_NONE	= 0	; unused -- must be 0 in a well-formed node
AST_R_NODE	= 1	; node id, 0 admitted (an optional child)
AST_R_NODEQ	= 2	; node id, must be non-zero
AST_R_LIST	= 3	; `extra` offset; the next slot is the element count
AST_R_LIST2	= 4	; ditto, but the list holds 2 entries per count (`Si`)
AST_R_COUNT	= 5	; element count for the previous slot
AST_R_NAME	= 6	; rt/intern.inc id
AST_R_DECL	= 7	; declaration id, 0 until it is filled (Stage 1 or 2)
AST_R_ANY	= 8	; opaque to the verifier (a width, an abi id, a class)
AST_R_ROWI	= 9	; `RowItem.a`: a `Path` node id, or -- when
			; `aux` has AST_ROW_SICUT -- an interner id. See
			; ast/verify.inc; this role exists because one slot
			; with two types selected by another field cannot be
			; checked by the plain table.

; ---- declaration kinds -----------------------------------------------------
; typed-ast.md section 2.4's list of name-introducing constructs, in the order
; that section writes them. There is no `Module` declaration: a module-level
; declaration has `parent` 0.
AST_D_FN	= 1
AST_D_PARAM	= 2
AST_D_GENPARAM	= 3
AST_D_STRUCT	= 4
AST_D_FIELD	= 5
AST_D_TYPUS	= 6
AST_D_IFACE	= 7
AST_D_MEMBER	= 8
AST_D_IMPL	= 9
AST_D_POTESTAS	= 10
AST_D_EXTERNUS	= 11
AST_D_EXTERNFN	= 12
AST_D_BINDING	= 13
AST_D_SUB	= 14
AST_D_LOOPVAR	= 15
AST_D_ACCUM	= 16
AST_D_LAMBDA	= 17
AST_D_CAPATOM	= 18	; a capability ATOM of spec §4.6 -- the one declaration
			; kind with no declaration SITE. See "capability atoms",
			; below, and ast/build.inc's `ast_cap_push`.
AST_DECL_MAX	= 18

; ---- declaration flags -----------------------------------------------------
; `AstDecl.flags` is one byte and typed-ast.md section 2.4 names exactly eight
; flags, so the byte is FULL. A ninth flag has nowhere to go and must take a
; bit of the (otherwise unused for most kinds) `AstDecl.aux` -- recorded here
; because it is not obvious from the record and would be found the hard way.
AST_F_PUBLICA	= 0x01
AST_F_MUTABILIS	= 0x02
AST_F_TRANSITUS	= 0x04
AST_F_NUCLEUS	= 0x08
AST_F_EXTERNUS	= 0x10
AST_F_CAPBEAR	= 0x20	; capability_bearing (spec §4.3)  -- Stage 2
AST_F_ADDRTAKEN	= 0x40	; address_taken   (ssa-ir.md 2.5) -- Stage 2
AST_F_MEMRES	= 0x80	; memory_resident (ssa-ir.md 2.5) -- Stage 2

; ---- per-kind `aux` bits ---------------------------------------------------
; `aux` is kind-specific, so these numbering spaces deliberately overlap.
AST_FN_PUBLICA	= 0x01	; `Fn`
AST_FN_NUCLEUS	= 0x02
AST_ST_TRANSITUS = 0x01	; `Struct` `Interface` `Potestas`
AST_BIND_MUT	= 0x01	; `Binding`
AST_FOR_QUISQUE	= 0x01	; `For`
AST_ROW_SICUT	= 0x01	; `RowItem` -- `a` is a parameter NAME, not a `Path`
AST_PTR_REF	= 0	; `TyPtr` -- `&T`
AST_PTR_RAW	= 1	; `TyPtr` -- `*T`
AST_SFX_ORDER	= 0	; `TySuffix` -- `: IDENT` (byte order, spec §5.2)
AST_SFX_APUD	= 1	; `TySuffix` -- `apud IDENT` (placement, spec §5.5)

; ---- `Externus.aux`: the ABI, as a CLOSED enumeration (spec §5.3) ---------
; §5.3: *"their spellings in `externus(..., abi: X)` are `sysv_amd64`,
; `aapcs64` and `lp64d`, and the set is closed: the AST stores the ABI as a
; small enumeration, not an interned name, so an unknown one is `EXS-E0309` at
; the `externus` head rather than a value that overflows a 16-bit slot."*
;
; THIS REPLACES AN INTERNER ID. `Externus.aux` used to hold the ABI
; identifier's intern id, which is unbounded while `aux` is sixteen bits, so
; `__ast_w_externus` `rassert`ed rather than truncating and
; tests/unit/ast_from_cst_abi_overflow.asm existed to prove the trap fired.
; The spec closed the set instead; the trap and its fixture are retired
; (tests/unit/ast_from_cst_abi.asm replaces it) and the slot can no longer
; overflow, because three is not near 65535.
;
; 0 MEANS "A SPELLING THIS TABLE DOES NOT HAVE", not "absent". The walker has
; no diagnostic sink (ast/from_cst.inc's `AstWalk`) and resolves nothing, so
; it records the 0 and says nothing; `EXS-E0309` is raised in Stage 2 against
; the `Externus` node's span, and since diag/render.inc quotes the source line
; the offending spelling does not have to be kept anywhere in the tree.
AST_ABI_UNKNOWN		= 0
AST_ABI_SYSV_AMD64	= 1
AST_ABI_AAPCS64		= 2
AST_ABI_LP64D		= 3
AST_ABI_COUNT		= 3

; ---- operators -------------------------------------------------------------
; `Binary.aux`, in spec §8.6's precedence-table order (level 9 down to level
; 4), then `Unary.aux`. `Contrahe.aux` is an `ArithOp`, which spec §8.6
; defines as a subset of these same binary numbers -- one numbering, not two.
AST_OP_VEL	= 1
AST_OP_ET	= 2
AST_OP_LT	= 3
AST_OP_LE	= 4
AST_OP_GT	= 5
AST_OP_GE	= 6
AST_OP_EQ	= 7
AST_OP_NE	= 8
AST_OP_RANGE	= 9	; `..`
AST_OP_ADD	= 10	; `+`
AST_OP_ADDW	= 11	; `+%`
AST_OP_ADDS	= 12	; `+|`
AST_OP_SUB	= 13	; `-`
AST_OP_SUBW	= 14	; `-%`
AST_OP_SUBS	= 15	; `-|`
AST_OP_MUL	= 16	; `*`
AST_OP_MAX	= 16

AST_U_NEG	= 1	; `-`
AST_U_ADDR	= 2	; `&`
AST_U_DEREF	= 3	; `*`

; ---- literal classes -------------------------------------------------------
; `Lit.aux`. typed-ast.md section 2.3: `Lit` keeps TEXT, not value -- the
; numeric grammar is `[OPEN]` (spec §8.4) and a value needs a width the
; checker supplies. `Lit.a` is therefore an interner id in both classes.
AST_LIT_INT	= 1
AST_LIT_STRING	= 2

; ---- type-node kinds -------------------------------------------------------
; typed-ast.md section 2.5's table, top to bottom. `AST_TY_ERROR` is id 1 in
; `Ast.types` by construction (ast/node.inc interns it first), which is what
; makes section 2.2's cascade-suppression rule checkable.
AST_TY_ERROR	= 1
AST_TY_INT	= 2
AST_TY_FLOAT	= 3
AST_TY_MENSURA	= 4
AST_TY_TEXTUS	= 5
AST_TY_OCTETI	= 6
AST_TY_SCALARES	= 7
AST_TY_GRAPHA	= 8
AST_TY_PTR	= 9
AST_TY_REF	= 10
AST_TY_REFC	= 11
AST_TY_BORROW	= 12
AST_TY_ACIES	= 13
AST_TY_STRUCT	= 14
AST_TY_ALIAS	= 15
AST_TY_IFACE	= 16
AST_TY_PARAM	= 17
AST_TY_FN	= 18
AST_TY_DYN	= 19
AST_TY_EVENTUS	= 20
AST_TY_BRAND	= 21
AST_TY_RED	= 22
AST_TY_UNIT	= 23
AST_TY_CAP	= 24	; `a` = the atom's declaration (AST_D_CAPATOM). NOT in
			; typed-ast.md section 2.5's table: `m: Mundus` and
			; `sock: rete` (spec §4.3) are ordinary type
			; annotations and had no kind at all, and a `sub`'s
			; `Decl.ty` needs one too. docs/design/checker.md
			; section 9, finding 1.
AST_TY_MAX	= 24

; `AstType.sign`, `.order`, `.place`. Byte order and placement are FIELDS OF
; THE TYPE NODE and part of the interning key: that is what makes spec §5.2's
; "byte order is in the type" and spec §5.5's "placement is in the type"
; literally true -- `u32:maior` and `u32` get different ids because these
; bytes differ (typed-ast.md section 2.5).
AST_SIGN_U	= 0
AST_SIGN_I	= 1
AST_ORD_NATIVUS	= 0
AST_ORD_MAIOR	= 1
AST_ORD_MINOR	= 2
AST_PLACE_HOST	= 0
AST_PLACE_MACHINA = 1

; ---- `numeri` axis indices -------------------------------------------------
; `Ast.numeri` is four bytes -- the module's, not any node's (typed-ast.md
; section 2.7) -- and spec §5.4's table fixes both the four axes and their
; order. The VALUES on each axis are the driver's to assign from the parsed
; `ego`; this file numbers the axes only, because spec §5.4 names two
; rounding modes beyond the default and leaves the rest of each axis open.
AST_NUM_ROTUNDATIO	= 0
AST_NUM_REASSOCIATIO	= 1
AST_NUM_CONTRACTIO	= 2
AST_NUM_SUBNORMALES	= 3

; ---- the operand-role table ------------------------------------------------
; Four role bytes per node kind, in slot order (`a b c d`), indexed
; `(kind - 1) * 4`. This is typed-ast.md section 2.3's table restated in a
; form ast/verify.inc can walk.
;
; SIX PLACES THIS TABLE IS NARROWER THAN THAT DOCUMENT'S, each because that
; document groups kinds into one row and the grouping over-admits. Recorded
; here rather than absorbed silently, because the table is `[OPEN]` design and
; this file is the first thing to exercise it:
;
;   1. `Param` `GenericParam` `Field` share a row reading "`T` or 0 (bound)".
;      Only `GenericParam` is optional: spec §8.6 writes `Param ::= IDENT ':'
;      Type` and `Field ::= IDENT ':' Type` with the type REQUIRED, and
;      `GenericParam ::= IDENT [':' Type]` with it optional. So `Param.b` and
;      `Field.b` are AST_R_NODEQ and `GenericParam.b` is AST_R_NODE. A
;      missing type after a parse error is an `Error` node, which is still a
;      node -- `0` there would be a builder bug, and now it is caught.
;   2. `Potestas` shares a row with `Struct` and `Interface`, which gives it a
;      `Generics` slot and a `transitus` `aux` bit. Spec §8.6's `PotestasDecl
;      ::= 'potestas' IDENT '=' '{' [Path (',' Path)*] '}'` admits neither.
;      `Potestas.c` is AST_R_NONE here.
;   3. `Redde` `Rumpe` `Perge` `ExprStmt` share a row reading "`E` or 0".
;      Spec §8.6's `JumpStmt ::= 'redde' [Expr] ';' | 'rumpe' ';' | 'perge'
;      ';'` gives `rumpe` and `perge` no expression at all, and an expression
;      statement always has one. Only `Redde.a` is optional.
;   4. `Unary` shares a row with `Binary`, giving it a second operand. It has
;      one; `Unary.b` is AST_R_NONE.
;   5. `TyPath` shares a row with `TyDyn`, whose `b` is the `dyn` row. Spec
;      §8.6's `CoreType ::= ... | Path [GenericArgs]` gives a path type no
;      row (generic arguments hang off the `Seg`), so `TyPath.b` is
;      AST_R_NONE.
;   6. `RowItem.d` is documented as "resolved: capability decl OR param
;      ordinal". An ordinal is not a declaration id, so the verifier cannot
;      range-check it as one; the slot is AST_R_ANY and the ordinal rule is
;      typed-ast.md section 3's Stage 2 check, which needs the enclosing
;      `Sig` and is not a per-node fact.
;
; And THREE places it is WIDER. `ForHead.d` carries the `forma` WIDTH,
; `Call.d` carries the CHOSEN IMPLS of a generic call, and `Member.c` carries
; GENERIC ARGUMENTS.
;
; `Call.d` was AST_R_NONE -- typed-ast.md section 2.3 leaves the slot blank --
; and is now AST_R_ANY, on docs/design/checker.md section 2.5's requirement:
; after Stage 2 it holds "the `extra` offset of the impl decls chosen for each
; generic argument, in generic-parameter order" (that document's section 2.7,
; for spec §15 item 5's dictionary layout). AST_R_ANY, not AST_R_LIST: the
; COUNT lives in the callee's `Generics`, not in a neighbouring slot, and
; `Call.c` is already the argument count. Stage 1 still writes 0 there -- what
; changed is that a checker may write something else, which under AST_R_NONE
; would have tripped `__ast_slot_check`'s `rassert`. Spec §8.6 admits `Suffix ::=
; GenericArgs` after any path segment, and a segment reached through `.` in
; expression position is an `AST_MEMBER`, whose row in that table has no slot
; for them at all -- `Seg.b` covers only the segments of a written `Path`. The
; alternative to adding the slot was dropping the arguments of a program the
; grammar admits. See
; ast/ast.inc's header -- spec §8.6 now reads `['forma' IDENT [INT]]` and
; typed-ast.md section 2.7 ("the tree reserves nothing because the grammar
; admits nothing") is stale.
ast_roles:
	db AST_R_LIST,  AST_R_COUNT, AST_R_NONE,  AST_R_NONE	;  1 Module
	db AST_R_NODEQ, AST_R_NODE,  AST_R_NODE,  AST_R_DECL	;  2 Fn
	db AST_R_LIST,  AST_R_COUNT, AST_R_NODE,  AST_R_NODE	;  3 Sig
	db AST_R_LIST,  AST_R_COUNT, AST_R_NONE,  AST_R_NONE	;  4 Generics
	db AST_R_NAME,  AST_R_NODEQ, AST_R_NONE,  AST_R_DECL	;  5 Param
	db AST_R_NAME,  AST_R_NODE,  AST_R_NONE,  AST_R_DECL	;  6 GenericParam
	db AST_R_NAME,  AST_R_NODEQ, AST_R_NONE,  AST_R_DECL	;  7 Field
	db AST_R_LIST,  AST_R_COUNT, AST_R_NODE,  AST_R_DECL	;  8 Struct
	db AST_R_LIST,  AST_R_COUNT, AST_R_NODE,  AST_R_DECL	;  9 Interface
	db AST_R_LIST,  AST_R_COUNT, AST_R_NONE,  AST_R_DECL	; 10 Potestas
	db AST_R_NODEQ, AST_R_LIST,  AST_R_COUNT, AST_R_DECL	; 11 Impl
	db AST_R_NODEQ, AST_R_NODEQ, AST_R_NODE,  AST_R_NONE	; 12 ImplHead
	db AST_R_NODEQ, AST_R_NONE,  AST_R_NODE,  AST_R_DECL	; 13 Typus
	db AST_R_LIST,  AST_R_COUNT, AST_R_NAME,  AST_R_DECL	; 14 Externus
	db AST_R_NODE,  AST_R_NODE,  AST_R_NONE,  AST_R_DECL	; 15 Binding
	db AST_R_LIST,  AST_R_COUNT, AST_R_NONE,  AST_R_NONE	; 16 Row
	db AST_R_ROWI,  AST_R_NONE,  AST_R_NONE,  AST_R_ANY	; 17 RowItem
	db AST_R_LIST,  AST_R_COUNT, AST_R_DECL,  AST_R_NONE	; 18 Block
	db AST_R_NODEQ, AST_R_NODEQ, AST_R_NODE,  AST_R_DECL	; 19 Sub
	db AST_R_NODE,  AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 20 Redde
	db AST_R_NONE,  AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 21 Rumpe
	db AST_R_NONE,  AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 22 Perge
	db AST_R_NODEQ, AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 23 ExprStmt
	db AST_R_NODEQ, AST_R_NODEQ, AST_R_NONE,  AST_R_NONE	; 24 Assign
	db AST_R_LIST2, AST_R_COUNT, AST_R_NODE,  AST_R_NONE	; 25 Si
	db AST_R_NODEQ, AST_R_NODE,  AST_R_NODEQ, AST_R_NONE	; 26 Dum
	db AST_R_NODEQ, AST_R_NODEQ, AST_R_NODE,  AST_R_DECL	; 27 For
	db AST_R_LIST,  AST_R_COUNT, AST_R_NAME,  AST_R_ANY	; 28 ForHead
	db AST_R_NAME,  AST_R_NONE,  AST_R_NONE,  AST_R_DECL	; 29 Contrahe
	db AST_R_NODEQ, AST_R_LIST,  AST_R_COUNT, AST_R_NODE	; 30 Discerne
	db AST_R_NODEQ, AST_R_NODEQ, AST_R_NONE,  AST_R_NONE	; 31 Casus
	db AST_R_NAME,  AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 32 Lit
	db AST_R_LIST,  AST_R_COUNT, AST_R_NONE,  AST_R_DECL	; 33 Path
	db AST_R_NAME,  AST_R_NODE,  AST_R_NONE,  AST_R_DECL	; 34 Seg
	db AST_R_NODEQ, AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 35 Unary
	db AST_R_NODEQ, AST_R_NODEQ, AST_R_NONE,  AST_R_NONE	; 36 Binary
	db AST_R_NODEQ, AST_R_NODEQ, AST_R_NONE,  AST_R_NONE	; 37 Cast
	db AST_R_NODEQ, AST_R_NODEQ, AST_R_NONE,  AST_R_NONE	; 38 Index
	db AST_R_NODEQ, AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 39 Try
	db AST_R_NODEQ, AST_R_LIST,  AST_R_COUNT, AST_R_ANY	; 40 Call
	db AST_R_NODEQ, AST_R_NAME,  AST_R_NODE,  AST_R_DECL	; 41 Member
	db AST_R_NODEQ, AST_R_NODEQ, AST_R_NONE,  AST_R_DECL	; 42 Lambda
	db AST_R_NODEQ, AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 43 TyPath
	db AST_R_NODEQ, AST_R_NODE,  AST_R_NONE,  AST_R_NONE	; 44 TyDyn
	db AST_R_NONE,  AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 45 TyBit
	db AST_R_NODEQ, AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 46 TyPtr
	db AST_R_LIST,  AST_R_COUNT, AST_R_NODEQ, AST_R_NODE	; 47 TyFn
	db AST_R_NODEQ, AST_R_NAME,  AST_R_NONE,  AST_R_NONE	; 48 TySuffix
	db AST_R_LIST,  AST_R_COUNT, AST_R_NONE,  AST_R_NONE	; 49 GenericArgs
	db AST_R_NONE,  AST_R_NONE,  AST_R_NONE,  AST_R_NONE	; 50 Error
AST_ROLES_LEN = $ - ast_roles
assert AST_ROLES_LEN = AST_KIND_MAX * 4

; ---- name pools ------------------------------------------------------------
; `db LEN, 'Name'` entries in id order, walked by ast/dump.inc to print a name
; and by ast/load.inc to read one back. A pool, not a pointer array, so the
; table costs one byte of overhead per entry and no relocation; walking 50
; entries to print one name is nothing next to the write syscall the dump ends
; in, and a linear scan is the only lookup whose order cannot depend on a hash
; (CLAUDE.md, "Determinism is not optional" -- the same argument
; lexer/keywords.inc makes for not hashing 30 keywords).
;
; THE DUMP PRINTS NAMES, NOT NUMBERS, and that is the point of these tables:
; a tree hand-written into a `.ast` file stays readable when this file is
; renumbered, and `docs/design/ssa-ir.md` 2.11's argument for a textual form
; -- "a hand-written function ... is all a backend test needs" -- only works
; if the text does not encode this file's integers.
ast_kind_names:
	db 6,'Module'
	db 2,'Fn'
	db 3,'Sig'
	db 8,'Generics'
	db 5,'Param'
	db 12,'GenericParam'
	db 5,'Field'
	db 6,'Struct'
	db 9,'Interface'
	db 8,'Potestas'
	db 4,'Impl'
	db 8,'ImplHead'
	db 5,'Typus'
	db 8,'Externus'
	db 7,'Binding'
	db 3,'Row'
	db 7,'RowItem'
	db 5,'Block'
	db 3,'Sub'
	db 5,'Redde'
	db 5,'Rumpe'
	db 5,'Perge'
	db 8,'ExprStmt'
	db 6,'Assign'
	db 2,'Si'
	db 3,'Dum'
	db 3,'For'
	db 7,'ForHead'
	db 8,'Contrahe'
	db 8,'Discerne'
	db 5,'Casus'
	db 3,'Lit'
	db 4,'Path'
	db 3,'Seg'
	db 5,'Unary'
	db 6,'Binary'
	db 4,'Cast'
	db 5,'Index'
	db 3,'Try'
	db 4,'Call'
	db 6,'Member'
	db 6,'Lambda'
	db 6,'TyPath'
	db 5,'TyDyn'
	db 5,'TyBit'
	db 5,'TyPtr'
	db 4,'TyFn'
	db 8,'TySuffix'
	db 11,'GenericArgs'
	db 5,'Error'
AST_KIND_NAMES_LEN = $ - ast_kind_names

ast_dkind_names:
	db 2,'Fn'
	db 5,'Param'
	db 12,'GenericParam'
	db 6,'Struct'
	db 5,'Field'
	db 5,'Typus'
	db 9,'Interface'
	db 6,'Member'
	db 4,'Impl'
	db 8,'Potestas'
	db 8,'Externus'
	db 8,'ExternFn'
	db 7,'Binding'
	db 3,'Sub'
	db 7,'LoopVar'
	db 5,'Accum'
	db 6,'Lambda'
	db 7,'CapAtom'
AST_DKIND_NAMES_LEN = $ - ast_dkind_names

; Lower case, matching typed-ast.md section 2.5's own table and
; docs/design/ssa-ir.md 2.11's type spelling.
ast_tkind_names:
	db 5,'error'
	db 3,'int'
	db 5,'float'
	db 7,'mensura'
	db 6,'textus'
	db 6,'octeti'
	db 8,'scalares'
	db 6,'grapha'
	db 3,'ptr'
	db 3,'ref'
	db 4,'refc'
	db 6,'borrow'
	db 5,'acies'
	db 6,'struct'
	db 5,'alias'
	db 5,'iface'
	db 5,'param'
	db 2,'fn'
	db 3,'dyn'
	db 7,'eventus'
	db 5,'brand'
	db 3,'red'
	db 4,'unit'
	db 3,'cap'
AST_TKIND_NAMES_LEN = $ - ast_tkind_names

; ---- capability atoms ------------------------------------------------------
; SPEC §4.6's ELEVEN ATOMS, IN THAT SECTION'S OWN ORDER, and the order is the
; content: `docs/design/checker.md` section 2.1 gives every scope frame a
; `cap[11]` indexed by the §4.6 ordinal, section 2.2's pass 0 gives the atom
; whose ordinal is `k` the declaration id `N + k`, and section 2.4's row
; sorting is "a bit scan once atom ids are consecutive". Permuting this pool
; therefore silently permutes every row in the language -- which is what
; tests/unit/ast_cap_atoms.asm asserts against, atom by atom.
;
; These are IDENTIFIERS IN THE CAPABILITY NAMESPACE (spec §8.4 tier 3), not
; reserved words: nothing in lexer/keywords.inc knows them, `rete` is a legal
; variable name, and the only thing that makes one an atom is a declaration
; of kind AST_D_CAPATOM whose `name` interns to the bytes below. Three are
; capitalised -- `Mundus`, `Filum`, `Crudum` -- exactly as §4.6 writes them;
; §8.1's NFC rule makes byte comparison the whole of identity here.
;
; A twelfth atom is a spec amendment to §4.6, not an edit to this table: §4.6
; closes with the standard streams belonging to `ambitus` "rather than
; spending a root on a twelfth atom".
AST_CAP_MUNDUS		= 1
AST_CAP_ALLOC		= 2
AST_CAP_SERMO		= 3
AST_CAP_HOROLOGIUM	= 4
AST_CAP_ARCHIVUM	= 5
AST_CAP_RETE		= 6
AST_CAP_FORTUNA		= 7
AST_CAP_AMBITUS		= 8
AST_CAP_FILUM		= 9
AST_CAP_MACHINA		= 10
AST_CAP_CRUDUM		= 11
AST_CAP_COUNT		= 11

ast_cap_names:
	db 6,'Mundus'
	db 5,'alloc'
	db 5,'sermo'
	db 10,'horologium'
	db 8,'archivum'
	db 4,'rete'
	db 7,'fortuna'
	db 7,'ambitus'
	db 5,'Filum'
	db 7,'machina'
	db 6,'Crudum'
AST_CAP_NAMES_LEN = $ - ast_cap_names

; ---- ABI spellings ---------------------------------------------------------
; Spec §5.3's three, in §5.3's order, with ONE ENTRY AHEAD OF THEM so that
; the pool index is `AST_ABI_* + 1`. That off-by-one is deliberate and does
; two jobs at once:
;
;   - ast/dump.inc prints `Externus.aux` through this pool instead of as a
;     bare integer, so `ignotus` and `sysv_amd64` are what a reader of a dump
;     sees -- the same argument the kind pools make ("THE DUMP PRINTS NAMES,
;     NOT NUMBERS", above), and it applies here and not to the other `aux`
;     enumerations because these three spellings are the SPEC's, written in
;     source text, while `Binary.aux`'s operator numbering is internal and no
;     program names it;
;   - ast/from_cst.inc recognises the spelling with `ast_name_id` over this
;     pool, whose miss answer is 0 and whose hit answer is the 1-based index.
;     `abi: ignotus` therefore lands on entry 1 and `abi: quidlibet` misses,
;     and BOTH give `aux` 0, which is the right answer for each.
ast_abi_names:
	db 7,'ignotus'		; AST_ABI_UNKNOWN -- see the note above
	db 10,'sysv_amd64'
	db 7,'aapcs64'
	db 5,'lp64d'
AST_ABI_NAMES_LEN = $ - ast_abi_names
