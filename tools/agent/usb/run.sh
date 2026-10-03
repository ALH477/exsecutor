#!/usr/bin/env bash
# exsecutor-usb/run.sh  (source: tools/agent/usb/run.sh)
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
#
# The launcher. Run it with `bash run.sh ...` -- a FAT/exFAT stick is often
# mounted noexec and cannot hold exec bits, which is why nothing is executed
# from the stick: everything executable is copied into RAM, verified there
# against MANIFEST, and only then run.
#
# The whole script is functions; the last line calls main. bash therefore
# parses all of it before running any of it, and a stick that is unplugged or
# rewritten mid-session cannot change what this process does.

set -euo pipefail

usage() {
  cat <<'EOF'
usage: bash run.sh [OPTIONS] [-- LOOP_ARGS...]
       bash run.sh [OPTIONS] --exec CMD [ARG...]
       bash run.sh --pin-local

Runs the Exsecutor compile-feedback loop in a RAM sandbox:
  1. reports the stick's filesystem and mount flags;
  2. enters new user + mount + pid + net namespaces (fails closed, exit 125,
     when unprivileged user namespaces are unavailable);
  3. mounts a fresh tmpfs visible only inside them, copies bin/ lib/ repo/
     into it and verifies every copy against MANIFEST BEFORE running any;
  4. remounts every other mount read-only inside the namespace, so neither
     the host's disks nor the stick can be written (amnesic by default);
  5. brings up loopback only: the model server and the loop can talk on
     127.0.0.1, and there is no other interface, so nothing else can;
  6. starts the model server (llama/llama-server or bin/llama-server, with
     models/*.gguf), runs the loop with the sandbox runner, then wipes RAM.

Loop arguments go after `--` and are passed to repo/tools/agent/loop/loop.py
verbatim; the launcher supplies --endpoint --model --run --runner --exsc
--fasmg --include --repo --workdir --transcript. Example:
  bash run.sh -- --task "exit with status 3" --expect-exit 3

options:
  --model NAME          a file in models/ (default: the only *.gguf there)
  --model-in-ram        copy the model into the RAM sandbox first (refused
                        unless MemAvailable >= model size + --headroom-mib).
                        Default: the server mmaps it from the stick -- slow
                        to load from a slow stick.
  --headroom-mib N      RAM to leave free when --model-in-ram (default 4096)
  --port N              server port on the namespace's loopback (default 8080)
  --ctx N               server context size, -c (default 8192)
  --ngl N               layers to offload to the GPU, -ngl (default 99)
  --server-arg ARG      extra llama-server argument (repeatable)
  --dry-run             do everything up to starting the server; print the
                        exact server and loop commands instead of running them
  --exec CMD...         run CMD inside the session instead of the loop (all
                        remaining arguments; for inspection and selftest.sh)
  --save-transcripts    copy the transcript and programs back to the stick,
                        transcripts/session-N/, on exit. Without it nothing
                        is written anywhere but RAM.
  --expect-manifest SHA refuse unless sha256(MANIFEST) is SHA (the value
                        build_bundle.sh printed; keep it off the stick)
  --pin-local           hash your own files (llama/**, bin/llama-server,
                        models/*.gguf) into LOCAL.MANIFEST on the stick and
                        exit. The server is never run from unpinned files.
  -h, --help            this text

exit: the loop's (or --exec command's) status; 125 sandbox unavailable or a
verification failure; 2 usage or missing pieces.
EOF
}

say()  { printf 'run.sh: %s\n' "$*" >&2; }
show_cmd() { local s; s="$(printf '%q ' "$@")"; printf '  %s\n' "${s% }" >&2; }
die()  { say "$*"; exit 2; }
fail_closed() { say "$*"; say "REFUSING to continue (exit 125)."; exit 125; }

# --------------------------------------------------------------- arguments
parse_args() {
  MODEL=""; MODEL_IN_RAM=0; HEADROOM_MIB=4096; PORT=8080; CTX=8192; NGL=99
  SERVER_ARGS=(); DRY=0; EXEC=(); SAVE=0; EXPECT=""; PIN=0; LOOP_ARGS=()
  while [ $# -gt 0 ]; do
    case "$1" in
      -h|--help) usage; exit 0 ;;
      --model) MODEL="${2:?--model needs a name}"; shift 2 ;;
      --model-in-ram) MODEL_IN_RAM=1; shift ;;
      --headroom-mib) HEADROOM_MIB="${2:?}"; shift 2 ;;
      --port) PORT="${2:?}"; shift 2 ;;
      --ctx) CTX="${2:?}"; shift 2 ;;
      --ngl) NGL="${2:?}"; shift 2 ;;
      --server-arg) SERVER_ARGS+=("${2?}"); shift 2 ;;
      --dry-run) DRY=1; shift ;;
      --save-transcripts) SAVE=1; shift ;;
      --expect-manifest) EXPECT="${2:?}"; shift 2 ;;
      --pin-local) PIN=1; shift ;;
      --exec) shift; [ $# -gt 0 ] || die "--exec needs a command"; EXEC=("$@"); break ;;
      --) shift; LOOP_ARGS=("$@"); break ;;
      *) die "unknown option: $1 (try --help)" ;;
    esac
  done
  for v in "$HEADROOM_MIB" "$PORT" "$CTX" "$NGL"; do
    [[ "$v" =~ ^[0-9]+$ ]] || die "not a number: $v"
  done
  case "$MODEL" in */*|..|.) die "--model takes a file name in models/, not a path" ;; esac
}

