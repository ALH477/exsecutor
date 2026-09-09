# ADR 0002: Host language — freestanding x86-64 assembly

**Status:** Accepted — 2026-09-09

## Context

`exsc` needs a host language. §9.3 sets an unusually strict bar for that
choice: the compiler must be a pure function of `(source, ego, lockfile,
flags)` — no environment reads outside an explicit `--env KEY=VALUE`
allowlist, no `setlocale`, no `$HOME` or dotfile discovery, **no network
access, ever, at any phase**, no implicit clock, byte-identical output for
identical input across directories, times, locales, and hostnames. §1 makes
the same demand of the *language* `exsc` compiles; the natural question §18
answers is whether the compiler holds itself to it.

Most host languages can be *disciplined* into meeting §9.3 — avoid the ambient
calls, review for them, lint for them. That discipline is only as strong as
the review that enforces it, and it degrades as the codebase and contributor
count grow. This ADR is the fuller record behind the terse decision in spec
§18: what it buys, what it costs, and what it forecloses. The costs below were
shown before the decision was made and reaffirmed, not discovered after.

## Decision

`exsc` is implemented in **freestanding x86-64 assembly**: no libc, no dynamic
linking, direct syscalls only, through the allowlist and single-gateway
convention (`rt/sys.inc`) that CLAUDE.md fixes and `docs/asm-conventions.md`
documents.

## Why (§18.1)

- **§9.3's purity contract stops being a discipline and becomes a property of
  the artifact.** A freestanding static binary cannot call `setlocale`, read
  `$HOME`, or discover a dotfile, because the code to do so is not linked in —
  there is nothing to review for, because there is nothing there. "No network
  access, ever, at any phase" becomes checkable rather than promised: extract
  every `syscall` site and its `rax` value from the binary and diff against
  the declared allowlist. CI enforces this (`make audit`). No candidate host
  language *with a runtime* offers a guarantee of this shape — the guarantee
  comes from what is **absent** from the binary, and a runtime is exactly the
  kind of thing that is present whether or not the program text calls into it.
- **Byte-identical output (§9.3) is close to free.** Insertion-ordered maps
  (`rt/map.inc`), no allocator nondeterminism, explicit symbol and section
  emission order — all under direct control instead of inherited from a
  language runtime that was never designed against this requirement.
- **The build closure collapses to `{fasmg}`** (ADR 0003) — §1's Nix-native
  claim applied to the compiler's own build, not only to what it produces.
- **§9.2's C backend emits text, not machine code** — among the easier things
  to do directly in assembly. The expensive parts of the pipeline are the
  lossless CST (§9.1) and the Unicode layer (§8.1–§8.2), not codegen; this
  decision does not make the hard 80% of the job harder.

## Consequences

### Positive

As above: an auditable rather than disciplinary purity contract, near-free
determinism, a `{fasmg}`-sized build closure, and cheap C-backend text
emission (§18.1).

### Negative — accepted costs (§18.2)

- **§12's headline demo is dropped.** `exsc aedifica --hospes riscv64-linux`
  on a Mac is impossible once the compiler itself is x86-64 machine code: a
  Mac is aarch64, and `buildPlatform` (§9.5) is now pinned to `x86_64-linux`.
  Cross-compilation to RISC-V is unaffected — that is `targetPlatform` (§9.5),
  a property of what `exsc` emits, not of what it runs on. The nearest
  surviving demo is the same command from an `x86_64-linux` host. §17 names
  never getting users the top failure mode, and §12 named this demo the
  adoption hook, so this is not a minor line item.
- **The LSP is deferred, marked `[OPEN]`.** §12 calls the LSP non-negotiable,
  and that judgement is not reversed here — this is a real amendment against
  §12, not a quiet omission of it. Incremental reparse over a red-green CST
  (§9.1) in hand-written assembly is not Stage 1 work (§16).
- **aarch64 and riscv64 hosts are full rewrites** of the architecture-specific
  tree — there is no shortcut from `compiler/x86_64/` to a `compiler/aarch64/`.
  This is about where `exsc` can **run**. §5.3's minimum ABI coverage (SysV
  AMD64, AArch64 AAPCS, RISC-V lp64d) is about what `exsc` can **target** and
  is unaffected: today's x86-64 `exsc` binary already emits code for those
  targets, it just cannot itself be one.
- **Debugging degrades further.** §12 already names DWARF as the C backend's
  weak point; hand-written assembly improves that story on neither side of the
  pipeline. Mitigated by stage dumps at every §9.1 pipeline boundary (CST,
  typed AST, SSA IR) rather than by interactive debugging.
- **§16's revise-on-measurement loop gets more expensive**, in direct tension
  with this project's own evidence discipline ("prose designs are hypotheses
  until code runs"). Mitigated, not solved: design probes stay in Python
  (`prototypes/`), thrown away, never on the build closure — cheap iteration
  is preserved by keeping it off the one thing this decision just made
  expensive to iterate on.

### Neutral

- Choosing assembly buys none of a host language's ergonomics for free.
  Calling convention, register discipline, and a macro dialect standing in for
  procedures, control flow, structs, and assertions all have to be designed
  rather than inherited. That design is `docs/asm-conventions.md` — a direct
  downstream consequence of this ADR, not an independent decision.
- This ADR fixes the host *language*, not the assembler. ADR 0003 is the
  reason "design a macro dialect from scratch" is a bounded cost rather than
  an open-ended one.

## Alternatives considered

Compared only on the criteria the decision above actually turned on —
purity-contract auditability and build-closure size. This is not a general
language shootout, and no benchmark was run to produce this table.

| host language | purity-contract auditability | build closure | note |
|---|---|---|---|
| C, freestanding (`-ffreestanding -nostdlib`) | Achievable, but the guarantee is a compiler-flag *discipline*, not a structural absence — the standard library is one `#include` away, kept out by convention and review rather than by not being linkable in the first place. | `{cc}` plus whatever scaffolding the freestanding subset needs. | Closest competitor; rejected because the auditability gap is exactly the property §9.3 cares about. |
| Rust / Zig, `no_std` / freestanding | Same shape of gap as C: a large standard library is one import away, kept out by convention and lint. Zig in particular is aligned in spirit — §1.2 already cites Zig's cross-compilation-as-default as prior art. | The compiler used to build `exsc` (`rustc` / `zig`) is itself a large, fast-moving dependency, in tension with the `{fasmg}`-closure goal above. | Not evaluated in fine detail; ruled out primarily on build-closure grounds, secondarily on the same auditability gap as C. |
| GC'd / runtime languages (Go, OCaml, Haskell, …) | The runtime itself is exactly the kind of ambient thing §9.3 is written against (GC threads, runtime-managed allocators and clocks) — auditing "no network syscall" means auditing the runtime, not just the program text. | Runtime plus toolchain: larger than `{fasmg}`. | Rejected early; the runtime is the whole problem here, not an incidental cost. |
| x86-64 assembly (chosen) | Structural: the guarantee is what is *absent* from the binary. | `{fasmg}` (ADR 0003). | This ADR. |

## Related

- ADR 0003 (assembler = fasmg) is the decision that makes this one tractable
  rather than merely possible.
- `docs/asm-conventions.md` is the binding convention set this decision
  requires and that everything above it is written against.
- Spec: §1, §5.3, §8.1, §8.2, §9.1, §9.2, §9.3, §9.5, §12, §16, §17, §18,
  §18.1, §18.2.
