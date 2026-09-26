#!/usr/bin/env python3
"""The onus oracle: examples/onus/'s Standard MIDI File, computed
independently in Python.

Verification-only, never on the build closure (spec 18). It writes the bytes
examples/onus/onus_oligarchiae.mid holds -- the score of "Onus Oligarchiae",
Oligarchy's theme -- by different means from the program's.

The SCORE is shared, and cannot be otherwise: the program and this oracle
play the same music, so the tables below are the same tables
(examples/onus/partitura.exsc; `--exsc` prints them in that file's spelling,
which is how they got there). What is independent is everything done WITH
the score:

  - the program walks each track once as a stream of steps, carrying a
    pending delta from one step to the next, and runs every track twice
    (once counting, once writing) to learn its MTrk length;
  - this oracle expands every step to ABSOLUTE-time note-on/note-off
    events, sorts them by (tick, off-before-on, emission order), and only
    then encodes deltas -- the ordinary way a sequencer writes a file --
    with struct.pack for the big-endian fields.

Both must land on the same bytes. The absolute-time form also asserts what
the stream form assumes silently: that every track fills exactly its 40
bars, and that no two notes of one pitch on one channel overlap.

Usage: onus_oracle.py [OUT]         write the .mid (default: stdout)
       onus_oracle.py --exsc        print partitura.exsc's tables
"""

import struct
import sys

PPQ = 96                 # ticks per quarter: a triplet eighth is 32
BAR = 5 * PPQ            # 5/4 -- 480 ticks
BARS = 40
TOTAL = BARS * BAR       # 19,200

# The Mars rhythm: triplet, two quarters, two eighths, a quarter. 5/4.
MARS = [32, 32, 32, 96, 96, 48, 48, 96]
ROLL = [32] * 15

# ---- the score ---------------------------------------------------------------
# Per-bar tables, 40 entries each. Sections: 0-3 exordium (the ostinato
# alone), 4-11 processio (the brass arrive), 12-19 scurra (the jester),
# 20-27 onus ascendit (the chromatic climb), 28-33 culmen, 34-39 coda iocosa.

# Every mode table below means the same thing: 0 rest, 1 sustain the bar,
# 2 the Mars rhythm, 3 a roll (the timpani's alone, whose 1 is special).
# Ostinatum (ch 0).
OST_MODE = [2] * 36 + [0, 0, 1, 0]
OST_PITCH = ([43] * 12 + [43] * 4 + [48, 48, 50, 50]
             + [43, 44, 45, 46, 47, 48, 49, 50]
             + [43] * 6 + [43, 43, 0, 0, 43, 0])
OST_VEL = ([40, 46, 52, 58]
           + [62, 64, 66, 68, 70, 72, 74, 76]
           + [50, 50, 50, 50, 52, 52, 56, 56]
           + [70, 74, 78, 82, 86, 90, 94, 98]
           + [110, 112, 114, 116, 118, 120]
           + [120, 120, 0, 0, 112, 0])

# Aes (ch 1, brass).
AES_MODE = ([0] * 4 + [1] * 8 + [1] * 8 + [2] * 8
            + [1, 1, 1, 1, 2, 2] + [2, 2, 0, 0, 1, 0])
AES_CHORD = (
    [(0, 0, 0)] * 4
    + [(43, 50, 55)] * 2          # G, bare
    + [(43, 50, 56)] * 2          # the flat second bites
    + [(49, 53, 56)] * 2          # D-flat major over the G pedal
    + [(50, 54, 57), (50, 54, 60)]  # D, D7
    + [(43, 50, 59), (43, 50, 59), (48, 55, 64), (50, 57, 66),
       (48, 55, 64), (48, 55, 63), (50, 57, 66), (50, 54, 60)]
    + [(r, r + 7, r + 13) for r in range(55, 63)]   # fifth + minor ninth
    + [(55, 59, 62), (56, 60, 63), (55, 59, 62), (54, 58, 61),
       (55, 58, 62), (50, 54, 60)]
    + [(43, 55, 62)] * 2 + [(0, 0, 0)] * 2 + [(43, 50, 55), (0, 0, 0)]
)
AES_VEL = ([0] * 4
           + [60, 62, 64, 66, 68, 70, 72, 74]
           + [54] * 8
           + [72, 76, 80, 84, 88, 92, 96, 100]
           + [112, 114, 116, 118, 120, 120]
           + [124, 124, 0, 0, 110, 0])

