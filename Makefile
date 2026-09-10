# Exsecutor compiler build.
#
# Build closure is exactly one tool: fasmg. If this Makefile ever grows a
# second build-path dependency, that is a bug -- see docs/spec §18.1.

FASMG   ?= fasmg
# fasmg resolves `include '...'` two ways: relative to the including file
# first, then via the INCLUDE-listed directories. vendor/fasmg-x86 is where
# the x86-64 instruction-set and ELF64-writer macro packages live (fasmg
# itself is architecture-neutral and ships none of that) -- see
# vendor/fasmg-x86/PROVENANCE.md. `nix develop` sets this to the repo's
# vendor/fasmg-x86 too; this default lets plain `make` work the same way
# outside the devShell, as long as `fasmg` is on PATH.
# Absolute, deliberately. tools/reproduce.sh builds from several different
# working directories on purpose (§9.3: output must not depend on cwd), and a
# relative INCLUDE resolves against whichever directory the child happens to
# be in -- fasmg then reports 'source file not found' for format/format.inc.
# The toolchain's location is fixed; only the source's location varies.
INCLUDE ?= $(CURDIR)/vendor/fasmg-x86
export INCLUDE

SRC     = compiler/x86_64/exsc.asm
OUT     = build/exsc

.PHONY: all clean test audit reproduce smoke spec-check map-order-probe check

all: $(OUT)

# Every .inc and .bin under compiler/ is a real prerequisite: exsc.asm is a
# few dozen lines of `include`, and the ~30 files it pulls in are where all
# the code lives. Without this, editing lexer/lex.inc and running `make`
# silently reuses a stale binary -- which happened, and cost a wrong
# conclusion about whether a lexer fix had worked. fasmg has no depfile
# support, so this is a wildcard rather than generated deps.
EXSC_SRCS = $(shell find compiler -name '*.inc' -o -name '*.bin' 2>/dev/null)

$(OUT): $(SRC) $(EXSC_SRCS) | build
	$(FASMG) $(SRC) $@
	chmod +x $@

build:
	mkdir -p build

# The compiler does not exist yet. tests/run.sh, tools/syscall-audit.sh and
# tools/reproduce.sh are all written to degrade honestly without it -- they
# exercise the fixtures in tests/unit/ and say plainly what they could not
# check. A hard `$(OUT)` prerequisite defeats that: make fails during
# dependency resolution ("No rule to make target 'compiler/x86_64/exsc.asm'")
# before any script runs, so the honest degradation is unreachable. Depend on
# the compiler only once there is a compiler to depend on.
ifneq ($(wildcard $(SRC)),)
  EXSC_DEP = $(OUT)
else
  EXSC_DEP =
endif

test: $(EXSC_DEP)
	tests/run.sh

# §9.3: "No network access, ever, at any phase." Verified, not promised.
#
# Degrades the same way the other scripts do. With no compiler to audit, the
# useful thing is still available: --self-test assembles the tests/unit/
# fixtures and asserts the audit ACCEPTS the clean one and REJECTS the socket
# one, which proves the audit catches what it claims before there is any exsc
# to point it at. Passing a nonexistent $(OUT) instead just exits 2 and checks
# nothing. flake.nix's `audit` check has always used --self-test; this brings
# `make audit` into line with it.
audit: $(EXSC_DEP)
ifneq ($(wildcard $(SRC)),)
	tools/syscall-audit.sh $(OUT)
else
	tools/syscall-audit.sh --self-test
endif

# §9.3: byte-identical output under divergent ambient conditions.
reproduce: $(EXSC_DEP)
	tools/reproduce.sh

# Read-only spec integrity: §13 registry vs. diag/codes.inc, citation
# validity, evidence-marker discipline. No toolchain needed, so it is safe to
# run anywhere and cheap enough to run often.
spec-check:
	tools/spec-check.sh

# rt/map.inc's insertion-order guarantee, checked against two real memory
# layouts rather than asserted. Needs fasmg and the probe fixtures.
map-order-probe:
	tools/rt-map-order-probe.sh

# Everything that can be checked without a compiler, in one target. These were
# each invoked by nothing before -- not make, not flake.nix, not run.sh -- and
# a check that is never run is indistinguishable from one that does not exist.
check: spec-check test audit map-order-probe

# Assembles a tiny known-good fixture through the fasmg+INCLUDE toolchain, so
# the toolchain itself can be checked before compiler/x86_64/exsc.asm exists.
# Dependency-free beyond fasmg + coreutils.
smoke: | build
	$(FASMG) tests/unit/smoke.asm build/smoke
	chmod +x build/smoke
	build/smoke

clean:
	rm -rf build
