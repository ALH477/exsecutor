#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
"""
exsecutor_gendict_check.py -- probe for Sec 15 open problem #5: the
three-way interaction of generics x capability rows x dictionary layout
(Sec 7.1), sharpened by Sec 4.2's amendment that rows travel with
function TYPES.

This is a SEPARATE, self-contained surface parser from
prototypes/capcheck/exsecutor_check.py -- it is not an edit to that
directory (CLAUDE.md: "each agent owns its directory"; capcheck/ has its
own dedicated agent). It duplicates a handful of small helpers
(strip_comments, matching_delim, split_top_level_commas) rather than
importing them, on purpose: these are throwaway probes, not a shared
library, and prototypes/README.md is explicit that nothing here may
become a dependency of anything else.

Extends capcheck's surface grammar with exactly what Sec 15 #5's question
needs and nothing else:
  - interfacies NAME { Member* }        -- trait declarations, whose
    members may carry a declared `poscit` ceiling (Sec 4.4's row, but
    Sec 4.4 states the exceeds-check only for `dyn` construction; this
    probe tests whether an analogous ceiling holds, or is even checked,
    for STATIC/generic dispatch).
  - interfacies NAME in TYPE poscit ROW { FunctionDecl* }   -- impl
    blocks. ROW is the impl's declared mark (Sec 4.4's word).
  - functio NAME<T: Trait>(...) -> ... [poscit ROW] { ... }  -- generic
    functions, one type parameter bound to one trait (enough to write
    every case below; multiple bounds are not needed here).
  - EXPR.METHOD(args) inside a generic body, where EXPR is a parameter
    of the generic's own bound type T -- this is the "dictionary call":
    there is no concrete impl in sight, only the trait's declared
    ceiling for that member.
  - EXPR sicut dyn TRAIT poscit {ROW} -- Sec 4.4's cast, generalized to
    an identifier operand (struct literals are Sec 8.6 [OPEN], so a real
    program reaches this only through a parameter, never a literal).

See README.md for the questions this answers and SYNTAX-PROPOSAL.md-style
disclosure of what is genuine Sec 8.6 grammar versus incidental filler
(`self` untyped, bare nominal types with no `structura` declaration) --
both already established ad hoc precedent in prototypes/capcheck/'s own
case files, not new invention here.
"""

import re
import sys
from dataclasses import dataclass, field

CAPABILITY_ATOMS = {
    "alloc", "sermo", "horologium", "archivum",
    "rete", "fortuna", "ambitus", "Filum", "Crudum",
}
KEYWORDS = {"functio", "poscit", "sicut", "redde", "sub", "structura",
            "interfacies", "in", "publica", "mutabilis", "dyn", "self"}


class CheckerError(Exception):
    pass


@dataclass
class Func:
    name: str
    public: bool
    params: list          # [(name, type_str), ...]
    generics: dict         # {type_param_name: trait_name}
    poscit_entries: list   # [('atom', 'rete'), ('sicut', 'f'), ...]
    body_text: str
    line: int

    def declared_atoms(self):
        return {v for k, v in self.poscit_entries if k == "atom"}

    def sicut_entries(self):
        return [v for k, v in self.poscit_entries if k == "sicut"]

    def param_atoms(self):
        return {t for _, t in self.params if t in CAPABILITY_ATOMS}


@dataclass
class Impl:
    trait: str
    type: str
    mark: set
    methods: dict   # {method_name: (params, body_text)}
    line: int


# ---------------------------------------------------------------- lexing --

def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def matching_delim(text, open_idx, open_ch, close_ch):
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


