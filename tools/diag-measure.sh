#!/usr/bin/env bash
# tools/diag-measure.sh
# ---------------------------------------------------------------------------
# The §16 Stage 1 kill-criterion measurement, made re-runnable.
#
# docs/design/diagnostics-review.md evaluated spec §16's kill criterion for
# diagnostics quality ("Bad here -> stop and fix") against a corpus of 22
# hand-written broken programs and reported a headline number -- 46
# diagnostics, 37 of them (80%) the identical sentence `unexpected token`.
# That corpus was never checked in, so the number was [UNREPRODUCED] by
# CLAUDE.md's evidence discipline. tests/diagnostics/ is the corpus,
# reconstructed from the review's own table; this script is what measures
# it.
#
# Runs `exsc aedifica --hospes x86_64-linux --diagnostica json FILE` over
# every *.exsc file in the corpus directory, parses the JSON-lines
# diagnostics from stderr, and reports:
#   - total diagnostics across the corpus
#   - count per distinct message text (desc by count, then by text)
#   - count per code (desc by count, then by code)
#   - the single most common message, its count, and its percentage of total
#   - how many diagnostics carry a non-null note, related span, and fix
#   - one line per file: `cNN: N diags [codes...]`
#
# This is a MEASUREMENT, not a gate: it always exits 0, and tests/run.sh
# does not invoke it (see tests/diagnostics/README.md for why, and what a
# future gate might threshold on). Read-only: it builds nothing and writes
# nothing outside its own stdout.
#
# Usage:
#   tools/diag-measure.sh [--exsc PATH] [--corpus DIR]
#
#   --exsc PATH    path to the exsc binary (default: $REPO_ROOT/build/exsc)
#   --corpus DIR   directory of *.exsc cases (default: $REPO_ROOT/tests/diagnostics)
#
# Uses only bash, coreutils, and python3 (confirmed present in the devShell
# alongside jq; python3 is used here as the JSON parser -- this tool is
# verification-only and is not on the build or compile path).
# ---------------------------------------------------------------------------
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EXSC="$REPO_ROOT/build/exsc"
CORPUS="$REPO_ROOT/tests/diagnostics"

while [ $# -gt 0 ]; do
	case "$1" in
	--exsc)
		EXSC="$2"
		shift 2
		;;
	--corpus)
		CORPUS="$2"
		shift 2
		;;
	*)
		echo "usage: $0 [--exsc PATH] [--corpus DIR]" >&2
		exit 2
		;;
	esac
done

if [ ! -x "$EXSC" ]; then
	echo "diag-measure: not executable: $EXSC" >&2
	exit 2
fi
if [ ! -d "$CORPUS" ]; then
	echo "diag-measure: no such directory: $CORPUS" >&2
	exit 2
fi

# One JSON array of {file, diags:[...]} per case, deterministic order
# (glob sort, which is the case files' cNN_ prefix order), fed to python for
# aggregation and formatting. Each exsc invocation is independent -- no
# state crosses files, so file order cannot affect the aggregate counts,
# only the per-file report's order, which is fixed by the sorted glob.
RUNS_JSON="$(mktemp)"
trap 'rm -f "$RUNS_JSON"' EXIT

