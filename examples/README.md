# examples/

Exsecutor source. **Nothing here compiles** — there is no compiler (spec §16,
Stage 1). These are the language as specified, kept where they can be read and
checked by hand against the spec.

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

`[OPEN]`, found by writing this: **§4.6 has no capability for standard
output.** The set is `Mundus, alloc, sermo, horologium, archivum, rete,
fortuna, ambitus, Filum, machina, Crudum`. Writing to a terminal would have to
borrow `archivum`, which over-grants — "can write to your screen" and "can
touch your filesystem" are different questions, and §10.3's audit is worth less
when they collapse. Row-polymorphism dodges it here; a concrete stdout writer
still has to declare something.

### The Latin

Checked, and it holds: `silentio`, `signo`, `codice` are correct ablatives
after `ex`; `nascitur` is a proper deponent; `fit` is the right passive of
`facere`. That matters more here than in most projects — a language whose
thesis is Latin morphology cannot afford a canonical program with bad Latin in
it.

`forma` in the second line is also, now, a reserved word (§8.4). Inside a
string literal that is no collision, but it is a coincidence worth having
noticed rather than discovered later.
