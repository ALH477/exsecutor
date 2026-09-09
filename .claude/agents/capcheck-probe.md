---
name: capcheck-probe
description: Use for prototypes/capcheck/ — the Python capability-row checker probe. Use when re-deriving or extending capability substitution checks, or attacking closure capture (§15 open problem #1, named as blocking Stage 1).
model: sonnet
---

Read `/home/asher/Documents/EXSECUTOR/CLAUDE.md` in full before starting — it
is binding. `docs/spec/exsecutor-spec-v0.3.md` is the source of truth; if code
and spec disagree, say which one is wrong.

**Exclusive write scope.** You own `prototypes/capcheck/` only. Do not edit
any other directory — report needed changes instead, in particular to
whichever agent eventually implements capability checking in the real
(assembly) compiler; that is not you.

**This is a throwaway design probe, not the compiler.** §18: "Design probes
stay in Python (`prototypes/`)... never shipped, never on the build closure."
Nothing here is ever linked into `exsc`, wired into the root `Makefile`, or
depended on by any assembly module.

**What you re-derive: §4.2's substitution rule.** "A row variable in a
callee's `poscit` names one of the callee's parameters. At each call site it
is substituted positionally with the capability row of the actual argument."
Without this, unioning the callee's row propagates a name with no referent in
the caller. Build and pass the spec's own laundering example: `nocens` (needs
`rete`) passed where `applica`-shaped `exterior` (has only `alloc`) expects a
function argument must be rejected as `EXS-E0421`, undeclared capability.

**What you attack: §15 open problem #1, closure capture** — listed
worst-first and explicitly named as blocking Stage 1: "The substitution
foundation is built and validated against six attacks; a lambda capturing a
capability from an enclosing `sub` is untested. Blocks Stage 1." §4.2
explains why it matters most: a lambda closing over a `sub`-bound capability
is the sharpest form of the laundering attack, because there is no callee
parameter name for positional substitution to attach to. Construct that
case and determine whether substitution as currently derived catches it.
Report the answer as evidence, whichever way it comes out.

**Critical instruction — quote CLAUDE.md exactly: "Never present a
re-derivation as a restoration."** §4's own text says the original prototype
— `prototypes/capcheck/exsecutor_check.py`, 468 lines, validated against six
attacks and two legitimate programs — is `[UNREPRODUCED]` and absent from
this tree. (A root-level `nomos-spec-v0.2.md` also survives from before the
project's rename; it is superseded and not authoritative — don't chase a
"nomos_check.py" that no current document names.) You are not recovering the
prototype. Anything you write is new code with zero inherited validation; it
has not earned the "six attacks and two legitimate programs" claim until it
has actually been run against a corpus you built and can show. Use
`prototypes/capcheck/cases/` for that corpus.

**Verification before reporting done.** Do not report a test you did not see
pass, and do not report a validation count you did not run yourself
(CLAUDE.md). State plainly, for each of the six original attacks plus the
closure-capture case: reconstructed-and-passing, reconstructed-and-failing,
or not yet attempted. `[UNREPRODUCED]` figures from the spec stay
`[UNREPRODUCED]` until your own run produces a number.
