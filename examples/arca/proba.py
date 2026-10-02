#!/usr/bin/env python3
# examples/arca/proba.py -- holds arca.exsc to GNU tar. Verification only
# (spec §18): never on the build closure. Usage: proba.py HOST [SEED]
#
# HOST is a build of proba.c against the emitted unit: tar on stdin, one
# "F name"/"D name" line per member, then ADMIT or REFUSE <v> @<off>.
#
# Three parts, every one required to see something:
#   1. reliquary's own output -- archives made with GNU tar and reliquary's
#      exact pack flags (archive.rs pack_tree) -- must be ADMITTED, and the
#      member list must equal `tar -tf`'s, in order.
#   2. a hostile corpus, each case REFUSED with its expected verdict.
#   3. a mutation fuzz: random byte flips in the headers of a real archive.
#      For every mutant the gate ADMITS, GNU tar must list exactly the members
#      the gate listed, every one a regular file or a directory, and exit 0.
#      That is the property that matters -- the gate may refuse more than
#      tar would, never admit something tar reads differently. A run in which
#      no mutant was admitted, or none refused, fails: it measured nothing.
import io, os, random, subprocess, sys, tarfile, tempfile

HOST = sys.argv[1]
SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 1
PACK = ["--sort=name", "--mtime=UTC 1970-01-01", "--owner=0", "--group=0", "--numeric-owner"]
fails = 0


def gate(data):
    r = subprocess.run([HOST], input=data, capture_output=True)
    lines = r.stdout.decode("utf-8", "surrogateescape").splitlines()
    return r.returncode, lines


def tar_list(data):
    # Names from `tar -tf` (one per line, exactly as stored: --quoting-style=
    # literal, and the gate never admits a newline), types from the first
    # column of `tar -tvf`. Parsing the name out of the -tv line instead
    # loses a leading space -- the harness's bug, which seed 4 found.
    with tempfile.NamedTemporaryFile(suffix=".tar") as f:
        f.write(data)
        f.flush()
        n = subprocess.run(["tar", "--quoting-style=literal", "-tf", f.name], capture_output=True)
        v = subprocess.run(["tar", "--quoting-style=literal", "-tvf", f.name], capture_output=True)
    names = n.stdout.decode("utf-8", "surrogateescape").split("\n")[:-1]
    types = [ln[0] for ln in v.stdout.decode("utf-8", "surrogateescape").splitlines()]
    rc = n.returncode or v.returncode
    if len(names) != len(types):
        return rc or 99, list(zip(types, names))
    return rc, list(zip(types, names))


def check(name, ok):
    global fails
    print(f"  [{'ok' if ok else 'FAIL'}]   {name}")
    fails += not ok


def benign():
    out = []
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "src")
        os.makedirs(os.path.join(src, "sub", "deeper"))
        for p, b in [("a.txt", b"hi\n"), ("empty", b""), ("sub/deeper/z", b"z" * 513),
                     ("sub/" + "n" * 150 + ".txt", b"x"), ("sub/ünï cødé.txt", b"u"),
                     ("sub/" + "q/" * 0 + "x" * 99, b"edge"), ("big", os.urandom(70000))]:
            with open(os.path.join(src, p), "wb") as f:
                f.write(b)
        os.makedirs(os.path.join(src, "d" * 120, "e" * 120))
        with open(os.path.join(src, "d" * 120, "e" * 120, "f"), "wb") as f:
            f.write(b"deep")
        out.append(("reliquary pack_tree flags",
                    subprocess.run(["tar", *PACK, "-C", d, "-cf", "-", "--", "src"],
                                   capture_output=True, check=True).stdout))
        out.append(("tar defaults",
                    subprocess.run(["tar", "-C", d, "-cf", "-", "src"],
                                   capture_output=True, check=True).stdout))
    return out


def raw(members, fmt=tarfile.GNU_FORMAT):
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode="w", format=fmt) as t:
        for name, typ, extra in members:
            ti = tarfile.TarInfo(name)
            ti.type = typ
            ti.mode = extra.get("mode", 0o644)
            ti.linkname = extra.get("link", "")
            data = extra.get("data", b"")
            ti.size = len(data) if typ == tarfile.REGTYPE else 0
            t.addfile(ti, io.BytesIO(data) if typ == tarfile.REGTYPE else None)
    return b.getvalue()


def header(name=b"f", typ=b"0", size=b"00000000000\0", magic=b"ustar  \0", chk=None, mode=b"0000644\0", mtime=b"00000000000\0"):
    h = bytearray(512)
    h[0:len(name)] = name
    h[100:108] = mode
    h[108:116] = b"0000000\0"
    h[116:124] = b"0000000\0"
    h[124:136] = size
    h[136:148] = mtime
    h[156:157] = typ
    h[257:265] = magic
    h[148:156] = b"        "
    s = sum(h)
    h[148:156] = chk if chk is not None else b"%06o\0 " % s
    return bytes(h)


END = bytes(1024)


