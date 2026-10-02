/* prototypes/beneath/sigillum.c -- ADR 0019's seal, measured.
 *
 * The process seals itself with Landlock exactly as ADR 0019 Decision 1
 * proposes: prctl(PR_SET_NO_NEW_PRIVS), a ruleset that HANDLES every
 * Landlock ABI-1 create and remove right (0x1ff0), one PATH_BENEATH rule
 * GRANTING only MAKE_DIR|MAKE_REG (0x180) beneath the root's descriptor,
 * restrict_self. Then it tries every create/remove/link shape. The three
 * constants are checked against the UAPI headers at compile time (L1-L3).
 * Every row states its expectation; **UNEXPECTED** on disagreement.
 * Instrument, not prelude. Never on the build closure.
 * Usage: cc -O1 -Wall -o sigillum sigillum.c && ./sigillum   (scratch dir)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <linux/openat2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
_Static_assert((LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG) == 0x180, "L2 constant");
_Static_assert(LANDLOCK_RULE_PATH_BENEATH == 1, "L3 constant");
_Static_assert(PR_SET_NO_NEW_PRIVS == 38, "L1 constant");
static int bad;
static void row(const char *w, long r, int want) {
  int got = r < 0 ? errno : 0;
  printf("%-52s %-12s%s\n", w, got ? strerror(got) : "ok", got == want ? "" : "  **UNEXPECTED**");
  bad += got != want;
}
int main(void) {
  if (system("rm -rf S && mkdir -p S/root S/out && echo secret > S/out/o")) return 2;
  int root = open("S/root", O_PATH | O_DIRECTORY | O_CLOEXEC);
  uint64_t handled = 0x1ff0;                     /* MAKE_* and REMOVE_*, ABI 1 */
  row("prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)", prctl(38, 1, 0, 0, 0), 0);
  long rs = syscall(SYS_landlock_create_ruleset, &handled, 8, 0);
  row("landlock_create_ruleset(&0x1ff0, 8, 0)", rs, 0);
  struct landlock_path_beneath_attr pb = {.allowed_access = 0x180, .parent_fd = root};
  row("landlock_add_rule(rs, PATH_BENEATH, &pb, 0)", syscall(SYS_landlock_add_rule, rs, 1, &pb, 0), 0);
  row("landlock_restrict_self(rs, 0)", syscall(SYS_landlock_restrict_self, rs, 0), 0);
  close(rs);
  struct open_how c = {.flags = O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC, .mode = 0600, .resolve = 0x0b};
  row("sealed: crea beneath root (openat2 how_crea)", syscall(SYS_openat2, root, "f", &c, sizeof c), 0);
  row("sealed: mkdirat beneath root", mkdirat(root, "d", 0700), 0);
  row("sealed: mkdirat(AT_FDCWD, \"S/out/x\") outside", mkdirat(AT_FDCWD, "S/out/x", 0700), EACCES);
  row("sealed: open(O_CREAT) outside, plain openat", open("S/out/y", O_WRONLY|O_CREAT|O_EXCL, 0600), EACCES);
  row("sealed: read outside still allowed (not handled)", open("S/out", O_RDONLY|O_DIRECTORY), 0);
  row("sealed: mknodat FIFO beneath root", mknodat(root, "p", S_IFIFO|0600, 0), EACCES);
  row("sealed: symlinkat beneath root", symlinkat("/etc", root, "l"), EACCES);
  row("sealed: unlinkat a file beneath root", unlinkat(root, "f", 0), EACCES);
  row("sealed: unlinkat a dir beneath root", unlinkat(root, "d", AT_REMOVEDIR), EACCES);
  row("sealed: linkat same dir beneath root (MAKE_REG granted)", linkat(root, "f", root, "h", 0), 0);
  row("sealed: linkat outside file INTO root (cross-dir)", linkat(AT_FDCWD, "S/out/o", root, "h2", 0), EXDEV);
  row("sealed: renameat outside file INTO root", renameat(AT_FDCWD, "S/out/o", root, "r2"), EACCES);
  printf("\n%d unexpected\n", bad);
  return bad != 0;
}
_Static_assert((LANDLOCK_ACCESS_FS_REMOVE_DIR|LANDLOCK_ACCESS_FS_REMOVE_FILE|LANDLOCK_ACCESS_FS_MAKE_CHAR|LANDLOCK_ACCESS_FS_MAKE_DIR|LANDLOCK_ACCESS_FS_MAKE_REG|LANDLOCK_ACCESS_FS_MAKE_SOCK|LANDLOCK_ACCESS_FS_MAKE_FIFO|LANDLOCK_ACCESS_FS_MAKE_BLOCK|LANDLOCK_ACCESS_FS_MAKE_SYM) == 0x1ff0, "handled set");