# --------------------------------------------------------------- helpers
gguf_pick() {  # sets MODEL_FILE (relative to the bundle) or empty
  MODEL_FILE=""
  if [ -n "$MODEL" ]; then
    [ -f "$BUNDLE/models/$MODEL" ] || die "no such model: models/$MODEL"
    MODEL_FILE="models/$MODEL"; return
  fi
  local found=() f
  for f in "$BUNDLE"/models/*.gguf; do [ -f "$f" ] && found+=("models/${f##*/}"); done
  if [ "${#found[@]}" -eq 1 ]; then MODEL_FILE="${found[0]}"
  elif [ "${#found[@]}" -gt 1 ]; then die "several models in models/; pick one with --model"
  fi
}

server_pick() {  # sets SERVER_REL (relative) or empty
  SERVER_REL=""
  if [ -f "$BUNDLE/llama/llama-server" ]; then SERVER_REL="llama/llama-server"
  elif [ -f "$BUNDLE/bin/llama-server" ]; then SERVER_REL="bin/llama-server"
  fi
}

pin_local() {
  local out="$BUNDLE/LOCAL.MANIFEST" tmp
  tmp="$(cd "$BUNDLE" && {
    [ -d llama ] && find llama -type f -print0
    [ -f bin/llama-server ] && printf '%s\0' bin/llama-server
    find models -maxdepth 1 -type f -name '*.gguf' -print0 2>/dev/null
  } | LC_ALL=C sort -z | while IFS= read -r -d '' f; do
        printf '%s  %s\n' "$(sha256sum < "$f" | cut -c1-64)" "$f"
      done)"
  [ -n "$tmp" ] || die "nothing to pin: no llama/, bin/llama-server or models/*.gguf"
  printf '%s\n' "$tmp" > "$out"
  say "wrote LOCAL.MANIFEST ($(wc -l < "$out") files). Its sha256: $(sha256sum < "$out" | cut -c1-64)"
}

# =============================================================== stage 1 (host)
stage1() {
  say "bundle: $BUNDLE"
  for t in python3 unshare mount sha256sum find cp; do
    command -v "$t" >/dev/null || fail_closed "missing required tool: $t"
  done
  [ "$(uname -m)" = x86_64 ] || fail_closed "x86-64 only (this is $(uname -m))"

  local fs
  fs="$(python3 "$BUNDLE/lib/session.py" fsinfo "$BUNDLE" 2>/dev/null || echo "fstype=unknown")"
  say "stick filesystem: $fs"
  case "$fs" in
    *"fat=yes"*) say "FAT holds no exec bits and caps files at 4 GiB: a GGUF bigger than that cannot live on it. Prefer exFAT or ext4." ;;
    *"fat=exfat"*) say "exFAT holds no exec bits; that is fine -- every executable is copied to RAM before it runs." ;;
  esac
  case "$fs" in
    *"noexec=yes"*) say "mounted noexec: expected; executables run only from the RAM copy." ;;
  esac
  if ! python3 "$BUNDLE/lib/session.py" swapinfo >/dev/null 2>&1; then
    say "WARNING: disk-backed swap is active; RAM pages of this session (transcripts, the model) could be written to it. Amnesia holds only without disk swap (or with encrypted swap)."
  fi

  if [ "$PIN" = 1 ]; then pin_local; exit 0; fi

  local err
  if ! err="$(unshare -r -m -n -p --fork --mount-proc true 2>&1)"; then
    say "cannot create user+mount+pid+net namespaces: ${err:-unknown error}"
    say "possible causes: kernel.unprivileged_userns_clone=0, user.max_user_namespaces=0,"
    say "kernel.apparmor_restrict_unprivileged_userns=1 (Ubuntu 23.10+), or a container seccomp profile."
    fail_closed "no sandbox, no session: nothing is run unsandboxed"
  fi

  if [ "$SAVE" = 1 ]; then
    mkdir -p "$BUNDLE/transcripts" || die "cannot create $BUNDLE/transcripts"
  fi

  # A minimal, explicit environment: nothing ambient leaks into the session.
  exec env -i PATH="${PATH:-/usr/bin:/bin}" TERM="${TERM:-dumb}" LC_ALL=C \
    EXSC_USB_STAGE=2 EXSC_USB_BUNDLE="$BUNDLE" \
    unshare -r -m -n -p --fork --mount-proc --propagation private \
    -- bash "$BUNDLE/run.sh" "$@"
}

