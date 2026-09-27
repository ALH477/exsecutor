#!/usr/bin/env bash
# tools/somnium-bench.sh -- frame time and resident set for examples/somnium/.
#
# docs/design/somnium.md carried "what the real program's frame rate is on
# either build is [UNTESTED]" from the day it was written, because the tree it
# was written in had no fasmg. This is the instrument that retires it. It
# measures, it prints, and it writes nothing into the tree: a figure belongs in
# docs/design/somnium.md only once a person has run this and pasted it there
# with the date and the machine, which is what CLAUDE.md's evidence discipline
# asks for and what a script cannot do on its own.
#
# Both builds, nine somnia, three runs, the MEDIAN reported -- not the mean,
# which one descheduled run moves, and not the minimum, which flatters.
#
# The reference build's cost is one write(2) per output byte (48,000 a frame);
# the C build's host buffers a whole frame and leaves in one. So the ratio
# between the two columns is, to a first approximation, the price of the
# missing bulk-write prelude primitive -- docs/design/somnium.md section 9's
# standing [OPEN]. That is the number this script exists to put a figure on.
#
# Usage: tools/somnium-bench.sh [OUTDIR]
#   Needs: build/exsc (make all), a C compiler, python3 for the oracle's
#   `petitio`. Run it inside `nix develop` -- fasmg is not on a bare PATH.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:-${TMPDIR:-/tmp}/somnium-bench.$$}"
mkdir -p "$out"

# The twelve sources in flake.nix's somniumSources order, which is also every
# tests/programs/somnium_*/TEST line's order: the unit measured is the unit
# shipped.
sources=(
  examples/somnium/somnium.exsc
  examples/somnium/plasma.exsc
  examples/somnium/ignis.exsc
  examples/somnium/vita.exsc
  examples/somnium/pluvia.exsc
  examples/somnium/stellae.exsc
  examples/somnium/cuniculus.exsc
  examples/somnium/abyssus.exsc
  examples/somnium/titulus.exsc
  examples/signaculum/forma.exsc
  examples/somnium/signum.exsc
  examples/somnium/machina.exsc
)

nomina=(plasma ignis vita pluvia stellae cuniculus abyssus titulus signum)
# Frames per run. abyssus and signum are the two expensive somnia (a 275-
# iteration Mandelbrot budget and a 512x512 rasterisation respectively), so
# they get fewer -- the figure reported is per frame either way.
tabulae=(120 120 120 120 120 120 24 120 24)

cd "$root"
echo "== building both somnium builds into $out"
build/exsc aedifica --hospes x86_64-linux "${sources[@]}" -o "$out/somnium.asm" >/dev/null
fasmg "$out/somnium.asm" "$out/somnium" >/dev/null
chmod +x "$out/somnium"
build/exsc aedifica --hospes x86_64-linux --emitte c "${sources[@]}" -o "$out/somnium.c" >/dev/null
"${CC:-cc}" -std=c11 -O2 -ffp-contract=off -fno-fast-math \
    -o "$out/somnium-c" "$out/somnium.c" examples/somnium/hospes.c 2>/dev/null

# A somnium 8 request carries the 44,801-byte EXSG model after its 17 bytes.
echo "== requests"
for id in 0 1 2 3 4 5 6 7 8; do
  python3 prototypes/somnium_oracle.py petitio "$id" 0 "${tabulae[$id]}" 0 > "$out/req$id.bin"
done

# Median of three wall times, in milliseconds per frame.
mensura() {  # mensura BINARY REQUEST FRAMES
  local bin="$1" req="$2" n="$3" t
  local -a ts=()
  for _ in 1 2 3; do
    t=$( { TIMEFORMAT=%R; time "$bin" < "$req" > /dev/null; } 2>&1 )
    ts+=("$t")
  done
  printf '%s\n' "${ts[@]}" | sort -g | sed -n 2p | awk -v n="$n" '{printf "%.2f", $1 * 1000 / n}'
}

printf '\n%-10s %7s  %10s  %10s  %7s\n' somnium frames 'ref ms/fr' 'C ms/fr' ratio
printf '%-10s %7s  %10s  %10s  %7s\n' ---------- ------ ---------- ---------- -------
for id in 0 1 2 3 4 5 6 7 8; do
  n="${tabulae[$id]}"
  r=$(mensura "$out/somnium"   "$out/req$id.bin" "$n")
  c=$(mensura "$out/somnium-c" "$out/req$id.bin" "$n")
  printf '%-10s %7d  %10s  %10s  %7s\n' "${nomina[$id]}" "$n" "$r" "$c" \
      "$(awk -v a="$r" -v b="$c" 'BEGIN{ if (b+0 == 0) print "-"; else printf "%.1fx", a/b }')"
done

# The stack frame machina reserves, read out of the emitted text rather than
# guessed: this is the ~3 MB of zero-filled locals its own header tabulates.
# The stack frame machina reserves, read out of the emitted text rather than
# guessed: this is the ~3 MB of zero-filled locals its own header tabulates
# (fb, calor, the two Life boards, Pluvia, Stellae, the EXSG model, and the 3D
# engine's 512x512 frame and f64 z-buffer). The player this bake exists to
# make possible is measured against exactly this number.
printf '\nstack frames, from the emitted text (bytes):\n'
for f in machina scribe_tabulam; do
  printf '  %-16s %s\n' "$f" \
      "$(awk -v f="bfausr_$f:" '$0 == f {g=1; next} g && /sub[[:space:]]+rsp,/ {gsub(/[^0-9]/,""); print; exit}' "$out/somnium.asm")"
done

echo
echo "artifacts in $out"
