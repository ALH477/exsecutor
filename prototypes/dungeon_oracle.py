#!/usr/bin/env python3
"""prototypes/dungeon_oracle.py -- the independent oracle for examples/dungeon/.

A procedural 64x64 dungeon chunk, grown from one 64-bit seed, with every step
written the way Python wants it (arbitrary-precision integers, masks written
out, list-of-lists grids) rather than transliterated from the Exsecutor. It
is the thing tests/programs/dungeon/expected.out is held to, so a bug in
dungeon.exsc cannot also be in this file unless it is in the DESIGN, and the
design's claims are measured here first (`selftest`).

    python3 prototypes/dungeon_oracle.py selftest
    python3 prototypes/dungeon_oracle.py show SEED          # ASCII chunk
    python3 prototypes/dungeon_oracle.py stream [-o FILE]   # expected.out

THE DESIGN, in the order the generator runs it. The numbers in brackets are
the draws each step takes from the stream.

  0. SEED.  `chunk_seed(world, index) = mix64(world + index * GAMMA)` mod 2^64.
     Both halves are bijections of u64 (GAMMA is odd, so `index * GAMMA` is
     invertible mod 2^64; the splitmix64 finaliser is two xorshift-rights and
     two odd multiplies, each invertible), so for one world every index below
     2^64 gets a DIFFERENT seed -- by construction, not by probability. That is
     what "a petabyte of distinct chunks" needs: 10^15 / 4096 = 244,140,625,000
     indices, and drawing that many seeds at random from 2^64 would collide
     about n^2 / 2^65 = 1,615 times (the birthday bound).
  1. STREAM.  xorshift64 (13, 7, 17): three shifts, three xors, no multiply.
     State 0 is its fixed point, so a zero seed is replaced by GAMMA. Exactly
     one index per world mixes to zero; that one chunk repeats another's
     layout, and `selftest` says which input that is for world 0.
  2. ROOMS.  A real BSP, flattened: leaf 0 is the chunk; the leaf of largest
     area is split on its longer side 11 times [11 draws]; each of the 12
     leaves holds one room, w, h, x, y [48 draws]. Every leaf side is >= 6 by
     induction (the largest of <= 11 leaves has area >= 372, so its longer
     side is >= 20 and both halves of a split at a/3..2a/3 are >= 6), so a
     room of >= 4x4 with one tile of margin always fits. `selftest` measures
     it as well, but the proof is the reason it holds for every seed.
  3. ROUGHEN + SMOOTH.  Boundary tiles (neighbourhood mixed) flip with
     probability 1/4 [one draw per boundary tile], then four cellular
     automaton passes over the 8-neighbourhood: wall if >= 5 of 8 are wall,
     floor if <= 3, else unchanged. Rooms stop being rectangles.
  4. CORRIDORS.  Leaf k > 0 is joined to the leaf it was split from by an L,
     horizontal-first or vertical-first by one draw [11 draws]. Carved AFTER
     the smoothing, so a one-wide corridor is not eroded by a rule that has no
     idea what a corridor is.
  5. PRUNE.  Floor not 4-connected to room 0's centre becomes wall. The
     corridors already join all 12 centres, so this removes only fragments the
     smoothing split off a room -- and it makes "one connected component" a
     fact about the algorithm instead of about the seeds tried.
  6. DOORS.  A floor tile with wall on two opposite sides, passage on the other
     two, and an open area (>= 5 of its 8 neighbours passable) at one end.
  7. TRAPS.  Ten attempts, [20 draws]; a trap goes only on a floor tile whose
     eight neighbours are all passable.

THE MEMORY BUDGET. 4,096 bytes of chunk plus 8,192 of scratch (the CA's second
buffer, then the prune's explicit stack of 4,096 u16). No allocation.
"""
import sys
import zlib

W = H = 64
N = W * H
M64 = (1 << 64) - 1

WALL, FLOOR, DOOR, TRAP = 0, 1, 2, 3
REACHED = 4          # transient: the prune's mark, never in a finished chunk
PRUNE_REFUSED = 4097
ROOMS = 12
TRAP_TRIES = 10
CA_PASSES = 4

