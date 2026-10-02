# Guards — audited processes as a flake output

Status: **`lib.buildExsecutorGuard` and `packages.custos-filtrum` are built
and their build-time checks have run; the systemd consumption in §4 is
`[UNTESTED]`.** `nix build .#custos-filtrum` ran to completion in the Nix
sandbox, with the inputs overridden to local clones of the pinned revisions
(see the commit that added this file). The binary it installed is
byte-identical to a hand build: 5,287 bytes, sha256 `2b32f114…7a2df8`.
`nix build --rebuild` reproduced it bit for bit. Its
closure is itself, and nothing else. No systemd unit has started a guard:
this environment has no systemd PID 1 and no NixOS VM. §3 says which parts
of the filter were measured and which were read from source.

## 1. The pattern

A **guard** is an Exsecutor program that takes hostile bytes and holds as
little authority as possible. It is freestanding, with no libc and no
interpreter, so its whole syscall surface is the `syscall` sites in its own
bytes. Two guards exist so far:

- `examples/custos/filtrum.exsc` judges DCF datagrams on stdin and writes
  verdicts to stdout.
- `examples/arca/` judges tar headers. It is a library and has no process
  yet.

The pattern has three layers, and each one closes a gap the others leave:

1. **The type system** (spec §4.6). `initium` derives only the atoms it
   names. filtrum derives `ambitus` from `Mundus` and nothing else. What
   this establishes is a property of the *source*.
2. **The audit** (spec §10.3, `tools/syscall-audit.sh --potestates`). It
   disassembles the binary and requires every `syscall` site to be admitted
   by the declared atoms. What this establishes is a property of the
   *bytes*.
3. **seccomp** (`SystemCallFilter=`). The kernel kills the process on any
   other syscall. What this establishes is a property of the *running
   process*, and it holds even when the audit missed something. One measured
   case of that is §3.3.

`lib.buildExsecutorGuard` makes layer 2 a build step and derives layer 3
from it. That means a consumer gets both without running anything.

## 2. `lib.buildExsecutorGuard`

```nix
lib.buildExsecutorGuard {
  pname, version, src, sources,          # as buildExsecutorProgram
  potestates ? [ "Mundus" "ambitus" ],   # the atoms the program derives
  installCheck ? "",                     # shell run on $out/bin/<pname>
  meta ? { },
}
```

The binary comes out of `buildExsecutorProgram`'s own buildPhase. The
helper adds steps around that phase and never alters the bytes it produces.
It sets `dontStrip` and `dontPatchELF`, so the installed file is the file
fasmg wrote. Before anything is installed, four checks run, and if any one
fails there is no output:

1. **Table drift.** `guardTable` in `flake.nix` transcribes the audit
   script's `POTESTATES_CORE` and `POTESTATES_TABLE`. The build reads both
   dicts back out of the script and fails on any row that differs. It was
   tested by hand against three mutated copies of the script: an extra
   syscall in `alloc`, an extra core syscall, and a new atom. All three were
   refused.
2. **The audit passes** with the declared atoms.
3. **The pass saw something.** At least one syscall site must appear in the
   audit's table.
4. **The declared set is minimal.** For each declared atom other than
   `Mundus`, the audit runs again with that atom dropped. It must exit 1
   (refused), not 0 and not 2 (usage error). The admitted set is a union
   over atoms, so the audit is monotone in them. If every one-smaller set
   fails, then every strictly smaller set fails.

   This check runs at build time, not as a separate test, because it is
   what makes `passthru.systemCallFilter` honest: every atom in the filter
   was shown to be needed by these bytes. For filtrum the check is exactly
   `proba_c.sh`'s "audit fails with `Mundus` alone".

Evaluation refuses four things, and each refusal says why:

- an atom spelling that is not one of §4.6's eleven;
- a duplicate atom;
- `rete`. The audit admits the socket family wholesale for it, so there is
  no closed list to derive a filter from, and a guard that can open a socket
  is not a guard;
- an atom other than `Mundus` whose table row is empty today: `sermo`,
  `horologium`, `archivum`, `fortuna`, `Filum`, `machina`, `Crudum`. Such an
  atom admits nothing, so check 4 could never pass for it.

`Mundus` itself is optional. It admits nothing, and a list without it
builds. Each of these refusals, and both build-time failures, was triggered
on purpose from a scratch flake:

| case | result |
|---|---|
| `[Mundus ambitus alloc]` | built, then refused at check 4: "dropping 'alloc' gave exit 0" |
| `[Mundus]` | refused at check 2 (read and two writes not admitted) |
| `rete`, `sermo`, `mundus` | refused at evaluation |

The audit's tools are `python3` and `binutils-unwrapped`. They are build
inputs of the guard derivation only. `allowedReferences = [ ]` turns "they
are not on the runtime closure" into a build failure rather than a comment.
The audit script is this flake's own copy, referenced by store path, so a
consumer's `src` cannot replace it.

The outputs are:

