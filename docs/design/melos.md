# The musical modem — HydraModem's `melody`, `bass` and duet in Exsecutor

Status: **implemented: the melody transmitter (sections 1–7), the bass voice
and the two-voice duet (section 8), a receiver for all three (section 9), and
that receiver streaming (section 10).** `examples/hydramodem/melos*.exsc`
writes HydraModem's melody-profile WAV byte for byte on the two vendored
inputs (`tests/programs/melos_{loopback,nihil}/`, 481,964 bytes each) and its
symbol stream on all 137 basis words (`tests/programs/melos_basis/`,
137/137). All three agree under the reference backend and under the C backend
built by gcc and clang at `-O0` and `-O2`. The receiver is section 9.

The reference is Punctim's `hydramodem/` at
`5c6a4e11f50d3f0453c2f3a593fe98469afae257`. It is read-only and never linked.
The profile and its music theory are its `hydramodem/docs/MUSIC.md`, and the
synthesis this program repeats is that file's "Exact synthesis" section and
`music_render` in `hydramodem/src/hydra_modem.c`. Reference output is vendored
at `vendor/hydramodem-melos/`, whose `PROVENANCE.md` has the recipe, every
program that produced a vendored byte, and the digests. `modem.md` is the
default-profile transmitter this one sits beside, and it is reused, not copied.

## 1. What the profile is

The receiver HydraModem uses is exact only when every tone completes a whole
number of cycles per symbol, `f = h · baud`. The set of such tones is the
harmonic series of the baud, and small-integer ratios between harmonics are
just intonation. The melody profile picks the harmonics that spell a major
pentatonic scale:

- 25 baud (1920 samples a symbol), 8-FSK on harmonics 24 27 30 36 40 48 54 60
  of 25 Hz (600–1500 Hz).
- Scale degrees Gray-mapped onto symbols, so pitch-adjacent notes differ in one
  bit.
- A 12-symbol preamble alternating tonic and octave (symbols 0 and 7).
- A drone on harmonics 12 and 18 (300 and 450 Hz), at 0.12 each, orthogonal to
  every data correlator.
- A 10 ms raised-cosine attack and release rendered outside the symbol body.
- The CRC, convolutional code and interleaver are the default profile's.

## 2. What made a byte-exact port possible: the reference was changed first

HydraModem's first melody transmitter ran its floating-point DSP oscillator.
That oscillator adds `f/48000` to a binary64 phase every sample, and 600/48000
is not a binary fraction, so the phase drifted over 240,000 samples. A port
would have had to repeat that drift, and libm's `sin` at every sample.

The reference was changed instead (Punctim `5c6a4e1`), because the drift was a
defect of its own. Every tone and drone completes whole cycles in a symbol, so
every phase is exactly `k/1920` of a cycle. `music_render` keeps integer phase
counters and reads one quarter-wave table, computing libm `sin` only for
`k ≤ 480` and folding the rest by symmetry. The ramp was set to 10 ms so its
raised cosine reads the same table (`4R = L = 1920`). The library is built with
`-ffp-contract=off`, so an FMA cannot change a bit. The waveform is now a
normative function of 481 binary64 values and a fixed order of IEEE-754
operations, which this program repeats.

## 3. The program

| file | what it is | authority |
|---|---|---|
| `melos.exsc` | the layout, the quarter-wave table, `melos_sinus` (the fold), `melos_vox` (one sample), the envelope, the Gray note table, `melos_tonus` (the symbol stream) | none: pure |
| `melos_emitte.exsc` | walks the layout with the three phase counters and writes every byte | the `Scriptor`'s |
| `melos_loopback.exsc`, `melos_nihil.exsc` | one `initium` each, one frame | `Mundus` → `ambitus` |
| `melos_basis.exsc` | the 137 words' symbols, one byte each | `Mundus` → `ambitus` |

`quantum.exsc` (the frame, `redundantia`) and `modulator.exsc` (`Caput`,
`Exemplum`, `codificatum`, `intertexe`, `praeambulum`, `synchronia`) are
compiled into every unit and used unchanged. The code and interleaver do not
depend on the tone count, and `melos_tonus` packs their bits three to a symbol.

