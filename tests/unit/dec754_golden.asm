; tests/unit/dec754_golden.asm
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
; asm-rt fixture for compiler/x86_64/rt/dec754.inc (positive path, whole
; corpus). Runs the exact decimal-to-IEEE-754 conversion over EIGHTY golden
; rows -- forty (mantissa, exponent) pairs at both widths -- and compares
; every returned bit pattern against the expected one.
;
; WHERE THE EXPECTED BITS COME FROM: Python's `float`, which parses a
; decimal string with one correctly-rounded step -- the same property
; dec754_bits claims, so agreement is evidence and disagreement is a
; finding. The table is regenerable with prototypes/dec754/gen_golden.py
; (a design probe, never on the build closure): run it, diff its output
; against the table's `dq` lines below with the row comments stripped,
; and any difference names a bug on one side or the other. The generator
; emits in the table's grouped order, so the diff is line-for-line. Two
; traps the generator exists to remember: the expectation comes from
; PARSING the string (`float(f"{m}e{e}")`), never `m*10.0**e` -- that
; double-rounds and turns 5e-324 into 0 -- and struct's refusal to pack
; an f32 overflow IS the f32-Inf answer.
;
; WHAT THE CORPUS PINS, and why each family is here:
;   * the everyday values (0.1, 0.5, 123.456, 999) -- the divide and
;     multiply paths at both widths, with quotients that do and do not
;     overflow the p+3-bit candidate window;
;   * the f32/f64 range boundaries -- just below each max, each Inf cut,
;     the min normal, the min subnormal, and the rounds-to-zero boundary
;     on both sides of half, at both widths. dec754_bits never READS the
;     table's width column beyond 32-vs-64, so these rows are what prove
;     the exponent cuts and the subnormal path actually branch on it;
;   * round-half-to-EVEN, the rule IEEE-754 requires and the single
;     rounding step must implement: 16777217 rounds DOWN to 0x4B800000
;     and 16777219 rounds UP to 0x4B800002. THE f64 TIE IS NOT
;     EXPRESSIBLE in this corpus: the shortest decimal that ties at f64
;     width is 2^53+1, SIXTEEN digits, and the checker's packing
;     contract caps a mantissa at fifteen (the entry rassert in
;     dec754_bits). The f32 rows carry the even rule alone;
;   * the fifteen-digit clamp itself (999999999999999, and
;     333333333333333e-15 / 999999999999999e-15 as the contract-legal
;     stand-ins for the sixteen- and seventeen-digit near-boundary values
;     they were rewritten from -- see the generator's header for which
;     and why).
;
; THIS TABLE HAS ALREADY CAUGHT A REAL BUG: the quotient-drop step once
; measured the candidate's bit length with `lea rdi,[d75Q]` -- the slot's
; ADDRESS -- so f32 rows (p+3 = 27, shorter than any stack address)
; truncated every mantissa to its top bits (0.1 came out 0x3DCC0000)
; while f64 rows (p+3 = 56) passed by accident and hid it. Five of the
; f32 rows below failed under that bug; they are the pin against it and
; against anything else that ever makes the conversion's idea of the
; candidate depend on anything but the candidate.
;
; Exit 0 = all eighty rows match; 10+N = row N (counting from 1) is the
; first mismatch. No output: the failing row number names the row, and
; the generator names the expected bits.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/dec754.inc'

D75_ROWS = 80

segment readable executable
  start:
	; one row = four qwords: m, e, width, expected bits
	xor	r12d, r12d		; the row index, zero-based
  .loop:
	cmp	r12, D75_ROWS
	jae	.pass
	lea	rbx, [d75_rows]
	mov	rax, r12
	shl	rax, 5			; the row stride: four qwords, thirty-two
	lea	rsi, [rbx + rax]	; bytes
	mov	rdi, [rsi]		; m
	mov	rax, [rsi + 8]
	mov	rdx, [rsi + 16]		; width
	mov	r13, [rsi + 24]		; expected
	mov	rsi, rax		; e
	call	dec754_bits
	cmp	rax, r13
	jne	.fail
	inc	r12
	jmp	.loop
  .pass:
	xor	edi, edi
	jmp	.exit
  .fail:
	lea	rdi, [r12 + 11]		; 10 + the row number, counting from 1
  .exit:
	mov	eax, 231		; exit_group: the audit's whole diet
	syscall