# Tympanum (ch 4): mode 0 rest, 1 one stroke on the downbeat, 2 the Mars
# rhythm, 3 a roll of fifteen triplet eighths.
TYM_MODE = ([0, 0, 1, 1] + [1] * 8 + [1] * 8 + [2] * 8
            + [3, 3, 3, 3, 2, 2] + [2, 2, 0, 0, 1, 0])
TYM_PITCH = ([0, 0, 43, 43] + [43] * 8 + [43, 38] * 4
             + [43, 44, 45, 46, 47, 48, 49, 50]
             + [43] * 6 + [43, 43, 0, 0, 43, 0])
TYM_VEL = ([0, 0, 50, 56] + [60, 60, 62, 62, 64, 64, 66, 66] + [56] * 8
           + [70, 74, 78, 82, 86, 90, 94, 98]
           + [90, 96, 102, 110, 116, 120] + [124, 124, 0, 0, 120, 0])

# Scurra (ch 2, the jester's piccolo) and fagottus (ch 3, bassoon): explicit
# lines, (pitch, ticks), pitch 0 a rest.
SCURRA = (
    [(0, 12 * BAR)]
    # 12
    + [(74, 32), (76, 32), (78, 32), (79, 96), (0, 48), (86, 48), (83, 96), (79, 96)]
    # 13
    + [(81, 48), (79, 48), (78, 48), (76, 48), (74, 96), (0, 96), (85, 32), (86, 32), (0, 32)]
    # 14
    + [(88, 96), (84, 48), (79, 48), (76, 32), (78, 32), (79, 32), (81, 144), (0, 48)]
    # 15
    + [(78, 48), (81, 48), (86, 48), (90, 48), (88, 96), (86, 32), (85, 32), (86, 32), (81, 96)]
    # 16
    + [(79, 32), (81, 32), (79, 32), (76, 96), (72, 96), (0, 48), (76, 48), (79, 96)]
    # 17
    + [(87, 144), (86, 48), (84, 96), (79, 96), (75, 96)]
    # 18
    + [(74, 32), (76, 32), (78, 32), (79, 32), (81, 32), (83, 32),
       (84, 32), (86, 32), (88, 32), (90, 96), (0, 48), (81, 48)]
    # 19
    + [(86, 96), (84, 96), (81, 48), (78, 48), (74, 96), (0, 96)]
    # 20-21
    + [(0, 2 * BAR)]
    # 22-27: squeaks over the climb
    + [(91, 32), (90, 32), (91, 32), (0, 384)]
    + [(0, BAR)]
    + [(0, 192), (92, 32), (91, 32), (92, 32), (0, 192)]
    + [(0, BAR)]
    + [(93, 32), (92, 32), (93, 32), (94, 96), (0, 288)]
    + [(95, 96), (96, 96), (97, 96), (98, 192)]
    # 28-36
    + [(0, 9 * BAR)]
    # 37: ta-da
    + [(0, 96), (86, 32), (88, 32), (90, 32), (91, 48), (0, 48), (86, 48), (83, 48), (79, 96)]
    # 38-39
    + [(91, 384), (0, 96), (0, BAR)]
)

FAGOTTUS = (
    [(0, 12 * BAR)]
    # 12-19: oom-pah-pah, oom-pah (3 + 2)
    + [(43, 96), (50, 96), (50, 96), (43, 96), (50, 96)] * 2
    + [(48, 96), (43, 96), (43, 96), (48, 96), (43, 96)]
    + [(50, 96), (45, 96), (45, 96), (50, 96), (45, 96)]
    + [(48, 96), (43, 96), (43, 96), (48, 96), (43, 96)]
    + [(48, 96), (51, 96), (55, 96), (48, 96), (51, 96)]
    + [(50, 96), (45, 96), (45, 96), (50, 96), (45, 96)]
    + [(50, 96), (48, 96), (45, 96), (42, 96), (38, 96)]   # the comic walk down
    # 20-27: the climb, an octave over the ostinato
    + [(r, BAR) for r in range(55, 63)]
    # 28-33: under the brass
    + [(43, BAR), (44, BAR), (43, BAR), (42, BAR), (43, BAR), (38, BAR)]
    # 34-36
    + [(0, 3 * BAR)]
    # 37: a pickup
    + [(0, 432), (50, 48)]
    # 38-39
    + [(43, 384), (0, 96), (0, BAR)]
)

