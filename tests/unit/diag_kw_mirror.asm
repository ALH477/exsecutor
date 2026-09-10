; tests/unit/diag_kw_mirror.asm
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
; ONE KEYWORD SET, TWO PLACES IT IS REACHED FROM, AND A CHECK THAT THEY ARE
; THE SAME BYTES.
;
; `diag/notes.inc` needs §8.4's thirty reserved words for its "did you mean"
; suggestion, and `lexer/token.inc` needs them to classify tokens. diag/ is
; processed FIRST in the include chain, so it cannot include
; `lexer/keywords.inc` at top level -- lexer/token.inc's own include would
; then be the second and collide on `kw_text`. notes.inc includes it inside
; `namespace diag_kw` instead, which prefixes every label it defines.
;
; This file reproduces exactly that arrangement -- diag/diag.inc first, then
; the top-level keywords.inc, the same order and the same two includes the
; real build has -- and then compares the two copies byte for byte at
; RUNTIME: the text pool, the offset table and the length table.
;
; WHY THAT IS WORTH A FIXTURE. CLAUDE.md forbids inventing a keyword as
; firmly as it forbids inventing a code, and §8.4 is normative via a
; generator that `tools/spec-check.sh` check 4 verifies. That check knows
; about `lexer/keywords.inc`. If the namespaced copy ever stopped being the
; same file -- someone "fixes" the ordering problem by pasting a second
; table, or fasmg's namespace semantics change under an upgrade -- nothing
; else in the tree would notice, and the symptom would be a suggestion
; naming a word the language does not have. This is the check that notices.
;
; Exit 0 = the two copies agree; 11 = KW_TEXT_LEN differs; 12 = the text
; pools differ; 13 = an offset differs; 14 = a length differs; 15 = the
; counts differ.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

; The real build's order, deliberately: lexer/token.inc includes
; ../diag/diag.inc and then keywords.inc, in that order and at these two
; scopes. Changing the order here would stop testing what actually ships.
include '../../compiler/x86_64/diag/diag.inc'
include '../../compiler/x86_64/lexer/keywords.inc'

; Assemble-time half of the same claim: the two constants must agree before
; any of the runtime comparisons below are worth making. fasmg's own native
; `assert`, not macros/assert.inc's runtime `rassert`.
assert diag_kw.KW_COUNT = KW_COUNT
assert diag_kw.KW_TEXT_LEN = KW_TEXT_LEN

segment readable executable
  start:
	; ---- the counts -----------------------------------------------------
	mov	rax, DIAG_KW_COUNT
	cmp	rax, KW_COUNT
	jne	.badcount

	; ---- the text pool length -------------------------------------------
	mov	rax, diag_kw.KW_TEXT_LEN
	cmp	rax, KW_TEXT_LEN
	jne	.badtextlen

	; ---- the text pool bytes --------------------------------------------
	xor	rcx, rcx
  .textloop:
	cmp	rcx, KW_TEXT_LEN
	jge	.textok
	lea	rsi, [diag_kw.kw_text]
	mov	al, [rsi + rcx]
	lea	rsi, [kw_text]
	cmp	al, [rsi + rcx]
	jne	.badtext
	inc	rcx
	jmp	.textloop
  .textok:

	; ---- the offset table (dd) and the length table (db) ----------------
	xor	rcx, rcx
  .taboop:
	cmp	rcx, KW_COUNT
	jge	.taboak
	mov	eax, [diag_kw.kw_off + rcx*4]
	cmp	eax, [kw_off + rcx*4]
	jne	.badoff
	movzx	eax, byte [diag_kw.kw_len + rcx]
	movzx	edx, byte [kw_len + rcx]
	cmp	eax, edx
	jne	.badlen
	inc	rcx
	jmp	.taboop
  .taboak:

	; ---- and the accessor agrees with the table it reads ----------------
	; diag_kw_word is what the suggestion path actually calls, so the
	; comparison above is only useful if this routine reads those bytes.
	; Index 1 is `functio` -- the word `docs/design/diagnostics-review.md`
	; case c04 is about.
	mov	rdi, 1
	call	diag_kw_word			; rax = ptr, rdx = len
	mov	rcx, rdx
	lea	rsi, [kw_text]
	mov	edx, [kw_off + 1*4]
	add	rsi, rdx
	movzx	edx, byte [kw_len + 1]
	cmp	rcx, rdx
	jne	.badlen
	xor	r8, r8
  .accloop:
	cmp	r8, rcx
	jge	.done
	mov	dl, [rax + r8]
	cmp	dl, [rsi + r8]
	jne	.badtext
	inc	r8
	jmp	.accloop

  .done:
	mov	eax, 231
	xor	edi, edi
	syscall

  .badtextlen:
	mov	edi, 11
	mov	eax, 231
	syscall
  .badtext:
	mov	edi, 12
	mov	eax, 231
	syscall
  .badoff:
	mov	edi, 13
	mov	eax, 231
	syscall
  .badlen:
	mov	edi, 14
	mov	eax, 231
	syscall
  .badcount:
	mov	edi, 15
	mov	eax, 231
	syscall
