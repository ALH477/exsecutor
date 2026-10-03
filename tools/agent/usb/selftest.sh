#!/usr/bin/env bash
# tools/agent/usb/selftest.sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
#
# Tests for the USB bundle that run with NO model: the runner contract, the
# sandbox's denials, fail-closed behaviour, bundle build / verify / tamper
# detection, relocation, a noexec "stick", a whole loop session against a
# STUB server (tests/fake_llama_server.py -- not llama.cpp), and the amnesia
# claim checked by find/mtime. Every expectation is asserted; the output is
# the evidence. Needs build/exsc (run `make`) and fasmg.

set -uo pipefail

usage() {
  cat <<'EOF'
usage: selftest.sh [--keep] [--no-unpriv] [--fasmg PATH]

  --keep         keep the work directory (under /dev/shm) for inspection
  --no-unpriv    skip the runs as uid 65534 (they need root + setpriv)
  --fasmg PATH   assembler (default: $FASMG, fasmg on PATH, /nix/store glob)

Exit 0 iff every case passed. Skipped cases are listed with the reason.
EOF
}

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO="$(cd -- "$HERE/../../.." && pwd -P)"
KEEP=0; UNPRIV=1; FASMG_BIN="${FASMG:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --keep) KEEP=1; shift ;;
    --no-unpriv) UNPRIV=0; shift ;;
    --fasmg) FASMG_BIN="${2:?}"; shift 2 ;;
    *) echo "selftest.sh: unknown argument $1 (try --help)" >&2; exit 2 ;;
  esac
done
[ -n "$FASMG_BIN" ] || FASMG_BIN="$(command -v fasmg || true)"
if [ -z "$FASMG_BIN" ]; then
  for f in /nix/store/*fasmg*/bin/fasmg; do [ -x "$f" ] && { FASMG_BIN="$f"; break; }; done
fi
EXSC="$REPO/build/exsc"
[ -x "$EXSC" ] || { echo "selftest.sh: no $EXSC -- run make first"; exit 2; }
[ -x "$FASMG_BIN" ] || { echo "selftest.sh: no fasmg (pass --fasmg)"; exit 2; }
export INCLUDE="$REPO/vendor/fasmg-x86"

PASS=0; FAIL=0; SKIP=0
pass() { PASS=$((PASS + 1)); echo "  PASS  $*"; }
fail() { FAIL=$((FAIL + 1)); echo "  FAIL  $*"; }
skip() { SKIP=$((SKIP + 1)); echo "  SKIP  $*"; }
section() { echo; echo "== $*"; }

WORK="$(mktemp -d /dev/shm/exsc-usb-selftest.XXXXXX)"
chmod 0755 "$WORK"
STICK=""
cleanup() {
  [ -n "$STICK" ] && umount "$STICK" 2>/dev/null
  if [ "$KEEP" = 1 ]; then echo "kept: $WORK"; else rm -rf -- "$WORK"; fi
}
trap cleanup EXIT
P="$WORK/progs"; mkdir -p "$P"
RUNNER="$HERE/sandbox_run.py"
AS_NOBODY=()
if [ "$UNPRIV" = 1 ] && [ "$(id -u)" = 0 ] && command -v setpriv >/dev/null; then
  AS_NOBODY=(setpriv --reuid=65534 --regid=65534 --clear-groups)
fi

# rcase LABEL WANT_RC STDOUT_EXACT|- SUBSTR|- -- CMD...
#   SUBSTR must appear in stderr or stdout.
rcase() {
  local label="$1" want="$2" wout="$3" werr="$4"; shift 5
  local out err rc
  out="$WORK/o.$$"; err="$WORK/e.$$"
  "$@" >"$out" 2>"$err" </dev/null; rc=$?
  local ok=1 why=""
  [ "$rc" = "$want" ] || { ok=0; why="exit $rc, want $want"; }
  if [ "$wout" != - ] && [ "$(cat "$out")" != "$wout" ]; then
    ok=0; why="$why; stdout '$(head -c 80 "$out")', want '$wout'"; fi
  if [ "$werr" != - ] && ! grep -qF -- "$werr" "$err" "$out"; then
    ok=0; why="$why; output lacks '$werr'"; fi
  if [ "$ok" = 1 ]; then pass "$label (exit $rc)"; else
    fail "$label: $why"; sed 's/^/          | /' "$err" | head -n 6; fi
  rm -f "$out" "$err"
}

