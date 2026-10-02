/* examples/abortus/proba.c -- the host keeps running after a trap.
 *
 * Linked with three emitted units (custos, arca, probatio), tutela.c and
 * exempla.c. Every check prints one line; the last line is "abortus: P/N"
 * and the exit status is 0 only when P == N.
 *
 *   proba          all checks below, in one process
 *   proba nudus    one trap with NO guard open: must print
 *                  "exsecutor: abortus 1" and die of SIGABRT (proba_c.sh
 *                  checks the status), so the fail-stop default is kept
 *
 * What is checked:
 *   1. the real units still answer correctly through the guard;
 *   2. host-reachable traps in the real units return their kind: arca's
 *      saltus on magnitudo's sentinel (overflow), custos's CRC window and
 *      arca's name window read past their ends (bounds);
 *   3. after each trap the guard chain is empty and the SAME units still
 *      answer every custos anchor correctly -- nothing was left behind;
 *   4. kind 5 (terminus) is carried as 5, not folded into 1;
 *   5. a trap 1001 frames deep returns, and so does the next call;
 *   6. memory a callee writes through `&mutabilis` holds exactly the stores
 *      made before the trap, and the bounds check kept the trapping store
 *      off the guard bytes past the end (README.md, P3);
 *   7. nested guards: a trap goes to the innermost, the outer survives it,
 *      and a later unguarded-inner trap goes to the outer;
 *   8. 1,000,000 traps interleaved with 1,000,000 good calls, with the
 *      guard depth 0 and resident memory growing by less than 5 bytes a
 *      trap (a proxy: nothing in a library-mode unit can leak, and this is
 *      what would show it if something did);
 *   9. eight threads trapping concurrently, each on its own guard chain.
 */
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include "tutela.h"
#include "exempla.h"

uint64_t exs_saltus(uint64_t m);   /* raw, for `nudus` and check 7 */

static unsigned passed, total;

static void check(int ok, const char *what)
{
  total++;
  passed += ok != 0;
  printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
}

static void hex(unsigned char *d, const char *s)
{
  size_t n = strlen(s) / 2;
  for (size_t k = 0; k < n; k++) {
    unsigned v;
    sscanf(s + 2 * k, "%2x", &v);
    d[k] = (unsigned char)v;
  }
}

/* custos's anchors, the subset with one case per verdict (examples/custos/
 * proba.c has them all, from Punctim's Python reference). */
struct casus { const char *hex; unsigned expect; };
static const struct casus casus[] = {
  {"d310000000000000000000000000005b80", 0},
  {"d315100000000000000000000000000010000000000000000000000000005b75", 0},
  {"d210000000000000000000000000005b80", 1},
  {"d320000000000000000000000000005b80", 2},
  {"d310000000000000000000000000005b81", 3},
  {"d314100000000000000000000000000010000000000000000000000000005b75", 4},
  {"d31520000000000000000000000000001000000000000000000000000000e073", 5},
  {"d310000000000000000000000000005b", 6},
};

/* Every custos anchor through the guard. 1 if all agree. */
static int custos_anchors(void)
{
  for (size_t i = 0; i < sizeof casus / sizeof casus[0]; i++) {
    unsigned char d[32];
    uint64_t r = 99;
    memset(d, 0xa5, sizeof d);
    hex(d, casus[i].hex);
    if (custos_admitte_tutum(d, strlen(casus[i].hex) / 2, &r) != 0) return 0;
    if (r != casus[i].expect) return 0;
  }
  return 1;
}

/* ---- check 7: nested guards ---------------------------------------------- */

struct nidus { unsigned interior, profunditas_intus; uint64_t r; };
static void opus_nidus(void *p)
{
  struct nidus *n = p;
  /* an inner guarded trap returns to the inner guard */
  n->interior = arca_saltus_tutum(UINT64_MAX, &n->r);
  n->profunditas_intus = exs_tutela_profunditas();
  /* a raw trap here has only the outer guard to go to */
  n->r = exs_saltus(UINT64_MAX);
  n->interior = 1000;            /* never reached */
}

/* ---- check 9: threads ---------------------------------------------------- */

struct filum { unsigned id; unsigned errata; };
static void *opus_fili(void *p)
{
  struct filum *f = p;
  for (unsigned i = 0; i < 100000; i++) {
    uint64_t r = 0;
    uint64_t m = (uint64_t)(i * 8u + f->id);
    if (arca_saltus_tutum(m, &r) != 0 || r != ((m + 511) / 512) * 512) f->errata++;
    if (arca_saltus_tutum(UINT64_MAX - (m & 255), &r) != 1) f->errata++;
    if (probatio_circuitus_tutum(9 + (i & 3), &r) != 5) f->errata++;
    if (exs_tutela_profunditas() != 0) f->errata++;
  }
  return NULL;
}

static long rss_kb(void)
{
  struct rusage u;
  getrusage(RUSAGE_SELF, &u);
  return u.ru_maxrss;
}

