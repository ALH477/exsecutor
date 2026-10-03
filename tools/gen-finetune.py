#!/usr/bin/env python3
# tools/gen-finetune.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
#
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version. See LICENSE. Code produced by this compiler is not
# covered by the GPL -- see Exception A in LICENSE.EXCEPTION.
# ---------------------------------------------------------------------------
# Generates datasets/finetune/ -- the in-repo fine-tuning dataset for the
# Exsecutor lexicon and the language built on it -- from three kinds of input:
#
#   1. the SPEC's own tables (docs/spec/exsecutor-spec-v0.4.md): §8.4 reserved
#      and contextual words, operators and capability atoms, §3.3-§3.5
#      morphemes, §4.6 atom glosses, §13 error registry. Read, never invented:
#      the same arrangement tools/gen-codes.py has with §13 and
#      tools/gen-keywords.py has with §8.4.
#   2. datasets/finetune/src/cases/*.exsc -- hand-written programs, each one
#      RUN THROUGH THE REAL COMPILER before it may become a record. A case
#      declares what exsc must do with it (`expect:`); this script runs
#      build/exsc and refuses to write a single line if any case disagrees.
#      That is the dataset's only claim to correctness for code, and it is the
#      reason a record carries an `evidence` field saying how it was checked.
#   3. datasets/finetune/src/keywords.json, qa.json -- editorial text
#      (Latin glosses, English analogues, spec-grounded Q&A). Editorial is
#      labelled editorial: nothing in them is a measurement.
#
# EVIDENCE DISCIPLINE (CLAUDE.md). Every record says how it was checked:
#   exsc-run         compiled, assembled with fasmg, executed; exit status
#                    (and stdout, where declared) matched
#   exsc-diagnostic  exsc rejected the program with exactly the declared set
#                    of EXS-E codes, and the fixed twin was accepted
#   exsc-check       exsc accepted the program (type-checked; not executed)
#   compiler-measured  a statement about exsc's behaviour observed by running
#                    it, which the spec does not make
#   spec-table       read mechanically from a spec table
#   spec-text        a statement the spec makes, cited by section; the spec is
#                    the source of truth and has been wrong before
#   editorial        author-supplied gloss; not normative
# A claim with nothing behind it would be [OPEN]/[UNTESTED]; the morphology
# records are [UNTESTED] against the checker (the lexicon pass is built and
# not enabled, spec §3.3) and say so in their text.
#
# VERIFICATION-ONLY. Never on the build path (spec §18.1: fasmg plus the
# vendored macro package builds `exsc`). The outputs are committed like any other project file; CI or a
# human reruns `--check` and diffs.
#
# DETERMINISM (CLAUDE.md, §9.3). No clock, no hostname, no environment read
# beyond finding fasmg and the include path, no hash-dependent ordering:
# every list is kept in spec/file order and cases are visited in C-locale
# filename order. The train/validation split is a function of the record id.
# Regenerating from unchanged inputs reproduces every output byte for byte.
#
# Usage:
#   make all                                   # build/exsc must exist
#   python3 tools/gen-finetune.py              # write datasets/finetune/*
#   python3 tools/gen-finetune.py --check      # regenerate in memory, diff
#   python3 tools/gen-finetune.py --no-verify  # skip running exsc (spec-table
#                                              # and editorial records only are
#                                              # trustworthy; refuses to write)
# Spec: docs/spec/exsecutor-spec-v0.4.md §3, §4.6, §8.4, §13.
# ---------------------------------------------------------------------------
"""Generate datasets/finetune/ from the spec tables and compiler-verified cases."""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(ROOT, "docs", "spec", "exsecutor-spec-v0.4.md")
OUT = os.path.join(ROOT, "datasets", "finetune")
SRC = os.path.join(OUT, "src")
EXSC = os.path.join(ROOT, "build", "exsc")
HOSPES = "x86_64-linux"
VALIDATION_MODULUS = 10  # one record in ten, chosen by id hash


def die(msg):
    sys.stderr.write("gen-finetune: " + msg + "\n")
    sys.exit(1)


# --------------------------------------------------------------------------
# Spec parsing. Each parser refuses to run on a shape it does not recognise
# rather than guess -- a changed spec is an amendment away from parseable.
# --------------------------------------------------------------------------

def read_spec():
    with open(SPEC, encoding="utf-8") as f:
        return f.read()


def section(text, start_pat, end_pat):
    m = re.search(start_pat, text, re.M)
    if not m:
        die("spec shape changed: no match for " + start_pat)
    rest = text[m.end():]
    e = re.search(end_pat, rest, re.M)
    if not e:
        die("spec shape changed: no end for " + start_pat)
    return rest[:e.start()]


def table_rows(block):
    rows = []
    for line in block.splitlines():
        line = line.strip()
        if not line.startswith("|") or re.match(r"^\|[\s\-|]+\|$", line):
            continue
        cells = [c.strip() for c in re.split(r"(?<!\\)\|", line.strip("|"))]
        rows.append(cells)
    return rows[1:]  # drop the header row


def ticks(s):
    return re.findall(r"`([^`]+)`", s)


def parse_reserved(spec):
    blk = section(spec, r"^\*\*1\. Reserved words\*\*", r"^Thirty words\.")
    out = []
    for cells in table_rows(blk):
        for w in ticks(cells[1]):
            out.append((cells[0], w))
    if len(out) != 30:
        die("§8.4 reserved set is %d words, expected 30" % len(out))
    return out


def parse_contextual(spec):
    """Tier-2 bullets, one (label, words) per `label (refs): words` segment.

    A bullet may carry two segments (`byte order (§5.2): ...; FFI (§5.3): ...`),
    and trailing prose after an em dash is commentary, not vocabulary.
    """
    blk = section(spec, r"^\*\*2\. Contextual keywords\*\*", r"^These are contextual on purpose")
    items, cur = [], None
    for line in blk.splitlines():
        if line.startswith("- "):
            cur = [line[2:]]
            items.append(cur)
        elif cur is not None and line.startswith("  "):
            cur.append(line.strip())
    out = []
    for it in items:
        text = " ".join(it)
        text = text.split(" — ")[0]
        for seg in re.split(r";\s+(?=[A-Za-z])", text):
            m = re.match(r"^(.*?)\s*(?:\(§[^)]*\))?:\s+(.*)$", seg)
            if not m:
                die("§8.4 tier-2 bullet shape changed: " + seg)
            label = m.group(1).strip()
            body = re.sub(r"\([^)]*\)", "", m.group(2))
            words = [w for w in ticks(body) if re.fullmatch(r"[a-z_]+", w)]
            out.append((label, words))
    return out


