/* prototypes/beneath/mkdirat.c -- what mkdirat(2) does to a name, measured.
 *
 * The evidence under ADR 0018. mkdirat takes no RESOLVE_* flags: it resolves
 * its path with the ordinary walk from the dirfd it is given. This probe
 * measures which names escape a directory opened with openat2(B|M|X), which
 * the kernel refuses on its own, and what a held parent descriptor does when
 * the directory it names is moved. Every row states the expectation it is
 * judged against and prints **UNEXPECTED** when the kernel disagrees.
 *
 * Instrument, not prelude: libc is used freely. Never on the build closure.
 * Usage: cc -O1 -Wall -o mkdirat mkdirat.c && ./mkdirat   (in a scratch dir)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int unexpected;

static int beneath(int dfd, const char *p) {
    struct open_how h = {.flags = O_PATH | O_DIRECTORY | O_CLOEXEC, .resolve = 0x0b};
    return (int)syscall(SYS_openat2, dfd, p, &h, sizeof h);
}

/* want: 0 = must succeed, otherwise the errno it must fail with */
static void row(const char *what, int r, int want) {
    int got = r < 0 ? errno : 0;
    printf("%-62s %-14s%s\n", what, got ? strerror(got) : "created",
           got == want ? "" : "  **UNEXPECTED**");
    unexpected += got != want;
}

static int exists(const char *p) { struct stat st; return lstat(p, &st) == 0; }

static void fresh(void) {
    if (system("rm -rf T && mkdir -p T/root/sub T/outside && "
               "ln -s ../outside T/root/up && ln -s ../outside/nope T/root/dangle"))
        exit(2);
}

int main(void) {
    fresh();
    int root = open("T/root", O_PATH | O_DIRECTORY | O_CLOEXEC);
    int sub = beneath(root, "sub");
    if (root < 0 || sub < 0) { perror("setup"); return 2; }

    puts("== multi-component names: the ordinary walk, so they escape ==");
    row("mkdirat(sub, \"../../outside/esc1\")", mkdirat(sub, "../../outside/esc1", 0700), 0);
    row("mkdirat(root, \"up/esc2\")  through an escaping symlink", mkdirat(root, "up/esc2", 0700), 0);
    if (!exists("T/outside/esc1") || !exists("T/outside/esc2")) {
        puts("  **UNEXPECTED** the escapes above did not land outside the root");
        unexpected++;
    } else {
        puts("  (both landed in T/outside: a single component is mandatory)");
    }

    puts("== single components: the kernel's own refusals ==");
    row("mkdirat(root, \"up\")  final component an escaping symlink", mkdirat(root, "up", 0700), EEXIST);
    row("mkdirat(root, \"dangle\")  final component a dangling link", mkdirat(root, "dangle", 0700), EEXIST);
    row("mkdirat(sub, \"..\")", mkdirat(sub, "..", 0700), EEXIST);
    row("mkdirat(sub, \".\")", mkdirat(sub, ".", 0700), EEXIST);
    row("mkdirat(sub, \"\")", mkdirat(sub, "", 0700), ENOENT);
    row("mkdirat(sub, \"c\")  legal", mkdirat(sub, "c", 0700), 0);
    row("mkdirat(sub, \"c\")  again", mkdirat(sub, "c", 0700), EEXIST);
    if (exists("T/outside/nope")) {
        puts("  **UNEXPECTED** the dangling link's target was created");
        unexpected++;
    }

    puts("== mode: a constant the audit can read ==");
    struct stat st;
    mode_t um = umask(0);   /* read the umask without changing it */
    umask(um);
    if (fstatat(sub, "c", &st, AT_SYMLINK_NOFOLLOW) == 0) {
        printf("mode of sub/c = 0%o (asked 0700, umask 0%o)%s\n", st.st_mode & 07777,
               (unsigned)um, (st.st_mode & 07777) == 0700 ? "" : "  **UNEXPECTED**");
        unexpected += (st.st_mode & 07777) != 0700;
    }

    puts("== a held parent follows its inode ==");
    fresh();
    root = open("T/root", O_PATH | O_DIRECTORY | O_CLOEXEC);
    sub = beneath(root, "sub");
    if (rename("T/root/sub", "T/outside/moved")) { perror("rename"); return 2; }
    row("mkdirat(sub, \"z\") after sub was moved outside the root", mkdirat(sub, "z", 0700), 0);
    printf("  T/outside/moved/z %s\n", exists("T/outside/moved/z")
           ? "exists: the window between opening the parent and mkdirat is real"
           : "missing  **UNEXPECTED**");
    unexpected += !exists("T/outside/moved/z");

    printf("\n%d unexpected\n", unexpected);
    return unexpected != 0;
}
