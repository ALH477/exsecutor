; tests/unit/driver_env_epoch.asm
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
; §9.3's `--env` and `--epoch` subsection, clause by clause. That subsection
; opens by saying both flags *"were named once each and never specified"* and
; that *"an implementer guessing is how a purity contract acquires an
; undocumented exception"* -- so every rule it does state is pinned here, and
; the pins are the sentences, not paraphrases:
;
;   "KEY matches an identifier (§8.2)"        -> checks 40, 41
;   "VALUE is the rest of the argument
;    verbatim, including `=` and spaces"      -> check 42
;   "A repeated KEY is an error, not a
;    last-wins"                               -> check 43
;   "Order is therefore not observable"       -> check 44
;   "--epoch N ... Optional, defaulting to 0" -> check 45
;
; CHECK 41 IS THE ONE THAT MATTERS MOST. `drv_ident_ok` runs the real lexer
; over the key rather than re-deriving UAX #31, so a key carrying a bidi
; override, a non-NFC sequence or two scripts is refused by the same code that
; refuses it in source. A driver with its own hand-rolled `isalnum` would pass
; every ASCII row of that table and fail every other one, which is exactly
; what the non-ASCII rows are for.
;
; Exit 0 = every check passed. Otherwise:
;   40  drv_ident_ok disagreed on an ASCII key
;   41  drv_ident_ok disagreed on a non-ASCII key
;   42  a stored VALUE was not the rest of the argument verbatim
;   43  a repeated KEY was not refused, or a fresh one after it was
;   44  the two --env orders did not agree
;   45  --epoch's default or its parse disagreed
;   99  setup failed
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1, §8.2, §9.3.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

DE_ARENA = 1 shl 20

segment readable executable
  start:
	lea	r15, [de_ctx]
	call	de_setup

	; ---- 40/41: drv_ident_ok --------------------------------------------
	xor	rbx, rbx
  .idloop:
	cmp	rbx, DE_ID_COUNT
	jge	.iddone
	mov	rcx, rbx
	shl	rcx, 2			; 4 qwords per row
	mov	rdi, [de_id_tab + rcx*8]
	mov	rsi, [de_id_tab + rcx*8 + 8]
	call	drv_ident_ok
	mov	rcx, rbx
	shl	rcx, 2
	mov	r12, [de_id_tab + rcx*8 + 16]
	cmp	eax, r12d
	jne	.id_bad
	inc	rbx
	jmp	.idloop
  .id_bad:
	mov	rcx, rbx
	shl	rcx, 2
	mov	rdi, [de_id_tab + rcx*8 + 24]	; 40 for ASCII, 41 for the rest
	jmp	de_die
  .iddone:

	; ---- 42: VALUE is the rest of the argument, verbatim ----------------
	; `K=a=b c` is KEY `K` and VALUE `a=b c` -- seven bytes containing both
	; an `=` and a space. Split on the FIRST `=`, and never re-scanned.
	call	de_reset
	mov	rdi, DE_VERB_N
	lea	rsi, [de_av_verbatim]
	call	drv_parse
	test	eax, eax
	jnz	.f42
	cmp	dword [r15 + DrvCtx.nenv], 1
	jne	.f42
	mov	rdi, [r15 + DrvCtx.envmap]
	lea	rsi, [de_s_k]
	mov	rdx, 1
	call	map_get
	test	rax, rax
	jz	.f42
	mov	rdi, [rax]		; VALUE pointer
	mov	rsi, [rax + 8]		; VALUE length
	lea	rdx, [de_s_value]
	call	drv_arg_is
	test	eax, eax
	jz	.f42

	; ---- 43: a repeated KEY is an error, and does not poison the map ----
	call	de_reset
	mov	rdi, DE_DUP_N
	lea	rsi, [de_av_dup]
	call	drv_parse
	cmp	eax, DRV_EXIT_USAGE
	jne	.f43
	; A refused key must leave nothing behind: the very next parse, with
	; the same KEY, must succeed. `drv_ident_ok` runs its lexer over a
	; throwaway diagnostic vector for exactly this reason.
	call	de_reset
	mov	rdi, DE_ONE_N
	lea	rsi, [de_av_one]
	call	drv_parse
	test	eax, eax
	jnz	.f43
	cmp	dword [r15 + DrvCtx.nenv], 1
	jne	.f43

	; ---- 44: --env order is not observable ------------------------------
	; Two orders of the same three bindings. Both accepted, both with the
	; same count, and each KEY resolving to the same VALUE bytes either
	; way -- so nothing downstream can tell the two command lines apart.
	call	de_reset
	mov	rdi, DE_ORD_N
	lea	rsi, [de_av_ord1]
	call	drv_parse
	test	eax, eax
	jnz	.f44
	cmp	dword [r15 + DrvCtx.nenv], 3
	jne	.f44
	call	de_env_digest
	mov	r13, rax

	call	de_reset
	mov	rdi, DE_ORD_N
	lea	rsi, [de_av_ord2]
	call	drv_parse
	test	eax, eax
	jnz	.f44
	cmp	dword [r15 + DrvCtx.nenv], 3
	jne	.f44
	call	de_env_digest
	cmp	rax, r13
	jne	.f44

	; ---- 45: --epoch defaults to 0, and parses when given ---------------
	call	de_reset
	mov	rdi, DE_ONE_N
	lea	rsi, [de_av_one]
	call	drv_parse
	test	eax, eax
	jnz	.f45
	cmp	qword [r15 + DrvCtx.epoch], 0
	jne	.f45
	cmp	dword [r15 + DrvCtx.epochset], 0
	jne	.f45
	call	de_reset
	mov	rdi, DE_EPO_N
	lea	rsi, [de_av_epoch]
	call	drv_parse
	test	eax, eax
	jnz	.f45
	cmp	qword [r15 + DrvCtx.epoch], 1700000000
	jne	.f45
	cmp	dword [r15 + DrvCtx.epochset], 1
	jne	.f45

	mov	eax, 231
	xor	edi, edi
	syscall

  .f42:	mov	edi, 42
	jmp	de_die
  .f43:	mov	edi, 43
	jmp	de_die
  .f44:	mov	edi, 44
	jmp	de_die
  .f45:	mov	edi, 45
	jmp	de_die

  de_die:
	mov	eax, 231
	syscall

