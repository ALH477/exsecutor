; tests/unit/driver_cli.asm
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
; The command line, §12 and §9.5, checked in process rather than through a
; shell. Every case here is a real `drv_parse` over a real argv array; nothing
; is stubbed and no message is intercepted, so the refusal paths write their
; prose to stderr exactly as they would in anger (tests/run.sh captures it and
; shows it only when a case fails).
;
; WHAT THIS PINS THAT A SHELL TEST COULD NOT: the contents of `DrvCtx` after a
; successful parse. An `exsc` that accepted `--hospes` and then dropped it on
; the floor would exit 0 on every command line a shell could hand it, and this
; is where that shows up.
;
; It also pins the ten subcommand names against §12's list. `drv_cmd_lookup` is
; the only place `exsc` decides what a subcommand is, and §12's sentence is the
; only place the list comes from.
;
; ONE THING THIS FIXTURE RECORDS FOR THE NEXT READER, because it cost a build:
; `drv_parse`'s value-fetch helper must be a LOCAL label (`.take_value`). Named
; without the dot it resets fasmg's local-label scope, and macros/proc.inc's
; `endp` then emits the epilogue as `__drv_take_value.__proc_epilogue` while
; every `return` earlier in the body still jumps to `drv_parse.__proc_epilogue`
; -- which fails with `symbol 'drv_parse.__proc_epilogue' is undefined or out
; of scope`, naming neither the helper nor the mistake.
;
; Exit 0 = every check passed. Otherwise the number identifies the check:
;   10  drv_cstrlen
;   11  drv_arg_is
;   12  drv_cmd_lookup over §12's ten names
;   13  drv_cmd_lookup on a name that is not one of them
;   14  drv_u64_parse
;   20  a command line that must be ACCEPTED was refused
;   21+N  accepted, but DrvCtx field N disagreed
;   27  the §12 multi-SOURCE case: two distinct SOURCEs were not accepted in
;       order, or the same two in the other order came out the same
;   30  a command line that must be REFUSED was accepted, or with the wrong
;       status -- the refusal table's row index is added
;   99  setup (arena_init) failed
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3, §9.5, §12.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

DT_ARENA = 1 shl 20

