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
; NOT WRITTEN THIS PASS, deliberately: `ast_from_cst`.
;
; The CST -> AST walker (typed-ast.md section 2.11) is absent, and its absence
; is a decision, not an omission. compiler/x86_64/cst/ is being written
; concurrently; building a walker against a parser whose API is still moving
; would be building against a moving target, which is what typed-ast.md was
; written to prevent. Everything the walker will need exists and is tested:
; the records, the constructors, the type table, the verifier, and a textual
; form that can build and check a tree with no parser in the repository at
; all. When cst/ settles, `ast_from_cst(tree, green_root, file_id)` is a
; postorder walk of red nodes calling `ast_node` / `ast_decl` /
; `ast_list_*` -- one `AstNode` per meaning-bearing nonterminal, with
; `Span{file_id, red.text_offset, green.text_len}` minus trivia, and an
; `AST_ERROR` node with its span for every `ERROR` or `MISSING` CST node so
; that a malformed file still yields a tree.
;
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
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.1 (Stage 1 builds it, Stage 2
; types it), §8.6, §5.1-§5.5, §4.1-§4.6, §6, §7.1.
; -----------------------------------------------------------------------------

include 'load.inc'
