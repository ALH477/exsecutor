; compiler/x86_64/ast/ast.inc
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
; The ast/ module's single aggregation point (docs/asm-conventions.md, "7. How
; to add a module", step 3). A consumer includes THIS file, not the pieces.
;
; THE TYPED AST is `docs/design/typed-ast.md`. That document is the contract;
; this module is the first thing to exercise it, and everything below that
; contradicts it is recorded as a finding, in place, rather than absorbed.
;
; The include graph inside ast/ is a strict CHAIN, never a diamond, because
; fasmg has one flat namespace and a `struct` or a `proc` processed twice by
; two paths fails on the second:
;
;   kinds.inc   (pure data: kinds, operand roles, name pools; includes nothing)
;     <- node.inc    (the records and the read-only accessors)
;        <- types.inc   (the interned type table)
;           <- build.inc   (ast_init and the constructors)
;              <- verify.inc  (the invariants)
;                 <- dump.inc    (the textual form)
;                    <- load.inc    (reading it back, and ast_equal)
;                       <- ast.inc  (this file)
;
; WHAT THIS MODULE DOES NOT INCLUDE, and what a consumer must therefore
; include FIRST -- the same contract driver/driver.inc and cst/ already have,
; for the same reason:
;
;	include '<...>/compiler/x86_64/diag/diag.inc'    ; or lexer/lexer.inc,
;	include '<...>/compiler/x86_64/ast/ast.inc'     ; which brings it
;
; ast/ uses rt/{arena,vec,map,intern,span} and diag/render.inc's `DiagOut`,
; and includes none of them: lexer/lexer.inc and diag/diag.inc each bring the
; whole rt/ chain, so a module that also included it would be the second path
; that makes the graph a diamond. `compiler/x86_64/exsc.asm` already includes
; lexer/lexer.inc before driver/driver.inc for exactly this reason; ast/ast.inc
; goes after it.
;
; ---------------------------------------------------------------------------
; THE SEAM IS CLOSED. `ast_from_cst(tree, t, file_id)` (ast/from_cst.inc)
; walks cst/parse.inc's §9.1 tree in postorder and produces this one.
; `compiler/x86_64/exsc.asm` includes cst/cst.inc, then this file, then
; driver/driver.inc; driver/run.inc parses after a clean `lex_run` and builds
; the AST from the result on both the clean and the diagnosed path. Both
; programs in `examples/` lex, parse and build a tree `ast_verify_stage1`
; accepts.
;
; WHAT THE WALKER DOES NOT DO, stated rather than left to be discovered:
;   - It resolves nothing. `Node.ty` is 0 everywhere, `Node.d` is filled only
;     on DECLARING nodes, and `Seg.d` / `Member.d` / `RowItem.d` stay 0 until
;     Stage 2 (typed-ast.md section 2.2).
;   - It interns no types. `Ast.types` holds exactly the error type at id 1
;     after a walk; type EXPRESSIONS are `Ty*` nodes, and turning them into
;     `AstType`s is the checker's first act (section 2.5).
;   - It fills no side table and no row table. `ast_side_alloc` and
;     `Ast.rows` are Stage 2's (sections 2.2, 2.6).
;
; ---------------------------------------------------------------------------
; ---------------------------------------------------------------------------
; WHAT THIS MODULE FOUND IN typed-ast.md. Each of these is a place the design
; did not survive contact; each is recorded at the file that hit it, and
; collected here so a reader of the design document can find them:
;
;  1. section 2.7's "Contradiction found" IS STALE. It says spec §8.6 has
;     `'forma' IDENT` with no width and that "the tree reserves nothing
;     because the grammar admits nothing". Spec §8.6 now reads `['forma'
;     IDENT [INT]]` -- amended, by that document's own asking. So the tree
;     MUST reserve a slot, and `ForHead.d` is it (ast/kinds.inc). Section 7
;     item 1 ("spec §8.6 needs a width operand on `forma`") is likewise
;     already done.
;  2. section 2.5's "interned by content through a map on their bytes" cannot
;     mean a map keyed on `Ast.types`' own bytes: `rt/map.inc` BORROWS keys
;     and `vec_push` MOVES its block. ast/types.inc copies each new key into
;     the arena; see its header.
;  3. section 2.1's second arena, "builder scratch ... reset per item", is
;     both unusable and unnecessary -- `arena_reset` invalidates the very
;     `Vec` the scratch would live in. One staging vec with stack discipline
;     replaces it; see ast/build.inc's header.
;  4. section 2.3's table groups kinds into shared rows in six places where
;     spec §8.6 is narrower than the group (`Param`/`Field` types are
;     required, `Potestas` has no generics, `rumpe`/`perge` take no
;     expression, `Unary` has one operand, `TyPath` has no row, and
;     `RowItem.d` holds either a declaration id or an ordinal, which cannot
;     be range-checked as one thing). ast/kinds.inc's role table is narrower
;     accordingly and lists all six.
;  5. section 2.4 names exactly eight declaration flags and `Decl.flags` is
;     one byte, so a ninth has nowhere to go. Recorded in ast/kinds.inc.
;  6. section 3's Stage 1 clause "every `Error` node has a diagnostic against
;     its span" is not checkable from inside this module: the emitted
;     diagnostics are the driver's list, not a field of the tree. See
;     ast/verify.inc.
;
; And four more, found by writing the walker and running it on real source:
;
;  7. section 2.3 has NO SLOT for generic arguments in expression position.
;     `Seg.b` holds them for the segments of a written `Path`, but spec §8.6's
;     `Suffix ::= GenericArgs` attaches them after ANY path segment, and a
;     segment reached through `.` is an `AST_MEMBER`. `Member.c` is that slot
;     now; see ast/kinds.inc.
;  8. section 2.3 says a STRING `Lit` holds the "decoded bytes id". Nothing in
;     this compiler decodes a string escape -- spec §8.4's literal grammar is
;     `[OPEN]` and the lexer interns the token's bytes as written -- so `a` is
;     the RAW interned id, for a string exactly as for a number. Decoding is
;     whoever closes §8.4's grammar, not this walker.
;  9. section 2.11's "an `ERROR` or `MISSING` CST node becomes an `Error`
;     node" holds for `ERROR` and is only sometimes right for `MISSING`. A
;     missing `;` is a zero-width leaf carrying nothing an `Error` operand
;     could stand in for, and putting one in `Redde.a` would replace a good
;     expression with a hole. ast/from_cst.inc's header states the real rule.
; 10. section 2.3 puts an `externus` block's ABI id in `Externus.aux`, which
;     is SIXTEEN BITS, while an ABI id is an interner id. A module that has
;     interned more than 65535 distinct strings before its `externus` block
;     cannot store it; ast/from_cst.inc traps rather than truncating. The
;     honest fix is an ABI enumeration, which needs spec §5.3 to name more
;     than one ABI first.
;
; ---------------------------------------------------------------------------
; WHAT STAGE 2's DESIGN ASKED FOR, AND WHAT THIS MODULE DID. `docs/design/
; checker.md` section 9 raised four findings against this tree; three needed
; code and are done, one needed nothing:
;
;  F1. NO TYPE KIND FOR A CAPABILITY. `m: Mundus`, `sock: rete` (spec §4.3)
;      and a `sub`'s `Decl.ty` had no `AstType` kind at all. Added:
;      `AST_TY_CAP`, `a` = the atom's declaration (ast/kinds.inc, ast/
;      types.inc), verified per entry by ast/verify.inc's `__ast_v_types`.
;  F2. NO DECLARATION KIND FOR AN ATOM. Added `AST_D_CAPATOM` and the pool
;      entry `CapAtom` -- and, the part that is a DECISION rather than an
;      enumeration value, THIS MODULE PUSHES THE ELEVEN ATOMS rather than
;      reserving the ids for the checker: `ast_cap_push`, called at the end
;      of `ast_from_cst`'s walk, so the atom of §4.6 ordinal `k` is
;      declaration `N + k` exactly as checker.md section 2.2's pass 0
;      specifies. The reasons are in ast/build.inc's header; the cost is
;      that every tree built from a CST now carries eleven declarations no
;      source text produced, which `ast_dump` shows and `ast_decl_count`
;      counts. checker.md section 2.2 should now read "pass 0 finds them"
;      (`ast_cap_base`) rather than "pushes them" -- reported, not edited
;      here, because docs/ is not this module's.
;  F3. "BOTH KEYS ARE DECLARATION INDICES" (typed-ast.md section 2.6) is
;      true now that an atom has a declaration. Nothing to do here: there is
;      NO `ast_row_intern` in this module and never was -- `Ast.rows` is
;      present and empty, and checker.md section 6 gives the interning API
;      (`chk_row_intern`) to the rows agent. The sort that section describes
;      does not exist in this tree.
;  F4. `Call.d` WAS AST_R_NONE and the verifier required 0, which would have
;      trapped the checker writing the impls a generic call chose. It is
;      AST_R_ANY (ast/kinds.inc; tests/unit/ast_call_d_impls.asm).
;
; Not done, and not this module's to do: checker.md's findings 5-22, which
; are against the spec, the probes and the conformance fixtures rather than
; against `ast/`. One correction owed to typed-ast.md section 2.3, reported
; and not edited: its `Member` row is stale -- `Member.c` holds the
; `GenericArgs` node (finding 7 above), so the row is
; `Member | E | name id | GenericArgs or 0 | resolved decl | -- |`.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.1 (Stage 1 builds it, Stage 2
; types it), §8.6, §5.1-§5.5, §4.1-§4.6, §6, §7.1.
; -----------------------------------------------------------------------------

include 'from_cst.inc'
