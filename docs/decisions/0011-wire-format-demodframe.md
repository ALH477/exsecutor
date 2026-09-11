# 0011 — Adopt the DCF DeModFrame as the wire-format reference

**Status:** Accepted, 2026-09-09. Vendored and probed; **implemented in
Exsecutor and certified**, 2026-09-11: §14 entry 23 runs and passes all 246
vectors (the last bullet under Open says what that does and does not show).
**Relates to:** spec §5.2, §9.3, §10.3, §14 (entries 21–23), §17;
`vendor/hydramesh-wire/`, `prototypes/wire/`

## Context

§5.2's `@transitus` is one of this project's stronger claims: byte order in the
type, `:nativus` rejected, no implicit padding, and therefore no
uninitialized-memory disclosure. It was illustrated by exactly one example,
`Capitulum`, written for the spec — every field a whole number of bytes, every
width one the language already had.

That is close to no evidence. An interface claim is tested by something that
did not know the interface existed.

HydraMesh's DCF DeModFrame is available and is the opposite kind of artifact: a
17-byte quantum in production, implemented eleven times in eleven languages,
carrying a finite golden certificate. It is also the same copyright holder's
work, so there is no negotiation to conduct.

## Decision

**Adopt DeModFrame as the canonical `@transitus` example and as a conformance
target**, vendored at `vendor/hydramesh-wire/`.

Three things follow.

**1. It is vendored, not referenced, and not relicensed.** Two files —
`WIRE_QUANTUM_SPEC.md` and `golden_vectors.json` — copied verbatim with
per-file and tree digests under `LC_ALL=C`, exactly as `vendor/fasmg-x86/` is.
A test that reaches into a sibling directory is not reproducible, and §9.3
requires output to be a function of (source, `ego`, lockfile, flags). They keep
their `LGPL-3.0-only` identifier. LGPLv3 permits conveying under GPLv3 and DeMoD
LLC could relicense at will, but neither is needed: nothing here links into
`exsc`, and leaving the licence alone keeps provenance legible. **Do not apply
`LICENSE.EXCEPTION` to these files.**

**2. It falsified §5.2, which is why it was worth doing.** `prototypes/wire/`
implements the format, checks itself against all 246 vectors, and then attempts
the declaration. Seven of nine fields placed. Two did not: `versio`/`genus` are
4-bit fields sharing a byte, and `tempus` is 24 bits. Neither is reachable by
choosing a different byte order.

§5.2 gained bit-width fields as a result. The rule that carried the weight was
not "allow `u4`" but **"declared widths must sum to a whole number of bytes"** —
an extension enforcing "no implicit padding" only between bytes would have
reopened the disclosure class the original rule closed, at bit granularity,
silently.

**3. §14 entry 23 is different in kind from every other conformance entry.**
The other twenty-two are cases this project wrote for itself. Entry 23 is an
external certificate this project must satisfy, and under its own theorem —
encoding is affine over GF(2), so a 246-vector basis spans the space — passing
it is equivalent to agreeing with the reference on all 2^108 frames.

## Consequences

**Positive**

- §5.2's sufficiency claim now rests on a format that did not accommodate it,
  rather than on an example written to fit.
- A complete finite oracle, free. §16's method is that prose designs are
  hypotheses until code runs; this supplies the thing to run against.
- A real interop target. §17 names "it never gets users" as the top failure
  mode, and DeModFrame has ten other implementations to be wrong against.
- The capability system gets a genuine demonstration rather than a toy one:
  encode/decode is pure computation and requires **no** capability, while
  transmitting requires `rete`. §10.3's audit answers "which modules in this
  closure can reach the network" on a real protocol.
- It costs the compiler's build closure nothing. The vendored files are data,
  and `prototypes/wire/` is Python, off the build path (§18).

**Negative**

- **Bit-width fields are a real language extension**, not a clarification.
  Packing order, alignment, and the whole-byte rule are now three more things a
  type checker must enforce and a backend must lower correctly.
- A second vendored tree to keep digest-checked and to re-vendor when upstream
  moves. Its provenance discipline is now this repository's problem.
- `u24` and friends have no obvious in-register representation. The wire layout
  is settled; **how a 24-bit field is held in memory between decode and use
  was not**, and §5.2 said nothing about it. Since answered — see Open,
  below: a `u24` value, zero-extended in the reference backend, three bytes
  on the wire.
