# 0008 — Root coinage governance

**Status:** Accepted as design, 2026-09-09. **Never exercised.**
**Relates to:** spec §3.8, §3.9 (new), §8.3, §10.1, §15 #4, §17;
[0005](0005-lexicon-is-bespoke.md); `docs/design/norma-algebra.md`

## Context

§15 listed this as open problem #4 and §3.8 carried it as `[OPEN]`: *"There is no
Latin for hash, socket, or mutex. They are coinable (`dispersio`,
`receptaculum`, `exclusor`) but coining requires judgment and judgment requires a
process. Unsolved."*

Two things made it urgent rather than theoretical.

ADR 0005 retained §3 unconditionally, which means the lexicon is now permanent
project furniture rather than a hypothesis that might be swapped for English
roots. Anything permanent needs a way to grow.

And `docs/design/norma-algebra.md` produced a concrete list of words a real
library needs and Latin does not have: *lane*, *stride*, *pivot*, *eigenvalue*
(German-derived), *workgroup*. An abstract governance gap became a blocking one.

A lexicon that cannot grow gets abandoned the first time someone needs to name a
hash table. A lexicon that grows without discipline stops being checkable, and
§3's whole claim is that names are *mechanically* verifiable.

## Decision

§3.9 defines the process. Five parts:

**Exhaustion first (§3.9.1).** A root is admissible only after showing the
concept cannot be built from the existing table within §3.8's two-affix ceiling.
Most apparently-new concepts are compounds; the exhaustion argument is where
that gets found out. Failing to find a word is not the same as one not existing.

**A five-rung ladder (§3.9.2).** Attested classical Latin → Late/medieval/Neo-
Latin scientific vocabulary → Greek combining form → descriptive compound →
marked loan. Descend only when the rung above genuinely fails. Rung 2 is doing
real work: botany, medicine and taxonomy have coined disciplined Latin for four
centuries, and that corpus already answers "the Romans had no word for this." A
bare English word is never admissible.

**Review and permanent registration (§3.9.4).** Reviewed by someone other than
the proposer, who checks the exhaustion argument and the collision check rather
than re-litigating taste. Registration amends `lexicon.norma`, whose content
hash changes, so every dependent sees a version bump — lexicon growth cannot
happen silently because §3.8 already made the morpheme table a content-addressed
dependency. A registered root is **permanent**: deprecable, never reusable for a
different meaning. This is §8.3's rule for error codes applied to morphemes, for
exactly the same reason — public names are interfaces.

**The loan register is a measurement (§3.9.5).** Every rung-5 loan is recorded.
The register is not an embarrassment to be minimised away; it is the running
measurement of whether §3 scales. A small, stable count is evidence the
derivational frame does real work. A count that climbs as the standard library
grows is evidence it does not.

**Domain sub-lexicons (§3.9.6).** `lexicon.algebra`, `lexicon.rete` — separately
hashed, so a program that never touches linear algebra does not inherit its
vocabulary.

## Consequences

**Positive**

- §15 #4 moves from unsolved to defined-but-untested, and §3.8's `[OPEN]` is
  retired. That is one of the two `[OPEN]` items §3 carried.
- The process rides machinery that already exists. The morpheme table was
  already content-addressed and versioned; `exsc lexicon` was already a
  subcommand (§12). Nothing new had to be invented to carry it.
- **It restores a falsifiable signal that ADR 0005 removed.** Closing the
  derivation test's kill criterion left §3 with no way to be shown wrong. The
  loan register supplies one: it is a number, it moves in one direction when the
  scheme is failing, and it is reported rather than argued about. §17 calls
  naming the largest adoption risk in the project; this measures it.
- §3.9.3's collision check catches namespace-versus-root collisions, which is
  not hypothetical — `norma` is already both the standard-library namespace and
  the Latin for a vector norm.

**Negative**

- **Coining is now slow on purpose.** Exhaustion argument, three demonstration
  derivations, stems, collision check, second reviewer. For a project with one
  contributor this is friction with no quorum behind it, and the honest
  description is that the process *is* the discipline, not a committee.
- Permanence means early mistakes are permanent. A badly chosen root can be
  deprecated but never reclaimed, and this process will produce some.
- The ladder's rungs 1–3 need someone who can actually search Latin and Greek
  corpora. That is a real skill requirement, and getting it wrong quietly
  produces bad Latin — which §17 identifies as the failure mode most likely to
  make developers reject the language.
- Sub-lexicons risk fragmentation: two domains independently coining different
  roots for the same concept. §3.9.4's collision check is scoped to registration,
  and cross-sub-lexicon collision is not yet addressed. `[OPEN]`

**Neutral**

- Marked loans are visible rather than disguised. Some will find the register an
  admission of defeat; it is intended as instrumentation.

## Open

- No root has been coined through this process. `[UNTESTED]`
- `norma.algebra` is the first real exercise and will produce the first loan
  register entries. Whether the ladder holds under a domain that genuinely
  outruns classical vocabulary is exactly what that will show.
