# The HydraModem receiver — design for the modem program, R2 and R3 (modem.md's M3)

Status: **R2 and R3 are both implemented and run.** `examples/hydramodem/
receptor.exsc`, `recipe.exsc` and `circuitus.exsc` are compiled, run and
tested by `tests/programs/receptio_*`: the three vendored WAVs decode to their
frames, 140 words round-trip through the transmitter's own `sona`, all nine
mutants of section 6 behave as predicted, and — R3 — the timing loop of D10 is
in `mollia` and **the receiver decodes all 62 of the 70 vendored impaired
vectors HydraModem's own receiver decodes, and never writes a frame that is
not the input's** (`vendor/hydramodem-rx/`, one `receptio_vec_*` directory per
vector). Section 12 records what was measured on the Exsecutor program at R2
and section 13 what R3 measured; **D3's `Praefixa` struct is the one decision
that did not survive contact with the emitter** (finding 20 — whose compiler
half has since been fixed, finding 28), and one figure of section 11 does not
reproduce (finding 21).

Before that, what had run was a scratch integer model of exactly this design
(section 11: Python, never shipped,
`prototypes/README.md`'s rule), which decodes the three vendored WAVs and
137 further `frame_tx` renders, and which, on 177 impaired inputs, decodes
every noise- and frequency-impaired WAV HydraModem's own receiver decodes
and never writes a wrong frame (the clock-offset inputs are R3's: without
a timing loop it fails one of seven). Every claim about the arithmetic is
either proved in section 3 or measured in section 11 or 12. `spec §N` cites
`docs/spec/exsecutor-spec-v0.4.md` as amended in the same commit as this
file; `modem.md §n`, `modem.md Dn` cite `docs/design/modem.md`; `WC Dn`
cites `docs/design/wire-codec.md`; ADR 0014 is
`docs/decisions/0014-hydramodem-receiver.md`. The reference is HydraMesh at
`fce2813f85ac17e29f34fa1adf4008056b116318`, subtree `hydramodem/`,
read-only, never linked, never vendored as source; `file:line` citations
are into it. The oracle is `dcf-tools/frame_rx` built by
`vendor/hydramodem-tx/PROVENANCE.md`'s recipe (rebuilt for this document:
same `frame_tx` digest, `f422db1d…`, and all three WAVs decode).

Two milestones. **R2**, the clean receiver: the three vendored WAVs and a
transmitter-to-receiver loopback of all 137 basis words decode to their
frames. **R3**, robustness: a vendored set of impaired WAVs with
HydraModem's own verdict on each, and a timing loop. The numbering
continues `modem.md`'s (M1, M2 are the transmitter's; this is M3, split
because the two halves need different language and different evidence).

## 1. What is being built, and the constraint that shapes it

The other half of the modem: a program that reads the WAV HydraModem's
transmitter writes — from standard input — and writes the 17-byte
DeModFrame it carries, or exits with a code that says why not. With M1 and
M2 the transmitter is certified byte for byte; the receiver closes the loop
so that a frame can go out through one Exsecutor program and come back
through another, and so that HydraModem's *receiver* becomes an oracle too.

The constraint is the one modem.md §1 states: the language says only what
it has settled. The transmitter needed nothing new. The receiver needs two
things, and the design's first job is to show they are the only two:

1. **A way to read.** When this was written no program could read standard
   input: `read(0)` was in `ambitus`'s admitted syscall set
   (`prelude/README.md`, "The syscall table, per atom") and no routine
   issued it; `tools/syscall-audit.sh` said so in its own comment. D1 —
   landed in `8524028`.
2. **A way to create an array.** An `acies` is born only from a
   `@transitus` struct or as a copy (modem.md finding 6, sharpened at M1:
   the first element *write* is already `EXS-E0307`). A receiver holds
   prefix sums over 19,008 samples, 64 path metrics, and a 158 × 64
   decision buffer; none of them can be written without first existing.
   D2, the array literal — the amendment modem.md D4 deferred to "a
   milestone with three tables in hand". The receiver brings the tables
   (the two local oscillators) and the buffers. Landed in `54ba744`.

Everything else — signed integers, `i64` multiply, comparison on signed
values, a variable shift count on `u64`, `dum … terminus` — is settled or
exercised. What the receiver **does not** need, and the design shows it
does not: division (HydraModem divides once, to normalise the soft bit;
D7 argues the un-normalised difference is the better metric), remainder
(walks, as in the transmitter), bitwise and/or (the Viterbi trellis is
shifts and `aut`, D8), floating point (HydraModem's receiver is `double`
throughout; every quantity here is an integer with a proved bound, D5),
signed shifts (R2 has none; R3's timing loop wants one and section 9 says
what to do about it).

Two facts about the reference make an integer receiver possible:

1. **The detector is non-coherent energy, and energy is invariant under
   the things floating point would carry.** `E_k = I² + Q²` over a
   48-sample window (`hydra_modem.c:150-155`) does not depend on the
   oscillator's phase, so the receiver's oscillator need not be the
   transmitter's and need not be exact — a 7-bit table is enough (D4,
   with the leakage bounded in D5).
2. **The certificate is decode success, not bit identity.** The
   transmitter's certificate was the WAV, byte for byte, because a byte
   is what the reference produces. The receiver's output is 17 bytes that
   are either the frame or not; nothing about the path metrics, the
   energies or the timing estimate is observable in the artifact, and the
   design is free to compute them differently — in integers, with a
   different soft metric, with different loop constants — provided the
   verdict agrees. ADR 0014 records this as the decision it is.

## 2. What the survey established, verified against source

Each row was read at the cited lines; the numbers were confirmed by the
scratch model reproducing HydraModem's verdicts (section 11).

| item | value | where |
|---|---|---|
| input | mono 16-bit PCM, any length ≥ 356 · 48 samples; the reader walks RIFF chunks, takes channel 0 of however many, **ignores the sample rate** for decoding | `wav.c:45-91`; `hydra_modem.c:204-205` (`HYDRA_ERR_NO_SIGNAL` when short) |
| sample to float | `int16 / 32768.0f` | `wav.c:86` |
| local oscillators | per tone `k`, phase starts at 0 at sample 0 of the buffer, used **before** the increment `f_k / 48000`: `I_k[n] = x[n] · cos(2π · n f_k / 48000)`, `Q_k[n] = x[n] · sin(…)` — no filtering | `hydra_dsp_ref.c:77`, `:90-105` |
| prefix sums | `PI_k[n] = Σ_{j<n} I_k[j]`, `PQ_k` likewise, so a window sum is one subtraction | `hydra_modem.c:127-148` |
| energy | `E_k(a) = (PI_k[a+48] − PI_k[a])² + (PQ_k[a+48] − PQ_k[a])²` | `hydra_modem.c:150-155` |
| known prefix | 40 symbols: 24 preamble (`k & 1`), then the 16 bits of `0x2DD4` MSB first | `hydra_modem.c:216-224` |
| acquisition | for every origin `o` in `0 .. nsamp − 356·48`: score = number of the 40 known symbols whose argmax energy (ties → tone 0) is the known tone; the best score's **first and last** origin bound a plateau; `best_o = (first + last) / 2` | `hydra_modem.c:237-246` |
| refinement | over `od ∈ [−24, 24]`, the origin maximising the summed energy of the 40 known tones at their known windows; first maximum wins | `hydra_modem.c:250-268` |
| reject | `best_score < 40 − 3`, i.e. fewer than 37 of 40 | `hydra_modem.c:278` |
| data symbols | 316 windows at `best_o + 40·48 + 48 j`, each moved by a timing loop: search ±2 samples for the peak of total energy, update only when `(max − min) > 0.15 · max`, `drift += 0.2 (off − drift)`, `pos += 48 + 0.5 · drift` | `hydra_modem.c:38-41`, `:297-326` |
| soft bit | `(max1 − max0) / (max1 + max0 + 1e−12)` — for 2-FSK, `(E_1 − E_0) / (E_1 + E_0)`; sign is the hard decision | `hydra_modem.c:173-184` |
| deinterleave | scatter: `out[(19 i) mod 316] = in[i]` — the inverse of the transmitter's gather | `hydra_interleave.c:33-39`; `hydra_frame.c:155` |
| Viterbi | K=7, 64 states; `pm[0] = 0`, others `−1e30`; for each of 158 steps and each live state `s` and input `b`: `reg = (b << 6) | s`, `o0 = parity(reg & 0x79)`, `o1 = parity(reg & 0x5B)`, `next = reg >> 1`, branch metric `(o0 ? s0 : −s0) + (o1 ? s1 : −s1)`, survivor on strict `>`; trace back from state 0; the first 152 bits are the data | `hydra_conv.c:50-117` |
| bytes and CRC | 152 bits MSB-first into 19 bytes; CRC-16/CCITT-FALSE of bytes 0–16 must equal bytes 17–18 | `hydra_frame.c:20-29`, `:188-193` |
| verdicts | `HYDRA_ERR_NO_SIGNAL` (−3, short input), `HYDRA_ERR_NO_SYNC` (−4), `HYDRA_ERR_CRC` (−5); `frame_rx` prints the hex on 0 and exits 1 on any failure | `hydra_modem.h:23-28`; `dcf-tools/frame_rx.c` |

**Energies tie at the symbol boundary, so the reference's origin is 959,
not 960.** The last sample of every symbol is `T[48 c mod 48] = T[0] = 0`
(modem.md §2), so the window starting one sample early contains the same
47 nonzero samples plus a zero from the previous symbol, and its energy is
identical. The plateau on a clean WAV is origins 938–981 (44 wide), centre
`(938 + 981) / 2 = 959`, and the refinement finds no better (its first
maximum is at offset 0). Measured in the model and reported by the
reference's own diagnostics (`frame_origin = 959`, section 11). Harmless —
the window holds every nonzero sample of its symbol — and recorded so that
"origin 959" is not read as an off-by-one.

**The receiver is sharper than its threshold suggests.** With 40 known
symbols and 1,921 candidate origins in a `frame_tx` WAV (not the "~11k"
RECEIVER.md counts for a streaming buffer with margin), a one-bit change to
the sync word still scores 39 and acquires; the tolerance of three misses
is the reference's design, and finding 5 says what it means for the
certificate.

## 3. Decisions

Two spec amendments (D1, D2), both `[UNTESTED]` when written and both since
landed and run (section 10); no new §13 code (section 4). D3–D9 are the
receiver; D10 is the timing loop deferred; D11 the certificates; D12 where
things live.

### D1 The reader: `Lector.ab_introitu(a)` and `l.lege_octeto() -> u16`

A prelude type `structura Lector { a: ambitus, descriptor: i32 }`,
capability-bearing with mark `{ambitus}` (spec §4.3) — the same record as
`Scriptor` (`prelude/interface.inc`, "Record layouts"), for the same
reason: the authority *is* the atom in the field. Obtained by
`Lector.ab_introitu(a: ambitus) -> Lector`, "from the entrance", an
associated function with no receiver, total, exactly as
`Scriptor.ad_exitum(a)` is. One method:

    l.lege_octeto() -> u16

reads **one byte** from descriptor 0 with `read(0)` and returns it, 0–255;
at end of input it returns **256**, and on a read error it returns 256 as
well. `EINTR` is retried, as `scribe_octeto` retries it. The two
non-byte cases are not distinguished: this is provisional in exactly
`scribe`'s way (`EXS_IFACE_F_APERTUM`; spec §11 wants `eventus`, which has
no syntax), and the sentinel is what a `u16` can carry that a `u8` cannot.
When `eventus<u8>` exists the call becomes `-> eventus<u8>` and the 256
goes away; nothing in this design depends on telling an error from the
end, because a WAV shorter than its own header promises is rejected either
way (D3).

**Why `u16` with a sentinel and not two calls.** A `-> u8` reader would
need a second query (`l.finis()`) to say whether the byte was one, and the
reader would carry a state between the two that a caller could forget to
ask; one call, one answer, is what a byte reader in a language without
`eventus` can honestly offer. Rejected likewise: reading into a `textus`
(a WAV is not UTF-8, spec §5.1, so no such `textus` exists); a
`lege(b: acies<u8, N>)` bulk reader (generic in `N`, which the prelude
cannot be — WC D7's reason); reading stdin as a file under `archivum`
(spec §4.6 puts the streams under `ambitus`, and the audit would over-grant).

**Cost.** 38,060 syscalls a WAV, one per byte. The transmitter's 38,060
one-byte writes take 31–39 ms (modem.md D7, §12); reads are the same
shape, and measured at R2: one WAV decode is 25 ms wall, 13 ms of it system
time (section 12). A buffered reader, when one exists, changes no byte and
no verdict.

**The names, checked against spec §3.1.** `Lector` is §3.7's own worked
derivation (`lector structura reader`): supine stem `lect-` + `-or`,
declared `structura`, exactly as §3.4 requires. `lege_octeto`: `lege` is
the third-conjugation imperative of `leg-` (§3.4, `-e`), a `functio`;
the qualifier `octeto` is the ablative `scribe_octeto` already carries
(WC D7 changed `octetum` to `octeto` for §3.1's sake). `ab_introitu`: the
qualifier `introitu` is the ablative singular of `introitus` (fourth
declension), which is what §3.1 asks of the part after `_` and what the
preposition `ab` governs. **Finding 1** records what §3.1 cannot accept in
either constructor: the part before the `_` — `ab`, and `ad` in the
existing `ad_exitum` — is a bare preposition with no root and no suffix,
and `ad_exitum`'s qualifier is an accusative (`ad` governs the accusative),
not the ablative §3.1 names. The lexicon pass is `[OPEN]` and not enabled
(spec §3.3), so nothing rejects either today; the names are kept as
mirrors of each other, and the resolution belongs with `lexicon.norma`.

**Syscalls.** `read(0)` is on the compiler's closed allowlist and on
`ambitus`'s admitted set; the compiler itself never issues it (only
emitted programs do, under the gate `EXS_POTESTAS_AMBITUS`). No allowlist
changes. `prelude/README.md`'s table row and `tools/syscall-audit.sh`'s
comment both said "`read` is `[UNIMPLEMENTED]`, no reader exists yet"; the
implementer retired both sentences in `8524028` (finding 17).

**Spec.** §4.6 (the streams under `ambitus`) now names both types and
their one-byte calls; §11's I/O bullet carries the sentinel beside
`scribe`'s `[OPEN]`; §12 lists `Lector` among the pre-seeded names. The
spec had never specified `Scriptor.ad_exitum` or `scribe_octeto` at all
(finding 2); the reader is the first prelude call written into the spec
before it exists.

**Retired by:** a `tests/unit/prelude_lege_octeto.asm` fixture of
`prelude_scribe_octeto.asm`'s shape (bytes `0x00 0x7f 0x80 0xff`, then 256
at end of input, then 256 again; `audit=pass`); `tests/programs/lector/`
reading a few bytes from a `stdin=` file; and R2's three WAV tests. **All
three exist and run** (`8524028`, `8deb727`); the fixture's audit reports
`read`, `write` and `exit_group`, not `read` and `exit_group` only, because
the blob's writers sit in the same `ambitus` gate and the audit sees the
binary, not the calls.

