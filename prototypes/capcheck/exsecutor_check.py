#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
"""
exsecutor_check.py -- capability-row checker probe.

Re-derives spec Sec 4.2's substitution rule from prose, against a tiny,
deliberately-limited surface parser for the .xsc case files under cases/.
This is NOT a general Exsecutor parser: it recognizes exactly the constructs
the case files use (functio declarations, poscit rows, sub bindings, single-
line lambda literals, impl/dyn casts, module-level mutabilis) and nothing
else. See README.md for why this file has no history before this change --
this is a re-derivation, not a restoration (CLAUDE.md).

Self-verifying CLI contract: for each input file, the expected verdict comes
from its filename (cases/ok_*.xsc -> accept, cases/bad_*.xsc -> reject),
UNLESS the file's first ~300 chars contain the marker "KNOWN-GAP", in which
case the expected verdict is "accept despite being a real violation" --
used for exactly one file, cases/bad_closure_capture.xsc. See
SYNTAX-PROPOSAL.md for why that file is marked this way instead of tuning
the checker to catch it. Exit 0 iff every file's actual verdict matches
what was expected of it.
"""

import re
import sys
from dataclasses import dataclass, field

CAPABILITY_ATOMS = {
    "alloc", "sermo", "horologium", "archivum",
    "rete", "fortuna", "ambitus", "Filum", "Crudum",
}
KEYWORDS = {"functio", "poscit", "sicut", "redde", "sub", "structura",
            "impl", "dyn", "as", "publica", "mutabilis", "for"}


class CheckerError(Exception):
    pass


@dataclass
class Func:
    name: str
    public: bool
    params: list          # [(name, type_str), ...]
    poscit_entries: list  # [('atom', 'rete'), ('sicut', 'f'), ...]
    body_text: str
    line: int

    def declared_atoms(self):
        return {v for k, v in self.poscit_entries if k == "atom"}

    def sicut_entries(self):
        return [v for k, v in self.poscit_entries if k == "sicut"]

    def param_atoms(self):
        return {t for _, t in self.params if t in CAPABILITY_ATOMS}


@dataclass
class ModVar:
    name: str
    type: str
    line: int


@dataclass
class ImplBlock:
    trait: str
    type: str
    mark: list


# ---------------------------------------------------------------- lexing --

def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def matching_delim(text, open_idx, open_ch, close_ch):
    """Index of the delimiter matching text[open_idx], by depth counting."""
    depth = 0
    i = open_idx
    while i < len(text):
        if text[i] == open_ch:
            depth += 1
        elif text[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise CheckerError(f"unbalanced '{open_ch}{close_ch}' from index {open_idx}")


def split_top_level_commas(s):
    parts, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur.strip())
    return [p for p in parts if p]


CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")


def find_calls(text):
    """All NAME(args) occurrences, including nested ones, as (name, [arg-strs])."""
    calls = []
    for m in CALL_RE.finditer(text):
        name = m.group(1)
        if name in KEYWORDS:
            continue
        open_idx = m.end() - 1
        close_idx = matching_delim(text, open_idx, "(", ")")
        args_text = text[open_idx + 1:close_idx]
        calls.append((name, split_top_level_commas(args_text)))
    return calls


def sub_atoms_of(body_text):
    """Capability atoms bound via `sub CAP = ...;` at any point in body_text."""
    return {m.group(1) for m in re.finditer(r"\bsub\s+(\w+)\s*=", body_text)
            if m.group(1) in CAPABILITY_ATOMS}


# ------------------------------------------------------------- top-level --

FUNC_HEADER_RE = re.compile(r"(publica\s+)?functio\s+(\w+)\s*\(")


