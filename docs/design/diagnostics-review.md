# Stage 1 kill-criterion review: diagnostics quality

spec §16 puts a kill criterion on Stage 1 — *"diagnostics quality is not
retrofittable. Bad here → stop and fix."* This is the first time anyone has
evaluated it. Method: 34 hand-written broken programs mutated from
`examples/` and from a working multi-function baseline, run through
`build/exsc` (239,904 bytes, `make all` at 2af4d30) in both `--diagnostica`
modes, with every fix payload mechanically applied and the result recompiled.
All commands and outputs below were run; nothing here is characterised from
reading source.

## Verdict

> **Re-evaluated 2026-09-10 after the fixes: see the final section.** The
> verdict below is the original, kept as written.

**Conditional fail. Do not start Stage 2 until D1 and D2 below are fixed.**
The half of spec §8.3 that genuinely cannot be retrofitted — spans, escaping,
the code-keyed machine format, error recovery — is built well and measured
good: spans are byte-exact, no raw bidi survives anywhere, 46 of 46 JSON
diagnostics parse, and recovery is genuinely strong (one mistake gives one
diagnostic in 9 of my 12 single-mistake cases; a 300-error file gives exactly
300 diagnostics; an error on line 1 does not destroy diagnostics on line 25).
The half that is missing is the message itself. Across all 34 programs the
compiler emitted 46 diagnostics, and **37 of them (80%) are the identical
sentence `unexpected token`**, with no statement of what was expected, no note,
and no reference to the opening delimiter. `struct Diag` (render.inc:112) has
no field that could carry one, and `render.inc`'s own header states the design
intent that no message is ever interpolated. spec §8.6 decision 6 normatively
promises the message *"`<` opens generic arguments; comparison is `lt`"*; the
compiler prints `unexpected token`. That promise cannot be kept by any code in
the tree as shaped. Separately, applying the compiler's own fix payloads made
the file *worse* in 2 of the 8 multi-fix cases I applied them to. A newcomer
writing the single most likely first-day mistake in a Latin-keyword language —
English keywords — gets four identical unhelpful errors and four fixes that
would corrupt the file. That is what §16 was written to catch.

## Cases

`->` marks what the compiler printed. Full inputs are reproducible from the
descriptions; all are 3–9 line programs.

