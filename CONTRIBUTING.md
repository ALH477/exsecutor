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
are the byte-exact paths `.gitattributes` names -- `vendor/`,
`tests/conformance/`, a program test's `expected.out`, and `tests/data/*.bin`,
which are a fixture's stdout and its stdin rather than source and may be raw
bytes -- for the reasons written there.

## The README gate

`README.md` is gated by [TrvthNvke](https://github.com/ALH477/TrvthNvke)
(policy: `.trvthnvke.toml`), the same evidence discipline as everything above
this section, applied to prose instead of code: a checkable sentence is bound
to a claim -- an HTML comment (invisible on GitHub) or a fenced command --
and the gate re-runs it rather than trusting the sentence. `docs/ARCHITECTURE.md`
in the TrvthNvke repository explains the mechanism; this section says what it
means here.

What it refuses:

- a bound claim that no longer verifies (a stale count, a renamed file, a
  moved path);
- a code fence in a gated doc with neither `truth:id=...` nor `truth:ignore`
  (an undocumented "copy this and run it" block);
- a malformed, duplicate, or unclosed claim block;
- a missing required heading;
- (locally and in CI) a policy file that does not match `.trvthnvke.lock`,
  which is regenerated with `trvthnvke lock`, never edited by hand.

**Moving a fixture floor is a three-step change, and the gate announces only
the first.** This has caught two sessions on 2026-09-27 alone, so it is
written down rather than rediscovered: the floors in `tests/run.sh` are bound
by a `command` claim whose allowlisted `grep` matches the floor **literals**,
so raising one turns that claim red. The fix is not a README edit. It is
(1) the floor in `tests/run.sh`, (2) the same literal in
`.trvthnvke.toml`'s `command_allow` regex — which is a *policy* edit — and
(3) `trvthnvke lock`, because a policy file that no longer matches its lock
fails every gate afterwards as `policy_drift`, a verdict whose text names a
hash mismatch and not the step you missed. All three belong in the commit
that moves the floor.

**Know which half of a claim the gate actually holds you to.** Measured on
2026-09-26 by breaking a claim of each shape and watching the verdict:

- **Path- and symbol-valued kinds** (`file_exists`, `dir_exists`,
  `file_contains`, `python_symbol`, `entrypoint`) enforce *entailment*: the
  bound value must appear **in the prose**, so the sentence and the tree cannot
  drift apart. Renaming the spec in the sentence alone fails the gate with
  `claim body does not mention bound value 'docs/spec/exsecutor-spec-v0.4.md'`.
  This is the strong form, and it is why paths are worth binding.
- **Number-valued kinds** (`glob_count`, `version_sync`, and `command`'s
  `expect_stdout`) do **not**. They check the *attribute* against the tree --
  `equals: 16` against sixteen matching files -- and never look at the
  sentence. So editing "sixteen ADRs" to "fifteen" beside a passing
  `glob_count equals: 16` **is not caught.** Verified, not assumed.

The consequence for anyone adding a count: `glob_count` pins the attribute, and
the attribute sits three lines above the prose where a reviewer will see it --
which is worth having, but it is not the guarantee the strong form gives. Where
a number is stated by some other document, bind it *there* instead and get
entailment back: `README.md`'s conformance count is a `file_contains` against
the spec's own "twenty-nine", not a re-derivation, which is both non-circular
and drift-proof. Prefer that shape whenever an external source of truth exists.

**What the gate cannot reach, and therefore what still needs a human.** The
gate's command runner is confined, allowlisted and time-capped, so anything
needing `fasmg`, a C toolchain, a GPU or emulation is out of its reach. Three
figures in `README.md` are in that class and each says so at the point it is
stated, with the commit it was measured at:

- `build/exsc`'s size -- needs `fasmg`, and `build/` is not in the repository.
- the emitted C unit's size -- needs `exsc`. This one drifts easily: every
  emitted unit carries the whole of `compiler/x86_64/backend_c/prologue.c.in`,
  so any addition to that runtime grows all of them at once. The 2026-09-26
  wave moved it by 13,303 bytes.
- the entry-23 certificate binary's size -- needs `exsc` and `fasmg`.

For each, what is gated instead is the property that matters more than the
number: `make reproduce` holds them byte-identical across directory, `TZ`,
locale, `SOURCE_DATE_EPOCH`, umask and hostname. A figure that cannot be
re-derived from inside the gate is stated with its commit and marked as
ungated, rather than left looking checked. And a figure that moves on every
fixture added -- the suite's total pass count -- is no longer stated at all:
the fixture FLOORS are what the gate binds, because those are what can be
re-derived.

It does **not** refuse an unbound `unbound_path_token` warning -- most paths
named in `README.md` are not bound to a claim, by choice, the same way most of
the compiler's own path citations are not re-verified by `tools/spec-check.sh`.
`fail_on = "error"` in `.trvthnvke.toml` means only claim failures, missing
headings, and unbound fences are gate failures; coverage gaps are reported,
not blocking.

To add a claim, wrap the sentence in a matched pair of comments:

```markdown
<!-- truth:claim
id: some-stable-id
kind: file_exists
path: tools/some-script.sh
-->
`tools/some-script.sh` does the thing this sentence says it does.
<!-- truth:end -->
```

The claim's `kind` decides what gets re-run (`file_exists`, `dir_exists`,
`glob_count`, `file_contains`, `command`, `heading`, `rel_link`,
`version_sync`, and a few more -- `trvthnvke schema` prints the full list with
every attribute each kind accepts). For a `file_exists`/`dir_exists`/
`file_contains` claim, the bound value (the path or pattern) must appear
literally in the sentence's own text -- `require_entailment = true` means the
checker refuses to bind a claim to a number or a path the sentence does not
actually say, which is what stops the sentence and the check from drifting
apart again. A number that is a floor rather than an exact count (this
project adds fixtures continuously; see `tests/run.sh`'s own floors) is bound
with `min:`, not `equals:`; bind `equals:` only where the count is genuinely
fixed, the way an ADR number or a byte count is.

Some sentences cannot be bound: a figure that requires the full `nix develop`
toolchain or a GPU to re-derive is out of reach of the gate's confined,
allow-listed command runner (`command_mode = "allowlist"`; `command_allow` in
`.trvthnvke.toml` is a closed list, the same idea as the syscall allowlist
above), and a "status as of this commit" line can never verify itself, because
the commit that updates it is always one ahead of the hash it names. Such a
sentence is marked `kind: prose` (tracked, not machine-checked -- the same
role `[OPEN]`/`[UNTESTED]`/`[UNREPRODUCED]` play above) rather than left
silently unbound, or is left as ordinary prose and reported only as a
coverage warning.

Verify locally with `trvthnvke verify --fail`. The tool is a flake input
(`flake.nix`: `inputs.trvthnvke`, following this flake's nixpkgs), so it is
on `PATH` inside `nix develop` and `nix flake check` runs the same gate
sandboxed as `checks.readme`; outside the devShell,
`pip install git+https://github.com/ALH477/TrvthNvke` into a virtualenv works
too. Install the pre-commit gate with
`cp hooks/pre-commit .git/hooks/pre-commit && chmod +x .git/hooks/pre-commit`
(or `trvthnvke install-hook`); the hook falls back to `nix develop -c` when
the tool is not on `PATH`, and refuses -- never skips -- when nothing can run
it. `.github/workflows/trvthnvke.yml` runs the same check in CI, and writes
a receipt to `.trvthnvke/` (ignored, uploaded as an artifact).

Agents get the same gate as a tool: `.mcp.json` starts TrvthNvke's stdio MCP
server (`trvthnvke_list_claims`, `trvthnvke_propose_edit`,
`trvthnvke_apply_edit` -- which reverts if the gate fails -- and
`trvthnvke_verify`), and CLAUDE.md's "The README gate" section is the
contract: change the code first, then the claim the code made false; never
rewrite the README freehand.

Until 2026-09-27 the tool was installed nowhere on the development machine
and the hook was not installed in the checkout, so the gate ran in CI on
every push and nowhere locally. An earlier version of this sentence added
"and CI was blocked", which was false: `gh run list` shows both the `ci` and
`TrvthNvke` workflows completing successfully on every push, this one
included. The claim came from a memory about Actions billing that was months
stale and was repeated without being checked -- the same failure mode as
reporting a benchmark you did not run. The flake input and the hook's
fallback exist so the local half cannot go missing silently again.

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

## Copyright and outside contributions

DeMoD LLC holds the copyright in this repository's own work. The licence is
GPL-3.0-or-later with the permissions in `LICENSE.EXCEPTION`, plus the grants
in `LICENSE.GRANTS`; `vendor/` keeps its own licences. `REUSE.toml` records
the holder and licence of every file, the licence texts are in `LICENSES/`,
and CI runs `reuse lint`, so a new file needs either an SPDX header or an
entry there.

Outside contributions will require a contributor licence agreement with
DeMoD LLC. That agreement is being prepared. Until it is published, pull
requests from outside contributors are not merged.

## Commit trailers

A substantial part of this tree was written by Claude (Anthropic) working
under the author's direction, and commits made through that tooling carry a
`Co-Authored-By: Claude ... <noreply@anthropic.com>` trailer. That is normal
here and is attribution, not a review signal; the evidence discipline above
applies to those commits exactly as to any other.

## Code of conduct

There is no code of conduct.
