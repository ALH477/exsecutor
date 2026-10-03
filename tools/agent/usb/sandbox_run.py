#!/usr/bin/env python3
# tools/agent/usb/sandbox_run.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
"""sandbox_run.py -- run ONE untrusted, freestanding x86-64 ELF in a throwaway
namespace sandbox. Python 3 standard library only (ctypes for the syscalls).

usage: sandbox_run.py [OPTIONS] ELF_PATH
       sandbox_run.py --check-elf ELF_PATH

The runner contract (tools/agent/loop/loop.py calls `RUNNER ELF_PATH`):
  stdout   the program's stdout (capped, see --max-output)
  stderr   the program's stderr (capped; an Exsecutor runtime abort prints
           `exsecutor: abortus N` there), then the runner's own diagnostics,
           each prefixed `sandbox_run:`
  exit     the program's exit status, or
             124  timeout (wall clock, or the CPU rlimit: SIGXCPU)
             125  runner or sandbox failure -- the program was NOT run, or the
                  runner could not tell how it ended. FAIL CLOSED: there is no
                  unsandboxed fallback, ever.
             132  SIGILL (an Exsecutor runtime abort)
             159  SIGSYS: the seccomp filter killed it (a syscall outside the
                  allowlist below)
             128+N any other fatal signal N

What the program gets, by construction (not by trusting Exsecutor's
capability checker: a program holding `archivum` can open any path the
process can see -- that is the spec's own design, measured by selftest.sh):
  * new user, mount, pid, net, ipc, uts and cgroup namespaces. The host uid
    is mapped to uid 1000 inside, so execve() drops every capability.
  * a fresh tmpfs as `/`, holding exactly one file, /prog (a copy of
    ELF_PATH), remounted read-only before exec. No /proc, no /dev, no /tmp.
    The old root is detached (pivot_root + MNT_DETACH), not merely hidden.
  * a network namespace with no interface configured (lo is down): no
    route to anything, including the host's 127.0.0.1.
  * stdin is the host's /dev/null (an fd opened before entering the
    sandbox: nothing is created inside); stdout and stderr are pipes to
    this runner, never the caller's terminal; every other fd is closed.
  * rlimits: AS (--mem MiB), CPU (--timeout s), FSIZE 0, NPROC (--nproc),
    NOFILE 16, CORE 0, MEMLOCK 0, MSGQUEUE 0; PR_SET_NO_NEW_PRIVS.
  * a seccomp filter (unless --no-seccomp): arch must be x86_64, x32
    numbers refused, and only these syscalls allowed --
      read(0) write(1) close(3) fstat(5) lseek(8) mmap(9) munmap(11)
      exit_group(231) openat2(437), plus execve(59) for the exec itself.
    That is the union of the prelude's per-atom table
    (compiler/x86_64/prelude/README.md; tools/syscall-audit.sh) and was
    re-measured with strace over every tests/programs binary that builds
    from one source and every tests/unit/prelude_*.asm fixture: the
    observed set was exactly read write close fstat lseek mmap munmap
    exit_group openat2 execve. Anything else is SECCOMP_RET_KILL_PROCESS.
    NOT in it: openat(257) (the compiler's, not a program's), exit(60),
    nanosleep, every socket-family call, clone/fork, ioctl.
  * a hard wall-clock timeout that SIGKILLs the namespace's pid 1, whose
    death the kernel turns into SIGKILL for everything else in the pid
    namespace, and the runner's helper's whole process group.

What it does NOT protect against (see README.md "Threat model"):
  * kernel bugs. This is namespace isolation on the host kernel, not a VM.
    The seccomp filter shrinks the reachable kernel surface; it does not
    remove it.
  * CPU-time side channels, Spectre-class leaks, rowhammer.
  * resource exhaustion below the rlimits: a program may use --mem MiB of
    RAM and one core for --timeout seconds. There is no cgroup.
  * a hostile caller: whoever runs this already has the user's authority.

Requires Linux on x86-64 with unprivileged user namespaces. When they are
unavailable (kernel.unprivileged_userns_clone=0, user.max_user_namespaces=0,
Ubuntu's kernel.apparmor_restrict_unprivileged_userns=1 without a profile,
a container's seccomp profile...), the first failing step is reported and
the exit status is 125. The program is not run.
"""

