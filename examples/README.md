# examples/

Exsecutor source, two programs, both compiled and run by `tests/run.sh`:

- **the hello world** — `saluta.exsc`, `imprime.exsc`, `initium.exsc`,
  below; `tests/programs/saluta/` and `tools/publish-gate.sh` run it;
- **`hydramodem/`** — a transmitter for HydraModem's acoustic modem that
  writes, byte for byte, the WAV HydraModem's own reference transmitter
  writes for the same frame. `hydramodem/README.md` has how to build and
  run it; `tests/programs/hydramodem_*/` compare its output with the
  vendored reference WAVs.

This file said "nothing here compiles — there is no compiler" until
2026-09-10, and then, until the hydramodem commit, that nothing here was
type-checked or compiled to code, which the hello world's test had already
made false.

## `saluta.exsc`

The canonical program. Two things about it are deliberate.

**It does not print.** I/O is authority and authority is declared, so a
greeting-producer that returns `textus` needs no `poscit` clause at all. The
empty row is the point: this program is provably incapable of allocating,
reading a clock, touching the filesystem, or reaching the network. In most
languages hello world performs I/O; here the first program demonstrates §4 by
being unable to.

**It is a multiline string literal**, which is what settled §8.4's rule that a
literal may contain raw newlines with no indentation stripping. Stripping would
make the value depend on the source's leading whitespace — the ambient-state
class §1 exists to remove.

### Two things it exposed

`saluta` sent me to §3.4, and §3.4 was wrong. The table gave the `functio`
suffix as `-e`, a spelling; the real rule is the present-stem imperative, which
is `-e` in the third conjugation (`lege`) and **`-a` in the first** (`saluta`,
and the spec's own `plica_unicode` and `applica`). Read literally, the table
rejected most of the spec's own function names. Corrected in §3.4.

`nocens` and `exterior` are still unresolved against §3.4 and are marked
`[OPEN]` there rather than excused — `exterior` ends in `-or`, which §14 entry
14 makes an `EXS-E0602` when declared `functio`.

## `saluta.expected` and the publish gate

`saluta.expected` is the 101 bytes `saluta()` must return, extracted from the
literal and checked against it character for character. No trailing newline —
the literal ends on the period, and a golden file that quietly added one would
be testing something else.

`tools/publish-gate.sh` is the condition for making this repository public,
written as a command rather than a judgement: **the hello world working and
proven.** Those are two claims, so it checks both. *Working* is that `exsc`
compiles `saluta.exsc` and the result runs. *Proven* is that it emits exactly
these bytes, reproducibly across directory, `TZ` and locale (§9.3), with every
existing check still green.

It exits 1 today and says why. Running the hello world needs the frontend
(Stage 1, built), the type checker (Stage 2) **and** the reference backend
with its runtime prelude (Stage 3). The gate is written now so that the day it
turns green is unambiguous, and so nobody has to decide by feel whether the
hello world "works."

Since §12 let `SOURCE` repeat, the gate compiles the three files together —
`saluta.exsc imprime.exsc initium.exsc` — and assembles the emitted text with
`fasmg`, the one tool in the closure (§18.1); `exsc` never assembles.

## `imprime.exsc`

The companion. `saluta` cannot print; this is what printing looks like when
authority has to be declared.

`imprime_gutenbergio` — `imprim-` + `-e`, the present-stem imperative of
`imprimere` (*to press into*), qualified by the ablative `Gutenbergio`. The
`_`-qualifier shape is not invented: §10.1 already writes `plica_unicode` and
`plica_sermone`, with `sermone` in the ablative exactly as `Gutenbergio` is.

**§3.9 refused the obvious version, and was right to.** The request was
Gutenberg's name on a print function. §3.9.1's exhaustion test admits a new
root only after showing the concept cannot be expressed from the existing
table — and `imprimere` is attested classical Latin, rung 1. So Gutenberg
cannot be the root for *print*. It enters only as an eponym, `Gutenbergius`,
rung 2 (Neo-Latin scientific, the `Copernicus`-for-Kopernik tradition), which
is the one thing no composition of roots yields. Proposal in
`prototypes/lexicon/coinage-0001-gutenbergius.md`; **proposed, not registered**
— §3.9.4 needs a reviewer who is not the proposer.

That proposal is §3.9's first exercise since it was written. It found that the
ladder is easy and the collision check is the part needing tooling that §3.9.7
does not yet have. Note that §3.8 and §15 #4 keep their `[UNTESTED]` markers:
a coinage that is proposed but unreviewed does not retire them.

### The row is the interesting part

`poscit sicut s` — not `poscit archivum`. Authority comes from the `Scriptor`
handed in, so the function requires exactly what its writer requires. A
file-backed writer needs the filesystem; an in-memory one needs nothing. This
is §4.2's rule after the amendment that made rows travel with a value's *type*,
and it is why the function does not have to guess.

Found by writing this, and since closed: §4.6 had no capability for
standard output, and borrowing `archivum` would over-grant — "can write to
your screen" and "can touch your filesystem" are different questions, and
§10.3's audit is worth less when they collapse. §4.6 now places the standard
streams under **`ambitus`**, the process environment, which is where a
process gets them from. No new atom was spent.

## `initium.exsc`

The third file, and the only one that can run: the entry point §4.7 pins,
`publica functio initium(m: Mundus) -> u8`. Its whole authority is the one
`Mundus` parameter; `ambitus` is derived from it and bound with `sub`, and
`Scriptor.ad_exitum` turns it into the writer `imprime_gutenbergio` needs.
`[UNTESTED]` — it parses and builds a typed AST; the `Scriptor` it names is
the runtime prelude's, pinned in `docs/design/runtime.md`.

### The Latin

Checked, and it holds: `silentio`, `signo`, `codice` are correct ablatives
after `ex`; `nascitur` is a proper deponent; `fit` is the right passive of
`facere`. That matters more here than in most projects — a language whose
thesis is Latin morphology cannot afford a canonical program with bad Latin in
it.

`forma` in the second line is also, now, a reserved word (§8.4). Inside a
string literal that is no collision, but it is a coincidence worth having
noticed rather than discovered later.

## `hydramodem/`

The second program: HydraModem's transmitter. Six files, one compilation
unit per frame — `quantum.exsc` (the frame's type and its CRC),
`modulator.exsc` (the transmitter, pure: no `poscit`, no capability),
`emitte.exsc` (the writer) and one of three drivers, each an `initium`
holding one frame as a `DeModFrame` struct literal. Only the driver names
`Mundus`. `hydramodem/README.md` says how to build it, what it computes and
what three frames do and do not prove; `docs/design/modem.md` is the design.

It is written without bitwise and or or, division, remainder, signed
arithmetic or array literals, none of which the language has settled. Its
sine table is a `discerne`; designing it found that no `discerne` had ever
compiled with `-o`, and that was fixed first (`tests/programs/discerne/`).
