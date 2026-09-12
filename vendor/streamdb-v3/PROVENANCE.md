# StreamDB v3 test corpus — vendored as a certificate

A 24-document container written by the **real upstream C writer**, three
mechanically-corrupted copies of it, the 24 raw payloads, and a
machine-readable manifest of what the reference reader (the N64-embedded
StreamDB v3 reader Kiln uses) says about all of it. This is **program
output vendored as test data**, not code and not the programs that
produced it — the same standing as `vendor/hydramodem-tx/` and
`vendor/hydramodem-rx/`.

It exists so a StreamDB v3 reader written in Exsecutor can be certified
against a real writer's bytes without either the compiler's build closure
or its test runs ever invoking a C compiler, `/dev/urandom`, or a
third-party repository. §9.3 requires byte-identical output from
`(source, ego, lockfile, flags)`; "run the upstream C writer today" is
none of those, and (below) is not even reproducible on its own terms.

| file | sha256 |
|---|---|
| `corpus.streamdb` | `fbf0a08ae6e569be705d7725cfca32bfed586b7768914460355fe9b0b65e97b3` |
| `expectation.json` | `9ff2d50b6b2282c448d388c7792c603cecdea4a0c38592351f359d1c1f7362b7` |
| `negative/corpus.trunc300.streamdb` | `1ebb4dab63133a0d07d32ba285e039807cd94cdf215b061f3d93876cd731f293` |
| `negative/corpus.payload-flip.streamdb` | `38e2ebfdbcf9405322721ed0eb8f2caba512537ae622fc72d35905e1d40f63dc` |
| `negative/corpus.header-flip.streamdb` | `f34f2793074d9fd007851b475b9704d9e9fe3b4d5f721548ec7677da3cbb1c49` |
| `payloads/data/asset_004.bin` | `73ad91fbf94401c220e5a36b03305d171d09f2dd6a092baeafca4ffecc475f4c` |
| `payloads/data/asset_009.bin` | `ee92614bef9ecba8df6deb7aa42983bb21feb6f0ec23ba7db0b28ba3db1741ca` |
| `payloads/data/asset_014.bin` | `898554fd2f7dfe761d5a3696dacd35391c270d84338d5db598723052fe10d7b7` |
| `payloads/data/asset_019.bin` | `67d368ffcb7b2b5f6f2c1bea2af18588097403e0636a2d772b8678de57e4b1cc` |
| `payloads/models/asset_000.t3dm` | `5d8fcfefa9aeeb711fb8ed1e4b7d5c8a9bafa46e8e76e68aa18adce5a10df6ab` |
| `payloads/models/asset_005.t3dm` | `99c7ba1341452965effa8be873e1e5c618f1218673b45d3a64be92795eb3c058` |
| `payloads/models/asset_010.t3dm` | `8631f8a9bde93f3024a8d3078206e0c44394f036474468d0ee39d0a046131226` |
| `payloads/models/asset_015.t3dm` | `5aa61ccfd76d360355f5011b7537a374c771f658fd146c58db9a14f92cdf3af7` |
| `payloads/models/asset_020.t3dm` | `b78a456fd777640e03298f024f56a7c5f5d59c5b606e23970470de3a688ceac6` |
| `payloads/music/asset_003.xm64` | `4fd3473f8febc39f96d9a865ae0e1312b279e99430e37e344940d4a06974138f` |
| `payloads/music/asset_008.xm64` | `858a41586b8f3d94e699140af63d773e60a5af6800ba39efff1ed2505c7b8a7d` |
| `payloads/music/asset_013.xm64` | `ff8f6b5b6a92a28a2a54f62e05d3d7dd85d47926f3d93d86034105a777963de6` |
| `payloads/music/asset_018.xm64` | `6e17598e79e82a1727cee0cb33b12fb743bad1d49c5ea84db827738a5bc406d6` |
| `payloads/music/asset_023.xm64` | `adecb95cbec289ac7c4953b63c4b162e00d48c4281f25848cb17390cb78dc90d` |
| `payloads/sfx/asset_002.wav64` | `d2f5a975f8d547c22f7c4a8ee80401209e954b70bc2b91bbe8509ffe5b9986d2` |
| `payloads/sfx/asset_007.wav64` | `53dd2d09a3f32f94ccd36f91e717bdbfc62866f94bb9869a990bed57c2f8c576` |
| `payloads/sfx/asset_012.wav64` | `e9c49ea7715fee5f4b64f0b38e4a472ead272234d0d42504af7625a7decca83e` |
| `payloads/sfx/asset_017.wav64` | `dc1d538f29d658a09794c79137dcf6bd2e805dfd039b7fc89397828c401dd95c` |
| `payloads/sfx/asset_022.wav64` | `35762800d84aa634963fafceb75ca179ff7aadfed8a72c591cb48321df4b0223` |
| `payloads/sprites/asset_001.sprite` | `bbf3f11cb5b43e700273a78d12de55e4a7eab741ed2abf13787a4d2dc832b8ec` |
| `payloads/sprites/asset_006.sprite` | `89ca415d111b384c32a143d4f17a68898e9e423f9d8acbb22c73528c8e827738` |
| `payloads/sprites/asset_011.sprite` | `bcf608928181b471e6f67ebc1dd2b14b69a450c1edfec52848dfd18140cac141` |
| `payloads/sprites/asset_016.sprite` | `429c46fda003ee5439ea3f54d1a30adcaf17eab1e879c95bbf2f3c78fe445626` |
| `payloads/sprites/asset_021.sprite` | `1b308fb9503064fbd0f8e55840e7470dea04b2276482c05b324ad2c3cf47fb7f` |

