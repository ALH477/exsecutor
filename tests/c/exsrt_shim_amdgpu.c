/* tests/c/exsrt_shim_amdgpu.c -- the device phase's stand-in for a runtime.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Exsecutor authors.
 *
 * VERIFICATION-ONLY, exactly as exsrt_shim.c and exsrt_shim_mips.c beside it
 * are, and for the same reason: whole-program mode is a later milestone
 * (docs/design/c-backend.md D1). This exists so that a unit emitted by
 * `exsc aedifica --hospes x86_64-linux --emitte c` can be compiled by clang
 * for `amdgcn-amdhsa` and DISPATCHED as a kernel on a real GCN/RDNA device,
 * with its output compared against the CPU goldens (Stage 6, G2).
 *
 * WHY A THIRD SHIM, rather than another `#if` arm:
 *
 *   exsrt_shim.c is hosted; exsrt_shim_mips.c is freestanding but still does
 *   fd I/O through Linux syscalls. A DEVICE HAS NO DESCRIPTORS AT ALL. No
 *   read(2), no write(2), no fd 2 for the abort line. So this shim is
 *   BUFFER-BACKED: the host dispatch runner (tools/amd-dispatch/, G2) places
 *   an input buffer and an output buffer in device memory and passes their
 *   addresses as kernargs; `scribe` appends to the output buffer and `lege`
 *   consumes the input buffer. This is the first in-tree runtime stand-in
 *   with no file-descriptor I/O anywhere.
 *
 * FIVE THINGS HERE ARE NOT ARBITRARY AND MUST NOT BE "TIDIED":
 *
 * 1. THE KERNEL IS ONE WORKITEM BY CONSTRUCTION. The entry signature takes
 *    the buffers as ordinary kernargs and calls exs_initium once. G1's whole
 *    point is the smallest execution proof: an existing program, un-modified
 *    front end, one workitem. `quisque` -> workitems is G4; a dispatch of
 *    more than one workitem over this shim shares the buffer globals and is
 *    WRONG, silently. tools/amd-dispatch dispatches global size (1,1,1) and
 *    nothing else until G4.
 *
 * 2. DESCRIPTORS BECOME BUFFER SELECTORS. The emitted unit treats a
 *    Scriptor/Lector as opaque bytes (measured by mutation, see
 *    exsrt_shim_mips.c point 1); the only field any shim interprets is the
 *    descriptor at offset 8. Here 0 (stdin) selects the input buffer, and 1
 *    or 2 (stdout/stderr) both select the output buffer -- a device-side
 *    stderr would be a second buffer the harness has no use for; the one
 *    line that ever lands on stderr, the `abortus N` line, is written into
 *    the output buffer where the host can read it back (point 3).
 *
 * 3. THE ABORT LINE SURVIVES IN THE OUTPUT BUFFER, THEN TRAPS. On the host
 *    shim the contract is "`exsecutor: abortus N` on fd 2, then SIGILL"
 *    (prelude/README.md), and tests/run.sh's `check_run` matches it. On the
 *    device there is no fd 2, so the line goes into the output buffer at the
 *    current cursor (best effort: only if it fits whole), a sticky
 *    `abort_kind` is recorded in the result record, and __builtin_trap()
 *    fires an s_trap that makes the kfd completion status report the fault.
 *    The byte-diff gate never reads device stdout from a trapped kernel as
 *    success: the result record carries rc AND abort_kind, and the runner
 *    fails on either.
 *
 * 4. OVERFLOW IS VISIBLE, NEVER SILENT. A scribe that would pass the output
 *    capacity stores only the bytes that fit and returns the count actually
 *    stored -- the same shape as a short host write -- and records the
 *    dropped byte count in the result record. A program that checks its
 *    counts sees the short count; the runner sees dropped != 0 and fails.
 *    No wrapping, no clamping to capacity-and-pretend.
 *
 * 5. NO FLOATING-POINT STATE IS PINNED HERE. exsrt_shim.c pins MXCSR to
 *    0x1F80 on x86-64 because C1 measured the process starting at 0x1FA0.
 *    The GCN/RDNA equivalent (MODE: FP_ROUND / FP_DENORM bits, DX10_CLAMP)
 *    is NOT set by this shim: per-generation rounding/subnormal behaviour is
 *    precisely what G3 measures, and a pin written before the measurement
 *    exists would be aspiration, not evidence. G3 may conclude the compiler
 *    or the runner must program those bits; until then this file names the
 *    omission rather than hiding it.
 *
 * THE HOSPES ROW. The unit is emitted for `--hospes x86_64-linux` and
 * compiled for the device: GCN flat pointers are 64-bit and little-endian,
 * which is exactly what that row's `_Static_assert`s pin (the Stage 6 scout:
 * no new hospes row, and the prologue's byte-order helpers select
 * correctly). The struct layouts below are therefore the same 16-byte /
 * 40-byte shapes exsrt_shim.c asserts, under the same interface.inc part A;
 * this is a FOURTH copy of those layouts (runtime.md hazard H4) and this
 * comment is its receipt. The _Static_asserts run where this shim is
 * compiled and hold there.
 *
 * WHAT IT DELIBERATELY DOES NOT DEFINE: the four exsrt_alloc_* routines, for
 * the same reason both siblings omit them -- a fixture that reaches one must
 * fail to LINK rather than be a silently different program.
 */

