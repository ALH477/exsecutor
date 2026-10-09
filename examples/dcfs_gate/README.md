# dcfs_gate -- the admission gate for DCF serializer frames

`dcf-serializer` (C11) frames the UDP traffic between DCF components. Its
reader parses bytes a remote peer chose. The C reader is now strict by default
and memory-safe on its own (that is fixed in C and does not depend on this
unit). This unit is a **second, independent statement of the same frame policy**
in a language whose trap-free subset can be checked: header, CRC-32 and the
whole payload grammar. The host admits a frame only when both agree.

Pure -- no `poscit`, no capability parameter (spec §4.1 rule 6), no allocation,
no I/O, no recursion -- and total: nothing a caller passes traps, which matters
because a trap reaches `exsrt_abortus` and ends the host process.

| function | question | verdicts |
|---|---|---|
| `admitte_caput(h, n)` | `h` = the 17 header bytes, `n` = the **total** bytes available (any value to 2^64-1): is the header acceptable for a frame that long? | 0 yes; 1 fewer than 17 bytes; 2 magic; 3 major version != 5; 4 flag outside STREAMING/FINAL/PRIORITY; 5 payload_len > 16 MiB; 6 `n != 17 + payload_len + 4` |
| `admitte_corpus(b, n)` | `b` = a whole frame of at most 65557 bytes: header policy, CRC-32 and the payload grammar | 1..6 as above; 7 n > 65557; 8 CRC-32; 9 unsupported tag; 10 truncated; 11 nesting > 32; 12 count; 13 string > 65536; 14 not UTF-8; 15 varint not canonical |

`dcfs_gate.h` is the hand-written C face. `b` is a buffer of exactly 17 or
65557 bytes, all readable; the host copies its input into a zero-padded buffer of
that size. No byte at an index `>= n` is consulted, so what the padding holds is
irrelevant (`proba.c` has a `corpusg` mode that pads with `0xA5` to prove it).
A frame longer than 65557 bytes cannot be copied whole: the host passes its first
17 bytes and its length to `admitte_caput`, and the C validator alone judges the
payload.

## The grammar

This is the contract all three implementations (this unit, the C reference in
`dcf_serialize.c`, and the Python oracle in `proba.py`) were written from.

```
frame   := header payload crc
header  := magic(4) version(2) msg_type(2) flags(1) payload_len(4) sequence(4)    -- big-endian, 17 bytes
           magic = 0x44434653 ("DCFS"); version: the major (first) byte is 5, the minor is free
           flags: only STREAMING 0x04, FINAL 0x08, PRIORITY 0x10 may be set
                  (COMPRESSED 0x01, ENCRYPTED 0x02, NO_CRC 0x20, 0x40 and EXTENDED 0x80 are refused)
           payload_len <= 16777216
frame is exactly 17 + payload_len + 4 bytes
crc     := CRC-32 of header payload (IEEE 802.3, reflected, polynomial 0xEDB88320,
           init and final xor 0xFFFFFFFF), big-endian
payload := value*                                  -- consumes exactly payload_len bytes
value   := tag body
  0x00 NULL                               no body
  0x01 BOOL  0x02 U8  0x03 I8             1 byte
  0x04 U16   0x05 I16                     2 bytes
  0x06 U32   0x07 I32  0x0A F32           4 bytes
  0x08 U64   0x09 I64  0x0B F64
  0x30 TIMESTAMP  0x31 DURATION           8 bytes
  0x13 UUID                               16 bytes
  0x10 VARINT    LEB128, 1..10 bytes; the 10th byte is 0 or 1; a varint of >= 2 bytes does not end in 0x00
  0x11 STRING    u32 len <= 65536, then len bytes of UTF-8 (RFC 3629; NUL allowed)
  0x12 BYTES     u32 len, then len bytes
  0x20 ARRAY     elem_type u8, u32 count <= 1048576 and <= payload bytes left, then `count` values
  0x21 MAP       key_type u8, val_type u8, u32 count <= 1048576 and 2*count <= payload bytes left,
                 then 2*count values
  0x22 STRUCT    u16 type_id, then fields until the end marker; a field is u16 id, u8 type, value;
                 the end marker is id 0 with type 0 (so id 0 with any other type is an ordinary field,
                 and any other id with type 0 is a field whose value follows)
  any other tag (0x23 TUPLE, 0x32 OPTIONAL, 0x33 ENUM, 0xFE EXTENSION, unassigned): refused
containers (ARRAY, MAP, STRUCT) nest at most 32 deep
```

The verdict is the **first failing check in scan order**: the header checks in
the order of the table above, then CRC, then the payload left to right; within a
value the tag, then the bytes of its header, then its count or length caps, then
its depth, then its body. The full table is the comment at the top of
`dcfs_gate.exsc`.

Two caps are redundant *inside the gate's capacity* and are kept for fidelity to
the C grammar: a count above 1048576 cannot also fit in a payload of at most
65536 bytes, and a string longer than 65531 bytes cannot fit either, so the
"bytes left" / "length past the payload" check answers first.

## What ran

`proba_c.sh` (needs `build/exsc`; no `fasmg`; python3; cargo for the Rust step;
about 2m15s):

- emission of the C unit, C face and Rust face twice, byte-identical, and the
  hand-written header compared prototype by prototype with the generated one;
