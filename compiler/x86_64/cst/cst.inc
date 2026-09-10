; compiler/x86_64/cst/cst.inc
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
; The cst/ module's single aggregation point (docs/asm-conventions.md, "7. How
; to add a module", step 3). A consumer includes THIS file and not the pieces.
;
; IT INCLUDES lexer/lexer.inc ITSELF, and that has a consequence for whoever
; includes it: `compiler/x86_64/exsc.asm` must include `cst/cst.inc` INSTEAD OF
; `lexer/lexer.inc`, not as well as. fasmg has one flat namespace and a file
; that emits a label, a `struct` or a `proc` fails outright the second time it
; is processed, so the include graph is a strict CHAIN and never a diamond
; (rt/vec.inc's header records the empirical finding; every module in this tree
; is arranged the same way).
;
;   keywords.inc <- token.inc <- source.inc <- ident.inc <- lex.inc
;     <- lexer.inc
;        <- kinds.inc  (pure constants and one name table)
;           <- green.inc     §9.1  the interned, position-free half
;              <- red.inc    §9.1  positions on demand, and the round trip
;                 <- parse.inc §8.6 the grammar
;                    <- cst.inc  (this file)
;
; `compiler/shared/unicode/tables/tables.inc` is NOT included from here, for
; the same reason lexer/lexer.inc does not include it: it emits ~120 KB of
; `file` data and only the consumer knows which segment that belongs in.
; Include it exactly once, in a data segment of your choosing.
;
; WHAT THIS MODULE GIVES THE REST OF THE COMPILER:
;   cst_parse                        -- §8.6's grammar over a lexed `Lexer`;
;                                       the one routine here that speaks CF/eax
;   cst_root                         -- the parsed root's green id
;   cst_tree_init / cst_finish       -- the green tree's storage and its root
;   cst_open / cst_close / cst_cancel / cst_emit_token
;                                    -- the builder, for anything that needs to
;                                       construct a tree that did not come from
;                                       a parse (a formatter's rewrite, §12)
;   cst_green_ptr / cst_green_kid    -- the green tree, by id
;   cst_red_root / cst_red_child / cst_red_kind / cst_red_len / cst_red_nkid
;                                    -- positions, computed on demand
;   cst_red_span                     -- a node's §8.3 `Span`, via `span_make`
;   cst_text                         -- the round trip: the subtree's bytes
;   cst_dump / cst_kind_name         -- a tree dump, for fixtures
;   CstTree / CstGreen / CstRed / CstParser / CST_*  -- the vocabulary
;
; WHAT IT DOES NOT GIVE: a typed AST. §9.1's next stage reads this tree; it is
; not built here, and compiler/x86_64/ast/ is empty.
;
; NO SYSCALLS. Nothing under cst/ reaches rt/sys.inc -- like lexer/, this is
; pure computation over a caller-supplied buffer and caller-supplied arenas.
; tools/syscall-audit.sh sees nothing from this module at all.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.6, §9.1, §8.3, §13.
; -----------------------------------------------------------------------------

include '../lexer/lexer.inc'
include 'parse.inc'
