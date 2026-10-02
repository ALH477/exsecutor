#!/usr/bin/env bash
# examples/arca/proba_c.sh -- arca checked the way reliquary consumes it.
#
#   1. exsc emits the C unit twice; the two must be byte-identical. So must
#      its generated C face (`--emitte h`) and Rust face (`--emitte rs`,
#      docs/design/c-backend.md D9). arca.h, the hand-written header, must
#      declare a subset of the generated one with the same prototypes.
#   2. proba.c (the host loop reliquary runs in Rust) is built against the unit
#      with every C compiler given, at -O0 and -O2, under UBSan, with the
#      GENERATED header force-included -- and proba.c's own `#include
#      "arca.h"` then holds the two headers to each other.
#   3. proba.py runs each build: reliquary's own archives admitted with the
#      member list `tar -tvf` reads, a hostile corpus refused verdict by
#      verdict, and a 3000-mutant fuzz in which every admitted mutant must be
#      read by GNU tar as exactly the members, files and directories the gate
#      listed.
#   4. behaviour mutants of arca.exsc must each FAIL proba.py.
#   5. capability: arca is pure by spec §4.1 rule 6. Drawing ambient
#      authority must be refused EXS-E0421; taking a capability parameter
#      must break arca.h, and the header generated from the unmutated
#      source (the stale consumer).
#   6. the Rust face, where cargo exists: tests/c/facies/crate links the unit
#      and calls caput_iudica, nomen_iudica and saltus through the generated
#      extern block; the Scriptor mutant's regenerated block must refuse to
#      compile that same consumer.
#
# arca.h is KEPT beside the generated header because it says what a
# generator cannot -- how many bytes `h` and `l` point at, that neither is
# written, what 0xffffffffffffffff from magnitudo means -- and because
# reliquary's API is four of the unit's five publica functions (`octalis` is
# publica for the fuzz harness, not for a host). Step 1's subset check and
# step 2's force-include fail the moment it drifts.
#
# Usage: examples/arca/proba_c.sh [CC...]   (default: gcc clang)
# Needs build/exsc (make all), python3 and GNU tar; cargo for step 6
# (skipped, by name, without it). Writes only to a temp dir.
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

emit() { "$exsc" aedifica --hospes x86_64-linux --emitte "${3:-c}" "$1" -o "$2" >/dev/null 2>&1; }

for k in c h rs; do
  emit "$here/arca.exsc" "$work/arca.gen.$k" "$k"
  emit "$here/arca.exsc" "$work/again.gen.$k" "$k"
  if [ -s "$work/arca.gen.$k" ] && cmp -s "$work/arca.gen.$k" "$work/again.gen.$k"; then
    echo "  [ok]   --emitte $k emitted twice, byte-identical ($(wc -c <"$work/arca.gen.$k") bytes)"
  else
    echo "  [FAIL] --emitte $k: two emissions differ, or wrote nothing"; fail=1
  fi
done
python3 "$root/tests/c/facies/protos.py" "$here/arca.h" "$work/arca.gen.h" \
  || { echo "  [FAIL] arca.h and the generated header disagree"; fail=1; }

build() {   # build CC OPT UNIT OUT
  local cc=$1 opt=$2 gen=$3 out=$4 san
  san=(-fsanitize=undefined -fno-sanitize-recover=all)
  if "$cc" --version 2>/dev/null | head -1 | grep -qi clang; then
    san=(-fsanitize=undefined -fsanitize-trap=undefined)
  fi
  "$cc" -std=c11 "$opt" -Wall -Wextra -Werror -Wno-unused-function "${san[@]}" \
    -include "$work/arca.gen.h" -I "$here" "$gen" "$here/proba.c" -o "$out"
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
emit "$work/cap.exsc" "$work/cap.gen.h" h
if ! "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$work/cap.gen.h" "$work/cap.gen.c" 2>/dev/null; then
  echo "  [FAIL] the Scriptor mutant's own generated header does not compile against it"; fail=1
elif "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$work/arca.gen.h" "$work/cap.gen.c" 2>"$work/stale.err"; then
  echo "  [FAIL] capability 'Scriptor parameter' still matches the generated header"; fail=1
elif grep -q 'conflicting types' "$work/stale.err"; then
  echo "  [ok]   capability 'Scriptor parameter' breaks the stale generated header (conflicting types)"
else
  echo "  [FAIL] the stale generated header is refused, but not for conflicting types"; fail=1
fi

if command -v cargo >/dev/null; then
  cat >"$work/consumer.rs" <<'RS'
fn consume() -> i32 {
    let mut h = [0u8; 512];
    let mut l = [0u8; 4096];
    let mut b = [0u8; 4096];
    unsafe {
        // a zero block ends the archive (3), or follows a long name (25)
        if exs_caput_iudica(h.as_mut_ptr(), l.as_mut_ptr(), 0, 0) != 3 { return 1; }
        if exs_caput_iudica(h.as_mut_ptr(), l.as_mut_ptr(), 0, 1) != 25 { return 2; }
        b[..6].copy_from_slice(b"a/../x");
        if exs_nomen_iudica(b.as_mut_ptr(), 6) != 21 { return 3; }
        b[..2].copy_from_slice(b"/x");
        if exs_nomen_iudica(b.as_mut_ptr(), 2) != 20 { return 4; }
        if exs_saltus(1) != 512 || exs_saltus(512) != 512 || exs_saltus(0) != 0 { return 5; }
    }
    0
}
RS
  cp -r "$root/tests/c/facies/crate" "$work/crate"
  rs_build() {  # UNIT FACE
    ( cd "$work/crate" && EXS_FACIES_UNIT="$1" EXS_FACIES_RS="$2" EXS_FACIES_USE="$work/consumer.rs" \
        CC="${ccs[0]}" CARGO_TARGET_DIR="$work/crate/target" cargo build --offline --quiet ) >"$work/cargo.log" 2>&1
  }
  if rs_build "$work/arca.gen.c" "$work/arca.gen.rs" && "$work/crate/target/debug/facies"; then
    echo "  [ok]   Rust: the generated extern block links the unit; caput_iudica, nomen_iudica, saltus answer"
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

[ "$fail" -eq 0 ] && echo "arca: PASS" || { echo "arca: FAIL"; exit 1; }
