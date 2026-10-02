# examples/abortus/

**A trap that returns, for a hosted host.** A library-mode unit
(`exsc --emitte c`, `docs/design/c-backend.md` D1) imports one symbol,
`_Noreturn void exsrt_abortus(unsigned kind)`. Every bounds, overflow and
`terminus` trap calls it. Every host in `examples/` defines it as "print and
abort", so a trap ends the whole process. That is the right default, and
it is wrong for two consumers:

- **Punctim's `dcf-ws-bridge`** links `custos` into a long-running relay.
- **Oligarchy's reliquary** links `arca` and runs as root.

In either one, an input that reaches a trap kills the host. That is a
denial of service, and both units have traps that a host is one missing
check away from:

- `arca`'s `saltus(magnitudo(h))` overflows on the sentinel that
  `magnitudo` returns for a malformed size field. `m + 511` traps, kind 1.
- `custos`'s `redundantia_sarcinae(d, n)` reads `d[32]` when `n > 32`.
- `arca`'s `nomen_iudica(b, n)` reads `b[4096]` when `n > 4096`.

`tutela.c` defines `exsrt_abortus` so that a trap inside a guarded call
**returns the trap's kind to the host**, and the host keeps running. With
no guard open it still prints `exsecutor: abortus N` and aborts. No
compiler change is involved; the emitted units are byte-for-byte what
`examples/custos/` and `examples/arca/` already ship.

## The pattern

`tutela.h` is the API. It has one function that holds the program's only
`setjmp`:

```c truth:ignore
unsigned exs_tutela_curre(exs_opus *opus, void *ctx);   /* 0, or the kind */
```

A host wraps each exported function it calls in two pieces:

- a **thunk** that unpacks a context struct and makes the raw call;
- a **wrapper** that runs the thunk under the guard.

`exempla.c` has eight wrappers, one per function the tests call. Here is
one:

```c truth:ignore
struct ctx_admitte { unsigned char *d; uint64_t n; uint64_t r; };
static void opus_admitte(void *p)
{ struct ctx_admitte *c = p; c->r = exs_admitte(c->d, c->n); }
unsigned custos_admitte_tutum(unsigned char *d, uint64_t n, uint64_t *r)
{
  struct ctx_admitte c = { d, n, 0 };
  unsigned k = exs_tutela_curre(opus_admitte, &c);
  if (k == 0) *r = c.r;
  return k;
}
```

A Rust host declares the wrapper `extern "C"` and maps the result to
`Result<u64, u32>` (see `proba.rs`).

How the guard works:

- The guard is a `jmp_buf` in `exs_tutela_curre`'s frame, linked into a
  per-thread chain.
- `exsrt_abortus` unlinks the innermost guard, stores the kind in it, and
  `longjmp`s back to it.
- Guards nest, so a trap goes to the innermost one.
- Each thread has its own chain.

## Why it is sound

A `longjmp` is defined when it abandons frames that hold nothing needing
release, and when it obeys C11's three `setjmp` rules. Each part is argued
below, and each is measured where it can be.

**1. What the jump abandons.** Between `exs_tutela_curre` and
`exsrt_abortus` there are only three kinds of frame: the thunk, the
emitted Exsecutor functions, and `exsrt_abortus` itself. In library mode
an emitted function cannot hold a resource:

