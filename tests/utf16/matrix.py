#!/usr/bin/env python3
"""tests/utf16/matrix.py -- every UTF-8 -> UTF-16 library, through exsc, to the oracle.

For each library examples/utf16/<name>.exsc:

  1. `exsc aedifica` compiles contractus.exsc + <name>.exsc + probatio.exsc as
     ONE unit (spec 12), `fasmg` assembles the emitted text, and the result is an
     ordinary static ELF. Nothing here is interpreted: what is run is what exsc
     made.
  2. It is run on the committed fixture (tests/data/utf16_corpus_{L,B}.bin) and
     on the deep corpora tests/utf16/corpus.py generates, and its standard output
     is compared BYTE FOR BYTE with what oracle.py computes from Python's codec.
     Agreement with the oracle on every corpus is agreement with every other
     library: they are all held to the one answer.
  3. With --c, the same unit is emitted as C (`--emitte c`) and built by gcc and
     clang at -O0 and -O2 under UBSan, linked with tests/c/exsrt_shim.c, exactly
     as tests/run.sh's differential phase builds it, and run on the same corpora.
  4. With --audit, tools/syscall-audit.sh checks each binary's syscall sites
     against what {Mundus, ambitus} admit: no socket, nothing off the closed set.
  5. With --mutants N, N literal mutations of each library are built and run on
     the fixture, and a mutant the fixture does not kill is reported. A corpus
     that cannot tell a broken library from a sound one is not a test.

Usage (fasmg on PATH, or FASMG=/path/to/fasmg):

  python3 tests/utf16/matrix.py                      fixture tier, all libraries
  python3 tests/utf16/matrix.py --deep               + the deep corpora
  python3 tests/utf16/matrix.py --deep --c --audit   everything
  python3 tests/utf16/matrix.py --libs ramus,tabula  a subset
  python3 tests/utf16/matrix.py --mutants 40         the corpus, mutation-tested

Exit status 0 only if every library passed every check it was asked to run.
"""
import argparse
import concurrent.futures as cf
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import corpus  # noqa: E402
import oracle  # noqa: E402

LIBDIR = os.path.join(REPO, 'examples', 'utf16')
SHARED_FIRST = ['contractus.exsc']
SHARED_LAST = ['probatio.exsc']
DATA = os.path.join(REPO, 'tests', 'data')
HOSTIS = 'x86_64-linux'


# ------------------------------------------------------------------ plumbing

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)


def libraries():
    skip = set(SHARED_FIRST + SHARED_LAST)
    return sorted(f[:-5] for f in os.listdir(LIBDIR)
                  if f.endswith('.exsc') and f not in skip)


def sources(lib, libdir=LIBDIR):
    return ([os.path.join(LIBDIR, f) for f in SHARED_FIRST] +
            [os.path.join(libdir, lib + '.exsc')] +
            [os.path.join(LIBDIR, f) for f in SHARED_LAST])


class Env:
    def __init__(self, workdir, fasmg, exsc):
        self.workdir, self.fasmg, self.exsc = workdir, fasmg, exsc
        self.environ = dict(os.environ)
        self.environ['INCLUDE'] = os.path.join(REPO, 'vendor', 'fasmg-x86')


def build_exsc(env):
    out = os.path.join(env.workdir, 'exsc')
    r = run([env.fasmg, os.path.join(REPO, 'compiler', 'x86_64', 'exsc.asm'), out],
            env=env.environ)
    if r.returncode != 0:
        sys.exit('could not assemble exsc:\n' + r.stdout.decode() + r.stderr.decode())
    os.chmod(out, 0o755)
    env.exsc = out