- `$out/bin/<pname>`, the guard binary;
- `$out/share/exsecutor-guard/<pname>.audit`, the passing audit report;
- passthru `potestates`, the declared atoms;
- passthru `syscalls`, the audited admitted set by name, in syscall-number
  order. It is always the core row, `exit_group` plus `write` (because
  `exsrt_abort` writes to fd 2 under every atom set), together with each
  atom's row;
- passthru `systemCallFilter`, which is `syscalls ++ [ "execve" ]`.

`installCheck` is the gate that exercises what the guard *does*. For
`custos-filtrum` it is `proba_c.sh`'s two runtime checks, run against the
installed binary:

- four records come back with verdicts `0 0 3 6`;
- a truncated record exits 2 and writes nothing.

## 3. The filter, and what was verified

For `custos-filtrum`, `systemCallFilter = [ "read" "write" "exit_group" "execve" ]`.

### 3.1 Measured on the binary

- **The filter is sufficient.** The installed binary ran under a raw
  seccomp-BPF allow-list of exactly `{read, write, exit_group, execve}`.
  The default action was KILL_PROCESS, and any architecture other than
  x86-64 was also killed, as `SystemCallArchitectures=native` would do. The
  launcher was a scratch C file, not part of this tree. Under that filter
  it produced verdicts `0 0 3 6`, and a truncated record exited 2 with no
  output.
- **Each entry is needed.** Removing any one of the four killed the process
  with SIGSYS (exit 159):
  - without `read`, nothing came out;
  - without `write`, nothing came out;
  - without `exit_group`, all four verdicts were written and then the
    process was killed;
  - without `execve`, the launcher could not exec at all. `execve` is the
    launcher's syscall, not the guard's: it is needed because the filter is
    installed before the exec.
- **strace agrees.** `strace -f` over the four-record run shows `execve` 1,
  `read` 271, `write` 4, `exit_group` 1, and nothing else.

### 3.2 Read from systemd's source (v258, the systemd of Oligarchy's nixos-25.11)

These are source reads, not runs.

- **An allow-list always gets `@default` added.** In
  `src/core/load-fragment.c`, `config_parse_syscall_filter` adds `@default`
  on the first non-`~` assignment. `@default` is about 60 syscalls, among
  them `execve`, `exit_group`, `mmap`, `mprotect`, `munmap`, `brk`,
  `getrandom`, `clock_*`, `futex`, and `@sandbox` (`seccomp`, `landlock_*`).
  `systemd-analyze syscall-filter @default` on systemd 255 lists the same
  set without `mseal`/`uretprobe`.

  So under systemd the effective allow-list is `@default ∪ {read, write}`,
  which is wider than the audit. The entries that matter are still outside
  it: `openat`, the whole socket family, `clone`/`fork`, `ptrace`, `kill`,
  and `process_vm_*`. A compromised guard can still call `execve`, but the
  filter survives exec, so whatever it runs is confined the same way.
- **systemd keeps `write` itself.** `apply_syscall_filter` (in
  `src/core/exec-invoke.c`) adds `write` itself for the exec and handoff
  fds. Between loading the filter and `execve` it also allocates (for the
  environment and argv), and per the man page it needs `execve`. That is
  why `@default` cannot simply be denied back out.
