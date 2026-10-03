; tests/unit/chk_row_sicut_forward.asm
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 DeMoD LLC.
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
; checker fixture -- spec §4.2's recorded defect, fixed: a function declared
; `poscit sicut s` that forwards `s` to another `poscit sicut s` function was
; refused EXS-E0421 (docs/design/wire-codec.md finding 9;
; checker/rows/compute.inc `__chk_row_sicutcov` is the fix and its header the
; rule). A `Directorium` (ADR 0017) is forwarded exactly this way, so the
; design could not be used past one call level until this held.
;
; BOTH DIRECTIONS, because §4.2 records that an earlier attempt at this change
; double-counted a `sicut`-declared row and accepted the closure-capture
; violation. Rows 1, 2, 4 and 5 are ACCEPTED shapes the fix must admit; rows
; 3, 6, 7, 8, 9 and 10 are violations it must keep refusing:
;
;	1  finding 9's repro, forwarding one level             clean
;	2  forwarding two levels                               clean
;	3  bad_closure_capture's `exterior`                    {EXS-E0421}
;	4  ok_closure_declared                                 clean
;	5  a closure forwarded under the caller's own `sicut`  clean
;	6  `sicut f` but a DIFFERENT named function passed     {EXS-E0421}
;	7  the callee DECLARES the atom (a carrier)            {EXS-E0421}
;	8  the callee declares the atom AND `sicut`, forwarded {EXS-E0421}
;	9  a struct whose mark the caller's `sicut` lacks      {EXS-E0421}
;	10 a row in a parameter's TYPE is not a provider       {EXS-E0421}
;	11 a lambda over `archivum`, called via a local        {EXS-E0421}
;	12 the same lambda, called where it is written         {EXS-E0421}
;	13 forwarded inline to a `sicut f` callee              {EXS-E0421}
;	14 forwarded through a local to a `sicut f` callee     {EXS-E0421}
;	15 called through an alias of the local                {EXS-E0421}
;	16 the same over `ambitus`, from a `poscit alloc` fn   {EXS-E0421}
;	17 a lambda over a REAL provider (`sub archivum`)      clean
;	18 a lambda that draws nothing, called and forwarded   clean
;	19 an ineligible argument's atom is not covered        {EXS-E0421}
;
; Row 10 is resolve.inc's `__chk_rowitem` defect, found while building this:
; every row item bound its atom in the function's root frame, including one
; written inside a parameter's function type. It is here because row 4 could
; not be measured without it (EXS-E0422 there) and because ADR 0017's R1 --
; the raw `archivum` atom cannot be obtained from a `Directorium` -- was false
; while it stood. (R1 was later found false twice more: `sub`'s untyped
; provider, pinned in chk_directorium.asm rows 14-16, and a lambda's invisible
; draw, rows 11-18 below. This header used to say a function holding only a
; `Directorium` "has no expression of type `archivum`", which is a
; CONSEQUENCE of those three rules and not a thing any one of them gives.)
;
; NON-VACUITY, run when this fixture was written (each mutant in a scratch
; copy of the tree, each failing exactly where predicted):
;   - `__chk_row_sicutcov` answering 0 (the defect restored): exit 11 (row 1
;     refused EXS-E0421);
;   - `__chk_row_eligible` answering 1 for every argument (no eligibility
;     rule): exit 16 (row 6 accepted);
;   - not excluding the callee's DECLARED atoms: exit 18 (row 8 accepted;
;     row 7 alone could not catch it -- its callee has no ordinal, so nothing
;     is substituted and the exclusion is never reached);
;   - `__chk_rowitem` binding type-position items again: exit 14 (row 4,
;     EXS-E0422) -- and row 10 accepted.
;
;
; Rows 11-18 (ADR 0017, blocker 2) are about a LAMBDA's row, which is inferred
; and which a call or a `sicut` argument used to read out of its TYPE (`{}`).
; They failed on the compiler before `__chk_row_lamof` -- row 11 first, exit
; 21 -- and each half is held by its own mutant, run when the rows were
; written:
;   - `__chk_row_callee` not consulting `__chk_row_lamof` (a lambda called
;     through a local, or where it is written, reads its type again): exit 21
;     (row 11 accepted);
;   - `__chk_row_argrow` not consulting it (a lambda forwarded as an
;     ARGUMENT reads its type again): exit 23 (row 13 accepted).
; Rows 17 and 18 are the twins that keep the fix from being "refuse every
; lambda": a lambda over a real provider, and one that draws nothing.
;
; Exit 0 = every row passed; 10+N = row N of the table above did not match,
; with the diagnostics it did produce printed to stdout first.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1, §4.2 (substitution, and the
; defect paragraph this fixture retires), §4.3; docs/design/checker.md 2.1,
; 2.3; docs/design/archivum-beneath.md D6.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

