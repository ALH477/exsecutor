# tests/

Two phases, run by `tests/run.sh` (no arguments; this is what `make test`
calls):

1. **`tests/unit/`** — real, running today. `.asm` fixtures assembled
   directly with `fasmg`, independent of the Exsecutor compiler. Right now
   the only thing that can actually be tested in this repo is the
   toolchain itself (`fasmg` + `vendor/fasmg-x86`) and `tools/syscall-audit.sh`,
   so that is what these fixtures exercise.
2. **`tests/conformance/`** — spec §14's 17-entry suite. **Empty on
   purpose.** Every entry needs `exsc` to compile something and check a
   diagnostic code, or (entries 16–17) check byte-identical output; there
   is no compiler yet (`compiler/x86_64/exsc.asm` is unwritten — see
   `CLAUDE.md`). `tests/run.sh` reports this phase as 0 entries, not as a
   failure, via a dedicated `run_conformance_tests` function kept separate
   from the unit-test runner for exactly this reason: wiring up real
   entries later should mean writing fixtures and filling in that one
   function, not restructuring the harness.

## What is deliberately absent

- **`tests/conformance/` entries.** See above. When `exsc` exists, each of
  the 17 rows in spec §14 needs a source fixture plus an expectation (a
  diagnostic code for 15 of them; byte-identical output under varied
  conditions for #16, which is exactly what `tools/reproduce.sh` already
  checks; byte-identical output cross-host for #17).
- **Performance/benchmark tests.** Nothing to benchmark yet. CLAUDE.md:
  "Never report a benchmark you did not run" — there will be no benchmark
  fixtures here until there is something real to measure.
- **aarch64/riscv64 fixtures.** The compiler is x86-64 assembly (spec
  §18); those hosts are full rewrites, not a fixture-file difference.
- **LSP/formatter tests.** Both deferred per spec §18.2.

## `tests/unit/` fixtures

Each `.asm` file may carry one directive comment, anywhere in the file,
read by `tests/run.sh`:

```
; TEST: run=yes|no  expect-exit=<N>  audit=pass|fail|skip
```

All three keys are optional; defaults are `run=yes expect-exit=0
audit=skip`. `tests/run.sh` always assembles every fixture; `run=no` skips
*executing* the resulting binary (for a fixture whose point is static
detection, not runtime behavior — see `socket_syscall.asm`); `audit=pass`
or `audit=fail` runs `tools/syscall-audit.sh` on the assembled binary and
asserts its verdict.

Current fixtures:

| file | proves | run | audit |
|---|---|---|---|
| `smoke.asm` | fasmg + vendor/fasmg-x86 produce a working freestanding ELF64 at all | exit 0 | **fail** (see below — deliberate) |
| `clean_syscalls.asm` | every syscall in CLAUDE.md's closed allowlist is reachable, and both rax-resolving idioms (`mov eax,imm` and `xor eax,eax`) the audit trusts are exercised | exit 0 | pass |
| `socket_syscall.asm` | the audit hard-rejects a socket-family syscall (`socket`, nr 41) | not executed | fail |
| `indeterminate_syscall.asm` | a syscall number routed through a second register (`mov ebx,231` / `mov eax,ebx`) is reported `INDETERMINATE` and fails the audit, rather than being silently skipped or accidentally resolved | not executed | fail |

`socket_syscall.asm` is not executed because the point is proving
`tools/syscall-audit.sh` catches it by static inspection — actually
invoking a live `socket(2)` from the test harness is unnecessary and
outside what this suite needs to touch to make its point. A negative test
that proves the audit catches what it claims to catch is worth more than
several positive ones; this is that test, and `tools/syscall-audit.sh
--self-test` runs it specifically to demonstrate the rejection before
`exsc` exists to produce a real target.

**Why `smoke.asm` is an audit `fail`, and that's correct, not a bug:**
`smoke.asm` is preserved exactly as independently verified — assembles to
241 bytes, runs, exits 0 — and it ends with `mov eax,60` (`exit`).
CLAUDE.md's closed syscall allowlist names `exit_group` (231), not `exit`
(60). Under a *closed* allowlist, "harmless" is not the same claim as
"allowlisted": `exit(60)` is completely benign and still fails the audit,
because it was never on the list. Rather than quietly changing the
verified smoke fixture, this suite keeps it as originally proven and adds
`clean_syscalls.asm` — identical in spirit, `exit_group` instead of
`exit` — as the fixture that is actually expected to pass. Both facts are
asserted by `tests/run.sh` and checked on every run.

## Adding a case

**A new `tests/unit/` fixture:** drop a `.asm` file in this directory with
a `; TEST:` line describing what should happen, and `tests/run.sh` picks
it up automatically — nothing else to wire up. Prefer one fixture per
thing being proven, and say in a comment what it proves; see the existing
three for the pattern.

**A new `tests/conformance/` entry (once `exsc` exists):** add the source
fixture under `tests/conformance/`, then extend
`run_conformance_tests()` in `tests/run.sh` to compile it and check the
expected diagnostic code (or, for entries 16–17, the expected
byte-identical-output property) — that function is currently a stub
specifically waiting for this.

## Known limitation

`make test` (and `make reproduce`) currently cannot run to completion via
`make` at all: both targets depend on `$(OUT)` (`build/exsc`), and
`build/exsc`'s own rule depends on `compiler/x86_64/exsc.asm`, which does
not exist yet — `make` fails at that dependency before ever invoking
`tests/run.sh` or `tools/reproduce.sh`. Both scripts are written to
degrade honestly and do useful work without `exsc` (see their own
`--help`-equivalent header comments), but that only happens when they are
invoked directly, e.g. `tests/run.sh` or `tools/reproduce.sh`, bypassing
`make`. This is a `Makefile` prerequisite question, not something this
directory owns — noted here so it isn't mistaken for a bug in these
scripts.
