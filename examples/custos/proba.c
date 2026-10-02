/* examples/custos/proba.c -- the anchors for custos.exsc's gate, run against
 * the C unit exsc emits. One case per verdict, plus SUPERPACK_SPEC.md's
 * joint-CRC anchor (two zero-payload DATA frames -> 0x5B75). Every value was
 * produced by Punctim's Python reference (python/MCP/mediumlab_core.py and
 * superpack.py) and is written out here so this file needs neither.
 *
 * The full certification is the consumer's: Punctim's web/bridge runs the
 * same unit over its committed golden, SuperPack and medium vectors and a
 * differential sweep against its Rust reference codec. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "custos.h"

_Noreturn void exsrt_abortus(unsigned kind)
{
  fprintf(stderr, "custos: exsrt_abortus(%u)\n", kind);
  abort();
}

struct casus { const char *nomen, *hex; unsigned expect; };

static const struct casus casus[] = {
  {"filler frame",           "d310000000000000000000000000005b80", 0},
  {"type 15 frame",          "d31f1234beefcafe01020304abcdef3e77", 0},
  {"filler superpack",       "d315100000000000000000000000000010000000000000000000000000005b75", 0},
  {"mixed superpack",        "d3151f1234beefcafe01020304abcdef10000100020003616263640000049b65", 0},
  {"frame bad sync",         "d210000000000000000000000000005b80", 1},
  {"frame bad version",      "d320000000000000000000000000005b80", 2},
  {"frame bad crc",          "d310000000000000000000000000005b81", 3},
  {"superpack bad sync",     "d215100000000000000000000000000010000000000000000000000000005b75", 1},
  {"superpack bad version",  "d325100000000000000000000000000010000000000000000000000000005b75", 2},
  {"superpack bad crc",      "d315100000000000000000000000000010000000000000000000000000005b74", 3},
  {"32 bytes, type 4",       "d314100000000000000000000000000010000000000000000000000000005b75", 4},
  {"core A version 2",       "d31520000000000000000000000000001000000000000000000000000000e073", 5},
  {"core B version 2",       "d31510000000000000000000000000002000000000000000000000000000004d", 5},
  {"zero cores, joint crc ok", "d31500000000000000000000000000000000000000000000000000000000f480", 5},
  {"empty",                  "", 6},
  {"16 bytes",               "d310000000000000000000000000005b", 6},
  {"18 bytes",               "d310000000000000000000000000005b8000", 6},
  {"31 bytes",               "d315100000000000000000000000000010000000000000000000000000005b", 6},
};

int main(void)
{
  unsigned fail = 0, n = sizeof casus / sizeof casus[0];
  for (unsigned i = 0; i < n; i++) {
    unsigned char d[32];
    size_t len = strlen(casus[i].hex) / 2;
    if (len > sizeof d) { printf("%s: longer than 32 bytes\n", casus[i].nomen); return 1; }
    memset(d, 0xa5, sizeof d);
    for (size_t k = 0; k < len; k++) {
      unsigned v;
      sscanf(casus[i].hex + 2 * k, "%2x", &v);
      d[k] = (unsigned char)v;
    }
    unsigned got = (unsigned)exs_admitte(d, len);
    printf("%-26s %u%s\n", casus[i].nomen, got, got == casus[i].expect ? "" : "  <-- WRONG");
    fail += got != casus[i].expect;
  }
  /* SUPERPACK_SPEC.md's anchor, read as the CRC itself rather than a verdict. */
  {
    unsigned char d[32] = {0xd3, 0x15, 0x10};
    d[16] = 0x10;
    unsigned crc = (unsigned)exs_redundantia_sarcinae(d, 30);
    printf("%-26s 0x%04x%s\n", "joint crc anchor", crc, crc == 0x5b75 ? "" : "  <-- WRONG");
    fail += crc != 0x5b75;
  }
  printf("custos: %u/%u\n", n + 1 - fail, n + 1);
  return fail != 0;
}
