# The C backend — design for spec §9.2's reach backend, library mode first

Status: **design only; nothing implemented.** No line of `compiler/x86_64/backend_c/`
exists, no C has been emitted, no C compiler has been run on anything this file
describes. Every claim below about what GCC or Clang does is stated from memory
and is `[UNTESTED]` until C1's first task (section 7) runs it. `spec §N` cites
`docs/spec/exsecutor-spec-v0.4.md` as amended in the same commit as this file;
`IR n.m` cites `docs/design/ssa-ir.md`; `RT n` cites `docs/design/runtime.md`;
`WC Dn` cites `docs/design/wire-codec.md`; ADR 0012 is
`docs/decisions/0012-c-backend-fidelity.md`, accepted as design and taken as
given here: the defence ladder (a) ISO C, (b) `#error` / `_Static_assert`,
(c) runtime assert, (d) `exsc` refuses; the four-problems table; the
differential test; the prologue incantations. This file is what ADR 0012
obliges the backend to be, made concrete enough to build against
hand-written IR with no frontend, the way `backend_fasmg/` was.

The first consumer is real: a StreamDB v3 reader that must compile with
`mips64-elf-gcc -march=vr4300 -mtune=vr4300 -mabi=o64` and link into the
Kiln engine (`/home/asher/Documents/M64`, read-only; its `engine/Makefile`
builds `libkiln.a` with `-std=gnu2x -Os`, `streamdb-embedded/` with
`-std=gnu11 -Os`, and `nix/checks/toolchain.nix` fails the build on any
`.d`-suffixed floating-point instruction). Section 6 is that reader's
section; its format survey is another agent's and is not in this file.

## 1. What is being built, and the constraints that shape it

A second emitter over the same IR. `backend_fasmg/{ir,parse,print,verify}.inc`
— 1,562 + 4,038 + 2,342 + 5,187 = 13,129 lines at `dbe1d64` — are reused
unchanged; the include chain is strictly linear (`ir → parse → print →
verify → emit → program`, each file including exactly one other, `ir.inc`'s
header) so `backend_c/emit_c.inc` includes `backend_fasmg/verify.inc` and
`backend_c/program_c.inc` includes `emit_c.inc`, with no diamond. What is
written fresh is the emitter and the unit skeleton — the reference's are
4,623 and 528 lines, which is the size estimate and nothing more.

The constraints, in the order they bind:

1. **The reference defines the semantics** (spec §9.2, ADR 0012). Nothing
   here may lower an opcode the reference does not lower: a C lowering the
   differential test cannot check is a hypothesis with no test, and the
   reference's `__bfa_emit_inst` lowers **37** of the IR's 60 opcodes —
   `param iconst add sub addw subw mul mulw cmp jmp br ret slot gaddr load
   store loadbits storebits call retain release trap adds subs and or xor
   shl shr zext sext trunc index chk copy addr`, plus `phi` at the edges.
   (The survey that preceded this file counted 38; it counted `fcmp`, which
   the dispatcher routes to `__bfa_emit_cmp` and which that routine refuses
   by name, `emit.inc:987`, "floats are not Tier 1". Finding 3.) The other
   23 are refused here by name, section 3.
2. **The first consumer has no OS.** Kiln links an object into `libkiln.a`:
   newlib's libc and libm exist but the engine's gates forbid libm and
   `malloc` after init, RAM is 4 MB, the ISA is MIPS III with 64-bit
   registers under an ABI with 32-bit addresses (o64), and the machine is
   big-endian reading a little-endian format. So the first deliverable
   emits **functions and nothing else** — no entry point, no runtime, no
   prelude, no capability gating, no ARC — and the one thing it imports is
   a trap hook the consumer supplies (D1, D4).
3. **The closure stays `{fasmg}`** (spec §18.1). A C compiler enters the
   *test* closure only, beside python3 and binutils, never `exsc`'s build
   (D6). `exsc` never runs one (spec §12: `execve` is on no allowlist).
4. **Byte-identical output** (spec §9.3). The C text is a function of the
   module, the flags and `exsc`'s own bytes (D5).
5. **The emitted C performs no operation whose result C leaves undefined
   or implementation-defined where the reference defines it**: no signed
   arithmetic that can overflow, no signed shift, no promotion of a byte
   into `int`, no shift by the type's width, no C bitfield, no dependence
   on the C host's byte order (ADR 0012 items 3 and 4, made a rule for
   every row of section 3).

## 2. Decisions

### D1 Library mode first

`--emitte c` emits **one C translation unit holding the module's functions**
— a definition per `functio` with a body, an `extern` prototype per bodiless
declaration, a `static const` byte array per `data` global, and a fixed
prologue. No `initium` wrapper, no `main`, no prelude, no `EXS_POTESTAS_*`
gating, no MXCSR setup, no `retain`/`release`. A module *may* contain an
`initium`; it is emitted as an ordinary function
`uint64_t exs_initium(unsigned char *p0)` and whoever links the unit decides
whether to call it (the differential harness does, section 5; Kiln never
will). A module without one is a library, which is the point: spec §4.7's
`[OPEN]` sentence — "no library artifact exists for the reference backend,
so `-o` on a module without `initium` … is `EXS-E0424`" — is closed for the
C target only, by the amendment to §4.7 in this commit: under `--emitte c`
the driver does not set `CHK_F_PROGRAM` (`driver/run.inc:1041`) and `-o`
names the translation unit. `[UNTESTED]`

Reason: the first consumer is a library and cannot host a runtime, and a
translation unit of pure functions is what lets the backend exist, be
differentially tested and ship one real program before a C prelude is
designed. Whole-program mode — an `initium` driven by a C `main`, the ten
`exsrt_*` symbols and `exsrt_abort` in freestanding C with raw syscalls per
architecture, `#if EXS_POTESTAS_*` gating so spec §10.3's audit holds of the
binary, `setlocale(LC_ALL, "C")` where a libc exists (spec §9.3), rounding
mode and the FTZ/DAZ assertion at start (ADR 0012 item 2), and the lockfile
shape that pins a per-host C toolchain — is a later milestone and needs all
of those designed first; none is designed here. `[OPEN]`

Rejected: whole-program mode first, with a C prelude. It would have put a
second runtime, a second syscall table and a second capability gate in front
of the first emitted function, for a consumer that can use none of them.

Retired by: C1 (the unit compiles), C2 (the unit runs against the reference).

### D2 Selection: `--emitte c`, and what `--hospes` means for the C target

**`--emitte c`** extends the existing flag, whose three values (`tokens`,
`cst`, `ast`; `driver/cli.inc:258-260`) already mean "emit this stage instead
of a program". Unlike those three, `c` is an artifact, not a dump: it
**requires `-o OUT`**, writes the translation unit there, writes nothing to
stdout, and emits no fasmg. `--emitte c` without `-o` is a usage error
(exit `DRV_EXIT_USAGE`, no `EXS-E` code — spec §9.3: a malformed flag is
about the invocation, not the source). Reason: spec §12 says `OUT` is text
`exsc` never assembles and `-o` names it, and `tools/reproduce.sh`-style
gates diff `OUT` files; a C unit on stdout would be the one emitted text
with no file to diff.

Rejected: a new subcommand or a `--backend` flag. `--emitte` already carries
the "instead of" meaning, and one flag with four values is one table in
`cli.inc`.

**`--hospes`** keeps spec §9.5's meaning — the host platform, required, no
default — and its value set becomes a closed table, one row per value,
stating what each fixes. The C target reads exactly one fact from it, the
width of `mensura` and therefore of `ptr`, because that is the one fact the
emitted text must *assert* (rung (b)) rather than leave to the C compiler:

| `--hospes` | `mensura` / `ptr` width | byte order of `nativus` places | backends |
|---|---|---|---|
| `x86_64-linux` | 64 | the C compiler's target's; the reference's is little | reference; C |
| `riscv64-linux` | 64 | the C compiler's target's | C only `[UNTESTED]` |
| `mips64-none-o64` | 32 | the C compiler's target's (big, on the N64) | C only `[UNTESTED]` |

What the value means for **byte order** is deliberate: nothing. A `nativus`
place is read and written through helpers the prologue selects once from
`__BYTE_ORDER__` at C-compile time, and `maior`/`minor` places are read
byte-at-a-time with explicit shifts (D4, rows `load`/`store`), so the
emitted text depends on no assumption about the host's order and asserts
none; the C compiler's target supplies it. What the value means for
**`mensura`** is the width the checker lays out and the lowering emits
(`u32` under `mips64-none-o64`, `u64` under the other two) and the width the
prologue pins with `_Static_assert(sizeof(void *) == 4)` or `== 8`. Today
the lowering maps `mensura` to `u64` unconditionally; making it read the
row is a change in `lower/` (finding 6), and C3/C4 wait on it.

