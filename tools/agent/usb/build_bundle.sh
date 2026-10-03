#!/usr/bin/env bash
# tools/agent/usb/build_bundle.sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
#
# Assemble the portable USB bundle `exsecutor-usb/` from this repository.
# Copies only; downloads nothing; bundles no model weights and no llama.cpp.
# The output is a pure function of the inputs: no timestamps, hostnames or
# absolute paths are written into it, and MANIFEST is sorted bytewise, so two
# builds from the same tree give byte-identical MANIFESTs (selftest.sh checks).

set -euo pipefail

usage() {
  cat <<'EOF'
usage: build_bundle.sh [--out DIR] [--exsc PATH] [--fasmg PATH] [--force]

  --out DIR      where to create the bundle (default: ./exsecutor-usb)
  --exsc PATH    the compiler to bundle (default: <repo>/build/exsc; run
                 `make` first). Must be a freestanding static ELF.
  --fasmg PATH   the assembler to bundle (default: $FASMG, else `fasmg` on
                 PATH, else /nix/store/*fasmg*/bin/fasmg). Must be static:
                 a dynamically linked fasmg would not run on another distro,
                 so it is refused rather than bundled.
  --force        replace DIR if it already holds a bundle (has a MANIFEST)

Layout written:
  run.sh verify.sh README.md MANIFEST BUILDINFO
  bin/exsc bin/fasmg bin/sandbox_run.py
  lib/session.py
  repo/vendor/fasmg-x86/...                 verbatim (CRLF files stay CRLF)
  repo/tools/agent/loop/*.py                whatever exists at build time
  repo/tools/finetune/harness.py            loop.py imports it
  repo/tools/gen-finetune.py                harness.py loads it
  repo/docs/spec/exsecutor-spec-v0.4.md     loop.py reads its §13 table
  repo/datasets/finetune/lexicon.json
  models/ transcripts/                      empty; yours (not in MANIFEST)

Prints the MANIFEST's sha256 at the end: write it down somewhere that is not
the stick, and pass it to run.sh --expect-manifest.
EOF
}

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO="$(cd -- "$HERE/../../.." && pwd -P)"
OUT="$PWD/exsecutor-usb"
EXSC_IN="$REPO/build/exsc"
FASMG_IN="${FASMG:-}"
FORCE=0
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --out) OUT="${2:?--out needs a directory}"; shift 2 ;;
    --exsc) EXSC_IN="${2:?--exsc needs a path}"; shift 2 ;;
    --fasmg) FASMG_IN="${2:?--fasmg needs a path}"; shift 2 ;;
    --force) FORCE=1; shift ;;
    *) echo "build_bundle.sh: unknown argument: $1 (try --help)" >&2; exit 2 ;;
  esac
done

die() { echo "build_bundle.sh: $*" >&2; exit 1; }

if [ -z "$FASMG_IN" ]; then
  FASMG_IN="$(command -v fasmg || true)"
fi
if [ -z "$FASMG_IN" ]; then
  for f in /nix/store/*fasmg*/bin/fasmg; do [ -x "$f" ] && { FASMG_IN="$f"; break; }; done
fi
[ -n "$FASMG_IN" ] && [ -f "$FASMG_IN" ] || die "no fasmg found (pass --fasmg)"
[ -f "$EXSC_IN" ] || die "no compiler at $EXSC_IN -- run \`make\` first (needs fasmg on PATH)"

# Both binaries must be freestanding: they will run from a RAM copy on a
# laptop whose libc we do not know. The runner's own ELF check decides.
for b in "$EXSC_IN" "$FASMG_IN"; do
  python3 "$HERE/sandbox_run.py" --check-elf "$b" >/dev/null \
    || die "$(python3 "$HERE/sandbox_run.py" --check-elf "$b") -- refusing to bundle it"
done

OUT="$(mkdir -p -- "$(dirname -- "$OUT")" && cd -- "$(dirname -- "$OUT")" && pwd -P)/$(basename -- "$OUT")"
if [ -e "$OUT" ]; then
  [ "$FORCE" = 1 ] || die "$OUT exists (pass --force to replace it)"
  [ -f "$OUT/MANIFEST" ] || die "$OUT exists and is not a bundle (no MANIFEST); not deleting it"
  rm -rf -- "$OUT"
fi

mkdir -p "$OUT"/{bin,lib,models,transcripts} \
         "$OUT"/repo/{vendor,tools/agent/loop,tools/finetune,docs/spec,datasets/finetune}

install_f() {  # install_f MODE SRC DST
  cp -- "$2" "$3"
  chmod "$1" "$3"
}

install_f 0755 "$EXSC_IN"  "$OUT/bin/exsc"
install_f 0755 "$FASMG_IN" "$OUT/bin/fasmg"
install_f 0755 "$HERE/sandbox_run.py" "$OUT/bin/sandbox_run.py"
install_f 0755 "$HERE/session.py" "$OUT/lib/session.py"
install_f 0755 "$HERE/run.sh" "$OUT/run.sh"
install_f 0755 "$HERE/verify.sh" "$OUT/verify.sh"
install_f 0644 "$HERE/README.md" "$OUT/README.md"

# Vendored macro packages: byte-for-byte. Some files are CRLF upstream; cp
# does not translate, and diff -r below proves the copy is exact.
cp -R -- "$REPO/vendor/fasmg-x86" "$OUT/repo/vendor/fasmg-x86"
diff -r -q -- "$REPO/vendor/fasmg-x86" "$OUT/repo/vendor/fasmg-x86" >/dev/null \
  || die "vendor/fasmg-x86 copy is not byte-identical"

loop_files=()
for f in "$REPO"/tools/agent/loop/*.py; do
  [ -f "$f" ] || continue
  install_f 0644 "$f" "$OUT/repo/tools/agent/loop/$(basename -- "$f")"
  loop_files+=("$(basename -- "$f")")
done
install_f 0644 "$REPO/tools/finetune/harness.py" "$OUT/repo/tools/finetune/harness.py"
install_f 0644 "$REPO/tools/gen-finetune.py" "$OUT/repo/tools/gen-finetune.py"
install_f 0644 "$REPO/docs/spec/exsecutor-spec-v0.4.md" "$OUT/repo/docs/spec/exsecutor-spec-v0.4.md"
install_f 0644 "$REPO/datasets/finetune/lexicon.json" "$OUT/repo/datasets/finetune/lexicon.json"

# BUILDINFO: what went in, by content. No clock, no host, no absolute path.
commit="unknown"; dirty="unknown"
if command -v git >/dev/null && git -C "$REPO" rev-parse HEAD >/dev/null 2>&1; then
  commit="$(git -C "$REPO" rev-parse HEAD)"
  if [ -z "$(git -C "$REPO" status --porcelain -- compiler vendor/fasmg-x86 tools/agent \
            tools/finetune/harness.py tools/gen-finetune.py docs/spec datasets/finetune/lexicon.json)" ]; then
    dirty="no"
  else
    dirty="yes"
  fi
fi
{
  echo "source_commit=$commit"
  echo "source_tree_dirty=$dirty"
  echo "exsc_sha256=$(sha256sum < "$OUT/bin/exsc" | cut -c1-64)"
  echo "fasmg_sha256=$(sha256sum < "$OUT/bin/fasmg" | cut -c1-64)"
  echo "exsc_linking=static (no PT_INTERP, no PT_DYNAMIC)"
  echo "fasmg_linking=static (no PT_INTERP, no PT_DYNAMIC)"
  echo "loop_files=${loop_files[*]:-none}"
  echo "requires=Linux x86-64, python3, util-linux unshare+mount, coreutils, unprivileged user namespaces"
} > "$OUT/BUILDINFO"
chmod 0644 "$OUT/BUILDINFO"

(
  cd "$OUT"
  find . -type f ! -path ./MANIFEST -print0 | LC_ALL=C sort -z \
    | while IFS= read -r -d '' f; do
        printf '%s  %s\n' "$(sha256sum < "$f" | cut -c1-64)" "${f#./}"
      done > MANIFEST
)
chmod 0644 "$OUT/MANIFEST"

bash "$OUT/verify.sh" --root "$OUT" --quiet
n="$(wc -l < "$OUT/MANIFEST")"
echo "build_bundle.sh: wrote $OUT ($n files in MANIFEST)"
if [ "${#loop_files[@]}" -eq 0 ]; then
  echo "build_bundle.sh: WARNING: tools/agent/loop/ had no .py files; the agent loop is NOT bundled"
else
  echo "build_bundle.sh: agent loop files bundled as found at build time: ${loop_files[*]}"
fi
echo "build_bundle.sh: MANIFEST sha256 $(sha256sum < "$OUT/MANIFEST" | cut -c1-64)"
echo "build_bundle.sh: keep that hash OFF the stick; pass it to run.sh --expect-manifest"
