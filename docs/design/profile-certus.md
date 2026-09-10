# `certus` — the safety-critical profile

**Status:** `[OPEN]` — design only. No compiler, no profile checker, nothing
qualified. Stage 2 at the earliest for enforcement.
**Relates to:** spec §4, §5.4, §6, §7.1, §9.3, §9.4, §10.1, §10.2, §10.3, §13,
§15 #3; [ADR 0010](../decisions/0010-profile-certus.md)

---

## 1. Why a profile, and why now

Safety-critical subsets are carved out of languages that were not designed for
them — MISRA out of C, SPARK out of Ada — and the carving is always painful
because the language already shipped features the subset must forbid.

Exsecutor has no users and no compiler. Defining the subset *alongside* the
language is the one moment it is cheap. SPARK works because Ada's designers
were thinking about it; MISRA is a list of ways to avoid C.

The name is `certus` — attested classical Latin for *fixed, settled,
determined*, and genuinely the root from which *certify* descends
(`certus` + `facere`). It decomposes under §3 without coinage.

> **§3.9 note.** *Profile* itself has no clean Latin root and must descend the
> §3.9.2 ladder before this is named in the language proper. `descriptio` and
> `norma` are both taken or ambiguous. Flagged, not resolved.

## 2. The central claim: mostly subtraction, not addition

Most of this profile is expressible in machinery the language already has. That
is the strongest evidence available that the design was well aimed — a subset
that needed a parallel type system would be an indictment.

| profile requirement | mechanism that already exists |
|---|---|
| no ambient authority | `poscit` (§4) — already mandatory and already audited |
| no hidden allocation | `poscit alloc` is already visible in every signature |
| no network, clock, filesystem, randomness | already separate capabilities |
| no raw pointers | `Crudum` is already a capability to withhold |
| whole-closure conformance | `ego` closures are already evaluable without building (§10.2) |
| deterministic arithmetic | `numeri` (§5.4) already declares it |
| no build-time code execution | §9.4 already forbids build scripts |

The genuinely new parts are §5 (memory), §6 (bounded control flow), and §8
(coverage). Everything else is a restriction on what a `poscit` may contain.

## 3. `certus` dissolves §15 #3 rather than deferring it

§15 open problem #3 reads: *"Reference cycles (§6.7). No answer. Accepted cost,
with a DoS exposure to document."*

An unresolved memory-leak class is disqualifying for DAL A/B, so the profile
cannot inherit it. It does not solve it either:

> **`certus` forbids reference counting entirely.** No ARC, no `refero` owning
> references, therefore no cycles — not as a mitigation but as a structural
> impossibility.

Memory is arena/region only, every arena sized during `initium`, nothing
allocated after initialisation completes. This is JPL Power-of-10 rule 3 and
DO-178C's practical position on dynamic memory, and it also retires ARC's
second certification problem — non-deterministic destruction timing — because
there are no destructors running at unpredictable points.

The full language keeps ARC and keeps the open problem. §15 #3 stays open *for
the full language*, and the profile is not a claim to have closed it.

## 4. Rules

### 4.1 Memory

1. **No ARC.** No `refero` owning references. No reference counting.
2. **Arena/region allocation only**, all arenas created and sized during
   `initium`.
3. **No allocation after initialisation.** A `certus` function outside `initium`
   may not carry `poscit alloc`.
4. **No recursion**, so stack depth is statically bounded. Mutual recursion is
   detected in the call graph, not merely direct self-calls.
5. Every buffer has a compile-time-known capacity.

### 4.2 Control flow

6. **Every loop carries a bound** — either a compile-time constant or an
   explicit declared maximum. An unbounded loop is a diagnostic, not a warning.
7. **All calls are direct.** §7.1 makes monomorphization *"a link-time
   optimization within a compilation closure, never a semantic requirement."*
   In `certus` it is a **requirement**: generics must monomorphize within the
   closure, so no witness-table indirection survives to runtime.
8. **`dyn` is restricted** to a closed, statically-known implementation set, so
   dispatch targets are enumerable for worst-case analysis.
9. **Closures may not capture.** A capturing lambda needs storage; a
   non-capturing one is a function pointer. This also sidesteps §15 #1's
   closure-capture problem inside the profile.

### 4.3 Capabilities

10. `Crudum` is **forbidden**. No raw pointers, no unchecked casts, no manual
    lifetimes.
11. `rete` is **forbidden**.
12. `fortuna`, `horologium`, `archivum`, `ambitus`, `machina`, `Filum` are
    forbidden in the control path, permitted only during `initium` where a
    system genuinely requires them, and each such use is declared and reviewed.
13. `sermo` is forbidden. Locale-dependent behaviour in a control path is
    exactly the CVE class §1 exists to prevent.

### 4.4 Arithmetic

14. **`numeri` (§5.4) must be fully declared.** No defaults, so the numeric
    contract is explicit in the interface rather than inherited.
