# The DeModFrame wire codec — design for §14 entry 23

Status: **implemented, M2 through M7; §14 entry 23 runs.** Section 11
records what M7 ran and what it found. The status this line first carried
-- "design only; nothing implemented. Nothing here has lexed, parsed,
checked, lowered or run" -- was true when it was written and stopped being
true milestone by milestone. Every `[UNTESTED]` this file carried has been
retired against a named, merged test (section 10 lists each); what remains
marked is `[OPEN]` and is open on purpose: `&T:o`, `:o` on a non-field
place, nested `@transitus` structs, signed shifts, bitwise and/or, a code for
a signed explicitly-ordered field, and narrowing `sicut`'s spec text. `spec §N` cites
`docs/spec/exsecutor-spec-v0.4.md` as amended in the same commit as this
file; `IR n.m` cites `docs/design/ssa-ir.md`; `CHK n.m`
`docs/design/checker.md`; `LOW n.m` `docs/design/lowering.md`; ADR 0011 is
`docs/decisions/0011-wire-format-demodframe.md`. The reference is HydraMesh
(`python/MCP/wirelab_core.py`, normative; `go/dcf/frame.go`, a clean port;
`python/MCP/verify_laws.py`, which generates the certificate) and the
vendored `vendor/hydramesh-wire/{WIRE_QUANTUM_SPEC.md,golden_vectors.json}`.

## 1. What is being built, and the constraint that shapes it

§14 entry 23 is the one conformance entry that is an **external**
certificate: 109 encode-basis and 137 syndrome-basis vectors, and under the
certificate's own theorem an implementation that is bit placement plus a CRC
— hence affine over GF(2) — and matches all 246 agrees with the reference on
every one of 2^108 frames and classifies every one of 2^136 words. The
fixture `tests/conformance/entry23_demodframe_golden_vectors.exsc` holds
§5.2's declaration verbatim and is `status=run` since M7 (it was
`status=deferred needs=backend,wire_codec` when this was written).

The work was to make it run **with the codec written in Exsecutor**. The
constraint was that the language could not yet express the codec. The
survey that preceded this document (recorded in the plan, repeated here so
the doc stands alone) found, at M0 — every row since closed:

| gap | where |
|---|---|
| no xor or shift operator | spec §8.4/§8.6 `[OPEN]` |
| no hex literal | spec §8.4 `[OPEN]`; `lexer/lex.inc:335` rejects `0x10` as `EXS-E0210` |
| no struct or array literal, so no program can create a `DeModFrame` | spec §8.6 `[OPEN]`, position reserved |
| byte order is part of type identity, so `u16:maior` ≠ `u16` and every field access is `EXS-E0303` | `checker/types/sig.inc:251` |
| the emitter aborts on any phi, so every loop dies | `backend_fasmg/emit.inc:2165` |
| arithmetic, compare, load, store are 64-bit only | `emit.inc:189-205` |
| no emission for `xor shl shr zext trunc loadbits storebits index chk copy` | `emit.inc` dispatcher |
| field access traps on sub-byte fields and hardcodes `nativus` | `lower/expr.inc:2046`, `lower/cfg.inc:168,191` |
| `acies` is unsized | `lower/ty.inc:275` |
| constants ≥ 2^31 do not assemble (`mov qword [slot], imm` is simm32) | `emit.inc:936` |
| aggregate `sicut` recurses forever | `lower/expr.inc:1897-1926` |

The IR already defines every opcode needed (IR 2.3, `backend_fasmg/ir.inc`);
the gap is emission and the language above it.

**The design result worth keeping.** `@transitus` field access *is* the
mask, shift and byte-swap machinery. Reading `f.versio` is the `movzx; shr
4; and 0x0F`; reading `f.numerus` is the big-endian 16-bit load; writing
`f.cursus` is the big-endian store. Once that is true, the codec needs only
exclusive or, shifts by a constant, loops, narrow integers and a byte view of
the struct. Bitwise **and** and **or** were needed nowhere — not in the CRC,
not in the encode, not in the decode, not in the driver — so they stay
`[OPEN]` in spec §8.6, with this codec as the evidence that a real wire
format did not need them. That is the opposite of the usual result, where a
codec is the argument *for* a bitwise operator set, and it is the point of
writing the format's layout into the type.

## 2. Decisions

No new §13 code. Every error below reuses an existing one; section 4 checks
each against the registry.

### D1 Operators: `aut`, `sursum`, `deorsum`

Three contextual words, reserving nothing, on spec §8.6's rule that operator
position is never operand position — the same rule that lets `lt` and `et`
stay identifiers everywhere else.

- **`aut` is exclusive or.** Latin's exclusive "or", paired against the
  inclusive `vel` the language already uses for logical disjunction. It is
  **strict**: both operands are evaluated, left first; there is no
  short-circuit because there is nothing to short-circuit — the result
  depends on both operands always.
- **`sursum` / `deorsum` shift toward more / less significance.** Not
  "left" and "right", deliberately: byte order lives in the type (§5.2), and
  a shift named by a direction on paper would invite the question of which
  paper. Significance is order-free.
- **Precedence**: level 5a shift, non-associative; level 5b `aut`,
  left-associative; both between additive (5) and range (6). The letters are
  so that the level numbers already cited in code comments
  (`cst/parse.inc`, `checker/types/types.inc`) do not move. Chaining a shift
  is `EXS-E0201` at the second word by the same mechanism that rejects
  `a lt b lt c`: the production takes one operator, and the next word is
  then an unexpected token where `;` or a lower-level operator was required.
- **Typing**: unsigned integer operands only — `iN`, floats, anything else is
  `EXS-E0305`. Both operands the **same type, including the shift count**,
  else `EXS-E0303`. The result has that type. A pending literal on either
  side takes the other side's type, as the checker already does for `+`
  (`types.inc:57`), so `c sursum 1` with `c: u16` types the `1` as `u16`.
- **Semantics**: bits moved beyond the width are **discarded** — that is the
  definition of the operation, not an overflow, so there is no trapping or
  wrapping variant. A count ≥ width **traps**, like `+` on overflow: the
  hardware's answer to an out-of-range count is target-dependent (x86 masks
  the count), and a target-dependent answer is the ambient state §1 exists
  to remove.

Rejected: symbolic tokens (`^`, `<<`, `>>`). §8.6 decision 6 already makes
`<` never an operator so that `acies<acies<f32, 4>, 4>` lexes as two `>`
tokens; `<<` and `>>` would undo that. `^` is free but a third sigil
convention (arithmetic is punctuation, comparison is words, then xor is
punctuation again) reads worse than one more contextual word.

**Retired by M5 and M3.** Parsed: `tests/unit/cst_shift_xor.asm`
(precedence as five dump lines, the chained shift `EXS-E0201` at the second
word with its parenthesised twin accepted, the words as names, `autem` is
not `aut`). Typed: `tests/unit/chk_ty_bitops.asm`, seventeen rows, each
rule with a rejected row and an accepted twin. Lowered:
`tests/unit/lwr_bitops.asm` (`xor u16`, `shl u16`, `shr u16`). Run:
`tests/ir/bitwise.ir`, `shift_narrow.ir`, `trap_shl_u8.ir` (count 8 in
`u8` aborts), `trap_shr_u32.ir`; `tests/programs/redundantia/` — CRC of
`"123456789"` is `0x29B1`, of fifteen zero bytes `0x4EC3`, exit 0.

