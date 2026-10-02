/* examples/abortus/exempla.h -- the guarded wrappers exempla.c defines. Each
 * returns 0 and writes *r, or returns the trap's kind (tutela.h) and leaves
 * *r alone. Every pointer argument has the length the raw function's
 * `acies` parameter has: custos d = 32 bytes, arca h = 512 and b = 4096,
 * probatio v = 8. */
#ifndef EXSECUTOR_ABORTUS_EXEMPLA_H
#define EXSECUTOR_ABORTUS_EXEMPLA_H
#include <stdint.h>

unsigned custos_admitte_tutum(unsigned char *d, uint64_t n, uint64_t *r);
unsigned custos_redundantia_sarcinae_tutum(unsigned char *d, uint64_t n, uint64_t *r);
unsigned arca_saltus_tutum(uint64_t m, uint64_t *r);
unsigned arca_saltus_capitis_tutum(unsigned char *h, uint64_t *r);
unsigned arca_nomen_iudica_tutum(unsigned char *b, uint64_t n, uint64_t *r);
unsigned probatio_imple_tutum(unsigned char *v, uint64_t n, uint64_t *r);
unsigned probatio_circuitus_tutum(uint64_t n, uint64_t *r);
unsigned probatio_profunda_tutum(uint64_t k, uint64_t n, uint64_t *r);

#endif
