#!/usr/bin/env bash
# examples/dcf_net_gate/proba_c.sh -- dcf_net_gate checked the way DCF-ID (Rust)
# and DCF-WatchDawg (C host for a root shell daemon) consume it.
#
#   1. exsc emits the C unit, its C face (--emitte h) and its Rust face
#      (--emitte rs) twice; the pairs must be byte-identical. dcf_net_gate.h,
#      the hand-written header, must match the generated one prototype for
#      prototype.
#   2. proba.c (a line-protocol host) is built against the unit with every C
#      compiler given, at -O0 and -O2, under UBSan, with the GENERATED header
#      force-included; one more gcc build runs under ASan+UBSan, because the
#      host hands the gate a buffer of exactly its declared size and a read
#      past it must be a failure, not a lucky answer. proba.py drives each:
#      ~900 000 cases against three independent definitions of "an IPv4
#      address" (a regex, Python's ipaddress, libc's inet_pton).
#   3. behaviour mutants of dcf_net_gate.exsc must each BUILD and then FAIL
#      proba.py -- a mutant that does not build proves nothing.
#   4. capability: the unit is pure by spec §4.1 rule 6. Drawing ambient
#      authority must be refused EXS-E0421; taking a capability parameter
#      must break the hand-written header and the stale generated one.
#   5. the Rust face, where cargo exists: tests/c/facies/crate links the unit
#      and calls the gate through the generated extern block.
#
# Usage: examples/dcf_net_gate/proba_c.sh [CC...]   (default: gcc clang)
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
unit="$here/dcf_net_gate.exsc"

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
python3 "$root/tests/c/facies/protos.py" "$here/dcf_net_gate.h" "$work/gate.gen.h" \
  || { echo "  [FAIL] dcf_net_gate.h and the generated header disagree"; fail=1; }

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

for cc in "${ccs[@]}"; do
  command -v "$cc" >/dev/null || { echo "  [FAIL] $cc: not on PATH"; fail=1; continue; }
  for opt in -O0 -O2; do
    if build "$cc" "$opt" "$work/gate.gen.c" "$work/proba" \
        && python3 "$here/proba.py" "$work/proba" >"$work/out.txt" 2>&1; then
      echo "  [ok]   $cc $opt: $(grep -o '[0-9]* cases; [0-9]* admitted, [0-9]* refused, 0 disagreements' "$work/out.txt")"
    else
      echo "  [FAIL] $cc $opt"; head -20 "$work/out.txt"; fail=1
    fi
  done
done
if build "${ccs[0]}" -O1 "$work/gate.gen.c" "$work/proba_asan" -fsanitize=address,undefined -fno-sanitize-recover=all 2>"$work/asan.err"; then
  if python3 "$here/proba.py" "$work/proba_asan" >"$work/out.txt" 2>&1; then
    echo "  [ok]   ${ccs[0]} -O1 ASan+UBSan: exact-size buffers, no read past them"
  else
    echo "  [FAIL] ${ccs[0]} -O1 ASan+UBSan"; head -20 "$work/out.txt"; fail=1
  fi
else
  echo "  [FAIL] ${ccs[0]} ASan build"; head -5 "$work/asan.err"; fail=1
fi

if [ "$fail" -ne 0 ]; then echo "dcf_net_gate: FAIL (baseline; mutants not run)"; exit 1; fi

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
  if python3 "$here/proba.py" "$work/mproba" >/dev/null 2>&1; then
    echo "  [FAIL] mutant '$name' passed proba.py"; fail=1
  else
    echo "  [ok]   mutant '$name' is caught"
  fi
}
mutant "leading zero admitted"            's/si initium eq 1 { redde 6; }//'
mutant "octet above 255 admitted"         's/si valor gt 255 { redde 7; }//'
mutant "dot count unchecked at the dot"   's/si puncta ge 3 { redde 4; }//'
mutant "trailing dot admitted"            's/^    si digiti eq 0 { redde 4; }/    si digiti eq 99 { redde 4; }/'
mutant "octet count unchecked"            's/si puncta ne 3 { redde 4; }//'
mutant "address length cap dropped"       's/si n gt 15 { redde 2; }//'
mutant "digit range off by one"           's/si c gt 0x39 { redde 3; }/si c gt 0x3a { redde 3; }/'
mutant "port leading zero admitted"       's/si n gt 1 { redde 4; }/si n gt 99 { redde 4; }/'
mutant "port maximum dropped"             's/si valor gt maximum { redde 5; }//'
mutant "port zero admitted"               's/si valor eq 0 { redde 5; }//'
mutant "interval ceiling is the port's"   's/numerus_iudica(b, n, 3600)/numerus_iudica(b, n, 65535)/'
mutant "loopback class moved"             's/si a eq 127 { redde 2; }/si a eq 126 { redde 2; }/'
mutant "172.16/12 widened"                's/si s le 31 { redde 6; }/si s le 63 { redde 6; }/'
mutant "multicast folded away"            's/si a ge 224 { redde 4; }//'

cap_refused() {  # cap_refused NAME LINE
  local name=$1 line=$2
  sed "s/^publica functio admitte_portum(b: acies<u8, 8>, n: mensura) -> u8 {/&\\n    $line/" "$unit" >"$work/cap.exsc"
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

sed 's/^publica functio admitte_portum(b: acies<u8, 8>,/publica functio admitte_portum(s: Scriptor, b: acies<u8, 8>,/' "$unit" >"$work/cap.exsc"
cmp -s "$work/cap.exsc" "$unit" && { echo "  [FAIL] Scriptor-parameter pattern moved"; fail=1; }
emit "$work/cap.exsc" "$work/cap.gen.c"
if "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$here/dcf_net_gate.h" "$work/cap.gen.c" 2>/dev/null; then
  echo "  [FAIL] capability 'Scriptor parameter' still matches dcf_net_gate.h"; fail=1
else
  echo "  [ok]   capability 'Scriptor parameter' breaks dcf_net_gate.h"
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
    let mut a = [0u8; 16];
    let mut p = [0u8; 8];
    unsafe {
        a[..7].copy_from_slice(b"1.2.3.4");
        if exs_admitte_ipv4(a.as_mut_ptr(), 7) != 0 { return 1; }
        if exs_ordo_ipv4(a.as_mut_ptr(), 7) != 0 { return 2; }
        a[..8].copy_from_slice(b"1.2.3.08");
        if exs_admitte_ipv4(a.as_mut_ptr(), 8) != 6 { return 3; }
        p[..4].copy_from_slice(b"7777");
        if exs_admitte_portum(p.as_mut_ptr(), 4) != 0 { return 4; }
        if exs_admitte_portum(p.as_mut_ptr(), u64::MAX) != 2 { return 5; }
        p[..2].copy_from_slice(b"60");
        if exs_admitte_intervallum(p.as_mut_ptr(), 2) != 0 { return 6; }
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
    echo "  [ok]   Rust: the generated extern block links the unit; ipv4, ordo, portum, intervallum answer"
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

[ "$fail" -eq 0 ] && echo "dcf_net_gate: PASS" || { echo "dcf_net_gate: FAIL"; exit 1; }
