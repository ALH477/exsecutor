/* tests/c/exsrt_shim_mips.c -- the cross phase's stand-in for a runtime.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Exsecutor authors.
 *
 * VERIFICATION-ONLY, exactly as exsrt_shim.c beside it is, and for the same
 * reason: whole-program mode, with a real freestanding C prelude per
 * architecture, is a later milestone and is not designed
 * (docs/design/c-backend.md D1). This exists so that a unit emitted by
 * `--emitte c --hospes mips64-none-o64` can be linked into something
 * runnable and compared against the reference backend on D6's three
 * observables. ADR 0015 decision 5.
 *
 * WHY A SECOND SHIM, rather than an `#if` arm in exsrt_shim.c:
 *
 *   exsrt_shim.c is HOSTED. It includes <unistd.h>, <string.h>, <stdio.h>
 *   and <errno.h> and calls snprintf, write, read, memcpy and errno. There
 *   is no MIPS libc in this repository's test closure and there is not going
 *   to be one -- nixpkgs' pkgsCross MIPS toolchain is not in the binary
 *   cache, so pulling a libc in would mean building GCC and newlib from
 *   source on every fresh checkout (ADR 0015 decision 4, which chose against
 *   exactly that). So this file is FREESTANDING: direct Linux syscalls
 *   through inline MIPS asm, its own memcpy and memset, and `_start`
 *   instead of `main`.
 *
 * THREE THINGS HERE ARE NOT ARBITRARY AND MUST NOT BE "TIDIED":
 *
 * 1. THE PRELUDE OFFSETS ARE LITERALS -- but NOT, as first written here,
 *    because the emitted unit reads them. MEASURED, by mutation: moving the
 *    descriptor from offset 8 to offset 4 changes nothing, and the whole
 *    cross phase still passes. The reason is that the emitted unit treats a
 *    Scriptor, a Lector and an ambitus as OPAQUE BYTES -- it allocates the
 *    slot, passes the pointer back, and never interprets a field. Only this
 *    file interprets them, and it both writes and reads every offset, so it
 *    is self-consistent at any value.
 *
 *    What IS shared with the unit is the SIZE of the slot it allocates.
 *    A shim that writes more than the slot holds overflows it -- and the
 *    cross phase does NOT catch that either: SCR_SIZE mutated from 16 to 32
 *    also passes, because there is no sanitizer on a freestanding MIPS
 *    build the way there is on the hosted x86-64 one. exsrt_shim.c's
 *    `_Static_assert`s, which run under UBSan in the differential phase,
 *    are the real guard on these numbers; the literals here are consistency
 *    and documentation, and they are written as offsets so that the numbers
 *    interface.inc fixes appear somewhere a reader can compare.
 *
 *    (ADR 0015 decision 6 originally claimed a natural C struct layout would
 *    disagree with the emitted unit at 32-bit pointers. It would differ from
 *    interface.inc -- `struct { void *a; int32_t d; int32_t pad; }` is 12
 *    bytes with the descriptor at 4 under n32, not 16 and 8 -- but nothing
 *    in the unit looks, so the disagreement is invisible. The decision is
 *    corrected in the ADR rather than quietly dropped.)
 *
 * 2. THE ABORT RAISES SIGILL DELIBERATELY. exsrt_shim.c uses
 *    __builtin_trap(), which is `ud2` on x86-64 and therefore SIGILL, and
 *    tests/run.sh's `check_run` matches on `signal 4` plus a line ending
 *    `abortus N` -- one key that reads both backends, which that file's
 *    header is rightly proud of. On MIPS __builtin_trap() is `teq $0,$0`,
 *    which raises SIGTRAP, and the key would not port. `.word 0` is a
 *    reserved instruction encoding and raises SIGILL, so the key still
 *    reads every backend and no harness learns a per-target signal.
 *
 * 3. IT MUST BE COMPILED -G0 -mno-abicalls. Without -G0 the MIPS ABI puts
 *    small objects in .sdata addressed through $gp, and $gp is set up by a
 *    crt0 that is not here: the program segfaults before reaching
 *    exs_initium. Found by running it. The build line lives in
 *    tests/run.sh's cross phase.
 *
 * WHAT IT DELIBERATELY DOES NOT DEFINE: the four exsrt_alloc_* routines, for
 * the same reason exsrt_shim.c omits them -- a fixture that reaches one must
 * fail to LINK rather than be a silently different program.
 *
 * The layout facts below are a THIRD copy of interface.inc part A (after the
 * compiler's and exsrt_shim.c's). That is runtime.md hazard H4, and this
 * comment is its receipt; unlike the other two, this copy states the
 * offsets as numbers, which is the point.
 */

#include <stdint.h>

/* n32 and n64 number their syscalls from different bases. Both are
 * big-endian MIPS-III with 64-bit registers; n32's addresses are 32-bit,
 * which is the o64 property under test. */
#if defined(_ABIN32) && defined(_MIPS_SIM) && _MIPS_SIM == _ABIN32
# define SYS_read       6000
# define SYS_write      6001
# define SYS_exit_group 6205
#else
# define SYS_read       5000
# define SYS_write      5001
# define SYS_exit_group 5205
#endif

typedef __SIZE_TYPE__ usize;

/* Linux/MIPS: $2 carries the number and the result, $4-$6 the arguments,
 * $7 is clobbered with the error flag. A negative return is -errno; the only
 * one acted on is -EINTR (-4). */
static long sys3(long n, long a, long b, long c)
{
    register long v0 __asm__("$2") = n;
    register long a0 __asm__("$4") = a;
    register long a1 __asm__("$5") = b;
    register long a2 __asm__("$6") = c;
    __asm__ volatile("syscall"
                     : "+r"(v0), "+r"(a0)
                     : "r"(a1), "r"(a2)
                     : "$7", "memory");
    return v0;
}