### D2 Array literals: `[e1, …, en]` and `[e; N]`

Spec §8.6 as amended. Two forms, both in **operand position** — where a
`[` after an operand is the index (level 1 postfix) and a `[` where an
operand is expected opens a literal: the position decides, never the
token, exactly as for prefix `&` and `*`.

- **List:** `[e1, e2, …, en]`, `n ≥ 1`, commas mandatory, no trailing
  comma (like struct literals and every other list in §8.6). Type
  `acies<T, n>`.
- **Repeat:** `[e; N]` with `N` an integer literal, `N ≥ 1`; `e` is
  evaluated **once** and every element is a copy of its value. Type
  `acies<T, N>`.

**Element type.** From the expected type when there is one
(`firma t: acies<i64, 48> = […]`, a parameter, a field initialiser);
otherwise from the first element that is not a pending literal; pending
literals then take it, as they take the other operand's type for `+`.
Every element must have that type — `EXS-E0303` at the first that does
not. A list of pending literals alone with no expected type is
`EXS-E0308`, as a lone pending literal is today. **`N` is part of the
type**: `acies<u8, 17>` initialised by a 16-element list is `EXS-E0303`,
a mismatch between `acies<u8, 16>` and `acies<u8, 17>`, not an arity
fault. Elements admitted: the integers (`uN`, `iN`, `mensura`) and
`@transitus` structs; anything else is `EXS-E0305` until a program needs
it. `[e; 0]` and `[]` do not exist: `[]` is `EXS-E0201` (the grammar
requires an element), `[e; 0]` is `EXS-E0308`.

**Evaluation** is in source order and the stores ascend, so `[f(), g()]`
calls `f` first and the emitted stores are index 0 then 1. The literal is
a **value**: binding it copies, as binding any aggregate does (spec §5.2),
and `[1, 2][0] = 3` is `EXS-E0306`. **No implicit zero-fill** of a
declared-but-uninitialised array: `mutabilis a: acies<u8, 40>;` stays
`EXS-E0307` at its first element write, exactly as measured at M1
(modem.md finding 6), and `[0; 40]` is how a program says it wants zeros.
This is the same rule struct literals took (WC D3): a zero the source did
not write is the disclosure class `@transitus` closes, and the receiver's
decision buffer is an example of why the explicit form is right — a
buffer of zeros that were never written would decode a frame from
nothing.

**Where a literal may appear.** Anywhere an expression may, `ExprNS`
included: `[` cannot begin a block, so nothing is disabled. The struct
literal's re-admission inside `[ ]` (spec §8.6, already stated) means
`[Exemplum { valor: 0 }; 4]` is a literal of four structs. Whether a
literal may initialise a **module-level `firma`** is admitted in principle
— §8.6 decision 5 makes a module `firma` a constant, and a table is one —
and `[UNTESTED]`; nothing here depends on it, because the tables are
returned from functions as `caput()` returns the header (section 7).

**Lowering, at the design level.** A list is a fresh slot and one store
per element (`slot`, then `index` + `store` in ascending order); a repeat
is `e` into a temporary, then a `u64` loop of `per`'s shape storing it —
**a loop, not an unrolled sequence**, so `[0; 20481]` is a dozen
instructions and not 20,481. A backend may recognise `[0; N]` as a fill;
that is an implementation and the bytes are the same. (As built, `54ba744`:
the repeat form unrolls at or below `LWR_ARRAY_UNROLL = 8` and loops above
it — `tests/unit/lwr_acies.asm` pins one of each.)

**Grammar** (§8.6): `Primary ::= … | ArrayLit`,
`ArrayLit ::= '[' Expr (',' Expr)* ']' | '[' Expr ';' INT ']'`. After
`[ Expr` the next token — `,`, `;` or `]` — decides the form: one token,
no new peek.

Rejected: the 48-field `@transitus` struct cast to `acies<u8, 96>`
(modem.md D4's reason: 96 hand-written little-endian bytes for a table
that wants 48 numbers, and here the entries are signed); a `novum
acies<T, N>` allocation form (spends a keyword on a zero-filled buffer,
the one thing the literal already says explicitly); a trailing comma
(every other §8.6 list refuses one, and a formatter would have to pick);
`N` as a named constant in `[e; N]` (the parser would take a `Path` it
cannot evaluate; a literal is what `acies<T, 1024>` already takes in a
type, and the same rule keeps the two in step); inferring the count from
the declared type (`[0; _]`) — a second way to say one thing.

**What this retires from the transmitter, and does not change.** M1's
`sinus` was a nine-arm `discerne` over thirteen quarter-wave values
because no literal existed (modem.md D4). It was to become a literal folded
by the same identities — **M5**, section 9 — with the M1 WAVs staying
byte-identical as its test. Not changed by this design, which does not edit
`examples/hydramodem/modulator.exsc`; done since by `76ca763`, as a
48-entry literal with the fold applied where the table is written
(modem.md D4).

