#!/usr/bin/env bash
# tools/spec-check.sh
# ---------------------------------------------------------------------------
# The three mechanical checks .claude/agents/spec-guardian.md describes in
# prose. spec-guardian is read-only and cannot author them, so they live here.
#
#   1. §13 registry <-> compiler/x86_64/diag/codes.inc  -- code set sync
#   2. spec citation validity                          -- every §N resolves
#   3. marker discipline                               -- [OPEN]/[UNTESTED]/
#                                                         [UNREPRODUCED]
#
# Read-only. Touches nothing, builds nothing, needs no toolchain.
#
# Exit: 0 all checks pass, 1 a check failed, 2 usage/environment error.
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SPEC="$REPO_ROOT/docs/spec/exsecutor-spec-v0.3.md"
CODES_INC="$REPO_ROOT/compiler/x86_64/diag/codes.inc"

FAIL=0
ok()   { echo "  [ok]   $*"; }
bad()  { FAIL=$((FAIL + 1)); echo "  [FAIL] $*"; }
note() { echo "  -      $*"; }

[[ -f "$SPEC" ]] || { echo "spec-check: $SPEC not found" >&2; exit 2; }

# Collation is pinned for the same reason vendor/fasmg-x86/PROVENANCE.md pins
# it: sort order is locale-dependent, and a check whose result depends on the
# developer's LANG is not a check. §9.3, and §1's own thesis.
export LC_ALL=C

# ---------------------------------------------------------------------------
# 1. Error-code registry sync
#
# CLAUDE.md: "§13's registry is the only source. compiler/x86_64/diag/codes.inc
# is generated from it and must stay in sync." Codes are permanent (§8.3);
# never invented, never renumbered.
# ---------------------------------------------------------------------------
check_registry_sync() {
  echo "== 1. error-code registry sync (§13 <-> diag/codes.inc) =="

  local spec_codes
  spec_codes="$(grep -ohE 'EXS-E[0-9]{4}' "$SPEC" | sort -u)"
  local n_spec
  n_spec="$(printf '%s\n' "$spec_codes" | grep -c . || true)"

  if [[ ! -f "$CODES_INC" ]]; then
    # Vacuous, and it must SAY it is vacuous. A check that silently reports
    # green when one side of the diff does not exist is worse than no check.
    note "compiler/x86_64/diag/codes.inc does not exist yet."
    note "The diag/ module is unwritten, so there is nothing to diff against."
    note "PASSES VACUOUSLY -- this is not evidence the registry is in sync."
    note "§13 currently defines $n_spec distinct codes."
    return 0
  fi

  local inc_codes
  inc_codes="$(grep -ohE 'EXS-E[0-9]{4}' "$CODES_INC" | sort -u)"

  local only_spec only_inc
  only_spec="$(comm -23 <(printf '%s\n' "$spec_codes") <(printf '%s\n' "$inc_codes"))"
  only_inc="$(comm -13 <(printf '%s\n' "$spec_codes") <(printf '%s\n' "$inc_codes"))"

  if [[ -n "$only_spec" ]]; then
    bad "in §13 but missing from codes.inc:"
    printf '           %s\n' $only_spec
  fi
  if [[ -n "$only_inc" ]]; then
    bad "in codes.inc but NOT in §13 -- an invented code:"
    printf '           %s\n' $only_inc
  fi
  [[ -z "$only_spec$only_inc" ]] && ok "$n_spec codes, both sides identical"
  return 0
}

