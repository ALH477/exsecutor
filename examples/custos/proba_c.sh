#!/usr/bin/env bash
# examples/custos/proba_c.sh -- the C side of custos, checked the way Punctim's
# dcf-ws-bridge consumes it: exsc emits one C11 library unit from §14 entry
# 23's fixture, entry23/codex.exsc and custos.exsc; the unit is emitted twice
# and must be byte-identical; custos.h is compiled against it; proba.c (one
# anchor per verdict, plus SUPERPACK_SPEC.md's joint-CRC anchor 0x5B75) is
# built with every C compiler given, at -O0 and -O2, under UBSan, and must
# pass. Then five mutants of custos.exsc must each FAIL proba.c, so the
# anchors are known to see what they claim to.
#
# Then the capability half. `admitte` is pure by spec §4.1 rule 6: its
# declared row is empty (no `poscit`) AND it takes no capability parameter.
# Each half is held by a mutant: drawing ambient authority (`ambitus`,
# `sermo`) without declaring it must be refused by the checker as EXS-E0421,
# and taking a capability parameter (`s: Scriptor`, which rule 4 makes
# usable without `poscit`) must change the C prototype so that custos.h no
# longer compiles against the unit. A mutant that slipped either check would
# be a gate that could reach the host.
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

# Capability: ambient draws are refused by the checker, by code.
cap_refused() {  # cap_refused NAME LINE
  local name=$1 line=$2
  sed "s/^publica functio admitte(d: acies<u8, 32>, n: mensura) -> u8 {/&\n    $line/" \
    "$here/custos.exsc" >"$work/cap.exsc"
  if "$exsc" aedifica --hospes x86_64-linux --emitte c --diagnostica json "${unit[@]}" \
      "$work/cap.exsc" -o "$work/cap.gen.c" >/dev/null 2>"$work/cap.err"; then
    echo "  [FAIL] capability '$name' compiled -- an ambient draw was accepted"; fail=1
  elif grep -q '"code":"EXS-E0421"' "$work/cap.err"; then
    echo "  [ok]   capability '$name' refused EXS-E0421"
  else
    echo "  [FAIL] capability '$name' refused, but not as EXS-E0421:"
    head -c 300 "$work/cap.err"; echo; fail=1
  fi
}
cap_refused "ambient ambitus" 'firma z = ambitus;'
cap_refused "ambient sermo"   'firma z = "custos".plica_sermone(sermo);'

# Capability: a capability parameter breaks the C face.
sed 's/^publica functio admitte(d: acies<u8, 32>, n: mensura) -> u8 {/publica functio admitte(s: Scriptor, d: acies<u8, 32>, n: mensura) -> u8 {\n    firma z = s.scribe("custos");/' \
  "$here/custos.exsc" >"$work/cap.exsc"
emit "$work/cap.exsc" "$work/cap.gen.c"
if "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$here/custos.h" \
    "$work/cap.gen.c" 2>/dev/null; then
  echo "  [FAIL] capability 'Scriptor parameter' still matches custos.h"; fail=1
else
  echo "  [ok]   capability 'Scriptor parameter' breaks custos.h"
fi

# The process (filtrum.exsc): the reference backend, freestanding, audited.
# Its only authority is `ambitus` derived from `Mundus`; the audit must pass
# with exactly {Mundus, ambitus} and FAIL without `ambitus`, or it is not
# looking at the binary's real surface.
if command -v fasmg >/dev/null && [ -n "${INCLUDE:-}" ]; then
  if "$exsc" aedifica --hospes x86_64-linux "${unit[@]}" "$here/custos.exsc" "$here/filtrum.exsc" \
       -o "$work/filtrum.asm" >/dev/null 2>&1 && fasmg "$work/filtrum.asm" "$work/filtrum" >/dev/null 2>&1; then
    chmod +x "$work/filtrum"
    echo "  [ok]   filtrum built freestanding ($(wc -c <"$work/filtrum") bytes)"
    if "$root/tools/syscall-audit.sh" --potestates Mundus,ambitus "$work/filtrum" >/dev/null 2>&1; then
      echo "  [ok]   filtrum audit --potestates Mundus,ambitus: PASS"
    else
      echo "  [FAIL] filtrum audit --potestates Mundus,ambitus"; fail=1
    fi
    if "$root/tools/syscall-audit.sh" --potestates Mundus "$work/filtrum" >/dev/null 2>&1; then
      echo "  [FAIL] filtrum audit passed WITHOUT ambitus -- the audit saw nothing"; fail=1
    else
      echo "  [ok]   filtrum audit --potestates Mundus (no ambitus): refused"
    fi
    # Records: filler frame (0), filler SuperPack (0), bad-CRC frame (3),
    # a 200-byte record (6). Then a record truncated mid-way: exit 2, no verdict.
    hx() { printf '%s' "$1" | sed 's/../\\x&/g'; }
    { printf '\x11'; printf "$(hx d310000000000000000000000000005b80)"
      printf '\x20'; printf "$(hx d315100000000000000000000000000010000000000000000000000000005b75)"
      printf '\x11'; printf "$(hx d310000000000000000000000000005b81)"
      printf '\xc8'; head -c 200 /dev/zero; } >"$work/in.bin"
    got="$("$work/filtrum" <"$work/in.bin" | od -An -tu1 | tr -s ' ' | sed 's/^ //')"
    if [ "$got" = "0 0 3 6" ]; then echo "  [ok]   filtrum verdicts: $got"
    else echo "  [FAIL] filtrum verdicts: '$got' (want '0 0 3 6')"; fail=1; fi
    rc=0; printf '\x11\xd3\x10' | "$work/filtrum" >"$work/trunc.out" || rc=$?
    if [ "$rc" -eq 2 ] && [ ! -s "$work/trunc.out" ]; then echo "  [ok]   filtrum truncated record: exit 2, no verdict"
    else echo "  [FAIL] filtrum truncated record: exit $rc"; fail=1; fi
  else
    echo "  [FAIL] filtrum did not build"; fail=1
  fi
else
  echo "  [skip] filtrum: needs fasmg and INCLUDE (nix develop)"
fi

[ "$fail" -eq 0 ] && echo "custos: PASS" || { echo "custos: FAIL"; exit 1; }
