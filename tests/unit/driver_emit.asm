; tests/unit/driver_emit.asm
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
; `-o OUT` WRITES A PROGRAM: the whole pipeline -- spec 8.1 gate, spec 8.4
; tokenizer, spec 8.6 parse, spec 9.1 typed AST, Stage 2 checker, the lowering
; to the SSA IR, `bfa_emit_program` -- driven through `drv_main` exactly as a
; process would, and the bytes it left on disk read back and checked.
;
; THE SOURCE IS WRITTEN BY THIS FIXTURE, NOT FOUND. `tests/run.sh` does not
; promise a working directory (driver_status.asm's header states the rule and
; the reason), so a fixture that opened `examples/saluta.exsc` by a relative
; path would pass or fail on where it happened to be started from. This one
; writes its own source to an ABSOLUTE path under /tmp, compiles that, and
; reads OUT back from another absolute path. The bytes it writes are the
; hello world's three functions in one file -- spec 12 makes several SOURCEs
; one module, so one file holding all three is the same module the gate
; compiles from three.
;
; WHAT IT ASSERTS, and why each one is the check it is:
;
;   1. `drv_main` returns 0. Writing OUT successfully is exit 0 (spec 12's
;      minimum build invocation, completed). Before this change the same
;      command returned 4.
;   2. OUT OPENS WITH THE BANNER AND THE ONE EXTERNAL INCLUDE, byte for byte:
;      `; exsecutor: reference backend\ninclude 'format/format.inc'\n`.
;      docs/design/runtime.md section 2.1 fixes those two lines and their
;      order, and `include 'format/format.inc'` is the ONLY include OUT
;      carries -- spec 18.1's vendored macro package, the one tool in the
;      closure. An OUT that opened with anything else would not be fasmg
;      source at all.
;   3. OUT CONTAINS THE LABEL DEFINITION `\nbfausr_initium:\n`. That is the
;      emitted label of spec 4.7's entry point, so its presence is the
;      difference between "the prelude blob was copied out" and "the module
;      was actually lowered and emitted". Checks 2 and 3 are deliberately on
;      opposite sides of that line: 2 would still pass if the module were
;      empty.
;
;      THE NEWLINE AND THE COLON ARE LOAD-BEARING AND THIS CHECK WAS VACUOUS
;      WITHOUT THEM. `bfausr_initium` as a bare name occurs in the PRELUDE
;      BLOB -- `call bfausr_initium` in the entry stub, prelude.asm's own text
;      -- which OUT carries verbatim whether or not anything was lowered. The
;      first version of this fixture searched for the bare name, and mutation
;      A below (lowering skipped, module empty, OUT 1,701 bytes shorter) still
;      exited 0. Found by running the mutation, not by reading the fixture.
;   4. THE CAPABILITY MASK IS NOT ALL ONES. `EXS_POTESTAS_AMBITUS    = 1` and
;      `EXS_POTESTAS_RETE       = 0` must BOTH appear. This is the check that
;      exists because the bug existed: the first `__drv_potestates` scanned
;      `Ast.types`, and since `ast_cap_push` gives every tree all eleven spec
;      4.6 atoms, every constant came out `= 1` -- a program granted the
;      network because the atom's type is interned in every module ever
;      compiled. It emitted a running hello world and passed every other check
;      here, which is exactly why this one is written as a PAIR: one bit that
;      must be set and one that must be clear.
;
; NON-VACUITY, BY MUTATION, IN THE FIXTURE ITSELF. A fixture that only ever
; asks "is this needle present" cannot distinguish a working search from one
; that answers yes to everything, and a fixture that only ever asks "do these
; bytes match" cannot distinguish a working comparison from `return 1`. So
; both routines are run a second time against an input that must give the
; OPPOSITE answer: `de_find` is asked for a needle that is not in any emitted
; program (check 23), and `de_eq` is given a prefix that differs from OUT's in
; one byte (check 24). If either control fires, every other check in this file
; is worthless and this fixture says so rather than reporting green.
;
; TWO MUTATIONS WERE ALSO RUN OUT OF PROCESS, against the compiler rather than
; the fixture, and both are recorded because one of them found a real defect
; in this file:
;
;   A. `lwr_module`'s call replaced by `xor eax, eax` in driver/run.inc, so
;      `bfa_emit_program` emits the header, the prelude blobs and an EMPTY
;      module. Expected: 22. Got 0 the first time, because check 3's needle
;      was the bare name -- see above. With the label form it fails 22 while
;      check 2 still passes, which is the separation check 3 exists to make.
;   B. `mov [drvpot], rax` replaced by `mov qword [drvpot], 0x7FF`, so every
;      one of the eleven constants is emitted `= 1`. Expected and got: 26,
;      `EXS_POTESTAS_RETE       = 0` absent. That is the all-ones bug check 4
;      was written for, reproduced deliberately.
;
; Exit 0 = every check passed. Otherwise:
;   10  arena_init for the pre-write failed
;   11  the fixture could not write its own source file
;   12  drv_main did not return DRV_EXIT_OK
;   13  OUT could not be read back
;   20  OUT is shorter than the two lines it must open with
;   21  OUT does not open with the banner and `include 'format/format.inc'`
;   22  OUT does not contain the label definition `bfausr_initium:`
;   23  CONTROL: a needle that is not in OUT was reported found
;   24  CONTROL: a prefix that differs from OUT's was reported equal
;   25  `EXS_POTESTAS_AMBITUS    = 1` is not in OUT
;   26  `EXS_POTESTAS_RETE       = 0` is not in OUT
;
; Spec: docs/spec/exsecutor-spec-v0.4.md 4.6, 4.7, 9.3, 9.5, 12, 16 Stage 3,
; 18.1. Design: docs/design/runtime.md 2.1, 5; docs/design/lowering.md 2.1.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

