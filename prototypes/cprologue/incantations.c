/* prototypes/cprologue/incantations.c -- the C backend's prologue, run.
 *
 * Verification-only (prototypes/README.md: never shipped, never on the build
 * closure). docs/design/c-backend.md D3 lists fourteen incantations, every
 * cell [UNTESTED], and ADR 0012's own Negative section says they were
 * "stated from memory, not run". This translation unit is them, run.
 *
 * Compiled by prototypes/cprologue/run.sh under gcc and clang at -O0 and
 * -O2 with -fsanitize=undefined -fno-sanitize-recover=all. Each row that
 * needs a flag to fire is compiled again with it; the driver checks that the
 * compile fails when it must and succeeds when it must.
 *
 * -DPROBE_ROW=n selects a row that must FAIL to compile; without it, the
 * whole unit compiles and runs and prints one line per runtime row.
 */

#include <stdint.h>
#include <limits.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>   /* _Exit, for the probe's own abort hook only */

/* ---- row: the family check (not in D3's table; the prologue's line 1) --- */
#if !defined(__GNUC__) && !defined(__clang__)
# error "exsecutor: the emitted C is C11 plus a GCC/Clang extension set (ADR 0012)"
#endif

/* ---- row: __BYTE_ORDER__ and the two order macros ---------------------- */
#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__) || !defined(__ORDER_BIG_ENDIAN__)
# error "exsecutor: __BYTE_ORDER__ is required to select the nativus helpers"
#endif

/* ---- row 1: #if defined(__FAST_MATH__) --------------------------------- */
#if defined(__FAST_MATH__)
# error "PROBE: __FAST_MATH__ is defined"
#endif

/* ---- row 2: __FINITE_MATH_ONLY__ is *defined*, and its value is the test */
#if !defined(__FINITE_MATH_ONLY__)
# error "PROBE: __FINITE_MATH_ONLY__ is not defined at all (ADR 0012's #ifdef test would never fire)"
#endif
#if __FINITE_MATH_ONLY__
# error "PROBE: __FINITE_MATH_ONLY__ is 1"
#endif

/* ---- rows 3 and 4: the two fp-contract incantations -------------------- */
#pragma STDC FP_CONTRACT OFF
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC optimize("fp-contract=off")
#endif

/* ---- row 5: the two's-complement assert under -std=c11 -pedantic ------- */
_Static_assert(CHAR_BIT == 8, "exsecutor: CHAR_BIT must be 8");
_Static_assert((-1 & 3) == 3, "exsecutor: two's complement");
_Static_assert(sizeof(unsigned) * CHAR_BIT >= 32, "exsecutor: unsigned holds a byte and a shift");

/* ---- ADR 0012's <float.h> asserts (freestanding-safe, unlike
 *      __STDC_IEC_559__). Not yet in the prologue -- D3 says they enter it
 *      with the first float opcode -- but measured here as ADR 0012 asks. */
_Static_assert(FLT_RADIX == 2, "exsecutor: FLT_RADIX must be 2");
_Static_assert(FLT_MANT_DIG == 24, "exsecutor: binary32");
_Static_assert(DBL_MANT_DIG == 53, "exsecutor: binary64");

/* ---- row 6: the per-hospes pointer-width assert ------------------------ */
#if PROBE_PTR32
_Static_assert(sizeof(void *) == 4, "exsecutor: hospes mips64-none-o64 has 32-bit addresses");
#else
_Static_assert(sizeof(void *) == 8, "exsecutor: hospes x86_64-linux has 64-bit addresses");
#endif

/* ---- row 8: _Noreturn, under c11 / gnu11 / gnu2x ----------------------- */
_Noreturn void exsrt_abortus(unsigned kind);

/* ---- row 10: __has_builtin ------------------------------------------- */
#if !defined(__has_builtin)
# error "PROBE: __has_builtin is not defined"
#endif
#if !__has_builtin(__builtin_mul_overflow)
# error "PROBE: __builtin_mul_overflow is not available"
#endif
#if !__has_builtin(__builtin_add_overflow) || !__has_builtin(__builtin_sub_overflow)
# error "PROBE: __builtin_add_overflow / __builtin_sub_overflow are not available"
#endif
#if !__has_builtin(__builtin_memcpy)
# error "PROBE: __builtin_memcpy is not available"
#endif

