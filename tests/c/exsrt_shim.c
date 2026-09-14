/* tests/c/exsrt_shim.c -- the differential test's stand-in for a runtime.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Exsecutor authors.
 *
 * VERIFICATION-ONLY. This is NOT a C prelude and is no part of any milestone
 * that ships (docs/design/c-backend.md D1: whole-program mode, with a
 * freestanding C prelude and raw syscalls per architecture, is a later
 * milestone and is not designed). It is hosted, uses libc freely, and exists
 * so that a translation unit emitted by `--emitte c` in LIBRARY MODE can be
 * linked into something runnable and compared against the reference
 * backend's binary on D6's three observables.
 *
 * WHAT IT DEFINES
 *
 *   exsrt_abortus(kind)   the trap hook the emitted unit imports -- the ONE
 *                         symbol library mode requires of its host (D1).
 *                         Writes `exsecutor: abortus N` to fd 2 and then
 *                         __builtin_trap()s, which is `ud2` on x86-64 and so
 *                         SIGILL: exactly the shape tests/run.sh's
 *                         `check_run` already checks for the reference
 *                         (`signal 4` plus that line), so the same key
 *                         reads both backends.
 *
 *   six of the ten prelude routines -- the {Mundus, ambitus} closure that
 *   tests/ir/emit_ir.asm fixes, over the records interface.inc lays out.
 *
 *   main(), which calls exs_initium and returns its value.
 *
 * WHAT IT DELIBERATELY DOES NOT DEFINE: the four `exsrt_alloc_*` routines.
 * A fixture that reaches one fails to LINK, which is the same statement
 * emit_ir.asm makes by fixing the closure at {Mundus, ambitus} -- a missing
 * routine must be a loud failure, not a silently different program.
 *
 * THE LAYOUT FACTS BELOW ARE A SECOND COPY of compiler/x86_64/prelude/
 * interface.inc's part A. That is runtime.md hazard H4 (two copies of one
 * layout, kept in step by hand) and is recorded as such: c-backend.md's open
 * question 5 asks whether a generated header is worth its lines for six
 * numbers, and C1's answer is "not yet, but this comment is the receipt".
 * The _Static_asserts below at least pin the sizes the emitted `slot`s use.
 */

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* ---- interface.inc part A, mirrored ------------------------------------ */
/* ExsAmbitus: in/out/err at 0/4/8 (32-bit descriptors), argc 16, argv 24,
 * envp 32, size 40. */
typedef struct {
    int32_t  in;        /* 0  */
    int32_t  out;       /* 4  */
    int32_t  err;       /* 8  */
    int32_t  pad;       /* 12 -- interface.inc jumps 8 -> 16, so 12 is hole */
    uint64_t argc;      /* 16 */
    char   **argv;      /* 24 */
    char   **envp;      /* 32 */
} ExsAmbitus;           /* 40 */
_Static_assert(sizeof(ExsAmbitus) == 40, "shim: ExsAmbitus is 40 bytes");
_Static_assert(offsetof(ExsAmbitus, out) == 4, "shim: ambitus.out at 4");
_Static_assert(offsetof(ExsAmbitus, err) == 8, "shim: ambitus.err at 8");
_Static_assert(offsetof(ExsAmbitus, argc) == 16, "shim: ambitus.argc at 16");
_Static_assert(offsetof(ExsAmbitus, envp) == 32, "shim: ambitus.envp at 32");

/* Scriptor and Lector: `a` at 0, descriptor at 8, size 16 -- byte for byte
 * the same shape, interface.inc says so explicitly. */
typedef struct { void *a; int32_t descriptor; int32_t pad; } ExsScriptor;
typedef struct { void *a; int32_t descriptor; int32_t pad; } ExsLector;
_Static_assert(sizeof(ExsScriptor) == 16, "shim: Scriptor is 16 bytes");
_Static_assert(sizeof(ExsLector) == 16, "shim: Lector is 16 bytes");
_Static_assert(offsetof(ExsScriptor, descriptor) == 8, "shim: descriptor at 8");

/* textus: { ptr, len }, 16 bytes. */
typedef struct { unsigned char *ptr; uint64_t len; } ExsTextus;
_Static_assert(sizeof(ExsTextus) == 16, "shim: textus is 16 bytes");
_Static_assert(offsetof(ExsTextus, len) == 8, "shim: textus.len at 8");

/* ---- the trap hook ----------------------------------------------------- */
/* prelude/README.md: "One shape for every runtime abort: the line
 * `exsecutor: abortus N` on fd 2". The kinds are 1 numeric/chk, 2 and 3 ARC,
 * 4 arena, 5 `terminus`. The English before it is not promised and is not
 * compared (D6); the line is. */
