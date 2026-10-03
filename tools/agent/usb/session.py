#!/usr/bin/env python3
# tools/agent/usb/session.py  (bundled as lib/session.py)
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
"""session.py -- small Linux helpers for run.sh. Python 3 standard library only.

usage: session.py SUBCOMMAND [ARGS]

  fsinfo PATH            print `fstype=.. mountpoint=.. options=..` for the
                         mount holding PATH, from /proc/self/mountinfo, plus
                         `noexec=yes|no` and `fat=yes|no`
  ro-host EXEMPT...      remount every mount in this mount namespace
                         read-only (MS_REMOUNT|MS_BIND|MS_RDONLY, keeping its
                         other per-mount flags), except the EXEMPT mount
                         points and everything beneath them; then re-read
                         mountinfo and exit 1 if any non-exempt mount is
                         still read-write. Only meaningful inside a private
                         mount namespace (run.sh's stage 2); it changes
                         nothing outside it.
  lo-up                  bring `lo` up in this network namespace (ioctl, no
                         iproute2 needed), then print the interfaces this
                         namespace has; exit 1 unless that is exactly `lo`
  meminfo                print MemAvailable and MemTotal in bytes
  swapinfo               print every active swap device that is NOT zram
                         (pages of this session could be written there);
                         exit 1 if there is one
  wait-http URL SECS PID wait until URL answers HTTP 200, PID dies (exit 1),
                         or SECS pass (exit 1)
"""

import ctypes
import fcntl
import os
import socket
import struct
import sys
import time

MS_RDONLY, MS_NOSUID, MS_NODEV, MS_NOEXEC = 0x1, 0x2, 0x4, 0x8
MS_REMOUNT, MS_NOATIME, MS_NODIRATIME, MS_BIND = 0x20, 0x400, 0x800, 0x1000
MS_RELATIME, MS_STRICTATIME = 0x200000, 0x1000000
FLAG_OF = {"nosuid": MS_NOSUID, "nodev": MS_NODEV, "noexec": MS_NOEXEC,
           "noatime": MS_NOATIME, "nodiratime": MS_NODIRATIME,
           "relatime": MS_RELATIME, "strictatime": MS_STRICTATIME}