; de_env_digest -> rax = an order-INDEPENDENT summary of the three bindings:
; for each of A, B and C, the KEY's byte plus its VALUE's first byte and
; length, summed. Two command lines that differ only in --env order must
; produce the same number; one that lost or swapped a binding must not.
  de_env_digest:
	push	rbx
	push	r12
	push	r13
	xor	r13, r13
	xor	rbx, rbx
  .loop:
	cmp	rbx, 3
	jge	.done
	mov	rdi, [r15 + DrvCtx.envmap]
	lea	rsi, [de_keys + rbx]
	mov	rdx, 1
	call	map_get
	test	rax, rax
	jz	.miss
	mov	r12, rax
	movzx	ecx, byte [de_keys + rbx]
	add	r13, rcx
	mov	rcx, [r12]		; VALUE pointer
	movzx	edx, byte [rcx]
	add	r13, rdx
	add	r13, [r12 + 8]		; VALUE length
	inc	rbx
	jmp	.loop
  .miss:
	mov	r13, -1
  .done:
	mov	rax, r13
	pop	r13
	pop	r12
	pop	rbx
	ret

  de_setup:
	push	rbx
	lea	rax, [de_arena]
	mov	[r15 + DrvCtx.arena], rax
	lea	rax, [de_iarena]
	mov	[r15 + DrvCtx.iarena], rax
	lea	rax, [de_scratch]
	mov	[r15 + DrvCtx.scratch], rax
	lea	rax, [de_interner]
	mov	[r15 + DrvCtx.interner], rax
	lea	rax, [de_envmap]
	mov	[r15 + DrvCtx.envmap], rax
	lea	rax, [de_srcs]
	mov	[r15 + DrvCtx.srcs], rax	; the §12 SOURCE table (driver/cli.inc)
	lea	rax, [de_vlx]
	mov	[r15 + DrvCtx.vlx], rax
	lea	rax, [de_vtoks]
	mov	[r15 + DrvCtx.vtoks], rax
	lea	rax, [de_vdiags]
	mov	[r15 + DrvCtx.vdiags], rax
	lea	rdi, [de_iarena]
	mov	rsi, DE_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [de_scratch]
	mov	rsi, DE_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [de_interner]
	lea	rsi, [de_iarena]
	mov	rdx, 64
	call	intern_init
	lea	rdi, [de_envmap]
	lea	rsi, [de_iarena]
	mov	rdx, 64
	call	map_init
	pop	rbx
	ret
  .boom:
	mov	edi, 99
	jmp	de_die

  de_reset:
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
  de_keys       db 'ABC'
  de_s_k        db 'K',0
  de_s_value    db 'a=b c',0

  ; ---- drv_ident_ok: text, length, expected verdict, failure code -------
  de_i_a        db 'A'
  de_i_snake    db 'a_b9'
  de_i_under    db '_x'
  de_i_kw       db 'forma'
  de_i_digit    db '1bad'
  de_i_space    db 'a b'
  de_i_eq       db 'a=b'
  de_i_dash     db 'a-b'
  de_i_greek    db 0xCE, 0xB1				; alpha, single script
  de_i_mixed    db 'a', 0xD0, 0xB0			; Latin a + Cyrillic a
  de_i_nonfc    db 'e', 0xCC, 0x81			; e + combining acute
  de_i_bidi     db 'a', 0xE2, 0x80, 0xAE, 'b'		; a + U+202E + b
