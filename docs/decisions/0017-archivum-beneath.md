# 0017 — `archivum` reaches the filesystem only beneath a `Directorium`, through `openat2`

**Status:** Accepted, 2026-10-02, by the repository owner; stage 1
implemented 2026-10-02; stage 2 (the surface API) implemented 2026-10-02,
without D11's `EXS-E0425`, which is not registered. As built, `d.a` and
`d.descriptor` are `EXS-E0305`, not the `EXS-E0301` written below, and the
reader and writer are `Lectorium` and `Scriptorium`
(`docs/design/archivum-beneath.md` section 10).
**Relates to:** spec §4.1, §4.2, §4.3, §4.6, §4.7, §9.5, §10.3, §13;
`docs/design/runtime.md` section 2.6; `docs/design/checker.md` sections 2.1
and 2.8; `docs/design/sum-types.md` D5; `docs/asm-conventions.md` section 6.

## Context

`archivum` is declared in §4.6 and cannot be derived (§4.7, `[OPEN]`). Its
syscall row exists only on paper. `docs/design/runtime.md` section 2.6 gives
it `openat(257)`, `close(3)`, `fstat(5)`, `lseek(8)`, `read(0)` and
`write(1)`. `tools/syscall-audit.sh` admits nothing for it, because the
prelude has no `EXS_POTESTAS_ARCHIVUM` routine, and says so whenever the
atom is named.

