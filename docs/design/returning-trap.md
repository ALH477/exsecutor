# A trap that returns — the kernel's half

**Status:** `[OPEN]` — design only. No compiler change exists. The hosted
half is built and tested: `examples/abortus/` (2026-10-02) gives a hosted C
or Rust host a returning trap without touching the compiler, using
`setjmp`/`longjmp`. This document is about the case where that cannot work.
**Relates to:** spec §5.4 (traps), §8.5 (`terminus`), §9.2 (library mode),
§12; ADR 0012; `docs/design/c-backend.md` D1, D4; `docs/design/ssa-ir.md`
section 2.9; `docs/design/profile-certus.md`; `compiler/x86_64/prelude/README.md`
(the abort kinds); Oligarchy's `docs/exsecutor-kernel-roadmap.md` B.3 and
phase K2 item 3.

## 1. The problem

A library-mode unit calls `_Noreturn void exsrt_abortus(unsigned kind)` on
every trap. A host that must not die has two choices:

| host | what it can do | status |
|---|---|---|
| hosted C, Rust, C++ through a C shim | `setjmp` in a guard, `longjmp` from `exsrt_abortus` | **built**, `examples/abortus/`: gcc and clang at `-O0` and `-O2` under UBSan, gcc under ASan, a Rust host, three mutants caught |
| a kernel, an LSM above all | none | this document |

The kernel cannot use the hosted pattern, for three reasons:

- **There is no `setjmp`.** `<setjmp.h>` is not a C11 freestanding header,
  and the kernel provides no general one. `examples/abortus/proba_c.sh`
  shows the guard refused under `-ffreestanding -nostdinc` while the units
  compile.
- **objtool would reject it.** Kbuild runs objtool's stack and control-flow
  validation on every object, and a non-local jump is what it is built to
  reject `[UNTESTED]`: no Exsecutor object has been through objtool.
- **An LSM fails closed by returning.** A hook returns `-EPERM`. A trap
  that ends anything (the process, the machine, or a jump to some other
  frame) is not that.

The roadmap names the three ways out:

1. a provably trap-free evaluator;
2. a trap that panics the machine;
3. a specification change to a returning error path.

Option 2 is not acceptable for a policy decision function. Option 1 is the
`certus` profile's territory, and its no-trap discipline is a checker that
does not exist. That leaves option 3, proposed here.

## 2. Why returning is cheap in library mode

The hosted guard's soundness argument (`examples/abortus/README.md`, "Why
it is sound") is the whole reason a returning lowering is small. A trap in
library mode abandons frames that hold:

- no allocation;
- no reference count;
- no lock;
- no mutable global;
- no VLA;
- no changed floating-point state.

Abandoning those frames by `longjmp` and unwinding them by `return` leave
the same observable state. So a returning lowering needs no cleanup code
anywhere, only a way to get the kind out. Both mechanisms abandon the same
frames at the same point. That gives the returning lowering an oracle
before it exists:

> For every unit and every input, the returning build and the
> setjmp-guarded build of the same unit report the same trap-or-not, the
> same kind, and leave caller-owned memory byte-identical.

That is ADR 0012's differential test with a third column. Section 5 has
how it would run.

## 3. Proposal: a returning lowering in the C backend