import ctypes
import errno
import os
import resource
import select
import signal
import struct
import sys
import time

# ---------------------------------------------------------------- constants
# x86-64 only: the syscall numbers below are that ABI's.
CLONE_NEWNS = 0x00020000
CLONE_NEWCGROUP = 0x02000000
CLONE_NEWUTS = 0x04000000
CLONE_NEWIPC = 0x08000000
CLONE_NEWUSER = 0x10000000
CLONE_NEWPID = 0x20000000
CLONE_NEWNET = 0x40000000

MS_RDONLY, MS_NOSUID, MS_NODEV = 0x1, 0x2, 0x4
MS_REMOUNT, MS_BIND, MS_REC, MS_PRIVATE = 0x20, 0x1000, 0x4000, 0x40000
MNT_DETACH = 2

SYS_mount, SYS_umount2, SYS_pivot_root = 165, 166, 155
SYS_sethostname, SYS_unshare, SYS_prctl = 170, 272, 157

PR_SET_PDEATHSIG, PR_SET_DUMPABLE, PR_SET_SECCOMP, PR_SET_NO_NEW_PRIVS = 1, 4, 22, 38
SECCOMP_MODE_FILTER = 2
AUDIT_ARCH_X86_64 = 0xC000003E
SECCOMP_RET_KILL_PROCESS = 0x80000000
SECCOMP_RET_ALLOW = 0x7FFF0000

# read write close fstat lseek mmap munmap exit_group openat2 -- the program
# set; execve only so that the filter can be installed before the exec.
SECCOMP_ALLOW = (0, 1, 3, 5, 8, 9, 11, 231, 437, 59)

EXIT_TIMEOUT, EXIT_RUNNER, EXIT_SIGILL = 124, 125, 132
SANDBOX_UID = 1000  # host uid -> this uid inside; non-zero so execve drops caps

MAX_ELF = 64 << 20

RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_CORE = 0, 1, 4
RLIMIT_NPROC, RLIMIT_NOFILE, RLIMIT_MEMLOCK, RLIMIT_AS, RLIMIT_MSGQUEUE = 6, 7, 8, 9, 12

_libc = None


def libc():
    global _libc
    if _libc is None:
        _libc = ctypes.CDLL(None, use_errno=True)
        _libc.syscall.restype = ctypes.c_long
    return _libc


def _arg(a):
    if a is None:
        return ctypes.c_void_p(0)
    if isinstance(a, str):
        return ctypes.c_char_p(a.encode())
    if isinstance(a, bytes):
        return ctypes.c_char_p(a)
    if isinstance(a, int):
        return ctypes.c_long(a)
    return a  # already a ctypes object / pointer


def sc(what, nr, *args):
    """A raw syscall; raises OSError naming `what` on failure."""
    r = libc().syscall(ctypes.c_long(nr), *[_arg(a) for a in args])
    if r == -1:
        e = ctypes.get_errno()
        raise OSError(e, "%s: %s" % (what, os.strerror(e)))
    return r


def diag(msg):
    try:
        os.write(2, ("sandbox_run: %s\n" % msg).encode("utf-8", "replace"))
    except OSError:
        pass


# ---------------------------------------------------------------- ELF check
def check_elf(data):
    """Return None if `data` is a freestanding x86-64 ELF executable, else why not."""
    if len(data) < 64 or data[:4] != b"\x7fELF":
        return "not an ELF file"
    if data[4] != 2 or data[5] != 1:
        return "not ELF64 little-endian"
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    if e_machine != 62:
        return "not x86-64 (e_machine %d)" % e_machine
    if e_type not in (2, 3):
        return "not an executable (e_type %d)" % e_type
    e_phoff, = struct.unpack_from("<Q", data, 32)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 54)
    if e_phnum == 0 or e_phentsize < 56 or e_phoff + e_phnum * e_phentsize > len(data):
        return "malformed program headers"
    for i in range(e_phnum):
        p_type, = struct.unpack_from("<I", data, e_phoff + i * e_phentsize)
        if p_type == 3:
            return "not freestanding: has PT_INTERP (dynamically linked)"
        if p_type == 2:
            return "not freestanding: has PT_DYNAMIC"
    return None


