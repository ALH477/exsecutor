# tools/amd-dispatch/ — run a compiled Exsecutor program on an AMD GPU

Dev tooling in the `qemu`/`ucd-gen` role: **never on the compiler's build
path**, never linked into anything that ships. It exists so the suite's
`device=amdgcn` phase can measure the claim spec §5.5 makes — the same
program, the same bytes, on the CPU and on the GPU — instead of asserting
it. The compiler's closed syscall allowlist (§9.3) is about the compiler;
this tool is host C that `dlopen`s the ROCm HSA runtime.

## Build

    cc -O2 -Wall -Wextra -std=c11 -o amd-dispatch tools/amd-dispatch/amd-dispatch.c -ldl

No headers from ROCm are needed: the six HSA structs the tool touches are
mirrored from `hsa.h` (ROCm 6.4.3) with `_Static_assert`s on every offset
written. The runtime library is found at run time, in this order:
`$EXS_HSA_LIBSO`, the loader's own search for `libhsa-runtime64.so.1`, then
the NixOS store (`/nix/store/*-rocm-runtime-*/lib/`, newest name wins —
`rocminfo` links the runtime, but the system profile exposes only `bin/`).
No runtime, no GPU agent, no fine-grained region: each is a distinct loud
exit, never a skip.

## The device build is ONE translation unit

    ./build/exsc aedifica --hospes x86_64-linux SOURCES... --emitte c -o unit.c
    cat unit.c tests/c/exsrt_shim_amdgpu.c > device.c
    env NIX_HARDENING_ENABLE= clang --target=amdgcn-amdhsa -mcpu=gfx1102 \
        -O2 -ffreestanding -fno-builtin -nostdlib -c device.c -o device.o
    env NIX_HARDENING_ENABLE= clang --target=amdgcn-amdhsa -mcpu=gfx1102 \
        -fuse-ld=lld -Wl,-e,exs_amdgcn_entry -Wl,--build-id=none device.o -o device.co

`--hospes x86_64-linux` is right for the device: GCN flat pointers are
64-bit little-endian, which is what that row's `_Static_assert`s pin. The
`NIX_HARDENING_ENABLE=` lever is the mips64 cross phase's (the nix wrapper
adds `-fzero-call-used-regs`, which amdgcn refuses).

**Why one unit, measured.** The kernel descriptor's register and scratch
budget is computed by the compiler for the kernel's translation unit — it
counts only the callees it can see. A kernel linked against a separately
compiled program unit runs with a budget sized for the shim alone (48
VGPRs) while the program uses more (100 in the one-unit build of a
call-chain test), and the wave silently clobbers live values across those
calls: `acies_float8` wrote all 640 correct bytes and then tripped an
overflow check on a garbage operand; a standalone call chain faulted in the
private aperture. As one unit, both are byte-identical to the CPU goldens.
The tool cannot see which shape it was handed, so the rule is written here.

## Run

    amd-dispatch --co device.co [--kernel exs_amdgcn_entry.kd] [--input FILE]
                 --output FILE [--capacity BYTES] [--scratch BYTES]
                 [--timeout SECONDS] [--agent gfx1102] [--list] [--trace]

One AQL kernel-dispatch packet, grid (1,1,1): the shim is one-workitem by
construction (its header, point 1). stdin is `--input`, stdout is the
output buffer (`--capacity`, default 4 MiB) written to `--output`, and a
32-byte result record carries `rc`, bytes stored, bytes dropped and the
abort kind. The stderr line is the whole report:

    amd-dispatch: agent=gfx1102 kernel=exs_amdgcn_entry.kd rc=0 out=101 dropped=0 abort=0

Exit status: the program's `initium` rc (0..255) on a clean completion;
**111** for a device-side abort (`exsecutor: abortus N` is then the tail of
`--output`, the same line `check_run` reads on the CPU), dropped bytes, or a
`--timeout`; 2 usage, 3 no runtime, 4 no agent, 5 an `hsa_*` failure, 6 no
such kernel, 7 fixed private segment larger than `--scratch`.

## What was measured on the way (2026-09-21, gfx1102 Navi 33 + gfx1103 Phoenix)

- The loader names a code-object-v5/v6 kernel by its descriptor symbol,
  `NAME.kd`; the plain name is not found.
- The emitted C keeps real calls, so the kernel declares
  `.uses_dynamic_stack` with a fixed private size of 0, and the dispatcher
  owes it the stack: `--scratch` (default 64 KiB) goes into the packet's
  `private_segment_size`. With 0 the first stack access faults — and with a
  NULL queue error callback that fault takes the host process down at
  address `0x4` inside the runtime. 16 KiB–128 KiB behave alike; 256 KiB
  and up fault outright, and clang refuses to compile a frame above 256 KiB
  (`stack frame size exceeds limit (262136)`): **the per-workitem stack is
  capped at 256 KiB**. `signaculum` at 512² needs 2.9 MB of caller-owned
  buffers on that frame and therefore does not build for the device; it
  stays CPU-only until buffers can live in global memory (G4).
- `__builtin_trap` lowers to `s_trap 2; s_sethalt` — a halted wave never
  completes. The runtime reports `HSA_STATUS_ERROR_EXCEPTION` through the
  queue callback; the tool treats that as the abort's completion (0.4 s).
  `tests/ir/trap_add_carry.ir` on device: `abortus 1` in the buffer, exit
  111.
- Doorbell: `store_write_index(widx+1)`, header store (release), then
  `doorbell = widx` — the index of the packet written, the ROCm reference
  order.
- Programs byte-identical to their CPU goldens on BOTH agents: saluta
  (101 B), acies_float8 (640 B), pictura_triangulum (15,565 B),
  pictura_octonaria (1,555,215 B); float_constants and float_division exit
  100 as expected. Cross-generation identity (amdgpu-backend.md §6) holds
  for every program that ran.

Nothing about rounding-mode or subnormal state is pinned by the shim or the
tool: that is G3's measurement, and the programs above did not need it to
agree with the CPU.
