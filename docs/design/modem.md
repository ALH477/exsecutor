# The HydraModem transmitter — design for the modem program, M1

Status: **design only; nothing implemented.** No Exsecutor program renders a
DeModFrame to audio. Every claim below about a running program is
`[UNTESTED]`; what has been measured is listed in section 11, and each such
measurement was made in a scratch directory with tools that never ship
(`prototypes/README.md`'s rule). `spec §N` cites
`docs/spec/exsecutor-spec-v0.4.md` at the commit of this file; `WC §n` and
`WC Dn` cite `docs/design/wire-codec.md`; ADR 0013 is
`docs/decisions/0013-hydramodem-transmitter.md`. The reference is HydraMesh
at `fce2813f85ac17e29f34fa1adf4008056b116318`, subtree `hydramodem/` —
read-only, never linked, never vendored as source — and the three WAVs its
`dcf-tools/frame_tx` rendered from that commit, vendored with digests at
`vendor/hydramodem-tx/` (`PROVENANCE.md` there records the recipe, the
compiler, the `-O0`/`-O2`/`-O3` identity and the `frame_rx` round trip).
`file:line` citations below are into `hydramodem/` at that commit.

## 1. What is being built, and the constraint that shapes it

An acoustic M-FSK modem interoperable with HydraModem, transmitter first: a
program that takes a 17-byte DeModFrame and writes the 38,060-byte WAV
HydraModem's reference transmitter writes for it, **byte for byte**. §14
entry 23 certified that Exsecutor can express the frame; this program is the
next layer out — the frame on a wire that is air — and the second external
certificate after entry 23's (ADR 0013).

The constraint is the same as WC §1's: the language may say only what it has
settled. Since entry 23 the settled set is unsigned integers at every width
(spec §5.4), `aut` / `sursum` / `deorsum` (WC D1), hex literals, struct
literals, `@transitus` fields as the byte-order and bit-placement machinery
(WC D2), the one aggregate cast (WC D4), `Scriptor.scribe_octeto` (WC D7),
`si`/`sin`/`aliter`, `per` over a range, `dum … terminus`. What is *not*
settled, and what a modem written the ordinary way would reach for first:
bitwise and/or, `/`, remainder, signed shifts, narrowing `sicut`, array
literals — all `[OPEN]` in spec §5.4 and §8.6. **The design result to state
and test: the transmitter needs none of them.** Section 3 says how, and
section 7 writes it out.

Two facts about the reference make that possible, both verified against
source and by measurement (section 2, section 11):

1. **The transmitter is integer-exact.** Every tone is an integer number of
   cycles per symbol (`hydra_profile_init`, `hydra_profile.c:89-94`, refuses
   anything else) and the sample rate is an integer multiple of the baud, so
   the phase accumulator (`hydra_dsp_ref.c:45-48`) returns to exactly 0 at
   every symbol boundary and each of a symbol's 48 samples is one of 48
   fixed values. A floating-point modulator with a floating-point sine is,
   for this profile, a 48-entry integer table and an index that steps by 2
   or 3 modulo 48.
2. **The pipeline up to the symbol stream is affine over GF(2)** and the
   modulator is memoryless per symbol, so a basis certificate on the symbol
   stream lifts to the whole WAV (D9). M1 does not build that certificate;
   it is M2, and D9 says exactly what M1's three frames prove without it.

## 2. What the survey established, verified against source

Each row was checked by reading the cited lines and, where it is a number,
by the scratch model of section 11 reproducing the vendored WAVs from it.

| item | value | where |
|---|---|---|
| default profile | 48 kHz, 1000 baud, 2 tones at 2000 and 3000 Hz, preamble 24 symbols, sync `0x2DD4`, `HYDRA_FEC_CONV`, interleave on, gain 0.9 | `hydra_profile.c:7-20` |
| samples per symbol | `(int)(48000/1000 + 0.5)` = 48 | `hydra_profile.c:61` |
| lead and tail silence | `(size_t)(0.02 * 48000)` = 960 samples each (`0.02 * 48000.0` is exactly `960.0` in binary64; measured) | `hydra_modem.c:73-74` |
| data field | 17 payload bytes then CRC-16/CCITT-FALSE **over all 17**, big-endian: 152 bits, MSB-first | `hydra_frame.c:67-75`, `:12-18`; `hydra_crc.c:5-18` |
| convolutional code | K=7, r=1/2, `G0 = 0x79` (0171), `G1 = 0x5B` (0133), `reg = (b << 6) \| state`, G0's output first, `state = reg >> 1`, six zero tail bits: 2·(152+6) = 316 coded bits | `hydra_conv.c:5-6`, `:19-33`; `hydra_conv.h:26` |
| interleaver | gather, `out[i] = in[(i·19) mod 316]`; 19 is the first `s ≥ 18` (18² = 324 ≥ 316) coprime to 316 | `hydra_interleave.c:10-31` |
| symbol map | 1 bit per symbol, plain binary, MSB-first grouping; zero-padding never fires at 1 bit/symbol | `hydra_frame.c:31-46`; `hydra_profile.c:57-59` |
| preamble | 24 symbols, `k & 1 ? N−1 : 0` — tone 0 first | `hydra_frame.c:116-118` |
| sync | `0x2DD4` as 16 symbols, MSB first | `hydra_frame.c:58-64`, `:120-123` |
| frame length | 24 + 16 + 316 = 356 symbols = 17,088 samples; 960 + 17,088 + 960 = 19,008 samples | `hydra_profile.c:77-79`; `hydra_modem.c:82-83` |
| modulation | `phase += f/48000; phase −= floor(phase); out = (float) sin(2π·phase)`, phase starting at 0, **before** the first output | `hydra_dsp_ref.c:42-50` |
| gain | `audio[i] *= (float) 0.9` — a `float` multiply on a `float` buffer | `hydra_modem.c:99-100` |
| WAV | 44-byte header (`RIFF`, 36 + data, `WAVE`, `fmt `, 16, PCM 1, mono 1, 48000, 96000, block 2, 16 bits, `data`, data bytes); each sample `lround(clamp(s) · 32767)` as `int16`, little-endian | `wav.c:12-40` |
| no scrambler, no DC blocker, no ramp on the reference path | — | `hydra_dsp_ref.c` has none; the Faust path adds them (`hydra_dsp_faust_tx.c:76-88`), which is why D1 picks the C reference |

**The table.** Sample `i` (0 ≤ i < 48) of any symbol on tone `k` is
`T[((i + 1) · c_k) mod 48]` with `c_0 = 2`, `c_1 = 3`, and

    T[m] = lround( f64( f32( f32(sin(2π·m/48)) · 0.9f ) ) · 32767.0 )

whose first quarter is `0 3849 7633 11285 14745 17953 20853 23396 25539
27245 28485 29238 29490`, with `T[24−m] = T[m]` and `T[24+m] = −T[m]`
(measured: the quarter-wave identities hold on all 48 entries, and `T[0] =
T[24] = 0`). The rounding chain is the reference's, in order — a `double`
sine, cast to `float`, multiplied by `0.9f` in `float`, widened, scaled,
rounded half away from zero — and it is what makes the table these integers
and not the ones a `double`-only chain gives. The two casts to `float` are
the `(float)` at `hydra_dsp_ref.c:48` and the `float` buffer at
`hydra_modem.c:86`; nothing in the survey's prose named them and the
reference's own comment (`hydra_dsp_ref.c:15`, "Everything is double
precision") does not either — finding 4.

**Only 32 of the 48 entries are ever read.** Tone 0 walks the even indices;
tone 1 walks the multiples of 3. The odd indices that are not multiples of 3
— 1, 5, 7, 11, 13, 17, 19, 23, 25, 29, 31, 35, 37, 41, 43, 47 — never occur,
so of the thirteen first-quarter values only **nine** are live: `m ∈ {0, 2,
3, 4, 6, 8, 9, 10, 12}`. A table that carries `T[1] = 3849` carries a value
no certificate can check (measured: `T[1] + 1` changes no byte of any
frame). D4 carries the nine. The preamble alone visits all 32 live entries —
24 samples of tone 0 and 24 of tone 1 in its first two symbols — so any one
frame certifies the whole table in its first 192 bytes of audio.

**The frame's own CRC makes the modem's CRC zero.** CRC-16/CCITT-FALSE over
`data ‖ crc(data)` is 0 for this CRC (no reflection, no final xor), and a
valid DeModFrame *is* `data ‖ crc(data)` — `cursus` at bytes 15–16 is the
same CRC over bytes 0–14. So `hydra_crc16_ccitt(payload, 17)` is `0x0000`
for every valid frame (measured on all three vendored frames; finding 3 says
what that means for the certificate).

**The survey's receiver figures** — phase-blind, scale-invariant in one-shot
mode, streaming needs peak > 0.02, decodes 1-bit quantisation, +200 Hz
offset decodes and +300 fails, −5000..+8000 ppm, 37 of 40 known symbols
needed — are `[UNREPRODUCED]` here: nothing in this document re-ran them,
and M3 is where they are re-measured. What *is* read from source: the
acquisition threshold `best_score < nknown − 3` (`hydra_modem.c:278`), i.e.
37 of 40; the streaming on-threshold `max(6·noise, 0.02)`
(`hydra_modem.c:441`).

## 3. Decisions

No new §13 code (section 4). No spec edit: nothing the transmitter needs
contradicts the spec, and the one thing that contradicts the *compiler* is
reported as finding 1, not fixed here.

### D1 The reference is the C DSP at `fce2813`, not Faust

`hydra_dsp_ref.c` is the target. The Faust transmitter
(`faust/hydramodem_tx.dsp`, `m.cpfsk : m.txout(gain)`) smooths the gain with
`si.smoo` and DC-blocks the output, and the C adapter runs it for 8,192
warm-up samples on a fixed tone so those settle (`hydra_dsp_faust_tx.c:76-88`).
Its samples are therefore not the reference's samples, and the README's
"both produce identical loopback results" (`README.md:164-165`) is a claim
about *decoding*, not bytes. A byte-identical certificate needs one
producer with one rounding chain; the reference's is fully determined by
section 2's table, the Faust path's by a Faust version and its `-double`
flag. Pinned to a commit, not a branch (ADR 0011's mitigation).

Rejected: certifying against both. There is nothing to certify against in
the Faust output that the C output does not already pin, and the extra
producer would make the certificate depend on a toolchain this repository
does not vendor.

**Retired by:** nothing to retire — this is the choice of oracle. The
vendored `PROVENANCE.md` records the build.

### D2 The M1 certificate: three WAVs, byte-identical, and what that proves

`tests/programs/hydramodem_*/` (D8) compile the modulator with one driver
per frame and compare stdout with `cmp` against
`vendor/hydramodem-tx/<frame>.wav` — the harness's existing `stdout=PATH`
check, repo-root-relative, so the reference bytes are content-addressed by
git and never regenerated at test time. The three frames:

| frame | why this one |
|---|---|
| `d310123400a1ffffdeadbeef0a1b2ca961` | `dcf_loopback.c:69-71`'s frame: genus 0, numerus `0x1234`, fons `0x00a1`, meta `0xffff` (broadcast), onus `deadbeef`, tempus `0x0a1b2c` — the frame the survey's sha256 was recorded on |
| `d31312340001ffffdeadbeefab12cd24c0` | entry 23's example frame (`anchors.exampleFrame_full`), many bits set in every field |
| `d310000000000000000000000000005b80` | the all-zero body sealed: the CRC of fifteen zero bytes is `0x5b80` (entry 23, section 1 vector 0) |

**Stronger than entry 23's certificate in one way:** the comparison is the
whole artifact, 38,060 bytes of it, with no theorem in between — a WAV that
matches is a WAV HydraModem's receiver decodes (the vendored `PROVENANCE.md`
round-tripped all three through `frame_rx`).

**Weaker in another:** three frames are three points, not a basis. Under
D9 the symbol stream is affine in the 136 frame bits, so it is determined
by 137 points; three of them determine nothing beyond themselves. What
three frames *do* prove, exactly:

- the WAV header, the lead and tail, the preamble and the sync word —
  constants, present in every frame;
- the whole 48-entry table and the index walk — the preamble visits every
  live entry (section 2);
- the layout arithmetic — where symbol `s` lands, that there are 356 of
  them, that the file is 38,060 bytes;
- that the CRC, the encoder and the interleaver agree with the reference at
  three inputs: 3 × 316 = 948 coded bits, a spot check.

What they cannot prove, with two concrete witnesses measured in the scratch
model:

- **A CRC replaced by the constant 0 passes.** Every valid frame's modem
  CRC is 0 (section 2), so on the three certified inputs the 16 CRC bits are
  the same under the real CRC and under no CRC at all. The `0x1020`
  polynomial mutant *is* caught (its residue on a valid frame is not 0),
  which is why section 6 lists it; the constant-0 mutant is listed as the
  one that must *pass* — a harness that reports it failing has a bug.
- **An encoder tap on data bits 136–151 alone is invisible**, for the same
  reason: those bits are 0 on every certified input.

Both close at M2, whose basis words are one-hot and all 136 of them
*invalid* DeModFrames with nonzero modem CRC (measured), so the CRC is
exercised on every non-trivial value. Section 6 states the M1 mutant set
with these limits in view.

Rejected: more frames at M1. Frames chosen by hand are more points; they
do not become a basis, and D9's basis is the right shape for M2. Also
rejected: an affine argument at M1 on the WAV. The WAV is not affine in
the frame — a sample is a table lookup, and a table lookup is not linear —
so entry 23's argument does not transfer; D9 says what does.

**Retired by M1:** the three `tests/programs/hydramodem_*/` directories
passing `cmp` against the vendored WAVs; the mutants of section 6 failing at
their predicted offsets; the constant-0 CRC mutant passing, as predicted.

### D3 Integer-exact modulation: a 48-entry table and an index that steps by `c`

The modulator holds no phase and no sine. Per symbol, `m` starts at 0 and,
for each of 48 samples, `m = m + c; si m ge 48 { m = m − 48; }` then
`T[m]` is the sample — `c = 2` for tone 0, `3` for tone 1. `m` is a
`mensura`; the compare-and-subtract is the remainder, so `/` and remainder
stay `[OPEN]`. After 48 steps `m` is `48·c mod 48 = 0`: each symbol begins
at index 0 whatever came before, which is the reference's "phase ≡ 0 at
every symbol boundary" restated as an invariant a reader can check by
arithmetic. `[UNTESTED]`: the walk in Exsecutor; measured: the walk in the
scratch model reproduces all three vendored WAVs (section 11).

**Why this is exact and not approximately so.** The reference accumulates
`f/48000` in `double` 17,088 times with a `floor` wrap
(`hydra_dsp_ref.c:46-47`); the per-step rounding error is at most half an
ulp of a value below 1, and 17,088 of them are below 2·10⁻¹², which moves a
pre-rounding sample by under 10⁻⁶ of a step — far from any half-integer
boundary at the nine live values (measured: no live pre-rounding value is
within 5·10⁻³ of `.5`). So the reference is, on this profile, the integer
table, and the vendored bytes are evidence of it at three frames and
`-O0`/`-O2`/`-O3` (`PROVENANCE.md`). What would break it: a profile where
`sr/baud` is not an integer (44.1 kHz at 1000 baud gives `spp = 44` and a
phase that does not return to 0 — M4 rules such profiles out, section 9).

Rejected: a phase accumulator in fixed point. It would reproduce the
reference's drift only by reproducing its `double` arithmetic, which the
language has not settled (spec §5.4 floating point is `[OPEN]` in the
backend) and which the certificate shows is unnecessary.

**Retired by M1** (the three frames: sample 0 of the preamble is `T[2]`,
sample 5 is `T[12]`, and every other entry follows in the first two
symbols).

### D4 The sine table is a function of nine values and quarter-wave symmetry; array literals stay `[OPEN]`

`sinus(m: mensura) -> u16` folds `m` into the first quarter — `m ≥ 24`
records a negation and subtracts 24; `m > 12` becomes `24 − m` — selects
one of the nine live values, and negates as `0 -% v` in `u16` when the
half was the second: the two's-complement `u16` D6 writes. No array. This
was the survey's proposal with `discerne` over thirteen values; two things
changed it.

**Nine, not thirteen** (section 2): the four dead values have no
certificate and would be carried on faith, exactly what CLAUDE.md's
evidence rule forbids for a number. A program that needs `T[1]` — M4's
`spp = 40` profile does not; a 4-FSK profile at this rate does not either,
since `c_k ∈ {2,3,4,5}` — reads a value nothing has checked; it is
recorded here as `3849` so that the day it is needed the number and its
derivation are on file, marked `[UNTESTED]`.

**`discerne` does not run today — finding 1.** The spelling this decision
was written for,

    discerne q { casus 0 { v = 0; } casus 2 { v = 7633; } … aliter { v = 0; } }

parses, type-checks and lowers on paper (`docs/design/lowering.md` §2,
"`discerne`", a compare chain), and `exsc aedifica` without `-o` accepts
it; with `-o` the compiler traps (`rassert`, exit 132) on a three-arm
`discerne` over a `u8`, a `u64` or a `mensura`, on arms that assign, on
arms that are empty, and on a one-arm-plus-`aliter` form (section 11). So
M1 spells the selection as `si q eq 0 { … } sin q eq 2 { … } … aliter { … }`
— nine `sin`s — which is measured to compile, assemble, run and produce
`T[12] T[2] T[36] T[22]` correctly through D6's view (section 11, probe
`p5`). Moving it to `discerne` when the trap is fixed is a mechanical edit
that changes no byte of output; the design's intent is `discerne`, and the
finding is reported to the lowering's owner rather than worked around
silently.