Tree digest, `find . -type f ! -name PROVENANCE.md | sort | xargs sha256sum |
sha256sum` under `LC_ALL=C`, computed **from the repository root** (same
convention as every other `vendor/` tree — `sha256sum` embeds the path it
was given, so this differs from running it inside the directory):

```
1a07a7a4dba9ff71e9f71b66bf3e5e2125ee21e7a6fd5a25250fbb6d20ff8934
```

`flake.nix`'s `streamdb-vendor-integrity` check asserts this. `LC_ALL=C` is
pinned for the reason every other `PROVENANCE.md` in this repository pins
it: `sort`'s collation is locale-dependent, and a digest that changes with
the developer's `LANG` is not an integrity check.

Total size, 29 files: **186,522 bytes**.

## Source

- Upstream writer: `https://github.com/ALH477/DeMoD-StreamDB`
  Local origin: `/home/asher/Downloads/boot-intro/DeMoD-StreamDB`
  Commit: `e9a1a91f6f1feaec8c93f9616c82caa52a743876`, 2026-08-01 — verified
  clean (`git status --porcelain` reported nothing)
  Paths used: `C/include/streamdb.h`, `C/src/streamdb.c` (the writer)
- Reference reader (the oracle this corpus is certified against, and the
  reader Kiln embeds): `/home/asher/Documents/M64/streamdb-embedded/`
  Local repo: `/home/asher/Documents/M64` at `134e036f6e24ee6559765fd9ec7ebfb799315381`;
  `streamdb-embedded/` last touched at `826c90fd1f2bccbf714abad50d373233fef97a3e`
  (2026-08-22) — both verified clean
  Paths used: `include/streamdb_embedded.h`, `src/streamdb_embedded.c`,
  `src/streamdb_io_stdio.c`, `test/pack.c`, `test/roundtrip.c`, and the
  120-asset corpus recipe at `/home/asher/Documents/M64/nix/checks/streamdb.nix`
  (kinds, seed, size range — the shape this corpus borrows at a much
  smaller scale)

Neither tree is itself vendored here. This vendors only **output**:
containers the C writer produced, and the embedded reader's verdicts on
them. Nothing from either tree links into `exsc`.