def parse_atoms(spec):
    blk = section(spec, r"^\*\*3\. Capability atoms\*\*", r"^### Operators and sigils")
    return [w for w in ticks(blk) if re.fullmatch(r"[A-Za-z]+", w)]


def parse_atom_glosses(spec):
    blk = section(spec, r"^## 4\.6 The capability set", r"^`alloc` appears in roughly")
    glosses = {}
    line = blk.strip().splitlines()[0]
    for m in re.finditer(r"`([A-Za-z]+)`(?: \(([^)]*)\))?", line):
        glosses[m.group(1)] = m.group(2)
    glosses["Mundus"] = "the root capability, passed to `initium` (§4.7)"
    if glosses.get("alloc") is None:
        glosses["alloc"] = "allocation; arenas (§4.5, §6.3)"
    return glosses


def parse_operators(spec):
    blk = section(spec, r"^### Operators and sigils", r"^The second half of that table")
    out = []
    for cells in table_rows(blk):
        toks = [t.replace("\\|", "|") for t in ticks(cells[0])]
        out.append((toks, re.sub(r"\s+", " ", cells[1].replace("\\|", "|")), cells[2]))
    return out


def parse_codes(spec):
    blk = section(spec, r"^# 13\. Error registry", r"^The `02xx` range is deliberately coarse")
    out = []
    for cells in table_rows(blk):
        code = cells[0].strip("`")
        if not re.fullmatch(r"EXS-E\d{4}", code):
            die("§13 row is not a code: " + cells[0])
        out.append((code, cells[1]))
    return out


def parse_morphemes(spec):
    roots = []
    for cells in table_rows(section(spec, r"^## 3\.3 Roots", r"^Greek roots cover")):
        roots.append({"root": cells[0].strip("`").rstrip("-"),
                      "supine": cells[1].strip("`").rstrip("-"),
                      "gloss": cells[2]})
    suffixes = []
    for cells in table_rows(section(spec, r"^## 3\.4 Suffixes carry type contracts", r"^A name whose suffix")):
        suffixes.append({"suffix": cells[0].strip("`").lstrip("-"), "stem": cells[1],
                         "kind": cells[2], "declares": cells[3].strip("`")})
    prefixes = []
    for cells in table_rows(section(spec, r"^## 3\.5 Prefixes carry signature laws", r"^Violating a prefix law")):
        forms = [p.strip("-") for p in ticks(cells[0])]
        prefixes.append({"prefixes": forms, "gloss": cells[1], "law": cells[2]})
    if len(roots) != 14 or len(suffixes) != 6 or len(prefixes) != 7:
        die("§3.3-§3.5 table sizes changed: %d/%d/%d" % (len(roots), len(suffixes), len(prefixes)))
    return roots, suffixes, prefixes


# --------------------------------------------------------------------------
# Case files: hand-written .exsc, metadata in leading `//!` comment lines.
# --------------------------------------------------------------------------

META_KEYS = {"task", "ask", "expect", "stdout", "refs", "explain", "primary", "fixed-expect",
             "fixed-stdout", "fix-note"}
FIXED_MARK = "//@ fixed"


def parse_case(path):
    with open(path, encoding="utf-8", newline="") as f:
        raw = f.read()
    if "\r" in raw:
        die(path + ": CR in source (§8.1)")
    lines = raw.split("\n")
    meta, key, i = {}, None, 0
    while i < len(lines) and lines[i].startswith("//!"):
        body = lines[i][3:]
        m = re.match(r"^ ([a-z\-]+):\s?(.*)$", body)
        if m and m.group(1) in META_KEYS:
            key = m.group(1)
            meta[key] = m.group(2)
        elif body.startswith("   ") and key:
            # continuation: newline-joined, so a fenced block in `ask` keeps its lines
            meta[key] += "\n" + body[3:]
        else:
            die("%s:%d: bad meta line %r" % (path, i + 1, lines[i]))
        i += 1
    code = "\n".join(lines[i:]).strip("\n") + "\n"
    broken, fixed = code, None
    if FIXED_MARK in code:
        broken, fixed = code.split(FIXED_MARK + "\n", 1)
        broken = broken.rstrip("\n") + "\n"
        fixed = fixed.strip("\n") + "\n"
    for k in ("task", "ask", "expect", "explain"):
        if k not in meta:
            die("%s: missing meta key %s" % (path, k))
    return {"id": os.path.basename(path)[:-5], "meta": meta, "code": broken, "fixed": fixed}


# --------------------------------------------------------------------------
# Running the compiler. A case that exsc disagrees with stops the build.
# --------------------------------------------------------------------------

def find_fasmg():
    p = shutil.which("fasmg")
    if p:
        return p
    die("fasmg not on PATH (it is only needed to RUN cases that declare exit=)")


