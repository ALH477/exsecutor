; tests/unit/driver_status.asm
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
; THE EXIT STATUSES, pinned as values rather than as a paragraph. `drv_main`
; is called with a real argv array and the number it returns is compared with
; the number this fixture says it owes.
;
; This exists because the statuses are a PUBLIC INTERFACE. tests/conformance/
; (spec §14) has to tell "this source was rejected" from "this source is
; clean" from "the compiler could not run at all", and §14's entries are all
; of the form "must fail to compile" -- a suite that cannot distinguish exit 1
; from exit 3 reports a passing conformance run for a compiler that crashed on
; every entry. Renumbering any row below breaks that suite, which is the
; point: the numbers are not free.
;
;   0  DRV_EXIT_OK     the requested work completed, nothing to report
;   1  DRV_EXIT_DIAG   at least one diagnostic was emitted
;   2  DRV_EXIT_USAGE  malformed invocation -- no EXS-E code (§9.3)
;   3  DRV_EXIT_HOST   a syscall failed, or output could not be rendered
;   4  DRV_EXIT_TODO   the requested work is not implemented
;
; NINE OF §12's TEN SUBCOMMANDS ARE REFUSALS, and the loop below walks
; `drv_cmd_tab` rather than a copy of the list, so adding a subcommand to §12
; without deciding what it does fails here rather than exiting 0 in silence.
;
; WHAT IS NOT HERE, and why. Exit 0 and exit 1 need a source file on disk, and
; a fixture that opens one has to know where it is -- tests/run.sh does not
; promise a working directory, and a path guessed relative to it is a fixture
; that passes for the wrong reason. Those two statuses are exercised
; end-to-end against examples/ instead. What IS here is every status reachable
; without touching a file that has to exist: 2, 3 and 4, plus the two
; `drv_slurp_unit` refusals that produce 3.
;
; Exit 0 = every check passed. Otherwise:
;   50  argc < 2 (no subcommand) did not give DRV_EXIT_USAGE
;   51  an unknown subcommand did not give DRV_EXIT_USAGE
;   52  one of §12's nine unimplemented subcommands did not give
;       DRV_EXIT_TODO -- its index is added
;   70  a missing --hospes did not give DRV_EXIT_USAGE (§9.5)
;   71  an unreadable SOURCE did not give DRV_EXIT_HOST
;   72  drv_slurp_unit returned a buffer for a path that does not exist
;   73  drv_slurp_unit returned a buffer for a directory
;   99  setup failed
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3, §9.5, §12, §14.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

DS_ARENA = 1 shl 20

segment readable executable
  start:
	lea	r15, [ds_ctx]
	call	ds_bind

	; ---- 50: no subcommand ----------------------------------------------
	; §12: "A subcommand is required; there is no bare form."
	mov	rdi, 1
	lea	rsi, [ds_av_bare]
	call	drv_main
	cmp	eax, DRV_EXIT_USAGE
	jne	.f50

	; ---- 51: a subcommand that is not one of §12's ----------------------
	mov	rdi, 2
	lea	rsi, [ds_av_unknown]
	call	drv_main
	cmp	eax, DRV_EXIT_USAGE
	jne	.f51

	; ---- 52: the nine refusals, walked off drv_cmd_tab ------------------
	xor	rbx, rbx
  .cmdloop:
	cmp	rbx, DRV_CMD_COUNT
	jge	.cmddone
	cmp	rbx, DRV_CMD_AEDIFICA
	je	.cmdnext		; the one that does something
	mov	rax, [drv_cmd_tab + rbx*8]
	mov	[ds_av2 + 8], rax
	mov	rdi, 2
	lea	rsi, [ds_av2]
	call	drv_main
	cmp	eax, DRV_EXIT_TODO
	jne	.f52
  .cmdnext:
	inc	rbx
	jmp	.cmdloop
  .cmddone:

	; ---- 70: aedifica with no --hospes (§9.5) ---------------------------
	mov	rdi, DS_NOHOSPES_N
	lea	rsi, [ds_av_nohospes]
	call	drv_main
	cmp	eax, DRV_EXIT_USAGE
	jne	.f70

	; ---- 71: aedifica on a SOURCE that cannot be read -------------------
	; A host failure, not a diagnostic: there is no EXS-E code for "the
	; file is not there", and exit 3 is what says so.
	mov	rdi, DS_NOFILE_N
	lea	rsi, [ds_av_nofile]
	call	drv_main
	cmp	eax, DRV_EXIT_HOST
	jne	.f71

	; ---- 72/73: drv_slurp_unit's own refusals ---------------------------
	; Its sentinel is a 0 RETURN, not CF -- a successful mmap-backed
	; allocation is never address 0. Both paths must also have said why,
	; which tests/run.sh shows in the runlog when this fixture fails.
	;
	; A ONE-ENTRY UNIT, built by hand: `drv_slurp_unit` reads
	; `DrvCtx.srcs`/`nsrc` rather than taking a path, because §12's unit is
	; N files and the arena has to be sized from all of them at once.
	call	ds_arenas
	mov	qword [ds_len], 0xDEAD
	lea	rdi, [ds_s_nofile]
	mov	rsi, DS_NOFILE_LEN
	call	ds_one_source
	lea	rdi, [ds_len]
	call	drv_slurp_unit
	test	rax, rax
	jnz	.f72
	cmp	qword [ds_len], 0xDEAD	; a refusal writes no length either
	jne	.f72

	mov	qword [ds_len], 0xDEAD
	lea	rdi, [ds_s_root]
	mov	rsi, DS_ROOT_LEN
	call	ds_one_source
	lea	rdi, [ds_len]
	call	drv_slurp_unit
	test	rax, rax
	jnz	.f73
	cmp	qword [ds_len], 0xDEAD
	jne	.f73

	mov	eax, 231
	xor	edi, edi
	syscall

  .f50:	mov	edi, 50
	jmp	ds_die
  .f51:	mov	edi, 51
	jmp	ds_die
  .f52:	lea	rdi, [rbx + 52]
	jmp	ds_die
  .f70:	mov	edi, 70
	jmp	ds_die
  .f71:	mov	edi, 71
	jmp	ds_die
  .f72:	mov	edi, 72
	jmp	ds_die
  .f73:	mov	edi, 73
	jmp	ds_die

  ds_die:
	mov	eax, 231
	syscall

