; compiler/x86_64/checker/resolve/resolve.inc
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
; STAGE 2, PASSES 0 AND 1 -- `docs/design/checker.md` sections 2.1, 2.2 and 6's
; `checker-resolve` row: find the capability atoms, then resolve every name.
;
; WHAT THIS FILE INCLUDES: nothing. Same contract as ast/ast.inc's -- a
; consumer includes diag/diag.inc (or lexer/lexer.inc, which brings it) and
; ast/ast.inc FIRST, and this file assumes `proc`, `rassert`, `struct`, the
; whole of rt/, `Diag` and the `Ast*` records are already defined. Including
; any of them here would be the second path that turns the include graph into
; a diamond, which fasmg's one flat namespace does not survive.
;
; ---------------------------------------------------------------------------
; THE KILL CRITERION (spec §16: "if `sub` resolution needs a search algorithm,
; redesign"). checker.md section 2.1's answer is implemented literally here:
; a stack of frames, one per open `Block` / `Fn` root / `Lambda` root, each
; holding `caps[11]` -- one slot per spec §4.6 atom, in that section's own
; order, holding the declaration that PROVIDES that atom in that scope, or 0.
; A draw is one outward walk over those slots. There is no candidate set, no
; unification and no backtracking; the cost is lexical depth.
;
; ---------------------------------------------------------------------------
; ONE FINDING AGAINST checker.md, FOUND BY RUNNING IT, AND THE FIX.
;
; Section 2.2's pass 1 says ordinary names resolve by "a scan of each open
; frame's decl range from the most recent *seen* decl backwards". A block's
; declaration range is `[Block.c, Block.c + Block.aux)` (AST 2.4) -- and those
; ranges NEST rather than partition: ast/verify.inc's `__ast_v_nest` proves a
; pair either nests or is disjoint, never overlaps. Measured on a real tree
; (a function holding `firma z`, then `per i in z { firma q = i; }`), the OUTER
; block's range is `[3, 8)` and the inner block's is `[7, 8)`: the outer range
; CONTAINS the inner block's declarations. A plain backwards range scan
; therefore makes an inner block's names visible in the enclosing block after
; that block has closed, as soon as the enclosing frame's cursor has advanced
; past them. That is not a scoping rule anyone wants and it is not what that
; sentence means to say.
;
; What is implemented instead, and it is smaller rather than larger: an
; explicit BIND STACK (`ChkCtx.binds`, a `Vec` of declaration ids used with
; stack discipline, exactly as ast/build.inc's staging vec is). A frame stores
; one mark into it; a frame's own bindings are `[frame.first, next_frame.first)`
; for an open inner frame and `[frame.first, binds.len)` for the innermost;
; popping a frame truncates the stack back to its mark. Nested ranges cannot
; leak, because an inner frame's binds are gone by the time the outer frame
; binds anything else. `Block.c` / `Block.aux` are not read by this pass at
; all. Reported against checker.md section 2.2, not edited there.
;
; ---------------------------------------------------------------------------
; WHAT THIS PASS RESOLVES, AND WHAT IT DELIBERATELY DOES NOT.
;
;   - Every `Path` in EXPRESSION position resolves its first `Seg` to a
;     declaration, or raises `EXS-E0301`. Order is nearest-wins: the frame
;     stack innermost-outward (capability slot first within a frame, then that
;     frame's own bindings, newest first), then the module table, then
;     `chk_prelude_lookup`. checker.md section 2.2 states nearest-wins in as
;     many words ("an inner block shadowing an ordinary name is allowed ...
;     nearest-wins is unambiguous"), and it is the order implemented.
;
;   - A `Path` in TYPE position resolves the same way but raises NOTHING when
;     it misses. Spec §5.1's own type names are not declarations anywhere in
;     this tree: `u32` is a `BitType` and becomes `TyBit`, but `i32`, `f32`,
;     `textus`, `mensura`, `octeti`, `scalares` and `grapha` all parse as
;     `TyPath` over an ordinary `Path` (measured -- `mutabilis c: i32 = 0;`
;     produces `TyPath -> Path -> Seg 'i32'`), and NOTHING in this repository
;     declares them. Recognising those spellings is pass 2's job: checker.md
;     section 2.2 gives pass 2 the interning of `Ty*` subtrees into
;     `Ast.types`, and `AST_TY_INT` / `AST_TY_FLOAT` / `AST_TY_TEXTUS` are
;     type KINDS, not declarations. Raising `EXS-E0301` here would report every
;     `i32` in every program as an unresolved name. So: resolved when a
;     declaration exists, 0 otherwise, no diagnostic -- and typed-ast.md
;     section 3's Stage 2 clause "`d` != 0 on every `Seg`" is completed by
;     pass 2, not here (checker/verify.inc gates that clause on CHK_S_TYPES).
;
;   - `Member.d` gets a CANDIDATE and never a diagnostic. Resolving `x.m` for
;     real needs `x`'s type (checker.md section 2.2: "`Member.d` is *not*
;     resolved here"), which is pass 2's. What this pass can do without a type
;     is record the unique module-wide `AST_D_MEMBER` declaration of that name
;     when there is exactly one -- §8.6 decision 8's "the type's members, then
;     the interfaces it implements" collapses to one answer in that case. Zero
;     or two candidates leave the slot 0. The types pass OVERWRITES this slot;
;     it is a hint, not an answer. It is a MAP lookup, not a scan: a scan over
;     every declaration per member expression is precisely the shape spec §16's
;     kill criterion is about, so `ChkCtx.membs` is built once, in declaration
;     order, and a second declaration of one member name stores a sentinel that
;     makes the name ambiguous rather than making the lookup search.
;
;   - `RowItem.d` on a `sicut IDENT` item stays 0: the ordinal needs the
;     enclosing `Sig`'s parameter list matched by name, and spec §4.2's
;     substitution is the rows agent's (checker.md section 2.3).
;
; ---------------------------------------------------------------------------
; DIAGNOSTICS ARE BUFFERED, THEN SORTED, THEN EMITTED. checker.md section 4
; requires emission order "pass number, then node index within the pass". A
; preorder walk does not produce node-index order -- `Fn` is node 28 while its
; own body's statements are nodes 8..26 -- so a diagnostic raised at the `Fn`
; would precede every diagnostic inside it. `chk_raise` therefore appends a
; `ChkDiag` to a scratch buffer carrying (pass, node), `chk_flush` sorts that
; buffer with `sort_stable` on exactly that key, and only then builds `Diag`
; records into the caller's vector. Two runs over the same bytes produce the
; same list in the same order, which is what spec §9.3 and §8.3 ask of output.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1-§4.6 (capabilities), §4.5
; (shadowing, no cross-module inference), §8.6 (the grammar), §9.3
; (determinism), §13 (the codes; never invent one -- CLAUDE.md).
; -----------------------------------------------------------------------------
; E0301 AND RECOVERY ARTIFACTS -- `tests/diagnostics/README.md`, "The number
; moved when the checker joined the pipeline": measured on that corpus, this
; pass adds four `EXS-E0301` the pre-checker baseline never had -- c14 (+2),
; c20 (+1), c22 (+1) -- every one of them a TRUE fact about the tree (the name
; really does not resolve) that is noise to a reader, because it exists only
; as a byproduct of parser recovery from an EARLIER, already-reported
; diagnostic on the same construct. AST H3 / checker.md H3 make the same
; argument one stage later, about `Error` operands and Stage 2 typing
; ("a checker that diagnoses an `Error` operand duplicates the parser");
; this is that argument applied to pass 1, before Stage 2 exists to apply it.
;
; RULE. Pass 1 does not raise `E0301` for a `Path`/`Seg` (`__chk_path`'s
; `.ordinary` miss) or a `RowItem` `Path` (`__chk_rowitem`'s `.potestas` miss)
; that lies inside a subtree -- at ANY depth, including through a nested
; `Block` -- containing a RECOVERY ARTIFACT. The name is still RESOLVED when
; it can be (a hit is a hit regardless of taint); only a genuine miss's
; diagnostic is withheld, so later passes still see `d` = 0 exactly where
; nothing was found, same as today.
;
; TWO ARTIFACT SHAPES, MEASURED, NOT ONE. `tests/diagnostics/README.md`'s own
; closing paragraph names one: "the checker's resolve pass now walks the
; recovered tree and reports names that exist only because recovery invented
; structure around a parse error... do not raise E0301 on a name inside a
; subtree that contains an Error node." That covers c14 exactly -- dumping it
; (`--emitte ast`) shows `Error` node 11 (the abandoned `> 10`) as a sibling
; STATEMENT, inside the same function-body `Block` as the `if`/`return` `Path`
; nodes that mis-resolve, so a taint that reaches every statement in a `Block`
; whose subtree contains an `Error` node removes both. It does NOT cover c20
; or c22: measured with the same dump, NEITHER tree contains a single
; `AST_ERROR` node (`--emitte ast | grep Error` matches nothing but the
; harmless `t 1 error` type-table entry every tree carries per typed-ast.md
; §2.2). Their cascade is `interior()` failing to resolve a nested, NAMED
; `functio` -- a construct spec §8.6's peek rejects with `EXS-E0201`, but
; `cst/parse.inc`'s `.functio` handler recovers by parsing it as the complete,
; well-formed declaration it plainly is (no `Error` wrapper at all: "the
; construct is then parsed as the declaration it plainly is"). Pass 1 never
; binds a nested `Fn`'s own name into any scope (by design -- nested named
; functions are not part of the grammar), so any reference to it is a REAL
; miss, structurally identical in kind to the `Error` case (full recovery
; around an already-reported diagnostic) but not identical in shape. Recorded
; here rather than silently folded into "the `Error`-node rule", because
; `tests/diagnostics/README.md`'s prose and the measured tree disagree, and
; CLAUDE.md's evidence discipline says which: the prose is the one that is
; wrong (or, charitably, incomplete) -- it should read "...or a nested `Fn`
; declaration the peek recovered around," and whoever owns that file next
; should amend it (out of this file's scope: `tests/diagnostics/` is not in
; this pass's write scope, only `checker/resolve/` and its own `tests/unit/`
; fixtures are).
;
; MECHANISM AND COST. One pass over every node, `__chk_taint_pass`, run once
; before the walk: `ChkCtx.taint` is a flat `u8[nodes+1]`, and node N's byte is
; 1 iff N is itself an artifact (`AST_ERROR`, or a nested-`Fn`-in-`Fn`
; declaration -- `__chk_is_nested_fn`) OR any child slot `ast_roles` names for
; N's kind already has its byte set. Postorder (typed-ast.md §2.1: "children
; are emitted before their parent") makes this a single forward scan, id 1..N,
; rather than a recursive walk or a second per-node subtree search: every
; child's id is smaller than N's, so its answer is already in the array.
; `__chk_tainted` then costs O(frame depth) -- it asks whether ANY currently
; open frame's node (or, with no frame open, the module root) is tainted, the
; same "ancestor, not just the immediate parent" shape `__chk_lookup` already
; walks for names, and IS how a `Block` nested inside the tainted one (c14's
; `return`, one `Block` below the `Error` sibling) still gets suppressed. Net
; cost: one extra O(nodes) pass, paid once per run, plus O(depth) per E0301
; candidate -- not a second walk per name, and no search over declarations.
; -----------------------------------------------------------------------------

; ---- flags, kinds, capacities ----------------------------------------------

; `ChkCtx.flags`. Bit 0 is the one fact the checker cannot derive from the
; tree: whether a PROGRAM was asked for. Spec §4.1 rule 2 and §13's `E0424`
; make "no `initium`" an error only then -- "A module with no `initium` is a
; library". The driver knows, from `-o`; the tree does not.
CHK_F_PROGRAM	= 0x01

; `ChkFrame.kind`.
CHK_FR_BLOCK	= 1
CHK_FR_FN	= 2	; a function root -- parameters and the declared row live here
CHK_FR_LAMBDA	= 3	; a lambda root -- crossing one outward records a capture
CHK_FR_TYDECL	= 4	; a `typus` / `structura` / `interfacies` root -- its own
			; `[GenericParams]` (spec §8.6's `TypeDecl`, `StructDecl`
			; and `InterfaceDecl` all admit them) live here and are
			; visible over its body and NOWHERE else. It is not a
			; function root, so it does not stop `__chk_lookup`'s
			; outward walk and `__chk_frame_root` skips it the way it
			; skips a `BLOCK`: `ChkFrame.node` is a `Typus`/`Struct`/
			; `Interface` node, which has no `Sig` to insert a row
			; into.

; `ChkUse.kind` -- what pass 3 (the rows agent) is being handed.
CHK_USE_DRAW	= 1	; an atom drawn where no frame provides it, in a function
			; whose row is INFERRED: a contribution to that row
CHK_USE_CAPTURE	= 2	; a draw that crossed a `Lambda` root on its way out: a
			; capture, and a contribution to that lambda's row

; `chk_verify`'s stage mask -- which passes have actually run. checker.md
; section 2.5's post-Stage-2 invariant is the conjunction of all of them, and a
; verifier that asserted the whole list after pass 1 alone would trap on every
; tree. Later agents OR their bit in and extend `chk_verify` (see that file).
CHK_S_RESOLVE	= 0x01
CHK_S_TYPES	= 0x02
CHK_S_ROWS	= 0x04
CHK_S_LAYOUT	= 0x08
CHK_S_LEXICON	= 0x10

; Pass numbers, for `chk_raise`'s ordering key (checker.md section 2.2's table).
CHK_P_RESOLVE	= 1
CHK_P_TYPES	= 2
CHK_P_ROWS	= 3
CHK_P_LAYOUT	= 4
CHK_P_LEXICON	= 5

; `ChkCtx.membs`: a member name mapping to this means "declared more than once
; across the module's types" -- ambiguous without a receiver type, so the
; candidate is withheld. Not a declaration id and never mistaken for one:
; `Ast.decls` cannot hold 2^32-1 entries in a 16 MiB arena.
CHK_MEMB_AMBIG	= 0xFFFFFFFF

CHK_FRAMES_CAP0	= 32
CHK_BINDS_CAP0	= 128
CHK_DIAGS_CAP0	= 16
CHK_USES_CAP0	= 32
CHK_MODS_BUCKETS_MIN = 16

; ---- the scope frame -------------------------------------------------------
; 60 bytes, which is what checker.md section 2.1 estimated for it. `caps` is
; indexed by (spec §4.6 ordinal - 1); ast/kinds.inc's `AST_CAP_*` constants ARE
; those ordinals and `ast_cap_of_name` returns one.
struct ChkFrame
	field node,  dd		; the `Block` / `Fn` / `Lambda` node this opened at
	field first, dd		; mark into `ChkCtx.binds` -- see this file's header
	field owner, dd		; the enclosing `Fn`/`Lambda` DECLARATION, 0 at
				; module level; copied down into `BLOCK` frames so
				; a draw knows whose row it contributes to
	field kind,  db		; CHK_FR_*
	field pad0,  db
	field pad1,  dw
	caps dd 11 dup ?
end struct
assert sizeof.ChkFrame = 60

; ---- a buffered diagnostic -------------------------------------------------
; 80 bytes. Everything `diag_init` / `diag_note_set` / `diag_rel_set` and one
; `diag_fix_*` constructor need, held until `chk_flush` can put them in
; checker.md section 4's order. `hasrel` and `fkind` are the two "is this field
; meaningful" bits, spelled as words rather than packed: the record is scratch
; and never crosses a boundary where its size matters.
struct ChkDiag
	field pass,   dd	; CHK_P_* -- the primary sort key
	field ord,    dd	; the node index -- the secondary sort key
	field code,   dd	; the §13 numeric part (the CF/eax value)
	field note,   dd	; DIAG_NOTE_* or DIAG_NOTE_NONE
	field fkind,  dd	; DIAG_FIX_*
	field hasrel, dd	; 1 if `rel` carries a related span
	caret Span
	rel   Span
	fx    Span
	field pad0,   dd
	field ftxt,   dq	; fix replacement text -- compiler-owned, never source
	field ftxtn,  dq
end struct
assert sizeof.ChkDiag = 80

; ---- a capability use pass 3 has to account for ----------------------------
; 16 bytes. checker.md section 2.2's pass 3 "first builds a use list in walk
; order ... and the fixpoint iterates the list, never the tree". This is the
; part of that list pass 1 can fill honestly: every DIRECT atom draw that
; reached a function root with no provider, and every lambda root such a draw
; crossed. Call-induced contributions -- an atom in a callee's row after
; substitution -- need the callee's type and are the rows agent's to append.
struct ChkUse
	field owner, dd		; the `Fn`/`Lambda` DECLARATION the draw contributes
				; to, 0 at module level
	field node,  dd		; the drawing node (a `Seg`)
	field ord,   dd		; spec §4.6 ordinal, 1..11
	field kind,  db		; CHK_USE_*
	field pad0,  db
	field pad1,  dw
end struct
assert sizeof.ChkUse = 16

; ---- the checker context ---------------------------------------------------
; Mirrors `Lexer`'s shape deliberately: `chk_init` takes what the checker
; OWNS, `chk_set_source` takes the borrowed text a diagnostic points into, and
; `chk_run` (checker/checker.inc) takes only the context. checker.md section 6
; wrote the entry point as `chk_run(tree, diags, arena)`; three arguments
; cannot carry the source text `diag_attach_source` needs (a checker
; diagnostic with no snippet is strictly worse output) nor the
; program-was-asked-for bit `E0424` turns on, and `lex_init` / `lex_set_source`
; / `lex_run` is the shape this tree already uses for exactly that pair of
; needs. Reported as a deviation.
struct ChkCtx
	field tree,    dq	; -> Ast. Annotated in place; never rebuilt.
	field diags,   dq	; -> Vec of `Diag`, the caller's
	field arena,   dq	; -> Arena, SCRATCH: the frame stack, the bind
				; stack, the buffered diagnostics, the use list and
				; the two tables. Safe to reset once `chk_run` has
				; returned AND every `Diag` has been rendered.
	field src,     dq	; borrowed source text (may be 0)
	field srcn,    dq
	field path,    dq	; borrowed file name (may be 0)
	field pathn,   dq
	field keys,    dq	; -> u32[decls+1]: STABLE map keys. `rt/map.inc`
				; borrows keys and `vec_push` moves its block, so a
				; key may not point into a `Vec` -- one flat
				; `arena_alloc` that never grows is the fix, the
				; same one ast/types.inc made for its type keys.
	field taint,   dq	; -> u8[nodes+1], ONE flat `arena_alloc`, computed
				; once by `__chk_taint_pass` before the walk: 1 if
				; that node's own subtree contains an `E0301`
				; RECOVERY ARTIFACT (this file's header, "E0301 AND
				; RECOVERY ARTIFACTS"). Read by `__chk_tainted`,
				; never grown, never mutated after the pass runs.
	field flags,   dd	; CHK_F_*
	field ndiag,   dd	; diagnostics this run has emitted
	field capbase, dd	; `ast_cap_base(tree)`; 0 if the tree has no atoms
	field nmain,   dd	; `initium` declarations seen at module level
	field mainid,  dd	; the FIRST one's declaration id
	field entryid, dd	; the interner id of `initium`, 0 if never interned
	field owner,   dd	; the enclosing `Fn`/`Lambda` DECLARATION, 0 at
				; module level
	field rowdecl, dd	; 1 while the enclosing function's row is DECLARED
				; (an explicit `poscit`, or `publica` -- checker.md
				; section 2.3), 0 while it is inferred
	field stages,  dd	; CHK_S_* -- what has run, for `chk_verify`
	field nsupp,   dd	; E0301s WITHHELD by the taint rule below. Not a
				; diagnostic count -- a record that this tree holds
				; names the checker deliberately did not resolve and
				; deliberately said nothing about, which is a state
				; `chk_verify`'s "unresolved is legal only in type
				; position" clause otherwise forbids.
	field ptrbits, dd	; pointer width of --hospes in bits (64 for x86_64-linux);
				; set by `chk_set_target`, read by pass 4. 0 = never set,
				; and pass 4 rasserts: spec §9.5 forbids a default.
	frames Vec		; ChkFrame
	binds  Vec		; dd -- declaration ids, stack discipline
	dbuf   Vec		; ChkDiag
	draws  Vec		; ChkUse
	mods   Map		; 4 key bytes (a name id) -> module-scope decl id
	membs  Map		; 4 key bytes (a name id) -> member decl id, or
				; CHK_MEMB_AMBIG
end struct

; ---- literal fragments -----------------------------------------------------
; Between two `proc` bodies, never fallen into -- the placement diag/render.inc
; and ast/kinds.inc both use.
chk_lit_firma	db 'firma'
CHK_LIT_FIRMA_LEN = $ - chk_lit_firma
chk_lit_initium	db 'initium'
CHK_LIT_INITIUM_LEN = $ - chk_lit_initium
chk_lit_poscit	db ' poscit '
CHK_LIT_POSCIT_LEN = $ - chk_lit_poscit
chk_lit_poscit_bare	db 'poscit '
CHK_LIT_POSCIT_BARE_LEN = $ - chk_lit_poscit_bare
chk_lit_comma	db ', '
CHK_LIT_COMMA_LEN = $ - chk_lit_comma
; `mutabilis` is 9 bytes and a module-level `Binding` node's span starts at it
; (measured: `mutabilis c: i32 = 0;` is span 0..21 on the `Binding`). That is
; what makes `E0500`'s required fix -- §8.3 promises one, `diag_fix_required`
; lists 500 -- derivable without a keyword span the tree does not keep. The
; CONSUMER is pass 3 (`__chk_row_e0501`, checker/rows/compute.inc), not this
; file: the fix text lives with the other `E05xx` literals and the code that
; chooses between `E0500` and `E0501` lives in the pass that can.
CHK_MUTABILIS_LEN = 9

; =============================================================================
; construction
; =============================================================================

; chk_init(chk_c, chk_t, chk_dg, chk_ar, chk_fl) -- `chk_t` is the Stage 1 tree
; (`ast_verify_stage1` must already have accepted it), `chk_dg` the caller's
; `Vec` of `Diag`, `chk_ar` a scratch arena this checker owns, `chk_fl` a set of
; CHK_F_* bits.
;
; The tables' bucket count is computed from the declaration count rather than
; fixed, which is checker.md section 5's own note ("the module table's bucket
; count should be the power of two >= 2 x item count, computed from
; `Ast.decls.len`, not a constant"). `rt/map.inc` requires a power of two and
; `rassert`s it.
proc chk_init, chk_c, chk_t, chk_dg, chk_ar, chk_fl
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chk_c]
	mov	rax, [chk_t]
	mov	[rbx + ChkCtx.tree], rax
	mov	rax, [chk_dg]
	mov	[rbx + ChkCtx.diags], rax
	mov	rax, [chk_ar]
	mov	[rbx + ChkCtx.arena], rax
	mov	qword [rbx + ChkCtx.src], 0
	mov	qword [rbx + ChkCtx.srcn], 0
	mov	qword [rbx + ChkCtx.path], 0
	mov	qword [rbx + ChkCtx.pathn], 0
	mov	qword [rbx + ChkCtx.taint], 0	; until __chk_taint_pass
	mov	eax, [chk_fl]
	mov	[rbx + ChkCtx.flags], eax
	mov	dword [rbx + ChkCtx.ndiag], 0
	mov	dword [rbx + ChkCtx.nsupp], 0
	mov	dword [rbx + ChkCtx.capbase], 0
	mov	dword [rbx + ChkCtx.nmain], 0
	mov	dword [rbx + ChkCtx.mainid], 0
	mov	dword [rbx + ChkCtx.entryid], 0
	mov	dword [rbx + ChkCtx.owner], 0
	mov	dword [rbx + ChkCtx.rowdecl], 0
	mov	dword [rbx + ChkCtx.stages], 0
	mov	dword [rbx + ChkCtx.ptrbits], 0	; until chk_set_target

	lea	rdi, [rbx + ChkCtx.frames]
	mov	rsi, [chk_ar]
	mov	rdx, sizeof.ChkFrame
	mov	rcx, CHK_FRAMES_CAP0
	call	vec_init
	lea	rdi, [rbx + ChkCtx.binds]
	mov	rsi, [chk_ar]
	mov	rdx, 4
	mov	rcx, CHK_BINDS_CAP0
	call	vec_init
	lea	rdi, [rbx + ChkCtx.dbuf]
	mov	rsi, [chk_ar]
	mov	rdx, sizeof.ChkDiag
	mov	rcx, CHK_DIAGS_CAP0
	call	vec_init
	lea	rdi, [rbx + ChkCtx.draws]
	mov	rsi, [chk_ar]
	mov	rdx, sizeof.ChkUse
	mov	rcx, CHK_USES_CAP0
	call	vec_init

	; the stable key array: one u32 per declaration, 1-based, never grown
	mov	rdi, [chk_t]
	call	ast_decl_count
	mov	r12, rax
	inc	r12
	mov	rdi, [chk_ar]
	mov	rsi, r12
	shl	rsi, 2
	call	arena_alloc
	mov	[rbx + ChkCtx.keys], rax

	; buckets: the power of two >= 2 * declarations, floored at 16
	mov	r13, CHK_MODS_BUCKETS_MIN
	mov	rcx, r12
	shl	rcx, 1
  .grow:
	cmp	r13, rcx
	jae	.have
	shl	r13, 1
	jmp	.grow
  .have:
	lea	rdi, [rbx + ChkCtx.mods]
	mov	rsi, [chk_ar]
	mov	rdx, r13
	call	map_init
	lea	rdi, [rbx + ChkCtx.membs]
	mov	rsi, [chk_ar]
	mov	rdx, r13
	call	map_init

	; the interner id of `initium`, WITHOUT interning it: `map_get` on the
	; interner's own map is a read. `intern_id` would add the name to a
	; shared interner on every compilation of a module that never writes it.
	mov	rdi, [chk_t]
	mov	rdi, [rdi + Ast.names]
	test	rdi, rdi
	jz	.no_names
	mov	rdi, [rdi + Interner.map]
	lea	rsi, [chk_lit_initium]
	mov	rdx, CHK_LIT_INITIUM_LEN
	call	map_get
	mov	rbx, [chk_c]
	mov	[rbx + ChkCtx.entryid], eax
  .no_names:
	return
endp

; chk_set_source(chk_c, chk_sp, chk_sn, chk_pp, chk_pn) -- the text every span
; this checker reports indexes into, and the name of the file it came from.
; Borrowed, never copied, exactly as `diag_attach_source` documents.
proc chk_set_source, chk_c, chk_sp, chk_sn, chk_pp, chk_pn
	locals
	endl
	mov	rdi, [chk_c]
	mov	rax, [chk_sp]
	mov	[rdi + ChkCtx.src], rax
	mov	rax, [chk_sn]
	mov	[rdi + ChkCtx.srcn], rax
	mov	rax, [chk_pp]
	mov	[rdi + ChkCtx.path], rax
	mov	rax, [chk_pn]
	mov	[rdi + ChkCtx.pathn], rax
	return
endp

; chk_diag_count(chk_c) -> rax = diagnostics this checker has emitted.
proc chk_diag_count, chk_c
	locals
	endl
	mov	rdi, [chk_c]
	mov	eax, [rdi + ChkCtx.ndiag]
	return
endp

; chk_use_count(chk_c) -> rax = entries in the capability-use list.
proc chk_use_count, chk_c
	locals
	endl
	mov	rdi, [chk_c]
	mov	rax, [rdi + ChkCtx.draws.len]
	return
endp

; chk_use_at(chk_c, chk_i) -> rax = pointer to use `chk_i` (0-based). Valid
; until the next use is recorded; nothing records one after `chk_resolve`
; returns, so pass 3 may hold these.
proc chk_use_at, chk_c, chk_i
	locals
	endl
	mov	rdi, [chk_c]
	add	rdi, ChkCtx.draws
	mov	rsi, [chk_i]
	call	vec_get
	return
endp

; =============================================================================
; diagnostics: buffer, sort, emit
; =============================================================================

; chk_raise_span(chk_c, chk_pass, chk_code, chk_ord, chk_sp) -> rax = pointer to
; the buffered `ChkDiag`, so the caller can attach a note, a related span or a
; fix. Valid until the next `chk_raise*`.
;
; `chk_ord` is the node index the diagnostic sorts by; 0 sorts first, which is
; where a diagnostic about the module as a whole (`E0424`, absent `initium`)
; belongs. `chk_sp` points at an `rt/span.inc` `Span`.
proc chk_raise_span, chk_c, chk_pass, chk_code, chk_ord, chk_sp
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	lea	rdi, [rbx + ChkCtx.dbuf]
	call	vec_push
	mov	r12, rax
	mov	eax, [chk_pass]
	mov	[r12 + ChkDiag.pass], eax
	mov	eax, [chk_ord]
	mov	[r12 + ChkDiag.ord], eax
	mov	eax, [chk_code]
	mov	[r12 + ChkDiag.code], eax
	mov	dword [r12 + ChkDiag.note], DIAG_NOTE_NONE
	mov	dword [r12 + ChkDiag.fkind], DIAG_FIX_NONE
	mov	dword [r12 + ChkDiag.hasrel], 0
	mov	rcx, [chk_sp]
	mov	eax, [rcx + Span.file_id]
	mov	[r12 + ChkDiag.caret.file_id], eax
	mov	eax, [rcx + Span.start]
	mov	[r12 + ChkDiag.caret.start], eax
	mov	eax, [rcx + Span.len]
	mov	[r12 + ChkDiag.caret.len], eax
	mov	dword [r12 + ChkDiag.rel.file_id], 0
	mov	dword [r12 + ChkDiag.rel.start], 0
	mov	dword [r12 + ChkDiag.rel.len], 0
	mov	dword [r12 + ChkDiag.fx.file_id], 0
	mov	dword [r12 + ChkDiag.fx.start], 0
	mov	dword [r12 + ChkDiag.fx.len], 0
	mov	dword [r12 + ChkDiag.pad0], 0
	mov	qword [r12 + ChkDiag.ftxt], 0
	mov	qword [r12 + ChkDiag.ftxtn], 0
	mov	rax, r12
	return
endp

; chk_raise(chk_c, chk_pass, chk_code, chk_n) -> rax = the buffered `ChkDiag`.
; The span is node `chk_n`'s. `chk_n` = 0 means "no node": the span becomes the
; root's file with zero offset and length, and the diagnostic sorts first.
proc chk_raise, chk_c, chk_pass, chk_code, chk_n
	uses	rbx, r12
	locals
		slot	chk_sp, sizeof.Span
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_n]
	mov	dword [chk_sp + Span.file_id], 0
	mov	dword [chk_sp + Span.start], 0
	mov	dword [chk_sp + Span.len], 0
	test	r12, r12
	jz	.nonode
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	mov	ecx, [rax + AstNode.span.file_id]
	mov	[chk_sp + Span.file_id], ecx
	mov	ecx, [rax + AstNode.span.start]
	mov	[chk_sp + Span.start], ecx
	mov	ecx, [rax + AstNode.span.len]
	mov	[chk_sp + Span.len], ecx
	jmp	.have
  .nonode:
	mov	rdi, [rbx + ChkCtx.tree]
	mov	eax, [rdi + Ast.root]
	test	eax, eax
	jz	.have
	mov	rsi, rax
	call	ast_node_at
	mov	ecx, [rax + AstNode.span.file_id]
	mov	[chk_sp + Span.file_id], ecx
  .have:
	mov	rdi, rbx
	mov	rsi, [chk_pass]
	mov	rdx, [chk_code]
	mov	rcx, r12
	lea	r8, [chk_sp]
	call	chk_raise_span
	return
endp

; __chk_at_decl(chk_c, chk_dg, chk_d) -- move a buffered diagnostic's span onto
; declaration `chk_d`'s. A declaring node's span is the whole construct; the
; declaration's is the name-bearing part ast/build.inc's `ast_decl` was handed,
; which is the better place for a caret on a duplicate or a `mutabilis`.
proc __chk_at_decl, chk_c, chk_dg, chk_d
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_dg]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_d]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.span.file_id]
	mov	[r12 + ChkDiag.caret.file_id], ecx
	mov	ecx, [rax + AstDecl.span.start]
	mov	[r12 + ChkDiag.caret.start], ecx
	mov	ecx, [rax + AstDecl.span.len]
	mov	[r12 + ChkDiag.caret.len], ecx
	return
