# prototypes/beneath/

A measurement, not a design: what `openat2(2)`'s `RESOLVE_*` flags refuse on
the host that runs it, and with which errno. It is the evidence under
`docs/design/archivum-beneath.md` (section 2) and ADR 0017. Never shipped,
never on the build closure, never referenced from the `Makefile`, the Nix
build or `compiler/` (`prototypes/README.md`).

It is C rather than Python because the question is about one syscall's
behaviour, and a C probe calls it with nothing in between. libc is used
freely: this is an instrument, not the prelude.

| file | what it is |
|---|---|
| `beneath.c` | builds a tree with every escape shape (`..`, absolute paths, escaping and staying symlinks, a magic link, a procfs, a tmpfs and a bind mount beneath the root, a hard link, a FIFO, a device node), then measures: the resolution matrix over six flag sets; creation through a dangling escaping link; attenuation through a sub-directory descriptor; the kernel's own validation of `struct open_how`; a rename race against `..`; composition with Landlock; what seccomp can and cannot require; the four `open_how` constants the design proposes, checked against the UAPI headers at compile time and passed to the kernel. Every row carries the expectation it is judged against and prints `**UNEXPECTED**` when the kernel disagrees |
| `run.sh` | compiles `beneath.c` and runs it in a fresh scratch directory. Needs root (a private mount namespace for the three mounts) and exits 2 otherwise. `run.sh 10` gives the race ten seconds per leg |
| `site.asm` | the three shapes an `openat2(437)` site can take in a binary, admissible, indeterminate and refused. It is assembled with fasmg and disassembled the way `tools/syscall-audit.sh` disassembles, so the audit specification in the design doc's section 3 is written against real `objdump` output. It is never run |

The anti-vacuity legs are part of the measurement. The creation row is run
again with `resolve = 0` and must create the file outside. The race is run
with `resolve = 0` and must produce escapes, or its `RESOLVE_BENEATH` leg has
proved nothing. On one of eleven runs that leg did not fire. The probe reported
that run as `1 unexpected`, and the design doc counts it as uninformative
rather than as a pass.

What it does not measure:

- **A kernel older than 5.6.** The `ENOSYS` row is a seccomp filter that
  *simulates* one. It shows what a program sees, not what an old kernel does.
- **A non-root caller.** Root bypasses DAC, which is why every `EACCES` in the
  Landlock section can only be Landlock's. An unprivileged run, without the
  mount rows, is `[UNTESTED]`.
- **Oligarchy's own hosts.** Every figure is from this one machine, which the
  design doc records.
