/* examples/somnium/hospes.c -- somnium's host for the C backend.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Exsecutor authors.
 *
 * `exsc --emitte c` emits a translation unit in LIBRARY MODE: pure
 * functions, no entry point, and a handful of `exsrt_*` routines it expects
 * its host to define (docs/design/c-backend.md D1). tests/c/exsrt_shim.c is
 * the differential suite's host, verification-only. This file is the same
 * contract, byte for byte in its records, written for ONE program to SHIP:
 * the screensaver engine, built for speed and for any C11 target.
 *
 * WHAT IS DIFFERENT FROM THE SHIM, AND WHY
 *
 *   Output is buffered. The reference prelude's scribe_octeto is one
 *   write(2) per byte -- 48,000 system calls a 160x100 frame, which is what
 *   bounds the reference build's frame rate. Here bytes collect in a buffer
 *   of EXACTLY one frame and go out in one write when it fills, so a frame
 *   leaves the moment its last byte is produced and never waits on the
 *   next one. The price is when a closed pipe is noticed: at the frame's
 *   flush rather than at its first byte. SIGPIPE ends the process there,
 *   exactly as it ends the reference one -- which is how the host stops a
 *   screensaver -- so what a viewer receives is the same whole frames.
 *
 *   Input is buffered too: the request is 17 bytes, but somnium 8's model
 *   is 44,801 more, one read(2) each in the reference.
 *
 * WHAT IS THE SAME, AND MUST BE
 *
 *   The records (Ambitus, Scriptor, Lector) and the abort line, as the shim
 *   and compiler/x86_64/prelude/interface.inc lay them out. MXCSR = 0x1F80
 *   before the first float instruction on x86-64 -- nearest-even, no FTZ, no
 *   DAZ -- so abyssus and signum compute the reference build's bits. Build
 *   with -ffp-contract=off and without -ffast-math for the same reason: an
 *   FMA contraction (which -march=x86-64-v3 makes available) changes the
 *   last bit of a*b+c, and the goldens under tests/programs/somnium_* are
 *   the reference build's bits.
 *
 * This file is hosted (it uses libc), so it is no part of the freestanding
 * claim spec §18.1 makes for exsc and the reference output: the fasmg build
 * of somnium keeps that claim and the syscall audit; this one trades them
 * for speed and portability, and says so.
 */

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

typedef struct {
    int32_t  in;
    int32_t  out;
    int32_t  err;
    int32_t  pad;
    uint64_t argc;
    char   **argv;
    char   **envp;
} ExsAmbitus;
_Static_assert(sizeof(ExsAmbitus) == 40, "hospes: ExsAmbitus is 40 bytes");
_Static_assert(offsetof(ExsAmbitus, argc) == 16, "hospes: ambitus.argc at 16");

typedef struct { void *a; int32_t descriptor; int32_t pad; } ExsScriptor;
typedef struct { void *a; int32_t descriptor; int32_t pad; } ExsLector;
_Static_assert(sizeof(ExsScriptor) == 16, "hospes: Scriptor is 16 bytes");
_Static_assert(offsetof(ExsScriptor, descriptor) == 8, "hospes: descriptor at 8");

typedef struct { unsigned char *ptr; uint64_t len; } ExsTextus;
_Static_assert(sizeof(ExsTextus) == 16, "hospes: textus is 16 bytes");

/* ---- the trap hook: `exsecutor: abortus N` on fd 2, then SIGILL ---------- */
_Noreturn void exsrt_abortus(unsigned kind);
_Noreturn void exsrt_abortus(unsigned kind)
{
    char buf[64];
    int n = snprintf(buf, sizeof buf, "exsecutor: abortus %u\n", kind);
    if (n > 0) {
        ssize_t ignored = write(2, buf, (size_t)n);
        (void)ignored;
    }
    __builtin_trap();
}

/* ---- buffered descriptors ---------------------------------------------- */

#define HOSPES_TABULA 48000u   /* one 160x100 rgb24 frame */

static unsigned char g_ex[HOSPES_TABULA];
static size_t g_ex_n;
static int g_ex_fd = 1;