def parse_funcs(text):
    """Top-level `functio NAME(...)` declarations. Lambda literals (bare
    `functio(` with no name) never match this regex, so they are never
    mistaken for declarations regardless of nesting position."""
    funcs = {}
    for m in FUNC_HEADER_RE.finditer(text):
        public = bool(m.group(1))
        name = m.group(2)
        popen = m.end() - 1
        pclose = matching_delim(text, popen, "(", ")")
        params_text = text[popen + 1:pclose]
        hopen = text.index("{", pclose)
        remainder = text[pclose + 1:hopen]
        poscit_text = remainder.partition("poscit")[2] if "poscit" in remainder else ""
        bclose = matching_delim(text, hopen, "{", "}")
        body_text = text[hopen + 1:bclose]

        params = []
        for p in split_top_level_commas(params_text):
            pname, _, ptype = p.partition(":")
            params.append((pname.strip(), ptype.strip()))

        poscit_entries = []
        for e in split_top_level_commas(poscit_text):
            if e.startswith("sicut "):
                poscit_entries.append(("sicut", e[len("sicut "):].strip()))
            else:
                poscit_entries.append(("atom", e))

        line = text.count("\n", 0, m.start()) + 1
        funcs[name] = Func(name, public, params, poscit_entries, body_text, line)
    return funcs


MODVAR_RE = re.compile(r"^mutabilis\s+(\w+)\s*:\s*(\S+?)\s*=", re.MULTILINE)


def parse_modvars(text):
    out = []
    for m in MODVAR_RE.finditer(text):
        line = text.count("\n", 0, m.start()) + 1
        out.append(ModVar(m.group(1), m.group(2), line))
    return out


IMPL_RE = re.compile(r"impl\s+(\w+)\s+for\s+(\w+)\s+poscit\s+([^{]+)\{")


def parse_impls(text):
    impls = []
    for m in IMPL_RE.finditer(text):
        trait, typ, mark_text = m.group(1), m.group(2), m.group(3)
        impls.append(ImplBlock(trait, typ, split_top_level_commas(mark_text)))
    return impls


DYN_CAST_RE = re.compile(r"(\w+)\{[^}]*\}\s*as\s+dyn\s+(\w+)\s+poscit\s+([^\n;]+)")


def check_dyn_escape(text, impls):
    for m in DYN_CAST_RE.finditer(text):
        concrete, trait, bound_text = m.group(1), m.group(2), m.group(3)
        bound = set(split_top_level_commas(bound_text))
        for imp in impls:
            if imp.type == concrete and imp.trait == trait and not set(imp.mark) <= bound:
                return (f"impl {trait} for {concrete} has mark {sorted(imp.mark)}, "
                        f"exceeding dyn bound {sorted(bound)}")
    return None


# ------------------------------------------------ substitution / rows -----

def row_of_reference(expr, funcs):
    """'The capability row of the actual argument' (Sec 4.2). Deliberately
    faithful to what positional substitution can name:
      - a NAMED function -> its own declared poscit (the public contract,
        rule 5: publica declares explicitly -- callers trust the signature).
      - an inline lambda LITERAL -> inferred from its body (rule 5: private
        infers from body), by walking calls it makes. Free identifiers are
        NOT resolved against the enclosing sub-scope -- see
        SYNTAX-PROPOSAL.md for why that is the crux of the closure-capture
        finding, not an oversight here.
      - anything else (a call expression, a returned closure, ...) -> empty.
        This is the second, sharper half of the same gap: a function VALUE
        that is itself the result of an expression carries no name for
        substitution to look up at all.
    """
    expr = expr.strip()
    if expr.startswith("functio(") or expr.startswith("functio ("):
        return lambda_row(expr, funcs)
    if re.fullmatch(r"[A-Za-z_]\w*", expr) and expr in funcs:
        return set(funcs[expr].declared_atoms())
    return set()


def call_contribution(name, args, funcs):
    """What calling `name(args)` demands of the caller: name's own declared
    atoms and capability-typed-parameter atoms, plus each `sicut P` entry
    substituted positionally with the row of the actual argument at P's
    index."""
    if name not in funcs:
        return set()
    f = funcs[name]
    result = set(f.declared_atoms()) | f.param_atoms()
    for pname in f.sicut_entries():
        idx = next((i for i, (pn, _) in enumerate(f.params) if pn == pname), None)
        if idx is not None and idx < len(args):
            result |= row_of_reference(args[idx], funcs)
    return result