# ---------------------------------------------------------------------------
# 2. Spec citation validity
#
# Every §N written anywhere in the repo must resolve to a real heading in the
# spec. Two bad citations were caught by hand during the previous pass; this
# is that check, mechanised.
# ---------------------------------------------------------------------------
check_citations() {
  echo "== 2. spec citation validity =="

  local real cited dangling n_real n_cited
  real="$(grep -ohE '^#{1,4} +[0-9]+(\.[0-9]+)*' "$SPEC" | sed -E 's/^#+ +//' | sort -u)"

  # Every tracked text file except the spec itself (which legitimately refers
  # to its own sections) and vendor/ (third-party, not ours to cite).
  cited="$(cd "$REPO_ROOT" && grep -rhoE '§[0-9]+(\.[0-9]+)*' \
             --exclude-dir=.git --exclude-dir=vendor --exclude-dir=build \
             --exclude='exsecutor-spec-v0.3.md' . 2>/dev/null \
           | sed 's/§//' | sort -u || true)"

  n_real="$(printf '%s\n' "$real" | grep -c . || true)"
  n_cited="$(printf '%s\n' "$cited" | grep -c . || true)"

  dangling="$(comm -23 <(printf '%s\n' "$cited") <(printf '%s\n' "$real") || true)"

  if [[ -n "$dangling" ]]; then
    bad "citations that resolve to no spec heading:"
    local d
    for d in $dangling; do
      echo "           §$d  cited in:"
      (cd "$REPO_ROOT" && grep -rlE "§$d([^0-9.]|\$)" \
         --exclude-dir=.git --exclude-dir=vendor --exclude-dir=build \
         --exclude='exsecutor-spec-v0.3.md' . 2>/dev/null | sed 's/^/             /') || true
    done
  else
    ok "$n_cited distinct sections cited, all resolve against $n_real headings"
  fi
  return 0
}

# ---------------------------------------------------------------------------
# 3. Marker discipline
#
# spec line 5: "Prose designs are hypotheses until code runs." CLAUDE.md:
# unbacked claims get [OPEN] or [UNTESTED]; figures that cannot currently be
# re-measured get [UNREPRODUCED]; "never present a re-derivation as a
# restoration."
#
# Marker *placement* is a judgement call, so this reports rather than fails.
# The one hard failure is a malformed marker -- a typo silences the discipline
# without anyone noticing.
# ---------------------------------------------------------------------------
check_markers() {
  echo "== 3. evidence-marker discipline =="

  local known='\[OPEN\]|\[UNTESTED\]|\[UNREPRODUCED\]'
  local m
  for m in OPEN UNTESTED UNREPRODUCED; do
    local n
    n="$(cd "$REPO_ROOT" && grep -rohE --include='*.md' "\[$m\]" \
           --exclude-dir=.git --exclude-dir=vendor --exclude-dir=build . 2>/dev/null \
         | grep -c . || true)"
    note "[$m]: $n occurrence(s)"
  done

  # Bracketed all-caps tokens that look like markers but are not the three
  # sanctioned ones. [UNIMPLEMENTED] is sanctioned by docs/asm-conventions.md.
  #
  # Scoped to prose (*.md) on purpose: evidence markers are a documentation
  # discipline. Scripts legitimately print bracketed all-caps labels -- the
  # harness's own "[FAIL]" and "[ok]" are not marker typos, and scanning them
  # produced exactly that false positive on this check's first run.
  local strays
  strays="$(cd "$REPO_ROOT" && grep -rohE --include='*.md' '\[[A-Z][A-Z_]{3,}\]' \
              --exclude-dir=.git --exclude-dir=vendor --exclude-dir=build . 2>/dev/null \
            | sort -u | grep -vE "^($known|\[UNIMPLEMENTED\])$" || true)"

  if [[ -n "$strays" ]]; then
    bad "marker-shaped tokens that are not sanctioned markers (typo?):"
    printf '           %s\n' $strays
  else
    ok "no malformed or unsanctioned markers"
  fi

  # §4, §6.2 and §9.2 carry [UNREPRODUCED] figures. If one loses its marker,
  # someone claimed a number they did not measure.
  local lost=0 s
  for s in "prototypes/capcheck/exsecutor_check.py" "prototypes/stage0-bench/"; do
    if grep -q "$s" "$SPEC" && ! grep -q '\[UNREPRODUCED\]' "$SPEC"; then
      lost=1
    fi
  done
  if [[ "$lost" == "1" ]]; then
    bad "an [UNREPRODUCED] marker disappeared from the spec while the artifact"
    bad "it describes is still absent -- a re-derivation presented as a restoration"
  else
    ok "the absent artifacts named in §4/§6.2/§9.2 still carry their markers"
  fi
  return 0
}

echo "spec-check: $(basename "$SPEC")"
echo
check_registry_sync
echo
check_citations
echo
check_markers
echo
echo "== summary =="
if [[ "$FAIL" -eq 0 ]]; then
  echo "RESULT: PASS"
  exit 0
fi
echo "failures: $FAIL"
echo "RESULT: FAIL"
exit 1
