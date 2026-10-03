/* tests/c/exsrt_shim.c -- the differential test's stand-in for a runtime.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 DeMoD LLC.
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
 *   tests/ir/emit_ir.asm fixes, over the records interface.inc lays out --
 *   and the eight of ADR 0017's `archivum` surface (`exsrt_mundus_archivum`,
 *   `exsrt_directorium_*`, `exsrt_lectorium_exlege_octeto`,
 *   `exsrt_scriptorium_inscribe*`), which make the same openat2 calls with
 *   the same four `open_how` values as compiler/x86_64/prelude/archivum.asm
 *   and write the same `eventus` bytes, so the tests/programs/archivum_ dirs hold
 *   the C backend to the reference on them.
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

/* `syscall(2)` and `O_PATH` are not ISO C; the archivum routines below need
 * both (glibc exposes neither under plain -std=c11). */
#define _GNU_SOURCE 1
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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

/* ---- `archivum` beneath a root (ADR 0017 stage 2) ---------------------- */
/* compiler/x86_64/prelude/archivum.asm, routine for routine, hosted: the
 * same refusals before any syscall (a relative root, an interior NUL, 4096
 * bytes or more, a negative directory descriptor), the same four `open_how`
 * values (<fcntl.h>'s names, not archivum_rodata.asm's x86-64 numbers, so
 * the shim stays right wherever it is compiled), the same 16 EAGAIN retries
 * after the first try, the same fstat refusal of a non-regular file, and the
 * same `eventus` bytes: tag at 0 (`prosperum` 0, `adversum` 1), payload at 1
 * (interface.inc part A). No fallback to openat, as in the reference. */
#ifndef SYS_openat2
# define SYS_openat2 437      /* every Linux ABI, 5.6 on */
#endif
#define EXS_RESOLVE_NO_XDEV       0x01u
#define EXS_RESOLVE_NO_MAGICLINKS 0x02u
#define EXS_RESOLVE_BENEATH       0x08u
#define EXS_RESOLVE_SUB (EXS_RESOLVE_BENEATH | EXS_RESOLVE_NO_MAGICLINKS | EXS_RESOLVE_NO_XDEV)
#define EXS_VIA_MAX 4096u
#define EXS_ITERUM 16

/* Directorium, Lectorium, Scriptorium: `Scriptor`'s record. */
typedef struct { void *a; int32_t descriptor; int32_t pad; } ExsManubrium;
_Static_assert(sizeof(ExsManubrium) == 16, "shim: a handle is 16 bytes");
_Static_assert(offsetof(ExsManubrium, descriptor) == 8, "shim: descriptor at 8");

static void exs_adversum(unsigned char *ret, long err)
{
    uint16_t n = (uint16_t)err;
    ret[0] = 1;
    memcpy(ret + 1, &n, sizeof n);
}

static void exs_manubrium(unsigned char *ret, void *a, long r)
{
    ExsManubrium m;
    if (r < 0) { exs_adversum(ret, -r); return; }
    m.a = a;
    m.descriptor = (int32_t)r;
    m.pad = 0;
    ret[0] = 0;
    memcpy(ret + 1, &m, sizeof m);
}

static void exs_mensura(unsigned char *ret, long r)
{
    uint64_t n;
    if (r < 0) { exs_adversum(ret, -r); return; }
    n = (uint64_t)r;
    ret[0] = 0;
    memcpy(ret + 1, &n, sizeof n);
}

/* The textus into a NUL-terminated buffer: 0, -ENAMETOOLONG or -EINVAL. */
static long exs_via(const unsigned char *tp, char *buf)
{
    ExsTextus t;
    memcpy(&t, tp, sizeof t);
    if (t.len >= EXS_VIA_MAX) return -ENAMETOOLONG;
    if (t.len != 0) {
        if (memchr(t.ptr, 0, (size_t)t.len) != NULL) return -EINVAL;
        memcpy(buf, t.ptr, (size_t)t.len);
    }
    buf[t.len] = 0;
    return 0;
}

static long exs_openat2(int dirfd, const char *p, uint64_t flags, uint64_t mode,
                        uint64_t resolve)
{
    struct { uint64_t flags, mode, resolve; } how;
    int i;
    _Static_assert(sizeof how == 24, "shim: open_how VER0 is 24 bytes");
    how.flags = flags;
    how.mode = mode;
    how.resolve = resolve;
    for (i = 0; i <= EXS_ITERUM; i++) {
        long r = syscall(SYS_openat2, dirfd, p, &how, sizeof how);
        if (r >= 0) return r;
        if (errno != EAGAIN) return -errno;
    }
    return -EAGAIN;
}

