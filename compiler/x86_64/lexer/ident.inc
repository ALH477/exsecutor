; compiler/x86_64/lexer/ident.inc
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
; §8.2 IDENTIFIERS: the UTS #39 restriction-level test behind `EXS-E0104`.
;
; §8.2 gives three rules. Two of them are structural and live in
; lexer/lex.inc, where they cost nothing because the scanner is already
; walking codepoints: "UAX #31 XID_Start/XID_Continue plus `_`" is what
; decides where an identifier begins and ends, and "comparison is byte
; equality after NFC" is why lexer/token.inc interns the raw source bytes
; (see its header). The third rule needs real work and is this file:
;
;   > UTS #39 **Moderately Restrictive**. Mixed-script is `EXS-E0104`.
;
; A LEXER THAT REJECTS `cafe` WITH AN ACUTE IS NOT SECURE, IT IS BROKEN. That
; is the failure mode this file is most likely to have, so it is the one
; tests/unit/lexer_ident_scripts.asm spends most of its rows on: legitimate
; non-ASCII identifiers in Latin, Greek, Cyrillic, Han, Hiragana+Han and
; Arabic must all tokenize with no diagnostic at all. §8.2 is explicit --
; "Non-ASCII identifiers are allowed" -- and a false `EXS-E0104` would make
; the language's own claim false.
;
; WHAT "MODERATELY RESTRICTIVE" IS, from UTS #39 section 5.2, and which parts
; are implemented:
;
;   level 1  ASCII-Only            every character is ASCII
;   level 2  Single Script         the resolved script sets have a common
;                                  member; Common and Inherited match anything
;   level 3  Highly Restrictive    level 2, or covered by Latin+Han+Hiragana+
;                                  Katakana, Latin+Han+Bopomofo, or
;                                  Latin+Han+Hangul
;   level 4  Moderately Restrictive
;                                  level 3, or covered by Latin plus any ONE
;                                  other Recommended script except Cyrillic,
;                                  Greek and Cherokee
;
; Levels 1, 2 and 3 are implemented exactly. Level 4 is implemented WITHOUT
; the "Recommended" qualifier, and that gap is `[UNIMPLEMENTED]` rather than
; hidden: UAX #31's Recommended-scripts list is not a property in the pinned
; UCD (compiler/shared/unicode/tables/ has Scripts.txt and
; ScriptExtensions.txt data and nothing about script status), so applying it
; would mean hardcoding ~30 ISO 15924 codes that no generated table backs --
; the same posture compiler/shared/unicode/README.md takes toward
; `confusables.txt`. The effect is bounded and one-directional: an identifier
; mixing Latin with a Limited-Use or Excluded script is ACCEPTED here where
; UTS #39 would reject it. It can never cause a false `EXS-E0104`.
; REPORTED to the unicode agent: a `script_status` table generated from
; UAX #31 Table 5 would close it, alongside the `confusables.txt` gap that
; `EXS-E0105` already waits on.
;
; NO AUGMENTED SCRIPT SETS, BY EQUIVALENCE, NOT BY OMISSION. UTS #39 also
; describes level 3 via "augmented script sets" (Han gains Hanb/Jpan/Kore,
; Hiragana and Katakana gain Jpan, ...), which exists so that the level-2
; intersection test alone yields the CJK answers. This file implements the
; three covered-by tests directly instead, which is the same section's own
; prose formulation and gives the same verdicts without a second script
; vocabulary to keep in step with the tables.
;
; NOT HERE: `EXS-E0105`. §8.2 scopes confusable detection to the IMPORT
; CLOSURE, not the compilation unit, and gives the reason in the spec itself
; -- "Module A exporting Cyrillic `add` and module B calling `add` contains no
; confusable *pair* in either unit." A single-file lexer cannot see a closure,
; the UTS #39 data it needs is not hermetically available
; (compiler/shared/unicode/README.md, "Not here yet"), and it lands with the
; `ego` reader. Nothing here attempts it and nothing here blocks on it.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.2; §13 for `EXS-E0104`.
; -----------------------------------------------------------------------------

include 'source.inc'

; The widest resolved script set the shipped tables can produce
; (`UNI_SCRIPTEXT_MAX_COUNT` is 23; compiler/shared/unicode/README.md records
; the sweep that measured it). A buffer this size can never trip
; `script_resolve`'s capacity rassert.
LEX_SCRIPT_SET_CAP = UNI_SCRIPTEXT_MAX_COUNT
; The same capacity rounded up to 8, for `locals` slots -- see the comment in
; `__lex_single_script`'s `locals` block.
LEX_SCRIPT_SET_SLOT = 24

; ---- set primitives --------------------------------------------------------

