; tests/unit/cst_error_tolerance.asm
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
; ERROR TOLERANCE: truncated and malformed input produces a TREE, not a crash,
; and produces the code §8.6 names for that situation, at the span §8.3
; requires, with the fix payload §8.6 asks for where one is derivable.
;
; §16's kill criterion for this stage is diagnostics quality -- "not
; retrofittable. Bad here -> stop and fix" -- so the expectations here are not
; "some error was reported": every row pins the COUNT, the CODE, the SPAN and
; the FIX KIND, and the last column pins the SHAPE of the recovery by asserting
; whether the tree must contain an `ERROR` or `MISSING` node at all.
;
; That last column is what stops this fixture from passing vacuously. Four of
; the thirteen rows expect NO error node: `u4:maior` and `u12` are diagnosed
; but parse into an ordinary `BIT_TYPE` (losing the token would cost the round
; trip and gain nothing), a reserved word in identifier position is CONSUMED as
; the name, and a nested named function is parsed as the declaration the author
; plainly wrote. A parser that answered every error by throwing tokens into an
; `ERROR` node would fail those four rows.
;
; The two end-of-input rows are a deliberate PAIR: identical except for one
; unclosed `(`, they are the whole of §8.6's "EXS-E0202 for a bracket, block,
; or `<…>` still open at end of input; EXS-E0203 for end of input inside any
; other construct". A parser with no bracket-depth counter passes one and
; fails the other.
;
; Exit 0 = every expectation held. 11 = wrong diagnostic count, 12 = wrong
; first code, 13 = wrong span start, 14 = wrong span length, 15 = wrong fix
; kind, 16 = wrong error-node presence, 17 = no root at all (which is the
; crash-shaped failure this fixture exists to catch), 99 = setup failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/cst/cst.inc'

CT_ARENA = 4 shl 20

segment readable executable
CT_ROW = 40

  start:
	call	ct_setup
	xor	rbx, rbx
  .case:
	cmp	rbx, CASE_N
	jae	.done
	mov	rax, rbx
	imul	rax, CT_ROW
	lea	r14, [case_tab]
	add	r14, rax
	mov	rdi, [r14]
	mov	rsi, [r14 + 8]
	call	ct_run

	; A tree, on every path. This is the whole of "error-tolerant".
	lea	rax, [ct_tree]
	cmp	dword [rax + CstTree.root], 0
	je	.fail17

	call	ct_ndiag
	cmp	eax, [r14 + 16]
	jne	.fail11
	test	eax, eax
	jz	.errnode

	xor	rdi, rdi
	call	ct_diag
	mov	r12, rax
	mov	ecx, [r14 + 20]
	cmp	[r12 + Diag.code_num], ecx
	jne	.fail12
	mov	ecx, [r14 + 24]
	cmp	[r12 + Diag.span.start], ecx
	jne	.fail13
	mov	ecx, [r14 + 28]
	cmp	[r12 + Diag.span.len], ecx
	jne	.fail14
	mov	rdi, r12
	call	diag_fix_ptr
	mov	ecx, [r14 + 32]
	cmp	[rax + DiagFix.kind], ecx
	jne	.fail15

	; ---- the shape of the recovery -------------------------------------
	; Scan the green nodes for an ERROR or a MISSING. Every green record
	; created during a parse is in the tree -- `cst_cancel` removes a MARK,
	; never a record -- so this is the tree's own answer, not a proxy.
  .errnode:
	xor	r13, r13
	xor	r12, r12
  .scan:
	lea	rax, [ct_green]
	cmp	r12, [rax + Vec.len]
	jae	.scanned
	lea	rdi, [ct_green]
	mov	rsi, r12
	call	vec_get
	cmp	dword [rax + CstGreen.kind], CST_ERROR
	je	.found
	cmp	dword [rax + CstGreen.kind], CST_MISSING
	je	.found
	inc	r12
	jmp	.scan
  .found:
	mov	r13d, 1
  .scanned:
	cmp	r13d, [r14 + 36]
	jne	.fail16

	inc	rbx
	jmp	.case
  .done:
	mov	edi, 0
	call	sys_exit_group
  .fail11:
	mov	edi, 11
	call	sys_exit_group
  .fail12:
	mov	edi, 12
	call	sys_exit_group
  .fail13:
	mov	edi, 13
	call	sys_exit_group
  .fail14:
	mov	edi, 14
	call	sys_exit_group
  .fail15:
	mov	edi, 15
	call	sys_exit_group
  .fail16:
	mov	edi, 16
	call	sys_exit_group
  .fail17:
	mov	edi, 17
	call	sys_exit_group

