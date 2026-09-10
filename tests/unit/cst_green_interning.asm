; compiler/x86_64/rt/span.inc
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
; INTERNING AND DETERMINISM. cst.md: "Two subtrees with identical shape and
; text share storage." That is the claim the green/red split exists to make
; true, and it is invisible from the outside -- a tree that interned nothing
; parses the same files, round-trips the same bytes and reports the same
; diagnostics. It has to be checked directly.
;
; THE POSITIVE AND THE NEGATIVE CASE ARE BOTH HERE, because only the pair
; means anything. Sample A declares the same function twice: two
; `FUNCTION_DECL` occurrences in the tree, ONE `CstGreen` record behind them.
; Sample B changes one letter of the second name: two occurrences, TWO records.
; A parser that never interned would fail A; one that wrongly interned
; everything of a kind would fail B.
;
; THE CHILD-ORDER INVARIANT. Every child id is strictly less than its parent's.
; That is what makes the green array a topological order of the tree, and it is
; the mechanical reason ids can be assigned in creation order without ever
; needing a fixup pass -- a child is finished before the node that holds it.
;
; DETERMINISM (§9.3, CLAUDE.md "Determinism is not optional"). The same source
; parsed twice, with fresh green storage and a fresh interning map both times,
; must produce byte-identical green records. Only the first 24 bytes of each
; record are compared -- kind, child count, text length, text id -- and NOT the
; `kids` pointer, deliberately: the arena is reset between the runs so the
; pointers would match too, and a fixture that compared them would be asserting
; that an ADDRESS is reproducible, which is exactly the property CLAUDE.md says
; nothing may depend on. What must be reproducible is the structure, and that
; is what is compared.
;
; Exit 0 = all held. 11 = wrong number of occurrences in the tree, 12 = wrong
; number of green records, 13 = a child id was not less than its parent's,
; 14 = the two runs produced different record counts, 15 = ... different
; records, 16 = the snapshot buffer was too small, 99 = setup failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/cst/cst.inc'

CT_ARENA = 4 shl 20

segment readable executable
CT_SNAP = 64 shl 10

  start:
	call	ct_setup

	; ---- sample A: the same declaration twice --------------------------
	lea	rdi, [sample_a]
	mov	rsi, sample_a_LEN
	call	ct_run
	mov	qword [ct_seen], 0
	lea	rdi, [ct_root]
	lea	rsi, [ct_tree]
	call	cst_red_root
	lea	rdi, [ct_root]
	call	ct_count
	cmp	qword [ct_seen], 2
	jne	.fail11
	call	ct_green_fns
	cmp	rax, 1
	jne	.fail12
	call	ct_order
	call	ct_snapshot

	; ---- determinism: the same bytes again -----------------------------
	lea	rdi, [sample_a]
	mov	rsi, sample_a_LEN
	call	ct_run
	call	ct_compare

	; ---- sample B: one letter different --------------------------------
	lea	rdi, [sample_b]
	mov	rsi, sample_b_LEN
	call	ct_run
	mov	qword [ct_seen], 0
	lea	rdi, [ct_root]
	lea	rsi, [ct_tree]
	call	cst_red_root
	lea	rdi, [ct_root]
	call	ct_count
	cmp	qword [ct_seen], 2
	jne	.fail11
	call	ct_green_fns
	cmp	rax, 2
	jne	.fail12
	call	ct_order

	mov	edi, 0
	call	sys_exit_group
  .fail11:
	mov	edi, 11
	call	sys_exit_group
  .fail12:
	mov	edi, 12
	call	sys_exit_group

; ct_count(rdi = a `CstRed`) -- counts `FUNCTION_DECL` OCCURRENCES in the red
; tree, which is the number of places the construct appears in the file --
; sharing is invisible here by construction, and that is the point.
  ct_count:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	sub	rsp, 48
	mov	rbx, rdi
	lea	rdi, [ct_tree]
	mov	rsi, rbx
	call	cst_red_kind
	cmp	eax, CST_FUNCTION_DECL
	jne	.kids
	inc	qword [ct_seen]
  .kids:
	lea	rdi, [ct_tree]
	mov	rsi, rbx
	call	cst_red_nkid
	mov	r12, rax
	xor	r13, r13
  .kid:
	cmp	r13, r12
	jae	.out
	lea	rdi, [ct_tree]
	mov	rsi, rbx
	mov	rdx, r13
	mov	rcx, rsp
	call	cst_red_child
	mov	rdi, rsp
	call	ct_count
	inc	r13
	jmp	.kid
  .out:
	add	rsp, 48
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	ret

