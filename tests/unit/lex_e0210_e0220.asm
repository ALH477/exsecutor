; tests/unit/lex_e0210_e0220.asm
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
; REGRESSION: `EXS-E0210` (malformed numeric literal) must never suppress
; `EXS-E0220` (reserved keyword used as identifier), in either order, and
; `lex_run` must not treat a §8.4-only diagnostic as if §8.1's gate had
; rejected the file.
;
; THE BUG, found by running tests/diagnostics/ (see that directory's README):
; against the driver end to end, a malformed numeric literal anywhere in a
; file silently dropped a reserved-word-as-identifier diagnostic elsewhere in
; the same file, regardless of which came first.
;
; THE MECHANISM was NOT in `lex_tokenize`'s scan loop -- a standalone
; `lex_run` over a file with both mistakes already recorded both diagnostics,
; in source order, before this fix existed. It was in `lex_run` itself
; (compiler/x86_64/lexer/lex.inc): it set CF, and NOT just eax, whenever
; `Lexer.ndiag` was nonzero after EITHER `lex_source_check` (§8.1, the real
; admissibility gate -- no token stream exists on that path) OR
; `lex_tokenize` (§8.4/§8.2 -- the token stream is COMPLETE either way, this
; file's own header: "Cannot fail in the CF sense"). `driver/run.inc` reads
; that CF with a single `jc .gate_failed` and, on the tokenize-phase branch,
; skipped `cst_parse` outright -- and `cst_parse` is where a reserved word in
; a general (non-`@nomen`) identifier position raises `EXS-E0220`, via this
; module's own `lex_kw_as_ident`. So the general-position `EXS-E0220` was
; lost whenever an unrelated `EXS-E0210` shared the file, in either order,
; while the `@nomen` form of `EXS-E0220` (raised directly inside
; `lex_tokenize`, checked here) was never actually at risk -- it was already
; safely in the `diags` vector by the time `lex_run` computed CF at all.
;
; This fixture therefore checks two things, not one:
;   1. (checks 1-2) `lex_tokenize`'s scan loop really does record both codes,
;      in source order, with correct spans, in EITHER order in the file --
;      confirming the scan loop was never the defect.
;   2. (check 3) `lex_run` leaves CF CLEAR after a run whose only
;      diagnostics came from `lex_tokenize` -- confirming the actual fix: a
;      caller that only branches on `jc`, as `driver/run.inc` does, now
;      proceeds to parse this file rather than discarding it as unreadable.
;      Check 4 is the control: a genuine §8.1 gate failure (a lone BOM) must
;      still set CF, so this isn't "CF stopped meaning anything".
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

	; ---- check 1: `@forma` THEN a malformed literal --------------------
	; src_a = "@forma\nfirma bad: u64 = 1ab;\n"
	; source order: the E0220 word ("forma", offset 1, len 5) precedes the
	; malformed literal ("1ab", offset 24, len 3).
	lea	rdi, [src_a]
	mov	esi, SRC_A_LEN
	call	lt_run
	call	lt_ndiag
	cmp	rax, 2
	jne	.fail1
	xor	rdi, rdi
	call	lt_diag			; diag 0 -- must be E0220, earlier in source
	mov	rbx, rax
	cmp	dword [rbx + Diag.code_num], 220
	jne	.fail1
	cmp	dword [rbx + Diag.span.start], 1
	jne	.fail1
	cmp	dword [rbx + Diag.span.len], 5
	jne	.fail1
	mov	rdi, 1
	call	lt_diag			; diag 1 -- must be E0210, later in source
	mov	rbx, rax
	cmp	dword [rbx + Diag.code_num], 210
	jne	.fail1
	cmp	dword [rbx + Diag.span.start], 24
	jne	.fail1
	cmp	dword [rbx + Diag.span.len], 3
	jne	.fail1

	; ---- check 2: the malformed literal THEN `@forma` (reverse order) --
	; src_b = "firma bad: u64 = 1ab;\n@forma\n"
	; source order: the malformed literal ("1ab", offset 17, len 3)
	; precedes the E0220 word ("forma", offset 23, len 5).
	lea	rdi, [src_b]
	mov	esi, SRC_B_LEN
	call	lt_run
	call	lt_ndiag
	cmp	rax, 2
	jne	.fail2
	xor	rdi, rdi
	call	lt_diag			; diag 0 -- must be E0210, earlier in source
	mov	rbx, rax
	cmp	dword [rbx + Diag.code_num], 210
	jne	.fail2
	cmp	dword [rbx + Diag.span.start], 17
	jne	.fail2
	cmp	dword [rbx + Diag.span.len], 3
	jne	.fail2
	mov	rdi, 1
	call	lt_diag			; diag 1 -- must be E0220, later in source
	mov	rbx, rax
	cmp	dword [rbx + Diag.code_num], 220
	jne	.fail2
	cmp	dword [rbx + Diag.span.start], 23
	jne	.fail2
	cmp	dword [rbx + Diag.span.len], 5
	jne	.fail2

	; ---- check 3: CF stays CLEAR -- both diagnostics are tokenize-phase,
	; so a complete token stream exists and `driver/run.inc`'s `jc` must
	; not treat this file as unreadable. `lt_run` returns whatever CF
	; `lex_run` left (see lt_run's own comment); `src_b` is reused from
	; check 2, still CF-clear-worthy since NEITHER diagnostic is §8.1's.
	lea	rdi, [src_b]
	mov	esi, SRC_B_LEN
	call	lt_run
	jc	.fail3
	cmp	eax, 210		; eax = firstcode either way, unchanged
	jne	.fail3			; by this fix -- see lex_run's header

	; ---- check 4: the control. A genuine §8.1 gate failure (a lone BOM,
	; no bytes after it) must still set CF -- this fix narrows CF, it does
	; not disable it. If this regressed to always-clear, this check catches
	; it exactly the way lexer_source_policy.asm's check 5 does for BOM
	; alone.
	lea	rdi, [src_bom]
	mov	esi, SRC_BOM_LEN
	call	lt_run
	jnc	.fail4
	cmp	eax, 101
	jne	.fail4

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

; ---- harness ---------------------------------------------------------------
; Plain labels, not `proc`: a `proc` argument name becomes an unmangled
; global (macros/proc.inc's header), and this fixture wants no part of that
; namespace. Every helper pushes an ODD number of registers so that `rsp` is
; 16-aligned at the `call`s inside it -- at a helper's first instruction
; `rsp` is 8 (mod 16), because `call` pushed the return address.
;
; NOTE, found by running: the sample path label here is `lt_path` and NOT
; `pathname`, because `rt/sys.inc` has `proc sys_openat, dirfd, pathname, ...`
; and every proc argument name is a global `= rbp - K`. A data label named
; `pathname` therefore resolves to a register expression, and the failure
; surfaces as "variable term used where not expected" at the USE site,
; naming neither the label nor sys.inc. (docs/asm-conventions.md §4.1.)

; lt_setup -- two arenas and an interner. The interner's arena is separate and
; is NEVER reset: rt/intern.inc's header makes an interned id valid only for
; as long as the arena behind it lives, and lt_run rewinds the other one per
; case.
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

; lt_run(rdi = source bytes, rsi = byte length) -> CF/eax exactly as
; `lex_run` left them. Fresh vectors and a rewound scratch arena every call,
; so cases cannot leak into one another.
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

segment readable
  lt_path:	db 'fixture.exsc'
  lt_path_end:
  LT_PATH_LEN = lt_path_end - lt_path

  ; "@forma\nfirma bad: u64 = 1ab;\n" -- the E0220 word before the E0210 run.
  src_a:
	db "@forma", 10
	db "firma bad: u64 = 1ab;", 10
  src_a_end:
  SRC_A_LEN = src_a_end - src_a

  ; "firma bad: u64 = 1ab;\n@forma\n" -- the E0210 run before the E0220 word.
  src_b:
	db "firma bad: u64 = 1ab;", 10
	db "@forma", 10
  src_b_end:
  SRC_B_LEN = src_b_end - src_b

  ; A lone UTF-8 BOM and nothing else -- the §8.1 gate-failure control.
  src_bom:
	db 0xEF, 0xBB, 0xBF
  src_bom_end:
  SRC_BOM_LEN = src_bom_end - src_bom

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  lt_arena	rb sizeof.Arena
  lt_iarena	rb sizeof.Arena
  lt_intern	rb sizeof.Interner
  lt_toks	rb sizeof.Vec
  lt_diags	rb sizeof.Vec
  lt_lx		rb sizeof.Lexer
