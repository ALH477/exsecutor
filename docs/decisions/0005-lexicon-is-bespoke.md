# 0005 — The Latin lexicon is retained regardless of the derivation test

**Status:** Accepted, 2026-09-09
**Relates to:** spec §3, §15 #2, §16 (Stage 0), §17; [0001](0001-name-exsecutor.md)

## Context

§16 defined the derivation test with a kill criterion:

> **Kill criterion:** if derivation accuracy does not clearly beat English recall, §3 is decorative — and everything else in this spec survives unchanged with English roots in the same derivational frame.

§15 listed the test as open problem #2, gating "whether §3 is load-bearing." §17
named naming constraints "the largest adoption risk in the project, larger than
ARC or capabilities."

The design as written therefore made §3 conditional: run the experiment, and if
the number comes back wrong, swap Latin roots for English ones inside the same
derivational frame.

## Decision

**§3 stays, whatever the test says.** The Latin morphological lexicon is a
deliberate identity choice for this language, not a hypothesis awaiting
confirmation. The project owner's stated intent: the language is meant to be
bespoke.

The derivation test is still worth running, but it is **calibration, not
adjudication.** It stops being a gate on §3's existence and becomes a
measurement of what §3 costs.

## What the test now buys

- **Where the scheme is hard.** Which affixes, roots, and derivation shapes
  produce errors — driving documentation priority and `exsc emenda`'s auto-fix
  coverage. §8.3 already promises machine-applicable fixes for lexicon errors;
  this says which fixes matter most.
- **Onboarding cost, as a number.** Time to first correct derivation is a real
  fact about adopting the language, useful independent of any comparison.
- **Diagnostic priority.** The tasks people fail map onto `EXS-E0601`/`0602`/
  `0603`/`0610` and tell you which of those messages carry the most weight.
- **Honest disclosure.** If Latin derivation costs more than English recall,
  that is a real adoption cost. Measuring its size beats pretending it is zero.

The English control is retained precisely because it makes the cost measurable
rather than anecdotal.

## Consequences

**Positive**

- Removes a conditional from the center of the design. §3's tables, §3.5's
  prefix laws, and `EXS-E0601`–`0610` are now unconditionally load-bearing, and
  downstream work (the lexicon checker in Stage 2) can be built against them
  without hedging.
- Stage 0's second item stops being a blocker on the critical path. It still
  needs human subjects to produce its number, but no downstream decision waits
  on that number.
- The naming scheme becomes a stated identity commitment rather than an
  unfalsified claim — which is a more honest description of what it always was.
  `exsecutor` itself derives under §3 ([0001](0001-name-exsecutor.md)); that was
  chosen for coherence, not measured for efficacy.

**Negative**

- **This forecloses a documented escape hatch.** §16 was explicit that
  "everything else in this spec survives unchanged with English roots in the
  same derivational frame" — that is a cheap, fully-specified fallback, and it
  is now off the table by choice rather than by evidence.
- §17's largest-adoption-risk bullet is **not** retired by this decision. It is
  accepted. If developers will not tolerate the naming scheme, that outcome is
  now unmitigated by design rather than by oversight.
- A negative test result becomes information the project has decided in advance
  not to act on. That is a legitimate stance, but it should not later be
  described as the test having "passed."

**Neutral**

- §15 #2 remains open in the sense that the measurement has not been taken.
  What changed is what hangs on it.
- The test materials in `prototypes/lexicon/` are unaffected in substance — only
  `RUBRIC.md`'s framing changes, from verdict to calibration.
