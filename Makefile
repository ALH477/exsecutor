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

.PHONY: all clean test audit reproduce smoke

all: $(OUT)

$(OUT): $(SRC) | build
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
audit: $(EXSC_DEP)
	tools/syscall-audit.sh $(OUT)

# §9.3: byte-identical output under divergent ambient conditions.
reproduce: $(EXSC_DEP)
	tools/reproduce.sh

# Assembles a tiny known-good fixture through the fasmg+INCLUDE toolchain, so
# the toolchain itself can be checked before compiler/x86_64/exsc.asm exists.
# Dependency-free beyond fasmg + coreutils.
smoke: | build
	$(FASMG) tests/unit/smoke.asm build/smoke
	chmod +x build/smoke
	build/smoke

clean:
	rm -rf build
