/* prototypes/beneath/beneath.c -- what openat2(2)'s RESOLVE_* flags refuse,
 * measured on the host that runs it.
 *
 * Verification-only (prototypes/README.md: never shipped, never on the build
 * closure, never referenced from the Makefile, the Nix build or compiler/).
 * It answers the measurement half of docs/design/archivum-beneath.md: before
 * a `Directorium` capability is designed to rest on RESOLVE_BENEATH, find out
 * what the kernel actually refuses, with which errno, and what it does NOT
 * refuse. Its output is transcribed into that document's section 2.
 *
 * libc is used freely; the probe is an instrument, not the prelude.
 * Must run as root (mount(2), unshare(2)); run.sh says so and exits 2
 * otherwise. Every mount happens in a private mount namespace, so nothing
 * outlives the process.
 *
 * Layout built under argv[1] (a fresh empty directory):
 *
 *   W/outside.txt          "ESCAPED"   -- the thing a root must not reach
 *   W/inside.txt           "ESCAPED"   -- the rename race's escape target
 *   W/x/                               -- where the race parks a/b
 *   W/root/                            -- the Directorium's root
 *     inside.txt           "INSIDE"
 *     sub/deep.txt         "DEEP"
 *     a/b/                             -- the race's victim directory
 *     link_in       -> sub/deep.txt    relative, stays inside
 *     link_in_abs   -> W/root/inside.txt  absolute, but names a path inside
 *     link_out      -> ../outside.txt  relative, escapes
 *     link_out_abs  -> /etc/passwd     absolute, escapes
 *     link_dir_out  -> ..              a directory link that escapes
 *     link_dangle   -> ../created.txt  dangling; O_CREAT through it would
 *                                      create W/created.txt
 *     link_magic    -> /proc/self/fd/<outside fd>
 *     hard_out             hard link to W/outside.txt (same filesystem)
 *     sub/hard_out         hard link to W/outside.txt (for Landlock)
 *     mnt/                 a tmpfs mounted here, holding file.txt "MNT"
 *     bind/                /etc bind-mounted here
 *     proc/                procfs mounted here
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/landlock.h>
#include <linux/openat2.h>
#include <linux/seccomp.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef OPEN_HOW_SIZE_VER0
#define OPEN_HOW_SIZE_VER0 24 /* include/linux/fcntl.h, kernel-internal */
#endif
#ifndef SYS_openat2
#define SYS_openat2 437
#endif
#ifndef SYS_landlock_create_ruleset
#define SYS_landlock_create_ruleset 444
#define SYS_landlock_add_rule 445
#define SYS_landlock_restrict_self 446
#endif

static char W[4096], ROOT[4200];
static int rootfd = -1, outside_fd = -1;
static int held = 0, unexpected = 0;

#define B RESOLVE_BENEATH
#define M RESOLVE_NO_MAGICLINKS
#define S RESOLVE_NO_SYMLINKS
#define X RESOLVE_NO_XDEV

static const char *ename(int e)
{
	switch (e) {
	case 0: return "ok";
	case EXDEV: return "EXDEV";
	case ELOOP: return "ELOOP";
	case ENOENT: return "ENOENT";
	case EACCES: return "EACCES";
	case EPERM: return "EPERM";
	case EINVAL: return "EINVAL";
	case E2BIG: return "E2BIG";
	case EAGAIN: return "EAGAIN";
	case ENOSYS: return "ENOSYS";
	case ENOTDIR: return "ENOTDIR";
	case EEXIST: return "EEXIST";
	default: return strerror(e);
	}
}

static void die(const char *what)
{
	fprintf(stderr, "beneath: setup failed: %s: %s\n", what, strerror(errno));
	exit(2);
}

static long o2(int dirfd, const char *path, uint64_t flags, uint64_t mode,
	       uint64_t resolve)
{
	struct open_how how;
	memset(&how, 0, sizeof how);
	how.flags = flags;
	how.mode = mode;
	how.resolve = resolve;
	return syscall(SYS_openat2, dirfd, path, &how, sizeof how);
}

