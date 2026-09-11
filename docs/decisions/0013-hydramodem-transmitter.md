# 0013 — Adopt HydraModem's reference transmitter as the second external certificate

**Status:** Accepted, 2026-09-11. Vendored (`7633035`); **design only** —
no Exsecutor transmitter exists, and every claim below about one is
`[UNTESTED]` until `docs/design/modem.md`'s M1 runs.
**Relates to:** ADR 0011 (the first external certificate); spec §4.6, §5.2,
§5.4, §8.6, §9.3, §14; `vendor/hydramodem-tx/`, `docs/design/modem.md`.

## Context

§14 entry 23 certified that Exsecutor can express a wire format nobody
wrote for it: 246 vectors, and under an affinity argument all 2^108 frames.
That certificate is about seventeen bytes in memory. The next thing a
language for this project's domain has to do with a DeModFrame is put it
on a channel — and the channel HydraMesh actually uses between machines
with no network is sound: HydraModem, an M-FSK acoustic modem, C for the
framing and a reference C DSP mirrored by Faust, LGPL-3.0-only, the same
copyright holder.

Its transmitter is a better oracle than it looks. The default profile is
integer-exact — every tone is a whole number of cycles per symbol and the
sample rate a multiple of the baud — so the floating-point modulator's
output is, for that profile, a 48-entry integer table indexed by a counter
(`docs/design/modem.md` §2). A byte-identical WAV is therefore a
*reasonable* thing to demand of an implementation with no floating point,
which Exsecutor's reference backend is today. And the pipeline from frame
to symbol stream — CRC, convolutional code, interleaver, symbol map — is
affine over GF(2), the same property that made entry 23's basis argument
work, with a modulator that has no memory across symbols on top of it.

## Decision

**Adopt HydraModem's reference transmitter output as the second external
certificate**, vendored at `vendor/hydramodem-tx/`: three WAVs rendered by
`dcf-tools/frame_tx` built from HydraMesh `fce2813`, the reference DSP
(`src/hydra_dsp_ref.c`), the default profile. The first milestone of the
modem program is a transmitter, written in settled Exsecutor only, whose
stdout is compared with `cmp` against those files by `tests/programs/`.

Four things follow.

**1. It is vendored, not referenced, and not relicensed.** Three WAVs and
a `PROVENANCE.md` with per-file and tree digests under `LC_ALL=C`, the
build recipe, the compiler (`gcc 14.3.0`), the measured `-O0`/`-O2`/`-O3`
identity of all nine renders and the `frame_rx` round trip of all three —
the arrangement `vendor/hydramesh-wire/` and `vendor/fasmg-x86/` have. No
HydraModem *source* is vendored: the certificate is program output, kept
under the program's `LGPL-3.0-only` identifier as a matter of provenance,
not because a WAV is a derivative work anyone here wants to argue about.
**Do not apply `LICENSE.EXCEPTION` to these files.** A test that
regenerated them by compiling HydraModem at test time would depend on a
`cc` and a `libm` being present and agreeing, which §9.3 does not admit as
an input.

**2. The C reference, not Faust.** HydraModem ships two DSP backends behind
one interface. The Faust transmitter smooths its gain and DC-blocks its
output, and its adapter runs 8,192 warm-up samples so those settle
(`hydra_dsp_faust_tx.c:76-88`); its samples are not the reference's, and
the README's "identical loopback results" is a decoding claim. One
producer, one rounding chain — `double` sine, `float` cast, `float` gain,
`lround` — fully determined by the source at one commit, is what a
byte-identical certificate can be pinned to. Pinned to a commit, as ADR
0011 pins its files.

