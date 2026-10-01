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
#   4. §8.4 keywords <-> compiler/x86_64/lexer/keywords.inc -- keyword sync
#   5. artifact-citation validity                      -- every cited PATH
#                                                         exists
#   6. §8.4 operators/sigils <-> compiler/x86_64/lexer/token.inc PUN_* --
#                                                         punctuation sync
#   7. §3.3-§3.5 <-> compiler/x86_64/checker/lexicon/morphemes.inc --
#                                                         lexicon regeneration
#
# Checks 1, 4 and 6 are the same arrangement: a spec table is the normative
# source, the .inc is generated from it, and drift is a build failure rather
# than something a reader is expected to notice. Check 7 is one step further
# down the same road: instead of diffing two extracted SETS, it runs the
# actual generator (tools/gen-lexicon.py) and diffs its output against what
# is committed -- the arrangement gen-lexicon.py's own header already
# described as what a human or CI should do, unenforced until now.
#
# Check 5 was added on 2026-09-26, and it was added because the thing it
# checks had already gone wrong: an amendment declared integer division,
# `residuum` and the bitwise words settled and cited three program
# directories and a unit fixture as the evidence, and none of the four
# existed in the tree. Check 2 passed it -- every SECTION reference resolved.
# CLAUDE.md's evidence discipline ("never report a benchmark you did not run,
# or a test you did not see pass") had no mechanical half, so a false
# evidence citation was exactly as cheap to write as a true one. This is the
# mechanical half, and it stands in the same relation to the evidence rule
# that `make audit` stands in to the socket prohibition: the contract becomes
# checkable rather than promised (§9.3).
#
# Checks 6 and 7 were added together, for the same reason check 5 was: the
# drift they gate had already happened with nothing here to catch it. Check 7
# is the clear case -- `gen-lexicon.py`'s regeneration had been reproducing
# stale header prose (three sentences describing the lexicon pass as unwritten
# and disabled, false by the time they were read) because nothing ever ran it
# and diffed the result.
#
# CHECK 6 DOES NOT CATCH THE TOKEN THAT PROMPTED IT, and saying so here is the
# point. `PUN_TICK` was added on 2026-09-30 declared **tier B** (`; B ' §5.1`),
# and tier B is attestation rather than enumeration (token.inc's header), so a
# tier-B token legitimately has no §8.4 row and gating it would falsely fail
# `-`, tier B on §10.1's `x86_64-linux`. Check 6 closes the tier-A half, where
# the token's own comment claims §8.4 is normative for it; the tier-B half
# stays ungated. What would have caught `PUN_TICK` is a CST-kind-to-§8.6
# grammar check -- it arrived with a whole `CST_BRAND_TYPE` production and no
# §8.6 amendment -- and that check does not exist yet. [OPEN]
#
# Read-only except check 7, which runs `tools/gen-lexicon.py` (python3) to a
# TEMP path and diffs -- it touches nothing in the tree. Checks 1-6 touch
# nothing and need no toolchain; check 7 needs python3, already a devShell
# dependency (verification-only, as gen-codes.py and gen-keywords.py are).
#
# Exit: 0 all checks pass, 1 a check failed, 2 usage/environment error.
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SPEC="$REPO_ROOT/docs/spec/exsecutor-spec-v0.4.md"
CODES_INC="$REPO_ROOT/compiler/x86_64/diag/codes.inc"
KEYWORDS_INC="$REPO_ROOT/compiler/x86_64/lexer/keywords.inc"
TOKEN_INC="$REPO_ROOT/compiler/x86_64/lexer/token.inc"
MORPHEMES_INC="$REPO_ROOT/compiler/x86_64/checker/lexicon/morphemes.inc"
GEN_LEXICON="$REPO_ROOT/tools/gen-lexicon.py"

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
             --exclude='exsecutor-spec-v0.4.md' . 2>/dev/null \
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
         --exclude='exsecutor-spec-v0.4.md' . 2>/dev/null | sed 's/^/             /') || true
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
# ---------------------------------------------------------------------------
# 4. Keyword table sync
#
# §8.4 is the normative keyword source; lexer/keywords.inc is generated from
# it. Same rule as §13/codes.inc: never invent a keyword, never let the two
# drift. Reserving a word also spends root-space permanently (§3.9.3 requires
# every proposed root to be collision-checked against the reserved set), so an
# entry appearing in code but not in the spec is a real cost taken silently.
# ---------------------------------------------------------------------------
spec_keywords() {
  # The reserved-word table in §8.4: rows between the "1. Reserved words"
  # marker and the count sentence. Only backticked cells in the second column.
  #
  # THE TERMINATOR IS COUNT-AGNOSTIC on purpose. It used to be the literal
  # "Thirty words", which meant the first word ever added to the table broke
  # this check in a way that looked like a sync failure: the scan ran past the
  # table into the contextual list and reported words code never claimed. It
  # fired exactly once, on `radix_quadrata` (thirty-one). The terminator is
  # guarded by `f` because an unguarded one matches prose ABOVE the table and
  # exits before the scan ever starts -- which extracts nothing and reads as
  # "code has 31 words the spec does not".
  awk '/^\*\*1\. Reserved words\*\*/{f=1} f&&/^[A-Za-z-]+ words/{exit} f&&/^\| [a-z]/{print}' "$SPEC" \
    | sed 's/^|[^|]*|//; s/|$//' \
    | grep -o '`[^`]*`' | tr -d '`' | sort -u
}

