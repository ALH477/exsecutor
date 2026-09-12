# flake.nix -- Exsecutor build and development environment.
#
# Exsecutor -- GPL-3.0-or-later WITH the output-and-runtime exception in
# LICENSE.EXCEPTION. Compiling with exsc places no GPL obligation on the
# compiled program. vendor/ is third-party and keeps its own license.
#
# Binding invariants (see /CLAUDE.md and docs/spec/exsecutor-spec-v0.4.md):
#
#   - spec §18: "The build closure is `{fasmg}`." A *package* build (fasmg-x86,
#     buildExsecutorPackage, exsc) depends on exactly fasmg plus the vendored
#     x86 macro package at vendor/fasmg-x86 (read-only third-party input --
#     see vendor/fasmg-x86/PROVENANCE.md). Verification-only tooling
#     (python3, binutils, ...) lives only in devShells, never in a package's
#     build inputs.
#   - spec §9.3: no network access at build time; byte-identical output.
#     Every `packages` and `checks` derivation below builds offline, inside
#     the Nix sandbox. (The one exception is the `vendor-fasmg-x86` app,
#     which explicitly needs the network to re-fetch upstream -- that is
#     exactly why it is an `app`, run by a human, and not a `check`.)
#   - spec §9.5 / §18.2: buildPlatform is pinned to x86_64-linux, deliberately
#     -- "the most Nix-aligned decision in the document" applied to the
#     compiler itself. One system, enumerated by hand: no flake-utils, no
#     eachDefaultSystem. Fewer inputs is the point.
#
# `compiler/x86_64/exsc.asm` exists and builds (Stage 1 closed 2026-09-10);
# the `compilerExists` guards below were written when it did not and stay
# because they cost nothing and keep the placeholder branch honest if the
# file is ever absent. `lib.buildExsecutorPackage` is still scaffolded: no
# .exsc compiles to an artifact yet (Stage 3). Nothing here fakes a successful compiler
# build.

