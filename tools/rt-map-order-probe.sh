#!/usr/bin/env bash
# tools/rt-map-order-probe.sh
# ---------------------------------------------------------------------------
# .claude/agents/asm-rt.md requires, for rt/map.inc specifically:
#
#   "write and run a probe that inserts a fixed key sequence under at least two
#    different memory-layout conditions and diffs the iteration order -- it must
#    be identical regardless of address"
#
# This is the harness for that. It is the mechanised form of CLAUDE.md's
# "Maps iterate in insertion order... do not substitute a faster map with
# address- or hash-dependent iteration", which §9.3's byte-identical-output
# guarantee rests on.
#
# THE PROBLEM: exsc is a static, non-PIE ELF loaded at a fixed address, so
# there is no image-level ASLR to vary. But Arena and Map storage comes from
# mmap, not from the image -- and mmap placement shifts deterministically with
# how many mmap calls preceded it. Two fixtures that differ only in how many
# throwaway pages they map first are therefore *guaranteed* to put the map at
# two different addresses, rather than merely likely to.
#
# THE CONTRACT (asm-rt owns these two files; this script only consumes them):
#
#   tests/unit/probe_map_order_pad0.asm
#   tests/unit/probe_map_order_pad7.asm
#
#   Each must:
#     - mmap PAD_PAGES throwaway pages first (0 and 7 respectively),
#     - create an arena and a map,
#     - insert the same fixed, deliberately-unsorted key sequence,
#     - write each key to stdout in map_iterate order, raw bytes, nothing else,
#     - exit_group(0).
#
#   Any difference in the two stdout captures means iteration order moved with
#   the address, which is a defect in rt/map.inc, not in this script.
#
# Exit: 0 identical, 1 differed, 2 fixtures absent or toolchain missing.
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FASMG="${FASMG:-fasmg}"
export INCLUDE="${INCLUDE:-$REPO_ROOT/vendor/fasmg-x86}"

A_SRC="$REPO_ROOT/tests/unit/probe_map_order_pad0.asm"
B_SRC="$REPO_ROOT/tests/unit/probe_map_order_pad7.asm"

echo "== rt/map.inc insertion-order probe =="

if ! command -v "$FASMG" >/dev/null 2>&1; then
  echo "probe: '$FASMG' not found on PATH (set FASMG=/path/to/fasmg)." >&2
  exit 2
fi

missing=0
for f in "$A_SRC" "$B_SRC"; do
  [[ -f "$f" ]] || { echo "probe: missing fixture ${f#"$REPO_ROOT"/}" >&2; missing=1; }
done
if [[ "$missing" == "1" ]]; then
  cat >&2 <<'EOF'

rt/map.inc has not been written yet, so its layout probe cannot run.

This is the honest "not yet" path, not a pass. The two fixtures named in this
script's header are asm-rt's to write, and this harness is deliberately in
place first so that the requirement is a contract rather than a note in a
report. Nothing here should be reported green until both fixtures exist and
this script exits 0.
EOF
  exit 2
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

for pair in "a:$A_SRC" "b:$B_SRC"; do
  tag="${pair%%:*}"; src="${pair#*:}"
  if ! "$FASMG" "$src" "$work/$tag" >"$work/$tag.log" 2>&1; then
    echo "probe: $(basename "$src") failed to assemble:" >&2
    sed 's/^/    /' "$work/$tag.log" >&2
    exit 1
  fi
  chmod +x "$work/$tag"
  if ! "$work/$tag" >"$work/$tag.out" 2>"$work/$tag.err"; then
    echo "probe: $(basename "$src") exited non-zero:" >&2
    sed 's/^/    /' "$work/$tag.err" >&2
    exit 1
  fi
  echo "  ran $(basename "$src") -> $(wc -c <"$work/$tag.out") bytes of iteration order"
done

if ! cmp -s "$work/a.out" "$work/b.out"; then
  echo
  echo "PROBE: FAIL -- iteration order changed with the map's backing address." >&2
  echo "rt/map.inc is ordering by something address- or hash-dependent." >&2
  echo "CLAUDE.md: maps iterate in insertion order; §9.3 depends on it." >&2
  echo >&2
  echo "pad0:" >&2; od -An -tx1 "$work/a.out" | sed 's/^/    /' >&2
  echo "pad7:" >&2; od -An -tx1 "$work/b.out" | sed 's/^/    /' >&2
  exit 1
fi

echo "  identical across pad0 / pad7 mmap layouts"

# Secondary, best-effort: the kernel randomises the mmap region per process
# even for a non-PIE binary, so two independent runs of one fixture are also a
# real (if weaker) layout variation. Degrades honestly when randomisation is
# off, in the style of tools/reproduce.sh's unshare fallback.
rvs="$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null || echo 0)"
if [[ "$rvs" == "0" ]]; then
  echo "  -    per-process mmap randomisation is off (randomize_va_space=0);"
  echo "       skipping the repeat-run check -- it would prove nothing here."
else
  "$work/a" >"$work/a2.out" 2>/dev/null
  if cmp -s "$work/a.out" "$work/a2.out"; then
    echo "  identical across two independent runs (randomize_va_space=$rvs)"
  else
    echo
    echo "PROBE: FAIL -- iteration order differed between two runs of the same" >&2
    echo "binary. Ordering depends on a per-process address." >&2
    exit 1
  fi
fi

echo
echo "PROBE: PASS -- iteration order identical across layouts"
exit 0