check_keyword_sync() {
  echo "== 4. keyword table sync (§8.4 <-> lexer/keywords.inc) =="

  local spec_kw n_spec
  spec_kw="$(spec_keywords)"
  n_spec="$(printf '%s\n' "$spec_kw" | grep -c . || true)"

  if [[ "$n_spec" -eq 0 ]]; then
    bad "§8.4's reserved-word table parsed to zero keywords"
    note "the table shape changed, or this extractor is wrong -- either way"
    note "this check is not measuring what it claims"
    return 0
  fi

  local dupes
  dupes="$(spec_keywords_raw_dupes)"
  if [[ -n "$dupes" ]]; then
    bad "a word appears in more than one §8.4 group:"
    printf '           %s\n' $dupes
  fi

  if [[ ! -f "$KEYWORDS_INC" ]]; then
    note "compiler/x86_64/lexer/keywords.inc does not exist yet."
    note "The lexer/ module is unwritten, so there is nothing to diff against."
    note "PASSES VACUOUSLY -- this is not evidence the table is in sync."
    note "§8.4 currently reserves $n_spec words."
    return 0
  fi

  local inc_kw only_spec only_inc
  inc_kw="$(grep -ohE '^[[:space:]]*kw[[:space:]]+[a-z_]+' "$KEYWORDS_INC" \
            | awk '{print $2}' | sort -u)"
  only_spec="$(comm -23 <(printf '%s\n' "$spec_kw") <(printf '%s\n' "$inc_kw"))"
  only_inc="$(comm -13 <(printf '%s\n' "$spec_kw") <(printf '%s\n' "$inc_kw"))"

  if [[ -n "$only_spec" ]]; then
    bad "in §8.4 but missing from keywords.inc:"
    printf '           %s\n' $only_spec
  fi
  if [[ -n "$only_inc" ]]; then
    bad "in keywords.inc but NOT in §8.4 -- an invented keyword:"
    printf '           %s\n' $only_inc
  fi
  [[ -z "$only_spec$only_inc" ]] && ok "$n_spec keywords, both sides identical"
  return 0
}

# Words listed under two different §8.4 groups. sort -u hides this, so the
# duplicate scan reads the unsorted extraction.
spec_keywords_raw_dupes() {
  awk '/^\*\*1\. Reserved words\*\*/{f=1} f&&/^[A-Za-z-]+ words/{exit} f&&/^\| [a-z]/{print}' "$SPEC" \
    | sed 's/^|[^|]*|//; s/|$//' \
    | grep -o '`[^`]*`' | tr -d '`' | sort | uniq -d
}

