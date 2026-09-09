Exsecutor — Twenty Derivation Tasks
===================================

Instrument for §16's derivation test. Give the subject this document's
**Tasks** section together with `affixes.md` and `roots-20.md`, and nothing
else. Do not show **Answer key** before scoring — see `RUBRIC.md` for how to
score, how to run the session, and what the result is (and is no longer) used
for.

For each task, produce one Exsecutor name. Write your decomposition too
(which prefix, which root/stem, which suffix) if you can — partial credit
depends on it; see `RUBRIC.md`.

---

## Tasks

1. Design a function that reads bytes **into** a destination buffer (the
   buffer is the first parameter).
2. Design a function that reads **two** configuration files **together**,
   merging them into one result.
3. Design a function that writes **out** a text transcript from an audio
   recording (the recording is the parameter; the transcript is the
   return — a different type).
4. Name the type for something that cuts.
5. Name the type for an **instrument** that cuts.
6. Name the interface for things that can be folded.
7. Name the type for something that joins.
8. Name the type representing an ordering operation, reified as a thing in
   its own right.
9. Name the interface for things that can be followed.
10. Design a function that takes an element **out of** a collection (the
    collection — the source — is the first parameter).
11. Design a function that places a fallback value **underneath** the
    primary one — used only when the primary is absent.
12. Name the type representing **the result** of holding something.
13. Name the type representing a turning/conversion operation, reified as a
    thing in its own right.
14. Design a function that sends the same message **again**.
15. The function `iunge` joins parts into a single whole and returns it.
    Design the function that **reverses** this: it takes that whole — the
    base function's return type — as its **first parameter**, and splits it
    back into parts.
16. Name the type representing **the result** of loosening or dissolving
    something.
17. Design a function that places a value **before** the others — prepends
    it.
18. Using only `roots-20.md`, give the root for: (a) hidden/secret,
    (b) time, (c) place.
19. Design a function that reads two files **together**, and does so
    **again**.
20. Design a function that folds a value. (No prefix needed.)

---

## Answer key

Decompositions use the notation `prefix + stem + suffix = name`. "Stem" is
marked present/supine only where the root has both (Latin); Greek roots have
one form. Section numbers cite where the rule lives in the spec, not
something the subject needs to look up.

1. **`inlege`** = `in-` + `leg-` (present) + `-e`. `functio`. Satisfies
   `in-`'s law: first parameter is the destination type (§3.5). This is one
   of §3.6's own three named examples — the classical-Latin trap is
   *illeg-*; §3.6 requires the unassimilated `inlege`.
2. **`conlege`** = `con-` + `leg-` (present) + `-e`. `functio`. Satisfies
   `con-`'s law: takes two or more of the base's operand type (two files,
   §3.5). §3.6's second named example — the trap is *collega*.
3. **`transscribe`** = `trans-` + `scrib-` (present) + `-e`. `functio`.
   Satisfies `trans-`'s law: parameter and return types differ (§3.5).
   §3.6's third named example — the trap is *transcribe* (single `s`).
4. **`sector`** = `sect-` (supine of `sec-`) + `-or`. `structura` (§3.4).
5. **`sectorium`** = `sect-` (supine of `sec-`) + `-orium`. `structura`
   (§3.4).
6. **`plicibilis`** = `plic-` (present) + `-ibilis`. `interfacies` (§3.4).
   Straight concatenation, same pattern as the spec's own `legibilis`
   (§3.7) — **not** `plicabilis`; Exsecutor has one `-ibilis` suffix, not
   classical Latin's present/third-conjugation `-ibilis`/`-abilis` split.
   See `PILOT.md` — this exact error was caught in self-testing.
7. **`iunctor`** = `iunct-` (supine of `iung-`) + `-or`. `structura` (§3.4).
8. **`ordinatio`** = `ordinat-` (supine of `ordin-`) + `-io`. `structura`
   (§3.4).
9. **`sequibilis`** = `sequ-` (present) + `-ibilis`. `interfacies` (§3.4).
10. **`excape`** = `ex-` + `cap-` (present) + `-e`. `functio`. Satisfies
    `ex-`'s law: first parameter is the source type (§3.5).
11. **`subpone`** = `sub-` + `pon-` (present) + `-e`. `functio`. `sub-` is
    positional only, "under" (§3.5) — no signature to check.
12. **`tentus`** = `tent-` (supine of `ten-`) + `-us`. `typus` (§3.4).
13. **`versio`** = `vers-` (supine of `vert-`) + `-io`. `structura` (§3.4).
14. **`remitte`** = `re-` + `mitt-` (present) + `-e`. `functio`. Satisfies
    `re-`'s law: same signature as the bare form (§3.5).
15. **`deiunge`** = `de-` + `iung-` (present) + `-e`. `functio`. Satisfies
    `de-`'s law: the base's return type becomes the first parameter
    (§3.5) — `iunge`'s return (the joined whole) is `deiunge`'s first
    parameter.
16. **`solutus`** = `solut-` (supine of `solv-`) + `-us`. `typus` (§3.4).
17. **`praepone`** = `prae-` + `pon-` (present) + `-e`. `functio`. `prae-`
    is positional only, "before" (§3.5) — no signature to check.
18. **`crypt-`**, **`chron-`**, **`top-`** (§3.3; any order; this is a
    three-part lookup, not a composition — see `RUBRIC.md` for how the
    three parts are scored).
19. **No single valid name.** `re-` + `con-` + `leg-` + `-e` = `reconlege`
    would need **two** prefixes on one root — three affixes total, over
    §3.8's ceiling of two, and would be rejected as `EXS-E0610`. The
    correct response drops one qualifier and gives either **`relege`**
    (`re-` + `leg-` + `-e`, "reads again," dropping "together") or
    **`conlege`** (`con-` + `leg-` + `-e`, "reads together," dropping
    "again") — either is acceptable, provided the response also says why
    `reconlege` itself is not.
20. **`plice`** = `plic-` (present) + `-e`. `functio`. No prefix — under
    the ceiling, not at it, and the baseline case before any prefix law
    applies.

## Coverage

- Agent nouns (`-or`): 4, 7.
- Suffix coverage: `-e` 9× (1,2,3,10,11,14,15,17,20), `-or` 2× (4,7),
  `-ibilis` 2× (6,9), `-io` 2× (8,13), `-us` 2× (12,16), `-orium` 1× (5).
- Prefix coverage: all eight entries of §3.5's table appear at least once
  (`in-` 1/19-fallback, `con-` 2/19-fallback, `trans-` 3, `ex-` 10, `sub-`
  11, `re-` 14/19-fallback, `de-` 15, `prae-` 17).
- No-assimilation (§3.6, all three of the spec's own named examples): 1, 2,
  3.
- Two-affix ceiling (§3.8): every prefixed task sits exactly at it (one
  prefix, one suffix); task 19 tests it directly, as a constraint rather
  than an incidental property.
- Bare baseline (root + suffix, no prefix): 20.
- Greek: 18 only — see `roots-20.md`'s disclosed gap on why Greek roots are
  not composed with the Latin suffix table in this task set (§3.3 gives
  them no present/supine pair, and the suffix table's stem selection has
  nothing to select between for a root with one form).
