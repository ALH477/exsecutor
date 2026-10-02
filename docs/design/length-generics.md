# Length generics — one body for every `acies<T, N>`

**Status:** `[OPEN]` — design only. No code exists. Nothing below has
been parsed, typed, lowered or run, except the measurements in section 1,
which were run on this tree on 2026-10-02. Every `EXS-E` code named as
*proposed* is a proposal for a §13 amendment. None is in §13 or
`compiler/x86_64/diag/codes.inc`, and no number is claimed for one.
**Relates to:** spec §5.2 (the one aggregate cast), §5.4 (Vectors), §6.3
decision 5, §7.1 (dictionary passing), §8.6 (`GenericParams`, `TypeArg`),
§13; `docs/design/ssa-ir.md` section 2.9; `docs/design/c-backend.md` D1, D4;
ADR 0016; Oligarchy's `docs/exsecutor-kernel-roadmap.md` B.2.

## 1. The problem, measured

`N` is part of `acies<T, N>`'s type (§8.6: "`N` is part of the type"), and
a function cannot be generic over it. Two consumers have paid for that.

**Duplicated code: `examples/custos/custos.exsc`.** It needs entry 23's
CRC-16 over a 32-byte SuperPack window. Entry 23's `redundantia` takes
`acies<u8, 17>`, so custos repeats the body as `redundantia_sarcinae` over
`acies<u8, 32>`. Its own comment says why: "there are no generics over a
length, so the body is repeated rather than shared". Two copies of a
certified polynomial division can drift. Only custos's anchors check the
second copy.

**Copied bytes: `examples/arca/arca.exsc`.** `nomen_iudica` takes
`acies<u8, 4096>`. `caput_iudica` judges either a long name (already in a
4096-byte buffer) or a header's 100-byte name field (inside a 512-byte
block). To make one call, it copies whichever applies into a local
`acies<u8, 4096>`. Measured with gcc 13.3.0 `-O2 -fno-stack-protector
-fstack-usage` on the emitted unit:

- **`exs_caput_iudica`'s frame is 4,160 bytes, and 4,096 of them are that
  copy.**
- Every other function in arca and custos is 48 bytes or less.

The kernel roadmap's phase K2 asks for "an emitted unit whose largest
frame is under 4 KB by `-fstack-usage`". arca misses that by 64 bytes,
because of this copy alone.

**What the compiler does today**, measured with `exsc aedifica --hospes
x86_64-linux --emitte c --diagnostica json`:

| source | result |
|---|---|
| `functio f<N: mensura>(b: acies<u8, N>, n: mensura) -> u16` | `EXS-E0304` "wrong number of arguments" at `<u8, N>` |
| `publica firma K: mensura = 17;` then `b: acies<u8, K>` | `EXS-E0304` at `<u8, K>` |
| `functio prima<T>(b: T) -> u8 { redde 0; }` | the front end accepts it (`--emitte ast`, exit 0). Lowering it **kills `exsc` with SIGILL (exit 132), no diagnostic**, under both `--emitte c` and the reference backend |

The rows show three things:

- **Row 1.** `checker/types/sig.inc`'s `__chk_ty_genlit` requires argument
  1 of `acies` to be an `AST_LIT`, and raises `EXS-E0304` for anything
  else.
- **Row 2 is a gap between spec and code.** §8.6's peek table says of
  `TypeArg` that "a named constant parses as `Path` and is resolved
  semantically". Nothing resolves it. The spec describes a behaviour the
  checker does not have. Since no program has needed it, the honest fix is
  either to implement it or to narrow that sentence. This document does
  neither: it is out of scope, and it is recorded here because section 3
  builds on the same resolution.
- **Row 3.** `lower/lower.inc`'s `__lwr_gencount` `rassert`s on any
  declaration with generic parameters, deliberately. The comment reads "a
  generic function traps rather than emitting a signature that is
  silently one argument short". Type generics are `[UNIMPLEMENTED]`
  because a dictionary's contents are open (spec §15 item 5). An internal
  assertion reachable from accepted source is still a compiler bug by
  `docs/asm-conventions.md`'s own table: "an internal contract violation
  — a compiler bug". A refusal by name would be better, and it is
  `[OPEN]` which existing code fits.

## 2. Options

