# examples/tempus/

The **time register**: one 17-byte DeModFrame that carries a node's held time
onto the [Punctim](https://github.com/ALH477/Punctim) wire, and the read of
one that arrives. What a face shows as `META:TEMPUS`.

`tempus.exsc` is the library: pure, no `poscit`, no `initium`, no allocation,
no I/O. `probatio.exsc` is the test driver.

## What it is built on

The unit is four files, in this order:

1. `tests/conformance/entry23_demodframe_golden_vectors.exsc`: the
   `DeModFrame` declaration, spec §5.2 verbatim.
2. `tests/conformance/entry23/codex.exsc`: the codec §14 entry 23 certifies
   against every one of the 246 vendored Punctim vectors.
3. `tempus.exsc`, the register.
4. `probatio.exsc`, the driver.

So the register **restates nothing about the wire**: not the CRC, not a byte
order, not a field width. Writing `f.tempus = …` is the big-endian 24-bit
store because the declaration says so; sealing is the codec's `obsigna`; the
verdict on an arriving frame is the codec's `lege`. A register that carried
its own copy of the codec would be certified by whatever it checked itself
against. This one is certified by what the codec is.

## What it does

| Function | What for |
|---|---|
| `para(fons, meta, genus)` | A register for node `fons` in room `meta`, sending frames of type `genus`: 2 (BEACON, Punctim's clock/broadcast type) for a clock, 0 (DATA) if the host says so. |
| `concede`, `adeo` | **Time is a capability.** The host that holds `horologium` passes `adeo` a microsecond difference; while the register is released (`concede(r, 0)`) the difference is discarded, the twenty-four bits stand still, and there is no ambient clock to catch up to when it is granted again. |
| `migra` | Move to another room. It moves the room and nothing else. `0xFFFF` is the lobby, Punctim's broadcast channel. |
| `signa(r, onus)` | Seal one frame. `numerus` is the frame's sequence number, stepped after each seal: that is what `seq` means on the wire, one per frame sent, not a second clock. |
| `tempus24`, `aera` | The wire's 24 bits, and how many times they have wrapped: the part the wire does not carry. |
| `accipe(w, prius)` | Read an arriving frame: the codec's verdict (0 valid, 1 bad sync, 2 bad version, 3 bad CRC), and on a valid one the fields plus the sender's held time made **absolute** by unwrapping against the last value read from that sender. A face can show an era it was never sent. A bad frame yields its verdict and nothing else. |
| `vitia(w)` | A copy of a sealed frame with one bit of `onus` flipped and the CRC left alone: what a disagreeing frame looks like. A pure function of a frame, not a flag on the register. |

## Where this came from, and what is different

A Grok Build workspace proposed `examples/tempus/` as a C library
(`tempus.c`, `tempus.h`, a `proba.sh`) with its own CRC and its own
byte-packing, checked against three CRC anchors. This directory is the same
idea done the way this repository does things:

- **In Exsecutor**, as an `examples/` program `tests/run.sh` compiles, runs
  and cross-compiles, not host C the suite never sees.
- **Over the certified codec**, not a re-implementation certified by three
  anchors.
- **`fons` is data.** The C version hard-coded `fons = 0x0007` as "the clock
  source"; no Punctim document assigns that. Here the host names its node.
- **`numerus` is a sequence number**, one per frame, as `seq` is on the wire.
  The C version stepped it every 50 ms of held time, a second clock in a
  field that is not one.
- **There is a receive path.** The C version could only pack. `accipe` reads,
  judges, and unwraps, so two faces can agree on an era neither was told.
- **Corruption is a function, not state.** A "corrupt" flag on a live
  register is a way to seal bad traffic by accident.
- **Refusals are stated.** A node, room or type that does not fit the frame,
  a grant value other than 0 or 1, a difference over 2³² µs (a host that has
  lost its clock), an `onus` over 2³²: each answers something the caller
  can see, and nothing traps.

## Certificates

**External, in-program.** The register must **seal, from its fields, two
frames whose bytes someone else published**: the vendored certificate's
anchor `d31312340001ffffdeadbeefab12cd24c0`
(`vendor/hydramesh-wire/golden_vectors.json`, `exampleFrame_full`) and the
frame HydraModem's own transmitter renders,
`d310123400a1ffffdeadbeef0a1b2ca961` (`vendor/hydramodem-tx/`). A mismatch
is the exit status, 10 or 11.

**Independent, byte for byte.** `tests/programs/tempus/expected.out` (1,601
bytes) is written by `prototypes/tempus_oracle.py`, which packs frames with
`struct.pack`, computes the CRC with a table, and unwraps with Punctim's
`unwrap_pid` formula verbatim. With `--wire` it first checks its own CRC
against every encode-basis vector and every anchor in the vendored JSON. The
suite holds the program's stream identical to it on the reference backend,
gcc and clang at two optimisation levels, and big-endian mips64 under qemu.

**Mutants**, eight, each against the stream:

- T1, `numerus` stepped before the seal: byte 247.
- T2, `adeo` ignores the grant: byte 338.
- T3, unwrap at exactly half a modulus `le`→`lt`: **survived** until a case
  half a modulus ahead was added; now byte 1374. The same gap
  `examples/metronomus` found in its own unwrap, met again.
- T4, `vitia` flips a CRC byte instead: byte 619.
- T5, `tempus24` keeps 23 bits: byte 863.
- T6, `accipe` reads fields out of a bad frame: byte 632.
- T7, `genus` not written: exit 10 (the published CTRL frame fails), byte 2.
- T8, `migra` moves `tempus` too: byte 450.

## Using it

From C, the way Kiln links Exsecutor: `exsc aedifica --hospes ROW --emitte c`
over the same four files gives one unit whose `exs_signa`, `exs_accipe` and
friends follow `docs/design/c-backend.md`'s ABI (`examples/metronomus/` shows
a hand-written header checked against such a unit). `[UNTESTED]`: no C host
has been built for this directory; what is measured is the four rows the
suite runs.

For the web face the workspace built, `src/lib/demod-frame.ts` already packs
the same bytes; what it lacks is this file's read path and its refusals.