**The cost, stated.** At run time, nothing: nine compares per sample
against a syscall per sample (D7). In the text, a table written as a
selection statement is a table written as control flow — eleven lines that
should be one — and it does not scale: M3's receiver needs, per tone, a
cosine and a sine table over the same 48 indices (or 32 live ones), and
M4's profiles need a table per `spp`. Three tables as thirty arms is the
first concrete evidence *for* array literals this project has produced —
entry 23 produced evidence *against* bitwise and/or by not needing them,
and this is the mirror case — and it is recorded as such in spec §8.6's
open list by reference, not by amendment (this file does not edit the
spec). M5 is where the decision is taken, with the receiver's tables as the
second and third data points. What array literals would need to settle
first, so the decision is not taken in a hurry: the element type of a
bare literal (a range of two pending literals already types as `mensura`,
spec §8.5; a literal of pending literals would need the same rule or an
annotation), whether a literal may be a module-level `firma` (spec §8.6
decision 5 makes module `firma` a constant, so a table would be one), and
whether a `mutabilis acies` binding may be declared *without* a literal
(finding 6: today an `acies` is born only from a `@transitus` struct).

Rejected: a 48-field `@transitus structura` cast to `acies<u8, 96>` and
indexed. It puts 96 bytes of hand-written little-endian constants into a
declaration whose only purpose is to be an array literal by another name,
and it needs the index doubled and two byte reads per sample where D6's
view does the split once at the write. Also rejected: computing the sine.
There is no integer sine to compute; the table *is* the specification of
the reference's rounding chain.

