# `archivum` beneath one root — `Directorium`, `openat2` and the audit

**Status:** stages 1 and 2 implemented; D11 (`EXS-E0425`) not.
[ADR 0017](../decisions/0017-archivum-beneath.md) was accepted by the
repository owner on 2026-10-02. **Built (stage 1):** the prelude routines
(`compiler/x86_64/prelude/archivum.asm`, `archivum_rodata.asm`), the
audit's openat2 site rules W and A1–A6 (`tools/syscall-audit.sh`), and the
fixtures that run and audit them (`tests/unit/prelude_archivum.asm`,
`tests/unit/audit_openat2.asm`); section 9 records what building them
found. **Built (stage 2, 2026-10-02):** the surface of section 4 —
`m.archivum()`, `Directorium`, `ad_radicem`, `infra`, `lege_ex`, `crea`,
and D4's reader and writer, `Lectorium` and `Scriptorium` — through the
checker, the lowering, both backends, R1–R3 as tests, and §4.2's
`sicut`-to-`sicut` defect fixed; section 10 records it, with what it found.
**Not built:** D11's compile-time path check and its code `EXS-E0425`,
which wait on an explicit decision. Section 2 is measured, on one machine,
by `prototypes/beneath/`; the routines re-measure its refusals through the
prelude's own constants, and `tests/programs/archivum_*/` through the
surface.
**Relates to:** spec §4.1, §4.2, §4.3, §4.6, §4.7, §9.5, §10.3, §11, §13;
`docs/design/runtime.md` section 2.6; `docs/design/checker.md` sections 2.1
and 2.8; `compiler/x86_64/prelude/README.md` ("The syscall table, per
atom"); `tools/syscall-audit.sh` (POTESTATES MODE).

---

## 1. The claim, and why now

A program that holds the `archivum` derived from one root, `/var/lib/x`,
should be unable to *name* anything outside `/var/lib/x`. That means no
`..` that climbs out, no absolute path, no symlink that points out, and no
`/proc/self/fd/N`. It should not be a check the program remembers to make.
It should be something the program cannot write.

The consumer that asked for this is Oligarchy. In one security pass it found
the same bug class three times, each in Rust that already had a check:

- **reliquary, block ids.** `optical::make_iso` joined an unvalidated
  `block_id` onto two roots and deleted whatever the result named. A
  `block_id` of `../../victim/dir` made an ISO of an arbitrary directory and
  deleted an arbitrary `.iso` (Oligarchy/modules/reliquary/docs/ADVERSARY_REVIEW.md,
  item 2).
- **reliquary, sums files.** `hashing::verify_sum_file` joined a filename
  parsed out of a `SHA256SUMS` that came off a USB stick. That turned
  `verify` into a read/existence oracle over every path root can reach
  (Oligarchy's CLAUDE.md, the reliquary landmines).
- **plugind, ids and forbidden paths.** An id of
  `X.service.d/../../../../etc/systemd/system/sshd` reached the path
  `remove_dropin` hands to root's `remove_dir_all`. Separately,
  `forbiddenPaths` was bypassed through ancestors and through symlinked
  spellings (Oligarchy/docs/plugins-roadmap.md, "Every id that reaches root"
  and "`forbiddenPaths` covered the subtree beneath each entry"; Oligarchy
  commits `5de841e`, `f389fdb`).

Each fix is a lexical rule placed at the right call site. Each bug was a
call site that lacked one. The proposal is to move the rule out of the call
site and into the kernel's path walk. The language then gives a program no
way to reach a filesystem except through that walk.

**What is true today.** `archivum` is declared (spec §4.6) and cannot be
derived. §4.7 says "only `m.ambitus()` has a prelude row". The prelude has no
routine gated on `EXS_POTESTAS_ARCHIVUM` (`compiler/x86_64/prelude/README.md`:
"`[OPEN]` | none"). So `tools/syscall-audit.sh --potestates archivum` admits
**no** syscall for it today: its `POTESTATES_TABLE` has `'archivum': {}`.
Whenever `archivum` is named, it prints a note that
`docs/design/runtime.md` section 2.6 tabulates `openat(257)`, `close(3)`,
`fstat(5)`, `lseek(8)`, `read(0)` and `write(1)` for the atom, and that it is
following the README, which admits nothing. That runtime.md row is the one
this document proposes to replace (section 5).

**What does not change.** The compiler's own closed allowlist (CLAUDE.md,
"The compiler is freestanding": `read write close fstat lseek mmap munmap
openat exit_group`) is the first closed set, for `exsc` itself. `exsc` reads
source files and keeps `openat(257)`. Everything here concerns the second
closed set: the per-atom table for compiled programs
(`docs/asm-conventions.md` section 6, as amended).

---

## 2. What the kernel does — measured

**Host.** Linux `6.18.44-fc-v51` (`#1 SMP PREEMPT_DYNAMIC`), x86-64, 4 vCPUs.
The kernel command line names Firecracker, so this is a microVM, not a
namespaced container. The probe ran as uid 0 with the full capability set,
`NoNewPrivs: 0` and `Seccomp: 0` at start. `securityfs` is not mounted
(`/sys/kernel/security/lsm` is absent), so Landlock was found by its
syscall: ABI version 7. `fs.protected_hardlinks = 1` and
`fs.protected_symlinks = 0`. The probe does its three mounts (a tmpfs, a
bind mount of `/etc` and a procfs) inside a private mount namespace. gcc
13.3.0. Run on 2026-10-02 with `prototypes/beneath/run.sh 10`. The
excerpts below are from the tenth run (section 2.7's are from the
eleventh, after that section was added). Every run gave the same matrix
and the same verdicts outside the race. Section 2.4 tabulates the race
run by run.

**Two container effects matter.** First, root bypasses DAC, so every
`EACCES` in section 2.5 is Landlock's and cannot be a permission bit. That
is a property of this run, not a claim about unprivileged callers, which are
`[UNTESTED]`. Second, the mount-crossing rows need `CAP_SYS_ADMIN` to *set
up*, not to *observe*. An unprivileged program sees the same refusals on
mounts someone else made.

### 2.1 The resolution matrix

`openat2(rootfd, path, O_RDONLY, how.resolve = column)`. `rootfd` is an
`O_PATH` descriptor on `W/root`. B = `RESOLVE_BENEATH` (0x08), M =
`RESOLVE_NO_MAGICLINKS` (0x02), S = `RESOLVE_NO_SYMLINKS` (0x04), X =
`RESOLVE_NO_XDEV` (0x01). A cell is the errno, or `ok:` followed by the
first bytes read. `W/outside.txt` holds `ESCAPED`. The block is the probe's
output reformatted for width: link targets are added to the path column,
and repeated long cells are shortened to `ok:…`.

```
path                        0              B           B|M         B|M|S       B|M|X       B|M|S|X
inside.txt                  ok:INSIDE      ok:INSIDE   ok:INSIDE   ok:INSIDE   ok:INSIDE   ok:INSIDE
sub/deep.txt                ok:DEEP        ok:DEEP     ok:DEEP     ok:DEEP     ok:DEEP     ok:DEEP
sub/../inside.txt           ok:INSIDE      ok:INSIDE   ok:INSIDE   ok:INSIDE   ok:INSIDE   ok:INSIDE
.                           ok             ok          ok          ok          ok          ok
../outside.txt              ok:ESCAPED     EXDEV       EXDEV       EXDEV       EXDEV       EXDEV
sub/../../outside.txt       ok:ESCAPED     EXDEV       EXDEV       EXDEV       EXDEV       EXDEV
/etc/passwd                 ok:root:x:0:   EXDEV       EXDEV       EXDEV       EXDEV       EXDEV
link_in      -> sub/deep    ok:DEEP        ok:DEEP     ok:DEEP     ELOOP       ok:DEEP     ELOOP
link_in_abs  -> /…/root/inside.txt
                            ok:INSIDE      EXDEV       EXDEV       ELOOP       EXDEV       ELOOP
link_out     -> ../outside  ok:ESCAPED     EXDEV       EXDEV       ELOOP       EXDEV       ELOOP
link_out_abs -> /etc/passwd ok:root:x:0:   EXDEV       EXDEV       ELOOP       EXDEV       ELOOP
link_dir_out/outside.txt    ok:ESCAPED     EXDEV       EXDEV       ELOOP       EXDEV       ELOOP
link_magic -> /proc/self/fd/N
                            ok:ESCAPED     EXDEV       EXDEV       ELOOP       EXDEV       ELOOP
/proc/self/fd/N             ok:ESCAPED     EXDEV       EXDEV       EXDEV       EXDEV       EXDEV
proc/self/fd/N   (procfs mounted beneath the root)
                            ok:ESCAPED     EXDEV       ELOOP       ELOOP       EXDEV       EXDEV
proc/self/root/etc/passwd   ok:root:x:0:   EXDEV       ELOOP       ELOOP       EXDEV       EXDEV
mnt/file.txt (tmpfs)        ok:MNT         ok:MNT      ok:MNT      ok:MNT      EXDEV       EXDEV
bind/passwd  (bind of /etc) ok:root:x:0:   ok:root:x:0: ok:…       ok:…        EXDEV       EXDEV
hard_out     (hard link to W/outside.txt)
                            ok:ESCAPED     ok:ESCAPED  ok:ESCAPED  ok:ESCAPED  ok:ESCAPED  ok:ESCAPED
"" (empty path)             ENOENT         ENOENT      ENOENT      ENOENT      ENOENT      ENOENT
```

All 120 cells matched the expectation the probe judges against, which was
written from the openat2(2) man page and `fs/namei.c` before the run.

**Findings.**

- **F1. B alone refuses every lexical and symlink escape this tree could
  build, all as `EXDEV`.** That covers `..` out, `..` out through a
  sub-directory, an absolute path, relative and absolute escaping symlinks,
  a directory symlink to `..`, and an absolute symlink that names a path
  *inside* the root (`link_in_abs`). A `..` that stays beneath is allowed
  (`sub/../inside.txt`). So does a relative symlink that stays beneath
  (`link_in`).
- **F2. B already refuses magic links on 6.18**, with `EXDEV`. Reading
  `fs/namei.c` (`nd_jump_link`), that is the scoped-lookup "not safe"
  path, but only the errno is measured. Adding M turns the same rows into `ELOOP`. The
  man page says B's magic-link refusal "may change in the future", so M is
  what pins the guarantee to a flag rather than to a kernel version's
  behaviour.
- **F3. B does not refuse a mount crossing. X does.** A tmpfs and a bind
  mount of `/etc` beneath the root are both read under B|M. The bind mount
  is the notable one: a privileged party who can mount beneath the root
  can make "beneath" contain `/etc`. Under X both are `EXDEV`.
- **F4. Nothing in openat2 bounds an inode, only a name.** A hard link
  beneath the root to a file outside it is read under every flag set. Under
  X, a hard link can only reach the root's own filesystem, because hard
  links cannot cross filesystems. With `fs.protected_hardlinks = 1` (measured
  here), an unprivileged user can only link files they own or could already
  read and write.
- **F5. S buys nothing B lacks for escapes, at a cost.** Every escaping
  symlink is already `EXDEV` under B. S additionally refuses `link_in`,
  which is benign.

### 2.2 Creation, attenuation, file types

```
[2] creation through the root (resolve = B|M)
  O_CREAT via link_dangle (-> ../created.txt)    EXDEV
    ... W/created.txt exists afterwards?         no
  O_CREAT|O_EXCL new.txt                         ok
  O_CREAT|O_EXCL ../new2.txt                     EXDEV
  anti-vacuity: same O_CREAT, resolve = 0        ok
    ... W/created.txt exists afterwards?         yes (the link really does escape)

[2b] attenuation (subfd = openat2(rootfd, "sub", O_PATH|O_DIRECTORY, B|M|X)) and file types
  derive subfd                                   ok
  subfd: deep.txt                                ok
  subfd: ../inside.txt (parent's file)           EXDEV
  subfd: hard_out (hard link to outside)         ok
  fifo, O_RDONLY|O_NONBLOCK                      ok
    ... fstat S_ISFIFO                           yes
  char device 1,3 beneath root                   ok
    ... fstat S_ISCHR                            yes
  char device, O_PATH (no driver open)           ok
    ... fstat on the O_PATH fd                   ok
```

- **F6. A dangling symlink cannot be used to create outside.** This is the
  write half of Oligarchy's "two sequential extracts" bug (CLAUDE.md,
  reliquary: the first extract plants a symlink and the second writes
  through it). The anti-vacuity leg shows the same call creates the file
  outside when `resolve = 0`.