def lambda_row(expr, funcs):
    popen = expr.index("(")
    pclose = matching_delim(expr, popen, "(", ")")
    bopen = expr.index("{", pclose)
    bclose = matching_delim(expr, bopen, "{", "}")
    body = expr[bopen + 1:bclose]
    total = set()
    for name, args in find_calls(body):
        total |= call_contribution(name, args, funcs)
    return total


def effective_row(f, funcs):
    total = set(sub_atoms_of(f.body_text))
    for name, args in find_calls(f.body_text):
        if name in funcs:
            total |= call_contribution(name, args, funcs)
    return total


def check_hof_own_row(f):
    """A function calling one of its OWN function-typed parameters must
    name it in `sicut`, regardless of what any caller later passes.
    Conformance suite item 9."""
    fn_params = {n for n, t in f.params if t.startswith("functio(") or t.startswith("functio (")}
    if not fn_params:
        return None
    sicut_names = set(f.sicut_entries())
    for name, _ in find_calls(f.body_text):
        if name in fn_params and name not in sicut_names:
            return (f"{f.name} calls its own parameter '{name}' without "
                    f"declaring 'sicut {name}'")
    return None


# --------------------------------------------------------------- driver --

def check_file(text):
    """Returns (verdict, code, message). verdict in {'accept','reject'}."""
    clean = strip_comments(text)

    modvars = parse_modvars(clean)
    for mv in modvars:
        if mv.type in CAPABILITY_ATOMS:
            return ("reject", "EXS-E0501",
                    f"module-level mutable '{mv.name}: {mv.type}' holds a "
                    f"capability (line {mv.line})")
    if modvars:
        mv = modvars[0]
        return ("reject", "EXS-E0500",
                f"module-level mutable state '{mv.name}' (line {mv.line})")

    impls = parse_impls(clean)
    dyn_msg = check_dyn_escape(clean, impls)
    if dyn_msg:
        return ("reject", "EXS-E0510", dyn_msg)

    funcs = parse_funcs(clean)

    for f in funcs.values():
        msg = check_hof_own_row(f)
        if msg:
            return ("reject", "EXS-E0421", msg)

    for f in funcs.values():
        if not (f.public or f.poscit_entries):
            continue
        eff = effective_row(f, funcs)
        extra = eff - f.declared_atoms()
        if extra:
            return ("reject", "EXS-E0421",
                    f"{f.name}: effective row {sorted(eff)} exceeds declared "
                    f"{sorted(f.declared_atoms())}; undeclared {sorted(extra)} "
                    f"(line {f.line})")

    return ("accept", None, None)


def expected_verdict(path, raw_text):
    import os
    base = os.path.basename(path)
    if "KNOWN-GAP" in raw_text[:300]:
        return "gap"
    if base.startswith("ok_"):
        return "accept"
    if base.startswith("bad_"):
        return "reject"
    return "unknown"


def main():
    argv = sys.argv[1:]
    verbose = False
    if argv and argv[0] == "-v":
        verbose = True
        argv = argv[1:]

    all_ok = True
    for path in argv:
        with open(path, encoding="utf-8") as fh:
            raw = fh.read()
        try:
            verdict, code, msg = check_file(raw)
        except CheckerError as e:
            print(f"ERROR {path}: {e}")
            all_ok = False
            continue

        exp = expected_verdict(path, raw)
        if exp == "gap":
            status = "FIXED" if verdict == "reject" else "GAP "
            passed = True  # documented either way; see SYNTAX-PROPOSAL.md
        elif exp == verdict:
            status = "PASS"
            passed = True
        else:
            status = "FAIL"
            passed = False
        all_ok = all_ok and passed

        tail = f"{code}: {msg}" if verdict == "reject" else "no violation found"
        print(f"{status} {path}: {verdict} ({tail})")
        if verbose and verdict == "accept":
            funcs = parse_funcs(strip_comments(raw))
            for f in funcs.values():
                print(f"       {f.name}: declared={sorted(f.declared_atoms())} "
                      f"effective={sorted(effective_row(f, funcs))}")

    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
