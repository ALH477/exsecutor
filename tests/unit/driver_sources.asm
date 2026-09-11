; tests/unit/driver_sources.asm
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
; `exsc aedifica --hospes x86_64-linux A.exsc B.exsc C.exsc` -- §12's repeated
; SOURCE, and the one property that makes it worth having: *"The files named
; form one compilation unit -- one module in §10's sense: one `ego`, one
; interface hash, one artifact."*
;
; THE HELLO WORLD IS THE EVIDENCE, AND IT IS THE REAL FILES. §12 chose the
; multi-file model *"because the hello world is three functions in three files
; (`examples/saluta.exsc`, `imprime.exsc`, `initium.exsc`)"*. So this fixture
; embeds THOSE THREE FILES' BYTES, with fasmg's `file` directive resolving the
; path relative to this source file, and writes them back out under /tmp to
; compile them. Two things that buys, both deliberate:
;   - no drift. A copy of the examples pasted into this file would go stale
;     the first time one of them is edited, and nothing would say so; `file`
;     re-reads them at every assembly.
;   - no working directory. `driver_status.asm`'s header records why a
;     fixture must not guess a path relative to an unpromised cwd; a path
;     this fixture WRITES itself, under a fixed absolute name, involves no
;     cwd on either end. `driver_checker.asm` established the pattern and the
;     syscall argument: `openat`/`write`/`close` are already on the closed
;     allowlist (CLAUDE.md) and already reached through rt/sys.inc. There is
;     no `unlink` on that allowlist, so the six files stay under /tmp;
;     `O_TRUNC` means a second run overwrites rather than accumulates.
;
; WHAT THE THREE EXAMPLES PROVE, and what the mutation is. `initium.exsc`
; calls `imprime_gutenbergio` and `saluta`, and both are declared in the OTHER
; two files. Compiled ALONE it therefore cannot resolve them and the checker
; says so, `EXS-E0301` per name; compiled as ONE unit those names are in the
; module and the diagnostics are gone. That pair -- the same file, one alone
; and one in its unit -- is the non-vacuity: a fixture that only ever compiled
; the unit could not tell "the names resolved" from "the checker never ran".
;
; THE COUNT IS ASSERTED AS AN INEQUALITY, NOT AS A NUMBER, and that is not
; hedging. `Scriptor` is a runtime-prelude name that no source file in the
; hello world declares (§12: *"The prelude's own names ... are pre-seeded
; declarations in a scope OUTSIDE the module's"*), so whether it resolves
; depends on a checker pass that is being written in another tree right now --
; it was `EXS-E0301` at the commit this fixture was written against and is not
; one in the working tree beside it. What does NOT depend on that pass is the
; claim §12 is actually about: the CROSS-FILE names resolve, so the unit's
; `EXS-E0301` count is strictly smaller than `initium.exsc`'s alone AND is at
; most one -- and if one survives, this fixture requires it to be about
; `Scriptor` by name, so a regression that left `saluta` unresolved could not
; hide inside the allowance.
;
; SPANS ARE UNIT OFFSETS AND FILES ARE RECOVERED FROM THEM. driver/io.inc
; reads the unit's files back to back into one buffer, driver/run.inc rewrites
; each file's token spans into that buffer's coordinates, and `drv_emit_diag`
; maps a diagnostic back to the file its offset falls in. Check 40 is the
; whole of that round trip end to end: a parse error in the SECOND of three
; files must render with the SECOND file's path and the SECOND file's own line
; number -- not the first file's name, and not a line counted from the start
; of the unit. It is a checker-independent check (§8.6's parser raises it) and
; it fails loudly if the localization is ever removed.
;
; CHECK 70 IS THE SAME QUESTION ON THE PATH THAT DOES NOT REACH THE PARSER,
; and it is here because that path got it WRONG when this was first written.
; §8.1 is a gate: `lex_run` refuses the file and returns without tokenizing,
; so `drv_aedifica` jumped straight to its refusal and SKIPPED the step that
; rewrites that file's diagnostics into unit coordinates. The offsets stayed
; local, and a CRLF in the second of three files printed the FIRST file's
; path and the first file's line 1 -- while the `--emitte` note printed
; directly above it named the second file correctly, which is how it was
; noticed. Found by running it, not by reading it.
;
; ORDER IS INPUT (§9.3 + §12). *"Their order on the command line is the order
; of their items and is part of the input, so §9.3's byte-identity holds for
; the same command and need not survive a reordering."* Check 50 dumps the
; typed AST for two orders of the same three files and requires the bytes to
; DIFFER; check 51 dumps the same order twice and requires them to be
; IDENTICAL. Nothing in the driver sorts a SOURCE list, and those two checks
; are what would notice if something started to.
;
; Exit 0 = every check passed. Otherwise:
;   10  initium.exsc alone did not exit 1 (diagnostics)
;   11  ... and did not raise at least two EXS-E0301 -- the mutation this
;       fixture's main claim is measured against is vacuous
;   20  the three examples as one unit exited neither 0 nor 1
;   21  ... and still raise as many EXS-E0301 as initium.exsc alone, or more
;       than one at all: a cross-file name did not resolve
;   22  ... and the one EXS-E0301 that survived is not about `Scriptor`
;   30  the unit's token vector holds more than one TOK_EOF -- an EOF in the
;       middle ends the module at the end of the first file
;   31  ... or carries fewer than two distinct Span.file_id values
;   32  ... or a token span goes backwards: the unit is not one ordered text
;   40  a parse error in the SECOND of three files did not exit 1
;   41  ... its rendered text does not name the second file
;   42  ... or does not put it at that file's OWN line 3
;   43  ... or names one of the other two files instead
;   44  the same unit with the offending file left out is not clean -- check
;       40's diagnostic did not come from where this fixture says it did
;   50  two DIFFERENT orders of the same three files produced the same
;       `--emitte ast` dump
;   51  the SAME order twice produced different bytes (§9.3)
;   60  the same path given twice was not a usage error
;   70  a spec 8.1 gate failure in the SECOND of three files did not exit 1
;   71  ... or did not raise exactly EXS-E0106 (the CRLF that file carries)
;   72  ... or its rendering does not name the second file
;   73  ... or names one of the other two instead
;   80  stderr could not be captured (see `dv_capture`, below)
;   81  what `exsc` ACTUALLY WROTE for a parse error in the second of three
;       files does not name that file at its own line 3
;   82  ... or names the first file instead
;   99  setup (arena_init, or writing a file under /tmp) failed
;
; CHECKS 81/82 ARE THE ONLY ONES THAT READ WHAT `exsc` ACTUALLY WROTE, and
; they exist because a mutation showed the rest of this fixture could not tell
; the difference. `dv_render_diag`, below, MIRRORS `drv_emit_diag`'s
; copy-localize-render -- so deleting the `__drv_localize` call from
; `drv_emit_diag` itself left every other check in this file passing. Measured,
; not suspected: with that one call removed the fixture still exited 0.
;
; So the last check redirects fd 2 into a file and reads it back. Nothing on
; the closed syscall allowlist can duplicate a descriptor -- there is no
; `dup2` and adding one is a reviewed change (CLAUDE.md) this fixture has no
; business asking for -- but `close` and `openat` are both on it, and the
; kernel gives a fresh `openat` the LOWEST free descriptor. Closing 2 and then
; opening a file therefore hands that file back AS fd 2, which is exactly what
; is wanted and uses nothing new. It is checked rather than assumed: an
; `openat` that came back as anything else is check 80, not a silent pass.
;
; It runs LAST, and that is deliberate: fd 2 stays pointed at the file
; afterward, so any message a later check produced would be invisible. These
; fixtures report by exit status, never by output, so nothing is lost -- but
; putting another check after this one would be a mistake.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3, §8.6, §9.3, §12, §13.
;
; TEST: run=yes expect-exit=0 audit=skip

include 'format/format.inc'

format ELF64 executable 3
entry start

DV_ARENA = 1 shl 20
DV_O_WRONLY_CREAT_TRUNC = 0x241	; O_WRONLY(1) | O_CREAT(0x40) | O_TRUNC(0x200)
DV_MODE_0644 = 420
DV_DUMP_CAP  = 1 shl 18
DV_RBUF_CAP  = 1 shl 14

segment readable executable
  start:
	lea	r15, [dv_ctx]
	call	dv_bind

	; ---- the six files this fixture compiles ---------------------------
	lea	rdi, [dv_p1]
	mov	rsi, DV_P1_LEN
	lea	rdx, [dv_ex_a]
	mov	rcx, DV_EX_A_LEN
	call	dv_write_file
	test	eax, eax
	jz	.f99
	lea	rdi, [dv_p2]
	mov	rsi, DV_P2_LEN
	lea	rdx, [dv_ex_b]
	mov	rcx, DV_EX_B_LEN
	call	dv_write_file
	test	eax, eax
	jz	.f99
	lea	rdi, [dv_p3]
	mov	rsi, DV_P3_LEN
	lea	rdx, [dv_ex_c]
	mov	rcx, DV_EX_C_LEN
	call	dv_write_file
	test	eax, eax
	jz	.f99

	lea	rdi, [dv_q1]
	mov	rsi, DV_Q1_LEN
	lea	rdx, [dv_src_q1]
	mov	rcx, DV_SRC_Q1_LEN
	call	dv_write_file
	test	eax, eax
	jz	.f99
	lea	rdi, [dv_q2]
	mov	rsi, DV_Q2_LEN
	lea	rdx, [dv_src_q2]
	mov	rcx, DV_SRC_Q2_LEN
	call	dv_write_file
	test	eax, eax
	jz	.f99
	lea	rdi, [dv_q3]
	mov	rsi, DV_Q3_LEN
	lea	rdx, [dv_src_q3]
	mov	rcx, DV_SRC_Q3_LEN
	call	dv_write_file
	test	eax, eax
	jz	.f99
	lea	rdi, [dv_qc]
	mov	rsi, DV_QC_LEN
	lea	rdx, [dv_src_qc]
	mov	rcx, DV_SRC_QC_LEN
	call	dv_write_file
	test	eax, eax
	jz	.f99

	; ---- 10/11: THE MUTATION -- initium.exsc on its own ----------------
	; It calls two functions declared in the other two files, so alone it
	; cannot resolve them. If this stops being true the main claim below
	; is measuring nothing, so it is checked first and separately.
	mov	rdi, DV_AV_C_N
	lea	rsi, [dv_av_c]
	call	drv_main
	cmp	eax, DRV_EXIT_DIAG
	jne	.f10
	mov	rdi, 301
	call	dv_count_code
	mov	r13, rax		; how many EXS-E0301 the file alone raises
	cmp	r13, 2
	jl	.f11

	; ---- 20/21/22: the three examples as ONE unit (§12) ----------------
	mov	rdi, DV_AV_ABC_N
	lea	rsi, [dv_av_abc]
	call	drv_main
	cmp	eax, DRV_EXIT_OK
	je	.unit_ok
	cmp	eax, DRV_EXIT_DIAG
	jne	.f20
  .unit_ok:
	mov	rdi, 301
	call	dv_count_code
	mov	r14, rax
	cmp	r14, r13
	jge	.f21			; nothing resolved that did not before
	cmp	r14, 1
	jg	.f21			; more than the one allowance
	test	r14, r14
	jz	.scriptor_done
	; Exactly one survived: it must be `Scriptor`, the prelude name no
	; source file in the hello world declares. Rendered through the same
	; renderer `drv_emit_diag` calls, so this is evidence about the text
	; `exsc` would have printed.
	mov	rdi, 301
	lea	rsi, [dv_s_scriptor]
	mov	rdx, DV_S_SCRIPTOR_LEN
	call	dv_code_renders_as
	test	eax, eax
	jz	.f22
  .scriptor_done:

	; ---- 30/31/32: one token stream, not three -------------------------
	; The unit's tokens are still in `DrvCtx.toks` from the run just above.
	call	dv_count_eof
	cmp	rax, 1
	jne	.f30
	call	dv_count_files
	cmp	rax, 2
	jl	.f31
	call	dv_spans_ordered
	test	eax, eax
	jz	.f32

	; ---- 40..44: a parse error in the SECOND of three files ------------
	mov	rdi, DV_AV_Q_N
	lea	rsi, [dv_av_q]
	call	drv_main
	cmp	eax, DRV_EXIT_DIAG
	jne	.f40
	xor	rdi, rdi
	call	dv_render_diag		; rax = bytes in dv_rbuf
	mov	rbx, rax
	lea	rdi, [dv_rbuf]
	mov	rsi, rbx
	lea	rdx, [dv_n_q2]
	mov	rcx, DV_N_Q2_LEN
	call	dv_contains
	test	eax, eax
	jz	.f41
	lea	rdi, [dv_rbuf]
	mov	rsi, rbx
	lea	rdx, [dv_n_q2line]
	mov	rcx, DV_N_Q2LINE_LEN
	call	dv_contains
	test	eax, eax
	jz	.f42
	lea	rdi, [dv_rbuf]
	mov	rsi, rbx
	lea	rdx, [dv_n_q1]
	mov	rcx, DV_N_Q1_LEN
	call	dv_contains
	test	eax, eax
	jnz	.f43
	lea	rdi, [dv_rbuf]
	mov	rsi, rbx
	lea	rdx, [dv_n_q3]
	mov	rcx, DV_N_Q3_LEN
	call	dv_contains
	test	eax, eax
	jnz	.f43

	; 44: the same unit without the middle file compiles clean, so the
	; diagnostic above really did come out of the middle file.
	mov	rdi, DV_AV_Q13_N
	lea	rsi, [dv_av_q13]
	call	drv_main
	cmp	eax, DRV_EXIT_OK
	jne	.f44

	; ---- 50/51: order is input (§12), and only order ------------------
	mov	rdi, DV_AV_ABC_N
	lea	rsi, [dv_av_abc]
	call	drv_main
	lea	rdi, [dv_dump1]
	call	dv_dump_ast
	mov	rbx, rax

	mov	rdi, DV_AV_CBA_N
	lea	rsi, [dv_av_cba]
	call	drv_main
	lea	rdi, [dv_dump2]
	call	dv_dump_ast
	mov	r12, rax

	mov	rdi, DV_AV_ABC_N
	lea	rsi, [dv_av_abc]
	call	drv_main
	lea	rdi, [dv_dump3]
	call	dv_dump_ast
	mov	r13, rax

	lea	rdi, [dv_dump1]
	mov	rsi, rbx
	lea	rdx, [dv_dump2]
	mov	rcx, r12
	call	dv_bytes_equal
	test	eax, eax
	jnz	.f50			; a reordering must NOT be the same input
	lea	rdi, [dv_dump1]
	mov	rsi, rbx
	lea	rdx, [dv_dump3]
	mov	rcx, r13
	call	dv_bytes_equal
	test	eax, eax
	jz	.f51			; the same command must be the same bytes

	; ---- 60: the same path twice ---------------------------------------
	mov	rdi, DV_AV_DUP_N
	lea	rsi, [dv_av_dup]
	call	drv_main
	cmp	eax, DRV_EXIT_USAGE
	jne	.f60

	; ---- 70..73: a §8.1 GATE failure in the SECOND of three files -------
	; The gate is not the parser: nothing is tokenized, so this reaches a
	; different refusal inside `drv_aedifica` -- and that refusal has to do
	; the same coordinate rewrite the ordinary path does, or the file it
	; names and the file it renders disagree. See this fixture's header.
	mov	rdi, DV_AV_G_N
	lea	rsi, [dv_av_g]
	call	drv_main
	cmp	eax, DRV_EXIT_DIAG
	jne	.f70
	mov	rdi, 106
	call	dv_count_code
	cmp	rax, 1
	jne	.f71
	xor	rdi, rdi
	call	dv_render_diag
	mov	rbx, rax
	lea	rdi, [dv_rbuf]
	mov	rsi, rbx
	lea	rdx, [dv_n_qc]
	mov	rcx, DV_N_QC_LEN
	call	dv_contains
	test	eax, eax
	jz	.f72
	lea	rdi, [dv_rbuf]
	mov	rsi, rbx
	lea	rdx, [dv_n_q1]
	mov	rcx, DV_N_Q1_LEN
	call	dv_contains
	test	eax, eax
	jnz	.f73
	lea	rdi, [dv_rbuf]
	mov	rsi, rbx
	lea	rdx, [dv_n_q3]
	mov	rcx, DV_N_Q3_LEN
	call	dv_contains
	test	eax, eax
	jnz	.f73

	; ---- 80/81/82: what `exsc` ACTUALLY WROTE -- RUNS LAST -------------
	call	dv_capture
	test	eax, eax
	jz	.f80
	mov	rdi, DV_AV_Q_N
	lea	rsi, [dv_av_q]
	call	drv_main
	call	dv_capture_read		; rax = bytes now in dv_rbuf
	mov	rbx, rax
	lea	rdi, [dv_rbuf]
	mov	rsi, rbx
	lea	rdx, [dv_n_q2line]
	mov	rcx, DV_N_Q2LINE_LEN
	call	dv_contains
	test	eax, eax
	jz	.f81
	lea	rdi, [dv_rbuf]
	mov	rsi, rbx
	lea	rdx, [dv_n_q1]
	mov	rcx, DV_N_Q1_LEN
	call	dv_contains
	test	eax, eax
	jnz	.f82

	mov	eax, 231
	xor	edi, edi
	syscall

  .f10: mov edi, 10
	jmp dv_die
  .f11: mov edi, 11
	jmp dv_die
  .f20: mov edi, 20
	jmp dv_die
  .f21: mov edi, 21
	jmp dv_die
  .f22: mov edi, 22
	jmp dv_die
  .f30: mov edi, 30
	jmp dv_die
  .f31: mov edi, 31
	jmp dv_die
  .f32: mov edi, 32
	jmp dv_die
  .f40: mov edi, 40
	jmp dv_die
  .f41: mov edi, 41
	jmp dv_die
  .f42: mov edi, 42
	jmp dv_die
  .f43: mov edi, 43
	jmp dv_die
  .f44: mov edi, 44
	jmp dv_die
  .f50: mov edi, 50
	jmp dv_die
  .f51: mov edi, 51
	jmp dv_die
  .f60: mov edi, 60
	jmp dv_die
  .f70: mov edi, 70
	jmp dv_die
  .f71: mov edi, 71
	jmp dv_die
  .f72: mov edi, 72
	jmp dv_die
  .f73: mov edi, 73
	jmp dv_die
  .f80: mov edi, 80
	jmp dv_die
  .f81: mov edi, 81
	jmp dv_die
  .f82: mov edi, 82
	jmp dv_die
  .f99: mov edi, 99
	jmp dv_die

  dv_die:
	mov	eax, 231
	syscall

; dv_bind -- what compiler/x86_64/exsc.asm's `start` does: name the process's
; mutable objects in the context r15 points at, `DrvCtx.srcs` (the §12 SOURCE
; table) among them.
  dv_bind:
	lea	rax, [dv_arena]
	mov	[r15 + DrvCtx.arena], rax
	lea	rax, [dv_iarena]
	mov	[r15 + DrvCtx.iarena], rax
	lea	rax, [dv_scratch]
	mov	[r15 + DrvCtx.scratch], rax
	lea	rax, [dv_interner]
	mov	[r15 + DrvCtx.interner], rax
	lea	rax, [dv_envmap]
	mov	[r15 + DrvCtx.envmap], rax
	lea	rax, [dv_srcs]
	mov	[r15 + DrvCtx.srcs], rax
	lea	rax, [dv_vlx]
	mov	[r15 + DrvCtx.vlx], rax
	lea	rax, [dv_vtoks]
	mov	[r15 + DrvCtx.vtoks], rax
	lea	rax, [dv_vdiags]
	mov	[r15 + DrvCtx.vdiags], rax
	lea	rax, [dv_lx]
	mov	[r15 + DrvCtx.lx], rax
	lea	rax, [dv_toks]
	mov	[r15 + DrvCtx.toks], rax
	lea	rax, [dv_diags]
	mov	[r15 + DrvCtx.diags], rax
	lea	rax, [dv_green]
	mov	[r15 + DrvCtx.green], rax
	lea	rax, [dv_work]
	mov	[r15 + DrvCtx.work], rax
	lea	rax, [dv_cmap]
	mov	[r15 + DrvCtx.cmap], rax
	lea	rax, [dv_ctree]
	mov	[r15 + DrvCtx.ctree], rax
	lea	rax, [dv_parser]
	mov	[r15 + DrvCtx.parser], rax
	lea	rax, [dv_ast]
	mov	[r15 + DrvCtx.ast], rax
	lea	rax, [dv_chk]
	mov	[r15 + DrvCtx.chk], rax
	ret

; dv_write_file(path, pathlen, data, datalen) -> eax = 1 on success, else 0.
; `path` is already NUL-terminated (every literal below is). Lifted from
; tests/unit/driver_checker.asm, which is where the argument for a fixture
; writing its own input is recorded in full.
  dv_write_file:
	push	rbx
	push	r12
	push	r13
	mov	rbx, rdi
	mov	r12, rdx
	mov	r13, rcx

	mov	rdi, DRV_AT_FDCWD
	mov	rsi, rbx
	mov	rdx, DV_O_WRONLY_CREAT_TRUNC
	mov	rcx, DV_MODE_0644
	call	sys_openat
	jc	.wf_fail
	mov	rbx, rax

	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	call	sys_write
	jc	.wf_close_fail
	cmp	rax, r13
	jne	.wf_close_fail

	mov	rdi, rbx
	call	sys_close
	jc	.wf_fail
	mov	eax, 1
	jmp	.wf_done
  .wf_close_fail:
	mov	rdi, rbx
	call	sys_close
  .wf_fail:
	xor	eax, eax
  .wf_done:
	pop	r13
	pop	r12
	pop	rbx
	ret

; dv_capture -> eax = 1 if fd 2 now refers to dv_perr, else 0. See this
; fixture's header for why this is `close` + `openat` and not `dup2`.
  dv_capture:
	push	rbx
	mov	rdi, 2
	call	sys_close
	mov	rdi, DRV_AT_FDCWD
	lea	rsi, [dv_perr]
	mov	rdx, DV_O_WRONLY_CREAT_TRUNC
	mov	rcx, DV_MODE_0644
	call	sys_openat
	jc	.cap_no
	cmp	rax, 2			; the lowest free descriptor, or nothing
	jne	.cap_no
	mov	eax, 1
	pop	rbx
	ret
  .cap_no:
	xor	eax, eax
	pop	rbx
	ret

; dv_capture_read -> rax = how many bytes of the captured stderr are now in
; `dv_rbuf`. Closes fd 2 first, so nothing is still in flight, then reads the
; file back through the same rt/sys.inc wrappers everything else here uses.
  dv_capture_read:
	push	rbx
	push	r12
	push	r13
	mov	rdi, 2
	call	sys_close
	mov	rdi, DRV_AT_FDCWD
	lea	rsi, [dv_perr]
	mov	rdx, DRV_O_RDONLY
	xor	rcx, rcx
	call	sys_openat
	jc	.cr_none
	mov	rbx, rax
	xor	r12, r12		; bytes read so far
  .cr_loop:
	cmp	r12, DV_RBUF_CAP
	jae	.cr_done
	mov	rdi, rbx
	lea	rsi, [dv_rbuf]
	add	rsi, r12
	mov	rdx, DV_RBUF_CAP
	sub	rdx, r12
	call	sys_read
	jc	.cr_done
	test	rax, rax
	jz	.cr_done
	add	r12, rax
	jmp	.cr_loop
  .cr_done:
	mov	rdi, rbx
	call	sys_close
	mov	rax, r12
	pop	r13
	pop	r12
	pop	rbx
	ret
  .cr_none:
	xor	eax, eax
	pop	r13
	pop	r12
	pop	rbx
	ret

; dv_count_code(code_num) -> rax = how many `Diag`s in `DrvCtx.diags` carry
; that numeric code. §8.3: tools match codes, never English prose.
  dv_count_code:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	rbp
	mov	r13, rdi
	mov	rbx, [r15 + DrvCtx.diags]
	xor	r12, r12
	xor	rbp, rbp
  .cc_loop:
	cmp	r12, [rbx + Vec.len]
	jae	.cc_done
	mov	rdi, rbx
	mov	rsi, r12
	call	vec_get
	mov	ecx, [rax + Diag.code_num]
	cmp	rcx, r13
	jne	.cc_next
	inc	rbp
  .cc_next:
	inc	r12
	jmp	.cc_loop
  .cc_done:
	mov	rax, rbp
	pop	rbp
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	ret

; dv_count_eof -> rax = how many TOK_EOF tokens `DrvCtx.toks` holds. One, for
; a unit of any size: `lex_tokenize` ends every file with one and
; driver/run.inc drops all but the last, because `__cst_module` stops at the
; first EOF it sees.
  dv_count_eof:
	push	rbx
	push	r12
	push	r13
	mov	rbx, [r15 + DrvCtx.toks]
	xor	r12, r12
	xor	r13, r13
  .ce_loop:
	cmp	r12, [rbx + Vec.len]
	jae	.ce_done
	mov	rdi, rbx
	mov	rsi, r12
	call	vec_get
	cmp	dword [rax + Tok.kind], TOK_EOF
	jne	.ce_next
	inc	r13
  .ce_next:
	inc	r12
	jmp	.ce_loop
  .ce_done:
	mov	rax, r13
	pop	r13
	pop	r12
	pop	rbx
	ret

; dv_count_files -> rax = how many DISTINCT `Span.file_id` values the token
; vector carries. rt/span.inc defines `file_id` as an INTERN ID of the path,
; not a 1-based position, so the values are whatever the interner handed out
; -- what matters is that a token knows which FILE it came from, and that
; more than one file is represented. Counts a change of value in a stream
; that is already grouped by file, which is what command-line order makes it.
  dv_count_files:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	rbp
	mov	rbx, [r15 + DrvCtx.toks]
	xor	r12, r12
	xor	r13, r13		; how many distinct so far
	mov	r14d, -1		; the previous file_id
  .cf_loop:
	cmp	r12, [rbx + Vec.len]
	jae	.cf_done
	mov	rdi, rbx
	mov	rsi, r12
	call	vec_get
	mov	ebp, [rax + Tok.span.file_id]
	cmp	ebp, r14d
	je	.cf_next
	inc	r13
	mov	r14d, ebp
  .cf_next:
	inc	r12
	jmp	.cf_loop
  .cf_done:
	mov	rax, r13
	pop	rbp
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	ret

; dv_spans_ordered -> eax = 1 if every token's span start is >= the previous
; one's, else 0. The unit is ONE text: rewriting each file's spans by that
; file's base must leave the stream monotonic, and a file whose spans were
; left in its own local coordinates would show up here as a jump backwards.
  dv_spans_ordered:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	rbp
	mov	rbx, [r15 + DrvCtx.toks]
	xor	r12, r12
	xor	r14, r14		; the previous span start
  .so_loop:
	cmp	r12, [rbx + Vec.len]
	jae	.so_ok
	mov	rdi, rbx
	mov	rsi, r12
	call	vec_get
	mov	ebp, [rax + Tok.span.start]
	cmp	rbp, r14
	jb	.so_no
	mov	r14, rbp
	inc	r12
	jmp	.so_loop
  .so_ok:
	mov	eax, 1
	jmp	.so_done
  .so_no:
	xor	eax, eax
  .so_done:
	pop	rbp
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	ret

; dv_render_diag(index) -> rax = the number of bytes `diag_render_text` wrote
; into `dv_rbuf` for that record, localized first. Exactly what
; `drv_emit_diag` (driver/run.inc) does before it writes to stderr: copy,
; `__drv_localize`, render -- so what this fixture searches is the text `exsc`
; would have printed, not an internal field.
  dv_render_diag:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	rbx, [r15 + DrvCtx.diags]
	mov	rdi, rbx
	mov	rsi, r12
	call	vec_get
	lea	rdi, [dv_dgcopy]
	mov	rsi, rax
	mov	rcx, sizeof.Diag
	cld
	rep	movsb
	lea	rdi, [dv_dgcopy]
	call	__drv_localize
	lea	rdi, [dv_dgcopy]
	lea	rsi, [dv_rbuf]
	mov	rdx, DV_RBUF_CAP
	call	diag_render_text
	pop	r13
	pop	r12
	pop	rbx
	ret

; dv_code_renders_as(code_num, needle, needlen) -> eax = 1 if some `Diag`
; with that code renders to text containing `needle`.
  dv_code_renders_as:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	rbp
	mov	r12, rsi
	mov	r13, rdx
	mov	r14, rdi
	mov	rbx, [r15 + DrvCtx.diags]
	xor	rbp, rbp
  .cr_loop:
	cmp	rbp, [rbx + Vec.len]
	jae	.cr_miss
	mov	rdi, rbx
	mov	rsi, rbp
	call	vec_get
	mov	ecx, [rax + Diag.code_num]
	cmp	rcx, r14
	jne	.cr_next
	mov	rdi, rbp
	call	dv_render_diag
	lea	rdi, [dv_rbuf]
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, r13
	call	dv_contains
	test	eax, eax
	jnz	.cr_hit
  .cr_next:
	inc	rbp
	jmp	.cr_loop
  .cr_hit:
	mov	eax, 1
	jmp	.cr_done
  .cr_miss:
	xor	eax, eax
  .cr_done:
	pop	rbp
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	ret

; dv_dump_ast(buf) -> rax = the number of bytes written. `ast_dump` is the
; same routine `drv_emit_dump` calls for `--emitte ast` (driver/dump.inc), and
; a `DiagOut` is the same bounded writer it hands it.
  dv_dump_ast:
	push	rbx
	push	r12
	push	r13
	mov	rbx, rdi
	lea	rdi, [dv_wr]
	mov	rsi, rbx
	mov	rdx, DV_DUMP_CAP
	call	diag_out_init
	mov	rdi, [r15 + DrvCtx.ast]
	lea	rsi, [dv_wr]
	call	ast_dump
	mov	rax, qword [dv_wr + DiagOut.len]
	pop	r13
	pop	r12
	pop	rbx
	ret

; dv_bytes_equal(a, alen, b, blen) -> eax = 1 if the two runs are equal.
  dv_bytes_equal:
	push	rbx
	cmp	rsi, rcx
	jne	.be_no
	xor	rbx, rbx
  .be_loop:
	cmp	rbx, rsi
	jae	.be_yes
	movzx	eax, byte [rdi + rbx]
	movzx	r8d, byte [rdx + rbx]
	cmp	eax, r8d
	jne	.be_no
	inc	rbx
	jmp	.be_loop
  .be_yes:
	mov	eax, 1
	pop	rbx
	ret
  .be_no:
	xor	eax, eax
	pop	rbx
	ret

; dv_contains(hay, haylen, needle, needlen) -> eax = 1 if `needle` occurs in
; `hay`. Lifted from tests/unit/driver_checker.asm.
  dv_contains:
	push	rbx
	push	r12
	push	r13
	push	r14
	mov	rbx, rdi
	mov	r12, rsi
	mov	r13, rdx
	mov	r14, rcx
	test	r14, r14
	jz	.ct_hit
	cmp	r14, r12
	ja	.ct_miss
	xor	rcx, rcx
  .ct_outer:
	mov	rax, rcx
	add	rax, r14
	cmp	rax, r12
	ja	.ct_miss
	xor	rdx, rdx
  .ct_inner:
	cmp	rdx, r14
	jae	.ct_hit
	lea	rdi, [rbx + rcx]
	movzx	eax, byte [rdi + rdx]
	movzx	esi, byte [r13 + rdx]
	cmp	eax, esi
	jne	.ct_next
	inc	rdx
	jmp	.ct_inner
  .ct_next:
	inc	rcx
	jmp	.ct_outer
  .ct_hit:
	mov	eax, 1
	jmp	.ct_done
  .ct_miss:
	xor	eax, eax
  .ct_done:
	pop	r14
	pop	r13
	pop	r12
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
  ; ---- the hello world's three files, byte for byte ---------------------
  ; `file` resolves relative to THIS source file, so these are the real
  ; examples/ bytes at assembly time and cannot drift from them.
  dv_ex_a: file '../../examples/saluta.exsc'
  DV_EX_A_LEN = $ - dv_ex_a
  dv_ex_b: file '../../examples/imprime.exsc'
  DV_EX_B_LEN = $ - dv_ex_b
  dv_ex_c: file '../../examples/initium.exsc'
  DV_EX_C_LEN = $ - dv_ex_c

  ; ---- the three-file unit whose MIDDLE file does not parse -------------
  ; `redde 0 0;` is §8.6's own "every simple statement ends with a
  ; semicolon" case. It sits on line 3 of the second file, which is what
  ; checks 41/42 require the rendering to say.
  dv_src_q1 db '// q1',10,'publica functio unus() -> u8 {',10,'    redde 0;',10,'}',10
  DV_SRC_Q1_LEN = $ - dv_src_q1
  dv_src_q2 db '// q2',10,'publica functio duo() -> u8 {',10,'    redde 0 0;',10,'}',10
  DV_SRC_Q2_LEN = $ - dv_src_q2
  dv_src_q3 db '// q3',10,'publica functio tres() -> u8 {',10,'    redde 0;',10,'}',10
  DV_SRC_Q3_LEN = $ - dv_src_q3

  ; CRLF, which §8.1 rejects with EXS-E0106 -- the gate, not the parser.
  dv_src_qc db '// qc',13,10,'publica functio quattuor() -> u8 {',13,10,'    redde 0;',13,10,'}',13,10
  DV_SRC_QC_LEN = $ - dv_src_qc

  dv_s_exsc     db 'exsc',0
  dv_s_aedifica db 'aedifica',0
  dv_s_hospes   db '--hospes',0
  dv_s_triple   db 'x86_64-linux',0

  dv_p1 db '/tmp/exsecutor-drvsrc-a.exsc'
  DV_P1_LEN = $ - dv_p1
  	db 0
  dv_p2 db '/tmp/exsecutor-drvsrc-b.exsc'
  DV_P2_LEN = $ - dv_p2
  	db 0
  dv_p3 db '/tmp/exsecutor-drvsrc-c.exsc'
  DV_P3_LEN = $ - dv_p3
  	db 0
  dv_q1 db '/tmp/exsecutor-drvsrc-q1.exsc'
  DV_Q1_LEN = $ - dv_q1
  	db 0
  dv_q2 db '/tmp/exsecutor-drvsrc-q2.exsc'
  DV_Q2_LEN = $ - dv_q2
  	db 0
  dv_q3 db '/tmp/exsecutor-drvsrc-q3.exsc'
  DV_Q3_LEN = $ - dv_q3
  	db 0
  dv_qc db '/tmp/exsecutor-drvsrc-qc.exsc'
  DV_QC_LEN = $ - dv_qc
  	db 0
  dv_perr db '/tmp/exsecutor-drvsrc-stderr.txt',0

  ; What checks 41/42/43 look for in the rendered diagnostic. The line is
  ; part of the needle: `q2.exsc:3:` is the second file's OWN third line,
  ; which is only true if the offset was mapped back out of unit
  ; coordinates.
  dv_n_q1     db 'drvsrc-q1.exsc'
  DV_N_Q1_LEN = $ - dv_n_q1
  dv_n_q2     db 'drvsrc-q2.exsc'
  DV_N_Q2_LEN = $ - dv_n_q2
  dv_n_q3     db 'drvsrc-q3.exsc'
  DV_N_Q3_LEN = $ - dv_n_q3
  dv_n_q2line db 'drvsrc-q2.exsc:3:'
  DV_N_Q2LINE_LEN = $ - dv_n_q2line
  dv_n_qc     db 'drvsrc-qc.exsc'
  DV_N_QC_LEN = $ - dv_n_qc

  dv_s_scriptor db 'Scriptor'
  DV_S_SCRIPTOR_LEN = $ - dv_s_scriptor

dv_av_c:
	dq dv_s_exsc, dv_s_aedifica, dv_s_hospes, dv_s_triple, dv_p3
dv_av_c_end:
DV_AV_C_N = (dv_av_c_end - dv_av_c) / 8
  assert DV_AV_C_N = 5

dv_av_abc:
	dq dv_s_exsc, dv_s_aedifica, dv_s_hospes, dv_s_triple, dv_p1, dv_p2, dv_p3
dv_av_abc_end:
DV_AV_ABC_N = (dv_av_abc_end - dv_av_abc) / 8
  assert DV_AV_ABC_N = 7

dv_av_cba:
	dq dv_s_exsc, dv_s_aedifica, dv_s_hospes, dv_s_triple, dv_p3, dv_p2, dv_p1
dv_av_cba_end:
DV_AV_CBA_N = (dv_av_cba_end - dv_av_cba) / 8
  assert DV_AV_CBA_N = 7

dv_av_q:
	dq dv_s_exsc, dv_s_aedifica, dv_s_hospes, dv_s_triple, dv_q1, dv_q2, dv_q3
dv_av_q_end:
DV_AV_Q_N = (dv_av_q_end - dv_av_q) / 8
  assert DV_AV_Q_N = 7

dv_av_q13:
	dq dv_s_exsc, dv_s_aedifica, dv_s_hospes, dv_s_triple, dv_q1, dv_q3
dv_av_q13_end:
DV_AV_Q13_N = (dv_av_q13_end - dv_av_q13) / 8
  assert DV_AV_Q13_N = 6

dv_av_g:
	dq dv_s_exsc, dv_s_aedifica, dv_s_hospes, dv_s_triple, dv_q1, dv_qc, dv_q3
dv_av_g_end:
DV_AV_G_N = (dv_av_g_end - dv_av_g) / 8
  assert DV_AV_G_N = 7

dv_av_dup:
	dq dv_s_exsc, dv_s_aedifica, dv_s_hospes, dv_s_triple, dv_p1, dv_p1
dv_av_dup_end:
DV_AV_DUP_N = (dv_av_dup_end - dv_av_dup) / 8
  assert DV_AV_DUP_N = 6

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  dv_ctx	rb sizeof.DrvCtx
  dv_arena	rb sizeof.Arena
  dv_iarena	rb sizeof.Arena
  dv_scratch	rb sizeof.Arena
  dv_interner	rb sizeof.Interner
  dv_envmap	rb sizeof.Map
  dv_srcs	rb DRV_SOURCES_MAX * sizeof.DrvSrc
  dv_vlx	rb sizeof.Lexer
  dv_vtoks	rb sizeof.Vec
  dv_vdiags	rb sizeof.Vec
  dv_lx		rb sizeof.Lexer
  dv_toks	rb sizeof.Vec
  dv_diags	rb sizeof.Vec
  dv_green	rb sizeof.Vec
  dv_work	rb sizeof.Vec
  dv_cmap	rb sizeof.Map
  dv_ctree	rb sizeof.CstTree
  dv_parser	rb sizeof.CstParser
  dv_ast	rb sizeof.Ast
  dv_chk	rb sizeof.ChkCtx
  dv_dgcopy	rb sizeof.Diag
  dv_wr		rb 32
  dv_rbuf	rb DV_RBUF_CAP
  dv_dump1	rb DV_DUMP_CAP
  dv_dump2	rb DV_DUMP_CAP
  dv_dump3	rb DV_DUMP_CAP
