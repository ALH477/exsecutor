// SPDX-License-Identifier: MIT
//
// exemplum_horologii.c -- metronomus as the firmware of a digital watch.
//
// A watch has a 32,768 Hz crystal and a real-time counter that wakes the CPU
// on a prescaler tick; this one wakes eight times a second (every 4,096
// counts), which is a common RTC setting. Everything the face shows is a
// function of the count the RTC hands the CPU, so there is exactly one source
// of time and nothing to drift against:
//
//   * time of day and date: a 1 kHz Metronomus over the crystal (32,768
//     counts are exactly 1,000 ms), added to the instant the wearer SET with
//     the buttons -- tempus_ex_hora -- and shown with hora_civilis in the
//     zone the wearer chose;
//   * the alarm: an instant, compared once per wake;
//   * the countdown: a Horologia timer counted in those milliseconds;
//   * the stopwatch: a 100 Hz Metronomus over the same crystal. 32,768 / 100
//     is 327.68 counts per centisecond -- NOT an integer -- and the rational
//     accumulator keeps it exact: a day on the stopwatch reads 24:00:00.00.
//     A firmware that divides by 327 instead gains three minutes a day; the
//     last line prints that figure for contrast.
//
// The wakes and button presses are scripted, so the transcript is the same on
// every machine and is compared byte for byte (exemplum_horologii.expected).
// On a watch, `scribe` would drive the display instead.
//
// TWO BUILDS. Hosted (the default): write(2) to stdout. Bare
// (-DMETRONOMUS_NUDUS -ffreestanding, 32-bit ARM): no libc at all -- its own
// _start, memcpy/memset for the compiler, and two Linux syscalls so that
// qemu-arm can run the very Thumb-2 code a Cortex-M would. proba_c.sh builds
// and runs both, against the mips64-none-o64 unit (the 32-bit-address row).

#include <stddef.h>
#include <stdint.h>

#include "metronomus.h"

// ---- the platform --------------------------------------------------------

#if defined(METRONOMUS_NUDUS)
// ARM EABI Linux: r7 is the syscall number -- and Thumb's frame pointer, so
// a C asm operand may not name it. A four-instruction stub saves it instead
// (Thumb-1, so it also assembles for ARMv6-M). Only for running under
// qemu-arm; a watch replaces these two calls with its display driver and a
// reset.
__asm__(".syntax unified\n"
        ".thumb\n"
        ".global nudus_syscall\n"
        ".type nudus_syscall, %function\n"
        ".thumb_func\n"
        "nudus_syscall:\n"
        "    push {r7}\n"
        "    mov r7, r3\n"
        "    svc 0\n"
        "    pop {r7}\n"
        "    bx lr\n");
long nudus_syscall(long a, long b, long c, long n);
static void emit(const char *p, size_t n) { nudus_syscall(1, (long)p, (long)n, 4); }
static _Noreturn void finis(int code)
{
    for (;;)
        nudus_syscall(code, 0, 0, 248);
}
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void *memset(void *d, int v, size_t n)
{
    unsigned char *a = d;
    while (n--)
        *a++ = (unsigned char)v;
    return d;
}

// The ARM run-time helpers the compiler calls and a watch toolchain's
// libgcc/compiler-rt supplies; there is none in this bare build, so here are
// the four the link asks for. Written in 32-bit operations only, so none can
// compile into a call to itself; the halves go through a union because
// building a u64 from two u32 with a shift is itself __aeabi_llsl on v6-M.
// (On Cortex-M4 only __aeabi_memcpy is needed; --gc-sections drops the rest.)
typedef union { uint64_t v; uint32_t w[2]; } Dimidia;  // w[0] low: ARM is little-endian here
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the helpers assume little-endian ARM");

void __aeabi_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }

uint64_t __aeabi_llsl(uint64_t v, int n)
{
    Dimidia x = {v}, r;
    if (n >= 32) {
        r.w[1] = x.w[0] << (n - 32);
        r.w[0] = 0;
    } else if (n == 0) {
        return v;
    } else {
        r.w[1] = x.w[1] << n | x.w[0] >> (32 - n);
        r.w[0] = x.w[0] << n;
    }
    return r.v;
}

