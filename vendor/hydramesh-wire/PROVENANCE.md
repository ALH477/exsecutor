# HydraMesh DCF wire quantum — vendored reference

Two files, copied verbatim, never edited. They are **reference data**, not code:
a normative format specification and a finite golden-vector certificate.

| file | sha256 |
|---|---|
| `WIRE_QUANTUM_SPEC.md` | `75640afc45184d9f003e0769d2ffb2205e82b223bd2c0da7d0bb57e18b36d798` |
| `golden_vectors.json` | `8d2b0e63c80826008b5f26e2434b1c972da424afe8b3d028245a5dcd8fba2056` |

Tree digest, `find . -type f ! -name PROVENANCE.md | sort | xargs sha256sum |
sha256sum` under `LC_ALL=C`:

```
770382f1f32b672fdee15a0d54d7231ba38729dd01c5494ec75c29f39a62d5e5
```

Computed **from the repository root**, not from inside this directory —
`sha256sum` embeds the path it was given, so the two differ. `vendor/fasmg-x86`
uses the same convention and `flake.nix`'s integrity checks assert it. Getting
this wrong is how the first recorded value here was wrong; the check caught it.

`LC_ALL=C` is pinned for the reason `vendor/fasmg-x86/PROVENANCE.md` pins it:
`sort` collation is locale-dependent, and a digest that changes with the
developer's `LANG` is not an integrity check. This project's thesis is that
locale is never ambient state; its own checks are held to it.

## Source

- Upstream: `https://github.com/ALH477/HydraMesh`
- Local origin: `/home/asher/Documents/HydraMesh`
- Commit: `fce2813f85ac17e29f34fa1adf4008056b116318`
- Paths: `Documentation/WIRE_QUANTUM_SPEC.md`, `Documentation/golden_vectors.json`

## Licensing

`LGPL-3.0-only`, © DeMoD LLC — the same copyright holder as this repository.

**These files are vendored, not relicensed.** They keep their own licence, the
same arrangement `vendor/fasmg-x86/` has with BSD-3-Clause. LGPLv3 does permit
conveying under GPLv3, and as sole copyright holder DeMoD LLC could relicense
at will, but neither is necessary: nothing here is linked into `exsc`, and
leaving the licence untouched keeps provenance legible. Do not add this
repository's `LICENSE.EXCEPTION` to these files.

## Why it is vendored rather than referenced

Same reason as `vendor/fasmg-x86/`: a build or a test that reaches outside the
tree is not reproducible. §9.3 requires byte-identical output from
(source, ego, lockfile, flags), and "whatever was in a sibling directory that
day" is none of those. Content-addressing by git is the mechanism.

## What it is

A 17-byte `DeModFrame`, big-endian throughout, no padding, CRC-16/CCITT-FALSE
over bytes `[0..14]`. Certified byte-identical across eleven language
implementations.

The certificate is why it is here. Encoding is affine over GF(2), so a finite
basis of 109 encode vectors and 137 syndrome vectors proves agreement on **all
2^108 frames** and identical classification of **all 2^136 words**. Complete
finite test corpora for a real production format are rare; this project's
method is that prose designs are hypotheses until code runs, and this is a
ready-made oracle.

## Status in this repository

`[UNTESTED]` as an Exsecutor artifact. No Exsecutor implementation of this
format exists, because there is no Exsecutor compiler. What has been run is
`prototypes/wire/`, which checks the format against these vectors and asks
whether §5.2's `@transitus` can describe it. See that directory for the answer.
