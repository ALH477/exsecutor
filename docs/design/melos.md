# The musical transmitter — HydraModem's `melody` profile in Exsecutor

Status: **implemented, transmitters only: melody (sections 1–7), and the bass
voice and the two-voice duet (section 8).** `examples/hydramodem/melos*.exsc`
writes HydraModem's melody-profile WAV byte for byte on the two vendored
inputs (`tests/programs/melos_{loopback,nihil}/`, 481,964 bytes each) and its
symbol stream on all 137 basis words (`tests/programs/melos_basis/`,
137/137). All three agree under the reference backend and under the C backend
built by gcc and clang at `-O0` and `-O2`. There is no melody receiver.

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

- **A melody receiver.** `receptor.md`'s integer receiver keeps
  whole-frame prefix sums in stack arrays. At 240,960 samples and 8 tones that
  is about 30 MB, against an 8 MiB stack that nothing checks, so the design
  would need to change first.
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
