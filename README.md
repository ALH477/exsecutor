# Exsecutor

<img src="logo/exsecutor-logo.gif" alt="The Exsecutor mark: two crossed arrows forming an X, one ascending in dark navy, one descending in crimson" width="180" align="right">

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

`nix flake check` runs nine sandboxed derivations -- `smoke`, `test`, `audit`,
`buildExsecutorPackage-smoke` and the five vendor-integrity checks
(`vendor-integrity`, `wire-vendor-integrity`, `modem-vendor-integrity`,
`rx-vendor-integrity`, `streamdb-vendor-integrity`, one per tree under
`vendor/`) -- over the git-tracked tree only. `tests/run.sh`, which the `test` check runs,
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
docs/decisions/     ADRs 0001-0014, immutable once written; superseded, never edited (a status line and an Open list may be updated)
docs/design/        design documents: hypotheses with a status line, built against, and amended by what building found
docs/asm-conventions.md   the binding rules for every line of assembly here
examples/           the hello world, its golden output, HydraModem's transmitter and receiver, and their READMEs
prototypes/         Python design probes -- never shipped, never on the build closure (§18)
tests/              unit fixtures, the §14 conformance suite, the Stage 1 diagnostics corpus
tools/              the audits, generators, and the publish gate; nothing here is on the build path except as a check
vendor/             third-party, byte-exact, own licences: fasmg-x86/ (BSD-3-Clause), hydramesh-wire/, hydramodem-tx/, hydramodem-rx/ (LGPL-3.0-only), streamdb-v3/ (LGPL-2.1-or-later)
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

## Status as of `5417576` (2026-09-12)

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

101 bytes, no trailing newline. Its syscall surface is `write(1)`,
`exit_group`, and a `read(0)` it never issues — audited against
`{Mundus, ambitus}`, the capabilities the program actually has, with the
socket family a hard failure because `rete` is not among them. The `read` is
there because the runtime prelude gates its routines per capability *atom*,
not per call: a binary whose closure holds `ambitus` carries the standard
streams' reader as well as their writers (`docs/design/runtime.md` 2.6). What
the audit proves is a property of the closure, and that is exact.

**A real wire format, certified.** HydraMesh's DCF `DeModFrame` — a 17-byte
production quantum with eleven independent implementations — is written in
Exsecutor (`tests/conformance/entry23/codex.exsc`) and passes §14 entry 23,
the external certificate vendored at `vendor/hydramesh-wire/`:

```
entry 23: certificate: 246/246 vectors (encode basis 109/109, syndrome basis 137/137)
entry 23: anchors: 3/3 (section 3)
entry 23: laws: 218/218 (section 4), 136/136 (section 5)
```

The codec declares no capability; the 8,671-byte certificate binary writes a
2,502-byte stream and its syscalls are `read`, `write` and `exit_group`, the
`read` being the atom's, as above. Three
mechanical mutants (the CRC polynomial, one field's byte order, two fields
swapped) each fail at the vector `docs/design/wire-codec.md` predicts. The
certificate's theorem extends 246 vectors to all 2^108 frames *given* that the
codec is affine, which is argued from reading it, not measured. The codec needs
no bitwise and or or: `@transitus` field access is the mask, shift and byte
swap, so the language gained only `aut` (xor), `sursum`/`deorsum` (shifts),
hex literals, struct literals and one aggregate cast — spec §5.2, §5.4, §8.4,
§8.6.