Every sample is `melos_vox(q, a, b1, b2, e)`: `v = gd·sin(a)`, then
`v + 0.12·sin(b1)`, then `v + 0.12·sin(b2)`, with `gd = 0.9 − 2.0·0.12`
written as the reference computes it. That is followed by `(e·v) sicut f32`,
back to f64, `·32767.0`, and the WAV writer's `lround` (truncate, then add one
if the exact fraction is ≥ 0.5, symmetric about zero). The carrier's counter
advances before its sample and each drone's after, as in the reference.

It uses no integer division, remainder, bitwise and/or, or signed integer,
following `modem.md`'s discipline. A wrap is one subtraction, because every
step is below 1920, and a negative sample is `0 -% m`.

A frame renders in 0.2 s (one run, 481,964 one-byte writes); the basis takes
0.04 s.

## 4. Finding: a binary64 cannot be written as a literal

Spec §8.4 caps a float literal at fifteen significant digits (`EXS-E0308`),
but a binary64 can need seventeen to round-trip. Of the table's 481 values,
435 change if rounded to fifteen digits (measured), so they cannot be
written as literals. The program
writes each value `Q[k]` as the integer `M[k] = Q[k]·2^61` instead. Every
`Q[k]` is a multiple of 2⁻⁶¹, since its smallest nonzero entry is above 2⁻⁹,
and each `M[k]` has at most 53 significant bits. So `M[k] sicut f64` is exact,
dividing by 2⁶¹ is exact, and the table is rebuilt bit for bit once per WAV.

This is a workaround, and the spec is not amended here. Any program that must
reproduce another implementation's binary64 constants meets the same limit.
Either a hexadecimal float literal or a documented bit-cast would remove it.
That is **[OPEN]**, and it belongs to the spec's owner.

## 5. What the certificate covers

The claim is that the WAV is byte-identical to the reference's for every
17-byte input, given the two structural facts below. Both are argued from the
code of both implementations, and the first was also measured.