; __lex_set_meets(a_ptr, a_n, b_ptr, b_n) -> eax = 1 if the two ascending
; byte-array script sets share at least one member, else 0.
;
; A linear merge, not a nested loop: `script_resolve` documents that every
; resolved set it produces is "sorted ascending, no duplicates", so a caller
; "intersecting two resolved sets can therefore do a linear merge and does not
; need to sort first". Cannot fail; leaves CF clear.
proc __lex_set_meets, a_ptr, a_n, b_ptr, b_n
	uses	rbx, r12
	locals
	endl
	xor	rbx, rbx			; index into a
	xor	r12, r12			; index into b
  .step:
	cmp	rbx, [a_n]
	jae	.no
	cmp	r12, [b_n]
	jae	.no
	mov	rax, [a_ptr]
	movzx	ecx, byte [rax + rbx]
	mov	rax, [b_ptr]
	movzx	edx, byte [rax + r12]
	cmp	ecx, edx
	je	.yes
	jb	.bump_a
	inc	r12
	jmp	.step
  .bump_a:
	inc	rbx
	jmp	.step
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __lex_set_keep(dst, dst_n, b_ptr, b_n) -> eax = the new element count of
; `dst`, which is narrowed in place to `dst` intersect `b`. Both inputs are
; ascending with no duplicates and the result is too, so the narrowing can be
; applied repeatedly. The write cursor never overtakes the read cursor, so the
; in-place compaction is always safe. Cannot fail; leaves CF clear.
proc __lex_set_keep, dst, dst_n, b_ptr, b_n
	uses	rbx, r12, r13
	locals
	endl
	xor	rbx, rbx			; read index into dst
	xor	r12, r12			; index into b
	xor	r13, r13			; write index into dst
  .step:
	cmp	rbx, [dst_n]
	jae	.done
	cmp	r12, [b_n]
	jae	.done
	mov	rax, [dst]
	movzx	ecx, byte [rax + rbx]
	mov	rax, [b_ptr]
	movzx	edx, byte [rax + r12]
	cmp	ecx, edx
	je	.both
	jb	.bump_dst
	inc	r12
	jmp	.step
  .bump_dst:
	inc	rbx
	jmp	.step
  .both:
	mov	rax, [dst]
	mov	[rax + r13], cl
	inc	r13
	inc	rbx
	inc	r12
	jmp	.step
  .done:
	mov	rax, r13
	return
endp

; __lex_is_wildcard(set_ptr, set_n) -> eax = 1 if this resolved script set is
; the one-element Common or Inherited set, which UTS #39 treats as matching
; every script, else 0.
;
; A codepoint that HAS an explicit Script_Extensions entry can never land
; here: compiler/shared/unicode/README.md records that no entry in the shipped
; scriptext table contains Common or Inherited. So this test is exactly "the
; primary Script property is Common or Inherited and there is no extension",
; which is what UTS #39 means. Cannot fail; leaves CF clear.
proc __lex_is_wildcard, set_ptr, set_n
	locals
	endl
	cmp	qword [set_n], 1
	jne	.no
	mov	rax, [set_ptr]
	movzx	ecx, byte [rax]
	cmp	ecx, UNI_SCRIPT_ZYYY
	je	.yes
	cmp	ecx, UNI_SCRIPT_ZINH
	je	.yes
  .no:
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; ---- the three restriction-level tests -------------------------------------

