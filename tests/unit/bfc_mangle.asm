; tests/unit/bfc_mangle.asm
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
; backend_c fixture for docs/design/c-backend.md D5's identifier mangling --
; the first place in this tree a mangling is stated, which is why it gets a
; fixture of its own rather than riding on the emitter's.
;
; THE RULE. C identifiers are [A-Za-z_][A-Za-z0-9_]*, which is stricter than
; an Exsecutor name (spec 8.1 admits any XID sequence, in NFC UTF-8). So:
;
;   [A-Za-z0-9]   kept
;   `_`           kept, UNLESS the next two bytes are both UPPERCASE hex
;                 digits, in which case it is escaped as `_5F`
;   anything else `_` + two UPPERCASE hex digits of the byte
;
; WHAT EACH CASE PROVES, and why the case exists at all:
;
;   scribe_octeto  an underscore SURVIVES. The lookahead is two bytes and
;                  `oc` is not a hex pair, so a Latin name with an
;                  underscore reads in C exactly as it does in Exsecutor --
;                  which is the whole reason the rule is a lookahead rather
;                  than "double every underscore" (D5's first rejection: it
;                  is injective too, but Kiln would have to spell
;                  `exs_lege__caput`).
;   Type_2Em       `.` (0x2E) escaped, and CASE PRESERVED either side of it.
;   lambda_247     `$` (0x24) escaped, and the DIGIT after it not confused
;                  with the escape -- `_24` then `7`, decoded left to right,
;                  is `$` then `7` and nothing else.
;   m_C4_93ns_C5_ABra   the two-byte UTF-8 of `ē` (C4 93) and `ū` (C5 AB),
;                  each byte escaped separately. This is the case that makes
;                  the mangling INJECTIVE over non-ASCII: two names differing
;                  only outside ASCII get two different C names, where a
;                  scheme that dropped or folded non-ASCII would collide.
;   a_5F5F         THE ESCAPE'S OWN ESCAPE, and the reason the `_` rule has
;                  an exception at all. The source name is `a`, `_`, `5`,
;                  `F`. Keeping the underscore would give `a_5F`, which
;                  decodes as `a` + byte 0x5F = `a_` -- a DIFFERENT name, so
;                  the mangling would not be injective. Escaping it gives
;                  `a_5F5F`, which decodes as `a` + `_` + `5` + `F`.
;                  Without this one line the scheme is quietly wrong for a
;                  whole family of names and nothing else here would notice.
;   b_5g           the NEAR MISS that proves the lookahead is two bytes and
;                  checks BOTH: `5` is a hex digit, `g` is not, so the
;                  underscore is kept. An implementation that tested only
;                  the first byte would escape this one.
;   c_5            the lookahead RUNNING OUT: one byte follows the
;                  underscore, so no escape could be read back from it and
;                  the underscore is kept. An implementation that read past
;                  the end to decide would be reading out of bounds.
;   lowercase hex  `d_5f` keeps its underscore, because the escape alphabet
;                  is UPPERCASE only. That is D5's second rejection made
;                  visible: with lowercase escapes, a source `_` before any
;                  of a-f would need escaping, which is most Latin names.
;
; Mutation (run) -- each of these was tried and each failed here:
;   the `_` lookahead deleted (always keep)          -> a_5F5F fails
;   the lookahead testing only the first byte        -> b_5g fails
;   the end-of-input guard dropped                   -> c_5 fails or reads
;                                                       out of bounds
;   __bfc_hexnib emitting lowercase                  -> Type_2Em fails
;   the `[A-Za-z0-9]` range missing its digits       -> lambda_247 fails
;
; Exit 0 = every case mangles exactly as stated. 12 = one did not, and the
; whole actual output goes to stderr first.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/span.inc'
include '../../compiler/x86_64/backend_fasmg/verify.inc'
include '../../compiler/x86_64/backend_c/emit_c.inc'

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail1

	; A BfaPrintCtx over one output Vec is all __bfc_mangle needs: it
	; reaches the module through nothing, only the buffer.
	lea	rdi, [ar]
	mov	rsi, sizeof.BfaPrintCtx
	call	arena_alloc
	mov	[mgctx], rax
	lea	rdi, [ar]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	rcx, [mgctx]
	mov	[rcx + BfaPrintCtx.obuf], rax
	mov	rdi, rax
	lea	rsi, [ar]
	mov	rdx, 1
	mov	rcx, 256
	call	vec_init

	; Every case, one after another, separated by a newline so a failure's
	; stderr reads as a list rather than as one run-on string.
	mov	rdi, [mgctx]
	lea	rsi, [n1]
	mov	rdx, n1.len
	call	__bfc_mangle
	call	.nl
	mov	rdi, [mgctx]
	lea	rsi, [n2]
	mov	rdx, n2.len
	call	__bfc_mangle
	call	.nl
	mov	rdi, [mgctx]
	lea	rsi, [n3]
	mov	rdx, n3.len
	call	__bfc_mangle
	call	.nl
	mov	rdi, [mgctx]
	lea	rsi, [n4]
	mov	rdx, n4.len
	call	__bfc_mangle
	call	.nl
	mov	rdi, [mgctx]
	lea	rsi, [n5]
	mov	rdx, n5.len
	call	__bfc_mangle
	call	.nl
	mov	rdi, [mgctx]
	lea	rsi, [n6]
	mov	rdx, n6.len
	call	__bfc_mangle
	call	.nl
	mov	rdi, [mgctx]
	lea	rsi, [n7]
	mov	rdx, n7.len
	call	__bfc_mangle
	call	.nl
	mov	rdi, [mgctx]
	lea	rsi, [n8]
	mov	rdx, n8.len
	call	__bfc_mangle
	call	.nl

	mov	rcx, [mgctx]
	mov	rcx, [rcx + BfaPrintCtx.obuf]
	mov	rax, [rcx + Vec.data]
	mov	[outptr], rax
	mov	rax, [rcx + Vec.len]
	mov	[outlen], rax

	mov	rdi, [outptr]
	mov	rsi, [outlen]
	lea	rdx, [expected]
	mov	rcx, expected.len
	call	__bfa_streq
	test	eax, eax
	jz	.fail2

	mov	eax, 231
	xor	edi, edi
	syscall

  .nl:
	mov	rdi, [mgctx]
	mov	esi, 10
	call	__bfa_out_byte
	ret

  .fail1:
	mov	eax, 231
	mov	edi, 11
	syscall
  .fail2:
	mov	edi, 2
	mov	rsi, [outptr]
	mov	rdx, [outlen]
	call	sys_write
	mov	eax, 231
	mov	edi, 12
	syscall