# ------------------------------------------------------------------ programs
section "build the probe programs (exsc + fasmg, and raw fasmg)"
for p in hello trap spin archivum_etc; do
  if "$EXSC" aedifica --hospes x86_64-linux "$HERE/tests/$p.exsc" -o "$P/$p.asm" >/dev/null 2>"$P/$p.log" \
     && "$FASMG_BIN" "$P/$p.asm" "$P/$p" >/dev/null 2>>"$P/$p.log"; then
    chmod 0755 "$P/$p"; pass "exsc+fasmg $p.exsc"
  else fail "build $p.exsc"; cat "$P/$p.log"; fi
done
# A loopback port something on the HOST listens on: the network probe must
# reach it from outside the sandbox and must not from inside.
PORT="$(python3 -c '
for line in open("/proc/net/tcp").readlines()[1:]:
    f = line.split()
    ip, port = f[1].split(":")
    if f[3] == "0A" and ip in ("0100007F", "00000000"):
        print(int(port, 16)); break' 2>/dev/null)"
for p in sleep12 openat2_probe openat_probe stdin_probe flood fork_probe net_probe; do
  extra=()
  [ "$p" = net_probe ] && extra=(-i "PORT = ${PORT:-1}")
  if "$FASMG_BIN" "${extra[@]}" "$HERE/tests/$p.asm" "$P/$p" >/dev/null 2>"$P/$p.log"; then
    chmod 0755 "$P/$p"; pass "fasmg $p.asm"
  else fail "fasmg $p.asm"; cat "$P/$p.log"; fi
done
chmod -R a+rX "$P"

section "what the binaries are (measured)"
for b in "$EXSC" "$FASMG_BIN" "$P/hello"; do
  echo "  $(basename "$b"): $(python3 "$RUNNER" --check-elf "$b" | sed 's/^.*: //')"
done

# ------------------------------------------------------------------ the contract
runner_cases() {  # runner_cases WHO PREFIX...
  local who="$1"; shift
  local R=("$@" "$RUNNER")
  rcase "[$who] stdout captured, exit status passed through" 7 salve - -- "${R[@]}" "$P/hello"
  rcase "[$who] runtime abort: abortus 1 on stderr, SIGILL -> 132" 132 "" "abortus 1" -- "${R[@]}" "$P/trap"
  rcase "[$who] archivum program cannot see the host's /etc (exit 1 = no root)" 1 "" - -- "${R[@]}" "$P/archivum_etc"
  rcase "[$who] raw openat2 /etc/passwd -> ENOENT (2)" 2 "" - -- "${R[@]}" "$P/openat2_probe"
  rcase "[$who] raw openat (not allowlisted) -> SIGSYS 159" 159 "" "SIGSYS" -- "${R[@]}" "$P/openat_probe"
  rcase "[$who] raw openat, --no-seccomp -> ENOENT (2): the empty root alone" 2 "" - -- "${R[@]}" --no-seccomp "$P/openat_probe"
  rcase "[$who] socket+connect -> SIGSYS 159" 159 "" "SIGSYS" -- "${R[@]}" "$P/net_probe"
  rcase "[$who] socket+connect host loopback, --no-seccomp -> ENETUNREACH (101): the netns alone" 101 "" - -- "${R[@]}" --no-seccomp "$P/net_probe"
  rcase "[$who] fail closed: unshare denied -> 125, program not run" 125 "" "refusing to run the program unsandboxed" -- "$@" "$HERE/tests/deny_syscalls.py" 272 -- "$RUNNER" "$P/hello"
  rcase "[$who] fail closed: mount denied (userns without caps) -> 125, not run" 125 "" "the program was not run" -- "$@" "$HERE/tests/deny_syscalls.py" 165 -- "$RUNNER" "$P/hello"
}