{
	echo "["
	first=1
	shopt -s nullglob
	for f in "$CORPUS"/*.exsc; do
		base="$(basename "$f" .exsc)"
		out="$(mktemp)"
		"$EXSC" aedifica --hospes x86_64-linux --diagnostica json "$f" >/dev/null 2>"$out"
		status=$?
		if [ "$first" -eq 1 ]; then
			first=0
		else
			echo ","
		fi
		python3 - "$base" "$f" "$status" "$out" <<'PYEOF'
import json, sys
base, path, status, outpath = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
diags = []
malformed = 0
with open(outpath, "r", encoding="utf-8", errors="replace") as fh:
	for line in fh:
		line = line.rstrip("\n")
		if not line.strip():
			continue
		try:
			diags.append(json.loads(line))
		except json.JSONDecodeError:
			malformed += 1
print(json.dumps({
	"case": base,
	"file": path,
	"exit_status": status,
	"diags": diags,
	"malformed_lines": malformed,
}))
PYEOF
		rm -f "$out"
	done
	echo
	echo "]"
} >"$RUNS_JSON"

python3 - "$RUNS_JSON" <<'PYEOF'
import json, sys
from collections import Counter, OrderedDict

with open(sys.argv[1], "r", encoding="utf-8") as fh:
	runs = json.load(fh)

total = 0
msg_counts = Counter()
code_counts = Counter()
n_note = 0
n_related = 0
n_fix = 0
n_malformed = 0
n_crash = 0
per_file_lines = []

for run in runs:
	case = run["case"]
	diags = run["diags"]
	status = run["exit_status"]
	malformed = run["malformed_lines"]
	n_malformed += malformed

	# exsc's own exit-status contract (driver/msg.inc): 0 = clean, 1 =
	# diagnostics emitted. Anything else on a corpus of syntactically
	# broken-on-purpose files is itself a finding worth surfacing, not
	# silently folding into "0 diagnostics".
	if status not in (0, 1):
		n_crash += 1

	codes_this_file = []
	for d in diags:
		total += 1
		msg = d.get("message")
		code = d.get("code")
		msg_counts[msg] += 1
		code_counts[code] += 1
		codes_this_file.append(code)
		if d.get("note") is not None:
			n_note += 1
		if d.get("related") is not None:
			n_related += 1
		if d.get("fix") is not None:
			n_fix += 1

	flag = ""
	if status not in (0, 1):
		flag = f" [exit={status}]"
	if malformed:
		flag += f" [{malformed} malformed JSON line(s)]"
	# task-specified format is `cNN: N diags [codes...]` -- the case id is
	# the file's cNN prefix, not its full descriptive slug.
	case_id = case.split("_", 1)[0]
	per_file_lines.append(
		f"{case_id}: {len(diags)} diags [{', '.join(codes_this_file)}]{flag}"
	)

def sorted_counts(counter):
	# desc by count, then asc by the key itself -- deterministic and
	# independent of dict/hash iteration order (CLAUDE.md).
	return sorted(counter.items(), key=lambda kv: (-kv[1], str(kv[0])))

print("=" * 78)
print("exsecutor diagnostics measurement -- spec sec16 Stage 1 kill criterion")
print("=" * 78)
print()
print(f"cases run:          {len(runs)}")
print(f"total diagnostics:  {total}")
if n_crash:
	print(f"NON-1/0 EXIT:       {n_crash} case(s) exited outside {{0,1}} -- see per-file report")
if n_malformed:
	print(f"MALFORMED JSON:     {n_malformed} line(s) on stderr did not parse as JSON")
print()

print("-- by message text " + "-" * 58)
if total == 0:
	print("(no diagnostics)")
else:
	for msg, n in sorted_counts(msg_counts):
		pct = 100.0 * n / total
		print(f"  {n:4d}  {pct:5.1f}%  {msg!r}")
print()

print("-- by code " + "-" * 66)
if total == 0:
	print("(no diagnostics)")
else:
	for code, n in sorted_counts(code_counts):
		pct = 100.0 * n / total
		print(f"  {n:4d}  {pct:5.1f}%  {code}")
print()

if total > 0:
	top_msg, top_n = sorted_counts(msg_counts)[0]
	top_pct = 100.0 * top_n / total
	print(f"most common message: {top_msg!r}")
	print(f"  {top_n} of {total} diagnostics ({top_pct:.1f}%)")
	print()

print("-- record richness " + "-" * 58)
if total > 0:
	print(f"  note:     {n_note:4d} / {total} ({100.0*n_note/total:5.1f}%) non-null")
	print(f"  related:  {n_related:4d} / {total} ({100.0*n_related/total:5.1f}%) non-null")
	print(f"  fix:      {n_fix:4d} / {total} ({100.0*n_fix/total:5.1f}%) non-null")
else:
	print("  (no diagnostics)")
print()

print("-- per file " + "-" * 66)
for line in per_file_lines:
	print(line)
PYEOF

# This is a measurement, not a gate -- always exit 0 regardless of what was
# found. Callers that want a gate wrap this script and threshold its output
# themselves; see tests/diagnostics/README.md.
exit 0
