#!/bin/sh
# Stage 0 benchmark suite. Requires gcc and python3.
set -e
gcc -O2 -o bench1 bench1_refcount.c        && ./bench1
gcc -O2 -o bench2 bench2b_biquad.c -lm     && ./bench2
gcc -O2 -o bench3 bench3_ast.c             && ./bench3
for n in 40 200 400 1000 2000; do python3 gen_c.py $n gen_$n.c; done
python3 time_cc.py
