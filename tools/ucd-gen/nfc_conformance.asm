; tools/ucd-gen/nfc_conformance.asm
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
; Drives compiler/shared/unicode/nfc.inc over the whole UCD
; `NormalizationTest.txt` corpus and reports real counts.
;
; VERIFICATION-ONLY, and deliberately NOT a tests/unit/ fixture: the corpus
; blob is several megabytes of derived data, so it is generated on demand and
; never checked in. The permanent, hermetic regression fixture for the same
; code is tests/unit/nfc_normalize.asm, which carries a small hand-checked
; case table instead. This file is what produces the number in
; compiler/shared/unicode/README.md's "Verified" section.
;
; RECIPE (all three steps, exactly as run):
;
;   UCD=$(nix build --no-link --print-out-paths \
;         nixpkgs#unicode-character-database)/share/unicode
;   python3 tools/ucd-gen/ntbin.py --ucd "$UCD" --out /tmp/nt_cases.bin
;   fasmg -i "NT_BLOB equ '/tmp/nt_cases.bin'" \
;         tools/ucd-gen/nfc_conformance.asm /tmp/nfcconf
;   chmod +x /tmp/nfcconf && /tmp/nfcconf | od -A d -t u8 -w8
;
; The blob path arrives through fasmg's `-i` (insert a statement at the start
; of the source) rather than a fixed relative path, so nothing generated ever
; lands inside the repository.
;
; OUTPUT: eight little-endian u64 counters on stdout, in this order --
;
;   0  records read
;   1  nfc_normalize: exact match with the corpus's expected NFC
;   2  nfc_normalize: MISMATCH
;   3  nfc_is_nfc: agreed with (NFC(src) == src)
;   4  nfc_is_nfc: DISAGREED
;   5  nfc_quick_check: said YES for a sequence that is not NFC (false YES)
;   6  nfc_quick_check: said NO for a sequence that is NFC (false NO)
;   7  index of the first failing record, or 0xFFFFFFFFFFFFFFFF if none
;
; Exit status 0 iff counters 2, 4, 5 and 6 are all zero. Counters rather than
; a formatted report because formatting decimal numbers is a page of assembly
; that would itself need testing, and `od` already exists.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1
; -----------------------------------------------------------------------------

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/shared/unicode/nfc.inc'

; Codepoint capacity of the two work buffers. ntbin.py refuses to write a blob
; whose longest sequence would not fit.
NT_CAP = 1024

; nt_seq_eq(pa, pb, cnt) -> eax = 1 if the two u32 arrays are element-wise
; equal over `cnt` elements, else 0.
proc nt_seq_eq, pa, pb, cnt
	locals
	endl
	xor	ecx, ecx
  .each:
	cmp	rcx, [cnt]
	jae	.equal
	mov	rax, [pa]
	mov	edx, [rax + rcx*4]
	mov	rax, [pb]
	cmp	edx, [rax + rcx*4]
	jne	.differ
	inc	rcx
	jmp	.each
  .equal:
	mov	eax, 1
	return
  .differ:
	xor	eax, eax
	return
endp

