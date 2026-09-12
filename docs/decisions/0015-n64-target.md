# 0015 — The N64 target: `mensura` is the address width, the IR's `ptr` is not

**Status:** Accepted as design, 2026-09-12. **Nothing implemented.** The
milestone is C4 in `docs/design/c-backend.md`; this ADR decides what C4
builds and corrects `finding 18` there, which costed it wrongly in two
places. The measurements below are real and re-runnable; every one of them
was taken against the **`mensura` = 64** compiler this repository ships
today, which is the point decision 1 turns on.

**Relates to:** spec §5.2, §5.4, §9.2, §9.3, §9.5, §14; ADR 0007 (numeric
semantics are declared), ADR 0012 (two backends, the differential method);
`docs/design/c-backend.md` finding 18 and milestone C4;
`compiler/x86_64/{driver,lower,backend_c,backend_fasmg,checker,prelude}/`;
the Kiln engine, which is the consumer.

## Context

Spec §9.5's closed table has three rows. Two are 64-bit and accepted. The
third, `mips64-none-o64`, is the Nintendo 64 under o64 — a 64-bit ISA whose
ABI has 32-bit addresses — and `exsc` parses it only so it can refuse it by
name. It is the row the **Kiln** engine needs in order to link an
Exsecutor-generated StreamDB reader into a ROM.

C3 stopped at this row and wrote down why, as finding 18: two constants in
`lower/ty.inc`, a pointer width threaded onto `BfaModule` so `bfa_ty_ptr`
could read it, a refusal in the reference emitter, and a cross toolchain in
the test closure — "four things, of which two are `backend_fasmg/`'s tree and
one is a flake decision." Two of those four do not survive measurement.

### What was measured, 2026-09-12, at `f6a4810`

1. **Kiln's own toolchain accepts the emitted dialect.**
   `mips64-elf-gcc` 14.4.0 (`M64/nix/toolchain.nix`, nixpkgs
   `pkgsCross.mips64-embedded` with GCC pinned to 14) at
   `-march=vr4300 -mtune=vr4300 -mabi=o64 -std=gnu11 -Os -Wall -Wextra
   -ffreestanding`, on the StreamDB unit with the prologue's
   `_Static_assert` changed to `sizeof(void *) == 4`: **clean, zero
   diagnostics.** The pure reader alone is 12,196 bytes, `nm -u` is exactly
   `{exsrt_abortus, memcpy}`, and it contains **no** double-precision
   instruction and no floating-point register reference at all.
2. **That assert is live.** The `== 8` form of the same file fails to
   compile under o64. o64 really does have 4-byte pointers, and the
   prologue really does catch a mismatched row.
3. **The emitted C is endian-independent — measured, not argued.** The
   StreamDB certificate stream built for big-endian MIPS-III and run under
   `qemu` is **byte-identical to the x86-64 reference backend's** over all
   four vendored containers: `corpus.streamdb` (32,591 bytes),
   `corpus.header-flip`, `corpus.payload-flip`, and `corpus.trunc300`
   including its exit 1. This held at **both** 4-byte and 8-byte pointers.
   Spec §9.5 claims the emitted text "depends on no assumption" about byte
   order; until this run, nothing had ever tested that claim against a
   big-endian host.
4. **A runnable certificate needs no cross GCC.** The clang already in the
   devShell cross-compiles to `ELF 32-bit MSB, N32 MIPS-III` with
   `sizeof(void *) == 4`; with `ld.lld` the result is a freestanding static
   binary that runs under `qemu-mipsn32`. `lld` and `qemu-user` are cached
   in nixpkgs; `pkgsCross.mips64-embedded`'s GCC and binutils are not, and
   would build from source on every fresh checkout. clang does **not**
   support `-mabi=o64`; n32 is the closest available ABI.
