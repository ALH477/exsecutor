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

## What this is

**A systems language.** Reference-counted, no borrow checker (§6); generics by
dictionary passing (§7); byte order and layout in the type at wire boundaries
(§5.2); floating-point rounding and contraction declared per module rather than
inherited from a compiler flag (§5.4). Authority is object-capability style
(§4): a function that touches the filesystem, the clock, the network, the
process environment, or human-language text says so in a `poscit` clause, and
the program's whole authority enters once, as the `Mundus` parameter of
`initium` (§4.7). Everything else is derived from that value, explicitly. The
same rule applied to the compiler makes the build a pure function of its
inputs, which is what "Nix-native" means here (§9).

**A Latin lexicon, deliberately.** Keywords are Latin (`publica functio`,
`redde`, `firma`, `poscit`), and every public name must decompose into a
prefix, a root, and a suffix drawn from a versioned morpheme table, where the
suffix carries a type contract (§3). That is a checked artifact, not a style.
It is also an identity commitment rather than a hypothesis: ADR 0005 records
that the lexicon stays whatever the planned derivation test measures, and the
spec's own §17 names naming as the project's largest adoption risk. You are
meant to know that before you decide whether this is for you.

**A freestanding x86-64 assembly compiler.** `exsc` is written in x86-64
assembly, assembled by fasmg, with no libc and no dynamic linking; the only way
it touches the operating system is a closed allowlist of nine syscalls in
`compiler/x86_64/rt/sys.inc`. The reason is §18.1: §9.3's purity contract --
no environment reads, no `setlocale`, no `$HOME`, no clock, no network, ever --
stops being a discipline that reviewers enforce and becomes a property of the
artifact. The code to read a dotfile is not linked in, and `tools/syscall-audit.sh`
disassembles the binary and diffs every `syscall` site's number against the
allowlist. The costs are recorded in §18.2 and ADR 0002: the compiler runs only
on `x86_64-linux`, other hosts are rewrites of the architecture-specific tree,
and the LSP is deferred.

## The hello world

`examples/saluta.exsc` is the canonical program, and it cannot print:

```exsecutor
publica functio saluta() -> textus {
    redde "Ave, mundus.
...
Hodie incipimus.";
}
```

There is no `poscit` clause because the empty row is the honest one. Printing
is I/O, I/O is authority, and authority is declared -- so this function returns
the greeting and is, by its own signature, incapable of allocating, reading a
clock, touching a file, or reaching the network. Most languages open with a
program that performs I/O; this one opens with a program that provably cannot.
(The literal spans several lines with no indentation stripping, which is what
settled §8.4's rule: stripping would make the value depend on the source's
leading whitespace, exactly the ambient-state class §1 exists to remove.)