endp

; __chk_rel_decl(chk_c, chk_dg, chk_d) -- attach declaration `chk_d`'s span as
; the RELATED one: "the place a reader has to look at to understand the one the
; diagnostic is reported at" (diag/render.inc). The earlier declaration, for a
; duplicate; the first `initium`, for a second.
proc __chk_rel_decl, chk_c, chk_dg, chk_d
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_dg]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_d]
	call	ast_decl_at
	mov	dword [r12 + ChkDiag.hasrel], 1
	mov	ecx, [rax + AstDecl.span.file_id]
	mov	[r12 + ChkDiag.rel.file_id], ecx
	mov	ecx, [rax + AstDecl.span.start]
	mov	[r12 + ChkDiag.rel.start], ecx
	mov	ecx, [rax + AstDecl.span.len]
	mov	[r12 + ChkDiag.rel.len], ecx
	return
endp

; __chk_dbefore(chk_a, chk_b) -> eax = 1 if `ChkDiag` `chk_a` must be emitted
; strictly BEFORE `chk_b`: pass number, then node index (checker.md section 4).
; Both arguments are POINTERS to records.
proc __chk_dbefore, chk_a, chk_b
	locals
	endl
	mov	rcx, [chk_a]
	mov	rdx, [chk_b]
	mov	eax, [rcx + ChkDiag.pass]
	cmp	eax, [rdx + ChkDiag.pass]
	jb	.yes
	ja	.no
	mov	eax, [rcx + ChkDiag.ord]
	cmp	eax, [rdx + ChkDiag.ord]
	jb	.yes
  .no:
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; __chk_dsort(chk_c) -- put the buffered diagnostics in checker.md section 4's
; order: pass number, then node index within the pass.
;
; A HAND-ROLLED INSERTION SORT RATHER THAN `rt/sort.inc`'s `sort_stable`, and
; the reason is the include graph, not the algorithm. `rt/sort.inc` opens with
; `include 'arena.inc'`, and `arena.inc` is already in this build through
; ast/ -> ... -> rt/vec.inc; fasmg has one flat namespace, so a second path to
; it re-processes `struct Arena` and every `proc` in it and fails outright.
; That is why `sort_stable` is not reachable from `compiler/x86_64/exsc.asm`
; at all today -- driver/io.inc records the same fact about its sibling
; `rt/str.inc` ("unreachable from the..."). Reported to `asm-rt`: hoisting
; those two `include` lines, or guarding them, would let the checker use the
; tested sort instead of this one.
;
; Insertion sort is STABLE by construction -- an element moves left only past
; elements that must come strictly after it, `__chk_dbefore` above -- which is
; the property that matters: two diagnostics with the same (pass, node) keep
; the order the walk raised them in, so the emitted list is a function of the
; input bytes alone (CLAUDE.md, "Determinism is not optional"). The buffer
; holds one record per diagnostic, so the quadratic worst case is over a list
; whose length a human is expected to read.
proc __chk_dsort, chk_c
	uses	rbx, r12, r13, r14
	locals
		slot	chk_n,  dq
		slot	chk_tm, sizeof.ChkDiag
	endl
	mov	rbx, [chk_c]
	mov	rax, [rbx + ChkCtx.dbuf.len]
	mov	[chk_n], rax
	mov	r12, 1
  .outer:
	cmp	r12, [chk_n]
	jae	.out
	; lift element r12 into the frame, then slide the run before it right
	lea	rdi, [rbx + ChkCtx.dbuf]
	mov	rsi, r12
	call	vec_get
	mov	rdi, rbx
	mov	rsi, rax
	lea	rdx, [chk_tm]
	call	__chk_dcopy
	mov	r13, r12
  .inner:
	test	r13, r13
	jz	.place
	lea	rdi, [rbx + ChkCtx.dbuf]
	lea	rsi, [r13 - 1]
	call	vec_get
	mov	r14, rax		; the element to the left
	lea	rdi, [chk_tm]
	mov	rsi, r14
	call	__chk_dbefore
	test	eax, eax
	jz	.place			; the lifted record does NOT precede its
					; left neighbour, so it belongs right here
					; -- and stopping on equality is exactly
					; what makes this sort stable
	mov	rdi, rbx
	mov	rsi, r13
	lea	rdx, [rbx + ChkCtx.dbuf]
	call	__chk_dshift
	dec	r13
	jmp	.inner
  .place:
	lea	rdi, [rbx + ChkCtx.dbuf]
	mov	rsi, r13
	call	vec_get
	mov	rdi, rbx
	lea	rsi, [chk_tm]
	mov	rdx, rax
	call	__chk_dcopy
	inc	r12
	jmp	.outer
  .out:
	return
