; compiler/shared/unicode/nfc.inc
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
; Unicode Normalization Form C (UAX #15) over the generated `ccc`, `decomp`
; and `compose` blobs: canonical decomposition, canonical ordering, canonical
; composition, and the quick check that lets a caller skip all three.
;
; This is the callable .claude/agents/unicode.md promises when it says of NFC
; that "the lexer calls it, it does not reimplement it". §8.1 makes non-NFC
; source `EXS-E0102`; deciding when to emit that is the lexer's, deciding what
; NFC *is* is this file's.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1
; -----------------------------------------------------------------------------
;
; See `xid.inc`'s header for how to include this file, why `tables/tables.inc`
; is NOT included from here, and the addressing-mode fact every table lookup
; below relies on.
;
; UNITS. Everything here works on CODEPOINT ARRAYS: `dd` (u32) elements, and
; every length and return count is a codepoint count, never a byte count.
; UTF-8 decoding is deliberately not done here -- the lexer already decodes
; while it scans (§8.1's BOM/CRLF/bidi hygiene runs on the same pass), and a
; second decoder in this file would be a second thing to keep correct. Compare
; `rt/str.inc`, which draws the same line from the other side: "Decoding
; UTF-8, validating it, or reasoning about codepoints/graphemes belongs to the
; lexer and compiler/shared/unicode, not here."
;
; HANGUL IS ALGORITHMIC (UAX #15 section 3.12) and deliberately absent from the
; tables -- `tables/README.md`: "storing 11,172 syllables would be waste". The
; L/V/T arithmetic below is what replaces it, in both directions, and it is
; not an optimization the tables could have supplied instead.
;
; -----------------------------------------------------------------------------
; MEASURED FACTS. Each was read out of the checked-in blobs, not recalled, and
; each is load-bearing for the code below.
;
;   * Maximum canonical (NFD) expansion is 4 codepoints, at U+1F82. That is
;     `UNI_NFC_MAX_EXPANSION`, and it is what makes `nfc_normalize`'s output
;     capacity a static multiple of its input length instead of something the
;     caller has to discover by trying.
;   * Maximum canonical decomposition recursion depth is 3, which is why
;     `nfc_decomp_rec` recurses rather than carrying an explicit stack.
;   * `uni_decomp_keys` (2081), `uni_compose_keys` (961) and
;     `uni_scriptext_keys` (669) are each strictly ascending -- binary search
;     is well founded on all three.
;   * The `compose` table now contains ZERO pairs whose left operand is a
;     non-starter -- swept directly against the `ccc` trie, 0 of 961. It used
;     to contain four, and the history is worth keeping because the mistake
;     was subtle and the fix was not the obvious one:
;
;         U+0308 (ccc 230) + U+0301 -> U+0344
;         U+0F71 (ccc 129) + U+0F72 -> U+0F73
;         U+0F71 (ccc 129) + U+0F74 -> U+0F75
;         U+0F71 (ccc 129) + U+0F80 -> U+0F81
;
;     These are UAX #15's "non-starter decompositions" exclusion class. They
;     survived because `tools/ucd-gen/gen.py` filtered only
;     `CompositionExclusions.txt`, which carries just the PRIMARY exclusion
;     list (81 rows: script specifics and post-composition version) and not
;     the full `Full_Composition_Exclusion` derivation -- that lives in
;     `DerivedNormalizationProps.txt`, which the generator does not read. All
;     four composites ARE Full_Composition_Exclusion=Yes in UCD 17.0.0 and
;     none is in the 81-row list; verified both ways.
;
;     `gen.py` now excludes any pair with ccc(left) != 0, which needs no new
;     UCD input, and the blobs were regenerated (UNI_COMPOSE_COUNT 965 -> 961).
;     Normalization results did not move -- 100,170/0 before and after, both
;     runs performed -- because composition only ever offers the last STARTER
;     as a left operand, so those four were unreachable from `nfc_normalize`.
;     They were reachable from `nfc_compose_pair`, which is public, and that
;     is why the DATA was fixed rather than the docstring.
;
; DERIVING NFC_QC WITHOUT AN NFC_QC TABLE. `DerivedNormalizationProps.txt` is
; not among the generated blobs, so `nfc_quick_check` derives what it needs:
;
;     Full_Composition_Exclusion(c)  ==  c has a canonical decomposition AND
;                                        (it is a singleton
;                                         OR its first element is a non-starter
;                                         OR the pair does not compose back to c)
;
; VERIFIED, not argued: computed that way over all 0x110000 codepoints, the
; resulting set is EXACTLY UCD 17.0.0's NFC_QC=No set -- same 1120 members, no
; difference in either direction. Also verified that the property needs no
; recursion: there is no codepoint c with FC(c) false whose decomposition's
; first element has FC true (0 such codepoints), so testing one level is
; enough.
;
; The "first element is a non-starter" clause is REDUNDANT against the current
; table and is kept deliberately. Measured: with the four non-starter pairs now
; filtered out of `compose`, dropping the clause gives the identical 1120-member
; set for every codepoint. It stays because this module's contract is that it
; derives Full_Composition_Exclusion *from these tables*, and that derivation
; should not silently depend on a filter living in a Python file that is not on
; the build path (spec §18.1) and cannot be checked by anything the compiler
; builds. One extra `nfc_ccc` call on a path only decomposable characters reach
; is a cheap way to keep the two independent.
;
; WHAT THE QUICK CHECK RETURNS, and why it is shaped this way. `UNI_NFC_YES`
; is a proof; `UNI_NFC_NO` is a proof; `UNI_NFC_MAYBE` means "run
; `nfc_normalize` and compare". Three earlier, cheaper predicates were tried
; and each was falsified by running it against the corpus rather than by
; inspection -- in a throwaway reference implementation reading these same
; blobs, BEFORE any of this was written in assembly, so the counts below are
; predicate disagreements, not fixture failures. Recorded because the failures
; are all the same shape and the next person to try to make this cheaper will
; hit them again:
;
;   1. "Track the last starter as written and check whether the next character
;      composes onto it." 162 wrong answers in 100,170. Killed by
;      [U+1E0A, U+0323]: 1E0A's own decomposition ends in a mark of class 230,
;      the following mark is class 220, and the reordering that follows is
;      invisible to anything that looks only at the characters as written.
;   2. "...and additionally return MAYBE whenever the PRECEDING character is
;      decomposable." 16 wrong answers. Killed by [U+1611E, U+16123], where it
;      is the FOLLOWING character whose decomposition supplies the piece that
;      composes.
;   3. Both of those returned only YES/NO, which is what forced the errors to
;      be silent. The rule that survives is deliberately blunt: once two
;      adjacent characters are involved and EITHER is decomposable, say MAYBE.
;
; The surviving predicate answers YES/NO/MAYBE with zero false YES and zero
; false NO over the 100,170 NormalizationTest.txt strings (60,797 YES / 24,144
; NO / 15,229 MAYBE) and over 500,000 randomly generated strings drawn from a
; pool seeded with every case above.
;
; COST. `nfc_quick_check` is O(len) with a binary search or two per character
; and no allocation. Adding a generated NFC_QC trie would cut those searches
; to one trie load per character; it is not done here because it means
; regenerating every blob to add a table, and nothing has yet shown the cost
; matters. `[OPEN]` as a wave-3 option, not a known win.
; -----------------------------------------------------------------------------

include '../../x86_64/macros/proc.inc'
include '../../x86_64/macros/assert.inc'

; Quick-check results. UNI_NFC_MAYBE is not a failure -- it is the answer
; "cheap analysis is not enough here", and `nfc_is_nfc` handles it for you.
UNI_NFC_NO    = 0
UNI_NFC_YES   = 1
UNI_NFC_MAYBE = 2

; Worst-case canonical (NFD) expansion, in codepoints. MEASURED over all
; 0x110000 codepoints against the checked-in blobs: 4, at U+1F82 (GREEK SMALL
; LETTER ALPHA WITH PSILI AND VARIA AND YPOGEGRAMMENI). `nfc_normalize`
; requires `dstcap >= srclen * UNI_NFC_MAX_EXPANSION`, which makes the buffer
; size a caller can compute rather than discover.
UNI_NFC_MAX_EXPANSION = 4

; UAX #15 section 3.12, "Hangul Syllable Decomposition". Deliberately absent
; from the tables; these constants plus the arithmetic below are the whole
; substitute.
UNI_HANGUL_S_BASE  = 0xAC00
UNI_HANGUL_L_BASE  = 0x1100
UNI_HANGUL_V_BASE  = 0x1161
UNI_HANGUL_T_BASE  = 0x11A7
UNI_HANGUL_L_COUNT = 19
UNI_HANGUL_V_COUNT = 21
UNI_HANGUL_T_COUNT = 28
UNI_HANGUL_N_COUNT = UNI_HANGUL_V_COUNT * UNI_HANGUL_T_COUNT	; 588
UNI_HANGUL_S_COUNT = UNI_HANGUL_L_COUNT * UNI_HANGUL_N_COUNT	; 11172

; nfc_ccc(cp) -> eax = cp's Canonical_Combining_Class (0..254). Zero for a
; starter, and zero for an out-of-range codepoint. Cannot fail; leaves CF
; clear.
;
; Same two-stage trie, and the same rassert-plus-range-check pairing, as
; `xid_flags` -- see xid.inc for why both are there.
proc nfc_ccc, cp
	locals
	endl
	mov	rcx, [cp]
	rassert	rcx lt UNI_MAX_CP
	cmp	rcx, UNI_MAX_CP
	jae	.zero
	mov	eax, ecx
	shr	eax, UNI_BLOCK_SHIFT
	movzx	eax, word [uni_ccc_stage1 + rax*2]
	shl	eax, UNI_BLOCK_SHIFT
	and	ecx, UNI_BLOCK_SIZE - 1
	add	eax, ecx
	movzx	eax, byte [uni_ccc_stage2 + rax]
	return
  .zero:
	xor	eax, eax
	return
endp

; nfc_decomp_of(cp) -> eax = cp's packed decomposition record, or 0 if cp has
; no canonical decomposition in the table.
;
; The record is `(pool_element_offset << 8) | length`, exactly as
; `tables/README.md` describes it, indexing `uni_decomp_pool` in u32 ELEMENTS
; (not bytes). Zero is an unambiguous "absent" sentinel because a present
; record always has length 1 or 2, so it can never itself be zero
; (docs/asm-conventions.md, "1.3 Error protocol: CF / eax", third channel: an
; expected "absent" result is a sentinel, not `CF`).
;
; HANGUL IS NOT IN THIS TABLE and this routine does not synthesize it: a
; Hangul syllable returns 0 here. Callers that want the real decomposition
; call `nfc_decompose`.
;
; Binary search over 2081 strictly ascending u32 keys -- not a hash. CLAUDE.md,
; "Determinism is not optional": a hash table would make iteration order
; depend on the hash function, which spec §9.3 forbids for anything that can
; reach output. Cannot fail; leaves CF clear.
proc nfc_decomp_of, cp
	locals
	endl
	mov	ecx, [cp]		; needle
	xor	esi, esi		; lo = 0
	mov	edi, UNI_DECOMP_COUNT	; hi = count (exclusive)
  .search:
	cmp	esi, edi
	jae	.absent
	mov	eax, esi
	add	eax, edi
	shr	eax, 1			; mid
	mov	edx, [uni_decomp_keys + rax*4]
	cmp	edx, ecx
	jb	.go_high
	ja	.go_low
	mov	eax, [uni_decomp_vals + rax*4]
	return
  .go_high:
	mov	esi, eax
	inc	esi
	jmp	.search
  .go_low:
	mov	edi, eax
	jmp	.search
  .absent:
	xor	eax, eax
	return
endp

; nfc_compose_pair(a, b) -> eax = the codepoint (a, b) canonically composes
; to, or 0 if the pair does not compose.
;
; Zero is again a sentinel, and an unambiguous one: U+0000 has no canonical
; decomposition, so it is never a composition result.
;
; Both halves of composition live here: the two algorithmic Hangul rules
; (L+V -> LV, LV+T -> LVT, UAX #15 section 3.12) and the table. The table has
; composition exclusions and singletons already removed by
; `tools/ucd-gen/gen.py`, so a hit is unconditionally valid -- this routine
; does no exclusion filtering of its own and must not start doing any.
;
; Cannot fail; leaves CF clear.
proc nfc_compose_pair, a, b
	locals
	endl
	mov	r10d, [a]
	mov	r11d, [b]

	; ---- Hangul L + V -> LV --------------------------------------------
	mov	eax, r10d
	sub	eax, UNI_HANGUL_L_BASE
	cmp	eax, UNI_HANGUL_L_COUNT
	jae	.try_lvt
	mov	ecx, r11d
	sub	ecx, UNI_HANGUL_V_BASE
	cmp	ecx, UNI_HANGUL_V_COUNT
	jae	.table
	imul	eax, eax, UNI_HANGUL_V_COUNT
	add	eax, ecx
	imul	eax, eax, UNI_HANGUL_T_COUNT
	add	eax, UNI_HANGUL_S_BASE
	return

	; ---- Hangul LV + T -> LVT ------------------------------------------
	; `b` must be a trailing jamo strictly after T_BASE (T_BASE itself is
	; the "no trailing consonant" slot and composes with nothing), and `a`
	; must be a syllable with no trailing consonant already.
  .try_lvt:
	mov	ecx, r11d
	sub	ecx, UNI_HANGUL_T_BASE
	cmp	ecx, 1
	jb	.table
	cmp	ecx, UNI_HANGUL_T_COUNT
	jae	.table
	mov	eax, r10d
	sub	eax, UNI_HANGUL_S_BASE
	cmp	eax, UNI_HANGUL_S_COUNT
	jae	.table
	xor	edx, edx
	mov	r8d, UNI_HANGUL_T_COUNT
	div	r8d			; edx = (a - S_BASE) mod T_COUNT
	test	edx, edx
	jnz	.table
	mov	eax, r10d
	add	eax, ecx		; a + (b - T_BASE)
	return

	; ---- the table: binary search for (a << 32) | b --------------------
  .table:
	mov	ecx, [a]		; 32-bit load: zero-extends into rcx
	shl	rcx, 32
	mov	eax, [b]		; likewise into rax
	or	rcx, rax		; needle
	xor	esi, esi		; lo = 0
	mov	edi, UNI_COMPOSE_COUNT	; hi = count (exclusive)
  .search:
	cmp	esi, edi
	jae	.no_pair
	mov	eax, esi
	add	eax, edi
	shr	eax, 1			; mid
	mov	rdx, [uni_compose_keys + rax*8]
	cmp	rdx, rcx
	jb	.go_high
	ja	.go_low
	mov	eax, [uni_compose_vals + rax*4]
	return
  .go_high:
	mov	esi, eax
	inc	esi
	jmp	.search
  .go_low:
	mov	edi, eax
	jmp	.search
  .no_pair:
	xor	eax, eax
	return
endp

; nfc_decomp_rec(cp, dst) -> eax = codepoints written at dst.
;
; INTERNAL to this file in intent (the module-prefixed global label is a
; naming convention, not an export decision -- docs/asm-conventions.md,
; "4. Source file conventions"). Call `nfc_decompose` instead: this routine
; deliberately does NO capacity checking, because it is the recursive step
; and re-asserting a shrinking capacity at every level would either be wrong
; or would have to thread a second argument through the recursion for no gain.
;
; Recursion, not an explicit stack: canonical decomposition nests at most 3
; deep (measured over every key in the table), and each level's frame is
; `proc`'s own, so there is nothing to hand-manage. Writes at most
; UNI_NFC_MAX_EXPANSION codepoints. Cannot fail; leaves CF clear.
proc nfc_decomp_rec, cp, dst
	uses	rbx, r12, r13, r14
	locals
	endl
	; ---- Hangul: algorithmic, never in the table ------------------------
	mov	eax, [cp]
	sub	eax, UNI_HANGUL_S_BASE
	cmp	eax, UNI_HANGUL_S_COUNT
	jae	.table
	xor	edx, edx
	mov	ecx, UNI_HANGUL_N_COUNT
	div	ecx			; eax = i / N_COUNT, edx = i mod N_COUNT
	add	eax, UNI_HANGUL_L_BASE
	mov	rbx, [dst]
	mov	[rbx], eax		; leading jamo
	mov	eax, edx
	xor	edx, edx
	mov	ecx, UNI_HANGUL_T_COUNT
	div	ecx			; eax = rem / T_COUNT, edx = rem mod T_COUNT
	add	eax, UNI_HANGUL_V_BASE
	mov	[rbx + 4], eax		; vowel jamo
	test	edx, edx
	jz	.hangul_lv
	add	edx, UNI_HANGUL_T_BASE
	mov	[rbx + 8], edx		; trailing jamo
	mov	eax, 3
	return
  .hangul_lv:
	mov	eax, 2
	return

	; ---- the table -----------------------------------------------------
  .table:
	mov	edi, [cp]
	call	nfc_decomp_of
	test	eax, eax
	jz	.itself
	mov	r12d, eax
	and	r12d, 0xFF		; length, 1 or 2
	shr	eax, 8
	mov	r13d, eax		; pool element offset
	xor	ebx, ebx		; codepoints written so far
	xor	r14d, r14d		; element index
  .each:
	cmp	r14d, r12d
	jae	.done
	mov	eax, r13d
	add	eax, r14d
	mov	edi, [uni_decomp_pool + rax*4]
	mov	rsi, [dst]
	lea	rsi, [rsi + rbx*4]
	call	nfc_decomp_rec
	add	ebx, eax
	inc	r14d
	jmp	.each
  .done:
	mov	eax, ebx
	return

	; ---- no decomposition: the codepoint stands for itself -------------
  .itself:
	mov	rcx, [dst]
	mov	eax, [cp]
	mov	[rcx], eax
	mov	eax, 1
	return
endp

; nfc_decompose(cp, dst, dstcap) -> eax = codepoints written at dst.
;
; Full canonical decomposition (NFD) of a single codepoint, Hangul included.
; `dstcap` is a codepoint capacity and must be at least
; UNI_NFC_MAX_EXPANSION; that is `rassert`ed rather than checked, because a
; caller passing a buffer smaller than the one bound this module publishes is
; a compiler bug, not a source condition (docs/asm-conventions.md,
; "1.3 Error protocol: CF / eax", fourth channel). Cannot fail; leaves CF
; clear.
;
; NOTE the output is NOT canonically ordered on its own -- ordering is a
; property of a whole sequence, and applying it is `nfc_normalize`'s job.
proc nfc_decompose, cp, dst, dstcap
	locals
	endl
	mov	rax, [dstcap]
	rassert	rax ge UNI_NFC_MAX_EXPANSION
	mov	rdi, [cp]
	mov	rsi, [dst]
	call	nfc_decomp_rec
	return
endp

; nfc_quick_check(src, srclen) -> eax = UNI_NFC_NO / UNI_NFC_YES /
; UNI_NFC_MAYBE for the codepoint array at `src` (u32 elements, `srclen` of
; them).
;
; YES and NO are proofs; MAYBE means run `nfc_normalize` and compare, which is
; what `nfc_is_nfc` does for you. An empty sequence is YES. No allocation, no
; writes, O(srclen). Cannot fail; leaves CF clear.
;
; The predicate, and the evidence for it, are in this file's header -- read
; "WHAT THE QUICK CHECK RETURNS" before changing anything here, because the
; two obvious tightenings have both already been tried and falsified against
; the corpus.
proc nfc_quick_check, src, srclen
	uses	rbx, r12, r13, r14
	locals
		slot	prevcp, dd
		slot	prevdec, dd
	endl
	xor	ebx, ebx		; i
	mov	dword [prevcp], -1	; "no previous character yet"
	mov	dword [prevdec], 0
  .each:
	cmp	rbx, [srclen]
	jae	.yes
	mov	rax, [src]
	mov	r12d, [rax + rbx*4]	; cp

	; ---- Full_Composition_Exclusion(cp)? then cp alone is not NFC ------
	mov	edi, r12d
	call	nfc_decomp_of
	mov	r13d, eax		; packed record, 0 if none
	xor	r14d, r14d		; decomposable?
	test	r13d, r13d
	jz	.hangul_test
	mov	r14d, 1
	mov	eax, r13d
	and	eax, 0xFF
	cmp	eax, 1
	je	.no			; singleton decomposition: FC by definition
	; The pool offset is recomputed rather than kept in a register across
	; the calls: spilling it with push/pop would leave rsp misaligned at the
	; `call`, which docs/asm-conventions.md ("2. Register discipline",
	; stack alignment) does not permit, and burning a fifth callee-saved
	; register is not an option -- `uses` takes at most four and r15 is
	; pinned. A shift and an add is cheaper than either.
	mov	eax, r13d
	shr	eax, 8			; pool element offset
	mov	edi, [uni_decomp_pool + rax*4]		; first element
	call	nfc_ccc
	test	eax, eax
	jnz	.no			; non-starter decomposition: FC.
					; Redundant against the current `compose`
					; table (0 of its 961 pairs has a
					; non-starter left operand) and kept on
					; purpose -- see "DERIVING NFC_QC" in the
					; header for why this file does not lean
					; on a filter that lives in the generator.
	mov	eax, r13d
	shr	eax, 8
	mov	edi, [uni_decomp_pool + rax*4]		; first element
	mov	esi, [uni_decomp_pool + rax*4 + 4]	; second element
	call	nfc_compose_pair
	cmp	eax, r12d
	jne	.no			; does not recompose to cp: FC
	jmp	.marks

  .hangul_test:
	mov	eax, r12d
	sub	eax, UNI_HANGUL_S_BASE
	cmp	eax, UNI_HANGUL_S_COUNT
	jae	.marks
	mov	r14d, 1			; a Hangul syllable is decomposable too

	; ---- a combining mark makes the cheap analysis insufficient --------
  .marks:
	mov	edi, r12d
	call	nfc_ccc
	test	eax, eax
	jnz	.maybe

	; ---- adjacent pair --------------------------------------------------
	cmp	dword [prevcp], -1
	je	.advance
	cmp	dword [prevdec], 0
	jne	.maybe
	cmp	r14d, 0
	jne	.maybe
	mov	edi, [prevcp]
	mov	esi, r12d
	call	nfc_compose_pair
	test	eax, eax
	jnz	.no			; it composes, so `src` was not composed

  .advance:
	mov	[prevcp], r12d
	mov	[prevdec], r14d
	inc	rbx
	jmp	.each

  .yes:
	mov	eax, UNI_NFC_YES
	return
  .no:
	mov	eax, UNI_NFC_NO
	return
  .maybe:
	mov	eax, UNI_NFC_MAYBE
	return
endp

; nfc_normalize(src, srclen, dst, dstcap) -> eax = codepoints written at dst.
;
; The full UAX #15 NFC pipeline over a codepoint array: canonical
; decomposition, canonical ordering, canonical composition. `src` and `dst`
; must not overlap. `dstcap` is a codepoint capacity and must be at least
; `srclen * UNI_NFC_MAX_EXPANSION`; that is `rassert`ed, for the same reason
; `nfc_decompose`'s is. Cannot fail; leaves CF clear.
;
; All three stages run over the WHOLE input rather than segment by segment at
; normalization boundaries. Identifiers are short and this keeps the state
; small enough to read; a boundary-chunked version would be strictly more
; code for the same answer.
;
; Stage 2 is the ordinary stable bubble on combining class, including the
; step BACK on a swap -- that backward step is what makes it stable across
; three or more marks, and dropping it is a classic way to produce output
; that is nearly right.
;
; Stage 3 composes into the last starter, tracking the class of the last
; character actually kept. A character that composes is absorbed, so the
; "last class seen" deliberately does NOT advance past it. Compaction is
; in place and always safe: the write cursor never overtakes the read cursor.
proc nfc_normalize, src, srclen, dst, dstcap
	uses	rbx, r12, r13, r14
	locals
		slot	dcount, dd	; codepoints after decomposition
		slot	wcount, dd	; codepoints kept after composition
		slot	lastst, dd	; index of last starter, -1 if none yet
		slot	lastcc, dd	; class of the last character kept
	endl
	mov	rax, [srclen]
	imul	rax, rax, UNI_NFC_MAX_EXPANSION
	mov	rcx, [dstcap]
	rassert	rcx ge rax

	; ---- stage 1: canonical decomposition ------------------------------
	xor	ebx, ebx		; i
	xor	r12d, r12d		; write cursor
  .decompose:
	cmp	rbx, [srclen]
	jae	.decomposed
	mov	rax, [src]
	mov	edi, [rax + rbx*4]
	mov	rsi, [dst]
	lea	rsi, [rsi + r12*4]
	call	nfc_decomp_rec
	add	r12d, eax
	inc	rbx
	jmp	.decompose
  .decomposed:
	mov	[dcount], r12d

	; ---- stage 2: canonical ordering (stable bubble on ccc) ------------
	mov	ebx, 1			; i
  .order:
	cmp	ebx, [dcount]
	jae	.ordered
	mov	r13, [dst]
	mov	edi, [r13 + rbx*4 - 4]
	call	nfc_ccc
	mov	r12d, eax		; ccc(d[i-1])
	mov	r13, [dst]
	mov	edi, [r13 + rbx*4]
	call	nfc_ccc
	mov	r14d, eax		; ccc(d[i])
	test	r14d, r14d
	jz	.order_next		; d[i] is a starter: never reordered
	cmp	r12d, r14d
	jle	.order_next
	mov	r13, [dst]
	mov	eax, [r13 + rbx*4 - 4]
	mov	ecx, [r13 + rbx*4]
	mov	[r13 + rbx*4 - 4], ecx
	mov	[r13 + rbx*4], eax
	cmp	ebx, 1
	jle	.order			; already at the bottom; i stays 1
	dec	ebx
	jmp	.order
  .order_next:
	inc	ebx
	jmp	.order
  .ordered:

	; ---- stage 3: canonical composition --------------------------------
	cmp	dword [dcount], 0
	je	.empty
	mov	r13, [dst]
	mov	edi, [r13]
	call	nfc_ccc
	mov	[lastcc], eax
	mov	dword [lastst], -1
	test	eax, eax
	jnz	.compose_init_done
	mov	dword [lastst], 0
  .compose_init_done:
	mov	dword [wcount], 1
	mov	ebx, 1			; k
  .compose:
	cmp	ebx, [dcount]
	jae	.composed
	mov	r13, [dst]
	mov	r12d, [r13 + rbx*4]	; cp
	mov	edi, r12d
	call	nfc_ccc
	mov	r14d, eax		; c = ccc(cp)

	cmp	dword [lastst], 0
	jl	.keep
	mov	eax, [lastcc]
	cmp	eax, r14d
	jl	.try_compose
	jne	.keep
	test	eax, eax
	jnz	.keep			; blocked: last kept class >= c, c != 0
  .try_compose:
	mov	r13, [dst]
	mov	eax, [lastst]
	mov	edi, [r13 + rax*4]
	mov	esi, r12d
	call	nfc_compose_pair
	test	eax, eax
	jz	.keep
	mov	r13, [dst]
	mov	ecx, [lastst]
	mov	[r13 + rcx*4], eax	; the starter absorbs cp
	test	r14d, r14d
	jnz	.compose_next		; cp was a mark: last kept class stands
	mov	dword [lastcc], 0
	jmp	.compose_next

  .keep:
	mov	r13, [dst]
	mov	eax, [wcount]
	mov	[r13 + rax*4], r12d
	inc	eax
	mov	[wcount], eax
	test	r14d, r14d
	jnz	.keep_mark
	dec	eax
	mov	[lastst], eax		; a starter: it becomes the composition target
  .keep_mark:
	mov	[lastcc], r14d
  .compose_next:
	inc	ebx
	jmp	.compose
  .composed:
	mov	eax, [wcount]
	return
  .empty:
	xor	eax, eax
	return
endp

; nfc_is_nfc(src, srclen, scratch, scratchcap) -> eax = 1 if the codepoint
; array at `src` is already in NFC, 0 if it is not. EXACT -- no MAYBE reaches
; the caller.
;
; Runs `nfc_quick_check` first and answers from it when it is conclusive;
; otherwise normalizes into `scratch` and compares. `scratch` therefore has
; to satisfy `nfc_normalize`'s capacity contract
; (`scratchcap >= srclen * UNI_NFC_MAX_EXPANSION`) even though most calls
; never write to it. Cannot fail; leaves CF clear.
;
; §8.1 makes a non-NFC identifier `EXS-E0102`; a caller wanting that
; diagnostic tests this for 0. Emitting the code is the caller's, not this
; file's -- CLAUDE.md, "Error codes are permanent": this module registers no
; code of its own and invents none.
proc nfc_is_nfc, src, srclen, scratch, scratchcap
	uses	rbx, r12, r13
	locals
	endl
	mov	rdi, [src]
	mov	rsi, [srclen]
	call	nfc_quick_check
	cmp	eax, UNI_NFC_YES
	je	.yes
	cmp	eax, UNI_NFC_NO
	je	.no

	mov	rdi, [src]
	mov	rsi, [srclen]
	mov	rdx, [scratch]
	mov	rcx, [scratchcap]
	call	nfc_normalize
	cmp	rax, [srclen]
	jne	.no			; a different length cannot be equal
	mov	rbx, rax		; count to compare
	xor	r12d, r12d
  .compare:
	cmp	r12, rbx
	jae	.yes
	mov	rax, [src]
	mov	r13d, [rax + r12*4]
	mov	rax, [scratch]
	cmp	r13d, [rax + r12*4]
	jne	.no
	inc	r12
	jmp	.compare
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp
