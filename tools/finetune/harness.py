# tools/finetune/harness.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# Shared code for the held-out evaluation harness in tools/finetune/:
# repository paths, finding exsc and fasmg, the compiler judge, code-block
# extraction, and the small statistics (Wilson interval, exact McNemar).
#
# The judge is a COPY of the minimal logic of tools/gen-finetune.py's
# Verifier (same exsc command lines, same "check clean" rule: exit 0 and no
# JSON diagnostic; same abort shape: `abortus N` on fd 2, then SIGILL).
# It is copied rather than imported because the generator's Verifier calls
# sys.exit() on any disagreement -- correct for a generator, wrong for a
# scorer, whose whole job is to record disagreements. Case-file parsing IS
# imported from the generator (importing it has no side effects: everything
# runs under `if __name__ == "__main__"`), so the harness reads `//!`
# metadata with exactly the parser that built the dataset.
#
# Model output is untrusted. It is compiled and, for exit=/abort= items,
# EXECUTED: in a private temporary directory, with an empty environment, no
# stdin, and a timeout. That is not a sandbox. Use score.py --no-run, or run
# the scorer in a throwaway container, if that matters to you.
#
# Deterministic: no clock, no hostname, no environment in any output. Temp
# paths are scrubbed from every message that is written out.
# ---------------------------------------------------------------------------
"""Shared helpers for tools/finetune/ (not a command; see the README)."""

import glob
import importlib.util
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata

sys.dont_write_bytecode = True  # never leave __pycache__ beside a source file

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DATASET = os.path.join(ROOT, "datasets", "finetune")
VALIDATION = os.path.join(DATASET, "validation.jsonl")
CASES = os.path.join(DATASET, "src", "cases")
OUT = os.path.join(HERE, "out")
DEFAULT_EXSC = os.path.join(ROOT, "build", "exsc")
HOSPES = "x86_64-linux"

EXSC_TIMEOUT = 60
FASMG_TIMEOUT = 120
RUN_TIMEOUT = 10
KEEP_STDOUT = 2000  # characters of program stdout kept in a result


def die(prog, msg):
    sys.stderr.write("%s: %s\n" % (prog, msg))
    sys.exit(2)