endp

; __chk_dcopy(chk_c, chk_a, chk_b) -- copy one `ChkDiag` from `chk_a` to
; `chk_b`. `rep movsb` over a fixed-size record; no field list to keep in step
; with the struct.
proc __chk_dcopy, chk_c, chk_a, chk_b
	locals
	endl
	mov	rdi, [chk_b]
	mov	rsi, [chk_a]
	mov	rcx, sizeof.ChkDiag
	cld
	rep	movsb
	return
endp

; __chk_dshift(chk_c, chk_i, chk_v) -- move element `chk_i - 1` of the `Vec` at
; `chk_v` into slot `chk_i`.
proc __chk_dshift, chk_c, chk_i, chk_v
	uses	rbx, r12
	locals
	endl
	mov	rdi, [chk_v]
	mov	rsi, [chk_i]
	call	vec_get
	mov	rbx, rax
	mov	rdi, [chk_v]
	mov	rsi, [chk_i]
	dec	rsi
	call	vec_get
	mov	rdi, rbx
	mov	rsi, rax
	mov	rcx, sizeof.ChkDiag
	cld
	rep	movsb
	return
endp

; chk_flush(chk_c) -> rax = how many `Diag` records were appended. Sorts the
; buffer into checker.md section 4's order and builds the real records. Called
; once, by `chk_run`, after every pass has raised what it is going to.
proc chk_flush, chk_c
	uses	rbx, r12, r13, r14
	locals
		slot	chk_n,  dq
		slot	chk_dg, dq
	endl
	mov	rbx, [chk_c]
	mov	r13, [rbx + ChkCtx.dbuf.len]
	mov	[chk_n], r13
	test	r13, r13
	jz	.done
	mov	rdi, rbx
	call	__chk_dsort

	xor	r12, r12
  .emit:
	cmp	r12, [chk_n]
	jae	.done
	lea	rdi, [rbx + ChkCtx.dbuf]
	mov	rsi, r12
	call	vec_get
	mov	r14, rax
	mov	rdi, [rbx + ChkCtx.diags]
	call	vec_push		; a `Diag` slot in the caller's vector
	mov	[chk_dg], rax
	mov	rdi, rax
	mov	esi, [r14 + ChkDiag.code]
	mov	edx, [r14 + ChkDiag.caret.file_id]
	mov	ecx, [r14 + ChkDiag.caret.start]
	mov	r8d, [r14 + ChkDiag.caret.len]
	call	diag_init
	mov	rdi, [chk_dg]
	mov	rsi, [rbx + ChkCtx.src]
	mov	rdx, [rbx + ChkCtx.srcn]
	mov	rcx, [rbx + ChkCtx.path]
	mov	r8,  [rbx + ChkCtx.pathn]
	call	diag_attach_source
	cmp	dword [r14 + ChkDiag.note], DIAG_NOTE_NONE
	je	.nonote
	mov	rdi, [chk_dg]
	mov	esi, [r14 + ChkDiag.note]
	xor	rdx, rdx
	xor	rcx, rcx
	call	diag_note_set
  .nonote:
	cmp	dword [r14 + ChkDiag.hasrel], 0
	je	.norel
	mov	rdi, [chk_dg]
	mov	esi, [r14 + ChkDiag.rel.file_id]
	mov	edx, [r14 + ChkDiag.rel.start]
	mov	ecx, [r14 + ChkDiag.rel.len]
	call	diag_rel_set
  .norel:
	mov	eax, [r14 + ChkDiag.fkind]
	cmp	eax, DIAG_FIX_NONE
	je	.nofix
	mov	rdi, [chk_dg]
	call	diag_fix_ptr
	mov	r13, rax
	mov	eax, [r14 + ChkDiag.fkind]
	cmp	eax, DIAG_FIX_DELETE
	je	.fixdel
	cmp	eax, DIAG_FIX_INSERT
	je	.fixins
	mov	rdi, r13
	mov	esi, [r14 + ChkDiag.fx.file_id]
	mov	edx, [r14 + ChkDiag.fx.start]
	mov	ecx, [r14 + ChkDiag.fx.len]
	mov	r8,  [r14 + ChkDiag.ftxt]
	mov	r9,  [r14 + ChkDiag.ftxtn]
	call	diag_fix_replace
	jmp	.nofix
  .fixins:
	mov	rdi, r13
	mov	esi, [r14 + ChkDiag.fx.file_id]
	mov	edx, [r14 + ChkDiag.fx.start]
	mov	rcx, [r14 + ChkDiag.ftxt]
	mov	r8,  [r14 + ChkDiag.ftxtn]
	call	diag_fix_insert
	jmp	.nofix
  .fixdel:
	mov	rdi, r13
	mov	esi, [r14 + ChkDiag.fx.file_id]
	mov	edx, [r14 + ChkDiag.fx.start]
	mov	ecx, [r14 + ChkDiag.fx.len]
	call	diag_fix_delete
  .nofix:
	inc	r12
	jmp	.emit
  .done:
	mov	rbx, [chk_c]
	mov	rax, [chk_n]
	add	[rbx + ChkCtx.ndiag], eax
	; the buffer is spent: a second flush must not re-emit it
	mov	qword [rbx + ChkCtx.dbuf.len], 0
	mov	rax, [chk_n]
	return
endp

; =============================================================================
; the frame stack
; =============================================================================

; __chk_frame_push(chk_c, chk_n, chk_k) -> rax = the new frame. `chk_k` is a
; CHK_FR_*; `owner` is inherited from the context, so a `BLOCK` frame inside a
; lambda answers with that lambda.
proc __chk_frame_push, chk_c, chk_n, chk_k
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chk_c]
	mov	r13, [rbx + ChkCtx.binds.len]
	lea	rdi, [rbx + ChkCtx.frames]
	call	vec_push
	mov	r12, rax
	mov	eax, [chk_n]
	mov	[r12 + ChkFrame.node], eax
	mov	[r12 + ChkFrame.first], r13d
	mov	eax, [rbx + ChkCtx.owner]
	mov	[r12 + ChkFrame.owner], eax
	mov	eax, [chk_k]
	mov	[r12 + ChkFrame.kind], al
	mov	byte [r12 + ChkFrame.pad0], 0
	mov	word [r12 + ChkFrame.pad1], 0
	lea	rdi, [r12 + ChkFrame.caps]
	mov	rcx, AST_CAP_COUNT
	xor	eax, eax
	cld
	rep	stosd
	mov	rax, r12
	return
endp

; __chk_frame_pop(chk_c) -- drops the innermost frame and truncates the bind
; stack back to its mark, which is what makes an inner block's names invisible
; once it closes (this file's header).
proc __chk_frame_pop, chk_c
	uses	rbx
	locals
	endl
	mov	rbx, [chk_c]
	mov	rax, [rbx + ChkCtx.frames.len]
	rassert	rax ge 1
	dec	rax
	lea	rdi, [rbx + ChkCtx.frames]
	mov	rsi, rax
	call	vec_get
	mov	ecx, [rax + ChkFrame.first]
	mov	[rbx + ChkCtx.binds.len], rcx
	dec	qword [rbx + ChkCtx.frames.len]
	return
endp

; __chk_frame_at(chk_c, chk_i) -> rax = frame `chk_i` (0-based, 0 = outermost).
proc __chk_frame_at, chk_c, chk_i
	locals
	endl
	mov	rdi, [chk_c]
	add	rdi, ChkCtx.frames
	mov	rsi, [chk_i]
	call	vec_get
	return
endp

; __chk_frame_top(chk_c) -> rax = the innermost frame, or 0 if none is open.
proc __chk_frame_top, chk_c
	uses	rbx
	locals
	endl
	mov	rbx, [chk_c]
	mov	rax, [rbx + ChkCtx.frames.len]
	test	rax, rax
	jz	.none
	dec	rax
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_frame_at
	return
  .none:
	xor	eax, eax
	return
endp

; __chk_frame_root(chk_c) -> rax = the innermost `FN` or `LAMBDA` frame, or 0.
; Parameters and a declared row bind here, never in an inner block -- and never
; in a `TYDECL` frame either, which is skipped exactly as a `BLOCK` is: both
; callers read `ChkFrame.node`'s `.a` as a `Sig`, and a `Struct`/`Interface`/
; `Typus` node's `.a` is a member list or a type.
proc __chk_frame_root, chk_c
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	r12, [rbx + ChkCtx.frames.len]
  .up:
	test	r12, r12
	jz	.none
	dec	r12
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_frame_at
	movzx	ecx, byte [rax + ChkFrame.kind]
	cmp	ecx, CHK_FR_BLOCK
	je	.up
	cmp	ecx, CHK_FR_TYDECL
	jne	.here
	jmp	.up
  .here:
	return
  .none:
	xor	eax, eax
	return
endp

; __chk_frame_limit(chk_c, chk_i) -> rax = one past the last bind belonging to
; frame `chk_i`: the next frame's mark, or the bind stack's length for the
; innermost frame.
proc __chk_frame_limit, chk_c, chk_i
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_i]
	inc	r12
	cmp	r12, [rbx + ChkCtx.frames.len]
	jae	.top
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_frame_at
	mov	eax, [rax + ChkFrame.first]
	return
  .top:
	mov	rax, [rbx + ChkCtx.binds.len]
	return
endp

; __chk_bind_at(chk_c, chk_i) -> rax = the declaration id at bind-stack slot
; `chk_i`.
proc __chk_bind_at, chk_c, chk_i
	locals
	endl
	mov	rdi, [chk_c]
	add	rdi, ChkCtx.binds
	mov	rsi, [chk_i]
	call	vec_get
	mov	eax, [rax]
	return
endp

; __chk_bind(chk_c, chk_d) -- bind declaration `chk_d` into the innermost frame.
; Raises `EXS-E0302` when a declaration of the same name is already bound in
; THAT frame (typed-ast.md section 2.4; checker.md section 2.4 class B, since
; numbered in §13). An inner block shadowing an outer name is not a duplicate
; and is not reported: only one scope is examined.
;
; At module level (no frame open) this does nothing -- `__chk_mods` owns that
; scope and its duplicate check, because module items resolve by a table and
; forward references between them are legal.
proc __chk_bind, chk_c, chk_d
	uses	rbx, r12, r13, r14
	locals
		slot	chk_nm, dq
		slot	chk_lo, dq
		slot	chk_hi, dq
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_d]
	test	r12, r12
	jz	.out
	mov	rdi, rbx
	call	__chk_frame_top
	test	rax, rax
	jz	.out
	mov	r13, rax
	mov	eax, [r13 + ChkFrame.first]
	mov	[chk_lo], rax
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_decl_at
	mov	eax, [rax + AstDecl.name]
	mov	[chk_nm], rax
	test	eax, eax
	jz	.push			; an unnamed declaration binds nothing
	mov	rax, [rbx + ChkCtx.binds.len]
	mov	[chk_hi], rax
  .scan:
	mov	rax, [chk_hi]
	cmp	rax, [chk_lo]
	jbe	.push
	dec	qword [chk_hi]
	mov	rdi, rbx
	mov	rsi, [chk_hi]
	call	__chk_bind_at
	mov	r14, rax
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r14
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.name]
	cmp	rcx, [chk_nm]
	jne	.scan
	; a duplicate in this very scope; r14 = the EARLIER declaration.
	;
	; ONE SOURCE FACT GETS ONE CODE. Two `sub` statements for one atom in one
	; scope are §4.5's capability shadowing, which §13 numbers `EXS-E0422` and
	; which `__chk_cap_bind` has already raised at the second `sub`, with the
	; fix §8.3 promises. Reporting the same two lines again as a duplicate
	; DECLARATION would be two codes for one edit, which is what §8.3's "one
	; class each, never one per message" argues against. A `sub` colliding with
	; an ordinary binding of the same name is NOT that case and is still
	; reported here, because no capability rule covers it.
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_SUB
	jne	.report
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_SUB
	je	.push
  .report:
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.node]
	mov	rdi, rbx
	mov	rsi, CHK_P_RESOLVE
	mov	rdx, 302
	call	chk_raise
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r12
	call	__chk_at_decl
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r14
	call	__chk_rel_decl
  .push:
	lea	rdi, [rbx + ChkCtx.binds]
	call	vec_push
	mov	ecx, [chk_d]
	mov	[rax], ecx
  .out:
	return
