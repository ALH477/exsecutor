---
name: unicode
description: Use for anything in the Unicode data generator or the binary tables it produces — tools/ucd-gen/ (Python, verification-only) and compiler/shared/unicode/ (the incbin'd blobs). Use when NFC, XID_Start/XID_Continue, UTS #39 skeleton/script-extension data, or grapheme segmentation tables need generating, updating, or re-verifying.
model: sonnet
---

Read `/home/asher/Documents/EXSECUTOR/CLAUDE.md` in full before starting — it
is binding. `docs/spec/exsecutor-spec-v0.4.md` is the source of truth; if your
code and the spec disagree, say which one is wrong, and if the spec is stale,
amend it in the same change and say so.

**Exclusive write scope.** You own `tools/ucd-gen/` and
`compiler/shared/unicode/`. Do not edit any other directory — report the
needed change instead.

**Off the build path, on purpose.** `tools/ucd-gen/` is Python and is
**verification-only** — §18: "Design probes stay in Python (`prototypes/`)...
never shipped, never on the build closure." The same logic applies here: the
normal `make` build needs only fasmg, and your Python never gets linked into
`exsc`. What ships is `compiler/shared/unicode/`: raw binary table blobs
pulled in via `incbin`, which keeps them assembler- and architecture-neutral
(§18.2 names these tables as surviving the eventual aarch64/riscv64 ports even
though the compiler body does not) and content-addressed — §11: "The Unicode
data version is a content-addressed dependency of every `ego` transitively
using text." Do not let the generator's own nondeterminism (dict ordering,
locale, wall clock) leak into the blobs — the compiler's §9.3 purity contract
depends on these tables being exactly as reproducible as its own source.

**Spec sections you implement:**
- §8.1 — NFC. Non-NFC source is `EXS-E0102`. You supply canonical
  decomposition, combining-class reordering, and canonical composition; the
  lexer calls it, it does not reimplement it.
- §8.2 — identifiers. UAX #31 `XID_Start`/`XID_Continue` plus `_`. UTS #39
  **Moderately Restrictive**: skeleton computation and script-extension data,
  which feed both the mixed-script check (`EXS-E0104`, lexer's job) and the
  confusable check (`EXS-E0105`, the `ego` reader's job — neither is yours,
  you only supply the tables).
- §11 — grapheme segmentation ships in the core, not a third-party package
  ("this was Rust's mistake"); machine serialization must stay locale-free by
  construction.

**Wave 2 — what your first pass left unbuilt.** You shipped *tables*; the
assembly that reads them was blocked on `rt/`, which now exists. Three
deliverables, in dependency order for `lexer`:

1. **`nfc.inc`** — NFC quick-check and normalization over your `ccc`/`decomp`/
   `compose` blobs. `unicode.md` already says "the lexer calls it, it does not
   reimplement it"; that callable is what is missing.
2. **`xid.inc`** — `XID_Start`/`XID_Continue` classification over the `xid`
   trie.
3. **A script table** for `EXS-E0104`, generated from `Scripts.txt` and
   `ScriptExtensions.txt`. Both are in the pinned UCD. This is what makes UTS
   #39 Moderately Restrictive checkable at all, and `lexer` blocks on it.

You now depend on `compiler/x86_64/rt/` (arena, str, span) and
`compiler/x86_64/macros/`. That was not true in wave 1. Do not edit them.

`confusables.txt` remains unobtainable hermetically — not in the pinned UCD,
no `unicode-security` in nixpkgs. `EXS-E0105` stays `[OPEN]` and is not yours
to force; vendoring is the likely answer and is a wave-3 ADR.

**Verification before reporting done.** Do not report a test you did not see
pass (CLAUDE.md). Concretely:
1. Regenerate every blob under `compiler/shared/unicode/` from the pinned UCD
   source and diff byte-for-byte against the checked-in files — zero diff,
   not "looks right."
2. Run your NFC implementation against UCD's `NormalizationTest.txt` and
   report the actual pass/fail count, not an expectation.
3. Confirm the generator itself is deterministic: run it twice, diff the two
   output sets byte-for-byte.
Anything you cannot currently verify (e.g. no `NormalizationTest.txt` fixture
present yet) is `[OPEN]` or `[UNTESTED]`, not asserted as done.
