# 0014 — The HydraModem receiver: certified by decode success against HydraModem's own verdicts, not by internal bit identity

**Status:** Accepted, 2026-09-11. **Design only; nothing implemented.**
`docs/design/receptor.md` is the design; a scratch integer model of it
decodes the three vendored WAVs, 137 further reference renders, and every
noise- and frequency-impaired input HydraModem's receiver decodes (177
inputs, section 11 there). The Exsecutor program, the reader, the array
literal and the vendored impaired set are all `[UNTESTED]`.
**Relates to:** ADR 0011 (the first external certificate), ADR 0013 (the
transmitter, the second); spec §3.1, §4.6, §5.4, §6.3, §8.6, §11, §12;
`vendor/hydramodem-tx/`; the proposed `vendor/hydramodem-rx/`.

## Context

ADR 0013 made HydraModem's reference transmitter the second external
certificate, and its output turned out to be integer-exact, so the
Exsecutor transmitter could be held to the WAV **byte for byte** — M1 and
M2 are. The receiver is the other half of the same modem and the other
half of the interoperability claim: a frame that goes out through
`examples/hydramodem/` should come back through it, and a WAV HydraModem
wrote should decode here.

A receiver is not integer-exact. HydraModem's is `double` throughout —
oscillators from `cos` and `sin`, energies, a normalised soft metric with
a division, a timing loop with the constants 0.15, 0.2 and 0.5 tuned
"jointly" for two sweeps (`hydra_modem.c:28-41`). Its internal state is
not a fact about the channel; it is one implementation's arithmetic. And
its *output* is a 17-byte frame or a refusal. Two things the transmitter
did not need are needed here and are the receiver's language cost: a way
for a program to read standard input, and a way to create an array — the
`[OPEN]` item ADR 0013 deferred to "a milestone with three tables in
hand".

## Decision

**1. The receiver is certified by its verdicts, not its arithmetic.** The
certificate is: the three vendored WAVs decode to their frames; a
transmitter-to-receiver loopback of all 137 basis words and the three
frames round-trips, in one process, through the transmitter's own `sona`;
and — the external half — on a **vendored set of impaired WAVs, the
receiver decodes every one HydraModem's `frame_rx` decodes and never
writes a frame that is not the input's**. What is *not* certified: the
origin, the score, the energies, the path metrics, the timing estimate.
The design is thereby free to compute in integers with a 7-bit oscillator
table (which is exactly orthogonal between the two tones on an aligned
window, a property the design proves), a soft metric that is the signed
energy difference with no division (argued the sounder metric: it weighs a
faded symbol less, as the likelihood does), a per-frame power-of-two
scaling, one decision bit per trellis state, and loop constants of 1/4 and
1/2 where the reference has 0.2 and 0.5. Every one of those would fail a
bit-identity certificate and none of them is observable in a frame.

**Why this and not bit identity.** Bit identity was right for the
transmitter because the reference's bytes *are* the specification of
what a receiver must accept, and because they happened to be integers.
A receiver's internal state is neither: two correct receivers disagree
on it by construction, and a certificate pinned to one implementation's
`double`s would certify the `libm`, not the modem. Decode success is what
interoperability means, and it is what the reference's own tests check
(`tests/test_loopback.c`: `memcmp(tx, rx, 17)`). The cost, stated in
`receptor.md` section 6 as four **negative controls** — mutants that must
pass: a one-bit sync change (the reference tolerates three misses), a
threshold of 36 for 37, the plateau's first origin for its centre, a
one-off in the table. Decode success cannot see them; the design says so
rather than pretending the certificate is stronger than it is.