; ct_green_fns -> rax = how many `CstGreen` RECORDS have kind FUNCTION_DECL.
; This is storage, not occurrences.
  ct_green_fns:
	push	rbx
	push	r12
	push	r13
	xor	r12, r12
	xor	r13, r13
  .one:
	lea	rax, [ct_green]
	cmp	r12, [rax + Vec.len]
	jae	.done
	lea	rdi, [ct_green]
	mov	rsi, r12
	call	vec_get
	cmp	dword [rax + CstGreen.kind], CST_FUNCTION_DECL
	jne	.next
	inc	r13
  .next:
	inc	r12
	jmp	.one
  .done:
	mov	rax, r13
	pop	r13
	pop	r12
	pop	rbx
	ret

; ct_order -- every child id is strictly less than its parent's.
  ct_order:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	xor	r12, r12
  .rec:
	lea	rax, [ct_green]
	cmp	r12, [rax + Vec.len]
	jae	.done
	lea	rdi, [ct_green]
	mov	rsi, r12
	call	vec_get
	mov	r13d, [rax + CstGreen.nkid]
	cmp	r13d, CST_GREEN_LEAF
	je	.next
	mov	r14, [rax + CstGreen.kids]
	xor	r15, r15
  .kid:
	cmp	r15, r13
	jae	.next
	mov	eax, [r14 + r15*4]
	lea	rcx, [r12 + 1]			; this record's own 1-based id
	cmp	rax, rcx
	jae	.bad
	inc	r15
	jmp	.kid
  .next:
	inc	r12
	jmp	.rec
  .done:
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	ret
  .bad:
	mov	edi, 13
	call	sys_exit_group

; ct_snapshot -- copy the position-free half of every green record aside.
  ct_snapshot:
	push	rbx
	push	r12
	push	r13
	lea	rax, [ct_green]
	mov	r12, [rax + Vec.len]
	mov	[ct_snapn], r12
	mov	rax, r12
	imul	rax, 24
	cmp	rax, CT_SNAP
	ja	.big
	xor	r13, r13
  .one:
	cmp	r13, r12
	jae	.done
	lea	rdi, [ct_green]
	mov	rsi, r13
	call	vec_get
	mov	rsi, rax
	lea	rdi, [ct_snap]
	mov	rax, r13
	imul	rax, 24
	add	rdi, rax
	mov	rcx, 24
	cld
	rep	movsb
	inc	r13
	jmp	.one
  .done:
	pop	r13
	pop	r12
	pop	rbx
	ret
  .big:
	mov	edi, 16
	call	sys_exit_group

; ct_compare -- the current tree's records against the snapshot.
  ct_compare:
	push	rbx
	push	r12
	push	r13
	lea	rax, [ct_green]
	mov	r12, [rax + Vec.len]
	cmp	r12, [ct_snapn]
	jne	.badn
	xor	r13, r13
  .one:
	cmp	r13, r12
	jae	.done
	lea	rdi, [ct_green]
	mov	rsi, r13
	call	vec_get
	mov	rsi, rax
	lea	rdi, [ct_snap]
	mov	rax, r13
	imul	rax, 24
	add	rdi, rax
	mov	rcx, 24
	cld
	repe	cmpsb
	jne	.badb
	inc	r13
	jmp	.one
  .done:
	pop	r13
	pop	r12
	pop	rbx
	ret
  .badn:
	mov	edi, 14
	call	sys_exit_group
  .badb:
	mov	edi, 15
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

  ; Two declarations that are identical from `functio` onward -- including the
  ; single space in front of it, which is that token's leading trivia and so is
  ; INSIDE the FunctionDecl. Nothing else about the file matters.
  sample_a:
	db	'publica functio f() { redde 1; }', 0x0A
	db	'publica functio f() { redde 1; }', 0x0A
  sample_a_end:
  sample_a_LEN = sample_a_end - sample_a

  ; The same file with one letter changed. The two declarations are now
  ; different text, so they are different green nodes -- which is what makes
  ; the sample above evidence rather than a coincidence.
  sample_b:
	db	'publica functio f() { redde 1; }', 0x0A
	db	'publica functio g() { redde 1; }', 0x0A
  sample_b_end:
  sample_b_LEN = sample_b_end - sample_b

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
  ct_seen	rq 1
  ct_snapn	rq 1
  ct_snap	rb CT_SNAP