uint64_t __aeabi_llsr(uint64_t v, int n)
{
    Dimidia x = {v}, r;
    if (n >= 32) {
        r.w[0] = x.w[1] >> (n - 32);
        r.w[1] = 0;
    } else if (n == 0) {
        return v;
    } else {
        r.w[0] = x.w[0] >> n | x.w[1] << (32 - n);
        r.w[1] = x.w[1] >> n;
    }
    return r.v;
}

// 32 x 32 -> 64 from four 16 x 16 -> 32 products, which v6-M's MULS does.
static void multiplica32(uint32_t a, uint32_t b, uint32_t *hi, uint32_t *lo)
{
    uint32_t al = a & 0xFFFFu, ah = a >> 16, bl = b & 0xFFFFu, bh = b >> 16;
    uint32_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint32_t mid = (ll >> 16) + (lh & 0xFFFFu) + (hl & 0xFFFFu);
    *lo = (ll & 0xFFFFu) | mid << 16;
    *hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
}

uint64_t __aeabi_lmul(uint64_t a, uint64_t b)
{
    Dimidia x = {a}, y = {b}, r;
    uint32_t hi, lo;
    multiplica32(x.w[0], y.w[0], &hi, &lo);
    r.w[0] = lo;
    r.w[1] = hi + x.w[0] * y.w[1] + x.w[1] * y.w[0];
    return r.v;
}
#else
#include <stdlib.h>
#include <unistd.h>
static void emit(const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(1, p, n);
        if (w <= 0)
            exit(3);
        p += w;
        n -= (size_t)w;
    }
}
static _Noreturn void finis(int code) { exit(code); }
#endif

_Noreturn void exsrt_abortus(unsigned kind)
{
    (void)kind;
    finis(100);
}

// ---- a formatter with no libc and no 64-bit `/` --------------------------------

static char linea[96];
static size_t lon;

static void scribe(const char *s)
{
    while (*s)
        linea[lon++] = *s++;
}

static void numerus(uint64_t v, unsigned width)
{
    char t[20];
    unsigned k = 0;
    do {
        t[k++] = (char)('0' + exs_residuum(v, 10));
        v = exs_quotus(v, 10);
    } while (v);
    while (k < width)
        t[k++] = '0';
    while (k)
        linea[lon++] = t[--k];
}

static void fini_lineam(void)
{
    linea[lon++] = '\n';
    emit(linea, lon);
    lon = 0;
}

static const char *const hebdomas[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static void facies(const char *titulus, uint64_t ms, int64_t zona)
{
    unsigned char h[HORA_BYTES];
    exs_hora_civilis(h, ms, (uint64_t)zona);
    scribe(titulus);
    scribe(hebdomas[h[HORA_HEBDOMAS]]);
    scribe(" ");
    numerus((uint64_t)(h[0] << 8 | h[1]), 4);
    scribe("-");
    numerus(h[HORA_MENSIS], 2);
    scribe("-");
    numerus(h[HORA_DIES], 2);
    scribe(" ");
    numerus(h[HORA_HORA], 2);
    scribe(":");
    numerus(h[HORA_MINUTUM], 2);
    scribe(":");
    numerus(h[HORA_SECUNDUM], 2);
    fini_lineam();
}

static void chronographum(const char *titulus, uint64_t cs)
{
    scribe(titulus);
    numerus(exs_quotus(cs, 360000), 2);
    scribe(":");
    numerus(exs_residuum(exs_quotus(cs, 6000), 60), 2);
    scribe(":");
    numerus(exs_residuum(exs_quotus(cs, 100), 60), 2);
    scribe(".");
    numerus(exs_residuum(cs, 100), 2);
    fini_lineam();
}

static uint64_t be64(const unsigned char *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++)
        v = v << 8 | p[i];
    return v;
}

// ---- the watch ---------------------------------------------------------------

#define CRYSTAL    32768u   // Hz
#define WAKE       4096u    // counts per wake: 8 wakes a second
#define ZONA       60       // the wearer's zone: UTC+01:00, minutes east