section "runner contract, as uid $(id -u)"
OUTSIDE=""
"$P/archivum_etc"; OUTSIDE="$?"
if [ "$OUTSIDE" = 0 ]; then pass "UNSANDBOXED, the archivum program opens the host's /etc/passwd (exit 0): the checker does not stop it"
else fail "unsandboxed archivum probe exit $OUTSIDE, expected 0"; fi
"$P/openat2_probe"; r=$?
[ "$r" = 0 ] && pass "UNSANDBOXED, raw openat2 opens /etc/passwd (exit 0)" || fail "unsandboxed openat2 exit $r"
if [ -n "$PORT" ]; then
  "$P/net_probe"; r=$?
  [ "$r" = 0 ] && pass "UNSANDBOXED, the net probe connects to a host loopback listener (exit 0)" \
    || fail "unsandboxed net probe exit $r (expected 0: a listener was found on 127.0.0.1)"
else
  skip "no host loopback listener found; the unsandboxed connect comparison is not made"
fi
runner_cases "uid $(id -u)"
echo x | "$RUNNER" "$P/stdin_probe" >/dev/null 2>&1; r=$?
[ "$r" = 0 ] && pass "stdin is /dev/null even when the caller's stdin has data (read -> EOF, exit 0)" \
  || fail "stdin probe exit $r, want 0"
rcase "dynamically linked ELF refused before anything runs -> 125" 125 "" "not freestanding" -- "$RUNNER" "$(type -P true)"
rcase "a non-ELF file refused -> 125" 125 "" "not an ELF" -- "$RUNNER" "$HERE/README.md"
out="$("$RUNNER" --max-output 65536 "$P/flood" 2>"$WORK/flood.err" | wc -c)"; r=${PIPESTATUS[0]}
if [ "$out" = 65536 ] && grep -q truncated "$WORK/flood.err"; then pass "stdout capped: 2 MiB written, 65536 bytes passed, truncation reported"
else fail "output cap: got $out bytes, exit $r"; fi
start=$SECONDS
rcase "Exsecutor infinite loop -> 124" 124 "" "timeout" -- "$RUNNER" "$P/spin"
el=$((SECONDS - start)); [ "$el" -le 12 ] && pass "  ...killed within 12 s of wall clock" || fail "  ...took ${el}s"
start=$SECONDS
rcase "12 s nanosleep, --no-seccomp (no CPU burnt: only the wall clock can stop it) -> 124" 124 "" "timeout" -- "$RUNNER" --no-seccomp "$P/sleep12"
el=$((SECONDS - start)); [ "$el" -le 12 ] && pass "  ...killed within 12 s of wall clock" || fail "  ...took ${el}s"
rcase "12 s nanosleep with seccomp -> SIGSYS 159 (nanosleep is not allowlisted)" 159 "" "SIGSYS" -- "$RUNNER" "$P/sleep12"
"$RUNNER" --no-seccomp --nproc 4 "$P/fork_probe" >/dev/null 2>&1; r=$?
echo "  INFO  fork probe as uid $(id -u), --nproc 4: $r of 64 forks succeeded (measured: RLIMIT_NPROC is not enforced for host root, consistent with the kernel exempting INIT_USER)"

if [ "${#AS_NOBODY[@]}" -gt 0 ]; then
  section "runner contract, UNPRIVILEGED (setpriv to uid 65534)"
  RUNNER_ORIG="$RUNNER"; RUNNER="$WORK/sandbox_run.py"
  cp "$RUNNER_ORIG" "$RUNNER"; chmod 0755 "$RUNNER"
  mkdir -p "$WORK/t"; cp "$HERE/tests/deny_syscalls.py" "$WORK/t/"; chmod -R a+rX "$WORK/t"
  HERE_ORIG="$HERE"; HERE="$WORK"; mkdir -p "$WORK/tests"; cp "$WORK/t/deny_syscalls.py" "$WORK/tests/"; chmod -R a+rX "$WORK/tests"
  runner_cases "uid 65534" "${AS_NOBODY[@]}"
  "${AS_NOBODY[@]}" "$RUNNER" --no-seccomp --nproc 4 "$P/fork_probe" >/dev/null 2>&1; r=$?
  if [ "$r" -lt 4 ]; then pass "[uid 65534] RLIMIT_NPROC 4 binds: $r of 64 forks succeeded"
  else fail "[uid 65534] NPROC 4 did not bind: $r forks"; fi
  RUNNER="$RUNNER_ORIG"; HERE="$HERE_ORIG"
