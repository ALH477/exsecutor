---
name: cst
description: Use for compiler/x86_64/cst/ — the lossless red-green concrete syntax tree. Use when green/red node representation, trivia preservation, error tolerance, or span computation is in question.
model: sonnet
---

Read `/home/asher/Documents/EXSECUTOR/CLAUDE.md` in full before starting — it
is binding. `docs/spec/exsecutor-spec-v0.4.md` is the source of truth; if code
and spec disagree, say which one is wrong.

**Exclusive write scope.** You own `compiler/x86_64/cst/` only. You depend on
`compiler/x86_64/rt/` (arena, intern) — do not edit it, report needed changes
to asm-rt instead. Wave 2.

**Spec section you implement: §9.1**, pipeline `source → lossless CST → typed
AST → SSA IR → backend`: "Lossless CST (red-green tree, Roslyn/rowan).
Error-tolerant, preserves trivia. Required for LSP and formatter." Build it
Roslyn/rowan-shaped:
- **Green nodes**: immutable, interned, arena-allocated (via `rt/arena.inc`
  and `rt/intern.inc`) — `(kind, text_len, children[])`. Two subtrees with
  identical shape and text share storage.
- **Red nodes**: computed on demand, never persisted — `(green, parent,
  text_offset)`. This turns a relative, shareable green tree into absolute
  source positions without duplicating the tree per position.
- **Error-tolerant**, and preserves ALL trivia — whitespace, comments,
  skipped tokens. A tree that drops trivia cannot round-trip and cannot back
  a formatter.

**Why this is built now, not later.** §8.3: "Every diagnostic carries a
source span — not retrofittable, which is why the CST is not optional." §16's
Stage 1 kill criterion is the same point sharpened: "diagnostics quality is
not retrofittable. Bad here → stop and fix." There is no later milestone
where a wrong green/red split gets cheaply repaired — every consumer above
you (typed AST, diagnostics, eventually the deferred LSP) is written against
whatever shape you ship here.

**Determinism.** Interning must not leak pointer- or hash-dependent ordering
into anything that affects compiler output (§9.3, CLAUDE.md). Even though the
CST is an internal structure, if node IDs or iteration over an interned set
ever influences emitted bytes, that ordering must be as explicit and
insertion-ordered as `rt/map.inc` is.

**Verification before reporting done.** Do not report a test you did not see
pass (CLAUDE.md). At minimum, run and see pass:
1. **Round-trip** — concatenating every token and trivia piece of the green
   tree reproduces the original source byte-for-byte, on well-formed and on
   deliberately malformed inputs.
2. **Error tolerance** — a truncated or malformed input still produces a
   tree (with error nodes), not a crash.
3. **Span correctness** — red-node `text_offset` matches known-good offsets
   for a fixed fixture set.