segment readable executable
  start:
	lea	r15, [dt_ctx]
	call	dt_setup

	; ---- 10: drv_cstrlen ------------------------------------------------
	lea	rdi, [dt_s_hospes]
	call	drv_cstrlen
	cmp	rax, 8			; "--hospes"
	jne	.f10
	lea	rdi, [dt_s_nul]
	call	drv_cstrlen
	test	rax, rax
	jnz	.f10

	; ---- 11: drv_arg_is -------------------------------------------------
	; Byte equality against a NUL-terminated literal, in both directions:
	; a prefix of the literal is not equal to it, and neither is an
	; extension of it.
	lea	rdi, [dt_s_hospes]
	mov	rsi, 8
	lea	rdx, [dt_s_hospes]
	call	drv_arg_is
	cmp	eax, 1
	jne	.f11
	lea	rdi, [dt_s_hospes]
	mov	rsi, 7			; "--hospe"
	lea	rdx, [dt_s_hospes]
	call	drv_arg_is
	test	eax, eax
	jnz	.f11
	lea	rdi, [dt_s_hospesx]	; "--hospesx"
	mov	rsi, 9
	lea	rdx, [dt_s_hospes]
	call	drv_arg_is
	test	eax, eax
	jnz	.f11

	; ---- 12/13: the §12 subcommand set ----------------------------------
	xor	rbx, rbx
  .cmdloop:
	cmp	rbx, DRV_CMD_COUNT
	jge	.cmddone
	mov	r12, [drv_cmd_tab + rbx*8]
	mov	rdi, r12
	call	drv_cstrlen
	mov	rdi, r12
	mov	rsi, rax
	call	drv_cmd_lookup
	cmp	eax, ebx
	jne	.f12
	inc	rbx
	jmp	.cmdloop
  .cmddone:
	lea	rdi, [dt_s_notacmd]
	mov	rsi, 10
	call	drv_cmd_lookup
	cmp	eax, DRV_CMD_NONE
	jne	.f13

	; ---- 14: drv_u64_parse ----------------------------------------------
	xor	rbx, rbx
  .numloop:
	cmp	rbx, DT_NUM_COUNT
	jge	.numdone
	mov	rcx, rbx
	shl	rcx, 2			; 4 qwords per row
	mov	rdi, [dt_num_tab + rcx*8]
	mov	rsi, [dt_num_tab + rcx*8 + 8]
	lea	rdx, [dt_num]
	mov	qword [dt_num], 0xDEAD
	call	drv_u64_parse
	mov	rcx, rbx
	shl	rcx, 2
	mov	r13, [dt_num_tab + rcx*8 + 16]
	mov	r12, [dt_num_tab + rcx*8 + 24]
	test	r13, r13
	jz	.num_reject
	cmp	eax, 1
	jne	.f14
	cmp	[dt_num], r12
	jne	.f14
	jmp	.numnext
  .num_reject:
	test	eax, eax
	jnz	.f14
	cmp	qword [dt_num], 0xDEAD	; a rejected parse writes nothing
	jne	.f14
  .numnext:
	inc	rbx
	jmp	.numloop
  .numdone:

	; ---- 20/21+: a command line that must be accepted -------------------
	; exsc aedifica --hospes x86_64-linux -o out.o --epoch 7
	;      --diagnostica json --env A=1 --env B=2 src.exsc
	call	dt_reset
	mov	rdi, DT_FULL_N
	lea	rsi, [dt_av_full]
	call	drv_parse
	test	eax, eax
	jnz	.f20

	mov	rdi, [r15 + DrvCtx.hospes]
	mov	rsi, [r15 + DrvCtx.hospesn]
	lea	rdx, [dt_s_triple]
	call	drv_arg_is
	test	eax, eax
	jz	.f21
	mov	rdi, [r15 + DrvCtx.srcpath]
	mov	rsi, [r15 + DrvCtx.srcpathn]
	lea	rdx, [dt_s_src]
	call	drv_arg_is
	test	eax, eax
	jz	.f22
	mov	rdi, [r15 + DrvCtx.outpath]
	mov	rsi, [r15 + DrvCtx.outpathn]
	lea	rdx, [dt_s_out]
	call	drv_arg_is
	test	eax, eax
	jz	.f23
	cmp	qword [r15 + DrvCtx.epoch], 7
	jne	.f24
	cmp	dword [r15 + DrvCtx.epochset], 1
	jne	.f24
	cmp	dword [r15 + DrvCtx.diagmode], DIAG_MODE_JSON
	jne	.f25
	cmp	dword [r15 + DrvCtx.nenv], 2
	jne	.f26

	; ---- 27: TWO SOURCEs, in order (§12) --------------------------------
	; *"`SOURCE` may be repeated. The files named form one compilation unit
	; ... Their order on the command line is the order of their items and is
	; part of the input."* So this must be ACCEPTED, both files must be
	; recorded, and the FIRST one must still be what `DrvCtx.srcpath` names
	; -- every message that speaks of "the source" uses that field, and a
	; one-file invocation must keep saying exactly what it said before.
	call	dt_reset
	mov	rdi, 4
	lea	rsi, [dt_av_twosrc]
	call	drv_parse
	test	eax, eax
	jnz	.f27
	cmp	dword [r15 + DrvCtx.nsrc], 2
	jne	.f27
	mov	rdi, [r15 + DrvCtx.srcpath]
	mov	rsi, [r15 + DrvCtx.srcpathn]
	lea	rdx, [dt_s_src]
	call	drv_arg_is
	test	eax, eax
	jz	.f27
	mov	rdi, 0
	call	drv_src_ptr
	mov	rdi, [rax + DrvSrc.spath]
	mov	rsi, [rax + DrvSrc.spathn]
	lea	rdx, [dt_s_src]
	call	drv_arg_is
	test	eax, eax
	jz	.f27
	mov	rdi, 1
	call	drv_src_ptr
	mov	rdi, [rax + DrvSrc.spath]
	mov	rsi, [rax + DrvSrc.spathn]
	lea	rdx, [dt_s_src2]
	call	drv_arg_is
	test	eax, eax
	jz	.f27

	; The same two files in the OTHER order is a DIFFERENT input, not the
	; same one spelled differently: entry 0 must now be the other file.
	; Nothing in this parser sorts, and this is what says so.
	call	dt_reset
	mov	rdi, 4
	lea	rsi, [dt_av_twosrc_rev]
	call	drv_parse
	test	eax, eax
	jnz	.f27
	mov	rdi, 0
	call	drv_src_ptr
	mov	rdi, [rax + DrvSrc.spath]
	mov	rsi, [rax + DrvSrc.spathn]
	lea	rdx, [dt_s_src2]
	call	drv_arg_is
	test	eax, eax
	jz	.f27

	; Put the accepted full command line back, so the checks below still
	; see the context they were written against.
	call	dt_reset
	mov	rdi, DT_FULL_N
	lea	rsi, [dt_av_full]
	call	drv_parse
	test	eax, eax
	jnz	.f27

	; ---- the minimum invocation §12 pins, and its defaults --------------
	; exsc aedifica --hospes x86_64-linux src.exsc
	call	dt_reset
	mov	rdi, DT_MIN_N
	lea	rsi, [dt_av_min]
	call	drv_parse
	test	eax, eax
	jnz	.f20
	cmp	qword [r15 + DrvCtx.outpath], 0
	jne	.f23
	cmp	qword [r15 + DrvCtx.epoch], 0		; §9.3: defaults to 0
	jne	.f24
	cmp	dword [r15 + DrvCtx.epochset], 0
	jne	.f24
	cmp	dword [r15 + DrvCtx.diagmode], DIAG_MODE_TEXT
	jne	.f25
	cmp	dword [r15 + DrvCtx.nenv], 0
	jne	.f26

	; ---- 30+: the refusals ----------------------------------------------
	; Each row is one argv that must be refused, with the status it must be
	; refused WITH -- an invocation error that came back as 0, or as some
	; other status, is the failure this table exists to catch.
	xor	rbx, rbx
  .badloop:
	cmp	rbx, DT_BAD_COUNT
	jge	.baddone
	call	dt_reset
	lea	rcx, [rbx + rbx*2]
	mov	rdi, [dt_bad_tab + rcx*8]	; argc
	mov	rsi, [dt_bad_tab + rcx*8 + 8]	; argv
	call	drv_parse
	lea	rcx, [rbx + rbx*2]
	mov	r12, [dt_bad_tab + rcx*8 + 16]
	cmp	eax, r12d
	jne	.f30
	inc	rbx
	jmp	.badloop
  .baddone:

	mov	eax, 231
	xor	edi, edi
	syscall

  .f10:	mov	edi, 10
	jmp	dt_die
  .f11:	mov	edi, 11
	jmp	dt_die
  .f12:	mov	edi, 12
	jmp	dt_die
  .f13:	mov	edi, 13
	jmp	dt_die
  .f14:	mov	edi, 14
	jmp	dt_die
  .f20:	mov	edi, 20
	jmp	dt_die
  .f21:	mov	edi, 21
	jmp	dt_die
  .f22:	mov	edi, 22
	jmp	dt_die
  .f23:	mov	edi, 23
	jmp	dt_die
  .f24:	mov	edi, 24
	jmp	dt_die
  .f25:	mov	edi, 25
	jmp	dt_die
  .f26:	mov	edi, 26
	jmp	dt_die
  .f27:	mov	edi, 27
	jmp	dt_die
  .f30:	lea	rdi, [rbx + 30]
	jmp	dt_die