Three things the implementer decided where the text above was silent, now
in spec §5.4 and pinned by `chk_ty_bitops.asm`:

- each operand is judged on its own, so `i8 sursum i8` is **two**
  `EXS-E0305`, one per operand, and a pending literal is never blamed;
- when neither side is wrong alone — `1 aut 2` where an `i8` is expected —
  the expectation is judged, and the one `EXS-E0305` is at the operator;
- `mensura` is admitted as an unsigned operand (§5.2 calls it `usize`) and
  keeps its own type, so `mensura aut u64` is `EXS-E0303` and
  `mensura deorsum 1` is fine.

### D2 Byte order belongs to places, not values

A field declared `T:o` is a **place** whose bytes lie in order `o`. A read
from it has type `T`; a write to it accepts `T`. The conversion *is* the
field access. `f.numerus` on a `DeModFrame` is the big-endian load and yields
a `u16`; `f.numerus = n` with `n: u16` is the big-endian store. The
declaration's type is still `u16:maior` — `sig.inc:251`'s "different ids"
stays true for the declaration, which is what makes `EXS-E0321` and the
layout check possible — but the *expression* `f.numerus` is typed `u16` by
`checker/types/member.inc`.

Reason: the alternative — an explicit `sicut` at every access, `f.numerus
sicut u16` — buries the codec in casts that carry no information the field's
declaration does not already carry, and makes the ordinary spelling the
wrong one (a `u16:maior` value in a register means nothing: a register has
no byte order).

This also answers ADR 0011's open question on the in-memory form of `u24`:
a read of `tempus: u24:maior` yields a `u24` value, an integer in
[0, 2^24), held however D5 says; it is exactly three bytes on the wire and
exactly a `u24` in a program. Whether `:o` on a non-field place (a binding,
a parameter) means anything is not decided here; nothing in the codec writes
one. `[OPEN]`

**`&` of a `@transitus` field is `EXS-E0305`.** The review asked what
`&f.numerus` is: prefix `&` exists and `&u16:maior` is a legal type, so the
sentence "no expression has an ordered type" was false as written. The
smaller commitment consistent with D2 is to refuse it — a sub-byte field
has no address at all, and an ordered field's address would be a value
carrying an order — and say "no *value* has one". Take `&` of the struct or
use D4's byte view. Admitting `&T:o` later, with dereference as a place
read in that order, is `[OPEN]` and would be an extension, not a reversal.

**Retired by M6 and M4.** `tests/unit/chk_ty_transitus.asm` (a `u16:maior`
field reads as `u16` and is written with a `u16`; `&w.n`, `&w.h` are
`EXS-E0305`, `&w` is not); `tests/unit/lwr_transitus.asm` (the three
orders as `load`/`store … maior|minor|nativus`, `loadbits 1 0` / `1 4`,
`store u16 %p 15 maior`); `tests/ir/byte_order.ir` and `bits.ir` (the
emitter, every order at every whole-byte width, both nibbles);
`tests/programs/forma/` (build the example frame from a literal, take its
CRC through the byte view, decode it back — exit 0); §14 entry 23. The
`u24` answer for ADR 0011 is read back in `forma` and in
`tests/ir/demodframe_decode.ir`. The two `[OPEN]`s above stay open.

### D3 Struct literals

- **Syntax**: `Path '{' [IDENT ':' Expr (',' IDENT ':' Expr)*] '}'`. Every
  field of the `structura` exactly once, in any order; commas between; no
  trailing comma (as `ParamList` and every other list in §8.6). The suffix
  attaches only directly after a path segment, in the position §8.6 reserved
  for it, and is **disabled in `ExprNS`** — so `si x {` is a condition and a
  block, never a literal, and a literal in a `si`/`dum`/`discerne` head is
  parenthesised. `cst/parse.inc:3072` already has the `ExprNS` entry.
- **Errors**: a field name the struct does not declare, `EXS-E0301`; a field
  named twice, `EXS-E0302`; a field left out, `EXS-E0304`. A `Path` that
  resolves to something that is not a `structura` is `EXS-E0305` — not in
  the plan's list of three, recorded here because the checker will need it.
- **Evaluation** is in source order; **stores** are in declaration order.
  So a `@transitus` literal writes bytes 0..N−1 in order whatever order the
  source named the fields, and the emitted code is a function of the
  declaration, not of the spelling.

Rejected: zero-filling. A `structura` binding declared without a literal and
read before assignment is `EXS-E0307` (definite assignment, §13). Filling
the gap with zeros would change what `EXS-E0307` means and, in a
`@transitus` type, would silently put bytes on the wire that no source line
wrote — the disclosure class `@transitus` exists to close, reintroduced at
the literal.

**Retired by M6.** Parsed: `tests/unit/cst_structlit.asm` (ten rows: the
literal counted per tree, all seven `ExprNS` positions, trailing and
missing commas `EXS-E0201`, no literal after `f(1)`). Typed:
`tests/unit/chk_ty_structlit.asm` (eighteen rows: `E0301`/`E0302`/`E0304`/
`E0303`/`E0308`/`E0305` each with a twin; the D2 literal `0x1234` an
unordered `u16`). Lowered: `tests/unit/lwr_transitus.asm` (a reversed
literal: source-order `iconst`s, declaration-order stores). Run:
`tests/programs/forma/`, and `vacuum()` in entry 23.

Two things the implementer decided where the text above was silent, now in
spec §8.6: inside an `ExprNS` position the literal is **re-admitted** by
`( )`, `[ ]`, a call's arguments, a literal's own braces and the statements
of any nested block — the rule Rust uses; `cst_structlit.asm` rows 6–8 pin
`si (S { a: 1 }).a eq 1 {`. And `EXS-E0304` for missing fields is reported
**once per literal**, not once per field; two different faults in one
literal are both reported, in source order.

### D4 The one aggregate `sicut`

`sicut` is admitted between a `@transitus` struct `S` and `acies<u8, N>`
where `N` is `S`'s declared size, in **both** directions, **on the explicit
condition that every field of `S` is an unsigned integer** (`u1`–`u7`, or a
whole-byte width with its order). Nothing else aggregate casts: any other
aggregate `sicut`, including one whose `N` is not `S`'s size or whose `S`
fails the field condition, is `EXS-E0305`, which is `types.inc:305`'s
existing class-E answer to "are these two related". The size equality is
checked **in pass 2, by the cast itself** (`checker/types/member.inc`,
`__chk_ty_wirebytes`): a packed `@transitus` size is the sum of the
declared widths, which the type checker already has, and pass 4 runs after
the bodies are typed — so waiting for it, as the first draft of this
paragraph said, would have been waiting for nothing (M6, finding 5 of its
commit). Spec §5.2 says the same.

