; tests/unit/chk_directorium.asm
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
; checker fixture -- ADR 0017 stage 2's surface, and the three properties the
; design rests its attenuation on (docs/design/archivum-beneath.md D6):
;
;   R1  the raw atom cannot be obtained from a `Directorium`: a `sicut` row
;       item never binds the atom's carrier, so inside a function whose row
;       is only `poscit sicut d`, `archivum` in expression position is
;       EXS-E0421 -- and nothing else in the signature (a parameter's TYPE
;       row included) provides it; and `sub P = e` requires `e` to have P's
;       own capability type, so `sub archivum = d;` is EXS-E0303 (rows 14-16);
;       and a lambda's draw is read at its live row, which is
;       chk_row_sicut_forward.asm rows 11-18;
;   R2  no prelude routine draws `archivum` implicitly: `ad_radicem` takes it
;       as a value, and every other row is empty;
;   R3  `Directorium`, `Lectorium` and `Scriptorium` have no field rows.
;
;	1  a whole program over the surface                     clean
;	2  a root forwarded two levels, derived beneath          clean
;	3  R2: every value row called from `sicut`-only helpers  clean
;	4  R1: `archivum` in a `sicut d` function                {EXS-E0421}
;	5  R1: a parameter's type row is not a provider          {EXS-E0421}
;	6  R2: `ad_radicem` without the atom                     {E0303, E0304}
;	7  R3: `d.a`, `d.descriptor`                             {E0305, E0305}
;	8  R3: `r.a`, `w.descriptor`                             {E0305, E0305}
;	9  `Scriptor` is not `Scriptorium`                       {EXS-E0303}
;	10 a `Directorium` in module-level state                 {EXS-E0501}
;	11 `@transitus` over a `Directorium`                     {EXS-E0321}
;	12 `d sicut acies<u8, 16>`                               {EXS-E0305}
;	13 `b sicut Directorium`                                 {EXS-E0305}
;	14 the exploit: `sub archivum = d;` in a `sicut d` fn   {EXS-E0303}
;	15 `sub rete = <u32>`                                    {EXS-E0303}
;	16 `sub alloc = <u32>` (the arena, `[OPEN]`)             clean
;	17 the reference exploit: `&d sicut &archivum`           {EXS-E0305}
;	18 `&d sicut &Scriptor`                                  {EXS-E0305}
;	19 `&a sicut &Directorium` (a ref to the raw atom)       {EXS-E0305}
;	20 `&d sicut &Directorium`, `u8 sicut u16`, `u16 sicut u8`  clean
;	21 `sub archivum = x sicut archivum;` (a u32)            {EXS-E0305}
;	22 `sub archivum = 0 sicut archivum;` (a pending literal) {EXS-E0305}
;	23 `&x sicut u64`, `n sicut &archivum`                   {E0305, E0305}
;	24 `a sicut u64` (the atom as a number)                  {EXS-E0305}
;	25 `g sicut functio(u8) -> u8` (a row taken off `g`)     {EXS-E0305}
;	26 `&x sicut &mutabilis u8` (mutability gained)          {EXS-E0305}
;
; NON-VACUITY, run when this fixture was written (each mutant in a scratch
; copy of the tree; the exit given is the one measured -- the first three
; were predicted, the last was predicted as 19 and landed earlier):
;   - R1: `__chk_rowitem` binding `archivum` for every `sicut` item (the
;     carrier a `Directorium`'s mark names) -> exit 14 (row 4 clean);
;   - R2: `ad_radicem`, `infra`, `lege_ex` and `crea` typed with the row
;     `{archivum}` instead of the empty row -> exit 11 (row 1: `salva`
;     declares only `sicut d` and its `d.crea` now draws the atom);
;   - R3: the `Directorium` member arm admitting `Scriptor`'s `a` and
;     `descriptor` rows -> exit 17 (row 7 clean);
;   - `Scriptorium` interned with `Scriptor`'s tag -> exit 11 (row 1's
;     writer is then a `Scriptor`, which has no `inscribe_octeto`; row 9
;     would be accepted behind it).
;
; Rows 17-26 pin R1 rule 4 (docs/design/archivum-beneath.md finding 5): no
; `sicut` retypes a value into or out of the atom or a reference to one. Row
; 17 is the exploit that falsified the claim "the atom cannot be minted out of
; a `Directorium`" after rows 12-14 were green: the cast dispatch judged only
; aggregates, so `&T sicut &U` fell through unjudged. `[OPEN]`: `Crudum`
; (spec §4.6) would license an unchecked reinterpret; nothing implements it,
; so none is admitted. NON-VACUITY, measured: with the dispatch as it was
; (`__chk_ty_castrel` never called) each of rows 17-19 and 21-26 fails at its
; own row, started one at a time (exit 10+N); row 20, the one that must stay
; clean, fails (exit 30) when castrel loses its same-type arm or stops
; counting an integer as a numeric scalar.
;
; Exit 0 = every row passed; 10+N = row N of the table above did not match,
; with the diagnostics it did produce printed to stdout first.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.2, §4.3, §4.6, §4.7, §5.2;
; ADR 0017; docs/design/archivum-beneath.md D1-D6.
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

  fx_path	db 'chk_directorium.exsc'
  FX_PATH_LEN = $ - fx_path

  ; ==== ROWS (each source measured with exsc --diagnostica json) ====
  ; s01: a whole program over the surface: `m.archivum()`, `sub archivum`,
  ; `Directorium.ad_radicem`, `d.infra`, and a child root handed to a helper
  ; that declares only `poscit sicut d` and calls `crea` and
  ; `inscribe_octeto`. Clean.
  fx_s01:	db 'functio salva(d: Directorium, b: u8) -> u8 poscit sicut d {', 10
		db 9, 'discerne d.crea("nova") {', 10
		db 9, 9, 'casus prosperum(w) {', 10
		db 9, 9, 9, 'discerne w.inscribe_octeto(b) {', 10
		db 9, 9, 9, 9, 'casus prosperum(n) { redde 0; }', 10
		db 9, 9, 9, 9, 'casus adversum(e) { redde 2; }', 10
		db 9, 9, 9, '}', 10
		db 9, 9, '}', 10
		db 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, '}', 10
		db '}', 10
		db 'publica functio initium(m: Mundus) -> u8 {', 10
		db 9, 'firma a = m.archivum();', 10
		db 9, 'sub archivum = a;', 10
		db 9, 'discerne Directorium.ad_radicem(a, "/tmp/x") {', 10
		db 9, 9, 'casus prosperum(d) {', 10
		db 9, 9, 9, 'discerne d.infra("sub") {', 10
		db 9, 9, 9, 9, 'casus prosperum(e) { redde salva(e, 65); }', 10
		db 9, 9, 9, 9, 'casus adversum(x) { redde 3; }', 10
		db 9, 9, 9, '}', 10
		db 9, 9, '}', 10
		db 9, 9, 'casus adversum(e) { redde 4; }', 10
		db 9, '}', 10
		db '}', 10
  fx_s01_LEN = $ - fx_s01
  ; s02: forwarding a `Directorium` through two helper levels, the lowest
  ; receiving a root DERIVED beneath the top one's (`d.infra`). Clean --
  ; tests/unit/chk_row_sicut_forward.asm is the rule this rests on.
  fx_s02:	db 'publica functio imus(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'discerne d.crea("nova") {', 10
		db 9, 9, 'casus prosperum(w) { redde 0; }', 10
		db 9, 9, 'casus adversum(x) { redde 1; }', 10
		db 9, '}', 10
		db '}', 10
		db 'publica functio medius(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'redde imus(d);', 10
		db '}', 10
		db 'publica functio summus(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'discerne d.infra("sub") {', 10
		db 9, 9, 'casus prosperum(e) { redde medius(e); }', 10
		db 9, 9, 'casus adversum(x) { redde 9; }', 10
		db 9, '}', 10
		db '}', 10
  fx_s02_LEN = $ - fx_s02
  ; s03: R2: helpers that declare only `sicut` call every row ADR 0017 adds
  ; on a value -- `infra`, `crea`, `lege_ex`, `exlege_octeto`, `inscribe`,
  ; `inscribe_octeto`. Clean: none of them draws `archivum`.
  fx_s03:	db 'publica functio lege(r: Lectorium) -> u16 poscit sicut r {', 10
		db 9, 'discerne r.exlege_octeto() {', 10
		db 9, 9, 'casus prosperum(b) { redde b; }', 10
		db 9, 9, 'casus adversum(e) { redde 300; }', 10
		db 9, '}', 10
		db '}', 10
		db 'publica functio scribe(w: Scriptorium) -> u8 poscit sicut w {', 10
		db 9, 'discerne w.inscribe("salve") {', 10
		db 9, 9, 'casus prosperum(n) {', 10
		db 9, 9, 9, 'discerne w.inscribe_octeto(10) {', 10
		db 9, 9, 9, 9, 'casus prosperum(k) { redde 0; }', 10
		db 9, 9, 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, 9, 9, '}', 10
		db 9, 9, '}', 10
		db 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, '}', 10
		db '}', 10
		db 'publica functio omnia(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'discerne d.infra("sub") {', 10
		db 9, 9, 'casus prosperum(e) {', 10
		db 9, 9, 9, 'discerne e.crea("nova") {', 10
		db 9, 9, 9, 9, 'casus prosperum(w) { si scribe(w) ne 0 { redde 1; } }', 10
		db 9, 9, 9, 9, 'casus adversum(x) { redde 2; }', 10
		db 9, 9, 9, '}', 10
		db 9, 9, 9, 'discerne e.lege_ex("nova") {', 10
		db 9, 9, 9, 9, 'casus prosperum(r) { si lege(r) ne 115 { redde 3; } }', 10
		db 9, 9, 9, 9, 'casus adversum(x) { redde 4; }', 10
		db 9, 9, 9, '}', 10
		db 9, 9, 9, 'redde 0;', 10
		db 9, 9, '}', 10
		db 9, 9, 'casus adversum(x) { redde 5; }', 10
		db 9, '}', 10
		db '}', 10
  fx_s03_LEN = $ - fx_s03
  ; s04: R1: in a function whose row is only `poscit sicut d`, the expression
  ; `archivum` -- the one way to a second root -- is EXS-E0421.
  fx_s04:	db 'publica functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'discerne Directorium.ad_radicem(archivum, "/") {', 10
		db 9, 9, 'casus prosperum(e) { redde 0; }', 10
		db 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, '}', 10
		db '}', 10
  fx_s04_LEN = $ - fx_s04
  ; s05: R1 again, past the hole commit 4160ad7 shut: a parameter whose TYPE
  ; carries `poscit {archivum}` provides no carrier. EXS-E0421.
  fx_s05:	db 'publica functio salva(d: Directorium, g: functio(u8) -> u8 poscit {archivum}) -> u8 poscit sicut d {', 10
		db 9, 'discerne Directorium.ad_radicem(archivum, "/") {', 10
		db 9, 9, 'casus prosperum(e) { redde 0; }', 10
		db 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, '}', 10
		db '}', 10
  fx_s05_LEN = $ - fx_s05
  ; s06: R2's other half: `ad_radicem` takes the atom as an explicit VALUE;
  ; leaving it out is not a call to a routine that finds one. {EXS-E0303 (the
  ; `textus` is not an `archivum`), EXS-E0304 (one argument of two)}.
  fx_s06:	db 'publica functio initium(m: Mundus) -> u8 {', 10
		db 9, 'discerne Directorium.ad_radicem("/x") {', 10
		db 9, 9, 'casus prosperum(e) { redde 0; }', 10
		db 9, 9, 'casus adversum(e) { redde 1; }', 10
		db 9, '}', 10
		db '}', 10
  fx_s06_LEN = $ - fx_s06
  ; s07: R3: `d.a` and `d.descriptor` do not resolve. EXS-E0305 twice --
  ; the code every missing member gets (the design said E0301; measured, it
  ; is E0305, as for `Lector`).
  fx_s07:	db 'publica functio f(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'firma x = d.a;', 10
		db 9, 'firma y = d.descriptor;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s07_LEN = $ - fx_s07
  ; s08: R3 for the reader and the writer: `r.a`, `w.descriptor`.
  fx_s08:	db 'publica functio f(r: Lectorium, w: Scriptorium) -> u8 poscit sicut r, sicut w {', 10
		db 9, 'firma x = r.a;', 10
		db 9, 'firma y = w.descriptor;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s08_LEN = $ - fx_s08
  ; s09: the three are their own nominal types: a `Scriptor` (mark
  ; {ambitus}) is not a `Scriptorium`. EXS-E0303.
  fx_s09:	db 'publica functio f(w: Scriptorium) -> u8 poscit sicut w {', 10
		db 9, 'redde 0;', 10
		db '}', 10
		db 'publica functio g(s: Scriptor) -> u8 poscit sicut s {', 10
		db 9, 'redde f(s);', 10
		db '}', 10
  fx_s09_LEN = $ - fx_s09
  ; s10: a `Directorium` in module-level state: EXS-E0501, as design D6
  ; says it already was.
  fx_s10:	db 'firma radix_globalis: Directorium;', 10
  fx_s10_LEN = $ - fx_s10
  ; s11: `@transitus` over a struct holding a `Directorium` is refused --
  ; EXS-E0321, because the prelude record is `:nativus`, not because it is
  ; capability-bearing (design D1's [OPEN]: no code says the latter).
  fx_s11:	db '@transitus', 10
		db 'structura Arca { d: Directorium }', 10
  fx_s11_LEN = $ - fx_s11
  ; s12: no cast turns a `Directorium` into bytes...
  fx_s12:	db 'publica functio f(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'firma b = d sicut acies<u8, 16>;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s12_LEN = $ - fx_s12
  ; s13: ...or bytes into a `Directorium`.
  fx_s13:	db 'publica functio f(b: acies<u8, 16>) -> u8 {', 10
		db 9, 'firma d = b sicut Directorium;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s13_LEN = $ - fx_s13

  ; s14: BLOCKER 1, the exploit itself. `initium` derives a root and a child
  ; beneath it; `salva` receives only the child and declares only `sicut d`.
  ; `sub archivum = d;` used to bind the raw atom (the provider was never
  ; typed unless it was ANOTHER atom's `cap`), after which `ad_radicem` opened
  ; /tmp/exs-xp, outside the child's root -- built and run: it read a file
  ; there and exited 42. EXS-E0303 now: `d` is a `Directorium`, not an
  ; `archivum`. This is the row that fails if `__chk_ty_sub`'s provider check
  ; goes back to half-applied.
  fx_s14:	db 'functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'sub archivum = d;', 10
		db 9, 'discerne Directorium.ad_radicem(archivum, "/tmp/exs-xp") {', 10
		db 9, 9, 'casus prosperum(o) { redde 42; }', 10
		db 9, 9, 'casus adversum(e) { redde 4; }', 10
		db 9, '}', 10
		db '}', 10
		db 'publica functio initium(m: Mundus) -> u8 {', 10
		db 9, 'firma a = m.archivum();', 10
		db 9, 'sub archivum = a;', 10
		db 9, 'discerne Directorium.ad_radicem(a, "/tmp/exs-xp/root") {', 10
		db 9, 9, 'casus prosperum(d) {', 10
		db 9, 9, 9, 'discerne d.infra("sub") {', 10
		db 9, 9, 9, 9, 'casus prosperum(c) { redde salva(c); }', 10
		db 9, 9, 9, 9, 'casus adversum(x) { redde 5; }', 10
		db 9, 9, 9, '}', 10
		db 9, 9, '}', 10
		db 9, 9, 'casus adversum(e) { redde 6; }', 10
		db 9, '}', 10
		db '}', 10
  fx_s14_LEN = $ - fx_s14
  ; s15: the same hole with no struct in it: `sub rete = a;` with a `u32`.
  ; EXS-E0303. (tests/unit/chk_e0422_sub_twice.asm used to rely on exactly
  ; this being accepted.)
  fx_s15:	db 'publica functio f(a: u32) -> u8 {', 10
		db 9, 'sub rete = a;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s15_LEN = $ - fx_s15
  ; s16: the ONE exemption, pinned so it cannot move silently. Spec §4.5's
  ; `sub alloc = a;` binds "an arena", a value whose type no section names
  ; (`[OPEN]`), so `alloc` still takes a provider that is not a `cap`. Clean.
  ; When the spec names the arena's type this row changes with it.
  fx_s16:	db 'publica functio f(a: u32) -> u8 {', 10
		db 9, 'sub alloc = a;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s16_LEN = $ - fx_s16

  ; s17: THE REFERENCE FORM of row 12, and the exploit that falsified R1 rule 2
  ; after rows 12-14 were green. `&d sicut &archivum` took a reference to a
  ; `Directorium` and retyped it as a reference to the raw atom; `*p` then
  ; typed as `archivum`, which is exactly what `sub archivum = e` demands, so
  ; `sub` bound the atom and `ad_radicem` opened a file outside the child's
  ; root (built and run: it read it and exited with its first byte). The cast
  ; dispatch sent a cast to the aggregate judge only when a SIDE was a struct
  ; or an `acies`; `&T` is neither, so a reference cast fell through
  ; unjudged. EXS-E0305 at the cast, and nothing else: the cast's type is its
  ; target either way, so `sub` is not asked a second time.
  fx_s17:	db 'functio salva(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'firma p = &d sicut &archivum;', 10
		db 9, 'sub archivum = *p;', 10
		db 9, 'discerne Directorium.ad_radicem(archivum, "/tmp/exs-xp") {', 10
		db 9, 9, 'casus prosperum(o) { redde 42; }', 10
		db 9, 9, 'casus adversum(e) { redde 4; }', 10
		db 9, '}', 10
		db '}', 10
  fx_s17_LEN = $ - fx_s17
  ; s18: a neighbour that holds no atom at all: a `Directorium` reference as a
  ; `Scriptor` reference (a different mark, a different type). Refused for the
  ; same reason -- the pointee changed.
  fx_s18:	db 'functio f(d: Directorium) -> u8 poscit sicut d {', 10
		db 9, 'firma p = &d sicut &Scriptor;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s18_LEN = $ - fx_s18
  ; s19: the other direction: a reference to the raw atom, taken from a
  ; function that legitimately holds it, retyped as a reference to a
  ; `Directorium`.
  fx_s19:	db 'publica functio initium(m: Mundus) -> u8 {', 10
		db 9, 'firma a = m.archivum();', 10
		db 9, 'firma p = &a;', 10
		db 9, 'firma q = p sicut &Directorium;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s19_LEN = $ - fx_s19
  ; s20: what must STAY accepted, so the refusal above is not a blanket one:
  ; a reference cast that changes nothing (`&d sicut &Directorium`), a cast
  ; between numeric scalars, and a cast of a pending-free scalar variable.
  ; Clean. (A reference cast that changes the pointee has no clean spelling;
  ; spec §4.6 gives `Crudum` that, and nothing implements it, `[OPEN]`.)
  fx_s20:	db 'functio f(d: Directorium, x: u8) -> u8 poscit sicut d {', 10
		db 9, 'firma p = &d sicut &Directorium;', 10
		db 9, 'firma w = x sicut u16;', 10
		db 9, 'firma n = w sicut u8;', 10
		db 9, 'redde n;', 10
		db '}', 10
  fx_s20_LEN = $ - fx_s20
  ; s21: the atom cannot be made out of a number (spec §4.1 rule 1: "No
  ; literal, no cast, no default"), by a value cast...
  fx_s21:	db 'functio f(x: u32) -> u8 {', 10
		db 9, 'sub archivum = x sicut archivum;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s21_LEN = $ - fx_s21
  ; s22: ...by a literal, whose type is pending and which the dispatch used to
  ; wave through before it looked at the target...
  fx_s22:	db 'functio f() -> u8 {', 10
		db 9, 'sub archivum = 0 sicut archivum;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s22_LEN = $ - fx_s22
  ; s23: ...or by turning a reference into an address and back.
  fx_s23:	db 'functio f(x: u8, n: u64) -> u8 {', 10
		db 9, 'firma a = &x sicut u64;', 10
		db 9, 'firma p = n sicut &archivum;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s23_LEN = $ - fx_s23
  ; s24: and the atom cannot be turned into a number either (the first half
  ; of a launder that needs both).
  fx_s24:	db 'publica functio initium(m: Mundus) -> u8 {', 10
		db 9, 'firma a = m.archivum();', 10
		db 9, 'firma n = a sicut u64;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s24_LEN = $ - fx_s24
  ; s25: the same hole in a function TYPE. `g` draws `archivum`; recast as
  ; `functio(u8) -> u8` it has the empty row, and the call through it draws
  ; nothing. Without the cast, `g`'s own row is `EXS-E0421` at the call.
  fx_s25:	db 'functio g(a: u8) -> u8 poscit archivum { redde a; }', 10
		db 'functio f(x: u8) -> u8 {', 10
		db 9, 'firma h = g sicut functio(u8) -> u8;', 10
		db 9, 'redde h(x);', 10
		db '}', 10
  fx_s25_LEN = $ - fx_s25
  ; s26: a borrow cannot gain mutability by a cast either.
  fx_s26:	db 'functio f(x: u8) -> u8 {', 10
		db 9, 'firma p = &x sicut &mutabilis u8;', 10
		db 9, 'redde 0;', 10
		db '}', 10
  fx_s26_LEN = $ - fx_s26

  fx_tab:
	dq fx_s01, fx_s01_LEN
	dd 0, 0, 0, 0
	dq fx_s02, fx_s02_LEN
	dd 0, 0, 0, 0
	dq fx_s03, fx_s03_LEN
	dd 0, 0, 0, 0
	dq fx_s04, fx_s04_LEN
	dd 1, 421, 0, 0
	dq fx_s05, fx_s05_LEN
	dd 1, 421, 0, 0
	dq fx_s06, fx_s06_LEN
	dd 2, 303, 304, 0
	dq fx_s07, fx_s07_LEN
	dd 2, 305, 305, 0
	dq fx_s08, fx_s08_LEN
	dd 2, 305, 305, 0
	dq fx_s09, fx_s09_LEN
	dd 1, 303, 0, 0
	dq fx_s10, fx_s10_LEN
	dd 1, 501, 0, 0
	dq fx_s11, fx_s11_LEN
	dd 1, 321, 0, 0
	dq fx_s12, fx_s12_LEN
	dd 1, 305, 0, 0
	dq fx_s13, fx_s13_LEN
	dd 1, 305, 0, 0
	dq fx_s14, fx_s14_LEN
	dd 1, 303, 0, 0
	dq fx_s15, fx_s15_LEN
	dd 1, 303, 0, 0
	dq fx_s16, fx_s16_LEN
	dd 0, 0, 0, 0
	dq fx_s17, fx_s17_LEN
	dd 1, 305, 0, 0
	dq fx_s18, fx_s18_LEN
	dd 1, 305, 0, 0
	dq fx_s19, fx_s19_LEN
	dd 1, 305, 0, 0
	dq fx_s20, fx_s20_LEN
	dd 0, 0, 0, 0
	dq fx_s21, fx_s21_LEN
	dd 1, 305, 0, 0
	dq fx_s22, fx_s22_LEN
	dd 1, 305, 0, 0
	dq fx_s23, fx_s23_LEN
	dd 2, 305, 305, 0
	dq fx_s24, fx_s24_LEN
	dd 1, 305, 0, 0
	dq fx_s25, fx_s25_LEN
	dd 1, 305, 0, 0
	dq fx_s26, fx_s26_LEN
	dd 1, 305, 0, 0
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