endp

; =============================================================================
; lookup
; =============================================================================

; chk_prelude_lookup(chk_c, chk_nm) -> rax = the declaration a prelude name
; resolves to, or 0.
;
; RETURNS 0, ALWAYS, TODAY -- and that is a hook, not an oversight.
; `docs/design/runtime.md` section 2.2 makes the prelude's names "pre-seeded
; declarations in a scope OUTSIDE the module", and since checker.md was written
; `compiler/x86_64/prelude/interface.inc` has grown a part B that serializes
; seven of them (`Scriptor`, its two fields, `m.ambitus()`,
; `Scriptor.ad_exitum`, `s.scribe`, `alloc_novum`) as fixed-width records with
; an index space of their own, and says in its own header that when
; `checker/resolve/` exists "its own `Decl` record wins and this serialization
; changes to match". Wiring it up is NOT this pass's call to make, for two
; reasons that are decisions rather than work:
;
;   1. `Seg.d` holds an `Ast.decls` INDEX (typed-ast.md section 2.3). A prelude
;      declaration is an index into a different table. Either those records are
;      pushed into `Ast.decls` -- which changes what `ast_cap_base`'s "the
;      atoms are the last eleven" means, and what `ast_dump` prints -- or
;      `Seg.d` grows a tag. Both are the ast agent's to decide.
;   2. That table has NO entry for spec §5.1's type names. `i32`, `f32`,
;      `textus`, `mensura`, `octeti`, `scalares` and `grapha` are `TyPath`s in
;      every program, and nothing declares them anywhere in this repository.
;      See this file's header: type-position misses are silent here for exactly
;      that reason, and the real fix is pass 2's primitive table.
;
; Until both are settled, a miss here is a miss, and every prelude name in
; `examples/` is `EXS-E0301`. checker.md section 9 finding 14 says the same
; thing about spec §5.1's methods.
proc chk_prelude_lookup, chk_c, chk_nm
	locals
	endl
	; WIRED, 2026-09-10, by `checker-types`: both blockers above are
	; answered in `checker/types/prim.inc` -- the tagged `Seg.d` (bit 31 =
	; a `prelude/interface.inc` part B row, which `chk_verify` now drops
	; before its bounds check) and pass 2's primitive table. Without this
	; forwarding line `Scriptor.ad_exitum(a)` in `examples/initium.exsc` is
	; `EXS-E0301` raised HERE, in pass 1, and pass 2 cannot retract a
	; diagnostic another pass has already buffered.
	mov	rdi, [chk_c]
	mov	rsi, [chk_nm]
	call	chk_ty_prelude_lookup
	return
endp

; __chk_mod_lookup(chk_c, chk_nm) -> rax = the module-scope declaration of name
; `chk_nm`, or 0. One hashed lookup; the table is never iterated (CLAUDE.md).
proc __chk_mod_lookup, chk_c, chk_nm
	uses	rbx
	locals
		slot	chk_k, dd
	endl
	mov	rbx, [chk_c]
	mov	eax, [chk_nm]
	test	eax, eax
	jz	.none
	mov	[chk_k], eax
	lea	rdi, [rbx + ChkCtx.mods]
	lea	rsi, [chk_k]
	mov	rdx, 4
	call	map_get
	return
  .none:
	xor	eax, eax
	return
endp

; __chk_memb_lookup(chk_c, chk_nm) -> rax = the unique module-wide member
; declaration of that name, or 0 (absent, or ambiguous).
proc __chk_memb_lookup, chk_c, chk_nm
	uses	rbx
	locals
		slot	chk_k, dd
	endl
	mov	rbx, [chk_c]
	mov	eax, [chk_nm]
	test	eax, eax
	jz	.none
	mov	[chk_k], eax
	lea	rdi, [rbx + ChkCtx.membs]
	lea	rsi, [chk_k]
	mov	rdx, 4
	call	map_get
	mov	rcx, CHK_MEMB_AMBIG
	cmp	rax, rcx
	je	.none
	return
  .none:
	xor	eax, eax
	return
endp

; __chk_lookup(chk_c, chk_nm, chk_ord) -> rax = the declaration, rdx = the index
; of the frame it was found in (or -1 when it came from the module table or the
; prelude, and rax = 0 on a miss).
;
; THIS IS checker.md SECTION 2.1'S FRAME WALK. Innermost frame outward. Within
; one frame: the capability slot first (when `chk_ord` names an atom), then that
; frame's own bindings, newest first. The walk stops after the innermost `FN`
; frame -- spec §8.6 has no nested `FunctionDecl`, so a function is never inside
; another function's scope -- and then consults the module table and the
; prelude. A `LAMBDA` frame does NOT stop it: a lambda is reached through its
; enclosing function's frames, and crossing one is a capture, which
; `__chk_note_captures` records for the caller that cares.
;
; `chk_ord` = 0 means "this name is not an atom" and the capability slots are
; not consulted; `chk_nm` = 0 means "look up by ordinal only" and the bindings
; are not scanned.
proc __chk_lookup, chk_c, chk_nm, chk_ord
	uses	rbx, r12, r13, r14
	locals
		slot	chk_lo, dq
		slot	chk_hi, dq
		slot	chk_fi, dq
	endl
	mov	rbx, [chk_c]
	mov	r12, [rbx + ChkCtx.frames.len]
  .frame:
	test	r12, r12
	jz	.outer
	dec	r12
	mov	[chk_fi], r12
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_frame_at
	mov	r13, rax

	mov	r14, [chk_ord]
	test	r14, r14
	jz	.binds
	lea	rcx, [r13 + ChkFrame.caps]
	mov	eax, [rcx + r14*4 - 4]
	test	eax, eax
	jz	.binds
	mov	r14, rax
	jmp	.hit

  .binds:
	cmp	qword [chk_nm], 0
	je	.next
	mov	eax, [r13 + ChkFrame.first]
	mov	[chk_lo], rax
	mov	rdi, rbx
	mov	rsi, [chk_fi]
	call	__chk_frame_limit
	mov	[chk_hi], rax
  .scan:
	mov	rax, [chk_hi]
	cmp	rax, [chk_lo]
	jbe	.next
	dec	qword [chk_hi]
	mov	rdi, rbx
	mov	rsi, [chk_hi]
	call	__chk_bind_at
	mov	r14, rax
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r14
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.name]
	cmp	rcx, [chk_nm]
	jne	.scan
	jmp	.hit

  .next:
	mov	rdi, rbx
	mov	rsi, [chk_fi]
	call	__chk_frame_at
	movzx	ecx, byte [rax + ChkFrame.kind]
	cmp	ecx, CHK_FR_FN
	jne	.cont
	; THE ONE THING OUTSIDE A FUNCTION THAT IS STILL A SCOPE. The stop above
	; is justified by "a function is never inside another function's scope";
	; a `TYDECL` frame is not a function's scope. An interface member's
	; signature may name the interface's own generic parameter
	; (`interfacies I<T> { functio acc(self: I, v: T) -> T }`, spec §8.6's
	; `InterfaceDecl [GenericParams]`), so the walk continues when -- and
	; only when -- the next frame out is a `TYDECL`.
	mov	rcx, [chk_fi]
	test	rcx, rcx
	jz	.outer
	mov	rdi, rbx
	lea	rsi, [rcx - 1]
	call	__chk_frame_at
	movzx	ecx, byte [rax + ChkFrame.kind]
	cmp	ecx, CHK_FR_TYDECL
	jne	.outer
  .cont:
	mov	r12, [chk_fi]
	jmp	.frame

  .hit:
	mov	rax, r14
	mov	rdx, [chk_fi]
	return

  .outer:
	mov	rdi, rbx
	mov	rsi, [chk_nm]
	call	__chk_mod_lookup
	test	rax, rax
	jnz	.outerhit
	mov	rdi, rbx
	mov	rsi, [chk_nm]
	call	chk_prelude_lookup
  .outerhit:
	mov	rdx, -1
	return
endp

; __chk_note_captures(chk_c, chk_fi, chk_ord, chk_n) -- record one
; CHK_USE_CAPTURE for every `LAMBDA` frame strictly inside frame `chk_fi`.
; checker.md section 2.1: "Crossing a `Lambda` root frame records a capture in
; that lambda's inferred row ... and continues" (spec §4.1 rule 4, fourth way).
;
; CAPABILITY DRAWS ONLY. An ordinary value captured by a lambda is a closure
; environment slot, which is Stage 3's (checker.md section 2.8).
proc __chk_note_captures, chk_c, chk_fi, chk_ord, chk_n
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chk_c]
	cmp	qword [chk_ord], 0
	je	.out
	mov	r12, [rbx + ChkCtx.frames.len]
  .up:
	test	r12, r12
	jz	.out
	dec	r12
	mov	rcx, [chk_fi]
	cmp	r12, rcx
	jbe	.out
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_frame_at
	movzx	ecx, byte [rax + ChkFrame.kind]
	cmp	ecx, CHK_FR_LAMBDA
	jne	.up
	mov	r13d, [rax + ChkFrame.owner]
	lea	rdi, [rbx + ChkCtx.draws]
	call	vec_push
	mov	[rax + ChkUse.owner], r13d
	mov	ecx, [chk_n]
	mov	[rax + ChkUse.node], ecx
	mov	ecx, [chk_ord]
	mov	[rax + ChkUse.ord], ecx
	mov	byte [rax + ChkUse.kind], CHK_USE_CAPTURE
	mov	byte [rax + ChkUse.pad0], 0
	mov	word [rax + ChkUse.pad1], 0
	jmp	.up
  .out:
	return
endp

; =============================================================================
; small readers over the tree
; =============================================================================

; __chk_seg0(chk_c, chk_n) -> rax = the first `Seg` node of `Path` node `chk_n`,
; rdx = the segment count.
proc __chk_seg0, chk_c, chk_n
	uses	rbx, r12
	locals
		slot	chk_cnt, dq
	endl
	mov	rbx, [chk_c]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_n]
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	rassert	rcx eq AST_PATH
	mov	ecx, [rax + AstNode.b]
	mov	[chk_cnt], rcx
	rassert	rcx ge 1
	mov	r12d, [rax + AstNode.a]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_extra_at
	mov	rdx, [chk_cnt]
	return
endp

; __chk_seg_name(chk_c, chk_n) -> rax = `Seg` node `chk_n`'s interner id.
proc __chk_seg_name, chk_c, chk_n
	locals
	endl
	mov	rdi, [chk_c]
	mov	rdi, [rdi + ChkCtx.tree]
	mov	rsi, [chk_n]
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	rassert	rcx eq AST_SEG
	mov	eax, [rax + AstNode.a]
	return
endp

; __chk_setd(chk_c, chk_n, chk_d) -- write node `chk_n`'s `d` slot. Stage 2's
; whole job is filling slots that exist (typed-ast.md section 2.2); this is the
; one place this pass writes the tree, so a reader has one place to look.
proc __chk_setd, chk_c, chk_n, chk_d
	locals
	endl
	mov	rdi, [chk_c]
	mov	rdi, [rdi + ChkCtx.tree]
	mov	rsi, [chk_n]
	test	rsi, rsi
	jz	.out
	call	ast_node_at
	mov	ecx, [chk_d]
	mov	[rax + AstNode.d], ecx
  .out:
	return
endp

; __chk_atom_decl(chk_c, chk_ord) -> rax = the `AST_D_CAPATOM` declaration of the
; atom whose spec §4.6 ordinal is `chk_ord`, or 0 when the tree has no atoms.
proc __chk_atom_decl, chk_c, chk_ord
	uses	rbx
	locals
	endl
	mov	rbx, [chk_c]
	mov	eax, [rbx + ChkCtx.capbase]
	test	eax, eax
	jz	.none
	add	rax, [chk_ord]
	dec	rax
	return
  .none:
	xor	eax, eax
	return
endp

