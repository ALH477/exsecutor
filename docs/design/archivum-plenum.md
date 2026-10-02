# The complete filesystem, and its flow into DCF

**Status:** programme plan, 2026-10-02. The repository owner asked for "a
fully fledged filesystem" that is first class with Oligarchy's reliquary and
Punctim's StreamDB, "with a perfect flow into networking via Punctim". Asked
to choose, the owner decided:

1. **Both, layer first.** First the complete filesystem *layer* of the
   language (`archivum`). Then a mountable filesystem written in Exsecutor,
   designed on top of it.
2. **DCF-scoped `rete`.** Networking is narrowed the way `archivum` was:
   - UDP to declared private-range peers only;
   - every datagram passes the `custos` gate as a valid `DeModFrame` or
     SuperPack;
   - bulk data rides DCF-Pipe.

   It is plaintext by design, and deployed inside WireGuard (Punctim's
   export posture).
3. **ADR 0019 accepted now.** Every further syscall ADR is measured, reviewed
   by Fable, and accepted by the owner one at a time.

This document is the coordination point. Each row names its ADR or design
document, and that document carries the decisions; this one carries only the
order. How the work is done: Sonnet agents implement, each in its own
worktree. A Fable agent reviews each branch adversarially (it runs the
tests and mutates the code to see whether the tests notice). Claude Code
makes the final review, merges, and runs the Nix sandbox check before
anything is pushed.

## The shape

```text truth:ignore
            Mundus
   ┌──────────┼───────────────┐
 ambitus   archivum          rete  (DCF-scoped: UDP, private peers, custos-gated)
 argv,     Directorium ──seal──┐            │
 std*      ├ files  lege/crea_ex/adde/rescribe    DCF frame codec ── DCF-Pipe
           ├ dirs   conde/enumera/dele/renomina         │
           ├ meta   status (fstat on O_PATH)            │
           └ fsync                                      │
                │                                       │
           StreamDB v3  (reader ✓, writer) ─────────────┘  objects stream
                │                                           to Punctim peers
       reliquary: explicator → packer
                │
          exsfs (mountable, over StreamDB) — phase M, later
```

## Phase F: the filesystem layer

| id | what | home | depends on | state |
|---|---|---|---|---|
| F0 | ADR 0017 stage 2: `Directorium`, `ad_radicem`, `infra`, `lege_ex`, `crea`; `sicut` forwarding | branch `wt/directorium` | — | in progress |
| F0 | `?` | `wt/interrogatio` | — | in progress |
| F0 | `argv` on `ambitus` | `wt/argumentum` | — | in progress |
| F0 | bytes → `textus`, validated | `wt/octeti-textus` | — | in progress |
| F1 | ADR 0019: `sigilla` (Landlock seal), `conde`, `crea_ex` | to come | F0 stage 2 | **accepted**, to build |
| F2 | stderr writer; decimal formatting; `lege` that tells EOF from error (`lege_octeto` conflates them) | to come | F0 stage 2 | to build |
| F3 | **ADR 0020**, enumeration: `d.enumera()` over `getdents64(217)`, on a new `open_how` constant (`O_RDONLY\|O_DIRECTORY`, B\|M\|X) | ADR draft + probe | — | to draft |
| F4 | **ADR 0021**, removal and rename: `unlinkat(263)` and `renameat2(316)`, a single component beneath parents opened with `openat2`. The seal grants `REMOVE_*` (and `REFER`, ABI 2, for cross-directory rename) beneath the root only. Atomic replace (`rescribe`) = `crea_ex` to a temporary name, then `renameat2`. | ADR draft + probe | F1 | to draft |
| F5 | **ADR 0022**, durability: `fsync(74)`/`fdatasync(75)` on `archivum` descriptors; `crea_ex` with an fsync option. Reliquary's own review records "no fsync after writes". | ADR draft + probe | F1 | to draft |
| F6 | metadata: `d.status(via)`, type/size/mode/mtime by `fstat(5)` on an `O_PATH` descriptor. `fstat` is already in the row, so **no new syscall** and no ADR. | — | F0 stage 2 | to build |
| F7 | append (`adde`): `O_APPEND`, B\|M\|X. One more `open_how` constant, and the hard-link hazard to measure. It may ride ADR 0021. | — | F1 | to design |
| F8 | **StreamDB v3 writer**, pure. Deterministic UUIDs (from content, §9.3; upstream mints them from `/dev/urandom`). Certified by reading back: Exsecutor's reader and the upstream C reader must both accept it, with the same documents, bytes and paths. | `examples/streamdb/` | — | to build |
| F9 | `explicator` (`docs/design/explicator.md`) | `examples/explicator/` | F1, F2, F0 all | after |
| F10 | reliquary: `explicator` replaces `tar -x` (Oligarchy) | Oligarchy | F9 | after |
| F11 | reliquary's packer in Exsecutor: a tar writer that emits exactly GNU tar's bytes for `pack_tree`'s flags, certified byte-for-byte, plus `enumera` | `examples/` | F3, F8 | after |

## Phase N: DCF-scoped networking

| id | what | depends on | state |
|---|---|---|---|
| N1 | **ADR 0023**, `rete` narrowed. Replace "the whole socket family" (`tools/syscall-audit.sh`'s current `rete` admission) with:<ul><li>`socket(AF_INET\|AF_INET6, SOCK_DGRAM)` only;</li><li>`bind`;</li><li>`sendto`/`recvfrom` only inside the prelude's DCF datagram routines, which run `custos` on every datagram both ways and refuse a peer outside the private ranges.</li></ul>What the audit can prove (constants and call shapes) and what it cannot (run-time peer addresses) are stated up front, as ADR 0017 did. | — | to draft |
| N2 | the DCF frame codec and SuperPack in Exsecutor, certified against `vendor/hydramesh-wire/golden_vectors.json` (246 vectors) | — | to check: `custos` gates, but no general codec is surfaced |
| N3 | DCF-Pipe in Exsecutor (OPEN/CREDIT/SACK/NACK/DONE/ABORT and the data lane), certified against Punctim's `pipe_vectors.json`, vendored the way `hydramesh-wire` is | N2 | to build |
| N4 | **the flow.** An Exsecutor program sends a StreamDB object, read beneath a `Directorium`, to a Punctim peer over DCF-Pipe on scoped `rete`. A Punctim node (Python or Rust `punctim`) receives it byte-exact. And the reverse. An interop test in both repositories. | F8, N1–N3 | after |

## Phase M: a mountable filesystem

| id | what | depends on |
|---|---|---|
| M1 | design: `exsfs`, a FUSE daemon in Exsecutor over StreamDB containers. It needs `/dev/fuse`, mount, and probably threads (`Filum`, undeclared today) | F, N4 |

## Rules every row inherits

- No syscall joins a row without an accepted ADR backed by a measurement in
  `prototypes/`.
- Containment is the kernel's (`openat2` B\|M\|X, Landlock), never only a
  check in program code. Where a check in code is unavoidable (a run-time
  peer address), the ADR says so in its first section.
- Every certification runs in both directions: what the reference admits,
  the Exsecutor side must admit or refuse for an enumerated reason. A
  refuse-everything implementation must fail its own test.
- No new error code without a §13 amendment first. No socket syscall in the
  compiler, ever (CLAUDE.md). `rete` is for compiled programs only.
