# The C backend — design for spec §9.2's reach backend, library mode first

Status: **C1, C2 and C3's host half are implemented and green, and the
float opcodes are lowered (2026-09-13), and float `load`/`store` with them (2026-09-14).**
`compiler/x86_64/backend_c/` exists (`emit_c.inc`, `program_c.inc`,
`prologue.c.in`); `--emitte c` emits; the incantation table of D3 has been
run under gcc 15.3.0 and clang 21.1.8 and is filled in below, with three
cells that did not hold. The differential harness of D6 runs as two
`tests/run.sh` phases:

- **over `tests/ir/`: 44 of the 58 IR fixtures are lowerable and all 44
  agree with the reference under four toolchains — gcc and clang, `-O0` and
  `-O2`, `-fsanitize=undefined -fno-sanitize-recover=all` — for 176 checked
  builds; the other 14 are rejection fixtures and the C emitter's exit
  status matches the reference's on every one.**
- **over `tests/programs/`: 32 of the 102 directories are eligible and all 32
  agree with the reference on stdout bytes, exit status and trap-or-not with
  the abort kind, under the same four toolchains, for 128 checked builds
  (some directories share a unit with another and
  emit byte-identical C, which is itself checked). The other 70 are the
  `receptio_vec_*` sweep, each declaring `c-differentia=nightly-sweep` in
  its own `TEST` directive: named, never skipped silently.** Nothing in the
  tree is ineligible for the other two reasons the key admits — no unit
  reaches past the shim's six routines, and the C emitter refuses no
  directory.
- **the StreamDB v3 reader is among the 32** (as is the logo renderer, `tests/programs/signaculum/`). `streamdb_{corpus,onus,caput,
  truncus}` produce byte-identical output through the C backend under all
  four builds; section 6.7 is the result.
