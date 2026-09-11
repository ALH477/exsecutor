; tests/unit/bfa_emit_program.asm
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
; `bfa_emit_program`: the whole of `OUT` for the hello world, compared to an
; exact expected text. docs/design/runtime.md section 5's three IR functions,
; hand-written (no lowering exists), through program.inc, with the capability
; closure {Mundus, ambitus} and the `ad_parem`/`conservata` MXCSR image
; 0x1F80.
;
; THE EXPECTED TEXT IS NOT ALL TYPED OUT. The two prelude blobs are ~31 KB
; and `OUT` carries them byte for byte, so the expected buffer below pulls
; them in with `file` from `compiler/x86_64/prelude/` -- the same two files
; program.inc itself carries. That is not a weakened comparison: it is the
; only comparison that can FAIL if program.inc ever transforms, truncates or
; re-indents a blob, which typing 31 KB of text into this fixture could not
; do any better. The lines around them -- banner, include, format, entry, the
; twelve constants, all three `segment` directives, the emitted module, the
; literal as hex `db` -- are typed out in full and compared byte for byte.
;
; WHAT THIS FIXTURE CANNOT DO, and what was done instead. It cannot assemble
; the `OUT` it just produced and run the result: `tests/run.sh` has no
; `execve` and this project's syscall allowlist has none either, by design
; (CLAUDE.md). So this binary has a SECOND MODE: run it with any argument and
; it writes `OUT` to stdout and exits 0, which is how the round trip
;
;     tests/unit/bfa_emit_program dump > OUT
;     INCLUDE=vendor/fasmg-x86 fasmg OUT BIN && ./BIN | cmp - examples/saluta.expected
;     tools/syscall-audit.sh --potestates Mundus,ambitus BIN
;
; was performed by hand. That run is in the agent's report, verbatim. With no
; argument -- how tests/run.sh invokes it -- it compares and exits 0 or 12.
;
; `data $0` / `gaddr 0`, and the three `externus` declarations with
; placeholder bodies, are explained in bfa_emit_tier2.asm's header; the same
; IR text is used here.
;
; Exit 0 = `OUT` matches exactly. 11 = arena init. 12 = mismatch.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

