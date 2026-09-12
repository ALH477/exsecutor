# HydraModem reference-receiver verdicts on impaired input — vendored as a certificate

Seventy-one files, never edited: seventy impaired WAVs and `verdicta.tsv`, the
verdict HydraModem's own `frame_rx` returned on each. They are **program
output vendored as test data**, not code and not the programs that produced
them.

This tree is the other kind of evidence from `vendor/hydramodem-tx/`. That one
holds what the reference *produces*; this one holds what it *judges*. The
seventy WAVs are the three vendored frames' audio put through three
impairments — additive white noise, a sample-clock offset, a carrier-frequency
offset — and the certificate they support is ADR 0014 decision 1's: the
Exsecutor receiver **decodes every input HydraModem's receiver decodes, and
never writes a frame that is not the input's**. It is one-directional on
purpose: the reference's silence on an input it refuses is not a verdict about
the frame, so a receiver may decode where the reference does not (one file
here is that case, and `docs/design/receptor.md` finding 7 is why).

Tree digest, `find . -type f ! -name PROVENANCE.md | sort | xargs sha256sum |
sha256sum` under `LC_ALL=C`:

```
4d8769c2a544057600d7271cfc75bac58dfe2801d97a9486eb6dbcddcc9ca05e
```

Computed **from the repository root**, not from inside this directory — the
same convention as `vendor/fasmg-x86/`, `vendor/hydramesh-wire/` and
`vendor/hydramodem-tx/`; `flake.nix`'s `rx-vendor-integrity` check asserts it.
`LC_ALL=C` is pinned for the reason the other three record: `sort`'s collation
is locale-dependent, and a digest that moves with the developer's `LANG` is not
an integrity check.

Total size, the seventy WAVs and `verdicta.tsv`: **2,668,388 bytes**.

## Source

- Upstream: `https://github.com/ALH477/HydraMesh` (HydraMesh monorepo;
  `hydramodem/` is a subtree of it)
- Local origin: `/home/asher/Documents/HydraMesh`
- Commit: `fce2813f85ac17e29f34fa1adf4008056b116318` — the same commit
  `vendor/hydramodem-tx/PROVENANCE.md` pins, verified clean
  (`git -C /home/asher/Documents/HydraMesh status --porcelain -- hydramodem/`
  reported nothing)
- Programs: `hydramodem/dcf-tools/frame_rx.c` (the judge) and, for the
  frequency offsets only, `frame_tx_offset.c` — thirty lines of glue around
  `hydra_frame_build`, `hydra_tx_dsp_process` and `hydra_wav_write`, printed
  verbatim below. Reference DSP backend (`hydramodem/src/hydra_dsp_ref.c`,
  **not** the Faust-compiled one), default profile.
- Inputs: the three WAVs of `vendor/hydramodem-tx/`, unmodified.

## What was built, and how

Extracted a clean copy of the pinned commit into a scratch directory (never
inside this tree, never written back into HydraMesh):

```sh
git -C /home/asher/Documents/HydraMesh archive fce2813 hydramodem | tar -x -C <scratch>
```

and compiled `frame_rx` and `frame_tx_offset` against the reference-DSP
sources with `vendor/hydramodem-tx/PROVENANCE.md`'s own line — the same file
set, the same flags, the same compiler:

```sh
SRC="hydramodem/src/hydra_profile.c hydramodem/src/hydra_crc.c \
     hydramodem/src/hydra_fec.c hydramodem/src/hydra_conv.c \
     hydramodem/src/hydra_interleave.c hydramodem/src/hydra_frame.c \
     hydramodem/src/hydra_modem.c hydramodem/src/hydra_dsp_ref.c \
     hydramodem/src/wav.c"
cc -std=gnu11 -O2 -Wall -Wextra -Ihydramodem/src \
   hydramodem/dcf-tools/frame_rx.c        $SRC -lm -o frame_rx
cc -std=gnu11 -O2 -Wall -Wextra -Ihydramodem/src \
   hydramodem/dcf-tools/frame_tx_offset.c $SRC -lm -o frame_tx_offset
```

Compiler: `gcc (GCC) 14.3.0` (`cc --version`, this build host).

**The build was re-verified before anything was generated**, the way
`vendor/hydramodem-tx/`'s own `profiles/` section re-verified its: `frame_tx`
built from the same archive re-rendered the three vendored WAVs and matched
their recorded sha256s (`f422db1d…a280bd` and the other two), and `frame_rx`
decoded all three back to their frames. A generator run against a build that
could not reproduce the certificate it starts from would be worthless.

## How the vectors are made

Two of the three impairments are **integer transforms of the vendored bytes**,
so that a reader with the three clean WAVs, `impedi.py` and the integers in
the tables below re-derives every byte with no `libm`, no `numpy` and no
Python `random`. The third cannot be: a real-valued mixer puts an image at
−Δf, and `frame_tx` refuses a base frequency that is not an integer multiple
of the baud (`hydra_profile.c:89-94`), so the frequency offsets are **rendered
by the reference's own DSP** with the tone frequencies displaced.

### AWGN — `impedi.py awgn IN OUT SIGMA SEED`

A xorshift64 generator, HydraModem's own tests' (`tests/test_loopback.c:26-30`):

```
s ^= s << 13;   s ^= s >> 7;   s ^= s << 17        (64-bit wrap, seed != 0)
```

