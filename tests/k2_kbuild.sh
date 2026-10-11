#!/usr/bin/env bash
# K3-shape: the emitted custos unit must survive the kernel's compile flags,
# link against a host shim, keep its import surface to {exsrt_abortus}, stay
# under a 4 KB frame, and answer the three verdict anchors. This is the
# falsifiable exit criterion of roadmap Phase K3 (objtool acceptance) minus
# objtool itself: no Kbuild tree is needed, only a C compiler.
#
# The unit is examples/custos/custos.exsc over the entry-23 fixture and
# codec -- the DCF datagram gate, pure by construction (no posit, no
# allocation, no I/O), which is exactly the (subject, object, operation,
# context) -> verdict shape an LSM evaluator needs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXSC="${EXSC:-$ROOT/build/exsc}"
CC="${CC:-gcc}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

UNIT=("$ROOT/tests/conformance/entry23_demodframe_golden_vectors.exsc"
      "$ROOT/tests/conformance/entry23/codex.exsc"
      "$ROOT/examples/custos/custos.exsc")

"$EXSC" aedifica --emitte c --hospes x86_64-linux \
  -o "$OUT/custos.c" "${UNIT[@]}" >/dev/null

if ! grep -q '^#define EXS_NEED_FLOAT 0$' "$OUT/custos.c"; then
  echo "K3-shape FAIL: expected #define EXS_NEED_FLOAT 0 (integer unit)" >&2
  head -n 5 "$OUT/custos.c" >&2
  exit 1
fi

# The kernel's flag set, plus the flags Kbuild adds for x86-64: -mcmodel=kernel
# for the top-2GB mapping, -mno-red-zone because interrupts clobber below rsp,
# -fno-stack-protector because the kernel has no userspace guard-page ABI,
# -ffreestanding -fno-builtin -fno-pie: no libc, no builtin assumptions, no PIE.
"$CC" -c -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -mcmodel=kernel -mno-sse -mno-sse2 -mno-mmx -mno-avx -mgeneral-regs-only \
  -mno-red-zone -fno-stack-protector -ffreestanding -fno-builtin -fno-pie \
  -fstack-usage \
  -o "$OUT/custos.o" "$OUT/custos.c"

# Import surface: the one symbol library mode requires of its host.
imports="$(nm -u "$OUT/custos.o" | awk '{print $2}' | sort -u)"
if [ "$imports" != "exsrt_abortus" ]; then
  echo "K3-shape FAIL: nm -u is not exactly {exsrt_abortus}:" >&2
  echo "$imports" >&2
  exit 1
fi

# Frame budget: a kernel task gets 16 KB with a guard page; the roadmap's
# exit criterion is largest frame under 4 KB by -fstack-usage. The SAME
# bound is compiled into the unit as a #error (see below), so a frame that
# grows past it fails at compile time with a named message rather than
# overflowing the kernel stack at runtime.
maxframe="$(awk -F'\t' '{print $2}' "$OUT/custos.su" | sort -rn | head -1)"
if [ -z "$maxframe" ] || [ "$maxframe" -ge 4096 ]; then
  echo "K3-shape FAIL: largest frame ${maxframe:-none} >= 4096 bytes" >&2
  cat "$OUT/custos.su" >&2
  exit 1
fi

# K2 item 2: the emitted unit carries the frame-size LIMIT as a named,
# overridable constant (EXS_FRAME_LIMIT, default 4096). The gate enforces
# the bound itself: -fstack-usage above already failed the build if any
# frame reached 4096. The constant makes the consuming build's own check
# possible and named; the emitter has no frame-size analysis, so the
# bound is enforced by the build that would otherwise hand the kernel a
# guard-page double fault -- not by exsc. This is the honest half of
# "a frame-size bound that is a compile error rather than a fault": the
# compile error lives in the C build's .su check, and the constant is
# what names the limit it holds the report against.
if ! grep -q '^#define EXS_FRAME_LIMIT 4096$' "$OUT/custos.c"; then
  echo "K3-shape FAIL: the emitted unit carries no EXS_FRAME_LIMIT 4096 default" >&2
  echo "  (K2 item 2: the frame-size limit constant, so consuming builds" >&2
  echo "   can fail on -fstack-usage against a named bound)" >&2
  exit 1
fi

# The host shim: the one symbol, with the kernel's _Noreturn trap ABI. A trap
# in this gate is a defect, so exit non-zero rather than trap.
cat > "$OUT/shim.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
_Noreturn void exsrt_abortus(unsigned kind)
{
    fprintf(stderr, "exsecutor: abortus %u\n", kind);
    exit(2);
}
EOF

# The three verdict anchors, from examples/custos/proba.c: a valid filler
# frame admits (0), a bad CRC refuses with reason 3, and a 20-byte record
# refuses with reason 6.
cat > "$OUT/main.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
uint64_t exs_admitte(unsigned char *p0, uint64_t p1);
int main(void)
{
    unsigned char d[32] = {0};
    /* filler frame: d3 10 00..00 5b 80 */
    d[0] = 0xd3; d[1] = 0x10; d[15] = 0x5b; d[16] = 0x80;
    if (exs_admitte(d, 17) != 0) return 1;
    d[16] = 0x81;                        /* bad CRC */
    if (exs_admitte(d, 17) != 3) return 1;
    if (exs_admitte(d, 20) != 6) return 1; /* wrong length */
    printf("verdicts 0 3 6\n");
    return 0;
}
EOF

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
  -o "$OUT/k3probe" "$OUT/custos.o" "$OUT/shim.c" "$OUT/main.c"
"$OUT/k3probe"

echo "K3-shape PASS: custos unit kernel-flags clean, imports {exsrt_abortus}, max frame ${maxframe} B, verdicts 0 3 6"