**Retired by:** `tests/unit/cst_acies.asm` (both forms, every
`ExprNS` position, the trailing comma and the empty list `EXS-E0201`, a
`[` after an operand still an index); `tests/unit/chk_ty_acies.asm`
(expected type, first-element type, the pending-only list `EXS-E0308`,
the count mismatch `EXS-E0303`, a `textus` element `EXS-E0305`, `[e; 0]`
`EXS-E0308`, a `firma` array's element store `EXS-E0306`);
`tests/unit/lwr_acies.asm` (source-order evaluation, ascending stores,
the repeat as a loop); `tests/programs/acies/` (a table read back, a
`[0; N]` written through indices); and R2, whose receiver cannot exist
without them. The fixtures were planned under the names `*_arraylit.asm`
and landed as `*_acies.asm` (`54ba744`); all four run.

### D3 The WAV is read once, its header verified through `Caput`, and only four prefix arrays are kept

The receiver reads 44 bytes into an `acies<u8, 44>` through a `[0; 44]`
buffer and casts it with `sicut Caput` — the transmitter's own header
struct (modem.md D6), in the direction WC D4 calls "the way a received
buffer becomes a frame". The fields are then **verified, not trusted**:
`riff`, `wave`, `fmt`, `data` are their tags; `fmt_longitudo` 16; `codex`
1 (PCM); `canales` 1; `frequentia` 48000; `octeti_secundo` 96000;
`passus` 2; `latitudo` 16; `longitudo` even, at least 34,176 (356 · 48
samples, the reference's `need`) and at most **40,960** (20,480 samples,
D5's capacity); `magnitudo = 36 + longitudo`. Any failure is exit **3**.
Then exactly `longitudo` more bytes are read; a 256 before the last is
exit 3 too; bytes after them are not read.

This is stricter than HydraModem's reader, which walks chunks, takes
channel 0 of any count and ignores the sample rate (finding 3). Deliberate:
the certified input is what HydraModem's *transmitter* writes, which is
exactly this header (`wav.c:12-30`; M1 certified all 44 bytes), and a
decoder that accepted a 44.1 kHz file would decode it wrongly rather than
refuse it. A reader for other WAVs is a different program.

**Samples are not stored.** Each sample is signed as it arrives (D4),
multiplied by the four oscillator values for its index, and accumulated
into four running sums whose running values are stored: `PI_0`, `PQ_0`,
`PI_1`, `PQ_1`, each `acies<i64, 20481>` — the prefix sums, index `n+1`
holding the sum over samples `< n+1`, index 0 holding 0 (the reference's
layout, `hydra_modem.c:127-148`). That is the receiver's whole large
state, 4 × 20,481 × 8 = 655,392 bytes, held in one plain struct:

    structura Praefixa { i0: acies<i64, 20481>, q0: …, i1: …, q1: … }

built by a struct literal whose four initialisers are `[0; 20481]` (D2),
and written through `p.i0[n + 1] = p.i0[n] + x * c;` in the driver's read
loop. One struct, not four arrays, because a function taking the four
arrays, the count and an origin and returning an aggregate would need
seven words and the calling convention passes six, the hidden result
pointer included (WC finding 8); a struct is one word. A `structura` field
of `acies` type is laid out by the checker (spec §5.2 forbids it only in
`@transitus` types); a 640 KB local and an index store through a field
place, `p.i0[k] = v`, are `[UNTESTED]` (findings 16, 11).

**`Praefixa` COULD NOT BE BUILT AT R2, and R2 keeps four arrays instead.**
A struct literal's aggregate field is lowered into a temporary and then
`copy`d, and `__bfa_emit_copy` (`compiler/x86_64/backend_fasmg/emit.inc`)
unrolled a `copy n` into n/8 emitted instructions — so one 163,848-byte
field was about a megabyte of fasmg text and the compilation arena
`rassert`ed: SIGILL, exit 132, no diagnostic. Measured: a field of
`acies<i64, 17000>` compiled (999,057 bytes of asm), `acies<i64, 18000>`
trapped. A **plain local** `acies<i64, 20481>` is a *loop* past
`LWR_ARRAY_UNROLL` (`lower/expr.inc`) and costs 40,577 bytes of asm at any
size, which is why `tests/programs/acies/` runs 20,000 elements and this did
not. Finding 20 has the sweep, and its retirement: since `9ede8bf` a `copy`
above `BFA_COPY_UNROLL_MAX = 128` bytes is a runtime loop, and
`tests/programs/copia_magna/` — a struct literal with one `acies<i64, 18000>`
field, the exact shape that trapped — compiles and runs. R2's receiver was
written before that fix and passes four `acies<i64, 20481>` locals one per
parameter; it has not been rewritten to use `Praefixa`, and the seven-word
problem this decision was taken for never arises, because no function needs
all four *and* a count *and* an origin: `vis` takes one tone's I and Q and an
origin (three words), `acquire` the four arrays and a count (five), `mollia`
the four arrays and an origin plus the hidden result pointer (six). The
prefix layout, the accumulate loop and every bound below are unchanged.

**Capacity 20,480 samples**, not 19,008: R3's clock-offset vectors are
resampled and a −2000 ppm file has 19,046 samples; +3000 ppm has 18,951.
20,480 is 19,008 plus 7.7 % and a round number; a longer file is exit 3,
not truncated, because a truncated tail would silently move the origin
search. HydraModem's streaming receiver bounds its buffer the same way
(`hydra_rx_create`: a frame plus 50 ms plus four symbols).

Rejected: buffering the 19,008 samples and building the prefix sums
after (a fifth 160 KB array for nothing — the sums are the samples'
only use); an `alloc` arena for the arrays (a capability for something
the type already says, and the audit would then show `mmap`); prefix sums
at a reduced width (D5 shows `i64` is needed and sufficient).

**Retired by:** R2's three WAV tests; `tests/programs/hydramodem_rx_caput/`
feeding each rejected header field in turn (exit 3, no output). As landed:
the three WAV tests run, and the header directory is `receptio_caput/`, one
field (the sample rate), whose own `TEST` records that it proves the header
path rejects and writes nothing, not the rate field in particular; the
field-by-field sweep is not written (section 12).

### D4 Samples signed through the view; the Q7 oscillator table, thirteen values folded

A sample arrives as two bytes. They go through the transmitter's
`Exemplum { valor: u16:minor }` view — `[b0, b1] sicut Exemplum` — read as
`u16`, widened `sicut i64`, and if `v ge 32768` then `v = v − 65536`. That
is the two's-complement reading of an `int16` with **no signed shift and
no narrowing cast**, the mirror of modem.md D6's `0 -% v`.

The oscillators are one table. The reference's `cos(2π n f_k / 48000)` at
`f_0 = 2000`, `f_1 = 3000` is periodic in 24 and 16 samples, so both are
`cos(2π m / 48)` at `m = n c_k mod 48`, `c_0 = 2`, `c_1 = 3` — the same
index walk as the transmitter's (modem.md D3), from sample 0 of the
buffer, phase used before the increment (`hydra_dsp_ref.c:99-101`). And
`sin θ = cos(θ − π/2)` is the same table at `m + 36 mod 48`. So there is
one table, **`T7[m] = round(127 · cos(2π m / 48))`**, |entry| ≤ 127:

    127 126 123 117 110 101 90 77 64 49 33 17 0     (m = 0..12)

with `T7[24 − m] = −T7[m]` and `T7[48 − m] = T7[m]` — thirteen values and
two exact identities, as D4 of modem.md derived the transmitter's, and for
the same reason: a value computed by floating point at each index is not
one; the model's first table had `−63` at index 16 against `64` at index
8, because `cos(2π/3)` came out `−0.4999…` (finding 10). `127 · cos 60° =
63.5` exactly; it is rounded **half away from zero**, 64, and stated so a
re-derivation gets the same table. Written as a 48-element `acies<i64,
48>` literal (D2, section 7), with the negative entries spelled `-17` …
`-127` — a negation is a `Unary` over the literal and the checker admits
the magnitude by the signed bound (`checker/types/types.inc`, the
`-128: i8` note); R2's `tabula()` is exactly that literal and every
decode reads it.

**Why seven bits are enough — and, on an aligned window, exact.** The
crude bound first: the table's rounding error is at most 0.5 in 127, so a
tone's leakage into the other tone's window is bounded by 48 · 32768 · 0.5
≈ 7.9 × 10⁵ in `I`, against a matched `I` of about 29,490 · 127 · 24 ≈ 9.0
× 10⁷: below 1 % in amplitude, below −40 dB in energy, and that bound is
what holds for a window that straddles two symbols. On a window aligned to
a symbol the leakage is **exactly zero**, table or no table: both the
transmitter's `T` and this `T7` keep the quarter-wave identities exactly
(they are defined by them), and a 48-periodic sequence with `f(24 − m) =
−f(m)` and `f(48 − m) = f(m)` has only odd harmonics; tone 0 walks the
table by 2 and tone 1 by 3, so their harmonic sets are `{2(2a + 1)}` and
`{3(2b + 1)}`, which are disjoint (`4a + 2 = 6b + 3` has no solution),
and the sum of products over a full period of two sequences with disjoint
harmonics is zero. Measured on the three clean WAVs at origins 959 and
960: the wrong tone's energy is 0 at every one of the 356 windows, and the
largest `|I|` or `|Q|` is 86,994,364 (section 11, re-measured and
reproduced at R2 — section 12). So the Q7 table costs
nothing on a clean channel; what it costs on a misaligned one is the
−40 dB above, beneath any noise the R3 vectors carry. Rejected: Q15 (the
products would need the scaling shifts D5 avoids: 32768 · 32767 · 48
squared overflows `i64`); a per-tone pair of tables (four tables where
one plus two offsets says the same thing).

**Retired by:** R2's three WAV tests (every window of every frame goes
through the table).

### D5 Integer down-conversion and prefix sums, with the bounds proved

Let `x[n]` be a sample, `|x| ≤ 32768`; `T7` the table, `|T7| ≤ 127`;
`L = 48`; the capacity `N = 20480`.

- **A product:** `|x · T7[m]| ≤ 32768 · 127 = 4,161,536 < 2²².`
- **A window sum** (the `I` or `Q` of one symbol): `|I| ≤ 48 · 4,161,536
  = 199,753,728 < 2²⁸.` On the vendored WAVs the largest is 8.99 × 10⁷
  (the amplitude is 0.9 · 32767 and `Σ cos²` over a period is 24).
- **A prefix sum:** `|PI_k[n]| ≤ N · 4,161,536 = 85,228,257,280 < 2³⁷.`
  Measured maximum **2.61 × 10¹⁰**, on the all-zero frame, whose symbol
  stream has the longest runs of one tone and whose `I_0` therefore ramps
  (the product of a tone with its own quadrature carrier has a DC term that
  a prefix sum integrates). Section 11's 7.0 × 10⁹ does not reproduce —
  finding 21. `i64` holds the bound with 26 bits to spare; `i32` does not,
  which is why the arrays are `i64`.
- **An energy:** `I² ≤ (2²⁸)² = 2⁵⁶`, so `E = I² + Q² < 2⁵⁷ ≈ 1.44 × 10¹⁷`.
  The exact bound is `2 · 199,753,728² = 7.98 × 10¹⁶`. Measured maximum
  8.11 × 10¹⁵ over the 356 frame windows, 8.39 × 10¹⁵ over every window the
  acquisition scan touches — the amplitude and the `cos²` mean again, plus
  leakage on the misaligned windows. The bound must cover the scan's, and
  does. No scaling shift is needed before squaring, which is what a Q7 table
  buys.
- **The acquisition sum** of 40 known-tone energies: `< 40 · 2⁵⁷ < 2⁶³`
  (`40 · 7.98 × 10¹⁶ = 3.19 × 10¹⁸ < 9.22 × 10¹⁸`). Measured 3.24 × 10¹⁷.
- **A total energy** across both tones (R3's discriminator): `< 2⁵⁸`; the
  gate `(max − min) · 20 > max · 3` multiplies by at most 20 < 2⁵, so
  `< 2⁶³`.
- **A soft bit** `E_1 − E_0`: `|soft| < 2⁵⁷`.
- **A path metric** (D7's scaling): every `|soft'| ≤ 2²⁰`, each of 158
  steps adds two, so `|pm| ≤ 316 · 2²⁰ < 2²⁹ ≈ 3.3 × 10⁸`. Measured
  2.98 × 10⁸ at shift 33.

Every `+`, `-` and `*` on these values is the trapping kind (spec §5.4),
and the bounds are what make the traps unreachable: a receiver that
overflowed would abort, not decode wrongly, and the bounds say it cannot
abort on any input the header check admits. `i64` multiplication from
source was `[UNTESTED]` when this was written (only `u64` products ran,
modem.md §9) — finding 14, retired by R2: `x * t[m0]` runs 4 × 19,008
times a decode and `i * i + q * q` some 150,000 times (section 10).

**Retired by:** R2 (every bound is exercised at the vendored amplitude —
done, section 12: no trap in 1,430 decodes); `tests/programs/
hydramodem_rx_plenus/`, a synthetic full-scale input (every sample ±32768
on the tones) that must not abort — the one input that reaches the bounds
rather than a tenth of them — **not written**.

### D6 Acquisition as the reference does it, in integers

For every origin `o` in `0 .. n − 17088`, the score is the number of the
40 known symbols `k` for which `E_1(o + 48k) > E_0(o + 48k)` equals the
known tone (the reference's `argmax_d`, ties to tone 0). The best score's
first and last origins bound the plateau; the centre is `(first + last)
deorsum 1` — `mensura`, unsigned, so the shift is the reference's `/ 2`
exactly. Then the ±24 refinement: the base in `centre − 24 .. centre + 24`
(skipping bases below 0 or whose last window exceeds `n`) maximising the
sum of the 40 known-tone energies, first maximum kept. Reject with exit
**1** if the best score is below 37.

This is `hydra_modem.c:237-278` transcribed with two changes of no
consequence: `mensura` indices where the reference has `long`, and
energies in `i64` where it has `double` — D5 says both are exact. The
scan is 1,921 origins × 40 symbols × 2 tones = 153,680 energies, each two
subtractions and two products; the refinement 49 × 40 more.

Rejected: an energy-onset detector before the scan (RECEIVER.md records it
as a failure mode the full-prefix match was adopted to remove); scanning
only the lead-in (the R3 vectors are clock-resampled and the origin
moves); a cheaper early-exit at the first score of 40 (the plateau's
centre needs its last origin, and the reference takes the centre for the
timing loop's sake — see finding 6 for what the certificate can and
cannot see of that).

**Retired by:** R2's three WAV tests and the loopback (D11); the mutants
of section 6 that fail by acquisition.

### D7 Soft bits as `E_1 − E_0`, un-normalised, scaled once per frame by a shift

The reference's soft bit is `(E_1 − E_0) / (E_1 + E_0 + ε)`
(`hydra_modem.c:173-184`), a value in `[−1, 1]`. This design uses the
**signed difference** `E_1 − E_0` and no division — and argues it is the
better branch metric, not merely an admissible one.

**Why the un-normalised difference is a sound Viterbi metric.** A Viterbi
decoder with an additive metric finds the path maximising the sum of
per-bit correlations `Σ ±m_j`. Three properties are what matter:

1. **The sign is the hard decision in both forms**, so on a channel where
   every symbol is decided correctly the two decoders agree exactly.
2. **On a stationary channel the two metrics are one scale factor apart.**
   For 2-FSK one tone carries the symbol's energy and the other carries
   noise, so `E_1 + E_0 ≈ A² L² / 4 + noise` is the same for every
   symbol, and dividing by it multiplies every metric by one positive
   constant — and the Viterbi argmax is invariant under a positive scale.
   The same holds for the per-frame shift of this design.
3. **Where they differ, the difference weighs a faded symbol less.** The
   normalised form gives a symbol received at a tenth of the amplitude
   the same confidence as one at full amplitude; the difference gives it
   a hundredth. The max-log approximation of the log-likelihood ratio for
   non-coherent FSK in Gaussian noise is proportional to `(√E_1 − √E_0)
   · A / σ²` — it *grows* with the received amplitude, because a strong
   symbol is more informative than a weak one. The difference `E_1 − E_0
   = (√E_1 − √E_0)(√E_1 + √E_0)` carries that weighting; the normalised
   form discards it. So under bursts and fades, the kind of impairment
   the interleaver exists for, the un-normalised metric is the one closer
   to the likelihood, and a symbol wiped by a burst contributes nearly
   nothing rather than a confident wrong sign.

Measured (section 11): on the 162 noise-impaired inputs the model's
verdict with the difference and with a scratch normalised variant agreed
on every one, and with the reference's. The argument, not the sweep, is
the evidence for
the general case; the sweep is evidence that nothing in it is wrong at
the reference's own cliff.

**Scaling.** `|E_1 − E_0| < 2⁵⁷` and 316 of them summed overflow `i64`
(`316 · 2⁵⁷ > 2⁶³`), so the frame's 316 differences are scaled **once**:
find the largest magnitude `M`; find the smallest `k` with `M deorsum k ≤
2²⁰` (a `dum M gt 1048576 terminus 64 { M = M deorsum 1; k = k + 1; }` on
a `u64`); then every soft bit is its magnitude `deorsum k` with its sign
put back. The magnitude is `(E_1 − E_0) sicut u64` or `(E_0 − E_1) sicut
u64` by the sign of the comparison — an equal-width `i64 → u64` cast of a
non-negative value, the bit pattern unchanged: a reinterpretation by spec
§5.4 as amended in the follow-up (finding 13). When this was written it
had run from source only in the `u8 → i8` direction
(`tests/programs/angusta/`: `200 sicut i8` is `−56`) and at the IR in this
one (`conv_roundtrip.ir`); since R2 `receptor.exsc`'s `mollia` writes
`d sicut u64` on an `i64` and every `receptio_*` decode runs it 316 times,
so the `i64 → u64` direction runs from source too. `k` is a `u64` because
the shift count
must have the operand's type (spec §5.4), and **a variable count on `u64`
is settled**: `chk_ty_bitops.asm` types it, `shift_narrow.ir` runs one.
One `k` for the frame rather than one per bit, so the relative weights of
the 316 bits — the thing property 3 is about — are preserved to within
one part in 2²⁰. The path-metric bound of D5 follows.

Rejected: dividing by a constant `2²⁰` fixed in advance (a quiet input
would lose every bit); per-bit normalisation by cross-multiplication
(possible without `/`, and it is the reference's metric, which property 3
argues against); `f64` (`[OPEN]` in the backend and not needed).

**Retired by:** R2 and R3's AWGN vectors (the metric decides only under
noise); `tests/programs/hydramodem_rx_plenus/` for the shift at the
bound.

### D8 Soft Viterbi: 64 states, `i64` metrics, one decision bit per state and step, output bit from the state

The trellis is the reference's register `reg = (b << 6) | s`, `next = reg
>> 1` (`hydra_conv.c:38-48`): with `s = s₅…s₀`, the outputs are

    o0 = b aut s5 aut s4 aut s3 aut s0        // G0 = 0x79 = 1111001: bits 6 5 4 3 0
    o1 = b aut s4 aut s3 aut s1 aut s0        // G1 = 0x5B = 1011011: bits 6 4 3 1 0

and `next = (b sursum 5) + (s deorsum 1)`. The bits of `s` are read as
the transmitter reads bits — `(s deorsum j)` then the low bit as `y − ((y
deorsum 1) sursum 1)` — so the trellis is shifts, `aut` and `+`, no
parity instruction, no mask. Two `acies<i64, 64>` hold the metrics,
`[−4611686018427387904; 64]` (−2⁶²; the reference's `−1e30`) with state 0
set to 0, and the add-compare-select is the reference's on strict `gt`,
states ascending, `b = 0` before `b = 1`, so ties resolve the same way.

**The decision buffer stores one bit per (step, state): the predecessor's
dropped bit.** Since `next = (b << 5) | (s >> 1)`, the next state
determines `b` (its top bit) and the top five bits of `s`; only `s₀` is
lost, and it is what the survivor decision must keep: `decisio: acies<u8,
10112>`, `[0; 10112]` (158 × 64), written at `t · 64 + next`. The
reference stores `prev` and `bit` as two bytes; one bit is enough and the
buffer is 10,112 bytes instead of 20,224. Traceback from state 0: for `t
= 157 down to 0`, the data bit is `s deorsum 5` and the previous state is
`((s − ((s deorsum 5) sursum 5)) sursum 1) + decisio[t · 64 + s]` — the
`& 31` as a subtraction of the shifted-out part. The first 152 bits, MSB
first, are packed into 19 bytes by `b = (b sursum 1) + bit` (the `sursum`
discards the top bit and the sum cannot carry).

Termination in state 0 is the reference's (`hydra_conv.c:100`) and is
sound because the transmitter's six tail bits are zero (modem.md D5). The
cost is 158 × 64 × 2 branch evaluations, 20,224, each a dozen shifts —
nothing, against the acquisition scan.

Rejected: hard-decision Viterbi with Hamming metrics (needs nothing this
design lacks, and loses the coding gain RECEIVER.md puts at 6 dB; the R3
vectors would show the loss); a register-form encoder table of 128 entries
built with a loop (the taps are five `aut`s and a table would be a second
place for them to be wrong).

**Retired by:** R2's loopback of all 137 basis words (every trellis edge
is taken somewhere in 137 × 158 steps: the basis words are one-hot and the
zero word's tail is not zero, modem.md finding 10); the taps mutant.

### D9 Deinterleave as the scatter walk; the CRC as a residue over nineteen bytes

The transmitter gathers, `out[i] = in[(19 i) mod 316]`; the receiver
scatters the soft bits back, `lin[(19 i) mod 316] = soft[i]`
(`hydra_interleave.c:38`), and the index is the transmitter's walk — `j =
j + 19; si j ge 316 { j = j − 316; }` — carried across the 316 bits in one
loop, so there is no multiplication and no remainder, and `lin` is a
`[0; 316]` of `i64` every element of which the walk writes (a permutation
writes each index once; a mutant stride that is not coprime to 316 would
leave zeros, and the CRC would see it).

The 19 decoded bytes are `f ‖ crc(f)`, and CRC-16/CCITT-FALSE over `data ‖
crc(data)` is 0 (modem.md §2, "the frame's own CRC makes the modem's CRC
zero", the same identity one level down). So the check is
`residuum(b: acies<u8, 19>) eq 0` — `redundantia` over nineteen bytes,
declared for that length since there is no generic `N` — and not a
comparison of two bytes with a recomputed value. Exit **2** on failure,
with no output: the receiver **never writes a frame it has not checked**.
Bytes 0–16 are written on success, exit 0.

**Retired by:** R2 (every vector's CRC passes) and the stride mutant; R3's
−9 dB vectors, on which the reference and the model reject and the
receiver must not write.

### D10 Timing tracking is R3; the loop's constants become powers of two — **implemented**

R2 samples every data symbol at `best_o + 40·48 + 48 j`, with no loop.
On a clean channel and on every AWGN vector the model decodes without one
(section 11: 108 of 108 at −6 dB and above), and on clock offsets it
decodes ±1000 ppm and −2000 without one and fails +2000 — the R3 vectors
are where the loop earns its place, and where it is designed.

The design for R3, stated now so its arithmetic is bounded with the rest:
per data symbol, the total energy `E_0 + E_1` at the five offsets `−2 ..
+2`; the peak offset; the gate `(max − min) · 20 gt max · 3` (the
reference's `0.15 · max`, by cross-multiplication in `i64`, D5's bound);
an EMA `drift += (off − drift) / 4` in place of the reference's `0.2`,
and `pos += 48 + drift / 2` in place of `0.5` — **weights of 1/4 and 1/2**,
a documented departure, admissible because the certificate is decode
success and the reference's own comment says the four constants were
"tuned jointly" for the sweeps, not derived. The drift is a signed
fixed-point value, and dividing a signed value by a power of two is a
signed shift — `[OPEN]` in spec §5.4 — or a sign-and-magnitude pair of
`u64`s with `deorsum`. Which of the two R3 takes is decided at R3 with the
vectors in hand; modem.md §9's prediction that the receiver "possibly"
needs signed shifts narrows to: R2 does not, R3's loop does or works
around them.

**What R3 built, and the four things the design did not say.** The loop is in
`mollia`, `positio` and `vagatio` being Q8 fixed point in `i64` — 256ths of a
sample — and the five offsets, the argmax, the cross-multiplied gate and the
two power-of-two weights exactly as above. Four things had to be decided that
this text left open, and each is a difference from the reference worth naming:

1. **The signed shift is not taken, and stays `[OPEN]` with this as evidence
   against needing it.** Both divisions are the sign-and-magnitude idiom
   `mollia`'s own per-frame scaling already uses: the magnitude cast to `u64`,
   `deorsum`, the sign put back. That truncates toward zero where a signed
   shift would floor toward −∞, and toward zero is the symmetric choice for an
   EMA. All eight clock-offset displacements on all three frames decode, so
   section 9's condition for leaving the shift `[OPEN]` is met — the mirror of
   entry 23's and/or, as it said.
2. **`a0` is `lround(pos)`, not `(long)pos`** — `(positio + 128) deorsum 8` on
   the `u64` reinterpretation, `positio` never being negative. The reference
   rounds (`hydra_modem.c:301`) and truncating would bias the grid half a
   sample early at every symbol.
3. **`o` and `n` travel in one `mensura`**, `on = (o sursum 32) + n`. The loop
   needs the sample count — a window must not run past the samples that were
   read — and `mollia` already spends four argument words on the arrays and one
   on the hidden result pointer. The emitter's limit is six and says so:
   `bfa: emitter: param index > 5 (Tier 1's 6-argument limit)`, measured. Both
   indices are under 2²¹ by D3's capacity, so the packing is exact. Finding 12's
   hidden pointer, one milestone later — and finding 28 records that D3's
   `Praefixa`, which would make the packing unnecessary, now compiles.
4. **A window past `n` is clamped, where the reference fails.**
   `hydra_modem.c:322` returns `HYDRA_ERR_NO_SYNC`; a function returning 316
   soft bits has no way to say that, so the window is pulled back to the last
   one wholly inside the input. Measured not to bind anywhere: an instrumented
   build reports the largest window the loop asks for on each of the 70 vectors
   and the three clean WAVs, and the tightest is **938 samples clear of `n`**.
   The same reason finding 23 gives one loop up — past `n` the prefix arrays
   hold zeros no sample wrote.

**Retired by:** R3's clock-offset vectors (D11) — 24 of 24, where the same
receiver without the loop decodes 17 of 24 (section 13).

### D11 Certificates: R2 in-process and from the vendored WAVs; R3 against HydraModem's verdicts on vendored impaired WAVs

**R2.** Three program tests, `tests/programs/hydramodem_rx_{loopback,
exemplum,vacuum}/`, each `stdin=vendor/hydramodem-tx/<frame>.wav`,
`expect-exit=0`, and an `expected.out` of the frame's 17 bytes (the
harness's existing binary-stdout comparison; the `stdin=` key did not
exist when this was written and is finding 15, since retired). And one
loopback test, `tests/programs/hydramodem_circuitus/`: a driver compiled with
`quantum.exsc`, `modulator.exsc` and `receptor.exsc` that, for the zero
word, the 136 one-hot words and the three frames, synthesises the 19,008
samples in memory with the transmitter's own `sona(k, i)` (M1's, unchanged
— the one-copy rule of modem.md D8), accumulates them into a `Praefixa`
exactly as the stdin driver does, decodes, and writes one byte per word: 0
if the 17 bytes came back equal, else 1. Expected: 140 zero bytes. **Every
word round-trips, valid DeModFrame or not**, because the modem CRC covers
all 17 bytes — DCF validity is a layer above and not the modem's to judge.
No process pipe, no second binary, no harness change beyond `stdin=`:
`tx | rx` is what this test *is*, with the pipe replaced by the language's
own by-value struct. Estimated cost: 140 decodes at a few million integer
operations each, against the basis driver's 100 ms for 10 million
(modem.md §13) — some seconds, within the 20-second limit, `[UNTESTED]`
when written; if not, the directory splits in two. As landed (`8deb727`)
the directories are `tests/programs/receptio_{loopback,exemplum,vacuum}/`
and `receptio_circuitus/`, and the loopback takes 1.17 s (section 12).

**R3.** A new vendored tree, **`vendor/hydramodem-rx/`**, on
`vendor/hydramodem-tx/`'s provenance discipline (ADR 0013 item 1: program
output, digests under `LC_ALL=C`, the recipe, the commit, no source): a
set of impaired WAVs and, for each, **HydraModem's own `frame_rx` verdict**
— the hex it printed, or its exit status — from the binary
`PROVENANCE.md` already records how to build. The Exsecutor receiver must
decode every WAV the reference decodes, and must **never write a frame that
is not the input frame** (the reference's silence on a rejected input is
not a verdict about the frame, and the receiver may decode where the
reference does not — one direction; finding 7 has a case). It lives beside
the transmitter's tree and not inside it because the two are different
kinds of evidence: `hydramodem-tx/` is what the reference *produces*,
`hydramodem-rx/` is what it *judges*.

**How the vectors are made — integer arithmetic, so a reader can re-derive
every byte** from the three vendored WAVs and the numbers in the table:

- *AWGN.* A xorshift64 generator (`s ^= s << 13; s ^= s >> 7; s ^= s <<
  17`, 64-bit wrap, seed ≠ 0 — HydraModem's own tests' generator,
  `tests/test_loopback.c:26-30`); per sample twelve draws, each masked to
  16 bits, summed, minus `6 · 65536` — an Irwin–Hall sum, near-Gaussian
  with σ = 65536; scaled by an integer `σ_v` as `(n · σ_v) >> 16` with a
  floor shift on the signed value; added; clamped to `[−32768, 32767]`.
  `σ_v` is chosen from the signal power over the non-silent samples for a
  nominal SNR and **the integer is what is recorded** (e.g. 5,535 for
  12 dB on the loopback frame, 43,965 for −6 dB); how it was chosen is
  documentation, the integer is the vector.
- *Sample-clock offset.* Output sample `j` sits at input position
  `j · (10⁶ + ppm) / 10⁶`: `num = j · (10⁶ + ppm)`, `i0 = num div 10⁶`,
  `f = num mod 10⁶`, `out = a + ((b − a) · f) div 10⁶` with floor
  division, `b` the next input sample or `a` at the end; the length is
  `n · 10⁶ div (10⁶ + ppm)`. The linear interpolation of
  `test_loopback.c:47-63` in integers.
- *Frequency offset.* Not an integer transform — a real-valued mixer
  puts an image at `−Δf` too — and `frame_tx` refuses a base frequency
  that is not a multiple of the baud (`hydra_profile.c:89-94`). So these
  are **rendered by the reference's own DSP**: `hydra_modem_tx`'s body
  with `f + Δf` in place of `f`, a thirty-line C program printed verbatim
  in `PROVENANCE.md` with its digest, as `extrahe.py` is. At `Δf = 0` it
  reproduces the vendored WAV byte for byte (measured).

**The set and its budget — as planned, then as built.** The plan was 35 files,
1.33 MB: AWGN at 6, 0 and −6 dB, two seeds each, on all three frames; AWGN at
−9 dB, one seed; clock offsets ±500, ±1000, ±2000, ±3000 ppm on the loopback
frame; frequency offsets −200, −100, +100, +200, +250, +300 Hz on it.

**What was vendored is 70 files, 2,668,388 bytes**, and it differs from the
plan in three ways, each for a reason measurement supplied:

- **Every impairment is on all three frames** where the plan put the clock and
  frequency rows on the loopback frame alone. The all-zero-body frame is the
  one whose symbol stream has the longest same-tone runs, which is exactly
  where the timing loop's transition gate coasts, so it is the frame a clock
  offset is hardest on; leaving it out would have left the gate untested where
  it matters. (The frequency offsets stay on the loopback frame: they cost a
  rendering run of the reference's own DSP per file, and the reference's
  failure mode at ±300 Hz is its timing loop, which is frame-independent —
  measured at 6 displacements in section 11 and at 10 here.)
- **Six AWGN levels, +12 to −6 dB, and the refusal level is −12 dB, not −9.**
  −9 dB is **not unanimous**: the reference decoded one of six there (the
  all-zero frame at one seed) and refused five. Every level must be unanimous
  or the certificate sits on the reference's own cliff, where two correct
  receivers are expected to differ seed by seed and agreement would certify
  nothing — so the refusal level moved to −12 dB, where the reference refuses
  6 of 6 and, measured over 18 more files, 0 of 18. The cliff is reported in
  `vendor/hydramodem-rx/PROVENANCE.md` and section 13 as a measurement: −7 dB
  16/18, −8 dB 3/18, −9 dB 0/18.
- **Both signs of the frequency offset**, ±50 to ±300 Hz. The reference fails
  at −300 as well as +300, at 50 Hz granularity, which the plan did not know.

The layout is `awgn/`, `clock/`, `freq/` under `vendor/hydramodem-rx/`, with
`PROVENANCE.md` and `verdicta.tsv` (file, exit status, hex) beside them, and
one `tests/programs/receptio_vec_<kind>_<name>/` per file: `stdin=`,
`expect-exit=0` with the frame as `expected.out` where the reference decoded,
and where it refused, this receiver's own status with **no** `expected.out` —
the harness refuses output where none is expected, which is the "never a wrong
frame" half. 2.67 MB against the 3 MB this milestone was given; the
alternative, storing the recipe and the digests and regenerating at test time,
is deterministic and takes 5.7 s but would put a Python interpreter and the
generator on `tests/run.sh`'s path, which needs neither today, and would make
every program test depend on a generator rather than on a file git can
content-address.

Rejected: generating impaired inputs at test time from the clean WAVs (the
verdicts are about *bytes*, and a generator in the harness is a second
implementation of the recipe to keep in step); Gaussian noise by
Box–Muller in `double` (re-derivable only with the same `libm`);
frequency offsets by a `double` mixer (the image, and the `libm`); the
reference's *diagnostics* (origin, score, ppm) as part of the certificate
(internal state, which ADR 0014 declines to certify).

**Retired by:** the tests named above — **70 directories, all green**, and the
tallies in section 13.

### D12 Where the code lives

`examples/hydramodem/receptor.exsc` — pure: no `poscit`, no capability
parameter, no `initium` — beside `modulator.exsc`; `recipe.exsc`, the
stdin driver, the only file of its unit naming `Mundus` and `Lector`;
`circuitus.exsc`, the loopback driver. Tests in `tests/programs/
receptio_*/` (planned here as `hydramodem_rx_*/` and `hydramodem_circuitus/`)
with `sources=` pointing at `examples/`, exactly modem.md D8's arrangement
and for its reason. The binary of the stdin driver audits to `read`, `write`
and `exit_group`; so does the loopback driver's, although it never reads —
the prelude gates by atom, not by call (`docs/design/runtime.md` 2.6), so
every `ambitus` binary carries `exsrt_lector_lege_octeto` since `8524028`.

## 4. Error codes, checked against §13

Nothing here adds a code. The array literal's diagnostics, each with its
registry class and the precedent that the use is in the class:

| code | §13 text | used here for | fit |
|---|---|---|---|
| `EXS-E0201` | unexpected token | `[]`; a trailing comma; `[e; n]` with `n` not an `INT` | the parser's general code, as for a struct literal's trailing comma (WC §4) |
| `EXS-E0303` | type mismatch | an element not of the literal's element type; a literal of `n` elements where `acies<T, m>`, `m ≠ n`, is expected | class C; the count is part of the type (`acies<f32, 8>` "carries its lane count in the type", spec §5.4) |
| `EXS-E0305` | operation not defined on the type | an element of a type not admitted (`textus`, a plain struct, a function) | class E; the aggregate cast's admissibility already lives here (WC D4) |
| `EXS-E0306` | assignment to an immutable or non-lvalue target | `[1, 2][0] = 3` | the cast's non-lvalue result is the precedent (spec §5.2) |
| `EXS-E0307` | control flow misuse (definite assignment) | unchanged: an array declared without a literal and written by element | **not** relaxed by D2, on purpose |
| `EXS-E0308` | literal cannot be typed or does not fit its width | a list of pending literals alone; `[e; 0]` | class H; the lone pending literal (`per i in 3`, spec §8.5) is the precedent. `[e; 0]` is the one stretch: a repeat count of zero is a literal with nothing to type |

The receiver's **exit codes** — 1 no sync, 2 CRC, 3 bad WAV — are values
a program returns, not §13 codes, exactly as `lege`'s reason codes are not
(WC §4). And the abort that would follow an overflow in D5's arithmetic
is a runtime trap, not a diagnostic; D5's bounds are what keep it
unreachable.

## 5. The program's shape

The plan as written before R2. What runs differs in two places the
retirement table records: `Praefixa` is four arrays (finding 20) and
`mixtio` was never written (finding 22); `examples/hydramodem/receptor.exsc`
is the program.

| file | contents | capability |
|---|---|---|
| `examples/hydramodem/quantum.exsc` | as at M1: `DeModFrame`, `redundantia` | none |
| `examples/hydramodem/modulator.exsc` | as at M1: `Caput`, `Exemplum`, `sona`, … — the receiver reuses `Caput` and `Exemplum` and the loopback reuses `sona` | none |
| `examples/hydramodem/receptor.exsc` | `Praefixa`, `tabula`, `signatum`, `mixtio`, `vis`, `acquire`, `mollia`, `decodifica`, `residuum` — pure | none |
| `examples/hydramodem/recipe.exsc` | `initium`: the reader, the header check, the accumulate loop, the verdict and the 17 bytes | `Mundus` → `ambitus` (`Lector`, `Scriptor`) |
| `examples/hydramodem/circuitus.exsc` | `initium`: 140 words through `sona` into a `Praefixa` and back | `Mundus` → `ambitus` (`Scriptor`) |

The functions and their contracts (names provisional against the lexicon
pass, as modem.md §5's are):

| function | contract |
|---|---|
| `tabula() -> acies<i64, 48>` | `T7`, the 48-entry Q7 cosine table (D4) |
| `signatum(b0: u8, b1: u8) -> i64` | the sample the two little-endian bytes carry, as a signed value (D4) |
| `mixtio(t: acies<i64, 48>, k: mensura, n: mensura, q: u1) -> i64` | the oscillator value for tone `k` at absolute sample `n`: `T7[n c_k mod 48]` (`q = 0`) or `T7[(n c_k + 36) mod 48]` (`q = 1`), walked not multiplied |
| `vis(p: Praefixa, k: mensura, a: mensura) -> i64` | `E_k(a)` (D5) |
| `acquire(p: Praefixa, n: mensura) -> mensura` | the refined origin, or `n` when the best score is below 37 (`n` is never an origin: the last origin is `n − 17088`) |
| `mollia(p: Praefixa, o: mensura) -> acies<i64, 316>` | the 316 scaled, deinterleaved soft bits (D7, D9) |
| `decodifica(s: acies<i64, 316>) -> acies<u8, 19>` | the Viterbi decision, 152 bits as 19 bytes (D8) |
| `residuum(b: acies<u8, 19>) -> u16` | CRC-16/CCITT-FALSE over all 19; 0 iff the frame checks (D9) |

`acquire` returns `n` for "none" rather than a struct with a flag because
an aggregate return costs a hidden pointer (WC finding 8) and a `mensura`
that cannot be an origin is a complete answer. Aggregate returns of an
`acies` (`mollia`, `decodifica`, `tabula`) are `[UNTESTED]`: `obsigna`
returns a struct by value (WC §11) and an array should take the same
convention (`docs/design/lowering.md`, section 2.7). WC finding 9 applies to the
drivers as it did to `emitte.exsc`: helpers write no `poscit`.

## 6. The certificates and the mutants

**R2 must pass:** the three WAV tests, byte-exact 17 bytes and exit 0;
`receptio_circuitus`, 140 zero bytes. **Each mutant below** is applied
mechanically to a temporary copy of `examples/hydramodem/` (CONTRIBUTING's
rule) and must produce the verdict predicted — including the ones that
must **pass**, which are the certificate's stated blind spots. Predicted
in the scratch model of section 11, which implements this design; the
harness must see the same. **It did: nine of nine, observed at R2**, with
the sed applied to a copy of `examples/hydramodem/` and both drivers rebuilt
each time (section 12).

| mutant | what it breaks | predicted, three WAVs | predicted, loopback | seen in the model | **observed, R2** |
|---|---|---|---|---|---|
| `G0` and `G1` taps swapped in `decodifica` | the trellis | exit 2 on all three (a wrong 19 bytes, CRC fails) | 140 × 1 | exit 2 ×3, wrong bytes on every frame | exit 2 ×3, no output; loopback 140 × 1 |
| stride 19 → 17 in `mollia`'s walk | the deinterleaver | exit 2 ×3 | 140 × 1 | exit 2 ×3 | exit 2 ×3, no output; loopback 140 × 1 |
| sync word `0x2dd4` → `0xd22b` (complement) | the known prefix | exit 1 ×3: the best score is 31–34 at some origin, below 37 | 140 × 1 | exit 1 ×3, score 31 | exit 1 ×3, no output; loopback 140 × 1 |
| tone-1 oscillator with `c = 2` (both LOs on tone 0) | the down-conversion | exit 1 ×3: score 20 (only the tone-0 symbols match) | 140 × 1 | exit 1 ×3, score 20 | exit 1 ×3, no output; loopback 140 × 1 |
| threshold 37 → 41 (`nknown + 1`) | acquisition rejects everything | exit 1 ×3 | 140 × 1 | exit 1 ×3 | exit 1 ×3, no output; loopback 140 × 1 |
| **sync word `0x2dd4` → `0xadd4` (one bit)** | one known symbol | **passes**: score 39 ≥ 37 — the reference's three-miss tolerance | passes | passes ×3, score 39 | **passes**: 17/17 bytes ×3, loopback 140 × 0 |
| **threshold 37 → 36** | one more miss tolerated | **passes**: every clean score is 40 | passes | passes | **passes**: 17/17 bytes ×3, loopback 140 × 0 |
| **plateau centre → first origin** | the sampling phase, 21 samples early | **passes**: 27 of 48 samples of each symbol suffice on a clean channel | passes | passes, and still at −6 dB AWGN | **passes**: 17/17 bytes ×3, loopback 140 × 0 |
| **`T7[0]` 127 → 126** | one table entry | **passes** | passes | passes | **passes**: 17/17 bytes ×3, loopback 140 × 0 |

The receiver keeps its **own** sync word rather than reading
`modulator.exsc`'s `synchronia`, which is what makes rows 3 and 6 mean
anything on the loopback: a constant shared by both halves would change both
and the round trip would close regardless. It is the one place the two
halves deliberately hold the same number twice.

The four bold rows are negative controls: a harness on which one *fails*
has a bug. They say precisely what decode success cannot see — a table
entry, a threshold at the margin, the phase refinement, a sync bit — and
ADR 0014 accepts that in exchange for a certificate that is about
interoperability rather than internal state. R3's vectors close two of
them by prediction: the plateau-edge mutant should fail on clock-offset
vectors, where the timing loop starts 21 samples off (RECEIVER.md: the
loop "tracks drift, not a static offset"), and a threshold of 36 is
visible only on a vector whose reference score is exactly 36 — none is in
the set, and none is sought.

**R3 must pass:** the vectors of D11, each against its recorded verdict. **It
does: 70 of 70 directories, 62 decodes of the reference's 62, no wrong frame**
(section 13). And, run for the record and not certified: the AWGN cliff, at
−7, −8 and −9 dB with six seeds each on all three frames, reported for the
reference.

**R3's own mutants**, three, on the timing loop, each applied mechanically to
a temporary copy of `examples/hydramodem/` and both drivers rebuilt.
Predictions written before the runs; the last two columns are what happened.

| mutant | predicted | observed, the 70 vectors | observed, clean + loopback |
|---|---|---|---|
| EMA weight 1/2 for 1/4 (`deorsum 1` for `deorsum 2`) | probably invisible: the gate fires only at transitions and a faster EMA still tracks the mean | **58 of 62** — loses `clock/loopback-p3000ppm`, `clock/vacuum-m3000ppm`, `clock/vacuum-p1000ppm`, `freq/loopback-m200hz`, all by CRC | 3/3 WAVs, loopback 140/140 |
| track direction inverted (`pos += 48 − drift/2`) | fails the clock vectors — positive feedback, the grid runs away; clean unaffected because drift stays 0 | **36 of 62** — loses 15 of 24 clock vectors *and* 9 AWGN ones, all by CRC | 3/3 WAVs, loopback 140/140 |
| the transition gate removed (always update) | fails the clean WAVs and nearly everything: a flat profile rails the argmax to the search edge | **62 of 62 — passes every vector** | **fails**: exit 2 on all three WAVs, loopback 140 × 1 |

The first prediction was wrong and the third was half wrong, and both are
findings: 24 and 25.

**And the prediction R3 was meant to settle, settled the other way.** Finding 6
predicted that the plateau-edge mutant — the plateau's first origin for its
centre, 21 samples early — "should fail on clock-offset vectors". It does not:
applied to the R3 receiver it decodes **62 of 62**, and applied to the R2
receiver (no timing loop) it decodes **55 of 62**, losing exactly the seven the
unmutated R2 receiver loses and no others. The mutant is invisible to this
certificate with the loop and without it. Finding 26.

## 7. Worked example, as designed — superseded by `examples/hydramodem/`

Written against the language as it ran at the commit of this file plus
D1 and D2, and kept as the design's record; the program that runs is
`examples/hydramodem/receptor.exsc` with `recipe.exsc` and
`circuitus.exsc`, which depart from this text where findings 20, 22 and
23 say. When this was written, `per i in 0..48`, `0..316`, `0..64` typed
as `mensura` (spec §8.5); `i64` arithmetic and comparison from source had
run at `i8` and `i32` (`angusta`, `discerne`) and not at `i64`; `*` on
`i64`, a 48-element `i64` literal with negative entries, a 640 KB struct
of arrays, an index store through a field, an `acies` returned by value,
`Lector`, and the two array-literal forms had not run at all. All of those
run now except the 640 KB struct, which R2 does not use (finding 20).

```exsecutor
// examples/hydramodem/receptor.exsc -- pure; no `poscit`, no `initium`.

// The four prefix sums (D3): index n + 1 holds the sum over samples < n + 1.
publica structura Praefixa {
    i0: acies<i64, 20481>
    q0: acies<i64, 20481>
    i1: acies<i64, 20481>
    q1: acies<i64, 20481>
}

// T7[m] = round(127 cos(2 pi m / 48)), half away from zero (D4). Thirteen
// values and the two quarter-wave identities, written out so the table
// is the specification and not a computation.
publica functio tabula() -> acies<i64, 48> {
    redde [127, 126, 123, 117, 110, 101, 90, 77, 64, 49, 33, 17, 0,
           -17, -33, -49, -64, -77, -90, -101, -110, -117, -123, -126,
           -127, -126, -123, -117, -110, -101, -90, -77, -64, -49, -33, -17,
           0, 17, 33, 49, 64, 77, 90, 101, 110, 117, 123, 126];
}

// The signed sample the two little-endian bytes carry (D4): the view
// reads a u16, and 32768.. is the negative half. No signed shift.
publica functio signatum(b0: u8, b1: u8) -> i64 {
    firma e = [b0, b1] sicut Exemplum;
    mutabilis v: i64 = e.valor sicut i64;
    si v ge 32768 { v = v - 65536; }
    redde v;
}

// E_k(a) over the 48-sample window at a (D5): two subtractions, two
// products, one sum; every value bounded in D5, so no trap is reachable.
publica functio vis(p: Praefixa, k: mensura, a: mensura) -> i64 {
    mutabilis i: i64 = 0;
    mutabilis q: i64 = 0;
    si k eq 0 {
        i = p.i0[a + 48] - p.i0[a];
        q = p.q0[a + 48] - p.q0[a];
    } aliter {
        i = p.i1[a + 48] - p.i1[a];
        q = p.q1[a + 48] - p.q1[a];
    }
    redde i * i + q * q;
}
```

The down-conversion inner loop lives in the driver, because it writes the
struct in place (finding 11). `m0`, `m1` are the two oscillator indices,
walked as the transmitter walks its phase (modem.md D3):

```exsecutor
// examples/hydramodem/recipe.exsc -- the stdin driver; the only file of
// its unit that names Mundus, Lector and Scriptor.

publica functio initium(m: Mundus) -> u8 {
    firma a = m.ambitus();
    sub ambitus = a;
    firma l = Lector.ab_introitu(a);
    firma s = Scriptor.ad_exitum(a);

    // The header (D3): 44 bytes into a buffer, the buffer as a Caput.
    mutabilis h: acies<u8, 44> = [0; 44];     // the type gives the pending 0 its width (D2)
    per i in 0..44 {
        firma b = l.lege_octeto();
        si b ge 256 { redde 3; }
        h[i] = b sicut u8;              // b < 256 here: the widening's inverse, see finding 13
    }
    firma c = h sicut Caput;
    si c.riff ne 0x52494646 { redde 3; }
    si c.wave ne 0x57415645 { redde 3; }
    si c.fmt ne 0x666d7420 { redde 3; }
    si c.fmt_longitudo ne 16 { redde 3; }
    si c.codex ne 1 { redde 3; }
    si c.canales ne 1 { redde 3; }
    si c.frequentia ne 48000 { redde 3; }
    si c.octeti_secundo ne 96000 { redde 3; }
    si c.passus ne 2 { redde 3; }
    si c.latitudo ne 16 { redde 3; }
    si c.data ne 0x64617461 { redde 3; }
    si c.longitudo lt 34176 { redde 3; }
    si c.longitudo gt 40960 { redde 3; }
    si c.longitudo ne ((c.longitudo deorsum 1) sursum 1) { redde 3; }
    si c.magnitudo ne c.longitudo + 36 { redde 3; }
    firma n: mensura = (c.longitudo deorsum 1) sicut mensura;

    // The samples, straight into the prefix sums (D3, D5). Nothing else
    // is kept. m0 walks 2 per sample, m1 walks 3; the sine is the cosine
    // 36 further on.
    firma t = tabula();
    mutabilis p = Praefixa { i0: [0; 20481], q0: [0; 20481], i1: [0; 20481], q1: [0; 20481] };
    mutabilis m0: mensura = 0;
    mutabilis m1: mensura = 0;
    per i in 0..n {
        firma b0 = l.lege_octeto();
        firma b1 = l.lege_octeto();
        si b0 ge 256 { redde 3; }
        si b1 ge 256 { redde 3; }
        firma x = signatum(b0 sicut u8, b1 sicut u8);
        mutabilis s0: mensura = m0 + 36; si s0 ge 48 { s0 = s0 - 48; }
        mutabilis s1: mensura = m1 + 36; si s1 ge 48 { s1 = s1 - 48; }
        p.i0[i + 1] = p.i0[i] + x * t[m0];
        p.q0[i + 1] = p.q0[i] + x * t[s0];
        p.i1[i + 1] = p.i1[i] + x * t[m1];
        p.q1[i + 1] = p.q1[i] + x * t[s1];
        m0 = m0 + 2; si m0 ge 48 { m0 = m0 - 48; }
        m1 = m1 + 3; si m1 ge 48 { m1 = m1 - 48; }
    }

    firma o = acquire(p, n);
    si o eq n { redde 1; }
    firma d = decodifica(mollia(p, o));
    si residuum(d) ne 0 { redde 2; }
    per i in 0..17 { s.scribe_octeto(d[i]); }
    redde 0;
}
```

One add-compare-select step of the Viterbi (D8), for step `t` with soft
bits `s0`, `s1`; `pm` and `npm` are the two `acies<i64, 64>`, `dec` the
`[0; 10112]`:

```exsecutor
    per st in 0..64 {
        firma basis = pm[st];
        si basis gt -4611686018427387904 {         // a live state
            // the bits of st, read as the transmitter reads bits
            firma b5 = (st deorsum 5) - (((st deorsum 5) deorsum 1) sursum 1);
            firma b4 = (st deorsum 4) - (((st deorsum 4) deorsum 1) sursum 1);
            firma b3 = (st deorsum 3) - (((st deorsum 3) deorsum 1) sursum 1);
            firma b1 = (st deorsum 1) - (((st deorsum 1) deorsum 1) sursum 1);
            firma b0 = st - ((st deorsum 1) sursum 1);
            per b in 0..2 {
                firma o0 = b aut b5 aut b4 aut b3 aut b0;     // G0 = 0x79
                firma o1 = b aut b4 aut b3 aut b1 aut b0;     // G1 = 0x5B
                mutabilis bm: i64 = 0;
                si o0 eq 1 { bm = bm + s0; } aliter { bm = bm - s0; }
                si o1 eq 1 { bm = bm + s1; } aliter { bm = bm - s1; }
                firma cand = basis + bm;
                firma next = (b sursum 5) + (st deorsum 1);
                si cand gt npm[next] {
                    npm[next] = cand;
                    dec[t * 64 + next] = b0 sicut u8;         // the dropped bit
                }
            }
        }
    }
```

and the traceback, from state 0, reading the data bit off the state:

```exsecutor
    mutabilis st: mensura = 0;
    mutabilis bits: acies<u8, 158> = [0; 158];
    per u in 0..158 {
        firma t = 157 - u;
        bits[t] = (st deorsum 5) sicut u8;
        st = ((st - ((st deorsum 5) sursum 5)) sursum 1) + (dec[t * 64 + st] sicut mensura);
    }
    mutabilis out: acies<u8, 19> = [0; 19];
    per i in 0..152 {
        out[i deorsum 3] = (out[i deorsum 3] sursum 1) + bits[i];
    }
```

Things this leans on and where each stands: `[b0, b1] sicut Exemplum`
(D2 with WC D4's cast, bytes to struct — the direction `forma` reads);
`h sicut Caput` likewise at 44 bytes; `p.i0[i + 1] = …` (an index store
through a field place, finding 11); `i * i` on `i64` (finding 14); `b
sicut u8` on a `u16` known to be below 256 (a narrowing cast — truncation
by spec §5.4 as amended in the follow-up, lowered as `trunc` and run at
`u16 → u8` by `angusta`, finding 13); `x * t[m0]` with `x: i64`, `t: acies<i64, 48>`; `bits[t]` with `t:
mensura` computed by subtraction; `dum … terminus 64` for the scaling
shift (in `mollia`, not shown); every `per` bound a pending-literal range.

## 8. Findings

Numbered; each names the document and the line.

1. **Spec §3.1 and the two constructors.** `Scriptor.ad_exitum` and
   `Lector.ab_introitu` both put a bare preposition before the `_` —
   neither `ad` nor `ab` is a prefix in §3.5's table, and neither
   decomposes as prefix + root + suffix — and `ad_exitum`'s qualifier is
   an accusative (`exitum`; `ad` governs the accusative) where §3.1 asks
   for an ablative. `ab_introitu`'s qualifier is an ablative, as `ab`
   governs, so the new name conforms where the old one does not; the
   part before the `_` conforms in neither. The lexicon pass is `[OPEN]`
   and disabled (§3.3), so nothing rejects either; the names are kept as
   mirrors and the question — a rule for associated constructors, or
   different names — is `lexicon.norma`'s. Reported prominently, as the
   task asked; not silently renamed.
2. **The spec never specified the prelude's surface.** `Scriptor.ad_exitum`
   and `scribe_octeto` appear nowhere in
   `docs/spec/exsecutor-spec-v0.4.md`; §4.2 uses `Scriptor` as a type in
   an example, §11 names `Scriptor.scribe`'s return, §12 says the prelude's
   names are pre-seeded. The surface lived in `docs/design/runtime.md`
   section 2.4 and `prelude/interface.inc`. The amendment for D1 puts the
   *streams'* two types, their constructors and their one-byte calls in
   §4.6, which is where the spec says the streams live — `Scriptor`'s
   with a citation to the fixtures that run it, `Lector`'s `[UNTESTED]`.
3. **`wav.c:45-91` decodes any sample rate as if it were the profile's.**
   `hydra_wav_read` returns the rate and `frame_rx` never compares it with
   `p.sample_rate`; it also takes channel 0 of any channel count. A 44.1
   kHz recording of a 48 kHz frame would be decoded at the wrong tone
   frequencies and fail by CRC rather than by a format check. Not a
   defect for the reference's own use (its transmitter always writes 48
   kHz mono) but a reason this receiver verifies the header (D3) rather
   than copying the reader.
4. **The reference's origin on a clean frame is 959, not 960** (§2): the
   last sample of every symbol is `T[0] = 0`, the energies at 959 and 960
   tie, the plateau is 938–981 and its centre is 959. Measured in the
   model and in the reference's diagnostics. Harmless; recorded so the
   number is not "fixed".
5. **A one-bit sync mutant is invisible to any clean certificate**: it
   scores 39 of 40 and the reference tolerates three misses
   (`hydra_modem.c:278`). The mutant that must fail is the complement
   (score 31–34); the one-bit form is a negative control in section 6.
   The transmitter's sync mutant (modem.md §6) *is* visible, at symbol 24,
   because a byte is compared and not a verdict — the two certificates
   see different things, which is ADR 0014's point.
6. **The plateau centre and the ±24 refinement cannot be seen by R2, nor
   by AWGN at −6 dB** (measured: the first-origin mutant, 21 samples
   early, decodes all 18 −6 dB vectors). RECEIVER.md gives the reason the
   reference has them — the timing loop "tracks drift, not a static
   offset" — so their test is R3's clock-offset vectors, predicted, not
   shown.
7. **At +300 Hz the reference fails by its timing loop, and the R2 model
   decodes.** `hydra_modem_rx_ex` returns `HYDRA_ERR_CRC` (−5) with
   `sync_score 16` of 16, `frame_origin 958`, `est_sps 47.86` (−2902 ppm):
   acquisition succeeded and the total-energy discriminator, seeing the
   offset's changed window shape, walked the grid off the symbols. The
   model, sampling on the fixed grid, decodes the frame. At +250 Hz both
   decode and the reference reports `est_sps 48.06` (+1320 ppm) — the
   loop reacts to a frequency offset it was not designed for, and at 300
   Hz reacts too much. The survey's "+200 decodes, +300 fails"
   (modem.md §2, `[UNREPRODUCED]` there) is reproduced here for the
   reference, at 6 offsets, and explained. For the certificate it is the
   one-directional case D11 allows: a receiver may decode what the
   reference does not. R3's loop is predicted to inherit the failure.
8. **The AWGN cliff is between −6 and −9 dB, and it is acquisition's**:
   at −6 dB 18 of 18 decode (reference and model); at −9 dB 0 of 18
   (reference), and the model returns "no sync" on 15 and "CRC" on 3 —
   never a frame. RECEIVER.md's "100 % to −6 dB" is reproduced with an
   integer noise source that is not theirs. The cliff being acquisition's
   (37 of 40 known symbols) rather than the code's is why the threshold
   and the metric are hard to tell apart by decode success.
9. **The un-normalised metric and the normalised one gave the same verdict
   on all 162 noise-impaired inputs**, including the 18 at the cliff's
   edge (−6 dB) and the 54 beyond it. Consistent with D7's argument; not
   a proof of it, and not
   evidence *for* the difference over the ratio — that is the argument's
   job.
10. **A table computed by floating point is not one table.** The model's
    first `T7` had `−63` at index 16 and `64` at index 8: `127 ·
    cos(2π/3)` is `−63.4999…` in binary64 and rounds away from `−63.5`.
    D4 defines the table by thirteen values and two identities, as
    modem.md D4 did, so the receiver's table cannot depend on a `libm`.
    The design's own text is what caught it: "quarter-wave symmetry" is
    a property one can check on 48 entries, and it did not hold.
11. *(**Corrected, 2026-09-12, ADR 0016.** The rule cited here to spec §6.3
    decision 3 is not in §6.3, which is one sentence about **retains** and says
    nothing about writing. The prohibition is `docs/design/ssa-ir.md` section 2.9's,
    and section 2.9 now admits `&mutabilis T`. Measured while finding this: mutation
    through a borrowed aggregate parameter already worked, which made `firma`
    violable by handing a binding to a callee. The finding stands as written;
    the attribution was wrong.)*

    **Mutation through a borrowed aggregate parameter has no form.** A
    function that fills the prefix sums must own them: a parameter is
    borrowed (spec §6.3 decision 3, IR 2.9), `obsigna` copies before it
    writes, and no fixture assigns through a `&T`. Returning a 640 KB
    struct per sample is not an option, so the accumulate loop is written
    in each driver (twice: `recipe.exsc`, `circuitus.exsc`) and the pure
    module reads the struct it is handed. Reported as a language gap the
    receiver meets and the transmitter did not; not designed around
    here.

    *(**Still open, 2026-09-12, and now for a measured reason rather than a
    missing feature.** ADR 0016 made `&mutabilis T` a mutable borrow, so a
    function can fill an array it was handed — the gap this finding named is
    closed. The duplicated loop is not, because a shared
    `accumula(i0, q0, i1, q1: &mutabilis acies<i64, 20481>, t, …)` is four
    borrows plus the table: two words short of taking both the bound and a
    `Lector`, against the six-word ceiling (`c-backend.md` finding 14). The
    only form that fits takes samples already in memory, which `recipe.exsc`
    deliberately never holds, and the two copies run to different bounds,
    20,481 and 19,008.)*
12. **The hidden result pointer counts** (WC finding 8, again): four
    arrays plus a count plus an origin plus the pointer is seven words.
    `Praefixa` is the answer, and it is the better one — one thing to
    hand over.
13. **Two casts the spec left `[OPEN]` while the code ran them.** A narrowing
    `sicut` (`u16 → u8`, for a byte known to be below 256) and an
    equal-width sign change (`i64 → u64`, for a magnitude) are both
    `trunc` in the lowering and both run at eight bits in `angusta`
    (`300 sicut u8` is 44; `200 sicut i8` is −56). Spec §5.4 has only the
    widening sentence and said narrowing "stays `[OPEN]`". The receiver
    needs both; the design uses both. The amendment was recommended when
    this finding was written and held back as outside the two this design
    was opened for; **it was then made, in the follow-up commit after
    `bd316cd`**: spec §5.4 now defines narrowing as the low N bits of the
    two's-complement representation (never a trap — overflow behaviour
    belongs to the arithmetic operator, and a trapping cast would have a
    failure mode its type does not show) and the equal-width sign change
    as a reinterpretation, citing exactly what `angusta` and
    `conv_roundtrip.ir` run and marking the source-level directions no
    program has written (`i64 sicut u64` among them) `[UNTESTED]`. The
    alternative, avoiding them, was a byte read by comparison-and-subtract
    into a `u8` (possible, eight steps) and a magnitude by branch into
    `u64` — which needs the cast anyway.
14. **`*` on `i64` from source is `[UNTESTED]`** (modem.md §9 said so;
    still true: `tests/programs/phi_loops/` multiplies `u64`, `angusta`
    adds and compares `i8`). The IR's `mul` at width 64 traps on the
    hardware flag (WC D5) and D5's bounds keep it silent.
15. **The harness runs every binary with `stdin </dev/null`**
    (`tests/run.sh:767-782`). R2 needs a `stdin=PATH` key, repo-root-
    relative like `stdout=`, in `parse_run_keys` and the Python runner —
    the conformance/harness owner's tree. Reported; without it the three
    WAV tests cannot be written and only `hydramodem_circuitus` runs.
    **Retired before R2 started**: the key exists, `tests/README.md`
    documents it, and `tests/programs/lector/` was the first to use it.
16. **A 640 KB local is `[UNTESTED]`.** The Tier-1 emitter keeps every
    value in a stack slot; nothing in the tree has a slot above a few
    kilobytes, and the default stack is 8 MB. If the slot allocator
    refuses, the fallback is four separate 160 KB arrays and the
    `Praefixa` trick is replaced by passing the struct's *address* — which
    needs finding 11's `&T`.
    **Retired, and the fallback is what R2 took** — for finding 20's reason,
    not this one. The frames that run are `sub rsp, 661344` in
    `recipe.exsc`'s `initium` (four prefix arrays and the rest) and
    `sub rsp, 812224` in `circuitus.exsc`'s `circui` (those plus the 19,008
    synthesised samples), the largest in the tree by two orders of
    magnitude, 9.7 % of the 8 MiB default `RLIMIT_STACK`. There is still no
    stack probe and no frame bound — `compiler/x86_64/lower/expr.inc:2259`
    says so in as many words ("WHAT BOUNDS AN ARRAY IS THE STACK, AND
    NOTHING SAYS SO"; `[0; 1000000]` builds and runs, `[0; 2000000]` is a
    SIGSEGV with no diagnostic). 812 KB works because it was measured, not
    because anything checks it.
