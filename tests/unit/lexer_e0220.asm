; tests/unit/lexer_e0220.asm
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
; `EXS-E0220` -- reserved keyword used as identifier -- and the
; machine-applicable fix §13 says is "mechanically derivable".
;
; §8.4 tier 1 makes a reserved word "never usable as an identifier, anywhere",
; so a reserved word ALWAYS lexes as `TOK_KEYWORD`; E0220 is raised at the
; moment something required an identifier and got one. Almost every such
; position is grammar, and there is no grammar (§15 #9). This fixture therefore
; tests the two things that exist today, which is exactly the honest scope:
;
;   1. The one identifier position a lexer can see with no grammar at all --
;      the name inside `@nomen`, which §8.4's own table lists as a single
;      token. `@forma` is E0220; `@transitus` is not.
;   2. `lex_kw_as_ident` called directly on a `TOK_KEYWORD`, which is how the
;      eventual parser raises it from every other position. Check 3 lexes
;      §8.5's own corrected example, `discerne forma { }` -- the text that
;      section records as the first thing §8.4's reserved set ever caught,
;      "and the first thing it caught was this document" -- finds the `forma`
;      token, and hands it straight to the routine.
;
; THE FIX IS ASSERTED BYTE FOR BYTE, not merely "a fix is present": kind
; `DIAG_FIX_REPLACE`, the edit span equal to the diagnostic's own span, and
; the replacement text `forma_`. §13 does not say what the derived edit is --
; lexer/lex.inc's header records that choosing it was this module's call and
; why `<word>_` is always lexically valid -- so pinning the actual bytes here
; is what makes that choice reviewable instead of implicit.
;
; Check 4 sweeps ALL THIRTY reserved words through the `@nomen` path, building
; `@<word>` at runtime, and requires each to produce E0220 with `<word>_` as
; its fix. A compare chain that handled `forma` and fell through for the other
; twenty-nine would pass checks 1-3 and fail here.
;
; Check 5 asserts `diag_fix_required(220)` is FALSE, which looks backwards and
; is not. §8.3 promises fixes for two named classes -- capability and lexicon
; errors -- and diag/fix.inc's `diag_fixreq_codes` enumerates exactly those.
; E0220 is in neither, so it carries a fix because it CAN, not because it is
; obliged to. Widening that table is diag's call on a spec amendment, and this
; check is here so that a future widening is a deliberate edit to a failing
; fixture rather than a silent divergence.
;
; Exit 0 = all checks passed; 10+N = check N failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/lexer/lexer.inc'

LT_ARENA = 1 shl 20

segment readable executable
  start:
	call	lt_setup

	; ---- check 1: `@forma` is E0220, with the whole fix payload --------
	lea	rdi, [src_forma]
	mov	esi, SRC_FORMA_LEN
	call	lt_run
	cmp	eax, 220
	jne	.fail1
	call	lt_ndiag
	cmp	rax, 1
	jne	.fail1
	xor	rdi, rdi
	call	lt_diag
	mov	rbx, rax
	; the span is the WORD, not the `@` -- the sigil is not the mistake
	cmp	dword [rbx + Diag.span.start], 1
	jne	.fail1
	cmp	dword [rbx + Diag.span.len], 5
	jne	.fail1
	cmp	dword [rbx + Diag.fix.kind], DIAG_FIX_REPLACE
	jne	.fail1
	cmp	dword [rbx + Diag.fix.edit.start], 1
	jne	.fail1
	cmp	dword [rbx + Diag.fix.edit.len], 5
	jne	.fail1
	cmp	qword [rbx + Diag.fix.text_len], 6
	jne	.fail1
	mov	rsi, [rbx + Diag.fix.text_ptr]
	lea	rdi, [want_fix]
	mov	rcx, 6
	repe	cmpsb
	jne	.fail1

	; ---- check 2: an ordinary annotation is not a diagnostic -----------
	lea	rdi, [src_transit]
	mov	esi, SRC_TRANSIT_LEN
	call	lt_run
	test	eax, eax
	jnz	.fail2
	xor	rdi, rdi
	call	lt_tok
	cmp	dword [rax + Tok.kind], TOK_ANNOT
	jne	.fail2
	cmp	dword [rax + Tok.span.start], 0
	jne	.fail2
	cmp	dword [rax + Tok.span.len], 10		; `@transitus`
	jne	.fail2

	; ---- check 3: §8.5's `discerne forma`, through lex_kw_as_ident -----
	lea	rdi, [src_discern]
	mov	esi, SRC_DISCERN_LEN
	call	lt_run
	test	eax, eax
	jnz	.fail3			; the LEXER must not complain: `forma`
					; here is a well-formed keyword token
	mov	rdi, 1
	call	lt_tok			; token 1 is `forma`
	mov	rbx, rax
	cmp	dword [rbx + Tok.kind], TOK_KEYWORD
	jne	.fail3
	cmp	dword [rbx + Tok.aux], KW_FORMA
	jne	.fail3
	; hand it to the routine a parser would call
	lea	rdi, [lt_lx]
	mov	esi, [rbx + Tok.span.start]
	mov	edx, [rbx + Tok.span.len]
	mov	ecx, [rbx + Tok.aux]
	call	lex_kw_as_ident
	mov	rbx, rax
	cmp	dword [rbx + Diag.code_num], 220
	jne	.fail3
	cmp	dword [rbx + Diag.span.start], 9
	jne	.fail3
	cmp	dword [rbx + Diag.span.len], 5
	jne	.fail3
	cmp	qword [rbx + Diag.fix.text_len], 6
	jne	.fail3
	mov	rsi, [rbx + Diag.fix.text_ptr]
	lea	rdi, [want_fix]
	mov	rcx, 6
	repe	cmpsb
	jne	.fail3
	call	lt_ndiag
	cmp	rax, 1
	jne	.fail3

	; ---- check 4: all thirty reserved words, through `@nomen` ----------
	xor	rbx, rbx
  .sweep:
	cmp	rbx, KW_COUNT
	jge	.sweep_done
	; build `@<word>` in the scratch buffer
	mov	byte [atbuf], '@'
	lea	rsi, [kw_text]
	mov	eax, [kw_off + rbx*4]
	add	rsi, rax
	lea	rdi, [atbuf + 1]
	movzx	ecx, byte [kw_len + rbx]
	mov	r12d, ecx
	rep	movsb
	lea	rdi, [atbuf]
	lea	esi, [r12 + 1]
	call	lt_run
	cmp	eax, 220
	jne	.fail4
	call	lt_ndiag
	cmp	rax, 1
	jne	.fail4
	xor	rdi, rdi
	call	lt_diag
	mov	r13, rax
	cmp	dword [r13 + Diag.span.start], 1
	jne	.fail4
	cmp	dword [r13 + Diag.span.len], r12d
	jne	.fail4
	cmp	dword [r13 + Diag.fix.kind], DIAG_FIX_REPLACE
	jne	.fail4
	; the fix text must be exactly `<word>_`
	mov	rax, r12
	inc	rax
	cmp	[r13 + Diag.fix.text_len], rax
	jne	.fail4
	mov	rdi, [r13 + Diag.fix.text_ptr]
	lea	rsi, [kw_text]
	mov	eax, [kw_off + rbx*4]
	add	rsi, rax
	mov	rcx, r12
	repe	cmpsb
	jne	.fail4
	mov	rax, [r13 + Diag.fix.text_ptr]
	cmp	byte [rax + r12], '_'
	jne	.fail4
	inc	rbx
	jmp	.sweep
  .sweep_done:

	; ---- check 5: E0220 is deliberately NOT in §8.3's fix-required set --
	mov	edi, 220
	call	diag_fix_required
	test	eax, eax
	jnz	.fail5
	; ... while a lexicon code IS, so the predicate is really being asked
	mov	edi, 601
	call	diag_fix_required
	cmp	eax, 1
	jne	.fail5

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1:
	mov	eax, 231
	mov	edi, 11
	syscall
  .fail2:
	mov	eax, 231
	mov	edi, 12
	syscall
  .fail3:
	mov	eax, 231
	mov	edi, 13
	syscall
  .fail4:
	mov	eax, 231
	mov	edi, 14
	syscall
  .fail5:
	mov	eax, 231
	mov	edi, 15
	syscall

; ---- harness ---------------------------------------------------------------
; Plain labels, not `proc`: a `proc` argument name becomes an unmangled global
; (macros/proc.inc's header), and this fixture wants no part of that namespace.
; Every helper pushes an ODD number of registers so that `rsp` is 16-aligned at
; the `call`s inside it -- at a helper's first instruction `rsp` is 8 (mod 16),
; because `call` pushed the return address.
;
; NOTE, found by running: the sample path label here is `lt_path` and NOT
; `pathname`, because `rt/sys.inc` has `proc sys_openat, dirfd, pathname, ...`
; and every proc argument name is a global `= rbp - K`. A data label named
; `pathname` therefore resolves to a register expression, and the failure
; surfaces as "variable term used where not expected" at the USE site, naming
; neither the label nor sys.inc.

; lt_setup -- two arenas and an interner. The interner's arena is separate and
; is NEVER reset: rt/intern.inc's header makes an interned id valid only for as
; long as the arena behind it lives, and lt_run rewinds the other one per case.
  lt_setup:
	push	rbx
	lea	rdi, [lt_arena]
	mov	rsi, LT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [lt_iarena]
	mov	rsi, LT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [lt_intern]
	lea	rsi, [lt_iarena]
	mov	rdx, 64
	call	intern_init
	pop	rbx
	ret
  .boom:
	mov	eax, 231
	mov	edi, 99
	syscall

; lt_run(rdi = source bytes, rsi = byte length) -> eax = the numeric part of
; the FIRST diagnostic, or 0 if the file is clean. Fresh vectors and a rewound
; scratch arena every call, so cases cannot leak into one another.
  lt_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	lea	rdi, [lt_arena]
	call	arena_reset
	lea	rdi, [lt_toks]
	lea	rsi, [lt_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 8
	call	vec_init
	lea	rdi, [lt_diags]
	lea	rsi, [lt_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 4
	call	vec_init
	lea	rdi, [lt_lx]
	lea	rsi, [lt_arena]
	lea	rdx, [lt_intern]
	lea	rcx, [lt_toks]
	lea	r8,  [lt_diags]
	call	lex_init
	lea	rdi, [lt_lx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [lt_path]
	mov	r8d, LT_PATH_LEN
	call	lex_set_source
	lea	rdi, [lt_lx]
	call	lex_run
	pop	r13
	pop	r12
	pop	rbx
	ret

; lt_ndiag -> rax = how many diagnostics the last lt_run recorded.
  lt_ndiag:
	lea	rax, [lt_diags]
	mov	rax, [rax + Vec.len]
	ret

; lt_diag(rdi = index) -> rax = that `Diag`. Traps through vec_get if the
; index is out of range, which is what a wrong expected-count would produce.
  lt_diag:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [lt_diags]
	call	vec_get
	pop	rbx
	ret

; lt_ntok -> rax = how many tokens the last lt_run produced.
  lt_ntok:
	lea	rax, [lt_toks]
	mov	rax, [rax + Vec.len]
	ret

; lt_tok(rdi = index) -> rax = that `Tok`.
  lt_tok:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [lt_toks]
	call	vec_get
	pop	rbx
	ret

segment readable
  lt_path:	db 'fixture.exsc'
  lt_path_end:
  LT_PATH_LEN = lt_path_end - lt_path

  src_forma:
	db	0x40, 0x66, 0x6F, 0x72, 0x6D, 0x61, 0x0A
  src_forma_end:
  SRC_FORMA_LEN = src_forma_end - src_forma

  src_transit:
	db	0x40, 0x74, 0x72, 0x61, 0x6E, 0x73, 0x69, 0x74, 0x75, 0x73, 0x0A, 0x73
	db	0x74, 0x72, 0x75, 0x63, 0x74, 0x75, 0x72, 0x61, 0x20, 0x50, 0x20, 0x7B
	db	0x20, 0x61, 0x3A, 0x20, 0x75, 0x38, 0x20, 0x7D, 0x0A
  src_transit_end:
  SRC_TRANSIT_LEN = src_transit_end - src_transit

  ; §8.5's own example, after the correction that section records.
  src_discern:
	db	0x64, 0x69, 0x73, 0x63, 0x65, 0x72, 0x6E, 0x65, 0x20, 0x66, 0x6F, 0x72
	db	0x6D, 0x61, 0x20, 0x7B, 0x20, 0x7D, 0x0A
  src_discern_end:
  SRC_DISCERN_LEN = src_discern_end - src_discern

  want_fix:
	db	0x66, 0x6F, 0x72, 0x6D, 0x61, 0x5F

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  lt_arena	rb sizeof.Arena
  lt_iarena	rb sizeof.Arena
  lt_intern	rb sizeof.Interner
  lt_toks	rb sizeof.Vec
  lt_diags	rb sizeof.Vec
  lt_lx		rb sizeof.Lexer
  atbuf		rb KW_MAX_LEN + 2
