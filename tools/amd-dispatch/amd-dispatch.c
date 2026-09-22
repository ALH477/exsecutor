/* tools/amd-dispatch/amd-dispatch.c -- dispatch one compiled Exsecutor
 * program on a real AMD GPU and return its output bytes.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Exsecutor authors.
 *
 * DEV TOOLING, in the qemu/ucd-gen role: never on the compiler's build path,
 * linked against nothing at build time (the HSA runtime is dlopen'd; a host
 * without "libhsa-runtime64.so.1" is a loud "missing tool" at RUN time for
 * the suite's device phase, not a build dependency). The compiler's own
 * closed syscall allowlist governs the compiler tree (its .inc sources);
 * this file is not the compiler.
 *
 * WHAT IT DOES
 *
 *   amd-dispatch --co UNIT.gfx1102.co [--kernel exs_amdgcn_entry.kd]
 *                [--input FILE] [--output FILE] [--capacity BYTES]
 *                [--scratch BYTES] [--timeout SECONDS] [--agent gfx1102]
 *                [--list] [--trace]
 *
 * loads an amdgcn-amdhsa code object -- clang's output over ONE translation
 * unit made of `exsc --emitte c`'s unit with tests/c/exsrt_shim_amdgpu.c
 * appended (see ONE TRANSLATION UNIT below) -- allocates in/out/result
 * buffers in fine-grained system memory, writes the five kernargs the
 * shim's entry takes, posts ONE AQL kernel-dispatch packet with grid
 * (1,1,1) (the shim is one-workitem by construction), waits on the
 * completion signal, and writes the output buffer's first result[1] bytes
 * to --output. A report line goes to stderr:
 *
 *   amd-dispatch: agent=gfx1102 kernel=exs_amdgcn_entry.kd rc=0 out=101
 *                 dropped=0 abort=0
 *
 * EXIT STATUS: the kernel's initium rc (0..255) on a clean completion;
 * 111 if the device recorded an abort (`abortus N`, its line is the tail
 * of --output), dropped output bytes (capacity too small), or the wait
 * ran out of --timeout; smaller distinct codes for host-side failures:
 * usage 2, no HSA runtime 3, no matching GPU agent 4, an hsa_* call
 * failure 5, code object lacking the kernel 6, fixed private segment above
 * --scratch 7.
 *
 * ONE TRANSLATION UNIT, OR THE WAVE CORRUPTS ITSELF. The kernel descriptor
 * (VGPR/SGPR allocation, scratch) is computed by the compiler for the
 * kernel's own translation unit: it can only count the registers of
 * callees it can see. A kernel linked against a separately compiled unit
 * runs with a register budget sized for the shim alone (48 VGPRs here)
 * while the program's functions use more (100 in the one-unit build), and
 * the wave silently clobbers live values across those calls. MEASURED:
 * acies_float8 wrote all 640 correct bytes then tripped an overflow check
 * on a garbage operand; a standalone call chain faulted in the private
 * aperture. Compiled as one unit -- `cat unit.c exsrt_shim_amdgpu.c` --
 * both are byte-identical to the CPU goldens. The device build is
 * therefore always one unit; this tool cannot detect the other shape, so
 * the rule lives here and in the README.
 *
 * MEMORY MODEL, v1 (deliberately minimal):
 *   - all buffers in a FINE-GRAINED system-memory global region: host- and
 *     device-coherent, no staging copies. A device with no runtime-
 *     allocatable fine-grained region is a loud failure, not a silent
 *     fall-back; a staging path is written when a real machine is seen
 *     needing it, not before (evidence discipline).
 *   - kernarg segment: symbol-reported size, zero-filled, the five explicit
 *     kernargs written at offset 0; hidden kernargs (hostcall buffer,
 *     default queue, completion action, queue pointer) stay zero because
 *     the shim's kernel never reads them.
 *   - private (scratch) segment: the emitted C keeps real calls, so the
 *     kernel declares `.uses_dynamic_stack` with a fixed size of 0 and the
 *     DISPATCHER owes it the stack: --scratch (default 64 KiB per
 *     workitem) goes into the packet. Measured on gfx1102: 0 faults on the
 *     first stack access (and, with a NULL queue callback, takes the host
 *     process down at 0x4); 16 KiB..128 KiB behave alike; 256 KiB and up
 *     fault outright (the per-wave scratch ring's limit). The symbol's
 *     fixed size must fit inside --scratch (exit 7 otherwise).
 *   - the abort path: the shim's __builtin_trap lowers to `s_trap 2` then
 *     `s_sethalt` -- the wave halts and never completes; the runtime
 *     reports HSA_STATUS_ERROR_EXCEPTION through the queue callback, which
 *     this tool treats as the abort's completion event (0.4 s, measured).
 */

