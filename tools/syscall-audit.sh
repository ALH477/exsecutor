#!/usr/bin/env bash
# tools/syscall-audit.sh -- spec §9.3's "no network access, ever, at any
# phase" made checkable rather than promised (see spec §18.1, CLAUDE.md
# "The compiler is freestanding").
#
# Usage:
#   tools/syscall-audit.sh <binary>     audit one ELF64 x86-64 executable
#                                       against the COMPILER's own closed
#                                       nine (CLAUDE.md, asm-conventions.md
#                                       §6 bullet one).
#   tools/syscall-audit.sh --potestates ATOM[,ATOM...] <binary>
#                                       audit a COMPILED PROGRAM's binary
#                                       against the union of the syscalls
#                                       its declared capability atoms admit
#                                       -- asm-conventions.md §6's SECOND
#                                       closed set (compiler/x86_64/prelude/,
#                                       docs/design/runtime.md section 2.6). Makes
#                                       spec §10.3's audit a property of the
#                                       artifact. See "POTESTATES MODE"
#                                       below for the table and its source.
#   tools/syscall-audit.sh --self-test  assemble the tests/unit/ fixtures
#                                       and audit them, proving this script
#                                       works before compiler/x86_64/exsc.asm
#                                       exists to produce a real target.
#                                       Covers both modes above.
#
# Exit status: 0 = audit passed. 1 = audit failed (disallowed syscall,
# indeterminate syscall number, or not freestanding). 2 = usage/environment
# error (binary missing, required tool missing, unknown --potestates atom
# spelling).
#
# ---------------------------------------------------------------------------
# POTESTATES MODE -- the second closed set, per binary rather than per repo.
#
# `--potestates` does not replace the freestanding / PT_INTERP / dynamic-
# section checks, and does not change how syscall SITES are found or how
# their `rax` is resolved (still the linear sweep described below). It only
# changes what a resolved number is checked AGAINST: instead of the
# compiler's fixed nine, it is the union, over the named atoms, of the
# table in `compiler/x86_64/prelude/README.md` ("The syscall table, per
# atom") -- taken from there "as built" (grepped from prelude.asm's actual
# `if EXS_POTESTAS_<atom>` gates), per this flag's design brief, NOT from
# `docs/design/runtime.md` section 2.6, which disagrees for `archivum`,
# `horologium`, `fortuna` and `rete`: runtime.md pre-declares specific
# syscalls for those atoms (openat/close/fstat/lseek/read/write for
# archivum; clock_gettime for horologium; getrandom for fortuna; "the
# socket family" for rete) while no routine gated on any of those four
# atoms exists in prelude.asm yet -- the README's per-atom table agrees
# with runtime.md that they are all `[OPEN]`, but tabulates them as
# admitting nothing ("none") rather than runtime.md's specific numbers.
# Where the two disagree, per this flag's brief, the README wins ("it is
# what was assembled"); this script says so in its own output whenever an
# atom whose two tables disagree is named.
#
#   core, always, regardless of which atoms (if any) are named:
#     exit_group(231)               -- exsrt_start. Neither it nor
#                                       exsrt_abort's write below sits
#                                       inside an `if EXS_POTESTAS_*` block
#                                       -- not even `if EXS_POTESTAS_MUNDUS`,
#                                       which gates no syscall site in the
#                                       blob as built today despite being a
#                                       required (spec §4.6 root) atom.
#     write(1) TO FD 2 ONLY         -- exsrt_abort, and ONLY exsrt_abort:
#                                       both tables read "write(1) to fd 2
#                                       ... from exsrt_abort only", not
#                                       "write(1), unconditionally". A
#                                       write(1) whose fd this script can
#                                       also resolve (same trusted forms as
#                                       the syscall number, applied to
#                                       rdi/edi) to the immediate 2 is
#                                       admitted here regardless of atoms;
#                                       any other or unresolved fd is NOT
#                                       core and needs `ambitus` (below).
#                                       This is what tells exsrt_abort's
#                                       write (edi loaded as an immediate)
#                                       apart from exsrt_scriptor_scribe's
#                                       (edi loaded from a Scriptor field)
#                                       when both compile to the identical
#                                       `mov eax,1` / `syscall`.
#   ambitus:   read(0), write(1) to any fd -- exsrt_scriptor_scribe (`read`
#                                       is [UNIMPLEMENTED], no reader
#                                       exists yet, but the atom's admitted
#                                       set is tabulated regardless)
#   alloc:     mmap(9), munmap(11)  -- exsrt_alloc_novum, exsrt_alloc_dimitte
#   rete:      the socket family    -- SPECIAL-CASED, not read off either
#                                       table (both leave it [OPEN] with no
#                                       routine and no numbers): "no
#                                       socket-family syscall is ever
#                                       admitted unless `rete` is named,"
#                                       per this flag's brief, restating
#                                       CLAUDE.md/asm-conventions.md §6's
#                                       "a program has one iff its closure
#                                       contains `rete`" as an audit this
#                                       tool can perform. When `rete` IS
#                                       named, every finding it admits says
#                                       so explicitly in its own row.
#   Mundus, sermo, horologium, archivum, fortuna, Filum, machina, Crudum:
#     admit nothing beyond core today (README: "[OPEN]" / "none" -- no
#     EXS_POTESTAS-gated routine exists for any of these eight yet). Naming
#     one is not an error -- it is a valid spec §4.6 atom -- it just adds
#     no syscalls to the union until a routine exists.
#
# Unknown atom spelling: exit 2, matching this script's other usage errors.
# Spellings are spec §4.6's eleven, case-sensitive, exactly as prelude.asm's
# own `EXS_POTESTAS_*` guard requires all be defined: Mundus alloc sermo
# horologium archivum rete fortuna ambitus Filum machina Crudum.
#
# Without --potestates, this script's behavior is byte-for-byte unchanged:
# the compiler's own closed nine, socket-family always a hard failure,
# --self-test unchanged in its original two cases (a third and fourth are
# added, covering this mode, using the same two existing fixtures).
# ---------------------------------------------------------------------------
#
# ---------------------------------------------------------------------------
# THIS IS A HEURISTIC. Read this before trusting a PASS.
#
# fasmg's minimal `format ELF64 executable` output has NO section headers
# (verified empirically: `objdump -d`/`objdump -D` print nothing on it --
# objdump's normal ELF path walks sections, and there are none to walk).
# So this script never runs plain `objdump -d`. Instead, per binary:
#
#   1. `readelf -h` / `readelf -lW` give the entry point and the PT_LOAD
#      segments, independent of section headers.
#   2. Executable-segment bytes are carved straight out of the file and fed
#      to `objdump -D -b binary -m i386:x86-64`, addressed with
#      `--adjust-vma` so reported addresses match real virtual addresses.
#   3. The linear sweep is anchored AT THE ENTRY POINT, not at the start of
#      the segment. This matters: fasmg overlaps the ELF header with the
#      executable segment to save bytes, and disassembling those header
#      bytes as code desynchronizes the instruction stream -- empirically,
#      on this project's own smoke fixture, a sweep from segment offset 0
#      swallows the very first `mov eax,imm` into a bogus instruction and
#      only resynchronizes a few bytes later by luck. The entry point is
#      the one address the CPU is guaranteed to treat as an instruction
#      boundary, so anchoring there sidesteps the problem for the normal
#      case: straight-line code reachable forward from `_start`. Any OTHER
#      executable PT_LOAD segment (unusual -- none of this project's
#      fixtures have one) is swept from its own start instead, with no such
#      guarantee, and reported with an explicit alignment warning.
#   4. Within each sweep, every instruction is walked in order, tracking
#      the last thing written to the rax/eax register family (equivalent
#      to, and simpler to implement correctly than, scanning backward from
#      each `syscall` for the nearest preceding write -- same answer, one
#      pass). Only two forms are trusted to fully resolve a syscall number,
#      because both are guaranteed to clear rax's high bits:
#        - `mov eax,<imm>` / `mov rax,<imm>`                (immediate load)
#        - `xor eax,eax` / `xor rax,rax` / `sub eax,eax` / `sub rax,rax`
#                                                    (self-zeroing idiom)
#      Anything else that touches any part of rax (register-to-register
#      moves, memory loads, `pop rax`, `call` [rax is caller-saved -- its
#      post-call value is not statically known], `div`/`mul`/`cdqe`/cpuid
#      and other implicit-operand forms, ...) marks the value INDETERMINATE
#      from that point on, until the next resolving write. A `syscall`
#      reached with no resolved value -- because an indeterminate write
#      intervened, or because no write to rax was seen at all in the
#      scanned range -- is reported as INDETERMINATE and counted as a
#      FAILURE. It is never silently skipped; a register-indirect or
#      computed rax defeats the audit if it is allowed to pass quietly.
#
# Known limits, stated plainly, not hidden:
#   - Linear sweep, not control-flow analysis. A syscall number computed
#     via a jump table, or set on one branch of a merge and read after,
#     will most likely resolve INDETERMINATE (fails safe) -- or, in
#     principle, could resolve to the wrong constant if two branches both
#     set rax and only one is nearer in linear file order. Not observed in
#     this project's straight-line fixtures; not proven impossible for a
#     more adversarial binary.
#   - Cannot see backward past the scan anchor -- code reachable only via a
#     backward jump into bytes before the entry point is not scanned.
#   - A static approximation of what a syscall site encodes, not a runtime
#     trace.
#   - x86-64-specific. This project has one backend.
#   - --potestates mode's fd resolution (rdi/edi, for write(1)'s core-vs-
#     ambitus distinction) reuses this exact same linear-sweep machinery
#     and inherits the same limits -- an indeterminate fd is treated as
#     NOT the fd-2 core case (fails safe: falls through to needing
#     `ambitus`), never silently assumed to be fd 2.
#
# The freestanding check (no PT_INTERP, no dynamic section) is NOT a
# heuristic -- readelf reports both directly from ELF structure -- and it
# is checked because a dynamically linked binary can reach the network
# through libc regardless of what its own syscall sites say.
# ---------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FASMG="${FASMG:-fasmg}"
READELF="${READELF:-readelf}"
OBJDUMP="${OBJDUMP:-objdump}"
PYTHON3="${PYTHON3:-python3}"

