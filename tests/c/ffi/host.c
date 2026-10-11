/* tests/c/ffi/host.c -- the C host for tests/programs/ffi_externus/.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 DeMoD LLC.
 *
 * The first LIVE `externus` host: the unit emitted from
 * tests/programs/ffi_externus/ffi_externus.exsc imports `triplicare`
 * (declared `externus("C", abi: sysv_amd64)` in the Exsecutor source)
 * plus the one symbol every library-mode unit imports, `exsrt_abortus`.
 * This file defines both and a main() that calls the emitted
 * `exs_initium` the way tests/c/exsrt_shim.c does.
 *
 * `triplicare` is the whole point: a pure C function, u64 -> u64, no
 * state. If the Exsecutor side mis-implements the SysV AMD64 ABI for
 * the call (wrong argument register, wrong width, wrong return
 * register), initium's `r eq 42` is false and the process exits 1.
 */
#define _GNU_SOURCE 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint64_t triplicare(uint64_t x) { return x * 3; }

_Noreturn void exsrt_abortus(unsigned kind)
{
    fprintf(stderr, "exsecutor: abortus %u\n", kind);
    exit(2);
}

uint64_t exs_initium(unsigned char *p0);

static unsigned char g_mundus[64];

int main(void)
{
    (void)g_mundus;
    return (int)(exs_initium(g_mundus) & 0xFFu);
}