17. **Other trees the implementer must touch, reported here:**
    `prelude/README.md`'s per-atom table ("`read` is `[UNIMPLEMENTED]`")
    and "What is not here"; `tools/syscall-audit.sh`'s `ambitus` comment;
    `docs/design/runtime.md` section 2.4 (a `Lector` beside `Scriptor`) and its
    fixture list; `prelude/interface.inc` rows for the type, its two
    fields, `ab_introitu` and `lege_octeto`; `tests/run.sh` (finding 15).
18. **modem.md §9's prediction for M3 narrowed.** It listed "a stdin
    reader, signed multiply-accumulate, energy without overflow and
    without signed shifts, argmax acquisition, Viterbi with arrays that
    must be created — possibly signed shifts". Of those, the reader and
    array creation are D1 and D2; the multiply-accumulate and the
    energies are D5 with no scaling shift at all (Q7 makes them fit);
    the sketch's "abs then `deorsum 6` then square" is not needed; the
    one signed shift is R3's EMA (D10), not R2's. The sketch's "prefix
    sums over 19,008 samples per I/Q per tone" was right to the array.
19. **RECEIVER.md's "~11k candidate origins"** is the streaming buffer's
    (`frame_len + margin`); a `frame_tx` WAV has 1,921. Not an error —
    a different input — and the reason the scan is cheap here.

