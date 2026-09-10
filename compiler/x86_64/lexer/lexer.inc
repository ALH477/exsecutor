; compiler/x86_64/lexer/lexer.inc
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
; The lexer/ module's single aggregation point (docs/asm-conventions.md, "7.
; How to add a module", step 3: fasmg assembles one entry file that pulls in
; everything else by `include`; a file nothing includes is invisible to the
; build no matter how correct it is). `compiler/x86_64/exsc.asm` -- which does
; not exist yet -- includes THIS file, not the individual pieces.
;
; The include graph inside lexer/ is a strict CHAIN, never a diamond, for the
; reason rt/vec.inc's header records empirically: fasmg has one flat namespace
; and a file that emits a label, a `struct` or a `proc` fails outright the
; second time it is processed.
;
;   keywords.inc  (GENERATED from §8.4, pure data, includes nothing)
;     <- token.inc    (+ diag/diag.inc, which brings all of diag/ and all of
;                       rt/; + the three compiler/shared/unicode/ modules)
;        <- source.inc                       §8.1  the source-policy gate
;           <- ident.inc                     §8.2  the UTS #39 verdict
;              <- lex.inc                    §8.4  the tokenizer, lex_run
;                 <- lexer.inc  (this file)
;
; macros/proc.inc and macros/assert.inc are reached many times over -- through
; diag/, through rt/, and once from each unicode module. That is SAFE and is
; checked by assembling it, not assumed: both define only macros, and fasmg
; redefines a macro rather than erroring.
;
; ONE THING THE CONSUMER MUST DO ITSELF. `compiler/shared/unicode/tables/
; tables.inc` is NOT included from here. It defines labels and emits ~120 KB
; of `file` data, so processing it twice is fatal, and only the consumer knows
; which segment the read-only blobs belong in. Include it exactly once, in a
; data segment of your choosing (compiler/shared/unicode/README.md, "How to
; include them"):
;
;	include '<...>/compiler/x86_64/lexer/lexer.inc'
;	...
;	segment readable
;	  include '<...>/compiler/shared/unicode/tables/tables.inc'
;
; WHAT THIS MODULE GIVES THE REST OF THE COMPILER:
;   lex_init / lex_set_source        -- prepare a `Lexer` over one file
;   lex_run                          -- §8.1 gate, then §8.4 tokens; the one
;                                       routine here that speaks CF/eax
;   lex_source_check / lex_tokenize  -- the two phases, separately, for tests
;   lex_diag                         -- record a diagnostic against this file
;   lex_kw_as_ident                  -- THE `EXS-E0220`, with its §8.3 fix,
;                                       for the eventual parser to call from
;                                       every grammar position this module
;                                       cannot see
;   lex_script_ok                    -- §8.2's UTS #39 verdict, as a predicate
;   kw_lookup / kw_text_of           -- §8.4's reserved set, from keywords.inc
;   Tok / TOK_* / PUN_* / Lexer      -- the token record and its vocabulary
;
; WHAT IT DOES NOT GIVE, deliberately: a parser. §15 #9 records that there is
; no phrase grammar and the CST is deferred. This module produces a token
; stream and stops.
;
; NO SYSCALLS. Nothing under lexer/ reaches rt/sys.inc: the lexer is pure
; computation over a caller-supplied byte buffer, and reading the file is the
; driver's job. tools/syscall-audit.sh sees nothing from this module at all.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1, §8.2, §8.3, §8.4.
; -----------------------------------------------------------------------------

include 'lex.inc'