| | A. length parameter `<N: mensura>` | B. bounded slice: an unsized borrow `&acies<T>` | C. monomorphisation |
|---|---|---|---|
| syntax | none new: `GenericParam ::= IDENT [':' Type]` and `TypeArg` already parse it | a new type form, `acies<T>` with one argument, borrow-only. Slices are `[OPEN]` in §8.6, and slicing `a[i..j]` would need a range-index rule | none new (A's syntax) |
| relation between lengths | expressible: `xor<N>(a: acies<u8, N>, b: acies<u8, N>)` requires equal lengths at the call, checked statically | lost: two slices have independent runtime lengths | expressible |
| §7.1 | **is** dictionary passing: the dictionary for a length is its value, one `mensura` | outside §7.1: a fat pointer, not a generic | contradicts §7.1 ("monomorphization is a link-time optimization ... never a semantic requirement") |
| C ABI | `(T *p, uint64_t N)`: one symbol per function | `(T *p, uint64_t len)`: the same pair, carried as one value | one symbol per instantiation, so a mangling scheme becomes ABI |
| windows (arca's 100 bytes inside 512) | not directly: pass the 512-byte block and a count, as arca already does with `n` | yes, with slicing | not directly |
| frame impact | removes copies like arca's 4,096 bytes | same | same |

**Recommendation: A.** It needs no new syntax and no new word (§3.9's
root-space cost is zero). It *is* §7.1's model, in its simplest case. And
it keeps the one thing B loses: the type says that two arrays have the
same length. B stays `[OPEN]` as the later answer to windows, if a program
needs one that a count parameter cannot express. C is rejected by §7.1.

## 3. Design of option A

### 3.1 Declaration

- A generic parameter annotated with `mensura` (`<N: mensura>`) is a
  **length parameter**.
- An annotation that resolves to an `interfacies` stays what §7.1 already
  says, a bound on a type parameter.
- Any other annotation (`<N: u8>`, `<N: textus>`) is `EXS-E0309`, the
  existing "type expression or annotation not applicable".

A length parameter may appear:

1. as argument 1 of `acies` in a **parameter** type, whether by value,
   `&acies<T, N>` or `&mutabilis acies<T, N>` (ADR 0016). The element type
   `T` must be concrete in the first version, so type generics are not a
   prerequisite;
2. as an expression of type `mensura`, anywhere in the body: `per i in
   0..N`, `si n gt N { … }`.

**Every length parameter must appear in at least one parameter's type.**
Otherwise it cannot be inferred, and nothing in the first version could
use it except as a number the caller passes explicitly. A function that
needs that number should take a `mensura`. Proposed: one new code, "a
length parameter appears in no parameter type". Whether `EXS-E0309`
suffices is `[OPEN]`; the argument for a new code is that the fault is in
the signature as a whole, not in one annotation.

### 3.2 Where an `acies` of parameter length is refused

These positions need a size the compiler does not have:

| position | code | why the existing code fits |
|---|---|---|
| a local binding, `mutabilis w: acies<u8, N>` | `EXS-E0309` | a type not applicable in that position; a VLA-shaped frame breaks B.2's bounded frames, the C backend's no-VLA property, and `examples/abortus/`'s soundness argument, which rests on there being no VLA |
| an array literal `[0; N]` | `EXS-E0201` | §8.6's grammar takes an integer literal there, so this is a parse error today (measured: `[0; k]` is `EXS-E0201`), and stays one |
| a binding or copy of the whole value, `firma c = b` | `EXS-E0309` | the copy is a local of parameter length |
| a return type | `EXS-E0309` | the caller-owned return slot (section 2.9 group 1) would have no static size; `[OPEN]` for a later version, since the caller does know `N` at a concrete call site |
| a `structura` field | `EXS-E0309` | generic structs are not in scope |
| `sicut` to or from a `@transitus` struct | `EXS-E0305` | §5.2 already says a cast "to an `acies` of the wrong length" is `EXS-E0305`, and the cast's totality rests on `N` equalling the struct's size **statically**. A runtime-checked narrowing would be an `eventus`, not a `sicut` `[OPEN]` |
| whole-acies float arithmetic | `EXS-E0305` | §5.4 admits lane counts `{2, 4, 8}` only, and a parameter is not known to be one |
| `apud machina` placement | `EXS-E0309` | §5.5 device arrays are out of scope |

The table shows that one new code is proposed, not seven. Each refusal
above is a case an existing code's text already describes. Section 3.1's
"appears in no parameter type" is the one fault no existing code names.

### 3.3 Calls: inference and checking

At a call to a function with length parameters:

1. Each length parameter is solved from the arguments. An argument of type
   `acies<T', L>` in a position typed `acies<T, N>` requires `T' = T`
   (`EXS-E0303` otherwise) and binds `N := L`. `L` is a literal, or the
   caller's own length parameter.
2. Every position that mentions the same `N` must bind the same `L`, or
   the call is `EXS-E0303` at the first that disagrees. This is a static
   check: `xor<N>` called with 17 and 32 bytes is a type mismatch, not a
   runtime trap.
3. Explicit arguments, `redundantia<17>(w, 17)`, are admitted, because
   `GenericArgs` already follows a path segment in §8.6. They must agree
   with what step 1 solves (`EXS-E0303`).
4. A parameter-length argument passed to a concrete position (`b:
   acies<u8, N>` handed to a callee taking `acies<u8, 17>`) is
   `EXS-E0303`. There is no implicit runtime-checked narrowing.

### 3.4 Lowering and the IR: no new opcode

Section 2.9's group (2), "dictionaries, one `ptr` per generic parameter in
declaration order", becomes:

- one `ptr` per **type** parameter;
- one `mensura` per **length** parameter;
- both in declaration order.

The dictionary for a length is the value itself, fully determined. So
length parameters do not wait on spec §15 item 5, the open contents of a
type dictionary. `__lwr_gencount`'s trap must distinguish the two kinds:
length parameters lower, and type parameters still refuse.

Inside the body:

- **Indexing** lowers to `chk i nN`, where `nN` is the parameter's SSA
  value instead of an `iconst`. `chk` already takes two values (`chk i n`,
  c-backend.md D4 row 48), so neither backend's `chk` changes.
- **Element addressing** uses `T`'s size, which is concrete.
- **`N` as an expression** is a read of the parameter.
- **No `slot`, `copy` or `__builtin_memcpy` ever needs a parameter
  length**, because section 3.2 refuses every position that would. Every
  size the backends emit stays a literal, so D5's determinism and the
  no-VLA property survive unchanged.

At a call site the caller passes the solved `L`. That is an `iconst`, or
its own parameter when the caller is itself generic over that length.

### 3.5 The C ABI

`redundantia<N>(b: acies<u8, N>, n: mensura) -> u16` emits once:

```c truth:ignore
uint64_t exs_redundantia(uint64_t N, unsigned char *b, uint64_t n);
```

The length comes first because group (2) precedes group (4). There is
**one symbol per function** and no mangling, because there is no
instantiation, so a header like `custos.h` stays hand-writable and stable.

The trust surface does not change. Today a C host must pass a `b` with 17
readable bytes. After the change it must pass a `b` with `N` readable
bytes. The pointer's extent was always the host's word, never checked by
the unit. A host that lies about `N` gets the same out-of-bounds read
that one passing a short buffer gets today.

**Performance.** Within one translation unit, a call with a literal `N`
is a constant argument to a function the C compiler can see. At `-O2`,
inlining or interprocedural constant propagation can recover the
specialised loop. That is §7.1's "recovered at link time where it
matters", done by the C compiler. It is unmeasured `[UNTESTED]`.

### 3.6 What the two consumers would become

**custos.** `redundantia_sarcinae` is deleted, and entry 23's codex
declares `redundantia<N: mensura>(b: acies<u8, N>, n: mensura)`. Entry 23's
own call sites solve `N := 17` and custos's solves `N := 32`. The 246
golden vectors certify the one body both use. Because entry 23 is a §14
conformance fixture, changing its signature is a spec-visible change and
belongs in the commit that implements this, not here.

**arca.** `nomen_iudica<N: mensura>(b: acies<u8, N>, n: mensura)`, called
as `nomen_iudica(l, nl - 1)` or `nomen_iudica(h, n)`. The 4,096-byte local
and its copy loop disappear. The trailing-`/` test reads from whichever
buffer applies. The expected frame is about 64 bytes instead of 4,160
`[UNTESTED]`, to be measured by the same `-fstack-usage` line as section
1.

### 3.7 Checker impact

- **Pass 1 (resolve).** A `Path` in `acies`'s argument 1 resolves against
  the function's `GenericParam` decls. These are already pushed into the
  `CHK_FR_FN` frame before the signature walk (`resolve.inc`). The same
  lookup is what §8.6's "named constant ... resolved semantically" needs
  for module `firma` constants (section 1, row 2).
- **Types.** `AST_TY_ACIES` carries its length as a literal value today.
  A parameter length needs a distinct representation: a separate type
  kind, or a tagged field holding the `GenericParam` decl id. Interning
  must keep `acies<u8, N>` (parameter) and `acies<u8, 17>` distinct, and
  equal only under a call's substitution.
- **`__chk_ty_genlit`.** It keeps refusing every non-literal that is not a
  length parameter in scope. That `EXS-E0304` is misleading ("wrong
  number of arguments" for a wrong *kind* of argument); whether it should
  be `EXS-E0309` is `[OPEN]` and independent of this design.
- **Rows.** No interaction: a length carries no capability, and §7.1's
  open row-on-type-parameter question does not arise.
- **ADR 0016.** `&mutabilis acies<T, N>` and `EXS-E0310`'s aliasing rule
  are unchanged. Two arguments naming the same storage are refused
  whatever their declared lengths.

### 3.8 Retirement

1. `tests/unit/` fixtures:
   - every acceptance in 3.1 and 3.3, beside its refused twin from 3.2;
   - section 1's three rows turned into their intended results: row 1
     accepted; row 2 accepted or the spec narrowed; row 3 refused by name
     and not by SIGILL.
2. custos and arca rewritten as in 3.6:
   - `examples/custos/proba_c.sh`, `examples/arca/proba_c.sh` and
     `examples/abortus/proba_c.sh` all still pass;
   - entry 23's 246 vectors still pass;
   - arca's frame measured.
3. Both backends in the differential phase, since nothing in the IR is
   new.