## Licensing — precisely, because the upstream repo states it two ways

The upstream repository's single root-level `LICENSE` file (7,652 bytes)
is the full text of **GNU LGPL v3.0**. Read on its own, that would suggest
the whole repository is LGPL-3.0-only.

It is not that simple, and the repository says so itself:
`README.md` line 24: *"licensed under **LGPLv3** (Rust) / **LGPLv2.1+**
(C) to support broad FOSS and commercial adoption"*, and again explicitly
under `## License` (lines 137–142):

```
- Rust implementation → GNU Lesser General Public License v3.0
- C implementation   → GNU Lesser General Public License v2.1 or later
```

Every C source and header file's own comment header confirms the second
half of that split independently of the README, e.g.
`C/include/streamdb.h:8-9` and `C/src/streamdb.c` (identical boilerplate):

```
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
```

— which is the standard LGPL-2.1-or-later grant, not v3. There is no
separate `LICENSE` file under `C/`; the C edition's licence is established
only by this per-file header text plus the README's explicit statement,
never reconciled with the one root `LICENSE` file whose text is v3.

**Finding, precisely:** the repository ships mismatched licence
signalling — one root `LICENSE` file (LGPLv3 text, applicable to the Rust
edition per the README) and a second, textually-undocumented licence
(LGPL-2.1-or-later) governing the C edition, evidenced only by per-file
header comments and the README's own table. This corpus is produced
**entirely by the C edition** (`C/include/streamdb.h`, `C/src/streamdb.c`
via the writer built below), so **LGPL-2.1-or-later** is the licence that
applies to the code that produced these bytes, not the root `LICENSE`
file's v3 text. The reference reader, `streamdb-embedded/`, carries its
own unambiguous SPDX header (`SPDX-License-Identifier: LGPL-2.1-or-later`
on every file) that agrees with this finding.

**These are program outputs vendored as test data, not the programs that
produced them, and not relicensed.** No StreamDB source is vendored here —
only containers it wrote and the embedded reader's verdicts on them — and
they keep the **LGPL-2.1-or-later** identifier as a matter of provenance
discipline, the arrangement `vendor/hydramesh-wire/` and
`vendor/hydramodem-tx/` already have with their own upstreams. Do not add
this repository's `LICENSE.EXCEPTION` to anything under this directory:
nothing here is linked into `exsc`, and `LICENSE.EXCEPTION` applies only to
what `exsc` emits from a user's input.

## Non-reproducibility — measured, not assumed