#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <glob.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- mirrored HSA ABI (ROCm 6.4.3 hsa.h, verbatim layout) --------------- */
typedef int32_t  hsa_status_t;          /* 0 == HSA_STATUS_SUCCESS */
typedef int64_t  hsa_signal_value_t;
typedef uint32_t hsa_queue_type32_t;
typedef struct { uint64_t handle; } hsa_agent_t;
typedef struct { uint64_t handle; } hsa_region_t;
typedef struct { uint64_t handle; } hsa_signal_t;
typedef struct { uint64_t handle; } hsa_executable_t;
typedef struct { uint64_t handle; } hsa_executable_symbol_t;
typedef struct { uint64_t handle; } hsa_code_object_reader_t;

enum { HSA_AGENT_INFO_NAME = 0, HSA_AGENT_INFO_DEVICE = 17 };
enum { HSA_DEVICE_TYPE_GPU = 1 };
enum {
    HSA_REGION_INFO_SEGMENT = 0,
    HSA_REGION_INFO_GLOBAL_FLAGS = 1,
    HSA_REGION_INFO_RUNTIME_ALLOC_ALLOWED = 5
};
/* hsa.h:3244-3257 -- KERNARG is 1, FINE_GRAINED is 2 (a first draft of this
 * file wrote 1 here and silently selected the kernarg region). */
enum { HSA_REGION_SEGMENT_GLOBAL = 0, HSA_REGION_GLOBAL_FLAG_FINE_GRAINED = 2 };
enum { HSA_PROFILE_FULL = 1, HSA_EXECUTABLE_STATE_UNFROZEN = 0 };
enum { HSA_QUEUE_TYPE_SINGLE = 1 };
enum {
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE = 11,
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE = 13,
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE = 14,
    HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT = 22
};
enum { HSA_SIGNAL_CONDITION_LT = 2, HSA_WAIT_STATE_BLOCKED = 0 };
enum { HSA_PACKET_TYPE_KERNEL_DISPATCH = 2, HSA_FENCE_SCOPE_SYSTEM = 2 };

typedef struct {               /* hsa_queue_t, HSA_LARGE_MODEL branch -- */
    hsa_queue_type32_t type;   /* hsa.h:77 defines it unconditionally on  */
    uint32_t           features; /* this platform; the LE branch differs */
    void              *base_address;
    hsa_signal_t       doorbell_signal;
    uint32_t           size;
    uint32_t           reserved1;
    uint64_t           id;
} hsa_queue_t;
_Static_assert(sizeof(hsa_queue_t) == 40, "hsa_queue_t ABI drift");
_Static_assert(__builtin_offsetof(hsa_queue_t, doorbell_signal) == 16,
               "hsa_queue_t doorbell offset drift");
_Static_assert(__builtin_offsetof(hsa_queue_t, size) == 24,
               "hsa_queue_t size offset drift");

typedef struct {                        /* hsa_kernel_dispatch_packet_t, */
    uint16_t  header;                   /* HSA_LARGE_MODEL branch */
    uint16_t  setup;
    uint16_t  workgroup_size_x;
    uint16_t  workgroup_size_y;
    uint16_t  workgroup_size_z;
    uint16_t  reserved0;
    uint32_t  grid_size_x;
    uint32_t  grid_size_y;
    uint32_t  grid_size_z;
    uint32_t  private_segment_size;
    uint32_t  group_segment_size;
    uint64_t  kernel_object;
    void     *kernarg_address;
    uint64_t  reserved2;
    hsa_signal_t completion_signal;
} aql_dispatch_t;
_Static_assert(sizeof(aql_dispatch_t) == 64,
               "an AQL packet is 64 bytes by specification");
_Static_assert(__builtin_offsetof(aql_dispatch_t, kernel_object) == 32,
               "packet kernel_object offset");
_Static_assert(__builtin_offsetof(aql_dispatch_t, kernarg_address) == 40,
               "packet kernarg_address offset");
_Static_assert(__builtin_offsetof(aql_dispatch_t, completion_signal) == 56,
               "packet completion_signal offset");

/* ---- dlopen'd entry points ---------------------------------------------- */
static void *g_rt;

static hsa_status_t (*hsa_init_)(void);
static hsa_status_t (*hsa_shut_down_)(void);
static hsa_status_t (*hsa_iterate_agents_)(
    hsa_status_t (*)(hsa_agent_t, void *), void *);
