/* prototypes/cprologue/fpcontract.c -- does the pragma actually stop a
 * contraction? Verification-only (prototypes/README.md).
 *
 * D3 row 3 asks whether `#pragma STDC FP_CONTRACT OFF` prevents `a*b+c`
 * contracting under `-O2 -ffp-contract=fast`, and ADR 0012's Open section
 * records the belief that GCC ignores it. Row 4 asks the same of
 * `#pragma GCC optimize("fp-contract=off")`.
 *
 * The witness: a*b+c and a*b are chosen so that the fused result and the
 * two-rounding result differ in the last bit. If fma(a,b,c) != a*b+c as
 * computed, the multiply-add was contracted.
 *
 * -DPRAGMA=0 no pragma; =1 STDC FP_CONTRACT OFF; =2 that plus the
 * family-specific belt (GCC: `#pragma GCC optimize("fp-contract=off")`;
 * Clang: `#pragma clang fp contract(off)`, which is Clang's own spelling and
 * which D3 row 4 did not know about -- measured here because row 4's GCC
 * spelling turns out to be a no-op under Clang).
 */

#include <stdio.h>
#include <stdint.h>

#if PRAGMA >= 1
# pragma STDC FP_CONTRACT OFF
#endif
#if PRAGMA >= 2 && defined(__clang__)
# pragma clang fp contract(off)
#elif PRAGMA >= 2 && defined(__GNUC__)
# pragma GCC optimize("fp-contract=off")
#endif

double madd(double a, double b, double c)
{
    /* PRAGMA=3: Clang's `#pragma clang fp` is documented as applying to a
     * compound statement, so the file-scope placement of PRAGMA=2 may simply
     * not be where it belongs. This is the same pragma at the top of the
     * function body, which is where a per-function emitter could put it. */
#if PRAGMA >= 3 && defined(__clang__)
# pragma clang fp contract(off)
#endif
    return a * b + c;
}

int main(void)
{
    /* (1 + 2^-30) * (1 - 2^-30) is exactly 1 - 2^-60, which rounds to 1.0.
     *   unfused: fl(a*b) + c = 1.0 - 1.0 = +0.0
     *   fused:   fl(a*b + c) = fl(-2^-60) = -2^-60, exactly representable
     * The two answers differ in every bit that matters, with no tie to
     * round away, so a zero result means the multiply-add was NOT
     * contracted and a nonzero one means it was. */
    volatile double a = 1.0 + 0x1p-30;
    volatile double b = 1.0 - 0x1p-30;
    volatile double c = -1.0;
    double r = madd(a, b, c);
    uint64_t bits;
    __builtin_memcpy(&bits, &r, 8);
    printf("PRAGMA=%d fma_isa=%d r=%.20g bits=0x%016llx contracted=%d\n",
           PRAGMA,
#if defined(__FMA__)
           1,
#else
           0,
#endif
           r, (unsigned long long)bits, r != 0.0);
    return 0;
}