def unescape(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 3 < len(s) and s[i + 1:i + 4].isdigit():
            out.append(chr(int(s[i + 1:i + 4], 8)))
            i += 4
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def mountinfo():
    """[(mountpoint, per-mount options list, fstype, super options)] in file order."""
    rows = []
    with open("/proc/self/mountinfo", encoding="utf-8", errors="surrogateescape") as f:
        for line in f:
            left, _, right = line.rstrip("\n").partition(" - ")
            a, b = left.split(), right.split()
            rows.append((unescape(a[4]), a[5].split(","), b[0] if b else "?",
                         b[2] if len(b) > 2 else ""))
    return rows


def beneath(path, root):
    return path == root or root == "/" or path.startswith(root.rstrip("/") + "/")


def cmd_fsinfo(path):
    path = os.path.realpath(path)
    best = None
    for mp, opts, fstype, sopts in mountinfo():
        if beneath(path, mp) and (best is None or len(mp) >= len(best[0])):
            best = (mp, opts, fstype, sopts)
    if best is None:
        print("fstype=unknown mountpoint=unknown options= noexec=unknown fat=unknown")
        return 1
    mp, opts, fstype, sopts = best
    fat = fstype in ("vfat", "msdos", "fat", "exfat", "fuseblk")
    print("fstype=%s mountpoint=%s options=%s noexec=%s fat=%s" % (
        fstype, mp, ",".join(opts), "yes" if "noexec" in opts else "no",
        ("exfat" if fstype == "exfat" else "yes") if fat else "no"))
    return 0


def cmd_ro_host(exempt):
    libc = ctypes.CDLL(None, use_errno=True)
    exempt = [os.path.realpath(e) for e in exempt]
    failed = []
    seen = set()

    def top(rows):
        # An over-mounted mount point appears more than once; only the last
        # (topmost) entry is reachable by path, so only it is judged.
        last = {}
        for row in rows:
            last[row[0]] = row
        return [r for r in last.values()]

    for mp, opts, fstype, _ in top(mountinfo()):
        if any(beneath(mp, e) for e in exempt if e != "/"):
            continue
        seen.add(mp)
        if "ro" in opts:
            continue
        flags = MS_REMOUNT | MS_BIND | MS_RDONLY
        for o in opts:
            flags |= FLAG_OF.get(o, 0)
        if libc.mount(None, mp.encode("utf-8", "surrogateescape"), None,
                      ctypes.c_ulong(flags), None) != 0:
            failed.append("%s (%s): %s" % (mp, fstype, os.strerror(ctypes.get_errno())))
    still = [mp for mp, opts, _, _ in top(mountinfo())
             if "ro" not in opts and not any(beneath(mp, e) for e in exempt if e != "/")]
    total = len(seen)
    print("ro-host: %d mounts examined, %d still writable" % (total, len(still)))
    for f in failed:
        print("ro-host: remount failed: " + f)
    for mp in still:
        print("ro-host: STILL WRITABLE: " + mp)
    return 1 if still else 0


def cmd_lo_up():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        req = struct.pack("16sH22x", b"lo", 0)
        flags = struct.unpack("16sH22x", fcntl.ioctl(s, 0x8913, req))[1]   # SIOCGIFFLAGS
        fcntl.ioctl(s, 0x8914, struct.pack("16sH22x", b"lo", flags | 1))   # SIOCSIFFLAGS, IFF_UP
    finally:
        s.close()
    names = []
    with open("/proc/net/dev", encoding="ascii") as f:
        for line in f.readlines()[2:]:
            names.append(line.split(":", 1)[0].strip())
    print("interfaces in this network namespace: " + " ".join(names))
    return 0 if names == ["lo"] else 1


def meminfo():
    vals = {}
    with open("/proc/meminfo", encoding="ascii") as f:
        for line in f:
            k, _, v = line.partition(":")
            vals[k] = int(v.split()[0]) * 1024
    return vals


def cmd_meminfo():
    m = meminfo()
    print("MemAvailable=%d MemTotal=%d" % (m.get("MemAvailable", 0), m.get("MemTotal", 0)))
    return 0


def cmd_swapinfo():
    bad = []
    with open("/proc/swaps", encoding="utf-8") as f:
        for line in f.readlines()[1:]:
            dev = line.split()[0]
            if not dev.startswith("/dev/zram"):
                bad.append(dev)
    for d in bad:
        print("swap on disk: " + d)
    return 1 if bad else 0


def cmd_wait_http(url, secs, pid):
    import urllib.request
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    end = time.monotonic() + float(secs)
    while time.monotonic() < end:
        try:
            os.kill(int(pid), 0)
        except OSError:
            print("wait-http: the server process exited")
            return 1
        try:
            with opener.open(url, timeout=2) as r:
                if r.status == 200:
                    return 0
        except Exception:  # noqa: BLE001 -- not up yet
            pass
        time.sleep(0.5)
    print("wait-http: no HTTP 200 from %s within %s s" % (url, secs))
    return 1


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.stdout.write(__doc__)
        return 0
    cmd, args = argv[0], argv[1:]
    if cmd == "fsinfo" and len(args) == 1:
        return cmd_fsinfo(args[0])
    if cmd == "ro-host":
        return cmd_ro_host(args)
    if cmd == "lo-up" and not args:
        return cmd_lo_up()
    if cmd == "meminfo" and not args:
        return cmd_meminfo()
    if cmd == "swapinfo" and not args:
        return cmd_swapinfo()
    if cmd == "wait-http" and len(args) == 3:
        return cmd_wait_http(*args)
    sys.stderr.write("session.py: bad usage (try --help)\n")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