static hsa_status_t (*hsa_agent_get_info_)(hsa_agent_t, int32_t, void *);
static hsa_status_t (*hsa_agent_iterate_regions_)(
    hsa_agent_t, hsa_status_t (*)(hsa_region_t, void *), void *);
static hsa_status_t (*hsa_region_get_info_)(hsa_region_t, int32_t, void *);
static hsa_status_t (*hsa_memory_allocate_)(hsa_region_t, size_t, void **);
static hsa_status_t (*hsa_memory_free_)(void *);
static hsa_status_t (*hsa_signal_create_)(
    hsa_signal_value_t, uint32_t, const hsa_agent_t *, hsa_signal_t *);
static hsa_signal_value_t (*hsa_signal_wait_scacquire_)(
    hsa_signal_t, int32_t, hsa_signal_value_t, uint64_t, int32_t);
static void (*hsa_signal_store_screlease_)(hsa_signal_t, hsa_signal_value_t);
static hsa_status_t (*hsa_signal_destroy_)(hsa_signal_t);
static hsa_status_t (*hsa_queue_create_)(
    hsa_agent_t, uint32_t, hsa_queue_type32_t,
    void (*)(hsa_status_t, hsa_queue_t *, void *), void *,
    uint32_t, uint32_t, hsa_queue_t **);
static hsa_status_t (*hsa_queue_destroy_)(hsa_queue_t *);
static uint64_t (*hsa_queue_load_write_index_relaxed_)(const hsa_queue_t *);
static void (*hsa_queue_store_write_index_relaxed_)(hsa_queue_t *, uint64_t);
static hsa_status_t (*hsa_code_object_reader_create_from_memory_)(
    const void *, size_t, hsa_code_object_reader_t *);
static hsa_status_t (*hsa_executable_create_)(
    int32_t, int32_t, const char *, hsa_executable_t *);
static hsa_status_t (*hsa_executable_load_agent_code_object_)(
    hsa_executable_t, hsa_agent_t, hsa_code_object_reader_t,
    const char *, void *);
static hsa_status_t (*hsa_executable_freeze_)(hsa_executable_t, const char *);
static hsa_status_t (*hsa_executable_destroy_)(hsa_executable_t);
static hsa_status_t (*hsa_executable_get_symbol_by_name_)(
    hsa_executable_t, const char *, const hsa_agent_t *,
    hsa_executable_symbol_t *);
static hsa_status_t (*hsa_executable_iterate_symbols_)(
    hsa_executable_t, hsa_status_t (*)(hsa_executable_t,
                                       hsa_executable_symbol_t, void *),
    void *);
static hsa_status_t (*hsa_executable_symbol_get_info_)(
    hsa_executable_symbol_t, int32_t, void *);
static hsa_status_t (*hsa_status_string_)(hsa_status_t, const char **);

