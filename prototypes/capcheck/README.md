# prototypes/capcheck/

Probe for the §4 capability-row checker: a tool that reads capability
declarations (`poscit` clauses) on `functio` signatures and enforces §4.2's
substitution rule —

> A row variable in a callee's `poscit` names one of the **callee's**
> parameters. At each call site it is substituted **positionally** with the
> capability row of the actual argument.

— so that capability laundering is a compile error rather than an annotation
nobody checks. §4.2 gives the canonical attack this exists to catch: passing
a function requiring `rete` where the caller only declared `alloc`
(`nocens`/`exterior`, `EXS-E0421`).

## What is not here

**The original checker is absent from this tree.** §4 says so directly, in
its own opening line:

> Prototype: `prototypes/capcheck/exsecutor_check.py`, 468 lines, validated
> against six attacks and two legitimate programs. `[UNREPRODUCED]` — that
> artifact is absent from the tree. The checker is being rebuilt from this
> section, which is a re-derivation, not a restoration.

`exsecutor_check.py` does not exist anywhere in this repository or its
history. `cases/` is empty. `run.sh` in this directory fails on purpose,
with an explicit message, until both exist. Every number attached to this
prototype in §4 — 468 lines, six attacks, two legitimate programs — describes
a file nobody here has seen and cannot currently be checked against
anything. Treat those numbers as historical description, not current fact.

## Re-derivation, not restoration

CLAUDE.md's evidence discipline says this exactly: **"Never present a
re-derivation as a restoration."** When `exsecutor_check.py` is next written,
it will be new code — produced by reading §4.2 and re-implementing the
substitution rule — not a recovery of the lost 468 lines. It may end up
checking the same things the old file did, or it may not; either way, "468
lines," "six attacks," and "two legitimate programs" describe a *different
artifact*, and the new one does not inherit those numbers by resemblance.

Any future claim that the rebuilt checker is "validated against six attacks
and two legitimate programs" has to be re-earned: six attack cases and two
legitimate-program cases have to exist under `cases/`, the new checker has to
run against all of them, and someone has to watch it pass. CLAUDE.md is
explicit about this too: **"Never report a benchmark you did not run, or a
test you did not see pass."** Until that happens, this checker's validation
status is `[UNREPRODUCED]`, same as §4's — not "restored," not "as before."

## What a rebuild needs

**1. `exsecutor_check.py` itself.** Not written in this change — this
directory is design probes, never shipped, never a build dependency (see
`prototypes/README.md`). At minimum it needs to recognize `poscit` on
`functio` declarations and apply §4.2's positional substitution at call
sites, flagging any row that doesn't reduce to something the caller already
declared.

**2. Case files: `cases/ok_*.exsc` and `cases/bad_*.exsc`.** The spec does not
say which six attacks or two legitimate programs the original file used —
that detail did not survive with the file. What §4 does supply is enough
rule content to derive candidates for a rebuild's case set. This list is
**inference from the spec text, not a recovered list**:

Attack candidates for `bad_*.exsc` (one rule violation each):

- Capability laundering through row substitution — §4.2's own
  `nocens`/`exterior` example, `EXS-E0421`.
- **Closure capture**: a lambda capturing a capability from an enclosing
  `sub` without declaring it. §4.2 calls this "the sharpest form of this
  attack" and it is explicitly *not* something the original prototype ever
  handled ("the prototype handles named functions and function-typed
  parameters... untested"). A rebuild that only matches the old file's
  coverage still leaves this open — see below for why that matters.
- A capability literal, cast, or default — forging a capability value
  directly, which §4.1 rule 1 says is impossible by construction.
- A capability derived from something other than `Mundus`, or derived
  implicitly rather than "explicitly and fallibly" (§4.1 rule 2).
- Module-level mutable state holding a capability without declaring it —
  `EXS-E0501` (§4.1 rule 7).
- A capability-bearing type (§4.3 — a struct with a capability-typed field,
  transitively) not declared as such, hiding it from the `ego` audit.
- A `dyn Trait poscit P` constructed from an implementation whose mark
  exceeds `P` — `EXS-E0510` (§4.4).

That is seven candidates for six slots; the spec supports more attack
surface than the original file evidently tested, so a rebuild has room to
choose. Closure capture should not be the one left out again.

Legitimate-program candidates for `ok_*.exsc` (checking for false positives):

- A pure function — no `poscit`, no capability parameters — that allocates
  or diverges but never observes the host (§4.1 rule 6: pure with respect to
  ambient state).
- A well-formed higher-order function using row substitution correctly —
  §4.2's own `applica(v, f)` shape, called with an argument whose `poscit`
  is a subset of what the caller already declares.

## Why this is more than restoring lost coverage

§15 ranks closure capture as open problem **#1**, worst first:

> Closure capture in capability rows (§4.2). The substitution foundation is
> built and validated against six attacks; a lambda capturing a capability
> from an enclosing `sub` is untested. Blocks Stage 1.

§16 lists it as the one item remaining before Stage 1 that touches this
prototype: **"Closure capture in the prototype. ~1 week."** So parity with
the lost 468 lines is not the actual target — the lost file never covered
the one case that currently blocks the roadmap. A rebuild that reproduces
only what the old file tested reproduces an already-known gap.

## `cases/`

Empty. See "What a rebuild needs" above for what belongs here.
