; tests/unit/cst_related_span.asm
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
; THE RELATED SPAN, AND THE TWO RECOVERY RULES THAT STOPPED CASCADING.
;
; `docs/design/diagnostics-review.md` D6: "`EXS-E0202` never names the
; construct it thinks is unterminated ... this is the case where a second span
; matters most. spec §8.6 *Recovery* does not require an opener span, so this
; is a quality gap rather than a violation -- but the parser knows the opener,
; since it is what the recovery rule *no production consumes a `}` it did not
; open* is written around."
;
; `diag_rel_set` has existed since the diagnostic record grew the field, and
; nothing called it, because the opener's POSITION is knowledge only a parser
; holds. This fixture pins the three things that changed in cst/parse.inc:
;
;   1. THE RELATED SPAN ITSELF. Rows 1-3 assert `DIAG_F_REL` is set and that
;      `rel.start` is the exact byte offset of the opening delimiter -- not
;      merely "some span". A parser that set the flag and left the offset at 0
;      passes a flag check and fails this one.
;
;   2. `}` IS NOT CONSUMED BY A PRODUCTION THAT DID NOT OPEN ONE. Row 4 is the
;      review's c05 shape: a reserved word used as a binding name, which makes
;      the parser invent a `per` statement, whose block ran to the file's real
;      closing brace and ATE it -- after which the function body that actually
;      opened that brace hit end of input and reported `EXS-E0203` in a file
;      whose braces balance. The review calls that diagnostic "bogus"; it was
;      spec §8.6's own recovery rule being broken one production removed. The
;      row forbids code 203 outright rather than pinning a count, because what
;      is being asserted is that a specific WRONG diagnostic is gone.
;
;   3. A MISMATCHED CLOSER IS ABSORBED ONCE, NOT REPORTED TWICE. Row 3 is
;      `a[0)` -- the review's c09, "2 diagnostics for 1 typo". The `)` closes
;      nothing that is open, so no enclosing production wants it; taking it
;      here as the `]` that is missing costs one diagnostic instead of two.
;      The row pins the count at 1.
;
; EVERY ROW ALSO ROUND-TRIPS. Both new recovery paths CONSTRUCT TREE NODES --
; a zero-width `CST_MISSING` for the brace a synthetic block never opened, and
; a `CST_ERROR` holding the absorbed closer -- and §9.1's losslessness claim is
; exactly the thing a new node placed in the wrong arm breaks. Concatenating
; the green tree's leaves must still reproduce the input byte for byte, so a
; recovery rule that swallowed the stray `)` instead of keeping it fails here
; rather than in a formatter three stages later.
;
; Exit 0 = every expectation held. 11 = wrong diagnostic count, 12 = wrong
; first code, 13 = related-span flag wrong, 14 = related-span offset wrong,
; 15 = a forbidden code was emitted, 16 = the round trip lost or gained bytes,
; 17 = no root at all, 99 = setup failed.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3, §8.6, §9.1.
; -----------------------------------------------------------------------------

; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/cst/cst.inc'

CRS_ARENA = 4 shl 20

