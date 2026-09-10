# tests/diagnostics/ -- the §16 Stage 1 kill-criterion corpus

`docs/design/diagnostics-review.md` evaluated spec §16's Stage 1 kill
criterion for diagnostics quality ("diagnostics quality is not retrofittable.
Bad here -> stop and fix") against 34 hand-written broken programs and
reported a headline number: 46 diagnostics, 37 of them (80%) the identical
sentence `unexpected token`. The corpus itself was never checked in -- the
review says "full inputs are reproducible from the descriptions" and gives
only a one-line-per-case table (cases c01-c32, with gaps: only 22 of the 34
programs are described in enough detail to reconstruct). By CLAUDE.md's
evidence discipline, a figure that cannot be re-measured is `[UNREPRODUCED]`.

This directory is that reconstruction: the 22 cases the review's table
actually describes, rebuilt as `.exsc` files, one per row, as faithfully as
each row's description allows. `tools/diag-measure.sh` is what measures them.

**This is a measurement, not a test.** `tests/run.sh` does not run it, and it
always exits 0 regardless of what it finds -- see "Should this be a gate?"
below.

## Running it

```
make all                                     # builds build/exsc
tools/diag-measure.sh                        # measures build/exsc against this corpus
tools/diag-measure.sh --exsc PATH --corpus DIR   # override either
```

Needs only bash, coreutils, and python3 (present in the devShell; the script
uses python3 as its JSON parser and is not on the build path). Deterministic:
identical output from two different working directories, verified.

## The 22 cases

Every file starts with a `//` header comment giving: the case id (matching
the review's table), the mistake, what the review reported, its verdict, and
what this reconstruction verified against `e5f283d` (see "The BEFORE
measurement" below) -- including whether the review's stated position (line,
column, or byte offset) was reproduced exactly or only approximated.

| file | byte/position fidelity |
|---|---|
| c01_missing_semicolon | **exact** -- line:col and fix byte offset (76) match |
| c02_missing_close_brace_si_block | **exact** -- 7-line file, reported 8:1, matches |
| c03_extra_close_brace | **exact** -- 8:1, no fix, matches |
| c04_functio_typo | **exact** -- 1:9 and 3:1, matches |
| c05_reserved_word_as_name | **exact** -- 2:11 primary + 3-diagnostic cascade ending in a bogus EXS-E0203, matches |
| c06_symbolic_comparison | **exact** -- 2:10, replace `<`->`lt`, matches |
| c07_unterminated_string | **exact** -- 2:11, span length 74 bytes, matches |
| c08_missing_semicolon_nested_block | **exact** -- 6:9, matches; fix verified to produce a clean file |
| c09_bracket_paren_mismatch | **approximate** -- diagnostic count (2) matches; exact columns and which token the second diagnostic lands on do NOT reproduce as described (see file header and "What didn't reproduce" below) |
| c13_truncated_final_brace | **exact** -- 23-line file, reported 24:1, matches |
| c14_english_habits_if_gt | **approximate** -- review's four diagnostics did NOT reproduce; this exact line produces three (see file header and below) |
| c15_error_line1_then_valid | **exact** -- exactly 1 diagnostic, matches |
| c17_missing_close_paren_deep_nesting | **exact** -- 2:54, matches |
| c18_non_ascii_identifier_missing_semicolon | **exact** -- 3:5, matches |
| c20_nested_named_functio | **exact** -- 2:5, no fix, matches |
| c22_nested_functio_plus_missing_semicolons | **exact** -- 2:5 (no fix) + two fixed cascades, matches |
| c23_caret_byte_vs_char_misalignment | **exact** -- char 36 / byte 39, matches to the byte; independently reconfirms D4 |
| c24_stray_close_brace_mid_file | approximate (review gives no position) -- 1 diagnostic, second function clean, matches qualitatively |
| c26_malformed_numeric_literal | **exact** -- 2:20, 6-byte span, matches |
| c29_bidi_override_in_comment | **exact** -- 2:8, 8-character escape, 8-caret run, matches |
| c30_errors_line1_and_line4 | **exact** -- exactly 2 diagnostics, matches |
| c32_garbage_soup | approximate (review gives no positions or named mistakes) -- 6 diagnostics, bounded, matches count only |

14 of 22 cases matched a review-given position (line:col, byte offset, or
span length) exactly. 6 more matched every qualitative claim the review made
(diagnostic count, "no fix", "clean recovery") but the review gave no
position to check a byte against. 2 cases (c09, c14) are recorded as
approximate because running them surfaced a **structural** mismatch with the
review's own description, not just an unpinned position -- see below.

### What didn't reproduce

- **c14** ("English habits", `if a > 10 { return a; }`): the review reports
  four `EXS-E0201` diagnostics, all carrying an `insert ";"` fix. Run against
  `e5f283d`, this exact line produces **three**, not four --
  `__cst_err`'s one-diagnostic-per-token suppression (`cst/parse.inc:698`)
  means the `>` and the `10` that follows it are skipped as a unit during
  error recovery and land the parser cleanly on the `{` as a fresh block
  statement, with no diagnostic charged to either. This does not change the
  case's point (three identical `unexpected token` diagnostics for one
  English-habits mistake, still fully corrupted by the fixes), but the
  review's specific count of four is not what this build produces.
- **c09** (`a[0)`): the review describes two diagnostics at `2:23` (an
  `insert "]"`) and `2:24` (`EXS-E0201` "on `)`"), implying the second
  diagnostic lands on the `)` a second time. Measured behaviour is
  different in kind: there is exactly ONE diagnostic on the `)` itself
  (unexpected, wanted `]`, with the `insert "]"` fix at that same position),
  and the SECOND diagnostic lands on whatever token follows the `)` (the
  trailing `;`, in this reconstruction) -- not on the `)` again. The
  diagnostic count (2) matches; which tokens carry them does not.

Both of these are recorded rather than corrected -- CLAUDE.md's evidence
discipline cuts both ways, and a corpus that quietly edits itself to hit a
number it's supposed to be checking is worse than useless.

### A bug found by running, not in the review

Building c32 turned up a compiler defect the review's table does not
mention: **a malformed numeric literal (`EXS-E0210`) anywhere in a file
silently suppresses a reserved-word-as-identifier diagnostic (`EXS-E0220`)
elsewhere in the same file, in either order.**

```
firma per: u64 = x;       // alone: EXS-E0220 "per", correctly
firma bad: u64 = 1ab;     // alone: EXS-E0210 "1ab", correctly
```

Put both in the same file (same statement, adjacent statements, or with one
per function) and only the `EXS-E0210` survives; the `EXS-E0220` is dropped
entirely, regardless of which mistake comes first in the file. Verified with
four separate minimal repros against `e5f283d`'s `build/exsc`. This is
outside this corpus's exclusive write scope (`compiler/x86_64/diag/` and
`compiler/x86_64/lexer/` are not writable from here) -- reported for the
owning agent to investigate, not fixed here. `c32_garbage_soup.exsc` was
redesigned to avoid the interaction so it measures six independent mistakes
rather than accidentally demonstrating this defect instead.

