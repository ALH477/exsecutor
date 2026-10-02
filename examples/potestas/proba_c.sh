#!/usr/bin/env bash
# examples/potestas/proba_c.sh -- potestas checked the way plugind consumes it.
#
#   1. exsc emits the C unit twice; the two must be byte-identical.
#   2. proba.c is built against the unit with every C compiler given, at -O0
#      and -O2, under UBSan, with potestas.h force-included so a drifted
#      signature is a compile error, and run: the anchors from plugind's own
#      Rust tests, and the boundaries of the rules they state.
#   3. behaviour mutants of potestas.exsc must each FAIL proba.c.
#   4. capability: potestas is pure by spec §4.1 rule 6. Drawing ambient
#      authority must be refused EXS-E0421; taking a capability parameter
#      must break potestas.h.
#
# The differential certification against plugind's Rust is the consumer's
# (Oligarchy, modules/oligarchy-plugins/potestas-cert); see README.md.
#
# Usage: examples/potestas/proba_c.sh [CC...]   (default: gcc clang)
# Needs build/exsc (make all). Writes only to a temp dir.
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

emit "$here/potestas.exsc" "$work/potestas.gen.c"
emit "$here/potestas.exsc" "$work/again.gen.c"
if cmp -s "$work/potestas.gen.c" "$work/again.gen.c"; then
  echo "  [ok]   emitted twice, byte-identical ($(wc -c <"$work/potestas.gen.c") bytes)"
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
    -include "$here/potestas.h" -I "$here" "$gen" "$here/proba.c" -o "$out"
}

for cc in "${ccs[@]}"; do
  command -v "$cc" >/dev/null || { echo "  [FAIL] $cc: not on PATH"; fail=1; continue; }
  for opt in -O0 -O2; do
    if build "$cc" "$opt" "$work/potestas.gen.c" "$work/proba" \
        && "$work/proba" >"$work/out.txt" 2>&1; then
      echo "  [ok]   $cc $opt: $(tail -1 "$work/out.txt")"
    else
      echo "  [FAIL] $cc $opt"; grep -v '\[ok\]' "$work/out.txt" | head -20; fail=1
    fi
  done
done

if [ "$fail" -ne 0 ]; then echo "potestas: FAIL (baseline; mutants not run)"; exit 1; fi

mutant() {  # mutant NAME SED-EXPR
  local name=$1 expr=$2
  sed "$expr" "$here/potestas.exsc" >"$work/m.exsc"
  if cmp -s "$work/m.exsc" "$here/potestas.exsc"; then
    echo "  [FAIL] mutant '$name' changed nothing -- its pattern moved"; fail=1; return
  fi
  emit "$work/m.exsc" "$work/m.gen.c"
  if build "${ccs[0]}" -O0 "$work/m.gen.c" "$work/mproba" 2>/dev/null \
      && ("$work/mproba" >/dev/null 2>&1; exit $?) 2>/dev/null; then
    echo "  [FAIL] mutant '$name' passed proba.c"; fail=1
  else
    echo "  [ok]   mutant '$name' is caught"
  fi
}
mutant "dot-dot unchecked"            '/si habet_regressum(p, np) eq 1 { redde 1; }/d'
mutant "relative path admitted"       's/si p\[0\] ne 0x24 { redde 1; }/si p[0] eq 0x99 { redde 1; }/'
mutant "contains direction dropped"   '/si subest(f, nf, p, np) eq 1 { redde 2; }/d'
mutant "byte prefix, not components"  '/si (ep - sp) ne (ef - sf) { redde 0; }/d'
mutant "dot components kept"          '/si (k - a) eq 1 { si b\[a\] eq 0x2e { nota = 1; } }/d'
mutant "empty components kept"        '/si (k - a) eq 0 { nota = 1; }/d'
mutant "leading-dot prefix matched"   '/si f\[1\] eq 0x2f { redde 0; }/d'
mutant "empty prefix matches nothing" 's/si nf eq 0 { redde 1; }/si nf eq 0 { redde 0; }/'
mutant "anchor without the dollar"    's/        si p\[0\] eq 0x24 {/        si p[0] ne 0x99 {/'
mutant "absolute cap unanchored"      's/    si p\[0\] ne 0x2f {/    si p[0] ne 0x99 {/'
mutant "dollar inside admitted"       's/        si p\[i\] eq 0x24 { redde 0; }/        si p[i] eq 0x99 { redde 0; }/'
mutant "variable as a prefix"         's/    si p\[k\] eq 0x2f { redde k; }/    redde k;/'
mutant "id admits '.' and '/'"        's/si c eq 0x2d { bonus = 1; }/si c le 0x2f { bonus = 1; }/'
mutant "id length unbounded"          '/si n gt 64 { redde 2; }/d'
mutant "W^X on a wasm unit"           '/si ordo eq 0 { redde 0; }/d'
mutant "jit=self still enforced"      '/si jit eq 2 { redde 0; }/d'
mutant "microvm read as native"       '/si ordo eq 3 { redde 1; }/d'
mutant "concession off bwrap tiers"   's/redde involucrum(ordo);/redde 1;/'

cap_refused() {  # cap_refused NAME LINE
  local name=$1 line=$2
  sed "s/^publica functio wx_cogitur(ordo: u8, jit: u8) -> u8 {/&\\n    $line/" "$here/potestas.exsc" >"$work/cap.exsc"
  if cmp -s "$work/cap.exsc" "$here/potestas.exsc"; then
    echo "  [FAIL] capability '$name' changed nothing -- its pattern moved"; fail=1; return
  fi
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

sed 's/^publica functio tangit(p: acies<u8, 4096>,/publica functio tangit(s: Scriptor, p: acies<u8, 4096>,/' \
  "$here/potestas.exsc" >"$work/cap.exsc"
if cmp -s "$work/cap.exsc" "$here/potestas.exsc"; then
  echo "  [FAIL] capability 'Scriptor parameter' changed nothing -- its pattern moved"; fail=1
elif ! emit "$work/cap.exsc" "$work/cap.gen.c"; then
  echo "  [FAIL] capability 'Scriptor parameter' did not compile, so it shows nothing"; fail=1
elif "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$here/potestas.h" "$work/cap.gen.c" 2>/dev/null; then
  echo "  [FAIL] capability 'Scriptor parameter' still matches potestas.h"; fail=1
else
  echo "  [ok]   capability 'Scriptor parameter' breaks potestas.h"
fi

[ "$fail" -eq 0 ] && echo "potestas: PASS" || { echo "potestas: FAIL"; exit 1; }