segment readable executable
CRS_ROW = 40

  start:
	call	crs_setup
	xor	rbx, rbx
  .case:
	cmp	rbx, CRS_N
	jae	.done
	mov	rax, rbx
	imul	rax, CRS_ROW
	lea	r14, [crs_tab]
	add	r14, rax
	mov	rdi, [r14]
	mov	rsi, [r14 + 8]
	call	crs_run

	lea	rax, [crs_tree]
	cmp	dword [rax + CstTree.root], 0
	je	.fail17

	; ---- the round trip, on every row ----------------------------------
	mov	edi, 16
	call	crs_text
	cmp	rax, [r14 + 8]
	jne	.fail16
	mov	rdi, [r14]
	lea	rsi, [crs_buf]
	mov	rdx, [r14 + 8]
	call	crs_memeq
	test	eax, eax
	jz	.fail16

	call	crs_ndiag
	cmp	eax, [r14 + 16]
	jne	.fail11

	; ---- no forbidden code anywhere in the record ----------------------
	mov	r13d, [r14 + 32]
	test	r13d, r13d
	jz	.nofor
	xor	r12, r12
  .scanf:
	call	crs_ndiag
	cmp	r12, rax
	jae	.nofor
	mov	rdi, r12
	call	crs_diag
	cmp	[rax + Diag.code_num], r13d
	je	.fail15
	inc	r12
	jmp	.scanf
  .nofor:

	call	crs_ndiag
	test	eax, eax
	jz	.next

	xor	rdi, rdi
	call	crs_diag
	mov	r12, rax
	mov	ecx, [r14 + 20]
	cmp	[r12 + Diag.code_num], ecx
	jne	.fail12

	; ---- the related span ----------------------------------------------
	xor	eax, eax
	test	dword [r12 + Diag.flags], DIAG_F_REL
	jz	.relq
	mov	eax, 1
  .relq:
	cmp	eax, [r14 + 24]
	jne	.fail13
	test	eax, eax
	jz	.next
	mov	ecx, [r14 + 28]
	cmp	[r12 + Diag.rel.start], ecx
	jne	.fail14

  .next:
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
; 16-aligned at the `call`s inside it.

  crs_setup:
	push	rbx
	lea	rdi, [crs_arena]
	mov	rsi, CRS_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [crs_iarena]
	mov	rsi, CRS_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [crs_intern]
	lea	rsi, [crs_iarena]
	mov	rdx, 1024
	call	intern_init
	pop	rbx
	ret
  .boom:
	mov	edi, 99
	call	sys_exit_group