DE_ARENA   = 1 shl 20
DE_BUF_CAP = 1024 * 1024

segment readable executable
  start:
	lea	r15, [de_ctx]
	call	de_bind

	; ---- 11: write the source this fixture is going to compile ----------
	; `drv_write_out` is driver/io.inc's own routine and it takes its
	; NUL-terminated path copy out of `DrvCtx.iarena`, so that arena has to
	; exist before the call. `drv_aedifica` re-`arena_init`s it moments
	; later, which is what it does on every invocation anyway.
	lea	rdi, [de_iarena]
	mov	rsi, DE_ARENA
	call	arena_init
	jc	.f10
	lea	rdi, [de_p_src]
	mov	rsi, DE_P_SRC_LEN
	lea	rdx, [de_source]
	mov	rcx, de_source.len
	call	drv_write_out
	test	eax, eax
	jz	.f11

	; ---- 12: the whole compiler, through the one entry point ------------
	mov	rdi, DE_ARGC
	lea	rsi, [de_argv]
	call	drv_main
	cmp	eax, DRV_EXIT_OK
	jne	.f12

	; ---- 13: read OUT back ----------------------------------------------
	mov	qword [de_got], 0
	lea	rdi, [de_p_out]
	mov	rsi, DE_P_OUT_LEN
	lea	rdx, [de_buf]
	mov	rcx, DE_BUF_CAP
	lea	r8,  [de_got]
	call	__drv_read_file
	test	eax, eax
	jz	.f13

	; ---- 20/21: the two lines OUT must open with ------------------------
	mov	rax, [de_got]
	cmp	rax, de_head.len
	jb	.f20
	lea	rdi, [de_buf]
	lea	rsi, [de_head]
	mov	rdx, de_head.len
	call	de_eq
	test	eax, eax
	jz	.f21

	; ---- 22: the module was lowered and emitted, not just the prelude ---
	lea	rdi, [de_buf]
	mov	rsi, [de_got]
	lea	rdx, [de_n_initium]
	mov	rcx, de_n_initium.len
	call	de_find
	test	eax, eax
	jz	.f22

	; ---- 23: CONTROL -- a needle that must NOT be found -----------------
	lea	rdi, [de_buf]
	mov	rsi, [de_got]
	lea	rdx, [de_n_absent]
	mov	rcx, de_n_absent.len
	call	de_find
	test	eax, eax
	jnz	.f23

	; ---- 24: CONTROL -- a prefix that must NOT compare equal ------------
	lea	rdi, [de_buf]
	lea	rsi, [de_head_bad]
	mov	rdx, de_head_bad.len
	call	de_eq
	test	eax, eax
	jnz	.f24

	; ---- 25/26: the capability mask is a MASK, not all ones -------------
	lea	rdi, [de_buf]
	mov	rsi, [de_got]
	lea	rdx, [de_n_ambitus]
	mov	rcx, de_n_ambitus.len
	call	de_find
	test	eax, eax
	jz	.f25

	lea	rdi, [de_buf]
	mov	rsi, [de_got]
	lea	rdx, [de_n_rete]
	mov	rcx, de_n_rete.len
	call	de_find
	test	eax, eax
	jz	.f26

	xor	edi, edi
	call	sys_exit_group

  .f10:	mov	edi, 10
	jmp	de_die
  .f11:	mov	edi, 11
	jmp	de_die
  .f12:	mov	edi, 12
	jmp	de_die
  .f13:	mov	edi, 13
	jmp	de_die
  .f20:	mov	edi, 20
	jmp	de_die
  .f21:	mov	edi, 21
	jmp	de_die
  .f22:	mov	edi, 22
	jmp	de_die
  .f23:	mov	edi, 23
	jmp	de_die
  .f24:	mov	edi, 24
	jmp	de_die
  .f25:	mov	edi, 25
	jmp	de_die
  .f26:	mov	edi, 26
	jmp	de_die

  de_die:
	mov	eax, 231
	syscall

