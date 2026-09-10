# Agent roster

Eleven agents have covered this repo. Two of them own a generator in `tools/`
alongside their output tree, mirroring how `unicode` owns `tools/ucd-gen/`:
spec tables are the normative source, the `.inc` is generated from them, and
`tools/spec-check.sh` fails the build on drift (checks 1 and 4). Each owns exactly one directory for writes;
`spec-guardian` owns none and writes nothing. Scopes are **exclusive** — an
agent that needs a change outside its own directory reports it to the owning
agent instead of making it.

| agent | owns (exclusive write scope) | spec sections |
|---|---|---|
| `asm-rt` | `compiler/x86_64/macros/`, `compiler/x86_64/rt/` | §6.3, §9.3 |
| `unicode` | `tools/ucd-gen/`, `compiler/shared/unicode/` | §8.1, §8.2, §11 |
| `lexer` | `compiler/x86_64/lexer/`, `tools/gen-keywords.py` | §8.1, §8.2, §8.4 |
| `cst` | `compiler/x86_64/cst/` | §9.1 |
| `diag` | `compiler/x86_64/diag/`, `tools/gen-codes.py` | §8.3, §13 |
| `capcheck-probe` | `prototypes/capcheck/` (Python, never shipped) | §4.2, §15 #1 |
| `conformance` | `tests/conformance/` | §14 |
| `spec-guardian` | none — read-only, whole tree | all |
| `ast` † | `compiler/x86_64/ast/` | §8.6, `docs/design/typed-ast.md` |
| `driver` † | `compiler/x86_64/driver/`, `compiler/x86_64/exsc.asm` | §9.3, §9.5, §12, §18.2 |
| `backend-fasmg` † | `compiler/x86_64/backend_fasmg/` | §9.2, `docs/design/ssa-ir.md` |

† briefed inline when spawned; no definition file in this directory yet. The
scope was exclusive all the same, and stays so.

## Wave ordering

**Wave 1 — parallel, no dependencies:**
`asm-rt` (macro dialect, then runtime), `unicode` (generator and tables),
`capcheck-probe` (§15 #1, named as blocking Stage 1), `spec-guardian`
(continuous from the start).

**Wave 2 — needs wave 1:** four agents, not three, and they are a dependency
chain rather than a free-for-all.

```
rt/ complete
 |-> unicode (second pass)  nfc.inc, xid.inc, script table
 |-> diag                   gen-codes -> codes.inc, emission, escaping
 \--------------------------> lexer   (needs both, plus spec §8.4)
```

`unicode` returns in wave 2. Its first pass shipped *tables*; the assembly
consumers that read them (`nfc.inc`, `xid.inc`) were blocked on `rt/` and are
still unwritten, and `EXS-E0104` additionally needs a script table generated
from `Scripts.txt`/`ScriptExtensions.txt`. So `unicode` now depends on `rt/`,
which was not true in wave 1.

`lexer` starts when `unicode` and `diag` both report. `cst` is **deferred** —
spec §15 #9: there is no phrase grammar, so there is nothing to parse against.
That is the next wave's opening design act, not this one's.

**Wave 3 — shipped:** `conformance` (24 entries, 6 running), `driver`
(`exsc aedifica`, `--emitte tokens|cst|ast`), `backend-fasmg` (parser, printer,
naive emitter against hand-written IR; no verifier yet).

**Wave 4 — shipped:** `cst` was un-deferred once §8.6 existed; `ast` built the
typed tree with its Stage 2 slots empty. Stage 1's kill criterion was then
measured (`docs/design/diagnostics-review.md`) and `diag` returned for the
fixes.

**Wave 5 — Stage 2:** `docs/design/checker.md` is the opening design act; the
agents it proposes will own `compiler/x86_64/checker/` one pass per file.

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