15. `reassociatio vetita` is **mandatory**.
16. **Trapping arithmetic only.** `+` (trapping) is permitted; `+%` (wrapping)
    and `+|` (saturating) require a declared, reviewed justification per use
    site. Silent wraparound is a defect, not a feature, in this domain.
17. Where a system forbids floating point altogether, `certus` permits an
    integer-and-fixed-point-only restriction as a sub-profile. `[OPEN]` — the
    fixed-point type does not exist.

### 4.5 Interface and closure

18. **The profile is declared in the `ego`:** `profilum certum`.
19. **A `certus` module may depend only on `certus` modules**, transitively.

Rule 19 is the one that makes this practical rather than aspirational. §10.2
already states the whole dependency graph and capability closure compute from
`ego` files alone, with no compilation and no network. Profile conformance
therefore rides the same evaluation: **whole-program profile verification is a
property of the interface graph, computable before a single line is built.**

Contrast with MISRA, where conformance of a third-party library is established
by reading it.

## 5. What the profile needs that does not exist

Stated as required work, not as though it were designed.

- ~~**Diagnostics.**~~ **Done.** §13 gained an `08xx` range for the profile,
  grouped to match this document's own section structure: `EXS-E0801`
  allocation after `initium` and `EXS-E0802` recursion (rules 3–4);
  `EXS-E0811` loop without a statically known bound, `EXS-E0812`
  non-monomorphizable generic and `EXS-E0813` capturing closure (rules 6, 7,
  9); `EXS-E0821` forbidden capability (rules 10–13); `EXS-E0831` undeclared
  `numeri` (rule 14); `EXS-E0841` non-`certus` dependency (rule 19).

  Two things fell out of writing it. **`EXS-E0811` is narrower than rule 6
  suggests** — §8.5 now makes `terminus` part of the loop syntax, so a `dum`
  without one is a parse-level fact and never reaches this code; `EXS-E0811`
  is for a bound that is present but not statically known. And **only
  `EXS-E0831` carries a machine-applicable fix**: §8.3 scopes those to edits
  that are mechanically derivable, and moving an allocation, unwinding
  recursion or dropping a capability all change what the program computes.
  Undeclared `numeri` is the exception because §5.4 already defines the
  defaults, so the fix is writing four lines that were implicit.

  The checker that emits them does not exist. `[UNIMPLEMENTED]`
- **MC/DC coverage instrumentation.** DAL A requires modified
  condition/decision coverage. This is compiler support and is very painful to
  retrofit — it should be designed while the CST and backend are being built,
  not after.
- **Loop-bound syntax.** Rule 6 needs surface syntax that does not exist.
- **Contracts.** SPARK's core value is pre/postconditions and invariants that a
  prover consumes. Exsecutor has none. Whether `certus` should grow them is the
  largest open design question here, and adding them is a language change, not
  a profile restriction. `[OPEN]`
- **A call-graph analysis** for rules 4 and 7.

## 6. What certification would still require

The profile is a precondition, not a certification. Being honest about the gap:

- **Tool qualification (DO-330).** Any tool whose output is not independently
  verified must itself be qualified, at a Tool Qualification Level derived from
  the software level and the tool's role. A new compiler has no qualification
  evidence and no service history. **This is the dominant cost and it is not
  close** — it is why safety-critical work runs decades-old toolchains.
- **Formal semantics.** Higher assurance increasingly expects them; this
  project has prose. A formal operational semantics for the `certus` subset
  alone would be far more tractable than for the full language, which is
  another argument for defining the subset early.
- **Requirements traceability**, bidirectional, from requirement to code to
  test. §-numbering and §13's registry are unusually good starting material.
- **Service history or exhaustive verification.** There is no shortcut.

Mapping profile rules onto specific DO-178C objectives, or onto MIL-HDBK-516
airworthiness criteria, requires a qualified authority. This document does not
attempt it and should not be read as claiming any level.

## 7. What Exsecutor brings that is genuinely unusual here

Worth stating because it is the reason to bother:

- **Reproducible builds (§9.3)** — byte-identical output across directories,
  times, locales and hostnames. Configuration-management and
  build-reproducibility objectives are normally argued; here they are measured.
- **`make audit`** — "this binary contains no socket-family syscall" is
  extracted from the artifact, not asserted by a reviewer.
- **Capability closure (§10.3)** — the authority of every transitive dependency
  is enumerable, which is ordinarily a manual review activity.
- **No build scripts (§9.4)** — no arbitrary code execution during build.
- **Licensing.** GPL compiler with the output exception
  (`LICENSE.EXCEPTION` A) means a contractor's deliverable carries no copyleft.
  This is GCC's arrangement and is why GCC is usable in this domain at all.

These are artifact properties where the industry norm is process claims. That
is the actual pitch, and it is worth more than the subset.

## 8. Status

Nothing implemented. No compiler exists. No profile checker exists. No rule
here has been enforced against a single line of Exsecutor source, because there
is no Exsecutor source. `[UNTESTED]`

Do not describe this profile as making the language certifiable. It makes the
language *not obviously uncertifiable*, which is a different and much smaller
claim, and the correct one today.