**Retired by M1** (every live entry, through the three frames);
`discerne`'s re-spelling by the fixture that retires finding 1.

### D5 Bits, the encoder and the interleaver, with settled language only — and why the encoder is evaluated feedforward

Three sub-decisions, each avoiding an `[OPEN]` operator by construction.

**Bit extraction.** Data bit `u` of the 152-bit field is read by `datum(f,
c, u)`: for `u < 136` byte `u deorsum 3` of the frame's byte view, shifted
toward significance `u − ((u deorsum 3) sursum 3)` times by `sursum 1` in
`u8`, then `ge 0x80`; for `136 ≤ u < 152` the same on the `u16` CRC; for
`u ≥ 152` (the six tail bits) 0. `sursum 1` discards the bits above the
width (WC D1), so there is no mask and no bitwise and; the top-bit test is
a comparison, as in `redundantia`. A shift by a *variable* count would
need the count to be a `u8` (WC D1, same type as the operand), which from
a `mensura` needs a narrowing `sicut` — `[OPEN]` in spec §5.4 — so the
count is a loop of unit shifts instead. Measured: the `ge 0x80` /
`sursum 1` walk over `0xd3` gives `11010011` (section 11, probe `p4`).

**The encoder.** The K=7 code is the shift register the reference defines:
with `reg = (b << 6) | state` and `state = reg >> 1`, bit 6 of `reg` is
the current input and bit `6−k` is the input `k` steps ago, so `G0 = 0x79
= 1111001₂` taps the current bit and delays 1, 2, 3, 6, and `G1 = 0x5B =
1011011₂` taps the current bit and delays 2, 3, 5, 6 (measured against
the mask-and-parity form on random inputs: identical). Each output is an
`aut` of five bits, so there is no parity instruction and no `and`. Tail:
six zeros after bit 151, which `datum`'s `u ≥ 152` case supplies; the
state starts at 0, which `datum`'s "delay before the start is 0" supplies
(`t − k < 0` reads nothing).

**It is evaluated feedforward, not as a register.** The interleaver
(`out[i] = in[(19·i) mod 316]`) needs coded bit 19, then 38, then 57 …
while a register produces 0, 1, 2 … — so a register form must buffer all
316 coded bits before the first symbol, and the language has no way to
*make* a 316-bit buffer: an `acies` binding is born only from a
`@transitus` struct (finding 6), and writing bit `j` of a byte array needs
a variable shift and a narrowing cast, both `[OPEN]`. So coded bit `j` is
computed when it is needed: `t = j deorsum 1`, `p = j − (t sursum 1)`
(which output), and `codificatum(f, c, j)` is the `aut` of `datum(t − k)`
over that output's taps, guarded by `t ge k`. Same taps, same code, no
state; measured equal to the mask encoder plus interleaver on the three
frames and 200 random ones (section 11). The register form is not lost: it
is M3's trellis, where it belongs.

**The interleaver walk.** The driver carries `j` across the 316 data
symbols: `j = j + 19; si j ge 316 { j = j − 316; }` — one subtraction
suffices because `19 < 316`. No multiplication, no remainder. Data symbol
0 reads coded bit 0 under any stride, which is why the stride mutant first
differs at data symbol 1 (section 6).

**Preamble and sync.** `praeambulum(s) = s − ((s deorsum 1) sursum 1)` —
`s mod 2`, tone 0 first, matching `k & 1 ? N−1 : 0`. `synchronia(n)` is
`0x2dd4` in `u16` shifted `n` times by `sursum 1`, then `ge 0x8000`.

