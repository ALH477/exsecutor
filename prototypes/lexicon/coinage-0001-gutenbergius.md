# Coinage proposal 0001 — `Gutenbergius`

**Status:** proposed, unreviewed. §3.9.4 requires a reviewer other than the
proposer; that has not happened, so this is **not registered**.
**Shape:** as required by spec §3.9.3.

## 1. The concept, and the exhaustion argument (§3.9.1)

The request was a print function naming Gutenberg.

**The exhaustion test refuses a new root for "print", and that refusal is
correct.** Latin has `imprimere` — *to press into, to imprint* — attested
classical, `imprimo / imprimere / impressi / impressum`, third conjugation.
Present stem `imprim-`, supine stem `impress-`. Rung 1 of §3.9.2 succeeds
outright, so no coinage for the action is admissible. §3.9.1 puts the burden on
the proposer and it is not met.

What is *not* expressible from the existing table is the **eponym**. Gutenberg
is a proper name, not a concept, and no composition of roots yields it. That is
the only thing this proposal coins.

## 2. Candidates considered, with rungs

| candidate | rung | verdict |
|---|---|---|
| `imprim-` for the action | 1 — attested classical | **adopted**; blocks any coinage for "print" |
| `Gutenbergius` (2nd decl. eponym) | 2 — Neo-Latin scientific | **adopted** for the eponym |
| `gutenberg-` as a bare root | 5 — marked loan | **rejected**: rung 2 succeeds, so rung 5 is inadmissible |
| `typograph-` (Greek) | 3 | rejected: names the trade, not the person; and rung 1 already covers the action |

Rung 2 is the right rung and is not a stretch. Latinising Germanic surnames in
`-ius` is exactly what four centuries of scientific Latin does — `Copernicus`
for Kopernik is the canonical case. Declension: `Gutenbergius, -ii`, m.,
2nd declension. Genitive `Gutenbergii`, ablative `Gutenbergio`.

## 3. Stems

`Gutenbergius` is a **noun, not a verb**, so it carries no present/supine split
(§3.3's two-stem rule governs verbal roots). Stem `Gutenbergi-`.

## 4. Collision check (§3.9.3)

- **Registered roots** (`roots-20.md`): no collision — no entry begins `guten`.
- **Reserved keywords** (spec §8.4, 30 words): no collision.
- **Contextual keywords and capability atoms**: no collision.
- **Module namespaces**: `norma.*` only; no collision. Worth stating because
  §3.9.3 names this trap specifically — `norma` is both the standard-library
  namespace and the Latin for a vector norm.
- **Instruction mnemonics**: none (this bites `proc` arguments in the compiler's
  own assembly, not the lexicon, but it is free to check).

## 5. Three derivations it must support (§3.9.3)

Required to show the root composes under §3.4/§3.5 rather than standing alone:

| name | decomposition | declares |
|---|---|---|
| `imprime_gutenbergio` | `imprim-` + `-e`, qualified by ablative `Gutenbergio` | `functio` |
| `impressor_gutenbergii` | supine `impress-` + `-or` (agent), genitive qualifier | `structura` |
| `impressio_gutenbergii` | supine `impress-` + `-io` (reified operation) | `structura` |

The qualifier pattern is not invented here: spec §10.1 already writes
`plica_unicode` and `plica_sermone` — head verb, `_`, qualifier, with
`sermone` in the ablative exactly as `Gutenbergio` is.

**This is the strongest evidence yet that §3's frame does real work.** One root
generated the action, the agent, and the operation, each landing on the
declaration §3.4 demands, with no further decisions.

## 6. What this exercise found

§3.9 has read `[UNTESTED]` since it was written — *"no root has been coined
through it."* This is its first run, and two things came out of it:

1. **The process refused the obvious answer.** The request was Gutenberg's name
   on a print function; the exhaustion test held the line and kept `imprim-` as
   the root. A governance process whose first act is to say no to its
   proposer is working.
2. **The ladder was not the hard part; the collision check was.** Rung
   selection took one lookup. Checking against keywords, atoms, namespaces and
   registered roots is the part that needs tooling, and §3.9.7's tooling does
   not exist.

`[UNTESTED]` on §3.8 and spec §15 #4 should come down only when a coinage has
been **reviewed and registered**, not merely proposed. This one has not been.