| # | input (the mistake) | `exsc` said | should have said | verdict |
|---|---|---|---|---|
| c01 | `;` forgotten after `mutabilis t: u64 = 0` | `3:5 EXS-E0201 unexpected token`, caret on `firma`, fix insert `;` at 76 | rustc: *expected `;`, found `firma`* + `help: add ';' here` at end of line 2 | span points at the token *after* the damage; fix byte offset is right and applying it gives a clean file | fair |
| c02 | `}` of an `si` block missing 3 lines up | `8:1 EXS-E0202 unterminated construct` (file has 7 lines), fix insert `}` at EOF | rustc: *this delimiter might not be properly closed* with a second span on the `{` at 3:17 | no opener span; fix compiles but silently moves `redde t;` inside the `si` block | **bad** |
| c03 | one `}` too many at end | `8:1 EXS-E0201 unexpected token`, no fix | rustc: *unexpected closing delimiter: `}`* | one diagnostic, no cascade; points at the second of two identical braces; a delete-fix is mechanically derivable and absent | fair |
| c04 | `funtcio` for `functio` | `1:9 EXS-E0201` + `3:1 EXS-E0201`, no fix | *did you mean `functio`?* — a Levenshtein-1 hit in a **closed 30-word** keyword set (spec §8.4) | no suggestion; 2 diagnostics from 1 typo | **bad** |
| c05 | `firma per: u64 = a;` (reserved word as name) | `2:11 EXS-E0220` + fix `per_` — **then 3 more diagnostics**, ending in a bogus `EXS-E0203 unexpected end of input` demanding a `}` in a brace-balanced file | rustc for `let for = 1`: one error, recovers by treating the keyword as the identifier | correct primary; 3 spurious follow-ons, one carrying a fix that is actively wrong | **bad** |
| c06 | `si a < b {` | `2:10 EXS-E0201 unexpected token`, fix replace `<` → `lt` | spec §8.6 decision 6, verbatim: *"`<` opens generic arguments; comparison is `lt`"* | span and fix exactly right; **the message the spec specifies is not emitted** | **bad** |
| c07 | unterminated `"` swallowing 5 lines | `2:11 EXS-E0202`, span 74 bytes to EOF, no fix | clang: *missing terminating `"` character* | one diagnostic, no cascade, correctly escaped; rest of file is silently unparsed (unavoidable) | good |
| c08 | `;` forgotten inside a doubly-nested block | `6:9 EXS-E0201`, caret on `}`, fix insert `;` | as c01 | one diagnostic; applying the fix gives a clean file | good |
| c09 | `a[0)` | `2:23` insert `]`, then `2:24 EXS-E0201` on `)` | clang: *expected `]`* once | 2 diagnostics for 1 typo; applying the fixes leaves `a[0])` and 2 new errors | fair |
| c13 | file truncated (final `}` lost) | `24:1 EXS-E0202`, fix insert `}` at EOF | rustc points at the unclosed `{` | one diagnostic, correct fix | fair |
| c14 | English habits: `if a > 10 { return a; }` | **four** `EXS-E0201 unexpected token`, all four with fix `insert ";"` | *`if` is spelled `si`*; *`>` is not an operator; comparison is `gt`* (the exact analogue of §8.6 decision 6) | nothing names `si`, nothing names `gt`, and applying all four fixes yields `if; a; > 10 {` | **worst case found** |
| c15 | one error on line 1, 24 valid lines after | exactly 1 diagnostic | — | early error does not poison the rest | good |
| c17 | `)` missing inside 5-deep nesting | `2:54 EXS-E0201`, fix insert `)` | rustc names the unclosed `(` | one diagnostic, correct fix, but no opener span where it matters most | fair |
| c18 | non-ASCII identifiers + missing `;` | `3:5 EXS-E0201`, correct span | — | non-ASCII lexes and renders fine | good |
| c20 | nested named `functio` | `2:5 EXS-E0201`, **no fix payload** | — | exactly what spec §8.6 *Peeks* mandates | good |
| c22 | nested `functio` + 2 missing `;` | `2:5` (correct, no fix) + 2 cascade `EXS-E0201`s **that do carry fixes** | — | the spec-mandated absence of a fix on the primary is undone by bad fixes on the follow-ons | **bad** |
| c23 | `firma piraña_señor_ñu: u64 = a < 3;` | caret printed under the `;` at char 39; the `<` is at char 36 | caret under `<` | **caret misaligned by one column per extra UTF-8 byte** | **bad** |
| c24 | stray `}` mid-file | 1 diagnostic; `duo` still parsed | — | clean recovery | good |
| c26 | `12ab34` | `2:20 EXS-E0210 malformed literal`, exact span | clang: *invalid suffix on integer literal* | correct span, message says nothing about what is malformed | fair |
| c29 | U+202E in a comment | `2:8 EXS-E0103`, rendered `\u{202e}`, 8 carets over an 8-column escape | — | escaping and caret **run** both correct | good |
| c30 | errors on line 1 and line 4, valid lines between | exactly 2 diagnostics | — | no cascade | good |
| c32 | garbage soup (6 mistakes on 4 lines) | 6 diagnostics | — | bounded, no explosion | good |

## Defects, worst first

### D1 — 80% of diagnostics are one sentence, and the record cannot carry another

37 of 46 diagnostics across the corpus are `unexpected token`. `EXS-E0201` is
the only thing the parser can say about a missing `;`, a wrong bracket, an
English keyword, a symbolic comparison, a stray `}`, a nested `functio`, and a
misspelt keyword. spec §13 defends this deliberately — *"code granularity and
message quality are independent axes"* — and then says *"which construct broke
belongs in the span and the structured fix payload."* That division is sound
in principle and **is not what the code implements**: the span says *where* and
the fix says *what bytes*, and nothing anywhere says *what was expected*. The
text renderer prints the fix as `= fix (insert): bytes 76..76 -> ";"`. A byte
offset is not a sentence a newcomer can act on.