# ---------------------------------------------------------------- seccomp
def seccomp_program():
    ins = []

    def stmt(code, k):
        ins.append([code, 0, 0, k])

    def jeq(k, jt, jf):
        ins.append([0x15, jt, jf, k])

    stmt(0x20, 4)                               # A = seccomp_data.arch
    jeq(AUDIT_ARCH_X86_64, 0, None)             # != x86_64 -> kill (patched)
    stmt(0x20, 0)                               # A = seccomp_data.nr
    ins.append([0x35, None, 0, 0x40000000])     # nr >= x32 bit -> kill (patched)
    for nr in SECCOMP_ALLOW:
        jeq(nr, None, 0)                        # == nr -> allow (patched)
    kill = len(ins)
    stmt(0x06, SECCOMP_RET_KILL_PROCESS)
    allow = len(ins)
    stmt(0x06, SECCOMP_RET_ALLOW)
    for i, (code, jt, jf, k) in enumerate(ins):
        if code == 0x15 and k == AUDIT_ARCH_X86_64 and i == 1:
            ins[i][2] = kill - i - 1
        elif code == 0x35:
            ins[i][1] = kill - i - 1
        elif code == 0x15 and jt is None:
            ins[i][1] = allow - i - 1
    for code, jt, jf, k in ins:
        assert 0 <= jt < 256 and 0 <= jf < 256
    return b"".join(struct.pack("<HBBI", c, jt, jf, k) for c, jt, jf, k in ins), len(ins)


def install_seccomp():
    blob, n = seccomp_program()
    buf = ctypes.create_string_buffer(blob, len(blob))

    class SockFprog(ctypes.Structure):
        _fields_ = [("len", ctypes.c_ushort), ("filter", ctypes.c_void_p)]

    prog = SockFprog(n, ctypes.addressof(buf))
    sc("prctl(PR_SET_SECCOMP)", SYS_prctl, PR_SET_SECCOMP, SECCOMP_MODE_FILTER,
       ctypes.cast(ctypes.pointer(prog), ctypes.c_void_p), 0, 0)
    return buf  # keep alive until exec


# ---------------------------------------------------------------- the child side
def write_file(path, text):
    fd = os.open(path, os.O_WRONLY)
    try:
        os.write(fd, text.encode())
    finally:
        os.close(fd)


def report(st_w, tag, msg):
    try:
        os.write(st_w, ("%s %s\n" % (tag, msg)).encode("utf-8", "replace"))
    except OSError:
        pass