- **F7. Attenuation composes.** A descriptor opened beneath a root is itself
  a root: `..` from it to the parent's file is `EXDEV`.
- **F8. B bounds names, not file types.** A FIFO and a character device
  beneath the root both open. `O_NONBLOCK` is what keeps the FIFO open from
  blocking. `fstat` tells the caller what it got. Opening the device ran its
  driver's `open`, which for `/dev/null` does nothing. Whether some other
  driver's open has effects that matter is not measured here (section 8).

### 2.3 What the kernel validates in `struct open_how`

```
sizeof(struct open_how) = 24, OPEN_HOW_SIZE_VER0 = 24, offsetof(resolve) = 16
size 16 (< VER0)                               EINVAL
size 32, zero tail                             ok
size 32, nonzero tail                          E2BIG
resolve |= 0x80 (unknown bit)                  EINVAL
BENEATH|IN_ROOT together                       EINVAL
mode 0644 without O_CREAT                      EINVAL
flags bit 40 (unknown)                         EINVAL
contrast: openat() with flag bit 30            ok
dirfd = AT_FDCWD (cwd = root), ../outside      EXDEV
```

- **F9. openat2 is strict where openat is lax.** Unknown flag bits, unknown
  resolve bits and a stray mode are all refused. So the 24 bytes the audit
  reads (section 3) are the whole of what the kernel acts on. The kernel
  accepts a larger struct only when the tail is zero, which is why the audit
  pins the size to exactly 24.
- **F10. `AT_FDCWD` with B is beneath the current directory**, which is
  ambient. A dirfd that is an immediate in the binary is therefore never a
  `Directorium` (section 3, rule A6).

### 2.4 The rename race

A racer thread renames `root/a/b` to `W/x/b` and back as fast as it can. The
opener walks `a/b/../../inside.txt`. If the walk reaches `b` while `b` is
under the root and takes the two `..` after `b` has moved, it lands on
`W/inside.txt`, which holds `ESCAPED`. Each run has two legs: `resolve = 0`,
the anti-vacuity leg, and a scoped leg.

| run | secs/leg | scoped leg | scoped opens | **scoped ESCAPED** | scoped EAGAIN | longest EAGAIN run | `resolve=0` opens | `resolve=0` ESCAPED |
|---|---|---|---|---|---|---|---|---|
| 1 | 2 | B\|M | 1,703,936 | **0** | 3,654 | (not tracked) | 1,775,616 | 22 |
| 2 | 5 | B\|M | 3,994,624 | **0** | 15,029 | (not tracked) | 4,111,360 | 20 |
| 3 | 5 | B\|M | 3,788,800 | **0** | 29,538 | (not tracked) | 3,955,712 | 24 |
| 4 | 5 | B\|M | 2,174,976 | **0** | 2,198 | 2 | 2,913,280 | **0 — race did not fire; run uninformative** |
| 5 | 10 | B\|M | 3,129,344 | **0** | 4,759 | 2 | 4,024,320 | 3 |
| 6 | 10 | B\|M | 3,924,992 | **0** | 2,793 | 4 | 3,692,544 | 4 |
| 7 | 10 | B\|M | 6,660,096 | **0** | 15,025 | 3 | 4,574,208 | 2 |
| 8 | 10 | B\|M | 6,220,800 | **0** | 10,962 | 3 | 7,871,488 | 34 |
| 9 | 10 | B\|M\|X | 3,921,920 | **0** | 683 | 1 | 4,661,248 | 2 |
| 10 | 10 | B\|M\|X | 4,584,448 | **0** | 1,713 | 2 | 3,344,384 | 2 |
| 11 | 10 | B\|M\|X | 3,892,224 | **0** | 529 | 2 | 3,723,264 | 1 |

