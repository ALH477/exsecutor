# Contributing to Exsecutor

This file says how contributions work in this repository specifically. The
general shape -- fork, branch, pull request -- is the ordinary one. What is
particular is below, and all of it is enforced by a command rather than by
asking; `CLAUDE.md` at the root is the binding statement and this file is the
explanation.

## The spec is the source of truth

`docs/spec/exsecutor-spec-v0.4.md`. Code that contradicts it is a bug -- in one
of the two. If you find the spec is wrong, amend it in the same change, and
say so and why in the commit message; `docs/spec/` is never edited without a
reason recorded there. The spec has been wrong before and says so in its own
evidence note, so "the spec is stale" is a legitimate finding. What is not
legitimate is code that quietly disagrees.

## Evidence

> Prose designs are hypotheses until code runs.

- A claim backed by nothing gets `[OPEN]` or `[UNTESTED]`. A figure that
  cannot currently be re-measured gets `[UNREPRODUCED]`.
- Never report a benchmark you did not run, or a test you did not see pass.
- Never present a re-derivation as a restoration.
- Every commit message says what was run and what it showed. Look at
  `git log` for the shape: the numbers, the commands, and what did not
  reproduce, in the message, not in a later note.

## Error codes

Codes are permanent, text is not, and tools match codes (§8.3). **Never invent
a code and never renumber one.** §13 of the spec is the only registry;
`compiler/x86_64/diag/codes.inc` is generated from it by `tools/gen-codes.py`,
and `tools/spec-check.sh` fails on drift. A change that needs a new code is a
spec amendment to §13 first, then a regeneration -- never a hand edit of
`codes.inc`.

## The compiler is freestanding, and the allowlist is closed

No libc, no dynamic linking; direct syscalls only, through `compiler/x86_64/rt/sys.inc`. The
allowlist is `read(0) write(1) close(3) fstat(5) lseek(8) mmap(9) munmap(11)
openat(257) exit_group(231)`, and it is closed: adding one is a reviewed change
with a stated reason, and a socket-family syscall is never added (§9.3).
`make audit` disassembles `build/exsc` and fails the build on anything outside
the list. It is what makes the purity contract checkable rather than promised;
do not weaken it, and do not route a syscall number through a second register
to get past it -- the audit reports that as `INDETERMINATE` and fails too.

No environment reads outside `--env KEY=VALUE`. No `$HOME`, no dotfiles, no
clock (`--epoch`). Maps iterate in insertion order (`compiler/x86_64/rt/map.inc`); no ordering
may depend on a pointer value; output is byte-identical across directories,
times, locales and hostnames, and `make reproduce` checks it.

## The macro dialect is frozen

`compiler/x86_64/macros/` is what everything above it is written in. A macro
change is a whole-tree change and goes through review. `docs/asm-conventions.md`
is binding on every line of assembly: calling convention, register discipline,
the error protocol, how a module is added. Read it before writing any.

## Scopes are exclusive

Each agent -- or contributor -- owns a directory, listed in
`.claude/agents/README.md`. Do not edit another tree; report the needed change
to its owner instead. This is not bureaucracy: it is how two concurrent
changes to the same file stopped happening.

## Fixtures

Every test fixture must be proven non-vacuous by mutation: break the thing it
claims to check and watch it fail, then restore it. A fixture that passes for
the wrong reason has happened here more than once (a BOM fixture that
contained no BOM; a determinism diff between two empty directories), which is
why `tests/run.sh` carries floors on how many fixtures it must discover.

- `tests/unit/*.asm` -- one fixture per thing proven, with a `; TEST:` line
  and a comment saying what it proves. `tests/run.sh` discovers it
  automatically.
- `UNIT_FIXTURE_FLOOR` in `tests/run.sh` is raised deliberately when fixtures
  are added, in the same commit. Adding fixtures never trips it; a discovery
  mechanism silently finding nothing does.
- `tests/conformance/` mirrors §14 entry for entry. A new entry is a spec
  amendment to §14 first, appended and never interleaved, because entries are
  cited by number.
- A fixture whose point is its bytes (a CR, a BOM) lives where `.gitattributes`
  marks the directory `-text`, and must be written with a tool that actually
  emits those bytes.

## Source policy

The repository holds itself to §8.1: UTF-8, no BOM, LF line endings, NFC. The
tool that rejects CRLF should not ship with CRLF in it. The only exceptions
are the two byte-exact directories `.gitattributes` names, `vendor/` and
`tests/conformance/`, for the reasons written there.

## Running the whole suite

```sh
nix develop
make all
make audit
make reproduce
tests/run.sh
tools/spec-check.sh
tools/syscall-audit.sh --self-test
nix flake check
```

That is the same list `.github/workflows/ci.yml` runs; `tools/publish-gate.sh`
runs most of it and adds the hello world itself. A change is ready when all of
it is green and the commit message says so with the numbers.

## Proposing a root coinage

The lexicon (§3) grows only through §3.9. A proposal contains the concept and
an exhaustion argument showing the existing table cannot express it within the
two-affix ceiling; the candidates considered and which rung of §3.9.2's ladder
each sits on (attested classical, Neo-Latin scientific, Greek combining form,
descriptive compound, marked loan -- a bare English word is never admissible);
the chosen root with its stems; a collision check against registered roots,
reserved keywords and module namespaces; and at least three derivations. It is
reviewed by someone other than its proposer. The worked example is
`prototypes/lexicon/coinage-0001-gutenbergius.md`, which is instructive partly
because §3.9 refused the obvious version of it: `imprimere` is attested Latin
for *print*, so Gutenberg enters only as an eponym. Put a proposal under
`prototypes/lexicon/` with the same shape and the next number.

## Commit trailers

A substantial part of this tree was written by Claude (Anthropic) working
under the author's direction, and commits made through that tooling carry a
`Co-Authored-By: Claude ... <noreply@anthropic.com>` trailer. That is normal
here and is attribution, not a review signal; the evidence discipline above
applies to those commits exactly as to any other.

## Code of conduct

There is no code of conduct.