# spec §4.6's eleven capability atoms, exactly as spelled there and as
# prelude.asm's own EXS_POTESTAS_* guard requires all eleven be defined.
# Case-sensitive.
POTESTATES_VALID=(Mundus alloc sermo horologium archivum rete fortuna ambitus Filum machina Crudum)

# Validates and canonicalizes a comma-separated --potestates atom list.
# Prints the deduplicated list back out (insertion order preserved -- this
# repo's determinism convention, CLAUDE.md, applies to this script's own
# output too even though it is not compiler output). Exits 2 on any
# unrecognized spelling, matching this script's other usage errors.
validate_potestates() {
  local csv="$1" atom out=() seen=","
  local -A known=()
  for atom in "${POTESTATES_VALID[@]}"; do known["$atom"]=1; done
  IFS=',' read -ra parts <<<"$csv"
  for atom in "${parts[@]}"; do
    atom="${atom#"${atom%%[![:space:]]*}"}"   # trim leading space
    atom="${atom%"${atom##*[![:space:]]}"}"   # trim trailing space
    if [[ -z "$atom" ]]; then
      continue
    fi
    if [[ -z "${known[$atom]:-}" ]]; then
      echo "syscall-audit: unknown --potestates atom spelling: '$atom'" >&2
      echo "spec §4.6's eleven, case-sensitive: ${POTESTATES_VALID[*]}" >&2
      exit 2
    fi
    if [[ "$seen" != *",$atom,"* ]]; then
      out+=("$atom")
      seen="$seen$atom,"
    fi
  done
  if [[ ${#out[@]} -eq 0 ]]; then
    echo "syscall-audit: --potestates given an empty atom list" >&2
    exit 2
  fi
  local IFS=','
  echo "${out[*]}"
}

# The audit engine. Everything past ELF/segment discovery is easier to get
# right in a real language than in awk, so it lives here as one embedded
# script rather than a second file (out of this agent's file scope).
#
# atoms: empty string for the compiler's default fixed-nine mode, or a
# validated/canonicalized comma-separated --potestates atom list.
audit_binary() {
  local bin="$1" atoms="${2:-}"
  READELF_BIN="$READELF" OBJDUMP_BIN="$OBJDUMP" POTESTATES_ATOMS="$atoms" \
    "$PYTHON3" - "$bin" <<'PYEOF'
import os, re, subprocess, sys, tempfile

ALLOWLIST = {
    0: 'read', 1: 'write', 3: 'close', 5: 'fstat', 8: 'lseek',
    9: 'mmap', 11: 'munmap', 257: 'openat', 231: 'exit_group',
}
SOCKET_FAMILY = {
    41: 'socket', 42: 'connect', 43: 'accept', 44: 'sendto',
    45: 'recvfrom', 46: 'sendmsg', 47: 'recvmsg', 48: 'shutdown',
    49: 'bind', 50: 'listen', 51: 'getsockname', 52: 'getpeername',
    53: 'socketpair', 54: 'setsockopt', 55: 'getsockopt',
    288: 'accept4', 299: 'recvmmsg', 307: 'sendmmsg',
}
OTHER_FLAGGED = {
    101: 'ptrace', 59: 'execve', 322: 'execveat',
    57: 'fork', 58: 'vfork', 56: 'clone',
}

# --potestates mode: compiler/x86_64/prelude/README.md, "The syscall table,
# per atom" -- taken "as built" (grepped from prelude.asm's actual `if
# EXS_POTESTAS_<atom>` gates), NOT docs/design/runtime.md section 2.6,
# which disagrees for archivum/horologium/fortuna/rete (see this script's
# header comment "POTESTATES MODE" for the full account of the
# disagreement and why the README wins here).
#
# core: exit_group(231) is always admitted, regardless of which atoms (if
# any) are named -- exsrt_start's exit_group sits outside every `if
# EXS_POTESTAS_*` block in prelude.asm, even EXS_POTESTAS_MUNDUS (which
# gates no syscall site in the blob as built today).
#
# write(1) is ALSO core, but not unconditionally: README/runtime.md section 2.6
# both read "write(1) to fd 2 ... from exsrt_abort only" -- exsrt_abort's
# write loads `edi, 2` as an immediate (fd 2, unconditional, outside every
# `if` block); exsrt_scriptor_scribe's write (gated on EXS_POTESTAS_AMBITUS)
# loads its fd from a Scriptor field, not an immediate, and is a DIFFERENT
# admission (ambitus, any fd). Both compile to the identical `mov eax,1` /
# `syscall` -- the syscall NUMBER alone cannot tell them apart, so
# classify_potestates additionally resolves the fd (scan()'s known_fd,
# tracked the same way as the syscall number) and admits write(1)
# unconditionally only when that fd resolves to the immediate 2; any other
# or unresolved fd needs `ambitus` named. write(1) is therefore NOT listed
# in this dict -- it is special-cased entirely in classify_potestates.
POTESTATES_CORE = {231: 'exit_group'}

# Per-atom addition to the admitted union. Empty means the atom is a valid
# spec §4.6 spelling that currently gates no EXS_POTESTAS-conditioned
# syscall site in prelude.asm (README: "[OPEN]" / "none") -- naming it is
# not an error, it just adds nothing yet.
POTESTATES_TABLE = {
    'Mundus':     {},
    'alloc':      {9: 'mmap', 11: 'munmap'},
    'sermo':      {},
    'horologium': {},
    'archivum':   {},
    'rete':       {},   # special-cased in classify_potestates -- see below
    'fortuna':    {},
    'ambitus':    {0: 'read', 1: 'write'},
    'Filum':      {},
    'machina':    {},
    'Crudum':     {},
}
# Atoms whose README/runtime.md tables actively disagree (both leave the
# atom [OPEN], but runtime.md pre-declares specific syscall numbers the
# README does not) -- named here so the tool can say so in its own output
# per this flag's brief, rather than silently picking one.
POTESTATES_DISAGREEMENT = {
    'archivum':   "runtime.md tabulates openat(257)/close(3)/fstat(5)/"
                   "lseek(8)/read(0)/write(1) for archivum; no such "
                   "EXS_POTESTAS_ARCHIVUM-gated routine exists in "
                   "prelude.asm yet, so the README (as built) admits "
                   "nothing for it -- using the README",
    'horologium': "runtime.md tabulates clock_gettime(228) [OPEN] for "
                  "horologium; the README lists no routine ('none') -- "
                  "using the README (admits nothing)",
    'fortuna':    "runtime.md tabulates getrandom(318) [OPEN] for "
                  "fortuna; the README lists no routine ('none') -- "
                  "using the README (admits nothing)",
}

FULL_WRITE_REGS = {'rax', 'eax'}          # zero/replace the full 64 bits
RAX_FAMILY = {'rax', 'eax', 'ax', 'al', 'ah'}
# Same two categories, for rdi/edi -- syscall arg1, tracked only to tell a
# write(1) targeting a resolvable, immediate fd 2 (--potestates mode's
# core-row case) apart from every other write(1). See scan()'s docstring.
FD_FULL_WRITE_REGS = {'rdi', 'edi'}
FD_FAMILY = {'rdi', 'edi', 'di', 'dil'}
NONWRITING_MNEMONICS = {
    'cmp', 'test', 'push', 'nop', 'ret', 'retn', 'leave', 'hlt', 'int3',
    'jmp', 'je', 'jz', 'jne', 'jnz', 'ja', 'jnbe', 'jae', 'jnb', 'jb',
    'jnae', 'jbe', 'jna', 'jg', 'jnle', 'jge', 'jnl', 'jl', 'jnge', 'jle',
    'jng', 'js', 'jns', 'jo', 'jno', 'jp', 'jpe', 'jnp', 'jpo', 'jcxz',
    'jecxz', 'jrcxz', 'loop', 'loope', 'loopz', 'loopne', 'loopnz',
    'wait', 'fwait', 'endbr64', 'ud2',
}
# Mnemonics that write (part of) rax WITHOUT showing it as a visible
# operand -- these need listing explicitly; anything that writes an
# *explicit* eax/rax/ax/al/ah operand is already caught generically below.
IMPLICIT_RAX_MNEMONICS = {
    'cdqe', 'cwde', 'cbw', 'cqo', 'cdq', 'cwd', 'div', 'idiv', 'mul',
    'imul', 'cpuid', 'rdtsc', 'rdtscp', 'rdrand', 'rdseed', 'lods',
    'lodsb', 'lodsw', 'lodsd', 'lodsq', 'in', 'xgetbv', 'sysenter',
    'sysexit',
}

IMM_RE = re.compile(r'^-?0x[0-9a-fA-F]+$|^-?[0-9]+$')
LINE_RE = re.compile(r'^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2}\s+)+)\s*(.*)$')


def parse_imm(tok):
    tok = tok.strip()
    if not IMM_RE.match(tok):
        return None
    try:
        return int(tok, 16) if '0x' in tok.lower() else int(tok, 10)
    except ValueError:
        return None


def run(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def readelf_header(readelf_bin, path):
    out = run([readelf_bin, '-h', path])
    m = re.search(r'Class:\s+(\S+)', out)
    klass = m.group(1) if m else None
    m = re.search(r'Machine:\s+(.*)', out)
    machine = m.group(1).strip() if m else None
    m = re.search(r'Entry point address:\s+(0x[0-9a-fA-F]+)', out)
    entry = int(m.group(1), 16) if m else None
    return klass, machine, entry


def readelf_segments(readelf_bin, path):
    out = run([readelf_bin, '-lW', path])
    segs = []
    interp = False
    dyn_phdr = False
    seg_re = re.compile(
        r'^LOAD\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+'
        r'(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+([RWE ]{1,3})\s+(0x[0-9a-fA-F]+)'
    )
    for raw in out.splitlines():
        s = raw.strip()
        if s.startswith('INTERP'):
            interp = True
        elif s.startswith('DYNAMIC'):
            dyn_phdr = True
        elif s.startswith('LOAD'):
            m = seg_re.match(s)
            if not m:
                continue
            off, vaddr, _paddr, filesz, _memsz, flags, _align = m.groups()
            segs.append({
                'offset': int(off, 16),
                'vaddr': int(vaddr, 16),
                'filesz': int(filesz, 16),
                'exec': 'E' in flags,
            })
    return segs, interp, dyn_phdr


def readelf_has_dynamic_section(readelf_bin, path):
    out = run([readelf_bin, '-d', path])
    return 'Dynamic section at offset' in out


def disassemble(objdump_bin, blob_path, vaddr):
    return run([
        objdump_bin, '-D', '-b', 'binary', '-m', 'i386:x86-64',
        '-M', 'intel', '--adjust-vma=0x%x' % vaddr, blob_path,
    ])


def scan(disasm_text):
    """Walk one linear sweep, yielding (addr, resolved_nr_or_None,
    resolved_fd_or_None) per `syscall` encountered, in the order documented
    at the top of this script.

    `fd` tracks the same two trusted forms, applied to the rdi/edi family
    (the Linux syscall ABI's first argument) instead of rax/eax. Default
    mode ignores it entirely -- classify(nr) never looks at it, so default
    output is unaffected. --potestates mode needs it because
    compiler/x86_64/prelude/README.md's core row is not "write(1),
    unconditionally": it is "write(1) to fd 2 ... from exsrt_abort only"
    (docs/design/runtime.md section 2.6 says the same, "from exsrt_abort only").
    `exsrt_scriptor_scribe`'s write loads its fd from a Scriptor field
    (register-to-register, not an immediate) and is gated on `ambitus` in
    prelude.asm; `exsrt_abort`'s write loads `edi, 2` as an immediate,
    unconditionally. The two are indistinguishable by syscall number alone
    (both are `mov eax,1` / `syscall`) -- resolving the fd the same way
    rax is resolved is what tells them apart."""
    findings = []
    known = None      # rax/eax family -- the syscall number
    known_fd = None    # rdi/edi family -- syscall arg1 (a write's fd)
    for line in disasm_text.splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        rest = m.group(3).split('#', 1)[0].strip()
        if not rest:
            continue
        parts = rest.split(None, 1)
        mnem = parts[0].lower()
        operand_str = parts[1] if len(parts) > 1 else ''
        operands = [o.strip().lower() for o in operand_str.split(',')] if operand_str else []

        if mnem == 'syscall':
            findings.append((addr, known, known_fd))
            known = None     # the return value is a dynamic quantity
            known_fd = None
            continue
        if mnem == 'call':
            # both rax and rdi are caller-saved; post-call values unknowable
            known = None
            known_fd = None
            continue
        if mnem in NONWRITING_MNEMONICS:
            continue
        if mnem in IMPLICIT_RAX_MNEMONICS:
            known = None
            continue

        # rax-family resolution -- same rules and same priority as before
        # this function also tracked rdi; only the early `continue`s became
        # `elif`s so that the rdi-family block below still sees every
        # instruction that isn't syscall/call/nonwriting/implicit-rax.
        if mnem == 'mov' and len(operands) == 2 and operands[0] in FULL_WRITE_REGS:
            known = parse_imm(operands[1])  # resolved int, or None
        elif (mnem in ('xor', 'sub') and len(operands) == 2
                and operands[0] in FULL_WRITE_REGS and operands[0] == operands[1]):
            known = 0
        elif operands and operands[0] in RAX_FAMILY:
            known = None  # written some other way we don't trust to resolve
        # else: doesn't touch rax -- `known` carries forward unchanged

        # rdi-family resolution -- identical rules, applied to arg1 instead
        if mnem == 'mov' and len(operands) == 2 and operands[0] in FD_FULL_WRITE_REGS:
            known_fd = parse_imm(operands[1])
        elif (mnem in ('xor', 'sub') and len(operands) == 2
                and operands[0] in FD_FULL_WRITE_REGS and operands[0] == operands[1]):
            known_fd = 0
        elif operands and operands[0] in FD_FAMILY:
            known_fd = None
        # else: doesn't touch rdi -- `known_fd` carries forward unchanged
    return findings


def classify(nr):
    if nr is None:
        return 'FAIL', '??', 'INDETERMINATE', (
            'rax not statically resolvable by linear sweep -- treated as '
            'a failure, not skipped')
    if nr in ALLOWLIST:
        return 'PASS', str(nr), ALLOWLIST[nr], ''
    if nr in SOCKET_FAMILY:
        return 'FAIL', str(nr), SOCKET_FAMILY[nr], (
            '<<< SOCKET-FAMILY SYSCALL -- HARD FAILURE, network-capable >>>')
    if nr in OTHER_FLAGGED:
        return 'FAIL', str(nr), OTHER_FLAGGED[nr], (
            'out-of-allowlist, flagged: process/trace control')
    return 'FAIL', str(nr), 'unknown', 'not in the closed allowlist'


def potestates_admitted(atoms):
    """Union of syscalls the given atoms admit, per POTESTATES_TABLE plus
    the always-on core (exit_group only -- write(1) is special-cased in
    classify_potestates because its admission depends on the resolved fd,
    not just on which atoms are named). Returns (admitted: {nr: name},
    rete_named: bool)."""
    admitted = dict(POTESTATES_CORE)
    rete_named = 'rete' in atoms
    for atom in atoms:
        admitted.update(POTESTATES_TABLE[atom])
    if rete_named:
        admitted.update(SOCKET_FAMILY)
    return admitted, rete_named


def classify_potestates(nr, fd, admitted, rete_named, atoms):
    if nr is None:
        return 'FAIL', '??', 'INDETERMINATE', (
            'rax not statically resolvable by linear sweep -- treated as '
            'a failure, not skipped')
    if nr == 1:
        # write -- the one syscall number whose core row is conditioned on
        # more than the atom list: README/runtime.md section 2.6, "write(1) to fd
        # 2 ... from exsrt_abort only". `ambitus` admits write(1) to any
        # fd (scribe's case); absent that, only a write whose fd resolves
        # to the immediate 2 is the unconditional exsrt_abort case.
        if 'ambitus' in atoms:
            return 'PASS', '1', 'write', (
                "admitted: 'ambitus' is named -- write(1) to any fd")
        if fd == 2:
            return 'PASS', '1', 'write', (
                "admitted: core row -- fd resolves to 2, the unconditional "
                "exsrt_abort case (compiler/x86_64/prelude/README.md / "
                "runtime.md section 2.6); 'ambitus' is not required for this one")
        fd_desc = ('fd=%d' % fd) if fd is not None else 'fd INDETERMINATE'
        return 'FAIL', '1', 'write', (
            "not admitted: write(1) with %s is not the core fd-2 "
            "(exsrt_abort) case, and 'ambitus' is not in the declared "
            "potestates (%s)" % (fd_desc, ','.join(atoms)))
    if nr in admitted:
        note = ''
        if rete_named and nr in SOCKET_FAMILY:
            note = ("admitted because 'rete' was named -- rete admits the "
                     "socket family (spec §10.3's iff, checked on the "
                     "binary)")
        return 'PASS', str(nr), admitted[nr], note
    if nr in SOCKET_FAMILY:
        return 'FAIL', str(nr), SOCKET_FAMILY[nr], (
            "<<< SOCKET-FAMILY SYSCALL -- not admitted: 'rete' is not in "
            "the declared potestates (%s) >>>" % ','.join(atoms))
    if nr in OTHER_FLAGGED:
        return 'FAIL', str(nr), OTHER_FLAGGED[nr], (
            'out-of-allowlist, flagged: process/trace control')
    return 'FAIL', str(nr), 'unknown', (
        'not admitted by the declared potestates closure (%s)'
        % ','.join(atoms))


def main():
    bin_path = sys.argv[1]
    readelf_bin = os.environ.get('READELF_BIN', 'readelf')
    objdump_bin = os.environ.get('OBJDUMP_BIN', 'objdump')
    atoms_csv = os.environ.get('POTESTATES_ATOMS', '').strip()
    atoms = [a for a in atoms_csv.split(',') if a] if atoms_csv else []
    potestates_mode = bool(atoms)

    print(f"== syscall audit: {bin_path} ==")
    print("(heuristic linear-sweep disassembly -- see this script's header "
          "comment for exact method and limits)")

    admitted = {}
    rete_named = False
    if potestates_mode:
        admitted, rete_named = potestates_admitted(atoms)
        print(f"POTESTATES MODE: closure = {', '.join(atoms)}")
        print("Admitted syscalls (union, core + named atoms), per "
              "compiler/x86_64/prelude/README.md's table as built: "
              + ', '.join(f'{name}({nr})'
                           for nr, name in sorted(admitted.items())
                           if nr != 1)
              + ('; write(1) admitted to any fd' if 'ambitus' in atoms else
                 '; write(1) admitted only when its fd resolves to the '
                 'immediate 2 (exsrt_abort\'s core case)'))
        for atom in atoms:
            if atom in POTESTATES_DISAGREEMENT:
                print(f"NOTE: README/runtime.md disagree for '{atom}' -- "
                      f"{POTESTATES_DISAGREEMENT[atom]}.")
        if rete_named:
            print("NOTE: 'rete' is named -- the socket family is admitted "
                  "for this audit. This is what makes spec §10.3's audit a "
                  "property of the binary: a program's binary may contain "
                  "a socket-family syscall iff its declared closure "
                  "contains 'rete', and this run proves the 'if' half by "
                  "admitting it explicitly and by name.")
        else:
            print("NOTE: 'rete' is not named -- no socket-family syscall "
                  "is admitted; one found below is a hard failure.")

    klass, machine, entry = readelf_header(readelf_bin, bin_path)
    if klass != 'ELF64' or 'X86-64' not in (machine or ''):
        print(f"FAIL: not an ELF64 X86-64 executable (class={klass} machine={machine})")
        sys.exit(1)

    segs, interp, dyn_phdr = readelf_segments(readelf_bin, bin_path)
    has_dyn_section = readelf_has_dynamic_section(readelf_bin, bin_path)

    ok = True
    if interp:
        print("FAIL: PT_INTERP program header present -- requests a dynamic linker")
        ok = False
    else:
        print("PASS: no PT_INTERP program header")
    if dyn_phdr or has_dyn_section:
        print("FAIL: dynamic section / PT_DYNAMIC present -- not freestanding; "
              "a dynamically linked binary can reach the network through libc "
              "regardless of its own syscall sites")
        ok = False
    else:
        print("PASS: no dynamic section (statically linked)")

    exec_segs = [s for s in segs if s['exec']]
    if not exec_segs:
        print("FAIL: no executable PT_LOAD segment found -- cannot audit syscalls")
        sys.exit(1)

    findings = []
    for seg in exec_segs:
        if entry is not None and seg['vaddr'] <= entry < seg['vaddr'] + seg['filesz']:
            start_off = seg['offset'] + (entry - seg['vaddr'])
            vaddr0 = entry
            length = seg['filesz'] - (entry - seg['vaddr'])
            print(f"Scanning executable segment 0x{seg['vaddr']:x} "
                  f"(size {seg['filesz']}) anchored at entry point 0x{entry:x} "
                  f"[alignment reliable]")
        else:
            start_off = seg['offset']
            vaddr0 = seg['vaddr']
            length = seg['filesz']
            print(f"Scanning executable segment 0x{seg['vaddr']:x} "
                  f"(size {seg['filesz']}) from its own start "
                  f"[WARNING: does not contain the entry point -- linear-sweep "
                  f"alignment from here is NOT guaranteed]")

        with open(bin_path, 'rb') as f:
            f.seek(start_off)
            blob = f.read(length)
        tmp_fd, tmp_path = tempfile.mkstemp(suffix='.bin')
        try:
            with os.fdopen(tmp_fd, 'wb') as tf:
                tf.write(blob)
            text = disassemble(objdump_bin, tmp_path, vaddr0)
        finally:
            os.unlink(tmp_path)
        findings.extend(scan(text))

    findings.sort(key=lambda x: x[0])
    if not findings:
        print("No `syscall` instruction found in any executable segment.")

    print()
    print(f"{'ADDRESS':<12}{'NR':<6}{'NAME':<14}VERDICT")
    for addr, nr, syscall_fd in findings:
        if potestates_mode:
            verdict, nr_s, name, note = classify_potestates(
                nr, syscall_fd, admitted, rete_named, atoms)
        else:
            verdict, nr_s, name, note = classify(nr)
        if verdict != 'PASS':
            ok = False
        row = f"0x{addr:<10x}{nr_s:<6}{name:<14}{verdict}"
        if note:
            row += f"  {note}"
        print(row)

    print()
    print(f"AUDIT: {'PASS' if ok else 'FAIL'} -- {bin_path}")
    sys.exit(0 if ok else 1)


try:
    main()
except subprocess.CalledProcessError as e:
    sys.stderr.write(f"syscall-audit: tool failed: {' '.join(e.cmd)}\n")
    if e.stderr:
        sys.stderr.write(e.stderr)
    sys.exit(2)
except FileNotFoundError as e:
    sys.stderr.write(f"syscall-audit: required tool not found: {e}\n")
    sys.exit(2)
PYEOF
}

self_test() {
  if ! command -v "$FASMG" >/dev/null 2>&1; then
    echo "self-test: '$FASMG' not found on PATH (set FASMG=/path/to/fasmg)" >&2
    exit 2
  fi

  local clean_src="$REPO_ROOT/tests/unit/clean_syscalls.asm"
  local socket_src="$REPO_ROOT/tests/unit/socket_syscall.asm"
  local f
  for f in "$clean_src" "$socket_src"; do
    if [[ ! -f "$f" ]]; then
      echo "self-test: fixture missing: $f" >&2
      exit 2
    fi
  done

  local tmpdir
  tmpdir="$(mktemp -d)"
  trap 'rm -rf "$tmpdir"' EXIT

  echo "### self-test 1/4: clean_syscalls.asm -- expect AUDIT: PASS ###"
  INCLUDE="${INCLUDE:-$REPO_ROOT/vendor/fasmg-x86}" "$FASMG" "$clean_src" "$tmpdir/clean.out" >/dev/null
  chmod +x "$tmpdir/clean.out"
  local clean_rc=0
  audit_binary "$tmpdir/clean.out" || clean_rc=$?
  echo

  echo "### self-test 2/4: socket_syscall.asm -- expect AUDIT: FAIL (socket flagged) ###"
  INCLUDE="${INCLUDE:-$REPO_ROOT/vendor/fasmg-x86}" "$FASMG" "$socket_src" "$tmpdir/socket.out" >/dev/null
  chmod +x "$tmpdir/socket.out"
  local socket_rc=0
  audit_binary "$tmpdir/socket.out" || socket_rc=$?
  echo

  # --potestates mode, reusing the same two fixtures (no new fixture files
  # needed -- clean_syscalls.asm already exercises read/write/exit_group,
  # socket_syscall.asm already exercises the one syscall this whole script
  # exists to reject unconditionally in default mode).

  echo "### self-test 3/4: clean_syscalls.asm --potestates Mundus,ambitus -- expect AUDIT: PASS ###"
  local pot_clean_admit_rc=0
  audit_binary "$tmpdir/clean.out" "Mundus,ambitus" || pot_clean_admit_rc=$?
  echo

  echo "### self-test 3/4: clean_syscalls.asm --potestates Mundus (no ambitus) -- expect AUDIT: FAIL (read not admitted) ###"
  local pot_clean_deny_rc=0
  audit_binary "$tmpdir/clean.out" "Mundus" || pot_clean_deny_rc=$?
  echo

  echo "### self-test 4/4: socket_syscall.asm --potestates Mundus,rete -- expect AUDIT: PASS (rete admits the socket family) ###"
  local pot_socket_admit_rc=0
  audit_binary "$tmpdir/socket.out" "Mundus,rete" || pot_socket_admit_rc=$?
  echo

  echo "### self-test 4/4: socket_syscall.asm --potestates Mundus (no rete) -- expect AUDIT: FAIL (socket-family, rete not named) ###"
  local pot_socket_deny_rc=0
  audit_binary "$tmpdir/socket.out" "Mundus" || pot_socket_deny_rc=$?
  echo

  local all_ok=1
  [[ "$clean_rc" -eq 0 ]] || all_ok=0
  [[ "$socket_rc" -ne 0 ]] || all_ok=0
  [[ "$pot_clean_admit_rc" -eq 0 ]] || all_ok=0
  [[ "$pot_clean_deny_rc" -ne 0 ]] || all_ok=0
  [[ "$pot_socket_admit_rc" -eq 0 ]] || all_ok=0
  [[ "$pot_socket_deny_rc" -ne 0 ]] || all_ok=0

  if [[ "$all_ok" -eq 1 ]]; then
    echo "SELF-TEST: PASS -- default-mode clean/socket fixtures correct; --potestates union (Mundus,ambitus / Mundus,rete) admits what it should and Mundus alone rejects read and socket correctly"
    exit 0
  else
    echo "SELF-TEST: FAIL -- clean_rc=$clean_rc(want 0) socket_rc=$socket_rc(want !=0)" \
         "pot_clean_admit_rc=$pot_clean_admit_rc(want 0) pot_clean_deny_rc=$pot_clean_deny_rc(want !=0)" \
         "pot_socket_admit_rc=$pot_socket_admit_rc(want 0) pot_socket_deny_rc=$pot_socket_deny_rc(want !=0)" >&2
    exit 1
  fi
}

main() {
  if [[ "${1:-}" == "--self-test" ]]; then
    self_test
    return
  fi

  local usage="usage: $(basename "$0") <binary> | --potestates ATOM[,ATOM...] <binary> | --self-test"

  local atoms=""
  if [[ "${1:-}" == "--potestates" ]]; then
    if [[ -z "${2:-}" ]]; then
      echo "$usage" >&2
      echo "syscall-audit: --potestates requires an ATOM[,ATOM...] list" >&2
      exit 2
    fi
    atoms="$(validate_potestates "$2")"
    shift 2
  fi

  local bin="${1:-}"
  if [[ -z "$bin" ]]; then
    echo "$usage" >&2
    exit 2
  fi
  if [[ ! -e "$bin" ]]; then
    {
      echo "syscall-audit: '$bin' does not exist."
      echo "The compiler is not built yet -- compiler/x86_64/exsc.asm has not"
      echo "been written, so there is nothing at $bin to assemble or audit."
      echo "Run 'make' once it exists, or use --self-test to verify this audit"
      echo "script itself against the tests/unit/ fixtures in the meantime."
    } >&2
    exit 2
  fi

  audit_binary "$bin" "$atoms"
}

main "$@"