GAMMA = 0x9E3779B97F4A7C15
MIX1 = 0xBF58476D1CE4E5B9
MIX2 = 0x94D049BB133111EB


# ---- 0. the seed -----------------------------------------------------------

def mix64(z):
    z &= M64
    z = ((z ^ (z >> 30)) * MIX1) & M64
    z = ((z ^ (z >> 27)) * MIX2) & M64
    return z ^ (z >> 31)


def chunk_seed(world, index):
    return mix64((world + index * GAMMA) & M64)


def _inv_xorshift_right(z, k):
    # x ^= x >> k is its own inverse after ceil(64/k) rounds
    r = z
    for _ in range((64 + k - 1) // k):
        r = z ^ (r >> k)
    return r


def unmix64(z):
    """The inverse of mix64 -- the proof that it is a bijection, run."""
    inv1 = pow(MIX1, -1, 1 << 64)
    inv2 = pow(MIX2, -1, 1 << 64)
    z = _inv_xorshift_right(z, 31)
    z = (z * inv2) & M64
    z = _inv_xorshift_right(z, 27)
    z = (z * inv1) & M64
    return _inv_xorshift_right(z, 30)


def index_of(world, seed):
    """The chunk index whose seed is `seed`: chunk_seed's inverse."""
    inv = pow(GAMMA, -1, 1 << 64)
    return ((unmix64(seed) - world) * inv) & M64


# ---- 1. the stream ---------------------------------------------------------

class Stream:
    def __init__(self, seed):
        self.s = seed if seed != 0 else GAMMA
        self.draws = 0

    def next(self):
        s = self.s
        s ^= (s << 13) & M64
        s ^= s >> 7
        s ^= (s << 17) & M64
        self.s = s
        self.draws += 1
        return s

    def below(self, n):
        """[0, n) by multiply-shift on the high word: no division, and the
        high bits of xorshift64 are its good ones."""
        assert 1 <= n < (1 << 32)
        return ((self.next() >> 32) * n) >> 32


# ---- the chunk -------------------------------------------------------------

def _nbrs8(g, x, y):
    return [g[(y + dy) * W + x + dx]
            for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dx or dy]


def prune(g, start):
    """Floor not 4-connected to the floor tile `start` becomes wall, in place.
    Returns the tiles pruned, or PRUNE_REFUSED (4097, a count no prune can
    reach) with `g` untouched when `start` is not floor."""
    if g[start] != FLOOR:
        return PRUNE_REFUSED
    seen = {start}
    stack = [start]
    while stack:
        c = stack.pop()
        for d in (-W, W, -1, 1):
            n = c + d
            if g[n] == FLOOR and n not in seen:
                seen.add(n)
                stack.append(n)
    pruned = 0
    for i in range(N):
        if g[i] == FLOOR and i not in seen:
            g[i] = WALL
            pruned += 1
    return pruned


def generate(seed):
    """-> (grid[4096], info). info carries what the properties are asserted on."""
    rng = Stream(seed)
    info = {}

    # 2. rooms: the flattened BSP
    lx, ly, lw, lh, parent = [0], [0], [W], [H], [0]
    while len(lx) < ROOMS:
        best = 0
        for i in range(1, len(lx)):
            if lw[i] * lh[i] > lw[best] * lh[best]:
                best = i
        a = lw[best]
        b = lh[best]
        k = len(lx)
        if a >= b:                               # vertical cut
            lo = a // 3
            p = lo + rng.below(a - 2 * lo + 1)
            lx.append(lx[best] + p); ly.append(ly[best])
            lw.append(a - p);        lh.append(b)
            lw[best] = p
        else:                                    # horizontal cut
            lo = b // 3
            p = lo + rng.below(b - 2 * lo + 1)
            lx.append(lx[best]); ly.append(ly[best] + p)
            lw.append(a);        lh.append(b - p)
            lh[best] = p
        parent.append(best)
    info['leaf_min_side'] = min(min(lw), min(lh))

    g = [WALL] * N
    cx, cy = [], []
    for i in range(ROOMS):
        w = 4 + rng.below(lw[i] - 5)
        h = 4 + rng.below(lh[i] - 5)
        x = lx[i] + 1 + rng.below(lw[i] - 1 - w)
        y = ly[i] + 1 + rng.below(lh[i] - 1 - h)
        assert x >= 1 and y >= 1 and x + w <= W - 1 and y + h <= H - 1
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                g[yy * W + xx] = FLOOR
        cx.append(x + w // 2)
        cy.append(y + h // 2)
    info['rooms'] = list(zip(cx, cy))

    # 3a. roughen: boundary tiles flip with probability 1/4
    src = g
    dst = list(g)
    boundary = 0
    for y in range(1, H - 1):
        for x in range(1, W - 1):
            here = src[y * W + x]
            # boundary: the 3x3 block around the tile is not all one thing
            walls9 = sum(1 for v in _nbrs8(src, x, y) if v == WALL) \
                + (1 if here == WALL else 0)
            if 0 < walls9 < 9:
                boundary += 1
                if rng.next() >> 62 == 0:
                    dst[y * W + x] = WALL if here == FLOOR else FLOOR
    info['boundary'] = boundary
    g = dst

    # 3b. smooth
    for _ in range(CA_PASSES):
        nxt = [WALL] * N
        for y in range(1, H - 1):
            for x in range(1, W - 1):
                walls = sum(1 for v in _nbrs8(g, x, y) if v == WALL)
                if walls >= 5:
                    nxt[y * W + x] = WALL
                elif walls <= 3:
                    nxt[y * W + x] = FLOOR
                else:
                    nxt[y * W + x] = g[y * W + x]
        g = nxt

    # 4. corridors, after the smoothing
    for k in range(1, ROOMS):
        ax, ay = cx[k], cy[k]
        bx, by = cx[parent[k]], cy[parent[k]]
        horizontal_first = rng.next() >> 63 == 0
        if horizontal_first:
            corner = (bx, ay)
        else:
            corner = (ax, by)
        for (x0, y0, x1, y1) in ((ax, ay, corner[0], corner[1]),
                                 (corner[0], corner[1], bx, by)):
            for yy in range(min(y0, y1), max(y0, y1) + 1):
                for xx in range(min(x0, x1), max(x0, x1) + 1):
                    g[yy * W + xx] = FLOOR

    # 5. prune: keep what is 4-connected to room 0's centre
    pruned = prune(g, cy[0] * W + cx[0])
    assert pruned != PRUNE_REFUSED
    info['pruned'] = pruned

    # 6. doors
    def passable(i):
        return g[i] != WALL

    def open_area(i):
        return sum(1 for v in _nbrs8(g, i % W, i // W) if v != WALL) >= 5

    for y in range(1, H - 1):
        for x in range(1, W - 1):
            i = y * W + x
            if g[i] != FLOOR:
                continue
            n_, s_, w_, e_ = i - W, i + W, i - 1, i + 1
            if (not passable(w_) and not passable(e_)
                    and passable(n_) and passable(s_)):
                if open_area(n_) or open_area(s_):
                    g[i] = DOOR
            elif (not passable(n_) and not passable(s_)
                    and passable(w_) and passable(e_)):
                if open_area(w_) or open_area(e_):
                    g[i] = DOOR

    # 7. traps
    for _ in range(TRAP_TRIES):
        x = rng.below(W)
        y = rng.below(H)
        if 1 <= x <= W - 2 and 1 <= y <= H - 2:
            i = y * W + x
            if g[i] == FLOOR and all(v != WALL for v in _nbrs8(g, x, y)):
                g[i] = TRAP

    info['draws'] = rng.draws
    info['walkable'] = sum(1 for v in g if v != WALL)
    return g, info


# ---- digests, the properties, the stream ------------------------------------

def crc32(g):
    return zlib.crc32(bytes(g)) & 0xFFFFFFFF


def components(g):
    """4-connected components of the non-wall tiles, by an independent BFS."""
    seen = [False] * N
    comps = 0
    for s in range(N):
        if g[s] != WALL and not seen[s]:
            comps += 1
            q = [s]
            seen[s] = True
            while q:
                c = q.pop()
                for d in (-W, W, -1, 1):
                    n = c + d
                    if 0 <= n < N and g[n] != WALL and not seen[n]:
                        # a row wrap needs a border tile that is passable
                        assert not (abs(d) == 1 and n // W != c // W)
                        seen[n] = True
                        q.append(n)
    return comps


def render(g):
    glyph = {WALL: '#', FLOOR: '.', DOOR: '+', TRAP: '^'}
    return '\n'.join(''.join(glyph[g[y * W + x]] for x in range(W))
                     for y in range(H))


# The stream expected.out is, in order, as big-endian u64 words except where
# a section says it writes bytes:
#   A  the petabyte arithmetic (5 words)
#   B  chunk_seed(world, index) for every (world, index) below (18 words)
#   C  one record per chunk: seed, walkable, draws, boundary, pruned, crc32
#      (6 words); the first CHUNKS_RAW records are followed by the chunk's
#      4,096 bytes, so a mismatch names a tile and not only a digest
#   D  a rolled digest over DIGEST_RANGE chunks (2 words)
#   E  the prune, on a grid built to have islands in it (3 words)
WORLDS = (0x0000000000000000, 0xDEADBEEFCAFEF00D)
INDICES = (0, 1, 2, 3, 7, 64, 4095, 244140624999, 0xFFFFFFFFFFFFFFFF)
# Direct seeds, to the chunk function. The last two are chunks where the prune
# FIRES (indices 10,220 and 1,585,467 of world 0: it removes 1 tile and 5),
# found by tests/c/dungeon_scan.c over 2,000,000 chunks -- twenty seeds drawn
# at random would almost never meet one.
SEEDS_EXTRA = (0xFFFFFFFFFFFFFFFF, 1, 0xF8536F0B5739D6EC, 0xA8B1E4B207C1C4C8)
CHUNKS_RAW = 4
DIGEST_WORLD = 0xDEADBEEFCAFEF00D
DIGEST_RANGE = 256


def be64(v):
    return v.to_bytes(8, 'big')


def pb_words():
    n = 10**15 // 4096
    return [n, 10**15 % 4096, M64 // n,
            1 if M64 > n else 0,            # 2^64 - 1 > n, the u64 stand-in
            1 if (1 << 32) < n else 0]      # a 32-bit seed cannot index it


def synthetic():
    """A grid with islands in it. Main region: a 11x3 room and a corridor
    joined to it. Islands: a 3x3 block, a lone tile, and a tile that touches
    the corridor's end only at a corner (4-connectivity must refuse it)."""
    g = [WALL] * N
    for y in range(10, 13):
        for x in range(10, 21):
            g[y * W + x] = FLOOR
    for x in range(21, 37):
        g[11 * W + x] = FLOOR
    for y in range(40, 43):
        for x in range(40, 43):
            g[y * W + x] = FLOOR
    g[5 * W + 50] = FLOOR
    g[12 * W + 37] = FLOOR
    return g


def chunk_record(seed, g, info):
    return be64(seed) + be64(info['walkable']) + be64(info['draws']) \
        + be64(info['boundary']) + be64(info['pruned']) + be64(crc32(g))


def stream():
    out = bytearray()
    for v in pb_words():
        out += be64(v)
    for w in WORLDS:
        for i in INDICES:
            out += be64(chunk_seed(w, i))
    raw = 0
    seeds = [chunk_seed(w, i) for w in WORLDS for i in INDICES] + list(SEEDS_EXTRA)
    for sd in seeds:
        g, info = generate(sd)
        out += chunk_record(sd, g, info)
        if raw < CHUNKS_RAW:
            out += bytes(g)
            raw += 1
    acc = 0
    for i in range(DIGEST_RANGE):
        g, info = generate(chunk_seed(DIGEST_WORLD, i))
        acc = (((acc << 7) | (acc >> 57)) & M64) ^ crc32(g)
    out += be64(DIGEST_RANGE) + be64(acc)
    g = synthetic()
    pruned = prune(g, 11 * W + 11)
    refused = prune(g, 0)
    out += be64(pruned) + be64(crc32(g)) + be64(refused)
    return bytes(out)


def selftest(count=3000):
    import collections
    # mix64 is a bijection: its inverse round-trips
    for z in (0, 1, M64, GAMMA, 0x0123456789ABCDEF):
        assert mix64(unmix64(z)) == z and unmix64(mix64(z)) == z, hex(z)
    # splitmix64's reference vector: seed 0, first output (state += GAMMA)
    assert mix64(GAMMA) == 0xE220A8397B1DCDAF, hex(mix64(GAMMA))
    # the index <-> seed bijection, and the one index that mixes to zero
    for w in (0, 12345, M64):
        for i in (0, 1, 244140624999, M64):
            assert index_of(w, chunk_seed(w, i)) == i
    zero_idx = index_of(0, 0)
    print(f"world 0: the one index whose seed is 0 (substituted by GAMMA): "
          f"{zero_idx} = {zero_idx:#x}")

    chunks = 244140625000
    print(f"1 PB / 4096 B = {10**15 // 4096} chunks "
          f"(exact: {10**15 % 4096 == 0}); 2^64 / that = {(1 << 64) // chunks}")

    seen_crc = collections.Counter()
    seen_seed = set()
    lo_leaf, comps_bad, size_bad = 99, 0, 0
    walk = []
    draws = []
    door_ct = trap_ct = prune_hits = 0
    for i in range(count):
        s = chunk_seed(0, i)
        assert s not in seen_seed
        seen_seed.add(s)
        g, info = generate(s)
        lo_leaf = min(lo_leaf, info['leaf_min_side'])
        if components(g) != 1:
            comps_bad += 1
        assert all(v in (WALL, FLOOR, DOOR, TRAP) for v in g)
        # the border is solid
        assert all(g[x] == WALL and g[(H - 1) * W + x] == WALL
                   and g[x * W] == WALL and g[x * W + W - 1] == WALL
                   for x in range(W))
        # every room centre survived
        assert all(g[y * W + x] != WALL for x, y in info['rooms'])
        walk.append(info['walkable'])
        prune_hits += 1 if info['pruned'] else 0
        draws.append(info['draws'])
        door_ct += sum(1 for v in g if v == DOOR)
        trap_ct += sum(1 for v in g if v == TRAP)
        seen_crc[crc32(g)] += 1
    dup = sum(c - 1 for c in seen_crc.values())
    print(f"{count} chunks: distinct layouts {len(seen_crc)} "
          f"(duplicate layouts {dup}); disconnected {comps_bad}")
    print(f"  min leaf side {lo_leaf} (proof says >= 6); "
          f"walkable min/mean/max {min(walk)}/{sum(walk) // count}/{max(walk)}")
    print(f"  the prune fired in {prune_hits} of {count} chunks (it is rare: tests/c/dungeon_scan.c "
          f"measured 283 in 2,000,000)")
    print(f"  draws min/mean/max {min(draws)}/{sum(draws) // count}/{max(draws)}; "
          f"mean doors {door_ct / count:.1f}, mean traps {trap_ct / count:.1f}")
    assert lo_leaf >= 6 and comps_bad == 0 and dup == 0
    print("selftest ok")


def main(argv):
    if len(argv) >= 2 and argv[1] == 'selftest':
        selftest(int(argv[2]) if len(argv) > 2 else 3000)
    elif len(argv) >= 3 and argv[1] == 'show':
        g, info = generate(int(argv[2], 0))
        print(render(g))
        print({k: v for k, v in info.items() if k != 'rooms'})
    elif len(argv) >= 2 and argv[1] == 'stream':
        data = stream()
        if len(argv) >= 4 and argv[2] == '-o':
            with open(argv[3], 'wb') as f:
                f.write(data)
            print(f"{argv[3]}: {len(data)} bytes")
        else:
            sys.stdout.buffer.write(data)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
