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
; tokenizer, parses under §8.6, builds §9.1's typed AST, runs Stage 2's
; checker, and emits whatever diagnostics all of that produced. GIVEN `-o OUT`
; AND NO DIAGNOSTIC, it then lowers the typed tree to the SSA IR (`lwr_module`,
; compiler/x86_64/lower/) and writes §12's OUT: the whole fasmg source file
; `bfa_emit_program` builds (compiler/x86_64/backend_fasmg/program.inc,
; docs/design/runtime.md section 2.1). That is §16 Stage 3's reference backend,
; end to end.
;
; SOURCE MAY BE REPEATED, AND THE FILES ARE ONE MODULE (§12): *"The files
; named form one compilation unit -- one module in §10's sense: one `ego`, one
; interface hash, one artifact. Their order on the command line is the order
; of their items and is part of the input."* The shape of that here, because
; it is spread over four routines and is easy to read as four unrelated
; things:
;
;   1. driver/io.inc reads every file, in order, back to back into ONE buffer
;      (`drv_slurp_unit`), and records where each one landed in `DrvCtx.srcs`.
;   2. this file lexes each file SEPARATELY -- §8.1 is a property of a file,
;      and two files must never lex into one token -- into ONE token vector,
;      then rewrites what each `lex_run` appended by that file's base
;      (`__drv_globalize`) and drops its trailing `TOK_EOF` unless it was the
;      last file (`__drv_drop_eof`, because `__cst_module` stops at the first
;      EOF it sees).
;   3. ONE `cst_parse`, ONE `ast_from_cst`, ONE `ast_verify_stage1`, ONE
;      `chk_run` then run over that, with the lexer pointed at the whole unit
;      buffer -- every span past this point is an offset into it.
;   4. `drv_emit_diag` maps a diagnostic back to the file its offset falls in
;      (`__drv_localize`) immediately before rendering it, so a diagnostic in
;      the third file prints the third file's name and the third file's own
;      line number.
;
; NOTHING SORTS. A different order is a different input, and §9.3's
; byte-identity is promised for the same command line, not across a
; reordering -- §12 says so in as many words.
;
; FOR ONE SOURCE THIS IS ALL THE IDENTITY. The base is 0, the globalize step
; returns immediately, no EOF is dropped, and the localize step restores the
; path and bytes the lexer already attached -- so a one-file invocation emits
; byte-for-byte what it emitted before any of this existed.
;
; `-o OUT` IS OPTIONAL HERE, AND THAT IS A DECISION, NOT AN OVERSIGHT. §12
; pins *"the minimum build invocation"* as
; `exsc aedifica --hospes TRIPLE SOURCE -o OUT`, and that whole command is now
; honoured end to end: given `-o` and a unit the checker accepts, OUT is
; written and the status is 0. Dropping `-o` asks for no artifact, so §12's
; *minimum build* invocation is not the command being given, and the front end
; running clean is the whole of the requested work: exit 0 as well, with a note
; saying no object was requested. Two different jobs, both completed, both 0.
;
; WHAT STILL EXITS NONZERO ON THE `-o` PATH, so that nothing reports success
; for work it did not do: any diagnostic at all exits 1 and writes NOTHING --
; the artifact is never opened, let alone truncated, because `.report` only
; reaches the build on an empty diagnostic vector; a `--hospes` this build
; cannot target exits 4 before the source is even opened; a failed
; `openat`/`write`/`close` on OUT exits 3 and says which syscall and which
; errno.
;
; §12: *"`OUT` is text, and `exsc` never assembles it."* This file writes
; bytes to a file and stops. There is no `execve` on the syscall allowlist and
; there will not be one: locating an assembler by `PATH` is exactly the
; ambient state §9.3 exists to remove. `fasmg OUT BIN` is the build's step.
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

; The LOWERING's scratch arena (`lwr_module`'s fourth argument), created only
; on the `-o` path. Fixed capacity, unlike the compilation arena, because what
; it holds is bounded by ONE FUNCTION and not by the unit: `lwr_module` resets
; it at every function (docs/design/lowering.md 2.1), so the high-water mark is
; the largest single function's Braun state -- `defs`, `incomplete`, `vars`,
; `scopes`, `loops` -- and not the sum over the module.
;
; 64 MiB against lowering.md section 4's "scratch per function well under 1 MiB
; except for a pathological single function". That is 64x a figure that
; document marks `[OPEN]` -- arithmetic on unmeasured estimates -- so the
; headroom is deliberate and the number is not a measurement. MEASURED is only
; this: after the hello world's last function, `Arena.cur - Arena.base` on this
; arena is 13,416 bytes. That is ONE function's state and not the module's
; maximum, because the reset already happened at every function boundary --
; which is the property that makes the number worth having at all.
; Over-running it is `arena_alloc`'s `rassert` -- SIGILL, exit 132 -- not
; silent corruption, and the reservation is anonymous `mmap`, so the 64 MiB
; costs address space and no pages (driver/io.inc's own argument for the same
; trade).
DRV_LWRSCR_CAP  = 64 * 1024 * 1024
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
drv_m_todo2  db 'only `aedifica` does anything: it reads the SOURCE files, applies spec 8.1, lexes them under spec 8.4, parses them under spec 8.6 into the spec 9.1 CST, builds the spec 9.1 typed AST, runs the checker, and -- given -o OUT -- lowers that tree and writes the fasmg source of the reference backend. Everything else here exits 4.',0
drv_m_todolsp db '`lsp` is marked [OPEN] Deferred in spec 18.2 -- it needs the lossless CST, which does not exist.',0
drv_m_nohospes db 'aedifica: --hospes TRIPLE is required. Spec 9.5: exsc with no --hospes is an error, and there is no default-to-build-platform.',0
drv_m_emptyhospes db 'aedifica: --hospes: TRIPLE must not be empty',0
; The `--hospes` table's three messages. Its rows live in cli.inc beside the
; value spellings; these say what each refusal is. The old single message
; said "there is no second backend to pick", which stopped being true the
; day `--emitte c` existed -- text is not permanent (spec 8.3), and this is
; the amendment.
drv_m_hospesunsup db 'aedifica: --hospes: not a value this build accepts. The closed table is x86_64-linux (both backends), riscv64-linux (--emitte c only) and mips64-none-o64 (not yet, see below). Spec 9.5 forbids guessing one. Got: ',0
drv_m_hospesnoc db 'aedifica: --hospes: this value names no reference backend, so it needs --emitte c. The reference backend emits x86-64 fasmg text and targets x86_64-linux alone. Got: ',0
drv_m_hospeso64 db 'aedifica: --hospes: mips64-none-o64 is a row of the design table that this build cannot yet honour. Its 32-bit `ptr` needs lower/ty.inc, which interns ref/refc at width 64 unconditionally, and backend_fasmg/ir.inc`s bfa_ty_ptr -- other trees, and one of them a file the C backend reuses unchanged. Scheduled as C4 (docs/design/c-backend.md section 7). Got: ',0
; The driver's `--hospes` row travels into `bfc_emit_unit` unchanged, so the
; two enumerations must agree value for value. Asserted at ASSEMBLY time
; rather than trusted: they are declared in two files, one of which
; (backend_c/) is meant to be usable without driver/ at all, and a silent
; renumbering would emit a unit asserting the wrong pointer width.
assert DRV_HOSPES_NONE    = BFC_HOSPES_NONE
assert DRV_HOSPES_X86_64  = BFC_HOSPES_X86_64
assert DRV_HOSPES_RISCV64 = BFC_HOSPES_RISCV64
assert DRV_HOSPES_MIPS64  = BFC_HOSPES_MIPS64

