; tests/unit/chk_decl_row_empty.asm
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
; front-end fixture -- the explicit empty row `poscit {}` on a DECLARATION
; (spec §4.1 rule 6, §8.6 "Where `poscit` attaches" and "Grammar").
;
; THE FINDING (docs/design/audit-2026-10-03-followups.md, item 6). Rule 6 says
; "any function written `poscit {}`" has a declared, empty row. The grammar's
; `DeclRow` had no brace form -- only `TypeRow`, in type position, was braced --
; so the form the rule cites was EXS-E0201. A `publica` function could state an
; empty row by writing nothing (rule 6's first half), but a PRIVATE function
; always inferred (rule 5) and could not assert purity, only have it measured.
; `DeclRow` now admits exactly one braced form, `poscit { }`, and the
; checker's seeding (`__chk_row_seed`) already read "a row node is present" as
; "declared", so the form needed no semantics of its own.
;
; What each row pins (source -> the SET of codes the WHOLE front end gives;
; `HAS` / `LACKS` mean "includes / does not include that code" and are used
; where a pre-existing, unrelated EXS-E0303 -- a function-typed parameter
; returned as a function-typed result is refused even with no row anywhere --
; would otherwise make the exact set depend on a different defect):
;
;	 1  `publica` + `poscit {}`                              clean
;	 2  private + `poscit {}`, called                        clean
;	 3  trivia between the braces                            clean
;	 4  PRIVATE `poscit {}` drawing `ambitus`                {EXS-E0421}
;	 5  ... the same body WITHOUT `poscit {}` (inferred)     clean
;	 6  PRIVATE `poscit {}` calling a `poscit alloc` fn      {EXS-E0421}
;	 7  ... the same call WITHOUT `poscit {}` (inferred)     clean
;	 8  `publica` + `poscit {}` drawing `ambitus`            {EXS-E0421}
;	 9  `publica`, no `poscit`, drawing `ambitus`            {EXS-E0421}
;	10  `publica` + `poscit alloc` drawing `ambitus`         {EXS-E0421}
;	11  `poscit { }` (a blank) drawing `ambitus`             {EXS-E0421}
;	12  bare `poscit` + a body (no braces-pair)              {E0201, E0423}
;	13  `poscit {rete}` on a declaration                     HAS EXS-E0201
;	14  `poscit {}` on a LAMBDA                              {EXS-E0201}
;	15  fn-typed result, `poscit {} poscit {}` + a draw      HAS EXS-E0421
;	16  fn-typed result, ONE `poscit {}` + a draw            LACKS EXS-E0421
;	17  an implementation head, `... in T poscit {} {`       clean
;	18  an interface member signature, `poscit {}`           clean
;
; Rows 4/6/8/15 are what makes "declared" a fact and not a parse that is then
; ignored: a private function that writes `poscit {}` is HELD to it, where rows
; 5/7 -- the same bodies, one clause shorter -- infer and pass. Row 15 against
; 16 pins the one peek that decides where a `poscit {` belongs (§8.6's "one odd
; corner"): the first goes to a function-type result, the second to the
; declaration.
;
; THE FIX PAYLOAD (rows 4, 8-11). §8.3 promises EXS-E0421 a machine-applicable
; fix. With a written row it inserts `, atom` at the row's end -- and with
; `poscit {}` that is `poscit {}, atom`, which does not parse. An empty written
; row is therefore REPLACED whole by `poscit atom`; the non-empty (row 10) and
; absent (row 9) shapes are pinned unchanged beside it.
;
; And after the table: the declared function's interned TYPE carries the empty
; row -- the very id `chk_row_empty` returns, the same one a function with no
; `poscit` has -- and one with `poscit alloc` does not.
;
; NON-VACUITY, each mutant applied in a scratch edit of one compiler file when
; this fixture was written, and the exit given is the one measured:
;   - `cst/parse.inc` reverted to the pre-change `__cst_decl_row` (no `{ }`
;     arm): exit 11 -- row 1 is EXS-E0201 instead of clean;
;   - `__cst_decl_row`'s peek reduced to the `{` alone (the `}` test removed):
;     exit 22 -- row 12, a bare `poscit` before a body, swallows the body;
;   - `__cst_at_type_row` never true (a function-type result never takes the
;     first `poscit {`): exit 26 -- row 16 gains the EXS-E0421 it must lack;
;   - pass 1's `.fn:` in `checker/resolve/resolve.inc` taking a zero-item row
;     for "no row": exit 14 -- row 4 is clean, the private function infers;
;   - pass 3's `__chk_row_seed` taking a zero-item row for "no row": exit 16 --
;     row 6 is clean (rows 4 and 8 are pass 1's, which is why there are two);
;   - `__chk_fix_e0421` without its empty-row arm: exit 14 -- row 4's fix is
;     the insertion `, ambitus`;
;   - `__chk_row_commit` giving a declared function a non-empty row: exit 31,
;     and dropping a declared function's atoms: exit 33 -- the two halves of
;     the check after the table.
; NOT killed, and said so: `chk_ty_rowid` or `chk_row_of_node` answering 0
; instead of the empty row for a zero-item node (exit 0 both). Pass 3 re-derives
; every function's final row from its declared atom masks, so the id the types
; pass first gave is overwritten; the check after the table is a statement
; about the END state, and is exercised by the commit mutants above.
;
; Exit 0 = every row passed; 10+N = row N of the table above did not match,
; with the diagnostics it did produce printed to stdout first; 10+NROWS+K =
; check K after the table.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1 rule 6, §8.6;
; docs/design/audit-2026-10-03-followups.md item 6.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

; One row, 64 bytes:
;   dq source, length
;   dd count | FX_HAS | FX_LACKS, code 0, code 1, pad
;   dd fix kind | FX_NOFIX, fix start, fix len, fix text length
;   dq fix text pointer, pad
FX_ROW = 64
FX_HAS = -1
FX_LACKS = -2
FX_NOFIX = -1

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99

	xor	r12, r12		; row index
  .row:
	cmp	r12, FX_NROWS
	jae	.rowsdone
	mov	rax, r12
	imul	rax, FX_ROW
	lea	r13, [fx_tab]
	add	r13, rax
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	call	fx_run
	mov	r14, rax
	; render whatever the row produced, so a failure shows it
	xor	ebx, ebx
  .show:
	cmp	rbx, r14
	jae	.shown
	mov	rdi, rbx
	call	fx_code
	inc	rbx
	jmp	.show
  .shown:
	mov	ecx, [r13 + 16]
	cmp	ecx, FX_HAS
	je	.has
	cmp	ecx, FX_LACKS
	je	.lacks
	cmp	r14, rcx
	jne	.bad			; not the expected NUMBER of diagnostics
	test	r14, r14
	jz	.fix
	xor	edi, edi
	mov	esi, [r13 + 20]
	call	fx_is
	test	eax, eax
	jz	.bad
	cmp	r14, 2
	jb	.fix
	mov	edi, 1
	mov	esi, [r13 + 24]
	call	fx_is
	test	eax, eax
	jz	.bad
	jmp	.fix
  .has:
	mov	esi, [r13 + 20]
	call	fx_any
	test	eax, eax
	jz	.bad
	jmp	.fix
  .lacks:
	mov	esi, [r13 + 20]
	call	fx_any
	test	eax, eax
	jnz	.bad
  .fix:
	mov	eax, [r13 + 32]
	cmp	eax, FX_NOFIX
	je	.ok
	; diagnostic 0 must carry exactly this edit
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	rbx, rax
	mov	eax, [r13 + 32]
	cmp	[rbx + Diag.fix.kind], eax
	jne	.bad
	mov	eax, [r13 + 36]
	cmp	[rbx + Diag.fix.edit.start], eax
	jne	.bad
	mov	eax, [r13 + 40]
	cmp	[rbx + Diag.fix.edit.len], eax
	jne	.bad
	mov	eax, [r13 + 44]
	cmp	[rbx + Diag.fix.text_len], rax
	jne	.bad
	mov	rsi, [rbx + Diag.fix.text_ptr]
	mov	rdi, [r13 + 48]
	mov	rcx, rax
	cld
	repe cmpsb
	jne	.bad
  .ok:
	inc	r12
	jmp	.row
  .bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group

  .rowsdone:
	; ---- after the table: the declared type's row ----------------------
	lea	rdi, [fx_rid]
	mov	rsi, FX_RID_LEN
	call	fx_run
	test	rax, rax
	jnz	.rid_bad1		; the source is clean
	call	fx_fn_rows		; -> fx_rowv[0..2] = vacua, tacita, onerata
	cmp	rax, 3
	jne	.rid_bad2
	lea	rdi, [fx_tree]
	call	chk_row_empty
	mov	rbx, rax
	cmp	[fx_rowv], ebx		; `poscit {}` is the empty row
	jne	.rid_bad3
	cmp	[fx_rowv + 4], ebx	; ... the same one no `poscit` gives
	jne	.rid_bad4
	cmp	[fx_rowv + 8], ebx	; ... and `poscit alloc` is not it
	je	.rid_bad5
	xor	edi, edi
	call	sys_exit_group
  .rid_bad1:
	mov	edi, FX_NROWS + 11
	call	sys_exit_group
  .rid_bad2:
	mov	edi, FX_NROWS + 12
	call	sys_exit_group
  .rid_bad3:
	mov	edi, FX_NROWS + 13
	call	sys_exit_group
  .rid_bad4:
	mov	edi, FX_NROWS + 14
	call	sys_exit_group
  .rid_bad5:
	mov	edi, FX_NROWS + 15
	call	sys_exit_group
  .fail99:
	mov	edi, 99
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; ---- harness ---------------------------------------------------------------
; `tests/unit/chk_directorium.asm`'s: lexer, parser, AST and the checker run
; over one source, and `fx_run` returns the TOTAL length of the diagnostic
; vector (lexer, parser, Stage 1, Stage 2) rather than `chk_run`'s own count,
; which is what lets a row assert a set that spans two passes. Plain labels: a
; `proc` argument name is an unmangled global (docs/asm-conventions.md 4.1).
; Each helper pushes an odd number of registers, so `rsp` is 16-aligned at
; every call inside it.

; fx_run(rdi = source, rsi = length) -> rax = how many diagnostics the WHOLE
; front end produced; -1 if the lexer failed outright, which no row's source
; does.
  fx_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_scratch]
	call	arena_reset
	lea	rdi, [fx_toks]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_diags]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 32
	call	vec_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_toks]
	lea	r8,  [fx_diags]
	call	lex_init
	lea	rdi, [fx_lx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	jc	.gate
	lea	rdi, [fx_green]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 512
	call	vec_init
	lea	rdi, [fx_work]
	lea	rsi, [fx_arena]
	mov	rdx, 4
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_cmap]
	lea	rsi, [fx_arena]
	mov	rdx, 1024
	call	map_init
	lea	rdi, [fx_ctree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_green]
	lea	r8,  [fx_work]
	lea	r9,  [fx_cmap]
	call	cst_tree_init
	lea	rdi, [fx_parser]
	lea	rsi, [fx_lx]
	lea	rdx, [fx_ctree]
	call	cst_parse
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chk]
	mov	rsi, 64			; --hospes x86_64-linux (spec §9.5)
	call	chk_set_target
	lea	rdi, [fx_chk]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	mov	rax, [fx_diags + Vec.len]
	pop	r13
	pop	r12
	pop	rbx
	ret
  .gate:
	mov	rax, -1
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_code(rdi = i) -- render diagnostic `i` to stdout, so a failing row prints
; what it actually got instead of only a number.
  fx_code:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [fx_diags]
	call	vec_get
	mov	rbx, rax
	mov	rdi, 1
	mov	rsi, rbx
	lea	rdx, [fx_buf]
	mov	rcx, 8192
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit
	pop	rbx
	ret

