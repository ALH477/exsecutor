# tests/

Two phases, run by `tests/run.sh` (no arguments; this is what `make test`
calls):

1. **`tests/unit/`** — real, running today. `.asm` fixtures assembled
   directly with `fasmg`, independent of the Exsecutor compiler. Right now
   the only thing that can actually be tested in this repo is the
   toolchain itself (`fasmg` + `vendor/fasmg-x86`) and `tools/syscall-audit.sh`,
   so that is what these fixtures exercise.
2. **`tests/conformance/`** — spec §14's 24-entry suite. **All 24 cases
   are written; 6 run.** `exsc` now exists, so the entries the lexer can
   decide are checked against a real diagnostic: 3 (bidi in a comment,
   `EXS-E0103`), 5 (non-NFC, `E0102`), 18 (BOM, `E0101`), 19 (mixed-script,
   `E0104`), 20 (CRLF, `E0106`). The other 19 report **`DEFERRED`** with
   what they wait on — `type_checker`, `capability_checker`, `parser`,
   `backend` — and are **never counted as passing**. A suite reporting
   24/24 while running 5 would be worse than no suite.

   Five rule shapes, and a runner assuming one will quietly mishandle four:
   reject-with-exact-code (most), byte-identical output (16, 17), external
   certificate (23), runtime abort (15), capability absence with no code
   assigned (1).

   `run_conformance_tests` carries its own floor, mirroring
   `UNIT_FIXTURE_FLOOR`. It earned that immediately: with `exsc.asm`
   untracked the Nix sandbox could not build it, 0 entries ran, and
   `nix flake check` FAILED loudly instead of printing a green subset.

## What is deliberately absent

- **`tests/conformance/` entries.** See above. When `exsc` exists, each of
  the 24 rows in spec §14 needs a source fixture plus an expectation (a
  diagnostic code for 15 of them; byte-identical output under varied
  conditions for #16, which is exactly what `tools/reproduce.sh` already
  checks; byte-identical output cross-host for #17).

  **§14's source-policy coverage gap is closed.** It once exercised only
  three of §13's six source-policy codes; `EXS-E0101`, `EXS-E0104` and
  `EXS-E0106` had no entry at all, which is exactly the set a Stage 1 lexer
  implements first. Spec §14 entries **18-20** now cover them, appended rather
  than interleaved because entries are cited by number elsewhere — renumbering
  a referenced list is the same mistake as renumbering an error code (§8.3).

  Entries **21-23** followed, covering §5.2's bit-width rules. Entry 23 is
  different in kind from the other twenty-two: they are cases this project
  wrote for itself, while 23 is an external certificate it must satisfy —
  `vendor/hydramesh-wire/golden_vectors.json`, 246 vectors, equivalent under
  its own theorem to agreeing with the reference on all 2^108 frames.

  **24 entries, five rule shapes.** Most are "rejects with exactly code
  EXS-Exxxx". Entries 16 and 17 instead require byte-identical output across
  conditions and hosts. Entry 23 is the certificate. Entry 15 is a runtime
  abort, not a compile failure. Entry 1 is a capability absence with no code
  assigned. A runner that assumes one shape will quietly mis-handle four.

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

Current fixtures: see `UNIT_FIXTURE_FLOOR` in `tests/run.sh` (124 as of
b9c0abc; this sentence said 46 for a long time), covering the macro dialect,
every `rt/` module, the Unicode consumers, `diag/`, the lexer, the CST, the
AST, the driver, the backend and its verifier, the prelude, and the
checker. The four below are
the originals and are listed because each one proves something about the
*harness* rather than about a compiler module — including two negative cases
that prove the audit catches what it claims to.

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

## A limitation that no longer holds

This section said `make test` and `make reproduce` could not run because
`compiler/x86_64/exsc.asm` did not exist. It exists; both targets run (`make
reproduce` was itself broken from the day exsc.asm gained an include until
528fe7d, which is the kind of thing this file is for). Kept as a heading so a
reader who remembers the limitation finds its retraction rather than silence.
