#!/usr/bin/env bash
# examples/custos/proba_c.sh -- the C side of custos, checked the way Punctim's
# dcf-ws-bridge consumes it: exsc emits one C11 library unit from §14 entry
# 23's fixture, entry23/codex.exsc and custos.exsc; the unit is emitted twice
# and must be byte-identical; custos.h is compiled against it; proba.c (one
# anchor per verdict, plus SUPERPACK_SPEC.md's joint-CRC anchor 0x5B75) is
# built with every C compiler given, at -O0 and -O2, under UBSan, and must
# pass. Then three mutants of custos.exsc must each FAIL proba.c, so the
# anchors are known to see what they claim to.
#
# Usage: examples/custos/proba_c.sh [CC...]    (default: gcc clang)
# Needs build/exsc (make all). Writes only to a temp dir.
#
# What is NOT here: the vectors. Punctim owns its golden, SuperPack and medium
# vectors and does not vendor them here; its web/bridge runs this unit over
# all of them plus a differential sweep against its Rust codec. See README.md.
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

# The unit, in the order the import has to be: declaration, codec, gate.
unit=("$root/tests/conformance/entry23_demodframe_golden_vectors.exsc"
      "$root/tests/conformance/entry23/codex.exsc")

emit() { "$exsc" aedifica --hospes x86_64-linux --emitte c "${unit[@]}" "$1" -o "$2" >/dev/null 2>&1; }

emit "$here/custos.exsc" "$work/custos.gen.c"
emit "$here/custos.exsc" "$work/again.gen.c"
if cmp -s "$work/custos.gen.c" "$work/again.gen.c"; then
  echo "  [ok]   emitted twice, byte-identical ($(wc -c <"$work/custos.gen.c") bytes)"
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
    -include "$here/custos.h" -I "$here" "$gen" "$here/proba.c" -o "$out"
}

for cc in "${ccs[@]}"; do
  if ! command -v "$cc" >/dev/null; then
    echo "  [FAIL] $cc: not on PATH"; fail=1; continue
  fi
  for opt in -O0 -O2; do
    if build "$cc" "$opt" "$work/custos.gen.c" "$work/proba" \
        && "$work/proba" >"$work/out.txt"; then
      echo "  [ok]   $cc $opt: $(tail -1 "$work/out.txt")"
    else
      echo "  [FAIL] $cc $opt"; cat "$work/out.txt" 2>/dev/null || true; fail=1
    fi
  done
done

# Mutants: each must make proba.c fail, or the anchors are not looking. A
# mutant failing proves nothing when the original fails too, so they run only
# after a clean baseline.
if [ "$fail" -ne 0 ]; then
  echo "custos: FAIL (baseline; mutants not run)"; exit 1
fi
mutant() {  # mutant NAME SED-EXPR
  local name=$1 expr=$2
  sed "$expr" "$here/custos.exsc" >"$work/mutant.exsc"
  if cmp -s "$work/mutant.exsc" "$here/custos.exsc"; then
    echo "  [FAIL] mutant '$name' changed nothing -- its pattern moved"; fail=1; return
  fi
  emit "$work/mutant.exsc" "$work/mutant.gen.c"
  if build "${ccs[0]}" -O0 "$work/mutant.gen.c" "$work/mproba" 2>/dev/null \
      && "$work/mproba" >/dev/null 2>&1; then
    echo "  [FAIL] mutant '$name' passed the anchors"; fail=1
  else
    echo "  [ok]   mutant '$name' is caught"
  fi
}
mutant "joint polynomial 0x1021 -> 0x1020" 's/(c sursum 1) aut 0x1021/(c sursum 1) aut 0x1020/'
mutant "core B version unchecked"          '/si s.b_versio ne 1/d'
mutant "sflags type 5 -> 4"                's/si s.genus ne 5/si s.genus ne 4/'
mutant "core A version unchecked"          '/si s.a_versio ne 1/d'
mutant "17-byte frames refused"            's/si n eq 17 {/si n eq 18 {/'

[ "$fail" -eq 0 ] && echo "custos: PASS" || { echo "custos: FAIL"; exit 1; }