def build_reference(env, name, srcs):
    """exsc -> fasmg -> ELF. Returns (path or None, message)."""
    asm = os.path.join(env.workdir, name + '.asm')
    binp = os.path.join(env.workdir, name + '.bin')
    r = run([env.exsc, 'aedifica', '--hospes', HOSTIS] + srcs + ['-o', asm],
            env=env.environ)
    if r.returncode != 0:
        return None, 'exsc exit %d: %s' % (r.returncode,
                                            (r.stderr or r.stdout).decode(errors='replace')[:600])
    r = run([env.fasmg, asm, binp], env=env.environ)
    if r.returncode != 0:
        return None, 'fasmg exit %d: %s' % (r.returncode, r.stdout.decode(errors='replace')[-600:])
    os.chmod(binp, 0o755)
    os.unlink(asm)
    return binp, ''


# gcc gets the runtime-library UBSan; clang gets -fsanitize-trap, because a
# minimal LLVM install carries no ubsan runtime to link (tests/run.sh's
# __ref_build makes the same choice, for the same reason).
CFLAGS = ['-std=c11', '-Wall', '-Wextra', '-Wno-unused-function']
SANITIZE = {'gcc': ['-fsanitize=undefined', '-fno-sanitize-recover=all'],
            'clang': ['-fsanitize=undefined', '-fsanitize-trap=undefined']}


def build_c(env, name, srcs):
    """exsc --emitte c -> gcc/clang x {-O0,-O2}. Returns {tag: path or None}."""
    cfile = os.path.join(env.workdir, name + '.c')
    r = run([env.exsc, 'aedifica', '--hospes', HOSTIS, '--emitte', 'c'] + srcs +
            ['-o', cfile], env=env.environ)
    if r.returncode != 0:
        return {'emit': (None, 'exsc --emitte c exit %d: %s' %
                         (r.returncode, (r.stderr or r.stdout).decode(errors='replace')[:400]))}
    shim = os.path.join(REPO, 'tests', 'c', 'exsrt_shim.c')
    out = {}
    for cc in ('gcc', 'clang'):
        if not shutil.which(cc):
            out[cc] = (None, '%s not on PATH' % cc)
            continue
        nowarn = '-Wno-cpp' if cc == 'gcc' else '-Wno-#warnings'
        for opt in ('-O0', '-O2'):
            tag = '%s%s' % (cc, opt)
            binp = os.path.join(env.workdir, '%s.%s.bin' % (name, tag))
            r = run([cc] + CFLAGS + SANITIZE[cc] + [nowarn, opt, '-o', binp, cfile, shim])
            out[tag] = (binp, '') if r.returncode == 0 else \
                (None, '%s exit %d: %s' % (cc, r.returncode, r.stderr.decode(errors='replace')[:400]))
    return out


# ------------------------------------------------------------------- corpora

class Corpus:
    """A named run: the bytes to feed, the bytes that must come back."""

    def __init__(self, name, order, records=None, stdin=None, expect=None):
        self.name, self.order = name, order
        self.records = records
        self._stdin, self._expect = stdin, expect

    @property
    def stdin(self):
        if self._stdin is None:
            self._stdin = oracle.frame(self.order, self.records)
        return self._stdin

    @property
    def expect(self):
        if self._expect is None:
            self._expect = oracle.expected(self.order, self.records)
        return self._expect


def fixture_corpora():
    out = []
    for order in ('L', 'B'):
        with open(os.path.join(DATA, 'utf16_corpus_%s.bin' % order), 'rb') as f:
            stdin = f.read()
        with open(os.path.join(DATA, 'utf16_expected_%s.bin' % order), 'rb') as f:
            expect = f.read()
        out.append(Corpus('fixture-' + order, order, stdin=stdin, expect=expect))
    return out


def deep_corpora():
    out = []
    for name, recs in corpus.deep():
        for order in ('L', 'B'):
            out.append(Corpus('%s-%s' % (name, order), order, records=recs))
    return out


