#!/usr/bin/env python3
"""tests/utf16/corpus.py -- the test inputs, all deterministic.

Nothing here reads a clock, a locale or the host's random source: the one
pseudo-random generator is splitmix64, written out below, so a corpus is a pure
function of its arguments and the same bytes come out under any Python.

Two tiers, because the repository should not carry megabytes of generated
bytes and the libraries should still be held to far more than a fixture:

  fixture()   small enough to commit (tests/data/utf16_*.bin) and cheap enough
              for tests/run.sh: every boundary the standard names, every
              ill-formed class, a systematic walk of Table 3-7, the capacity
              limits, and a seeded fuzz. Both modes, one record each.

  deep()      generated on the fly by matrix.py and never committed: every
              Unicode scalar value alone and in long runs, EVERY 1- and 2-byte
              input, a class-complete walk of 3- and 4-byte inputs, and a large
              seeded fuzz.

  python3 corpus.py write DIR      writes utf16_corpus_{L,B}.bin and
                                   utf16_expected_{L,B}.bin into DIR
"""
import itertools
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle  # noqa: E402

M64 = (1 << 64) - 1


class Splitmix64:
    """Steele, Lea and Flood's splitmix64; the constants are the published ones."""

    def __init__(self, seed):
        self.s = seed & M64

    def next(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & M64
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        return z ^ (z >> 31)

    def below(self, n):
        return self.next() % n

    def pick(self, seq):
        return seq[self.below(len(seq))]


def enc(cp):
    return chr(cp).encode('utf-8')


# Bytes where Table 3-7 changes its mind, and the neighbours that prove it.
EDGE = [0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2,
        0xDF, 0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4, 0xF5,
        0xF7, 0xF8, 0xFF]
LEADS = [0xC0, 0xC1, 0xC2, 0xDF, 0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1,
         0xF3, 0xF4, 0xF5, 0xF8, 0xFF]
SECOND = [0x00, 0x41, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC2, 0xE0]
LATER = [0x7F, 0x80, 0xBF, 0xC0]

SCALARS = [0x00, 0x01, 0x09, 0x0A, 0x0D, 0x20, 0x41, 0x7E, 0x7F, 0x80, 0x81,
           0xA0, 0xA9, 0xFF, 0x100, 0x3A9, 0x7FE, 0x7FF, 0x800, 0x801, 0xFFF,
           0x1000, 0x20AC, 0xD7FE, 0xD7FF, 0xE000, 0xE001, 0xF8FF, 0xFEFF,
           0xFFFD, 0xFFFE, 0xFFFF, 0x10000, 0x10001, 0x1D11E, 0x1F600, 0x2FFFF,
           0xE0001, 0xFFFFF, 0x100000, 0x10FFFE, 0x10FFFF]

TEXTS = [
    'Hello, mundus.', '',
    'Διαφορά',          # Greek
    'Привет, мир',  # Cyrillic
    '你好，世界',                      # CJK
    'שלום مرحبا',   # Hebrew, Arabic
    'aé€\U0001d11e', '\U0001f600\U0001f4a9\U0001f680',
    'é é Å Å',                        # combining, NFC/NFD
    '﻿�￾￿',
    'line1\r\nline2\n\x00end',
]

# Ill-formed prefixes that end a sequence early, to be followed by something.
BAD_PREFIXES = [
    b'\x80', b'\xbf', b'\xc0', b'\xc1', b'\xc2', b'\xdf', b'\xe0', b'\xe0\x80',
    b'\xe0\x9f', b'\xe0\xa0', b'\xe1', b'\xe1\x80', b'\xed', b'\xed\x80',
    b'\xed\xa0', b'\xed\xa0\x80', b'\xed\xbf\xbf', b'\xee', b'\xef\xbf',
    b'\xf0', b'\xf0\x80', b'\xf0\x8f', b'\xf0\x90', b'\xf0\x90\x80', b'\xf1',
    b'\xf1\x80\x80', b'\xf4', b'\xf4\x8f', b'\xf4\x8f\xbf', b'\xf4\x90',
    b'\xf4\x90\x80\x80', b'\xf5', b'\xf8', b'\xf8\x88\x80\x80\x80', b'\xff',
    b'\xfe', b'\xc0\x80', b'\xc1\xbf', b'\xe0\x80\x80', b'\xe0\x9f\xbf',
    b'\xf0\x80\x80\x80', b'\xf0\x8f\xbf\xbf', b'\xf7\xbf\xbf\xbf',
]
FOLLOWERS = [b'A', b'\xc3\xa9', b'\xe2\x82\xac', b'\xf0\x9f\x98\x80', b'\x80',
             b'\xc2', b'\xf0', b'']


def curated():
    """A list of byte strings, no modes yet. Order is part of the corpus."""
    out = []
    for cp in SCALARS:
        out.append(enc(cp) if not 0xD800 <= cp <= 0xDFFF else b'')
    for t in TEXTS:
        out.append(t.encode('utf-8'))
    # a sequence of every scalar that matters, abutting: state must not leak
    out.append(b''.join(enc(cp) for cp in SCALARS))
    out.append(b''.join(enc(cp) for cp in reversed(SCALARS)))
    # every single byte
    for b in range(256):
        out.append(bytes([b]))
    # a lead byte then the edge bytes
    for lead in LEADS:
        out.append(bytes([lead]))
        for s in SECOND:
            out.append(bytes([lead, s]))
            for t in LATER:
                out.append(bytes([lead, s, t]))
                for u in LATER:
                    out.append(bytes([lead, s, t, u]))
    # ill-formed, then something that must survive
    for bad in BAD_PREFIXES:
        for f in FOLLOWERS:
            out.append(bad + f)
            out.append(b'ok' + bad + f + b'ok')
    # two ill-formed in a row, and a long runs of one
    for a, b in itertools.product(BAD_PREFIXES[::3], BAD_PREFIXES[1::4]):
        out.append(a + b)
    return out


def position_sweeps():
    """EVERY byte value at EACH position after a prefix that is mid-sequence.

    Added after an author mutated a decoder's 256-entry byte-class table and
    found the fixture did not notice entry 0xFA: no record had 0xFA after a
    lead, and only 42 of the 256 values ever appeared as a second byte after
    C2/E1/F1. A table-driven decoder has 256 places to be wrong, and a fixture
    that visits 42 of them cannot see the rest. The prefixes are the lead bytes
    and valid partial sequences where each position's allowed range differs.
    """
    prefixes = (
        [b'\xc2', b'\xe0', b'\xe1', b'\xed', b'\xee', b'\xf0', b'\xf1', b'\xf4'] +
        [b'\xe0\xa0', b'\xe1\x80', b'\xed\x9f', b'\xee\x80', b'\xf0\x90',
         b'\xf1\x80', b'\xf4\x8f'] +
        [b'\xf0\x90\x80', b'\xf1\x80\x80', b'\xf4\x8f\xbf'])
    return [p + bytes([b]) for p in prefixes for b in range(256)]


def capacity():
    """Records that press on the 4096-byte limit and on output length = input."""
    return [
        b'', b'A', b'A' * 4095, b'A' * 4096,
        b'\xff' * 4096, b'\x80' * 4096, b'\xc3' * 4096,
        b'\xe2\x82' * 2048, b'\xf0\x9f\x98' * 1365 + b'\xf0',
        b'\xe2\x82\xac' * 1365 + b'\xe2',
        'é'.encode() * 2048,
        b'\xf0\x9f\x98\x80' * 1024,
        b'\xf0\x9f\x98\x80' * 1023 + b'\xf0\x9f\x98',
        (b'a' * 4091) + b'\xf0\x9f\x98\x80',
        (b'a' * 4093) + b'\xe2\x82\xac',
        (b'a' * 4095) + b'\xc3',
        (b'\xc3\xa9' * 2047) + b'\xc3\xa9',
        bytes(range(256)) * 16,
    ]


def valid_text(rng, nunits):
    out = bytearray()
    for _ in range(nunits):
        k = rng.below(10)
        if k < 4:
            cp = rng.below(0x80)
        elif k < 6:
            cp = 0x80 + rng.below(0x780)
        elif k < 9:
            cp = 0x800 + rng.below(0xF800)
            if 0xD800 <= cp <= 0xDFFF:
                cp = 0x20AC
        else:
            cp = 0x10000 + rng.below(0x100000)
        out += enc(cp)
    return bytes(out)


def fuzz(seed, count, maxlen=48):
    """Random bytes, biased toward the bytes that matter; plus valid text with
    bytes deleted, inserted, flipped and truncated."""
    rng = Splitmix64(seed)
    out = []
    for _ in range(count):
        kind = rng.below(4)
        if kind == 0:
            out.append(bytes(rng.below(256) for _ in range(rng.below(maxlen))))
        elif kind == 1:
            out.append(bytes(rng.pick(EDGE) for _ in range(rng.below(maxlen))))
        else:
            data = bytearray(valid_text(rng, 1 + rng.below(12)))
            for _ in range(1 + rng.below(3)):
                if not data:
                    break
                op = rng.below(4)
                pos = rng.below(len(data))
                if op == 0:
                    del data[pos]
                elif op == 1:
                    data.insert(pos, rng.pick(EDGE))
                elif op == 2:
                    data[pos] = rng.pick(EDGE)
                else:
                    del data[pos:]
            out.append(bytes(data))
    return out


def with_modes(strings):
    """Each string in both modes, strict first: the pair is adjacent so a
    reader of a diff sees both verdicts on one input together."""
    out = []
    for s in strings:
        out.append((oracle.STRICT, s))
        out.append((oracle.SUBSTITUE, s))
    return out


def fixture():
    return with_modes(curated() + position_sweeps() + capacity() + fuzz(0x5EED16, 700))


# ---------------------------------------------------------------- deep tier

def scalar_values():
    return [cp for cp in range(0x110000) if not 0xD800 <= cp <= 0xDFFF]


def deep_scalars_each():
    """Every scalar value as its own record, strict (the substitue answer is
    identical on valid input)."""
    return [(oracle.STRICT, enc(cp)) for cp in scalar_values()]


def deep_scalars_runs(per_record=900):
    """Every scalar value in order, packed into long records: 900 scalars is at
    most 3600 bytes, under the 4096 limit."""
    vals = scalar_values()
    return [(oracle.SUBSTITUE, b''.join(enc(cp) for cp in vals[i:i + per_record]))
            for i in range(0, len(vals), per_record)]


def deep_two_byte():
    """EVERY one- and two-byte input, in both modes."""
    return with_modes([bytes([a]) for a in range(256)] +
                      [bytes([a, b]) for a in range(256) for b in range(256)])


def deep_class_complete(width, alphabet=EDGE):
    return with_modes([bytes(t) for t in itertools.product(alphabet, repeat=width)])


def deep_three_byte_all():
    """EVERY three-byte input whose lead is an ill-formed-or-3-byte lead and
    whose tail is the full byte range, in strict mode only. 16.7M records:
    kept out of the default deep set."""
    return [(oracle.STRICT, bytes(t))
            for t in itertools.product(range(256), repeat=3)]


def deep_fuzz(seed=0xDEC0DE, count=60000):
    return with_modes(fuzz(seed, count, maxlen=96))


def deep():
    """name -> list of records. Each becomes one run of every library."""
    return [
        ('scalars-each', deep_scalars_each()),
        ('scalars-runs', deep_scalars_runs()),
        ('every-1-2-byte', deep_two_byte()),
        ('class-complete-3', deep_class_complete(3)),
        ('class-complete-4', deep_class_complete(4)),
        ('fuzz', deep_fuzz()),
    ]


def write_fixture(directory):
    # The big-endian file is every seventh record: byte order is only the
    # driver's serialisation, so it needs a spread, not the whole corpus -- the
    # deep tier runs every record both ways.
    recs = fixture()
    for order, subset in (('L', recs), ('B', recs[::7])):
        with open(os.path.join(directory, 'utf16_corpus_%s.bin' % order), 'wb') as f:
            f.write(oracle.frame(order, subset))
        with open(os.path.join(directory, 'utf16_expected_%s.bin' % order), 'wb') as f:
            f.write(oracle.expected(order, subset))
    return len(recs), len(recs[::7])


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'write':
        print('%d little-endian records, %d big-endian records' % write_fixture(sys.argv[2]))
    else:
        sys.exit(__doc__)