; ---- harness ---------------------------------------------------------------
; Plain labels, not `proc`: a `proc` argument name is an unmangled global
; (macros/proc.inc's header), and a fixture has no business adding to that
; namespace. Every helper pushes an ODD number of registers so `rsp` is
; 16-aligned at the `call`s inside it -- at a helper's first instruction `rsp`
; is 8 (mod 16), because `call` pushed the return address.
;
; The interner's arena is separate and is NEVER reset: rt/intern.inc makes an
; id valid only for as long as the arena behind it lives, and `ct_run` rewinds
; the other one per case. The green tree's leaf text lives in that interner
; too, so resetting it would invalidate every leaf of every earlier tree.

; ct_setup -- two arenas and an interner.
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

; ct_run(rdi = source bytes, rsi = byte length) -> eax = the numeric part of
; the first PARSE diagnostic, or 0. Fresh vectors, a fresh green tree and a
; rewound scratch arena every call, so cases cannot leak into one another.
;
; §8.1's pass runs first and its result is deliberately IGNORED for the
; tokenize step, unlike `lex_run`, which refuses to tokenize a file that failed
; it. Every sample here is admissible §8.1 source; splitting the two phases is
; what lets a fixture feed the parser a file with §8.4-level errors in it.
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
	mov	rcx, 32
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
	call	lex_source_check
	lea	rdi, [ct_lx]
	call	lex_tokenize
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
	jc	.dirty
	xor	eax, eax
  .dirty:
	pop	r13
	pop	r12
	pop	rbx
	ret

; ct_text -> rax = the round-tripped byte length in ct_buf. Exits `code` if the
; writer refused a byte (the buffer is 128 KB and no sample is close).
;   rdi = the exit code to use on truncation
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
	pop	r13
	pop	r12
	pop	rbx
	ret
  .short:
	mov	edi, r12d
	call	sys_exit_group

; ct_ntok -> rax = tokens from the last ct_run.
  ct_ntok:
	lea	rax, [ct_toks]
	mov	rax, [rax + Vec.len]
	ret

; ct_tok(rdi = index) -> rax = that `Tok`.
  ct_tok:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [ct_toks]
	call	vec_get
	pop	rbx
	ret

; ct_ndiag -> rax = diagnostics from the last ct_run (lexer's and parser's,
; in one vector -- they share the `Lexer`, which is the point).
  ct_ndiag:
	lea	rax, [ct_diags]
	mov	rax, [rax + Vec.len]
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
  ct_path:	db 'fixture.exsc'
  ct_path_end:
  CT_PATH_LEN = ct_path_end - ct_path
  CT_BUF = 4 shl 10

  case_01:	db	'publica functio saluta() -> textus { redde "x" }'
  case_01_end:
  case_01_LEN = case_01_end - case_01
  case_02:	db	'publica functio f('
  case_02_end:
  case_02_LEN = case_02_end - case_02
  case_03:	db	'firma x = 1'
  case_03_end:
  case_03_LEN = case_03_end - case_03
  case_04:	db	'@transitus publica structura M { campus: u4:maior }'
  case_04_end:
  case_04_LEN = case_04_end - case_04
  case_05:	db	'firma x: u12 = 1;'
  case_05_end:
  case_05_LEN = case_05_end - case_05
  case_06:	db	'publica functio f() { firma forma = 1; }'
  case_06_end:
  case_06_LEN = case_06_end - case_06
  case_07:	db	'publica functio f() { firma c = a < b; }'
  case_07_end:
  case_07_LEN = case_07_end - case_07
  case_08:	db	'} publica functio f() { }'
  case_08_end:
  case_08_LEN = case_08_end - case_08
  case_09:	db	'firma s = "abc'
  case_09_end:
  case_09_LEN = case_09_end - case_09
  case_10:
  case_10_end:
  case_10_LEN = case_10_end - case_10
  case_11:	db	'redde 1;'
  case_11_end:
  case_11_LEN = case_11_end - case_11
  case_12:	db	'publica functio f() { functio g() { } }'
  case_12_end:
  case_12_LEN = case_12_end - case_12
  case_13:	db	'publica functio f() { firma h = functio(a: f32) poscit rete { redde a; }; }'
  case_13_end:
  case_13_LEN = case_13_end - case_13
  case_14:	db	'publica functio f() { per i in 0..n contrahe s: + forma arborea 8 { } }'
  case_14_end:
  case_14_LEN = case_14_end - case_14
  case_15:	db	'publica functio f() { quisque i in a contrahe s: * forma ordinata { } }'
  case_15_end:
  case_15_LEN = case_15_end - case_15

  ; §14 entry 22 ITSELF, read from tests/conformance/ at assembly time rather
  ; than paraphrased -- the paraphrase is case 4 above, and this is the file
  ; the suite actually runs. Its directive says
  ; `expect-code=EXS-E0201 status=deferred needs=parser`, and this row is the
  ; evidence that the parser it was waiting for produces exactly that code on
  ; exactly those bytes. What is still missing is the WIRING: `exsc` does not
  ; call cst_parse yet (compiler/x86_64/exsc.asm includes lexer/lexer.inc, not
  ; cst/cst.inc), so the entry cannot be flipped from this file's success
  ; alone. REPORTED, not flipped -- tests/conformance/ and tests/run.sh belong
  ; to another agent.
  case_e22:
	file '../conformance/entry22_subbyte_byte_order_maior.exsc'
  case_e22_end:
  case_e22_LEN = case_e22_end - case_e22

  case_tab:
	; the missing `;` of §8.6 decision 1: EXS-E0201 at the token that was found
	; instead, with DIAG_FIX_INSERT carrying the `;`
	dq	case_01, case_01_LEN
	dd	1, 201, 47, 1, 2, 1
	; end of input with `(` still open: EXS-E0202, not E0203 (§8.6,
	; "Recovery"). It still carries the insertion fix: the token that is
	; missing is known even though the file ended
	dq	case_02, case_02_LEN
	dd	1, 202, 18, 0, 2, 1
	; end of input with nothing open: EXS-E0203. The pair with the case above
	; is the whole of the E0202/E0203 distinction
	dq	case_03, case_03_LEN
	dd	1, 203, 11, 0, 2, 1
	; §14 entry 22 verbatim: `u4:maior`, a byte order on a sub-byte field,
	; EXS-E0201 spanning the suffix (§5.2 rule 3)
	dq	case_04, case_04_LEN
	dd	1, 201, 43, 6, 0, 0
	; §5.2 rule 2 made literal: `u12` is a BitType shape with an inadmissible
	; width, so it does not parse
	dq	case_05, case_05_LEN
	dd	1, 201, 9, 3, 0, 0
	; a reserved word in identifier position: EXS-E0220 with the lexer's own
	; DIAG_FIX_REPLACE, raised through lex_kw_as_ident so there is exactly one
	; E0220 in the compiler
	dq	case_06, case_06_LEN
	dd	1, 220, 28, 5, 1, 0
	; §8.6 decision 6: `<` opens generic arguments and never compares, so an
	; unclosed one is EXS-E0201 AT THE `<` with the edit to `lt` attached
	dq	case_07, case_07_LEN
	dd	1, 201, 34, 1, 1, 1
	; a stray `}` before anything opened it: reported once, then skipped into
	; an ERROR node so the rest of the file still parses
	dq	case_08, case_08_LEN
	dd	1, 201, 0, 1, 0, 1
	; an unterminated literal. The FIRST diagnostic is the lexer's EXS-E0202;
	; the parser then adds its own for the input that ends mid-binding, and
	; both are in one vector because both go through the same `Lexer`
	dq	case_09, case_09_LEN
	dd	2, 202, 10, 4, 0, 1
	; the empty file is a valid `Module ::= Item* EOF` with no items
	dq	case_10, case_10_LEN
	dd	0, 0, 0, 0, 0, 0
	; a statement where a module item belongs
	dq	case_11, case_11_LEN
	dd	1, 201, 0, 5, 0, 1
	; §8.6's `functio IDENT` peek. NO FIX PAYLOAD: the edit §8.6 asks for is
	; three insertions and diag/fix.inc carries one -- see cst/parse.inc's
	; header, finding 3
	dq	case_12, case_12_LEN
	dd	1, 201, 22, 7, 0, 0
	; §8.6 decision 2: a lambda takes no `poscit`. The row is kept, inside a
	; CST_ERROR, so the file still round-trips
	dq	case_13, case_13_LEN
	dd	1, 201, 48, 6, 0, 1
	; §8.6's `forma IDENT [INT]`, amended one day after the section landed
	; because §5.4's `arborea w` could not be lowered without the width. Zero
	; diagnostics: the width is OPTIONAL in the grammar and the
	; arborea/ordinata pairing is a checker rule, because deciding a production
	; on a CONTEXTUAL identifier is what §8.6's LL(1) constraint forbids
	dq	case_14, case_14_LEN
	dd	0, 0, 0, 0, 0, 0
	; the same reduction with no width at all, which must parse just as cleanly
	; -- that is what makes the row above a test of the grammar and not of the
	; lexer
	dq	case_15, case_15_LEN
	dd	0, 0, 0, 0, 0, 0
	; the conformance entry itself: one diagnostic, EXS-E0201, at the
	; `:maior` that §5.2 rule 3 forbids on a sub-byte field. The span start is
	; a byte offset into that file, so it moves whenever the file's comment
	; header changes length: 2124 until 2f21338 rewrote the header, 2503 since.
	dq	case_e22, case_e22_LEN
	dd	1, 201, 2503, 6, 0, 0
  case_end:
  CASE_N = (case_end - case_tab) / CT_ROW
  assert CASE_N = 16

segment readable writeable
  ct_arena	rb sizeof.Arena
  ct_iarena	rb sizeof.Arena
  ct_intern	rb sizeof.Interner
  ct_toks	rb sizeof.Vec
  ct_diags	rb sizeof.Vec
  ct_lx		rb sizeof.Lexer
  ct_green	rb sizeof.Vec
  ct_work	rb sizeof.Vec
  ct_map	rb sizeof.Map
  ct_tree	rb sizeof.CstTree
  ct_p		rb sizeof.CstParser
  ct_out	rb sizeof.DiagOut
  ct_buf	rb CT_BUF