; __lex_single_script(ptr, len) -> eax = 1 if the identifier's resolved script
; sets have a member in common (UTS #39 level 2), else 0. An identifier made
; entirely of Common/Inherited characters -- digits and `_` -- is Single
; Script trivially, and that is the answer UTS #39 gives too.
;
; Assumes the bytes are valid UTF-8, which lexer/source.inc has already
; established: `lex_run` does not tokenize a file that failed the §8.1 pass.
; A decode failure here would therefore be an internal contract violation, so
; it traps rather than being reported (docs/asm-conventions.md, "1.3 Error
; protocol: CF / eax" -- the rassert channel). Cannot fail; leaves CF clear.
proc __lex_single_script, ptr, len
	uses	rbx, r12, r14
	locals
		; Scalars first so each lands 8-aligned: `slot` offsets simply
		; accumulate with no padding of their own, so a 24-byte array
		; declared ahead of a `dq` would misalign it for nothing.
		slot	accn,  dq
		slot	onen,  dq
		slot	anyset, dd	; 0 while every codepoint was a wildcard
		slot	pad,   dd
		slot	acc,   LEX_SCRIPT_SET_SLOT
		slot	one,   LEX_SCRIPT_SET_SLOT
	endl
	xor	rbx, rbx			; byte cursor
	mov	qword [accn], 0
	mov	dword [anyset], 0
  .next:
	cmp	rbx, [len]
	jae	.verdict
	mov	rdi, [ptr]
	add	rdi, rbx
	mov	rsi, [len]
	sub	rsi, rbx
	call	__diag_utf8_decode
	mov	r14, rdx			; bytes consumed
	mov	rcx, DIAG_CP_INVALID
	rassert	rax ne rcx
	mov	r12d, eax			; the codepoint

	mov	edi, r12d
	lea	rsi, [one]
	mov	rdx, LEX_SCRIPT_SET_CAP
	call	script_resolve
	mov	[onen], rax

	lea	rdi, [one]
	mov	rsi, [onen]
	call	__lex_is_wildcard
	test	eax, eax
	jnz	.skip

	cmp	dword [anyset], 0
	jne	.narrow
	; first non-wildcard codepoint: the intersection starts as its set
	mov	dword [anyset], 1
	mov	rcx, [onen]
	mov	[accn], rcx
	lea	rdi, [acc]
	lea	rsi, [one]
	rep	movsb
	jmp	.skip
  .narrow:
	lea	rdi, [acc]
	mov	rsi, [accn]
	lea	rdx, [one]
	mov	rcx, [onen]
	call	__lex_set_keep
	mov	[accn], rax
	test	rax, rax
	jz	.no
  .skip:
	add	rbx, r14
	jmp	.next

  .verdict:
	cmp	dword [anyset], 0
	je	.yes				; all wildcards
	cmp	qword [accn], 0
	je	.no
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __lex_covered_by(ptr, len, set_ptr, set_n) -> eax = 1 if every non-wildcard
; codepoint of the identifier has a resolved script in the given ascending
; set, else 0. This is UTS #39's "the string is covered by <these scripts>".
; Cannot fail; leaves CF clear.
proc __lex_covered_by, ptr, len, set_ptr, set_n
	uses	rbx, r12, r14
	locals
		slot	onen, dq
		slot	one,  LEX_SCRIPT_SET_SLOT
	endl
	xor	rbx, rbx
  .next:
	cmp	rbx, [len]
	jae	.yes
	mov	rdi, [ptr]
	add	rdi, rbx
	mov	rsi, [len]
	sub	rsi, rbx
	call	__diag_utf8_decode
	mov	r14, rdx
	mov	rcx, DIAG_CP_INVALID
	rassert	rax ne rcx
	mov	r12d, eax

	mov	edi, r12d
	lea	rsi, [one]
	mov	rdx, LEX_SCRIPT_SET_CAP
	call	script_resolve
	mov	[onen], rax

	lea	rdi, [one]
	mov	rsi, [onen]
	call	__lex_is_wildcard
	test	eax, eax
	jnz	.skip

	lea	rdi, [one]
	mov	rsi, [onen]
	mov	rdx, [set_ptr]
	mov	rcx, [set_n]
	call	__lex_set_meets
	test	eax, eax
	jz	.no
  .skip:
	add	rbx, r14
	jmp	.next
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; ---- the fixed script sets level 3 tests against ---------------------------
; Ascending by script id, because that is what `__lex_set_meets` requires. The
; ids come from compiler/shared/unicode/tables/tables.inc, which numbers every
; registered ISO 15924 short code alphabetically -- so these arrays are sorted
; by their SYMBOLIC names too, and a reader can check the ordering without
; looking the numbers up: Bopo(14) < Hang(49) < Hani(50) < Hira(54) <
; Kana(63) < Latn(74).
;
; Deliberately NOT reached by execution: like diag/render.inc's literal
; fragments, each sits between two `proc` bodies and every `proc` leaves
; through `endp`'s `ret`.
lex_set_jpan db UNI_SCRIPT_HANI, UNI_SCRIPT_HIRA, UNI_SCRIPT_KANA, UNI_SCRIPT_LATN
LEX_SET_JPAN_N = $ - lex_set_jpan
lex_set_hanb db UNI_SCRIPT_BOPO, UNI_SCRIPT_HANI, UNI_SCRIPT_LATN
LEX_SET_HANB_N = $ - lex_set_hanb
lex_set_kore db UNI_SCRIPT_HANG, UNI_SCRIPT_HANI, UNI_SCRIPT_LATN
LEX_SET_KORE_N = $ - lex_set_kore
lex_set_latn db UNI_SCRIPT_LATN
LEX_SET_LATN_N = $ - lex_set_latn

; ---- the §8.2 verdict ------------------------------------------------------

; lex_script_ok(ptr, len) -> eax = 1 if the identifier spelled by the `len`
; bytes at `ptr` satisfies §8.2's UTS #39 Moderately Restrictive level, else
; 0. The caller raises `EXS-E0104`; this routine only answers, exactly as the
; three compiler/shared/unicode/ modules do ("None of them emits a diagnostic
; or decides policy; they answer questions") -- keeping the code in one place,
; lexer/lex.inc. Cannot fail; leaves CF clear.
proc lex_script_ok, ptr, len
	uses	rbx, r12, r13, r14
	locals
		slot	onen, dq
		slot	pair, 2			; { Latn, X }, ascending
		slot	one,  LEX_SCRIPT_SET_SLOT
	endl
	; ---- level 1: ASCII-Only -------------------------------------------
	; The overwhelmingly common case, and it costs one pass over the bytes
	; with no table lookups at all.
	xor	rbx, rbx
  .ascii:
	cmp	rbx, [len]
	jae	.ok
	mov	rax, [ptr]
	cmp	byte [rax + rbx], 0x80
	jae	.wide
	inc	rbx
	jmp	.ascii

  .wide:
	; ---- level 2: Single Script ----------------------------------------
	mov	rdi, [ptr]
	mov	rsi, [len]
	call	__lex_single_script
	test	eax, eax
	jnz	.ok

	; ---- level 3: the three CJK combinations ---------------------------
	mov	rdi, [ptr]
	mov	rsi, [len]
	lea	rdx, [lex_set_jpan]
	mov	rcx, LEX_SET_JPAN_N
	call	__lex_covered_by
	test	eax, eax
	jnz	.ok
	mov	rdi, [ptr]
	mov	rsi, [len]
	lea	rdx, [lex_set_hanb]
	mov	rcx, LEX_SET_HANB_N
	call	__lex_covered_by
	test	eax, eax
	jnz	.ok
	mov	rdi, [ptr]
	mov	rsi, [len]
	lea	rdx, [lex_set_kore]
	mov	rcx, LEX_SET_KORE_N
	call	__lex_covered_by
	test	eax, eax
	jnz	.ok

	; ---- level 4: Latin plus one other script --------------------------
	; The candidate X must come from the resolved set of the FIRST
	; non-wildcard codepoint that does NOT already admit Latin. If no such
	; codepoint existed, every non-wildcard set would contain Latin, Latin
	; would be in the level-2 intersection, and Single Script would have
	; answered yes above -- so this search is complete, not a heuristic.
	xor	rbx, rbx
  .seek:
	cmp	rbx, [len]
	jae	.bad
	mov	rdi, [ptr]
	add	rdi, rbx
	mov	rsi, [len]
	sub	rsi, rbx
	call	__diag_utf8_decode
	mov	r14, rdx
	mov	rcx, DIAG_CP_INVALID
	rassert	rax ne rcx
	mov	r12d, eax

	mov	edi, r12d
	lea	rsi, [one]
	mov	rdx, LEX_SCRIPT_SET_CAP
	call	script_resolve
	mov	[onen], rax

	lea	rdi, [one]
	mov	rsi, [onen]
	call	__lex_is_wildcard
	test	eax, eax
	jnz	.seek_next

	lea	rdi, [one]
	mov	rsi, [onen]
	lea	rdx, [lex_set_latn]
	mov	rcx, LEX_SET_LATN_N
	call	__lex_set_meets
	test	eax, eax
	jz	.candidates			; this set has no Latin: use it
  .seek_next:
	add	rbx, r14
	jmp	.seek

  .candidates:
	xor	r13, r13
  .try:
	cmp	r13, [onen]
	jae	.bad
	lea	rax, [one]
	movzx	r12d, byte [rax + r13]
	; §8.2's level 4 names three exclusions outright. Common, Inherited and
	; Unknown are excluded here as well, not as extra policy but because
	; none of them is "any one other script": the first two are wildcards
	; and cannot be a set member at all (`__lex_is_wildcard` filtered
	; them), and Unknown is the table's own "no script" answer.
	cmp	r12d, UNI_SCRIPT_LATN
	je	.try_next
	cmp	r12d, UNI_SCRIPT_CYRL
	je	.try_next
	cmp	r12d, UNI_SCRIPT_GREK
	je	.try_next
	cmp	r12d, UNI_SCRIPT_CHER
	je	.try_next
	cmp	r12d, UNI_SCRIPT_ZYYY
	je	.try_next
	cmp	r12d, UNI_SCRIPT_ZINH
	je	.try_next
	cmp	r12d, UNI_SCRIPT_ZZZZ
	je	.try_next

	; { Latn, X } ascending. Latn is 74, so which order depends on X.
	cmp	r12d, UNI_SCRIPT_LATN
	ja	.latn_first
	mov	[pair],   r12b
	mov	byte [pair+1], UNI_SCRIPT_LATN
	jmp	.have_pair
  .latn_first:
	mov	byte [pair],   UNI_SCRIPT_LATN
	mov	[pair+1], r12b
  .have_pair:
	mov	rdi, [ptr]
	mov	rsi, [len]
	lea	rdx, [pair]
	mov	rcx, 2
	call	__lex_covered_by
	test	eax, eax
	jnz	.ok
  .try_next:
	inc	r13
	jmp	.try

  .ok:
	mov	eax, 1
	return
  .bad:
	xor	eax, eax
	return
endp