The four below were found by implementing R2 and are numbered after it.

20. **A struct literal's big array field cannot be compiled, and that is
    what killed `Praefixa`.** `mutabilis p = P { f1: [0; N] };` lowers the
    field into a temporary and `copy`s it, and
    `compiler/x86_64/backend_fasmg/emit.inc:3828` unrolls a `copy n` at
    compile time into n/8 emitted `mov` pairs — "Never `rep movsb`, which
    would need rdi and rsi", which is a defensible choice for the aggregates
    the tree had. At 8 bytes an instruction and ~59 bytes of text an
    instruction it is 7.35 bytes of fasmg per byte of array, and the
    compilation arena (`DRV_ARENA_FLOOR + 384 ×` the source, `driver/io.inc:
    217-219`) runs out. **Measured**, one field of `acies<i64, N>`, the rest
    of the program six lines: N = 16,384 compiles to 963,329 bytes of asm;
    N = 17,000 to 999,057; N = 18,000 traps; and so does every larger one,
    with `arena_alloc`'s `rassert` — SIGILL, exit 132, `exsc` printing
    nothing at all. The threshold is the arena's, not the struct's: it moves
    with the source's own length. A **plain local** `acies<i64, N>` takes the
    repeat form's *loop* (`lower/expr.inc:2268`, `LWR_ARRAY_UNROLL = 8`) and
    is 40,577 bytes of asm at N = 20,481 and at N = 200,000 alike.
    Not fixed at R2, and the fix was not obvious: a `copy` above some size
    wants a loop, which needs a counter register the emitter's three-scratch
    contract does not have spare, and a struct literal that lowered its
    fields straight into the destination would change the aliasing
    semantics D2 was careful about (`a = [a[1], a[0]]`). Reported for the
    backend's owner. The workaround costs the receiver nothing: four
    parameters instead of one, and no call needs more than six words.
    **Retired by `9ede8bf`.** The counter lives in the `copy`'s own stack
    slot, not a register: above `BFA_COPY_UNROLL_MAX = 128` bytes
    `__bfa_emit_copy` writes a loop — `rax` the destination, `rdx` the
    source, `rcx` the word in flight, the count in memory — then the same
    4/2/1 tail, a fixed dozen lines at any size; at or below 128 the text
    is byte-identical to before, so every pinned fixture is unchanged.
    `tests/ir/copy_magna.ir` copies 100,003 bytes and checks five positions
    including one past the end; `tests/programs/copia_magna/` is the
    six-line `acies<i64, 18000>` struct literal above, which compiles and
    exits 0. The same commit made `rt/arena.inc`'s exhaustion say what it
    knows on stderr (bytes wanted, used, capacity) before trapping, so the
    "printing nothing at all" half of this finding is gone too; the status
    is still 132, because §13 has no code for `exsc` running out of its own
    memory and none was invented. R2's receiver is unchanged: it still
    passes four arrays, and rewriting it to `Praefixa` is nobody's
    milestone.
