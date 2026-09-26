#!/usr/bin/env bash
# tests/run.sh -- minimal, real test harness.
#
# Five phases:
#   1. run_unit_tests: discovers tests/unit/*.asm, assembles each with
#      fasmg, and checks the expectations declared in its `; TEST:`
#      directive comment (run=yes|no, expect-exit=<N>, audit=pass|fail|skip,
#      stdin=<repo-relative PATH>).
#      This is the only phase that can do anything today -- there is no
#      compiler's own modules and the toolchain; exsc exists and the
#      driver_* fixtures drive it.
#   2. run_conformance_tests: tests/conformance/ (spec §14, 24 entries),
#      driven by build/exsc; entries the built stages can decide are
#      status=run, the rest DEFERRED and never counted as passing. This
#      comment once said "a deliberate no-op until exsc.asm exists"; it
#      was kept as a separate phase precisely so wiring it up
#      later does not require touching run_unit_tests.
#   3. run_ir_tests: tests/ir/*.ir, SSA IR text fed through tests/ir/
#      emit_ir.asm (parse -> VERIFY -> bfa_emit_program), the fasmg program
#      it prints assembled, RUN, and its exit status / abort / stdout
#      checked, then audited with --potestates Mundus,ambitus.
#   4. run_program_tests: tests/programs/<name>/, Exsecutor sources compiled
#      by exsc, assembled, run and audited the same way.
#   5. run_differential_tests: ADR 0012's differential test -- the SAME
#      corpora compiled through the C backend (`--emitte c`), built by gcc
#      and clang at -O0 and -O2 under UBSan against tests/c/exsrt_shim.c,
#      and required to agree with the reference on stdout bytes, exit status
#      and trap-or-not with the abort kind. Two loops and two banners, one
#      phase: tests/ir/ then tests/programs/.
#   6. run_cross_tests: the same programs, opted in with `cross=yes`,
#      cross-built for big-endian mips64 and run under qemu (ADR 0015).
#   7. run_device_tests: the same programs, opted in with `device=amdgcn`,
#      built as ONE translation unit with tests/c/exsrt_shim_amdgpu.c for
#      every AMD GPU present and dispatched by tools/amd-dispatch/ -- only
#      when this script is invoked with `--device=amdgcn`, because a GPU is
#      hardware, not a flake input, and `nix flake check` must stay
#      hermetic. With the flag, a missing runtime or agent is a FAILURE.
#   Phases 3 to 7 are the only places in this script that execute code a
#   compiler EMITTED; everything before them compares text or diagnostics.
#
# See tests/README.md for the directive format and how to add a fixture.
#
# Exit status: 0 if every check in every phase passed, 1 otherwise.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FASMG="${FASMG:-fasmg}"

# The one argument this script takes. Everything else about a run is a fact
# of the tree (directive keys, floors) or of the environment the flake
# builds; this is the exception because the device phase needs hardware.
DEVICE=""
for arg in "$@"; do
  case "$arg" in
    --device=amdgcn) DEVICE="amdgcn" ;;
    --device=*) echo "tests/run.sh: --device takes exactly 'amdgcn', got '${arg#--device=}'" >&2; exit 2 ;;
    *) echo "usage: tests/run.sh [--device=amdgcn]" >&2; exit 2 ;;
  esac
done
export INCLUDE="${INCLUDE:-$REPO_ROOT/vendor/fasmg-x86}"
AUDIT="$REPO_ROOT/tools/syscall-audit.sh"

# Minimum number of tests/unit/*.asm fixtures that MUST be discovered. Fewer
# than this is a FAILURE, not a pass.
#
# This exists because a check that cannot see what it is checking reports
# green, and this project has produced that outcome four times: a smoke
# fixture asserted against the wrong syscall, a determinism diff between two
# empty directories, a vacuous registry check, and -- the one that motivated
# this line -- `nix flake check` running 4 of 25 fixtures because the rest
# were untracked and flakes filter source to the git-tracked tree. Every one
# of those printed PASS.
#
# Raise it deliberately when fixtures are added. Adding fixtures never TRIPS
# this -- the test is `found < floor` -- so a floor that drifts below the real
# count still catches the failure mode that matters: a discovery mechanism
# silently finding nothing. Drift costs precision, not the guarantee.
# 171 -> 172 was the dec754 wave's dec754_golden.asm; 172 -> 176 is the float
# wave's four: lex_float_literal.asm, lwr_float.asm, chk_ty_floatlit.asm,
# chk_ty_floatops.asm. 176 -> 178 is Stage 5.2's pair: chk_ty_aciesops.asm
# (the whole-acy admission gate) and lwr_aciesops.asm (the lowering it admits).
UNIT_FIXTURE_FLOOR="${UNIT_FIXTURE_FLOOR:-178}"

# The same guarantee for the two run phases below: tests/ir/*.ir fixtures,
# and tests/programs/*/ directories. Same rule -- `found < floor` fails --
# and the same reason. Raise each in the commit that adds a fixture.
#
# PROGRAM_FIXTURE_FLOOR COUNTS DIRECTORIES THAT RUN, not directories that
# exist: a `status=deferred` program (run_program_tests' header) is counted
# separately and never toward this floor, for the reason the conformance
# suite never counts a deferred entry as passing. It went from 6 to 5 when
# ordo_maior_custodia/ was retired -- that directory expected exsc to REFUSE
# a `u64:maior` field read, milestone M6 made the read compile, and the
# positive test of what it compiles to is tests/unit/lwr_transitus.asm
# (`load u64 %0 0 maior`), because the emitter cannot run a `maior` load yet.
# It went from 22 to 92 when the receiver's R3 milestone added one directory
# per vendored impaired vector (receptio_vec_*, seventy of them): the
# certificate of ADR 0014 decision 1 is a per-file verdict, and seventy
# directories is what "per file" means here. 98 -> 100 is the float front
# end's two programs: float_constants/ and float_division/, the first .exsc
# sources whose float literals and `/` reach the float IR opcodes both
# backends lower (check 100 from both, differential phase below).
# 100 -> 101 is pictura_triangulum/, the RGB triangle: floats rendering an
# image, held to the Python oracle's bytes. 102 -> 103 is Stage 5.2's
# acies_float8/: whole-acy `+ - * /` on acies<f32,8>/acies<f64,8>, held to
# prototypes/acies_float8_oracle.py's numpy bytes, cross=yes.
# 103 -> 104 is Stage 5.3's pictura_octonaria/: the lane-parallel RGB
# triangle, 960x540 with 2x2 SSAA, eight f32 lanes -- the first program
# whose picture comes from whole-acy arithmetic, held to
# prototypes/pictura_octonaria_oracle.py's numpy f32 bytes, cross=yes.
# IR fixtures: 53 -> 64 is Stage 5.1's vector float group (sse-ir.md 2.2):
# vec_arith and vec_mem positive, four rejections (parse lanes, verifier
# x2 maior/minor, emitter vadd-on-scalar), all with C-backend parity.
# Programs 104 -> 125: the musical HydraModem examples (transmitter melos/
# bassus/bicinium, receiver auditus, streaming receiver auditus_fluxus), set to
# the count measured with them in.
IR_FIXTURE_FLOOR="${IR_FIXTURE_FLOOR:-64}"
PROGRAM_FIXTURE_FLOOR="${PROGRAM_FIXTURE_FLOOR:-125}"

# The differential phase (run_differential_tests, below), which compiles the
# C backend's emitted units and runs them against the same expectations the
# reference backend is held to. Its floor counts BUILDS THAT RAN AND WERE
# CHECKED, not fixtures: the phase's whole claim is "four toolchains agreed
# with the reference on every fixture C1 lowers", and a floor on fixtures
# would still read green if three of the four builds silently stopped
# happening. 39 of the 49 IR fixtures are lowerable (the other 10 are
# rejections, checked separately and by exit status), times gcc and clang
# times -O0 and -O2 = 156. As of Stage 5.1 (2026-09-14) the phase measures
# 46 lowerable x 4 builds = 184 (the vec_arith/vec_mem pair plus float-wave
# fixtures the floor had not been re-bumped for); the floor is the measured
# count now, not the arithmetic above, which stays as the arithmetic.
DIFFERENTIAL_BUILD_FLOOR="${DIFFERENTIAL_BUILD_FLOOR:-184}"

# The same phase over tests/programs/. Two floors, because the claim has two
# halves and a floor on either alone reads green while the other collapses:
#
#   DIFFERENTIAL_PROGRAM_FLOOR        program DIRECTORIES that were eligible
#     (no c-differentia= in their TEST directive) and whose FOUR builds ALL
#     agreed with the reference. 26 of the 96 directories are eligible; the
#     other 70 are the receptio_vec_* sweep, each declaring
#     c-differentia=nightly-sweep by name. A directory that stopped agreeing
#     drops this count, and a directory that stopped being COMPILED does too
#     -- which a floor on builds alone would not catch, since a directory
#     silently marked ineligible removes four builds and four agreements
#     together.
#   DIFFERENTIAL_PROGRAM_BUILD_FLOOR  builds RUN AND CHECKED: 26 x 4 = 104.
#     Not 26 x 4 compiles -- sixteen of those directories share a unit with
#     another (the four streamdb_*, the four receptio_*, three hydramodem_*,
#     each one unit over several inputs), so the phase compiles 18 distinct
#     units and RUNS 104 binaries. The count that matters is the runs: each
#     is one comparison against the reference over a different input.
#     28 -> 30 and 112 -> 120 are the float front end's two programs
#     (float_constants/, float_division/): each eligible, each its own unit,
#     four builds apiece -- the differential phase's first float-source
#     byte-identity claim. 30 -> 31 and 120 -> 124: pictura_triangulum/,
#     the RGB triangle, the same bargain over a whole image.
#     32 -> 33 and 128 -> 132: acies_float8/ (Stage 5.2), its own unit --
#     packed whole-acy arithmetic byte-identical across the four toolchains.
#     33 -> 34 and 132 -> 136: pictura_octonaria/ (Stage 5.3), its own unit --
#     the lane rasterizer's 1.5 MB image byte-identical across the four
#     toolchains.
#     34 -> 55 and 136 -> 220: the musical HydraModem examples (melos_*,
#     bassus_*, bicinium_*, auditus_*, auditus_fluxus_*), eligible and
#     agreeing under all four builds; the floors are set to the counts
#     measured with them in (55 directories, 220 runs).
DIFFERENTIAL_PROGRAM_FLOOR="${DIFFERENTIAL_PROGRAM_FLOOR:-55}"
DIFFERENTIAL_PROGRAM_BUILD_FLOOR="${DIFFERENTIAL_PROGRAM_BUILD_FLOOR:-220}"

# The cross phase's own floor, deliberately NOT folded into the differential
# numbers above: a cross-compiled, emulated run of a 32-bit-`mensura` unit is
# a different claim from a host build of a 64-bit one, and one number
# reporting both would name neither. §14 entry 25, ADR 0015.
# 8 -> 9 is acies_float8/ (cross=yes, Stage 5.2): gcc's/LLVM's soft lowering
# of `vector_size` arithmetic reproduces every lane bit-identically on the
# big-endian mips64 qemu run -- measured, not assumed (risk register 1).
# 9 -> 10 is pictura_octonaria/ (cross=yes, Stage 5.3): the same soft
# lowering reproduces a whole 960x540 SSAA image on the big-endian run,
# ~1.9 s measured against the 120 s budget.
CROSS_PROGRAM_FLOOR="${CROSS_PROGRAM_FLOOR:-10}"

# The device phase's floor (Stage 6 G2): programs carrying `device=amdgcn`,
# each built as one translation unit with tests/c/exsrt_shim_amdgpu.c and
# dispatched on every AMD GPU the machine has, byte-diffed against the
# program's own golden. 6 at the start: saluta, acies_float8,
# pictura_triangulum, pictura_octonaria (byte-identical on gfx1102 and
# gfx1103, 2026-09-21), float_constants and float_division (exit 100).
# signaculum is NOT here: its 2.9 MB of caller-owned buffers exceed the
# 256 KiB per-workitem private segment (docs/design/amdgpu-backend.md 10).
# Counted only when --device=amdgcn is given; otherwise the phase reports
# itself not requested and no floor is applied.
DEVICE_PROGRAM_FLOOR="${DEVICE_PROGRAM_FLOOR:-6}"

PASS=0
FAIL=0

ok()  { PASS=$((PASS + 1)); echo "  [ok]   $*"; }
bad() { FAIL=$((FAIL + 1)); echo "  [FAIL] $*"; }
note() { echo "  -      $*"; }

# Prints the space-joined key=value tokens from a fixture's `; TEST:` line,
# or nothing if it has none (defaults apply -- see below).
directive_of() {
  local file="$1" line
  line="$(grep -m1 '^; TEST:' "$file" || true)"
  echo "${line#; TEST:}"
}

