# dcfid_gate -- untrusted text at DCF-ID's edges

DCF-ID (a Rust identity and billing service) takes text from strangers: a
username, a session cookie, an access token, the `Stripe-Signature` header, a
checkout amount. That text goes into SQL parameters, Redis keys, log lines and
-- through `users.last_ip` -- a root daemon's firewall batch. DCF-ID's own
checks were per call site and Unicode-aware: `char::is_alphanumeric` admits
Cyrillic and Arabic-Indic digits, so `admin` and a look-alike are two
accounts. This unit states what each of those strings is once, ASCII only, and
DCF-ID calls it before the text reaches anything else.

Pure -- no `poscit`, no allocation, no I/O -- and total: nothing a caller
passes traps, which matters because a trap reaches `exsrt_abortus` and ends the
host process. It is built together with the shared network gate:

    exsc aedifica --hospes x86_64-linux --emitte c \
        examples/dcf_net_gate/dcf_net_gate.exsc examples/dcfid_gate/dcfid_gate.exsc -o dcfid_gate.gen.c

| function | question | verdicts |
|---|---|---|
| `admitte_nomen(b, n)` | is `b[0..n)` a username: 3..=32 bytes of `[A-Za-z0-9_-]`? | 0 yes; 1 empty; 2 over 32 bytes; 3 under 3 bytes; 4 byte outside the set (every byte >= 0x80, NUL and newline included) |
| `admitte_signum(b, n, genus)` | genus 0: a session id, exactly 64 of `[A-Za-z0-9]`; genus 1: an access token or OAuth state, exactly 32 | 0 yes; 1 genus is neither 0 nor 1; 2 wrong length (empty and over-long included); 3 byte outside `[A-Za-z0-9]` |
| `admitte_summam(c)` | is `c` cents in 250..=10000 ($2.50..=$100)? | 0 yes; 1 below; 2 above |
| `admitte_formam_signaturae(b, n)` | is `b[0..n)` the *shape* of a `Stripe-Signature` header? | 0 yes; 1 empty; 2 over 512 bytes; 3 item not `t=` / `v1=` / `v0=` (an empty item, a header that stops inside a prefix); 4 bad `t` payload (empty, over 12 digits, non-digit); 5 bad `v` payload (not exactly 64 of `[0-9a-f]`); 6 a ninth item; 7 a second `t`; 8 no `t`; 9 no `v1` |

The header shape is `t=<1..12 digits>` and one or more `v1=<64 lowercase hex>`
items, in any order, at most 8 items, `,`-separated, no other byte. It also
admits `v0=<64 lowercase hex>` items, which is **not** in the task statement
that asked for this gate: Stripe documents a `v0` signature in test-mode
headers, and a gate that refused them would refuse every test-mode webhook.
`v0` is admitted and the Rust side ignores it; it does not count towards the
required `v1`. `[UNTESTED]` against a real Stripe header -- there is no
network here -- and argued from Stripe's documentation of the header, not
measured. The gate judges the shape only; it does not parse the timestamp and it
does not decide whether the signature is true -- the Rust side parses the values
after this admits the header, and the HMAC (kept in `hmac`/`sha2`; nothing
cryptographic is reimplemented here) decides.

Items are judged left to right, each in full before the next. 6 fires on the
eighth `,`; 7 fires at the `=` of the second `t`, before its payload is read;
8 and 9 are the end-of-input checks. The first failing check is the verdict.

`dcfid_gate.h` is the hand-written C face. `b` is a buffer of exactly 32
(usernames), 64 (session ids, tokens) or 512 (signature headers) bytes, all
readable; the host copies its input into a zero-padded buffer of that size. A
length past the capacity is answered (verdict 2) before any byte is read.

## Anchors

Every row is a fact the gate must answer. `proba.py` parses this table and
checks each row against the reference *and* against the gate built from the
source; DCF-ID vendors a copy of the table (`gate/ANCHORS.md`) and its
`cargo test --lib` asserts that its safe wrapper gives the same verdicts. The
input is written with `\xNN` for a byte, `{c*N}` for N copies of `c`, `\\` for
a backslash; `n` is `=` for the byte count, else the length handed to the gate
(a lying length). For `summam` the input is the decimal number of cents.

