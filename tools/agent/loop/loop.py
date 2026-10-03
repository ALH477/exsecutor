#!/usr/bin/env python3
# tools/agent/loop/loop.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# The generate -> compile -> read diagnostics -> retry loop for a (small,
# fine-tuned) language model writing Exsecutor, with the real compiler
# build/exsc as the oracle.
#
# Backend: any OpenAI-compatible /v1/chat/completions server (llama.cpp's
# llama-server, ollama, vLLM), spoken to with urllib. No SDK.
#
# Model output is UNTRUSTED DATA. It is never executed by this script, never
# interpolated into a shell string, and never used to name a file: it is
# written to <workdir>/iter-NN/prog.exsc (a fixed name) and handed to exsc as
# an argv list. By default nothing the model wrote is ever run. With --run the
# emitted binary is executed ONLY through --runner (a sandbox runner), and
# --run without --runner is refused.
#
# Reused, not copied: code-block extraction and the exsc command line come
# from tools/finetune/harness.py; the §13 table parser and the rendering /
# application of exsc's own edit payload come from tools/gen-finetune.py (the
# same words the fine-tuning set was written with). Both are imported with
# importlib and neither is modified.
#
# Deterministic given the same model responses: no clock, no hostname, no
# environment in the transcript; the work directory is scrubbed to <work>.
# ---------------------------------------------------------------------------
"""Generate -> exsc check -> feed diagnostics back -> retry, against a local
OpenAI-compatible chat-completions server.

Exit status: 0 converged; 1 did not converge (max iterations or stuck);
2 usage or setup error; 3 backend failure; 4 runner (sandbox) failure."""

import argparse
import hashlib
import importlib.util
import ipaddress
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

sys.dont_write_bytecode = True  # never leave __pycache__ beside a source file

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))

EXIT_CONVERGED, EXIT_NOT_CONVERGED, EXIT_USAGE, EXIT_BACKEND, EXIT_RUNNER = 0, 1, 2, 3, 4

FASMG_TIMEOUT = 120
RUNNER_OUTER_TIMEOUT = 60   # the runner enforces its own 10 s; this is a backstop
MAX_DIAGS_SHOWN = 8         # diagnostics described per feedback message
MAX_EDIT_CHECKS = 8         # exsc re-runs to test exsc's own edits, per iteration
KEEP = 2000                 # characters of program stdout / tool output kept
SNIPPET_MAX = 100


def die(msg, code=EXIT_USAGE):
    sys.stderr.write("loop.py: %s\n" % msg)
    sys.exit(code)


def load_module(name, path):
    if not os.path.isfile(path):
        die("cannot find %s (pass --repo)" % path)
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def est_tokens(text):
    """A crude token estimate: characters / 4, rounded up. Not a tokenizer."""
    return (len(text) + 3) // 4


# ---------------------------------------------------------------------------
# The cheat sheet. Built from datasets/finetune/lexicon.json (words, glosses,
# atoms, operator words) and from §13 (code meanings). The pitfalls and the
# example are editorial; every code a pitfall cites was measured against exsc
# and selftest.py re-measures each one (bad form -> that code, good form ->
# clean) so the prompt cannot silently drift from the compiler.
# ---------------------------------------------------------------------------

