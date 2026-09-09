# 0006 — GPL-3.0-or-later, with an output and runtime exception

**Status:** Accepted, 2026-09-09
**Relates to:** `LICENSE`, `LICENSE.EXCEPTION`, [0003](0003-assembler-fasmg.md) (the vendored BSD-3 component), spec §6, §9.2, §11

## Context

The project had no license. An unlicensed public repository grants no rights at
all, so this needed settling before any code beyond the toolchain landed and
before any outside contribution accrued.

The requirement, as stated: GPL, in the manner FAUST does it — copyleft on the
compiler, with compiler output and the runtime explicitly *not* infected.

That pattern is standard for compilers and code generators, and exists because
the naive reading of the GPL would be disastrous for one: if output were a
derivative work of the compiler, every program compiled with `exsc` would be
forced to GPL. No one would use it. GCC solved this with the Runtime Library
Exception, Bison with a parser-skeleton exception, FAUST by stating outright
that generated code is not covered.

## Decision

**GPL-3.0-or-later for the compiler**, plus a written additional permission
under GPLv3 section 7 (`LICENSE.EXCEPTION`) granting that:

- Compiler Output — emitted C, assembly, object code, executables, `ego` files
  — may be propagated under any terms.
- A work combining the user's program with the **target runtime** may be
  propagated under any terms, however combined: static, dynamic, source
  inclusion, or emission by the compiler itself.

The exception explicitly does *not* permit relicensing a modified compiler, or
lifting the target runtime into a different compiler to escape that compiler's
own terms.

### v3, not v2

FAUST is GPLv2-or-later. This project takes **v3**-or-later, because the
precedent that actually matters here is GCC's — GCC's Runtime Library Exception
is drafted as a section 7 additional permission, and section 7 is a GPLv3
construct. v2 has no equivalent framework; the same grant under v2 has to be
bolted on as bare prose. v3 also brings the patent grant and clearer
anti-tivoization terms, both of which suit a compiler. `-or-later` keeps the
door open.

### The distinction the exception spells out

The word "runtime" names two different things in this repository, and
conflating them would be a licensing bug in either direction:

| | what it is | covered? |
|---|---|---|
| `compiler/x86_64/rt/` | the **compiler's own** internals — arena, vec, insertion-ordered map, interner, syscall wrappers. Linked into `exsc`, never emitted, never linked into a user program. | GPL, **no exception** |
| the **target runtime** | ARC and refcount support (§6), and `norma.*` modules the compiler links or emits into a compiled program | **excepted** |

No target runtime exists yet — §6's memory model is specified but nothing is
emitted into user programs, because there is no backend. The exception is
written forward deliberately: settling this before the code lands is cheap,
and renegotiating it afterwards, once contributors hold copyright in it, is
not.

## Consequences

**Positive**

- The compiler stays free, and improvements to it must come back. That is the
  point of choosing copyleft over a permissive license for this component.
- Adoption is unobstructed. Writing proprietary software in Exsecutor is
  explicitly fine, which is a precondition for §17's "it never gets users"
  failure mode not being self-inflicted.
- The position is settled while the copyright holder set is one person.
  Relicensing a compiler after contributors accumulate requires unanimous
  consent and is how projects get stuck.
- `vendor/fasmg-x86/` (BSD-3-Clause) is GPL-compatible, so the combined work
  distributes cleanly with no conflict.

**Negative**

- Copyleft on the compiler will deter some corporate contribution. Accepted.
- A bespoke exception has no SPDX identifier, so automated license scanners
  will report plain `GPL-3.0-or-later` and miss the grant. Anyone auditing a
  downstream binary must read `LICENSE.EXCEPTION` rather than trust a tool.
  `flake.nix` names the base license and points at the exception in a comment
  for this reason.
- **This text has not been reviewed by a lawyer.** It was drafted against the
  named precedents and says so in its own section 5. If the project's licensing
  position ever matters commercially, counsel should read it first. Recorded as
  a known limitation rather than glossed.

**Neutral**

- `-or-later` means a future FSF version could apply. That is the usual trade
  and is deliberate.
- The specification document is covered by the same license as the rest of the
  repository. Documentation under GPL is slightly unusual but harmless here,
  and avoids introducing a second license for one file.
