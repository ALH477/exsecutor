#!/usr/bin/env bash
# exsecutor-usb/verify.sh  (source: tools/agent/usb/verify.sh)
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
#
# Check a bundle tree against its MANIFEST (sha256sum format, one
# "<64 hex>  <relative path>" line per file).
#
# What a pass means: every listed file is present with exactly the listed
# bytes, no symlink exists in the checked tree, and (unless --no-extra-check)
# no file is present that the manifest does not list -- an extra .py beside
# loop.py would otherwise be importable. What it does NOT mean: that the
# MANIFEST itself is genuine. It lives on the same stick, so anyone who can
# rewrite a file can rewrite the MANIFEST too. Pin the MANIFEST's own sha256
# somewhere else (build_bundle.sh prints it) and pass --expect-manifest.

set -euo pipefail

usage() {
  cat <<'EOF'
usage: verify.sh [--root DIR] [--manifest FILE] [--only TOP[,TOP...]]
                 [--no-extra-check] [--expect-manifest SHA256] [--quiet]

  --root DIR             the bundle directory (default: this script's directory)
  --manifest FILE        manifest path relative to --root (default: MANIFEST)
  --only TOP,...         check only entries under these top-level names
                         (e.g. bin,lib,repo); the extra-file check is then
                         limited to those names too
  --no-extra-check       do not fail on files the manifest does not list
  --expect-manifest SHA  fail unless sha256(MANIFEST) is exactly SHA
  --quiet                print only failures and the summary line

exit: 0 all good; 1 a mismatch, missing, unlisted or symlinked file, or a
manifest hash mismatch; 2 usage error or unreadable manifest.
EOF
}

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
MAN="MANIFEST"
ONLY=""
EXTRA=1
EXPECT=""
QUIET=0
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --root) ROOT="${2:?--root needs a directory}"; shift 2 ;;
    --manifest) MAN="${2:?--manifest needs a file}"; shift 2 ;;
    --only) ONLY="${2:?--only needs a list}"; shift 2 ;;
    --no-extra-check) EXTRA=0; shift ;;
    --expect-manifest) EXPECT="${2:?--expect-manifest needs a sha256}"; shift 2 ;;
    --quiet) QUIET=1; shift ;;
    *) echo "verify.sh: unknown argument: $1 (try --help)" >&2; exit 2 ;;
  esac
done

cd -- "$ROOT" || { echo "verify.sh: cannot enter $ROOT" >&2; exit 2; }
[ -f "$MAN" ] && [ ! -L "$MAN" ] || { echo "verify.sh: no manifest $ROOT/$MAN" >&2; exit 2; }

if [ -n "$EXPECT" ]; then
  got="$(sha256sum < "$MAN" | cut -c1-64)"
  if [ "$got" != "$EXPECT" ]; then
    echo "verify.sh: MANIFEST sha256 is $got, expected $EXPECT -- REFUSING" >&2
    exit 1
  fi
fi

selected() {  # selected PATH -> 0 if PATH is under one of --only
  [ -z "$ONLY" ] && return 0
  local top="${1%%/*}" t
  IFS=',' read -r -a tops <<< "$ONLY"
  for t in "${tops[@]}"; do [ "$top" = "$t" ] && return 0; done
  return 1
}

declare -A listed=()
bad=0; ok=0
while IFS= read -r line || [ -n "$line" ]; do
  [ -z "$line" ] && continue
  if ! [[ "$line" =~ ^([0-9a-f]{64})\ \ (.+)$ ]]; then
    echo "verify.sh: malformed manifest line: $line" >&2; exit 2
  fi
  want="${BASH_REMATCH[1]}"; p="${BASH_REMATCH[2]}"
  case "/$p/" in
    *"/../"*|*"/./"*|"//"*) echo "verify.sh: unsafe path in manifest: $p" >&2; exit 2 ;;
  esac
  listed["$p"]=1
  selected "$p" || continue
  if [ -L "$p" ]; then
    echo "SYMLINK   $p"; bad=$((bad + 1)); continue
  fi
  if [ ! -f "$p" ]; then
    echo "MISSING   $p"; bad=$((bad + 1)); continue
  fi
  got="$(sha256sum -- "$p" | cut -c1-64)"
  if [ "$got" != "$want" ]; then
    echo "MISMATCH  $p"; bad=$((bad + 1)); continue
  fi
  ok=$((ok + 1))
done < "$MAN"

if [ "$EXTRA" = 1 ]; then
  if [ -n "$ONLY" ]; then
    IFS=',' read -r -a roots <<< "$ONLY"
  else
    roots=(.)
  fi
  for r in "${roots[@]}"; do
    [ -e "$r" ] || continue
    while IFS= read -r -d '' f; do
      f="${f#./}"
      case "$f" in
        "$MAN"|LOCAL.MANIFEST|models/*|llama/*|transcripts/*|bin/llama-server) continue ;;
      esac
      if [ -L "$f" ]; then echo "SYMLINK   $f"; bad=$((bad + 1)); continue; fi
      if [ -z "${listed[$f]+x}" ]; then echo "UNLISTED  $f"; bad=$((bad + 1)); fi
    done < <(find "$r" \( -type f -o -type l \) -print0)
  done
fi

if [ "$bad" -ne 0 ]; then
  echo "verify.sh: FAILED -- $bad problem(s), $ok file(s) OK ($ROOT/$MAN)"
  exit 1
fi
[ "$QUIET" = 1 ] || echo "verify.sh: OK -- $ok file(s) match $MAN"
exit 0
