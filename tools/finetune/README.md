# tools/finetune — a compiler-judged, held-out eval for a fine-tuned model

The question this answers: **did fine-tuning on `datasets/finetune/` make a
model better at writing Exsecutor that the real compiler accepts?** It
compares a base model with base + adapter on the dataset's held-out cases.
The compiler is the only judge: `build/exsc`, then `fasmg`, then execution of
the program. There is no string matching against the reference and no
model-as-judge.

It is model-agnostic. These scripts use only the Python 3 standard library
and never import a model library. You supply the generator.

## The chain

```
make                                              # build/exsc (needs fasmg on PATH)
python3 tools/finetune/make_eval_prompts.py       # -> tools/finetune/out/eval_prompts.jsonl
# your generator, once per model: read eval_prompts.jsonl, write {"id","text"} lines
O=tools/finetune/out
python3 tools/finetune/score.py $O/base.jsonl  --label base    # -> $O/base.results.jsonl
python3 tools/finetune/score.py $O/tuned.jsonl --label tuned   # -> $O/tuned.results.jsonl
python3 tools/finetune/compare.py $O/base.results.jsonl $O/tuned.results.jsonl
python3 tools/finetune/selftest.py                # proves the harness itself, no model needed
```

Every script takes `--help`. `out/` is gitignored.

**The generator's contract.** For each line of `eval_prompts.jsonl`, send
`messages` (one user turn, the dataset's prompt verbatim; add your own
system message if your template wants one) and write one line
`{"id": <the item's id>, "text": <the model's reply>}`. Use the same decoding
settings for both models. Greedy decoding makes a run repeatable; with
sampling, score several files and do not report the best one. One generation
per id per file. A missing id is scored as a failure (`missing_generation`),
so skipping hard items cannot raise a score.

## What is scored

| task | what is judged | metrics |
|---|---|---|
| `write`, `translate` | first ` ```exsecutor ` block, falling back to the first fenced block | `check`: `exsc` exit 0 and no diagnostic. `run`, only for `exit=N` / `abort=N` cases: assembled, executed, exit status (or `abortus N` + SIGILL) and stdout match |
| `fix` | the **last** fenced block (see below) | `check` on the corrected program, `run` if the fixed twin declares an exit; and, separately, `named_primary`: the reply's text names the case's primary `EXS-E` code |
| `trace` | the exit status stated in the prose (fences removed) | `answer`: equals the case's `exit=N`. Only `exit=` cases; `abort=` trace items are left out |

No code block at all is the failure `no_code_block`. The levels are reported
separately and never merged into one number. `pass` is a per-item headline:
`run` where there is one, else `check` (for `fix`, both); for `trace`, `answer`.

**One deliberate departure from "first block".** For `fix` the last block is
judged, because an answer to "what is wrong and how do I fix it?" often quotes
the broken program first. Judging the quote would fail exactly the answers
that are right, and would fail the base model more often than the tuned one,
biasing the comparison toward the adapter. `--fix-block first` restores the
literal rule.

## What a result does and does not mean

- **The sample is tiny.** On this tree there are 10 items from 8 case groups:
  8 `write`, 1 `fix`, 1 `trace`, and no `translate` item, because the
  dataset's split is by group and not stratified by task. Only 2 items have a
  `run` level. A 95% interval on 10/10 is about 72–100%. McNemar on paired
  items can reach p < 0.05 only with at least 6 discordant items, all one
  way. A null result here means the eval is too small to tell. It does not
  mean the adapter did nothing.
- **`check` on an `expect: ok` item means "valid Exsecutor", not "did what was
  asked".** A model that answers every `write` with a trivial function passes
  `check`. Only the `run` items test behaviour. Read the generations.
- **A `fix` that checks clean may have fixed the wrong thing.** For example,
  it may have deleted the loop instead of the `rumpe`. The compiler cannot
  tell intent.
- **Items in one group are not independent.** `run-fibonacci.write` and
  `.trace` share a program.

## Safety

Model output is compiled, and for `exit=`/`abort=` items it is executed in a
private temporary directory with an empty environment, no stdin and a 10 s
timeout. **That is not a sandbox.** Pass `--no-run` to score `check` only, or
run the scorer in a throwaway container.

## Provenance

`harness.py` copies the minimal judge logic of `tools/gen-finetune.py`'s
`Verifier`: the same `exsc` command lines, the same "clean" rule and the same
abort shape. It imports the generator only for `parse_case`, so case metadata
is read by the same parser that built the dataset. Importing it has no side
effects. Outputs carry no clock, hostname or path, and `selftest.py` checks
that scoring the same input twice gives the same bytes.
