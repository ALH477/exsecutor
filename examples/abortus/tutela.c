/* examples/abortus/tutela.c -- the guard behind tutela.h: exsrt_abortus that
 * returns to the host instead of ending the process, for a HOSTED C host of a
 * library-mode unit. README.md carries the soundness argument; the comments
 * here say which C11 rule each line answers to.
 *
 * Compile this file on its own, never with the unit's flags merged into it
 * and never under LTO with the unit: exs_tutela_curre holds the program's one
 * setjmp, and the argument below is about that one frame. (GCC refuses to
 * inline a function that calls setjmp; separate compilation makes the same
 * true of every compiler without relying on that.)
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "tutela.h"

struct exs_tutela {
  jmp_buf locus;                 /* written by setjmp, read by longjmp only */
  struct exs_tutela *prior;      /* the guard this one is nested in */
  /* Written by exsrt_abortus between the setjmp and the longjmp, then read
   * by exs_tutela_curre after it. C11 7.13.2.1p3: an automatic object of
   * the setjmp caller that is changed in that interval and is NOT volatile
   * has an indeterminate value after the longjmp. This one is volatile. */
  volatile unsigned genus;
};

/* The innermost open guard of this thread. Static storage duration, so
 * 7.13.2.1p3 does not apply to it at all. */
static _Thread_local struct exs_tutela *summa;
static _Thread_local unsigned profunditas;

unsigned exs_tutela_curre(exs_opus *opus, void *ctx)
{
  struct exs_tutela t;
  t.prior = summa;               /* set BEFORE setjmp, never changed after */
  t.genus = 0;
  summa = &t;
  profunditas++;
  /* 7.13.1.1p4: setjmp only as the whole controlling expression, or one
   * operand of == against an integer constant. This is the second form. */
  if (setjmp(t.locus) == 0) {
    opus(ctx);
    /* Back by return. The chain must be exactly as it was opened: anything
     * else means a guard escaped its frame, and a later trap would longjmp
     * into a dead frame (7.13.2.1p2, undefined). Stop rather than risk it. */
    if (summa != &t) {
      fputs("exsecutor: tutela: guard chain corrupted\n", stderr);
      abort();
    }
    summa = t.prior;
    profunditas--;
    return 0;
  }
  /* Back by longjmp. exsrt_abortus already unlinked t and decremented the
   * depth; the only object read here is t.genus, which is volatile. */
  return t.genus;
}

/* The counter and the chain are kept separately on purpose: a guard that was
 * not unlinked leaves the counter right and the chain wrong, and this is
 * where that shows -- as EXS_TUTELA_GENUS_NULLUS, never as a plausible
 * depth. Only the head pointer is compared, never dereferenced, so a dead
 * guard is detected without reading its frame. */
unsigned exs_tutela_profunditas(void)
{
  if ((profunditas == 0) != (summa == NULL)) return EXS_TUTELA_GENUS_NULLUS;
  return profunditas;
}

_Noreturn void exsrt_abortus(unsigned kind)
{
  struct exs_tutela *t = summa;
  if (t != NULL) {
    /* Unlink first, so the chain is right the moment the guard resumes and
     * a trap during anything the host does next goes to the next guard out. */
    summa = t->prior;
    profunditas--;
    t->genus = kind != 0 ? kind : EXS_TUTELA_GENUS_NULLUS;
    /* 7.13.2.1p2: t's frame (exs_tutela_curre) is live -- it is below us on
     * this thread's stack, because a guard is unlinked before its frame
     * returns -- and the jump is on the thread that called setjmp, since the
     * chain is thread-local. The value 1 is never read; t->genus is. */
    longjmp(t->locus, 1);
  }
  /* No guard: fail-stop, as every other host of a unit in examples/ does. */
  fprintf(stderr, "exsecutor: abortus %u\n", kind);
  abort();
}