run_unit_tests() {
  echo "== unit fixtures (tests/unit/) =="
  if ! command -v "$FASMG" >/dev/null 2>&1; then
    bad "fasmg not found on PATH (set FASMG=/path/to/fasmg) -- cannot run unit fixtures"
    return
  fi

  local workdir found=0
  workdir="$(mktemp -d)"

  shopt -s nullglob
  local src
  for src in "$REPO_ROOT"/tests/unit/*.asm; do
    found=$((found + 1))
    local name run_flag expect_exit audit_flag stdin_ref directive kv
    name="$(basename "$src")"
    echo "-- $name"

    run_flag="yes"; expect_exit="0"; audit_flag="skip"; stdin_ref=""
    directive="$(directive_of "$src")"
    if [[ -n "$directive" ]]; then
      for kv in $directive; do
        case "$kv" in
          run=*) run_flag="${kv#run=}" ;;
          expect-exit=*) expect_exit="${kv#expect-exit=}" ;;
          audit=*) audit_flag="${kv#audit=}" ;;
          stdin=*) stdin_ref="${kv#stdin=}" ;;
        esac
      done
    fi
    # stdin=PATH -- repo-root-relative, the same spelling stdout= has in the
    # two run phases below. Absent: </dev/null, as every fixture had before
    # a reader existed to notice the difference.
    local stdin_path="/dev/null"
    if [[ -n "$stdin_ref" ]]; then
      if [[ ! -f "$REPO_ROOT/$stdin_ref" ]]; then
        bad "$name: stdin=$stdin_ref does not exist (paths are repo-root-relative)"
        continue
      fi
      stdin_path="$REPO_ROOT/$stdin_ref"
    fi

    local out="$workdir/$name.out"
    if "$FASMG" "$src" "$out" >"$workdir/$name.asmlog" 2>&1; then
      chmod +x "$out"
      ok "$name: assembles"
    else
      bad "$name: assemble failed"
      sed 's/^/         /' "$workdir/$name.asmlog"
      continue
    fi

    if [[ "$run_flag" == "yes" ]]; then
      local rc=0
      # A 20-SECOND LIMIT, the same one the two run phases give an emitted
      # program: a fixture that hangs -- a read loop whose end-of-input
      # sentinel stopped arriving is the case that put this here -- must be a
      # FAILURE, not a stuck suite. `timeout` exits 124 (or 128+SIGKILL after
      # -k), which matches no fixture's expect-exit=.
      timeout -k 5 20 "$out" <"$stdin_path" >"$workdir/$name.runlog" 2>&1 || rc=$?
      if [[ "$rc" == "$expect_exit" ]]; then
        ok "$name: runs, exit=$rc (expected $expect_exit)"
      else
        bad "$name: exit=$rc, expected $expect_exit"
        sed 's/^/         /' "$workdir/$name.runlog"
      fi
    else
      note "$name: run=no (deliberately not executed -- see fixture header)"
    fi

    if [[ "$audit_flag" != "skip" ]]; then
      local arc=0 verdict
      "$AUDIT" "$out" >"$workdir/$name.auditlog" 2>&1 || arc=$?
      verdict="fail"; [[ "$arc" == "0" ]] && verdict="pass"
      if [[ "$verdict" == "$audit_flag" ]]; then
        ok "$name: audit verdict=$verdict (expected $audit_flag)"
      else
        bad "$name: audit verdict=$verdict, expected $audit_flag"
        sed 's/^/         /' "$workdir/$name.auditlog"
      fi
    fi
  done
  shopt -u nullglob

  rm -rf "$workdir"
  if [[ "$found" -lt "$UNIT_FIXTURE_FLOOR" ]]; then
    bad "discovered $found fixtures in tests/unit/, floor is $UNIT_FIXTURE_FLOOR"
    note "a harness that finds nothing must not report success -- see"
    note "UNIT_FIXTURE_FLOOR at the top of this file for why this check exists"
    if [[ "$found" -gt 0 ]]; then
      note "under Nix, check that the fixtures are git-tracked: flakes filter"
      note "source to the tracked tree, so an untracked fixture is invisible"
    fi
  else
    note "discovered $found fixtures (floor $UNIT_FIXTURE_FLOOR)"
  fi
}

run_conformance_tests() {
  # ---------------------------------------------------------------------
  # spec §14, 26 entries (was 24 when this header was written; 25 appended
  # with the mips64 cross row, 26 with the float wave's RGB triangle), FIVE
  # rule shapes (tests/README.md, "entries, five rule shapes" -- a runner
  # that assumes one shape quietly mishandles four):
  #
  #   code   -- "exsc rejects this source with exactly code EXS-Exxxx"
  #             (entries 2-14, 18-22)
  #   bytes  -- byte-identical output across conditions/hosts (16, 17) --
  #             tools/reproduce.sh already knows how to check this; called
  #             from below, never reimplemented
  #   cert   -- an EXTERNAL certificate, vendor/hydramesh-wire's 246 golden
  #             vectors, not a case this project wrote for itself (23 only)
  #   abort  -- a RUNTIME abort, not a compile failure -- needs a built and
  #             EXECUTED binary (15 only)
  #   nocap  -- a capability-absence violation with NO code assigned in
  #             §13 at all (1 only) -- confirmed directly against §2.4's
  #             unrepresentability table, whose "Locale case fold in
  #             program logic" row has an em dash in the Error column,
  #             same as two other rows and unlike every code-bearing row
  #             around it. Never invent one (CLAUDE.md).
  #
  # PER-FIXTURE EXPECTATION DIRECTIVE. run_unit_tests' `; TEST:` (above)
  # cannot be reused as-is: `.exsc` source has no `;` comment syntax to
  # begin with (spec §8.4: "Comments are `//` to end of line" -- full
  # stop, no semicolon form exists), and a `.exsc` fixture for entries 18
  # or 20 needs to stay parseable Exsecutor-shaped source around the one
  # violation under test, not accidentally read as a second one. One line,
  # anywhere in the fixture, `//`-commented, space-separated key=value
  # tokens, mirroring directive_of's own format one section up:
  #
  #   // TEST: entry=<1-25> shape=<code|bytes|cert|abort|nocap>
  #            [expect-code=EXS-E0XXX] status=<run|deferred> [needs=<token>]
  #            [sources=A,B]
  #
  #   entry=N       the §14 entry number this fixture exercises.
  #   shape=        one of the five above.
  #   expect-code=  the exact EXS-Exxxx exsc must reject with. Required
  #                 when shape=code; absent for the other four shapes
  #                 (bytes/cert/abort have no code at all; nocap has none
  #                 BY DEFINITION -- see above).
  #   status=       run      -- this function actually exercises the
  #                             fixture and the result counts toward
  #                             PASS/FAIL below.
  #                 deferred -- the machinery this entry needs does not
  #                             exist yet. Reported as deferred and NEVER
  #                             counted as passing (CLAUDE.md: "Never
  #                             report a test you did not see pass").
  #   needs=        required when status=deferred (comma-separated if more
  #                 than one): what it is waiting on -- parser,
  #                 type_checker, capability_checker, lexicon_checker,
  #                 wire_layout_checker, ffi_checker, import_closure,
  #                 backend, runtime, cross_compile, wire_codec.
  #   sources=      shape=cert only: the files compiled AFTER the fixture,
  #                 in this order, as one unit (spec §12), relative to
  #                 tests/conformance/. They live in a subdirectory
  #                 (entry23/) so the `*.exsc` glob below does not take
  #                 them for fixtures of their own.
  #
  # WHY A DIRECTIVE PER FIXTURE RATHER THAN A TABLE HERE: codes.inc vs §13
  # is this project's own argument against a second, hand-maintained copy
  # of the same facts (CLAUDE.md, "§13's registry is the only source") --
  # applied to this suite, the fixture is the one place that should say
  # what it expects, so adding an entry never means updating two files in
  # sync by hand.
  #
  # HOW EACH SHAPE IS ACTUALLY CHECKED, this wave:
  #   code, status=run   -- build compiler/x86_64/exsc.asm (once, reused
  #                         for every such fixture) and run
  #                         `exsc aedifica --hospes x86_64-linux
  #                         --diagnostica json FIXTURE`; §8.3's own
  #                         machine-readable mode is JSON Lines (driver/
  #                         run.inc: one object per diagnostic, one per
  #                         line, no wrapping array), so every line's
  #                         "code" member is pulled out and the fixture
  #                         passes iff that SET, DEDUPED, is exactly
  #                         {expect-code} -- never a substring check on one
  #                         line, and never on English text. A repeated
  #                         occurrence of expect-code (the same violation
  #                         named once per occurrence in source -- entry
  #                         19's identifier at its declaration and its use
  #                         -- collapses to one element of the set and is
  #                         not a second code); ANY other code, even
  #                         alongside a correct one, fails the fixture --
  #                         that is exactly the gap a plain `grep -qF` left
  #                         open (a spec-guardian review caught it: entries
  #                         19 and 22 were passing on a match against
  #                         expect-code while ALSO emitting a second,
  #                         different code the grep never looked for). The
  #                         exit status is also checked, separately, against
  #                         exsc.asm's own table (comment above `start:`,
  #                         "Exit: 0 clean, 1 diagnostics, ...") -- 1, the
  #                         diagnostics exit, not 132 (an `rassert` trap) or
  #                         4 (unimplemented). Front-end diagnostics have no
  #                         target concept (§8.1-§8.4 predate targets
  #                         entirely), so which --hospes triple is used
  #                         here is not load-bearing.
  #   bytes, status=run   -- tools/reproduce.sh, reused verbatim per this
  #                         agent's brief ("already knows how to do that
  #                         -- reuse it, do not reimplement"); guarded
  #                         against counting its own honest fallback (it
  #                         prints a banner naming the fixture it actually
  #                         built when exsc can't be) as a pass for this
  #                         suite.
  #   cert, status=run    -- entry 23 only: cert_entry23, below. The fixture
  #                         plus its `sources=` (paths relative to
  #                         tests/conformance/) are compiled as one unit,
  #                         assembled, RUN, audited, and the stream it writes
  #                         compared section by section with what
  #                         entry23/expecta.py builds from the vendored
  #                         certificate; then three mechanical mutants must
  #                         each fail at the vector the design names.
  #   abort / nocap       -- no fixture claims status=run for these this
  #                         wave (no executed-binary abort check, no
  #                         capability checker). Dispatched defensively
  #                         below so a future fixture that DID claim
  #                         status=run without real support fails loudly
  #                         instead of being silently treated as a pass.
  #
  # FLOORS, mirroring UNIT_FIXTURE_FLOOR above but local to this function
  # (this project has produced four green checks that saw nothing; a
  # conformance suite silently running zero entries -- or silently losing
  # fixtures -- would be the fifth):
  #   fixture_floor -- §14 has exactly 24 entries; fewer *.exsc files than
  #                    that means fixtures went missing, not that §14 shrank.
  #   run_floor     -- entries 3, 5, 18, 20 are lexically checkable, entries
  #                    6, 7, 9, 19, 21 are checkable by the wire-codec
  #                    branch's @transitus layout checker, type checker and
  #                    lexer identifier classification, and entry 23 runs
  #                    its certificate -- all ten verified passing under the
  #                    exact-code-set check below (see this suite's own
  #                    report). Entry 22 (`u4:maior`) is DEFERRED, not run:
  #                    tightening the check from a substring match to an
  #                    exact set found it was never really passing --
  #                    exsc emits EXS-E0201 (the entry's own expectation)
  #                    AND EXS-E0322 (implicit padding), the second raised
  #                    by the wire-layout checker over whatever partial
  #                    parse the E0201 recovery leaves behind -- a real
  #                    second diagnostic, not a fixture bug (§5.2 rule 3:
  #                    "`u4:maior` does not parse, so it is EXS-E0201.
  #                    Neither needs a new code" -- one code, not two). If
  #                    the number that actually RUN ever drops below the
  #                    floor, something silently stopped working.
  echo "== conformance suite (tests/conformance/, spec §14) =="
  local dir="$REPO_ROOT/tests/conformance"
  local fixture_floor=26
  local run_floor=12

  if [[ ! -d "$dir" ]]; then
    bad "tests/conformance/ does not exist"
    return
  fi

  shopt -s nullglob
  local fixtures=("$dir"/*.exsc)
  shopt -u nullglob
  local nfix=${#fixtures[@]}

  if [[ "$nfix" -lt "$fixture_floor" ]]; then
    bad "discovered $nfix *.exsc fixtures in tests/conformance/, floor is $fixture_floor"
    note "a suite that finds fewer fixtures than §14 has entries must not"
    note "report success -- see UNIT_FIXTURE_FLOOR's header, above, for the"
    note "class of bug this floor (and that one) exists to catch"
  else
    note "discovered $nfix *.exsc fixtures (floor $fixture_floor)"
  fi

  # ---- build exsc once, reused for every shape=code status=run fixture ----
  # Degrades exactly like tools/reproduce.sh and the Makefile's EXSC_DEP:
  # compiler/x86_64/exsc.asm not existing is expected and not a failure of
  # THIS suite (it is the driver agent's tree, built concurrently -- see
  # CLAUDE.md's Scope: "Do not edit another agent's tree"); fasmg missing,
  # or exsc.asm existing but failing to assemble, are real failures because
  # they are regressions in something this suite depends on, not an
  # expected absence.
  local workdir; workdir="$(mktemp -d)"
  local exsc_src="$REPO_ROOT/compiler/x86_64/exsc.asm"
  local exsc_bin="$workdir/exsc"
  local exsc_ok=0
  if [[ ! -f "$exsc_src" ]]; then
    note "compiler/x86_64/exsc.asm is absent from this tree -- every shape=code"
    note "fixture is checked against its fixture's own status= instead;"
    note "none can be status=run without it"
  elif ! command -v "$FASMG" >/dev/null 2>&1; then
    bad "fasmg not found on PATH (set FASMG=/path/to/fasmg) -- cannot build exsc"
  else
    if "$FASMG" "$exsc_src" "$exsc_bin" >"$workdir/exsc.asmlog" 2>&1; then
      chmod +x "$exsc_bin"
      exsc_ok=1
      note "built exsc from compiler/x86_64/exsc.asm for this run"
    else
      bad "compiler/x86_64/exsc.asm failed to assemble -- every shape=code"
      bad "status=run fixture below will fail until this is fixed (report"
      bad "to the driver agent -- CLAUDE.md: do not edit another agent's tree)"
      sed 's/^/         /' "$workdir/exsc.asmlog"
    fi
  fi

  local ran=0 deferred=0
  local -A seen_entries=()

  local f
  for f in "${fixtures[@]}"; do
    local name; name="$(basename "$f")"
    local directive; directive="$(grep -m1 '^// TEST:' "$f" || true)"
    directive="${directive#// TEST:}"
    if [[ -z "$directive" ]]; then
      bad "$name: no '// TEST:' directive -- cannot tell which §14 entry or shape this is"
      continue
    fi

    local entry="" shape="" expect_code="" status="" needs="" sources=""
    local kv
    for kv in $directive; do
      case "$kv" in
        entry=*) entry="${kv#entry=}" ;;
        shape=*) shape="${kv#shape=}" ;;
        expect-code=*) expect_code="${kv#expect-code=}" ;;
        status=*) status="${kv#status=}" ;;
        needs=*) needs="${kv#needs=}" ;;
        sources=*) sources="${kv#sources=}" ;;
      esac
    done

    if [[ -z "$entry" || -z "$shape" || -z "$status" ]]; then
      bad "$name: directive missing entry=/shape=/status= (got: '$directive')"
      continue
    fi
    if [[ ! "$entry" =~ ^[0-9]+$ || "$entry" -lt 1 || "$entry" -gt 26 ]]; then
      bad "$name: entry='$entry' is not a §14 entry number (1-26)"
      continue
    fi
    seen_entries[$entry]=$(( ${seen_entries[$entry]:-0} + 1 ))

    if [[ "$status" == "deferred" ]]; then
      if [[ -z "$needs" ]]; then
        bad "$name: status=deferred but no needs= -- say what it is waiting on"
        continue
      fi
      deferred=$((deferred + 1))
      note "$name: entry $entry DEFERRED (shape=$shape needs=$needs) -- not counted as passing"
      continue
    fi
    if [[ "$status" != "run" ]]; then
      bad "$name: unknown status='$status' (expected run|deferred)"
      continue
    fi

    case "$shape" in
      code)
        if [[ -z "$expect_code" ]]; then
          bad "$name: shape=code but no expect-code="
          continue
        fi
        if [[ "$exsc_ok" -ne 1 ]]; then
          bad "$name: entry $entry claims status=run but exsc is not available this run"
          continue
        fi
        local errlog="$workdir/$name.err"
        local rc=0
        "$exsc_bin" aedifica --hospes x86_64-linux --diagnostica json "$f" \
          >"$workdir/$name.out" 2>"$errlog" || rc=$?
        if [[ "$rc" -ne 1 ]]; then
          bad "$name: entry $entry: exsc exited $rc (expected 1, 'diagnostics were emitted')"
          sed 's/^/         /' "$errlog"
          continue
        fi
        # EXACT match, not "found somewhere": pull the "code" member out of
        # every JSON Lines diagnostic (one object per line -- driver/
        # run.inc), dedupe, and require the resulting SET to be exactly
        # {expect_code}. A repeated occurrence of expect_code collapses to
        # one element and still passes (see the header comment above); any
        # OTHER code -- a second, real diagnostic the old `grep -qF`
        # presence check never looked for -- fails it.
        local codes
        codes="$(grep -oE '"code":"EXS-E[0-9]+"' "$errlog" |
                 sed -E 's/.*"(EXS-E[0-9]+)"$/\1/' | sort -u | tr '\n' ' ')"
        codes="${codes% }"
        if [[ "$codes" == "$expect_code" ]]; then
          ok "$name: entry $entry rejected with exactly {$expect_code}, no other code"
          ran=$((ran + 1))
        else
          bad "$name: entry $entry expected exactly {$expect_code}, exsc's diagnostics carry {${codes:-none}}:"
          sed 's/^/         /' "$errlog"
        fi
        ;;
      bytes)
        # Reused, not reimplemented (this agent's brief). No fixture is
        # status=run for this shape yet (16/17 both need a backend this
        # project does not have) -- this branch exists so the day one is,
        # flipping that fixture's status= is the only change needed.
        local rc=0
        "$REPO_ROOT/tools/reproduce.sh" >"$workdir/$name.out" 2>&1 || rc=$?
        if grep -q "THIS RUN TESTS THE FIXTURE, NOT exsc" "$workdir/$name.out"; then
          bad "$name: entry $entry: tools/reproduce.sh degraded to its own"
          bad "toolchain fallback (no exsc output to diff) -- that proves the"
          bad "harness, not entry $entry; do not mark this fixture status=run"
          bad "until exsc actually emits an object to reproduce"
        elif [[ "$rc" -eq 0 ]]; then
          ok "$name: entry $entry: tools/reproduce.sh reports byte-identical output"
          ran=$((ran + 1))
        else
          bad "$name: entry $entry: tools/reproduce.sh reported non-reproducible output"
          sed 's/^/         /' "$workdir/$name.out"
        fi
        ;;
      cert)
        if [[ "$entry" != "23" || -z "$sources" ]]; then
          bad "$name: shape=cert is §14 entry 23 only, and needs sources= (got entry=$entry sources='$sources')"
          continue
        fi
        if [[ "$exsc_ok" -ne 1 ]]; then
          bad "$name: entry $entry claims status=run but exsc is not available this run"
          continue
        fi
        if cert_entry23 "$f" "$sources" "$exsc_bin" "$workdir"; then
          ran=$((ran + 1))
        fi
        ;;
      abort|nocap)
        bad "$name: entry $entry: shape=$shape has no status=run implementation --"
        bad "no fixture should claim status=run for this shape yet (abort needs"
        bad "an executed binary, nocap needs a capability checker) -- fix the"
        bad "fixture's directive, not this branch"
        ;;
      *)
        bad "$name: unknown shape='$shape'"
        ;;
    esac
  done

  local missing=() dup=()
  local i
  for ((i = 1; i <= 26; i++)); do
    case "${seen_entries[$i]:-0}" in
      0) missing+=("$i") ;;
      1) ;;
      *) dup+=("$i") ;;
    esac
  done
  if [[ ${#missing[@]} -gt 0 ]]; then
    bad "no fixture claims entry=${missing[*]} -- §14 has 26 entries, all must be represented"
  fi
  if [[ ${#dup[@]} -gt 0 ]]; then
    bad "more than one fixture claims entry=${dup[*]} -- each §14 entry should have exactly one"
  fi

  rm -rf "$workdir"

  if [[ "$ran" -lt "$run_floor" ]]; then
    bad "only $ran entries actually ran (checked against a real exsc diagnostic), floor is $run_floor"
    note "a conformance suite that silently ran zero (or too few) entries is"
    note "exactly the fifth false-green this project has already produced"
    note "four times over -- see UNIT_FIXTURE_FLOOR's header, above"
  else
    note "$ran/$nfix entries RAN and passed; $deferred/$nfix entries DEFERRED (not counted as passing)"
  fi
}

# ===========================================================================
# §14 entry 23: the DeModFrame codec, written in Exsecutor, certified.
#
# cert_entry23 FIXTURE SOURCES EXSC WORKDIR -- returns 0 iff every check
# below passed; each check also counts ok/bad itself. docs/design/
# wire-codec.md sections 5-6 are the design, and every number asserted here
# is that document's:
#
#   1. purity, spec §4.1 rule 6: "declared row empty AND no capability
#      parameter". The first source (entry23/codex.exsc) must check with the
#      fixture and nothing else; every function in it is `publica` and none
#      writes `poscit`, so each row is DECLARED EMPTY and it is the checker,
#      not this script, that refuses any draw in a body; and outside
#      comments it names no capability atom (§4.6), no `Scriptor` (the
#      prelude's capability-bearing type), no `sub` and no `initium`, so no
#      parameter can carry authority in. The one other way in, a
#      capability-bearing user struct, is closed by §5.2: the only types it
#      declares or uses are `@transitus`, whose fields are integers.
#   2. the unit -- the fixture, then SOURCES in order -- compiles, assembles
#      and runs: exit 0 (probatio's own count of 2,502 one-byte writes), no
#      stderr.
#   3. the syscall audit with `--potestates Mundus,ambitus` passes, AND the
#      syscall sites it finds are `read`, `write` and `exit_group` and
#      nothing else. `read` is in that set without this program calling it:
#      the prelude gates by ATOM, not by use (runtime.md 2.6), so a binary
#      whose closure holds `ambitus` carries the whole atom -- both writers
#      and `exsrt_lector_lege_octeto` -- and the audit's claim is about the
#      closure, not about reachability. This list read `exit_group write`
#      until the reader landed.
#   4. a second run writes the byte-identical stream (spec §9.3).
#   5. entry23/expecta.py --compara: section by section against the stream
#      built from vendor/hydramesh-wire/golden_vectors.json. Sections 1-2
#      are the 246-vector certificate, 3 the anchors, 4-5 the laws, and each
#      is reported on its own line; a mismatch names the section, the vector
#      and the differing bytes.
#   6. three mutants, applied mechanically to COPIES of the sources in a
#      temp dir (never to a tracked file), must each produce a stream whose
#      FIRST mismatch is the section and vector wire-codec.md section 6.1
#      names -- not merely "differs". A mutant that passes, or fails
#      somewhere else, fails the entry: a harness a mutant survives has
#      proved nothing, and one that fails at the wrong place is not testing
#      what it says.
# ===========================================================================

# cert_stream EXSC PREFIX SRC... -- compile, assemble and run the unit; its
# stdout is PREFIX.stdout, its binary PREFIX.bin. Returns 0 iff it exited 0
# with an empty stderr; otherwise prints what went wrong.
cert_stream() {
  local exsc="$1" p="$2"; shift 2
  if ! "$exsc" aedifica --hospes x86_64-linux "$@" -o "$p.asm" >"$p.exsclog" 2>&1; then
    echo "exsc refused the unit:"; sed 's/^/         /' "$p.exsclog"; return 1
  fi
  if ! "$FASMG" "$p.asm" "$p.bin" >"$p.asmlog" 2>&1; then
    echo "fasmg cannot assemble exsc's output:"; sed 's/^/         /' "$p.asmlog"; return 1
  fi
  chmod +x "$p.bin"
  local st; st="$(run_binary "$p.bin" "$p.stdout" "$p.stderr")"
  if [[ "$st" != "exit 0" || -s "$p.stderr" ]]; then
    echo "the binary gave '$st' (expected 'exit 0' and no stderr):"
    sed 's/^/         /' "$p.stderr"; return 1
  fi
  return 0
}

# cert_mutate KIND FIXTURE CODEX -- applies one mutant to the (copied) files,
# each substitution required to match EXACTLY once, so a source edit that
# moved the text makes the mutant fail loudly rather than silently become
# the unmutated program.
cert_mutate() {
  python3 - "$@" <<'PY'
import re, sys
kind, fixture, codex = sys.argv[1:4]
def sub(path, pat, rep):
    with open(path, encoding='utf-8') as fh:
        s = fh.read()
    t, n = re.subn(pat, rep, s, flags=re.M)
    if n != 1:
        sys.exit('mutant %s: %r matches %d times in %s, expected exactly once'
                 % (kind, pat, n, path))
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write(t)
if kind == 'polynomium':      # the CRC polynomial, 0x1021 -> 0x1020
    sub(codex, r'\(c sursum 1\) aut 0x1021;', '(c sursum 1) aut 0x1020;')
elif kind == 'ordo':          # one field's byte order, maior -> minor
    sub(fixture, r'^    numerus: u16:maior$', '    numerus: u16:minor')
elif kind == 'permutatio':    # the sub-byte packing order: versio, genus swapped
    sub(fixture, r'^    versio:  u4\n    genus:   u4$', '    genus:   u4\n    versio:  u4')
else:
    sys.exit('unknown mutant ' + kind)
PY
}

cert_entry23() {
  local fixture="$1" sources="$2" exsc="$3" work="$4"
  local cdir="$REPO_ROOT/tests/conformance"
  local name; name="$(basename "$fixture")"
  local expecta="$cdir/entry23/expecta.py"
  local rel=() srcs=("$fixture") s rc=0 msg
  IFS=',' read -r -a rel <<<"$sources"
  for s in "${rel[@]}"; do
    if [[ ! -f "$cdir/$s" ]]; then
      bad "$name: entry 23: source $s does not exist (sources= is relative to tests/conformance/)"
      return 1
    fi
    srcs+=("$cdir/$s")
  done
  if [[ ${#srcs[@]} -ne 3 ]]; then
    bad "$name: entry 23: expected sources=CODEX,DRIVER, got '$sources'"
    return 1
  fi
  local codex="${srcs[1]}" cname; cname="$(basename "$codex")"

  # 1. purity
  local code; code="$(sed 's://.*$::' "$codex")"
  local impure='poscit|initium|sub|Scriptor|Mundus|alloc|sermo|horologium|archivum|rete|fortuna|ambitus|Filum|machina|Crudum'
  if grep -qwE "$impure" <<<"$code"; then
    bad "$name: entry 23: $cname is meant to be pure, and names one of $impure:"
    grep -nwE "$impure" <<<"$code" | sed 's/^/         /'; rc=1
  elif grep -E '(^|[^[:alnum:]_])functio[[:space:]]' <<<"$code" |
       grep -vqE '^[[:space:]]*publica[[:space:]]+functio[[:space:]]'; then
    bad "$name: entry 23: every function in $cname must be publica, so its empty row is declared"
    rc=1
  elif ! "$exsc" aedifica --hospes x86_64-linux "$fixture" "$codex" >"$work/e23.purity" 2>&1; then
    bad "$name: entry 23: $cname does not check with the fixture alone:"
    sed 's/^/         /' "$work/e23.purity"; rc=1
  else
    ok "$name: entry 23: $cname is pure (spec §4.1 rule 6) -- every function publica with no poscit, no capability named, and it checks with the fixture alone"
  fi

  # 2. compile, assemble, run
  local p="$work/e23"
  if ! msg="$(cert_stream "$exsc" "$p" "${srcs[@]}")"; then
    bad "$name: entry 23: $msg"
    return 1
  fi
  ok "$name: entry 23: fixture + ${rel[*]} compile, assemble and run: exit 0, $(wc -c <"$p.stdout" | tr -d ' ') bytes on stdout"

  # 3. the syscall surface: within {Mundus, ambitus}, and write + exit_group only
  if "$AUDIT" --potestates Mundus,ambitus "$p.bin" >"$p.audit" 2>&1; then
    local kinds
    kinds="$(grep -E '^0x[0-9a-f]+[[:space:]]+[0-9]+[[:space:]]' "$p.audit" |
             awk '{print $3}' | LC_ALL=C sort -u | tr '\n' ' ')"
    if [[ "$kinds" == "exit_group read write " ]]; then
      ok "$name: entry 23: syscall audit (--potestates Mundus,ambitus) passes; the binary's syscalls are read, write and exit_group, nothing else (the read is the ambitus atom's, carried by the gate rather than called -- see this function's header)"
    else
      bad "$name: entry 23: syscall kinds found are '$kinds', expected exactly exit_group, read and write"
      sed 's/^/         /' "$p.audit"; rc=1
    fi
  else
    bad "$name: entry 23: syscall surface exceeds {Mundus, ambitus}"
    sed 's/^/         /' "$p.audit"; rc=1
  fi

  # 4. determinism: the same binary, run again
  local st2; st2="$(run_binary "$p.bin" "$p.again" "$p.again.err")"
  if [[ "$st2" == "exit 0" ]] && cmp -s "$p.stdout" "$p.again"; then
    ok "$name: entry 23: a second run writes the byte-identical stream"
  else
    bad "$name: entry 23: a second run gave '$st2' and a different stream"; rc=1
  fi

  # 5. the certificate, the anchors, the laws
  local report crc=0 line
  report="$(python3 "$expecta" --compara "$p.stdout" 2>&1)" || crc=$?
  grep -E '^MISMATCH|^section ' <<<"$report" | sed 's/^/         /' || true
  for line in certificate anchors laws; do
    local l; l="$(grep -m1 "^$line: " <<<"$report" || true)"
    if [[ -z "$l" ]]; then
      bad "$name: entry 23: expecta.py printed no '$line:' line (exit $crc):"
      sed 's/^/         /' <<<"$report"; rc=1; continue
    fi
    # every a/b on the line must have a == b
    local pair allok=1
    for pair in $(grep -oE '[0-9]+/[0-9]+' <<<"$l"); do
      [[ "${pair%/*}" == "${pair#*/}" ]] || allok=0
    done
    if [[ "$allok" -eq 1 ]]; then ok "$name: entry 23: $l"; else bad "$name: entry 23: $l"; rc=1; fi
  done
  if [[ "$crc" -ne 0 && "$rc" -eq 0 ]]; then
    bad "$name: entry 23: expecta.py --compara exited $crc:"; sed 's/^/         /' <<<"$report"; rc=1
  fi

  # 6. the three mutants of wire-codec.md section 6.1, each with the first
  # vector it must fail at. Section 1 is the encode basis; index = vector.
  local mspec
  for mspec in "polynomium:1:0:polynomial 0x1021 -> 0x1020 in redundantia" \
               "ordo:1:5:numerus u16:maior -> u16:minor in the declaration" \
               "permutatio:1:0:versio and genus swapped in the declaration"; do
    local kind="${mspec%%:*}" rest="${mspec#*:}"
    local wsec="${rest%%:*}"; rest="${rest#*:}"
    local widx="${rest%%:*}" what="${rest#*:}"
    local m; m="$(mktemp -d "$work/mutant.XXXXXX")"
    local msrcs=() t
    for t in "${srcs[@]}"; do cp -- "$t" "$m/"; msrcs+=("$m/$(basename "$t")"); done
    if ! msg="$(cert_mutate "$kind" "${msrcs[0]}" "${msrcs[1]}" 2>&1)"; then
      bad "$name: entry 23: mutant '$what' could not be applied: $msg"; rc=1; continue
    fi
    if cmp -s "${msrcs[0]}" "${srcs[0]}" && cmp -s "${msrcs[1]}" "${srcs[1]}"; then
      bad "$name: entry 23: mutant '$what' changed nothing"; rc=1; continue
    fi
    if ! msg="$(cert_stream "$exsc" "$m/out" "${msrcs[@]}")"; then
      bad "$name: entry 23: mutant '$what' produced no stream to judge -- it must run and"
      bad "fail at section $wsec vector $widx: $msg"; rc=1; continue
    fi
    local mrep first detail
    mrep="$(python3 "$expecta" --compara "$m/out.stdout" 2>&1 || true)"
    first="$(grep -m1 '^FIRST-FAIL ' <<<"$mrep" || true)"
    detail="$(grep -m1 '^MISMATCH ' <<<"$mrep" || true)"
    if [[ -z "$first" ]]; then
      bad "$name: entry 23: mutant '$what' PASSED the certificate -- the harness proves nothing"
      rc=1
    elif [[ "$first" == "FIRST-FAIL section=$wsec index=$widx" ]]; then
      ok "$name: entry 23: mutant '$what' fails, first at section $wsec vector $widx, as wire-codec.md section 6.1 says"
      note "  ${detail#MISMATCH }"
    else
      bad "$name: entry 23: mutant '$what' fails, but first at ${first#FIRST-FAIL }, not section=$wsec index=$widx"
      note "  ${detail#MISMATCH }"; rc=1
    fi
  done
  return "$rc"
}