class Verifier:
    def __init__(self):
        if not os.access(EXSC, os.X_OK):
            die("build/exsc not built -- run `make` first (it needs fasmg)")
        self.work = tempfile.mkdtemp(prefix="gen-finetune-")
        self.env = dict(os.environ)
        self.env.setdefault("INCLUDE", os.path.join(ROOT, "vendor", "fasmg-x86"))

    def close(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def diagnostics(self, name, source):
        path = os.path.join(self.work, name + ".exsc")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(source)
        r = subprocess.run([EXSC, "aedifica", "--hospes", HOSPES, "--diagnostica", "json", path],
                           capture_output=True, text=True, timeout=60)
        diags = []
        # exsc writes its JSON Lines to stderr; read both so that a change of
        # stream cannot turn "rejected" into "no diagnostics".
        for line in (r.stdout + "\n" + r.stderr).splitlines():
            line = line.strip()
            if line.startswith("{"):
                d = json.loads(line)
                diags.append({"code": d["code"], "line": d["line"], "col": d["col"],
                              "message": d["message"], "note": d.get("note"),
                              "snippet": d.get("snippet"), "suggestion": d.get("suggestion"),
                              "fix": d.get("fix")})
        return r.returncode, diags

    def run(self, name, source):
        """Build, assemble and execute. Returns (returncode, stdout, stderr, error)."""
        path = os.path.join(self.work, name + ".exsc")
        asm = os.path.join(self.work, name + ".asm")
        binp = os.path.join(self.work, name + ".bin")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(source)
        r = subprocess.run([EXSC, "aedifica", "--hospes", HOSPES, path, "-o", asm],
                           capture_output=True, text=True, timeout=60)
        if r.returncode != 0:
            return None, None, None, "exsc exit %d: %s" % (r.returncode, r.stdout + r.stderr)
        r = subprocess.run([find_fasmg(), asm, binp], capture_output=True, text=True,
                           timeout=120, env=self.env)
        if r.returncode != 0:
            return None, None, None, "fasmg failed: " + r.stdout + r.stderr
        os.chmod(binp, 0o755)
        r = subprocess.run([binp], capture_output=True, timeout=20, stdin=subprocess.DEVNULL)
        return r.returncode, r.stdout.decode("utf-8"), r.stderr.decode("utf-8"), None

    def judge(self, cid, label, source, expect, stdout):
        """Check `source` against `expect`; return (evidence, diagnostics)."""
        if expect == "ok":
            rc, diags = self.diagnostics(cid + label, source)
            if rc != 0 or diags:
                die("%s%s: expected ok, exsc exit %d, codes %s" % (cid, label, rc, [d["code"] for d in diags]))
            return "exsc-check", []
        m = re.fullmatch(r"exit=(\d+)", expect)
        if m:
            rc, out, _, err = self.run(cid + label, source)
            if err:
                die("%s%s: %s" % (cid, label, err))
            if rc != int(m.group(1)):
                die("%s%s: expected exit=%s, ran exit=%d" % (cid, label, m.group(1), rc))
            want_out = "" if stdout is None else stdout.replace("\\n", "\n")
            if out != want_out:
                die("%s%s: stdout %r != declared %r" % (cid, label, out, want_out))
            return "exsc-run", []
        m = re.fullmatch(r"abort=(\d+)", expect)
        if m:
            # §6.6: one observable shape -- `abortus N` on fd 2, then SIGILL.
            rc, out, errtxt, err = self.run(cid + label, source)
            if err:
                die("%s%s: %s" % (cid, label, err))
            if rc != -4 or ("abortus %s" % m.group(1)) not in errtxt:
                die("%s%s: expected abortus %s + SIGILL, got rc=%s stderr=%r" % (
                    cid, label, m.group(1), rc, errtxt))
            return "exsc-run", []
        want = [c.strip() for c in expect.split(",")]
        for c in want:
            if not re.fullmatch(r"EXS-E\d{4}", c):
                die("%s: bad expect %r" % (cid, expect))
        rc, diags = self.diagnostics(cid + label, source)
        got = []
        for d in diags:
            if d["code"] not in got:
                got.append(d["code"])
        if rc == 0 or sorted(got) != sorted(want):
            die("%s%s: expected exactly %s, exsc gave %s (exit %d)" % (cid, label, want, got, rc))
        return "exsc-diagnostic", diags


# --------------------------------------------------------------------------
# Record construction
# --------------------------------------------------------------------------

def nfc(s):
    return unicodedata.normalize("NFC", s)


def fence(code):
    return "```exsecutor\n" + code.rstrip("\n") + "\n```"


def prose_codes(codes_table, codes):
    return ", ".join("`%s` (%s)" % (c, codes_table[c]) for c in codes)


def describe_edit(source, d):
    """Describe the edit exsc itself attached to a diagnostic, from its JSON.

    `fix` is the machine-applicable payload (§8.3); `suggestion` is a hint. The
    text is escaped in the rendering (`\\n` for a newline), so it is unescaped
    only for APPLYING, and the description quotes it as exsc printed it.
    Returns (label, description, edit) or None.
    """
    edit, label = d.get("fix"), "machine-applicable fix"
    if not edit:
        edit, label = d.get("suggestion"), "suggested edit"
    if not edit:
        return None
    raw = source.encode("utf-8")
    gone = raw[edit["start"]:edit["start"] + edit["len"]].decode("utf-8", "replace")
    text = (edit.get("text") or "").replace("\\n", " ").strip()
    if edit["kind"] == "insert":
        desc = "insert `%s`" % text
    elif edit["kind"] == "replace":
        desc = "replace `%s` with `%s`" % (gone, text)
    elif edit["kind"] == "delete":
        desc = "delete `%s`" % gone
    else:
        return None
    return label, desc, edit


def apply_edit(source, edit):
    raw = source.encode("utf-8")
    text = (edit.get("text") or "").replace("\\n", "\n").encode("utf-8")
    return (raw[:edit["start"]] + text + raw[edit["start"] + edit["len"]:]).decode("utf-8")


def case_records(case, v, codes_table, pool):
    meta, cid = case["meta"], case["id"]
    task = meta["task"]
    refs = [r.strip() for r in meta.get("refs", "").split(",") if r.strip()]
    recs = []
    ev1, diags = v.judge(cid, "", case["code"], meta["expect"], meta.get("stdout"))
    if ev1 in ("exsc-check", "exsc-run"):
        pool.append((cid, case["code"], ev1))
    if task == "write":
        recs.append(rec(cid + ".write", "write", meta["ask"],
                        fence(case["code"]) + "\n\n" + meta["explain"], ev1, refs, cid))
        recs.append(rec(cid + ".explain", "explain",
                        "Explain what this Exsecutor does and why it is written this way:\n\n" + fence(case["code"]),
                        meta["explain"], ev1, refs, cid))
        if meta["expect"].startswith("abort="):
            recs.append(rec(cid + ".trace", "trace",
                            "What happens when this program runs?\n\n" + fence(case["code"]),
                            "It aborts rather than returning a status: it writes `abortus %s` to standard error and "
                            "is killed by `SIGILL`, the one observable shape of a runtime abort (§6.6). I compiled "
                            "it, assembled it with `fasmg` and ran it to confirm.\n\n%s" % (
                                meta["expect"][6:], meta["explain"]), ev1, refs, cid))
        if meta["expect"].startswith("exit="):
            n = meta["expect"][5:]
            tail = "" if "stdout" not in meta else " and writes `%s` to standard output" % meta["stdout"].replace("\\n", "\n")
            recs.append(rec(cid + ".trace", "trace",
                            "What exit status does this program return?\n\n" + fence(case["code"]),
                            "It returns exit status %s%s. I compiled it with `exsc`, assembled it with `fasmg` "
                            "and ran it to confirm.\n\n%s" % (n, tail, meta["explain"]), ev1, refs, cid))
    elif task == "fix":
        if case["fixed"] is None:
            die(cid + ": task fix needs a //@ fixed twin")
        ev2, _ = v.judge(cid, ".fixed", case["fixed"], meta.get("fixed-expect", "ok"), meta.get("fixed-stdout"))
        pool.append((cid + ".fixed", case["fixed"], ev2))
        codes = []
        for d in diags:
            if d["code"] not in codes:
                codes.append(d["code"])
        primary = meta.get("primary", codes[0])
        first = next(d for d in diags if d["code"] == primary)
        lines = ["`exsc` rejects this with %s." % prose_codes(codes_table, codes if len(codes) > 1 else [primary])]
        if len(codes) > 1:
            lines = ["`exsc` rejects this with the primary diagnostic `%s` (%s); the others it reports are "
                     "recovery cascade from the same mistake: %s." % (
                         primary, codes_table[primary],
                         prose_codes(codes_table, [c for c in codes if c != primary]))]
        lines.append("It is reported at line %d, column %d, on `%s`." % (first["line"], first["col"], first["snippet"]) if first.get("snippet") else
                     "It is reported at line %d, column %d." % (first["line"], first["col"]))
        for d in diags:
            got_edit = describe_edit(case["code"], d)
            if got_edit:
                label, desc, edit = got_edit
                line = "The compiler's %s for `%s` is: %s." % (label, d["code"], desc)
                rc2, diags2 = v.diagnostics(cid + ".edit", apply_edit(case["code"], edit))
                if rc2 == 0 and not diags2:
                    line += " Applying that edit alone makes the program compile (checked)."
                lines.append(line)
                break
        lines.append(meta["explain"])
        lines.append("Corrected, and accepted by `exsc`:\n\n" + fence(case["fixed"]))
        recs.append(rec(cid + ".fix", "fix",
                        "This Exsecutor does not compile. What is wrong and how do I fix it?\n\n" + fence(case["code"]),
                        "\n\n".join(lines), "exsc-diagnostic", refs, cid, codes=codes))
        recs.append(rec(cid + ".ask", "write", meta["ask"],
                        fence(case["fixed"]) + "\n\n" + meta["explain"], ev2, refs, cid))
    elif task == "diagnose":
        codes = []
        for d in diags:
            if d["code"] not in codes:
                codes.append(d["code"])
        text = "`exsc` rejects this with %s.\n\n%s" % (prose_codes(codes_table, codes), meta["explain"])
        recs.append(rec(cid + ".diagnose", "diagnose",
                        "Does this Exsecutor compile?\n\n" + fence(case["code"]), text,
                        "exsc-diagnostic", refs, cid, codes=codes))
    elif task == "translate":
        recs.append(rec(cid + ".translate", "translate", meta["ask"],
                        fence(case["code"]) + "\n\n" + meta["explain"], ev1, refs, cid))
    else:
        die("%s: unknown task %s" % (cid, task))
    return recs


def rec(rid, task, user, assistant, evidence, refs, source, codes=None):
    r = {"id": rid, "task": task,
         "messages": [{"role": "user", "content": nfc(user)},
                      {"role": "assistant", "content": nfc(assistant)}],
         "meta": {"evidence": evidence, "spec_refs": refs, "source": source}}
    if codes:
        r["meta"]["codes"] = codes
    return r


def code_tokens(code):
    """Identifier-like tokens of Exsecutor source, outside comments and strings."""
    code = re.sub(r"//[^\n]*", "", code)
    code = re.sub(r'"[^"]*"', '""', code)
    return set(re.findall(r"[A-Za-z_][A-Za-z_0-9]*", code))


def example_records(kw_ed, pool, reserved, contextual):
    """`Show me X in use`, answered with the smallest VERIFIED program that uses X."""
    recs, missing = [], []
    roles = {w: kw_ed["reserved"][w]["role"] for _, w in reserved}
    roles.update({w: kw_ed["contextual"][w]["role"] for _, ws in contextual for w in ws})
    ordered = [w for _, w in reserved] + [w for _, ws in contextual for w in ws]
    toks = [(cid, code, ev, code_tokens(code)) for cid, code, ev in pool]
    for w in ordered:
        hits = [(len(code.splitlines()), cid, code, ev) for cid, code, ev, t in toks if w in t]
        if not hits:
            missing.append(w)
            continue
        _, cid, code, ev = sorted(hits)[0]
        kind = "kw" if w in kw_ed["reserved"] else "ctx"
        recs.append(rec("lex.%s.%s.example" % (kind, w), "lexicon",
                        "Show me a small, working Exsecutor example that uses `%s`." % w,
                        fence(code) + "\n\nHere `%s` appears in use. What it does: %s" % (w, roles[w]),
                        ev, [], cid.split(".")[0]))
    return recs, missing


def as_identifier(v, word):
    """Diagnostic codes exsc gives `firma WORD: u8 = 1;` used as a local name."""
    src = "publica functio f() -> u8 {\n    firma %s: u8 = 1;\n    redde 1;\n}\n" % word
    rc, diags = v.diagnostics("ident-" + word, src)
    return rc, [d["code"] for d in diags]


def lexicon_records(spec, kw_ed, qa, v):
    reserved = parse_reserved(spec)
    contextual = parse_contextual(spec)
    atoms = parse_atoms(spec)
    glosses = parse_atom_glosses(spec)
    ops = parse_operators(spec)
    codes = parse_codes(spec)
    with open(os.path.join(ROOT, "compiler", "x86_64", "diag", "codes.inc"), encoding="utf-8") as f:
        inc = f.read()
    for c, _ in codes:
        if c not in inc:
            die("§13 code %s is not in diag/codes.inc -- the two have drifted (tools/gen-codes.py)" % c)
    roots, suffixes, prefixes = parse_morphemes(spec)

    ed = kw_ed["reserved"]
    if [w for _, w in reserved] != list(ed.keys()):
        die("src/keywords.json reserved set/order differs from §8.4 -- editorial text must follow the spec")
    ced = kw_ed["contextual"]
    ctx_words = [w for _, ws in contextual for w in ws]
    if sorted(ctx_words) != sorted(ced.keys()):
        die("src/keywords.json contextual set differs from §8.4: spec-only=%s json-only=%s" % (
            sorted(set(ctx_words) - set(ced)), sorted(set(ced) - set(ctx_words))))

    # Measured, not asserted: every reserved word is refused as an identifier
    # with EXS-E0220, and a contextual word is accepted as one.
    for _, w in reserved:
        rc, got = as_identifier(v, w)
        if rc == 0 or "EXS-E0220" not in got:
            die("reserved word %r as an identifier: expected EXS-E0220, exsc gave rc=%d %s" % (w, rc, got))
    ctx_ok = {}
    for label, ws in contextual:
        for w in ws:
            rc, got = as_identifier(v, w)
            ctx_ok[w] = (rc == 0 and not got)
            if label.startswith(("`ego`", "`numeri`")):
                # in no compiler table (the CST parses no `ego` block), so "accepted as a
                # name" is true vacuously and is not offered as evidence
                ctx_ok[w] = False
                continue
            if not ctx_ok[w]:
                sys.stderr.write("gen-finetune: note: contextual word %r is NOT accepted as an identifier "
                                 "(rc=%d %s); the claim is dropped from its record\n" % (w, rc, got))

    recs = []
    reserved_names = [w for _, w in reserved]
    list_line = ", ".join("`%s`" % w for w in reserved_names)

    for grp, w in reserved:
        e = ed[w]
        a = ("`%s` is a reserved word in group \"%s\" (§8.4, tier 1). Latin: %s. Role: %s "
             "English analogue: %s. Being reserved, it is never usable as an identifier anywhere: "
             "`firma %s: u8 = 1;` is `EXS-E0220` (checked with `exsc`)." % (w, grp, e["latin"], e["role"], e["english"], w))
        recs.append(rec("lex.kw.%s.meaning" % w, "lexicon", "What does `%s` mean in Exsecutor?" % w, a,
                        "editorial", ["§8.4"] + e.get("refs", []), "keywords.json"))
        if e["english"] and not e["english"].startswith("none"):
            first = e["english"].split(",")[0].split(" ")[0].strip("`")
            recs.append(rec("lex.kw.%s.english" % w, "lexicon",
                            "I know the keyword `%s` from other languages. What is the Exsecutor equivalent?" % first
                            if first.isalpha() else
                            "What Exsecutor word plays the role of %s?" % e["english"],
                            "The Exsecutor word is `%s` (Latin: %s). %s" % (w, e["latin"], e["role"]),
                            "editorial", ["§8.4"] + e.get("refs", []), "keywords.json"))

    recs.append(rec("lex.kw.reserved.list", "lexicon", "List every reserved word in Exsecutor.",
                    "There are exactly thirty, and the set is closed (§8.4):\n\n" +
                    "\n".join("- %s: %s" % (g, ", ".join("`%s`" % x for gg, x in reserved if gg == g))
                              for g in dict.fromkeys(g for g, _ in reserved)) +
                    "\n\nEach one spends root-space permanently, because §3.9.3 collision-checks every proposed "
                    "morpheme root against the reserved set. That is why the set is small and most of the "
                    "vocabulary is contextual.", "spec-table", ["§8.4", "§3.9.3"], "spec §8.4"))

    for label, ws in contextual:
        for w in ws:
            e = ced[w]
            legal = ("so `firma %s: u8 = 1;` is accepted by `exsc` (checked)." % w) if ctx_ok[w] else "so it is not refused as a name by the spec."
            recs.append(rec("lex.ctx.%s" % w, "lexicon", "Is `%s` a reserved word in Exsecutor?" % w,
                            "No. `%s` is a contextual keyword (§8.4, tier 2: %s): %s Everywhere else it is an "
                            "ordinary identifier, %s Latin/gloss: %s.%s" % (
                                w, label, e["role"], legal, e["latin"],
                                " `[UNTESTED]`: the CST parses no `ego` block yet, so this word's contextual "
                                "status in an `ego` file is the grammar's claim and nothing has measured it (§8.4)."
                                if label.startswith(("`ego`", "`numeri`")) else ""),
                            "exsc-check" if ctx_ok[w] else "editorial", ["§8.4"], "keywords.json"))
    recs.append(rec("lex.ctx.principle", "lexicon", "Why are `versio`, `numeri` and `forma` treated so differently?",
                    "`forma` is one of the thirty reserved words, so it can never be an identifier. `versio` and "
                    "`numeri` are contextual: meaningful only in a specific position (the `ego` file, §10.1 / §5.4) "
                    "and ordinary identifiers elsewhere. §8.4 keeps them contextual on purpose: they are good Latin "
                    "words and good variable names, and reserving them globally would be hostile in a language whose "
                    "identifiers are Latin, and would spend root-space for nothing. A real consequence recorded in "
                    "the spec: `discerne forma` was the first draft of a §8.5 example and is `EXS-E0220`.",
                    "spec-text", ["§8.4", "§8.5"], "spec §8.4"))

    for t in ("if", "else", "while", "for", "return", "true", "false", "null", "let", "fn", "struct"):
        recs.append(rec("lex.not.%s" % t, "lexicon", "Is `%s` a keyword in Exsecutor?" % t,
                        "No. `%s` is not an Exsecutor keyword, so it is an ordinary identifier: you may declare a "
                        "name spelled `%s`, and until you do, using it is `EXS-E0301` (name does not resolve). The "
                        "reserved set is the thirty words of §8.4, Latin apart from `dyn`; a few contextual words "
                        "(`lt le gt ge eq ne`, `abi`) are English abbreviations. In the keyword's own position the "
                        "first diagnostic is usually `EXS-E0201`, not `EXS-E0301`: `if c {` is not parsed as a "
                        "condition. The Exsecutor spelling of the common words is: `if` -> "
                        "`si`, `else if` -> `sin`, `else` -> `aliter`, `while` -> `dum` (with a `terminus` bound), "
                        "`for` -> `per` or `quisque`, `return` -> `redde`, `let`/`const` -> `firma`, `let mut` -> "
                        "`mutabilis`, `fn` -> `functio`, `struct` -> `structura`. There is no boolean literal "
                        "(measured with `exsc`: `redde true;` is `EXS-E0301`); a comparison yields the one-bit "
                        "type `u1`, which the spec does not state outright, so treat `u1` as 1 and 0 by "
                        "observation." % (t, t),
                        "spec-text", ["§8.4"], "spec §8.4"))

    for toks, meaning, first in ops:
        shown = ", ".join("`%s`" % t for t in toks)
        recs.append(rec("lex.op.%s" % hashlib.sha256("\0".join(toks).encode()).hexdigest()[:8], "lexicon",
                        "What does %s mean in Exsecutor?" % shown,
                        "%s: %s (§8.4 \"Operators and sigils\"; first shown in %s)." % (shown, meaning, first),
                        "spec-table", ["§8.4"], "spec §8.4"))

    for sym, word, why in kw_ed["operator_words"]:
        recs.append(rec("lex.opw.%s" % word, "lexicon",
                        "How do I write `%s` in Exsecutor?" % sym,
                        "Use the contextual word `%s`. %s" % (word, why),
                        "spec-text", ["§8.4", "§8.6"], "keywords.json"))

    for a in atoms:
        g = glosses.get(a)
        recs.append(rec("lex.atom.%s" % a, "lexicon", "What is the capability `%s`?" % a,
                        "`%s` is a capability atom (§4.6, §8.4 tier 3)%s. Capability atoms are identifiers in the "
                        "capability namespace, not reserved words. Capability types are unforgeable: no literal, no "
                        "cast, no default. The only root is the `Mundus` passed to `initium`; every other capability "
                        "is to be derived from it explicitly (§4.1). `[OPEN]`: only `m.ambitus()` and "
                        "`m.archivum()` are derivable today; the other atoms are declared and not yet "
                        "derivable (§4.7)." % (a, " — " + g if g else ""),
                        "spec-table", ["§4.6", "§8.4"], "spec §4.6"))
    recs.append(rec("lex.atom.list", "lexicon", "List Exsecutor's capability atoms.",
                    "Eleven (§4.6): " + ", ".join("`%s`%s" % (a, " (%s)" % glosses[a] if glosses.get(a) else "")
                                                  for a in atoms) + ". The spec says `alloc` appears in roughly 70% of "
                    "non-kernel `poscit` clauses (`[UNTESTED]`, asserted without a measurement) and designs the "
                    "audit view to suppress it by default (§4.6, §10.3; `[OPEN]` since `exsc ego` is a stub).",
                    "spec-table", ["§4.6"], "spec §4.6"))

    # §13's closing paragraph: registered codes with no raise site, and the lexicon
    # codes that exist behind a call the checker does not make.
    no_raise = {"EXS-E0105", "EXS-E0332", "EXS-E0701"} | {c for c, _ in codes if c.startswith("EXS-E08")}
    not_called = {"EXS-E0601", "EXS-E0602", "EXS-E0603", "EXS-E0610"}
    for code, meaning in codes:
        live = (" `[OPEN]`: registered, with no raise site in the compiler today (§13)." if code in no_raise else
                " `[OPEN]`: the lexicon pass raises it but the driver does not call that pass (§3.3, §13)."
                if code in not_called else "")
        recs.append(rec("lex.err.%s.meaning" % code, "lexicon", "What does the diagnostic `%s` mean?" % code,
                        "`%s` means: %s. Codes are permanent and tools match codes, never English prose (§8.3); "
                        "the wording of the message may change, the code never will.%s" % (code, meaning, live),
                        "spec-table", ["§13", "§8.3"], "spec §13"))
    for code, meaning in codes:
        recs.append(rec("lex.err.%s.reverse" % code, "lexicon",
                        "Which Exsecutor error code is registered for: %s?" % meaning,
                        "`%s` (§13)." % code, "spec-table", ["§13"], "spec §13"))
    recs.append(rec("lex.err.no-invent", "lexicon", "Can I make up a new EXS-E error code for my check?",
                    "No. §13 is the only registry and codes are permanent: never invent one and never renumber "
                    "one. A change that needs a new code is a spec amendment to §13 first, then a regeneration of "
                    "`compiler/x86_64/diag/codes.inc` by `tools/gen-codes.py`.",
                    "spec-text", ["§13", "§8.3"], "CLAUDE.md"))

    # ---- morphology (§3). [UNTESTED]: the lexicon pass is built and not enabled.
    caveat = ("This is derived mechanically from §3.3-§3.5's tables, which the spec itself says are illustrative "
              "and not the morpheme table (`lexicon.norma` does not exist yet). The checker's lexicon pass "
              "(`EXS-E0601`-`EXS-E0603`, `EXS-E0610`) is built and tested against hand-built trees and is not "
              "enabled in the driver, so no compiler run backs this. `[UNTESTED]`")
    recs.append(rec("lex.morph.rule", "lexicon", "What is the lexicon rule for public names in Exsecutor?",
                    "Every public name must decompose into prefixes + root + suffix drawn from a declared, "
                    "versioned morpheme table, and the compiler checks the decomposition against the declared "
                    "type (§3.1). Locals, private functions and struct fields are free-form. A public name may "
                    "carry one qualifier after `_` (`plica_unicode`, `imprime_gutenbergio`); only the part before "
                    "the `_` is decomposed, and the qualifier is a single word — a Latin ablative or a proper "
                    "noun — and is not checked. Composition depth is two affixes at most (§3.8). Keywords are "
                    "Latin for a different reason: ADR 0005 made the lexicon an identity commitment; §3 does not "
                    "govern keywords. `[OPEN]`: this rule is not enforced today, because the lexicon pass is "
                    "built and not called by the driver (§3.3).", "spec-text", ["§3.1", "§3.8", "§8.4"], "spec §3"))
    recs.append(rec("lex.morph.stems", "lexicon", "Why do Latin roots in Exsecutor have two stems?",
                    "Latin verbs carry a present stem and a supine stem, and affixes attach to one or the other: "
                    "the agent of `leg-` is `lector`, not `lecttor`. Every root in the morpheme table therefore "
                    "declares both, e.g. `leg-` / `lect-` (read), `scrib-` / `script-` (write). Suffixes name "
                    "which stem they take (§3.3, §3.4).", "spec-text", ["§3.3", "§3.4"], "spec §3.3"))
    for r in roots:
        fam = []
        for s in suffixes:
            stem = r["root"] if s["stem"] == "present" else r["supine"]
            fam.append((stem + s["suffix"], s))
        lines = ["- `%s` — %s `%s`, takes the %s stem" % (n, s["kind"], s["declares"], s["stem"])
                 for n, s in fam if s["suffix"] != "e"]
        imp = "`lege`" if r["root"] == "leg" else "not given by the spec's table"
        recs.append(rec("lex.morph.root.%s" % r["root"], "lexicon",
                        "Derive the family of names from the Exsecutor root `%s-`." % r["root"],
                        "Root `%s-` (supine `%s-`), \"%s\" (§3.3). Applying §3.4's suffixes to the right stem:\n\n%s\n\n"
                        "The `-e` action form is a morphological category, not a spelling — the imperative of "
                        "this root is %s, since Latin realises it differently by conjugation (`-e` in the third, "
                        "`-a` in the first: `lege`, `plica`, `saluta`). %s" % (
                            r["root"], r["supine"], r["gloss"], "\n".join(lines), imp, caveat),
                        "spec-table", ["§3.3", "§3.4", "§3.7"], "spec §3.3-§3.4"))
    for s in suffixes:
        recs.append(rec("lex.morph.suffix.%s" % s["suffix"], "lexicon",
                        "What does the Exsecutor suffix `-%s` mean and what must it declare?" % s["suffix"],
                        "`-%s` takes the %s stem and marks %s; a name carrying it must be declared as `%s` "
                        "(§3.4). A name whose suffix disagrees with its declaration is `EXS-E0602` — a "
                        "well-formedness condition relating a name to a type, not a style lint." % (
                            s["suffix"], s["stem"], s["kind"], s["declares"]),
                        "spec-table", ["§3.4"], "spec §3.4"))
    for p in prefixes:
        names = " / ".join("`%s-`" % x for x in p["prefixes"])
        recs.append(rec("lex.morph.prefix.%s" % p["prefixes"][0], "lexicon",
                        "What does the Exsecutor prefix %s mean?" % names,
                        "%s means \"%s\" and carries this signature law: %s (§3.5). Violating a prefix law is "
                        "`EXS-E0603`. Prefix laws constrain `functio` signatures only; on a `structura`, `typus` "
                        "or `interfacies` a prefix is positional and semantic." % (names, p["gloss"], p["law"]),
                        "spec-table", ["§3.5"], "spec §3.5"))
    recs.append(rec("lex.morph.no-assimilation", "lexicon",
                    "Does Exsecutor apply Latin assimilation, so `con-` + `leg-` gives `collega`?",
                    "No, deliberately. Exsecutor concatenates without assimilating: `conlege`, `transscribe`, "
                    "`inlege`. This is wrong Latin on purpose, because assimilation destroys guessability and "
                    "greppability, the two properties the scheme exists to provide (§3.6). A root whose classical "
                    "form is already assimilated, such as `applic-` (`applica`), enters the table as a root and "
                    "not as `ad-` + `plic-`.", "spec-text", ["§3.6"], "spec §3.6"))
    recs.append(rec("lex.morph.family", "lexicon", "Show the family derived from `leg-` and what each name must declare.",
                    "From §3.7, one root plus the table yields the family without lookup:\n\n"
                    "- `lege` — `functio`, read\n- `lector` — `structura`, reader (the prelude's type is `Lector`)\n"
                    "- `legibilis` — `interfacies`, can be read\n- `lectio` — `structura`, a read operation\n"
                    "- `lectus` — `typus`, read result\n- `lectorium` — `structura`, read instrument\n"
                    "- `relege` — `functio`, again read\n\n"
                    "Declaring `lector` as a `functio` is `EXS-E0602` (§14 entry 14). Over-composed names are "
                    "`EXS-E0610`; a name that decomposes over no root is `EXS-E0601`. " + caveat,
                    "spec-text", ["§3.7", "§14"], "spec §3.7"))
    recs.append(rec("lex.morph.open", "lexicon", "Can I rely on the compiler to reject `saluta` or `initium` as unlexicable?",
                    "Not today, and the spec says why. §3.3's tables are illustrative and are not the morpheme "
                    "table: `saluta`, `construe`, `imprime`, `textus` and `grapha` are correct Latin and none "
                    "decomposes over the fourteen listed roots. The lexicon pass is built and not enabled; "
                    "running it against the spec's own examples would reject them. `[OPEN]` until "
                    "`lexicon.norma` exists. Two names in the spec also remain unresolved against §3.4: `nocens` "
                    "(a present participle, which the table has no row for) and `exterior` (ends in `-or`, which "
                    "the table assigns to `structura`). `[OPEN]`",
                    "spec-text", ["§3.3", "§3.4"], "spec §3.3"))
    for i, (q, a, refs) in enumerate(kw_ed["coinage"]):
        recs.append(rec("lex.coin.%02d" % (i + 1), "lexicon", q, a, "spec-text", refs, "spec §3.9"))

    for e in qa:
        recs.append(rec(e["id"], "lexicon" if e["id"].startswith("lex.") else "concept", e["q"], e["a"],
                        e.get("evidence", "spec-text"), e.get("refs", []), e.get("source", "qa.json")))

    structured = {
        "reserved": [{"word": w, "group": g, **{k: ed[w][k] for k in ("latin", "english", "role")}} for g, w in reserved],
        "contextual": [{"label": l, "words": ws} for l, ws in contextual],
        "capability_atoms": [{"atom": a, "gloss": glosses.get(a)} for a in atoms],
        "operators": [{"tokens": t, "meaning": m, "first_shown": f} for t, m, f in ops],
        "operator_words": [{"symbol": s, "word": w} for s, w, _ in kw_ed["operator_words"]],
        "error_codes": [{"code": c, "meaning": m} for c, m in codes],
        "morphemes": {"roots": roots, "suffixes": suffixes, "prefixes": prefixes,
                      "status": "illustrative table, not lexicon.norma; lexicon pass not enabled [UNTESTED]"},
    }
    return recs, structured


# --------------------------------------------------------------------------

def dumps(o):
    return json.dumps(o, ensure_ascii=False, sort_keys=True)


def assert_nfc_lf(label, s):
    if "\r" in s:
        die(label + ": CR in output (§8.1)")
    if unicodedata.normalize("NFC", s) != s:
        die(label + ": not NFC (§8.1)")
    if s.startswith("﻿"):
        die(label + ": BOM (§8.1)")


def is_validation(r):
    """The split is a function of the record's GROUP, never of the record.

    Several records come from one program (write / explain / trace / fix) and
    a keyword's "show me an example" record reuses a case's code. Splitting by
    record id would put the same program in train and in validation and make
    the validation set measure memorisation, so every record carries the group
    it must stay in, and the whole group goes one way.
    """
    return hashlib.sha256(r["meta"]["group"].encode("utf-8")).digest()[0] % VALIDATION_MODULUS == 0


def sha(path_or_bytes):
    if isinstance(path_or_bytes, bytes):
        return hashlib.sha256(path_or_bytes).hexdigest()
    with open(path_or_bytes, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def build(verify):
    spec = read_spec()
    with open(os.path.join(SRC, "keywords.json"), encoding="utf-8") as f:
        kw_ed = json.load(f)
    with open(os.path.join(SRC, "qa.json"), encoding="utf-8") as f:
        qa = json.load(f)
    codes_table = dict(parse_codes(spec))

    case_dir = os.path.join(SRC, "cases")
    names = sorted(n for n in os.listdir(case_dir) if n.endswith(".exsc"))
    cases = [parse_case(os.path.join(case_dir, n)) for n in names]
    ids = [c["id"] for c in cases]
    if len(set(ids)) != len(ids):
        die("duplicate case ids")

    if not verify:
        die("--no-verify: refusing to write code records no compiler has seen")
    v = Verifier()
    pool = []
    try:
        recs, structured = lexicon_records(spec, kw_ed, qa, v)
        for c in cases:
            recs.extend(case_records(c, v, codes_table, pool))
    finally:
        v.close()
    ex, missing = example_records(kw_ed, pool, parse_reserved(spec), parse_contextual(spec))
    recs.extend(ex)
    if missing:
        sys.stderr.write("gen-finetune: note: no verified example uses: %s\n" % ", ".join(missing))

    case_ids = set(ids)
    for r in recs:
        src = r["meta"]["source"]
        if src in case_ids:
            g = src
        else:
            g = re.sub(r"\.(meaning|english|example|reverse)$", "", r["id"])
        r["meta"]["group"] = g

    seen = set()
    for r in recs:
        if r["id"] in seen:
            die("duplicate record id " + r["id"])
        seen.add(r["id"])
        for m in r["messages"]:
            assert_nfc_lf(r["id"], m["content"])
    return recs, structured, cases


def outputs(recs, structured, cases):
    train = [r for r in recs if not is_validation(r)]
    val = [r for r in recs if is_validation(r)]
    files = {
        "all.jsonl": "".join(dumps(r) + "\n" for r in recs),
        "train.jsonl": "".join(dumps(r) + "\n" for r in train),
        "validation.jsonl": "".join(dumps(r) + "\n" for r in val),
        "lexicon.json": json.dumps(structured, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
    }
    by_task, by_ev = {}, {}
    for r in recs:
        by_task[r["task"]] = by_task.get(r["task"], 0) + 1
        by_ev[r["meta"]["evidence"]] = by_ev.get(r["meta"]["evidence"], 0) + 1
    manifest = {
        "records": len(recs), "train": len(train), "validation": len(val),
        "by_task": dict(sorted(by_task.items())), "by_evidence": dict(sorted(by_ev.items())),
        "cases": len(cases),
        "inputs_sha256": {
            "docs/spec/exsecutor-spec-v0.4.md": sha(SPEC),
            "datasets/finetune/src/keywords.json": sha(os.path.join(SRC, "keywords.json")),
            "datasets/finetune/src/qa.json": sha(os.path.join(SRC, "qa.json")),
            "datasets/finetune/src/cases": sha("".join(
                "%s %s\n" % (c["id"], sha(os.path.join(SRC, "cases", c["id"] + ".exsc"))) for c in cases).encode()),
        },
        "outputs_sha256": {k: sha(v.encode("utf-8")) for k, v in files.items()},
    }
    files["manifest.json"] = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="regenerate in memory and diff against the committed files")
    ap.add_argument("--no-verify", action="store_true")
    args = ap.parse_args()
    recs, structured, cases = build(verify=not args.no_verify)
    files = outputs(recs, structured, cases)
    if args.check:
        bad = 0
        for name, content in files.items():
            p = os.path.join(OUT, name)
            have = open(p, encoding="utf-8", newline="").read() if os.path.exists(p) else None
            if have != content:
                sys.stderr.write("gen-finetune: --check: %s differs from a fresh generation\n" % name)
                bad = 1
        if bad:
            sys.exit(1)
        print("gen-finetune: --check: %d records, every output identical to a fresh generation" % len(recs))
        return
    for name, content in files.items():
        with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(content)
    print("gen-finetune: wrote %d records (%d train, %d validation) from %d verified cases" % (
        len(recs), sum(1 for r in recs if not is_validation(r)),
        sum(1 for r in recs if is_validation(r)), len(cases)))


if __name__ == "__main__":
    main()
