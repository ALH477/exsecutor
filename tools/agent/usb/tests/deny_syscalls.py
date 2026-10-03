#!/usr/bin/env python3
# tools/agent/usb/tests/deny_syscalls.py
# SPDX-License-Identifier: GPL-3.0-or-later
"""deny_syscalls.py NR[,NR...] -- CMD [ARG...]

Selftest helper: exec CMD under a seccomp filter that makes the listed
x86-64 syscalls fail with EPERM and allows everything else. This is how a
container runtime's seccomp profile typically disables unshare(2), and how
selftest.sh simulates a host where unprivileged user namespaces are
unavailable (deny unshare, 272) or granted without capabilities (deny
mount, 165) -- from OUTSIDE the runner, with no test hook inside it."""
import ctypes
import os
import struct
import sys

if len(sys.argv) < 4 or sys.argv[2] != "--" or sys.argv[1] in ("-h", "--help"):
    sys.stdout.write(__doc__ + "\n")
    sys.exit(0 if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help") else 2)
deny = [int(x) for x in sys.argv[1].split(",")]
ins = [(0x20, 0, 0, 0)]                                    # A = nr
for i, nr in enumerate(deny):
    ins.append((0x15, len(deny) - i, 0, nr))               # == nr -> errno
ins.append((0x06, 0, 0, 0x7FFF0000))                       # allow
ins.append((0x06, 0, 0, 0x00050000 | 1))                   # ERRNO(EPERM)
blob = b"".join(struct.pack("<HBBI", *t) for t in ins)
buf = ctypes.create_string_buffer(blob, len(blob))


class Fprog(ctypes.Structure):
    _fields_ = [("len", ctypes.c_ushort), ("filter", ctypes.c_void_p)]


libc = ctypes.CDLL(None, use_errno=True)
prog = Fprog(len(ins), ctypes.addressof(buf))
if libc.prctl(38, 1, 0, 0, 0) != 0 or libc.prctl(22, 2, ctypes.byref(prog), 0, 0) != 0:
    sys.stderr.write("deny_syscalls: cannot install filter: %s\n" % os.strerror(ctypes.get_errno()))
    sys.exit(2)
os.execvp(sys.argv[3], sys.argv[3:])
