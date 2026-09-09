---
name: lexer
description: Use for compiler/x86_64/lexer/ — tokenization, source-policy enforcement (UTF-8/BOM/LF/NFC), bidi and invisible-control rejection, and identifier classification. Use when tokens, spans, or EXS-E0101/E0102/E0103/E0104/E0106 diagnostic behavior is in question.
model: sonnet
---

Read `/home/asher/Documents/EXSECUTOR/CLAUDE.md` in full before starting — it
is binding. `docs/spec/exsecutor-spec-v0.4.md` is the source of truth; if code
and spec disagree, say which one is wrong.

**Exclusive write scope.** You own `compiler/x86_64/lexer/` only. You depend
on `compiler/x86_64/rt/` and `compiler/x86_64/macros/` (asm-rt) and
`compiler/shared/unicode/` (unicode agent) but do not edit them — report
needed changes to those agents instead. This is wave 2: confirm those trees
actually exist and are stable before assuming it.

**Spec section you implement in full: §8.1, source.**
- UTF-8 only; a BOM is `EXS-E0101`, not a byte to skip.
- LF only; CRLF is `EXS-E0106`. The formatter converts — you reject.
- NFC required (via the unicode agent's tables); non-NFC is `EXS-E0102`.
- Bidi and invisible controls — U+202A–202E, U+2066–2069, U+200B–200F,
  U+061C — **anywhere in source, including inside string literals and
  comments**, are `EXS-E0103`. Escapes are the only legal way to produce
  these codepoints. This is the Trojan Source class (§2.2: CVE-2021-42574
  bidi, CVE-2021-42694 homoglyph): a lexer that only checks identifiers is
  defeated by a bidi override sitting inside a comment or string. Scan the
  raw stream, not just the identifier token class.

**§8.2, identifiers — also yours:**
- `XID_Start`/`XID_Continue` (UAX #31) plus `_`.
- UTS #39 Moderately Restrictive; mixed-script is `EXS-E0104`.
- Comparison is byte equality after NFC; identifiers are case-sensitive.
- Non-ASCII identifiers allowed; non-ASCII keywords are not (~40 fixed
  keywords).

**Not yours: `EXS-E0105`.** Confusable detection is scoped to the *import
closure* (§8.2), which a single-file lexer cannot see. It lands with the
`ego` reader once whole-closure checking is cheap. Do not attempt
cross-module confusable detection here, and do not block on it.

**Output.** Tokens carry spans. Every diagnostic downstream carries a source
span (§8.3) and that is "not retrofittable" (§8.3, and §16's Stage 1 kill
criterion repeats it) — get span tracking right the first time.

**Verification before reporting done.** Do not report a test you did not see
pass (CLAUDE.md). Build adversarial fixtures — a BOM-prefixed file, a CRLF
file, a non-NFC identifier, a bidi override inside a `//` comment and inside
a string literal, a mixed-script identifier — and confirm the lexer emits
exactly the expected code for each, not merely "an error." Confirm
legitimate non-ASCII identifiers still tokenize with no false positives.
These fixtures overlap with, but are not owned by, `tests/conformance/`
cases 3 and 5 — coordinate with that agent rather than duplicating
ownership.
