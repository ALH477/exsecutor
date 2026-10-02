/* examples/arca/proba.c -- a host for arca.exsc, the loop reliquary runs in
 * Rust, written in C so the unit can be exercised here. Reads a tar stream on
 * stdin; prints one line per admitted member ("F name" / "D name"), then
 * "ADMIT" or "REFUSE <verdict> @<offset>". Exit 0 on ADMIT, 1 on REFUSE.
 *
 * The host's whole job: read blocks, obey verdicts. After the first zero
 * block -- where GNU tar stops -- the rest of the stream must be zero bytes;
 * a gate that stopped reading where tar stops reading, but let the stream go
 * on, would admit an archive whose tail it never saw. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arca.h"

_Noreturn void exsrt_abortus(unsigned kind)
{
  fprintf(stderr, "arca: exsrt_abortus(%u)\n", kind);
  abort();
}

static unsigned long long off;

static size_t namelen(const unsigned char *h)   /* strnlen(h, 100), in C11 */
{
  size_t n = 0;
  while (n < 100 && h[n]) n++;
  return n;
}

static int fill(unsigned char *b, size_t n)
{
  size_t got = fread(b, 1, n, stdin);
  off += got;
  return got == n;
}

static int refuse(unsigned v)
{
  printf("REFUSE %u @%llu\n", v, off);
  return 1;
}

int main(void)
{
  unsigned char h[512], l[4096];
  unsigned long long nl = 0, habet = 0;
  for (;;) {
    if (!fill(h, 512)) return refuse(100);              /* truncated */
    unsigned v = (unsigned)exs_caput_iudica(h, l, nl, habet);
    unsigned long long m = exs_magnitudo(h);
    if (v == 3) {
      int c;
      while ((c = getchar()) != EOF) { off++; if (c) return refuse(101); }
      printf("ADMIT\n");
      return 0;
    }
    if (v == 2) {                                       /* long name */
      unsigned long long pad = exs_saltus(m), i;
      memset(l, 0, sizeof l);
      for (i = 0; i < pad; i++) {
        int c = getchar();
        if (c == EOF) return refuse(100);
        off++;
        if (i < m) l[i] = (unsigned char)c;
      }
      nl = m; habet = 1;
      continue;
    }
    if (v > 1) return refuse(v);
    {
      const unsigned char *nm = habet ? l : h;
      size_t n = habet ? (size_t)nl - 1 : namelen(h);
      printf("%c %.*s\n", v == 1 ? 'D' : 'F', (int)n, nm);
    }
    habet = 0; nl = 0;
    {
      unsigned long long pad = exs_saltus(m), i;
      for (i = 0; i < pad; i++) {
        if (getchar() == EOF) return refuse(100);
        off++;
      }
    }
  }
}
