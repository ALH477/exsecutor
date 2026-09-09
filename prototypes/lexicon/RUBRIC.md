RUBRIC — Scoring, Administration, and What the Number Is For
==============================================================

## Status: this is calibration, not adjudication

**This is the project owner's decision, not something the spec itself
argues for.** `docs/decisions/0005-lexicon-is-bespoke.md` records it: the
Latin/Greek lexicon (§3) is retained regardless of what this test measures.
The language is meant to be bespoke. Do not read anything below as a
pass/fail gate — there isn't one anymore.

§16 originally shipped this test with a kill criterion: *"if derivation
accuracy does not clearly beat English recall, §3 is decorative — and
everything else in this spec survives unchanged with English roots in the
same derivational frame."* That branch is closed. §16 as it now reads (and
§15 open problem #2, and §17) all point to ADR 0005 for why: §3 is an
identity commitment, not a hypothesis under test.

What runs is unchanged — twenty Exsecutor derivation tasks
(`tasks-20.md`), twenty matched English-control tasks
(`control-english.md`), same subjects, same session. **What the result is
used for has changed:**

- **Where the scheme is hard.** Per-item and per-affix error rates say
  which roots, suffixes, and prefixes trip people up. That is a
  documentation-priority list, and a priority list for `exsc emenda`'s
  auto-fix coverage — §8.3 already commits lexicon errors to
  machine-applicable fixes; this says which fixes to write first.
- **Onboarding cost, as a number.** Time to first correct derivation is a
  real fact about adopting Exsecutor, independent of any comparison to
  English.
- **Diagnostic priority.** Which of `EXS-E0601`, `EXS-E0602`, `EXS-E0603`,
  `EXS-E0610` fire most often on the tasks people get wrong tells you which
  diagnostic needs the best message, the best span, and the best
  auto-fix — see "Mapping errors to codes," below.
- **Honest disclosure.** If Exsecutor derivation costs measurably more than
  English recall, that is a real adoption cost. §17 now lists the
  naming-adoption risk as **accepted, not mitigated** (ADR 0005) — this
  test puts a number on the size of a risk the project has already decided
  to carry, not a lever that removes it. A bad number is still a bad
  number and should be reported as one; ADR 0005 is explicit that a
  negative result must not later be described as the test having
  "passed."

The English control stays in the design for exactly one reason: without it,
"Exsecutor derivation is hard" has no scale. 62% accuracy is meaningless on
its own — 62% against an English control also scoring 65% describes a
barely-there cost; 62% against 95% describes a real one. Keep both
conditions even though nothing gates on their difference anymore.

---

## Scoring a single task

Score each task **1.0 / 0.5 / 0**, per the categories below. `tasks-20.md`
and `control-english.md` each have 20 items; sum and report as a percentage
(treat item 18 / item 20's three-part lookups as a single item scored by
their own averaging rule, below).