Taken as written, that row hands a program `openat`, which resolves any path
from any directory. A capability system whose filesystem atom is `openat` is
ambient authority with an audit trail. Holding `archivum` would mean being
able to name every file the process can reach. Exsecutor's consumer
Oligarchy has just found three bugs of exactly this kind in its Rust: two
in reliquary (a `block_id`, and a filename read from a sums file) and one in
plugind (an id that reached root's `remove_dir_all`). Each sat one lexical
check away from being fixed, at a call site that lacked the check.

Linux 5.6 added `openat2(2)` (syscall 437). Its `RESOLVE_BENEATH` makes the
kernel's path walk refuse any name that leaves the starting directory. The
measurement on this tree's machine (Linux 6.18.44, `prototypes/beneath/`)
found:

- every `..`, absolute path, escaping symlink and magic link is `EXDEV`;
- no rename race escapes, in 43,996,160 opens. The same race without the
  flag escaped 114 times in 44,647,424;
- a mount crossing and a hard link are **not** refused by
  `RESOLVE_BENEATH`. `RESOLVE_NO_XDEV` refuses the first. Nothing in
  `openat2` refuses the second;
- a seccomp filter cannot require the flag, because it lives in a struct in
  memory.

## Decision

1. **`archivum`'s only open is `openat2`, and its only unscoped open is the
   derivation of a root.** The per-atom table (asm-conventions §6's
   *second* closed set, for compiled programs) gives `archivum`
   `openat2(437)`, `close(3)`, `fstat(5)`, `lseek(8)`, `read(0)` and
   `write(1)`, and **not** `openat(257)`. The compiler's *own* closed
   allowlist (CLAUDE.md: `read write close fstat lseek mmap munmap openat
   exit_group`) is a different set for a different binary and **does not
   change**. `exsc` keeps `openat` to read source files.

2. **A root is a value: `Directorium`.** It is a prelude
   `structura Directorium { a: archivum, descriptor: i32 }`, mark
   `{archivum}` (§4.3), whose fields have no rows. It is obtained by
   `Directorium.ad_radicem(a: archivum, via: textus) -> eventus<Directorium, erratum>`,
   which takes the atom as an explicit value. The call fails at run time
   when `via` is not absolute, is not a directory, or cannot be opened, and
   when `openat2` is unavailable. `m.archivum()` stays total, as §4.7
   already decides.

3. **Attenuation goes only downward.** `d.infra(via) -> eventus<Directorium, erratum>`
   roots a new `Directorium` beneath `d`. No operation widens one, exposes
   its descriptor or path, or recovers `archivum` from it.

4. **Every open beneath a root passes one of three fixed `open_how`
   constants, each with `resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS |
   RESOLVE_NO_XDEV` (0x0b).** The three are `how_infra` (a directory),
   `how_lege` (an existing regular file, `O_NONBLOCK`, `fstat`-checked)
   and `how_crea` (a new file, `O_CREAT|O_EXCL`, mode `0600`). The one
   unscoped constant is `how_radix`: `RESOLVE_NO_MAGICLINKS`, `AT_FDCWD`,
   absolute path only. `RESOLVE_NO_SYMLINKS` and `RESOLVE_IN_ROOT` are
   rejected. The design's D5 gives the reasons, against the measurement.

5. **The audit proves it, and fails closed.** At every `openat2` site,
   `tools/syscall-audit.sh --potestates` resolves `rdx` (a rip-relative
   `lea`) onto 24 bytes in a non-writable `LOAD` segment and matches them
   byte for byte against the admitted constants. It also requires `r10` to
   be the immediate 24, `rdi` not to be an immediate on a scoped constant,
   and `rdi = AT_FDCWD` on `how_radix`. Anything it cannot resolve is a
   FAIL, as an unresolved `rax` already is. The rules are A1–A6 in the
   design's section 3.

6. **No fallback, ever.** `ENOSYS`, `EPERM` or any other failure of
   `openat2` fails the operation. A program on a kernel older than 5.6, or in
   a sandbox that refuses the syscall, has no filesystem. It does not have an
   unscoped one.

7. **`EAGAIN` is retried a bounded number of times (16), then reported.**

8. **No surface `close` on a copyable handle.** Descriptor reuse would let a
   stale copy name another root. Release waits for deterministic
   destruction of a refcounted handle (§6.6).

### Proposed spec text

Recorded here so the amendment can be reviewed as text. It is to be applied
to the spec only when this ADR is Accepted, with the reason in the commit
(CLAUDE.md, "Scope").

**§4.6, a new paragraph after "Standard input, output and error belong to
`ambitus`":**

> **`archivum` reaches the filesystem only beneath a root.** Holding the
> atom does not let a program name a file. It lets the program derive a
> `Directorium`: `Directorium.ad_radicem(a: archivum, via: textus) ->
> eventus<Directorium, erratum>`, for an absolute `via`. That is a
> capability-bearing `structura` with mark `{archivum}`, whose fields have
> no rows. Every operation on a `Directorium` resolves its path beneath it
> with `openat2(2)` and `RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS |
> RESOLVE_NO_XDEV`. So `..` past the root, an absolute path, a symlink out
> of it, a magic link and a mount crossing are refused by the kernel and not
> by the program. `d.infra(via)` derives a `Directorium` beneath `d`, and
> nothing derives one above it. A function that receives a `Directorium`
> can reach that tree and nothing else, and the *value* is what bounds it,
> not a row: the prelude's rows on a `Directorium` are empty, so a function
> that merely uses one needs no `poscit`, and `poscit sicut d` is needed only
> to forward the handle to a `sicut`-declared callee. The function cannot
> obtain the raw atom: a `sicut` item binds no carrier, and `sub P = e`
> requires `e` to have P's own capability type (EXS-E0303). What
> `RESOLVE_BENEATH` bounds is *names*: a hard
> link beneath the root to a file outside it is reachable (measured,
> `prototypes/beneath/`). `docs/design/archivum-beneath.md` is the design,
> and its section 2 is the measurement.

**§4.7, replacing the sentence "`[OPEN]` Only `m.ambitus()` has a prelude
row today (…); `archivum` and the other atoms are declared (§4.6) and not yet
derivable." with:**

> `m.ambitus()` has a prelude row (`compiler/x86_64/prelude/interface.inc`,
> `checker/types/prim.inc`'s `.fn_ambitus`). `m.archivum()` is total, like
> `ambitus`. Naming a root is the separate, run-time-fallible
> `Directorium.ad_radicem` (§4.6). Its failure, including a kernel without
> `openat2(2)` (Linux < 5.6) or a sandbox that refuses it, leaves the
> program with no filesystem, never with an unscoped one. The other atoms
> are declared (§4.6) and not yet derivable.

**§10.3, a sentence after the `alloc` bullet (optional, `[OPEN]` in the
design):**

> A row of `archivum` reached only through `sicut` over a `Directorium`
> renders as `archivum (Directorium)`. A bare `poscit archivum`, which can
> derive any root, renders as `archivum`.

**§13, one new code: a proposal, not a registration.** This ADR does not add
it to §13 or to `codes.inc` (CLAUDE.md, "Error codes are permanent"). It is
needed only for D11's compile-time check, which soundness does not depend
on, because the kernel refuses the same paths at run time.

> | `EXS-E0425` | path literal names nothing beneath its `Directorium` (absolute, or a `..` above its start) |

`EXS-E0425` is unused in the tree today (the design records the grep).
Everything else needs no new code. `d.a` and `d.descriptor` are
`EXS-E0301` (as built: `EXS-E0305`). Writing `archivum` in a function whose
row is only `sicut d` is `EXS-E0421`, and `sub archivum = d;` is
`EXS-E0303` (added after review: the proposal above did not type `sub`'s
provider, and a `Directorium` could mint the raw atom through it).

**`docs/design/runtime.md` section 2.6 and
`compiler/x86_64/prelude/README.md`'s per-atom tables**, when the prelude
routine lands, have one row for `archivum`: `openat2(437)`, `close(3)`,
`fstat(5)`, `lseek(8)`, `read(0)`, `write(1)`. `tools/syscall-audit.sh`'s
`POTESTATES_TABLE['archivum']` takes the same set, its
`POTESTATES_DISAGREEMENT` entry for `archivum` is deleted, and its
`write(1)`/`read(0)` admission tests `ambitus` *or* `archivum`.

## Consequences

**Positive.**

- **Traversal is unrepresentable.** It is not a check the program can
  forget. There is no expression that names a file outside the roots a
  program derived, and the binary is audited to contain no open that could.
- A root is an inode fixed at derivation, so renaming its spelling later
  moves nothing. That removes the check-then-use window between
  canonicalising a path and opening it.
- The design composes with Landlock (measured): the walk refuses first, and
  Landlock refuses what the walk admits. An Oligarchy plugin written in
  Exsecutor would sit under both.

**Negative.**

- **Linux ≥ 5.6 or no filesystem.** Any host or sandbox without `openat2`
  gets a program that cannot open a file. That is deliberate, and it will
  surprise someone.
- **A tree with a mount inside it needs one `Directorium` per filesystem**
  (`RESOLVE_NO_XDEV`). The authority grows, visibly, at `initium`.
- **No `mkdir`, `unlink`, `rename` or directory listing in v1.** None of
  `mkdirat`, `unlinkat`, `renameat2` or `getdents64` takes `RESOLVE_*`. The
  single-component pattern that would admit them is recorded and
  `[UNTESTED]`.
- **Names, not inodes.** A hard link beneath the root reaches a file outside
  it on the same filesystem. Reads follow it, and `how_crea`'s `O_EXCL`
  keeps writes off it.
- **It solves two of Oligarchy's three bug classes, not the third.**
  plugind's `forbiddenPaths` bug compares two path *policies* for a third
  party. A `Directorium` bounds only the holder's own opens.
- **It waits on `eventus` being inhabited** (`docs/design/sum-types.md` D5)
  and on §4.2's recorded `sicut`-to-`sicut` checker defect. Before either is
  fixed, a `Directorium` cannot be derived, or cannot be forwarded past one
  call.

## Open

- ~~The checker properties R1–R3 the design depends on, as tests. Whether
  the lowering passes a hidden full-atom carrier beside a `Directorium`~~ --
  closed by stage 2, which also found R1 false twice (`sub`'s provider was
  untyped; a lambda's draw was invisible to a call). What stays `[OPEN]`: the
  closure check follows a lambda written in place or held in an immutable
  local, not one that reaches a parameter with an empty row, a field, a
  return, or a `mutabilis` local, none of which reaches an object while the
  lowering refuses lambdas (design section 10, finding 4).
- The reader and writer types' spelling (D4). `Directorium`, `ad_radicem`,
  `infra`, `lege_ex` and `crea` are provisional under §3.9.
- Recording a literal root in the ego as `archivum[/path]` (§10.1).
- Device opens with side effects, hard-link policy in adversary-writable
  roots, and rewrite/append (design section 8).
- Unprivileged callers, a real pre-5.6 kernel, and Oligarchy's own kernels:
  none measured.
