# `explicator` — reliquary's extractor as an audited Exsecutor process

**Status:** design, 2026-10-02, revised the same day after a Fable review;
section 7 lists what changed. The repository owner asked for it ("do it")
as the language's flagship: one real program, run as root on hostile
input, whose binary is proven unable to do what tar can be talked into.
Nothing here is built yet. Section 5 is the order it gets built in. It
depends on ADR 0019, which is Proposed.

*Explicare* is to unroll a scroll; `explicator` is the agent noun (§3.4's
`-tor` on the supine stem `explicat-`). The name is provisional under §3.9.

## 1. The claim

Reliquary (Oligarchy's cold-storage tool) runs as root. It extracts
payloads that may come off a USB stick, and a stick's label can be forged.
Today it judges the payload with `arca` (`examples/arca/`) and then runs
`tar -xf`. The judge is the security. GNU tar is a 100,000-line program
that can create devices, FIFOs, symlinks and hard links, change owners and
modes, and follow paths anywhere. Here it is trusted to do only what the
judge already decided.

`explicator` replaces `tar -x` with a program whose binary is audited
(`tools/syscall-audit.sh --potestates Mundus,ambitus,archivum`) to make no
syscall outside:

| syscall | why | bounded by |
|---|---|---|
| `read(0)`, `write(1)` | the tar stream on stdin, file contents out, diagnostics on stderr | `ambitus`, `archivum` |
| `openat2(437)` | every open, beneath the root | ADR 0017's A1–A6: the four `open_how` constants, B\|M\|X |
| `mkdirat(258)` | directories | ADR 0019's K1, K2 and K4: mode `0700`, a non-immediate dirfd |
| `prctl(157)`, `landlock_*(444–446)` | the seal, once, before the first create | ADR 0019's L1–L4: no-new-privs only; handled set `0x1ff0` pinned |
| `close(3)`, `fstat(5)` | the stage-1 routines; `crea_ex` closes each file | — |
| `exit_group(231)` | — | — |

The claim comes in three strengths. They are kept apart, because only the
first two are about bytes.

**Proven by the audit, from the shipped binary.** It contains no syscall
outside the table. So it cannot:
- open a socket;
- `execve`;
- `openat` (unscoped);
- `chmod` or `chown`;
- `mknod`;
- `symlink`;
- `link`;
- `unlink` or `rename`.

**Refused by the kernel, once sealed** (ADR 0019, measured in
`prototypes/beneath/sigillum.c`):
- every file or directory the process creates is beneath the sealed root,
  through any rename race;
- it can create no device, FIFO, socket node or symlink anywhere;
- it can delete nothing anywhere.

These would hold even if the program's own checks were wrong.

**A source property, `[UNTESTED]` until ADR 0017's R1–R3 are tests.**
- *One root, from `argv[1]`.* The audit sees `how_radix` with
  `rdi = AT_FDCWD`; it does not see `rsi`, or how many times it runs.
- *No environment.* `envp` is in the `ambitus` carrier (`ExsAmbitus
  @32`), and reading it needs no syscall. The surface has no accessor,
  which is a property of the language, not of the bytes.

"Cannot open outside the root" is a claim about naming. `write(1)` to any
descriptor is admitted, so the host must spawn the extractor with only
descriptors 0, 1 and 2 open. Rust's `Command` does that by default
(`O_CLOEXEC` everywhere).

## 2. Interface

```text truth:ignore
explicator DEST < payload.tar
```

- `DEST` must be absolute. It is `Directorium.ad_radicem`'s `via` (ADR
  0017 D2). Reliquary already refuses a non-empty destination
  (`Store::extract`), and keeps doing so.
- The tar stream comes in on stdin, already decompressed. zstd stays in the
  Rust host: decompressing a stream creates no file, and an Exsecutor zstd
  decoder is another project.

Exit status:

| status | meaning |
|---|---|
| 0 | every member extracted, and the stream after the end block is zeros |
| 2 | usage: not exactly one argument, or `DEST` is not valid UTF-8 |
| 3 | `DEST` could not be rooted or sealed (relative, missing, not a directory, no `openat2`, no Landlock); the errno goes to stderr |
| 4 | a create or a write failed, including `EEXIST` and `ENOSPC` (the errno goes to stderr) |
| 5 | a member name `arca` admitted is not valid UTF-8 (§5.1); nothing was created for it |
| 16–27 | `arca`'s verdict on the offending header, unchanged |
| 100 | the stream ended inside a member, or reading stdin failed. The prelude's `lege_octeto` does not tell EOF from error apart (`[OPEN]`), and the host must hand over a blocking pipe |
| 101 | data after the end block |

The sets are disjoint. A trap is `ud2`, so the process dies of `SIGILL`
with no status at all. The host treats death by a signal as failure, which
Rust's `!status.success()` already does. On any failure the host removes
`DEST`. A partial tree is not a restore, and `Store::extract`'s non-empty
guard would refuse a retry into it.

