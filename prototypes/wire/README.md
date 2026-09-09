# `wire` — can `@transitus` describe a real wire format?

A design probe in spec §18's sense: an instrument for answering a design
question. Python, never shipped, never on the build closure.

```sh
python3 prototypes/wire/wire_probe.py
```

## The question

§5.2's `@transitus` annotation claims to make a struct wire-safe: byte order in
the type, no implicit padding, `:nativus` rejected. Its only worked example,
`Capitulum`, was written for the spec — every field a whole number of bytes,
every width one the language already had.

**A format nobody had to satisfy is weak evidence that an annotation is
sufficient.** So the probe tries one that already exists: the DCF DeModFrame
(`vendor/hydramesh-wire/`), 17 bytes, eleven independent implementations,
carrying a finite certificate.

## What it does, in order

**Phase 1 validates the probe, not the language.** It implements the format
from `WIRE_QUANTUM_SPEC.md` and checks it against all 246 golden vectors plus
three anchors. If this fails, phase 2 is analysis of a format the probe does
not correctly implement, and is not evidence — the probe says so and exits 1.

The certificate is worth understanding. Encoding is affine over GF(2), so 109
encode-basis vectors and 137 syndrome-basis vectors prove agreement on **all
2^108 frames** and identical classification of **all 2^136 words**. A complete
finite corpus for a real production format is a rare thing to be handed.

**Phase 2 asks the question.** It attempts the declaration using only the type
vocabulary §5.2 actually defines, and reports every field it cannot place.

**Phase 3 checks the proposed fix** rather than letting the spec assert one:
the extension must lay out to exactly 17 bytes with exactly 108 free bits and
no implicit padding at bit granularity.

## The result

**§5.2 as written could not express it.** Seven of nine fields placed; two did
not:

| field | why |
|---|---|
| `versio`/`genus` | two 4-bit fields share byte 1 — no sub-byte width in §5.2 |
| `tempus` | 24 bits wide — §5.2 has `u8`, `u16`, `u32`, `u64` |

Neither is fixable by choosing a different byte order. This is a falsification
of §5.2's completeness, produced by a format written by people who had never
heard of this language — which is the only kind of test of an interface claim
that is worth much.

§5.2 now carries bit-width fields, and phase 3 confirms the frame lays out to
17 bytes exactly. The rule that mattered was not "allow `u4`" but **"declared
widths must sum to a whole number of bytes."** An extension enforcing "no
implicit padding" only between bytes would have quietly reopened the
uninitialized-memory disclosure class that rule exists to close.

## Why nothing runs this automatically

It would be easy to make phase 1 a CI gate — it is a real test against a real
certificate, and an unrun check is worth very little. `prototypes/README.md`
forbids it, in terms that leave no room: *"Nothing under `prototypes/` ... may
be referenced from `Makefile`, the Nix build, or `compiler/`, or otherwise
become a build or test dependency."* A flake check for this probe was written
and then removed for that reason.

The rule is right and the temptation was a symptom. Phase 1 is not really a
design probe at all — it is conformance material wearing a probe's clothes, and
conformance material belongs in `tests/conformance/` as §14 entry 23, driving a
compiler. Until there is one, this runs by hand like every other prototype
here.

`vendor/hydramesh-wire/` *is* integrity-checked by the flake
(`wire-vendor-integrity`). That is vendored data, not a prototype, and the same
arrangement `vendor/fasmg-x86/` has.

## Status

`[UNTESTED]` as an Exsecutor artifact. Phase 1 is a real test and really
passes; phases 2 and 3 are analyses of a declaration no compiler has read,
because there is no Exsecutor compiler. When there is, §14 entry 23 is this
same certificate pointed at real output, and that is the version that counts.
