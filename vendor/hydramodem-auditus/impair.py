#!/usr/bin/env python3
"""impair.py -- the impaired inputs of vendor/hydramodem-auditus/, from the clean
reference WAVs of vendor/hydramodem-melos/ and vendor/hydramodem-bicinium/.
Standard library only; every step is IEEE-754 binary64 arithmetic in a fixed
order plus integer xorshift, so the output is a function of this text alone.

  awgn  NAME IN OUT SNR_DB SEED : add white noise, SNR over the non-silent samples.
        Noise = xorshift64* -> 12 uniforms in [0,1), summed, minus 6 (Irwin-Hall,
        unit variance), times sigma. Wideband per-sample SNR, as MUSIC.md measures.
  clock NAME IN OUT PPM          : resample as a receiver clock off by PPM would see
        it (linear interpolation, output step 1 + PPM e-6 input samples), then cut
        to 344,640 samples.
Samples are rounded half-to-even (Python round) and clipped to int16."""
import struct, sys

def rd(p):
    d = open(p, "rb").read()
    n = (len(d) - 44) // 2
    return list(struct.unpack("<%dh" % n, d[44:44 + 2 * n]))

def wr(p, x):
    y = [max(-32768, min(32767, int(round(v)))) for v in x]
    n = 2 * len(y)
    hdr = (b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " +
           struct.pack("<IHHIIHH", 16, 1, 1, 48000, 96000, 2, 16) + b"data" + struct.pack("<I", n))
    open(p, "wb").write(hdr + struct.pack("<%dh" % len(y), *y))

def xorshift(seed):
    s = seed & 0xFFFFFFFFFFFFFFFF or 1
    while True:
        s ^= (s >> 12); s ^= (s << 25) & 0xFFFFFFFFFFFFFFFF; s ^= (s >> 27)
        yield ((s * 0x2545F4914F6CDD1D) & 0xFFFFFFFFFFFFFFFF) >> 11   # 53 bits

def awgn(x, snr_db, seed):
    sig = [v for v in x if v != 0]
    p = sum(v * v for v in sig) / len(sig)
    sigma = (p / 10.0 ** (snr_db / 10.0)) ** 0.5
    g = xorshift(seed)
    out = []
    for v in x:
        u = 0.0
        for _ in range(12):
            u += next(g) / 9007199254740992.0
        out.append(v + sigma * (u - 6.0))
    return out

def clock(x, ppm):
    r = 1.0 + ppm * 1e-6
    m = int((len(x) - 1) / r)
    out = []
    for j in range(m):
        t = j * r
        i = int(t)
        f = t - i
        b = x[i + 1] if i + 1 < len(x) else x[i]
        out.append(x[i] + f * (b - x[i]))
    return out[:344640]

if __name__ == "__main__":
    op, src, dst = sys.argv[1], sys.argv[2], sys.argv[3]
    x = rd(src)
    if op == "awgn":
        wr(dst, awgn(x, float(sys.argv[4]), int(sys.argv[5])))
    elif op == "clock":
        wr(dst, clock(x, float(sys.argv[4])))
    else:
        sys.exit("usage: impair.py awgn|clock IN OUT ARG [SEED]")
