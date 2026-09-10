; tests/unit/diag_note_derive.asm
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
; WHAT A DIAGNOSTIC SAYS BEYOND ITS CODE'S ONE SENTENCE. §8.3, and
; `docs/design/diagnostics-review.md` D1/D2/D5: 37 of 46 diagnostics across
; the review's corpus were the identical sentence `unexpected token`, and the
; record had no field that could hold another.
;
; `__diag_advise` is the derivation, and this fixture pins it as a FUNCTION:
; each row is (code, span, attached edit) in, (note id, note argument) out.
; Asserting the id rather than the English is the point -- §8.3 says text is
; not permanent, so a fixture matching on the sentence would be a fixture
; that has to be rewritten every time the wording improves. The id and the
; argument are the machine contract.
;
; One case per rule, and the ORDER of the rules is what several of them are
; really testing:
;   0  `<`         -> §8.6 decision 6, argument `lt`. The review's case c06,
;                     which the spec used to promise a sentence for and the
;                     compiler could not emit.
;   1  `>`         -> the same rule, argument `gt`. Case c14's second error.
;   2  `funtcio`   -> did you mean `functio`. Case c04, a Levenshtein-2
;                     transposition inside §8.4's closed thirty.
;   3  `if`        -> §8.4 spells this `si`, from the English-habit table.
;                     Checked BEFORE the near-miss scan on purpose: `if` is
;                     one edit from the reserved word `in` and two from `si`,
;                     so a distance-first order answers "did you mean `in`".
;   4  `a` after `if`, with an insert-`;` edit -> STILL `si`. This is the
;                     case c14 actually produces: the parser reports one
;                     token PAST the English keyword, because `if` is a legal
;                     identifier and only what follows it is unexpected. The
;                     span cannot name the mistake; the word before the
;                     insertion point can.
;   5  an insert-`;` edit with no English word before it -> the statement
;                     rule, no argument. Case c01.
;   6  an insert-`}` edit on `EXS-E0201` -> a delimiter is open, argument `}`.
;   7  `EXS-E0202` with an insert-`}` edit -> end of input, argument `}`.
;   8  `EXS-E0202` with NO edit -> end of input, no argument. The
;                     unterminated-string case c07, where no closer is known.
;   9  `12ab34` on `EXS-E0210` -> a literal takes no letter suffix. Case c26,
;                     whose message "says nothing about what is malformed."
;  10  `per` on `EXS-E0220` -> §8.4's reservation rule.
;  11  `redde` on `EXS-E0201` -> NOTHING. It is spelled correctly, so the
;                     near-miss scan must not offer `rumpe` or `refero`; a
;                     suggestion for a word that is already right is worse
;                     than silence.
;  12  `EXS-E0421` -> NOTHING. A code outside the derivation's dispatch gets
;                     no note rather than a wrong one.
;  13  an EXPLICIT note set by `diag_note_set` -> that note, unchanged, even
;                     though the record would otherwise derive one. A pass
;                     knows what it expected; nothing here can.
;
; Exit 0 = every row matched; 20+K = row K derived the wrong note id;
; 40+K = row K derived the wrong argument.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/diag/diag.inc'

segment readable executable
  start:
	xor	rbx, rbx
  .case:
	cmp	rbx, NOTE_COUNT
	jge	.all_done
	mov	r12, rbx
	imul	r12, NOTE_STRIDE

	lea	rdi, [rec]
	mov	rsi, [ntab + r12 + 0]		; code_num
	mov	rdx, 1				; file_id
	mov	rcx, [ntab + r12 + 8]		; span start
	mov	r8,  [ntab + r12 + 16]		; span len
	call	diag_init

	lea	rdi, [rec]
	lea	rsi, [srcblob]
	mov	rdx, SRC_LEN
	lea	rcx, [pathblob]
	mov	r8,  PATH_LEN
	call	diag_attach_source

	; the attached edit, if the row has one: always an insert, because
	; every edit the frontend emits on these codes today is one.
	mov	rax, [ntab + r12 + 24]		; fix text length, 0 = no fix
	test	rax, rax
	jz	.no_fix
	lea	rdi, [rec]
	call	diag_fix_ptr
	mov	rdi, rax
	mov	rsi, 1
	mov	rdx, [ntab + r12 + 32]		; insertion offset
	lea	rcx, [fixblob]
	add	rcx, [ntab + r12 + 40]
	mov	r8,  [ntab + r12 + 24]
	call	diag_fix_insert
  .no_fix:

	; the explicit-note row, which must survive the derivation untouched
	mov	rax, [ntab + r12 + 64]
	test	rax, rax
	jz	.no_explicit
	lea	rdi, [rec]
	mov	rsi, rax
	xor	rdx, rdx
	xor	rcx, rcx
	call	diag_note_set
  .no_explicit:

	lea	rdi, [rec]
	lea	rsi, [advrec]
	call	__diag_advise

	mov	eax, dword [advrec + DiagAdvice.note_id]
	cmp	rax, [ntab + r12 + 48]
	jne	.badnote

	; the argument: compared by bytes, so a note that pointed at the right
	; length of the wrong word would still fail
	mov	rax, qword [advrec + DiagAdvice.arg_len]
	cmp	rax, [ntab + r12 + 56]
	jne	.badarg
	test	rax, rax
	jz	.next
	mov	rdi, qword [advrec + DiagAdvice.arg_ptr]
	mov	rsi, rax
	lea	rdx, [argblob]
	add	rdx, [ntab + r12 + 72]
	mov	rcx, rax
	call	diagt_eq
	test	eax, eax
	jz	.badarg

  .next:
	inc	rbx
	jmp	.case

  .all_done:
	mov	eax, 231
	xor	edi, edi
	syscall

  .badnote:
	lea	rdi, [rbx + 20]
	mov	eax, 231
	syscall
  .badarg:
	lea	rdi, [rbx + 40]
	mov	eax, 231
	syscall

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