21. **One of section 11's figures does not reproduce.** "prefix sums
    ≤ 7.0 × 10⁹" is wrong by 3.7×: the maximum over the three clean WAVs
    is **2.61 × 10¹⁰** (the all-zero frame; 1.53 × 10¹⁰ and 1.67 × 10¹⁰ for
    the other two), re-measured by an independent model that decodes all
    three. The other clean-frame figures reproduce **exactly** —
    |I|, |Q| ≤ 86,994,364 and E ≤ 8.111 × 10¹⁵ over the 356 frame windows at
    origins 959 and 960, the wrong tone's energy 0 at every one of them,
    known-prefix energy 3.238 × 10¹⁷, path metrics ≤ 2.98 × 10⁸, plateau
    938–981, score 40, refinement 0, shift 33 — which is what makes the one
    outlier worth reporting rather than shrugging at. It was checked against
    the other candidate explanation and is not one: rebuilding the prefix
    sums with the *transmitter's* phase convention (`(n+1)c mod 48` rather
    than `n c mod 48`) gives 2.70 × 10¹⁰, not 7.0 × 10⁹, and decodes all
    three either way — which incidentally confirms D4's claim that the
    detector does not care about a constant rotation. **Nothing is wrong
    with the design**: the proved bound is 8.52 × 10¹⁰ and `i64` holds it
    with 26 bits to spare whichever figure is right. The figure is `[UNREPRODUCED]`
    and the arithmetic is untouched.