**R1. A per-build choice, not a mode of the language.** Spec §7.2 says
modes may never change representation. The source does not change, and
neither does any Exsecutor-level type. What changes is the C face of each
exported function. Today's `_Noreturn` hook stays the default. The choice
is a backend flag. Its spelling is `[OPEN]`, and it would be named in §12
and §9.2 by the amendment that adopts it. No new `EXS-E` code is needed,
because a trap kind is not a diagnostic (`prelude/README.md`, "Abort
kinds").

**R2. The internal convention: a status pointer, threaded.** Every emitted
function takes one hidden trailing parameter, `unsigned *exs_g`. The rule
is:

| at | today | returning |
|---|---|---|
| `chk i n` | `if (i >= n) exsrt_abortus(1);` | `if (i >= n) { *exs_g = 1; return 0; }` |
| `trap terminus` | `exsrt_abortus(5);` | `{ *exs_g = 5; return 0; }` |
| a trapping helper (`exsi_add_u`, `exsi_shl_u`, `exsi_div_i`, …) | calls `exsrt_abortus(1)` | returns a flag the call site tests, or takes `exs_g` and the call site tests `*exs_g` after it |
| a call to another emitted function | `v = exs_f(a, b);` | `v = exs_f(a, b, exs_g); if (*exs_g) return 0;` |
| an aggregate return through the hidden `ptr` | unchanged | unchanged; the caller must not read it when `*exs_g != 0` |

The return value on the trap path is a dummy. `return 0;` in a `void`
function is plain `return;`. The status pointer is a trailing parameter,
not the first, so the order of section 2.9's four groups is untouched for
every existing parameter. The pointer is appended after group (4) and
belongs to no group. A `chk` is a statement in the emitted C today, and
`exsi_*` helpers are `static inline`. So every trap point is already a
place where a statement can return. No expression has to be split, except
the trapping helpers, whose call sites are each one complete statement (D4:
one IR op is one C statement).

**R3. The exported face.** Each `publica` function `exs_f(args) -> T` gets
a public wrapper, emitted by the backend:

```c truth:ignore
unsigned exs_f_tutum(args, T *out);   /* 0 and *out written, or the kind */
```

The wrapper declares `unsigned g = 0`, calls the internal `exs_f` with `&g`,
and writes `*out` only when `g == 0`. An LSM's hook is then:

```c truth:ignore
if (exs_politia_tutum(subj, obj, op, &verdict) != 0) return -EPERM;
```

**R4. What the unit imports.** Under the returning lowering, the unit
imports nothing at all: `exsrt_abortus` is never called. This is a change
to §9.2's sentence "the unit imports exactly one symbol", and the amendment
must say so. `nm -u` then lists only what the C compiler adds, such as
`memcpy` and `memset`.

**R5. `externus`.** A foreign function called from Exsecutor cannot trap
into Exsecutor's status, so it needs no change. The reverse, foreign code
calling an exported function, goes through R3's wrapper. That is exactly
the nested-guard case the hosted pattern tests.

## 4. Costs and what is open

**Cost.** One load, compare and branch per call site and per trap point.
Most trap points already end in a compare and branch (`chk`, the overflow
predicates); the change is the target. The hosted guard costs about 6 ns
per guarded *call* on the host it was measured on
(`examples/abortus/README.md`). That is the figure to beat. The returning
lowering's cost per *call site* is unmeasured `[UNTESTED]`.

**Open:**

- **The reference backend.** ADR 0012 makes fasmg's output the definition.
  Two answers are possible:
  - the reference grows the same lowering. A carry flag on return is the
    natural shape, per `docs/asm-conventions.md`'s `CF`/`eax` channel.
  - the returning lowering is declared a C-only property, held to the
    reference by the oracle in section 2. Trap-or-not and the kind are the
    reference's observables; *what the host sees afterwards* is not one
    the reference has.

  The second needs no reference change and is the proposal's lean.
  `[OPEN]` until reviewed.
- **Whether the hidden pointer is per call or per thread.** Per call (R2)
  needs no thread-local storage, which a kernel's atomic context makes
  awkward. It also costs one argument register. A per-CPU or per-task slot
  is the alternative and is not proposed.
- **Interaction with `certus`.** A profile that proves no trap reachable
  makes this lowering unnecessary for the functions it covers. The two are
  complementary, not alternatives: the proof is the strong claim, the
  returning lowering is the fail-closed backstop for what the proof does
  not cover.
- **The kernel's other gates.** These are unaffected by this proposal and
  each blocks a kernel object on its own:
  - roadmap B.1: the float prologue;
  - roadmap B.2: frame size. `arca`'s `caput_iudica` is 4,160 bytes at gcc
    13 `-O2`, of which 4,096 is a name copy that
    `docs/design/length-generics.md` would remove;
  - roadmap B.4: concurrency.

## 5. How it would be retired

1. A `tests/run.sh` differential leg: for every program directory and IR
   fixture already eligible for the C differential phase, emit the
   returning build. Link it with a shim whose `main` calls each exported
   `_tutum` wrapper. Require the same three observables as the reference.
   For a trap, the shim prints `exsecutor: abortus N` and exits like the
   reference does.
2. The `examples/abortus/` oracle: `proba.c` rebuilt against the returning
   units' `_tutum` wrappers must report the same 25 results, including the
   `&mutabilis` prefix state.
3. A kernel row: an emitted unit built by Kbuild as an out-of-tree module
   with `-mno-red-zone -mcmodel=kernel` and objtool, which loads and returns
   `-EPERM` on a forced trap. This is phase K2's evidence, and it needs
   B.1 and B.2 first.
