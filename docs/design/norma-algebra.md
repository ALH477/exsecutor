# `norma.algebra` — linear algebra on shaped parallel units

**Status:** `[OPEN]` — design only. No code exists. Depends on §5.4 (Stage 2/3),
the type checker (Stage 2), and the backend (Stage 3).
**Relates to:** spec §5.4, §6.1, §6.3, §10.3, §11, §15 #4;
[ADR 0007](../decisions/0007-numeric-semantics.md)

---

## 1. The claim

Every mature BLAS gives you different numbers when you change the thread count,
the blocking factor, or the CPU. This is so normal that numerical scientists
budget for it, and reproducibility papers routinely fail on it.

`norma.algebra`'s single differentiating claim:

> **The same shape declaration produces the same bits — on one core or sixty-four,
> on AVX-512 or a scalar fallback.**

Not "the same to within tolerance". The same bits.

This is not achieved by giving up parallelism. It is achieved by moving one
decision out of the optimizer and into the program: **the shape of the
decomposition**. Once the tile shape is declared, the summation order is fixed;
once the summation order is fixed, the result is determined. The machine then
decides only how long it takes.

## 2. What the language already provides

| from | used for |
|---|---|
| §5.4 `numeri`, `reassociatio vetita` | the compiler cannot silently reorder the arithmetic underneath the library |
| §5.4 declared reduction shape | `summa_arborea(v, k)` is the primitive every dot product is built on |
| §5.4 `acies<T, N>` lanes in the type | vector width never inferred from the host |
| §6.1 rule 5 | `acies<f32>` is already a flat, unboxed, contiguous buffer |
| §6.3 arenas | preallocate-and-reset; no allocator jitter in a hot loop |
| §4 capabilities | threading and allocation are visible in every signature |
| §10.3 audit view | `exsc ego --potestates` shows which parts of a numerical stack thread |

The library adds no new language machinery. That is the point — if it needed
some, that would be evidence §5.4 is wrong.

## 3. Types

Shape is static wherever it can be, so dimension mismatch is a compile error
rather than a runtime check.

```exsecutor
matrix<f32, 4, 4, ordo_linearum>     // 4x4, row-major
matrix<f64, M, N, ordo_columnarum>   // column-major
acies<f32, 8>                        // 8 lanes, §5.4
```

**Layout is in the type**, exactly as byte order is in §5.2. Row-major versus
column-major is the single most common source of silent wrongness in numerical
code, and it is the same class of defect as endianness: a representation
assumption that ought to be visible. Passing a `ordo_linearum` matrix where a
`ordo_columnarum` is expected does not transpose silently; it does not compile.

## 4. Tiles are the unit of parallelism, and they are declared

Real parallel hardware is shaped. SIMD is a 1×N lane group; cache blocking wants
an L1-sized square; a thread partition is a band of rows; a GPU workgroup (out of
scope, §9) is a 2D tile. One abstraction covers all of them:

```exsecutor
tessella<8, 8>        // a tile shape -- compile-time descriptor, not data
```

A decomposition is a nest of tile shapes, and it is an *argument*, not a
heuristic:

```exsecutor
publica functio multiplica(
        a: &matrix<f32, M, K, ordo_linearum>,
        b: &matrix<f32, K, N, ordo_columnarum>,
        d: decompositio { externa: tessella<64,64>, interna: tessella<8,8> })
    -> matrix<f32, M, N, ordo_linearum>
    poscit alloc, Filum
```

`externa` is the cache tile; `interna` is the SIMD tile. Together they fix the
order in which partial products are summed. Change the decomposition and you
have written a different function that computes a different (equally valid)
floating-point result — and the type says so.

## 5. Why this gives determinism for free

A tiled matrix multiply is a reduction tree whose shape is exactly the tile
nest. Fix the nest and the tree is fixed. A fixed tree evaluates to one value.

- **Core count is irrelevant.** Tiles are independent; how many workers execute
  them changes scheduling, not association.
- **ISA is irrelevant.** `interna: tessella<8,8>` lowers to AVX-512 masked ops,
  two AVX2 ops, four NEON ops, or a scalar loop. All four sum in the same order.
- **`reassociatio vetita` (§5.4) is what makes this hold**, because it denies the
  compiler permission to "helpfully" re-tree the arithmetic underneath.

The cost is real and stated in ADR 0007: a fixed tree is slower than letting each
machine reduce in its natural width. This library takes that trade deliberately
and must report it honestly when benchmarked.

## 6. Capabilities make the cost structure legible

This is the language paying off on a problem it was not designed for.

| signature | what it tells the caller |
|---|---|
| `poscit` (nothing) | pure; no allocation, no threads. Safe in an audio callback. |
| `poscit alloc` | allocates a temporary |
| `poscit Filum` | **will spawn threads** |
| `poscit alloc, Filum` | both |

Anyone who has debugged nested-OpenMP oversubscription — a numerical library
silently spawning threads inside an already-parallel loop — knows why this
matters. §10.3's audit view makes it a whole-closure property: `exsc ego
--potestates` answers "does anything in my numerical stack thread?" without
reading a line of its source.

Every operation therefore ships in variants, and the variants differ in their
`poscit`, not in a flag:

```exsecutor
multiplica_in(dest, a, b, d)      // in-place, no alloc, no threads
multiplica(a, b, d)               // allocates the result
multiplica_parallela(a, b, d)     // poscit Filum
```

## 7. Allocation discipline

§6.3 measured arena allocation at 2.8x over malloc and leads its documentation
with *preallocate-and-reset*; it also warns that creating an arena inside an
audio callback is itself an allocation. `norma.algebra` follows that:

- Every operation with a temporary has an in-place or output-parameter form.
- Workspace is passed in, never conjured. A factorization takes its scratch
  buffer as an argument.
- The hot path is `poscit`-free, which is a machine-checked claim, not a comment.

## 8. What this stresses in the language

**Root coinage is the real problem, and this library is its forcing function.**

§15 #4 records that there is no governance process for coining roots and no Latin
for *hash*, *socket*, *mutex*. Linear algebra is worse. `matrix`, `tessella`,
`multiplica`, `transpone`, `norma` are genuine Latin. *Lane*, *eigenvalue*
(German-derived), *workgroup*, *stride*, *pivot* are not, and no amount of
lexicon discipline invents them.

So `norma.algebra` cannot be written until §15 #4 has an answer. That is a
finding, not an obstacle: it converts an abstract governance gap into a concrete
list of words a real library needs. Better to discover it here than after §3 has
been published as settled.

Note also that `norma` is already the standard-library namespace *and* the Latin
for a vector norm. That collision needs resolving before the module is named.

## 9. Out of scope

- **GPU.** `hospites` (§10.1) lists no GPU target. Tiles map naturally onto
  workgroups, so nothing here precludes it, but it is not designed for.
- **Sparse matrices.** Different shape algebra entirely.
- **Dynamic shapes.** Runtime-sized matrices need a dependent-ish shape story
  the type system does not have. Static shapes first.
- **Autotuning.** Deliberately excluded: an autotuner picks a decomposition per
  machine, which is precisely the machine-dependent result this design exists to
  prevent. Tuning belongs in the caller's declared decomposition, where it is
  visible and versioned.

## 10. Status

Nothing here is implemented, measured, or validated. §5.4 is `[OPEN]`, the type
checker is Stage 2, the backend is Stage 3. The determinism claim in section 1 is
a design consequence, not a measurement — it becomes a real claim the first time
`proba-reproducibilitatem` runs a tiled matmul across two core counts and
compares bits. `[UNTESTED]`