/* The emitted unit's only two libc imports (c-backend.md finding 13:
 * __builtin_memcpy may become a call; -ftrivial-auto-var-init, which Kiln
 * sets, produces memset). Byte at a time on purpose: this is a test shim,
 * not a runtime, and a correct slow one is what it should be. */
void *memcpy(void *d, const void *s, usize n)
{
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memset(void *d, int c, usize n)
{
    unsigned char *dp = (unsigned char *)d;
    while (n--) *dp++ = (unsigned char)c;
    return d;
}

/* ---- the trap hook ----------------------------------------------------- */
_Noreturn void exsrt_abortus(unsigned kind);
_Noreturn void exsrt_abortus(unsigned kind)
{
    char b[32];
    const char *m = "exsecutor: abortus ";
    int i = 0;
    while (m[i]) { b[i] = m[i]; i++; }
    if (kind >= 10) b[i++] = (char)('0' + kind / 10);
    b[i++] = (char)('0' + kind % 10);
    b[i++] = '\n';
    sys3(SYS_write, 2, (long)(usize)b, i);
    __asm__ volatile(".word 0");        /* reserved instruction -> SIGILL */
    for (;;) { }
}

/* ---- interface.inc part A, by literal offset --------------------------- */
#define AMB_SIZE        40
#define AMB_IN           0
#define AMB_OUT          4
#define AMB_ERR          8
#define SCR_SIZE        16
#define SCR_DESCRIPTOR   8

static unsigned char g_ambitus[AMB_SIZE];

static void put32(unsigned char *p, int32_t v) { memcpy(p, &v, 4); }
static int32_t get32(const unsigned char *p) { int32_t v; memcpy(&v, p, 4); return v; }

/* exsrt_mundus_ambitus(m: ptr) -> ptr. argc/argv/envp are a pinned empty
 * vector, as in exsrt_shim.c: deterministic, and nothing in the corpus
 * reads them. */
unsigned char *exsrt_mundus_ambitus(unsigned char *m)
{
    (void)m;
    memset(g_ambitus, 0, AMB_SIZE);
    put32(g_ambitus + AMB_IN, 0);
    put32(g_ambitus + AMB_OUT, 1);
    put32(g_ambitus + AMB_ERR, 2);
    return g_ambitus;
}

/* Both take the hidden result pointer first (IR 2.9) and read the stream out
 * of the ambitus rather than hardcoding a descriptor, as the reference does. */
static void mk(unsigned char *ret, unsigned char *a, int fdoff)
{
    memset(ret, 0, SCR_SIZE);
    memcpy(ret, &a, sizeof a);                  /* `a` at offset 0 */
    put32(ret + SCR_DESCRIPTOR, get32(a + fdoff));
}

void exsrt_scriptor_ad_exitum(unsigned char *ret, unsigned char *a) { mk(ret, a, AMB_OUT); }
void exsrt_lector_ab_introitu(unsigned char *ret, unsigned char *a) { mk(ret, a, AMB_IN); }

/* textus: { ptr, len }, 16 bytes, len at 8 -- and this one is right at BOTH
 * widths even if the C compiler chooses, since a uint64_t aligns to 8. It is
 * still read by literal offset, because relying on that coincidence is how
 * the Scriptor case above got its bug. */
#define TEX_SIZE 16
#define TEX_LEN   8

/* exsrt_scriptor_scribe(s: ptr, t: ptr) -> mensura -- bytes actually
 * written, looping on a short write and retrying EINTR, as the reference
 * does. */
uint64_t exsrt_scriptor_scribe(unsigned char *sp, unsigned char *tp)
{
    long fd = get32(sp + SCR_DESCRIPTOR);
    unsigned char *p;
    uint64_t len;
    uint64_t remaining;
    memcpy(&p, tp, sizeof p);
    memcpy(&len, tp + TEX_LEN, 8);
    remaining = len;
    while (remaining != 0) {
        long w = sys3(SYS_write, fd, (long)(usize)p, (long)remaining);
        if (w < 0) {
            if (w == -4) continue;              /* -EINTR */
            break;
        }
        p += w;
        remaining -= (uint64_t)w;
    }
    return len - remaining;
}

/* exsrt_scriptor_scribe_octeto(s: ptr, b: u8) -> mensura -- one raw byte. */
uint64_t exsrt_scriptor_scribe_octeto(unsigned char *sp, uint64_t b)
{
    unsigned char one = (unsigned char)(b & 0xFFu);
    long fd = get32(sp + SCR_DESCRIPTOR);
    for (;;) {
        long w = sys3(SYS_write, fd, (long)(usize)&one, 1);
        if (w == 1) return 1;
        if (w == -4) continue;                  /* -EINTR */
        return 0;
    }
}

/* exsrt_lector_lege_octeto(l: ptr) -> u16 -- one raw byte, or 256 at end of
 * input: a value no byte has. EOF and error are not distinguished, which is
 * the reference's own [OPEN] and is mirrored rather than improved on. */
uint64_t exsrt_lector_lege_octeto(unsigned char *lp)
{
    unsigned char one;
    long fd = get32(lp + SCR_DESCRIPTOR);
    for (;;) {
        long r = sys3(SYS_read, fd, (long)(usize)&one, 1);
        if (r == 1) return one;
        if (r == -4) continue;                  /* -EINTR */
        return 256;
    }
}

/* ---- the entry point --------------------------------------------------- */
uint64_t exs_initium(unsigned char *p0);

static unsigned char g_mundus[64];

void _start(void);
void _start(void)
{
    uint64_t rc = exs_initium(g_mundus);
    sys3(SYS_exit_group, (long)(rc & 0xFFu), 0, 0);
    __builtin_unreachable();
}