The probe was edited between runs (EAGAIN-run tracking added after run 3,
the B|M|X leg after run 8, section 2.7 after run 10), which is why the columns differ. The remaining
opens in each leg are `ENOENT`, because `a/b` is mostly not there. The
transcripts are not committed. These are the figures they printed.

- **F11. Across 43,996,160 scoped opens under an adversarial renamer, zero
  escapes.** The unscoped leg escaped 114 times in 44,647,424 opens. It
  fired in ten of the eleven runs, so the race is real and rare (about 3
  per million). A test that ran it for a second would usually see nothing. This
  is a measurement on one kernel, not a proof.
- **F12. The scoped walk answers the race with `EAGAIN`, and it is
  transient.** The longest run of consecutive `EAGAIN` seen was 4. Any
  caller has to handle it (D8).

### 2.5 Landlock composition

A child process restricts itself with Landlock (`READ_FILE|READ_DIR`
handled, allowed only beneath `W/root/sub`), then opens:

```
B|M sub/deep.txt (allowed by both)             ok
B|M link_in (-> sub/deep.txt)                  ok
B|M inside.txt (beneath; Landlock denies)      EACCES
B|M ../outside.txt (both deny; who first?)     EXDEV
resolve=0 ../outside (Landlock alone)          EACCES
B|M sub/hard_out (hard link to outside)        ok
B|M sub as O_DIRECTORY                         ok
```

- **F13. They compose, and the errno says which layer refused.** The path
  walk refuses first (`EXDEV`). Landlock refuses what the walk admits
  (`EACCES`). Neither weakens the other. Landlock does not bound inodes
  either: `sub/hard_out` is a hard link to an outside file placed under an
  allowed directory, and it opens. Oligarchy's tier-1 plugins already run
  under Landlock (Oligarchy/docs/plugins-roadmap.md), so an Exsecutor
  program there would sit under both. A `Directorium` is about what a
  program can name. Landlock is about what an operator grants. They answer
  different questions.

### 2.6 What a seccomp filter can and cannot require

```
filter A: openat -> EPERM, openat2 -> ALLOW
  openat ../outside.txt                          EPERM
  openat2 B|M ../outside.txt                     EXDEV
  openat2 resolve=0 ../outside.txt               ok
filter B: openat2 -> ENOSYS (simulates a pre-5.6 kernel; NOT a measurement of one)
  openat2 B|M inside.txt                         ENOSYS
```

- **F14. seccomp cannot require `RESOLVE_BENEATH`.** `how` is a pointer.
  BPF sees the pointer and the size, not the bytes. A filter that denies
  `openat` and allows `openat2` still admits `openat2` with `resolve = 0`,
  and that escapes. So the guarantee has to be static: an audit of the
  binary (section 3) that proves every `openat2` site passes a constant
  containing the flags. This is the same reason `tools/syscall-audit.sh`
  exists for the socket rule.

### 2.7 The proposed constants, passed to the kernel

Section 3's table states four `open_how` constants as hex. The probe
checks that hex against the UAPI headers with `_Static_assert` (gcc and
clang both compile it), then passes each constant to the kernel:

```
[7] the proposed constants, passed to the kernel
  how_radix  AT_FDCWD, absolute root             ok
  how_radix  AT_FDCWD, relative (prelude must refuse) ENOENT
  how_infra  sub                                 ok
  how_lege   deep.txt                            ok
  how_crea   fresh.txt                           ok
  how_crea   fresh.txt again (O_EXCL)            EEXIST
  how_crea   link_in (existing symlink)          EEXIST
  how_crea   hard_out (existing hard link)       EEXIST
```

- **F15. `how_crea` never writes to an existing inode.** It refuses an
  existing name whether that name is a file, an in-root symlink or a hard
  link to a file outside the root. That is what D4 relies on.
- The relative `how_radix` row resolves against the probe's cwd. It fails
  here only because no `root` exists there. That is why the prelude, not the
  kernel, must refuse a relative root (D2).

---

## 3. The audit: proving every `openat2` site passes `RESOLVE_BENEATH`

The flags are in memory, not in a register. The existing audit resolves
`rax` by a linear sweep (two trusted forms, an immediate `mov` and `xor r,r`)
and fails closed on anything else. The same discipline extends to the
pointer, if the prelude makes the pointer resolvable and the target
immutable.

**What the prelude must emit** (a contract on the prelude, specified here
and not built). Every `openat2` site is these four instructions,
contiguous and in this order, emitted by one prelude macro:

```asm truth:ignore
	lea	rdx,[exsrt_how_<name>]	; rip-relative onto a 24-byte constant
	mov	r10d,24			; sizeof(struct open_how), VER0, exactly
	mov	eax,437			; openat2
	syscall
```

`rdi` is the descriptor and `rsi` the path, both loaded earlier. Each
constant is three little-endian `u64`s, `flags`, `mode` and `resolve`, in a
**non-writable** segment. Either the `segment readable` the program wrapper
already opens for literals (`compiler/x86_64/prelude/README.md`, step 5) or
the executable segment the prelude is included into will do. Measured with
`prototypes/beneath/site.asm`: fasmg emits `lea rdx,[how_beneath]` as
rip-relative in an `ELF64 executable`, and the audit's own disassembly mode
(`objdump -D -b binary --adjust-vma`) prints it as

```text truth:ignore
4000f5:	48 8d 15 3a 10 00 00 	lea    rdx,[rip+0x103a]        # 0x401136
4000fc:	41 ba 18 00 00 00    	mov    r10d,0x18
400102:	b8 b5 01 00 00       	mov    eax,0x1b5
400107:	0f 05                	syscall
```

and the 24 bytes at file offset `0x136` (the `R`-only segment, vaddr
`0x401136`) are `0x80000, 0, 0x0b`.

**The rules the audit would add, for each site whose `rax` resolves to 437.**

- **A1. Resolve `rdx`.** Track the `rdx` family (`rdx edx dx dl dh`) exactly
  as `rax` and `rdi` are tracked today, with one new trusted form:
  `lea rdx,[rip+D]`. Its target is *this instruction's address plus its
  length plus D*. The audit already has both numbers: the address column,
  and the byte count from the hex column. It must compute the target itself,
  because `scan()` strips everything after `#` and so never sees objdump's
  `# 0x401136`. Every other write to the family, a `call`, or a value
  carried in from before a `syscall` makes `rdx` unresolved.
- **A2. Resolve `r10`.** Track the family (`r10 r10d r10w r10b`) with the
  immediate-`mov` form. The value must be exactly 24. 32 with a zero tail is
  accepted by the kernel (F9), but it would make the audited bytes and the
  bytes the kernel reads differ, so it is refused.
- **A3. Locate the target.** The 24 bytes `[T, T+24)` must lie inside one
  `LOAD` segment's file-backed range (`filesz`, not `memsz`: bss is zeros at
  run time and not in the file) whose flags lack `W`.
- **A4. Read and judge the constant.** Read the 24 bytes from the file at
  `offset + (T − vaddr)`. They must equal one of the admitted constants,
  byte for byte. The admitted constants are a table in the audit, like the
  syscall numbers, and the table is the prelude's (section 4, D5):

  | name | flags | mode | resolve | `rdi` must be |
  |---|---|---|---|---|
  | `how_radix` | `O_PATH\|O_DIRECTORY\|O_CLOEXEC` (0x290000) | 0 | `NO_MAGICLINKS` (0x02) | the immediate `AT_FDCWD` (−100; a 32-bit `mov edi,-100` disassembles as `0xffffff9c`) |
  | `how_infra` | `O_PATH\|O_DIRECTORY\|O_CLOEXEC` (0x290000) | 0 | `B\|M\|X` (0x0b) | not an immediate |
  | `how_lege` | `O_RDONLY\|O_NOCTTY\|O_NONBLOCK\|O_CLOEXEC` (0x80900) | 0 | `B\|M\|X` (0x0b) | not an immediate |
  | `how_crea` | `O_WRONLY\|O_CREAT\|O_EXCL\|O_NOCTTY\|O_CLOEXEC` (0x801c1) | 0o600 | `B\|M\|X` (0x0b) | not an immediate |

  The flag values are x86-64 Linux's. Each hex value is checked against the
  UAPI headers and passed to the kernel by the probe (section 2.7).