5. **The emitted unit refuses Kiln's own ROM flags.** libdragon's `n64.mk`
   sets `-ffast-math` globally, and Kiln relies on it
   (`engine/src/kiln/kiln_dict.c:258`). The emitted prologue answers with
   two `#error`s, `-ffast-math is refused (spec 5.4)` and
   `-ffinite-math-only is refused`. `-fno-fast-math` on that one
   translation unit resolves it: clean, 14,796 bytes. Under Kiln's
   `-ftrivial-auto-var-init=pattern` the imported symbol set becomes
   `{exsrt_abortus, memset}` — `memset`, not `memcpy`.
6. **A `mensura` field in a `@transitus` struct is already refused**, so
   the wire format cannot become target-dependent through this row:
   `EXS-E0321` unannotated, `EXS-E0309` with an order annotation. §5.2's
   "every field of a `@transitus` type is an unsigned integer" is enforced
   by `checker/rows/layout.inc`'s kind test, and no new code is needed.

### The correction those measurements force

Measurements 1 and 3 look like they certify the artifact C4 will emit. They
do not, and the reason is the whole substance of this ADR.

`--hospes` does not contribute one `_Static_assert` line to the emitted
text. It contributes a `mensura` width, and that width is written into the
text everywhere:

- `backend_c/emit_c.inc:683-700` — `__bfc_norm_open` emits **nothing** at
  width ≥ 64 and `exsi_norm_u(` below it. Every wrapping operation on a
  `mensura` gains a wrapper at 32.
- `emit_c.inc:1517-1545` — `__bfc_mem_k` returns a **literal byte count**
  for an integer; only an address answers 0, meaning `sizeof(void *)`.
  Every `mensura` load and store goes from 8 to 4.
- `emit_c.inc:725-740`, `:795+`, `:1144-1215` — `exsi_add_u(a, b, 32)`, and
  both widths of every conversion, are literals in the text.
- `lower/ty.inc:250-330` — `acies<mensura, 2048>` halves, so every `slot`
  size, struct offset and `index` stride changes with it.

So the `mips64-none-o64` unit is a **different, larger C file**, not the
same file with a flipped assert. Measurements 1, 2 and 5 were taken on the
`mensura` = 64 unit; measurement 3 ran with 32-bit pointers but 64-bit
`mensura`. They are evidence that Kiln's toolchain accepts the emitted
*dialect*, that the flag collision is real, and that the text is
endian-clean. **They are not evidence about the C4 artifact**, and no
commit message may present them as such (CLAUDE.md: never present a
re-derivation as a restoration).

## Decision

### 1. `mensura` is the target's address width — 32 on this row

Spec §9.5 defines the row by its `mensura` width, and §5.2 makes `mensura`
the `usize` of the target. Measurement 1 shows that `mensura` = 64 under
o64 compiles and works, so the shortcut is real and available. **Reject
it.** A 64-bit `usize` on a target with 32-bit addresses is a lie that
holds right up until the first `positio` or `crudum` boundary, and taking
it would mean amending §9.5 to disagree with its own table. It is recorded
here because it is the obvious proposal, and the next person should find
the reason it was declined rather than the absence of one.

The consequence is accepted deliberately: **`+` traps at 2^32 on this
row.** That is correct under §5.4 and ADR 0007 — `mensura` is `usize`, and
`usize` here is 32 bits.

### 2. The IR's `ptr`, `ref` and `refc` width is **not** a target fact

Leave all three interned at 64. This is the headline, because it is what
finding 18 got wrong.

Nothing observes that number except the reference backend. In the C
backend, `__bfc_ty_class` (`emit_c.inc:213-240`) switches on the type's
*kind* alone and answers "address" for `PTR`/`REF`/`REFC`;
`__bfc_emit_ctype` yields `unsigned char *`; `__bfc_mem_k` answers
`sizeof(void *)` and asks C for the number. `__bfc_int_width` would read a
width, but it refuses any non-integer first, so a pointer can never reach
it. In the lowering, `lwr_ty_size` reads `AstType.width` — the checker's
tree, sized from `ChkRowWork.wptr` — not the IR type's, and
`LwrState.tptr` is used as an opaque type id.