A huge member is a resource limit, not a containment question. A size field
near 2^63 fills the disk, and that ends with `ENOSPC`, exit 4. Disk quotas
are the host's business.

## 3. What it does

It is one compilation unit: `examples/arca/arca.exsc`, unmodified, then
`explicator.exsc`. It has the same shape as `filtrum.exsc` over
`custos.exsc`.

First: `d = Directorium.ad_radicem(m.archivum(), argv[1])`, then
`d.sigilla()` (ADR 0019). After the seal, the kernel refuses any create
outside `d`. Then, for each 512-byte block:

1. `caput_iudica(h, l, nl, habet)`. This is the same judge, so there is no
   second parser to disagree with.
2. Verdict 2 (long name): read `magnitudo(h)` bytes into `l`, skip to the
   block boundary, and set `habet = 1`.
3. Verdict 1 (directory): `d.conde(name)` with the trailing `/` removed.
4. Verdict 0 (file): `d.crea_ex(name, stdin, magnitudo(h))`. It creates the
   file, copies exactly that many bytes, checks every write's count, and
   closes the file. Then it skips `saltus(magnitudo(h)) - magnitudo(h)`
   padding bytes.
5. Verdict 3 (end): require zeros until end of input.
6. Any other verdict: exit with it.

**No descriptor outlives its member.** `conde` returns no handle, and
`crea_ex` closes its file before it returns. Per member the process opens
at most two descriptors (a parent and a file) and closes both. The first
design returned a handle per create; under ADR 0017 D7, which has no
surface `close`, that failed at the 1020th member with `EMFILE` at the
common soft limit of 1024 (measured). Every path is opened from the one
root by its full relative name with B|M|X, never through a descriptor held
across members.

### Deliberate differences from `tar -x`

The certification in section 4 compares only what is the same by
construction. These differences are deliberate:

- **Modes.** Files are `0600` and directories `0700`: they are constants,
  so the audit can read them. A setgid parent adds `S_ISGID` (`02700`,
  measured in the review), and a umask or a default ACL can only narrow.
  Reliquary's `tar --no-same-permissions` gives `0644`/`0755` under root's
  usual umask, so **every non-root reader of a restore loses access**.
  Section 5 step 7 has to decide this: the host widens after a successful
  extraction, or reliquary documents root-only restores. This design does
  not decide it silently.
- **`a//`.** `arca` admits a directory name ending in `//` and tar creates
  `a`. `conde` strips one `/` and refuses the empty component, exit 4.
  Measured by the review with GNU tar 1.35.
- **Times are not restored.** Reliquary's packer pins every mtime to 1970
  (`--mtime='UTC 1970-01-01'`), so tar's restore carries no information.
- **No overwrite.** An existing name is `EEXIST` (from `O_EXCL` and
  `mkdirat`), so the run fails. tar would replace it. Reliquary never writes
  duplicates, and the destination starts empty.
- **No implicit parents.** A member whose parent directory was not created
  by an earlier member fails with `ENOENT`. GNU tar creates missing parents.
  Reliquary's packer lists every directory before its contents.
- **Non-UTF-8 names are refused.** `textus` is UTF-8 (§5.1). `arca` admits
  any byte `>= 0x80`; `explicator` refuses more than tar would and never
  admits something tar would read differently, which is `arca`'s own rule.

## 4. How it is certified

`examples/explicator/proba.sh` and `proba.py`, in the shape of
`examples/arca/`:

1. **Reproducible.** The binary is emitted twice and must be byte-identical.
2. **The audit.**
   - `--potestates Mundus,ambitus,archivum` must PASS.
   - Without `archivum` it must FAIL.
   - The binary's syscall set must be a subset of section 1's table. One
     outside the table is a FAIL.
3. **Differential against GNU tar.**
   - Reliquary's own archives (`pack_tree`'s flags, and the same corpus as
     `arca`'s proba) are extracted by GNU tar into one directory and by
     `explicator` into another.
   - GNU tar must exit 0.
   - The two trees must be equal as sets of `(path, type, sha256 of
     contents)`. Modes and times are excluded, per section 3.
   - Separately, the modes must be exactly `0600`/`0700`, plus `S_ISGID`
     where the parent has it.
4. **The hostile corpus.** `arca`'s 32 cases plus the extractor's own:
   - a duplicate member;
   - a file before its directory;
   - a long name to a missing parent;
   - an escaping name hidden in a long-name record.

   Each case must exit with its expected status. Nothing may exist outside
   `DEST` (a sentinel tree beside it is hashed before and after), and
   nothing under `DEST` may be anything but a regular file or a directory.