<!-- anchors:begin -->
| fn | input | n | verdict |
|---|---|---|---|
| nomen | `alice` | = | 0 |
| nomen | `bob` | = | 0 |
| nomen | `a_b-C9` | = | 0 |
| nomen | `ab` | = | 3 |
| nomen | `a` | 0 | 1 |
| nomen | `{a*32}` | = | 0 |
| nomen | `{a*33}` | = | 2 |
| nomen | `{a*3}` | 4294967296 | 2 |
| nomen | `al ice` | = | 4 |
| nomen | `al\x00ice` | = | 4 |
| nomen | `alice\x0a` | = | 4 |
| nomen | `\xd0\xb0dmin` | = | 4 |
| nomen | `adm\xc3\xafn` | = | 4 |
| nomen | `a.b` | = | 4 |
| nomen | `a@b` | = | 4 |
| nomen | `a/b` | = | 4 |
| nomen | `{a*30}\xc3\xa9` | = | 4 |
| nomen | `---` | = | 0 |
| nomen | `{a*3}` | 18446744073709551615 | 2 |
| signum:0 | `{A*64}` | = | 0 |
| signum:0 | `{a*63}` | = | 2 |
| signum:0 | `{a*65}` | = | 2 |
| signum:0 | `{a*63}-` | = | 3 |
| signum:0 | `{a*63}\x00` | = | 3 |
| signum:0 | `a` | 0 | 2 |
| signum:0 | `{a*32}` | = | 2 |
| signum:0 | `{0*32}{9*32}` | = | 0 |
| signum:0 | `{a*3}` | 18446744073709551615 | 2 |
| signum:1 | `{Z*32}` | = | 0 |
| signum:1 | `{Z*31}` | = | 2 |
| signum:1 | `{Z*33}` | = | 2 |
| signum:1 | `{Z*31}_` | = | 3 |
| signum:1 | `{Z*64}` | = | 2 |
| signum:1 | `{Z*31}\xc3\xa9` | = | 2 |
| signum:1 | `{Z*30}\xc3\xa9` | = | 3 |
| signum:2 | `{a*32}` | = | 1 |
| signum:255 | `{a*64}` | = | 1 |
| signum:3 | `{a*64}` | = | 1 |
| summam | `0` | = | 1 |
| summam | `100` | = | 1 |
| summam | `249` | = | 1 |
| summam | `250` | = | 0 |
| summam | `500` | = | 0 |
| summam | `9999` | = | 0 |
| summam | `10000` | = | 0 |
| summam | `10001` | = | 2 |
| summam | `4294967296` | = | 2 |
| summam | `18446744073709551615` | = | 2 |
| forma | `t=1492774577,v1={a*64}` | = | 0 |
| forma | `t=1492774577,v1={a*64},v0={b*64}` | = | 0 |
| forma | `v1={a*64},t=1` | = | 0 |
| forma | `t=1,v1={0*64},v1={f*64}` | = | 0 |
| forma | `t=123456789012,v1={a*64}` | = | 0 |
| forma | `t=1,v1={a*64},v1={a*64},v1={a*64},v1={a*64},v1={a*64},v1={a*64},v1={a*64}` | = | 0 |
| forma | `t=1,v1={a*64},v1={a*64},v1={a*64},v1={a*64},v1={a*64},v1={a*64},v1={a*64},x` | = | 6 |
| forma | `t` | 0 | 1 |
| forma | `{t*513}` | = | 2 |
| forma | `t=1` | = | 9 |
| forma | `v1={a*64}` | = | 8 |
| forma | `t=1,v0={a*64}` | = | 9 |
| forma | `t=1,v1={a*63}` | = | 5 |
| forma | `t=1,v1={a*65}` | = | 5 |
| forma | `t=1,v1={A*64}` | = | 5 |
| forma | `t=1,v1={a*64}\x00` | = | 5 |
| forma | `t=1,v0={a*10}` | = | 5 |
| forma | `t=,v1={a*64}` | = | 4 |
| forma | `t=1234567890123,v1={a*64}` | = | 4 |
| forma | `t=12x,v1={a*64}` | = | 4 |
| forma | `t=1\x0a,v1={a*64}` | = | 4 |
| forma | `t=-1,v1={a*64}` | = | 4 |
| forma | `t=1.5,v1={a*64}` | = | 4 |
| forma | `t=1, v1={a*64}` | = | 3 |
| forma | `t=1,,v1={a*64}` | = | 3 |
| forma | `,t=1,v1={a*64}` | = | 3 |
| forma | `t=1,v1={a*64},` | = | 3 |
| forma | `t=1,v2={a*64}` | = | 3 |
| forma | `x=1` | = | 3 |
| forma | `T=1,v1={a*64}` | = | 3 |
| forma | `t=1,v1` | = | 3 |
| forma | `t=1,v` | = | 3 |
| forma | `t=1;v1={a*64}` | = | 4 |
| forma | `t=1,t=2,v1={a*64}` | = | 7 |
| forma | `t=1,v1={a*64},t=2` | = | 7 |
<!-- anchors:end -->

