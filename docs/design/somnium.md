# somnium — a screensaver engine in Exsecutor

Status: **written, `[UNTESTED]` as Exsecutor.** `examples/somnium/` and
`tests/programs/somnium_*/` were written in an environment that had no
`fasmg` (the proxy refused `flatassembler.net`, and running an unvetted
prebuilt binary was declined), so **no `exsc` has compiled these five files
and no binary built from them has run.** What *has* run is the independent
oracle, `prototypes/somnium_oracle.py`: it wrote every `expected.out` under
`tests/programs/somnium_*/`, and its frames were looked at (section 6). The
first `tests/run.sh` on a machine with the toolchain is the measurement this
document is waiting for. Where the sources lean on a construct, section 5
names the tree program that already exercises it, which is evidence that the
construct compiles and says nothing about whether this program does.

The consumer is Oligarchy's `custom.screensaver` (`modules/screensaver.nix`
there), which pipes the engine into a fullscreen viewer when hypridle
decides the session is idle.

## 1. What it is

One program, `somnium`, holding three effects ("somnia"):

| id | name | what | state across frames |
|---|---|---|---|
| 0 | `plasma` | four sine waves summed per pixel through a three-phase palette | none (a pure function of `t` and the seed) |
| 1 | `ignis` | heat-diffusion fire from two noise rows under the picture | the 160 × 102 heat field |
| 2 | `vita` | Conway's Life on a 160 × 100 torus, coloured by age, with fading trails | two 16,000-cell boards and the frame buffer |

It reads one request on stdin and writes frames on stdout: **raw rgb24,
160 × 100, row-major, no header, 48,000 bytes a frame.** The host tells its
viewer the geometry; mpv's `rawvideo` demuxer takes exactly this.

## 2. Why the engine is shaped like this

**No ambient time, no ambient entropy (spec §1, §9.3).** A screensaver is a
function of the clock and a random source in every other implementation.
Here time is the frame index the engine counts and the only entropy is a
32-bit seed in the request. So the same request writes the same bytes on
every machine: a screensaver can be pinned by a golden file, and is
(section 4). The host is free to seed from its clock; that is the host's
ambient state, entering through the one declared door.

**160 × 100, because of the runtime, measured from its source.** The only
way arbitrary bytes reach stdout is `Scriptor.scribe_octeto`, and
`compiler/x86_64/prelude/prelude.asm` implements it as one `write(2)` per
byte. A 320 × 200 frame would be 192,000 syscalls; 160 × 100 is 48,000. The
cost per frame at a given syscall latency is `[UNTESTED]` — no binary has
run — and it is the number that decides the default frame rate on the Oligarchy
side (20 fps there, a guess until measured). The fix is a prelude primitive
that writes an `acies<u8, N>` in one call; that is the `lower` agent's tree
(`compiler/x86_64/prelude/`) and a spec §4.6 surface change, and is
**reported, not made** here. 16:10 is also the Framework 16's own aspect, and
the viewer scales with nearest-neighbour, so the pixels are a look.

**Integers only.** Nothing here needs a float, and staying in integers
removes the rounding-mode question from the oracle entirely: two integer
programs either agree byte for byte or one of them changed a formula.

**What the language lacks and how the program does without it:**

| missing (spec §5.4, `[OPEN]`) | used instead |
|---|---|
| `*%` (wrapping multiply) | xorshift32 as the generator: xor and shifts only |
| remainder, bitwise and | `sicut u8` for mod 256 (narrowing is truncation); `(t8 sursum 5) deorsum 5` for mod 8 |
| integer division | `deorsum 2` for the fire's four-cell mean; `(b * 158) deorsum 8` to scale a byte into a range |
| sine, square root | a frozen 256-entry table; the plasma's rings are spaced by distance squared |
| signed arithmetic | `distantia(a, b)` = \|a − b\| on `mensura`; `t * 252` for `−4t mod 256` |

## 3. The request

17 bytes, then end of input:

| bytes | field | meaning |
|---|---|---|
| 0..4 | `"SOM1"` | magic, with the format version in its last byte |
| 4 | `somnium` | 0, 1 or 2 |
| 5..9 | `praetermitte`, u32 LE | frames advanced and not written (a warm fire, a board past its opening chaos) |
| 9..13 | `tabulae`, u32 LE | frames written; `0xFFFFFFFF` is "until the pipe closes" (6.8 years at 20 fps) |
| 13..17 | `semen`, u32 LE | the seed; 0 is xorshift32's fixed point and becomes `0x9E3779B9` |

Exit status: **0** every requested frame written; **1** the request was
refused (short, long, wrong magic, unknown somnium) and nothing was written;
**2** a frame was not taken. 2 is reachable only with SIGPIPE ignored — by
default a closed pipe kills the process at the write, which is how the host
stops a screensaver. The runtime's SIGPIPE behaviour is already `[OPEN]` in
`docs/design/runtime.md` and nothing here changes it.

The request is read whole, and checked for its end, **before a frame is
rendered**: a refused request writes zero bytes, which the four refusal
fixtures pin as "stdout empty".