## The BEFORE measurement

Per instructions, the pre-fix compiler is commit `e5f283d` -- built in a
clean detached worktree (`git worktree add --detach ... e5f283d`, then
`nix develop --command bash -c 'make all'`), with this corpus and
`tools/diag-measure.sh` copied in (neither exists at that commit). The
worktree was removed after measuring.

Note on provenance: the review states it ran `build/exsc` "239,904 bytes,
`make all` at `2af4d30`". That commit exists and is an ancestor of `e5f283d`
(sixteen commits back; `git merge-base --is-ancestor 2af4d30 HEAD` succeeds).
The build measured here is at `e5f283d`, not `2af4d30`, and is 241,949 bytes;
the review's binary was not rebuilt and its size is `[UNREPRODUCED]`. An
earlier draft of this paragraph claimed `2af4d30` was absent from the
history -- it was a worktree-scoped `git log` that missed it, and the claim
was wrong.

**Measured** (`tools/diag-measure.sh --exsc <e5f283d build/exsc> --corpus tests/diagnostics`):

```
cases run:          22
total diagnostics:  37

-- by message text --
  28  75.7%  'unexpected token'
   3   8.1%  'reserved keyword used as identifier'
   3   8.1%  'unterminated construct'
   1   2.7%  'bidi or invisible control character in source'
   1   2.7%  'malformed literal'
   1   2.7%  'unexpected end of input'

-- by code --
  28  75.7%  EXS-E0201
   3   8.1%  EXS-E0202
   3   8.1%  EXS-E0220
   1   2.7%  EXS-E0103
   1   2.7%  EXS-E0203
   1   2.7%  EXS-E0210

most common message: 'unexpected token' -- 28 of 37 (75.7%)

record richness: note 0/37 (0.0%), related 0/37 (0.0%), fix 24/37 (64.9%)
```

**This is 37 diagnostics, 28 of them (75.7%) `unexpected token` -- not the
review's headline 46/37 (80%).** That is expected and not a failure to
reproduce: this corpus covers only the **22 cases the review's table
describes**, not all 34 programs the review says it ran. The review's 46/37
figure is a total over programs this repository has no way to reconstruct
(12 of the 34 are never described past being counted). What this
measurement DOES confirm, independently, at the case level: every one of the
22 reconstructable cases behaves exactly as the review's per-case row says
(with the two exceptions above), `unexpected token` / `EXS-E0201` is
overwhelmingly dominant (75.7% here; 80% over the fuller original corpus),
`note` and `related` are 0% populated because `struct Diag` at this commit
has no field for them (D1, confirmed structurally, not just by count), and
`fix` is populated on a majority of diagnostics including several outside
the codes spec §8.3 scopes it to (D3, also confirmed).

The apparent coincidence that this corpus's "unexpected token" count (37)
matches the review's *total* diagnostic count (37 of 46) is exactly that --
a coincidence of a smaller sub-corpus, not a cross-check of the original
number.

## Should this be a gate?

Not wired here (out of scope for this change, and `tests/run.sh` is not this
corpus's write scope). If a future change wants to wire this in as a Stage 2
entry gate, the natural threshold is on message diversity rather than raw
count, since D1 is about repetition, not volume: e.g. "no single message
text may account for more than N% of diagnostics on this corpus" with N
around 40-50%, well below the 75.7% measured here and well above what a
corpus with real per-construct messages (D1's fix) should produce. Whatever
threshold is chosen should also gate on `note` and `related` population
(e.g. "every diagnostic on an `EXS-E020x` code carries a non-null `note`"),
since a note field that exists but goes unpopulated at half the construction
sites would pass a message-diversity gate while leaving D1 half-fixed.