- **the float opcodes are lowered — D4 rows 24–28, 30–34 and 60.** Eleven
  opcodes (`fadd fsub fmul fdiv fneg fcmp fext ftrunc itof ftoi fconst`),
  one IR op per C statement, so contraction has nowhere to happen by
  construction. Measured on scratch fixtures in `/tmp/floatfix_c/`
  (deliberately not `tests/ir/`, which is the reference backend's tree):
  a 68-check fixture and a 4-check edges fixture (sign of zero, ±inf, no
  trap on division by zero), every expectation computed in Python from
  IEEE 754 — never by running the emitted C as its own reference — green
  under all four toolchains at `-Wall -Wextra` with zero warnings; 15
  emitter refusals at exit 4, each with its message; two emissions of the
  same module byte-identical. The committed differential coverage of
  floats is the `tests/ir/` float fixtures that land with the reference's
  own lowering in the same wave.

C2's work largely landed with C1 because the harness was the only way to
know the lowerings were right; what C2 still owed — the program corpus, and
`tools/reproduce.sh` over two `--emitte c` units — landed with C3.

What is still `[OPEN]` or `[UNTESTED]` is marked where it stands: in
floats, the unordered compare predicates, `fma`, `bitcast`, and the
honouring of a declared `numeri` — both backends emit
default `ad_parem` code whatever is declared, and `EXS-E0701`'s driver
wiring still does not exist (finding 23) — whole-program mode (D1), the
N64 cross-compile (D7's target half), and
the `mips64-none-o64` row (D2, refused by name — see finding 6, which was
wrong about why, and finding 18, which measures how much more than two
constants it is). `spec §N` cites
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
unchanged, and C1 touched **no line of `backend_fasmg/`**.

The include chain is not what this paragraph first said. It said
`backend_c/emit_c.inc` includes `backend_fasmg/verify.inc`, keeping the chain
linear. That is impossible: **`exsc` contains both backends** — `--emitte c`
and a plain `-o` are two flags of one binary — and `lower/ssa.inc` already
includes `backend_fasmg/program.inc`, which includes `emit.inc`, which
includes `verify.inc`. A second `include` of a file full of `struct` and
`proc` definitions fails outright; fasmg has no include guards, and this is
the same diamond `emit.inc`'s own header records paying for once. So the C
backend's chain **hangs off** the existing one rather than duplicating its
tail:

```
(the consumer brings) … → verify.inc → print.inc → parse.inc → ir.inc
                       \
                        program_c.inc → emit_c.inc
```

`emit_c.inc` includes nothing, and states the contract its header: the
consumer must already have `backend_fasmg/verify.inc`, or anything that
brings it. That is exactly what `ir.inc` says of `rt/`. Finding 14. What is
written fresh is the emitter and the unit skeleton — the reference's are
4,623 and 528 lines, which is the size estimate and nothing more.

The constraints, in the order they bind:

1. **The reference defines the semantics** (spec §9.2, ADR 0012). Nothing
   here may lower an opcode the reference does not lower: a C lowering the
   differential test cannot check is a hypothesis with no test, and
   before the float wave the reference's `__bfa_emit_inst` lowered **37**
   of the IR's 60 opcodes —
   `param iconst add sub addw subw mul mulw cmp jmp br ret slot gaddr load
   store loadbits storebits call retain release trap adds subs and or xor
   shl shr zext sext trunc index chk copy addr`, plus `phi` at the edges.
   (The survey that preceded this file counted 38; it counted `fcmp`, which
   the dispatcher routes to `__bfa_emit_cmp` and which that routine refused
   by name, `emit.inc:987`, "floats are not Tier 1". Finding 3.) The float
   wave of 2026-09-13 lowers D4 rows 24–28, 30–34 and 60 — the eleven float
   opcodes — **in both backends in one change**: the reference's half is
   `backend_fasmg/`'s tree and lands in the same wave, and the two
   refusal sets were agreed as ONE set before either was written, so a
   divergence is a finding, not a backend difference (D4). This backend
   lowers **46** of the 60, not the reference's 48: `retain`/`release`
   are the reference's but not library mode's (no object header exists,
   D8), so the C refusals are the reference's twelve plus those two.
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

Retired by: C1 (the unit compiles), C2/C3 (the unit runs against the
reference — **done**: 260 builds across both corpora, every one agreeing).

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
  Nothing is invented. Even with float opcodes lowered (2026-09-13)
  `EXS-E0701` is still not reachable from the C path: the driver consults
  `numeri` nowhere, and both backends emit default `ad_parem` code
  whatever is declared (finding 23) — this row says where the wiring goes
  when the driver grows it.
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
`UINT64_C`), `<limits.h>` (`CHAR_BIT`), `<float.h>` (since the float
opcodes were lowered, 2026-09-13), `_Static_assert`, `_Alignas`,
`_Noreturn`, `goto`. No `<string.h>`, no `<stdlib.h>`, no `<stdio.h>`.
C1's line "no `<float.h>` until a float opcode is lowered" held until
that date; now every unit asserts the IEEE shape — `FLT_RADIX == 2`,
`FLT_MANT_DIG == 24 && DBL_MANT_DIG == 53`, `FLT_MAX_EXP == 128 &&
DBL_MAX_EXP == 1024`, `FLT_EVAL_METHOD == 0` (else `#error`: no x87
extended evaluation), and `FLT_HAS_SUBNORM`/`DBL_HAS_SUBNORM` where
defined — compile-time asserts only (rung (b)), so the freestanding header
a target must provide is `<float.h>` itself, which C11 requires even with
no floating-point hardware.

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
— and its item 2's `fesetround` / `_mm_getcsr` / FPCR reads were to "enter
the prologue with the first float opcode and not before". That happened on
2026-09-13, and what entered is decided by the C1 measurement above:

- **the GCC pragma only**: `#pragma GCC optimize("fp-contract=off")` under
  `#if defined(__GNUC__) && !defined(__clang__)`. The `#pragma STDC`
  spelling is *not* emitted even as documentation of intent — finding 12's
  "may still be emitted" is answered no: a pragma both compilers ignore
  documents nothing, and one compiler accepts it *silently*, which is worse
  than a warning. For Clang the honest state is what finding 12 measured —
  nothing inside the source works — so contraction is prevented by
  construction instead: one IR op is one complete C statement, never an
  `a*b+c` expression for a compiler to fuse.
- **no `fesetround` anywhere**: the emitted code never switches the
  rounding mode. Default `ad_parem` (nearest-even) is C's default and the
  reference's hardcoded value; a declared `numeri` is parsed by both
  backends and honoured by neither (finding 23).
- **no MXCSR write in the prologue** — the control word belongs to the
  program's start, and in library mode the program's start is the shim's
  `main`, not the emitted unit: finding 21. `_mm_setcsr(0x1F80)` sits in
  `tests/c/exsrt_shim.c`, guarded `#if defined(__x86_64__)` because the
  shim also links for non-x86-64 rows.

**C1's first task**, before any emitter line is written: a scratch
translation unit under `prototypes/` (verification-only, never shipped)
holding every incantation above and in ADR 0012, compiled by `gcc` and
`clang` at `-O0` and `-O2` with `-fsanitize=undefined
-fno-sanitize-recover=all`, and a table with one row per incantation, each
cell "held", "rejected" or "no effect" with the compiler version.

**That probe was written and run.** It is
`prototypes/cprologue/{incantations.c,fpcontract.c,run.sh}`, and
`nix develop . --command prototypes/cprologue/run.sh` reproduces the table
below (`pass=22 fail=0` on the assertions the script itself checks; the
fp-contract and fenv/MXCSR rows are readings, not assertions). Measured
**2026-09-12** on `x86_64-linux` under **gcc 15.3.0** and **clang 21.1.8**
(the versions nixpkgs pins in this flake — the design said gcc 14, and
`nix develop` gives 15.3.0; recorded as measured). Base flags
`-std=c11 -Wall -Wextra -pedantic -fsanitize=undefined
-fno-sanitize-recover=all` at `-O0` and `-O2`.

| incantation | what it must do | gcc 15.3.0 | clang 21.1.8 |
|---|---|---|---|
| `#if defined(__FAST_MATH__)` | fire under `-ffast-math`, not otherwise | **held** (`-O0`/`-O2` clean; `#error` under `-ffast-math`) | **held** |
| `#if __FINITE_MATH_ONLY__` | fire under `-ffinite-math-only`, not otherwise, **and be defined at all** | **held** — defined, `0` by default, `1` under the flag | **held** — same |
| `#pragma STDC FP_CONTRACT OFF` | prevent `a*b+c` contracting under `-O2 -ffp-contract=fast` | **no effect** — and GCC says so itself: `warning: ignoring '#pragma STDC FP_CONTRACT' [-Wunknown-pragmas]`. `vfmadd` still emitted, witness contracts | **no effect** — accepted *silently* (no diagnostic at all, even under `-Wunknown-pragmas`) and `vfmadd` still emitted |
| `#pragma GCC optimize("fp-contract=off")` | the belt to that brace, accepted per translation unit | **held** — accepted per TU, no `vfmadd`, witness does not contract | **no effect** — `#pragma GCC optimize` is not implemented by Clang; `#pragma clang fp contract(off)`, Clang's own spelling, was measured too and *also* fails to override a command-line `-ffp-contract=fast`, both at file scope and at the top of the function body |
| `_Static_assert((-1 & 3) == 3, …)` | compile under `-std=c11 -pedantic` | **held** | **held** |
| `_Static_assert(sizeof(void *) == 4, …)` | fire under o64, pass under lp64 | **held** — fires (compile error) on this lp64 host, as it must | **held** |
| `_Alignas(16) unsigned char s[n];` on a local | accepted, `&s[0]` 16-aligned at `-O0` and `-O2` | **held** at both levels | **held** at both levels |
| `_Noreturn void f(unsigned);` | accepted under `-std=c11`, `-std=gnu11`, `-std=gnu2x` | **held** under all three, no `-Wall -Wextra` diagnostic | **held** under all three |
| `__builtin_memcpy(d, s, 144000)` | compile without `<string.h>`; note whether `memcpy` is referenced at `-Os` | **held**; at `-Os` `nm -u` shows **no** `memcpy` reference (inlined) | **held**; at `-Os` `memcpy` **is** referenced — recorded, not forbidden (newlib resolves it; Kiln's gate forbids libm and `malloc`, not `memcpy`) |
| `__has_builtin(__builtin_mul_overflow)` | true on both | **held** — `__has_builtin` defined, and `add`/`sub`/`mul_overflow` and `__builtin_memcpy` all report available | **held** |
| the `exsi_*` helpers of D4, each | no UBSan report | **held** — `checks=63 aborts=19 fails=0` (63 assertions over every helper, 19 of them trapping edges), clean at `-O0` and `-O2` | **held** — identical figures at both |
| `(unsigned char *)((uintptr_t)p + (uintptr_t)i * s)` | no `-fsanitize=pointer-overflow` report in bounds | **held** — `-fsanitize=undefined,pointer-overflow` accepted, no report | **held** |
| `fesetround(FE_UPWARD)` then a folded `1.0/3.0` | whether the fold respects the mode | **no effect** — the fold uses round-to-nearest (`0.33333333333333331483`) while the runtime division under `FE_UPWARD` gives `…37034`; the fold ignores the dynamic mode | **no effect** — identical figures |
| `_mm_getcsr()` FTZ/DAZ | readable at process start | **held** — `0x00001fa0`, FTZ=0 DAZ=0 at start | **held** — identical |

**What the measurement changes.** Three cells were not what was written
from memory, and the first two change the design:

1. **Row 3 is dead.** `#pragma STDC FP_CONTRACT OFF` does nothing in either
   compiler — ADR 0012 suspected GCC and was right, and Clang is no better,
   which ADR 0012 did not suspect. It must not be emitted as if it worked.
   When the first float opcode is lowered the prologue emits row 4's GCC
   pragma **under `#if defined(__GNUC__) && !defined(__clang__)`** and, for
   Clang, has nothing that works from inside the source: the only thing
   measured to prevent contraction under Clang is the command-line flag
   `-ffp-contract=off`, which is exactly the leak spec §9.3 forbids. So the
   honest lowering for Clang is rung (d) — `exsc` refuses a module declaring
   `contractio explicita` when the pinned toolchain is Clang — or rung (b),
   an `#error` on Clang when such a module is compiled. Neither is written
   now: no float opcode is lowered (D8), so nothing depends on it yet. It is
   recorded here as **finding 12** so the later milestone starts from the
   measurement instead of the memory. The `#pragma STDC` line may still be
   emitted as documentation of intent, but never counted as a defence.
2. **Row 2's correction was right** (finding 2): `__FINITE_MATH_ONLY__` is
   always defined, so ADR 0012's `#ifdef` test would have fired on every
   build. The prologue's value test is correct, and the design's suspicion
   is now a measurement.
3. **`memcpy` at `-Os` differs between the two compilers** — GCC inlines a
   144,000-byte `__builtin_memcpy`, Clang emits a call. Open question 6 is
   therefore already half-answered for the host compiler: a `copy` that big
   *does* become a call under at least one compiler of the family, so Kiln's
   link must admit `memcpy`. The `mips64-elf-gcc -Os` half stays
   `[UNTESTED]` until C4.

Everything else in the table held exactly as written, including the two
`_Static_assert`s the design flagged as suspect-by-construction and the
`index` idiom under `-fsanitize=pointer-overflow` (open question 4: **no
report**, so the row keeps the idiom and the harness needs no
`-fno-sanitize=pointer-overflow`).

Rejected: `-std=c99`. `_Static_assert`, `_Alignas` and `_Noreturn` are C11,
and each replaces a GCC attribute that would otherwise be on the extension
list; both consumers' dialects (`gnu11`, `gnu2x`) include them.

Retired by: C1's measurement table — **retired**, above.

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

**Floats are the deliberate exception to the carrier.** A float value is
the C float itself — `float` for `f32`, `double` for `f64` — never packed
into the `uint64_t`. The carrier exists so that C's *integer* semantics
can be made to match the reference's by construction; there is no integer
spelling of an IEEE multiply, so the honest C statement for `fadd f64` is
`vk = a + b;` over two `double`s. One IR op is exactly one complete C
statement, which is also the contraction defence: `fmul` and `fadd` are
separate statements, so no compiler is ever shown an `a*b+c` expression
to fuse, whatever its `fp-contract` mode (finding 12). The prologue
asserts `FLT_EVAL_METHOD == 0` so a `float` is evaluated as a `float`, and
the float rows assume the MXCSR the shim sets at `main` (finding 21).
`numeri` is parsed and ignored — default `ad_parem` code is emitted
whatever is declared, in this backend as in the reference (finding 23);
spec 5.4's "a C target that cannot honour a declared `numeri` fails the
build" is not yet wired to `EXS-E0701` in the driver (D2's row, section 4).

**The refusal set is ONE set.** The two emitters' by-name refusals are
maintained as a single set — `fma` and `bitcast` are refused here and in
`backend_fasmg/emit.inc` in the same change, and a divergence between the
two refusal sets is a finding, not a backend difference. The operand-level
float refusals (inline immediates, float load/store, `iconst` with a
float type, `cmp`/`fcmp` type disagreement, `fconst` patterns too wide
for `f32`, `itof u64 → f32`) mirror the reference's row for row.

**The mapping table**, one row per opcode in `ir.inc` order (`BFA_OP_*`,
1–60). `T` is the instruction's type, `N` its width, `M` the mask
`exsi_mask(N)` (`UINT64_MAX` at 64, else `(1 << N) - 1`), `S` the sign bit
`1 << (N-1)`. Value `%k` is `vk`, block `bk` is label `bk`, a `slot`'s
storage is `sk`, a phi's edge temporary is `tk`; all are internal ids
(IR 2.1, creation order), declared at the top of the function in id order.
For the float rows, `F` is the C spelling of the type — `float` for
`f32`, `double` for `f64` — and `vk` has that type, not `uint64_t`.
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
| 24 | `fadd F a b` | `vk = a + b;` — one IR op, one C operator; `a` and `b` must be class-3 (float) and values, or refused by name | lowered |
| 25 | `fsub F a b` | `vk = a - b;` | lowered |
| 26 | `fmul F a b` | `vk = a * b;` | lowered |
| 27 | `fdiv F a b` | `vk = a / b;` — a zero divisor is IEEE ±inf or NaN, never a trap: spec 5.4's "no float op traps" is C's own rule here; measured (finding 22): neither compiler's `-fsanitize=undefined` includes float-divide-by-zero on this host, so the harness flags do not make it one | lowered |
| 28 | `fneg F a` | `vk = -a;` — the sign bit flipped, exact on every value including NaN and ±0 | lowered |
| 29 | `fma F a b c` | refused: `bfc: emitter: fma: no lowering in EITHER backend -- the reference's SSE2 baseline has no FMA instruction, and the one C spelling would be <math.h>'s fma, a library the dialect does not promise; contraction is the thing spec 5.4 says must be asked for, never made` | refused |
| 30 | `fcmp.p F a b → u1` | five predicates are C's own: `vk = (a OP b);` with `== < <= > >=`. `ne` is **not** C's `!=`, which is unordered-NE (true on a NaN) — ordered-NE is `vk = ((a < b) \|\| (a > b));`, so all six are false on a NaN (spec 5.4's ordered meaning). Refused, each by name: an unordered predicate word (`fcmp: ordered predicates only (eq ne lt le gt ge; unordered forms are [OPEN], ssa-ir.md 2.3)`), an inline immediate operand (`fcmp: float operands are values only (fconst is the leaf -- the raw bit pattern, never decimal text)`), and a type disagreement with the opcode (`cmp.*` on floats or `fcmp.*` on integers: `cmp/fcmp: the compare and its operands must agree`) | lowered |
| 31 | `fext F a` | f32→f64: `vk = (double)a;` — exact. Anything else refused: `bfc: emitter: fext: f32 -> f64 only (narrowing and equal width are ftrunc)` | lowered |
| 32 | `ftrunc F a` | **the narrowing cast, not a fraction truncation** — the name names the width, not the mantissa (finding 20). f64→f32: `vk = (float)a;` (round to nearest even); equal width: `vk = a;` — a move, not a re-round. f32→f64 refused: `bfc: emitter: ftrunc: the source must be the same width or wider (f32 -> f64 is fext)` | lowered |
| 33 | `itof F a` | source unsigned: `vk = (T)a;`; source signed: `vk = (T)exsi_i64_from_bits(a);` — the bit-cast helper, because `uint64_t → int64_t` is implementation-defined in C and the reference defines it as the two's-complement reinterpretation. `u64 → f32` refused: `bfc: emitter: itof: u64 -> f32 is not implemented (the reference refuses it too: no correctly-rounded one-step sequence at its baseline -- a naive (float)(double) double-rounds)`; `u64 → f64` is lowered | lowered |
| 34 | `ftoi T a` | dest `uN`: `vk = (uint64_t)a;`; dest `iN`: `vk = (uint64_t)(int64_t)a;` — C's float→integer conversion truncates toward zero, the reference's `cvttsd2si` rule. NaN and out-of-range are `[OPEN]` in the reference; on the C side UBSan's float-cast-overflow aborts them under the harness flags — the documented C-side twin of the same `[OPEN]`, said so in a comment at the emission site | lowered |
| 35 | `bitcast T a` | refused: `bfc: emitter: bitcast: nothing in-tree produces it and its semantics are [OPEN] (ssa-ir.md 2.3); fconst's bits-to-float path is a prologue helper, not this opcode` | refused |
| 36 | `redinit F op shape w` | refused: `bfc: emitter: redinit: reductions have no reference lowering (ssa-ir.md 2.6: the reference's lowering of arborea is the definition)` | refused |
| 37 | `contrib h v` | refused, as `redinit` | refused |
| 38 | `redfin F h` | refused, as `redinit` | refused |
| 39 | `slot n align → ptr` | at function top: `_Alignas(align) unsigned char sk[n];`; at the instruction: `vk = sk;`. Alignment a power of two 1..16, as the reference requires | lowered |
| 40 | `load T p off o` | integer `T`, whole bytes `k = N/8`: `nativus` → `vk = normT(exsi_ld_n(p + off, k), N)`; `maior` → `exsi_ld_be`; `minor` → `exsi_ld_le`; `k == 1` all three are one byte. `T = ptr`: `vk = (unsigned char *)(uintptr_t)exsi_ld_n(p + off, sizeof(void *))`. Not whole bytes: refused, as the reference does. `T = f64/f32`, `nativus` only (the verifier refuses an order before either backend): `vk = exsi_f64_from_bits(exsi_ld_n(p + off, 8))` / `exsi_f32_from_bits((uint32_t)exsi_ld_n(p + off, 4))` — the bits through the integer byte helper and back through the `__builtin_memcpy` bit-cast, both halves byte-order-explicit; a float's IR width field is 0, so the byte count comes from the kind | lowered |
| 41 | `store T p off o v` | `exsi_st_n(p + off, k, v)` / `exsi_st_be` / `exsi_st_le`; `ptr`: `exsi_st_n(p + off, sizeof(void *), (uintptr_t)v)`; `T = f64/f32`, `nativus` only: `exsi_st_n(p + off, 8, exsi_bits_from_f64(v))` / `…, 4, exsi_bits_from_f32(v))` — bit baggage exactly as the reference's `mov` (no arithmetic, so NaN payloads, −0.0 and subnormals survive; pinned by `tests/ir/float_mem.ir` on four toolchains) | lowered |
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
| 60 | `fconst F bits` | `vk = exsi_f32_from_bits(UINT64_C(lo));` / `vk = exsi_f64_from_bits(UINT64_C(pattern));` — the two extra words are the raw bit pattern, exactly `iconst`'s mechanism, and the round-trip is exact by construction: a C float literal cannot spell a NaN payload, −0.0 or a subnormal, so the bits go through a `__builtin_memcpy` bit-cast helper (D3's extension set) rather than through decimal text. A pattern with bits at or above 2^32 on an `f32` is refused: `bfc: emitter: fconst: the pattern is not a value of f32 (bits at or above 2^32)`; an integer type is refused: `fconst: the type must be a float (f32/f64) -- iconst is the integer leaf (ssa-ir.md 2.3)` | lowered |

Not in the table: `nop` (op 0; verifier rule 9 refuses it, `verify.inc`
finding 2) and `faddr` (IR 2.3, `[UNIMPLEMENTED]` in `ir.inc`); both are
refused by the emitter if met, by name.

Count: **46 of the 60 rows are lowered** — 45 by statement, `phi` at the
edges — and **14 are refused**, of which 12 because the reference refuses
them too (`div`, `rem`, `muls`, the three `*ov`, the three reductions,
`callind`, and now `fma` and `bitcast`, refused in both backends in the
same change) and 2 (`retain`, `release`) because library mode has no
runtime. The float wave of 2026-09-13 moved eleven rows from refused to
lowered. A row-by-row recount in that change also corrected the figures
this paragraph carried before it (37 lowered / 23 refused): counted row
by row the pre-float table was 35 lowered / 25 refused, both wrong by
two, and the corrected pre-float figures are 34 by statement + `phi` =
35 / 25. The recount is recorded, not smoothed over. None of the 14
refusals occurs in any of the 49 `tests/ir/*.ir`
fixtures or the (then 22, now 26) non-vector `tests/programs/` directories
(measured by grep at `dbe1d64`; re-measured in C3 by *running* the
emitter — `exsc … --emitte c` exits 0 on all 26, so not one of them is
narrowed by a refusal, which is a stronger statement than the grep's).

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
C2/C3 (the differential test runs every row the corpus reaches — **done**:
the 39 lowerable IR fixtures and the 26 eligible programs, four builds each).

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
| prologue, `--hospes` assert, globals, prototypes, definitions | fixed; **globals precede prototypes**, not the other way round as this table first said — globals must precede the *definitions* that name them and are independent of the prototypes, so the one real constraint holds either way, and emitting them before a single call into `bfc_emit_module` keeps that routine self-contained enough for a unit fixture to pin on its own |
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
different C names), C3 (`tools/reproduce.sh`, **done** — see below).

**Determinism, measured.** `tools/reproduce.sh` now emits two units under
its two condition sets — differing cwd, the absolute path of every source,
`TZ`, `LC_ALL`/`LANG`, `SOURCE_DATE_EPOCH`, `umask`, and hostname (via an
unprivileged UTS namespace) — and `cmp`s them: the hello world's 8,691 bytes
and the StreamDB reader's **137,742** bytes, both byte-identical, 2026-09-12.
The StreamDB unit is the one that matters: 5,929 lines over 22
functions; a `@transitus` struct per record and `acies` slots up to 65,536 bytes is the
first unit in the tree large enough for an ordering that depended on an
address or a hash bucket to have had somewhere to hide. Two mutations prove
the check is not vacuous: one byte changed in condition set B's copy of
`saluta.exsc` fails the `cmp`, and an emission that writes no unit at all
fails the empty-file guard rather than passing as "two identical absences"
— the vacuous-diff failure mode `tools/reproduce.sh`'s own header already
records having shipped once.

A second, independent determinism check rides in the differential phase:
sixteen of the 26 eligible program directories name the same `sources=` as
another (the four `streamdb_*`, the four `receptio_*`, three
`hydramodem_*`), and each is required to emit a unit byte-identical to the
first directory that named that unit. That is D5 inside one process where
`reproduce.sh` is D5 across two.

### D6 The differential test is the acceptance gate, and the toolchain stays in the test closure

**What is compared.** ADR 0012's three observables, for one input under
both backends: the bytes written to stdout, the exit status, and
trap-or-not — and when both trap, the **kind** `N` of `abortus N` on fd 2,
since `tests/run.sh`'s `abort=` key already distinguishes kinds and a `chk`
trap (1) reported as a `terminus` trap (5) is a wrong program. Stderr is
otherwise not compared: the reference's prelude and the shim below write
different English before `abortus`, and the English is not promised
(`prelude/README.md`).

**Over which corpus.** Every `tests/ir/*.ir` (49 at `dbe1d64`, 50 now) and
every non-`receptio_vec_*` `tests/programs/` directory (22 then, **26**
now — the four `streamdb_*` landed with section 6). Every fixture keeps its
one `; TEST:` directive: the same `expect-exit=`, `abort=`, `stdout=`,
`stdin=` apply to the C build, so a fixture whose C build meets its own
directive but differs from the reference is caught twice.

**How the 70 are excluded, and on what grounds — corrected by
measurement.** This paragraph said the `receptio_vec_*` sweep "take minutes
under the reference". They do not take minutes under the C build: one such
run is **56 / 30 / 70 / 26 ms** (gcc/clang × `-O0`/`-O2`, measured
2026-09-12), so all seventy would be about 13 s of runs. The exclusion
stands on a better reason, found by building them: **all seventy name the
identical `sources=` as `receptio_exemplum`** — only `stdin=` differs — so
what they would add is 280 more runs of four binaries this phase already
builds and checks, not one more lowering and not one more emitted line.
They remain out of the per-commit gate, listed as a nightly run `[OPEN]`.

**Exclusion is declared, never inferred.** Each of the seventy carries
`c-differentia=nightly-sweep` in its own `TEST` directive; a directory with
no such key is eligible and its four builds must agree. There is no glob in
`tests/run.sh` that passes over a name. The value set is closed —
`nightly-sweep`, `prelude-beyond-shim` (the unit imports an `exsrt_*`
routine the shim does not define, so it would not link), `emitter-refusal`
(`exsc --emitte c` refuses the module by name) — and an unrecognised value
fails the fixture rather than silently becoming a new reason. The last two
are unused today: every eligible unit's imports are within the shim's six,
and the emitter refuses none of the 32.

**How it runs**, as a fifth phase `run_differential_tests` of
`tests/run.sh` after `run_program_tests` — one phase with two loops and two
banners, not two phases: the loop over programs needs the `exsc` binary the
IR loop already assembled for D2's driver rows, the same compiler-presence
check, and the same `$cflags` with the two suppressions finding 15 argues
for, and a sixth phase would be a third `fasmg exsc.asm` in one run plus a
second copy of that reasoning. The steps:

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
   and `int main(void) { return (int)exs_initium(&mundus); }` — plus,
   since the float opcodes were lowered, one line before the call:
   `_mm_setcsr(0x1F80u)` under `#if defined(__x86_64__)` (with
   `<xmmintrin.h>` under the same guard). The reference's `program.inc`
   writes MXCSR 0x1F80 at program start; a C process on this host
   *measured* 0x1FA0 at start — the same six masks already set plus a
   stale precision flag (finding 21) — so the shim makes the two programs
   start from the same control word or the differential test would compare
   an `ad_parem` run against an `ad_parem` run with a different PE flag.
   The guard keeps the shim compiling for non-x86-64 rows, where the
   write is not this backend's to make.
   The four `exsrt_alloc_*` routines are **not** defined: a fixture that reaches
   them fails to link, which is the same statement `emit_ir.asm` makes by
   fixing the closure.

   **C3 added nothing to it.** The reader drives `Lector` and `Scriptor`
   from `initium` and needed no routine the shim did not already have: the
   six it defines are `exsrt_mundus_ambitus`, `exsrt_scriptor_ad_exitum`,
   `exsrt_lector_ab_introitu`, `exsrt_scriptor_scribe`,
   `exsrt_scriptor_scribe_octeto` and `exsrt_lector_lege_octeto`, and the
   external closure of every one of the 26 eligible units is a subset of
   those plus `exsrt_abortus` (measured by reading the `extern` prototypes
   out of all 26 emitted units). Mutation, run: make
   `exsrt_lector_lege_octeto` report end-of-input at once and `lector`,
   `lector_numerus`, all four `receptio_*` and all four `streamdb_*`
   directories fail across all four builds — so the reader's whole result
   really does come through this one routine.
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
6. **Floors, two of them, over the program loop.** `DIFFERENTIAL_PROGRAM_
   FLOOR` (26) counts directories that were *eligible and whose four builds
   all agreed*; `DIFFERENTIAL_PROGRAM_BUILD_FLOOR` (104) counts builds run
   and checked. Neither alone suffices: a directory quietly marked
   ineligible removes four builds and four agreements together, so a floor
   on builds would not name it and a floor on agreements would not catch a
   build that stopped running. A third check says so directly — the count of
   eligible directories and the count of agreeing directories must be equal.
   Mutation, run: adding `c-differentia=nightly-sweep` to `streamdb_corpus`
   drops both floors (25 < 26, 100 < 104) and fails; an unrecognised
   `c-differentia=` value fails the fixture by name.

**Cost, measured.** `tests/run.sh` went from **179.4 s to 226.3 s** on this
host (2026-09-12), of which the program loop is **46.1 s** — a 26% increase,
not a doubling, so no sampling rule is proposed. Where it goes: 18 distinct
units compiled four ways (`streamdb_*`'s unit is ~1.3 s under gcc `-O2` and
~2.0 s under clang `-O2`; `receptio_circuitus`'s is ~1.0 s), plus 104 runs
of which `receptio_circuitus` alone is **6.3 s at gcc `-O0` and 8.8 s at
clang `-O0`** — UBSan at `-O0` over the receiver's inner loops, and the
single largest item in the phase. The StreamDB reader is cheap by
comparison: its 80,300 one-byte syscalls over the 47,708-byte corpus cost
**44–57 ms** per run at every level under both compilers, so the syscall
count that section 6.3 flagged is not what makes this phase expensive.
If it ever must be cut, the honest cut is `receptio_circuitus` to one
`-O2` build with the reason recorded, not a silent sample.

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

Retired by: C2 (the IR corpus) and C3 (the program corpus, the StreamDB
certificate, and the two floors) — **done**.

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

**Both host halves are done and section 6 reports them, not a plan.**
`examples/streamdb/{lector_streamdb,probatio}.exsc` and
`tests/programs/streamdb_{corpus,onus,caput,truncus}/` run under the fasmg
backend and agree with `vendor/streamdb-v3/expectation.json` on all four
containers, with five mutants run (sections 6.3, 6.4); and the same two
sources through `--emitte c`, compiled by gcc 15.3.0 and clang 21.1.8 at
`-O0` and `-O2` under `-fsanitize=undefined -fno-sanitize-recover=all`
against `tests/c/exsrt_shim.c`, produce **byte-identical output on all four
containers under all sixteen builds** (section 6.7). What stays `[UNTESTED]`
is exactly the target half: the `mips64-elf-gcc -mabi=o64` compile and
Kiln's gates, which are C4's and which `--hospes mips64-none-o64` does not
yet accept (finding 18).

### D8 Not in scope for C1–C4

Floats were this section's first item until 2026-09-13, when D4 rows
24–28, 30–34 and 60 landed in both backends together; float `load`/`store`
(`nativus`, the value as its raw bit pattern) followed on 2026-09-14 with
the signaculum stage, closing the last float gap `ssa-ir.md`'s `@dot`
example tripped on (finding 24). What remains outside
the float lowering, each a refusal by name or a recorded `[OPEN]`, never
a silent omission: the unordered `fcmp` predicates (`ssa-ir.md` 2.3 marks
them `[OPEN]`; the emitter refuses the predicate word by name); `fma` and
`bitcast` (D4 rows 29 and 35); a float `load`/`store` under `maior` or
`minor` — byte order is an integer surface (spec 5.2), refused by the
verifier and by both emitters (`tests/ir/reject_verify_float_load_ord.ir`);
the honouring of a declared `numeri` (finding 23); the three reduction
opcodes — what `@dot` still trips; `div`/`rem`, `muls`,
the three `*ov` predicates — integer opcodes the
reference lacks, which enter **both** backends in one later milestone
with fixtures for each; ARC — no object header exists in library mode;
generics and dictionaries; `callind`;
whole-program mode (D1). Each is a refusal by name in D4's table, never a
silent omission.

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
| a module whose `numeri` the C target cannot honour | diagnostic at the driver | `EXS-E0701` — exists in §13 and `diag/codes.inc:111`; still unreachable from the C path even with float opcodes lowered, because the driver wiring does not exist: `run.inc` performs no `numeri` check, and neither backend honours a declared `numeri` — both emit default `ad_parem` code whatever is declared (finding 23). Wiring it is the driver's tree, not this backend's |
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
| eligibility | every directory | every directory without `c-differentia=` | the ineligible are named with their reason; two floors and an equality check say none went missing |
| unit sharing | — | directories with the same `sources=` must emit byte-identical C before a build is reused | D5, inside one process |

**As built, 2026-09-12.** 156 IR builds + 104 program builds = **260**, all
agreeing; 11 IR rejections at matching exit status; 70 directories
ineligible by name; the phase costs 46.1 s of the suite's 226.3 s.

## 6. The StreamDB v3 reader — built, run and certified on both backends

**Status: the whole host half of D7 is done.** `examples/streamdb/` holds the
reader and its driver, `tests/programs/streamdb_{corpus,onus,caput,truncus}/`
run it over `vendor/streamdb-v3/` on the **reference (fasmg/x86-64) backend**
— sections 6.1 to 6.6 — **and on the C backend**, where the same two sources
through `--emitte c` produce the identical four streams under four
toolchains: section 6.7. Every figure below was measured on this tree. What
is still `[UNTESTED]` is the N64 cross-compile, which is C4's. This section
previously stated a design; it now states what the design became, and names
where the two differ.

**The container**, re-verified byte by byte against `corpus.streamdb` rather
than read off a header comment:

| region | layout |
|---|---|
| header | two alternating **128-byte slots** at the front; the newest slot whose magic, version, own CRC-32 over `[0, 72)` and bounds all validate wins |
| documents | from offset 256: `[u32 size LE][u32 crc LE][payload]`, one record after another |
| index blob | `u64 count`, then `count` **32-byte** entries `[16-byte UUID][u64 offset][u32 size][u32 crc]`, **sorted by UUID bytes** — a binary search over the blob in place |
| trie blob | a **reversed** trie — keys indexed last byte first, which is what makes a suffix search a prefix walk — serialised **recursively and inline**: `u64 nchild`, then `nchild` times `(u8 key, node)` ascending by key, then `u8 tag`, then if the tag says so `u64 16` and a 16-byte UUID, then `u64 subtree_count` |
| checksums | CRC-32/ISO-HDLC: reflected polynomial `0xEDB88320`, init and final xor `0xFFFFFFFF` |
| **the trap** | an index entry's `offset` addresses the **payload**; the 8-byte record header is at `offset - 8`. The `- 8` is spelled once, in `documentum_proba`, and mutant 1 below proves it is checked |
| **the zero pads** | bytes `[36, 40)` and `[60, 64)` of a slot, and `[76, 128)`. The survey did not list them; `@transitus` forbids implicit padding, so declaring the slot forced them into the open |

### 6.1 `@transitus` carried it, and that is the section's main result

Every fixed-shape record is a `@transitus` struct with `:minor` fields reached
through §5.2's one aggregate cast. **There is not one shift and not one mask
anywhere in the reader for a header field, an index entry or a record
header**, and nothing in the file assumes the host's byte order — `Caput.ordo`
is a `u64:minor` field on any machine. Two places pay for themselves twice:

- **`Caput` is all 128 bytes**, twenty fields, the two zero pads and the
  52-byte tail included, because §5.2 admits no implicit padding. The pad was
  in the format and is now in the type.
- **`Indicium` declares the UUID as `idm: u64:maior` + `idn: u64:maior`.** The
  index is sorted by UUID *byte* order, so comparing the two big-endian halves
  as unsigned integers **is** `memcmp` on sixteen bytes: the binary search has
  no byte loop at all. A `:minor` pair would have given the wrong order. This
  is the sharpest evidence in the tree that the annotation is load-bearing
  rather than decorative.

Where it stops: a trie node is variable-length and has no `@transitus` shape.
Its three `u64`s are read through `Octo { valor: u64:minor }` — the one-field
view `Syndroma` already is in entry 23 — so even there the order is the
declaration's. The alternative, a hand-written byte-wise `rd_u64`, exists in
neither file.

### 6.2 What was built, against the table this section used to hold

| this section's design | what exists | why it differs |
|---|---|---|
| `caput_lege(b: &acies<u8, 128>) -> Caput` | `caput_lege(b: acies<u8, 65536>, s: mensura) -> Caput` | there are no slices and `sicut` needs an exactly-sized `acies`, so each reader copies its fixed window out of the container into a local and casts that. `&T` is not written anywhere (finding 15) |
| `caput_valet(b) -> u1` | `caput_valet(b, s, longitudo) -> u8` | `u1` is a `@transitus` field width, not a value type |
| `caput_elige(a, b) -> u8` | `caput_elige(b, longitudo) -> mensura` — 0, 128, or 256 for "no commit" | it chooses **and** validates, so it takes the container rather than two parsed headers |
| `indicium_lege`, `recordum_lege`, `indicem_quaere`, `redundantia32`, `arbor_descende` | as designed, with the container and an offset in place of a slice | — |
| `arbor_percurre(b, n, out, cap) -> mensura` writing a caller array | `arbor_percurre(b, s, n) -> Arbor`, returning the flat trie **by value** | **finding 12 below.** A parameter is borrowed (§6.3 decision 3); a function cannot fill an array it was handed. `receptor.md` finding 11 is the same gap |
| — | `clavem_quaere`, `documentum_quaere`, `documentum_proba`, `suffixum_percurre` | the lookup, the verify and the suffix walk the table did not name |
| one `suffixum_quaere(a, b, s, d, clavis, n)` | `suffixum_percurre(a, clavis, n) -> Nodi`, the caller resolving each node | six parameters plus the hidden result pointer is seven words, one more than a call passes — WC finding 8, met again. The split is the better shape anyway |

The 25-byte flat node became six parallel arrays in one `Arbor` value
(`primus`, `numeri`, `habet`, `claves`, `idm`, `idn`), because an `acies` of a
`@transitus` struct would have had to hold the UUID and a `u64:maior` pair
reads it in the order the index compares.

**Siblings adjacent is the whole trick** (`streamdb_embedded.c:186-196`, and
mutant 3 below). A node fills a slot its *parent* assigned it and
bump-allocates a contiguous range for its own children **before** descending
into any of them. Allocate each child's slot at the moment you descend instead
and the array is still the right size, the node count is still right, both
CRCs still pass, and every multi-child lookup silently misses.

The walk is **iterative with an explicit stack** — five parallel `mensura`
arrays 1025 deep, one more than the format's 1024-byte maximum key — so the
depth bound is the array's and not the machine stack's.

**Stricter than the reference, deliberately: `subtree_count` is verified.**
Upstream writes the number of value-carrying nodes in the subtree, inclusive —
re-measured over both of the corpus's commits, 373 nodes and 316 nodes, every
stored count equal to its subtree's value count. `streamdb_embedded.c:181-183`
and `:242-243` read the field and throw it away. Verifying it rejects
hand-corrupted blobs the reference accepts, and would refuse a writer that
ever disagreed with this reading.

### 6.3 The certificate, and what it measured

Four directories, one driver, four containers; the driver writes a byte
stream and `tests/run.sh` compares it with `cmp` against a committed
`expected.out`. `tests/programs/streamdb_corpus/expecta.py` builds those three
files from the vendored corpus — an independent Python reading of the format
that asserts itself against `expectation.json` (every payload against
`payloads/`, every size and crc32 against the manifest, both suffix orders
against the captured traversal) before it writes a byte. It is
verification-only and `tests/run.sh` never calls it.

| directory | container | stream | result |
|---|---|---|---|
| `streamdb_corpus/` | `corpus.streamdb` | 32,591 bytes | exit 0; 24 documents, 373 trie nodes, slot 128 (seq 3); all 24 keys verdict 0 with their **payload bytes inline**; `.t3dm` 5 matches, `.bin` 4, in the manifest's captured order |
| `streamdb_onus/` | one payload byte flipped | 29,988 bytes | exit 0; `models/asset_010.t3dm` alone carries verdict 2 (checksum mismatch); the other 23 byte-exact. The difference from the good stream is exactly that document's 2,603 payload bytes plus its one verdict byte |
| `streamdb_caput/` | one header byte flipped | 25,099 bytes | exit 0 **against the older commit**: seq 2, 20 documents, **316 trie nodes**, slot 0; `asset_020..023` verdict 1 (not found); `.t3dm` drops 5 → 4 with the other four unmoved |
| `streamdb_truncus/` | `head -c 300` | 0 bytes | exit 1, nothing written: both slots' bounds fail against a 300-byte file |

Each of the three streams' every byte agrees with `expecta.py`'s, and
`expecta.py`'s agrees with `expectation.json` wherever the manifest speaks.

**Measured, on this tree.** The corpus is **47,708 bytes** — an earlier
statement of 316 KB was wrong. `Lector.lege_octeto` reads one byte per
`read(2)`, so the program makes 47,709 reads and 32,591 writes: **80,300
syscalls**, **0.044 s / 0.044 s / 0.043 s wall** over three runs with stdout
to `/dev/null`. That is a thirtieth of the suite's slowest existing directory
(`receptio_circuitus/`, 1.4 s), so **no buffered read was added** — the
prelude has none to call in any case, and adding one is the prelude owner's
change. `exsc` emits 212,143 bytes of fasmg for the two sources in 0.2 s;
`fasmg` makes a 45,507-byte binary of it. The syscall audit passes: the
binaries' surface is within `{Mundus, ambitus}`.

### 6.4 Mutants: predicted, then run

Each on a temp copy of the two sources, compiled and run; **predicted before
observed**, and the two agreed in every case.

| # | mutation | predicted | observed | does the corpus catch it? |
|---|---|---|---|---|
| 1 | the `- 8` on the record header dropped | every document fails the header-vs-index check | all 24 verdict 3, stream 211 bytes, first differing byte at stream offset 17 — key 0 | **yes** |
| 2 | the reversed trie walked first byte first | no key found, no suffix matched | all 24 verdict 1, `.t3dm` 0, `.bin` 0, stream 139 bytes | **yes** |
| 3 | child slots allocated at descent, so siblings are not adjacent | the counts still right, the lookups gone, no checksum disturbed | 24 documents, **373 nodes**, slot 128 — all correct — and all 24 verdict 1, both suffix counts 0 | **yes, and only the certificate does** |
| 4 | the wrapping bound `off + len > file_len` | the vendored corpus cannot tell the two forms apart | byte-identical on `corpus.streamdb`. It needs a hand-made header (slot 1's `trie_off = 2^64 - 1`, `trie_len = 2`, slot CRC recomputed), where the correct reader falls back to the older commit and writes 25,099 bytes, exit 0, and the mutant accepts the slot and aborts | **no** |
| 5 | the index-blob CRC not checked | the vendored corpus cannot tell, since its index CRC passes | byte-identical on `corpus.streamdb`. It needs a hand-flipped UUID byte in the index blob, where the correct reader exits 2 writing nothing and the mutant exits 0 writing 30,237 bytes with `data/asset_009.bin` not found and `.bin` 3 instead of 4 | **no** |

**What the certificate therefore does not prove.** It does not exercise the
*form* of the bounds arithmetic (mutant 4) or the index CRC (mutant 5): both
need inputs no honest writer produces, so the vendored corpus — which is
writer-produced on purpose — cannot contain them. It also does not cover a
zero-length payload (the reference writer refuses to make one,
`PROVENANCE.md`), a key over 24 bytes, a trie deeper than 24, a container over
65,536 bytes, or more than 2,048 trie nodes.

**A result worth its own line: mutant 4 will not compile into the reference's
bug.** Written with plain `+`, `off + len` on `u64` **traps** — `abortus 1`,
§6.6's shape — so the reference reader's wrapping comparison is *not
expressible* in Exsecutor without asking for it in as many words (`+%`).
`streamdb_embedded.c:120-122` and `:375-378` are written in the form C makes
silent and this language makes loud.

### 6.5 What the language needed, and the five gaps it found

Used and sufficient: unsigned integers at every width, `@transitus` fields
with `:minor` and `:maior` (the field access **is** the byte order), the one
aggregate cast, array literals in both forms, `per`, `dum`, `rumpe`, `perge`,
`si`/`sin`/`aliter`, `sicut`, `aut`, `sursum`/`deorsum`, and `chk` on every
index. No float, no recursion, no division or remainder — `lo + ((hi - lo)
deorsum 1)` is every midpoint and a two-slot header is a comparison, not a
`% HEADER_SLOTS` — no bitwise and/or (§8.6 still `[OPEN]`; the CRC's low-bit
test is `(c sursum 31) deorsum 31` and its final xor is `aut 0xffffffff`), and
no CRC table: the whole corpus asks for about 35 KB of CRC, so the bitwise
form is cheaper than the array literal it would take to avoid.

12. *(**Corrected, 2026-09-12, ADR 0016.** The rule cited here to spec §6.3
    decision 3 is not in §6.3, which is one sentence about **retains** and says
    nothing about writing. The prohibition is `docs/design/ssa-ir.md` section 2.9's,
    and section 2.9 now admits `&mutabilis T`. Measured while finding this: mutation
    through a borrowed aggregate parameter already worked, which made `firma`
    violable by handing a binding to a callee. The finding stands as written;
    the attribution was wrong.)*

    **A function cannot fill an array it was handed.** A parameter is borrowed
    (§6.3 decision 3) and there is no `&T` to assign through, so
    "caller-supplied output buffer" — this section's own asking shape — has no
    form. The reader owns each output and returns it by value (`Arbor` is
    ~69 KB, `Nodi` ~520 bytes); there is still no allocation and every buffer
    is still a fixed-size `acies` on the stack. Same gap as
    `docs/design/receptor.md` finding 11, met by a second program.
13. **An array length must be an integer literal.** `acies<u8, CAPACITAS>`
    with a module `firma` is `EXS-E0304` at the type and `EXS-E0303` at the
    initialiser (measured, `exsc` at `9c67d49`). So `65536`, `2048`, `1025`
    and `1024` are literals in every signature, and the module `firma`s beside
    them exist only for the arithmetic. Decision 5 of §8.6 makes a module
    `firma` a constant; nothing makes it a *type-level* constant.
14. **The 6-argument limit counts the hidden result pointer** (WC finding 8).
    `suffixum_quaere(a, b, s, d, clavis, n) -> Inventa` is `bfa: emitter:
    param index > 5`. Recorded again because it has now shaped two designs.
15. **`&` is written nowhere in the reader.** This section's table asked for
    `&acies<u8, 128>`; the reader takes the container by value-borrow and an
    offset instead, which is what the language admits today. Not a defect —
    a correction to the table.
16. **§3.1's qualifier rule cannot spell these names.** It decomposes only
    the part before `_` and requires the part after to be an ablative or a
    proper noun; `caput_lege` puts the noun first and the verb in the
    qualifier slot, and `lege_caput` would need `capite`. The names here are
    kept as this section wrote them so the C backend's table still lines up.
    §3's lexicon checker is `[OPEN]` and enforces nothing, so this is a gap in
    the rule, recorded rather than worked around.

### 6.6 The reference reader, where it is weaker than the format demands

Read-only, at `/home/asher/Documents/M64/streamdb-embedded/`. Reported, not
patched — it is not this repository's tree.

1. **`streamdb_embedded.c:120-122` — the bounds check wraps.**
   `if (h->trie_off + h->trie_len > file_len) return 0;` on two `uint64_t`s
   read straight off the wire. A header claiming `off = 2^64 - 1, len = 2`
   computes `1`, passes, and the reader then reads from `off`. The same form
   is at `:375-378` for every index entry's `offset + size`. The upstream
   form — `len > file_len || off > file_len - len` — is what this reader uses,
   and mutant 4 above is the difference made visible.
2. **`streamdb_embedded.c:181-183`, `:242-243` — `subtree_count` is read and
   discarded**, in both passes. It is a checkable invariant (measured: exact
   over all 689 nodes of the corpus's two commits) and nothing checks it.
3. **`streamdb_embedded.c:319` — the arena estimate's comment contradicts its
   arithmetic.** "A node is at minimum 10 bytes on the wire (u64 nchild + u8
   tag + u64 count is 17, so this is conservative)" — 17 is the minimum, and
   dividing by 10 over-estimates, which is safe; the sentence calls 10 the
   minimum and then says why it is not. Harmless, and misleading to the next
   reader.

The reference is otherwise agreed with byte for byte: every verdict in
`expectation.json`, on all four containers, is reproduced exactly.

### 6.7 The same reader, through the C backend — C3's certificate

**Measured 2026-09-12, gcc 15.3.0 and clang 21.1.8 at `-O0` and `-O2`, all
four with `-std=c11 -Wall -Wextra -fsanitize=undefined
-fno-sanitize-recover=all`, linked against `tests/c/exsrt_shim.c`.** The
unit is `exsc aedifica --hospes x86_64-linux
examples/streamdb/{lector_streamdb,probatio}.exsc --emitte c -o out.c`:
**137,742 bytes** of C -- 5,929 lines, 22 functions -- from the two
sources' 3,915 AST nodes.

| directory | container | reference | C, ×4 |
|---|---|---|---|
| `streamdb_corpus/` | `corpus.streamdb` | exit 0, 32,591 bytes | exit 0, `cmp`-identical to `expected.out`, gcc/clang × `-O0`/`-O2` |
| `streamdb_onus/` | one payload byte flipped | exit 0, 29,988 bytes | exit 0, `cmp`-identical, ×4 |
| `streamdb_caput/` | one header byte flipped | exit 0, 25,099 bytes | exit 0, `cmp`-identical, ×4 |
| `streamdb_truncus/` | `head -c 300` | exit 1, nothing written | exit 1, nothing written, ×4 |

Sixteen builds, sixteen agreements, **no disagreement anywhere between the
two backends** — over the whole 26-directory program corpus, not only these
four. **No UBSan report fired** on any of the 260 builds across both
corpora, at either level under either compiler, with
`-fno-sanitize-recover=all` throughout. The StreamDB unit compiles clean
under `-Wall -Wextra` at both levels under both compilers; three *other*
units in the corpus draw exactly one `-Wuninitialized` each at gcc `-O2`,
which is finding 19 and is not a disagreement.

Three things this run established that C1's IR fixtures could not:

- **The shim needed nothing added.** D6 anticipated extending it for the
  reader's `Lector`/`Scriptor` traffic; the six routines it already had were
  exactly enough (D6 step 2).
- **The four directories emit one unit.** They share a `sources=`, and the
  harness requires the emitted C to be byte-identical across them before it
  reuses a build — which it is, so the four containers really are four
  inputs to one program and not four programs.
- **The syscall count is not the cost.** Section 6.3 measured 80,300
  one-byte syscalls per corpus run and 0.043–0.044 s under the reference.
  Under C with UBSan the same run is **44–57 ms** at every level under both
  compilers — the same order. The expensive thing in the differential phase
  is `receptio_circuitus` (6.3 s under gcc `-O0`, 8.8 s under clang `-O0`),
  not this reader.

What section 6.4's five mutants prove is unchanged and is **not** re-run
through the C backend: a mutant is a statement about the *source*, and both
backends compile the same source, so running them twice would measure the
same thing twice. The C half's claim is agreement, and agreement is what
the sixteen builds show.

## 7. Milestones

C1 is done; C2's harness landed with it. What each says now:

| milestone | delivers | retires | its certificate |
|---|---|---|---|
| **C1** skeleton and prologue measurements — **DONE** | (1) the measurement table of D3, filled in, **first**; (2) `compiler/x86_64/backend_c/{emit_c,program_c}.inc` — the 37 lowerings, the 23 refusals by name (figures as of C1; the float wave of 2026-09-13 took the table to 46 lowerings / 14 refusals, and D4's count paragraph records the recount), the prologue, mangling; (3) `--emitte c`, `-o` required, `--hospes` rows, `CHK_F_PROGRAM` not set; (4) `tests/unit/bfc_emit_*.asm` pinning emitted text per opcode family, `bfc_mangle.asm`, `driver_emitte_c*.asm` for D2's three-way split | D2, D3 (as measured), D4 rows (text), D5 mangling, D1 (the unit compiles) | the hello world's IR through `emit_c`, compiled by `gcc -std=c11 -pedantic -Wall -Wextra` with the shim, prints `examples/saluta.expected` and exits 0 — by hand, recorded in the commit |
| **C2** the differential harness — **DONE** (in C1 and C3) | landed in C1: `tests/ir/emit_c.asm`, `tests/c/exsrt_shim.c`, `run_differential_tests` over `tests/ir/`, `c-emit-exit=`/`c-exsc-exit=`, `checks.test` and the devShell gaining `gcc` and `clang`, the eval-time closure assertion. **Landed in C3, closing what was owed:** the program corpus (a second loop in the same phase; `exsc … --emitte c -o out.c` per directory; `c-differentia=` with its closed reason set; `program_sources` shared with `run_program_tests` so the two phases cannot disagree about what the unit is; two floors), and `tools/reproduce.sh` diffing two `--emitte c` units | D6 for both corpora, D5 determinism, D1 (the unit runs) | `tests/ir/`: 39 lowerable × 4 = 156 builds agreeing, plus 11 rejections at matching exit status. `tests/programs/`: 26 eligible directories × 4 = 104 builds, all agreeing; 70 declared ineligible by name; `reproduce.sh` byte-identical on two units across divergent cwd/TZ/locale/epoch/umask/hostname. `nix flake check` green |
| **C3** the reader — **host half DONE** | `examples/streamdb/` in Exsecutor, `tests/programs/streamdb_*/` driving it from `initium` over `vendor/streamdb-v3/` on stdin, **both backends**; the Python expectation. The reference half landed with section 6; the C half is section 6.7 | D7 (host half), section 6, and what C2 owed | the semantic stream of section 6 — every key byte-exact with CRC verified, the counts, the error outcomes — **identical under both backends** on all four containers under all sixteen C builds, and equal to the expectation; five mutants, three of which the corpus catches and two of which need hand-made input (section 6.4) |
| **C4** the N64 cross-compile — **DONE** | `--hospes mips64-none-o64` accepted: the row's width to `chk_set_target` (one call site, which had been a literal 64), a `== 4` arm in `program_c.inc`, and a refusal by name in the reference emitter so a narrow address is named rather than mis-described. **Nothing in `lower/` and nothing in `ir.inc`** — ADR 0015 decision 2. The certificate is `tests/run.sh`'s cross phase: six `cross=yes` directories emitted for the row, cross-compiled `-mabi=n32 -march=mips3` and RUN under `qemu-mipsn32`, held to the reference's three observables. Closure cost `lld` + `qemu-user`, both cached; not a cross GCC | D7 (target half), D2's o64 row | **met.** §14 entry 25 `status=run`; six directories agree byte for byte, `forma` among them, so `@transitus` byte order is exercised big-endian. Kiln's own `mips64-elf-gcc` 14.4.0 compiles the o64 reader clean at `-Wall -Wextra -Werror` with its ROM flags **plus `-fno-fast-math`** — 14,616 bytes, `nm -u` = `{exsrt_abortus, memset}` (**`memset`**, not `memcpy`: `-ftrivial-auto-var-init=pattern` produces it), zero FP-register references. **Linked into a Kiln ROM and run**, 2026-09-12. Kiln's `examples/exsec-streamdb-demo/` checks the o64 reader in as generated C, runs it on a 32,768-byte libdragon kthread, and compares every key with Kiln's own `streamdb-embedded` reader on the same container; `./dev shot` in Ares shows open verdict 0, 2 documents (Kiln 2), 29 trie nodes, both present keys found at 56 and 78 bytes, the absent key absent, and BOTH READERS AGREE. It fits because the traversal frame fell from 127,184 to 20,680 bytes (ADR 0016). Not gated: an emulator run on a live display is a recorded measurement, not a check. Two things the first runs found belong to the ROM's side and are recorded in that example: libdragon's `kthread_join` cannot block (a thread must be joined after it has finished), and `n64.mk` compiles an object with the HOST compiler unless it is a prerequisite of the `.z64` |

Later, not scheduled: `div`/`rem`/`muls`/`*ov` in both backends; the
unordered `fcmp` predicates, `fma`, float load/store and `numeri` honouring
(`EXS-E0701` becomes reachable when the driver wires it) — the float
opcodes themselves are no longer on this list; whole-program mode with a C
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
6. **~~The lowering maps `mensura` to `u64` unconditionally~~ — WRONG, and
   corrected in C1.** It does not. `lower/ty.inc`'s `.mensura` arm reads
   `[lwrw]`, the width *the checker recorded*, and the checker takes it from
   `chk_set_target(chk_c, ptrbits)`, which `checker.inc`'s own header says
   exists precisely so that "pass 4's `mensura` is concrete only once this
   is set". The plumbing was already there and already parameterised. The
   one hardcoded site was **the driver's**, `run.inc`'s `mov esi, 64` with
   the comment "the pointer width of the one triple `--hospes` accepts",
   beside a note predicting its own replacement: *"a second accepted triple,
   the day one exists, is a small table here (triple bytes → ptrbits) in
   place of the single compare below — not a redesign."* C1 wrote that
   table. `mensura` was never the obstacle.

   **What the obstacle actually is**, found by looking: `lower/ty.inc`
   interns `ref` and `refc` at width 64 unconditionally (`mov ecx, 64`,
   twice), and `bfa_ty_ptr` interns `ptr` at width 64 in `ir.inc` itself —
   which is a file this milestone reuses *unchanged by contract*. Both are
   other trees, one of them off limits to this milestone by its own rule.
   So `mips64-none-o64` is refused by name with that reason, and C1 accepts
   `x86_64-linux` and `riscv64-linux`, which are both 64-bit and need no
   change anywhere. The o64 row stays C4's, as the milestone table always
   said — but it is two `mov ecx, 64`s away, not a redesign, and the next
   person should not go looking in `lower/`'s `mensura` arm for it.
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
12. **Neither fp-contract pragma is a defence under Clang, and the ISO one
    is a defence under neither.** Measured, D3's table: `#pragma STDC
    FP_CONTRACT OFF` is ignored by gcc 15.3.0 (which says so:
    `-Wunknown-pragmas`) and silently ignored by clang 21.1.8; `#pragma GCC
    optimize("fp-contract=off")` works under GCC and is not implemented by
    Clang; `#pragma clang fp contract(off)` does not override a command-line
    `-ffp-contract=fast` at file scope or at function scope. So ADR 0012's
    row-1 defence for problem 1 holds for GCC only, and for Clang the
    emitted source has **no** way to forbid contraction — the only measured
    remedy is the caller's `-ffp-contract=off`, which is the leak spec §9.3
    exists to remove. Nothing depends on this yet (no float opcode is
    lowered, D8). When one is, the choice is rung (b) — an `#error` under
    Clang for a module declaring `contractio explicita` — or rung (d), and
    it is a decision, not a detail. ADR 0012's Open item on this is
    answered and its answer is worse than it feared.
13. **`__builtin_memcpy` of 144,000 bytes is inlined by GCC at `-Os` and
    becomes a `memcpy` call under Clang.** Open question 6's host half,
    measured; the `mips64-elf-gcc` half is C4's. The consequence is that
    Kiln's link must admit `memcpy` (newlib provides it) — `nm -u` is not
    guaranteed to list only `exsrt_abortus`, and D7's gate already allows
    for it.

14. **`emit_c.inc` cannot include `verify.inc`, and section 1's chain was
    therefore impossible as drawn.** `exsc` holds BOTH backends, so
    `verify.inc` is already on its chain through `lower/ssa.inc` →
    `backend_fasmg/program.inc` → `emit.inc`. `emit_c.inc` includes nothing
    and states the consumer's obligation in its header, the way `ir.inc`
    does for `rt/`. Section 1 is corrected above. Found by assembling
    `exsc.asm`, not by reading.

15. **Clang warns on every unused `static inline` helper; GCC does not.**
    The prologue and the `exsi_*` helpers are one fixed blob by design (D5:
    the text is a function of `exsc`'s bytes), so a unit that uses no shifts
    still carries `exsi_shl_u`, and Clang says so 18 times per unit under
    `-Wall`. Measured both ways. The blob is the design and not a defect,
    and the differential harness passes `-Wno-unused-function` with that
    reason recorded at the flag. **Everything outside the blob — every line
    the lowering actually emits — is clean under `-Wall -Wextra -pedantic`
    with nothing suppressed**, which is the claim that matters and which the
    39 lowerable fixtures demonstrate. D3's "`-std=c11` clean under
    `-Wall -Wextra -pedantic` is the target" is met for the emitted
    lowerings and qualified for the carried blob. (The harness also passes
    `-Wno-cpp` / `-Wno-#warnings`: nixpkgs' wrappers inject
    `-D_FORTIFY_SOURCE=2` after the caller's flags — neither `-U` nor `=0`
    wins, measured — and glibc's `<features.h>` then `#warning`s at `-O0`.
    That is this host's packaging talking to its own libc from inside a
    system header, with no emitted line involved.)

    **The bolded sentence was over-broad and finding 19 corrects it.** It
    was measured on the 39 lowerable IR fixtures, none of which contains the
    one shape that draws a warning; the program corpus does, three times.

16. **A `proc` slot named `al` or `st` shadows a register.**
    *(numbered before the three C3 added below)*
    `docs/asm-conventions.md` §4.1 says slot names are plain unmangled
    globals, and its burned-name list is about collisions with other
    *project* labels. It does not say that the x86 register names are also
    burned, and they are: `slot al, dd` for an alignment made `ir.inc`'s
    `mov al, byte [kind]` fail with "invalid combination of operands",
    naming neither the slot nor the file. `slot st, dd` for a stride does
    the same against the x87 stack register. Both were renamed (`algn`,
    `strd`); `program.inc` had already learned the general lesson and named
    its own argument `gptr2`. Worth adding to §4.1 when that file is next
    touched — reported, not done, since it is another tree's.

17. **`--emitte c` dumped the typed AST to stdout, every time.** D2 says it
    "writes the translation unit there, writes nothing to stdout", and
    `driver/run.inc`'s own `-o` check repeats it; `drv_emit_dump` dispatches
    on three arms with `ast` as the fall-through, so `DRV_EMIT_C` reached
    it and dumped. 3,866 bytes for the hello world, on a stream D2 promises
    is empty, and spec §18.2's stage-dump mitigation firing for a flag that
    asked for no stage. No C1 check saw it because every call site redirects
    stdout to a file nobody reads. Fixed in C3 (two instructions at the call
    site; the emitted unit is byte-for-byte unchanged), and the six D2
    driver rows plus all 26 program directories now assert stdout is empty.
    The lesson is the general one: **a check that redirects an output stream
    is not checking it.**

18. **The `mips64-none-o64` row is not two constants, and finding 6's
    closing sentence ("it is two `mov ecx, 64`s away, not a redesign")
    over-promised.** *(**DONE, 2026-09-12, and two of its four costs were
    wrong.** The row is accepted; §14 entry 25 runs. What C4 actually
    changed: the width handed to `chk_set_target` — which this finding
    never lists, and which had been a literal 64 with a comment already
    stale — one `_Static_assert` arm in `program_c.inc`, and the refusal in
    `emit.inc`. Nothing in `lower/`, and **nothing in `ir.inc`**: ADR 0015
    decision 2 keeps `ptr`/`ref`/`refc` interned at 64 on every row, because
    the C backend switches on a type's KIND and asks C for
    `sizeof(void *)`, and `lwr_ty_ir` sizes from the checker's
    `AstType.width`. The file this backend reuses unchanged by contract was
    never in the way. And the certificate needed no cross toolchain: the
    clang already in the closure cross-compiles to big-endian MIPS-III, and
    `lld` and `qemu-user` are cached where a cross GCC is not.
    The third bullet's "silently miscompile" was also too harsh — measured,
    a `ptr@32` already died, but with a message that is factually wrong
    about 32; the refusal now says what is actually wrong
    (`tests/unit/bfa_emit_narrow_addr.asm`).
    What this finding never names is what ADR 0015 calls C4's real
    substance: the row makes `mensura` 32 for the first time in the
    project's history, so the o64 unit is a **different, larger C file**
    — 139,144 bytes against 137,742, differing in 2,344 lines, not one.
    The record below stands as written and is not rewritten.)*
    C3 looked at what the change actually is, and stopped
    rather than half-doing it:
    - `lower/ty.inc`'s two `mov ecx, 64` (`.ref`, `.refc`) are indeed two
      constants, and `mensura` is indeed already parameterised — finding 6
      is right about both.
    - But `ir.inc`'s `bfa_ty_ptr(module)` takes **no width at all**: making
      it read one means a new `BfaModule` field, a default in
      `bfa_module_init`, a setter, and a width threaded through
      `lwr_module` from the driver's `--hospes` row. `ir.inc` is the file
      section 1 says this backend **reuses unchanged by contract**, and it
      is `backend_fasmg/`'s tree — CLAUDE.md's Scope makes that a request,
      not an edit.
    - And the reference emitter would then **silently miscompile** such a
      module rather than refuse it: `__bfa_mem_access` and
      `__bfa_mem_plan` dispatch on the IR type's *width*
      (`emit.inc:2203`, `:2219`), so a `ptr` interned at 32 takes the
      `.narrow` arm and emits 32-bit x86-64 accesses. A refusal by name has
      to be added to `backend_fasmg/emit.inc` in the same change — a second
      edit to the same other tree, and the one that makes the change safe
      rather than merely possible.
    - Finally, the row would have **no runnable certificate in this
      repository's closure.** `nix flake check`'s test closure holds gcc,
      clang, python3, binutils and fasmg; there is no `mips64-elf-gcc`
      (D6 fixes that closure deliberately, and adding a cross toolchain to
      it is C4's own decision). The differential test cannot execute a
      mips64 binary, so what would land is emitted text with a
      `_Static_assert(sizeof(void *) == 4)` and nothing that runs —
      `[UNTESTED]` by construction, which CLAUDE.md's evidence discipline
      says not to ship as a table row.

    So the row stays refused by name, `drv_m_hospeso64`'s message stays
    accurate (it names `lower/ty.inc` and `bfa_ty_ptr`; it does not yet name
    the emitter refusal, which is worth adding when C4 touches it), and C4
    keeps it. What C3 changes is the estimate: **two constants, one new
    module field and its plumbing, one new refusal in another tree, and a
    cross toolchain in the test closure** — four things, of which two are
    another agent's and one is a flake decision.

19. **GCC at `-O2` warns `-Wuninitialized` on a `storebits` that is the
    first write to its slot.** Three of the 26 program units — `forma`,
    `hydramodem_basis`, `receptio_circuitus` — draw exactly one warning
    each, all the same shape:

    ```c
    _Alignas(1) unsigned char s2[17];
    …
    v2[1] = (unsigned char)(((unsigned)v2[1] & ~(15u << 4)) | (((unsigned)v5 & 15u) << 4));
    v2[1] = (unsigned char)(((unsigned)v2[1] & ~(15u << 0)) | (((unsigned)v4 & 15u) << 0));
    ```

    D4 row 43 makes `storebits` a read-modify-write of one byte so the
    neighbouring fields are untouched; when it is the *first* write to a
    fresh slot there are no neighbouring fields yet, and the read is of an
    indeterminate value. Clang does not warn, at either level; GCC does, at
    `-O2` only.

    **It is benign in effect and it is not a backend disagreement.** The
    bits the first store preserves are overwritten by the second, every one
    of the 17 bytes is defined before the slot is read, and all 104 program
    builds agree with the reference byte for byte. The reference does the
    same thing: it also read-modify-writes an uninitialised stack slot.

    **Nothing is changed to silence it, and the harness does not suppress
    it.** Zero-filling a `slot` in the C lowering would make the C backend
    differ from the reference in precisely the way ADR 0012's differential
    test exists to detect, and it would hide a real use of an
    uninitialised slot the day one exists. Knowing a `storebits` is the
    first write to its slot is dataflow analysis, which D4's one-lowering-
    per-opcode rule does not admit. So the warning is printed by
    `run_differential_tests` as a `note` on every run — C1's choice that "a
    warning that nobody sees is a target nobody holds" is what surfaced
    this — and finding 15's claim is qualified above. `[OPEN]`: whether a
    later milestone emits a first-write `storebits` as a plain assignment
    is a decision for whoever adds dataflow to the emitter, and it must be
    made on both backends at once or not at all.

20. **`ftrunc` is the width-narrowing conversion, not round-toward-integer.**
    The float commissioning brief described `ftrunc` as the
    round-toward-integer operation (the `roundsd` shape) — a premise the
    implementation could not honour, because the reference's lowering is
    the *width* cast: f64→f32 is `cvtsd2ss`, equal width is a move, and
    f32→f64 is `fext`'s row. The name names the width, exactly as integer
    `trunc` does; the fraction-truncating operation is `ftoi`. The brief's
    premise is recorded as wrong here, D4 row 32 is written to the
    reference, and `ssa-ir.md` 2.3's table (which groups
    `fext ftrunc itof ftoi` without naming `ftrunc`'s direction) would be
    the place for the reference's owner to say so explicitly.

21. **MXCSR at process start is 0x1FA0, masks set, and the brief said
    "masks clear".** Measured 2026-09-13 on this host: both compilers'
    programs start with MXCSR `0x00001fa0` — the six exception masks of
    `0x1f80` **already set**, plus a stale precision flag (0x20). The
    reference's `program.inc` writes `BFA_MXCSR_AD_PAREM` 0x1F80 at program
    start, so a differential run of float fixtures would otherwise compare
    two programs whose only difference is a stale PE bit. The C twin is
    one line in the shim (`_mm_setcsr(0x1F80u)`,
    `#if defined(__x86_64__)`), not a prologue line: the prologue belongs
    to every emitted unit, the control word to the one place a program
    starts, and in library mode that place is the shim's `main`. This is
    a *differential* fix only; whether the masks-before-start state is
    itself a spec question is the reference's tree's to ask.

22. **Measured: `-fsanitize=undefined` does not include
    float-divide-by-zero on this host, under either compiler.** gcc
    15.3.0 and clang 21.1.8 both compile `1.0f / 0.0f` under the
    harness's exact flags (`-std=c11 -fsanitize=undefined
    -fno-sanitize-recover=all`) to code that produces `inf` and exits 0.
    Consequences, both relied on: the fixture checks that divide by
    ±0.0 to observe the sign of zero and the infinities are safe under
    the harness flags, and `fdiv`'s row 27 claim "never a trap" holds
    under UBSan, not only in plain C. What UBSan *does* include is
    float-cast-overflow: `ftoi` of a NaN or an out-of-range value aborts,
    which row 34 documents as the C-side twin of the reference's `[OPEN]`
    NaN/range behaviour rather than hiding it.

23. **A declared `numeri` is parsed by both backends and honoured by
    neither, and the failure spec 5.4 promises does not exist.** Spec 5.4:
    "a C target that cannot honour a declared `numeri` fails the build."
    Measured: a module whose every function declares
    `numeri ad_superius vetita explicita conservata` parses, verifies and
    emits on both backends, and both emit round-to-nearest code — the
    reference hardcodes `ad_parem` in `program.inc`'s MXCSR constant, the
    C backend emits bare C operators with no `fesetround`. The `EXS-E0701`
    diagnostic exists in §13 but nothing fires it: the driver
    (`run.inc`) performs no `numeri` check, in either backend's path.
    So ADR 0012's numeric subset is still untestable in the *declared*
    direction — the differential test passes on a non-default-`numeri`
    module precisely because both backends ignore the declaration, which
    is agreement on the wrong thing. Two more things measured while
    finding this, both for other trees: the verifier's rule 8 requires
    the four `numeri` word ids to be EQUAL across every direct call, so a
    module declaring `numeri` on one function and leaving it off another
    fails verification (exit 5) before any backend sees it; and wiring
    `EXS-E0701` is a `driver/` change this backend cannot make.

24. **`ssa-ir.md`'s own `@dot` example cannot be lowered by either
    backend — reductions only, since 2026-09-14.** Its body contains
    `%8 = load f32 %7 0 nativus` — a float in memory, which both
    backends' memory lowering used to refuse (the reference's
    `__bfa_mem_plan` dispatched on integer widths; the C emitter's
    `__bfc_mem_k` died on the float class) — and it is built on
    `redinit`/`contrib`/`redfin`, refused in both backends since D4
    was first written. The float half closed on 2026-09-14: the
    signaculum stage needed `acies<f64, N>` element loads and stores,
    so float `load`/`store` in `nativus` order now lowers in both
    backends (the value as its raw IEEE bit pattern, 8 or 4 bytes — no
    arithmetic on the path), with `maior`/`minor` still refused by the
    verifier and both emitters (`tests/ir/float_mem.ir`,
    `tests/ir/reject_verify_float_load_ord.ir`, D4 rows 40–41). What
    `@dot` still trips is the reductions half, and that choice —
    example gets an integer body, or the reductions get lowerings —
    is not this backend's to make.

## 9. What retires each marker

| decision or claim | milestone | the test as planned |
|---|---|---|
| D3's incantation table, every cell | C1, first task | the scratch unit under `gcc`/`clang` × `-O0`/`-O2` × UBSan, results written into section 8 |
| D4's 46 rows, text | C1 for the 35 pre-float rows; the 11 float rows are scratch-verified only (2026-09-13) | `tests/unit/bfc_emit_{narrow,bitwise,bytes,phi,call,program}.asm`, exact emitted text, mirroring `bfa_emit_*`; a `tests/unit/bfc_emit_float.asm` pinning the float rows is owed to `tests/unit/`'s tree, not this backend's |
| D4's 46 rows, semantics | C2 for the integer rows; the float rows land with the reference's own `tests/ir/` fixtures in the same wave | **integer rows done**: the 50 IR fixtures (39 lowerable) and 26 programs under four builds each, three observables — 156 + 104 builds, all agreeing. **Float rows**: the scratch fixtures of the status header (68 + 4 checks, four toolchains), Python-computed IEEE expectations |
| D4's 14 refusals | C1 for 12; `fma` and `bitcast` refused 2026-09-13 in both backends at once | one `tests/ir/reject_c_*.ir` per refusal class with `emit-exit=4` and, for the two runtime refusals, `c-emit-exit=4` against a reference that lowers them; the float refusal classes are scratch-verified (15 fixtures, exit 4, message by name) until the reference's `tests/ir/` rejection fixtures land |
| D2's three-way split | C1 | `tests/unit/driver_emitte_c_{nohospes,badrow,noout}.asm`; the `EXS-E0701` row stays `[UNTESTED]` even with float opcodes lowered — the driver wiring does not exist (finding 23) |
| D5 mangling | C1 | `tests/unit/bfc_mangle.asm` |
| D5 determinism | C3 | **done**: `tools/reproduce.sh` diffs two `--emitte c` units (the hello world, 8,691 bytes; the StreamDB reader, 137,742) across divergent cwd/TZ/locale/`SOURCE_DATE_EPOCH`/umask/hostname — byte-identical, and two mutations show the diff is not vacuous. Plus, inside one process, sixteen program directories that share a `sources=` must emit byte-identical C |
| D6 | C3 | **done**: `run_differential_tests` green in `nix flake check` over both corpora |
| D6's closure assertion | C1 | **done, run once by hand**: `pkgs.gcc` added to `buildExsecutorPackage`'s `nativeBuildInputs` makes `nix build .#exsc` fail at EVALUATION -- `error: an integer with value '2' is not equal to an integer with value '1'`, pointing at `assertBuildClosure`, before any derivation is instantiated. Reverted immediately; the assertion is `flake.nix:175`. |
| D7, section 6 | C3 (host), C4 (target) | **host half done**: the reader's certificate, identical under both backends on four containers × four toolchains (section 6.7). The N64 gates are C4's |
| D1's whole-program mode | — | `[OPEN]`, not scheduled |

## 10. Open questions the implementer must answer first

In the order they block. **1–5 are answered**; 6 and 7 are C4's and stand.

1. ~~**D3's table.**~~ Answered by C1's measurement, section 2 D3.
2. ~~**Where `mensura`'s width lives**~~ — answered twice: finding 6 (it is
   already parameterised; the obstacle is `ref`/`refc`/`ptr`) and, more
   precisely, finding 18 (what the o64 row actually costs, and why C3
   stopped rather than half-doing it).
3. ~~**`print.inc`'s output helpers**~~ (finding 8) — resolved in C1.
4. ~~**UBSan and the `index` idiom**~~ — answered in D3's table: **no
   report** under `-fsanitize=undefined,pointer-overflow`, so the row keeps
   the idiom and the harness needs no `-fno-sanitize=pointer-overflow`. Now
   also demonstrated at scale: 260 builds across both corpora, every one
   under `-fno-sanitize-recover=all`, no report.
5. ~~**The shim's record layouts**~~ — answered by not needing to ask.
   C3 drove `Lector` and `Scriptor` from `initium` through the shim over
   four containers and the six routines were exactly enough; the six
   numbers have not moved, and RT H4's hazard is still recorded in the
   shim's header. A generated header stays admissible and stays unbuilt;
   the question as first asked was whether `tests/c/exsrt_shim.c` can
   `#include` a generated header from `interface.inc`'s constants rather
   than restate them (RT H4), and the answer is "it could, and it has not
   had to".
6. **`__builtin_memcpy` at `-Os` under `mips64-elf-gcc`** — whether a
   144,000-byte `copy` (`tests/ir/copy_magna.ir`) becomes a `memcpy` call,
   and whether Kiln's link admits it (newlib provides it; the gate forbids
   libm and `malloc`, not `memcpy`).
7. **`-std=gnu2x` and `_Noreturn`** — Kiln's engine dialect is C23-ish,
   where `_Noreturn` is deprecated and `[[noreturn]]` is the spelling;
   whether GCC 14 warns under `-Wall -Wextra`, and whether that matters
   under `-Wno-error`.