; __chk_typus_binds(chk_c, chk_d, chk_nm) -> eax = 1 if `typus` declaration
; `chk_d` declares a generic parameter named `chk_nm`, else 0. A generic
; parameter is visible over its own `typus`'s right-hand side and shadows a
; module-scope name there (this file's `.tydecl` arm), which is what
; `__chk_type_atom`'s alias walk has to honour when it resolves that
; right-hand side by NAME. `GenParam.a` is the name's interner id and
; `Generics` is a counted list (`Generics.a` = the `extra` offset, `.b` = the
; count -- `__chk_ty_ngen`'s reading of it).
proc __chk_typus_binds, chk_c, chk_d, chk_nm
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [chk_c]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_d]
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.node]
	test	ecx, ecx
	jz	.no
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rcx
	call	ast_node_at
	mov	ecx, [rax + AstNode.c]	; `Typus.c`: the `Generics`, or 0
	test	ecx, ecx
	jz	.no
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rcx
	call	ast_node_at
	mov	r12d, [rax + AstNode.a]
	mov	r13d, [rax + AstNode.b]
	xor	r14, r14
  .next:
	cmp	r14, r13
	jae	.no
	mov	rdi, [rbx + ChkCtx.tree]
	lea	rsi, [r12 + r14]
	call	ast_extra_at
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rax
	call	ast_node_at
	mov	ecx, [rax + AstNode.a]
	cmp	rcx, [chk_nm]
	je	.yes
	inc	r14
	jmp	.next
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_type_atom(chk_c, chk_n) -> rax = the spec §4.6 ordinal named by type
; node `chk_n`, or 0. A capability-typed thing is written `m: Mundus` /
; `sock: rete` (spec §4.3), which parses as `TyPath -> Path -> Seg`; nothing
; else in §8.6 can name an atom in type position -- EXCEPT THROUGH A `typus`
; ALIAS, which checker.md section 2.2 makes TRANSPARENT: `typus Amb = ambitus;`
; makes `a: Amb` an `ambitus` exactly as `a: ambitus` is.
;
; WHY THE ALIAS IS FOLLOWED HERE. Pass 2 already resolves it (`__chk_ty_typus`
; interns the alias's right-hand side, so `Amb` is `AST_TY_CAP` over `ambitus`
; in every judgement it makes: the `EXS-E0303` provider test, a call's
; argument, a method receiver). This routine is pass 1's own, SYNTACTIC reading
; of the same fact, and it is what registers a parameter as the PROVIDER of its
; atom -- so before it followed aliases the two passes disagreed about one
; parameter. Measured: `typus Amb = ambitus; functio f(a: Amb) -> u8 poscit
; alloc { sub ambitus = a; }` was accepted where `a: ambitus` is `EXS-E0422`
; (the double-bind is `__chk_cap_bind_root`'s lookup, and an unregistered
; parameter gives it nothing to find), and `a: Amb` followed by a draw of
; `ambitus` was `EXS-E0421` where `a: ambitus` is a provider. Not an authority
; escape in either direction -- the parameter holds the atom visibly under both
; spellings -- but one fact answered two ways by two passes is the shape the
; reviewed escapes in ADR 0017 took, so the aliases are canonicalized here
; rather than the two cases patched.
;
; THE WALK. The written type's `Seg` is read through its resolved `d`, which
; the `.param` arm's walk of the type has just set, so a generic parameter or a
; local that shadows the alias's name is honoured (`functio f<Amb>(a: Amb)`
; names the generic parameter, not the alias; `Seg.d` may also be a TAGGED
; prelude row, bit 31, which is not an `Ast.decls` index and is never a
; `typus`). From there each hop is a `typus` declaration's right-hand side
; (`Typus.a`), read by NAME because that alias may be written AFTER the
; function and has not been walked yet (nothing here may depend on item
; order): an atom's name ends the walk with its ordinal; any other name is the
; MODULE table's, except that a generic parameter of the alias being followed
; shadows it (`typus Id<T> = T;`, which names `T`, not a module `typus T`).
; Generic arguments are ignored on purpose: the types pass instantiates no
; alias (`__chk_ty_decl_inst`'s header), so `typus Amb<T> = ambitus;` is an
; `ambitus` for every `Amb<X>` and this routine must say so too. Anything that
; is not a plain one-segment `TyPath` ends the walk with 0, which is exactly
; what the pre-alias routine answered for it.
;
; THE WALK HAS NO DEPTH LIMIT OF ITS OWN, and that is the invariant, not an
; omission. Pass 2 resolves a legal alias chain of ANY length (it marks each
; `typus` BUSY then DONE and never counts hops: `__chk_ty_typus`), so a pass-1
; walk that gave up at a fixed depth -- it first did, at 32 -- answers "not a
; provider" for a chain pass 2 answers "ambitus" for, one hop past whatever
; constant was chosen: `typus A1 = ambitus; ... typus A33 = A32;` with `a: A33`
; escaped EXS-E0422 and drew a spurious EXS-E0421, measured. The only way this
; walk can fail to end is a CYCLE, and a cycle is bounded without a constant:
; a chain of DISTINCT `typus` declarations has at most `ast_decl_count` links,
; so more hops than declarations means one repeated. That repeat is `typus A =
; B; typus B = A;` -- pass 2's own BUSY mark reports it ONCE as `EXS-E0303`
; (class C, "alias cycle") at the declaration that closed it -- and the walk's
; answer for it is 0, because a cycle names no atom. That 0 is not silent: the
; program is rejected by pass 2, and `tests/unit/chk_e0422_alias.asm` asserts
; exactly one E0303 for a self-alias, a three-cycle and a generic cycle. No code
; is raised here, because a second E0303 for one cycle would be a second
; diagnostic for one fact (spec 8.3).

proc __chk_type_atom, chk_c, chk_n
	uses	rbx, r12, r13, r14
	locals
		slot	chk_nm, dq
		slot	chk_hp, dq
		slot	chk_ct, dq
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_n]
	xor	r13, r13		; the `typus` being followed; 0 = none yet
	mov	qword [chk_hp], 0
	mov	rdi, [rbx + ChkCtx.tree]
	call	ast_decl_count
	mov	[chk_ct], rax		; no chain of distinct declarations is longer
  .again:
	test	r12, r12
	jz	.none
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_TYPATH
	jne	.none
	mov	r12d, [rax + AstNode.a]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_seg0
	cmp	rdx, 1
	jne	.none			; `a.b` names no atom
	mov	r14, rax		; the `Seg`
	mov	rdi, rbx
	mov	rsi, r14
	call	__chk_seg_name
	mov	[chk_nm], rax
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rax
	call	ast_cap_of_name
	test	rax, rax
	jnz	.out			; an atom's own name: the ordinal
	test	r13, r13
	jnz	.byname
	; the type as WRITTEN: the walk that preceded this call resolved it
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r14
	call	ast_node_at
	mov	eax, [rax + AstNode.d]
	jmp	.decl
  .byname:
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, [chk_nm]
	call	__chk_typus_binds
	test	eax, eax
	jnz	.none			; the alias's own generic parameter
	mov	rdi, rbx
	mov	rsi, [chk_nm]
	call	__chk_mod_lookup
  .decl:
	test	rax, rax
	jz	.none
	mov	r13, rax
	cmp	r13, [chk_ct]
	ja	.none			; a tagged prelude row is not an index
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r13
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_TYPUS
	jne	.none
	mov	ecx, [rax + AstDecl.node]
	test	ecx, ecx
	jz	.none
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rcx
	call	ast_node_at
	mov	r12d, [rax + AstNode.a]	; `Typus.a`: the aliased type, or a `SumBody`
	inc	qword [chk_hp]
	mov	rax, [chk_hp]
	cmp	rax, [chk_ct]
	ja	.none			; more hops than declarations: a cycle (pass 2's E0303)
	jmp	.again
  .none:
	xor	eax, eax
  .out:
	return
endp

; __chk_fix_text(chk_c, chk_pre, chk_pren, chk_ord) -> rax = pointer, rdx =
; length: `chk_pre` followed by the atom's canonical spelling.
;
; ALLOCATED FROM THE TREE'S ARENA, not the checker's scratch. A `DiagFix` holds
; a BORROWED pointer and the driver renders diagnostics after the checker
; returns; scratch text would be a dangling pointer the moment the caller reset
; the arena `ChkCtx.arena` documents it may reset. The bytes come from
; ast/kinds.inc's `ast_cap_names` pool rather than from the interner, so this
; works on a tree loaded from a dump whose interner was never populated.
proc __chk_fix_text, chk_c, chk_pre, chk_pren, chk_ord
	uses	rbx, r12, r13
	locals
		slot	chk_np, dq
		slot	chk_nn, dq
	endl
	mov	rbx, [chk_c]
	lea	rdi, [ast_cap_names]
	mov	rsi, [chk_ord]
	call	ast_name_at
	mov	[chk_np], rax
	mov	[chk_nn], rdx
	mov	r12, [chk_pren]
	add	r12, rdx		; total length
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rdi, [rdi + Ast.arena]
	mov	rsi, r12
	call	arena_alloc
	mov	r13, rax
	mov	rdi, r13
	mov	rsi, [chk_pre]
	mov	rcx, [chk_pren]
	cld
	rep	movsb
	mov	rsi, [chk_np]
	mov	rcx, [chk_nn]
	rep	movsb
	mov	rax, r13
	mov	rdx, r12
	return
endp

; =============================================================================
; pass 0 -- find the atoms
; =============================================================================

; __chk_pass0(chk_c) -- checker.md section 2.2's pass 0, in the form ast/
; settled on: Stage 1 PUSHES the eleven declarations (`ast_cap_push`, called by
; `ast_from_cst`), so this pass FINDS them and must not push a second set.
; `ast_cap_base` is that lookup; `ast_verify`'s `__ast_v_caps` has already
; proved all-eleven-or-none and last-eleven, and the `rassert` below re-states
; the arithmetic this file then relies on (atom `k` is `capbase + k - 1`).
;
; A tree with NO atoms is admitted, not trapped: `ast_verify` admits one, a
; hand-built fixture may be one, and every capability lookup in such a tree
; simply misses.
proc __chk_pass0, chk_c
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	rdi, [rbx + ChkCtx.tree]
	call	ast_cap_base
	mov	[rbx + ChkCtx.capbase], eax
	test	eax, eax
	jz	.out
	mov	r12, rax
	mov	rdi, [rbx + ChkCtx.tree]
	call	ast_decl_count
	sub	rax, r12
	inc	rax
	rassert	rax eq AST_CAP_COUNT
  .out:
	return
endp

; =============================================================================
; the module scope
; =============================================================================

; __chk_mods(chk_c) -- build the module table and the member table, and raise
; everything that is a property of the module's own declarations rather than of
; any expression:
;
;   `EXS-E0302`  a second module-scope declaration of one name
;   (`EXS-E0500` is NOT raised here: spec §4.1 rule 7 is one code chosen by
;    capability-bearing, which pass 3 knows -- see the `.binding` arm below)
;   `EXS-E0424`  two `initium`, or none where a program was asked for
;
; MODULE SCOPE IS A SCAN OF `Ast.decls`, NOT OF `Module.a`. Declarations are
; pushed in source order (typed-ast.md section 2.4), so scanning them in id
; order IS item order -- and it reaches the `ExternFn` declarations inside an
; `externus` block, which are module-visible names that hang off the `Externus`
; declaration rather than off the `Module` node. Everything with a deeper parent
; (a field, a member, a parameter, a local) is skipped, and so is every
; `CapAtom`: an atom is not an ordinary name and is found by `ast_cap_of_name`.
;
; Forward references between module items resolve because the whole table is
; built before the walk starts -- checker.md section 2.2's own reason for
; having a table at all.
;
; NOT CHECKED HERE, AND WHY: "two impls of one interface for one type"
; (checker.md section 2.4's other class-B case) keys on the TARGET TYPE's
; interned id, which pass 2 produces. It belongs to the impl map that section
; 2.7 gives the types agent, and raising half of one class in two passes would
; make the same rule two implementations.
proc __chk_mods, chk_c
	uses	rbx, r12, r13, r14
	locals
		slot	chk_n,  dq
		slot	chk_nm, dq
		slot	chk_k,  dd
	endl
	mov	rbx, [chk_c]
	mov	rdi, [rbx + ChkCtx.tree]
	call	ast_decl_count
	mov	[chk_n], rax
	mov	r12, 1
  .decl:
	cmp	r12, [chk_n]
	ja	.entry
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_decl_at
	mov	r13, rax
	movzx	ecx, byte [r13 + AstDecl.kind]
	cmp	ecx, AST_D_CAPATOM
	je	.next
	cmp	ecx, AST_D_MEMBER
	je	.member
	mov	ecx, [r13 + AstDecl.parent]
	test	ecx, ecx
	jz	.scoped
	; module-visible only through an `externus` block -- or as a VARIANT of
	; a module-level sum `typus` (docs/design/sum-types.md D2: a variant is
	; a name in the scope its type is declared in, which is why it collides
	; with a constant of the same name as `EXS-E0302`, and why a
	; constructor `prosperum(n)` resolves in expression position at all)
	movzx	eax, byte [r13 + AstDecl.kind]
	mov	[chk_k], eax
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rcx
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_EXTERNUS
	je	.visible
	cmp	ecx, AST_D_TYPUS
	jne	.next
	cmp	dword [chk_k], AST_D_VARIANT
	jne	.next
	cmp	dword [rax + AstDecl.parent], 0
	jne	.next
  .visible:
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_decl_at
	mov	r13, rax

  .scoped:
	mov	eax, [r13 + AstDecl.name]
	mov	[chk_nm], rax
	test	eax, eax
	jz	.checks			; an `Impl` head introduces no name
	mov	rdi, rbx
	mov	rsi, [chk_nm]
	call	__chk_mod_lookup
	test	rax, rax
	jz	.insert
	mov	r14, rax		; the EARLIER declaration
	; A PRELUDE DECLARATION NEVER COLLIDES: a module's own declaration
	; shadows it (spec §12, and `__chk_ty_path_type`'s order for the
	; primitive table). The prelude's are pushed after every source
	; declaration (`ast_prelude_push`, compiler/x86_64/prelude/eventus.inc),
	; so the earlier one is always the module's and the prelude's is the
	; one left out -- `functio eventus(...)` in examples/onus/onus.exsc
	; keeps its name and is not `EXS-E0302`.
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_decl_synth
	test	eax, eax
	jnz	.checks
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_decl_at
	mov	ecx, [rax + AstDecl.node]
	mov	rdi, rbx
	mov	rsi, CHK_P_RESOLVE
	mov	rdx, 302
	call	chk_raise
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r12
	call	__chk_at_decl
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r14
	call	__chk_rel_decl
	jmp	.checks
  .insert:
	; the key must outlive this call and must not move: `ChkCtx.keys` is
	; one flat allocation, indexed by declaration id.
	mov	rcx, [rbx + ChkCtx.keys]
	lea	rcx, [rcx + r12*4]
	mov	eax, [chk_nm]
	mov	[rcx], eax
	lea	rdi, [rbx + ChkCtx.mods]
	mov	rsi, rcx
	mov	rdx, 4
	mov	rcx, r12
	call	map_insert
	jmp	.checks

  .member:
	; a member name, for `__chk_member`'s candidate. A second declaration of
	; the same name anywhere in the module makes it ambiguous without a
	; receiver type; `map_insert` shadows for `map_get`, so writing the
	; sentinel over it is the whole of "withhold the candidate".
	mov	eax, [r13 + AstDecl.name]
	mov	[chk_nm], rax
	test	eax, eax
	jz	.next
	mov	rdi, rbx
	mov	rsi, [chk_nm]
	lea	rcx, [rbx + ChkCtx.membs]
	mov	[chk_k], eax
	lea	rdi, [rbx + ChkCtx.membs]
	lea	rsi, [chk_k]
	mov	rdx, 4
	call	map_get
	mov	r14, rax
	mov	rcx, [rbx + ChkCtx.keys]
	lea	rcx, [rcx + r12*4]
	mov	eax, [chk_nm]
	mov	[rcx], eax
	lea	rdi, [rbx + ChkCtx.membs]
	mov	rsi, rcx
	mov	rdx, 4
	mov	rcx, r12
	test	r14, r14
	jz	.membins
	mov	rcx, CHK_MEMB_AMBIG
  .membins:
	call	map_insert
	jmp	.next

  .checks:
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_decl_at
	mov	r13, rax
	movzx	ecx, byte [r13 + AstDecl.kind]
	cmp	ecx, AST_D_BINDING
	je	.binding
	cmp	ecx, AST_D_FN
	je	.fn
	jmp	.next

  .binding:
	; SPEC §4.1 RULE 7 IS ONE CODE, AND THIS PASS CANNOT CHOOSE IT. The rule
	; reads "No module-level mutable state. (`EXS-E0500`; with a capability,
	; `EXS-E0501`.)" -- one diagnostic per offending binding, and WHICH one
	; depends on whether the binding's type is capability-bearing (spec
	; §4.3). That is `AST_F_CAPBEAR`, which pass 3's fixpoint computes from
	; `Decl.ty`, which pass 2 fills. This pass has neither, so raising
	; `E0500` here produced BOTH codes for `mutabilis retis_globalis: rete;`
	; -- measured on §14 entry 11, which expects exactly `{EXS-E0501}`.
	;
	; So pass 1 says nothing and pass 3 raises exactly one. NOTHING HAS TO BE
	; RECORDED FOR IT: `mutabilis` is already `AST_F_MUTABILIS` on the
	; declaration (Stage 1 sets it, ast/kinds.inc), and "module level" is
	; already `AstDecl.parent == 0`, so `__chk_row_e0501` in
	; checker/rows/compute.inc reads the same two facts this arm read and
	; needs no new `Decl.flags` bit (checker.md hazard H4: there is no ninth
	; flag to spend). `chk_lit_firma` / `CHK_MUTABILIS_LEN` above stay here,
	; where the other `E05xx` fix text lives, and that file uses them.
	jmp	.next

  .fn:
	mov	ecx, [r13 + AstDecl.parent]
	test	ecx, ecx
	jnz	.next			; an interface member is not an entry point
	mov	eax, [rbx + ChkCtx.entryid]
	test	eax, eax
	jz	.next
	cmp	eax, [r13 + AstDecl.name]
	jne	.next
	inc	dword [rbx + ChkCtx.nmain]
	cmp	dword [rbx + ChkCtx.nmain], 1
	jne	.second
	mov	[rbx + ChkCtx.mainid], r12d
	jmp	.next
  .second:
	mov	ecx, [r13 + AstDecl.node]
	mov	rdi, rbx
	mov	rsi, CHK_P_RESOLVE
	mov	rdx, 424
	call	chk_raise
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r14
	mov	edx, [rbx + ChkCtx.mainid]
	call	__chk_rel_decl

  .next:
	inc	r12
	jmp	.decl

  .entry:
	; spec §4.1 rule 2: "A second `initium`, one with the wrong signature,
	; or none where a program was asked for, is `EXS-E0424`."
	;
	; THE COUNT CASES ONLY. The SIGNATURE case -- `publica functio
	; initium(m: Mundus) -> u8` -- needs the parameter's type and the
	; result's, which are `Ast.types` ids pass 2 interns; checking it here
	; would mean a second, structural implementation of type comparison that
	; pass 2 would then have to be kept in step with. Left to the types
	; agent, with the code and the span settled here: `EXS-E0424` on the
	; `Fn` node.
	mov	eax, [rbx + ChkCtx.flags]
	test	eax, CHK_F_PROGRAM
	jz	.out
	cmp	dword [rbx + ChkCtx.nmain], 0
	jne	.out
	mov	rdi, rbx
	mov	rsi, CHK_P_RESOLVE
	mov	rdx, 424
	xor	rcx, rcx		; no node: the module as a whole
	call	chk_raise
  .out:
	return
