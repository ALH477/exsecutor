#!/usr/bin/env bash
# examples/dcfid_gate/proba_c.sh -- dcfid_gate checked the way DCF-ID (a Rust
# service) consumes it.
#
#   1. exsc emits the C unit, its C face (--emitte h) and its Rust face
#      (--emitte rs) twice from the ORDERED file list
#      `dcf_net_gate/dcf_net_gate.exsc dcfid_gate/dcfid_gate.exsc` (the shared
#      network gate first, as DCF-ID builds it); the pairs must be
#      byte-identical. dcfid_gate.h, the hand-written header, must match the
#      generated one prototype for prototype.
#   2. proba.c (a line-protocol host) is built against the unit with every C
#      compiler given, at -O0 and -O2, under UBSan, with the GENERATED header
#      force-included; one more gcc build runs under ASan+UBSan, because the
#      host hands the gate a buffer of exactly its declared size and a read
#      past it must be a failure, not a lucky answer. proba.py drives each
#      against independent definitions (a regular expression per gate, and a
#      left-to-right reference of the verdict tables) and checks every row of
#      the README's anchors table against both the reference and the gate.
#   3. behaviour mutants of dcfid_gate.exsc must each BUILD and then FAIL
#      proba.py -- a mutant that does not build proves nothing.
#   4. capability: the unit is pure by spec §4.1 rule 6. Drawing ambient
#      authority must be refused EXS-E0421; taking a capability parameter
#      must break the hand-written header and the stale generated one.
#   5. the Rust face, where cargo exists: tests/c/facies/crate links the unit
#      and calls the gate through the generated extern block.
#
# Usage: examples/dcfid_gate/proba_c.sh [CC...]   (default: gcc clang)
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
net="$root/examples/dcf_net_gate/dcf_net_gate.exsc"
unit="$here/dcfid_gate.exsc"

emit() { "$exsc" aedifica --hospes x86_64-linux --emitte "${3:-c}" "$net" "$1" -o "$2" >/dev/null 2>&1; }

for k in c h rs; do
  emit "$unit" "$work/gate.gen.$k" "$k"
  emit "$unit" "$work/again.gen.$k" "$k"
  if [ -s "$work/gate.gen.$k" ] && cmp -s "$work/gate.gen.$k" "$work/again.gen.$k"; then
    echo "  [ok]   --emitte $k emitted twice, byte-identical ($(wc -c <"$work/gate.gen.$k") bytes)"
  else
    echo "  [FAIL] --emitte $k: two emissions differ, or wrote nothing"; fail=1
  fi
done
python3 "$root/tests/c/facies/protos.py" "$here/dcfid_gate.h" "$work/gate.gen.h" \
  || { echo "  [FAIL] dcfid_gate.h and the generated header disagree"; fail=1; }

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
echo "$(grep -h 'README anchors' "$work/out.txt" | head -1)"
if build "${ccs[0]}" -O1 "$work/gate.gen.c" "$work/proba_asan" -fsanitize=address,undefined -fno-sanitize-recover=all 2>"$work/asan.err"; then
  if python3 "$here/proba.py" "$work/proba_asan" >"$work/out.txt" 2>&1; then
    echo "  [ok]   ${ccs[0]} -O1 ASan+UBSan: exact-size buffers, no read past them"
  else
    echo "  [FAIL] ${ccs[0]} -O1 ASan+UBSan"; head -20 "$work/out.txt"; fail=1
  fi
else
  echo "  [FAIL] ${ccs[0]} ASan build"; head -5 "$work/asan.err"; fail=1
fi

if [ "$fail" -ne 0 ]; then echo "dcfid_gate: FAIL (baseline; mutants not run)"; exit 1; fi

