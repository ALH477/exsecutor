; tests/unit/driver_emitte.asm
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
; `--emitte tokens|cst|ast` (driver/dump.inc, driver/cli.inc), spec 18.2's
; named stage-dump mitigation. This fixture does not build a real Lexer/CST/
; AST -- that is what `tests/run.sh`'s conformance phase and a manual run
; against examples/ already exercise end-to-end (see this repo's own
; `driver_status.asm`: "a fixture that opens one has to know where it is").
; What IS addressable in-process, with no file on disk:
;
;   1. `drv_tok_kind_name` over all seven `TOK_*` kinds.
;   2. `drv_dump_tokens`'s EXACT output bytes over a hand-built three-token
;      `Vec` -- nothing here comes from a real lex, so the expected string
;      below is not a round-trip of anything, it is the format's own spec
;      (driver/dump.inc's header) applied by hand.
;   3. `__drv_dump_buf`'s arithmetic: a fresh arena, one nearly exhausted by
;      six bytes short of an 8-aligned boundary, and one already past its
;      limit (the defensive clamp a real arena should never reach).
;   4. `--emitte`'s three-way parse through `drv_parse`: the three accepted
;      values, a rejected one, and the repeated-option refusal every other
;      `aedifica` option already gets (§9.3's reasoning, applied here by
;      driver/cli.inc exactly as it is to `--diagnostica`).
;
; Exit 0 = every check passed. Otherwise:
;   10+K  `drv_tok_kind_name` disagreed for TOK_* id K
;   20    `drv_dump_tokens`'s output did not match the expected bytes exactly
;   30    `__drv_dump_buf` disagreed on a fresh arena
;   31    ... on an arena 6 bytes short of its 8-aligned limit
;   32    ... on an arena already past its limit (the clamp-to-zero path)
;   40    an --emitte value that must be ACCEPTED was refused, or set the
;         wrong `DrvCtx.emitte`
;   50+N  an --emitte command line that must be REFUSED was accepted, or
;         with the wrong status -- N is the refusal table's row index
;   99    setup (arena_init) failed
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.1, §8.4, §9.3, §18.2.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

DE_ARENA = 1 shl 20

segment readable executable
  start:
	lea	r15, [de_ctx]

	; ---- 1: drv_tok_kind_name over every TOK_* id ------------------------
	; Actual ptr/len are stashed to memory before the second call
	; (`drv_cstrlen`, to measure the EXPECTED literal) so that call's own
	; use of rdx as scratch cannot quietly clobber what the first call
	; returned -- registers alone do not survive a call between them.
	;
	; THE ROW OFFSET LIVES IN r12, NOT rcx. `rcx` is caller-saved under
	; SysV (docs/asm-conventions.md, "1.1 Register roles") -- a callee is
	; free to clobber it whether or not its own `uses` list says so, and
	; `drv_tok_kind_name` does (`mov rcx, [drv_tk_tab + rax*8]`). Keeping
	; the offset in rcx across that call segfaulted this fixture the first
	; time it ran: found by running it, not by reading the loop.
	xor	rbx, rbx
  .kindloop:
	cmp	rbx, DE_KIND_COUNT
	jge	.kinddone
	mov	r12, rbx
	shl	r12, 4			; 16 bytes per row: kind id, name ptr
	mov	edi, [de_kindtab + r12*1]
	call	drv_tok_kind_name	; rax = actual ptr, rdx = actual len
	mov	[de_actptr], rax
	mov	[de_actlen], rdx
	mov	rdi, [de_kindtab + r12*1 + 8]	; expected name pointer
	mov	[de_expptr], rdi
	call	drv_cstrlen		; rax = expected len
	cmp	rax, [de_actlen]
	jne	.f_kind
	mov	rcx, rax
	mov	rsi, [de_actptr]
	mov	rdi, [de_expptr]
	cld
	repe	cmpsb
	jne	.f_kind
	inc	rbx
	jmp	.kindloop
  .f_kind:
	mov	rax, rbx
	add	rax, 10
	mov	edi, eax
	jmp	de_die
  .kinddone:

	; ---- 2: drv_dump_tokens's exact bytes ---------------------------------
	lea	rdi, [de_tokarena]
	mov	rsi, DE_ARENA
	call	arena_init
	jc	de_die99
	lea	rdi, [de_toks]
	lea	rsi, [de_tokarena]
	mov	rdx, sizeof.Tok
	mov	rcx, 4
	call	vec_init

	; token 0: KEYWORD, aux 5, span{file 1, start 0, len 7}
	lea	rdi, [de_toks]
	call	vec_push
	mov	dword [rax + Tok.kind], TOK_KEYWORD
	mov	dword [rax + Tok.aux], 5
	mov	dword [rax + Tok.span.file_id], 1
	mov	dword [rax + Tok.span.start], 0
	mov	dword [rax + Tok.span.len], 7
	; token 1: IDENT, aux 42, span{file 1, start 8, len 3}
	lea	rdi, [de_toks]
	call	vec_push
	mov	dword [rax + Tok.kind], TOK_IDENT
	mov	dword [rax + Tok.aux], 42
	mov	dword [rax + Tok.span.file_id], 1
	mov	dword [rax + Tok.span.start], 8
	mov	dword [rax + Tok.span.len], 3
	; token 2: EOF, aux 0, span{file 1, start 11, len 0}
	lea	rdi, [de_toks]
	call	vec_push
	mov	dword [rax + Tok.kind], TOK_EOF
	mov	dword [rax + Tok.aux], 0
	mov	dword [rax + Tok.span.file_id], 1
	mov	dword [rax + Tok.span.start], 11
	mov	dword [rax + Tok.span.len], 0

	lea	rdi, [de_wr]
	lea	rsi, [de_dumpbuf]
	mov	rdx, DE_DUMPBUF_CAP
	call	diag_out_init
	lea	rdi, [de_toks]
	lea	rsi, [de_wr]
	call	drv_dump_tokens

	cmp	dword [de_wr + DiagOut.trunc], 0
	jne	.f20
	mov	rax, qword [de_wr + DiagOut.len]
	cmp	rax, DE_EXPECT_LEN
	jne	.f20
	lea	rdi, [de_dumpbuf]
	lea	rsi, [de_expect]
	mov	rcx, DE_EXPECT_LEN
	cld
	repe	cmpsb
	je	.dumpok
  .f20:
	mov	edi, 20
	jmp	de_die
  .dumpok:

	; ---- 3: __drv_dump_buf ------------------------------------------------
	lea	rdi, [de_arena3]
	mov	rsi, 4096
	call	arena_init
	jc	de_die99

	lea	rdi, [de_arena3]
	lea	rsi, [de_cap3]
	call	__drv_dump_buf
	mov	rcx, qword [de_arena3 + Arena.base]
	cmp	rax, rcx		; base is already 8-aligned (page-aligned)
	jne	.f30
	cmp	qword [de_cap3], 4096
	jne	.f30

	; six bytes short of the 8-aligned limit: (4090+7)&~7 == 4096 == limit
	mov	rax, qword [de_arena3 + Arena.base]
	add	rax, 4090
	mov	qword [de_arena3 + Arena.cur], rax
	lea	rdi, [de_arena3]
	lea	rsi, [de_cap3]
	call	__drv_dump_buf
	cmp	qword [de_cap3], 0
	jne	.f31

	; past the limit entirely -- the defensive clamp-to-zero path. A real
	; arena_alloc caller can never reach this (rt/arena.inc's own `rassert`
	; forbids it); __drv_dump_buf reads Arena.cur/limit directly rather
	; than through arena_alloc, so it is the one place in driver/ this
	; state is even representable, and it must not compute a negative cap.
	mov	rax, qword [de_arena3 + Arena.base]
	add	rax, 4099
	mov	qword [de_arena3 + Arena.cur], rax
	lea	rdi, [de_arena3]
	lea	rsi, [de_cap3]
	call	__drv_dump_buf
	cmp	qword [de_cap3], 0
	jne	.f32
	jmp	.buf3ok
  .f30:	mov	edi, 30
	jmp	de_die
  .f31:	mov	edi, 31
	jmp	de_die
  .f32:	mov	edi, 32
	jmp	de_die
  .buf3ok:

	; ---- 4: --emitte through drv_parse ------------------------------------
	call	de_setup

	xor	rbx, rbx
  .acloop:
	cmp	rbx, DE_ACCEPT_COUNT
	jge	.acdone
	call	de_reset
	mov	rcx, rbx
	shl	rcx, 4			; 2 qwords per row
	mov	rdi, [de_accept_tab + rcx*1]	; argc
	mov	rsi, [de_accept_tab + rcx*1 + 8]	; argv
	call	drv_parse
	test	eax, eax
	jnz	.f40
	mov	rcx, rbx
	shl	rcx, 2			; one dd per row
	mov	r12d, [de_accept_want + rcx*1]
	cmp	dword [r15 + DrvCtx.emitte], r12d
	jne	.f40
	cmp	dword [r15 + DrvCtx.emitteset], 1
	jne	.f40
	inc	rbx
	jmp	.acloop
  .f40:
	mov	edi, 40
	jmp	de_die
  .acdone:

	xor	rbx, rbx
  .rjloop:
	cmp	rbx, DE_REFUSE_COUNT
	jge	.rjdone
	call	de_reset
	lea	rcx, [rbx + rbx*2]
	mov	rdi, [de_refuse_tab + rcx*8]		; argc
	mov	rsi, [de_refuse_tab + rcx*8 + 8]	; argv
	call	drv_parse
	lea	rcx, [rbx + rbx*2]
	mov	r12, [de_refuse_tab + rcx*8 + 16]
	cmp	eax, r12d
	jne	.f50
	inc	rbx
	jmp	.rjloop
  .f50:
	lea	rdi, [rbx + 50]
	jmp	de_die
  .rjdone:

	mov	eax, 231
	xor	edi, edi
	syscall

; de_die(edi = status) -- never returns.
de_die:
	mov	eax, 231
	syscall
de_die99:
	mov	edi, 99
	jmp	de_die

; de_setup -- the context fields `drv_parse` reads/writes that must be real
; storage before the first call: none of the arenas, since none of our
; command lines use --env or --epoch's value slot beyond drv_ctx_reset's own
; zeroing (drv_u64_parse and drv_ident_ok are exercised by driver_cli.asm,
; not duplicated here).
de_setup:
	ret

; de_reset -- clear everything `drv_parse` writes, so one case cannot make
; the next one pass or fail (driver_cli.asm's `dt_reset` does the same).
de_reset:
	push	rbx
	; The §12 SOURCE table `drv_parse` appends positionals to. Bound the
	; same way compiler/x86_64/exsc.asm binds it, because this fixture
	; drives `drv_parse` over command lines that carry a SOURCE.
	lea	rax, [de_srcs]
	mov	[r15 + DrvCtx.srcs], rax
	call	drv_ctx_reset
	pop	rbx
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'
; driver/run.inc's `-o` path calls `lwr_module` and `bfa_emit_program`, so
; every consumer of driver/ needs lower/ -- which brings the whole backend
; chain (program -> emit -> verify -> print -> parse -> ir) and relies on the
; consumer for rt/, ast/ and prelude/interface.inc: the first two above, the
; third through checker/types/prim.inc. Exactly compiler/x86_64/exsc.asm's
; order, for exactly its reasons, and backend_fasmg/ must NOT be included
; separately.
include '../../compiler/x86_64/lower/lower.inc'
include '../../compiler/x86_64/driver/driver.inc'

segment readable
  ; ---- 1: TOK_* -> expected name ----------------------------------------
  de_n_eof     db 'EOF',0
  de_n_ident   db 'IDENT',0
  de_n_keyword db 'KEYWORD',0
  de_n_string  db 'STRING',0
  de_n_number  db 'NUMBER',0
  de_n_annot   db 'ANNOT',0
  de_n_punct   db 'PUNCT',0
de_kindtab:
	dq TOK_EOF,     de_n_eof
	dq TOK_IDENT,   de_n_ident
	dq TOK_KEYWORD, de_n_keyword
	dq TOK_STRING,  de_n_string
	dq TOK_NUMBER,  de_n_number
	dq TOK_ANNOT,   de_n_annot
	dq TOK_PUNCT,   de_n_punct
de_kindtab_end:
DE_KIND_COUNT = (de_kindtab_end - de_kindtab) / 16
  assert DE_KIND_COUNT = 7

  ; ---- 2: the exact expected dump, built by hand against driver/dump.inc's
  ; own documented format -- NOT captured from a run of this fixture.
  de_expect:
	db 'tokv 1 3', 10
	db 't 0 KEYWORD 5 1 0 7', 10
	db 't 1 IDENT 42 1 8 3', 10
	db 't 2 EOF 0 1 11 0', 10
  de_expect_end:
  DE_EXPECT_LEN = de_expect_end - de_expect
  DE_DUMPBUF_CAP = 256

  ; ---- 4: accepted --emitte command lines --------------------------------
  de_s_exsc     db 'exsc',0
  de_s_aedifica db 'aedifica',0
  de_s_hospes   db '--hospes',0
  de_s_triple   db 'x86_64-linux',0
  de_s_src      db 'src.exsc',0
  de_s_emitte   db '--emitte',0
  de_s_tokens   db 'tokens',0
  de_s_cst      db 'cst',0
  de_s_ast      db 'ast',0
  de_s_bogus    db 'xml',0

de_av_tokens:
	dq de_s_exsc, de_s_aedifica, de_s_hospes, de_s_triple
	dq de_s_emitte, de_s_tokens, de_s_src
de_av_tokens_end:
DE_AV_TOKENS_N = (de_av_tokens_end - de_av_tokens) / 8

de_av_cst:
	dq de_s_exsc, de_s_aedifica, de_s_hospes, de_s_triple
	dq de_s_emitte, de_s_cst, de_s_src
de_av_cst_end:
DE_AV_CST_N = (de_av_cst_end - de_av_cst) / 8

de_av_ast:
	dq de_s_exsc, de_s_aedifica, de_s_hospes, de_s_triple
	dq de_s_emitte, de_s_ast, de_s_src
de_av_ast_end:
DE_AV_AST_N = (de_av_ast_end - de_av_ast) / 8

de_accept_tab:
	dq DE_AV_TOKENS_N, de_av_tokens
	dq DE_AV_CST_N,    de_av_cst
	dq DE_AV_AST_N,    de_av_ast
de_accept_tab_end:
DE_ACCEPT_COUNT = (de_accept_tab_end - de_accept_tab) / 16
  assert DE_ACCEPT_COUNT = 3

de_accept_want:
	dd DRV_EMIT_TOKENS
	dd DRV_EMIT_CST
	dd DRV_EMIT_AST
de_accept_want_end:
  assert (de_accept_want_end - de_accept_want) / 4 = DE_ACCEPT_COUNT

  ; ---- 4: refused --emitte command lines ----------------------------------
de_av_bogus:
	dq de_s_exsc, de_s_aedifica, de_s_hospes, de_s_triple
	dq de_s_emitte, de_s_bogus, de_s_src
de_av_bogus_end:
DE_AV_BOGUS_N = (de_av_bogus_end - de_av_bogus) / 8

de_av_repeat:
	dq de_s_exsc, de_s_aedifica, de_s_hospes, de_s_triple
	dq de_s_emitte, de_s_tokens, de_s_emitte, de_s_cst, de_s_src
de_av_repeat_end:
DE_AV_REPEAT_N = (de_av_repeat_end - de_av_repeat) / 8

de_av_noval:
	dq de_s_exsc, de_s_aedifica, de_s_hospes, de_s_triple, de_s_emitte
de_av_noval_end:
DE_AV_NOVAL_N = (de_av_noval_end - de_av_noval) / 8

de_refuse_tab:
	dq DE_AV_BOGUS_N,  de_av_bogus,  DRV_EXIT_USAGE
	dq DE_AV_REPEAT_N, de_av_repeat, DRV_EXIT_USAGE
	dq DE_AV_NOVAL_N,  de_av_noval,  DRV_EXIT_USAGE
de_refuse_tab_end:
DE_REFUSE_COUNT = (de_refuse_tab_end - de_refuse_tab) / 24
  assert DE_REFUSE_COUNT = 3

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  de_ctx	rb sizeof.DrvCtx
  de_srcs	rb DRV_SOURCES_MAX * sizeof.DrvSrc
  de_actptr	rq 1
  de_actlen	rq 1
  de_expptr	rq 1
  de_toks	rb sizeof.Vec
  de_tokarena	rb sizeof.Arena
  de_arena3	rb sizeof.Arena
  de_cap3	rq 1
  de_wr		rb 32
  de_dumpbuf	rb DE_DUMPBUF_CAP
