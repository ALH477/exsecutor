# Exsecutor

A systems language whose thesis is one line:

> **Locale and target are capabilities, never ambient state.**

No operation may implicitly read the host locale, encoding, byte order, pointer
width, clock, or environment. If a computation depends on a human language or on
a machine, that dependency appears in its signature or the program does not
compile.

The full design is `docs/spec/exsecutor-spec-v0.4.md`. Read it before writing
code; it is the source of truth, and code that contradicts it is a bug in one of
the two.

## Status

**Stage 1, roughly half.** `exsc` builds and runs: it lexes a file, enforces
§8.1's source policy, and reports diagnostics with exact codes and spans. It
does not parse — the CST and typed AST are being written now — so nothing
compiles to an artifact yet.

| | |
|---|---|
| Source extension | `.exsc` |
| Interface file | `ego.exsc` |
| Compiler | `exsc` |
| Diagnostics | `EXS-E####` (§13) |

## Building

The build closure is `fasmg` plus a vendored macro package, and nothing else.

```sh
nix develop           # fasmg on PATH, INCLUDE preset
make smoke            # assemble a fixture -- checks the toolchain itself
make test             # unit fixtures (conformance suite is still empty)
make audit            # prove the binary makes no network syscall
make reproduce        # byte-identical output under divergent conditions
nix flake check       # test + audit + toolchain + vendor integrity,
                      # hermetically. Not reproduce -- see below.
```

Outside the devShell, plain `make` works too, given `fasmg` on `PATH`.

`nix flake check` runs five sandboxed checks: `smoke`, `test`, `audit`,
`buildExsecutorPackage-smoke`, and `vendor-integrity`. It does **not** run
`make reproduce` — that builds the same source repeatedly under deliberately
divergent ambient conditions, which is the one thing a hermetic sandbox
cannot vary. Run it from the devShell.

The compiler is written in **x86-64 assembly**, freestanding — no libc, no
dynamic linking, direct syscalls only. That choice and its costs are recorded in
§18 of the spec and in `docs/decisions/`. The short version: it makes §9.3's
purity contract a checkable property of the artifact rather than a discipline,
and it costs the aarch64 host and the LSP for now.

`make audit` is the part worth understanding. §9.3 says "no network access,
ever, at any phase"; `tools/syscall-audit.sh` disassembles the binary, resolves
every `syscall` site's number, and fails on anything outside a closed allowlist
— loudly on the socket family. A negative fixture in `tests/unit/` proves the
audit catches what it claims to:

```
0x400089    41    socket    FAIL  <<< SOCKET-FAMILY SYSCALL -- HARD FAILURE >>>
```

### fasmg does not know x86

`fasmg` is architecture-neutral: the binary knows no machine instructions, and
`mov eax, 60` alone is `Error: illegal instruction`. The x86-64 instruction set
and the ELF64 writer are *macro packages* — ordinary fasmg source — and the
nixpkgs derivation ships only `bin/fasmg`. They are vendored at
`vendor/fasmg-x86/`, which also removes the last network dependency from the
build: upstream publishes to a rolling URL whose bytes have already drifted from
the hash nixpkgs pins. See `vendor/fasmg-x86/PROVENANCE.md`.

## Layout

```
docs/spec/         the specification -- source of truth
docs/decisions/    ADRs
compiler/x86_64/   the compiler (macros -> rt -> lexer/cst/ast/diag -> driver)
compiler/shared/   generated Unicode tables, architecture-neutral
tools/ucd-gen/     table generator; verification-only, OFF the build path
tests/             unit tests and the §14 conformance suite
prototypes/        Python design probes -- never shipped, never on the closure
vendor/fasmg-x86/  third-party: fasmg's x86-64 macro package, verbatim
.claude/agents/    per-directory agent scopes for the compiler work
```

## What is not here

Two artifacts the spec cites do not exist in this tree and are marked
`[UNREPRODUCED]` in it:

- the 468-line capability-row checker behind §4.2 — being rebuilt from the spec,
  which is a re-derivation, not a restoration
- the Stage 0 benchmark sources behind §6.2 and §9.2 — figures are carried
  forward unverified

## Current state, precisely

What is verified working, today, on this tree:

| tree | lines | what |
|---|---|---|
| `macros/` | 1,086 | the fasmg dialect — `proc`, `flow_*`, `struct`, `rassert`. Frozen. |
| `rt/` | 1,743 | syscalls, arena, vec, map, intern, str, span, sort |
| `shared/unicode/` | 1,117 | NFC, XID, script — over generated UCD 17.0.0 tables |
| `lexer/` | 2,549 | §8.1 source policy, §8.2 identifiers, tokens with spans |
| `diag/` | 2,399 | escaping, rendering, `--diagnostica json`, fix payloads |
| `driver/` + `exsc.asm` | 2,116 | the CLI. `make all` → `build/exsc`, 195,783 bytes |

- **141 checks pass, 0 fail** — 46 unit fixtures plus 23 conformance entries.
- **5 of 23 conformance entries actually run** against a real `exsc` diagnostic.
  The other 18 report `DEFERRED` with what they wait on and are **never counted
  as passing**.
- `make audit` audits **the real compiler**, not fixtures: nine syscalls, all
  from `rt/sys.inc`, exactly the allowlist, extracted by disassembly. §9.3's
  "no network access, ever, at any phase" is a property of the artifact.
- `nix flake check` — 6 hermetic checks green, and the logs say what they
  covered rather than leaving it inferred.
- NFC is verified against all **100,170** `NormalizationTest.txt` assertions,
  driven through the shipping assembly rather than through Python.
- `tools/reproduce.sh` — byte-identical output across divergent working
  directory, `TZ`, locale, `SOURCE_DATE_EPOCH`, umask, and hostname.

**What does not exist: a parser, a type checker, and a backend.** `exsc`
therefore exits 4 on a well-formed program — "the front end accepts this source,
but code generation is not implemented" — and `tools/publish-gate.sh` fails at
step 3 and says so. Nothing here passes vacuously; that has been tested by
breaking it.

## License

**GPL-3.0-or-later, with two exceptions.**

The compiler is free software and stays free. What you write in Exsecutor is
yours.

- **Exception A — compiler output.** Compiling with `exsc` places no licensing
  obligation on your program, on the code `exsc` emits from it, or on the
  resulting binaries. Automatic. This is the FAUST / GCC / Bison arrangement:
  a compiler's freedom should not be contagious through its output.
- **Exception B — linking.** The GNU Classpath exception, as OpenJDK uses it,
  for runtime files linked into compiled programs.

Files: `LICENSE` (GNU GPL v3, verbatim) and `LICENSE.EXCEPTION` (both grants,
their limits, and the precedents they follow).

### Which files are excepted

Exception B attaches to **individually designated files** — a file is covered
if and only if its own header says so, in OpenJDK's words. Nothing is excepted
by category, by directory, or by resemblance.

That matters because one word names two different things here:

| | | |
|---|---|---|
| `compiler/x86_64/rt/` | the **compiler's own** internals — arena, map, interner, syscall wrappers, linked into `exsc` and never into your program | plain GPL, **not** designated |
| the **target runtime** | §6's refcount support and the `norma.*` modules linked into a *compiled program* | designated, per file |

No target runtime exists yet — there is no backend, so nothing is emitted into
user programs, and **no file in this repository currently carries a designation
line.** The terms are written forward so the position is settled before that
code lands rather than renegotiated once contributors hold copyright in it.

`vendor/` is third-party and keeps its own license — `vendor/fasmg-x86/` is
BSD-3-Clause (Tomasz Grysztar), which is GPL-compatible. Do not relicense it or
strip its notices.
