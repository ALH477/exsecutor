#!/usr/bin/env python3
"""Can spec §5.2's `@transitus` describe a real production wire format?

A design probe, in the sense spec §18 means it: an instrument for answering a
design question, never shipped, never on the build closure.

The question is not rhetorical. §5.2's only worked example, `Capitulum`, was
invented for the spec -- every field a whole number of bytes, every width one
the language already has. A format nobody had to satisfy is weak evidence that
the annotation is sufficient.

`vendor/hydramesh-wire/` supplies one that is not invented: the DCF DeModFrame,
17 bytes, eleven independent implementations, and a finite certificate that is
provably equivalent to agreement on the whole input space.

Two phases:

  1. Implement the format from its specification and check it against all 246
     golden vectors. This validates the reader, not the language -- if the
     layout below is misread, phase 2 is analysis of a format that does not
     exist.

  2. Attempt to express the validated layout as an Exsecutor `@transitus`
     struct, using only the type vocabulary §5.2 actually defines, and report
     every field that cannot be placed.

Run: python3 prototypes/wire/wire_probe.py
Exit: 0 both phases ran, 1 a vector mismatched (phase 1 is a real test).
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
VECTORS = os.path.join(ROOT, "vendor", "hydramesh-wire", "golden_vectors.json")

# --------------------------------------------------------------------------
# The format, transcribed from vendor/hydramesh-wire/WIRE_QUANTUM_SPEC.md.
#
# Field order is the order of the 108 free bits; `bits` is the field width.
# `at` is the byte offset on the wire and `hi` the high bit within that byte
# (7 for a byte-aligned field). Big-endian throughout.
# --------------------------------------------------------------------------
FIELDS = [
    # name        bits  wire byte  high bit
    ("genus",        4,     1,       3),   # flags[3:0] -- frame type 0..3
    ("numerus",     16,     2,       7),   # seq
    ("fons",        16,     4,       7),   # src_id
    ("meta",        16,     6,       7),   # dst_id (0xFFFF = broadcast)
    ("onus",        32,     8,       7),   # payload
    ("tempus",      24,    12,       7),   # 24-bit microsecond offset
]
SYNC = 0xD3
VERSION = 1


def crc16_ccitt(data):
    """CRC-16/CCITT-FALSE. poly=0x1021 init=0xFFFF refin/refout=false xorout=0."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(values):
    """Build the 17-byte frame from a {name: int} mapping."""
    frame = bytearray(17)
    frame[0] = SYNC
    frame[1] = VERSION << 4
    for name, bits, at, hi in FIELDS:
        v = values.get(name, 0) & ((1 << bits) - 1)
        if bits <= 8 and hi != 7:                      # sub-byte field
            frame[at] |= v << (hi - bits + 1)
        else:                                          # whole bytes, big-endian
            for i in range(bits // 8):
                frame[at + i] = (v >> (bits - 8 * (i + 1))) & 0xFF
    crc = crc16_ccitt(frame[0:15])
    frame[15] = crc >> 8
    frame[16] = crc & 0xFF
    return bytes(frame)


def syndrome(word):
    """Validity syndrome: computed CRC xor stored CRC. Zero iff the CRC holds."""
    return crc16_ccitt(word[0:15]) ^ ((word[15] << 8) | word[16])


def unpack_input(bit_index):
    """Map one of the 108 free-input bit positions onto a field and bit.

    Recovered from the certificate itself, then checked against it: fields sit
    in FIELDS order, and within a field the index runs from the value's least
    significant bit upward.
    """
    off = 0
    for name, bits, _at, _hi in FIELDS:
        if off <= bit_index < off + bits:
            return name, bit_index - off
        off += bits
    raise IndexError(bit_index)


# --------------------------------------------------------------------------
# Phase 1 -- the reader is checked before the language is.
# --------------------------------------------------------------------------
def phase1(doc):
    print("== phase 1: implementation vs. the 246-vector certificate ==")
    failures = []

    for anchor, expect in (("crc_123456789", crc16_ccitt(b"123456789")),
                           ("crc_zero15", crc16_ccitt(bytes(15)))):
        want = int(doc["anchors"][anchor], 16)
        mark = "ok  " if want == expect else "FAIL"
        print(f"  [{mark}] {anchor} = 0x{expect:04X} (expected 0x{want:04X})")
        if want != expect:
            failures.append(anchor)

    eb = doc["encode_basis"]
    base = eb[0]
    got = encode({}).hex()
    if got != base["frame"]:
        failures.append("encode_basis[0]")
        print(f"  [FAIL] encode(0): {got} != {base['frame']}")

    for v in eb[1:]:
        name, bit = unpack_input(v["input_bit"])
        got = encode({name: 1 << bit}).hex()
        if got != v["frame"]:
            failures.append(f"encode_basis bit {v['input_bit']}")
    n_enc = len(eb)
    print(f"  [{'ok  ' if not any('encode' in f for f in failures) else 'FAIL'}] "
          f"encode basis: {n_enc} vectors")

    sb = doc["syndrome_basis"]
    zero = bytes.fromhex(sb[0]["word"])
    if syndrome(zero) != sb[0]["syndrome"]:
        failures.append("syndrome_basis[0]")
    for v in sb[1:]:
        k = v["bit"]                       # bit 0 = byte 0 MSB, MSB-first
        word = bytearray(17)
        word[k // 8] = 1 << (7 - k % 8)
        if syndrome(bytes(word)) != v["syndrome"]:
            failures.append(f"syndrome_basis bit {k}")
    n_syn = len(sb)
    print(f"  [{'ok  ' if not any('syndrome' in f for f in failures) else 'FAIL'}] "
          f"syndrome basis: {n_syn} vectors")

    ex = doc["anchors"]["exampleFrame_full"]
    built = encode({"genus": 3, "numerus": 0x1234, "fons": 1, "meta": 0xFFFF,
                    "onus": 0xDEADBEEF, "tempus": 0xAB12CD}).hex()
    mark = "ok  " if built == ex else "FAIL"
    print(f"  [{mark}] exampleFrame: {built}")
    if built != ex:
        failures.append("exampleFrame")
        print(f"         expected: {ex}")

    total = 2 + n_enc + n_syn + 1
    if failures:
        print(f"  {len(failures)} FAILED of {total}")
        return False
    print(f"  all {total} checks pass -- {n_enc + n_syn} certificate vectors")
    print("  Under the certificate's own theorem this equals the reference on")
    print("  all 2^108 frames and classifies all 2^136 words identically.")
    return True


# --------------------------------------------------------------------------
# Phase 2 -- now the actual question.
# --------------------------------------------------------------------------
SPEC_5_2_WIDTHS = {8: "u8", 16: "u16", 32: "u32", 64: "u64"}


def phase2():
    print()
    print("== phase 2: can §5.2 `@transitus` express it? ==")
    placeable, blocked = [], []

    header = [("signum", 8, 0, 7), ("vexilla", 8, 1, 7)]   # sync, flags
    trailer = [("cursus", 16, 15, 7)]                      # crc16
    for name, bits, at, hi in header + FIELDS + trailer:
        sub_byte = bits < 8 or hi != 7
        if sub_byte:
            blocked.append((name, bits, at, "sub-byte field: no bit-width syntax in §5.2"))
        elif bits not in SPEC_5_2_WIDTHS:
            blocked.append((name, bits, at, f"no u{bits} type: §5.2 defines "
                                            f"{', '.join(SPEC_5_2_WIDTHS.values())}"))
        else:
            placeable.append((name, bits, at))

    for name, bits, at in placeable:
        print(f"  [ok  ] {name:9s} byte {at:2d}  {SPEC_5_2_WIDTHS[bits]}:maior")
    for name, bits, at, why in blocked:
        print(f"  [GAP ] {name:9s} byte {at:2d}  {bits:2d} bits -- {why}")

    print()
    print(f"  {len(placeable)} of {len(placeable) + len(blocked)} fields placeable.")
    if blocked:
        print("  §5.2 CANNOT express this frame. The gaps are structural, not")
        print("  cosmetic: `vexilla` carries two 4-bit fields the format's own")
        print("  validity rule reads (version nibble), and `tempus` is 24 bits")
        print("  wide. Neither is expressible by choosing a different byte order.")
    return blocked


# --------------------------------------------------------------------------
# Phase 3 -- the proposed §5.2 extension, checked rather than asserted.
#
# Two additions, each the minimum that closes a gap phase 2 found:
#   (a) any bit width `uN`, 1 <= N <= 64, not just 8/16/32/64;
#   (b) sub-byte fields pack MSB-first in declaration order.
#
# Everything else §5.2 already says must survive, especially "no implicit
# padding" -- which now has to hold at BIT granularity, not byte granularity,
# or the extension quietly reopens the disclosure class the rule closed.
# --------------------------------------------------------------------------
PROPOSED = [
    # name        bits   byte order
    ("signum",      8,   None),      # 0xD3
    ("versio",      4,   None),      # packs MSB-first with genus
    ("genus",       4,   None),
    ("numerus",    16,   "maior"),
    ("fons",       16,   "maior"),
    ("meta",       16,   "maior"),
    ("onus",       32,   "maior"),
    ("tempus",     24,   "maior"),   # the width §5.2 does not have
    ("cursus",     16,   "maior"),
]


def phase3():
    print()
    print("== phase 3: does the proposed extension lay out correctly? ==")
    bit = 0
    problems = []
    for name, bits, order in PROPOSED:
        # Rule: a field wider than one byte must start on a byte boundary,
        # or byte order has nothing to be an order OF.
        if bits > 8 and bit % 8 != 0:
            problems.append(f"{name}: {bits}-bit field starts mid-byte (bit {bit})")
        # Rule: byte order is required above 8 bits and meaningless at or below.
        # An unannotated multi-byte field in @transitus IS :nativus, already
        # EXS-E0321 -- so this needs no new code.
        if bits > 8 and order is None:
            problems.append(f"{name}: multi-byte with no byte order = :nativus = EXS-E0321")
        if bits <= 8 and order is not None:
            problems.append(f"{name}: byte order on a {bits}-bit field is meaningless")
        suffix = f":{order}" if order else ""
        print(f"  bit {bit:3d}  {name:9s} u{bits}{suffix}")
        bit += bits

    print()
    total_bits, total_bytes = bit, bit // 8
    if bit % 8:
        problems.append(f"total {bit} bits is not a whole number of bytes "
                        f"-- trailing {8 - bit % 8} bits would be implicit "
                        f"padding, EXS-E0322")
    print(f"  total: {total_bits} bits = {total_bytes} bytes")

    wire_len = len(encode({}))
    if total_bytes != wire_len:
        problems.append(f"laid out to {total_bytes} bytes, the wire quantum is {wire_len}")

    free = sum(b for n, b, _ in PROPOSED if n not in ("signum", "versio", "cursus"))
    if free != 108:
        problems.append(f"free field bits = {free}, certificate says 108")

    for why in problems:
        print(f"  [FAIL] {why}")
    if not problems:
        print(f"  [ok  ] {total_bytes} bytes, matches the wire quantum exactly")
        print(f"  [ok  ] {free} free bits, matches the certificate's GF(2)^108")
        print("  [ok  ] no implicit padding at bit granularity")
        print("  [ok  ] every field placeable -- the two gaps close")
    return problems


def main():
    if not os.path.exists(VECTORS):
        print(f"wire_probe: {VECTORS} not found", file=sys.stderr)
        return 2
    with open(VECTORS) as fh:
        doc = json.load(fh)

    print(f"DCF DeModFrame vs. Exsecutor §5.2 -- {doc['format']}")
    print()
    passed = phase1(doc)
    blocked = phase2()
    problems = phase3()

    print()
    print("== finding ==")
    if not passed:
        print("  Phase 1 FAILED. Phase 2's analysis describes a format this")
        print("  probe does not correctly implement, and is not evidence.")
        return 1
    print("  §5.2's `@transitus` is INSUFFICIENT for a real production wire")
    print(f"  format. {len(blocked)} of 9 fields cannot be declared.")
    print("  This is a falsification of §5.2's completeness, found by trying a")
    print("  format written by someone who was not thinking about Exsecutor.")
    if problems:
        print()
        print(f"  The proposed extension does NOT close it: {len(problems)} problem(s).")
        return 1
    print("  The proposed extension closes it, and is checked here rather than")
    print("  asserted: 17 bytes, 108 free bits, no bit-level implicit padding.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