static _Noreturn void die(int code, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));static void die(int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("amd-dispatch: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(code);
}
static _Noreturn void die_hsa(const char *call, hsa_status_t s)
{
    const char *m = "?";
    if (hsa_status_string_) hsa_status_string_(s, &m);
    die(5, "%s failed: status %d (%s)", call, (int)s, m);
}
#define HSA_ST(o) do { hsa_status_t s_ = (o); if (s_ != 0) die_hsa(#o, s_); } while (0)

static void load_sym(void **slot, const char *name)
{
    *slot = dlsym(g_rt, name);
    if (!*slot)
        die(3, "libhsa-runtime64 lacks %s -- wrong library version?", name);
}

static void open_runtime(void)
{
    /* Discovery order: an explicit path, then the loader's own search, then
     * the NixOS store (rocminfo links the runtime but the profile exposes
     * only bin/, so on this machine the library lives in a store path the
     * loader will never see). Newest-named store path wins. */
    const char *env = getenv("EXS_HSA_LIBSO");
    if (env && *env) g_rt = dlopen(env, RTLD_NOW | RTLD_LOCAL);
    if (!g_rt) g_rt = dlopen("libhsa-runtime64.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!g_rt) g_rt = dlopen("libhsa-runtime64.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_rt) {
        /* /nix/store/<hash>-rocm-runtime-<ver>/lib/libhsa-runtime64.so.1 */
        char best[1024] = { 0 };
        glob_t g;
        if (glob("/nix/store/*-rocm-runtime-*/lib/libhsa-runtime64.so.1",
                 0, NULL, &g) == 0 && g.gl_pathc > 0) {
            strncpy(best, g.gl_pathv[g.gl_pathc - 1], sizeof best - 1);
        }
        globfree(&g);
        if (*best) g_rt = dlopen(best, RTLD_NOW | RTLD_LOCAL);
    }
    if (!g_rt)
        die(3, "no HSA runtime (libhsa-runtime64) on this host -- the ROCm"
               " userland is the device phase's qemu; without it the phase"
               " cannot run (override with EXS_HSA_LIBSO=/path/to/libhsa"
               "-runtime64.so.1)");
    load_sym((void **)&hsa_init_, "hsa_init");
    load_sym((void **)&hsa_shut_down_, "hsa_shut_down");
    load_sym((void **)&hsa_iterate_agents_, "hsa_iterate_agents");
    load_sym((void **)&hsa_agent_get_info_, "hsa_agent_get_info");
    load_sym((void **)&hsa_agent_iterate_regions_, "hsa_agent_iterate_regions");
    load_sym((void **)&hsa_region_get_info_, "hsa_region_get_info");
    load_sym((void **)&hsa_memory_allocate_, "hsa_memory_allocate");
    load_sym((void **)&hsa_memory_free_, "hsa_memory_free");
    load_sym((void **)&hsa_signal_create_, "hsa_signal_create");
    load_sym((void **)&hsa_signal_wait_scacquire_, "hsa_signal_wait_scacquire");
    load_sym((void **)&hsa_signal_store_screlease_,
             "hsa_signal_store_screlease");
    load_sym((void **)&hsa_signal_destroy_, "hsa_signal_destroy");
    load_sym((void **)&hsa_queue_create_, "hsa_queue_create");
    load_sym((void **)&hsa_queue_destroy_, "hsa_queue_destroy");
    load_sym((void **)&hsa_queue_load_write_index_relaxed_,
             "hsa_queue_load_write_index_relaxed");
    load_sym((void **)&hsa_queue_store_write_index_relaxed_,
             "hsa_queue_store_write_index_relaxed");
    load_sym((void **)&hsa_code_object_reader_create_from_memory_,
             "hsa_code_object_reader_create_from_memory");
    load_sym((void **)&hsa_executable_create_, "hsa_executable_create");
    load_sym((void **)&hsa_executable_load_agent_code_object_,
             "hsa_executable_load_agent_code_object");
    load_sym((void **)&hsa_executable_freeze_, "hsa_executable_freeze");
    load_sym((void **)&hsa_executable_destroy_, "hsa_executable_destroy");
    load_sym((void **)&hsa_executable_get_symbol_by_name_,
             "hsa_executable_get_symbol_by_name");
    load_sym((void **)&hsa_executable_symbol_get_info_,
             "hsa_executable_symbol_get_info");
    load_sym((void **)&hsa_status_string_, "hsa_status_string");
    load_sym((void **)&hsa_executable_iterate_symbols_,
             "hsa_executable_iterate_symbols");
}

/* ---- agent discovery ------------------------------------------------------ */
#define MAX_GPU 8
typedef struct {
    hsa_agent_t agents[MAX_GPU];
    char        names[MAX_GPU][64];
    int         count;
    const char *want;                   /* --agent substring, or NULL */
    int         chosen;                 /* -1 until matched */
} agent_scan_t;

static hsa_status_t agent_cb(hsa_agent_t a, void *data)
{
    agent_scan_t *s = (agent_scan_t *)data;
    int32_t type = 0;
    char name[64];
    hsa_status_t st;
    memset(name, 0, sizeof name);
    st = hsa_agent_get_info_(a, HSA_AGENT_INFO_DEVICE, &type);
    if (st != 0 || type != HSA_DEVICE_TYPE_GPU) return 0;
    st = hsa_agent_get_info_(a, HSA_AGENT_INFO_NAME, name);
    if (st != 0) return 0;
    if (s->count >= MAX_GPU) return 0;
    s->agents[s->count] = a;
    memcpy(s->names[s->count], name, sizeof s->names[0]);
    if ((!s->want && s->chosen < 0) ||
        (s->want && strstr(name, s->want) && s->chosen < 0))
        s->chosen = s->count;
    s->count++;
    return 0;
}

/* ---- region discovery ----------------------------------------------------- */
typedef struct { hsa_region_t region; int found; } region_scan_t;

static hsa_status_t region_cb(hsa_region_t r, void *data)
{
    region_scan_t *s = (region_scan_t *)data;
    int32_t  seg = -1, allowed = 0;
    uint32_t flags = 0;
    if (s->found) return 0;
    if (hsa_region_get_info_(r, HSA_REGION_INFO_SEGMENT, &seg) != 0) return 0;
    if (seg != HSA_REGION_SEGMENT_GLOBAL) return 0;
    if (hsa_region_get_info_(r, HSA_REGION_INFO_GLOBAL_FLAGS, &flags) != 0)
        return 0;
    if (!(flags & HSA_REGION_GLOBAL_FLAG_FINE_GRAINED)) return 0;
    if (hsa_region_get_info_(r, HSA_REGION_INFO_RUNTIME_ALLOC_ALLOWED,
                             &allowed) != 0 || !allowed)
        return 0;
    s->region = r;
    s->found = 1;
    return 0;
}

/* ---- I/O helpers ------------------------------------------------------------ */
static unsigned char *slurp(const char *path, uint64_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    if (!f) die(2, "cannot open %s", path);
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0)
        die(2, "cannot size %s", path);
    buf = malloc(n ? (size_t)n : 1);
    if (!buf) die(2, "out of memory reading %s", path);
    if (n && fread(buf, 1, (size_t)n, f) != (size_t)n)
        die(2, "cannot read %s", path);
    fclose(f);
    *len = (uint64_t)n;
    return buf;
}

