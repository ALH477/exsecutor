#!/usr/bin/env bash
# examples/metronomus/proba_c.sh -- the C side of metronomus, checked the way
# Kiln consumes it: exsc emits a C11 library unit per --hospes row, the
# hand-written metronomus.h is checked against each unit's definitions, and
# exemplum.c -- a two-peer lockstep/rollback session over a laggy link -- is
# built against the host row with every C compiler given and run under
# UBSan, its transcript compared byte for byte with exemplum.expected.
#
# Usage: examples/metronomus/proba_c.sh [CC...]    (default: gcc clang)
# Needs build/exsc (make all). Run from anywhere; writes only to a temp dir.
#
# What is NOT here: a console build. The mips64-none-o64 unit is only
# EMITTED (it asserts 32-bit pointers, which a host compiler does not have);
# running that row is tests/programs/metronomus/'s cross=yes, under qemu,
# which is where big-endian execution is actually measured.
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

for row in x86_64-linux mips64-none-o64; do
  "$exsc" aedifica --hospes "$row" --emitte c "$here/metronomus.exsc" \
    -o "$work/metronomus_$row.gen.c" >/dev/null 2>&1
  echo "emitted: metronomus_$row.gen.c ($(wc -c <"$work/metronomus_$row.gen.c") bytes)"
done

for cc in "${ccs[@]}"; do
  if ! command -v "$cc" >/dev/null; then
    echo "  [FAIL] $cc: not on PATH"; fail=1; continue
  fi
  # The header against the host row's definitions, compiled and never
  # linked: a conflicting type is an error. (Only the host row: the o64 unit
  # asserts 32-bit pointers, which a host compiler does not have.)
  if "$cc" -std=c11 -Wall -Wextra -Werror -Wno-unused-function -c -o /dev/null \
      -I "$here" -DMETRONOMUS_GEN="\"$work/metronomus_x86_64-linux.gen.c\"" \
      "$here/metronomus_proto.c"; then
    echo "  [ok]   $cc: metronomus.h agrees with the x86_64-linux unit"
  else
    echo "  [FAIL] $cc: metronomus.h disagrees with the generated unit"; fail=1
  fi
  san=(-fsanitize=undefined -fno-sanitize-recover=all)
  [ "$cc" = clang ] && san=(-fsanitize=undefined -fsanitize-trap=undefined)
  for opt in -O0 -O2; do
    bin="$work/exemplum_${cc##*/}$opt"
    "$cc" -std=c11 "$opt" -Wall -Wextra -Werror -Wno-unused-function "${san[@]}" \
      -I "$here" "$here/exemplum.c" "$work/metronomus_x86_64-linux.gen.c" -o "$bin"
    if "$bin" >"$bin.out" && cmp -s "$bin.out" "$here/exemplum.expected"; then
      echo "  [ok]   $cc $opt: exemplum exit 0, transcript byte-identical"
    else
      echo "  [FAIL] $cc $opt: exemplum exit or transcript differs"; fail=1
    fi
  done
done

[ "$fail" -eq 0 ] && echo "RESULT: ok" || echo "RESULT: FAIL"
exit "$fail"