endp

; =============================================================================
; E0301 recovery-artifact taint -- see this file's header
; =============================================================================

; __chk_is_nested_fn(chk_c, chk_n) -> al = 1 if node `chk_n` is a `Fn` whose
; own declaration is `AST_D_FN` and whose declaration's PARENT is ALSO
; `AST_D_FN`. The only way that pairing arises is spec §8.6's peek rejecting a
; nested NAMED `functio` (`EXS-E0201`) and `cst/parse.inc`'s `.functio`
; recovering by parsing it anyway -- an ordinary top-level `Fn` has `parent`
; 0, an `Impl`/`Externus` member's parent is `AST_D_IMPL`/`AST_D_EXTERNUS`
; (or `AST_D_EXTERNFN` for the externus function itself), and nothing else in
; this grammar nests a `Fn` declaration inside another `Fn` declaration. See
; this file's header, "E0301 AND RECOVERY ARTIFACTS".
proc __chk_is_nested_fn, chk_c, chk_n
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_n]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_FN
	jne	.no
	mov	eax, [rax + AstNode.d]
	test	eax, eax
	jz	.no
	mov	r12, rax		; this Fn's own declaration
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_FN
	jne	.no
	mov	ecx, [rax + AstDecl.parent]
	test	ecx, ecx
	jz	.no			; module level -- ordinary and legal
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rcx
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_FN
	jne	.no
	mov	al, 1
	return
  .no:
	xor	al, al
	return
endp

; __chk_taint_of(chk_c, chk_n) -> al = 1 if node `chk_n` is itself a recovery
; artifact, or any child slot `ast_roles` names for its kind is already
; tainted. Driven by the same table `__chk_kids` walks, in the same slot
; order, but this is a flat read of `ChkCtx.taint` for each child -- never a
; recursive call -- because `__chk_taint_pass` below only ever asks this for
; node ids in increasing order, and postorder guarantees every child's id is
; smaller than `chk_n`'s (typed-ast.md §2.1).
proc __chk_taint_of, chk_c, chk_n
	uses	rbx, r12, r13, r14
	locals
		slot	chk_k,   dq
		slot	chk_r,   dq
		slot	chk_off, dq
		slot	chk_cnt, dq
		slot	chk_tb,  dq
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_n]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	mov	[chk_k], rcx
	mov	rax, [rbx + ChkCtx.taint]
	mov	[chk_tb], rax

	cmp	qword [chk_k], AST_ERROR
	jne	.notsrc
	mov	al, 1
	return
  .notsrc:
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_is_nested_fn
	test	al, al
	jz	.slots
	mov	al, 1
	return

  .slots:
	xor	r13, r13
  .slot:
	cmp	r13, 4
	jae	.miss
	mov	rdi, [chk_k]
	mov	rsi, r13
	call	ast_role_of
	mov	[chk_r], rax
	cmp	eax, AST_R_NODE
	je	.child
	cmp	eax, AST_R_NODEQ
	je	.child
	cmp	eax, AST_R_LIST
	je	.list
	cmp	eax, AST_R_LIST2
	je	.list
	jmp	.next
  .child:
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	lea	rcx, [rax + AstNode.a]
	mov	eax, [rcx + r13*4]
	test	eax, eax
	jz	.next
	mov	rcx, [chk_tb]
	movzx	edx, byte [rcx + rax]
	test	dl, dl
	jz	.next
	mov	al, 1
	return
  .list:
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	lea	rcx, [rax + AstNode.a]
	mov	eax, [rcx + r13*4]
	mov	[chk_off], rax
	lea	rdx, [r13 + 1]
	mov	eax, [rcx + rdx*4]
	mov	[chk_cnt], rax
	cmp	qword [chk_r], AST_R_LIST2
	jne	.have_n
	shl	qword [chk_cnt], 1	; `Si` stores (cond, block) PAIRS
  .have_n:
	xor	r14, r14
  .elem:
	cmp	r14, [chk_cnt]
	jae	.next
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_off]
	add	rsi, r14
	call	ast_extra_at
	test	eax, eax
	jz	.elemnext
	mov	rcx, [chk_tb]
	movzx	edx, byte [rcx + rax]
	test	dl, dl
	jz	.elemnext
	mov	al, 1
	return
  .elemnext:
	inc	r14
	jmp	.elem
  .next:
	inc	r13
	jmp	.slot
  .miss:
	xor	al, al
	return
endp

; __chk_taint_pass(chk_c) -- ONE FORWARD SCAN over every node id, building
; `ChkCtx.taint`. Allocated from `ChkCtx.arena` (scratch -- never read after
; `chk_run` returns, same lifetime as `ChkCtx.keys`), sized `nodes + 1` so a
; 0 id (never a live node id) is always 0. Must run before pass 1's walk;
; `chk_resolve` calls it first, unconditionally -- an empty tree costs one
; `ast_node_count` and no loop iterations.
proc __chk_taint_pass, chk_c
	uses	rbx, r12, r13
	locals
		slot	chk_n,   dq
		slot	chk_buf, dq
	endl
	mov	rbx, [chk_c]
	mov	rdi, [rbx + ChkCtx.tree]
	call	ast_node_count
	mov	[chk_n], rax
	mov	rdi, [rbx + ChkCtx.arena]
	lea	rsi, [rax + 1]
	call	arena_alloc
	mov	r13, rax
	mov	rdi, rax
	mov	rcx, [chk_n]
	inc	rcx
	xor	al, al
	cld
	rep	stosb			; scratch arena content is not promised
					; zero -- this pass owns making it so
	mov	rbx, [chk_c]
	mov	[rbx + ChkCtx.taint], r13

	mov	r12, 1
  .node:
	cmp	r12, [chk_n]
	ja	.out
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_taint_of
	mov	rcx, [rbx + ChkCtx.taint]
	mov	[rcx + r12], al
	inc	r12
	jmp	.node
  .out:
	return
endp

; __chk_tainted(chk_c) -> al = 1 if `E0301` must be WITHHELD right now: any
; currently open frame's node is tainted, or -- with no frame open -- the
; module root is. Ancestor-inclusive, not just the immediate frame: c14's
; `return` (this file's header) sits in a `Block` one level below the one
; whose sibling statement is the `Error` node, so the check walks every open
; frame, same "innermost outward, but the whole stack" shape `__chk_lookup`
; already uses for names. O(frame depth), not a second tree walk.
proc __chk_tainted, chk_c
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	cmp	qword [rbx + ChkCtx.taint], 0
	je	.no			; defensive: pass 1 always runs the taint
					; pass first, so this should not happen
	cmp	qword [rbx + ChkCtx.frames.len], 0
	jne	.frame
	mov	rdi, [rbx + ChkCtx.tree]
	mov	eax, [rdi + Ast.root]
	test	eax, eax
	jz	.no
	mov	rcx, [rbx + ChkCtx.taint]
	movzx	eax, byte [rcx + rax]
	test	al, al
	jz	.no
	mov	al, 1
	return

  .frame:
	xor	r12, r12
  .up:
	cmp	r12, [rbx + ChkCtx.frames.len]
	jae	.no
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_frame_at
	mov	eax, [rax + ChkFrame.node]
	test	eax, eax
	jz	.next
	mov	rcx, [rbx + ChkCtx.taint]
	movzx	edx, byte [rcx + rax]
	test	dl, dl
	jz	.next
	mov	al, 1
	return
  .next:
	inc	r12
	jmp	.up
  .no:
	xor	al, al
	return
endp

; =============================================================================
; pass 1 -- the walk
; =============================================================================

; __chk_cap_dup(chk_c, chk_n) -- `EXS-E0422` at node `chk_n`, with the fix
; §8.3 promises and `diag/fix.inc` names: "delete the second binding".
proc __chk_cap_dup, chk_c, chk_n
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	rdi, rbx
	mov	rsi, CHK_P_RESOLVE
	mov	rdx, 422
	mov	rcx, [chk_n]
	call	chk_raise
	mov	r12, rax
	mov	dword [r12 + ChkDiag.fkind], DIAG_FIX_DELETE
	mov	eax, [r12 + ChkDiag.caret.file_id]
	mov	[r12 + ChkDiag.fx.file_id], eax
	mov	eax, [r12 + ChkDiag.caret.start]
	mov	[r12 + ChkDiag.fx.start], eax
	mov	eax, [r12 + ChkDiag.caret.len]
	mov	[r12 + ChkDiag.fx.len], eax
	return
endp

; __chk_cap_bind(chk_c, chk_ord, chk_d, chk_n) -- make declaration `chk_d` the
; provider of atom `chk_ord` in the innermost frame. `chk_n` is the node a
; diagnostic goes on.
;
; SPEC §4.5: "shadowing is an error", and checker.md section 2.1's rule 1 makes
; that ONE VALUE PER CAPABILITY TYPE PER SCOPE -- an implicit draw with two
; providers to choose between would be the search the kill criterion forbids.
; So the bind runs a lookup FIRST and a hit anywhere in the enclosing frames is
; `EXS-E0422`. The atom is bound anyway, so that draws after it resolve to
; something rather than cascading.
proc __chk_cap_bind, chk_c, chk_ord, chk_d, chk_n
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_ord]
	rassert	r12 ge 1
	mov	rcx, AST_CAP_COUNT
	rassert	r12 le rcx
	mov	rdi, rbx
	xor	rsi, rsi		; by ordinal, never by name
	mov	rdx, r12
	call	__chk_lookup
	test	rax, rax
	jz	.free
	mov	rdi, rbx
	mov	rsi, [chk_n]
	call	__chk_cap_dup
  .free:
	mov	rdi, rbx
	call	__chk_frame_top
	test	rax, rax
	jz	.out
	lea	rcx, [rax + ChkFrame.caps]
	mov	edx, [chk_d]
	mov	[rcx + r12*4 - 4], edx
  .out:
	return
endp

; __chk_cap_bind_root(chk_c, chk_ord, chk_d, chk_n) -- the same, into the
; innermost `FN`/`LAMBDA` frame rather than the innermost frame. A parameter of
; capability type and a declared `poscit` item both provide for the whole
; function body, not for the signature alone.
proc __chk_cap_bind_root, chk_c, chk_ord, chk_d, chk_n
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_ord]
	rassert	r12 ge 1
	mov	rcx, AST_CAP_COUNT
	rassert	r12 le rcx
	mov	rdi, rbx
	xor	rsi, rsi
	mov	rdx, r12
	call	__chk_lookup
	test	rax, rax
	jz	.free
	mov	rdi, rbx
	mov	rsi, [chk_n]
	call	__chk_cap_dup
  .free:
	mov	rdi, rbx
	call	__chk_frame_root
	test	rax, rax
	jz	.out
	lea	rcx, [rax + ChkFrame.caps]
	mov	edx, [chk_d]
	mov	[rcx + r12*4 - 4], edx
  .out:
	return
endp

; __chk_fix_e0421(chk_c, chk_dg, chk_ord) -- §8.3 promises `E0421` a fix and
; `diag/fix.inc`'s header names it: "the insertion of the missing atom into the
; enclosing declaration's row". Three shapes, each ONE edit:
;
;   - the enclosing `Sig` already has a `DeclRow`  ->  `, atom` at its end;
;   - it has none                                  ->  ` poscit atom` at the end
;     of the signature, which spec §8.6 puts exactly there ("bare row after
;     `-> Type` or `)`");
;   - its row is the explicit empty one, `poscit {}` -> the whole row replaced
;     by `poscit atom` (a REPLACE, not an insertion: `, atom` after `}` would
;     not parse).
;
; With no enclosing signature at all -- a draw at module level -- there is
; nothing to insert into and the diagnostic carries no fix. That is the one case
; `diag_fix_required` promises a fix for and this does not supply one, and it is
; recorded rather than papered over: spec §4.1 rule 7 forbids the construct that
; reaches it, and the code for that (`E0501`, capability in module-level state)
; is pass 3's, which will report the same line.
proc __chk_fix_e0421, chk_c, chk_dg, chk_ord
	uses	rbx, r12, r13, r14
	locals
		slot	chk_row, dq
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_dg]
	mov	r13, [chk_ord]
	mov	rdi, rbx
	call	__chk_frame_root
	test	rax, rax
	jz	.out
	mov	eax, [rax + ChkFrame.node]
	test	eax, eax
	jz	.out
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rax
	call	ast_node_at
	mov	eax, [rax + AstNode.a]	; `Fn.a` / `Lambda.a` = the `Sig`
	test	eax, eax
	jz	.out
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rax
	call	ast_node_at
	mov	r14, rax
	mov	eax, [r14 + AstNode.d]	; `Sig.d` = the `Row`, or 0
	mov	[chk_row], rax
	test	eax, eax
	jz	.norow
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_row]
	call	ast_node_at
	mov	r14, rax
	cmp	dword [r14 + AstNode.b], 0	; the row's item count
	je	.emptyrow
	mov	rdi, rbx
	lea	rsi, [chk_lit_comma]
	mov	rdx, CHK_LIT_COMMA_LEN
	mov	rcx, r13
	call	__chk_fix_text
	jmp	.place
  .emptyrow:
	; THE EXPLICIT EMPTY ROW, `poscit {}` (spec §4.1 rule 6, §8.6). It is a
	; `Row` node with no items, so ", atom" at its end would write
	; `poscit {}, atom` -- source that does not parse, offered as a
	; machine-applicable fix. The one edit that is right is to replace the
	; whole row, braces included, with `poscit atom`; replacing the row's span
	; rather than inserting between the braces is what makes it independent
	; of whatever sits inside them (`{ }`, a comment).
	mov	rdi, rbx
	lea	rsi, [chk_lit_poscit_bare]
	mov	rdx, CHK_LIT_POSCIT_BARE_LEN
	mov	rcx, r13
	call	__chk_fix_text
	mov	[r12 + ChkDiag.ftxt], rax
	mov	[r12 + ChkDiag.ftxtn], rdx
	mov	dword [r12 + ChkDiag.fkind], DIAG_FIX_REPLACE
	mov	ecx, [r14 + AstNode.span.file_id]
	mov	[r12 + ChkDiag.fx.file_id], ecx
	mov	ecx, [r14 + AstNode.span.start]
	mov	[r12 + ChkDiag.fx.start], ecx
	mov	ecx, [r14 + AstNode.span.len]
	mov	[r12 + ChkDiag.fx.len], ecx
	jmp	.out
  .norow:
	mov	rdi, rbx
	lea	rsi, [chk_lit_poscit]
	mov	rdx, CHK_LIT_POSCIT_LEN
	mov	rcx, r13
	call	__chk_fix_text
  .place:
	mov	[r12 + ChkDiag.ftxt], rax
	mov	[r12 + ChkDiag.ftxtn], rdx
	mov	dword [r12 + ChkDiag.fkind], DIAG_FIX_INSERT
	mov	ecx, [r14 + AstNode.span.file_id]
	mov	[r12 + ChkDiag.fx.file_id], ecx
	mov	ecx, [r14 + AstNode.span.start]
	add	ecx, [r14 + AstNode.span.len]
	mov	[r12 + ChkDiag.fx.start], ecx
	mov	dword [r12 + ChkDiag.fx.len], 0
  .out:
	return
endp

