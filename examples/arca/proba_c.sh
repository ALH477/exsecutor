#!/usr/bin/env bash
# examples/arca/proba_c.sh -- arca checked the way reliquary consumes it.
#
#   1. exsc emits the C unit twice; the two must be byte-identical.
#   2. proba.c (the host loop reliquary runs in Rust) is built against the unit
#      with every C compiler given, at -O0 and -O2, under UBSan, with arca.h
#      force-included so a drifted signature is a compile error.
#   3. proba.py runs each build: reliquary's own archives admitted with the
#      member list `tar -tvf` reads, a hostile corpus refused verdict by
#      verdict, and a 3000-mutant fuzz in which every admitted mutant must be
#      read by GNU tar as exactly the members, files and directories the gate
#      listed.
#   4. behaviour mutants of arca.exsc must each FAIL proba.py.
#   5. capability: arca is pure by spec §4.1 rule 6. Drawing ambient
#      authority must be refused EXS-E0421; taking a capability parameter
#      must break arca.h.
#
# Usage: examples/arca/proba_c.sh [CC...]   (default: gcc clang)
# Needs build/exsc (make all), python3 and GNU tar. Writes only to a temp dir.
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

emit() { "$exsc" aedifica --hospes x86_64-linux --emitte c "$1" -o "$2" >/dev/null 2>&1; }

emit "$here/arca.exsc" "$work/arca.gen.c"
emit "$here/arca.exsc" "$work/again.gen.c"
if cmp -s "$work/arca.gen.c" "$work/again.gen.c"; then
  echo "  [ok]   emitted twice, byte-identical ($(wc -c <"$work/arca.gen.c") bytes)"
else
  echo "  [FAIL] two emissions differ"; fail=1
fi

build() {   # build CC OPT UNIT OUT
  local cc=$1 opt=$2 gen=$3 out=$4 san
  san=(-fsanitize=undefined -fno-sanitize-recover=all)
  if "$cc" --version 2>/dev/null | head -1 | grep -qi clang; then
    san=(-fsanitize=undefined -fsanitize-trap=undefined)
  fi
  "$cc" -std=c11 "$opt" -Wall -Wextra -Werror -Wno-unused-function "${san[@]}" \
    -include "$here/arca.h" -I "$here" "$gen" "$here/proba.c" -o "$out"
}

for cc in "${ccs[@]}"; do
  command -v "$cc" >/dev/null || { echo "  [FAIL] $cc: not on PATH"; fail=1; continue; }
  for opt in -O0 -O2; do
    if build "$cc" "$opt" "$work/arca.gen.c" "$work/proba" \
        && python3 "$here/proba.py" "$work/proba" >"$work/out.txt" 2>&1; then
      echo "  [ok]   $cc $opt: $(grep -c '\[ok\]' "$work/out.txt") checks; $(grep -o '[0-9]* admitted, [0-9]* refused, 0 disagreements' "$work/out.txt")"
    else
      echo "  [FAIL] $cc $opt"; grep -v '\[ok\]' "$work/out.txt" | head -20; fail=1
    fi
  done
done

if [ "$fail" -ne 0 ]; then echo "arca: FAIL (baseline; mutants not run)"; exit 1; fi

mutant() {  # mutant NAME SED-EXPR
  local name=$1 expr=$2
  sed "$expr" "$here/arca.exsc" >"$work/m.exsc"
  if cmp -s "$work/m.exsc" "$here/arca.exsc"; then
    echo "  [FAIL] mutant '$name' changed nothing -- its pattern moved"; fail=1; return
  fi
  emit "$work/m.exsc" "$work/m.gen.c"
  if build "${ccs[0]}" -O0 "$work/m.gen.c" "$work/mproba" 2>/dev/null \
      && python3 "$here/proba.py" "$work/mproba" >/dev/null 2>&1; then
    echo "  [FAIL] mutant '$name' passed proba.py"; fail=1
  else
    echo "  [ok]   mutant '$name' is caught"
  fi
}
mutant "devices and links admitted"   's/si t ne 0x30 { redde 18; }/si t eq 0x99 { redde 18; }/'
mutant "dot-dot unchecked"            's/si b\[a + 1\] eq 0x2e { redde 21; }/si b[a + 1] eq 0x99 { redde 21; }/'
mutant "POSIX ustar magic admitted"   '/si h\[26[234]\] ne/d'
mutant "numeric fields unchecked"     '/si octalis(h, 100, 8) eq 0/d'
mutant "checksum unchecked"           's/si scripta ne summa { redde 16; }//'
mutant "long name not re-judged"      's/firma v = nomen_iudica(b, n);/firma v: u8 = 0;/'

cap_refused() {  # cap_refused NAME LINE
  local name=$1 line=$2
  sed "s/^publica functio saltus(m: u64) -> u64 {/&\\n    $line/" "$here/arca.exsc" >"$work/cap.exsc"
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

sed 's/^publica functio caput_iudica(h: acies<u8, 512>,/publica functio caput_iudica(s: Scriptor, h: acies<u8, 512>,/' \
  "$here/arca.exsc" >"$work/cap.exsc"
emit "$work/cap.exsc" "$work/cap.gen.c"
if "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$here/arca.h" "$work/cap.gen.c" 2>/dev/null; then
  echo "  [FAIL] capability 'Scriptor parameter' still matches arca.h"; fail=1
else
  echo "  [ok]   capability 'Scriptor parameter' breaks arca.h"
fi

[ "$fail" -eq 0 ] && echo "arca: PASS" || { echo "arca: FAIL"; exit 1; }
