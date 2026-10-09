/* examples/dcf_net_gate/dcf_net_gate.h -- the C face of dcf_net_gate.exsc
 * (exsc --emitte c), for a C or Rust host. Hand-written; proba_c.sh compiles
 * it against the emitted unit and proba_c.sh step 1 compares it with the
 * generated header, so a signature that drifts is a build error there.
 *
 * Every integer is uint64_t. `b` points at a buffer of EXACTLY the size named
 * below, all of it readable, and none of it is written:
 *   admitte_ipv4, ordo_ipv4      16 bytes   (an address is at most 15)
 *   admitte_portum,
 *   admitte_intervallum           8 bytes   (a number is at most 5)
 * `n` is the length of the text in `b`. A host copies its input into a
 * zero-padded buffer of that size; an `n` past the capacity is answered
 * (verdict 2) before any byte is read, so an over-long input need not be
 * copied whole. All four functions are total: nothing a caller passes traps.
 *
 * Verdicts: 0 admitted; the rest are the first failing check, tabulated at
 * the top of dcf_net_gate.exsc. ordo_ipv4 answers 255 for text that
 * admitte_ipv4 does not admit, else a class 0..7 (same table). A trap
 * reaches exsrt_abortus, which the host supplies and which must not return. */
#ifndef EXSECUTOR_DCF_NET_GATE_H
#define EXSECUTOR_DCF_NET_GATE_H
#include <stdint.h>

uint64_t exs_admitte_ipv4(unsigned char *b, uint64_t n);
uint64_t exs_ordo_ipv4(unsigned char *b, uint64_t n);
uint64_t exs_admitte_portum(unsigned char *b, uint64_t n);
uint64_t exs_admitte_intervallum(unsigned char *b, uint64_t n);

_Noreturn void exsrt_abortus(unsigned kind);
#endif