# ---------------------------------------------------------------------------
# 5. Artifact-citation validity
#
# The spec cites evidence by path -- a fixture, a program directory, an
# oracle, a compiler module -- and CLAUDE.md makes an unbacked claim a
# marked one. A citation to a path that is not there is an unbacked claim
# wearing evidence's clothes, and unlike marker PLACEMENT (check 3) it is
# not a judgement call: the file is present or it is not. So this FAILS.
#
# Extraction: backticked runs whose first segment is a real top-level
# directory of the tree. The directory list is read from git rather than
# hardcoded, so adding one does not silently narrow the check. A `*` makes
# the citation a glob and at least one match is required. Placeholder
# spellings (`tests/programs/<name>/`) cannot match the token pattern and
# are not scanned, which is right -- they cite a shape, not an artifact.
#
# One convention this imposes on the prose: backticks are what make a path a
# citation, so a RETIRED path -- one a sentence names in order to record that
# it is gone -- is written without them. §3's and §5.3's amendments of
# 2026-09-26 do exactly that, and say why.
#
# Deliberately-absent artifacts go in ABSENT_BY_DESIGN below, each with the
# reason. It is an allowlist and not a filter: adding a line to it is a
# recorded decision that the spec is claiming something it cannot show.
#
# Scope is the spec, which is this script's subject. The same scan over
# docs/design/ and docs/decisions/ is reported as a note, not a failure, and
# that split is a measurement rather than a convenience. All twelve absent
# paths those documents named on 2026-09-26 were triaged, and not one was a
# false evidence claim -- they were three things, and the third is now empty:
#
#   - a HYPOTHETICAL tree. `compiler/aarch64/` in ADRs 0002 and 0003 (and in
#     asm-conventions.md, which this scan does not reach), each time as "a
#     future" or "the eventual" one.
#   - ANOTHER PROJECT'S path. HydraModem's tests/test_loopback.c (ADR 0014)
#     and Kiln's examples/exsec-streamdb-demo/ (ADR 0015, c-backend.md) are
#     repo-relative in THEIR trees, not this one.
#   - A PLAN'S OWN SPELLING, recorded beside what landed instead -- CLOSED on
#     2026-09-27, which is why the note below now reports five and not
#     twelve. Five citations were renamed to what landed
#     (`tests/unit/lwr_ssa.asm`, `tests/unit/lwr_saluta.asm`,
#     `tests/unit/driver_emit.asm`, `receptio_caput/`,
#     `receptio_circuitus/`), each with the plan's spelling kept on the line
#     as history, and two owed fixtures -- c-backend.md's
#     tests/unit/bfc_emit_float.asm and receptor.md's
#     tests/programs/hydramodem_rx_plenus/ -- lost their backticks, which is
#     the convention below applied rather than argued about.
#
# So the docs' prose is already honest where the spec's was not, and what the
# note still reports is five citations of three distinct paths, every one of
# them naming a tree that is not this one. Promoting this half to a failure
# means editing four historical ADRs and one design document to satisfy a lint
# whose finding is that they are correct, which is the wrong way round. It
# reports, the list stays visible, and a document that starts claiming
# evidence it does not have is one grep from being seen.
# ---------------------------------------------------------------------------
ABSENT_BY_DESIGN=(
  # (empty) -- every path the spec cites is in the tree. Keep it that way:
  # a new entry here is a claim the spec cannot back, and it needs the
  # marker discipline of check 3 in the prose as well as a line here.
)