else
  section "runner contract, UNPRIVILEGED"
  skip "not root, no setpriv, or --no-unpriv: the unprivileged path was exercised only if this user is unprivileged"
fi

# ------------------------------------------------------------------ the bundle
section "bundle: build twice, verify, determinism, no baked-in paths"
"$HERE/build_bundle.sh" --out "$WORK/b1/exsecutor-usb" | sed 's/^/  | /'
"$HERE/build_bundle.sh" --out "$WORK/b2/exsecutor-usb" >/dev/null
B="$WORK/b1/exsecutor-usb"
cmp -s "$B/MANIFEST" "$WORK/b2/exsecutor-usb/MANIFEST" \
  && pass "two builds in different directories: byte-identical MANIFEST" || fail "MANIFESTs differ"
bash "$B/verify.sh" >/dev/null && pass "verify.sh: bundle matches its MANIFEST" || fail "verify.sh on a fresh bundle"
if grep -rqF -e "$REPO" -e "$WORK" "$B"; then fail "an absolute build path is baked into the bundle: $(grep -rlF -e "$REPO" -e "$WORK" "$B" | head -3)"
else pass "neither the repo path nor the output path appears in any bundled file"; fi
if diff -r -q "$REPO/vendor/fasmg-x86" "$B/repo/vendor/fasmg-x86" >/dev/null; then
  crlf="$(grep -rlI $'\r' "$B/repo/vendor/fasmg-x86" | wc -l)"
  pass "vendor/fasmg-x86 byte-identical ($crlf CRLF files kept as CRLF)"
else fail "vendor copy differs"; fi
MSHA="$(sha256sum < "$B/MANIFEST" | cut -c1-64)"

section "tamper detection"
cp -R "$B" "$WORK/tamper"; TB="$WORK/tamper"
python3 - "$TB/bin/exsc" <<'PY'
import sys
p = sys.argv[1]
b = bytearray(open(p, "rb").read()); b[4096] ^= 0x01; open(p, "wb").write(bytes(b))
PY
bash "$TB/verify.sh" > "$WORK/v.out" 2>&1; r=$?
[ "$r" = 1 ] && grep -q "MISMATCH  bin/exsc" "$WORK/v.out" && pass "one flipped bit in bin/exsc: verify.sh names it (exit 1)" || fail "verify.sh missed the flip (exit $r)"
rcase "one flipped bit in bin/exsc: run.sh refuses before running anything" 125 - "does not match MANIFEST" -- bash "$TB/run.sh" --exec true
cp "$B/bin/exsc" "$TB/bin/exsc"
printf 'print("hi")\n' > "$TB/repo/tools/agent/loop/extra.py"
rcase "an unlisted extra.py beside loop.py: run.sh refuses" 125 - "does not match MANIFEST" -- bash "$TB/run.sh" --exec true
rm -f "$TB/repo/tools/agent/loop/extra.py"
rcase "--expect-manifest with the wrong hash: refused" 125 - "not the expected" -- bash "$TB/run.sh" --expect-manifest "$(printf '0%.0s' {1..64})" --exec true
rcase "--expect-manifest with the right hash: accepted" 0 - "MANIFEST matches --expect-manifest" -- bash "$TB/run.sh" --expect-manifest "$MSHA" --exec true

