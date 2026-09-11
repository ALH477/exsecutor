; tests/unit/lex_hex_literal.asm
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
; D6 (docs/design/wire-codec.md), retired: hex integer literals,
; `0x[0-9a-fA-F]+`, lex as ONE `TOK_NUMBER` -- the same token kind a decimal
; run already uses (token.inc has no separate hex kind; §8.4: "The token
; class is INT for both forms"). Spec: §8.4 "Literals, comments, layout".
;
; ACCEPTED: `0x0`, `0xd3`, `0xDEADBEEF`, `0xffffffffffffffff` -- lowercase and
; uppercase hex digits both attested, and a run wide enough that only the
; CHECKER's width/typing pass (chk_ty_litval, out of this tree) has anything
; to say about it; the lexer's job stops at producing one well-formed token
; with the right span.
;
; REJECTED, all `EXS-E0210`, exercising `lex.inc:335`'s hex path exactly as
; D6 specifies it:
;   `0X10`  -- capital `X`. One spelling of the prefix; capital never takes
;             the hex path at all and falls to the pre-existing decimal/
;             "digit run into an identifier character" rule, unchanged by
;             this milestone.
;   `0x`    -- the prefix with no digit after it.
;   `0x1G`  -- a hex digit run that itself runs into a non-hex identifier
;             character, the hex-path analogue of `10u32`.
;   `0b101` -- binary stays `[OPEN]`/`EXS-E0210`, per D6: "Binary and octal
;             bases ... stay [OPEN]." `0` is not followed by `x` at all, so
;             this never enters the hex path either.
;
; Exit 0 = all checks passed. Accepted-sample failures are 21-24 (A-D, token
; stream disagreed) and 30 (a clean sample produced an unexpected
; diagnostic). Rejected-sample failures are 11 (wrong first code), 12 (wrong
; diagnostic count), 13 (wrong span start), 14 (wrong span length).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/lexer/lexer.inc'

LT_ARENA = 1 shl 20
ANY = 0xFFFFFFFF

segment readable executable
  start:
	call	lt_setup

	; ---- accepted: exact token streams, clean (no diagnostics) --------
	lea	rdi, [acc_a]
	mov	esi, ACC_A_LEN
	lea	rdx, [tok_a]
	mov	ecx, TOK_A_N
	mov	r8d, 21
	call	lt_check

	lea	rdi, [acc_b]
	mov	esi, ACC_B_LEN
	lea	rdx, [tok_b]
	mov	ecx, TOK_B_N
	mov	r8d, 22
	call	lt_check

	lea	rdi, [acc_c]
	mov	esi, ACC_C_LEN
	lea	rdx, [tok_c]
	mov	ecx, TOK_C_N
	mov	r8d, 23
	call	lt_check

	lea	rdi, [acc_d]
	mov	esi, ACC_D_LEN
	lea	rdx, [tok_d]
	mov	ecx, TOK_D_N
	mov	r8d, 24
	call	lt_check

	; ---- rejected: EXS-E0210, one diagnostic, exact span ---------------
	; `rcx` is caller-saved (docs/asm-conventions.md 1.1) and every helper
	; below is a real `call`, so the row-index-to-byte-offset multiply is
	; RECOMPUTED after each one rather than trusted to survive it --
	; lexer_tokens.asm's own error-case loop hit exactly this and does the
	; same recompute; skipping it here would be a fresh instance of the
	; same bug in a copy of that loop.
	xor	rbx, rbx
  .case:
	cmp	rbx, CASE_COUNT
	jge	.case_done
	lea	rcx, [rbx + rbx*3]	; 4 dd per row -- *16 needs a legal scale
	lea	rdi, [case_ptrs]
	mov	rax, [rdi + rbx*8]	; case_ptrs[rbx] -- this row's source
	mov	rdi, rax
	mov	esi, [case_tab + rcx*4]	; row.len
	call	lt_run
	lea	rcx, [rbx + rbx*3]
	mov	r12d, [case_tab + rcx*4 + 4]	; row.code
	cmp	eax, r12d
	jne	.fail1
	call	lt_ndiag
	lea	rcx, [rbx + rbx*3]
	mov	r12d, [case_tab + rcx*4 + 8]	; row.ndiag (always 1 here)
	cmp	eax, r12d
	jne	.fail2
	xor	rdi, rdi
	call	lt_diag
	lea	rcx, [rbx + rbx*3]
	mov	r12d, [case_tab + rcx*4 + 12]	; row.span_len
	cmp	dword [rax + Diag.span.start], 0
	jne	.fail3			; every rejected sample's bad run starts at 0
	cmp	[rax + Diag.span.len], r12d
	jne	.fail4
	inc	rbx
	jmp	.case
  .case_done:

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
; Plain labels, not `proc`: a `proc` argument name becomes an unmangled global
; (macros/proc.inc's header), and this fixture wants no part of that
; namespace. Every helper pushes an ODD number of registers so that `rsp` is
; 16-aligned at the `call`s inside it -- at a helper's first instruction
; `rsp` is 8 (mod 16), because `call` pushed the return address.
;
; NOTE, found by running (recorded in tests/unit/lex_e0210_e0220.asm and
; lexer_tokens.asm too): the sample path label here is `lt_path`, NOT
; `pathname` -- `rt/sys.inc` has `proc sys_openat, dirfd, pathname, ...` and
; every proc argument name is a global `= rbp - K`, so a data label named
; `pathname` silently resolves to a register expression instead.

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
; `lex_run` left them (docs/asm-conventions.md's CF/eax protocol; lex_run's
; own header: CF clear + eax = first diagnostic code, 0 if none, once the
; file is admissible §8.1 source -- every case here is plain ASCII, so every
; case in this fixture takes that path). Fresh vectors and a rewound scratch
; arena every call, so cases cannot leak into one another.
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

; lt_check(rdi = source, rsi = length, rdx = expected table, rcx = expected
;          token count, r8d = the exit code to use if it disagrees)
; Exits the process on any mismatch; returns normally otherwise. Exits 30 if
; the sample produced a diagnostic at all -- these four samples are all
; supposed to be clean, valid hex literals.
  lt_check:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	push	rbp
	mov	r13, rdx
	mov	r14, rcx
	mov	rbp, r8
	sub	rsp, 8			; six pushes leave rsp 8 (mod 16); SysV
					; wants 0 immediately before a `call`
	call	lt_run
	test	eax, eax
	jnz	.dirty
	call	lt_ntok
	cmp	rax, r14
	jne	.bad
	xor	rbx, rbx
  .one:
	cmp	rbx, r14
	jge	.done
	mov	rdi, rbx
	call	lt_tok
	mov	r12, rax
	mov	rax, rbx
	shl	rax, 4			; 4 dd per expected row
	lea	r15, [r13 + rax]
	mov	ecx, [r15]
	cmp	[r12 + Tok.kind], ecx
	jne	.bad
	mov	ecx, [r15 + 4]
	cmp	ecx, ANY
	je	.skip_aux
	cmp	[r12 + Tok.aux], ecx
	jne	.bad
  .skip_aux:
	mov	ecx, [r15 + 8]
	cmp	ecx, ANY
	je	.skip_start
	cmp	[r12 + Tok.span.start], ecx
	jne	.bad
  .skip_start:
	mov	ecx, [r15 + 12]
	cmp	ecx, ANY
	je	.skip_len
	cmp	[r12 + Tok.span.len], ecx
	jne	.bad
  .skip_len:
	inc	rbx
	jmp	.one
  .done:
	add	rsp, 8
	pop	rbp
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	ret
  .bad:
	mov	edi, ebp
	mov	eax, 231
	syscall
  .dirty:
	mov	eax, 231
	mov	edi, 30
	syscall

segment readable
  lt_path:	db 'fixture.exsc'
  lt_path_end:
  LT_PATH_LEN = lt_path_end - lt_path

  ; ---- accepted samples -------------------------------------------------
  acc_a:
	db	"0x0;", 10
  acc_a_end:
  ACC_A_LEN = acc_a_end - acc_a
tok_a:
	dd	TOK_NUMBER, 0, 0, 3	; 0x0
	dd	TOK_PUNCT, PUN_SEMI, 3, 1	; ;
	dd	TOK_EOF, 0, 5, 0	; end of input
  tok_a_end:
  TOK_A_N = (tok_a_end - tok_a) / 16
  assert TOK_A_N = 3

  acc_b:
	db	"0xd3;", 10
  acc_b_end:
  ACC_B_LEN = acc_b_end - acc_b
tok_b:
	dd	TOK_NUMBER, 0, 0, 4	; 0xd3
	dd	TOK_PUNCT, PUN_SEMI, 4, 1	; ;
	dd	TOK_EOF, 0, 6, 0	; end of input
  tok_b_end:
  TOK_B_N = (tok_b_end - tok_b) / 16
  assert TOK_B_N = 3

  acc_c:
	db	"0xDEADBEEF;", 10
  acc_c_end:
  ACC_C_LEN = acc_c_end - acc_c
tok_c:
	dd	TOK_NUMBER, 0, 0, 10	; 0xDEADBEEF
	dd	TOK_PUNCT, PUN_SEMI, 10, 1	; ;
	dd	TOK_EOF, 0, 12, 0	; end of input
  tok_c_end:
  TOK_C_N = (tok_c_end - tok_c) / 16
  assert TOK_C_N = 3

  acc_d:
	db	"0xffffffffffffffff;", 10
  acc_d_end:
  ACC_D_LEN = acc_d_end - acc_d
tok_d:
	dd	TOK_NUMBER, 0, 0, 18	; 0xffffffffffffffff -- 64 one-bits; the
					; checker (out of this tree), not the
					; lexer, is where width is judged
	dd	TOK_PUNCT, PUN_SEMI, 18, 1	; ;
	dd	TOK_EOF, 0, 20, 0	; end of input
  tok_d_end:
  TOK_D_N = (tok_d_end - tok_d) / 16
  assert TOK_D_N = 3

  ; ---- rejected samples: each its own buffer, bad run always at offset 0 -
  rej_a:
	db	"0X10", 10		; capital X -- never takes the hex path
  rej_a_end:
  REJ_A_LEN = rej_a_end - rej_a

  rej_b:
	db	"0x", 10		; the prefix, no digit
  rej_b_end:
  REJ_B_LEN = rej_b_end - rej_b

  rej_c:
	db	"0x1G", 10		; a hex run into a non-hex identifier char
  rej_c_end:
  REJ_C_LEN = rej_c_end - rej_c

  rej_d:
	db	"0b101", 10		; binary -- still [OPEN], still EXS-E0210
  rej_d_end:
  REJ_D_LEN = rej_d_end - rej_d

  case_ptrs:
	dq	rej_a, rej_b, rej_c, rej_d

  ; row: len, code, ndiag, span_len (span_start is always 0 -- checked
  ; directly in the loop above, not from this table)
  case_tab:
	dd	REJ_A_LEN, 210, 1, 4	; 0X10
	dd	REJ_B_LEN, 210, 1, 2	; 0x
	dd	REJ_C_LEN, 210, 1, 4	; 0x1G
	dd	REJ_D_LEN, 210, 1, 5	; 0b101
  case_tab_end:
  CASE_COUNT = (case_tab_end - case_tab) / 16
  ; A table that parsed to zero rows would make the loop above pass by never
  ; running. fasmg's native assemble-time `assert` (NOT macros/assert.inc's
  ; runtime `rassert`) refuses that.
  assert CASE_COUNT = 4

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  lt_arena	rb sizeof.Arena
  lt_iarena	rb sizeof.Arena
  lt_intern	rb sizeof.Interner
  lt_toks	rb sizeof.Vec
  lt_diags	rb sizeof.Vec
  lt_lx		rb sizeof.Lexer
