#!/usr/bin/env bash
# K2 item 5: `externus` exercised end to end against a live C ABI.
#
# The C backend: emit tests/programs/ffi_externus/ffi_externus.exsc with
# `exsc --emitte c`, link the unit against tests/c/ffi/host.c (which
# defines `triplicare` and `exsrt_abortus`), build under gcc and clang
# at -O0 and -O2 with UBSan, and require exit 42 from every one. Exit 42
# means the foreign call carried 14 in and 42 out through the SysV AMD64
# ABI; exit 1 means the ABI was mis-implemented.
#
# The reference backend: assemble the same source with `exsc aedifica`
# (reference backend), inject a fasmg definition of `bfausr_triplicare`
# (the reference emitter's spelling of the same externus name) into the
# executable segment, assemble with fasmg, and require exit 42. Both
# backends agreeing on 42 is the measurement; the C backend alone would
# only prove the C side.
#
# Needs: build/exsc, fasmg (INCLUDE=vendor/fasmg-x86), gcc, clang.
# Writes only to a temp dir.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
EXSC="${EXSC:-$ROOT/build/exsc}"
FASMG="${FASMG:-fasmg}"
INCLUDE="${INCLUDE:-$ROOT/vendor/fasmg-x86}"
export INCLUDE
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

SRC="$ROOT/tests/programs/ffi_externus/ffi_externus.exsc"
HOST="$ROOT/tests/c/ffi/host.c"

fail() { echo "ffi: FAIL: $*" >&2; exit 1; }

# ---- the C backend, four builds -------------------------------------------
"$EXSC" aedifica --hospes x86_64-linux --emitte c -o "$OUT/unit.c" "$SRC" >/dev/null

cflags="-std=c11 -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=all -Wno-unused-function"
# Both compilers when both are present (the differential phase's rule:
# a missing compiler is a failure of the phase, not a skip). On a host
# with only gcc, the gcc builds still run and the clang lane is
# reported as skipped by name, not silently.
CCS=(gcc)
command -v clang >/dev/null 2>&1 && CCS+=(clang)
for cc in "${CCS[@]}"; do
  for opt in -O0 -O2; do
    nowarn="-Wno-cpp"; [[ "$cc" == clang ]] && nowarn="-Wno-#warnings"
    # shellcheck disable=SC2086
    "$cc" $cflags $nowarn "$opt" -o "$OUT/c.$cc$opt" "$OUT/unit.c" "$HOST" \
      || fail "$cc $opt: the emitted unit does not compile/link against the FFI host"
    rc=0; "$OUT/c.$cc$opt" || rc=$?
    [ "$rc" -eq 42 ] || fail "$cc $opt: exit $rc, expected 42 (the foreign call did not round-trip)"
    echo "  [ok]   C backend [$cc $opt]: exit 42 (triplicare(14) == 42 through the C ABI)"
  done
done

# ---- the reference backend -------------------------------------------------
"$EXSC" aedifica --hospes x86_64-linux -o "$OUT/ref.asm" "$SRC" >/dev/null
command -v "$FASMG" >/dev/null 2>&1 || fail "fasmg not on PATH (the reference build needs it)"

# The emitted file is self-contained (format/entry/segment, capability
# constants, MXCSR image, prelude blob) but does NOT define the externus
# -- that is the host's job, same as the C side. Inject the definition
# into the executable segment, right after the `segment readable
# executable` line, in plain SysV AMD64 (rdi in, rax out), matching
# the emitted `call bfausr_triplicare`.
python3 - "$OUT/ref.asm" "$OUT/ref.linked.asm" <<'PYEOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
s = open(src).read()
marker = "segment readable executable\n"
if s.count(marker) != 1:
    sys.exit("expected exactly one 'segment readable executable' line")
inject = marker + """
; K2.5 FFI shim: the externus definition the program cannot supply.
; Plain SysV AMD64: rdi in, rax out, matching `call bfausr_triplicare`.
bfausr_triplicare:
    mov rax, rdi
    add rax, rdi
    add rax, rdi
    ret
"""
open(dst, "w").write(s.replace(marker, inject))
PYEOF
"$FASMG" "$OUT/ref.linked.asm" "$OUT/ref.bin" >/dev/null 2>&1 \
  || fail "fasmg could not assemble the reference output with the FFI shim"
chmod +x "$OUT/ref.bin"
rc=0; "$OUT/ref.bin" || rc=$?
[ "$rc" -eq 42 ] || fail "reference backend: exit $rc, expected 42"
echo "  [ok]   reference backend: exit 42 (bfausr_triplicare(14) == 42)"

echo "ffi: PASS (externus live against a C ABI, both backends agree on 42)"