SCURRA_VEL = 90
FAGOTTUS_VEL = 84

# Conductor: tempo in microseconds per quarter, at a tick.
TEMPI = [(0, 400000), (37 * BAR, 460000), (38 * BAR, 540000)]  # 150, ~130, ~111 bpm

# GM programs (0-based), so the file also plays on any General MIDI synth.
TRACKS = [
    # name, channel, program
    ("Ostinatum", 0, 45),   # pizzicato strings -- col legno
    ("Aes", 1, 57),         # trombone
    ("Scurra", 2, 72),      # piccolo
    ("Fagottus", 3, 70),    # bassoon
    ("Tympanum", 4, 47),    # timpani
]

# ---- gates: how long a note of step length d sounds ---------------------------
# Expressed as the program's shifts, but computed with // here.


def gate_ost(d):
    return d // 2


def gate_aes_sustain(d):
    return d - 24


def gate_three_quarters(d):
    return d - d // 4


def gate_fagottus(d):
    return d - d // 8


# ---- steps -----------------------------------------------------------------------
# A step: (pitches, ticks, sounding ticks, velocity). No pitches = a rest.


def bar_steps(mode, pitches, vel, *, sustain_gate, pattern_gate):
    if mode == 0 or vel == 0:
        return [((), BAR, 0, 0)]
    if mode == 1:
        return [(pitches, BAR, sustain_gate(BAR), vel)]
    if mode == 2:
        return [(pitches, d, pattern_gate(d), vel) for d in MARS]
    if mode == 3:
        return [(pitches, d, gate_three_quarters(d), vel) for d in ROLL]
    raise ValueError(mode)


def steps_ostinatum():
    out = []
    for b in range(BARS):
        out += bar_steps(OST_MODE[b], (OST_PITCH[b],), OST_VEL[b],
                         sustain_gate=gate_three_quarters, pattern_gate=gate_ost)
    return out


def steps_aes():
    out = []
    for b in range(BARS):
        out += bar_steps(AES_MODE[b], AES_CHORD[b], AES_VEL[b],
                         sustain_gate=gate_aes_sustain,
                         pattern_gate=gate_three_quarters)
    return out


def steps_tympanum():
    out = []
    for b in range(BARS):
        m = TYM_MODE[b]
        if m == 1 and TYM_VEL[b]:
            # one stroke: a quarter, then the rest of the bar
            out += [((TYM_PITCH[b],), PPQ, gate_three_quarters(PPQ), TYM_VEL[b]),
                    ((), BAR - PPQ, 0, 0)]
        else:
            out += bar_steps(m, (TYM_PITCH[b],), TYM_VEL[b],
                             sustain_gate=gate_three_quarters,
                             pattern_gate=gate_three_quarters)
    return out


def steps_line(line, vel, gate):
    return [((), d, 0, 0) if p == 0 else ((p,), d, gate(d), vel) for p, d in line]


def all_steps():
    return [
        steps_ostinatum(),
        steps_aes(),
        steps_line(SCURRA, SCURRA_VEL, gate_three_quarters),
        steps_line(FAGOTTUS, FAGOTTUS_VEL, gate_fagottus),
        steps_tympanum(),
    ]


# ---- encoding, the sequencer's way ---------------------------------------------


def vlq(n):
    assert 0 <= n < 1 << 21
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def chunk(tag, body):
    return tag + struct.pack(">I", len(body)) + body


def encode(events):
    """events: (tick, sort_key, bytes). Sort, then delta-encode."""
    events = sorted(events, key=lambda e: (e[0], e[1]))
    body = b""
    now = 0
    for tick, _, data in events:
        body += vlq(tick - now) + data
        now = tick
    return body