def hostile():
    F = tarfile.REGTYPE
    D = tarfile.DIRTYPE
    return [
        ("block device", raw([("blk/", D, {}), ("blk/disk", tarfile.BLKTYPE, {})]), 18),
        ("char device", raw([("c", tarfile.CHRTYPE, {})]), 18),
        ("fifo", raw([("p", tarfile.FIFOTYPE, {})]), 18),
        ("symlink to /etc/shadow", raw([("l", tarfile.SYMTYPE, {"link": "/etc/shadow"})]), 18),
        ("hard link", raw([("a", F, {"data": b"x"}), ("h", tarfile.LNKTYPE, {"link": "a"})]), 18),
        ("pax header", raw([("p" * 120, F, {"data": b"x"})], tarfile.PAX_FORMAT), 17),
        ("posix ustar", raw([("u", F, {"data": b"x"})], tarfile.USTAR_FORMAT), 17),
        ("absolute name", header(b"/etc/passwd") + END, 20),
        ("dot-dot component", header(b"a/../../x") + END, 21),
        ("dot-dot alone", header(b"..") + END, 21),
        ("dot-dot last", header(b"a/..") + END, 21),
        ("backslash", header(b"a\\b") + END, 22),
        ("control byte", header(b"a\nb") + END, 22),
        ("empty name", header(b"") + END, 23),
        ("bad checksum", header(chk=b"000000\0 ") + END, 16),
        ("checksum not GNU-shaped", header(chk=b"0000000 ") + END, 16),
        ("size not octal", header(size=b"0000000009\0\0") + END, 19),
        ("size no NUL", header(size=b"000000000000") + END, 19),
        ("base-256 over 2^63", header(size=b"\x80\0\0\0\x80" + b"\0" * 7) + END, 19),
        ("directory with data", header(b"d/", b"5", b"00000000001\0") + bytes(512) + END, 26),
        ("file with trailing slash", header(b"f/") + END, 26),
        ("directory without slash", header(b"d", b"5") + END, 26),
        ("long name then end", header(b"././@LongLink", b"L", b"00000000002\0") + b"a\0".ljust(512, b"\0") + END, 25),
        ("long name twice", header(b"././@LongLink", b"L", b"00000000002\0") + b"a\0".ljust(512, b"\0")
         + header(b"././@LongLink", b"L", b"00000000002\0") + b"a\0".ljust(512, b"\0") + END, 24),
        ("long name without NUL", header(b"././@LongLink", b"L", b"00000000001\0") + b"a".ljust(512, b"\0")
         + header(b"f") + END, 24),
        ("long name with inner NUL", header(b"././@LongLink", b"L", b"00000000004\0") + b"a\0b\0".ljust(512, b"\0")
         + header(b"f") + END, 24),
        ("long name misnamed", header(b"x", b"L", b"00000000002\0") + b"a\0".ljust(512, b"\0") + header(b"f") + END, 24),
        ("long name hides dot-dot", header(b"././@LongLink", b"L", b"00000000005\0") + b"../x\0".ljust(512, b"\0")
         + header(b"innocent") + END, 21),
        ("mode not octal", header(mode=b"0p00644\0") + END, 27),
        ("mtime without NUL", header(mtime=b"000000000000") + END, 27),
        ("data after end", header(b"f") + END + b"\x01", 101),
        ("truncated", header(b"f", size=b"00000001000\0") + b"x" * 10, 100),
    ]


def mutate(rng, data, nhdr):
    b = bytearray(data)
    # header offsets: walk the real archive once
    offs, o = [], 0
    while o + 512 <= len(b) and any(b[o:o + 512]):
        offs.append(o)
        h = bytes(b[o:o + 512])
        sz = int(h[124:135] or b"0", 8) if h[124] != 0x80 else int.from_bytes(h[128:136], "big")
        o += 512 + ((sz + 511) // 512) * 512
    for _ in range(rng.randint(1, 3)):
        base = rng.choice(offs)
        i = base + rng.choice([rng.randrange(512), rng.choice([156, 124, 130, 135, 0, 1, 99, 257, 263])])
        b[i] = rng.randrange(256) if rng.random() < 0.5 else b[i] ^ (1 << rng.randrange(8))
    if rng.random() < 0.7:   # keep the checksum right so the fuzz reaches past it
        for base in offs:
            h = b[base:base + 512]
            h[148:156] = b"        "
            h[148:156] = b"%06o\0 " % sum(h)
            b[base:base + 512] = h
    return bytes(b)


print("arca: reliquary's own archives")
corpus = benign()
for name, data in corpus:
    rc, g = gate(data)
    trc, t = tar_list(data)
    gl = [(("d" if x[0] == "D" else "-"), x[2:]) for x in g[:-1]]
    check(f"{name}: ADMIT, {len(gl)} members == tar -tvf", rc == 0 and g[-1] == "ADMIT" and gl == t and trc == 0)

print("arca: hostile corpus")
for name, data, want in hostile():
    rc, g = gate(data)
    check(f"{name}: REFUSE {want}", rc == 1 and g[-1].startswith(f"REFUSE {want} "))

print("arca: mutation fuzz, gate-admitted => tar reads the same members")
rng = random.Random(SEED)
admitted = refused = disagree = 0
base_data = corpus[0][1]
for _ in range(3000):
    m = mutate(rng, base_data, 0)
    rc, g = gate(m)
    if rc != 0:
        refused += 1
        continue
    admitted += 1
    trc, t = tar_list(m)
    gl = [(("d" if x[0] == "D" else "-"), x[2:]) for x in g[:-1]]
    if trc != 0 or gl != t or any(ty not in "-d" for ty, _ in t):
        disagree += 1
        if disagree <= 3:
            print(f"    disagreement: gate {gl[:4]} tar rc={trc} {t[:4]}")
check(f"3000 mutants: {admitted} admitted, {refused} refused, {disagree} disagreements with GNU tar",
      admitted > 0 and refused > 0 and disagree == 0)

print(f"arca: {'PASS' if fails == 0 else 'FAIL'}")
sys.exit(1 if fails else 0)
