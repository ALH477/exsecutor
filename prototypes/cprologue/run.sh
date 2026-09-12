#!/usr/bin/env bash
# prototypes/cprologue/run.sh -- run docs/design/c-backend.md D3's fourteen
# incantations under gcc and clang, at -O0 and -O2, with UBSan, and print one
# line per cell. Verification-only (prototypes/README.md): never shipped,
# never on the build closure, never referenced from the Makefile or the Nix
# build. Run by hand inside `nix develop`; its output is transcribed into
# docs/design/c-backend.md section 8's table.
set -u
cd "$(dirname "$0")"
WORK="${TMPDIR:-/tmp}/cprologue.$$"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

BASE="-std=c11 -Wall -Wextra -pedantic -fsanitize=undefined -fno-sanitize-recover=all"

pass=0; fail=0
say() { printf '%-56s %s\n' "$1" "$2"; }
ok()   { pass=$((pass+1)); say "$1" "HELD: $2"; }
bad()  { fail=$((fail+1)); say "$1" "**REJECTED**: $2"; }

# want_compile <label> <expect: ok|fail> <compiler> <flags...>
want_compile() {
    local label=$1 expect=$2 cc=$3; shift 3
    local log="$WORK/log"
    if "$cc" "$@" -o "$WORK/a.out" incantations.c >"$log" 2>&1; then
        if [ "$expect" = ok ]; then ok "$label" "compiled"
        else bad "$label" "compiled but should not have"; fi
    else
        if [ "$expect" = fail ]; then ok "$label" "refused at compile time"
        else bad "$label" "$(head -3 "$log" | tr '\n' ' ')"; fi
    fi
}

