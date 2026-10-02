# examples/potestas/

**The install-time policy facts of a plugin host.** `potestas.exsc` decides
the parts of Oligarchy's plugin policy that are pure functions of bytes:
whether a plugin id is well formed, whether a capability path reaches a
forbidden prefix (lexically), whether a capability path is anchored, and
whether a plugin's unit gets W^X. It is pure: no `poscit`, no capability
parameter (spec §4.1 rule 6), no allocation, and no I/O.

## Why

Its consumer is plugind, Oligarchy's plugin host
(`modules/oligarchy-plugins/host`). Root acts on every answer here:

- an id becomes a systemd instance name and a path component;
- a capability path becomes a Landlock subtree grant;
- the W^X answer becomes `MemoryDenyWriteExecute=` in a drop-in that root
  writes.

Two of these rules were wrong in Oligarchy before this unit existed:

- ids from the control socket reached root unvalidated;
- a forbidden prefix was checked in one direction only. A capability on `/`
  was not "under" `/proc`, so `fs_read_write = ["/"]` granted `/proc`. That
  is a complete W^X bypass through `/proc/self/mem`.

Both were fixed in Rust first. This unit states the fixed rules a second
time, in a language whose purity the compiler checks. The W^X rule is
already stated twice in Oligarchy: once in Rust (`Manifest::wx_enforced`)
and once in Nix (`wxEnforced` in `modules/plugins.nix`), and a comment asks
that the two be kept in step by hand. This is a third statement. It is the
one that both of the others can be checked against.

It is a first step toward the policy compiler in Oligarchy's
`docs/exsecutor-kernel-roadmap.md`: one declaration lowered to Landlock,
seccomp and a systemd drop-in. This unit decides; it emits nothing yet.

## The rules

Each public function is the exact rule of one Rust function.

| function | Rust | answer |
|---|---|---|
| `titulus_iudica(b, n)` | `manifest::check_id` | 0 admitted; 1 empty; 2 over 64 bytes; 3 a byte outside `[A-Za-z0-9_-]`. The length is judged first, before any byte is read. |
| `habet_regressum(b, n)` | the `..` test inside `policy::is_under` | 1 if any `/`-separated piece is exactly `..` |
| `subest(p, f)` | `policy::is_under` | 1 or 0. See below. |
| `tangit(p, f)` | the lexical half of `policy::overlaps` | 0 disjoint; 1 `p` is under `f`; 2 `p` contains `f`; 3 a side longer than 4096 bytes |
| `ancora(p, n)` | the anchor check in `Manifest::validate` | 1 if non-empty and absolute, or starting with `$STATE`, `$CONFIG` or `$STORE`; else 0 |
| `involucrum(t)` | `Manifest::uses_bwrap` | 1, 0, or 2 for an unknown code |
| `wx_cogitur(t, j)` | `Manifest::wx_enforced` | 1, 0, or 2 for an unknown code |
| `wx_conceditur(t, j)` | `Manifest::grants_wx_to_plugin` | 1, 0, or 2 for an unknown code |

Tier codes are 0 wasm, 1 native, 2 lua and 3 microvm. Jit codes are 0 none,
1 host and 2 self.

`subest` follows Rust's `Path::components`:

- Any `..` piece in `p` answers 1. A path containing `..` cannot be judged
  without the filesystem, so it matches every prefix and is always refused.
- A `p` that is relative and not `$`-anchored answers 1. That includes the
  empty path. A relative path would be resolved against the unit's working
  directory, `/`.
- Otherwise `f`'s components must be a prefix of `p`'s components. Empty and
  `.` components are dropped. A root matches only a root.
- A `.` that starts a relative `f` matches nothing, and neither does a `..`
  in `f`. An empty `f` has no components, so every `p` is under it.

`tangit` tries both directions. A Landlock grant covers a whole subtree, so a
capability on an ancestor of a forbidden path grants the forbidden path. Only
an absolute `p` can contain anything.

## What stays in Rust

Symlink resolution. `policy::overlaps` retries both directions with each
side canonicalised: `/var/run` is `/run` on NixOS, and sops-nix keeps its
secrets behind a `/run/secrets` symlink. That needs the filesystem, so it
stays in Rust. It runs after the lexical half and can only add refusals.

Two other things stay outside this unit:

- **Paths longer than 4096 bytes.** `tangit` answers 3 (refuse) for them,
  where Rust's lexical half has no limit. Linux's `PATH_MAX` is 4096, so
  Landlock could not open such a path anyway.
- **Bytes that are not UTF-8.** `titulus_iudica` refuses them. Rust's
  `check_id` cannot be given them, because it takes a `&str`.

## Checks: `proba_c.sh` (after `make all`)

1. Emits the unit twice and requires byte-identical output (60,048 bytes).
2. Builds `proba.c` against the unit with gcc and clang, at `-O0` and `-O2`,
   under UBSan, with `potestas.h` force-included. Then it runs `proba.c`.
   That file holds 122 anchors:
   - the cases from plugind's own Rust tests;
   - the edges of the rules those tests state: empty prefixes, a leading `.`
     in a prefix, trailing slashes, `...` and `.b` names, and the 4096-byte
     edge;
   - all 12 tier × jit rows.
3. Requires each of 16 behaviour mutants of `potestas.exsc` to fail
   `proba.c`. The mutants cover `..` unchecked, relative admitted, the
   "contains" direction dropped, a byte prefix in place of components, `.`
   and empty components kept, a prefix's leading `.` matched, an empty prefix
   matching nothing, the `$` unchecked, absolute paths unanchored, `.` and
   `/` admitted in ids, an unbounded id length, W^X on a wasm unit, jit=self
   still enforced, microvm read as native, and the concession granted off
   the bubblewrap tiers.
4. Checks the capability claim:
   - an ambient draw of `ambitus` or `archivum` is refused with `EXS-E0421`;
   - a `Scriptor` parameter breaks `potestas.h`.

One check in `subest` has no mutant: it refuses a `..` in `f`. By the time
that check runs, `p` holds no `..` piece, so the byte comparison would refuse
the pair anyway. The check is kept so the walk reads like Rust's match arms.

The full certification belongs to the consumer, as it does for `custos/`.
Oligarchy's `modules/oligarchy-plugins/potestas-cert` links this unit. It
compares the unit with plugind's own `check_id`, `Policy::authorize`,
`Manifest::validate` and `wx_enforced`, and with the Nix mirror, on a
generated corpus. The figures are in the commit that added it there. This
directory holds no Rust and no Oligarchy source.