def sandbox_init(opts, data, fds, st_w):
    """Runs as pid 1 of the new pid namespace. Never returns."""
    devnull, out_w, err_w = fds
    try:
        sc("prctl(PR_SET_PDEATHSIG)", SYS_prctl, PR_SET_PDEATHSIG, signal.SIGKILL, 0, 0, 0)
        sc("mount(/, MS_PRIVATE)", SYS_mount, None, "/", None, MS_REC | MS_PRIVATE, None)
        base = None
        for cand in ("/tmp", "/mnt", "/var/tmp", "/run", "/srv", "/opt", "/home", "/"):
            if os.path.isdir(cand):
                base = cand
                break
        size_kib = (len(data) >> 10) + 64
        sc("mount(tmpfs)", SYS_mount, "exsc-sandbox", base, "tmpfs", MS_NOSUID | MS_NODEV,
           "size=%dk,nr_inodes=8,mode=0755" % size_kib)
        os.chdir(base)
        fd = os.open("prog", os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o555)
        view = memoryview(data)
        while view:
            n = os.write(fd, view)
            view = view[n:]
        os.close(fd)
        # Make the tmpfs the root and detach the old one entirely.
        sc("pivot_root", SYS_pivot_root, ".", ".")
        sc("umount2(old root)", SYS_umount2, ".", MNT_DETACH)
        os.chdir("/")
        sc("remount / read-only", SYS_mount, None, "/", None,
           MS_REMOUNT | MS_BIND | MS_RDONLY | MS_NOSUID | MS_NODEV, None)
        if os.listdir("/") != ["prog"]:
            raise OSError(0, "root is not exactly {prog}: %r" % os.listdir("/"))
        try:
            sc("sethostname", SYS_sethostname, "sandbox", 7)
        except OSError:
            pass
        os.dup2(devnull, 0)
        os.dup2(out_w, 1)
        os.dup2(err_w, 2)
        top = resource.getrlimit(resource.RLIMIT_NOFILE)[0]
        if top == resource.RLIM_INFINITY or top > (1 << 20):
            top = 1 << 20
        os.closerange(3, st_w)
        os.closerange(st_w + 1, top)
        mem = opts["mem"] << 20
        lims = [(RLIMIT_AS, mem, mem),
                (RLIMIT_CPU, opts["timeout"], opts["timeout"] + 1),
                (RLIMIT_FSIZE, 0, 0), (RLIMIT_CORE, 0, 0),
                (RLIMIT_NPROC, opts["nproc"], opts["nproc"]),
                (RLIMIT_NOFILE, 16, 16), (RLIMIT_MEMLOCK, 0, 0), (RLIMIT_MSGQUEUE, 0, 0)]
        for res, soft, hard in lims:
            resource.setrlimit(res, (soft, hard))
        sc("prctl(PR_SET_NO_NEW_PRIVS)", SYS_prctl, PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)
        sc("prctl(PR_SET_DUMPABLE)", SYS_prctl, PR_SET_DUMPABLE, 0, 0, 0, 0)
        keepalive = None
        if opts["seccomp"]:
            keepalive = install_seccomp()
        os.execve("/prog", ["prog"], {})
        del keepalive
        raise OSError(0, "execve returned")
    except BaseException as e:  # noqa: BLE001 -- every failure is a setup failure
        report(st_w, "F", str(e) or e.__class__.__name__)
        os._exit(EXIT_RUNNER)


def helper(opts, data, fds, st_w):
    """Runs in the host pid namespace, as the parent of the sandbox's pid 1."""
    try:
        os.setsid()
        sc("prctl(PR_SET_PDEATHSIG)", SYS_prctl, PR_SET_PDEATHSIG, signal.SIGKILL, 0, 0, 0)
        uid, gid = os.geteuid(), os.getegid()
        sc("unshare(user,mount,pid,net,ipc,uts,cgroup)", SYS_unshare,
           CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWNET
           | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_NEWCGROUP)
        write_file("/proc/self/setgroups", "deny")
        write_file("/proc/self/uid_map", "%d %d 1\n" % (SANDBOX_UID, uid))
        write_file("/proc/self/gid_map", "%d %d 1\n" % (SANDBOX_UID, gid))
    except BaseException as e:  # noqa: BLE001
        report(st_w, "F", "%s -- unprivileged user namespaces are unavailable or restricted here; "
               "refusing to run the program unsandboxed" % e)
        os._exit(EXIT_RUNNER)
    pid = os.fork()
    if pid == 0:
        sandbox_init(opts, data, fds, st_w)
    for fd in fds:
        os.close(fd)
    report(st_w, "P", str(pid))
    while True:
        try:
            _, status = os.waitpid(pid, 0)
            break
        except InterruptedError:
            continue
        except ChildProcessError:
            report(st_w, "F", "lost the sandbox process")
            os._exit(EXIT_RUNNER)
    if os.WIFEXITED(status):
        report(st_w, "X", str(os.WEXITSTATUS(status)))
    elif os.WIFSIGNALED(status):
        report(st_w, "S", str(os.WTERMSIG(status)))
    else:
        report(st_w, "F", "unexpected wait status %d" % status)
    os._exit(0)


