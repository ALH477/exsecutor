; compiler/x86_64/diag/diag.inc
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
; The diag/ module's single aggregation point (docs/asm-conventions.md, "7.
; How to add a module", step 3: fasmg assembles one entry file that pulls in
; everything else by `include`; a file nothing includes is invisible to the
; build no matter how correct it is). `compiler/x86_64/exsc.asm` -- which
; does not exist yet -- includes THIS file, not the individual pieces.
;
; The include graph inside diag/ is a strict CHAIN, never a diamond, because
; fasmg has one flat namespace and a `struct` or `proc` processed twice via
; two different paths fails on the second:
;
;   codes.inc  (generated, pure data, includes nothing)
;     <- util.inc     (+ macros/proc.inc, macros/assert.inc)
;        <- escape.inc
;           <- fix.inc  (+ rt/span.inc, which brings the rest of rt/)
;              <- render.inc
;                 <- diag.inc  (this file)
;
; macros/proc.inc and macros/assert.inc are reached twice -- once through
; util.inc, once through rt/span.inc's chain (span -> intern -> map -> vec ->
; arena -> sys). That is SAFE and was checked by assembling it, not assumed:
; both files define only macros, and redefining a fasmg macro shadows the
; previous definition rather than erroring. The rule that matters is about
; things that emit a label or a `struc` -- no such file is reached twice
; here.
;
; What this module gives the rest of the compiler:
;   diag_init / diag_attach_source / diag_fix_ptr  -- build a diagnostic
;   diag_fix_replace / _insert / _delete / _clear  -- its §8.3 fix payload
;   diag_fix_required                              -- which codes owe one
;   diag_render_text / diag_render_json            -- render it
;   diag_emit                                      -- render it and write it
;   diag_escape / diag_escape_json                 -- THE escaping path
;   diag_is_dangerous_cp                           -- §8.1's class, for the
;                                                     lexer to reuse rather
;                                                     than re-derive
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3, §13.
; -----------------------------------------------------------------------------

include 'render.inc'
