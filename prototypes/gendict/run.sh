#!/usr/bin/env bash
# Probe driver for Sec 15 open problem #5. Never referenced from the
# Makefile, flake.nix, or tests/run.sh -- see prototypes/README.md.
set -euo pipefail
cd "$(dirname "$0")"

echo "== default checker (Sec 4 as written) =="
python3 exsecutor_gendict_check.py -v cases/ok_*.exsc cases/bad_*.exsc

echo
echo "== --strict-static-ceiling experiment (NOT the default; see README.md) =="
python3 exsecutor_gendict_check.py --strict-static-ceiling \
    cases/ok_static_matches_ceiling.exsc \
    cases/ok_generic_matches_ceiling.exsc \
    cases/bad_static_impl_exceeds_member_ceiling.exsc
