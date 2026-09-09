# ADR 0003: Assembler — fasmg (flat assembler g)

**Status:** Accepted — 2026-09-09

## Context

ADR 0002 commits `exsc` to freestanding x86-64 assembly. That decision is only
as tractable as the assembler underneath it: CLAUDE.md observes that once the
first module depends on the macro dialect, "a macro change is a whole-tree
change" — which means the assembler's macro engine is the load-bearing tool in
this entire implementation, not an interchangeable detail. `docs/asm-conventions.md`
(the calling convention, register discipline, and the four `macros/*.inc`
files it specifies) is written *in* whatever this ADR chooses, so this
decision had to be settled first, and everything above it depends on it having
been settled correctly.

Candidates considered: **fasmg** (flat assembler g), **fasm** (classic),
**NASM**, **YASM**, **GNU as** (GAS).

## Decision

Use **fasmg**, vendoring the x86/x86-64 instruction-set and ELF64-writer macro
packages at `vendor/fasmg-x86/` rather than depending on them being present at
build time.

## Reasons

1. **Strongest macro engine of the candidates.** The macro dialect is the
   tractability lever for writing a compiler in assembly (see Context);
   fasmg's macro engine is a general-purpose compile-time language in its own
   right, which is what lets `docs/asm-conventions.md` propose
   `proc`/`flow`/`struct`/`assert` as ordinary *library* code rather than
   requiring changes to the assembler itself.
2. **Architecture-neutral.** fasmg's binary "knows no machine instructions at
   all" — verified directly, below. The x86-64 instruction set and the ELF64
   writer are ordinary fasmg source, not built-ins. That means the macro
   dialect, the generated Unicode tables, and the test harness survive the
   eventual aarch64 and riscv64 host ports (§18) even though
   `compiler/x86_64/` itself does not — a future `compiler/aarch64/` tree
   assembles with this same `fasmg` binary against a different, equally
   ordinary, vendored macro package.

## Facts verified on this machine, 2026-09-09

Evidence, not assumption — full record in `vendor/fasmg-x86/PROVENANCE.md`,
cited rather than duplicated here except where the finding itself is the
reason for the decision.

- `fasmg` **is** in nixpkgs (`nixpkgs#fasmg`, version `l8vn`, BSD-3-Clause,
  platforms `x86_64-linux` + `i686-linux`). An earlier assumption that it
  would need a custom derivation was **wrong**, and is corrected here rather
  than carried forward silently.
- The nixpkgs package installs **only `$out/bin/fasmg`** — no instruction-set
  or object-format packages. Without vendoring, the compiler cannot be
  assembled from that package alone.
- fasmg's architecture neutrality is real in the strong sense, not marketing:
  given `mov eax, 60` with no includes, it reports `Error: illegal
  instruction.` The x86-64 instruction set and the ELF64 executable writer are
  macro packages — ordinary fasmg source — and are now vendored at
  `vendor/fasmg-x86/` (71 files, ~830 KB).
- End-to-end assembly was verified working: `include 'format/format.inc'`
  followed by `format ELF64 executable 3` / `entry start` / `segment readable
  executable`, invoked as `INCLUDE=vendor/fasmg-x86 fasmg src.asm out`,
  produces a 241-byte static ELF64 binary with no dynamic section and no
  interpreter, which runs and exits 0.
- fasmg **self-hosts** on Linux x86-64 using this same include idiom (its own
  `source/linux/x64/fasmg.asm`) — evidence, not just a claim, that the
  toolchain can carry a program of compiler scale, since it already carries
  itself.

## Supply-chain integrity — why vendored rather than fetched

The upstream URL is not content-stable:
`https://flatassembler.net/fasmg.l8vn.zip` is a rolling link, republished in
place under the same name when the author rebuilds, with the bytes changing
while the name does not. Measured, not assumed: nixpkgs pins this archive at
`sha256-/Izf7w7yofmPp1J85BgWbMLIGC4SGsCqXzhdecOo7CE=`; fetching that same URL
on 2026-09-09 yielded `sha256-FYe1KWy7hbTvlMRPeKuE0Tg3nEYLU+Gf92f83jjrS7E=` — a
mismatch. `nixpkgs#fasmg` builds today only because its *output* is
substitutable from `cache.nixos.org`; on a cold cache it does not build at all.