- **281,314 cases** through gcc and clang at `-O0` and `-O2` under UBSan, and
  through one gcc `-O1` ASan+UBSan build whose buffers are exactly the declared
  size, with **0 disagreements** against `proba.py`, which compares the *exact
  verdict* (not only admit/refuse). Verdicts reached, by count: 0:32928 1:12591
  2:20728 3:5309 4:7030 5:1315 6:41258 7:31 8:4846 9:81805 10:8404 11:55 12:7924
  13:1075 14:44977 15:11038 -- every verdict from 0 to 15 is hit, and the run
  fails if one is not. 99 (the loop bound) is never reached, as argued;
- the cases: every header byte perturbed, every `flags` value, every major
  version, `payload_len` and `n` boundaries including `n = 2^63` and `2^64-1`;
  random structured payloads of every value type, in valid frames, byte-mutated,
  with the length and CRC repaired or not (an unrepaired CRC would stop every
  case at the CRC check); nesting sweeps 0..40 for arrays, maps and structs;
  count and length boundaries; a UTF-8 table (overlong, surrogate, > U+10FFFF,
  cut short, every lead byte against continuation bytes); varint shapes;
  struct end-marker lookalikes; lying lengths (a valid frame passed with every
  `n` from 0 to its length + 30, around the 65557 capacity, and 2^32, 2^63,
  2^64-1); and 15 anchors with known answers so a corpus that quietly stopped
  reaching a check cannot pass. The zero-padded and `0xA5`-padded buffers must
  give the same verdict;
- **26 behaviour mutants** of the source, each of which must **build** and then
  **fail** the oracle (or trap): a flag mask off by one bit, the version check
  removed, `payload_len` cap and length rule loosened, a one-byte-short header,
  the CRC polynomial / final xor / whole check, the capacity check (this one
  traps, and the host aborts), six UTF-8 rules separately, the string-body bound,
  the string cap, varint canonical form and 10th byte, nesting limit 33, the map
  and array count checks, the struct end marker ignoring its type, a fixed-size
  tag missing, a size off by one, and unassigned tags admitted. All 26 are caught;
- the rule-6 capability checks: an ambient `ambitus`/`archivum` is refused
  `EXS-E0421`, and a `Scriptor` parameter breaks both headers;
- the Rust face: the generated extern block links the unit and answers.

One further run of `proba.py --cases 800000 --seed 99` (1,138,203 cases, gcc -O2 host):
0 disagreements, every verdict 0..15 reached.

Separately, in the `dcf-serializer` repository (`gate/dcfs_gate_diff_test.c`),
this unit's C emission is compared against the C reference implementation on
**358,132 frames** of up to 65557 bytes (deterministic generator, structured
families plus 250,000 mutated random frames) with **0 disagreements** -- and,
with five other seeds of 1,500,000 random cases each, about 9.6 M more frames, still
0 -- and 12
mutants of this source were each noticed by that comparison except one (the
string cap at 65537, which is equivalent there because only admit/refuse is
compared -- `proba.py` compares exact verdicts and does catch it). That
comparison found a real defect on its first run: the C library's CRC-32 table
had a mistyped entry (see that repository's README, "Security changes").

## What was not

- **The three implementations share an author and a reading of the grammar.**
  They were written from the same text. Agreement catches slips in any one of
  them (it caught a typo in the C CRC table); it cannot catch a misreading the
  three share. The independence is of *code*, not of *design*. `[OPEN]`: a
  review by someone who did not write the grammar.
- Throughput. The CRC here is bit-by-bit (no table, no module-level array). Measured
  with `make bench` in `dcf-serializer` on this sandbox, once: the gate alone about
  90 MB/s (C validation about 300 MB/s), so a maximum-size frame costs about 0.7 ms in
  the gate and about 0.2 ms in C. One machine, one run: not a benchmark of anything
  else. A host that cannot afford the gate sets `DCF_SER_POLICY_NO_GATE` for its
  reader; the gate is a second opinion, not the only check. A table-driven CRC for
  large frames is possible and not done.
- A frame longer than 65557 bytes is judged by this unit for its header only.
  That is a limit of the fixed-buffer ABI (no slice type), not a decision.
- No consumer is wired by this directory. `dcf-serializer` vendors the emitted C
  (`gate/`, with `PROVENANCE.md`); its repository says what it ran.
- `[UNTESTED]` on 32-bit hosts and non-Linux hosts: the emitted C is
  `uint64_t`-based; only x86_64 Linux with gcc 13 and clang 18 was run.
- Trap containment. `exsrt_abortus` is host-supplied. `dcf-serializer` supplies a
  `_Thread_local` setjmp guard (`gate/dcfs_gate_host.c`) and tests it with eight
  threads trapping concurrently; a mutant of this unit that does trap (the
  string-body bound removed, on a frame that claims a 64 KiB string) was run
  through it and returned a contained trap with the process alive. The `[OPEN]`
  in `examples/dcf_net_gate/README.md` about `examples/abortus/tutela.c` is
  answered by reading `tutela.c` (`summa` and `profunditas` are `_Thread_local`)
  and by running `examples/abortus/proba_c.sh` (25/25 checks per build, which
  include eight threads x 100,000 rounds of traps; PASS); this directory does
  not depend on it.
- Licence: the `.exsc` source is GPL-3.0-or-later; `LICENSE.EXCEPTION` Exception A
  frees only the compiler's own contribution to the emitted C. Vendoring the
  emitted C into the BSD-3-Clause `dcf-serializer` needs a relicensing grant of the
  kind `LICENSE.GRANTS` GRANT 1 gives `custos`. Status: pending owner decision.