**The field condition is the load-bearing part, and §5.2 had not stated
it.** §5.2 said a field *may* declare any admitted `uN` and never said a
field must be one; `checker/rows/layout.inc:264` lays out an aggregate
field it is handed. Had a `@transitus` struct been allowed to hold a
`refero`, a `textus` or a capability-bearing struct, `acies<u8, N> sicut S`
would forge one from bytes — the cast would be the disclosure class in
reverse. §5.2 now states the restriction (integers only; no nested
`@transitus` structs, which is `[OPEN]` rather than decided against, since
admitting them makes this condition recursive and needs rule 2's byte
alignment for the nested struct), and the cast checks it **itself** rather
than trusting the layout pass. Enforcement of the restriction on the
declaration reuses existing codes: an unannotated non-integer field is
`:nativus` by rule 3 and is `EXS-E0321`; an order annotation on a
non-integer type (`textus:maior`) is `EXS-E0309`, an annotation not
applicable to the type. Both are pinned by
`tests/unit/chk_row_layout_kind.asm` (commit 622a541: `textus`,
`acies<u8, 4>`, `refero<u8>`, a nested struct and a capability-bearing
type each `E0321` unannotated; `textus:maior` `E0309`) — a fixture that
exists because the width-0 exemption in `layout.inc` had let
`@transitus structura T { t: textus }` check clean, which `exsc` confirmed
before the fix. Two gaps that fixture recorded rather than closed, now
also in spec §5.2: a **signed** field with an explicit order
(`i32:maior`) has no code assigned — the enforcement text distinguishes
integer from non-integer and never revisits signedness — so it checks
clean, pinned as a zero-diagnostic row, `[OPEN]`; and `textus:maior` in
real source draws **two** diagnostics, `EXS-E0309` from pass 2 and
`EXS-E0321` from pass 4 (measured: `exsc aedifica --diagnostica json` on
exactly that field), which §8.3 neither forbids nor addresses — it states
no cascade discipline — so whether to suppress the second is `[OPEN]`.

Under that condition it is **total**, which is why it can be a cast and not
an `eventus`: a `@transitus` layout is packed (§5.2), every multi-byte field
carries an order, `:nativus` is rejected, there is no padding, and every
field is an unsigned integer of the declared width — so every byte pattern
of length `N` is a valid `S`, and every `S` is exactly `N` bytes. Both
directions are bijections on bytes.

**The result is a value, not a place.** It cannot be assigned through
(`(f sicut acies<u8, 17>)[0] = 1` is `EXS-E0306`), and binding it copies:
`firma b = f sicut acies<u8, 17>` has its own seventeen bytes, as binding
any aggregate value does, and a later write to `f` is not seen in `b`. The
first draft said "pointer passthrough … `w sicut DeModFrame` and `w`
alias", which left aliasing observable and undefined. A backend may still
implement it as a passthrough — the same bytes, no copy — exactly where no
write can observe the difference; that is an implementation, and ADR 0012's
differential test is what keeps it one. The codec never writes through a
cast: `syndroma` and `lege` read through `w sicut DeModFrame`, section 2 of
the stream reads through `Syndroma { valor: y } sicut acies<u8, 2>`, and
the bit-flip loop writes to a `mutabilis` `acies` binding of its own.
`obsigna`'s `mutabilis g = f` relies on the same by-value rule for a
struct binding, which the spec text now carries.

Rejected: a method (`f.octeti()`) — it would need a prelude entry per struct
or a generic the prelude has no dictionary for; and an implicit view (an
`acies<u8, N>` parameter accepting an `S`) — §5.4's "no implicit promotion"
covers aggregates as much as integers.

**Retired by M6.** `tests/unit/chk_ty_transitus.asm` (both directions;
`N = 5`, `acies<u16, 2>`, a plain struct, two structs and the same struct
all `EXS-E0305`; a `textus` field and an `i8` field refused by the cast's
own check; `EXS-E0306` through a cast, `a[0] = 1` accepted on a bound
copy); `tests/unit/lwr_transitus.asm` (a cast read with no copy, a bound
cast as `slot 17 1` + `copy 17`, and mutants M20/M21 — a read that copies,
a binding that shares — each caught); `tests/programs/forma/`; entry 23.

### D5 Narrow integers have one canonical form

At the language level: a value of `uN` is an integer in [0, 2^N); the
trapping operators are exact on it, the wrapping operators are arithmetic
modulo 2^N, and D1's shifts discard. How it is held is the backend's: IR 2.2
as amended says `uN` is zero-extended in 64 bits, `iN` sign-extended, `u1`
is 0 or 1, and the emitter normalises after every wrapping or converting
operation with `shl 64−N` then `shr` (or `sar`). A trapping op computes in
64 bits, normalises, and traps if the normalised value differs from the raw
one — a complete rule for `add`/`sub` at every width below 64 and for `mul`
at widths up to 32, where the 64-bit product is exact. **It is not complete
for `mul` at widths 33–63**: in `u40`, 2^32 × 2^32 = 2^64 has raw 64-bit
result 0, which normalises to 0 and would not trap. So `mul` also traps on
the 64-bit `mul`/`imul` overflow flag, and at width 64 `add`/`sub`/`mul`
use the flags alone, as the emitter does today. The review found the gap
(M2); the first draft's "the carry flag deciding the 64-bit case" had
covered add/sub and not the product.

Reason: one canonical form means every instruction may assume its inputs
are canonical and the verifier has nothing to check; the alternative
(masking on read) puts the mask at every use instead of every definition,
and a use is more common than a definition.

**Retired by M3.** `tests/ir/`: `narrow_wrap.ir` (wrapping at
`u4 u8 u24 u32 i8`, and the trapping forms at the edge that must not
trap), `trap_add_{u4,u8,u24,u32,i8}.ir`, `trap_sub_u8.ir`,
`trap_mul_u8.ir`, `trap_mul_u40_2p64.ir` and `trap_mul_i40_2p64.ir` (the
flag-only case), `trap_mul_u40_below_2p64.ir` (the compare-only case),
`mul_u40.ir` (in-range products that must not trap), `iconst_wide.ir`
(`3735928559` and five more outside simm32), `trap_shl_u8.ir`,
`trap_shr_u32.ir`, `trap_chk.ir`, `conv_roundtrip.ir`, `cmp_signedness.ir`;
`tests/unit/bfa_emit_narrow.asm` and `bfa_emit_bitwise.asm` pin the emitted
text; `tests/programs/angusta/` runs it from source. M3 also corrected IR
2.2 (a `sext` into a `uN` normalises; a `zext` of an `iN` extends from the
source width first), which is in `ssa-ir.md` and stays.

### D6 Hex literals

