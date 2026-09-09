# AMD device backend — plan

**Status:** `[OPEN]` — plan only. No code. Stage 3 at the earliest, realistically Stage 5.
**Relates to:** spec §5.4, §5.5, §9.2, §9.5, §10.1, §18.1;
[ADR 0007](../decisions/0007-numeric-semantics.md),
[0009](../decisions/0009-heterogeneous-cpu-gpu.md); `norma-algebra.md`

---

## 1. Two things are called "microcode." Only one is a target.

**x86 CPU microcode is not viable, and this should be stated plainly before any
effort is spent on it.**

- Modern AMD CPUs verify microcode patches cryptographically. Loading a patch
  you wrote is not an available operation. There is no documented encoding, no
  published assembler, and no supported interface.
- The 2025 disclosure that AMD's verification used AES-CMAC as though it were a
  collision-resistant hash — allowing forged patches on Zen 1–4 — was a
  **security vulnerability, since patched via AGESA**. It was never an interface,
  and a product built on a patched vulnerability has no future and no
  defensibility.
- The genuine reverse-engineering literature (Koppe et al., USENIX Security
  2017) covers AMD K8/K10-era parts — families 0Fh–16h — where microcode was
  effectively unprotected. That hardware is two decades old and irrelevant to a
  dual-EPYC rig.
- The same holds for GPU-side control-processor firmware (CP/MEC): signed,
  undocumented, not a target.

**AMD GPU shader ISA is fully viable, documented, and is what "AMD assembly"
means in high-performance work.** AMD publishes an ISA reference per generation
(GCN, CDNA 1–3, RDNA 1–4). LLVM carries a complete AMDGPU assembler. AMD's own
`rocBLAS` reaches peak throughput via **Tensile**, which emits GCN assembly
directly — the practice this document proposes is the practice AMD itself uses.

Everything below concerns shader ISA.

## 2. Why this fits, and where it fights the thesis

§5.5 already lists `amdgcn-rocm` under `acceleratores`. A device backend is
expected work, not a new direction.

But there is a real tension to name first. ADR 0009 excluded autotuning because
picking a decomposition per machine reproduces the machine-dependent results the
design exists to prevent. **Hand-written per-generation assembly looks like
exactly that.** A gfx942 kernel and a gfx1100 kernel are different code.

The resolution is the same one §5.4 uses, and it constrains the whole effort:

> Device-specific assembly is a **lowering**, never a semantic choice. Two
> kernels for two generations must produce **bit-identical** output for identical
> input, or one of them is wrong.

This is enforceable and must be enforced mechanically (§6). It is also the thing
that makes the library worth writing rather than being one more BLAS.

## 3. What has to be produced

A loadable AMD code object is more than instruction encoding:

| component | what it is |
|---|---|
| instruction encoding | per-generation ISA; VOP/SOP/FLAT/MUBUF/MIMG families, and the matrix ops (MFMA on CDNA, WMMA on RDNA3+) that carry the performance |
| kernel descriptor | 64-byte record: VGPR/SGPR counts, LDS size, `rsrc1`/`rsrc2` bits, kernarg segment size |
| metadata note | AMDHSA metadata, msgpack-encoded in an ELF `.note` — argument layout, dispatch requirements |
| ELF container | `amdgcn-amd-amdhsa`, code object v5 |
| host-side dispatch | HSA queue, AQL packet, signal, argument buffer |

Register allocation and occupancy are the performance problem, not encoding.
Encoding is tedious; occupancy is where the work is.

## 4. Build-closure decision — the fasmg question, again

§18.1 states the closure is `{fasmg}` plus a vendored macro package. An LLVM
dependency would break that claim, so the choice is the same one ADR 0003 faced:

**Path A — emit ISA text, assemble with LLVM `llvm-mc` / clang.**
Fast to stand up, well-tested, tracks new generations for free. Puts LLVM on the
build path.

**Path B — emit the code object directly.**
Consistent with a compiler that already writes ELF itself. No new build
dependency. Costs a per-generation encoder that must be maintained as ISA
revisions land.

