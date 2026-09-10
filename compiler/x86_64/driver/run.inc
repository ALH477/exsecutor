; compiler/x86_64/driver/run.inc
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
; What each subcommand does: `aedifica` for real, the other nine as refusals
; that say so. `drv_main` is what compiler/x86_64/exsc.asm calls.
;
; WHAT `aedifica` ACTUALLY IS TODAY, said plainly because the exit statuses
; below depend on it: it reads the source, runs lexer/'s §8.1 gate and §8.4
; tokenizer, and emits whatever diagnostics that produced. There is no
; parser, no type checker and no backend (§16 Stages 1-3; §15 #9 records that
; there is no phrase grammar yet). So `aedifica` can find that a file is not
; admissible Exsecutor source, and it cannot yet turn an admissible one into
; anything.
;
; `-o OUT` IS THEREFORE OPTIONAL HERE, AND THAT IS A DECISION, NOT AN
; OVERSIGHT. §12 pins *"the minimum build invocation"* as
; `exsc aedifica --hospes TRIPLE SOURCE -o OUT`, and that whole command is
; honoured: given `-o`, `aedifica` runs the front end and then says that code
; generation is unimplemented, leaving OUT untouched and exiting 4. It never
; reports success for work it did not do, and tools/publish-gate.sh's step 3
; fails on exactly that, honestly, instead of on a missing binary.
;
; Dropping `-o` asks for no artifact, so §12's *minimum build* invocation is
; not the command being given, and the front end running clean is the whole
; of the requested work: exit 0. That is the only way exit 0 is reachable
; today, and a status nothing can produce is a status nothing can test. If
; the spec would rather `-o` were mandatory, this file is the one place that
; changes -- and exit 0 becomes unreachable until a backend exists.
;
; §8.1 IS A GATE AND THIS FILE DOES NOT RE-IMPLEMENT IT. `lex_run` runs the
; source pass first and does not tokenize at all if it reported anything
; (lexer/lex.inc's header). Nothing here re-checks a BOM or a CRLF; there is
; one §8.1 in this compiler and it is not in driver/.
;
; ONE DIAGNOSTIC PATH. `diag_render_text` and `diag_render_json` do the
; rendering; this file frames and writes. In `--diagnostica json` the frame
; is JSON Lines -- one object, one newline, no wrapping array -- which is
; what `diag_render_json`'s own header asks for ("no trailing newline (the
; caller frames records however it frames them)") and what lets a consumer
; read diagnostics as they arrive. In that mode stderr carries JSON and
; nothing else on the diagnostic path: the informational note the clean path
; writes is suppressed, because a plain-text line in the middle of a JSON
; Lines stream is a parse error for the tool it was meant to help. Host
; failures (exit 3) and usage errors (exit 2) still write prose in either
; mode -- there are no diagnostics on those paths to interleave with.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1, §8.3, §9.3, §9.5, §12, §16.
; -----------------------------------------------------------------------------

include 'dump.inc'

; One rendered diagnostic. Large because diag_render_text echoes the whole
; offending source line escaped, and escape.inc can spend up to nine bytes
; (`\u{10FFFF}`) on one codepoint; the renderers truncate rather than overrun
; either way, and truncation is reported, never silent.
DRV_DIAG_CAP = 65536

; Fixed-capacity arenas that exist before the source file's size is known.
; The compilation arena is NOT among them -- driver/io.inc sizes that one
; from the file.
DRV_IARENA_CAP  = 4 * 1024 * 1024
DRV_SCRATCH_CAP = 1024 * 1024
DRV_INTERN_BUCKETS = 256	; power of two -- map_init rassert's it
DRV_ENVMAP_BUCKETS = 64

; The §9.1 CST's three caller-owned structures (cst/green.inc: "this file
; allocates no storage of its own beyond what it takes from `arena`"). The two
; vectors grow by doubling out of the compilation arena, so these are starting
; points and not limits; the bucket count is NOT -- rt/map.inc fixes it for the
; map's lifetime, so a bigger file gets longer chains rather than a rehash.
; 8192 buckets is 64 KiB out of an arena sized at 256x the source (io.inc).
DRV_GREEN_CAP0  = 256
DRV_WORK_CAP0   = 256
DRV_CST_BUCKETS = 8192		; power of two -- map_init rassert's it