The spelling `mips64-none-o64`: the first two parts follow the `<arch>-<os>`
shape of the values spec §10.1 already lists (`x86_64-linux`,
`riscv64-linux`); Kiln's toolchain is nixpkgs' `pkgsCross.mips64-embedded`,
i.e. `mips64-none-elf`, and the third part names the one fact `mips64-none`
alone does not fix — that under o64 a 64-bit ISA has 32-bit addresses. A
value that left the ABI implicit would leave `mensura`'s width implicit,
which is the ambient state spec §9.5 exists to remove. `aarch64-linux`,
`wasm32-wasi` and `none-eabi` appear in spec §10.1's `hospites` list and are
**not accepted** until a row states their width; `none-eabi` names no
architecture at all, so its width is not derivable from the value. `[OPEN]`

**What `exsc` does with a combination it cannot honour** — ADR 0012's
rung (d) — is three different things, and the difference is where the fault
lies:

- a `--hospes` value not in the table, or a value whose row does not list
  the backend asked for (`--hospes mips64-none-o64` without `--emitte c`):
  a **usage error**, message on stderr, exit 4 as `drv_m_hospesunsup` does
  today, **no `EXS-E` code** — the fault is in the invocation (spec §9.3).
  The current message's text ("there is no second backend to pick") stops
  being true the day `--emitte c` exists and changes; text is not permanent.
- a well-formed module whose declared `numeri` (spec §5.4) the C target
  cannot honour: **`EXS-E0701`**, "target cannot honour the declared
  `numeri`", the `07xx` code spec §13 says was owed *by ADR 0012* and now
  carries. The task that commissioned this design and ADR 0012's own
  Neutral section both say no such code exists; both are stale (finding 1).
  Nothing is invented. In library mode with floats refused (D8) `numeri` is
  never consulted, so `EXS-E0701` is not reachable from the C path until a
  float opcode is lowered; this row says where it goes when one is.
- an opcode, width or shape the C emitter has no lowering for: an **emitter
  refusal by name** — `bfc: emitter: <reason>` on stderr, exit 4 — exactly
  the class `__bfa_emit_die` already is (`emit.inc:91`) and
  `tests/ir/reject_emit_straddle.ir` already pins for the reference. Not a
  diagnostic: it is a limit of this compiler, not a fact about the source,
  which is `drv_m_trunc`'s own distinction.

Retired by: C1 (a `driver_*` fixture per row of the three-way split).

### D3 The emitted dialect: C11 plus an enumerated GCC/Clang set, and the prologue

The unit is C11 (`-std=c11` clean under `-Wall -Wextra -pedantic` is the
target, `[UNTESTED]`) plus exactly these extensions, each with its reason;
anything outside this list in emitted text is a defect:

| extension | used for | portable fallback |
|---|---|---|
| `__BYTE_ORDER__`, `__ORDER_LITTLE_ENDIAN__`, `__ORDER_BIG_ENDIAN__` | selecting the `nativus` helpers once | `#error` when undefined (rung (b)) |
| `__builtin_memcpy` | `copy n`, and nothing else | none needed — freestanding C does not promise `<string.h>`, and the builtin is what GCC and Clang inline for a constant `n` |
| `__has_builtin` + `__builtin_add_overflow` / `__builtin_sub_overflow` / `__builtin_mul_overflow` | speed only, in the trapping helpers | the portable expressions of D4, which are the definition |
| `__GNUC__` / `__clang__` | the family check | `#error` outside the family — ADR 0012: not a goal |

Everything else is ISO C11: `<stdint.h>` (`uint64_t`, `uintptr_t`,
`UINT64_C`), `<limits.h>` (`CHAR_BIT`), `_Static_assert`, `_Alignas`,
`_Noreturn`, `goto`. No `<string.h>`, no `<stdlib.h>`, no `<stdio.h>`, no
`<float.h>` until a float opcode is lowered — the prologue carries only the
asserts a lowered opcode relies on, so a unit that uses no floats asserts
nothing about floats and compiles on a target with none.