; __chk_draw(chk_c, chk_n, chk_ord, chk_nm) -> rax = the declaration the draw
; resolves to. checker.md section 2.1's "Lookup(P)", including both its endings:
;
;   - a hit anywhere in the frames is the provider;
;   - at the function root with no hit, a DECLARED row makes it `EXS-E0421` at
;     the drawing node, and an INFERRED row makes it a contribution recorded in
;     the use list for pass 3's fixpoint.
;
; Either way the node resolves to the atom's own declaration rather than to 0,
; so nothing downstream sees an unresolved name and reports it a second time.
;
; SCOPE, STATED SO IT IS NOT MISTAKEN FOR MORE THAN IT IS: this is the DIRECT
; draw -- an atom named in expression position, checker.md section 2.1's first
; "what draws". The other three (a call whose callee's row contains P after
; substitution, a `*T` requiring `Crudum`, an inner lambda's own contributions)
; need types and are pass 2's and pass 3's; `EXS-E0421` for those is raised
; there, from the same use list this one appends to.
proc __chk_draw, chk_c, chk_n, chk_ord, chk_nm
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_n]
	mov	r13, [chk_ord]
	mov	rdi, rbx
	mov	rsi, [chk_nm]
	mov	rdx, r13
	call	__chk_lookup
	test	rax, rax
	jz	.miss
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, rdx		; the frame it was found in
	mov	rdx, r13
	mov	rcx, r12
	call	__chk_note_captures
	mov	rax, r14
	return

  .miss:
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_atom_decl
	mov	r14, rax		; the atom's own declaration, or 0

	cmp	dword [rbx + ChkCtx.rowdecl], 0
	jne	.undeclared

	; INFERRED: this draw joins the enclosing function's row (spec §4.1
	; rule 5). Recorded, never resolved here -- the fixpoint is pass 3's.
	lea	rdi, [rbx + ChkCtx.draws]
	call	vec_push
	mov	ecx, [rbx + ChkCtx.owner]
	mov	[rax + ChkUse.owner], ecx
	mov	[rax + ChkUse.node], r12d
	mov	[rax + ChkUse.ord], r13d
	mov	byte [rax + ChkUse.kind], CHK_USE_DRAW
	mov	byte [rax + ChkUse.pad0], 0
	mov	word [rax + ChkUse.pad1], 0
	mov	rax, r14
	return

  .undeclared:
	mov	rdi, rbx
	mov	rsi, CHK_P_RESOLVE
	mov	rdx, 421
	mov	rcx, r12
	call	chk_raise
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, r13
	call	__chk_fix_e0421
	mov	rax, r14
	return
endp

; __chk_path_genargs(chk_c, chk_n) -- walk every segment's `GenericArgs`
; subtree. `Seg.b` is the slot (typed-ast.md section 2.3), and its contents are
; types, so the descent enters them in type position of its own accord.
proc __chk_path_genargs, chk_c, chk_n
	uses	rbx, r12, r13
	locals
		slot	chk_off, dq
		slot	chk_cnt, dq
	endl
	mov	rbx, [chk_c]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_n]
	call	ast_node_at
	mov	ecx, [rax + AstNode.a]
	mov	[chk_off], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chk_cnt], rcx
	xor	r12, r12
  .seg:
	cmp	r12, [chk_cnt]
	jae	.out
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_off]
	add	rsi, r12
	call	ast_extra_at
	mov	r13, rax
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r13
	call	ast_node_at
	mov	ecx, [rax + AstNode.b]
	test	ecx, ecx
	jz	.next
	mov	rdi, rbx
	mov	rsi, rcx
	mov	rdx, 1
	call	__chk_walk
  .next:
	inc	r12
	jmp	.seg
  .out:
	return
endp

; `chk_pos` = 2: a `casus` PATTERN (docs/design/sum-types.md D2). Passed by the
; `.casus` arm of `__chk_walk` to the pattern node alone; nothing inherits it
; past a `Path`, because a pattern is a `Lit` or a `Path` and a path's only
; walked children are its generic arguments, which are walked as types.
CHK_POS_PATTERN = 2

; __chk_path(chk_c, chk_n, chk_pos) -- resolve `Path` node `chk_n`.
; `chk_pos` = 0 in expression position, 1 in type position (this file's header
; explains why a type-position miss is silent).
;
; ONLY THE FIRST SEGMENT. `A.B` in type position, and a pattern's qualified
; path, need `A`'s meaning before `B` can be looked up in it; that is pass 2's,
; exactly as `Member.d` is. `Path.d` is filled only for a one-segment path,
; where it is the same answer as `Seg.d`.
proc __chk_path, chk_c, chk_n, chk_pos
	uses	rbx, r12, r13, r14
	locals
		slot	chk_cnt, dq
		slot	chk_nm,  dq
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_n]
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_seg0
	mov	r13, rax		; the first `Seg` node
	mov	[chk_cnt], rdx

	; every segment may carry generic arguments, and those are types
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_path_genargs

	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_seg_name
	mov	[chk_nm], rax
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rax
	call	ast_cap_of_name
	mov	r14, rax		; the §4.6 ordinal, or 0

	cmp	qword [chk_pos], CHK_POS_PATTERN
	je	.patpos
	cmp	qword [chk_pos], 0
	jne	.typepos

	test	r14, r14
	jz	.ordinary
	; an atom in expression position IS a draw (checker.md section 2.1)
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r14
	mov	rcx, [chk_nm]
	call	__chk_draw
	jmp	.have

  .ordinary:
	mov	rdi, rbx
	mov	rsi, [chk_nm]
	xor	rdx, rdx
	call	__chk_lookup
	test	rax, rax
	jnz	.have
	; a genuine miss -- withhold E0301 inside a recovery artifact's taint
	; (this file's header, "E0301 AND RECOVERY ARTIFACTS")
	mov	rdi, rbx
	call	__chk_tainted
	test	al, al
	jz	.raise301
	inc	dword [rbx + ChkCtx.nsupp]
	xor	rax, rax
	jmp	.have
  .raise301:
	mov	rdi, rbx
	mov	rsi, CHK_P_RESOLVE
	mov	rdx, 301
	mov	rcx, r13
	call	chk_raise
	xor	rax, rax
	jmp	.have

  .patpos:
	; a `casus` pattern (the `.casus` arm says why a miss is silent here,
	; and why an atom is not a draw in this position)
	mov	rdi, rbx
	mov	rsi, [chk_nm]
	xor	rdx, rdx
	call	__chk_lookup
	jmp	.have

  .typepos:
	test	r14, r14
	jz	.typename
	; `m: Mundus` -- the segment names the atom's own declaration, which is
	; what pass 2 interns `AST_TY_CAP` over (`a` = the atom declaration).
	mov	rdi, rbx
	mov	rsi, r14
	call	__chk_atom_decl
	jmp	.have
  .typename:
	mov	rdi, rbx
	mov	rsi, [chk_nm]
	xor	rdx, rdx
	call	__chk_lookup
	; a miss is SILENT here -- see this file's header

  .have:
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r14
	call	__chk_setd
	cmp	qword [chk_cnt], 1
	jne	.out
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__chk_setd
  .out:
	return
endp

; __chk_member(chk_c, chk_n) -- record a CANDIDATE for `Member` node `chk_n`.
; See this file's header: a hint the types pass overwrites, never an answer, and
; it never raises a diagnostic.
proc __chk_member, chk_c, chk_n
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_n]
	call	ast_node_at
	mov	ecx, [rax + AstNode.b]	; `Member.b` = the member's name id
	test	ecx, ecx
	jz	.out
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_memb_lookup
	test	rax, rax
	jz	.out
	mov	rdi, rbx
	mov	rsi, [chk_n]
	mov	rdx, rax
	call	__chk_setd
  .out:
	return
endp

; __chk_rowitem(chk_c, chk_n, chk_rpos) -- one item of a `poscit` row.
;
; `sicut IDENT` (aux has AST_ROW_SICUT): `a` is an interner id naming a
; PARAMETER and `d` becomes an ordinal, positionally, under spec §4.2's
; substitution. Both are the rows agent's (checker.md section 2.3); this pass
; leaves the slot at 0 and says nothing.
;
; A `Path` item: an ATOM binds the atom's own declaration as the provider in the
; function root frame -- checker.md section 2.1's "a declared row item
; `poscit P` sets `cap[P]` in the root frame to the atom's own decl, meaning the
; caller's carrier". A non-atom is a `potestas` name, resolved through the
; module table; spec §4.1 flattens it to its atoms at interning, which is pass
; 3's, so nothing is bound for it here. A name that is neither is `EXS-E0301`.
;
; ONLY A DECLARED ROW BINDS. `chk_rpos` is the walk's position: 0 for the row
; of a function's own `Sig` (the declaration's `poscit`), 1 for a row written
; inside a TYPE -- `functio(A) -> B poscit {P}` as a parameter's or a result's
; type, `dyn I poscit {P}` in a cast. A type's row says what a VALUE of that
; type carries (spec §4.2: "rows travel with values"); it provides no carrier
; to the function whose signature happens to mention the type. Until
; 2026-10-02 every item bound, so `f(s: Scriptor, g: functio(u8) -> u8
; poscit {ambitus}) -> u8 poscit sicut s` could name `ambitus` in its body
; and was accepted -- measured with `exsc aedifica`, and the hole
; docs/design/archivum-beneath.md D6's R1 (the raw `archivum` atom cannot be
; obtained from a `Directorium`) depends on being shut.
; A type-position item is still RESOLVED (`d` set), because pass 3 interns
; that row; it is just not a provider. `tests/unit/chk_directorium.asm`
; pins it.
proc __chk_rowitem, chk_c, chk_n, chk_rpos
	uses	rbx, r12, r13, r14
	locals
		slot	chk_p, dq
		slot	chk_o, dq
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_n]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.aux]
	test	ecx, AST_ROW_SICUT
	jnz	.out
	mov	ecx, [rax + AstNode.a]
	mov	[chk_p], rcx
	test	ecx, ecx
	jz	.out
	mov	rdi, rbx
	mov	rsi, [chk_p]
	call	__chk_seg0
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_seg_name
	mov	r14, rax
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r14
	call	ast_cap_of_name
	mov	[chk_o], rax
	test	rax, rax
	jz	.potestas
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_atom_decl
	jmp	.record

  .potestas:
	mov	rdi, rbx
	mov	rsi, r14
	xor	rdx, rdx
	call	__chk_lookup
	test	rax, rax
	jnz	.record
	; a genuine miss -- withhold E0301 inside a recovery artifact's taint
	; (this file's header, "E0301 AND RECOVERY ARTIFACTS")
	mov	rdi, rbx
	call	__chk_tainted
	test	al, al
	jz	.rowitem301
	inc	dword [rbx + ChkCtx.nsupp]
	xor	rax, rax
	jmp	.record
  .rowitem301:
	mov	rdi, rbx
	mov	rsi, CHK_P_RESOLVE
	mov	rdx, 301
	mov	rcx, r13
	call	chk_raise
	xor	rax, rax

  .record:
	mov	r14, rax		; the declaration this item names, or 0
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r14
	call	__chk_setd
	mov	rdi, rbx
	mov	rsi, [chk_p]
	mov	rdx, r14
	call	__chk_setd
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r14
	call	__chk_setd
	cmp	qword [chk_o], 0
	je	.out
	cmp	qword [chk_rpos], 0
	jne	.out			; a type's row: resolved, never a provider
	mov	rdi, rbx
	mov	rsi, [chk_o]
	mov	rdx, r14
	mov	rcx, r12
	call	__chk_cap_bind_root
  .out:
	return
endp

; __chk_kids(chk_c, chk_n, chk_pos) -- the GENERIC descent: every operand slot
; whose role names a child, in slot order, then every element of every list
; slot. Driven by `ast_roles` (ast/kinds.inc) rather than by a switch, so a node
; kind this pass has no opinion about is still walked correctly, and a new kind
; is picked up without an edit here.
;
; `chk_pos` is inherited; `__chk_walk` is where it is set to 1, at the first
; `Ty*` node, and every descendant of a type is a type or a literal.
proc __chk_kids, chk_c, chk_n, chk_pos
	uses	rbx, r12, r13, r14
	locals
		slot	chk_k,   dq
		slot	chk_r,   dq
		slot	chk_off, dq
		slot	chk_cnt, dq
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_n]
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	mov	[chk_k], rcx
	xor	r13, r13
  .slot:
	cmp	r13, 4
	jae	.out
	mov	rdi, [chk_k]
	mov	rsi, r13
	call	ast_role_of
	mov	[chk_r], rax
	cmp	eax, AST_R_NODE
	je	.child
	cmp	eax, AST_R_NODEQ
	je	.child
	cmp	eax, AST_R_LIST
	je	.list
	cmp	eax, AST_R_LIST2
	je	.list
	jmp	.next
  .child:
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	lea	rcx, [rax + AstNode.a]
	mov	eax, [rcx + r13*4]
	test	eax, eax
	jz	.next
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chk_pos]
	call	__chk_walk
	jmp	.next
  .list:
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	lea	rcx, [rax + AstNode.a]
	mov	eax, [rcx + r13*4]
	mov	[chk_off], rax
	lea	rdx, [r13 + 1]
	mov	eax, [rcx + rdx*4]
	mov	[chk_cnt], rax
	cmp	qword [chk_r], AST_R_LIST2
	jne	.have_n
	shl	qword [chk_cnt], 1	; `Si` stores (cond, block) PAIRS
  .have_n:
	xor	r14, r14
  .elem:
	cmp	r14, [chk_cnt]
	jae	.next
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_off]
	add	rsi, r14
	call	ast_extra_at
	test	eax, eax
	jz	.elemnext
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chk_pos]
	call	__chk_walk
  .elemnext:
	inc	r14
	jmp	.elem
  .next:
	inc	r13
	jmp	.slot
  .out:
	return
endp

