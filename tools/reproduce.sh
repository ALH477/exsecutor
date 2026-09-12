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
# TWO THINGS ARE REPRODUCED, not one. First `exsc` itself: fasmg over
# compiler/x86_64/exsc.asm, twice, the binaries `cmp`ed. Then, with the
# binary that just proved itself, the C BACKEND'S EMITTED TEXT: `exsc
# --emitte c -o OUT` over two units, twice each, under the same two
# condition sets, the OUT files `cmp`ed. That second half is
# docs/design/c-backend.md D5's claim -- "the unit's text is a function of
# exactly the module, the --hospes row, and exsc's own bytes; no path, no
# --epoch, no host name, no address, no hash bucket" -- CHECKED rather than
# asserted. It was asserted until this commit: D5's row in c-backend.md
# section 9 named this script and this script did not do it, and the only
# standing evidence was that the two 64-bit --hospes rows emit the same
# bytes within ONE process, which is not a statement about cwd or locale
# at all. The reference backend's OUT is not diffed here because the
# BINARY built from it already is, one step earlier, and a differing OUT
# cannot produce an identical binary.
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
# The source is not one file. exsc.asm includes lexer/, cst/, ast/, diag/,
# driver/, rt/, macros/ by relative path, and the fixture fallback includes
# ../../compiler/x86_64/ the same way -- so each build directory gets a
# copy of the whole compiler/ and tests/ trees at the same relative depth,
# and SRC keeps its path inside them. Copying exsc.asm alone (what this
# script did until 2026-09-10) failed the moment exsc.asm gained an
# include, with `symbol 'DrvCtx.arena' is undefined`.
#
# `examples/` comes along for the --emitte c half below: each build
# directory gets its own copy, at its own absolute path and its own depth,
# which is precisely the divergence D5 says the emitted text must not carry.
# Nothing from vendor/ is needed -- the StreamDB reader's container is read
# at RUN time and this script never runs an emitted program.
REL="${SRC#"$REPO_ROOT/"}"
for d in "$DIR_A" "$DIR_B"; do
  cp -r "$REPO_ROOT/compiler" "$d/compiler"
  mkdir -p "$d/tests" && cp -r "$REPO_ROOT/tests/unit" "$d/tests/unit"
  cp -r "$REPO_ROOT/examples" "$d/examples"
done
SRC_A="$DIR_A/$REL"
SRC_B="$DIR_B/$REL"
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

# cmp_pair WHAT A B -- 0 and a PASS line if identical; 1 and the difference
# otherwise. Shared by the two halves so they report the same way.
cmp_pair() {
  local what="$1" a="$2" b="$3"
  if cmp -s "$a" "$b"; then
    echo "REPRODUCE: PASS -- $what byte-identical ($(wc -c < "$a" | tr -d ' ') bytes) across divergent cwd/TZ/locale/SOURCE_DATE_EPOCH/umask$([[ $HOSTNAME_VARY -eq 1 ]] && echo '/hostname')"
    return 0
  fi
  echo "REPRODUCE: FAIL -- $what differs" >&2
  echo "  A: $a ($(wc -c < "$a" | tr -d ' ') bytes)" >&2
  echo "  B: $b ($(wc -c < "$b" | tr -d ' ') bytes)" >&2
  echo "  first difference (cmp):" >&2
  cmp "$a" "$b" >&2 || true
  echo "  differing bytes (cmp -l, first 20 shown):" >&2
  cmp -l "$a" "$b" 2>/dev/null | head -20 >&2 || true
  return 1
}

RC=0
cmp_pair "the exsc binary" "$OUT_A" "$OUT_B" || RC=1