de_id_tab:
	dq de_i_a,     1, 1, 40
	dq de_i_snake, 4, 1, 40
	dq de_i_under, 2, 1, 40
	dq de_i_kw,    5, 1, 40		; a reserved word still SPELLS an identifier
	dq de_i_digit, 4, 0, 40
	dq de_i_space, 3, 0, 40		; two tokens is not one identifier
	dq de_i_eq,    3, 0, 40
	dq de_i_dash,  3, 0, 40
	dq de_i_a,     0, 0, 40		; empty
	dq de_i_greek, 2, 1, 41		; §8.2 allows non-ASCII identifiers
	dq de_i_mixed, 3, 0, 41		; §8.2 / UTS #39: EXS-E0104
	dq de_i_nonfc, 3, 0, 41		; §8.1: EXS-E0102
	dq de_i_bidi,  5, 0, 41		; §8.1: EXS-E0103
de_id_tab_end:
DE_ID_COUNT = (de_id_tab_end - de_id_tab) / 32
  ; A table that measured to zero rows would make the loop pass by never
  ; running. fasmg's NATIVE assemble-time `assert`, not `rassert`.
  assert DE_ID_COUNT = 13

  de_s_exsc     db 'exsc',0
  de_s_aedifica db 'aedifica',0
  de_s_env      db '--env',0
  de_s_epoch    db '--epoch',0
  de_s_1700     db '1700000000',0
  de_s_kv       db 'K=a=b c',0
  de_s_a1       db 'A=1',0
  de_s_a9       db 'A=9',0
  de_s_b22      db 'B=22',0
  de_s_c333     db 'C=333',0

de_av_verbatim:
	dq de_s_exsc, de_s_aedifica, de_s_env, de_s_kv
de_av_verbatim_end:
DE_VERB_N = (de_av_verbatim_end - de_av_verbatim) / 8
  assert DE_VERB_N = 4

de_av_dup:
	dq de_s_exsc, de_s_aedifica, de_s_env, de_s_a1, de_s_env, de_s_a9
de_av_dup_end:
DE_DUP_N = (de_av_dup_end - de_av_dup) / 8
  assert DE_DUP_N = 6

de_av_one:
	dq de_s_exsc, de_s_aedifica, de_s_env, de_s_a1
de_av_one_end:
DE_ONE_N = (de_av_one_end - de_av_one) / 8
  assert DE_ONE_N = 4

de_av_ord1:
	dq de_s_exsc, de_s_aedifica
	dq de_s_env, de_s_a1, de_s_env, de_s_b22, de_s_env, de_s_c333
de_av_ord1_end:
DE_ORD_N = (de_av_ord1_end - de_av_ord1) / 8
  assert DE_ORD_N = 8

de_av_ord2:
	dq de_s_exsc, de_s_aedifica
	dq de_s_env, de_s_c333, de_s_env, de_s_b22, de_s_env, de_s_a1
de_av_ord2_end:
  assert (de_av_ord2_end - de_av_ord2) / 8 = DE_ORD_N

de_av_epoch:
	dq de_s_exsc, de_s_aedifica, de_s_epoch, de_s_1700
de_av_epoch_end:
DE_EPO_N = (de_av_epoch_end - de_av_epoch) / 8
  assert DE_EPO_N = 4

  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  de_ctx	rb sizeof.DrvCtx
  de_arena	rb sizeof.Arena
  de_iarena	rb sizeof.Arena
  de_scratch	rb sizeof.Arena
  de_interner	rb sizeof.Interner
  de_envmap	rb sizeof.Map
  de_srcs	rb DRV_SOURCES_MAX * sizeof.DrvSrc
  de_vlx	rb sizeof.Lexer
  de_vtoks	rb sizeof.Vec
  de_vdiags	rb sizeof.Vec
