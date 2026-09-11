# HydraModem reference-transmitter output — vendored as a certificate

Four files, never edited. They are **program output vendored as test data**,
not code and not the program that produced them. Three are WAVs, copied
verbatim, each the reference HydraModem TX chain's rendering of one 17-byte
DeModFrame. The fourth, `symbola_basis.bin`, is the symbol streams of 137
further renders — the 137-word basis of `docs/design/modem.md` D9 — reduced
from the WAVs by one exact rule whose script is printed below: program
output passed through a stated, checkable function, not a re-derivation of
what the program would have written.

| file | input (hex) | bytes | sha256 |
|---|---|---|---|
| `d310123400a1ffffdeadbeef0a1b2ca961.wav` | `D310123400A1FFFFDEADBEEF0A1B2CA961` (`dcf_loopback`'s frame) | 38060 | `f422db1d864c499a7ef3c4a6757aeb0eb6d17b0c82cbd79055b7038b30a280bd` |
| `d31312340001ffffdeadbeefab12cd24c0.wav` | `D31312340001FFFFDEADBEEFAB12CD24C0` (the DeModFrame example frame, §14 entry 23's anchor) | 38060 | `94763a115aedfa43d18ecbf365ea34f038803330679d2bd6efcc4b2b5774cd65` |
| `d310000000000000000000000000005b80.wav` | `D310000000000000000000000000005B80` (all-zero body, valid) | 38060 | `7fc0d246fd73e1bc655fde71d3a7d90671021675eec083c59d4ddc75b492b293` |
| `symbola_basis.bin` | the zero word, then each of the 136 one-hot words (section "The symbol-stream basis", below) | 48772 | `45c6125dd13416837a7dc2ec96f82156dd4e76402737a1ede04c3f902d42d402` |

The WAVs' filenames are the frame's 17 bytes as 34 lowercase hex characters
(the input frame is stored big-endian; case is normalised to lowercase for
the filename only — the WAV bytes themselves are untouched program output).

Tree digest, `find . -type f ! -name PROVENANCE.md | sort | xargs sha256sum |
sha256sum` under `LC_ALL=C`:

```
51cca6f0c60f287f06a523aaaf2bfaca737edbbda75090343d79d3b61e72e1da
```

(It was `5ce6a3d11d10b0a3d7143a011963300c96ccadab239a1479a320de07ba45a3f2`
over the three WAVs alone, before `symbola_basis.bin` was added.)

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

## The symbol-stream basis — `symbola_basis.bin`

The certificate `docs/design/modem.md` D9 calls for: the symbol stream of
the zero word and of each one-hot word, so that, given the pipeline's
affinity over GF(2), agreeing on these 137 words is agreeing on all 2^136
17-byte inputs. Vendored 2026-09-11, from the same commit and the same
build as the WAVs above.

**Format.** 137 records of 356 bytes, 48,772 bytes, nothing else — no
header, no padding. Byte `356·k + s` is the tone index, `0x00` or `0x01`, of
symbol `s` of word `k`: symbols 0–23 are the preamble, 24–39 the sync word,
40–355 the 316 interleaved coded bits. Word `k = 0` is seventeen zero bytes;
word `k = 1..136` has exactly wire bit `i = k − 1` set, bit `i` being byte
`i div 8` under the mask `0x80 >> (i mod 8)` — MSB first across the wire,
the numbering of §14 entry 23's syndrome basis (`tests/conformance/
entry23/probatio.exsc`, `verte`). None of the 137 is a valid DeModFrame, and
every one has a nonzero modem CRC (the zero word's is `0xc7ec`; measured on
all 137) — which is the point: the three WAVs above are valid frames, whose
modem CRC is always `0x0000`.

**Why a byte a symbol**, where `docs/design/modem.md` proposed 45 bytes a
record (356 bits packed MSB-first, 6,165 bytes in all). Three reasons, at a
cost of 42,607 bytes: a first differing byte at 0-based offset `b` *is* its
location — word `b div 356`, symbol `b mod 356` — so `cmp`'s report names
the failing word and symbol with no helper; a byte holds a tone index, so
the same format serves the 4- and 8-FSK profiles of the design's M4, where
one bit per symbol would not; and with nothing packed there is no bit order
inside the format for the extractor and the program under test to agree on.

**How it was made.** `frame_tx` built exactly as above (`git archive
fce2813 hydramodem` into a scratch directory, the `cc -std=gnu11 -O2 -Wall
-Wextra` line, `gcc (GCC) 14.3.0`; the rebuild reproduced all three WAVs'
digests before anything else was rendered), then, with Python 3.13.12:

```sh
python3 extrahe.py ./frame_tx \
  vendor/hydramodem-tx/d310000000000000000000000000005b80.wav \
  <scratch>/renders symbola_basis.bin
```

**The rule** — the WAV is header ‖ lead ‖ 356 blocks of 96 bytes ‖ tail,
and a block is one of two: block `A` (tone 0) and block `B` (tone 1) are
symbols 0 and 1 of the vendored all-zero-body WAV, both preamble symbols.
Every render must be 38,060 bytes, carry that WAV's header, lead and tail
unchanged, and have every block equal to `A` (→ 0) or `B` (→ 1); anything
else stops the run and names the word and the symbol. **It held on every
block of every render**: 137 renders, 48,772 blocks, none matching neither,
header, lead and tail identical throughout. That is also a measurement, at
137 inputs, of the reference modulator having no memory across symbols —
each symbol's 96 bytes a function of its tone alone.

The script, verbatim (sha256 of the file
`ae29538afcee4815f32b978d022b1050647e958315964e61eff0e709628e04af`; it is
not vendored, because nothing in the tree runs it — this copy is how anyone
re-derives the file):

```python
#!/usr/bin/env python3
"""extrahe.py -- the 137 basis renders of HydraModem's frame_tx, reduced to
the symbol streams they carry: vendor/hydramodem-tx/symbola_basis.bin.

usage: extrahe.py FRAME_TX REFERENCE_WAV WORKDIR OUT

FRAME_TX       frame_tx built from HydraMesh fce2813 (PROVENANCE.md's recipe)
REFERENCE_WAV  vendor/hydramodem-tx/d310000000000000000000000000005b80.wav
WORKDIR        where the 137 renders are written, k000.wav .. k136.wav
OUT            the basis file: 137 records of 356 bytes, byte s of record k
               the tone index (0 or 1) of symbol s of word k

Word k = 0 is seventeen zero bytes; word k = 1..136 has exactly wire bit
i = k - 1 set, bit i being byte i // 8 under the mask 0x80 >> (i % 8) -- MSB
first across the wire, the numbering of spec §14 entry 23's syndrome basis.

The rule. The reference WAV is header || lead || 356 symbol blocks of 96
bytes || tail. From REFERENCE_WAV (already certified by M1) take the header
(bytes 0..43), the lead (44..1963), block A = symbol 0 (a preamble symbol,
tone 0) and block B = symbol 1 (tone 1), and the tail (36140..38059). Every
render must be 38,060 bytes with that header, lead and tail, and each of its
356 blocks must equal A or B -- A gives 0, B gives 1. Anything else stops the
run with the word and the symbol: the reduction is only a reduction if the
block map is exact on every block of every render.

Standard library only. Prints one line per render: k, the word in hex, the
render's sha256.
"""

import hashlib
import os
import subprocess
import sys

WORDS, SYMBOLS, BLOCK = 137, 356, 96
LEAD_END, TAIL_START, LENGTH = 44 + 1920, 44 + 1920 + 356 * 96, 38060


def word(k):
    w = bytearray(17)
    if k > 0:
        i = k - 1
        w[i // 8] = 0x80 >> (i % 8)
    return bytes(w)


def parts(ref):
    assert len(ref) == LENGTH, 'reference is %d bytes' % len(ref)
    a = ref[LEAD_END:LEAD_END + BLOCK]
    b = ref[LEAD_END + BLOCK:LEAD_END + 2 * BLOCK]
    assert a != b, 'blocks A and B are equal'
    return ref[:44], ref[44:LEAD_END], a, b, ref[TAIL_START:]


def symbols(wav, ref, label):
    head, lead, a, b, tail = parts(ref)
    if len(wav) != LENGTH:
        sys.exit('%s: %d bytes, expected %d' % (label, len(wav), LENGTH))
    if wav[:44] != head or wav[44:LEAD_END] != lead or wav[TAIL_START:] != tail:
        sys.exit('%s: header, lead or tail differs from the reference' % label)
    out = bytearray()
    for s in range(SYMBOLS):
        blk = wav[LEAD_END + BLOCK * s:LEAD_END + BLOCK * (s + 1)]
        if blk == a:
            out.append(0)
        elif blk == b:
            out.append(1)
        else:
            sys.exit('%s: symbol %d is neither tone block' % (label, s))
    return bytes(out)


def main():
    frame_tx, refpath, work, outpath = sys.argv[1:5]
    with open(refpath, 'rb') as fh:
        ref = fh.read()
    basis = bytearray()
    for k in range(WORDS):
        w = word(k)
        path = os.path.join(work, 'k%03d.wav' % k)
        subprocess.run([frame_tx, w.hex().upper(), path], check=True)
        with open(path, 'rb') as fh:
            wav = fh.read()
        basis += symbols(wav, ref, 'word %d' % k)
        print('%03d  %s  %s' % (k, w.hex(), hashlib.sha256(wav).hexdigest()))
    assert len(basis) == WORDS * SYMBOLS
    with open(outpath, 'wb') as fh:
        fh.write(basis)


if __name__ == '__main__':
    main()
```

**The 137 renders** are not vendored (137 × 38,060 bytes, 5.2 MB); their
digests are, exactly as `extrahe.py` printed them — `k`, the word, the
render's sha256 — so a re-render can be checked render by render:

```
000  0000000000000000000000000000000000  6e6efb1fac98e0d7f367621cfb1a6d4d19a4e05a64fc39eb2d0282934641098e
001  8000000000000000000000000000000000  5956fa0f5e9dec623405524db80c86160d8e7f5ee6aff1eaaadf55ac139c2dfe
002  4000000000000000000000000000000000  2c4a28c780b31bd55eb50123b75c0dcd504aa930fd329d24dc426001744858b0
003  2000000000000000000000000000000000  bd08ec7386065b5a5cd24308ec96b7e1ec99600adfff8922fa0be9fb0a903ec0
004  1000000000000000000000000000000000  3b57800e1a07f16ffa87c4626c4e27ab04c85137325f31da6853e2dbd768acc9
005  0800000000000000000000000000000000  7546eecc7466498dd5f03283a9893413ec8e082a99a142b953b1b21d8afdc148
006  0400000000000000000000000000000000  27152f9989b4a0499394408b2cd5d2e4f8c9226c2e6eab01dc62f3058696d1c9
007  0200000000000000000000000000000000  e0439e51a21c2a0dc21c5a9bd8036f77ffaa6170e3607c6fec83183e7d5fe799
008  0100000000000000000000000000000000  7a65eb88b6571fa05c49f104b9dfd39bd4f11456290374b4c08253abefc2702e
009  0080000000000000000000000000000000  bf73c7a0259bffb3b3cd5b39fba0bf32eb441a30fc9b6000faf17baf980b6ead
010  0040000000000000000000000000000000  ccd737eea5facd9851f78e4e024587628a6b49bf1eddbad6a51a71be25a3f7fa
011  0020000000000000000000000000000000  a811680f1caa452b18189944a7c2fde0f846bf51548058d7d4b10cf567e482fa
012  0010000000000000000000000000000000  e39afe24ea42234c96703aacefdcacc84fd5ef3afddd963ff470fe57db58d34d
013  0008000000000000000000000000000000  86e297d92b8510058e9e47747f5ddfb260afb409eb0a7b0569a5693eba97bcf2
014  0004000000000000000000000000000000  6040d440ff2a8f2bfeccff24ba7cf50583f698e6346ab974d0dd83638f87f880
015  0002000000000000000000000000000000  5397082fa05c9a1409aee9e25fc32ec5a5f844eff09d9cf1487e7d28db090e6c
016  0001000000000000000000000000000000  ed590a48dc14613d22060d08550e8403103aa29c2aa72af9d01bf1eaad7c4c69
017  0000800000000000000000000000000000  430747159c8ea9ca528e88b194959a74ec3046a3678b683ed6656fc90a5c9cee
018  0000400000000000000000000000000000  3376ab3d3221a1e5d4a2562c1202041a3c0e6616fcf0c3f47c325867db9d4d5e
019  0000200000000000000000000000000000  2b8d99672f3345839e8f85b080bc69d34a2896f71511ff32654ab842bf8631a6
020  0000100000000000000000000000000000  14709ae71164bf4bd4a2b49b47d034e8055f6f457755fd8c60f9534bad9a29e7
021  0000080000000000000000000000000000  376afae1c205b61e81df088d31f538a67e949ba3462df3ffe58f6ae30f3e0d4e
022  0000040000000000000000000000000000  95f8942699289aec6257b2a8458bd7455dff012ced8b24f0c8ad1098da2f72ef
023  0000020000000000000000000000000000  d842cf63d2823eaf2478e484271088a246edfc73faed382a5d682a7b2828cebe
024  0000010000000000000000000000000000  30ba517f6c3a80ba46d3ccc5ad161638ad25c2a47300c134801c9e70f090e347
025  0000008000000000000000000000000000  3d0b4b41b28e8d36692d4e48ccd6c6faf730c20518331512782921cc46596105
026  0000004000000000000000000000000000  23e48124e93dbd2ff87ed8a2c4942d7c4df1ffae8dbd29486a393fdd249ae4d9
027  0000002000000000000000000000000000  fe9a9f0e6300e695398b14039749196abb2ce1f0ad9d727b7891a2d317a0c471
028  0000001000000000000000000000000000  fdd3d1dde209ffe5e6578282a21c7f35f8c5515e527a523614c35bcbfc2f8789
029  0000000800000000000000000000000000  e6edc09667c6a05e44ee9a5209419347fce779b3679403d2d25d1808ae75e739
030  0000000400000000000000000000000000  2ba309b0fdd42f2c00ca21dbb6a27a0b97f469a98f1b97f4959536d51f520083
031  0000000200000000000000000000000000  c65ea703f6ded14e64693a067bece61fb872090d0f2c25e2a672f90830c3af6f
032  0000000100000000000000000000000000  1b1a0703567f458895b192cbaba3709cf96c3b0e0e3a17a2da519556cd245310
033  0000000080000000000000000000000000  c1bca15320878eeb8d3973ecd95bc18becd9150f48271204ef93a5ead37abb12
034  0000000040000000000000000000000000  0d2b34f7a2c670e81616fb3f66dac08b202b76a03dca2ee5f3761ef4a3249a78
035  0000000020000000000000000000000000  edac8c04787e68ef6f34f9eebb4ac16937ab3a172c4deb0125dbdcd9da633bd0
036  0000000010000000000000000000000000  93dfee1d2a1a1ee080ec81cf9e67624c9c500d47e8e865ce30c261e34a3e10f1
037  0000000008000000000000000000000000  7f74d202d14de94d8de2cec72accc29097387ac1a6891c116e50812f443bbba8
038  0000000004000000000000000000000000  cab4d09a73ccf13a064374f7965a31bcc76c4f188a6822f510002d62d49edc09
039  0000000002000000000000000000000000  c33cb0146c2f25b8151463ed9c7c72072e07cb501a4aaca7817f4a1b5c201d71
040  0000000001000000000000000000000000  f07ab8076a7d24e94d9cde641e9b121b8f9594f5d84bdc1e10f08ea7f7627981
041  0000000000800000000000000000000000  b95bec89dbcca272864b1b04e3d3b6e488cc7d13421994936dd05288fe38ad6a
042  0000000000400000000000000000000000  784746640a5d636034ea72f13eb9e9dc7730b669f3fa0cf718829283bab58aea
043  0000000000200000000000000000000000  b149e1cbcfbeee7f351cb7ef10757c98fcc189d4294548acd6af7cc68628833d
044  0000000000100000000000000000000000  1a488bf4732a9c5cbf46e155d4e0b32a9303c73bee5934dc72ac28af7080d2ce
045  0000000000080000000000000000000000  846219f1cba63aaefff20e28ea8c00413cb4e6df0a2cfbba08f30f6cc0d2b6cf
046  0000000000040000000000000000000000  b25a5a46e981e1d5c5951a965ff1036e4d505237009ddbde6541c1317798657a
047  0000000000020000000000000000000000  cda927ea341d9c4b5201cd2bad2797a9ffc04a447b49b3e16ddc6092ef124700
048  0000000000010000000000000000000000  e238348c2860ade6d6c218afdb8616c874a89a29a5161bd3eb7684ca6d0b7696
049  0000000000008000000000000000000000  15f775b25553b222e1ba7aac9c3ce354f3f62eb1e03bc1ffc25f88744f805eb6
050  0000000000004000000000000000000000  57b3c5ec8f8133e5a63988fc8b955835529012f09e63346c26b4fd919e7d0d93
051  0000000000002000000000000000000000  c321243507483c9eb007ed64b56ba9277560c3eec0ff945be0ef1cac46a04155
052  0000000000001000000000000000000000  494556b00c9276d6d65740e9988fa93363c7946cef8a59effbd9214dd617f6e7
053  0000000000000800000000000000000000  f3f4fabf7e1df26f6e4501b5cc709b9327bfb1c344a1959219488b4ed9572c24
054  0000000000000400000000000000000000  d8256ef335205dea658f082b4a30078d5487fcf9fc54230e3868e80f0648cb89
055  0000000000000200000000000000000000  fa3ad0702108fe586232d31ce11f0cd69ef44f955646b13dac5a474d4630df20
056  0000000000000100000000000000000000  dd5c37c7d11f7b2bc21b5d92f3eb02210806c54030abc212ed273f73c44cc198
057  0000000000000080000000000000000000  129fb30c91b1cba094371f25f910639bee434ea91d5967b4a52633b89f774b3c
058  0000000000000040000000000000000000  47cb9ff0eb441129068b7bcb210fa6b4ba1a8a7afa54994c45990eacee3e00f2
059  0000000000000020000000000000000000  c1157a67ef71d6460306cc02f4fc023887e46b43a56c471bd0d97b9efdb44cd0
060  0000000000000010000000000000000000  4dd039e8f3eee82605f584690895de3add43bf575f3789091d9bb81cc4d94a05
061  0000000000000008000000000000000000  6c19b7c06de0010ded8546cce99d29c01d5d695514210c82f4668f063d335dd6
062  0000000000000004000000000000000000  dd1e7008c83a525947022120c6d2a55cca9dddf1f7a3dc14fa239d29a04e3c31
063  0000000000000002000000000000000000  990c28d15d43c4d98d9c7ca752416c22b775ac110c5f4d06083939a40737a2e1
064  0000000000000001000000000000000000  a12e90b18fdc4d1118f46ae1e175498c95d83512482bd4fc115a6a9977e2732f
065  0000000000000000800000000000000000  b67e12353b31a74e03103d48a5929a8ed26f2d50ce9277081f401da052707c22
066  0000000000000000400000000000000000  082f836559157c7e8399cb403389853f0cc48d3f4f911681dbe8fcd7ab16f366
067  0000000000000000200000000000000000  bba0ae9ae07f1047304a264887824be9c7facb58138ec9013b211755bf2369d5
068  0000000000000000100000000000000000  7f283d17047a7f39b362c57a623d99a66220bdebcccd49905c1461b7eb7fdf6d
069  0000000000000000080000000000000000  6bc10489b5a1bcbfe24734bb20b0beab98720ece21603ce5f3d9e5c23238fb98
070  0000000000000000040000000000000000  458719eb1959cfc3e84112612f95055c1fbfa725ce919f0cf7d8140cf0dbed9a
071  0000000000000000020000000000000000  4e607fe46ee668ea92ea3989c60b0080badc384ea77ce375dc6535f27b3c6a47
072  0000000000000000010000000000000000  d96f1c69eca1550a715040738912c06f29c62a96753cb693a26fcafa308fbddb
073  0000000000000000008000000000000000  79b470796730fd96d1d370febb9a5639d611b5fabec7f3373787aff364a147c7
074  0000000000000000004000000000000000  fb3a6ed40b5084e7b539e8fc3ac0e8922967370431943ae27209d5149f00ca5a
075  0000000000000000002000000000000000  f63aeb41e9ff0fd03e0e357903a45a7b55ee5f5510abc25e8b0421dc328a512f
076  0000000000000000001000000000000000  b57d8c32ae414d3d82a0649162868a30ef6ad6f42887fadf3ddaa9104b29eb56
077  0000000000000000000800000000000000  55430809451714c324445a7a7cb47900790d7679606f79dc5124a083244e5723
078  0000000000000000000400000000000000  0156c2d2392fc9198341ce11d23be741d814306104f9dc3d8d80ca6859bdbd83
079  0000000000000000000200000000000000  2761ca2555b308871cfa7c7ee2ed7eb577f6ce7b573c5c92fd61b7d193931ffd
080  0000000000000000000100000000000000  14c13211b98a49c371f4a3fe7e76b5a029a71b767741067556ebeb7cbfbb8590
081  0000000000000000000080000000000000  9e6365cd0f09381aaaa402eb6b823f5074154da520f27ea3ff0d13cd170e4405
082  0000000000000000000040000000000000  859fb24f176f9b5af5c613a7ecbd037f55c4d17ebe4fcba8aa1e7299475ff152
083  0000000000000000000020000000000000  5cd8c828f1afb229217e8feadfb2bc41df6d24da222e8f683ce20cdfde14c477
084  0000000000000000000010000000000000  850571bd3e128ec57d8b3d5a6a8a249fa836f865433b76606020c86facb30bae
085  0000000000000000000008000000000000  658eb45d81a3c98b6f1b8c2b7524ed7e94bb8a6975556f3ce4e76ca51712261a
086  0000000000000000000004000000000000  8b239fb82f6c950d9ca62ee588d3c39f558cfff93254199c6604e9845f701070
087  0000000000000000000002000000000000  348d195af99fe5a165f4bee90f9266692016632fd53de242ba49050e23f8687e
088  0000000000000000000001000000000000  6c548c43dd2315e46bbba603c14beb069cd58a891358269cc7afe16def1eff0c
089  0000000000000000000000800000000000  479b304067f2b51895600ddd6b1706747a09aaf39f43443e6df4338f736d006b
090  0000000000000000000000400000000000  7b7d15d8261533e9d23dcd58484d3d16dec520c2e6a170e922123812cc31eb46
091  0000000000000000000000200000000000  ad0c3b2832b401e18d5d28041163f1e77897744b8ea8ccfaeb4912dfa08b5b38
092  0000000000000000000000100000000000  4683fcbf6d1e3bbf82f9a9b3678fb46da0fc986ad9a4d86c9fa9e7f0b9f1d210
093  0000000000000000000000080000000000  bca9001336c018bedaba155bd20ce6f26a1cc4cb780d0fdb444f5f30588d18d8
094  0000000000000000000000040000000000  faf22ef8116f2c680970bd9c52d2c4bb856ca5e704d71220efa39bee79cdc121
095  0000000000000000000000020000000000  62cc62b033e5a3847571bf17248ffe14192477f8c053ffb84943661274e79775
096  0000000000000000000000010000000000  8d3bde61316a81daf4aecbccc42153b90d4d3f8f6a6c85f8e2f8d765d6786c1c
097  0000000000000000000000008000000000  7cd2943479ffd82212945feafc2861a83241fab8953fea05f3f2305a5ff38b2b
098  0000000000000000000000004000000000  84cf548b07e5ea6689d759be2e90b014aab1b44d7931a8b6d65983b28e0ba300
099  0000000000000000000000002000000000  a4b0fda892f662e2bf8952b492f76f055936525856b6389c8cb3febef4291183
100  0000000000000000000000001000000000  0acfe6a2dba434edd35791d417d4e693b656d110e361c455b0bc42b6b42971d2
101  0000000000000000000000000800000000  9e351d4a02f4ddf5fe2ad50f69b804c6acba9f6fd706d5662fa1affd9617f77d
102  0000000000000000000000000400000000  e2359944be60585088bb9595ca71a2c14a9a3be073568f1a972edfb57ad7b153
103  0000000000000000000000000200000000  a08cefd54ee4c11b3cdfe030d634ef03b15fb3ace2a43d3c7e8ee72fb69d3d77
104  0000000000000000000000000100000000  773857111fe38b7001d822d64657ade701b8b35cf01433989663e62576e9c4f9
105  0000000000000000000000000080000000  ab76bf77bec73c1f851adf036be17bb45f5781bf8de02518eda7f9003597e36c
106  0000000000000000000000000040000000  8143b60cc68b30cbdf33561cfeec6a506878c33448f3cdb3f0ee405b9f53b4c9
107  0000000000000000000000000020000000  cbfe40e3b26a366efd79b235ffef1342a2709034c64e935a21b4f51bcbdf5093
108  0000000000000000000000000010000000  d408a67631dd389d8c5315bae8252553aa31e2af3258f8950dbe3fd6c4082094
109  0000000000000000000000000008000000  8a0ccea27dcfc4136fc88805d7a22d184f1cd8e8f6299de87ef1ee78963d89f7
110  0000000000000000000000000004000000  04ddd975c9942437d680ecd959d23dd0fb20ba56a79310240c99a9070aa0d711
111  0000000000000000000000000002000000  80aa4c79a30107e8475593399f2565ace47156bd990e0aefe66e4454d79ce510
112  0000000000000000000000000001000000  d70eb39fb7f272877e389bc216f910d5f78973e3ffcda37f11d77d29492ce2f0
113  0000000000000000000000000000800000  4317b25c70fb441190bd229f98f4746f080f15049690e06ebc4a4a374a0588fc
114  0000000000000000000000000000400000  4bc0ae379f20811e9fd0bd88f9bc9a7667e8bf3253c5694b60a0d2243eae7ea5
115  0000000000000000000000000000200000  a805b900a8cee75badd9913654074f95cf4eb1cd13ec4ff2c68663fb6c594e7a
116  0000000000000000000000000000100000  7dd9bfce9d1403933714ee4096c3441baf5af81b3f64e0f87cc1cc32fdcedf10
117  0000000000000000000000000000080000  0482c92fe917d69f2d868aa2c95489e1ee80ff71ef19f1cfce1377c174ffb01e
118  0000000000000000000000000000040000  0566fe42a313157093e62b3d84be1a8075dcc2937aa578d8a29ca24be0795a0e
119  0000000000000000000000000000020000  4c941bf490a70f050460d94f0b7bac0c4c4c668471ba43c215905e9c2be91d53
120  0000000000000000000000000000010000  666d75b0d8624715dddb57d7397111a0d1caab7d0fbc3897fdc86cf3788739ad
121  0000000000000000000000000000008000  1cc4bce66af3df7a46e4bdff94767f2ca051adb75b75db2848c48506a014d1fa
122  0000000000000000000000000000004000  c0b67cd17c5e2ca90c0a64c7f1c754279722c1ba52e4104562c7e5fa603c2841
123  0000000000000000000000000000002000  423f40202eb8a352aaca8d0dd8a5e30ced302df8a174a72e53f66eee6c7c4671
124  0000000000000000000000000000001000  608b2a875605d09d6a0324963cc682108ecc5838afd07e8ae4a4d4aabcd7d605
125  0000000000000000000000000000000800  95722a0c8ad6ad967931054643947e0d7777b1c290ee91811d07c38fef808aad
126  0000000000000000000000000000000400  737a6b37f885cc9af3324f802d24b4ed79bb0a4d1754da54277774054c8324bf
127  0000000000000000000000000000000200  9b7e0e78788c7caffbba04830759456a4307a1a3dd63b650ec56491e32c44b28
128  0000000000000000000000000000000100  0fb9ba49fbcb6410bc029afaab8233b615a469db9988ab33df77c9c3b75ac261
129  0000000000000000000000000000000080  44fb57fda37cf59e6a7a0ff4b4ef32ed5dea7d9b2251fac9b046bf6008f0e166
130  0000000000000000000000000000000040  b063a00f1dc8ed4c157ada0113fcf9b65196595cf3f672b0dcc706d30665d813
131  0000000000000000000000000000000020  dd93aa87447e4db69a89bfe0ef7c37f0db56b735c6983a55ef48ab076bbceda4
132  0000000000000000000000000000000010  2764272491fbd336a76fcfae0daaf704a440a831361b0acbc3a96aa4b3f5f59c
133  0000000000000000000000000000000008  653f54f79ee8067201bf4b0c483a8f276bf2e311b5bfabc882128481294a0d46
134  0000000000000000000000000000000004  3f3b5350c72c1221273c0efcc0744c5fca4651569fa66dd1fc4bd25ecd38c5d8
135  0000000000000000000000000000000002  49da2f5ae81013292f5ebf186ee8b70d2f071028defceb4eb9a9c835aab9bda6
136  0000000000000000000000000000000001  8990536481c3e0c0f97aff0278a067f4c36d072d34e244075a27c60cba1034b0
```

**Measured alongside, none of it vendored:**

- **Optimisation level.** `frame_tx` built at `-O0`, `-O2` and `-O3` and
  the 137 renders made at each: 411 WAVs, the 137 digests identical level
  to level, so the reduced file is too.
- **`frame_rx` round trip.** Each of the 137 `-O2` renders decoded by
  `frame_rx` (built as above) back to its own word, 137/137. `frame_rx`
  checks the modem's CRC, not the DeModFrame's validity, so an invalid
  frame decodes.
- **Against the three WAVs.** `extrahe.py`'s `symbols` applied to the three
  vendored WAVs gives, for each, the stream a scratch model computes from
  `docs/design/modem.md` section 2 (the CRC, the mask-and-parity encoder,
  the gather interleaver, preamble and sync) — and that model computes all
  137 records of this file as well.
- **The reference's affinity, spot-checked.** Predicting a word's stream
  from this file as `S(0) ⊕ ⨁ (S(e_i) ⊕ S(0))` over its set bits and
  comparing with a render of it, reduced by the same rule (block map exact
  on every one): the three WAVs' frames and 1,000 random 17-byte words
  (Python's `random.Random(20260911)`), 1,003/1,003 equal. The 136
  difference columns `S(e_i) ⊕ S(0)` have rank 136 over GF(2), and none
  touches the 40 preamble and sync symbols. Evidence for the premise, not
  a proof of it: affinity is argued from the reference's source
  (`docs/design/modem.md` D9).

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
Vendoring the rendered WAVs and the reduced basis makes the program tests
that compare against them (`exsc`-compiled-and-run stdout,
`stdout=vendor/hydramodem-tx/<name>`) content-addressed by git instead.

## Status in this repository

The three WAVs are certified: `examples/hydramodem/`, HydraModem's
transmitter written in Exsecutor, writes each of them byte for byte
(`tests/programs/hydramodem_{loopback,exemplum,vacuum}/`, since `7ed75ca`).

`symbola_basis.bin` is certified too: `examples/hydramodem/basis.exsc`
runs the same pure `tonus` on the 137 words and writes these 48,772 bytes
exactly (`tests/programs/hydramodem_basis/`, milestone M2 of
`docs/design/modem.md`), in the commit after the one that vendored it.
The functions that decide those bytes are M1's, unchanged; the driver
only derives the words and writes.
