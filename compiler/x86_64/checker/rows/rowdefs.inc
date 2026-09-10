; compiler/x86_64/checker/rows/rowdefs.inc
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
; ROW ITEM SHAPE AND CONSTANTS. `docs/design/typed-ast.md` section 2.6's row
; item -- "(kind: capability | parameter ordinal, id)" -- made into a record,
; plus the four bounds every routine in checker/rows/ asserts against.
;
; THE HEAD OF THIS MODULE'S CHAIN. `checker/verify.inc` states the pass chain
; -- resolve <- types <- rows <- lexicon <- verify -- and this file is where
; `rows` joins it. It includes the PREVIOUS LINK and nothing else, so the graph
; stays a chain and never a diamond (fasmg has one flat namespace; a `struct`
; or `proc` processed twice by two paths fails on the second).
;
; **WHEN `checker-types` LANDS `types/types.inc`, THE LINE BELOW BECOMES
; `include '../types/types.inc'`** -- one line, here, and nothing else in this
; module moves. It points at `resolve` today only because `types` does not
; exist yet; leaving it pointing at `resolve` after `types` lands is exactly
; the diamond this note exists to prevent.
;
; THE ITEM IS TWO DWORDS, NOT (u8, u32). typed-ast.md section 2.6 writes the
; kind as a tag and ast/node.inc's `AstRow` comment says the items live in
; `Ast.extra` as "pairs of (kind, id)" -- and `Ast.extra` has a four-byte
; stride. So the stored form is two dwords per item and the INPUT form is the
; same record, which means `chk_row_intern`'s argument array and
; `chk_row_items`' return pointer have one layout between them and nothing
; converts. Only 0 and 1 are legal in the kind dword; `__chk_row_add` traps on
; anything else.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1-§4.6 (rows), §8.6 (where
; `poscit` attaches).
; -----------------------------------------------------------------------------

include '../resolve/resolve.inc'

; ---- item kinds ------------------------------------------------------------
; `docs/design/checker.md` section 6 fixes these two numbers as part of the
; shared API: "items are (kind, id) pairs, kind 0 = atom or `potestas` decl
; (flattened), 1 = ordinal". A `potestas` decl is admitted ONLY on input --
; interning flattens it to its atoms (checker.md section 2.1's table, last
; row: "flattened to its atoms at interning; no lookup at draw time"), so no
; STORED item ever names one.
CHK_ROW_ATOM	= 0	; `id` = an AST_D_CAPATOM decl, or an AST_D_POTESTAS
			; decl on input only
CHK_ROW_ORD	= 1	; `id` = a zero-based parameter ordinal (`sicut f`)

; ---- the item record -------------------------------------------------------
struct ChkRowItem
	field kind, dd		; CHK_ROW_ATOM or CHK_ROW_ORD
	field id,   dd
end struct
assert sizeof.ChkRowItem = 8

; ---- bounds ----------------------------------------------------------------
; ORDINALS ARE BOUNDED AT 64 because checker.md section 2.3 makes the
; per-function row "a pair of masks (`u16` atoms, `u64` ordinals)" and the
; fixpoint of pass 3 is that pair. A signature with 64 parameters is not a
; program this compiler needs to accept before it accepts anything; the bound
; is asserted, not silently truncated. `[OPEN]`: nothing has measured a real
; signature's parameter count, because no parser has fed one to this pass.
CHK_ROW_ORD_MAX	 = 64

; The eleven §4.6 atoms as a bit set, bit `k - 1` for §4.6 ordinal `k`
; (ast/kinds.inc's `AST_CAP_*`). Ascending bit index is ascending atom
; declaration id is ascending §4.6 order -- which is what makes the (kind, id)
; sort typed-ast.md section 2.6 asks for a BIT SCAN rather than a sort
; (checker.md finding 3, and ast/build.inc's `ast_cap_push` is what makes the
; ids consecutive: 08b4a61).
CHK_ROW_ATOM_MASK = 0x7FF
assert AST_CAP_COUNT = 11
assert CHK_ROW_ATOM_MASK + 1 = 2048

; A `potestas` may name another `potestas`; spec §8.6's `PotestasDecl` admits
; a `Path` and says nothing about what it resolves to. Flattening therefore
; recurses, and a cycle would not terminate -- so it is bounded and the bound
; traps. A cycle is a resolve-pass error (class B/L), not something this file
; can diagnose: it has no diagnostic sink.
CHK_ROW_DEPTH_MAX = 8

; ---- the id convention -----------------------------------------------------
; `CHK_ROW_NONE` is 0 and IS NEVER A ROW. Row ids are 1-based indices into
; `Ast.rows`, exactly as node, declaration and type ids are 1-based indices
; into their vecs, so 0 is free to mean "no row is recorded here" -- which is
; what `AstType.b` holds on every type that is not an `fn` or a `dyn`.
;
; THE EMPTY ROW IS A REAL ROW WITH A NON-ZERO ID, and the two are different
; facts. Spec §8.6 makes `poscit {}` an EXPLICIT empty row and spec §4.1 rule
; 6 gives it a meaning -- pure with respect to ambient state -- that "no row
; recorded" does not have. checker.md section 2.1 rule 3 then makes a bare
; `functio(f32) -> f32` the empty row too, and fn types compare by interned
; id with the row inside the key (ast/types.inc), so a bare fn type and one
; whose row slot was left 0 MUST NOT be the same type: if the empty row were
; id 0 they would be, and IR 2.9's hidden-argument count would stop being a
; function of the type. `chk_row_empty` is how a caller gets it.
;
; READING a 0 is permissive: `chk_row_atoms`, `chk_row_ords`, `chk_row_len`,
; `chk_row_items`, `chk_row_union` and `chk_row_subset` all treat row 0 as an
; empty set, because pass 3 reads `AstType.b` off types that legitimately have
; none (a struct's, a `param`'s). WRITING never produces 0: every routine here
; that returns a row id returns one >= 1.
CHK_ROW_NONE	= 0

; ---- the default pointer width ---------------------------------------------
; Pass 3 does not read a pointer width and pass 4 takes the real one as an
; argument (`chk_layout`), but the shared scratch state carries one field for
; it and asserts a sane value, so pass 3 needs something to put there. Sixty-
; four is the reference backend's (spec §9.2, §12: `x86_64-linux`), and using
; it as a DEFAULT rather than as an assumption is the point -- a pass that
; actually depends on the width takes it from its caller, who took it from
; `--hospes` (spec §9.5: there is no default-to-build-platform).
CHK_ROW_PTRBITS = 64