/* ======================================================================= */
/* The exsi_* helpers of D4, verbatim, so UBSan sees exactly what the       */
/* emitter will emit (row 11).                                             */
/* ======================================================================= */

static inline uint64_t exsi_mask(unsigned n)
{ return n == 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1; }
static inline uint64_t exsi_norm_u(uint64_t x, unsigned n)
{ return x & exsi_mask(n); }
static inline uint64_t exsi_norm_i(uint64_t x, unsigned n)
{ uint64_t s = UINT64_C(1) << (n - 1); return ((x & exsi_mask(n)) ^ s) - s; }

static inline uint64_t exsi_add_u(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a + b;
  if (n == 64 ? t < a : t > exsi_mask(n)) exsrt_abortus(1);
  return t; }
static inline uint64_t exsi_add_i(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a + b;
  if (n == 64 ? ((((a ^ t) & (b ^ t)) >> 63) != 0) : exsi_norm_i(t, n) != t)
    exsrt_abortus(1);
  return t; }
static inline uint64_t exsi_sub_u(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a - b;
  if (n == 64 ? a < b : t > exsi_mask(n)) exsrt_abortus(1);
  return t; }
static inline uint64_t exsi_sub_i(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a - b;
  if (n == 64 ? ((((a ^ b) & (a ^ t)) >> 63) != 0) : exsi_norm_i(t, n) != t)
    exsrt_abortus(1);
  return t; }
static inline uint64_t exsi_mul_u(uint64_t a, uint64_t b, unsigned n)
{ if (n <= 32) { uint64_t t = a * b; if (t > exsi_mask(n)) exsrt_abortus(1); return t; }
  if (a != 0 && b > exsi_mask(n) / a) exsrt_abortus(1);
  return a * b; }
static inline uint64_t exsi_mul_i(uint64_t a, uint64_t b, unsigned n)
{ uint64_t sa = a >> 63, sb = b >> 63;
  uint64_t ma = sa ? 0 - a : a, mb = sb ? 0 - b : b;
  uint64_t neg = sa ^ sb;
  uint64_t lim = neg ? UINT64_C(1) << (n - 1) : (UINT64_C(1) << (n - 1)) - 1;
  if (ma != 0 && mb > lim / ma) exsrt_abortus(1);
  { uint64_t p = ma * mb; return neg ? 0 - p : p; } }

static inline uint64_t exsi_adds_u(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a + b; return (n == 64 ? t < a : t > exsi_mask(n)) ? exsi_mask(n) : t; }
static inline uint64_t exsi_subs_u(uint64_t a, uint64_t b, unsigned n)
{ (void)n; return a < b ? 0 : a - b; }
static inline uint64_t exsi_adds_i(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a + b, s = UINT64_C(1) << (n - 1);
  if (n == 64) return ((((a ^ t) & (b ^ t)) >> 63) != 0) ? ((a >> 63) ? s : s - 1) : t;
  return exsi_norm_i(t, n) != t ? ((t >> 63) ? exsi_norm_i(s, n) : s - 1) : t; }
static inline uint64_t exsi_subs_i(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a - b, s = UINT64_C(1) << (n - 1);
  if (n == 64) return ((((a ^ b) & (a ^ t)) >> 63) != 0) ? ((a >> 63) ? s : s - 1) : t;
  return exsi_norm_i(t, n) != t ? ((t >> 63) ? exsi_norm_i(s, n) : s - 1) : t; }

