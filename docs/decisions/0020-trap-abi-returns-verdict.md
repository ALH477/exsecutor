# 0020 — the trap ABI returns a verdict: `exsrt_abortus` is no longer `_Noreturn`

**Status:** Proposed, 2026-10-11. Not yet accepted. Supersedes no ADR; it
amends the trap ABI that `docs/design/runtime.md` §2.5 and
`docs/design/c-backend.md` D1 describe, and the §6.6 sentence that says a
runtime abort is `abortus N` on fd 2 then `SIGILL`.
**Relates to:** Oligarchy `docs/exsecutor-kernel-roadmap.md` B.3 (K2 item 3);
`exsecutor-for-linux` `src/exsrt_abortus.c`; spec §6.6, §8.8, §13;
`compiler/x86_64/backend_c/prologue.c.in`; `compiler/x86_64/backend_c/emit_c.inc`
`__bfc_emit_chk` / `__bfc_emit_trap`; `compiler/x86_64/backend_fasmg/emit.inc`
`__bfa_emit_trap`; `tests/c/exsrt_shim.c`.

## Context

K2 item 3 (Oligarchy's kernel roadmap B.3) names the blocker: every overflow
and bounds check in an emitted unit calls `exsrt_abortus`, whose prototype is
`_Noreturn void exsrt_abortus(unsigned kind)`. An LSM hook must fail closed
by **returning** `-EPERM`; the kernel has no `longjmp`, and a `_Noreturn`
callee cannot return anything. The roadmap lists three options: a provably
trap-free evaluator, a trap that panics the machine, or a specification change
to a returning error path. This ADR is the third option.

The cost of the current ABI is not hypothetical: `exsecutor-for-linux`'s
`src/exsrt_abortus.c` is a `BUG()` — a trap in an LSM panics the machine,
which is fail-closed only in the sense that a crashed kernel is a stopped
kernel. That is not acceptable for a loadable module.

The returning ABI is a *widening* of what the host must supply, not a
narrowing of what the emitted unit does. The emitted unit's contract changes
from "one import, `_Noreturn`" to "one import, returns `int`". A host that
cannot return (the userspace shim, the reference backend's prelude) keeps
its current behaviour: log and trap. A host that can return (the kernel
shim) returns `-EPERM`.

## Decision

1. **The trap hook's prototype changes.**

   ```c
   int exsrt_abortus(unsigned kind);
   ```

   It returns a verdict. The return value is a signed `int` so the kernel
   shim can return `-EPERM` (or any negative errno) directly. The userspace
   shim ignores the return value's meaning and traps as before — it never
   returns, which satisfies the caller's expectation that control does not
   continue past a trap.

2. **The emitted unit's contract is: a trap site calls `exsrt_abortus(kind)`
   and, if it returns, returns 0 from the enclosing function.** Every trap
   site in both backends gains a `return 0;` after the call. The value 0 is
   the "trap was absorbed" verdict: it is the identity for the evaluator's
   decision table (a 0 entry means "admit"), so a host that absorbs a trap
   by returning admits the operation, which is the fail-open direction. A
   kernel shim must therefore NEVER return 0 from `exsrt_abortus`; it
   returns `-EPERM` (fail closed) or panics (fail stopped). The 0-after-trap
   path exists so the emitted C compiles and links against a returning host;
   it is not a sanctioned runtime path.

3. **The trap kinds are unchanged.** Kind 1 (numeric: overflow, shift,
   zero divisor, failed `chk`) and kind 5 (`terminus`) keep their numbers.
   A new kind is NOT added for "kernel refusal": the kind names WHAT trapped,
   not what the host should do about it. The host's policy (return `-EPERM`,
   panic, log-and-continue) is the host's, keyed on the kind if it wants.

4. **The reference backend's trap is unchanged.** `__bfa_emit_trap` in
   `emit.inc` still emits `mov edi, K; jmp exsrt_abort` into the prelude's
   `exsrt_abort`, which drains and `ud2`s. The reference backend is a
   whole-program artifact; it has no host that can return a verdict, so
   its trap ABI is unchanged. Only the C backend (library mode) changes.

5. **The C backend's prologue declares the new prototype.**

   ```c
   int exsrt_abortus(unsigned kind);
   ```

   Every `exsrt_abortus(1)` call site in `prologue.c.in` (the `exsi_*`
   arithmetic helpers) and in the emitter's inline sites (`__bfc_emit_chk`,
   `__bfc_emit_trap`) gains a `return 0;` after the call. The helpers are
   `static inline uint64_t`, so `return 0;` is well-typed. The emitter's
   inline sites are inside emitted function bodies, whose return type is
   the function's declared result (always `uint64_t` in the current calling
   convention); `return 0;` is well-typed there too.

