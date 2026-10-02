# 0019 — creation beneath a root is a kernel guarantee: a Landlock seal, then `mkdirat`

**Status:** Accepted, 2026-10-02, by the repository owner. The owner chose
"accept 0019 now", having been shown the five syscalls it adds to
`archivum`'s row and that it fails closed without Landlock. It supersedes
ADR 0018, whose Decision 4 a review measured to be false. Not yet
implemented: it lands after ADR 0017's stage 2. Each later syscall ADR in
the filesystem programme (`docs/design/archivum-plenum.md`) is reviewed by
Fable and accepted by the owner separately; this acceptance does not cover
them.
**Relates to:** ADR 0017 (D7, F13); ADR 0018 (superseded);
`docs/design/explicator.md`; spec §4.6; `tools/syscall-audit.sh`;
CLAUDE.md, "The compiler is freestanding".

## Context

ADR 0018 admitted `mkdirat(258)` under a run-time lexical check in one
prelude routine. It accepted the moved-parent window on the grounds that
"closing it needs a kernel primitive that does not exist". A Fable review
(2026-10-02) showed that the primitive exists. Re-measured here, Linux
6.18.44, uid 0, 0 unexpected. A process that restricts itself with Landlock
(`handled_access_fs = MAKE_DIR`, one rule granting `MAKE_DIR` beneath the
root's descriptor) gets `EACCES` for:

- `mkdirat(sub, "../../outside/esc1")`: a multi-component `..`;
- `mkdirat(root, "up/esc2")`: through an escaping symlink;
- `mkdirat(held sub, "z")` after another process moved `sub` outside the
  root: the moved-parent window.

It still creates legal directories beneath the root. Landlock judges the
target's actual parent chain at the moment of the call, not the spelling.
ADR 0017 F13 already measured that it composes with the `openat2` walk.

ADR 0018 had therefore reintroduced what ADR 0017 retired: containment by a
check in program code rather than by a refusal in the kernel. The same
review found the design's other blocker. With no surface `close` (ADR 0017
D7), a handle returned per created file or directory leaks one descriptor
each. Measured: the 1020th member fails with `EMFILE` at the common soft
limit of 1024.

## Decision

1. **A `Directorium` can be sealed: `d.sigilla() -> eventus<u8, erratum>`**
   (provisional under §3.9: *sigillare*, to seal). It restricts the whole
   process, irreversibly, so that it can create entries (Landlock
   `MAKE_DIR` and `MAKE_REG`) **only beneath `d`'s inode**.
   - It may be called once per process. A second call is refused before
     any syscall.
   - It makes four calls, in order:
     - `prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)`;
     - `landlock_create_ruleset(&attr, 8, 0)`;
     - `landlock_add_rule(rs, LANDLOCK_RULE_PATH_BENEATH, &pb, 0)`, with
       `pb.parent_fd = d`'s descriptor;
     - `landlock_restrict_self(rs, 0)`.

     Then it closes `rs`.
   - Any failure is an `adversum`, and the program has created nothing. That
     includes `ENOSYS` (Landlock is Linux 5.13+), `EOPNOTSUPP` (Landlock
     disabled at boot), and an ABI older than 1. Never fall back (ADR 0017
     D9's rule).
   - The ruleset **handles** every Landlock ABI-1 create and remove right:
     `REMOVE_DIR`, `REMOVE_FILE`, and `MAKE_CHAR`, `_DIR`, `_REG`,
     `_SOCK`, `_FIFO`, `_BLOCK` and `_SYM` (`0x1ff0`). The one rule
     **grants** only `MAKE_DIR | MAKE_REG` (`0x180`) beneath `d`. Read and
     write rights are not handled, so they stay as they were. Sealing removes
     authority and adds none. Measured (`prototypes/beneath/sigillum.c`,
     Linux 6.18.44, uid 0, 0 unexpected). Once sealed:
     - creating a file or directory beneath `d` works;
     - creating one outside is `EACCES`;
     - a FIFO or a symlink *beneath* `d` is `EACCES`;
     - unlinking a file or a directory beneath `d` is `EACCES`;
     - hard-linking an outside file into `d` is `EXDEV` (ABI 1 denies
       cross-directory reparenting);
     - renaming one in is `EACCES`;
     - a same-directory hard link beneath `d` is allowed (the new name is
       `MAKE_REG`, which is granted). `archivum`'s row has no `linkat`.

2. **`archivum`'s creates require the seal.**
   - `d.conde(via)` (directories, `mkdirat`) refuses before any syscall
     unless this process has sealed.
   - `d.crea(via)` keeps working unsealed, because `openat2` B|M|X already
     bounds it, and gains the kernel's second refusal once sealed.

   `conde` keeps ADR 0018's lexical single-component check as defence in
   depth. The guarantee is the seal.

3. **Creation returns no descriptor.**
   - `d.conde(via) -> eventus<u8, erratum>`. It carries 0 until the spec
     has a unit type, which is `[OPEN]`. It makes the directory and holds
     nothing.
   - `d.crea_ex(via, l: Lector, n: u64) -> eventus<u64, erratum>` creates a
     file with `how_crea`, copies exactly `n` bytes from the reader into it,
     and closes it.
     - A short read is an `adversum`.
     - A short write (`ENOSPC`, `EFBIG`) is an `adversum`, and the
       partial file stays for the host to remove.
     - No copyable handle exists, so ADR 0017 D7's descriptor-reuse
       argument is untouched.
     - Nothing leaks: per member, one descriptor is opened and closed.

   `d.crea` returning a writer stays for programs that create a bounded
   number of files.

4. **`archivum`'s row gains exactly these, each with an audit rule:**

   | syscall | rule |
   |---|---|
   | `mkdirat(258)` | K1: `rdx` the immediate `0o700`. K2: `rdi` not an immediate. K4: `archivum` named. |
   | `prctl(157)` | L1: `rdi` the immediate 38 (`PR_SET_NO_NEW_PRIVS`), `rsi` the immediate 1. Any other `prctl` fails. |
   | `landlock_create_ruleset(444)` | L2: `rdi` a rip-relative `lea` onto 8 bytes in a non-writable segment, equal to the handled set `0x1ff0`. `rsi` the immediate 8. `rdx` the immediate 0. |
   | `landlock_add_rule(445)` | L3: `rsi` the immediate 1 (`PATH_BENEATH`), `r10` the immediate 0. The rule struct is built at run time (its `parent_fd` is run-time data, like `rdi` under A6). |
   | `landlock_restrict_self(446)` | L4: `rsi` the immediate 0. |

   - ADR 0018's K3 (pinning the routine's bytes) is dropped. The review
     found it unimplementable in the audit's symbol-less sweep, and it is no
     longer load-bearing: the kernel enforces what K3 was meant to protect.
   - What the audit still cannot prove is that `parent_fd` is a
     `Directorium`'s descriptor. That is A6's argument: any descriptor the
     program holds was opened by an audited `openat2`, and the
     `allowed_access` of the rule can grant only beneath it. The handled
     set, the one constant that makes the seal mean anything, is pinned (L2).