def _depth0_poscit(s):
    """Index of the LAST `poscit` at paren/brace depth 0, or -1. See
    capcheck's identical function for why this must skip a row that
    belongs to a function-typed result rather than the declaration."""
    depth, last = 0, -1
    i = 0
    while i < len(s):
        ch = s[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif depth == 0 and s.startswith("poscit", i) and (
                i == 0 or not s[i - 1].isalnum()):
            last = i
            i += 6
            continue
        i += 1
    return last


def find_body_open(text, start):
    """First '{' at or after `start` that is NOT a TypeRow's brace (a
    TypeRow is always immediately preceded by `poscit`, Sec 8.6) -- so a
    declaration whose result type is `dyn X poscit {rete}` or
    `functio(...) -> Y poscit {rete}` does not mistake that row's open
    brace for the body's."""
    i = start
    while True:
        brace = text.index("{", i)
        j = brace - 1
        while j >= 0 and text[j] in " \t\n":
            j -= 1
        k = j
        while k >= 0 and (text[k].isalnum() or text[k] == "_"):
            k -= 1
        word = text[k + 1:j + 1]
        if word == "poscit":
            i = matching_delim(text, brace, "{", "}") + 1
            continue
        return brace


def type_row(type_str):
    """The capability row carried BY A TYPE (Sec 4.2 as amended)."""
    if not type_str:
        return set()
    i = _depth0_poscit(type_str)
    if i < 0:
        return set()
    rest = type_str[i + 6:].strip()
    if rest.startswith("{"):
        close = matching_delim(rest, 0, "{", "}")
        rest = rest[1:close]
    return {a for a in (x.strip() for x in rest.split(",")) if a in CAPABILITY_ATOMS}


CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
GENERIC_CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*<[^<>()]*>\s*\(")
METHOD_CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\.\s*([A-Za-z_]\w*)\s*\(")
DYN_CAST_RE = re.compile(
    r"\b([A-Za-z_]\w*)\s+sicut\s+dyn\s+(\w+)\s+poscit\s+\{([^}]*)\}")


def strip_generic_call_type_args(text):
    """`total<Left>(a, b)` -> `total(a, b)`. A DIRECT call to a generic
    function needs no type-argument resolution at all: the callee's own
    declared row is fixed and uniform (that is the entire content of
    Sec 7.1's "compiled once"), so what the call site demands is just the
    callee's declared row, exactly as for a non-generic call. Only a
    METHOD call reached THROUGH a bound type parameter (handled
    separately, below) needs to know anything about the type argument."""
    return GENERIC_CALL_RE.sub(lambda m: m.group(1) + "(", text)


def find_calls(text):
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


def find_method_calls(text):
    """`recv.method(args)` occurrences: (receiver_name, method_name)."""
    out = []
    for m in METHOD_CALL_RE.finditer(text):
        out.append((m.group(1), m.group(2)))
    return out


def find_dyn_casts(text):
    """`operand sicut dyn Trait poscit {row}` -> (operand, trait, {atoms})."""
    out = []
    for m in DYN_CAST_RE.finditer(text):
        bound = {a for a in split_top_level_commas(m.group(3)) if a in CAPABILITY_ATOMS}
        out.append((m.group(1), m.group(2), bound))
    return out


def sub_atoms_of(body_text):
    return {m.group(1) for m in re.finditer(r"\bsub\s+(\w+)\s*=", body_text)
            if m.group(1) in CAPABILITY_ATOMS}


# ---------------------------------------------------- interfaces / impls --

INTERFACE_HEAD_RE = re.compile(r"\binterfacies\s+(\w+)\s*(?:<[^>]*>)?\s*")


def parse_trait_members(body_block):
    """Signature-only members (Sec 8.6: interfacies bodies are
    separator-free, no trailing `;`). Returns {method_name: {atoms}} --
    the declared row CEILING for that member."""
    members = {}
    starts = [m.start() for m in re.finditer(r"\bfunctio\s+\w+\s*\(", body_block)]
    for i, s in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else len(body_block)
        chunk = body_block[s:end]
        nm = re.match(r"functio\s+(\w+)\s*\(", chunk)
        name = nm.group(1)
        popen = chunk.index("(")
        pclose = matching_delim(chunk, popen, "(", ")")
        tail = chunk[pclose + 1:]
        atoms = set()
        pi = _depth0_poscit(tail)
        if pi >= 0:
            rest = tail[pi + 6:]
            atoms = {a for a in split_top_level_commas(rest) if a in CAPABILITY_ATOMS}
        members[name] = atoms
    return members


def parse_impl_methods(body_block):
    methods = {}
    for m in re.finditer(r"\bfunctio\s+(\w+)\s*\(", body_block):
        name = m.group(1)
        popen = m.end() - 1
        pclose = matching_delim(body_block, popen, "(", ")")
        hopen = body_block.index("{", pclose)
        hclose = matching_delim(body_block, hopen, "{", "}")
        params_text = body_block[popen + 1:pclose]
        params = []
        for p in split_top_level_commas(params_text):
            pname, _, ptype = p.partition(":")
            params.append((pname.strip(), ptype.strip()))
        body_text = body_block[hopen + 1:hclose]
        methods[name] = (params, body_text)
    return methods


def parse_interfaces_and_impls(text):
    """Returns (traits, impls, redacted_text). redacted_text has every
    interfacies block blanked out (spaces, same length, newlines kept) so
    a later top-level-function pass never double-counts a method inside
    an impl block as a free function."""
    traits = {}
    impls = []
    out = list(text)
    for m in INTERFACE_HEAD_RE.finditer(text):
        name = m.group(1)
        pos = m.end()
        while pos < len(text) and text[pos].isspace():
            pos += 1
        if text.startswith("in", pos) and (
                pos + 2 >= len(text) or not (text[pos + 2].isalnum() or text[pos + 2] == "_")):
            pos += 2
            while text[pos].isspace():
                pos += 1
            tm = re.match(r"\w+", text[pos:])
            typ = tm.group(0)
            pos += tm.end()
            while text[pos].isspace():
                pos += 1
            mark = set()
            if text.startswith("poscit", pos):
                pos += 6
                brace = text.index("{", pos)
                row_text = text[pos:brace]
                mark = {a for a in split_top_level_commas(row_text) if a in CAPABILITY_ATOMS}
                pos = brace
            else:
                pos = text.index("{", pos)
            close = matching_delim(text, pos, "{", "}")
            body_block = text[pos + 1:close]
            line = text.count("\n", 0, m.start()) + 1
            impls.append(Impl(name, typ, mark, parse_impl_methods(body_block), line))
            for i in range(m.start(), close + 1):
                if out[i] != "\n":
                    out[i] = " "
        else:
            brace = text.index("{", pos)
            close = matching_delim(text, brace, "{", "}")
            body_block = text[brace + 1:close]
            traits[name] = parse_trait_members(body_block)
            for i in range(m.start(), close + 1):
                if out[i] != "\n":
                    out[i] = " "
    return traits, impls, "".join(out)


# ------------------------------------------------------------- top-level --

FUNC_HEADER_RE = re.compile(
    r"(publica\s+)?functio\s+(\w+)\s*(?:<([^>]*)>)?\s*\(")


def parse_generic_params(text):
    """`T: Summable, U` -> {'T': 'Summable', 'U': None}."""
    out = {}
    for entry in split_top_level_commas(text):
        pname, _, bound = entry.partition(":")
        out[pname.strip()] = bound.strip() or None
    return out


def parse_funcs(text):
    funcs = {}
    for m in FUNC_HEADER_RE.finditer(text):
        public = bool(m.group(1))
        name = m.group(2)
        generics = parse_generic_params(m.group(3)) if m.group(3) else {}
        popen = m.end() - 1
        pclose = matching_delim(text, popen, "(", ")")
        params_text = text[popen + 1:pclose]
        hopen = find_body_open(text, pclose)
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
        funcs[name] = Func(name, public, params, generics, poscit_entries, body_text, line)
    return funcs


# --------------------------------------------------------- effective row --

def call_contribution(name, args, funcs):
    if name not in funcs:
        return set()
    f = funcs[name]
    result = set(f.declared_atoms()) | f.param_atoms()
    for pname in f.sicut_entries():
        idx = next((i for i, (pn, _) in enumerate(f.params) if pn == pname), None)
        if idx is not None and idx < len(args):
            arg = args[idx].strip()
            if re.fullmatch(r"[A-Za-z_]\w*", arg) and arg in funcs:
                result |= set(funcs[arg].declared_atoms())
    return result


def method_call_contribution(recv, method, f, traits):
    """The row a DICTIONARY call demands. `recv` must be one of f's own
    parameters whose declared type is one of f's OWN generic type
    parameters -- otherwise this mechanism does not apply (see README:
    concrete-typed method calls outside an impl body are out of scope for
    this probe). Sec 4.2's revised rule resolves a row from the actual
    argument's TYPE; here the "actual argument" is never known -- T is
    abstract at the generic's own single compilation (Sec 7.1) -- so the
    only type-level fact available is the trait's own declared ceiling
    for that member."""
    ptype = dict(f.params).get(recv)
    if ptype is None or ptype not in f.generics:
        return set()
    trait_name = f.generics[ptype]
    if trait_name not in traits:
        return set()
    return set(traits[trait_name].get(method, set()))


def effective_row(f, funcs, traits):
    total = set(sub_atoms_of(f.body_text))
    polymorphic = set(f.sicut_entries())
    for pname, ptype in f.params:
        if pname not in polymorphic:
            total |= type_row(ptype)
    body = strip_generic_call_type_args(f.body_text)
    for name, args in find_calls(body):
        if name in funcs:
            total |= call_contribution(name, args, funcs)
    for recv, method in find_method_calls(body):
        total |= method_call_contribution(recv, method, f, traits)
    return total


def impl_effective_row(impl, funcs):
    total = set()
    for _, (params, body) in impl.methods.items():
        total |= sub_atoms_of(body)
        for name, args in find_calls(strip_generic_call_type_args(body)):
            if name in funcs:
                total |= call_contribution(name, args, funcs)
    return total


def check_dyn_casts_in_func(f, impls):
    """Resolve `operand sicut dyn Trait poscit {bound}` inside f's body.
    Returns a violation message, or None. When `operand`'s declared type
    is one of f's OWN generic parameters, there is no concrete impl for
    this -- by construction, not by an implementable oversight -- to
    compare against, so no violation can be reported here even if one
    exists at some future instantiation; that silence IS the finding."""
    ptypes = dict(f.params)
    for operand, trait, bound in find_dyn_casts(f.body_text):
        ptype = ptypes.get(operand)
        if ptype is None:
            continue
        if ptype in f.generics:
            continue  # unresolvable: T is abstract here (see README)
        for imp in impls:
            if imp.type == ptype and imp.trait == trait and not (imp.mark <= bound):
                return (f"{f.name}: casting {operand} ({ptype}) to dyn {trait} "
                        f"poscit {{{','.join(sorted(bound))}}} exceeds its mark "
                        f"{sorted(imp.mark)} (line {f.line})")
    return None


def check_static_ceiling(impl, traits):
    """EXPERIMENTAL, not run by default. Sec 4.4 states the mark-exceeds-
    bound check only for `dyn` CONSTRUCTION. This function is the
    hypothesis that the analogous ceiling -- an impl's mark must not
    exceed the union of the CEILINGS its trait declares for the members
    it implements -- should hold for the STATIC/generic path too, since
    that ceiling is the only thing a generic function can safely assume
    about ANY T bound to that trait. Returns a message or None."""
    ceiling = set()
    members = traits.get(impl.trait, {})
    for name in impl.methods:
        ceiling |= members.get(name, set())
    if not (impl.mark <= ceiling):
        return (f"interfacies {impl.trait} in {impl.type}: mark {sorted(impl.mark)} "
                f"exceeds the trait's declared ceiling {sorted(ceiling)} "
                f"(line {impl.line})")
    return None


# --------------------------------------------------------------- driver --

def check_file(text, strict_static_ceiling=False):
    clean = strip_comments(text)
    traits, impls, redacted = parse_interfaces_and_impls(clean)
    funcs = parse_funcs(redacted)

    for imp in impls:
        eff = impl_effective_row(imp, funcs)
        extra = eff - imp.mark
        if extra:
            return ("reject", "EXS-E0421",
                    f"interfacies {imp.trait} in {imp.type}: body uses "
                    f"{sorted(eff)}, exceeding its own declared mark "
                    f"{sorted(imp.mark)}; undeclared {sorted(extra)} (line {imp.line})")

    if strict_static_ceiling:
        for imp in impls:
            msg = check_static_ceiling(imp, traits)
            if msg:
                return ("reject", "EXS-E0421", msg)

    for f in funcs.values():
        msg = check_dyn_casts_in_func(f, impls)
        if msg:
            return ("reject", "EXS-E0510", msg)

    for f in funcs.values():
        if not (f.public or f.poscit_entries):
            continue
        eff = effective_row(f, funcs, traits)
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
    if "KNOWN-GAP" in raw_text[:400]:
        return "gap"
    if base.startswith("ok_"):
        return "accept"
    if base.startswith("bad_"):
        return "reject"
    return "unknown"


def main():
    argv = sys.argv[1:]
    verbose = False
    strict = False
    while argv and argv[0].startswith("-"):
        if argv[0] == "-v":
            verbose = True
        elif argv[0] == "--strict-static-ceiling":
            strict = True
        else:
            print(f"unknown flag {argv[0]}", file=sys.stderr)
            sys.exit(2)
        argv = argv[1:]

    all_ok = True
    for path in argv:
        with open(path, encoding="utf-8") as fh:
            raw = fh.read()
        try:
            verdict, code, msg = check_file(raw, strict_static_ceiling=strict)
        except CheckerError as e:
            print(f"ERROR {path}: {e}")
            all_ok = False
            continue

        exp = expected_verdict(path, raw)
        if exp == "gap":
            status = "FIXED" if verdict == "reject" else "GAP "
            passed = True
        elif exp == verdict:
            status = "PASS"
            passed = True
        else:
            status = "FAIL"
            passed = False
        all_ok = all_ok and passed

        tail = f"{code}: {msg}" if verdict == "reject" else "no violation found"
        mode = " [strict]" if strict else ""
        print(f"{status} {path}{mode}: {verdict} ({tail})")

    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