Why a request on stdin rather than arguments: the engine's capability is
`ambitus`, the standard streams, and nothing else (spec §4.6). The process
arguments and environment are other authority, and this program declares
none. The syscall surface is therefore `read(0)`, `write(1)`, `exit_group` —
the same closure as `examples/signaculum/`.

## 4. The fixtures

All eight compile the same five files in the same order —
`somnium.exsc plasma.exsc ignis.exsc vita.exsc machina.exsc` — which is also
the order `flake.nix`'s `packages.somnium` compiles, so the unit tested is
the unit shipped.

| directory | request | checks |
|---|---|---|
| `somnium_plasma` | plasma, skip 37, write 2, seed `0x0BADCAFE` | the effect; that skipping a stateless somnium lands on the same `t` |
| `somnium_ignis` | ignis, skip 60, write 1, seed 1 | diffusion, cooling, column wrap, palette, 19,520 draws in order |
| `somnium_vita` | vita, skip 23, write 2, seed `0x5EED` | the fill, four injections (t = 0, 8, 16, 24), torus, age palette, trails |
| `somnium_semen_nihil` | plasma, frame 0, seed 0 | the zero-seed substitution |
| `somnium_ignotum` | somnium 3 | refused, exit 1, no output |
| `somnium_brevis` | 16 bytes | refused, exit 1, no output |
| `somnium_longa` | 18 bytes | refused, exit 1, no output |
| `somnium_magia` | `"SOM2"` | refused, exit 1, no output |

`tests/data/somnium_*.bin` are the requests (`prototypes/somnium_oracle.py
petitio ID SKIP WRITE SEED` writes one). The goldens are 96,000, 48,000,
96,000 and 48,000 bytes. None of these directories carries `cross=yes` or
`c-differentia=`: they are in the differential phase by default and not in
the cross phase. **The program-fixture floors in `tests/run.sh` were not
raised**, because a floor is set to a measured count and nothing was
measured; the eight directories only add to what the floor already demands.

**One finding before any Exsecutor ran, from the oracle.** The first design
of `vita`'s injection drew a spot as a raw byte and skipped it when the 3x3
would cross the edge. The oracle showed that on the fixture's seed **none of
the four attempts landed** — a raw byte is under 158 and under 98 about 24%
of the time — so the fixture would have pinned an injection path that never
ran. The spot is now the byte scaled into range by multiply and shift, and
every attempt lands. Written down because it is the "green check that
exercised nothing" shape this repository keeps meeting.

## 5. Constructs, and where the tree already runs them

| construct | used in | already exercised by |
|---|---|---|
| `&mutabilis acies<u8, N>` parameters, `(*p)[i]` read and write | every somnium | `examples/signaculum/forma.exsc`, `examples/hydramodem/auditus.exsc` |
| a function returning an array literal as the table | `sinus()` | `examples/hydramodem/modulator.exsc` (`tabula_sinus`) |
| `aut`, `sursum`, `deorsum` on `u32`/`u8` | `alea`, `vita` | `tests/programs/redundantia/`, `tests/ir/bitwise.ir` |
| narrowing `sicut` to `u8` | `octo`, the palettes | `tests/programs/angusta/` (u16 → u8); `mensura`/`u32` → `u8` is the same `trunc` and is `[UNTESTED]` from source until this runs |
| `per` with a runtime bound | `machina` | `examples/hydramodem/modulator.exsc` |
| `Lector.lege_octeto` with the 256 end sentinel | `machina` | `examples/signaculum/signaculum.exsc` |
| `Scriptor.scribe_octeto` in a loop | `scribe_tabulam` | `examples/pictura/pictura.exsc` |
| `[0; N]` locals, ~96 KB of them | `machina` | `examples/signaculum/` (786,432 + 2 MiB) |

Two things that could still refuse, stated so the first run knows where to
look: the same loop variable name in sibling `per` loops of one function
(`i` in `ignis_pinge`; `examples/signaculum/signaculum.exsc` does the same
with `i`), and a multi-line binary expression inside `redde`
(`verbum`; `examples/hydramodem/receptor.exsc:455` does it in an assignment).

## 6. What was looked at

`prototypes/somnium_oracle.py png ID SKIP SEED out.png` writes one frame as a
PNG. Plasma at frames 0, 40 and 120; fire at 20, 80 and 300 (at 80 the flame
reaches about 60% of the height and holds there); Life at 1, 60 and 400 (the
opening soup, then gliders and oscillators, with old cells blue and the
newborn white). Looking is not a test and is recorded as what it is.

## 7. Open

- `[UNTESTED]`: everything in `examples/somnium/` as Exsecutor (the status line).
- `[UNTESTED]`: frame time. It decides the default frame rate on the Oligarchy side.
- `[OPEN]`: a bulk-write prelude primitive, reported to `compiler/x86_64/prelude/`'s owner.
- `[OPEN]`: a fourth somnium that uses `acies<f32, 8>` would put the lane path
  (`examples/pictura/octonaria.exsc`) on screen; not started.
