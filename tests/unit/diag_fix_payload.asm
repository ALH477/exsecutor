; tests/unit/diag_fix_payload.asm
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
; §8.3: "Capability and lexicon errors ship machine-applicable fixes; `exsc
; emenda` applies them." This fixture holds the payload record to that
; promise: that it can express the edit, that each constructor sets exactly
; the fields it claims, and that the set of codes owing a fix is the set the
; spec names -- not a set that drifted.
;
;   1. `diag_fix_clear` leaves DIAG_FIX_NONE and every other field zero (a
;      half-populated record that is never rendered is still a record a later
;      pass could read)
;   2. `diag_fix_replace` sets kind, the edit `Span`'s three fields, and the
;      replacement text pointer and length
;   3. `diag_fix_insert` is a zero-length edit span with non-empty text --
;      `edit.len` must be 0 even though `len` was never passed
;   4. `diag_fix_delete` is a non-empty edit span with a NULL, zero-length
;      text -- both text fields must be cleared, not left from a prior use of
;      the same record (this is checked by reusing the record filled by
;      check 2)
;   5. `diag_fix_kind_str` returns the exact stable token for each of the four
;      kinds. These tokens go into machine-readable output, so they are part
;      of the contract in the same way a code is.
;   6. `diag_fix_required` agrees, for EVERY code in the generated table, with
;      §8.3's two classes: capability (`EXS-E0421`, `E0500`, `E0501`, `E0510`
;      -- §2.4's ambient-authority row, with §4.1 rule 7 putting `E0500`/
;      `E0501` in §4) and lexicon (`EXS-E0601`, `E0602`, `E0603`, `E0610` --
;      §3's rules, the range §16 names for `exsc emenda` coverage).
;      `EXS-E0520` is deliberately outside the set. Driven by
;      DIAG_CODE_COUNT, so a §13 amendment plus a regeneration is covered
;      here the moment it lands.
;   7. `diag_fix_ptr` returns the address of the record's OWN embedded fix,
;      and `diag_init` leaves that fix cleared
;
; Exit 0 = all checks passed; 10+N = check N failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/diag/diag.inc'

segment readable executable
  start:
	; ---- check 1: diag_fix_clear ----
	lea	rdi, [fixrec]
	call	diag_fix_clear
	cmp	dword [fixrec + DiagFix.kind], DIAG_FIX_NONE
	jne	.fail1
	cmp	dword [fixrec + DiagFix.edit.file_id], 0
	jne	.fail1
	cmp	dword [fixrec + DiagFix.edit.start], 0
	jne	.fail1
	cmp	dword [fixrec + DiagFix.edit.len], 0
	jne	.fail1
	cmp	qword [fixrec + DiagFix.text_ptr], 0
	jne	.fail1
	cmp	qword [fixrec + DiagFix.text_len], 0
	jne	.fail1

	; ---- check 2: diag_fix_replace ----
	lea	rdi, [fixrec]
	mov	rsi, 7
	mov	rdx, 100
	mov	rcx, 12
	lea	r8, [repl]
	mov	r9, repl_len
	call	diag_fix_replace
	cmp	dword [fixrec + DiagFix.kind], DIAG_FIX_REPLACE
	jne	.fail2
	cmp	dword [fixrec + DiagFix.edit.file_id], 7
	jne	.fail2
	cmp	dword [fixrec + DiagFix.edit.start], 100
	jne	.fail2
	cmp	dword [fixrec + DiagFix.edit.len], 12
	jne	.fail2
	lea	rax, [repl]
	cmp	qword [fixrec + DiagFix.text_ptr], rax
	jne	.fail2
	cmp	qword [fixrec + DiagFix.text_len], repl_len
	jne	.fail2

	; ---- check 3: diag_fix_insert ----
	lea	rdi, [fixrec]
	mov	rsi, 9
	mov	rdx, 55
	lea	rcx, [repl]
	mov	r8, repl_len
	call	diag_fix_insert
	cmp	dword [fixrec + DiagFix.kind], DIAG_FIX_INSERT
	jne	.fail3
	cmp	dword [fixrec + DiagFix.edit.file_id], 9
	jne	.fail3
	cmp	dword [fixrec + DiagFix.edit.start], 55
	jne	.fail3
	cmp	dword [fixrec + DiagFix.edit.len], 0
	jne	.fail3
	cmp	qword [fixrec + DiagFix.text_len], repl_len
	jne	.fail3

	; ---- check 4: diag_fix_delete clears BOTH text fields ----
	lea	rdi, [fixrec]
	mov	rsi, 4
	mov	rdx, 8
	mov	rcx, 3
	call	diag_fix_delete
	cmp	dword [fixrec + DiagFix.kind], DIAG_FIX_DELETE
	jne	.fail4
	cmp	dword [fixrec + DiagFix.edit.start], 8
	jne	.fail4
	cmp	dword [fixrec + DiagFix.edit.len], 3
	jne	.fail4
	cmp	qword [fixrec + DiagFix.text_ptr], 0
	jne	.fail4
	cmp	qword [fixrec + DiagFix.text_len], 0
	jne	.fail4

	; ---- check 5: the stable kind tokens ----
	mov	edi, DIAG_FIX_NONE
	call	diag_fix_kind_str
	mov	rdi, rax
	mov	rsi, rdx
	lea	rdx, [k_none]
	mov	rcx, k_none_len
	call	diagt_eq
	test	eax, eax
	jz	.fail5
	mov	edi, DIAG_FIX_REPLACE
	call	diag_fix_kind_str
	mov	rdi, rax
	mov	rsi, rdx
	lea	rdx, [k_replace]
	mov	rcx, k_replace_len
	call	diagt_eq
	test	eax, eax
	jz	.fail5
	mov	edi, DIAG_FIX_INSERT
	call	diag_fix_kind_str
	mov	rdi, rax
	mov	rsi, rdx
	lea	rdx, [k_insert]
	mov	rcx, k_insert_len
	call	diagt_eq
	test	eax, eax
	jz	.fail5
	mov	edi, DIAG_FIX_DELETE
	call	diag_fix_kind_str
	mov	rdi, rax
	mov	rsi, rdx
	lea	rdx, [k_delete]
	mov	rcx, k_delete_len
	call	diagt_eq
	test	eax, eax
	jz	.fail5

	; ---- check 6: which codes owe a fix, over the whole table ----
	xor	rbx, rbx
  .req:
	cmp	rbx, REQ_COUNT
	jge	.req_done
	mov	edi, [req_tab + rbx*8]
	call	diag_fix_required
	cmp	eax, [req_tab + rbx*8 + 4]
	jne	.fail6
	inc	rbx
	jmp	.req
  .req_done:

	; ---- check 7: diag_fix_ptr, and diag_init clears the fix ----
	lea	rdi, [rec]
	mov	rsi, 421
	mov	rdx, 1
	mov	rcx, 0
	mov	r8, 0
	call	diag_init
	lea	rdi, [rec]
	call	diag_fix_ptr
	lea	rcx, [rec + Diag.fix]
	cmp	rax, rcx
	jne	.fail7
	cmp	dword [rax + DiagFix.kind], DIAG_FIX_NONE
	jne	.fail7

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
  .fail7:
	mov	eax, 231
	mov	edi, 17
	syscall

; ---- byte helpers (fixture-local; see this file's header) ------------------
; diagt_eq(rdi=a, rsi=alen, rdx=b, rcx=blen) -> eax = 1 if identical.
  diagt_eq:
	cmp	rsi, rcx
	jne	.ne
	xor	r8, r8
  .l:
	cmp	r8, rsi
	jge	.eq
	mov	al, [rdi + r8]
	cmp	al, [rdx + r8]
	jne	.ne
	inc	r8
	jmp	.l
  .eq:
	mov	eax, 1
	ret
  .ne:
	xor	eax, eax
	ret

; diagt_find(rdi=hay, rsi=hlen, rdx=needle, rcx=nlen) -> eax = 1 if `needle`
; occurs anywhere in `hay`. This is what makes the "never the raw codepoint"
; check independent of the expected-output comparison: even if BOTH the
; renderer and this fixture's expected bytes were wrong in the same way, a
; raw bidi override in the output would still be caught here.
  diagt_find:
	test	rcx, rcx
	jz	.no
	cmp	rsi, rcx
	jb	.no
	mov	r8, rsi
	sub	r8, rcx			; last valid start offset
	xor	r9, r9
  .outer:
	cmp	r9, r8
	jg	.no
	xor	r10, r10
  .inner:
	cmp	r10, rcx
	jge	.yes
	mov	r11, r9
	add	r11, r10
	mov	al, [rdi + r11]
	cmp	al, [rdx + r10]
	jne	.next
	inc	r10
	jmp	.inner
  .next:
	inc	r9
	jmp	.outer
  .yes:
	mov	eax, 1
	ret
  .no:
	xor	eax, eax
	ret

segment readable writeable
  repl	db 'archivum'
  repl_len = $ - repl
  k_none	db 'none'
  k_none_len = $ - k_none
  k_replace	db 'replace'
  k_replace_len = $ - k_replace
  k_insert	db 'insert'
  k_insert_len = $ - k_insert
  k_delete	db 'delete'
  k_delete_len = $ - k_delete

  ; (code numeric, expected diag_fix_required) in §13's own table order
  req_tab:
	dd	101, 0	; EXS-E0101
	dd	102, 0	; EXS-E0102
	dd	103, 0	; EXS-E0103
	dd	104, 0	; EXS-E0104
	dd	105, 0	; EXS-E0105
	dd	106, 0	; EXS-E0106
	dd	201, 0	; EXS-E0201
	dd	202, 0	; EXS-E0202
	dd	203, 0	; EXS-E0203
	dd	210, 0	; EXS-E0210
	dd	220, 0	; EXS-E0220
	dd	311, 0	; EXS-E0311
	dd	321, 0	; EXS-E0321
	dd	322, 0	; EXS-E0322
	dd	332, 0	; EXS-E0332
	dd	421, 1	; EXS-E0421
	dd	500, 1	; EXS-E0500
	dd	501, 1	; EXS-E0501
	dd	510, 1	; EXS-E0510
	dd	520, 0	; EXS-E0520
	dd	601, 1	; EXS-E0601
	dd	602, 1	; EXS-E0602
	dd	603, 1	; EXS-E0603
	dd	610, 1	; EXS-E0610
  REQ_COUNT = ($ - req_tab) / 8
  ; This fixture's expectation table must cover the GENERATED one exactly --
  ; a §13 amendment that adds a code and a regeneration that picks it up must
  ; not leave check 6 silently testing a subset. fasmg's own native,
  ; assemble-time `assert` (NOT macros/assert.inc's runtime `rassert`), so a
  ; drift is a build failure rather than something a reader has to notice.
  assert REQ_COUNT = DIAG_CODE_COUNT

  fixrec rb sizeof.DiagFix
  rec	 rb sizeof.Diag