check_m() {  # check_m NAME  (the mutated source is in $work/m.exsc)
  local name=$1
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
mutant() {  # mutant NAME SED-EXPR
  sed "$2" "$unit" >"$work/m.exsc"
  check_m "$1"
}
pmut() {  # pmut NAME OLD NEW [NTH]: replace the NTH (default 1st; 0 = every) occurrence of OLD ("\n" is a newline)
  python3 - "$unit" "$work/m.exsc" "$2" "$3" "${4:-1}" <<'PY'
import sys
src = open(sys.argv[1]).read()
old = sys.argv[3].replace("\\n", "\n")
new = sys.argv[4].replace("\\n", "\n")
nth = int(sys.argv[5])
if nth == 0:
    out = src.replace(old, new)
else:
    parts = src.split(old)
    out = old.join(parts[:nth]) + new + old.join(parts[nth:]) if len(parts) > nth else src
open(sys.argv[2], "w").write(out)
PY
  check_m "$1"
}
NOM='/^publica functio admitte_nomen/,/^}/'
SIG='/^publica functio admitte_signum/,/^}/'
SUM='/^publica functio admitte_summam/,/^}/'
mutant "nomen: minimum length dropped"        "$NOM s/si n lt 3 { redde 3; }//"
mutant "nomen: maximum length dropped"        "$NOM s/si n gt 32 { redde 2; }//"
mutant "nomen: digit range off by one"        "$NOM s/si c le 0x39 { licet = 1; }/si c le 0x3a { licet = 1; }/"
mutant "nomen: upper-case range off by one"   "$NOM s/si c le 0x5a { licet = 1; }/si c le 0x5b { licet = 1; }/"
mutant "nomen: lower-case range off by one"   "$NOM s/si c le 0x7a { licet = 1; }/si c le 0x7b { licet = 1; }/"
mutant "nomen: '_' dropped"                   "$NOM s/si c eq 0x5f { licet = 1; }//"
mutant "nomen: '-' dropped"                   "$NOM s/si c eq 0x2d { licet = 1; }//"
mutant "nomen: '.' admitted for '-'"          "$NOM s/si c eq 0x2d { licet = 1; }/si c eq 0x2e { licet = 1; }/"
mutant "nomen: bytes >= 0x80 admitted"        "$NOM s/si licet eq 0 { redde 4; }/si licet eq 0 { si c lt 0x80 { redde 4; } }/"
mutant "signum: token length is the id's"     "$SIG s/longitudo = 32;/longitudo = 64;/"
mutant "signum: unknown genus is an id"       "$SIG s/^        redde 1;/        longitudo = 64;/"
mutant "signum: length unchecked"             "$SIG s/si n ne longitudo { redde 2; }//"
mutant "signum: '-' admitted"                 "$SIG s/si licet eq 0 { redde 3; }/si c eq 0x2d { licet = 1; }\\n        si licet eq 0 { redde 3; }/"
mutant "signum: lower-case range off by one"  "$SIG s/si c le 0x7a { licet = 1; }/si c le 0x7b { licet = 1; }/"
mutant "signum: digit range off by one"       "$SIG s/si c ge 0x30 { si c le 0x39/si c ge 0x2f { si c le 0x39/"
mutant "summam: floor is 249"                 "$SUM s/si c lt 250/si c lt 249/"
mutant "summam: floor is 251"                 "$SUM s/si c lt 250/si c lt 251/"
mutant "summam: ceiling is 10001"             "$SUM s/si c gt 10000/si c gt 10001/"
mutant "summam: ceiling is 9999"              "$SUM s/si c gt 10000/si c gt 9999/"
pmut "forma: tenth item admitted"             'si commata ge 8 { redde 6; }' 'si commata ge 9 { redde 6; }' 0
pmut "forma: ninth item test dropped (t)"     'si commata ge 8 { redde 6; }' '' 1
pmut "forma: ninth item test dropped (v)"     'si commata ge 8 { redde 6; }' '' 2
pmut "forma: a second t admitted"             'si ts ge 1 { redde 7; }' ''
pmut "forma: no t admitted"                   'si ts eq 0 { redde 8; }' ''
pmut "forma: no v1 admitted"                  'si vs eq 0 { redde 9; }' ''
pmut "forma: v0 counts as v1 (end)"           'si genus eq 1 { vs = vs + 1; }' 'si genus ge 1 { vs = vs + 1; }' 2
pmut "forma: v0 counts as v1 (comma)"         'si genus eq 1 { vs = vs + 1; }' 'si genus ge 1 { vs = vs + 1; }' 1
pmut "forma: upper-case hex admitted"         'si c ge 0x61 { si c le 0x66 { hex = 1; } }' 'si c ge 0x61 { si c le 0x66 { hex = 1; } }\n                si c ge 0x41 { si c le 0x46 { hex = 1; } }'
pmut "forma: hex range off by one"            'si c le 0x66 { hex = 1; }' 'si c le 0x67 { hex = 1; }'
pmut "forma: short final v payload"           '        si cnt ne 64 { redde 5; }' '        si cnt lt 1 { redde 5; }'
pmut "forma: short mid-header v payload"      '                si cnt ne 64 { redde 5; }' '                si cnt lt 1 { redde 5; }'
# (cnt ge 64 -> cnt ge 65 is observably equivalent: the end-of-payload test cnt ne 64 gives the same verdict 5.
#  What the bound is FOR is the u8 counter, so the mutant drops it: a 256-byte payload must then trap.)
pmut "forma: v payload bound dropped (u8 wraps)" 'si cnt ge 64 { redde 5; }' ''
pmut "forma: 13-digit t admitted"             'si cnt ge 12 { redde 4; }' 'si cnt ge 13 { redde 4; }'
pmut "forma: non-digit in t"                  'si c gt 0x39 { redde 4; }' 'si c gt 0x3a { redde 4; }'
pmut "forma: empty t before a comma"          '                si cnt eq 0 { redde 4; }' '                si cnt eq 99 { redde 4; }'
pmut "forma: empty t at the end"              '        si cnt eq 0 { redde 4; }' '        si cnt eq 99 { redde 4; }'
pmut "forma: header ending in a prefix"       '        redde 3;\n    }\n    si ts eq 0' '        ts = ts + 0;\n    }\n    si ts eq 0'
pmut "forma: '=' not required after t"        'sin status eq 1 {\n            si c ne 0x3d { redde 3; }' 'sin status eq 1 {'
pmut "forma: '=' not required after v1"       'sin status eq 3 {\n            si c ne 0x3d { redde 3; }' 'sin status eq 3 {'
pmut "forma: length cap dropped"              'si n gt 512 { redde 2; }' ''
pmut "forma: v2 admitted as v0"               'sin c eq 0x30 {\n                genus = 2;' 'sin c ge 0x30 {\n                genus = 2;'
pmut "forma: item start accepts any letter"   '} sin c eq 0x76 {\n                status = 2;' '} sin c ge 0x61 {\n                status = 2;'

cap_refused() {  # cap_refused NAME LINE
  local name=$1 line=$2
  sed "s/^publica functio admitte_summam(c: u64) -> u8 {/&\\n    $line/" "$unit" >"$work/cap.exsc"
  if cmp -s "$work/cap.exsc" "$unit"; then echo "  [FAIL] capability '$name': pattern moved"; fail=1; return; fi
  if "$exsc" aedifica --hospes x86_64-linux --emitte c --diagnostica json \
      "$net" "$work/cap.exsc" -o "$work/cap.gen.c" >/dev/null 2>"$work/cap.err"; then
    echo "  [FAIL] capability '$name' compiled"; fail=1
  elif grep -q '"code":"EXS-E0421"' "$work/cap.err"; then
    echo "  [ok]   capability '$name' refused EXS-E0421"
  else
    echo "  [FAIL] capability '$name' refused, but not as EXS-E0421"; fail=1
  fi
}
cap_refused "ambient ambitus" 'firma z = ambitus;'
cap_refused "ambient archivum" 'firma z = archivum;'

sed 's/^publica functio admitte_nomen(b: acies<u8, 32>,/publica functio admitte_nomen(s: Scriptor, b: acies<u8, 32>,/' "$unit" >"$work/cap.exsc"
cmp -s "$work/cap.exsc" "$unit" && { echo "  [FAIL] Scriptor-parameter pattern moved"; fail=1; }
emit "$work/cap.exsc" "$work/cap.gen.c"
if "${ccs[0]}" -std=c11 -fsyntax-only -Wno-unused-function -include "$here/dcfid_gate.h" "$work/cap.gen.c" 2>/dev/null; then
  echo "  [FAIL] capability 'Scriptor parameter' still matches dcfid_gate.h"; fail=1
else
  echo "  [ok]   capability 'Scriptor parameter' breaks dcfid_gate.h"
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
    let mut n = [0u8; 32];
    let mut s = [0u8; 64];
    let mut f = [0u8; 512];
    unsafe {
        n[..5].copy_from_slice(b"alice");
        if exs_admitte_nomen(n.as_mut_ptr(), 5) != 0 { return 1; }
        n[..2].copy_from_slice(b"ab");
        if exs_admitte_nomen(n.as_mut_ptr(), 2) != 3 { return 2; }
        s[..32].copy_from_slice(&[b'a'; 32]);
        if exs_admitte_signum(s.as_mut_ptr(), 32, 1) != 0 { return 3; }
        if exs_admitte_signum(s.as_mut_ptr(), 32, 0) != 2 { return 4; }
        if exs_admitte_signum(s.as_mut_ptr(), 32, 7) != 1 { return 5; }
        if exs_admitte_summam(250) != 0 { return 6; }
        if exs_admitte_summam(u64::MAX) != 2 { return 7; }
        let h = b"t=1492774577,v1=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        f[..h.len()].copy_from_slice(h);
        if exs_admitte_formam_signaturae(f.as_mut_ptr(), h.len() as u64) != 0 { return 8; }
        if exs_admitte_formam_signaturae(f.as_mut_ptr(), 4) != 9 { return 9; }
        // the shared network gate is in the same unit
        let mut a = [0u8; 16];
        a[..7].copy_from_slice(b"1.2.3.4");
        if exs_admitte_ipv4(a.as_mut_ptr(), 7) != 0 { return 10; }
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
    echo "  [ok]   Rust: the generated extern block links the unit; nomen, signum, summam, forma and the shared ipv4 gate answer"
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

[ "$fail" -eq 0 ] && echo "dcfid_gate: PASS" || { echo "dcfid_gate: FAIL"; exit 1; }