**The prologue**, emitted at the top of every unit, a fixed text carried in
`program_c.inc` the way `program.inc` carries the prelude blobs (a function
of `exsc`'s bytes and nothing else), then the one per-`hospes` line:

```c
/* exsecutor: reach backend */
#include <stdint.h>
#include <limits.h>
#if !defined(__GNUC__) && !defined(__clang__)
# error "exsecutor: the emitted C is C11 plus a GCC/Clang extension set (ADR 0012)"
#endif
#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__) || !defined(__ORDER_BIG_ENDIAN__)
# error "exsecutor: __BYTE_ORDER__ is required to select the nativus helpers"
#endif
#if defined(__FAST_MATH__)
# error "exsecutor: -ffast-math is refused (spec 5.4)"
#endif
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__
# error "exsecutor: -ffinite-math-only is refused (spec 5.4)"
#endif
_Static_assert(CHAR_BIT == 8, "exsecutor: CHAR_BIT must be 8");
_Static_assert((-1 & 3) == 3, "exsecutor: two's complement");
_Static_assert(sizeof(unsigned) * CHAR_BIT >= 32, "exsecutor: unsigned holds a byte and a shift");
_Static_assert(sizeof(void *) == 8, "exsecutor: hospes x86_64-linux has 64-bit addresses");   /* per row of D2 */
_Noreturn void exsrt_abortus(unsigned kind);
/* exsi_* helpers follow: section 3 */
```

Every line of it is `[UNTESTED]`. Two are already suspect from memory:
ADR 0012 says `__FINITE_MATH_ONLY__` must be *undefined*, but GCC's
documentation says it is always defined, `0` by default and `1` under
`-ffinite-math-only`, which is why the prologue tests its value and not its
presence (finding 2); and `(-1 & 3) == 3` is a two's-complement test that
C23 makes vacuous and C11 leaves meaningful. The pragmas ADR 0012 item 1
names — `#pragma STDC FP_CONTRACT OFF`, `#pragma GCC optimize("fp-contract=off")`
— and its item 2's `fesetround` / `_mm_getcsr` / FPCR reads enter the
prologue with the first float opcode and not before, but **they are measured
in C1 anyway**, because ADR 0012 made that the backend's first job:

**C1's first task**, before any emitter line is written: a scratch
translation unit under `prototypes/` (verification-only, never shipped)
holding every incantation above and in ADR 0012, compiled by `gcc` and
`clang` at `-O0` and `-O2` with `-fsanitize=undefined
-fno-sanitize-recover=all`, and a table in section 8 with one row per
incantation and four cells per row, each cell "held", "rejected" or "no
effect" with the compiler version. The rows, all `[UNTESTED]`:

| incantation | what it must do |
|---|---|
| `#if defined(__FAST_MATH__)` | fire under `-ffast-math`, not otherwise |
| `#if __FINITE_MATH_ONLY__` | fire under `-ffinite-math-only`, not otherwise, and be defined at all |
| `#pragma STDC FP_CONTRACT OFF` | prevent `a*b+c` contracting under `-O2 -ffp-contract=fast`; GCC is believed to ignore it (ADR 0012 open item) |
| `#pragma GCC optimize("fp-contract=off")` | the belt to that brace, accepted per translation unit |
| `_Static_assert((-1 & 3) == 3, …)` | compile under `-std=c11 -pedantic` |
| `_Static_assert(sizeof(void *) == 4, …)` | fire under `-m32` / o64, pass under lp64 |
| `_Alignas(16) unsigned char s[n];` on a local | accepted, and `&s[0]` 16-aligned at `-O0` and `-O2` |
| `_Noreturn void f(unsigned);` | accepted under `-std=c11`, `-std=gnu11` and `-std=gnu2x` (Kiln's two dialects) |
| `__builtin_memcpy(d, s, 144000)` | compile without pulling in `<string.h>`; note whether a `memcpy` symbol is referenced at `-Os` (Kiln links newlib, so it resolves; recorded, not forbidden) |
| `__has_builtin(__builtin_mul_overflow)` | true on both; the helper's fast path taken |
| the `exsi_*` helpers of section 3, each | no UBSan report on the fixtures of `tests/ir/` that exercise them |
| `(unsigned char *)((uintptr_t)p + (uintptr_t)i * s)` | no `-fsanitize=pointer-overflow` report when in bounds |
| `fesetround(FE_UPWARD)` then a constant-folded `1.0/3.0` | whether the fold respects the mode (ADR 0012 open item; floats are D8's, the measurement is cheap) |
| `_mm_getcsr()` FTZ/DAZ bits on x86-64; `__builtin_aarch64_get_fpcr` or inline `mrs` on aarch64 | readable at process start (ADR 0012 item 2) |

Rejected: `-std=c99`. `_Static_assert`, `_Alignas` and `_Noreturn` are C11,
and each replaces a GCC attribute that would otherwise be on the extension
list; both consumers' dialects (`gnu11`, `gnu2x`) include them.

Retired by: C1's measurement table.

### D4 One lowering per opcode, on one carrier type

**Canonical form in C.** Every integer value of any `uN` or `iN` is held in
a `uint64_t` in exactly the reference's canonical form (IR 2.2): a `uN` is
zero-extended, an `iN` is sign-extended to 64 bits, `u1` is 0 or 1. Every
helper assumes canonical inputs and returns a canonical result; every
arithmetic operation is performed on `uint64_t`, whose overflow C defines as
modular. **No signed integer type appears in emitted arithmetic**: signed
compare flips the sign bit and compares unsigned, arithmetic shift right is
an unsigned shift with the sign replicated by a mask, and signed
multiplication traps by magnitude. `u24`, `u40`, `u48` and `u56`, which
have no C type, are therefore not a special case anywhere but in the
`nativus` helpers, and `u1` is a `uint64_t` holding 0 or 1.

Reason: the reference holds values this way (IR 2.2 says the C backend
"may hold it differently as long as ADR 0012's differential test cannot
tell"), and holding them the *same* way means the trap rule — compute in
64 bits, normalise, trap if the normalised value differs from the raw one —
is one rule in two languages and the differential test compares like with
like. The cost is 64-bit arithmetic on a 32-bit ISA; the VR4300 has 64-bit
integer registers under o64, so the first consumer pays nothing, and a
narrower carrier for `u8`–`u32` on a 32-bit ISA is an optimisation the
differential test licenses later. `[OPEN]`

Rejected: the smallest C type that fits (`uint8_t` for `u8`, …). C's integer
promotions turn `uint16_t * uint16_t` into a signed `int` multiply that
overflows at 65535 × 65535 — undefined behaviour introduced by the choice of
storage type, which is ADR 0012 item 4's warning applied to arithmetic.

**The mapping table**, one row per opcode in `ir.inc` order (`BFA_OP_*`,
1–60). `T` is the instruction's type, `N` its width, `M` the mask
`exsi_mask(N)` (`UINT64_MAX` at 64, else `(1 << N) - 1`), `S` the sign bit
`1 << (N-1)`. Value `%k` is `vk`, block `bk` is label `bk`, a `slot`'s
storage is `sk`, a phi's edge temporary is `tk`; all are internal ids
(IR 2.1, creation order), declared at the top of the function in id order.
`nu(x,N)` is `exsi_norm_u`, `x & M`; `ni(x,N)` is `exsi_norm_i`,
`((x & M) ^ S) - S` in unsigned arithmetic, which wraps to the sign-extended
pattern. "norm" means `nu` for a `uN`, `ni` for an `iN`. Every helper named
`exsi_*` is `static inline` in the prologue and is written out in full
after the table; the C1 status column says what C1 does with the row.

| # | opcode | C emitted, or refused with this message | C1 |
|---|---|---|---|
| 1 | `add T a b` | `vk = exsi_add_u(a, b, N)` / `exsi_add_i` — trap on overflow, `exsrt_abortus(1)` | lowered |
| 2 | `sub T a b` | `exsi_sub_u` / `exsi_sub_i` | lowered |
| 3 | `mul T a b` | `exsi_mul_u` / `exsi_mul_i` — one rule at every width, by division; no `__int128` (finding 5) | lowered |
| 4 | `div T a b` | refused: `bfc: emitter: div: no reference lowering exists (ssa-ir.md 2.3); refused rather than guessed` | refused |
| 5 | `rem T a b` | refused, as `div` | refused |
| 6 | `addw T a b` | `vk = norm(a + b, N)` | lowered |
| 7 | `subw T a b` | `vk = norm(a - b, N)` | lowered |
| 8 | `mulw T a b` | `vk = norm(a * b, N)` — the low N bits of a modular 64-bit product are the low N bits of the true product | lowered |
| 9 | `adds T a b` | `exsi_adds_u` / `exsi_adds_i` — clamp to `[0, M]` or `[-S, S-1]` | lowered |
| 10 | `subs T a b` | `exsi_subs_u` / `exsi_subs_i` | lowered |
| 11 | `muls T a b` | refused: `bfc: emitter: muls: no reference lowering exists (spec 5.4 has no *\|)` | refused |
| 12 | `addov T a b` | refused: `bfc: emitter: addov: no reference lowering exists` | refused |
| 13 | `subov T a b` | refused, as `addov` | refused |
| 14 | `mulov T a b` | refused, as `addov` | refused |
| 15 | `and T a b` | `vk = a & b` — canonical in, canonical out | lowered |
| 16 | `or T a b` | `vk = a \| b` | lowered |
| 17 | `xor T a b` | `vk = a ^ b` | lowered |
| 18 | `shl T a c` | `vk = exsi_shl_u(a, c, N)` / `exsi_shl_i`: `if (c >= N) exsrt_abortus(1)`, then `norm(a << c, N)` — `c < N <= 64` so the C shift is defined; a negative `iN` count is a huge unsigned and traps, as the reference's `jae` does | lowered |
| 19 | `shr T a c` | `exsi_shr_u`: trap as `shl`, then `a >> c`; `exsi_shr_i`: trap, then `c == 0 ? a : (a >> c) \| ((0 - (a >> 63)) << (64 - c))` — arithmetic without a signed shift | lowered |
| 20 | `cmp.p T a b → u1` | `uN`, `ptr`: `vk = (a OP b)` with `== != < <= > >=`; `iN`: `vk = ((a ^ S64) OP (b ^ S64))`, `S64 = 1 << 63` — a sign-bit flip makes unsigned order signed order. `ptr` under `<`: compared as `uintptr_t` | lowered |
| 21 | `zext T a` | source `(s, m)`: `vk = normT(nu(a, m), N)` — zero-extend from the *source* width first (IR 2.2), then the destination's form | lowered |
| 22 | `sext T a` | `vk = normT(ni(a, m), N)` — sign-extend from the source width, then cut to the destination's form; `sext u16` of `i8` −56 is 65480 | lowered |
| 23 | `trunc T a` | `vk = normT(a, N)` — low bits kept, then normalised | lowered |
| 24 | `fadd F a b` | refused: `bfc: emitter: fadd: floats are not Tier 1 (the reference has no lowering; ADR 0012's numeric subset is untestable until it does)` | refused |
| 25 | `fsub` | refused, as `fadd` | refused |
| 26 | `fmul` | refused, as `fadd` | refused |
| 27 | `fdiv` | refused, as `fadd` | refused |
| 28 | `fneg` | refused, as `fadd` | refused |
| 29 | `fma` | refused, as `fadd` | refused |
| 30 | `fcmp.p` | refused, as `fadd` — the reference refuses it too, `emit.inc:987` | refused |
| 31 | `fext` | refused, as `fadd` | refused |
| 32 | `ftrunc` | refused, as `fadd` | refused |
| 33 | `itof` | refused, as `fadd` | refused |
| 34 | `ftoi` | refused, as `fadd` | refused |
| 35 | `bitcast` | refused, as `fadd` | refused |
| 36 | `redinit F op shape w` | refused: `bfc: emitter: redinit: reductions have no reference lowering (ssa-ir.md 2.6: the reference's lowering of arborea is the definition)` | refused |
| 37 | `contrib h v` | refused, as `redinit` | refused |
| 38 | `redfin F h` | refused, as `redinit` | refused |
| 39 | `slot n align → ptr` | at function top: `_Alignas(align) unsigned char sk[n];`; at the instruction: `vk = sk;`. Alignment a power of two 1..16, as the reference requires | lowered |
| 40 | `load T p off o` | integer `T`, whole bytes `k = N/8`: `nativus` → `vk = normT(exsi_ld_n(p + off, k), N)`; `maior` → `exsi_ld_be`; `minor` → `exsi_ld_le`; `k == 1` all three are one byte. `T = ptr`: `vk = (unsigned char *)(uintptr_t)exsi_ld_n(p + off, sizeof(void *))`. Not whole bytes: refused, as the reference does | lowered |
| 41 | `store T p off o v` | `exsi_st_n(p + off, k, v)` / `exsi_st_be` / `exsi_st_le`; `ptr`: `exsi_st_n(p + off, sizeof(void *), (uintptr_t)v)` | lowered |
| 42 | `loadbits T p byte bit` | `vk = ((unsigned)p[byte] >> (8 - bit - N)) & ((1u << N) - 1)` — the byte cast to `unsigned` *before* the shift (never promoted into `int`), shifts of at most 7, MSB-first (spec §5.2 rule 1). `bit + N > 8` refused: `bfc: emitter: loadbits/storebits: a field that straddles a byte boundary is not implemented (DeModFrame never straddles)` — parity with `reject_emit_straddle.ir`; an `iN` field refused as the reference refuses it | lowered |
| 43 | `storebits T p byte bit v` | `p[byte] = (unsigned char)(((unsigned)p[byte] & ~(m << s)) \| (((unsigned)v & m) << s))`, `m = (1u << N) - 1`, `s = 8 - bit - N`; a read-modify-write of one byte, the neighbours untouched | lowered |
| 44 | `copy n d s` | `__builtin_memcpy(d, s, n);` — non-overlapping is the lowering's guarantee (distinct slots; borrowed sources, IR 2.9); overlap is undefined by both backends and the differential test cannot compare it `[OPEN]` | lowered |
| 45 | `addr p off → ptr` | `vk = (unsigned char *)((uintptr_t)p + off)` | lowered |
| 46 | `index p i stride → ptr` | `vk = (unsigned char *)((uintptr_t)p + (uintptr_t)i * stride)` — integer arithmetic on the address, not C pointer arithmetic, so an out-of-bounds index (which the lowering's `chk` precedes) is not C undefined behaviour but the same wild address the reference computes; `(uintptr_t)i` truncates a `u64` index on a 32-bit host, which the `mensura` row of D2 makes unreachable from the lowering | lowered |
| 47 | `gaddr N → ptr` | `vk = (unsigned char *)exsi_gN;` over `static const _Alignas(align) unsigned char exsi_gN[size] = { 0x41, … };` — read-only, as the reference's `segment readable`; a `store` through it is a wrong program in both backends. `size == 0` emits `[1]` with a comment, since C has no zero-length array (finding 7) | lowered |
| 48 | `chk i n` | `if (i >= n) exsrt_abortus(1);` — unsigned, `i == n` traps (`tests/ir/trap_chk.ir`) | lowered |
| 49 | `retain r` | refused: `bfc: emitter: retain/release: no object header exists in library mode (ADR 0012, c-backend.md D8)` | refused |
| 50 | `release r` | refused, as `retain` | refused |
| 51 | `call T @f args…` | `vk = exs_f(args);` or `exs_f(args);` for `void`; every argument a `vN`; the callee's prototype is emitted before every definition, so order is free. A bodiless callee is its `externus` symbol, bare (D5) | lowered |
| 52 | `callind T fp args…` | refused: `bfc: emitter: callind: no reference lowering exists (ssa-ir.md 2.9: a function value's closure layout is [OPEN])` | refused |
| 53 | `ret [v]` | `return vk;` / `return;` | lowered |
| 54 | `jmp bT` | the parallel copy for the edge (row 57), then `goto bT;` | lowered |
| 55 | `br c bT bF` | `if (vc) { copies for the T edge; goto bT; } else { copies for the F edge; goto bF; }` — a C `goto` needs no per-edge stub; when `bT == bF` the true edge takes the phi's first pair naming this predecessor and the false edge the second, in edge order (`phi_same_target.ir`) | lowered |
| 56 | `trap kind` | `terminus` → `exsrt_abortus(5);`; any other word refused: `bfc: emitter: trap: unknown kind word (only terminus has a runtime kind)` — parity with `emit.inc:3227` | lowered |
| 57 | `phi T bP v …` | no statement at the phi. At each incoming edge, a **parallel copy**: `tj = vj` for every phi `j` of the target in phi order, reading the operand for this predecessor, *then* `vj = tj` for every `j` — every read precedes every write, so the swap and the lost-copy case are correct with no cycle analysis (`phi_swap.ir`, `phi_lost_copy.ir`); the temporaries are function-scope `uint64_t` (or `unsigned char *`) declared at the top | lowered |
| 58 | `param T i` | the function's parameter `pi`, typed `uint64_t` for every integer, `unsigned char *` for `ptr`; `vk = pi;` | lowered |
| 59 | `iconst T imm` | `vk = UINT64_C(<canonical pattern in decimal>);` — an `iN` constant prints its sign-extended 64-bit pattern as an unsigned decimal (`i8` −56 is `18446744073709551560`); no signed literal, no hex | lowered |
| 60 | `fconst F bits` | refused, as `fadd` | refused |

Not in the table: `nop` (op 0; verifier rule 9 refuses it, `verify.inc`
finding 2) and `faddr` (IR 2.3, `[UNIMPLEMENTED]` in `ir.inc`); both are
refused by the emitter if met, by name.

Count: **36 lowered by statement, `phi` lowered at the edges = 37, exactly
the reference's set; 23 refused**, of which 22 are refused because the
reference refuses them and 2 (`retain`, `release`) because library mode has
no runtime — and none of the 23 occurs in any of the 49 `tests/ir/*.ir`
fixtures or the 22 non-vector `tests/programs/` directories (measured by
grep at `dbe1d64`), so the corpus of section 5 is not narrowed by any
refusal.

**The helpers**, as they will appear in the prologue (`[UNTESTED]`, every
one; the differential test is their proof):

```c
static inline uint64_t exsi_mask(unsigned n)
{ return n == 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1; }
static inline uint64_t exsi_norm_u(uint64_t x, unsigned n)
{ return x & exsi_mask(n); }
static inline uint64_t exsi_norm_i(uint64_t x, unsigned n)
{ uint64_t s = UINT64_C(1) << (n - 1); return ((x & exsi_mask(n)) ^ s) - s; }

static inline uint64_t exsi_add_u(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a + b;
  if (n == 64 ? t < a : t > exsi_mask(n)) exsrt_abortus(1);
  return t; }
static inline uint64_t exsi_add_i(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a + b;                        /* exact for n < 64: |a|,|b| < 2^62 */
  if (n == 64 ? ((((a ^ t) & (b ^ t)) >> 63) != 0) : exsi_norm_i(t, n) != t)
    exsrt_abortus(1);
  return t; }
static inline uint64_t exsi_sub_u(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a - b;
  if (n == 64 ? a < b : t > exsi_mask(n)) exsrt_abortus(1);
  return t; }
static inline uint64_t exsi_sub_i(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a - b;
  if (n == 64 ? ((((a ^ b) & (a ^ t)) >> 63) != 0) : exsi_norm_i(t, n) != t)
    exsrt_abortus(1);
  return t; }
static inline uint64_t exsi_mul_u(uint64_t a, uint64_t b, unsigned n)
{ if (n <= 32) { uint64_t t = a * b; if (t > exsi_mask(n)) exsrt_abortus(1); return t; }
  if (a != 0 && b > exsi_mask(n) / a) exsrt_abortus(1);   /* a*b > M  <=>  b > M/a */
  return a * b; }
static inline uint64_t exsi_mul_i(uint64_t a, uint64_t b, unsigned n)
{ uint64_t sa = a >> 63, sb = b >> 63;
  uint64_t ma = sa ? 0 - a : a, mb = sb ? 0 - b : b;     /* magnitudes; 2^63 stays 2^63 */
  uint64_t neg = sa ^ sb;
  uint64_t lim = neg ? UINT64_C(1) << (n - 1) : (UINT64_C(1) << (n - 1)) - 1;
  if (ma != 0 && mb > lim / ma) exsrt_abortus(1);
  { uint64_t p = ma * mb; return neg ? 0 - p : p; } }   /* canonical by construction */

static inline uint64_t exsi_adds_u(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a + b; return (n == 64 ? t < a : t > exsi_mask(n)) ? exsi_mask(n) : t; }
static inline uint64_t exsi_subs_u(uint64_t a, uint64_t b, unsigned n)
{ (void)n; return a < b ? 0 : a - b; }
static inline uint64_t exsi_adds_i(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a + b, s = UINT64_C(1) << (n - 1);
  if (n == 64) return ((((a ^ t) & (b ^ t)) >> 63) != 0) ? ((a >> 63) ? s : s - 1) : t;
  return exsi_norm_i(t, n) != t ? ((t >> 63) ? exsi_norm_i(s, n) : s - 1) : t; }
static inline uint64_t exsi_subs_i(uint64_t a, uint64_t b, unsigned n)
{ uint64_t t = a - b, s = UINT64_C(1) << (n - 1);
  if (n == 64) return ((((a ^ b) & (a ^ t)) >> 63) != 0) ? ((a >> 63) ? s : s - 1) : t;
  return exsi_norm_i(t, n) != t ? ((t >> 63) ? exsi_norm_i(s, n) : s - 1) : t; }

static inline uint64_t exsi_shl_u(uint64_t a, uint64_t c, unsigned n)
{ if (c >= n) exsrt_abortus(1); return exsi_norm_u(a << c, n); }
static inline uint64_t exsi_shl_i(uint64_t a, uint64_t c, unsigned n)
{ if (c >= n) exsrt_abortus(1); return exsi_norm_i(a << c, n); }
static inline uint64_t exsi_shr_u(uint64_t a, uint64_t c, unsigned n)
{ if (c >= n) exsrt_abortus(1); return a >> c; }
static inline uint64_t exsi_shr_i(uint64_t a, uint64_t c, unsigned n)
{ if (c >= n) exsrt_abortus(1);
  return c == 0 ? a : (a >> c) | ((0 - (a >> 63)) << (64 - c)); }

static inline uint64_t exsi_ld_be(const unsigned char *p, unsigned k)
{ uint64_t v = 0; unsigned i; for (i = 0; i < k; i++) v = (v << 8) | p[i]; return v; }
static inline uint64_t exsi_ld_le(const unsigned char *p, unsigned k)
{ uint64_t v = 0; unsigned i; for (i = 0; i < k; i++) v |= (uint64_t)p[i] << (8 * i); return v; }
static inline void exsi_st_be(unsigned char *p, unsigned k, uint64_t v)
{ unsigned i; for (i = 0; i < k; i++) p[i] = (unsigned char)(v >> (8 * (k - 1 - i))); }
static inline void exsi_st_le(unsigned char *p, unsigned k, uint64_t v)
{ unsigned i; for (i = 0; i < k; i++) p[i] = (unsigned char)(v >> (8 * i)); }
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define exsi_ld_n exsi_ld_le
# define exsi_st_n exsi_st_le
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define exsi_ld_n exsi_ld_be
# define exsi_st_n exsi_st_be
#else
# error "exsecutor: unknown __BYTE_ORDER__"
#endif
```

`p[i]` in the byte helpers is an `unsigned char` promoted to `int` and then
converted to `uint64_t` by the `|` — a value in 0..255, no sign, no
overflow: the one promotion the emitted text contains, and it is harmless
because the promoted value is never operated on as an `int`. The
`loadbits`/`storebits` rows cast to `unsigned` first for the same reason
stated the other way round. Where `__has_builtin(__builtin_add_overflow)`
is true the trapping helpers take the builtin as a fast path under
`#if`; the expressions above stay the definition and the differential
test at `-O0` and `-O2` under both compilers is what shows the two paths
agree `[UNTESTED]`.

Rejected, per row: `__int128` for `mul` at widths 33–63 (ADR 0012 item 3's
"computed in a wider type") — GCC provides it only where
`__SIZEOF_INT128__` is defined, which excludes 32-bit-pointer targets such
as o64; the division check is exact at every width and needs no wider type
(finding 5). `memcpy` into a `uint32_t` for a `nativus` load — it is what
one writes by hand, but it needs a C type per width, which `u24`/`u40`/
`u48`/`u56` lack, and a second mechanism beside the byte helpers; both
compilers pattern-match the shift-or loop into one load at `-O2`
`[UNTESTED]`. A C bitfield anywhere — ADR 0012 item 4. C pointer
arithmetic for `index` — undefined outside the object, where the reference
merely computes a wrong address; the differential test compares
observables, and "undefined" has none.

Retired by: C1 (`tests/unit/bfc_emit_*.asm` pin the emitted text per row),
C2 (the differential test runs every row the corpus reaches).

### D5 Determinism, and identifier mangling

The unit's text is a function of exactly: the module (function order =
declaration order; block order = block index; value, slot and temporary
names = internal ids; global order = id order; literal bytes = `gdata`),
the `--hospes` row (one `_Static_assert` line), and `exsc`'s own bytes (the
prologue and helpers, carried verbatim as `program.inc` carries the prelude
blobs). No path, no `--epoch` (nothing in the unit is a timestamp), no host
name, no `exsc` version, no address, no hash bucket; the one map the IR
holds is looked up and never walked (IR 4). The audit table is RT 4's with
"segment order" replaced by "prologue, prototypes, globals, definitions":

| this order | is a function of |
|---|---|
| prototypes, definitions | `Module.funcs` order = declaration order |
| labels `bk`, values `vk`, slots `sk`, temporaries `tk` | internal ids, creation order |
| globals `exsi_gN` | global id order; bytes from `gdata` |
| parameters `pi` | signature order |
| helper text | `exsc`'s bytes |
| the `_Static_assert` on `sizeof(void *)` | the `--hospes` row |

**Mangling.** C identifiers are `[A-Za-z_][A-Za-z0-9_]*`, which is stricter
than a fasmg label, and the reference left non-ASCII names `[OPEN]`
(`program.inc:82`, RT finding 13). Decided here, and it is the first place
in the tree a mangling is stated:

- A **defined** function `@name` is emitted as `exs_` + `mangle(name)`.
- A **bodiless** declaration (`externus`, or a prelude routine such as
  `@exsrt_scriptor_scribe`) is emitted **verbatim**, with no prefix, and
  must already be a C identifier or the unit is refused by name:
  `bfc: emitter: externus symbol is not a C identifier`. Reason: an
  `externus` name is the symbol the *linker* must find (spec §5.3's
  `strlen` must be `strlen`); the prefix exists to keep this module's
  definitions out of the linker's way, and a declaration names something
  outside the module. The prelude's ten `exsrt_*` names and the trap hook
  `exsrt_abortus` therefore keep their names in C, and a user function
  named `exsrt_x` becomes `exs_exsrt_x`, which cannot collide.
- `mangle` is injective and keeps Latin names readable: bytes
  `[A-Za-z0-9]` are kept; every other byte of the NFC UTF-8 (spec §8.1
  guarantees NFC, so the byte sequence is canonical) is `_` followed by
  **two uppercase hex digits**; a source `_` is kept **unless the next two
  bytes are both uppercase hex digits**, in which case it is escaped as
  `_5F`. Decoding is left to right: at `_`, two uppercase hex digits mean
  an escaped byte, anything else means a literal underscore. So
  `scribe_octeto` → `exs_scribe_octeto`, `lege_caput` → `exs_lege_caput`,
  `Type.m` → `exs_Type_2Em`, `lambda$7` → `exs_lambda_247`, `mēnsūra`
  (`ē` is `C4 93`) → `exs_m_C4_93ns_C5_ABra`, `a_5F` → `exs_a_5F5F`.
- File-scope helpers are `exsi_*`, runtime hooks `exsrt_*`, definitions
  `exs_*`: three prefixes that differ at their fourth byte, so no mangled
  name can spell a helper and no helper a mangled name. Nothing begins with
  `_` or `__`, which C reserves.

Rejected: doubling every underscore (`exs_lege__caput`) — injective, but it
is the consumer's API and Kiln would have to spell it. Lowercase hex
escapes — injective only if a source `_` before any of `a`–`f` is escaped,
which is most Latin names. A length-prefixed scheme (Itanium's
`<len><name>`) — needs escapes anyway for `.` and `$`, so it adds a
mechanism without removing one. No C compiler limit on identifier length
is assumed: C11's 31-significant-character translation limit is a minimum
the GCC/Clang family exceeds without bound, which is one more reason the
family is the target and not ISO C alone.

Retired by: C1 (`tests/unit/bfc_mangle.asm`: the six examples above, each
way; a unit with two names that differ only outside ASCII gets two
different C names), C2 (two `exsc --emitte c` runs under
`tools/reproduce.sh`'s divergent conditions, byte-identical `[UNTESTED]`).

### D6 The differential test is the acceptance gate, and the toolchain stays in the test closure

**What is compared.** ADR 0012's three observables, for one input under
both backends: the bytes written to stdout, the exit status, and
trap-or-not — and when both trap, the **kind** `N` of `abortus N` on fd 2,
since `tests/run.sh`'s `abort=` key already distinguishes kinds and a `chk`
trap (1) reported as a `terminus` trap (5) is a wrong program. Stderr is
otherwise not compared: the reference's prelude and the shim below write
different English before `abortus`, and the English is not promised
(`prelude/README.md`).

**Over which corpus.** Every `tests/ir/*.ir` (49 at `dbe1d64`) and every
non-`receptio_vec_*` `tests/programs/` directory (22). The 70
`receptio_vec_*` directories are the receiver's impaired-vector sweep and
take minutes under the reference; they are excluded from the per-commit
gate and listed as a nightly run `[OPEN]`. Every fixture keeps its one
`; TEST:` directive: the same `expect-exit=`, `abort=`, `stdout=`, `stdin=`
apply to the C build, so a fixture whose C build meets its own directive
but differs from the reference is caught twice.

**How it runs**, as a fifth phase `run_differential_tests` of
`tests/run.sh` after `run_program_tests`:

1. `tests/ir/emit_c.asm` — the mirror of `emit_ir.asm`: IR on stdin, the C
   unit on stdout, the same verifier call per function, the same exit
   codes (3 parse, 4 emitter refusal, 5 verifier), `--hospes x86_64-linux`
   fixed, freestanding and audited against the compiler's nine syscalls
   exactly as `emit_ir` is. For a program directory, `exsc aedifica
   --hospes x86_64-linux SRC… --emitte c -o out.c` beside the existing
   `-o out.asm`.
2. `tests/c/exsrt_shim.c` — **the test's stand-in for a runtime, not a C
   prelude** (verification-only; hosted; may use libc). It defines
   `exsrt_abortus(kind)`: write `exsecutor: abortus N\n` to fd 2, then
   `__builtin_trap()`, which is `ud2` on x86-64 and so SIGILL, the shape
   `check_run` already checks; the ten `exsrt_*` routines of the
   `{Mundus, ambitus}` closure `emit_ir.asm` fixes, over records laid out
   exactly as `prelude/README.md` states them (`ExsAmbitus` 40 bytes with
   `in`/`out`/`err` at 0/4/8, `Scriptor` and `Lector` 16 bytes with the
   descriptor at 8, `textus` `{ptr, len}`) — a second copy of `interface.inc`'s
   layout facts, which is RT H4's hazard and is recorded as such `[OPEN]`;
   and `int main(void) { return (int)exs_initium(&mundus); }`. The four
   `exsrt_alloc_*` routines are **not** defined: a fixture that reaches
   them fails to link, which is the same statement `emit_ir.asm` makes by
   fixing the closure.
3. For each unit, four builds: `gcc` and `clang`, each at `-O0` and `-O2`,
   all with `-std=c11 -Wall -Wextra -fsanitize=undefined
   -fno-sanitize-recover=all`, linked with the shim; then run under the
   same `run_binary` (empty environment, 20-second limit, stdin per the
   directive) and compared with the reference run's three observables and
   with the directive. A UBSan report is a failure on its own.
4. A **backend-dependent refusal**: the default rule is parity — the C
   emitter must exit exactly as `emit-exit=`/`exsc-exit=` says the
   reference does, so `reject_emit_straddle.ir` (`emit-exit=4`) requires
   `emit_c` to exit 4 too. A fixture where the two verdicts legitimately
   differ writes `c-emit-exit=N` (IR) or `c-exsc-exit=N` (programs);
   `parse_run_keys` learns the two keys and still fails on an unknown one.
   No fixture needs either today; the keys exist so the harness can say so
   when one does, rather than special-casing a name.
5. A missing `gcc` or `clang` is a **failure of the phase**, not a skip —
   the same choice `run_ir_tests` makes for a missing `fasmg`: a harness
   that finds nothing must not report success.

**Where the toolchain lives, and how that is kept true.** `pkgs.gcc` and
`pkgs.clang` are added to `checks.test`'s `nativeBuildInputs` (beside
`python3` and `binutils`, `flake.nix:581`) and to `devShells.default`
(`flake.nix:349`), each with the same "verification-only, never on the
build closure" comment those two carry. They are added to nothing else:
`packages.exsc` and `buildExsecutorPackage` keep `nativeBuildInputs =
[ fasmgPkg ]`. Kept true by an eval-time assertion in `flake.nix` —
`assert builtins.length exscPkg.nativeBuildInputs == 1` on the package
definition, so a C compiler leaking into the build closure is a flake
evaluation error `[UNTESTED]` — and by the flake's header rule (lines
9–16), which already states the line. `exsc` itself never invokes a C
compiler (constraint 3).

Rejected: a C toolchain in the devShell only, with the differential phase
skipped in `nix flake check` — that is the false green `tests/run.sh`'s
header lists. Comparing emitted text between backends — there is none in
common; ADR 0012 is explicit that the comparison is of observables. Running
under one compiler — ADR 0012 asks for both, at both levels, and the
prologue's incantations are the first thing that differs between them.

Retired by: C2.

### D7 The first real program: the StreamDB v3 reader for Kiln

Section 6. Its certificate is **semantic, not byte-identical**: the same
Exsecutor source compiled by both backends, run over the frozen corpus at
`vendor/streamdb-v3/`, agreeing under D6's three observables — and, because
a container is not byte-reproducible (the upstream writer mints UUIDs from
`/dev/urandom`), what the driver program writes to stdout is not the
container but what a reader can *say* about one: every key read back
byte-exact with its CRC verified, the document and suffix-match counts,
and the outcome of every error case (truncation, a flipped payload byte,
an absent key). The emitted C must then compile under `mips64-elf-gcc
-march=vr4300 -mtune=vr4300 -mabi=o64 -Os -std=gnu11 -Wall -Wextra` with
Kiln's own gates — no `.d`-suffixed instruction in the disassembly
(`nix/checks/toolchain.nix:82`'s grep), no undefined reference to libm or
`malloc` (`nm -u` lists exactly `exsrt_abortus`, and `memcpy` if `-Os`
out-lines a `__builtin_memcpy`, which newlib resolves). Retired by C3 and
C4.

### D8 Not in scope for C1–C4

Floats — the reference cannot lower them either, so ADR 0012's numeric
subset is exactly what a differential test cannot yet check, and a C
lowering written first would be untested by construction; `div`/`rem`,
`muls`, the three `*ov` predicates — integer opcodes the reference lacks,
which enter **both** backends in one later milestone with fixtures for
each; ARC — no object header exists in library mode; generics and
dictionaries; `callind`; the three reduction opcodes; whole-program mode
(D1). Each is a refusal by name in D4's table, never a silent omission.

## 3. Shape of the emitted unit

For IR 2.11's `@dot` example the unit would be (illustrative, `[UNTESTED]`;
`redinit` makes the real `@dot` a refusal, so this is the integer skeleton
of it):

```c
/* exsecutor: reach backend */
/* prologue and exsi_* helpers, section 2 D3/D4 */
uint64_t exs_dot(unsigned char *p0, unsigned char *p1, uint64_t p2);   /* prototypes, all functions */

uint64_t exs_dot(unsigned char *p0, unsigned char *p1, uint64_t p2)
{
    unsigned char *v1, *v2, *v7, *v9;
    uint64_t v3, v4, v5, v6, v8, v10, v11, t5;
b1:
    v1 = p0; v2 = p1; v3 = p2;
    v4 = UINT64_C(0);
    t5 = v4; v5 = t5;                    /* the b1 -> b2 edge's parallel copy */
    goto b2;
b2:
    v6 = (v5 < v3);
    if (v6) { goto b3; } else { goto b4; }
b3:
    v7 = (unsigned char *)((uintptr_t)v1 + (uintptr_t)v5 * 4);
    v8 = exsi_ld_n(v7 + 0, 4);
    v9 = (unsigned char *)((uintptr_t)v2 + (uintptr_t)v5 * 4);
    v10 = exsi_ld_n(v9 + 0, 4);
    v11 = exsi_norm_u(v5 + UINT64_C(1), 64);
    t5 = v11; v5 = t5;                   /* the b3 -> b2 edge */
    goto b2;
b4:
    return v10;
}
```

Every declaration is at the top, so no label is followed by a declaration
(C11 forbids it; C23 does not, and the unit does not rely on C23). The
entry block is the first in index order and is fallen into. `(void)pi;`
is emitted for a parameter no instruction reads, so `-Wextra` stays quiet
without `-Wno-unused-parameter`.

## 4. Error codes, checked against spec §13

No code is added. The three refusal classes of D2 map onto what exists:

| situation | what happens | code |
|---|---|---|
| `--hospes` value unknown, or its row lacks the backend asked for; `--emitte c` without `-o` | usage error, exit 4 / 2, message | none — spec §9.3: a malformed flag is not a diagnostic |
| a module whose `numeri` the C target cannot honour | diagnostic at the driver | `EXS-E0701` — exists in §13 and `diag/codes.inc:111`; unreachable from the C path until a float opcode is lowered |
| a module without `initium` under `--emitte c -o` | a library; accepted | `EXS-E0424` does **not** fire (spec §4.7 as amended) |
| a module without `initium` under `-o` without `--emitte c` | as today | `EXS-E0424` |
| an opcode, width or shape without a C lowering | `bfc: emitter: …`, exit 4 | none — a compiler limit, the reference's own class |

Runtime traps are `exsrt_abortus(N)` with `prelude/README.md`'s kinds
(1 numeric, 5 `terminus`) and are not diagnostics (IR 2.3).

## 5. The harness in one table

| step | reference | C | compared |
|---|---|---|---|
| IR fixture → text | `emit_ir` → `OUT` (fasmg) | `emit_c` → `out.c` | exit status: parity, or `c-emit-exit=` |
| program → text | `exsc … -o out.asm` | `exsc … --emitte c -o out.c` | exit status: parity, or `c-exsc-exit=` |
| text → binary | `fasmg OUT BIN` | `{gcc,clang} × {-O0,-O2}`, UBSan, + shim | a C compile error is a failure |
| run | `run_binary` | `run_binary`, four times | stdout bytes; exit status; trap-or-not and kind |
| directive | `check_run` | `check_run` | each build meets the fixture's own directive too |

## 6. The StreamDB v3 reader — the format as surveyed, and the reader's shape

The format survey is another agent's and landed after the first draft of
this section, which had stated expectations; those are replaced here by
what the survey found. The corpus it certifies against is being vendored at
`vendor/streamdb-v3/` by a third agent (recipe, digests and the upstream
commit in its `PROVENANCE.md`, the discipline of `vendor/hydramodem-tx/`).
Nothing in this section has been run through either backend: `[UNTESTED]`
until C3.

**The container**, little-endian throughout, read on a big-endian machine —
spec §5.2's case exactly, and the reason this is the right first program
for a backend whose `load … minor` is emitted byte-at-a-time (D4 row 40):

| region | layout |
|---|---|
| header | two alternating **128-byte slots** at the front; the newest slot whose CRC-32 over its bytes `[0, 72)` validates wins |
| documents | from offset 256: `[u32 size LE][u32 crc LE][payload]`, one record after another |
| index blob | `u64 count`, then `count` **32-byte** entries `[16-byte UUID][u64 offset][u32 size][u32 crc]`, **sorted by UUID bytes** — so a UUID lookup is a binary search over the blob in place |
| trie blob | a **reversed** trie — keys are indexed last byte first, which is what makes a suffix search a prefix walk — whose nodes are serialised **recursively and inline**: `u64 nchild`, then `nchild` times `(u8 key, node)`, then `u8 tag`, then if the tag says so `u64 16` and a 16-byte UUID, then `u64 subtree_count` |
| checksums | CRC-32/ISO-HDLC everywhere: reflected polynomial `0xEDB88320`, init and final xor `0xFFFFFFFF` |
| **the trap** | an index entry's `offset` addresses the **payload**; the 8-byte record header is at `offset - 8`. The upstream format comment reads the other way and it fails on every document (`streamdb-embedded/README.md`, which paid for it once already) |

**The reader's shape.** Pure functions over caller-supplied byte arrays
(spec §4.1 rule 6: no `poscit`, no `initium`), no allocation, no I/O, no
capability beyond reading its input — which is why it is the right first
program for library mode: it needs nothing D1 withholds. Kiln's
`streamdb_emb_io_t` reads bytes out of ROM and sizes an arena; the
Exsecutor code parses what it is handed. The split, in Kiln's terms:

| Kiln today (`streamdb_embedded.c`) | Exsecutor function (names per spec §3) | the C signature it becomes |
|---|---|---|
| `header_parse` + `read_commit` over the two slots | `caput_lege(b: &acies<u8, 128>) -> Caput`; `caput_valet(b: &acies<u8, 128>) -> u1` (CRC over `[0, 72)`); the "newest valid slot" rule in `caput_elige(a, b) -> u8` | `void exs_caput_lege(unsigned char *ret, unsigned char *b)` — an aggregate return is a hidden `ptr` first (IR 2.9); `uint64_t exs_caput_valet(unsigned char *b)` |
| the index entry (32 bytes) | `indicium_lege(b: &acies<u8, 32>) -> Indicium` with `Indicium` a `@transitus` struct of `u8`×16, `u64:minor`, `u32:minor`, `u32:minor` | `void exs_indicium_lege(unsigned char *ret, unsigned char *b)` |
| the binary search by UUID | `indicem_quaere(b: ptr, n: u64, id: &acies<u8, 16>) -> u64` — index of the entry or `n`; every access through `chk` | `uint64_t exs_indicem_quaere(unsigned char *b, uint64_t n, unsigned char *id)`; `n` is `u32`-typed under `mips64-none-o64` (D2) |
| the record header, at `offset - 8` | `recordum_lege(b: &acies<u8, 8>) -> Recordum` — the `- 8` lives in the *caller*, spelled once, beside a comment naming the trap | `void exs_recordum_lege(unsigned char *ret, unsigned char *b)` |
| `crc32` of a payload | `redundantia32(b: ptr, n: mensura) -> u32` | `uint64_t exs_redundantia32(unsigned char *b, uint64_t n)` |
| the one-pass trie flattening into the arena | `arbor_percurre(b: ptr, n: mensura, out: ptr, cap: mensura) -> mensura` — walks the serialised trie **in order** with an explicit stack in a caller-supplied scratch array (no recursion, so the walk's depth is bounded by the array the caller sized, not by the stack), emitting Kiln's 25-byte flat node `(child_first u32, child_count u16, has_value u8, pad u8, uuid 16)` into `out`; returns the node count or 0 on overflow | `uint64_t exs_arbor_percurre(unsigned char *b, uint64_t n, unsigned char *out, uint64_t cap)` |
| child lookup over the flat array; `find_suffix`'s descent | `arbor_descende(nodes: ptr, keys: ptr, node: u32, key: u8) -> u32` | `uint64_t exs_arbor_descende(unsigned char *nodes, unsigned char *keys, uint64_t node, uint64_t key)` |

Why the flattening is Exsecutor's and the arena is Kiln's: a lookup over
the *serialised* trie is linear in the blob, because a child's inline
subtree has no stored byte length to skip by (`subtree_count` counts
entries, not bytes), so the one pass that turns it into contiguous child
ranges is the reader's real work — and it is a pure function of the bytes
into a caller-sized array, which is the shape D1 admits. Sizing that array
(`streamdb_emb_probe`) stays a C-side estimate from `trie_len`, as it is
today.

**What the language must not need for it.** The same constraint WC §1
stated: the reader may use only what is settled — unsigned integers at
every width, `@transitus` fields with `:minor` (the field access *is* the
byte swap), `aut`/`sursum`/`deorsum`, hex and struct literals, the one
aggregate cast, `per`, `dum … terminus`, `si`/`sin`/`aliter`, `chk` on
every index, and array literals (spec §8.6, since `54ba744`) for the
CRC-32 table. Bitwise and/or are still `[OPEN]` in spec §8.6; CRC-32/
ISO-HDLC's low-bit test is `(c sursum 31) deorsum 31` in `u32` and its
byte extraction `(c sursum 24) deorsum 24`, the shift-discard idiom WC §5
used for the encode basis, so no operator is added. No float, no
recursion, no `mensura` mixed with a fixed width without an explicit
widening (spec §5.2). The one thing the reader may need that the tree does
not yet have: `u64:minor` fields are lowered by `load u64 … minor`, which
`tests/ir/byte_order.ir` runs — nothing new; recorded so it is checked
rather than assumed.

**The certificate** (D7), semantic because containers are not
byte-reproducible: `tests/programs/streamdb_*/` compile one driver over
`examples/streamdb/` from `initium`, read a container from `stdin=` (the
prelude's `Lector`, one byte at a time into a `slot`-backed buffer sized
for the corpus), and write a **stream** in the entry-23 style, sections in
order: (1) per key in index order, the payload's length and the payload's
own bytes, so a mismatch names the key; (2) the document count, the node
count from the flattening, and the suffix-match counts for the corpus's
fixed suffix set; (3) the outcomes of the error cases — a container
truncated at 300 bytes (rejected, format), one payload byte flipped
(exactly one CRC mismatch, naming the key), an absent key (not found) —
as one byte each. The reference backend's stream is compared against a
verification-only Python expectation built from the vendored corpus and
the upstream reader's answers; the C backend's stream is compared against
the reference's (D6). Three mutants, run on copies as WC §6.1 does, each
failing at a named section: the polynomial `0xEDB88320` → `0xEDB88321`
(section 1, key 0: every CRC); `offset` read as the header rather than the
payload, the `- 8` dropped (section 1, key 0: size mismatch on every
document — the trap, made a mutant so the harness proves it is checked);
the reversed trie walked first byte first (section 2: the suffix counts).
A harness on which any of them passes has proved nothing.

## 7. Milestones

| milestone | delivers | retires | its certificate |
|---|---|---|---|
| **C1** skeleton and prologue measurements | (1) the measurement table of D3, filled in, **first**; (2) `compiler/x86_64/backend_c/{emit_c,program_c}.inc` — the 37 lowerings, the 23 refusals by name, the prologue, mangling; (3) `--emitte c`, `-o` required, `--hospes` rows, `CHK_F_PROGRAM` not set; (4) `tests/unit/bfc_emit_*.asm` pinning emitted text per opcode family, `bfc_mangle.asm`, `driver_emitte_c*.asm` for D2's three-way split | D2, D3 (as measured), D4 rows (text), D5 mangling, D1 (the unit compiles) | the hello world's IR through `emit_c`, compiled by `gcc -std=c11 -pedantic -Wall -Wextra` with the shim, prints `examples/saluta.expected` and exits 0 — by hand, recorded in the commit |
| **C2** the differential harness | `tests/ir/emit_c.asm`, `tests/c/exsrt_shim.c`, `run_differential_tests`, `c-emit-exit=`/`c-exsc-exit=`, `checks.test` and the devShell gaining `gcc` and `clang`, the closure assertion | D6, D1 (the unit runs), D5 (byte-identical under `reproduce.sh`'s conditions) | 49 + 22 fixtures × 4 builds agreeing with the reference on all three observables, `nix flake check` green |
| **C3** the reader | `examples/streamdb/` in Exsecutor, `tests/programs/streamdb_*/` driving it from `initium` over `vendor/streamdb-v3/` on stdin, both backends; the Python expectation | D7 (host half), section 6 | the semantic stream of section 6 — every key byte-exact with CRC verified, the counts, the error outcomes — identical under both backends and equal to the expectation; three mutants each failing at the named section |
| **C4** the N64 cross-compile | the `lower/` change of finding 6 (`mensura` from the `--hospes` row), `--hospes mips64-none-o64`, a `checks.n64` that compiles the C3 unit with Kiln's toolchain and gates | D7 (target half), D2's o64 row | the object compiles under Kiln's flags with no `.d` instruction and `nm -u` = `{exsrt_abortus}` (+ `memcpy`); linked into a Kiln test ROM by hand and recorded, not gated `[OPEN]` |

Later, not scheduled: `div`/`rem`/`muls`/`*ov` in both backends; floats in
both (and `EXS-E0701` becomes reachable); whole-program mode with a C
prelude; the nightly `receptio_vec_*` sweep; a narrower carrier on 32-bit
ISAs.

## 8. Findings

Numbered; each names the document and the sentence.

1. **Spec §13 carries `EXS-E0701`, and both the commissioning brief and
   ADR 0012 say it does not.** §13's own note: "`EXS-E0701` opens a `07xx`
   range for target failures … Found by writing ADR 0012 against the
   amended §9.2 … this one was owed." ADR 0012's Neutral section — "that
   needs a spec §13 code, none exists, and none is invented here" — was
   true when written and is stale; `docs/design/runtime.md` 2.7 and
   `checker.md` §2 already cite the code. ADR 0012's body is not edited
   (ADR README: records are not rewritten); its status line now points
   here. D2 uses the code where it applies and invents nothing.
2. **ADR 0012's prologue says `__FINITE_MATH_ONLY__` must be undefined.**
   From memory of GCC's documentation it is always defined — `0` by
   default, `1` under `-ffinite-math-only` — so a `#ifdef` test would fire
   on every build. The prologue tests the value. `[UNTESTED]`; C1's table
   row 2 decides.
3. **The survey's "38 opcodes lowered" is 37.** `fcmp` is dispatched to
   `__bfa_emit_cmp` and refused there by name (`emit.inc:987`). The C
   backend refuses it too, so the parity count is 37 lowered and 23
   refused, with `retain`/`release` counted on the C side as refused for a
   different reason (no runtime) than the other 21 (no reference).
4. **Spec §4.7's `[OPEN]` on library artifacts conflicted with D1** — "no
   library artifact exists for the reference backend … `-o` on a module
   without `initium` … is `EXS-E0424`" — because `-o` is exactly how a C
   unit is written. Amended in this commit, for the C target only: the
   reference's half of the sentence stays true and stays `[OPEN]`.
5. **ADR 0012 item 3's method — "widths below 64 computed in a wider type
   and range-checked" — does not extend to `mul` at widths 33–63**, where
   the wider type is 128 bits and GCC provides `__int128` only where
   `__SIZEOF_INT128__` is defined, which excludes the first consumer's
   target. D4 checks by division (`b > M / a`), exact at every width, and
   uses the reference's own two-check rule (IR 2.2) as the thing it must
   agree with, not as the method. ADR 0012's *result* — parity, no signed
   overflow anywhere — is unchanged.
6. **The lowering maps `mensura` to `u64` unconditionally** (the `per i in
   0..17` loop is "a `u64` loop", WC §8; `lower/ty.inc`). D2's
   `mips64-none-o64` row needs it to be `u32` there, and the checker's
   layout of `mensura`-typed fields likewise. That is `lower/`'s and
   `checker/`'s tree, reported and not done here; C4 waits on it. Until
   then a unit emitted for `x86_64-linux` and compiled for o64 truncates
   every index to 32 bits at the `(uintptr_t)` cast — correct for any
   array under 4 GB and wrong in principle, which is why the prologue's
   `sizeof(void *)` assert exists: that unit does not compile for o64.
7. **A `data $N 0 …` global has no C spelling**: C forbids a zero-length
   array. D4 row 47 emits `[1]`. No fixture has one; `[UNTESTED]` that the
   parser admits it.
8. **`print.inc`'s output helpers (`__bfa_out_lit`, `__bfa_out_byte`,
   `__bfa_out_u64`, `__bfa_out_bytes`, `__bfa_emit_nl`) are `__bfa_`
   internals** by `docs/asm-conventions.md` §4.1, and `backend_c/` is a
   separate tree. The C emitter either duplicates ~200 lines or the five
   are promoted to `bfa_out_*` in `backend_fasmg/` — the latter is
   requested of that tree's owner; C1 duplicates if the request is not
   met, and says so.
9. **IR 2.9's "aggregates by value across `externus` need SysV
   classification, which the C backend inherits"** applies only to an
   `externus` C function taking a C aggregate by value, which no lowering
   emits today. Functions *defined* in Exsecutor never present a C
   aggregate: every aggregate is a `ptr` to bytes in the IR already, so
   Kiln sees pointers in and a hidden pointer out, and nothing is
   classified. The sentence is not wrong; it is about a case that does not
   yet exist.
10. **Spec §9.3's "the C target's runtime calls `setlocale(LC_ALL, "C")` at
    startup"** describes whole-program mode. Library mode has no startup
    and calls nothing; the sentence is untouched and applies when D1's
    later milestone exists.
11. **The reference's `copy n` and C's `__builtin_memcpy` differ on
    overlap** (the reference copies forward in 8-byte steps). Neither
    backend defines overlapping `copy`, the lowering emits none, and the
    differential test cannot compare an undefined case; recorded so no one
    later "fixes" one side to match the other.

## 9. What retires each marker

| decision or claim | milestone | the test as planned |
|---|---|---|
| D3's incantation table, every cell | C1, first task | the scratch unit under `gcc`/`clang` × `-O0`/`-O2` × UBSan, results written into section 8 |
| D4's 37 rows, text | C1 | `tests/unit/bfc_emit_{narrow,bitwise,bytes,phi,call,program}.asm`, exact emitted text, mirroring `bfa_emit_*` |
| D4's 37 rows, semantics | C2 | the 49 IR fixtures and 22 programs under four builds, three observables |
| D4's 23 refusals | C1 | one `tests/ir/reject_c_*.ir` per refusal class with `emit-exit=4` and, for the two runtime refusals, `c-emit-exit=4` against a reference that lowers them |
| D2's three-way split | C1 | `tests/unit/driver_emitte_c_{nohospes,badrow,noout}.asm`; the `EXS-E0701` row stays `[UNTESTED]` until a float opcode exists |
| D5 mangling | C1 | `tests/unit/bfc_mangle.asm` |
| D5 determinism | C2 | `tools/reproduce.sh` extended to diff two `--emitte c` units |
| D6 | C2 | `run_differential_tests` green in `nix flake check` |
| D6's closure assertion | C2 | `nix flake check` with a deliberately added `pkgs.gcc` on `packages.exsc` fails at evaluation (run once by hand, recorded) |
| D7, section 6 | C3, C4 | the reader's certificate; the N64 gates |
| D1's whole-program mode | — | `[OPEN]`, not scheduled |

## 10. Open questions the implementer must answer first

In the order they block:

1. **D3's table.** Nothing else starts until the incantations have run;
   two are suspect from memory already (finding 2, `(-1 & 3)`).
2. **Where `mensura`'s width lives in `lower/` and `checker/`** (finding 6)
   — one site or many? C3 can run on `x86_64-linux` without the answer;
   C4 cannot start without it.
3. **`print.inc`'s output helpers** (finding 8) — promoted, or duplicated?
4. **UBSan and the `index` idiom** — does `-fsanitize=pointer-overflow`
   object to `(unsigned char *)((uintptr_t)p + …)` when the result is in
   bounds? If it does, the row keeps the idiom and the harness adds
   `-fno-sanitize=pointer-overflow` with the reason recorded; it does not
   switch to C pointer arithmetic.
5. **The shim's record layouts** — whether `tests/c/exsrt_shim.c` can
   `#include` a generated header from `interface.inc`'s constants rather
   than restate them (RT H4). A generator is verification-only and
   admissible; the question is whether it is worth its lines for six
   numbers.
6. **`__builtin_memcpy` at `-Os` under `mips64-elf-gcc`** — whether a
   144,000-byte `copy` (`tests/ir/copy_magna.ir`) becomes a `memcpy` call,
   and whether Kiln's link admits it (newlib provides it; the gate forbids
   libm and `malloc`, not `memcpy`).
7. **`-std=gnu2x` and `_Noreturn`** — Kiln's engine dialect is C23-ish,
   where `_Noreturn` is deprecated and `[[noreturn]]` is the spelling;
   whether GCC 14 warns under `-Wall -Wextra`, and whether that matters
   under `-Wno-error`.