# ===========================================================================
# Phases 3 and 4: RUN what a compiler emitted.
#
# Shared by both. A fixture's expectations are key=value tokens (the unit
# directive's style, one section up); these keys mean the same thing in both
# phases:
#
#   expect-exit=N   the program exits normally with status N. Its stderr
#                   must be EMPTY.
#   abort=N         spec 6.6's one abort shape instead: "`abortus N` on
#                   fd 2, then SIGILL". Checked as exactly that -- killed by
#                   signal 4, and a stderr line ending in `abortus N` -- not
#                   as shell status 132, which `redde 132;` also produces
#                   (spec 6.6 chose SIGILL precisely because every u8 is a
#                   legitimate `initium` result). The number is the code;
#                   the English before `abortus` is not promised
#                   (prelude/README.md) and is not matched.
#   stdout=PATH     stdout must be byte-identical to PATH, relative to the
#                   repo root (so a fixture can point at examples/ rather
#                   than copy it). Absent: stdout must be EMPTY.
#   stdin=PATH      the program's stdin is that file, opened read-only,
#                   repo-root-relative exactly as stdout= is. Absent:
#                   /dev/null, which is what every fixture written before a
#                   reader existed assumed. A `cat` program is
#                   `stdin=F stdout=F` and proves identity against the one
#                   file rather than against a copy of it.
#
# Exactly one of expect-exit= / abort= is required for anything that runs.
# An UNKNOWN KEY FAILS THE FIXTURE: the unit directive ignores one, and a
# typo there (`expect_exit=3`) silently falls back to the default and
# passes -- a false green of exactly the kind UNIT_FIXTURE_FLOOR's header
# lists. These two phases do not repeat it.
#
# Every binary runs with stdin </dev/null (or stdin=PATH), an EMPTY
# environment, and a 20-second limit (a hang -- a loop the emitter got wrong -- is a failure,
# not a stuck suite). python3 runs it because bash cannot tell a signal from
# an exit status >= 128; python3 is already required by tools/syscall-audit.sh.
# Every binary is then audited with `--potestates Mundus,ambitus`, the
# publish gate's own invocation for the hello world.
# ===========================================================================

