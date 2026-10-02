/* examples/potestas/proba.c -- the anchors for potestas.exsc, run against
 * the C unit exsc emits. Every path, id and table row below is a case from
 * plugind's own Rust tests (modules/oligarchy-plugins/host/src/policy.rs and
 * manifest.rs in Oligarchy), or a boundary of the rule those functions
 * state, written out with the answer the Rust gives so this file needs
 * neither Rust nor Oligarchy.
 *
 * The full certification is the consumer's: Oligarchy's
 * modules/oligarchy-plugins/potestas-cert links the same unit and compares
 * it with plugind's own check_id, Policy::authorize and Manifest::wx_enforced
 * on a generated corpus. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "potestas.h"

_Noreturn void exsrt_abortus(unsigned kind)
{
  fprintf(stderr, "potestas: exsrt_abortus(%u)\n", kind);
  abort();
}

static unsigned char via[4096], prefixum[4096], titulus[64];
static int ok, fail;

static void show(const char *s)
{
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x20 || c >= 0x7f) printf("\\x%02x", c); else putchar(c);
  }
}

static void check(const char *what, const char *a, const char *b, uint64_t got, uint64_t want)
{
  printf(got == want ? "  [ok]   %s(\"" : "  [FAIL] %s(\"", what);
  show(a);
  if (b) { printf("\", \""); show(b); }
  printf("\") = %llu", (unsigned long long)got);
  if (got == want) { ok++; printf("\n"); }
  else { fail++; printf(", want %llu\n", (unsigned long long)want); }
}

static uint64_t load(unsigned char *buf, size_t cap, const char *s)
{
  size_t n = strlen(s);
  memset(buf, 0, cap);
  memcpy(buf, s, n < cap ? n : cap);
  return n;
}

static void sub(const char *p, const char *f, uint64_t want)
{
  uint64_t np = load(via, sizeof via, p), nf = load(prefixum, sizeof prefixum, f);
  check("subest", p, f, exs_subest(via, np, prefixum, nf), want);
}

static void tng(const char *p, const char *f, uint64_t want)
{
  uint64_t np = load(via, sizeof via, p), nf = load(prefixum, sizeof prefixum, f);
  check("tangit", p, f, exs_tangit(via, np, prefixum, nf), want);
}

static void anc(const char *p, uint64_t want)
{
  uint64_t n = load(via, sizeof via, p);
  check("ancora", p, NULL, exs_ancora(via, n), want);
}

static void tit(const char *id, uint64_t want)
{
  uint64_t n = load(titulus, sizeof titulus, id);
  check("titulus_iudica", id, NULL, exs_titulus_iudica(titulus, n), want);
}

int main(void)
{
  /* policy.rs is_under_compares_components_not_bytes */
  sub("/home/asher/.ssh", "/home", 1);
  sub("//home/asher/.ssh", "/home", 1);
  sub("/./home/asher/.ssh", "/home", 1);
  sub("/homework/notes", "/home", 0);
  sub("/home", "/home", 1);
  sub("/tmp/../home", "/anything", 1);
  /* policy.rs relative_cap_spellings_are_refused_unconditionally */
  sub("proc", "/proc", 1);
  sub("proc/self/mem", "/proc", 1);
  sub("etc/shadow", "/home", 1);
  sub("home/asher/.ssh", "/does/not/matter", 1);
  sub("$STATE", "/home", 0);
  sub("//proc/self/mem", "/proc", 1);
  sub("/opt/plugin/data", "/home", 0);
  /* Boundaries of Path::components as is_under reads it. */
  sub("", "/x", 1);              /* empty: relative, refused */
  sub("/x", "", 1);              /* an empty prefix has no components */
  sub("/x", "x", 0);             /* RootDir against Normal */
  sub("$STATE/x", "$STATE", 1);
  sub("$STATE/x", "$STATE/./", 1);
  sub("$STATE", "./$STATE", 0);  /* a leading "." of a relative prefix is CurDir */
  sub("$STATE", ".", 0);
  sub("/a", "/a/..", 0);         /* ParentDir in the prefix matches nothing */
  sub("/a/b", "/a/./b/", 1);
  sub("/a/b", "/a//b", 1);
  sub("/", "/", 1);
  sub("/", "//", 1);
  sub("/a", "/a/b", 0);
  sub("/...", "/...", 1);        /* "..." is a name */
  sub("/a/..b", "/a", 1);
  sub("/a/.b", "/a/.b/", 1);
  sub("$STATE/..", "/x", 1);
  sub("/x/.", "/x", 1);
  sub("/x/", "/x/.", 1);
  sub("/ab", "/a", 0);
  sub("/a", "/ab", 0);

  /* policy.rs proc_is_refused / an_ancestor_of_a_forbidden_path_is_refused /
   * forbidden_paths_are_prefixes, against the default forbidden set. */
  tng("/proc", "/proc", 1);
  tng("/proc/self/mem", "/proc", 1);
  tng("/proc/1/mem", "/proc", 1);
  tng("//proc/cpuinfo", "/proc", 1);
  tng("/run/secrets/guitar-key", "/run/secrets", 1);
  tng("/", "/proc", 2);
  tng("/etc", "/etc/ssh", 2);
  tng("/run", "/run/secrets", 2);
  tng("/var/lib", "/var/lib/sops", 2);
  tng("//", "/proc", 2);
  tng("/./etc", "/etc/shadow", 2);
  tng("/srv/audio", "/proc", 0);
  tng("/etc-like", "/etc/ssh", 0);
  tng("/homework", "/home", 0);
  tng("$STATE/x", "/home", 0);
  tng("proc", "/proc", 1);
  tng("$STATE", "$STATE/x", 0);  /* a '$' path is never an ancestor */
  tng("/x", "/x/../y", 2);       /* the forbidden side's ".." is refused too */
  tng("/x", "rel", 2);           /* as is a relative forbidden entry */
  tng("/x", "", 1);
  tng("", "/proc", 1);
  {
    uint64_t nf = load(prefixum, sizeof prefixum, "/proc");
    memset(via, '/', sizeof via);
    check("tangit", "<4097 bytes>", "/proc", exs_tangit(via, 4097, prefixum, nf), 3);
    check("tangit", "<4096 slashes>", "/proc", exs_tangit(via, 4096, prefixum, nf), 2);
  }

  /* manifest.rs fs_caps_must_be_anchored */
  anc("/usr/share/fonts", 1);
  anc("$STATE/ro", 1);
  anc("$CONFIG", 1);
  anc("$STORE/lib", 1);
  anc("proc", 0);
  anc("proc/self/mem", 0);
  anc("etc/shadow", 0);
  anc("", 0);
  anc("$STATEX", 1);             /* starts_with, not a component test */
  anc("$STAT", 0);
  anc("$CONFI", 0);
  anc("$STOR", 0);
  anc("$HOME", 0);
  anc("$", 0);
  anc("%STATE/ro", 0);           /* the anchor is "$STATE", not "?STATE" */
  anc("xCONFIG", 0);

  /* manifest.rs rejects_empty_and_overlong_id and the 5de841e ids */
  {
    char a65[66], a64[65], s65[66];
    memset(a65, 'a', 65); a65[65] = 0;
    memset(a64, 'a', 64); a64[64] = 0;
    memset(s65, '/', 65); s65[65] = 0;
    tit("", 1);
    tit(a65, 2);
    tit(a64, 0);
    tit(s65, 2);                 /* the length is judged first */
  }
  tit("evil", 0);
  tit("A-z_09", 0);
  tit("X.service.d/../../../../etc/systemd/system/sshd", 3);
  tit("a b", 3);
  tit("a\n", 3);
  tit("\xc3\xa9", 3);            /* U+00E9: alphabetic, not ASCII */
  tit("-", 0);
  tit("@", 3);
  tit("[", 3);
  tit("`", 3);
  tit("{", 3);
  tit("/", 3);
  tit(":", 3);

  /* manifest.rs wx_enforced / uses_bwrap / grants_wx_to_plugin, all 12 rows */
  {
    static const char *ord[] = {"wasm", "native", "lua", "microvm"};
    static const char *jit[] = {"none", "host", "self"};
    static const unsigned wx[4][3] = {{0, 0, 0}, {1, 1, 0}, {1, 1, 0}, {1, 1, 1}};
    static const unsigned cn[4][3] = {{0, 0, 0}, {0, 0, 1}, {0, 0, 1}, {0, 0, 0}};
    static const unsigned bw[4] = {0, 1, 1, 0};
    for (unsigned t = 0; t < 4; t++) {
      check("involucrum", ord[t], NULL, exs_involucrum(t), bw[t]);
      for (unsigned j = 0; j < 3; j++) {
        check("wx_cogitur", ord[t], jit[j], exs_wx_cogitur(t, j), wx[t][j]);
        check("wx_conceditur", ord[t], jit[j], exs_wx_conceditur(t, j), cn[t][j]);
      }
    }
    check("involucrum", "4", NULL, exs_involucrum(4), 2);
    check("wx_cogitur", "4", "0", exs_wx_cogitur(4, 0), 2);
    check("wx_cogitur", "0", "3", exs_wx_cogitur(0, 3), 2);
    check("wx_conceditur", "1", "3", exs_wx_conceditur(1, 3), 2);
  }

  printf("potestas: %d ok, %d failed\n", ok, fail);
  return fail ? 1 : 0;
}
