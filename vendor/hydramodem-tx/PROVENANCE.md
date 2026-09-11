# HydraModem reference-transmitter output — vendored as a certificate

Three files, copied verbatim, never edited. They are **program output vendored
as test data**, not code and not the program that produced them: three WAVs,
each the reference HydraModem TX chain's rendering of one 17-byte DeModFrame.

| file | frame (hex) | bytes | sha256 |
|---|---|---|---|
| `d310123400a1ffffdeadbeef0a1b2ca961.wav` | `D310123400A1FFFFDEADBEEF0A1B2CA961` (`dcf_loopback`'s frame) | 38060 | `f422db1d864c499a7ef3c4a6757aeb0eb6d17b0c82cbd79055b7038b30a280bd` |
| `d31312340001ffffdeadbeefab12cd24c0.wav` | `D31312340001FFFFDEADBEEFAB12CD24C0` (the DeModFrame example frame, §14 entry 23's anchor) | 38060 | `94763a115aedfa43d18ecbf365ea34f038803330679d2bd6efcc4b2b5774cd65` |
| `d310000000000000000000000000005b80.wav` | `D310000000000000000000000000005B80` (all-zero body, valid) | 38060 | `7fc0d246fd73e1bc655fde71d3a7d90671021675eec083c59d4ddc75b492b293` |

Filenames are the frame's 17 bytes as 34 lowercase hex characters (the input
frame is stored big-endian; case is normalised to lowercase for the filename
only — the WAV bytes themselves are untouched program output).

Tree digest, `find . -type f ! -name PROVENANCE.md | sort | xargs sha256sum |
sha256sum` under `LC_ALL=C`:

```
5ce6a3d11d10b0a3d7143a011963300c96ccadab239a1479a320de07ba45a3f2
```

Computed **from the repository root**, not from inside this directory — same
convention as `vendor/fasmg-x86/` and `vendor/hydramesh-wire/`; `flake.nix`'s
`modem-vendor-integrity` check asserts it. `LC_ALL=C` is pinned for the same
reason recorded in the other two `PROVENANCE.md` files: `sort`'s collation is
locale-dependent, and a digest that moves with the developer's `LANG` is not
an integrity check.

## Source

- Upstream: `https://github.com/ALH477/HydraMesh` (HydraMesh monorepo;
  `hydramodem/` is a subtree of it)
- Local origin: `/home/asher/Documents/HydraMesh`
- Commit: `fce2813f85ac17e29f34fa1adf4008056b116318` (verified clean —
  `git -C /home/asher/Documents/HydraMesh status --porcelain -- hydramodem/`
  reported no local changes, so the commit tree was used directly rather than
  needing `git archive`)
- Program: `hydramodem/dcf-tools/frame_tx.c` and `hydramodem/dcf-tools/frame_rx.c`,
  the reference DSP backend (`hydramodem/src/hydra_dsp_ref.c`, **not** the
  Faust-compiled backend), default profile (CONV FEC + interleave —
  `hydra_profile_default`, unmodified by `dcf-tools/frame_profile.h`'s CLI
  overrides)

## What was built, and how

Extracted a clean copy of the pinned commit into a scratch directory (never
inside this tree, never written back into HydraMesh):

```sh
git -C /home/asher/Documents/HydraMesh archive fce2813 hydramodem | tar -x -C <scratch>
```

Compiled `frame_tx` and `frame_rx` directly against the reference-DSP sources
— the same file set `Makefile`'s `CORE` + `REF_OBJ` link into
`libhydramodem.a` by default, minus `hydramodem_version.c` (neither tool
prints a version):

```sh
cc -std=gnu11 -O2 -Wall -Wextra -I<scratch>/hydramodem/src \
  <scratch>/hydramodem/dcf-tools/frame_tx.c \
  <scratch>/hydramodem/src/hydra_profile.c \
  <scratch>/hydramodem/src/hydra_crc.c \
  <scratch>/hydramodem/src/hydra_fec.c \
  <scratch>/hydramodem/src/hydra_conv.c \
  <scratch>/hydramodem/src/hydra_interleave.c \
  <scratch>/hydramodem/src/hydra_frame.c \
  <scratch>/hydramodem/src/hydra_modem.c \
  <scratch>/hydramodem/src/hydra_dsp_ref.c \
  <scratch>/hydramodem/src/wav.c \
  -lm -o frame_tx
```

and identically for `frame_rx.c` in place of `frame_tx.c`. `-O2 -Wall -Wextra`
is `dcf-tools/build.sh`'s own `CFLAGS` default (the vendored WAVs here are
this recipe's output, not `Makefile`'s — `Makefile`'s `libhydramodem.a` links
the reference DSP at its own default, `-O3`; see below for why the difference
does not matter to the vendored bytes).