; de_eq(rdi = a, rsi = b, rdx = n) -> eax = 1 if the n bytes match, else 0.
; Twelve lines of byte comparison rather than rt/str.inc's `str_eq`, for the
; reason driver/cli.inc's `drv_arg_is` records in full: that file is not
; reachable from this include chain.
  de_eq:
	xor	rcx, rcx
  .loop:
	cmp	rcx, rdx
	jae	.same
	mov	al, [rdi + rcx]
	cmp	al, [rsi + rcx]
	jne	.diff
	inc	rcx
	jmp	.loop
  .same:
	mov	eax, 1
	ret
  .diff:
	xor	eax, eax
	ret

; de_find(rdi = hay, rsi = haylen, rdx = needle, rcx = nlen) -> eax = 1 if the
; needle occurs, else 0. Naive, because the inputs are one emitted program and
; a dozen bytes.
  de_find:
	push	rbx
	push	r12
	push	r13
	mov	rbx, rdi
	mov	r12, rdx
	mov	r13, rcx
	sub	rsi, rcx		; last start offset that can still fit
	jb	.no
	xor	r8, r8
  .at:
	cmp	r8, rsi
	ja	.no
	xor	r9, r9
	lea	r10, [rbx + r8]		; x86-64 has base+index+disp, never three
					; registers -- the start is folded here
  .byte:
	cmp	r9, r13
	jae	.hit
	mov	al, [r10 + r9]
	cmp	al, [r12 + r9]
	jne	.next
	inc	r9
	jmp	.byte
  .next:
	inc	r8
	jmp	.at
  .hit:
	mov	eax, 1
	pop	r13
	pop	r12
	pop	rbx
	ret
  .no:
	xor	eax, eax
	pop	r13
	pop	r12
	pop	rbx
	ret

