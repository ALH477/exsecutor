Control — the "Control API" English Instrument
================================================

This is the matched control for `tasks-20.md`. It is what makes the
Exsecutor score mean something instead of being a number nobody can
interpret — see the "Design notes" section at the end for exactly how it is
matched and, honestly, where that matching is weakest.

A subject working this condition gets **only the material above the
"Answer key" heading below** — the rule, the verb list, the two tables, the
spelling note, the ceiling, and the tasks — and nothing else: not
`affixes.md`, not `roots-20.md`, not the spec, and not this document's own
Answer key or Design notes sections. The
"Control API" below is fictional: a small systems-utility library, invented
for this test, with its own documented naming convention. It is deliberately
**not** a real library, so that no subject can have prior exposure to it —
the same footing Exsecutor starts from.

---

## The one rule

> Every name is built from **(prefix) + verb + suffix**. At most **one**
> prefix. The Control API's suffix table, below, says whether a suffix is
> present at all — the "bare verb" row means no suffix, not a missing one.
> Parts are joined by the spelling rule below — not always ordinary English
> spelling.

---

## The verb list

Sixteen of these are used by the twenty tasks; four (marked *spare*) are not
needed by any task and are here only so this list is the same size as
Exsecutor's twenty-root sheet.

open *(spare)*, close *(spare)*, load, save, copy *(spare)*, move, split,
throttle, latch, filter, parse, render, watch, notify, spool, queue, scan,
pack, stop, unlock *(spare)*

---

## Suffix table

| suffix | attaches to | meaning | this name is |
|---|---|---|---|
| *(none)* | the bare verb | the action itself | a **Function** |
| `-er` | the verb | the agent — the thing that does it | a **Type** |
| `-able` | the verb | can-be-Xed | an **Interface** |
| `-ing` | the verb | the action, reified as a thing | a **Type** |
| `-Outcome` | the verb (capitalized, compounded) | the result of doing it | a **Type** |
| `-Rig` | the verb (capitalized, compounded) | the instrument that does it | a **Type** |

Six rows, matching the Exsecutor sheet's six suffixes one for one in
function: bare/agent/can-be-Xed/reified-action/result/instrument.

## Prefix table