- **A5. `how_radix` is the only constant without B**, and it is admissible
  only with `rdi = AT_FDCWD` and an absolute path. The audit can check the
  former. The prelude checks the latter at run time (D2). This is the one
  site that turns the `archivum` atom into a root. Every other site opens
  beneath a descriptor.
- **A6. On every B constant, `rdi` must not resolve to an immediate.** An
  immediate dirfd is `AT_FDCWD`, which F10 shows is beneath the *cwd* and so
  ambient, or a guessed descriptor number. Neither is a value the type
  system can produce. An unresolved `rdi` is admitted. That it holds a
  `Directorium`'s descriptor is the checker's guarantee, not the audit's.

**"Indeterminate" means FAIL**, with the same words the script already uses
for an unresolvable `rax`. The cases are: `rdx` or `r10` unresolved; `r10`
not 24; a target outside every file-backed `LOAD` range, in a writable
segment, or straddling a segment end; bytes that match no admitted
constant; `rdi` immediate on a B constant; `rdi` not `AT_FDCWD` on
`how_radix`. None is skipped and none is a warning. A site the audit cannot
prove is a site it refuses.

**And `openat(257)` is not admitted under any program atom** (section 5).
So a binary that carries a plain `openat` fails `--potestates` whatever it
names. There is no unscoped open for a site to fall back to.

**What the audit inherits and does not fix.** The sweep is linear and
heuristic, as the script's own header says. A direct or indirect jump that
lands between the `lea` and the `syscall` would carry a different `rdx`
into a site the sweep judged with the `lea`'s. Requiring A1–A2's four
instructions to be contiguous narrows the window to that sequence. Refusing
any direct branch whose target falls strictly inside such a window is a
further check the sweep could make, because objdump prints direct targets.
Indirect branches cannot be bounded this way. `[OPEN]` whether the emitter
ever emits one (section 8). The audit is also atom-granular, not
descriptor-granular. A `write(1)` to an unresolved fd under `archivum` is a
file write or a stdout write, and the binary does not say which (section 5).

**Self-test fixtures — built (stage 1; section 9 has the result).** `site.asm`'s three shapes become
`tests/unit/` fixtures: site 1 must PASS under `--potestates archivum`,
site 2 (rdx from a register) must FAIL as indeterminate, and site 3
(constant in the `RW` segment) must FAIL. A fourth fixture needs a constant
with `resolve = 0` in an `R` segment, which must FAIL A4. That one is the
anti-vacuity case: without it, A4 could be checking nothing.

---

## 4. The language design

### D1. `Directorium`: what it is

`structura Directorium { a: archivum, descriptor: i32 }` is a prelude type.
It is capability-bearing with mark `{archivum}` (spec §4.3), 16 bytes, and
uses `Scriptor`'s layout, the same representation byte for byte. The
descriptor is an `O_PATH|O_DIRECTORY` descriptor, and every operation on
the value is an `openat2` relative to it.

**Its fields have no rows**, which is `Lector`'s precedent
(`compiler/x86_64/prelude/interface.inc`, "ITS FIELDS HAVE NO ROWS").
Here it is a soundness requirement, not a convenience. If `d.a` resolved,
a holder of one `Directorium` could take out the unscoped atom and derive
any root. With no row, `d.a` is `EXS-E0305` ("operation not defined on the
type": this read `EXS-E0301` until stage 2 measured it -- the checker gives
every member a type lacks E0305, `Lector`'s `l.a` included). `d.descriptor`
is refused for the same reason: an integer descriptor could be handed to an
`openat` the program does not have, or compared, or forged into a second
`Directorium` by a layout cast. §5.2's one aggregate cast is over
`@transitus` types, and a capability-bearing type must never be one.
*(Measured in stage 2: a `@transitus` struct holding a `Directorium` is
refused, `EXS-E0321`, because the prelude record is `:nativus` -- not
because it is capability-bearing, which no code says. And
`d sicut acies<u8, 16>` and its inverse are `EXS-E0305`.
`tests/unit/chk_directorium.asm` rows 11-13.)*

The name follows §3.4: `-orium` on a supine stem (`direct-`, from `dirig-`)
gives an instrument, declared `structura`. `dirig-` is not in §3.3's
illustrative table and `lexicon.norma` does not exist, so under §3.9 the
spelling is provisional. The method names below are provisional too.

### D2. Obtaining one: from `Mundus`, at run time, fallibly

```exsecutor truth:ignore
publica functio initium(m: Mundus) -> u8 {
    firma a = m.archivum();                                  // total: a host property (§4.7)
    firma radix = Directorium.ad_radicem(a, "/var/lib/x");   // eventus<Directorium, erratum>
    …
}
```

§4.7 already says `m.archivum()` is total, because whether a filesystem
exists is decided at compile time by `--hospes`. That stays. Naming a root
is a different event. The directory may not exist or may not be a
directory, the kernel may lack `openat2`, and a sandbox may refuse it. So
`Directorium.ad_radicem(a: archivum, via: textus) -> eventus<Directorium, erratum>`
is fallible **at run time**. It is the third case of §4.7's "fallibly is
resolved at two different times", and it is the same shape as
`Scriptor.ad_exitum(a: ambitus)`: an associated function that takes the
atom as an **explicit value**.

- **Run-time path, not build-time.** Both of Oligarchy's consumers take
  their root from configuration (reliquary's store, plugind's
  `stateDir`). A root fixed in source would serve neither. When `via` is a
  literal, the compiler could record it in the ego's `potestates`
  (`archivum[/var/lib/x]` rather than `archivum`), which would make §10.3's
  audit name the tree and not just the atom. `[OPEN]`: that is an ego format
  change (§10.1) and is not proposed here.
- **Absolute only.** The prelude refuses a `via` that does not begin with
  `/` before any syscall. A relative root would be relative to the cwd,
  which is ambient (F10).
- **Symlinks in the root's own spelling are followed, magic links are not**
  (`how_radix`: `RESOLVE_NO_MAGICLINKS`, `rdi = AT_FDCWD`). NixOS spells
  `/var/run` as a link to `/run`, and Oligarchy's own forbidden-path bug
  turned on exactly that. The holder of the atom could name either spelling
  anyway. Refusing the link would only make roots fragile.
- **The root is an inode, fixed at derivation.** Once `ad_radicem` returns,
  renaming or re-pointing the path it was spelled with does not move the
  `Directorium`. That closes the check-then-use window in which plugind
  canonicalises a path at install time and Landlock opens it later.

### D3. Attenuation: only downward

`d.infra(via: textus) -> eventus<Directorium, erratum>` opens `via` beneath
`d` with `how_infra` and returns a new `Directorium` rooted there. F7 shows
the new root cannot climb back to its parent. **No operation widens.** There
is no `supra`, no accessor for the descriptor or the path, and no way from
a `Directorium` back to `archivum`. A function holding a `Directorium` for
`/var/lib/x/plugins` and nothing else cannot open `/var/lib/x/secret`.

### D4. Opening a file

- `d.lege_ex(via) -> eventus<…, erratum>` opens an existing file to read,
  with `how_lege` (`O_RDONLY|O_NOCTTY|O_NONBLOCK|O_CLOEXEC`). It then
  `fstat`s the descriptor and refuses anything that is not a regular file:
  it closes the descriptor and returns the error. F8 is why: a FIFO beneath
  the root would otherwise block the program forever, and a device node
  would be read as data. `O_NONBLOCK` is what lets the FIFO be rejected
  rather than waited on. On a regular file, Linux ignores it for reads.
- `d.crea(via) -> eventus<…, erratum>` creates a **new** file, with
  `how_crea` (`O_WRONLY|O_CREAT|O_EXCL|…`, mode `0600`). `O_EXCL` fails on
  an existing name, whether file, symlink or hard link (F15), so a write
  never lands on an inode the program did not create. That is what makes
  F4's hard link harmless for writes. The mode is a constant so the audit can read it.

  Rewriting or appending an existing file is not in v1. Truncating through
  a planted hard link is the hazard, and it is `[OPEN]` (section 8).
- **The result types** *(decided in stage 2: `Lectorium` and `Scriptorium`,
  section 10)*. They are a reader and a writer with
  mark `{archivum}`. `Lector` and `Scriptor` are fixed to `{ambitus}` by
  their `a: ambitus` field. So it is either those two made generic over the
  atom they carry, or two new prelude types, which §3.9 governs. Until
  `eventus` is inhabited (`docs/design/sum-types.md` D5), none of these can
  be called. `lege_octeto`'s 256 sentinel does not generalise to a handle,
  so this design waits on D5 rather than inventing a stopgap.

### D5. The resolve constant: `B|M|X`, not S, not IN_ROOT

Every beneath site uses `0x0b`.

- **B** is the guarantee (F1).
- **M** pins it to a flag rather than to 6.18's behaviour (F2).
- **X** refuses mount crossings (F3), and it is the one with a cost: a
  program whose tree has a mount inside it must derive a second
  `Directorium` for that mount from `Mundus`. Its authority grows, but
  visibly. What X buys: "beneath" means one filesystem, a hard link can
  reach only that filesystem (F4), and a bind mount beneath the root cannot
  make it contain `/etc`.
- **Not S**, because it adds nothing against escapes and refuses benign
  in-root links (F5).
- **Not `RESOLVE_IN_ROOT`**, which re-reads an absolute path or symlink as
  relative to the root instead of refusing it. A program could then open
  `root/etc/passwd` believing it had opened `/etc/passwd`. A refusal cannot
  be misread that way.

One constant per operation, not a caller-chosen flag set. A flag the
caller can choose is a flag the audit must judge at every site.

### D6. Passing a `Directorium` to library code: rows and marks

A library function takes one the way `imprime_gutenbergio` takes a
`Scriptor`:

```exsecutor truth:ignore
publica functio salva(d: Directorium, nomen: textus, t: textus) -> … poscit sicut d { … }
```

**What bounds such a function is the set of VALUES it holds, not its row,
and not the `sicut`.** Every prelude row on a `Directorium` is empty
(section 10: `infra`, `lege_ex` and `crea` draw no atom), so a function
that merely USES one needs no `poscit` at all and its own row shows nothing
for `archivum`. `poscit sicut d` is needed only to FORWARD the handle to a
callee that is itself declared `sicut`: by §4.2's table `sicut d`
substitutes `Directorium`'s mark, `{archivum}`, at that call, and the
caller's `sicut` covers it. (An earlier version of this section said the
function's row "reads `{archivum}`" and that it "can reach that tree and
nothing else" because of it. Both were wrong: the row is empty or `sicut d`,
and it bounds nothing.) A row says *which atom*, never *which tree*. What
keeps the function from reaching any other tree is that it cannot obtain the
raw atom, and that rests on three properties of the checker
(`docs/design/checker.md` sections 2.1 and 2.8), pinned by tests:

