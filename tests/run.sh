#!/usr/bin/env bash
# tests/run.sh -- minimal, real test harness.
#
# Four phases:
#   1. run_unit_tests: discovers tests/unit/*.asm, assembles each with
#      fasmg, and checks the expectations declared in its `; TEST:`
#      directive comment (run=yes|no, expect-exit=<N>, audit=pass|fail|skip).
#      This is the only phase that can do anything today -- there is no
#      compiler's own modules and the toolchain; exsc exists and the
#      driver_* fixtures drive it.
#   2. run_conformance_tests: tests/conformance/ (spec §14, 24 entries),
#      driven by build/exsc; entries the built stages can decide are
#      status=run, the rest DEFERRED and never counted as passing. This
#      comment once said "a deliberate no-op until exsc.asm exists"; it
#      was kept as a separate phase precisely so wiring it up
#      later does not require touching run_unit_tests.
#   3. run_ir_tests: tests/ir/*.ir, SSA IR text fed through tests/ir/
#      emit_ir.asm (parse -> VERIFY -> bfa_emit_program), the fasmg program
#      it prints assembled, RUN, and its exit status / abort / stdout
#      checked, then audited with --potestates Mundus,ambitus.
#   4. run_program_tests: tests/programs/<name>/, Exsecutor sources compiled
#      by exsc, assembled, run and audited the same way.
#   Phases 3 and 4 are the first place in this script that executes code a
#   compiler EMITTED; everything before them compares text or diagnostics.
#
# See tests/README.md for the directive format and how to add a fixture.
#
# Exit status: 0 if every check in every phase passed, 1 otherwise.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FASMG="${FASMG:-fasmg}"
export INCLUDE="${INCLUDE:-$REPO_ROOT/vendor/fasmg-x86}"
AUDIT="$REPO_ROOT/tools/syscall-audit.sh"

# Minimum number of tests/unit/*.asm fixtures that MUST be discovered. Fewer
# than this is a FAILURE, not a pass.
#
# This exists because a check that cannot see what it is checking reports
# green, and this project has produced that outcome four times: a smoke
# fixture asserted against the wrong syscall, a determinism diff between two
# empty directories, a vacuous registry check, and -- the one that motivated
# this line -- `nix flake check` running 4 of 25 fixtures because the rest
# were untracked and flakes filter source to the git-tracked tree. Every one
# of those printed PASS.
#
# Raise it deliberately when fixtures are added. Adding fixtures never TRIPS
# this -- the test is `found < floor` -- so a floor that drifts below the real
# count still catches the failure mode that matters: a discovery mechanism
# silently finding nothing. Drift costs precision, not the guarantee.
UNIT_FIXTURE_FLOOR="${UNIT_FIXTURE_FLOOR:-158}"

# The same guarantee for the two run phases below: tests/ir/*.ir fixtures,
# and tests/programs/*/ directories. Same rule -- `found < floor` fails --
# and the same reason. Raise each in the commit that adds a fixture.
#
# PROGRAM_FIXTURE_FLOOR COUNTS DIRECTORIES THAT RUN, not directories that
# exist: a `status=deferred` program (run_program_tests' header) is counted
# separately and never toward this floor, for the reason the conformance
# suite never counts a deferred entry as passing. It went from 6 to 5 when
# ordo_maior_custodia/ was retired -- that directory expected exsc to REFUSE
# a `u64:maior` field read, milestone M6 made the read compile, and the
# positive test of what it compiles to is tests/unit/lwr_transitus.asm
# (`load u64 %0 0 maior`), because the emitter cannot run a `maior` load yet.
IR_FIXTURE_FLOOR="${IR_FIXTURE_FLOOR:-48}"
PROGRAM_FIXTURE_FLOOR="${PROGRAM_FIXTURE_FLOOR:-8}"

PASS=0
FAIL=0

ok()  { PASS=$((PASS + 1)); echo "  [ok]   $*"; }
bad() { FAIL=$((FAIL + 1)); echo "  [FAIL] $*"; }
note() { echo "  -      $*"; }

# Prints the space-joined key=value tokens from a fixture's `; TEST:` line,
# or nothing if it has none (defaults apply -- see below).
directive_of() {
  local file="$1" line
  line="$(grep -m1 '^; TEST:' "$file" || true)"
  echo "${line#; TEST:}"
}

