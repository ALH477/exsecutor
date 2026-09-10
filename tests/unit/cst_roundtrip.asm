; tests/unit/cst_roundtrip.asm
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
; THE ROUND TRIP: concatenating every leaf of the green tree reproduces the
; source BYTE FOR BYTE. §9.1 requires the CST to be lossless and this is the
; only honest test of that claim -- a tree that quietly dropped a comment, a
; blank line, or a byte the lexer refused to tokenize would pass every
; structural assertion and fail here.
;
; MALFORMED INPUT IS HALF THE POINT. Cases 3 onward are truncated, mis-spelled
; or outright wrong, and every one of them must still round-trip: §8.6's
; recovery rules say skipped tokens go into an `ERROR` node and absent tokens
; become zero-width `MISSING` nodes, so nothing may be dropped on the error
; path either. Case 11 is the unterminated string literal, which is the one
; case where the LEXER consumes bytes and emits no token at all -- those bytes
; reach the tree only because cst/parse.inc reconstructs trivia from the gaps
; between token spans rather than from a token stream that does not contain
; them.
;
; Cases 1 and 2 are `examples/saluta.exsc` and `examples/imprime.exsc`, read
; from the tree at assembly time with fasmg's `file` directive rather than
; copied in -- so this fixture cannot drift from the programs it claims to
; parse. saluta.exsc is the canonical program; if the CST cannot reproduce it
; exactly, nothing built on the CST is worth running.
;
; THE LAST SIX CASES ARE PATHOLOGICAL, and they are here for a failure mode a
; round-trip check would otherwise miss entirely: a recovery loop that makes no
; progress does not produce a wrong tree, it produces no tree, forever. Every
; loop in cst/parse.inc consumes at least one token per iteration or exits, and
; these are the inputs that test the claim -- six openers with no closers, six
; closers with nothing open, five nested generic-argument lists left dangling.
; A regression here hangs the suite rather than failing it, which is a visible
; result either way.
;
; Exit 0 = every case round-tripped. 20+N = case N (0-based) came back
; different; 90 = the 128 KB writer filled up, which no sample here should do;
; 91 = a parse produced no root at all; 99 = arena setup failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/cst/cst.inc'

CT_ARENA = 4 shl 20