| prefix | gloss | if the name is a **Function**, its signature must obey | on a **Type**/**Interface** |
|---|---|---|---|
| `Re-` | again | same signature as the bare form | positional/semantic only |
| `Un-` | reverse | the base's return type becomes the new first parameter | ″ |
| `From-` | out of | first parameter is the source type | ″ |
| `Into-` | into | first parameter is the destination type | ″ |
| `Cross-` | across | parameter type and return type differ | ″ |
| `Co-` | together | takes two or more of the base's operand type | ″ |
| `Pre-` | before | positional only, no signature law | ″ |
| `Sub-` | under | positional only, no signature law | ″ |

Eight rows, matching the Exsecutor sheet's eight prefixes one for one,
including the same six-lawful/two-positional split. The specific pairing of
prefix to law is this API's own choice, not a general English rule — a
different library could reasonably have picked `Out-` instead of `From-`,
or `To-` instead of `Into-`. You have to read it off this table, the same
way an Exsecutor prefix has to be read off `affixes.md` — general English
fluency will not tell you which synonym *this* library picked.

---

## Spelling — regularized on purpose

Ordinary English spelling changes a verb before some of these suffixes:
doubles a final consonant (`stop` → `stopping`), drops a final `e`
(`move` → `moving`). **The Control API's convention does neither.** It
concatenates the verb and the suffix exactly as written, letter for letter,
even where that looks wrong:

| verb + suffix | ordinary English spelling (do NOT write this) | Control API convention (write this) |
|---|---|---|
| `stop` + `-er` | *stopper* | **`stoper`** |
| `move` + `-ing` | *moving* | **`moveing`** |
| `scan` + `-able` | *scannable* | **`scanable`** |

If a derivation you produce "looks like a misspelling" because a letter
that would normally double or drop is sitting there uncontracted — that is
usually a sign you did it correctly for *this* API, whatever your spell
-checker thinks.

## Composition ceiling

**At most one prefix per name.** If a meaning seems to call for two
prefixes at once, it cannot be built as a single name under this
convention. Pick the qualifier that matters most and drop the other, or say
plainly that it does not fit in one name.

---

## Tasks

1. You need a **Type** for something that stops. *(spelling — read the
   table above before answering.)*
2. You need a **Type** for something that throttles traffic (an agent
   noun for "throttle").
3. You need an **Interface** for things that can be scanned. *(spelling —
   read the table above before answering.)*
4. You need an **Interface** for things that can be latched.
5. You need a **Type** representing the act of moving, reified as a thing
   in its own right. *(spelling — read the table above before answering.)*
6. You need a **Type** representing the act of spooling, reified as a
   thing in its own right.
7. You need a **Type** representing the result of parsing something.
8. You need a **Type** representing the result of splitting something.
9. You need a **Type** that is an instrument for packing things.
10. You need a **Function** that filters a list. (No prefix needed.)
11. Design a **Function** that sends the same notification **again**.
12. The Function `Spool` gathers items into a single bundle and returns
    it. Design the **Function** that **reverses** this: it takes that
    bundle — `Spool`'s return type — as its **first parameter**, and
    unspools it back into individual items. (This is a stronger
    relationship than "takes some type that differs from its return" —
    the new first parameter is specifically `Spool`'s own return type.)
13. Design a **Function** that loads data **out of** a specified source
    (the source is the first parameter).
14. Design a **Function** that saves data **to** a specified destination
    (the destination is the first parameter).
15. Design a **Function** that renders a document **as** a different
    output format than its input (parameter type and return type differ).
16. Design a **Function** that takes two or more lists and queues them
    **together** as one.
17. Design a **Function** that scans ahead of the main operation, as a
    preliminary pass.
18. Design a **Function** that watches in the background, **under** the
    main process.
19. Design a **Function** that notifies the same two recipients
    **together**, and does so **again**.
20. Using only the prefix table above, give the prefix for: (a) "first
    parameter is the destination type," (b) "positional only, before,"
    (c) "parameter type and return type differ."

---

## Answer key

1. **`stoper`** = `stop` + `-er`, no doubling. **Type.** Spelling trap —
   ordinary English default is *stopper*.
2. **`throttler`** = `throttle` + `-er`. **Type.** Clean — not a
   pre-existing whole word, so producing it demonstrates use of the rule
   rather than recall of a word already known.
3. **`scanable`** = `scan` + `-able`, no doubling. **Interface.** Spelling
   trap — ordinary English default is *scannable*.
4. **`latchable`** = `latch` + `-able`. **Interface.** Clean.
5. **`moveing`** = `move` + `-ing`, no `e`-drop. **Type.** Spelling trap —
   ordinary English default is *moving*.
6. **`spooling`** = `spool` + `-ing`. **Type.** Clean.
7. **`ParseOutcome`** = `Parse` + `-Outcome`. **Type.**
8. **`SplitOutcome`** = `Split` + `-Outcome`. **Type.**
9. **`PackRig`** = `Pack` + `-Rig`. **Type.**
10. **`filter`** = bare verb, no suffix, no prefix. **Function.** Baseline
    case — no rule to apply beyond "use the bare verb."
11. **`ReNotify`** = `Re-` + `notify`. **Function.** Satisfies `Re-`'s law:
    same signature as the bare form.
12. **`UnSpool`** = `Un-` + `spool`. **Function.** Satisfies `Un-`'s law:
    the base's return type (the bundle `Spool` returns) becomes the first
    parameter.
13. **`FromLoad`** = `From-` + `load`. **Function.** Satisfies `From-`'s
    law: first parameter is the source type.
14. **`IntoSave`** = `Into-` + `save`. **Function.** Satisfies `Into-`'s
    law: first parameter is the destination type.
15. **`CrossRender`** = `Cross-` + `render`. **Function.** Satisfies
    `Cross-`'s law: parameter and return types differ.
16. **`CoQueue`** = `Co-` + `queue`. **Function.** Satisfies `Co-`'s law:
    takes two or more of the base's operand type.
17. **`PreScan`** = `Pre-` + `scan`. **Function.** `Pre-` is positional
    only, "before" — no signature to check.
18. **`SubWatch`** = `Sub-` + `watch`. **Function.** `Sub-` is positional
    only, "under" — no signature to check.
19. **No single valid name.** `Re-` + `Co-` + `Notify` would need two
    prefixes on one verb, over this convention's one-prefix ceiling. Correct
    response drops one qualifier: either **`ReNotify`** ("again," dropping
    "together") or **`CoNotify`** ("together," dropping "again") — either
    is acceptable, provided the response also says why `ReCoNotify` is not.
20. **`Into-`**, **`Pre-`**, **`Cross-`** (any order; scored as three parts,
    same as Exsecutor task 18).

---

## Design notes: how this matches the Exsecutor instrument, and where it doesn't

**What is matched on purpose, structurally, not just in size:**

- Same task count (20) and the same shape of task (a meaning, a required
  declared kind, produce one name).
- The prefix table mirrors Exsecutor's **exactly**: eight prefixes, the
  same six-carry-a-signature-law / two-are-positional-only split, the same
  six laws in substance (same signature / return-becomes-first-param /
  first-param-is-source / first-param-is-destination / param-and-return-
  differ / takes-2+-operands). A subject reasons through literally
  isomorphic logic in both conditions; only the token spelling differs.
- The suffix table mirrors Exsecutor's six-way split (action / agent /
  can-be-Xed / reified action / result / instrument) one for one.
- Each condition has exactly **three** "resist your trained instinct, the
  documented rule overrides it" trap items (Exsecutor: `conlege`,
  `transscribe`, `inlege` against real-Latin assimilation; Control:
  `stoper`, `moveing`, `scanable` against real-English spelling), exactly
  **one** composition-ceiling trap, and exactly **one** pure-lookup item
  with no composition (Exsecutor task 18: name a Greek root from its gloss;
  Control task 20: name a prefix from its law).
- Both conditions are **equally novel** to any subject — neither Latin
  roots nor this fictional API's specific token choices can be known in
  advance. This symmetry matters more than any other single design choice:
  an English control built from a *real, existing* library would let prior
  exposure contaminate recall in a way Exsecutor can never benefit from,
  which would not be a fair fight.

**Where the trap sits differs between conditions, and that is intentional,
not sloppy.** Latin assimilation is a prefix+root phenomenon — Exsecutor's
three traps are all prefix-side. English's irregular spelling
(doubling, `e`-dropping) is a root+suffix phenomenon — the Control traps are
all suffix-side. Forcing traps onto the same morpheme boundary in both
conditions would have meant faking one of them; matching the *mechanism*
(documented regularity overriding trained instinct) rather than its
surface position is the honest choice.

**Where the matching is weakest — the main threat to validity:**

English compounds remain **partially guessable from general fluency in a
way Exsecutor derivations never are.** A subject with zero exposure to
Latin cannot guess `conlege` from general knowledge; there is nothing to
free-associate from. A subject with zero exposure to *this specific
fictional API* can still sometimes reason their way to a plausible English
compound from ordinary compositional competence with English — recognizing
that `Cross-` might mean "across" is a much smaller inferential leap than
recognizing that `trans-` does, even for someone who has genuinely never
seen this table before, because `Cross-` is already a real, meaningful
English word doing real, meaningful English work outside of any table.

This was actively designed against — task roots were chosen so the
*specific derived string* is not already a memorized whole word (`throttler`
over `sorter`, `latchable` over `lockable`, `-Outcome` over the more
generically-obvious `-Result`, `-Rig` over `-Tool`; see `PILOT.md` for the
audit that found and fixed these) — but the residual advantage cannot be
fully removed without the control stopping to look like English at all,
which would defeat its purpose as an *English* control. **This biases the
comparison conservatively, against Exsecutor**: any English-condition score
is inflated somewhat by residual guessability that no reference sheet can
fully suppress, which means a result showing Exsecutor ahead is trustworthy,
while a result showing English ahead — or even close — is the expected
direction and should not be over-read as proof the derivation scheme is
harder than it looks in ordinary use.

**A structural difference that cannot be matched at all:** Latin's
present/supine two-stem system, and the Latin/Greek root split, have no
English analogue. English verbs don't carry a second stem the way Latin
ones do, so the Control API uses one uniform verb list rather than mirroring
that internal structure. This means the Control condition never tests
"pick the right stem" at all — a genuine asymmetry in what each instrument
covers, not just in how hard it is.

**Also unmatched, disclosed rather than hidden:** real-world English API
naming is frequently *contested among near-synonyms* (`getConfig` vs.
`fetchConfig` vs. `readConfig`) in a way this fixed, single-answer-per-task
instrument does not capture. `RUBRIC.md`'s scoring treats the documented
choice as the only correct one, same as Exsecutor's; that is the right
design for a scoreable instrument, but it means neither condition measures
the "which of several reasonable names did this team pick" difficulty that
dominates real API recall complaints.