6. **The userspace shim's `exsrt_abortus` keeps its `_Noreturn` body but
   drops the `_Noreturn` qualifier on the declaration.** `tests/c/exsrt_shim.c`
   declares `int exsrt_abortus(unsigned kind)` and its body still writes the
   `abortus N` line and calls `__builtin_trap()`. A `_Noreturn` function can
   be assigned to a `int (*)(unsigned)` pointer type's call site (the call
   simply never returns), but the DECLARATION the emitted unit sees must
   match: the prologue says `int exsrt_abortus(unsigned kind);`, so the shim
   must not say `_Noreturn` on the same symbol. The shim's body is unchanged
   in behaviour.

7. **The kernel shim returns `-EPERM`.** `exsecutor-for-linux`'s
   `src/exsrt_abortus.c` changes from `BUG()` to `return -EPERM;`. The
   `pr_err` log line stays (it is the one diagnostic a trap leaves behind).

8. **The emitted unit's import surface is unchanged in name.** The one
   symbol library mode imports is still `exsrt_abortus`. Its type changes
   from `_Noreturn void (unsigned)` to `int (unsigned)`. `nm -u` on an emitted
   object shows the same single undefined symbol. The kbuild-shape gate's
   import-surface check is unaffected.

9. **No new EXS-E diagnostic code.** A trap is a runtime event, not a
   compile-time diagnostic (§6.6, §13). The ABI change is a runtime contract
   change, not a language change. No spec §13 row is added.

10. **Spec §6.6's sentence is amended.** The current text says: "A runtime
    abort has one observable shape: `abortus N` on fd 2, then `SIGILL`."
    It becomes: "In whole-program mode a runtime abort has one observable
    shape: `abortus N` on fd 2, then `SIGILL`. In library mode (the C
    backend) the trap hook `exsrt_abortus(kind)` RETURNS a verdict; the
    emitted unit returns 0 from the enclosing function if it does, and the
    host decides whether 0 means admit or the host never returns 0." The
    SIGILL shape is the whole-program mode's shape, not the only shape.

## Consequences

- **The kernel stub can fail closed without panicking.** K2 item 3 is closed:
  an LSM hook's trap path returns `-EPERM` up the call chain, the emitted
  function returns 0 (admit), and the LSM's own logic sees the 0 and can
  override it to deny. The evaluator's table is the fast path; the LSM's
  verdict is the final word. This is the design the roadmap asked for.
- **The userspace behaviour is unchanged.** The reference backend's prelude
  `exsrt_abort` still drains and `ud2`s. The C backend's shim still writes
  the `abortus N` line and `__builtin_trap()`s. Tests that check for
  `shape=abort` (SIGILL, exit 132, the line on fd 2) are unaffected.
- **The C backend's emitted C gains a `return 0;` at every trap site.**
  This is dead code against the userspace shim (which never returns) but
  live against a returning host. It is well-typed and costs one instruction
  per trap site.
- **A host that returns 0 from `exsrt_abortus` admits the trapped operation.**
  This is fail-open by construction. The kernel shim must not do it. The
  contract is: 0 means "I absorbed the trap"; the evaluator's caller (the
  LSM) must treat a 0 return from the emitted function as "the table said
  yes", which is correct only if the trap was genuinely absorbed by a policy
  decision, not by a bug in the shim.
- **The kbuild-shape gate's import check is unaffected** (same symbol name).
  The kbuild-shape gate's frame-budget check is unaffected (the `return 0;`
  adds no stack). The byte-identical re-emission check WILL fire: the
  prologue and the emitter change, so `src/custos.c` must be regenerated.
- **The differential test is unaffected.** The shim's `exsrt_abortus` never
  returns, so the `return 0;` after it is dead code in the differential
  build. The differential phase's exit-code checks are unchanged.

## Open

- The LSM's policy for a 0 return from the emitted function (admit vs deny)
  is a K3 design question, not a K2 question. This ADR only makes the
  returning ABI possible.
- A negative return other than `-EPERM` (e.g. `-ENOMEM` for an allocation
  trap) is a future refinement; kind 4 (arena exhausted) is the candidate.
  Not in this ADR.
- The reference backend's `exsrt_abort` could gain a returning variant for
  symmetry, but no consumer needs it. [OPEN].