for cc in gcc clang; do
    command -v "$cc" >/dev/null 2>&1 || { echo "MISSING: $cc"; fail=$((fail+1)); continue; }
    ver=$("$cc" --version | head -1)
    echo "=== $cc -- $ver"
    for opt in -O0 -O2; do
        # rows 1,2,5,6,8,10 + family/byte-order: the whole prologue compiles
        want_compile "prologue compiles ($cc $opt -pedantic)" ok "$cc" $BASE $opt
        # and runs: rows 7, 9, 11, 12
        if [ -x "$WORK/a.out" ]; then
            out=$("$WORK/a.out" 2>&1); rc=$?
            if [ $rc -eq 0 ] && ! printf '%s' "$out" | grep -q 'runtime error'; then
                ok "helpers + alignas + memcpy + index ($cc $opt, UBSan)" "$out"
            else
                bad "helpers + alignas + memcpy + index ($cc $opt, UBSan)" "rc=$rc $out"
            fi
        fi
    done

    # row 1: __FAST_MATH__ fires under -ffast-math, not otherwise
    want_compile "row 1  __FAST_MATH__ fires under -ffast-math ($cc)" fail "$cc" $BASE -O2 -ffast-math
    # row 2: __FINITE_MATH_ONLY__ defined at all, value 1 under the flag
    want_compile "row 2  __FINITE_MATH_ONLY__ value fires ($cc)" fail "$cc" $BASE -O2 -ffinite-math-only
    # row 6: sizeof(void*) == 4 fires under lp64; the o64 half is C4's
    want_compile "row 6  sizeof(void*)==4 fires under lp64 ($cc)" fail "$cc" $BASE -O2 -DPROBE_PTR32=1
    # row 8: _Noreturn under the other two dialects Kiln uses
    for std in gnu11 gnu2x; do
        want_compile "row 8  _Noreturn under -std=$std ($cc)" ok "$cc" \
            -std=$std -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=all -O2
    done
    # row 9: does -Os leave a memcpy symbol reference behind?
    if "$cc" -std=c11 -Os -c -o "$WORK/o.o" incantations.c >/dev/null 2>&1; then
        if nm -u "$WORK/o.o" 2>/dev/null | grep -qw memcpy; then
            say "row 9  memcpy symbol at -Os ($cc)" "REFERENCED (recorded, not forbidden)"
        else
            say "row 9  memcpy symbol at -Os ($cc)" "not referenced"
        fi
    else
        bad "row 9  -Os compile ($cc)" "did not compile"
    fi
    # row 12: pointer-overflow sanitizer specifically
    want_compile "row 12 -fsanitize=pointer-overflow accepted ($cc)" ok "$cc" \
        -std=c11 -Wall -Wextra -O2 -fsanitize=undefined,pointer-overflow -fno-sanitize-recover=all
    if [ -x "$WORK/a.out" ]; then
        out=$("$WORK/a.out" 2>&1); rc=$?
        if [ $rc -eq 0 ] && ! printf '%s' "$out" | grep -q 'runtime error'; then
            ok "row 12 index idiom, pointer-overflow clean ($cc)" "no report"
        else
            bad "row 12 index idiom under pointer-overflow ($cc)" "rc=$rc $out"
        fi
    fi
    # rows 13, 14: floats are D8's; the measurement is cheap
    if "$cc" -std=c11 -O2 -DPROBE_FENV=1 -DPROBE_MXCSR=1 -o "$WORK/f.out" \
         incantations.c -lm >"$WORK/log" 2>&1; then
        say "rows 13/14 ($cc -O2)" "$("$WORK/f.out" 2>&1 | grep -E '^(fenv|mxcsr):' | tr '\n' ' ')"
    else
        say "rows 13/14 ($cc -O2)" "did not compile: $(head -2 "$WORK/log" | tr '\n' ' ')"
    fi

    # rows 3 and 4: the fp-contract pragmas, measured. -mfma, because the
    # x86-64 baseline ISA has no fused multiply-add and nothing can contract
    # into an instruction the target does not have -- a "not contracted"
    # reading without it would be the ISA's answer, not the pragma's.
    for p in 0 1 2 3; do
        for opt in -O0 -O2; do
            if "$cc" -std=c11 -Wall -Wextra -mfma $opt -ffp-contract=fast -DPRAGMA=$p \
                 -o "$WORK/fp.out" fpcontract.c >"$WORK/log" 2>&1; then
                # two independent readings: the numeric witness, and whether
                # a vfmadd instruction is in the asm at all.
                "$cc" -std=c11 -mfma $opt -ffp-contract=fast -DPRAGMA=$p -S \
                      -o "$WORK/fp.s" fpcontract.c >/dev/null 2>&1
                nfma=$(grep -c vfmadd "$WORK/fp.s" 2>/dev/null || echo 0)
                say "rows 3/4 ($cc $opt -mfma -ffp-contract=fast PRAGMA=$p)" \
                    "$("$WORK/fp.out") vfmadd_in_asm=$nfma"
            else
                say "rows 3/4 ($cc $opt PRAGMA=$p)" "did not compile: $(head -2 "$WORK/log" | tr '\n' ' ')"
            fi
        done
    done
    # Does the pragma even parse under this compiler? A silently-dropped
    # pragma and an honoured-but-overridden one look the same from the
    # numbers alone.
    "$cc" -std=c11 -Wall -Wextra -Wunknown-pragmas -mfma -O2 -ffp-contract=fast \
          -DPRAGMA=3 -S -o /dev/null fpcontract.c >"$WORK/log" 2>&1
    say "rows 3/4 pragma diagnostics ($cc)" \
        "$(grep -ci warning "$WORK/log" | tr -d '\n') warning(s): $(grep -m2 -i warning "$WORK/log" | tr '\n' ' ')"
    # The one thing that is known to work is the flag -- which is exactly the
    # leak spec §9.3 forbids. Measured so the finding names the alternative.
    if "$cc" -std=c11 -mfma -O2 -ffp-contract=off -DPRAGMA=0 \
         -o "$WORK/fp.out" fpcontract.c >/dev/null 2>&1; then
        say "rows 3/4 the FLAG -ffp-contract=off ($cc -O2)" "$("$WORK/fp.out")"
    fi
done

echo "=== pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