static void dump(const char *path, const unsigned char *buf, uint64_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) die(2, "cannot open %s for writing", path);
    if (len && fwrite(buf, 1, (size_t)len, f) != (size_t)len)
        die(2, "short write on %s", path);
    if (fclose(f) != 0) die(2, "closing %s failed", path);
}

/* The queue error callback is not optional in practice: with NULL, an error
 * (e.g. the shim's s_trap) dereferences a null function pointer inside the
 * runtime and the tool segfaults instead of reporting the fault. */
static volatile int g_queue_error = 0;
static void queue_error_cb(hsa_status_t status, hsa_queue_t *q, void *data)
{
    const char *m = "?";
    (void)q; (void)data;
    if (hsa_status_string_) hsa_status_string_(status, &m);
    fprintf(stderr, "amd-dispatch: queue error: status %d (%s) -- the kernel"
            " faulted or trapped\n", (int)status, m);
    g_queue_error = 1;
}

/* SYMBOL_TYPE/NAME infos for the lookup-failure diagnostic */
enum {
    HSA_EXECUTABLE_SYMBOL_INFO_TYPE = 0,
    HSA_EXECUTABLE_SYMBOL_INFO_NAME_LENGTH = 1,
    HSA_EXECUTABLE_SYMBOL_INFO_NAME = 2
};
enum { HSA_SYMBOL_TYPE_KERNEL = 0 };

static hsa_status_t sym_name_cb(hsa_executable_t e, hsa_executable_symbol_t s,
                                void *d)
{
    uint32_t len = 0, type = 1;
    char name[128];
    (void)e; (void)d;
    if (hsa_executable_symbol_get_info_(s, HSA_EXECUTABLE_SYMBOL_INFO_NAME_LENGTH,
                                        &len) != 0 || len >= sizeof name)
        return 0;
    hsa_executable_symbol_get_info_(s, HSA_EXECUTABLE_SYMBOL_INFO_NAME, name);
    hsa_executable_symbol_get_info_(s, HSA_EXECUTABLE_SYMBOL_INFO_TYPE, &type);
    name[len] = 0;
    fprintf(stderr, "    %s (type %u)\n", name, type);
    return 0;
}

static const char *a_co = NULL;
/* The loader names a code-object-v5 kernel by its DESCRIPTOR symbol, `.kd`
 * appended (measured: the plain name is not found; the symbol listing on
 * failure showed `exs_amdgcn_entry.kd (type 1 = KERNEL)`). */
static const char *a_kernel = "exs_amdgcn_entry.kd";
static const char *a_input = NULL;
static const char *a_output = NULL;
static const char *a_agent = NULL;
static uint64_t    a_capacity = 4u << 20;
static uint64_t    a_scratch = 64u << 10;   /* per-workitem private segment */
static uint64_t    a_timeout = 60;          /* seconds; a hang signal, not a budget */
static int         a_list = 0;
static int         a_trace = 0;
#define TRACE(...) do { if (a_trace) { fprintf(stderr, "+ "__VA_ARGS__); } } while (0)