segment readable executable
  start:
	call	ct_setup
	xor	rbx, rbx
  .case:
	cmp	rbx, CASE_N
	jae	.done
	lea	rax, [case_tab]
	mov	rcx, rbx
	shl	rcx, 4
	add	rax, rcx
	mov	r12, [rax]			; the source bytes
	mov	r13, [rax + 8]			; and their length
	mov	rdi, r12
	mov	rsi, r13
	call	ct_run

	; A tree exists on BOTH paths. §9.1's CST is error-tolerant, so a
	; diagnosed parse still has a root, and a fixture that only checked the
	; clean cases would not be testing error tolerance at all.
	lea	rax, [ct_tree]
	cmp	dword [rax + CstTree.root], 0
	je	.noroot

	mov	edi, 90
	call	ct_text				; rax = bytes written
	cmp	rax, r13
	jne	.bad
	lea	rdi, [ct_buf]
	mov	rsi, r12
	mov	rcx, r13
	cld
	test	rcx, rcx
	jz	.next
	repe	cmpsb
	jne	.bad
  .next:
	inc	rbx
	jmp	.case
  .done:
	mov	edi, 0
	call	sys_exit_group
  .bad:
	lea	edi, [rbx + 20]
	call	sys_exit_group
  .noroot:
	mov	edi, 91
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
  CT_BUF = 128 shl 10

  ; THE canonical program and its companion, read from the tree rather than
  ; copied -- see this file's header.
  sample_a:
	file '../../examples/saluta.exsc'
  sample_a_end:
  sample_a_LEN = sample_a_end - sample_a
  sample_b:
	file '../../examples/imprime.exsc'
  sample_b_end:
  sample_b_LEN = sample_b_end - sample_b

  sample_03:	db	'publica functio saluta() -> textus { redde "x" }'
  sample_03_end:
  sample_03_LEN = sample_03_end - sample_03
  sample_04:	db	'publica functio f('
  sample_04_end:
  sample_04_LEN = sample_04_end - sample_04
  sample_05:	db	'firma x = 1'
  sample_05_end:
  sample_05_LEN = sample_05_end - sample_05
  sample_06:	db	'@transitus publica structura M { campus: u4:maior }'
  sample_06_end:
  sample_06_LEN = sample_06_end - sample_06
  sample_07:	db	'firma x: u12 = 1;'
  sample_07_end:
  sample_07_LEN = sample_07_end - sample_07
  sample_08:	db	'publica functio f() { firma forma = 1; }'
  sample_08_end:
  sample_08_LEN = sample_08_end - sample_08
  sample_09:	db	'publica functio f() { firma c = a < b; }'
  sample_09_end:
  sample_09_LEN = sample_09_end - sample_09
  sample_10:	db	'} publica functio f() { }'
  sample_10_end:
  sample_10_LEN = sample_10_end - sample_10
  sample_11:	db	'firma s = "abc'
  sample_11_end:
  sample_11_LEN = sample_11_end - sample_11
  sample_12:
  sample_12_end:
  sample_12_LEN = sample_12_end - sample_12
  sample_13:	db	'redde 1;'
  sample_13_end:
  sample_13_LEN = sample_13_end - sample_13
  sample_14:	db	'publica functio f() { , ; }'
  sample_14_end:
  sample_14_LEN = sample_14_end - sample_14
  sample_15:	db	'publica functio f() { functio g() { } }'
  sample_15_end:
  sample_15_LEN = sample_15_end - sample_15
  sample_16:	db	'publica functio f() { firma h = functio(a: f32) poscit rete { redde a; }; }'
  sample_16_end:
  sample_16_LEN = sample_16_end - sample_16
  sample_17:	db	'@a @b'
  sample_17_end:
  sample_17_LEN = sample_17_end - sample_17
  sample_18:	db	'publica structura S { a: u4 b: u8 c: u9 }'
  sample_18_end:
  sample_18_LEN = sample_18_end - sample_18
  sample_19:	db	'publica interfacies X { functio'
  sample_19_end:
  sample_19_LEN = sample_19_end - sample_19
  sample_20:	db	'publica functio f() -> acies<f32, { }'
  sample_20_end:
  sample_20_LEN = sample_20_end - sample_20
  sample_21:	db	'{{{{{{'
  sample_21_end:
  sample_21_LEN = sample_21_end - sample_21
  sample_22:	db	'}}}}}}'
  sample_22_end:
  sample_22_LEN = sample_22_end - sample_22
  sample_23:	db	'publica functio f((((('
  sample_23_end:
  sample_23_LEN = sample_23_end - sample_23
  sample_24:	db	'publica functio f() -> a<b<c<d<e<'
  sample_24_end:
  sample_24_LEN = sample_24_end - sample_24
  sample_25:	db	',,,,,,'
  sample_25_end:
  sample_25_LEN = sample_25_end - sample_25
  sample_26:	db	'publica functio f() { x = y = z; }'
  sample_26_end:
  sample_26_LEN = sample_26_end - sample_26

  case_tab:
	dq	sample_a, sample_a_LEN	; examples/saluta.exsc
	dq	sample_b, sample_b_LEN	; examples/imprime.exsc
	dq	sample_03, sample_03_LEN	; the `;` §8.6 decision 1 made mandatory, missing
	dq	sample_04, sample_04_LEN	; truncated mid-parameter-list -- a bracket open at end of input
	dq	sample_05, sample_05_LEN	; truncated with nothing open
	dq	sample_06, sample_06_LEN	; §14 entry 22, verbatim
	dq	sample_07, sample_07_LEN	; §5.2 rule 2: `u12` does not parse
	dq	sample_08, sample_08_LEN	; a reserved word as a name
	dq	sample_09, sample_09_LEN	; §8.6 decision 6: `<` is never a comparison
	dq	sample_10, sample_10_LEN	; a stray closing brace before anything opened
	dq	sample_11, sample_11_LEN	; an unterminated literal -- the lexer consumes it and emits no token
	dq	sample_12, sample_12_LEN	; the empty file
	dq	sample_13, sample_13_LEN	; a statement where an item belongs
	dq	sample_14, sample_14_LEN	; two tokens that begin nothing
	dq	sample_15, sample_15_LEN	; §8.6's `functio IDENT` peek
	dq	sample_16, sample_16_LEN	; a `poscit` on a lambda
	dq	sample_17, sample_17_LEN	; annotations with no item after them
	dq	sample_18, sample_18_LEN	; a separator-free body with one inadmissible width
	dq	sample_19, sample_19_LEN	; truncated inside a separator-free body
	dq	sample_20, sample_20_LEN	; a generic argument list that never closes
	dq	sample_21, sample_21_LEN	; nothing but openers -- every one of them at module level, where no production opened it
	dq	sample_22, sample_22_LEN	; nothing but closers
	dq	sample_23, sample_23_LEN	; a parameter list that opens five times and closes none
	dq	sample_24, sample_24_LEN	; generic arguments nested five deep and closed none
	dq	sample_25, sample_25_LEN	; separators with nothing to separate
	dq	sample_26, sample_26_LEN	; §8.6 decision 5: assignment is a statement, so the second `=` is not an operator
  case_end:
  CASE_N = (case_end - case_tab) / 16
  assert CASE_N = 26

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