int main(int argc, char **argv)
{
  if (argc == 2 && strcmp(argv[1], "nudus") == 0) {
    printf("nudus: %llu\n", (unsigned long long)exs_saltus(UINT64_MAX));
    return 0;                    /* reached only if the trap returned */
  }
  if (argc != 1) { fprintf(stderr, "usage: proba [nudus]\n"); return 2; }
  /* Line buffered, so a mutant that crashes the process leaves every line
   * before the crash for proba_c.sh to read. */
  setvbuf(stdout, NULL, _IOLBF, 0);

  uint64_t r;
  unsigned k;

  /* 1 */
  check(custos_anchors(), "custos: 8 anchors through the guard, one per verdict");
  r = 0; k = arca_saltus_tutum(1000, &r);
  check(k == 0 && r == 1024, "arca: saltus(1000) = 1024 through the guard");
  check(exs_tutela_profunditas() == 0, "guard depth 0 outside any guard");

  /* 2, 3 */
  {
    unsigned char h[512];
    memset(h, 'x', sizeof h);    /* size field not octal -> sentinel */
    r = 77; k = arca_saltus_capitis_tutum(h, &r);
    check(k == 1 && r == 77, "arca: saltus(magnitudo(garbage header)) traps, kind 1, *r untouched");
    check(exs_tutela_profunditas() == 0, "  guard chain empty after the trap");
    check(custos_anchors(), "  custos anchors still right after an arca trap");
  }
  {
    unsigned char d[32];
    memset(d, 0, sizeof d);
    k = custos_redundantia_sarcinae_tutum(d, 30, &r);
    check(k == 0, "custos: redundantia_sarcinae(d, 30) returns");
    k = custos_redundantia_sarcinae_tutum(d, 33, &r);
    check(k == 1, "custos: redundantia_sarcinae(d, 33) reads d[32]: trap, kind 1");
    check(exs_tutela_profunditas() == 0 && custos_anchors(),
          "  chain empty, custos anchors still right after a custos trap");
  }
  {
    static unsigned char b[4096];
    memset(b, 'a', sizeof b);
    k = arca_nomen_iudica_tutum(b, 4096, &r);
    check(k == 0 && r == 0, "arca: nomen_iudica(4096 x 'a') = 0");
    k = arca_nomen_iudica_tutum(b, 4097, &r);
    check(k == 1, "arca: nomen_iudica(b, 4097) reads b[4096]: trap, kind 1");
  }

  /* 4 */
  k = probatio_circuitus_tutum(8, &r);
  check(k == 0 && r == 8, "probatio: circuitus(8) = 8 under terminus 8");
  k = probatio_circuitus_tutum(9, &r);
  check(k == 5, "probatio: circuitus(9) traps with kind 5, not 1");

  /* 5 */
  k = probatio_profunda_tutum(1000, 0, &r);
  check(k == 0 && r == 1007, "probatio: profunda(1000, 0) = 1007");
  k = probatio_profunda_tutum(1000, 4, &r);
  check(k == 1, "probatio: profunda(1000, 4) traps 1001 frames deep, kind 1");
  k = probatio_profunda_tutum(3, 1, &r);
  check(k == 0 && r == 10, "  and the next call returns (profunda(3, 1) = 10)");

  /* 6 */
  {
    unsigned char v[16];
    int prefix = 1, custodes = 1;
    memset(v, 0, 8);
    memset(v + 8, 0x5a, 8);      /* guard bytes past the 8-byte acies */
    k = probatio_imple_tutum(v, 12, &r);
    for (int i = 0; i < 8; i++) prefix &= v[i] == 0x41;
    for (int i = 8; i < 16; i++) custodes &= v[i] == 0x5a;
    check(k == 1, "probatio: imple_et_cade(v, 12) traps, kind 1");
    check(prefix, "  v[0..8) holds the 8 stores made before the trap (P3)");
    check(custodes, "  v[8..16) untouched: the trapping store was never made");
  }

  /* 7 */
  {
    struct nidus n = { 0, 0, 0 };
    k = exs_tutela_curre(opus_nidus, &n);
    check(n.interior == 1 && n.profunditas_intus == 1,
          "nested: the inner trap returned to the inner guard, the outer still open");
    check(k == 1, "nested: a raw trap inside the outer guard returned to it, kind 1");
    check(exs_tutela_profunditas() == 0, "  guard depth 0 after both");
  }

  /* 8 */
  {
    long before, after;
    unsigned errata = 0;
    for (unsigned i = 0; i < 1000; i++) {      /* warm up before measuring */
      (void)arca_saltus_tutum(UINT64_MAX, &r);
      (void)probatio_profunda_tutum(20, 9, &r);
    }
    before = rss_kb();
    for (unsigned i = 0; i < 1000000; i++) {
      unsigned char d[32] = {0};
      if (arca_saltus_tutum(UINT64_MAX - (i & 255), &r) != 1) errata++;
      if (custos_redundantia_sarcinae_tutum(d, 30, &r) != 0) errata++;
    }
    after = rss_kb();
    check(errata == 0 && exs_tutela_profunditas() == 0,
          "1,000,000 traps and 1,000,000 good calls, every one answered as expected");
    char line[128];
    /* The bound is per trap, not a guess at the allocator: anything a trap
     * left behind would be at least a guard (a jmp_buf alone is 200 bytes
     * on x86-64 glibc), so 1,000,000 traps would add 190 MiB or more.
     * 4 MiB is under 5 bytes a trap; ASan's own bookkeeping measured about
     * 1 MiB here, the plain builds well under that. */
    snprintf(line, sizeof line, "  max RSS %ld KiB -> %ld KiB (bound: +4096 KiB, < 5 bytes a trap)", before, after);
    check(after - before <= 4096, line);
  }

  /* 9 */
  {
    pthread_t t[8];
    struct filum f[8];
    unsigned errata = 0;
    for (unsigned i = 0; i < 8; i++) {
      f[i].id = i; f[i].errata = 0;
      if (pthread_create(&t[i], NULL, opus_fili, &f[i]) != 0) { errata++; f[i].id = 99; }
    }
    for (unsigned i = 0; i < 8; i++) {
      if (f[i].id != 99) pthread_join(t[i], NULL);
      errata += f[i].errata;
    }
    check(errata == 0, "8 threads x 100,000 rounds of (good, overflow trap, terminus trap)");
  }

  printf("abortus: %u/%u\n", passed, total);
  return passed == total ? 0 : 1;
}
