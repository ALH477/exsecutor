# 0006 — GPL-3.0-or-later, with output and linking exceptions

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

**GPL-3.0-or-later for the compiler**, plus two written additional permissions
under GPLv3 section 7 (`LICENSE.EXCEPTION`). They solve different problems and
are kept separate on purpose:

**Exception A — compiler output.** Emitted C, assembly, object code,
executables and `ego` files may be propagated under any terms. Automatic;
covers everything the compiler produces. Modelled on FAUST, the GCC Runtime
Library Exception, and Bison.

**Exception B — linking, Classpath-style.** The GNU Classpath exception as used
by OpenJDK, adapted only in saying "this file" where the original says "this
library". Linking a designated file into an independent module may be
distributed under terms of your choice.

Neither permits relicensing a modified compiler, or lifting a designated file
into a different compiler to escape that compiler's own terms.

### Why the Classpath mechanism, specifically

Exception B attaches to **individually designated files** — a file is covered
if and only if its own header says:

> This particular file is designated as subject to the "Linking" exception as
> provided in the LICENSE.EXCEPTION file that accompanies this code.

Version 1.0 of this document instead described the covered set in prose, as
"the target runtime." That required every future reader to correctly
distinguish two things sharing a word (see below), and a misreading in either
direction is a licensing bug: either the compiler's internals leak an
exception they should not have, or user programs get infected by a runtime
that should have been excepted. OpenJDK's per-file designation cannot be
misread. Nothing is excepted by category, by directory, or by resemblance.

### v3, not v2

FAUST is GPLv2-or-later. This project takes **v3**-or-later, because the
precedent that actually matters here is GCC's — GCC's Runtime Library Exception
is drafted as a section 7 additional permission, and section 7 is a GPLv3
construct. v2 has no equivalent framework; the same grant under v2 has to be
bolted on as bare prose. v3 also brings the patent grant and clearer
anti-tivoization terms, both of which suit a compiler. `-or-later` keeps the
door open.

### The distinction the designation mechanism removes

The word "runtime" names two different things in this repository, and
conflating them would be a licensing bug in either direction:

| | what it is | covered? |
|---|---|---|
| `compiler/x86_64/rt/` | the **compiler's own** internals — arena, vec, insertion-ordered map, interner, syscall wrappers. Linked into `exsc`, never emitted, never linked into a user program. | GPL, **no exception** |
| the **target runtime** | ARC and refcount support (§6), and `norma.*` modules the compiler links or emits into a compiled program | **excepted, per file, by an explicit designation line** |

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
- **Exception A has no registered SPDX identifier.** Scanners will report a
  compiler source file as plain `GPL-3.0-or-later` and miss the output grant,
  so anyone auditing a downstream artifact must read `LICENSE.EXCEPTION` rather
  than trust a tool. (`GCC-exception-3.1` and `Bison-exception-2.2` are
  registered, but neither is this text, and claiming an id for a text you did
  not use would be worse than having none.)

  Exception B does *not* have this problem — adopting the Classpath exception
  verbatim means designated files carry
  `SPDX-License-Identifier: GPL-3.0-or-later WITH Classpath-exception-2.0`,
  which is a registered, machine-resolvable expression. That is a direct
  benefit of using OpenJDK's exception rather than drafting one, and it was not
  true of version 1.0 of `LICENSE.EXCEPTION`.
- **This text has not been reviewed by a lawyer.** Exception B's operative
  paragraph is the Classpath exception, which is widely relied upon; Exception A
  was drafted against the named precedents. `LICENSE.EXCEPTION` says so in its
  own closing section rather than leaving it implied. If the project's licensing
  position ever matters commercially, counsel should read it first. Recorded as
  a known limitation rather than glossed.

**Neutral**

- `-or-later` means a future FSF version could apply. That is the usual trade
  and is deliberate.
- The specification document is covered by the same license as the rest of the
  repository. Documentation under GPL is slightly unusual but harmless here,
  and avoids introducing a second license for one file.
