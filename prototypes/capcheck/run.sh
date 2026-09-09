#!/usr/bin/env bash
# Capability-row checker prototype. Must accept ok_*.xsc and reject bad_*.xsc.
#
# exsecutor_check.py does not exist in this tree yet, and cases/ is empty.
# See prototypes/capcheck/README.md for why, and for what rebuilding this
# probe needs. Spec §4's figures stay [UNREPRODUCED] until it exists and
# this script passes against real case files.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -f exsecutor_check.py ]; then
    echo "error: prototypes/capcheck/exsecutor_check.py is absent." >&2
    echo "This prototype has not been rebuilt yet -- see prototypes/capcheck/README.md." >&2
    echo "Spec §4's claim (468 lines, six attacks, two legitimate programs) stays" >&2
    echo "[UNREPRODUCED] until new code exists here and is actually run." >&2
    exit 1
fi

if ! compgen -G "cases/ok_*.xsc" >/dev/null || ! compgen -G "cases/bad_*.xsc" >/dev/null; then
    echo "error: no case files under prototypes/capcheck/cases/" >&2
    echo "Need cases/ok_*.xsc (legitimate programs) and cases/bad_*.xsc (attacks)." >&2
    echo "See prototypes/capcheck/README.md for what they need to cover." >&2
    exit 1
fi

python3 exsecutor_check.py -v cases/ok_*.xsc
python3 exsecutor_check.py cases/bad_*.xsc