# ---------------------------------------------------------------------------
# The C backend's emitted text, D5. Same two condition sets, the binary that
# just proved itself as the compiler, two units:
#
#   saluta    examples/{saluta,imprime,initium}.exsc -- the hello world the
#             publish gate already builds; small, and the one unit whose
#             emitted C tests/run.sh pins byte for byte against emit_c.
#   streamdb  examples/streamdb/{lector_streamdb,probatio}.exsc -- the C3
#             program: 3,915 IR instructions, a `@transitus` struct per
#             record, `acies` slots up to 65,536 bytes, and the only unit in
#             the tree big enough for an ordering that depended on an address
#             or a hash bucket to have somewhere to hide.
#
# Each is emitted in BOTH build directories, so the two runs differ in cwd,
# in the absolute path of every source file, in TZ, locale,
# SOURCE_DATE_EPOCH, umask and (where unshare allows) hostname. The
# `--hospes` row is FIXED WITHIN a unit and varied BETWEEN units: the row is
# an input to the text by design, so varying it inside one comparison would
# be testing a different claim. `streamdb-o64` is the same two sources at
# `--hospes mips64-none-o64`, which is a genuinely different and larger text
# -- `mensura` is 32 there, and that is written through the unit as literal
# widths -- so it is the third unit and not a variation of the second.
# Without it the newest width path in the emitter would be the one path
# whose determinism nothing checks.
#
# A failure here is NOT a failure of exsc's own reproducibility: the first
# half passing and this one failing localises the defect to the C emitter,
# which is why they are reported as two lines and not one.
if [[ "$TESTING_FIXTURE" -eq 0 ]]; then
  chmod +x "$OUT_A"
  # emit_one WORKDIR EXSC OUT TZ LOCALE EPOCH UMASK HOSTNAME SRC...
  # emit_one WORKDIR EXSC OUT TZ LOCALE EPOCH UMASK HOSTNAME HOSPES SRC...
  emit_one() {
    local workdir="$1" exsc="$2" out="$3" tz="$4" locale="$5" epoch="$6"
    local umask_val="$7" host="$8" hospes="$9"; shift 9
    local inner srcs=""
    local s; for s in "$@"; do srcs="$srcs '$workdir/$s'"; done
    inner="cd '$workdir' && umask '$umask_val' && env -i PATH='$PATH' HOME='$HOME' \
TZ='$tz' LC_ALL='$locale' LANG='$locale' SOURCE_DATE_EPOCH='$epoch' \
'$exsc' aedifica --hospes '$hospes'$srcs --emitte c -o '$out' >/dev/null 2>&1"
    if [[ "$HOSTNAME_VARY" -eq 1 ]]; then
      unshare --uts -r -- bash -c "hostname '$host' && $inner"
    else
      bash -c "$inner"
    fi
  }

  echo
  for unit in \
    "saluta:x86_64-linux:examples/saluta.exsc examples/imprime.exsc examples/initium.exsc" \
    "streamdb:x86_64-linux:examples/streamdb/lector_streamdb.exsc examples/streamdb/probatio.exsc" \
    "streamdb-o64:mips64-none-o64:examples/streamdb/lector_streamdb.exsc examples/streamdb/probatio.exsc"
  do
    uname_="${unit%%:*}"
    uhospes="${unit#*:}"; uhospes="${uhospes%%:*}"
    # shellcheck disable=SC2206
    usrcs=(${unit##*:})
    ca="$DIR_A/unit-$uname_.c"
    cb="$DIR_B/unit-$uname_.c"
    echo "reproduce: --emitte c, unit '$uname_' (--hospes $uhospes, ${#usrcs[@]} sources), condition sets A and B"
    emit_one "$DIR_A" "$OUT_A" "$ca" "UTC" "C" "0" "022" "repro-host-a" "$uhospes" "${usrcs[@]}" || true
    emit_one "$DIR_B" "$OUT_A" "$cb" "Pacific/Kiritimati" "C.UTF-8" "999999999" "077" "repro-host-b" "$uhospes" "${usrcs[@]}" || true
    if [[ ! -s "$ca" || ! -s "$cb" ]]; then
      echo "REPRODUCE: FAIL -- --emitte c wrote nothing for unit '$uname_'" >&2
      echo "  (a unit of 0 bytes compared against another of 0 bytes is the" >&2
      echo "  vacuous pass this project has already shipped twice)" >&2
      RC=1
      continue
    fi
    cmp_pair "the --emitte c unit '$uname_'" "$ca" "$cb" || RC=1
  done
else
  echo
  echo ">>> the --emitte c half is SKIPPED: there is no exsc to run. D5 is"
  echo ">>> unchecked by this run. <<<"
fi

exit "$RC"