/* errno of one openat2 call (0 on success), and the first bytes read */
static int try_open(int dirfd, const char *path, uint64_t flags, uint64_t mode,
		    uint64_t resolve, char *got, size_t gotsz)
{
	long fd = o2(dirfd, path, flags, mode, resolve);
	if (got) got[0] = 0;
	if (fd < 0) return errno;
	if (got) {
		ssize_t n = read((int)fd, got, gotsz - 1);
		got[n > 0 ? n : 0] = 0;
		char *nl = strchr(got, '\n');
		if (nl) *nl = 0;
		if (strlen(got) > 12) strcpy(got + 9, "...");
	}
	close((int)fd);
	return 0;
}

static void judge(const char *label, int e, int want)
{
	if (e == want) held++; else unexpected++;
	printf("  %-46s %-8s %s\n", label, ename(e),
	       e == want ? "" : (want == 0 ? "**UNEXPECTED (want ok)**"
					   : "**UNEXPECTED**"));
}

static void writefile(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0 || write(fd, s, strlen(s)) < 0) die(path);
	close(fd);
}

static void build(void)
{
	char p[8192], t[8192];
#define P(...) (snprintf(p, sizeof p, __VA_ARGS__), p)
	snprintf(ROOT, sizeof ROOT, "%s/root", W);
	writefile(P("%s/outside.txt", W), "ESCAPED\n");
	writefile(P("%s/inside.txt", W), "ESCAPED\n");
	if (mkdir(P("%s/x", W), 0755)) die(p);
	if (mkdir(ROOT, 0755)) die(ROOT);
	writefile(P("%s/inside.txt", ROOT), "INSIDE\n");
	if (mkdir(P("%s/sub", ROOT), 0755)) die(p);
	writefile(P("%s/sub/deep.txt", ROOT), "DEEP\n");
	if (mkdir(P("%s/a", ROOT), 0755)) die(p);
	if (mkdir(P("%s/a/b", ROOT), 0755)) die(p);
	if (symlink("sub/deep.txt", P("%s/link_in", ROOT))) die(p);
	snprintf(t, sizeof t, "%s/inside.txt", ROOT);
	if (symlink(t, P("%s/link_in_abs", ROOT))) die(p);
	if (symlink("../outside.txt", P("%s/link_out", ROOT))) die(p);
	if (symlink("/etc/passwd", P("%s/link_out_abs", ROOT))) die(p);
	if (symlink("..", P("%s/link_dir_out", ROOT))) die(p);
	if (symlink("../created.txt", P("%s/link_dangle", ROOT))) die(p);
	outside_fd = open(P("%s/outside.txt", W), O_RDONLY);
	if (outside_fd < 0) die(p);
	snprintf(t, sizeof t, "/proc/self/fd/%d", outside_fd);
	if (symlink(t, P("%s/link_magic", ROOT))) die(p);
	snprintf(t, sizeof t, "%s/outside.txt", W);
	if (link(t, P("%s/hard_out", ROOT))) die(p);
	if (link(t, P("%s/sub/hard_out", ROOT))) die(p);
	if (mkdir(P("%s/mnt", ROOT), 0755)) die(p);
	if (mkdir(P("%s/bind", ROOT), 0755)) die(p);
	if (mkdir(P("%s/proc", ROOT), 0755)) die(p);
	if (mkfifo(P("%s/fifo", ROOT), 0644)) die(p);
	if (mknod(P("%s/null_dev", ROOT), S_IFCHR | 0644, makedev(1, 3))) die(p);
	if (mount("none", P("%s/mnt", ROOT), "tmpfs", 0, "size=64k")) die(p);
	writefile(P("%s/mnt/file.txt", ROOT), "MNT\n");
	if (mount("/etc", P("%s/bind", ROOT), NULL, MS_BIND, NULL)) die(p);
	if (mount("proc", P("%s/proc", ROOT), "proc", 0, NULL)) die(p);
	rootfd = open(ROOT, O_PATH | O_DIRECTORY);
	if (rootfd < 0) die(ROOT);
#undef P
}

/* ---- 1. the resolution matrix ------------------------------------------- */

