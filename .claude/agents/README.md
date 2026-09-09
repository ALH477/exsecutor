# Agent roster

Eight agents cover this repo. Each owns exactly one directory for writes;
`spec-guardian` owns none and writes nothing. Scopes are **exclusive** — an
agent that needs a change outside its own directory reports it to the owning
agent instead of making it.

| agent | owns (exclusive write scope) | spec sections |
|---|---|---|
| `asm-rt` | `compiler/x86_64/macros/`, `compiler/x86_64/rt/` | §6.3, §9.3 |
| `unicode` | `tools/ucd-gen/`, `compiler/shared/unicode/` | §8.1, §8.2, §11 |
| `lexer` | `compiler/x86_64/lexer/` | §8.1, §8.2 |
| `cst` | `compiler/x86_64/cst/` | §9.1 |
| `diag` | `compiler/x86_64/diag/` | §8.3, §13 |
| `capcheck-probe` | `prototypes/capcheck/` (Python, never shipped) | §4.2, §15 #1 |
| `conformance` | `tests/conformance/` | §14 |
| `spec-guardian` | none — read-only, whole tree | all |

## Wave ordering

**Wave 1 — parallel, no dependencies:**
`asm-rt` (macro dialect, then runtime), `unicode` (generator and tables),
`capcheck-probe` (§15 #1, named as blocking Stage 1), `spec-guardian`
(continuous from the start).

**Wave 2 — needs wave 1:**
`lexer` (needs `rt/` + `compiler/shared/unicode/`), `cst` (needs `rt/`),
`diag` (needs `rt/`).

**Wave 3:**
`conformance`, driver integration.

## The rule

Scopes are exclusive. Two agents never write the same directory. An agent
that finds a needed change outside its own tree stops and reports it to the
agent that owns that tree — it does not make the edit itself.
`spec-guardian` never writes anywhere, including `docs/spec/`; a spec
amendment happens outside this roster, with an explicit reason recorded in
the commit message (`CLAUDE.md`, "Scope").

Every agent here is bound by `/home/asher/Documents/EXSECUTOR/CLAUDE.md` and
treats `docs/spec/exsecutor-spec-v0.4.md` as the source of truth. Where the
two disagree, the fix states which one is wrong, per the same file.
