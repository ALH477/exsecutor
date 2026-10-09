# dcf_net_gate -- network text that two programs must agree on

DCF-ID (Rust) records a client address in `users.last_ip`. DCF-WatchDawg (a
shell daemon running as root with `NET_ADMIN`) interpolates that column into
an `nft -f -` batch. `nft` parses the whole batch as one string, so a spelling
that is not exactly an IPv4 address is either a rejected batch -- every user's
whitelist stops updating -- or, in a position nft reads as syntax, a rule.
The two programs used to disagree about what an address is: the watchdog's
bash accepted `1.2.3.08` (arithmetic on `08` errors, and the error was read as
"not above 255"), while libc and nft refuse it.

This unit states it once. Pure -- no `poscit`, no allocation, no I/O -- and
total: nothing a caller passes traps, which matters because a trap reaches
`exsrt_abortus` and ends the host process.

| function | question | verdicts |
|---|---|---|
| `admitte_ipv4(b, n)` | is `b[0..n)` a canonical dotted quad? | 0 yes; 1 empty; 2 over 15 bytes; 3 byte outside `[0-9.]`; 4 not four octets; 5 octet over 3 digits; 6 leading zero; 7 octet over 255 |
| `ordo_ipv4(b, n)` | what kind of address? | 255 not admitted; else 0 global, 1 `0/8`, 2 loopback, 3 link-local, 4 multicast, 5 reserved/broadcast, 6 private, 7 shared `100.64/10` |
| `admitte_portum(b, n)` | a port, 1..=65535, no leading zero | 0 yes; 1 empty; 2 over 5 bytes; 3 non-digit; 4 leading zero; 5 zero or too large |
| `admitte_intervallum(b, n)` | a sync interval, 1..=3600 seconds | same as the port |

Whether a class is *acceptable* (is a private address whitelistable?) is the
consumer's policy, not this unit's; it only says what the address is.

`dcf_net_gate.h` is the hand-written C face. `b` is a buffer of exactly 16
bytes (addresses) or 8 (numbers), all readable; the host copies its input into
a zero-padded buffer of that size. A length past the capacity is answered
(verdict 2) before any byte is read.

## What was run

`proba_c.sh` (needs `build/exsc`; no `fasmg`; python3; cargo for the Rust step):

- emission of the C unit, C face and Rust face twice, byte-identical, and the
  hand-written header compared prototype by prototype with the generated one;
- 1,042,480 cases through gcc and clang at `-O0` and `-O2` under UBSan, and
  through one gcc `-O1` ASan+UBSan build whose input buffers are exactly the
  declared size, with **0 disagreements** against the oracle. "Admitted" is
  checked against three independent definitions that must agree with each other
  first: a regex, Python's `ipaddress`, and libc's `inet_pton`. The refusal
  codes are checked against a plain left-to-right reading of the table above;
- the cases: every string over `0159.` up to 7 bytes, every string over
  `0123456789./:@a -\n` up to 4, every sequence of up to 6 octet tokens from
  `{0,1,9,255,256,08,999}`, every octet value 0..299 in every position, every
  port 0..70009, every interval 0..4009, a hostile corpus (`1.2.3.08`,
  `0x7f.0.0.1`, `127.1`, trailing newline, NUL, unicode digits, `1.2.3.4/32`,
  `1.2.3.4 accept`, ...), a seeded 3,000-mutant fuzz, lying lengths (`n` from
  `cap-1` to `2^64-1`) and the no-trap sweep (every length `0..cap+5`);
- 14 behaviour mutants of the source, each of which must **build** and then
  **fail** the oracle; the first run of this suite let one through (removing
  the early too-many-dots check changes only the verdict of inputs like
  `1.2.3.4.999`, which the first corpus did not contain) and the structured
  octet-sequence cases were added because of it;
- the rule-6 capability checks: an ambient `ambitus`/`archivum` is refused
  `EXS-E0421`, and a `Scriptor` parameter breaks both headers;
- the Rust face: the generated extern block links the unit and answers.

## What was not

- No consumer is wired to it by this directory. DCF-ID and DCF-WatchDawg each
  vendor the C this emits; their own repositories say what they ran.
- `[UNTESTED]` against `nft` itself: that nft accepts everything this admits
  and rejects `1.2.3.08` is argued from libc's `inet_pton` and nft's lexer, not
  measured here (no `CAP_NET_ADMIN` check was made).
- `[OPEN]` whether the `tutela` setjmp guard (`examples/abortus/`) is safe to
  use from several threads; this unit does not need it, because it does not trap.