static inline uint64_t exsi_shl_u(uint64_t a, uint64_t c, unsigned n)
{ if (c >= n) exsrt_abortus(1); return exsi_norm_u(a << c, n); }
static inline uint64_t exsi_shl_i(uint64_t a, uint64_t c, unsigned n)
{ if (c >= n) exsrt_abortus(1); return exsi_norm_i(a << c, n); }
static inline uint64_t exsi_shr_u(uint64_t a, uint64_t c, unsigned n)
{ if (c >= n) exsrt_abortus(1); return a >> c; }
static inline uint64_t exsi_shr_i(uint64_t a, uint64_t c, unsigned n)
{ if (c >= n) exsrt_abortus(1);
  return c == 0 ? a : (a >> c) | ((0 - (a >> 63)) << (64 - c)); }

static inline uint64_t exsi_ld_be(const unsigned char *p, unsigned k)
{ uint64_t v = 0; unsigned i; for (i = 0; i < k; i++) v = (v << 8) | p[i]; return v; }
static inline uint64_t exsi_ld_le(const unsigned char *p, unsigned k)
{ uint64_t v = 0; unsigned i; for (i = 0; i < k; i++) v |= (uint64_t)p[i] << (8 * i); return v; }
static inline void exsi_st_be(unsigned char *p, unsigned k, uint64_t v)
{ unsigned i; for (i = 0; i < k; i++) p[i] = (unsigned char)(v >> (8 * (k - 1 - i))); }
static inline void exsi_st_le(unsigned char *p, unsigned k, uint64_t v)
{ unsigned i; for (i = 0; i < k; i++) p[i] = (unsigned char)(v >> (8 * i)); }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define exsi_ld_n exsi_ld_le
# define exsi_st_n exsi_st_le
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define exsi_ld_n exsi_ld_be
# define exsi_st_n exsi_st_be
#else
# error "exsecutor: unknown __BYTE_ORDER__"
#endif

/* ======================================================================= */

static unsigned long g_aborts;
static unsigned g_last_kind;

/* The probe's own abort hook: counts instead of trapping, so one run can
 * exercise every trapping edge. The real shim (tests/c/exsrt_shim.c) traps.
 * _Noreturn is honoured by longjmp-free means: this one never returns
 * because it exits the process only on an unexpected kind; for the probe we
 * use setjmp-free control flow -- see probe_expect_trap(). */
#include <setjmp.h>
static jmp_buf g_jb;
static int g_in_trapping_call;
void exsrt_abortus(unsigned kind)
{
    g_aborts++;
    g_last_kind = kind;
    if (g_in_trapping_call) longjmp(g_jb, 1);
    fprintf(stderr, "probe: unexpected abortus %u\n", kind);
    _Exit(70);
}

static int fails, checks;
static void ck(int cond, const char *what)
{
    checks++;
    if (!cond) { printf("FAIL %s\n", what); fails++; }
}

#define EXPECT_TRAP(expr, kindwant, what)                               \
    do {                                                                \
        unsigned long before = g_aborts;                                \
        g_in_trapping_call = 1;                                         \
        if (setjmp(g_jb) == 0) { (void)(expr); }                        \
        g_in_trapping_call = 0;                                         \
        ck(g_aborts == before + 1 && g_last_kind == (kindwant), what);   \
    } while (0)

#define EXPECT_OK(expr, want, what)                                     \
    do {                                                                \
        unsigned long before = g_aborts;                                \
        uint64_t got = 0;                                               \
        g_in_trapping_call = 1;                                         \
        if (setjmp(g_jb) == 0) { got = (expr); }                        \
        g_in_trapping_call = 0;                                         \
        ck(g_aborts == before && got == (uint64_t)(want), what);          \
    } while (0)

/* ---- row 7: _Alignas(16) on a local ----------------------------------- */
static int probe_alignas(void)
{
    _Alignas(16) unsigned char s[24];
    s[0] = 1;
    return ((uintptr_t)&s[0] % 16u) == 0 && s[0] == 1;
}

/* ---- row 9: __builtin_memcpy of 144000 bytes, no <string.h> ----------- */
static unsigned char big_src[144000];
static unsigned char big_dst[144000];
static int probe_memcpy_big(void)
{
    big_src[0] = 0x41; big_src[143999] = 0x5a;
    __builtin_memcpy(big_dst, big_src, 144000);
    return big_dst[0] == 0x41 && big_dst[143999] == 0x5a;
}