def first_difference(c, got):
    """Describe the first record at which `got` departs from the oracle."""
    exp_off = got_off = 0
    recs = c.records
    if recs is None:
        recs = parse_frame(c.stdin)
    for idx, (modus, data) in enumerate(recs):
        want = oracle.record_out(*oracle.py_convert(data, modus), c.order)
        have = got[got_off:got_off + len(want)]
        if have != want:
            return ('record %d (modus %d, %d bytes in): input %s\n'
                    '          expected %s\n          got      %s' %
                    (idx, modus, len(data), data[:48].hex(), want[:48].hex(), have[:48].hex()))
        got_off += len(want)
    if len(got) != got_off:
        return 'all %d records equal, but %d surplus bytes' % (len(recs), len(got) - got_off)
    return 'no difference found'


def parse_frame(stdin):
    recs, i = [], 1
    while i < len(stdin):
        modus = stdin[i]
        n = stdin[i + 1] | (stdin[i + 2] << 8)
        recs.append((modus, stdin[i + 3:i + 3 + n]))
        i += 3 + n
    return recs


def execute(binp, c, timeout=300):
    r = subprocess.run([binp], input=c.stdin, capture_output=True, timeout=timeout)
    return r.returncode, r.stdout


def check(binp, c):
    """(ok, detail)"""
    try:
        rc, got = execute(binp, c)
    except subprocess.TimeoutExpired:
        return False, 'timed out'
    if rc != 0:
        return False, 'exit %d' % rc
    if got == c.expect:
        return True, ''
    return False, first_difference(c, got)


# ------------------------------------------------------------------- audit

def audit(binp):
    script = os.path.join(REPO, 'tools', 'syscall-audit.sh')
    r = run([script, '--potestates', 'Mundus,ambitus', binp])
    return r.returncode == 0, (r.stdout + r.stderr).decode(errors='replace')[-400:]


# ----------------------------------------------------------------- mutation

# Literals whose neighbours are the standard's boundaries. A mutant swaps one
# occurrence for the value in the table; most are NOT equivalent, because each
# of these numbers is a place Table 3-7 or the surrogate algorithm changes.
MUTATE = {
    '0x80': ['0x81', '0x7F'], '0xBF': ['0xBE', '0xC0'], '0xC2': ['0xC1', '0xC3'],
    '0xDF': ['0xDE', '0xE0'], '0xE0': ['0xDF', '0xE1'], '0xA0': ['0x9F', '0xA1'],
    '0xED': ['0xEC', '0xEE'], '0x9F': ['0x9E', '0xA0'], '0xEF': ['0xEE', '0xF0'],
    '0xF0': ['0xEF', '0xF1'], '0x90': ['0x8F', '0x91'], '0xF4': ['0xF3', '0xF5'],
    '0x8F': ['0x8E', '0x90'], '0xF3': ['0xF2', '0xF4'], '0xF1': ['0xF0', '0xF2'],
    '0xE1': ['0xE0', '0xE2'], '0xC0': ['0xBF', '0xC1'],
    # payload masks: only the NARROWER neighbour. The wider one is equivalent
    # whenever the byte's own high bits are already clear (0x0F -> 0x1F on an
    # E0..EF lead), which is every use, so it would only add noise.
    '0x3F': ['0x1F'], '0x1F': ['0x0F'], '0x0F': ['0x07'], '0x07': ['0x03'],
    '0x10000': ['0x10001', '0xFFFF'], '0xD800': ['0xD801', '0xD7FF'],
    '0xDC00': ['0xDC01', '0xDBFF'], '0x3FF': ['0x3FE', '0x7FF'],
    '0xFFFD': ['0xFFFC', '0xFFFE'], '0xD7C0': ['0xD7C1', '0xD7BF'],
    '0x7F': ['0x7E', '0x80'],
}
TOKEN = re.compile(r'(?<![0-9A-Za-z_])0x[0-9A-Fa-f]+(?![0-9A-Za-z_])')