struct row { const char *path; int want[6]; };
/* columns: resolve = 0 | B | B|M | B|M|S | B|M|X | B|M|S|X
 * want[] is the expectation the probe judges against; it is written from
 * the openat2(2) man page and fs/namei.c, and the output is the
 * measurement. A row where the two disagree prints **UNEXPECTED**. */
static const uint64_t cols[6] = { 0, B, B | M, B | M | S, B | M | X, B | M | S | X };
static const char *colname[6] = { "0", "B", "B|M", "B|M|S", "B|M|X", "B|M|S|X" };

static void matrix(void)
{
	char magic_abs[64];
	snprintf(magic_abs, sizeof magic_abs, "/proc/self/fd/%d", outside_fd);
	char proc_fd[64];
	snprintf(proc_fd, sizeof proc_fd, "proc/self/fd/%d", outside_fd);
	const int OK = 0;
	struct row rows[] = {
		{ "inside.txt",               { OK, OK, OK, OK, OK, OK } },
		{ "sub/deep.txt",             { OK, OK, OK, OK, OK, OK } },
		{ "sub/../inside.txt",        { OK, OK, OK, OK, OK, OK } },
		{ ".",                        { OK, OK, OK, OK, OK, OK } },
		{ "../outside.txt",           { OK, EXDEV, EXDEV, EXDEV, EXDEV, EXDEV } },
		{ "sub/../../outside.txt",    { OK, EXDEV, EXDEV, EXDEV, EXDEV, EXDEV } },
		{ "/etc/passwd",              { OK, EXDEV, EXDEV, EXDEV, EXDEV, EXDEV } },
		{ "link_in",                  { OK, OK, OK, ELOOP, OK, ELOOP } },
		{ "link_in_abs",              { OK, EXDEV, EXDEV, ELOOP, EXDEV, ELOOP } },
		{ "link_out",                 { OK, EXDEV, EXDEV, ELOOP, EXDEV, ELOOP } },
		{ "link_out_abs",             { OK, EXDEV, EXDEV, ELOOP, EXDEV, ELOOP } },
		{ "link_dir_out/outside.txt", { OK, EXDEV, EXDEV, ELOOP, EXDEV, ELOOP } },
		{ "link_magic",               { OK, EXDEV, EXDEV, ELOOP, EXDEV, ELOOP } },
		{ magic_abs,                  { OK, EXDEV, EXDEV, EXDEV, EXDEV, EXDEV } },
		{ proc_fd,                    { OK, EXDEV, ELOOP, ELOOP, EXDEV, EXDEV } },
		{ "proc/self/root/etc/passwd",{ OK, EXDEV, ELOOP, ELOOP, EXDEV, EXDEV } },
		{ "mnt/file.txt",             { OK, OK, OK, OK, EXDEV, EXDEV } },
		{ "bind/passwd",              { OK, OK, OK, OK, EXDEV, EXDEV } },
		{ "hard_out",                 { OK, OK, OK, OK, OK, OK } },
		{ "",                         { ENOENT, ENOENT, ENOENT, ENOENT, ENOENT, ENOENT } },
	};
	printf("\n[1] resolution matrix: openat2(rootfd = O_PATH of W/root, path, "
	       "O_RDONLY, how.resolve = column)\n");
	printf("    B=RESOLVE_BENEATH M=NO_MAGICLINKS S=NO_SYMLINKS X=NO_XDEV; "
	       "cell = errno or ok:<first bytes read>\n");
	printf("  %-27s", "path");
	for (int c = 0; c < 6; c++) printf(" %-13s", colname[c]);
	printf("\n");
	for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++) {
		const char *shown = rows[r].path[0] ? rows[r].path : "\"\" (empty)";
		if (!strncmp(shown, "/proc/self/fd/", 14)) shown = "/proc/self/fd/<N>";
		if (!strncmp(shown, "proc/self/fd/", 13)) shown = "proc/self/fd/<N>";
		printf("  %-27s", shown);
		for (int c = 0; c < 6; c++) {
			char got[64], cell[96];
			int e = try_open(rootfd, rows[r].path, O_RDONLY, 0, cols[c],
					 got, sizeof got);
			if (e == 0)
				snprintf(cell, sizeof cell, "ok:%s", got);
			else
				snprintf(cell, sizeof cell, "%s", ename(e));
			int ok = e == rows[r].want[c];
			if (ok) held++; else unexpected++;
			printf(" %-12s%s", cell, ok ? " " : "!");
		}
		printf("\n");
	}
	printf("  ('!' after a cell = differs from the man-page expectation)\n");
}

