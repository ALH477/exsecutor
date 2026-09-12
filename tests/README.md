# tests/

Four phases, run by `tests/run.sh` (no arguments; this is what `make test`
calls):

1. **`tests/unit/`** — `.asm` fixtures assembled directly with `fasmg`,
   each `include`-ing the compiler modules it exercises (or none: the four
   harness fixtures below). 166 of them (`UNIT_FIXTURE_FLOOR`), covering
   the macro dialect, every `rt/` module, the Unicode consumers, `diag/`,
   the lexer, the CST, the AST, the checker, the lowering, the backend and
   its verifier, the driver, and the prelude. This item once said the only
   thing testable here was the toolchain itself; that was true for the first
   four fixtures.
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
   run, audited (`read`, `write` and `exit_group` only -- the `read` is
   the `ambitus` atom's, carried by the gate rather than called), and its
   2,502-byte stream
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
   49 fixtures (`IR_FIXTURE_FLOOR`); the newest, `copy_magna.ir`, moves
   100,003 bytes through the emitter's loop form of `copy` (above 128 bytes)
   and checks five positions, one of them past the end.
4. **`tests/programs/`** — Exsecutor sources, RUN. Each directory is one
   program: `exsc aedifica --hospes x86_64-linux SRC... -o OUT`, `fasmg OUT
   BIN`, run, check. 96 programs run (`PROGRAM_FIXTURE_FLOOR`; this sentence
   said 22 for a long time); none is
   deferred (the `status=deferred` mechanism below stays for the next one).
   Beside the hello world and the arithmetic, loop, `discerne`, cast and
   struct programs: `lector/` and `lector_numerus/` (the reader, below),
   `acies/` (array literals at every admitted element type, with the
   transmitter's sine table tabulated and compared entry by entry against
   the function), and `copia_magna/` (a struct literal with an
   `acies<i64, 18000>` field — the exact shape that used to exhaust the
   compiler's arena with SIGILL and no message, `docs/design/receptor.md`
   finding 20, and now compiles and exits 0).
   Four are HydraModem's transmitter, `examples/hydramodem/`: three WAVs
   (`hydramodem_{loopback,exemplum,vacuum}/`) and the 137-word symbol-stream
   basis (`hydramodem_basis/`, `docs/design/modem.md` D9), each `cmp`ed
   against `vendor/hydramodem-tx/`; the basis TEST says how a failing byte
   names its word and symbol.

   Five more are its **receiver** (`docs/design/receptor.md`, milestone R2).
   `receptio_{loopback,exemplum,vacuum}/` are the transmitter's three tests
   turned round: `stdin=` is the vendored WAV and `expected.out` is the 17
   bytes the frame carries. `receptio_caput/` feeds a 44-byte header whose
   only fault is a 44,100 Hz sample rate — which HydraModem's own reader
   ignores — and expects exit 3 with no output. `receptio_circuitus/` is the
   loopback: 140 words out through the transmitter's `sona` and back through
   the receiver in one process, one byte a word, 140 zeros expected, so a
   failing word names itself by its offset. It is the slowest directory in
   the suite at 1.4 s (`9ede8bf`, three runs 1.38–1.41 s); one WAV decode is
   26 ms, about 10 ms of it system time for 38,060 one-byte `read`
   syscalls. Robustness — impaired input, a timing loop — is R3 and is not
   in this suite.

   Four are the **StreamDB v3 reader** (`examples/streamdb/`,
   `docs/design/c-backend.md` section 6). `streamdb_corpus/` feeds the
   24-document container the real upstream C writer produced
   (`vendor/streamdb-v3/`) on `stdin=` and compares a 32,591-byte certificate
   stream with `expected.out`: every key's payload **inline**, byte for byte
   against `vendor/streamdb-v3/payloads/`, then the document and trie-node
   counts, then both suffix searches in the traversal order
   `expectation.json` captured from the embedded reference reader. The other
   three are that manifest's three negative cases —
   `streamdb_onus/` (one flipped payload byte: one checksum mismatch, 23
   intact), `streamdb_caput/` (one flipped header byte: the torn-write
   fallback to the older commit, 20 documents readable and 4 absent) and
   `streamdb_truncus/` (300 bytes: no valid commit, exit 1, **no output**) —
   each reproducing the reference reader's recorded outcome exactly.
   `streamdb_corpus/expecta.py` builds the three `expected.out` files from the
   vendored corpus and asserts itself against `expectation.json` first; it is
   verification-only and this script never calls it. At 0.044 s the whole
   directory set is a rounding error beside `receptio_circuitus/`, which is
   why the reader reads one byte per syscall and has no buffer.

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

- **`tests/conformance/` entries that run.** See above: all 24 are written,
  10 run, 14 are `DEFERRED` on a named component. (This item was written
  when `exsc` did not exist and said what each of the 24 rows would need: a
  source fixture plus an expectation — a diagnostic code for most; byte-
  identical output under varied conditions for #16, which is what
  `tools/reproduce.sh` checks on the compiler itself; byte-identical output
  cross-host for #17.)

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

  **25 entries, five rule shapes.** Most are "rejects with exactly code
  EXS-Exxxx". Entries 16, 17 and 25 instead require byte-identical output
  across conditions and hosts — 17 compares the cross-compiled text, 25
  compares what the cross-compiled artifact does. Entry 23 is the
  certificate. Entry 15 is a runtime
  abort, not a compile failure. Entry 1 is a capability absence with no code
  assigned. A runner that assumes one shape will quietly mis-handle four.

- **Performance/benchmark tests.** None. CLAUDE.md: "Never report a
  benchmark you did not run" — the timings quoted in this file and the
  design documents are one-off measurements attributed to a commit, not
  fixtures, and nothing fails when they move.
- **aarch64/riscv64 fixtures.** The compiler is x86-64 assembly (spec
  §18); those hosts are full rewrites, not a fixture-file difference.
- **LSP/formatter tests.** Both deferred per spec §18.2.

## `tests/unit/` fixtures

Each `.asm` file may carry one directive comment, anywhere in the file,
read by `tests/run.sh`:

```
; TEST: run=yes|no  expect-exit=<N>  audit=pass|fail|skip  stdin=<PATH>
```

All four keys are optional; defaults are `run=yes expect-exit=0
audit=skip`, and stdin from `/dev/null`. `tests/run.sh` always assembles
every fixture; `run=no` skips *executing* the resulting binary (for a
fixture whose point is static detection, not runtime behavior — see
`socket_syscall.asm`); `audit=pass` or `audit=fail` runs
`tools/syscall-audit.sh` on the assembled binary and asserts its verdict;
`stdin=PATH` (repo-root-relative, the spelling `stdout=` uses in the two
run phases below) opens that file as the fixture's fd 0 —
`prelude_lege_octeto.asm` reads its four bytes through the descriptor
`Lector.ab_introitu(a)` derives from `ExsAmbitus.in`, which is the whole
point of feeding it rather than opening a path inside the fixture.

Every fixture runs under a **20-second limit**. A hang is a failure, not a
stuck suite: `timeout` exits 124, which matches no fixture's
`expect-exit=`. It went in with the reader, whose loop ends only when the
end-of-input sentinel arrives.

Current fixtures: see `UNIT_FIXTURE_FLOOR` in `tests/run.sh` (166 as of
9ede8bf; 124 at b9c0abc, and this sentence said 46 for a long time),
covering the macro dialect, every `rt/` module, the Unicode consumers,
`diag/`, the lexer, the CST, the AST, the driver, the backend and its
verifier, the prelude, the checker and the lowering. The four below are
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
| `stdin=PATH` | the program's stdin is that file, repo-root-relative exactly as `stdout=` is. Absent: `/dev/null`, which is what every fixture written before a reader existed assumed |

Exactly one of `expect-exit=` / `abort=` is required for anything that
runs. **An unknown key fails the fixture** — the unit directive silently
ignores one, so a typo there falls back to the default and passes.
Binaries run with an empty environment and a 20-second limit.

`lector/` is `stdin=F stdout=F` with the same `F`: a `cat` program proved
against the one file rather than against a copy of it, so no second file
can drift from the input. `F` is `tests/data/lector_intra.bin`, 41 bytes —
not a power of two, so a reader that lost a tail shows up — holding `0x00`,
`0xff`, a lone `0x80` and a CR, which is why `.gitattributes` marks
`tests/data/*.bin` `-text`. Its companion `lector_numerus/` reads the same
file, writes nothing, and returns the COUNT as its exit status
(`expect-exit=41`).

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
rather than keep a second copy. `sources=` names the whole unit, so a
directory that uses it may still hold its own source, **provided `sources=`
lists it**: `acies/` compiles `examples/hydramodem/`'s modulator (a library
— it has no `initium` and cannot be a unit by itself) together with its own
`acies.exsc`. An own `*.exsc` that `sources=` does not list is refused, which
is the ambiguity the rule was written for. A file `expected.out` is the stdout
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

### The C backend's keys

Three keys exist so the differential phase — which compiles the same
fixture through `--emitte c` and holds the result to the same directive —
can *say* where the two backends part company, instead of a name being
special-cased inside `tests/run.sh`.

`c-emit-exit=N` (IR) and `c-exsc-exit=N` (programs) give the C side's own
expected status where it legitimately differs from the reference's
`emit-exit=`/`exsc-exit=`. The default is parity, so
`reject_emit_straddle.ir` needs neither. No fixture needs either today.

`c-differentia=REASON` (programs only) declares a directory **ineligible**
for the differential phase. A directory without the key is eligible and its
four builds — gcc and clang, `-O0` and `-O2`, under
`-fsanitize=undefined -fno-sanitize-recover=all` — must agree with the
reference on stdout bytes, exit status, and trap-or-not with the abort
kind. The reason set is closed, and an unrecognised value fails the
fixture:

| reason | means |
|---|---|
| `nightly-sweep` | the `receptio_vec_*` impaired-vector sweep (`docs/design/c-backend.md` D6). All seventy name the identical `sources=` as `receptio_exemplum`, so they would add 280 runs of four binaries the phase already builds, not one more lowering |
| `prelude-beyond-shim` | the unit imports an `exsrt_*` routine `tests/c/exsrt_shim.c` does not define, so it would not link. Unused today |
| `emitter-refusal` | `exsc --emitte c` refuses the module by name (one of D4's 23 refusals). Unused today |

There is deliberately **no glob** in `tests/run.sh` that passes over a
directory: a name silently absent from a phase is the false green
`UNIT_FIXTURE_FLOOR`'s header lists four times over, so exclusion is data in
the fixture and the count of exclusions is printed on every run.

## Adding a case

**A new `tests/unit/` fixture:** drop a `.asm` file in this directory with
a `; TEST:` line describing what should happen, and `tests/run.sh` picks
it up automatically — nothing else to wire up. Prefer one fixture per
thing being proven, and say in a comment what it proves; see the existing
three for the pattern.

**A new `tests/ir/` or `tests/programs/` case:** add the `.ir` file or the
directory with its `TEST` file, then raise `IR_FIXTURE_FLOOR` or
`PROGRAM_FIXTURE_FLOOR` in `tests/run.sh` in the same commit, and show it is
not vacuous (CONTRIBUTING.md: break the thing, watch it fail, restore). A
new program directory is also a differential case unless it declares
`c-differentia=`, so raise `DIFFERENTIAL_PROGRAM_FLOOR` and
`DIFFERENTIAL_PROGRAM_BUILD_FLOOR` (by 1 and by 4) in the same commit too —
or, if it declares one, say which reason and why.

**A new `tests/conformance/` entry:** a spec amendment to §14 first
(entries are cited by number; append, never renumber), then the source
fixture under `tests/conformance/` with its `// TEST: entry=N shape=…
expect-code=… status=run` line, and raise `fixture_floor` in
`run_conformance_tests()` in the same commit. Un-deferring an existing
entry is turning `status=deferred needs=…` into `status=run` once the
named component exists, and seeing it emit exactly its code.

## A limitation that no longer holds

This section said `make test` and `make reproduce` could not run because
`compiler/x86_64/exsc.asm` did not exist. It exists; both targets run (`make
reproduce` was itself broken from the day exsc.asm gained an include until
528fe7d, which is the kind of thing this file is for). Kept as a heading so a
reader who remembers the limitation finds its retraction rather than silence.