- **R1. The raw atom cannot be obtained from a `Directorium`.** Three
  rules, each of which was needed, and the first two of which were found
  missing by review:
  1. A `sicut` row item never binds the atom's carrier. Only a `poscit P`
     *atom* item sets `cap[P]` in the function's root frame (checker.md
     2.1, "Bind"). So inside `salva`, `archivum` in expression position is
     `EXS-E0421`, and the body cannot call
     `Directorium.ad_radicem(archivum, "/")`.
  2. **`sub P = e` requires `e` to have the atom's own capability type**
     (`EXS-E0303`; spec §4.5). `sub` is the one statement that mints an
     atom, and it used to accept a provider of any type that was not
     another atom's: `sub archivum = d;` with `d: Directorium` bound the raw
     atom, and the program that did so (built and run) read a file outside
     its root and exited 42. Rule 1 does not reach it, because the `sub` IS
     the provider. `alloc` is the one exemption (spec §4.5 types its
     provider only as "an arena", `[OPEN]`).
  3. **A lambda's draw is read at its live row** when the lambda is called
     or forwarded (`__chk_row_lamof`). A lambda's row is inferred, and the
     call used to read the row of its TYPE, `{}`, so a closure over
     `archivum` called inside `salva` contributed nothing and was accepted.
     It is `EXS-E0421` at the call now. `[OPEN]`, not claimed: a lambda that
     reaches a parameter whose function type has an empty row, is stored in
     a field, is returned, or sits in a `mutabilis` local is still read at
     its type's row. No such shape produces an object (the lowering refuses
     every lambda), and an atom with no carrier is `EXS-E0421` in the
     lowering rather than a trap.
- **R2. `ad_radicem` takes the atom as an explicit value.** No prelude
  routine draws `archivum` implicitly. With R1, a function that holds only a
  `Directorium` has no expression of type `archivum` -- *as a consequence
  of R1's three rules*, not by itself.
- **R3. The fields have no rows** (D1).

A library that writes `poscit archivum` is asking for the whole atom, and
its caller must provide one (a `sub` or a parameter). That request is in its
signature. §10.3 renders `poscit archivum` and `poscit sicut d` the same
way, as `archivum`. `[OPEN]` whether the audit view should tell them apart
(for example `archivum (Directorium)`), since the difference is exactly the
one a reviewer needs.

**A dependency, not a footnote.** §4.2 records a checker defect: a function
declared `poscit sicut s` that calls another declared `poscit sicut s` with
the same `s` is refused `EXS-E0421` (`docs/design/wire-codec.md`, finding
9). Every library that forwards a `Directorium` to a helper is that shape.
The defect has to be fixed before this design is usable past one call
level.

*(Lowering -- answered in stage 2, section 10: no hidden carrier is passed
beside a `Directorium`.)* checker.md 2.8 makes a row's atom items hidden
arguments and says a `sicut` parameter's carriers "travel inside that
argument's closure". For a `structura` argument, it had to be confirmed that
no hidden `archivum` carrier, the caller's full atom, is passed alongside a
`Directorium`. R1 means the callee could not name one. Not passing it at all
is defence in depth.