- Adopting an external format as a conformance target means an upstream change
  becomes a change here. Mitigated by pinning a commit, not a branch.

**Neutral**

- No new `EXS-E` code was needed. Bit widths not summing to a byte is `E0322`
  (implicit padding); an unannotated multi-byte `@transitus` field *is*
  `:nativus`, already `E0321`; `u4:maior` simply does not parse, `E0201`. That
  the existing registry absorbed a language extension without growing is weak
  evidence the codes were cut along the right joints.

## Open

Closed, as of the M7 bullet at the end: the implementation itself, and the
in-memory representation (for the reference backend). Still open: adapters
above the quantum; the `certus` codec/transport boundary as a worked
example; the C backend's own answer to the representation, which ADR
0012's differential test will give. Bullets are kept in order as the
record.

- **Nothing is implemented.** (Was `[UNTESTED]`; superseded by the M7
  bullet below.) When written there was no Exsecutor compiler, so the
  declaration in §5.2 had never been read by one. Phase 1 of the probe is a
  real test that really passes; phases 2 and 3 were analyses.
- In-memory representation of non-power-of-two widths. **Closed for the
  reference backend**: a `uN` is held zero-extended in 64 bits
  (`docs/design/ssa-ir.md` §2.2); `tests/ir/byte_order.ir` reads `u16`,
  `u24`, `u32` and `u64` in both orders, `u40` and `u56` as `maior` only,
  `u48` as `minor` only; `tests/ir/demodframe_decode.ir` reads `tempus`
  back, and `tests/programs/forma/` does it from source.
  The C backend's answer is `[OPEN]` until ADR 0012's differential test
  covers it.
- Whether adapters above the quantum (DCF-Audio and the rest) are worth
  expressing at all is not decided and is not needed for the point being made.
- The `certus` profile (ADR 0010) forbids `rete` outright. A DeModFrame *codec*
  is pure and profile-clean; a *transport* is not. That boundary looks like a
  good worked example for the profile and has not been written.
- **Appended 2026-09-11.** The in-memory question three bullets up was
  answered in the spec as text before anything ran (it carried
  `[UNTESTED]` then; the bullet above records what has since verified it):
  `docs/design/wire-codec.md` D2 and D5, now in spec §5.2 and §5.4. Byte
  order belongs to the *place*, so a read of `tempus: u24:maior` yields a
  `u24` — an integer in [0, 2^24) — and the reference backend holds every
  `uN` zero-extended in 64 bits (`docs/design/ssa-ir.md` §2.2). Exactly
  three bytes on the wire, exactly a `u24` in the program, and no value has
  a byte-ordered type (`&` of a `@transitus` field is refused, so no address
  carries one either). When this was appended nothing was implemented; M6's
  `forma` (aca9755) then read a `u24` back through a compiled program, and
  the bullet above records the closure.
- **Appended 2026-09-11, M7 of `docs/design/wire-codec.md`.** An Exsecutor
  implementation exists and the certificate runs: §14 entry 23 is
  `status=run`. `tests/conformance/entry23/codex.exsc` is the codec — the
  CRC, the syndrome, `obsigna` (seal), `lege` (read). Every function in it
  is `publica` with no `poscit`, so each row is declared empty and the
  checker refuses any draw, and none takes a capability or a
  capability-bearing value, so each is pure in spec §4.1 rule 6's sense:
  the "encode/decode is pure computation and requires **no** capability"
  bullet above is now a property the harness checks, not a sentence. `probatio.exsc`
  drives it, deriving every basis input from its bit index; the binary's
  syscalls are `write` and `exit_group` only; its 2,502-byte stream matches
  the one `entry23/expecta.py` builds from the vendored JSON — **246/246**
  certificate vectors, the three anchors, decode after encode the identity
  on all 109 basis frames, and the reference's decode order on all 136
  one-bit corruptions of the example frame. Three mechanical mutants
  (polynomial, one field's byte order, the nibble order) each fail at the
  vector the design predicted. What this does NOT show: the extension from
  246 vectors to 2^108 frames rests on the codec being affine, which is an
  argument from reading its code (wire-codec.md section 5), not a
  measurement; and the `u24` bullet above is answered for this backend
  only. The "Nothing is implemented" bullet is superseded by this one and
  kept as the record.
