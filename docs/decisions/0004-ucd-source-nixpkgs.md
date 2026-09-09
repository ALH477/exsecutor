# 0004 — Unicode data comes from nixpkgs, not vendored or fetched

**Status:** Accepted, 2026-09-09
**Relates to:** [0003](0003-assembler-fasmg.md) (the vendoring precedent this deliberately does *not* follow), spec §8.1, §8.2, §11

## Context

§11 requires that **"the Unicode data version is a content-addressed dependency
of every `ego` transitively using text. `plica_unicode` is stable only against a
pinned table."**

No UCD version was pinned anywhere in this repository, and no UCD data was
present. `tools/ucd-gen/` and `compiler/shared/unicode/` were empty. The Unicode
layer needs, at minimum: `XID_Start`/`XID_Continue` (§8.2), canonical
decomposition, `Canonical_Combining_Class` and composition exclusions for NFC
(§8.1), `Script_Extensions` and the UTS #39 confusables mapping for
`EXS-E0104`/`EXS-E0105`, grapheme-break data for `grapha` (§5.1, §11), and
`NormalizationTest.txt` to verify any of it.

§9.3 forbids network access at build time, so the data has to arrive some other
way. Three options were considered.

## Decision

**Use `nixpkgs#unicode-character-database`, version 17.0.0 (Unicode-3.0).**

`tools/ucd-gen/` consumes it from the devShell only — never from a package's
build inputs, so the compiler's build closure stays `{fasmg}` plus the vendored
macro package (§18.1). Generated table blobs are committed to
`compiler/shared/unicode/tables/` and re-verified by a sandboxed check that
regenerates them and diffs byte-for-byte.

## Alternatives considered

| option | why not |
|---|---|
| **Vendor it**, as `vendor/fasmg-x86/` does | Adds several MB of third-party text to the repo. The fasmg precedent exists because *upstream is not content-stable* — `flatassembler.net` republishes a rolling URL, and its bytes had already drifted from the hash nixpkgs pins. unicode.org has no such problem: it publishes immutable versioned paths (`Public/17.0.0/ucd/`). Vendoring solves a problem that does not exist here. |
| **A `vendor-ucd` fetch app**, mirroring `apps.vendor-fasmg-x86` | Needs the network, so nothing works on a cold clone until a human runs it. Acceptable for a package upstream cannot be trusted to keep stable; unnecessary overhead when nixpkgs already pins the artifact. |

## Consequences

**Positive**

- A nix store path *is* a content hash. §11's requirement is satisfied by the
  mechanism rather than by an added convention, and the pin lives in
  `flake.lock` alongside every other dependency.
- Hermetic and offline. `nix develop` already has the data; no fetch step, no
  `--write` flag, no `.ucd-cache/` to manage.
- No megabytes of third-party text in git, and no second `PROVENANCE.md` to keep
  in sync by hand.
- Upgrading Unicode versions is a `flake.lock` bump, and the table-regeneration
  check turns any resulting blob change into a visible diff rather than a
  silent one.

**Negative**

- Ties table generation to nixpkgs. Someone building outside Nix must supply the
  UCD themselves; `tools/ucd-gen/` should therefore take the data root as an
  argument rather than hard-coding a store path.
- The version is nixpkgs' choice, not ours. Pinning a *different* UCD version
  than nixpkgs carries means overriding the package — possible, but it moves the
  pin out of `flake.lock` and into our own expression.
- 17.0.0 is a decision with a long tail: identifier legality and NFC results
  change between Unicode versions, so this number becomes part of the language's
  observable behaviour. §11 is explicit that this is expected, which is why it
  wants the version content-addressed rather than implicit.

**Neutral**

- `.gitignore` already carries `.ucd-cache/`, added speculatively in an earlier
  pass. Under this decision nothing caches downloaded UCD data, so that entry is
  now inert. Harmless; left in place rather than churned.
- The generator stays Python and stays off the build path, exactly as §18 says
  design probes must.
