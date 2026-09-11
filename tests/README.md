# tests/

Four phases, run by `tests/run.sh` (no arguments; this is what `make test`
calls):

1. **`tests/unit/`** — real, running today. `.asm` fixtures assembled
   directly with `fasmg`, independent of the Exsecutor compiler. Right now
   the only thing that can actually be tested in this repo is the
   toolchain itself (`fasmg` + `vendor/fasmg-x86`) and `tools/syscall-audit.sh`,
   so that is what these fixtures exercise.
2. **`tests/conformance/`** — spec §14's 24-entry suite. **All 24 cases
   are written; 10 run.** `exsc` now exists, so the entries the lexer can
   decide are checked against a real diagnostic: 3 (bidi in a comment,
   `EXS-E0103`), 5 (non-NFC, `E0102`), 18 (BOM, `E0101`), 19 (mixed-script,
   `E0104`), 20 (CRLF, `E0106`). The wire-codec branch's `@transitus`
   layout checker and type checker moved four more from `DEFERRED` to run:
   6 (`:nativus` in a wire struct, `E0321`), 7 (implicit padding in a wire
   struct, `E0322`), 9 (a row-carrying function where a bare function type
   is expected, `E0303`), 21 (bit widths not summing to a whole byte,
   `E0322`). **Entry 23 runs its certificate**: the DeModFrame codec
   written in Exsecutor (`conformance/entry23/codex.exsc`, pure;
   `probatio.exsc`, the driver) is compiled with the fixture as one unit,
   run, audited (`write` and `exit_group` only), and its 2,502-byte stream
   compared section by section with what `entry23/expecta.py` builds from
   `vendor/hydramesh-wire/golden_vectors.json` — 246/246 certificate
   vectors, the three anchors and two laws reported apart — after which
   three mechanical mutants must each fail at the vector
   `docs/design/wire-codec.md` section 6.1 names. `entry23/` is a
   subdirectory so the `*.exsc` glob does not take its files for fixtures.

   Each `shape=code` entry above is checked against the exact SET of
   `EXS-E` codes in exsc's `--diagnostica json` output (JSON Lines — one
   object per diagnostic), required to equal `{expect-code}` after
   deduplication, not merely to contain it — a substring/presence check
   passes a fixture that ALSO emits an unrelated second diagnostic, which
   is exactly what a prior version of this check did. That tightening
   moved **entry 22** (`u4:maior`, `E0201`) to `DEFERRED`: it now emits
   `E0201` *and* a spurious `E0322` from the wire-layout checker running
   over the parser's recovery from the unparseable annotation — a real
   compiler-side cascading-diagnostic gap (`needs=parser_error_recovery`),
   not a fixture defect, and outside this tree to fix (CLAUDE.md's Scope).
   Entry 19 (mixed-script) was a genuine fixture defect instead — two
   missing `;`s that a source-policy fast-path had been masking on 3/5/18/20
   but not on 19, whose `E0104` is raised later, mid-lex, without halting
   the parser — and was fixed in place; both `xа` occurrences still raise
   `E0104`, which is one element of the set, not two.

   The other 14 report **`DEFERRED`** with what they wait on —
   `type_checker`, `capability_checker`, `import_closure`, `ffi_checker`,
   `lexicon_checker`, `backend`, `runtime`, `cross_compile`,
   `parser_error_recovery` — and are **never counted as passing**. A suite
   reporting 24/24 while running 10 would be worse than no suite.

   Five rule shapes, and a runner assuming one will quietly mishandle four:
   reject-with-exact-code (most), byte-identical output (16, 17), external
   certificate (23), runtime abort (15), capability absence with no code
   assigned (1).

   `run_conformance_tests` carries its own floor, mirroring
   `UNIT_FIXTURE_FLOOR`. It earned that immediately: with `exsc.asm`
   untracked the Nix sandbox could not build it, 0 entries ran, and
   `nix flake check` FAILED loudly instead of printing a green subset.
3. **`tests/ir/`** — SSA IR text, RUN. `emit_ir.asm` reads a `.ir` file on
   stdin, parses it, **verifies** every function (ssa-ir.md section 3 —
   the driver never calls the verifier; this harness does), and prints the
   whole fasmg program `bfa_emit_program` makes of it, with the hello
   world's closure `{Mundus, ambitus}` and MXCSR `0x1F80`. The phase
   assembles that, runs it, and checks the exit status or abort and stdout.
   48 fixtures (`IR_FIXTURE_FLOOR`).
4. **`tests/programs/`** — Exsecutor sources, RUN. Each directory is one
   program: `exsc aedifica --hospes x86_64-linux SRC... -o OUT`, `fasmg OUT
   BIN`, run, check. 13 programs run (`PROGRAM_FIXTURE_FLOOR`); none is
   deferred (the `status=deferred` mechanism below stays for the next one).
   Four are HydraModem's transmitter, `examples/hydramodem/`: three WAVs
   (`hydramodem_{loopback,exemplum,vacuum}/`) and the 137-word symbol-stream
   basis (`hydramodem_basis/`, `docs/design/modem.md` D9), each `cmp`ed
   against `vendor/hydramodem-tx/`; the basis TEST says how a failing byte
   names its word and symbol.

Phases 3 and 4 are the first in this script to execute code a compiler
*emitted*. Until them a `tests/unit/` fixture could only compare emitted
text — it cannot `execve` — and the running half was done by hand and
reported (`bfa_emit_tier1.asm`'s header says so). Every binary either phase
runs is also audited with `tools/syscall-audit.sh --potestates
Mundus,ambitus`, the publish gate's own invocation; `emit_ir` itself is
audited against the compiler's closed nine, since it is built from the
compiler's modules and everything phase 3 reports is trusted through it.
`make audit` still audits `build/exsc` alone: `emit_ir` is test tooling, not
a shipped artifact, and is checked every time it is built instead.

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
  different in kind from the other twenty-three: they are cases this project
  wrote for itself, while 23 is an external certificate it must satisfy —
  `vendor/hydramesh-wire/golden_vectors.json`, 246 vectors, equivalent under
  its own theorem to agreeing with the reference on all 2^108 frames **given
  that the codec is affine** (bit placement plus a CRC) — a premise argued
  from reading `entry23/codex.exsc`, not measured; the example frame in the
  stream's third section is one non-basis spot check of it.

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

## `tests/ir/` and `tests/programs/` — the run directives

Both phases read the same keys (space-separated `key=value`, the unit
directive's style):

| key | meaning |
|---|---|
| `expect-exit=N` | the program exits normally with `N`; its stderr must be empty |
| `abort=N` | spec §6.6's one abort shape: killed by `SIGILL`, with a stderr line ending `abortus N`. Not shell status 132, which `redde 132;` also produces — the runner tells a signal from an exit status |
| `stdout=PATH` | stdout byte-identical to `PATH`, relative to the repo root. Absent: stdout must be empty |

Exactly one of `expect-exit=` / `abort=` is required for anything that
runs. **An unknown key fails the fixture** — the unit directive silently
ignores one, so a typo there falls back to the default and passes.
Binaries run with stdin from `/dev/null`, an empty environment, and a
20-second limit.

**`tests/ir/*.ir`:** the directive is a `; TEST:` line in the IR itself
(`;` is the IR's comment). One more key, `emit-exit=N` — `emit_ir`'s own
status (3 parse error, 4 emitter refusal, 5 verifier verdict;
`emit_ir.asm`'s header has the table). Non-zero makes the fixture a
rejection: nothing is assembled or run.

**`tests/programs/<name>/`:** the directive is the `TEST:` line of a file
named `TEST` in the directory (`#` lines are comments). The compilation
unit is the directory's own `*.exsc` in byte order (`LC_ALL=C`), or
`sources=A,B,...` — repo-relative, in that order — to compile sources that
live elsewhere; `saluta/` uses it to build the hello world from `examples/`
rather than keep a second copy. A file `expected.out` is the stdout
reference when present. `exsc-exit=N` expects `exsc` itself to fail with
shell status `N` and runs nothing; `ordo_maior_custodia/` used it for the
lowering's byte-order guard until milestone M6 removed the guard, and the
positive test of the read it refused is now `tests/unit/lwr_transitus.asm`.

`status=deferred needs=A,B` marks a program written ahead of the backend
that can run it, in the conformance suite's sense of the word: it is
compiled without `-o` (lexed, parsed, type-checked), that must exit 0 and
is counted as one check, and nothing is assembled or run. It is reported
`DEFERRED` with what it waits on and never counted toward
`PROGRAM_FIXTURE_FLOOR`, which counts directories that run. Its
`expect-exit=`/`abort=`/`stdout=` stay in the directive, so un-deferring
is deleting two keys. `forma/` was the first: milestone M6's program, which
lowered to verified IR (`tests/unit/lwr_forma.asm` still lowers that very
file) while it waited on the emitter's M3/M4 opcodes; since aca9755 it runs
(`expect-exit=0`), and no directory is deferred at the time of writing.

## Adding a case

**A new `tests/unit/` fixture:** drop a `.asm` file in this directory with
a `; TEST:` line describing what should happen, and `tests/run.sh` picks
it up automatically — nothing else to wire up. Prefer one fixture per
thing being proven, and say in a comment what it proves; see the existing
three for the pattern.

**A new `tests/ir/` or `tests/programs/` case:** add the `.ir` file or the
directory with its `TEST` file, then raise `IR_FIXTURE_FLOOR` or
`PROGRAM_FIXTURE_FLOOR` in `tests/run.sh` in the same commit, and show it is
not vacuous (CONTRIBUTING.md: break the thing, watch it fail, restore).

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