Compiler: `gcc (GCC) 14.3.0` (`cc --version`, this build host).

```sh
./frame_tx D310123400A1FFFFDEADBEEF0A1B2CA961 d310123400a1ffffdeadbeef0a1b2ca961.wav
./frame_tx D31312340001FFFFDEADBEEFAB12CD24C0 d31312340001ffffdeadbeefab12cd24c0.wav
./frame_tx D310000000000000000000000000005B80 d310000000000000000000000000005b80.wav
```

Each produced 38060 bytes; the first hashes to
`f422db1d864c499a7ef3c4a6757aeb0eb6d17b0c82cbd79055b7038b30a280bd`, matching
the figure a prior survey measured with the prebuilt binary. No mismatch
occurred, so there was nothing to stop and report.

## Optimisation level does not change the output bytes — measured, not assumed

The reference DSP is floating-point, so it was not obvious a priori that
`-O0` and `-O3` code generation would produce bit-identical audio once
quantised to `s16`. Built all three tools (`frame_tx` only; `frame_rx` was
built once, at `-O2`, since it only needs to recover the same three frames)
at `-std=gnu11 -Wall -Wextra` with `-O0`, `-O2`, and `-O3`, and rendered all
three frames under each:

| optimisation | sha256 (per frame) matches `-O2`? |
|---|---|
| `-O0` | yes, all three |
| `-O2` | (reference) |
| `-O3` | yes, all three |

All nine renders (three frames × three optimisation levels) produced
byte-identical WAVs. The result is consistent with the survey's expectation:
output is quantised to `s16` PCM, and the reference DSP path (`hydra_dsp_ref.c`)
does no timing- or precision-sensitive branching that code generation could
perturb across this range.

## `frame_rx` round-trip

Built `frame_rx` the same way (reference DSP, default profile) and decoded
each of the three vendored WAVs:

```sh
./frame_rx d310123400a1ffffdeadbeef0a1b2ca961.wav   # -> d310123400a1ffffdeadbeef0a1b2ca961
./frame_rx d31312340001ffffdeadbeefab12cd24c0.wav   # -> d31312340001ffffdeadbeefab12cd24c0
./frame_rx d310000000000000000000000000005b80.wav   # -> d310000000000000000000000000005b80
```

All three recovered exactly the input frame (case-normalised hex; the frame
bytes are identical). Round-trip verified for all three vendored WAVs.

## Licensing

`LGPL-3.0-only`, © DeMoD LLC (`hydramodem/LICENSE`, `hydramodem/NOTICE`) — the
same copyright holder as this repository, as with `vendor/hydramesh-wire/`.

**These are program outputs vendored as test data, not the program that
produced them, and not relicensed.** No HydraModem source is vendored here —
only its output — and the output keeps the source's licence identifier as a
matter of provenance discipline, the same arrangement `vendor/hydramesh-wire/`
and `vendor/fasmg-x86/` have. Do not add this repository's
`LICENSE.EXCEPTION` to these files: nothing here is linked into `exsc`, and
`LICENSE.EXCEPTION` applies only to what `exsc` emits from a user's input.

## Why it is vendored rather than referenced or regenerated at test time

Same reason as `vendor/hydramesh-wire/` and `vendor/fasmg-x86/`: a test that
reaches outside the tree, or that depends on a `cc` and a `libm` being
present and behaving identically at test time, is not reproducible. §9.3
requires output to be a function of (source, `ego`, lockfile, flags); "run
HydraModem's reference DSP on this machine today" is none of those, even
though the O0/O3 result above shows it is stable across the compilers tried.
Vendoring the rendered WAVs makes the next milestone's program tests
(comparing `exsc`-compiled-and-run stdout against these files,
`stdout=vendor/hydramodem-tx/<name>.wav`) content-addressed by git instead.

## Status in this repository

`[UNTESTED]` as an Exsecutor artifact. No Exsecutor program renders a
DeModFrame to audio yet; these are a certificate waiting for one, the way
`vendor/hydramesh-wire/golden_vectors.json` was before §14 entry 23 existed.
