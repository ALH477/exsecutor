# datasets/finetune — the Exsecutor lexicon and language, as a fine-tuning set

A supervised fine-tuning dataset for the Exsecutor language: its Latin lexicon
(§3, §8.4), its capability system, its numeric and wire-format rules, its
error registry, and a corpus of programs. It is generated, not hand-edited, and
**every code example in it was run through the real compiler before it was
allowed in.**

> Prose designs are hypotheses until code runs. (CLAUDE.md)

The dataset follows the same rule. What it can and cannot claim is in
[What is and is not evidenced](#what-is-and-is-not-evidenced), and the answer
to "did training on it make a model better at Exsecutor?" is `[UNTESTED]`:
no model has been trained on it.

## Files

| file | what |
|---|---|
| `all.jsonl` | every record, one JSON object per line |
| `train.jsonl`, `validation.jsonl` | the same records split by group (below) |
| `lexicon.json` | the structured lexicon the records were built from: reserved and contextual words with Latin glosses, capability atoms, operators and operator words, the §13 registry, §3's morpheme tables |
| `manifest.json` | counts by task and by evidence, and a SHA-256 of every input and output |
| `src/cases/*.exsc` | the hand-written programs, one per file, each declaring what `exsc` must do with it |
| `src/keywords.json` | editorial glosses (Latin, English analogue, role) for the §8.4 words, operator words, root-coinage Q&A |
| `src/qa.json` | spec-grounded conceptual Q&A |

The generator is `tools/gen-finetune.py`. Counts are in `manifest.json`, not
here, so this file cannot go stale against them.

## Record format

Chat format, one single-turn exchange per line. No system message is included;
add your own (the content has no dependence on one).

```json
{"id": "fix-e0201-symbolic-comparison.fix",
 "task": "fix",
 "messages": [{"role": "user", "content": "..."},
              {"role": "assistant", "content": "..."}],
 "meta": {"evidence": "exsc-diagnostic", "codes": ["EXS-E0201", "EXS-E0303"],
          "group": "fix-e0201-symbolic-comparison", "source": "...", "spec_refs": ["§8.4"]}}
```

`task` is one of:

| task | prompt | answer |
|---|---|---|
| `lexicon` | what a word, operator, atom, code, root, suffix or prefix means; the English-to-Exsecutor mapping; a verified example using a word | the gloss, tier, role and spec section |
| `concept` | a question about a language rule (capabilities, memory, numerics, syntax, determinism) | the rule, cited by section, with `[OPEN]`/`[UNTESTED]` carried through |
| `write` | a task in English | a program, then why it is written that way |
| `explain` | a program | what it does and why |
| `trace` | a runnable program | its exit status or abort, and stdout where it writes any |
| `fix` | a program that does not compile | the exact `EXS-E` codes, the compiler's own edit, and a corrected program |
| `diagnose` | a program that does not compile and has no fixed twin | the rejection and the rule |
| `translate` | C or Rust | Exsecutor |

### `meta.evidence` — how each record was checked

| value | meaning |
|---|---|
| `exsc-run` | compiled by `exsc`, assembled with `fasmg`, executed; exit status (or `abortus N` + SIGILL) and stdout matched what the case declared |
| `exsc-diagnostic` | `exsc` rejected the program with **exactly** the declared set of codes (not merely containing them), and the corrected twin was accepted |
| `exsc-check` | `exsc` accepted the program: it lexes, parses and type-checks. It was **not** executed |
| `spec-table` | read mechanically from a spec table by the generator |
| `spec-text` | a statement the spec makes, cited by section. The spec is the source of truth and has been wrong before |
| `editorial` | an author-supplied gloss. Not normative |

Filter on this field. A run that wants only machine-checked code can take
`exsc-run` and `exsc-diagnostic`; one that wants only the language's own words
can take `spec-table`.

### Using it

Most trainers take the chat format directly. For a prompt/completion trainer,
and to keep only the records a compiler checked:

```
jq -c '{prompt: .messages[0].content, completion: .messages[1].content}' train.jsonl
jq -c 'select(.meta.evidence == "exsc-run" or .meta.evidence == "exsc-diagnostic")' all.jsonl
```

### `meta.group` — and why the split is by group

Several records come from one program (its `write`, `explain`, `trace` and `fix`
records), and a keyword's "show me an example" record reuses a case's code.
Splitting by record would put one program on both sides and make the
validation set measure memorisation. The split is a function of `meta.group`
(`sha256(group)[0] mod 10 == 0` is validation), so a program and everything
derived from it stay together. The split is therefore by group, roughly a
tenth, and **not stratified by task**: read `manifest.json` before trusting a
per-task validation number, since a small task can land with very few
validation records.

## What is and is not evidenced

**Code is evidenced; prose about code is the author's reading of it.** The
program in a record is what `exsc` accepted or rejected. The sentences around
it, such as why `sin` exists or what a note means, are written by a person and
checked against the spec by reading, not by a machine. Where the compiler's own
edit is quoted (`replace `<` with `lt``), it is exsc's JSON payload, and the
record says whether applying it alone made the program compile, which the
generator re-ran.

Things that are deliberately **not** claimed:

- **The morphology records are `[UNTESTED]` against the compiler.** §3.3–§3.5
  say their tables are illustrative and are not the morpheme table
  (`lexicon.norma` does not exist). The checker's lexicon pass is built and not
  enabled in the driver, so `EXS-E0601`–`E0603` and `E0610` are not raised by
  any compiler run. The derivation records are mechanical applications of
  §3.4's suffixes to §3.3's two stems, and say so. Do not read them as "the
  compiler accepts `lectorium`".
- **Some constructs are checked and not run**, and the records say so in the
  text: lambdas and text-view methods (`t.octeti().numerus()`, `plica_unicode`)
  are known cases, and `evidence: exsc-check` marks every record whose program
  was type-checked and not executed. Measured on this tree: `exsc ... -o` on a
  program containing a lambda or a text-method call terminates with `SIGILL`
  instead of refusing cleanly. `tests/conformance/entry12_capability_behind_bare_dyn.exsc` and
  `docs/design/lowering.md` record the lowering as refusing them. That is a gap in the compiler, not something the dataset
  smooths over.
- **Spec §5.1 is stale on one word.** It says `t.octeti().numerus()` "types
  and runs"; it types and cannot be emitted. The dataset follows the
  measurement. The spec was not edited as part of this change.
- **`ego` files are not exercised.** The CST parses no `ego` block, so the nine
  `ego`-structure words and the `numeri` keys have glosses and no program.
- **The glosses are editorial.** Latin etymology and the English analogues in
  `src/keywords.json` are the author's, not the spec's, and carry
  `evidence: editorial`.
- **Nothing measures a trained model.** No benchmark, no held-out evaluation,
  no comparison. The validation split exists so that someone can; nobody has.

## Regenerating

```
make                                   # builds build/exsc (needs fasmg)
python3 tools/gen-finetune.py          # rewrites this directory
python3 tools/gen-finetune.py --check  # regenerates in memory and diffs
```

It needs `build/exsc` and `fasmg` on `PATH`, and it **refuses to write a single
line** if any case disagrees with the compiler: a case that expects `ok` and
draws a diagnostic, a case that expects exactly `{EXS-E0201}` and draws
`{EXS-E0201, EXS-E0303}`, or a run whose exit status or stdout differs. The
generator is verification tooling and is never on the build path (§18.1).

It is deterministic: no clock, no hostname, no environment beyond finding
`fasmg`, every list in spec or file order, the split a function of the group.
Regenerating from unchanged inputs reproduces every file byte for byte, and
`manifest.json` records the SHA-256 of the spec, the editorial files, the cases
and every output.

The generator also reads the spec's tables rather than copying them:

- `src/keywords.json` can only gloss what §8.4 lists. The generator stops if
  its reserved or contextual set differs from the spec's, so editorial text
  cannot add a word. A new reserved word is a §8.4 amendment first.
- Every §13 code must appear in `compiler/x86_64/diag/codes.inc`, or the
  generator stops. **No code in this dataset was invented**; the only source is
  §13.
- Text is checked to be NFC, LF, and BOM-free before it is written (§8.1).

## Adding a case

A case is one `.exsc` file in `src/cases/`. Its leading `//!` lines are
metadata, and everything after them is the program (the metadata lines are
ordinary comments, so the file is a valid Exsecutor source as it stands).

```
//! task: fix                      write | fix | diagnose | translate
//! ask: Return a true value.      what the user asks (continuation lines: `//!` + 3 spaces)
//! expect: EXS-E0301              ok | exit=N | abort=N | EXS-Exxxx[,EXS-Exxxx...]
//! stdout: text\n                 optional, with exit=N; checked exactly
//! primary: EXS-E0201             optional; which of several codes is the cause, not cascade
//! refs: §8.4                     spec sections
//! explain: why, in one line.     the assistant's reasoning, restating the spec

publica functio f() -> u1 {
    redde true;
}
//@ fixed
publica functio f() -> u1 {
    redde 1;
}
```

`expect:` is a **set**, matched exactly. A program that "should" draw one code
and draws two is a finding about the compiler's recovery, and the case must
list both and say which is primary. Run the generator; it will not write
anything until every case agrees with the compiler.

Rules for cases, which are CLAUDE.md's rules:

- Never invent an `EXS-E` code, and never write an `expect` for a code that
  isn't in §13.
- Don't write an `explain` that says more than the spec does. A claim with
  nothing behind it carries `[OPEN]` or `[UNTESTED]`.
- If a construct type-checks and cannot be run, the case says `[OPEN]` and the
  evidence is `exsc-check`; it never gets an `exit=` it did not earn.
- A security boundary (the capability checker, `sicut` into a capability atom)
  is a *negative* case here: the dataset teaches what the compiler refuses.
  It does not substitute for the adversary pass CLAUDE.md requires before
  changing the checker, and a green dataset says nothing about whether the
  checker has a hole.

## Licence

The repository is GPL-3.0-or-later with the output exception in
`LICENSE.EXCEPTION`, and this directory is part of it. The records quote the
spec, the compiler's diagnostics and programs in this tree. **Whether a model
trained on GPL material is itself bound by it is not settled by this file and
is not something the generator can decide.** If you train and distribute
weights, take that question to whoever owns the project's legal position
before you do.