22. **`mixtio` was never written.** Section 5's table gives the receiver a
    `mixtio(t, k, n, q) -> i64`, "the oscillator value for tone `k` at
    absolute sample `n`, walked not multiplied" — but a walk from sample 0
    to sample `n` is O(n) per sample and 19,008 samples would be 1.8 × 10⁸
    steps for the same four numbers the driver already has. Section 7's own
    worked example does not call it either: it carries `m0` and `m1` across
    the read loop, which is what both drivers do. The function is dropped;
    the walk is the design, and it lives where the loop that needs it does.
23. **The refinement's guard is the frame's last window, not the prefix's.**
    D6 says "skipping bases below 0 or whose last window exceeds `n`" and
    this takes it literally, testing `base + 17088 ≤ n`. The reference tests
    only the 40 known windows (`hydra_modem.c:256`, `a + L > nsamp`) and
    lets the data loop fail later with `HYDRA_ERR_NO_SYNC`. Here the
    difference matters for a reason it does not there: past `n` the prefix
    arrays hold zeros that were never written, so an energy computed from
    them is not a measurement of anything. On every input of R2's
    certificate the guard is slack by three orders of magnitude (n = 19,008,
    centre = 959, last admissible base 1,920), so it changes no verdict that
    has been observed; it is a difference from the reference and is recorded
    as one.

The six below were found by implementing R3 and are numbered after it.

