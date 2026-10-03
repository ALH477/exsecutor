# tools/agent/loop — generate, compile, read the diagnostic, retry

`loop.py` puts a language model (meant for a small fine-tuned one, ~3B) in a
loop with the real compiler: the model writes Exsecutor, `build/exsc` checks
it, and the loop sends exsc's diagnostics back as the next user turn until the
program is accepted, the model repeats itself, or the iteration budget runs
out. Python 3 standard library only; the backend is any OpenAI-compatible
`/v1/chat/completions` server on loopback, spoken to with `urllib`.

**Status: nothing here has been run against a real model.** Every result
below comes from `selftest.py`, which drives the loop with `mock_backend.py`
(scripted replies) and the real exsc. How well a model does in this loop is
`[UNTESTED]`.

## Use

```
make                                              # build/exsc (needs fasmg on PATH)
python3 tools/agent/loop/selftest.py              # no model needed; PASS/FAIL per case

llama-server -m model.gguf --port 8080 -c 8192    # llama.cpp
python3 tools/agent/loop/loop.py --endpoint http://127.0.0.1:8080 --model local \
    --task "Return gcd(48, 18) as the exit status." --transcript t.jsonl --out prog.exsc

ollama serve                                      # ollama, port 11434
python3 tools/agent/loop/loop.py --endpoint http://127.0.0.1:11434 --model MODEL_TAG \
    --task-file task.txt --transcript t.jsonl
```

Both server lines are `[UNTESTED]` here. llama-server ignores `--model`;
ollama needs the tag it serves. A 3B model's default context may be too small
for six rounds: the system prompt alone is ~916 tokens (characters / 4, an
estimate, not a tokenizer count; `loop.py --print-system-prompt` prints it).

Flags worth knowing: `--max-iters 6`, `--temperature 0`, `--seed N`,
`--block last` (which fenced block is the program), `--workdir DIR` (default:
a fresh directory under `/dev/shm`, else the temp dir, removed afterwards
unless `--keep-workdir`), `--exsc`, `--fasmg`, `--include`, `--repo`.
Exit status: 0 converged, 1 not converged (`max_iters` or `stuck`), 2 usage
error or refusal, 3 backend failure, 4 runner failure.

## Running what the model wrote

Default is **check only**: exsc type-checks the program and nothing the model
wrote is ever executed. Model output is untrusted data: it goes to
`<workdir>/iter-NN/prog.exsc` (a fixed name, whatever the reply says), is
passed to exsc as an argv list, and is never put in a shell string.

`--run` builds (`exsc -o`, then `fasmg`) and executes the binary **only**
through `--runner PATH`, a sandbox runner called as `RUNNER ELF_PATH`; `--run`
without `--runner` is refused before the backend is contacted. Runner exit
statuses: the program's own, 124 timeout, 125 runner failure (stops the loop,
exit 4), 132 SIGILL (with `abortus N` on stderr: a runtime abort), and, as
`tools/agent/usb/sandbox_run.py` adds, 159 SIGSYS and 128+N for signal N
(reported as "exit status or signal", never as success unless
`--expect-exit` asks for that number). A program that itself exits 124, 125,
132 or 129-159 is indistinguishable from those; that is the contract's, not
the loop's. `stub_runner.sh` is a **test stub and not a
sandbox**: it runs the ELF directly and exists only for `selftest.py`.

## The feedback message

Machine facts only: the codes, each with its meaning read from the §13 table
of the spec at run time (never from memory), `line:column`, the snippet, the
source line, exsc's `note`, and exsc's own `fix` (machine-applicable) or
`suggestion` (a hint), described in the dataset's words. Each edit is then
**applied alone and re-checked**, and the message says whether exsc then
accepts the program. That matters: many of exsc's suggestions make a program
worse (below). With `--run` it adds `exit status N`, `aborted: abortus N` or
`timed out`, and the mismatch against `--expect-exit` / `--expect-stdout`.

## Reading a transcript

One JSON object per line, ASCII, keys sorted, the work directory scrubbed to
`<work>`, no clock or hostname. Same model replies in, same bytes out
(checked by `selftest.py` across two work directories).

- `header`: model, endpoint, task, settings, the full system prompt and its
  size, SHA-256 of exsc, the spec and the lexicon.
- `iteration`: `request` (the exact payload sent), `response.text`, `code`
  and `code_source`, `check` (exsc's exit and full JSON diagnostics),
  `edit_checks`, `run`, `feedback` (the next user turn), `outcome`.
- `verdict`: `converged` | `max_iters` | `stuck` | `backend_failure` |
  `runner_failure`.

```
jq -r 'select(.kind=="iteration") | "\(.n) \(.outcome) \([.check.diagnostics[]?.code]|unique)"' t.jsonl
jq -r 'select(.kind=="iteration") | .feedback // empty' t.jsonl
```

## Limits, stated plainly

- **A clean check means valid Exsecutor, not a program that does the task.**
  Without `--expect-exit N` / `--expect-stdout S` there is no oracle for
  correctness at all; `converged` then means "exsc accepted it" (and, with
  `--run`, "it exited without aborting or timing out").
- exsc's suggestions are often wrong. Measured on this tree: for `a == 1`
  and `a && b` the suggestion is "insert `{`", which adds `EXS-E0202`; for
  `if a > 10` it is "insert `;`"; `while` draws the note "did you mean the
  reserved word si"; `else`, `==`, `&&` and `%` get no note naming `aliter`,
  `eq`, `et` or `residuum`. The loop reports these, labelled, with the
  re-check result. It does not hide or rewrite them.
- A machine fix can **widen authority**: for `EXS-E0421` exsc's fix is
  "insert `poscit ambitus`". A model that follows it gets a program that
  checks because it now asks for more capability. Review the `poscit` rows of
  any converged program; this loop is not a capability review.
- The cheat sheet's rules are re-measured by `selftest.py` (bad form draws
  the stated code, good form is clean, both examples run). Rule 7 (`dum`
  needs `terminus`) is a convention: exsc accepts a bare `dum` outside
  `profilum certum`. Rule 8 (no string escapes) is measured: `"\n"` prints a
  backslash and an `n` (§8.1: the escape grammar is `[OPEN]`).
- Stuck detection compares extracted programs exactly; a model that cycles
  through trivially different wrong programs runs to `--max-iters`.
- The whole conversation is resent every iteration (no truncation).