drv_m_emitcnoout db 'aedifica: --emitte c requires -o OUT. Unlike tokens, cst and ast, it emits an ARTIFACT rather than a stage dump: spec 12 makes OUT the emitted text exsc never assembles, and the reproducibility gates diff OUT files. A C translation unit on stdout would be the one emitted text with no file to diff.',0
drv_m_nosrc  db 'aedifica: no SOURCE given',0
drv_m_trunc  db 'a diagnostic did not fit its buffer and was truncated; the output above is incomplete. That is a limit of this compiler, not of the source.',0
drv_m_okpfx  db 'aedifica: ',0
drv_m_okbare db ': the front end accepts this source -- it lexes, parses, and builds a typed AST. No object was requested, so none was written.',0
; The `-o` success note. It names OUT rather than the first SOURCE, because
; OUT is the thing that now exists that did not before, and it says what OUT
; is: spec 12's EMITTED TEXT, which `exsc` writes and never assembles. The
; trailing number is `drv_msgu`'s, and is the byte count -- the one fact a
; caller can check without opening the file.
drv_m_wrote  db 'aedifica: wrote ',0
drv_m_wrote2 db ' -- fasmg source, not a binary: assemble it with `fasmg OUT BIN` (spec 12, OUT is emitted text and exsc never assembles it). Bytes: ',0
drv_m_wrote2c db ' -- one C11 translation unit of this module`s functions, not a binary and not fasmg source: compile it with your own C compiler (spec 12, OUT is emitted text and exsc never compiles it; spec 12 also puts execve on no allowlist). It is a LIBRARY: it has no entry point and imports one symbol, `_Noreturn void exsrt_abortus(unsigned kind)`. Bytes: ',0
drv_m_lwrfail db 'aedifica: the lowering refused this module and no object was written. That is a compiler bug, not a fact about this source (spec 16 Stage 3).',0
drv_m_okunit db ' and the other SOURCE files given with it: the front end accepts this compilation unit -- it lexes and parses all of it and builds ONE typed AST from it (spec 12: the files named form one module). No object was requested, so none was written. Files in the unit: ',0
drv_m_emitgate db ': --emitte was given, but this source failed the spec 8.1 source-hygiene gate, so it was never tokenized -- there is no token stream, no CST and no typed AST to dump. Nothing was written to stdout. See the diagnostic below.',0

; __drv_src_at(drs_off) -> rax = the `DrvSrc` whose bytes contain the unit
; offset `drs_off`, or 0 if no SOURCE has been recorded at all. Linear over
; `DrvCtx.nsrc`, which is at most DRV_SOURCES_MAX and is walked in
; command-line order, so nothing here depends on a pointer value (CLAUDE.md).
;
; The first entry with `off < sbase + slen` wins. That skips an EMPTY file
; (`slen` 0) rather than letting it claim the boundary offset it shares with
; the next file, and an offset past the last file's last byte -- the unit's
; final `TOK_EOF` sits at exactly `total` -- clamps to the last entry rather
; than resolving to nothing. Cannot fail; leaves CF clear.
proc __drv_src_at, drs_off
	uses	rbx, r12
	locals
	endl
	mov	ecx, [r15 + DrvCtx.nsrc]
	test	ecx, ecx
	jz	.none
	mov	r12, [drs_off]
	xor	rbx, rbx
  .loop:
	cmp	ebx, [r15 + DrvCtx.nsrc]
	jae	.last
	mov	rdi, rbx
	call	drv_src_ptr
	mov	rcx, [rax + DrvSrc.sbase]
	add	rcx, [rax + DrvSrc.slen]
	cmp	r12, rcx
	jb	.done
	inc	rbx
	jmp	.loop
  .last:
	mov	eax, [r15 + DrvCtx.nsrc]
	dec	eax
	mov	rdi, rax
	call	drv_src_ptr
  .done:
	return
  .none:
	xor	eax, eax
	return
endp

; __drv_localize(drs_dg) -- rewrite one `Diag` from unit coordinates into the
; coordinates of the file it is actually in.
;
; WHY THIS EXISTS AT ALL, since it is the load-bearing half of multi-file
; support. Every span in this compilation is an offset into the ONE buffer
; driver/io.inc read the unit's files into: the lexer's, because
; driver/run.inc rewrites each file's spans by that file's base (below); the
; parser's, because `cst_parse` works off `Lexer.src_ptr`, which is that
; buffer; the checker's, because `ast_from_cst` stamps one `file_id` on every
; node and `chk_set_source` takes one source. A diagnostic therefore arrives
; here knowing its offset in the UNIT and nothing about which file that is.
; This routine is the only place that knows, and it applies the mapping at
; the last possible moment -- on a COPY, so the record in `DrvCtx.diags` is
; never mutated and rendering it twice gives the same bytes twice, which is
; what diag/render.inc promises about its own inputs.
;
; A DIAGNOSTIC WITH NO SOURCE ATTACHED IS LEFT ALONE (`src_len` 0). Attaching
; one here would give a snippet to a record that never claimed to have one.
;
; A RELATED SPAN IN A DIFFERENT FILE IS DROPPED, and this is the one place
; this composition loses information. `diag_render_text` renders `rel`'s
; gutter block against the record's single `src_ptr` (diag/render.inc), so a
; related span in another file would be rendered as a line of the CARET's
; file -- a line that has nothing to do with it. §8.3 makes a diagnostic's
; span the part a reader is entitled to trust, so showing the wrong line is
; worse than showing no second block: `DIAG_F_REL` is cleared and the caret,
; the code and the note still render. This is reachable in practice -- an
; `EXS-E0202` for a `{` opened in the unit's first file and never closed
; reports at the unit's end, which is the last file -- and the real fix is a
; second source pair on `Diag` (or a file table diag/ can consult), which is
; diag/'s change to make, not driver/'s to force. REPORTED, not done.
proc __drv_localize, drs_dg
	uses	rbx, r12
	locals
	endl
	mov	rbx, [drs_dg]
	cmp	qword [rbx + Diag.src_len], 0
	je	.leave

	mov	edi, [rbx + Diag.span.start]
	call	__drv_src_at
	test	rax, rax
	jz	.leave
	mov	r12, rax

	mov	eax, [rbx + Diag.span.start]
	sub	rax, [r12 + DrvSrc.sbase]
	mov	[rbx + Diag.span.start], eax
	mov	eax, [r12 + DrvSrc.sfid]
	mov	[rbx + Diag.span.file_id], eax
	mov	rax, [r12 + DrvSrc.sptr]
	mov	[rbx + Diag.src_ptr], rax
	mov	rax, [r12 + DrvSrc.slen]
	mov	[rbx + Diag.src_len], rax
	mov	rax, [r12 + DrvSrc.spath]
	mov	[rbx + Diag.path_ptr], rax
	mov	rax, [r12 + DrvSrc.spathn]
	mov	[rbx + Diag.path_len], rax

	test	dword [rbx + Diag.flags], DIAG_F_REL
	jz	.norel
	mov	edi, [rbx + Diag.rel.start]
	call	__drv_src_at
	cmp	rax, r12
	jne	.dropre
	mov	eax, [rbx + Diag.rel.start]
	sub	rax, [r12 + DrvSrc.sbase]
	mov	[rbx + Diag.rel.start], eax
	mov	eax, [r12 + DrvSrc.sfid]
	mov	[rbx + Diag.rel.file_id], eax
	jmp	.norel
  .dropre:
	and	dword [rbx + Diag.flags], not DIAG_F_REL
  .norel:

	cmp	dword [rbx + Diag.fix.kind], DIAG_FIX_NONE
	je	.leave
	; A fix is an EDIT, and `exsc emenda` applies it to the file its
	; `file_id` names -- so it is resolved against its OWN offset rather
	; than assumed to be in the caret's file.
	mov	edi, [rbx + Diag.fix.edit.start]
	call	__drv_src_at
	test	rax, rax
	jz	.leave
	mov	ecx, [rbx + Diag.fix.edit.start]
	sub	rcx, [rax + DrvSrc.sbase]
	mov	[rbx + Diag.fix.edit.start], ecx
	mov	ecx, [rax + DrvSrc.sfid]
	mov	[rbx + Diag.fix.edit.file_id], ecx
  .leave:
	return