24. **The EMA weight is visible to the certificate; the design expected it not
    to be.** D10 called 1/4 for the reference's 0.20 "a documented departure,
    admissible because the certificate is decode success", which is true, and
    the implicit expectation was that 1/2 would be equally admissible. It is
    not: the 1/2 mutant loses four vectors — three clock offsets and, oddly,
    a −200 Hz frequency offset — all by CRC, with the clean channel and the
    140-word loopback untouched. So the constant is not free, and the
    certificate can see it where it cannot see a table entry or a threshold at
    the margin (section 6's four negative controls). 1/4 is *not* thereby shown
    optimal; it is shown to be inside the set that passes and 1/2 outside it.
25. **The transition gate's certificate is the CLEAN channel, not the impaired
    vectors — the opposite of what it was written for.** Removing it fails all
    three clean WAVs (exit 2) and all 140 loopback words, and passes **every
    one of the 70 impaired vectors**. The reason is exact ties: on a noiseless
    same-tone run the total energy at the five offsets is *equal to the last
    integer*, the argmax's strict `>` keeps the first, and the first is −2, so
    the grid walks backwards one sample a symbol. Add noise and the profile is
    never exactly flat, the argmax scatters symmetrically about 0, and the EMA
    averages it to nothing. The reference's own comment ("same-tone runs are
    flat and carry no timing information") describes the mechanism; what it
    does not say, and what an integer receiver makes stark, is that the failure
    needs an *exactly* flat profile, which only a synthetic input has.
    HydraModem's `double` energies would tie far less often, so this is a
    hazard the integer arithmetic sharpens rather than one it inherits.
26. **Finding 6's prediction is falsified.** The plateau-edge mutant was
    predicted to become visible on the clock-offset vectors. It decodes 62 of
    62 with the timing loop and 55 of 62 without it — the same 55 the unmutated
    R2 receiver decodes — so it is invisible either way. With the loop the
    reason is mechanical and worth stating: starting 21 samples early, the gate
    fires at the first transition, the argmax reads +2 every symbol, `vagatio`
    climbs to its ceiling and the grid advances 49 samples a symbol until it
    catches up, about twenty symbols in. The loop does not *expose* the missing
    refinement, it *absorbs* it. The plateau centre and the ±24 refinement
    remain a blind spot of this certificate with no vector in sight that would
    close it.
27. **The receiver inherits one of the reference's failures and not the other.**
    At +300 Hz the reference fails by its timing loop (finding 7) and this
    receiver decodes — the one-directional case D11 allows, recorded in
    `receptio_vec_freq_loopback_p300hz/`'s own `TEST` as recorded and not
    certified. At −300 Hz the reference fails and so does this receiver, by
    CRC — where the **R2** receiver, with no timing loop, decoded it. So adding
    the loop lost one input the reference also refuses. Permitted by the
    certificate, and recorded because it is the loop reacting to a frequency
    offset it was not designed for, which is the same mechanism finding 7
    describes in the reference.
28. **Finding 20's compiler defect is fixed, and D3's `Praefixa` now compiles.**
    `9ede8bf` (another tree, landed while R3 was in progress) emits a `copy`
    above 128 bytes as a loop instead of unrolling it, and makes an exhausted
    compilation arena print a message instead of trapping. Re-measured here:
    the four-field `Praefixa { i0: [0; 20481], … }` of D3 compiles to **46,940
    bytes of asm**, and a thousand by-value `summa(p, a)` calls run in 2 ms, so
    the struct travels as one word and not as a 640 KB copy — which is what D3
    assumed and R2 could not check. **The receiver was not converted**, because
    that is a whole-file refactor of R2's decision landing inside R3's commit
    and it would have made the tallies and the mutants harder to attribute. It
    is a named follow-up, and it would retire two workarounds at once: the four
    array parameters, and D10's `(o sursum 32) + n` packing, since
    `mollia(p, o, n)` is three words and the emitter's limit is six.
29. **`impedi.py` takes the top sixteen bits of xorshift64, not the bottom.**
    D11's sketch said "masked to 16 bits"; xorshift64's low bits are its weak
    ones, and a noise vector should not inherit that. Stated in
    `vendor/hydramodem-rx/PROVENANCE.md` as the departure it is, with the
    stream measured: over 200,000 draws from seed 1, mean −287, σ 65,738,
    kurtosis 2.9023 against Irwin–Hall(12)'s exact 2.9. The 0.3 % excess σ makes
    every labelled SNR pessimistic by 0.03 dB. The generator also discards 64
    warm-up steps, so the seeds can be the readable integers 1–36 rather than
    large ones chosen to look random.

## 9. Later milestones

**R3 — robustness. Done.** `vendor/hydramodem-rx/` is vendored (70 files, the
generator and the offset renderer printed verbatim in `PROVENANCE.md`, the
reference's verdicts in `verdicta.tsv`), the timing loop of D10 is in
`mollia`, and 70 `receptio_vec_*` directories run. Language needed, as
predicted: a signed division by a power of two for the EMA and the track. The
decision the section asked for is taken — **sign-and-magnitude in `u64`, and
the signed shift stays `[OPEN]`**, because all 24 clock-offset vectors decode
without it (section 13). Certified by the 70 vectors; **not** by the
plateau-edge mutant, which finding 26 records as a falsified prediction.

**M4 — other profiles**, transmitter and receiver together, each certified
against new `frame_tx` renders (for the TX, byte-identical) and the C
receiver's verdicts (for the RX): the aux-cable profile (1200 baud, `spp
= 40`, tones 1200/2400, `c_k = 1, 2`, a 40-entry table, a 16-symbol
preamble — `hydra_profile.c:22-39`); 4- and 8-FSK (2 and 3 bits a symbol;
`bits_to_symbols` groups MSB-first and zero-pads the last symbol; the
soft bit for bit `b` is `max` over tones with that bit set minus `max`
over the rest, `bit_soft`, `hydra_modem.c:173-184`; 316 coded bits become
158 or 106 symbols, the sync 8 or 6); and 125 baud (`spp = 384`, tones
1000/1500 per `test_channel.c:88-89`, `c_k = 8, 12`; the reverberation
profile). Language needed: nothing beyond D2 — a profile is a set of
`firma` constants, a table per `spp` as a literal, and for M-FSK a
`Praefixa` with `2N` arrays and a max over `N` energies. `[OPEN]` items
forced: none. The bound of D5 with `L = 384`: `|I| ≤ 384 · 4,161,536 <
2³¹`, so `E < 2 · 2⁶² = 2⁶³` (5.1 × 10¹⁸ against 9.2 × 10¹⁸) — a single
energy still fits, with no headroom to speak of, but the acquisition sum
of 40 known energies does not, and **must** be scaled by a shift on the
magnitude before summing; M4's design says where. Excluded as before: any profile
where `sr/baud` is not an integer.

**M5 — the transmitter's table as a literal.** Replace `sinus`'s nine-arm
`discerne` with a 13-element `acies<u16, 13>` literal of the quarter-wave
values (the four dead entries carried as 0 or as their `[UNTESTED]`
values, modem.md D4 — a decision M5 takes) and the same fold; the
certificate is that the three M1 WAVs and the M2 basis stay
byte-identical — `cmp` reports nothing. Language needed: D2. This is the
"three tables in hand" modem.md D4 asked for: the transmitter's, and the
receiver's cosine table which is also its sine table. The third data
point turned out to be the buffers — `[0; N]` — which are not tables at
all and which modem.md §9 rightly said not to conflate: D2 gives them a
second form rather than a second construct. **Done**, `76ca763`, with one
departure: all 48 entries are written (the fold applied where the table is
written, not read), the dead entries as 0 — what the `aliter` arm produced
— and the four transmitter certificates and five `receptio_*` directories
stay byte-identical (modem.md D4, as amended).

## 10. What retires each marker

| decision or claim | milestone | the test as planned | what retires it |
|---|---|---|---|
| D1 `Lector`, `ab_introitu`, `lege_octeto` `[UNTESTED]`; the 256 sentinel | prelude, then R2 | `tests/unit/prelude_lege_octeto.asm`; `tests/programs/lector/` | **retired**: those fixtures run, and R2's four stdin tests read 38,060 bytes apiece through them |
| D2 array literals `[UNTESTED]`, both forms, typing, the six codes | checker, then R2 | `cst_arraylit.asm`, `chk_ty_arraylit.asm`, `lwr_arraylit.asm`, `tests/programs/acies/` | **retired**, `54ba744`: those run as `tests/unit/{cst,chk_ty,lwr}_acies.asm` and `tests/programs/acies/`, and `receptor.exsc` is written in both forms — the 48-entry signed table, `[0; 20481]`, `[0; 10112]`, `[-4611686018427387904; 64]` |
| D3 header check; D4 the table; D5 the bounds; D6 acquisition; D7 the metric; D8 the trellis; D9 the residue | R2 | `tests/programs/receptio_{loopback,exemplum,vacuum}/`, `receptio_circuitus/` (140/140), `receptio_caput/` | **retired** by those five directories, all green. D3's `Praefixa` is *not* what shipped (finding 20); D5's `hydramodem_rx_plenus/`, a full-scale synthetic input, is **not written** — the bounds are exercised at 0.9 of full scale and no higher |
| section 6's five failing mutants and four negative controls | R2 | the mutation run over a copy of `examples/hydramodem/` | **retired**: 9 of 9 observed as predicted, section 12 |
| D7's argument (the difference is the sounder metric) | never fully | R3's vectors are consistent with it; a proof is not a test | stays an argument, as D9's affinity does |
| D10 the timing loop | R3 | the clock-offset vectors | **retired**: the loop is in `mollia`, 24 of 24 clock vectors decode where the same receiver without it decodes 17, and the signed shift was not needed. The four things D10 left open are decided in its own text |
| finding 6's prediction (the plateau centre becomes visible on clock offsets) | R3 | the plateau-edge mutant failing on them | **falsified**, finding 26: 62 of 62 with the loop, 55 of 62 without, which is exactly the unmutated R2 receiver's own score. No vector closes this blind spot |
| D11 R3: vectors against the reference's verdicts | R3 | one directory per vector | **retired**: 70 vectors, 70 directories, 62 of the reference's 62 decodes, no wrong frame. The set is not the 35 planned — three frames throughout, −12 dB for −9, both signs of Δf — and D11 says why |
| finding 7 (+300 Hz: reference fails by timing, receiver decodes) | R3 | the `+300` vector with `expect-exit=0` — the receiver decodes it, or the loop makes it fail as the reference does; either is recorded, neither certified | **recorded, not certified**: at +300 Hz this receiver decodes where the reference does not; at **−300** Hz it fails where the R2 receiver decoded. Finding 27 |
| R3's three timing-loop mutants | R3 | predictions in section 6 | **run**: two predictions held, one was wrong (the EMA weight is visible) and one was half wrong (the gate's certificate is the clean channel). Findings 24 and 25 |
| finding 20's compiler defect (a struct literal's big `acies` field) | the backend's owner | — | **fixed by `9ede8bf`** and re-measured here, finding 28: `Praefixa` compiles to 46,940 bytes of asm and travels as one word. The receiver still passes four arrays; converting it is a named follow-up |
| finding 13 (two casts the spec left `[OPEN]`) | the follow-up commit after `bd316cd` | — | **retired** as a spec question: §5.4 defines both as truncation; the source-level directions no program writes stay `[UNTESTED]` there |
| findings 11, 15, 16 (in-place mutation; `stdin=`; a 640 KB local) | the implementer's trees | — | 15 and 16 **retired** (the key exists; the frames are 661,344 and 812,224 bytes and run). 11 stands: the accumulate loop is still written twice, once per driver |
| finding 14 (`*` on `i64` from source `[UNTESTED]`) | R2 | — | **retired**: `x * t[m0]` runs 4 × 19,008 times a decode and `i * i + q * q` ~150,000 times, on every one of the five directories |
| section 5's function table (`mixtio`; `Praefixa` in five signatures) | R2 | — | **superseded** by findings 20 and 22; section 5 is the design's plan and `examples/hydramodem/receptor.exsc` is what runs |
| finding 20 (a large `copy` unrolled; a struct literal's big array field traps the compiler) | the backend's tree | a struct literal with an `acies<i64, 18000>` field compiling | **retired**, `9ede8bf`: `tests/ir/copy_magna.ir` (100,003 bytes through the loop form), `tests/programs/copia_magna/` (the struct that trapped, exit 0). The receiver is not rewritten to use it |
| finding 21 (section 11's prefix-sum maximum) | — | — | stays `[UNREPRODUCED]` as a figure of the scratch model; section 12's 2.61 × 10¹⁰ is the measured value and the bound covers both |
| findings 22, 23 (`mixtio` dropped; the refinement's guard) | R2 | — | recorded as departures, not defects: 22 changes no arithmetic, 23 changes no verdict on any input observed |
| M5's byte-identity | M5 | `cmp` on the M1 WAVs and the M2 basis after M5 | **retired**, `76ca763`: 4/4 transmitter certificates and 5/5 `receptio_*` unchanged (modem.md D4) |
| M4's per-profile tables and the scaled acquisition sum | M4 | new `frame_tx` renders per profile | the renders are vendored (`6121656`, `vendor/hydramodem-tx/profiles/`, twelve WAVs); no Exsecutor program reads them yet |

## 11. What was measured for this document

All in a scratch directory, nothing committed, nothing on the build path;
every figure re-measurable by the recipe named.

- **The oracle.** `frame_tx` and `frame_rx` built from a copy of
  `hydramodem/` at `fce2813` with `PROVENANCE.md`'s line (`cc -std=gnu11
  -O2 -Wall -Wextra`, gcc 14.3.0). `frame_tx` on the loopback frame:
  `f422db1d…a280bd`, the vendored digest. `frame_rx` on the three
  vendored WAVs: the three frames.
- **The integer model** (`rx_model.py`, Python 3.13, `int` throughout):
  D3's header check, D4's table and signing, D5's prefix sums, D6's
  acquisition with the plateau centre and the ±24 refinement, D7's
  difference and per-frame shift, D8's 64-state Viterbi with the
  one-bit decision buffer and the traceback off the state, D9's scatter
  and residue. **Three vendored WAVs: 3/3**, each to its frame, origin
  959, score 40, plateau 938–981, refinement 0, shift 33. **137 basis
  renders** (the zero word and the 136 one-hot words, rendered by the
  scratch `frame_tx`): **137/137**, every one back to its word.
- **The bounds, observed** on the clean frames: prefix sums ≤ 7.0 × 10⁹
  (`[UNREPRODUCED]` — finding 21; R2's re-measurement is 2.61 × 10¹⁰);
  |I|, |Q| ≤ 86,994,364; |soft| ≤ 8.11 × 10¹⁵; known-prefix energy 3.24 ×
  10¹⁷; path metrics ≤ 2.98 × 10⁸; the wrong tone's window energy
  **exactly 0** at all 356 windows of all three frames, at origins 959 and
  960 alike (D4's orthogonality argument, `leak.py`).
- **The reference's diagnostics** (`hydra_modem_rx_ex` through a scratch
  wrapper): clean frame `origin 959, sync_score 16, est_sps 48.0000`;
  +250 Hz `rc 0, origin 953, est_sps 48.0634 (+1320 ppm)`; +300 Hz
  `rc −5 (CRC), origin 958, sync_score 16, est_sps 47.8607 (−2902 ppm)`.
- **Mutants** on the three WAVs and two −6 dB vectors: taps swapped → CRC
  failure on every input; stride 17 → CRC failure; sync complement →
  no sync, scores 31–34; both oscillators on tone 0 → no sync, score 20;
  threshold 41 → no sync. Negative controls: sync one bit → decodes,
  scores 39 (37 on one noisy vector); threshold 36 → decodes; plateau
  first-origin → decodes, every input; `T7[0] = 126` → decodes.
- **The impairment sweep** (`impair.py`, integers only; `sweep.py`): AWGN
  on all three frames at 12, 6, 3, 0, −3, −6 dB × 6 seeds = 108 files:
  reference 108/108, model 108/108, plateau-edge mutant 108/108,
  normalised-metric variant 108/108. At −9, −12, −15 dB × 6 seeds × 3
  frames = 54 files: reference 0/54; model 0/54 with no wrong frame ever
  written (at −9 dB: 15 "no sync", 3 "CRC"; below: all "no sync"). Clock
  offsets +200, ±500, ±1000, ±2000 ppm on the loopback frame: reference
  7/7; the R2 model (no loop) 6/7, failing +2000 by CRC. Frequency
  offsets +50, ±100, +150, ±200, +250, +300 Hz: reference 7/8 (+300
  fails); model 8/8. **177 inputs in all. On every noise- and
  frequency-impaired input the reference decoded (115), the model decoded
  the same frame; on no input of the 177 did the model write a frame that
  was not the input's; the one input the reference decoded and the model
  did not is the +2000 ppm clock offset, which is what D10's loop is
  for.** The σ integers for the AWGN
  rows (loopback frame): 5,535 (12 dB), 11,043 (6), 15,599 (3), 22,035
  (0), 31,125 (−3), 43,965 (−6), 62,102 (−9), 87,721 (−12), 123,909
  (−15); the other two frames' differ in the last two or three digits
  and are printed by the generator.
- **The offset renderer** (`frame_tx_offset.c`, forty lines against the
  reference's own `hydra_frame_build`, `hydra_tx_dsp_process`, gain and
  `hydra_wav_write`): at `Δf = 0`, byte-identical to the vendored
  loopback WAV (`cmp` silent).
- **Not measured:** any Exsecutor receiver; `Lector`; any array literal;
  the cost of the loopback driver; the timing loop of D10 in any form;
  the plateau-edge mutant on clock-offset vectors; any toolchain but the
  one recorded.

## 12. What R2 measured, on the program that runs

Everything here is `tests/run.sh`'s own output or a command run beside it in
a clean worktree at the commit that added `examples/hydramodem/receptor.exsc`.
Section 11's figures are the scratch model's and stand as they are, except
where finding 21 says otherwise.

- **The three vendored WAVs decode.** `receptio_loopback/`,
  `receptio_exemplum/`, `receptio_vacuum/`: exit 0 and 17 bytes each,
  `cmp`ed against a committed `expected.out` — `d310123400a1ffffdeadbeef0a1b2ca961`,
  `d31312340001ffffdeadbeefab12cd24c0`, `d310000000000000000000000000005b80`.
  **3 of 3, first run, no iteration on the algorithm.**
- **The loopback: 140 of 140.** `receptio_circuitus/` writes 140 zero bytes
  and exits 0. The zero word, the 136 one-hot words, and the three frames,
  each synthesised in-process from `modulator.exsc`'s own `sona` through
  `Exemplum`'s two bytes and `signatum`, and each decoded back to its own
  seventeen bytes. No pipe, no second binary, no WAV.
- **The header check refuses what the reference would mis-decode.**
  `receptio_caput/` feeds 44 bytes that differ from `caput()`'s only in the
  sample rate (44,100 for 48,000) and gets exit 3 with no output. Its own
  `TEST` records what it does *not* prove, measured: with the rate check
  deleted the same fixture still exits 3, on truncation, so the directory
  certifies "the header path rejects and writes nothing" and not the rate
  field in particular. D3's field-by-field sweep is not written.
- **The nine mutants: 9 of 9 as predicted** (section 6's table, the
  `observed, R2` column). Five fail, four decode. The four that decode do so
  byte for byte on all three WAVs *and* 140 of 140 on the loopback, which is
  the strongest form of "this certificate cannot see it".
- **Timing.** One WAV decode: **25 ms** wall, of which 13 ms is system time —
  38,060 one-byte `read` syscalls, D1's stated cost. The 140-word loopback,
  which reads nothing: **1.17 s**, 8.4 ms a word for modulation, 19,008
  samples of down-conversion, a 1,921-origin acquisition scan and a 158-step
  trellis. Both are far inside the harness's 20-second limit, and no
  buffered reader is needed to keep them there. `tests/run.sh` end to end:
  694 checks, 0 failures, 21 program directories.
- **Frames.** `sub rsp, 812224` in `circuitus.exsc`'s `circui` is the largest
  in the tree; `recipe.exsc`'s `initium` is 661,344. 9.7 % of the default
  8 MiB stack, with no probe and no bound checking it (finding 16).
- **The bounds, re-derived before the code was written, and every one holds.**
  `|x·T7| ≤ 4,161,536 < 2²²`; a window sum `≤ 199,753,728 < 2²⁸`; a prefix
  sum `≤ 85,228,257,280 < 2³⁷`; an energy `< 2·199,753,728² = 7.98 × 10¹⁶
  < 2⁵⁷`; the acquisition sum of 40 `< 3.19 × 10¹⁸ < 2⁶³`; a scaled soft bit
  `≤ 2²⁰` and a path metric `≤ 316 · 2²⁰ < 2²⁹`. Observed, by the
  re-derived model on the three clean WAVs: prefix ≤ 2.61 × 10¹⁰
  (finding 21), |I|,|Q| ≤ 88,966,905 over the whole acquisition scan and
  86,994,364 over the frame's own windows, E ≤ 8.39 × 10¹⁵ / 8.11 × 10¹⁵,
  acquisition sum 3.238 × 10¹⁷, path metrics ≤ 2.98 × 10⁸, shift 33,
  plateau 938–981, score 40, refinement 0, origin **959**. **No arithmetic
  trap fired anywhere**: 3 WAVs and 140 loopback words on the shipped
  receiver, and 3 + 140 again on each of the nine mutants — 1,430 decodes,
  every exit status 0, 1, 2 or 3 and never a SIGILL. That is the bounds' real
  test, since every `+`, `-` and `*` above is the trapping kind.
- **The wrong tone's energy is exactly 0** at all 356 frame windows of all
  three WAVs, at origins 959 and 960 alike — D4's orthogonality argument,
  re-measured and reproduced.
- **The table.** `T7` re-derived from the thirteen values and the two
  identities, then compared entry by entry with `round(127·cos(2πm/48))`
  computed in binary64: they differ at **exactly one** index, 16, where the
  identity gives −64 and `libm` gives −63. Finding 10, reproduced.
- **Not measured at R2:** anything impaired (that is R3); a full-scale input
  (`hydramodem_rx_plenus/` is not written, so the bounds are exercised at
  0.9 of full scale); the timing loop in any form; `Praefixa` working, since
  it does not compile; the per-field header sweep D3 asks for, of which
  `receptio_caput/` is one field.

## 13. What R3 measured, on the program that runs

Everything here is `tests/run.sh`'s own output or a command run beside it in a
clean worktree at the commit that added the timing loop, with `frame_rx` built
from HydraMesh `fce2813` by `vendor/hydramodem-rx/PROVENANCE.md`'s recipe
(re-verified first: `frame_tx` from the same build re-rendered all three
vendored WAVs to their recorded digests).

- **The certificate: 62 of 62, and no wrong frame.** Over the 70 vendored
  vectors, HydraModem's `frame_rx` decodes 62 and refuses 8. This receiver
  decodes **every one of the 62, each to the input's own frame**, refuses 7 of
  the 8 the reference refuses, and decodes the eighth — `freq/loopback-p300hz`
  — correctly. **On no input of the 70 did it write a frame that was not the
  input's.** By kind: AWGN 30 of the reference's 30 (and 0 of the 6 it
  refuses); clock 24 of 24; frequency 8 of 8 (and 1 of the 2 it refuses).
- **The loop is what buys the clock offsets.** The same receiver with D10's
  loop removed — HEAD's `receptor.exsc` at `8deb727`, rebuilt against this
  tree — decodes **55 of 62**, losing seven: `±3000 ppm` on all three frames
  and `+2000 ppm` on the loopback frame, every one by CRC. Nothing else in the
  set separates the two receivers except the ±300 Hz pair of finding 27.
- **The clean-channel certificates are untouched, and so is the grid.**
  `receptio_{loopback,exemplum,vacuum}/` decode to their 17 bytes, and
  `receptio_circuitus/` writes 140 zero bytes. An instrumented build reports
  the origin, the per-frame scaling shift, and the first and last sampled
  window for each of the three WAVs, with and without the loop: **959, 33,
  2879, 17999 — identical on all three frames, both ways.** On a clean channel
  the gate fires at every transition, the argmax reads 0, `vagatio` never
  leaves 0, and the timing loop samples exactly the grid R2 sampled. The
  decoded origins and shifts did not change.
- **The clamp never binds.** The same instrumented build reports the largest
  window the loop asks for on each of the 70 vectors and the three WAVs; the
  tightest is `freq/loopback-m300hz.wav`, whose last window ends 938 samples
  short of `n`. D10's fourth decision is a guard, not a behaviour.
- **The three mutants**, section 6's table: EMA 1/2 loses four vectors, the
  inverted track loses 26, the removed gate loses the clean channel and the
  140-word loopback and no vector at all. Predictions and what they were wrong
  about are findings 24 and 25.
- **Timing.** One decode: **24.9 ms** wall over ten runs of a clean WAV
  (12.6 ms of it system time — the 38,060 one-byte reads D1 costed), against
  **25.6 ms** for the same measurement on the loop-less R2 binary: the loop's
  3,160 extra energies are lost in the acquisition scan's 153,680. All 70
  vectors back to back: **1.78 s**. The 140-word loopback: **1.42 s** (R2's was
  1.17 s; it synthesises as well as decodes). `tests/run.sh` end to end:
  **2 m 11.7 s, 973 checks, 0 failures, 92 program directories**, against
  **1 m 49.2 s and 700 checks** for the same tree with the seventy
  `receptio_vec_*` directories moved aside — so the certificate costs **22.5 s**,
  0.32 s a directory, of which the decode is 25 ms and the rest is compiling and
  assembling the same four sources afresh for each.
- **No arithmetic trap fired anywhere.** Seven binaries were run over the whole
  set — the shipped receiver, the loop-less R2 one, the three timing mutants,
  and the plateau-edge mutant in both its R2 and R3 forms — which is 70 × 7 =
  490 impaired decodes, plus the three clean WAVs on each of the seven and 140
  loopback words on each of four. Every exit status was 0, 1 or 2; never a
  SIGILL. D5's bounds cover the loop's own arithmetic — a
  total energy under 1.6 × 10¹⁷ and twenty of those under 3.2 × 10¹⁸, inside
  `i64` — and that is their test, since the cross-multiplied gate is a trapping
  multiply.
- **Not measured at R3:** a full-scale synthetic input (`hydramodem_rx_plenus/`
  is still not written); the per-field header sweep; `Praefixa` in the
  receiver (it compiles — finding 28 — but the receiver was not converted); any
  toolchain but the one recorded; whether 1/4 is the *best* EMA weight rather
  than one inside the set that passes.