; dt_die(edi = status) -- never returns.
  dt_die:
	mov	eax, 231
	syscall

; dt_setup -- the two arenas, the interner and the env map that
; `drv_aedifica` would otherwise build, plus the context binding
; compiler/x86_64/exsc.asm does at process entry.
  dt_setup:
	push	rbx
	lea	rax, [dt_arena]
	mov	[r15 + DrvCtx.arena], rax
	lea	rax, [dt_iarena]
	mov	[r15 + DrvCtx.iarena], rax
	lea	rax, [dt_scratch]
	mov	[r15 + DrvCtx.scratch], rax
	lea	rax, [dt_interner]
	mov	[r15 + DrvCtx.interner], rax
	lea	rax, [dt_envmap]
	mov	[r15 + DrvCtx.envmap], rax
	lea	rax, [dt_srcs]
	mov	[r15 + DrvCtx.srcs], rax	; the §12 SOURCE table (driver/cli.inc)
	lea	rax, [dt_vlx]
	mov	[r15 + DrvCtx.vlx], rax
	lea	rax, [dt_vtoks]
	mov	[r15 + DrvCtx.vtoks], rax
	lea	rax, [dt_vdiags]
	mov	[r15 + DrvCtx.vdiags], rax
	lea	rdi, [dt_iarena]
	mov	rsi, DT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [dt_scratch]
	mov	rsi, DT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [dt_interner]
	lea	rsi, [dt_iarena]
	mov	rdx, 64
	call	intern_init
	pop	rbx
	ret
  .boom:
	mov	edi, 99
	jmp	dt_die

