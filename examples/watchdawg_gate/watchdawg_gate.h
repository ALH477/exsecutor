/* examples/watchdawg_gate/watchdawg_gate.h -- the C face of watchdawg_gate.exsc
 * (exsc --emitte c), for a C host. Hand-written; proba_c.sh compiles it against
 * the emitted unit and compares it, prototype by prototype, with the generated
 * header, so a signature that drifts is a build error there.
 *
 * Every integer is uint64_t. `b` points at a buffer of EXACTLY the size named
 * below, all of it readable, and none of it is written:
 *   admitte_numerum   20 bytes   (an integer is at most 20 digits)
 *   admitte_onus      16 bytes   (a load average is at most 13 bytes)
 * `n` is the length of the text in `b`. A host copies its input into a
 * zero-padded buffer of that size; an `n` past the longest admitted text is
 * answered (verdict 2) before any byte is read, so an over-long input need
 * not be copied whole. Both functions are total: nothing a caller passes traps.
 *
 * Verdicts: 0 admitted; the rest are the first failing check, tabulated at the
 * top of watchdawg_gate.exsc. A trap reaches exsrt_abortus, which the host
 * supplies and which must not return. */
#ifndef EXSECUTOR_WATCHDAWG_GATE_H
#define EXSECUTOR_WATCHDAWG_GATE_H
#include <stdint.h>

uint64_t exs_admitte_numerum(unsigned char *b, uint64_t n);
uint64_t exs_admitte_onus(unsigned char *b, uint64_t n);

_Noreturn void exsrt_abortus(unsigned kind);
#endif
