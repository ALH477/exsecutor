#!/usr/bin/env python3
# tests/c/facies/protos.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
#
# Code produced by this compiler is not covered by the GPL --
# see Exception A in LICENSE.EXCEPTION.
# -----------------------------------------------------------------------------
# Compare a HAND-WRITTEN C header with the one `exsc --emitte h` generated
# from the same unit (docs/design/c-backend.md D9), prototype by prototype.
#
#   protos.py HAND.h GENERATED.h
#
# Each header is reduced to its function prototypes, normalized: comments
# dropped, whitespace collapsed, parameter NAMES dropped (`unsigned char *d`
# and `unsigned char *p0` are the same parameter), so what is compared is the
# C type of every parameter and of the result, and the name. Prints one line
# per prototype and exits:
#   0  every prototype in HAND.h is in GENERATED.h, identically;
#   1  one is missing from GENERATED.h, or declared differently there;
#   2  usage, or a header with no prototype at all (a vacuous comparison).
# Prototypes GENERATED.h has and HAND.h does not are reported as `extra`
# and do not fail: they are publica functions the hand-written header chose
# not to offer. "Exactly the same set" is the caller's call: pass --exact.
import re
import sys

PROTO = re.compile(r'^(?P<ret>.*?)(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^()]*)\)$')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def norm_type(t):
    t = re.sub(r'\s+', ' ', t).strip()
    return re.sub(r'\s*\*\s*', ' *', t).strip()


def norm_param(p):
    p = re.sub(r'\s+', ' ', p).strip()
    if p == 'void' or p == '':
        return p
    # drop a trailing identifier that is a parameter name, not a type word
    m = re.match(r'^(.*?[\s*])([A-Za-z_]\w*)$', p)
    if m and m.group(2) not in ('int', 'char', 'long', 'short', 'unsigned',
                                'signed', 'float', 'double', 'void'):
        if not re.fullmatch(r'u?int\d+_t', m.group(2)):
            p = m.group(1)
    return norm_type(p)


def protos(path):
    text = strip_comments(open(path, encoding='utf-8').read())
    out = {}
    # one declaration per `;`-terminated statement outside the preprocessor
    lines = [ln for ln in text.split('\n') if not ln.lstrip().startswith('#')]
    for stmt in ' '.join(lines).split(';'):
        stmt = re.sub(r'\s+', ' ', stmt).strip()
        if not stmt or '(' not in stmt:
            continue
        m = PROTO.match(stmt)
        if not m:
            continue
        name = m.group('name')
        ret = norm_type(m.group('ret'))
        params = [norm_param(p) for p in m.group('params').split(',')]
        out[name] = '%s %s(%s)' % (ret, name, ', '.join(params))
    return out


def main(argv):
    exact = '--exact' in argv
    argv = [a for a in argv if a != '--exact']
    if len(argv) != 3:
        print('usage: protos.py [--exact] HAND.h GENERATED.h', file=sys.stderr)
        return 2
    hand, gen = protos(argv[1]), protos(argv[2])
    if not hand or not gen:
        print('protos: a header with no prototype -- nothing compared', file=sys.stderr)
        return 2
    bad = 0
    for name in hand:
        if name not in gen:
            print('  [FAIL] %s: in the hand-written header, not in the generated one' % hand[name])
            bad = 1
        elif hand[name] != gen[name]:
            print('  [FAIL] %s: hand-written `%s`, generated `%s`' % (name, hand[name], gen[name]))
            bad = 1
        else:
            print('  [ok]   %s: the same in both headers' % hand[name])
    extra = [n for n in gen if n not in hand]
    for name in extra:
        print('  -      extra: %s (publica; the hand-written header does not offer it)' % gen[name])
        if exact:
            bad = 1
    print('  -      %d hand-written prototypes, %d generated, %d extra'
          % (len(hand), len(gen), len(extra)))
    return bad


if __name__ == '__main__':
    sys.exit(main(sys.argv))