_Noreturn void exsrt_abortus(unsigned kind);
_Noreturn void exsrt_abortus(unsigned kind)
{
    char buf[64];
    int n = snprintf(buf, sizeof buf, "exsecutor: abortus %u\n", kind);
    if (n > 0) {
        ssize_t ignored = write(2, buf, (size_t)n);
        (void)ignored;
    }
    __builtin_trap();     /* ud2 on x86-64 -> SIGILL, `signal 4` to check_run */
}

/* ---- the {Mundus, ambitus} closure ------------------------------------- */
/* The emitted unit reaches these by their bare names: a BODILESS IR
 * declaration keeps its symbol verbatim, with no `exs_` prefix, because it
 * names something the LINKER must find (D5). */

static ExsAmbitus g_ambitus;
static char *g_argv[1] = { NULL };
static char *g_envp[1] = { NULL };

/* exsrt_mundus_ambitus(m: ptr) -> ptr.
 * The reference derives argc/argv/envp from the initial rsp it stashed in
 * Mundus. There is no such thing here and nothing in tests/ir/ reads them,
 * so they are a pinned empty vector: deterministic, and a fixture that
 * starts reading argv will see it and can then be given a real one. */
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

/* exsrt_scriptor_ad_exitum(ret: ptr, a: ptr) -> void -- hidden return first
 * (IR 2.9). Reads the stream out of the ambitus rather than hardcoding 1,
 * exactly as the reference does. */
void exsrt_scriptor_ad_exitum(unsigned char *ret, unsigned char *a)
{
    ExsScriptor s;
    s.a = a;
    s.descriptor = ((ExsAmbitus *)a)->out;
    s.pad = 0;
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

/* exsrt_scriptor_scribe(s: ptr, t: ptr) -> u64 -- bytes actually written,
 * looping on a short write and retrying EINTR, as the reference does. */
uint64_t exsrt_scriptor_scribe(unsigned char *sp, unsigned char *tp)
{
    ExsScriptor s;
    ExsTextus t;
    memcpy(&s, sp, sizeof s);
    memcpy(&t, tp, sizeof t);
    {
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
}

/* exsrt_scriptor_scribe_octeto(s: ptr, b: u8) -> u64 -- one raw byte. */
uint64_t exsrt_scriptor_scribe_octeto(unsigned char *sp, uint64_t b)
{
    ExsScriptor s;
    unsigned char one = (unsigned char)(b & 0xFFu);
    memcpy(&s, sp, sizeof s);
    for (;;) {
        ssize_t w = write(s.descriptor, &one, 1);
        if (w == 1) return 1;
        if (w < 0 && errno == EINTR) continue;
        return 0;
    }
}

/* exsrt_lector_lege_octeto(l: ptr) -> u16 -- one raw byte, or 256 at end of
 * input: a value no byte has. EOF and error are not distinguished, which is
 * the reference's own [OPEN] and is mirrored rather than improved on. */
uint64_t exsrt_lector_lege_octeto(unsigned char *lp)
{
    ExsLector l;
    unsigned char one;
    memcpy(&l, lp, sizeof l);
    for (;;) {
        ssize_t r = read(l.descriptor, &one, 1);
        if (r == 1) return one;
        if (r < 0 && errno == EINTR) continue;
        return 256;
    }
}

/* ---- the entry point --------------------------------------------------- */
/* Every tests/ir/ fixture declares `functio @initium (ptr) -> u8`, so the
 * emitted definition is `uint64_t exs_initium(unsigned char *p0)` and its
 * argument is the Mundus token. The reference's Mundus carries the initial
 * rsp; nothing in library mode can, and nothing in the corpus reads it
 * except through exsrt_mundus_ambitus above, which ignores it. */
uint64_t exs_initium(unsigned char *p0);

static unsigned char g_mundus[64];

/* MXCSR: the float image the reference's own prelude pins for every program
 * it emits (program.inc's BFA_MXCSR_AD_PAREM, 0x1F80). C1 measured this
 * process STARTING at 0x1FA0 (c-backend.md D3) -- the same six masks plus
 * a stale precision flag -- and that is a different program from the one
 * the reference runs before its first float instruction: the reference
 * image has the flags CLEAR, rounding pinned to nearest-even (bits 13-14
 * zero), and FTZ and DAZ clear, which is what makes a subnormal a value
 * (spec 5.4 subnormales conservata) and a float op a non-trapping one.
 * The differential corpus can only differ on rounding, denormals or flag
 * state if the shim sets the same image, so it does -- x86-64 only, since
 * this shim is also linked for riscv64 (FPCR there is untouched: the
 * emitted text asserts nothing about it, and the reference's prelude owns
 * the same pins on its own targets). Verified-only, like everything here:
 * a shipped prelude would set this before main, not in it. */
#if defined(__x86_64__)
# include <xmmintrin.h>
#endif

int main(void)
{
#if defined(__x86_64__)
    _mm_setcsr(0x1F80u);
#endif
    return (int)(exs_initium(g_mundus) & 0xFFu);
}