The target fact is the emitted text's `sizeof(void *)` assertion, which
`backend_c/program_c.inc:240-261` already emits from the `--hospes` row.

Therefore C4 needs **no** `BfaModule` field, no default in
`bfa_module_init`, no setter, no width threaded through `lwr_module`, and
**no change to `backend_fasmg/ir.inc` at all** — the file the C backend
reuses unchanged by contract stays unchanged. Finding 18's second bullet
is retired, and `driver/run.inc`'s refusal message must stop naming
`bfa_ty_ptr` as the obstacle, because it is not one.

What is still required is the *refusal*: `backend_fasmg/emit.inc` must
reject a `ptr`/`ref`/`refc` narrower than 64 **by name**. Today such a type
reaches `__bfa_mem_access`'s `.bad` arm and dies with "a width that is not
a whole number of bytes", which is factually wrong about 32 — four of them.
Finding 18 called this a silent miscompile; it is not silent, but its
message is useless, and the refusal is defence in depth for a future row
where a pointer width genuinely differs.

The accepted cost: `--emitte ir` prints `ptr` as 64 bits on a 32-bit
target. That is a cosmetic dishonesty in a debug dump. It is said out loud
here and in `lower/ty.inc`'s comment rather than papered over.

### 3. A target-dependent trap threshold is a target semantic, not a backend disagreement

ADR 0012's differential method compares two backends on one source and
requires identical observables. Across two *targets* that is the wrong
question: a program whose `mensura` arithmetic crosses 2^32 is correct on
`x86_64-linux` and traps on `mips64-none-o64`, and both behaviours are
right. The harness must be able to **declare** such a difference, the way
`c-differentia=` already declares a backend difference, rather than
reporting it as a failure. A program whose correctness depends on a 64-bit
`mensura` is a program that does not support this row.

### 4. The prelude interface layout is the compiler's, in both places

`prelude/interface.inc:110-149` fixes a 64-bit shape — `Scriptor.descriptor`
at 8 with size 16, `ExsAmbitus.argv` at 24 and `envp` at 32 with size 40.
A natural C rendering of those structs on o64 is 12 and 32 bytes with
`descriptor` at 4 and `envp` at 28. The two disagree, and the only thing
standing between that disagreement and a wrong-offset read is the
`_Static_assert` set in `tests/c/exsrt_shim.c:61-73`.

The fix is the cheap one: **pin the shim's offsets with explicit padding**
to the literal offsets `interface.inc` fixes, instead of letting the C
compiler choose a layout and then testing whether it guessed right. The
asserts stay, and then they hold at both widths. This keeps one layout,
owned by the compiler, and leaves `runtime.md` hazard H4 exactly as
tracked — two copies of one layout, kept in step by hand — rather than
adding a third possibility.

### 5. n32 under qemu is the certificate; o64 is the deployment

clang cannot emit o64 (measurement 4), so the gate compiles `-mabi=n32` and
runs under `qemu-mipsn32`. What n32 shares with o64: **endianness, pointer
width, register width, ISA, and the emitted text itself** — both are
`--hospes mips64-none-o64`, so the artifact under test is the artifact that
ships. What differs: argument passing, and the compiler. Both sides of
every call in the certificate are compiled together by one compiler, so the
certificate crosses no ABI boundary that is not also internal to it.

The honest labelling that follows, and which §9.5 must carry:

- **tested** — the `mips64-none-o64` unit compiles and runs correctly, big
  endian, with 32-bit addresses, agreeing byte for byte with the reference
  backend over the vendored corpus;
- **`[UNTESTED]`** — under the o64 ABI specifically, and linked into a ROM;
- and `riscv64-linux` **stays `[UNTESTED]`**. After C4 the harness runs a
  mips64 binary and still never runs a riscv64 one. The mips64 result must
  not be allowed to launder the row beside it.