`examples/imprime.exsc` is what printing looks like when authority has to be
declared -- `imprime_gutenbergio(s: Scriptor, t: textus) -> mensura poscit
sicut s`, which requires exactly what its writer requires and nothing more
(§4.2: rows travel with a value's type). `examples/initium.exsc` is the entry
point: `publica functio initium(m: Mundus) -> u8`, deriving the standard
streams from `m` as `ambitus` (§4.6) and handing `saluta()` to
`imprime_gutenbergio`. `examples/saluta.expected` is the 101 bytes the program
must write, with no trailing newline. `examples/README.md` explains each file,
including the two spec defects writing them exposed and the reason
`Gutenbergius` is an eponym and not a root.

`tools/publish-gate.sh` is the condition for this repository going public,
written as a command: `exsc` compiles the three files together, `fasmg`
assembles the emitted text, the result runs, writes exactly those bytes,
reproducibly, with its syscall surface inside `{Mundus, ambitus}` and every
other check green. Whether that is true today is in the status block below.

## Building and checking

The build closure is `fasmg` plus the vendored x86-64 macro package under
`vendor/fasmg-x86/`, and nothing else. fasmg is architecture-neutral -- the
binary knows no machine instructions -- so the instruction set and the ELF64
writer are ordinary fasmg source, vendored verbatim with recorded hashes
(`vendor/fasmg-x86/PROVENANCE.md`).

```sh
nix develop                          # fasmg, binutils, python3 on PATH; INCLUDE set
make all                             # assembles compiler/x86_64/exsc.asm -> build/exsc
make audit                           # tools/syscall-audit.sh build/exsc
make reproduce                       # tools/reproduce.sh
tests/run.sh                         # unit fixtures + the §14 conformance suite (= make test)
tools/spec-check.sh                  # spec integrity; needs no toolchain
tools/syscall-audit.sh --self-test   # proves the audit rejects what it claims to
nix flake check                      # the hermetic subset, in the Nix sandbox
tools/publish-gate.sh                # the publish condition; exits 1 until met
```

Outside the devShell, plain `make` works given `fasmg` on `PATH`. Direnv is
optional (`.envrc` is `use flake`).

What the two proofs prove:

- **`make audit`** audits the real compiler, not a fixture. It checks the
  binary has no `PT_INTERP` and no dynamic section, sweeps the executable
  segment for every `syscall` instruction, resolves each one's `rax`, and fails
  on any number outside the allowlist, on any number it cannot resolve, and --
  loudly -- on anything in the socket family. `--self-test` assembles
  `tests/unit/clean_syscalls.asm` and `socket_syscall.asm` and asserts the
  audit accepts the first and rejects the second, so the audit is proven to
  catch what it claims before it is trusted on `exsc`; `tests/run.sh` adds
  `indeterminate_syscall.asm`, a syscall number routed through a second
  register, which must fail too. With `--potestates ATOM,...` it audits a
  *compiled program* against the syscalls its declared capability atoms admit
  (§10.3), which is what the publish gate runs on the hello world.
- **`make reproduce`** builds `exsc` twice under deliberately divergent
  ambient conditions -- working directory, `TZ`, `LC_ALL`/`LANG`,
  `SOURCE_DATE_EPOCH`, umask, hostname -- and `cmp`s the outputs byte for byte
  (§9.3). `nix flake check` cannot run this: varying ambient conditions is the
  one thing a hermetic sandbox does not do, so `reproduce` runs from the
  devShell and in CI.

`nix flake check` runs six sandboxed derivations -- `smoke`, `test`, `audit`,
`buildExsecutorPackage-smoke`, `vendor-integrity`, `wire-vendor-integrity` --
over the git-tracked tree only. `tests/run.sh`, which the `test` check runs,
carries a floor on the number of fixtures it must discover, because this
project has produced green checks that saw nothing four times, and the floor is
what stopped a fifth.

To see the front end work on a file:

```sh
build/exsc aedifica --hospes x86_64-linux --emitte ast examples/initium.exsc
build/exsc aedifica --hospes x86_64-linux --diagnostica json tests/conformance/entry20_crlf.exsc
```

`--hospes` is required; there is no default-to-build-platform (§9.5). `exsc`
with no arguments prints its usage, its options, and its exit-status table.

## The tree

```
compiler/           the compiler: x86_64/ is the machine-specific body, shared/ is not
docs/spec/          the specification -- source of truth
docs/decisions/     ADRs 0001-0012, immutable once written; superseded, never edited
docs/design/        design documents: hypotheses with a status line, built against, and amended by what building found
docs/asm-conventions.md   the binding rules for every line of assembly here
examples/           the hello world, its golden output, and their README
prototypes/         Python design probes -- never shipped, never on the build closure (§18)
tests/              unit fixtures, the §14 conformance suite, the Stage 1 diagnostics corpus
tools/              the audits, generators, and the publish gate; nothing here is on the build path except as a check
vendor/             third-party, byte-exact, own licences: fasmg-x86/ (BSD-3-Clause), hydramesh-wire/ (LGPL-3.0-only)
.claude/agents/     how the work is organised: one agent per directory, scopes exclusive
CLAUDE.md           the working invariants, binding on every change
```

Inside `compiler/x86_64/`, in dependency order, each named with the document
it was built against:

```
macros/         the fasmg dialect (proc, flow_*, struct, rassert) -- docs/asm-conventions.md; frozen
rt/             syscalls, arena, vec, insertion-ordered map, interner, str, span, sort -- docs/asm-conventions.md, spec §6.3, §9.3
lexer/          §8.1 source policy, §8.2 identifiers, §8.4 tokens; keywords.inc generated from §8.4
diag/           diagnostics: rendering, escaping, --diagnostica json, notes, fixes -- spec §8.3; codes.inc generated from §13
cst/            lossless red-green concrete syntax tree and the recursive-descent parser -- docs/design/phrase-grammar.md, spec §8.6
ast/            the typed AST, with its Stage 2 slots present and empty -- docs/design/typed-ast.md
driver/         the command line, exsc.asm's entry -- spec §9.3, §9.5, §12, §18.2
backend_fasmg/  the SSA IR, its parser, printer, verifier, and the naive emitter -- docs/design/ssa-ir.md, spec §9.2
prelude/        the runtime every compiled program carries -- docs/design/runtime.md
checker/        Stage 2 name resolution and capability rows -- docs/design/checker.md (in progress; see status)
exsc.asm        the aggregation point: includes the chain above
```

`compiler/shared/unicode/` holds the NFC, XID and script tables generated from
UCD 17.0.0 by `tools/ucd-gen/` (ADR 0004), plus the assembly that reads them;
the tables are the architecture-neutral part.

## How the project works

**The spec is the source of truth**, and it has been wrong before: its own
evidence note records that three earlier versions asserted things measurement
then contradicted. So a disagreement between code and spec is a bug in one of
the two, and every fix says which. `docs/spec/` is amended only with the reason
recorded in the commit message.

**Design documents are hypotheses.** Each file in `docs/design/` opens with a
status line -- `[OPEN]`, "design only", "partly built", "built for Stage 1" --
and when the code built against it disagrees with it, the disagreement is
recorded in place as a finding rather than absorbed silently. Several of those
status lines say, in so many words, that they were stale for a few commits
before being corrected.

**Evidence markers.** A claim backed by nothing is `[OPEN]` or `[UNTESTED]`. A
figure that cannot currently be re-measured is `[UNREPRODUCED]` -- the spec
carries several, because the Stage 0 benchmark sources and the original
capability-row checker are not in this tree. `tools/spec-check.sh` counts the
markers; nothing is reported as passing that was not seen to pass.

**Error codes are permanent** (§8.3). Tools match `EXS-E####` codes, never
English text. §13 is the only registry; `compiler/x86_64/diag/codes.inc` is
generated from it by `tools/gen-codes.py`, and `tools/spec-check.sh` fails on
drift. A new code is a spec amendment first.

**Conformance is §14.** Twenty-four entries, each a fixture under
`tests/conformance/`. The runner distinguishes five rule shapes (reject with
exactly this code; byte-identical output; external certificate; runtime abort;
capability absence) and reports an entry it cannot yet run as `DEFERRED`,
naming what it waits on; deferred entries are never counted as passing.

**Kill criteria are evaluated.** §16 attaches one to every stage. Stage 1's --
"diagnostics quality is not retrofittable; bad here, stop and fix" -- was
evaluated against 34 broken programs in `docs/design/diagnostics-review.md`,
returned a conditional fail (37 of 46 diagnostics were the identical sentence
`unexpected token`), was fixed (§8.3 gained a note and a related span; machine
fixes were split from suggestions), and was re-evaluated on a checked-in corpus
(`tests/diagnostics/`) before Stage 2 opened. The original verdict is kept as
written above the re-evaluation.

**Scopes are exclusive.** `.claude/agents/README.md` lists who owns which
directory. An agent needing a change outside its tree reports it rather than
making it. Every fixture is proven non-vacuous by mutation, every commit says
what was run, and the repository holds itself to the source policy it enforces
(§8.1: UTF-8, no BOM, LF, NFC -- `.gitattributes` says which two directories
are byte-exact exceptions and why).

<!-- STATUS BLOCK: the only place this file makes claims about what works.
     Refresh at publish; everything outside this block is written to stay true. -->

## Status as of `b9c0abc` (2026-09-10)

Every figure here was produced by running the named command at this commit, in
the `nix develop` shell, on `x86_64-linux`.

**The publish gate is not met.** `tools/publish-gate.sh` exits 1 at step 3:
`exsc aedifica` at this commit accepts exactly one `SOURCE` and exits 2 on the
second, while spec §12 (amended after the driver was written) lets `SOURCE`
repeat so the three hello-world files form one compilation unit. The driver is
behind the spec on that point. Given one file and `-o`, `exsc` exits 4 -- "the
front end accepts this source, but code generation is not implemented" -- which
is the honest state: nothing compiles to an artifact yet.

**What exists and runs:**

- `make all` -> `build/exsc`, 249,874 bytes, freestanding. The front end is
  complete for Stage 1: all three `examples/*.exsc` lex, parse, and build a
  typed AST (exit 0 without `-o`; `--emitte tokens|cst|ast` dumps each stage).
- **Stage 1's kill criterion was evaluated, failed, fixed, and re-evaluated**;
  it no longer fires (`docs/design/diagnostics-review.md`, final section).
  Stage 2 is open.
- **Stage 2, the checker**, is being written under `compiler/x86_64/checker/`
  against `docs/design/checker.md`. This commit holds its first four passes
  (name resolution, the row fixpoint, packed layout) under nine `chk_*` unit
  fixtures. It is not yet included from `exsc.asm`, so `build/exsc` does not
  run it and no `.exsc` file is type- or capability-checked yet.
- **Stage 3 pieces exist ahead of their input:** `backend_fasmg/` parses,
  prints, verifies (nine rules, each with a rejected and an accepted twin) and
  naively emits the SSA IR, and `prelude/` is the runtime a compiled program
  carries. Both pass their unit fixtures (`bfa_*`, `prelude_*`), and the
  emitter's output has been assembled by real fasmg and run -- against
  hand-written IR only. No lowering from the AST exists
  (`docs/design/lowering.md` is design only), so nothing from an `.exsc` file
  reaches either of them yet.

**What the checks said:**

| command | result |
|---|---|
| `tests/run.sh` | **356 pass, 0 fail.** 124 unit fixtures discovered, floor 124. Conformance: **6 of 24 entries ran and passed** -- 3, 5, 18, 19, 20, 22, each rejected with exactly its §14 code -- and **18 are `DEFERRED`**, never counted as passing. |
| `make audit` | PASS. Nine syscall sites, all in `rt/sys.inc`: `read write close fstat lseek mmap munmap openat exit_group`. Exactly the allowlist. |
| `make reproduce` | PASS. Byte-identical 249,874 bytes across divergent cwd, `TZ`, locale, `SOURCE_DATE_EPOCH`, umask, hostname. |
| `tools/spec-check.sh` | PASS. 49 codes in §13, 49 in `codes.inc`, in sync; keywords in sync; every `§N` citation resolves; no stray evidence markers. |
| `tools/syscall-audit.sh --self-test` | PASS. The socket fixture is rejected; the `--potestates` union admits what it should and `Mundus` alone rejects `read` and `socket`. |
| `nix flake check` | all six checks pass: `smoke`, `test`, `audit`, `buildExsecutorPackage-smoke`, `vendor-integrity`, `wire-vendor-integrity`. |

**What is missing before the gate can pass:** the driver accepting repeated
`SOURCE` (§12); the Stage 2 checker; AST-to-SSA lowering; and the program the
backend emits from `initium` running and writing `examples/saluta.expected`.

**Two artifacts the spec cites are not in this tree** and are marked
`[UNREPRODUCED]` in it: the 468-line capability-row checker behind §4.2 (what
is in `prototypes/capcheck/` is a re-derivation from the spec, not a
restoration) and the Stage 0 benchmark sources behind §6.2 and §9.2.

<!-- END STATUS BLOCK -->

## Licence

Exsecutor is GPL-3.0-or-later (`LICENSE`), with two additional permissions
under GPLv3 section 7 in `LICENSE.EXCEPTION`: Exception A lets you distribute
what `exsc` produces from your input -- emitted source, object code,
executables, `ego` files, diagnostics -- under terms of your choosing, and
Exception B is the GNU Classpath linking exception, attaching only to a file
whose own header carries the designation line, which no file in this repository
currently does. `vendor/` is third-party and keeps its own licences --
`vendor/fasmg-x86/` is BSD-3-Clause and `vendor/hydramesh-wire/` is
LGPL-3.0-only -- and neither exception applies to it. `LICENSE.EXCEPTION`
states that it has not been reviewed by a lawyer, and so does this sentence.

## Where to look next

- `docs/spec/exsecutor-spec-v0.4.md` §1, §2, §4, then §16 and §18.
- `examples/README.md`, then the three example files.
- `docs/decisions/0002-host-language-x86-64-asm.md` and `0005-lexicon-is-bespoke.md`
  for the two choices most likely to decide whether you stay.
- `docs/design/diagnostics-review.md` for what evaluating a kill criterion
  looks like here.
- `tests/README.md` and `tests/run.sh` for how a check earns the right to say
  PASS.
- `CONTRIBUTING.md` and `SECURITY.md`.