# =============================================================== stage 2 (namespaces)
cleanup() {
  local rc=$?
  trap - EXIT INT TERM
  if [ -n "${SPID:-}" ]; then kill "$SPID" 2>/dev/null || true; wait "$SPID" 2>/dev/null || true; fi
  if [ "$SAVE" = 1 ] && [ "${STARTED:-0}" = 1 ] && [ -d "$T/out" ]; then
    local n=1
    while [ -e "$T/out/session-$n" ]; do n=$((n + 1)); done
    mkdir "$T/out/session-$n" \
      && cp -R "$W/transcripts" "$W/work" "$T/out/session-$n/" 2>/dev/null \
      && say "saved transcripts to transcripts/session-$n/ on the stick" \
      || say "WARNING: could not save transcripts"
    umount "$T/out" 2>/dev/null || true
  fi
  if [ -n "${W:-}" ]; then rm -rf -- "$W"; fi
  if [ -n "${T:-}" ]; then cd /; umount "$T" 2>/dev/null || true; fi
  say "RAM sandbox wiped (exit $rc)"
  exit "$rc"
}

stage2() {
  BUNDLE="$EXSC_USB_BUNDLE"
  local m cand
  T=""
  for cand in /tmp /mnt /media /opt /srv /var/tmp; do
    [ -d "$cand" ] || continue
    case "$BUNDLE/" in "$cand"/*) continue ;; esac
    T="$cand"; break
  done
  [ -n "$T" ] || fail_closed "no directory to mount the RAM sandbox on"

  gguf_pick
  server_pick
  local model_bytes=0 ram_mib
  [ -n "$MODEL_FILE" ] && model_bytes="$(stat -c %s "$BUNDLE/$MODEL_FILE")"
  read -r avail total < <(python3 -c '
import sys
v={}
for l in open("/proc/meminfo"):
    k,_,x=l.partition(":"); v[k]=int(x.split()[0])*1024
print(v.get("MemAvailable",0), v.get("MemTotal",0))')
  ram_mib=1024
  if [ "$MODEL_IN_RAM" = 1 ] && [ -n "$MODEL_FILE" ]; then
    local need=$(( model_bytes + HEADROOM_MIB * 1048576 ))
    if [ "$avail" -lt "$need" ]; then
      fail_closed "--model-in-ram: model is $((model_bytes >> 20)) MiB, headroom $HEADROOM_MIB MiB, but only $((avail >> 20)) MiB available"
    fi
    ram_mib=$(( 1024 + (model_bytes >> 20) + 1 ))
  fi
  [ -d "$BUNDLE/llama" ] && ram_mib=$(( ram_mib + $(du -sm "$BUNDLE/llama" | cut -f1) ))

  mount -t tmpfs -o "size=${ram_mib}m,mode=0700,nosuid,nodev" exsc-ram "$T" \
    || fail_closed "cannot mount the RAM tmpfs on $T"
  W="$T/exsc"
  trap cleanup EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM
  mkdir -p "$W"/{home,tmp,work,transcripts}
  if [ "$SAVE" = 1 ]; then
    # The ONE writable path outside RAM, and only with --save-transcripts:
    # the stick's transcripts/ directory, bound in before everything else
    # is made read-only. Nothing writes to it until cleanup.
    mkdir "$T/out"
    mount --bind "$BUNDLE/transcripts" "$T/out" \
      || fail_closed "cannot bind $BUNDLE/transcripts for --save-transcripts"
  fi
  say "RAM sandbox: fresh tmpfs (${ram_mib} MiB cap) on $T, visible only inside this session's namespaces"

  # Copy, then verify the COPIES: what runs is what was checked.
  cp "$BUNDLE/MANIFEST" "$BUNDLE/verify.sh" "$W/"
  cp -R "$BUNDLE/bin" "$BUNDLE/lib" "$BUNDLE/repo" "$W/"
  rm -f "$W/bin/llama-server"
  if [ -n "$EXPECT" ]; then
    m="$(sha256sum < "$W/MANIFEST" | cut -c1-64)"
    [ "$m" = "$EXPECT" ] || fail_closed "MANIFEST sha256 is $m, not the expected $EXPECT"
    say "MANIFEST matches --expect-manifest"
  else
    printf '%s\n' "run.sh: WARNING: the MANIFEST is UNANCHORED (no --expect-manifest). verify.sh only checks the" \
      "  files against a MANIFEST that lives on the same stick, so anyone who could write the stick could" \
      "  rewrite a file and its MANIFEST line together and this run would still say OK. Build the bundle on a" \
      "  trusted machine, keep the MANIFEST sha256 it prints OFF the stick, and pass it with --expect-manifest." >&2
  fi
  local want got
  want="$(grep -E '  verify\.sh$' "$W/MANIFEST" | cut -c1-64 || true)"
  got="$(sha256sum < "$W/verify.sh" | cut -c1-64)"
  [ -n "$want" ] && [ "$want" = "$got" ] || fail_closed "verify.sh does not match MANIFEST"
  want="$(grep -E '  run\.sh$' "$W/MANIFEST" | cut -c1-64 || true)"
  got="$(sha256sum < "$BUNDLE/run.sh" | cut -c1-64)"
  [ "$want" = "$got" ] || fail_closed "run.sh on the stick does not match MANIFEST"
  bash "$W/verify.sh" --root "$W" --only bin,lib,repo >&2 \
    || fail_closed "the RAM copy does not match MANIFEST (tampered or corrupt stick)"

  if [ -n "$SERVER_REL" ] && [ "$DRY" = 0 ]; then
    [ -f "$BUNDLE/LOCAL.MANIFEST" ] || fail_closed "$SERVER_REL is not pinned: run \`bash run.sh --pin-local\` once, after checking it"
    cp "$BUNDLE/LOCAL.MANIFEST" "$W/LOCAL.MANIFEST"
    if [ "$SERVER_REL" = llama/llama-server ]; then cp -R "$BUNDLE/llama" "$W/llama"
    else mkdir -p "$W/userbin"; cp "$BUNDLE/bin/llama-server" "$W/userbin/llama-server"; fi
  fi
  chmod -R u+rwX,go-rwx "$W"
  chmod 0755 "$W/bin/exsc" "$W/bin/fasmg" "$W/bin/sandbox_run.py" "$W/lib/session.py"

  STARTED=1
  python3 "$W/lib/session.py" ro-host "$T" /proc >&2 \
    || fail_closed "could not make the host's mounts read-only inside the session"
  python3 "$W/lib/session.py" lo-up >&2 \
    || fail_closed "network namespace is not loopback-only"

  # The model and the server.
  local model_path="" server=""
  if [ -n "$MODEL_FILE" ]; then
    if [ "$MODEL_IN_RAM" = 1 ]; then
      mkdir -p "$W/models"
      say "copying $MODEL_FILE ($((model_bytes >> 20)) MiB) into RAM"
      cp "$BUNDLE/$MODEL_FILE" "$W/models/"
      model_path="$W/$MODEL_FILE"
    else
      model_path="$BUNDLE/$MODEL_FILE"
      say "model will be mmapped from the stick (read-only here); loading is bounded by the stick's read speed"
    fi
  fi
  if [ "$SERVER_REL" = llama/llama-server ]; then server="$W/llama/llama-server"
  elif [ -n "$SERVER_REL" ]; then server="$W/userbin/llama-server"; fi

  local scmd=("${server:-<llama-server>}" -m "${model_path:-<models/*.gguf>}" --host 127.0.0.1
              --port "$PORT" -c "$CTX" -ngl "$NGL" "${SERVER_ARGS[@]}")
  local mname="${MODEL_FILE##*/}"
  local lcmd=(python3 "$W/repo/tools/agent/loop/loop.py"
              --endpoint "http://127.0.0.1:$PORT" --model "${mname:-model}"
              --run --runner "$W/bin/sandbox_run.py"
              --exsc "$W/bin/exsc" --fasmg "$W/bin/fasmg"
              --include "$W/repo/vendor/fasmg-x86" --repo "$W/repo"
              --workdir "$W/work" --keep-workdir
              --transcript "$W/transcripts/transcript.jsonl" "${LOOP_ARGS[@]}")

  export HOME="$W/home" TMPDIR="$W/tmp" XDG_CACHE_HOME="$W/home/.cache" \
         XDG_CONFIG_HOME="$W/home/.config" XDG_DATA_HOME="$W/home/.local/share" \
         PYTHONDONTWRITEBYTECODE=1 PATH="$W/bin:$PATH" \
         EXSC="$W/bin/exsc" FASMG="$W/bin/fasmg" INCLUDE="$W/repo/vendor/fasmg-x86" \
         EXSC_RUNNER="$W/bin/sandbox_run.py" EXS_REPO="$W/repo" \
         EXSC_USB_RAM="$W" EXSC_TRANSCRIPTS="$W/transcripts"
  cd "$W/work"

  if [ "$DRY" = 1 ] || [ -z "$server" ] || [ -z "$model_path" ]; then
    [ "$DRY" = 1 ] || say "no model server: need llama/llama-server (or bin/llama-server) AND models/*.gguf"
    say "[dry-run] server command:"
    show_cmd "${scmd[@]}"
    say "[dry-run] loop command:"
    show_cmd "${lcmd[@]}"
    if [ "${#EXEC[@]}" -gt 0 ]; then
      say "running --exec inside the session"
      local rc=0; "${EXEC[@]}" || rc=$?; return "$rc"
    fi
    [ "$DRY" = 1 ] && return 0
    return 2
  fi

  local entry; local pinned=0
  while IFS= read -r entry; do
    local h="${entry:0:64}" p="${entry:66}" f
    case "$p" in
      llama/*|bin/llama-server) f="$W/${p/#bin\/llama-server/userbin/llama-server}" ;;
      models/*) [ "$p" = "$MODEL_FILE" ] || continue; f="$model_path" ;;
      *) continue ;;
    esac
    [ -f "$f" ] || continue
    [ "$(sha256sum < "$f" | cut -c1-64)" = "$h" ] || fail_closed "$p does not match LOCAL.MANIFEST"
    pinned=$((pinned + 1))
  done < "$W/LOCAL.MANIFEST"
  grep -qE "  ${SERVER_REL//./\\.}\$" "$W/LOCAL.MANIFEST" || fail_closed "$SERVER_REL is not in LOCAL.MANIFEST"
  grep -qE "  ${MODEL_FILE//./\\.}\$" "$W/LOCAL.MANIFEST" || fail_closed "$MODEL_FILE is not in LOCAL.MANIFEST"
  if [ -d "$W/llama" ]; then
    while IFS= read -r -d '' f; do
      grep -qF "  ${f#"$W/"}" "$W/LOCAL.MANIFEST" || fail_closed "unpinned file in llama/: ${f#"$W/"}"
    done < <(find "$W/llama" -type f -print0)
  fi
  say "LOCAL.MANIFEST: $pinned pinned file(s) verified"
  chmod 0755 "$server"

  say "starting the model server:"
  show_cmd "${scmd[@]}"
  if [ -d "$W/llama" ]; then export LD_LIBRARY_PATH="$W/llama"; fi
  "${scmd[@]}" > "$W/transcripts/server.log" 2>&1 &
  SPID=$!
  python3 "$W/lib/session.py" wait-http "http://127.0.0.1:$PORT/health" 600 "$SPID" >&2 \
    || { tail -n 20 "$W/transcripts/server.log" >&2; fail_closed "model server did not come up"; }
  say "model server is up on 127.0.0.1:$PORT (this namespace's loopback only)"

  local rc=0
  if [ "${#EXEC[@]}" -gt 0 ]; then
    "${EXEC[@]}" || rc=$?
  else
    say "loop command:"
    show_cmd "${lcmd[@]}"
    "${lcmd[@]}" || rc=$?
  fi
  return "$rc"
}

main() {
  parse_args "$@"
  if [ "${EXSC_USB_STAGE:-1}" = 2 ]; then
    stage2
    return $?
  fi
  BUNDLE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
  stage1 "$@"
}

main "$@"; exit $?
