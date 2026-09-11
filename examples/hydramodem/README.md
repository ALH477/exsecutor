# examples/hydramodem/

A transmitter for [HydraModem](https://github.com/ALH477/HydraMesh)'s acoustic
2-FSK modem, written in Exsecutor. Give it a 17-byte DeModFrame and it writes
the WAV file HydraModem's own reference transmitter writes for that frame —
**byte for byte**, all 38,060 bytes. HydraModem's receiver, `frame_rx`,
decodes those reference files back to their frames
(`vendor/hydramodem-tx/PROVENANCE.md`), so it decodes these.

The DeModFrame codec (spec §14 entry 23) showed the language can express the
frame. This is the next layer out: the frame on a wire that is air.

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

## The files

| file | what it is | authority |
|---|---|---|
| `quantum.exsc` | `DeModFrame` (spec §5.2's declaration) and the CRC-16 HydraModem appends | none |
| `modulator.exsc` | the transmitter: the tone of each symbol, the samples each tone becomes, the WAV header | none: pure |
| `emitte.exsc` | walks that layout and writes every byte to a `Scriptor` | whatever the `Scriptor` carries |
| `loopback.exsc`, `exemplum.exsc`, `vacuum.exsc` | one `initium` each, holding one frame as a struct literal | `Mundus`, from which `ambitus` |
| `basis.exsc` | the certificate driver: no WAV, but the 356 tones of each of 137 words, one byte a tone — compiled with `quantum.exsc` and `modulator.exsc` only | `Mundus`, from which `ambitus` |

Only a driver names `Mundus`. `modulator.exsc` declares no `poscit` and takes
no capability, so it is pure in spec §4.1 rule 6's sense: it may compute,
and it may not observe or touch the host. The compiled program's syscalls
are `write` and `exit_group` and nothing else; the test harness audits each
binary for exactly that.

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