; One row: dq source, length; dd expected count, code 0, code 1, pad.
FX_ROW = 32

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99

	xor	r12, r12		; row index
  .row:
	cmp	r12, FX_NROWS
	jae	.done
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
	cmp	r14, rcx
	jne	.bad			; not the expected NUMBER of diagnostics
	test	r14, r14
	jz	.ok
	xor	edi, edi
	mov	esi, [r13 + 20]
	call	fx_is
	test	eax, eax
	jz	.bad
	cmp	r14, 2
	jb	.ok
	mov	edi, 1
	mov	esi, [r13 + 24]
	call	fx_is
	test	eax, eax
	jz	.bad
  .ok:
	inc	r12
	jmp	.row
  .bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .done:
	xor	edi, edi
	call	sys_exit_group
  .fail99:
	mov	edi, 99
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; ---- harness ---------------------------------------------------------------
; `tests/unit/chk_row_layout_src.asm`'s, with ONE change, and the change is the
; point: `fx_run` does NOT bail out when the lexer or the parser has said
; something, and it returns the TOTAL length of the diagnostic vector rather
; than `chk_run`'s own count. That is what lets a row assert a SET spanning two
; passes -- "exactly {EXS-E0201}, and nothing from pass 4" is the claim spec
; §5.2 rule 3 makes, and a harness that stopped at the parser could not make
; it. Plain labels: a `proc` argument name is an unmangled global
; (docs/asm-conventions.md 4.1). Each helper pushes an odd number of
; registers, so `rsp` is 16-aligned at every call inside it.