# (text for the prompt, code exsc raises on the bad form or None, bad program, good program)
PITFALLS = [
    ("Comparison and logic are words: `a lt b`, never `a < b`. Likewise le ge gt eq ne, "
     "`et` for &&, `vel` for ||, `residuum` for %. `<` and `>` only delimit generic arguments.",
     "EXS-E0201",
     "publica functio f(a: u64, b: u64) -> u64 {\n    si a < b { redde a; }\n    redde b;\n}\n",
     "publica functio f(a: u64, b: u64) -> u64 {\n    si a lt b { redde a; }\n    redde b;\n}\n"),
    ("`if else return let fn while` are not keywords: write si / sin / aliter, redde, "
     "firma / mutabilis, functio, dum. Every statement ends with `;`.",
     "EXS-E0201",
     "publica functio f(a: u64) -> u64 {\n    if a gt 10 { return a; }\n    redde 0;\n}\n",
     "publica functio f(a: u64) -> u64 {\n    si a gt 10 { redde a; }\n    redde 0;\n}\n"),
    ("`firma` is immutable. A variable you assign again must be `mutabilis`.",
     "EXS-E0306",
     "publica functio f() -> u8 {\n    firma x: u8 = 1;\n    x = 2;\n    redde x;\n}\n",
     "publica functio f() -> u8 {\n    mutabilis x: u8 = 1;\n    x = 2;\n    redde x;\n}\n"),
    ("No implicit widening or narrowing: convert with `sicut`, e.g. `redde x sicut u32;`.",
     "EXS-E0303",
     "publica functio f(x: u8) -> u32 {\n    redde x;\n}\n",
     "publica functio f(x: u8) -> u32 {\n    redde x sicut u32;\n}\n"),
    ("`discerne` over an integer needs an `aliter { ... }` arm; literal `casus` arms are never exhaustive.",
     "EXS-E0351",
     "publica functio f(m: u8) -> u8 {\n    discerne m {\n        casus 1 { redde 10; }\n    }\n}\n",
     "publica functio f(m: u8) -> u8 {\n    discerne m {\n        casus 1 { redde 10; }\n        aliter { redde 0; }\n    }\n}\n"),
    ("No `true`/`false`: a truth value is `u1`, written 1 or 0.",
     "EXS-E0301",
     "publica functio f() -> u1 {\n    redde true;\n}\n",
     "publica functio f() -> u1 {\n    redde 1;\n}\n"),
    # Measured: exsc ACCEPTS a bare `dum` outside `profilum certum`, so this is
    # a convention the prompt asks for, not a rule the compiler enforces.
    ("Give every `dum` a bound: `dum i lt n terminus 1000 { ... }`; running past it aborts instead of hanging.",
     None,
     "publica functio f(n: u64) -> u64 {\n    mutabilis i: u64 = 0;\n    dum i lt n {\n        i = i + 1;\n    }\n    redde i;\n}\n",
     "publica functio f(n: u64) -> u64 {\n    mutabilis i: u64 = 0;\n    dum i lt n terminus 1000 {\n        i = i + 1;\n    }\n    redde i;\n}\n"),
    ("Strings have no escapes: `\\n` is a backslash and an n. For a newline, break the line inside the quotes.",
     None, None, None),
    ("A program's entry point is `publica functio initium(m: Mundus) -> u8`; its result is the exit status.",
     "EXS-E0424",
     "publica functio initium() -> u8 {\n    redde 0;\n}\n",
     "publica functio initium(m: Mundus) -> u8 {\n    redde 0;\n}\n"),
]

# (program, exit status, stdout). The first is datasets/finetune/src/cases/
# run-collatz.exsc verbatim (exsc-run, exit=111); the second is run-hello-world
# reduced to `initium`, with a raw line break in the literal because string
# escapes are [OPEN] (§8.1: a literal's bytes are taken verbatim). selftest.py
# compiles, assembles and runs both and compares exit status and stdout.
EXAMPLES = [
    ("""functio gradus(n: u64) -> u64 {
    mutabilis x: u64 = n;
    mutabilis k: u64 = 0;
    dum x ne 1 terminus 200 {
        si x residuum 2 eq 0 {
            x = x / 2;
        } aliter {
            x = 3 * x + 1;
        }
        k = k + 1;
    }
    redde k;
}

publica functio initium(m: Mundus) -> u8 {
    redde gradus(27) sicut u8;
}
""", 111, ""),
    ("""publica functio initium(m: Mundus) -> u8 {
    firma a = m.ambitus();
    sub ambitus = a;
    firma s = Scriptor.ad_exitum(a);
    s.scribe("Ave, mundus.
");
    redde 0;
}
""", 0, "Ave, mundus.\n"),
]


def _plain(s):
    """Strip section references and backticks from a lexicon gloss."""
    s = re.sub(r"\s*\((?:§|`tests|\d{4}-)[^)]*\)", "", s)
    s = s.split(" — ")[0]
    return s.replace("`", "").strip()


