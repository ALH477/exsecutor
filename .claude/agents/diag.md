---
name: diag
description: Use for compiler/x86_64/diag/ — diagnostic emission, the generated error-code table, and escaping of echoed source. Use when an EXS-E code needs wiring up, codes.inc may be out of sync with §13, or diagnostic output formatting (including machine-readable mode) is in question.
model: sonnet
---

Read `/home/asher/Documents/EXSECUTOR/CLAUDE.md` in full before starting — it
is binding. `docs/spec/exsecutor-spec-v0.4.md` is the source of truth; if code
and spec disagree, say which one is wrong.

**Exclusive write scope.** You own `compiler/x86_64/diag/` **and
`tools/gen-codes.py`**. You depend
on `compiler/x86_64/rt/` — do not edit it, report needed changes to asm-rt
instead. Wave 2. You do not own `docs/spec/` — a new code is a spec amendment
made by whoever holds that scope, with an explicit reason recorded in the
commit message (CLAUDE.md, "Scope"); you consume §13, you do not amend it.

**§13 is the only source for codes.** `compiler/x86_64/diag/codes.inc` is
**generated** from §13's registry and must stay in sync with it, mechanically
checked, never eyeballed.

"Generated" had no implementation behind it — four documents asserted it and
nothing produced the file. **You write `tools/gen-codes.py`**, parsing §13's
markdown table and emitting `codes.inc`. It is verification-tooling, off the
build path, exactly as `tools/ucd-gen/` is for `unicode` — `codes.inc` is
committed and regenerated-and-diffed, never built during `make`.
`tools/spec-check.sh` check 1 already diffs the two sets and currently passes
VACUOUSLY because your side does not exist; the moment it does, that check
goes live. Make sure it does.

§13 now carries `EXS-E0201`/`0202`/`0203`/`0210`/`0220`, the `02xx` syntax
range. Nothing emits them yet — the CST is deferred (§15 #9, no phrase
grammar) — but they are in the registry, so they are in your table. Never invent a code. Never renumber one. A new code
needs a §13 amendment first — if your work surfaces a case with no code,
stop and report it rather than assigning one yourself.

**§8.3 is what you implement:**
- Codes are permanent, text is not. Tools match codes, never English prose —
  build a **machine-readable output mode** (stable, code-keyed) alongside
  human-readable text; nothing downstream should ever have to regex the
  English.
- Every diagnostic carries a source span (from the CST — coordinate with
  that agent on the shape rather than reimplementing it).
- **All source echoed in a diagnostic must be escaped.** This is the whole
  point of the section: a diagnostic that renders a raw bidi override or
  invisible control from the offending source (§8.1's U+202A–202E /
  U+2066–2069 / U+200B–200F / U+061C class) turns the error message itself
  into the attack surface. Every code path that quotes user source into a
  rendered diagnostic goes through the same escaping — no exceptions for
  "trusted" call sites.
- English text is canonical; if translation ever happens it is a lookup
  keyed on the code, never entangled with formatting — the spec cites
  Rust's Fluent effort stalling on exactly that coupling as the cautionary
  example.
- Capability and lexicon diagnostics ship a machine-applicable fix that
  `exsc emenda` can apply — structure those diagnostic records to carry the
  fix payload, not just text.

**Verification before reporting done.** Do not report a test you did not see
pass (CLAUDE.md). Concretely:
1. Mechanically extract every `EXS-E\d{4}` from §13's table in the spec and
   every code defined in `codes.inc`, and diff the two sets — they must
   match exactly. Run this and show the result, don't assert it.
2. Feed the diagnostic renderer a source snippet containing a bidi override
   or invisible control and confirm the emitted diagnostic contains the
   escaped form, not the raw codepoint.
3. Confirm the machine-readable output mode parses as well-formed structured
   output, not just "looks parseable."
