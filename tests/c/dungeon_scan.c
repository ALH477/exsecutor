/* tests/c/dungeon_scan.c -- the dungeon generator, at scale, over its emitted C.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 The Exsecutor authors.
 *
 * VERIFICATION-ONLY, like exsrt_shim.c beside it: not part of any build, not
 * run by tests/run.sh. It exists because the properties examples/dungeon/
 * README.md states about the generator -- every chunk one connected component,
 * a solid border, no two chunks the same -- are claims about MILLIONS of
 * chunks, and tests/programs/dungeon/ runs twenty-odd. It is what turned
 * "the prune removes nothing, ever" (said after 3,000 seeds) into "the prune
 * fires in 283 of 2,000,000" (said after running this).
 *
 *   exsc aedifica --hospes x86_64-linux --emitte c examples/dungeon/dungeon.exsc -o d.c
 *   cc -std=gnu11 -O2 -o scan tests/c/dungeon_scan.c d.c
 *   ./scan WORLD FIRST COUNT HASHFILE      # e.g. ./scan 0 0 500000 h0.bin
 *
 * For each index in [FIRST, FIRST+COUNT) of WORLD it generates the chunk and
 * checks, with code that is NOT the library's:
 *   - every tile is 0..3 and the border is solid;
 *   - the returned walkable count equals a recount;
 *   - the chunk is exactly ONE 4-connected component (its own flood fill);
 * and it writes a 64-bit FNV-1a hash of the 4,096 bytes per chunk to HASHFILE,
 * so uniqueness can be checked across several runs (`sort | uniq -d` on the
 * hashes; a duplicate hash is a duplicate layout with probability 1 - 10^-7
 * over two million chunks).
 *
 * It prints, as the last line: count, violations, chunks the prune fired in,
 * the most tiles it pruned, min and max walkable, doors, traps. A line
 * `PR index pruned=N ...` is printed for each chunk the prune fired in, so a
 * seed with a given prune size can be picked out as a test vector, and an
 * `NC index comps=N` line for each chunk that is not one component.
 *
 * To measure what the prune is worth, build the same file against a unit
 * emitted from dungeon.exsc with the `fig_dungeon_prune` call replaced by `0`:
 * the `NC` lines are then exactly the `PR` lines of the real build.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint64_t exs_fig_dungeon_chunk(unsigned char *w, unsigned char *stats, uint64_t seed);
uint64_t exs_fig_dungeon_seed(uint64_t world, uint64_t index);

_Noreturn void exsrt_abortus(unsigned kind)
{
    fprintf(stderr, "abortus %u\n", kind);
    _Exit(70);
}

static unsigned char w[12288];
static uint64_t st[3];

/* 4-connected components of the non-wall tiles. */
static int components(const unsigned char *g)
{
    static unsigned char seen[4096];
    static int stk[4096];
    memset(seen, 0, sizeof seen);
    int comps = 0;
    for (int s = 0; s < 4096; s++) {
        if (!g[s] || seen[s]) continue;
        comps++;
        int sp = 0;
        stk[sp++] = s;
        seen[s] = 1;
        while (sp) {
            const int u = stk[--sp];
            const int x = u % 64, y = u / 64;
            const int nb[4] = { y > 0 ? u - 64 : -1, y < 63 ? u + 64 : -1,
                                x > 0 ? u - 1 : -1,  x < 63 ? u + 1 : -1 };
            for (int k = 0; k < 4; k++)
                if (nb[k] >= 0 && g[nb[k]] && !seen[nb[k]]) { seen[nb[k]] = 1; stk[sp++] = nb[k]; }
        }
    }
    return comps;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s WORLD FIRST COUNT HASHFILE\n", argv[0]);
        return 2;
    }
    const uint64_t world = strtoull(argv[1], 0, 0);
    const uint64_t first = strtoull(argv[2], 0, 0);
    const uint64_t count = strtoull(argv[3], 0, 0);
    FILE *hf = fopen(argv[4], "wb");
    if (!hf) { perror(argv[4]); return 2; }

    uint64_t violations = 0, prune_hits = 0, max_pruned = 0, min_walk = UINT64_MAX, max_walk = 0;
    uint64_t doors = 0, traps = 0;
    for (uint64_t i = first; i < first + count; i++) {
        const uint64_t seed = exs_fig_dungeon_seed(world, i);
        memset(w, 0xEE, sizeof w);                 /* the chunk owes nothing to its buffer */
        const uint64_t walk = exs_fig_dungeon_chunk(w, (unsigned char *)st, seed);

        uint64_t n = 0, h = UINT64_C(0xcbf29ce484222325);
        for (int k = 0; k < 4096; k++) {
            if (w[k] > 3) violations++;
            if (w[k]) n++;
            if (w[k] == 2) doors++;
            if (w[k] == 3) traps++;
            h = (h ^ w[k]) * UINT64_C(0x100000001b3);
        }
        for (int x = 0; x < 64; x++)
            if (w[x] || w[63 * 64 + x] || w[x * 64] || w[x * 64 + 63]) violations++;
        if (n != walk) violations++;
        const int cc = components(w);
        if (cc != 1) {
            violations++;
            printf("NC %llu comps=%d\n", (unsigned long long)i, cc);
        }
        if (st[2]) {
            prune_hits++;
            if (st[2] > max_pruned) max_pruned = st[2];
            printf("PR %llu pruned=%llu walk=%llu seed=%#llx\n", (unsigned long long)i,
                   (unsigned long long)st[2], (unsigned long long)walk, (unsigned long long)seed);
        }
        if (walk < min_walk) min_walk = walk;
        if (walk > max_walk) max_walk = walk;
        fwrite(&h, 8, 1, hf);
    }
    fclose(hf);
    printf("%llu %llu %llu %llu %llu %llu %llu %llu\n", (unsigned long long)count,
           (unsigned long long)violations, (unsigned long long)prune_hits,
           (unsigned long long)max_pruned, (unsigned long long)min_walk,
           (unsigned long long)max_walk, (unsigned long long)doors, (unsigned long long)traps);
    return 0;
}
