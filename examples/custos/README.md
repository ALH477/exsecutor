# examples/custos/

**The DCF datagram gate.** `custos.exsc` decides whether one UDP datagram in
Punctim's bare dialect is something a DCF relay may forward: a valid 17-byte
`DeModFrame`, or a valid 32-byte SuperPack whose two frames are valid. Anything
else is refused, with a reason code.

It is pure: no `poscit`, no `initium`, no allocation, no I/O. Its consumer is
Punctim's `dcf-ws-bridge` (`web/bridge/`), a Rust WebSocket-to-UDP relay that
used to forward any bytes to any peer. The bridge links the C this file
becomes and calls `admitte` on every datagram, in both directions.

## Why it is built with entry 23

The compilation unit is three files, in this order:

```sh truth:ignore
build/exsc aedifica --hospes x86_64-linux --emitte c \
    tests/conformance/entry23_demodframe_golden_vectors.exsc \
    tests/conformance/entry23/codex.exsc \
    examples/custos/custos.exsc -o custos.gen.c
```

The first file declares `DeModFrame`. The second is the codec that passes
spec §14 entry 23, the 246-vector external certificate. So a 17-byte datagram
is judged by exactly the `lege` that entry 23 certifies, not by a copy.
`examples/tempus/` is built the same way. There is no module system yet; the
file list is the import.

## The rule

This is Punctim's `DCF_MEDIUM_SPEC.md` `udp_bare` decode, with the frame gate
applied as `punctim io` and `dcf.bridge` apply it:

| bytes | admitted iff |
|---|---|
| 17 | sync `0xD3`, version nibble 1, CRC-16/CCITT-FALSE over bytes 0..14 |
| 32 | sync `0xD3`, sflags `0x15`, joint CRC over bytes 0..29, both cores version 1 |
| other | never |

The type nibble is not gated, for frames or for cores. Reserved types 4 to 15
pass, as the medium spec's gate requires.

The core check is not redundant. `unpack` rebuilds each frame with a fresh
CRC, so after rebuilding, the version nibble is the only gate a frame can
still fail. A SuperPack with a correct joint CRC and a core of version 2 is
refused (verdict 5); `proba.c` has one for each core.

The SuperPack layout is a `@transitus` struct, `Sarcina`, declared like
§5.2's `DeModFrame`: a field read is the shift and the mask. The only
arithmetic in the file is the CRC, which is `aut` and a shift by one.

Verdicts: `0` admitted, `1` bad sync, `2` bad version, `3` bad CRC, `4` 32
bytes that are not a SuperPack, `5` a core with version not 1, `6` wrong
length. Only zero versus nonzero is the certified half. The codes are for a
caller's counters.

## The process: `filtrum.exsc`

The library has no authority. `filtrum.exsc` is the smallest program that can
run it. Its `initium` derives `ambitus` (the standard streams) from `Mundus` and
derives nothing else.

The stream format:

- **Input.** One record per datagram: a length byte `n`, then `n` bytes.
- **Output.** One verdict byte per record.
- **Truncation.** A record cut off mid-way exits with status 2 and gets no
  verdict.

Built on the reference backend it is a freestanding binary of 5,287 bytes,
with no interpreter and no dynamic section. `tools/syscall-audit.sh
--potestates Mundus,ambitus` passes on it: the only syscall sites are
`read(0)` and `write(1)`. The same audit with `--potestates Mundus` alone fails,
which shows the audit is reading the binary's real surface.

The point is privilege separation that can be checked. A host can hand
hostile bytes to this child process. Whatever those bytes do to it, the child
cannot open a file, open a socket, map memory, or run another program, because
none of those syscalls appears anywhere in the binary.

Over the 14,523 datagrams of the differential run in the commit that added
`custos`, its verdicts agree with Punctim's Python reference on every one.

## The C face

`custos.h` declares the three functions the unit defines and the one symbol it
imports, `exsrt_abortus`. A bounds or overflow trap is the only thing that
reaches it. `admitte` reads at most `d[0..32)`, and nothing a caller passes
with `n <= 32` traps. A trap is a defect, so a host's `exsrt_abortus` should
stop the process. A host that must keep running anyway (a relay should not die
on one datagram) can wrap its calls in `examples/abortus/`'s guard. The
trap's kind then comes back as a return value. That example drives
`redundantia_sarcinae(d, 33)`, which reads `d[32]`, as one of its traps.

## Checks

`proba_c.sh` (run after `make all`) does seven things:

1. Emits the unit twice and requires the two outputs to be byte-identical.
2. Compiles `custos.h` against the emitted unit.
3. Builds `proba.c` with gcc and clang at `-O0` and `-O2` under UBSan.
4. Runs `proba.c`: 18 verdict anchors (at least one per verdict) plus
   `SUPERPACK_SPEC.md`'s joint-CRC anchor `0x5B75`.
5. Applies five mutants to `custos.exsc` and requires each one to fail
   `proba.c`. The mutants change the joint polynomial, drop core A's version
   check, drop core B's version check, change the sflags type, and refuse
   17-byte frames. They run only after a clean baseline, because a mutant
   failing proves nothing when the original fails too.

6. Checks the capability claim. `admitte` is pure by spec §4.1 rule 6
   because its declared row is empty **and** it takes no capability
   parameter. Each half is tested:
   - **No ambient draw.** A mutant that draws `ambitus`, and another that
     draws `sermo`, without declaring it must each be refused by the
     checker as `EXS-E0421`. Both are refused.
   - **No capability parameter.** A mutant that takes `s: Scriptor` and
     writes through it must break the header check. Rule 4 makes that
     mutant legal Exsecutor: a capability received as a parameter needs no
     `poscit`, since the parameter already shows it in the signature. So
     the checker accepts it, and what refuses it is `custos.h`, which pins
     the signature the consumer links against.

7. Builds `filtrum` freestanding when fasmg and `INCLUDE` are available. It
   then checks:
   - the audit passes with `Mundus,ambitus`;
   - the audit fails without `ambitus`;
   - four records come back with verdicts `0 0 3 6`;
   - a truncated record exits 2 and writes nothing.

An earlier draft of the anchors had no case with a bad version in core B
alone. The core-B mutant passed, and that is how the gap was found.

What it does not do is certify against Punctim's vectors. Those are
Punctim's, and they are not vendored here. The bridge does that work: it runs
this unit over Punctim's committed golden, SuperPack and medium vectors, and
runs a differential sweep against Punctim's Rust reference codec
(`web/bridge/tests/certify_gate.rs` in Punctim).