run_unit_tests() {
  echo "== unit fixtures (tests/unit/) =="
  if ! command -v "$FASMG" >/dev/null 2>&1; then
    bad "fasmg not found on PATH (set FASMG=/path/to/fasmg) -- cannot run unit fixtures"
    return
  fi

  local workdir found=0
  workdir="$(mktemp -d)"

  shopt -s nullglob
  local src
  for src in "$REPO_ROOT"/tests/unit/*.asm; do
    found=$((found + 1))
    local name run_flag expect_exit audit_flag directive kv
    name="$(basename "$src")"
    echo "-- $name"

    run_flag="yes"; expect_exit="0"; audit_flag="skip"
    directive="$(directive_of "$src")"
    if [[ -n "$directive" ]]; then
      for kv in $directive; do
        case "$kv" in
          run=*) run_flag="${kv#run=}" ;;
          expect-exit=*) expect_exit="${kv#expect-exit=}" ;;
          audit=*) audit_flag="${kv#audit=}" ;;
        esac
      done
    fi

    local out="$workdir/$name.out"
    if "$FASMG" "$src" "$out" >"$workdir/$name.asmlog" 2>&1; then
      chmod +x "$out"
      ok "$name: assembles"
    else
      bad "$name: assemble failed"
      sed 's/^/         /' "$workdir/$name.asmlog"
      continue
    fi

    if [[ "$run_flag" == "yes" ]]; then
      local rc=0
      "$out" </dev/null >"$workdir/$name.runlog" 2>&1 || rc=$?
      if [[ "$rc" == "$expect_exit" ]]; then
        ok "$name: runs, exit=$rc (expected $expect_exit)"
      else
        bad "$name: exit=$rc, expected $expect_exit"
        sed 's/^/         /' "$workdir/$name.runlog"
      fi
    else
      note "$name: run=no (deliberately not executed -- see fixture header)"
    fi

    if [[ "$audit_flag" != "skip" ]]; then
      local arc=0 verdict
      "$AUDIT" "$out" >"$workdir/$name.auditlog" 2>&1 || arc=$?
      verdict="fail"; [[ "$arc" == "0" ]] && verdict="pass"
      if [[ "$verdict" == "$audit_flag" ]]; then
        ok "$name: audit verdict=$verdict (expected $audit_flag)"
      else
        bad "$name: audit verdict=$verdict, expected $audit_flag"
        sed 's/^/         /' "$workdir/$name.auditlog"
      fi
    fi
  done
  shopt -u nullglob

  rm -rf "$workdir"
  if [[ "$found" -lt "$UNIT_FIXTURE_FLOOR" ]]; then
    bad "discovered $found fixtures in tests/unit/, floor is $UNIT_FIXTURE_FLOOR"
    note "a harness that finds nothing must not report success -- see"
    note "UNIT_FIXTURE_FLOOR at the top of this file for why this check exists"
    if [[ "$found" -gt 0 ]]; then
      note "under Nix, check that the fixtures are git-tracked: flakes filter"
      note "source to the tracked tree, so an untracked fixture is invisible"
    fi
  else
    note "discovered $found fixtures (floor $UNIT_FIXTURE_FLOOR)"
  fi
}

run_conformance_tests() {
  # ---------------------------------------------------------------------
  # spec §14, 24 entries, FIVE rule shapes (tests/README.md, "24 entries,
  # five rule shapes" -- a runner that assumes one shape quietly mishandles
  # four):
  #
  #   code   -- "exsc rejects this source with exactly code EXS-Exxxx"
  #             (entries 2-14, 18-22)
  #   bytes  -- byte-identical output across conditions/hosts (16, 17) --
  #             tools/reproduce.sh already knows how to check this; called
  #             from below, never reimplemented
  #   cert   -- an EXTERNAL certificate, vendor/hydramesh-wire's 246 golden
  #             vectors, not a case this project wrote for itself (23 only)
  #   abort  -- a RUNTIME abort, not a compile failure -- needs a built and
  #             EXECUTED binary (15 only)
  #   nocap  -- a capability-absence violation with NO code assigned in
  #             §13 at all (1 only) -- confirmed directly against §2.4's
  #             unrepresentability table, whose "Locale case fold in
  #             program logic" row has an em dash in the Error column,
  #             same as two other rows and unlike every code-bearing row
  #             around it. Never invent one (CLAUDE.md).
  #
  # PER-FIXTURE EXPECTATION DIRECTIVE. run_unit_tests' `; TEST:` (above)
  # cannot be reused as-is: `.exsc` source has no `;` comment syntax to
  # begin with (spec §8.4: "Comments are `//` to end of line" -- full
  # stop, no semicolon form exists), and a `.exsc` fixture for entries 18
  # or 20 needs to stay parseable Exsecutor-shaped source around the one
  # violation under test, not accidentally read as a second one. One line,
  # anywhere in the fixture, `//`-commented, space-separated key=value
  # tokens, mirroring directive_of's own format one section up:
  #
  #   // TEST: entry=<1-24> shape=<code|bytes|cert|abort|nocap>
  #            [expect-code=EXS-E0XXX] status=<run|deferred> [needs=<token>]
  #
  #   entry=N       the §14 entry number this fixture exercises.
  #   shape=        one of the five above.
  #   expect-code=  the exact EXS-Exxxx exsc must reject with. Required
  #                 when shape=code; absent for the other four shapes
  #                 (bytes/cert/abort have no code at all; nocap has none
  #                 BY DEFINITION -- see above).
  #   status=       run      -- this function actually exercises the
  #                             fixture and the result counts toward
  #                             PASS/FAIL below.
  #                 deferred -- the machinery this entry needs does not
  #                             exist yet. Reported as deferred and NEVER
  #                             counted as passing (CLAUDE.md: "Never
  #                             report a test you did not see pass").
  #   needs=        required when status=deferred (comma-separated if more
  #                 than one): what it is waiting on -- parser,
  #                 type_checker, capability_checker, lexicon_checker,
  #                 wire_layout_checker, ffi_checker, import_closure,
  #                 backend, runtime, cross_compile, wire_codec.
  #
  # WHY A DIRECTIVE PER FIXTURE RATHER THAN A TABLE HERE: codes.inc vs §13
  # is this project's own argument against a second, hand-maintained copy
  # of the same facts (CLAUDE.md, "§13's registry is the only source") --
  # applied to this suite, the fixture is the one place that should say
  # what it expects, so adding an entry never means updating two files in
  # sync by hand.
  #
  # HOW EACH SHAPE IS ACTUALLY CHECKED, this wave:
  #   code, status=run   -- build compiler/x86_64/exsc.asm (once, reused
  #                         for every such fixture) and run
  #                         `exsc aedifica --hospes x86_64-linux
  #                         --diagnostica json FIXTURE`; §8.3's own
  #                         machine-readable mode is matched on its "code"
  #                         member via a plain substring check, never on
  #                         English text. Front-end diagnostics have no
  #                         target concept (§8.1-§8.4 predate targets
  #                         entirely), so which --hospes triple is used
  #                         here is not load-bearing.
  #   bytes, status=run   -- tools/reproduce.sh, reused verbatim per this
  #                         agent's brief ("already knows how to do that
  #                         -- reuse it, do not reimplement"); guarded
  #                         against counting its own honest fallback (it
  #                         prints a banner naming the fixture it actually
  #                         built when exsc can't be) as a pass for this
  #                         suite.
  #   cert / abort / nocap -- no fixture claims status=run for these this
  #                         wave (nothing exists to check them against:
  #                         no backend, no executed binary, no capability
  #                         checker). Dispatched defensively below so a
  #                         future fixture that DID claim status=run
  #                         without real support fails loudly instead of
  #                         being silently treated as a pass.
  #
  # FLOORS, mirroring UNIT_FIXTURE_FLOOR above but local to this function
  # (this project has produced four green checks that saw nothing; a
  # conformance suite silently running zero entries -- or silently losing
  # fixtures -- would be the fifth):
  #   fixture_floor -- §14 has exactly 24 entries; fewer *.exsc files than
  #                    that means fixtures went missing, not that §14 shrank.
  #   run_floor     -- entries 3, 5, 18, 19, 20 are lexically checkable
  #                    today and verified passing (see this suite's own
  #                    report); if the number that actually RUN ever drops
  #                    below that, something silently stopped working.
  echo "== conformance suite (tests/conformance/, spec §14) =="
  local dir="$REPO_ROOT/tests/conformance"
  local fixture_floor=24
  local run_floor=5

  if [[ ! -d "$dir" ]]; then
    bad "tests/conformance/ does not exist"
    return
  fi

  shopt -s nullglob
  local fixtures=("$dir"/*.exsc)
  shopt -u nullglob
  local nfix=${#fixtures[@]}

  if [[ "$nfix" -lt "$fixture_floor" ]]; then
    bad "discovered $nfix *.exsc fixtures in tests/conformance/, floor is $fixture_floor"
    note "a suite that finds fewer fixtures than §14 has entries must not"
    note "report success -- see UNIT_FIXTURE_FLOOR's header, above, for the"
    note "class of bug this floor (and that one) exists to catch"
  else
    note "discovered $nfix *.exsc fixtures (floor $fixture_floor)"
  fi

  # ---- build exsc once, reused for every shape=code status=run fixture ----
  # Degrades exactly like tools/reproduce.sh and the Makefile's EXSC_DEP:
  # compiler/x86_64/exsc.asm not existing is expected and not a failure of
  # THIS suite (it is the driver agent's tree, built concurrently -- see
  # CLAUDE.md's Scope: "Do not edit another agent's tree"); fasmg missing,
  # or exsc.asm existing but failing to assemble, are real failures because
  # they are regressions in something this suite depends on, not an
  # expected absence.
  local workdir; workdir="$(mktemp -d)"
  local exsc_src="$REPO_ROOT/compiler/x86_64/exsc.asm"
  local exsc_bin="$workdir/exsc"
  local exsc_ok=0
  if [[ ! -f "$exsc_src" ]]; then
    note "compiler/x86_64/exsc.asm is absent from this tree -- every shape=code"
    note "fixture is checked against its fixture's own status= instead;"
    note "none can be status=run without it"
  elif ! command -v "$FASMG" >/dev/null 2>&1; then
    bad "fasmg not found on PATH (set FASMG=/path/to/fasmg) -- cannot build exsc"
  else
    if "$FASMG" "$exsc_src" "$exsc_bin" >"$workdir/exsc.asmlog" 2>&1; then
      chmod +x "$exsc_bin"
      exsc_ok=1
      note "built exsc from compiler/x86_64/exsc.asm for this run"
    else
      bad "compiler/x86_64/exsc.asm failed to assemble -- every shape=code"
      bad "status=run fixture below will fail until this is fixed (report"
      bad "to the driver agent -- CLAUDE.md: do not edit another agent's tree)"
      sed 's/^/         /' "$workdir/exsc.asmlog"
    fi
  fi

  local ran=0 deferred=0
  local -A seen_entries=()

  local f
  for f in "${fixtures[@]}"; do
    local name; name="$(basename "$f")"
    local directive; directive="$(grep -m1 '^// TEST:' "$f" || true)"
    directive="${directive#// TEST:}"
    if [[ -z "$directive" ]]; then
      bad "$name: no '// TEST:' directive -- cannot tell which §14 entry or shape this is"
      continue
    fi

    local entry="" shape="" expect_code="" status="" needs=""
    local kv
    for kv in $directive; do
      case "$kv" in
        entry=*) entry="${kv#entry=}" ;;
        shape=*) shape="${kv#shape=}" ;;
        expect-code=*) expect_code="${kv#expect-code=}" ;;
        status=*) status="${kv#status=}" ;;
        needs=*) needs="${kv#needs=}" ;;
      esac
    done

    if [[ -z "$entry" || -z "$shape" || -z "$status" ]]; then
      bad "$name: directive missing entry=/shape=/status= (got: '$directive')"
      continue
    fi
    if [[ ! "$entry" =~ ^[0-9]+$ || "$entry" -lt 1 || "$entry" -gt 24 ]]; then
      bad "$name: entry='$entry' is not a §14 entry number (1-24)"
      continue
    fi
    seen_entries[$entry]=$(( ${seen_entries[$entry]:-0} + 1 ))

    if [[ "$status" == "deferred" ]]; then
      if [[ -z "$needs" ]]; then
        bad "$name: status=deferred but no needs= -- say what it is waiting on"
        continue
      fi
      deferred=$((deferred + 1))
      note "$name: entry $entry DEFERRED (shape=$shape needs=$needs) -- not counted as passing"
      continue
    fi
    if [[ "$status" != "run" ]]; then
      bad "$name: unknown status='$status' (expected run|deferred)"
      continue
    fi

    case "$shape" in
      code)
        if [[ -z "$expect_code" ]]; then
          bad "$name: shape=code but no expect-code="
          continue
        fi
        if [[ "$exsc_ok" -ne 1 ]]; then
          bad "$name: entry $entry claims status=run but exsc is not available this run"
          continue
        fi
        local errlog="$workdir/$name.err"
        local rc=0
        "$exsc_bin" aedifica --hospes x86_64-linux --diagnostica json "$f" \
          >"$workdir/$name.out" 2>"$errlog" || rc=$?
        if [[ "$rc" -ne 1 ]]; then
          bad "$name: entry $entry: exsc exited $rc (expected 1, 'diagnostics were emitted')"
          sed 's/^/         /' "$errlog"
          continue
        fi
        if grep -qF "\"code\":\"$expect_code\"" "$errlog"; then
          ok "$name: entry $entry rejected with exactly $expect_code"
          ran=$((ran + 1))
        else
          bad "$name: entry $entry expected $expect_code, not found in exsc's diagnostics:"
          sed 's/^/         /' "$errlog"
        fi
        ;;
      bytes)
        # Reused, not reimplemented (this agent's brief). No fixture is
        # status=run for this shape yet (16/17 both need a backend this
        # project does not have) -- this branch exists so the day one is,
        # flipping that fixture's status= is the only change needed.
        local rc=0
        "$REPO_ROOT/tools/reproduce.sh" >"$workdir/$name.out" 2>&1 || rc=$?
        if grep -q "THIS RUN TESTS THE FIXTURE, NOT exsc" "$workdir/$name.out"; then
          bad "$name: entry $entry: tools/reproduce.sh degraded to its own"
          bad "toolchain fallback (no exsc output to diff) -- that proves the"
          bad "harness, not entry $entry; do not mark this fixture status=run"
          bad "until exsc actually emits an object to reproduce"
        elif [[ "$rc" -eq 0 ]]; then
          ok "$name: entry $entry: tools/reproduce.sh reports byte-identical output"
          ran=$((ran + 1))
        else
          bad "$name: entry $entry: tools/reproduce.sh reported non-reproducible output"
          sed 's/^/         /' "$workdir/$name.out"
        fi
        ;;
      cert|abort|nocap)
        bad "$name: entry $entry: shape=$shape has no status=run implementation --"
        bad "no fixture should claim status=run for this shape yet (cert needs a"
        bad "backend + wire codec, abort needs an executed binary, nocap needs a"
        bad "capability checker) -- fix the fixture's directive, not this branch"
        ;;
      *)
        bad "$name: unknown shape='$shape'"
        ;;
    esac
  done

  local missing=() dup=()
  local i
  for ((i = 1; i <= 24; i++)); do
    case "${seen_entries[$i]:-0}" in
      0) missing+=("$i") ;;
      1) ;;
      *) dup+=("$i") ;;
    esac
  done
  if [[ ${#missing[@]} -gt 0 ]]; then
    bad "no fixture claims entry=${missing[*]} -- §14 has 24 entries, all must be represented"
  fi
  if [[ ${#dup[@]} -gt 0 ]]; then
    bad "more than one fixture claims entry=${dup[*]} -- each §14 entry should have exactly one"
  fi

  rm -rf "$workdir"

  if [[ "$ran" -lt "$run_floor" ]]; then
    bad "only $ran entries actually ran (checked against a real exsc diagnostic), floor is $run_floor"
    note "a conformance suite that silently ran zero (or too few) entries is"
    note "exactly the fifth false-green this project has already produced"
    note "four times over -- see UNIT_FIXTURE_FLOOR's header, above"
  else
    note "$ran/$nfix entries RAN and passed; $deferred/$nfix entries DEFERRED (not counted as passing)"
  fi
}

# ===========================================================================
# Phases 3 and 4: RUN what a compiler emitted.
#
# Shared by both. A fixture's expectations are key=value tokens (the unit
# directive's style, one section up); these keys mean the same thing in both
# phases:
#
#   expect-exit=N   the program exits normally with status N. Its stderr
#                   must be EMPTY.
#   abort=N         spec 6.6's one abort shape instead: "`abortus N` on
#                   fd 2, then SIGILL". Checked as exactly that -- killed by
#                   signal 4, and a stderr line ending in `abortus N` -- not
#                   as shell status 132, which `redde 132;` also produces
#                   (spec 6.6 chose SIGILL precisely because every u8 is a
#                   legitimate `initium` result). The number is the code;
#                   the English before `abortus` is not promised
#                   (prelude/README.md) and is not matched.
#   stdout=PATH     stdout must be byte-identical to PATH, relative to the
#                   repo root (so a fixture can point at examples/ rather
#                   than copy it). Absent: stdout must be EMPTY.
#
# Exactly one of expect-exit= / abort= is required for anything that runs.
# An UNKNOWN KEY FAILS THE FIXTURE: the unit directive ignores one, and a
# typo there (`expect_exit=3`) silently falls back to the default and
# passes -- a false green of exactly the kind UNIT_FIXTURE_FLOOR's header
# lists. These two phases do not repeat it.
#
# Every binary runs with stdin </dev/null, an EMPTY environment, and a
# 20-second limit (a hang -- a loop the emitter got wrong -- is a failure,
# not a stuck suite). python3 runs it because bash cannot tell a signal from
# an exit status >= 128; python3 is already required by tools/syscall-audit.sh.
# Every binary is then audited with `--potestates Mundus,ambitus`, the
# publish gate's own invocation for the hello world.
# ===========================================================================

# run_binary BIN OUT ERR -- prints "exit N", "signal N" or "timeout".
run_binary() {
  python3 - "$1" "$2" "$3" <<'PY'
import subprocess, sys
exe, out, err = sys.argv[1:4]
with open(out, 'wb') as o, open(err, 'wb') as e:
    try:
        r = subprocess.run([exe], stdin=subprocess.DEVNULL, stdout=o,
                           stderr=e, env={}, timeout=20)
    except subprocess.TimeoutExpired:
        print('timeout')
        sys.exit(0)
if r.returncode < 0:
    print('signal %d' % -r.returncode)
else:
    print('exit %d' % r.returncode)
PY
}

# check_run LABEL BIN WORK EXPECT_EXIT ABORT STDOUT_REF -- runs BIN, checks
# the keys above, audits it. EXPECT_EXIT/ABORT/STDOUT_REF are "" when unset.
# Returns 0 iff every check passed (each check also counts ok/bad itself).
check_run() {
  local label="$1" bin="$2" work="$3" want_exit="$4" want_abort="$5" ref="$6"
  local out="$work.stdout" err="$work.stderr" status rcode=0
  status="$(run_binary "$bin" "$out" "$err")"

  if [[ -n "$want_abort" ]]; then
    if [[ "$status" == "signal 4" ]] &&
       grep -qE "(^|[^[:alnum:]_])abortus $want_abort\$" "$err"; then
      ok "$label: aborts with abortus $want_abort, SIGILL"
    else
      bad "$label: expected abortus $want_abort then SIGILL, got '$status'; stderr:"
      sed 's/^/         /' "$err"; rcode=1
    fi
  else
    if [[ "$status" == "exit $want_exit" ]]; then
      ok "$label: runs, exit=$want_exit"
    else
      bad "$label: got '$status', expected 'exit $want_exit'"
      sed 's/^/         /' "$err"; rcode=1
    fi
    if [[ -s "$err" ]]; then
      bad "$label: wrote to stderr, expected nothing:"
      sed 's/^/         /' "$err"; rcode=1
    fi
  fi

  if [[ -n "$ref" ]]; then
    if cmp -s "$out" "$REPO_ROOT/$ref"; then
      ok "$label: stdout byte-identical to $ref"
    else
      bad "$label: stdout differs from $ref"
      cmp "$out" "$REPO_ROOT/$ref" 2>&1 | sed 's/^/         /' || true
      rcode=1
    fi
  elif [[ -s "$out" ]]; then
    bad "$label: wrote to stdout, expected nothing (no stdout= given)"
    head -c 400 "$out" | sed 's/^/         /'; rcode=1
  fi

  if "$AUDIT" --potestates Mundus,ambitus "$bin" >"$work.auditlog" 2>&1; then
    ok "$label: syscall surface within {Mundus, ambitus}"
  else
    bad "$label: syscall surface exceeds {Mundus, ambitus}"
    sed 's/^/         /' "$work.auditlog"; rcode=1
  fi
  return "$rcode"
}

# parse_run_keys LABEL KV... -- sets k_expect_exit k_abort k_stdout k_emit_exit
# k_exsc_exit k_sources k_status k_needs from the tokens; returns 1 (having
# said why) on an unknown key, a malformed number, or a stdout= that does not
# exist.
parse_run_keys() {
  local label="$1"; shift
  k_expect_exit=""; k_abort=""; k_stdout=""; k_emit_exit="0"; k_exsc_exit="0"
  k_sources=""; k_status="run"; k_needs=""
  local kv
  for kv in "$@"; do
    case "$kv" in
      expect-exit=*) k_expect_exit="${kv#expect-exit=}" ;;
      abort=*)       k_abort="${kv#abort=}" ;;
      stdout=*)      k_stdout="${kv#stdout=}" ;;
      emit-exit=*)   k_emit_exit="${kv#emit-exit=}" ;;
      exsc-exit=*)   k_exsc_exit="${kv#exsc-exit=}" ;;
      sources=*)     k_sources="${kv#sources=}" ;;
      status=*)      k_status="${kv#status=}" ;;
      needs=*)       k_needs="${kv#needs=}" ;;
      *) bad "$label: unknown directive key '$kv'"; return 1 ;;
    esac
  done
  if [[ "$k_status" != "run" && "$k_status" != "deferred" ]]; then
    bad "$label: unknown status='$k_status' (expected run|deferred)"; return 1
  fi
  if [[ "$k_status" == "deferred" && -z "$k_needs" ]]; then
    bad "$label: status=deferred but no needs= -- say what it is waiting on"
    return 1
  fi
  if [[ "$k_status" == "run" && -n "$k_needs" ]]; then
    bad "$label: needs= without status=deferred"; return 1
  fi
  local n
  for n in "$k_expect_exit" "$k_abort" "$k_emit_exit" "$k_exsc_exit"; do
    if [[ -n "$n" && ! "$n" =~ ^[0-9]+$ ]]; then
      bad "$label: '$n' is not a decimal status"; return 1
    fi
  done
  if [[ -n "$k_stdout" && ! -f "$REPO_ROOT/$k_stdout" ]]; then
    bad "$label: stdout=$k_stdout does not exist (paths are repo-root-relative)"
    return 1
  fi
  return 0
}

# want_one_outcome LABEL -- exactly one of expect-exit= / abort=.
want_one_outcome() {
  if [[ -n "$k_expect_exit" && -n "$k_abort" ]] ||
     [[ -z "$k_expect_exit" && -z "$k_abort" ]]; then
    bad "$1: needs exactly one of expect-exit= / abort="
    return 1
  fi
  return 0
}

floor_check() {  # WHAT FOUND FLOOR VARNAME
  if [[ "$2" -lt "$3" ]]; then
    bad "discovered $2 $1, floor is $3"
    note "a harness that finds nothing must not report success -- see"
    note "UNIT_FIXTURE_FLOOR at the top of this file; the floor here is $4"
  else
    note "discovered $2 $1 (floor $3)"
  fi
}

run_ir_tests() {
  # -------------------------------------------------------------------------
  # tests/ir/*.ir -- `; TEST:` on any line (parse.inc reads `;` to EOL as a
  # comment, so the directive is part of the IR text itself), keys above
  # plus:
  #   emit-exit=N   emit_ir's own exit status (default 0). Non-zero means
  #                 the fixture is a REJECTION -- 3 parse error, 4 emitter
  #                 refusal, 5 verifier verdict (emit_ir.asm's header has
  #                 the table) -- and
  #                 nothing is assembled or run, so expect-exit=/abort=/
  #                 stdout= are refused alongside it.
  # -------------------------------------------------------------------------
  echo "== IR run tests (tests/ir/) =="
  if ! command -v "$FASMG" >/dev/null 2>&1; then
    bad "fasmg not found on PATH -- cannot run IR tests"
    return
  fi
  local workdir; workdir="$(mktemp -d)"
  local emit="$workdir/emit_ir"
  if "$FASMG" "$REPO_ROOT/tests/ir/emit_ir.asm" "$emit" >"$workdir/emit_ir.asmlog" 2>&1; then
    chmod +x "$emit"
    ok "emit_ir.asm: assembles"
  else
    bad "emit_ir.asm: assemble failed -- no IR test can run"
    sed 's/^/         /' "$workdir/emit_ir.asmlog"
    rm -rf "$workdir"
    return
  fi
  # The harness is held to the compiler's own closed nine: it is built from
  # the compiler's modules and is what every result below is trusted through.
  if "$AUDIT" "$emit" >"$workdir/emit_ir.auditlog" 2>&1; then
    ok "emit_ir: audit verdict=pass (the compiler's nine syscalls)"
  else
    bad "emit_ir: audit verdict=fail"
    sed 's/^/         /' "$workdir/emit_ir.auditlog"
  fi

  local found=0 src
  shopt -s nullglob
  for src in "$REPO_ROOT"/tests/ir/*.ir; do
    found=$((found + 1))
    local name; name="$(basename "$src" .ir)"
    local w="$workdir/$name"
    echo "-- $name.ir"
    local directive; directive="$(directive_of "$src")"
    if [[ -z "$directive" ]]; then
      bad "$name: no '; TEST:' directive"; continue
    fi
    # shellcheck disable=SC2086
    parse_run_keys "$name" $directive || continue
    if [[ -n "$k_sources" || "$k_exsc_exit" != "0" || "$k_status" != "run" ]]; then
      bad "$name: sources=/exsc-exit=/status= belong to tests/programs/, not an IR fixture"
      continue
    fi

    local erc=0
    "$emit" <"$src" >"$w.asm" 2>"$w.emitlog" || erc=$?
    if [[ "$k_emit_exit" != "0" ]]; then
      if [[ -n "$k_expect_exit$k_abort$k_stdout" ]]; then
        bad "$name: emit-exit=$k_emit_exit is a rejection; nothing runs, so no expect-exit=/abort=/stdout="
        continue
      fi
      if [[ "$erc" == "$k_emit_exit" ]]; then
        ok "$name: emit_ir refuses it, exit=$erc (expected $k_emit_exit)"
        sed 's/^/         /' "$w.emitlog"
      else
        bad "$name: emit_ir exit=$erc, expected $k_emit_exit"
        sed 's/^/         /' "$w.emitlog"
      fi
      continue
    fi
    want_one_outcome "$name" || continue
    if [[ "$erc" -ne 0 ]]; then
      bad "$name: emit_ir exit=$erc"
      sed 's/^/         /' "$w.emitlog"; continue
    fi
    if "$FASMG" "$w.asm" "$w.bin" >"$w.asmlog" 2>&1; then
      chmod +x "$w.bin"
      ok "$name: emitted program assembles"
    else
      bad "$name: fasmg cannot assemble the emitted program"
      sed 's/^/         /' "$w.asmlog"; continue
    fi
    check_run "$name" "$w.bin" "$w" "$k_expect_exit" "$k_abort" "$k_stdout" || true
  done
  shopt -u nullglob
  rm -rf "$workdir"
  floor_check "IR fixtures in tests/ir/" "$found" "$IR_FIXTURE_FLOOR" IR_FIXTURE_FLOOR
}

run_program_tests() {
  # -------------------------------------------------------------------------
  # tests/programs/<name>/ -- one directory per program. Its expectations
  # are in <name>/TEST, the first line beginning `TEST:` (`#` lines are
  # comments), keys above plus:
  #   sources=A,B   the compilation unit, repo-root-relative, in this order
  #                 (spec 12: the files form ONE unit). Used to point at
  #                 sources that live elsewhere -- examples/ -- rather than
  #                 copying them. Absent: the directory's own *.exsc, in
  #                 byte order (LC_ALL=C, never the locale's collation).
  #                 Both at once is refused as ambiguous.
  #   exsc-exit=N   exsc's exit status (default 0), as the shell reports it
  #                 (132 = an `rassert` in the compiler). Non-zero: nothing
  #                 is assembled or run.
  #   status=deferred needs=A,B
  #                 the program is WRITTEN AHEAD of the machinery that can
  #                 run it (the conformance suite's own word and rule): it is
  #                 compiled WITHOUT -o -- lexed, parsed, type-checked -- and
  #                 that must exit 0, which is a real check and is counted as
  #                 one; nothing is lowered to a file, assembled or run, the
  #                 directory is reported DEFERRED with what it waits on, and
  #                 it is NEVER counted toward PROGRAM_FIXTURE_FLOOR or as a
  #                 program that ran. Its expect-exit=/abort=/stdout= stay in
  #                 the directive, so un-deferring is deleting two keys.
  #                 needs= is required; status=run is the default.
  # A file <name>/expected.out is the stdout reference when present (as if
  # stdout=tests/programs/<name>/expected.out); stdout= and expected.out
  # together are refused as ambiguous.
  #
  # Compiled exactly as tools/publish-gate.sh compiles the hello world:
  # `exsc aedifica --hospes x86_64-linux SRC... -o OUT`, then `fasmg OUT
  # BIN` with the vendored INCLUDE.
  # -------------------------------------------------------------------------
  echo "== program run tests (tests/programs/) =="
  if ! command -v "$FASMG" >/dev/null 2>&1; then
    bad "fasmg not found on PATH -- cannot run program tests"
    return
  fi
  local workdir; workdir="$(mktemp -d)"
  local exsc="$workdir/exsc"
  # Built here rather than taken from build/: the Nix check sandbox has no
  # build/, and a stale build/exsc would test yesterday's compiler.
  if "$FASMG" "$REPO_ROOT/compiler/x86_64/exsc.asm" "$exsc" >"$workdir/exsc.asmlog" 2>&1; then
    chmod +x "$exsc"
    note "built exsc from compiler/x86_64/exsc.asm for this phase"
  else
    bad "compiler/x86_64/exsc.asm failed to assemble -- no program test can run"
    sed 's/^/         /' "$workdir/exsc.asmlog"
    rm -rf "$workdir"
    return
  fi

  local found=0 deferred=0 dir
  shopt -s nullglob
  for dir in "$REPO_ROOT"/tests/programs/*/; do
    dir="${dir%/}"
    local name; name="$(basename "$dir")"
    local w="$workdir/$name"
    echo "-- $name/"
    if [[ ! -f "$dir/TEST" ]]; then
      bad "$name: no TEST file"; continue
    fi
    local directive; directive="$(grep -m1 '^TEST:' "$dir/TEST" || true)"
    directive="${directive#TEST:}"
    if [[ -z "$directive" ]]; then
      bad "$name: TEST has no 'TEST:' line"; continue
    fi
    # shellcheck disable=SC2086
    parse_run_keys "$name" $directive || continue
    if [[ "$k_emit_exit" != "0" ]]; then
      bad "$name: emit-exit= belongs to tests/ir/, not a program"; continue
    fi

    local srcs=() s
    local own; own="$(printf '%s\n' "$dir"/*.exsc | LC_ALL=C sort)"
    if [[ -n "$k_sources" ]]; then
      if [[ -n "$own" ]]; then
        bad "$name: has its own *.exsc AND sources= -- which is the unit?"; continue
      fi
      local rel=()
      IFS=',' read -r -a rel <<<"$k_sources"
      for s in "${rel[@]}"; do srcs+=("$REPO_ROOT/$s"); done
    elif [[ -n "$own" ]]; then
      while IFS= read -r s; do srcs+=("$s"); done <<<"$own"
    else
      bad "$name: no *.exsc and no sources="; continue
    fi
    local missing=0
    for s in "${srcs[@]}"; do
      [[ -f "$s" ]] || { bad "$name: source $s does not exist"; missing=1; }
    done
    [[ "$missing" -eq 0 ]] || continue

    local ref="$k_stdout"
    if [[ -f "$dir/expected.out" ]]; then
      if [[ -n "$ref" ]]; then
        bad "$name: both expected.out and stdout= -- which is the reference?"; continue
      fi
      ref="tests/programs/$name/expected.out"
    fi

    if [[ "$k_status" == "deferred" ]]; then
      if [[ "$k_exsc_exit" != "0" ]]; then
        bad "$name: status=deferred is a program written ahead of its backend; exsc-exit= is a refusal"
        continue
      fi
      want_one_outcome "$name" || continue
      deferred=$((deferred + 1))
      local drc=0
      "$exsc" aedifica --hospes x86_64-linux "${srcs[@]}" \
        >"$w.exsclog" 2>&1 || drc=$?
      if [[ "$drc" -eq 0 ]]; then
        ok "$name: type-checks clean (exsc aedifica, no -o)"
      else
        bad "$name: exsc aedifica (no -o) exit=$drc -- a deferred program must still check"
        sed 's/^/         /' "$w.exsclog"
      fi
      note "$name: DEFERRED (needs=$k_needs) -- not assembled, not run, not counted as passing"
      continue
    fi
    found=$((found + 1))

    local crc=0
    "$exsc" aedifica --hospes x86_64-linux "${srcs[@]}" -o "$w.asm" \
      >"$w.exsclog" 2>&1 || crc=$?
    if [[ "$k_exsc_exit" != "0" ]]; then
      if [[ -n "$k_expect_exit$k_abort$ref" ]]; then
        bad "$name: exsc-exit=$k_exsc_exit means nothing runs, so no expect-exit=/abort=/stdout="
        continue
      fi
      if [[ "$crc" == "$k_exsc_exit" ]]; then
        ok "$name: exsc refuses it, exit=$crc (expected $k_exsc_exit)"
      else
        bad "$name: exsc exit=$crc, expected $k_exsc_exit"
        sed 's/^/         /' "$w.exsclog"
      fi
      continue
    fi
    want_one_outcome "$name" || continue
    if [[ "$crc" -ne 0 ]]; then
      bad "$name: exsc exit=$crc"
      sed 's/^/         /' "$w.exsclog"; continue
    fi
    if "$FASMG" "$w.asm" "$w.bin" >"$w.asmlog" 2>&1; then
      chmod +x "$w.bin"
      ok "$name: compiles and assembles (${#srcs[@]} source(s))"
    else
      bad "$name: fasmg cannot assemble exsc's output"
      sed 's/^/         /' "$w.asmlog"; continue
    fi
    check_run "$name" "$w.bin" "$w" "$k_expect_exit" "$k_abort" "$ref" || true
  done
  shopt -u nullglob
  rm -rf "$workdir"
  floor_check "program directories in tests/programs/ that run" "$found" \
    "$PROGRAM_FIXTURE_FLOOR" PROGRAM_FIXTURE_FLOOR
  note "$deferred program directories DEFERRED (type-checked only; not counted as passing)"
}

run_unit_tests
echo
run_conformance_tests
echo
run_ir_tests
echo
run_program_tests
echo
echo "== summary =="
echo "pass: $PASS  fail: $FAIL"
if [[ "$FAIL" -gt 0 ]]; then
  echo "RESULT: FAIL"
  exit 1
fi
echo "RESULT: PASS"
exit 0