## What was run

`proba_c.sh` (needs `build/exsc`; no `fasmg`; python3; cargo for the Rust step):

- emission of the C unit, C face and Rust face twice, byte-identical, and the
  hand-written header compared prototype by prototype with the generated one;
- the corpus through gcc and clang at `-O0` and `-O2` under UBSan, and through
  one gcc `-O1` ASan+UBSan build whose input buffers are exactly the declared
  size, with **0 disagreements** against the oracle. Last run: 297,812 cases
  per configuration (23,141 admitted, 274,671 refused), 42 mutants, all caught;
  the script prints these numbers, so they are re-measured, not trusted from here.
  "Admitted" is checked against an independent regular expression per gate that
  must agree with the left-to-right reference first; the refusal codes are
  checked against the reference;
- the cases: every string over small alphabets including the bytes on either
  side of every class (`/ : @ [ \` { 0x7f 0x80 0xff`), every byte value in
  every position of a valid 3- and 32-byte username and of a valid 32- and
  64-byte secret, look-alikes (Cyrillic, fullwidth, Arabic-Indic digits, zero-
  width, bidi override), every amount 0..10119 and the 2^31/2^32/2^63/2^64
  boundaries, every sequence of up to three items from 20 well- and
  ill-formed header items (four items from the first 14), every header of 0..12 items with the `t` in every
  position, the eight/nine item boundary with a malformed ninth, the shapes a
  proxy damages (space after a comma, CRLF, upper case, `;` for `,`), a seeded
  mutation fuzz of each gate, lying lengths (`n` from `cap-1` to `2^64-1`) and
  the no-trap sweep (every length `0..cap+5` over random contents);
- every verdict code of every table must occur in the corpus (a vacuous pass
  would be reported), and every README anchor row is checked against both the
  reference and the gate;
- behaviour mutants of the source, each of which must **build** and then
  **fail** the oracle (the script prints each; a mutant that survives means the
  corpus is too thin -- strengthen the corpus, do not delete the mutant);
- the rule-6 capability checks: an ambient `ambitus`/`archivum` is refused
  `EXS-E0421`, and a `Scriptor` parameter breaks both headers;
- the Rust face: the generated extern block links the unit and answers.

## What was not

- `[UNTESTED]` against a real Stripe `Stripe-Signature` header and against
  Stripe's own v0 spelling (see above); the shape comes from Stripe's
  documentation.
- `[OPEN]` whether the 8-item ceiling is right for every Stripe account: a
  header carries one `t`, one `v1` per active signing secret and, in test mode,
  a `v0`. Rolling a secret with several concurrent `v1`s beyond seven would be
  refused (fail closed).
- No policy lives here: that a username is not also a reserved word, or that a
  case-folded duplicate is refused, is the consumer's rule (DCF-ID enforces the
  second in SQL).
- The thread-safety of the `tutela` trap guard is answered in DCF-ID's
  `gate/PROVENANCE.md`; this unit does not trap, so it is only defence in depth.