; de_bind -- what compiler/x86_64/exsc.asm's `start` does: name this process's
; mutable objects in the context r15 points at. Every field, including
; `lwrscr`, which is the lowering's per-function scratch arena and the one
; `drv_aedifica`'s `-o` path `arena_init`s for itself.
  de_bind:
	lea	rax, [de_arena]
	mov	[r15 + DrvCtx.arena], rax
	lea	rax, [de_iarena]
	mov	[r15 + DrvCtx.iarena], rax
	lea	rax, [de_scratch]
	mov	[r15 + DrvCtx.scratch], rax
	lea	rax, [de_lwrscr]
	mov	[r15 + DrvCtx.lwrscr], rax
	lea	rax, [de_interner]
	mov	[r15 + DrvCtx.interner], rax
	lea	rax, [de_envmap]
	mov	[r15 + DrvCtx.envmap], rax
	lea	rax, [de_srcs]
	mov	[r15 + DrvCtx.srcs], rax
	lea	rax, [de_vlx]
	mov	[r15 + DrvCtx.vlx], rax
	lea	rax, [de_vtoks]
	mov	[r15 + DrvCtx.vtoks], rax
	lea	rax, [de_vdiags]
	mov	[r15 + DrvCtx.vdiags], rax
	lea	rax, [de_lx]
	mov	[r15 + DrvCtx.lx], rax
	lea	rax, [de_toks]
	mov	[r15 + DrvCtx.toks], rax
	lea	rax, [de_diags]
	mov	[r15 + DrvCtx.diags], rax
	lea	rax, [de_green]
	mov	[r15 + DrvCtx.green], rax
	lea	rax, [de_work]
	mov	[r15 + DrvCtx.work], rax
	lea	rax, [de_cmap]
	mov	[r15 + DrvCtx.cmap], rax
	lea	rax, [de_ctree]
	mov	[r15 + DrvCtx.ctree], rax
	lea	rax, [de_parser]
	mov	[r15 + DrvCtx.parser], rax
	lea	rax, [de_ast]
	mov	[r15 + DrvCtx.ast], rax
	lea	rax, [de_chk]
	mov	[r15 + DrvCtx.chk], rax
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'
; lower/ brings the whole backend chain (program -> emit -> verify -> print ->
; parse -> ir) and relies on the consumer for rt/, ast/ and
; prelude/interface.inc -- the first two above, the third through
; checker/types/prim.inc. Exactly compiler/x86_64/exsc.asm's order, for
; exactly its reasons.
include '../../compiler/x86_64/lower/lower.inc'
include '../../compiler/x86_64/driver/driver.inc'

