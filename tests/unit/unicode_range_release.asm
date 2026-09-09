; tests/unit/unicode_range_release.asm
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
; The RELEASE half of the three trie lookups' out-of-range contract, and the
; reason those lookups carry BOTH an `rassert` and a runtime range check.
;
; A codepoint at or above U+110000 reaching `xid_flags`, `script_of` or
; `nfc_ccc` is a compiler bug -- only a UTF-8 decoder that failed to reject an
; over-long or over-large sequence can produce one -- so each of them
; `rassert`s the range, and in a debug build that traps. `RELEASE` erases the
; assert (macros/assert.inc), and with it erased the trie arithmetic on its own
; would index past the end of stage 1, read whatever follows as a block number,
; and dereference up to 8 MB past stage 2. That is a segfault, not a wrong
; answer, and it is why the range check is not redundant with the assert.
;
; MEASURED, not assumed: with the range checks removed and RELEASE defined,
; this same input set faults (SIGSEGV) -- confirmed by building exactly that
; variant before this fixture was written. With them present it returns the
; documented defaults, which is what the rows below assert.
;
; This is the counterpart to tests/unit/rassert_release.asm and follows its
; idiom: `RELEASE = 1` before the include.
;
; Exit 0 = every out-of-range answer was the documented default;
; 10 xid_flags, 11 xid_start, 12 xid_continue, 13 script_of, 14 nfc_ccc,
; 15 script_resolve count, 16 script_resolve value.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'
RELEASE = 1

format ELF64 executable 3
entry start

include '../../compiler/shared/unicode/xid.inc'
include '../../compiler/shared/unicode/script.inc'
include '../../compiler/shared/unicode/nfc.inc'

segment readable executable
  start:
	xor	ebx, ebx
  .loop:
	cmp	ebx, BAD_ROWS
	jge	.done
	mov	r12d, [bad_cps + rbx*4]

	mov	edi, r12d
	call	xid_flags
	test	eax, eax
	jnz	.f10

	mov	edi, r12d
	call	xid_start
	test	eax, eax
	jnz	.f11

	mov	edi, r12d
	call	xid_continue
	test	eax, eax
	jnz	.f12

	mov	edi, r12d
	call	script_of
	cmp	eax, UNI_SCRIPT_ZZZZ
	jne	.f13

	mov	edi, r12d
	call	nfc_ccc
	test	eax, eax
	jnz	.f14

	mov	edi, r12d
	lea	rsi, [scriptbuf]
	mov	edx, UNI_SCRIPTEXT_MAX_COUNT
	call	script_resolve
	cmp	eax, 1
	jne	.f15
	movzx	r8d, byte [scriptbuf]
	cmp	r8d, UNI_SCRIPT_ZZZZ
	jne	.f16

	inc	ebx
	jmp	.loop
  .done:
	xor	edi, edi
	jmp	.exit
  .f10:	mov edi, 10
	jmp .exit
  .f11:	mov edi, 11
	jmp .exit
  .f12:	mov edi, 12
	jmp .exit
  .f13:	mov edi, 13
	jmp .exit
  .f14:	mov edi, 14
	jmp .exit
  .f15:	mov edi, 15
	jmp .exit
  .f16:	mov edi, 16
  .exit:
	mov	eax, 231
	syscall

segment readable writeable
  scriptbuf:	rb 32
  ; U+110000 itself (one past the last legal codepoint), the largest value a
  ; 4-byte UTF-8 sequence can encode, and the 32-bit ceiling.
  bad_cps:	dd 0x110000, 0x1FFFFF, 0x00200000, 0x7FFFFFFF, 0xFFFFFFFF
  bad_cps_end:
  BAD_ROWS = (bad_cps_end - bad_cps) / 4

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'
