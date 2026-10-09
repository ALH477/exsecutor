/* examples/dcfs_gate/dcfs_gate.h -- the C face of dcfs_gate.exsc
 * (exsc --emitte c), for a C or Rust host. Hand-written; proba_c.sh compiles
 * it against the emitted unit and compares it with the generated header
 * (tests/c/facies/protos.py), so a signature that drifts is a build error there.
 *
 * Every integer is uint64_t. A buffer is a pointer to EXACTLY the size named
 * below, all of it readable, none of it written:
 *   exs_admitte_caput    b = 17 bytes   (the frame header)
 *   exs_admitte_corpus   b = 65557 bytes (17 header + 65536 payload + 4 CRC)
 * `n` is the number of bytes the frame really has (for caput: the TOTAL, which
 * may be anything up to 2^64-1; only the first 17 bytes are ever read). A host
 * copies its input into a zero-padded buffer of that size; no byte at index >= n
 * is consulted, so the padding value is irrelevant. Both functions are total:
 * nothing a caller passes traps.
 *
 * Verdicts: 0 admitted; the rest are the first failing check, tabulated at the
 * top of dcfs_gate.exsc. A trap reaches exsrt_abortus, which the host supplies
 * and which must not return. */
#ifndef EXSECUTOR_DCFS_GATE_H
#define EXSECUTOR_DCFS_GATE_H
#include <stdint.h>

uint64_t exs_admitte_caput(unsigned char *b, uint64_t n);
uint64_t exs_admitte_corpus(unsigned char *b, uint64_t n);

_Noreturn void exsrt_abortus(unsigned kind);
#endif
