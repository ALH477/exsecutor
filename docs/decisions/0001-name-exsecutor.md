# ADR 0001: Name the language "Exsecutor"

**Status:** Accepted — 2026-09-09

## Context

The design carried a placeholder name, "Nomos," through v0.1–v0.2. The spec's own
**§Name** section required a collision check before the name could be considered
resolved, and the check found exactly the reason that requirement exists:
**Nomos** collides with an existing CMU research language (Das & Hoffmann,
resource-aware session types). A systems language named after a language
already in the academic literature is a standing source of confusion, not a
style nit, and it was carried as a placeholder specifically because it had not
yet been checked.

Independently, §3 requires that every **public** name in an Exsecutor program
decompose into a checked (prefix + root + suffix) form drawn from a declared,
versioned morpheme table (§3.1). A project that asks this of its users while
its own name fails the same test would be asking for a discipline its authors
were not willing to apply to themselves first.

## Decision

Rename the language and project to **Exsecutor**, effective spec v0.3.

The name decomposes under the language's own §3 machinery, which is the reason
it was chosen over an arbitrary replacement rather than after it:

| part | rule | note |
|---|---|---|
| `ex-` | §3.5 prefix, "out of" | |
| `secut-` | §3.3 root table, supine stem of `sequ-` "follow" | already in the published root table |
| `-or` | §3.4 suffix, supine stem + agent → declares `structura` | |

§3.6 forbids assimilation, so the parts concatenate rather than blend: `ex` +
`secut` + `or` = `exsecutor`. That is also the unmodified classical Latin
spelling — unlike the deliberately wrong-Latin `conlege`, `transscribe`,
`inlege` that §3.6 accepts elsewhere as the price of greppability, `exsecutor`
needed no forcing to come out right. §3.8 caps composition at two affixes; this
decomposition sits exactly at that ceiling (`ex-` and `-or` around one root),
not over it.

**Collision check** (discharging the instruction the placeholder carried, per
§Name): "Exsecutor" as an exact string is unclaimed — no language, compiler, or
notable project uses it. The anglicized "Executor" is not free: Java's
`Executor`, the C++ executors proposal, Spark executors, and the Mac 68k
emulator all use it. The Latin `-s-` spelling was kept precisely because it is
search-unique; the accepted, recorded cost is constant autocorrection to
"Executor."

**Decided identifiers**, numerically/structurally unchanged from the
Nomos-era draft — only the letters moved: source extension `.xsc`, compiler
binary `exsc`, diagnostic prefix `EXS-E####` (§8.3, §13 — `NOM-` became `EXS-`,
the numbers did not move), interface file `ego.xsc` (§10.1), spec version v0.3.

## Consequences

### Positive

- **Free evidence for §3.** A language that can name itself under its own
  derivation rule is worth the sentence it takes to say so — and §17 names
  naming constraints as the *largest* adoption risk in the project, ahead of
  ARC and capabilities. A self-hosting example costs nothing further to
  produce, and it is the first thing a skeptical reader can check by hand.
- **No assimilation friction.** Because the classical spelling and the
  §3.6-concatenated spelling coincide, `exsecutor` carries none of the
  "this doesn't look like a real word" cost that `conlege` or `inlege` accept
  deliberately elsewhere in the scheme.
- **Search-unique**, which is exactly the test the placeholder failed.

### Negative

- **A real thematic downgrade, recorded rather than argued away.** "Nomos" was
  chosen because νόμος (convention) opposes φύσις (nature) — the classical
  distinction between what holds by agreement and what is intrinsic, which is
  §1's thesis (*"locale and target are capabilities, never ambient state"*) in
  one word. That opposition **is** the declared-vs-ambient line the type
  system draws. *Exsecutor* means "one who carries out" — it encodes
  enforcement, not the convention/nature distinction. That is a defensible fit
  for a compiler whose entire character is refusing to compile, but it is a
  strictly weaker thematic match than the name it replaces. This is logged as
  a loss, not retrofitted into a claim that Exsecutor means what Nomos meant.
- **Permanent autocorrection friction.** Accepted at decision time (see
  Collision check, above), but it does not end at decision time — every doc,
  post, and IDE spellchecker will make the same "Executor" substitution
  indefinitely.

### Neutral

- **Surfaced a gap in §3.5, closed within the same spec revision.** §3.5's
  prefix laws are stated against `functio` signatures ("first parameter is the
  source type," and so on) — but `exsecutor` is an `-or` agent noun bound to
  `structura` (§3.4), and a `structura` declaration has no parameter list for a
  prefix law to constrain. v0.3 closes this with an added line: prefix laws
  constrain `functio` signatures; on a `structura`, `typus`, or `interfacies`
  declaration a prefix is positional and semantic only. This is not a
  contradiction raised by this ADR — it is already present in the spec text
  this decision was checked against (§3.5, final paragraph, which names
  §Name directly as the example). Recorded here so the reason for that
  sentence is not lost.

## Alternatives considered

| name | disposition | reason |
|---|---|---|
| Nomos | retired | Real-world collision with a CMU research language (Context, above). Thematically the *strongest* fit for §1's νόμος/φύσις framing — which is exactly what makes retiring it a real cost, not a free move. |
| Executor (bare anglicized form) | rejected | Heavily overloaded — Java, the C++ executors proposal, Spark, the Mac 68k emulator — so it fails the search-uniqueness test the whole exercise was for. |
| Exsecutor | **chosen** | Unclaimed as an exact string; well-formed under the language's own §3 rules at exactly the §3.8 composition ceiling; classical spelling, no assimilation forcing. Cost: weaker thematic fit than Nomos, permanent autocorrection friction. |

## Related

- ADR 0002 (host language) and ADR 0003 (assembler) are the implementation
  decisions this project name now sits on top of; none of the three depends
  on either of the others' content.
- `docs/decisions/README.md` — ADR format and numbering.
- Spec: §Name, §1, §3.1, §3.3–§3.6, §3.8, §8.3, §10.1, §13, §17.
