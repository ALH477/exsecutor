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
     Every figure is from running the named command at the named commit.
     Refresh it here and nowhere else. -->

## Status as of `2e257f0` (2026-09-10)

Every figure here was produced by running the named command at this commit, in
the `nix develop` shell, on `x86_64-linux`.

**The publish gate is met.** `tools/publish-gate.sh` reports `RESULT: GATE MET`
— twelve checks of twelve. The hello world compiles and runs:

```
$ build/exsc aedifica --hospes x86_64-linux \
      examples/saluta.exsc examples/imprime.exsc examples/initium.exsc -o hello.asm
$ INCLUDE=vendor/fasmg-x86 fasmg hello.asm hello && ./hello
Ave, mundus.

Ex silentio surgit forma.
Ex signo nascitur vox.
Ex codice fit lumen.

Hodie incipimus.
$ ./hello | cmp - examples/saluta.expected && echo BYTES MATCH
BYTES MATCH
```

101 bytes, no trailing newline. The binary is 1,074 bytes, statically linked,
and its entire syscall surface is one `write(1)` and `exit_group` — audited
against `{Mundus, ambitus}`, the capabilities the program actually has, with
the socket family a hard failure because `rete` is not among them.

**What runs:**

- `make all` → `build/exsc`, **397,785 bytes**, freestanding, no libc.
- **All three stages of §16 reach end to end for this program.** Stage 1: the
  §8.1 source gate, the lexer, the lossless CST, the typed AST. Stage 2: name
  resolution, types, capability rows, packed layout — the lexicon pass is built
  and **not enabled**, because §3.3's root table is illustrative and rejects the
  language's own canonical names, which its fixture asserts. Stage 3: the
  lowering to SSA IR, the verifier, and the fasmg reference backend with its
  runtime prelude.
- **Both kill criteria that could fire have been evaluated.** Stage 1's
  (diagnostics) fired, was fixed, and was re-measured against a checked-in
  corpus — `docs/design/diagnostics-review.md`, final section, and
  `tests/diagnostics/`. Stage 2's (`sub` resolution needing a search) does not
  fire, argued first in `docs/design/checker.md` §2.1.
- `tests/run.sh`: **405 pass, 0 fail**, 141 unit fixtures, 6 of 24 conformance
  entries running (the other 18 report `DEFERRED` and are never counted as
  passing).
- `make audit`: PASS — the nine allowlisted syscalls and nothing else, on the
  real binary. `make reproduce`: PASS, byte-identical across directory, `TZ`,
  locale, `SOURCE_DATE_EPOCH`, umask and hostname. `tools/spec-check.sh`: PASS,
  49 error codes in sync with §13. `nix flake check`: green.

**What does not run yet.** Only one program has been compiled end to end, and
most of the language is `rassert`-refused rather than lowered: `contrahe` and
its reduction triple, lambdas, `eventus`, generics, and every `numeri` but the
default. The C backend (§9.2's reach backend) does not exist; neither does the
`ego` reader, the module system, the LSP, or `exsc emenda`. `EXS-E0105`
(confusables) has no hermetic data source. The emitted program's capability
mask is an over-approximation with the exact fix recorded beside it.

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
