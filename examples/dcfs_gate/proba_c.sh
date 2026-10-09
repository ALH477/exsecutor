#!/usr/bin/env bash
# examples/dcfs_gate/proba_c.sh -- dcfs_gate checked the way dcf-serializer (a C
# host with a network-facing reader) consumes it.
#
#   1. exsc emits the C unit, its C face (--emitte h) and its Rust face
#      (--emitte rs) twice; the pairs must be byte-identical. dcfs_gate.h, the
#      hand-written header, must match the generated one prototype for prototype.
#   2. proba.c (a line-protocol host) is built against the unit with every C
#      compiler given, at -O0 and -O2, under UBSan, with the GENERATED header
#      force-included; one more gcc build runs under ASan+UBSan, because the host
#      hands the gate a buffer of exactly its declared size and a read past it
#      must be a failure, not a lucky answer. proba.py drives all of them against
#      its own oracle: it compares the EXACT verdict, not only admit/refuse.
#   3. behaviour mutants of dcfs_gate.exsc must each BUILD and then FAIL
#      proba.py (or trap) -- a mutant that does not build proves nothing.
#   4. capability: the unit is pure by spec §4.1 rule 6. Drawing ambient
#      authority must be refused EXS-E0421; taking a capability parameter must
#      break the hand-written header and the stale generated one.
#   5. the Rust face, where cargo exists: tests/c/facies/crate links the unit
#      and calls the gate through the generated extern block.
#
# Usage: examples/dcfs_gate/proba_c.sh [CC...]   (default: gcc clang)
# Needs build/exsc (make all), fasmg is NOT needed here, python3. Writes only
# to a temp dir.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
exsc="$root/build/exsc"
[ -x "$exsc" ] || { echo "proba_c: $exsc missing -- run 'make all'" >&2; exit 2; }
ccs=("$@")
[ ${#ccs[@]} -gt 0 ] || ccs=(gcc clang)
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
fail=0
unit="$here/dcfs_gate.exsc"

emit() { "$exsc" aedifica --hospes x86_64-linux --emitte "${3:-c}" "$1" -o "$2" >/dev/null 2>&1; }

for k in c h rs; do
  emit "$unit" "$work/gate.gen.$k" "$k"
  emit "$unit" "$work/again.gen.$k" "$k"
  if [ -s "$work/gate.gen.$k" ] && cmp -s "$work/gate.gen.$k" "$work/again.gen.$k"; then
    echo "  [ok]   --emitte $k emitted twice, byte-identical ($(wc -c <"$work/gate.gen.$k") bytes)"
  else
    echo "  [FAIL] --emitte $k: two emissions differ, or wrote nothing"; fail=1
  fi
done
python3 "$root/tests/c/facies/protos.py" "$here/dcfs_gate.h" "$work/gate.gen.h" \
  || { echo "  [FAIL] dcfs_gate.h and the generated header disagree"; fail=1; }

build() {   # build CC OPT UNIT OUT [SAN...]
  local cc=$1 opt=$2 gen=$3 out=$4; shift 4
  local san=("$@")
  if [ ${#san[@]} -eq 0 ]; then
    san=(-fsanitize=undefined -fno-sanitize-recover=all)
    if "$cc" --version 2>/dev/null | head -1 | grep -qi clang; then
      san=(-fsanitize=undefined -fsanitize-trap=undefined)
    fi
  fi
  "$cc" -std=c11 "$opt" -Wall -Wextra -Werror -Wno-unused-function -Wno-cpp -Wno-#warnings "${san[@]}" \
    -include "$work/gate.gen.h" -I "$here" "$gen" "$here/proba.c" -o "$out"
}

hosts=()
for cc in "${ccs[@]}"; do
  command -v "$cc" >/dev/null || { echo "  [FAIL] $cc: not on PATH"; fail=1; continue; }
  for opt in -O0 -O2; do
    if build "$cc" "$opt" "$work/gate.gen.c" "$work/proba_${cc}${opt}" 2>"$work/build.err"; then
      hosts+=("$work/proba_${cc}${opt}")
    else
      echo "  [FAIL] $cc $opt: build"; head -5 "$work/build.err"; fail=1
    fi
  done
done
if build "${ccs[0]}" -O1 "$work/gate.gen.c" "$work/proba_asan" -fsanitize=address,undefined -fno-sanitize-recover=all 2>"$work/asan.err"; then
  hosts+=("$work/proba_asan")
else
  echo "  [FAIL] ${ccs[0]} ASan build"; head -5 "$work/asan.err"; fail=1
fi
if [ ${#hosts[@]} -gt 0 ]; then
  echo "  hosts: $(for h in "${hosts[@]}"; do basename "$h"; done | tr '\n' ' ')"
  if python3 "$here/proba.py" "${hosts[@]}" >"$work/out.txt" 2>&1; then
    cat "$work/out.txt"
  else
    cat "$work/out.txt" | head -30; fail=1
  fi
fi

if [ "$fail" -ne 0 ]; then echo "dcfs_gate: FAIL (baseline; mutants not run)"; exit 1; fi

mutant() {  # mutant NAME SED-EXPR
  local name=$1 expr=$2
  sed "$expr" "$unit" >"$work/m.exsc"
  if cmp -s "$work/m.exsc" "$unit"; then
    echo "  [FAIL] mutant '$name' changed nothing -- its pattern moved"; fail=1; return
  fi
  emit "$work/m.exsc" "$work/m.gen.c"
  if ! build "${ccs[0]}" -O0 "$work/m.gen.c" "$work/mproba" 2>"$work/m.err"; then
    echo "  [FAIL] mutant '$name' did not build (so it proves nothing)"; head -3 "$work/m.err"; fail=1; return
  fi
  if python3 "$here/proba.py" --cases 30000 "$work/mproba" >/dev/null 2>&1; then
    echo "  [FAIL] mutant '$name' passed proba.py"; fail=1
  else
    echo "  [ok]   mutant '$name' is caught"
  fi
}
mutant "FINAL flag admitted"                  's/(h\[8\] atque 0xe3) ne 0/(h[8] atque 0xeb) ne 0/'
mutant "major version unchecked"              's/si h\[4\] ne 5 { redde 3; }//'
mutant "payload_len cap off by one"           's/si pondus gt 16777216 { redde 5; }/si pondus gt 16777217 { redde 5; }/'
mutant "length must only be at least"         's/si n ne ((pondus sicut mensura) + 21) { redde 6; }/si n lt ((pondus sicut mensura) + 21) { redde 6; }/'
mutant "header minimum one byte short"        's/si n lt 17 { redde 1; }/si n lt 16 { redde 1; }/'
mutant "CRC polynomial typo"                  's/0xEDB88320/0xEDB88321/'
mutant "CRC final xor dropped"                's/redde crc aut 0xFFFFFFFF;/redde crc;/'
mutant "CRC not checked"                      's/si dcfs_crc(b, finis) ne dcfs_lege32(b, finis) { redde 8; }//'
mutant "capacity check dropped (traps)"       's/^    si n gt 65557 { redde 7; }//'
mutant "overlong C0/C1 lead admitted"         's/si c lt 0xc2 { redde 14; }//'
mutant "E0 overlong admitted"                 's/si c eq 0xe0 { imum = 0xa0; }//'
mutant "surrogate admitted"                   's/si c eq 0xed { summum = 0x9f; }//'
mutant "F0 overlong admitted"                 's/si c eq 0xf0 { imum = 0x90; }//'
mutant "above U+10FFFF admitted"              's/si c eq 0xf4 { summum = 0x8f; }//'
mutant "cut-short sequence admitted"          's/si resid ne 0 { redde 14; }//'
mutant "string body past payload unchecked"   's/si lon gt (finis - pos) { redde 10; }//'
mutant "string cap off by one"                's/si longitudo gt 65536 { redde 13; }/si longitudo gt 65537 { redde 13; }/'
mutant "varint canonical form unchecked"      's/si numerus ge 2 { si ultimus eq 0 { redde 15; } }//'
mutant "varint 10th byte unchecked"           's/si numerus eq 10 { si ultimus gt 1 { redde 15; } }//'
mutant "nesting limit 33"                     's/si alt ge 32 { redde 11; }/si alt ge 33 { redde 11; }/'
mutant "map count*2 unchecked"                's/si (duplum sicut mensura) gt (finis - pos) { redde 12; }//'
mutant "array count unchecked"                's/si (numerus sicut mensura) gt (finis - pos) { redde 12; }//'
mutant "struct end marker ignores type"       's/si fty eq 0 {/si fty ge 0 {/'
mutant "F64 tag not recognised"               's/si tag eq 0x0b { notum = 1; fixum = 8; }//'
mutant "UUID one byte short"                  's/si tag eq 0x13 { notum = 1; fixum = 16; }/si tag eq 0x13 { notum = 1; fixum = 15; }/'
mutant "unassigned tags admitted"             's/si notum eq 0 { redde 9; }//'

cap_refused() {  # cap_refused NAME LINE
  local name=$1 line=$2
  sed "s/^publica functio admitte_caput(h: acies<u8, 17>, n: mensura) -> u8 {/&\\n    $line/" "$unit" >"$work/cap.exsc"
  if cmp -s "$work/cap.exsc" "$unit"; then echo "  [FAIL] capability '$name': pattern moved"; fail=1; return; fi
  if "$exsc" aedifica --hospes x86_64-linux --emitte c --diagnostica json \
      "$work/cap.exsc" -o "$work/cap.gen.c" >/dev/null 2>"$work/cap.err"; then
    echo "  [FAIL] capability '$name' compiled"; fail=1
  elif grep -q '"code":"EXS-E0421"' "$work/cap.err"; then
    echo "  [ok]   capability '$name' refused EXS-E0421"
  else
    echo "  [FAIL] capability '$name' refused, but not as EXS-E0421"; fail=1
  fi
}
cap_refused "ambient ambitus" 'firma z = ambitus;'
cap_refused "ambient archivum" 'firma z = archivum;'

# admitte_corpus is not called by another function of the unit, so a changed signature only breaks the face
sed 's/^publica functio admitte_corpus(b: acies<u8, 65557>,/publica functio admitte_corpus(s: Scriptor, b: acies<u8, 65557>,/' "$unit" >"$work/cap.exsc"
cmp -s "$work/cap.exsc" "$unit" && { echo "  [FAIL] Scriptor-parameter pattern moved"; fail=1; }
emit "$work/cap.exsc" "$work/cap.gen.c"
if "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$here/dcfs_gate.h" "$work/cap.gen.c" 2>/dev/null; then
  echo "  [FAIL] capability 'Scriptor parameter' still matches dcfs_gate.h"; fail=1
else
  echo "  [ok]   capability 'Scriptor parameter' breaks dcfs_gate.h"
fi
emit "$work/cap.exsc" "$work/cap.gen.h" h
if ! "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$work/cap.gen.h" "$work/cap.gen.c" 2>/dev/null; then
  echo "  [FAIL] the Scriptor mutant's own generated header does not compile against it"; fail=1
elif "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$work/gate.gen.h" "$work/cap.gen.c" 2>"$work/stale.err"; then
  echo "  [FAIL] capability 'Scriptor parameter' still matches the generated header"; fail=1
elif grep -q 'conflicting types' "$work/stale.err"; then
  echo "  [ok]   capability 'Scriptor parameter' breaks the stale generated header (conflicting types)"
else
  echo "  [FAIL] the stale generated header is refused, but not for conflicting types"; fail=1
fi

if command -v cargo >/dev/null; then
  cat >"$work/consumer.rs" <<'RS'
fn consume() -> i32 {
    // a 23-byte frame: u8 9, CRC-32 8d7100c9
    let frame: [u8; 23] = [0x44, 0x43, 0x46, 0x53, 0x05, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
                           0x00, 0x00, 0x01, 0x02, 0x09, 0x8d, 0x71, 0x00, 0xc9];
    let mut b = vec![0u8; 65557];
    let mut h = [0u8; 17];
    unsafe {
        b[..23].copy_from_slice(&frame);
        h.copy_from_slice(&frame[..17]);
        if exs_admitte_corpus(b.as_mut_ptr(), 23) != 0 { return 1; }
        if exs_admitte_caput(h.as_mut_ptr(), 23) != 0 { return 2; }
        if exs_admitte_caput(h.as_mut_ptr(), u64::MAX) != 6 { return 3; }
        b[22] ^= 1;
        if exs_admitte_corpus(b.as_mut_ptr(), 23) != 8 { return 4; }
        if exs_admitte_corpus(b.as_mut_ptr(), 22) != 6 { return 5; }
        if exs_admitte_corpus(b.as_mut_ptr(), 0) != 1 { return 6; }
    }
    0
}
RS
  cp -r "$root/tests/c/facies/crate" "$work/crate"
  rs_build() {  # UNIT FACE
    ( cd "$work/crate" && EXS_FACIES_UNIT="$1" EXS_FACIES_RS="$2" EXS_FACIES_USE="$work/consumer.rs" \
        CC="${ccs[0]}" CARGO_TARGET_DIR="$work/crate/target" cargo build --offline --quiet ) >"$work/cargo.log" 2>&1
  }
  if rs_build "$work/gate.gen.c" "$work/gate.gen.rs" && "$work/crate/target/debug/facies"; then
    echo "  [ok]   Rust: the generated extern block links the unit; caput and corpus answer"
  else
    echo "  [FAIL] Rust: the generated extern block did not build or did not answer"; head -20 "$work/cargo.log"; fail=1
  fi
  emit "$work/cap.exsc" "$work/cap.gen.rs" rs
  if rs_build "$work/cap.gen.c" "$work/cap.gen.rs"; then
    echo "  [FAIL] Rust: the stale consumer still builds against the Scriptor mutant's face"; fail=1
  elif grep -q 'E0061' "$work/cargo.log"; then
    echo "  [ok]   Rust: the Scriptor mutant's regenerated face breaks the stale consumer (E0061)"
  else
    echo "  [FAIL] Rust: the mutant's face fails, but not on the argument count"; head -20 "$work/cargo.log"; fail=1
  fi
else
  echo "  [skip] Rust face: cargo is not on PATH"
fi

[ "$fail" -eq 0 ] && echo "dcfs_gate: PASS" || { echo "dcfs_gate: FAIL"; exit 1; }
