# `explicator` — reliquary's extractor as an audited Exsecutor process

**Status:** design, 2026-10-02. The repository owner asked for it ("do it")
as the language's flagship: one real program, run as root on hostile
input, whose binary is proven unable to do what tar can be talked into.
Nothing here is built yet. Section 5 is the order it gets built in.

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
| `mkdirat(258)` | directories | ADR 0018's K1–K4: one checked component, mode `0700` |
| `close(3)`, `fstat(5)` | the stage-1 routines | — |
| `exit_group(231)` | — | — |

So the binary cannot:
- open a socket;
- `execve` anything;
- create a device, a FIFO, a symlink or a hard link;
- change an owner or a mode;
- read the environment;
- open a path outside the one root it derived from `argv[1]`.

That is the claim, and every word of it is checked against the shipped
bytes, not against the source.

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
| 3 | `DEST` could not be rooted (relative, missing, not a directory, no `openat2`); the errno goes to stderr |
| 4 | a create failed (the errno goes to stderr), including `EEXIST` |
| 16–27 | `arca`'s verdict on the offending header, unchanged |
| 100 | the stream ended inside a member |
| 101 | data after the end block |

## 3. What it does

It is one compilation unit: `examples/arca/arca.exsc`, unmodified, then
`explicator.exsc`. It has the same shape as `filtrum.exsc` over
`custos.exsc`. For each 512-byte block:

1. `caput_iudica(h, l, nl, habet)`. This is the same judge, so there is no
   second parser to disagree with.
2. Verdict 2 (long name): read `magnitudo(h)` bytes into `l`, skip to the
   block boundary, and set `habet = 1`.
3. Verdict 1 (directory): `d.conde(name)` with the trailing `/` removed.
4. Verdict 0 (file): `d.crea(name)`, copy exactly `magnitudo(h)` bytes from
   stdin to the writer, then skip `saltus(magnitudo(h)) - magnitudo(h)`
   padding bytes.
5. Verdict 3 (end): require zeros until end of input.
6. Any other verdict: exit with it.

Every path is opened from the one root by its full relative name, with
B|M|X. A directory created by an earlier member is reached through the
kernel's walk each time, and never through a descriptor held across
members. That keeps ADR 0018's moved-parent window to one `conde` call
instead of the whole extraction.

### Deliberate differences from `tar -x`

The certification in section 4 compares only what is the same by
construction. These differences are deliberate:

- **Modes.** Files are `0600` and directories `0700`: they are constants,
  so the audit can read them. Reliquary's `tar --no-same-permissions` gives
  `0644`/`0755` under root's usual umask. The restored tree is root's, and
  widening it is a separate, explicit step.
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
   - The binary's syscall set must equal section 1's table exactly. A
     syscall that never appears is fine; one outside the table is a FAIL.
3. **Differential against GNU tar.**
   - Reliquary's own archives (`pack_tree`'s flags, and the same corpus as
     `arca`'s proba) are extracted by GNU tar into one directory and by
     `explicator` into another.
   - The two trees must be equal as sets of `(path, type, sha256 of
     contents)`. Modes and times are excluded, per section 3.
4. **The hostile corpus.** `arca`'s 32 cases plus the extractor's own:
   - a duplicate member;
   - a file before its directory;
   - a long name to a missing parent;
   - an escaping name hidden in a long-name record.

   Each case must exit with its expected status. Nothing may exist outside
   `DEST` (a sentinel tree beside it is hashed before and after), and
   nothing under `DEST` may be anything but a regular file or a directory.
5. **The fuzz.** 3000 header mutants per seed, as in `arca`:
   - For each mutant `explicator` admits, GNU tar's tree must equal its
     tree.
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
| 3 | ADR 0018: `d.conde`, `mkdirat` K1–K4 | accepted, after 1 |
| 4 | `argv`: `a.argumentum(i: mensura) -> eventus<textus, erratum>` on `ambitus`. The carrier already holds `argc`/`argv` (`ExsAmbitus @16/@24`). The prelude computes the length, validates UTF-8 (§5.1), and refuses an out-of-range index. §4.6's "process environment" covers it; it needs one sentence there. No syscall. | after 1 |
| 5 | bytes to `textus`: a validated conversion from a byte buffer to a `textus` view. Member names arrive as bytes in an `acies<u8, 4096>`. Nothing in the prelude converts them today, so no program can name a file from its input. Generally useful, beyond this program. | after 1 |
| 6 | `examples/explicator/` and section 4's certification | after 1–5 |
| 7 | Oligarchy: reliquary's `extract_payload` keeps its `arca` judging pass (so nothing is created for a hostile payload), then pipes `zstd -dc` into `explicator DEST` instead of running `tar -xf`. The binary comes from the `exsecutor` flake input (already pinned by Oligarchy's screensaver). A Rust test runs the real binary. | after 6 |

## 6. Open

- Steps 4 and 5: their surface spellings are provisional under §3.9, and
  their spec text is owed.
- Whether reliquary keeps its second, tar-based name listing
  (`extract_payload`) once tar no longer extracts. It reads tar's own view,
  which then guards nothing that runs.
- A non-root caller, a pre-5.6 kernel, and a tree shared with an untrusted
  writer during extraction (ADR 0018 Decision 4): none measured.
