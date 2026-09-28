; compiler/x86_64/checker/checker.inc
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
; THE checker/ MODULE'S SINGLE AGGREGATION POINT (docs/asm-conventions.md, "7.
; How to add a module", step 3). A consumer includes THIS file, not the pieces.
;
; STAGE 2 is `docs/design/checker.md`. That document is the contract; this
; module is the first thing to exercise it, and everything below that
; contradicts it is recorded as a finding, in place -- resolve.inc's header
; carries the one this pass hit.
;
; WHAT THIS MODULE DOES NOT INCLUDE, and what a consumer must therefore include
; FIRST -- the same contract ast/ast.inc has, for the same reason (fasmg has one
; flat namespace and a `struct` or `proc` processed twice by two paths fails on
; the second):
;
;	include '<...>/compiler/x86_64/lexer/lexer.inc'   ; brings diag/ and rt/
;	include '<...>/compiler/x86_64/ast/ast.inc'
;	include '<...>/compiler/x86_64/checker/checker.inc'
;
; The include chain INSIDE checker/ is verify.inc's to state, because verify.inc
; is the last link and the one a new pass is spliced in front of; see its header
; for the ordering and who owns each file.
;
; ---------------------------------------------------------------------------
; WHAT RUNS TODAY, AND WHAT THE HOOKS BELOW ARE FOR. checker.md section 2.2
; lists six deliverables; `checker-resolve` (this agent) built numbers 0, 1 and
; 6. `chk_run` calls the passes that exist and NOTHING ELSE -- an empty hook is
; a comment here rather than a stub proc, so that "the lexicon pass has not
; run" is visible in the source instead of being a call that silently succeeds.
;
; | # | pass    | owner            | state                                     |
; |---|---------|------------------|-------------------------------------------|
; | 0 | atoms   | `checker-resolve`| DONE -- `__chk_pass0`                     |
; | 1 | resolve | `checker-resolve`| DONE -- `chk_resolve`                     |
; | 2 | types   | `checker-types`  | DONE -- `chk_types`                       |
; | 3 | rows    | `checker-rows`   | DONE -- `chk_rows`, called when CHK_S_TYPES is set |
; | 4 | layout  | `checker-rows`   | DONE -- `chk_layout`, called when CHK_S_TYPES is set |
; | 5 | lexicon | `checker-lexicon`| DONE -- `chk_lexicon`                     |
; | 6 | verify  | `checker-resolve`| DONE -- `chk_verify`, gated on CHK_S_*    |
;
; This table read "2 | types | not written" and "5 | lexicon | not written"
; until 2026-09-27, by which time `chk_types` was called fifteen lines below
; it. Rows 3 and 4 carried a paragraph here explaining why they were INCLUDED
; BUT NOT CALLED; they are called, gated on CHK_S_TYPES, and the gate is what
; that paragraph asked for.
;
; WHY PASS 5 IS WRITTEN AND STILL DOES NOT RUN. `checker/lexicon/lexicon.inc`
; exists and defines `chk_lexicon`, raising EXS-E0601, E0602, E0603 and E0610,
; with six fixtures (`tests/unit/chk_lx_*.asm`) that include that file DIRECTLY
; and run green. It is not in `verify.inc`'s chain and `chk_run` does not call
; it, for a reason that is about the spec and not about the code: E0601 ("does
; not decompose") judges the coverage of the morpheme table, and spec §3.3's
; table is fourteen illustrative roots, so a called pass would flag ordinary
; identifiers across the whole tree. Enabling it is one `include` in verify.inc
; and one call below, behind a decision recorded in §3.3 -- not a port.
;
; ---------------------------------------------------------------------------
; HOW THE DRIVER CALLS THIS. `driver/run.inc` calls it, after
; `ast_verify_stage1` and before `--emitte`, in the sequence below (this note
; said the driver "does not call the checker yet" until 2026-09-27; the call
; site is `run.inc`'s "---- Stage 2: the checker" block, which cites this note
; back). `chk_set_target` is the one step the driver owns rather than this
; note: spec §9.5 forbids a default, so the pointer width comes from the
; `--hospes` row -- 32 for `mips64-none-o64`, 64 otherwise.
;
;	mov	rdi, [r15 + DrvCtx.chk]		; -> ChkCtx, sizeof.ChkCtx
;	mov	rsi, [r15 + DrvCtx.ast]
;	mov	rdx, [r15 + DrvCtx.diags]
;	mov	rcx, [r15 + DrvCtx.scratch]	; an arena the checker owns
;	xor	r8, r8
;	cmp	qword [r15 + DrvCtx.outpath], 0
;	je	.nochkprog
;	mov	r8, CHK_F_PROGRAM		; -o was given: a PROGRAM was asked
;					; for, so spec §4.1 rule 2's `initium`
;					; is required and its absence is
;					; `EXS-E0424`. Without -o the module is
;					; a library and its absence is nothing.
;  .nochkprog:
;	call	chk_init
;	mov	rdi, [r15 + DrvCtx.chk]
;	mov	rsi, [srcbuf]
;	mov	rdx, [srclen]
;	mov	rcx, [r15 + DrvCtx.srcpath]
;	mov	r8,  [r15 + DrvCtx.srcpathn]
;	call	chk_set_source
;	mov	rdi, [r15 + DrvCtx.chk]
;	call	chk_run				; rax = diagnostics raised
;
; and then, exactly as it already does for the lexer's, fall into `.report`,
; which walks `DrvCtx.diags` in order. `chk_run` appends to that vector and
; returns a count; it never writes to a descriptor and never exits.
;
; THE SCRATCH ARENA MAY BE RESET once `.report` has finished rendering, and not
; before: a `Diag` borrows its source pointer and its fix text, and while the
; fix text is allocated from the TREE's arena on purpose (resolve.inc's
; `__chk_fix_text`), the records themselves are the caller's. `DrvCtx.scratch`
; is currently reset once per `--env` key, which happens long before this point,
; so reusing it is safe; a checker of its own arena would be cleaner and is the
; driver agent's call.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.1 (Stage 2 types the tree), §16
; (the Stage 2 kill criterion), §13.
; -----------------------------------------------------------------------------

