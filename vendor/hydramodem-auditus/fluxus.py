#!/usr/bin/env python3
"""fluxus.py -- the two STREAMS of vendor/hydramodem-auditus/, from the clean
reference renders vendored in vendor/hydramodem-melos/ and hydramodem-bicinium/.
Standard library only, deterministic (impair.py's xorshift64* Irwin-Hall noise).

  fluxus-contiguus.wav : melody(A) || duet(A, Z) || melody(Z), the renders' PCM
                         concatenated exactly (each keeps its own 20 ms + ramp
                         guards, so bursts are 60 ms apart, as frame_tx and
                         poly_tx files played back to back are), WAV-wrapped.
                         Expected frames, in order: A, A, Z, Z.
  fluxus-ictus.raw     : RAW s16le, no header: 30,000 samples of noise
                         (sigma 150), a 4,000-sample click (sigma 3000), 20,000
                         samples of noise, the bass render of A, 20,000 samples of
                         noise. The click opens a window too early and the noise
                         keeps it from closing, so the bass burst runs off its end.
                         Expected frame: A.
A = d310123400a1ffffdeadbeef0a1b2ca961, Z = seventeen zero bytes.

  python3 fluxus.py REPO_ROOT OUT_DIR"""
import os, struct, sys
from impair import rd, xorshift

A = "d310123400a1ffffdeadbeef0a1b2ca961"
Z = "0" * 34

def noise(n, sigma, g):
    out = []
    for _ in range(n):
        u = 0.0
        for _ in range(12):
            u += next(g) / 9007199254740992.0
        out.append(int(round(sigma * (u - 6.0))))
    return out

def s16(x):
    return struct.pack("<%dh" % len(x), *[max(-32768, min(32767, v)) for v in x])

def main(root, out):
    m = os.path.join(root, "vendor/hydramodem-melos")
    c = os.path.join(root, "vendor/hydramodem-bicinium")
    pcm = (rd(os.path.join(m, A + ".wav")) + rd(os.path.join(c, "bicinium-%s-%s.wav" % (A, Z)))
           + rd(os.path.join(m, Z + ".wav")))
    body = s16(pcm)
    n = len(body)
    hdr = (b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " +
           struct.pack("<IHHIIHH", 16, 1, 1, 48000, 96000, 2, 16) + b"data" + struct.pack("<I", n))
    open(os.path.join(out, "fluxus-contiguus.wav"), "wb").write(hdr + body)
    g = xorshift(5)
    x = (noise(30000, 150.0, g) + noise(4000, 3000.0, g) + noise(20000, 150.0, g)
         + rd(os.path.join(c, "bassus-%s.wav" % A)) + noise(20000, 150.0, g))
    open(os.path.join(out, "fluxus-ictus.raw"), "wb").write(s16(x))

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