def build_cheatsheet(lexicon, codes):
    for _, code, _, _ in PITFALLS:
        if code is not None and code not in codes:
            die("cheat sheet cites %s, which is not in the §13 registry" % code)
    out = ["You write Exsecutor, a systems language whose keywords are Latin. Answer with ONE complete "
           "program in a ```exsecutor fenced block. When the compiler (exsc) reports errors, answer with "
           "the whole corrected program in one ```exsecutor block.", ""]
    out.append("Reserved words (English analogue):")
    groups = []
    for r in lexicon["reserved"]:
        alts = re.findall(r"`([^`]+)`", r["english"])
        if alts:
            gloss = "/".join(alts[:2])
        else:
            gloss = "(" + _plain(re.sub(r"^none;\s*", "", r["english"])) + ")"
        if not groups or groups[-1][0] != r["group"]:
            groups.append((r["group"], []))
        groups[-1][1].append("%s=%s" % (r["word"], gloss))
    for g, items in groups:
        out.append("- %s: %s" % (g, ", ".join(items)))
    out.append("Operator words (no symbol forms): " + ", ".join(
        "%s for %s" % (w["word"], w["symbol"].replace("a ", "").replace(" b", "").replace(" n", ""))
        for w in lexicon["operator_words"]))
    syms = []
    for o in lexicon["operators"]:
        toks = [t for t in o["tokens"] if t not in ("( )", "[ ]", "{ }", ",", ";", ".", "=", ":")]
        if toks:
            syms.append("%s %s" % (" ".join(toks), _plain(o["meaning"])))
    out.append("Symbols: " + "; ".join(syms) + ".")
    out.append("Capability atoms: " + ", ".join(
        "%s (%s)" % (a["atom"], _plain(a["gloss"])) for a in lexicon["capability_atoms"]) + ".")
    out.append("Types: u1 u8 u16 u32 u64, i8..i64, f32 f64, mensura (usize), textus (text).")
    out.append("")
    out.append("Rules (code = what exsc reports when broken):")
    for n, (text, code, _, _) in enumerate(PITFALLS, 1):
        out.append("%d. %s%s" % (n, text, " [%s]" % code if code else ""))
    for prog, status, stdout in EXAMPLES:
        out.append("")
        out.append("Example (exit status %d%s):" % (status, ", prints a line" if stdout else ""))
        out.append("```exsecutor\n" + prog + "```")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Repository context
# ---------------------------------------------------------------------------

class Context:
    def __init__(self, args):
        repo = os.path.abspath(args.repo)
        self.repo = repo
        self.harness = load_module("exs_ft_harness", os.path.join(repo, "tools", "finetune", "harness.py"))
        self.gen = self.harness.load_generator()
        self.spec_path = args.spec or os.path.join(repo, "docs", "spec", "exsecutor-spec-v0.4.md")
        self.lexicon_path = args.lexicon or os.path.join(repo, "datasets", "finetune", "lexicon.json")
        with open(self.spec_path, encoding="utf-8") as f:
            self.codes = dict(self.gen.parse_codes(f.read()))
        with open(self.lexicon_path, encoding="utf-8") as f:
            self.lexicon = json.load(f)
        self.system_prompt = build_cheatsheet(self.lexicon, self.codes)
        self.exsc = os.path.abspath(args.exsc or os.environ.get("EXSC") or os.path.join(repo, "build", "exsc"))
        self.include = os.path.abspath(args.include or os.environ.get("INCLUDE")
                                       or os.path.join(repo, "vendor", "fasmg-x86"))
        self.fasmg = None
        self.hospes = self.harness.HOSPES
        self.exsc_timeout = self.harness.EXSC_TIMEOUT


# ---------------------------------------------------------------------------
# The backend
# ---------------------------------------------------------------------------

def normalise_endpoint(url):
    url = url.rstrip("/")
    if url.endswith("/chat/completions"):
        return url
    if url.endswith("/v1"):
        return url + "/chat/completions"
    return url + "/v1/chat/completions"


def is_local(url):
    host = urllib.parse.urlsplit(url).hostname or ""
    if host == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


class BackendError(Exception):
    pass


def chat(endpoint, payload, timeout):
    body = json.dumps(payload, ensure_ascii=True, sort_keys=True).encode("utf-8")
    req = urllib.request.Request(endpoint, data=body, method="POST",
                                 headers={"Content-Type": "application/json"})
    # No proxy for a local server: an HTTP(S)_PROXY in the environment must not
    # route a localhost request elsewhere.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(req, timeout=timeout) as r:
            raw = r.read()
    except urllib.error.HTTPError as e:
        detail = e.read()[:300].decode("utf-8", "replace")
        raise BackendError("HTTP %d from backend: %s" % (e.code, detail))
    except urllib.error.URLError as e:
        raise BackendError("cannot reach backend: %s" % (e.reason,))
    except (TimeoutError, OSError) as e:
        raise BackendError("backend I/O failure: %s" % (e,))
    try:
        j = json.loads(raw.decode("utf-8"))
        choice = j["choices"][0]
        text = choice["message"]["content"]
        if not isinstance(text, str):
            raise TypeError("content is not a string")
    except (ValueError, KeyError, IndexError, TypeError) as e:
        raise BackendError("malformed response from backend: %s" % (e,))
    # Lone surrogates cannot be written as UTF-8; replace them and say so.
    clean = text.encode("utf-8", "replace").decode("utf-8")
    return clean, choice.get("finish_reason"), clean != text