/* ---- 2. writes and creation ---------------------------------------------- */

static void creation(void)
{
	char p[8192];
	printf("\n[2] creation through the root (resolve = B|M)\n");
	int e = try_open(rootfd, "link_dangle", O_WRONLY | O_CREAT, 0644, B | M,
			 NULL, 0);
	snprintf(p, sizeof p, "%s/created.txt", W);
	struct stat st;
	int made = stat(p, &st) == 0;
	judge("O_CREAT via link_dangle (-> ../created.txt)", e, EXDEV);
	printf("  %-46s %s\n", "  ... W/created.txt exists afterwards?",
	       made ? "**YES -- ESCAPED**" : "no");
	if (made) unexpected++; else held++;
	e = try_open(rootfd, "new.txt", O_WRONLY | O_CREAT | O_EXCL, 0644, B | M,
		     NULL, 0);
	judge("O_CREAT|O_EXCL new.txt", e, 0);
	e = try_open(rootfd, "../new2.txt", O_WRONLY | O_CREAT | O_EXCL, 0644,
		     B | M, NULL, 0);
	judge("O_CREAT|O_EXCL ../new2.txt", e, EXDEV);
	e = try_open(rootfd, "link_dangle", O_WRONLY | O_CREAT, 0644, 0, NULL, 0);
	judge("anti-vacuity: same O_CREAT, resolve = 0", e, 0);
	made = stat(p, &st) == 0;
	printf("  %-46s %s\n", "  ... W/created.txt exists afterwards?",
	       made ? "yes (the link really does escape)" : "**NO -- vacuous**");
	if (made) held++; else unexpected++;
}

/* ---- 3. what the kernel refuses in struct open_how itself ---------------- */

static long raw2(const void *how, size_t size)
{
	return syscall(SYS_openat2, rootfd, "inside.txt", how, size);
}

/* ---- 2b. attenuation and what BENEATH does not filter --------------------- */

static void attenuation(void)
{
	const uint64_t BMX = B | M | X;
	printf("\n[2b] attenuation (subfd = openat2(rootfd, \"sub\", O_PATH|O_DIRECTORY, B|M|X)) and file types\n");
	long subfd = o2(rootfd, "sub", O_PATH | O_DIRECTORY | O_CLOEXEC, 0, BMX);
	judge("derive subfd", subfd < 0 ? errno : 0, 0);
	if (subfd < 0) return;
	int e;
	e = try_open((int)subfd, "deep.txt", O_RDONLY, 0, BMX, NULL, 0);
	judge("subfd: deep.txt", e, 0);
	e = try_open((int)subfd, "../inside.txt", O_RDONLY, 0, BMX, NULL, 0);
	judge("subfd: ../inside.txt (parent's file)", e, EXDEV);
	e = try_open((int)subfd, "hard_out", O_RDONLY, 0, BMX, NULL, 0);
	judge("subfd: hard_out (hard link to outside)", e, 0);
	close((int)subfd);
	/* BENEATH bounds names, not file types: a FIFO and a device node
	 * beneath the root open. O_NONBLOCK is what keeps the FIFO open from
	 * blocking forever; fstat is what tells the caller what it got. */
	long fd = o2(rootfd, "fifo", O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0, BMX);
	judge("fifo, O_RDONLY|O_NONBLOCK", fd < 0 ? errno : 0, 0);
	if (fd >= 0) {
		struct stat st;
		fstat((int)fd, &st);
		printf("  %-46s %s\n", "  ... fstat S_ISFIFO", S_ISFIFO(st.st_mode) ? "yes" : "no");
		close((int)fd);
	}
	fd = o2(rootfd, "null_dev", O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0, BMX);
	judge("char device 1,3 beneath root", fd < 0 ? errno : 0, 0);
	if (fd >= 0) {
		struct stat st;
		fstat((int)fd, &st);
		printf("  %-46s %s\n", "  ... fstat S_ISCHR", S_ISCHR(st.st_mode) ? "yes" : "no");
		close((int)fd);
	}
	/* an O_PATH descriptor can be fstat'd without opening the file */
	fd = o2(rootfd, "null_dev", O_PATH | O_CLOEXEC, 0, BMX);
	judge("char device, O_PATH (no driver open)", fd < 0 ? errno : 0, 0);
	if (fd >= 0) {
		struct stat st;
		int r = fstat((int)fd, &st);
		judge("  ... fstat on the O_PATH fd", r < 0 ? errno : 0, 0);
		close((int)fd);
	}
}