segment readable writeable
  ar:     rb sizeof.Arena
  mgctx:  dq 0
  outptr: dq 0
  outlen: dq 0

  n1: db "scribe_octeto"
  .len = $ - n1
  n2: db "Type.m"
  .len = $ - n2
  n3: db "lambda$7"
  .len = $ - n3
  ; `mēnsūra` in NFC UTF-8: m, U+0113 (C4 93), n, s, U+016B (C5 AB), r, a.
  ; Written as explicit bytes rather than as source text, because a fixture
  ; whose point is its bytes must be written with something that emits those
  ; bytes (CONTRIBUTING, "Fixtures") -- and because tests/unit/ is not one of
  ; the byte-exact paths .gitattributes marks.
  n4: db "m", 0xC4, 0x93, "ns", 0xC5, 0xAB, "ra"
  .len = $ - n4
  n5: db "a_5F"
  .len = $ - n5
  n6: db "b_5g"
  .len = $ - n6
  n7: db "c_5"
  .len = $ - n7
  n8: db "d_5f"
  .len = $ - n8

  expected:
  db "scribe_octeto", 10
  db "Type_2Em", 10
  db "lambda_247", 10
  db "m_C4_93ns_C5_ABra", 10
  db "a_5F5F", 10
  db "b_5g", 10
  db "c_5", 10
  db "d_5f", 10
  .len = $ - expected
