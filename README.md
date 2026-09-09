# Exsecutor

A systems language whose thesis is one line:

> **Locale and target are capabilities, never ambient state.**

No operation may implicitly read the host locale, encoding, byte order, pointer
width, clock, or environment. If a computation depends on a human language or on
a machine, that dependency appears in its signature or the program does not
compile.

The full design is `docs/spec/exsecutor-spec-v0.3.md`. Read it before writing
code; it is the source of truth, and code that contradicts it is a bug in one of
the two.

## Status

**Stage 0 → Stage 1.** Nothing compiles yet. The specification is complete; the
compiler is being built.

| | |
|---|---|
| Source extension | `.xsc` |
| Interface file | `ego.xsc` |
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

- the toolchain — `fasmg` + the vendored macro package assembles a freestanding
  static ELF64 with no dynamic section and no interpreter
- `tests/run.sh` — 10 checks over 4 fixtures, including a negative case proving
  the syscall audit rejects a socket call
- `tools/reproduce.sh` — byte-identical output across divergent working
  directory, `TZ`, locale, `SOURCE_DATE_EPOCH`, umask, and hostname
- `nix flake check` — 3 hermetic checks green

What does not exist yet: **the compiler**. `compiler/x86_64/` is a set of empty
directories. `make audit` therefore reports that there is nothing to audit, and
the conformance suite holds 0 of §14's 17 entries. Both say so plainly rather
than passing vacuously.