# run_binary BIN OUT ERR [STDIN] -- prints "exit N", "signal N" or "timeout".
# STDIN is a path (absolute, or already resolved by the caller); absent or
# empty means /dev/null.
# run_binary BIN OUT ERR [STDIN] [RUNNER] [TIMEOUT]
# RUNNER, when given, is prepended to the command -- `qemu-mipsn32` for the
# cross phase, which cannot execute a big-endian MIPS binary directly. The
# empty environment is kept: qemu-user needs nothing from it.
run_binary() {
  python3 - "$1" "$2" "$3" "${4:-}" "${5:-}" "${6:-20}" <<'PY'
import subprocess, sys
exe, out, err, inp, runner, tmo = sys.argv[1:7]
cmd = ([runner] if runner else []) + [exe]
with open(out, 'wb') as o, open(err, 'wb') as e:
    i = open(inp, 'rb') if inp else None
    try:
        r = subprocess.run(cmd, stdin=(i or subprocess.DEVNULL), stdout=o,
                           stderr=e, env={}, timeout=float(tmo))
    except subprocess.TimeoutExpired:
        print('timeout')
        sys.exit(0)
    finally:
        if i is not None:
            i.close()
if r.returncode < 0:
    print('signal %d' % -r.returncode)
else:
    print('exit %d' % r.returncode)
PY
}

# check_run LABEL BIN WORK EXPECT_EXIT ABORT STDOUT_REF [STDIN_REF] [AUDIT]
# -- runs BIN, checks the keys above, audits it. EXPECT_EXIT/ABORT/
# STDOUT_REF/STDIN_REF are "" when unset; STDIN_REF is repo-root-relative.
# Returns 0 iff every check passed (each check also counts ok/bad itself).
#
# AUDIT is "audit" (the default, and what every existing caller gets by
# omitting it) or "noaudit". The one caller that passes "noaudit" is
# run_differential_tests: a binary built by gcc or clang from the emitted C
# plus tests/c/exsrt_shim.c is a HOSTED, dynamically linked glibc program,
# and its syscall surface is glibc's rather than the emitted code's. Auditing
# it would not be a weaker check, it would be a check of the wrong thing --
# spec 10.3's audit is a statement about what the REFERENCE backend puts in a
# binary, and the reference's own build is audited one phase earlier. The C
# target's syscall surface becomes checkable when whole-program mode exists
# and the C prelude issues the syscalls itself (c-backend.md D1, [OPEN]).
check_run() {
  local label="$1" bin="$2" work="$3" want_exit="$4" want_abort="$5" ref="$6"
  local sref="${7:-}" doaudit="${8:-audit}" runner="${9:-}" tmo="${10:-20}"
  local out="$work.stdout" err="$work.stderr" status rcode=0
  local inp=""
  [[ -n "$sref" ]] && inp="$REPO_ROOT/$sref"
  status="$(run_binary "$bin" "$out" "$err" "$inp" "$runner" "$tmo")"

  if [[ -n "$want_abort" ]]; then
    if [[ "$status" == "signal 4" ]] &&
       grep -qE "(^|[^[:alnum:]_])abortus $want_abort\$" "$err"; then
      ok "$label: aborts with abortus $want_abort, SIGILL"
    else
      bad "$label: expected abortus $want_abort then SIGILL, got '$status'; stderr:"
      sed 's/^/         /' "$err"; rcode=1
    fi
  else
    if [[ "$status" == "exit $want_exit" ]]; then
      ok "$label: runs, exit=$want_exit"
    else
      bad "$label: got '$status', expected 'exit $want_exit'"
      sed 's/^/         /' "$err"; rcode=1
    fi
    if [[ -s "$err" ]]; then
      bad "$label: wrote to stderr, expected nothing:"
      sed 's/^/         /' "$err"; rcode=1
    fi
  fi

  if [[ -n "$ref" ]]; then
    if cmp -s "$out" "$REPO_ROOT/$ref"; then
      ok "$label: stdout byte-identical to $ref"
    else
      bad "$label: stdout differs from $ref"
      cmp "$out" "$REPO_ROOT/$ref" 2>&1 | sed 's/^/         /' || true
      rcode=1
    fi
  elif [[ -s "$out" ]]; then
    bad "$label: wrote to stdout, expected nothing (no stdout= given)"
    head -c 400 "$out" | sed 's/^/         /'; rcode=1
  fi

  if [[ "$doaudit" == "audit" ]]; then
    if "$AUDIT" --potestates Mundus,ambitus "$bin" >"$work.auditlog" 2>&1; then
      ok "$label: syscall surface within {Mundus, ambitus}"
    else
      bad "$label: syscall surface exceeds {Mundus, ambitus}"
      sed 's/^/         /' "$work.auditlog"; rcode=1
    fi
  fi
  return "$rcode"
}

# parse_run_keys LABEL KV... -- sets k_expect_exit k_abort k_stdout k_emit_exit
# k_exsc_exit k_sources k_status k_needs from the tokens; returns 1 (having
# said why) on an unknown key, a malformed number, or a stdout= that does not
# exist.
parse_run_keys() {
  local label="$1"; shift
  k_expect_exit=""; k_abort=""; k_stdout=""; k_emit_exit="0"; k_exsc_exit="0"
  k_sources=""; k_status="run"; k_needs=""; k_stdin=""
  # Empty, not "0": empty means "no C-specific verdict given, so parity with
  # the reference's own key applies". A default of "0" would silently claim
  # parity with success even where emit-exit= says the reference refuses.
  k_c_emit_exit=""; k_c_exsc_exit=""; k_c_differentia=""; k_cross=""; k_device=""
  local kv
  for kv in "$@"; do
    case "$kv" in
      expect-exit=*) k_expect_exit="${kv#expect-exit=}" ;;
      abort=*)       k_abort="${kv#abort=}" ;;
      stdout=*)      k_stdout="${kv#stdout=}" ;;
      stdin=*)       k_stdin="${kv#stdin=}" ;;
      emit-exit=*)   k_emit_exit="${kv#emit-exit=}" ;;
      exsc-exit=*)   k_exsc_exit="${kv#exsc-exit=}" ;;
      # The C backend's own verdict, for the ONE case where the two backends
      # legitimately differ: `retain`/`release`, which the reference lowers
      # and library mode cannot (no object header exists there). The default
      # is PARITY -- the C emitter must exit exactly as emit-exit= says the
      # reference does -- so reject_emit_straddle.ir needs neither key, and
      # neither does anything else today. The keys exist so the harness can
      # SAY a difference is intended rather than a name being special-cased
      # inside it, and parse_run_keys still fails on an unknown key.
      c-emit-exit=*) k_c_emit_exit="${kv#c-emit-exit=}" ;;
      c-exsc-exit=*) k_c_exsc_exit="${kv#c-exsc-exit=}" ;;
      # INELIGIBILITY FOR THE DIFFERENTIAL PHASE, declared here and nowhere
      # else. A tests/programs/ directory WITHOUT this key is eligible, and
      # its four C builds must agree with the reference; with it, the
      # directory is named and its reason printed, and it is never counted
      # as agreeing. There is no pattern match anywhere in this script that
      # decides this: a name silently absent from a phase is exactly the
      # false green UNIT_FIXTURE_FLOOR's header lists, and "receptio_vec_*
      # is excluded" written as a glob in the harness would be one fact
      # nobody greps for. The value set is CLOSED (below), so a typo is a
      # failed fixture rather than a new, silently accepted reason.
      c-differentia=*) k_c_differentia="${kv#c-differentia=}" ;;
      # OPT-IN TO THE CROSS PHASE, declared the same way and for the same
      # reason as c-differentia= above, but with the polarity reversed: the
      # differential phase is opt-OUT because every host build is cheap,
      # while a qemu run is 10-50x and a directory that takes 7 s natively
      # would take minutes. So the cross phase is opt-IN -- `cross=yes` and
      # nothing else -- and the set of directories that carry it is a fact
      # you can grep for rather than a glob inside the harness.
      cross=*)       k_cross="${kv#cross=}" ;;
      # OPT-IN TO THE DEVICE PHASE, the cross phase's twin: the value set is
      # CLOSED (amdgcn), the directories carrying it are grep-able, and a
      # typo is a failed fixture. See run_device_tests.
      device=*)      k_device="${kv#device=}" ;;
      sources=*)     k_sources="${kv#sources=}" ;;
      status=*)      k_status="${kv#status=}" ;;
      needs=*)       k_needs="${kv#needs=}" ;;
      *) bad "$label: unknown directive key '$kv'"; return 1 ;;
    esac
  done
  if [[ -n "$k_cross" && "$k_cross" != "yes" ]]; then
    bad "$label: cross='$k_cross' -- the only value is 'yes'"; return 1
  fi
  if [[ -n "$k_device" && "$k_device" != "amdgcn" ]]; then
    bad "$label: device='$k_device' -- the only value is 'amdgcn'"; return 1
  fi
  if [[ "$k_status" != "run" && "$k_status" != "deferred" ]]; then
    bad "$label: unknown status='$k_status' (expected run|deferred)"; return 1
  fi
  if [[ "$k_status" == "deferred" && -z "$k_needs" ]]; then
    bad "$label: status=deferred but no needs= -- say what it is waiting on"
    return 1
  fi
  if [[ "$k_status" == "run" && -n "$k_needs" ]]; then
    bad "$label: needs= without status=deferred"; return 1
  fi
  local n
  for n in "$k_expect_exit" "$k_abort" "$k_emit_exit" "$k_exsc_exit" \
           "$k_c_emit_exit" "$k_c_exsc_exit"; do
    if [[ -n "$n" && ! "$n" =~ ^[0-9]+$ ]]; then
      bad "$label: '$n' is not a decimal status"; return 1
    fi
  done
  if [[ -n "$k_stdout" && ! -f "$REPO_ROOT/$k_stdout" ]]; then
    bad "$label: stdout=$k_stdout does not exist (paths are repo-root-relative)"
    return 1
  fi
  if [[ -n "$k_stdin" && ! -f "$REPO_ROOT/$k_stdin" ]]; then
    bad "$label: stdin=$k_stdin does not exist (paths are repo-root-relative)"
    return 1
  fi
  # The closed reason set for c-differentia=. Each value is a REASON A
  # PROGRAM CANNOT BE HELD TO THE DIFFERENTIAL TEST, not a way to quiet one:
  #
  #   nightly-sweep         the receiver's impaired-vector sweep, D6's own
  #                         exclusion ("the 70 receptio_vec_* directories …
  #                         are excluded from the per-commit gate and listed
  #                         as a nightly run"). Every one of the seventy
  #                         compiles the IDENTICAL unit to receptio_exemplum
  #                         -- same sources=, only stdin= differs -- so what
  #                         they would add is 280 more runs of four binaries
  #                         this phase already builds and checks, not one
  #                         more lowering. Measured, 2026-09-12: one such run
  #                         is 56/30/70/26 ms (gcc/clang x -O0/-O2), so the
  #                         sweep is ~13 s of runs, not the minutes D6
  #                         assumed of the reference. The exclusion stands on
  #                         the duplicate unit, not on the clock.
  #   prelude-beyond-shim   the unit imports an exsrt_* routine
  #                         tests/c/exsrt_shim.c does not define (the four
  #                         exsrt_alloc_*, retain/release). It would not
  #                         LINK, and a link failure is the shim's design
  #                         (its header: "a missing routine must be a loud
  #                         failure, not a silently different program").
  #                         Unused today: every eligible unit's imports are
  #                         within the six the shim provides.
  #   emitter-refusal       `exsc --emitte c` refuses the module by name --
  #                         an opcode with no C lowering (c-backend.md D4's
  #                         23 refusals). Unused today.
  #
  # Adding a fourth value is a change here AND a sentence in c-backend.md.
  if [[ -n "$k_c_differentia" ]]; then
    case "$k_c_differentia" in
      nightly-sweep|prelude-beyond-shim|emitter-refusal) ;;
      *) bad "$label: c-differentia='$k_c_differentia' is not one of nightly-sweep|prelude-beyond-shim|emitter-refusal"
         return 1 ;;
    esac
  fi
  return 0
}

