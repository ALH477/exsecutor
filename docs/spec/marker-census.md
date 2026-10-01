# Marker census — docs/spec/exsecutor-spec-v0.4.md

Produced while making the spec true about the tree (see the commit this file
ships with). Classifies every `[OPEN]`/`[UNTESTED]`/`[UNREPRODUCED]`
occurrence in `exsecutor-spec-v0.4.md` as **live** or **narrative**, for
`tools/spec-check.sh` check 3 to eventually tell apart mechanically. Hand-
built by reading each occurrence in context; re-run by hand if the spec
changes under it; CLAUDE.md's evidence discipline applies to this file too —
nothing here retires a marker, it only sorts what is already there.

## The test

A **live** marker opens a bullet or a sentence: it is the main clause's own
assertion that something is currently unresolved (`§5.1 L497` "`[OPEN]` Of
the four method calls above..."; `§7.1 L1299` "`[OPEN]` The three-way
interaction..."). It belongs in an open-items register.

A **narrative** marker sits inside a subordinate or relative clause,
quoting a *past* open/untested state that the sentence's main clause then
resolves, retires, or explicitly quotes as history (`§8.6 L1824` "This
settles what §8.4 left `[OPEN]` for the comparison words"; `§8.6 L2231`
"struct literals were `[OPEN]` and they are the only thing it excludes").
Signal words: "had been", "was"/"were", "left `[OPEN]`", "used to
say"/"used to read", "no longer", "narrowed to ... on `<date>`", "stays
`[OPEN]`" as a retired-banner quotation. It is evidence trail, not a TODO.

Two occurrences are **definitional** rather than either: the evidence-base
paragraph's own two mentions of the marking convention (line 7). They name
the convention, not a specific claim, so they are filed under narrative
(they are not register-worthy) but are not themselves quoting a resolved
past state — noted here rather than invented a third bucket for two rows.

## Counts (as of this file's commit)

Raw bracket occurrences in `exsecutor-spec-v0.4.md`: **73 `[OPEN]`, 34
`[UNTESTED]`, 3 `[UNREPRODUCED]`** — 110 total. (`tools/spec-check.sh`
check 3 reports a larger number because it scans every `*.md` file in the
repo, not just this one.)

| | OPEN | UNTESTED | UNREPRODUCED | total |
|---|---|---|---|---|
| live | 65 | 27 | 3 | 95 |
| narrative | 8 | 7 | 0 | 15 |
| **total** | **73** | **34** | **3** | **110** |

## The live markers (95)

These are the ones indexed in `exsecutor-spec-v0.4.md` §19's consolidated
register, grouped by section. Line numbers below are as of this commit and
will drift on the next edit.

119, 164, 197, 255, 259, 357, 370\*, 434, 486(×2), 488(×2), 504, 523, 537,
641, 653, 706, 711, 716, 794, 904, 1008, 1021, 1041, 1068, 1116, 1187, 1234,
1290, 1306, 1310, 1340, 1347, 1423, 1501, 1513, 1573, 1585, 1640, 1646, 1722,
1752, 1769(second), 1874, 1897, 1902, 1903, 1933, 2002, 2064, 2082, 2108,
2140, 2144, 2191, 2254, 2265, 2275, 2305, 2314, 2334, 2335, 2337, 2359, 2363,
2368, 2369, 2469, 2472, 2496, 2516, 2574, 2602, 2603, 2607, 2632, 2640, 2669,
2726, 2764, 2775, 2785, 2820, 2839, 3123, 3126, 3127(×2), 3131(×2 of 3),
3150(×2), 3174, 3213

(\*line 370 is `[UNREPRODUCED]`, not `[OPEN]`/`[UNTESTED]`; it is the v0.2
capability-row probe's absence, distinct from line 434's `[UNTESTED]`
soundness claim two paragraphs later.)

## The narrative markers (15)

| line | marker | why narrative |
|---|---|---|
| 7 | OPEN | names the marking convention, not a claim |
| 7 | UNTESTED | names the marking convention, not a claim |
| 472 | OPEN | "recorded this as `[OPEN]` when... it is closed by placing the streams" |
| 656 | OPEN | "ADR 0011 left `[OPEN]`" inside "This also answers..." |
| 789 | UNTESTED | quotes a retired banner sentence verbatim |
| 1058 | OPEN | "both had been `[OPEN]` since..." inside "(settled 2026-09-25; ...)" |
| 1769 (first) | OPEN | "marked `[OPEN]`" — meta-reference introducing §8.6's own list, not itself an item |
| 1832 | OPEN | "This settles what §8.4 left `[OPEN]` for the comparison words" |
| 2250 | OPEN | "struct literals were `[OPEN]` and they are the only thing it excludes" |
| 2613 | UNTESTED | "The single `[UNTESTED]` this paragraph used to carry... is now false of two" |
| 2625 | UNTESTED | "there is no longer a mips64 float `[UNTESTED]`" |
| 2647 | UNTESTED | "was itself `[UNTESTED]` from the day it was written until 2026-09-12" |
| 3080 | UNTESTED | quotes a retired sentence verbatim ("the sentence used to read...") |
| 3082 | UNTESTED | "narrowed to `[UNTESTED]` on 2026-09-26" — later resolved in the same entry |
| 3131 (middle of 3) | OPEN | "§8.4 still marked the comparison words `[OPEN]`" — listed among contradictions "all since fixed" |

## How to re-run this by hand

```
grep -n '\[OPEN\]\|\[UNTESTED\]\|\[UNREPRODUCED\]' docs/spec/exsecutor-spec-v0.4.md
```

then, for each line, read the full sentence (not just the grepped line — long
sentences wrap) and apply the test above. Multi-marker lines need each
occurrence judged separately; seven lines in this version carry two or three.
