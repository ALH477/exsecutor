# 0014 — The HydraModem receiver: certified by decode success against HydraModem's own verdicts, not by internal bit identity

**Status:** Accepted, 2026-09-11. **R2 and R3 both implemented and running.**
`docs/design/receptor.md` is the design. The Exsecutor program is
`examples/hydramodem/{receptor,recipe,circuitus}.exsc`, certified by
`tests/programs/receptio_*` — the three WAVs decode, 140 words round-trip,
and all nine mutants of section 6 behave as predicted (section 12 there).
Decision 1's **impaired set** and decision 2's `vendor/hydramodem-rx/` are
no longer `[UNTESTED]`: seventy vectors are vendored with HydraModem's own
verdict on each, `mollia` carries D10's timing loop, and **the receiver
decodes all 62 the reference decodes and never writes a frame that is not
the input's** (section 13 there, with the tallies, three new mutants and
findings 24-29). After R2, M5 (`76ca763`) made the transmitter's table a
literal with every receiver certificate byte-identical. Decision 1's `Praefixa` did not survive the emitter at R2 —
four arrays instead, receptor.md finding 20 — which changes no verdict and no
bound; the emitter defect has since been fixed and the struct now compiles
(finding 28), and converting the receiver to it is a named follow-up.
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
recipe; no source; not relicensed, no exception applied). **Seventy files,
2.67 MB** where this ADR planned thirty-five: white noise at +12, +6, 0, −3
and −6 dB on three frames and two seeds each (the reference decodes all 30),
at −12 dB (it decodes none — the receiver must not write a frame),
sample-clock offsets to ±3000 ppm on all three frames, frequency offsets to
±300 Hz (the reference fails both by its timing loop, measured, and this
receiver decodes +300 — the certificate is one-directional and allows that).
The refusal level is −12 dB and not −9 because −9 dB is **not unanimous**:
the reference decoded one of six there, which is its own cliff. The generator is integer arithmetic
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

- Four blind spots the certificate states and cannot close at R2. The
  prediction that two of them would become visible under R3's clock-offset
  vectors is **falsified**: the plateau-edge mutant decodes 62 of 62 with the
  timing loop and 55 of 62 without — exactly the unmutated R2 receiver's own
  score (receptor.md finding 26). All four stand. R3 added one the design did
  not have: the EMA weight **is** visible (finding 24), and the transition
  gate is visible only on the *clean* channel (finding 25).
- A fourth vendored tree, 2.67 MB, to digest-check and to re-vendor when
  upstream moves. Pinned to the same commit as the transmitter's, with its
  own flake check (`rx-vendor-integrity`).
- Seventy new test directories, one per vector, and 22.5 s on `tests/run.sh`
  (measured: 1 m 49 s to 2 m 12 s). A per-file verdict is what this
  certificate is, and one directory per file is what "per file" means.
- ~~Nothing runs.~~ The reader, the literal, an index store through a field,
  `i64` multiplication from source, the two casts of finding 13 and the
  harness's `stdin=` key all run as of R2. ~~**The 640 KB struct of prefix
  sums does not and cannot**~~: at R2 a struct literal's aggregate field was
  `copy`d and the emitter unrolled a `copy n` into n/8 instructions, so the
  compilation arena trapped (receptor.md finding 20, with the sweep). Four
  arrays passed one per parameter cost nothing — no call needs more than six
  words — and the finding is reported for the backend's owner rather than
  worked around in the emitter. **Fixed since, by `9ede8bf`**: `Praefixa`
  compiles and travels as one word (finding 28). R3's timing loop, which
  needs the sample count as well as the origin, therefore carries `o` and `n`
  packed in one `mensura` — a workaround the struct would retire, and the
  conversion is a named follow-up rather than something smuggled into R3.
- R3's timing loop wants a signed division by a power of two. **Decided with
  the vectors, as this said it would be: sign-and-magnitude on `u64`, and the
  signed shift stays `[OPEN]`** — all 24 clock-offset vectors decode without
  it, which is evidence against needing it rather than for.

**Neutral**

- No new `EXS-E` code. Every array-literal diagnostic is one of six
  existing classes (`receptor.md` section 4); the receiver's exit codes
  are values, not diagnostics.
- The transmitter is untouched by this ADR. Replacing its `discerne` table
  by a literal is M5, certified by the M1 WAVs and the M2 basis staying
  byte-identical — done since, `76ca763`, 4/4 and the five `receptio_*`
  directories unchanged.

## Open

- ~~**Everything is unimplemented.**~~ ~~`vendor/hydramodem-rx/` and
  everything R3 does not.~~ R2's five test directories, the prelude fixture,
  the array-literal fixtures, the seventy `receptio_vec_*` directories and
  the timing loop all exist and run.
- **The soft-metric argument** (`receptor.md` D7) is an argument; the
  sweep found no input on which the difference and the ratio disagree,
  which is consistent with it and not a test of it.
- **Whether decode success can ever see the plateau refinement.** The design
  predicted the clock-offset vectors would; they do not, and the reason is
  the loop itself — starting 21 samples early, it rails the offset to +2 and
  catches up in about twenty symbols (receptor.md finding 26). No candidate
  vector is in sight, and the blind spot is now expected to be permanent.
- **Whether 1/4 is the right EMA weight**, as opposed to one inside the set
  that passes. 1/2 loses four vectors; nothing here says 1/4 is best.
- **The names** (decision 4): a rule for associated constructors, or
  different names, when `lexicon.norma` exists.
- **Other profiles** (M4): sketched in `receptor.md` section 9, with what
  language it needs — nothing beyond this ADR's two amendments. The
  reference renders for four more profiles are vendored (`6121656`,
  `vendor/hydramodem-tx/profiles/`); no Exsecutor program reads them yet.
  ~~The transmitter's table as a literal (M5)~~ — done, `76ca763`.