def load_generator():
    """Import tools/gen-finetune.py as a module (for parse_case only)."""
    path = os.path.join(ROOT, "tools", "gen-finetune.py")
    spec = importlib.util.spec_from_file_location("gen_finetune", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def dumps(o):
    return json.dumps(o, ensure_ascii=False, sort_keys=True)


def read_jsonl(path):
    out = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                out.append(json.loads(line))
            except ValueError as e:
                raise SystemExit("%s:%d: not JSON: %s" % (path, n, e))
    return out


def write_text(path, text):
    """Write UTF-8, NFC, LF, no BOM (CLAUDE.md §8.1 rule for this repo)."""
    text = unicodedata.normalize("NFC", text).replace("\r\n", "\n")
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def find_fasmg(explicit=None):
    if explicit:
        return explicit
    p = shutil.which("fasmg")
    if p:
        return p
    hits = sorted(glob.glob("/nix/store/*fasmg*/bin/fasmg"))
    return hits[0] if hits else None


# --------------------------------------------------------------------------
# Code-block extraction
# --------------------------------------------------------------------------

FENCE_EXS = re.compile(r"```[ \t]*exsecutor[^\n]*\n(.*?)(?:```|\Z)", re.S)
FENCE_ANY = re.compile(r"```[^\n]*\n(.*?)(?:```|\Z)", re.S)


def extract_code(text, which="first"):
    """Return (code, source) where source says how it was found.

    source: "exsecutor-fence" | "other-fence" | None (no code block).
    `which` picks the first or the last block of the preferred kind. An
    unterminated fence (a generation cut off at max tokens) runs to the end
    of the text and is still judged: the compiler decides whether it is a
    program, not this function.
    """
    for rx, label in ((FENCE_EXS, "exsecutor-fence"), (FENCE_ANY, "other-fence")):
        blocks = [m.group(1) for m in rx.finditer(text or "")]
        if blocks:
            code = blocks[0] if which == "first" else blocks[-1]
            return code.rstrip("\n") + "\n", label
    return None, None


CODE_RX = re.compile(r"EXS-E\d{4}")


def strip_fences(text):
    return re.sub(r"```.*?(?:```|\Z)", " ", text or "", flags=re.S)


def extract_exit_number(text):
    """The exit status a trace answer states, from its prose (fences removed).

    Returns (number or None, method). Tries "exit status/code ... N" first,
    then "returns ... N", then the first integer in the prose.
    """
    prose = strip_fences(text)
    for method, rx in (("exit-phrase", r"exit\s+(?:status|code|value)\D{0,24}?(\d+)"),
                       ("returns-phrase", r"\breturns?\D{0,24}?(\d+)"),
                       ("first-integer", r"(?<![\w.])(\d+)(?![\w.])")):
        m = re.search(rx, prose, re.I)
        if m:
            return int(m.group(1)), method
    return None, None


# --------------------------------------------------------------------------
# The compiler judge
# --------------------------------------------------------------------------

class Judge:
    def __init__(self, exsc=None, fasmg=None, run=True):
        self.exsc = exsc or DEFAULT_EXSC
        if not os.access(self.exsc, os.X_OK):
            raise SystemExit("build/exsc not built -- run `make` first (it needs fasmg on PATH)")
        self.fasmg = find_fasmg(fasmg)
        self.allow_run = run
        if run and not self.fasmg:
            raise SystemExit("fasmg not found on PATH or under /nix/store; pass --fasmg or --no-run")
        self.work = tempfile.mkdtemp(prefix="exs-ft-eval-")
        self.include = os.environ.get("INCLUDE") or os.path.join(ROOT, "vendor", "fasmg-x86")
        self.n = 0

    def close(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def _scrub(self, s):
        s = (s or "").replace(self.work, "<work>")
        return s[:KEEP_STDOUT]

    def _slot(self, source):
        self.n += 1
        d = os.path.join(self.work, "%04d" % self.n)
        os.makedirs(d)
        with open(os.path.join(d, "prog.exsc"), "w", encoding="utf-8", newline="\n") as f:
            f.write(source)
        return d

    def check(self, source):
        """exsc aedifica --diagnostica json: the type-check level.

        Returns {"clean": bool, "exsc_rc": int, "codes": [...], "diagnostics": [...], "error": str|None}.
        """
        d = self._slot(source)
        try:
            r = subprocess.run([self.exsc, "aedifica", "--hospes", HOSPES, "--diagnostica", "json", "prog.exsc"],
                               capture_output=True, cwd=d, timeout=EXSC_TIMEOUT, env={})
        except subprocess.TimeoutExpired:
            return {"clean": False, "exsc_rc": None, "codes": [], "diagnostics": [], "error": "exsc_timeout"}
        out = (r.stdout + b"\n" + r.stderr).decode("utf-8", "replace")
        diags, codes = [], []
        for line in out.splitlines():
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                j = json.loads(line)
            except ValueError:
                continue
            c = j.get("code")
            diags.append({"code": c, "line": j.get("line"), "col": j.get("col"), "message": j.get("message")})
            if c not in codes:
                codes.append(c)
        err = None
        if r.returncode < 0:
            err = "exsc_crash_signal_%d" % -r.returncode
        return {"clean": r.returncode == 0 and not diags, "exsc_rc": r.returncode,
                "codes": codes, "diagnostics": diags[:20], "error": err}

    def run(self, source, expect, stdout):
        """Build, assemble, execute, compare with `expect` (exit=N | abort=N).

        Returns {"pass": bool, "reason": str|None, "exit": int|None, "stdout": str|None}.
        """
        if not self.allow_run:
            return {"pass": None, "reason": "not_run", "exit": None, "stdout": None}
        d = self._slot(source)
        try:
            r = subprocess.run([self.exsc, "aedifica", "--hospes", HOSPES, "prog.exsc", "-o", "prog.asm"],
                               capture_output=True, cwd=d, timeout=EXSC_TIMEOUT, env={})
        except subprocess.TimeoutExpired:
            return {"pass": False, "reason": "exsc_timeout", "exit": None, "stdout": None}
        if r.returncode != 0:
            reason = "lowering_failed" if r.returncode > 0 else "lowering_crash_signal_%d" % -r.returncode
            return {"pass": False, "reason": reason, "exit": None, "stdout": None}
        try:
            r = subprocess.run([self.fasmg, "prog.asm", "prog.bin"], capture_output=True, cwd=d,
                               timeout=FASMG_TIMEOUT, env={"INCLUDE": self.include})
        except subprocess.TimeoutExpired:
            return {"pass": False, "reason": "fasmg_timeout", "exit": None, "stdout": None}
        if r.returncode != 0:
            return {"pass": False, "reason": "assemble_failed", "exit": None, "stdout": None}
        binp = os.path.join(d, "prog.bin")
        os.chmod(binp, 0o700)
        try:
            r = subprocess.run([binp], capture_output=True, cwd=d, timeout=RUN_TIMEOUT,
                               stdin=subprocess.DEVNULL, env={})
        except subprocess.TimeoutExpired:
            return {"pass": False, "reason": "run_timeout", "exit": None, "stdout": None}
        got_out = r.stdout.decode("utf-8", "replace")
        got_err = r.stderr.decode("utf-8", "replace")
        res = {"exit": r.returncode, "stdout": self._scrub(got_out)}
        m = re.fullmatch(r"exit=(\d+)", expect)
        if m:
            want_out = stdout or ""
            if r.returncode != int(m.group(1)):
                res.update({"pass": False, "reason": "wrong_exit"})
            elif got_out != want_out:
                res.update({"pass": False, "reason": "wrong_stdout"})
            else:
                res.update({"pass": True, "reason": None})
            return res
        m = re.fullmatch(r"abort=(\d+)", expect)
        if m:
            ok = r.returncode == -4 and ("abortus %s" % m.group(1)) in got_err
            res.update({"pass": ok, "reason": None if ok else "wrong_abort"})
            return res
        raise ValueError("run() called with expect %r" % expect)


# --------------------------------------------------------------------------
# Statistics. n is tiny here; these are stated, not hidden.
# --------------------------------------------------------------------------

Z95 = 1.959963984540054


def wilson(k, n, z=Z95):
    """Wilson score 95% interval for k successes in n. (0, 1) when n == 0."""
    if n == 0:
        return 0.0, 1.0
    p = k / n
    den = 1 + z * z / n
    centre = (p + z * z / (2 * n)) / den
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den
    return max(0.0, centre - half), min(1.0, centre + half)


def mcnemar_exact(b, c):
    """Two-sided exact McNemar p-value: binomial(b + c, 1/2) on the discordant pairs."""
    n = b + c
    if n == 0:
        return 1.0
    k = min(b, c)
    tail = sum(math.comb(n, i) for i in range(k + 1)) / (2 ** n)
    return min(1.0, 2 * tail)


def fmt_rate(k, n):
    if n == 0:
        return "%d/%d     n/a" % (k, n)
    lo, hi = wilson(k, n)
    return "%d/%d  %5.1f%%  [%5.1f%%, %5.1f%%]" % (k, n, 100.0 * k / n, 100 * lo, 100 * hi)


if __name__ == "__main__":
    # A library, not a step of the chain; --help (or anything) says so.
    sys.stdout.write(__doc__ + "\nImported by make_eval_prompts.py, score.py, compare.py and selftest.py.\n"
                     "See tools/finetune/README.md.\n")
