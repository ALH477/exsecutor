# Impaired inputs for the musical receiver — vendored as a certificate

Seven files, never edited: five WAVs, the generator that made them
(`impair.py`, standard library only), and `verdicta.tsv`, HydraModem's own
receiver's verdict on each. They certify `examples/hydramodem/auditus*.exsc`
and its drivers under noise and clock offset (`tests/programs/auditus_*/`,
`docs/design/melos.md` section 9). Like `vendor/hydramodem-rx/`, this is the
kind of evidence that records what the reference **judges**, not what it
**produces**.

## What each file is

Every WAV is `impair.py` applied to a clean reference render already vendored
here:

| file | made by | reference verdict |
|---|---|---|
| `melos-awgn-18.wav` | `impair.py awgn <melos>/d310123400a1ffffdeadbeef0a1b2ca961.wav … -18 1` | decodes |
| `melos-clock+3000.wav` | `impair.py clock <melos>/d310123400a1ffffdeadbeef0a1b2ca961.wav … 3000` | decodes |
| `melos-awgn-26.wav` | `impair.py awgn <melos>/d310123400a1ffffdeadbeef0a1b2ca961.wav … -26 3` | **no frame**: below the knee |
| `bicinium-awgn-18.wav` | `impair.py awgn <bicinium>/bicinium-d3101…a961-0000…0000.wav … -18 2` | both voices decode |
| `bicinium-clock+3000.wav` | `impair.py clock <bicinium>/bicinium-d3101…a961-0000…0000.wav … 3000` | both voices decode |

Here `<melos>` is `vendor/hydramodem-melos` and `<bicinium>` is
`vendor/hydramodem-bicinium`.

- **SNR** is wideband per-sample SNR over the non-silent samples, as
  Punctim's `hydramodem/docs/MUSIC.md` measures it. The noise is xorshift64* →
  12 uniforms − 6 (Irwin–Hall, unit variance).
- **Clock** is linear-interpolation resampling at a step of `1 + ppm·1e−6`.
  `bicinium-clock+3000.wav` is the input that caught a real bug: a first draft
  of the receiver asked for one symbol more than the reference needs, and at
  +3000 ppm the duet's bass is compressed just under that bound.

## The reference verdicts

These are Punctim's `hydramodem/dcf-tools/frame_rx --profile melody` and
`poly_rx`, built by `dcf-tools/build.sh` at Punctim `f86f5d5`: `libhydramodem`
at HydraModem 2.0.0, C reference DSP, `-ffp-contract=off`. The verdicts are
recorded in `verdicta.tsv`. This receiver matches each one: the frames where
the reference decodes, and exit 1 with nothing written where it does not.

The generator is deterministic. It is IEEE-754 double arithmetic in a fixed
order, integer xorshift, Python `round` (half to even), and int16 clipping.
Re-running it reproduced `melos-clock+3000.wav` byte for byte.

## Digests

Tree digest, from the repository root,
`find vendor/hydramodem-auditus -type f ! -name PROVENANCE.md | sort | xargs sha256sum | sha256sum`
under `LC_ALL=C` (`flake.nix`'s `auditus-vendor-integrity`):

```
ca4943786de91a3a8dcf0d4cd2a0839ef3044bb79032a54c2928e49235bfb106
```

Per file:

```
324e51fc8e5bd505c269a96ca372fe5cd6b32ffb284f058c71f27afb6e6caaed  vendor/hydramodem-auditus/bicinium-awgn-18.wav
8fe4201b69db466f42c4009b242e2dff5505f843b3472f5944bab19a779d5728  vendor/hydramodem-auditus/bicinium-clock+3000.wav
9d970c4fd2f662d36572e5f7c257d95afb6c0a9318e6baff79d2abb52b35b90a  vendor/hydramodem-auditus/impair.py
6be170a1716c5baeb0247e568f2fbedb214c71650483e854f43eba33602a9ac8  vendor/hydramodem-auditus/melos-awgn-18.wav
251415ef9570c06b330d0fc742f2ddd1c7892444d1bbe65fea56c8ac94fb43e1  vendor/hydramodem-auditus/melos-awgn-26.wav
5dd9a0445c99f1b1fc5779d2ad6c80480433df8424bf80278abaddec20707e32  vendor/hydramodem-auditus/melos-clock+3000.wav
4386bc680a7d77414fbe5804352d8a113c710eaf7202ffed98bb03da239bae90  vendor/hydramodem-auditus/verdicta.tsv
```
