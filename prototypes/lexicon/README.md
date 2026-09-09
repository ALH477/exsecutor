# prototypes/lexicon/

Materials for §3's derivation scheme and the §16 human-subjects derivation
test — Stage 0 item 2. **Prepared. Piloted by the author. Not administered
to a human subject.** That last step stays open regardless of anything
else in this directory; see "Status," below.

## Framing — read this before the rest

**The derivation test is calibration, not a gate.** §16 originally shipped
it with a kill criterion — a bad number would have meant swapping §3's
Latin roots for English ones in the same derivational frame. That branch is
closed: `docs/decisions/0005-lexicon-is-bespoke.md` records the project
owner's decision that **the Latin lexicon is retained regardless of what
this test measures** — the language is meant to be bespoke, not
provisionally Latin pending a passing score. §15 open problem #2 and §17's
naming-adoption-risk bullet were both updated to match (§17's risk is now
*accepted*, not mitigated by a documented fallback).

What running this test still buys, per ADR 0005: which affixes, roots, and
derivation shapes are hardest (documentation and `exsc emenda` auto-fix
priority), a real number for onboarding cost (time to first correct
derivation), which of `EXS-E0601`–`EXS-E0610` need the sharpest diagnostics,
and an honest, measured size for a cost §17 already concedes exists. See
`RUBRIC.md` for the full framing — do not read any file in this directory
as adjudicating whether §3 survives. It survives. The open question is only
how much it costs.

## What's here

- **`affixes.md`** — the affix reference sheet: §3.4's suffix table, §3.5's
  prefix table, §3.6's no-assimilation rule, §3.8's two-affix ceiling, and
  the `exsecutor` worked example. One of exactly two documents a test
  subject may consult.
- **`roots-20.md`** — the other one: all twenty roots §3.3 names (fourteen
  Latin present/supine pairs, six Greek combining forms), with the one
  disclosed gap (§3.3 doesn't gloss the Greek six, so this sheet supplies
  standard classical glosses and says so).
- **`tasks-20.md`** — twenty derivation tasks with a full answer key,
  covering agent nouns, prefix signature laws, all three of §3.6's own
  named no-assimilation examples, and the §3.8 composition ceiling
  (including a task that tests it as a constraint, not just a property
  every prefixed answer happens to sit at).
- **`control-english.md`** — the matched English-recall control: a small
  fictional "Control API" with its own documented, equally-novel naming
  convention, built to the same size and the same internal structure
  (eight prefixes with the same six-lawful/two-positional split, six
  suffixes in the same six roles, three regularized-spelling traps, one
  composition-ceiling trap, one pure-lookup item). Its own "Design notes"
  section states plainly how the matching was done and where it's weakest:
  English compounds stay partially guessable from general fluency in a way
  Exsecutor derivations structurally cannot be, which biases any
  comparison *against* Exsecutor, not for it.
- **`RUBRIC.md`** — scoring (exact/partial/zero credit, spelled out
  concretely, including how the assimilation and ceiling traps score), the
  error-code mapping (which task failures correspond to which of
  `EXS-E0601`–`0610`), the administration protocol (counterbalanced order,
  what subjects may consult, timing, subject count, and — new — subject
  language-background metadata, since the no-assimilation traps
  specifically test resistance to a Latin/Romance instinct some subjects
  won't have), and what a result means now that nothing is gated on it.
- **`PILOT.md`** — a self-administered validation pass: all twenty
  `tasks-20.md` items and all twenty `control-english.md` items worked
  from the reference sheets alone and checked against the answer keys,
  specifically hunting for multiple-defensible-answer tasks, answer-key
  disagreements with §3's tables, items solvable without the sheet, and —
  for the control — ceiling effects. It found and fixed real problems in
  both instruments (an answer-key error, a too-guessable task, an
  ambiguous task, and six ceiling-effect items in the control, among
  others — the full list is in `PILOT.md`). It states its own limits
  just as plainly: a perfect self-score proves the item set is
  solvable-and-correctly-keyed, nothing about whether it's easy for anyone
  who isn't the person who wrote the answer key.

## Status

**Stage 0 item 2 remains open.** Everything above is instrument
construction and instrument validation — the derivation test itself,
per §16, needs human subjects, and none have taken it yet. Nothing in this
directory closes that item; `RUBRIC.md`'s administration protocol and
`PILOT.md`'s pre-flight checks exist so that when subjects do run it, their
time isn't spent finding the bugs this pass already found.

`exsecutor` itself remains the scheme's worked example: `ex-` (§3.5 prefix,
"out of") + `secut-` (§3.3 root table, supine of `sequ-`, "follow") +
`-or` (§3.4 suffix, supine stem + agent → `structura`), concatenated
without assimilation (§3.6) at exactly the §3.8 two-affix ceiling. See the
spec's front-matter "Name" section and `docs/decisions/0001-name-
exsecutor.md`.
