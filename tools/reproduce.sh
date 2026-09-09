#!/usr/bin/env bash
# tools/reproduce.sh -- spec §9.3: "Byte-identical output for identical
# inputs across directories, times, locales, hostnames." `exsc
# proba-reproducibilitatem` (§9.3, §14 entry 16) applied to the compiler's
# own build, ahead of there being an `exsc` to run that subcommand.
#
# Builds the same source twice under deliberately divergent ambient
# conditions -- working directory, TZ, LC_ALL/LANG, SOURCE_DATE_EPOCH,
# umask, and (best-effort) hostname -- and `cmp`s the two outputs
# byte-for-byte.
#
# If compiler/x86_64/exsc.asm does not exist yet (it does not, as of this
# writing -- see CLAUDE.md), this falls back to reproducing the
# tests/unit/ toolchain fixture instead, so the harness itself is proven
# correct ahead of having a real target. It says so loudly: a fixture pass
# is NOT evidence exsc is reproducible, only that this script can tell the
# difference when it matters.
#
# Exit status: 0 = byte-identical. 1 = outputs differ. 2 = usage/environment
# error (fasmg missing, nothing to build).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FASMG="${FASMG:-fasmg}"
INCLUDE_DIR="${INCLUDE:-$REPO_ROOT/vendor/fasmg-x86}"

if ! command -v "$FASMG" >/dev/null 2>&1; then
  echo "reproduce: '$FASMG' not found on PATH (set FASMG=/path/to/fasmg)" >&2
  exit 2
fi

TESTING_FIXTURE=0
SRC="$REPO_ROOT/compiler/x86_64/exsc.asm"
if [[ ! -f "$SRC" ]]; then
  echo "reproduce: compiler/x86_64/exsc.asm does not exist yet -- there is no"
  echo "exsc to build. Falling back to tests/unit/clean_syscalls.asm so this"
  echo "harness itself is proven correct."
  echo ">>> THIS RUN TESTS THE FIXTURE, NOT exsc. Re-run once exsc.asm exists. <<<"
  SRC="$REPO_ROOT/tests/unit/clean_syscalls.asm"
  TESTING_FIXTURE=1
  if [[ ! -f "$SRC" ]]; then
    echo "reproduce: fixture $SRC is also missing -- nothing to build" >&2
    exit 2
  fi
fi

# Best-effort hostname variation: an unprivileged user+UTS namespace lets
# us call hostname(1) without touching the real system. Probed once; if
# unavailable (no `unshare`, or unprivileged user namespaces disabled --
# both real possibilities in a locked-down CI or container), that one axis
# is honestly skipped rather than silently faked.
HOSTNAME_VARY=0
if command -v unshare >/dev/null 2>&1 \
   && unshare --uts -r true >/dev/null 2>&1; then
  HOSTNAME_VARY=1
fi

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

DIR_A="$WORKDIR/build-a/deep/nested/dir1"
DIR_B="$WORKDIR/build-b/other/dir2"
mkdir -p "$DIR_A" "$DIR_B"
SRC_A="$DIR_A/$(basename "$SRC")"
SRC_B="$DIR_B/$(basename "$SRC")"
cp "$SRC" "$SRC_A"
cp "$SRC" "$SRC_B"
OUT_A="$DIR_A/out.bin"
OUT_B="$DIR_B/out.bin"

# build_one WORKDIR SRC OUT TZ LOCALE EPOCH UMASK HOSTNAME
build_one() {
  local workdir="$1" src="$2" out="$3" tz="$4" locale="$5" epoch="$6" umask_val="$7" host="$8"
  local inner
  inner="cd '$workdir' && umask '$umask_val' && env -i PATH='$PATH' HOME='$HOME' \
TZ='$tz' LC_ALL='$locale' LANG='$locale' SOURCE_DATE_EPOCH='$epoch' \
INCLUDE='$INCLUDE_DIR' '$FASMG' '$src' '$out' >/dev/null"
  if [[ "$HOSTNAME_VARY" -eq 1 ]]; then
    unshare --uts -r -- bash -c "hostname '$host' && $inner"
  else
    bash -c "$inner"
  fi
}

echo
echo "reproduce: condition set A"
echo "  cwd=$DIR_A TZ=UTC LC_ALL=C SOURCE_DATE_EPOCH=0 umask=022 hostname=repro-host-a$([[ $HOSTNAME_VARY -eq 0 ]] && echo ' (skipped: unshare unavailable)')"
build_one "$DIR_A" "$SRC_A" "$OUT_A" "UTC" "C" "0" "022" "repro-host-a"

echo "reproduce: condition set B"
echo "  cwd=$DIR_B TZ=Pacific/Kiritimati LC_ALL=C.UTF-8 SOURCE_DATE_EPOCH=999999999 umask=077 hostname=repro-host-b$([[ $HOSTNAME_VARY -eq 0 ]] && echo ' (skipped: unshare unavailable)')"
build_one "$DIR_B" "$SRC_B" "$OUT_B" "Pacific/Kiritimati" "C.UTF-8" "999999999" "077" "repro-host-b"
echo

if [[ "$TESTING_FIXTURE" -eq 1 ]]; then
  echo ">>> reminder: this run built tests/unit/clean_syscalls.asm, NOT exsc <<<"
fi

if [[ ! -f "$OUT_A" || ! -f "$OUT_B" ]]; then
  echo "reproduce: a build did not produce output" >&2
  exit 2
fi

if cmp -s "$OUT_A" "$OUT_B"; then
  echo "REPRODUCE: PASS -- byte-identical output ($(wc -c < "$OUT_A") bytes) across divergent cwd/TZ/locale/SOURCE_DATE_EPOCH/umask$([[ $HOSTNAME_VARY -eq 1 ]] && echo '/hostname')"
  exit 0
else
  echo "REPRODUCE: FAIL -- outputs differ" >&2
  echo "  A: $OUT_A ($(wc -c < "$OUT_A") bytes)" >&2
  echo "  B: $OUT_B ($(wc -c < "$OUT_B") bytes)" >&2
  echo "  first difference (cmp):" >&2
  cmp "$OUT_A" "$OUT_B" >&2 || true
  echo "  differing bytes (cmp -l, first 20 shown):" >&2
  cmp -l "$OUT_A" "$OUT_B" 2>/dev/null | head -20 >&2 || true
  exit 1
fi
