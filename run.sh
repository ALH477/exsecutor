#!/bin/sh
# Capability-row checker prototype. Must accept ok_*.nom and reject bad_*.nom.
cd "$(dirname "$0")"
python3 nomos_check.py -v cases/ok_*.nom
python3 nomos_check.py cases/bad_*.nom