1. **The audio is a function of the symbol stream alone, in a fixed way.**
   Every tone and drone completes whole cycles in 1920 samples. So the three
   phase counters hold the same values at every symbol boundary, and a body
   symbol's 1920 samples depend only on its note.
   - Measured on the reference: across the 372 body symbols of three valid
     frames, every two symbols on the same note were sample-identical, and all
     eight notes occurred.
   - The attack always plays note 0 (the preamble's first symbol).
   - The release plays the last symbol, whose bits are coded bit 297 and two
     pad bits, so it is note 0 or 4.
   - The loopback WAV checks all eight body blocks, the attack, and release 0.
     The zero word (nonzero modem CRC) checks release 4. Every valid frame
     ends on 0.
2. **The symbol stream is affine over GF(2) in the word**, symbol by symbol, as
   `modem.md` D9 argues for the default profile. A symbol's three bits are
   three coded bits, each affine, or constants. So the 137-word basis
   determines every input's stream.

The two other valid frames of `vendor/hydramodem-tx/` were also rendered and
compared once, byte-identical, and are not kept (`PROVENANCE.md`).

## 6. Negative controls

Thirteen mutants were applied one at a time, and each ran against both WAVs and
the basis. These **fail**, as they should:

- the Gray table (symbols 2 and 3 swapped)
- the preamble's upper note (7 → 4)
- the second drone's harmonic (18 → 20)
- the drone gain (0.12 → 0.11)
- the carrier advancing after its sample
- skipping the f32 rounding
- truncating instead of `lround`
- the attack index off by one
- the release index off by one
- the table fold mirrored at 959 instead of 960, which traps: SIGILL on the
  checked subtraction

The basis alone does not catch the Gray-table, drone, rounding or envelope
mutants, and should not, since those are downstream of the symbols. The WAVs
catch all of them.

These **survive**, and the certificate cannot see them:

- **`melos_synchronia`'s guard `ge 16` → `ge 17`.** An equivalent mutant:
  `synchronia` already returns 0 past bit 15, so the guard is belt-and-braces.
- **The two drones added in the other order.** Binary64 addition is not
  associative, but the ≤ 1-ulp difference never survived the f32 and 16-bit
  roundings on these inputs.
- **One table entry moved by 2 ulp** (`M[300] + 512`). A 16-bit sample does
  not see the last bits of a binary64. The table's exactness rests on section
  4's arithmetic, not on this test; the comment in `melos.exsc` says so.

## 7. Not done

- ~~**A melody receiver.**~~ Done: section 9. The prefix-sum design would have
  needed about 30 MB (44 MB for the duet); section 9 stores the samples instead
  and slides the correlator.
- **`chime` and `nocturne`**, the other musical presets. `nocturne` is the
  same shape with another table (minor pentatonic, one drone). `chime`'s
  `L = 480` is smaller than its `4R = 1920`, so its envelope is not the data
  table and needs a second one.
- **Other libms.** The reference's 481 values came from glibc. Whether another
  libm's `sin` gives the same 481 values is **[UNTESTED]**; if it does not, it
  is the reference that moves, and this program stays pinned to the vendored
  bytes.

## 8. The bass voice and the duet (bicinium)

Status: **implemented.**

- `bassus.exsc` is HydraModem's `bass` profile: 4-FSK at 25 baud on harmonics
  3 5 6 9 of 25 Hz (D2 B2 D3 A3), no drone, 178 symbols.
- `bassus_emitte.exsc` writes the bass alone.
- `bicinium_emitte.exsc` writes the **duet**: `hydra_modem_tx_poly` with
  `hydra_profile_duet`, two frames in one burst, frame A on the melody voice
  (gain 0.5, no drone) and frame B on the bass (0.4).

The reference is Punctim `3aeff9d`, and `vendor/hydramodem-bicinium/` holds its
output with the recipe. `tests/programs/bassus_loopback/` and
`bicinium_loopback/` compare the two 689,324-byte WAVs byte for byte, and
`bassus_basis/` compares hydra_frame_build's symbols on the 137 basis words.
All three agree under the reference backend and the C backend (gcc and clang,
`-O0`/`-O2`).

**What made the duet portable.** The reference sums voices into one float buffer:
the melody into zeros, then the bass added, so a sample is `(0 + m) + b` in f32.
`0 + m` is `m` because no contribution is `-0.0` (qsin's only zeros are `+0.0`),
and IEEE-754 addition of two operands is commutative. So `m + b` here is the same
bits whichever voice is written first. That is not a guess: the swapped order
survives as an equivalent mutant below.

**Layout.** The reference right-aligns voices by whole symbols. The bass is 178
symbols and the melody 124, so the melody enters 54 symbols in:

- Its attack is the last quarter of bass symbol 53 (samples 104,640–105,119).
- Its symbol `y` sits under bass symbol `y + 54`.
- The two releases coincide.

The writer walks the bass's 178 symbols and adds the melody where it sounds, so
no voice needs an offset counter.

**What the certificate covers.** Section 5's argument carries over: body
symbols depend only on their note, and the attack is always note 0.

- **Bass release:** the last bass symbol is two bits, so the release can be any
  of 4 notes.
  - The bass-alone WAV (a valid frame) checks note 0.
  - The duet, which puts the zero word on the bass, checks note 1.
  - Notes 2 and 3 **are not byte-checked**. They run the same code on another
    table entry, so they are argued, not measured.
- **Melody release in the duet:** note 0 (a valid frame). Its note-4 release is
  checked single-voice in `vendor/hydramodem-melos/`.
- **Melody entry:** the duet WAV checks where the melody enters and how the two
  voices add.

**Negative controls.** Nine mutants were run against both WAVs and the bass
basis. These **fail**, each where it should:

- the bass Gray table
- the bass preamble note (3 → 2)
- the bass sync bit order
- the bass-alone gain (0.9 → 0.89; only the bass WAV sees it)
- the duet's entry symbol (54 → 53)
- the duet's bass gain (0.4 → 0.41)
- dropping the melody's attack
- summing in f64 with a perturbation

One **survives**, as it must: the sum written `b + m` instead of `m + b`, an
equivalent mutant because two-operand addition is commutative.

**Refactor.** `melos.exsc` gained `melos_scribibilis` (the WAV writer's
quantizer, split out of `melos_vox`) and `melos_vox_simplex` (one voice with no
drone). The melody certificates were re-run and are byte-identical.

**Not done:** a duet receiver (section 7's receiver problem, times two voices),
duets of more than two voices, and `hydra:profile=duet`'s pairing rule, which is
a medium and lives in Punctim's Python.

## 9. The receiver (auditus)

Status: **implemented, certified by verdicts.**

- `auditus.exsc` is pure. It is HydraModem's `decode_window` for the melody and
  bass voices, and so for the duet.
- `auditus_lege.exsc` reads a WAV into the caller's array, through
  `&mutabilis` (ADR 0016).
- `melos_recipe.exsc`, `bassus_recipe.exsc` and `bicinium_recipe.exsc` are the
  stdin drivers:
  - exit 0 with 17 bytes, 17 bytes, or 34 bytes (melody first, then bass);
  - 1 no sync, 2 CRC, 3 a WAV they will not read;
  - **nothing written on any failure**, recipe.exsc's rule.

The reference is `decode_window` at Punctim `f86f5d5`. The receiver is held to
verdicts, not bytes, as `receptor.exsc` is (ADR 0014).

**Design: store the samples, slide the correlator.** `receptor.exsc` keeps a
prefix sum per (tone, I/Q), so any window is two subtractions. For 8 tones over
the duet's 344,640 samples that is 44 MB, against an 8 MiB stack that nothing
checks. This receiver instead:

- **Stores the samples once**, as f64: 2.76 MB, in the driver's frame, borrowed
  immutably from then on.
- **Integrates each window on demand**, at L = 1920 multiply-adds a tone.
- **Uses a sliding DFT for the acquisition scan**, which needs the argmax at
  every sample offset. Every tone completes whole cycles in L samples, so
  S(a+1) = S(a) + (x[a+L] − x[a])·w(a): one multiply-add a tone a sample.
  - It is recomputed exactly once every L samples, so float error never spans
    more than a symbol.
  - The argmax is kept as one byte an origin (345 KB).
  - An energy is invariant under a fixed phase rotation, so each window's phase
    reference is simply its own first sample.

Peak stack is about 3.2 MB, and a duet decodes both voices in 0.5 s (one run,
native).

**Kept from the reference, unchanged:**

- the known-prefix plateau and its centre;
- the ±L/2 fine refinement on the known prefix's energy, itself slid;
- the nknown − 3 threshold;
- the timing loop: ±2 search on total energy, 15 % gate, EMA 0.2, half the drift
  a symbol.

**Changed, and why:**

- **The soft bit** is max E(1) − max E(0) (receptor.md D7's choice), not the
  normalized form.
- **The Viterbi and the residue** are `receptor.exsc`'s `decodifica` and
  `residuum`, reused unchanged, so the soft bits are scaled to ±2^20.
- **One voice's decoder serves the duet.** Each voice is decoded independently
  from the same samples. They share a 25 Hz grid and a symbol grid, so each is
  invisible to the other's correlators. `auditus_bassus_ex_bicinio` shows it: the
  bass receiver, run alone on the duet, recovers its frame under the melody.

**Finding: the comparison caught a bug.** The first draft asked for
`total_syms·L + L` samples before scanning; the reference needs `total_syms·L`.
The two receivers were run on 48 impaired inputs (noise at −10 to −20 dB, clock
at ±1000 and ±3000 ppm, melody and duet). They disagreed once: the duet at
+3000 ppm, whose compressed bass burst is 343,608 samples, between the two
bounds. After the fix, 48 of 48 agree. That input is now vendored and tested
(`auditus_bicinium_clock3000`).

**Measured against the reference [UNREPRODUCED]** (C `frame_rx`/`poly_rx`,
Punctim `f86f5d5`, the same impaired WAVs fed to both, numpy-generated for this
measurement and not vendored, so the table below cannot be re-run from this
tree; only its one disagreeing input is vendored):

| set | inputs | verdicts identical |
|---|---|---|
| noise −10…−20 dB (4 each) and clock ±1000/±3000 ppm, melody and duet | 48 | **48** |
| the cliff: noise −21…−24 dB (5 each) and clock ±4000/±5000 ppm | 48 | 44 |

At the cliff the four splits go both ways: this receiver decodes 3 inputs the
reference loses, and the reference decodes 1 this one loses. That is trial-level
disagreement from the different soft metric, not a shifted knee. Both lose the
melody at −23 dB and below, both lose the duet's bass from about −21 dB, and
both keep the melody at ±5000 ppm.

**The certificate** (`tests/programs/auditus_*`, 11 tests):

- **Clean inputs**, the reference renders already vendored:
  - melody: the loopback frame and the zero word;
  - bass: the loopback frame;
  - duet: both voices; and the bass voice alone out of the duet.
- **Impaired inputs**, `vendor/hydramodem-auditus/` (a stdlib-only
  deterministic generator plus the reference's verdicts):
  - melody at −18 dB and at +3000 ppm;
  - the duet at −18 dB and at +3000 ppm;
  - melody at −26 dB, where the reference finds nothing: exit 1 and **nothing
    written**.
- **Negative:** the bass receiver on a melody-only burst: exit 1, nothing
  written.

**Negative controls.** Seven receiver mutants were run against the clean
melody, melody at +3000 ppm and −18 dB, and the duet at +3000 ppm and −18 dB.

These **fail**:

- the soft bit's sign: all five inputs;
- the sliding update's sign: all five;
- the threshold raised to nknown: the noisy duet only.

These **survive**, and are named rather than hidden, as receptor.md section 6
names its own:

- **The timing loop disabled** (the gate never opens). It survives here, and it
  survives on every clock input tried from ±1000 to ±8000 ppm, for both voices
  **[UNREPRODUCED]** (those inputs beyond the vendored +3000 ppm pair were not
  vendored). The reference itself fails from ±7000 ppm, loop or no loop. At 25 baud a
  frame's whole drift within the reference's working range is about half a
  symbol, and an integrate-and-dump window mostly on the right note still has
  the right argmax. **The loop is kept for fidelity with the reference; no
  certificate input can tell it from its absence.**
- **The plateau's first origin instead of its centre**, and **fine refinement
  switched off.** Both survive: receptor.md's surviving mutants, one profile
  on.
- **The once-a-symbol exact recompute removed** (slide only). Binary64 drift
  over 345k steps stays far below what changes an argmax. The recompute is
  cheap insurance, not a load-bearing step.

One more edge disagreement: the duet's bass at −6000 ppm decodes in the
reference and not here, loop or no loop. That is twice the bass's documented
±3000 ppm (MUSIC.md), and it is recorded, not chased.

**Not done:**

- ~~a streaming receiver~~: section 10;
- chime and nocturne (their tables would slot into `auditus_harmonicus`);
- voices at different bauds;
- any real acoustic channel.

## 10. Streaming (ausculta_fluxus)

Status: **implemented.**

`ausculta_fluxus.exsc` reads an unbounded stream on stdin and writes each frame
the moment it decodes:

    arecord -q -t raw -f S16_LE -r 48000 -c 1 | ./ausculta_fluxus

- **Input** is raw s16le at 48 kHz mono, or the same behind a RIFF/WAVE header.
  The header's format fields are checked (exit 3); its lengths are ignored,
  since a stream has none.
- **Output** is 17 bytes per decoded frame, in decode order. Every window is
  tried for both voices, the melody's frame first: a melody burst gives one
  frame, a bass burst one, a duet two.
- **Memory** is one window, 351,840 samples (2.8 MB, f64). Nothing older is
  kept.

**The segmenter** is `hydra_rx_push`'s, in its units (a sample over 32768):

- a noise EMA while searching;
- trigger at max(6·noise, 0.02);
- collect to `frame_len` (the longest voice plus 0.05 s + 4 symbols = 351,840),
  or until quiet below max(3·noise, 0.01) for more than 3 symbols;
- drop a silence-ended window shorter than the shorter voice.

**Finding: the reference streaming receiver lost frames sent back to back.**
Its window (body + margin) is longer than a melody burst plus the 60 ms between
bursts, so the window held the next burst's start, and after decoding it
discarded the whole window. Measured on Punctim's `hydra_rx_push`: **1 of 4**
melody frames at frame_tx's 60 ms gap, 4 of 4 only once gaps exceeded 120 ms.
The default profile was unaffected (8 of 8): its margin is about one gap. It is
fixed in both implementations the same way:

- **Consume through the frame, replay the rest.** After a successful decode,
  only the samples through the frame's end (its release included) are consumed.
  The rest of the window is replayed through the segmenter, in place; a replayed
  sample is written at an index no greater than the one it is read from.
- **The plateau is the first run.** Acquisition takes best-scoring origins only
  within one symbol of the first. A window holding two bursts would otherwise
  centre between their plateaus. One burst's plateau is never a symbol wide, so
  single-burst verdicts do not move. Re-measured **[UNREPRODUCED]** (the same
  unvendored inputs): the 96 impaired inputs of section 9 give the same verdicts
  as before, 48/48 and 44/48 with the same four cliff splits.

Punctim's `hydramodem/tests/test_music.c` [6] pins the C half: 4 of 4, failing
at 1 of 4 on the old code.

**Beyond the reference: truncated-burst recovery.** A click opens a window
early; background noise (σ = 150, about −47 dBFS) keeps the silence rule from
ever closing it; and a real burst starting inside it then runs off the window's
end. The reference, and this receiver without the fix, discard that window and
the burst with it.

Here, when a window decodes nothing, acquisition continues past the
complete-burst range for a known prefix (nknown − 3 matches) whose burst does
not fit, and the driver replays from a symbol before it. This runs only after
the reference's own scan has failed, so no verdict changes. It is **not** in
the C receiver; porting it is open.

**The certificate:**

- `auditus_fluxus_contiguus`: melody(A) ‖ duet(A, Z) ‖ melody(Z), back to back
  → A, A, Z, Z.
- `auditus_fluxus_ictus`: noise, a click, noise, bass(A), noise, as **raw**
  s16le → A.
- `auditus_fluxus_truncus`: a click, then melody(A) starting ~1,400 samples in,
  cut by a dropout that closes the window short of the longest voice; then
  bass(A) → A.

The streams come from `vendor/hydramodem-auditus/fluxus.py` (stdlib,
deterministic) applied to the vendored renders. Both mechanisms are proven
load-bearing: without replay the contiguous stream yields 2 frames, and without
truncated-burst recovery the ictus stream yields none.

**Finding (review): the first draft's replay start could underflow.** It
replayed from `truncus − 1920` and assumed a truncated burst starts at 9,120 or
later. That holds for a full window, but a window closed by silence (or flushed
at the end of the stream) can find the burst within its first symbol. Then `−`
trapped (§5.4, exit 132), or at exactly 1,921 it returned 1, the value the
driver also used for a failed write (exit 4, nothing wrong). A write failure is
now a flag in the segmenter's state rather than a return value.

Clamping the start to 1 was not enough. With only the clamp, the truncus stream
decoded correctly but took 11.8 s against 0.7 s for the ictus stream. The replay
re-closed the same window one sample shorter, about 1,400 times over. The root
cause is that a burst cut off by silence or by the end of the stream really was
cut, so replaying cannot recover it. Recovery now applies only to a **full**
window, the click case it was built for, where a truncated burst starts at
sample 10,081 or later. The clamp to 1 stays as a backstop.
`auditus_fluxus_truncus` traps on the first draft (exit 132, measured) and
passes now.

The review also found that a window completing *during* a replay overwrote the
replay cursor, dropping the rest of the replay it interrupted. The driver now
moves that remainder down behind the new replay. Its safety argument: writes
never pass reads. This path is argued, not exercised by a vendored input
**[UNTESTED]**.

**Also measured, not vendored [UNREPRODUCED]:**

- The contiguous stream at −16 dB: A, A, Z, Z.
- A stream of noise, a click, bass(A), melody(Z), duet(A, Z) and melody(A), with
  gaps of 0 to 1 s: all 5 frames in order.
- The contiguous stream (17.2 s of audio) takes 1.1 s, about 15× real time on
  one core (one run, native).

**Not done:**

- truncated-burst recovery in the C receiver;
- a duet streaming receiver in C (`hydra_rx_push` is single-profile);
- live audio on real hardware.
