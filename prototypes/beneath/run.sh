#!/usr/bin/env bash
# prototypes/beneath/run.sh -- build and run beneath.c, the openat2(2)
# RESOLVE_* measurement behind docs/design/archivum-beneath.md section 2.
# Verification-only (prototypes/README.md): never shipped, never on the build
# closure, never referenced from the Makefile or the Nix build. Run by hand;
# its output is transcribed into the design document.
#
# Needs root (mount(2) and unshare(2) for the mount-crossing rows; every
# mount is made in a private mount namespace and dies with the process).
# Usage: run.sh [race-seconds]   (default 2)
set -u
cd "$(dirname "$0")"
if [ "$(id -u)" != 0 ]; then
    echo "beneath: needs root (mount namespace, tmpfs/bind/proc mounts)" >&2
    exit 2
fi
CC=${CC:-cc}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/beneath.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
"$CC" -std=c11 -O1 -Wall -Wextra -pthread -o "$WORK/beneath" beneath.c || exit 2
mkdir "$WORK/w"
"$WORK/beneath" "$WORK/w" "${1:-2}"