# ---------------------------------------------------------------- the parent side
class Sink:
    def __init__(self, fd, cap, name):
        self.fd, self.cap, self.name, self.n, self.dropped = fd, cap, name, 0, 0

    def take(self, chunk):
        room = self.cap - self.n
        if room > 0:
            part = chunk[:room]
            view = memoryview(part)
            while view:
                try:
                    w = os.write(self.fd, view)
                except BrokenPipeError:
                    w = len(view)
                view = view[w:]
            self.n += len(part)
        self.dropped += max(0, len(chunk) - max(room, 0))


def run(opts, path):
    try:
        with open(path, "rb") as f:
            data = f.read(MAX_ELF + 1)
    except OSError as e:
        diag("cannot read %s: %s" % (path, e.strerror))
        return EXIT_RUNNER
    if len(data) > MAX_ELF:
        diag("ELF larger than %d bytes; refused" % MAX_ELF)
        return EXIT_RUNNER
    why = check_elf(data)
    if why:
        diag("refused %s: %s" % (path, why))
        return EXIT_RUNNER

    devnull = os.open("/dev/null", os.O_RDONLY | os.O_CLOEXEC)
    out_r, out_w = os.pipe()
    err_r, err_w = os.pipe()
    st_r, st_w = os.pipe2(os.O_CLOEXEC)
    for fd in (out_r, err_r):
        os.set_inheritable(fd, False)
    sys.stdout.flush()
    sys.stderr.flush()
    c1 = os.fork()
    if c1 == 0:
        try:
            os.close(out_r)
            os.close(err_r)
            os.close(st_r)
            helper(opts, data, (devnull, out_w, err_w), st_w)
        finally:
            os._exit(EXIT_RUNNER)
    os.close(devnull)
    os.close(out_w)
    os.close(err_w)
    os.close(st_w)

    sinks = {out_r: Sink(1, opts["max_output"], "stdout"),
             err_r: Sink(2, opts["max_stderr"], "stderr")}
    status_buf = b""
    open_fds = {out_r, err_r, st_r}
    deadline = time.monotonic() + opts["timeout"]
    timed_out = False
    c2 = None
    hard_stop = None
    while open_fds:
        now = time.monotonic()
        if not timed_out and now >= deadline:
            timed_out = True
            for pid in ([c2] if c2 else []):
                try:
                    os.kill(pid, signal.SIGKILL)
                except OSError:
                    pass
            try:
                os.killpg(c1, signal.SIGKILL)
            except OSError:
                pass
            hard_stop = now + 5.0
        if hard_stop is not None and now >= hard_stop:
            break
        wait = (deadline - now) if not timed_out else max(0.0, hard_stop - now)
        try:
            ready, _, _ = select.select(list(open_fds), [], [], max(0.01, wait))
        except InterruptedError:
            continue
        for fd in ready:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                chunk = b""
            if not chunk:
                open_fds.discard(fd)
                os.close(fd)
                continue
            if fd == st_r:
                status_buf += chunk
                for line in status_buf.split(b"\n"):
                    if line.startswith(b"P "):
                        c2 = int(line[2:])
            else:
                sinks[fd].take(chunk)
    for fd in open_fds:
        os.close(fd)
    try:
        os.waitpid(c1, 0)
    except ChildProcessError:
        pass

    for s in sinks.values():
        if s.dropped:
            diag("%s truncated: %d bytes kept, %d dropped (--max-output / --max-stderr)"
                 % (s.name, s.n, s.dropped))

    msgs = [ln.decode("utf-8", "replace") for ln in status_buf.split(b"\n") if ln]
    fails = [m[2:] for m in msgs if m.startswith("F ")]
    ends = [m for m in msgs if m[:2] in ("X ", "S ")]
    if fails:
        for m in fails:
            diag("sandbox setup failed: " + m)
        diag("the program was not run (exit 125)")
        return EXIT_RUNNER
    if timed_out:
        diag("timeout: killed after %d s wall clock" % opts["timeout"])
        return EXIT_TIMEOUT
    if not ends:
        diag("no exit status from the sandbox helper")
        return EXIT_RUNNER
    kind, val = ends[-1][0], int(ends[-1][2:])
    if kind == "X":
        return val
    if val == signal.SIGILL:
        diag("killed by SIGILL")
        return EXIT_SIGILL
    if val == signal.SIGXCPU:
        diag("CPU time limit (%d s) exceeded: SIGXCPU" % opts["timeout"])
        return EXIT_TIMEOUT
    if val == signal.SIGSYS:
        diag("killed by SIGSYS: a syscall outside the seccomp allowlist")
    elif val == signal.SIGKILL:
        diag("killed by SIGKILL (memory or CPU hard limit, or an external kill)")
    else:
        diag("killed by signal %d" % val)
    return 128 + val