drv_s_empty  db 0
drv_m_hint   db "run `exsc` with no arguments for usage",0
drv_m_todo   db ': not implemented in this build',0
drv_m_todo2  db 'only `aedifica` does anything today: it reads a source file, applies spec 8.1, lexes it under spec 8.4, parses it under spec 8.6 into the spec 9.1 CST, builds the spec 9.1 typed AST, and reports diagnostics. That is spec 16 Stage 1.',0
drv_m_todolsp db '`lsp` is marked [OPEN] Deferred in spec 18.2 -- it needs the lossless CST, which does not exist.',0
drv_m_nohospes db 'aedifica: --hospes TRIPLE is required. Spec 9.5: exsc with no --hospes is an error, and there is no default-to-build-platform.',0
drv_m_emptyhospes db 'aedifica: --hospes: TRIPLE must not be empty',0
; The only triple this assembly implementation has anything to target --
; see the comment on drv_aedifica's third required-field check, above.
drv_t_x86_64_linux db 'x86_64-linux',0
drv_m_hospesunsup db 'aedifica: --hospes: this build only targets x86_64-linux (no backend exists for any triple, and spec 9.5 forbids guessing one), got: ',0
drv_m_nosrc  db 'aedifica: no SOURCE given',0
drv_m_trunc  db 'a diagnostic did not fit its buffer and was truncated; the output above is incomplete. That is a limit of this compiler, not of the source.',0
drv_m_okpfx  db 'aedifica: ',0
drv_m_okbare db ': the front end accepts this source -- it lexes, parses, and builds a typed AST. No object was requested, so none was written.',0
drv_m_okout  db ': the front end accepts this source, but code generation is not implemented, so no object was written. Spec 16 Stage 3.',0
drv_m_emitgate db ': --emitte was given, but this source failed the spec 8.1 source-hygiene gate, so it was never tokenized -- there is no token stream, no CST and no typed AST to dump. Nothing was written to stdout. See the diagnostic below.',0

; drv_emit_diag(dg, mode) -> eax = 1 if the rendering was truncated, else 0.
; Renders one `Diag` and writes it to stderr. Cannot fail; leaves CF clear --
; a failed write to stderr is swallowed by `drv_say` for the reason its own
; comment gives.
;
; The renderer is handed DRV_DIAG_CAP-1, not DRV_DIAG_CAP, so the JSON Lines
; newline always has somewhere to go: a record that exactly filled the buffer
; and then lost its terminator would silently glue two objects together, and
; the truncation flag would not say so.
proc drv_emit_diag, dg, mode
	uses	rbx
	locals
		slot tr,      dq
		slot drvdbuf, DRV_DIAG_CAP
	endl
	mov	rdi, [dg]
	lea	rsi, [drvdbuf]
	mov	rdx, DRV_DIAG_CAP - 1
	cmp	qword [mode], DIAG_MODE_JSON
	je	.json
	call	diag_render_text	; already ends in a newline
	jmp	.wrote
  .json:
	call	diag_render_json	; one object, unterminated, by design
  .wrote:
	mov	[tr], rdx
	mov	rbx, rax
	cmp	qword [mode], DIAG_MODE_JSON
	jne	.framed
	lea	rcx, [drvdbuf]
	mov	byte [rcx + rbx], 10
	inc	rbx
  .framed:
	lea	rdi, [drvdbuf]
	mov	rsi, rbx
	call	drv_say
	mov	rax, [tr]
	return
endp

; drv_stub(cmd) -> eax = DRV_EXIT_TODO, always. Names the subcommand, says it
; is not implemented, and says what is. Never silent: a tool that cannot tell
; "did nothing" from "did nothing and did not say so" has to guess.
proc drv_stub, cmd
	uses	rbx
	locals
	endl
	mov	ebx, [cmd]
	mov	rax, [drv_cmd_tab + rbx*8]
	mov	rdi, rax
	call	drv_cstrlen
	mov	rsi, [drv_cmd_tab + rbx*8]
	mov	rdx, rax
	lea	rdi, [drv_s_empty]
	lea	rcx, [drv_m_todo]
	call	drv_msg

	lea	rdi, [drv_m_todo2]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg

	cmp	ebx, DRV_CMD_LSP
	jne	.done
	lea	rdi, [drv_m_todolsp]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
  .done:
	mov	eax, DRV_EXIT_TODO
	return
endp