# program_sources NAME DIR -- resolves a tests/programs/ directory's §12
# compilation unit into the array `p_srcs`, reading `k_sources` (which
# parse_run_keys has already set). Returns 1, having said why, on an
# ambiguity or a missing file.
#
# SHARED BY TWO PHASES on purpose. run_program_tests compiles the unit with
# the reference backend and run_differential_tests compiles THE SAME UNIT
# with `--emitte c`; if each worked the rule out for itself, the day the two
# disagreed about which files the unit is would be the day the differential
# test compared two different programs and called them equal. One routine,
# one answer.
program_sources() {
  local name="$1" dir="$2" s
  p_srcs=()
  local own; own="$(printf '%s\n' "$dir"/*.exsc | LC_ALL=C sort)"
  if [[ -n "$k_sources" ]]; then
    local rel=()
    IFS=',' read -r -a rel <<<"$k_sources"
    for s in "${rel[@]}"; do p_srcs+=("$REPO_ROOT/$s"); done
    # `sources=` names the unit, so a directory may hold its own source too
    # -- a program that needs a LIBRARY from elsewhere (examples/hydramodem/
    # is one: no `initium`, so it cannot be a unit by itself) and has a main
    # source of its own otherwise has no way to say so. What stays refused
    # is the ambiguity the rule was written for: an own `*.exsc` that
    # `sources=` does not list would be silently ignored, and "which is the
    # unit?" would again have two answers.
    if [[ -n "$own" ]]; then
      local o listed stray=0
      while IFS= read -r o; do
        listed=0
        # an `if`, not `[[ … ]] && listed=1`: under `set -e` a loop whose
        # last command is a failing `&&` list takes the whole script down
        for s in "${p_srcs[@]}"; do
          if [[ "$s" == "$o" ]]; then listed=1; fi
        done
        if [[ "$listed" -eq 0 ]]; then
          bad "$name: $(basename "$o") is in the directory and not in sources= -- which is the unit?"
          stray=1
        fi
      done <<<"$own"
      [[ "$stray" -eq 0 ]] || return 1
    fi
  elif [[ -n "$own" ]]; then
    while IFS= read -r s; do p_srcs+=("$s"); done <<<"$own"
  else
    bad "$name: no *.exsc and no sources="; return 1
  fi
  local missing=0
  for s in "${p_srcs[@]}"; do
    [[ -f "$s" ]] || { bad "$name: source $s does not exist"; missing=1; }
  done
  [[ "$missing" -eq 0 ]] || return 1
  return 0
}

# want_one_outcome LABEL -- exactly one of expect-exit= / abort=.
want_one_outcome() {
  if [[ -n "$k_expect_exit" && -n "$k_abort" ]] ||
     [[ -z "$k_expect_exit" && -z "$k_abort" ]]; then
    bad "$1: needs exactly one of expect-exit= / abort="
    return 1
  fi
  return 0
}

floor_check() {  # WHAT FOUND FLOOR VARNAME
  if [[ "$2" -lt "$3" ]]; then
    bad "discovered $2 $1, floor is $3"
    note "a harness that finds nothing must not report success -- see"
    note "UNIT_FIXTURE_FLOOR at the top of this file; the floor here is $4"
  else
    note "discovered $2 $1 (floor $3)"
  fi
}