segment readable writeable
  pathblob db 'n.exs'
  PATH_LEN = $ - pathblob

  ; the two edit texts any row attaches
  fixblob  db ';', '}'
  FIX_SEMI  = 0
  FIX_BRACE = 1

  ; every note argument any row expects
  argblob:
  arg_lt:      db 'lt'
  arg_gt:      db 'gt'
  arg_functio: db 'functio'
  arg_si:      db 'si'
  arg_brace:   db '}'
  ARG_LT      = arg_lt      - argblob
  ARG_GT      = arg_gt      - argblob
  ARG_FUNCTIO = arg_functio - argblob
  ARG_SI      = arg_si      - argblob
  ARG_BRACE   = arg_brace   - argblob

  ; publica functio una(a: u64) -> u64 {
  ;     firma b: u64 = a < 3;
  ;     if a > 10 { return a; }
  ;     firma c: u64 = 12ab34;
  ;     firma per: u64 = a;
  ;     funtcio d;
  ;     redde b;
  ; }
  srcblob:
	db	0x70, 0x75, 0x62, 0x6C, 0x69, 0x63, 0x61, 0x20, 0x66, 0x75, 0x6E, 0x63
	db	0x74, 0x69, 0x6F, 0x20, 0x75, 0x6E, 0x61, 0x28, 0x61, 0x3A, 0x20, 0x75
	db	0x36, 0x34, 0x29, 0x20, 0x2D, 0x3E, 0x20, 0x75, 0x36, 0x34, 0x20, 0x7B
	db	0x0A, 0x20, 0x20, 0x20, 0x20, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x62
	db	0x3A, 0x20, 0x75, 0x36, 0x34, 0x20, 0x3D, 0x20, 0x61, 0x20, 0x3C, 0x20
	db	0x33, 0x3B, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x69, 0x66, 0x20, 0x61, 0x20
	db	0x3E, 0x20, 0x31, 0x30, 0x20, 0x7B, 0x20, 0x72, 0x65, 0x74, 0x75, 0x72
	db	0x6E, 0x20, 0x61, 0x3B, 0x20, 0x7D, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x66
	db	0x69, 0x72, 0x6D, 0x61, 0x20, 0x63, 0x3A, 0x20, 0x75, 0x36, 0x34, 0x20
	db	0x3D, 0x20, 0x31, 0x32, 0x61, 0x62, 0x33, 0x34, 0x3B, 0x0A, 0x20, 0x20
	db	0x20, 0x20, 0x66, 0x69, 0x72, 0x6D, 0x61, 0x20, 0x70, 0x65, 0x72, 0x3A
	db	0x20, 0x75, 0x36, 0x34, 0x20, 0x3D, 0x20, 0x61, 0x3B, 0x0A, 0x20, 0x20
	db	0x20, 0x20, 0x66, 0x75, 0x6E, 0x74, 0x63, 0x69, 0x6F, 0x20, 0x64, 0x3B
	db	0x0A, 0x20, 0x20, 0x20, 0x20, 0x72, 0x65, 0x64, 0x64, 0x65, 0x20, 0x62
	db	0x3B, 0x0A, 0x7D, 0x0A
  SRC_LEN = $ - srcblob

  ; code, span start, span len,
  ; fix text len (0 = no fix), fix offset, fix text offset,
  ; expected note id, expected arg len, explicit note id (0 = none),
  ; expected arg offset
  ntab:
	dq	201,  58,  1,  0, 0, 0,            DIAG_NOTE_CMP_WORD,     2, 0, ARG_LT
	dq	201,  72,  1,  0, 0, 0,            DIAG_NOTE_CMP_WORD,     2, 0, ARG_GT
	dq	201, 146,  7,  0, 0, 0,            DIAG_NOTE_DID_YOU_MEAN, 7, 0, ARG_FUNCTIO
	dq	201,  67,  2,  0, 0, 0,            DIAG_NOTE_ENGLISH_KW,   2, 0, ARG_SI
	dq	201,  70,  1,  1, 70, FIX_SEMI,    DIAG_NOTE_ENGLISH_KW,   2, 0, ARG_SI
	dq	201,  95,  5,  1, 95, FIX_SEMI,    DIAG_NOTE_STMT_SEMI,    0, 0, 0
	dq	201, 170,  1,  1, 170, FIX_BRACE,  DIAG_NOTE_UNCLOSED,     1, 0, ARG_BRACE
	dq	202, 172,  0,  1, 171, FIX_BRACE,  DIAG_NOTE_EOF_OPEN,     1, 0, ARG_BRACE
	dq	202, 172,  0,  0, 0, 0,            DIAG_NOTE_EOF_NOCLOSE,  0, 0, 0
	dq	210, 110,  6,  0, 0, 0,            DIAG_NOTE_LIT_SUFFIX,   0, 0, 0
	dq	220, 128,  3,  0, 0, 0,            DIAG_NOTE_RESERVED,     0, 0, 0
	dq	201, 161,  5,  0, 0, 0,            DIAG_NOTE_NONE,         0, 0, 0
	dq	421,  47,  1,  0, 0, 0,            DIAG_NOTE_NONE,         0, 0, 0
	dq	201,  58,  1,  0, 0, 0,            DIAG_NOTE_NO_IMPORT,    0, DIAG_NOTE_NO_IMPORT, 0
  NOTE_STRIDE = 10 * 8
  NOTE_COUNT = ($ - ntab) / NOTE_STRIDE

  rec	rb sizeof.Diag
  advrec rb sizeof.DiagAdvice