- **Tightening is `[OPEN]`.** A second `SystemCallFilter=~…` line removes
  syscalls from an allow-list (`seccomp_parse_syscall_filter`, and the man
  page's read/write example). Some `@default` members could probably be
  removed this way, such as `getrandom`, `@sandbox` and the `*sleep` calls.
  Which of them systemd's own post-filter code needs has not been run, so
  no such line is proposed. `mmap`/`mprotect` are the ones worth closing,
  and `MemoryDenyWriteExecute=yes` already closes their W+X half.

### 3.3 The audit has a gap that seccomp closes (measured)

`tools/syscall-audit.sh` looks for the `syscall` instruction only. An
eight-instruction fasmg probe made one i386 `write` through `int 0x80`, then
`exit_group`. The probe printed `int80`, and the audit passed it with
`--potestates Mundus`. The same probe under the native-arch seccomp filter
was killed with SIGSYS.

exsc emits no `int 0x80`: neither `int 0x80` nor `sysenter` appears
anywhere under `compiler/`, and neither appears in filtrum's disassembly. So
this is not a defect in any guard. It is a
blind spot in layer 2, and it is the measured reason
`SystemCallArchitectures=native` belongs beside every guard's filter.

Reported for whoever owns `tools/`: the audit should refuse `int 0x80` and
`sysenter` outright.

## 4. Consuming a guard from Oligarchy `[UNTESTED]`

The following has not been evaluated against Oligarchy's tree or booted.
One shape that fits filtrum's stdin/stdout protocol is socket activation
with one process per connection. `read` and `write` on an inherited socket
fd need no socket-family syscall, so the guard's filter stays the audited
one.

```nix
{ inputs, pkgs, lib, ... }:
let
  guard = inputs.exsecutor.packages.${pkgs.stdenv.hostPlatform.system}.custos-filtrum;
in {
  # §10.3's "a dependency that gains rete is a one-line diff", across the
  # flake boundary: an exsecutor bump that widens the guard fails eval here.
  assertions = [{
    assertion = guard.potestates == [ "Mundus" "ambitus" ];
    message = "custos-filtrum's capability set changed: ${toString guard.potestates}";
  }];

  systemd.sockets.custos-filtrum = {
    wantedBy = [ "sockets.target" ];
    socketConfig = { ListenStream = "/run/custos-filtrum.sock"; Accept = true; };
  };
  systemd.services."custos-filtrum@" = {
    serviceConfig = {
      ExecStart = "${guard}/bin/custos-filtrum";
      StandardInput = "socket";
      StandardOutput = "socket";
      StandardError = "journal";          # exsrt_abort's fd-2 trap report
      DynamicUser = true;
      SystemCallFilter = lib.concatStringsSep " " guard.systemCallFilter;
      SystemCallArchitectures = "native"; # §3.3
      MemoryDenyWriteExecute = true;
      RestrictAddressFamilies = "none";
      PrivateNetwork = true;
      IPAddressDeny = "any";
      ProtectSystem = "strict";
      ProtectHome = true;
      PrivateDevices = true;
      CapabilityBoundingSet = "";
      NoNewPrivileges = true;
    };
  };
}
```

With an empty `SystemCallErrorNumber=` (the default), a call outside the
filter kills the process with SIGSYS. That is the right failure for a guard,
because the client sees its connection closed with no verdict. The other
option is a process that keeps going after being refused.

Oligarchy's own rule applies here: a gate must exercise what the subsystem
does. The NixOS test this needs would connect to the socket, send the four
records, assert `0 0 3 6`, and then assert that the unit's
`SystemCallFilter` property is non-empty. Without the last step, a filter
silently dropped by a NixOS option rename would keep the test green.

## 5. Versioning proposal

**The situation.** Oligarchy's `flake.nix` names `github:ALH477/exsecutor`
with no ref, and its comment says the lock pins a commit on the
`claude/executor-screensaver-engine-vr1ka5` branch. Its `flake.lock`
actually holds two exsecutor nodes: `exsecutor` at `d581c64` (the root
input) and `exsecutor_2` at `d35b7d2` (hydramesh's own input). So the
distribution builds against two compiler revisions at once.
`nix flake update exsecutor` moves to whatever the default branch is, and
the comment there already warns that this breaks evaluation. A guard is a
security boundary, so its version should be something a reviewer can name.

**Proposal.** Nothing has been tagged. This agent cannot create tags or
push.

1. **Tags `vMAJOR.MINOR.PATCH` on the default branch only.** Semver over
   the flake's public surface:
   - the names of the `packages` outputs;
   - the argument sets of the `lib.*` functions;
   - the passthru names (`potestates`, `syscalls`, `systemCallFilter`);
   - each guard's stream protocol and verdict codes.

   Each guard keeps its own `version` (custos-filtrum `0.1.0`) for its
   protocol. The repository tag moves when any guard or `lib` surface does.
2. **A guard's `potestates` widening is a MAJOR change**, and so is any
   change to its verdict codes. Narrowing is MINOR. The consumer assertion
   in §4 catches a widening even if the tag gets this wrong.
3. **A tag is cut only from a commit where all of these are green:**
   - `nix flake check`;
   - `tests/run.sh`;
   - `make audit`;
   - `make reproduce`;
   - `nix build .#<every guard>`.

   The annotated tag message records each guard's sha256. The builds are
   byte-reproducible (§9.3), so a consumer can rebuild and compare. A
   GitHub release carries the same list.
4. **Oligarchy pins the tag:**

   ```nix
   exsecutor.url = "github:ALH477/exsecutor/v0.5.0";
   hydramesh.inputs.exsecutor.follows = "exsecutor";
   ```

   The `follows` collapses the two lock nodes into one. Whether hydramesh
   builds against the tagged revision is `[OPEN]` until someone runs it.
   With the ref named, `nix flake update` refreshes the lock *within* that
   tag, and moving to a new tag is an edit someone reviews. The lock's
   `narHash` stays the integrity pin: GitHub tags can be force-moved, and
   the tag records intent, not content.
5. **Before the first tag**, the somnium branch and this one need to reach
   the default branch, and `exsc`'s `version = "0.0.0-unreleased"` should
   be derived from the tag rather than written by hand.

## 6. What this does not do

- **No flake `checks` entry.** `nix flake check` evaluates packages but
  does not build them, so a regression in a guard is caught by
  `nix build .#custos-filtrum`, not by `nix flake check`.

  Adding `checks.custos-filtrum = self.packages…` is a one-line change. It
  was left out because README.md's "`nix flake check` runs nine sandboxed
  derivations" sentence is prose, outside any claim block, and is already
  stale: there are thirteen checks, counting `readme` and the `melos`,
  `bicinium` and `auditus` integrity checks. A fourteenth would widen a
  miscount this change has no claim block to correct. The right fix is to
  bind that count in a claim and then add the check.
- **arca has no process yet.** Packaging it as a guard needs an `initium`
  like filtrum's.
- The audit is a heuristic linear sweep, and the script's header says so.
  §3.3 is one thing it misses. The seccomp layer exists because of that,
  not as decoration.