static unsigned char g_in[65536];
static size_t g_in_pos, g_in_n;

/* Write everything pending; returns the count that did NOT go out. */
static size_t hospes_effunde(void)
{
    size_t done = 0;
    while (done < g_ex_n) {
        ssize_t w = write(g_ex_fd, g_ex + done, g_ex_n - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        done += (size_t)w;
    }
    size_t left = g_ex_n - done;
    g_ex_n = 0;
    return left;
}

/* ---- the {Mundus, ambitus} closure ------------------------------------- */

static ExsAmbitus g_ambitus;
static char *g_argv[1] = { NULL };
static char *g_envp[1] = { NULL };

unsigned char *exsrt_mundus_ambitus(unsigned char *m)
{
    (void)m;
    g_ambitus.in = 0;
    g_ambitus.out = 1;
    g_ambitus.err = 2;
    g_ambitus.argc = 0;
    g_ambitus.argv = g_argv;
    g_ambitus.envp = g_envp;
    return (unsigned char *)&g_ambitus;
}

void exsrt_scriptor_ad_exitum(unsigned char *ret, unsigned char *a)
{
    ExsScriptor s;
    s.a = a;
    s.descriptor = ((ExsAmbitus *)a)->out;
    s.pad = 0;
    g_ex_fd = s.descriptor;
    memcpy(ret, &s, sizeof s);
}

void exsrt_lector_ab_introitu(unsigned char *ret, unsigned char *a)
{
    ExsLector l;
    l.a = a;
    l.descriptor = ((ExsAmbitus *)a)->in;
    l.pad = 0;
    memcpy(ret, &l, sizeof l);
}

/* s.scribe(t): flush what is pending, then the text, as the reference
 * writes it -- looping on short writes and EINTR. somnium never calls it. */
uint64_t exsrt_scriptor_scribe(unsigned char *sp, unsigned char *tp)
{
    ExsScriptor s;
    ExsTextus t;
    memcpy(&s, sp, sizeof s);
    memcpy(&t, tp, sizeof t);
    if (hospes_effunde() != 0) return 0;
    uint64_t remaining = t.len;
    unsigned char *p = t.ptr;
    while (remaining != 0) {
        ssize_t w = write(s.descriptor, p, (size_t)remaining);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        p += w;
        remaining -= (uint64_t)w;
    }
    return t.len - remaining;
}

/* s.scribe_octeto(b): into the frame buffer; out in one write when full.
 * Returns 1, or 0 once a flush has failed -- the reference's count. */
uint64_t exsrt_scriptor_scribe_octeto(unsigned char *sp, uint64_t b)
{
    (void)sp;
    g_ex[g_ex_n++] = (unsigned char)(b & 0xFFu);
    if (g_ex_n == HOSPES_TABULA) {
        if (hospes_effunde() != 0) return 0;
    }
    return 1;
}

/* l.lege_octeto(): one byte, or 256 at end of input (or error, which the
 * reference does not distinguish either). */
uint64_t exsrt_lector_lege_octeto(unsigned char *lp)
{
    ExsLector l;
    memcpy(&l, lp, sizeof l);
    if (g_in_pos == g_in_n) {
        for (;;) {
            ssize_t r = read(l.descriptor, g_in, sizeof g_in);
            if (r > 0) {
                g_in_pos = 0;
                g_in_n = (size_t)r;
                break;
            }
            if (r < 0 && errno == EINTR) continue;
            return 256;
        }
    }
    return g_in[g_in_pos++];
}

/* ---- the entry point --------------------------------------------------- */

uint64_t exs_initium(unsigned char *p0);

static unsigned char g_mundus[64];

#if defined(__x86_64__)
# include <xmmintrin.h>
#endif

int main(void)
{
#if defined(__x86_64__)
    _mm_setcsr(0x1F80u);
#endif
    int rc = (int)(exs_initium(g_mundus) & 0xFFu);
    /* A partial frame is only ever pending when the program stopped early
     * (a refused request writes nothing at all); send it anyway, as the
     * reference would have written those bytes one at a time. */
    (void)hospes_effunde();
    return rc;
}
