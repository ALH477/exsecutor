; tests/unit/driver_checker.asm
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
; `drv_aedifica` now calls Stage 2's checker (checker/checker.inc) between
; `ast_verify_stage1` and `--emitte`, exactly as that file's own "HOW THE
; DRIVER CALLS THIS" note specifies, plus the `chk_set_target` call that note
; leaves to the driver. This fixture is what proves the WIRING, not the
; checker itself -- `checker/resolve/resolve.inc`'s own fixtures (e.g.
; `chk_e0500_module_mutabilis.asm`) already prove E0500 given a hand-built
; tree; this one proves that `exsc aedifica`, given real source bytes on
; disk, actually reaches `chk_init`/`chk_set_source`/`chk_set_target`/
; `chk_run` in the right place, with the right `CHK_F_PROGRAM` bit, and that
; a checker diagnostic renders through the SAME `.report` path and exit
; status as a lexer or parser one.
;
; THIS NEEDS REAL FILES ON DISK, unlike most of this directory. `driver_status
; .asm`'s own header explains why that usually means testing against
; examples/ instead ("tests/run.sh does not promise a working directory, and
; a path guessed relative to it is a fixture that passes for the wrong
; reason"). That objection is about a path GUESSED relative to an unknown
; cwd; it does not apply to a path this fixture WRITES itself, under a fixed
; absolute name under /tmp, then reads back -- no cwd is involved on either
; end. `read write close openat` are already on the closed syscall allowlist
; (CLAUDE.md) and already reached through rt/sys.inc from driver/io.inc; this
; fixture calls the same wrappers directly to create its own input, which is
; not a new capability, just a new caller of one every fixture in this tree
; already links against. There is no `unlink` on that allowlist, so the two
; files this fixture writes are left behind under /tmp; `O_TRUNC` means a
; second run overwrites them cleanly rather than accumulating garbage.
;
; NON-VACUITY, THREE WAYS, EACH A ONE-KEYWORD OR ONE-FLAG EDIT FROM A CASE
; THAT MUST NOT REJECT:
;
;   `mutabilis c: i32 = 0;` (module level) rejects, EXACTLY EXS-E0500;
;   `firma     c: i32 = 0;` -- the same source, one keyword changed --
;   is clean. (checker/resolve/resolve.inc's own pair, spec §4.1 rule 7.)
;
;   The firma source, given `-o OUT`, rejects, EXACTLY EXS-E0424 (spec §4.1
;   rule 2: a PROGRAM was asked for and this module declares no `initium`);
;   the SAME source with no `-o` is clean -- proving `CHK_F_PROGRAM` is
;   actually conditioned on `DrvCtx.outpath`, not always on or always off.
;
;   `--hospes riscv64-linux` on a clean source is refused, exit 4 -- proving
;   the new "only x86_64-linux exists in this build" check (driver/run.inc)
;   actually runs, and runs BEFORE the source is ever opened: the path handed
;   to it does not exist, so acceptance would show up as exit 3 (a `drv_slurp`
;   host failure), not exit 4, and that distinction is check 40's whole point.
;
; Beyond the exit status, checks 11/21 also confirm the numeric `Diag.code_num`
; the checker actually appended, and checks 12/22 re-render that record with
; `diag_render_text` -- the same renderer `drv_emit_diag` calls -- and search
; the bytes it produced for the literal `EXS-E0500` / `EXS-E0424`, so a
; passing fixture is evidence about what `exsc` would have PRINTED, not just
; about an internal field no diagnostic consumer ever reads.
;
; Exit 0 = every check passed. Otherwise:
;   99      setup (arena_init / writing a fixture source file) failed
;   10      the mutabilis source did not exit DRV_EXIT_DIAG
;   11      ... did not append a Diag with code_num = 500
;   12      ... that Diag's rendered text did not contain "EXS-E0500"
;   20      the firma twin (no -o) did not exit DRV_EXIT_OK
;   21      ... or raised a diagnostic anyway
;   30      the firma source with -o did not exit DRV_EXIT_DIAG
;   31      ... did not append a Diag with code_num = 424
;   32      ... that Diag's rendered text did not contain "EXS-E0424"
;   40      an unsupported --hospes triple did not exit DRV_EXIT_TODO
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §4.1 rules 2 and 7, §9.5, §13,
; §16 Stage 2; docs/design/checker.md.
;
; TEST: run=yes expect-exit=0 audit=skip

include 'format/format.inc'

format ELF64 executable 3
entry start

DC_ARENA = 1 shl 20
DC_O_WRONLY_CREAT_TRUNC = 0x241	; O_WRONLY(1) | O_CREAT(0x40) | O_TRUNC(0x200)
DC_MODE_0644 = 420

segment readable executable
  start:
	lea	r15, [dc_ctx]
	call	dc_bind

	lea	rdi, [dc_p_mut]
	mov	rsi, DC_P_MUT_LEN
	lea	rdx, [dc_src_mut]
	mov	rcx, DC_SRC_MUT_LEN
	call	dc_write_file
	test	eax, eax
	jz	.f99

	lea	rdi, [dc_p_firma]
	mov	rsi, DC_P_FIRMA_LEN
	lea	rdx, [dc_src_firma]
	mov	rcx, DC_SRC_FIRMA_LEN
	call	dc_write_file
	test	eax, eax
	jz	.f99

	; ---- 10/11/12: module-level `mutabilis` -- EXS-E0500 -----------------
	mov	rdi, DC_AV_MUT_N
	lea	rsi, [dc_av_mut]
	call	drv_main
	cmp	eax, DRV_EXIT_DIAG
	jne	.f10
	mov	rdi, 500
	call	dc_find_code
	test	eax, eax
	jz	.f11
	mov	rdi, 500
	lea	rsi, [dc_s_e0500]
	mov	rdx, DC_S_E0500_LEN
	call	dc_code_renders_as
	test	eax, eax
	jz	.f12

	; ---- 20/21: the same source, `firma` instead of `mutabilis` ---------
	mov	rdi, DC_AV_FIRMA_N
	lea	rsi, [dc_av_firma]
	call	drv_main
	cmp	eax, DRV_EXIT_OK
	jne	.f20
	mov	rbx, [r15 + DrvCtx.diags]
	cmp	qword [rbx + Vec.len], 0
	jne	.f21

	; ---- 30/31/32: firma + `-o` -- no `initium`, EXS-E0424 ---------------
	mov	rdi, DC_AV_FIRMA_O_N
	lea	rsi, [dc_av_firma_o]
	call	drv_main
	cmp	eax, DRV_EXIT_DIAG
	jne	.f30
	mov	rdi, 424
	call	dc_find_code
	test	eax, eax
	jz	.f31
	mov	rdi, 424
	lea	rsi, [dc_s_e0424]
	mov	rdx, DC_S_E0424_LEN
	call	dc_code_renders_as
	test	eax, eax
	jz	.f32

	; ---- 40: an unsupported --hospes triple, exit 4, file untouched ------
	; The path in dc_av_badhospes does not exist. If the triple check ran
	; AFTER `drv_slurp`, this would come back DRV_EXIT_HOST (3), not
	; DRV_EXIT_TODO (4) -- so this check is also an ordering check.
	mov	rdi, DC_AV_BADHOSPES_N
	lea	rsi, [dc_av_badhospes]
	call	drv_main
	cmp	eax, DRV_EXIT_TODO
	jne	.f40

	mov	eax, 231
	xor	edi, edi
	syscall

  .f99: mov edi, 99
	jmp dc_die
  .f10: mov edi, 10
	jmp dc_die
  .f11: mov edi, 11
	jmp dc_die
  .f12: mov edi, 12
	jmp dc_die
  .f20: mov edi, 20
	jmp dc_die
  .f21: mov edi, 21
	jmp dc_die
  .f30: mov edi, 30
	jmp dc_die
  .f31: mov edi, 31
	jmp dc_die
  .f32: mov edi, 32
	jmp dc_die
  .f40: mov edi, 40
	jmp dc_die

  dc_die:
	mov	eax, 231
	syscall

; dc_bind -- what compiler/x86_64/exsc.asm's `start` does: name the process's
; mutable objects in the context r15 points at. Every field `drv_aedifica`
; reaches, including `chk` (this fixture's whole point) and the CST/AST
; fields `driver_status.asm` never needed to bind because none of ITS checks
; run the front end that far.
  dc_bind:
	lea	rax, [dc_arena]
	mov	[r15 + DrvCtx.arena], rax
	lea	rax, [dc_iarena]
	mov	[r15 + DrvCtx.iarena], rax
	lea	rax, [dc_scratch]
	mov	[r15 + DrvCtx.scratch], rax
	lea	rax, [dc_interner]
	mov	[r15 + DrvCtx.interner], rax
	lea	rax, [dc_envmap]
	mov	[r15 + DrvCtx.envmap], rax
	lea	rax, [dc_srcs]
	mov	[r15 + DrvCtx.srcs], rax	; the §12 SOURCE table (driver/cli.inc)
	lea	rax, [dc_vlx]
	mov	[r15 + DrvCtx.vlx], rax
	lea	rax, [dc_vtoks]
	mov	[r15 + DrvCtx.vtoks], rax
	lea	rax, [dc_vdiags]
	mov	[r15 + DrvCtx.vdiags], rax
	lea	rax, [dc_lx]
	mov	[r15 + DrvCtx.lx], rax
	lea	rax, [dc_toks]
	mov	[r15 + DrvCtx.toks], rax
	lea	rax, [dc_diags]
	mov	[r15 + DrvCtx.diags], rax
	lea	rax, [dc_green]
	mov	[r15 + DrvCtx.green], rax
	lea	rax, [dc_work]
	mov	[r15 + DrvCtx.work], rax
	lea	rax, [dc_cmap]
	mov	[r15 + DrvCtx.cmap], rax
	lea	rax, [dc_ctree]
	mov	[r15 + DrvCtx.ctree], rax
	lea	rax, [dc_parser]
	mov	[r15 + DrvCtx.parser], rax
	lea	rax, [dc_ast]
	mov	[r15 + DrvCtx.ast], rax
	lea	rax, [dc_chk]
	mov	[r15 + DrvCtx.chk], rax
	ret

; dc_write_file(path, pathlen, data, datalen) -> eax = 1 on success, else 0.
; `path` must already be NUL-terminated (every literal below is). A single
; `sys_write` call, not a short-write loop: every buffer here is well under a
; page, and a short write to a fresh regular file at offset 0 is not a
; failure mode this fixture is trying to exercise.
  dc_write_file:
	push	rbx
	push	r12
	push	r13
	mov	rbx, rdi		; path
	mov	r12, rdx		; data
	mov	r13, rcx		; datalen

	mov	rdi, DRV_AT_FDCWD
	mov	rsi, rbx
	mov	rdx, DC_O_WRONLY_CREAT_TRUNC
	mov	rcx, DC_MODE_0644
	call	sys_openat
	jc	.wf_fail
	mov	rbx, rax		; fd

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

; dc_find_code(code_num) -> eax = 1 if `DrvCtx.diags` at r15 holds a `Diag`
; whose `code_num` equals the argument, else 0. Linear scan -- these vectors
; hold at most a handful of records in this fixture.
  dc_find_code:
	push	rbx
	push	r12
	push	r13
	mov	r13, rdi		; the code to look for
	mov	rbx, [r15 + DrvCtx.diags]
	xor	r12, r12
  .fc_loop:
	cmp	r12, [rbx + Vec.len]
	jae	.fc_miss
	mov	rdi, rbx
	mov	rsi, r12
	call	vec_get
	mov	ecx, [rax + Diag.code_num]
	cmp	rcx, r13
	je	.fc_hit
	inc	r12
	jmp	.fc_loop
  .fc_hit:
	mov	eax, 1
	jmp	.fc_done
  .fc_miss:
	xor	eax, eax
  .fc_done:
	pop	r13
	pop	r12
	pop	rbx
	ret

; dc_contains(hay, haylen, needle, needlen) -> eax = 1 if `needle` occurs
; anywhere in `hay`, else 0. O(n*m); every haystack here is one rendered
; diagnostic, at most a few hundred bytes.
  dc_contains:
	push	rbx
	push	r12
	push	r13
	push	r14
	mov	rbx, rdi		; hay
	mov	r12, rsi		; haylen
	mov	r13, rdx		; needle
	mov	r14, rcx		; needlen
	test	r14, r14
	jz	.ct_hit			; an empty needle always "occurs"
	cmp	r14, r12
	ja	.ct_miss
	xor	rcx, rcx		; outer index into hay
  .ct_outer:
	mov	rax, rcx
	add	rax, r14
	cmp	rax, r12
	ja	.ct_miss
	xor	rdx, rdx		; inner index into needle
  .ct_inner:
	cmp	rdx, r14
	jae	.ct_hit
	lea	rdi, [rbx + rcx]	; x86 addressing has no base+index+index
	movzx	eax, byte [rdi + rdx]	; form, so the hay offset is folded first
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

; dc_code_renders_as(code_num, needle, needlen) -> eax = 1 if `DrvCtx.diags`
; holds a `Diag` with `code_num` equal to the argument AND `diag_render_text`
; over it produces bytes containing `needle`. Renders through the exact
; function `drv_emit_diag` (driver/run.inc) calls, so a hit here is evidence
; about the text `exsc` would have written to stderr, not just about
; `Diag.code_num`.
; FIVE callee-saved registers, deliberately, not four: `vec_get` and
; `diag_render_text` are ordinary `proc`s and may clobber any caller-saved
; register (docs/asm-conventions.md, "1.1 Register roles"), so every value
; that has to survive a `call` below -- the diags vector, the code to match,
; the needle and its length, AND the loop index -- needs a callee-saved home,
; not a stack slot this routine would have to track by hand. `rbp` is free
; to use as a fifth one here: this routine sets up no frame and nothing else
; in it depends on `rbp` meaning anything in particular. Five pushes is also
; what keeps every `call` inside this routine 16-byte aligned (docs/asm-
; conventions.md, "2. Register discipline") -- entry lands at rsp = 8 (mod
; 16), and an ODD number of 8-byte pushes is what brings that back to 0.
  dc_code_renders_as:
	push	rbx
	push	r12
	push	r13
	push	r14
	push	rbp
	mov	r12, rsi		; needle
	mov	r13, rdx		; needlen
	mov	r14, rdi		; code to look for
	mov	rbx, [r15 + DrvCtx.diags]
	xor	rbp, rbp		; index
  .cr_loop:
	cmp	rbp, [rbx + Vec.len]
	jae	.cr_miss
	mov	rdi, rbx
	mov	rsi, rbp
	call	vec_get
	mov	r10, rax		; the Diag*
	cmp	dword [r10 + Diag.code_num], r14d
	je	.cr_render
	inc	rbp
	jmp	.cr_loop
  .cr_render:
	mov	rdi, r10
	lea	rsi, [dc_rbuf]
	mov	rdx, DC_RBUF_CAP
	call	diag_render_text	; rax = bytes written
	lea	rdi, [dc_rbuf]
	mov	rsi, rax
	mov	rdx, r12
	mov	rcx, r13
	call	dc_contains
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
  dc_s_exsc     db 'exsc',0
  dc_s_aedifica db 'aedifica',0
  dc_s_hospes   db '--hospes',0
  dc_s_triple   db 'x86_64-linux',0
  dc_s_triple_bad db 'riscv64-linux',0
  dc_s_o        db '-o',0
  dc_s_outpath  db '/tmp/exsc-fixture-driver-checker.o',0
  dc_s_badpath  db '/nonexistent-exsc-fixture-driver-checker-path.exsc',0

  ; The two source files this fixture writes to disk, and reads back through
  ; the real `drv_aedifica` pipeline -- see this file's header.
  dc_p_mut      db '/tmp/exsc-fixture-driver-checker-mutabilis.exsc',0
  DC_P_MUT_LEN = ($ - dc_p_mut) - 1
  dc_p_firma    db '/tmp/exsc-fixture-driver-checker-firma.exsc',0
  DC_P_FIRMA_LEN = ($ - dc_p_firma) - 1

  ; checker/resolve/resolve.inc's own pair (its header: "THE PAIR IS ONE EDIT
  ; APART, AND THE EDIT IS THE KEYWORD"), reused here at the driver level.
  dc_src_mut    db 'mutabilis c: i32 = 0;', 10
  DC_SRC_MUT_LEN = $ - dc_src_mut
  dc_src_firma  db 'firma c: i32 = 0;', 10
  DC_SRC_FIRMA_LEN = $ - dc_src_firma

  dc_s_e0500    db 'EXS-E0500'
  DC_S_E0500_LEN = $ - dc_s_e0500
  dc_s_e0424    db 'EXS-E0424'
  DC_S_E0424_LEN = $ - dc_s_e0424

dc_av_mut:
	dq dc_s_exsc, dc_s_aedifica, dc_s_hospes, dc_s_triple, dc_p_mut
dc_av_mut_end:
DC_AV_MUT_N = (dc_av_mut_end - dc_av_mut) / 8
  assert DC_AV_MUT_N = 5

dc_av_firma:
	dq dc_s_exsc, dc_s_aedifica, dc_s_hospes, dc_s_triple, dc_p_firma
dc_av_firma_end:
DC_AV_FIRMA_N = (dc_av_firma_end - dc_av_firma) / 8
  assert DC_AV_FIRMA_N = 5

dc_av_firma_o:
	dq dc_s_exsc, dc_s_aedifica, dc_s_hospes, dc_s_triple, dc_s_o, dc_s_outpath, dc_p_firma
dc_av_firma_o_end:
DC_AV_FIRMA_O_N = (dc_av_firma_o_end - dc_av_firma_o) / 8
  assert DC_AV_FIRMA_O_N = 7

dc_av_badhospes:
	dq dc_s_exsc, dc_s_aedifica, dc_s_hospes, dc_s_triple_bad, dc_s_badpath
dc_av_badhospes_end:
DC_AV_BADHOSPES_N = (dc_av_badhospes_end - dc_av_badhospes) / 8
  assert DC_AV_BADHOSPES_N = 5

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  DC_RBUF_CAP = 4096
  dc_rbuf	rb DC_RBUF_CAP

  dc_ctx	rb sizeof.DrvCtx
  dc_arena	rb sizeof.Arena
  dc_iarena	rb sizeof.Arena
  dc_scratch	rb sizeof.Arena
  dc_interner	rb sizeof.Interner
  dc_envmap	rb sizeof.Map
  dc_srcs	rb DRV_SOURCES_MAX * sizeof.DrvSrc
  dc_vlx	rb sizeof.Lexer
  dc_vtoks	rb sizeof.Vec
  dc_vdiags	rb sizeof.Vec
  dc_lx		rb sizeof.Lexer
  dc_toks	rb sizeof.Vec
  dc_diags	rb sizeof.Vec
  dc_green	rb sizeof.Vec
  dc_work	rb sizeof.Vec
  dc_cmap	rb sizeof.Map
  dc_ctree	rb sizeof.CstTree
  dc_parser	rb sizeof.CstParser
  dc_ast	rb sizeof.Ast
  dc_chk	rb sizeof.ChkCtx