`struct Diag` (render.inc:112) is `code_num`, `Span`, `src_*`, `path_*`,
`DiagFix`. There is no note, label, or expected-token field, and render.inc's
header states the intent — *"no format string in this file interpolates
anything into the message text, so there is no formatting site to entangle a
translation with."* That reasoning is good and the conclusion drawn from it is
too strong: what spec §8.3 forbids is tools matching on English, and what
Rust's Fluent effort tripped over was 400+ *format* sites. A single optional
`note` field, rendered on its own line and carried in JSON as a separate
member, breaks neither. Codes stay permanent, tools keep matching `code`, and
translation stays a lookup keyed on `(code, note_id)`.

**This is the retrofit cost §16 is about.** Adding the field now touches one
struct, two renderers, the JSON schema, and the ~20 construction sites in
`cst/parse.inc`. After Stage 2 it touches every capability, type, and lexicon
site as well.

### D2 — spec §8.6 decision 6 promises a message the compiler cannot emit

> `a < b` is `EXS-E0201` with the message *"`<` opens generic arguments;
> comparison is `lt`"* and the edit attached.

Measured: the span and the `<` → `lt` replace-fix are exactly right; the
message is `unexpected token`. **The code is what is wrong here, not the
spec** — this is a quality promise §16 makes binding, and D1 is why it cannot
be kept. But spec §8.6 is also the wrong *place* for it: §8.3 says text is not
permanent, so normative message text does not belong in a grammar section.
Fixing D1 and then relocating the promise to a note-id table is the coherent
resolution; either way §8.6 currently describes a compiler that does not exist.

### D3 — applying the compiler's own fixes can make a file worse

All fixes from a run applied right-to-left, then recompiled:

| case | fixes | result |
|---|---|---|
| c01, c06, c08 | 1 | clean, exit 0 |
| c02 | 1 | exit 0, but `}}` at EOF and `redde t;` silently moved inside the `si` block |
| c09 | 1 | `a[0])` — exit 1, **2 new diagnostics** |
| c05 | 3 | `redde; per;` and a surplus `}` — exit 1, still broken, and now malformed |

spec §8.3 scopes machine-applicable fixes to *"capability and lexicon errors"*,
and `diag/fix.inc` enumerates exactly `EXS-E0421/E0500/E0501/E0510` and
`EXS-E0601`–`E0610`. Every fix I could produce is on a code **outside** that
set (`EXS-E0201/E0202/E0203/E0220`). So the compiler ships fixes it was never
asked for, on the codes where the edit is *not* mechanically derivable, and
those are the ones that corrupt. Either the fix on cascade `EXS-E0201`s is
suppressed, or `exsc emenda` must apply one fix and re-run rather than apply a
batch.

### D4 — the caret is padded in bytes, the source line is printed in characters

```
 2 |     firma piraña_señor_ñu: u64 = a < 3;
   |                                       ^
```
The `<` is character 36 of a 39-character line; the caret lands at 39. Cause:
`pre` in `diag_render_text` is the byte count `diag_escape` wrote, and a
pass-through multibyte codepoint contributes 2–4 bytes and 1 column. The caret
*run* has the same bug — `mensūra` (7 characters, 8 bytes) gets 8 carets.
render.inc:403 documents the reasoning for escaped codepoints and it is
correct there (c29 confirms: `\u{202e}` gets exactly 8 carets); the
pass-through path was not considered. In a language whose §3 identifiers are
Latin and whose §8.1 policy exists to admit non-ASCII, this misaligns on
ordinary code.

### D5 — no "did you mean" against a closed 30-word keyword set