; drv_aedifica(argc, argv) -> eax = the process exit status. Parses this
; subcommand's options, reads the source, runs the lexer, and reports.
;
; The two required-field checks run in a fixed order -- `--hospes` first,
; then SOURCE -- so an invocation missing both always gets the same message.
; An order that depended on which check happened to be written first is the
; kind of thing that makes a conformance expectation flaky.
;
; A THIRD CHECK FOLLOWS THEM, ONCE BOTH ARE PRESENT: `--hospes`'S VALUE MUST
; BE EXACTLY `x86_64-linux`. Checked here because it was NOT checked before --
; any non-empty string was silently accepted and then ignored, which was only
; invisible because nothing downstream read it. That changed the moment
; Stage 2's `chk_set_target` needed a pointer width derived from the triple
; (checker/checker.inc's driver-wiring note), and spec §9.5 forbids guessing
; one: *"`exsc` with no `--hospes` is an error. No default-to-build-platform."*
; This assembly implementation is x86-64 machine code with no backend at all
; (§18.2's own note: the §12 cross-compilation demo is "dropped" for exactly
; this reason), so `x86_64-linux` is the only triple there is anything to do
; with -- refused with exit 4, not 2: the triple is not malformed, the target
; is simply unimplemented, the same distinction `drv_stub` already draws for
; nine whole subcommands. A second accepted triple, the day one exists, is a
; small table here (triple bytes -> ptrbits) in place of the single compare
; below -- not a redesign.
proc drv_aedifica, argc, argv
	uses	rbx, r12, r13, r14
	locals
		slot srclen, dq
		slot srcbuf, dq
		slot trunc,  dq
	endl
	; ---- the arenas that exist before the source does -------------------
	mov	rdi, [r15 + DrvCtx.iarena]
	mov	rsi, DRV_IARENA_CAP
	call	arena_init
	jc	.no_memory
	mov	rdi, [r15 + DrvCtx.scratch]
	mov	rsi, DRV_SCRATCH_CAP
	call	arena_init
	jc	.no_memory
	mov	rdi, [r15 + DrvCtx.interner]
	mov	rsi, [r15 + DrvCtx.iarena]
	mov	rdx, DRV_INTERN_BUCKETS
	call	intern_init
	mov	rdi, [r15 + DrvCtx.envmap]
	mov	rsi, [r15 + DrvCtx.iarena]
	mov	rdx, DRV_ENVMAP_BUCKETS
	call	map_init
	call	drv_ctx_reset

	; ---- the command line ----------------------------------------------
	mov	rdi, [argc]
	mov	rsi, [argv]
	call	drv_parse
	test	eax, eax
	jnz	.usage_hint

	cmp	qword [r15 + DrvCtx.hospes], 0
	je	.no_hospes
	cmp	qword [r15 + DrvCtx.hospesn], 0
	je	.empty_hospes
	cmp	qword [r15 + DrvCtx.srcpath], 0
	je	.no_source

	; ---- the target: only x86_64-linux exists in this build -------------
	mov	rdi, [r15 + DrvCtx.hospes]
	mov	rsi, [r15 + DrvCtx.hospesn]
	lea	rdx, [drv_t_x86_64_linux]
	call	drv_arg_is
	test	eax, eax
	jz	.hospes_unsupported

	; ---- the source ------------------------------------------------------
	mov	rdi, [r15 + DrvCtx.srcpath]
	mov	rsi, [r15 + DrvCtx.srcpathn]
	lea	rdx, [srclen]
	call	drv_slurp
	test	rax, rax
	jz	.host			; drv_slurp has already said why
	mov	[srcbuf], rax

	; ---- the front end ---------------------------------------------------
	mov	rdi, [r15 + DrvCtx.toks]
	mov	rsi, [r15 + DrvCtx.arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 256
	call	vec_init
	mov	rdi, [r15 + DrvCtx.diags]
	mov	rsi, [r15 + DrvCtx.arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 16
	call	vec_init
	mov	rdi, [r15 + DrvCtx.lx]
	mov	rsi, [r15 + DrvCtx.arena]
	mov	rdx, [r15 + DrvCtx.interner]
	mov	rcx, [r15 + DrvCtx.toks]
	mov	r8,  [r15 + DrvCtx.diags]
	call	lex_init
	mov	rdi, [r15 + DrvCtx.lx]
	mov	rsi, [srcbuf]
	mov	rdx, [srclen]
	mov	rcx, [r15 + DrvCtx.srcpath]
	mov	r8,  [r15 + DrvCtx.srcpathn]
	call	lex_set_source
	mov	rdi, [r15 + DrvCtx.lx]
	call	lex_run			; CF/eax: the FIRST code, if any
	jc	.gate_failed		; §8.1 IS A GATE. A file that failed it
					; was never tokenized (lexer/source.inc),
					; so there is no token stream to parse
					; and nothing a parse could add but a
					; cascade of diagnostics about bytes
					; the source policy already rejected.

	; ---- §8.6: the parse, into §9.1's lossless CST -----------------------
	; cst/ owns no storage: the green array, the builder's work stack and
	; the node-identity map are all the caller's, so that a caller wanting
	; several trees over one arena can decide what is shared. Here there is
	; exactly one tree and it shares the compilation arena with everything
	; else the front end allocates.
	mov	rdi, [r15 + DrvCtx.green]
	mov	rsi, [r15 + DrvCtx.arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, DRV_GREEN_CAP0
	call	vec_init
	mov	rdi, [r15 + DrvCtx.work]
	mov	rsi, [r15 + DrvCtx.arena]
	mov	rdx, 4
	mov	rcx, DRV_WORK_CAP0
	call	vec_init
	mov	rdi, [r15 + DrvCtx.cmap]
	mov	rsi, [r15 + DrvCtx.arena]
	mov	rdx, DRV_CST_BUCKETS
	call	map_init
	mov	rdi, [r15 + DrvCtx.ctree]
	mov	rsi, [r15 + DrvCtx.arena]
	mov	rdx, [r15 + DrvCtx.interner]
	mov	rcx, [r15 + DrvCtx.green]
	mov	r8,  [r15 + DrvCtx.work]
	mov	r9,  [r15 + DrvCtx.cmap]
	call	cst_tree_init
	mov	rdi, [r15 + DrvCtx.parser]
	mov	rsi, [r15 + DrvCtx.lx]
	mov	rdx, [r15 + DrvCtx.ctree]
	call	cst_parse
	; `cst_parse`'s CF is deliberately NOT branched on. §9.1's CST is
	; error-tolerant: a root exists on both paths, and every diagnostic the
	; parser raised is already in DrvCtx.diags, because cst/parse.inc
	; reports through the lexer's own `lex_diag` rather than carrying a
	; second list. The loop below picks them up with no change. Branching
	; here would build a SECOND diagnostic path for the same facts.

	; ---- §9.1 Stage 1: the typed AST -------------------------------------
	; Built on the error path too, and for the same reason: §16's Stage 1
	; kill criterion is diagnostics, and a checker that stopped at the
	; first parse error could not report a second. `ERROR` and `MISSING`
	; CST nodes become `Error` AST nodes with spans (ast/from_cst.inc).
	mov	rdi, [r15 + DrvCtx.ast]
	mov	rsi, [r15 + DrvCtx.arena]
	mov	rdx, [r15 + DrvCtx.interner]
	call	ast_init
	mov	rcx, [r15 + DrvCtx.lx]
	mov	edx, [rcx + Lexer.file_id]	; an INTERN id of the path, not a
						; descriptor (lexer/source.inc)
	mov	rdi, [r15 + DrvCtx.ast]
	mov	rsi, [r15 + DrvCtx.ctree]
	call	ast_from_cst
	; A malformed tree is a compiler bug, not a fact about the source, so
	; this traps rather than diagnosing -- and it is compiled out entirely
	; in a RELEASE build (macros/assert.inc).
	mov	rdi, [r15 + DrvCtx.ast]
	call	ast_verify_stage1

	; ---- Stage 2: the checker (checker/checker.inc) ----------------------
	; The exact sequence checker.inc's own "HOW THE DRIVER CALLS THIS" note
	; specifies, plus the `chk_set_target` call that note names but leaves
	; to the driver: runs on the same non-gate-failed path --emitte already
	; requires below (this call site is unreachable before
	; `ast_verify_stage1` returns), and appends straight into
	; `DrvCtx.diags` -- the SAME vector `.report` below already walks in
	; source order. A checker diagnostic therefore renders through the
	; identical path, and gets the identical exit-status treatment (a
	; nonzero `DrvCtx.diags` length is DRV_EXIT_DIAG), as a lexer or parser
	; one; there is no separate branch anywhere for "the checker found
	; something".
	mov	rdi, [r15 + DrvCtx.chk]	; -> ChkCtx, sizeof.ChkCtx = 328
	mov	rsi, [r15 + DrvCtx.ast]
	mov	rdx, [r15 + DrvCtx.diags]
	mov	rcx, [r15 + DrvCtx.scratch]	; the checker's own scratch arena --
					; already reset once per --env key,
					; long before this point, and not
					; touched again until then next run
	xor	r8, r8
	cmp	qword [r15 + DrvCtx.outpath], 0
	je	.chk_nomain
	mov	r8, CHK_F_PROGRAM		; -o was given: a PROGRAM was asked
					; for (spec §4.1 rule 2), so a missing
					; `initium` is `EXS-E0424`. Without -o
					; this is a library and its absence is
					; nothing.
  .chk_nomain:
	call	chk_init
	mov	rdi, [r15 + DrvCtx.chk]
	mov	rsi, [srcbuf]
	mov	rdx, [srclen]
	mov	rcx, [r15 + DrvCtx.srcpath]
	mov	r8,  [r15 + DrvCtx.srcpathn]
	call	chk_set_source
	mov	rdi, [r15 + DrvCtx.chk]
	mov	esi, 64			; the pointer width of the one triple
					; --hospes accepts (checked above) --
					; x86_64-linux is 64-bit. Spec §9.5
					; forbids guessing this from nothing;
					; deriving it is only reachable because
					; the triple was already pinned to a
					; single accepted value.
	call	chk_set_target
	mov	rdi, [r15 + DrvCtx.chk]
	call	chk_run			; rax = diagnostics appended; already
					; in DrvCtx.diags, so nothing reads it

	; ---- --emitte: spec 18.2's stage-dump mitigation, if requested --------
	; Runs whether or not the parse, the AST build, or the checker raised a
	; diagnostic: §9.1's CST and the Stage 1 AST are both built on the error
	; path too (the comments above), so the tree to dump exists either way,
	; and `.report` below still reports every diagnostic afterward
	; regardless. `--emitte ast` now dumps AFTER the checker has run, so a
	; `Seg`/`Path`/`Sub` node's `d` slot shows the declaration pass 1
	; resolved it to (0 where nothing was found, or where the target was a
	; type-position name pass 1 does not resolve -- see checker/resolve/
	; resolve.inc's header); driver/msg.inc's usage text says so.
	cmp	dword [r15 + DrvCtx.emitte], DRV_EMIT_NONE
	je	.report
	call	drv_emit_dump
	jmp	.report

  .gate_failed:
	; --emitte was asked for a stage that never came into existence: §8.1
	; is a gate, so a rejected file has no tokens, no CST and no AST to
	; dump. Say so on stderr -- see driver/dump.inc's header for why an
	; empty dump here would be a second, silent claim rather than an
	; honest one -- then fall into the same diagnostic report every other
	; refusal goes through.
	cmp	dword [r15 + DrvCtx.emitte], DRV_EMIT_NONE
	je	.report
	lea	rdi, [drv_m_okpfx]
	mov	rsi, [r15 + DrvCtx.srcpath]
	mov	rdx, [r15 + DrvCtx.srcpathn]
	lea	rcx, [drv_m_emitgate]
	call	drv_msg
	jmp	.report

  .report:
	; ---- report ----------------------------------------------------------
	; Every diagnostic, in source order, not just `lex_run`'s first: the
	; vector is what the lexer promises is complete and ordered, and eax is
	; only the head of it. Walked by index, so nothing here depends on a
	; pointer value (CLAUDE.md).
	mov	qword [trunc], 0
	mov	rbx, [r15 + DrvCtx.diags]
	mov	r13, [rbx + Vec.len]
	test	r13, r13
	jz	.clean
	xor	r12, r12
  .diagloop:
	cmp	r12, r13
	jae	.diagdone
	mov	rdi, rbx
	mov	rsi, r12
	call	vec_get
	mov	rdi, rax
	mov	esi, [r15 + DrvCtx.diagmode]	; a dd field -- a qword load here
						; would drag `dmset` in with it
	call	drv_emit_diag
	test	eax, eax
	jz	.notrunc
	mov	qword [trunc], 1
  .notrunc:
	inc	r12
	jmp	.diagloop
  .diagdone:
	cmp	qword [trunc], 0
	jne	.truncated
	mov	eax, DRV_EXIT_DIAG
	return
  .truncated:
	lea	rdi, [drv_m_trunc]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_HOST
	return

  ; ---- the clean paths -----------------------------------------------------
  .clean:
	cmp	qword [r15 + DrvCtx.outpath], 0
	jne	.clean_out
	; No artifact was asked for, so nothing is owed. The note is
	; suppressed in json mode -- see this file's header.
	cmp	dword [r15 + DrvCtx.diagmode], DIAG_MODE_JSON
	je	.quiet_ok
	lea	rdi, [drv_m_okpfx]
	mov	rsi, [r15 + DrvCtx.srcpath]
	mov	rdx, [r15 + DrvCtx.srcpathn]
	lea	rcx, [drv_m_okbare]
	call	drv_msg
  .quiet_ok:
	mov	eax, DRV_EXIT_OK
	return

  .clean_out:
	; An artifact WAS asked for. Saying so and exiting nonzero is the only
	; honest answer while there is no backend; exiting 0 here would make
	; tools/publish-gate.sh report a build that never happened.
	lea	rdi, [drv_m_okpfx]
	mov	rsi, [r15 + DrvCtx.srcpath]
	mov	rdx, [r15 + DrvCtx.srcpathn]
	lea	rcx, [drv_m_okout]
	call	drv_msg
	mov	eax, DRV_EXIT_TODO
	return

  ; ---- the refusals --------------------------------------------------------
  .usage_hint:
	mov	r14d, eax
	lea	rdi, [drv_m_hint]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
	mov	eax, r14d
	return
  .no_hospes:
	lea	rdi, [drv_m_nohospes]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return
  .empty_hospes:
	lea	rdi, [drv_m_emptyhospes]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return
  .hospes_unsupported:
	; Not a usage error -- the triple is well-formed, this build simply has
	; no backend for anything but x86_64-linux. See the comment on this
	; proc's third required-field check, above, for why that is exit 4.
	lea	rdi, [drv_m_hospesunsup]
	mov	rsi, [r15 + DrvCtx.hospes]
	mov	rdx, [r15 + DrvCtx.hospesn]
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_TODO
	return
  .no_source:
	lea	rdi, [drv_m_nosrc]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
	lea	rdi, [drv_m_hint]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return
  .no_memory:
	mov	r14d, eax
	neg	r14d
	lea	rdi, [drv_m_nomem]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	mov	r8d, r14d
	call	drv_msgu
  .host:
	mov	eax, DRV_EXIT_HOST
	return
endp

; drv_main(argc, argv) -> eax = the process exit status. THE driver entry.
; `r15` already points at a zeroed `DrvCtx` whose pointer fields
; compiler/x86_64/exsc.asm has filled in -- see docs/asm-conventions.md,
; "1.2 The r15 pin".
;
; argv is indexed strictly below argc, so the NULL that terminates it -- and
; the envp array immediately after it -- are never touched. §9.3: *"Reads no
; environment variables."*
proc drv_main, argc, argv
	uses	rbx, r12
	locals
		slot cn, dq
	endl
	mov	dword [r15 + DrvCtx.diagmode], DIAG_MODE_TEXT
	mov	dword [r15 + DrvCtx.cmd], DRV_CMD_NONE

	cmp	qword [argc], 2
	jl	.no_subcommand

	mov	rax, [argv]
	mov	r12, [rax + 8]		; argv[1]
	mov	rdi, r12
	call	drv_cstrlen
	mov	[cn], rax
	mov	rdi, r12
	mov	rsi, rax
	call	drv_cmd_lookup
	cmp	eax, DRV_CMD_NONE
	je	.unknown_subcommand
	mov	[r15 + DrvCtx.cmd], eax
	cmp	eax, DRV_CMD_AEDIFICA
	jne	.stub

	mov	rdi, [argc]
	mov	rsi, [argv]
	call	drv_aedifica
	return

  .stub:
	mov	edi, [r15 + DrvCtx.cmd]
	call	drv_stub
	return

  .no_subcommand:
	lea	rdi, [drv_m_nosub]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
	call	drv_usage
	mov	eax, DRV_EXIT_USAGE
	return

  .unknown_subcommand:
	lea	rdi, [drv_m_unkcmd]
	mov	rsi, r12
	mov	rdx, [cn]
	xor	rcx, rcx
	call	drv_msg
	call	drv_usage
	mov	eax, DRV_EXIT_USAGE
	return
endp