static void usage(void)
{
    die(2, "usage: amd-dispatch --co FILE.co [--kernel NAME] [--input FILE]\n"
           "                [--output FILE] [--capacity BYTES] [--scratch BYTES]\n"
           "                [--timeout SECONDS] [--agent SUBSTR] [--list] [--trace]");
}

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        #define TAKE(dst) do { if (++i >= argc) usage(); dst = argv[i]; } while (0)
        if (!strcmp(a, "--co")) TAKE(a_co);
        else if (!strcmp(a, "--kernel")) TAKE(a_kernel);
        else if (!strcmp(a, "--input")) TAKE(a_input);
        else if (!strcmp(a, "--output")) TAKE(a_output);
        else if (!strcmp(a, "--agent")) TAKE(a_agent);
        else if (!strcmp(a, "--capacity")) {
            if (++i >= argc) usage();
            a_capacity = strtoull(argv[i], NULL, 0);
            if (!a_capacity) usage();
        }
        else if (!strcmp(a, "--scratch")) {
            if (++i >= argc) usage();
            a_scratch = strtoull(argv[i], NULL, 0);
            if (a_scratch > UINT32_MAX) usage();
        }
        else if (!strcmp(a, "--timeout")) {
            if (++i >= argc) usage();
            a_timeout = strtoull(argv[i], NULL, 0);
            if (!a_timeout) usage();
        }
        else if (!strcmp(a, "--list")) a_list = 1;
        else if (!strcmp(a, "--trace")) a_trace = 1;
        else usage();
    }
    if (!a_list && (!a_co || !a_output)) usage();

    open_runtime();
    HSA_ST(hsa_init_());

    agent_scan_t scan;
    memset(&scan, 0, sizeof scan);
    scan.want = a_agent;
    scan.chosen = -1;
    HSA_ST(hsa_iterate_agents_(agent_cb, &scan));
    if (a_list) {
        for (i = 0; i < scan.count; i++)
            printf("%s\n", scan.names[i]);
        hsa_shut_down_();
        return scan.count ? 0 : 4;
    }
    if (scan.count == 0)
        die(4, "no GPU agents -- /dev/kfd present but no compute devices?");
    if (a_agent && scan.chosen < 0)
        die(4, "no GPU agent matching \"%s\" (have %d: use --list)",
            a_agent, scan.count);
    hsa_agent_t agent = scan.agents[a_agent ? scan.chosen : 0];
    const char *agent_name = scan.names[a_agent ? scan.chosen : 0];

    region_scan_t rscan;
    memset(&rscan, 0, sizeof rscan);
    HSA_ST(hsa_agent_iterate_regions_(agent, region_cb, &rscan));
    if (!rscan.found)
        die(5, "agent %s offers no runtime-allocatable fine-grained global"
               " region -- the staged-copy path is not written until a real"
               " machine needs it", agent_name);

    /* the buffers */
    uint64_t in_len = 0;
    unsigned char *in_host = NULL;
    if (a_input) in_host = slurp(a_input, &in_len);

    void *in_dev = NULL, *out_dev = NULL, *res_dev = NULL, *karg = NULL;
    if (in_len) HSA_ST(hsa_memory_allocate_(rscan.region, (size_t)in_len, &in_dev));
    HSA_ST(hsa_memory_allocate_(rscan.region, (size_t)a_capacity, &out_dev));
    HSA_ST(hsa_memory_allocate_(rscan.region, 64, &res_dev));  /* 4 words used, room to grow */
    if (in_len) memcpy(in_dev, in_host, (size_t)in_len);
    memset(out_dev, 0, (size_t)a_capacity);
    memset(res_dev, 0, 64);

    /* load the code object (from memory: create_from_file takes an
     * hsa_file_t fd, and a memory read keeps this tool's I/O in one place) */
    uint64_t co_len = 0;
    unsigned char *co_host = slurp(a_co, &co_len);
    hsa_code_object_reader_t reader;
    HSA_ST(hsa_code_object_reader_create_from_memory_(co_host, (size_t)co_len,
                                                      &reader));
    hsa_executable_t exec;
    HSA_ST(hsa_executable_create_(HSA_PROFILE_FULL,
                                  HSA_EXECUTABLE_STATE_UNFROZEN, "", &exec));
    HSA_ST(hsa_executable_load_agent_code_object_(exec, agent, reader, "",
                                                  NULL));
    HSA_ST(hsa_executable_freeze_(exec, NULL));
    hsa_executable_symbol_t sym;
    hsa_status_t st = hsa_executable_get_symbol_by_name_(exec, a_kernel,
                                                         &agent, &sym);
    if (st != 0) {
        /* say what IS there before dying -- this line saved a guess once */
        fprintf(stderr, "amd-dispatch: code object %s has no kernel named"
                " %s; its symbols are:\n", a_co, a_kernel);
        hsa_executable_iterate_symbols_(exec, sym_name_cb, NULL);
        exit(6);
    }
    TRACE("kernel symbol found\n");

    uint64_t kernel_object = 0;
    uint32_t kernarg_size = 0, group_size = 0, private_size = 0;
    HSA_ST(hsa_executable_symbol_get_info_(
        sym, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &kernel_object));
    TRACE("kernel_object=%llx\n", (unsigned long long)kernel_object);
    HSA_ST(hsa_executable_symbol_get_info_(
        sym, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE,
        &kernarg_size));
    HSA_ST(hsa_executable_symbol_get_info_(
        sym, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE,
        &group_size));
    HSA_ST(hsa_executable_symbol_get_info_(
        sym, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE,
        &private_size));
    TRACE("kernarg=%u group=%u private=%u\n", kernarg_size, group_size,
          private_size);
    /* The symbol's private size is the FIXED part only. The emitted C keeps
     * real calls (exs_initium -> exs_saluta -> exsrt_scriptor_scribe are
     * separate functions in the object) and a call on GCN needs scratch, so
     * the metadata says `.uses_dynamic_stack: true` with fixed size 0. A
     * packet with private_segment_size 0 then faults on the first stack
     * access, and the runtime's fault path takes the host process down with
     * it (measured: SEGV at 0x4 on the async-event thread, both agents).
     * --scratch is the budget the dispatcher owes such a kernel; the fixed
     * part must fit inside it. */
    if (private_size > a_scratch)
        die(7, "kernel %s declares a %u-byte fixed private segment; raise"
               " --scratch above it", a_kernel, private_size);
    if (kernarg_size < 40)
        die(6, "kernel %s declares a %u-byte kernarg segment; the shim's entry"
               " takes 40", a_kernel, kernarg_size);

    /* kernargs: the shim's entry signature, five u64s at offset 0 */
    HSA_ST(hsa_memory_allocate_(rscan.region, kernarg_size, &karg));
    memset(karg, 0, kernarg_size);
    {
        uint64_t *k = (uint64_t *)karg;
        k[0] = (uint64_t)(uintptr_t)in_dev;
        k[1] = in_len;
        k[2] = (uint64_t)(uintptr_t)out_dev;
        k[3] = a_capacity;
        k[4] = (uint64_t)(uintptr_t)res_dev;
    }

    /* the queue, the packet, the doorbell */
    hsa_queue_t *queue = NULL;
    /* private_segment_size hint = the scratch we will dispatch with, not
     * UINT32_MAX ("no information"): measured -- see the README -- the
     * runtime sizes the queue's scratch backing from it. */
    HSA_ST(hsa_queue_create_(agent, 1024, HSA_QUEUE_TYPE_SINGLE,
                             queue_error_cb, NULL,
                             (uint32_t)a_scratch, UINT32_MAX, &queue));
    TRACE("queue created: size=%u base=%p\n", queue->size, queue->base_address);
    hsa_signal_t completion;
    HSA_ST(hsa_signal_create_(1, 0, NULL, &completion));

    uint64_t widx = hsa_queue_load_write_index_relaxed_(queue);
    aql_dispatch_t *pkt =
        (aql_dispatch_t *)((char *)queue->base_address +
                           (widx & (queue->size - 1)) * sizeof(aql_dispatch_t));
    memset(pkt, 0, sizeof *pkt);
    pkt->setup = 1;                 /* one-dimensional grid */
    pkt->workgroup_size_x = 1;
    pkt->workgroup_size_y = 1;
    pkt->workgroup_size_z = 1;
    pkt->grid_size_x = 1;
    pkt->grid_size_y = 1;
    pkt->grid_size_z = 1;
    pkt->private_segment_size = (uint32_t)a_scratch;
    pkt->group_segment_size = group_size;
    pkt->kernel_object = kernel_object;
    pkt->kernarg_address = karg;
    pkt->completion_signal = completion;
    {
        uint16_t header = HSA_PACKET_TYPE_KERNEL_DISPATCH
                        | (HSA_FENCE_SCOPE_SYSTEM << 9)   /* acquire scope */
                        | (HSA_FENCE_SCOPE_SYSTEM << 11); /* release scope */
        /* Publication order per the HSA runtime spec: bump the write index,
         * then the header store (release) makes the packet visible, then
         * the doorbell carries the new write index. For a SINGLE-type queue
         * the doorbell must be monotonic (hsa.h, hsa_queue_t.doorbell), and
         * the value is the index of the LAST packet written, i.e. widx --
         * ROCm's own HIP/CLR dispatch writes `doorbell = index` after
         * `store_write_index(index + 1)`. */
        hsa_queue_store_write_index_relaxed_(queue, widx + 1);
        __atomic_store_n(&pkt->header, header, __ATOMIC_RELEASE);
        hsa_signal_store_screlease_(queue->doorbell_signal, (int64_t)widx);
    }
    TRACE("doorbell rung, waiting\n");

    /* wait: the --timeout budget is a hang signal, not an expectation. Waited
     * in 250 ms slices because a trapped wave never completes: the shim's
     * __builtin_trap lowers to `s_trap 2` followed by `s_sethalt` (measured
     * from `clang -S`), the runtime reports HSA_STATUS_ERROR_EXCEPTION
     * through the queue callback, and the completion signal stays at 1
     * forever. The callback is therefore the abort's completion event. */
    hsa_signal_value_t v = 1;
    {
        uint64_t slice, slices = a_timeout * 4;
        for (slice = 0; slice < slices && v >= 1 && !g_queue_error; slice++)
            v = hsa_signal_wait_scacquire_(
                completion, HSA_SIGNAL_CONDITION_LT, 1,
                250ull * 1000 * 1000, HSA_WAIT_STATE_BLOCKED);
    }
    if (g_queue_error) {
        /* the shim wrote the record from exsrt_abortus before trapping;
         * read it, hand the buffer (with its `abortus N` line) to --output,
         * and report the abort the way check_run reads it: exit 111 plus
         * the kind. No teardown: a halted wave's queue is the runtime's to
         * reclaim at process exit. */
        const uint64_t *r = (const uint64_t *)res_dev;
        uint64_t out_len = r[1] < a_capacity ? r[1] : a_capacity;
        if (a_trace) {
            int w;
            for (w = 0; w < 8; w++)
                fprintf(stderr, "+ record[%d]=%llu (0x%llx)\n", w,
                        (unsigned long long)r[w], (unsigned long long)r[w]);
        }
        fprintf(stderr,
                "amd-dispatch: agent=%s kernel=%s rc=- out=%llu dropped=%llu"
                " abort=%llu\n",
                agent_name, a_kernel, (unsigned long long)out_len,
                (unsigned long long)r[2], (unsigned long long)r[3]);
        dump(a_output, (const unsigned char *)out_dev, out_len);
        die(111, "kernel aborted on device (abortus %llu) -- the line is"
                 " the tail of %s", (unsigned long long)r[3], a_output);
    }
    if (v >= 1) {
        /* A hung kernel still leaves evidence: the shim writes the result
         * record from exsrt_abortus before it traps, and the output buffer
         * holds whatever was scribed. Dump both before dying so the hang
         * can be read rather than guessed at. */
        const uint64_t *r = (const uint64_t *)res_dev;
        uint64_t out_len = r[1] < a_capacity ? r[1] : a_capacity;
        fprintf(stderr, "amd-dispatch: agent=%s kernel=%s TIMEOUT record:"
                " rc=%llu out=%llu dropped=%llu abort=%llu\n",
                agent_name, a_kernel, (unsigned long long)r[0],
                (unsigned long long)r[1], (unsigned long long)r[2],
                (unsigned long long)r[3]);
        dump(a_output, (const unsigned char *)out_dev, out_len);
        die(111, "kernel on %s did not complete inside %llu s -- hung, or"
                 " slower than the budget (--timeout); %llu buffer bytes"
                 " written to %s", agent_name, (unsigned long long)a_timeout,
            (unsigned long long)out_len, a_output);
    }
    if (g_queue_error)
        die(111, "kernel faulted on %s (queue error above) -- for an abortus"
                 " the shim's line and kind are in the output buffer",
            agent_name);

    /* the result record the shim wrote */
    if (a_trace) {
        const uint64_t *r = (const uint64_t *)res_dev;
        int w;
        for (w = 0; w < 8; w++)
            fprintf(stderr, "+ record[%d]=%llu (0x%llx)\n", w,
                    (unsigned long long)r[w], (unsigned long long)r[w]);
    }
    {
        const uint64_t *r = (const uint64_t *)res_dev;
        uint64_t rc = r[0], out_len = r[1], dropped = r[2], abort_kind = r[3];
        fprintf(stderr,
                "amd-dispatch: agent=%s kernel=%s rc=%llu out=%llu dropped=%llu"
                " abort=%llu\n",
                agent_name, a_kernel, (unsigned long long)rc,
                (unsigned long long)out_len, (unsigned long long)dropped,
                (unsigned long long)abort_kind);
        if (abort_kind)
            die(111, "kernel aborted on device (abortus %llu) -- the line is"
                     " in the output buffer", (unsigned long long)abort_kind);
        if (dropped)
            die(111, "kernel dropped %llu output bytes beyond the %llu-byte"
                     " capacity", (unsigned long long)dropped,
                     (unsigned long long)a_capacity);
        dump(a_output, (const unsigned char *)out_dev, out_len);

        HSA_ST(hsa_queue_destroy_(queue));
        HSA_ST(hsa_signal_destroy_(completion));
        HSA_ST(hsa_executable_destroy_(exec));
        HSA_ST(hsa_shut_down_());
        return (int)(rc & 0xFFu);
    }
}
