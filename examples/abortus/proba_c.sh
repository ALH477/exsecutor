#!/usr/bin/env bash
# examples/abortus/proba_c.sh -- a returning trap for a hosted host of a
# library-mode unit, checked. README.md is the argument; this is the evidence.
#
#   1. exsc emits three units -- custos (with entry 23's declaration and
#      codec, as examples/custos/ builds it), arca, and the test-only
#      probatio.exsc -- each twice, byte-identical.
#   2. For every C compiler given, at -O0 and -O2, under UBSan (-Werror,
#      -Wextra, so GCC's -Wclobbered is on), and once more under GCC with
#      ASan + UBSan: the units, tutela.c, exempla.c (with custos.h and arca.h
#      force-included, so a wrapper's prototype that drifts from the unit is a
#      compile error) and proba.c are built and linked. `proba` must print
#      N/N; `proba nudus` (a trap with no guard open) must write
#      "exsecutor: abortus 1" and die of SIGABRT -- the fail-stop default kept.
#   3. Three mutants of tutela.c must each make `proba` fail: the guard
#      ignored, the kind dropped, the guard not unlinked.
#   4. Not in a kernel: every emitted unit compiles with -ffreestanding
#      against the compiler's own headers and nothing else, and tutela.c does
#      not, because <setjmp.h> is not a freestanding header.
#   5. The Rust host, when rustc is on PATH: proba.rs over a static library
#      of the same objects, -O2, no sanitizer.
#
# Usage: examples/abortus/proba_c.sh [CC...]    (default: gcc clang)
# Needs build/exsc (make all). Writes only to a temp dir.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
exsc="$root/build/exsc"
[ -x "$exsc" ] || { echo "proba_c: $exsc missing -- run 'make all'" >&2; exit 2; }
ccs=("$@")
[ ${#ccs[@]} -gt 0 ] || ccs=(gcc clang)

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
fail=0

e23=("$root/tests/conformance/entry23_demodframe_golden_vectors.exsc"
     "$root/tests/conformance/entry23/codex.exsc")
emit() {  # emit OUT SRC...
  local out=$1; shift
  "$exsc" aedifica --hospes x86_64-linux --emitte c "$@" -o "$out" >/dev/null 2>&1
}
units=(custos arca probatio)
src_custos=("${e23[@]}" "$root/examples/custos/custos.exsc")
src_arca=("$root/examples/arca/arca.exsc")
src_probatio=("$here/probatio.exsc")

# ---- 1. emission ------------------------------------------------------------
for u in "${units[@]}"; do
  eval "src=(\"\${src_$u[@]}\")"
  if emit "$work/$u.gen.c" "${src[@]}" && emit "$work/$u.again.c" "${src[@]}" \
      && cmp -s "$work/$u.gen.c" "$work/$u.again.c"; then
    echo "  [ok]   $u: emitted twice, byte-identical ($(wc -c <"$work/$u.gen.c") bytes)"
  else
    echo "  [FAIL] $u: did not emit, or two emissions differ"; fail=1
  fi
done
[ "$fail" -eq 0 ] || { echo "abortus: FAIL (emission)"; exit 1; }

# ---- P2, measured: what each unit imports ---------------------------------
# The soundness argument (README.md) rests on a trap abandoning nothing that
# needs releasing. In C that is a statement about the object's imports: a
# unit that could allocate, lock or call the host would import something to
# do it with. Each unit's object may import exsrt_abortus and nothing else,
# beyond memcpy/memset (a compiler's own lowering of __builtin_memcpy or of
# an initializer) and __stack_chk_fail (a distribution GCC's default
# -fstack-protector; a frame abandoned by longjmp never reaches its epilogue,
# so its canary is never checked, and the guard's own frame is intact).
if command -v nm >/dev/null; then
  for u in "${units[@]}"; do
    "${ccs[0]}" -std=c11 -O2 -c "$work/$u.gen.c" -o "$work/$u.plain.o"
    imp="$(nm -u "$work/$u.plain.o" | awk '{print $2}' | sort | tr '\n' ' ')"
    other="$(nm -u "$work/$u.plain.o" | awk '{print $2}' \
      | grep -vxE 'exsrt_abortus|memcpy|memset|__stack_chk_fail' || true)"
    if [ -z "$other" ] && grep -q 'exsrt_abortus' <<<"$imp"; then
      echo "  [ok]   $u: imports $imp"
    else
      echo "  [FAIL] $u: imports $imp-- outside the set the argument covers"; fail=1
    fi
  done
else
  echo "  [FAIL] nm not on PATH: P2 unmeasured"; fail=1
fi

# ---- 2. builds --------------------------------------------------------------
is_clang() { "$1" --version 2>/dev/null | head -1 | grep -qi clang; }

build() {  # build CC OUTDIR TUTELA.C FLAGS...
  local cc=$1 out=$2 tut=$3; shift 3
  local f=(-std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-cpp -Wno-#warnings "$@")
  mkdir -p "$out"
  for u in "${units[@]}"; do "$cc" "${f[@]}" -c "$work/$u.gen.c" -o "$out/$u.o" || return 1; done
  "$cc" "${f[@]}" -I "$here" -c "$tut" -o "$out/tutela.o" || return 1
  "$cc" "${f[@]}" -I "$here" -include "$root/examples/custos/custos.h" \
    -include "$root/examples/arca/arca.h" -c "$here/exempla.c" -o "$out/exempla.o" || return 1
  "$cc" "${f[@]}" -pthread -I "$here" -c "$here/proba.c" -o "$out/proba.o" || return 1
  "$cc" "$@" -pthread "$out"/{custos,arca,probatio,tutela,exempla,proba}.o -o "$out/proba"
}

run_proba() {  # run_proba LABEL DIR
  local label=$1 dir=$2 rc
  if "$dir/proba" >"$dir/out.txt" 2>&1; then
    echo "  [ok]   $label: $(tail -1 "$dir/out.txt")"
  else
    echo "  [FAIL] $label"; grep -v '\[ok\]' "$dir/out.txt" | head -20; fail=1
  fi
  # In a subshell that waits (the trailing `exit` stops bash from exec'ing
  # the command in place), with the subshell's stderr discarded: bash's own
  # "Aborted" job message goes there, and the status still comes through.
  rc=0; ( "$dir/proba" nudus >"$dir/nudus.out" 2>"$dir/nudus.err"; exit $? ) 2>/dev/null || rc=$?
  if [ "$rc" -eq 134 ] && grep -qx 'exsecutor: abortus 1' "$dir/nudus.err" \
      && ! grep -q '^nudus:' "$dir/nudus.out"; then
    echo "  [ok]   $label: unguarded trap -> 'exsecutor: abortus 1', SIGABRT (exit 134)"
  else
    echo "  [FAIL] $label: unguarded trap: exit $rc, stderr '$(head -c 200 "$dir/nudus.err")'"; fail=1
  fi
}

n=0
for cc in "${ccs[@]}"; do
  command -v "$cc" >/dev/null || { echo "  [FAIL] $cc: not on PATH"; fail=1; continue; }
  if is_clang "$cc"; then san=(-fsanitize=undefined -fsanitize-trap=undefined)
  else san=(-fsanitize=undefined -fno-sanitize-recover=all); fi
  for opt in -O0 -O2; do
    n=$((n + 1)); d="$work/b$n"
    if build "$cc" "$d" "$here/tutela.c" "$opt" "${san[@]}" 2>"$work/b$n.err"; then
      run_proba "$cc $opt UBSan" "$d"
    else
      echo "  [FAIL] $cc $opt: build"; head -20 "$work/b$n.err"; fail=1
    fi
  done
  if ! is_clang "$cc"; then
    n=$((n + 1)); d="$work/b$n"
    if build "$cc" "$d" "$here/tutela.c" -O1 -fno-omit-frame-pointer \
        -fsanitize=address,undefined -fno-sanitize-recover=all 2>"$work/b$n.err"; then
      run_proba "$cc -O1 ASan+UBSan" "$d"
    else
      echo "  [FAIL] $cc -O1 ASan: build"; head -20 "$work/b$n.err"; fail=1
    fi
  fi
done
[ "$fail" -eq 0 ] || { echo "abortus: FAIL (baseline; mutants not run)"; exit 1; }

# ---- 3. mutants of tutela.c ------------------------------------------------
# Each mutant must make proba fail, and fail at the check that names what the
# mutant broke -- not merely crash somewhere later. proba's stdout is line
# buffered, so the line survives a crash after it.
mutant() {  # mutant NAME SED-EXPR EXPECTED-FAIL-LINE
  local name=$1 expr=$2 want=$3 d="$work/mut" rc
  sed "$expr" "$here/tutela.c" >"$work/mutant.c"
  if cmp -s "$work/mutant.c" "$here/tutela.c"; then
    echo "  [FAIL] mutant '$name' changed nothing -- its pattern moved"; fail=1; return
  fi
  rm -rf "$d"
  if ! build "${ccs[0]}" "$d" "$work/mutant.c" -O0 2>/dev/null; then
    echo "  [FAIL] mutant '$name' did not build -- it tests nothing"; fail=1; return
  fi
  rc=0; ( timeout 60 "$d/proba" >"$d/out.txt" 2>&1; exit $? ) 2>/dev/null || rc=$?
  if [ "$rc" -eq 0 ]; then
    echo "  [FAIL] mutant '$name' passed proba"; fail=1
  elif grep -qF -- "$want" "$d/out.txt"; then
    echo "  [ok]   mutant '$name' is caught (exit $rc) at: $want"
  else
    echo "  [FAIL] mutant '$name' failed (exit $rc), but not at: $want"; fail=1
  fi
}
# Ignoring the guard sends the first trap down the fail-stop path, whose
# stderr line (captured with stdout) names it.
mutant "guard ignored, every trap aborts" 's/  if (t != NULL) {/  if (0) {/' \
  "exsecutor: abortus 1"
mutant "kind dropped, a trap reads as success" \
  's/t->genus = kind != 0 ? kind : EXS_TUTELA_GENUS_NULLUS;/t->genus = 0;/' \
  "[FAIL] arca: saltus(magnitudo(garbage header)) traps, kind 1"
mutant "guard not unlinked before the jump" '/^    summa = t->prior;$/d' \
  "[FAIL]   guard chain empty after the trap"

# ---- 4. not in a kernel ----------------------------------------------------
for cc in "${ccs[@]}"; do
  command -v "$cc" >/dev/null || continue
  # Only the compiler's own headers, as an old-style Kbuild passes them.
  # This host's GCC (Ubuntu 13.3.0) installs a <limits.h> that always
  # recurses into libc's; _LIBC_LIMITS_H_ is the guard libc's own copy
  # defines, and setting it stops that recursion. (A current kernel passes
  # no compiler include directory at all, so the unit's <stdint.h>,
  # <limits.h> and <float.h> would have to come from the kernel side --
  # roadmap B.1's territory, not this example's.)
  if is_clang "$cc"; then inc="$("$cc" -print-resource-dir)/include"; extra=()
  else inc="$("$cc" -print-file-name=include)"; extra=(-D_LIBC_LIMITS_H_); fi
  fs=(-std=c11 -ffreestanding -nostdinc -isystem "$inc" "${extra[@]}" -fsyntax-only -Wno-unused-function)
  ok=1
  for u in "${units[@]}"; do "$cc" "${fs[@]}" "$work/$u.gen.c" 2>/dev/null || ok=0; done
  if [ "$ok" -eq 1 ]; then
    echo "  [ok]   $cc -ffreestanding -nostdinc: all three units compile"
  else
    echo "  [FAIL] $cc -ffreestanding -nostdinc: a unit does not compile"; fail=1
  fi
  if "$cc" "${fs[@]}" -I "$here" "$here/tutela.c" 2>"$work/fs.err"; then
    echo "  [FAIL] $cc -ffreestanding -nostdinc: tutela.c compiled -- the claim it needs a hosted <setjmp.h> is wrong"; fail=1
  elif grep -q 'setjmp.h' "$work/fs.err"; then
    echo "  [ok]   $cc -ffreestanding -nostdinc: tutela.c refused, <setjmp.h> not found"
  else
    echo "  [FAIL] $cc -ffreestanding -nostdinc: tutela.c refused, but not for <setjmp.h>:"
    head -3 "$work/fs.err"; fail=1
  fi
done

# ---- 5. the Rust host ------------------------------------------------------
if command -v rustc >/dev/null; then
  d="$work/rs"; mkdir -p "$d"
  cc0="${ccs[0]}"
  ok=1
  for u in "${units[@]}"; do "$cc0" -std=c11 -O2 -c "$work/$u.gen.c" -o "$d/$u.o" || ok=0; done
  "$cc0" -std=c11 -O2 -I "$here" -c "$here/tutela.c" -o "$d/tutela.o" || ok=0
  "$cc0" -std=c11 -O2 -I "$here" -c "$here/exempla.c" -o "$d/exempla.o" || ok=0
  if [ "$ok" -eq 1 ] && ar rcs "$d/libabortus.a" "$d"/{custos,arca,probatio,tutela,exempla}.o \
      && rustc --edition 2021 -O "$here/proba.rs" -L "$d" -l static=abortus -o "$d/proba_rs" 2>"$d/rustc.err"; then
    if "$d/proba_rs" >"$d/out.txt" 2>&1; then
      echo "  [ok]   rust host ($(rustc --version | cut -d' ' -f1-2), $cc0 -O2): $(tail -1 "$d/out.txt")"
    else
      echo "  [FAIL] rust host"; grep -v '\[ok\]' "$d/out.txt" | head -20; fail=1
    fi
  else
    echo "  [FAIL] rust host: build"; head -20 "$d/rustc.err" 2>/dev/null; fail=1
  fi
else
  echo "  [skip] rust host: rustc not on PATH"
fi

[ "$fail" -eq 0 ] && echo "abortus: PASS" || { echo "abortus: FAIL"; exit 1; }
