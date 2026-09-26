#!/usr/bin/env python3
"""The metronomus oracle: examples/metronomus/probatio.exsc's stream, computed
independently in Python.

Verification-only, never on the build closure (spec 18). It writes the bytes
tests/programs/metronomus/expected.out holds, and it gets there by different
means wherever a different means exists:

  * division and remainder are Python's `//` and `%`, not shift-and-subtract;
  * the wrapping multiply is `(a * b) % 2**64`;
  * the fixed-step clock is `fractions.Fraction` -- time in exact rational
    ticks, floored -- not an integer accumulator scaled by fons x 256;
  * civil dates are `datetime`, not Hinnant's days-to-date arithmetic;
  * timers are a list of deadlines scanned per tick; a periodic deadline
    advances by `period` in a loop until it passes `now`, not by a quotient;
  * the input buffer is a dict keyed (player, tick), evicting a tick's
    64-apart predecessors when it is written -- the ring's reuse rule stated
    as a property, not as an index computation;
  * Punctim's certificate values come from Punctim's own JSON: embedded
    below verbatim, and re-read from a Punctim checkout when one is given.

Usage: metronomus_oracle.py [--punctim DIR] [OUT]   (default OUT: stdout)

With --punctim, the embedded vectors are checked against
DIR/Documentation/{game,snake}_vectors.json first and the oracle refuses to
write if they differ. The embedded copy is from Punctim commit
99baf3f49e0f8eba2eeb6a66d77ef607de313869, where the two files hash to
  game_vectors.json   c35b11a37acf67b4fa175dcc7501b8fe7c31a5cb3ef9cf4cc58edeaf9943945f
  snake_vectors.json  9c5f22dff5c401b4ca8680675fb7b0b3ca180f3733dc1e3de44e86ee0dd9b96c
"""

import datetime
import json
import os
import struct
import sys
from fractions import Fraction

M64 = 1 << 64
NULLUS = M64 - 1

# game_vectors.json "input_roundtrip", file order.
INPUT_VECTORS = [
    ("000102030405", 66051, 1029),
    ("ffffffffffff", 4294967295, 65535),
    ("000000000000", 0, 0),
    ("0894b730de66", 143963952, 56934),
]

# snake_vectors.json "unwrap", file order: (prev_abs, raw, mod, expect).
UNWRAP_VECTORS = [
    (0, 1, 2048, 1),
    (5, 5, 2048, 5),
    (2047, 0, 2048, 2048),
    (2040, 5, 2048, 2053),
    (100, 98, 2048, 98),
    (0, 2047, 2048, 0),
    (1000, 1500, 2048, 1500),
    (5000, 3, 2048, 4099),
]


def check_against_punctim(root):
    doc = os.path.join(root, "Documentation")
    with open(os.path.join(doc, "game_vectors.json")) as f:
        game = json.load(f)
    with open(os.path.join(doc, "snake_vectors.json")) as f:
        snake = json.load(f)
    live_in = [(v["bytes"], v["fields"]["tick"], v["fields"]["buttons"])
               for v in game["input_roundtrip"]]
    live_un = [(v["prev_abs"], v["raw"], v["mod"], v["expect"])
               for v in snake["unwrap"]]
    if live_in != INPUT_VECTORS:
        sys.exit("metronomus_oracle: game_vectors.json input_roundtrip differs from the embedded copy")
    if live_un != UNWRAP_VECTORS:
        sys.exit("metronomus_oracle: snake_vectors.json unwrap differs from the embedded copy")


class Out:
    def __init__(self):
        self.b = bytearray()

    def u64(self, v):
        assert 0 <= v < M64, v
        self.b += struct.pack(">Q", v)

    def i64(self, v):
        self.u64(1 if v < 0 else 0)
        self.u64(abs(v))

    def raw(self, bs):
        self.b += bs


# ---- the Punctim section -------------------------------------------------------

def unwrap(prev_abs, raw, mod):
    # Punctim's own statement of it (client/src-tauri/src/sync.rs), verbatim
    # in shape: fwd = (raw + MOD - prev % MOD) % MOD; forward if fwd <= MOD/2,
    # else prev.saturating_sub(MOD - fwd).
    fwd = (raw + mod - prev_abs % mod) % mod
    if fwd <= mod // 2:
        return prev_abs + fwd
    return max(0, prev_abs - (mod - fwd))


