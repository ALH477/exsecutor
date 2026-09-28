#!/usr/bin/env bash
# K2.1: integer-only --emitte c must compile with kernel-ish FPU flags
# and must not keep the float/SIMD prologue after preprocessing.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXSC="${EXSC:-$ROOT/build/exsc}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

"$EXSC" aedifica --emitte c --hospes x86_64-linux \
  -o "$OUT/arith.c" \
  "$ROOT/tests/programs/arithmetica/arithmetica.exsc"

if ! grep -q '^#define EXS_NEED_FLOAT 0$' "$OUT/arith.c"; then
  echo "K2.1 FAIL: expected #define EXS_NEED_FLOAT 0" >&2
  head -n 5 "$OUT/arith.c" >&2
  exit 1
fi

gcc -E -P -ffreestanding "$OUT/arith.c" -o "$OUT/arith.i"
if grep -q 'vector_size' "$OUT/arith.i"; then
  echo "K2.1 FAIL: preprocessed integer unit still contains vector_size" >&2
  grep -n 'vector_size' "$OUT/arith.i" | head >&2
  exit 1
fi
if grep -q 'exs_vf32' "$OUT/arith.i"; then
  echo "K2.1 FAIL: preprocessed integer unit still contains vector float types" >&2
  exit 1
fi

gcc -c -ffreestanding -mno-sse -mno-sse2 -mno-mmx -mno-avx \
  -mgeneral-regs-only -mno-red-zone -fno-stack-protector \
  -o "$OUT/arith.o" "$OUT/arith.c"
echo "K2.1 PASS: integer unit preprocesses without SIMD and compiled -mno-sse"
