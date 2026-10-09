/* examples/dcfid_gate/dcfid_gate.h -- the C face of dcfid_gate.exsc
 * (exsc --emitte c), for a C or Rust host. Hand-written; proba_c.sh compares
 * it with the generated header, so a signature that drifts is a build error
 * there. The unit is emitted from the ORDERED file list
 *     dcf_net_gate/dcf_net_gate.exsc  dcfid_gate/dcfid_gate.exsc
 * (the shared network gate first), so the one C unit also defines the four
 * exs_* functions of dcf_net_gate.h; this header names only this gate's.
 *
 * Every integer is uint64_t. `b` points at a buffer of EXACTLY the size named
 * below, all of it readable, and none of it is written:
 *   admitte_nomen                  32 bytes   (a username is at most 32)
 *   admitte_signum                 64 bytes   (a session id is 64, a token 32)
 *   admitte_formam_signaturae     512 bytes   (a Stripe-Signature header)
 * `n` is the length of the text in `b`. A host copies its input into a
 * zero-padded buffer of that size; an `n` past the capacity is answered
 * (verdict 2) before any byte is read, so an over-long input need not be
 * copied whole. admitte_summam takes cents by value. All four functions are
 * total: nothing a caller passes traps.
 *
 * Verdicts: 0 admitted; the rest are the first failing check, tabulated at
 * the top of dcfid_gate.exsc. A trap reaches exsrt_abortus, which the host
 * supplies and which must not return. */
#ifndef EXSECUTOR_DCFID_GATE_H
#define EXSECUTOR_DCFID_GATE_H
#include <stdint.h>

uint64_t exs_admitte_nomen(unsigned char *b, uint64_t n);
uint64_t exs_admitte_signum(unsigned char *b, uint64_t n, uint64_t genus);
uint64_t exs_admitte_summam(uint64_t c);
uint64_t exs_admitte_formam_signaturae(unsigned char *b, uint64_t n);

_Noreturn void exsrt_abortus(unsigned kind);
#endif
