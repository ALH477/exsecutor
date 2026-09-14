; compiler/x86_64/rt/dec754.inc
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
; dec754_bits(m, e, width) -- the exact decimal-to-IEEE-754 conversion a
; float literal needs, as one correctly-rounded step.
;
; THE CALL: a decimal float literal (spec §8.4's grammar as this wave lands
; it) is `m * 10^e` with `m` an integer of at most FIFTEEN significant digits
; and `e` a signed decimal exponent. `checker/types/sig.inc`'s
; `chk_ty_floatval` packs that pair into the literal's `konst`; when an
; expectation settles, `__chk_ty_fits` calls THIS routine with the target
; width and overwrites the `konst` with the bit pattern returned here, which
; `lower/expr.inc` emits as a `BFA_OP_FCONST` through `lwr_fconst`.
;
; WHY THIS FILE EXISTS AT ALL, rather than a call to the host's own
; conversion: CLAUDE.md's freestanding contract. `exsc` links nothing, calls
; no libc, and -- the part that matters here -- executes no floating-point
; instruction anywhere. `strtod` is libc; `x87`'s load-and-store rounding
; is floating point. Either would put the FPU inside the compiler forever.
; So the conversion is INTEGER ARITHMETIC ONLY: multiplies, shifts,
; compares. The SSE/x87 units are never touched, and a build of `exsc` on a
; machine with no FPU would convert literals identically.
;
; CORRECTNESS: Clinger's classical shape. Compute the value EXACTLY as a
; big integer (or a big-integer quotient), reduce to a significand with
; guard and sticky bits, and round to the target precision ONCE. One
; rounding, round-to-nearest-even, is what IEEE-754 requires of a
; decimal-to-binary conversion; any scheme that rounds twice (say, decimal
; mantissa to double, then double to float) risks the double-rounding
; hazard -- a value that lies exactly on a tie boundary after the first
; rounding but not before. Every intermediate here is exact, so the single
; rounding at the end IS the IEEE answer, bit for bit, and the golden
; fixture (tests/unit/dec754_golden.asm) pins that against a corpus
; computed by Python's `float`, which is itself correctly rounded.
;
; THE TWO PATHS, and why `e` splits at zero:
;
;   e >= 0: the value is the integer `m * 10^e`. Compute it exactly in a
;           big-int buffer (10^e by binary exponentiation -- repeated
;           squaring -- then one multiply), keep the top p+3 bits as the
;           significand candidate, and the OR of everything below as
;           sticky.
;
;   e < 0:  the value is the rational `m / 10^|e|`. Binary long division,
;           one quotient bit per step, MSB first: double the remainder,
;           bring in the next numerator bit, subtract the divisor when it
;           fits. The numerator is `m << k` with `k` chosen so the quotient
;           comes out at p+3 bits -- clamped at zero when a wide mantissa
;           against a small power of ten makes the integer part alone wider
;           than that, in which case the whole quotient still fits a u64
;           and a drop step after the loop reduces it to the top p+3 bits
;           with everything cut off folded into sticky. The loop runs to
;           the numerator's LAST bit, so the final remainder is exact and
;           "nonzero" is the whole error term of the candidate -- the value
;           lies strictly between `Q * 2^-s` and `(Q+1) * 2^-s` iff that
;           remainder is nonzero.
;
;   Both paths then feed ONE rounding core, written once.
;
; SIZES, argued rather than hoped. The checker refuses a literal whose
; exponent leaves [-4096, 4095] (below that every width rounds it to zero,
; above, past every maximum), and the range cuts below decide everything
; outside |e| <= 338 without arithmetic, because at those exponents the
; rounding answer is forced whatever the mantissa is. 10^338 < 2^1124, so
; every power and product fits eighteen 64-bit words; the division's
; numerator is `m << k` and `blm + k = blP + p + 3` whenever k > 0, so it
; never exceeds 1180 bits -- nineteen words. Buffers are twenty words
; (D75_WMAX) -- or forty where a squaring could structurally write na+nb
; words -- with the bounds `rassert`ed or argued in place, not hoped.
;
; DETERMINISM (§9.3): every result is a pure function of (m, e, width).
; No address arithmetic, no input-dependent iteration order, and nothing
; read that was not handed in.
;
; INCLUDE DISCIPLINE. This file brings `macros/proc.inc` and
; `macros/assert.inc` and NOTHING else -- the arrangement
; `shared/unicode/*` uses, and safe for the reason token.inc's header
; records: those two define macros and nothing else, so no second include
; path can collide. That matters because `checker/types/types.inc`
; includes this file directly: modules above rt/ include no rt/ FILE (the
; D1 invariant, lower/ssa.inc's header), and the one fatal case there was
; a second path to an rt/ STRUCT. This file defines one proc and brings
; none, so the checker may reach it the way it reaches nothing else in
; rt/ -- and a unit fixture may include it alone, without the checker.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.4 (floats), §8.4 (literal
; grammar), §9.3 (determinism); docs/design/ssa-ir.md section 2.1
; (`fconst`, the consumer of these bits).
; -----------------------------------------------------------------------------

include '../macros/proc.inc'
include '../macros/assert.inc'

; One big-int buffer is twenty u64 words = 1280 bits. The largest value
; held is the division numerator at < 2^1180 (the size argument in the
; header), so twenty is headroom, not a tight fit.
D75_WMAX = 20

; The per-width parameters, IEEE-754 binary32/binary64: `p` the significand
; width in bits including the implicit leading bit, `emin`/`emax` the
; unbiased range of normal exponents. The stored emin+1 is 1, not 0 -- the
; subnormal encoding -- and the rounding core assembles that explicitly
; rather than flowing a zero exponent field out of the arithmetic.
D75_F64_P	= 53
D75_F64_BIAS	= 1023
D75_F64_EMIN	= -1022
D75_F64_EMAX	= 1023
D75_F64_INF	= 0x7FF0000000000000

D75_F32_P	= 24
D75_F32_BIAS	= 127
D75_F32_EMIN	= -126
D75_F32_EMAX	= 127
D75_F32_INF	= 0x7F800000

; The exponent cuts, in DECIMAL. A value >= 10^309 is past f64's maximum
; (~1.798e308) whatever the mantissa is, and >= 10^39 past f32's; a value
; <= 10^-324 at f64 is below half the smallest subnormal (2^-1075 ~
; 4.94e-324) even with a full fifteen-digit mantissa, since m * 10^e <=
; 10^(e+15) <= 10^-324, and <= 10^-150 at f32 likewise. Both directions are
; decided with NO arithmetic, which is also what keeps the buffers bounded:
; only |e| <= 338 ever reaches the big integers.
D75_F64_EMAX10	= 309
D75_F64_EMIN10	= -339
D75_F32_EMAX10	= 39
D75_F32_EMIN10	= -165

; ============================================================================
; small pieces
; ============================================================================

; dec754_bl64(v) -> rax = the bit length of the nonzero u64 `v`: the index
; of its top set bit, plus one. `bsr` is undefined on zero, so nonzero is
; the caller's contract; every caller derives `v` from an `m >= 1`, a
; quotient `>= 2^26`, or a word already scanned set.
proc dec754_bl64, d75v
	locals
	endl
	bsr	rax, [d75v]
	inc	rax
	return
endp

; dec754_blw(buf, w) -> rax = the bit length of the first `w` words of
; `buf`, 0 for all-zero. Top-down scan: the first nonzero word decides, and
; `bsr` gives its top bit. A linear scan, not a binary search, for the same
; reason every lookup in this compiler is linear (CLAUDE.md, "Determinism
; is not optional"): the order of the scan is a function of the data and
; nothing else.
proc dec754_blw, d75b, d75n
	uses	rbx
	locals
		slot d75i, dq
	endl
	mov	rbx, [d75b]
	mov	rax, [d75n]
  .word:
	test	rax, rax
	jz	.zero
	dec	rax
	mov	[d75i], rax
	mov	rcx, [rbx + rax*8]
	test	rcx, rcx
	jz	.word
	bsr	rcx, rcx
	mov	rax, [d75i]
	shl	rax, 6
	lea	rax, [rax + rcx + 1]
	return
  .zero:
	xor	eax, eax
	return
endp

; dec754_zero(buf, w) -- the first `w` words of `buf` to zero. `rep stosq`
; rather than a loop of `mov [..], 0`; DF is clear per SysV (token.inc's
; `kw_lookup` relies on the same rule).
proc dec754_zero, d75b, d75n
	uses	rdi
	locals
	endl
	mov	rdi, [d75b]
	mov	rcx, [d75n]
	xor	eax, eax
	cld
	rep	stosq
	return
endp

; dec754_mul(dst, a, na, b, nb) -- `dst = a * b`, schoolbook, with `na`/`nb`
; the exact word counts of the inputs. `dst` must not alias either source,
; and must have room for `na + nb` words, which this file's size argument
; bounds for every call site: the destination is cleared for exactly
; `na + nb` words and the product is written into them.
;
; 128-BIT ACCUMULATION, the standard shape: `mul` gives the full 64x64
; product in rdx:rax, and the running sum (the partial-product word already
; in `dst`, plus the product low, plus the carry) is accumulated with
; `add`/`adc` into a fresh high word each step. Rows run in ASCENDING order,
; which is what makes the row-carry a plain store: when row `i` finishes,
; word `i + nb` has been touched by no earlier row (row `i' < i` writes at
; most word `i' + nb - 1 + 1 = i' + nb < i + nb`), while later rows ADD into
; it through the inner loop. A descending row order would clobber that
; carry word with the previous row's partial sum -- the bug ascending order
; exists to make impossible.
proc dec754_mul, d75d, d75a, d75na, d75b, d75nb
	uses	rbx, r12, r13, r14
	locals
		slot d75i, dq		; the row index i, 0 .. na-1
		slot d75j, dq		; the column index j, 0 .. nb-1
		slot d75c, dq		; the carry
		slot d75x, dq		; the dst index i + j
	endl
	mov	rbx, [d75d]
	mov	r12, [d75a]
	mov	r13, [d75b]
	mov	rax, [d75na]
	mov	rcx, [d75nb]
	add	rcx, rax
	rassert	rcx le 2*D75_WMAX	; the largest structural write, a square
	mov	rdi, rbx
	xor	eax, eax
	cld
	rep	stosq
	mov	qword [d75i], 0
  .row:
	mov	rax, [d75i]
	cmp	rax, [d75na]
	jae	.done
	mov	r14, [r12 + rax*8]		; a[i]
	test	r14, r14
	jz	.rownext		; a zero word contributes nothing but time
	mov	qword [d75c], 0
	mov	qword [d75j], 0
  .col:
	mov	rax, r14
	mov	rcx, [d75j]
	mul	qword [r13 + rcx*8]	; rdx:rax = a[i] * b[j]
	mov	rcx, [d75i]
	add	rcx, [d75j]		; the dst word i + j
	add	rax, [rbx + rcx*8]
	adc	rdx, 0
	add	rax, [d75c]
	adc	rdx, 0
	mov	[rbx + rcx*8], rax
	mov	[d75c], rdx
	inc	qword [d75j]
	mov	rax, [d75j]
	cmp	rax, [d75nb]
	jb	.col
	mov	rax, [d75i]
	add	rax, [d75nb]
	mov	rcx, [d75c]
	mov	[rbx + rax*8], rcx	; plain store -- proven untouched (header)
  .rownext:
	inc	qword [d75i]
	jmp	.row
  .done:
	return
endp

; dec754_pow10(dst, n) -- `dst = 10^n` for 1 <= n <= 338, by binary
; exponentiation: square a running base and multiply it into the result for
; each set bit of `n`. Repeated squaring rather than `n` multiplies by ten
; because the ladder's bound is provable: the base is 10^(2^i) only while
; bits of `n` remain above `i`, so every intermediate -- the base, its
; square, and the result -- is <= 10^n < 2^1124, eighteen words. The
; squaring STRUCTURALLY writes na+nb words into its scratch even though the
; product is eighteen, so the scratch here is forty words, the one buffer
; that big.
proc dec754_pow10, d75o, d75e
	uses	rbx, r12
	locals
		slot d75base, 160	; D75_WMAX words
		slot d75tmp, 320	; 2*D75_WMAX: a square's structural write
		slot d75n, dq
		slot d75rw, dq		; the result's word count
		slot d75bw, dq		; the base's word count
		slot d75k, dq
	endl
	mov	rax, [d75e]
	mov	[d75n], rax
	rassert	rax ge 1
	rassert	rax le 338
	mov	rbx, [d75o]

	; result = 1, base = 10
	mov	rdi, rbx
	mov	rsi, D75_WMAX
	call	dec754_zero
	mov	qword [rbx], 1
	mov	qword [d75rw], 1
	lea	rdi, [d75base]
	mov	rsi, D75_WMAX
	call	dec754_zero
	mov	qword [d75base], 10
	mov	qword [d75bw], 1

  .bit:
	mov	rax, [d75n]
	test	rax, 1
	jz	.nosq
	; result = result * base, into the scratch, then the product's OWN
	; word count copied back -- no more: the scratch beyond na+nb holds an
	; earlier square's leftovers, and copying twenty words would smuggle
	; them into the result's high words and poison the next bit-length
	; scan
	mov	rax, [d75rw]
	add	rax, [d75bw]
	mov	[d75k], rax
	lea	rdi, [d75tmp]
	mov	rsi, rbx
	mov	rdx, [d75rw]
	lea	rcx, [d75base]
	mov	r8, [d75bw]
	call	dec754_mul
	lea	rdi, [d75tmp]
	mov	rsi, [d75k]
	call	dec754_blw
	add	rax, 63
	shr	rax, 6
	mov	[d75k], rax
	mov	rdi, rbx
	lea	rsi, [d75tmp]
	mov	rcx, rax
	cld
	rep	movsq
	mov	[d75rw], rax
  .nosq:
	shr	qword [d75n], 1
	jz	.done			; no bits left: square NOT once more
	; base = base * base, same scratch discipline
	mov	rax, [d75bw]
	add	rax, [d75bw]
	mov	[d75k], rax
	lea	rdi, [d75tmp]
	lea	rsi, [d75base]
	mov	rdx, [d75bw]
	lea	rcx, [d75base]
	mov	r8, [d75bw]
	call	dec754_mul
	lea	rdi, [d75tmp]
	mov	rsi, [d75k]
	call	dec754_blw
	add	rax, 63
	shr	rax, 6
	mov	[d75k], rax
	lea	rdi, [d75base]
	lea	rsi, [d75tmp]
	mov	rcx, rax
	cld
	rep	movsq
	mov	[d75bw], rax
	jmp	.bit
  .done:
	; the bound the ladder argument claims, checked rather than trusted
	mov	rdi, rbx
	mov	rsi, D75_WMAX
	call	dec754_blw
	rassert	rax le 1124		; 10^338 < 2^1124
	return
endp

; dec754_shl(dst, m, nbits) -- `dst = m << nbits`, `m` a plain u64, `dst`
; a D75_WMAX-word buffer (zeroed here). This is the division path's
; numerator: the shift is chosen (in `dec754_bits`) so the quotient comes
; out with exactly p+2 or p+3 significant bits, which is what makes guard
; and sticky the whole of the rounding information.
proc dec754_shl, d75o, d75m, d75s
	uses	rbx
	locals
		slot d75w, dq		; the word offset
		slot d75o2, dq		; the in-word bit offset
	endl
	mov	rbx, [d75o]
	mov	rdi, rbx
	mov	rsi, D75_WMAX
	call	dec754_zero
	mov	rax, [d75s]
	mov	rcx, rax
	and	rax, 63
	mov	[d75o2], rax
	shr	rcx, 6
	mov	[d75w], rcx
	; `m` is at most 2^50 and `nbits` at most ~1180, so the shifted top
	; word index stays inside the buffer
	rassert	rcx le D75_WMAX-2
	cmp	qword [d75o2], 0
	je	.even
	mov	rdx, [d75m]
	mov	rcx, 64
	sub	rcx, [d75o2]
	mov	rax, rdx
	shr	rax, cl
	mov	rcx, [d75w]
	mov	[rbx + rcx*8 + 8], rax
	mov	rcx, [d75o2]
	mov	rax, rdx
	shl	rax, cl
	mov	rcx, [d75w]
	mov	[rbx + rcx*8], rax
	return
  .even:
	mov	rax, [d75m]
	mov	rcx, [d75w]
	mov	[rbx + rcx*8], rax
	return
endp

; dec754_top(buf, d) -> rax = `buf >> d` for `d` below the buffer's bit
; length. Two words through the same window `dec754_shl` writes: the low
; half from word d/64, the high half from the word above it, which is
; always inside the buffer because `d` is at most bitlen-p-3 here.
proc dec754_top, d75b, d75d
	uses	rbx
	locals
		slot d75w, dq
		slot d75o, dq
	endl
	mov	rbx, [d75b]
	mov	rax, [d75d]
	mov	rcx, rax
	and	rax, 63
	mov	[d75o], rax
	shr	rcx, 6
	mov	[d75w], rcx
	mov	rdx, [rbx + rcx*8]
	cmp	qword [d75o], 0
	je	.out
	; the LOW half first: buf[w] >> o. The first draft OR'd the high
	; half straight into UNSHIFTED buf[w], so every window with a
	; nonzero offset returned buf[w]'s low bits in place under the
	; high half -- a garbage candidate with its top bit stuck at 63,
	; which the exponent e2 = L-1-s then took for the value's own
	mov	rcx, [d75o]
	shr	rdx, cl
	mov	rcx, [d75w]
	mov	rax, [rbx + rcx*8 + 8]
	mov	rcx, 64
	sub	rcx, [d75o]
	shl	rax, cl
	or	rdx, rax
  .out:
	mov	rax, rdx
	return
endp

; dec754_orlow(buf, nbits) -> rax = 1 if any of `buf`'s low `nbits` bits is
; set -- the sticky bit. Whole words OR-reduced from the bottom, the
; boundary word masked. It answers "is anything down there nonzero", never
; "what", which is all rounding needs: that is the sticky bit's definition.
proc dec754_orlow, d75b, d75k
	uses	rbx
	locals
		slot d75w, dq
		slot d75o, dq
		slot d75i, dq
	endl
	mov	rbx, [d75b]
	mov	rax, [d75k]
	mov	rcx, rax
	and	rax, 63
	mov	[d75o], rax
	shr	rcx, 6
	mov	[d75w], rcx
	mov	[d75i], rcx
  .word:
	mov	rax, [d75i]
	test	rax, rax
	jz	.tail
	dec	rax
	mov	[d75i], rax
	mov	rdx, [rbx + rax*8]
	test	rdx, rdx
	jnz	.yes
	jmp	.word
  .tail:
	mov	rax, [d75o]
	test	rax, rax
	jz	.no
	mov	rcx, rax
	mov	rax, 1
	shl	rax, cl
	dec	rax
	mov	rcx, [d75w]
	and	rax, [rbx + rcx*8]
	test	rax, rax
	jnz	.yes
  .no:
	xor	eax, eax
	return
  .yes:
	mov	eax, 1
	return
endp

; ============================================================================
; the conversion
; ============================================================================

; dec754_bits(m, e, width) -> rax = the IEEE-754 bit pattern of the decimal
; value `m * 10^e` rounded to `width` (32 or 64), round-to-nearest-even.
; Both widths' parameters are selected here; the guard/sticky/round core
; below is written ONCE and shared, per the header's correctness argument.
;
; THE SHIFT BOOKKEEPING, stated once because every rounding decision hangs
; on it: the significand candidate `Q` carries exactly `p+3` significant
; bits and represents `Q * 2^(-s)`, with `s` possibly NEGATIVE -- which
; means `Q * 2^|s|`, the multiplier case. So the value is always
;
;     Q * 2^(-s)      with L = bitlen(Q), s the candidate's scale
;
; and the exponent of Q's leading bit is `e2 = L - 1 - s`. `p+3` candidate
; bits leave the two low bits for the guard/sticky decision, and every
; rounding path reduces to: kept = the top p bits of Q, guard = the next
; one, sticky = "anything below", plus the one bit the long division's
; exact final remainder contributes.
;
; NORMAL VS SUBNORMAL, the IEEE-754 subtlety: when e2 >= emin, the kept
; p bits ARE the significand and e2 lands in the (biased) exponent field.
; When e2 < emin the value is subnormal: the significand's implicit bit
; stays at weight 2^emin (not 2^e2), so `d = s + emin - p + 1` more bits
; drop off the candidate's bottom, those extra bits join sticky, and the
; same round-to-nearest-even runs at the reduced precision. Two boundary
; facts are handled explicitly rather than flowing out of the arithmetic:
; a kept that rounds UP to exactly 2^(p-1) is the SMALLEST NORMAL (exponent
; field 1, mantissa 0), not a subnormal; and a d >= 64 means every
; significant bit dropped off -- the value rounds to zero, since the cuts
; above already guaranteed |e| <= 338 keeps only near-half cases here.
proc dec754_bits, d75m, d75e, d75w
	uses	rbx, r12, r13
	locals
		; the big-int work buffers: P the power of ten, R the product or
		; the running remainder, X the division numerator. R is the only
		; one needing D75_WMAX words; P at most 18 (10^338), X at most
		; blP + 53 + 2 bits, also under 20 words.
		slot d75P, 160
		slot d75R, 160
		slot d75X, 160
		slot d75p, dq			; the target precision p
		slot d75bias, dq
		slot d75emin, dq
		slot d75emax, dq
		slot d75inf, dq
		slot d75blp, dq			; bit length of 10^|e|
		slot d75nP, dq			; word count of P
		slot d75s, dq			; the candidate's scale
		slot d75Q, dq			; the candidate
		slot d75stk, dq			; the division's exact remainder flag
		slot d75L, dq			; bitlen(Q)
		slot d75e2, dq			; the leading bit's exponent
		slot d75k, dq
		slot d75t, dq
		slot d75g, dq
		slot d75u, dq
	endl
	mov	rax, [d75m]
	test	rax, rax
	jnz	.nz
	xor	eax, eax		; a zero mantissa is zero at any width
	return
  .nz:
	; the checker packs a fifteen-digit-or-fewer mantissa; this is the
	; contract's cheap end, asserted rather than re-derived
	shr	rax, 50
	rassert	rax eq 0

	; width select
	cmp	qword [d75w], 64
	je	.w64
	mov	rax, D75_F32_P
	mov	[d75p], rax
	mov	rax, D75_F32_BIAS
	mov	[d75bias], rax
	mov	rax, D75_F32_EMIN
	mov	[d75emin], rax
	mov	rax, D75_F32_EMAX
	mov	[d75emax], rax
	mov	rax, D75_F32_INF
	mov	[d75inf], rax
	mov	rax, [d75e]
	cmp	rax, D75_F32_EMAX10
	jge	.retinf
	cmp	rax, D75_F32_EMIN10
	jle	.ret0
	jmp	.split
  .w64:
	mov	rax, D75_F64_P
	mov	[d75p], rax
	mov	rax, D75_F64_BIAS
	mov	[d75bias], rax
	mov	rax, D75_F64_EMIN
	mov	[d75emin], rax
	mov	rax, D75_F64_EMAX
	mov	[d75emax], rax
	mov	rax, D75_F64_INF
	mov	[d75inf], rax
	mov	rax, [d75e]
	cmp	rax, D75_F64_EMAX10
	jge	.retinf
	cmp	rax, D75_F64_EMIN10
	jle	.ret0

  .split:
	mov	rax, [d75e]
	test	rax, rax
	jz	.e0			; 10^0: pow10 rasserts n >= 1
	js	.divide
	jmp	.multiply

  .e0:
	; m * 10^0 = m, already exact and small: m is at most 2^50 and p+3 is
	; at least 27, so the candidate is m itself with scale zero
	mov	rax, [d75m]
	mov	[d75Q], rax
	mov	qword [d75s], 0
	mov	qword [d75stk], 0
	jmp	.core

  .multiply:
	; ---- e > 0: N = m * 10^e, exact big integer -------------------------
	lea	rdi, [d75P]
	mov	rsi, [d75e]
	call	dec754_pow10
	lea	rdi, [d75P]
	mov	rsi, D75_WMAX
	call	dec754_blw
	mov	[d75blp], rax
	add	rax, 63
	shr	rax, 6
	mov	[d75nP], rax
	; R = X * P with X the one-word buffer holding m
	mov	qword [d75X], 0
	mov	rax, [d75m]
	mov	[d75X], rax		; X's word 0: m (X doubles as a one-word
					; buffer here; the division rezeros it)
	lea	rdi, [d75R]
	lea	rsi, [d75X]
	mov	rdx, 1
	lea	rcx, [d75P]
	mov	r8, [d75nP]
	call	dec754_mul
	; the product's OWN word count, or stale words poison the bit-length
	; scan -- the same discipline as pow10's copy-backs
	mov	rax, [d75nP]
	inc	rax			; 1 + nP
	lea	rdi, [d75R]
	mov	rsi, rax
	call	dec754_blw
	mov	[d75L], rax
	mov	rcx, [d75p]
	add	rcx, 3
	cmp	rax, rcx
	jbe	.mexact
	; d = blN - (p+3): Q = the top p+3 bits, and the scale is NEGATIVE
	; -- the value is the integer N = Q * 2^d, i.e. Q * 2^-s with s = -d.
	; Getting this sign wrong flips the exponent by 2d and the fixture
	; would catch it at the first multiply-path row; writing it right the
	; first time is cheaper
	sub	rax, rcx
	mov	[d75k], rax		; d
	neg	rax
	mov	[d75s], rax		; s = -d
	lea	rdi, [d75R]
	mov	rsi, [d75k]
	call	dec754_top
	mov	[d75Q], rax
	lea	rdi, [d75R]
	mov	rsi, [d75k]
	call	dec754_orlow
	mov	[d75stk], rax
	jmp	.core
  .mexact:
	; N fits in p+3 bits: it is its own candidate, exact, scale zero
	mov	rax, [d75R]
	mov	[d75Q], rax
	mov	qword [d75s], 0
	mov	qword [d75stk], 0
	jmp	.core

  .divide:
	; ---- e < 0: the value is m / 10^|e| ---------------------------------
	mov	rcx, [d75e]
	neg	rcx
	mov	[d75k], rcx
	lea	rdi, [d75P]
	mov	rsi, rcx
	call	dec754_pow10
	lea	rdi, [d75P]
	mov	rsi, D75_WMAX
	call	dec754_blw
	mov	[d75blp], rax
	add	rax, 63
	shr	rax, 6
	mov	[d75nP], rax
	; k, the numerator's binary scaling, chosen so the quotient comes out
	; at p+3 bits when the mantissa is short enough for that -- and
	; CLAMPED AT ZERO when it is not: a fifteen-digit m against a small
	; power of ten has an integer part that already overflows p+3 bits,
	; and the quotient then fits a u64 anyway (under 2^(p+4) when k > 0,
	; under m's own fifty when k = 0). blm + k = blP + p + 3 whenever
	; k > 0, so the numerator never exceeds 1180 bits, nineteen words,
	; whatever m is.
	mov	rdi, [d75m]
	call	dec754_bl64
	mov	rcx, [d75blp]
	add	rcx, [d75p]
	add	rcx, 3
	sub	rcx, rax		; k = blP + p + 3 - blm
	jns	.kpos
	xor	ecx, ecx		; clamp
  .kpos:
	mov	[d75blp], rcx		; k (blP dead from here; the slot recycles)
	lea	rdi, [d75X]
	mov	rsi, [d75m]
	mov	rdx, rcx
	call	dec754_shl
	; R = 0 over the WHOLE buffer: the loop below writes only the words
	; it reads back, so stale bytes from the multiply path's use of this
	; slot -- or plain stack garbage -- must not survive to the first
	; iteration's spill word
	lea	rdi, [d75R]
	mov	ecx, D75_WMAX
	xor	eax, eax
	cld
	rep	stosq
	lea	r12, [d75X]		; the numerator
	lea	r13, [d75P]		; the divisor
	; t = the numerator's top bit index, blm + k - 1
	mov	rax, [d75blp]
	mov	rcx, [d75m]
	bsr	rcx, rcx
	inc	rcx
	add	rax, rcx
	dec	rax
	mov	[d75t], rax
	; the quotient accumulates in the u64 (see .dcol); Q starts empty
	mov	qword [d75Q], 0

  .dstep:
	; the next numerator bit: R = (R << 1) | bit over the nP+1 words.
	; Per word i, top-down: R'[i] = (old R[i] << 1) | (old R[i-1] >> 63),
	; and at i = 0 the numerator bit joins AFTER the loop, into R[0]'s
	; new low bit. Descending order means old R[i] and old R[i-1] are
	; both intact when word i is computed -- the whole reason the loop
	; runs backwards. The spill word R[nP] is always ZERO at loop entry
	; (a subtraction's borrow zeroes it; a skipped subtraction means it
	; was zero), so nothing above it is ever lost.
	mov	rax, [d75t]
	mov	rcx, rax
	and	rcx, 63
	shr	rax, 6
	mov	rax, [r12 + rax*8]
	shr	rax, cl			; cl: x86 variable shifts take cl only
	and	eax, 1
	mov	[d75k], rax		; the bit, saved across the shift loop
	; the i = 0 case IS an iteration of the loop, not an epilogue: the
	; FIRST draft left u = nP + 1 and controlled it with dec/jnz, which
	; exits at u = 0 BEFORE the body runs -- word 0 was never doubled,
	; R[0] only ever accumulated the OR'd numerator bits, every compare
	; saw "less", and the quotient came out identically zero. jns (which
	; .dcmp below already used for the same shape) lets u = 0 run its
	; body first
	mov	rcx, [d75nP]
	mov	[d75u], rcx
  .dsh:
	mov	rcx, [d75u]
	mov	rax, [d75R + rcx*8]	; old R[i]
	shl	rax, 1
	test	rcx, rcx
	jz	.dlo
	mov	rdx, [d75R + rcx*8 - 8]	; old R[i-1]
	shr	rdx, 63
	or	rax, rdx
  .dlo:
	mov	[d75R + rcx*8], rax
	dec	qword [d75u]
	jns	.dsh
	mov	rax, [d75k]
	or	[d75R], rax
	; compare R against P. The spill word first: nonzero means the
	; doubled remainder has P's whole weight above it -- subtract with
	; no further comparison
	mov	rcx, [d75nP]
	mov	rax, [d75R + rcx*8]
	test	rax, rax
	jnz	.dsub
	mov	rcx, [d75nP]
	dec	rcx
	mov	[d75u], rcx
  .dcmp:
	mov	rcx, [d75u]
	mov	rax, [d75R + rcx*8]
	mov	rdx, [r13 + rcx*8]
	cmp	rax, rdx
	ja	.dsub
	jb	.dq0
	dec	rcx
	mov	[d75u], rcx
	jns	.dcmp
	jmp	.dsub		; every word equal: R == P, and the subtraction
			; leaves the exact zero remainder
  .dsub:
	; R -= P over the nP low words, an sbb chain; the chain lives on CF, so
	; NOTHING between the sbb's may write it: not cmp (proc.inc's epilogue
	; records the same law for its sub), and not sub either -- the first
	; draft computed the word index as `sub rcx,[d75u]`, whose own borrow
	; replaced each word's carry-out and corrupted every word but the
	; first. The index therefore lives in rcx itself, ascending via `inc`
	; (which, like dec, leaves CF alone), and the loop is controlled by a
	; dec/jnz on a count slot alone. The final borrow comes out of the
	; spill word with a second sbb, which is what zeroes it for the next
	; .dstep
	clc
	xor	ecx, ecx		; the word index, ascending: CF untouched
	mov	rax, [d75nP]
	mov	[d75u], rax
  .dsubl:
	mov	rax, [d75R + rcx*8]
	sbb	rax, [r13 + rcx*8]
	mov	[d75R + rcx*8], rax
	inc	rcx
	dec	qword [d75u]
	jnz	.dsubl
	mov	rcx, [d75nP]
	mov	rax, [d75R + rcx*8]
	sbb	rax, 0
	mov	[d75R + rcx*8], rax
	mov	qword [d75k], 1	; the quotient bit
	jmp	.dcol
  .dq0:
	mov	qword [d75k], 0
  .dcol:
	; collect the quotient bits at scale 2^63 .. 2^0 into the u64, which
	; by the bound in the .divide header holds the ENTIRE quotient (it
	; is under 2^57): every bit at scale 2^64 and above is zero. The
	; first draft instead collected the FIRST sixty-four bits, from the
	; numerator's top down -- a window anchored to the wrong end: with a
	; 66-bit numerator it swallowed the quotient's leading zeros and let
	; the two lowest, significant bits fall out the bottom (1e-3 came out
	; a factor of four low). Anchoring at the scale, not the iteration
	; count, is what makes the collection independent of how long the
	; numerator is
	mov	rax, [d75t]
	cmp	rax, 64
	jae	.dnext
	shl	qword [d75Q], 1
	mov	rax, [d75k]
	or	[d75Q], rax
  .dnext:
	mov	rax, [d75t]
	test	rax, rax
	jz	.dlast
	dec	rax
	mov	[d75t], rax
	jmp	.dstep
  .dlast:
	; the numerator's last bit is consumed: the remainder in R is exact,
	; and "nonzero" is the candidate's entire error -- the value lies
	; strictly between Q*2^-s and (Q+1)*2^-s iff it is
	lea	rdi, [d75R]
	mov	rsi, [d75nP]
	inc	rsi
	shl	rsi, 6
	call	dec754_orlow
	mov	[d75stk], rax
	; the drop step: Q holds floor(m * 2^k / P) ENTIRE. Reduce it to the
	; top p+3 bits with the dropped quotient bits folded into sticky,
	; and set the scale s = k - d: the value is
	;     (Q >> d) * 2^(d-k)  +  a positive error below 2^(d-k),
	; exactly the candidate contract the core expects. When the quotient
	; is p+3 bits or fewer, d = 0 and s = k -- the original design and
	; the common case; the clamp path is what made the general form
	; necessary.
	; bl64 takes the VALUE, not a pointer: the first draft passed `lea
	; rdi,[d75Q]` -- the SLOT'S ADDRESS, whose bit length (~47, an rbp
	; displacement) is a property of the stack layout, not of the data.
	; f64 hid the bug (p+3 = 56 admits a ~47-bit "length", so .dkeep
	; ran and the quotient went through intact); f32 exposed it (p+3 =
	; 27, so the drop step ran on a fictional length and truncated the
	; quotient to its top bits -- 0.1 came out 0x3DCC0000). The address
	; is also what broke determinism's spirit: the SAME input would
	; convert differently under a different frame layout
	mov	rdi, [d75Q]
	call	dec754_bl64
	mov	rcx, [d75p]
	add	rcx, 3
	cmp	rax, rcx
	jbe	.dkeep
	sub	rax, rcx
	mov	[d75t], rax		; d (t is dead: the loop is over)
	mov	rcx, rax
	mov	rax, 1
	shl	rax, cl
	dec	rax
	and	rax, [d75Q]		; the dropped low bits
	test	rax, rax
	jz	.dnodrop
	mov	qword [d75stk], 1
  .dnodrop:
	mov	rcx, [d75t]
	mov	rax, [d75Q]
	shr	rax, cl
	mov	[d75Q], rax
	mov	rcx, [d75blp]		; k
	sub	rcx, [d75t]		; - d
	mov	[d75s], rcx
	jmp	.core
  .dkeep:
	mov	rax, [d75blp]		; k
	mov	[d75s], rax
	jmp	.core
  .core:
	; ---- the shared rounding core ---------------------------------------
	mov	rax, [d75Q]
	rassert	rax ne 0
	bsr	rcx, rax
	inc	rcx
	mov	[d75L], rcx		; L = bitlen(Q)
	mov	rax, rcx
	dec	rax
	sub	rax, [d75s]
	mov	[d75e2], rax		; e2 = L - 1 - s
	mov	rcx, [d75emin]
	cmp	rax, rcx
	jl	.subnormal
	jmp	.normal

  .normal:
	; kept = Q >> (L - p) when the candidate is longer than p, exact and
	; left-shifted when shorter; guard the next bit down, sticky
	; everything below it (plus the division's remainder bit)
	mov	rax, [d75L]
	sub	rax, [d75p]
	mov	[d75k], rax		; shift = L - p, may be <= 0
	test	rax, rax
	jle	.nexact
	; kept
	mov	rcx, rax
	mov	rdx, [d75Q]
	shr	rdx, cl
	mov	[d75t], rdx		; kept
	; guard
	dec	rcx
	mov	rax, [d75Q]
	shr	rax, cl
	and	eax, 1
	mov	[d75g], rax
	; sticky: the mask of everything below the guard bit
	mov	rax, 1
	mov	rcx, [d75k]
	dec	rcx
	shl	rax, cl
	dec	rax
	and	rax, [d75Q]
	test	rax, rax
	setne	al
	movzx	eax, al
	mov	[d75u], rax
	jmp	.nround
  .nexact:
	; L <= p: the candidate fits in the significand with room to spare
	mov	rcx, [d75p]
	sub	rcx, [d75L]
	mov	rax, [d75Q]
	shl	rax, cl
	mov	[d75t], rax		; kept, exact
	mov	qword [d75g], 0
	mov	qword [d75u], 0
  .nround:
	; round up iff guard && (sticky || kept's low bit)
	mov	rax, [d75t]
	mov	rcx, [d75g]
	test	rcx, rcx
	jz	.nkeep
	mov	rcx, [d75u]
	or	rcx, [d75stk]
	test	rcx, rcx
	jnz	.nup
	mov	rax, [d75t]
	test	rax, 1
	jnz	.nup
  .nkeep:
	mov	rax, [d75t]
	jmp	.nemit
  .nup:
	mov	rax, [d75t]
	inc	rax
	; carry-out: kept == 2^p means the rounding carried all the way up
	; -- the significand renormalizes to 2^(p-1) and the exponent climbs
	mov	rcx, [d75p]
	mov	rdx, 1
	shl	rdx, cl
	cmp	rax, rdx
	jne	.nemit
	mov	rcx, [d75p]
	dec	rcx
	mov	rdx, 1
	shl	rdx, cl
	mov	rax, rdx
	inc	qword [d75e2]
  .nemit:
	mov	[d75t], rax		; kept, rounded
	mov	rcx, [d75emax]
	cmp	[d75e2], rcx
	jg	.retinf
	; bits = ((e2 + bias) << (p-1)) | (kept & (2^(p-1) - 1))
	mov	rax, [d75e2]
	add	rax, [d75bias]
	mov	rcx, [d75p]
	dec	rcx
	shl	rax, cl
	mov	rdx, 1
	shl	rdx, cl
	dec	rdx
	and	rdx, [d75t]
	or	rax, rdx
	return

  .subnormal:
	; ---- e2 < emin: the subnormal encoding -------------------------------
	; d = s + emin - p + 1: the bits to drop so the survivor's low bit
	; weighs 2^(emin - p + 1). d <= 0 cannot happen (e2 = L-1-s < emin
	; with L >= 27 forces s > emin - p + 1); asserted, not branched on.
	mov	rax, [d75s]
	mov	rcx, [d75emin]
	add	rax, rcx
	sub	rax, [d75p]
	inc	rax
	mov	[d75k], rax
	rassert	rax gt 0
	cmp	rax, 64
	jl	.sgo
	; every significant bit dropped: rounds to zero. The cuts leave only
	; near-half cases this deep, and half-even rounds down at even -- but
	; the general rule below would need a wider shift than 64 to decide,
	; so the shallow answer is taken here: sticky says "there was
	; something", and the nearest even at this depth is zero when the
	; guard is the candidate's top bit and Q < 2^(d-1) held. Q's top bits
	; ARE below 2^(d-1) when L - 1 - s < emin and d = s + emin - p + 1:
	; L - 1 - s <= emin - 1 means L <= s + emin = d + p - 2 < d + 64 for
	; every p, so Q's bit length is below d and the kept part is zero.
	; Guard, then, is Q's bit d-1, which is above L -- zero. Guard 0:
	; round down, zero. Exact zero mantissa with e2 below range is the
	; only representable answer and IEEE-754 requires it.
	jmp	.ret0
  .sgo:
	; kept = Q >> d, guard = Q's bit d-1, sticky the rest plus the
	; division's remainder bit
	mov	rcx, [d75k]
	mov	rax, [d75Q]
	shr	rax, cl
	mov	[d75t], rax		; kept
	mov	rax, [d75k]
	dec	rax
	mov	rcx, rax
	mov	rax, [d75Q]
	shr	rax, cl
	and	eax, 1
	mov	[d75g], rax		; guard
	mov	rax, 1
	mov	rcx, [d75k]
	dec	rcx
	shl	rax, cl
	dec	rax
	and	rax, [d75Q]
	test	rax, rax
	setne	al
	movzx	eax, al
	mov	[d75u], rax		; sticky from Q's low bits
	; round up iff guard && (sticky || kept's low bit) -- the same law
	; as the normal path
	mov	rcx, [d75g]
	test	rcx, rcx
	jz	.skeep
	mov	rcx, [d75u]
	or	rcx, [d75stk]
	test	rcx, rcx
	jnz	.sup
	mov	rax, [d75t]
	test	rax, 1
	jnz	.sup
  .skeep:
	mov	rax, [d75t]
	jmp	.semit
  .sup:
	mov	rax, [d75t]
	inc	rax
	mov	[d75t], rax
	; kept == 2^(p-1) after a round-up is the smallest NORMAL: exponent
	; field 1, mantissa 0 -- assembled explicitly, never flowed out of
	; the exponent arithmetic (the header's rule)
	mov	rcx, [d75p]
	dec	rcx
	mov	rdx, 1
	shl	rdx, cl
	cmp	rax, rdx
	jne	.semit
	mov	rax, rdx
	return
  .semit:
	; subnormal: exponent field 0, mantissa = kept
	mov	rax, [d75t]
	return

  .ret0:
	xor	eax, eax
	return
  .retinf:
	mov	rax, [d75inf]
	return
endp