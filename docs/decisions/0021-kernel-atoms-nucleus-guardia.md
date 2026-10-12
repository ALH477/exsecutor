# 0021 — two kernel-side capability atoms: `nucleus` (kernel host) and `guardia` (LSM authority)

**Status:** Proposed, 2026-10-11. Not yet accepted. Amends spec §4.6's
capability set from eleven atoms to thirteen, and §9.1's list to match.
**Relates to:** Oligarchy `docs/exsecutor-kernel-roadmap.md` B.4 (K2 item 6);
`exsecutor-for-linux`; ADR 0020 (the returning trap ABI, which this depends on);
spec §4.6, §9.1, §10.3, §13; `compiler/x86_64/ast/kinds.inc`;
`compiler/x86_64/prelude/prelude.asm`; `tools/syscall-audit.sh`.

## Context

K2 item 6 (Oligarchy's kernel roadmap B.4) names the gap: the atom set is
`Mundus alloc sermo horologium archivum rete fortuna ambitus Filum machina
Crudum` — all process-level. None models a `struct file *`, an LSM hook's
credentials, or an RCU read-side section. A "kernel `Mundus`" is therefore an
amendment to the capability section of the specification plus new diagnostic
codes, under that project's ADR and root-coinage process. The roadmap's own
words: "which forbids inventing a code without amending the spec first."

The roadmap also names the concurrency argument: `Filum` (threads) is an atom
with no implementation, `refero<T>` is non-atomic by design, and there is no
memory model. An LSM hook runs concurrently on every CPU, in atomic context,
under RCU. The strongest argument for the pure-function, allocation-free,
table-walk shape the roadmap recommends is that shape is the only one that
is safe in that context — and that argument is only as strong as the
capability model that enforces it.

Two distinct authorities are needed, and they are not the same:

1. **Being a kernel host.** The emitted unit is linked into a kernel module.
   It runs in atomic context, on every CPU, under RCU. It must not allocate
   (no `kmalloc` in an LSM hook's fast path), must not sleep, must not take
   locks that can sleep, and must not touch user memory without `uaccess`.

2. **Holding LSM authority.** An LSM hook's job is to say yes or no to an
   operation. That authority is a policy decision, not a host property. A
   kernel module can be a host without being an LSM (a telemetry module, a
   device driver), and an LSM's authority is scoped to the hooks it registers,
   not to the whole kernel.

Conflating the two would be the same mistake as putting the standard streams
under `archivum`: it would over-grant in exactly the way §10.3's audit exists
to expose. Two atoms.

## Decision

### 1. Two new atoms, appended (never reordered)

Spec §4.6's list becomes thirteen atoms, in this order:

```
Mundus alloc sermo horologium archivum rete fortuna ambitus Filum machina Crudum nucleus guardia
```

The existing eleven keep their ordinals 1–11. `nucleus` is ordinal 12,
`guardia` is ordinal 13. Appending preserves every existing ordinal; the
frame layout (`cap[11]` → `cap[13]`), the potestates bitmask (`1 shl 0` …
`1 shl 12`), and the audit tool's atom list all grow by the same two at the
end. No existing program's meaning changes.

The names are Latin, per §3.1's rule that identifiers are Latin:

- **`nucleus`** — the kernel, the core, the host environment. A program that
  holds `nucleus` is declaring "I am a kernel host." It is the kernel-side
  analogue of `Mundus` (the process root): `Mundus` says "I am a process
  with a world"; `nucleus` says "I am the world a process runs in."
- **`guardia`** — the guardian, the watcher. A program that holds `guardia`
  is declaring "I hold LSM authority: I may say no to an operation." It is
  scoped to the hooks the module registers, not to the whole kernel.

### 2. What each atom permits

**`nucleus` permits:**

- The `externus` ABI `kernel` (spec §5.3's closed ABI enumeration gains a
  fourth row: `kernel` — the kernel's own C ABI, distinct from
  `sysv_amd64`/`sysv_riscv64`/`o64`). A function declared
  `externus(kernel) functio f(...)` may be called from an emitted unit
  linked into a kernel module. The ABI is the kernel's: `asmlinkage`,
  `noinstr`, the kernel's calling convention, no floating-point.
- The emitted unit's import surface may include kernel symbols beyond
  `exsrt_abortus` — but ONLY those the kernel shim explicitly exports for
  this purpose. The kbuild-shape gate's import-surface check
  (`nm -u` == `{exsrt_abortus}`) is RELAXED under `nucleus` to
  `{exsrt_abortus, <kernel shim exports>}`, and the gate's audit step
  enumerates the kernel shim's `EXPORT_SYMBOL_GPL` list and refuses any
  import not on it.
- No prelude syscalls. `nucleus` does not grant `write(1)`, `exit_group(231)`,
  or any of the eleven atoms' process-level syscalls. The kernel host has its
  own logging (`pr_err`, already in the shim's `exsrt_abortus`). This is the
  atomic-context argument: an LSM hook cannot `write(2)` to a file descriptor.
- No allocation. `alloc` is a separate atom; holding `nucleus` does not imply
  `alloc`. A kernel unit that needs `kmalloc` declares `poscit alloc` AND
  `poscit nucleus`, and the audit checks that `alloc` under `nucleus` admits
  only `kmalloc`/`kfree` with `GFP_NOWAIT` (no sleeping allocation in atomic
  context). This is a [OPEN] refinement; the first version refuses `alloc`
  under `nucleus` outright (no kernel allocation in an LSM hook).

**`guardia` permits:**

- Registering LSM hooks. The kernel shim's hook registration function
  (`security_add_hook` or `register_security_hook`, depending on the kernel
  version) is callable only from a unit whose closure holds `guardia`.
- Returning a verdict from a hook: `0` (admit) or `-EPERM` (deny). The
  emitted function's return type is the hook's verdict type. ADR 0020's
  returning trap ABI is what makes this possible: a trap in a `guardia`
  function returns `-EPERM` through the kernel shim's `exsrt_abortus`, and
  the emitted function returns 0 (admit) — which the LSM's policy layer
  overrides to deny.
- Reading the hook's arguments (the `struct linux_binprm *`, the `struct
  file *`, etc.) as opaque pointers. `guardia` does not grant `archivum`,
  `rete`, or any other atom; it grants the authority to judge, not to act.

**Neither atom permits the other's powers.** A module can be a host
(`nucleus`) without being an LSM (`guardia`), and the two atoms are
independent in a `poscit` clause: `poscit nucleus guardia` declares both,
`poscit nucleus` alone declares host-only.

### 3. The audit tool's atom list grows

`tools/syscall-audit.sh`'s `POTESTATES_VALID` array gains `nucleus` and
`guardia`. The audit admits:

- Under `nucleus`: the kernel's own symbols (enumerated from the shim's
  `EXPORT_SYMBOL_GPL` list, checked at gate time, not hardcoded). No
  process-level syscalls.
- Under `guardia`: the hook registration symbols and the hook's verdict
  return path. No syscalls (an LSM hook does not syscall).
- Under neither: the process-level syscalls of the other eleven atoms,
  unchanged.

### 4. The compiler's atom tables grow

- `compiler/x86_64/ast/kinds.inc`: `AST_CAP_NUCLEUS = 12`,
  `AST_CAP_GUARDIA = 13`, `AST_CAP_COUNT = 13`. `ast_cap_names` gains
  `db 7,'nucleus'` and `db 7,'guardia'` at the end.
- `compiler/x86_64/lower/ssa.inc`: `LWR_CAP_N = 13`. Frame cap slots
  grow from 11 to 13 per frame.
- `compiler/x86_64/backend_fasmg/program.inc`: `BFA_POTESTAS_NUCLEUS =
  1 shl 11`, `BFA_POTESTAS_GUARDIA = 1 shl 12`, `BFA_POTESTAS_COUNT = 13`.
  The `EXS_POTESTAS_*` name table gains two rows. The row width is already
  24 (set by `EXS_POTESTAS_HOROLOGIUM`); `EXS_POTESTAS_NUCLEUS` is 19 chars
  and `EXS_POTESTAS_GUARDIA` is 19 chars, both within the existing width.
- `compiler/x86_64/prelude/prelude.asm`: the `if ~ defined EXS_POTESTAS_*`
  guard block gains two lines for the new atoms. No new prelude routines:
  `nucleus` and `guardia` gate the KERNEL shim's symbols, not the prelude's
  process-level syscalls.
- `compiler/x86_64/checker/rows/intern.inc`: the mask computation's
  `AST_CAP_COUNT` reference picks up the new count automatically (it reads
  the constant). The `rassert rcx le rdx` bound uses `AST_CAP_COUNT - 1`,
  which is now 12.

### 5. The diagnostic codes

No new EXS-E code is needed for the atoms themselves: an undeclared
`nucleus` or `guardia` in a `poscit` clause is `EXS-E0421` (undeclared
capability), the same code as for any other atom. The codes that ARE new:

- **`EXS-E0522`** — `nucleus` or `guardia` declared in a module compiled
  for a process-level target (`--hospes x86_64-linux`, `riscv64-linux`,
  `mips64-none-o64`). The atoms are kernel-only; a process-level module
  cannot hold them. Raised by the checker when the atoms appear in a row
  and the target is not `kernel`.
- **`EXS-E0523`** — `externus(kernel)` declared in a module compiled for a
  process-level target. Same rule: the kernel ABI is kernel-only.

Both are registered in spec §13 as `05xx` (a capability rule, alongside
`EXS-E0520`'s `externus` rule). The kernel target (`--hospes
x86_64-kernel`) is a [OPEN] addition to spec §9.5's closed triple table; this
ADR does not add it. The atoms can be declared and checked under any target;
the checker raises `EXS-E0522`/`EXS-E0523` only when the atoms are used with
a process-level `externus` ABI or a process-level target.

Wait — that is contradictory. Let me restate: the atoms are DECLARABLE under
any target (so a library module can declare `poscit nucleus` and be compiled
for testing), but the `externus(kernel)` ABI and any kernel-symbol import are
REFUSED under a process-level target (`EXS-E0523`). The atoms alone, without
the kernel ABI, are inert: they declare intent, and the linker's import
surface is what makes them real. This is the same shape as `archivum`: the
atom is declarable, but its syscalls are gated by the binary's potestates mask.

### 6. Spec amendments

- **§4.6**: the atom list becomes thirteen. After `Crudum`, two new entries:
  > `nucleus` (the kernel host: atomic context, no process-level syscalls, no
  > allocation by default), `guardia` (LSM authority: hook registration and
  > verdict returns, scoped to the registered hooks).
  The sentence "spending a root on a twelfth atom" is updated: the streams
  are still under `ambitus`, not a twelfth atom; `nucleus` and `guardia` are
  the twelfth and thirteenth, and they are not for streams.
- **§5.3**: the closed ABI enumeration gains `kernel` as a fourth row.
- **§9.1**: the atom list gains `nucleus` and `guardia`.
- **§13**: `EXS-E0522` and `EXS-E0523` are registered.
- **§10.3**: the audit's atom list gains `nucleus` and `guardia`, with the
  admission rules above.

## Consequences

- **The capability model now distinguishes "kernel host" from "LSM authority."**
  A telemetry module declares `poscit nucleus`; an LSM declares
  `poscit nucleus guardia`. The audit can tell them apart, and a module that
  declares `guardia` without `nucleus` is suspicious (an LSM authority with
  no kernel host) — flagged by the audit as a warning, not an error.
- **The atomic-context argument is now enforced, not argued.** The roadmap
  said the pure-function, allocation-free shape is the only safe one in an LSM
  hook. With `nucleus`, the compiler can CHECK that shape: no `alloc` under
  `nucleus` (first version), no process-level syscalls under `nucleus`, no
  `Filum` (threads) under `nucleus` (threads don't exist in atomic context).
  These checks are [OPEN] refinements; the first version only adds the atoms.
- **The frame layout grows by two slots per frame.** `cap[13]` instead of
  `cap[11]`. The frame struct's `fcapb` arithmetic is unchanged (it's an
  offset, not a count). The per-frame caps vec grows from 11 to 13 entries;
  the memory cost is 8 bytes per frame (two `dd`s), negligible.
- **The potestates bitmask grows from 11 to 13 bits.** Both fit in a
  `dword` (32 bits). No overflow.
- **The audit tool's atom list grows.** `POTESTATES_VALID` gains two entries.
  The `--potestates` flag accepts them. Existing invocations are unaffected
  (the new atoms are opt-in, like `archivum`).
- **The prelude's guard block grows by two lines.** No new prelude routines:
  `nucleus` and `guardia` gate the KERNEL shim's symbols, not the prelude's.
- **The kbuild-shape gate's import-surface check is RELAXED under
  `nucleus`.** This is a change to the gate, not just the compiler: the gate
  currently asserts `nm -u` == `{exsrt_abortus}`. Under `nucleus`, it asserts
  `nm -u` is a subset of `{exsrt_abortus} ∪ {kernel shim exports}`. The
  kernel shim's `EXPORT_SYMBOL_GPL` list is the allowlist.
- **Existing programs are unaffected.** No existing program declares
  `nucleus` or `guardia`. The ordinals of the existing eleven atoms are
  unchanged. The frame layout grows, but the growth is internal to the
  compiler (the `cap` vec is per-frame scratch, not a program-visible layout).
- **The spec's "twelfth atom" sentence is updated.** It was about the
  standard streams; `nucleus` and `guardia` are the twelfth and thirteenth,
  and they are for kernel authority, not for streams.

## Open

- The `x86_64-kernel` target triple (spec §9.5's closed table). This ADR adds
  the atoms, not the target. The target is a separate ADR.
- The `externus(kernel)` ABI spelling (spec §5.3's fourth row). This ADR
  names it; the ABI's calling convention, `asmlinkage`/`noinstr` emission, and
  the kernel's argument-passing rules are a separate piece of work.
- `alloc` under `nucleus` (GFP_NOWAIT-only `kmalloc`). First version refuses
  `alloc` under `nucleus` outright.
- `Filum` under `nucleus` (threads in atomic context). First version refuses
  `Filum` under `nucleus`.
- The kernel shim's `EXPORT_SYMBOL_GPL` allowlist. The kbuild-shape gate
  reads it at gate time; the list itself is the kernel shim's, not the
  compiler's.
- LSM hook registration API (`security_add_hook` vs `register_security_hook`,
  kernel-version-dependent). K3 work, not K2.