**1.0 — full credit.** The name is exactly correct: right root/verb, right
stem (Exsecutor: present vs. supine — this has no equivalent in the
Control condition, see `control-english.md`'s design notes), right
suffix, right prefix if any, concatenated exactly per the sheet (no
assimilation / no ordinary-spelling correction), and the declared kind
matches what the task asked for.

**0.5 — partial credit.** The subject correctly identified every
*morpheme* and its grammatical role — right root, right suffix (hence
right declared kind), right prefix (hence right law satisfied) where
applicable — but the surface string has exactly one of:

- the wrong stem selected on an otherwise-correct root (present used where
  supine was needed, or vice versa), or
- a single character inserted, deleted, or substituted relative to the
  fully correct concatenation, or
- **(the trap tasks specifically)** the morphemes are right and the law is
  satisfied, but the subject wrote the assimilated/ordinary-spelling form
  instead of the sheet's regularized one. This is a distinct, important
  outcome — score it 0.5 and log it separately from an ordinary spelling
  slip, because "understood the derivation but reverted to the trained
  instinct" is exactly what those three items exist to detect. Do not
  fold it into "wrong."

**0 — no credit.** Wrong root or verb, wrong suffix (wrong declared kind),
wrong prefix (law violated or wrong law satisfied), a fabricated morpheme
not on the sheet, more than one character off, or left blank.

**Ceiling-ceiling tasks (Exsecutor #19, Control #19):**

- 1.0 — response identifies that the described meaning needs two prefixes
  on one root/verb, states or clearly implies that this exceeds the
  two-affix maximum, and supplies a valid single-prefix fallback (or
  states plainly that no single valid name exists).
- 0.5 — response supplies a valid single-prefix fallback (drops one
  qualifier) but does not say why — ambiguous whether the ceiling was
  understood or the subject just picked one arbitrarily.
- 0 — response produces the over-ceiling form (`reconlege` / `ReCoNotify`)
  as if it were valid, or leaves the task blank.

**Pure-lookup tasks (Exsecutor #18, Control #20):** three independent
sub-answers, no composition involved. Score each sub-answer 1/0 (exact
match against the sheet) and average the three for the task's score — so a
2-of-3 scores 0.67, not a flat pass/fail.

**A response with no decomposition shown**, just a final string: score the
string alone (1.0 if exactly right, 0 otherwise — 0.5 is unavailable
without a decomposition to check, since partial credit specifically
requires seeing that the *morphemes* were right even if the string wasn't).
This is a reason to ask for the decomposition, not just the answer —
`tasks-20.md` says so up front.

---

## Mapping errors to codes

Every task maps to a real §13 code — recording *which* code a wrong answer
would have triggered, had it been fed to a real lexicon checker, is what
turns a raw score into the diagnostic-priority list ADR 0005 asks for.

| failure mode | code | which tasks can show it |
|---|---|---|
| Wrong or fabricated root/prefix/suffix — doesn't decompose into the table at all | `EXS-E0601` | any task |
| Right prefix and root, but the suffix doesn't match the declared kind asked for (e.g. an `-e` form where a `structura` was wanted) | `EXS-E0602` | 4, 5, 6, 7, 8, 9, 12, 13, 16, 20 |
| Prefix chosen doesn't satisfy its signature law, or the wrong lawful prefix was picked for the described signature | `EXS-E0603` | 1, 2, 3, 10, 14, 15 |
| Two prefixes stacked on one root | `EXS-E0610` | 19 (by design); possible on any prefixed task if a subject over-composes |

**One disclosed gap this mapping surfaces, not resolved by this
instrument:** a wrong-stem answer that otherwise uses the right root and
suffix (`legor` for `lector`) doesn't cleanly fit any single row above.
It's not a fabricated morpheme (`EXS-E0601` overstates it) and it isn't a
suffix/type or prefix/signature disagreement (`EXS-E0602`/`0603` don't
describe it either). Whether that deserves its own §13 code is a spec
question for whoever owns §13 — CLAUDE.md is explicit that a new code needs
a spec amendment first, and this document doesn't attempt one. Tag these
responses "stem-selection" in raw notes so the pattern is visible if it
turns out to be common.

---

## Administration protocol

**Design:** within-subject, both conditions, single session.

**Counterbalancing — required, not optional.** Half the subjects do
Exsecutor first then the Control; half do the Control first then
Exsecutor. Randomize or alternate assignment; do not let subjects
self-select order. Doing both conditions in a fixed order confounds
whichever comes second with practice on the task *format* (novel-sheet
derivation under time pressure), independent of which language it's in.

**Break between conditions.** 5–10 minutes of an unrelated filler task
between conditions, to blunt direct carry-over.

**What subjects may consult:** for the Exsecutor condition, `affixes.md`
and `roots-20.md` only. For the Control condition, `control-english.md`
only (the reference sections; hide the answer key). No spec, no dictionary,
no search engine, no machine translation, no calculator. No consulting the
other condition's sheet while working the current one.

**Time:** budget roughly 2 minutes/task, ~40–45 minutes/condition including
instructions, ~90–100 minutes total with the break. **Pilot this on 2–3
people before trusting the number** — `PILOT.md` ran the item set, not the
clock, and cannot tell you whether this budget is realistic for someone
who isn't the test's author.

**Record per task, not just per session:** the answer, a timestamp (start
and submit), and — critically for the calibration framing — whether the
subject consulted the sheet for that item at all if that's observable
(e.g. in an in-person or screen-recorded session). "Time to first correct
derivation" (ADR 0005's onboarding-cost metric) is computed from these
per-task timestamps within the Exsecutor condition, not from the session
total.

**Subject metadata to collect** (self-report, before the session):
native language(s); prior exposure to Latin or a Romance language
(yes/no is enough); years of programming experience. Reason: the
no-assimilation traps (§3.6) specifically catch subjects with some prior
Latin/Romance exposure, who have a real instinct toward the assimilated
spelling to resist — a subject with zero such exposure may pass those
items "for free," not because the item is easy but because they have
no wrong instinct to overcome in the first place. Recording this lets
later analysis check whether that's actually happening rather than
guessing from the aggregate.

**Subject count.** Minimum 12 for the paired difference to be worth
reporting at all; 20–30 gives a materially tighter interval on both the
accuracy gap and the timing numbers. This is a precision target for a cost
estimate, not a power calculation for a significance test — nothing
downstream requires crossing a threshold (see "Status," above).

**Scoring:** score against the written rubric above, ideally by someone
other than whoever administered the session. Keep the decomposition, not
just the final string, for every response — partial credit and the
error-code mapping both depend on it.

---

## How to read the numbers afterward

Report, at minimum:

1. **Mean accuracy per condition**, with spread (SD or IQR) — not a single
   aggregate number standing alone.
2. **The paired within-subject difference** (Exsecutor % − Control %,
   computed per subject, then summarized), with a confidence interval —
   Wilcoxon signed-rank or a bootstrap on the paired differences both work
   at small N; a paired t-test is fine if scores look roughly normal.
   Report the interval. Do **not** reduce it to a pass/fail against a
   threshold — there is nothing to pass or fail.
3. **Mean time to first correct derivation**, Exsecutor condition, with
   spread.
4. **Per-item accuracy**, both conditions — this is the table that actually
   drives documentation and `exsc emenda` priority; the aggregate score in
   (1) is context for it, not the headline.
5. **Error-code tally** from "Mapping errors to codes," above, across all
   wrong Exsecutor answers.
6. **Subject metadata cross-tab**, at minimum: no-assimilation item
   accuracy (tasks 1–3) split by prior Latin/Romance exposure.

## What the result means either way

Both outcomes leave §3 in the spec, per ADR 0005. What changes is what the
number justifies spending effort on:

- **A small measured cost** (Exsecutor accuracy and speed close to the
  Control's) — light-touch follow-up: keep `affixes.md`-style reference
  material available in real docs, ordinary priority on `exsc emenda`'s
  lexicon auto-fixes.
- **A large measured cost** — high priority on exactly the items that
  failed: best-in-class messages and auto-fix for whichever of
  `EXS-E0601`/`0602`/`0603`/`0610` the tally names most, and likely a
  tutorial pass aimed at the specific roots/affixes the per-item table
  flags. §17's naming-adoption risk is accepted, not mitigated (ADR 0005)
  — the response to a bad number is to spend engineering effort narrowing
  the cost, not to revisit whether §3 exists.

Either way, report the number honestly. ADR 0005 records that a negative
result is information the project has decided in advance not to act on
*architecturally* — that is a legitimate stance, but it is not the same as
the test having passed, and this document should never be used to describe
it that way.