def mutants_of(text, limit):
    """Deterministic: occurrences in source order, spread evenly to `limit`."""
    sites = [(m.start(), m.end(), m.group(0)) for m in TOKEN.finditer(text)
             if m.group(0) in MUTATE
             and not text[:m.start()].rsplit('\n', 1)[-1].lstrip().startswith('//')]
    cands = [(a, b, old, new) for a, b, old in sites for new in MUTATE[old]]
    if len(cands) > limit:
        step = len(cands) / limit
        cands = [cands[int(i * step)] for i in range(limit)]
    out = []
    for a, b, old, new in cands:
        line = text.count('\n', 0, a) + 1
        out.append((text[:a] + new + text[b:], '%s->%s line %d: %s' %
                    (old, new, line, text.splitlines()[line - 1].strip()[:70])))
    return out


# --------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--libs', help='comma-separated library names (default: all)')
    ap.add_argument('--deep', action='store_true', help='also run the deep corpora')
    ap.add_argument('--c', action='store_true', help='also build and run the C backend (gcc, clang; -O0, -O2)')
    ap.add_argument('--audit', action='store_true', help='also run tools/syscall-audit.sh on each binary')
    ap.add_argument('--mutants', type=int, default=0, metavar='N', help='mutation-test the fixture with N mutants per library')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 2)
    ap.add_argument('--keep', action='store_true', help='keep the work directory')
    ap.add_argument('--exsc', help='use this exsc instead of assembling one')
    a = ap.parse_args()

    fasmg = os.environ.get('FASMG') or shutil.which('fasmg')
    if not fasmg:
        sys.exit('fasmg not found: put it on PATH or set FASMG')
    libs = libraries()
    if a.libs:
        want = a.libs.split(',')
        unknown = [w for w in want if w not in libs]
        if unknown:
            sys.exit('no such library: %s (have: %s)' % (', '.join(unknown), ', '.join(libs)))
        libs = want
    if not libs:
        sys.exit('no libraries under %s' % LIBDIR)

    work = tempfile.mkdtemp(prefix='utf16-matrix.')
    env = Env(work, fasmg, a.exsc)
    t0 = time.time()
    failures = []
    try:
        if not env.exsc:
            build_exsc(env)
        print('exsc: %s' % env.exsc)
        print('libraries: %d' % len(libs))

        corpora = fixture_corpora()
        if a.deep:
            t = time.time()
            corpora += deep_corpora()
            print('deep corpora: %d runs, %d bytes of input (generated in %.1fs, expected outputs lazily)' %
                  (len(corpora) - 2, sum(len(c.stdin) for c in corpora[2:]), time.time() - t))
            for c in corpora[2:]:
                _ = c.expect
            print('oracle outputs computed in %.1fs' % (time.time() - t))

        # -- build every library (parallel), reference backend
        def build(lib):
            return lib, build_reference(env, lib, sources(lib))
        with cf.ThreadPoolExecutor(a.jobs) as ex:
            built = dict(ex.map(build, libs))

        cbuilt = {}
        if a.c:
            def buildc(lib):
                return lib, build_c(env, lib, sources(lib))
            with cf.ThreadPoolExecutor(a.jobs) as ex:
                cbuilt = dict(ex.map(buildc, libs))

        # -- run
        columns = [c.name for c in corpora]
        results = {lib: {} for lib in libs}
        details = []

        def task(lib, col, binp, c):
            ok, why = check(binp, c)
            return lib, col, ok, why

        jobs = []
        with cf.ThreadPoolExecutor(a.jobs) as ex:
            for lib in libs:
                binp, msg = built[lib]
                if binp is None:
                    results[lib]['build'] = (False, msg)
                    continue
                results[lib]['build'] = (True, '%d bytes' % os.path.getsize(binp))
                for c in corpora:
                    jobs.append(ex.submit(task, lib, c.name, binp, c))
                if a.c:
                    for tag, (cb, cmsg) in cbuilt[lib].items():
                        if cb is None:
                            results[lib]['C:' + tag] = (False, cmsg)
                            continue
                        for c in corpora:
                            jobs.append(ex.submit(task, lib, 'C:%s:%s' % (tag, c.name), cb, c))
                if a.audit:
                    results[lib]['audit'] = audit(binp)
            for j in cf.as_completed(jobs):
                lib, col, ok, why = j.result()
                results[lib][col] = (ok, why)

        # -- report
        print()
        width = max(len(l) for l in libs)
        order_cols = ['build'] + columns
        if a.c:
            order_cols += ['C:%s:%s' % (t, c) for t in ('gcc-O0', 'gcc-O2', 'clang-O0', 'clang-O2')
                           for c in columns]
        if a.audit:
            order_cols.append('audit')
        # collapse the C columns to one cell per compiler configuration
        for lib in libs:
            row = results[lib]
            cells = []
            ref_ok = all(row.get(c, (False,))[0] for c in ['build'] + columns)
            cells.append('exsc:%s' % ('ok' if ref_ok else 'FAIL'))
            if a.c:
                for t in ('gcc-O0', 'gcc-O2', 'clang-O0', 'clang-O2'):
                    keys = [k for k in row if k.startswith('C:%s' % t)] or ['C:' + t]
                    ok = all(row.get(k, (False,))[0] for k in keys) and \
                        len([k for k in row if k.startswith('C:%s:' % t)]) == len(corpora)
                    cells.append('%s:%s' % (t, 'ok' if ok else 'FAIL'))
            if a.audit:
                cells.append('audit:%s' % ('ok' if row.get('audit', (False,))[0] else 'FAIL'))
            size = row.get('build', (False, ''))[1]
            print('%-*s  %s  [%s]' % (width, lib, '  '.join(cells), size))
            for col, (ok, why) in sorted(row.items()):
                if not ok:
                    failures.append((lib, col, why))
        print()
        if failures:
            print('FAILURES (%d):' % len(failures))
            for lib, col, why in failures[:60]:
                print('  %s / %s: %s' % (lib, col, why))
            if len(failures) > 60:
                print('  ... and %d more' % (len(failures) - 60))

        # -- mutation
        if a.mutants:
            survivors, killed, total = [], 0, 0
            fix = fixture_corpora()[0]

            def mutate_one(arg):
                lib, idx, text, tag = arg
                mdir = os.path.join(work, 'm')
                os.makedirs(mdir, exist_ok=True)
                name = '%s__m%d' % (lib, idx)
                path = os.path.join(mdir, name + '.exsc')
                with open(path, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(text)
                binp, msg = build_reference(env, name, sources(name, mdir))
                if binp is None:
                    return lib, tag, 'unbuildable', msg
                ok, why = check(binp, fix)
                os.unlink(binp)
                return lib, tag, ('survived' if ok else 'killed'), why
            work_items = []
            for lib in libs:
                with open(os.path.join(LIBDIR, lib + '.exsc'), encoding='utf-8') as f:
                    text = f.read()
                for idx, (mt, tag) in enumerate(mutants_of(text, a.mutants)):
                    work_items.append((lib, idx, mt, tag))
            with cf.ThreadPoolExecutor(a.jobs) as ex:
                outcomes = list(ex.map(mutate_one, work_items))
            per = {}
            for lib, tag, verdict, why in outcomes:
                d = per.setdefault(lib, {'killed': 0, 'survived': 0, 'unbuildable': 0})
                d[verdict] += 1
                if verdict == 'survived':
                    survivors.append((lib, tag))
            print('MUTATION (fixture corpus only):')
            for lib in libs:
                d = per.get(lib, {'killed': 0, 'survived': 0, 'unbuildable': 0})
                print('  %-*s killed %3d  survived %3d  unbuildable %3d' %
                      (width, lib, d['killed'], d['survived'], d['unbuildable']))
            tk = sum(d['killed'] for d in per.values())
            ts = sum(d['survived'] for d in per.values())
            tu = sum(d['unbuildable'] for d in per.values())
            print('  total: %d killed, %d survived, %d unbuildable' % (tk, ts, tu))
            for lib, tag in survivors:
                print('  SURVIVOR %s %s' % (lib, tag))

        print('done in %.1fs' % (time.time() - t0))
        return 1 if failures else 0
    finally:
        if a.keep:
            print('work directory kept: %s' % work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