- **No allocation and no host call.** `proba_c.sh` checks each unit's
  imports with `nm -u`. They are `exsrt_abortus` plus `__stack_chk_fail`
  (distribution GCC's default stack protector), and nothing else.
  `memcpy`/`memset` are also admitted, as a compiler's own lowering. A
  unit that allocated would import an `exsrt_alloc_*`, and library mode
  defines none (c-backend.md D6).
- **No reference counting.** `retain` and `release` are refused by name
  in library mode (c-backend.md D4 row 49): no object header exists.
- **No mutable global.** The only file-scope objects the backend emits
  are `static const` byte arrays (`program_c.inc`). There was nothing
  else in the three units used here.
- **No VLA.** Every array bound in emitted text is a decimal literal
  (`emit_c.inc` prints it with `__bfa_out_u64`). C11 7.13.2.1's VLA clause
  therefore never applies.
- **No floating-point state change.** The emitted code never calls
  `fesetround` and never writes MXCSR (c-backend.md D2). `longjmp` does
  not restore the FP environment, and nothing changed it.
- **The stack protector is unaffected.** A frame abandoned by `longjmp`
  never reaches its epilogue, so its canary is never checked. The guard's
  own frame is intact.

**2. C11's `setjmp`/`longjmp` rules**, applied in `tutela.c`, where the
comments cite them line by line:

- *7.13.1.1p4.* `setjmp` appears only as an operand of `== 0` in an `if`.
- *7.13.2.1p2.* The jump goes to a live frame on the same thread. A guard
  is unlinked before its frame returns (on both paths), and the chain is
  thread-local. A guard found out of place at return stops the process,
  rather than leaving a dangling `jmp_buf` for a later trap.
- *7.13.2.1p3.* The only automatic object of the `setjmp` caller that
  changes between `setjmp` and `longjmp` is the kind. It is `volatile`.
  The chain head is `_Thread_local`, which is static storage and not
  covered by the rule.

Wrappers and thunks contain no `setjmp`, so this argument is made once and
not once per wrapper. `tutela.c` is compiled on its own, and GCC also
refuses to inline a function that calls `setjmp`.

**3. What the host may rely on afterwards.** `exsrt_abortus` is an
external call. The C compiler must therefore perform every store that
precedes it, because the callee could read the memory. It performs none
that follows it. So caller-owned memory written through `&mutabilis`
(ADR 0016) holds exactly the stores made before the trap. The test shows
this with eight bytes written, then the ninth store refused, with guard
bytes past the end untouched. That is defined, but it is not useful: the
host must treat any output buffer as garbage after a nonzero return (P3
in `tutela.h`). The unit itself keeps no state, so the next call starts
clean. The test re-runs custos's anchors after each trap to show this.

**4. No Rust frame is crossed.** Rust calls the C wrapper, and the wrapper
returns to Rust by an ordinary `return`. The jump crosses only C frames,
so no Rust frame is skipped and nothing of Rust's is unwound.

**This depends on a precondition (P1).** If Exsecutor code ever calls
host code through `externus`, and that host code calls back into
Exsecutor, the callback must guard its own call. The inner guard then
catches the trap before the jump could cross the host's frames. Nested
guards are tested. No running program uses `externus` today (Oligarchy's
kernel roadmap, B.5), so P1 holds vacuously for now.

What is **not** shown:

- **The `volatile` on the kind is argued, not tested.** Removing it
  produced no failure at `-O0` or `-O2` under either compiler (measured
  2026-10-02, without sanitizers). C11 permits that outcome, so it proves
  nothing.
- **Calling a guarded function from a signal handler** is `[UNTESTED]`.
- **A libc other than glibc** is `[UNTESTED]`. musl's `setjmp` is a
  different implementation.
- **CET shadow stacks** are `[UNTESTED]`. glibc's `longjmp` is written to
  unwind the shadow stack, and nothing here ran with one enforced.

## Checks: `proba_c.sh` (after `make all`)

1. Emits `custos` (with entry 23's two files, as `examples/custos/` does),
   `arca`, and the test-only `probatio.exsc`, each twice, and requires the
   two outputs to be byte-identical (31,948, 63,400 and 23,105 bytes).
2. Lists each unit's imports (P2, above).
3. Builds the units, `tutela.c`, `exempla.c` and `proba.c` with every C
   compiler given, at `-O0` and `-O2`, with `-Wall -Wextra -Werror` (so
   GCC's `-Wclobbered` is on) and under UBSan. It adds one GCC build under
   ASan + UBSan, since ASan has its own `longjmp` handling. `custos.h` and
   `arca.h` are force-included into `exempla.c`, so a wrapper whose
   prototype drifts from the unit is a compile error. `proba` runs 25
   checks:
   - the real units answer correctly through the guard;
   - the three host-reachable traps above each return kind 1, with
     `*r` untouched;
   - after each trap the chain is empty, and custos's anchors are right
     again;
   - `terminus` returns kind 5, not 1;
   - a trap 1,001 frames deep returns, and the next call works;
   - the `&mutabilis` prefix state described in point 3 above;
   - nested guards;
   - 1,000,000 traps interleaved with 1,000,000 good calls, with resident
     memory growing by less than 5 bytes per trap. Anything kept per trap
     would be at least a 200-byte `jmp_buf`.
   - eight threads trapping concurrently.

   `proba nudus` traps with no guard open. It must print
   `exsecutor: abortus 1` and die of SIGABRT, so the fail-stop default
   is kept.
4. Applies three mutants of `tutela.c`. Each must fail `proba` at the
   check that names what it broke:
   - guard ignored: the first trap aborts;
   - kind dropped: the trap reads as success;
   - guard not unlinked: the chain check fails, before the dangling guard
     crashes the process.
5. **Not in a kernel.** It compiles with `-ffreestanding -nostdinc` and
   only the compiler's own header directory. All three units compile.
   `tutela.c` is refused because `<setjmp.h>` is not found: it is not a
   C11 freestanding header (C11 4p6). This host's GCC (Ubuntu 13.3.0)
   needs `-D_LIBC_LIMITS_H_` for the units to compile this way, because
   its `<limits.h>` recurses into libc's.
6. **The Rust host**, when `rustc` is on `PATH`: `proba.rs` is linked by
   plain `rustc` against a static library of the same objects. It checks
   `Err(1)` from both custos and arca, the chain empty afterwards, custos
   still admitting, and four threads trapping concurrently.

Results on this host, 2026-10-02, with gcc 13.3.0, clang 18.1.3 and
rustc 1.97.0:

- all five C builds: 25/25, and the unguarded trap aborts;
- all three mutants caught;
- both compilers refuse `tutela.c` freestanding;
- Rust: 7/7.

**Cost**, measured once on this host and noisy (a shared 4-core VM; not a
gate), at gcc `-O2`, 20,000,000 calls each:

- a guarded call adds about 6 ns over a raw one: `saltus` 2 ns raw, 8 ns
  guarded;
- a trap and return costs about 30 to 55 ns;
- `admitte` (about 160 to 200 ns raw) showed no stable difference.

## Why the wrappers are not generated by `exsc`

Three reasons:

1. **They cannot go in the unit.** `<setjmp.h>` is hosted. A unit that
   included it would stop compiling for Kiln's N64 ROM and the bare
   firmware rows (c-backend.md C4), which is check 5 above.
2. **A generated hosted companion file would be a runtime.** Spec §9.2
   says library mode has "no runtime". Having `exsc` emit one is a change
   to §9.2, and to §12 for the new `--emitte` value. That deserves its own
   review, not a rider on this example.
3. **The policy is the host's.** Whether a trap is an error code, a log
   line or a process restart differs between the bridge and reliquary. A
   wrapper is six lines given `tutela.c`.

If a third consumer needs this, generating `exempla.c` from the prototype
block every unit already prints is the obvious next step `[OPEN]`.

## Not in a kernel

None of this works in a kernel:

- there is no `setjmp` (check 5);
- objtool's stack validation would reject the control flow even on an
  architecture that had one;
- "fail closed" in an LSM means **returning** `-EPERM` from the hook.

What a kernel needs is for the compiler to lower a trap as a return. That
is `docs/design/returning-trap.md` (`[OPEN]`, design only). This example is
that design's oracle: a returning build must give the same kind and leave
caller memory in the same state as a guarded build of the same unit.