`0x[0-9a-fA-F]+`, and only that. `0X` is `EXS-E0210` — one spelling of the
prefix, so a formatter has nothing to normalise and a reader nothing to
learn twice. `0x` with no digit is `EXS-E0210`. A hex literal that runs into
an identifier character (`0x1G`, `0xFFu`) is `EXS-E0210` by the rule
`lex.inc:335` already applies to decimal runs. The token class is `INT`; the
value is typed by expectation exactly as a decimal literal is
(`types.inc:57`), so a hex literal too wide for the type it lands in is
`EXS-E0308`, and one beyond 64 bits is `EXS-E0308` as `chk_ty_litval`
already answers for decimal. Binary and octal bases, digit separators and
floats stay `[OPEN]`.

**Retired by M5.** `tests/unit/lex_hex_literal.asm` (accepted: `0x0`,
`0xd3`, `0xDEADBEEF`, `0xffffffffffffffff`, exact kind and span, no
diagnostic; rejected: `0X10`, `0x`, `0x1G`, `0b101`, exactly `EXS-E0210`
with its span); `tests/unit/chk_ty_hexlit.asm` (the value, upper-case
digits folded, a seventeenth digit `EXS-E0308`); `tests/programs/redundantia/`
runs on `0x1021` and `0xffff`.

### D7 Byte output

One prelude call, `Scriptor.scribe_octeto(s, b: u8) -> mensura`, provisional
(`EXS_IFACE_F_APERTUM`) exactly as `scribe` is (`prelude/interface.inc:368`).
It sits inside the `ambitus` gate `Scriptor.ad_exitum` already enforces and
adds no syscall: it is `write(1)` of one byte, and `write` is on the closed
allowlist.

The name was first written `scribe_octetum`. Spec §3.1 makes the part after
`_` an ablative or a proper noun, as in `imprime_gutenbergio`, and `octetum` is
an accusative. So the name is `scribe_octeto`, "write by a byte". The
implementer found this while building the call. The name was changed before any
program depended on it.