; __chk_walk(chk_c, chk_n, chk_pos) -- the preorder descent. Every kind that
; opens a scope, binds a name, or denotes one is handled here; everything else
; falls through to `__chk_kids`.
;
; RECURSION, not an explicit stack. Depth is the tree's, which is the source's
; nesting depth; cst/parse.inc already recursed to build it and nothing here is
; deeper than that walk was.
proc __chk_walk, chk_c, chk_n, chk_pos
	uses	rbx, r12, r13, r14
	locals
		slot	chk_k,   dq
		slot	chk_a,   dq
		slot	chk_b,   dq
		slot	chk_cc,  dq
		slot	chk_dd,  dq
		slot	chk_ord, dq
		slot	chk_sv,  dq
		slot	chk_sv2, dq
		slot	chk_i,   dq
	endl
	mov	rbx, [chk_c]
	mov	r12, [chk_n]
	test	r12, r12
	jz	.out
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	mov	[chk_k], rcx
	mov	ecx, [rax + AstNode.a]
	mov	[chk_a], rcx
	mov	ecx, [rax + AstNode.b]
	mov	[chk_b], rcx
	mov	ecx, [rax + AstNode.c]
	mov	[chk_cc], rcx
	mov	ecx, [rax + AstNode.d]
	mov	[chk_dd], rcx

	; every `Ty*` node puts its whole subtree in type position
	mov	rcx, [chk_k]
	cmp	rcx, AST_TYPATH
	jb	.notype
	cmp	rcx, AST_TYSUFFIX
	ja	.notype
	mov	qword [chk_pos], 1
  .notype:

	mov	rcx, [chk_k]
	cmp	rcx, AST_PATH
	je	.path
	cmp	rcx, AST_MEMBER
	je	.member
	cmp	rcx, AST_ROWITEM
	je	.rowitem
	cmp	rcx, AST_FN
	je	.fn
	cmp	rcx, AST_TYPUS
	je	.tydecl
	cmp	rcx, AST_STRUCT
	je	.tydecl
	cmp	rcx, AST_IFACE
	je	.tydecl
	cmp	rcx, AST_LAMBDA
	je	.lambda
	cmp	rcx, AST_BLOCK
	je	.block
	cmp	rcx, AST_PARAM
	je	.param
	cmp	rcx, AST_GENPARAM
	je	.genparam
	cmp	rcx, AST_BINDING
	je	.binding
	cmp	rcx, AST_SUB
	je	.sub
	cmp	rcx, AST_FOR
	je	.for
	cmp	rcx, AST_CONTRAHE
	je	.contrahe
	cmp	rcx, AST_CASUS
	je	.casus
	cmp	rcx, AST_ERROR
	je	.out			; AST H3: an `Error` node says nothing here

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [chk_pos]
	call	__chk_kids
	jmp	.out

  .path:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [chk_pos]
	call	__chk_path
	jmp	.out

  .member:
	; the receiver first, then the generic arguments, then the candidate
	mov	rdi, rbx
	mov	rsi, [chk_a]
	mov	rdx, [chk_pos]
	call	__chk_walk
	mov	rdi, rbx
	mov	rsi, [chk_cc]
	mov	rdx, 1
	call	__chk_walk
	mov	rdi, rbx
	mov	rsi, r12
	call	__chk_member
	jmp	.out

  .rowitem:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [chk_pos]
	call	__chk_rowitem
	jmp	.out

  .fn:
	; `Fn.a` = `Sig`, `Fn.b` = body (0 for an interface member or an
	; `externus` signature), `Fn.d` = the declaration.
	mov	eax, [rbx + ChkCtx.owner]
	mov	[chk_sv], rax
	mov	eax, [rbx + ChkCtx.rowdecl]
	mov	[chk_sv2], rax
	mov	ecx, [chk_dd]
	mov	[rbx + ChkCtx.owner], ecx
	; DECLARED vs INFERRED (checker.md section 2.3): an explicit `poscit` is
	; declared, and so is `publica` -- spec §4.1 rule 6's "no `poscit` ... is
	; pure" is read as the meaning of an explicitly empty DECLARED row, which
	; cannot also govern a private function without contradicting rule 5
	; (that section's finding 10).
	mov	dword [rbx + ChkCtx.rowdecl], 0
	mov	rcx, [chk_a]
	test	rcx, rcx
	jz	.fn_pub
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rcx
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	test	ecx, ecx
	jz	.fn_pub
	mov	dword [rbx + ChkCtx.rowdecl], 1
  .fn_pub:
	mov	rcx, [chk_dd]
	test	rcx, rcx
	jz	.fn_open
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rcx
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.flags]
	test	ecx, AST_F_PUBLICA
	jz	.fn_open
	mov	dword [rbx + ChkCtx.rowdecl], 1
  .fn_open:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CHK_FR_FN
	call	__chk_frame_push
	; `Fn.c` = `Generics` (typed-ast.md section 2.4's table) -- bind the
	; function's own generic parameters into the `CHK_FR_FN` frame BEFORE the
	; signature is walked, since a parameter type may name one (`.genparam:`
	; already knows how to bind a single `GenericParam`; `__chk_walk`'s
	; default `__chk_kids` fallback iterates the `Generics` list into it).
	mov	rdi, rbx
	mov	rsi, [chk_cc]
	xor	rdx, rdx
	call	__chk_walk		; the function's own generic parameters
	mov	rdi, rbx
	mov	rsi, [chk_a]
	xor	rdx, rdx
	call	__chk_walk		; the signature: params, result, row
	mov	rdi, rbx
	mov	rsi, [chk_b]
	xor	rdx, rdx
	call	__chk_walk		; the body
	mov	rdi, rbx
	call	__chk_frame_pop
	mov	eax, [chk_sv]
	mov	[rbx + ChkCtx.owner], eax
	mov	eax, [chk_sv2]
	mov	[rbx + ChkCtx.rowdecl], eax
	jmp	.out

  .tydecl:
	; `typus T<…> = …`, `structura S<…> { … }`, `interfacies I<…> { … }`.
	; Modelled on `.fn` above and for its reason: spec §8.6 gives all three
	; an optional `[GenericParams]`, and until this arm existed pass 1 never
	; put them in scope over the body. `__chk_kids`' slot order walked the
	; BODY first (`Typus.a`, `Struct.a`/`Interface.a`) and the `Generics`
	; (`.c`) after it, and then `.genparam`'s `__chk_bind` found no frame
	; open at module level and dropped the parameter on the floor -- so
	; `typus box<T> = refero<T>;`, `typus e2<T, E> = casus prosperum(T),
	; casus adversum(E);`, `structura S<T> { x: T }` and
	; `interfacies I<T> { functio acc(self: I, v: T) -> T }` all raised
	; `EXS-E0301` at the parameter's USE. MEASURED, all four, with
	; `--diagnostica json` before this arm; `potestas` is not in the list
	; because its grammar takes no generic parameters (`Potestas.c` is
	; `AST_R_NONE` and `potestas P<T>` is `EXS-E0201` at the `<`).
	;
	; One arm for three kinds, because the scoping question is one question.
	; They differ only in the SHAPE of the body slot: `Typus.a` is a single
	; node (a `Ty*` for an alias, an `AST_SUMBODY` for a sum -- the child's
	; kind is the discriminator, sum-types.md D1), while `Struct.a` and
	; `Interface.a` are a member LIST with `.b` its count.
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CHK_FR_TYDECL
	call	__chk_frame_push
	mov	rdi, rbx
	mov	rsi, [chk_cc]		; `Generics`, or 0 -- BEFORE the body
	xor	rdx, rdx
	call	__chk_walk
	cmp	qword [chk_k], AST_TYPUS
	je	.tydecl_one
	mov	qword [chk_i], 0
  .tydecl_elem:
	mov	rax, [chk_i]
	cmp	rax, [chk_b]
	jae	.tydecl_done
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_a]
	add	rsi, [chk_i]
	call	ast_extra_at
	test	eax, eax
	jz	.tydecl_next
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chk_pos]
	call	__chk_walk
  .tydecl_next:
	inc	qword [chk_i]
	jmp	.tydecl_elem
  .tydecl_one:
	mov	rdi, rbx
	mov	rsi, [chk_a]
	mov	rdx, [chk_pos]
	call	__chk_walk
  .tydecl_done:
	mov	rdi, rbx
	call	__chk_frame_pop
	jmp	.out

  .lambda:
	; spec §8.6 decision 2: a lambda takes no row and its row is ALWAYS
	; inferred, so `rowdecl` is 0 for the whole of its body.
	mov	eax, [rbx + ChkCtx.owner]
	mov	[chk_sv], rax
	mov	eax, [rbx + ChkCtx.rowdecl]
	mov	[chk_sv2], rax
	mov	ecx, [chk_dd]
	mov	[rbx + ChkCtx.owner], ecx
	mov	dword [rbx + ChkCtx.rowdecl], 0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CHK_FR_LAMBDA
	call	__chk_frame_push
	mov	rdi, rbx
	mov	rsi, [chk_a]
	xor	rdx, rdx
	call	__chk_walk
	mov	rdi, rbx
	mov	rsi, [chk_b]
	xor	rdx, rdx
	call	__chk_walk
	mov	rdi, rbx
	call	__chk_frame_pop
	mov	eax, [chk_sv]
	mov	[rbx + ChkCtx.owner], eax
	mov	eax, [chk_sv2]
	mov	[rbx + ChkCtx.rowdecl], eax
	jmp	.out

  .block:
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CHK_FR_BLOCK
	call	__chk_frame_push
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [chk_pos]
	call	__chk_kids
	mov	rdi, rbx
	call	__chk_frame_pop
	jmp	.out

  .casus:
	; `casus P(n, …) B` (sum-types.md D2): the pattern's bindings are
	; visible in the arm's block and NOWHERE ELSE, so the arm is a frame
	; of its own -- the pattern walked first (its path may not see the
	; names it binds), then each binding bound, then the block, which
	; opens its own frame inside this one. Bindings are `Casus.c`/`d`,
	; `Binding` nodes with no annotation and no initializer; walking them
	; through `.binding` would bind them too, but in whichever frame
	; happened to be innermost -- the FUNCTION's, since an arm is not a
	; block -- and two arms binding `n` would then be `EXS-E0302`.
	;
	; THE PATTERN IS WALKED IN PATTERN POSITION (`chk_pos` = 2), where a
	; name that resolves to nothing is SILENT: D2 resolves a pattern's path
	; against the SCRUTINEE'S VARIANTS FIRST and only then against the value
	; namespace, and the scrutinee's type is pass 2's, so pass 2 is where a
	; pattern that names nothing becomes `EXS-E0301`
	; (checker/types/stmt.inc's `__chk_ty_pattern`). What this pass binds is
	; the value-namespace answer, which pass 2 overrides with the variant
	; when the scrutinee is a sum.
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CHK_FR_BLOCK
	call	__chk_frame_push
	mov	rdi, rbx
	mov	rsi, [chk_a]
	mov	rdx, CHK_POS_PATTERN
	call	__chk_walk
	mov	qword [chk_i], 0
  .casus_bind:
	mov	rax, [chk_i]
	cmp	rax, [chk_dd]
	jae	.casus_body
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, [chk_cc]
	add	rsi, [chk_i]
	call	ast_extra_at
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rax
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	test	rcx, rcx
	jz	.casus_bind_next
	mov	rdi, rbx
	mov	rsi, rcx
	call	__chk_bind
  .casus_bind_next:
	inc	qword [chk_i]
	jmp	.casus_bind
  .casus_body:
	mov	rdi, rbx
	mov	rsi, [chk_b]
	xor	rdx, rdx
	call	__chk_walk
	mov	rdi, rbx
	call	__chk_frame_pop
	jmp	.out

  .param:
	; the type first: it may not see the parameter, and it is where a
	; capability-typed parameter is recognised (spec §4.3).
	mov	rdi, rbx
	mov	rsi, [chk_b]
	mov	rdx, 1
	call	__chk_walk
	mov	rdi, rbx
	mov	rsi, [chk_b]
	call	__chk_type_atom
	test	rax, rax
	jz	.param_bind
	cmp	qword [chk_dd], 0
	je	.param_bind
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [chk_dd]
	mov	rcx, r12
	call	__chk_cap_bind_root
  .param_bind:
	mov	rdi, rbx
	mov	rsi, [chk_dd]
	call	__chk_bind
	jmp	.out

  .genparam:
	mov	rdi, rbx
	mov	rsi, [chk_b]
	mov	rdx, 1
	call	__chk_walk		; the bound, an interface
	mov	rdi, rbx
	mov	rsi, [chk_dd]
	call	__chk_bind
	jmp	.out

  .binding:
	; `Binding.a` = the annotation, `Binding.b` = the initializer, and
	; NEITHER may see the name being bound: checker.md section 2.2's
	; "bindings are visible only after their declaration".
	mov	rdi, rbx
	mov	rsi, [chk_a]
	mov	rdx, 1
	call	__chk_walk
	mov	rdi, rbx
	mov	rsi, [chk_b]
	xor	rdx, rdx
	call	__chk_walk
	mov	rdi, rbx
	mov	rsi, [chk_dd]
	call	__chk_bind
	jmp	.out

  .sub:
	; `SubStmt ::= 'sub' Path '=' ExprNS ( ';' | Block )` (spec §8.6).
	; `Sub.a` = the `Path` naming the atom, `Sub.b` = the provider
	; expression, `Sub.c` = the block (0 in the statement form),
	; `Sub.d` = the declaration.
	mov	rdi, rbx
	mov	rsi, [chk_b]
	xor	rdx, rdx
	call	__chk_walk		; the provider, in the enclosing scope
	mov	rdi, rbx
	mov	rsi, [chk_a]
	call	__chk_seg0
	mov	r13, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	__chk_seg_name
	mov	rdi, [rbx + ChkCtx.tree]
	mov	rsi, rax
	call	ast_cap_of_name
	mov	[chk_ord], rax
	; `sub` OF A NON-ATOM is checker.md section 2.4's class L, since numbered
	; `EXS-E0423` (row item malformed), and it belongs with the rest of the
	; row rules in pass 3 -- raising it here would put half of one class in
	; two passes. The `Path` is left unresolved and unreported rather than
	; run through ordinary name resolution, which would report a DIFFERENT
	; thing (`EXS-E0301`) about the same token.
	test	rax, rax
	jz	.sub_body
	mov	rdi, rbx
	mov	rsi, rax
	call	__chk_atom_decl
	mov	r14, rax
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, r14
	call	__chk_setd
	mov	rdi, rbx
	mov	rsi, [chk_a]
	mov	rdx, r14
	call	__chk_setd
  .sub_body:
	cmp	qword [chk_cc], 0
	jne	.sub_scoped
	; STATEMENT FORM: the binding is in the enclosing scope, from here on.
	cmp	qword [chk_ord], 0
	je	.sub_name
	cmp	qword [chk_dd], 0
	je	.sub_name
	mov	rdi, rbx
	mov	rsi, [chk_ord]
	mov	rdx, [chk_dd]
	mov	rcx, r12
	call	__chk_cap_bind
  .sub_name:
	mov	rdi, rbx
	mov	rsi, [chk_dd]
	call	__chk_bind
	jmp	.out
  .sub_scoped:
	; BLOCK FORM: the binding is the block's alone. A frame of its own,
	; because the declaration lives in the ENCLOSING block's declaration
	; range (measured) and would otherwise outlive its `{ }`.
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CHK_FR_BLOCK
	call	__chk_frame_push
	cmp	qword [chk_ord], 0
	je	.sub_sname
	cmp	qword [chk_dd], 0
	je	.sub_sname
	mov	rdi, rbx
	mov	rsi, [chk_ord]
	mov	rdx, [chk_dd]
	mov	rcx, r12
	call	__chk_cap_bind
  .sub_sname:
	mov	rdi, rbx
	mov	rsi, [chk_dd]
	call	__chk_bind
	mov	rdi, rbx
	mov	rsi, [chk_cc]
	xor	rdx, rdx
	call	__chk_walk
	mov	rdi, rbx
	call	__chk_frame_pop
	jmp	.out

  .for:
	; `For.a` = the iterable, `For.b` = the body, `For.c` = the `ForHead`
	; (`contrahe` accumulators and `forma`), `For.d` = the loop variable.
	; The iterable is evaluated in the ENCLOSING scope; the loop variable is
	; the body's and gets a frame of its own for the same reason the `sub`
	; block form does.
	;
	; THE HEAD IS WALKED IN THE ENCLOSING SCOPE, and the accumulators it
	; declares are therefore bound THERE, not in the frame that dies with the
	; body. Spec §5.4: "After the loop `acc` is an ordinary binding" -- a
	; sentence with no meaning at all if the binding is gone by then, and
	; every `contrahe` whose result was read was `EXS-E0301` until this line
	; moved (`docs/design/checker.md` finding 27, defect C2). The head holds
	; no expression -- `Contrahe ::= 'contrahe' IDENT ':' ArithOp` and
	; `forma IDENT [INT]` (spec §8.6) -- so nothing in it can name the loop
	; variable and the earlier position bought nothing.
	;
	; The accumulator is still invisible BEFORE the loop, because binding is
	; a step of this walk and the statements above the `For` were walked
	; already; and still visible INSIDE the body, because frames nest. What
	; keeps it write-only in there is `EXS-E0341`, not the scope
	; (checker/types/types.inc, `__chk_ty_path_expr`).
	mov	rdi, rbx
	mov	rsi, [chk_a]
	xor	rdx, rdx
	call	__chk_walk
	mov	rdi, rbx
	mov	rsi, [chk_cc]
	xor	rdx, rdx
	call	__chk_walk
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, CHK_FR_BLOCK
	call	__chk_frame_push
	mov	rdi, rbx
	mov	rsi, [chk_dd]
	call	__chk_bind
	mov	rdi, rbx
	mov	rsi, [chk_b]
	xor	rdx, rdx
	call	__chk_walk
	mov	rdi, rbx
	call	__chk_frame_pop
	jmp	.out

  .contrahe:
	mov	rdi, rbx
	mov	rsi, [chk_dd]
	call	__chk_bind
	jmp	.out

  .out:
	return
endp

; chk_resolve(chk_c) -- passes 0 and 1. Diagnostics are BUFFERED; `chk_flush`
; (called by `chk_run`) is what puts them in the caller's vector, in checker.md
; section 4's order.
proc chk_resolve, chk_c
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]
	mov	rdi, rbx
	call	__chk_pass0
	mov	rdi, rbx
	call	__chk_taint_pass	; before __chk_mods/__chk_walk: both may
					; call __chk_tainted through __chk_path
					; and __chk_rowitem
	mov	rdi, rbx
	call	__chk_mods
	mov	rdi, [rbx + ChkCtx.tree]
	mov	eax, [rdi + Ast.root]
	test	eax, eax
	jz	.done
	mov	rdi, rbx
	mov	rsi, rax
	xor	rdx, rdx
	call	__chk_walk
  .done:
	; every frame opened was closed -- a leak here would silently change
	; what the next module-level name resolves to
	mov	r12, [rbx + ChkCtx.frames.len]
	rassert	r12 eq 0
	or	dword [rbx + ChkCtx.stages], CHK_S_RESOLVE
	return
endp