# Backticked path-shaped citations in one file, one per line.
cited_paths() {
  local file="$1" tops
  # Top-level directories that actually exist, as an alternation.
  tops="$(cd "$REPO_ROOT" && git ls-files 2>/dev/null \
          | awk -F/ 'NF>1{print $1}' | sort -u | paste -sd'|' -)"
  [[ -n "$tops" ]] || return 0
  # `|| true`: a file with no path citations is not an error, and under
  # `set -o pipefail` a grep that matches nothing would abort the caller's
  # assignment. melos.md found that on this check's first run.
  grep -ohE '`[A-Za-z0-9_.@/*-]+`' "$file" | tr -d '`' \
    | grep -E "^($tops)/" | sort -u || true
}

# Echoes each path in the list that resolves to nothing.
dangling_paths() {
  local p
  while read -r p; do
    [[ -n "$p" ]] || continue
    case "$p" in
      # Glob citation: at least one match required. Expanded with `nullglob`
      # in a subshell rather than `compgen -G`, which this line used until
      # 2026-09-26 and which is NOT dependably present in a non-interactive
      # shell -- run straight from a terminal it worked, run under `make` it
      # printed "compgen: command not found" five times and reported every
      # glob in the spec as absent. A check whose verdict depends on how the
      # shell was started is not a check; this file already pins LC_ALL for
      # the same reason, one environment variable over.
      *'*'*) ( cd "$REPO_ROOT" && shopt -s nullglob && set -- $p \
                 && [ "$#" -gt 0 ] ) || echo "$p" ;;
      # A cited DIRECTORY must hold something git tracks. An empty directory
      # satisfied this check on its first day -- `tests/programs/basis64/` was
      # cited by §5.4 as "a Base64 encoder held byte-identical to coreutils'"
      # while being an empty directory, and existence alone passed it. A
      # citation names evidence, and an empty directory is not evidence; an
      # untracked one is not evidence either, since it would not survive a
      # clone. Found by a whole-tree audit on 2026-09-26, which is the sort of
      # gap a mechanical check has and a reader does not.
      */)    if [[ ! -d "$REPO_ROOT/$p" ]]; then
               echo "$p"
             elif [[ -z "$(cd "$REPO_ROOT" && git ls-files -- "$p" 2>/dev/null | head -1)" ]]; then
               # Present but holding nothing git tracks. Two different
               # situations with two different fixes, so they are reported
               # apart: EMPTY needs the evidence written, UNTRACKED needs
               # `git add` and is one command from correct.
               if [[ -z "$(ls -A "$REPO_ROOT/$p" 2>/dev/null)" ]]; then
                 echo "$p	EMPTY"
               else
                 echo "$p	UNTRACKED"
               fi
             fi ;;
      *)     [[ -e "$REPO_ROOT/$p" ]] || echo "$p" ;;
    esac
  done
}

