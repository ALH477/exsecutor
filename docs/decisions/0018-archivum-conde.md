# 0018 — `archivum` creates a directory with `mkdirat`, one checked component beneath an `openat2` parent

**Status:** Accepted, 2026-10-02, by the repository owner, as step 3 of the
`explicator` plan (`docs/design/explicator.md`). Not yet implemented: it
lands after ADR 0017's stage 2, whose prelude and audit it extends. The audit
rules K1–K4 below are this ADR's own and are the part to review.
**Relates to:** ADR 0017; `docs/design/archivum-beneath.md` section 5
("Not in v1", the single-component pattern recorded there as `[UNTESTED]`);
spec §4.6; `tools/syscall-audit.sh`; CLAUDE.md, "The compiler is
freestanding".

## Context

ADR 0017 gives `archivum` one way to open: `openat2` with
`RESOLVE_BENEATH|RESOLVE_NO_MAGICLINKS|RESOLVE_NO_XDEV`. It can open
directories and create regular files beneath a `Directorium`. It cannot
create a directory, because `openat2` cannot: `O_CREAT|O_DIRECTORY` creates
no directory. The flagship program, an archive extractor for reliquary
(`docs/design/explicator.md`), cannot exist without one.

`mkdirat(258)` takes no `RESOLVE_*` flags. It walks its path from the dirfd
with the ordinary resolution. Measured on Linux 6.18.44, as uid 0
(`prototypes/beneath/mkdirat.c`, 0 unexpected):

- **A multi-component name escapes.** `mkdirat(sub, "../../outside/esc1")`
  and `mkdirat(root, "up/esc2")`, where `up` is a symlink out, both create
  their directory outside the root.
- **A single component does not.** If the final component is a symlink,
  escaping or dangling, the result is `EEXIST`: the link is not followed and
  its target is not created. `.` and `..` are `EEXIST`. The empty name is
  `ENOENT`.
- **The mode is honoured.** `0700` under umask `022` gives `0700`. A umask
  only clears bits, so it cannot widen a constant.
- **A held parent follows its inode.** If the parent directory is moved
  outside the root between being opened and the `mkdirat`, the new
  directory is created in it, outside. That is the property ADR 0017 D2
  already states for every root ("the root is an inode, fixed at
  derivation"). Here it is reached through an intermediate directory too.

## Decision

1. **`archivum`'s row gains `mkdirat(258)`, and nothing else.** No
   `unlinkat`, `renameat2`, `symlinkat`, `linkat`, `mknodat`, `fchmodat` or
   `getdents64`. The compiler's own nine syscalls are unchanged; this is the
   per-atom table for compiled programs (asm-conventions section 6's second
   closed set).

2. **One prelude routine issues it**, the stage-2 surface
   `d.conde(via) -> eventus<Directorium, erratum>` (provisional under §3.9:
   *condere*, to found or build). It:
   - splits `via` at its last `/`;
   - opens the parent beneath `d` with `how_infra`, or uses `d` itself when
     there is no `/`;
   - checks the last component lexically: non-empty, no `/`, no NUL, not
     `.` or `..`, at most 255 bytes;
   - calls `mkdirat(parent, component, 0700)`;
   - opens the result beneath `d` with `how_infra` and returns it as a
     `Directorium`;
   - closes the parent.

   Every refusal comes before any syscall that could create anything. A
   trailing `/` on `via` is stripped once; `a//` is refused.

3. **The audit proves what it can see, and the routine carries the rest.**
   The rules for an `mkdirat(258)` site, all checked like ADR 0017's A1–A6,
   and failing closed:
   - **K1:** `rdx` is the immediate `0o700`, so the mode is read from the
     binary.
   - **K2:** `rdi` is not an immediate. An immediate dirfd is `AT_FDCWD` or
     a guessed number, never a parent opened beneath a root (A6's
     argument).
   - **K3:** the bytes from the routine's entry to the `syscall` equal the
     reference routine's bytes, which the audit carries. The routine is
     written with no rip-relative operand before the site, so its bytes do
     not depend on where it is placed. `make` asserts that the reference in
     the audit equals the assembled prelude, so the two cannot drift. A
     binary whose `mkdirat` is not the prelude's checked routine fails.
   - **K4:** `mkdirat` is admitted only when `archivum` is named.

   **What the audit cannot prove:** that `rsi` points to one checked
   component. The name is run-time data. That property belongs to the
   routine and is proven by `tests/unit/prelude_archivum.asm`'s rows and by
   mutation: delete the check, and the escaping rows must fail. A program
   that jumps into the routine past its check needs hand-written machine
   code. Safe Exsecutor cannot express that; it is the same trust every
   prelude routine already carries. K3 is what stops a compiler or backend
   from emitting a second, unchecked site.

4. **The moved-parent window is accepted and recorded.** Closing it needs a
   kernel primitive that does not exist (an `mkdirat` that takes
   `RESOLVE_*`). It requires an adversary with write access to the tree
   *during* the call. Reliquary's destination is created fresh by root, and
   that is the deployment the extractor targets. It is not a property to
   rely on where an untrusted writer shares the tree.

## Consequences

- An Exsecutor program can build a directory tree beneath a root it was
  given, and the binary is audited to contain exactly one routine that can
  do so.
- Every new directory is `0700`. Callers who want other modes need
  `fchmodat`, which is deliberately not admitted.
- `unlinkat` and `renameat2` would follow the same pattern (a single
  component beneath a parent opened with `openat2`) when they are needed.
  Each is its own ADR with its own measurement. This one does not admit
  them.

## Open

- The surface name (`conde`) is provisional under §3.9.
- A non-root caller and a pre-5.6 kernel: not measured (ADR 0017's
  caveats carry over).
- K3's reference bytes are per backend. The C backend's `exsrt_` import
  for `conde` is implemented by its host in C, which `syscall-audit.sh`
  does not judge (it audits reference-backend binaries).
