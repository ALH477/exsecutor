# HydraModem melody-profile reference output — vendored as a certificate

Three files, never edited. They are **program output vendored as test data**,
not code: what HydraModem's own transmitter produced for its musical `melody`
profile (`hydra_profile_melody`; the design is Punctim's
`hydramodem/docs/MUSIC.md`). They certify `examples/hydramodem/melos*.exsc`
(`tests/programs/melos_*/`, `docs/design/melos.md`).

| file | input | bytes | sha256 |
|---|---|---|---|
| `d310123400a1ffffdeadbeef0a1b2ca961.wav` | `D310123400A1FFFFDEADBEEF0A1B2CA961` (`dcf_loopback`'s frame, as in `vendor/hydramodem-tx/`) | 481964 | `51b092c5fa9163fd1f281a23ae71b88b78f54f8445ea356c4aa9710fc8e822bc` |
| `0000000000000000000000000000000000.wav` | seventeen zero bytes (not a valid DeModFrame; nonzero modem CRC, so its last symbol is 4, not 0 — see below) | 481964 | `73d7a13fce34408ddf6bb83bbb5dc1aef8120dd6bc24392cd50f6d78828259e4` |
| `symbola_basis.bin` | the zero word, then each of the 136 one-hot words, one byte a symbol (0–7), 124 symbols a word | 16988 | `5bf86914585c21184aabdba5a8445a9d74530504319ab636f2f3a307ecce7951` |

Tree digest, from the repository root,
`find vendor/hydramodem-melos -type f ! -name PROVENANCE.md | sort | xargs sha256sum | sha256sum`
under `LC_ALL=C` (`flake.nix`'s `melos-vendor-integrity` asserts it):

```
106744814f7cc21f7e331ca2e5473d42da001e0131d9a7c2871b3a9c7deefd34
```

## Source

- Upstream: `https://github.com/ALH477/Punctim`, `hydramodem/` (LGPL-3.0-only;
  the program output keeps that identifier; nothing here links into `exsc`).
- Commit: `5c6a4e11f50d3f0453c2f3a593fe98469afae257` (branch
  `claude/music-theory-audio-modem-bpqz4i`): HydraModem 2.0.0 with the exact
  musical synthesis (`music_render` in `hydramodem/src/hydra_modem.c`).
- Extracted clean with `git archive 5c6a4e1 hydramodem | tar -x -C <scratch>`;
  nothing was built inside either tree.

## What was built, and how

The `vendor/hydramodem-tx/` recipe, plus `-ffp-contract=off` (the library's own
Makefile forces it since this commit; it changes nothing on x86-64, which has no
baseline FMA, but it is the flag the reference is specified under):

```sh
SRC="hydramodem/src/hydra_profile.c hydramodem/src/hydra_crc.c hydramodem/src/hydra_fec.c \
     hydramodem/src/hydra_conv.c hydramodem/src/hydra_interleave.c hydramodem/src/hydra_frame.c \
     hydramodem/src/hydra_modem.c hydramodem/src/hydra_dsp_ref.c hydramodem/src/wav.c"
FL="-std=gnu11 -O2 -Wall -Wextra -ffp-contract=off -Ihydramodem/src"
cc $FL hydramodem/dcf-tools/frame_tx.c $SRC -lm -o frame_tx
cc $FL hydramodem/dcf-tools/frame_rx.c $SRC -lm -o frame_rx
./frame_tx D310123400A1FFFFDEADBEEF0A1B2CA961 d310123400a1ffffdeadbeef0a1b2ca961.wav --profile melody
./frame_tx 0000000000000000000000000000000000 0000000000000000000000000000000000.wav --profile melody
```

Compiler: `cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`, x86-64, glibc libm.
`frame_rx --profile melody` recovers each WAV's input exactly (both printed
their 34 hex characters back).

`symbola_basis.bin` is `hydra_frame_build`'s own output, not a reduction of
WAVs and not a re-derivation: this program, built with `$FL` against `$SRC`
like the tools, writes the symbols the reference emits for each basis word:

```c
/* one byte a symbol, as hydra_frame_build emits them, for the melody profile:
 * word 0 = 17 zero bytes, word k = 1..136 has wire bit k-1 set (MSB first). */
#include "hydramodem.h"
#include "hydra_frame.h"
#include <stdio.h>
#include <string.h>
int main(void){ hydra_profile p; hydra_profile_melody(&p); if(hydra_profile_init(&p)) return 2;
  uint8_t sym[512]; size_t n;
  for(int k=0;k<=136;k++){ uint8_t w[17]; memset(w,0,17); if(k) w[(k-1)/8] = (uint8_t)(0x80u >> ((k-1)%8));
    if(hydra_frame_build(&p,w,sym,sizeof sym,&n) || n!=p.total_syms) return 1; fwrite(sym,1,n,stdout);} return 0; }
```

```sh
cc $FL symbola.c $SRC -lm -o symbola && ./symbola > symbola_basis.bin
```

## The quarter-wave table

`examples/hydramodem/melos.exsc` carries the reference's 481 first-quarter sine
values as integers `M[k] = Q[k] x 2^61` (why: a float literal is limited to
fifteen significant digits, spec §8.4). `Q[k]` was printed by this program,
which computes exactly the reference's `qsin` expression, and the integers were
formed from those doubles exactly (Python `fractions.Fraction(q) * 2**61`,
asserted integral and below 2^53 significant bits). Python's `math.sin` on the
same expression agreed on all 481.

```c
#include <math.h>
#include <stdio.h>
#include <string.h>
/* qsin(1920, k) for k = 0..480, exactly as hydramodem/src/hydra_modem.c computes it
 * (k == 0 -> 0.0; else sin((2.0 * M_PI * (double)k) / (double)N)), printed %.17g,
 * which round-trips every double. */
int main(void){ for(int k=0;k<=480;k++){ double v = k ? sin((2.0*M_PI*(double)k)/(double)1920) : 0.0;
  char b[64]; snprintf(b,sizeof b,"%.17g",v); if(!strpbrk(b,".e")) strcat(b,".0"); printf("%s%s",b,k<480?",":"\n"); } return 0; }
```

That table is **not** a vendored file: it is source, written once, and the
WAV certificates are what hold it (to 16-bit resolution — see
`docs/design/melos.md`).

## Why these two WAVs and not three

Every body symbol's 1920 samples depend only on that symbol's note, because
every tone and both drones complete whole cycles in a symbol, so the carrier's
and the drones' phases are the same at every symbol boundary. This was
measured: over the three valid frames of `vendor/hydramodem-tx/` rendered at
this profile, every two of their 372 body symbols on the same note were
sample-for-sample identical, all eight notes occurred, and the silence was zero. The attack always plays note 0, the
preamble's first. The release plays the last symbol, whose three bits are
coded bit 297 and two pad bits, so it is note 0 or 4. Every valid frame's modem
CRC is `0x0000`, which makes it 0. So the three valid frames' WAVs check the
same things, and the zero word, the one input here with a nonzero CRC, is kept
instead of two of them for its release on note 4. The other two valid frames
(`d31312340001ffffdeadbeefab12cd24c0`, `d310000000000000000000000000005b80`)
were compared once, in the session that vendored these files, and were
byte-identical; they are not kept.