USAGE = __doc__


def parse(argv):
    opts = {"timeout": 10, "mem": 512, "nproc": 16, "max_output": 1 << 20,
            "max_stderr": 64 << 10, "seccomp": True}
    ints = {"--timeout": "timeout", "--mem": "mem", "--nproc": "nproc",
            "--max-output": "max_output", "--max-stderr": "max_stderr"}
    path, check_only = None, False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("-h", "--help"):
            sys.stdout.write(USAGE)
            sys.stdout.write("\noptions:\n"
                             "  --timeout S        wall-clock and CPU limit, seconds (default 10)\n"
                             "  --mem MIB          RLIMIT_AS in MiB (default 512)\n"
                             "  --nproc N          RLIMIT_NPROC (default 16)\n"
                             "  --max-output B     stdout bytes passed through (default 1048576)\n"
                             "  --max-stderr B     stderr bytes passed through (default 65536)\n"
                             "  --no-seccomp       namespaces and rlimits only (testing the layers)\n"
                             "  --check-elf        only validate ELF_PATH: exit 0 if freestanding\n")
            sys.exit(0)
        if a in ints:
            if i + 1 >= len(argv) or not argv[i + 1].isdigit() or int(argv[i + 1]) < 1:
                diag("%s needs a positive integer" % a)
                sys.exit(EXIT_RUNNER)
            opts[ints[a]] = int(argv[i + 1])
            i += 2
            continue
        if a == "--no-seccomp":
            opts["seccomp"] = False
        elif a == "--check-elf":
            check_only = True
        elif a == "--":
            if i + 2 != len(argv) or path is not None:
                diag("exactly one ELF_PATH (try --help)")
                sys.exit(EXIT_RUNNER)
            path = argv[i + 1]
            break
        elif a.startswith("-"):
            diag("unknown option %s (try --help)" % a)
            sys.exit(EXIT_RUNNER)
        elif path is None:
            path = a
        else:
            diag("exactly one ELF_PATH (try --help)")
            sys.exit(EXIT_RUNNER)
        i += 1
    if path is None:
        diag("usage: sandbox_run.py [OPTIONS] ELF_PATH (try --help)")
        sys.exit(EXIT_RUNNER)
    return opts, path, check_only


def main():
    opts, path, check_only = parse(sys.argv[1:])
    if check_only:
        try:
            with open(path, "rb") as f:
                why = check_elf(f.read(MAX_ELF + 1))
        except OSError as e:
            why = e.strerror
        if why:
            print("%s: %s" % (path, why))
            return 1
        print("%s: freestanding x86-64 ELF" % path)
        return 0
    if not sys.platform.startswith("linux") or os.uname().machine != "x86_64":
        diag("Linux x86-64 only (this is %s %s)" % (sys.platform, os.uname().machine))
        return EXIT_RUNNER
    try:
        return run(opts, path)
    except Exception as e:  # noqa: BLE001 -- any runner fault fails closed
        diag("internal failure: %s" % e)
        return EXIT_RUNNER


if __name__ == "__main__":
    sys.exit(main())
