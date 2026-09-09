; tests/unit/unicode_modules.asm
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
; Integration fixture: all three of compiler/shared/unicode/'s consumers --
; nfc.inc, xid.inc and script.inc -- included into ONE translation unit
; alongside a single copy of tables/tables.inc.
;
; This is the arrangement the lexer will actually use, and it is the one thing
; the three per-module fixtures cannot check, because each of them includes
; exactly one module. It proves two claims made in xid.inc's header:
;
;   1. `macros/proc.inc` and `macros/assert.inc` are safe to include three
;      times over (once from each module). They define only macros, and fasmg
;      permits macro redefinition -- unlike a file that emits labels, structs
;      or `proc`s, which is fatal the second time
;      ("symbol already defined") and is why tables/tables.inc is left for
;      the consumer to include.
;   2. The three modules can be included in any order, because none of them
;      includes another.
;
; The checks themselves are shaped like the work the lexer has to do for
; §8.1/§8.2 on a real identifier: classify the first codepoint with
; `xid_start` and the rest with `xid_continue`, ask `nfc_is_nfc` whether the
; run needs `EXS-E0102`, and resolve each codepoint's script set for the
; UTS #39 mixed-script check behind `EXS-E0104`. The POLICY is the lexer's;
; what is checked here is that the primitives are all reachable and agree.
;
; Three identifiers:
;   A  c a f + U+0065 U+0301  -- Latin, XID-legal, NOT NFC: the acute is a
;                               separate combining mark
;   B  c a f + U+00E9        -- the same identifier, NFC
;   C  U+0070 U+0061 U+0443  -- Latin p, Latin a, CYRILLIC SMALL LETTER U:
;                               XID-legal and NFC, but two scripts, which is
;                               what EXS-E0104 exists for
;
; Exit 0 = all checks passed; 10..19 = the numbered check that failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

; Deliberately NOT in dependency order -- there is no dependency order. Each
; module pulls in the same two macro files independently.
include '../../compiler/shared/unicode/script.inc'
include '../../compiler/shared/unicode/nfc.inc'
include '../../compiler/shared/unicode/xid.inc'

UM_CAP = 32

segment readable executable
  start:
	; ---- check 1: every identifier is XID-legal ------------------------
	; ident_a is the representative: c a f e + combining acute. The
	; combining mark must be XID_Continue but not XID_Start, which is what
	; makes the "first codepoint is different" rule bite.
	mov	edi, [ident_a]
	call	xid_start
	cmp	eax, 1
	jne	.f10
	mov	ebx, 1
  .xid_loop:
	cmp	ebx, IDENT_A_LEN
	jge	.xid_done
	mov	edi, [ident_a + rbx*4]
	call	xid_continue
	cmp	eax, 1
	jne	.f11
	inc	ebx
	jmp	.xid_loop
  .xid_done:
	; the combining acute alone may NOT start an identifier
	mov	edi, 0x00301
	call	xid_start
	test	eax, eax
	jnz	.f12

	; ---- check 2: NFC ---------------------------------------------------
	mov	edi, ident_a
	mov	esi, IDENT_A_LEN
	lea	rdx, [scratchbuf]
	mov	ecx, UM_CAP
	call	nfc_is_nfc
	test	eax, eax
	jnz	.f13			; A is decomposed: must NOT be NFC

	mov	edi, ident_b
	mov	esi, IDENT_B_LEN
	lea	rdx, [scratchbuf]
	mov	ecx, UM_CAP
	call	nfc_is_nfc
	cmp	eax, 1
	jne	.f14			; B is precomposed: must be NFC

	; normalizing A must produce B exactly
	mov	edi, ident_a
	mov	esi, IDENT_A_LEN
	lea	rdx, [outbuf]
	mov	ecx, UM_CAP
	call	nfc_normalize
	cmp	eax, IDENT_B_LEN
	jne	.f15
	xor	ecx, ecx
  .nfc_cmp:
	cmp	ecx, IDENT_B_LEN
	jge	.nfc_ok
	mov	r8d, [outbuf + rcx*4]
	cmp	r8d, [ident_b + rcx*4]
	jne	.f16
	inc	ecx
	jmp	.nfc_cmp
  .nfc_ok:

	; ---- check 3: scripts ----------------------------------------------
	; B is single-script Latin: every codepoint resolves to exactly {Latn}.
	xor	ebx, ebx
  .sc_loop:
	cmp	ebx, IDENT_B_LEN
	jge	.sc_done
	mov	edi, [ident_b + rbx*4]
	lea	rsi, [scriptbuf]
	mov	edx, UNI_SCRIPTEXT_MAX_COUNT
	call	script_resolve
	cmp	eax, 1
	jne	.f17
	movzx	r8d, byte [scriptbuf]
	cmp	r8d, UNI_SCRIPT_LATN
	jne	.f17
	inc	ebx
	jmp	.sc_loop
  .sc_done:

	; C mixes Latin and Cyrillic: same call, two different answers. That
	; difference is the whole input to EXS-E0104's decision, which is the
	; lexer's to make, not this file's.
	mov	edi, [ident_c]
	call	script_of
	cmp	eax, UNI_SCRIPT_LATN
	jne	.f18
	mov	edi, [ident_c + 8]
	call	script_of
	cmp	eax, UNI_SCRIPT_CYRL
	jne	.f19

	; ...and C is nonetheless perfectly well-formed on the other two axes,
	; which is exactly why the script check has to exist separately.
	mov	edi, ident_c
	mov	esi, IDENT_C_LEN
	lea	rdx, [scratchbuf]
	mov	ecx, UM_CAP
	call	nfc_is_nfc
	cmp	eax, 1
	jne	.f14

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
	jmp .exit
  .f17:	mov edi, 17
	jmp .exit
  .f18:	mov edi, 18
	jmp .exit
  .f19:	mov edi, 19
  .exit:
	mov	eax, 231
	syscall

segment readable writeable
  outbuf:	rd UM_CAP
  scratchbuf:	rd UM_CAP
  scriptbuf:	rb 32

  ident_a:	dd 0x00063,0x00061,0x00066,0x00065,0x00301	; c a f e + acute
  ident_a_end:
  IDENT_A_LEN = (ident_a_end - ident_a) / 4
  ident_b:	dd 0x00063,0x00061,0x00066,0x000E9		; c a f e-acute
  ident_b_end:
  IDENT_B_LEN = (ident_b_end - ident_b) / 4
  ident_c:	dd 0x00070,0x00061,0x00443			; p a CYRILLIC u
  ident_c_end:
  IDENT_C_LEN = (ident_c_end - ident_c) / 4

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'
