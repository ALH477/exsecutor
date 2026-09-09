PILOT — Self-Administered Instrument Validation
================================================

Human subjects are the scarce resource this whole probe is built around.
This document is the check that runs *before* any of them are spent: does
the item set even hold together — one defensible answer per task, an
answer key that agrees with §3's actual tables, nothing solvable by
guessing past the sheet — on both `tasks-20.md` and `control-english.md`.

## The caveat this whole file sits on

**My own score on this instrument is an upper bound, not a result, and it
is not evidence about whether §3 works for anyone else.** I built
`affixes.md`, `roots-20.md`, `tasks-20.md`, and `control-english.md` from
the spec's own tables, which means I already know exactly which root, which
stem, and which affix every task wants before I read the task. A perfect
score from me proves the item set is **internally consistent and correctly
keyed against §3** — it says absolutely nothing about whether a developer
who has never seen Latin morphology before can do this in forty-five
minutes from a cold reference sheet, which is the entire question §16's
test exists to answer. Do not cite the score below as if it were that
answer. It isn't. It can't be — I am the single worst-case subject this
instrument could ever be run on, in the specific sense that I am the one
person guaranteed not to need the sheet at all.

---

## Method

1. Wrote `affixes.md`, `roots-20.md`, `tasks-20.md`, `control-english.md`
   as briefed.
2. Re-derived all twenty `tasks-20.md` answers from scratch, mechanically
   following the "Procedure" section of `affixes.md` step by step against
   `roots-20.md`, rather than recalling how each item was originally
   constructed — the closest a single author can get to a fresh read.
3. Compared that fresh derivation against the draft answer key,
   entry by entry.
4. Independently re-derived all twenty `control-english.md` answers the
   same way, against its own reference sheet.
5. For every task in both sets, adversarially checked for: a second
   defensible answer; a task answerable from the prompt's wording alone,
   without the table; a root or gloss that collides with another entry;
   and, for the Control set specifically, a produced string that is
   already a common, memorized whole word or an idiomatic real-world
   naming pattern (the ceiling-effect check).
6. Fixed everything found in place, in the four files themselves — there
   is no separate "corrected" copy; what's on disk now already reflects
   every fix below.

## Score

**20/20 on `tasks-20.md`. 20/20 on `control-english.md`.** Both, against
the final, fixed answer keys, using only each instrument's own reference
sheet. Per the caveat above: this confirms the two instruments are
solvable-as-keyed, and nothing more.

---

## Findings — `tasks-20.md`

