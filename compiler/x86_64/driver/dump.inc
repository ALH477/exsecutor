; compiler/x86_64/driver/dump.inc
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
; `--emitte tokens|cst|ast` -- spec 18.2's named, undelivered mitigation for
; hand-written assembly's weak debugging story: *"Mitigated by stage dumps at
; every pipeline boundary rather than by a debugger."* `cst_dump`
; (cst/red.inc) and `ast_dump` (ast/dump.inc) already existed; nothing reached
; them from a command line. This file is that wiring, plus the one dumper
; that did not exist anywhere yet -- the token vector has no dumper of its
; own, because lexer/ is not driver/'s to add one to (CLAUDE.md, Scope).
;
; THREE STAGES, spec 9.1's pipeline as far as it currently runs: `tokens`
; (post `lex_run`), `cst` (post `cst_parse`, the lossless §9.1 tree), `ast`
; (post `ast_from_cst` + `ast_verify_stage1`, the Stage 1 typed tree). SSA IR
; and a backend are §16 Stages 2/3 and do not exist, so there is nothing past
; `ast` to name.
;
; STDOUT FOR THE DUMP, STDERR FOR EVERYTHING ELSE. `drv_say` (driver/msg.inc)
; only ever writes fd 2; this file adds `drv_say_out`, fd 1 and nothing else,
; so a dump and the diagnostics that accompany it are separable by
; redirection alone -- `exsc aedifica ... --emitte ast SOURCE >tree.txt`
; carries only the tree in `tree.txt`; every diagnostic is still on the
; terminal. No new syscall: `drv_say_out` is `write(1)`, already on the
; allowlist and already reached through rt/sys.inc from driver/msg.inc.
;
; A GATE FAILURE HAS NOTHING TO DUMP, AND SAYS SO. lexer/lex.inc's own header
; is explicit that a file failing spec 8.1 is never tokenized at all -- the
; token vector stays at length 0, indistinguishable BY CONTENT from a
; legitimately empty source. Printing an empty `tokv 1 0` dump in that case
; would be a second, silent claim ("here is the (empty) token stream") on top
; of the diagnostic already explaining why there is none. `drv_aedifica`
; (driver/run.inc) instead prints nothing on stdout and one explanatory line
; on stderr, ahead of the §8.1 diagnostic itself -- honest emptiness, not
; silent emptiness, which is what this file's header in the task brief asks
; for by name.
;
; DETERMINISM. Every dumper here prints a name (this file's own literals, or
; `ast`/`cst`'s kind-name pools) or a plain decimal, never a pointer and never
; anything iterated by hash order (CLAUDE.md). `drv_tok_kind_name` indexes a
; small table by the token's own dense `TOK_*` id, not by any property of
; where the token lives in memory. No raw source byte is ever echoed by any
; of the three dumps -- unlike a diagnostic (§8.3), a dump line is numbers and
; compiler-authored names only, so none of §8.3's escaping obligation applies
; and none of escape.inc is reached from here.
;
; THE BUFFER. `DiagOut` (diag/render.inc) is a bounded, caller-owned cursor;
; neither `cst_dump` nor `ast_dump` streams or chunks, so this file has to
; hand them a buffer big enough to hold the whole dump before a single byte
; reaches `write(1)`. `__drv_dump_buf` hands back whatever is left in the
; compilation arena (`DrvCtx.arena` at `r15`) rather than guessing a constant:
; by the time a dump runs, lexing, parsing and the AST build have already
; taken their share out of an arena driver/io.inc sized at 256x the source
; (measured there against real adversarial shapes), so what remains scales
; with the same input the dump is proportional to, and never trips
; `arena_alloc`'s `rassert` (rt/arena.inc) -- this file reads `Arena.cur` and
; `Arena.limit` directly and never calls `arena_alloc` at all, so there is no
; capacity to overrun. A dump too big for what is left truncates -- `DiagOut`
; already refuses to overrun on its own -- and truncation is reported on
; stderr, the same honesty `driver/run.inc`'s `drv_m_trunc` already promises
; for one diagnostic that overran `DRV_DIAG_CAP`.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.1, §8.4, §9.3, §18.2.
; -----------------------------------------------------------------------------

include 'io.inc'

; ---- token kind names -------------------------------------------------------
; TOK_* (lexer/token.inc) is seven small dense ids with no name table of its
; own -- lexer/ has never needed one, having no dumper before this file. Kept
; here rather than added to lexer/token.inc: driver/ is the only consumer,
; and CLAUDE.md's Scope keeps a lexer/-owned file out of this change.
drv_tk_eof     db 'EOF',0
drv_tk_ident   db 'IDENT',0
drv_tk_keyword db 'KEYWORD',0
drv_tk_string  db 'STRING',0
drv_tk_number  db 'NUMBER',0
drv_tk_annot   db 'ANNOT',0
drv_tk_punct   db 'PUNCT',0

; Indexed directly by TOK_* -- TOK_EOF=0 .. TOK_PUNCT=6 (lexer/token.inc) --
; so a lookup is one load, not a chain of compares, and adding an eighth kind
; there without touching this table fails loudly (`rassert` below) instead of
; printing a wrong name.
drv_tk_tab:
	dq drv_tk_eof, drv_tk_ident, drv_tk_keyword, drv_tk_string
	dq drv_tk_number, drv_tk_annot, drv_tk_punct
DRV_TK_TAB_COUNT = 7

; drv_tok_kind_name(kind) -> rax = pointer, rdx = length. `kind` must be a
; TOK_* value; anything else is an internal contract violation (this driver
; handed itself a `Tok` the lexer never produced) and traps, the same
; convention lexer/token.inc's own `kw_text_of` uses for `KW_*`.
proc drv_tok_kind_name, kind
	locals
	endl
	mov	eax, [kind]
	rassert	eax ge 0
	rassert	eax lt DRV_TK_TAB_COUNT
	mov	rcx, [drv_tk_tab + rax*8]
	mov	rdi, rcx
	call	drv_cstrlen
	mov	rdx, rax
	mov	rax, rcx
	return
endp

; ---- one field, one tag, one newline ----------------------------------------
; The same three primitives ast/dump.inc's own `__ast_d_*` build on, kept as a
; separate, driver-owned copy rather than called cross-module: those three are
; `__`-prefixed internals of ast/dump.inc, not part of what ast/ast.inc's
; header lists as reachable from outside ast/, and reaching for them anyway
; would make an ast/-owned rename break driver/ silently.
proc __drv_d_num, wr, value
	uses	rbx
	locals
	endl
	mov	rbx, [wr]
	mov	rdi, rbx
	mov	rsi, 32			; ' '
	mov	rdx, 1
	call	diag_out_fill
	mov	rdi, rbx
	mov	rsi, [value]
	call	diag_out_u32
	return
endp

proc __drv_d_kindname, wr, kind
	uses	rbx, r12, r13
	locals
	endl
	mov	rdi, [kind]
	call	drv_tok_kind_name	; rax = ptr, rdx = len
	mov	r12, rax
	mov	r13, rdx
	mov	rbx, [wr]
	mov	rdi, rbx
	mov	rsi, 32			; ' '
	mov	rdx, 1
	call	diag_out_fill
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	diag_out_bytes
	return
endp

proc __drv_d_tag, wr, ch
	uses	rbx
	locals
	endl
	mov	rbx, [wr]
	mov	rdi, rbx
	mov	rsi, [ch]
	mov	rdx, 1
	call	diag_out_fill
	return
endp

proc __drv_d_nl, wr
	locals
	endl
	mov	rdi, [wr]
	mov	rsi, 10
	mov	rdx, 1
	call	diag_out_fill
	return
endp

; ---- tokens: the one dump this file owns outright ---------------------------
; Format, deliberately the same shape ast_dump's header documents for its own
; tree ("a representation with a text form can be built, read and compared BY
; HAND"):
;
;	tokv 1 NTOK
;	t I KIND AUX FILE START LEN
;
; `I` is the token's own 0-based position in `DrvCtx.toks` (matching
; `vec_get`'s own indexing -- ast_dump's records are 1-based for a reason
; specific to its id scheme, spelled out in that file's header, that does not
; apply here). `KIND` is a NAME (drv_tok_kind_name), never the bare integer,
; for the same reason ast_dump's kind fields are names: renumbering `TOK_*`
; must not silently reinterpret a dump someone is reading by eye. `AUX` is
; printed as a raw number always -- its meaning is kind-dependent
; (lexer/token.inc's header table) and resolving it would mean re-deriving
; that table here and having two copies to keep in sync.
drv_tokv_magic db 'tokv'
DRV_TOKV_MAGIC_LEN = $ - drv_tokv_magic
DRV_TOKV_VERSION = 1

; drv_dump_tokens(toks, wr) -- the whole token vector, from index 0.
proc drv_dump_tokens, toks, wr
	uses	rbx, r12, r13, r14
	locals
		slot	tk, dq
	endl
	mov	rbx, [toks]
	mov	r14, [wr]

	mov	rdi, r14
	lea	rsi, [drv_tokv_magic]
	mov	rdx, DRV_TOKV_MAGIC_LEN
	call	diag_out_bytes
	mov	rdi, r14
	mov	rsi, DRV_TOKV_VERSION
	call	__drv_d_num
	mov	rdi, r14
	mov	rsi, [rbx + Vec.len]
	call	__drv_d_num
	mov	rdi, r14
	call	__drv_d_nl

	xor	r12, r12
	mov	r13, [rbx + Vec.len]
  .loop:
	cmp	r12, r13
	jae	.done
	mov	rdi, rbx
	mov	rsi, r12
	call	vec_get
	mov	[tk], rax

	mov	rdi, r14
	mov	rsi, 116		; 't'
	call	__drv_d_tag
	mov	rdi, r14
	mov	rsi, r12
	call	__drv_d_num

	mov	rax, [tk]
	mov	rdi, r14
	mov	esi, [rax + Tok.kind]
	call	__drv_d_kindname

	mov	rax, [tk]
	mov	rdi, r14
	mov	esi, [rax + Tok.aux]
	call	__drv_d_num

	mov	rax, [tk]
	mov	rdi, r14
	mov	esi, [rax + Tok.span.file_id]
	call	__drv_d_num

	mov	rax, [tk]
	mov	rdi, r14
	mov	esi, [rax + Tok.span.start]
	call	__drv_d_num

	mov	rax, [tk]
	mov	rdi, r14
	mov	esi, [rax + Tok.span.len]
	call	__drv_d_num

	mov	rdi, r14
	call	__drv_d_nl

	inc	r12
	jmp	.loop
  .done:
	return
endp

; ---- the dump buffer ---------------------------------------------------------
; __drv_dump_buf(arena, capout) -> rax = an 8-byte-aligned pointer into
; `arena`'s unused tail; [capout] = how many bytes are usable there. Reads
; `Arena.cur`/`Arena.limit` (rt/arena.inc) directly and calls neither
; `arena_alloc` nor `arena_reset` -- see this file's header for why grabbing
; the remainder this way, once, at the end of a run, is safe here and would
; not be a pattern to repeat inside a hot allocation path. Cannot fail (a cap
; of 0 is a legal, if useless, answer); leaves CF clear.
proc __drv_dump_buf, arena, capout
	uses	rbx
	locals
	endl
	mov	rbx, [arena]
	mov	rax, [rbx + Arena.cur]
	add	rax, 7
	and	rax, -8
	mov	rcx, [rbx + Arena.limit]
	sub	rcx, rax
	cmp	rcx, 0
	jge	.fits
	xor	rcx, rcx
  .fits:
	mov	rbx, [capout]
	mov	[rbx], rcx
	return
endp

; ---- the whole thing, wired to a subcommand ----------------------------------
drv_m_dtrunc db 'the --emitte dump did not fit and was truncated; the output above is incomplete. That is a limit of this compiler, not of the source.',0

; drv_emit_dump -- reads `DrvCtx.emitte` at `r15` and writes that stage's dump
; to stdout. Called only once `DrvCtx.emitte` is known not to be
; `DRV_EMIT_NONE` and, for `cst`/`ast`, only once the CST and the typed AST
; both exist -- driver/run.inc's `drv_aedifica` is the only caller and holds
; both preconditions structurally (the call site is unreachable before
; `ast_verify_stage1` returns). Cannot fail; leaves CF clear.
proc drv_emit_dump
	uses	rbx, r12, r13, r14
	locals
		slot	cap, dq
		slot	wr,  32
	endl
	mov	rdi, [r15 + DrvCtx.arena]
	lea	rsi, [cap]
	call	__drv_dump_buf
	lea	rdi, [wr]
	mov	rsi, rax
	mov	rdx, [cap]
	call	diag_out_init

	mov	eax, [r15 + DrvCtx.emitte]
	cmp	eax, DRV_EMIT_TOKENS
	je	.tokens
	cmp	eax, DRV_EMIT_CST
	je	.cst

	; DRV_EMIT_AST
	mov	rdi, [r15 + DrvCtx.ast]
	lea	rsi, [wr]
	call	ast_dump
	jmp	.flush

  .tokens:
	mov	rdi, [r15 + DrvCtx.toks]
	lea	rsi, [wr]
	call	drv_dump_tokens
	jmp	.flush

  .cst:
	mov	rbx, [r15 + DrvCtx.lx]
	mov	ebx, [rbx + Lexer.file_id]
	mov	rdi, [r15 + DrvCtx.ctree]
	lea	rsi, [wr]
	mov	rdx, rbx
	call	cst_dump

  .flush:
	mov	rdi, [wr + DiagOut.ptr]
	mov	rsi, [wr + DiagOut.len]
	call	drv_say_out
	cmp	dword [wr + DiagOut.trunc], 0
	je	.done
	lea	rdi, [drv_m_dtrunc]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
  .done:
	return
endp
