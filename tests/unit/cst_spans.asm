; tests/unit/cst_spans.asm
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
; SPAN CORRECTNESS: the red half of §9.1's tree turns position-free green nodes
; into absolute source offsets, and this fixture is the check that it does.
; §8.3: "Every diagnostic carries a source span -- not retrofittable, which is
; why the CST is not optional"; §16 makes diagnostics this stage's kill
; criterion. A tree with the right shape and the wrong offsets underlines the
; wrong text in every diagnostic the compiler will ever emit.
;
; Three checks, and the second is the one that could not be faked.
;
; 1. PARTITION. For every node, the children's ranges tile the parent's
;    exactly: the first child starts where the parent starts, each next child
;    starts where the last one ended, and the last one ends where the parent
;    ends. This is what makes the round trip and the offsets the same fact --
;    a gap or an overlap here IS a lost or duplicated byte there.
;
; 2. AGREEMENT WITH THE LEXER. Every leaf that came from a real token must
;    have exactly the span lexer/lex.inc gave that token, in order, and the
;    counts must match. The two numbers are computed by completely different
;    means -- the lexer walks bytes, cst/red.inc sums interned text lengths
;    down from the root -- so agreeing is evidence and not a tautology. It also
;    proves the trivia reconstruction is exact: if a single whitespace byte
;    were dropped or double-counted, every subsequent leaf would be off by one
;    and this check would say so at the first one.
;
; 3. KNOWN-GOOD OFFSETS, hand-computed. A fixed sample and a table of paths
;    from the root, each with the kind, offset and length it must have. This is
;    the check cst.md asks for by name ("red-node `text_offset` matches
;    known-good offsets for a fixed fixture set"); the other two are
;    invariants, which hold just as well of a tree that is uniformly wrong.
;
; Checks 1 and 2 run over `examples/saluta.exsc` and `examples/imprime.exsc`
; and over four malformed samples -- error recovery must not damage offsets
; either, and `MISSING` leaves in particular are zero-width precisely so they
; cannot.
;
; Exit 0 = every check held. 11 = children do not start where they should,
; 12 = children do not cover the parent, 13 = a leaf disagrees with its token's
; span, 14 = a leaf disagrees with its token's KIND, 15 = leaf and token counts
; differ, 16 = the root is not the whole file, 21 = a known-good kind is wrong,
; 22 = a known-good offset is wrong, 23 = a known-good length is wrong,
; 24 = `cst_red_span` disagrees with `cst_red_child`, 99 = setup failed.
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

	; ---- checks 1 and 2, over every sample ------------------------------
	xor	rbx, rbx
  .case:
	cmp	rbx, CASE_N
	jae	.walked
	lea	rax, [case_tab]
	mov	rcx, rbx
	shl	rcx, 4
	add	rax, rcx
	mov	r12, [rax]
	mov	r13, [rax + 8]
	mov	rdi, r12
	mov	rsi, r13
	call	ct_run

	lea	rdi, [ct_root]
	lea	rsi, [ct_tree]
	call	cst_red_root
	lea	rdi, [ct_tree]
	lea	rsi, [ct_root]
	call	cst_red_len
	cmp	rax, r13
	jne	.fail16
	lea	rax, [ct_root]
	cmp	qword [rax + CstRed.off], 0
	jne	.fail16
	lea	rdi, [ct_tree]
	lea	rsi, [ct_root]
	call	cst_red_kind
	cmp	eax, CST_MODULE
	jne	.fail16

	mov	qword [ct_leaf], 0
	lea	rdi, [ct_root]
	call	ct_walk

	call	ct_ntok
	cmp	rax, [ct_leaf]
	jne	.fail15
	inc	rbx
	jmp	.case
  .walked:

	; ---- check 3, over the one fixed sample -----------------------------
	lea	rdi, [sample_k]
	mov	rsi, sample_k_LEN
	call	ct_run
	xor	rbx, rbx
  .known:
	cmp	rbx, KNOWN_N
	jae	.done
	mov	rax, rbx
	imul	rax, CT_ROW
	lea	r14, [known_tab]
	add	r14, rax

	lea	rdi, [ct_r0]
	lea	rsi, [ct_tree]
	call	cst_red_root
	mov	r12d, [r14]			; path length
	xor	r13, r13
  .step:
	cmp	r13, r12
	jae	.stepped
	lea	rdi, [ct_tree]
	lea	rsi, [ct_r0]
	mov	edx, [r14 + 8 + r13*4]
	lea	rcx, [ct_r1]
	call	cst_red_child
	; copy r1 back into r0 -- `cst_red_child`'s destination may not alias
	; its source, and a walk needs somewhere stable to stand
	lea	rdi, [ct_r0]
	lea	rsi, [ct_r1]
	mov	rcx, sizeof.CstRed
	cld
	rep	movsb
	inc	r13
	jmp	.step
  .stepped:
	lea	rdi, [ct_tree]
	lea	rsi, [ct_r0]
	call	cst_red_kind
	cmp	eax, [r14 + 32]
	jne	.fail21
	lea	rax, [ct_r0]
	mov	rax, [rax + CstRed.off]
	cmp	eax, [r14 + 36]
	jne	.fail22
	lea	rdi, [ct_tree]
	lea	rsi, [ct_r0]
	call	cst_red_len
	cmp	eax, [r14 + 4]
	jne	.fail23

	; `cst_red_span` must say the same thing, through rt/span.inc, and must
	; name the file: a span whose file_id is not the interned path would
	; point a diagnostic at the wrong file in a multi-file compilation.
	lea	rdi, [ct_tree]
	lea	rsi, [ct_r0]
	lea	rax, [ct_lx]
	mov	edx, [rax + Lexer.file_id]
	lea	rcx, [ct_span]
	call	cst_red_span
	lea	rax, [ct_span]
	mov	ecx, [r14 + 36]
	cmp	[rax + Span.start], ecx
	jne	.fail24
	mov	ecx, [r14 + 4]
	cmp	[rax + Span.len], ecx
	jne	.fail24
	lea	rcx, [ct_lx]
	mov	ecx, [rcx + Lexer.file_id]
	cmp	[rax + Span.file_id], ecx
	jne	.fail24

	inc	rbx
	jmp	.known
  .done:
	mov	edi, 0
	call	sys_exit_group
  .fail11:
	mov	edi, 11
	call	sys_exit_group
  .fail12:
	mov	edi, 12
	call	sys_exit_group
  .fail15:
	mov	edi, 15
	call	sys_exit_group
  .fail16:
	mov	edi, 16
	call	sys_exit_group
  .fail21:
	mov	edi, 21
	call	sys_exit_group
  .fail22:
	mov	edi, 22
	call	sys_exit_group
  .fail23:
	mov	edi, 23
	call	sys_exit_group
  .fail24:
	mov	edi, 24
	call	sys_exit_group

; ct_walk(rdi = a `CstRed`) -- checks 1 and 2 over that subtree, recursively.
; Exits the process on any disagreement. The child view lives in this frame's
; own 24 bytes, so the recursion needs no arena and no fixed depth limit.
;
; Five pushes plus 48 bytes leaves `rsp` 16-aligned at every `call` -- at the
; first instruction `rsp` is 8 (mod 16) because `call` pushed the return
; address (docs/asm-conventions.md, "2. Register discipline").
  ct_walk:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	sub	rsp, 48
	mov	rbx, rdi
	lea	rdi, [ct_tree]
	mov	rsi, rbx
	call	cst_red_nkid
	mov	r12, rax
	test	r12, r12
	jz	.leaf

	xor	r13, r13
	mov	r14, [rbx + CstRed.off]		; where the next child must start
  .kid:
	cmp	r13, r12
	jae	.covered
	lea	rdi, [ct_tree]
	mov	rsi, rbx
	mov	rdx, r13
	mov	rcx, rsp
	call	cst_red_child
	mov	rax, [rsp + CstRed.off]
	cmp	rax, r14
	jne	.bad_start
	mov	rdi, rsp
	call	ct_walk
	lea	rdi, [ct_tree]
	mov	rsi, rsp
	call	cst_red_len
	add	r14, rax
	inc	r13
	jmp	.kid
  .covered:
	lea	rdi, [ct_tree]
	mov	rsi, rbx
	call	cst_red_len
	add	rax, [rbx + CstRed.off]
	cmp	rax, r14
	jne	.bad_cover
	jmp	.out

	; A leaf that came from a real token -- not trivia, not MISSING -- must
	; carry that token's span and that token's kind.
  .leaf:
	lea	rdi, [ct_tree]
	mov	rsi, rbx
	call	cst_red_kind
	cmp	eax, CST_TK_PUNCT
	ja	.out
	mov	r15d, eax
	mov	rdi, [ct_leaf]
	call	ct_tok
	mov	r12, rax
	mov	eax, [r12 + Tok.kind]
	inc	eax
	cmp	eax, r15d
	jne	.bad_kind
	mov	eax, [r12 + Tok.span.start]
	cmp	rax, [rbx + CstRed.off]
	jne	.bad_span
	lea	rdi, [ct_tree]
	mov	rsi, rbx
	call	cst_red_len
	mov	ecx, [r12 + Tok.span.len]
	cmp	rax, rcx
	jne	.bad_span
	inc	qword [ct_leaf]
  .out:
	add	rsp, 48
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	ret
  .bad_start:
	mov	edi, 11
	call	sys_exit_group
  .bad_cover:
	mov	edi, 12
	call	sys_exit_group
  .bad_span:
	mov	edi, 13
	call	sys_exit_group
  .bad_kind:
	mov	edi, 14
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

  sample_a:
	file '../../examples/saluta.exsc'
  sample_a_end:
  sample_a_LEN = sample_a_end - sample_a
  sample_b:
	file '../../examples/imprime.exsc'
  sample_b_end:
  sample_b_LEN = sample_b_end - sample_b

  sample_c:	db	'publica functio f('
  sample_c_end:
  sample_c_LEN = sample_c_end - sample_c
  sample_d:	db	'} publica functio f() { redde 1 }'
  sample_d_end:
  sample_d_LEN = sample_d_end - sample_d
  sample_e:	db	'firma s = "abc'
  sample_e_end:
  sample_e_LEN = sample_e_end - sample_e
  sample_f:	db	'publica structura S { a: u4 b: u9 }'
  sample_f_end:
  sample_f_LEN = sample_f_end - sample_f
  ; The fixed sample of check 3. Every offset in `known_tab` was
  ; computed by hand from these 38 bytes, not read back off a run.
  sample_k:	db	'publica functio f() -> u8 { redde 1; }'
  sample_k_end:
  sample_k_LEN = sample_k_end - sample_k
  assert sample_k_LEN = 38

  case_tab:
	dq	sample_a, sample_a_LEN	; examples/saluta.exsc
	dq	sample_b, sample_b_LEN	; examples/imprime.exsc
	dq	sample_c, sample_c_LEN	; truncated
	dq	sample_d, sample_d_LEN	; a stray `}` and a missing `;`
	dq	sample_e, sample_e_LEN	; bytes the lexer consumed and never tokenized
	dq	sample_f, sample_f_LEN	; an inadmissible width mid-body
	dq	sample_k, sample_k_LEN	; the fixed sample, walked too
  case_end:
  CASE_N = (case_end - case_tab) / 16

  ; dd path_len, dd expected_len, dd path[6], dd kind, dd offset
  known_tab:
	; the whole file
	dd	0, 38
	dd	0, 0, 0, 0, 0, 0
	dd	CST_MODULE, 0
	; the one item covers it all
	dd	1, 38
	dd	0, 0, 0, 0, 0, 0
	dd	CST_ITEM, 0
	; starts at the space before `functio` -- leading trivia belongs to the token that follows it
	dd	2, 31
	dd	0, 1, 0, 0, 0, 0
	dd	CST_FUNCTION_DECL, 7
	; ` functio f() -> u8`
	dd	3, 18
	dd	0, 1, 0, 0, 0, 0
	dd	CST_SIGNATURE, 7
	; ` u8`
	dd	4, 3
	dd	0, 1, 0, 7, 0, 0
	dd	CST_TYPE, 22
	; ` u8` -- a BitType and not a Path (§8.6, "Types"), and it is TYPE's only child: the space before `u8` is that token's leading trivia, so it is inside the BitType and not a sibling of it
	dd	5, 3
	dd	0, 1, 0, 7, 0, 0
	dd	CST_BIT_TYPE, 22
	; ` { redde 1; }`
	dd	3, 13
	dd	0, 1, 1, 0, 0, 0
	dd	CST_BLOCK, 25
	; ` redde 1;`
	dd	4, 9
	dd	0, 1, 1, 2, 0, 0
	dd	CST_JUMP_STMT, 27
	; ` 1`
	dd	5, 2
	dd	0, 1, 1, 2, 2, 0
	dd	CST_LITERAL, 33
	; the `;` §8.6 decision 1 requires
	dd	5, 1
	dd	0, 1, 1, 2, 3, 0
	dd	CST_TK_PUNCT, 35
	; zero-width, at end of input
	dd	1, 0
	dd	1, 0, 0, 0, 0, 0
	dd	CST_TK_EOF, 38
  known_end:
  KNOWN_N = (known_end - known_tab) / CT_ROW
  assert KNOWN_N = 11

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
  ct_root	rb sizeof.CstRed
  ct_r0		rb sizeof.CstRed
  ct_r1		rb sizeof.CstRed
  ct_span	rb sizeof.Span
  ct_leaf	rq 1