/* ---- row 12: the index idiom, in bounds ------------------------------- */
static unsigned char idx_obj[64];
static int probe_index_idiom(void)
{
    unsigned char *p = idx_obj;
    uint64_t i = 7, stride = 4;
    unsigned char *q = (unsigned char *)((uintptr_t)p + (uintptr_t)i * stride);
    *q = 0x33;
    return idx_obj[28] == 0x33;
}

/* ---- row 13: fesetround and a folded 1.0/3.0 -------------------------- */
#if PROBE_FENV
# include <fenv.h>
# pragma STDC FENV_ACCESS ON
static void probe_fenv(void)
{
    volatile double one = 1.0, three = 3.0;
    double folded = 1.0 / 3.0;            /* the compiler may fold this */
    double runtime;
    fesetround(FE_UPWARD);
    runtime = one / three;                /* volatile operands: computed now */
    fesetround(FE_TONEAREST);
    /* fold_ignored_mode=1 means the compile-time fold used round-to-nearest
     * while the runtime division under FE_UPWARD gave something else --
     * i.e. the fold did NOT respect the dynamic mode. */
    printf("fenv: folded=%.20g runtime_upward=%.20g fold_ignored_mode=%d\n",
           folded, runtime, folded != runtime);
}
#endif

/* ---- row 14: MXCSR FTZ/DAZ at start ----------------------------------- */
#if PROBE_MXCSR && (defined(__x86_64__) || defined(__i386__))
# include <xmmintrin.h>
# include <pmmintrin.h>
static void probe_mxcsr(void)
{
    unsigned csr = _mm_getcsr();
    printf("mxcsr: 0x%08x FTZ=%u DAZ=%u\n",
           csr, (csr >> 15) & 1u, (csr >> 6) & 1u);
}
#endif