def meta(kind, data):
    return bytes([0xFF, kind]) + vlq(len(data)) + data


def conductor():
    ev = [(0, (0, 0), meta(0x03, b"Onus Oligarchiae")),
          (0, (0, 1), meta(0x58, bytes([5, 2, 24, 8]))),      # 5/4
          (0, (0, 2), meta(0x59, bytes([1, 0])))]             # G major
    for i, (t, us) in enumerate(TEMPI):
        ev.append((t, (0, 3 + i), meta(0x51, us.to_bytes(3, "big"))))
    ev.append((TOTAL, (9, 0), meta(0x2F, b"")))
    return encode(ev)


def instrument(name, ch, program, steps):
    ev = [(0, (0, 0), meta(0x03, name.encode("ascii"))),
          (0, (0, 1), bytes([0xC0 | ch, program]))]
    t = 0
    seq = 0
    sounding = {}
    for pitches, d, g, v in steps:
        assert d > 0
        if pitches:
            assert 0 < g <= d, (name, t, d, g)
            for p in pitches:
                assert 0 < p < 128 and 0 < v < 128
                assert sounding.get(p, 0) <= t, (name, "overlap", p, t)
                sounding[p] = t + g
                seq += 1
                # offs sort before ons at the same tick
                ev.append((t, (2, seq), bytes([0x90 | ch, p, v])))
                ev.append((t + g, (1, seq), bytes([0x80 | ch, p, 0x40])))
        t += d
    assert t == TOTAL, (name, t, TOTAL)
    ev.append((TOTAL, (9, 0), meta(0x2F, b"")))
    return encode(ev)


def smf():
    tracks = [conductor()]
    for (name, ch, prog), steps in zip(TRACKS, all_steps()):
        tracks.append(instrument(name, ch, prog, steps))
    head = chunk(b"MThd", struct.pack(">HHH", 1, len(tracks), PPQ))
    return head + b"".join(chunk(b"MTrk", t) for t in tracks)


# ---- partitura.exsc's tables ---------------------------------------------------


def exsc_tables():
    def arr(name, vals, per_line=10):
        rows = [", ".join(str(v) for v in vals[i:i + per_line])
                for i in range(0, len(vals), per_line)]
        body = ",\n        ".join(rows)
        return (f"functio {name}() -> acies<mensura, {len(vals)}> {{\n"
                f"    redde [\n        {body}\n    ];\n}}\n")

    flat_chords = [p for c in AES_CHORD for p in c]
    out = [
        arr("ostinati_modi", OST_MODE), arr("ostinati_soni", OST_PITCH),
        arr("ostinati_vires", OST_VEL),
        arr("aeris_modi", AES_MODE), arr("aeris_soni", flat_chords, 12),
        arr("aeris_vires", AES_VEL),
        arr("tympani_modi", TYM_MODE), arr("tympani_soni", TYM_PITCH),
        arr("tympani_vires", TYM_VEL),
        arr("scurrae_soni", [p for p, _ in SCURRA], 12),
        arr("scurrae_morae", [d for _, d in SCURRA], 12),
        arr("fagotti_soni", [p for p, _ in FAGOTTUS], 12),
        arr("fagotti_morae", [d for _, d in FAGOTTUS], 12),
    ]
    # Track names, ASCII, each in a 16-byte slot: the conductor's, then the
    # five sections in track order.
    names = [b"Onus Oligarchiae"] + [n.encode("ascii") for n, _, _ in TRACKS]
    assert all(len(n) <= 16 for n in names)
    out.append(arr("nomina", [b for n in names for b in n.ljust(16, b"\0")], 16))
    out.append(arr("nominum_longitudines", [len(n) for n in names]))

    return "\n".join(out)


def main(argv):
    if argv[1:] == ["--exsc"]:
        sys.stdout.write(exsc_tables())
        return 0
    data = smf()
    if len(argv) > 1:
        with open(argv[1], "wb") as f:
            f.write(data)
    else:
        sys.stdout.buffer.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