# ------------------------------------------------------------------ relocation
SESSION_PROBE='set -e
exsc aedifica --hospes x86_64-linux "$1" -o "$TMPDIR/t.asm" >/dev/null 2>&1 || exit 90
fasmg "$TMPDIR/t.asm" "$TMPDIR/t" >/dev/null || exit 91
set +e
"$EXSC_RUNNER" "$TMPDIR/t" 2>/dev/null; r=$?
[ "$r" = 132 ] || exit 92
python3 "$EXS_REPO/tools/agent/loop/loop.py" --print-system-prompt >/dev/null 2>&1 || exit 93
echo session-ok'
mkdir -p "$WORK/probe"; cp "$HERE/tests/trap.exsc" "$WORK/probe/"; chmod -R a+rX "$WORK/probe"
section "relocation: the bundle runs from anywhere"
for where in "/dev/shm/exsc-usb-copy.$$" "$WORK/elsewhere/with space/deep"; do
  mkdir -p "$where"; cp -R "$B" "$where/"
  label="$(echo "$where" | sed "s|$WORK|<work>|; s|\.$$\$|.<pid>|")"
  rcase "from $label, cwd /: exsc+fasmg+runner (trap -> 132) and loop.py inside the session" \
    0 session-ok - -- bash -c 'cd / && exec bash "$1/exsecutor-usb/run.sh" --exec bash -c "$2" _ "$3"' _ \
    "$where" "$SESSION_PROBE" "$WORK/probe/trap.exsc"
  rm -rf "$where"
done

# ------------------------------------------------------------------ noexec stick
section "a noexec 'stick' (ext4 loop image mounted noexec,nosuid,nodev)"
if [ "$(id -u)" = 0 ] && command -v mkfs.ext4 >/dev/null && truncate -s 64M "$WORK/stick.img" \
   && mkfs.ext4 -q "$WORK/stick.img" && mkdir -p "$WORK/stick" \
   && mount -o loop,noexec,nosuid,nodev "$WORK/stick.img" "$WORK/stick" 2>/dev/null; then
  STICK="$WORK/stick"
  cp -R "$B" "$STICK/"
  "$STICK/exsecutor-usb/bin/exsc" >/dev/null 2>&1; r=$?
  [ "$r" = 126 ] && pass "executing bin/exsc straight from the stick fails (exit 126: noexec)" || fail "direct exec from noexec gave $r"
  rcase "bash run.sh from the noexec stick: exsc+fasmg+runner run from the RAM copy" 0 session-ok "noexec=yes" -- \
    bash "$STICK/exsecutor-usb/run.sh" --exec bash -c "$SESSION_PROBE" _ "$WORK/probe/trap.exsc"
  umount "$STICK"; STICK=""
else
  skip "needs root, mkfs.ext4 and a loop device"
fi

# ------------------------------------------------------------------ a whole session + amnesia
section "a whole session: stub server on the namespace's loopback, loop, runner, wipe"
A="$WORK/amnesia/exsecutor-usb"; mkdir -p "$WORK/amnesia"; cp -R "$B" "$A"
mkdir -p "$A/llama"; cp "$HERE/tests/fake_llama_server.py" "$A/llama/llama-server"
printf 'not a real gguf\n' > "$A/models/stub.gguf"
rcase "llama-server unpinned: refused" 125 - "not pinned" -- bash "$A/run.sh" -- --task t
bash "$A/run.sh" --pin-local >/dev/null 2>&1 && pass "--pin-local wrote LOCAL.MANIFEST" || fail "--pin-local"
rcase "--model-in-ram with impossible headroom: refused before copying" 125 - "--model-in-ram: model is" -- bash "$A/run.sh" --model-in-ram --headroom-mib 99999999 --dry-run
rcase "--dry-run prints the exact server command" 0 - "--host 127.0.0.1 --port 8080 -c 8192 -ngl 99" -- bash "$A/run.sh" --dry-run
rcase "--model-in-ram copies the model into the RAM dir" 0 - "into RAM" -- bash "$A/run.sh" --model-in-ram --dry-run

touch "$WORK/marker"
WRITE_PROBE='for d in "$1" "$2" /dev/shm /var/tmp / "$3"; do
  if ( : > "$d/.exsc-amnesia-probe" ) 2>/dev/null; then echo "WROTE $d"; else echo "denied $d"; fi; done