endp

; drv_emit_diag(dg, mode) -> eax = 1 if the rendering was truncated, else 0.
; Renders one `Diag` and writes it to stderr. Cannot fail; leaves CF clear --
; a failed write to stderr is swallowed by `drv_say` for the reason its own
; comment gives.
;
; THE RECORD IS COPIED AND LOCALIZED FIRST -- see `__drv_localize`, just
; above. For a one-file unit that mapping is the identity (base 0, and the
; file's own path and bytes are what the lexer already attached), so this
; produces byte-for-byte what it produced before the driver could take more
; than one SOURCE.
;
; The renderer is handed DRV_DIAG_CAP-1, not DRV_DIAG_CAP, so the JSON Lines
; newline always has somewhere to go: a record that exactly filled the buffer
; and then lost its terminator would silently glue two objects together, and
; the truncation flag would not say so.
proc drv_emit_diag, dg, mode
	uses	rbx
	locals
		slot tr,      dq
		slot dg2,     sizeof.Diag
		slot drvdbuf, DRV_DIAG_CAP
	endl
	lea	rdi, [dg2]
	mov	rsi, [dg]
	mov	rcx, sizeof.Diag
	cld
	rep	movsb
	lea	rdi, [dg2]
	call	__drv_localize

	lea	rdi, [dg2]
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

; __drv_globalize(drs_delta, drs_t0, drs_d0) -- add `drs_delta` to every span
; of the tokens from index `drs_t0` on and the diagnostics from index `drs_d0`
; on, so that what one `lex_run` produced over one file's bytes reads as
; offsets into the unit buffer that file was read into.
;
; WHY THE DIAGNOSTICS TOO, when the lexer already attached each one to the
; right file with the right local offset: so that there is exactly ONE
; convention in `DrvCtx.diags`. A vector holding some records in file
; coordinates and others (everything the parser, the AST walk and the checker
; raise, all of which work off the unit buffer) in unit coordinates would need
; `drv_emit_diag` to tell them apart, and the only thing available to tell
; them apart BY is a pointer or a file id the parser does not vary. One
; convention, applied here, undone in `__drv_localize`, and the round trip is
; the identity for a one-file unit because `drs_delta` is then 0.
;
; THE THREE SPANS OF A `Diag` ARE ALL MOVED: the caret, the related span (only
; when `DIAG_F_REL` says it is real -- diag/render.inc), and a fix's edit range
; (only when `fix.kind` says there is one). A `DiagFix`'s `text_ptr` is
; replacement text in an arena, not an offset, and is not touched.
; Cannot fail; leaves CF clear.
proc __drv_globalize, drs_delta, drs_t0, drs_d0
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	r14, [drs_delta]
	test	r14, r14
	jz	.done			; a one-file unit moves nothing

	mov	rbx, [r15 + DrvCtx.toks]
	mov	r12, [drs_t0]
	mov	r13, [rbx + Vec.len]
  .toks:
	cmp	r12, r13
	jae	.toksdone
	mov	rdi, rbx
	mov	rsi, r12
	call	vec_get
	add	dword [rax + Tok.span.start], r14d
	inc	r12
	jmp	.toks
  .toksdone:

	mov	rbx, [r15 + DrvCtx.diags]
	mov	r12, [drs_d0]
	mov	r13, [rbx + Vec.len]
  .diags:
	cmp	r12, r13
	jae	.done
	mov	rdi, rbx
	mov	rsi, r12
	call	vec_get
	add	dword [rax + Diag.span.start], r14d
	test	dword [rax + Diag.flags], DIAG_F_REL
	jz	.norel
	add	dword [rax + Diag.rel.start], r14d
  .norel:
	cmp	dword [rax + Diag.fix.kind], DIAG_FIX_NONE
	je	.nofix
	add	dword [rax + Diag.fix.edit.start], r14d
  .nofix:
	inc	r12
	jmp	.diags
  .done:
	return
endp

; __drv_drop_eof -- remove the `TOK_EOF` `lex_tokenize` just appended.
;
; A `Vec` has no pop (rt/vec.inc: `vec_init`, `vec_push`, `vec_get`, and
; nothing else), and adding one is rt/'s change, not driver/'s -- so this
; decrements `Vec.len` directly, which is the same public field
; `drv_dump_tokens` and `.report` below already read. It `rassert`s that the
; element it is dropping really is an EOF: dropping anything else would delete
; a token of the program, and that is a compiler bug rather than a fact about
; the source. Cannot fail; leaves CF clear.
proc __drv_drop_eof
	uses	rbx
	locals
	endl
	mov	rbx, [r15 + DrvCtx.toks]
	mov	rcx, [rbx + Vec.len]
	rassert	rcx gt 0
	dec	rcx
	mov	rdi, rbx
	mov	rsi, rcx
	call	vec_get
	mov	ecx, [rax + Tok.kind]
	rassert	ecx eq TOK_EOF
	dec	qword [rbx + Vec.len]
	return
endp

; __drv_potestates(drp_t) -> rax = the eleven-bit capability mask
; `bfa_emit_program` wants, bit `k-1` set for spec 4.6 ordinal `k`. That is the
; SAME bit order as `BFA_POTESTAS_MUNDUS`..`BFA_POTESTAS_CRUDUM`
; (backend_fasmg/program.inc) and as `chk_row_atoms`'s own mask
; (checker/rows/intern.inc), and the three agreeing is not a coincidence: all
; three are spec 4.6's list, in spec 4.6's order.
;
; THE CHECKER DOES NOT EXPOSE "THE CAPABILITY CLOSURE OF `initium`", AND THIS
; IS NOT IT. Said first and plainly, because docs/design/runtime.md section 2.1
; says the eleven constants come "from the checker's computed closure of
; initium (spec 10.3)" and no such call exists. What the checker does expose,
; and what was tried:
;
;   - `ChkCtx.mainid` is `initium`'s declaration and `Decl.ty` is its `fn`
;     type, whose `AstType.b` is its row. THIS IS ALWAYS EMPTY AND CANNOT BE
;     THE CLOSURE. Spec 4.7: the entry point "carries no `poscit` and there is
;     nothing to declare"; `checker/rows/compute.inc`'s `__chk_row_seed` reads
;     spec 4.1 rule 5 as "a function is DECLARED when it writes a `poscit`,
;     when it is `publica`, or when it has no body", and `initium` is
;     `publica`, and "declared rows do not grow". So pass 3 seeds `initium`'s
;     row from a `poscit` that spec 4.7 forbids it to have and never widens it.
;     Reading it gives 0 for every program, including one that opens a socket.
;   - `chk_use_count`/`chk_use_at` is the capability-USE list, and it holds
;     only the draws NO FRAME PROVIDED (`CHK_USE_DRAW`/`CHK_USE_CAPTURE`,
;     resolve.inc). The hello world's `sub ambitus = a;` provides `ambitus` in
;     `initium`'s own frame, so the draw it satisfies is not in the list, and
;     the list is empty for exactly the program whose closure is `{Mundus,
;     ambitus}`. It is the wrong list: it exists so pass 3 can raise
;     `EXS-E0421`, not so anyone can enumerate authority.
;   - Pass 3 DOES compute a per-function atom mask -- `ChkRowWork.wrat` -- and
;     that is the closure, transitively, by the fixpoint spec 4.1 rule 5 asks
;     for. It lives in the checker's scratch arena, is not written anywhere
;     that outlives `chk_rows`, and has no accessor. REQUESTED, exactly:
;
;         chk_row_of_decl(chk_c, declid) -> rax = the eleven-bit atom mask
;         pass 3 computed for declaration `declid`, or 0.
;
;     One `__chk_row_g4(wrat, declid)` behind a `proc`, plus somewhere for the
;     vector to live past `chk_rows` -- the tree's arena, as `__chk_fix_text`
;     already does for the same reason. With it, this whole routine becomes
;     `chk_row_of_decl(chk, ChkCtx.mainid) or BFA_POTESTAS_MUNDUS`, and the
;     over-approximation below goes away. checker/ is another agent's tree
;     (CLAUDE.md, Scope); REPORTED, not done.
;
; WHAT THIS DOES INSTEAD, and it is an OVER-APPROXIMATION, said in the code
; rather than in a report nobody reads: it unions every capability atom that
; appears anywhere in the module's INTERNED TYPE TABLE. `Ast.types` is
; interned by content and an entry exists only because something in this
; module has that type, so an atom reaches this mask iff some value, some
; parameter, some field or some function row in the module is of that atom's
; capability type. For `examples/`'s hello world that is exactly `{Mundus,
; ambitus}`: `m: Mundus`, `firma a = m.ambitus()`, `sub ambitus = a`, and
; `Scriptor.a : ambitus`.
;
; It is an over-approximation in one direction and one direction only, which
; is the direction that MATTERS for spec 10.3's audit and is why it is
; flagged rather than shipped quietly: a module that mentions an atom in a
; type without ever reaching it still grants it, so the audit could pass a
; binary holding a routine the program cannot call. It CANNOT under-grant --
; a call to a prelude routine is a call to a declaration whose parameter
; carries the atom's type, so no reachable authority is missing from the
; table -- so the emitted program never lacks a routine it needs, which is
; the failure that would show up as a broken build rather than as a weakened
; contract. `[UNTESTED]` beyond the hello world and the two fixtures below it.
; __drv_cap_of_ty(drp_t2, drp_ty) -> rax = the eleven-bit mask ONE type id
; contributes: a capability type contributes its own atom, an `fn` or `dyn`
; type contributes its row's atoms, and everything else contributes nothing.
; `drp_base` is `ast_cap_base`'s answer, passed in rather than re-derived per
; type. Cannot fail; leaves CF clear.
proc __drv_cap_of_ty, drp_t2, drp_ty, drp_base
	uses	rbx
	locals
	endl
	xor	eax, eax
	mov	rcx, [drp_ty]
	test	rcx, rcx
	jz	.out			; untyped: Stage 2 fills every live node,
					; so this is a node Stage 2 never reached
	mov	rbx, [drp_t2]
	mov	rdi, rbx
	mov	rsi, rcx
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_CAP
	je	.cap
	cmp	ecx, AST_TY_FN
	je	.row
	cmp	ecx, AST_TY_DYN
	je	.row
	xor	eax, eax
	return
  .cap:
	; `AstType.a` is the atom's own declaration (ast/kinds.inc, AST_TY_CAP),
	; and `ast_cap_base` + k - 1 IS that declaration for §4.6 ordinal k --
	; so the subtraction is the ordinal, zero-based, which is the bit index.
	mov	ecx, [rax + AstType.a]
	sub	rcx, [drp_base]
	cmp	rcx, 0
	jl	.none
	cmp	rcx, AST_CAP_COUNT
	jge	.none
	mov	rax, 1
	shl	rax, cl
	return
  .row:
	; An `fn` or `dyn` type carries a row id in `AstType.b`. Pass 3
	; re-interned these (checker/rows/compute.inc's D1), so this reads what
	; the fixpoint left rather than what the source wrote.
	mov	ecx, [rax + AstType.b]
	test	ecx, ecx
	jz	.none
	mov	rdi, rbx
	mov	rsi, rcx
	call	chk_row_atoms
	return
  .none:
	xor	eax, eax
  .out:
	return
endp

; __drv_potestates(drp_t) -> rax = the eleven-bit capability mask
; `bfa_emit_program` wants, bit `k-1` set for spec 4.6 ordinal `k`. That is the
; SAME bit order as `BFA_POTESTAS_MUNDUS`..`BFA_POTESTAS_CRUDUM`
; (backend_fasmg/program.inc) and as `chk_row_atoms`'s mask
; (checker/rows/intern.inc), and the three agreeing is not a coincidence: all
; three are §4.6's list in §4.6's order.
;
; THE CHECKER DOES NOT EXPOSE "THE CAPABILITY CLOSURE OF `initium`", AND THIS
; IS NOT IT. Said first and plainly, because docs/design/runtime.md section 2.1
; says the eleven constants come "from the checker's computed closure of
; initium (spec §10.3)" and no such call exists. Three things were tried; two
; of them are traps and are recorded so nobody re-finds them:
;
;   - `ChkCtx.mainid` is `initium`'s declaration and `Decl.ty` is its `fn`
;     type, whose `AstType.b` is its row. THAT ROW IS ALWAYS EMPTY AND CANNOT
;     BE THE CLOSURE. §4.7: the entry point "carries no `poscit` and there is
;     nothing to declare"; `checker/rows/compute.inc`'s `__chk_row_seed` reads
;     §4.1 rule 5 as "a function is DECLARED when it writes a `poscit`, when it
;     is `publica`, or when it has no body", `initium` is `publica`, and
;     "declared rows do not grow". So pass 3 seeds `initium`'s row from a
;     `poscit` §4.7 forbids it to have and never widens it. Reading it answers
;     0 for every program ever written, including one that opens a socket.
;   - `chk_use_count`/`chk_use_at` is the capability-USE list, and it holds
;     only the draws NO FRAME PROVIDED (`CHK_USE_DRAW`/`CHK_USE_CAPTURE`,
;     checker/resolve/resolve.inc). The hello world's `sub ambitus = a;`
;     provides `ambitus` in `initium`'s own frame, so the draw it satisfies is
;     not in the list -- the list is empty for exactly the program whose
;     closure is `{Mundus, ambitus}`. It is the wrong list: it exists so pass 3
;     can raise `EXS-E0421`, not so anyone can enumerate authority.
;   - Pass 3 DOES compute the closure -- a per-function atom mask, by the
;     transitive fixpoint §4.1 rule 5 asks for, in `ChkRowWork.wrat`. It lives
;     in the checker's scratch arena, is written nowhere that outlives
;     `chk_rows`, and has no accessor. REQUESTED, exactly:
;
;         chk_row_of_decl(chk_c, declid) -> rax = the eleven-bit atom mask
;         pass 3 computed for declaration `declid`, or 0 if it computed none.
;
;     One `__chk_row_g4(wrat, declid)` behind a `proc`, plus somewhere for that
;     vector to outlive `chk_rows` -- the tree's arena, which
;     `__chk_fix_text` already uses for the same reason. With it this routine
;     becomes `chk_row_of_decl(chk, ChkCtx.mainid) or BFA_POTESTAS_MUNDUS` and
;     the over-approximation below goes away. checker/ is another agent's tree
;     (CLAUDE.md, Scope): REPORTED, not done.
;
; WHAT THIS DOES INSTEAD: it unions the capability atoms named by the type of
; every DECLARATION and every NODE in the module -- `Decl.ty` and `Node.ty`,
; both filled by Stage 2 pass 2 -- plus the row of every `fn`/`dyn` type
; either of them carries. An atom therefore reaches this mask iff some value,
; parameter, binding, field or function row IN THIS MODULE is of that atom's
; capability type. For the hello world that is exactly `{Mundus, ambitus}`:
; `m: Mundus`, `firma a = m.ambitus()`, `sub ambitus = a`, and the `Scriptor`
; whose row is `{ambitus}`.
;
; THE ELEVEN ATOM DECLARATIONS THEMSELVES ARE SKIPPED, and that is not
; tidiness -- it is the whole difference between this working and being
; vacuous. `ast_cap_push` appends all eleven §4.6 atoms to EVERY tree, and
; Stage 2 gives each one a capability type, so the interned type table
; (`Ast.types`) holds all eleven capability types in every module ever
; compiled. Scanning that table was the first version of this routine and it
; emitted `EXS_POTESTAS_* = 1` eleven times for the hello world -- a mask that
; grants everything, which would have made §10.3's audit meaningless while
; looking like it worked. Measured, then fixed. `AST_D_CAPATOM` declarations
; are the only ones skipped, and a program that actually uses an atom reaches
; it through some OTHER declaration or node.
;
; IT IS STILL AN OVER-APPROXIMATION, in one direction, and the direction is
; the one §10.3 cares about: a module that mentions an atom in a type without
; ever reaching it still grants it, so the audit can pass a binary carrying a
; routine the program cannot call. It cannot UNDER-grant -- reaching a prelude
; routine means calling a declaration whose parameter carries the atom's type
; -- so the emitted program never lacks a routine it needs. `[UNTESTED]`
; beyond the hello world and `tests/unit/driver_emit.asm`.
proc __drv_potestates, drp_t
	uses	rbx, r12, r13
	locals
		slot drpbase, dq
		slot drpmask, dq
		slot drpn,    dq
	endl
	mov	qword [drpmask], 0
	mov	rbx, [drp_t]
	mov	rdi, rbx
	call	ast_cap_base		; `Mundus`'s declaration id; atom `k` is
					; `base + k - 1` (ast/build.inc)
	mov	[drpbase], rax
	test	rax, rax
	jz	.done			; a tree with no atoms grants nothing

	; ---- every declaration's type, except the atoms' own ----------------
	mov	rdi, rbx
	call	ast_decl_count
	mov	[drpn], rax
	mov	r12, 1
  .dloop:
	cmp	r12, [drpn]
	ja	.ddone
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_CAPATOM
	je	.dnext			; see this proc's header: every tree has
					; all eleven, so counting them grants all
					; eleven
	mov	r13d, [rax + AstDecl.ty]
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, [drpbase]
	call	__drv_cap_of_ty
	or	[drpmask], rax
  .dnext:
	inc	r12
	jmp	.dloop
  .ddone:

	; ---- every node's type ----------------------------------------------
	; A capability can reach an expression without reaching a declaration:
	; `m.ambitus()`'s own `Call` node is of capability type before anything
	; is bound to it, and a `sub` whose right-hand side is a call has the
	; atom on the node rather than on a name.
	mov	rdi, rbx
	call	ast_node_count
	mov	[drpn], rax
	mov	r12, 1
  .nloop:
	cmp	r12, [drpn]
	ja	.done
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	mov	r13d, [rax + AstNode.ty]
	mov	rdi, rbx
	mov	rsi, r13
	mov	rdx, [drpbase]
	call	__drv_cap_of_ty
	or	[drpmask], rax
	inc	r12
	jmp	.nloop

  .done:
	mov	rax, [drpmask]
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
		slot drvk,   dq		; the file being lexed
		slot drvt0,  dq		; DrvCtx.toks length before it
		slot drvd0,  dq		; DrvCtx.diags length before it
		slot drvent, dq		; -> its DrvSrc
		slot drvmdl, dq		; -> BfaModule, the lowered IR (`-o` only)
		slot drvpot, dq		; the eleven-bit capability mask
		slot drvhos, dd		; the accepted DRV_HOSPES_* row
		slot drvop,  dq		; -> OUT's bytes }  bfa_emit_program's
		slot drvol,  dq		; ... and how many }  two return values
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
	cmp	dword [r15 + DrvCtx.nsrc], 0
	je	.no_source

	; ---- the target: one row of `--hospes`'s closed table ---------------
	; What used to be one compare against `x86_64-linux` is now the small
	; table that compare's own comment anticipated ("a second accepted
	; triple, the day one exists, is a small table here (triple bytes ->
	; ptrbits) in place of the single compare below -- not a redesign").
	; The day has arrived: docs/design/c-backend.md D2 gives the table and
	; the C backend is the second backend that compare said did not exist.
	;
	; A row is accepted iff SOME backend in this build can honour it:
	;   x86_64-linux     both backends
	;   riscv64-linux    the C backend only, so `--emitte c` is required
	;   mips64-none-o64  refused outright -- its 32-bit `ptr` needs
	;                    lower/ty.inc and ir.inc, which are other trees
	; Each refusal is a USAGE error, exit 4, no EXS-E code: the fault is in
	; the invocation, not in the source (spec 9.3), which is the same class
	; `drv_m_hospesunsup` always was.
	mov	dword [drvhos], DRV_HOSPES_NONE
	mov	rdi, [r15 + DrvCtx.hospes]
	mov	rsi, [r15 + DrvCtx.hospesn]
	lea	rdx, [drv_v_x86_64]
	call	drv_arg_is
	test	eax, eax
	jz	.not_x86
	mov	dword [drvhos], DRV_HOSPES_X86_64
	jmp	.hospes_known
  .not_x86:
	mov	rdi, [r15 + DrvCtx.hospes]
	mov	rsi, [r15 + DrvCtx.hospesn]
	lea	rdx, [drv_v_riscv64]
	call	drv_arg_is
	test	eax, eax
	jz	.not_riscv
	mov	dword [drvhos], DRV_HOSPES_RISCV64
	; this row names no reference backend, so it needs --emitte c
	cmp	dword [r15 + DrvCtx.emitte], DRV_EMIT_C
	jne	.hospes_needs_c
	jmp	.hospes_known
  .not_riscv:
	mov	rdi, [r15 + DrvCtx.hospes]
	mov	rsi, [r15 + DrvCtx.hospesn]
	lea	rdx, [drv_v_mips64]
	call	drv_arg_is
	test	eax, eax
	jnz	.hospes_o64
	jmp	.hospes_unsupported
  .hospes_known:

	; ---- --emitte c requires -o OUT --------------------------------------
	; Unlike tokens/cst/ast, `c` is an artifact rather than a dump: it is
	; written to OUT and nothing goes to stdout, because spec 12 says OUT is
	; emitted text exsc never assembles and the reproducibility gates diff
	; OUT files. A C unit on stdout would be the one emitted text with no
	; file to diff (c-backend.md D2).
	cmp	dword [r15 + DrvCtx.emitte], DRV_EMIT_C
	jne	.emitte_c_ok
	cmp	qword [r15 + DrvCtx.outpath], 0
	je	.emitte_c_no_out
  .emitte_c_ok:

	; ---- the source: every file of the §12 unit, into one buffer ---------
	lea	rdi, [srclen]
	call	drv_slurp_unit
	test	rax, rax
	jz	.host			; drv_slurp_unit has already said why
	mov	[srcbuf], rax

	; ---- the front end ---------------------------------------------------
	; ONE token vector and ONE diagnostic vector for the WHOLE unit.
	; lexer/source.inc says this is supported by construction: `lex_init`
	; "does not initialise them, because a caller may legitimately want one
	; vector to outlive several files."
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

	; ---- §8.1 and §8.4, ONCE PER FILE, into that one vector --------------
	; Per file, because both are properties of a FILE: a BOM is `EXS-E0101`
	; at offset 0 and `EXS-E0103` anywhere else (§8.1), NFC segmentation
	; must not run across a boundary, and two files must never lex into one
	; token -- `finis` at the end of one and `functio` at the start of the
	; next are two tokens, not `finisfunctio`. Each file therefore gets its
	; own `lex_set_source` (its own `Span.file_id`, its own §8.1 gate) and
	; its own `lex_run`, and `__drv_globalize` then rewrites what that call
	; appended into offsets within the unit buffer, which is what the parser
	; below needs and what `__drv_localize` maps back at render time.
	mov	qword [drvk], 0
  .lexfile:
	mov	rax, [drvk]
	cmp	eax, [r15 + DrvCtx.nsrc]
	jae	.lexed
	mov	rdi, [drvk]
	call	drv_src_ptr
	mov	[drvent], rax
	mov	rcx, [r15 + DrvCtx.toks]
	mov	rcx, [rcx + Vec.len]
	mov	[drvt0], rcx
	mov	rcx, [r15 + DrvCtx.diags]
	mov	rcx, [rcx + Vec.len]
	mov	[drvd0], rcx

	mov	rdi, [r15 + DrvCtx.lx]
	mov	rax, [drvent]
	mov	rsi, [rax + DrvSrc.sptr]
	mov	rdx, [rax + DrvSrc.slen]
	mov	rcx, [rax + DrvSrc.spath]
	mov	r8,  [rax + DrvSrc.spathn]
	call	lex_set_source
	mov	rax, [r15 + DrvCtx.lx]
	mov	ecx, [rax + Lexer.file_id]	; an INTERN id of the path, not a
						; descriptor (lexer/source.inc)
	mov	rax, [drvent]
	mov	[rax + DrvSrc.sfid], ecx

	mov	rdi, [r15 + DrvCtx.lx]
	call	lex_run			; CF/eax: the FIRST code, if any
	jc	.gate_failed		; §8.1 IS A GATE. A file that failed it
					; was never tokenized (lexer/source.inc),
					; so there is no token stream to parse
					; and nothing a parse could add but a
					; cascade of diagnostics about bytes
					; the source policy already rejected.
					; The unit stops here; whatever earlier
					; files already reported is still
					; reported below.

	mov	rax, [drvent]
	mov	rdi, [rax + DrvSrc.sbase]
	mov	rsi, [drvt0]
	mov	rdx, [drvd0]
	call	__drv_globalize

	; Every `lex_tokenize` ends its file with a `TOK_EOF` (lexer/lex.inc),
	; and `__cst_module` stops at the first one it sees -- so an EOF in the
	; middle of the unit would end the module at the end of the first file.
	; All but the last are dropped here. Nothing is lost with them: an EOF
	; token is zero-width, and the trailing trivia it was standing in front
	; of is picked up as the gap before the next file's first token.
	mov	rax, [drvk]
	inc	rax
	cmp	eax, [r15 + DrvCtx.nsrc]
	jae	.keep_eof
	call	__drv_drop_eof
  .keep_eof:
	inc	qword [drvk]
	jmp	.lexfile
  .lexed:

	; ---- the unit is one text from here down -----------------------------
	; `cst_parse` reads `Lexer.src_ptr` for every leaf's bytes and for every
	; byte of trivia between two tokens, so it is pointed at the whole unit
	; buffer, whose offsets are exactly what the spans now carry. The path
	; and `file_id` it also picks up here are the FIRST file's, and they are
	; what every diagnostic raised past this point carries -- which is why
	; `drv_emit_diag` resolves a diagnostic's file from its OFFSET rather
	; than from its `file_id` (see `__drv_localize`).
	mov	rdi, [r15 + DrvCtx.lx]
	mov	rsi, [srcbuf]
	mov	rdx, [srclen]
	mov	rcx, [r15 + DrvCtx.srcpath]
	mov	r8,  [r15 + DrvCtx.srcpathn]
	call	lex_set_source

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
	; `--emitte c` writes a LIBRARY: one translation unit of functions,
	; with no entry point and no runtime (docs/design/c-backend.md D1). So
	; `-o` does not mean "a program was asked for" on that path and a
	; missing `initium` is not EXS-E0424. This closes spec 4.7's [OPEN]
	; sentence for the C target only -- "no library artifact exists for the
	; reference backend, so -o on a module without initium ... is
	; EXS-E0424" -- and the reference's half of it stays true and stays
	; [OPEN], which is why the flag is suppressed HERE rather than the
	; sentence being deleted.
	cmp	dword [r15 + DrvCtx.emitte], DRV_EMIT_C
	je	.chk_nomain
	mov	r8, CHK_F_PROGRAM		; -o was given: a PROGRAM was asked
					; for (spec §4.1 rule 2), so a missing
					; `initium` is `EXS-E0424`. Without -o
					; this is a library and its absence is
					; nothing.
  .chk_nomain:
	call	chk_init
	; `chk_set_source` TAKES ONE SOURCE AND ONE PATH, and a §12 unit has N
	; of each. What it is given is the whole unit buffer and the FIRST
	; file's path, because that is the only shape the call has. Those
	; values reach exactly one place -- `diag_attach_source` on every
	; diagnostic the checker appends (checker/resolve/resolve.inc) -- and
	; `drv_emit_diag` overwrites both from the file the diagnostic's OFFSET
	; falls in before rendering, so no checker diagnostic is rendered
	; against the wrong file's bytes. Said plainly because the honest
	; version matters: if the driver did NOT localize, a checker diagnostic
	; in the second file would print the second file's line NUMBER against
	; the first file's NAME and the first file's text. What the checker
	; should take is the table (`DrvCtx.srcs`), or a per-node `file_id` from
	; `ast_from_cst`; both are other agents' trees. REPORTED, not done --
	; and tests/unit/driver_sources.asm asserts the rendered path of a
	; checker diagnostic raised in the SECOND file, so the day this
	; localization is removed the fixture says so.
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
	; FIRST, the same globalize every other file got. `lex_run` raised its
	; §8.1 diagnostics against THIS file's bytes with THIS file's offsets,
	; and they are in the one vector `.report` walks, so they need the same
	; rewrite the tokens of the files before them got -- otherwise a local
	; offset resolves, in `__drv_localize`, to whichever EARLIER file
	; happens to span it, and the diagnostic prints the wrong file's name
	; and the wrong file's line. Found by running it: a CRLF in the second
	; of three files reported the FIRST file's path and line 1, while the
	; `--emitte` note two lines below it named the second file correctly.
	; There are no tokens to move (§8.1 is a gate; nothing was tokenized),
	; and `__drv_globalize` walking an empty token range costs nothing.
	mov	rax, [drvent]
	mov	rdi, [rax + DrvSrc.sbase]
	mov	rsi, [drvt0]
	mov	rdx, [drvd0]
	call	__drv_globalize

	; ONE file of the unit failed §8.1, and the file it was is named --
	; `[drvent]`, not `DrvCtx.srcpath`, because with several SOURCEs the
	; first one is usually not the one at fault. Everything earlier files
	; already reported is still reported below; nothing later is lexed,
	; because §8.1 is a gate and a unit missing one of its files is not the
	; unit that was asked for.
	;
	; --emitte was asked for a stage that never came into existence: §8.1
	; is a gate, so a rejected file has no tokens, no CST and no AST to
	; dump. Say so on stderr -- see driver/dump.inc's header for why an
	; empty dump here would be a second, silent claim rather than an
	; honest one -- then fall into the same diagnostic report every other
	; refusal goes through.
	cmp	dword [r15 + DrvCtx.emitte], DRV_EMIT_NONE
	je	.report
	mov	rax, [drvent]
	lea	rdi, [drv_m_okpfx]
	mov	rsi, [rax + DrvSrc.spath]
	mov	rdx, [rax + DrvSrc.spathn]
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
	cmp	dword [r15 + DrvCtx.nsrc], 1
	ja	.clean_unit
	lea	rdi, [drv_m_okpfx]
	mov	rsi, [r15 + DrvCtx.srcpath]
	mov	rdx, [r15 + DrvCtx.srcpathn]
	lea	rcx, [drv_m_okbare]
	call	drv_msg
	jmp	.quiet_ok
  .clean_unit:
	; Several SOURCEs were given and they are ONE module (§12), so the
	; sentence is about the unit and it says how many files are in it. The
	; one-file sentence above is left byte-for-byte as it was.
	lea	rdi, [drv_m_okpfx]
	mov	rsi, [r15 + DrvCtx.srcpath]
	mov	rdx, [r15 + DrvCtx.srcpathn]
	lea	rcx, [drv_m_okunit]
	mov	r8d, [r15 + DrvCtx.nsrc]
	call	drv_msgu
  .quiet_ok:
	mov	eax, DRV_EXIT_OK
	return

  .clean_out:
	; ---- spec 16 Stage 3: an artifact WAS asked for, so build it ---------
	; Reached only with ZERO diagnostics (`.report` jumps here off an empty
	; `DrvCtx.diags`), which is `lwr_module`'s stated input invariant --
	; CHK 2.5, "it runs only when Stage 2 emitted no diagnostic" -- and the
	; reason this pass raises nothing and never gets an `EXS-E` code of its
	; own. A diagnostic still exits 1 and writes nothing; `--hospes` other
	; than `x86_64-linux` was refused with exit 4 long before the source was
	; opened, let alone OUT.
	;
	; WHICH ARENA IS WHICH, because `lwr_module` takes two and the choice is
	; not free (docs/design/lowering.md 2.1):
	;   - the IR arena is `DrvCtx.arena`, the compilation arena, which
	;     already holds the tree the IR is built from and is sized from the
	;     unit (driver/io.inc; its factor went 256 -> 384 for this). The IR
	;     must outlive `lwr_module` -- `bfa_emit_program` reads it next -- so
	;     it cannot be an arena anything resets.
	;   - the scratch arena is `DrvCtx.lwrscr`, a FOURTH arena, created here
	;     and nowhere else. `lwr_module` RESETS it at every function, which
	;     rules out all three of the others: `arena` holds the AST and the
	;     IR, `iarena` backs a live interner (rt/intern.inc's own rule), and
	;     `scratch` is the checker's -- checker/checker.inc says it "may be
	;     reset once `.report` has finished rendering, and not before", and
	;     while nothing is rendered on THIS path, borrowing an arena on the
	;     strength of the branch we happen to be in is how that rule stops
	;     holding the day the branch moves.
	mov	rdi, [r15 + DrvCtx.lwrscr]
	mov	rsi, DRV_LWRSCR_CAP
	call	arena_init
	jc	.no_memory

	mov	rdi, [r15 + DrvCtx.arena]
	mov	rsi, sizeof.BfaModule
	call	arena_alloc
	mov	[drvmdl], rax
	mov	rdi, rax
	mov	rsi, [r15 + DrvCtx.arena]
	call	bfa_module_init

	mov	rdi, [r15 + DrvCtx.ast]
	mov	rsi, [drvmdl]
	mov	rdx, [r15 + DrvCtx.arena]
	mov	rcx, [r15 + DrvCtx.lwrscr]
	call	lwr_module		; rax = 0 on success (lower/lower.inc)
	test	rax, rax
	jnz	.lower_refused

	; ---- the eleven constants and the MXCSR image ------------------------
	; `potestates` is spec 10.3's capability closure; `__drv_potestates`
	; above says at length what it actually computes and what it asked the
	; checker for. `mxcsr` is not a choice: `Ast.numeri` is all-zero -- the
	; driver never sets it and `lwr_module` `rassert`s that it is 0, because
	; docs/design/lowering.md finding 17 says no table maps a `numeri` byte
	; to a spec 5.4 word -- so `initium`'s `numeri` is spec 5.4's defaults
	; and the image for those is `BFA_MXCSR_AD_PAREM` (runtime.md 2.7).
	; The day `--numeri` or an `ego` reader exists, this is where the other
	; two `BFA_MXCSR_*` images get chosen from.
	; ---- which backend ---------------------------------------------------
	; `--emitte c` takes the REACH backend, and takes NONE of the two
	; arguments above: library mode has no capability closure to gate on
	; and no MXCSR image to set, because it emits no entry point and no
	; runtime (c-backend.md D1). What it takes instead is the `--hospes`
	; row, which contributes exactly one `_Static_assert` line to the unit.
	; The capability computation is skipped rather than computed and
	; discarded, so a module whose closure the checker cannot answer still
	; emits a C library.
	cmp	dword [r15 + DrvCtx.emitte], DRV_EMIT_C
	je	.emit_c

	mov	rdi, [r15 + DrvCtx.ast]
	call	__drv_potestates
	mov	[drvpot], rax

	mov	rdi, [drvmdl]
	mov	rsi, [r15 + DrvCtx.arena]
	mov	rdx, [drvpot]
	mov	rcx, BFA_MXCSR_AD_PAREM
	call	bfa_emit_program	; rax = the bytes, rdx = how many
	mov	[drvop], rax
	mov	[drvol], rdx
	jmp	.wrote_pick

  .emit_c:
	mov	rdi, [drvmdl]
	mov	rsi, [r15 + DrvCtx.arena]
	mov	edx, [drvhos]		; a DRV_HOSPES_* row; BFC_HOSPES_* has
					; the same three values by construction,
					; and __bfc_prog_hospes refuses any row
					; it cannot honour rather than trusting
					; the driver to have filtered it
	call	bfc_emit_unit		; rax = the bytes, rdx = how many
	mov	[drvop], rax
	mov	[drvol], rdx
  .wrote_pick:

	mov	rdi, [r15 + DrvCtx.outpath]
	mov	rsi, [r15 + DrvCtx.outpathn]
	mov	rdx, [drvop]
	mov	rcx, [drvol]
	call	drv_write_out
	test	eax, eax
	jz	.host			; drv_write_out has already said why

	; The note names OUT and is suppressed in json mode for the reason this
	; file's header gives: a plain-text line in a JSON Lines stream is a
	; parse error for the tool it was meant to help.
	cmp	dword [r15 + DrvCtx.diagmode], DIAG_MODE_JSON
	je	.wrote_quiet
	lea	rdi, [drv_m_wrote]
	mov	rsi, [r15 + DrvCtx.outpath]
	mov	rdx, [r15 + DrvCtx.outpathn]
	lea	rcx, [drv_m_wrote2]
	cmp	dword [r15 + DrvCtx.emitte], DRV_EMIT_C
	jne	.wrote_kind
	lea	rcx, [drv_m_wrote2c]	; it is not fasmg source and it is not
					; a binary, and saying "assemble it with
					; fasmg" of a C file would be wrong
  .wrote_kind:
	mov	r8, [drvol]
	call	drv_msgu
  .wrote_quiet:
	mov	eax, DRV_EXIT_OK
	return

  .lower_refused:
	; `lwr_module` answers 0 or nothing at all -- every contract violation
	; inside it is an `rassert` (SIGILL, exit 132), which is what
	; docs/asm-conventions.md reserves for a compiler bug. A non-zero return
	; is therefore a case that document's four channels do not cover, and
	; the honest report is "no object was written", not an invented code.
	lea	rdi, [drv_m_lwrfail]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_HOST
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
	; The triple is well-formed; it is simply not a row of the table. See
	; the comment on this proc's third required-field check, above, for why
	; that is exit 4 rather than exit 2.
	lea	rdi, [drv_m_hospesunsup]
	mov	rsi, [r15 + DrvCtx.hospes]
	mov	rdx, [r15 + DrvCtx.hospesn]
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_TODO
	return
  .hospes_needs_c:
	; A row whose only backend is the C one, asked for without `--emitte c`.
	; The value is in the table and the fault is still in the invocation.
	lea	rdi, [drv_m_hospesnoc]
	mov	rsi, [r15 + DrvCtx.hospes]
	mov	rdx, [r15 + DrvCtx.hospesn]
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_TODO
	return
  .hospes_o64:
	; The row exists in the design's table and this build cannot honour it.
	; Named separately from an unknown triple because the two are different
	; facts: this one is scheduled work with a stated blocker, and saying
	; "unknown" of a value the design lists would be false.
	lea	rdi, [drv_m_hospeso64]
	mov	rsi, [r15 + DrvCtx.hospes]
	mov	rdx, [r15 + DrvCtx.hospesn]
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_TODO
	return
  .emitte_c_no_out:
	lea	rdi, [drv_m_emitcnoout]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
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
