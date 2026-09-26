# HydraModem bass-voice and duet reference output — vendored as a certificate

Three files, never edited: **program output vendored as test data**, not code.
They are what HydraModem produced for its `bass` profile and its polyphonic
`duet` (`hydra_profile_bass`, `hydra_profile_duet`, `hydra_modem_tx_poly`; the
design is Punctim's `hydramodem/docs/MUSIC.md`, "Bass and polyphony"). They
certify `examples/hydramodem/{bassus,bassus_emitte,bicinium_emitte}.exsc`
(`tests/programs/{bassus_loopback,bicinium_loopback,bassus_basis}/`,
`docs/design/melos.md` section 8).

| file | input | bytes | sha256 |
|---|---|---|---|
| `bassus-d310123400a1ffffdeadbeef0a1b2ca961.wav` | `frame_tx D310123400A1FFFFDEADBEEF0A1B2CA961 … --profile bass` | 689324 | `b79ef456748e125687e27cb1b0a3b92a3a476fce2af0b049bf19e4225a61001e` |
| `bicinium-d310123400a1ffffdeadbeef0a1b2ca961-0000000000000000000000000000000000.wav` | `poly_tx D310123400A1FFFFDEADBEEF0A1B2CA961 0000000000000000000000000000000000 …`: that frame on the melody voice, the zero word on the bass voice | 689324 | `adfe96861682a014efef8212ea52a538562472113792dea89f3d873ffeafe715` |
| `symbola_bassus.bin` | the zero word, then the 136 one-hot words, one byte a symbol (0–3), 178 symbols a word | 24386 | `81c90c72b3a2db25b56eaefaaec8d4ceee5b99995c4d92aeaffa8432dde238e5` |

Tree digest, from the repository root,
`find vendor/hydramodem-bicinium -type f ! -name PROVENANCE.md | sort | xargs sha256sum | sha256sum`
under `LC_ALL=C` (`flake.nix`'s `bicinium-vendor-integrity` asserts it):

```
4f729256777d326c6d00e93036304738013223efffd0f2455962d9db31071e72
```

## Source

- Upstream: `https://github.com/ALH477/Punctim`, `hydramodem/` (LGPL-3.0-only;
  the program output keeps that identifier; nothing here links into `exsc`).
- Commit: `3aeff9d` (branch `claude/music-theory-audio-modem-bpqz4i`): HydraModem
  2.0.0 with the bass voice, polyphony, and `poly_tx`'s lone-frame mode.
- Extracted clean with `git archive 3aeff9d hydramodem | tar -x -C <scratch>`.

## What was built, and how

`vendor/hydramodem-melos/`'s recipe exactly, with `poly_tx` and `poly_rx` built the
same way as `frame_tx`:

```sh
FL="-std=gnu11 -O2 -Wall -Wextra -ffp-contract=off -Ihydramodem/src"
for t in frame_tx frame_rx poly_tx poly_rx; do cc $FL hydramodem/dcf-tools/$t.c $SRC -lm -o $t; done
./frame_tx D310123400A1FFFFDEADBEEF0A1B2CA961 bassus-d310123400a1ffffdeadbeef0a1b2ca961.wav --profile bass
./poly_tx d310123400a1ffffdeadbeef0a1b2ca961 0000000000000000000000000000000000 bicinium-d310123400a1ffffdeadbeef0a1b2ca961-0000000000000000000000000000000000.wav
```

(`$SRC` as in `vendor/hydramodem-melos/PROVENANCE.md`.) Compiler: `cc (Ubuntu
13.3.0-6ubuntu2~24.04.1) 13.3.0`, x86-64, glibc libm. `frame_rx --profile bass`
recovered the bass WAV's frame, and `poly_rx` recovered both of the duet's.

`symbola_bassus.bin` is `hydra_frame_build`'s own output, from this program built
with `$FL` against `$SRC`:

```c
/* one byte a symbol, as hydra_frame_build emits them, for the bass profile:
 * word 0 = 17 zero bytes, word k = 1..136 has wire bit k-1 set (MSB first). */
#include "hydramodem.h"
#include "hydra_frame.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    hydra_profile p;
    uint8_t sym[512];
    size_t n;
    hydra_profile_bass(&p);
    if (hydra_profile_init(&p)) return 2;
    for (int k = 0; k <= 136; k++) {
        uint8_t w[17];
        memset(w, 0, 17);
        if (k) w[(k - 1) / 8] = (uint8_t)(0x80u >> ((k - 1) % 8));
        if (hydra_frame_build(&p, w, sym, sizeof sym, &n) || n != p.total_syms) return 1;
        fwrite(sym, 1, n, stdout);
    }
    return 0;
}
```

## Why these inputs

- **The bass release** plays the last symbol, and a bass symbol is two bits, so
  it is one of 4 notes. Over the basis words the last symbol takes all four
  values. The three valid frames of `vendor/hydramodem-tx/` all end on 0; the
  zero word ends on 1.
- **Coverage:** the bass-alone WAV (a valid frame) checks the release on note 0
  and the duet (zero word on the bass) checks it on note 1. **Notes 2 and 3 on
  the release are not byte-checked.** They run the same code with a different
  table entry, so they are argued, not measured (`docs/design/melos.md` section 8).
- **The duet's melody voice** carries a valid frame, whose release is note 0.
  The melody's note-4 release is checked single-voice by
  `vendor/hydramodem-melos/`.