static void structure(void)
{
	printf("\n[3] struct open_how validation (the audit's constant)\n");
	printf("  sizeof(struct open_how) = %zu, OPEN_HOW_SIZE_VER0 = %d, "
	       "offsetof(resolve) = %zu\n", sizeof(struct open_how),
	       OPEN_HOW_SIZE_VER0, offsetof(struct open_how, resolve));
	struct { struct open_how h; uint64_t tail; } big;
	struct open_how h;
	long r;

	memset(&h, 0, sizeof h); h.resolve = B;
	r = raw2(&h, 16); judge("size 16 (< VER0)", r < 0 ? errno : 0, EINVAL);
	if (r >= 0) close((int)r);

	memset(&big, 0, sizeof big); big.h.resolve = B;
	r = raw2(&big, sizeof big); judge("size 32, zero tail", r < 0 ? errno : 0, 0);
	if (r >= 0) close((int)r);
	big.tail = 1;
	r = raw2(&big, sizeof big); judge("size 32, nonzero tail", r < 0 ? errno : 0, E2BIG);
	if (r >= 0) close((int)r);

	memset(&h, 0, sizeof h); h.resolve = B | 0x80;
	r = raw2(&h, sizeof h); judge("resolve |= 0x80 (unknown bit)", r < 0 ? errno : 0, EINVAL);
	if (r >= 0) close((int)r);

	memset(&h, 0, sizeof h); h.resolve = B | RESOLVE_IN_ROOT;
	r = raw2(&h, sizeof h); judge("BENEATH|IN_ROOT together", r < 0 ? errno : 0, EINVAL);
	if (r >= 0) close((int)r);

	memset(&h, 0, sizeof h); h.resolve = B; h.mode = 0644;
	r = raw2(&h, sizeof h); judge("mode 0644 without O_CREAT", r < 0 ? errno : 0, EINVAL);
	if (r >= 0) close((int)r);

	memset(&h, 0, sizeof h); h.resolve = B; h.flags = 1ULL << 40;
	r = raw2(&h, sizeof h); judge("flags bit 40 (unknown)", r < 0 ? errno : 0, EINVAL);
	if (r >= 0) close((int)r);

	/* openat(2) for contrast: unknown flag bits are silently ignored */
	r = syscall(SYS_openat, rootfd, "inside.txt", O_RDONLY | (1 << 30), 0);
	judge("contrast: openat() with flag bit 30", r < 0 ? errno : 0, 0);
	if (r >= 0) close((int)r);

	/* AT_FDCWD as dirfd: BENEATH is then relative to the cwd */
	char cwd[4096];
	if (getcwd(cwd, sizeof cwd) && chdir(ROOT) == 0) {
		memset(&h, 0, sizeof h); h.resolve = B;
		r = syscall(SYS_openat2, AT_FDCWD, "../outside.txt", &h, sizeof h);
		judge("dirfd = AT_FDCWD (cwd = root), ../outside", r < 0 ? errno : 0, EXDEV);
		if (r >= 0) close((int)r);
		if (chdir(cwd)) die("chdir back");
	}
}

