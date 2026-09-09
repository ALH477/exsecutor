# Lambda and closure-capture syntax — a proposal, not spec

`docs/spec/exsecutor-spec-v0.4.md` has no lambda syntax and no worked `sub`
example. Verified: `grep -ni 'lambda\|anonymous'` finds nothing; the only
`sub` example in the whole document is the statement form in §4.5,
`sub alloc = a;`, with no accompanying program. Sec 15 item 1 and Sec 4.2's
`[OPEN]` note are both stated in terms of "a lambda capturing a capability
from an enclosing `sub`" — a construct the language, as specified, never
writes down. This file invents the minimum surface syntax needed to write
that case down at all, so it can be run instead of only discussed. It is a
probe fixture, proposed here, not an amendment to the spec.

## Proposed: `functio` in expression position

```
functio(x: f32) -> f32 { redde nocens_helper(x, rete) }
```

Reuse the existing `functio` keyword as an anonymous literal wherever an
expression is expected — no new keyword, and consistent with `functio(f32)
-> f32` already denoting a function *type* in Sec 4.2's own `applica`
example. No `poscit` clause on the literal: a lambda is inherently
non-`publica`, so rule 5 already says its row is inferred from its body,
never declared. `sub`'s block and statement forms are used exactly as
Sec 4.5 already gives them.

## Incidental syntax (parser fixtures, not part of this proposal)

Three other rule families tested under `cases/` also have no worked syntax
in the spec (rule 7's module state, Sec 4.4's `impl`/cast forms). What is
used there — `mutabilis NAME: TYPE = EXPR;`, `impl Trait for Type poscit
ROW { … }`, `EXPR as dyn Trait poscit ROW` — is ad hoc, chosen only to be
easy for a tiny surface parser to recognize. Do not read design intent into
it.

## The closure-capture question

**Does positional substitution, as specified in Sec 4.2, catch a lambda
capturing a `sub`-bound capability once the closure escapes its
construction site?**

**No.** Reproduced by running the checker, not asserted:
`cases/bad_closure_capture.exsc` is a real violation — `exterior` declares
only `alloc` but its call chain exercises `rete` — and `exsecutor_check.py`
accepts it (see the `GAP` line in `run.sh`'s output). The file carries a
`KNOWN-GAP` marker specifically so this is a visible, checked, expected
outcome rather than a silently-passing bug in the probe.

### Why, precisely

Substitution (Sec 4.2) is defined over exactly two things: a named
function's own declared `poscit` (trusted at its boundary, rule 5), and a
`sicut PARAM` entry resolved by matching the actual argument's *syntactic
form* at a call site. The checker implements two such forms: a bare
reference to a named top-level function, and an inline lambda literal whose
body it can walk. Both presuppose the row is recoverable by looking
something up by **name**.

A `sub`-bound capability is acquired through rule 4's third path — lexical
binding — which is disjoint from the parameter/argument channel
substitution is defined over. Once a closure built from such a capture is
returned, stored, or threaded through an ordinary function-typed parameter,
it arrives at its eventual call site as a bare **value**: not a top-level
function, not a lambda literal syntactically present at that point, just an
identifier of function type with nothing attached. `cases/bad_closure_capture.exsc`
isolates exactly this: `fabrica` legitimately owns and correctly declares
`rete`; `consumidor` correctly declares `sicut f`; `exterior` receives the
already-built closure as a plain parameter `cerrado` and never writes the
name `fabrica` at all. Substitution's own vocabulary — "names one of the
callee's parameters," "the capability row of the actual argument" — has
nothing to attach to at that point, by construction, not by an
implementation gap that a smarter parser would close.

(A shallower version of the same question — can the checker see a bare
free-variable reference to a `sub`-bound capability *inside* a lambda body,
with no escape involved — is also unimplemented here, but was not what
isolated the gap: routing the captured value through a callee with a
capability-typed parameter turned out to be independently detectable for a
different, mundane reason (Sec 4.1 rule 4 treats a capability-typed
parameter as self-declaring). The escaped-closure construction above was
needed to isolate the failure that is actually load-bearing.)

### The shape a fix would need

Not a patch to substitution itself — substitution can stay exactly as
specified. What is missing is a prerequisite: a computed capability row
needs to be a property that travels **with a function-typed value**,
through assignment, return, and parameter-passing, not a property looked up
**by name** at the moment a value is handed to a call. That is closer to
row-polymorphic effect typing on function types than to an extension of
positional substitution. Substitution would still be the mechanism that
checks a row against a declaration at a boundary; it is the row's journey
between construction and use that currently has no representation at all.

If this were closed, the diagnostic would still be `EXS-E0421` — same
violation shape as every other case here, effective row exceeding declared
— so closing it needs no new registry entry, only the missing mechanism.

### What this does and does not earn down

Sec 4's `[UNREPRODUCED]` marking is not earned down to "closure capture is
solved" — it isn't. What is earned down, by actually running code, is the
difference between *untested* (the spec's prior state) and *tested and
shown to fail, for a stated and reproducible reason*. That is a smaller
claim than a positive result, and CLAUDE.md's evidence discipline is
explicit that the smaller, honest claim is the one to make.
