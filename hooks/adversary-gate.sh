#!/usr/bin/env bash
# hooks/adversary-gate.sh -- a PreToolUse gate for Claude Code sessions.
#
# Blocks `git push` of the development branch when the commits being pushed
# touch a security-relevant path (the checker, the prelude's capability
# routines, the lowering, or the syscall audit) without an `Adversary-Reviewed:`
# trailer anywhere in the pushed range. CLAUDE.md ("Security changes get an
# adversary pass") requires a separate adversarial review -- one that breaks the
# specific guarantee with running exploit programs -- before such a change
# merges, and a green `tests/run.sh` is explicitly not enough. This is the
# backstop so the pass is not forgotten; it is a reminder, not a cryptographic
# control (it runs only in Claude Code sessions, not for human git or CI).
#
# It reads the hook JSON on stdin and emits a PreToolUse permission decision.
# It fails OPEN on any ambiguity -- a push it cannot reason about is allowed, so
# a bug here never bricks the workflow -- and fails CLOSED only on the one clear
# case: a security path is touched and no marker is present. Non-security pushes
# (docs, CI, tests) and feature-branch (wt/*) pushes are allowed untouched.
#
# Bypass for an explicit, logged exception: ADVERSARY_OVERRIDE=1.
set -uo pipefail

DEV_BRANCH="ccr-5d6bd334-hasnvm"
SEC_PATHS_RE='^(compiler/x86_64/checker/|compiler/x86_64/prelude/|compiler/x86_64/lower/|tools/syscall-audit\.sh)'
MARKER='Adversary-Reviewed:'

allow() { printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"allow"}}\n'; exit 0; }
deny() {
  printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":%s}}\n' \
    "$(jq -Rn --arg r "$1" '$r')"
  exit 0
}

input=$(cat 2>/dev/null) || allow
cmd=$(printf '%s' "$input" | jq -r '.tool_input.command // ""' 2>/dev/null) || allow

# Only git push is of interest; and an explicit override is honoured.
case "$cmd" in *git*push*) ;; *) allow ;; esac
[ "${ADVERSARY_OVERRIDE:-}" = "1" ] && allow

# Must be this repository (no-op in Punctim/Oligarchy/anywhere else).
root=$(git rev-parse --show-toplevel 2>/dev/null) || allow
[ -f "$root/tools/syscall-audit.sh" ] && [ -d "$root/compiler/x86_64/checker" ] || allow

# Only gate a push of the development branch. A feature-branch push (wt/*, the
# insurance-push pattern) or any other explicit ref is allowed.
if ! printf '%s' "$cmd" | grep -qw "$DEV_BRANCH"; then
  printf '%s' "$cmd" | grep -Eq 'wt/|refs/|HEAD:' && allow
  cur=$(git -C "$root" rev-parse --abbrev-ref HEAD 2>/dev/null) || allow
  [ "$cur" = "$DEV_BRANCH" ] || allow
fi

# Scope to the commits actually being pushed (those not yet on the remote dev
# branch). If the remote ref is unknown (a first push), we cannot scope it, so
# allow rather than scan all of history.
base="origin/$DEV_BRANCH"
git -C "$root" rev-parse --verify -q "$base" >/dev/null 2>&1 || allow
range="$base..HEAD"
git -C "$root" rev-list -n1 "$range" >/dev/null 2>&1 || allow   # nothing to push

# Does any pushed commit touch a security-relevant path?
touched=""
while read -r h; do
  [ -n "$h" ] || continue
  if git -C "$root" show --name-only --format= "$h" 2>/dev/null | grep -Eq "$SEC_PATHS_RE"; then
    touched=1; break
  fi
done < <(git -C "$root" rev-list "$range" 2>/dev/null)
[ -n "$touched" ] || allow

# A marker anywhere in the pushed range clears it.
git -C "$root" log --format='%B' "$range" 2>/dev/null | grep -q "$MARKER" && allow

deny "Adversary-check gate (CLAUDE.md: 'Security changes get an adversary pass'): the commits you are pushing to $DEV_BRANCH touch security-relevant code (the checker, the prelude capability routines, the lowering, or tools/syscall-audit.sh) but no commit in the push carries an '$MARKER' trailer. A green tests/run.sh is not sufficient for a security boundary. Run the adversarial (fable) review that tries to break the specific guarantee with running exploit programs; when it reports CONFIRMED, add a trailer like 'Adversary-Reviewed: fable <verdict/agent>' to a commit in the push. For an explicit, deliberate exception, set ADVERSARY_OVERRIDE=1."