5. **The fuzz.** 3000 header mutants per seed, as in `arca`, checked in
   **both** directions:
   - For each mutant `explicator` admits, GNU tar must exit 0 and its tree
     must equal `explicator`'s.
   - For each mutant that `arca` admits *and* GNU tar extracts with exit 0
     into only files and directories, `explicator` must admit it too, or
     refuse it for one of section 3's enumerated reasons: non-UTF-8 name,
     duplicate, missing parent, or empty component. Report the count per
     reason. Any other refusal is a FAIL.

     Without this direction, an extractor that refused almost everything
     would pass.
   - For each mutant it refuses, step 4's invariants must hold.
   - A run with no admitted mutant, or no refused one, fails.
6. **Capability mutants.** The program must fail to compile (`EXS-E0421`)
   when a helper that holds only `sicut d` draws `archivum`. It must fail
   the audit when built with a hand-added `openat` site.
7. **Throughput.** MiB/s against `tar -x` on the same payload. Reported,
   not gated: the reference prelude's buffered I/O measured about 181 MiB/s
   for `cat` (`docs/design/runtime.md`).

## 5. What it needs, in order

| step | what | state |
|---|---|---|
| 1 | ADR 0017 stage 2: `m.archivum()`, `Directorium`, `ad_radicem`, `infra`, `lege_ex`, `crea`, the writer type, `sicut` forwarding | in progress |
| 2 | `?` (`docs/design/sum-types.md` D4) | in progress |
| 3 | ADR 0019 (supersedes 0018): `d.sigilla()` (Landlock seal, L1–L4), `d.conde` (`mkdirat`, K1/K2/K4, requires the seal), and `d.crea_ex(via, l, n)` (create, copy, close; no handle) | **Proposed: needs the owner's acceptance**, after 1 |
| 3a | a stderr writer (`Scriptor.ad_errorem(a)` or an fd-2 row) and decimal formatting of a `u16`, for section 2's diagnostics | after 1 |
| 4 | `argv`: `a.argumentum(i: mensura) -> eventus<textus, erratum>` on `ambitus`. The carrier already holds `argc`/`argv` (`ExsAmbitus @16/@24`). The prelude computes the length, validates UTF-8 (§5.1), and refuses an out-of-range index. §4.6's "process environment" covers it; it needs one sentence there. No syscall. | in progress (Sonnet, then a Fable review) |
| 5 | bytes to `textus`: a validated conversion from a byte buffer to a `textus` view. Member names arrive as bytes in an `acies<u8, 4096>`. Nothing in the prelude converts them today, so no program can name a file from its input. Generally useful, beyond this program. | in progress (Sonnet, then a Fable review) |
| 6 | `examples/explicator/` and section 4's certification | after 1–5 |
| 7 | Oligarchy: reliquary's `extract_payload` keeps its `arca` judging pass (so nothing is created for a hostile payload), then pipes `zstd -dc` into `explicator DEST` instead of running `tar -xf`, and removes `DEST` on any failure. The binary comes from the `exsecutor` flake input (already pinned by Oligarchy's screensaver). The reliquary judge (`arca.gen.c`) and the extractor are pinned to **one** exsecutor commit, so their two readings of an archive cannot diverge. A decision on restore modes (section 3). A Rust test runs the real binary. | after 6 |

## 6. Open

- Steps 4 and 5: their surface spellings are provisional under §3.9, and
  their spec text is owed.
- Whether reliquary keeps its second, tar-based name listing
  (`extract_payload`) once tar no longer extracts. It reads tar's own view,
  which then guards nothing that runs.
- Whether `arca.gen.c` stays in reliquary once `explicator` judges its own
  stream. It would then be a third reading of the archive. It keeps
  "nothing is created for a hostile payload", which a single pass cannot.
- A non-root caller, a kernel with Landlock disabled, a pre-5.13 kernel:
  none measured.

## 7. What the review changed (2026-10-02)

A Fable review of the first version found a blocker and eleven other
problems. Each was re-measured or re-reasoned before it was acted on. The
blocker and the Landlock result were re-measured independently.

- **Blocker: descriptor exhaustion.** One handle per create, and no
  `close` (ADR 0017 D7), failed at member 1020 of 1024. Fixed by `crea_ex`
  and a handle-less `conde` (section 3).
- **ADR 0018's premise was false.** Landlock closes the moved-parent window
  and the multi-component hole. ADR 0019 supersedes it, and adds a seal
  that also makes "no device, FIFO, symlink or deletion" a kernel
  guarantee.
- **ADR 0018's K3 could not be implemented** in the audit's symbol-less
  sweep. It was dropped as no longer load-bearing.
- **The status line overstated acceptance.** 0018 is Superseded and 0019
  is Proposed.
- **Section 1 claimed that every word was checked against bytes.** It is
  split into audit, kernel and source strengths.
- **The fuzz could be passed by an extractor that refused everything.**
  It is now checked in both directions.
- Plus: tar's exit status is required; the `a//` difference is listed; the
  modes decision is surfaced; setgid is noted; exit 5 and the stderr writer
  are added; partial `DEST` is removed by the host; the two `arca` readings
  are pinned to one commit; "subset" replaces "equal".
