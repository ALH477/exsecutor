# AMD device backend — plan

**Status:** `[OPEN]` — phases 2+ (own encoder, shipping kernels) remain plans.
As of 2026-09-14 the execution proof has a landed first stone: Stage 6 G1
(`tests/c/exsrt_shim_amdgpu.c`, see §9) compiled the compiler's own emitted C
to loadable `amdgcn-amdhsa` code objects for both gfx generations on the
development machine. Nothing has been *dispatched* yet — that is G2.
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

- This is **larger than the compiler itself**. This bullet predates
  self-hosting ("the compiler does not exist" is stale — the compiler builds
  itself), and G1 measured the C backend's device route to be far smaller
  than this document feared, but phases 2–5 of §7 stand in full: the
  encoder, the occupancy work, and the cross-generation gates are the bulk.
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

`[UNTESTED]` — the build path of §9 is measured; dispatch and every
on-device statement in this document remain unmeasured as of 2026-09-14.

## 9. 2026-09-14 — Stage 6 G1: the device-C build proof, measured

Stage 6's plan (G1) was: compile the C backend's emitted unit with clang for
`amdgcn-amdhsa`, zero compiler changes, and see what breaks. **Nothing
broke.** This changes §7: the C-backend device-C route **supersedes phase 1**
("assemble hand-written ISA text with LLVM, dispatch via HSA") as the
execution proof — the proof program is the compiler's own output, not a
hand-written kernel. Path B (own encoder) remains the shipping goal per
§18.1; Path A (LLVM) is now *already on the verification path* as the
device-C toolchain, exactly the off-build-path-oracle role §4 assigns it.

**Measured** (nix devShell clang 21.1.8, this machine's two GPUs):

- The shim `tests/c/exsrt_shim_amdgpu.c` — a new shape: **buffer-backed**
  (a device has no descriptors; `scribe` appends to an output buffer, `lege`
  consumes an input buffer, addresses passed as kernargs). Header comment
  lists its five load-bearing decisions: one-workitem kernel; descriptors as
  buffer selectors; the `abortus N` line into the output buffer then
  `s_trap`; overflow visible via a result record; **no rounding/subnormal
  state pinned** (G3 measures the GCN MODE register first — a pin written
  before the measurement would be aspiration).
- `examples/saluta.exsc + imprime + initium` → `--emitte c` (17,815 bytes of
  C) → code objects for **gfx1102 and gfx1103** (this machine's Navi 33 dGPU
  and Phoenix1 APU, kfd `gfx_target_version` 110002/110003): 10,760-byte ELF
  each, `Machine: AMD GPU`, AMDHSA metadata note present,
  `kernarg_segment_size` 296, wavefront 32, **zero undefined symbols** —
  the freestanding unit plus shim needs no libc, no compiler-rt.
- `tests/programs/acies_float8` — deliberately chosen to smoke the Stage 5
  vector prologue — compiles and links identically (29,456-byte code
  objects, both generations): GCC/Clang `vector_size` arithmetic on the
  emitted `exs_vf32_8`/`exs_vf64_8` lowers to GCN with no intrinsics and no
  headers, confirming the plan's no-new-hospes-row argument (GCN flat
  pointers are 64-bit little-endian; the x86_64 row's `_Static_assert`s
  hold as compiled for the device).
- Toolchain lever, same one the mips64 cross phase already uses:
  `env NIX_HARDENING_ENABLE= clang --target=amdgcn-amdhsa -mcpu=gfxNNNN
  -ffreestanding -fno-builtin -nostdlib -fuse-ld=lld` (the nix cc-wrapper's
  hardening default `-fzero-call-used-regs=used-gpr` is unsupported on
  amdgcn; `tests/run.sh:2041` precedent).

**Not measured, deliberately:** no dispatch has run (G2's kfd runner does
that); no value has come back from a device. §5.5's CPU≡GPU sentence keeps
its `[UNTESTED]` until G2/G3 produce the bytes.
