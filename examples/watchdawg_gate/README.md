# watchdawg_gate -- the two numbers a root script pastes into JSON

`dcf-telemetry.sh` (DCF-WatchDawg; a shell script that runs as root) writes a
`status.json` into a web-served volume. Its load average, memory percentage,
byte counters, uptime and tunnel count are read from `/proc`, `/sys` and the
output of `free`, `ip` and `nft`, and **interpolated into number positions**,
where JSON has no quotes to contain them. A reading of `0.42,"injected":true` is
itself valid JSON and adds a field to the published document; `007` or `nan`
makes the document invalid and the dashboard goes blank.

This unit states, once, which spellings may go into a number position. Pure --
no `poscit`, no allocation, no I/O -- and total: nothing a caller passes traps,
which matters because a trap reaches `exsrt_abortus` and ends the host process.

| function | question | verdicts |
|---|---|---|
| `admitte_numerum(b, n)` | is `b[0..n)` a JSON integer of 1..20 digits? (`0` or `[1-9][0-9]*`) | 0 yes; 1 empty; 2 over 20 bytes; 3 byte outside `[0-9]`; 4 leading zero |
| `admitte_onus(b, n)` | is it a load average: an integer part (`0` or `[1-9][0-9]{0,5}`), optionally `.` and 1..6 digits? | 0 yes; 1 empty; 2 over 13 bytes; 3 byte outside `[0-9.]`; 4 no digit before the `.`; 5 leading zero in the integer part; 6 more than six integer digits; 7 the `.` is not followed by 1..6 digits |

The verdict is the *first* failing check in scan order; the tables are in the
header comment of `watchdawg_gate.exsc`. 20 digits is `uint64_t`'s length
(18446744073709551615 has 20); the unit does not check the value against 2^64,
because JSON has no such limit and the property wanted is "safe to paste".

`watchdawg_gate.h` is the hand-written C face. `b` is a buffer of exactly 20
bytes (`admitte_numerum`) or 16 (`admitte_onus`), all readable; the host copies
its input into a zero-padded buffer of that size. A length past the longest
admitted text is answered (verdict 2) before any byte is read.

## What was run

`proba_c.sh` (needs `build/exsc`; no `fasmg`; python3):

- emission of the C unit, C face and Rust face twice, byte-identical, and the
  hand-written header compared prototype by prototype with the generated one;
- 696,610 cases through gcc and clang at `-O0` and `-O2` under UBSan, and
  through one gcc `-O1` ASan+UBSan build whose input buffers are exactly the
  declared size, with **0 disagreements** against the oracle. "Admitted" is
  checked against three independent definitions that must agree with each other
  first: a regex, a hand-written reader, and JSON's own parser (Python's
  `json`). The refusal codes are checked against a plain left-to-right reading
  of the tables;
- the cases: every string over `0159.` up to 7 bytes, every string over
  `0123456789./:@a -\n,eE+` up to 4, every digit-run length 0..25 with and
  without a leading zero, every integer/fraction digit count 0..8, a hostile
  corpus (a forged field after a number, `nan`, `inf`, `1e5`, `0x1f`, leading
  zeros, unicode digits, a newline, a NUL, `18446744073709551616`), a seeded
  6,000-mutant fuzz, lying lengths (`n` from `cap-1` to `2^64-1`) and the
  no-trap sweep (every length `0..cap+5`);
- 16 behaviour mutants of the source, each of which must **build** and then
  **fail** the oracle (none survived the first corpus);
- the rule-6 capability checks: an ambient `ambitus`/`archivum` is refused
  `EXS-E0421`, and a `Scriptor` parameter breaks both headers.

## What was not

- No Rust consumer: the Rust face is emitted and compared twice but never
  linked here (the only consumer is a C host, `dcf-gate` in DCF-WatchDawg, which
  vendors the emitted C).
- The value of a 20-digit integer is not range-checked (see above).
- The `tutela` setjmp guard (`examples/abortus/`) keeps its chain in
  `_Thread_local` storage (`tutela.c`, `summa` and `profunditas`), and
  `examples/abortus/proba_c.sh` includes eight threads trapping concurrently.
  This unit does not need the guard, because it does not trap (the no-trap
  sweep in `proba.py` is the evidence), and its only consumer, the `dcf-gate`
  CLI, is single-threaded and exits on a trap.