Rejected: a 40-field `@transitus` struct as the coded-bit buffer (needs the
two `[OPEN]` items above and a second 316-iteration pass for no output);
computing the interleaved index as `(19·i) mod 316` (needs remainder);
`u1` delays with `aut` in a register (measured to work, probe `p2`, but
see the buffer above — the delays are `datum` reads instead).

**Retired by M1:** the three frames (every data symbol is a coded bit
through `datum`/`codificatum`/the walk); the G1-tap, stride and sync
mutants of section 6.

### D6 Samples and the header through `@transitus` views; negatives by `0 -% v`

A sample is `@transitus structura Exemplum { valor: u16:minor }` cast to
`acies<u8, 2>` and written byte 0 then byte 1 — entry 23's `Syndroma`
pattern with the other order. A negative table entry is `0 -% v` in `u16`:
the wrapping subtraction (spec §5.4, `tests/programs/angusta/`) gives the
two's-complement bit pattern the reference's `(uint16_t)(int16_t)v`
(`wav.c:36`) writes, with **no signed type anywhere in the program**.
Measured: `0 -% 7633` through the view is `2f e2` (section 11, probe
`p3`), which is `0xe22f = −7633` little-endian.

The WAV header is one `@transitus structura Caput` of thirteen fields and
44 bytes — the four-character tags as `u32:maior` holding their ASCII
(`0x52494646` is `R I F F` in that order on the wire), the numbers as
`u32:minor` / `u16:minor` — built by a struct literal (every field named,
WC D3) and written through `sicut acies<u8, 44>`. The declaration *is* the
header layout, field by field, the same claim WC §3 made for the frame.