check_artifact_citations() {
  echo "== 5. artifact-citation validity =="

  local all n_all dangling
  all="$(cited_paths "$SPEC")"
  n_all="$(printf '%s\n' "$all" | grep -c . || true)"
  if [[ "$n_all" -eq 0 ]]; then
    bad "the spec parsed to zero path citations -- this extractor is wrong,"
    bad "and a check measuring nothing reads the same as a check passing"
    return 0
  fi

  dangling="$(printf '%s\n' "$all" | dangling_paths)"

  # Subtract the allowlist, and say so for each one taken out.
  local a p kept=""
  while read -r p; do
    [[ -n "$p" ]] || continue
    local excused=0 bare="${p%%$'\t'*}"
    for a in ${ABSENT_BY_DESIGN[@]+"${ABSENT_BY_DESIGN[@]}"}; do
      [[ "$bare" == "$a" ]] && excused=1
    done
    if [[ "$excused" == "1" ]]; then
      note "$p: absent by design (ABSENT_BY_DESIGN)"
    else
      kept+="$p"$'\n'
    fi
  done <<< "$dangling"

  if [[ -n "${kept//[$'\n']/}" ]]; then
    bad "the spec cites artifacts it cannot show:"
    while read -r p; do
      [[ -n "$p" ]] || continue
      local why="${p##*$'\t'}" path="${p%%$'\t'*}"
      case "$why" in
        EMPTY)     echo "           $path  EXISTS BUT IS EMPTY -- cited as evidence, holds none" ;;
        UNTRACKED) echo "           $path  UNTRACKED -- would not survive a clone; git add it" ;;
        *)         path="$p"; echo "           $path  not in the tree" ;;
      esac
      echo "             cited at:"
      grep -nF "$path" "$SPEC" | cut -d: -f1 \
        | sed "s|^|               $(basename "$SPEC"):|"
    done <<< "$kept"
  else
    ok "$n_all distinct paths cited by the spec, all present"
  fi

  # The same scan over the design documents and ADRs. A note: these are not
  # the spec, and their drift is a separate change.
  local other f od
  other=0
  for f in "$REPO_ROOT"/docs/design/*.md "$REPO_ROOT"/docs/decisions/*.md; do
    [[ -f "$f" ]] || continue
    od="$(cited_paths "$f" | dangling_paths)"
    while read -r p; do
      [[ -n "$p" ]] || continue
      other=$((other + 1))
      note "docs/${f#"$REPO_ROOT"/docs/}: $p (not in the tree)"
    done <<< "$od"
  done
  if [[ "$other" -eq 0 ]]; then
    note "docs/design/ and docs/decisions/ cite no absent paths either"
  else
    note "$other absent-path citation(s) outside the spec -- reported, not failed"
  fi
  return 0
}

# ---------------------------------------------------------------------------
# 6. Punctuation table sync
#
# §8.4's "Operators and sigils" table is PUN_TICK's gap: the table was never
# checked against lexer/token.inc's PUN_* block the way keywords.inc is
# checked against §8.4's reserved-word table (check 4), so a new punctuation
# token could land -- and did -- with the spec untouched and nothing here to
# say so.
#
# THE TABLE IS NOT A FLAT LIST, so the extractor has to do more than check
# 4's. Some rows name a GROUP of tokens in one cell (`+` `+%` `+\|`; `( )`;
# `[ ]`; `{ }`) and some rows name a TYPE/EXPRESSION FORM rather than bare
# punctuation (`&T`, `*T`, `A..B`, `E?`, `<...>`, plus `T:maior` and `->`,
# which also happen to have their own bare rows elsewhere in the table and
# so need no special handling). spec_operator_atoms splits every group cell
# on whitespace; FORM_ROWS below derives the bare glyph a form row attests
# ONLY when that form row is still present, so the derivation tracks the
# table rather than standing in for it.
#
# ONE DIRECTION ONLY, on purpose. §8.4's table says of itself "All already
# in evidence in this document, recorded here rather than introduced" --
# `@nomen` is a row with no PUN_* at all (TOK_ANNOT, not punctuation), and
# token.inc's own header says tier B "is NOT in §8.4's table ... attestation,
# not enumeration" until §8.4 gives a complete delimiter list. So a §8.4 row
# with nothing in token.inc is not necessarily a problem; a token.inc tier-A
# PUN_* -- the tier that means "§8.4 is where this is normative" -- with no
# matching row in §8.4 always is. That is the one direction this check gates:
# a token claiming the table without the table being amended to agree. Note
# that this is NOT `PUN_TICK`'s shape -- it was declared tier B and so claimed
# §5.1, not the table (see the file header); this check would have let it
# through.
# ---------------------------------------------------------------------------

# §8.4's operators-and-sigils table, column 1 of each data row, one row per
# output line, backticks kept and escaped pipes (`+\|`) restored to real
# pipes. Stops at the first blank line after the table starts, the same
# "read until the shape ends" rule check 4's spec_keywords uses.
spec_operator_col1() {
  awk '/^### Operators and sigils/{f=1; next}
       f && /^\|/{print; started=1; next}
       f && started && /^$/{exit}' "$SPEC" \
    | grep '^\| `' \
    | sed 's/\\|/\x01/g' \
    | awk -F'|' '{print $2}' \
    | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//' \
    | sed 's/\x01/|/g'
}

# Every atomic punctuation spelling the table records, backticks stripped,
# one per line: a group cell's several backtick spans each become their own
# line (grep -o already does this), and a single span with an internal
# space (`( )`) is split on that space into its two atoms.
spec_operator_atoms() {
  spec_operator_col1 | grep -oE '`[^`]*`' | tr -d '`' | tr ' ' '\n' | grep -v '^$'
}

# form-row text -> the bare glyph it attests, for the §8.4 rows that record
# a TYPE/EXPRESSION FORM rather than bare punctuation. Reviewed by hand
# against the table as written (see this check's header above); a new form
# row in §8.4 is a change here too, on spec-check.sh's ABSENT_BY_DESIGN
# pattern -- a recorded decision, not a silent inference.
FORM_ROWS=("&T:&" "*T:*" "A..B:.." "E?:?" "<…>:<" "<…>:>")

spec_operator_atoms_with_forms() {
  local atoms; atoms="$(spec_operator_atoms)"
  printf '%s\n' "$atoms"
  local pair form glyph
  for pair in "${FORM_ROWS[@]}"; do
    form="${pair%%:*}"; glyph="${pair#*:}"
    if printf '%s\n' "$atoms" | grep -qxF -- "$form"; then
      printf '%s\n' "$glyph"
    fi
  done
}

# Every PUN_* entry in token.inc except PUN_COUNT (a count, not a token),
# name and glyph, tier A only. A PUN_* line with no parseable `; A|B <glyph>`
# tier comment is reported as its own failure below rather than silently
# skipped -- an undocumented token is exactly as uncheckable as an
# undocumented one that happens to carry a `; B` it does not deserve, and
# PUN_TICK's own landing is not known to have carried a tier comment at all.
code_pun_entries() {
  grep -E '^PUN_[A-Za-z0-9_]+[[:space:]]*=[[:space:]]*[0-9]+' "$TOKEN_INC" \
    | grep -v '^PUN_COUNT'
}

check_pun_sync() {
  echo "== 6. punctuation table sync (§8.4 operators/sigils <-> lexer/token.inc PUN_*) =="

  local spec_atoms n_spec
  spec_atoms="$(spec_operator_atoms_with_forms | sort -u)"
  n_spec="$(printf '%s\n' "$spec_atoms" | grep -c . || true)"
  if [[ "$n_spec" -eq 0 ]]; then
    bad "§8.4's operators-and-sigils table parsed to zero tokens"
    note "the table shape changed, or this extractor is wrong -- either way"
    note "this check is not measuring what it claims"
    return 0
  fi

  if [[ ! -f "$TOKEN_INC" ]]; then
    note "compiler/x86_64/lexer/token.inc does not exist yet."
    note "The lexer/ module is unwritten, so there is nothing to diff against."
    note "PASSES VACUOUSLY -- this is not evidence the table is in sync."
    note "§8.4's table currently parses to $n_spec distinct atoms."
    return 0
  fi

  local unparsed=0 mismatched=0 n_a=0 line name tier glyph
  while IFS= read -r line; do
    [[ -n "$line" ]] || continue
    name="$(echo "$line" | sed -E 's/^(PUN_[A-Za-z0-9_]+).*/\1/')"
    if [[ "$line" =~ \;[[:space:]]*([AB])[[:space:]]+([^[:space:]]+) ]]; then
      tier="${BASH_REMATCH[1]}"
      glyph="${BASH_REMATCH[2]}"
    else
      bad "$name: no parseable '; A <glyph>' or '; B <glyph>' tier comment -- cannot be checked against §8.4 at all"
      unparsed=1
      continue
    fi
    [[ "$tier" == "A" ]] || continue
    n_a=$((n_a + 1))
    if ! printf '%s\n' "$spec_atoms" | grep -qxF -- "$glyph"; then
      bad "$name ('$glyph', tier A -- claims §8.4) has no matching row in §8.4's operators-and-sigils table"
      mismatched=1
    fi
  done < <(code_pun_entries)

  if [[ "$unparsed" -eq 0 && "$mismatched" -eq 0 ]]; then
    ok "$n_a tier-A PUN_* tokens, every glyph found in §8.4's table ($n_spec atoms parsed)"
  fi
  note "tier B is attestation, not enumeration (token.inc's own header) -- not gated here"
  return 0
}

