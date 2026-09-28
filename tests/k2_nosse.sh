#!/usr/bin/env bash
# K2.1: integer-only --emitte c must not carry the float/SIMD prologue
# and must compile with kernel-ish FPU flags.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXSC="${EXSC:-$ROOT/build/exsc}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

"$EXSC" aedifica --emitte c --hospes x86_64-linux \
  -o "$OUT/arith.c" \
  "$ROOT/tests/programs/arithmetica/arithmetica.exsc"

if grep -q 'vector_size' "$OUT/arith.c"; then
  echo "K2.1 FAIL: integer unit still contains vector_size" >&2
  grep -n 'vector_size' "$OUT/arith.c" | head >&2
  exit 1
fi
if grep -q 'exs_vf32' "$OUT/arith.c"; then
  echo "K2.1 FAIL: integer unit still contains vector float typedefs" >&2
  exit 1
fi
if ! grep -q '#define EXS_NEED_FLOAT 0' "$OUT/arith.c"; then
  echo "K2.1 FAIL: missing #define EXS_NEED_FLOAT 0" >&2
  head -n 5 "$OUT/arith.c" >&2
  exit 1
fi

gcc -c -ffreestanding -mno-sse -mno-sse2 -mno-mmx -mno-avx \
  -mgeneral-regs-only -mno-red-zone -fno-stack-protector \
  -o "$OUT/arith.o" "$OUT/arith.c"
echo "K2.1 PASS: integer unit has no SIMD prologue and compiled -mno-sse"
