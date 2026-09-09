#!/usr/bin/env bash
# tools/publish-gate.sh
# ---------------------------------------------------------------------------
# The condition for making this repository public, as a command rather than a
# judgement: "when the hello world is working and proven."
#
# `working` and `proven` are not the same claim, so this checks both. Working
# is: exsc compiles examples/saluta.exsc and the result runs. Proven is: it
# produces exactly the expected bytes, reproducibly, with every existing check
# still green -- and by this project's own standard (CLAUDE.md), a thing is
# proven when a command says so, not when someone reports it did.
#
# It exits nonzero today, on purpose, and says why. That is the honest state.
#
# Exit: 0 gate MET, 1 gate NOT met, 2 environment problem.
# ---------------------------------------------------------------------------
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

EXSC="build/exsc"
SRC="examples/saluta.exsc"
GOLD="examples/saluta.expected"
# spec §9.5: "`exsc` with no `--hospes` is an error. No default-to-build-
# platform." This gate first invoked a bare `exsc SRC OUT`, which that rule
# makes permanently impossible -- it would have failed at step 3 forever, and
# a gate that cannot pass is worse than no gate. The invocation shape is
# pinned in spec §12.
HOSPES="${HOSPES:-x86_64-linux}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

fail=0
no()  { fail=1; echo "  [NOT MET] $*"; }
yes() { echo "  [met]     $*"; }
note(){ echo "  -         $*"; }

echo "== publish gate: examples/saluta.exsc working and proven =="

# --- 1. there must be a compiler -----------------------------------------
if [[ ! -f compiler/x86_64/exsc.asm ]]; then
  no "no compiler: compiler/x86_64/exsc.asm does not exist"
  note "spec §16 puts this at the end of Stage 3 -- the frontend (Stage 1),"
  note "the type checker (Stage 2), and the C backend (Stage 3) all stand"
  note "between here and a saluta.exsc that runs. Everything below is"
  note "unreachable until then and is not evaluated."
  echo
  echo "RESULT: GATE NOT MET -- do not publish"
  exit 1
fi
yes "compiler source exists"

# --- 2. it must build -----------------------------------------------------
if make >/dev/null 2>&1 && [[ -x "$EXSC" ]]; then yes "exsc builds"; else no "exsc does not build"; fi

# --- 3. working: it compiles and runs -------------------------------------
if [[ -x "$EXSC" ]] && "$EXSC" aedifica --hospes "$HOSPES" "$SRC" -o "$WORK/saluta" >"$WORK/log" 2>&1; then
  yes "exsc compiles $SRC"
  if "$WORK/saluta" >"$WORK/out" 2>/dev/null; then yes "the result runs"
  else no "the result does not run"; fi
else
  no "exsc cannot compile $SRC"; sed 's/^/            /' "$WORK/log" 2>/dev/null | head -5
fi

# --- 4. proven: exact bytes ----------------------------------------------
if [[ -f "$WORK/out" ]] && cmp -s "$WORK/out" "$GOLD"; then
  yes "output is byte-identical to $GOLD"
else
  no "output does not match $GOLD"
fi

# --- 5. proven: reproducible (spec §9.3) ----------------------------------
if [[ -x "$EXSC" ]]; then
  ( cd "$WORK" && TZ=UTC LC_ALL=C \
      "$ROOT/$EXSC" aedifica --hospes "$HOSPES" "$ROOT/$SRC" -o a >/dev/null 2>&1 )
  ( cd /tmp && TZ=Asia/Tokyo LC_ALL=tr_TR.UTF-8 \
      "$ROOT/$EXSC" aedifica --hospes "$HOSPES" "$ROOT/$SRC" -o "$WORK/b" >/dev/null 2>&1 )
  if [[ -f "$WORK/a" && -f "$WORK/b" ]] && cmp -s "$WORK/a" "$WORK/b"; then
    yes "byte-identical across directory, TZ and locale (§9.3)"
  else
    no "not reproducible across ambient conditions (§9.3)"
  fi
fi

# --- 6. everything else already green ------------------------------------
for c in "tests/run.sh" "tools/spec-check.sh" "tools/syscall-audit.sh --self-test"; do
  if $c >/dev/null 2>&1; then yes "$c"; else no "$c"; fi
done
if command -v nix >/dev/null 2>&1; then
  if nix flake check >/dev/null 2>&1; then yes "nix flake check"; else no "nix flake check"; fi
else
  note "nix absent -- flake checks not evaluated"
fi

echo
if [[ "$fail" -ne 0 ]]; then echo "RESULT: GATE NOT MET -- do not publish"; exit 1; fi
echo "RESULT: GATE MET"
echo
echo "Publishing is still a human decision, not this script's. Before it:"
echo "  - CONTRIBUTING.md, CODE_OF_CONDUCT.md, SECURITY.md, AUTHORS, CITATION.cff"
echo "  - .github/workflows CI running these same checks"
echo "  - a README written for a stranger, not for the author"
echo "  - confirm LICENSE.EXCEPTION does NOT apply to vendor/ (it keeps its own)"
exit 0