run_ir_tests() {
  # -------------------------------------------------------------------------
  # tests/ir/*.ir -- `; TEST:` on any line (parse.inc reads `;` to EOL as a
  # comment, so the directive is part of the IR text itself), keys above
  # plus:
  #   emit-exit=N   emit_ir's own exit status (default 0). Non-zero means
  #                 the fixture is a REJECTION -- 3 parse error, 4 emitter
  #                 refusal, 5 verifier verdict (emit_ir.asm's header has
  #                 the table) -- and
  #                 nothing is assembled or run, so expect-exit=/abort=/
  #                 stdout= are refused alongside it.
  # -------------------------------------------------------------------------
  echo "== IR run tests (tests/ir/) =="
  if ! command -v "$FASMG" >/dev/null 2>&1; then
    bad "fasmg not found on PATH -- cannot run IR tests"
    return
  fi
  local workdir; workdir="$(mktemp -d)"
  local emit="$workdir/emit_ir"
  if "$FASMG" "$REPO_ROOT/tests/ir/emit_ir.asm" "$emit" >"$workdir/emit_ir.asmlog" 2>&1; then
    chmod +x "$emit"
    ok "emit_ir.asm: assembles"
  else
    bad "emit_ir.asm: assemble failed -- no IR test can run"
    sed 's/^/         /' "$workdir/emit_ir.asmlog"
    rm -rf "$workdir"
    return
  fi
  # The harness is held to the compiler's own closed nine: it is built from
  # the compiler's modules and is what every result below is trusted through.
  if "$AUDIT" "$emit" >"$workdir/emit_ir.auditlog" 2>&1; then
    ok "emit_ir: audit verdict=pass (the compiler's nine syscalls)"
  else
    bad "emit_ir: audit verdict=fail"
    sed 's/^/         /' "$workdir/emit_ir.auditlog"
  fi

  local found=0 src
  shopt -s nullglob
  for src in "$REPO_ROOT"/tests/ir/*.ir; do
    found=$((found + 1))
    local name; name="$(basename "$src" .ir)"
    local w="$workdir/$name"
    echo "-- $name.ir"
    local directive; directive="$(directive_of "$src")"
    if [[ -z "$directive" ]]; then
      bad "$name: no '; TEST:' directive"; continue
    fi
    # shellcheck disable=SC2086
    parse_run_keys "$name" $directive || continue
    if [[ -n "$k_sources" || "$k_exsc_exit" != "0" || "$k_status" != "run" ||
          -n "$k_c_differentia" ]]; then
      bad "$name: sources=/exsc-exit=/status=/c-differentia= belong to tests/programs/, not an IR fixture"
      continue
    fi

    local erc=0
    "$emit" <"$src" >"$w.asm" 2>"$w.emitlog" || erc=$?
    if [[ "$k_emit_exit" != "0" ]]; then
      if [[ -n "$k_expect_exit$k_abort$k_stdout" ]]; then
        bad "$name: emit-exit=$k_emit_exit is a rejection; nothing runs, so no expect-exit=/abort=/stdout="
        continue
      fi
      if [[ "$erc" == "$k_emit_exit" ]]; then
        ok "$name: emit_ir refuses it, exit=$erc (expected $k_emit_exit)"
        sed 's/^/         /' "$w.emitlog"
      else
        bad "$name: emit_ir exit=$erc, expected $k_emit_exit"
        sed 's/^/         /' "$w.emitlog"
      fi
      continue
    fi
    want_one_outcome "$name" || continue
    if [[ "$erc" -ne 0 ]]; then
      bad "$name: emit_ir exit=$erc"
      sed 's/^/         /' "$w.emitlog"; continue
    fi
    if "$FASMG" "$w.asm" "$w.bin" >"$w.asmlog" 2>&1; then
      chmod +x "$w.bin"
      ok "$name: emitted program assembles"
    else
      bad "$name: fasmg cannot assemble the emitted program"
      sed 's/^/         /' "$w.asmlog"; continue
    fi
    check_run "$name" "$w.bin" "$w" "$k_expect_exit" "$k_abort" "$k_stdout" "$k_stdin" || true
  done
  shopt -u nullglob
  rm -rf "$workdir"
  floor_check "IR fixtures in tests/ir/" "$found" "$IR_FIXTURE_FLOOR" IR_FIXTURE_FLOOR
}

run_differential_tests() {
  # -------------------------------------------------------------------------
  # ADR 0012's differential test, over tests/ir/*.ir. Two backends compile
  # the same IR; the three OBSERVABLES must agree -- the bytes written to
  # stdout, the exit status, and trap-or-not with the abort KIND when both
  # trap. Not the emitted text: there is none in common, which is the whole
  # point of having a reference (docs/design/c-backend.md D6).
  #
  # HOW AGREEMENT IS CHECKED WITHOUT RUNNING BOTH BINARIES HERE. Each
  # fixture's `; TEST:` directive IS the reference's pinned behaviour --
  # run_ir_tests above has just assembled the reference's output and held it
  # to exactly these keys. So checking the C build against the same directive
  # checks it against the reference, and a fixture whose C build met its own
  # directive but differed from the reference would have to be a fixture
  # whose directive the reference does not meet, which run_ir_tests failed on
  # one phase earlier. The directive is the shared expectation, and both
  # phases are held to it.
  #
  # FOUR BUILDS PER FIXTURE: {gcc, clang} x {-O0, -O2}, every one with
  # -fsanitize=undefined -fno-sanitize-recover=all, linked with
  # tests/c/exsrt_shim.c. A UBSan report is a failure on its own -- it aborts
  # the process, so it shows up as a wrong exit status, and its text is
  # printed. Both compilers because ADR 0012 asks for both and the prologue's
  # incantations are the first thing that differs between them (the measured
  # table in c-backend.md D3 has three rows where they disagree); both
  # optimisation levels because a trapping helper has a `__has_builtin` fast
  # path at -O2 that -O0 does not take.
  #
  # REFUSALS ARE CHECKED FOR PARITY. A fixture with `emit-exit=N` requires
  # emit_c to exit N too, unless `c-emit-exit=` says the two legitimately
  # differ. That covers the ten rejection fixtures -- 3 parse, 4 emitter,
  # 5 verifier -- and it is a real check: emit_c runs the SAME parser and the
  # SAME verifier, so a divergence there would mean the C path had somehow
  # reached a different front end.
  #
  # A MISSING COMPILER IS A FAILURE OF THE PHASE, NOT A SKIP -- the same
  # choice run_ir_tests makes for a missing fasmg. A harness that finds
  # nothing must not report success; flake.nix puts gcc and clang in
  # checks.test's inputs precisely so this never has to be a skip.
  # -------------------------------------------------------------------------
  echo "== differential tests (C backend vs the reference, tests/ir/) =="
  if ! command -v "$FASMG" >/dev/null 2>&1; then
    bad "fasmg not found on PATH -- cannot build emit_c"
    return
  fi
  local cc missing=0
  for cc in gcc clang; do
    if ! command -v "$cc" >/dev/null 2>&1; then
      bad "$cc not found on PATH -- the differential phase needs both (ADR 0012)"
      missing=1
    fi
  done
  [[ "$missing" -eq 1 ]] && return

  local workdir; workdir="$(mktemp -d)"
  local emit="$workdir/emit_c"
  if "$FASMG" "$REPO_ROOT/tests/ir/emit_c.asm" "$emit" >"$workdir/emit_c.asmlog" 2>&1; then
    chmod +x "$emit"
    ok "emit_c.asm: assembles"
  else
    bad "emit_c.asm: assemble failed -- no differential test can run"
    sed 's/^/         /' "$workdir/emit_c.asmlog"
    rm -rf "$workdir"
    return
  fi
  # emit_c is held to the compiler's own closed nine exactly as emit_ir is,
  # and for the same reason: it is what every result below is trusted
  # through. Note what this also proves -- emit_c contains no `execve`,
  # because there is none to contain. exsc never runs a C compiler; this
  # script does, and this script is verification tooling.
  if "$AUDIT" "$emit" >"$workdir/emit_c.auditlog" 2>&1; then
    ok "emit_c: audit verdict=pass (the compiler's nine syscalls)"
  else
    bad "emit_c: audit verdict=fail"
    sed 's/^/         /' "$workdir/emit_c.auditlog"
  fi

  # -------------------------------------------------------------------------
  # D2's three-way split, end to end against a real exsc. This is NOT a unit
  # fixture because it cannot be one: `drv_aedifica` needs argc/argv and real
  # files on disk, which is the same reason driver_status.asm gives for not
  # opening one ("a fixture that opens one has to know where it is"). The
  # PARSE half -- that `--emitte c` is a fourth value of one table -- is
  # tests/unit/driver_emitte.asm's; this is the half that needs a process.
  #
  # Each row is a refusal with a DIFFERENT reason, and the point of checking
  # all of them is that they must not collapse into one:
  #   --emitte c without -o        usage error (2): an artifact needs a file
  #   an unknown triple            exit 4: not a row of the table
  #   riscv64-linux without c      exit 4: a row whose only backend is the C
  #                                one, asked for without asking for it
  #   mips64-none-o64 without c    exit 4: the same, for a second reason --
  #                                its addresses are 32-bit and the reference
  #                                emitter refuses a narrow address by name
  # (mips64-none-o64 WITH --emitte c was a fourth refusal until C4, on the
  # ground that this build could not honour the row. It can; ADR 0015.)
  #
  # And three acceptances, which prove the refusals are not simply
  # "everything fails". The two 64-bit rows emit the SAME BYTES -- D5's
  # determinism claim reduced to its smallest testable form. The 32-bit row
  # must emit DIFFERENT bytes, in more than the one `_Static_assert` line,
  # because the row sets `mensura` as well and that reaches every emitted
  # width; a one-line diff would mean the plumbing did nothing.
  # The last check is the one that ties the two harnesses together:
  # `exsc --emitte c` and `emit_c` must agree byte for byte on the same
  # program, the same equality tests/ir/saluta.ir's header records for the
  # reference side.
  local exsc="$workdir/exsc"
  if "$FASMG" "$REPO_ROOT/compiler/x86_64/exsc.asm" "$exsc" >"$workdir/exsc.asmlog" 2>&1; then
    chmod +x "$exsc"
    ok "exsc: assembles with both backends in it"
    local hw=( "$REPO_ROOT/examples/saluta.exsc" "$REPO_ROOT/examples/imprime.exsc"
               "$REPO_ROOT/examples/initium.exsc" )
    # Every row also checks that STDOUT IS EMPTY. `--emitte c` is the fourth
    # value of one table and the only one naming an ARTIFACT rather than a
    # stage dump: D2 says it "writes the translation unit there, writes
    # nothing to stdout", and driver/run.inc's own -o check says the same.
    # It did not hold. `drv_emit_dump` has three arms -- tokens, cst, and ast
    # as the FALL-THROUGH -- so `--emitte c` dumped the typed AST to stdout
    # beside the unit, every time, and no C1 check saw it because every call
    # site redirects stdout to a file nobody reads. This assertion is why the
    # next such regression is loud. It is checked on the REFUSALS too: a
    # refusal that dumped a stage would be a stage dump for an invocation
    # that produced no artifact.
    drv_case() {  # LABEL WANT-EXIT ARGS...
      local lbl="$1" want="$2"; shift 2
      local rc=0
      "$exsc" "$@" >"$workdir/drv.out" 2>"$workdir/drv.err" || rc=$?
      if [[ "$rc" == "$want" ]]; then
        ok "driver: $lbl -> exit $rc"
      else
        bad "driver: $lbl -> exit $rc, expected $want"
        sed 's/^/         /' "$workdir/drv.err"
      fi
      if [[ -s "$workdir/drv.out" ]]; then
        bad "driver: $lbl wrote $(wc -c <"$workdir/drv.out" | tr -d ' ') bytes to stdout; --emitte c writes its unit to OUT and nothing to stdout (c-backend.md D2)"
        head -c 300 "$workdir/drv.out" | sed 's/^/         /'
      else
        ok "driver: $lbl wrote nothing to stdout"
      fi
    }
    drv_case "--emitte c without -o" 2 \
      aedifica --hospes x86_64-linux --emitte c "${hw[@]}"
    drv_case "--hospes aarch64-linux (not in the table)" 4 \
      aedifica --hospes aarch64-linux --emitte c "${hw[@]}" -o "$workdir/a.c"
    drv_case "--hospes mips64-none-o64 without --emitte c" 4 \
      aedifica --hospes mips64-none-o64 "${hw[@]}" -o "$workdir/a.asm"
    drv_case "--hospes riscv64-linux without --emitte c" 4 \
      aedifica --hospes riscv64-linux "${hw[@]}" -o "$workdir/a.asm"
    drv_case "--hospes x86_64-linux --emitte c" 0 \
      aedifica --hospes x86_64-linux --emitte c "${hw[@]}" -o "$workdir/x86.c"
    drv_case "--hospes riscv64-linux --emitte c" 0 \
      aedifica --hospes riscv64-linux --emitte c "${hw[@]}" -o "$workdir/rv.c"
    drv_case "--hospes mips64-none-o64 --emitte c" 0 \
      aedifica --hospes mips64-none-o64 --emitte c "${hw[@]}" -o "$workdir/o64.c"
    if cmp -s "$workdir/x86.c" "$workdir/rv.c"; then
      ok "driver: the two 64-bit rows emit byte-identical units"
    else
      bad "driver: the two 64-bit rows differ, and both are 64-bit"
    fi
    # The 32-bit row must emit something DIFFERENT, and the difference must
    # not be confined to the prologue's one `_Static_assert` line. Reading
    # `__bfc_prog_hospes` alone gives the impression that the assert is all a
    # row contributes; it is not, because the row also sets `mensura`, and
    # that is written through the unit as literal widths in every
    # `exsi_norm_u`, `exsi_add_u` and `exsi_ld_*`. A one-line diff here would
    # mean the width plumbing silently did nothing -- which is exactly the
    # error ADR 0015 was written to correct, so it is checked rather than
    # assumed.
    if cmp -s "$workdir/x86.c" "$workdir/o64.c"; then
      bad "driver: the 32-bit row emitted the same unit as the 64-bit one"
    else
      local o64diff
      o64diff=$(diff "$workdir/x86.c" "$workdir/o64.c" | grep -c '^[<>]' || true)
      # 2 lines would be the assert alone (one `<`, one `>`), so the
      # threshold is the next change after it. On this hello world the
      # diff is exactly 4: the assert, and `exsrt_scriptor_scribe`'s
      # `mensura` result gaining an `exsi_norm_u(..., 32)`. On the
      # StreamDB unit it is in the thousands.
      if [[ "$o64diff" -ge 4 ]]; then
        ok "driver: mips64-none-o64 differs from x86_64-linux in $o64diff lines, more than the assert's 2"
      else
        bad "driver: mips64-none-o64 differs in only $o64diff lines -- at 2 that is the assert alone, so mensura=32 never reached the text"
      fi
    fi
    if grep -q '_Static_assert(sizeof(void \*) == 4,' "$workdir/o64.c"; then
      ok "driver: the mips64-none-o64 unit asserts 32-bit addresses"
    else
      bad "driver: the mips64-none-o64 unit does not assert sizeof(void *) == 4"
    fi
    if "$emit" <"$REPO_ROOT/tests/ir/saluta.ir" >"$workdir/hw.c" 2>/dev/null &&
       cmp -s "$workdir/hw.c" "$workdir/x86.c"; then
      ok "driver: exsc --emitte c is byte-identical to emit_c on the hello world"
    else
      bad "driver: exsc --emitte c and emit_c disagree on the hello world"
      diff "$workdir/hw.c" "$workdir/x86.c" 2>&1 | head -20 | sed 's/^/         /'
    fi
  else
    bad "exsc.asm failed to assemble -- the driver's half of D2 cannot be checked"
    sed 's/^/         /' "$workdir/exsc.asmlog"
  fi

  local shim="$REPO_ROOT/tests/c/exsrt_shim.c"
  if [[ ! -f "$shim" ]]; then
    bad "tests/c/exsrt_shim.c is missing -- nothing can be linked"
    rm -rf "$workdir"; return
  fi
  # Two suppressions, each for something that is NOT a property of the
  # emitted C, each named rather than folded into a blanket -w:
  #
  #   -Wno-unused-function   The prologue and the exsi_* helpers are ONE
  #     FIXED BLOB (c-backend.md D5: the text is a function of exsc's bytes),
  #     so a unit that uses no shifts still carries exsi_shl_u. GCC is silent
  #     about an unused `static inline`; Clang warns, 18 times per unit.
  #     Measured, both ways; recorded as c-backend.md finding 15. The blob is
  #     the design, not a defect, and a consumer of the emitted C passes this
  #     same flag (Kiln will) exactly as one does for any header-shaped set
  #     of inline helpers. Everything OUTSIDE the blob -- every line the
  #     lowering actually emits -- is clean under -Wall -Wextra -pedantic
  #     with nothing suppressed, which is the claim that matters and is what
  #     the 39 lowerable fixtures demonstrate.
  #
  #   -Wno-cpp / -Wno-#warnings   nixpkgs' compiler wrappers inject
  #     -D_FORTIFY_SOURCE=2 AFTER the caller's flags, so neither
  #     -U_FORTIFY_SOURCE nor -D_FORTIFY_SOURCE=0 wins (measured: both still
  #     leave the warning). glibc's <features.h> then `#warning`s at -O0,
  #     because fortification needs optimisation. That is this HOST's
  #     packaging talking to its own libc from inside a system header; no
  #     emitted line is involved, and it was 78 warnings in the log for a
  #     fact about nixpkgs. The two spellings are the same suppression under
  #     the two compilers, and both are passed to both -- each accepts the
  #     other's as an unknown-warning option without complaint.
  local cflags="-std=c11 -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=all"
  cflags="$cflags -Wno-unused-function"

  local found=0 builds=0 src
  shopt -s nullglob
  for src in "$REPO_ROOT"/tests/ir/*.ir; do
    found=$((found + 1))
    local name; name="$(basename "$src" .ir)"
    local w="$workdir/$name"
    echo "-- $name.ir"
    local directive; directive="$(directive_of "$src")"
    if [[ -z "$directive" ]]; then
      bad "$name: no '; TEST:' directive"; continue
    fi
    # shellcheck disable=SC2086
    parse_run_keys "$name" $directive || continue

    # The verdict this fixture's C build must reach: its own c-emit-exit= if
    # it has one, else parity with the reference's emit-exit=.
    local want_emit="$k_emit_exit"
    local why="parity with emit-exit="
    if [[ -n "$k_c_emit_exit" ]]; then
      want_emit="$k_c_emit_exit"
      why="c-emit-exit= (a stated difference)"
    fi

    local erc=0
    "$emit" <"$src" >"$w.c" 2>"$w.emitlog" || erc=$?
    if [[ "$want_emit" != "0" ]]; then
      if [[ "$erc" == "$want_emit" ]]; then
        ok "$name: emit_c refuses it, exit=$erc ($why)"
        sed 's/^/         /' "$w.emitlog"
      else
        bad "$name: emit_c exit=$erc, expected $want_emit ($why)"
        sed 's/^/         /' "$w.emitlog"
      fi
      continue
    fi
    if [[ "$erc" -ne 0 ]]; then
      bad "$name: emit_c exit=$erc, expected 0 ($why)"
      sed 's/^/         /' "$w.emitlog"; continue
    fi
    want_one_outcome "$name" || continue

    local opt
    for cc in gcc clang; do
      for opt in -O0 -O2; do
        local tag="$name [$cc $opt]"
        local bin="$w.$cc$opt.bin"
        local nowarn="-Wno-cpp"
        [[ "$cc" == clang ]] && nowarn="-Wno-#warnings"
        # shellcheck disable=SC2086
        if ! "$cc" $cflags $nowarn "$opt" -o "$bin" "$w.c" "$shim" >"$w.cclog" 2>&1; then
          bad "$tag: the emitted C does not compile"
          sed 's/^/         /' "$w.cclog"
          continue
        fi
        # A warning is not a failure, but it is reported: c-backend.md D3
        # makes `-Wall -Wextra` clean the target, and a warning that nobody
        # sees is a target nobody holds.
        if [[ -s "$w.cclog" ]]; then
          note "$tag: compiler said:"
          sed 's/^/         /' "$w.cclog"
        fi
        builds=$((builds + 1))
        check_run "$tag" "$bin" "$w.$cc$opt" \
                  "$k_expect_exit" "$k_abort" "$k_stdout" "$k_stdin" noaudit || true
      done
    done
  done
  shopt -u nullglob
  floor_check "IR fixtures in tests/ir/" "$found" "$IR_FIXTURE_FLOOR" IR_FIXTURE_FLOOR
  floor_check "differential builds run and checked" "$builds" \
              "$DIFFERENTIAL_BUILD_FLOOR" DIFFERENTIAL_BUILD_FLOOR

  # =========================================================================
  # The same test over tests/programs/, which is what C2's row still owed:
  # "the 22 tests/programs/ directories (they need `exsc … --emitte c -o
  # out.c` per directory, which run_program_tests does not yet drive)".
  #
  # WHY HERE AND NOT A SIXTH PHASE. This phase already has the three things
  # a program loop needs and a sibling would have to build again: the `exsc`
  # binary (assembled above for D2's driver rows, ~2 s), the two compilers'
  # presence check, and `$cflags` with the two suppressions argued at
  # length. A sixth phase would be a third `fasmg exsc.asm` in one run and a
  # second copy of that reasoning. The phase's name already says "C backend
  # vs the reference"; the corpus is now both corpora.
  #
  # WHAT IS COMPARED is D6's three observables, exactly as for the IR
  # fixtures and by the same argument: the directory's TEST directive is the
  # reference's pinned behaviour -- run_program_tests has just held the
  # reference build to it -- so holding each C build to the same directive
  # holds it to the reference. stdout bytes (`cmp` against expected.out or
  # stdout=), exit status, and trap-or-not with the abort kind (`abort=N`
  # is SIGILL plus a line ending `abortus N`, which the shim's
  # exsrt_abortus writes). Stderr is otherwise not compared: the reference's
  # prelude and the shim write different English before `abortus`, and the
  # English is not promised.
  #
  # ELIGIBILITY IS DECLARED, NEVER INFERRED. A directory with no
  # `c-differentia=` key is eligible, and its four builds must agree; there
  # is no glob in this loop that quietly passes over a name. See
  # parse_run_keys for the closed reason set.
  #
  # ONE UNIT, MANY DIRECTORIES. Sixteen of the eligible directories share a
  # `sources=` with another -- the four streamdb_* are one unit over four
  # containers, the four receptio_* one unit over four WAVs, three
  # hydramodem_* one unit -- so `exsc --emitte c` runs per DIRECTORY (that
  # is the invocation C2 owed) but the four C builds are reused when the
  # emitted unit is byte-identical to one already built. That reuse is
  # itself a check, and a sharp one: two directories naming the same unit
  # whose emitted C differed would be a D5 determinism failure inside a
  # single run of one `exsc`, and it is reported as one rather than silently
  # recompiled.
  echo "== differential tests (C backend vs the reference, tests/programs/) =="
  local pfound=0 pagree=0 pbuilds=0 pskip=0 pdefer=0 dir
  local -A unit_of=()        # sources= key -> the work prefix whose binaries
                             # are already built for that exact unit text
  shopt -s nullglob
  for dir in "$REPO_ROOT"/tests/programs/*/; do
    dir="${dir%/}"
    local name; name="$(basename "$dir")"
    local w="$workdir/p_$name"
    if [[ ! -f "$dir/TEST" ]]; then
      bad "$name: no TEST file"; continue
    fi
    local directive; directive="$(grep -m1 '^TEST:' "$dir/TEST" || true)"
    directive="${directive#TEST:}"
    if [[ -z "$directive" ]]; then
      bad "$name: TEST has no 'TEST:' line"; continue
    fi
    # shellcheck disable=SC2086
    parse_run_keys "$name" $directive || continue

    if [[ -n "$k_c_differentia" ]]; then
      pskip=$((pskip + 1))
      note "$name/: INELIGIBLE ($k_c_differentia) -- named, not skipped silently"
      continue
    fi
    # A deferred program has no reference build to agree with (nothing was
    # lowered, assembled or run one phase earlier), so there is nothing to
    # compare against; it is reported and counted SEPARATELY from the
    # declared-ineligible, because the two are different statements and one
    # line reporting both would name neither.
    if [[ "$k_status" != "run" ]]; then
      pdefer=$((pdefer + 1))
      note "$name/: status=$k_status (needs=$k_needs) -- no reference run to agree with"
      continue
    fi
    echo "-- $name/"

    local srcs=()
    program_sources "$name" "$dir" || continue
    srcs=("${p_srcs[@]}")

    local ref="$k_stdout"
    if [[ -f "$dir/expected.out" ]]; then
      if [[ -n "$ref" ]]; then
        bad "$name: both expected.out and stdout= -- which is the reference?"; continue
      fi
      ref="tests/programs/$name/expected.out"
    fi

    # The verdict this directory's C emission must reach: its own
    # c-exsc-exit= if it has one, else parity with the reference's
    # exsc-exit=. Same rule, same defaults, as c-emit-exit= above.
    local want_exsc="$k_exsc_exit"
    local pwhy="parity with exsc-exit="
    if [[ -n "$k_c_exsc_exit" ]]; then
      want_exsc="$k_c_exsc_exit"
      pwhy="c-exsc-exit= (a stated difference)"
    fi

    local crc=0
    "$exsc" aedifica --hospes x86_64-linux "${srcs[@]}" --emitte c -o "$w.c" \
      >"$w.out" 2>"$w.exsclog" || crc=$?
    # D2 again, per directory and not only on the six driver rows: the unit
    # goes to OUT and stdout stays empty.
    if [[ -s "$w.out" ]]; then
      bad "$name: exsc --emitte c wrote $(wc -c <"$w.out" | tr -d ' ') bytes to stdout"
      head -c 300 "$w.out" | sed 's/^/         /'
    fi
    if [[ "$want_exsc" != "0" ]]; then
      if [[ "$crc" == "$want_exsc" ]]; then
        ok "$name: exsc --emitte c refuses it, exit=$crc ($pwhy)"
      else
        bad "$name: exsc --emitte c exit=$crc, expected $want_exsc ($pwhy)"
        sed 's/^/         /' "$w.exsclog"
      fi
      continue
    fi
    if [[ "$crc" -ne 0 ]]; then
      bad "$name: exsc --emitte c exit=$crc, expected 0 ($pwhy)"
      sed 's/^/         /' "$w.exsclog"; continue
    fi
    want_one_outcome "$name" || continue
    pfound=$((pfound + 1))

    # Reuse the four binaries of an already-built directory naming the same
    # unit, but only after `cmp` says the emitted text really is the same.
    local key="$k_sources"
    [[ -z "$key" ]] && key="own:$name"
    local share="${unit_of[$key]:-}"
    if [[ -n "$share" ]]; then
      if cmp -s "$w.c" "$share.c"; then
        ok "$name: emits the byte-identical unit to $(basename "${share#"$workdir/p_"}") (same sources=; D5)"
      else
        bad "$name: names the same sources= as $(basename "${share#"$workdir/p_"}") and emits DIFFERENT C -- two runs of one exsc over one unit must agree (D5)"
        diff "$share.c" "$w.c" | head -20 | sed 's/^/         /' || true
        share=""      # do not reuse binaries built from other text
      fi
    fi
    if [[ -z "$share" ]]; then
      share="$w"
      unit_of[$key]="$w"
    fi

    local agree=1 opt
    for cc in gcc clang; do
      for opt in -O0 -O2; do
        local tag="$name/ [$cc $opt]"
        local bin="$share.$cc$opt.bin"
        if [[ ! -x "$bin" ]]; then
          local nowarn="-Wno-cpp"
          [[ "$cc" == clang ]] && nowarn="-Wno-#warnings"
          # shellcheck disable=SC2086
          if ! "$cc" $cflags $nowarn "$opt" -o "$bin" "$share.c" "$shim" >"$share.cclog" 2>&1; then
            bad "$tag: the emitted C does not compile"
            sed 's/^/         /' "$share.cclog"
            agree=0; continue
          fi
          if [[ -s "$share.cclog" ]]; then
            note "$tag: compiler said:"
            sed 's/^/         /' "$share.cclog"
          fi
        fi
        pbuilds=$((pbuilds + 1))
        if check_run "$tag" "$bin" "$w.$cc$opt" \
                     "$k_expect_exit" "$k_abort" "$ref" "$k_stdin" noaudit; then :
        else agree=0; fi
      done
    done
    [[ "$agree" -eq 1 ]] && pagree=$((pagree + 1))
  done
  shopt -u nullglob
  rm -rf "$workdir"
  note "$pskip program directories declared INELIGIBLE by name (c-differentia=)"
  note "$pdefer program directories status=deferred (no reference run to agree with)"
  floor_check "program directories eligible and agreeing under all four builds" \
    "$pagree" "$DIFFERENTIAL_PROGRAM_FLOOR" DIFFERENTIAL_PROGRAM_FLOOR
  floor_check "differential program builds run and checked" "$pbuilds" \
    "$DIFFERENTIAL_PROGRAM_BUILD_FLOOR" DIFFERENTIAL_PROGRAM_BUILD_FLOOR
  if [[ "$pagree" -ne "$pfound" ]]; then
    bad "$pfound program directories were eligible but only $pagree agreed with the reference under all four builds"
  fi
}

run_program_tests() {
  # -------------------------------------------------------------------------
  # tests/programs/<name>/ -- one directory per program. Its expectations
  # are in <name>/TEST, the first line beginning `TEST:` (`#` lines are
  # comments), keys above plus:
  #   sources=A,B   the compilation unit, repo-root-relative, in this order
  #                 (spec 12: the files form ONE unit). Used to point at
  #                 sources that live elsewhere -- examples/ -- rather than
  #                 copying them. Absent: the directory's own *.exsc, in
  #                 byte order (LC_ALL=C, never the locale's collation).
  #                 Both at once is refused as ambiguous.
  #   exsc-exit=N   exsc's exit status (default 0), as the shell reports it
  #                 (132 = an `rassert` in the compiler). Non-zero: nothing
  #                 is assembled or run.
  #   status=deferred needs=A,B
  #                 the program is WRITTEN AHEAD of the machinery that can
  #                 run it (the conformance suite's own word and rule): it is
  #                 compiled WITHOUT -o -- lexed, parsed, type-checked -- and
  #                 that must exit 0, which is a real check and is counted as
  #                 one; nothing is lowered to a file, assembled or run, the
  #                 directory is reported DEFERRED with what it waits on, and
  #                 it is NEVER counted toward PROGRAM_FIXTURE_FLOOR or as a
  #                 program that ran. Its expect-exit=/abort=/stdout= stay in
  #                 the directive, so un-deferring is deleting two keys.
  #                 needs= is required; status=run is the default.
  # A file <name>/expected.out is the stdout reference when present (as if
  # stdout=tests/programs/<name>/expected.out); stdout= and expected.out
  # together are refused as ambiguous.
  #
  # Compiled exactly as tools/publish-gate.sh compiles the hello world:
  # `exsc aedifica --hospes x86_64-linux SRC... -o OUT`, then `fasmg OUT
  # BIN` with the vendored INCLUDE.
  # -------------------------------------------------------------------------
  echo "== program run tests (tests/programs/) =="
  if ! command -v "$FASMG" >/dev/null 2>&1; then
    bad "fasmg not found on PATH -- cannot run program tests"
    return
  fi
  local workdir; workdir="$(mktemp -d)"
  local exsc="$workdir/exsc"
  # Built here rather than taken from build/: the Nix check sandbox has no
  # build/, and a stale build/exsc would test yesterday's compiler.
  if "$FASMG" "$REPO_ROOT/compiler/x86_64/exsc.asm" "$exsc" >"$workdir/exsc.asmlog" 2>&1; then
    chmod +x "$exsc"
    note "built exsc from compiler/x86_64/exsc.asm for this phase"
  else
    bad "compiler/x86_64/exsc.asm failed to assemble -- no program test can run"
    sed 's/^/         /' "$workdir/exsc.asmlog"
    rm -rf "$workdir"
    return
  fi

  local found=0 deferred=0 dir
  shopt -s nullglob
  for dir in "$REPO_ROOT"/tests/programs/*/; do
    dir="${dir%/}"
    local name; name="$(basename "$dir")"
    local w="$workdir/$name"
    echo "-- $name/"
    if [[ ! -f "$dir/TEST" ]]; then
      bad "$name: no TEST file"; continue
    fi
    local directive; directive="$(grep -m1 '^TEST:' "$dir/TEST" || true)"
    directive="${directive#TEST:}"
    if [[ -z "$directive" ]]; then
      bad "$name: TEST has no 'TEST:' line"; continue
    fi
    # shellcheck disable=SC2086
    parse_run_keys "$name" $directive || continue
    if [[ "$k_emit_exit" != "0" ]]; then
      bad "$name: emit-exit= belongs to tests/ir/, not a program"; continue
    fi

    local srcs=()
    program_sources "$name" "$dir" || continue
    srcs=("${p_srcs[@]}")

    local ref="$k_stdout"
    if [[ -f "$dir/expected.out" ]]; then
      if [[ -n "$ref" ]]; then
        bad "$name: both expected.out and stdout= -- which is the reference?"; continue
      fi
      ref="tests/programs/$name/expected.out"
    fi

    if [[ "$k_status" == "deferred" ]]; then
      if [[ "$k_exsc_exit" != "0" ]]; then
        bad "$name: status=deferred is a program written ahead of its backend; exsc-exit= is a refusal"
        continue
      fi
      want_one_outcome "$name" || continue
      deferred=$((deferred + 1))
      local drc=0
      "$exsc" aedifica --hospes x86_64-linux "${srcs[@]}" \
        >"$w.exsclog" 2>&1 || drc=$?
      if [[ "$drc" -eq 0 ]]; then
        ok "$name: type-checks clean (exsc aedifica, no -o)"
      else
        bad "$name: exsc aedifica (no -o) exit=$drc -- a deferred program must still check"
        sed 's/^/         /' "$w.exsclog"
      fi
      note "$name: DEFERRED (needs=$k_needs) -- not assembled, not run, not counted as passing"
      continue
    fi
    found=$((found + 1))

    local crc=0
    "$exsc" aedifica --hospes x86_64-linux "${srcs[@]}" -o "$w.asm" \
      >"$w.exsclog" 2>&1 || crc=$?
    if [[ "$k_exsc_exit" != "0" ]]; then
      if [[ -n "$k_expect_exit$k_abort$ref" ]]; then
        bad "$name: exsc-exit=$k_exsc_exit means nothing runs, so no expect-exit=/abort=/stdout="
        continue
      fi
      if [[ "$crc" == "$k_exsc_exit" ]]; then
        ok "$name: exsc refuses it, exit=$crc (expected $k_exsc_exit)"
      else
        bad "$name: exsc exit=$crc, expected $k_exsc_exit"
        sed 's/^/         /' "$w.exsclog"
      fi
      continue
    fi
    want_one_outcome "$name" || continue
    if [[ "$crc" -ne 0 ]]; then
      bad "$name: exsc exit=$crc"
      sed 's/^/         /' "$w.exsclog"; continue
    fi
    if "$FASMG" "$w.asm" "$w.bin" >"$w.asmlog" 2>&1; then
      chmod +x "$w.bin"
      ok "$name: compiles and assembles (${#srcs[@]} source(s))"
    else
      bad "$name: fasmg cannot assemble exsc's output"
      sed 's/^/         /' "$w.asmlog"; continue
    fi
    check_run "$name" "$w.bin" "$w" "$k_expect_exit" "$k_abort" "$ref" "$k_stdin" || true
  done
  shopt -u nullglob
  rm -rf "$workdir"
  floor_check "program directories in tests/programs/ that run" "$found" \
    "$PROGRAM_FIXTURE_FLOOR" PROGRAM_FIXTURE_FLOOR
  note "$deferred program directories DEFERRED (type-checked only; not counted as passing)"
}

# ---------------------------------------------------------------------------
# PHASE 6: the cross phase -- §14 entry 25, ADR 0015.
#
# THE CLAIM. A unit emitted for `--hospes mips64-none-o64`, cross-compiled
# and RUN big-endian with 32-bit addresses, produces the reference backend's
# observables: stdout bytes, exit status, and trap-or-not with the abort kind.
# The same three D6 compares on, checked by the same `check_run`, so one key
# reads three backends.
#
# WHY IT IS WORTH ITS RUNTIME, beyond the N64. Spec §9.5 claims the emitted
# text "depends on no assumption" about the host's byte order -- `nativus`
# places go through helpers the C compiler picks from its own target, and
# `maior`/`minor` places byte by byte. That claim has been in the spec since
# the section was written and NOTHING had ever tested it, because every
# target in this repository's closure is little-endian. This phase is the
# first big-endian execution of anything this compiler produced.
#
# WHY n32 AND NOT o64. clang does not implement `-mabi=o64` ("unknown target
# ABI"), and a GCC that does is not in the binary cache, so putting one in
# the closure would mean building a cross toolchain from source on every
# fresh checkout. n32 shares with o64 every property the emitted text could
# depend on -- big-endian, MIPS-III, 32-bit addresses, 64-bit registers --
# and, decisively, THE TEXT UNDER TEST IS THE SAME TEXT, because both are
# `--hospes mips64-none-o64`. What differs is argument passing and the
# compiler, and every call in the built program has both sides compiled
# together here, so no ABI boundary is crossed that is not internal.
# ADR 0015 decision 5 states the gap: the o64 ABI itself is [UNTESTED] in
# this repository, and linking into a ROM is [OPEN].
#
# WHY IT IS OPT-IN. `cross=yes` on a tests/programs/ directory, and nothing
# else. Emulation is 10-50x, so the 96 directories cannot all be run; the
# differential phase is opt-OUT because a host build is cheap. Declared per
# directory rather than matched by name in here, for the reason
# c-differentia= gives at length.
#
# A MISSING TOOL IS A FAILURE OF THE PHASE, NOT A SKIP -- the rule the
# differential phase already applies to gcc and clang. A silent skip is the
# false green this file's floors exist to prevent.
run_cross_tests() {
  echo "== cross phase: mips64-none-o64, big-endian, under emulation =="

  local shim="$REPO_ROOT/tests/c/exsrt_shim_mips.c"
  if [[ ! -f "$shim" ]]; then
    bad "cross: tests/c/exsrt_shim_mips.c is missing -- nothing can be linked"
    return
  fi
  local tool
  for tool in clang ld.lld qemu-mipsn32; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      bad "cross: $tool not found on PATH -- this phase needs it (ADR 0015 decision 4)"
      return
    fi
  done
  # run_binary runs the child with env={}, so it has no PATH of its own and a
  # bare `qemu-mipsn32` would not resolve. Every other caller passes an
  # absolute path to the binary itself; the runner needs the same treatment.
  local qemu
  qemu="$(command -v qemu-mipsn32)"

  local workdir
  workdir="$(mktemp -d)" || { bad "cross: mktemp failed"; return; }

  # Assemble exsc here rather than expecting build/exsc: `nix flake check`
  # stages the repository's sources into a sandbox and runs this script, and
  # nothing in that sandbox has run `make`. run_differential_tests builds its
  # own for the same reason.
  local exsc="$workdir/exsc"
  if ! "$FASMG" "$REPO_ROOT/compiler/x86_64/exsc.asm" "$exsc" \
       >"$workdir/exsc.asmlog" 2>&1; then
    bad "cross: exsc.asm failed to assemble -- the cross phase cannot run"
    sed 's/^/         /' "$workdir/exsc.asmlog"
    rm -rf "$workdir"; return
  fi
  chmod +x "$exsc"

  # -G0 -mno-abicalls is not a preference: without it the MIPS ABI addresses
  # small objects through $gp, which a crt0 that is not here would have set
  # up, and the program segfaults before reaching exs_initium. Found by
  # running it. `-fno-builtin` keeps the shim's own memcpy/memset honest.
  local cflags="--target=mips64-unknown-linux-musl -mabi=n32 -march=mips3"
  cflags="$cflags -ffreestanding -fno-builtin -nostdlib -static -O2 -std=c11"
  cflags="$cflags -G0 -mno-abicalls -fno-pic -Wall -Wno-unused-function"
  local ldflags="-fuse-ld=lld -Wl,-e,_start -Wl,--build-id=none"

  local found=0 ran=0 d name ref srcs
  shopt -s nullglob
  for d in "$REPO_ROOT"/tests/programs/*/; do
    name="$(basename "$d")"
    [[ -f "$d/TEST" ]] || continue
    local directive
    directive="$(grep -m1 '^[[:space:]]*TEST:' "$d/TEST" 2>/dev/null || true)"
    [[ -n "$directive" ]] || continue
    directive="${directive#*TEST:}"
    # shellcheck disable=SC2086
    parse_run_keys "cross/$name" $directive || continue
    [[ "$k_cross" == "yes" ]] || continue
    found=$((found + 1))
    if [[ "$k_status" != "run" ]]; then
      note "cross/$name: status=$k_status -- not run"
      continue
    fi

    srcs=()
    if [[ -n "$k_sources" ]]; then
      local IFS=,
      for s in $k_sources; do srcs+=("$REPO_ROOT/$s"); done
      unset IFS
    else
      local f
      for f in "$d"*.exsc; do srcs+=("$f"); done
    fi
    if [[ "${#srcs[@]}" -eq 0 ]]; then
      bad "cross/$name: no sources"; continue
    fi

    local w="$workdir/$name"
    if ! "$exsc" aedifica --hospes mips64-none-o64 "${srcs[@]}" --emitte c \
         -o "$w.c" >"$w.emitlog" 2>&1; then
      bad "cross/$name: exsc --hospes mips64-none-o64 --emitte c failed"
      sed 's/^/         /' "$w.emitlog"; continue
    fi
    if ! grep -q '_Static_assert(sizeof(void \*) == 4,' "$w.c"; then
      bad "cross/$name: the emitted unit does not assert 32-bit addresses"
      continue
    fi
    # NIX_HARDENING_ENABLE= because nixpkgs' cc-wrapper injects flags
    # (-fzero-call-used-regs=used-gpr) that clang cannot apply to a MIPS
    # target and rejects outright, and it warns about any cross --target.
    # Both belong to the wrapper, not to the emitted C. The variable is
    # meaningless off nixpkgs, so setting it costs nothing there.
    # shellcheck disable=SC2086
    if ! env NIX_HARDENING_ENABLE= clang $cflags $ldflags "$w.c" "$shim" \
         -o "$w.bin" >"$w.cclog" 2>&1; then
      bad "cross/$name: clang cannot build the mips64 unit"
      grep -v 'cc-wrapper is currently not designed' "$w.cclog" |
        sed 's/^/         /'; continue
    fi
    grep -v 'cc-wrapper is currently not designed' "$w.cclog" >"$w.ccreal" || true
    [[ -s "$w.ccreal" ]] && note "cross/$name: clang said:$(sed 's/^/ /' "$w.ccreal" | head -3)"
    ok "cross/$name: emits and cross-builds for big-endian MIPS-III, 32-bit addresses"

    ref=""
    if [[ -n "$k_stdout" ]]; then
      ref="$k_stdout"
    elif [[ -f "$d/expected.out" ]]; then
      ref="tests/programs/$name/expected.out"
    fi
    # noaudit: the audit is a statement about what the REFERENCE backend puts
    # in a binary, and this one is clang's. The 120 s timeout is emulation,
    # not slack -- the reference does the corpus in 0.044 s.
    check_run "cross/$name" "$w.bin" "$w" "$k_expect_exit" "$k_abort" "$ref" \
      "$k_stdin" noaudit "$qemu" 120 || true
    ran=$((ran + 1))
  done
  shopt -u nullglob
  rm -rf "$workdir"

  note "discovered $found program directories opted into the cross phase (floor $CROSS_PROGRAM_FLOOR)"
  floor_check "cross program directories" "$found" "$CROSS_PROGRAM_FLOOR" CROSS_PROGRAM_FLOOR
  note "$ran of them ran"
  floor_check "cross runs" "$ran" "$CROSS_PROGRAM_FLOOR" CROSS_PROGRAM_FLOOR
}


# -----------------------------------------------------------------------------
# DEVICE PHASE -- Stage 6 G2: the same program, the same bytes, on an AMD GPU.
#
# Spec 5.5's claim is that a program's bytes do not depend on which of the
# machine's processors ran it. This phase measures it the only way that
# counts: every tests/programs/ directory carrying `device=amdgcn` is emitted
# through the C backend (`--hospes x86_64-linux`: GCN flat pointers are
# 64-bit little-endian, that row's _Static_asserts hold on the device),
# concatenated with tests/c/exsrt_shim_amdgpu.c into ONE translation unit,
# compiled by clang for every GPU agent tools/amd-dispatch finds, dispatched
# as a single workitem, and its output byte-diffed against the same golden
# the reference backend is held to.
#
# ONE TRANSLATION UNIT IS LOAD-BEARING. The kernel descriptor's register
# budget is computed per unit; a kernel linked against a separately compiled
# program runs with a budget sized for the shim alone and corrupts live
# values across calls -- measured (acies_float8 wrote 640 correct bytes and
# then trapped on a garbage operand). tools/amd-dispatch/README.md.
#
# WHY A SUITE-LEVEL FLAG AND NOT JUST THE DIRECTIVE. cross=yes needs qemu,
# which flake.nix provides, so a missing qemu is a failure. A GPU cannot be
# provided by a flake, and `nix flake check` runs this script in a sandbox
# with no /dev/kfd. So the phase runs only under `--device=amdgcn`; WITH the
# flag, every missing piece -- the ROCm runtime, an agent, the shim, clang --
# is a failure, never a skip, exactly as the cross phase treats qemu.
# -----------------------------------------------------------------------------
run_device_tests() {
  echo "== device phase: amdgcn -- the C backend's unit on every AMD GPU present =="
  if [[ -z "$DEVICE" ]]; then
    note "not requested -- run tests/run.sh --device=amdgcn on a machine with an AMD GPU and the ROCm runtime"
    return
  fi
  local shim="$REPO_ROOT/tests/c/exsrt_shim_amdgpu.c"
  if [[ ! -f "$shim" ]]; then
    bad "device: tests/c/exsrt_shim_amdgpu.c is missing -- nothing can be built"
    return
  fi
  local tool
  for tool in cc clang ld.lld; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      bad "device: $tool not found on PATH -- this phase needs it"
      return
    fi
  done
  local workdir
  workdir="$(mktemp -d)" || { bad "device: mktemp failed"; return; }

  local disp="$workdir/amd-dispatch"
  if ! cc -O2 -std=c11 -o "$disp" "$REPO_ROOT/tools/amd-dispatch/amd-dispatch.c" -ldl \
       >"$workdir/disp.cclog" 2>&1; then
    bad "device: tools/amd-dispatch/amd-dispatch.c does not build"
    sed 's/^/         /' "$workdir/disp.cclog"; rm -rf "$workdir"; return
  fi
  ok "device: amd-dispatch builds"
  local agents=()
  if ! "$disp" --list >"$workdir/agents" 2>"$workdir/agents.err"; then
    bad "device: no GPU agent -- $(head -1 "$workdir/agents.err")"
    rm -rf "$workdir"; return
  fi
  mapfile -t agents <"$workdir/agents"
  if [[ "${#agents[@]}" -eq 0 ]]; then
    bad "device: amd-dispatch --list found no agents"; rm -rf "$workdir"; return
  fi
  note "agents: ${agents[*]}"

  local exsc="$workdir/exsc"
  if ! "$FASMG" "$REPO_ROOT/compiler/x86_64/exsc.asm" "$exsc" \
       >"$workdir/exsc.asmlog" 2>&1; then
    bad "device: exsc.asm failed to assemble -- the device phase cannot run"
    sed 's/^/         /' "$workdir/exsc.asmlog"
    rm -rf "$workdir"; return
  fi
  chmod +x "$exsc"

  # NIX_HARDENING_ENABLE= for the same reason the cross phase sets it: the
  # nixpkgs wrapper's -fzero-call-used-regs is refused by the amdgcn target.
  local cflags="--target=amdgcn-amdhsa -O2 -ffreestanding -fno-builtin -nostdlib"
  cflags="$cflags -Wno-unused-command-line-argument"
  local ldflags="-fuse-ld=lld -Wl,-e,exs_amdgcn_entry -Wl,--build-id=none"

  local found=0 ran=0 d name ref srcs a
  shopt -s nullglob
  for d in "$REPO_ROOT"/tests/programs/*/; do
    name="$(basename "$d")"
    [[ -f "$d/TEST" ]] || continue
    local directive
    directive="$(grep -m1 '^[[:space:]]*TEST:' "$d/TEST" 2>/dev/null || true)"
    [[ -n "$directive" ]] || continue
    directive="${directive#*TEST:}"
    # shellcheck disable=SC2086
    parse_run_keys "device/$name" $directive || continue
    [[ "$k_device" == "amdgcn" ]] || continue
    found=$((found + 1))
    if [[ "$k_status" != "run" ]]; then
      note "device/$name: status=$k_status -- not run"
      continue
    fi

    srcs=()
    if [[ -n "$k_sources" ]]; then
      local IFS=,
      for s in $k_sources; do srcs+=("$REPO_ROOT/$s"); done
      unset IFS
    else
      local f
      for f in "$d"*.exsc; do srcs+=("$f"); done
    fi
    if [[ "${#srcs[@]}" -eq 0 ]]; then
      bad "device/$name: no sources"; continue
    fi

    local w="$workdir/$name"
    if ! "$exsc" aedifica --hospes x86_64-linux "${srcs[@]}" --emitte c \
         -o "$w.c" >"$w.emitlog" 2>&1; then
      bad "device/$name: exsc --emitte c failed"
      sed 's/^/         /' "$w.emitlog"; continue
    fi
    { cat "$w.c"; echo; cat "$shim"; } >"$w.one.c"

    ref=""
    if [[ -n "$k_stdout" ]]; then
      ref="$REPO_ROOT/$k_stdout"
    elif [[ -f "$d/expected.out" ]]; then
      ref="$d/expected.out"
    fi
    local input=()
    [[ -n "$k_stdin" ]] && input=(--input "$REPO_ROOT/$k_stdin")

    for a in "${agents[@]}"; do
      # shellcheck disable=SC2086
      if ! env NIX_HARDENING_ENABLE= clang $cflags -mcpu="$a" -c "$w.one.c" \
             -o "$w.$a.o" >"$w.$a.cclog" 2>&1 ||
         ! env NIX_HARDENING_ENABLE= clang $cflags -mcpu="$a" $ldflags "$w.$a.o" \
             -o "$w.$a.co" >>"$w.$a.cclog" 2>&1; then
        bad "device/$name on $a: clang cannot build the amdgcn unit"
        grep -v 'cc-wrapper is currently not designed' "$w.$a.cclog" |
          sed 's/^/         /'; continue
      fi
      local t0 t1 rc=0
      t0="$(date +%s%N)"
      "$disp" --co "$w.$a.co" --agent "$a" --output "$w.$a.out" "${input[@]}" \
        --capacity 4194304 --timeout 120 >"$w.$a.log" 2>&1 || rc=$?
      t1="$(date +%s%N)"
      local report
      report="$(grep -m1 '^amd-dispatch: agent=' "$w.$a.log" | sed 's/^amd-dispatch: //' || true)"
      local ms=$(( (t1 - t0) / 1000000 ))
      if [[ -n "$k_abort" ]]; then
        # the CPU's `abortus N` + SIGILL is the device's exit 111 + the line
        # as the tail of the output buffer (tools/amd-dispatch/README.md)
        if [[ "$rc" -eq 111 ]] && tail -c 64 "$w.$a.out" 2>/dev/null |
             grep -q "exsecutor: abortus ${k_abort}\$"; then
          ok "device/$name on $a: abortus $k_abort recorded on device ($ms ms)"
        else
          bad "device/$name on $a: expected abortus $k_abort, got exit $rc -- $report"
          sed 's/^/         /' "$w.$a.log" | head -5
        fi
      else
        local want="${k_expect_exit:-0}"
        if [[ "$rc" -ne "$want" ]]; then
          bad "device/$name on $a: exit $rc, expected $want -- $report"
          sed 's/^/         /' "$w.$a.log" | head -5
        elif [[ -n "$ref" ]] && ! cmp -s "$w.$a.out" "$ref"; then
          bad "device/$name on $a: output differs from $(basename "$ref") -- $report"
        else
          ok "device/$name on $a: $report ($ms ms)"
        fi
      fi
      ran=$((ran + 1))
    done
  done
  shopt -u nullglob
  rm -rf "$workdir"

  floor_check "device program directories" "$found" "$DEVICE_PROGRAM_FLOOR" DEVICE_PROGRAM_FLOOR
  note "$ran runs over ${#agents[@]} agent(s)"
  floor_check "device runs" "$ran" "$((found * ${#agents[@]}))" "found x agents"
}


run_unit_tests
echo
run_conformance_tests
echo
run_ir_tests
echo
run_program_tests
echo
run_differential_tests
echo
run_cross_tests
echo
run_device_tests
echo
echo "== summary =="
echo "pass: $PASS  fail: $FAIL"
if [[ "$FAIL" -gt 0 ]]; then
  echo "RESULT: FAIL"
  exit 1
fi
echo "RESULT: PASS"
exit 0