{
  description = "Exsecutor -- a freestanding, Nix-native systems language. fasmg-based x86-64 bootstrap toolchain.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux"; # spec §18.2: buildPlatform pinned here, deliberately.
      pkgs = import nixpkgs { inherit system; };
      nixpkgsLib = pkgs.lib;

      # ----------------------------------------------------------------------
      # vendor/fasmg-x86, installed into the store.
      #
      # fasmg is architecture-neutral: the binary knows no machine
      # instructions at all until the x86-64 instruction-set and ELF64
      # writer macro packages are included. Those packages are not shipped
      # by nixpkgs' fasmg derivation (it installs only bin/fasmg) -- they are
      # vendored in this repo instead, and read-only from here (see
      # vendor/fasmg-x86/PROVENANCE.md for why: the upstream archive URL is
      # not content-stable). This derivation only copies that tree, verbatim,
      # into the store so package builds get a hermetic INCLUDE path. It does
      # not edit vendor/fasmg-x86, and neither should anything reached from
      # this file.
      fasmg-x86 = pkgs.stdenvNoCC.mkDerivation {
        pname = "fasmg-x86-includes";
        # Version of the fasmg dialect these includes are written against
        # (see vendor/fasmg-x86/PROVENANCE.md), not an independent release.
        version = "l8vn";
        src = ./vendor/fasmg-x86;
        dontConfigure = true;
        dontBuild = true;
        # Verbatim copy. PROVENANCE.md records a deliberate CRLF exception
        # for this tree (upstream ships CRLF; byte-identity to upstream is
        # the entire point of vendoring it) -- plain `cp` does not touch
        # line endings, so that exception survives into the store.
        installPhase = ''
          runHook preInstall
          mkdir -p "$out"
          cp -r --no-preserve=mode -- . "$out"/
          runHook postInstall
        '';
        meta = {
          description = "fasmg x86/x86-64 instruction-set and ELF64 executable-writer macro package (vendored from Tomasz Grysztar's fasmg.l8vn.zip; see vendor/fasmg-x86/PROVENANCE.md)";
          license = nixpkgsLib.licenses.bsd3;
          platforms = [ system ];
        };
      };

      fasmgPkg = pkgs.fasmg;

      # ----------------------------------------------------------------------
      # lib.buildExsecutorPackage -- scaffold, not the real thing yet.
      #
      # Named as a Stage 3 roadmap deliverable (spec §16: "buildExsecutorPackage.").
      # Its eventual *semantics* are shaped by the `ego` file (spec §10.1):
      # resolve `fontes` as content-addressed dependencies, honour
      # `hospites`/`potestates`, split `exitus` into lib/dev/doc outputs --
      # and, centrally, compile *Exsecutor* (.xsc) source by invoking `exsc`.
      # None of that is implementable today: `exsc` exists (this function
      # builds it, below) but has no `ego` reader, so there is nothing for
      # the function to resolve `fontes` or `potestates` from. (This comment
      # said "`exsc` does not exist" until the documentation pass that
      # corrected it.) Rather than fake it:
      #
      # [UNIMPLEMENTED]:
      #   - `fontes` dependency resolution against declared content hashes
      #     (spec §10.1, §11)
      #   - `hospites` / cross-compilation, i.e. an Exsecutor-level
      #     `targetPlatform` (spec §9.5's `--hospes`). Unrelated to, and not
      #     fixed by, this file's own `buildPlatform` pin to x86_64-linux.
      #   - `potestates` capability audit (spec §10.3)
      #   - `exitus` multi-output (lib/dev/doc) splitting (spec §10.1)
      #   - compiling Exsecutor source at all -- there is no `exsc` binary
      #     for this function to invoke.
      #
      # What this scaffold actually does today: assemble fasmg-dialect
      # source (an entry .asm file plus whatever it includes) into a
      # freestanding ELF64 binary, using vendor/fasmg-x86 as the INCLUDE
      # root. That is the one thing genuinely buildable right now -- and not
      # incidentally: it is also exactly the mechanism spec §18 describes for
      # building `exsc` itself ("Host language: x86-64 assembly. Assembler:
      # fasmg."). `packages.exsc` below is defined in terms of this function.
      #
      # Signature: `{ pname, version, src, ... }` as instructed, plus `main`
      # (the fasmg entry file, relative to `src`) since fasmg's own CLI is
      # exactly `fasmg source [output]` -- something has to name `source`.
      buildExsecutorPackage =
        { pname
        , version
        , src
        , main ? "${pname}.asm" # entry source file, relative to `src`
        , includeDir ? fasmg-x86 # INCLUDE search root; see vendor/fasmg-x86
        , meta ? { }
        , ...
        }@args:
        let
          passthroughArgs = builtins.removeAttrs args [ "pname" "version" "src" "main" "includeDir" "meta" ];
        in
        pkgs.stdenvNoCC.mkDerivation (passthroughArgs // {
          inherit pname version src meta;
          nativeBuildInputs = [ fasmgPkg ] ++ (passthroughArgs.nativeBuildInputs or [ ]);
          INCLUDE = "${includeDir}";
          dontConfigure = true;
          buildPhase = ''
            runHook preBuild
            fasmg ${nixpkgsLib.escapeShellArg main} ${nixpkgsLib.escapeShellArg pname}
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p "$out/bin"
            install -m 0755 ${nixpkgsLib.escapeShellArg pname} "$out/bin/${pname}"
            runHook postInstall
          '';
        });

      compilerSrcPath = ./compiler/x86_64/exsc.asm;
      # NOTE: under `nix flake check`/`nix build` without --impure, this also
      # reads false for a file that exists on disk but is not yet tracked by
      # git -- flakes filter their source to the tracked tree. Once exsc.asm
      # exists AND is `git add`-ed (even just `-N`), this flips to true under
      # plain evaluation too -- no flake.nix change needed then.
      #
      # That same filter is load-bearing for `checks.test` below, and it once
      # hid a real defect. The check stages ./compiler (added 2026-09-09)
      # because 14 of 19 tests/unit fixtures `include` ../../compiler/x86_64/
      # by relative path. Before that line existed the check still passed --
      # not because the includes resolved, but because only 4 of the 19
      # fixtures were git-tracked, and those 4 are exactly the ones that touch
      # no compiler/ path. A green check that cannot see what it is checking
      # is the failure mode to watch for here: staging a directory is only
      # half the fix, the files must also be tracked. tests/run.sh carries a
      # fixture-count floor for this reason.
      compilerExists = builtins.pathExists compilerSrcPath;

      exscPkg =
        if compilerExists then
          buildExsecutorPackage {
            pname = "exsc";
            version = "0.0.0-unreleased"; # no versioning scheme exists yet; honest placeholder
            src = ./compiler/x86_64;
            main = "exsc.asm";
            meta = {
              description = "Exsecutor compiler (exsc) -- freestanding x86-64, assembled by fasmg";
              # Plain GPL-3.0-or-later is correct for the compiler itself. The
              # output grant (Exception A, LICENSE.EXCEPTION) has no registered
              # SPDX id, so it cannot be expressed here -- it is prose, and an
              # auditor must read the file. Exception B (Classpath, per-file
              # designation) DOES have one, but applies only to target-runtime
              # files, none of which exist yet.
              # See docs/decisions/0006-license-gpl3-with-exception.md.
              license = nixpkgsLib.licenses.gpl3Plus;
              platforms = [ system ];
            };
          }
        else
          pkgs.stdenvNoCC.mkDerivation {
            name = "exsc-not-yet-written";
            dontUnpack = true;
            buildCommand = ''
              echo "exsc: compiler/x86_64/exsc.asm does not exist yet." >&2
              echo "The Exsecutor compiler is not written (spec Stage 1 is not complete)." >&2
              echo "This is a deliberate failure, not a build error: see /CLAUDE.md and" >&2
              echo "docs/spec/exsecutor-spec-v0.4.md section 18. Nothing here fakes a" >&2
              echo "successful compiler build." >&2
              exit 1
            '';
            meta = {
              description = "placeholder: compiler/x86_64/exsc.asm does not exist yet";
              license = nixpkgsLib.licenses.gpl3Plus;   # see LICENSE.EXCEPTION
              # Deliberately NOT `broken = true`: nixpkgs' mkDerivation refuses to
              # even evaluate a broken-marked package (an assert in
              # lib/customisation.nix) unless the caller sets
              # `config.allowBroken`, which replaces this derivation's own clear
              # stderr message with a generic Nix config error before it ever
              # runs (confirmed empirically while writing this file). A plain
              # failing `buildCommand` (above) is the honest choice instead:
              # `nix build .#exsc` reaches this message and fails with it,
              # rather than failing on an unrelated config error first.
            };
          };

      # ----------------------------------------------------------------------
      # Fixtures shared by the checks below.
      smokeAsmSrc = pkgs.writeText "exsecutor-smoke.asm" ''
        include 'format/format.inc'

        format ELF64 executable 3
        entry start

        segment readable executable
          start:
            mov eax,1
            mov edi,1
            lea rsi,[msg]
            mov edx,msg.len
            syscall
            mov eax,60
            xor edi,edi
            syscall

        segment readable writeable
          msg db 'exsecutor: fasmg toolchain live',10
          .len = $ - msg
      '';

      buildExsecutorPackageSmokeSrc = pkgs.writeTextDir "smoke-pkg.asm" ''
        include 'format/format.inc'

        format ELF64 executable 3
        entry start

        segment readable executable
          start:
            mov eax,1
            mov edi,1
            lea rsi,[msg]
            mov edx,msg.len
            syscall
            mov eax,60
            xor edi,edi
            syscall

        segment readable writeable
          msg db 'exsecutor: buildExsecutorPackage smoke',10
          .len = $ - msg
      '';

      buildExsecutorPackageSmokePkg = buildExsecutorPackage {
        pname = "exsecutor-build-lib-smoke";
        version = "0.0.0";
        src = buildExsecutorPackageSmokeSrc;
        main = "smoke-pkg.asm";
      };

      # Recorded in vendor/fasmg-x86/PROVENANCE.md:
      #   find vendor/fasmg-x86 -type f ! -name PROVENANCE.md | sort | xargs sha256sum | sha256sum
      # -> 9e17fab0e357c097d1b48969c50dc629fae5e3a0b2658151546c17b0ae90ea47
      #
      # Verified by hand outside Nix (2026-09-09) that this reproduces --
      # UNDER THE INVOKING SHELL'S AMBIENT LOCALE. It does not reproduce
      # under `LC_ALL=C`: `sort`'s collation order for these filenames
      # (`LICENSE.txt` vs lowercase names; `avx512.inc` vs
      # `avx512_ifma.inc`-style names with underscores) depends on locale,
      # so the byte order `sha256sum` hashes over is not locale-independent.
      # Under `LC_ALL=C` -- which is what a Nix build sandbox uses regardless
      # (stdenv sets it, precisely for reproducibility, and the sandbox
      # normally has no other locale data available to fall back to) -- the
      # identical recipe instead produces:
      #   3a21ac587fdb291667977aa3a2ccfb94257c39ccabf075b685086718c3b44b66
      # Both figures are measured, not assumed -- reproduced independently,
      # interactively and inside this flake's own sandboxed check.
      #
      # This flake does not own PROVENANCE.md and does not edit its recorded
      # value. But it also will not pull in a locale-data package just to
      # reproduce a result that depends on ambient locale -- that is exactly
      # the kind of dependency spec §9.3 rules out ("byte-identical output
      # ... across ... locales"). So this check verifies the LC_ALL=C figure,
      # pins LC_ALL=C explicitly rather than relying on the sandbox default,
      # and reports both numbers on every run so the discrepancy stays
      # visible instead of silently going stale in either direction. Report
      # to whoever owns vendor/fasmg-x86/PROVENANCE.md: its recorded command
      # is locale-sensitive and its own recipe likely wants `LC_ALL=C` added.
      vendorIntegrityProvenanceDigest = "9e17fab0e357c097d1b48969c50dc629fae5e3a0b2658151546c17b0ae90ea47";
      vendorIntegrityExpectedDigest = "3a21ac587fdb291667977aa3a2ccfb94257c39ccabf075b685086718c3b44b66"; # LC_ALL=C

      # vendor/hydramesh-wire: the DCF DeModFrame spec plus its 246-vector
      # golden certificate (ADR 0011). Reference DATA, not code -- nothing here
      # links into exsc, and the files keep their own LGPL-3.0-only identifier.
      # Same digest discipline as fasmg-x86 above: PROVENANCE.md excluded,
      # LC_ALL=C pinned, because a digest that moves with the developer's LANG
      # is not an integrity check.
      wireVendorDigest = "770382f1f32b672fdee15a0d54d7231ba38729dd01c5494ec75c29f39a62d5e5"; # LC_ALL=C

      # vendor/hydramodem-tx: three WAVs rendered by HydraModem's own reference
      # transmitter (frame_tx, reference DSP, default profile) for the three
      # frames vendor/hydramodem-tx/PROVENANCE.md names, symbola_basis.bin,
      # the symbol streams of 137 further renders (the modem design's D9
      # basis) reduced by the rule PROVENANCE.md prints, and profiles/, the
      # M4 renders of the same three frames under the aux-cable-cli, 4-FSK,
      # 8-FSK and 125-baud profiles (PROVENANCE.md's "Profiles (M4)"
      # section). Program OUTPUT vendored as a test certificate, not code --
      # nothing here links into exsc, and the files keep their upstream
      # LGPL-3.0-only identifier. Same digest discipline as fasmg-x86 and
      # hydramesh-wire above: PROVENANCE.md excluded, LC_ALL=C pinned.
      modemVendorDigest = "f3d58691816242e1e0de86d60ef2defb7a323a6c85551c12a2e3f96f721c6ac2"; # LC_ALL=C

      # vendor/hydramodem-rx: seventy impaired WAVs made from the three above
      # (white noise, sample-clock offsets, carrier-frequency offsets) and
      # verdicta.tsv, HydraModem's own frame_rx verdict on each --
      # vendor/hydramodem-rx/PROVENANCE.md has the generator verbatim, the
      # integers every vector is a function of, and the per-file digests. The
      # OTHER kind of evidence from hydramodem-tx: that tree is what the
      # reference produces, this one is what it judges (ADR 0014 decision 2).
      # Program output vendored as a test certificate, not code; nothing here
      # links into exsc and the files keep upstream's LGPL-3.0-only
      # identifier. Same digest discipline as the three above: PROVENANCE.md
      # excluded, LC_ALL=C pinned.
      rxVendorDigest = "4d8769c2a544057600d7271cfc75bac58dfe2801d97a9486eb6dbcddcc9ca05e"; # LC_ALL=C
    in
    {
      packages.${system} = {
        inherit fasmg-x86;
        fasmg = fasmgPkg;
        exsc = exscPkg;
        # exsc doesn't exist yet; fasmg-x86 is the most meaningful thing this
        # repo actually builds today (fasmg itself is just nixpkgs, unchanged).
        default = if compilerExists then exscPkg else fasmg-x86;
      };

      lib = { inherit buildExsecutorPackage; };

      devShells.${system}.default = pkgs.mkShell {
        packages = [
          pkgs.fasmg
          pkgs.python3 # verification-only: design probes (spec §18), never on the build closure
          pkgs.binutils # verification-only: readelf/objdump, needed by `make audit`
          pkgs.gnumake
          pkgs.file
        ];
        shellHook = ''
          REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
          export INCLUDE="$REPO_ROOT/vendor/fasmg-x86"
          FASMG_BANNER="$(fasmg 2>&1 | head -n1)"
          echo "exsecutor devShell -- $FASMG_BANNER"
          echo "INCLUDE=$INCLUDE"
        '';
      };

      checks.${system} = {
        # Assembles the verified smoke fixture inside a sandboxed derivation
        # (store INCLUDE path, no network, no ambient state) and checks that
        # the toolchain is actually live: it runs, exits 0, produces the
        # expected output, and the binary is genuinely freestanding (no
        # dynamic section, no PT_INTERP).
        smoke = pkgs.stdenvNoCC.mkDerivation {
          name = "check-fasmg-toolchain-smoke";
          nativeBuildInputs = [ fasmgPkg pkgs.binutils pkgs.coreutils ];
          INCLUDE = "${fasmg-x86}";
          dontUnpack = true;
          buildCommand = ''
            set -e
            cp ${smokeAsmSrc} smoke.asm
            fasmg smoke.asm smoke
            chmod +x smoke

            echo "== size =="
            stat -c '%s bytes' smoke

            echo "== run =="
            set +e
            smoke_stdout="$(./smoke)"
            smoke_exit=$?
            set -e
            if [ "$smoke_exit" -ne 0 ]; then
              echo "FAIL: smoke binary exited $smoke_exit, expected 0" >&2
              exit 1
            fi
            if [ "$smoke_stdout" != "exsecutor: fasmg toolchain live" ]; then
              echo "FAIL: unexpected stdout: $smoke_stdout" >&2
              exit 1
            fi
            echo "stdout ok: $smoke_stdout"

            echo "== readelf -d (expect: no dynamic section) =="
            dyn="$(readelf -d ./smoke 2>&1)"
            echo "$dyn"
            case "$dyn" in
              *"There is no dynamic section"*) ;;
              *) echo "FAIL: expected no dynamic section" >&2; exit 1 ;;
            esac

            echo "== readelf -l (expect: no PT_INTERP -- no dynamic linker) =="
            phdrs="$(readelf -l ./smoke 2>&1)"
            echo "$phdrs"
            case "$phdrs" in
              *INTERP*) echo "FAIL: PT_INTERP present -- not freestanding" >&2; exit 1 ;;
              *) ;;
            esac

            mkdir -p "$out"
            cp smoke "$out"/smoke
            echo pass > "$out"/PASS
          '';
        };

        # Exercises lib.buildExsecutorPackage end to end (not just "does it
        # evaluate"): builds a trivial fasmg package through it and runs the
        # result, inside the sandbox, no network.
        buildExsecutorPackage-smoke = pkgs.stdenvNoCC.mkDerivation {
          name = "check-buildExsecutorPackage-smoke";
          nativeBuildInputs = [ pkgs.coreutils ];
          dontUnpack = true;
          buildCommand = ''
            set -e
            smoke_stdout="$(${buildExsecutorPackageSmokePkg}/bin/exsecutor-build-lib-smoke)"
            echo "$smoke_stdout"
            if [ "$smoke_stdout" != "exsecutor: buildExsecutorPackage smoke" ]; then
              echo "FAIL: unexpected output from buildExsecutorPackage-built binary" >&2
              exit 1
            fi
            mkdir -p "$out"
            echo pass > "$out"/PASS
          '';
        };

        # Verifies the vendored tree still hashes to a known value, using
        # PROVENANCE.md's own recipe (find | sort | xargs sha256sum |
        # sha256sum), staged at the same relative path ("vendor/fasmg-x86")
        # the recorded command used, since the recipe is path-prefix-
        # sensitive by construction.
        #
        # The digest asserted here is the LC_ALL=C figure, not the one
        # written in PROVENANCE.md -- measured to differ from it because
        # `sort`'s collation is locale-dependent for these filenames. See the
        # long comment on vendorIntegrityExpectedDigest above for the
        # measurement and why this check pins LC_ALL=C rather than chasing
        # PROVENANCE.md's ambient-locale figure. Both numbers are echoed
        # below on every run.
        vendor-integrity = pkgs.stdenvNoCC.mkDerivation {
          name = "check-vendor-fasmg-x86-integrity";
          nativeBuildInputs = [ pkgs.coreutils pkgs.findutils ];
          dontUnpack = true;
          buildCommand = ''
            set -e
            export LC_ALL=C
            mkdir -p work/vendor
            cp -r --no-preserve=mode -- ${./vendor/fasmg-x86} work/vendor/fasmg-x86
            cd work
            actual="$(find vendor/fasmg-x86 -type f ! -name PROVENANCE.md | sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"
            echo "PROVENANCE.md recorded (ambient-locale sort):  ${vendorIntegrityProvenanceDigest}"
            echo "this check asserts (LC_ALL=C sort):            ${vendorIntegrityExpectedDigest}"
            echo "actual (LC_ALL=C, this sandbox):               $actual"
            if [ "$actual" != "${vendorIntegrityExpectedDigest}" ]; then
              echo "" >&2
              echo "FAIL: vendor/fasmg-x86 content hash (LC_ALL=C) does not match the" >&2
              echo "value this check expects. This flake does not own PROVENANCE.md and" >&2
              echo "does not edit its recorded hash from here -- report the mismatch." >&2
              exit 1
            fi
            mkdir -p "$out"
            echo "$actual" > "$out"/digest
          '';
        };

        # The unit fixtures and the syscall audit, run hermetically -- the same
        # scripts `make test` and `make audit` invoke, staged into a repo-shaped
        # tree so their own SCRIPT_DIR/.. root resolution works unchanged.
        #
        # These take python3 and binutils as *check* inputs. That does not widen
        # the build closure: `packages.exsc` still builds from fasmg plus the
        # vendored macro package and nothing else. A check is verification, not a
        # shipped artifact, which is exactly the line spec §18.1 draws.
        wire-vendor-integrity = pkgs.stdenvNoCC.mkDerivation {
          name = "check-vendor-hydramesh-wire-integrity";
          nativeBuildInputs = [ pkgs.coreutils pkgs.findutils ];
          dontUnpack = true;
          buildCommand = ''
            set -e
            export LC_ALL=C
            mkdir -p work/vendor
            cp -r --no-preserve=mode -- ${./vendor/hydramesh-wire} work/vendor/hydramesh-wire
            cd work
            actual="$(find vendor/hydramesh-wire -type f ! -name PROVENANCE.md | sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"
            echo "expected (LC_ALL=C): ${wireVendorDigest}"
            echo "actual   (LC_ALL=C): $actual"
            if [ "$actual" != "${wireVendorDigest}" ]; then
              echo "" >&2
              echo "FAIL: vendor/hydramesh-wire content hash does not match." >&2
              echo "These files are copied verbatim from upstream and are never" >&2
              echo "edited here. A mismatch means a local edit or a re-vendor;" >&2
              echo "report it rather than updating this digest to match." >&2
              exit 1
            fi
            mkdir -p "$out"
            echo "$actual" > "$out"/digest
          '';
        };

        # Same check as wire-vendor-integrity above, for vendor/hydramodem-tx
        # (ADR-adjacent to 0011: the transmitter-output certificate, not the
        # wire-format spec itself). See vendor/hydramodem-tx/PROVENANCE.md.
        modem-vendor-integrity = pkgs.stdenvNoCC.mkDerivation {
          name = "check-vendor-hydramodem-tx-integrity";
          nativeBuildInputs = [ pkgs.coreutils pkgs.findutils ];
          dontUnpack = true;
          buildCommand = ''
            set -e
            export LC_ALL=C
            mkdir -p work/vendor
            cp -r --no-preserve=mode -- ${./vendor/hydramodem-tx} work/vendor/hydramodem-tx
            cd work
            actual="$(find vendor/hydramodem-tx -type f ! -name PROVENANCE.md | sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"
            echo "expected (LC_ALL=C): ${modemVendorDigest}"
            echo "actual   (LC_ALL=C): $actual"
            if [ "$actual" != "${modemVendorDigest}" ]; then
              echo "" >&2
              echo "FAIL: vendor/hydramodem-tx content hash does not match." >&2
              echo "These files are HydraModem reference-transmitter output," >&2
              echo "vendored verbatim and never edited here. A mismatch means" >&2
              echo "a local edit or a re-vendor; report it rather than" >&2
              echo "updating this digest to match." >&2
              exit 1
            fi
            mkdir -p "$out"
            echo "$actual" > "$out"/digest
          '';
        };

        # Same check again, for vendor/hydramodem-rx (ADR 0014 decision 2: the
        # impaired set and the reference receiver's verdict on each). Its own
        # check and not an extension of modem-vendor-integrity above, because
        # the two trees are re-vendored for different reasons and a single
        # digest over both would not say which moved.
        rx-vendor-integrity = pkgs.stdenvNoCC.mkDerivation {
          name = "check-vendor-hydramodem-rx-integrity";
          nativeBuildInputs = [ pkgs.coreutils pkgs.findutils ];
          dontUnpack = true;
          buildCommand = ''
            set -e
            export LC_ALL=C
            mkdir -p work/vendor
            cp -r --no-preserve=mode -- ${./vendor/hydramodem-rx} work/vendor/hydramodem-rx
            cd work
            actual="$(find vendor/hydramodem-rx -type f ! -name PROVENANCE.md | sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"
            echo "expected (LC_ALL=C): ${rxVendorDigest}"
            echo "actual   (LC_ALL=C): $actual"
            if [ "$actual" != "${rxVendorDigest}" ]; then
              echo "" >&2
              echo "FAIL: vendor/hydramodem-rx content hash does not match." >&2
              echo "These are impaired WAVs derived from HydraModem's own" >&2
              echo "transmitter output by the integer generator printed in" >&2
              echo "PROVENANCE.md, plus the reference receiver's verdict on" >&2
              echo "each. They are never edited here. A mismatch means a local" >&2
              echo "edit or a re-vendor; report it rather than updating this" >&2
              echo "digest to match -- the verdicts are attached to THESE" >&2
              echo "bytes and mean nothing attached to others." >&2
              exit 1
            fi
            mkdir -p "$out"
            echo "$actual" > "$out"/digest
          '';
        };

        test = pkgs.stdenvNoCC.mkDerivation {
          name = "check-unit-tests";
          nativeBuildInputs = [
            fasmgPkg pkgs.bash pkgs.python3 pkgs.binutils
            pkgs.coreutils pkgs.gnugrep pkgs.gawk pkgs.diffutils
          ];
          dontUnpack = true;
          buildCommand = ''
            set -e
            mkdir -p repo/vendor
            cp -r --no-preserve=mode -- ${./tests} repo/tests
            cp -r --no-preserve=mode -- ${./tools} repo/tools
            cp -r --no-preserve=mode -- ${./compiler} repo/compiler
            cp -r --no-preserve=mode -- ${./examples} repo/examples
            cp -r --no-preserve=mode -- ${./vendor/fasmg-x86} repo/vendor/fasmg-x86
            # §14 entry 23's expected stream is built from the vendored
            # certificate (tests/conformance/entry23/expecta.py reads
            # vendor/hydramesh-wire/golden_vectors.json); without it here the
            # cert branch would have nothing to compare against.
            cp -r --no-preserve=mode -- ${./vendor/hydramesh-wire} repo/vendor/hydramesh-wire
            # tests/programs/hydramodem_*/ compare stdout against these
            # reference files (stdout=vendor/hydramodem-tx/<name>); without
            # them here those tests would have nothing to compare against.
            cp -r --no-preserve=mode -- ${./vendor/hydramodem-tx} repo/vendor/hydramodem-tx
            # tests/programs/receptio_vec_*/ read these seventy impaired WAVs
            # on stdin (stdin=vendor/hydramodem-rx/<kind>/<name>.wav); without
            # them here those directories would have nothing to decode.
            cp -r --no-preserve=mode -- ${./vendor/hydramodem-rx} repo/vendor/hydramodem-rx
            chmod +x repo/tests/run.sh repo/tools/*.sh
            # The sandbox has no /usr/bin/env, and tests/run.sh invokes the
            # audit as an executable -- so the `#!/usr/bin/env bash` shebang is
            # resolved by the kernel and fails. Rewrite to store paths here
            # rather than changing the scripts, which must stay portable
            # outside Nix.
            patchShebangs repo/tests repo/tools
            cd repo
            bash tests/run.sh
            mkdir -p "$out"
            echo "unit fixtures passed" > "$out"/result
          '';
        };

        # §9.3: "no network access, ever, at any phase" as a checkable property.
        # --self-test asserts the audit PASSES the clean fixture and REJECTS the
        # socket fixture, so the audit is proven to catch what it claims before
        # there is any exsc to point it at.
        audit = pkgs.stdenvNoCC.mkDerivation {
          name = "check-syscall-audit";
          nativeBuildInputs = [
            fasmgPkg pkgs.bash pkgs.python3 pkgs.binutils
            pkgs.coreutils pkgs.gnugrep pkgs.gawk
          ];
          dontUnpack = true;
          buildCommand = ''
            set -e
            mkdir -p repo/vendor
            cp -r --no-preserve=mode -- ${./tests} repo/tests
            cp -r --no-preserve=mode -- ${./tools} repo/tools
            cp -r --no-preserve=mode -- ${./compiler} repo/compiler
            cp -r --no-preserve=mode -- ${./vendor/fasmg-x86} repo/vendor/fasmg-x86
            chmod +x repo/tools/*.sh
            patchShebangs repo/tools   # no /usr/bin/env in the sandbox
            cd repo
            bash tools/syscall-audit.sh --self-test

            # Audit the REAL binary once there is one -- not just the two
            # fixtures. This is where §9.3's "no network access, ever, at any
            # phase" stops being a property of a test and becomes a property
            # of the shipped artifact. `make audit` already switches this way
            # (Makefile:61-66); without this, `nix flake check` would keep
            # self-testing while make audited exsc, and CI would be weaker
            # than the developer's own command.
            #
            # Rendered as a shell flag rather than Nix string concatenation:
            # compilerExists reads false for an untracked file (see the note
            # at compilerSrcPath), so this silently skips until exsc.asm is
            # BOTH present and `git add`-ed. That is the same trap that once
            # let checks.test run 4 of 19 fixtures and still print PASS.
            EXSC_PRESENT=${if compilerExists then "1" else "0"}
            if [ "$EXSC_PRESENT" = 1 ]; then
              echo "auditing the real exsc, not the fixtures"
              # INCLUDE is required: fasmg resolves `include 'format/...'`
              # via it, and unlike tests/run.sh (which exports a default)
              # this invocation is direct. Absolute, because Makefile:14-19
              # records that a relative INCLUDE breaks the moment anything
              # builds from a different cwd.
              export INCLUDE="$PWD/vendor/fasmg-x86"
              "${fasmgPkg}/bin/fasmg" compiler/x86_64/exsc.asm exsc.bin
              bash tools/syscall-audit.sh exsc.bin
            else
              echo "no tracked compiler/x86_64/exsc.asm -- fixtures only"
            fi

            mkdir -p "$out"
            echo "syscall audit self-test passed" > "$out"/result
          '';
        };
      };

      # Re-vendoring helper: refetches the upstream fasmg.l8vn.zip archive,
      # extracts examples/x86/include/**, and diffs it against
      # vendor/fasmg-x86. Needs the network -- that is exactly why this is an
      # `app` (human-invoked, `nix run`) and not a `check` (sandboxed, no
      # network, part of CI). Never overwrites vendor/fasmg-x86 without an
      # explicit --write, and even then never touches PROVENANCE.md -- its
      # hashes and provenance narrative are updated by hand, on purpose (see
      # PROVENANCE.md's own "Re-vendoring" section).
      apps.${system}.vendor-fasmg-x86 =
        let
          script = pkgs.writeShellApplication {
            name = "vendor-fasmg-x86";
            runtimeInputs = [ pkgs.curl pkgs.unzip pkgs.diffutils pkgs.coreutils pkgs.findutils pkgs.git ];
            text = ''
              export LC_ALL=C
              URL="https://flatassembler.net/fasmg.l8vn.zip"
              # PROVENANCE.md's own recorded figure was measured (see flake.nix)
              # to depend on the sorting shell's locale; this script always
              # hashes under LC_ALL=C, so compares against the LC_ALL=C figure,
              # not the one printed in PROVENANCE.md.
              EXPECTED_DIGEST_C_LOCALE="${vendorIntegrityExpectedDigest}"
              PROVENANCE_RECORDED_DIGEST="${vendorIntegrityProvenanceDigest}"
              WRITE=0

              for a in "$@"; do
                case "$a" in
                  --write)
                    WRITE=1
                    ;;
                  -h|--help)
                    echo "usage: vendor-fasmg-x86 [--write]"
                    echo ""
                    echo "Refetches $URL, extracts examples/x86/include/**, and diffs it"
                    echo "against vendor/fasmg-x86. Reports differences; does not write unless"
                    echo "--write is given. Never touches PROVENANCE.md."
                    exit 0
                    ;;
                  *)
                    echo "unknown argument: $a (use --write or --help)" >&2
                    exit 2
                    ;;
                esac
              done

              REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
              VENDOR_DIR="$REPO_ROOT/vendor/fasmg-x86"

              TMP="$(mktemp -d)"
              trap 'rm -rf "$TMP"' EXIT

              echo "fetching $URL ..." >&2
              curl -fsSL "$URL" -o "$TMP/fasmg.zip"
              archive_sha="$(sha256sum "$TMP/fasmg.zip" | cut -d' ' -f1)"
              echo "fetched archive sha256: $archive_sha" >&2

              mkdir -p "$TMP/extracted"
              unzip -q "$TMP/fasmg.zip" -d "$TMP/extracted"

              src_include="$TMP/extracted/examples/x86/include"
              if [ ! -d "$src_include" ]; then
                echo "error: examples/x86/include/ not found in the fetched archive." >&2
                echo "Upstream layout has changed; vendor/fasmg-x86/PROVENANCE.md's 'Path taken' needs review." >&2
                exit 1
              fi

              mkdir -p "$TMP/staged/vendor/fasmg-x86"
              cp -r "$src_include"/. "$TMP/staged/vendor/fasmg-x86"/
              if [ -f "$TMP/extracted/license.txt" ]; then
                cp "$TMP/extracted/license.txt" "$TMP/staged/vendor/fasmg-x86/LICENSE.txt"
              fi
              stage="$TMP/staged/vendor/fasmg-x86"

              new_digest="$(cd "$TMP/staged" && find vendor/fasmg-x86 -type f ! -name PROVENANCE.md | sort | xargs sha256sum | sha256sum | cut -d' ' -f1)"

              echo "" >&2
              echo "== diff: freshly fetched upstream vs $VENDOR_DIR (PROVENANCE.md excluded) ==" >&2
              if diff -rq --exclude=PROVENANCE.md "$VENDOR_DIR" "$stage" 1>&2; then
                echo "no differences." >&2
              fi
              echo "" >&2
              echo "fetched-tree digest (PROVENANCE.md's recipe, LC_ALL=C): $new_digest" >&2
              echo "vendored-tree digest this flake expects (LC_ALL=C):     $EXPECTED_DIGEST_C_LOCALE" >&2
              echo "vendored-tree digest as recorded in PROVENANCE.md:      $PROVENANCE_RECORDED_DIGEST" >&2
              echo "(the last two rows differ because PROVENANCE.md's recipe is locale-" >&2
              echo "sensitive -- see flake.nix; not a sign anything here is broken)" >&2

              if [ "$WRITE" -eq 0 ]; then
                echo "" >&2
                echo "dry run -- not writing. Re-run with --write to overwrite vendor/fasmg-x86" >&2
                echo "(PROVENANCE.md is never touched by this script; update its hashes and" >&2
                echo "provenance note by hand, per its own 'Re-vendoring' section)." >&2
                exit 0
              fi

              echo "" >&2
              echo "--write given: overwriting $VENDOR_DIR (except PROVENANCE.md) ..." >&2
              find "$VENDOR_DIR" -mindepth 1 -maxdepth 1 ! -name PROVENANCE.md -exec rm -rf {} +
              cp -r "$stage"/. "$VENDOR_DIR"/
              echo "done. Update vendor/fasmg-x86/PROVENANCE.md's hashes and provenance note by hand." >&2
            '';
          };
        in
        {
          type = "app";
          program = "${script}/bin/vendor-fasmg-x86";
          meta = {
            description = "Refetch upstream fasmg.l8vn.zip and diff it against vendor/fasmg-x86 (network; --write to overwrite)";
          };
        };
    };
}
