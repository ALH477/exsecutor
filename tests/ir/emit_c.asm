; tests/ir/emit_c.asm
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
; `emit_c`: SSA IR text on stdin -> one C translation unit on stdout. The
; MIRROR of emit_ir.asm, deliberately line for line where it can be: same
; pipeline, same arenas, same verifier call per function, same exit codes,
; so that a difference between the two backends is never a difference
; between two harnesses.
;
;   1. read all of fd 0 into one buffer;
;   2. bfa_parse_module -- malformed text is fatal inside parse.inc:
;      "bfa: parse error at line N: ..." on fd 2, exit 3;
;   3. VERIFY every function with bfa_verify_func (ssa-ir.md section 3),
;      printing the verdict, exit 5. Same calls, same order, same verdicts
;      as emit_ir: an IR fixture is held to the verifier on BOTH paths, and
;      a fixture that the verifier rejects must be rejected identically;
;   4. bfc_emit_unit with `--hospes x86_64-linux`, which is the one row this
;      harness fixes -- c-backend.md D6 step 1. An emitter refusal is fatal
;      inside emit_c.inc: "bfc: emitter: ..." on fd 2, exit 4;
;   5. write the unit to fd 1, looping on a short write.
;
; WHAT IS NOT HERE, and why. There is no capability closure and no MXCSR
; image: library mode has neither (c-backend.md D1), so the two arguments
; `bfa_emit_program` needs have no counterpart. What replaces them is the
; `--hospes` row, and it is fixed at `x86_64-linux` for the same reason
; emit_ir fixes its closure -- an IR fixture has no checker to compute one
; from, and the differential harness compares against a reference binary
; built for exactly that host.
;
; EXIT STATUS, identical to emit_ir's so that `emit-exit=` reads the same on
; both: 0 the unit was written. 3 parse error. 4 emitter refusal. 5 verifier
; verdict. 6 stdin unreadable or larger than the input buffer. 7 arena init
; failed. 8 stdout write failed. 132 (SIGILL) an `rassert`.
;
; FREESTANDING like the compiler (CLAUDE.md): rt/sys.inc only, every syscall
; on the compiler's closed nine. tests/run.sh audits this binary with
; tools/syscall-audit.sh before trusting anything it prints -- and note that
; THIS PROGRAM NEVER RUNS A C COMPILER. exsc does not and cannot (spec 12:
; `execve` is on no allowlist); the shell script compiles what this prints,
; because it is verification tooling and not the compiler.
;
; DETERMINISM. Output is a function of stdin's bytes and this binary's bytes
; only: no argv, no env, no clock. Function iteration is Vec (declaration)
; order.
; -----------------------------------------------------------------------------

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/span.inc'
; backend_c/ hangs off the reference backend's chain rather than duplicating
; its tail (emit_c.inc's header, finding 14), so the consumer brings
; verify.inc -- which this harness needs anyway, for step 3.
include '../../compiler/x86_64/backend_fasmg/verify.inc'
include '../../compiler/x86_64/backend_c/program_c.inc'

ECC_IN_CAP   = 16777216		; 16 MiB of IR text is far past any fixture
ECC_IR_CAP   = 268435456	; the IR image AND the unit (the Vec doubles
				; inside it); mmap is lazy, so size is free
ECC_SCR_CAP  = 16777216
ECC_VER_CAP  = 16777216		; reset per function

segment readable executable
  start:
	lea	rdi, [ecc_in]
	mov	rsi, ECC_IN_CAP
	call	arena_init
	jc	.e_arena
	lea	rdi, [ecc_ir]
	mov	rsi, ECC_IR_CAP
	call	arena_init
	jc	.e_arena
	lea	rdi, [ecc_scr]
	mov	rsi, ECC_SCR_CAP
	call	arena_init
	jc	.e_arena
	lea	rdi, [ecc_ver]
	mov	rsi, ECC_VER_CAP
	call	arena_init
	jc	.e_arena

	; ---- 1. all of stdin -------------------------------------------------
  .readloop:
	mov	rsi, [ecc_in + Arena.base]
	add	rsi, [ecc_inlen]
	mov	rdx, ECC_IN_CAP
	sub	rdx, [ecc_inlen]
	jz	.e_input
	xor	edi, edi
	call	sys_read
	jc	.e_input
	test	rax, rax
	jz	.readdone
	add	[ecc_inlen], rax
	jmp	.readloop
  .readdone:

	; ---- 2. parse (fatal on error, inside parse.inc) ---------------------
	lea	rdi, [ecc_ir]
	lea	rsi, [ecc_scr]
	mov	rdx, [ecc_in + Arena.base]
	mov	rcx, [ecc_inlen]
	call	bfa_parse_module
	mov	[ecc_mod], rax

	; ---- 3. verify every function, verdict printed -----------------------
	mov	qword [ecc_fi], 0
  .verloop:
	mov	rax, [ecc_mod]
	mov	rdi, [rax + BfaModule.funcs]
	mov	rax, [ecc_fi]
	cmp	rax, [rdi + Vec.len]
	jae	.verdone
	mov	rsi, rax
	call	vec_get
	mov	rax, [rax]
	mov	[ecc_func], rax

	lea	rdi, [ecc_ver]
	call	arena_reset

	mov	rdi, [ecc_mod]
	mov	rsi, [ecc_func]
	lea	rdx, [ecc_ver]
	lea	rcx, [ecc_vblk]
	lea	r8, [ecc_vinst]
	call	bfa_verify_func
	test	eax, eax
	jnz	.e_verify
	inc	qword [ecc_fi]
	jmp	.verloop
  .verdone:

	; ---- 4. emit the whole translation unit ------------------------------
	mov	rdi, [ecc_mod]
	lea	rsi, [ecc_ir]
	mov	edx, BFC_HOSPES_X86_64
	call	bfc_emit_unit
	mov	[ecc_outp], rax
	mov	[ecc_outl], rdx

	; ---- 5. write it, all of it ------------------------------------------
  .writeloop:
	mov	rdx, [ecc_outl]
	test	rdx, rdx
	jz	.done
	mov	edi, 1
	mov	rsi, [ecc_outp]
	call	sys_write
	jc	.e_write
	add	[ecc_outp], rax
	sub	[ecc_outl], rax
	jmp	.writeloop
  .done:
	xor	edi, edi
	call	sys_exit_group

  ; ---- failures ----------------------------------------------------------
  .e_arena:
	lea	rdi, [ecc_m_arena]
	mov	rsi, ecc_m_arena.len
	mov	edx, 7
	jmp	.die
  .e_input:
	lea	rdi, [ecc_m_input]
	mov	rsi, ecc_m_input.len
	mov	edx, 6
	jmp	.die
  .e_write:
	lea	rdi, [ecc_m_write]
	mov	rsi, ecc_m_write.len
	mov	edx, 8
	jmp	.die

  .e_verify:
	; "emit_c: verify: rule R in @NAME (block id B, inst id I)\n", exit 5.
	; The ids are verify.inc's INTERNAL ones: 1-based, and text `bN` is
	; block id N+1 (parse.inc's header).
	mov	[ecc_rule], eax
	lea	rdi, [ecc_m_ver1]
	mov	rsi, ecc_m_ver1.len
	call	__bfa_write_stderr
	mov	edi, [ecc_rule]
	call	ecc_put_udec
	lea	rdi, [ecc_m_ver2]
	mov	rsi, ecc_m_ver2.len
	call	__bfa_write_stderr
	mov	rax, [ecc_mod]
	mov	rdi, [rax + BfaModule.names]
	mov	rax, [ecc_func]
	mov	esi, [rax + BfaFunc.name]
	call	intern_bytes
	mov	rdi, rax
	mov	rsi, rdx
	call	__bfa_write_stderr
	lea	rdi, [ecc_m_ver3]
	mov	rsi, ecc_m_ver3.len
	call	__bfa_write_stderr
	mov	edi, [ecc_vblk]
	call	ecc_put_udec
	lea	rdi, [ecc_m_ver4]
	mov	rsi, ecc_m_ver4.len
	call	__bfa_write_stderr
	mov	edi, [ecc_vinst]
	call	ecc_put_udec
	lea	rdi, [ecc_m_ver5]
	mov	rsi, ecc_m_ver5.len
	call	__bfa_write_stderr
	mov	edi, 5
	call	sys_exit_group

  ; .die: rdi = message, rsi = its length, edx = exit status.
  .die:
	mov	[ecc_rule], edx
	call	__bfa_write_stderr
	mov	edi, [ecc_rule]
	call	sys_exit_group

; ecc_put_udec(value) -- `value` (zero-extended 32 bits) in decimal on fd 2.
proc ecc_put_udec, value
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
  ecc_in:    rb sizeof.Arena
  ecc_ir:    rb sizeof.Arena
  ecc_scr:   rb sizeof.Arena
  ecc_ver:   rb sizeof.Arena
  ecc_inlen: dq 0
  ecc_mod:   dq 0
  ecc_func:  dq 0
  ecc_fi:    dq 0
  ecc_outp:  dq 0
  ecc_outl:  dq 0
  ecc_vblk:  dd 0
  ecc_vinst: dd 0
  ecc_rule:  dd 0

  ecc_m_arena: db "emit_c: arena_init failed", 10
  .len = $ - ecc_m_arena
  ecc_m_input: db "emit_c: stdin unreadable, or larger than the 16 MiB input buffer", 10
  .len = $ - ecc_m_input
  ecc_m_write: db "emit_c: write to stdout failed", 10
  .len = $ - ecc_m_write
  ecc_m_ver1: db "emit_c: verify: rule "
  .len = $ - ecc_m_ver1
  ecc_m_ver2: db " in @"
  .len = $ - ecc_m_ver2
  ecc_m_ver3: db " (block id "
  .len = $ - ecc_m_ver3
  ecc_m_ver4: db ", inst id "
  .len = $ - ecc_m_ver4
  ecc_m_ver5: db ")", 10
  .len = $ - ecc_m_ver5