A project whose §9.3 demands byte-identical builds "at any phase," and whose
§11 treats the Unicode data version as a content-addressed dependency of every
`ego`, cannot rest its only build input on a mutable URL — that would be the
one unpinned link in an otherwise pinned chain. Vendoring makes the x86 macro
package content-addressed by git instead, and makes the build offline-capable.
Full hashes, the exact upstream path taken, and the re-vendoring procedure
(`nix run .#vendor-fasmg-x86`) are recorded in `vendor/fasmg-x86/PROVENANCE.md`
and are not repeated here — read that file, not this one, for the audit trail.

## Consequences

### Positive

- The macro-engine and architecture-neutrality reasons above, realized rather
  than theoretical: a working freestanding ELF64 "hello, exit" was produced
  and run through this exact toolchain during evaluation.
- No custom Nix derivation needed for the assembler itself — corrects an
  earlier, wrong assumption instead of compounding it.
- Vendoring turns a mutable-URL dependency into a content-addressed,
  git-tracked one, matching this project's own standard for dependencies
  (§11) and closing the one build input ADR 0002's `{fasmg}`-closure claim
  would otherwise leave open.

### Negative

- **Vendoring is an ongoing obligation, not a one-time cost.** 71 files now
  live under version control that this project did not write and must not
  edit ("Do not edit these files; re-vendor instead" — PROVENANCE.md).
  Re-vendoring produces a diff to review, not an update to accept blindly:
  "assembling `compiler/` against a changed instruction-set package is a
  whole-tree change" (PROVENANCE.md).
- **Smaller ecosystem than NASM or GAS.** fasm-family assemblers have a
  narrower user base and less third-party tutorial and troubleshooting
  material; this is a real onboarding cost for a contributor who has only used
  AT&T-syntax GAS or NASM before.
- **Single-author tool.** fasmg is designed and maintained by one person
  (Tomasz Grysztar). Not measured or scored here — flagged `[OPEN]` as a
  bus-factor risk this decision accepts, in the same spirit the spec flags its
  own open risks rather than silently carrying them.

### Neutral

- Intel syntax by default, matching the convention fixed in
  `docs/asm-conventions.md`.
- BSD-3-Clause permits vendoring the source into this repository, under
  `LICENSE.txt`, retained alongside the vendored tree.
- The vendored tree is a deliberate, documented exception to this repo's own
  LF-endings rule (§8.1 / CLAUDE.md): upstream ships CRLF, and 71 of the 72
  files under `vendor/fasmg-x86/` keep it, because the point of vendoring is
  byte-fidelity to upstream, checkable by hash — see `.gitattributes` and
  `vendor/fasmg-x86/PROVENANCE.md`. `docs/asm-conventions.md` scopes the LF
  rule to Exsecutor-authored source accordingly, rather than restating CLAUDE.md's
  rule as if it had no exceptions.

## Alternatives considered

| assembler | macro engine | architecture scope | disposition |
|---|---|---|---|
| **fasmg** | Full compile-time language; user-definable syntax extensions. | Neutral by design — instruction sets and object formats are macro packages, not built-ins. | **Chosen.** |
| fasm (classic) | Same lineage, strong macros. | x86/x86-64 only — the instruction set is part of the assembler itself, not a swappable package. | Rejected: would not survive an aarch64/riscv64 host port the way fasmg's separated core does. |
| NASM | Preprocessor-level macros (`%macro`); far short of a compile-time language. | x86/x86-64 only. | Rejected: macro engine too weak to carry `proc`/`flow`/`struct`/`assert`-level abstraction (`docs/asm-conventions.md`) without becoming its own maintenance burden. |
| YASM | NASM-compatible; same macro limitations. | x86/x86-64 only. | Rejected for the same reason as NASM; no advantage over it for this project. |
| GNU as (GAS) | Minimal native macro support; typically leans on the C preprocessor instead. | One binary per target triple, by binutils convention — no single tool spanning hosts the way fasmg does. | Rejected: no shared macro dialect across the eventual aarch64/riscv64 ports without effectively rebuilding it per target. |

## Related

- ADR 0002 (host language) — this decision is downstream of, and only
  meaningful given, that one.
- `docs/asm-conventions.md` — the include idiom, and the macro dialect this
  choice is what makes tractable.
- `vendor/fasmg-x86/PROVENANCE.md` — full hash record and the re-vendoring
  procedure.
- Spec: §9.3, §11, §18, §18.1.
