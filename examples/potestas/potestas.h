/* examples/potestas/potestas.h -- the C face of potestas.exsc (exsc --emitte
 * c), for a C or Rust host. Hand-written; proba_c.sh compiles it against the
 * emitted unit, so a signature that drifts is a compile error there.
 *
 * Buffers are read, never written. An id buffer `b` must point at 64
 * readable bytes and a path buffer at 4096; bytes at and past the length are
 * not read, and a length past the buffer is answered before any byte is.
 * A bounds or overflow trap reaches exsrt_abortus, which the host supplies
 * and which must not return. Nothing a caller can pass traps.
 *
 * The private helpers the unit also defines (exs_proxima, exs_finis,
 * exs_par) are not part of this face. */
#ifndef EXSECUTOR_POTESTAS_H
#define EXSECUTOR_POTESTAS_H
#include <stdint.h>

/* manifest::check_id. 0 admitted, 1 empty, 2 over 64 bytes, 3 a byte
 * outside [A-Za-z0-9_-]. */
uint64_t exs_titulus_iudica(unsigned char *b, uint64_t n);

/* policy.rs: `path.split('/').any(|c| c == "..")`. 1 or 0. */
uint64_t exs_habet_regressum(unsigned char *b, uint64_t n);

/* policy::is_under(p, f). 1 or 0. */
uint64_t exs_subest(unsigned char *p, uint64_t np, unsigned char *f, uint64_t nf);

/* The lexical half of policy::overlaps(p, f). 0 disjoint, 1 p is under f,
 * 2 p contains f, 3 a side longer than 4096 bytes (refuse). */
uint64_t exs_tangit(unsigned char *p, uint64_t np, unsigned char *f, uint64_t nf);

/* Manifest::validate's fs-capability anchor rule. 1 anchored, 0 refused. */
uint64_t exs_ancora(unsigned char *p, uint64_t n);

/* tier: 0 wasm, 1 native, 2 lua, 3 microvm. jit: 0 none, 1 host, 2 self.
 * 1 yes, 0 no, 2 an unknown code. */
uint64_t exs_involucrum(uint64_t tier);                 /* Manifest::uses_bwrap */
uint64_t exs_wx_cogitur(uint64_t tier, uint64_t jit);   /* Manifest::wx_enforced */
uint64_t exs_wx_conceditur(uint64_t tier, uint64_t jit);/* Manifest::grants_wx_to_plugin */

_Noreturn void exsrt_abortus(unsigned kind);
#endif