Rejected: `scribe` of a `textus` built from the frame bytes — a `textus` is
UTF-8 (§5.1) and a frame is arbitrary bytes, so no such `textus` can exist;
and an `acies<u8, N>` writer — generic over `N`, which the prelude cannot be
(spec §7.1's dictionary does not exist in the prelude). One byte per call
costs 2,502 syscalls for the certificate stream; the audit counts kinds, not
calls, and the output bytes are the same.

**Retired by M7.** `tests/unit/prelude_scribe_octeto.asm` (`audit=pass`:
seven bytes including `0xff` and two with garbage above bit 7, each
returning 1 and moving the descriptor's offset by exactly one; a
descriptor of −1 returns 0); `tests/unit/prelude_sine_ambitus.asm` (with
`ambitus` at 0 none of the four `ambitus` routines is defined — the gate,
which nothing had measured before); `tests/programs/octeti/` (26 raw bytes
compared with `cmp`); entry 23's binary audits to `write` and `exit_group`
only.

### D8 Phi lowering: a parallel copy on each edge, through the stack

The Tier-1 emitter keeps every value in a stack slot and, until M2, aborted
on any phi (`emit.inc:2165` at M0). Phis are lowered as a **parallel copy on each incoming
edge**: push each phi's operand for that edge, in phi order; then pop into
each phi's slot in reverse order. All reads precede all writes, so a swap
(`%a = phi [%b …]; %b = phi [%a …]`) and the lost-copy case are correct with
no cycle analysis and no scratch register. A `jmp` into a block with phis
emits the copies before the jump. A `br` has two targets whose copies
differ, so each edge into a block with phis goes through a **per-edge
stub**: `jcc stub_T; jmp stub_F`, each stub doing its copies and jumping on.
When the target has no phi the emitted text **must be unchanged**, byte for
byte: the fixtures that pin emitted text are a requirement on M2, and a
diff in one of them is a defect in the change, not a fixture to update.

Rejected: Boissinot-style sequentialisation with one spare register —
shorter code and a real algorithm to get wrong, in a Tier-1 emitter whose
brief is correctness before size.

**Retired by M2.** `tests/ir/phi_sum_loop.ir`, `phi_swap.ir` (two phis
naming each other on the back edge; redesigned after its first form
passed under the forward-pop mutation), `phi_lost_copy.ir` (the only
fixture sensitive to the per-edge stub: every latch the lowering emits
today ends in `jmp`), `phi_same_target.ir` (a `br` with both targets one
block); `tests/unit/bfa_emit_phi.asm` (exact text of a `jmp` into phis, a
swapping self-loop through stub `e0`, a false edge through `e1`);
`tests/programs/phi_loops/` (seven checks, exit 77). The requirement above
held: `bfa_emit_tier1/tier2/program.asm` and the hello world through the
gate were byte-identical before and after.

## 3. Why field access is the whole bit-manipulation story

The CRC needs xor and a shift by one. Everything else the codec does is
place a value in a field or read one back:

| operation | in C | here |
|---|---|---|
| version nibble in | `(v & 0x0F) << 4 \| t` | `f.versio = v; f.genus = t;` — `storebits u4` at bit 0 and bit 4 |
| version nibble out | `b[1] >> 4` | `f.versio` — `loadbits u4 %p 1 0`, MSB-first (§5.2 rule 1) |
| 16-bit big-endian in | `b[2] = s >> 8; b[3] = s` | `f.numerus = s;` — `store u16 %p 2 maior` |
| 24-bit big-endian out | `b[12]<<16 \| b[13]<<8 \| b[14]` | `f.tempus` — `load u24 %p 12 maior`, zero-extended |
| the 15 bytes under the CRC | `b[:15]` | `f sicut acies<u8, 17>`, indexed `0..15` |
| the CRC in | `b[15] = c >> 8; b[16] = c` | `f.cursus = c;` |

Every mask and every byte swap the reference implementations write by hand
is a consequence of a width or an order that the declaration states once.
That is the sufficiency claim §5.2 made and ADR 0011 said needed a format
that was not written to fit; this table is what it costs to cash it. The
only thing left is the polynomial division in the CRC, which is xor and a
shift by a literal.

## 4. Error codes, checked against §13

Every code below exists in §13. For each, the registry text, the use here,
and the precedent that the use is in the code's class.

| code | §13 text | used here for | fit |
|---|---|---|---|
| `EXS-E0201` | unexpected token | a chained shift; `u4:maior`; a trailing comma in a literal | the parser's general code; a chained comparison is already this |
| `EXS-E0210` | malformed literal | `0X…`, `0x` with no digit, `0x1G` | the lexer's; `lex.inc:335` already gives `0x10` this code today |
| `EXS-E0301` | name does not resolve | a field name the struct does not declare | class A: the name does not resolve *in the struct's scope* |
| `EXS-E0302` | duplicate declaration in one scope | a field named twice in one literal | class B; precedent: `sig.inc:46` uses it for a second implementation of one interface on one type, which is likewise a second binding of one name where one is allowed, not a declaration in the grammatical sense. §8.3: text is not permanent, the class is |
| `EXS-E0303` | type mismatch | shift count not the left operand's type; `aut` operands of different types | class C |
| `EXS-E0304` | wrong number of arguments | a field left out of a literal | class D; precedent: `sig.inc:742` uses it for a wrong number of *generic* arguments, so "arguments" already reads as "things a form requires, counted". A literal is the struct's constructor and its field initialisers are its arguments |
| `EXS-E0305` | operation not defined on the type | `aut`/shift on a signed or non-integer operand; an aggregate `sicut` other than D4's pair; a literal whose path is not a `structura` | class E; `types.inc:305` already routes cast admissibility here |
| `EXS-E0308` | literal cannot be typed or does not fit its width | a hex literal too wide for its type, or beyond 64 bits | class H, `types.inc:63` and `sig.inc:829` |

**Finding on the mapping.** `E0302` and `E0304` are the two stretches, and
both hold for the same reason: each code is one *class* with a stable
meaning (§13's Stage 2 note), and both already cover a case that is not the
literal English — a second `interfacies X in T` and a wrong generic arity.
Nothing in the mapping needed a new code, which is the same weak evidence
ADR 0011 recorded once already: the registry was cut at the right joints.

Reason codes returned by `lege` (section 5) are **not** §13 codes and are not
diagnostics: they are `u8` values a program computes, exactly as IR 2.3's
`trap` kinds are not §13 codes.

## 5. The codec's shape

Sources, in the order the `TEST:` directive names them (the conformance tree
is `tests/conformance/`; `entry23/` is a subdirectory so the `*.exsc` glob
skips it):

1. `entry23_demodframe_golden_vectors.exsc` — the fixture itself, so the
   certified declaration is §5.2's, verbatim.
2. `entry23/codex.exsc` — the codec. **Pure**: no `poscit` anywhere, no
   `initium`, nothing that could reach a capability. §10.3's audit of it
   must show no capability, and that is part of "done".
3. `entry23/probatio.exsc` — the `initium` driver, the only file that
   touches `Mundus`.

The directive becomes `status=run sources=entry23/codex.exsc,entry23/probatio.exsc`.

`codex.exsc` provides four functions (names per §3: `redundantia` is the
CRC, `syndroma` the affine validity map, `obsigna` "seal", `lege` "read"),
plus `vacuum()`, the all-zero frame the driver starts every input from:

| function | contract |
|---|---|
| `redundantia(b: acies<u8, 17>, n: mensura) -> u16` | CRC-16/CCITT-FALSE over `b[0..n)`: init `0xFFFF`, polynomial `0x1021`, no reflection, no final xor |
| `syndroma(w: acies<u8, 17>) -> u16` | `redundantia(w, 15) aut (w sicut DeModFrame).cursus`; zero iff the CRC holds |
| `obsigna(f: DeModFrame) -> DeModFrame` | the frame with its six free fields (`genus` … `tempus`) as given, `signum = 0xd3`, `versio = 1`, and `cursus` computed; whatever `f` held in those three is overwritten |
| `lege(w: acies<u8, 17>) -> u8` | `0` valid, `1` bad sync, `2` bad version, `3` bad CRC, checked **in that order** |

**Where the decode order comes from.** The vendored spec's *Validity*
section says a frame is valid *iff* three conditions hold and lists them
unordered — an "iff" over a conjunction has no order. So the vendored spec
certifies the **verdict** (valid or not) and says nothing about *which*
condition a bad frame fails. The order `lege` implements — sync, then
version, then CRC — is the reference implementation's
(`wirelab_core.py:43-48`, and `frame.go:112-124` agrees). Reason codes 1–3
are therefore **supplementary** evidence of agreement with the reference
*code*, not part of the 246-vector certificate, and the harness reports them
separately (section 6, section 5 of the stream). There is no length verdict:
the reference's `ErrBadLength` is unrepresentable here, because the
argument's type is `acies<u8, 17>` and a 16-byte word cannot be passed.

**Why the encode's affinity is an argument, not a measurement.** The
theorem in `golden_vectors.json` is conditional: *any implementation that is
bit-placement + CRC (hence affine) and matches the basis equals the
reference everywhere*. Passing 246 vectors proves agreement on the basis;
the extension to 2^108 frames rests on the implementation being affine,
which is a property of its **structure**. The criterion, stated so it can be
checked by reading: **every data-dependent operation in the encode is
GF(2)-linear, and no carrying arithmetic touches data.** Bit placement into
fields is linear; `aut` is addition in GF(2); a shift by a constant is
linear; and the CRC's conditional — `si c ge 0x8000 { c = (c sursum 1) aut
0x1021 } aliter { c = c sursum 1 }` — *is* a branch on data, but the two
arms differ by a constant xor, so the whole is `(c sursum 1) aut
(top_bit(c) · 0x1021)`, which is linear in `c`. The init `0xFFFF` is the
affine offset. What would break affinity is `+`, `*`, a comparison used as
a value, or a mask applied to a signed path — and `redundantia`, `obsigna`
and the field stores contain none of those. That is an argument from
reading the code; the first draft said "nothing branches on data", which
was false of the CRC and did not need to be true.
The example frame — `d31312340001ffffdeadbeefab12cd24c0`, with many bits
set across every field — is one non-basis point at which the argument is
spot-checked: a carry, a wrong mask or a field written through a signed
path would pass the one-hot basis and fail there. One point is not a proof
and is not presented as one.

## 6. The certificate stream

`probatio.exsc` writes exactly **2,502 bytes** to standard output through
`scribe_octeto`, in five sections. `entry23/expecta.py` (verification-only
Python, off the build path, per `prototypes/README.md`'s rule for tools that
never ship) builds the same 2,502 bytes from the vendored JSON, and
`tests/run.sh` compares section by section and names the first differing
vector index.

No vector is embedded in `probatio.exsc`: every basis input is derived from
its index, as `verify_laws.py:66-82` derives them. Two bit-numbering
conventions are in play and they differ — recorded so the driver cannot
confuse them:

- **Encode-basis input bits** (`verify_laws.py:66-72`) number the 108 free
  bits **LSB-first within each field, fields in declaration order**: bit
  `i` of the input is bit `i` of `genus` for `i < 4`, bit `i−4` of
  `numerus` for `4 ≤ i < 20`, then `fons` (20–35), `meta` (36–51), `onus`
  (52–83), `tempus` (84–107). "Bit `b` of a field" is the field's integer
  value `1 sursum b`; for `onus` that is `payload = int.to_bytes(4, "big")`,
  which is what `u32:maior` writes.
- **Syndrome-basis word bits** (`verify_laws.py:79-80`) number the 136 wire
  bits **MSB-first across the wire**: bit `i` is byte `i / 8`, mask
  `0x80 deorsum (i mod 8)`.

The driver avoids division and a variable shift count entirely: it walks a
one-hot value with `x = x sursum 1` (for the encode basis, per field) or
`m = m deorsum 1` inside a byte loop (for the wire bits), so D1's
same-type rule never needs a narrowing cast — which is `[OPEN]` (IR 2.3).

| § | bytes | content | expected from |
|---|---|---|---|
| 1 | 109 × 17 = 1,853 | `obsigna` of input 0 (all zero), then of input bit `i` set, `i = 0..107`; each frame written byte 0..16 through `f sicut acies<u8, 17>` | `encode_basis[k].frame` |
| 2 | 137 × 2 = 274 | `syndroma` of the zero word, then of the word with wire bit `i` set, `i = 0..135`; each written big-endian through `Syndroma { valor: y } sicut acies<u8, 2>` where `@transitus structura Syndroma { valor: u16:maior }` | `syndrome_basis[k].syndrome`, big-endian |
| 3 | 2 + 2 + 17 = 21 | `redundantia("123456789")`, `redundantia(0^15)`, big-endian; then `obsigna` of the frame with `genus 3, numerus 0x1234, fons 1, meta 0xffff, onus 0xdeadbeef, tempus 0xab12cd` | `anchors.crc_123456789`, `anchors.crc_zero15`, `anchors.exampleFrame_full` |
| 4 | 109 + 109 = 218 | for each frame of section 1, in order: `lege(frame)` — 109 verdict bytes; then, for each, `1` if the six free fields read back through `frame sicut DeModFrame` equal the inputs and `0` otherwise — 109 flag bytes | 109 × `00` then 109 × `01`: decode∘encode = id, the vendored spec's first formal property |
| 5 | 136 | the example frame from section 3; for wire bit `i = 0..135`: flip it with `aut`, `lege`, flip it back | derived by `expecta.py` from the reference decode: `01` × 8 (sync), `02` × 4 (version nibble), `03` × 124 (every other single-bit flip breaks the CRC) |

Sections 1 and 2 **are** the certificate and are reported as "246/246".
Sections 3–5 are anchors and laws and are reported separately; section 5 in
particular is what checks the decode *order* — a flipped sync bit also
breaks the CRC, so verdict `1` rather than `3` on bits 0–7 is the evidence
that sync is tested first, and `2` on bits 8–11 that version precedes CRC.

`Syndroma` is a second `@transitus` type declared in `codex.exsc` for
section 2 — the type is the byte-order conversion (D2), so a big-endian
`u16` write is a one-field struct and a cast, not a pair of shifts.

The driver's `initium` derives `ambitus` from `Mundus` exactly as
`examples/initium.exsc` does; the harness audits the binary with
`--potestates Mundus,ambitus` and `syscall-audit` must show `write` and
`exit_group` only.

### 6.1 The three mutants

CONTRIBUTING's mutation rule, done mechanically by the harness. Each mutant
is applied to a copy of the sources, and the run **must fail**, naming a
vector:

| mutant | what it breaks | first failing section |
|---|---|---|
| polynomial `0x1021` → `0x1020` in `redundantia` | every CRC | 1, at vector 0 (`5b80` is the CRC of the zero frame) |
| `numerus: u16:maior` → `u16:minor` in the fixture's declaration | byte order of one field | 1, at vector 5 (input bit 4 is `numerus` bit 0: `00 01` becomes `01 00`) |
| `versio` and `genus` swapped in the declaration | sub-byte packing order | 1, at vector 0 (byte 1 becomes `01`, not `10`) |

A harness on which any mutant passes has proved nothing and is itself the
bug.

## 7. What the checker must not do

- Must not admit `aut`/`sursum`/`deorsum` on `iN`. Arithmetic shift right
  exists in the IR (`shr` is arithmetic for `iN`) and is deliberately not
  reachable from the language yet: signed shifts have two sensible
  semantics and no program here needs either. `[OPEN]`
- Must not type `f.numerus` as `u16:maior`. That was the status quo before
  M6, and it was why every field access was `EXS-E0303`
  (`chk_ty_transitus.asm` mutant M4 puts it back and fails).
- Must not fold a literal shift count ≥ N into a compile-time diagnostic
  without a §13 code for it; IR 6 already lists the constant-folded trap as
  open and unnumbered. It traps at runtime, like `1 + 255` in `u8`.
- Must not let `ExprNS` admit the literal. `dum f eq DeModFrame {` must be a
  parse error at `{`'s contents, not a literal.

## 8. Worked example, as written and as it runs

Written at M0 as the target the milestones implemented against, in
D1/D3/D4/D6 syntax: struct literal with every field and mandatory commas,
`sicut acies<u8, 17>` for the byte view, `sursum 1` for the shift,
`aut 0x1021` for the xor, `ge 0x8000` for the top bit, `mensura` indices.
The `codex.exsc` block below is, but for comments, the file
`tests/conformance/entry23/codex.exsc` that passes the certificate
(section 11); `redundantia` is also `tests/programs/redundantia/` and
`obsigna`/`lege` are also exercised by `tests/programs/forma/`.

```exsecutor
// entry23/codex.exsc -- pure; no `poscit`, no `initium`.

@transitus
publica structura Syndroma {
    valor: u16:maior
}

// CRC-16/CCITT-FALSE over b[0..n). Init 0xFFFF, polynomial 0x1021, no
// reflection, no final xor. `c sursum 1` discards the top bit (D1), which
// is why there is no `& 0xFFFF`; `c ge 0x8000` reads it before it goes.
publica functio redundantia(b: acies<u8, 17>, n: mensura) -> u16 {
    mutabilis c: u16 = 0xffff;
    per i in 0..n {
        c = c aut ((b[i] sicut u16) sursum 8);
        per k in 0..8 {
            si c ge 0x8000 {
                c = (c sursum 1) aut 0x1021;
            } aliter {
                c = c sursum 1;
            }
        }
    }
    redde c;
}

// Zero iff the stored CRC matches. `.cursus` is `u16:maior` in the
// declaration and reads as `u16` (D2): the big-endian load is the access.
publica functio syndroma(w: acies<u8, 17>) -> u16 {
    firma f = w sicut DeModFrame;
    redde redundantia(w, 15) aut f.cursus;
}

// Seal: a frame whose six free fields are set goes in; the same frame with
// its sync byte, version and CRC written comes out. It takes the frame, not
// six scalars, because six scalars plus the hidden result pointer are seven
// words and the IR calling convention passes six (emit.inc's call path;
// finding 8). `f` is borrowed (IR 2.9), so the seal works on a copy.
publica functio obsigna(f: DeModFrame) -> DeModFrame {
    mutabilis g = f;
    g.signum = 0xd3;
    g.versio = 1;
    g.cursus = redundantia(g sicut acies<u8, 17>, 15);
    redde g;
}

// The all-zero frame. A struct literal names every field (D3); this is the
// one place the driver has to.
publica functio vacuum() -> DeModFrame {
    redde DeModFrame {
        signum: 0,
        versio: 0,
        genus: 0,
        numerus: 0,
        fons: 0,
        meta: 0,
        onus: 0,
        tempus: 0,
        cursus: 0
    };
}

// Read: 0 valid, 1 bad sync, 2 bad version, 3 bad CRC -- the reference
// implementation's order (wirelab_core.py decode), not the vendored spec's
// unordered "iff". No length verdict: the type is the length.
publica functio lege(w: acies<u8, 17>) -> u8 {
    firma f = w sicut DeModFrame;
    si f.signum ne 0xd3 { redde 1; }
    si f.versio ne 1    { redde 2; }
    si syndroma(w) ne 0 { redde 3; }
    redde 0;
}
```

The driver's two byte-emitting helpers, for the shape of D7 and section 2
(as built, `probatio.exsc` writes them without `poscit sicut s` -- finding
9):

```exsecutor
// entry23/probatio.exsc -- the only file that names Mundus.

functio scribe_quantum(s: Scriptor, f: DeModFrame) -> mensura poscit sicut s {
    firma b = f sicut acies<u8, 17>;
    per i in 0..17 { s.scribe_octeto(b[i]); }
    redde 17;
}

functio scribe_u16(s: Scriptor, v: u16) -> mensura poscit sicut s {
    firma y = Syndroma { valor: v };
    firma b = y sicut acies<u8, 2>;
    s.scribe_octeto(b[0]);
    s.scribe_octeto(b[1]);
    redde 2;
}
```

and the encode-basis walk for one field, showing how a variable shift count
is avoided:

```exsecutor
    // input bits 4..19 are numerus bits 0..15
    mutabilis n: u16 = 1;
    per i in 0..16 {
        mutabilis f = vacuum();
        f.numerus = n;
        scribe_quantum(s, obsigna(f));
        n = n sursum 1;
    }
```

Things this example leans on that are settled elsewhere, each since run:
`per i in 0..n` binds `i: mensura` (the range partner's type;
`__chk_ty_index` wants `mensura` for `acies`); **`per k in 0..8`, `0..16`,
`0..17` — a range of two pending literals — binds `mensura` by the §8.5
rule added for it** (before M6, `__chk_ty_for` → `__chk_ty_settled` made
every such loop `EXS-E0308`, the review's M4; the rule went into the spec
rather than the loops being rewritten with a typed bound, because
`per i in 0..17` is what anyone writes over an array;
`tests/unit/chk_ty_transitus.asm` pins it and `forma` and entry 23 write
it); `b[i] sicut u16` is §5.2's explicit widening (`redundantia`, a
`zext`); a literal takes the other operand's type (`types.inc:57`;
`chk_ty_bitops.asm`'s last check reads the ids); assignment through an
index `x[b] = …` on a `mutabilis` `acies` binding is an lvalue (§8.6;
`probatio.exsc`'s `verte`); `mutabilis g = f` copies the struct (§5.2, the
cast paragraph; `obsigna` runs on it in entry 23).

**What M6 checked of this section, and what it did not.** The `codex.exsc`
block above, with the fixture's `DeModFrame`; the two `probatio.exsc`
helpers with each `s.scribe_octeto(b[k])` replaced by an `aut` into a
`u8` (`scribe_octeto` is M7's); the encode-basis walk; and an `initium`
calling them, was compiled by the M6 compiler: it type-checks clean, and
`lwr_module` lowers it to IR that `bfa_verify_func` accepts --
`obsigna(f) sicut acies<u8, 17>` passing a fresh slot the call writes into,
`Syndroma { valor: v } sicut acies<u8, 2>` a `store u16 ... 0 maior` and a
two-byte `copy`, `per i in 0..17 { ... b[i] ... }` a `u64` loop with `chk`
and `index` on the bound copy. So nothing in the example needed changing.
At M6 it had not run — the emitter lacked the opcodes M3 and M4 add; after
them `tests/programs/forma/` (aca9755) and entry 23 (07bc8d5) ran it, and
the markers this paragraph once deferred are retired above. One placement
differed from D4's first text: the size equality `N` = size of `S` is
checked in pass 2, inside the cast's own check (`checker/types/member.inc`,
`__chk_ty_wirebytes`), not in pass 4 -- pass 4 runs after the bodies are
typed, and a packed `@transitus` size is the sum of the declared widths,
which pass 2 already has. D4 and spec §5.2 now say so.

## 9. Findings

Numbered; each names the document and the sentence. Where the spec is the
one that was wrong, the amendment is in the same commit as this file.

1. **Spec §14, "Each entry must fail to compile, except entries 16 and
   17."** False for entry 15 (a runtime abort: it compiles) since it was
   written, and for 23 (a run that must match a certificate) since ADR 0011
   added it. Twenty entries reject; four compile and are judged by what they
   produce. Amended.
2. **Vendored `WIRE_QUANTUM_SPEC.md` §Validity** is an unordered "iff", so it
   certifies the verdict and not the reason. `lege`'s reason codes follow
   the reference *code*, and the harness reports them outside the 246
   count. Not a defect in the vendored text — it never claimed an order —
   but a limit on what "spec-conformant" can mean for a decoder that
   reports why.
3. **The two bit-numbering conventions** in `verify_laws.py` (LSB-first
   within fields for the encode basis; MSB-first across the wire for the
   syndrome basis) are both correct and are easy to swap. Section 6 states
   both; `probatio.exsc` is written so that neither is computed with
   division or a variable shift.
4. **`docs/design/phrase-grammar.md` section 2.6** still lists `!`, `/`, `*%`, `*|`
   in its precedence table and shifts as `[OPEN]`; spec §8.6 wins where
   they disagree (§8.6 says so itself) and is now the one with shifts and
   `aut`. The design record is not edited here; it is a record.
5. **IR 2.7, "Spec §5.2 admits `u12:maior` … `[OPEN]`"** was stale: §5.2
   rule 2 says `u12` does not parse, and §8.6 makes `BitType` a table
   lookup. IR 7 item 1 was likewise already done by §5.2. Both corrected
   in the marker-retirement commit; the verifier refuses `load u12`
   (`tests/ir/reject_verify_load_width.ir`, M4).
6. **`sig.inc:822`'s `chk_ty_litval` admits `_` as a digit separator** while
   §8.4 leaves separators `[OPEN]` and `lex.inc:335` rejects `1_000` as
   `EXS-E0210` before the checker sees it. Unreachable today; when
   separators are settled one of the two is wrong. D6 does not touch it.
7. **`tests/run.sh`'s `cert` branch** failed any `status=run` fixture of
   that shape by design until M7 replaced it with `cert_entry23`
   (07bc8d5); the directive is `status=run`.
8. **Nothing in the language passes an aggregate by value in a certified
   program yet.** `obsigna` returns a `DeModFrame`; LOW 2.7's aggregate
   return convention is what it lands on. The first draft of this file gave
   `obsigna` the six free fields as scalar parameters; with the hidden
   result pointer that is seven words, and the emitter's call path refuses a
   seventh (`emit.inc:1552-1579`, which counts the result pointer). So
   `obsigna` takes the frame and seals it, which is also what "seal" means.
   Stack-passed arguments are a backend extension nothing here needs. The
   aggregate-return convention turned out to be implemented: `obsigna`
   takes and returns a `DeModFrame` by value in entry 23 (section 11), so
   the `&DeModFrame` fallback this finding once held in reserve was not
   needed.

9. **`poscit sicut s` does not compose: a function declared `poscit sicut
   s` that calls another declared `poscit sicut s` with the same `s` is
   refused, `EXS-E0421`.** Found by M7 writing `probatio.exsc` as section 8
   drew it, with helpers that call helpers. Minimal repro:

   ```exsecutor
   functio unum(s: Scriptor, b: u8) -> mensura poscit sicut s {
       redde s.scribe_octeto(b);
   }
   functio duo(s: Scriptor, b: u8) -> mensura poscit sicut s {
       redde unum(s, b);        // EXS-E0421 here
   }
   ```

   with an `initium` that binds `sub ambitus` and calls `duo`. By spec §4.2
   as amended, the row of a `Scriptor`-typed value is its mark, `{ambitus}`:
   at the call `unum(s, b)` the callee's `sicut s` substitutes to `{ambitus}`
   (`checker/rows/compute.inc`, `__chk_row_contrib`, the `.have_row`
   substitution through `__chk_row_tyrow`), and `duo`'s own declared `sicut
   s` means the same `{ambitus}` -- so the draw is covered. But
   `__chk_row_e0421` (`compute.inc:1713`) subtracts only the caller's
   declared ATOMS (`wrat`) from a contribution that substitution has already
   turned into atoms; the caller's declared ordinals (`wror`) are subtracted
   only from leftover ordinals, never expanded to the mark of the parameter
   they name. **The spec is right and the checker is wrong**, and it is
   still wrong at the marker-retirement commit: nothing in the spec — §4.2's
   substitution table included — should be read as the checker composing
   `sicut` today, and §4.2 now carries a sentence saying so. Not fixed
   here: it is a soundness-relevant change to capability rows in the
   checker's tree, not a small obviously-right one. `probatio.exsc` does what §4.1 rule 5
   already provides for a private function -- it writes no `poscit` and the
   rows are inferred -- which is not a workaround in the language, only a
   departure from section 8's drawing; `initium` still binds `sub ambitus`
   and the binary's audit is unchanged.

## 10. What retired each marker

Every row's tests are merged on `wire-codec` and were seen passing in
`tests/run.sh` at 07bc8d5 (626 pass, 0 fail, per the integrator's run).

| decision | milestone | the test as planned | what retired it |
|---|---|---|---|
| D8 phi | M2 (1b238c1) | IR: sum loop, swap, lost copy; program `phi_loops` | `tests/ir/phi_sum_loop.ir`, `phi_swap.ir`, `phi_lost_copy.ir`, `phi_same_target.ir`; `tests/unit/bfa_emit_phi.asm`; `tests/programs/phi_loops/` |
| D5 canonical form; IR 2.3's shift trap; the `mul` flag at 33–63 | M3 (6955adf) | IR: wrap and trap at `u4 u8 u24 u32 i8`, `u40` multiply at and just below 2^64, `3735928559`, shift by `N`, `chk` | `tests/ir/narrow_wrap.ir`, `trap_add_{u4,u8,u24,u32,i8}.ir`, `trap_sub_u8.ir`, `trap_mul_u8.ir`, `trap_mul_u40_2p64.ir`, `trap_mul_i40_2p64.ir`, `trap_mul_u40_below_2p64.ir`, `mul_u40.ir`, `iconst_wide.ir`, `trap_shl_u8.ir`, `trap_shr_u32.ir`, `trap_chk.ir`, `conv_roundtrip.ir`; `tests/unit/bfa_emit_narrow.asm`, `bfa_emit_bitwise.asm`; `tests/programs/angusta/` |
| byte order and bit fields in the emitter | M4 (8c852b0) | hand-written IR encoder producing `d31312340001ffffdeadbeefab12cd24c0` | `tests/ir/demodframe_encode.ir`, `demodframe_decode.ir`, `byte_order.ir`, `bits.ir`, `reject_verify_load_width.ir`, `reject_emit_straddle.ir`; `tests/unit/bfa_emit_bytes.asm` |
| D1 operators, D6 hex | M5 (6a3bb2b, b1f0042) | program `redundantia`: `0x29B1`, `0x4EC3` | `tests/unit/lex_hex_literal.asm`, `cst_shift_xor.asm`, `chk_ty_bitops.asm`, `chk_ty_hexlit.asm`, `lwr_bitops.asm`; `tests/programs/redundantia/` |
| D2 places, D3 literals, D4 the cast, §8.5's literal range | M6 (36ff767, aca9755) | program `forma`: literal → CRC through the view → decode | `tests/unit/cst_structlit.asm`, `chk_ty_structlit.asm`, `chk_ty_transitus.asm`, `lwr_transitus.asm`, `lwr_forma.asm`; `tests/programs/forma/` |
| §5.2's field restriction | 622a541 | — (found by review H1) | `tests/unit/chk_row_layout_kind.asm` |
| D7 `scribe_octeto`; the stream; the mutants | M7 (f8e0cd9, 2188b9f, 07bc8d5) | entry 23 `status=run`, 246/246, anchors, laws, three mutants failing | `tests/unit/prelude_scribe_octeto.asm`, `prelude_sine_ambitus.asm`; `tests/programs/octeti/`; `tests/conformance/entry23/` through `tests/run.sh`'s `cert_entry23` |

§14 entry 23 is `status=run`; ADR 0011's status line says implemented and
certified; what is still a hypothesis in this file is marked `[OPEN]`.

## 11. What M7 ran

`tests/run.sh`'s cert branch (`cert_entry23`) compiles the fixture,
`entry23/codex.exsc` and `entry23/probatio.exsc` as one unit, assembles and
runs it, and compares the stream with `entry23/expecta.py`'s. The codex is
section 8's block, unchanged but for comments; `probatio.exsc` walks the
encode basis three times (frames, verdicts, round-trip flags) through one
function taking a mode, and the wire bits twice (syndromes, bit-flip
verdicts) the same way, so the 109 inputs and 136 masks are each derived in
one place. Observed, in the worktree the M7 commit was verified in:

- the binary exits 0 and writes **2,502 bytes**; a second run writes the
  same bytes;
- `tools/syscall-audit.sh --potestates Mundus,ambitus`: PASS, and the only
  syscall sites are `write` and `exit_group`;
- **246/246 certificate vectors** (encode basis 109/109, syndrome basis
  137/137); anchors 3/3; laws 218/218 (section 4) and 136/136 (section 5);
- the three mutants of section 6.1 each fail, first at exactly the vector
  that section predicted: polynomial `0x1020`, section 1 vector 0
  (`...5b80` became `...6a40`, byte 15); `numerus: u16:minor`, section 1
  vector 5 (byte 2 `00` became `01`); `versio`/`genus` swapped, section 1
  vector 0 (byte 1 `10` became `01`).

The compiler needed no change for any of it. The one departure from
section 8 is finding 9. Two things section 8 leaned on as `[UNTESTED]` ran
here: a function taking and returning a `DeModFrame` by value (`obsigna`,
LOW 2.7's aggregate return, with the hidden result pointer inside the
six-word limit), and `mutabilis w = basis` copying an `acies<u8, 17>`
parameter before `verte` writes through an index.