; ds_bind -- what compiler/x86_64/exsc.asm's `start` does: name the process's
; mutable objects in the context r15 points at. NOT the arenas -- `drv_main`
; builds those itself on the `aedifica` path, once per call, which is what
; makes calling it repeatedly here safe.
  ds_bind:
	lea	rax, [ds_arena]
	mov	[r15 + DrvCtx.arena], rax
	lea	rax, [ds_iarena]
	mov	[r15 + DrvCtx.iarena], rax
	lea	rax, [ds_scratch]
	mov	[r15 + DrvCtx.scratch], rax
	lea	rax, [ds_interner]
	mov	[r15 + DrvCtx.interner], rax
	lea	rax, [ds_envmap]
	mov	[r15 + DrvCtx.envmap], rax
	lea	rax, [ds_srcs]
	mov	[r15 + DrvCtx.srcs], rax	; the §12 SOURCE table (driver/cli.inc)
	lea	rax, [ds_vlx]
	mov	[r15 + DrvCtx.vlx], rax
	lea	rax, [ds_vtoks]
	mov	[r15 + DrvCtx.vtoks], rax
	lea	rax, [ds_vdiags]
	mov	[r15 + DrvCtx.vdiags], rax
	lea	rax, [ds_lx]
	mov	[r15 + DrvCtx.lx], rax
	lea	rax, [ds_toks]
	mov	[r15 + DrvCtx.toks], rax
	lea	rax, [ds_diags]
	mov	[r15 + DrvCtx.diags], rax
	ret

; ds_one_source(rdi = path, rsi = its length) -- make `DrvCtx.srcs` name
; exactly that one file, the way `drv_parse` would have. `drv_ctx_reset`
; clears `nsrc` so each call starts from an empty unit rather than appending
; to the previous one -- and appending the same path twice would be refused
; by `drv_src_add` anyway, which is the point of that check.
; Three pushes, not two: `call` needs rsp 16-byte aligned immediately before
; it and entry leaves it 8 off, so an ODD number of 8-byte pushes is what puts
; it back (docs/asm-conventions.md, "2. Register discipline"). `ds_arenas`,
; just below, does the same with one.
  ds_one_source:
	push	rbx
	push	r12
	push	rax
	mov	rbx, rdi
	mov	r12, rsi
	call	drv_ctx_reset
	mov	rdi, rbx
	mov	rsi, r12
	call	drv_src_add
	pop	rax
	pop	r12
	pop	rbx
	ret

; ds_arenas -- the interner arena `drv_slurp_unit` allocates its C-string path
; out of, for the two calls below that do not go through drv_main.
  ds_arenas:
	push	rbx
	lea	rdi, [ds_iarena]
	mov	rsi, DS_ARENA
	call	arena_init
	jc	.boom
	pop	rbx
	ret
  .boom:
	mov	edi, 99
	jmp	ds_die

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
  ds_s_exsc     db 'exsc',0
  ds_s_aedifica db 'aedifica',0
  ds_s_unknown  db 'aedificax',0
  ds_s_hospes   db '--hospes',0
  ds_s_triple   db 'x86_64-linux',0
  ds_s_nofile   db '/nonexistent-exsecutor-fixture-path'
  DS_NOFILE_LEN = $ - ds_s_nofile
  		db 0
  ds_s_root     db '/'
  DS_ROOT_LEN   = $ - ds_s_root
  		db 0

ds_av_bare:
	dq ds_s_exsc
ds_av_unknown:
	dq ds_s_exsc, ds_s_unknown
ds_av_nohospes:
	dq ds_s_exsc, ds_s_aedifica, ds_s_nofile
ds_av_nohospes_end:
DS_NOHOSPES_N = (ds_av_nohospes_end - ds_av_nohospes) / 8
  assert DS_NOHOSPES_N = 3
ds_av_nofile:
	dq ds_s_exsc, ds_s_aedifica, ds_s_hospes, ds_s_triple, ds_s_nofile
ds_av_nofile_end:
DS_NOFILE_N = (ds_av_nofile_end - ds_av_nofile) / 8
  assert DS_NOFILE_N = 5

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  ; argv[1] is rewritten in place per subcommand, so the loop walks
  ; drv_cmd_tab itself rather than a second copy of §12's list.
  ds_av2	rq 2
  ds_len	rq 1
  ds_ctx	rb sizeof.DrvCtx
  ds_arena	rb sizeof.Arena
  ds_iarena	rb sizeof.Arena
  ds_scratch	rb sizeof.Arena
  ds_interner	rb sizeof.Interner
  ds_envmap	rb sizeof.Map
  ds_srcs	rb DRV_SOURCES_MAX * sizeof.DrvSrc
  ds_vlx	rb sizeof.Lexer
  ds_vtoks	rb sizeof.Vec
  ds_vdiags	rb sizeof.Vec
  ds_lx		rb sizeof.Lexer
  ds_toks	rb sizeof.Vec
  ds_diags	rb sizeof.Vec
