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

### The Latin

Checked, and it holds: `silentio`, `signo`, `codice` are correct ablatives
after `ex`; `nascitur` is a proper deponent; `fit` is the right passive of
`facere`. That matters more here than in most projects — a language whose
thesis is Latin morphology cannot afford a canonical program with bad Latin in
it.

`forma` in the second line is also, now, a reserved word (§8.4). Inside a
string literal that is no collision, but it is a coincidence worth having
noticed rather than discovered later.