**A `.streamdb` container is not byte-reproducible, even from identical
inputs on the identical build.** The writer mints each document's 16-byte
UUID from `/dev/urandom` (`C/src/streamdb.c`'s `doc_write`/id-generation
path) rather than deriving it from the key or content, and the index and
trie blobs are serialised in UUID order — so index bytes, trie bytes, and
every CRC32 downstream of either (`trie_crc`, `index_crc`, and each header
slot's own CRC, which covers the field carrying those two) differ from
run to run even with byte-identical keys and payloads.

Measured directly (scratch corpus, 3 documents, packed twice from the
identical `pack.c` command line):

```
$ ./pack a1.streamdb k0 files/f0 k1 files/f1 k2 files/f2
$ ./pack a2.streamdb k0 files/f0 k1 files/f1 k2 files/f2
$ cmp a1.streamdb a2.streamdb
a1.streamdb a2.streamdb differ: byte 33, line 1
$ cmp -l a1.streamdb a2.streamdb | wc -l
120
```

614 bytes total, 120 differing (≈19.5%). Field-by-field (parsed from both
files' header slot 0, the only slot either build ever validates — see
below):

| field (header slot 0) | a1 | a2 | stable? |
|---|---|---|---|
| magic, version, seq, offsets, lengths | identical | identical | yes |
| `trie_crc` (bytes 32..36) | `0xbf49216` | `0xb87fa05c` | **no** |
| `index_crc` (bytes 56..60) | `0x6400a507` | `0x43052452` | **no** |
| header's own CRC (bytes 72..76) | `0x74072f56` | `0x24853506` | **no** (downstream of the two above) |

And by region:

```
data records region (256..trie_off), size+CRC32(payload)+payload per doc, insertion order:  EQUAL
index blob (UUIDs + offsets/sizes/crcs, sorted by UUID):                                    DIFFERENT
trie blob (keyed on the same UUIDs):                                                         DIFFERENT
```

**What is stable:** the document records themselves — each document's
`size` (u32) + `CRC32(payload)` (u32) + `payload`, back to back in
insertion order, starting at byte 256 — are byte-identical run to run,
because they depend only on the payload content, not on any UUID. Header
slot 1 (the fresh-file initial commit, `seq=1`, all-zero body) is also
identical run to run — it is written before any document is inserted and
carries no document-derived data at all. **What is not stable:** the index
blob, the trie blob, and every CRC32 that covers either (`trie_crc`,
`index_crc`, and each subsequent slot's own header CRC) — all downstream
of the per-document UUIDs.

**Consequence for this vendoring:** `corpus.streamdb` and its two flipped
derivatives are generated **once** and frozen. `payloads/` and
`expectation.json` freeze the payloads and keys separately, in the
generator's own insertion order, so a reader implementation can be
certified against **content and semantics** (does key K read back to this
exact payload with this exact CRC; does this suffix search visit these
keys in this order) without ever depending on the specific UUIDs this one
build happened to draw. A second run of `corpus/gen.py` (printed below)
reproduces the payloads, keys and sizes exactly; a second run of the
writer over them would **not** reproduce `corpus.streamdb`'s bytes.

## A zero-length payload could not be produced, and is not in this corpus

The task this corpus was built for named a zero-length payload as an
edge case to cover. It is not covered, because it cannot be produced by
the reference writer: `streamdb_insert` rejects it unconditionally,
before anything reaches the wire format —

```c
if (!db || !key || key_len == 0 || !value || value_size == 0) {
    return STREAMDB_INVALID_ARG;
}
```

(`C/src/streamdb.c:1384`). Measured directly: packing this corpus with a
size-0 first document failed with `insert models/asset_000.t3dm failed: -3`
(`STREAMDB_INVALID_ARG`). The on-disk record format itself has no
difficulty representing `size=0` (a `u32` size field, a CRC32 of zero
bytes — `0x00000000` — and no payload bytes), but no genuine
reference-writer output can ever contain one. Rather than manufacture a
document by hand-editing bytes the writer itself refuses to produce
(which would stop this being writer-certified data), document index 0 was
given an ordinary size (33 bytes) instead. See `corpus/gen.py` below.

## Build

Built in a scratch directory, never inside either source tree.

**The writer** — the same recipe `/home/asher/Documents/M64/nix/checks/streamdb.nix`
uses (mirrored here so this corpus's own build is independently
reproducible against the pinned commit above):

```sh
gcc -O2 -std=gnu11 -I<upstream>/C/include -o pack \
    <M64>/streamdb-embedded/test/pack.c \
    <upstream>/C/src/streamdb.c -lpthread
```

**The reference reader (oracle)**:

```sh
gcc -O2 -std=gnu11 -Wall -Wextra -Werror -I<M64>/streamdb-embedded/include \
    -o roundtrip \
    <M64>/streamdb-embedded/test/roundtrip.c \
    <M64>/streamdb-embedded/src/streamdb_embedded.c \
    <M64>/streamdb-embedded/src/streamdb_io_stdio.c
```

Both built clean, `-Wall -Wextra -Werror` on the reader, no warnings on
either.

Compiler: `gcc (GCC) 14.3.0` (`gcc --version`, this build host — the same
compiler `vendor/hydramesh-wire/` and `vendor/hydramodem-tx/` record).

**A third tool, `pack2.c`** (scratch-only, never vendored, printed here so
anyone can re-derive its behaviour): `test/pack.c` calls `streamdb_flush`
exactly once, which — combined with `header_parse`'s own requirement that
`index_len >= 8` — means a single-flush container the roundtrip harness
reads only ever has ONE valid header slot (the fresh-file initial commit
in the other slot has `index_len=0` and never validates). Producing the
"reader must fall back to the other header slot" negative case for real
therefore needs a container with two genuinely valid commits, which needs
two `streamdb_flush()` calls. `pack2.c` does that — same
`streamdb_insert`/`streamdb_flush`/`streamdb_free` calls `pack.c` makes,
just split into two batches:

```c
/* Scratch-only harness (not vendored): pack a StreamDB in two flush()
 * batches so both header slots hold a valid commit, to exercise the
 * embedded reader's torn-write fallback. Links against the unmodified
 * upstream streamdb.c the same way test/pack.c does; this file is not
 * part of either source tree.
 *
 * usage: pack2 <out.streamdb> <n_first_batch> <key> <file> [<key> <file> ...]
 * The first n_first_batch pairs are inserted then flushed once; the rest
 * are inserted then flushed again; streamdb_free() follows (no 3rd flush,
 * since not dirty).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "streamdb.h"

static int put_file(StreamDB *db, const char *key, const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); return -1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(n ? n : 1);
    if (n && fread(buf, 1, n, f) != (size_t)n) { fclose(f); free(buf); return -1; }
    fclose(f);
    StreamDBStatus s = streamdb_insert(db, (const unsigned char*)key, strlen(key), buf, n);
    free(buf);
    if (s != STREAMDB_OK) { fprintf(stderr, "insert %s failed: %d\n", key, s); return -1; }
    printf("  packed %-28s %ld bytes\n", key, n);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: pack2 <out.streamdb> <n_first_batch> <key> <file> ...\n");
        return 2;
    }
    remove(argv[1]);
    int n_first = atoi(argv[2]);
    StreamDB *db = streamdb_init(argv[1], 0);
    if (!db) { fprintf(stderr, "streamdb_init failed\n"); return 1; }

    int pair = 0, i = 3;
    for (; i + 1 < argc && pair < n_first; i += 2, pair++)
        if (put_file(db, argv[i], argv[i+1]) != 0) return 1;

    if (streamdb_flush(db) != STREAMDB_OK) { fprintf(stderr, "flush1 failed\n"); return 1; }
    printf("-- flush 1 done (%d docs) --\n", pair);

    for (; i + 1 < argc; i += 2)
        if (put_file(db, argv[i], argv[i+1]) != 0) return 1;

    if (streamdb_flush(db) != STREAMDB_OK) { fprintf(stderr, "flush2 failed\n"); return 1; }
    printf("-- flush 2 done --\n");

    streamdb_free(db);
    printf("wrote %s\n", argv[1]);
    return 0;
}
```

`corpus.streamdb` was built with `pack2 corpus.streamdb 20 <48 key/file
args, in gen.py's order>` — the first 20 documents (index 0–19) in the
first `streamdb_flush()`, the remaining 4 (index 20–23) in the second.
Confirmed after the build (parsing both header slots): slot 0 holds
`seq=2` (20 documents), slot 1 holds `seq=3` (all 24 documents, the
newest — this is what every ordinary read of `corpus.streamdb` sees).

**A fourth tool, `list_suffix.c`** (scratch-only, never vendored): neither
`test/roundtrip.c` nor `streamdb_suffix_search`'s embedded-reader
counterpart prints per-match order, only a count — and the point of
`expectation.json`'s suffix-search section is the reader's actual
traversal order, not a predicted one. This dumps it, linking the
unmodified `streamdb_embedded.c`:

```c
/* Scratch-only harness (not vendored): dump streamdb_emb_find_suffix's
 * actual traversal order -- one line per match, in callback-visit order --
 * so the expectation manifest records what the embedded reader really
 * prints rather than a predicted order. Links against the unmodified
 * embedded reader (streamdb_embedded.c) exactly as test/roundtrip.c does;
 * this file lives only in scratch and is not part of either source tree.
 *
 * usage: list_suffix <db> <suffix>
 * prints, per match in traversal order: doc UUID (32 hex chars), size, crc32
 */
#define STREAMDB_EMB_BACKEND_STDIO 1
#include "streamdb_embedded.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int print_cb(const streamdb_emb_doc_t *d, void *u) {
    int *n = (int *)u;
    printf("%3d  ", *n);
    for (int i = 0; i < 16; i++) printf("%02x", d->id[i]);
    printf("  size=%u  crc=%08x  offset=%llu\n",
           d->size, d->crc, (unsigned long long)d->offset);
    (*n)++;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: list_suffix <db> <suffix>\n"); return 2; }

    streamdb_emb_io_t io;
    void *iostore = malloc(streamdb_emb_io_stdio_size());
    streamdb_emb_result_t r = streamdb_emb_io_stdio(&io, iostore, argv[1]);
    if (r != STREAMDB_EMB_OK) { printf("open io: %s\n", streamdb_emb_strerror(r)); return 1; }

    size_t need = 0;
    r = streamdb_emb_probe(&io, &need);
    if (r != STREAMDB_EMB_OK) { printf("probe: %s\n", streamdb_emb_strerror(r)); return 1; }

    void *arena = malloc(need);
    streamdb_emb_t db;
    r = streamdb_emb_open(&db, &io, arena, need);
    if (r != STREAMDB_EMB_OK) { printf("open db: %s\n", streamdb_emb_strerror(r)); return 1; }

    printf("documents=%u suffix=%s\n", db.doc_count, argv[2]);
    int n = 0;
    int found = streamdb_emb_find_suffix(&db, argv[2], strlen(argv[2]), print_cb, &n);
    printf("total matches=%d\n", found);
    return 0;
}
```

## Corpus construction

`corpus/gen.py` (scratch-only, never vendored; sha256
`c447a7023c4ddf959e134308a581d9b2c9952ef0b96673e033c234b16245c62c`) is
the single source of truth for keys, sizes and payload content — printed
here in full so the corpus's content (though not `corpus.streamdb`'s
literal bytes — see "Non-reproducibility" above) can be independently
re-derived:

```python
#!/usr/bin/env python3
"""Generate the frozen 24-document StreamDB v3 test corpus.

Deterministic content: payload byte j of document i is (i*37+j) & 0xff.
Keys are shaped like the Kiln/M64 corpus: "<kind>/asset_%03d.<ext>", kind
cycling over the same 5 kinds M64's nix/checks/streamdb.nix uses.

Sizes are chosen to cover: a 1-byte payload (i=1), sizes crossing an
8-byte boundary -- 7/8/9 (i=2,3,4), a payload over 3000 bytes (i=5, size
3001), and 19 ordinary sizes (i=0,6..23) computed by a fixed formula so
they are reproducible from this script alone.

A true zero-length payload (i=0's original role in this design) is NOT in
this corpus: the reference C writer's streamdb_insert() rejects it
unconditionally -- `if (!db || !key || key_len == 0 || !value ||
value_size == 0) return STREAMDB_INVALID_ARG;` (C/src/streamdb.c:1384),
before anything reaches the wire format. Measured directly: packing this
corpus with a size-0 first document failed with "insert ...: failed -3"
(STREAMDB_INVALID_ARG). The on-disk record format (u32 size + u32 CRC32 +
payload) has no difficulty representing size=0, but no genuine
reference-writer output can ever contain one, so this corpus does not
manufacture a document the writer itself refuses to produce. See
PROVENANCE.md.

Prints, to stdout: one line per document -- index, key, size, sha256, crc32
(IEEE 802.3 / zlib, matching streamdb_embedded.c's own algorithm) -- and
writes each payload to files/f%03d.
"""
import hashlib
import os
import zlib

KINDS = [("models", "t3dm"), ("sprites", "sprite"), ("sfx", "wav64"),
         ("music", "xm64"), ("data", "bin")]

EDGE_SIZES = {1: 1, 2: 7, 3: 8, 4: 9, 5: 3001}


def size_for(i):
    if i in EDGE_SIZES:
        return EDGE_SIZES[i]
    # 19 ordinary sizes for i = 0, 6..23, fixed formula, kept clear of the
    # edge-case sizes above by construction (20 + a range that never lands
    # on 1/7/8/9/3001).
    return 20 + ((i * 257 + 13) % 2950)


def main():
    os.makedirs("files", exist_ok=True)
    rows = []
    for i in range(24):
        kind, ext = KINDS[i % len(KINDS)]
        key = "%s/asset_%03d.%s" % (kind, i, ext)
        size = size_for(i)
        payload = bytes(((i * 37 + j) & 0xff) for j in range(size))
        path = "files/f%03d" % i
        with open(path, "wb") as f:
            f.write(payload)
        sha = hashlib.sha256(payload).hexdigest()
        crc = zlib.crc32(payload) & 0xffffffff
        rows.append((i, key, path, size, sha, crc))
        print("%3d  %-28s  %5d  %s  %08x" % (i, key, size, sha, crc))

    with open("manifest_rows.tsv", "w") as f:
        for i, key, path, size, sha, crc in rows:
            f.write("%d\t%s\t%s\t%d\t%s\t%08x\n" % (i, key, path, size, sha, crc))


if __name__ == "__main__":
    main()
```

24 documents, keys shaped like the M64/Kiln corpus (`<kind>/asset_%03d.<ext>`,
5 kinds cycling exactly as `nix/checks/streamdb.nix` uses: `models/t3dm`,
`sprites/sprite`, `sfx/wav64`, `music/xm64`, `data/bin`). Payload byte `j`
of document `i` is `(i*37+j) & 0xff` — deterministic, stated exactly, and
independent of any upstream randomness. Total payload bytes: **32,380** —
under the ~200 KB threshold named for this vendoring, so the raw payloads
are frozen alongside the container (`payloads/<key>`) rather than only
their hashes: a certificate can then compare bytes directly with no
re-derivation step, at a cost far below the budget.

| index | key | size | edge case |
|---|---|---|---|
| 0 | `models/asset_000.t3dm` | 33 | ordinary (see "zero-length" note above) |
| 1 | `sprites/asset_001.sprite` | 1 | 1-byte payload |
| 2 | `sfx/asset_002.wav64` | 7 | crosses an 8-byte boundary |
| 3 | `music/asset_003.xm64` | 8 | crosses an 8-byte boundary |
| 4 | `data/asset_004.bin` | 9 | crosses an 8-byte boundary |
| 5 | `models/asset_005.t3dm` | 3001 | over 3000 bytes |
| 6–23 | (5 kinds cycling) | 44–2860 | ordinary |

Full index → key/size/sha256/crc32 table: `expectation.json`'s
`documents` array (also reproduced by running `gen.py`, which prints the
same rows to stdout).

## Suffix search — captured, not predicted

`list_suffix.c` run against `corpus.streamdb` (the normal, uncorrupted
24-document commit):

```
$ ./list_suffix corpus.streamdb .t3dm
documents=24 suffix=.t3dm
  0  ...  crc=e4908305  (models/asset_000.t3dm)
  1  ...  crc=d3df1433  (models/asset_010.t3dm)
  2  ...  crc=67d61af5  (models/asset_020.t3dm)
  3  ...  crc=67024a12  (models/asset_005.t3dm)
  4  ...  crc=50b98bad  (models/asset_015.t3dm)
total matches=5

$ ./list_suffix corpus.streamdb .bin
documents=24 suffix=.bin
  0  ...  crc=20ddab93  (data/asset_004.bin)
  1  ...  crc=be4c9d78  (data/asset_014.bin)
  2  ...  crc=638dcc46  (data/asset_009.bin)
  3  ...  crc=9ad17160  (data/asset_019.bin)
total matches=4
```

(UUIDs and offsets elided here; crc32 resolves each match to a key via
`expectation.json`'s `documents` table — every document in this corpus has
a distinct size and a distinct crc32, so the resolution is unambiguous.
Full lines, with UUIDs, are in `expectation.json`'s `suffix_search`
section, already resolved to keys.)

**This order is exactly reverse-lexicographic by key** —
`streamdb-embedded/README.md`'s own description of why the format uses a
reverse trie — confirmed, not assumed: reversing each matching key's
digits (`000→000`, `005→500`, `010→010`, `015→510`, `020→020` for
`.t3dm`) and sorting the reversed strings ascending reproduces
`000, 010, 020, 005, 015` exactly, and the same check holds for `.bin`
(`004→400, 014→410, 009→900, 019→910` sorts to `004, 014, 009, 019`).

## Negative cases

All three are mechanical, byte-level corruptions of `corpus.streamdb`
itself — nothing here re-invokes the writer. Full construction (exact
file offsets and byte values) and the reference reader's exact result,
transcript excerpt and exit code for each: `expectation.json`'s
`negative_cases` section. Summary:

| file | corruption | reference result |
|---|---|---|
| `negative/corpus.trunc300.streamdb` | `head -c 300 corpus.streamdb` | both header slots' bounds checks fail against the 300-byte file → `STREAMDB_EMB_ERR_FORMAT` ("bad format"), exit 1 |
| `negative/corpus.payload-flip.streamdb` | one byte of `models/asset_010.t3dm`'s payload XORed with `0xFF` (file offset 11250) | `streamdb_emb_get` on that one key → `STREAMDB_EMB_ERR_CRC` ("checksum mismatch"); all 23 other documents read back correctly; exit 1 |
| `negative/corpus.header-flip.streamdb` | one byte of header slot 1 (the newest commit, `seq=3`) XORed with `0xFF` (file offset 136), breaking that slot's own trailing CRC | reader falls back to slot 0 (`seq=2`, 20 documents): the first 20 documents (index 0–19) read back correctly; the last 4 (index 20–23, only present in the newer commit) each return `STREAMDB_EMB_ERR_NOT_FOUND` ("not found"); `.t3dm` suffix matches drop from 5 to 4; exit 1 |

The header-flip case is the one that requires `corpus.streamdb` to have
been built with two `streamdb_flush()` calls (`pack2.c`, above) rather
than `pack.c`'s one — a single-flush container has only one header slot
that ever validates (see "Build" above), so corrupting it fails the whole
open rather than demonstrating the fallback. This corpus's own two-batch
construction (20 documents, flush, 4 more documents, flush) makes the
fallback a property of *this* container, not a separately fabricated one.

## `expectation.json`

Machine-readable manifest, JSON, at the repository-relative path
`vendor/streamdb-v3/expectation.json`. Top level:

- `document_count`: 24
- `documents`: array of `{index, key, size, sha256, crc32, payload_file}`,
  one per document, `payload_file` pointing at the frozen bytes under
  `payloads/`
- `suffix_search`: `.t3dm` and `.bin`, each an array of
  `{key, size, crc32}` **in the reference reader's actual traversal
  order**, plus a note on how that order was captured and independently
  confirmed (see above)
- `negative_cases`: keyed by the three files under `negative/`, each with
  `construction` (exact byte offset and value) and
  `reference_reader_result` (the precise result code(s), a transcript
  excerpt, and the exit code)

## Status in this repository

`[UNTESTED]` as an Exsecutor artifact. No StreamDB v3 reader written in
Exsecutor exists yet — the same standing `vendor/hydramesh-wire/` had
before an Exsecutor codec existed for it. This corpus is the fixture such
a reader would be certified against: byte-exact payload recovery, CRC
verification (including the two induced-failure cases), suffix-search
order, and the torn-write header fallback.