; dt_reset -- clear everything `drv_parse` writes, and give the env map a
; fresh set of buckets, so one case cannot make the next one pass or fail.
  dt_reset:
	push	rbx
	call	drv_ctx_reset
	mov	rdi, [r15 + DrvCtx.envmap]
	mov	rsi, [r15 + DrvCtx.iarena]
	mov	rdx, 64
	call	map_init
	pop	rbx
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'
include '../../compiler/x86_64/driver/driver.inc'

segment readable
  dt_s_nul      db 0
  dt_s_hospes   db '--hospes',0
  dt_s_hospesx  db '--hospesx',0
  dt_s_notacmd  db 'aedificax',0
  dt_s_exsc     db 'exsc',0
  dt_s_aedifica db 'aedifica',0
  dt_s_triple   db 'x86_64-linux',0
  dt_s_src      db 'src.exsc',0
  dt_s_src2     db 'other.exsc',0
  dt_s_out      db 'out.o',0
  dt_s_dasho    db '-o',0
  dt_s_epoch    db '--epoch',0
  dt_s_seven    db '7',0
  dt_s_diagn    db '--diagnostica',0
  dt_s_json     db 'json',0
  dt_s_yaml     db 'yaml',0
  dt_s_env      db '--env',0
  dt_s_a1       db 'A=1',0
  dt_s_a2       db 'A=2',0
  dt_s_b2       db 'B=2',0
  dt_s_noeq     db 'noequals',0
  dt_s_badkey   db '1bad=x',0
  dt_s_frob     db '--frob',0
  dt_s_banana   db 'banana',0

  ; ---- drv_u64_parse: text, length, must-accept, expected value ---------
  ; FOUR columns, not three. The obvious three-column shape uses -1 in the
  ; value column to mean "must reject" -- and 2^64-1, the largest value that
  ; MUST be accepted, is bit-for-bit -1. Written that way first; this row is
  ; why it is not written that way now.
  dt_n_0        db '0'
  dt_n_007      db '007'
  dt_n_7        db '7'
  dt_n_max      db '18446744073709551615'
  dt_n_over     db '18446744073709551616'
  dt_n_x        db '1x'
  dt_n_neg      db '-1'
  dt_n_sp       db ' 1'
  dt_n_plus     db '+1'
  dt_n_over2    db '99999999999999999999'
  ; 2^62 * 10. Past 2^64-1, but the accumulator reaching it has bit 62 set
  ; with bits 63 and 61 clear -- the exact shape a `shl acc,3` carry check
  ; misses. driver/cli.inc's `drv_u64_parse` carries the full story; this
  ; row is the regression.
  dt_n_wrap     db '46116860184273879040'
dt_num_tab:
	dq dt_n_0,    1,  1, 0
	dq dt_n_007,  3,  1, 7
	dq dt_n_7,    1,  1, 7
	dq dt_n_max,  20, 1, 0xFFFFFFFFFFFFFFFF
	dq dt_n_over, 20, 0, 0		; one past 2^64-1 must not wrap
	dq dt_n_x,    2,  0, 0
	dq dt_n_neg,  2,  0, 0		; no sign: §9.3 says decimal seconds
	dq dt_n_sp,   2,  0, 0
	dq dt_n_plus, 2,  0, 0
	dq dt_n_over2,20, 0, 0
	dq dt_n_wrap, 20, 0, 0		; the shift-carry blind spot
	dq dt_n_0,    0,  0, 0		; empty is not a number
dt_num_tab_end:
DT_NUM_COUNT = (dt_num_tab_end - dt_num_tab) / 32
  ; A table that measured to zero rows would make the loop above pass by
  ; never running. fasmg's NATIVE assemble-time `assert`, not `rassert`.
  assert DT_NUM_COUNT = 12

  ; ---- the accepted command lines ---------------------------------------
dt_av_full:
	dq dt_s_exsc, dt_s_aedifica
	dq dt_s_hospes, dt_s_triple
	dq dt_s_dasho, dt_s_out
	dq dt_s_epoch, dt_s_seven
	dq dt_s_diagn, dt_s_json
	dq dt_s_env, dt_s_a1
	dq dt_s_env, dt_s_b2
	dq dt_s_src