**2. The impaired set is vendored with the reference's verdicts, in
`vendor/hydramodem-rx/`**, on `vendor/hydramodem-tx/`'s provenance
discipline (program output, digests under `LC_ALL=C`, the commit, the
recipe; no source; not relicensed, no exception applied). Thirty-five
files, 1.33 MB: white noise at 6, 0 and −6 dB on three frames and two
seeds (the reference decodes all), at −9 dB (it decodes none — the
receiver must not write a frame), sample-clock offsets to ±3000 ppm,
frequency offsets to +300 Hz (the reference fails +300 by its timing
loop, measured, and the design's R2 model decodes it — the certificate
is one-directional and allows that). The generator is integer arithmetic
throughout — a xorshift64 and an Irwin–Hall sum for noise, integer
interpolation for the clock — so every byte is re-derivable from the
three clean WAVs and the integers in the table, with no `libm` in the
recipe; the frequency offsets, which no integer transform can make
without an image, are rendered by the reference's own DSP with the tones
displaced, by a forty-line C program printed verbatim in `PROVENANCE.md`.
Every impairment level is **unanimous** in the reference's verdict, so
the certificate never sits on the reference's own cliff, where two
correct receivers are expected to differ seed by seed; the cliff itself
(between −6 and −9 dB, and it is acquisition's) is reported as a
measurement. It is a separate tree from `hydramodem-tx/` because it is a
different kind of evidence: what the reference produces, and what it
judges.

**3. Two language amendments, both `[UNTESTED]`, both in spec.** A stdin
reader, `Lector.ab_introitu(a: ambitus)` and `l.lege_octeto() -> u16`
with 256 for end of input — the mirror of `Scriptor.ad_exitum` and
`scribe_octeto`, under `ambitus` where §4.6 places the streams, adding no
syscall (`read(0)` was already in the atom's set), provisional as
`scribe` is until `eventus` has syntax. And array literals, `[e1, …, en]`
and `[e; N]`, in operand position, typed from the expectation or the
first element, `N` part of the type, no implicit zero-fill — the decision
ADR 0013 deferred, taken now with the three tables in hand (the
transmitter's sine, the receiver's cosine which is also its sine) plus
the buffers that turned out to be the stronger reason. The spec is
edited for both, with the reason in the commit message, and every place
that restated array literals as `[OPEN]` is updated.

**4. The names, as instructed, and a finding on them.** `Lector`,
`lege_octeto` and `ab_introitu` are kept — another agent is building
against them — and `receptor.md` finding 1 records that neither
`ab_introitu` nor the existing `ad_exitum` decomposes under §3.1 (a bare
preposition before the `_`), and that `ad_exitum`'s qualifier is an
accusative where §3.1 asks for an ablative and `ab_introitu`'s is the
ablative it asks for. The lexicon pass is disabled; the question is
`lexicon.norma`'s.

## Consequences

**Positive**

- The loop closes: a frame out through one Exsecutor program and back
  through another, and the reference's receiver becomes an oracle in the
  direction the transmitter's certificate could not use.
- A receiver with **no floating point, no division, no bitwise and/or, no
  signed shift** (R2), with every bound proved from `|x| ≤ 32768` and
  `|T7| ≤ 127`, and a model that matches the reference on 177 inputs.
- The two amendments are small and shaped by a program that needs them,
  which is how ADR 0011 and 0013 wanted amendments made.
- Findings worth reporting upstream: the reference's origin is 959 on a
  clean frame (a boundary tie, harmless); its reader ignores the sample
  rate; at +250 and +300 Hz its timing loop reacts to a frequency offset
  it was not designed for and at +300 walks off the frame.

**Negative**

- Four blind spots the certificate states and cannot close at R2; two of
  them (the plateau centre, the threshold) are predicted to become visible
  under R3's clock-offset vectors and one (the table one-off) never.
- A fourth vendored tree, 1.33 MB, to digest-check and to re-vendor when
  upstream moves. Pinned to the same commit as the transmitter's.
- Nothing runs. The reader, the literal, the 640 KB struct of prefix
  sums, an index store through a field, `i64` multiplication from source,
  two `[OPEN]` casts the design leans on (finding 13) and the harness's
  `stdin=` key are each somebody's work before R2 can be measured.
- R3's timing loop wants a signed division by a power of two, which is a
  signed shift (`[OPEN]`) or a sign-and-magnitude workaround; decided at
  R3 with the vectors, not here.

**Neutral**

- No new `EXS-E` code. Every array-literal diagnostic is one of six
  existing classes (`receptor.md` section 4); the receiver's exit codes
  are values, not diagnostics.
- The transmitter is untouched. Replacing its `discerne` table by a
  literal is M5, certified by the M1 WAVs and the M2 basis staying
  byte-identical.

## Open

- **Everything is unimplemented.** R2's tests, the prelude fixture, the
  three array-literal fixtures and `vendor/hydramodem-rx/` do not exist.
- **The soft-metric argument** (`receptor.md` D7) is an argument; the
  sweep found no input on which the difference and the ratio disagree,
  which is consistent with it and not a test of it.
- **Whether decode success can ever see the plateau refinement** — the
  design predicts the clock-offset vectors will, and records it as a
  prediction.
- **The names** (decision 4): a rule for associated constructors, or
  different names, when `lexicon.norma` exists.
- **Other profiles** (M4) and the transmitter's table as a literal (M5):
  sketched in `receptor.md` section 9, with what language each needs —
  nothing beyond this ADR's two amendments.
