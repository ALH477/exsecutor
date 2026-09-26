; tests/unit/cst_refero_type.asm
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
; cst fixture: spec §8.6's NEW `CoreType` alternative, `'refero' '<' TypeArg
; '>'` (spec §6.4), through the real lexer and `cst_parse`.
;
; WHAT THIS FIXTURE IS THE EVIDENCE FOR. `refero` is a tier-1 reserved word
; (spec §8.4's "value flow" row, `lexer/keywords.inc`'s `KW_REFERO`) and until
; this alternative existed it had NO phrase-level form: spec §8.6's own "Not
; settled here" said so, and `functio consume_nodum(n: refero<Nodus>)` inside an
; `externus` block -- §14 entry 13's source -- was `EXS-E0201` at `refero` and
; never reached the checker, whose `EXS-E0520` rule for it has existed all
; along (`tests/unit/chk_ty_e0520_refero.asm`, which builds the tree BY HAND
; for exactly that reason). Row 3 below pins the half that is still refused: a
; BARE `refero` is `EXS-E0201` at the word, unchanged, because the form spec
; §8.6 now admits is `refero<T>` and not the name.
;
; THE PEEK IS ONE TOKEN AND IT IS REQUIRED. `__cst_at_refero` answers yes only
; at `refero <`, so the alternative costs the LL(1) grammar nothing and changes
; `__cst_type_starts` nowhere else. Row 6 is the other half of spec §8.6
; decision 6: `<` is a delimiter and never an operator, so an unclosed one is
; `EXS-E0201` at the `<` with the `lt` edit attached -- the same diagnostic any
; other generic argument list gets, which is what "error tolerance consistent
; with neighbours" has to mean.
;
; `refero` STAYS A KEYWORD LEAF AND THE NODE IS NOT A `PATH`. That is what the
; `PATH@` column is for: rows 1, 4, 5 and 6 have none, and row 2 -- the atomic
; twin `refero_communis<T>`, an ordinary identifier -- has one. A green leaf's
; kind is the lexer's class plus one (cst/parse.inc's `__cst_bump`), so
; emitting `TK_IDENT` for a reserved word to make the AST's job easier would
; have been a lexical lie in a tree whose whole purpose is losslessness;
; ast/from_cst.inc synthesises the `TyPath -> Path -> Seg` the checker needs
; instead (`tests/unit/chk_ty_e0520_refero_source.asm`).
;
;   row  source                                      diag   REFERO GARGS PATH
;    1   externus { functio nova(x: refero<u32>) }      0      1     1     0
;    2   the same with `refero_communis<u32>`           0      0     1     1
;    3   x: refero            (bare: E0201 at 21)     1@21     0     0     0
;    4   x: refero<refero<u32>>  (nested; `>>` is
;                             two tokens, decision 6)   0      2     2     0
;    5   x: &refero<u32>      (a reference to one)      0      1     1     0
;    6   x: refero<u32        (unclosed: E0201 at the
;                             `<`, 27, `lt` attached)  1@27     1     1     0
;
; EVERY ROW ALSO ROUND-TRIPS. `cst_text` on the root must give back the source
; byte for byte -- the trivia inside a `REFERO_TYPE` (the keyword's leading
; whitespace) is part of that, and a node that dropped it would still have the
; right node counts.
;
; Exit 0 = every row held; 10+N = row N's counts or diagnostic failed; 30+N =
; row N did not round-trip; 99 = setup.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §6.4, §8.4, §8.6, §9.1, §13.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/cst/cst.inc'

CT_ARENA = 4 shl 20
CT_BUF   = 64 shl 10
CT_ROW   = 40		; dq src, len; dd ndiag, code, off, nrefero, ngargs, npath

segment readable executable

  start:
	mov	rax, [rsp]
	mov	[ct_argc], rax
	call	ct_setup

	xor	r12, r12
  .row:
	cmp	r12, CT_NROWS
	jae	.done
	mov	rax, r12
	imul	rax, CT_ROW
	lea	r13, [ct_tab]
	add	r13, rax
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	call	ct_run

	; ---- the round trip, before anything else -----------------------------
	; A tree that lost a byte is not a tree this fixture can then count
	; nodes in and believe.
	lea	rdi, [r12 + 31]
	call	ct_text
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	call	ct_same
	test	eax, eax
	jz	.bad_text

	call	ct_dump
	; the diagnostic count, then the first one's code and offset
	call	ct_ndiag
	mov	r14, rax
	mov	ecx, [r13 + 16]
	cmp	r14, rcx
	jne	.bad
	test	r14, r14
	jz	.counts
	xor	edi, edi
	call	ct_diag
	mov	ecx, [r13 + 20]
	cmp	[rax + Diag.code_num], ecx
	jne	.bad
	mov	ecx, [r13 + 24]
	cmp	[rax + Diag.span.start], ecx
	jne	.bad
  .counts:
	lea	rdi, [kw_refero]
	mov	rsi, KW_REFERO_LEN
	call	ct_count
	mov	ecx, [r13 + 28]
	cmp	rax, rcx
	jne	.bad
	lea	rdi, [kw_gargs]
	mov	rsi, KW_GARGS_LEN
	call	ct_count
	mov	ecx, [r13 + 32]
	cmp	rax, rcx
	jne	.bad
	lea	rdi, [kw_path]
	mov	rsi, KW_PATH_LEN
	call	ct_count
	mov	ecx, [r13 + 36]
	cmp	rax, rcx
	jne	.bad
	inc	r12
	jmp	.row
  .bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .bad_text:
	lea	rdi, [r12 + 31]
	call	sys_exit_group
  .done:
	xor	edi, edi
	call	sys_exit_group

; ---- harness ---------------------------------------------------------------
; tests/unit/cst_structlit.asm's, plus `ct_text`/`ct_same`. Plain labels, not
; `proc`: a `proc` argument name is an unmangled global (macros/proc.inc's
; header). Every helper pushes an ODD number of registers so `rsp` is
; 16-aligned at the calls inside it.

; ct_setup -- two arenas and an interner. The interner's arena is never reset:
; leaf text lives in it.
  ct_setup:
	push	rbx
	lea	rdi, [ct_arena]
	mov	rsi, CT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [ct_iarena]
	mov	rsi, CT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [ct_intern]
	lea	rsi, [ct_iarena]
	mov	rdx, 1024
	call	intern_init
	pop	rbx
	ret
  .boom:
	mov	edi, 99
	call	sys_exit_group

; ct_run(rdi = source bytes, rsi = length) -- lex and parse into a fresh tree.
  ct_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	lea	rdi, [ct_arena]
	call	arena_reset
	lea	rdi, [ct_toks]
	lea	rsi, [ct_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 64
	call	vec_init
	lea	rdi, [ct_diags]
	lea	rsi, [ct_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [ct_lx]
	lea	rsi, [ct_arena]
	lea	rdx, [ct_intern]
	lea	rcx, [ct_toks]
	lea	r8,  [ct_diags]
	call	lex_init
	lea	rdi, [ct_lx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [ct_path]
	mov	r8d, CT_PATH_LEN
	call	lex_set_source
	lea	rdi, [ct_lx]
	call	lex_run
	jc	.boom
	lea	rdi, [ct_green]
	lea	rsi, [ct_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 64
	call	vec_init
	lea	rdi, [ct_work]
	lea	rsi, [ct_arena]
	mov	rdx, 4
	mov	rcx, 64
	call	vec_init
	lea	rdi, [ct_map]
	lea	rsi, [ct_arena]
	mov	rdx, 256
	call	map_init
	lea	rdi, [ct_tree]
	lea	rsi, [ct_arena]
	lea	rdx, [ct_intern]
	lea	rcx, [ct_green]
	lea	r8,  [ct_work]
	lea	r9,  [ct_map]
	call	cst_tree_init
	lea	rdi, [ct_p]
	lea	rsi, [ct_lx]
	lea	rdx, [ct_tree]
	call	cst_parse
	pop	r13
	pop	r12
	pop	rbx
	ret
  .boom:
	; a sample that fails spec §8.1 is a broken fixture, not a parse result
	mov	edi, 99
	call	sys_exit_group

; ct_text(rdi = the exit code to use on truncation) -- the last tree's SOURCE
; TEXT into ct_buf; ct_len = its length.
  ct_text:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	lea	rdi, [ct_out]
	lea	rsi, [ct_buf]
	mov	rdx, CT_BUF
	call	diag_out_init
	lea	rdi, [ct_tree]
	lea	rax, [ct_tree]
	mov	esi, [rax + CstTree.root]
	lea	rdx, [ct_out]
	call	cst_text
	test	eax, eax
	jz	.short
	lea	rax, [ct_out]
	mov	rax, [rax + DiagOut.len]
	mov	[ct_len], rax
	pop	r13
	pop	r12
	pop	rbx
	ret
  .short:
	mov	edi, r12d
	call	sys_exit_group

; ct_same(rdi = bytes, rsi = length) -> eax = 1 if ct_buf[0..ct_len) is exactly
; those bytes.
  ct_same:
	push	rbx
	cmp	rsi, [ct_len]
	jne	.no
	xor	ecx, ecx
  .byte:
	cmp	rcx, rsi
	jae	.yes
	lea	rdx, [ct_buf]
	mov	r8b, [rdx + rcx]
	cmp	r8b, [rdi + rcx]
	jne	.no
	inc	rcx
	jmp	.byte
  .yes:
	mov	eax, 1
	pop	rbx
	ret
  .no:
	xor	eax, eax
	pop	rbx
	ret

; ct_dump -- the last tree's dump into ct_buf; ct_len = its length. Printed to
; stdout as well when the fixture was given an argument.
  ct_dump:
	push	rbx
	lea	rdi, [ct_out]
	lea	rsi, [ct_buf]
	mov	rdx, CT_BUF
	call	diag_out_init
	lea	rdi, [ct_tree]
	lea	rsi, [ct_out]
	xor	edx, edx
	call	cst_dump
	mov	rax, [ct_out + DiagOut.len]
	mov	[ct_len], rax
	cmp	qword [ct_argc], 1
	jbe	.quiet
	mov	rdi, 1
	lea	rsi, [ct_buf]
	mov	rdx, [ct_len]
	call	sys_write
  .quiet:
	pop	rbx
	ret

; ct_count(rdi = needle, rsi = needle length) -> rax = how many times the
; needle occurs in the last dump. A byte search, overlapping matches counted.
  ct_count:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	xor	eax, eax		; matches
	xor	ecx, ecx		; start position
  .pos:
	mov	rdx, rcx
	add	rdx, r13
	cmp	rdx, [ct_len]
	ja	.out
	xor	r8, r8
  .cmp:
	cmp	r8, r13
	jae	.hit
	lea	r9, [ct_buf]
	add	r9, rcx
	mov	r10b, [r9 + r8]
	cmp	r10b, [r12 + r8]
	jne	.miss
	inc	r8
	jmp	.cmp
  .hit:
	inc	rax
  .miss:
	inc	rcx
	jmp	.pos
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; ct_ndiag -> rax = diagnostics from the last ct_run (lexer's and parser's).
  ct_ndiag:
	mov	rax, [ct_diags + Vec.len]
	ret

; ct_diag(rdi = index) -> rax = that `Diag`.
  ct_diag:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [ct_diags]
	call	vec_get
	pop	rbx
	ret

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'
  ct_path:	db 'cst_refero_type.exsc'
  CT_PATH_LEN = $ - ct_path

  ; Rows 1 and 2 are §14 entry 13's own shape: the pair is nine characters
  ; apart and only the second one may cross an `externus` boundary (spec §6.4).
  ct_s01:	db 'externus("C", abi: sysv_amd64) {', 10
		db 9, 'functio nova(x: refero<u32>) -> u8', 10
		db '}', 10
  ct_s01_LEN = $ - ct_s01
  ct_s02:	db 'externus("C", abi: sysv_amd64) {', 10
		db 9, 'functio nova(x: refero_communis<u32>) -> u8', 10
		db '}', 10
  ct_s02_LEN = $ - ct_s02

  ; `publica functio f(x: ` is 21 bytes, so the type starts at byte 21.
  macro ct_fn name, ty
	name: db 'publica functio f(x: ', ty, ') -> u8 {', 10, 9, 'redde 1;', 10, '}', 10
	name#_LEN = $ - name
  end macro

  ct_fn ct_s03, 'refero'
  ct_fn ct_s04, 'refero<refero<u32>>'
  ct_fn ct_s05, '&refero<u32>'
  ct_fn ct_s06, 'refero<u32'

  kw_refero:	db 'REFERO_TYPE@'
  KW_REFERO_LEN = $ - kw_refero
  kw_gargs:	db 'GENERIC_ARGS@'
  KW_GARGS_LEN = $ - kw_gargs
  kw_path:	db 'PATH@'
  KW_PATH_LEN = $ - kw_path

  ct_tab:
	dq ct_s01, ct_s01_LEN
	dd 0, 0, 0, 1, 1, 0
	dq ct_s02, ct_s02_LEN
	dd 0, 0, 0, 0, 1, 1
	dq ct_s03, ct_s03_LEN
	dd 1, 201, 21, 0, 0, 0
	dq ct_s04, ct_s04_LEN
	dd 0, 0, 0, 2, 2, 0
	dq ct_s05, ct_s05_LEN
	dd 0, 0, 0, 1, 1, 0
	dq ct_s06, ct_s06_LEN
	dd 1, 201, 27, 1, 1, 0
  CT_NROWS = ($ - ct_tab) / CT_ROW
  ; a row of the wrong width would shift every row after it
  assert ($ - ct_tab) mod CT_ROW = 0

segment readable writeable
  ct_argc:	rq 1
  ct_len:	rq 1
  ct_arena:	rb sizeof.Arena
  ct_iarena:	rb sizeof.Arena
  ct_intern:	rb sizeof.Interner
  ct_toks:	rb sizeof.Vec
  ct_diags:	rb sizeof.Vec
  ct_lx:	rb sizeof.Lexer
  ct_green:	rb sizeof.Vec
  ct_work:	rb sizeof.Vec
  ct_map:	rb sizeof.Map
  ct_tree:	rb sizeof.CstTree
  ct_p:		rb sizeof.CstParser
  ct_out:	rb sizeof.DiagOut
  ct_buf:	rb CT_BUF