dt_av_full_end:
DT_FULL_N = (dt_av_full_end - dt_av_full) / 8
  assert DT_FULL_N = 15

dt_av_min:
	dq dt_s_exsc, dt_s_aedifica, dt_s_hospes, dt_s_triple, dt_s_src
dt_av_min_end:
DT_MIN_N = (dt_av_min_end - dt_av_min) / 8
  assert DT_MIN_N = 5

  ; ---- the refused command lines ----------------------------------------
dt_av_dup_env:
	dq dt_s_exsc, dt_s_aedifica, dt_s_env, dt_s_a1, dt_s_env, dt_s_a2
dt_av_noeq:
	dq dt_s_exsc, dt_s_aedifica, dt_s_env, dt_s_noeq
dt_av_badkey:
	dq dt_s_exsc, dt_s_aedifica, dt_s_env, dt_s_badkey
dt_av_dup_hospes:
	dq dt_s_exsc, dt_s_aedifica, dt_s_hospes, dt_s_triple, dt_s_hospes, dt_s_triple
dt_av_dup_out:
	dq dt_s_exsc, dt_s_aedifica, dt_s_dasho, dt_s_out, dt_s_dasho, dt_s_out
dt_av_dup_epoch:
	dq dt_s_exsc, dt_s_aedifica, dt_s_epoch, dt_s_seven, dt_s_epoch, dt_s_seven
dt_av_dup_diagn:
	dq dt_s_exsc, dt_s_aedifica, dt_s_diagn, dt_s_json, dt_s_diagn, dt_s_json
dt_av_unknown:
	dq dt_s_exsc, dt_s_aedifica, dt_s_frob
; §12 admits N SOURCEs; the same one twice is still refused (driver/cli.inc).
dt_av_dupsrc:
	dq dt_s_exsc, dt_s_aedifica, dt_s_src, dt_s_src
dt_av_twosrc:
	dq dt_s_exsc, dt_s_aedifica, dt_s_src, dt_s_src2
dt_av_twosrc_rev:
	dq dt_s_exsc, dt_s_aedifica, dt_s_src2, dt_s_src
dt_av_noval:
	dq dt_s_exsc, dt_s_aedifica, dt_s_hospes
dt_av_badepoch:
	dq dt_s_exsc, dt_s_aedifica, dt_s_epoch, dt_s_banana
dt_av_baddiagn:
	dq dt_s_exsc, dt_s_aedifica, dt_s_diagn, dt_s_yaml

dt_bad_tab:
	dq 6, dt_av_dup_env,    DRV_EXIT_USAGE	; §9.3: a repeated KEY is an error
	dq 4, dt_av_noeq,       DRV_EXIT_USAGE
	dq 4, dt_av_badkey,     DRV_EXIT_USAGE	; §8.2: KEY must spell an identifier
	dq 6, dt_av_dup_hospes, DRV_EXIT_USAGE
	dq 6, dt_av_dup_out,    DRV_EXIT_USAGE
	dq 6, dt_av_dup_epoch,  DRV_EXIT_USAGE
	dq 6, dt_av_dup_diagn,  DRV_EXIT_USAGE
	dq 3, dt_av_unknown,    DRV_EXIT_USAGE
	dq 4, dt_av_dupsrc,     DRV_EXIT_USAGE	; §12 repeats SOURCE; it does not repeat A FILE
	dq 3, dt_av_noval,      DRV_EXIT_USAGE
	dq 4, dt_av_badepoch,   DRV_EXIT_USAGE
	dq 4, dt_av_baddiagn,   DRV_EXIT_USAGE
dt_bad_tab_end:
DT_BAD_COUNT = (dt_bad_tab_end - dt_bad_tab) / 24
  assert DT_BAD_COUNT = 12

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  dt_ctx	rb sizeof.DrvCtx
  dt_arena	rb sizeof.Arena
  dt_iarena	rb sizeof.Arena
  dt_scratch	rb sizeof.Arena
  dt_interner	rb sizeof.Interner
  dt_envmap	rb sizeof.Map
  dt_srcs	rb DRV_SOURCES_MAX * sizeof.DrvSrc
  dt_vlx	rb sizeof.Lexer
  dt_vtoks	rb sizeof.Vec
  dt_vdiags	rb sizeof.Vec
  dt_num	rq 1
