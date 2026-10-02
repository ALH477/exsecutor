/* examples/arca/arca.h -- the C face of arca.exsc (exsc --emitte c), for a C
 * or Rust host. Hand-written; proba_c.sh compiles it against the emitted
 * unit, so a signature that drifts is a compile error there.
 *
 * `h` points at one 512-byte header block, `l` at a 4096-byte long-name
 * buffer. Neither is written. A bounds or overflow trap reaches
 * exsrt_abortus, which the host supplies and which must not return. */
#ifndef EXSECUTOR_ARCA_H
#define EXSECUTOR_ARCA_H
#include <stdint.h>

uint64_t exs_caput_iudica(unsigned char *h, unsigned char *l, uint64_t nl, uint64_t habet);
uint64_t exs_magnitudo(unsigned char *h);   /* 0xffffffffffffffff = malformed */
uint64_t exs_saltus(uint64_t m);            /* m rounded up to 512 */
uint64_t exs_nomen_iudica(unsigned char *b, uint64_t n);

_Noreturn void exsrt_abortus(unsigned kind);
#endif
