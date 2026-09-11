# examples/hydramodem/

A transmitter **and a receiver** for
[HydraModem](https://github.com/ALH477/HydraMesh)'s acoustic 2-FSK modem,
written in Exsecutor. Give the transmitter a 17-byte DeModFrame and it writes
the WAV file HydraModem's own reference transmitter writes for that frame —
**byte for byte**, all 38,060 bytes. HydraModem's receiver, `frame_rx`,
decodes those reference files back to their frames
(`vendor/hydramodem-tx/PROVENANCE.md`), so it decodes these. Give the
receiver one of those same reference WAVs on standard input and it writes the
seventeen bytes back out.

The DeModFrame codec (spec §14 entry 23) showed the language can express the
frame. This is the next layer out: the frame on a wire that is air — and,
since R2, the wire read back.

The two halves are certified differently, and `docs/decisions/0014-hydramodem-receiver.md`
says why. The transmitter is held to the reference's **bytes**, because a
transmitter's output is bytes and the reference's happen to be integers. The
receiver is held to its **verdicts** — the three vendored WAVs decode to their
frames, and 140 words round-trip out through the transmitter and back — because
a receiver's internal state is one implementation's arithmetic (HydraModem's is
`double` throughout) and two correct receivers disagree on it by construction.
What that buys is an integer receiver with no floating point, no division, no
bitwise and or or and no signed shift; what it costs is four **negative
controls** — mutants that decode anyway — which `docs/design/receptor.md`
section 6 names rather than hides.

## Build and run

From the repository root, inside `nix develop`:

```sh
make all
build/exsc aedifica --hospes x86_64-linux \
  examples/hydramodem/quantum.exsc \
  examples/hydramodem/modulator.exsc \
  examples/hydramodem/emitte.exsc \
  examples/hydramodem/loopback.exsc \
  -o loopback.asm
fasmg loopback.asm loopback      # the dev shell sets INCLUDE for the vendored fasmg tree
chmod +x loopback                # fasmg writes the ELF; it does not mark it executable
./loopback > loopback.wav
cmp loopback.wav vendor/hydramodem-tx/d310123400a1ffffdeadbeef0a1b2ca961.wav
```

`cmp` prints nothing: the files are identical. The four sources are one
compilation unit (spec §12). To render another frame, swap the last source
for `exemplum.exsc` or `vacuum.exsc`, or copy a driver and change its
frame. The program writes to standard output and exits 0 when all 38,060
bytes were written.

The receiver is the same shape with `receptor.exsc` and `recipe.exsc` in
place of `modulator.exsc`'s writer:

```sh
build/exsc aedifica --hospes x86_64-linux \
  examples/hydramodem/quantum.exsc \
  examples/hydramodem/modulator.exsc \
  examples/hydramodem/receptor.exsc \
  examples/hydramodem/recipe.exsc \
  -o recipe.asm
fasmg recipe.asm recipe && chmod +x recipe
./recipe < vendor/hydramodem-tx/d310123400a1ffffdeadbeef0a1b2ca961.wav | od -An -tx1
```

which prints `d3 10 12 34 00 a1 ff ff de ad be ef 0a 1b 2c a9 61` and exits 0.
It exits **1** when it cannot find the frame, **2** when the frame's CRC does
not check, and **3** when the WAV is not one it will decode — and on every one
of those it writes nothing at all: the receiver never writes a frame it has not
checked. `modulator.exsc` is in the unit because the receiver reuses its
`Caput` and its `Exemplum`, and nothing else of it.

## The files

| file | what it is | authority |
|---|---|---|
| `quantum.exsc` | `DeModFrame` (spec §5.2's declaration) and the CRC-16 HydraModem appends | none |
| `modulator.exsc` | the transmitter: the tone of each symbol, the samples each tone becomes, the WAV header | none: pure |
| `emitte.exsc` | walks that layout and writes every byte to a `Scriptor` | whatever the `Scriptor` carries |
| `loopback.exsc`, `exemplum.exsc`, `vacuum.exsc` | one `initium` each, holding one frame as a struct literal | `Mundus`, from which `ambitus` |
| `basis.exsc` | the certificate driver: no WAV, but the 356 tones of each of 137 words, one byte a tone — compiled with `quantum.exsc` and `modulator.exsc` only | `Mundus`, from which `ambitus` |
| `receptor.exsc` | the receiver: the Q7 oscillator table, the window energy, acquisition, the soft bits, the 64-state Viterbi, the residue | none: pure |
| `recipe.exsc` | reads a WAV from standard input, verifies its header, builds the prefix sums, writes the 17 bytes | `Mundus`, from which `ambitus` (`Lector` and `Scriptor`) |
| `circuitus.exsc` | the loopback certificate: 140 words out through `sona` and back through `receptor.exsc`, in one process | `Mundus`, from which `ambitus` (`Scriptor` only — it reads nothing) |

Only a driver names `Mundus`. `modulator.exsc` and `receptor.exsc` declare no
`poscit` and take no capability, so they are pure in spec §4.1 rule 6's sense:
they may compute, and they may not observe or touch the host. The compiled
transmitter's syscalls are `write` and `exit_group` and nothing else; the
receiver's add `read`; the test harness audits each binary for exactly that.

## What it does

The default profile (HydraMesh `hydramodem/src/hydra_profile.c`): 48 kHz,
1000 baud, tones at 2000 and 3000 Hz, one bit per symbol.

1. The 17 frame bytes, then their CRC-16/CCITT-FALSE, big-endian: 152 bits,
   most significant first.
2. A K=7 rate-1/2 convolutional code (generators `0x79` and `0x5B`) with
   six zero tail bits: 316 coded bits.
3. An interleaver: output bit `d` is coded bit `19·d mod 316`.
4. Symbols: 24 preamble symbols alternating 0 and 1, the sync word
   `0x2DD4`, then the 316 interleaved bits. 356 symbols.
5. Each symbol is 48 samples of a sine at its tone, scaled by 0.9. The
   oscillator completes a whole number of cycles per symbol, so the audio
   is exactly a 48-entry table of integers, indexed by a counter.
6. 960 samples of silence either side, a 44-byte WAV header, 16-bit
   little-endian samples.

What it does **not** use, because the language has not settled them: bitwise
and or or, division, remainder, signed arithmetic, array literals. None of
them is needed. A remainder is a counter walked and wrapped; a bit is read
by comparing with `0x80` and shifting; a negative sample is `0 -% v` in
`u16`; the sine table is a `discerne`. `modulator.exsc`'s comments say where
each piece comes from in HydraModem's source.

Array literals have since settled (spec §8.6) and the receiver is written in
them; the transmitter is **not** changed to match, because its certificate is
byte identity and a table rewritten is a table to re-certify. That is milestone
M5 in `docs/design/receptor.md` section 9, and `tests/programs/acies/` already
tabulates `sinus` as a literal and compares all 48 entries with the function.

## How it is checked

`tests/programs/hydramodem_loopback/`, `hydramodem_exemplum/` and
`hydramodem_vacuum/` compile these files, run each driver, and compare its
standard output with `cmp` against the WAV HydraModem's `frame_tx` rendered
for the same frame, vendored in `vendor/hydramodem-tx/` (its `PROVENANCE.md`
has the build recipe and digests). Every byte is compared: header, silence,
all 17,088 samples of signal.

What three frames prove, and what they do not, is set out in
`docs/design/modem.md`. The short version: the header, the timing, the sine
table and the whole symbol layout are fully checked, since every frame
exercises them; the CRC, the code and the interleaver are checked at three
inputs. A CRC replaced by the constant 0 would still pass those three,
because a valid DeModFrame always has a modem CRC of `0x0000` — the frame
already carries its own CRC, and a CRC over data followed by its own CRC is
zero.

`tests/programs/hydramodem_basis/` closes that. `basis.exsc` runs the same
`tonus` on 137 words — seventeen zero bytes, then each word with exactly
one of the 136 bits set — and writes their 48,772 tones, which `cmp`
compares with the tones of HydraModem's own renders of the same 137 words
(`vendor/hydramodem-tx/symbola_basis.bin`). None of those words is a valid
frame, so the CRC is exercised on every one, and the constant-0 CRC now
fails, at the first word. Because every step from the frame's bits to the
tones is an exclusive-or, a bit placement or a permutation, the tones of
any word are those of the zero word with the differences of its set bits
folded in; so agreeing on these 137 words is agreeing on every one of the
2^136 possible inputs — and since each symbol's audio depends on its tone
alone, which the three WAVs check, on every byte of every WAV. That
"because" is read off the code, not measured; `docs/design/modem.md` D9
says exactly what it rests on.

## What the receiver does

The same profile, read backwards. Every quantity is an integer with a proved
bound; `docs/design/receptor.md` D5 has the proofs and this file's comments
carry them.

1. The 44-byte header is read into a buffer and the buffer is read *as* a
   `Caput` — the transmitter's own struct, in the other direction — and then
   every field is checked. HydraModem's reader walks chunks, takes channel 0
   of any channel count and ignores the sample rate, so it would decode a
   44.1 kHz recording of a 48 kHz frame at the wrong tone frequencies and
   reject it by CRC. This one refuses it (exit 3).
2. Each sample is signed through `Exemplum` and −65536, multiplied by the two
   local oscillators' `cos` and `sin`, and accumulated into four prefix sums.
   The samples themselves are never stored. Both oscillators are one table,
   `T7[m] = round(127·cos(2π m / 48))`: the tones are 2 and 3 whole cycles a
   symbol, so their phase is a whole number of 48ths, and `sin θ = cos(θ−π/2)`
   is the same table 36 entries on. Thirteen values and two quarter-wave
   identities — a table computed in floating point is a *different* table, at
   index 16, and that is why this one is written down.
3. A window's energy is `(ΔPI)² + (ΔPQ)²`: two subtractions, two products.
   Seven bits of oscillator is enough because on a window aligned to a symbol
   the two tones are **exactly** orthogonal — both harmonic sets are odd
   multiples of 2 and of 3, which are disjoint — and the wrong tone's energy
   at all 356 windows of all three vendored WAVs is measured to be 0.
4. Acquisition scans all 1,921 candidate origins, scoring each by how many of
   the 40 known prefix symbols (24 alternating, then the 16 bits of `0x2DD4`)
   have their known tone as the argmax. The best score is a *plateau*, so the
   origin is its centre, then refined over ±24 samples by the known prefix's
   total energy. Below 37 of 40 it gives up (exit 1).
5. A soft bit is `E₁ − E₀`, **not** the reference's `(E₁−E₀)/(E₁+E₀)`: no
   division, and it is the sounder metric — it weighs a faded symbol less, as
   the likelihood does and the normalised form does not. The 316 differences
   are scaled by one shift for the whole frame so the path metrics fit.
6. The soft bits are scattered back through the interleaver's walk, a 64-state
   soft Viterbi decodes them with one decision *bit* per (step, state) — the
   next state already carries everything but the dropped bit — and the 152
   surviving bits become 19 bytes whose CRC-16 residue must be 0.

## How the receiver is checked

`tests/programs/receptio_loopback/`, `receptio_exemplum/` and
`receptio_vacuum/` feed it the three vendored WAVs on standard input and
compare its 17 bytes with `cmp`. `receptio_caput/` feeds a header at the wrong
sample rate and expects exit 3 and no output. `receptio_circuitus/` is the
loopback: 140 words — seventeen zero bytes, the 136 one-hot words, and the
three frames — each synthesised with the transmitter's own `sona`, run through
the receiver in the same process, and checked byte for byte. Its output is 140
zero bytes, so a failing word names itself by its offset.

`tx | rx` is what that last test *is*, with the pipe replaced by an array. It
takes 1.2 s; one WAV decode takes 25 ms, of which most is 38,060 one-byte
`read` syscalls.

What the certificate **cannot** see is stated rather than hidden:
`docs/design/receptor.md` section 6 names four mutants that decode anyway — a
one-bit change to the sync word (the reference tolerates three misses of 40), a
threshold of 36 for 37, the plateau's first origin for its centre, and a
one-off in the oscillator table. All four were applied and all four still
decode, which is the result the design predicts; the five that must fail (the
generator taps swapped, the interleaver's stride, the sync word complemented,
both oscillators on one tone, the threshold raised past 40) all fail, each in
the predicted way.
