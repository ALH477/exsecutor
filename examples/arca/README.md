# examples/arca/

**A tar-header judge for root.** `arca.exsc` decides, one 512-byte header at
a time, whether a tar stream is something root may hand to GNU tar to
extract. It is pure: no `poscit`, no capability parameter (spec §4.1
rule 6), no allocation, and no I/O.

## Why

Its consumer is reliquary, Oligarchy's cold-storage tool. Reliquary runs as
root and extracts payloads that may have come off a USB stick, and a stick's
label is forgeable. Before this, reliquary checked member **names** (absolute
paths, `..`) and never member **types**. This was measured with GNU tar 1.35
as root, using reliquary's own flags (`--no-same-owner --no-same-permissions`).
A hostile payload produced:

- `blk/disk`, a block device `259,0` (an NVMe disk), mode `brw-r--r--`, so
  any local user could read the raw disk;
- a FIFO;
- a symlink to `/etc/shadow`.

The setuid bit, for the record, *was* stripped.

## The rule: the producer's own output, and nothing else

A gate that parses a format differently from the program that acts on it can
be shown one archive while the program extracts another. `oligarchy-p2p`'s
narinfo parser had exactly that bug. So `arca` does not "parse tar". It
admits only what reliquary's packer (`tar --sort=name --mtime=… --owner=0
--group=0 --numeric-owner -cf`) writes, in the exact bytes GNU tar writes, and
refuses everything else.

| what | the gate requires |
|---|---|
| format | GNU magic `ustar  \0` only. POSIX ustar and pax are refused, and so is the name prefix that only POSIX has. |
| types | `0` (file), `5` (directory), `L` (GNU long name). No link, device, FIFO, sparse, volume or extension header. |
| checksum | 6 octal digits, then NUL, then a space, equal to the unsigned byte sum. |
| size | 11 octal digits and a NUL, or base-256 below 2^63 (GNU's form for files of 8 GiB and up). |
| mode, uid, gid, mtime | the octal digits and NUL that GNU writes. GNU tar reads other shapes with a warning and a failing exit status, which the fuzz found. |
| directories | the name ends in `/` and the size is 0; a file's name does not end in `/`. |
| names | non-empty and relative, no `..` component, no control byte, no DEL, no backslash. |
| long names | 1 to 4096 bytes whose only NUL is the last, a header named `././@LongLink`, and then a file or directory header. |
| end | the first zero block, which is where GNU tar stops. The host then requires the rest of the stream to be zeros. |

## The host's half

A host (reliquary's Rust, or `proba.c` here) does the I/O and obeys the
verdicts:

1. It reads a 512-byte block and calls `caput_iudica`.
2. It reads a long name's bytes when the verdict is 2.
3. It skips `saltus(magnitudo(h))` bytes of file data.
4. It stops at verdict 3.

The host never interprets a header itself, so there is no second parser to
disagree with this one.

The host must obey the order of those steps. `saltus(magnitudo(h))` on a
header the gate has not admitted overflows on `magnitudo`'s sentinel, and
that is a trap. A trap calls a `_Noreturn` hook, which in reliquary would
end a root process. `examples/abortus/` shows a guard that turns the trap
into a returned error instead, and it uses exactly this call as one of its
test cases.

## Why in-process, not a process

`examples/custos/filtrum.exsc` shows the gate pattern as a freestanding
process whose syscalls are audited (`read`, `write`, `exit_group`). That would
be stronger here too, but the reference prelude's `lege_octeto` is one
`read(2)` per byte (`docs/design/runtime.md`), and a payload is gigabytes of
file data the host must stream past. Until `Lector` reads in blocks, `arca`
is linked as C (`--emitte c`), the way the bridge links `custos`.

## Checks: `proba_c.sh` (after `make all`; needs python3 and GNU tar)

1. Emits the unit twice and requires byte-identical output (63,400 bytes).
2. Builds `proba.c` against the unit with gcc and clang, at `-O0` and `-O2`,
   under UBSan, with the header `exsc --emitte h` generates force-included
   (`proba.c` includes `arca.h` itself, so the compiler holds the two to
   each other; `docs/design/c-backend.md` D9).
3. Runs `proba.py` on each build:
   - reliquary's own archives are admitted, and the member list equals
     `tar -tvf`'s, in order. The archives include long names, UTF-8 names
     and nested 120-character directories.
   - a 32-case hostile corpus is refused, each case with its expected
     verdict.
   - a 3000-mutant header fuzz must produce **zero** disagreements with GNU
     tar. Every mutant the gate admits must be read by GNU tar as exactly
     the members the gate listed, each one a file or a directory, with exit
     status 0. A run with no admitted mutant, or no refused one, fails.
4. Requires each of six behaviour mutants of `arca.exsc` to fail `proba.py`:
   types unchecked, `..` unchecked, POSIX magic admitted, numeric fields
   unchecked, checksum unchecked, long name not re-judged.
5. Checks the capability claim:
   - an ambient draw of `ambitus` or `archivum` is refused with `EXS-E0421`;
   - a `Scriptor` parameter breaks `arca.h`.

**Two findings from building it.**

- The fuzz's first run found 41 admitted mutants that GNU tar read with
  exit status 2. Their mode, uid and mtime fields were malformed (now
  verdict 27).
- A later seed found a "disagreement" that was the *harness's* bug. It
  parsed names out of `tar -tv` with `split()`, which lost a leading space.
  Names now come from `tar -tf`.

Six seeds (1 to 6), 18,000 mutants: 4,629 admitted, 0 disagreements.