/* ---- 4. the rename race ---------------------------------------------------
 * a/b is renamed to W/x/b and back as fast as one thread can. The opener
 * walks a/b/../../inside.txt: if it reaches b while b is under root and
 * takes the two `..` after b moved to W/x, the walk lands on W/inside.txt
 * ("ESCAPED"). The resolve = 0 leg is the anti-vacuity check: it must
 * produce escapes, or the race never fired and the B leg proves nothing. */

static volatile int stop;
static char rb_in[8192], rb_out[8192];

static void *racer(void *arg)
{
	(void)arg;
	while (!stop) {
		rename(rb_in, rb_out);
		rename(rb_out, rb_in);
	}
	return NULL;
}

static void race(uint64_t resolve, double secs)
{
	long ok_in = 0, escaped = 0, eagain = 0, exdev = 0, enoent = 0, other = 0;
	long run = 0, maxrun = 0;
	stop = 0;
	pthread_t t;
	if (pthread_create(&t, NULL, racer, NULL)) die("pthread_create");
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	long n = 0;
	for (;;) {
		char got[64];
		int e = try_open(rootfd, "a/b/../../inside.txt", O_RDONLY, 0,
				 resolve, got, sizeof got);
		n++;
		if (e != EAGAIN) run = 0;
		if (e == 0) {
			if (!strcmp(got, "INSIDE")) ok_in++;
			else escaped++;
		} else if (e == EAGAIN) { eagain++; if (++run > maxrun) maxrun = run; }
		else if (e == EXDEV) exdev++;
		else if (e == ENOENT) enoent++;
		else other++;
		if ((n & 1023) == 0) {
			clock_gettime(CLOCK_MONOTONIC, &t1);
			if ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9 > secs)
				break;
		}
	}
	stop = 1;
	pthread_join(t, NULL);
	rename(rb_out, rb_in);  /* leave it in place; may fail harmlessly */
	printf("  resolve=%-6s %8ld opens: inside %ld, ESCAPED %ld, EAGAIN %ld, "
	       "EXDEV %ld, ENOENT %ld, other %ld\n",
	       resolve ? "B|M|X" : "0", n, ok_in, escaped, eagain, exdev, enoent, other);
	printf("  %15s longest run of consecutive EAGAIN: %ld\n", "", maxrun);
	if (resolve) { if (escaped == 0) held++; else unexpected++; }
	else { if (escaped > 0) held++; else unexpected++; }
}

static void races(double secs)
{
	snprintf(rb_in, sizeof rb_in, "%s/a/b", ROOT);
	snprintf(rb_out, sizeof rb_out, "%s/x/b", W);
	printf("\n[4] rename race on a/b/../../inside.txt, %.1f s per leg, "
	       "racer renames root/a/b <-> W/x/b\n", secs);
	race(0, secs);
	race(B | M | X, secs);
	printf("  (want: resolve=0 shows ESCAPED > 0 (the race fires), "
	       "B|M|X shows ESCAPED == 0)\n");
}

/* ---- 5. Landlock composition --------------------------------------------- */