## Consequences

**Positive**

- The one item that crossed into another tree by contract disappears.
  `backend_fasmg/ir.inc` is untouched; `backend_fasmg/` is entered only for
  a refusal, which is additive.
- The test closure gains two cached packages instead of an hour-long GCC
  build, and `nix flake check` stays usable on a fresh checkout.
- The endian-independence claim in §9.5, asserted since the section was
  written, becomes measured. That is worth having independently of the N64.
- `mensura` = 32 shrinks the StreamDB reader's live stack frame by roughly
  40 KB, which the ROM needs (see Open).

**Negative**

- `mensura` = 32 is a code path with no existing coverage anywhere in the
  tree, and it goes live all at once the moment the row is accepted. In
  particular `checker/types/types.inc:632-660`'s literal range check has
  never run: at width 64 the `cmp r14, 64 / jae .yes` short-circuit means
  **no literal has ever been range-checked against `mensura`**.
- The certificate does **not** cover decision 1. The StreamDB reader was
  written narrow-safe — file offsets stay `u64` and narrow to `mensura`
  only after a bounds test (`lector_streamdb.exsc:658-661`, `:376-379`,
  `:498`) — so it behaves identically at either width. What certifies
  `mensura` = 32 is a checker unit fixture plus an assertion that the two
  rows' emitted text differs by more than the assert line. A green qemu run
  must not be read as covering it, and the mutation set says so explicitly.
- The abort observable does not port. `__builtin_trap()` is `ud2` → SIGILL
  on x86-64 but `teq` → SIGTRAP on MIPS, while `tests/run.sh`'s `check_run`
  matches `signal 4`. The MIPS shim must raise SIGILL deliberately to keep
  one key reading every backend.
- Emulation is 10–50×, so the cross phase must be restricted to declared
  directories rather than run over every program fixture.

## Open

- **The ROM's stack budget.** `Arbor`
  (`examples/streamdb/lector_streamdb.exsc:191-199`) is about 69,640 bytes
  at `mensura` = 64 and 53,252 at 32; with `probatio`'s 64 KB buffer and
  `arbor_percurre`'s locals the live frame is roughly **180 KB at 64 and
  ~140 KB at 32**, and it is returned through the hidden-return pointer, so
  the *caller* owns those bytes. libultra thread stacks are conventionally
  8–16 KB. Under qemu's 8 MB stack this passes silently: **the certificate
  will be green and the ROM will overflow.** Linking into a Kiln ROM is
  `[OPEN]` and that `[OPEN]` includes a measured stack budget. This is not a
  C4 correctness defect — it is a fact about the reader that C4 is the first
  thing to make consequential.
- **`prelude/interface.inc:87`'s `EXS_IFACE_T_U64`** is commented "`mensura`
  on x86_64-linux", which is the tell that it was known to be
  target-specific. `lower/lower.inc:154,169` maps it to an unconditional
  64-bit intern while `lower/expr.inc:1847-1850` types the call's result
  from the AST, which at 32 is 32. Both spell `uint64_t` in C, so the C
  backend papers over it, and `backend_fasmg/verify.inc:206-208` states
  that call types are not cross-checked against the callee's signature, so
  no verifier rule fires either. Invisible today; a real miscompile the day
  a backend honours declared widths. `prelude/` is another tree. `[OPEN]`
- **`lower/ty.inc:312-320` gives a `refero` a zero-byte slot** — it computes
  `(AstType.width + 7) >> 3` and `checker/types/sig.inc:711-729` interns
  `AST_TY_REF` with no width. Unreachable today, since the C backend
  refuses `retain`/`release` and the reference has no ARC runtime.
  Decision 2 says not to open it. `[OPEN]`
- **Whole-program mode for this row.** Library mode is what Kiln needs, and
  it is what C4 builds. A freestanding C prelude with raw MIPS syscalls is
  not designed and is not this milestone. `[OPEN]`