# ---------------------------------------------------------------------------
# 7. Lexicon table regeneration
#
# §3.3's root table, §3.4's suffix table and §3.5's prefix table are the
# normative source for compiler/x86_64/checker/lexicon/morphemes.inc, the
# same arrangement check 1 has with codes.inc and check 4 has with
# keywords.inc -- except gen-lexicon.py had no check at all: gen-codes.py is
# covered by check 1, gen-keywords.py by check 4, gen-lexicon.py by nothing.
# That silence is how morphemes.inc's own header kept emitting stale prose
# (describing the lexicon pass as unwritten and disabled) across however
# many regenerations happened while nobody ran a diff.
#
# This check RUNS the generator rather than re-deriving its extraction
# (unlike checks 1/4/6): gen-lexicon.py already refuses to guess at a law,
# a kind word or an imperative form it does not recognise (see its own
# header), so re-implementing that parser a second time here would be the
# thing CLAUDE.md's evidence discipline forbids -- a second, divergent
# opinion about what §3.3-§3.5 say, instead of running the one that exists.
# ---------------------------------------------------------------------------
check_lexicon_regen() {
  echo "== 7. lexicon table regeneration (§3.3-§3.5 <-> checker/lexicon/morphemes.inc) =="

  if [[ ! -f "$MORPHEMES_INC" ]]; then
    note "compiler/x86_64/checker/lexicon/morphemes.inc does not exist yet."
    note "PASSES VACUOUSLY -- this is not evidence the table is in sync."
    return 0
  fi

  if [[ ! -f "$GEN_LEXICON" ]]; then
    bad "tools/gen-lexicon.py is missing -- morphemes.inc exists with nothing that generates it"
    return 0
  fi

  if ! command -v python3 >/dev/null 2>&1; then
    bad "python3 not found on PATH -- tools/gen-lexicon.py cannot be run to check morphemes.inc"
    note "python3 is a devShell dependency (verification-only, as it is for"
    note "gen-codes.py and gen-keywords.py); this check needs nix develop"
    return 0
  fi

  local tmp; tmp="$(mktemp -d)"
  if ! python3 "$GEN_LEXICON" --spec "$SPEC" --out "$tmp/morphemes.inc" \
       >"$tmp/gen.log" 2>&1; then
    bad "tools/gen-lexicon.py refused to regenerate from the current spec:"
    sed 's/^/           /' "$tmp/gen.log"
    rm -rf "$tmp"
    return 0
  fi

  if cmp -s "$tmp/morphemes.inc" "$MORPHEMES_INC"; then
    ok "morphemes.inc is byte-identical to a fresh regeneration from §3.3-§3.5"
  else
    bad "morphemes.inc is STALE -- regenerating from the current spec differs:"
    diff "$MORPHEMES_INC" "$tmp/morphemes.inc" | head -40 | sed 's/^/           /'
    note "regenerate with: python3 tools/gen-lexicon.py --spec docs/spec/exsecutor-spec-v0.4.md \\"
    note "  --out compiler/x86_64/checker/lexicon/morphemes.inc"
  fi
  rm -rf "$tmp"
  return 0
}

check_registry_sync
echo
check_citations
echo
check_markers
echo
check_keyword_sync
echo
check_artifact_citations
echo
check_pun_sync
echo
check_lexicon_regen
echo
echo "== summary =="
if [[ "$FAIL" -eq 0 ]]; then
  echo "RESULT: PASS"
  exit 0
fi
echo "failures: $FAIL"
echo "RESULT: FAIL"
exit 1