spec §13 justifies `EXS-E0220` on the argument that *"§8.4's keywords are Latin
words that read like plausible identifiers, so reserved-word-as-identifier is
the predictable error."* The mirror error — an identifier that is a near-miss
for a keyword — is at least as predictable for a newcomer who does not know
Latin, and gets nothing (c04, c14). The keyword set is closed and generated
(`lexer/keywords.inc`, 30 entries, verified in sync by `tools/spec-check.sh`);
edit-distance-1 over 30 words is cheap. This needs D1's note field, not a code.

### D6 — `EXS-E0202` never names the construct it thinks is unterminated

c02, c13, c17 all report at or past EOF with an empty source line and a caret
under nothing, and never mention the `{` or `(` that opened. This is what
rustc and clang both do best and it is the case where a second span matters
most. spec §8.6 *Recovery* does not require an opener span, so this is a
quality gap rather than a violation — but the parser knows the opener, since
it is what the recovery rule *"no production consumes a `}` it did not open"*
is written around.

### D7 — minor

- The JSON `snippet` is **double-encoded**: `diag_escape` first, then JSON
  string escaping, so the raw line reads `"\\\"Ave, mundus.;\\n}\\n"`. It is
  not lossy (a literal `\n` in source and a real newline decode differently,
  verified), but a tool that wants the source bytes must implement an inner
  decoder whose grammar is published nowhere. §8.3's escaping rule is about
  what a *terminal* renders; a JSON member is not a terminal.
- No error count is ever printed. A 300-error file prints 300 diagnostics and
  no summary; there is no `-ferror-limit` equivalent.
- A missing source file reports `exsc: cannot open: nope.exsc, errno 2`
  (exit 3). Raw `errno 2` is not an error message.
- `EXS-E0202`/`EXS-E0203` spans at EOF are `start == file_size` (one past the
  last byte) while their fixes are at `file_size - 1`. Both defensible in
  isolation; a consumer should not have to know they differ.

## What is genuinely good

- **Spans are byte-exact.** Every span I checked with `dd` extracted exactly
  the offending token. Line/column agree with the offsets.
- **Escaping holds.** U+202E renders `\u{202e}` in both modes; a file path
  containing `"` and a tab renders `q\\\"uo\\tte.exsc` and the JSON still
  parses. There is no path through the renderer that echoes raw source.
- **Recovery is the strongest thing here** and is worth defending as-is. One
  mistake gives one diagnostic in 9 of 12 single-mistake cases. A stray `{` at
  module level, a stray `}` mid-file, and 4 lines of garbage all recover
  locally. An error on line 1 leaves the diagnostics on line 25 intact (c15,
  c30). Nothing I wrote produced a cascade explosion.
- **JSON is sound.** 46 of 46 diagnostics parse; `code` and `code_num` are both
  present; `fix` is `null` rather than absent when there is none; output is on
  stderr, one object per line, stdout empty.
- **Determinism holds.** Byte-identical JSON from two different working
  directories (spec §9.3).
- **Spec conformance where it is checkable.** `tools/spec-check.sh` is `PASS`:
  35 codes in sync with §13, 30 keywords in sync with §8.4. The nested-`functio`
  diagnostic carries no fix payload, exactly as spec §8.6 *Peeks* requires, and
  `examples/saluta.exsc` and `examples/imprime.exsc` both compile clean.
- **The escaped-width caret logic is right** for escaped codepoints (c29). D4
  is a gap in that same logic, not an absence of it.

## What I could not evaluate

- **`exsc emenda` is [UNIMPLEMENTED]** — it exits 4. Every fix-application
  result in D3 comes from a script I wrote that applies the JSON `fix` members
  right-to-left. Whether `emenda` will batch, iterate, or filter by code is
  therefore an open variable, and D3's severity depends on which.
- **Capability and lexicon diagnostics do not exist in this build.** Grepping
  the passes, the emittable set is `EXS-E0101`–`E0106`, `E0201`, `E0202`,
  `E0203`, `E0210`, `E0220` — ten codes. Module-level `mutabilis`
  (`EXS-E0500`), an undecomposable public name (`EXS-E0601`) and a missing
  `poscit` row (`EXS-E0421`) all compile clean today. So **the only two classes
  spec §8.3 actually promises fixes for are untestable**, which is Stage 2 work
  per §16 and expected — but it means this review certifies nothing about the
  fix contract as spec'd, only about the fixes shipped beyond it.
