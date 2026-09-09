# Exsecutor — working invariants

Binding on every agent and every change in this repo.

## The spec is the source of truth

`docs/spec/exsecutor-spec-v0.4.md`. Section references below are to it.

Code that contradicts the spec is a bug — but say **which** of the two is wrong.
The spec has been wrong before: its own evidence note records that three earlier
versions asserted things measurement then contradicted. If the code is right and
the spec is stale, amend the spec in the same change and say so.

## Evidence discipline

> Prose designs are hypotheses until code runs.

- A claim backed by nothing gets `[OPEN]` or `[UNTESTED]`. Figures that cannot
  currently be re-measured get `[UNREPRODUCED]`.
- Never present a re-derivation as a restoration.
- Never report a benchmark you did not run, or a test you did not see pass.

## Error codes are permanent

§8.3: codes are permanent, text is not; tools match codes, never English prose.

- **Never invent a code.** Never renumber one.
- §13's registry is the only source. `compiler/x86_64/diag/codes.inc` is
  generated from it and must stay in sync.
- A new code requires a spec amendment to §13 first.

## The compiler is freestanding

- **No libc. No dynamic linking.** Direct syscalls only, via `rt/sys.inc`.
- The syscall allowlist is closed: `read(0) write(1) close(3) fstat(5) lseek(8)
  mmap(9) munmap(11) openat(257) exit_group(231)`. Adding one is a reviewed
  change with a stated reason.
- **No socket-family syscall, ever** (§9.3). `make audit` fails the build if one
  appears. This is what makes the purity contract checkable rather than
  promised; do not weaken it.
- No env reads outside the explicit `--env KEY=VALUE` allowlist. No `$HOME`, no
  dotfile discovery, no ambient clock — timestamps come from `--epoch` (§9.3).

## Determinism is not optional

§9.3 requires byte-identical output for identical inputs across directories,
times, locales, and hostnames.

- Maps iterate in **insertion order**. `rt/map.inc` is built that way on purpose;
  do not substitute a faster map with address- or hash-dependent iteration.
- No ordering may depend on a pointer value.
- Symbol emission, section ordering, and hash iteration are explicit.

## The compiler holds itself to §8.1

All source in this repo: UTF-8, no BOM, LF endings, NFC. The tool that rejects
CRLF should not ship with CRLF in it.

## The macro dialect is frozen after wave 1

`compiler/x86_64/macros/` is what everything above it is written in. Once the
first module depends on it, changes go through review — a macro change is a
whole-tree change. Conventions are in `docs/asm-conventions.md` and are binding.

## Scope

Each agent owns its directory. Do not edit another agent's tree; report the
needed change instead. `docs/spec/` is edited only with an explicit reason
recorded in the commit message.
