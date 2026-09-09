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
UNIT_FIXTURE_FLOOR="${UNIT_FIXTURE_FLOOR:-37}"

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
  echo "== conformance suite (tests/conformance/, spec §14) =="
  local dir="$REPO_ROOT/tests/conformance"
  local n=0
  if [[ -d "$dir" ]]; then
    n="$(find "$dir" -mindepth 1 -maxdepth 1 | wc -l)"
  fi
  if [[ "$n" -eq 0 ]]; then
    note "0 entries -- expected: compiler/x86_64/exsc.asm does not exist yet."
    note "§14 lists 23 required entries (20 diagnostic-code cases, plus the"
    note "reproducibility and cross-compilation cases); none can run without"
    note "a compiler to drive. This phase is intentionally a no-op, not a"
    note "failure, until that changes."
    note "Extend THIS function when it does -- not run_unit_tests above."
    note "Most entries need a rule of the shape 'exsc rejects this source"
    note "with exactly code EXS-Exxxx'; entries 16 and 17 instead need"
    note "byte-identical output across conditions and hosts, which is exactly"
    note "what tools/reproduce.sh already knows how to check."
  else
    bad "tests/conformance/ has $n entries but run_conformance_tests() does"
    bad "not yet know how to run them -- implement this phase before trusting"
    bad "a green run.sh to mean the conformance suite passed"
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
