/* examples/dcfs_gate/proba.c -- a line-protocol host for dcfs_gate.exsc.
 *
 * stdin, one case per line:   FN HEX [LEN]
 *   FN   caput    the 17-byte header gate
 *        corpus   the whole-frame gate, buffer padded with zero bytes
 *        corpusg  the whole-frame gate, buffer padded with 0xA5 bytes
 *                 (the verdict must not depend on bytes past the frame)
 *   HEX  the input bytes in hex, or "-" for none
 *   LEN  optional: the `n` to pass, when it should not be the byte count
 *        (a lying or enormous length); decimal or 0x...
 * stdout, one line per case:  the verdict, decimal.
 *
 * The buffer handed to the gate is malloc'd at EXACTLY the capacity the header
 * names, so an ASan build (proba_c.sh makes one) turns a read past it into a
 * failure rather than a wrong answer that happens to match. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dcfs_gate.h"

_Noreturn void exsrt_abortus(unsigned kind)
{
  fprintf(stderr, "dcfs_gate: exsrt_abortus(%u)\n", kind);
  abort();
}

static int hexval(int c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

#define CAP_CAPUT 17u
#define CAP_CORPUS 65557u

/* the longest line: "corpusg " + 2 * 65557 hex digits + " " + a 20-digit length */
#define LINE_MAX_BYTES 140000

int main(void)
{
  static char line[LINE_MAX_BYTES];
  static char hex[LINE_MAX_BYTES];
  while (fgets(line, sizeof line, stdin)) {
    char fn[16], lenstr[40];
    lenstr[0] = 0;
    int got = sscanf(line, "%15s %139999s %39s", fn, hex, lenstr);
    if (got < 2) { printf("BADLINE\n"); continue; }

    size_t cap, hl = strcmp(hex, "-") == 0 ? 0 : strlen(hex);
    int garbage = 0;
    if (!strcmp(fn, "caput")) cap = CAP_CAPUT;
    else if (!strcmp(fn, "corpus")) cap = CAP_CORPUS;
    else if (!strcmp(fn, "corpusg")) { cap = CAP_CORPUS; garbage = 1; }
    else { printf("BADFN\n"); continue; }
    if (hl % 2) { printf("BADHEX\n"); continue; }

    size_t bytes = hl / 2;
    unsigned char *raw = malloc(bytes ? bytes : 1);
    if (!raw) return 3;
    int bad = 0;
    for (size_t i = 0; i < bytes; i++) {
      int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
      if (hi < 0 || lo < 0) { bad = 1; break; }
      raw[i] = (unsigned char)(hi * 16 + lo);
    }
    if (bad) { printf("BADHEX\n"); free(raw); continue; }

    uint64_t n = bytes;
    if (lenstr[0]) n = strtoull(lenstr, NULL, 0);

    unsigned char *buf = malloc(cap);           /* exactly cap bytes */
    if (!buf) return 3;
    memset(buf, garbage ? 0xA5 : 0x00, cap);
    memcpy(buf, raw, bytes < cap ? bytes : cap);
    uint64_t v = !strcmp(fn, "caput") ? exs_admitte_caput(buf, n) : exs_admitte_corpus(buf, n);
    free(buf);
    free(raw);
    printf("%llu\n", (unsigned long long)v);
  }
  return 0;
}
