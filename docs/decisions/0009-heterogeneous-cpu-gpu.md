# 0009 — Heterogeneous CPU/GPU execution

**Status:** Accepted as design, 2026-09-09. **Nothing implemented.** Stage 3+.
**Relates to:** spec §4.6, §5.2, §5.4, §5.5 (new), §6.3, §9.2, §9.5, §10.1,
§10.3; [0007](0007-numeric-semantics.md); `docs/design/norma-algebra.md`

## Context

The project intends numerical work across CPU **and** GPU, with the same source
producing trustworthy results on both. `norma-algebra` had recorded GPU as out of
scope. That was wrong, and re-examining it produced a better answer than expected.

A GPU is not "more cores." It is a separate device with a separate address space,
a separate ISA, its own floating-point behaviour, and — critically — its own
opinions about denormals and FMA contraction. Every one of those is ambient state
if the language stays quiet, which is precisely the failure mode this document
exists to prevent. The existing toolchain answer is that the same source produces
different numbers on the accelerator, discovered later by whoever trusted them.

## Decision

Four rules, in §5.5.

**Placement is in the type.** `acies<f32, N> apud machina` is device-resident;
transfer is explicit. No unified-memory abstraction that silently turns an index
into an interconnect round trip. This is §5.2's rule — representation assumptions
belong in the type — applied to locality. **ARC does not cross the device
boundary**: device buffers are region-allocated, host-owned, released as a block,
which is §6.3's arena model and what device memory wants anyway.

**Reaching a device is a capability.** `machina`, added to §4.6. `poscit machina`
means this call dispatches to an accelerator, and §10.3's audit answers "does
anything in my dependency closure quietly use the GPU?" across the whole closure.

**`@nucleus` restricts a function for device execution**, as `@transitus`
restricts a struct for wire safety — same pattern, same reason. No allocation, no
unbounded recursion, nothing observed of the host. §4.1 rule 6 already supplies
most of it; rule 6 permits allocation and divergence, and a kernel permits
neither, so `@nucleus` is that tightening and the capability system carries the
rest without a new annotation.

**A target that cannot honour the declared `numeri` fails the build.** It never
silently degrades. GPUs routinely force denormal flushing and contract
aggressively; a module declaring `subnormales conservata` will not compile for a
device that cannot deliver it.

### What this buys

Combined with ADR 0007's declared reduction shape:

> The same source, with the same declared decomposition, produces **the same
> bits** on CPU and on GPU.

The tile nest maps onto device hierarchy with no reinterpretation — grid, block,
warp, lane are tiles exactly as cache block and SIMD group are tiles. Because the
nest is declared rather than inferred, the summation order is identical on both,
and the accelerator changes only how long the work takes.

Notably, **the library design did not have to change to accommodate GPU.** The
tile abstraction, chosen for cache blocking and SIMD, already was the device
abstraction. A design that generalises without modification to a case it was not
drawn for is weak evidence that the decomposition is the right one.

## Consequences

**Positive**

- CPU/GPU bit-identity is a property almost nothing offers. For anyone whose
  results must be defensible — scientific, financial, regulated — it is worth
  more than throughput.
- `poscit machina` makes accelerator use auditable across a dependency closure,
  the same way `poscit rete` makes network use auditable.
- The arena model already suited device memory, and the workspace-passed-in
  discipline `norma.algebra` adopted for real-time safety is already the
  `@nucleus` discipline. Two constraints arrived at independently turned out to
  be the same constraint.

**Negative**

- **A second backend.** §9.2 commits to emitting C. Devices need SPIR-V, PTX, or
  device-C, which is a large amount of work for a project whose compiler does not
  exist. This is the dominant cost and it is not close.
- **Some modules will not compile for some devices**, by design. Strict `numeri`
  enforcement means a GPU that cannot preserve subnormals is simply not a valid
  target for a module that requires them. That is the intended behaviour and it
  will still be experienced as the compiler refusing to work.
- Fixed decompositions forgo per-device autotuning, which is how GPU libraries
  normally reach peak throughput. `norma.algebra` excludes autotuning explicitly
  for this reason; the performance gap against cuBLAS will be real and must be
  reported rather than explained away.
- **`acceleratores` widens the `ego` and the build matrix.** Cross-compilation
  (§9.5) already distinguishes build/host/target; devices add a fourth axis.

**Neutral**

- `machina` is genuine Latin and needs no coinage. *Warp*, *workgroup*, *lane*
  and *stride* do — see §3.9 and ADR 0008.

## Open

- **The bit-identity claim is unmeasured.** `[UNTESTED]` It becomes a real claim
  the first time `proba-reproducibilitatem` compares a CPU result against a
  device result byte for byte, and not before. It must not be repeated as fact
  until then.
- Which device targets are honoured first is undecided. SPIR-V is the most
  portable and the least performant; PTX is the reverse.
- Multi-device and device-to-device transfer are not addressed at all.