segment readable
  d75_rows:
		; -- the everyday values: both paths, both widths ---------------
		dq 1, -1, 64, 0x3fb999999999999a		; 0.1
		dq 1, -1, 32, 0x3dcccccd
		dq 5, -1, 64, 0x3fe0000000000000		; 0.5
		dq 5, -1, 32, 0x3f000000
		dq 1, 0, 64, 0x3ff0000000000000		; 1.0
		dq 1, 0, 32, 0x3f800000
		dq 123, 0, 64, 0x405ec00000000000	; 123.0
		dq 123, 0, 32, 0x42f60000
		dq 1, -3, 64, 0x3f50624dd2f1a9fc		; 1e-3
		dq 1, -3, 32, 0x3a83126f
		dq 25, -1, 64, 0x4004000000000000	; 2.5
		dq 25, -1, 32, 0x40200000
		dq 35, -1, 64, 0x400c000000000000	; 3.5
		dq 35, -1, 32, 0x40600000
		dq 123456, -3, 64, 0x405edd2f1a9fbe77	; 123.456
		dq 123456, -3, 32, 0x42f6e979
		dq 1, 23, 64, 0x44b52d02c7e14af6		; 1e23
		dq 1, 23, 32, 0x65a96816
		dq 83886095, -7, 64, 0x4020c6f7d30ad46f	; f32 rounding boundary
		dq 83886095, -7, 32, 0x410637bf
		dq 1, 1, 64, 0x4024000000000000		; 10.0
		dq 1, 1, 32, 0x41200000
		dq 999, 0, 64, 0x408f380000000000	; 999.0
		dq 999, 0, 32, 0x4479c000
		; -- the range boundaries --------------------------------------
		dq 15, 299, 64, 0x7e41eb2d66005835	; 1.5e300
		dq 15, 299, 32, 0x7f800000			; f32 Inf
		dq 179769313486231, 293, 64, 0x7fb9999999999982	; just below f64 max
		dq 179769313486231, 293, 32, 0x7f800000		; f32 Inf
		dq 1, 308, 64, 0x7fe1ccf385ebc8a0	; 1e308
		dq 1, 308, 32, 0x7f800000			; f32 Inf
		dq 1, 309, 64, 0x7ff0000000000000	; f64 overflow cut
		dq 1, 309, 32, 0x7f800000
		dq 1, 338, 64, 0x7ff0000000000000	; f64 overflow cut
		dq 1, 338, 32, 0x7f800000
		dq 1, 99, 64, 0x547d42aea2879f2e		; 1e99
		dq 1, 99, 32, 0x7f800000			; f32 Inf
		dq 1, 38, 64, 0x47d2ced32a16a1b1		; 1e38
		dq 1, 38, 32, 0x7e967699
		dq 34028235, 31, 64, 0x47efffffe54daff8	; just below f32 max
		dq 34028235, 31, 32, 0x7f7fffff
		dq 117549435, -46, 64, 0x380fffffff9fdba8	; near f32 min normal
		dq 117549435, -46, 32, 0x800000
		dq 5, -324, 64, 0x1				; min subnormal f64
		dq 5, -324, 32, 0x0				; f32 zero
		dq 3, -324, 64, 0x1				; just above half, up
		dq 3, -324, 32, 0x0
		dq 2, -324, 64, 0x0				; tie below half, down
		dq 2, -324, 32, 0x0
		dq 1, -324, 64, 0x0				; below half, zero
		dq 1, -324, 32, 0x0
		dq 1, -338, 64, 0x0				; f64 underflow cut
		dq 1, -338, 32, 0x0
		dq 1, -339, 64, 0x0				; deep underflow
		dq 1, -339, 32, 0x0
		dq 1, -99, 64, 0x2b617f7d4ed8c33e	; 1e-99
		dq 1, -99, 32, 0x0				; f32 zero
		dq 1, -45, 64, 0x3696d601ad376ab9	; near f32 subnormal
		dq 1, -45, 32, 0x1
		dq 14, -46, 64, 0x369ff868bf4d956a
		dq 14, -46, 32, 0x1
		dq 7, -46, 64, 0x368ff868bf4d956a
		dq 7, -46, 32, 0x0
		dq 15, -46, 64, 0x36a1208141e9900b
		dq 15, -46, 32, 0x1
		dq 2, -45, 64, 0x36a6d601ad376ab9
		dq 2, -45, 32, 0x1
		dq 1, -40, 64, 0x37a16c262777579c
		dq 1, -40, 32, 0x116c2				; subnormal
		; -- round-half-to-even, f32 only (see header for why) ----------
		dq 16777217, 0, 64, 0x4170000010000000	; tie: f32 rounds DOWN
		dq 16777217, 0, 32, 0x4b800000
		dq 16777219, 0, 64, 0x4170000030000000	; tie: f32 rounds UP
		dq 16777219, 0, 32, 0x4b800002
		dq 16777216, 0, 64, 0x4170000000000000	; 2^24, exact
		dq 16777216, 0, 32, 0x4b800000
		; -- the fifteen-digit packing contract ------------------------
		dq 999999999999999, 0, 64, 0x430c6bf52633fff8
		dq 999999999999999, 0, 32, 0x58635fa9
		dq 333333333333333, -15, 64, 0x3fd555555555554f	; near 1/3
		dq 333333333333333, -15, 32, 0x3eaaaaab
		dq 999999999999999, -15, 64, 0x3feffffffffffff7	; near one
		dq 999999999999999, -15, 32, 0x3f800000
  d75_rows_end:					; D75_ROWS rows above, by
						; the entry rassert's grace