# examples/onus/

**Onus Oligarchiae** — *the burden of the oligarchy* — the theme song of
[Oligarchy](https://github.com/ALH477/Oligarchy), written as a program. The
program writes the score as a Standard MIDI File; Oligarchy's FAUST
orchestra (`assets/onus-oligarchiae/` there) performs it.

A march in 5/4 in G, after Holst's *Mars, the Bringer of War*: a col legno
ostinato hammering the Mars rhythm (triplet, two quarters, two eighths, a
quarter) on a G pedal, and brass that climb a semitone a bar in fifths with
a minor ninth on top. It is also a whimsical piece. A jester's piccolo is
loose in the court. A bassoon keeps trying to march in 3 + 2. At the end,
after the last hammered chord, there is a bar of silence, and the jester
gets the last word before the final G.

| bars | section | what happens |
|---|---|---|
| 0–3 | *exordium* | the ostinato alone, pp to p, timpani on the downbeat |
| 4–11 | *processio* | the brass arrive: bare G, the flat second, D-flat major over the G pedal, D7 |
| 12–19 | *scurra* | the jester's tune over oom-pah-pah-oom-pah; a C-minor frown at bar 17; the bassoon walks off down a D7 |
| 20–27 | *onus ascendit* | ostinato, brass, bassoon and timpani climb G to D a semitone a bar; the piccolo squeaks above them |
| 28–33 | *culmen* | G, A-flat, G, G-flat, G minor, D7 at full weight, timpani rolling |
| 34–39 | *coda iocosa* | two hammered bars, a bar of silence, the jester's *ta-da*, the final G, broadening from 150 to about 111 |

Forty bars at 150 to the quarter: 81.7 seconds before the room's tail.

## The files

- `partitura.exsc` — the score, pure data: per-bar tables for the three
  bar-shaped sections (ostinatum, aes, tympanum), step lists for the two
  melodic ones (scurra, fagottus), and the track names.
- `onus.exsc` — the engraver and the `initium`: a Standard MIDI File, format
  1, six tracks, 96 ticks a quarter, on standard output.
- `onus_oligarchiae.mid` — the golden file: 7,897 bytes, written by
  `prototypes/onus_oracle.py`.

```sh
build/exsc aedifica --hospes x86_64-linux \
  examples/onus/partitura.exsc examples/onus/onus.exsc -o onus.asm
fasmg onus.asm onus && chmod +x onus
./onus > onus.mid
cmp onus.mid examples/onus/onus_oligarchiae.mid
```

The file also plays on any General MIDI synth. Each track sets a GM program:
pizzicato strings, trombone, piccolo, bassoon, timpani.

## How it writes MIDI without an event list

A sequencer usually expands every note into absolute-time on and off events,
sorts them, and delta-codes the result. The oracle works that way. This
program never holds an event: it walks each track once as a stream of
*steps*. A step is up to three pitches with a length and a sounding length.
It carries the delta it owes, and the note in hand, in a `Calamus` (a pen)
passed as `&mutabilis`. A note-on pays the debt. The note-off owes the
sounding length. The rest of the step becomes the next debt. A rest only
adds to the debt. `finis` pays whatever is left, so every track ends at tick
19,200.

An MTrk chunk's length comes before its body, so each track is walked twice
through the same pen: once with `vere = 0` (counting, one per byte), then
with `vere = 1` (writing). The two walks see the same score, so the count is
the length.

Like the hydramodem transmitter, it uses no division, no remainder, no
bitwise and or or, and no signed integer:

- a VLQ group's low seven bits are `x - ((x deorsum 7) sursum 7)`;
- a big-endian field is its shifts, each narrowed by `sicut u8`, which keeps
  the low eight bits (spec §5.4);
- the articulations are shifts: col legno sounds half its length
  (`d deorsum 1`), most notes three quarters, and the bassoon seven eighths.

## Evidence, exactly

`tests/programs/onus_oligarchiae/` passes in CI (commit `84db83d`, the
`check` job running `tests/run.sh`). On the reference backend the program
compiles and assembles, exits 0, writes the golden file byte for byte, and
stays within `{Mundus, ambitus}`. It is eligible for the C backend's
differential phase, so the same job held its gcc and clang builds, at -O0
and -O2, to the same bytes; the gcc pair was also run locally.

The first draft was written without `fasmg` and did not compile. The
compiler was right both times:

- `varius(s, c, (*c).mora)` passes the pen and a field of it to one call,
  which is an aliased mutable argument (`EXS-E0310`). The delta is now
  copied out first.
- `gradus` and `tactus` took 9 and 10 arguments, but the Tier 1 emitter
  passes six. So the note in hand (channel, pitches, velocity) now rides in
  the pen, set by `tene`.

Also run:

- the oracle, which also asserts what the stream form takes on faith: every
  track fills exactly 40 bars, and no pitch overlaps itself on its channel;
- before the program compiled, a mechanical Python transliteration of the
  first draft, with eight mutants recorded in the TEST header. Seven were
  caught. The survivor is the VLQ's third group, which this score never
  reaches, because its longest delta is 5,760 ticks. The mutants have not
  been re-run against the compiled program.

Not opted in: the big-endian `cross=yes` phase.
