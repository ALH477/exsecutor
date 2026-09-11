; tests/ir/emit_ir.asm
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
; `emit_ir`: SSA IR text on stdin -> the whole fasmg program (`OUT`) on
; stdout. `tests/run.sh`'s `run_ir_tests` phase builds it once and feeds
; every `tests/ir/*.ir` fixture through it; the phase then assembles what
; this prints, RUNS it, and checks the exit status and output.
;
; WHY THIS EXISTS. A `tests/unit/*.asm` fixture cannot `execve`, so until this
; harness the emitter's output was compared as TEXT and only "run by hand"
; (tests/unit/bfa_emit_tier1.asm's and bfa_emit_program.asm's headers say so).
; This program does the one part a sandboxed fixture can do -- turn IR into
; `OUT` -- and leaves the assembling and running to the shell script, which
; may `execve` because it is verification tooling, not the compiler.
;
; THE PIPELINE, and the one step `exsc` does not have:
;   1. read all of fd 0 (read(0) -- allowlisted) into one buffer;
;   2. bfa_parse_module -- malformed text is fatal inside parse.inc itself:
;      "bfa: parse error at line N: ..." on fd 2, exit 3;
;   3. VERIFY every function with bfa_verify_func (ssa-ir.md section 3). The
;      driver never calls the verifier (driver/run.inc goes lwr_module ->
;      bfa_emit_program directly); this harness does, so every IR test is
;      also a verifier test. `bfa_verify_func` is called per function rather
;      than through `bfa_verify_module` because the latter `rassert`s on the
;      first bad verdict -- a bare SIGILL with no message -- and a harness
;      whose job is to say what is wrong should say it. Same calls, same
;      order (funcs Vec order, verify.inc's own), same verdicts; the only
;      difference is that the verdict is PRINTED: "emit_ir: verify: rule R
;      in @NAME (block id B, inst id I)", exit 5. `varena` is reset before
;      every function: verify.inc allocates its ctx and every scratch table
;      from it and reads nothing back across calls;
;   4. bfa_emit_program with the capability closure {Mundus, ambitus} and
;      MXCSR image 0x1F80 (`ad_parem`, `conservata`) -- exactly what the
;      hello world is emitted with (tests/unit/bfa_emit_program.asm) and what
;      tools/publish-gate.sh audits against. Emitter refusals are fatal
;      inside emit.inc: "bfa: emitter: ..." on fd 2, exit 4;
;   5. write `OUT` to fd 1 (write(1)), looping on a short write.
;
; WHY THE CLOSURE IS FIXED AT {Mundus, ambitus}. An IR fixture has no
; checker to compute `initium`'s closure from, and `tests/run.sh` audits
; every assembled binary with `--potestates Mundus,ambitus`. A wider closure
; here would put routines in the binary the audit then (correctly) rejects;
; a narrower one would make every fixture that writes to stdout fail to
; assemble. When a fixture needs `alloc`, this becomes a directive, not a
; guess -- `[OPEN]` until one does.
;
; EXIT STATUS: 0 `OUT` written. 3 parse error (parse.inc). 4 emitter refusal
; (emit.inc). 5 verifier verdict. 6 stdin unreadable or larger than the
; input buffer. 7 arena init failed. 8 stdout write failed. 132 (SIGILL) an
; `rassert` -- an internal contract violation, docs/asm-conventions.md 1.3.
;
; FREESTANDING like the compiler (CLAUDE.md): rt/sys.inc only, and every
; syscall this binary contains is on the compiler's closed nine --
; `tests/run.sh` audits it with tools/syscall-audit.sh (no --potestates: the
; compiler's own allowlist) before trusting anything it prints.
;
; DETERMINISM. Output is a function of stdin's bytes and this binary's bytes
; only: no argv, no env, no clock. Function iteration is Vec (declaration)
; order.
; -----------------------------------------------------------------------------

include 'format/format.inc'

format ELF64 executable 3
entry start

; backend_fasmg/ir.inc does not include rt/ (the consumer brings it), so this
; program brings the chain itself, exactly as tests/unit/bfa_emit_program.asm
; does (span.inc includes intern.inc, which includes the rest).
include '../../compiler/x86_64/rt/span.inc'
include '../../compiler/x86_64/backend_fasmg/program.inc'

EIR_IN_CAP   = 16777216		; 16 MiB of IR text is far past any fixture
EIR_IR_CAP   = 268435456	; the IR image AND `OUT` (emit.inc's Vec doubles
				; inside it); mmap is lazy, so size is free
EIR_SCR_CAP  = 16777216
EIR_VER_CAP  = 16777216		; reset per function

segment readable executable
  start:
	lea	rdi, [eir_in]
	mov	rsi, EIR_IN_CAP
	call	arena_init
	jc	.e_arena
	lea	rdi, [eir_ir]
	mov	rsi, EIR_IR_CAP
	call	arena_init
	jc	.e_arena
	lea	rdi, [eir_scr]
	mov	rsi, EIR_SCR_CAP
	call	arena_init
	jc	.e_arena
	lea	rdi, [eir_ver]
	mov	rsi, EIR_VER_CAP
	call	arena_init
	jc	.e_arena

	; ---- 1. all of stdin -------------------------------------------------
	; `eir_inlen` counts bytes in; the buffer is the input arena's region,
	; used directly (nothing else allocates from it). A read that fills the
	; buffer exactly is refused rather than guessed about: one more byte may
	; have been waiting.
  .readloop:
	mov	rsi, [eir_in + Arena.base]
	add	rsi, [eir_inlen]
	mov	rdx, EIR_IN_CAP
	sub	rdx, [eir_inlen]
	jz	.e_input
	xor	edi, edi
	call	sys_read
	jc	.e_input
	test	rax, rax
	jz	.readdone
	add	[eir_inlen], rax
	jmp	.readloop
  .readdone:

	; ---- 2. parse (fatal on error, inside parse.inc) ---------------------
	lea	rdi, [eir_ir]
	lea	rsi, [eir_scr]
	mov	rdx, [eir_in + Arena.base]
	mov	rcx, [eir_inlen]
	call	bfa_parse_module
	mov	[eir_mod], rax

	; ---- 3. verify every function, verdict printed -----------------------
	mov	qword [eir_fi], 0
  .verloop:
	mov	rax, [eir_mod]
	mov	rdi, [rax + BfaModule.funcs]
	mov	rax, [eir_fi]
	cmp	rax, [rdi + Vec.len]
	jae	.verdone
	mov	rsi, rax
	call	vec_get
	mov	rax, [rax]
	mov	[eir_func], rax

	lea	rdi, [eir_ver]
	call	arena_reset

	mov	rdi, [eir_mod]
	mov	rsi, [eir_func]
	lea	rdx, [eir_ver]
	lea	rcx, [eir_vblk]
	lea	r8, [eir_vinst]
	call	bfa_verify_func
	test	eax, eax
	jnz	.e_verify
	inc	qword [eir_fi]
	jmp	.verloop
  .verdone:

	; ---- 4. emit the whole program ---------------------------------------
	mov	rdi, [eir_mod]
	lea	rsi, [eir_ir]
	mov	rdx, BFA_POTESTAS_MUNDUS or BFA_POTESTAS_AMBITUS
	mov	rcx, BFA_MXCSR_AD_PAREM
	call	bfa_emit_program
	mov	[eir_outp], rax
	mov	[eir_outl], rdx

	; ---- 5. write it, all of it ------------------------------------------
  .writeloop:
	mov	rdx, [eir_outl]
	test	rdx, rdx
	jz	.done
	mov	edi, 1
	mov	rsi, [eir_outp]
	call	sys_write
	jc	.e_write
	add	[eir_outp], rax
	sub	[eir_outl], rax
	jmp	.writeloop
  .done:
	xor	edi, edi
	call	sys_exit_group

  ; ---- failures ----------------------------------------------------------
  .e_arena:
	lea	rdi, [eir_m_arena]
	mov	rsi, eir_m_arena.len
	mov	edx, 7
	jmp	.die
  .e_input:
	lea	rdi, [eir_m_input]
	mov	rsi, eir_m_input.len
	mov	edx, 6
	jmp	.die
  .e_write:
	lea	rdi, [eir_m_write]
	mov	rsi, eir_m_write.len
	mov	edx, 8
	jmp	.die

  .e_verify:
	; "emit_ir: verify: rule R in @NAME (block id B, inst id I)\n", exit 5.
	; The ids are verify.inc's INTERNAL ones: 1-based, and text `bN` is
	; block id N+1 (parse.inc's header).
	mov	[eir_rule], eax
	lea	rdi, [eir_m_ver1]
	mov	rsi, eir_m_ver1.len
	call	__bfa_write_stderr
	mov	edi, [eir_rule]
	call	eir_put_udec
	lea	rdi, [eir_m_ver2]
	mov	rsi, eir_m_ver2.len
	call	__bfa_write_stderr
	mov	rax, [eir_mod]
	mov	rdi, [rax + BfaModule.names]
	mov	rax, [eir_func]
	mov	esi, [rax + BfaFunc.name]
	call	intern_bytes
	mov	rdi, rax
	mov	rsi, rdx
	call	__bfa_write_stderr
	lea	rdi, [eir_m_ver3]
	mov	rsi, eir_m_ver3.len
	call	__bfa_write_stderr
	mov	edi, [eir_vblk]
	call	eir_put_udec
	lea	rdi, [eir_m_ver4]
	mov	rsi, eir_m_ver4.len
	call	__bfa_write_stderr
	mov	edi, [eir_vinst]
	call	eir_put_udec
	lea	rdi, [eir_m_ver5]
	mov	rsi, eir_m_ver5.len
	call	__bfa_write_stderr
	mov	edi, 5
	call	sys_exit_group

  ; .die: rdi = message, rsi = its length, edx = exit status.
  .die:
	mov	[eir_rule], edx
	call	__bfa_write_stderr
	mov	edi, [eir_rule]
	call	sys_exit_group

; eir_put_udec(value) -- `value` (zero-extended 32 bits) in decimal on fd 2.
proc eir_put_udec, value
	locals
		slot buf, 24
	endl
	mov	eax, dword [value]
	mov	rdi, rax
	lea	rsi, [buf + 24]
	call	__bfa_fmt_udec
	mov	rdi, rax
	mov	rsi, rdx
	call	__bfa_write_stderr
	return
endp

segment readable writeable
  eir_in:    rb sizeof.Arena
  eir_ir:    rb sizeof.Arena
  eir_scr:   rb sizeof.Arena
  eir_ver:   rb sizeof.Arena
  eir_inlen: dq 0
  eir_mod:   dq 0
  eir_func:  dq 0
  eir_fi:    dq 0
  eir_outp:  dq 0
  eir_outl:  dq 0
  eir_vblk:  dd 0
  eir_vinst: dd 0
  eir_rule:  dd 0

  eir_m_arena: db "emit_ir: arena_init failed", 10
  .len = $ - eir_m_arena
  eir_m_input: db "emit_ir: stdin unreadable, or larger than the 16 MiB input buffer", 10
  .len = $ - eir_m_input
  eir_m_write: db "emit_ir: write to stdout failed", 10
  .len = $ - eir_m_write
  eir_m_ver1: db "emit_ir: verify: rule "
  .len = $ - eir_m_ver1
  eir_m_ver2: db " in @"
  .len = $ - eir_m_ver2
  eir_m_ver3: db " (block id "
  .len = $ - eir_m_ver3
  eir_m_ver4: db ", inst id "
  .len = $ - eir_m_ver4
  eir_m_ver5: db ")", 10
  .len = $ - eir_m_ver5
