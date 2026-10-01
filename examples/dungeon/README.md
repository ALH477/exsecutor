# examples/dungeon/

A **64×64 dungeon chunk grown from one 64-bit seed**, written in Exsecutor for
[Kiln](https://github.com/ALH477/kiln), which links it through the C backend the
way it already links the StreamDB reader, `kiln_soft3d` and `fig_pose`.

Two modules, one unit. `furor_petabytorum.exsc` — *Furor Petabytorum*, "the madness
of petabytes" — is the seed: it turns (world, chunk index) into the 64-bit seed a
chunk is grown from, and back. `dungeon.exsc` takes a seed and grows the chunk. Both are pure: no `poscit`,
no `initium`, no allocation, no clock. `probatio.exsc` is the test driver.
`prototypes/dungeon_oracle.py` is the design, the independent reference, and the
measurements behind the design (`python3 prototypes/dungeon_oracle.py selftest`).

```
semina_furore(orbis, index)     -> u64      sow: the seed of chunk `index` in world `orbis`
desemina_furore(semen, orbis)   -> u64      un-sow: the chunk whose seed this is
misce_furore(z) / demisce_furore(z) -> u64  the splitmix64 finaliser and its inverse
semina_plano(orbis, x, y)       -> u64      the seed of the chunk at coordinates (x, y), u32 each
desemina_plano(semen, orbis, xy) -> u8      xy[0], xy[1] = the (x, y) whose seed this is
fig_dungeon_chunk(w, stats, semen)  -> u64      fills w[0..4096); returns walkable tiles
fig_dungeon_prune(w, start)     -> mensura  the connectivity guarantee, public so it can be tested
fig_dungeon_crc32(w)            -> u32      zlib's crc32 of w[0..4096)
```

**The names.** `semina` is the imperative of *semino*, to sow; `misce` of *misceo*, to
mix; `de-` is §3.5's reversal, which is why `desemina_furore` takes the seed before
the world. After the `_` is §3.1's one qualifier, an ablative: `furore`, "by
madness". The lexicon pass is not enabled and §3.3's table has no `semin-` or
`misc-`, so these names are unchecked, and entering them is §3.9's job when
`lexicon.norma` exists; the code does not depend on any of it, and a rename is a
find-and-replace. The parameter is `orbis`, not `mundus`: `Mundus` is the
capability type every `initium` receives, and a world seed is not authority.

`w` is one caller-owned `acies<u8, 12288>`: the chunk at `[0, 4096)`, the
cellular automaton's second buffer above it, and the prune's stack reusing that
space after. Tile codes: 0 wall, 1 floor, 2 door, 3 trap.

**Coordinates.** A game finds a chunk by where it is. `semina_plano` packs
`(y << 32) | x` into the index and calls `semina_furore`, a bijection of u32 × u32
onto u64, so every chunk of a 4,294,967,296-per-side plane has its own seed.
The coordinates are unsigned; a world centred on the origin passes `c sicut u32`
for a signed chunk coordinate (a bijection that never traps), so (−1, 0) is
(0xFFFFFFFF, 0). They are `u32` and not `i32` so the C ABI stays natural: the C
backend holds an `iN` sign-extended in its `uint64_t` carrier, and a C caller
passing a zero-extended negative would be handing it a non-canonical value. The
first petabyte (10¹⁵ bytes) is not a square — 244,140,625,000 chunks is 494,105.9
a side — but **2¹⁹ = 524,288 chunks a side is exactly one pebibyte, 2⁵⁰ bytes**,
which `probatio.exsc` computes. Tested at the origin, one step on each axis, the
far corner of that square, the far corner of the plane, the signed extremes and
(−1, 0), in two worlds.

## Where this came from, and what was checked

The design began as a sketch: a 64-bit seed, xorshift, twelve rooms, four
smoothing passes, about 7.55 ms. Taking it to code checked each claim. Some held
and some did not.

**Held.** 10¹⁵ bytes ÷ 4,096 is exactly 244,140,625,000 chunks (the division is
exact, `probatio.exsc` computes it and the oracle recomputes it with bigints). A
32-bit seed indexes 4,294,967,296 of them, too few; a 64-bit seed's 2⁶⁴ is
75,557,863 times the number needed. Xorshift is three shifts and three xors and
needs no multiply, which is also what the language can say natively.

**Needed a correction: "unique".** 2⁶⁴ seeds are *enough*, but distinct chunks
over a whole petabyte are only guaranteed if the seeds are distinct, and seeds
*drawn at random* from 2⁶⁴ collide: for n draws the expected number of colliding
pairs is about n²/2⁶⁵, which for 244,140,625,000 draws is **about 1,616**. So the
chunk's seed is not drawn. `semina_furore(orbis, index)` is
`mix(orbis + index × γ)`, a composition of bijections of u64 (γ is odd, and the
mixer is two xorshift-rights and two odd multiplies), so for one world every
index below 2⁶⁴ has a different seed *by construction*. `desemina_furore` runs it
backwards, in Exsecutor, and `probatio.exsc` checks the round trip for every
(world, index) of its scenario (exit 5); the oracle does the same in Python. Distinct seeds are not
distinct layouts in general; over 3,000 chunks the oracle found no duplicate
layout, which is evidence and not a proof.

**Needed a correction: the cost.** The sketch counted 48 draws (twelve rooms by
four values). A chunk takes about **1,050** — the roughening pass draws once per
boundary tile, about 900 of them — and the cellular automaton is five 3×3
passes, not one. The sketch's 5 cycles per cell evaluation is a guess, and the
measurement below is larger than the whole sketched total.

## Measured, and what it is not

One chunk (seed `0x123456789`), `clang -O2 -march=mips3 -mabi=n32`, big-endian,
counted by `qemu-mipsn32 -one-insn-per-tb -d exec`:

| | instructions |
|---|---|
| the whole chunk | **2,483,730** |
| `muri_circum` (the 3×3 wall count, five passes plus doors and traps) | 1,512,506 (60.9%) |
| `leniga` (the automaton's pass) | 421,697 (17.0%) |
| `fig_dungeon_chunk` itself | 155,158 |
| `empuja` and `fig_dungeon_prune` (the prune's DFS) | 205,425 |
| `proximus` (the stream) and `asperge` (the roughening) | 188,928 |
| `fig_dungeon_crc32`, run separately | 174,002 |
| `semina_furore`, one call, for comparison | 91 |

At one instruction per cycle on a 93.75 MHz VR4300 that is **no less than
26.5 ms** for this build. It is a floor, not an estimate, and for this compiler
only: Kiln builds with `mips64-elf-gcc -Os -mabi=o64 -march=vr4300`, whose
instruction count will differ, and a real run adds cache misses, RDRAM latency
and multiplier stalls that this does not model. **Nothing here has run on a
VR4300 or in Ares.** `[UNTESTED]` is the honest status of every time claim,
including that floor. What the count does say is where the time goes: the
neighbour count is the first thing to optimise, by a wide margin.

## The design, in the order it runs

The oracle's docstring has each step's reasoning; in short:

1. **Seed.** `mix(world + index·γ)`. The mixer at γ reproduces splitmix64's
   published first output, `0xE220A8397B1DCDAF` (checked by `probatio.exsc`).
2. **Stream.** xorshift64 (13, 7, 17); `[0, n)` by multiply-shift on the high
   word, no division. Zero is the generator's fixed point, so a zero seed is
   replaced by γ — exactly one index per world, for world 0 index 0, and that
   chunk is the same layout as the seed γ (a probatio check).
3. **Rooms.** A real BSP, flattened to arrays: split the largest leaf on its
   longer side until there are twelve; one room per leaf. Every leaf side stays
   at least 6, by induction (the oracle's docstring) and measured (≥ 7 over 3,000
   chunks), so a room of 4×4 or more with a tile of margin always fits.
4. **Roughen and smooth.** Boundary tiles flip with probability ¼; four passes
   of "wall if ≥ 5 of 8 neighbours are wall, floor if ≤ 3".
5. **Corridors,** after the smoothing, so a one-wide corridor is not eroded by a
   rule that cannot tell it from noise. Each leaf joins the one it was split from.
6. **Prune.** Floor not 4-connected to room 0's centre becomes wall.
7. **Doors** (a one-wide passage with an open area at one end) and **traps** (ten
   tries, floor tiles whose eight neighbours are all passable).

**The prune is rare, and necessary.** The corridors already join all twelve room
centres, so the prune only ever removes a fragment the smoothing split off a
room. An earlier draft of this README, written from 3,000 seeds, said it
removed nothing, ever; **that was wrong.** Over 2,000,000 chunks
(`tests/c/dungeon_scan.c`, run over the emitted C) it fires in **283** — about
one in 7,000 — removing 1 to 5 tiles. Disabling it makes exactly those chunks
disconnected: in the first 100,000, the 11 chunks it prunes are the 11 that an
independent flood fill then finds in two pieces. So a test that compares twenty
seeds will rarely meet it. `probatio.exsc` therefore also runs it on a grid built
to have islands, one touching the main region only at a corner, and the stream
includes two chunks where it fires (it removes 1 tile in one, 5 in the other). Two
of the thirty-two mutants of the generator are caught by nothing else: one that
also walks diagonals, caught only by the synthetic grid, and one that removes the
prune call, caught only by those two chunk records — the other twenty records
are byte-identical with it disabled.

**Measured at scale** (2,000,000 chunks of world 0, indices 0 to 1,999,999, the
emitted C at `-O2`, `tests/c/dungeon_scan.c`): every tile is 0 to 3; every border
is solid; every returned walkable count equals a recount; every chunk is exactly
one 4-connected component by a flood fill written in C, independent of the
prune; no two chunks have the same layout (a 64-bit hash of each, no duplicate).
Walkable tiles range 596 to 2,399. The oracle and the emitted C agree on 611
sampled chunks, 11 of them where the prune fires. These are measurements of a
generator, not proofs about it: the proofs are the bijection of the seed and the
leaf-size induction.

## What is not here

- **No chunk-to-chunk joins.** Each chunk is a closed 64×64 with a solid
  border; a world is a lookup of chunks by index, not a continuous map.
- **No geometry.** The output is tile codes. Turning them into walls is the
  embedder's job.
- **No time claim**, as above.
- **No relation to `fig_rng`.** Kiln's `fig_rng` is xorshift64\*; this stream is
  deliberately separate. A world generated from a seed is a file format, and it
  must not change if the engine's RNG is ever retuned.

## Checking it

```sh
make all                                    # build/exsc
python3 prototypes/dungeon_oracle.py stream -o /tmp/expected.out
build/exsc aedifica --hospes x86_64-linux examples/dungeon/furor_petabytorum.exsc \
    examples/dungeon/dungeon.exsc examples/dungeon/probatio.exsc -o /tmp/p.asm
fasmg /tmp/p.asm /tmp/p && chmod +x /tmp/p && /tmp/p | cmp - /tmp/expected.out
```

`tests/programs/dungeon/` holds the stream against the oracle on the reference
backend, all four hosted C builds and the big-endian mips64 qemu run; its `TEST`
header lists the thirty-two mutants and where each is caught.
