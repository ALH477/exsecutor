/* examples/custos/custos.h -- the C face of custos.exsc, for any C host that
 * links the unit `exsc --emitte c` emits from it (Punctim's dcf-ws-bridge is
 * the first). Hand-written; proba_c.sh compiles it against the generated unit,
 * so a signature that drifts from the definition is a compile error there.
 *
 * Library mode (docs/design/c-backend.md D1): the unit has no entry point and
 * imports one symbol, exsrt_abortus, which the host supplies. It is reached
 * only by a bounds or overflow trap; admitte reads d[0..32) at most and traps
 * on nothing a caller can pass with n <= 32. A trap is a defect, so the host's
 * exsrt_abortus should stop the process, not return (it cannot: _Noreturn).
 */
#ifndef EXSECUTOR_CUSTOS_H
#define EXSECUTOR_CUSTOS_H

#include <stdint.h>

/* Verdict on one bare-dialect DCF datagram d[0..n). d must point at 32
 * readable bytes; bytes at and past n are not read. 0 = admitted (a valid
 * 17-byte DeModFrame, or a valid 32-byte SuperPack whose two frames are
 * valid); 1 bad sync; 2 bad version; 3 bad CRC; 4 32 bytes that are not a
 * SuperPack; 5 a SuperPack core with version != 1; 6 neither 17 nor 32. */
uint64_t exs_admitte(unsigned char *d, uint64_t n);

/* CRC-16/CCITT-FALSE over d[0..n), n <= 32. */
uint64_t exs_redundantia_sarcinae(unsigned char *d, uint64_t n);

/* entry 23's frame verdict: 0 valid, 1 sync, 2 version, 3 CRC. */
uint64_t exs_lege(unsigned char *w);

_Noreturn void exsrt_abortus(unsigned kind);

#endif