static void landlock(void)
{
	printf("\n[5] Landlock composition (child process; rule: READ_FILE|"
	       "READ_DIR beneath W/root/sub only)\n");
	long abi = syscall(SYS_landlock_create_ruleset, NULL, 0,
			   LANDLOCK_CREATE_RULESET_VERSION);
	if (abi < 0) {
		printf("  Landlock unavailable: %s -- section SKIPPED (not a pass)\n",
		       ename(errno));
		return;
	}
	printf("  Landlock ABI version: %ld\n", abi);
	fflush(stdout);
	pid_t pid = fork();
	if (pid < 0) die("fork");
	if (pid == 0) {
		int base = unexpected;
		struct landlock_ruleset_attr ra = {
			.handled_access_fs = LANDLOCK_ACCESS_FS_READ_FILE |
					     LANDLOCK_ACCESS_FS_READ_DIR,
		};
		int rs = (int)syscall(SYS_landlock_create_ruleset, &ra, sizeof ra, 0);
		if (rs < 0) die("landlock_create_ruleset");
		char p[8192];
		snprintf(p, sizeof p, "%s/sub", ROOT);
		struct landlock_path_beneath_attr pb = {
			.allowed_access = LANDLOCK_ACCESS_FS_READ_FILE |
					  LANDLOCK_ACCESS_FS_READ_DIR,
			.parent_fd = open(p, O_PATH | O_DIRECTORY),
		};
		if (pb.parent_fd < 0) die(p);
		if (syscall(SYS_landlock_add_rule, rs, LANDLOCK_RULE_PATH_BENEATH, &pb, 0))
			die("landlock_add_rule");
		if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) die("no_new_privs");
		if (syscall(SYS_landlock_restrict_self, rs, 0)) die("restrict_self");
		int e;
		e = try_open(rootfd, "sub/deep.txt", O_RDONLY, 0, B | M, NULL, 0);
		judge("B|M sub/deep.txt (allowed by both)", e, 0);
		e = try_open(rootfd, "link_in", O_RDONLY, 0, B | M, NULL, 0);
		judge("B|M link_in (-> sub/deep.txt)", e, 0);
		e = try_open(rootfd, "inside.txt", O_RDONLY, 0, B | M, NULL, 0);
		judge("B|M inside.txt (beneath; Landlock denies)", e, EACCES);
		e = try_open(rootfd, "../outside.txt", O_RDONLY, 0, B | M, NULL, 0);
		judge("B|M ../outside.txt (both deny; who first?)", e, EXDEV);
		e = try_open(rootfd, "sub/../../outside.txt", O_RDONLY, 0, 0, NULL, 0);
		judge("resolve=0 ../outside (Landlock alone)", e, EACCES);
		e = try_open(rootfd, "sub/hard_out", O_RDONLY, 0, B | M, NULL, 0);
		judge("B|M sub/hard_out (hard link to outside)", e, 0);
		e = try_open(rootfd, "sub", O_RDONLY | O_DIRECTORY, 0, B | M, NULL, 0);
		judge("B|M sub as O_DIRECTORY", e, 0);
		fflush(stdout);
		_exit(unexpected > base ? 1 : 0);
	}
	int st;
	waitpid(pid, &st, 0);
	/* the child counted its own rows; fold its verdict in */
	if (WIFEXITED(st) && WEXITSTATUS(st) == 0) held++; else unexpected++;
}

/* ---- 6. seccomp: what a runtime filter can and cannot require ------------- */

static int install_filter(int deny_openat_eperm, int openat2_enosys)
{
	struct sock_filter f[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0xC000003E /* AUDIT_ARCH_X86_64 */, 1, 0),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_openat, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, deny_openat_eperm
			 ? (SECCOMP_RET_ERRNO | EPERM) : SECCOMP_RET_ALLOW),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_openat2, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, openat2_enosys
			 ? (SECCOMP_RET_ERRNO | ENOSYS) : SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = { .len = sizeof f / sizeof f[0], .filter = f };
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) return -1;
	return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
}

static void seccomp_leg(void)
{
	printf("\n[6] seccomp (child processes)\n");
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		int base = unexpected;
		if (install_filter(1, 0)) die("seccomp");
		printf("  filter A: openat -> EPERM, openat2 -> ALLOW (no argument inspection possible on how->resolve)\n");
		int e;
		long r = syscall(SYS_openat, rootfd, "../outside.txt", O_RDONLY, 0);
		judge("openat ../outside.txt", r < 0 ? errno : 0, EPERM);
		e = try_open(rootfd, "../outside.txt", O_RDONLY, 0, B | M, NULL, 0);
		judge("openat2 B|M ../outside.txt", e, EXDEV);
		e = try_open(rootfd, "../outside.txt", O_RDONLY, 0, 0, NULL, 0);
		judge("openat2 resolve=0 ../outside.txt", e, 0);
		fflush(stdout);
		_exit(unexpected > base ? 1 : 0);
	}
	int st;
	waitpid(pid, &st, 0);
	if (WIFEXITED(st) && WEXITSTATUS(st) == 0) held++; else unexpected++;
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		int base = unexpected;
		if (install_filter(0, 1)) die("seccomp");
		printf("  filter B: openat2 -> ENOSYS (simulates a pre-5.6 kernel; NOT a measurement of one)\n");
		int e = try_open(rootfd, "inside.txt", O_RDONLY, 0, B | M, NULL, 0);
		judge("openat2 B|M inside.txt", e, ENOSYS);
		fflush(stdout);
		_exit(unexpected > base ? 1 : 0);
	}
	waitpid(pid, &st, 0);
	if (WIFEXITED(st) && WEXITSTATUS(st) == 0) held++; else unexpected++;
}