/* exsrt_mundus_archivum(m: ptr) -> ptr -- an opaque token, the reference's
 * choice: the Mundus record's own address. Nothing dereferences it. */
unsigned char *exsrt_mundus_archivum(unsigned char *m)
{
    return m;
}

void exsrt_directorium_ad_radicem(unsigned char *ret, unsigned char *a,
                                  unsigned char *via)
{
    char buf[EXS_VIA_MAX];
    long r = exs_via(via, buf);
    if (r == 0) {
        if (buf[0] != '/') r = -EINVAL;
        else r = exs_openat2(AT_FDCWD, buf, O_PATH | O_DIRECTORY | O_CLOEXEC, 0,
                             EXS_RESOLVE_NO_MAGICLINKS);
    }
    exs_manubrium(ret, a, r);
}

/* The three opens beneath a root: `kind` 0 infra, 1 lege_ex, 2 crea. */
static void exs_infra(unsigned char *ret, unsigned char *dp, unsigned char *via,
                      int kind)
{
    ExsManubrium d;
    char buf[EXS_VIA_MAX];
    long r;
    memcpy(&d, dp, sizeof d);
    if (d.descriptor < 0) { exs_manubrium(ret, d.a, -EBADF); return; }
    r = exs_via(via, buf);
    if (r == 0) {
        if (kind == 0)
            r = exs_openat2(d.descriptor, buf, O_PATH | O_DIRECTORY | O_CLOEXEC,
                            0, EXS_RESOLVE_SUB);
        else if (kind == 1)
            r = exs_openat2(d.descriptor, buf,
                            O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC, 0,
                            EXS_RESOLVE_SUB);
        else
            r = exs_openat2(d.descriptor, buf,
                            O_WRONLY | O_CREAT | O_EXCL | O_NOCTTY | O_CLOEXEC,
                            0600, EXS_RESOLVE_SUB);
    }
    if (kind == 1 && r >= 0) {
        struct stat st;
        if (fstat((int)r, &st) != 0) {
            long e = -errno;
            close((int)r);
            r = e;
        } else if (!S_ISREG(st.st_mode)) {
            close((int)r);
            r = -EINVAL;
        }
    }
    exs_manubrium(ret, d.a, r);
}

void exsrt_directorium_infra(unsigned char *ret, unsigned char *d, unsigned char *via)
{
    exs_infra(ret, d, via, 0);
}

void exsrt_directorium_lege_ex(unsigned char *ret, unsigned char *d, unsigned char *via)
{
    exs_infra(ret, d, via, 1);
}

void exsrt_directorium_crea(unsigned char *ret, unsigned char *d, unsigned char *via)
{
    exs_infra(ret, d, via, 2);
}

/* exsrt_lectorium_exlege_octeto(ret: ptr, r: ptr) -> void --
 * eventus<u16, erratum>: the byte, or 256 at end of file, or adversum. */
void exsrt_lectorium_exlege_octeto(unsigned char *ret, unsigned char *rp)
{
    ExsManubrium r;
    unsigned char one;
    memcpy(&r, rp, sizeof r);
    for (;;) {
        ssize_t n = read(r.descriptor, &one, 1);
        uint16_t v;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { exs_adversum(ret, errno); return; }
        v = (n == 1) ? (uint16_t)one : (uint16_t)256;
        ret[0] = 0;
        memcpy(ret + 1, &v, sizeof v);
        return;
    }
}

/* Every byte or the errno that stopped it -- never a short count. */
static long exs_inscribe(int fd, const unsigned char *p, uint64_t len)
{
    uint64_t remaining = len;
    while (remaining != 0) {
        ssize_t w = write(fd, p, (size_t)remaining);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        p += w;
        remaining -= (uint64_t)w;
    }
    return (long)len;
}

void exsrt_scriptorium_inscribe(unsigned char *ret, unsigned char *wp, unsigned char *tp)
{
    ExsManubrium w;
    ExsTextus t;
    memcpy(&w, wp, sizeof w);
    memcpy(&t, tp, sizeof t);
    exs_mensura(ret, exs_inscribe(w.descriptor, t.ptr, t.len));
}

void exsrt_scriptorium_inscribe_octeto(unsigned char *ret, unsigned char *wp, uint64_t b)
{
    ExsManubrium w;
    unsigned char one = (unsigned char)(b & 0xFFu);
    memcpy(&w, wp, sizeof w);
    exs_mensura(ret, exs_inscribe(w.descriptor, &one, 1));
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