: > "$TMPDIR/ram-ok" && echo "ram writable"'
HOST_HOME="$(getent passwd "$(id -u)" | cut -d: -f6)"
out="$(bash "$A/run.sh" --exec bash -c "$WRITE_PROBE" _ "$A" "$A/transcripts" "$HOST_HOME" 2>/dev/null)"
echo "$out" | sed 's/^/  | /' | sed "s|$WORK|<work>|g"
if echo "$out" | grep -q '^WROTE'; then fail "a write outside RAM succeeded inside the session"
elif [ "$(echo "$out" | grep -c '^denied')" = 6 ] && echo "$out" | grep -q 'ram writable'; then
  pass "inside the session: the stick, its transcripts/, /dev/shm, /var/tmp, / and \$HOME all refuse writes; RAM accepts them"
else fail "write probe output unexpected"; fi
rcase "the loop converges against the stub server, binary run through the runner" 0 - "converged after 2 iterations" -- \
  bash "$A/run.sh" -- --task "Print salve and exit with status 3." --expect-exit 3 --expect-stdout salve
changed="$( { find "$A" -newer "$WORK/marker"; find "$A" -cnewer "$WORK/marker";
              find /dev/shm /var/tmp -path "$WORK" -prune -o \( -newer "$WORK/marker" -o -cnewer "$WORK/marker" \) -print 2>/dev/null;
              find /tmp -path /tmp/claude-0 -prune -o \( -newer "$WORK/marker" -o -cnewer "$WORK/marker" \) -print 2>/dev/null; } | sort -u)"
if [ -z "$changed" ]; then pass "AMNESIA: after two sessions nothing changed on the stick, /dev/shm, /var/tmp or /tmp (find -newer/-cnewer)"
else fail "files changed outside RAM:"; echo "$changed" | sed 's/^/          | /' | head; fi

section "--save-transcripts: the only thing written is transcripts/session-N/"
touch "$WORK/marker2"
rcase "session with --save-transcripts" 0 - "saved transcripts to transcripts/session-1/" -- \
  bash "$A/run.sh" --save-transcripts -- --task "Print salve and exit with status 3." --expect-exit 3 --expect-stdout salve
changed="$(find "$A" -newer "$WORK/marker2" -o -cnewer "$WORK/marker2" | sed "s|^$A/||; s|^$A\$|.|" | sort -u)"
others="$(echo "$changed" | grep -v -e '^transcripts' -e '^\.$' || true)"
if [ -z "$others" ] && echo "$changed" | grep -q 'transcripts/session-1/transcripts/transcript.jsonl'; then
  pass "changed on the stick: only transcripts/ ($(echo "$changed" | grep -c '^transcripts/session-1/') paths under session-1/)"
else fail "unexpected changes: $others"; fi
[ "$(grep -c '"kind"' "$A/transcripts/session-1/transcripts/transcript.jsonl")" -ge 1 ] \
  && pass "saved transcript.jsonl holds the loop's records" || fail "transcript empty"

if [ "${#AS_NOBODY[@]}" -gt 0 ]; then
  section "a whole session, UNPRIVILEGED (uid 65534)"
  U="/dev/shm/exsc-usb-unpriv.$$"; mkdir -p "$U"; cp -R "$A" "$U/"; rm -rf "$U/exsecutor-usb/transcripts/"*
  chown -R 65534:65534 "$U"
  rcase "[uid 65534] loop session converges, saved transcript" 0 - "saved transcripts to transcripts/session-1/" -- \
    "${AS_NOBODY[@]}" env -i PATH=/usr/bin:/bin bash "$U/exsecutor-usb/run.sh" --save-transcripts -- \
    --task "Print salve and exit with status 3." --expect-exit 3 --expect-stdout salve
  rcase "[uid 65534] launcher fails closed when unshare is denied" 125 - "no sandbox, no session" -- \
    "${AS_NOBODY[@]}" env -i PATH=/usr/bin:/bin "$HERE/tests/deny_syscalls.py" 272 -- bash "$U/exsecutor-usb/run.sh" --exec true
  rm -rf "$U"
fi

echo
echo "selftest: $PASS passed, $FAIL failed, $SKIP skipped"
[ "$FAIL" = 0 ]