; crs_run(rdi = source bytes, rsi = byte length)
  crs_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	lea	rdi, [crs_arena]
	call	arena_reset
	lea	rdi, [crs_toks]
	lea	rsi, [crs_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 32
	call	vec_init
	lea	rdi, [crs_diags]
	lea	rsi, [crs_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [crs_lx]
	lea	rsi, [crs_arena]
	lea	rdx, [crs_intern]
	lea	rcx, [crs_toks]
	lea	r8,  [crs_diags]
	call	lex_init
	lea	rdi, [crs_lx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [crs_path]
	mov	r8d, CRS_PATH_LEN
	call	lex_set_source
	lea	rdi, [crs_lx]
	call	lex_source_check
	lea	rdi, [crs_lx]
	call	lex_tokenize
	lea	rdi, [crs_green]
	lea	rsi, [crs_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 64
	call	vec_init
	lea	rdi, [crs_work]
	lea	rsi, [crs_arena]
	mov	rdx, 4
	mov	rcx, 64
	call	vec_init
	lea	rdi, [crs_map]
	lea	rsi, [crs_arena]
	mov	rdx, 256
	call	map_init
	lea	rdi, [crs_tree]
	lea	rsi, [crs_arena]
	lea	rdx, [crs_intern]
	lea	rcx, [crs_green]
	lea	r8,  [crs_work]
	lea	r9,  [crs_map]
	call	cst_tree_init
	lea	rdi, [crs_p]
	lea	rsi, [crs_lx]
	lea	rdx, [crs_tree]
	call	cst_parse
	pop	r13
	pop	r12
	pop	rbx
	ret

; crs_text(rdi = exit code on truncation) -> rax = round-tripped length
  crs_text:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	lea	rdi, [crs_out]
	lea	rsi, [crs_buf]
	mov	rdx, CRS_BUF
	call	diag_out_init
	lea	rdi, [crs_tree]
	lea	rax, [crs_tree]
	mov	esi, [rax + CstTree.root]
	lea	rdx, [crs_out]
	call	cst_text
	test	eax, eax
	jz	.short
	lea	rax, [crs_out]
	mov	rax, [rax + DiagOut.len]
	pop	r13
	pop	r12
	pop	rbx
	ret
  .short:
	mov	edi, r12d
	call	sys_exit_group

; crs_memeq(rdi = a, rsi = b, rdx = n) -> eax = 1 if equal
  crs_memeq:
	push	rbx
	xor	rcx, rcx
  .cmp:
	cmp	rcx, rdx
	jae	.same
	mov	al, [rdi + rcx]
	cmp	al, [rsi + rcx]
	jne	.diff
	inc	rcx
	jmp	.cmp
  .same:
	mov	eax, 1
	pop	rbx
	ret
  .diff:
	xor	eax, eax
	pop	rbx
	ret

  crs_ndiag:
	lea	rax, [crs_diags]
	mov	rax, [rax + Vec.len]
	ret

  crs_diag:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [crs_diags]
	call	vec_get
	pop	rbx
	ret

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

  crs_path:	db 'fixture.exsc'
  crs_path_end:
  CRS_PATH_LEN = crs_path_end - crs_path
  CRS_BUF = 4 shl 10

; Row 1 -- the review's c02 shape. The `si` block closes on the file's only
; `}`, so the delimiter genuinely still open at end of input is the FUNCTION
; body's `{`, at byte 27. rustc names the inner `{` instead, on the strength
; of an indentation heuristic; this names the one the parser is actually
; inside, which is the difference between knowing and guessing.
  crs_c02:	db	'publica functio f() -> u64 { si 1 lt 2 { redde 1; }'
  crs_c02_end:
  CRS_C02_LEN = crs_c02_end - crs_c02

; Row 2 -- the review's c17 shape: a missing `)` inside nesting. The `h(` at
; 38 is closed; the `g(` at 36 is not, and 36 is what the related span must
; name. "no opener span where it matters most" was the review's verdict.
  crs_c17:	db	'publica functio f() -> u64 { redde g(h(1); }'
  crs_c17_end:
  CRS_C17_LEN = crs_c17_end - crs_c17

; Row 3 -- the review's c09: `a[0)`. ONE diagnostic, and the related span
; names the `[` at 36. The `)` is absorbed into a CST_ERROR node, which is
; why the round-trip assertion above is not decorative here.
  crs_c09:	db	'publica functio f() -> u64 { redde a[0); }'
  crs_c09_end:
  CRS_C09_LEN = crs_c09_end - crs_c09

; Row 4 -- the review's c05: `per` (reserved, §8.4) used as a binding name in
; a file whose braces BALANCE. Code 203 must not appear.
  crs_c05:	db	'publica functio f(a: u64) -> u64 { firma per: u64 = a; redde per; }'
  crs_c05_end:
  CRS_C05_LEN = crs_c05_end - crs_c05

; src, len | ndiag, code0, relflag, relstart, forbid, pad
  crs_tab:
	dq	crs_c02, CRS_C02_LEN
	dd	1, 202, 1, 27, 0, 0
	dq	crs_c17, CRS_C17_LEN
	dd	1, 201, 1, 36, 0, 0
	dq	crs_c09, CRS_C09_LEN
	dd	1, 201, 1, 36, 0, 0
	dq	crs_c05, CRS_C05_LEN
	dd	3, 220, 0, 0, 203, 0
  crs_end:
  CRS_N = (crs_end - crs_tab) / CRS_ROW
  assert CRS_N = 4

segment readable writeable
  crs_arena	rb sizeof.Arena
  crs_iarena	rb sizeof.Arena
  crs_intern	rb sizeof.Interner
  crs_toks	rb sizeof.Vec
  crs_diags	rb sizeof.Vec
  crs_lx	rb sizeof.Lexer
  crs_green	rb sizeof.Vec
  crs_work	rb sizeof.Vec
  crs_map	rb sizeof.Map
  crs_tree	rb sizeof.CstTree
  crs_p		rb sizeof.CstParser
  crs_out	rb sizeof.DiagOut
  crs_buf	rb CRS_BUF
