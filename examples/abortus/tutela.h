/* examples/abortus/tutela.h -- a returning trap for a HOSTED C host of a
 * library-mode unit (exsc --emitte c). See README.md for the soundness
 * argument; this header is the API only.
 *
 * A unit imports one symbol, `_Noreturn void exsrt_abortus(unsigned kind)`,
 * and calls it on every bounds, overflow and `terminus` trap. tutela.c
 * defines that symbol: when the calling thread has a guard open, it jumps
 * back to the guard, which returns the trap's kind; with no guard open it
 * writes `exsecutor: abortus N` to stderr and calls abort(), which is the
 * fail-stop behaviour every other host in examples/ has.
 *
 * Hosted only. <setjmp.h> is not among C11's freestanding headers (C11 4p6),
 * so none of this exists in a kernel: README.md, "Not in a kernel".
 *
 * Use. Wrap each exported function you call in a thunk that takes one
 * context pointer, and run the thunk through exs_tutela_curre:
 *
 *     struct ctx { unsigned char *d; uint64_t n; uint64_t r; };
 *     static void opus(void *p) { struct ctx *c = p; c->r = exs_admitte(c->d, c->n); }
 *     unsigned custos_admitte_tutum(unsigned char *d, uint64_t n, uint64_t *r)
 *     {
 *       struct ctx c = { d, n, 0 };
 *       unsigned k = exs_tutela_curre(opus, &c);
 *       if (k == 0) *r = c.r;
 *       return k;
 *     }
 *
 * The thunk and the wrapper contain no setjmp; the one setjmp in the program
 * is inside exs_tutela_curre, so C11 7.13.2.1p3's rule about a setjmp
 * caller's locals is discharged once, in tutela.c, and not per wrapper.
 *
 * Preconditions, each argued in README.md:
 *   P1  between exs_tutela_curre and the trap there are only frames of
 *       emitted Exsecutor C and the thunk -- no frame of C++, of Rust, or of
 *       C that holds a lock or a resource;
 *   P2  the unit is library mode (no object header, no allocation, no
 *       mutable global), which is every unit --emitte c emits today;
 *   P3  after a nonzero return, memory the callee could write through a
 *       pointer (`&mutabilis` parameters) holds whatever had been stored
 *       before the trap, and the host must treat it as garbage.
 */
#ifndef EXSECUTOR_TUTELA_H
#define EXSECUTOR_TUTELA_H

/* What exs_tutela_curre returns when exsrt_abortus was called with kind 0,
 * which no backend emits (prelude/README.md's kinds are 1 to 5): a trap
 * must never read as success. */
#define EXS_TUTELA_GENUS_NULLUS 0xffffffffu

typedef void exs_opus(void *ctx);

/* Run opus(ctx) under a guard on the calling thread. Returns 0 when opus
 * returned, or the kind of the trap that ended it (1 numeric or bounds, 5
 * terminus; 2-4 belong to the reference runtime and do not occur in library
 * mode). Guards nest: a trap returns to the innermost open guard. Thread-safe:
 * each thread has its own chain of guards. */
unsigned exs_tutela_curre(exs_opus *opus, void *ctx);

/* How many guards the calling thread has open. 0 outside every
 * exs_tutela_curre; a test reads it after a trap to see the chain restored.
 * EXS_TUTELA_GENUS_NULLUS if the count and the chain disagree. */
unsigned exs_tutela_profunditas(void);

/* The unit's one import. Defined in tutela.c. */
_Noreturn void exsrt_abortus(unsigned kind);

#endif