#include <stdint.h>
#define offsetof(t, f) __builtin_offsetof(t, f)  /* freestanding: no stddef */

/* ---- freestanding necessities ------------------------------------------ */
/* The emitted unit's only libc imports (c-backend.md finding 13):
 * __builtin_memcpy may become a call and auto-var-init may become memset.
 * Byte at a time on purpose, as in exsrt_shim_mips.c. */
typedef __SIZE_TYPE__ usize;

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

/* ---- interface.inc part A, mirrored ------------------------------------ */
typedef struct {
    int32_t  in;        /* 0  */
    int32_t  out;       /* 4  */
    int32_t  err;       /* 8  */
    int32_t  pad;       /* 12 */
    uint64_t argc;      /* 16 */
    char   **argv;      /* 24 */
    char   **envp;      /* 32 */
} ExsAmbitus;           /* 40 */
_Static_assert(sizeof(ExsAmbitus) == 40, "shim: ExsAmbitus is 40 bytes");
_Static_assert(offsetof(ExsAmbitus, out) == 4, "shim: ambitus.out at 4");
_Static_assert(offsetof(ExsAmbitus, err) == 8, "shim: ambitus.err at 8");

typedef struct { void *a; int32_t descriptor; int32_t pad; } ExsScriptor;
typedef struct { void *a; int32_t descriptor; int32_t pad; } ExsLector;
_Static_assert(sizeof(ExsScriptor) == 16, "shim: Scriptor is 16 bytes");
_Static_assert(sizeof(ExsLector) == 16, "shim: Lector is 16 bytes");
_Static_assert(offsetof(ExsScriptor, descriptor) == 8, "shim: descriptor at 8");

typedef struct { unsigned char *ptr; uint64_t len; } ExsTextus;
_Static_assert(sizeof(ExsTextus) == 16, "shim: textus is 16 bytes");
_Static_assert(offsetof(ExsTextus, len) == 8, "shim: textus.len at 8");

/* ---- the buffer world --------------------------------------------------- */
/* Set by the kernel entry from its kernargs before exs_initium runs. One
 * workitem (point 1) means plain globals are correct, not racy. */
static const unsigned char *g_in;       /* input bytes                   */
static uint64_t              g_in_len;
static uint64_t              g_in_pos;
static unsigned char        *g_out;     /* output buffer                 */
static uint64_t              g_out_cap;
static uint64_t              g_out_pos; /* cursor: bytes stored          */
static uint64_t              g_dropped; /* bytes a scribe could not fit  */
static uint64_t              g_abort;   /* abort kind, 0 = no abort      */

/* The result record the host reads back after completion:
 *   [0] rc        exs_initium's return & 0xFF
 *   [1] out_len   bytes stored in the output buffer (<= capacity)
 *   [2] dropped   bytes that did not fit (point 4)
 *   [3] abort     abort kind, 0 = clean (point 3) */
static uint64_t *g_result;