segment readable executable
  start:
	lea	rax, [nt_cases]
	mov	ecx, [rax]
	mov	[nt_nrec], rcx
	add	rax, 4
	mov	[nt_cur], rax
	mov	qword [nt_idx], 0
	mov	qword [nt_firstbad], -1

  .record:
	mov	rax, [nt_idx]
	cmp	rax, [nt_nrec]
	jae	.finish

	mov	rcx, [nt_cur]
	mov	eax, [rcx]
	mov	[nt_srclen], rax
	mov	eax, [rcx + 4]
	mov	[nt_explen], rax
	add	rcx, 8
	mov	[nt_srcptr], rcx
	mov	rax, [nt_srclen]
	lea	rcx, [rcx + rax*4]
	mov	[nt_expptr], rcx
	mov	rax, [nt_explen]
	lea	rcx, [rcx + rax*4]
	mov	[nt_cur], rcx
	inc	qword [nt_nread]

	; ---- nt_want = (src == exp), i.e. "src is already NFC" ----------------
	mov	qword [nt_want], 0
	mov	rax, [nt_srclen]
	cmp	rax, [nt_explen]
	jne	.want_done
	mov	rdi, [nt_srcptr]
	mov	rsi, [nt_expptr]
	mov	rdx, [nt_srclen]
	call	nt_seq_eq
	mov	[nt_want], rax
  .want_done:

	; ---- nfc_normalize -------------------------------------------------
	mov	rdi, [nt_srcptr]
	mov	rsi, [nt_srclen]
	lea	rdx, [nt_outbuf]
	mov	rcx, NT_CAP
	call	nfc_normalize
	cmp	rax, [nt_explen]
	jne	.norm_bad
	lea	rdi, [nt_outbuf]
	mov	rsi, [nt_expptr]
	mov	rdx, [nt_explen]
	call	nt_seq_eq
	test	eax, eax
	jz	.norm_bad
	inc	qword [nt_normok]
	jmp	.norm_done
  .norm_bad:
	inc	qword [nt_normbad]
	call	nt_note_first
  .norm_done:

	; ---- nfc_is_nfc ----------------------------------------------------
	mov	rdi, [nt_srcptr]
	mov	rsi, [nt_srclen]
	lea	rdx, [nt_scratchbuf]
	mov	rcx, NT_CAP
	call	nfc_is_nfc
	cmp	rax, [nt_want]
	jne	.isnfc_bad
	inc	qword [nt_isnfcok]
	jmp	.isnfc_done
  .isnfc_bad:
	inc	qword [nt_isnfcbad]
	call	nt_note_first
  .isnfc_done:

	; ---- nfc_quick_check must never contradict `nt_want` ------------------
	mov	rdi, [nt_srcptr]
	mov	rsi, [nt_srclen]
	call	nfc_quick_check
	cmp	eax, UNI_NFC_YES
	je	.q_yes
	cmp	eax, UNI_NFC_NO
	je	.q_no
	jmp	.q_done
  .q_yes:
	cmp	qword [nt_want], 0
	jne	.q_done
	inc	qword [nt_falseyes]
	call	nt_note_first
	jmp	.q_done
  .q_no:
	cmp	qword [nt_want], 0
	je	.q_done
	inc	qword [nt_falseno]
	call	nt_note_first
  .q_done:

	inc	qword [nt_idx]
	jmp	.record

  .finish:
	mov	eax, 1			; write(1, counters, 64)
	mov	edi, 1
	lea	rsi, [nt_nread]
	mov	edx, 64
	syscall

	xor	edi, edi
	cmp	qword [nt_normbad], 0
	jne	.bad
	cmp	qword [nt_isnfcbad], 0
	jne	.bad
	cmp	qword [nt_falseyes], 0
	jne	.bad
	cmp	qword [nt_falseno], 0
	jne	.bad
	jmp	.exit
  .bad:
	mov	edi, 1
  .exit:
	mov	eax, 231
	syscall

  ; Records the index of the first failing record. Plain routine, not a
  ; `proc`: it takes nothing, returns nothing, and touches only rax.
  nt_note_first:
	cmp	qword [nt_firstbad], -1
	jne	.already
	mov	rax, [nt_idx]
	mov	[nt_firstbad], rax
  .already:
	ret

segment readable writeable
  ; The eight counters, contiguous and in the documented order -- the `write`
  ; above emits this block verbatim, so DO NOT reorder or insert into it.
  nt_nread		dq 0
  nt_normok	dq 0
  nt_normbad	dq 0
  nt_isnfcok	dq 0
  nt_isnfcbad	dq 0
  nt_falseyes	dq 0
  nt_falseno	dq 0
  nt_firstbad	dq 0
  assert (nt_firstbad + 8) - nt_nread = 64

  nt_cur		dq 0
  nt_nrec		dq 0
  nt_idx		dq 0
  nt_srclen	dq 0
  nt_explen	dq 0
  nt_srcptr	dq 0
  nt_expptr	dq 0
  nt_want		dq 0
  nt_outbuf	rd NT_CAP
  nt_scratchbuf	rd NT_CAP

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'
  nt_cases:  file NT_BLOB