Closures (§4.2's capture rule), generics (§7.1) and `dyn` (§4.4) need
nothing new. A captured `Directorium` puts its mark in the closure's type,
and `Directorium` is a concrete `structura`. Storing one in module-level
state is already `EXS-E0501`.

### D7. Lifetime: no explicit close on a copyable handle

A `Directorium` is a 16-byte value and is copied freely. If one copy could
close the descriptor, the kernel would reuse the number for the next open.
A stale copy for `/var/lib/x` would then name whatever was opened next,
possibly a root that copy's holder was never given. That is capability
confusion by descriptor reuse, and it needs no bug in the kernel or the
audit. So v1 has **no surface `close`** for a `Directorium` or for D4's
handles: descriptors are released at process exit. The real answer is
deterministic destruction (§6.6) of a refcounted handle, closing at
release-to-zero. `[OPEN]`: `refero` does not yet carry a resource
destructor (`docs/design/runtime.md` section 2.6). A long-running daemon
that derives many roots is bounded by `RLIMIT_NOFILE` until then.

### D8. `EAGAIN`: bounded retry, then failure

The scoped walk returns `EAGAIN` when a rename or mount races it (F12). The
prelude retries the same call up to **16** times, then returns the error.
The longest run measured under a dedicated adversarial renamer was 4. 16 is
a guess with a 4× margin, and `[UNTESTED]` under other loads. Failing after
the bound is the safe direction: a lost open, not an escape.

### D9. Old kernels and sandboxes: fail closed, never fall back

`openat2` first appeared in Linux 5.6. That is the openat2(2) man page,
cited and not measured here: section 2's only kernel is 6.18. On a kernel
without it the call returns `ENOSYS`. A seccomp policy that does not know
the syscall returns `ENOSYS` or `EPERM`, depending on the policy. All three
make `ad_radicem` return its error, so the program has **no filesystem at
all**. The prelude never retries with `openat`. A fallback would bring the
hole back on exactly the hosts and sandboxes that blocked the safe call, and
`openat(257)` is not in the atom's table anyway (section 5), so the audit
would refuse the binary. F14's filter B shows what the program sees. A real
pre-5.6 kernel is `[UNTESTED]`.

`--hospes` (§9.5) names a target, not a kernel version. `[OPEN]` whether
`x86_64-linux` should come to mean "≥ 5.6 when `archivum` is in the closure",
or whether the run-time refusal is enough. This design takes the run-time
refusal. It needs no new spec machinery and it fails closed.

### D10. The path buffer

A `textus` is a pointer and a length. The kernel wants a NUL-terminated
string, so the prelude copies `via` into a buffer of `PATH_MAX` (4096)
bytes and appends a NUL. A `via` with an interior NUL is refused. It cannot
escape, because truncation only shortens and B still applies, but it would
open a different file from the one the program named. A `via` of 4096
bytes or more is refused. The buffer is prelude-owned and not reentrant.
*(As built, section 9: the buffer is in the calling routine's stack frame,
which is reentrant now; the limit and the refusals are the same.)*
`[OPEN]` until threads exist (`Filum`, §4.6).

### D11. A compile-time check for path literals (optional)

When `via` is a literal that is absolute or contains a `..` component
reaching above its start, the call cannot succeed: the kernel answers
`EXDEV` (F1). A compile-time diagnostic would report it earlier. Soundness
does not need it. ADR 0017 proposes it as a §13 code, `EXS-E0425`, **as a
proposal only**, and no code is added here. On 2026-10-02 the number is
unused anywhere in the tree: a grep for `E0425` over every `.md`, `.inc`,
`.asm` and `.py` file returned nothing.

---

## 5. The atom → syscall table change

| atom | today (`tools/syscall-audit.sh`, as built) | today (`runtime.md` 2.6, design) | proposed |
|---|---|---|---|
| `archivum` | nothing | `openat(257)`, `close(3)`, `fstat(5)`, `lseek(8)`, `read(0)`, `write(1)` | **`openat2(437)`**, `close(3)`, `fstat(5)`, `lseek(8)`, `read(0)`, `write(1)` to any fd. **Not `openat(257)`** |

- **`archivum` loses `openat(257)`.** Nothing about `openat` can be bounded:
  its path is resolved from whatever dirfd and spelling it is given. If
  `openat` stays admissible, one site that uses it is a hole the audit
  cannot see.
- **`write(1)` and `read(0)` to an unresolved fd** become admissible under
  `archivum` as well as `ambitus`. In the script, `classify_potestates`'s
  write case would test `'ambitus' in atoms or 'archivum' in atoms`. The
  imprecision is inherited from the existing design: the binary does not
  say whether an unresolved fd is a file or stdout.
- **Not in v1:** `mkdirat(258)`, `unlinkat(263)`, `renameat2(316)`,
  `symlinkat(266)`, `linkat(265)`, `getdents64(217)`. None of them takes
  `RESOLVE_*`. Each resolves its path with the ordinary walk, so each would
  be an unscoped hole. When they are needed (reliquary's extractor needs
  `mkdirat`; plugind's `remove` needs `unlinkat`), the pattern is fixed.
  First `openat2` the **parent** with `how_infra`. Then pass the syscall
  that parent descriptor and **one** name component that the prelude has
  checked lexically: non-empty, no `/`, not `.` or `..`. A single component
  cannot climb, and `unlinkat` and `renameat2` do not follow a final
  symlink. That rule is `[UNTESTED]` and is recorded so the first person to
  add `unlinkat` does not reach for a full path.
- The **compiler's** own nine are unchanged (section 1).
- `docs/design/runtime.md` section 2.6's row and the prelude README's row are
  the two places this lands when it is built. ADR 0017 gives the spec text.
  This document edited neither; ADR 0017's stage 1 changed both (section 9).

---

## 6. Oligarchy, consumer by consumer

What a `Directorium` would have done to the three findings in section 1,
if those programs were written in Exsecutor. They are Rust, so this is a
statement about the design, not about Oligarchy's code.

| finding | with a `Directorium` | what still needs a lexical rule |
|---|---|---|
| reliquary `block_id` → `make_iso` | the store is a `Directorium`, and the block is `store.infra(block_id)`. `../../victim` is `EXDEV` at the kernel, with or without `validate_block_id` | the id grammar (`YYYYMMDD-` + 16 hex) is still worth keeping for good errors. It stops being the only thing between a typo and root's `rm` |
| reliquary `SHA256SUMS` names | the block is a `Directorium`, and each named file is `block.lege_ex(name)`. A name that climbs or is absolute is `EXDEV`, and a symlink out is `EXDEV` | the fix also refuses any `/`. A `Directorium` permits `sub/x` beneath the block, so "a flat list" remains a lexical rule |
| reliquary extract (tar slip, two-step symlink) | an Exsecutor extractor would create through `how_crea`. A planted symlink pointing out is `EXDEV` on create (F6), and `O_EXCL` refuses writing through any existing name | `examples/arca` already refuses symlink and device members. Directories need `mkdirat` with the single-component rule (section 5), `[OPEN]` |
| plugind id → `remove_dir_all` | the drop-in directory is a `Directorium` and the id is `dropins.infra(id)`. The `sshd` path is `EXDEV` | removal needs `unlinkat` (section 5), `[OPEN]` |
| plugind `forbiddenPaths` (ancestors, symlinked spellings) | **not solved by this.** That bug is a *policy comparison* between two path sets, made before granting a Landlock rule to someone else. A `Directorium` bounds what *this* program can name | A `Directorium`'s inode identity (`fstat`'s `st_dev`/`st_ino`, D2) gives a comparison that spelling cannot defeat. Building that is `[OPEN]` |
| plugind state dirs | each plugin's state dir is one `Directorium`, derived once by the host and handed in. The plugin cannot name its neighbour's | — |

So the design removes two of the three bug classes (a name that climbs out
of a root) and does nothing for the third (comparing two policies).

---

## 7. How it was measured

```sh truth:ignore
sudo prototypes/beneath/run.sh 10     # needs root; private mount namespace
fasmg prototypes/beneath/site.asm site.elf   # INCLUDE=vendor/fasmg-x86
dd if=site.elf of=site.text bs=1 skip=$((0xb0)) count=$((0x136-0xb0))
objdump -D -b binary -m i386:x86-64 -M intel --adjust-vma=0x4000b0 site.text
```

The probe prints `== N held, M unexpected ==` and exits nonzero on any
unexpected row. Final run (the eleventh): `156 held, 0 unexpected`. Of the
eleven runs, one reported `1 unexpected`: run 4's anti-vacuity race leg,
recorded in section 2.4.

---

## 8. Open

- **Unprivileged callers** (section 2's host note): every row is from uid 0.
- **A real kernel older than 5.6**: only simulated (D9).
- **Oligarchy's kernels**: not measured here. Its CachyOS kernels are far
  past 5.6, but no row above was taken on them.
- **Device-node opens with side effects** (F8): `lege_ex` refuses a
  non-regular file *after* opening it. Opening `O_PATH`, `fstat`ing, then
  reopening would avoid the driver open, but the reopen is either a second
  walk by name (a race) or a walk through `/proc/self/fd`, a magic link,
  which M refuses. A `nodev` mount is the operational answer.
- **Hard links in adversary-writable roots** (F4): reads follow them.
  Refusing `st_nlink > 1` would break Nix-optimised trees. Undecided.
- **Rewrite and append of existing files** (D4): would need a truncate or
  append open, which F4's hard link turns into a write outside.
- ~~**The result types' spelling** (D4) and **`eventus`'s inhabitation**~~
  -- closed by stage 2 (section 10); the spellings stay provisional
  under §3.9.
- ~~**R1–R3 as tests** (D6), the **`sicut`-to-`sicut` checker defect**
  (§4.2), and **whether a hidden full-atom carrier is passed** (D6,
  lowering)~~ -- closed by stage 2 (section 10).
- ~~**`@transitus` on a capability-bearing type** (D1)~~ -- refused, by
  `EXS-E0321` and for the byte-order reason (D1's note).
- **A reader buffer**: `exlege_octeto` is one `read(2)` per byte (section
  10).
- **Indirect branches into an `openat2` window** (section 3).
- **`archivum[path]` in the ego** (D2) and **telling `poscit archivum` from
  `poscit sicut d` in §10.3's view** (D6).
- **`mkdirat`/`unlinkat`/`renameat2`/`getdents64`** under the
  single-component rule (section 5).
- ~~**A4's constants as prelude bytes**~~ — closed by stage 1: fasmg
  assembles them into `segment readable` (`archivum_rodata.asm`), the audit
  reads them back byte for byte, and `tests/unit/prelude_archivum.asm`
  passes them to the kernel (section 9).

---

## 9. Stage 1, as built (2026-10-02)

ADR 0017 was accepted on 2026-10-02 and built in two stages. Stage 1 is
everything that does not need `eventus`. Nothing below refutes a decision;
three implementation choices go beyond or differ from the text above, and
they are recorded as such.

**The routines.** `compiler/x86_64/prelude/archivum.asm` is a second
executable blob beside `prelude.asm`, not an `include` in it (runtime.md H1
forbids `include` there: `OUT` is self-contained). The four constants are a
third file, `archivum_rodata.asm`, for `segment readable`: in the
executable segment the audit's linear sweep would decode them as
instructions. Both are gated on `EXS_POTESTAS_ARCHIVUM` and assemble to
zero bytes when it is 0, so a binary without the atom is unchanged. The
routines carry the private prefix (`exsrt_archivum_radix`, `_infra`,
`_lege_ex`, `_crea`, `_lege`, `_scribe`) because `interface.inc` declares
none of them: no IR can call them until stage 2. They return the result or
a negated errno, the raw kernel convention, until `erratum` has variants.

**Departures from the text, each deliberate:**

- **D10's buffer is in the caller's frame** (4096 bytes), not
  prelude-owned. Same limit, same refusals, and reentrant without waiting
  for threads.
- **A negative directory descriptor is refused at run time** (-EBADF) on
  every scoped call. A6 refuses `AT_FDCWD` as an *immediate*; this refuses
  it as a *value*, which A6 cannot see. Defence in depth, not in D1–D11.
- **The prelude's own refusals are spelled with errnos**: -EINVAL for a
  relative root, an interior NUL and a non-regular `lege_ex` target;
  -ENAMETOOLONG for 4096 bytes or more. Provisional, for `erratum`.

**Measured through the routines** (`tests/unit/prelude_archivum.asm`, 51
checks, exit 0, Linux 6.18.44 in this Firecracker VM, as uid 0): every
section 2 refusal reproduces through the prelude's bytes — `..` out,
`nexus_foras` (a relative symlink out), an absolute path and an absolute
symlink are EXDEV; a magic link beneath a root is ELOOP; `/` to
`proc/self/status` is EXDEV (X); a child root cannot reach its parent's
file (F7); `how_crea` refuses an existing file, an in-root symlink and an
escaping one with EEXIST (F15); `how_radix` refuses `/proc/self/cwd` with
ELOOP and follows `nexus_absolutus -> /`. Each refusal has a legal twin.
**Non-vacuity, measured by mutation** in a scratch copy: changing the
scoped constants' resolve to 0x00, 0x03 (B dropped), 0x0a (X dropped),
0x09 (M dropped) or 0x0f (S added) made the fixture fail at checks 6, 6,
22, 18 and 5 respectively, and `how_radix` with resolve 0 failed at
check 19. The EAGAIN retry, ENOSYS/EPERM and EINTR remain `[UNTESTED]`
(no racer, no seccomp); that there is no fallback is proven statically
instead — the binary has no `openat(257)` and passes `--potestates
Mundus,archivum`.

**The audit.** Rules W and A1–A6 are as section 3 states; W (the four
instructions contiguous, in order) is checked as its own rule. A1 computes
a `lea`'s target from the *next* instruction's address, not from a byte
count (objdump wraps long instructions) and never from objdump's comment.
`tests/unit/audit_openat2.asm` holds eleven cases under one `CASUS` symbol;
the self-test requires case 0 (four sites, the prelude's own four
constants) to pass and each other case to fail naming exactly its rule set:
resolve 0 {A4}, B|M {A4}, writable segment {A3}, `rdx` from `rcx` {W, A1},
`r10` = 32 {A2}, `how_radix` with `rdi` from memory {A5}, `how_lege` with
`rdi` = AT_FDCWD {A6}, an unrelated instruction in the window {W},
`openat(257)` (not admitted), a constant cut short by its segment's end
{A3}. Two changes reach beyond `openat2`: `xchg`/`xadd`/`cmpxchg` and
implicit writers now unresolve every tracked register they write (they
could only turn a PASS into a FAIL), and the i386 gates `int 0x80` and
`sysenter` are refused everywhere, in both modes (`tests/unit/legacy_gate.asm`;
found missing by another agent's measurement while this was built).

**Still `[OPEN]` after stage 1**, beyond section 8: the routines are not
carried into `OUT` (`backend_fasmg/program.inc` copies two blobs; it must
copy these two as well, which is a backend change for stage 2 -- *done,
section 10*); `lseek(8)` is in the row and issued by no routine.

**Stage 2 needs from `eventus`:** a two-variant `eventus<T, E>` with a
layout the lowering can return from an IR call, a constructor for each
variant, and a pattern to take it apart, so that `Directorium.ad_radicem`,
`d.infra`, `d.lege_ex` and `d.crea` can return `eventus<_, erratum>`; and an
`erratum` that can carry at least the kernel's errno and the prelude's
four refusals. With those, stage 2 is: `interface.inc` rows (and
`bfausr_`-prefixed entry points) for the four calls and `m.archivum()`,
the reader and writer types (D4), the checker's R1–R3 as tests, D11's
compile-time check with `EXS-E0425` registered in §13 first, and
`program.inc` carrying both blobs.

---

## 10. Stage 2, as built (2026-10-02)

Stage 2 is the surface: everything in section 4 except D11. It was built
on `eventus` as `docs/design/sum-types.md` D5 left it, in four commits:
the §4.2 checker defect, the checker surface, the lowering and backends,
and the programs and documents. Nothing below refutes a decision. Three
findings correct this document or the spec, and are marked.

**The surface, as declared** (`compiler/x86_64/prelude/interface.inc`
rows 11-21, appended; `checker/types/prim.inc` types them, all with the
EMPTY row):

| call | result | entry point |
|---|---|---|
| `m.archivum()` | `archivum` (total) | `bfausr_exsrt_mundus_archivum` |
| `Directorium.ad_radicem(a: archivum, via: textus)` | `eventus<Directorium, erratum>` | `..._directorium_ad_radicem` |
| `d.infra(via)` | `eventus<Directorium, erratum>` | `..._directorium_infra` |
| `d.lege_ex(via)` | `eventus<Lectorium, erratum>` | `..._directorium_lege_ex` |
| `d.crea(via)` | `eventus<Scriptorium, erratum>` | `..._directorium_crea` |
| `r.exlege_octeto()` | `eventus<u16, erratum>`: the byte, 256 at end of file | `..._lectorium_exlege_octeto` |
| `w.inscribe(t: textus)` | `eventus<mensura, erratum>`: the length, never short | `..._scriptorium_inscribe` |
| `w.inscribe_octeto(b: u8)` | `eventus<mensura, erratum>`: 1 | `..._scriptorium_inscribe_octeto` |

`erratum.numerus` is the errno: the kernel's, or the prelude's own refusals
(section 9). The `eventus` the prelude writes is the layout the lowering
reads -- tag at byte 0 (`prosperum` 0, `adversum` 1), payload from byte 1,
so `eventus<Directorium, erratum>` is 17 bytes -- stated in interface.inc
part A and asserted against `archivum.asm`'s constants; a program that
took one apart at a different offset would fail every check in
`tests/programs/archivum_radix/`. The prelude's `eventus` and `erratum`
are found by name and by their zero span (`ast_decl_synth`), never by
scope, so a module that shadows `eventus` still gets the prelude's; the
three new type names join `prelude/eventus.inc`'s triggers.

**D4, the reader and writer: two new prelude types**, `Lectorium` and
`Scriptorium`, each `Scriptor`'s record with mark `{archivum}` and no field
rows -- not `Lector`/`Scriptor` made generic over the atom. That was the
smaller change by a wide margin: a prelude struct has no `AstDecl` to hang a
type parameter on, its mark would have to depend on the argument, and
nothing sizes or names an instance of a generic prelude type; and it left
`Lector`, `Scriptor`, their rows, their routines and every fixture that pins
them byte for byte as they were. `-orium` is spec 3.4's instrument, the
suffix `Directorium` already uses (`lectorium` is spec 3.7's own
derivation). The METHODS needed new names because a prelude member is found
by NAME in one pool (`chk_ty_pre_names`, and the lowering's
`__lwr_pre_row`), so a second `lege_octeto` or `scribe` would be unreachable
behind the first: `exlege_octeto` and `inscribe`/`inscribe_octeto`, whose
`ex-` and `in-` follow spec 3.5's laws (the first parameter is the source,
the destination). All spellings provisional under §3.9.

**R1-R3, as tests** (`tests/unit/chk_directorium.asm`, 16 rows, exact
codes): `archivum` in a function whose row is only `sicut d` is EXS-E0421
(R1 rule 1); `sub archivum = d;` over a `Directorium` and `sub rete = <u32>`
are EXS-E0303 (R1 rule 2, rows 14-15, with the `alloc` exemption pinned by
row 16); every value row is empty and `ad_radicem` without its atom is
{EXS-E0303, EXS-E0304} (R2); `d.a`, `d.descriptor`, `r.a`, `w.descriptor`
are EXS-E0305 (R3). The closure shapes of R1 rule 3 are rows 11-18 of
`tests/unit/chk_row_sicut_forward.asm`. Each property was broken on purpose
in a scratch copy and the fixture failed at the row predicted (the commits
have the exits).

**Finding 1 -- R1 was false, through a parameter's TYPE.** A row written
inside a parameter's function type (`g: functio(u8) -> u8 poscit
{archivum}`) bound its atom's carrier in the function's own frame
(`checker/resolve/resolve.inc`, `__chk_rowitem`), so a function declaring
only `sicut d` could name `archivum` and derive any root. Measured, then
fixed: a type's row is resolved and never a provider.
`tests/unit/chk_row_sicut_forward.asm` row 10 and `chk_directorium.asm`
row 5 pin it. It predates this design; it would have broken it.

**Finding 2 -- §4.2's `sicut`-to-`sicut` defect, fixed.** D6's dependency.
`checker/rows/compute.inc`'s `__chk_row_sicutcov`: a caller's `sicut`
items cover atoms that travel INSIDE a value -- substituted at an argument
that is one of the caller's `sicut` parameters or is of `structura` type,
or the row of a call through such a parameter -- and never a callee's
declared atom, which is a carrier the caller must hold. So a `Directorium`
forwards through any number of helpers, a child derived with `infra` can be
handed on, and §4.2's closure-capture violation is still refused
(`tests/unit/chk_row_sicut_forward.asm`, 19 rows now; spec §4.2 states the
rule).

**Finding 3 -- D1 said `d.a` is EXS-E0301; it is EXS-E0305** (corrected in
D1 and in interface.inc). No code was added: `codes.inc` and §13 are
untouched.

**Finding 4 -- R1 was false twice more, and the "reach that tree and nothing
else" sentence was wrong.** Review of stage 2 found one runnable escape, one
checker hole the lowering happened to mask, and two defects in the tests and
this document.

- *`sub` did not type its provider.* `sub archivum = d;` over a
  `Directorium` minted the raw atom (above, R1 rule 2). Built and run
  before the fix: `ad_radicem(archivum, "/tmp/exs-xp")` from a function
  holding only a child of `/tmp/exs-xp/root` opened a file outside it and
  the program exited 42. After: EXS-E0303 at the `sub`.
- *A lambda's draw was invisible to a call.* Checker-clean, then a SIGILL in
  the lowering. The SIGILL turned out to be the lowering's blanket refusal
  of every lambda (`lwr_expr`), not the atom arm -- a lambda with no
  capability in it traps there too -- so no escape was reachable at run
  time; but the checker was accepting a program its own rule says to
  refuse. Fixed in the checker (R1 rule 3); the lowering's atom arm and its
  staged-carrier site now answer EXS-E0421 instead of trapping, pinned by
  `tests/unit/lwr_unprovided.asm`, with the driver half `[UNTESTED]`
  because nothing reaches it.
- *The ineligible-argument exclusion in `__chk_row_sicutcov` was not
  pinned:* deleting it left both fixtures green. Row 19 of
  `chk_row_sicut_forward.asm` fails without it.
- *This document and the spec said the function's row "reads
  `{archivum}`" and that `sicut d` bounds it.* Neither is so; corrected in
  D6 above and in spec §4.6.

**D6's lowering question: no hidden carrier.** A function declared `poscit
sicut d` over a `Directorium` lowers to a signature with the `Directorium`
and nothing else -- `__lwr_sig_carriers` emits one `ptr` per ATOM item of a
row and none for a `sicut` ordinal -- and every prelude call is staged with
no carrier (the rows are empty). `tests/unit/lwr_directorium.asm` pins the
IR byte for byte: `salva(d: Directorium) poscit sicut d` is `(ptr) -> u8`;
its contrast, `radix(v: textus) poscit archivum`, is `(ptr ptr) -> u8`, the
carrier first. Making `__lwr_sig_carriers` emit a `ptr` for an ordinal fails
it (exit 20). Building that contrast found a lowering gap: an atom named as
a VALUE in a function whose row declares it (`ad_radicem(archivum, v)`)
fell to `__lwr_module_const` and trapped -- for `ambitus` as much as for
`archivum` -- and now reads the provider's carrier as a call does
(`lower/expr.inc`, `__lwr_path`).

**The carrier.** `m.archivum()` returns the Mundus record's own address:
an opaque, non-null token no routine reads. The atom's authority is to
derive a root and nothing else, and a root is the descriptor in the
`Directorium`; a token needs no storage, so `prelude_data.asm`, which every
`OUT` carries, is unchanged.

**Backends.** `backend_fasmg/program.inc` copies `archivum.asm` into the
executable segment after `prelude.asm`, and `archivum_rodata.asm` into
`segment readable`, ONLY when the program's mask holds `archivum` -- not
unconditionally behind the blobs' own gate, which would assemble to the
same binary but change every `OUT`'s text. The driver's mask
(`driver/run.inc`, `__drv_cap_of_ty`) counts the three new types as
`archivum`, so a module that mentions only a `Directorium` still carries the
routines its calls name. The stage-2 entry points add NO syscall and no
`openat2` site: the audit judges exactly stage 1's four. One stage-1 routine
changed: `exsrt_archivum_scribe` returns the -errno that stopped it even
after a partial write, never a short count, which `inscribe` needs to say
why. The C backend needed nothing -- it emits the `exsrt_` imports from the
IR -- and `tests/c/exsrt_shim.c` mirrors the eight entry points, hosted, with
the same `open_how` values (from `<fcntl.h>`), the same refusals and the
same `eventus` bytes, so the differential phase holds the C build to the
reference on them.

**End to end** (`tests/programs/`, each `potestates=Mundus,archivum
radix=yes`, each passing on the reference backend and on gcc and clang at
-O0 and -O2, each audited under exactly `{Mundus, archivum}`):
`archivum_radix/` derives a root, attenuates it, creates, writes and reads
back a file, and is refused EEXIST by a second `crea`; `archivum_refusa/`
is refused EINVAL for a relative root, ENOENT for a missing one, EXDEV for
`..`, `sub/../..`, an absolute path and `crea("../x")`, EINVAL for
`lege_ex` of a directory, and EXDEV for a child root climbing to its
parent, each beside a legal twin; `archivum_profundum/` forwards a root
through two helpers declaring only `sicut d`, writes beneath a derived
child, and reads the result back beneath the top root.

**How the root is made, and why it is a fixed path.** `tests/run.sh`'s
`radix=yes`: under an exclusive lock on `/tmp/exsecutor-radix.lock`,
`run_binary` removes `/tmp/exsecutor-radix`, re-creates it as a copy of the
program directory's own `radix/` tree, runs the binary, and removes it. The
program must name its root as a LITERAL -- `ad_radicem` refuses a relative
path, and `/proc/self/cwd` is a magic link `how_radix` refuses (section
9) -- and nothing in the language carries a path in from outside yet. So
the path is fixed and the tree is made fresh for every run, which is what
makes the runs deterministic and lets two suites share a machine. Whether
`/tmp` is writable inside `nix flake check`'s sandbox is `[UNTESTED]`.

**Still `[OPEN]` after stage 2:** D11 and `EXS-E0425` (waits on the
owner); `exlege_octeto` reads one byte per `read(2)` (a buffer needs state
shared between copies of a 16-byte value, which D7 already defers); §10.3's
`archivum (Directorium)` rendering (not implemented); `lseek(8)`, admitted
and issued by nothing; everything in section 8 not struck through.
