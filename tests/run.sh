#!/usr/bin/env bash
# tests/run.sh -- minimal, real test harness.
#
# Two phases:
#   1. run_unit_tests: discovers tests/unit/*.asm, assembles each with
#      fasmg, and checks the expectations declared in its `; TEST:`
#      directive comment (run=yes|no, expect-exit=<N>, audit=pass|fail|skip).
#      This is the only phase that can do anything today -- there is no
#      compiler yet, so the only thing under test is the toolchain itself
#      and tools/syscall-audit.sh.
#   2. run_conformance_tests: tests/conformance/ (spec §14, 23 entries) is
#      a deliberate no-op until compiler/x86_64/exsc.asm exists to drive
#      it. Kept as a separate function/phase precisely so wiring it up
#      later does not require touching run_unit_tests.
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
UNIT_FIXTURE_FLOOR="${UNIT_FIXTURE_FLOOR:-64}"

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
  # spec §14, 23 entries, FIVE rule shapes (tests/README.md, "23 entries,
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
    note "compiler/x86_64/exsc.asm does not exist yet -- every shape=code"
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
  for ((i = 1; i <= 23; i++)); do
    case "${seen_entries[$i]:-0}" in
      0) missing+=("$i") ;;
      1) ;;
      *) dup+=("$i") ;;
    esac
  done
  if [[ ${#missing[@]} -gt 0 ]]; then
    bad "no fixture claims entry=${missing[*]} -- §14 has 23 entries, all must be represented"
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

run_unit_tests
echo
run_conformance_tests
echo
echo "== summary =="
echo "pass: $PASS  fail: $FAIL"
if [[ "$FAIL" -gt 0 ]]; then
  echo "RESULT: FAIL"
  exit 1
fi
echo "RESULT: PASS"
exit 0