; backend_fasmg/ir.inc no longer includes rt/ (the consumer brings it, as
; every other module's does), so this fixture brings the chain itself (span.inc includes intern.inc).
include '../../compiler/x86_64/rt/span.inc'
include '../../compiler/x86_64/backend_fasmg/program.inc'

segment readable executable
  start:
	mov	rbx, [rsp]			; argc, before anything moves rsp

	lea	rdi, [pgar]
	mov	rsi, 33554432
	call	arena_init
	jc	.fail1
	lea	rdi, [pgscr]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail1

	lea	rdi, [pgar]
	lea	rsi, [pgscr]
	lea	rdx, [pgsrc]
	mov	rcx, pgsrc.len
	call	bfa_parse_module
	mov	[pgmod], rax

	mov	rdi, [pgmod]
	lea	rsi, [pgar]
	mov	rdx, BFA_POTESTAS_MUNDUS or BFA_POTESTAS_AMBITUS
	mov	rcx, BFA_MXCSR_AD_PAREM
	call	bfa_emit_program
	mov	[pgoutp], rax
	mov	[pgoutl], rdx

	cmp	rbx, 1
	jg	.dump

	mov	rdi, [pgoutp]
	mov	rsi, [pgoutl]
	lea	rdx, [pgexp]
	mov	rcx, pgexp.len
	call	__bfa_streq
	test	eax, eax
	jz	.fail2

	mov	eax, 231
	xor	edi, edi
	syscall

  .dump:
	mov	edi, 1
	mov	rsi, [pgoutp]
	mov	rdx, [pgoutl]
	call	sys_write
	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1: mov eax,231
	mov edi,11
	syscall
  .fail2:
	mov	edi, 2
	mov	rsi, [pgoutp]
	mov	rdx, [pgoutl]
	call	sys_write
	mov	eax,231
	mov	edi,12
	syscall

segment readable writeable
  pgar:   rb sizeof.Arena
  pgscr:  rb sizeof.Arena
  pgmod:  dq 0
  pgoutp: dq 0
  pgoutl: dq 0
  pgsrc:
  db "data $0 101 1 4176652c206d756e6475732e0a0a45782073696c656e74696f2073757267697420666f726d612e0a4578207369676e6f206e6173636974757220766f782e0a457820636f6469636520666974206c756d656e2e0a0a486f64696520696e636970696d75732e", 10
  db "", 10
  db "functio @exsrt_mundus_ambitus (ptr) -> ptr numeri ad_parem vetita explicita conservata externus sysv_amd64 {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "ret %0", 10
  db "}", 10
  db "", 10
  db "functio @exsrt_scriptor_ad_exitum (ptr ptr) -> void numeri ad_parem vetita explicita conservata externus sysv_amd64 {", 10
  db "b0:", 10
  db "ret", 10
  db "}", 10
  db "", 10
  db "functio @exsrt_scriptor_scribe (ptr ptr) -> u64 numeri ad_parem vetita explicita conservata externus sysv_amd64 {", 10
  db "b0:", 10
  db "%0 = iconst u64 0", 10
  db "ret %0", 10
  db "}", 10
  db "", 10
  db "functio @saluta (ptr) -> void numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = gaddr 0", 10
  db "store ptr %0 0 nativus %1", 10
  db "%2 = iconst u64 101", 10
  db "store u64 %0 8 nativus %2", 10
  db "ret", 10
  db "}", 10
  db "", 10
  db "functio @imprime_gutenbergio (ptr ptr) -> u64 numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = param ptr 1", 10
  db "%2 = call u64 @exsrt_scriptor_scribe %0 %1", 10
  db "ret %2", 10
  db "}", 10
  db "", 10
  db "functio @initium (ptr) -> u8 numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = call ptr @exsrt_mundus_ambitus %0", 10
  db "%2 = slot 16 8", 10
  db "call void @exsrt_scriptor_ad_exitum %2 %1", 10
  db "%3 = slot 16 8", 10
  db "call void @saluta %3", 10
  db "%4 = call u64 @imprime_gutenbergio %2 %3", 10
  db "%5 = iconst u8 0", 10
  db "ret %5", 10
  db "}", 10
  .len = $ - pgsrc

  ; ---- the expected OUT ------------------------------------------
  pgexp:
  db "; exsecutor: reference backend", 10
  db "include 'format/format.inc'", 10
  db "format ELF64 executable 3", 10
  db "entry exsrt_start", 10
  db "EXS_POTESTAS_MUNDUS     = 1", 10
  db "EXS_POTESTAS_ALLOC      = 0", 10
  db "EXS_POTESTAS_SERMO      = 0", 10
  db "EXS_POTESTAS_HOROLOGIUM = 0", 10
  db "EXS_POTESTAS_ARCHIVUM   = 0", 10
  db "EXS_POTESTAS_RETE       = 0", 10
  db "EXS_POTESTAS_FORTUNA    = 0", 10
  db "EXS_POTESTAS_AMBITUS    = 1", 10
  db "EXS_POTESTAS_FILUM      = 0", 10
  db "EXS_POTESTAS_MACHINA    = 0", 10
  db "EXS_POTESTAS_CRUDUM     = 0", 10
  db "EXS_MXCSR               = 0x1F80", 10
  db "segment readable executable", 10
  file '../../compiler/x86_64/prelude/prelude.asm'
  db "bfausr_saluta:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 64", 10
  db "bfausr_saluta_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	lea	rax, [bfausr_g0]", 10
  db "	mov	qword [rbp-16], rax", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-16]", 10
  db "	mov	qword [rax+0], rcx", 10
  db "	mov	qword [rbp-32], 101", 10
  db "	mov	rax, [rbp-8]", 10
  db "	mov	rcx, [rbp-32]", 10
  db "	mov	qword [rax+8], rcx", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_imprime_gutenbergio:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 48", 10
  db "bfausr_imprime_gutenbergio_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	qword [rbp-16], rsi", 10
  db "	mov	rdi, [rbp-8]", 10
  db "	mov	rsi, [rbp-16]", 10
  db "	call	bfausr_exsrt_scriptor_scribe", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rax, [rbp-24]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_initium:", 10
  db "	push	rbp", 10
  db "	mov	rbp, rsp", 10
  db "	sub	rsp, 112", 10
  db "bfausr_initium_b1:", 10
  db "	mov	qword [rbp-8], rdi", 10
  db "	mov	rdi, [rbp-8]", 10
  db "	call	bfausr_exsrt_mundus_ambitus", 10
  db "	mov	qword [rbp-16], rax", 10
  db "	lea	rax, [rbp-96]", 10
  db "	mov	qword [rbp-24], rax", 10
  db "	mov	rdi, [rbp-24]", 10
  db "	mov	rsi, [rbp-16]", 10
  db "	call	bfausr_exsrt_scriptor_ad_exitum", 10
  db "	lea	rax, [rbp-112]", 10
  db "	mov	qword [rbp-40], rax", 10
  db "	mov	rdi, [rbp-40]", 10
  db "	call	bfausr_saluta", 10
  db "	mov	rdi, [rbp-24]", 10
  db "	mov	rsi, [rbp-40]", 10
  db "	call	bfausr_imprime_gutenbergio", 10
  db "	mov	qword [rbp-56], rax", 10
  db "	mov	qword [rbp-64], 0", 10
  db "	mov	rax, [rbp-64]", 10
  db "	mov	rsp, rbp", 10
  db "	pop	rbp", 10
  db "	ret", 10
  db "bfausr_trap:", 10
  db "	mov	edi, 1", 10
  db "	jmp	exsrt_abort", 10
  db "segment readable", 10
  db "bfausr_g0:", 10
  db "	db	0x41,0x76,0x65,0x2C,0x20,0x6D,0x75,0x6E,0x64,0x75,0x73,0x2E", 10
  db "	db	0x0A,0x0A,0x45,0x78,0x20,0x73,0x69,0x6C,0x65,0x6E,0x74,0x69", 10
  db "	db	0x6F,0x20,0x73,0x75,0x72,0x67,0x69,0x74,0x20,0x66,0x6F,0x72", 10
  db "	db	0x6D,0x61,0x2E,0x0A,0x45,0x78,0x20,0x73,0x69,0x67,0x6E,0x6F", 10
  db "	db	0x20,0x6E,0x61,0x73,0x63,0x69,0x74,0x75,0x72,0x20,0x76,0x6F", 10
  db "	db	0x78,0x2E,0x0A,0x45,0x78,0x20,0x63,0x6F,0x64,0x69,0x63,0x65", 10
  db "	db	0x20,0x66,0x69,0x74,0x20,0x6C,0x75,0x6D,0x65,0x6E,0x2E,0x0A", 10
  db "	db	0x0A,0x48,0x6F,0x64,0x69,0x65,0x20,0x69,0x6E,0x63,0x69,0x70", 10
  db "	db	0x69,0x6D,0x75,0x73,0x2E", 10
  db "segment readable writeable", 10
  file '../../compiler/x86_64/prelude/prelude_data.asm'
  .len = $ - pgexp