**3. What three frames prove, and what they do not.** Stronger than entry
23's certificate in one respect: the artifact compared is the whole
38,060-byte output, and a matching WAV is a WAV HydraModem's own receiver
decodes — no theorem stands between the test and interoperability. Weaker
in another: three frames are three points. The WAV is not affine in the
frame (a sample is a table lookup), so entry 23's basis argument does not
transfer to the audio *directly*. It transfers *indirectly*: the symbol
stream is affine in the 136 frame bits, and the modulator is a memoryless
per-symbol substitution whose two blocks any single frame's preamble
certifies — so a 137-word basis on the symbol stream, the second
milestone, would extend byte-identity to every 17-byte input, valid or
not (`modem.md` D9). Until then, the three frames certify the header, the
layout, the whole table, the preamble and sync, and spot-check the coded
path at 948 bits. Two blind spots are stated and measured rather than
discovered later: on a valid DeModFrame the modem's own CRC is always
`0x0000` (the frame already ends in the same CRC), so the three frames
cannot distinguish the CRC from a constant zero, and a tap confined to
those sixteen bits is invisible. The basis words of M2 are all invalid
frames with nonzero CRC, which is what closes both.

**4. It is a second language experiment, with a predicted result.** Entry
23 found that a real wire format needed no bitwise and/or, because
`@transitus` field access was the mask and shift machinery. The
transmitter's prediction (`modem.md` §1, §3): no bitwise and/or, no `/`, no
remainder, no signed arithmetic, no narrowing cast — bits by top-bit test
and `sursum 1`, the K=7 encoder as `aut` of tapped bits, the interleaver
and phase index as add-and-subtract walks, negative samples as `0 -% v`
in `u16` through a `u16:minor` view. And one thing it *does* want and the
language lacks: an array literal for a 48-entry table (`[OPEN]`, spec
§8.6), which the design writes as a nine-arm selection and records as the
first concrete evidence for array literals — the mirror of entry 23's
evidence against and/or. The decision on literals is deferred to a
milestone with three tables in hand, not taken on one.

## Consequences

**Positive**

- A second oracle nobody wrote for this language, one layer out from the
  first, and the first whose output a person can listen to.
- The purity claim gets a second real demonstration: the modulator is a
  pure module, the driver holds `ambitus`, the audit shows `write` and
  `exit_group`.
- The integer-exactness of the reference is now written down with its
  rounding chain and checked (`modem.md` §2, §11) — a fact about HydraModem
  that its own comments did not state.
- Four upstream findings worth reporting, none blocking: a stale "FEC off"
  header comment, a false aux-cable/Python interop claim, the always-zero
  modem CRC on valid frames, and the "everything is double" comment that
  the `float` chain contradicts.

**Negative**

- A third vendored tree, 114 KB of binary, to keep digest-checked and to
  re-vendor when upstream moves. Pinned to a commit.
- The certificate is three points until M2 vendors 137 more renders'
  worth of symbol streams; the design says so on every page it matters.
- Writing a table as control flow does not scale; the receiver will make
  that concrete and force a decision the transmitter can defer.
- The design found that `discerne` — parsed, typed, lowered on paper —
  traps in today's compiler when code is asked for (`modem.md` finding 1).
  Not fixed here; the transmitter is written around it.

**Neutral**

- No new `EXS-E` code. No spec edit: nothing the transmitter needs
  contradicts the spec, and the one contradiction found is between the
  compiler and its own lowering design.

## Open

- **Nothing is implemented.** `[UNTESTED]` — the design's worked example
  compiles in fragments (the table, the sample view, the bit extraction,
  the `u1` register step, the 38,060 writes), never as a whole.
- **The affinity of the Exsecutor symbol stream** is an argument from the
  code as designed (`modem.md` §7), to be tested by M2's basis and never
  proved by it — exactly ADR 0011's caveat, one layer out.
- **The receiver** needs a stdin reader under `ambitus` (a spec/prelude
  decision), signed multiply-accumulate, array creation, and possibly
  signed shifts — the `[OPEN]` items M3 will force (`modem.md` §9).
- **Other profiles** are limited to those where `sr/baud` is an integer;
  the 44.1 kHz variant HydraModem's README mentions is not, and the
  design does not claim it.