| # | issue class | what was wrong | fix |
|---|---|---|---|
| 6 | **answer key disagreed with §3's tables** | Draft key gave `plicabilis` (`plic-` + `-ibilis`), silently importing classical Latin's present/third-conjugation `-abilis` allomorph. §3.4 publishes exactly one `-ibilis` suffix, no allomorphy, and §3.7's own worked example confirms straight concatenation (`leg-` + `-ibilis` = `legibilis`, not `legabilis`). Re-deriving mechanically from the rule as stated (not from memory of real Latin) gives `plicibilis`. | Key corrected to `plicibilis`; a note added to the key entry itself so this catch stays visible. |
| 7 | **solvable without the sheet** | Draft task used `numer-`/`numerat-` + `-or` = `numerator` — correctly keyed, but `numerator` is already a common, fully-known English word whose rough sense ("something to do with counting") a subject could plausibly produce from memory alone, without ever selecting between present and supine stems. An item that can be answered without touching the reference sheet measures English vocabulary, not Exsecutor derivation. | Swapped to `iunctor` (`iung-`/`iunct-` + `-or`, "something that joins") — correctly keyed, not a pre-existing English word. `numer-` stays on `roots-20.md` (it is genuinely one of §3.3's fourteen) but is no longer load-bearing for any task. |
| 15 | **more-than-one-defensible-answer risk** | The reversing-function task's language ("takes that whole... as its first parameter") is de-'s law almost verbatim, but is *also* a specific instance of trans-'s more generic "parameter and return types differ" — a subject could defend `transiunge` on that reading. | Added a parenthetical distinguishing "the base's own return type specifically" from "some type that merely differs" — the exact axis that separates `de-` from `trans-`. |
| 18 | **more-than-one-defensible-answer risk (the sharpest one found)** | Sub-item (c) asked to "give the root for... place," with no qualifier. `roots-20.md` glosses **two** roots "place": the Greek `top-` (the intended answer) and the Latin verb `pon-`/`posit-` (§3.3's own root table). As drafted, `pon-` is an equally defensible answer and the item would have been unscoreable without an arbitrary call. | Task restricted explicitly to "the **Greek** root," with an inline note naming `pon-` as the trap. Also fixed at the source: `roots-20.md`'s gloss for `top-` now says "the Greek nominal... distinct from the Latin **verb** `pon-`/`posit-`," so the collision is visible on the reference sheet itself, not just patched over in the task text. |
| 1–5, 8–14, 16, 17, 19, 20 | checked, no change | Audited each for a second defensible prefix/suffix reading and for whether the English gloss word used in the prompt (e.g. "together," "before," "out of") ever coincides with the *token spelling* rather than just its meaning. None do — Latin morpheme spellings don't collide with the English words used to describe their meaning, which is structurally why this class of leak doesn't occur on the Exsecutor side (see the Control findings below, where it did). | — |

No root gloss besides `top-`/`pon-` (above) was found ambiguous.

## Findings — `control-english.md`

This set had more wrong, for a specific reason: the Control API's
suffix/prefix *tokens* are themselves ordinary English words (that's what
makes it a fair English control at all), so two failure modes are possible
here that structurally can't happen on the Exsecutor side.

**Ceiling-effect audit (ordinary-English guessability).** Checked every
"clean" (non-trap) task for whether the fully-correct answer is already a
common, pre-known whole word or an idiomatic real naming pattern —if so,
a subject could score full marks without the sheet doing any work, which
would make the English-condition score uninformative regardless of what
Exsecutor scores.

| task | original | problem | fix |
|---|---|---|---|
| clean `-er` | `sort` → `sorter` | ordinary, extremely common word | `throttle` → `throttler` |
| clean `-able` | `lock` → `lockable` | ordinary, extremely common word | `latch` → `latchable` |
| clean `-ing` | `render` → `rendering` | ordinary, extremely common word | `spool` → `spooling` (root `render` moved to the `Cross-` task instead, where `CrossRender` is not a pre-existing phrase) |
| result suffix | `-Result` (`ParseResult`, `SplitResult`) | `XResult` is the single most idiomatic real-world naming pattern for "the result of X" — guessable from general programming convention, not from this sheet | suffix token changed to `-Outcome` project-wide (`ParseOutcome`, `SplitOutcome`) — plausible, but not the reflexive default the way `-Result` is |
| instrument suffix | `-Tool` (`PackTool`) | `Tool` is the generic default word for "instrument" — same problem as above | suffix token changed to `-Rig` (`PackRig`) |
| `Un-` task | `pack` → `UnPack` | `unpack` is itself an extremely common, specifically *programming*-idiomatic word (tuple/argument unpacking) independent of any documented law | root changed to `spool` → `UnSpool` |

**Wording-leak audit (prompt text accidentally spelling the token).**
Because the "meaning" cue a task prompt gives and the *token itself* are
both English, a prompt can accidentally hand over the exact string being
tested rather than just its meaning:

| task | leak found | fix |
|---|---|---|
| 7, 8 | Prompts said "the **outcome** of parsing/splitting" — literally the suffix token `-Outcome`, not just its sense. (The suffix table's own gloss column correctly says "the result of doing it," not "outcome" — the leak was a wording drift between the table and the task text, introduced when `-Result` was renamed to `-Outcome` for the ceiling-effect fix above and the task prompts weren't re-checked against the new token.) | Reworded to "the **result** of parsing/splitting," matching the table's own gloss column. |
| 9 | Prompt said "an instrument/**rig** for packing" — spells the token `-Rig` directly. | Reworded to "an instrument for packing." |
| 14 | Prompt said "saves data **into** a... destination" — `Into-` is spelled out verbatim (the gloss column for `Into-` is literally the word "into," unlike every other prefix gloss on this sheet, which is a different word from its own token). | Reworded to "saves data **to** a... destination"; the disambiguating work is carried by "(the destination is the first parameter)" instead, matching the law column exactly. |
| 15 | Prompt said "renders a document **into** a different output format" — accidentally spells the `Into-` token inside what is supposed to be the `Cross-` task, which risks pointing a subject at the wrong prefix entirely, not just leaking one. | Reworded to "renders a document **as** a different output format." |
| 12 | Same more-than-one-defensible-answer risk as Exsecutor task 15 (`Un-`'s specific law vs. `Cross-`'s more generic one). | Same fix: added the parenthetical distinguishing "the base's own return type" from "some type that merely differs." |

**Two more mechanical errors caught on re-read, unrelated to guessability:**

- Task 2's prompt read "something that **sorts throttles traffic**" — a
  leftover fragment from swapping the root out during the ceiling-effect
  fix above, grammatically broken and referring to a verb ("sorts") that
  had already been removed from the task set. Reworded to "something that
  throttles traffic."
- The verb list marked three of its four spare (task-unused) entries
  `*(spare)*` and missed the fourth (`open`). Fixed.
- The instruction on what a subject may see read "gets **only this
  document** (or, if the administrator wants to hide the answer key,
  everything above the Answer key heading)" — as written, that makes
  showing the full document *including the answer key* the default
  reading, and hiding it the optional branch. Exactly backwards for a live
  test. Reworded so seeing only the material above "Answer key" is the
  only stated option, full stop.

## Ceiling-effect conclusion

**Yes, I found real ceiling-effect risk, in six of the Control set's
clean items, and fixed all six** (table above). Before the fix, a fluent
English-speaking subject could plausibly have scored close to full marks
on roughly a third of the Control instrument from general vocabulary and
programming convention alone, with the reference sheet doing no
measurable work — which would have made any Exsecutor-vs-Control
comparison uninformative regardless of how Exsecutor scored, exactly the
failure mode this pilot step exists to catch. After the fix, every clean
Control item requires either combining an uncommon verb with a suffix in a
form that isn't already a memorized word, or reading a specific,
non-obvious token choice off the prefix table. The residual, irreducible
gap between the two conditions' guessability is discussed in
`control-english.md`'s own "Design notes" section (English remains
*somewhat* more guessable than Latin no matter how the roots are chosen,
because it is a living vocabulary and Latin isn't) — that gap is disclosed
as the experiment's main remaining threat to validity, not claimed to be
fully closed.

## What changed, in one list

`tasks-20.md`: task 6 key corrected (`plicibilis`, not `plicabilis`); task
7 re-rooted (`iunctor`, not `numerator`); task 15 clarified; task 18
restricted to the Greek root with a disambiguating note. `roots-20.md`:
`top-`'s gloss expanded to flag its collision with `pon-`. `control-
english.md`: three verb swaps (`throttle`, `latch`, `spool` in place of
`sort`, `lock`, and `render` in the clean-suffix slots — `render` kept
elsewhere), two suffix-token renames (`-Outcome`, `-Rig`), one prefix-task
re-rooting (`UnSpool`, not `UnPack`), five task-prompt rewordings (7, 8, 9,
14, 15) to stop echoing token spellings, one task-12 clarification, one
broken sentence fixed (task 2), one missing `*(spare)*` tag added, and one
backwards visibility instruction corrected.

Twenty-some findings across two twenty-item instruments on a first pass is
the expected outcome of actually looking, not a sign either instrument was
built carelessly — it is exactly what running this check before spending
a human subject's time is for. A pilot that found nothing here would be
the one worth doubting.

---

## What this pilot does not and cannot tell you

- **Difficulty, for anyone but the author.** Every number above is a
  solvability check, not a difficulty measurement. Only real subjects
  produce that.
- **Time to first correct derivation** (the onboarding-cost figure ADR
  0005 asks for). Meaningless from a self-timed run by the person who
  wrote the answer key.
- **Whether the six-percentage-point-style gap RUBRIC.md's kill-criterion
  successor asks for is small or large.** Requires the Control condition
  actually being taken cold by someone, which this document is not.
- **Whether the no-assimilation items behave as designed** (§3.6's traps
  specifically catch subjects with some prior Latin/Romance-language
  instinct to resist; a subject with none may pass them "for free," not
  because the item is easy). `RUBRIC.md`'s administration protocol now
  asks to record subject language background for exactly this reason —
  this pilot can flag the concern but cannot resolve it without subjects
  who vary on that axis.

Stage 0 item 2 stays open, per `README.md` and `RUBRIC.md`: this pilot
validates the instrument. It does not run the experiment.
