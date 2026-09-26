# examples/metronomus/

A general-purpose **timer, clock and multiplayer tick layer**, written in
Exsecutor for two consumers:

- **[Kiln](https://github.com/ALH477/Kiln)**, the N64/host game engine, which
  links Exsecutor through the C backend (as it already does for the StreamDB
  reader and `kiln_soft3d`);
- **[Punctim](https://github.com/ALH477/Punctim)**, the DCF mesh, whose wire
  it speaks: the DCF-Game INPUT body, the DeModFrame's 24-bit microsecond
  timestamp, `unwrap_pid`, and PING/PONG's four timestamps.

`metronomus.exsc` is the library: pure, with no `poscit`, no `initium`, no
allocation and no I/O. `probatio.exsc` is the test driver.
`metronomus.h` is the C face for Kiln or any C host. `exemplum.c` is a
two-peer lockstep/rollback session in C. `exemplum_horologii.c` is the same
library as the firmware of a digital watch. `proba_c.sh` checks the C side.

## Why a clock library reads no clock

Reading the time is the `horologium` capability (spec §4.6, §11: *"Time is a
capability; time zones are data"*). The prelude has no `horologium` routine
yet. That isn't worked around here, because it doesn't need to be.

The host reads its own clock and passes the **difference** in as an integer.
That might be libdragon's `TICKS_READ()` at 46,875,000 Hz, `CLOCK_MONOTONIC`
in nanoseconds, a 48 kHz sample count, or Punctim's microseconds. Everything
in the library is a function of the readings it is handed. The same readings
give the same ticks on the console and on the PC.

That property is exactly what lockstep, replays and rollback need. A timer that
read its own clock could not provide it. Keeping the library pure is also what
lets its whole stream be certified byte-for-byte below.

## What it does

| # | Part | What for |
|---|---|---|
| 1 | `quotus`, `residuum`, `proportio` | Integer division by shift-and-subtract. The language has no integer `/` or remainder (§8.4, both `[OPEN]`). `proportio` computes `floor(n·a/b)` without forming `n·a`. |
| 2 | `multiplica_modulo` | Wrapping 64-bit multiply. `*%` is `[OPEN]` too. |
| 3 | `Metronomus` | The **fixed-step clock** (details below). |
| 4 | `hora_civilis`, `tempus_ex_hora` | **Wall clock.** Unix milliseconds plus a zone offset in minutes (data, passed in) become year, month, day, time of day and weekday, and back again: `tempus_ex_hora` turns a date and time the user set into milliseconds, and refuses one that does not exist (29 February 2027, 31 September, 24:00, second 60). The year is 16 bits: the range is the epoch through 65535-12-31, clamped at both ends, never trapping. |
| 5 | `Horologia` | **Sixteen timers** counted in ticks, one-shot or periodic. Fired as a slot-ordered bitmask, so every peer handles them in the same order. Periodic timers keep their phase across stalls. `pulsus_ex_millesimis` rounds up, so a timer never fires early. |
| 6 | `Mandatum`, `tempus24`, `revolve` | **Punctim's wire.** The DCF-Game INPUT body `tick u32 \| buttons u16` (big-endian, 6 bytes), the DeModFrame timestamp, and `unwrap_pid` generalised from 2¹¹ to any 2ᵏ (16 for `seq`, 24 for the timestamp). |
| 7 | `Consonantia` | **Clock sync.** Offset and round trip from PING/PONG's four instants. The offset is taken from the minimum-delay exchange of the last eight. The round trip is smoothed with an integer EWMA at α = 1/8, Punctim's own `udp_node.py` value. `pulsus_remotus` gives the server's tick now. |
| 8 | `celeritatem_elige` | **Time dilation.** Picks the Q8 speed that walks a client's tick back onto the server's without a visible jump: ±2/256 per tick of error, capped at ±6.25%. |
| 9 | `Consessus` | The **lockstep/rollback input buffer** (details below). |
| 10 | `alea_*`, `summa_misce` | **Kiln's `KilnRng`**, xorshift64\*, bit for bit, plus a per-tick seeded stream so a resimulated tick draws the same numbers. An FNV-1a fold is provided for desync checks. |

**`Metronomus`, the fixed-step clock.**

- It works over any source frequency below 2³¹ Hz and at any integer tick rate.
- It is **exact**. The accumulator is rational (source units × Hz × 256), so 60 Hz over nanoseconds never drifts.
- A catch-up cap makes it slip rather than spiral. Dropped ticks are counted.
- It supports pause and a Q8 time scale.
- It gives a Q16 render-interpolation fraction.
- Its state is a 40-byte `@transitus` record: **packed and big-endian by the language's definition**. The same bytes mean the same clock on the big-endian console and a little-endian PC.

**`Consessus`, the lockstep/rollback input buffer.** It holds 8 players × a 64-tick window.

- `lege` hands back either the real input or a *recorded* repeat-last prediction,
  or `nullus` when no answer can be right (no such player, a tick 64 or more
  ahead, or a tick whose slot has been reused). `nullus` is a fault, never
  buttons; `exemplum.c` exits on it.
- `inscribe` confirms real inputs. It advances the watermark below which every tick is final, which is what lockstep needs.
- When a real input contradicts the prediction that was already simulated, `inscribe` sets `revertendum`, the earliest tick to resimulate from. That is what rollback needs.
- Two different real inputs for one tick are refused (verdict 6) rather than silently overwritten.
- **The 64-tick rule.** A rollback must be collected with `revertendum_cape`
  and resimulated before the window moves 64 ticks past the tick it names, or
  the inputs it needs are gone and `lege` answers `nullus`. Collecting once
  per frame, as the demo does, is always soon enough.

## Using it from Kiln

The pattern is the one `examples/exsec-streamdb-demo/` in Kiln already uses:

```sh
build/exsc aedifica --hospes mips64-none-o64 --emitte c \
    examples/metronomus/metronomus.exsc -o metronomus_mips64.gen.c   # console
build/exsc aedifica --hospes x86_64-linux --emitte c \
    examples/metronomus/metronomus.exsc -o metronomus_x86_64.gen.c   # host
```

Add the unit to the ROM's `OBJS` with `-fno-fast-math`, because the unit
refuses fast-math. Include `metronomus.h`, and supply
`exsrt_abortus(unsigned)`. Then:

```c
static METRONOMUS_RECORD(clk, METRONOMUS_BYTES);
exs_metronomum_para(clk, TICKS_PER_SECOND, 60, 8);   // 60 Hz sim, <= 8 ticks catch-up
uint32_t last = TICKS_READ();
for (;;) {
    uint32_t now = TICKS_READ();
    uint64_t n = exs_pulsa(clk, (uint32_t)TICKS_DISTANCE(last, now)); last = now;
    while (n--) { kiln_input_update(); game_tick(); kiln_actor_update_all(1.0f / 60); }
    render(exs_fractio(clk) / 65536.0f);              // interpolation alpha
}
```

Three things it replaces in Kiln today:

- Kiln examples hard-code `dt = 1/60` and run the sim at vsync speed.
- `kiln_event` truncates 1/60 s to 16 ms per frame, so its timers run about 4% slow. `Horologia` counts ticks instead.
- `kiln_physics`'s sub-stepping rounds `dt/fixed_dt` instead of accumulating it.

For multiplayer, remote inputs enter through the tick the buffer returns and
Kiln's existing input-injection points: `kiln_input_play` and
`kiln_host_pad_set`.

Kiln's own `CLAUDE.md` notes that an N64 cannot be a direct network peer, so
the DCF link runs on the host tier or through a host-PC proxy. That transport
is **not** part of this library. `[UNTESTED]`: nothing here has run on N64
hardware or in a Kiln ROM build. What is measured is the o64 row's C
under qemu (below) and the host row in `exemplum.c`.

**Determinism caveat, from Kiln's side.** The library's arithmetic is all
integer, and its C unit refuses `-ffast-math`. Kiln's simulation code is
`float` and the console build enables fast-math globally. So bit-identical
*game state* across peers is a property the game has to earn. The tick
schedule, the inputs and the RNG are identical by construction; `summa_misce`
exists to catch the tick at which the game state diverges.

## Using it in a digital watch

Yes, for the timekeeping core, and that is measured, not argued.
`exemplum_horologii.c` is the library as watch firmware:

- a 32,768 Hz crystal whose RTC wakes the CPU eight times a second;
- a time of day and date **set with the buttons** (`tempus_ex_hora`), with an
  impossible date refused;
- an alarm, a 45-second countdown (`Horologia`), and a 1/100 s stopwatch with
  a lap time;
- the whole thing crossing midnight into a leap day, then running for one
  simulated day.

A crystal gives 327.68 counts per centisecond, which is not an integer. The
rational accumulator keeps the stopwatch exact: a day reads **24:00:00.00**.
A firmware that divides by 327 reads 24:02:59.66, and the transcript prints
both.

It builds **bare** (no libc, its own `_start`) from the 32-bit-address unit
for a **Cortex-M4** and a **Cortex-M0+**, the range most watch chips sit in.
It runs under `qemu-arm`, and the transcript is byte-identical to the PC's.
The whole firmware is about **8 KB of code and 452 bytes of RAM** on either
core. `proba_c.sh` repeats all of this.

The 32-bit-address row is named `mips64-none-o64`, but its C has no MIPS in
it. It asserts 32-bit pointers and reads `__BYTE_ORDER__`, and that is all.

On a v6-M part (M0/M0+) the compiler calls three 64-bit helpers
(`__aeabi_llsl`, `__aeabi_llsr`, `__aeabi_lmul`). Every toolchain's libgcc or
compiler-rt supplies them, as it does `__aeabi_memcpy`. The demo's bare build
carries its own copies only because this environment has no ARM runtime.

**What a watch still needs from elsewhere:**

- **Crystal trim.** A watch crystal is off by tens of ppm, and the Q8
  `celeritas` step (1/256, about 3,900 ppm) is far too coarse to correct it.
  Use the RTC's own calibration register, which is the normal way on
  STM32/nRF/SAM parts. Fine trim in the library is `[OPEN]`.
- **Daylight saving.** The zone offset is data the host supplies. Choosing it
  from DST rules is not here.
- **Power.** `[UNTESTED]`: no cycle count or current draw has been measured
  on hardware. The per-wake work is two `pulsa` calls, one `excita` and a
  compare. On an M0+, each `quotus` is a 64-step loop over 64-bit values
  built from 32-bit operations.
- **Hardware.** `[UNTESTED]`: nothing has run on a real microcontroller, only
  its instruction set under `qemu-arm` (A-profile user mode, running the same
  Thumb-2 and Thumb-1 code a Cortex-M would).

## Using it with Punctim

- **INPUT body.** `mandatum_scribe(tick, buttons)` produces DCF-Game message
  type 1's six bytes. Fragment it with Punctim's game adapter, 11:5, as a
  single data frame.
- **Frame timestamp.** Put `tempus24(tick_start_us)` in the DeModFrame
  timestamp.
- **Unwrapping.** `revolve(prev, raw, 11)` unwraps packet ids and
  `revolve(prev, ts24, 24)` unwraps timestamps.
- **Clock sync.** Feed `consonantiam_nota` the PING's `timestamp` (t0), the
  server's receive and send instants (t1, t2), and your receive instant (t3).
  PONG already echoes t0. Carrying t1 and t2 in the PONG body is a
  convention of this library, not something Punctim specifies today.

## Certificates

**External: Punctim's golden vectors.** `probatio.exsc` checks these in-program,
and its exit status names the first vector that fails:

- all 4 of `Documentation/game_vectors.json`'s `input_roundtrip`;
- all 8 of `Documentation/snake_vectors.json`'s `unwrap`.

Both are copied verbatim from Punctim `99baf3f`; the oracle's docstring records
the files' hashes.

**Independent: the whole 2,616-byte stream.**
`tests/programs/metronomus/expected.out` is written by
`prototypes/metronomus_oracle.py`, which computes everything its own way:

- Python's `//` and `%`;
- `Fraction` for the clock;
- `datetime` for the dates;
- a dict with an eviction rule for the ring;
- Punctim's JSON re-read with `--punctim DIR`.

The test runs it on the reference backend, both C compilers at two optimisation
levels, and **big-endian mips64 with 32-bit addresses under qemu**
(`cross=yes`, the console's row).

**Kiln's RNG, against Kiln's source.** `kiln_rng.c` from Kiln `20043e7`,
compiled beside the generated unit, agreed on 600,000 draws over six seeds,
including 0 and 2⁶³, with no mismatches. That was measured once, in a scratch
harness; it is not in the suite, because Kiln is not vendored.

**C side.** `proba_c.sh` checks:

- the watch demo, hosted under UBSan with gcc and clang at `-O0` and `-O2`,
  then bare on Cortex-M4 and Cortex-M0+ under `qemu-arm`: one transcript,
  byte-identical everywhere (`exemplum_horologii.expected`);

- the header agrees with the generated definitions, and a deliberately wrong
  prototype is a compile error;
- `exemplum.c` runs clean under UBSan with gcc and clang at `-O0` and `-O2`,
  and its transcript is byte-identical to `exemplum.expected`: two peers,
  240 ticks, 66 and 100 rollbacks, and **240 of 240 tick checksums agreeing**
  at the end.

**Mutants.** Nineteen mechanical mutants of `metronomus.exsc` were run against
the final stream (byte positions are into the 2,616-byte stream):

- **M1: unwrap exactly-half-modulus `le`→`lt`.** Survived at first, because
  none of Punctim's eight vectors sits on the boundary. Boundary cases were
  added, and it now fails at byte 1335.
- **M3: min-delay tie to the later slot.** Survived at first because the
  scenario had no tie. A tie was added, and it now fails at byte 1975.
- **M8 and the first M10 were equivalent mutants,** and so not failures of the
  test. M8 disabled a `quotus` special case for divisors ≥ 2⁶³ that cannot
  arise: after step *i* the remainder is below 2ⁱ⁺¹. The case was removed.
  The first M10 changed a guard whose body clamps to the same value; the
  re-aimed M10 fails at byte 2120.
- **M13 made the compiler crash** (see findings).
- **Every other mutant fails at a stated byte:**
  - M2, INPUT `claves` `:maior`→`:minor`: exit 10, byte 5.
  - M4, civil month: byte 802.
  - M5, prediction not recorded: byte 2240.
  - M6, slips not counted: byte 528.
  - M7, periodic re-arm from now: byte 1232.
  - M9, EWMA α 1/4: byte 1415.
  - M11, `quotus` `ge`→`gt`: byte 176.
  - M12, 23-bit timestamp: byte 1278.
  - M14, Q15 fraction: byte 351.
  - M15, xorshift shift 25→24: byte 2485.
- **Setting the time (W1–W4):** dropping the 400-year leap rule fails at byte
  897, a flipped zone sign at 917, admitting second 60 at 1001. W4 forgot that
  September has 30 days; it survived until 31 September was added as a case,
  and now fails at byte 1033.
- **The follow-up's guards (F1–F5), against the 2,714-byte stream:** removing
  the year-65535 clamp, the zone-overflow guard, `revolve`'s width guard,
  `imum`'s 64-bit case, and `lege`'s `nullus` for a tick beyond the window
  each fail at a stated byte (the commit message lists them).

## Findings

**Compiler crash, reported rather than fixed.** The lowering is not this
directory's tree (CLAUDE.md's Scope). A `per` loop whose body *ends* in an
unconditional `rumpe;` makes `exsc` die with SIGILL, on both backends. The
front end accepts it.

```exsecutor
publica functio f(n: u64) -> u64 {
    per i in 0..4 {
        rumpe;
    }
    redde n;
}
```

`exsc aedifica --hospes x86_64-linux --emitte c f.exsc -o f.c` exits 132. This
library never writes that shape.

**Limits stated, after the first review of PR #1:** `hora_civilis` clamps
instead of wrapping the 16-bit year or trapping on a zone add near 2⁶⁴;
`revolve` answers `nullus` for a width outside 1..63 and `imum` answers 0
and `x` at the two ends; `lege` distinguishes "no answer can be right" from a
prediction. Each has a case in the stream.

**Language gaps met, and not worked around silently:**

- There is no integer `/`, remainder or `*%`, so `quotus` and
  `multiplica_modulo` exist. A 64-step `quotus` costs more than a `div` would.
  Nothing here is hot enough to matter: at most a handful of calls per
  `pulsa`.
- An `&mutabilis` borrow does not coerce to a `&` parameter; `paratus(*c, …)`
  is how `inscribe` calls it.
- Plain structures are laid out **packed**, not naturally aligned: `Specimen`
  is 17 bytes. The records a C host allocates are ordered largest-field-first,
  so the two layouts agree anyway, and `metronomus.h` states each size.

**Upstream, noted for the owners:**

- Kiln's `kiln_rng.h` says a zero seed becomes 1, but `kiln_rng.c` makes it
  `0x9E3779B97F4A7C15`. This library follows the code.
- Kiln's `kiln_event` runs about 4% slow at 60 Hz, as above.
- Punctim's C game reassembler (`codec/demod_game.h`) stores each arriving
  fragment's timestamp over the last one. The spec says all fragments of a
  message carry the same timestamp, and the reassembler never checks that they
  do. That is harmless from a conforming sender; the unwrap above depends on
  the value being the send time.