; fx_run(rdi = source, rsi = length) -> rax = how many diagnostics the WHOLE
; front end produced (lexer, parser, Stage 1, Stage 2); -1 if the lexer failed
; outright, which no row's source does.
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

  fx_path	db 'chk_row_sicut_forward.exsc'
  FX_PATH_LEN = $ - fx_path

  ; ==== ROWS (each source measured with exsc --diagnostica json) ====
  ; s01: the minimal repro of docs/design/wire-codec.md finding 9, whole program:
  ; `duo` declares `sicut s` and forwards `s` to `unum`, which declares the
  ; same. Both rows are `Scriptor`'s mark; clean. Before the fix: {EXS-E0421}
  ; at `unum(s, b)`.
  fx_s01:	db 'functio unum(s: Scriptor, b: u8) -> mensura poscit sicut s {', 10
		db 9, 'redde s.scribe_octeto(b);', 10
		db '}', 10
		db 'functio duo(s: Scriptor, b: u8) -> mensura poscit sicut s {', 10
		db 9, 'redde unum(s, b);', 10
		db '}', 10
		db 'publica functio initium(m: Mundus) -> u8 {', 10
		db 9, 'firma a = m.ambitus();', 10
		db 9, 'sub ambitus = a;', 10
		db 9, 'firma s = Scriptor.ad_exitum(a);', 10
		db 9, 'duo(s, 65);', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s01_LEN = $ - fx_s01
  ; s02: the same forwarding through two levels (`tres` -> `duo` -> `unum`): clean.
  ; Before the fix: {EXS-E0421, EXS-E0421}.
  fx_s02:	db 'publica functio unum(s: Scriptor, b: u8) -> mensura poscit sicut s {', 10
		db 9, 'redde s.scribe_octeto(b);', 10
		db '}', 10
		db 'publica functio duo(s: Scriptor, b: u8) -> mensura poscit sicut s {', 10
		db 9, 'redde unum(s, b);', 10
		db '}', 10
		db 'publica functio tres(s: Scriptor, b: u8) -> mensura poscit sicut s {', 10
		db 9, 'redde duo(s, b);', 10
		db '}', 10
  fx_s02_LEN = $ - fx_s02
  ; s03: §4.2's closure-capture violation, the part the compiler can state
  ; (`prototypes/capcheck/cases/bad_closure_capture.exsc`'s `consumidor` and
  ; `exterior`): `exterior` declares only `alloc` and forwards a closure whose
  ; TYPE demands `rete`. Exactly one EXS-E0421, at `exterior`'s call. Before
  ; the fix there were TWO -- `consumidor`'s own `f(x)` was refused as well.
  fx_s03:	db 'publica functio consumidor(x: f32, f: functio(f32) -> f32 poscit {rete}) -> f32 poscit sicut f {', 10
		db 9, 'redde f(x);', 10
		db '}', 10
		db 'publica functio exterior(v: f32, cerrado: functio(f32) -> f32 poscit {rete}) -> f32 poscit alloc {', 10
		db 9, 'redde consumidor(v, cerrado);', 10
		db '}', 10
  fx_s03_LEN = $ - fx_s03
  ; s04: its correct twin, `ok_closure_declared.exsc`: `exterior` declares
  ; `alloc, rete`. Clean. Before the fix: {EXS-E0422, EXS-E0421} -- the
  ; `{rete}` in the PARAMETER's type had bound `rete` in the root frame
  ; (resolve.inc's `__chk_rowitem`), and `consumidor`'s `f(x)` was refused.
  fx_s04:	db 'publica functio consumidor(x: f32, f: functio(f32) -> f32 poscit {rete}) -> f32 poscit sicut f {', 10
		db 9, 'redde f(x);', 10
		db '}', 10
		db 'publica functio exterior(v: f32, cerrado: functio(f32) -> f32 poscit {rete}) -> f32 poscit alloc, rete {', 10
		db 9, 'redde consumidor(v, cerrado);', 10
		db '}', 10
  fx_s04_LEN = $ - fx_s04
  ; s05: the closure FORWARDED under `sicut cerrado` instead of declared: the
  ; caller's row is the closure's row, so the draw is covered. Clean.
  fx_s05:	db 'publica functio consumidor(x: f32, f: functio(f32) -> f32 poscit {rete}) -> f32 poscit sicut f {', 10
		db 9, 'redde f(x);', 10
		db '}', 10
		db 'publica functio exterior(v: f32, cerrado: functio(f32) -> f32 poscit {rete}) -> f32 poscit alloc, sicut cerrado {', 10
		db 9, 'redde consumidor(v, cerrado);', 10
		db '}', 10
  fx_s05_LEN = $ - fx_s05
  ; s06: a caller that writes `sicut cerrado` but passes a DIFFERENT, named
  ; function `nocens` (`poscit rete`): building that closure needs a `rete`
  ; carrier the caller does not hold, and `sicut` provides none. EXS-E0421.
  fx_s06:	db 'publica functio consumidor(x: f32, f: functio(f32) -> f32 poscit {rete}) -> f32 poscit sicut f {', 10
		db 9, 'redde f(x);', 10
		db '}', 10
		db 'publica functio nocens(x: f32) -> f32 poscit rete {', 10
		db 9, 'redde x;', 10
		db '}', 10
		db 'publica functio exterior(v: f32, cerrado: functio(f32) -> f32 poscit {rete}) -> f32 poscit sicut cerrado {', 10
		db 9, 'redde consumidor(v, nocens);', 10
		db '}', 10
  fx_s06_LEN = $ - fx_s06
  ; s07: the callee DECLARES the atom (`g ... poscit ambitus`): a hidden
  ; carrier the caller must pass, and `sicut s` binds none (checker.md 2.1).
  ; EXS-E0421 -- `sicut s` covers what travels IN `s`, not the atom itself.
  fx_s07:	db 'publica functio g(b: u8) -> u8 poscit ambitus {', 10
		db 9, 'redde b;', 10
		db '}', 10
		db 'publica functio f(s: Scriptor, b: u8) -> u8 poscit sicut s {', 10
		db 9, 'redde g(b);', 10
		db '}', 10
  fx_s07_LEN = $ - fx_s07
  ; s08: the callee declares the atom AND a `sicut` the caller forwards to:
  ; `g ... poscit ambitus, sicut t` called as `g(s, b)`. The substituted
  ; `{ambitus}` is covered by the caller's `sicut s`; the DECLARED `ambitus`
  ; is a carrier and is not. EXS-E0421 -- the case that tells "cover what
  ; was substituted" from "cover the whole contribution".
  fx_s08:	db 'publica functio g(t: Scriptor, b: u8) -> u8 poscit ambitus, sicut t {', 10
		db 9, 'redde b;', 10
		db '}', 10
		db 'publica functio f(s: Scriptor, b: u8) -> u8 poscit sicut s {', 10
		db 9, 'redde g(s, b);', 10
		db '}', 10
  fx_s08_LEN = $ - fx_s08
  ; s09: a capability-bearing struct whose mark (`{rete}`) is NOT what the
  ; caller's `sicut s` denotes (`{ambitus}`): EXS-E0421.
  fx_s09:	db 'structura R { r: rete }', 10
		db 'publica functio h(x: R) -> u8 poscit sicut x {', 10
		db 9, 'redde 0;', 10
		db '}', 10
		db 'publica functio f(s: Scriptor, x: R) -> u8 poscit sicut s {', 10
		db 9, 'redde h(x);', 10
		db '}', 10
  fx_s09_LEN = $ - fx_s09
  ; s10: a row written in a PARAMETER'S TYPE is not a provider:
  ; `g: functio(u8) -> u8 poscit {ambitus}` does not let the body name
  ; `ambitus`. EXS-E0421 at `ambitus`. Before the fix: clean -- the type's
  ; row item bound the atom in the root frame (resolve.inc `__chk_rowitem`).
  fx_s10:	db 'publica functio f(s: Scriptor, g: functio(u8) -> u8 poscit {ambitus}) -> u8 poscit sicut s {', 10
		db 9, 'firma l = Lector.ab_introitu(ambitus);', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s10_LEN = $ - fx_s10

  ; ---- ADR 0017, blocker 2: a lambda's draw is not laundered by being called
  ; or forwarded. A lambda's row is inferred (spec §8.6 decision 2), and the
  ; contribution of a call, or the substitution at a `sicut` argument, used to
  ; read the row out of the callee's / argument's TYPE -- `{}`, interned before
  ; any row existed. `__chk_row_lamof` reads the live row instead.
  ;
  ; s11: a lambda over `archivum`, held in a local and called, inside `sicut d`.
  ; The draw reaches `salva` through the callee's OWN row, which no `sicut`
  ; item covers. EXS-E0421. (the reviewer's `p21`)
  fx_s11:	db 'publica functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'firma k = functio(v: textus) -> u8 {', 10
		db 9, 9, 'discerne Directorium.ad_radicem(archivum, v) {', 10
		db 9, 9, 9, 'casus prosperum(w) { redde 0; }', 10
		db 9, 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, 9, '}', 10
		db 9, '};', 10
		db 9, 'redde k("/");', 10
		db '}', 10
  fx_s11_LEN = $ - fx_s11
  ; s12: the same lambda called where it is written. (`q21e`'s shape.)
  fx_s12:	db 'publica functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'redde (functio() -> u8 {', 10
		db 9, 9, 'discerne Directorium.ad_radicem(archivum, "/") {', 10
		db 9, 9, 9, 'casus prosperum(w) { redde 0; }', 10
		db 9, 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, 9, '}', 10
		db 9, '})();', 10
		db '}', 10
  fx_s12_LEN = $ - fx_s12
  ; s13: forwarded, as an inline lambda, to a `sicut f` callee. The argument is
  ; a lambda, so it is INELIGIBLE and `salva`'s `sicut d` cannot cover what it
  ; brings in. (`q07c`'s shape.)
  fx_s13:	db 'publica functio consumidor(x: u8, f: functio(u8) -> u8 poscit {archivum}) -> u8 poscit sicut f {', 10
		db 9, 'redde f(x);', 10
		db '}', 10
		db 'publica functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'redde consumidor(0, functio(y: u8) -> u8 {', 10
		db 9, 9, 'discerne Directorium.ad_radicem(archivum, "/") {', 10
		db 9, 9, 9, 'casus prosperum(w) { redde 0; }', 10
		db 9, 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, 9, '}', 10
		db 9, '});', 10
		db '}', 10
  fx_s13_LEN = $ - fx_s13
  ; s14: forwarded to the same `sicut f` callee THROUGH A LOCAL. (`q07d`.)
  fx_s14:	db 'publica functio consumidor(x: u8, f: functio(u8) -> u8 poscit {archivum}) -> u8 poscit sicut f {', 10
		db 9, 'redde f(x);', 10
		db '}', 10
		db 'publica functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'firma k = functio(y: u8) -> u8 {', 10
		db 9, 9, 'discerne Directorium.ad_radicem(archivum, "/") {', 10
		db 9, 9, 9, 'casus prosperum(w) { redde 0; }', 10
		db 9, 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, 9, '}', 10
		db 9, '};', 10
		db 9, 'redde consumidor(0, k);', 10
		db '}', 10
  fx_s14_LEN = $ - fx_s14
  ; s15: through an alias of the local.
  fx_s15:	db 'publica functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'firma k = functio() -> u8 {', 10
		db 9, 9, 'discerne Directorium.ad_radicem(archivum, "/") {', 10
		db 9, 9, 9, 'casus prosperum(w) { redde 0; }', 10
		db 9, 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, 9, '}', 10
		db 9, '};', 10
		db 9, 'firma j = k;', 10
		db 9, 'redde j();', 10
		db '}', 10
  fx_s15_LEN = $ - fx_s15
  ; s16: the same shape over `ambitus`, from a function declaring only `alloc`.
  ; This one never reached lowering as a closure trap -- it was ACCEPTED, which
  ; is the checker half of the same hole. EXS-E0421 at the forwarding call.
  fx_s16:	db 'publica functio consumidor(x: u8, f: functio(u8) -> u8 poscit {ambitus}) -> u8 poscit sicut f {', 10
		db 9, 'redde f(x);', 10
		db '}', 10
		db 'publica functio exterior(v: u8) -> u8 poscit alloc {', 10
		db 9, 'firma k = functio(y: u8) -> u8 { firma l = Lector.ab_introitu(ambitus); redde y; };', 10
		db 9, 'redde consumidor(v, k);', 10
		db '}', 10
  fx_s16_LEN = $ - fx_s16
  ; s17: the legal twin of s11: a lambda over `archivum` whose provider is
  ; REAL -- `initium` holds the atom (`m.archivum()`) and the lambda captures
  ; it. Clean: the lambda row is read, and a `sub` satisfies it.
  fx_s17:	db 'publica functio initium(m: Mundus) -> u8 {', 10
		db 9, 'firma a = m.archivum();', 10
		db 9, 'sub archivum = a;', 10
		db 9, 'firma k = functio(v: textus) -> u8 {', 10
		db 9, 9, 'discerne Directorium.ad_radicem(archivum, v) {', 10
		db 9, 9, 9, 'casus prosperum(w) { redde 0; }', 10
		db 9, 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, 9, '}', 10
		db 9, '};', 10
		db 9, 'redde k("/");', 10
		db '}', 10
  fx_s17_LEN = $ - fx_s17
  ; s18: the legal twin of s13/s14: a lambda that draws NOTHING, called and
  ; forwarded inside `sicut d`. Clean: an empty live row contributes nothing.
  fx_s18:	db 'publica functio consumidor(x: u8, f: functio(u8) -> u8) -> u8 poscit sicut f {', 10
		db 9, 'redde f(x);', 10
		db '}', 10
		db 'publica functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'firma k = functio(y: u8) -> u8 { redde y; };', 10
		db 9, 'si k(1) ne 1 { redde 1; }', 10
		db 9, 'redde consumidor(0, k);', 10
		db '}', 10
  fx_s18_LEN = $ - fx_s18
  ; s19: THE EXCLUSION THAT WAS NOT PINNED (review finding, "M7"): an atom an
  ; INELIGIBLE argument also brings in is not covered by the caller's `sicut`.
  ; `g` takes two `sicut` ordinals; `s` (a parameter of `f`'s own, ELIGIBLE)
  ; substitutes `{ambitus}` at one and `nocens` (a named function drawing
  ; `ambitus`, INELIGIBLE) substitutes `{ambitus}` at the other. The atom is
  ; the same, so `f`'s `sicut s` covers the first and must not cover the
  ; second: without the `cvi` subtraction in `__chk_row_sicutcov` the two
  ; collapse and `f` is accepted having laundered `nocens`'s authority behind
  ; `s`'s. EXS-E0421. Rows 6 and 9 alone did not catch deleting it (mutant
  ; run when this row was written: both fixtures stayed green).
  fx_s19:	db 'publica functio nocens(x: u8) -> u8 poscit ambitus { redde x; }', 10
		db 'publica functio g(t: Scriptor, h: functio(u8) -> u8 poscit {ambitus}, b: u8) -> u8 poscit sicut t, sicut h {', 10
		db 9, 'redde h(b);', 10
		db '}', 10
		db 'publica functio f(s: Scriptor, b: u8) -> u8 poscit sicut s {', 10
		db 9, 'redde g(s, nocens, b);', 10
		db '}', 10
  fx_s19_LEN = $ - fx_s19

  fx_tab:
	dq fx_s01, fx_s01_LEN
	dd 0, 0, 0, 0
	dq fx_s02, fx_s02_LEN
	dd 0, 0, 0, 0
	dq fx_s03, fx_s03_LEN
	dd 1, 421, 0, 0
	dq fx_s04, fx_s04_LEN
	dd 0, 0, 0, 0
	dq fx_s05, fx_s05_LEN
	dd 0, 0, 0, 0
	dq fx_s06, fx_s06_LEN
	dd 1, 421, 0, 0
	dq fx_s07, fx_s07_LEN
	dd 1, 421, 0, 0
	dq fx_s08, fx_s08_LEN
	dd 1, 421, 0, 0
	dq fx_s09, fx_s09_LEN
	dd 1, 421, 0, 0
	dq fx_s10, fx_s10_LEN
	dd 1, 421, 0, 0
	dq fx_s11, fx_s11_LEN
	dd 1, 421, 0, 0
	dq fx_s12, fx_s12_LEN
	dd 1, 421, 0, 0
	dq fx_s13, fx_s13_LEN
	dd 1, 421, 0, 0
	dq fx_s14, fx_s14_LEN
	dd 1, 421, 0, 0
	dq fx_s15, fx_s15_LEN
	dd 1, 421, 0, 0
	dq fx_s16, fx_s16_LEN
	dd 1, 421, 0, 0
	dq fx_s17, fx_s17_LEN
	dd 0, 0, 0, 0
	dq fx_s18, fx_s18_LEN
	dd 0, 0, 0, 0
	dq fx_s19, fx_s19_LEN
	dd 1, 421, 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW
  assert ($ - fx_tab) mod FX_ROW = 0

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