/* ---- the trap hook ----------------------------------------------------- */
_Noreturn void exsrt_abortus(unsigned kind);
_Noreturn void exsrt_abortus(unsigned kind)
{
    /* The one line prelude/README.md promises, into the output buffer where
     * the host can read it (point 3). Best effort: whole or not at all. */
    static const char m[] = "exsecutor: abortus N\n";
    if (g_abort == 0) {
        char buf[sizeof m];
        uint64_t i;
        memcpy(buf, m, sizeof buf);
        buf[sizeof buf - 2] = (char)('0' + (kind % 10u));
        if (g_out_pos + sizeof buf - 1 <= g_out_cap) {
            for (i = 0; i + 1 < sizeof buf; i++) g_out[g_out_pos + i] = buf[i];
            g_out_pos += sizeof buf - 1;
        }
        g_abort = kind;
    }
    __builtin_trap();       /* s_trap on amdgcn: the runner's fault signal */
    for (;;) { }
}

/* ---- the {Mundus, ambitus} closure ------------------------------------- */
static ExsAmbitus g_ambitus;

unsigned char *exsrt_mundus_ambitus(unsigned char *m)
{
    (void)m;
    g_ambitus.in = 0;
    g_ambitus.out = 1;
    g_ambitus.err = 2;
    g_ambitus.pad = 0;
    g_ambitus.argc = 0;
    g_ambitus.argv = (char **)0;
    g_ambitus.envp = (char **)0;
    return (unsigned char *)&g_ambitus;
}

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

/* exsrt_scriptor_scribe(s: ptr, t: ptr) -> u64 -- bytes actually STORED,
 * short on capacity (point 4). */
uint64_t exsrt_scriptor_scribe(unsigned char *sp, unsigned char *tp)
{
    ExsScriptor s;
    ExsTextus t;
    uint64_t room, stored, i;
    memcpy(&s, sp, sizeof s);
    memcpy(&t, tp, sizeof t);
    (void)s;                /* 1 and 2 select the same buffer (point 2) */
    room = (g_out_pos < g_out_cap) ? g_out_cap - g_out_pos : 0;
    stored = (t.len < room) ? t.len : room;
    for (i = 0; i < stored; i++) g_out[g_out_pos + i] = t.ptr[i];
    g_out_pos += stored;
    g_dropped += t.len - stored;
    return stored;
}

/* exsrt_scriptor_scribe_octeto(s: ptr, b: u8) -> u64 -- one raw byte. */
uint64_t exsrt_scriptor_scribe_octeto(unsigned char *sp, uint64_t b)
{
    ExsScriptor s;
    memcpy(&s, sp, sizeof s);
    (void)s;
    if (g_out_pos >= g_out_cap) { g_dropped += 1; return 0; }
    g_out[g_out_pos++] = (unsigned char)(b & 0xFFu);
    return 1;
}

/* exsrt_lector_lege_octeto(l: ptr) -> u16 -- one raw byte from the input
 * buffer, or 256 at its end: the host shim's EOF, mirrored. */
uint64_t exsrt_lector_lege_octeto(unsigned char *lp)
{
    ExsLector l;
    memcpy(&l, lp, sizeof l);
    (void)l;                /* descriptor 0 is the only reader (point 2) */
    if (g_in_pos >= g_in_len) return 256;
    return g_in[g_in_pos++];
}

/* ---- the kernel entry --------------------------------------------------- */
/* tools/amd-dispatch passes the buffers as kernargs, in this order; the
 * metadata note clang emits records the layout for the runner. The kernel
 * name is load-bearing: the runner looks it up by symbol. */
uint64_t exs_initium(unsigned char *p0);

static unsigned char g_mundus[64];

__attribute__((amdgpu_kernel))
void exs_amdgcn_entry(unsigned char *in, uint64_t in_len,
                      unsigned char *out, uint64_t out_cap,
                      uint64_t *result)
{
    uint64_t rc;
    g_in = in;        g_in_len = in_len;   g_in_pos = 0;
    g_out = out;      g_out_cap = out_cap; g_out_pos = 0;
    g_dropped = 0;    g_abort = 0;
    g_result = result;
    rc = exs_initium(g_mundus);
    g_result[0] = rc & 0xFFu;
    g_result[1] = g_out_pos;
    g_result[2] = g_dropped;
    g_result[3] = g_abort;
}