**An acoustic modem, byte for byte.** HydraModem's transmitter — 2-FSK at
48 kHz and 1000 baud, CRC-16, a K=7 convolutional code, an interleaver and
CPFSK modulation — is written in Exsecutor (`examples/hydramodem/`; the
transmitter's seven files are 563 lines by `wc -l`). It writes WAV files
**byte-identical** to HydraModem's own reference transmitter, built from
source and vendored at `vendor/hydramodem-tx/` (ADR 0013):

```
hydramodem_loopback: stdout byte-identical to vendor/hydramodem-tx/d310123400a1ffffdeadbeef0a1b2ca961.wav
hydramodem_exemplum: stdout byte-identical to vendor/hydramodem-tx/d31312340001ffffdeadbeefab12cd24c0.wav
hydramodem_vacuum:   stdout byte-identical to vendor/hydramodem-tx/d310000000000000000000000000005b80.wav
hydramodem_basis:    stdout byte-identical to vendor/hydramodem-tx/symbola_basis.bin
```

The last line is a 137-word basis of the symbol stream. With the three WAVs
and the per-symbol memorylessness of the modulator, it extends to
byte-identical audio for every one of 2^136 inputs, *given* that the stream is
affine over GF(2). That is argued from the code, not measured
(`docs/design/modem.md` §13 lists the premises). It uses no floating point, no
signed arithmetic, no bitwise and/or and no remainder. Its sine table is now a
48-entry array literal (M5, `76ca763`); the four certificates above did not
move by a byte when it changed. Reference renders of four more profiles
(4-FSK, 8-FSK, 125 baud, and the aux-cable profile as far as HydraModem's own
CLI can express it) are vendored under `vendor/hydramodem-tx/profiles/`; no
Exsecutor program targets them yet.

**And the receiver, certified by its verdicts.** The other half is written too
(ADR 0014): it reads a WAV on standard input and writes the seventeen bytes it
carries, in integers throughout -- no floating point, no division, no
remainder, no signed shift -- against a reference that is `double` from end to
end. It cannot be held to bit identity, because two correct receivers disagree
on their internals by construction, so it is held to **decode success against
HydraModem's own receiver**: `vendor/hydramodem-rx/` holds seventy impaired
WAVs (white noise from +12 to -12 dB, sample-clock offsets to ±3000 ppm,
carrier-frequency offsets to ±300 Hz) and `frame_rx`'s verdict on each, and the
Exsecutor receiver decodes **all 62 the reference decodes** and never writes a
frame that is not the input's. The three clean WAVs decode, and 140 words
round-trip transmitter-to-receiver in one process. Every vector is an integer
function of the vendored transmitter output and the integers in
`vendor/hydramodem-rx/PROVENANCE.md`, which prints the generator verbatim.

**A second backend, and a third external format.** `exsc --emitte c` emits a
C11 translation unit of pure functions (`docs/design/c-backend.md`, ADR 0012).
Its acceptance test is that the two backends cannot be told apart: for every
IR fixture and every eligible program, the C build must match the reference
build's stdout, exit status and trap behaviour under gcc and clang at two
optimisation levels, all under UBSan. They match everywhere.

The first program through it is a **StreamDB v3 reader** — the third-party
container the Kiln N64 engine reads (`examples/streamdb/`,
`vendor/streamdb-v3/`). It parses the alternating 128-byte header slots, the
sorted index and the reversed trie with no allocation and no capability, and
is certified against the upstream writer's own bytes: all 24 documents
byte-exact, both suffix searches in traversal order, and three corrupted
containers behaving exactly as the reference C reader does — including
falling back to the older commit when the newer header is damaged. The
emitted C unit is 137,742 bytes and reproduces byte-identically across
divergent directory, locale, time zone and hostname.

Declaring the header as a `@transitus` struct forced two zero-pad gaps in the
format into the open that no survey of it had listed, and declaring the
16-byte UUID as two `u64:maior` halves makes the index's byte-order sort an
integer comparison — the binary search has no byte loop. There is no shift and
no mask anywhere in the header, index or record parsing.

**What runs:**

- `make all` → `build/exsc`, **453,732 bytes**, freestanding, no libc.
- **All three stages of §16 reach end to end.** Stage 1: the §8.1 source gate,
  the lexer, the lossless CST, the typed AST. Stage 2: name resolution, types,
  capability rows, packed layout — the lexicon pass is built and **not
  enabled**, because §3.3's root table is illustrative and rejects the
  language's own canonical names, which its fixture asserts. Stage 3: the
  lowering to SSA IR, the verifier, and the fasmg reference backend — phi,
  narrow integers at any width with trapping and wrapping arithmetic, byte
  order and sub-byte bit fields, arrays with bounds checks, array literals, a
  `copy` that is a loop above 128 bytes (`9ede8bf`, so a struct literal with a
  144,000-byte array field compiles in constant text) — with its runtime
  prelude.
- **Both kill criteria that could fire have been evaluated.** Stage 1's
  (diagnostics) fired, was fixed, and was re-measured against a checked-in
  corpus — `docs/design/diagnostics-review.md`, final section, and
  `tests/diagnostics/`. Stage 2's (`sub` resolution needing a search) does not
  fire, argued first in `docs/design/checker.md` §2.1.
- `tests/run.sh`: **1351 pass, 0 fail** in 3 m 46 s — 166 unit fixtures; 50 IR
  fixtures and 96 Exsecutor programs, each compiled, assembled, **run**, and
  syscall-audited (70 of those programs are the receiver's impaired vectors);
  and a **differential phase**: 156 IR builds and 104 program builds in which
  the C backend's output must agree with the reference's on stdout bytes, exit
  status and trap-or-not, across gcc and clang at `-O0` and `-O2`, every one
  under `-fsanitize=undefined -fno-sanitize-recover=all`. They agree
  everywhere; zero sanitizer reports;
  10 of 24 conformance entries running, each required to emit exactly its
  expected code and nothing else (the other 14 report `DEFERRED` and are never
  counted as passing); 0 program directories deferred.
- `make audit`: PASS — the nine allowlisted syscalls and nothing else, on the
  real binary. `make reproduce`: PASS, byte-identical (426,389 bytes) across
  directory, `TZ`, locale, `SOURCE_DATE_EPOCH`, umask and hostname.
  `tools/spec-check.sh`: PASS, 49 error codes in sync with §13.
  `tools/syscall-audit.sh --self-test`: PASS, including the prelude's own
  reader binary accepted under `Mundus,ambitus` and rejected under `Mundus`.
  `nix flake check`: green (run by the gate).

**What does not run yet.** Most of the language beyond what these programs use
is `rassert`-refused rather than lowered: `contrahe` and its reduction triple,
lambdas, `eventus`, generics, floating point, and every `numeri` but the
default. Bitwise and/or, division, remainder and signed shifts are unspecified
(`[OPEN]`); narrowing and equal-width `sicut` are truncation (spec §5.4), with
narrowing from a signed source still unwritten by any program. An array
literal at module scope (`publica firma t: acies<u16, 4> = [1, 2, 3, 4];`)
type-checks and traps in the lowering under `-o` — the transmitter's table is
a function returning the literal for that reason (`docs/design/modem.md` D4).
A large local array is bounded only by the stack: `[0; 1000000]` of `i64`
runs and `[0; 2000000]` is a SIGSEGV with no diagnostic (re-measured at this
commit under the default 8 MiB `RLIMIT_STACK`), because the emitter has no
stack probe and §13 has no code for it. `exsc` running out of its own
compilation arena says so on stderr since `9ede8bf` and still exits 132, as
that commit's mutation run recorded. The checker
refuses a correct program where a `poscit sicut s` function calls another
with the same `s` (`EXS-E0421`; the spec is right, `docs/design/wire-codec.md`
finding 9). The C backend exists in **library mode only** — a translation
unit of pure functions, no entry point, no runtime, no ARC — and refuses 23
of the 60 IR opcodes by name, every one of them an opcode the reference
backend does not lower either (floats, `div`/`rem`, the overflow predicates,
`callind`, the reductions). A 32-bit-pointer `--hospes` row, which the N64
needs, is **not** there: `ptr` is interned at 64 bits in a file the C backend
reuses unchanged, and the reference emitter would silently miscompile such a
module rather than refuse it (`docs/design/c-backend.md` finding 18). Neither
does the `ego` reader, the module system, the LSP, or `exsc emenda`. `EXS-E0105`
(confusables) has no hermetic data source. The emitted program's capability
mask is an over-approximation with the exact fix recorded beside it, and,
separately, every `ambitus` binary carries `read` whether it reads or not
(above) — a property of the closure the audit states exactly, and of the
binary that a per-call gate would tighten.

<!-- END STATUS BLOCK -->

## Licence

Exsecutor is GPL-3.0-or-later (`LICENSE`), with two additional permissions
under GPLv3 section 7 in `LICENSE.EXCEPTION`: Exception A lets you distribute
what `exsc` produces from your input -- emitted source, object code,
executables, `ego` files, diagnostics -- under terms of your choosing, and
Exception B is the GNU Classpath linking exception, attaching only to a file
whose own header carries the designation line, which no file in this repository
currently does. `vendor/` is third-party and keeps its own licences --
`vendor/fasmg-x86/` is BSD-3-Clause; `vendor/hydramesh-wire/`,
`vendor/hydramodem-tx/` and `vendor/hydramodem-rx/` are LGPL-3.0-only;
`vendor/streamdb-v3/` is LGPL-2.1-or-later (the upstream C edition's own
licence, per its file headers and README -- see
`vendor/streamdb-v3/PROVENANCE.md`'s licensing section, since the upstream
repository's single root LICENSE file text is LGPLv3 and governs only the
Rust edition) -- and none of the exceptions apply
to it. `LICENSE.EXCEPTION` states that it has not been reviewed by a lawyer,
and so does this sentence.

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