5. **One seal is the deployment shape.** Landlock layers can narrow and never
   widen. A program that needs to create beneath two roots seals the nearest
   common root, or does not seal and so cannot `conde`. Multi-root rulesets
   are `[OPEN]`.

## Consequences

- **Every directory or file a sealed program creates is, by the kernel's
  own refusal, beneath the one root it sealed.** That holds whatever the
  program's lexical checks do, and through a rename race.
- **A sealed program cannot delete anything, or create a device, FIFO,
  socket node or symlink, anywhere.** The kernel refuses it, not merely the
  syscall row. The audit proves that the handled set is exactly `0x1ff0`
  and that no `prctl` other than no-new-privs is made.
- **Linux 5.13 or later, with Landlock enabled, or no `conde`.** Oligarchy's
  plugind already requires Landlock (`oligarchy-plugins` tier 1), so its
  hosts have it. Elsewhere the program fails closed.
- `PR_SET_NO_NEW_PRIVS` also means a sealed process can never gain
  privilege through `execve`. The `archivum` row contains no `execve`
  anyway.
- **Modes.** Directories are `0700` and files `0600`. A setgid parent adds
  `S_ISGID` (measured by the review: `02700`). A umask and default ACLs can
  only narrow.
- **Five syscalls join a per-atom row** that, before ADR 0017, admitted
  nothing. Each is admitted only under `archivum`, and only in the shapes
  L1–L4 and K1–K2. The compiler's own nine are unchanged.

## Proposed spec and table text (to apply on acceptance)

- **§4.6**, after ADR 0017's paragraph: "A `Directorium` can be sealed
  (`d.sigilla()`). The process then creates files and directories only
  beneath it, refused by the kernel (Landlock) rather than by the program.
  Directory creation (`d.conde`) requires the seal."
- Also: §4.6's list of what `--potestates` admits for `archivum`,
  `docs/design/runtime.md` section 2.6's row, `compiler/x86_64/prelude/README.md`'s
  table, `tools/syscall-audit.sh`'s `POTESTATES_TABLE`, and the header of
  `compiler/x86_64/prelude/archivum.asm`.

## Open

- A unit type, for `conde`'s and `sigilla`'s result.
- Multi-root seals. Handling Landlock ABI ≥ 2 rights such as `REFER` and
  `TRUNCATE` (a later ADR may widen the handled set; widening the handled
  set narrows the program).
- A non-root caller, a kernel with Landlock disabled, and a pre-5.13 kernel:
  not measured.
