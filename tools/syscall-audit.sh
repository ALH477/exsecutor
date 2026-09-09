#!/usr/bin/env bash
# tools/syscall-audit.sh -- spec §9.3's "no network access, ever, at any
# phase" made checkable rather than promised (see spec §18.1, CLAUDE.md
# "The compiler is freestanding").
#
# Usage:
#   tools/syscall-audit.sh <binary>     audit one ELF64 x86-64 executable
#   tools/syscall-audit.sh --self-test  assemble the tests/unit/ fixtures
#                                       and audit them, proving this script
#                                       works before compiler/x86_64/exsc.asm
#                                       exists to produce a real target.
#
# Exit status: 0 = audit passed. 1 = audit failed (disallowed syscall,
# indeterminate syscall number, or not freestanding). 2 = usage/environment
# error (binary missing, required tool missing).
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

# The audit engine. Everything past ELF/segment discovery is easier to get
# right in a real language than in awk, so it lives here as one embedded
# script rather than a second file (out of this agent's file scope).
audit_binary() {
  local bin="$1"
  READELF_BIN="$READELF" OBJDUMP_BIN="$OBJDUMP" "$PYTHON3" - "$bin" <<'PYEOF'
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

FULL_WRITE_REGS = {'rax', 'eax'}          # zero/replace the full 64 bits
RAX_FAMILY = {'rax', 'eax', 'ax', 'al', 'ah'}
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
    """Walk one linear sweep, yielding (addr, resolved_nr_or_None) per
    `syscall` encountered, in the order documented at the top of this
    script."""
    findings = []
    known = None
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
            findings.append((addr, known))
            known = None  # the return value is a dynamic quantity
            continue
        if mnem == 'call':
            known = None  # rax is caller-saved; post-call value unknowable here
            continue
        if mnem in NONWRITING_MNEMONICS:
            continue
        if mnem in IMPLICIT_RAX_MNEMONICS:
            known = None
            continue
        if mnem == 'mov' and len(operands) == 2 and operands[0] in FULL_WRITE_REGS:
            imm = parse_imm(operands[1])
            known = imm  # resolved int, or None if the source wasn't an immediate
            continue
        if (mnem in ('xor', 'sub') and len(operands) == 2
                and operands[0] in FULL_WRITE_REGS and operands[0] == operands[1]):
            known = 0
            continue
        if operands and operands[0] in RAX_FAMILY:
            known = None  # written some other way we don't trust to resolve
            continue
        # doesn't touch rax -- carry `known` forward unchanged
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


def main():
    bin_path = sys.argv[1]
    readelf_bin = os.environ.get('READELF_BIN', 'readelf')
    objdump_bin = os.environ.get('OBJDUMP_BIN', 'objdump')

    print(f"== syscall audit: {bin_path} ==")
    print("(heuristic linear-sweep disassembly -- see this script's header "
          "comment for exact method and limits)")

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
        fd, tmp_path = tempfile.mkstemp(suffix='.bin')
        try:
            with os.fdopen(fd, 'wb') as tf:
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
    for addr, nr in findings:
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

  echo "### self-test 1/2: clean_syscalls.asm -- expect AUDIT: PASS ###"
  INCLUDE="${INCLUDE:-$REPO_ROOT/vendor/fasmg-x86}" "$FASMG" "$clean_src" "$tmpdir/clean.out" >/dev/null
  chmod +x "$tmpdir/clean.out"
  local clean_rc=0
  audit_binary "$tmpdir/clean.out" || clean_rc=$?
  echo

  echo "### self-test 2/2: socket_syscall.asm -- expect AUDIT: FAIL (socket flagged) ###"
  INCLUDE="${INCLUDE:-$REPO_ROOT/vendor/fasmg-x86}" "$FASMG" "$socket_src" "$tmpdir/socket.out" >/dev/null
  chmod +x "$tmpdir/socket.out"
  local socket_rc=0
  audit_binary "$tmpdir/socket.out" || socket_rc=$?
  echo

  if [[ "$clean_rc" -eq 0 && "$socket_rc" -ne 0 ]]; then
    echo "SELF-TEST: PASS -- clean fixture audited PASS; socket fixture correctly caught and rejected"
    exit 0
  else
    echo "SELF-TEST: FAIL -- clean_rc=$clean_rc (expected 0), socket_rc=$socket_rc (expected nonzero)" >&2
    exit 1
  fi
}

main() {
  if [[ "${1:-}" == "--self-test" ]]; then
    self_test
    return
  fi

  local bin="${1:-}"
  if [[ -z "$bin" ]]; then
    echo "usage: $(basename "$0") <binary> | --self-test" >&2
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

  audit_binary "$bin"
}

main "$@"
