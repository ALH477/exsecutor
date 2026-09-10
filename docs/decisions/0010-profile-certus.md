# 0010 — A safety-critical profile, defined alongside the language

**Status:** Accepted as design, 2026-09-09. **Nothing implemented.**
**Relates to:** spec §4, §5.4, §6.7, §7.1, §9.3, §9.4, §10.2, §13, §15 #3;
`docs/design/profile-certus.md`

## Context

The question was whether this language could be suitable for defence and
safety-critical use. The properties that domain finds hardest to establish —
reproducible builds, enumerable authority, no build-time code execution,
artifact-level proof that a binary cannot reach the network — are ones Exsecutor
already has or has designed. That is an unusually good starting position, and it
is mostly accidental: those properties were chosen for §1's thesis, not for
certification.

The properties that domain finds *disqualifying* are also present. §15 #3
records reference cycles as *"No answer. Accepted cost, with a DoS exposure to
document."* An unresolved memory-leak class does not survive a DAL A/B
conversation.

Safety-critical subsets are normally carved out of languages that already
shipped — MISRA out of C, SPARK out of Ada — and the carving hurts because the
language is already full of things the subset must forbid. Exsecutor has no
compiler and no users. This is the only moment defining the subset is cheap, and
SPARK's success owes a great deal to Ada's designers having had it in mind.

## Decision

Define **`certus`**, a safety-critical profile, now — as design, enforced later.
Full rules in `docs/design/profile-certus.md`.

The name is attested classical Latin for *fixed, settled, determined*, and is
the root from which *certify* descends. It needs no coinage. (*Profile* itself
does, and is flagged for §3.9's ladder.)

Three decisions carry the weight:

**1. The profile is mostly subtraction.** Almost every rule is a restriction on
what a `poscit` may contain, or a tightening of something already declared:
`Crudum` withheld, `rete` forbidden, `alloc` absent outside `initium`, `numeri`
mandatory rather than defaulted. A subset needing a parallel type system would
have been evidence the language was badly aimed; this one needing so little is
weak evidence of the opposite.

**2. `certus` dissolves §15 #3 rather than deferring it.** The profile forbids
reference counting outright — no ARC, no owning `refero`, therefore no cycles as
a structural impossibility rather than a mitigated risk. Memory is arena/region
only, sized during `initium`, with nothing allocated afterwards. This also
retires ARC's other certification problem, non-deterministic destruction timing.
**§15 #3 remains open for the full language.** The profile is not a claim to
have solved it.

**3. Profile conformance is an interface property.** A `certus` module may
depend only on `certus` modules, transitively, and the profile is declared in
the `ego`. §10.2 already makes the dependency graph and capability closure
computable from `ego` files alone — so whole-program profile verification
happens before anything is built. Compare MISRA, where establishing a
third-party library's conformance means reading it.

## Consequences

**Positive**

- The design question is answered while it is still free. Every later language
  decision now has a second test: *does this remain expressible under `certus`?*
- §7.1's monomorphization, described there as *"a link-time optimization, never
  a semantic requirement,"* becomes a profile requirement — all calls direct, no
  witness-table indirection at runtime, worst-case analysis tractable.
- Rule 9 (no capturing closures) sidesteps §15 #1's closure-capture problem
  inside the profile, independently of how that open problem resolves for the
  full language.
- A formal semantics for the `certus` subset is far more tractable than for the
  full language, and higher assurance levels increasingly expect one. Defining
  the subset early is what makes that reachable at all.

**Negative**

- **A second dialect to specify, implement, test and document**, for a project
  whose first compiler does not exist. Profile drift — where `certus` and the
  full language diverge because only one is exercised — is a standing risk and
  MISRA has suffered exactly it.
- Eight classes of profile violation need diagnostics, and CLAUDE.md forbids
  inventing codes. **A §13 amendment is a prerequisite**, not a follow-up.
  **Met**: §13's `08xx` range, grouped to the profile's own sections. The
  checker remains unwritten, but it is now buildable without inventing a code
  — which was the whole point of calling this a prerequisite.
- MC/DC coverage instrumentation is compiler support that is very painful to
  retrofit and must be designed alongside the CST and backend.
- **Tool qualification (DO-330) remains the dominant cost and this decision does
  not reduce it.** A new compiler has no qualification evidence and no service
  history. This is why the domain runs decades-old toolchains, and no subset
  changes it.
- Contracts — SPARK's actual source of value — are absent, and adding them is a
  language change rather than a profile restriction. `[OPEN]`

**Neutral**

- The profile inherits the full language's syntax; it is a checker mode
  (`exsc aedifica --profilum certum`) plus an `ego` declaration, not a fork.
- The licensing arrangement already settled in ADR 0006 happens to matter here:
  a GPL compiler with the output exception means a contractor's deliverable
  carries no copyleft. That is GCC's arrangement, and it is why GCC is usable
  in this domain.

## Open

- No rule has been enforced against any Exsecutor source, because none exists.
  `[UNTESTED]`
- Whether `certus` grows contracts is the largest unresolved design question.
- The fixed-point-only sub-profile (rule 17) has no type to rest on.
- Mapping rules onto specific DO-178C objectives or MIL-HDBK-516 criteria
  requires a qualified authority and is not attempted.

**This profile does not make the language certifiable.** It makes it not
obviously uncertifiable, which is a smaller and more honest claim, and the only
one available today.