def sectio_punctim(o):
    for hexs, tick, buttons in INPUT_VECTORS:
        enc = struct.pack(">IH", tick, buttons)
        assert enc.hex() == hexs
        o.raw(enc)
        t, b = struct.unpack(">IH", bytes.fromhex(hexs))
        o.u64(t)
        o.u64(b)
    for prev_abs, raw, mod, expect in UNWRAP_VECTORS:
        got = unwrap(prev_abs, raw, mod)
        assert got == expect, (prev_abs, raw, got, expect)
        o.u64(expect)


# ---- arithmetic ------------------------------------------------------------------

def sectio_arithmetica(o):
    pairs = [(0, 1), (1, 1), (1000000007, 60), (M64 - 1, 1),
             (M64 - 1, 1 << 63), (M64 - 1, M64 - 1), (12345678901234, 97), (5, 0)]
    for n, d in pairs:
        if d == 0:
            o.u64(NULLUS)
            o.u64(n)
        else:
            o.u64(n // d)
            o.u64(n % d)
    for a, b in [(M64 - 1, M64 - 1), (0x2545F4914F6CDD1D, 0x123456789ABCDEF0),
                 (3, 5), (1 << 32, 1 << 32)]:
        o.u64((a * b) % M64)


# ---- the fixed-step clock, in exact rational ticks ----------------------------------

class Clock:
    """Phase is a Fraction of ticks. A pulse of dt source units at speed
    cel/256 adds dt * rate * cel / (256 * fons) ticks; the whole part is
    handed out (capped, the excess dropped as slips), the fraction kept."""

    def __init__(self, fons, rate, cap):
        self.ok = 1 <= fons < 2**31 and 1 <= rate <= 0xFFFF and 1 <= cap <= 0xFFFF
        self.fons = fons if self.ok else 0
        self.rate = rate if self.ok else 0
        self.cap = cap if self.ok else 0
        self.cel = 256
        self.phase = Fraction(0)
        self.ticks = 0
        self.slips = 0

    def pulse(self, dt):
        if not self.ok:
            return 0
        dt = min(dt, self.fons)
        self.phase += Fraction(dt * self.rate * self.cel, 256 * self.fons)
        whole = self.phase.numerator // self.phase.denominator
        self.phase -= whole
        n = min(whole, self.cap)
        self.slips += whole - n
        self.ticks += n
        return n

    def alpha(self):
        if not self.ok:
            return 0
        return (self.phase * 65536).numerator // (self.phase * 65536).denominator

    def reliquum(self):
        # The library's accumulator is the same phase in units of
        # 1 / (fons * 256) of a tick; exact because every addend is.
        r = self.phase * self.fons * 256
        assert r.denominator == 1
        return r.numerator

    def image(self):
        return struct.pack(">QQQIHHHHI", self.ticks, self.reliquum(), self.slips,
                           self.fons, self.rate, self.cap, self.cel, 0, 0)


def floor_frac(num, den):
    return num // den


def sectio_metronomi(o):
    m = Clock(10**9, 60, 5)
    o.u64(1)

    def step(c, dt):
        o.u64(c.pulse(dt))
        o.u64(c.alpha())

    for dt in [16666667, 16666666, 16666667, 8000000, 8666667, 0, 50000000, 2000000000]:
        step(m, dt)
    m.cel = 0
    step(m, 100000000)
    m.cel = 128
    step(m, 33333334)
    m.cel = 512
    step(m, 16666667)
    o.u64(1)                      # celeritatem_pone(65536) refused
    o.u64(m.ticks)
    o.u64(m.reliquum())
    o.u64(m.slips)
    o.u64(floor_frac(m.ticks * 10**9, 60))
    o.raw(m.image())

    b = Clock(46875000, 60, 4)
    for _ in range(3):
        step(b, 781250)
    o.u64(floor_frac(3 * 46875000, 60))

    for args in [(0, 60, 5), (10**9, 0, 5), (2**31, 60, 5), (10**9, 60, 0), (10**9, 0x10000, 5)]:
        o.u64(1 if Clock(*args).ok else 0)
    o.u64(Clock(0, 60, 5).pulse(10**9))

    for t in [0, 16666666, 16666667, 10**9, 3600 * 10**9]:
        o.u64(t * 60 // 10**9)
    for ms in [0, 1, 16, 17, 250, 1000, 16667, 50]:
        o.u64(-(-ms * 60 // 1000))
    o.u64(-(-7 * 1000 // 1000))
    o.u64(-(-100 * 30 // 1000))


# ---- the wall clock -------------------------------------------------------------------

def sectio_horae(o):
    epoch = datetime.datetime(1970, 1, 1)
    cases = [(0, 0), (0, -300), (951782400000, 0), (1758844800123, 0),
             (1758844800123, 330), (1758844800123, -420),
             (4102444799999, 0), (253402300799000, 0)]
    for ms, zone in cases:
        local = ms + zone * 60000
        if local < 0:
            local = 0
        t = epoch + datetime.timedelta(milliseconds=local)
        weekday = (t.weekday() + 1) % 7      # Python: Monday 0; here Sunday 0
        o.raw(struct.pack(">HBBBBBBH", t.year, t.month, t.day, t.hour,
                          t.minute, t.second, weekday, t.microsecond // 1000))
    for ms, zone in cases:
        local = max(0, ms + zone * 60000)
        o.u64(local - zone * 60000)          # always >= 0 for these cases
    dates = [
        (2028, 2, 29, 23, 59, 59, 999), (2027, 2, 29, 0, 0, 0, 0),
        (2100, 2, 29, 0, 0, 0, 0), (2000, 2, 29, 12, 0, 0, 0),
        (2026, 13, 1, 0, 0, 0, 0), (2026, 4, 31, 0, 0, 0, 0),
        (2026, 9, 26, 24, 0, 0, 0), (2026, 9, 26, 23, 59, 60, 0),
        (1970, 1, 1, 0, 59, 59, 999), (1970, 1, 1, 1, 0, 0, 0),
        (2026, 1, 1, 0, 0, 0, 0), (2026, 9, 31, 0, 0, 0, 0),
    ]
    for y, mo, d, h, mi, se, ms in dates:
        try:
            t = datetime.datetime(y, mo, d, h, mi, se, ms * 1000)
        except ValueError:
            o.u64(NULLUS)
            continue
        utc = (t - epoch) // datetime.timedelta(milliseconds=1) - 60 * 60000
        o.u64(utc if utc >= 0 else NULLUS)
    for y, mo in [(2028, 2), (1900, 2)]:
        o.u64(((datetime.date(y + (mo == 12), mo % 12 + 1, 1)) - datetime.date(y, mo, 1)).days)
    o.u64(0)


# ---- timers -------------------------------------------------------------------------------

def sectio_horologiorum(o):
    timers = {}                     # slot -> [deadline, period]

    def arm(i, now, delay, period):
        if i >= 16:
            return 1
        timers[i] = [now + delay, period]
        return 0

    def fire(now):
        mask = 0
        for i in sorted(timers):
            dl, per = timers[i]
            if dl <= now:
                mask |= 1 << i
                if per == 0:
                    del timers[i]
                else:
                    while dl <= now:
                        dl += per
                    timers[i][0] = dl
        return mask

    def left(i, now):
        if i >= 16 or i not in timers:
            return NULLUS
        return max(0, timers[i][0] - now)

    def kill(i):
        if i >= 16:
            return 1
        timers.pop(i, None)
        return 0

    o.u64(arm(0, 0, 3, 0))
    o.u64(arm(1, 0, 2, 5))
    o.u64(arm(2, 0, 0, 0))
    o.u64(arm(15, 0, 10, 1))
    o.u64(arm(16, 0, 1, 0))
    for now in range(12):
        o.u64(fire(now))
        if now == 11:
            o.u64(kill(15))
    o.u64(left(1, 11))
    o.u64(fire(40))
    o.u64(left(1, 40))
    o.u64(left(0, 40))
    o.u64(left(15, 40))
    o.u64(left(99, 40))
    o.u64(kill(16))


# ---- wire helpers --------------------------------------------------------------------------

def sectio_filorum(o):
    for us in [0, 16777215, 16777216, 0x0102030405]:
        o.u64(us % (1 << 24))
    o.u64(unwrap(65535, 2, 1 << 16))
    o.u64(unwrap(70000, 65535, 1 << 16))
    o.u64(unwrap(5, 16777210, 1 << 24))
    o.u64(unwrap(16777200, 20, 1 << 24))
    o.u64(unwrap(0, 1024, 1 << 11))
    o.u64(unwrap(65536, 32768, 1 << 16))
    o.u64(unwrap(1025, 0, 1 << 11))


# ---- clock offset -------------------------------------------------------------------------

def sectio_consonantiae(o):
    run_consonantia(o, main=True)


def run_consonantia(o, main):
    window = []                     # (rtt, offset), oldest first, at most 8
    count = 0
    srtt8 = 0

    def best():
        if not window:
            return 0
        # Minimum round trip; a tie goes to the earlier RING SLOT, and the
        # ring slot of the j-th accepted exchange is j mod 8.
        slots = {}
        for j, (rtt, off) in window:
            slots[j % 8] = (rtt, off)
        k = min(slots, key=lambda s: (slots[s][0], s))
        return slots[k][1]

    def note(t0, t1, t2, t3):
        nonlocal count, srtt8
        ok = t3 >= t0 and t2 >= t1 and (t2 - t1) <= (t3 - t0)
        if ok:
            rtt = (t3 - t0) - (t2 - t1)
            off = ((t1 - t0) + (t2 - t3)) // 2          # floor, toward -inf
            window.append((count, (rtt, off)))
            del window[:-8]
            srtt8 = rtt * 8 if count == 0 else srtt8 - srtt8 // 8 + rtt
            count += 1
        o.u64(0 if ok else 1)
        o.u64(srtt8 // 8)
        o.i64(best())

    if not main:
        note(0, 1000, 1000, 100)
        note(1000, 2100, 2100, 1100)
        return
    o.i64(best())
    note(1000, 7000, 7100, 3100)
    note(2000, 8500, 8600, 5600)
    note(3000, 9000, 8000, 4000)
    note(4000, 9500, 9510, 4600)
    note(5000, 10001, 10002, 5002)
    note(100000, 90000, 90010, 100030)
    note(200000, 190001, 190001, 200000)
    note(10, 5, 5, 9)
    for k in range(8):
        t0 = 300000 + k * 1000
        t1 = t0 + 5400 + k * 3
        note(t0, t1, t1 + 50, t0 + 850 + k * 2)
    o.u64(count)
    for d in [16666667, -2000000000, -16666668]:
        t = max(0, 10**9 + d)
        o.u64(t * 60 // 10**9)
    run_consonantia(o, main=False)
    o.u64((5002 - 5000) - (10002 - 10001))
    o.i64(((10001 - 5000) + (10002 - 5002)) // 2)
    o.i64(-7 // 2)
    o.i64(7 // 2)
    o.i64(-8 // 2)


def sectio_celeritatis(o):
    for ahead in [-20, -8, -3, -1, 0, 1, 3, 8, 20]:
        o.u64(256 - 2 * max(-8, min(8, ahead)))


# ---- the lockstep / rollback buffer ---------------------------------------------------------

class Session:
    W = 64

    def __init__(self):
        self.players = 0
        self.conf = 0
        self.rollback = NULLUS
        self.held = {}              # (player, tick) -> (buttons, "real"|"pred")

    def start(self, players, first):
        if not 1 <= players <= 8:
            return 1
        self.players, self.conf, self.rollback, self.held = players, first, NULLUS, {}
        return 0

    def put(self, p, t, b, kind):
        # One slot per (player, tick mod 64): writing a tick evicts whatever
        # tick 64k apart the slot held.
        for key in [k for k in self.held if k[0] == p and k[1] % 64 == t % 64 and k[1] != t]:
            del self.held[key]
        self.held[(p, t)] = (b, kind)

    def real(self, p, t):
        v = self.held.get((p, t))
        return v is not None and v[1] == "real"

    def ready(self, t):
        return all(self.real(p, t) for p in range(self.players))

    def record(self, p, t, b):
        if p >= self.players:
            return 5
        if t < self.conf:
            return 3
        if t - self.conf >= self.W:
            return 4
        b &= 0xFFFF
        verdict = 0
        cur = self.held.get((p, t))
        if cur is not None:
            if cur[1] == "real":
                return 1 if cur[0] == b else 6
            if cur[0] != b:
                verdict = 2
                self.rollback = min(self.rollback, t)
        self.put(p, t, b, "real")
        for _ in range(64):
            if not self.ready(self.conf):
                break
            self.conf += 1
        return verdict

    def read(self, p, t):
        if p >= self.players:
            return 0x10000
        if t < self.conf:
            return self.held[(p, t)][0] if self.real(p, t) else 0x10000
        if t - self.conf >= self.W:
            return 0x10000
        cur = self.held.get((p, t))
        if cur is not None:
            return cur[0] + (0x10000 if cur[1] == "pred" else 0)
        guess = 0
        for back in range(t - 1, max(t - 64, -1), -1):
            if self.real(p, back):
                guess = self.held[(p, back)][0]
                break
        self.put(p, t, guess, "pred")
        return guess + 0x10000

    def take(self):
        r, self.rollback = self.rollback, NULLUS
        return r


def sectio_consessus(o):
    s = Session()

    def rec(p, t, b):
        o.u64(s.record(p, t, b))
        o.u64(s.conf)

    o.u64(s.start(0, 100))
    o.u64(s.start(9, 100))
    o.u64(s.start(2, 100))
    rec(0, 100, 0x0001)
    rec(1, 100, 0x0010)
    o.u64(1 if s.ready(100) else 0)
    o.u64(1 if s.ready(101) else 0)
    o.u64(s.read(0, 101))
    o.u64(s.read(1, 101))
    o.u64(s.read(0, 101))
    rec(0, 101, 0x0001)
    rec(1, 101, 0x0030)
    rec(1, 101, 0x0030)
    rec(1, 101, 0x0031)
    rec(0, 99, 0x0001)
    rec(0, 166, 0x0001)
    rec(2, 102, 0x0001)
    o.u64(s.read(1, 103))
    o.u64(s.read(0, 105))
    rec(0, 103, 0x0002)
    rec(1, 103, 0x0030)
    o.u64(s.take())
    o.u64(s.take())
    rec(0, 102, 0x0002)
    rec(1, 102, 0x0030)
    o.u64(s.read(0, 50))
    o.u64(s.read(0, 100))
    o.u64(s.read(0, 168))
    o.u64(s.read(5, 104))
    rec(0, 105, 0x1FFFF)
    o.u64(s.take())


# ---- randomness and checksums ----------------------------------------------------------------

def kiln_rng_u32(state):
    # engine/src/kiln/kiln_rng.c at Kiln 20043e7, in Python.
    x = state
    x ^= x >> 12
    x ^= (x << 25) % M64
    x ^= x >> 27
    return x, ((x * 0x2545F4914F6CDD1D) % M64) >> 32


def splitmix_at(seed, tick):
    z = (seed + (tick + 1) * 0x9E3779B97F4A7C15) % M64
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) % M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) % M64
    z ^= z >> 31
    return z or 0x9E3779B97F4A7C15


def sectio_aleae(o):
    for seed, draws in [(0, 5), (12345, 3)]:
        x = seed or 0x9E3779B97F4A7C15
        for _ in range(draws):
            x, v = kiln_rng_u32(x)
            o.u64(v)
    for t in range(4):
        o.u64(splitmix_at(0xC0FFEE, t))
    h = 0xCBF29CE484222325
    o.u64(h)
    for w in [0, 1, 0xDEADBEEF, M64 - 1]:
        for byte in struct.pack(">Q", w):
            h = ((h ^ byte) * 0x100000001B3) % M64
        o.u64(h)


def main(argv):
    args = list(argv[1:])
    if args[:1] == ["--punctim"]:
        check_against_punctim(args[1])
        args = args[2:]
    o = Out()
    sectio_punctim(o)
    sectio_arithmetica(o)
    sectio_metronomi(o)
    sectio_horae(o)
    sectio_horologiorum(o)
    sectio_filorum(o)
    sectio_consonantiae(o)
    sectio_celeritatis(o)
    sectio_consessus(o)
    sectio_aleae(o)
    if args:
        with open(args[0], "wb") as f:
            f.write(o.b)
    else:
        sys.stdout.buffer.write(o.b)


if __name__ == "__main__":
    main(sys.argv)
