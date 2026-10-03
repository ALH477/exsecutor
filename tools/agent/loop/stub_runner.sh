#!/bin/sh
# tools/agent/loop/stub_runner.sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# TEST STUB ONLY. NOT A SANDBOX. It runs the ELF directly on the host, with
# no network isolation, no empty root and no memory limit. selftest.py uses it
# solely to exercise loop.py's --run path against programs the selftest itself
# supplies (mock_backend.py's fixed scripts). Never point loop.py --runner at
# this file for a real model's output; use the sandbox runner instead.
#
# It implements only the observable half of the runner contract:
#   stub_runner.sh ELF_PATH
#   stdin=/dev/null; program stdout -> stdout; exits with the program's exit
#   status, 124 on a 10 s timeout, 128+N on signal N (so 132 on SIGILL), 125
#   when it cannot run at all.

if [ "$#" -ne 1 ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "usage: stub_runner.sh ELF_PATH   (TEST STUB, NOT A SANDBOX; see header)" >&2
    [ "$#" -eq 1 ] && exit 0
    exit 125
fi
[ -x "$1" ] || { echo "stub_runner: not executable: $1" >&2; exit 125; }
command -v timeout >/dev/null 2>&1 || { echo "stub_runner: timeout(1) not found" >&2; exit 125; }

# Not `exec`: GNU timeout re-raises a child's fatal signal on itself, and the
# shell turns that into 128+N here, which is the contract's 132 for SIGILL.
timeout 10 "$1" </dev/null
exit $?