include 'verify.inc'

; chk_run(chk_c) -> rax = the number of diagnostics this run appended to the
; caller's vector. `chk_init` and, if the caller has source text, `chk_set_source`
; must already have been called.
;
; checker.md section 6 wrote this as `chk_run(tree, diags, arena)`; the context
; struct is a deviation, and resolve.inc's `ChkCtx` header states why (source
; text for `diag_attach_source`, and the program-was-asked-for bit, do not fit
; in three arguments, and `lex_init`/`lex_set_source`/`lex_run` is the shape
; this tree already uses for the same pair of needs).
;
; DIAGNOSTICS ARE FLUSHED ONCE, AT THE END, not per pass: checker.md section 4
; orders emission by "pass number, then node index within the pass", and every
; pass buffers through `chk_raise` so that one `sort_stable` establishes both
; keys at once. A pass that wrote straight into the caller's vector would fix
; its own diagnostics' position before the next pass had raised any.
proc chk_run, chk_c
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chk_c]

	mov	rdi, rbx
	call	chk_resolve		; passes 0 and 1

	; ---- the passes after resolve, in checker.md section 2.2's order. One
	; `include` line per pass lives in verify.inc (the chain); one CALL per
	; pass lives here.
	;
	; Passes 3 and 4 read `Decl.ty`/`Node.ty`, which only pass 2 fills, so
	; they are GATED on CHK_S_TYPES rather than called unconditionally: an
	; ungated call would run the row fixpoint over `ty == 0` everywhere and
	; either rassert or raise E0421 against nothing. `chk_types` ORs that bit
	; in on its way out and has no early return, so in the driver the gate is
	; always open and passes 3 and 4 always follow pass 2; what it still buys
	; is that the precondition is stated in the source rather than assumed.
	; (This comment said "pass 2 does not exist yet" until 2026-09-27, eight
	; lines above the call that runs it.)
	;
	mov	rdi, rbx
	call	chk_types		; pass 2 -- `checker-types`
	;
	test	dword [rbx + ChkCtx.stages], CHK_S_TYPES
	jz	.no_rows
	mov	rdi, rbx
	call	chk_rows		; pass 3 -- `checker-rows`
	mov	rdi, rbx
	mov	esi, [rbx + ChkCtx.ptrbits]
	rassert	esi ne 0	; spec §9.5: no default target; the driver sets it
	call	chk_layout		; pass 4 -- `checker-rows`
  .no_rows:
	; Pass 5 is called from the driver after chk_run, not here: unit
	; fixtures load AST dumps into chk_run and are not the public API.
	; Each pass takes what it needs, raises through `chk_raise*` with its own
	; CHK_P_* pass number, and ORs its CHK_S_* bit into `ChkCtx.stages` on the
	; way out, the way `chk_resolve` does. `chk_verify` then checks exactly the
	; clauses those bits license.

	mov	rdi, rbx
	call	chk_flush
	mov	r12, rax

	mov	rdi, [rbx + ChkCtx.tree]
	mov	esi, [rbx + ChkCtx.ndiag]
	add	esi, [rbx + ChkCtx.nsupp]	; see chk_verify's gate 1
	mov	edx, [rbx + ChkCtx.stages]
	call	chk_verify

	mov	rax, r12
	return
endp

; chk_set_target(chk_c, ptrbits) -- the pointer width of --hospes, in bits.
; The driver derives it from the triple it accepted (x86_64-linux -> 64);
; nothing here guesses, because spec §9.5 forbids a default-to-build-platform
; and pass 4's `mensura` is concrete only once this is set.
proc chk_set_target, chk_c, chk_bits
	locals
	endl
	mov	rdi, [chk_c]
	mov	eax, [chk_bits]
	mov	[rdi + ChkCtx.ptrbits], eax
	return
endp