warmed up by **64 discarded steps** so that a small readable seed is as good
as a large one, then twelve draws per sample, each the state's **top** sixteen
bits `(s >> 48) & 0xffff`, summed, minus `6 · 65536`. That is an Irwin–Hall
sum: near-Gaussian, mean −6, σ = 65,536 to within half a part in 2³². Measured
on 200,000 draws from seed 1: **mean −287, σ 65,738, kurtosis 2.9023** (the
exact Irwin–Hall(12) kurtosis is 2.9; a Gaussian's is 3.0). The 0.3 % excess σ
means every labelled SNR below is pessimistic by 0.03 dB.

The high sixteen bits, not the low ones, are a **stated departure** from the
sketch in `docs/design/receptor.md` D11 ("masked to 16 bits"): xorshift64's low
bits are its weak ones and a noise vector should not inherit that.

The Irwin–Hall value is scaled by the integer `SIGMA` as `(g · SIGMA) >> 16`
— an **arithmetic** shift on the signed product, flooring toward −∞ — added to
the sample, and the sum clamped to `[−32768, 32767]`, the clamp `wav.c`'s own
writer applies.

**`SIGMA` is the vector; how it was chosen is documentation.** It was chosen
as `round(RMS · 10^(−SNR/20))` where RMS is the integer root-mean-square of
the 17,088 body samples — the frame between the two 960-sample guards —
which is **20,852 for all three frames** (`impedi.py power` prints it; the
three differ only in the fifth significant figure of the sum of squares). The
`round` and the `10^(−SNR/20)` are floating point and are *not* on the path
from the clean WAV to the vector: they produced six integers, once, and the
six integers are in the table.

The **seed is the file's 1-based index in the table below**, 1 to 36, in
exactly the order `genera.sh` writes them.

### Sample-clock offset — `impedi.py clock IN OUT PPM`

Output sample `j` sits at input position `j · (10⁶ + PPM) / 10⁶`, by the
linear interpolation of `test_loopback.c:47-63` written in integers:

```
num = j · (10⁶ + PPM);   i = num // 10⁶;   f = num − i · 10⁶
out[j] = x[i] + ((x[i+1] − x[i]) · f) // 10⁶
```

with floor division throughout and `x[i+1]` taken as `x[i]` at the end of the
input; the output length is `n · 10⁶ // (10⁶ + PPM)`. A positive ppm is a
shorter, time-compressed file — the receiver's clock running fast — and a
negative one a longer file. The 44-byte header is rebuilt, not copied, because
the sample count changes; `RIFF` size and `data` size follow from it.

### Frequency offset — `frame_tx_offset FRAME OUT DELTA_HZ`

`hydra_modem_tx`'s body with `+ df` on the one line that fills the frequency
track. **At Δf = 0 it reproduces all three vendored WAVs byte for byte** —
`cmp` silent on each — which is the check that this is the reference's DSP and
not a second implementation of it.

## The set, and the reference's verdict on every file

Seventy WAVs from the three frames: `loopback` is
`d310123400a1ffffdeadbeef0a1b2ca961`, `exemplum` is
`d31312340001ffffdeadbeefab12cd24c0`, `vacuum` is
`d310000000000000000000000000005b80` — the same three
`vendor/hydramodem-tx/` holds and the same three names
`tests/programs/receptio_*` already uses. `verdicta.tsv` carries the same
verdicts in machine-readable form: `file`, `exit`, `hex` (the hex `frame_rx`
printed, or `-`), tab-separated, sorted by path under `LC_ALL=C`.

**Every impairment level is unanimous in the reference's verdict.** That is
deliberate and it is why −12 dB is here and −9 dB is not: at −9 dB the
reference decoded one of six (the `vacuum` frame at one seed) and refused the
rest, which is the reference's own cliff, where two correct receivers are
expected to differ seed by seed and a certificate would certify nothing. The
cliff is reported as a measurement under "Measured alongside" below.

Of the seventy, the reference **decoded 62** — every one to its own frame,
never another — and **refused 8**: the six at −12 dB and the two at ±300 Hz.

### Additive white Gaussian noise (36 files)

| file | frame | SNR dB | σ | seed | bytes | reference | sha256 |
|---|---|---|---|---|---|---|---|
| `awgn/loopback-p12db-s1.wav` | loopback | +12 | 5238 | 1 | 38060 | decoded | `6af11a8dac99709c96fede5eee5a9a6d1f0ea4906cde3aecbd4d88b9f86d2469` |
| `awgn/loopback-p12db-s2.wav` | loopback | +12 | 5238 | 2 | 38060 | decoded | `6483c249790f358423232dfc75bf5b31e6f821b2eae4ac5ad90187d0e356a9ed` |
| `awgn/loopback-p06db-s1.wav` | loopback | +6 | 10451 | 3 | 38060 | decoded | `782d4a6af90e570089b43a52195b387b5358f4e3956fbd3fdb3263bd1aa920d3` |
| `awgn/loopback-p06db-s2.wav` | loopback | +6 | 10451 | 4 | 38060 | decoded | `a265b2ae2a092993920dd4ad72325fb66f9bad61f3239f04fa1d78ab2bf11c54` |
| `awgn/loopback-p00db-s1.wav` | loopback | 0 | 20852 | 5 | 38060 | decoded | `1f62515b5a78271d5960dcf3eb8f5db1766287a4c7571fa04ce1c96604465273` |
| `awgn/loopback-p00db-s2.wav` | loopback | 0 | 20852 | 6 | 38060 | decoded | `e9437655390bc7cfb5d6ba82a32e46eb3420dad9ef5cba7888f546fbf7a5cdab` |
| `awgn/loopback-m03db-s1.wav` | loopback | -3 | 29454 | 7 | 38060 | decoded | `1c781ca14f314975da7893f3c2edc09fc3a7946b926e5ac8d1cd436de4b1d5c6` |
| `awgn/loopback-m03db-s2.wav` | loopback | -3 | 29454 | 8 | 38060 | decoded | `208ea121a414e878f4ec78901acfd773e9819c173ba36f5b3e6fbe7a8279fb05` |
| `awgn/loopback-m06db-s1.wav` | loopback | -6 | 41605 | 9 | 38060 | decoded | `8674b2c89c91abb040ee940df0530d4b1ca79454728ecff5b6072deea769131c` |
| `awgn/loopback-m06db-s2.wav` | loopback | -6 | 41605 | 10 | 38060 | decoded | `a175388e4ec2f2ec517589754f74a0e0fe58ce72a1fbf1cd9005ced06ffd7415` |
| `awgn/loopback-m12db-s1.wav` | loopback | -12 | 83013 | 11 | 38060 | exit 1, nothing printed | `6c72224dc57adfb3428957c486e5cf3addbe17144e90e386134c7fe1a4e27822` |
| `awgn/loopback-m12db-s2.wav` | loopback | -12 | 83013 | 12 | 38060 | exit 1, nothing printed | `b9209967876d2ab095094c9c9852d8ebe269c376d5d046bce1c55166a78c82a8` |
| `awgn/exemplum-p12db-s1.wav` | exemplum | +12 | 5238 | 13 | 38060 | decoded | `70f4e1f6be16be9952bdd5c83fb4eaa667abf4731009d59c7ce28dac6557390d` |
| `awgn/exemplum-p12db-s2.wav` | exemplum | +12 | 5238 | 14 | 38060 | decoded | `ecbf59e3b33c1ffbd241ffaec8813e5164456ad7c97e13ee69e0cd5b16eac5dc` |
| `awgn/exemplum-p06db-s1.wav` | exemplum | +6 | 10451 | 15 | 38060 | decoded | `127c989003052540dc0d16072d717ffedeaa214a942a18018a4994422340f6a1` |
| `awgn/exemplum-p06db-s2.wav` | exemplum | +6 | 10451 | 16 | 38060 | decoded | `6dca74b70ffb6a11638e66086d5aeb184b815c2b2d9144cc1bb6a70e787af042` |
| `awgn/exemplum-p00db-s1.wav` | exemplum | 0 | 20852 | 17 | 38060 | decoded | `8b40f718d329d092ecbf2c9816a00e8d91fc6f97bf567832fed0a9ea83f0fa52` |
| `awgn/exemplum-p00db-s2.wav` | exemplum | 0 | 20852 | 18 | 38060 | decoded | `76adcc9b2ae254be092a219165a67f6b7db8e4934e57443336115f96d704b7a6` |
| `awgn/exemplum-m03db-s1.wav` | exemplum | -3 | 29454 | 19 | 38060 | decoded | `d2bf11b7d691ec08b83efcc0de54411f02d9344ec531c00e5cbf09467851d47b` |
| `awgn/exemplum-m03db-s2.wav` | exemplum | -3 | 29454 | 20 | 38060 | decoded | `1a85276c3045e938d6bf060f3160b19c98a26dc67d06b2e00c333d905d2dc2b8` |
| `awgn/exemplum-m06db-s1.wav` | exemplum | -6 | 41605 | 21 | 38060 | decoded | `8f74ff5ed860ed1a311d256b33c9dd91b3ee4241a0a318bc748a66c252144114` |
| `awgn/exemplum-m06db-s2.wav` | exemplum | -6 | 41605 | 22 | 38060 | decoded | `49140169aa300c0b36304542acceaab3a82369ad93fdf87781da54d78445d57f` |
| `awgn/exemplum-m12db-s1.wav` | exemplum | -12 | 83013 | 23 | 38060 | exit 1, nothing printed | `11f85c278b98c8779fc1a346e414cc82f293e0b00b2bdeeed0126e02da58f3ba` |
| `awgn/exemplum-m12db-s2.wav` | exemplum | -12 | 83013 | 24 | 38060 | exit 1, nothing printed | `907bb4054ee0c554fe70785ea6ec8cde2f65da74e89eeb9f39f68849a753bd98` |
| `awgn/vacuum-p12db-s1.wav` | vacuum | +12 | 5238 | 25 | 38060 | decoded | `572cb2a90f2036f5f25d6ff7e65b841938457e7591cff43ea0cc70db04b84dd8` |
| `awgn/vacuum-p12db-s2.wav` | vacuum | +12 | 5238 | 26 | 38060 | decoded | `c476faa6d0a1b7120b5954462951e59c34767beebc8e4ee745954b5cb16bce02` |
| `awgn/vacuum-p06db-s1.wav` | vacuum | +6 | 10451 | 27 | 38060 | decoded | `54161afc708e705f00c11ddf15dc44e8737622cac6478ae64cd7958d7d54dbee` |
| `awgn/vacuum-p06db-s2.wav` | vacuum | +6 | 10451 | 28 | 38060 | decoded | `73df463875232fc97b1a6c3677827d360498d8a441cb679eb58765ee537b5643` |
| `awgn/vacuum-p00db-s1.wav` | vacuum | 0 | 20852 | 29 | 38060 | decoded | `6b9df8ee2c1d0f338ec663d479138231a94731f6565559de73d9c659f5ceb6e1` |
| `awgn/vacuum-p00db-s2.wav` | vacuum | 0 | 20852 | 30 | 38060 | decoded | `665b9cc66305d4d9850ca716c284da5eb1b3ae8b85c2fbb4a9e1f3d0bd3455d1` |
| `awgn/vacuum-m03db-s1.wav` | vacuum | -3 | 29454 | 31 | 38060 | decoded | `3bd53e31aeb96ffc9584a74c3fd8ce0b85e0bf459c049589dc8e788d71e3d90c` |
| `awgn/vacuum-m03db-s2.wav` | vacuum | -3 | 29454 | 32 | 38060 | decoded | `eff013bfcc9dfd7902b19700e200d90e58fa57519bbadbd696ee2b7ed4218e86` |
| `awgn/vacuum-m06db-s1.wav` | vacuum | -6 | 41605 | 33 | 38060 | decoded | `f74ddd874abf3bd15154baf7f11b9f045b6b0cda4f5d163eaecc80a2c75e60af` |
| `awgn/vacuum-m06db-s2.wav` | vacuum | -6 | 41605 | 34 | 38060 | decoded | `1a274cc1c4bde1f9cbf75e0714613e795ffbf9ddda08e8ed5b97272df11c2eed` |
| `awgn/vacuum-m12db-s1.wav` | vacuum | -12 | 83013 | 35 | 38060 | exit 1, nothing printed | `1fa9366c837a1bdce930254a927dcee3954b6108fd5b253367e08658908d0e9d` |
| `awgn/vacuum-m12db-s2.wav` | vacuum | -12 | 83013 | 36 | 38060 | exit 1, nothing printed | `b89a826d0a3f0438d665540dcf2b5affebbb3cf896f45e2536e2d56388b0b8c3` |

### Sample-clock offset (24 files)

| file | frame | ppm | bytes | reference | sha256 |
|---|---|---|---|---|---|
| `clock/loopback-m3000ppm.wav` | loopback | -3000 | 38174 | decoded | `e4b525dc93e584d177356512847a6ad88ea08a2ebaee954b9fba3f4bbaf2d614` |
| `clock/loopback-m2000ppm.wav` | loopback | -2000 | 38136 | decoded | `057481899c3700a867604c82921d5c691d8d59b17ee5414e9f8d7e503b9142ac` |
| `clock/loopback-m1000ppm.wav` | loopback | -1000 | 38098 | decoded | `546b6632897b571419e96e961c450f1de440109694f01ef56b725344c888504c` |
| `clock/loopback-m0500ppm.wav` | loopback | -500 | 38078 | decoded | `2dbeb9d376fd66b07c4cf759251d84497b5922ab5f08d1cae2adafcb3d14bc67` |
| `clock/loopback-p0500ppm.wav` | loopback | +500 | 38040 | decoded | `7908a986cdb79bc6d3227ca005ad5097b4ecf0775e4a2c254efed411c31b01ca` |
| `clock/loopback-p1000ppm.wav` | loopback | +1000 | 38022 | decoded | `64b5572b8a6b6e5547ff41b813c0e7636cab3094b246e556ebc63216f0c1bb91` |
| `clock/loopback-p2000ppm.wav` | loopback | +2000 | 37984 | decoded | `7a68421fa6ff9c8d50a1a8debfc8eb358afcc14866355e2327d8795858f44c40` |
| `clock/loopback-p3000ppm.wav` | loopback | +3000 | 37946 | decoded | `8c6fe2e2f409f082a811233a1fc70245e61024152ec157f93607722ba89284d6` |
| `clock/exemplum-m3000ppm.wav` | exemplum | -3000 | 38174 | decoded | `cbad92983224ec540a84c45c2b65ae97ba0a8bb5aac2ee579203fefc9ed19ca3` |
| `clock/exemplum-m2000ppm.wav` | exemplum | -2000 | 38136 | decoded | `317e7d64c61506baad97623e794b3d19c5f3b157e5a8565a2908bfb2e18834ad` |
| `clock/exemplum-m1000ppm.wav` | exemplum | -1000 | 38098 | decoded | `4033266ad7e33326f48a4dad9ea5a0d3fbbc12a9bf5d37051604b7b11d38de71` |
| `clock/exemplum-m0500ppm.wav` | exemplum | -500 | 38078 | decoded | `fc55a4451f90b5af12bb5180d212d203d7f87f897d79845e95483bc5dc909419` |
| `clock/exemplum-p0500ppm.wav` | exemplum | +500 | 38040 | decoded | `dcde2de17baf3d4d67b929176b975416b203a255305c719ce67aed8ff21973a0` |
| `clock/exemplum-p1000ppm.wav` | exemplum | +1000 | 38022 | decoded | `e81b864d974b69562d02cadc09cd83d8b6705ce82618c2e0271e467d6a45fed1` |
| `clock/exemplum-p2000ppm.wav` | exemplum | +2000 | 37984 | decoded | `00368bfa692095a6f1d0b90212189d3644258c59781abfb942fc04ee258899f3` |
| `clock/exemplum-p3000ppm.wav` | exemplum | +3000 | 37946 | decoded | `c8f5903bdedcabd6641035e8162e5f56357fbb4c2b0889d1754b556d9e74b5ed` |
| `clock/vacuum-m3000ppm.wav` | vacuum | -3000 | 38174 | decoded | `489489d47e611067ac580d92af8be45b53b0b762ee2c676f662f4f6cd3662add` |
| `clock/vacuum-m2000ppm.wav` | vacuum | -2000 | 38136 | decoded | `517131496b791f596e70c4447c9da1eba439e1f0bcae4146babe7904dadc9d9c` |
| `clock/vacuum-m1000ppm.wav` | vacuum | -1000 | 38098 | decoded | `a8bd8d2766f70257f541c08361021b75f97ce9d69db52005615755685f214cb0` |
| `clock/vacuum-m0500ppm.wav` | vacuum | -500 | 38078 | decoded | `2c1ba7197c5ef6bd2a797566db13c55aa17a2c7c45d14a2d90bf9d3d7542a5e0` |
| `clock/vacuum-p0500ppm.wav` | vacuum | +500 | 38040 | decoded | `36d9480601581159d4130f77e14a23b183146552719e7c22d9b6dae74322f429` |
| `clock/vacuum-p1000ppm.wav` | vacuum | +1000 | 38022 | decoded | `19644243e1c431e029815797c5c44127b056f96c5db5b425bb400e44dcf933ae` |
| `clock/vacuum-p2000ppm.wav` | vacuum | +2000 | 37984 | decoded | `c66dccc218629472a70f1d0ff621ad463381954fbf18478181b303a50e0c7315` |
| `clock/vacuum-p3000ppm.wav` | vacuum | +3000 | 37946 | decoded | `bbe9d579dd9ee44b3f9097213a61228f29df2c299d699ee28ac98a5d3bef1196` |

### Carrier-frequency offset (10 files)

| file | frame | Δf Hz | bytes | reference | sha256 |
|---|---|---|---|---|---|
| `freq/loopback-m300hz.wav` | loopback | -300 | 38060 | exit 1, nothing printed | `7f9f8b114f579fdc246596f3a271ed254624ee92144fc158b88bf3d96720725e` |
| `freq/loopback-m250hz.wav` | loopback | -250 | 38060 | decoded | `839cd84794a7f117b0cbaf23e0781c8a84518f8615499b53268202f22a4a78e3` |
| `freq/loopback-m200hz.wav` | loopback | -200 | 38060 | decoded | `b31c859c114eb43a0501680f1b33bde48c774373ccf8912bd54d604cfd96a1cd` |
| `freq/loopback-m100hz.wav` | loopback | -100 | 38060 | decoded | `07b295d2de642ef488a8648cdcfa37704c37f5f668d1a5ad1f915a757c0014d3` |
| `freq/loopback-m050hz.wav` | loopback | -50 | 38060 | decoded | `96654115703026020ac6ac67f585577e04cf0d33a5bef8083e12074c6d27bffe` |
| `freq/loopback-p050hz.wav` | loopback | +50 | 38060 | decoded | `faca2a01ac71794a4eae0c96b1491190fb5b17a6234a67d6d334398505091253` |
| `freq/loopback-p100hz.wav` | loopback | +100 | 38060 | decoded | `688c8e3cc34ea01af2cdad8a30163c5f73c6e33bc291e194a01bc1c4149b9038` |
| `freq/loopback-p200hz.wav` | loopback | +200 | 38060 | decoded | `a7b47681edc3af01477b485920569adaf36472d1ac50aba0030937159f82685b` |
| `freq/loopback-p250hz.wav` | loopback | +250 | 38060 | decoded | `18378de2529382e40c00b7456b1deba5775336ff92daf8dea343584599cb6d58` |
| `freq/loopback-p300hz.wav` | loopback | +300 | 38060 | exit 1, nothing printed | `3101dcd64f407cb4ef1e6f8e252ee54ac9ff277256f2167ace8396c5fa3bd66a` |

### The verdict file

| file | | | bytes | | sha256 |
|---|---|---|---|---|---|
| `verdicta.tsv` | -- | -- | 4194 | -- | `5ba385d165962a73c5d9ebb58934d954e25c8f064e894770e1279fa52bff93c4` |

## Measured alongside, none of it vendored

- **The AWGN cliff.** Three frames × six seeds (1001–1006) at each level, with
  the same generator: at **−7 dB** the reference decoded **16 of 18**, at
  **−8 dB** **3 of 18**, at **−9 dB** **0 of 18**. Between −6 dB (18 of 18 in
  this tree and 30 of 30 counting every seed) and −9 dB the reference's
  acquisition threshold — 37 of 40 known symbols — gives way. This reproduces
  `docs/design/receptor.md` finding 8 with a noise source that is not
  HydraModem's own, and it is the reason the certified levels stop at −6 dB on
  one side and resume at −12 dB on the other.
- **±5000 ppm.** The reference decodes all three frames at ±5000 ppm too
  (`RECEIVER.md` claims ≥ ±3000); not vendored, because ±3000 already
  exercises the loop and the extra files buy nothing.
- **Δf beyond ±250 Hz.** −300 and +300 Hz are the first failures in either
  direction, at 50 Hz granularity. `docs/design/receptor.md` finding 7 explains
  the +300 one: acquisition succeeds (`sync_score 16` of 16) and the
  reference's own timing loop, reacting to a frequency offset it was not
  designed for, walks the grid off the symbols and fails by CRC.
- **Optimisation level.** Not re-measured here.
  `vendor/hydramodem-tx/PROVENANCE.md` measured `-O0`/`-O2`/`-O3` to be
  byte-identical for the transmitter over 411 renders; `frame_rx` is a
  *verdict* and not a byte stream, and the only new floating-point program on
  this path is `frame_tx_offset`, whose Δf = 0 output is checked against the
  `-O2` vendored bytes above.

## The generator, verbatim

Nothing in the tree runs these; this copy is how anyone re-derives the files.

`impedi.py` (sha256 `9cddc3f6396c84ef4aa737468c876b6af56bb38ccbd2aaa82db823e890a8798d`):

```python
#!/usr/bin/env python3
"""impedi.py -- the impaired vectors of vendor/hydramodem-rx/, made from the
three clean WAVs of vendor/hydramodem-tx/ by integer arithmetic alone.

usage: impedi.py awgn  IN.wav OUT.wav SIGMA SEED
       impedi.py clock IN.wav OUT.wav PPM
       impedi.py power IN.wav                 (prints the body RMS; no output file)

No floating point anywhere on the path from IN.wav to OUT.wav: every byte of
every vector is an integer function of the input bytes and the integers on the
command line, so a re-derivation needs this file and nothing else -- no libm,
no numpy, no Python `random`.

THE WAV. Both forms read and write the 44-byte canonical header HydraModem's
own wav.c writes (RIFF/WAVE/fmt 16/PCM/mono/48000/96000/2/16/data), mono
16-bit little-endian PCM. The header is rebuilt, not copied, because `clock`
changes the sample count; `RIFF` size and `data` size follow from it.

AWGN. A xorshift64 generator

    s ^= s << 13;  s ^= s >> 7;  s ^= s << 17      (64-bit wrap, seed != 0)

-- HydraModem's own tests' generator (tests/test_loopback.c:26-30). The state
is stepped 64 times and those outputs DISCARDED before any are used, so that a
small readable seed (1, 2, 3, ...) is as good as a large one; without the
warm-up a seed of 1 spends its first draws climbing out of a nearly-empty
state. Each draw is the state's TOP sixteen bits, `(s >> 48) & 0xffff` -- the
high bits, because xorshift64's low bits are its weak ones and a noise vector
should not inherit that. Twelve draws are summed and 6*65536 subtracted: an
Irwin-Hall sum, near-Gaussian, mean -6 and sigma ~= 65536 (each draw is
uniform on [0, 65535], variance (2^32-1)/12, twelve of them 2^32-1, so sigma
is 65536 to within half a part in 2^32).

The Irwin-Hall value g is scaled by the integer SIGMA as `(g * SIGMA) >> 16`,
an ARITHMETIC shift on the signed product (floor, toward -inf -- Python's `>>`
on a negative int is exactly that, and it is stated here because a language
whose `>>` truncated toward zero would produce different bytes). The result is
added to the sample and the sum clamped to [-32768, 32767], the same clamp
wav.c's writer applies.

SIGMA is the vector. How it was chosen -- from the body RMS and a nominal SNR
-- is documentation and lives in PROVENANCE.md; `impedi.py power` prints the
RMS the choice was made from. Nothing downstream re-derives SIGMA.

CLOCK. Output sample j sits at input position j * (10^6 + PPM) / 10^6:

    num = j * (10^6 + PPM);  i = num // 10^6;  f = num - i * 10^6
    out[j] = x[i] + ((x[i+1] - x[i]) * f) // 10^6

with floor division throughout (again stated: `//` on a negative numerator
floors) and x[i+1] taken as x[i] at the end of the input. The output length is
n * 10^6 // (10^6 + PPM), so a positive PPM is a shorter, time-compressed file
(the receiver's clock running fast) and a negative one a longer file. This is
the linear interpolation of HydraModem's test_loopback.c:47-63 in integers.

Frequency offsets are NOT here: a real-valued mixer puts an image at -df and
`frame_tx` refuses a base frequency that is not a multiple of the baud
(hydra_profile.c:89-94), so those vectors are rendered by the reference's own
DSP -- frame_tx_offset.c, printed in PROVENANCE.md beside this file.
"""

import struct
import sys

M64 = (1 << 64) - 1
HDR = b'RIFF' + b'\0\0\0\0' + b'WAVE' + b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, 48000, 96000, 2, 16) + b'data'


def read_wav(path):
    with open(path, 'rb') as fh:
        raw = fh.read()
    if len(raw) < 44:
        sys.exit('%s: %d bytes, not a WAV' % (path, len(raw)))
    head = raw[:44]
    if head[:4] != b'RIFF' or head[8:16] != b'WAVEfmt ' or head[36:40] != b'data':
        sys.exit('%s: not the canonical 44-byte header' % path)
    if head[16:36] != HDR[16:36]:
        sys.exit('%s: fmt chunk is not 16-bit mono PCM at 48000 Hz' % path)
    n = struct.unpack('<I', head[40:44])[0] // 2
    if 44 + 2 * n != len(raw):
        sys.exit('%s: data length %d disagrees with the file size' % (path, 2 * n))
    return list(struct.unpack('<%dh' % n, raw[44:]))


def write_wav(path, samples):
    data = struct.pack('<%dh' % len(samples), *samples)
    head = bytearray(HDR)
    head[4:8] = struct.pack('<I', 36 + len(data))
    head += struct.pack('<I', len(data))
    with open(path, 'wb') as fh:
        fh.write(bytes(head))
        fh.write(data)


def xorshift64(s):
    s ^= (s << 13) & M64
    s ^= s >> 7
    s ^= (s << 17) & M64
    return s & M64


def awgn(x, sigma, seed):
    if seed == 0:
        sys.exit('seed must not be 0')
    s = seed & M64
    for _ in range(64):                       # warm-up, discarded
        s = xorshift64(s)
    out = []
    for v in x:
        acc = 0
        for _ in range(12):
            s = xorshift64(s)
            acc += (s >> 48) & 0xFFFF
        g = acc - 6 * 65536                   # Irwin-Hall, sigma ~= 65536
        y = v + ((g * sigma) >> 16)           # arithmetic (floor) shift
        if y > 32767:
            y = 32767
        if y < -32768:
            y = -32768
        out.append(y)
    return out


def clock(x, ppm):
    n = len(x)
    den = 10 ** 6
    num_per = den + ppm
    if num_per <= 0:
        sys.exit('ppm out of range')
    nout = n * den // num_per
    out = []
    for j in range(nout):
        num = j * num_per
        i = num // den
        f = num - i * den
        a = x[i]
        b = x[i + 1] if i + 1 < n else a
        out.append(a + ((b - a) * f) // den)
    return out


def main():
    mode = sys.argv[1]
    x = read_wav(sys.argv[2])
    if mode == 'power':
        # The body is the 17088 samples between the two 960-sample guards
        # (0.02 * 48000 each side, hydra_modem.c:73). Integer RMS, floor.
        body = x[960:len(x) - 960]
        ss = sum(v * v for v in body)
        rms = 0
        while (rms + 1) * (rms + 1) * len(body) <= ss:
            rms += 1
        print('n=%d body=%d sum_sq=%d rms=%d' % (len(x), len(body), ss, rms))
        return
    out = sys.argv[3]
    if mode == 'awgn':
        write_wav(out, awgn(x, int(sys.argv[4]), int(sys.argv[5])))
    elif mode == 'clock':
        write_wav(out, clock(x, int(sys.argv[4])))
    else:
        sys.exit('usage: impedi.py awgn|clock|power ...')


if __name__ == '__main__':
    main()
```

`frame_tx_offset.c` (sha256 `7b4bca047ad3ac176bfd6de22296d4a5bd9f38d146a299e2071dc6e171df449b`), built into
`<scratch>/hydramodem/dcf-tools/` beside `frame_tx.c`:

```c
/* frame_tx_offset.c -- frame_tx with the tone frequencies displaced by an
 * integer number of hertz. hydra_modem_tx's body, verbatim but for the one
 * `+ df` on the line that fills the frequency track, because `frame_tx`
 * cannot express the offset: hydra_profile_init refuses a base frequency that
 * is not an integer multiple of the baud (hydra_profile.c:89-94), and a
 * real-valued mixer applied afterwards would put an image at -df.
 *
 *   frame_tx_offset <34-hex-char-frame> out.wav <delta_hz>
 *
 * At delta_hz = 0 it must be byte-identical to frame_tx's render of the same
 * frame; that is the check that this is the reference's DSP and not a second
 * implementation of it.
 *
 * Built against the same ten reference-DSP sources as frame_tx (see
 * PROVENANCE.md). LGPL-3.0-only, (c) DeMoD LLC: it is HydraModem's own code
 * with one line changed, and it is not vendored -- only its output is.
 */
#include "../src/hydramodem.h"
#include "../src/hydra_dsp.h"
#include "../src/hydra_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_hex(const char *h, uint8_t *out, int n)
{
    if ((int)strlen(h) != 2 * n) return -1;
    for (int i = 0; i < n; ++i) {
        unsigned v;
        if (sscanf(h + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <34-hex-char-frame> out.wav <delta_hz>\n", argv[0]);
        return 2;
    }
    uint8_t payload[HYDRA_DCF_BYTES];
    if (parse_hex(argv[1], payload, HYDRA_DCF_BYTES)) {
        fprintf(stderr, "bad hex\n"); return 2;
    }
    double df = (double)atoi(argv[3]);

    hydra_profile p;
    hydra_profile_default(&p);
    if (hydra_profile_init(&p) != 0) { fprintf(stderr, "bad profile\n"); return 2; }

    size_t spp  = (size_t)p.samples_per_symbol;
    size_t lead = (size_t)(0.02 * p.sample_rate), tail = lead;
    size_t nsym = 0, body, total, i;
    long   s;

    uint8_t *symbols = (uint8_t *)malloc(p.total_syms);
    if (!symbols) return 1;
    if (hydra_frame_build(&p, payload, symbols, p.total_syms, &nsym) != 0) {
        fprintf(stderr, "frame build failed\n"); return 1;
    }
    body  = nsym * spp;
    total = lead + body + tail;

    float *freq  = (float *)malloc(body * sizeof *freq);
    float *audio = (float *)calloc(total, sizeof *audio);
    if (!freq || !audio) return 1;

    for (s = 0; s < (long)nsym; ++s) {
        double f = hydra_tone_freq(&p, symbols[s]) + df;   /* the one change */
        for (i = 0; i < spp; ++i)
            freq[(size_t)s * spp + i] = (float)f;
    }

    hydra_tx_dsp *tx = hydra_tx_dsp_create(p.sample_rate);
    if (!tx) return 1;
    hydra_tx_dsp_process(tx, freq, audio + lead, (int)body);
    for (i = 0; i < body; ++i)
        audio[lead + i] *= (float)p.tx_gain;

    int rc = hydra_wav_write(argv[2], audio, total, (int)p.sample_rate);
    free(symbols); free(freq); free(audio);
    hydra_tx_dsp_destroy(tx);
    if (rc != 0) { fprintf(stderr, "write %s failed\n", argv[2]); return 1; }
    return 0;
}
```

And the driver that ran them in the order the tables record, and that wrote
`verdicta.tsv`:

```sh
#!/usr/bin/env bash
# genera.sh -- build vendor/hydramodem-rx/ from the three clean WAVs of
# vendor/hydramodem-tx/, then record HydraModem's own frame_rx verdict on
# every one. Printed verbatim in vendor/hydramodem-rx/PROVENANCE.md.
set -e
export LC_ALL=C
R="$1"          # repository root
S="$2"          # scratch: holds impedi.py, frame_tx_offset and frame_rx
OUT="$R/vendor/hydramodem-rx"
cd "$R"

FRAMES="loopback:d310123400a1ffffdeadbeef0a1b2ca961 exemplum:d31312340001ffffdeadbeefab12cd24c0 vacuum:d310000000000000000000000000005b80"
LEVELS="p12db:5238 p06db:10451 p00db:20852 m03db:29454 m06db:41605 m12db:83013"
PPMS="m3000:-3000 m2000:-2000 m1000:-1000 m0500:-500 p0500:500 p1000:1000 p2000:2000 p3000:3000"
HZS="m300:-300 m250:-250 m200:-200 m100:-100 m050:-50 p050:50 p100:100 p200:200 p250:250 p300:300"

rm -rf "$OUT"
mkdir -p "$OUT/awgn" "$OUT/clock" "$OUT/freq"

# --- AWGN: 3 frames x 6 levels x 2 seeds. The seed is the 1-based index of
# --- the file in this loop's own order, 1..36.
seed=0
for fr in $FRAMES; do
  tag="${fr%%:*}"; hex="${fr##*:}"
  for lv in $LEVELS; do
    lvt="${lv%%:*}"; sigma="${lv##*:}"
    for slot in 1 2; do
      seed=$((seed + 1))
      python3 "$S/impedi.py" awgn "vendor/hydramodem-tx/$hex.wav" \
        "$OUT/awgn/$tag-$lvt-s$slot.wav" "$sigma" "$seed"
    done
  done
done

# --- sample-clock offsets: 3 frames x 8 ppm
for fr in $FRAMES; do
  tag="${fr%%:*}"; hex="${fr##*:}"
  for pp in $PPMS; do
    ppt="${pp%%:*}"; ppm="${pp##*:}"
    python3 "$S/impedi.py" clock "vendor/hydramodem-tx/$hex.wav" \
      "$OUT/clock/$tag-${ppt}ppm.wav" "$ppm"
  done
done

# --- frequency offsets: the loopback frame, 10 displacements, rendered by the
# --- reference's own DSP (frame_tx_offset.c)
for hz in $HZS; do
  hzt="${hz%%:*}"; df="${hz##*:}"
  "$S/hm/frame_tx_offset" D310123400A1FFFFDEADBEEF0A1B2CA961 \
    "$OUT/freq/loopback-${hzt}hz.wav" "$df"
done

# --- the reference's verdict on every file
{
  printf 'file\texit\thex\n'
  find vendor/hydramodem-rx -name '*.wav' | sort | while read -r f; do
    hexout="$("$S/hm/frame_rx" "$f" 2>/dev/null)" && rc=0 || rc=$?
    [ -n "$hexout" ] || hexout='-'
    printf '%s\t%d\t%s\n' "${f#vendor/hydramodem-rx/}" "$rc" "$hexout"
  done
} > "$OUT/verdicta.tsv"

find vendor/hydramodem-rx -type f ! -name PROVENANCE.md | sort | xargs sha256sum > "$S/digests.txt"
find vendor/hydramodem-rx -type f ! -name PROVENANCE.md | sort | xargs sha256sum | sha256sum
du -sb vendor/hydramodem-rx
```

## Licensing

`LGPL-3.0-only`, © DeMoD LLC (`hydramodem/LICENSE`, `hydramodem/NOTICE`) — the
same copyright holder as this repository, as with `vendor/hydramesh-wire/` and
`vendor/hydramodem-tx/`.

**These are program outputs vendored as test data, not the programs that
produced them, and not relicensed.** No HydraModem source is vendored here.
`frame_tx_offset.c` above is HydraModem's own `hydra_modem_tx` with one line
changed and carries HydraModem's licence; it is printed, not vendored, for the
same reason `extrahe.py` is printed in the transmitter's tree. Do not add this
repository's `LICENSE.EXCEPTION` to any of these files: nothing here is linked
into `exsc`, and `LICENSE.EXCEPTION` applies only to what `exsc` emits from a
user's input.

## Why it is vendored rather than regenerated at test time

The same reason the other three vendored trees give, and one more of its own.
The general reason: §9.3 requires output to be a function of (source, `ego`,
lockfile, flags), and "run HydraModem's reference DSP on this machine today"
is none of those. The reason peculiar to this tree: **the verdicts are about
bytes**. A generator living in the harness would be a second implementation of
`impedi.py` to keep in step with this one, and the day the two disagreed the
recorded verdict would be attached to a file that no longer existed. The
recipe is documentation; the bytes are the certificate.

The alternative considered and rejected was storing only the recipe and the
digests and regenerating at test time. It is deterministic and it is fast
(5.7 s for all seventy), but it would put a Python interpreter and this script
on the path of `tests/run.sh`, which today needs neither, and it would make
every program test depend on a generator rather than on a file git can
content-address.

## Status in this repository

All seventy are certified, as of the commit that added the timing loop:
`tests/programs/receptio_vec_*/` runs the Exsecutor receiver on each with
`stdin=`, expecting exit 0 and the frame's seventeen bytes where the reference
decoded, and a refusal with no output where it did not. The one exception is
recorded in its own `TEST`: `freq/loopback-p300hz.wav`, which the reference
refuses and this receiver decodes to the correct frame — the one-directional
case ADR 0014 decision 1 allows, recorded rather than certified.