segment readable
  ; The hello world, spec 12's compilation unit written as one file. Byte for
  ; byte the three `examples/*.exsc` bodies, comments dropped: this fixture is
  ; about the driver, and a comment that drifted from examples/ would look
  ; like a difference in what is compiled when it is not.
  de_source:
	db 'publica functio saluta() -> textus {', 10
	db '    redde "Ave, mundus.', 10
	db 10
	db 'Ex silentio surgit forma.', 10
	db 'Ex signo nascitur vox.', 10
	db 'Ex codice fit lumen.', 10
	db 10
	db 'Hodie incipimus.";', 10
	db '}', 10
	db 10
	db 'publica functio imprime_gutenbergio(s: Scriptor, t: textus) -> mensura poscit sicut s {', 10
	db '    redde s.scribe(t);', 10
	db '}', 10
	db 10
	db 'publica functio initium(m: Mundus) -> u8 {', 10
	db '    firma a = m.ambitus();', 10
	db '    sub ambitus = a;', 10
	db '    firma s = Scriptor.ad_exitum(a);', 10
	db '    imprime_gutenbergio(s, saluta());', 10
	db '    redde 0;', 10
	db '}', 10
  .len = $ - de_source

  ; docs/design/runtime.md section 2.1's first two lines, in order. The banner
  ; carries no version, no path and no timestamp -- section 4's determinism
  ; audit -- and the include is the only one OUT has (spec 18.1).
  de_head:
	db '; exsecutor: reference backend', 10
	db "include 'format/format.inc'", 10
  .len = $ - de_head

  ; The control for `de_eq`: `de_head` with one byte changed. If this compares
  ; EQUAL, `de_eq` answers yes to everything and check 21 proved nothing.
  de_head_bad:
	db '; exsecutor: reference backenD', 10
	db "include 'format/format.inc'", 10
  .len = $ - de_head_bad

  ; THE LABEL DEFINITION, NOT THE NAME. `bfausr_initium` on its own is in the
  ; PRELUDE BLOB -- the entry stub's `call bfausr_initium`, prelude.asm line
  ; 256 -- so a needle without the newline and the colon is found in an OUT
  ; whose module is completely empty. Mutation A (this file's header) found
  ; that by skipping `lwr_module` and watching this check still pass. The
  ; leading newline is what makes it a definition at the start of a line and
  ; not a mention inside one.
  de_n_initium	db 10, 'bfausr_initium:', 10
  .len = $ - de_n_initium

  ; The control for `de_find`. Not a near miss on purpose -- a needle no
  ; emitted program can contain, so that a `de_find` which always says yes is
  ; caught rather than argued about.
  de_n_absent	db 10, 'bfausr_NOT_A_FUNCTION_IN_ANY_MODULE:', 10
  .len = $ - de_n_absent

  ; The mask, as two bits: one set, one clear. The column is fixed at
  ; BFA_PROG_NAMEW = 24 by backend_fasmg/program.inc, so the spacing is part
  ; of the emitted text and is written out here rather than skipped over.
  de_n_ambitus	db 'EXS_POTESTAS_AMBITUS    = 1', 10
  .len = $ - de_n_ambitus
  de_n_rete	db 'EXS_POTESTAS_RETE       = 0', 10
  .len = $ - de_n_rete

  ; Absolute, for the reason this file's header gives: no working directory is
  ; promised. /tmp is where a fixture may write; nothing else on the machine
  ; is touched, and both files are overwritten rather than appended to.
  de_s_exsc	db 'exsc',0
  de_s_aedifica	db 'aedifica',0
  de_s_hospes	db '--hospes',0
  de_s_triple	db 'x86_64-linux',0
  de_s_dasho	db '-o',0
  de_p_src	db '/tmp/exsecutor-driver-emit.exsc'
  DE_P_SRC_LEN = $ - de_p_src
		db 0
  de_p_out	db '/tmp/exsecutor-driver-emit.out.asm'
  DE_P_OUT_LEN = $ - de_p_out
		db 0

de_argv:
	dq de_s_exsc, de_s_aedifica, de_s_hospes, de_s_triple
	dq de_p_src, de_s_dasho, de_p_out
de_argv_end:
DE_ARGC = (de_argv_end - de_argv) / 8
  assert DE_ARGC = 7

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  de_got	rq 1
  de_ctx	rb sizeof.DrvCtx
  de_arena	rb sizeof.Arena
  de_iarena	rb sizeof.Arena
  de_scratch	rb sizeof.Arena
  de_lwrscr	rb sizeof.Arena
  de_interner	rb sizeof.Interner
  de_envmap	rb sizeof.Map
  de_srcs	rb DRV_SOURCES_MAX * sizeof.DrvSrc
  de_vlx	rb sizeof.Lexer
  de_vtoks	rb sizeof.Vec
  de_vdiags	rb sizeof.Vec
  de_lx		rb sizeof.Lexer
  de_toks	rb sizeof.Vec
  de_diags	rb sizeof.Vec
  de_green	rb sizeof.Vec
  de_work	rb sizeof.Vec
  de_cmap	rb sizeof.Map
  de_ctree	rb sizeof.CstTree
  de_parser	rb sizeof.CstParser
  de_ast	rb sizeof.Ast
  de_chk	rb sizeof.ChkCtx
  de_buf	rb DE_BUF_CAP
