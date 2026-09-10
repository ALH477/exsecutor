; compiler/x86_64/checker/rows/rows.inc
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
; The checker/rows/ module's single aggregation point (docs/asm-conventions.md,
; "7. How to add a module", step 3). `checker/checker.inc` includes THIS file,
; not the pieces:
;
;	include 'rows/rows.inc'
;
; from `checker/verify.inc`, whose header states the pass chain and where this
; module joins it. `rowdefs.inc` includes the PREVIOUS LINK (today
; `../resolve/resolve.inc`; `../types/types.inc` the moment that file exists --
; see its header) and this file includes the LAST link of the sub-chain, so
; the graph is a chain and never a diamond: fasmg has one flat namespace, and
; a `struct` or `proc` processed twice by two include paths fails outright.
;
; The chain inside checker/rows/:
;
;   rowdefs.inc  (the item record, the four bounds, the id convention;
;                 includes the previous pass)
;     <- intern.inc   (`Ast.rows`, the shared `chk_row_intern` API)
;        <- compute.inc  (pass 3: draws, the row fixpoint, the ceiling)
;           <- layout.inc   (pass 4: `Ast.layout`)
;              <- rows.inc  (this file)
;
; THE TWO ENTRY POINTS `checker/checker.inc`'s `chk_run` calls:
;
;	mov	rdi, rbx
;	call	chk_rows		; pass 3
;	mov	rdi, rbx
;	mov	rsi, <pointer width in bits, from --hospes>
;	call	chk_layout		; pass 4
;
; `chk_layout` takes ONE argument more than the sketch in `chk_run`, and the
; reason is in layout.inc's header: spec §9.5 forbids defaulting `--hospes`,
; `DrvCtx.hospes` is a triple STRING, and nothing in this tree turns a triple
; into a pointer width yet. A `ChkCtx` field would be the better home; asked
; for, not taken.
;
; THIS MODULE OWNS PASSES 3 AND 4 of docs/design/checker.md section 2.2 --
; both fill per-declaration tables from `Decl.ty`, which is why that document
; gives them to one agent -- plus `chk_row_intern`, which section 6 names as
; the one API two checker agents share and which is therefore delivered and
; frozen ahead of everything else here.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1-§4.6, §5.2, §8.6.
; -----------------------------------------------------------------------------

include 'layout.inc'
