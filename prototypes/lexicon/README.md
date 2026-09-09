# prototypes/lexicon/

Empty. No `lexicon.py` exists here yet, though §3 already names it as this
probe's target: *"This is where the Latin/Greek is load-bearing. Prototype:
`prototypes/lexicon/lexicon.py`."*

## What this probe is for

Materials for §3's derivation scheme, aimed at the one experiment §16 puts
before Stage 1:

> **The derivation test.** Print the affix table and twenty roots; give
> twenty derivation tasks; score against recall accuracy on an equivalent
> English API. Days, no compiler, no engineering. Cheapest high-value
> experiment in the project.
>
> Kill criterion: if derivation accuracy does not clearly beat English
> recall, §3 is decorative — and everything else in this spec survives
> unchanged with English roots in the same derivational frame.

That test needs, at minimum: the root table (§3.3 — present/supine stem
pairs), the suffix table (§3.4 — each suffix carries a type contract), the
prefix table (§3.5 — each prefix carries a signature law), the
no-assimilation rule (§3.6 — `con-` + `leg-` stays `conlege`, not the
assimilated Latin *collega*), and the two-affix composition ceiling (§3.8).
§3.3's table currently holds 14 Latin roots plus a handful of Greek roots
named only in prose (`crypt-`, `graph-`, `metr-`, `morph-`, `chron-`,
`top-`); §16 wants twenty for the test itself, so the table is short of
test-ready before any code is even written here.

`exsecutor` is the scheme's own worked example: `ex-` (§3.5 prefix, "out of")
+ `secut-` (§3.3 root table, supine of `sequ-`, "follow") + `-or` (§3.4
suffix, supine stem + agent) → `exsecutor`, concatenated without
assimilation per §3.6, at exactly the two-affix ceiling §3.8 allows. See the
spec's own front-matter "Name" section for the full derivation.

Nothing has been built here. This README exists so a rebuild starts from the
spec, not from a guess.
