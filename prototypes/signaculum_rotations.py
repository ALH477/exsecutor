#!/usr/bin/env python3
# prototypes/signaculum_rotations.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# The ROTATION TABLE for the logo's animation: twenty-eight 3x3 matrices on
# the same 2^-23 grid as every other float in the EXSG stream, so an
# embedding engine can animate `signaculum_pingue` by patching THIRTY-SIX
# BYTES between renders instead of carrying twenty-eight copies of a
# 44,801-byte stream.
#
# VERIFICATION-ONLY, like prototypes/signaculum_mesh.py and
# tools/render-logo.py: never on the build path, never imported by the
# compiler. Deterministic -- no clock, no randomness, math and struct only
# (not even numpy, which the mesh producer needs for the shading bake and
# this does not). logo/ is not read at all; this file is pure trigonometry.
#
#   python3 prototypes/signaculum_rotations.py   # -> tests/data/signaculum_rotations.bin
#
# WHY A PATCH AND NOT TWENTY-EIGHT STREAMS. prototypes/signaculum_mesh.py's
# format puts the rotation matrix at a FIXED OFFSET near the front:
#
#     offset  bytes  field
#          0      4  magic "EXSG"
#          4      8  2 x u32   vertex count, face count
#         12      8  2 x i32   q23(CAM_D), q23(CAM_F)
#         20     36  9 x i32   R = Rx(pitch) @ Ry(yaw), row-major, 2^-23
#         56    ...           vertices (3 x i32), then faces (3H 3B)
#
# Everything from 56 on is the mesh and the baked per-face colours -- 44,745
# of the stream's 44,801 bytes, identical in every frame. So the animation is
# this file's 1,008 bytes (28 x 36) plus ONE stream, not 1,254,428 bytes of
# near-duplicate. The consumer overwrites [20, 56) and calls the renderer
# again; nothing else in the stream moves, and the decode's own magic and
# count checks still see a well-formed stream.
#
# THE MOTION IS tools/render-logo.py's, NOT A NEW ONE. That script's GIF row
# is the animation this table reproduces, and the two formulas are copied
# from it rather than re-derived:
#
#     t   = i / 28
#     yaw = 0.45 * sin(2*pi*t)
#     pit = -0.14 + 0.05 * cos(2*pi*t)
#
# It is a ROCK, not a turntable, and logo/README.md says why: the mark is an
# extruded flat form, so a full 360 degrees turns it edge-on and loses it.
# It is also NOT a palindrome -- frame 28-i mirrors the yaw but keeps the
# pitch, and the mark is asymmetric (one arrow ascending navy, one descending
# crimson), so a consumer cannot store half the table and play it backwards.
#
# THE HERO VIEW IS NOT IN THIS TABLE, and that matters to anyone checking a
# framebuffer against a pinned digest. signaculum_mesh.py bakes YAW, PITCH =
# 0.16, -0.14 -- render-logo.py's PNG row, the still -- and the animation's
# frame 0 is yaw 0.0, pitch -0.09. They are different views, so the CRC that
# certifies the committed stream's render (tests/programs/signaculum/) is NOT
# any frame of this table. A consumer that wants a digest per animation frame
# has to pin twenty-eight new ones.
#
# R IS CONSTRUCTED THE SAME WAY, and that is checked rather than asserted:
# run with an EXSG stream as argv[1] and this script rebuilds the hero matrix
# from YAW, PITCH and compares it against the nine integers already at offset
# 20 in that stream. They agree exactly, which is the evidence that this
# file's Rx @ Ry ordering, its q23 and the producer's are one convention and
# not two that happen to look alike.
# ---------------------------------------------------------------------------
import math, struct, sys

OUT   = 'tests/data/signaculum_rotations.bin'
SCALE = 1 << 23            # 2^23, as signaculum_mesh.py
FRAMES = 28                # logo/README.md's GIF row: 400 square, 28 frames
ROT_OFFSET, ROT_LEN = 20, 36
HERO_YAW, HERO_PITCH = 0.16, -0.14   # signaculum_mesh.py's still, for the check


def q23(x):
    # Verbatim from prototypes/signaculum_mesh.py: round-to-nearest-even on
    # the 2^-23 grid, then clamp to i32. Python's round() is banker's --
    # deterministic.
    q = int(round(x * SCALE))
    if q < -2147483648 or q > 2147483647:
        sys.exit('fixed-point overflow: %r' % (x,))
    return q


def rotation(yaw, pitch):
    # R = Rx(pitch) @ Ry(yaw), row-major -- signaculum_mesh.py's ordering.
    # Written out rather than via numpy: three-by-three, and this file has no
    # other reason to pull numpy into a verification script.
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    Ry = ((cy, 0.0, sy), (0.0, 1.0, 0.0), (-sy, 0.0, cy))
    Rx = ((1.0, 0.0, 0.0), (0.0, cp, -sp), (0.0, sp, cp))
    return [[sum(Rx[r][k] * Ry[k][c] for k in range(3)) for c in range(3)]
            for r in range(3)]


def frame_angles(i):
    t = i / FRAMES
    return (0.45 * math.sin(2 * math.pi * t),
            -0.14 + 0.05 * math.cos(2 * math.pi * t))


buf = bytearray()
for i in range(FRAMES):
    yaw, pitch = frame_angles(i)
    R = rotation(yaw, pitch)
    buf += struct.pack('<9i', *[q23(R[r][c]) for r in range(3) for c in range(3)])

assert len(buf) == FRAMES * ROT_LEN, len(buf)
open(OUT, 'wb').write(bytes(buf))
print('wrote %s: %d bytes, %d frames x %d'
      % (OUT, len(buf), FRAMES, ROT_LEN))
print('yaw  %+.6f .. %+.6f   pitch %+.6f .. %+.6f'
      % (min(frame_angles(i)[0] for i in range(FRAMES)),
         max(frame_angles(i)[0] for i in range(FRAMES)),
         min(frame_angles(i)[1] for i in range(FRAMES)),
         max(frame_angles(i)[1] for i in range(FRAMES))))

# The convention check described in the header. Optional argument so the
# script's normal job needs no stream.
if len(sys.argv) > 1:
    s = open(sys.argv[1], 'rb').read()
    if s[:4] != b'EXSG':
        sys.exit('%s is not an EXSG stream' % sys.argv[1])
    hero = rotation(HERO_YAW, HERO_PITCH)
    mine = [q23(hero[r][c]) for r in range(3) for c in range(3)]
    theirs = list(struct.unpack_from('<9i', s, ROT_OFFSET))
    if mine != theirs:
        print('this file  :', mine)
        print('the stream :', theirs)
        sys.exit('MISMATCH: the Rx @ Ry convention or q23 differs from the producer')
    print('hero-matrix convention check against %s: AGREE (9/9 integers)'
          % sys.argv[1])