# ---------------------------------------------------------------------------
# The compiler, and running what it emits
# ---------------------------------------------------------------------------

class Tools:
    def __init__(self, ctx, work, runner=None):
        self.ctx, self.work, self.runner = ctx, work, runner

    def scrub(self, s, limit=KEEP):
        s = (s or "")
        for p in (self.work, self.ctx.include, self.ctx.repo):
            s = s.replace(p, "<work>" if p == self.work else "<repo>")
        return s[:limit]

    def write_program(self, d, source):
        os.makedirs(d, exist_ok=True)
        # Bytes, not text mode: a CR the model wrote reaches exsc as a CR (§8.1).
        with open(os.path.join(d, "prog.exsc"), "wb") as f:
            f.write(source.encode("utf-8"))

    def check(self, d, source):
        """exsc aedifica --diagnostica json on <d>/prog.exsc (the harness's command line)."""
        self.write_program(d, source)
        argv = [self.ctx.exsc, "aedifica", "--hospes", self.ctx.hospes, "--diagnostica", "json", "prog.exsc"]
        try:
            r = subprocess.run(argv, capture_output=True, cwd=d, timeout=self.ctx.exsc_timeout,
                               env={}, stdin=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            return {"clean": False, "exsc_rc": None, "signal": None, "diagnostics": [], "other": "",
                    "error": "exsc_timeout"}
        diags, other = [], []
        for line in (r.stdout + b"\n" + r.stderr).decode("utf-8", "replace").splitlines():
            s = line.strip()
            if s.startswith("{"):
                try:
                    j = json.loads(s)
                except ValueError:
                    other.append(s)
                    continue
                diags.append({k: j.get(k) for k in ("code", "line", "col", "message", "note",
                                                     "snippet", "fix", "suggestion", "span")})
            elif s:
                other.append(s)
        sig = -r.returncode if r.returncode < 0 else None
        return {"clean": r.returncode == 0 and not diags, "exsc_rc": r.returncode, "signal": sig,
                "diagnostics": diags, "other": self.scrub("\n".join(other)),
                "error": ("exsc_crash_signal_%d" % sig) if sig else None}

    def build_and_run(self, d):
        """exsc -o, fasmg, then the runner. Called only on a clean check, only with --run."""
        res = {"stage": None, "outcome": None, "exit": None, "abortus": None, "stdout": None,
               "detail": None}
        try:
            r = subprocess.run([self.ctx.exsc, "aedifica", "--hospes", self.ctx.hospes, "prog.exsc",
                                "-o", "prog.asm"], capture_output=True, cwd=d, env={},
                               stdin=subprocess.DEVNULL, timeout=self.ctx.exsc_timeout)
        except subprocess.TimeoutExpired:
            res.update(stage="lower", outcome="lower_failed", detail="exsc timed out")
            return res
        if r.returncode != 0:
            how = ("killed by signal %d" % -r.returncode) if r.returncode < 0 else ("exit %d" % r.returncode)
            res.update(stage="lower", outcome="lower_failed",
                       detail="%s; %s" % (how, self.scrub((r.stdout + r.stderr).decode("utf-8", "replace"), 400)))
            return res
        try:
            r = subprocess.run([self.ctx.fasmg, "prog.asm", "prog.bin"], capture_output=True, cwd=d,
                               env={"INCLUDE": self.ctx.include}, stdin=subprocess.DEVNULL,
                               timeout=FASMG_TIMEOUT)
        except subprocess.TimeoutExpired:
            res.update(stage="assemble", outcome="assemble_failed", detail="fasmg timed out")
            return res
        if r.returncode != 0:
            res.update(stage="assemble", outcome="assemble_failed",
                       detail="fasmg exit %d; %s" % (r.returncode,
                                                     self.scrub((r.stdout + r.stderr).decode("utf-8", "replace"), 400)))
            return res
        binp = os.path.join(d, "prog.bin")
        os.chmod(binp, 0o700)
        res["stage"] = "run"
        try:
            r = subprocess.run([self.runner, binp], capture_output=True, cwd=d, stdin=subprocess.DEVNULL,
                               env={"PATH": os.environ.get("PATH", "/usr/bin:/bin")},
                               timeout=RUNNER_OUTER_TIMEOUT)
        except subprocess.TimeoutExpired:
            res.update(outcome="runner_failure", detail="runner did not return within %d s" % RUNNER_OUTER_TIMEOUT)
            return res
        except OSError as e:
            res.update(outcome="runner_failure", detail="cannot start runner: %s" % e.strerror)
            return res
        err = r.stderr.decode("utf-8", "replace")
        res["stdout"] = self.scrub(r.stdout.decode("utf-8", "replace"))
        res["runner_stderr"] = self.scrub(err, 600)
        res["exit"] = r.returncode
        m = re.search(r"abortus (\d+)", err)
        if r.returncode < 0 or r.returncode == 125:
            res.update(outcome="runner_failure", detail="runner exit %d" % r.returncode)
        elif r.returncode == 124:
            res["outcome"] = "timeout"
        elif r.returncode == 132 and m:
            res.update(outcome="abort", abortus=int(m.group(1)))
        elif r.returncode == 132:
            res["outcome"] = "sigill"
        else:
            res["outcome"] = "exit"
        return res


# ---------------------------------------------------------------------------
# Feedback: machine facts only
# ---------------------------------------------------------------------------

def line_col_of_byte(source, off):
    pre = source.encode("utf-8")[:off].decode("utf-8", "replace")
    line = pre.count("\n") + 1
    return line, len(pre) - (pre.rfind("\n") + 1) + 1


def short(s, n=SNIPPET_MAX):
    s = (s or "").replace("\r", "\\r")
    return s if len(s) <= n else s[:n] + "..."


def codes_of(diags):
    out = []
    for d in diags:
        if d.get("code") not in out:
            out.append(d.get("code"))
    return out


def meaning(ctx, code):
    m = ctx.codes.get(code)
    return ("%s (§13)" % m) if m else "not in the §13 registry"


def edit_checks(ctx, tools, d, source, check):
    """Apply each edit exsc attached, alone, and re-run exsc. Returns a list aligned with diagnostics."""
    out, n = [], 0
    for i, dg in enumerate(check["diagnostics"][:MAX_DIAGS_SHOWN]):
        try:
            got = ctx.gen.describe_edit(source, dg)
        except (KeyError, TypeError, ValueError):
            got = None
        if not got or n >= MAX_EDIT_CHECKS:
            out.append(None)
            continue
        n += 1
        label, desc, edit = got
        try:
            edited = ctx.gen.apply_edit(source, edit)
        except (KeyError, TypeError, ValueError):
            out.append({"label": label, "desc": desc, "applied": None})
            continue
        r = tools.check(os.path.join(d, "edit-%d" % (i + 1)), edited)
        line, col = line_col_of_byte(source, edit.get("start", 0))
        out.append({"label": label, "desc": desc, "at_line": line, "at_col": col,
                    "after_clean": r["clean"], "after_codes": codes_of(r["diagnostics"]),
                    "after_count": len(r["diagnostics"])})
    return out


def feedback_for_check(ctx, source, check, edits):
    diags = check["diagnostics"]
    lines = source.split("\n")
    if check.get("error") == "exsc_timeout":
        return "exsc did not finish within %d s on this program." % ctx.exsc_timeout
    if not diags:
        how = ("killed by signal %d" % check["signal"]) if check["signal"] else ("exit status %s" % check["exsc_rc"])
        txt = "exsc rejected the program (%s) without a JSON diagnostic." % how
        if check["other"]:
            txt += "\nexsc printed: " + json.dumps(short(check["other"], 400))
        return txt
    head = "exsc rejected the program: %d diagnostic%s, code%s %s." % (
        len(diags), "" if len(diags) == 1 else "s", "" if len(codes_of(diags)) == 1 else "s",
        ", ".join(codes_of(diags)))
    out = [head]
    for i, dg in enumerate(diags[:MAX_DIAGS_SHOWN]):
        code = dg.get("code")
        out.append("")
        out.append("%d. %s: %s" % (i + 1, code, meaning(ctx, code)))
        if dg.get("message") and dg.get("message") != ctx.codes.get(code):
            out.append("   exsc message: %s" % dg["message"])
        where = "   at line %s, column %s" % (dg.get("line"), dg.get("col"))
        if dg.get("snippet"):
            where += ", on `%s`" % short(dg["snippet"])
        out.append(where)
        ln = dg.get("line")
        if isinstance(ln, int) and 1 <= ln <= len(lines):
            out.append("   line %d reads: `%s`" % (ln, short(lines[ln - 1].strip())))
        if dg.get("note"):
            out.append("   exsc note: %s" % dg["note"])
        e = edits[i] if i < len(edits) else None
        if e:
            kind = "machine-applicable fix" if e["label"] == "machine-applicable fix" else \
                "suggested edit (a hint, not marked machine-applicable)"
            txt = "   exsc %s: %s" % (kind, e["desc"])
            if "at_line" in e:
                txt += " (at line %d, column %d)." % (e["at_line"], e["at_col"])
                if e["after_clean"]:
                    txt += " Applying only this edit: exsc accepts the program (checked)."
                else:
                    txt += " Applying only this edit: exsc still rejects the program, %d diagnostic%s, %s (checked)." % (
                        e["after_count"], "" if e["after_count"] == 1 else "s", ", ".join(e["after_codes"]))
            out.append(txt)
    if len(diags) > MAX_DIAGS_SHOWN:
        out.append("")
        out.append("%d further diagnostics not shown." % (len(diags) - MAX_DIAGS_SHOWN))
    return "\n".join(out)


def feedback_for_run(res, expect_exit, expect_stdout):
    """(feedback or None if the run satisfies the task, stop_reason or None)."""
    o = res["outcome"]
    pre = "exsc accepted the program. "
    if o == "lower_failed":
        return pre + "Generating code with `exsc -o` then failed: %s." % res["detail"], None
    if o == "assemble_failed":
        return pre + "The assembly exsc emitted did not assemble: %s." % res["detail"], None
    if o == "runner_failure":
        return None, "runner_failure"
    if o == "timeout":
        return pre + "It was built and run: timed out (the runner's time limit; runner exit 124).", None
    if o == "abort":
        return pre + "It was built and run: aborted: abortus %d (a runtime abort; SIGILL, runner exit 132)." % (
            res["abortus"]), None
    if o == "sigill":
        return pre + "It was built and run: killed by SIGILL (runner exit 132) with no `abortus` line.", None
    facts = []
    if expect_exit is not None and res["exit"] != expect_exit:
        facts.append("it exited with status %d; the task expects exit status %d" % (res["exit"], expect_exit))
    if expect_stdout is not None and res["stdout"] != expect_stdout:
        facts.append("its standard output was %s; the task expects %s" % (
            json.dumps(short(res["stdout"], 400)), json.dumps(short(expect_stdout, 400))))
    if facts:
        return pre + "It was built and run: " + "; ".join(facts) + ".", None
    return None, None


# ---------------------------------------------------------------------------
# The loop
# ---------------------------------------------------------------------------

def dumps(o):
    return json.dumps(o, ensure_ascii=True, sort_keys=True)


def run_loop(args, ctx, work, tw):
    tools = Tools(ctx, work, args.runner)
    endpoint = normalise_endpoint(args.endpoint)
    messages = [{"role": "system", "content": ctx.system_prompt}, {"role": "user", "content": args.task_text}]
    tw({"kind": "header", "tool": "tools/agent/loop/loop.py", "model": args.model, "endpoint": endpoint,
        "task": args.task_text, "max_iters": args.max_iters, "run": bool(args.run), "runner_given": bool(args.runner),
        "temperature": args.temperature, "seed": args.seed, "max_tokens": args.max_tokens,
        "expect_exit": args.expect_exit, "expect_stdout": args.expect_stdout, "block": args.block,
        "system_prompt": ctx.system_prompt, "system_prompt_chars": len(ctx.system_prompt),
        "system_prompt_est_tokens_chars_div_4": est_tokens(ctx.system_prompt),
        "exsc_sha256": sha256_file(ctx.exsc), "spec_sha256": sha256_file(ctx.spec_path),
        "lexicon_sha256": sha256_file(ctx.lexicon_path)})
    seen = []
    last_code = None
    for n in range(1, args.max_iters + 1):
        payload = {"model": args.model, "messages": [dict(m) for m in messages],
                   "temperature": args.temperature, "max_tokens": args.max_tokens, "stream": False}
        if args.seed is not None:
            payload["seed"] = args.seed
        rec = {"kind": "iteration", "n": n, "request": payload}
        try:
            text, finish, replaced = chat(endpoint, payload, args.http_timeout)
        except BackendError as e:
            rec["backend_error"] = str(e)
            tw(rec)
            return "backend_failure", n, str(e), last_code
        rec["response"] = {"text": text, "finish_reason": finish, "invalid_unicode_replaced": replaced}
        code, how = ctx.harness.extract_code(text, which=args.block)
        rec["code"], rec["code_source"] = code, how
        key = code if code is not None else "\0no-code\0" + text
        if key in seen:
            rec["stuck"] = True
            rec["repeat_of"] = seen.index(key) + 1
            tw(rec)
            return "stuck", n, "iteration %d repeats iteration %d %s" % (
                n, seen.index(key) + 1, "program" if code is not None else "reply (no code block)"), last_code
        seen.append(key)
        if code is None:
            fb = "No fenced code block was found in the reply, so nothing was compiled."
            rec.update(check=None, run=None, feedback=fb, outcome="no_code")
            tw(rec)
            messages += [{"role": "assistant", "content": text}, {"role": "user", "content": fb}]
            continue
        last_code = code
        d = os.path.join(work, "iter-%02d" % n)
        check = tools.check(d, code)
        rec["check"] = check
        if not check["clean"]:
            edits = edit_checks(ctx, tools, d, code, check)
            rec["edit_checks"] = edits
            fb = feedback_for_check(ctx, code, check, edits)
            rec.update(run=None, feedback=fb, outcome="rejected")
            tw(rec)
            messages += [{"role": "assistant", "content": text}, {"role": "user", "content": fb}]
            continue
        if not args.run:
            rec.update(run=None, feedback=None, outcome="clean")
            tw(rec)
            return "converged", n, "exsc check clean (not run: check-only mode)", code
        res = tools.build_and_run(d)
        rec["run"] = res
        fb, stop = feedback_for_run(res, args.expect_exit, args.expect_stdout)
        if stop:
            rec.update(feedback=None, outcome=stop)
            tw(rec)
            return stop, n, res.get("detail") or "", code
        if fb is None:
            rec.update(feedback=None, outcome="ran")
            tw(rec)
            return "converged", n, "exsc check clean; ran: exit status %d" % res["exit"], code
        rec.update(feedback=fb, outcome="run_failed")
        tw(rec)
        messages += [{"role": "assistant", "content": text}, {"role": "user", "content": fb}]
    return "max_iters", args.max_iters, "no accepted program within %d iterations" % args.max_iters, last_code


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="loop.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Model output is untrusted: by default nothing it wrote is executed. --run executes the "
               "emitted binary only through --runner.")
    p.add_argument("--endpoint", help="base URL of an OpenAI-compatible server, e.g. http://127.0.0.1:8080 "
                                      "(/v1/chat/completions is appended unless present)")
    p.add_argument("--model", help="model name sent to the server")
    t = p.add_mutually_exclusive_group()
    t.add_argument("--task", help="the task, in English")
    t.add_argument("--task-file", help="read the task from this UTF-8 file")
    p.add_argument("--max-iters", type=int, default=6)
    p.add_argument("--run", action="store_true", help="after a clean check, build and run through --runner")
    p.add_argument("--runner", help="sandbox runner: `RUNNER ELF_PATH` (required by --run)")
    p.add_argument("--expect-exit", type=int, help="with --run: the exit status the task requires")
    p.add_argument("--expect-stdout", help="with --run: the exact stdout the task requires (\\n is a newline)")
    p.add_argument("--workdir", help="where programs are written (default: a fresh dir under /dev/shm, "
                                     "else the system temp dir; removed afterwards unless --keep-workdir)")
    p.add_argument("--keep-workdir", action="store_true")
    p.add_argument("--exsc", help="compiler (default: $EXSC, else <repo>/build/exsc)")
    p.add_argument("--fasmg", help="assembler for --run (default: $FASMG, else fasmg on PATH)")
    p.add_argument("--include", help="fasmg INCLUDE dir (default: $INCLUDE, else <repo>/vendor/fasmg-x86)")
    p.add_argument("--repo", default=os.environ.get("EXS_REPO") or DEFAULT_REPO,
                   help="repository root (default: $EXS_REPO, else three levels above this script)")
    p.add_argument("--spec", help="spec file (default: <repo>/docs/spec/exsecutor-spec-v0.4.md)")
    p.add_argument("--lexicon", help="lexicon (default: <repo>/datasets/finetune/lexicon.json)")
    p.add_argument("--seed", type=int, help="sent to the backend as `seed`")
    p.add_argument("--temperature", type=float, default=0.0)
    p.add_argument("--max-tokens", type=int, default=1024)
    p.add_argument("--http-timeout", type=float, default=600.0, help="seconds per backend request")
    p.add_argument("--block", choices=("first", "last"), default="last",
                   help="which code block of a reply is the program (default: last)")
    p.add_argument("--transcript", help="write one JSON line per iteration here")
    p.add_argument("--out", help="write the last extracted program here")
    p.add_argument("--allow-remote", action="store_true", help="permit a non-loopback endpoint")
    p.add_argument("--print-system-prompt", action="store_true",
                   help="print the cheat sheet and its size estimate, then exit")
    a = p.parse_args(argv)
    if a.expect_stdout is not None:
        a.expect_stdout = a.expect_stdout.replace("\\n", "\n")
    return a


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    ctx = Context(args)
    if args.print_system_prompt:
        sys.stdout.write(ctx.system_prompt)
        sys.stderr.write("system prompt: %d characters, ~%d tokens (estimate: characters / 4)\n" % (
            len(ctx.system_prompt), est_tokens(ctx.system_prompt)))
        return EXIT_CONVERGED
    # Refusals come before any backend contact.
    for need in ("endpoint", "model"):
        if not getattr(args, need):
            die("--%s is required" % need)
    if args.task is None and args.task_file is None:
        die("one of --task / --task-file is required")
    if args.max_iters < 1:
        die("--max-iters must be at least 1")
    if args.run and not args.runner:
        die("--run executes model-written code and is refused without --runner (a sandbox runner)")
    if (args.expect_exit is not None or args.expect_stdout is not None) and not args.run:
        die("--expect-exit / --expect-stdout need --run (and so a --runner)")
    if args.runner and not os.access(args.runner, os.X_OK):
        die("runner is not an executable file: %s" % args.runner)
    if args.runner:
        args.runner = os.path.abspath(args.runner)
    scheme = urllib.parse.urlsplit(args.endpoint).scheme
    if scheme not in ("http", "https"):
        die("--endpoint must be an http(s) URL")
    if not is_local(args.endpoint) and not args.allow_remote:
        die("--endpoint is not a loopback address; pass --allow-remote to permit it")
    if not os.access(ctx.exsc, os.X_OK):
        die("exsc not found at %s -- run `make` (needs fasmg on PATH) or pass --exsc" % ctx.exsc)
    if args.run:
        ctx.fasmg = args.fasmg or os.environ.get("FASMG") or shutil.which("fasmg")
        if not ctx.fasmg:
            die("--run needs fasmg: put it on PATH or pass --fasmg")
    if args.task_file:
        with open(args.task_file, encoding="utf-8") as f:
            args.task_text = f.read().strip()
    else:
        args.task_text = args.task

    made_tmp = args.workdir is None
    if made_tmp:
        base = "/dev/shm" if os.path.isdir("/dev/shm") and os.access("/dev/shm", os.W_OK) else None
        work = tempfile.mkdtemp(prefix="exs-loop-", dir=base)
    else:
        work = os.path.abspath(args.workdir)
        os.makedirs(work, exist_ok=True)
    work = os.path.realpath(work)

    tf = open(args.transcript, "w", encoding="utf-8", newline="\n") if args.transcript else None
    tools_for_scrub = Tools(ctx, work)

    def tw(rec):
        if tf:
            line = dumps(rec)
            line = tools_for_scrub.scrub(line, limit=len(line) + 1)
            tf.write(line + "\n")
            tf.flush()

    try:
        verdict, n, detail, code = run_loop(args, ctx, work, tw)
        tw({"kind": "verdict", "verdict": verdict, "iterations": n, "detail": detail})
    finally:
        if tf:
            tf.close()
        if made_tmp and not args.keep_workdir:
            shutil.rmtree(work, ignore_errors=True)
    if args.out and code is not None:
        with open(args.out, "wb") as f:
            f.write(code.encode("utf-8"))
    sys.stdout.write("verdict: %s after %d iteration%s -- %s\n" % (verdict, n, "" if n == 1 else "s",
                                                                    tools_for_scrub.scrub(detail)))
    return {"converged": EXIT_CONVERGED, "max_iters": EXIT_NOT_CONVERGED, "stuck": EXIT_NOT_CONVERGED,
            "backend_failure": EXIT_BACKEND, "runner_failure": EXIT_RUNNER}[verdict]


if __name__ == "__main__":
    sys.exit(main())
