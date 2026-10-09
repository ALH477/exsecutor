#!/usr/bin/env bash
# examples/watchdawg_gate/proba_c.sh -- watchdawg_gate checked the way DCF-WatchDawg
# (a shell daemon with a C host, dcf-gate) consumes it.
#
#   1. exsc emits the C unit, its C face (--emitte h) and its Rust face
#      (--emitte rs) twice; the pairs must be byte-identical. watchdawg_gate.h,
#      the hand-written header, must match the generated one prototype for
#      prototype.
#   2. proba.c (a line-protocol host) is built against the unit with every C
#      compiler given, at -O0 and -O2, under UBSan, with the GENERATED header
#      force-included; one more gcc build runs under ASan+UBSan, because the
#      host hands the gate a buffer of exactly its declared size and a read
#      past it must be a failure, not a lucky answer. proba.py drives each:
#      ~700 000 cases against three independent definitions of "a number"
#      (a regex, a hand-written reader, JSON's own parser).
#   3. behaviour mutants of watchdawg_gate.exsc must each BUILD and then FAIL
#      proba.py -- a mutant that does not build proves nothing.
#   4. capability: the unit is pure by spec §4.1 rule 6. Drawing ambient
#      authority must be refused EXS-E0421; taking a capability parameter
#      must break the hand-written header and the stale generated one.
#
# There is no Rust step: the only consumer is a C host. The Rust face is
# emitted (and compared twice) but never linked here.
#
# Usage: examples/watchdawg_gate/proba_c.sh [CC...]   (default: gcc clang)
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
unit="$here/watchdawg_gate.exsc"

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
python3 "$root/tests/c/facies/protos.py" "$here/watchdawg_gate.h" "$work/gate.gen.h" \
  || { echo "  [FAIL] watchdawg_gate.h and the generated header disagree"; fail=1; }

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

if [ "$fail" -ne 0 ]; then echo "watchdawg_gate: FAIL (baseline; mutants not run)"; exit 1; fi

mutant() {  # mutant NAME SED-EXPR   (a 0,/re/ address restricts a mutant to its first match)
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
mutant "numerus: empty admitted"                   '0,/si n eq 0 { redde 1; }/s//si n eq 99 { redde 1; }/'
mutant "numerus: length cap dropped"               's/si n gt 20 { redde 2; }//'
mutant "numerus: digit range off by one (high)"    '0,/si c gt 0x39 { redde 3; }/s//si c gt 0x3a { redde 3; }/'
mutant "numerus: digit range off by one (low)"     '0,/si c lt 0x30 { redde 3; }/s//si c lt 0x2f { redde 3; }/'
mutant "numerus: leading zero admitted"            's/    si n gt 1 {/    si n gt 99 {/'
mutant "numerus: a leading zero of only two digits" 's/    si n gt 1 {/    si n gt 2 {/'
mutant "onus: empty admitted"                      '/admitte_onus/,$ s/si n eq 0 { redde 1; }/si n eq 99 { redde 1; }/'
mutant "onus: length cap dropped"                  's/si n gt 13 { redde 2; }//'
mutant "onus: second dot admitted"                 's/si punctum eq 1 { redde 7; }//'
mutant "onus: leading dot admitted"                's/si integri eq 0 { redde 4; }//'
mutant "onus: integer leading zero admitted"       's/si initium eq 1 { redde 5; }//'
mutant "onus: seven integer digits admitted"       's/si integri ge 6 { redde 6; }//'
mutant "onus: seven fraction digits admitted"      's/si fracti ge 6 { redde 7; }//'
mutant "onus: trailing dot admitted"               's/si fracti eq 0 { redde 7; }//'
mutant "onus: digit range off by one"              '/admitte_onus/,$ s/si c gt 0x39 { redde 3; }/si c gt 0x3a { redde 3; }/'
mutant "onus: initium never set"                   's/si c eq 0x30 { initium = 1; }//'

cap_refused() {  # cap_refused NAME LINE
  local name=$1 line=$2
  sed "s/^publica functio admitte_numerum(b: acies<u8, 20>, n: mensura) -> u8 {/&\\n    $line/" "$unit" >"$work/cap.exsc"
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

sed 's/^publica functio admitte_numerum(b: acies<u8, 20>,/publica functio admitte_numerum(s: Scriptor, b: acies<u8, 20>,/' "$unit" >"$work/cap.exsc"
cmp -s "$work/cap.exsc" "$unit" && { echo "  [FAIL] Scriptor-parameter pattern moved"; fail=1; }
emit "$work/cap.exsc" "$work/cap.gen.c"
if "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$here/watchdawg_gate.h" "$work/cap.gen.c" 2>/dev/null; then
  echo "  [FAIL] capability 'Scriptor parameter' still matches watchdawg_gate.h"; fail=1
else
  echo "  [ok]   capability 'Scriptor parameter' breaks watchdawg_gate.h"
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

[ "$fail" -eq 0 ] && echo "watchdawg_gate: PASS" || { echo "watchdawg_gate: FAIL"; exit 1; }