; fx_is(edi = i, esi = code) -> eax = 1 if diagnostic `i` carries that code.
  fx_is:
	push	rbx
	push	r12
	push	r13
	mov	r12d, esi
	mov	esi, edi
	lea	rdi, [fx_diags]
	call	vec_get
	cmp	[rax + Diag.code_num], r12d
	jne	.no
	mov	eax, 1
	jmp	.out
  .no:
	xor	eax, eax
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_any(esi = code) -> eax = 1 if ANY diagnostic carries that code.
  fx_any:
	push	rbx
	push	r12
	push	r13
	mov	r12d, esi
	xor	ebx, ebx
  .next:
	cmp	rbx, [fx_diags + Vec.len]
	jae	.no
	lea	rdi, [fx_diags]
	mov	rsi, rbx
	call	vec_get
	cmp	[rax + Diag.code_num], r12d
	je	.yes
	inc	rbx
	jmp	.next
  .yes:
	mov	eax, 1
	jmp	.out
  .no:
	xor	eax, eax
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_fn_rows -> rax = how many function declarations the last `fx_run` source
; has; `fx_rowv[i]` = the row id carried by function `i`'s TYPE, in source
; order. Parameters and bindings are other declaration kinds and are skipped.
  fx_fn_rows:
	push	rbx
	push	r12
	push	r13
	xor	r12, r12		; function count
	mov	ebx, 1			; declaration id (1-based)
  .decl:
	lea	rdi, [fx_tree]
	call	ast_decl_count
	cmp	rbx, rax
	ja	.done
	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_decl_at
	cmp	byte [rax + AstDecl.kind], AST_D_FN
	jne	.next
	mov	r13d, [rax + AstDecl.ty]
	test	r13d, r13d
	jz	.next
	lea	rdi, [fx_tree]
	mov	rsi, r13
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_FN
	jne	.next
	mov	ecx, [rax + AstType.b]
	cmp	r12, 8
	jae	.next
	mov	[fx_rowv + r12*4], ecx
	inc	r12
  .next:
	inc	rbx
	jmp	.decl
  .done:
	mov	rax, r12
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_setup -> eax = 0 once the arenas and the interner exist.
  fx_setup:
	push	rbx
	lea	rdi, [fx_arena]
	mov	rsi, 32 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_iarena]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_scratch]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_names]
	lea	rsi, [fx_iarena]
	mov	rdx, 1024
	call	intern_init
	xor	eax, eax
	pop	rbx
	ret
  .bad:
	mov	eax, 1
	pop	rbx
	ret

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

  fx_path	db 'chk_decl_row_empty.exsc'
  FX_PATH_LEN = $ - fx_path

  ; fix texts
  fx_t_repl	db 'poscit ambitus'
  FX_T_REPL_LEN = $ - fx_t_repl
  fx_t_ins	db ' poscit ambitus'
  FX_T_INS_LEN = $ - fx_t_ins
  fx_t_comma	db ', ambitus'
  FX_T_COMMA_LEN = $ - fx_t_comma

  ; ==== ROWS (each source measured with exsc --diagnostica json) ====
  ; s01: a `publica` function with the explicit empty row. Clean.
  fx_s01:	db 'publica functio vacua(x: f32) -> f32 poscit {} {', 10
		db 9, 'redde x;', 10
		db '}', 10
  fx_s01_LEN = $ - fx_s01
  ; s02: the same, private and called from a public one. Clean.
  fx_s02:	db 'functio vacua(x: f32) -> f32 poscit {} {', 10
		db 9, 'redde x;', 10
		db '}', 10
		db 'publica functio usa(x: f32) -> f32 {', 10
		db 9, 'redde vacua(x);', 10
		db '}', 10
  fx_s02_LEN = $ - fx_s02
  ; s03: a newline and a comment between the braces are not tokens. Clean.
  fx_s03:	db 'functio vacua(x: f32) -> f32 poscit {', 10
		db 9, '// nihil', 10
		db '} {', 10
		db 9, 'redde x;', 10
		db '}', 10
		db 'publica functio usa(x: f32) -> f32 {', 10
		db 9, 'redde vacua(x);', 10
		db '}', 10
  fx_s03_LEN = $ - fx_s03
  ; s04: the claim that makes the form worth having. A PRIVATE function that
  ; writes `poscit {}` is declared, so a draw of `ambitus` -- which pass 1 sees
  ; directly, with no provider in scope -- is EXS-E0421, and the fix replaces
  ; the whole row (18..27) with `poscit ambitus`.
  fx_s04:	db 'functio f() -> u8 poscit {} {', 10
		db 9, 'firma h = ambitus;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s04_LEN = $ - fx_s04
  ; s05: its twin, one clause shorter: no `poscit`, so the row is inferred
  ; (rule 5) and the draw is absorbed. Clean. Without s04 this proves nothing
  ; about the form, and without this s04 proves nothing about the checker.
  fx_s05:	db 'functio f() -> u8 {', 10
		db 9, 'firma h = ambitus;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s05_LEN = $ - fx_s05
  ; s06: the same ceiling, reached by a CALL: pass 3, not pass 1. `dat`
  ; declares `alloc`; `g` declares nothing and calls it.
  fx_s06:	db 'functio dat() -> u32 poscit alloc {', 10
		db 9, 'redde 1;', 10
		db '}', 10
		db 'functio g() -> u32 poscit {} {', 10
		db 9, 'redde dat();', 10
		db '}', 10
  fx_s06_LEN = $ - fx_s06
  ; s07: its twin: `g` infers `{alloc}`. Clean.
  fx_s07:	db 'functio dat() -> u32 poscit alloc {', 10
		db 9, 'redde 1;', 10
		db '}', 10
		db 'functio g() -> u32 {', 10
		db 9, 'redde dat();', 10
		db '}', 10
  fx_s07_LEN = $ - fx_s07
  ; s08: `publica` and `poscit {}`: held to it exactly as s04 is. The row
  ; starts after `publica ` (26).
  fx_s08:	db 'publica functio f() -> u8 poscit {} {', 10
		db 9, 'firma h = ambitus;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s08_LEN = $ - fx_s08
  ; s09: `publica` and NO `poscit` -- rule 6's first half, which is how a
  ; public function stated an empty row before this form existed. The same
  ; code, and the fix is the insertion of a whole `poscit` clause.
  fx_s09:	db 'publica functio f() -> u8 {', 10
		db 9, 'firma h = ambitus;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s09_LEN = $ - fx_s09
  ; s10: a NON-empty written row: the fix is still the one insertion `, atom`
  ; at the row's end. Unchanged by this change; pinned beside the new arm.
  fx_s10:	db 'publica functio f() -> u8 poscit alloc {', 10
		db 9, 'firma h = ambitus;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s10_LEN = $ - fx_s10
  ; s11: `poscit { }` -- the replaced span is the row's, whatever is between
  ; the braces (18..28).
  fx_s11:	db 'functio f() -> u8 poscit { } {', 10
		db 9, 'firma h = ambitus;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s11_LEN = $ - fx_s11
  ; s12: a bare `poscit` before a function BODY. The peek is `{ }`, both
  ; tokens, so this is unchanged: EXS-E0201 at the `{` (the missing row item),
  ; the body parsed as a body, and the checker's EXS-E0423 for the item that is
  ; not there. Had the peek been `{` alone, the body would have been swallowed
  ; as a row.
  fx_s12:	db 'functio f() -> u8 poscit {', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s12_LEN = $ - fx_s12
  ; s13: the braced form on a declaration admits the EMPTY row only; with an
  ; item it is still a parse error, at the `{`.
  fx_s13:	db 'publica functio f(x: u32) -> u32 poscit {rete} {', 10
		db 9, 'redde x;', 10
		db '}', 10
  fx_s13_LEN = $ - fx_s13
  ; s14: §8.6 decision 2 -- a lambda takes no `poscit` -- and `poscit {}` is no
  ; exception: one EXS-E0201, at the `poscit`, and nothing after it.
  fx_s14:	db 'publica functio f() -> u32 {', 10
		db 9, 'firma g = functio(a: u32) -> u32 poscit {} { redde a; };', 10
		db 9, 'redde g(1);', 10
		db '}', 10
  fx_s14_LEN = $ - fx_s14
  ; s15: a function-TYPE result takes the first `poscit {` (§8.6's peek) and
  ; the declaration takes the second. `mk` is therefore DECLARED empty, and the
  ; draw is EXS-E0421. (The EXS-E0303 that comes with it is the unrelated
  ; defect in this fixture's header.)
  fx_s15:	db 'functio mk(g: functio(f32) -> f32) -> functio(f32) -> f32 poscit {} poscit {} {', 10
		db 9, 'firma h = ambitus;', 10
		db 9, 'redde g;', 10
		db '}', 10
  fx_s15_LEN = $ - fx_s15
  ; s16: with ONE `poscit {}` it belongs to the result type and `mk` has no
  ; row of its own -- private, so inferred, and no EXS-E0421.
  fx_s16:	db 'functio mk(g: functio(f32) -> f32) -> functio(f32) -> f32 poscit {} {', 10
		db 9, 'firma h = ambitus;', 10
		db 9, 'redde g;', 10
		db '}', 10
  fx_s16_LEN = $ - fx_s16
  ; s17: `DeclRow` is shared with the implementation head; the empty row there
  ; is the same row an absent one is (an implementation's mark is its head row
  ; union its target's bearing atoms). Clean.
  fx_s17:	db 'interfacies Summabilis {', 10
		db 9, 'functio combina(self: Summabilis, other: f32) -> f32 poscit alloc', 10
		db '}', 10
		db 'structura Sinister { n: mensura }', 10
		db 'interfacies Summabilis in Sinister poscit {} {', 10
		db 9, 'functio combina(self: Sinister, other: f32) -> f32 {', 10
		db 9, 9, 'redde other;', 10
		db 9, '}', 10
		db '}', 10
  fx_s17_LEN = $ - fx_s17
  ; s18: ... and on a member signature that has no body. Clean.
  fx_s18:	db 'interfacies Summabilis {', 10
		db 9, 'functio combina(self: Summabilis, other: f32) -> f32 poscit {}', 10
		db '}', 10
		db 'structura Sinister { n: mensura }', 10
		db 'interfacies Summabilis in Sinister {', 10
		db 9, 'functio combina(self: Sinister, other: f32) -> f32 {', 10
		db 9, 9, 'redde other;', 10
		db 9, '}', 10
		db '}', 10
  fx_s18_LEN = $ - fx_s18

  ; after the table: three functions, three rows -- the declared type's row.
  fx_rid:	db 'publica functio vacua(x: f32) -> f32 poscit {} {', 10
		db 9, 'redde x;', 10
		db '}', 10
		db 'publica functio tacita(x: f32) -> f32 {', 10
		db 9, 'redde x;', 10
		db '}', 10
		db 'publica functio onerata(x: f32) -> f32 poscit alloc {', 10
		db 9, 'redde x;', 10
		db '}', 10
  FX_RID_LEN = $ - fx_rid

  fx_tab:
	; 1-3: parses, clean
	dq fx_s01, fx_s01_LEN
	dd 0, 0, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	dq fx_s02, fx_s02_LEN
	dd 0, 0, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	dq fx_s03, fx_s03_LEN
	dd 0, 0, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	; 4-7: DECLARED, and the twins that are not
	dq fx_s04, fx_s04_LEN
	dd 1, 421, 0, 0
	dd DIAG_FIX_REPLACE, 18, 9, FX_T_REPL_LEN
	dq fx_t_repl, 0
	dq fx_s05, fx_s05_LEN
	dd 0, 0, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	dq fx_s06, fx_s06_LEN
	dd 1, 421, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	dq fx_s07, fx_s07_LEN
	dd 0, 0, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	; 8-11: the fix payload, one shape each
	dq fx_s08, fx_s08_LEN
	dd 1, 421, 0, 0
	dd DIAG_FIX_REPLACE, 26, 9, FX_T_REPL_LEN
	dq fx_t_repl, 0
	dq fx_s09, fx_s09_LEN
	dd 1, 421, 0, 0
	dd DIAG_FIX_INSERT, 25, 0, FX_T_INS_LEN
	dq fx_t_ins, 0
	dq fx_s10, fx_s10_LEN
	dd 1, 421, 0, 0
	dd DIAG_FIX_INSERT, 38, 0, FX_T_COMMA_LEN
	dq fx_t_comma, 0
	dq fx_s11, fx_s11_LEN
	dd 1, 421, 0, 0
	dd DIAG_FIX_REPLACE, 18, 10, FX_T_REPL_LEN
	dq fx_t_repl, 0
	; 12-14: what must stay an error
	dq fx_s12, fx_s12_LEN
	dd 2, 201, 423, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	dq fx_s13, fx_s13_LEN
	dd FX_HAS, 201, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	dq fx_s14, fx_s14_LEN
	dd 1, 201, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	; 15-16: which `poscit {` belongs to the type
	dq fx_s15, fx_s15_LEN
	dd FX_HAS, 421, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	dq fx_s16, fx_s16_LEN
	dd FX_LACKS, 421, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	; 17-18: the other two places a DeclRow is parsed
	dq fx_s17, fx_s17_LEN
	dd 0, 0, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
	dq fx_s18, fx_s18_LEN
	dd 0, 0, 0, 0
	dd FX_NOFIX, 0, 0, 0
	dq 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW
  assert ($ - fx_tab) mod FX_ROW = 0
  assert FX_NROWS = 18

segment readable writeable
  fx_arena:	rb sizeof.Arena
  fx_iarena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_toks:	rb sizeof.Vec
  fx_diags:	rb sizeof.Vec
  fx_lx:	rb sizeof.Lexer
  fx_green:	rb sizeof.Vec
  fx_work:	rb sizeof.Vec
  fx_cmap:	rb sizeof.Map
  fx_ctree:	rb sizeof.CstTree
  fx_parser:	rb sizeof.CstParser
  fx_tree:	rb sizeof.Ast
  fx_chk:	rb sizeof.ChkCtx
  fx_buf:	rb 8192
  fx_rowv:	rd 8
