# vendor/fasmg-x86 — provenance

Third-party. Not Exsecutor code. Do not edit these files; re-vendor instead.

## What this is

The fasmg x86/x86-64 instruction-set and output-format macro package.

`fasmg` is architecture-neutral: the binary knows no machine instructions at
all. Given `mov eax, 60` with no includes it reports `Error: illegal
instruction.` The x86-64 instruction set, and the ELF64 executable writer, are
*macro packages* — ordinary fasmg source — and they live here.

The nixpkgs `fasmg` derivation installs **only `bin/fasmg`**. It does not ship
this package. Without this directory the compiler cannot be assembled.

## Upstream

| | |
|---|---|
| Project | flat assembler g (fasmg) |
| Author | Tomasz Grysztar |
| Version | `l8vn` |
| Archive | `https://flatassembler.net/fasmg.l8vn.zip` |
| Path taken | `examples/x86/include/**` (70 files), plus `license.txt` → `LICENSE.txt` |
| License | BSD-3-Clause — redistribution in source form permitted with notice retained (`LICENSE.txt`) |

## Hashes

Archive as fetched 2026-09-09:

```
sha256-FYe1KWy7hbTvlMRPeKuE0Tg3nEYLU+Gf92f83jjrS7E=
```

Vendored tree, content only (order- and permission-independent):

```
LC_ALL=C find vendor/fasmg-x86 -type f ! -name PROVENANCE.md | LC_ALL=C sort \
  | xargs sha256sum | sha256sum
3a21ac587fdb291667977aa3a2ccfb94257c39ccabf075b685086718c3b44b66
```

**`LC_ALL=C` is load-bearing, not decoration.** `sort`'s collation is
locale-dependent, and these filenames expose the difference — `LICENSE.txt`
against the lowercase names, and `avx512.inc` against `avx512_ifma.inc` and its
siblings, order differently under `en_US.UTF-8` than under `C`. The same tree
therefore hashes two different ways:

| collation | digest |
|---|---|
| `LC_ALL=C` (**canonical**) | `3a21ac587fdb291667977aa3a2ccfb94257c39ccabf075b685086718c3b44b66` |
| `en_US.UTF-8` | `9e17fab0e357c097d1b48969c50dc629fae5e3a0b2658151546c17b0ae90ea47` |

The first revision of this file recorded the second figure, taken from an
ambient shell. That was a defect, and an ironic one in this repo: §9.3 requires
byte-identical results across locales, and §1's thesis is that locale is a
capability rather than ambient state. An integrity check that silently reads the
developer's `LANG` is precisely the failure the language exists to make
unrepresentable. Both values are kept above so the discrepancy stays legible
rather than looking like tampering; `C` is canonical because it is the one that
does not depend on the machine it runs on.

`checks.vendor-integrity` in `flake.nix` pins `LC_ALL=C` and asserts the
canonical figure.

## Why vendored rather than fetched

**The upstream URL is not content-stable.** `flatassembler.net` serves a rolling
URL: the same `fasmg.l8vn.zip` link is re-published in place when the author
rebuilds, and the bytes change while the name does not. Measured, not assumed —
nixpkgs pins this archive at

```
sha256-/Izf7w7yofmPp1J85BgWbMLIGC4SGsCqXzhdecOo7CE=
```

and fetching that URL on 2026-09-09 yields `sha256-FYe1KW…`, a mismatch. Today
`nixpkgs#fasmg` builds anyway only because its *output* is substitutable from
`cache.nixos.org`. On a cold cache, it does not build.

A project whose §9.3 requires byte-identical output "at any phase" and whose §11
treats data versions as content-addressed dependencies cannot rest its only
build input on a mutable URL. Vendoring makes the include package
content-addressed by git, and the build offline-capable.

## Line endings — a deliberate exception

Upstream ships CRLF. 71 of the 72 files here contain CR; only `PROVENANCE.md`
(ours, and excluded from the digest) is LF.

`CLAUDE.md` requires all source in this repo to be LF, and says so pointedly:
"The tool that rejects CRLF should not ship with CRLF in it." This directory is
the exception, because the property that makes vendoring defensible is that the
tree is *byte-identical to upstream and provable by hash*. Normalising the line
endings would destroy exactly that.

`.gitattributes` pins `vendor/fasmg-x86/** -text` so git performs no EOL
conversion in either direction. Without it, a clone with `core.autocrlf=true`
would rewrite these bytes on checkout, the digest above would stop reproducing,
and the re-vendor diff would report all 71 files as changed. The exception is
therefore enforced, not merely noted.

fasmg itself is indifferent: the CRLF tree assembles the verified fixture to a
byte-identical binary.

## Re-vendoring

```sh
nix run .#vendor-fasmg-x86      # refetch, diff against this tree, report
```

A diff is a decision, not an error: read the upstream changelog, then either
accept the new tree and update the hashes above, or keep this one. Assembling
`compiler/` against a changed instruction-set package is a whole-tree change.