- **`EXS-E0105` (confusable identifiers in the import closure)** needs a
  multi-module closure; there is no import statement (spec §8.6 decision 5).
- **Column semantics for an editor.** `col` is a 1-based byte offset within the
  line. Whether that is what an LSP client wants is a Stage 4 question
  (spec §12) and is not decided anywhere I could find. Flagged, not judged.
- **Terminal rendering of wide and combining characters.** D4 is about
  characters versus bytes; East Asian width and combining marks are a further
  question I did not test.

## What must be fixed before Stage 2

1. Add a per-diagnostic note to `struct Diag`, rendered as its own line in text
   and its own member in JSON, and populate it at the `cst/parse.inc`
   construction sites with what was expected. This needs a spec §8.3 amendment
   saying the field exists and that tools still match on `code`. **(D1, D2, D5,
   D6 all reduce to this.)**
2. Stop attaching fix payloads to cascade `EXS-E0201`s, or make `emenda`
   single-step-and-re-run. **(D3.)**
3. Count characters, not bytes, when padding and sizing the caret. **(D4.)**

## Re-evaluation, 2026-09-10

The corpus is now checked in (`tests/diagnostics/`, `tools/diag-measure.sh`)
and every number here was re-measured on it, at the named commit, in a clean
worktree — not carried forward from this document's first draft, whose 46/37
figure covered 34 programs of which only 22 were described well enough to
reconstruct.

| | before (e5f283d) | after (this commit) |
|---|---|---|
| diagnostics | 37 | 39 |
| most common message | `unexpected token` 28 (75.7%) | `unexpected token` 28 (71.8%) |
| records carrying a note | **0** | **28 (71.8%)** |
| records carrying a related span | **0** | **6 (15.4%)** |
| machine `fix` payloads | 24 | 0 |
| advisory `suggestion` payloads | — | 25 |
| header col vs caret (c23) | 39 vs 36 | 36 vs 36 |

The message text is unchanged and will stay so: codes and their canonical
text are what tools match (§8.3). What changed is that the record can carry
a second sentence, and 28 of 39 now do. The two extra diagnostics are true
positives — c32 had an extra `}` and a real `<` the old recovery masked —
and the lexer fix that stopped one malformed literal from discarding every
parser diagnostic (2897663) surfaced one each in c07 and c26.

| defect | status |
|---|---|
| D1 record cannot carry detail | fixed, 5551478 — `note_id` + one appended token, `rel Span`; an id, not a string |
| D2 §8.6 promises a message | fixed, abdfd89 — a grammar can require code, span and edit, not a sentence |
| D3 fixes make files worse | fixed, 5551478 — parser guesses are `suggestion`, never `fix`; `emenda` applies `fix` only |
| D4 caret in bytes | fixed, 5551478 and 3a763dd — caret and header both count characters; combining marks, East Asian width `[UNTESTED]` |
| D5 no did-you-mean | fixed, 5551478 — Levenshtein against §8.4's thirty, English-habits table; `si` no longer suggests `sin` |
| D6 no opener span | fixed, 5551478 and the cst commit — six related spans; c07's span already starts at the quote |
| D7 minor | span/fix asymmetry re-characterised as trailing trivia, not a bug; JSON snippet and errno text still open |

**Verdict: the kill criterion no longer fires.** The structural failure —
one field short of being able to say anything — is gone, and the corpus
that measures it is a command. What remains is named, not hidden: note ids
for nested `functio`, stray `}`, expected expression and expected
identifier do not exist yet (the parser wanted them and invented nothing);
diagnostics are appended by phase rather than sorted by span, so a lexical
record can print after a parser record at an earlier byte; and the corpus
is 22 of the review's 34 programs. None of those is a reason to stop
Stage 2. Bad here was stopped and fixed, which is what §16 asked.
