#!/usr/bin/env bash
# Stage 0 benchmark suite. Requires gcc and python3.
#
# None of the sources below exist in this tree. See
# prototypes/stage0-bench/README.md for exactly what is missing and which
# spec sections depend on it. Reconstructing them is out of scope for now;
# §6.2 (ARC) and §9.2 (compile-speed) figures stay [UNREPRODUCED] until
# someone does and this script actually runs and passes.
set -euo pipefail
cd "$(dirname "$0")"

missing=()
for f in bench1_refcount.c bench2b_biquad.c bench3_ast.c gen_c.py time_cc.py; do
    [ -f "$f" ] || missing+=("$f")
done

if [ "${#missing[@]}" -gt 0 ]; then
    echo "error: stage0-bench source(s) absent from prototypes/stage0-bench/:" >&2
    printf '  - %s\n' "${missing[@]}" >&2
    echo "See prototypes/stage0-bench/README.md -- these are not reconstructed." >&2
    echo "§6.2 (ARC overhead) and §9.2 (compile speed) figures stay [UNREPRODUCED]" >&2
    echo "until they exist and this script actually runs and passes." >&2
    exit 1
fi

gcc -O2 -o bench1 bench1_refcount.c        && ./bench1
gcc -O2 -o bench2 bench2b_biquad.c -lm     && ./bench2
gcc -O2 -o bench3 bench3_ast.c             && ./bench3
for n in 40 200 400 1000 2000; do python3 gen_c.py "$n" "gen_$n.c"; done
python3 time_cc.py