Rejected: writing the header as 44 literal bytes (the layout would be in
the reader's head, not the type); a `u16` sample written as two narrowing
casts (`[OPEN]`, and the view says the order once instead of twice).

**Retired by M1** (the first 44 bytes and every sample of all three
frames).

### D7 Output: the WAV on stdout, one byte per call; the modulator is pure

`Scriptor.scribe_octeto` (WC D7), 38,060 calls, no new prelude call and no
new syscall. Only the `initium` driver names `Mundus` and binds `sub
ambitus`; `modulator.exsc` has no `poscit` and takes no capability-bearing
value, so every function in it is pure in spec §4.1 rule 6's sense, and
the binary's audit must show `write` and `exit_group` only — the harness's
`--potestates Mundus,ambitus` audit already checks this for every program.

**Cost, measured:** 38,060 one-byte writes take 31 ms wall on this host
(section 11, probe `p6`), against a 20-second harness limit. Not a reason
to add a buffered writer; when one exists it changes no byte.

Rejected: an `acies<u8, N>` writer (WC D7's reason: no generic `N` in the
prelude); writing a file (needs `archivum`, and the harness compares
stdout).

**Retired by M1** (the audit lines of the three program tests).

### D8 Where the code lives: `examples/hydramodem/`, tested from `tests/programs/hydramodem_*/`

The modulator is a program other people should read — the second thing
this language has been used for, and the first that makes sound — so it
lives in `examples/`, beside the hello world, and is run by the harness the
way the hello world is: `tests/programs/saluta/TEST` points `sources=` at
`examples/*.exsc` and `stdout=` at `examples/saluta.expected`, one copy of
the program, "so this test and the gate can never drift apart on what the
hello world is". Three directories,

    tests/programs/hydramodem_loopback/TEST
    tests/programs/hydramodem_exemplum/TEST
    tests/programs/hydramodem_vacuum/TEST

each `TEST: expect-exit=0 sources=examples/hydramodem/quantum.exsc,examples/hydramodem/modulator.exsc,examples/hydramodem/emitte.exsc,examples/hydramodem/<driver>.exsc stdout=vendor/hydramodem-tx/<frame>.wav`.
The harness resolves both paths against the repository root
(`tests/run.sh`, `run_program_tests` and `parse_run_keys`), and the
vendoring commit's `flake.nix` already copies `vendor/hydramodem-tx/` into
the `test` sandbox for exactly this use.

There is no argument or stdin reader in the prelude (`ambitus` carries
`argv` at offset 24, `prelude/interface.inc:124`, and no surface call
reads it; M3 needs one — section 9), so the frame is compiled in: one
driver file per frame, holding the frame as a **`DeModFrame` struct
literal** (`quantum.exsc` declares §5.2's type, as `tests/programs/forma/`
does, since there is no module system to import entry 23's) and passing
`f sicut acies<u8, 17>` to the modulator. A modem transports a DeModFrame;
the driver says so in the type.

Rejected: `tests/programs/` alone — a test is the wrong place for the
thing being demonstrated, and the harness already has the `sources=`
mechanism for this. Rejected: one driver taking the frame from stdin (no
reader; M3's problem, not M1's).

**Retired by M1** (the three directories running; the `saluta` precedent
already runs).

### D9 The symbol stream is affine over GF(2), and the modulator is memoryless — so a symbol-stream basis lifts to the WAV

**Claim 1: `S: {0,1}^136 → {0,1}^356`, frame bits to tone indices, is
affine.** Read off the reference: the CRC is affine (WC §5's argument: the
conditional xor is `(c sursum 1) aut (top(c) · 0x1021)`, linear in `c`,
init `0xFFFF` the offset); appending it is bit placement; the encoder is a
linear feedforward filter over GF(2) with zero initial state and zero tail
(`hydra_conv.c:20-33` — parity of a masked register is a sum of taps);
the interleaver is a permutation; the symbol map at one bit per symbol is
the identity; the preamble and sync are constants. So `S(f) = A·f ⊕ S(0)`
for a fixed 356×136 matrix `A`, and

    S(f) = S(0) ⊕ ⨁_{i : f_i = 1} ( S(e_i) ⊕ S(0) )

determines `S` everywhere from `S(0)` and the 136 one-hot words — the
same 137-vector shape as entry 23's syndrome basis, with the same
MSB-first-across-the-wire bit numbering (WC §6), derived from the index
exactly as `probatio.exsc` derives them. For the *Exsecutor* transmitter
the premise is a property of its code, to be argued the way WC §5 argues
it: every data-dependent operation in `datum`, `codificatum`, the walk and
`tonus` is a bit extraction, an `aut`, a constant shift or a
data-independent index computation; no `+`, no `*`, no comparison used as
a value touches data (the `ge 0x80` tests select a constant, as the CRC's
does). That argument is made in section 7 against the code as written and
is `[UNTESTED]` until M2 runs.

**Claim 2: the WAV is a memoryless function of the symbol stream.** D3:
`m` starts at 0 at every symbol, so the 96 bytes of symbol `s` depend on
`S(f)_s` alone — block `A` (tone 0) or block `B` (tone 1), which differ
already at their first sample (`T[2] = 7633` vs `T[3] = 11285`). So
`WAV(f) = header ‖ lead ‖ block(S(f)_0) ‖ … ‖ block(S(f)_355) ‖ tail`, an
injective per-symbol substitution.

**Consequence.** Blocks `A` and `B` are certified by any one frame's
preamble (M1). If M2 certifies `S` on the 137-word basis, then by claim 1
`S` agrees with the reference on all 2^136 words, and by claim 2 so does
the WAV — **byte-identical audio for every 17-byte input**, valid frame or
not, which is stronger than the prompt for this design anticipated (it
asked whether the argument applies to the symbol stream; it applies to the
audio, because the modulator has no memory). What it rests on is the two
structural premises about the Exsecutor code, argued from reading, plus
M1's certification of the blocks, the header and the layout.

**Proposal for M2** (section 9): a vendored basis of 137 symbol streams,
each 356 bits packed MSB-first in 45 bytes (6,165 bytes in all), extracted
from 137 `frame_tx` renders at `fce2813` by the rule "sample 0 of symbol
`s` is `7633` → 0, `11285` → 1" — the block map's own injectivity — with
the 137 WAV digests and the extraction script's digest recorded in the
tree's `PROVENANCE.md`, so the basis is the reference program's output
reduced by a stated, checkable rule and not a re-derivation. The Exsecutor
side writes `tonus` for each of the 137 words as one byte per symbol, and
a verification-only Python script compares. The one-hot words are all
invalid DeModFrames with nonzero modem CRC (measured), which is what
closes D2's two blind spots.

Rejected: a basis at M1. M1 is the artifact people will play; the basis is
the proof, and it needs 137 renders the vendoring did not include.

**Retired by:** M2 (the basis, 137/137); the structural premises stay an
argument, as entry 23's do.

## 4. Error codes, checked against §13

Nothing here adds a code. The program is well-formed by construction, and
every diagnostic it could draw while being written is one entry 23 already
drew: `EXS-E0303` for a shift count of the wrong type, `EXS-E0305` for a
shift on a signed operand, `EXS-E0304` for a `Caput` literal missing a
field, `EXS-E0307` for a `mutabilis` read before assignment, `EXS-E0421`
for a `poscit` the row does not cover. Finding 1's trap is not a
diagnostic and must not become one without a §13 amendment: it is a
compiler defect (`rassert`, `docs/asm-conventions.md`: an internal
contract violation, never a user-facing code).

## 5. The program's shape

Four files, compiled as one unit in this order (spec §12):

| file | contents | capability |
|---|---|---|
| `examples/hydramodem/quantum.exsc` | `DeModFrame` (§5.2's declaration verbatim) and `redundantia` — the same two things `tests/programs/forma/` declares | none |
| `examples/hydramodem/modulator.exsc` | `sinus`, `datum`, `codificatum`, `praeambulum`, `synchronia`, `tonus`, `Exemplum`, `Caput`, `caput()` | none — **pure**, no `poscit`, no `initium` |
| `examples/hydramodem/emitte.exsc` | `scribe_exemplum`, `scribe_caput`, `modula` — the byte emitters, `poscit sicut s` on the writer | `ambitus` through `s` |
| `examples/hydramodem/<driver>.exsc` | `initium`: the frame as a literal, `modula(s, f sicut acies<u8, 17>)` | `Mundus` → `ambitus` |

The functions and their contracts (names per spec §3 as far as the
lexicon pass, which is not enabled, would judge them; provisional):

| function | contract |
|---|---|
| `sinus(m: mensura) -> u16` | `T[m]` for `0 ≤ m < 48` as a two's-complement `u16`; the four dead first-quarter indices and their mirrors return 0 and are never called |
| `datum(f: acies<u8, 17>, c: u16, u: mensura) -> u1` | bit `u` of `f ‖ c ‖ 000000`, MSB-first; 0 for `u ≥ 152` |
| `codificatum(f, c, j: mensura) -> u1` | coded bit `j` of the K=7 encoder over `datum`, `0 ≤ j < 316`; even `j` from G0, odd from G1 |
| `praeambulum(s: mensura) -> mensura` | tone of preamble symbol `s`: `s mod 2` |
| `synchronia(n: mensura) -> mensura` | bit `n` of `0x2dd4`, MSB-first, as a tone index |
| `tonus(f, c, s: mensura, j: mensura) -> mensura` | tone of symbol `s`, given the interleaved coded-bit index `j` the caller carries for `s ≥ 40` |
| `caput() -> Caput` | the 44-byte header for 19,008 samples at 48 kHz mono 16-bit |
| `scribe_exemplum(s: Scriptor, v: u16) -> mensura poscit sicut s` | two bytes, little-endian, through `Exemplum` |
| `scribe_caput(s: Scriptor) -> mensura poscit sicut s` | 44 bytes through `caput() sicut acies<u8, 44>` |
| `modula(s: Scriptor, f: acies<u8, 17>) -> mensura poscit sicut s` | the whole WAV: header, 960 zero samples, 356 symbols × 48 samples, 960 zero samples; returns the byte count, 38,060 |

`tonus` takes `j` rather than computing it so that the walk lives in one
place (D5) and `tonus` stays a pure function of its arguments. WC finding
9 applies as it did to `probatio.exsc`: a `poscit sicut s` function calling
another `poscit sicut s` with the same `s` is refused (`EXS-E0421`, the
checker's defect), so `emitte.exsc`'s helpers write **no** `poscit` and let
spec §4.1 rule 5 infer their rows, exactly as `probatio.exsc` does. The
table in section 7 shows the intended spelling with `poscit`; the file
will not carry it until finding 9 is fixed.

## 6. The certificate and the mutants

Three program tests, each `cmp` against a vendored WAV, each audited; all
three must pass and all listed mutants must fail **at the predicted first
byte**, which `cmp` prints (1-based; the offsets below are 0-based, so
`cmp` reports each as one more). Layout: header bytes 0–43; lead samples
bytes 44–1963; symbol `s` at byte `44 + 1920 + 96·s`, sample `i` of it at
`+2i`; data symbol `d` is symbol `40 + d`; tail from byte 36,140.

Predicted in the scratch model by applying each mutant to the model and
diffing (section 11); the harness applies them to the Exsecutor source
mechanically, per CONTRIBUTING's mutation rule, and must see the same
offsets.

| mutant | what it breaks | first differing byte |
|---|---|---|
| preamble starts on tone 1 (`praeambulum` returns `1 − (s mod 2)`) | the constant prefix | 1964 (symbol 0, sample 0) on every frame |
| `sinus`: `7633` → `7634` (`T[2]`, the first sample of tone 0) | the table | 1964 on every frame |
| `sinus`: `29490` → `29491` (`T[12]`) | the table, at a later index | 1974 (symbol 0, sample 5) on every frame |
| `sinus`: `11285` → `11286` (`T[3]`, first sample of tone 1) | the table's odd side | 2060 (symbol 1, sample 0) on every frame |
| sync `0x2dd4` → `0xadd4` (MSB flipped) | the first sync symbol | 4268 (symbol 24) on every frame |
| sync `0x2dd4` → `0x2dd5` (LSB flipped) | the last sync symbol | 5708 (symbol 39) on every frame |
| stride 19 → 17 | the interleaver | 5900 (symbol 41 = data 1) on every frame; data 0 is coded bit 0 under both |
| G1 tap `d5` → `d4` (mask `0x5B` → `0x5D`) | the encoder's second output | **frame-dependent**: 6284 (data 5), 6668 (data 9), 9164 (data 35) for the three frames in D2's order — the first coded bit that changes is the first `t` with data bit `t−4 ≠ t−5`, scattered by the interleaver |
| G0 tap `d3` → `d4` (mask `0x79` → `0x75`) | the encoder's first output | 6572 (data 8), 5996 (data 2), 9068 (data 34) |
| CRC polynomial `0x1021` → `0x1020` in `redundantia` | the appended CRC, bits 136–151 | 8780 (data 31) on every frame — coded bit 273, the first CRC bit's G1 output, lands at interleaved position 31 |
| **CRC replaced by constant 0** | nothing visible at M1 | **no difference on any frame** — must *pass*; the negative control (D2) |

The last row is the one entry 23 did not need: it records the certificate's
blind spot as a prediction the harness checks, so that when M2's basis
closes it the change is visible.

A harness on which any of the first ten passes, or the eleventh fails, has
proved nothing and is itself the bug.

## 7. Worked example, in current syntax `[UNTESTED]`

Written against the language as it runs at the commit of this file; the
fragments marked "probe" were compiled, assembled and run in that form
(section 11), the rest has not been. `per i in 0..48` and `0..8` — ranges
of two pending literals — type as `mensura` (spec §8.5); `per k in 0..r`
with `r: mensura` is `redundantia`'s loop; `0 -% v` types the `0` from `v`
(WC D1's literal rule for `+`, applied to `-%` in
`tests/programs/angusta/`).

```exsecutor
// examples/hydramodem/modulator.exsc -- pure; no `poscit`, no `initium`.

@transitus
publica structura Exemplum {
    valor: u16:minor
}

// The 44-byte WAV header, field by field; the tags are their ASCII, on the
// wire in reading order because the field is `maior`.
@transitus
publica structura Caput {
    riff:          u32:maior   // 0x52494646  "RIFF"
    magnitudo:     u32:minor   // 36 + 38016
    wave:          u32:maior   // 0x57415645  "WAVE"
    fmt:           u32:maior   // 0x666d7420  "fmt "
    fmt_longitudo: u32:minor   // 16
    codex:         u16:minor   // 1, PCM
    canales:       u16:minor   // 1
    frequentia:    u32:minor   // 48000
    octeti_secundo: u32:minor  // 96000
    passus:        u16:minor   // 2, block align
    latitudo:      u16:minor   // 16 bits per sample
    data:          u32:maior   // 0x64617461  "data"
    longitudo:     u32:minor   // 38016
}

publica functio caput() -> Caput {
    redde Caput {
        riff: 0x52494646, magnitudo: 38052, wave: 0x57415645,
        fmt: 0x666d7420, fmt_longitudo: 16, codex: 1, canales: 1,
        frequentia: 48000, octeti_secundo: 96000, passus: 2, latitudo: 16,
        data: 0x64617461, longitudo: 38016
    };
}

// T[m], 0 <= m < 48, as a two's-complement u16 (D4). Nine live values;
// quarter-wave symmetry; `0 -% v` is the negation. `discerne` is the
// intended spelling and traps in today's compiler (finding 1).   -- probe p5
publica functio sinus(m: mensura) -> u16 {
    mutabilis q: mensura = m;
    mutabilis neg: u1 = 0;
    si q ge 24 { q = q - 24; neg = 1; }
    si q gt 12 { q = 24 - q; }
    mutabilis v: u16 = 0;
    si q eq 0 { v = 0; }
    sin q eq 2 { v = 7633; }
    sin q eq 3 { v = 11285; }
    sin q eq 4 { v = 14745; }
    sin q eq 6 { v = 20853; }
    sin q eq 8 { v = 25539; }
    sin q eq 9 { v = 27245; }
    sin q eq 10 { v = 28485; }
    sin q eq 12 { v = 29490; }
    aliter { v = 0; }               // 1 5 7 11: dead indices, never reached
    si neg eq 1 { v = 0 -% v; }
    redde v;
}

// Bit u of  f || crc || 000000, MSB-first (D5).                 -- probe p4
publica functio datum(f: acies<u8, 17>, c: u16, u: mensura) -> u1 {
    si u ge 152 { redde 0; }
    si u ge 136 {
        mutabilis w: u16 = c;
        per k in 0..(u - 136) { w = w sursum 1; }
        si w ge 0x8000 { redde 1; }
        redde 0;
    }
    firma o: mensura = u deorsum 3;
    mutabilis b: u8 = f[o];
    per k in 0..(u - (o sursum 3)) { b = b sursum 1; }
    si b ge 0x80 { redde 1; }
    redde 0;
}

// Coded bit j of the K=7 r=1/2 code: G0 (0x79) taps the current bit and
// delays 1 2 3 6, G1 (0x5B) the current bit and delays 2 3 5 6; state and
// tail are zero, which `datum` supplies. Feedforward, no register (D5).
publica functio codificatum(f: acies<u8, 17>, c: u16, j: mensura) -> u1 {
    firma t: mensura = j deorsum 1;
    firma p: mensura = j - (t sursum 1);
    mutabilis o: u1 = datum(f, c, t);
    si t ge 6 { o = o aut datum(f, c, t - 6); }
    si p eq 0 {
        si t ge 1 { o = o aut datum(f, c, t - 1); }
        si t ge 2 { o = o aut datum(f, c, t - 2); }
        si t ge 3 { o = o aut datum(f, c, t - 3); }
    } aliter {
        si t ge 2 { o = o aut datum(f, c, t - 2); }
        si t ge 3 { o = o aut datum(f, c, t - 3); }
        si t ge 5 { o = o aut datum(f, c, t - 5); }
    }
    redde o;
}

publica functio praeambulum(s: mensura) -> mensura {
    redde s - ((s deorsum 1) sursum 1);          // s mod 2: tone 0 first
}

publica functio synchronia(n: mensura) -> mensura {
    mutabilis w: u16 = 0x2dd4;
    per k in 0..n { w = w sursum 1; }
    si w ge 0x8000 { redde 1; }
    redde 0;
}

// Tone of symbol s; j is the interleaved coded-bit index the caller walks.
publica functio tonus(f: acies<u8, 17>, c: u16, s: mensura, j: mensura) -> mensura {
    si s lt 24 { redde praeambulum(s); }
    si s lt 40 { redde synchronia(s - 24); }
    si codificatum(f, c, j) eq 1 { redde 1; }
    redde 0;
}
```

```exsecutor
// examples/hydramodem/emitte.exsc -- the byte emitters. Written without
// `poscit` until WC finding 9 is fixed; the intended rows are shown.

functio scribe_exemplum(s: Scriptor, v: u16) -> mensura poscit sicut s {   // probe p3
    firma y = Exemplum { valor: v };
    firma b = y sicut acies<u8, 2>;
    s.scribe_octeto(b[0]);
    s.scribe_octeto(b[1]);
    redde 2;
}

functio scribe_caput(s: Scriptor) -> mensura poscit sicut s {
    firma b = caput() sicut acies<u8, 44>;
    per i in 0..44 { s.scribe_octeto(b[i]); }
    redde 44;
}

functio modula(s: Scriptor, f: acies<u8, 17>) -> mensura poscit sicut s {
    firma c: u16 = redundantia(f, 17);       // 0 for a valid frame (finding 3)
    mutabilis n: mensura = scribe_caput(s);
    per i in 0..960 { n = n + scribe_exemplum(s, 0); }
    mutabilis j: mensura = 0;                // the interleaver walk (D5)
    per sy in 0..356 {
        firma k: mensura = tonus(f, c, sy, j);
        si sy ge 40 {
            j = j + 19;
            si j ge 316 { j = j - 316; }
        }
        mutabilis step: mensura = 2;         // c_0 = 2 cycles per symbol
        si k eq 1 { step = 3; }              // c_1 = 3
        mutabilis m: mensura = 0;            // the phase index (D3)
        per i in 0..48 {
            m = m + step;
            si m ge 48 { m = m - 48; }
            n = n + scribe_exemplum(s, sinus(m));
        }
    }
    per i in 0..960 { n = n + scribe_exemplum(s, 0); }
    redde n;                                  // 38060
}
```

```exsecutor
// examples/hydramodem/loopback.exsc -- the driver for dcf_loopback's frame.
// The frame is a DeModFrame, not seventeen bytes: the type says what the
// modem carries. `cursus` is the frame's own CRC, as sealed upstream.

publica functio initium(m: Mundus) -> u8 {
    firma a = m.ambitus();
    sub ambitus = a;
    firma s = Scriptor.ad_exitum(a);
    firma f = DeModFrame {
        signum: 0xd3, versio: 1, genus: 0, numerus: 0x1234, fons: 0x00a1,
        meta: 0xffff, onus: 0xdeadbeef, tempus: 0x0a1b2c, cursus: 0xa961
    };
    si modula(s, f sicut acies<u8, 17>) ne 38060 { redde 1; }
    redde 0;
}
```

**The affinity argument on this text (D9, claim 1).** `datum`: an index
computation on `u` (data-independent), a byte read, unit shifts, a
comparison selecting a constant — bit extraction, linear. `codificatum`:
`aut` of `datum`s under data-independent guards — linear. `redundantia`:
WC §5's argument, affine. `synchronia`, `praeambulum`: constants. `tonus`:
selects among them by `s`, data-independent. The walk: data-independent.
Nothing carries, multiplies or compares data as a value. `[UNTESTED]` as a
measurement; it is the reading M2 will test.

**What is known to run of this section**, from the probes of section 11:
`sinus` as written (through `Exemplum`, four indices including a negated
one); `scribe_exemplum` with `0 -% v`; the `ge 0x80` / `sursum 1`
extraction over a byte; `u1` bindings, `aut` on `u1` and a `u1` copied
between bindings; `per i in 0..n` at `n = 38060` with a `scribe_octeto`
per iteration. Not run: `datum`, `codificatum`, `tonus`, `caput`,
`modula`, any `acies<u8, 17>` parameter read at a computed index, and the
`discerne` spelling, which traps.

## 8. Findings

Numbered; each names the document and the line.

1. **`discerne` traps in the compiler at `-o`.** A three-arm `discerne`
   with `aliter` over a `u8`, `u64` or `mensura` scrutinee — arms that
   assign a local, or empty arms, or one arm plus `aliter` — passes
   `exsc aedifica` (lex, parse, typed AST; the driver prints "the front end
   accepts this source") and dies with SIGILL, exit 132, an `rassert`, when
   `-o` asks for code (section 11, six probe files, `exsc` built from
   `2da0040` in `nix develop`). `docs/design/lowering.md` §2 describes the
   lowering as a compare chain and `lower/stmt.inc` implements
   `__lwr_discerne`; the checker's `.discerne` arm types the scrutinee and
   the bodies. Where between lowering and emission the contract fails is
   **not localised here** (no debugger in the environment; `--emitte`
   offers `tokens|cst|ast`, not IR). `tests/unit/cst_structlit.asm` is the
   only fixture in the tree that mentions `casus`, so the construct has
   never been run from source. Reported to the lowering/backend owners;
   D4 routes around it with `si`/`sin`. The README's status block says
   most of the language beyond what the programs use is `rassert`-refused;
   `discerne` should be named there.
2. **`hydra_profile.h:61-63` says the default is "FEC off".** The code
   sets `HYDRA_FEC_CONV` (`hydra_profile.c:16`, "production default: soft
   Viterbi") and the vendored WAVs are 356 symbols long — 316 coded data
   symbols, not 152. The comment is stale; the code and the WAVs are right.
   A reader who trusted the header would render a 192-symbol frame that no
   `frame_rx` decodes.
3. **The modem's CRC over a valid DeModFrame is always `0x0000`.**
   `hydra_frame.c:70` computes CRC-16/CCITT-FALSE over all 17 bytes; bytes
   15–16 already hold that CRC over bytes 0–14, and this CRC's residue over
   `data ‖ crc(data)` is 0. So for every frame the DCF stack will ever hand
   the modem, data bits 136–151 are zero: the modem's CRC adds **no
   detection** the frame's `cursus` did not already provide on valid input
   (the receiver's check at `hydra_frame.c:190-193` compares a recomputed
   CRC with two received bytes that, for a valid frame, are the CRC of a
   word ending in its own CRC — it is the same code applied to a nested
   span, and it does not catch an error the inner check catches for free).
   For this design it means D2's blind spot, and it is why M2's basis is
   made of invalid words. Not a defect in HydraModem — the payload is
   opaque by design (`hydra_profile.h:5-7`) — but a fact its README's
   "CRC-checked" should not be read as adding.
4. **`hydra_dsp_ref.c:15`, "Everything is double precision (deployment
   uses -double)"** is not what determines the bytes. The reference casts
   the sine to `float` (`:48`), stores in a `float` buffer, and applies the
   gain as a `float` multiply (`hydra_modem.c:99-100`); the WAV writer then
   widens to `double` to scale. The table of section 2 has two `f32`
   roundings for that reason, and a re-implementation that computed in
   `double` throughout would differ at some entries. The vendored bytes
   are the ones the `float` chain produces (measured: model = `frame_tx`,
   three frames).
5. **`hydra_profile.h:66-70` and `hydra_profile.c:24-27`: the aux-cable
   profile "matches python/modem/acoustic_frame.py 'aux-cable' profile
   behavior".** It does not, on any layer but the baud. The C profile is
   2-FSK at 1200 and 2400 Hz, 1200 baud, a 16-*symbol* alternating
   preamble, sync `0x2DD4` (16 bits), CRC-16 and the K=7 code with the
   interleaver. The Python profile (`acoustic_frame.py:37-41`) is mark
   1000 Hz / space 1500 Hz, 1200 baud, a 16-*bit* preamble, sync `0x7E`
   (8 bits), payload `frame ‖ crc8` (poly `0x31`) or a Reed–Solomon
   codeword, no convolutional code, no interleaver, a 16-bit postamble.
   A C aux-cable transmitter is not decodable by the Python aux-cable
   receiver or the reverse. M4 targets the C profile and says so; the
   interop claim is reported upstream, not relied on.
6. **An `acies` value can only be born from a `@transitus` struct.** There
   is no array literal (`[OPEN]`, spec §8.6), and a `mutabilis a:
   acies<u8, 40>;` with no initialiser is a definite-assignment fault at
   first read (`EXS-E0307`) with no statement that could assign the whole.
   Every `acies` in the tree today is `S sicut acies<u8, N>` or a copy of a
   parameter. That is what forced D5's feedforward encoder and what M3
   (a 64-state metric array, a 158×64 traceback) cannot live with —
   section 9.
7. **`README.md:60` (HydraModem), "FEC: conv … — default"** agrees with
   the code and contradicts finding 2's header comment; the README is the
   one to trust. `README.md:164-165`, "identical loopback results", is a
   decode claim (D1).
8. **Four of the survey's thirteen table values are unreachable**
   (section 2). Not an error in the survey — the formula is right for all
   48 — but a number carried without a certificate, and the design carries
   only the nine that have one.

## 9. Later milestones

Sketches, not designs; each names what language it needs and which
`[OPEN]` items it forces a decision on.

**M2 — the symbol-stream basis (D9).** Vendor: 137 symbol streams
extracted from 137 `frame_tx` renders (the zero word and wire bit `i` set,
`i = 0..135`), 6,165 bytes plus digests, the extraction rule and the
renders' sha256s in `PROVENANCE.md`. Exsecutor: a driver deriving each
word from its index as `probatio.exsc` does (an `aut` of a one-hot `u8`
walked with `deorsum 1` into a `mutabilis` copy of the zero word) and
writing `tonus` for `s = 0..355` as one byte each; a verification-only
Python comparer. Language needed: nothing new. `[OPEN]` items forced:
none. What it retires: D9's consequence — all 2^136 inputs byte-identical
— under the two structural premises, and D2's blind spots.

**M3 — the receiver.** Decode the three vendored WAVs and M1's own output
back to the frame; then robustness under noise, gain, clock offset and
frequency offset generated by a verification-only Python script that
perturbs the vendored WAVs (the survey's figures, `[UNREPRODUCED]` here,
become measurements). Language needed, in order of how hard each is:
- **a stdin reader in the prelude** — `read(0)` is on the compiler's
  allowlist, but a *program* reading stdin needs a prelude call under
  `ambitus` (spec §4.6 places the streams there): a `Lector.ab_introitu(a)`
  and `lege_octeto` shaped like `Scriptor.ad_exitum` / `scribe_octeto`. A
  spec/prelude decision, and the first program capability added since
  `scribe_octeto`.
- **signed multiply-accumulate** for I/Q correlation: 16-bit samples ×
  16-bit table entries summed over 48 → fits `i64`; `*` on `iN` from
  source is `[UNTESTED]` (only `u64` products run, `tests/programs/phi_loops/`).
- **energy without overflow and without signed shifts**: `I² + Q²` at
  2^37 overflows `u64`; scaling `I` down first is a signed shift
  (`[OPEN]`, spec §5.4). The division-free route: `abs` by `si I lt 0 { I
  = 0 - I; }`, an equal-width sign-change `sicut` to `u64` (measured to
  lower as `trunc`, `tests/programs/angusta/`, but "narrowing `sicut`" is
  the spec's `[OPEN]` sentence and needs one for the equal-width case),
  then `deorsum 6` unsigned, then square. Comparison of energies by
  cross-multiplication where a ratio is wanted, so `/` stays `[OPEN]`.
- **argmax acquisition** over ~11,000 origins × 40 symbols: `per` loops
  and comparisons, nothing new — but prefix sums over 19,008 samples per
  tone (`hydra_modem.c:117-146`) need an **array of 19,009 `i64`s per I/Q
  per tone**, and
- **Viterbi**: 64 path metrics, a 158×64 traceback — arrays that must be
  *created* and *written by index* (finding 6). This is where M3 forces
  **array literals or a zero-initialised `acies` binding** (`[OPEN]`), or
  `alloc` (spec §4.6) for the traceback, and it is the second and third
  data point D4 wants before M5 decides. Hard-decision Viterbi with
  integer Hamming metrics needs no division; soft metrics need the energy
  difference scaled, which is the signed-shift question again.
`[OPEN]` items forced: stdin under `ambitus`; equal-width sign-change
`sicut`; array creation; possibly signed shifts. Certified by: the three
WAVs decoding to their filenames' frames; M1's output decoding; the
perturbation sweep.

**M4 — other profiles.** The aux-cable profile at `spp = 40` (`c_k =
1, 2`, a 40-entry table, 16-symbol preamble: `hydra_profile.c:22-39`);
4- and 8-FSK (`bits_per_symbol` 2 and 3: the symbol map groups bits
MSB-first, `hydra_frame.c:31-46`; 316 coded bits become 158 or 106
symbols, the last zero-padded at 3 bits; the sync word becomes 8 or 6
symbols, likewise padded; tone `k` has `c_k = c_0 + k·Δ`). Language
needed: nothing new — a profile is a set of `firma` constants and a second
`sinus`; multi-bit symbols are two or three `datum`-style extractions.
`[OPEN]` forced: none, but D4's cost multiplies by the number of tables,
which is M5's evidence. Excluded by construction: any profile where
`sr/baud` is not an integer (44.1 kHz at 1000 baud), because D3's
memorylessness fails there and the reference's output is then a
`double` accumulation this design does not model; and finding 5's Python
"aux-cable", which is a different modem.

**M5 — array literals**, if M3's and M4's tables make the case D4 opens.
The three questions D4 lists are the decision's content; the evidence is
`sinus` (one table, nine values, eleven lines), M3's `cos`/`sin` per tone,
M4's per-`spp` tables, and M3's metric and traceback arrays, which are
not literals at all but zero-initialised storage — possibly a different
construct, and the design should not conflate them.

## 10. What retires each marker

| decision or claim | milestone | the test as planned | what retires it |
|---|---|---|---|
| D2 three frames byte-identical; D3 the walk; D4 the nine values; D5 extraction, encoder, interleaver; D6 views and `0 -% v`; D7 output and audit; D8 the layout | M1 | `tests/programs/hydramodem_{loopback,exemplum,vacuum}/`, `cmp` against `vendor/hydramodem-tx/*.wav`, audit `write` + `exit_group` | — |
| section 6's ten mutants failing at their offsets; the constant-0 CRC mutant passing | M1 | the harness's mutation run over `examples/hydramodem/` | — |
| finding 1: `discerne` traps | a compiler fix outside this tree | a `tests/programs/` fixture running a `discerne` from source; then `sinus` re-spelled | — |
| D9 claim 1 (affinity of the Exsecutor symbol stream) | M2 | 137/137 against the vendored basis | — |
| D9 consequence (all 2^136 inputs) | M2 | the same, plus M1's blocks | argued, never measured — as entry 23's |
| the survey's receiver figures `[UNREPRODUCED]` | M3 | the perturbation sweep | — |
| array literals `[OPEN]` | M5 | — | a spec amendment, if taken |
| `T[1] = 3849` and the three other dead values `[UNTESTED]` | M4 or never | a profile that reads an odd index | — |

## 11. What was measured for this document

All in a scratch directory, nothing committed, nothing on the build path;
figures re-measurable by re-running the same recipes.

- **`frame_tx` from source.** Built from `/home/asher/Documents/HydraMesh`
  at `fce2813` with `gcc -O2 -std=c99` against the ten reference sources
  (the vendoring commit's recipe, `-std=gnu11` there; same bytes). The
  three frames: 38,060 bytes each, sha256
  `f422db1d…a280bd`, `94763a11…74cd65`, `7fc0d246…92b293` — identical to
  `vendor/hydramodem-tx/PROVENANCE.md`'s.
- **The integer model.** A Python model of section 2 — the table as
  written there, the `(i+1)·c mod 48` walk, the mask-and-parity encoder,
  the gather interleaver, the header — produces the same three files,
  byte for byte (`first diff: None` on each). The nine live indices and
  the sixteen dead ones were enumerated by walking both tones; the
  quarter-wave identities checked on all 48 entries; the tap sets
  `(0,1,2,3,6)` / `(0,2,3,5,6)` checked equal to `0x79` / `0x5B` masking
  on random input; the feedforward `datum`/`codificatum`/walk
  formulation checked equal to the model on the three frames and 200
  random ones; every mutant of section 6 applied to the model and diffed
  against the unmutated model, including the four table entries and the
  constant-0 CRC; the 136 one-hot words' modem CRCs all nonzero; `0.02 ·
  48000` exactly `960.0`.
- **The compiler probes**, `exsc` built at `2da0040` (416,880 bytes) in
  `nix develop`, assembled with the vendored `fasmg`, run:
  - `p2`: `u1` bindings, `aut` on `u1`, a `u1` copied — runs, `41 42`.
  - `p3`: `0 -% 7633` in `u16` through `Exemplum { valor: u16:minor }
    sicut acies<u8, 2>` — runs, `2f e2`.
  - `p4`: `ge 0x80` / `sursum 1` over `0xd3` — runs, `11010011`.
  - `p5`: `sinus` as in section 7 (the `si`/`sin` spelling) for `m = 12, 2,
    36, 22` through the view — runs, `32 73  d1 1d  ce 8c  d1 1d`, which is
    `29490, 7633, −29490, 7633`.
  - `p6`: 38,060 calls of `scribe_octeto` in a `per` loop — runs, exit 0,
    38,060 bytes, 31 ms wall.
  - `p1`, `p1b`, `p1c`, `p7`, `p8`: `discerne` over `mensura`, `u8`, `u64`;
    assigning arms, empty arms, one arm plus `aliter` — each accepted
    without `-o`, each SIGILL (exit 132) with `-o`. Finding 1.
- **Not measured:** any complete Exsecutor transmitter; the affinity of
  the Exsecutor code (an argument, section 7); the receiver figures.