**Recommendation: A for validation, B for shipping** — using the precedent the
project already has. `tools/ucd-gen/` is Python, checked in, and explicitly
**off the build path**: it generates artifacts that are verified by regeneration,
never linked. An LLVM-based assembler occupies exactly that role — a
*differential oracle* that our own encoder is checked against, never a
dependency of `make`.

That keeps §18.1 true and still gets the correctness benefit of a mature
assembler.

## 5. Dual-rig: two different problems

"Dual AMD rig" is ambiguous and both readings need answers.

### Dual-socket EPYC — NUMA

Memory locality on a two-socket machine changes throughput by large factors, and
it is currently ambient state. §5.5 already put device residence in the type;
NUMA is the same problem one level down:

```exsecutor
acies<f32, N> apud nodus<0>      // proposed: NUMA node residence
```

This is a natural extension of `apud machina` and needs no new machinery.
Cross-socket access becomes visible rather than mysterious, and a first-touch
allocation policy stops being something you discover with a profiler.

### Dual GPU — peer transfer

ADR 0009 explicitly left device-to-device transfer unaddressed. Two AMD GPUs
communicate over xGMI/Infinity Fabric or fall back to host-staged copies, and the
difference is an order of magnitude.

**The determinism question is the sharp one:** does a 2-GPU run produce the same
bits as a 1-GPU run? Only if the tile nest spans devices and the cross-device
reduction order is declared, exactly as the intra-device order is. If a
multi-device reduction is allowed to combine partial results in completion order,
the whole bit-identity claim collapses at the top of the tree.

So: **device count is part of the declared decomposition**, not a runtime
discovery. `norma-algebra`'s `decompositio` gains an outermost level.

## 6. Verification — the part that makes this defensible

Every kernel ships with a **scalar reference implementation it must match bit for
bit.** Differential testing is not a nice-to-have here; it is the only thing
separating this from a normal GPU library.

| gate | check |
|---|---|
| encoding | our encoder vs. `llvm-mc` on the same ISA text — byte-identical |
| numerics | kernel vs. scalar reference — bit-identical |
| cross-generation | gfx90a vs. gfx942 vs. gfx1100 — bit-identical |
| cross-device-count | 1 GPU vs. 2 GPUs — bit-identical |
| CPU/GPU | §5.5's central claim — bit-identical |
| numeri honoured | build **fails** if the device cannot preserve subnormals or suppress FMA contraction as declared (§5.5) |

The last row is the one vendors will find unreasonable and is not negotiable.

## 7. Phasing

Nothing here starts before the compiler exists.

1. **Oracle only.** Assemble hand-written ISA text with LLVM, dispatch via HSA,
   compare against a scalar reference. Proves the pipeline with no encoder.
2. **One kernel, one generation.** SGEMM on a single CDNA target, MFMA path,
   bit-matched to reference. This is where the occupancy work is learned.
3. **Own encoder for that subset**, checked byte-for-byte against `llvm-mc`.
   §18.1 restored.
4. **Second generation.** RDNA3 WMMA. The cross-generation bit-identity gate
   becomes real here, and this is where the design either holds or breaks.
5. **Multi-device.** Declared cross-device reduction order; the 1-vs-2-GPU gate.
6. **NUMA placement** for the host side.

## 8. Honest assessment

- This is **larger than the compiler itself**, and the compiler does not exist.
  A realistic reading is Stage 5, after self-hosting, or as a separately staffed
  effort.
- The bit-identity gates in §6 are stricter than any shipping GPU library
  enforces. They may prove unachievable on some hardware — particularly where
  matrix units have fixed internal accumulation orders that cannot be made to
  agree with a scalar reference. **If MFMA/WMMA accumulation cannot be
  reconciled with the declared reduction shape, then §5.5's claim does not extend
  to matrix-unit kernels**, and that limitation must be documented rather than
  quietly excepted. This is the single largest technical risk in this plan.
- Performance against rocBLAS will be worse, possibly much worse, because
  autotuning is excluded by design. The correct response is to publish the
  comparison alongside the reproducibility property that buys it.
- Root coinage (§3.9) has real work here: *warp*, *wavefront*, *lane*,
  *occupancy*, *workgroup* all need to descend the ladder, and several will land
  on rung 5 as marked loans.

`[UNTESTED]` — nothing in this document has been built or measured.