/* ---- 7. the four constants docs/design/archivum-beneath.md proposes ------
 * Section 3's table states each constant as hex. This checks the hex
 * against the UAPI headers at compile time, and passes each one to the
 * kernel: radix from AT_FDCWD with an absolute path, then infra, lege and
 * crea beneath it. */

_Static_assert((O_PATH | O_DIRECTORY | O_CLOEXEC) == 0x290000, "how_radix/how_infra flags");
_Static_assert((O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC) == 0x80900, "how_lege flags");
_Static_assert((O_WRONLY | O_CREAT | O_EXCL | O_NOCTTY | O_CLOEXEC) == 0x801c1, "how_crea flags");
_Static_assert((RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV) == 0x0b, "beneath resolve");
_Static_assert(RESOLVE_NO_MAGICLINKS == 0x02, "radix resolve");
_Static_assert(AT_FDCWD == -100, "AT_FDCWD");

static void constants(void)
{
	printf("\n[7] the proposed constants, passed to the kernel\n");
	long r = o2(AT_FDCWD, ROOT, 0x290000, 0, 0x02);
	judge("how_radix  AT_FDCWD, absolute root", r < 0 ? errno : 0, 0);
	if (r < 0) return;
	long e = o2(AT_FDCWD, "root", 0x290000, 0, 0x02);
	judge("how_radix  AT_FDCWD, relative (prelude must refuse)", e < 0 ? errno : 0, ENOENT);
	if (e >= 0) close((int)e);
	long s = o2((int)r, "sub", 0x290000, 0, 0x0b);
	judge("how_infra  sub", s < 0 ? errno : 0, 0);
	if (s < 0) { close((int)r); return; }
	e = o2((int)s, "deep.txt", 0x80900, 0, 0x0b);
	judge("how_lege   deep.txt", e < 0 ? errno : 0, 0);
	if (e >= 0) close((int)e);
	e = o2((int)s, "fresh.txt", 0x801c1, 0600, 0x0b);
	judge("how_crea   fresh.txt", e < 0 ? errno : 0, 0);
	if (e >= 0) close((int)e);
	e = o2((int)s, "fresh.txt", 0x801c1, 0600, 0x0b);
	judge("how_crea   fresh.txt again (O_EXCL)", e < 0 ? errno : 0, EEXIST);
	if (e >= 0) close((int)e);
	e = o2((int)r, "link_in", 0x801c1, 0600, 0x0b);
	judge("how_crea   link_in (existing symlink)", e < 0 ? errno : 0, EEXIST);
	if (e >= 0) close((int)e);
	e = o2((int)r, "hard_out", 0x801c1, 0600, 0x0b);
	judge("how_crea   hard_out (existing hard link)", e < 0 ? errno : 0, EEXIST);
	if (e >= 0) close((int)e);
	close((int)s);
	close((int)r);
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: beneath WORKDIR [race-seconds]\n"); return 2; }
	double secs = argc > 2 ? atof(argv[2]) : 2.0;
	if (!realpath(argv[1], W)) die(argv[1]);
	setvbuf(stdout, NULL, _IOLBF, 0);
	struct utsname u;
	uname(&u);
	printf("== beneath: openat2(2) RESOLVE_* measured ==\n");
	printf("kernel: %s %s %s\n", u.sysname, u.release, u.version);
	printf("euid: %d\n", (int)geteuid());
	if (unshare(CLONE_NEWNS)) die("unshare(CLONE_NEWNS)");
	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) die("make / private");
	build();
	matrix();
	creation();
	attenuation();
	structure();
	races(secs);
	landlock();
	seccomp_leg();
	constants();
	printf("\n== %d held, %d unexpected ==\n", held, unexpected);
	return unexpected ? 1 : 0;
}