int main(void)
{
    /* ---- row 11: the helpers, every trapping and non-trapping edge ---- */
    EXPECT_OK(exsi_mask(64), UINT64_MAX, "mask 64");
    EXPECT_OK(exsi_mask(1), 1, "mask 1");
    EXPECT_OK(exsi_mask(24), 0xFFFFFFu, "mask 24");
    EXPECT_OK(exsi_norm_i(0xC8, 8), UINT64_C(0xFFFFFFFFFFFFFFC8), "norm_i i8 -56");
    EXPECT_OK(exsi_norm_u(0x1FF, 8), 0xFF, "norm_u u8");

    EXPECT_OK(exsi_add_u(200, 55, 8), 255, "add_u u8 max");
    EXPECT_TRAP(exsi_add_u(200, 56, 8), 1, "add_u u8 overflow");
    EXPECT_OK(exsi_add_u(UINT64_MAX - 1, 1, 64), UINT64_MAX, "add_u u64 max");
    EXPECT_TRAP(exsi_add_u(UINT64_MAX, 1, 64), 1, "add_u u64 overflow");
    EXPECT_OK(exsi_add_i(exsi_norm_i(0x7E, 8), 1, 8), 127, "add_i i8 max");
    EXPECT_TRAP(exsi_add_i(exsi_norm_i(0x7F, 8), 1, 8), 1, "add_i i8 overflow");
    EXPECT_OK(exsi_add_i(UINT64_C(0x7FFFFFFFFFFFFFFE), 1, 64),
              UINT64_C(0x7FFFFFFFFFFFFFFF), "add_i i64 max");
    EXPECT_TRAP(exsi_add_i(UINT64_C(0x7FFFFFFFFFFFFFFF), 1, 64), 1, "add_i i64 overflow");
    EXPECT_TRAP(exsi_add_i(UINT64_C(0x8000000000000000), UINT64_MAX, 64), 1,
                "add_i i64 underflow");

    EXPECT_OK(exsi_sub_u(5, 5, 8), 0, "sub_u u8 zero");
    EXPECT_TRAP(exsi_sub_u(4, 5, 8), 1, "sub_u u8 underflow");
    EXPECT_TRAP(exsi_sub_u(4, 5, 64), 1, "sub_u u64 underflow");
    EXPECT_OK(exsi_sub_i(exsi_norm_i(0x80, 8), 0, 8), UINT64_C(0xFFFFFFFFFFFFFF80),
              "sub_i i8 min");
    EXPECT_TRAP(exsi_sub_i(exsi_norm_i(0x80, 8), 1, 8), 1, "sub_i i8 underflow");
    EXPECT_TRAP(exsi_sub_i(UINT64_C(0x8000000000000000), 1, 64), 1, "sub_i i64 underflow");

    EXPECT_OK(exsi_mul_u(15, 17, 8), 255, "mul_u u8 max");
    EXPECT_TRAP(exsi_mul_u(16, 16, 8), 1, "mul_u u8 overflow");
    EXPECT_OK(exsi_mul_u(0, 0, 64), 0, "mul_u u64 zero");
    EXPECT_OK(exsi_mul_u(UINT64_C(0xFFFFFFFF), UINT64_C(0xFFFFFFFF), 64),
              UINT64_C(0xFFFFFFFE00000001), "mul_u u64 big");
    EXPECT_TRAP(exsi_mul_u(UINT64_C(0x100000000), UINT64_C(0x100000000), 64), 1,
                "mul_u u64 overflow");
    /* width 33..63: the row that has no wider C type (finding 5) */
    EXPECT_OK(exsi_mul_u(UINT64_C(0x3FFFFFFF), 2, 48), UINT64_C(0x7FFFFFFE), "mul_u u48");
    EXPECT_TRAP(exsi_mul_u(UINT64_C(0x1000000), UINT64_C(0x1000000), 48), 1,
                "mul_u u48 overflow");
    EXPECT_OK(exsi_mul_i(exsi_norm_i(0xF8, 8), 16, 8), UINT64_C(0xFFFFFFFFFFFFFF80),
              "mul_i i8 -8*16 = -128");
    EXPECT_TRAP(exsi_mul_i(exsi_norm_i(0xF8, 8), 17, 8), 1, "mul_i i8 overflow neg");
    EXPECT_TRAP(exsi_mul_i(16, 8, 8), 1, "mul_i i8 overflow pos");
    EXPECT_OK(exsi_mul_i(UINT64_C(0xFFFFFFFF80000000), 2, 64),
              UINT64_C(0xFFFFFFFF00000000), "mul_i i64 neg");
    EXPECT_TRAP(exsi_mul_i(UINT64_C(0x8000000000000000), UINT64_MAX, 64), 1,
                "mul_i i64 INT64_MIN * -1");

    EXPECT_OK(exsi_adds_u(200, 200, 8), 255, "adds_u u8 clamp");
    EXPECT_OK(exsi_adds_u(UINT64_MAX, 5, 64), UINT64_MAX, "adds_u u64 clamp");
    EXPECT_OK(exsi_subs_u(4, 5, 8), 0, "subs_u clamp");
    EXPECT_OK(exsi_adds_i(exsi_norm_i(0x7F, 8), 1, 8), 127, "adds_i i8 clamp high");
    EXPECT_OK(exsi_adds_i(exsi_norm_i(0x80, 8), exsi_norm_i(0x80, 8), 8),
              UINT64_C(0xFFFFFFFFFFFFFF80), "adds_i i8 clamp low");
    EXPECT_OK(exsi_adds_i(UINT64_C(0x7FFFFFFFFFFFFFFF), 1, 64),
              UINT64_C(0x7FFFFFFFFFFFFFFF), "adds_i i64 clamp high");
    EXPECT_OK(exsi_adds_i(UINT64_C(0x8000000000000000), UINT64_MAX, 64),
              UINT64_C(0x8000000000000000), "adds_i i64 clamp low");
    EXPECT_OK(exsi_subs_i(exsi_norm_i(0x80, 8), 1, 8), UINT64_C(0xFFFFFFFFFFFFFF80),
              "subs_i i8 clamp low");
    EXPECT_OK(exsi_subs_i(UINT64_C(0x8000000000000000), 1, 64),
              UINT64_C(0x8000000000000000), "subs_i i64 clamp low");

    EXPECT_OK(exsi_shl_u(1, 7, 8), 128, "shl_u u8 top");
    EXPECT_TRAP(exsi_shl_u(1, 8, 8), 1, "shl_u u8 count == width");
    EXPECT_TRAP(exsi_shl_u(1, 64, 64), 1, "shl_u u64 count == width");
    EXPECT_TRAP(exsi_shl_u(1, UINT64_MAX, 64), 1, "shl_u negative count");
    EXPECT_OK(exsi_shl_i(exsi_norm_i(0x40, 8), 1, 8), UINT64_C(0xFFFFFFFFFFFFFF80),
              "shl_i i8 into sign");
    EXPECT_OK(exsi_shr_u(0xFF, 4, 8), 0x0F, "shr_u u8");
    EXPECT_OK(exsi_shr_i(UINT64_C(0xFFFFFFFFFFFFFF80), 1, 8),
              UINT64_C(0xFFFFFFFFFFFFFFC0), "shr_i i8 -128 >> 1");
    EXPECT_OK(exsi_shr_i(UINT64_C(0xFFFFFFFFFFFFFF80), 0, 8),
              UINT64_C(0xFFFFFFFFFFFFFF80), "shr_i count 0 (the 64-c guard)");
    EXPECT_OK(exsi_shr_i(UINT64_C(0x8000000000000000), 63, 64), UINT64_MAX,
              "shr_i i64 min >> 63");
    EXPECT_TRAP(exsi_shr_i(1, 64, 64), 1, "shr_i count == width");

    {
        static const unsigned char bytes[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        unsigned char out[8];
        EXPECT_OK(exsi_ld_be(bytes, 3), UINT64_C(0x010203), "ld_be 3");
        EXPECT_OK(exsi_ld_le(bytes, 3), UINT64_C(0x030201), "ld_le 3");
        EXPECT_OK(exsi_ld_be(bytes, 8), UINT64_C(0x0102030405060708), "ld_be 8");
        EXPECT_OK(exsi_ld_le(bytes, 8), UINT64_C(0x0807060504030201), "ld_le 8");
        exsi_st_be(out, 5, UINT64_C(0x1122334455));
        ck(out[0] == 0x11 && out[4] == 0x55, "st_be 5");
        exsi_st_le(out, 5, UINT64_C(0x1122334455));
        ck(out[0] == 0x55 && out[4] == 0x11, "st_le 5");
        exsi_st_be(out, 1, 0xAB);
        ck(out[0] == 0xAB, "st_be 1");
    }

    /* the loadbits/storebits idiom of D4 rows 42/43, unsigned throughout */
    {
        unsigned char b[2] = { 0xB4, 0x00 };   /* 1011 0100 */
        unsigned n = 3, bit = 1;
        unsigned m = (1u << n) - 1u, s = 8u - bit - n;
        uint64_t got = ((unsigned)b[0] >> s) & m;
        ck(got == 0x3, "loadbits u3 @bit1 of 0xB4");
        /* 0xB4 = 1011 0100; s = 4; clearing bits 6..4 and writing 101 gives
         * (0xB4 & 0x8F) | 0x50 = 0xD4. */
        b[0] = (unsigned char)(((unsigned)b[0] & ~(m << s)) | ((5u & m) << s));
        ck(b[0] == 0xD4 && b[1] == 0x00, "storebits u3 @bit1, neighbours untouched");
    }

    ck(probe_alignas(), "_Alignas(16) local, 16-aligned");
    ck(probe_memcpy_big(), "__builtin_memcpy 144000 without <string.h>");
    ck(probe_index_idiom(), "index idiom in bounds");

#if PROBE_FENV
    probe_fenv();
#endif
#if PROBE_MXCSR && (defined(__x86_64__) || defined(__i386__))
    probe_mxcsr();
#endif

    printf("checks=%d aborts=%lu fails=%d\n", checks, g_aborts, fails);
    return fails != 0;
}