static int horologium(void)
{
    static METRONOMUS_RECORD(ms_clk, METRONOMUS_BYTES);
    static METRONOMUS_RECORD(chrono, METRONOMUS_BYTES);
    static METRONOMUS_RECORD(timers, HOROLOGIA_BYTES);

    // The wearer sets 2028-02-28 23:59:30, UTC+1, with the buttons.
    unsigned char set[HORA_BYTES] = {2028 >> 8, 2028 & 0xFF, 2, 28, 23, 59, 30, 0, 0, 0};
    uint64_t base = exs_tempus_ex_hora(set, (uint64_t)(int64_t)ZONA);
    // ... and a wrong one first, which the watch refuses.
    unsigned char bad[HORA_BYTES] = {2027 >> 8, 2027 & 0xFF, 2, 29, 7, 0, 0, 0, 0, 0};
    scribe("set 2027-02-29 07:00:00: ");
    scribe(exs_tempus_ex_hora(bad, (uint64_t)(int64_t)ZONA) == METRONOMUS_NULLUS ? "refused" : "accepted");
    fini_lineam();

    // Alarm at 00:00:30 on the 29th -- the leap day.
    unsigned char al[HORA_BYTES] = {2028 >> 8, 2028 & 0xFF, 2, 29, 0, 0, 30, 0, 0, 0};
    uint64_t alarm = exs_tempus_ex_hora(al, (uint64_t)(int64_t)ZONA);

    exs_metronomum_para(ms_clk, CRYSTAL, 1000, 65535);
    exs_metronomum_para(chrono, CRYSTAL, 100, 65535);
    exs_celeritatem_pone(chrono, 0);    // the stopwatch starts stopped
    if (!exs_metronomus_valet(ms_clk) || !exs_metronomus_valet(chrono) ||
        base == METRONOMUS_NULLUS || alarm == METRONOMUS_NULLUS)
        return 2;
    facies("set:        ", base, ZONA);

    int alarm_rang = 0;
    uint64_t chrono_lap = 0;
    for (uint32_t wake = 1; wake <= 8u * 120u; wake++) {
        exs_pulsa(ms_clk, WAKE);
        exs_pulsa(chrono, WAKE);
        uint64_t ms = be64(ms_clk + METRONOMUS_PULSUS);
        uint64_t now = base + ms;

        if (wake == 8u * 10u) {                         // t = 10 s: start 45 s countdown
            exs_arma(timers, 0, ms, 45000, 0);
            scribe("countdown 00:45 started");
            fini_lineam();
        }
        if (exs_excita(timers, ms) & 1u) {
            facies("countdown done at ", now, ZONA);
        }
        if (wake == 8u * 5u)                            // t = 5 s: stopwatch start
            exs_celeritatem_pone(chrono, 256);
        if (wake == 8u * 65u + 2u)                      // t = 65.25 s: lap
            chrono_lap = be64(chrono + METRONOMUS_PULSUS);
        if (wake == 8u * 100u)                          // t = 100 s: stop
            exs_celeritatem_pone(chrono, 0);
        if (!alarm_rang && now >= alarm) {
            alarm_rang = 1;
            facies("ALARM       ", now, ZONA);
        }
        if (exs_residuum(wake, 8u * 15u) == 0)          // show the face every 15 s
            facies("            ", now, ZONA);
    }
    chronographum("lap:        ", chrono_lap);
    chronographum("stopwatch:  ", be64(chrono + METRONOMUS_PULSUS));

    // A day on the shelf, stopwatch running: 691,200 wakes.
    exs_celeritatem_pone(chrono, 256);
    uint64_t cs0 = be64(chrono + METRONOMUS_PULSUS);
    uint64_t ms0 = be64(ms_clk + METRONOMUS_PULSUS);
    for (uint32_t wake = 0; wake < 8u * 86400u; wake++) {
        exs_pulsa(ms_clk, WAKE);
        exs_pulsa(chrono, WAKE);
    }
    uint64_t dms = be64(ms_clk + METRONOMUS_PULSUS) - ms0;
    uint64_t dcs = be64(chrono + METRONOMUS_PULSUS) - cs0;
    facies("a day later ", base + be64(ms_clk + METRONOMUS_PULSUS), ZONA);
    scribe("ms elapsed: ");
    numerus(dms, 1);
    fini_lineam();
    chronographum("stopwatch day: ", dcs);
    // The naive firmware: one centisecond per 327 counts, truncated.
    chronographum("naive (/327):  ", exs_quotus((uint64_t)8u * 86400u * WAKE, 327));
    return dms == 86400000u && dcs == 8640000u && alarm_rang ? 0 : 1;
}

#if defined(METRONOMUS_NUDUS)
_Noreturn void _start(void) { finis(horologium()); }
#else
int main(void) { return horologium(); }
#endif
