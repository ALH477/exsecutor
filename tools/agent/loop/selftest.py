#!/usr/bin/env python3
# tools/agent/loop/selftest.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# End-to-end self-test of loop.py against mock_backend.py and the REAL
# compiler (build/exsc). No model is involved anywhere: every reply is
# scripted in mock_backend.py. The --run cases use stub_runner.sh, which is a
# test stub and NOT a sandbox (it runs only the programs this test scripts).
#
# Prints PASS/FAIL per case; exit 0 only if every case passes.
# ---------------------------------------------------------------------------
"""End-to-end self-test of loop.py (mock backend, real exsc, stub runner)."""

import argparse
import importlib.util
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
LOOP = os.path.join(HERE, "loop.py")
MOCK = os.path.join(HERE, "mock_backend.py")
STUB = os.path.join(HERE, "stub_runner.sh")


def load_loop():
    spec = importlib.util.spec_from_file_location("exs_agent_loop", LOOP)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Fail(Exception):
    pass


class Skip(Exception):
    pass


def need(cond, msg):
    if not cond:
        raise Fail(msg)


def read_jsonl(path):
    with open(path, encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


def snapshot(root, skip=()):
    out = []
    for d, dirs, files in os.walk(root):
        dirs[:] = sorted(x for x in dirs if os.path.join(d, x) not in skip)
        for f in sorted(files):
            p = os.path.join(d, f)
            if p not in skip:
                out.append(os.path.relpath(p, root))
    return out


class T:
    def __init__(self, a):
        self.a = a
        base = "/dev/shm" if os.path.isdir("/dev/shm") and os.access("/dev/shm", os.W_OK) else None
        self.box = os.path.realpath(tempfile.mkdtemp(prefix="exs-loop-selftest-", dir=base))
        self.log = os.path.join(self.box, "mock.log")
        self.canary = os.path.join(self.box, "e", "CANARY")
        self.mock = subprocess.Popen([sys.executable, MOCK, "--port", "0", "--log", self.log,
                                      "--canary", self.canary], stdout=subprocess.PIPE, text=True)
        line = self.mock.stdout.readline()
        m = re.fullmatch(r"PORT (\d+)\n", line)
        if not m:
            raise SystemExit("mock backend did not start: %r" % line)
        self.endpoint = "http://127.0.0.1:%s" % m.group(1)
        self.loop = load_loop()
        self.ctx = self.loop.Context(self.loop.parse_args(["--repo", a.repo] + (["--exsc", a.exsc] if a.exsc else [])))
        if not os.access(self.ctx.exsc, os.X_OK):
            raise SystemExit("exsc not built at %s -- run `make`" % self.ctx.exsc)
        self.fasmg = a.fasmg or os.environ.get("FASMG") or shutil.which("fasmg")
        self.ctx.fasmg = self.fasmg

    def close(self):
        self.mock.terminate()
        self.mock.wait()
        if not self.a.keep:
            shutil.rmtree(self.box, ignore_errors=True)

    def loop_run(self, model, extra, name, cwd=None, workdir=True, task="Return a when it exceeds 10, else 0."):
        d = os.path.join(self.box, name)
        os.makedirs(d, exist_ok=True)
        tr = os.path.join(d, "transcript.jsonl")
        argv = [sys.executable, LOOP, "--endpoint", self.endpoint, "--model", model, "--task", task,
                "--transcript", tr, "--repo", self.a.repo]
        if workdir:
            argv += ["--workdir", os.path.join(d, "work")]
        if self.a.exsc:
            argv += ["--exsc", self.a.exsc]
        if self.fasmg:
            argv += ["--fasmg", self.fasmg]
        r = subprocess.run(argv + extra, capture_output=True, text=True, timeout=600, cwd=cwd or d,
                           stdin=subprocess.DEVNULL)
        recs = read_jsonl(tr) if os.path.exists(tr) else []
        return r, recs, tr

    def mock_requests(self, model):
        if not os.path.exists(self.log):
            return []
        return [r for r in read_jsonl(self.log) if r["model"] == model]

    # -- cases ---------------------------------------------------------------

    def case_cheatsheet(self):
        sp = self.ctx.system_prompt
        est = self.loop.est_tokens(sp)
        need(est < 1500, "cheat sheet is ~%d tokens (chars/4), over budget" % est)
        for w in ("redde", "mutabilis", "terminus", "aliter", "lt", "residuum", "Mundus", "archivum"):
            need(re.search(r"\b%s\b" % w, sp), "cheat sheet lacks %s" % w)
        for c in set(re.findall(r"EXS-E\d{4}", sp)):
            need(c in self.ctx.codes, "cheat sheet cites %s, not in §13" % c)
        work = os.path.join(self.box, "cheat")
        tools = self.loop.Tools(self.ctx, work, STUB)
        notes = []
        for i, (text, code, bad, good) in enumerate(self.loop.PITFALLS, 1):
            if bad is None:
                continue
            rb = tools.check(os.path.join(work, "p%d-bad" % i), bad)
            got = [d["code"] for d in rb["diagnostics"]]
            if code is None:
                need(rb["clean"], "rule %d: stated as not enforced, but exsc now reports %s" % (i, got))
                notes.append("rule %d bad form accepted (as stated)" % i)
            else:
                need(code in got, "rule %d: bad form drew %s, not %s" % (i, got, code))
            rg = tools.check(os.path.join(work, "p%d-good" % i), good)
            need(rg["clean"], "rule %d: good form rejected: %s" % (i, [d["code"] for d in rg["diagnostics"]]))
        need(self.fasmg, "fasmg not found; cannot run the examples")
        for i, (prog, status, stdout) in enumerate(self.loop.EXAMPLES, 1):
            d = os.path.join(work, "ex%d" % i)
            need(tools.check(d, prog)["clean"], "example %d rejected" % i)
            res = tools.build_and_run(d)
            need(res["outcome"] == "exit" and res["exit"] == status and res["stdout"] == stdout,
                 "example %d: %r" % (i, res))
        # Rule 8 (no escapes) measured: "\n" in a literal prints a backslash and an n.
        esc = self.loop.EXAMPLES[1][0].replace('mundus.\n");', 'mundus.\\n");')
        d = os.path.join(work, "escape")
        need(tools.check(d, esc)["clean"], "escape probe rejected")
        res = tools.build_and_run(d)
        need(res["stdout"] == "Ave, mundus.\\n", "escape probe printed %r" % res["stdout"])
        return "~%d tokens (chars/4), %d chars; %d rules re-measured, %d examples ran; %s; `\\n` printed literally" % (
            est, len(sp), sum(1 for p in self.loop.PITFALLS if p[2]), len(self.loop.EXAMPLES), "; ".join(notes))

    def case_a_converge(self):
        r, recs, _ = self.loop_run("converge:a", [], "a")
        need(r.returncode == 0, "exit %d, want 0: %s" % (r.returncode, r.stdout + r.stderr))
        its = [x for x in recs if x["kind"] == "iteration"]
        need(len(its) == 2, "%d iterations, want 2" % len(its))
        c1 = [d["code"] for d in its[0]["check"]["diagnostics"]]
        need("EXS-E0201" in c1, "iteration 1 codes %s lack EXS-E0201" % c1)
        fb = its[0]["feedback"]
        need("EXS-E0201: unexpected token (§13)" in fb, "feedback lacks the §13 meaning")
        need("spells this si" in fb, "feedback lacks exsc's note")
        need(its[1]["check"]["clean"] and recs[-1]["verdict"] == "converged", "not converged")
        reqs = self.mock_requests("converge:a")
        need(len(reqs) == 2 and reqs[1]["last_user"] == fb, "the wire did not carry the feedback")
        for c in set(re.findall(r"EXS-E\d{4}", fb)):
            need(c in self.ctx.codes, "feedback carries %s, not in §13" % c)
        return "converged in 2; iteration 1 codes %s" % sorted(set(c1))

    def case_b_never(self):
        r, recs, _ = self.loop_run("never:b", ["--max-iters", "4"], "b")
        need(r.returncode == 1, "exit %d, want 1" % r.returncode)
        its = [x for x in recs if x["kind"] == "iteration"]
        need(len(its) == 4 and all(x["outcome"] == "rejected" for x in its), "want 4 rejected iterations")
        need(recs[-1]["verdict"] == "max_iters", "verdict %s" % recs[-1]["verdict"])
        need("verdict: max_iters after 4 iterations" in r.stdout, "stdout: %r" % r.stdout)
        return r.stdout.strip()

    def case_c_stuck(self):
        r, recs, _ = self.loop_run("stuck:c", [], "c")
        need(r.returncode == 1, "exit %d, want 1" % r.returncode)
        its = [x for x in recs if x["kind"] == "iteration"]
        need(recs[-1]["verdict"] == "stuck" and len(its) == 2 and its[1].get("repeat_of") == 1,
             "verdict %s after %d" % (recs[-1]["verdict"], len(its)))
        return r.stdout.strip()

    def case_d_backend(self):
        r, recs, _ = self.loop_run("http500:d", [], "d")
        need(r.returncode == 3 and recs[-1]["verdict"] == "backend_failure", "500: exit %d" % r.returncode)
        need("HTTP 500" in recs[-2]["backend_error"], recs[-2].get("backend_error"))
        r2, recs2, _ = self.loop_run("garbage:d", [], "d2")
        need(r2.returncode == 3 and "malformed" in recs2[-2]["backend_error"], "garbage: exit %d" % r2.returncode)
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        dead = s.getsockname()[1]
        s.close()
        argv = [sys.executable, LOOP, "--endpoint", "http://127.0.0.1:%d" % dead, "--model", "x", "--task", "t",
                "--repo", self.a.repo, "--workdir", os.path.join(self.box, "d3")]
        r3 = subprocess.run(argv, capture_output=True, text=True, timeout=120, stdin=subprocess.DEVNULL)
        need(r3.returncode == 3 and "cannot reach" in r3.stdout, "refused: exit %d %r" % (r3.returncode, r3.stdout))
        return "HTTP 500 / non-JSON 200 / connection refused -> exit 3 each"

    def case_e_hostile(self):
        e = os.path.join(self.box, "e")
        cwd = os.path.join(e, "cwd")
        os.makedirs(cwd)
        before = snapshot(self.box, skip=(self.log,))
        r, recs, tr = self.loop_run("hostile:e", ["--run", "--runner", STUB], "e", cwd=cwd)
        need(r.returncode == 1 and recs[-1]["verdict"] == "stuck", "exit %d verdict %s" % (
            r.returncode, recs[-1].get("verdict")))
        need(not os.path.exists(self.canary), "CANARY EXISTS: model text was executed")
        need(os.listdir(cwd) == [], "files appeared in the loop's cwd: %s" % os.listdir(cwd))
        work = os.path.join(e, "work")
        after = snapshot(self.box, skip=(self.log, tr, work))
        need(after == before, "files appeared outside --workdir: %s" % sorted(set(after) - set(before)))
        for f in snapshot(work):
            need(os.path.basename(f) == "prog.exsc", "unexpected file in workdir: %s" % f)
        for name in ("escape.exsc", "leak"):
            for root in (self.box, os.path.dirname(self.box)):
                need(not os.path.exists(os.path.join(root, name)), "%s/%s exists" % (root, name))
        its = [x for x in recs if x["kind"] == "iteration"]
        need("touch " + self.canary in (its[0]["code"] or ""), "hostile text did not reach the compiler as data")
        need(all(x.get("run") is None for x in its), "something was run")
        return "%d iterations, outcomes %s; canary absent; only prog.exsc files under --workdir" % (
            len(its), [x["outcome"] for x in its])

    def case_f_run(self):
        r, recs, _ = self.loop_run("run:f", ["--run", "--runner", STUB, "--expect-exit", "6"], "f",
                                   task="Return gcd(48, 18) as the exit status.")
        need(r.returncode == 0, "exit %d: %s" % (r.returncode, r.stdout + r.stderr))
        its = [x for x in recs if x["kind"] == "iteration"]
        need(len(its) == 3, "%d iterations, want 3" % len(its))
        need(its[0]["run"]["outcome"] == "abort" and "aborted: abortus 1" in its[0]["feedback"], its[0]["feedback"])
        need("exited with status 5; the task expects exit status 6" in its[1]["feedback"], its[1]["feedback"])
        need(its[2]["run"]["exit"] == 6 and recs[-1]["verdict"] == "converged", "last run %r" % its[2]["run"])
        return "abortus 1 -> exit 5 (expects 6) -> exit 6"

    def case_g_stdout(self):
        r, recs, _ = self.loop_run("hello:g", ["--run", "--runner", STUB, "--expect-stdout", "Ave, mundus.\\n"],
                                   "g", task="Print the line `Ave, mundus.`")
        need(r.returncode == 0 and recs[-1]["verdict"] == "converged", "exit %d" % r.returncode)
        return "stdout %s matched in 1 iteration" % json.dumps(recs[-2]["run"]["stdout"])

    def case_h_refusals(self):
        out = []
        r, _, _ = self.loop_run("converge:h1", ["--run"], "h1")
        need(r.returncode == 2 and "refused without --runner" in r.stderr, "--run w/o runner: %d" % r.returncode)
        need(self.mock_requests("converge:h1") == [], "backend was contacted before the refusal")
        out.append("--run without --runner")
        r, _, _ = self.loop_run("converge:h2", ["--expect-exit", "6"], "h2")
        need(r.returncode == 2 and self.mock_requests("converge:h2") == [], "--expect-exit w/o --run: %d" % r.returncode)
        out.append("--expect-exit without --run")
        argv = [sys.executable, LOOP, "--endpoint", "http://192.0.2.1:8080", "--model", "x", "--task", "t",
                "--repo", self.a.repo]
        r = subprocess.run(argv, capture_output=True, text=True, timeout=60, stdin=subprocess.DEVNULL)
        need(r.returncode == 2 and "loopback" in r.stderr, "non-loopback: %d" % r.returncode)
        out.append("non-loopback endpoint")
        return "refused with exit 2: " + ", ".join(out)

    def case_i_determinism(self):
        shm = "/dev/shm" if os.path.isdir("/dev/shm") else tempfile.gettempdir()
        pre = sorted(x for x in os.listdir(shm) if x.startswith("exs-loop-") and "selftest" not in x)
        r1, _, t1 = self.loop_run("converge:i", [], "i1")
        r2, _, t2 = self.loop_run("converge:i", [], "i2", workdir=False)   # default temp workdir
        need(r1.returncode == 0 and r2.returncode == 0, "exits %d %d" % (r1.returncode, r2.returncode))
        b1, b2 = open(t1, "rb").read(), open(t2, "rb").read()
        need(b1 == b2, "transcripts differ between two workdirs")
        need(self.box.encode() not in b1 and b"/dev/shm" not in b1 and b"/tmp" not in b1, "a path leaked")
        need(b"\r" not in b1 and all(c < 128 for c in b1), "transcript is not ASCII/LF")
        post = sorted(x for x in os.listdir(shm) if x.startswith("exs-loop-") and "selftest" not in x)
        need(pre == post, "default workdir left behind: %s" % sorted(set(post) - set(pre)))
        return "byte-identical transcripts (%d bytes) from two workdirs; default temp workdir removed" % len(b1)

    def case_j_runner_failure(self):
        bad = os.path.join(self.box, "fail_runner.sh")
        with open(bad, "w", encoding="utf-8", newline="\n") as f:
            f.write("#!/bin/sh\necho 'sandbox_run: sandbox setup failed: stub' >&2\nexit 125\n")
        os.chmod(bad, 0o700)
        r, recs, _ = self.loop_run("run:j", ["--run", "--runner", bad], "j")
        need(r.returncode == 4 and recs[-1]["verdict"] == "runner_failure", "exit %d" % r.returncode)
        seen = []
        for status, want in ((159, "SIGSYS, runner exit 159"), (139, "either exit status 139 or fatal signal 11")):
            fake = os.path.join(self.box, "fake_%d.sh" % status)
            with open(fake, "w", encoding="utf-8", newline="\n") as f:
                f.write("#!/bin/sh\n%sexit %d\n" % ("echo 'sandbox_run: killed by SIGSYS: stub' >&2\n" if status == 159 else "", status))
            os.chmod(fake, 0o700)
            r, recs, _ = self.loop_run("hello:j%d" % status, ["--run", "--runner", fake, "--max-iters", "1"],
                                       "j%d" % status)
            fb = recs[-2].get("feedback") or ""
            need(r.returncode == 1 and want in fb, "status %d: exit %d, feedback %r" % (status, r.returncode, fb))
            seen.append(str(status))
        # A bare 125 with no `sandbox_run:` line is a program that chose that exit status
        forged = os.path.join(self.box, "forged_125.sh")
        with open(forged, "w", encoding="utf-8", newline="\n") as f:
            f.write("#!/bin/sh\nexit 125\n")
        os.chmod(forged, 0o700)
        r, recs, _ = self.loop_run("hello:jf", ["--run", "--runner", forged, "--max-iters", "1"], "jf")
        run = [x for x in recs if x["kind"] == "iteration"][-1]["run"]
        need(r.returncode != 4 and recs[-1]["verdict"] != "runner_failure" and run["outcome"] == "exit"
             and run["exit"] == 125,
             "a forged bare 125 was not an ordinary exit: loop exit %d, run %r" % (r.returncode, run))
        # control bytes in program output never reach the transcript raw
        esc = os.path.join(self.box, "esc.sh")
        with open(esc, "w", encoding="utf-8", newline="\n") as f:
            f.write("#!/bin/sh\nprintf 'a\\033[31mb\\007c\\n'\nexit 0\n")
        os.chmod(esc, 0o700)
        r, recs, _ = self.loop_run("hello:je", ["--run", "--runner", esc, "--max-iters", "1"], "je")
        out = [x for x in recs if x["kind"] == "iteration"][-1]["run"]["stdout"]
        need("\x1b" not in out and "\x07" not in out and "\ufffd" in out, "control bytes survived: %r" % out)
        return "runner exit 125 -> runner_failure (exit 4); %s -> not converged, reported; bare forged 125 is an ordinary exit; control bytes scrubbed" % "/".join(seen)

    def case_k_real_runner(self):
        if not self.a.runner:
            raise Skip("no --runner given (pass the sandbox runner to exercise it)")
        r, recs, _ = self.loop_run("run:k", ["--run", "--runner", self.a.runner, "--expect-exit", "6"], "k",
                                   task="Return gcd(48, 18) as the exit status.")
        need(r.returncode == 0, "exit %d: %s" % (r.returncode, r.stdout + r.stderr))
        its = [x for x in recs if x["kind"] == "iteration"]
        need([x["run"]["outcome"] for x in its] == ["abort", "exit", "exit"], [x["run"] for x in its])
        r2, recs2, _ = self.loop_run("hello:k", ["--run", "--runner", self.a.runner, "--expect-stdout",
                                                 "Ave, mundus.\\n"], "k2")
        need(r2.returncode == 0, "hello: exit %d" % r2.returncode)
        return "%s: abortus 1 -> exit 5 -> exit 6; hello stdout matched" % os.path.basename(self.a.runner)


def main():
    p = argparse.ArgumentParser(prog="selftest.py", description=__doc__)
    p.add_argument("--repo", default=os.environ.get("EXS_REPO") or os.path.dirname(os.path.dirname(os.path.dirname(HERE))))
    p.add_argument("--exsc", help="compiler (default: <repo>/build/exsc)")
    p.add_argument("--fasmg", help="assembler (default: $FASMG, else fasmg on PATH)")
    p.add_argument("--keep", action="store_true", help="keep the scratch directory")
    p.add_argument("--runner", help="also run the --run cases through this (real) sandbox runner")
    a = p.parse_args()
    a.repo = os.path.abspath(a.repo)
    if a.runner:
        a.runner = os.path.abspath(a.runner)
    t = T(a)
    cases = [("cheatsheet", t.case_cheatsheet), ("a-converge", t.case_a_converge), ("b-never", t.case_b_never),
             ("c-stuck", t.case_c_stuck), ("d-backend", t.case_d_backend), ("e-hostile", t.case_e_hostile),
             ("f-run-expect-exit", t.case_f_run), ("g-run-expect-stdout", t.case_g_stdout),
             ("h-refusals", t.case_h_refusals), ("i-determinism", t.case_i_determinism),
             ("j-runner-statuses", t.case_j_runner_failure), ("k-real-runner", t.case_k_real_runner)]
    failed = skipped = 0
    try:
        for name, fn in cases:
            try:
                msg = fn()
                print("PASS  %-22s %s" % (name, msg))
            except Skip as e:
                print("SKIP  %-22s %s" % (name, e))
                skipped += 1
            except Fail as e:
                failed += 1
                print("FAIL  %-22s %s" % (name, e))
            except Exception as e:  # a crash in a case is a failure of that case, not of the run
                failed += 1
                print("FAIL  %-22s %s: %s" % (name, type(e).__name__, e))
            sys.stdout.flush()
    finally:
        t.close()
    print("%d/%d passed, %d skipped" % (len(cases) - failed - skipped, len(cases), skipped))
    if a.keep:
        print("scratch kept: %s" % t.box)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
