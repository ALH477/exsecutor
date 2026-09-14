# prototypes/

Python design probes. They exist to answer design questions cheaply — before
a decision is committed to `compiler/x86_64/` assembly, where iteration is
expensive. Spec §18.2 states the reason for the split directly: *"The Python
probe layer is the mitigation — design questions get answered where
iteration is cheap, and only settled answers are written in assembly."*

**Never shipped. Never on the build closure.** The compiler is a freestanding
x86-64 assembly program — no libc, no dynamic linking, direct syscalls only
(§18: *"Host language: x86-64 assembly, freestanding..."*) — and its entire
build closure is `{fasmg}` (§18.1: *"The build closure is `{fasmg}`."*).
Spec §18 is equally direct about this directory's status: *"Design probes
stay in Python (`prototypes/`). They are instruments for answering design
questions — never shipped, never on the build closure."* Nothing under
`prototypes/` — script, case file, or generated output — may be referenced
from `Makefile`, the Nix build, or `compiler/`, or otherwise become a build
or test dependency. If a probe here settles a question, the settled answer
goes into the spec and then gets written into assembly by hand; the Python
itself does not travel forward.

These are throwaway instruments, not implementation. "Good enough to trust
the answer it gives" is the bar — not "good enough to maintain."

## Subdirectories

- **`capcheck/`** — probe for the §4 capability-row checker: does positional
  substitution (§4.2) catch capability laundering, and can it be extended to
  cover closure capture (§15 open problem #1, which §16 says blocks Stage
  1)? The original probe is absent from this tree; see `capcheck/README.md`.
- **`stage0-bench/`** — the Stage 0 measurement harness behind §6.2's ARC
  overhead figures and §9.2's compile-speed figures. The C sources and
  timing scripts are absent from this tree; see `stage0-bench/README.md`.
- **`wire/`** — probe for §5.2's `@transitus`: can it describe a wire format
  written by people who had never heard of this language? It could not
  (ADR 0011); §5.2 gained bit-width fields as a result. See `wire/README.md`.
- **`lexicon/`** — materials for §3's derivation scheme and the §16
  human-subjects derivation test. Currently empty; see `lexicon/README.md`.
- **`gendict/`** — probe for §15 open problem #5 and §7.1's `[OPEN]` note:
  the three-way interaction of generics × capability rows × dictionary
  layout, sharpened by §4.2 putting rows in function types. Ten cases
  covering dictionary-reached substitution, witness-table layout stability,
  and whether a dictionary can launder authority past a `dyn` bound
  (`EXS-E0510`); two are found and reproduced as sound negatives, one
  reproducing the original closure-capture defect's shape one level up
  (a type parameter rather than a value). See `gendict/README.md`.
- **`dec754/`** — oracle for the float-literal conversion's unit fixture:
  the expected IEEE-754 bits of every `tests/unit/dec754_golden.asm` row,
  computed by Python's `float()` (itself correctly rounded, so agreement
  is evidence). Also the record of the two traps that bit during
  development -- parse the decimal string, never `m*10.0**e`; struct's
  f32 overflow refusal IS Inf. See `dec754/README.md`.

Every subdirectory README states plainly what exists, what doesn't, and
which spec figures depend on the missing part. That is the point of writing
them.
