; tests/unit/diag_escape_truncate.asm
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
; `diag_escape` must be UNABLE to overrun `out_cap`, whatever it is fed.
; escape.inc's header argues why this is a truncation rather than an
; `rassert`: the amount of output is a function of SOURCE CONTENT length, and
; source content is exactly what an adversarial input controls, so a trapping
; escaper would hand an attacker a one-line way to crash `exsc` by writing
; source that produces a diagnostic. That argument is only worth anything if
; the bound actually holds, which is what this fixture measures.
;
; The method is a poison-filled buffer, not an equality check on the output:
; all 64 bytes are set to 0xAA before every call, and after each call every
; byte from `out_cap` to the end of the buffer must still be 0xAA. An
; off-by-one that wrote a single byte past the cap would pass a
; "does the prefix look right?" test and fail this one.
;
; Input is `ab` + U+202E + `cd` -- 7 source bytes, 12 escaped bytes in text
; mode (`ab\u{202e}cd`) and 13 in JSON mode (the escape's backslash doubles).
; The interesting caps are the ones that land INSIDE the 8-byte escape
; sequence, because that is where a partial write would happen if appends
; were not all-or-nothing.
;
;   1. cap 0            -> wrote 0, truncated, nothing written at all
;   2. cap 5            -> wrote 2 ("ab"), truncated; the escape did not fit
;                          and NO prefix of it was emitted
;   3. cap 12 (exact)   -> wrote 12, NOT truncated, byte 12 onward untouched
;   4. cap 11 (one shy) -> wrote 11, truncated
;   5. JSON mode, cap 3 -> wrote 2, truncated (the JSON escape needs 9)
;   6. JSON mode, cap 13 (exact) -> wrote 13, NOT truncated
;
; Exit 0 = all checks passed; 10+N = check N failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/diag/escape.inc'

BUFSZ = 64
POISON = 0xAA

segment readable executable
  start:
	; ---- check 1: cap 0 ----
	call	poison_buf
	lea	rdi, [sample]
	mov	rsi, sample_len
	lea	rdx, [obuf]
	mov	rcx, 0
	call	diag_escape
	test	rax, rax
	jnz	.fail1
	cmp	rdx, 1
	jne	.fail1
	mov	rdi, 0
	call	check_tail
	test	eax, eax
	jz	.fail1

	; ---- check 2: cap 5, escape does not fit ----
	call	poison_buf
	lea	rdi, [sample]
	mov	rsi, sample_len
	lea	rdx, [obuf]
	mov	rcx, 5
	call	diag_escape
	cmp	rax, 2
	jne	.fail2
	cmp	rdx, 1
	jne	.fail2
	mov	rdi, 2
	call	check_tail
	test	eax, eax
	jz	.fail2

	; ---- check 3: cap 12, an exact fit ----
	call	poison_buf
	lea	rdi, [sample]
	mov	rsi, sample_len
	lea	rdx, [obuf]
	mov	rcx, 12
	call	diag_escape
	cmp	rax, 12
	jne	.fail3
	test	rdx, rdx
	jnz	.fail3
	mov	rdi, 12
	call	check_tail
	test	eax, eax
	jz	.fail3

	; ---- check 4: cap 11, one byte short ----
	call	poison_buf
	lea	rdi, [sample]
	mov	rsi, sample_len
	lea	rdx, [obuf]
	mov	rcx, 11
	call	diag_escape
	cmp	rax, 11
	jne	.fail4
	cmp	rdx, 1
	jne	.fail4
	mov	rdi, 11
	call	check_tail
	test	eax, eax
	jz	.fail4

	; ---- check 5: JSON mode, cap 3 ----
	call	poison_buf
	lea	rdi, [sample]
	mov	rsi, sample_len
	lea	rdx, [obuf]
	mov	rcx, 3
	call	diag_escape_json
	cmp	rax, 2
	jne	.fail5
	cmp	rdx, 1
	jne	.fail5
	mov	rdi, 2
	call	check_tail
	test	eax, eax
	jz	.fail5

	; ---- check 6: JSON mode, cap 13, an exact fit ----
	call	poison_buf
	lea	rdi, [sample]
	mov	rsi, sample_len
	lea	rdx, [obuf]
	mov	rcx, 13
	call	diag_escape_json
	cmp	rax, 13
	jne	.fail6
	test	rdx, rdx
	jnz	.fail6
	mov	rdi, 13
	call	check_tail
	test	eax, eax
	jz	.fail6

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1:
	mov	eax, 231
	mov	edi, 11
	syscall
  .fail2:
	mov	eax, 231
	mov	edi, 12
	syscall
  .fail3:
	mov	eax, 231
	mov	edi, 13
	syscall
  .fail4:
	mov	eax, 231
	mov	edi, 14
	syscall
  .fail5:
	mov	eax, 231
	mov	edi, 15
	syscall
  .fail6:
	mov	eax, 231
	mov	edi, 16
	syscall

; ---- fixture-local helpers -------------------------------------------------
; poison_buf -- fill all BUFSZ bytes with POISON.
  poison_buf:
	xor	rcx, rcx
  .l:
	cmp	rcx, BUFSZ
	jge	.done
	mov	byte [obuf + rcx], POISON
	inc	rcx
	jmp	.l
  .done:
	ret

; check_tail(rdi = first byte that must still be poison) -> eax = 1 if every
; byte from rdi to BUFSZ-1 is still POISON.
  check_tail:
	mov	rcx, rdi
  .l:
	cmp	rcx, BUFSZ
	jge	.ok
	cmp	byte [obuf + rcx], POISON
	jne	.bad
	inc	rcx
	jmp	.l
  .ok:
	mov	eax, 1
	ret
  .bad:
	xor	eax, eax
	ret

segment readable writeable
  sample:
	db	'ab'
	db	0xE2, 0x80, 0xAE	; U+202E RIGHT-TO-LEFT OVERRIDE
	db	'cd'
  sample_len = $ - sample

  obuf	rb BUFSZ
