/* examples/dcf_net_gate/proba.c -- a line-protocol host for dcf_net_gate.exsc.
 *
 * stdin, one case per line:   FN HEX [LEN]
 *   FN   ipv4 | ordo | porta | intervallum
 *   HEX  the input bytes in hex, or "-" for none
 *   LEN  optional: the `n` to pass, when it should not be the byte count
 *        (an over-long or lying length)
 * stdout, one line per case:  the verdict, decimal.
 *
 * The buffer handed to the gate is malloc'd at EXACTLY the capacity the
 * header names, so an ASan build (proba_c.sh makes one) turns a read past it
 * into a failure rather than a wrong answer that happens to match. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dcf_net_gate.h"

_Noreturn void exsrt_abortus(unsigned kind)
{
  fprintf(stderr, "dcf_net_gate: exsrt_abortus(%u)\n", kind);
  abort();
}

static int hexval(int c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int main(void)
{
  static char line[4096];
  while (fgets(line, sizeof line, stdin)) {
    char fn[32], hex[3000], lenstr[40];
    lenstr[0] = 0;
    int got = sscanf(line, "%31s %2999s %39s", fn, hex, lenstr);
    if (got < 2) { printf("BADLINE\n"); continue; }
    size_t bytes = 0;
    unsigned char raw[1500];
    if (strcmp(hex, "-") != 0) {
      size_t hl = strlen(hex);
      if (hl % 2 || hl / 2 > sizeof raw) { printf("BADHEX\n"); continue; }
      for (size_t i = 0; i < hl; i += 2) {
        int hi = hexval(hex[i]), lo = hexval(hex[i + 1]);
        if (hi < 0 || lo < 0) { bytes = (size_t)-1; break; }
        raw[bytes++] = (unsigned char)(hi * 16 + lo);
      }
      if (bytes == (size_t)-1) { printf("BADHEX\n"); continue; }
    }
    uint64_t n = bytes;
    if (lenstr[0]) n = strtoull(lenstr, NULL, 0);

    size_t cap;
    if (!strcmp(fn, "ipv4") || !strcmp(fn, "ordo")) cap = 16;
    else if (!strcmp(fn, "porta") || !strcmp(fn, "intervallum")) cap = 8;
    else { printf("BADFN\n"); continue; }

    unsigned char *buf = calloc(1, cap);      /* exactly cap bytes, zeroed */
    if (!buf) return 3;
    memcpy(buf, raw, bytes < cap ? bytes : cap);
    uint64_t v;
    if (!strcmp(fn, "ipv4")) v = exs_admitte_ipv4(buf, n);
    else if (!strcmp(fn, "ordo")) v = exs_ordo_ipv4(buf, n);
    else if (!strcmp(fn, "porta")) v = exs_admitte_portum(buf, n);
    else v = exs_admitte_intervallum(buf, n);
    free(buf);
    printf("%llu\n", (unsigned long long)v);
  }
  return 0;
}
