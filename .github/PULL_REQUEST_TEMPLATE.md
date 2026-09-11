<!--
Tick what's true, strike through (~~like this~~) what does not apply to this
change and say why in one clause. Leaving a line unaddressed is not the same
as ticking it.
-->

## What changed, and why

<!-- What the diff does, in the terms the spec or CLAUDE.md use for it. -->

## What measuring it found

<!--
Include what did not work. CLAUDE.md: "never present a re-derivation as a
restoration," and a design note that turned out wrong belongs here, not in a
quiet fixup a reader would have to diff to find.
-->

## Checklist

- [ ] The spec (`docs/spec/exsecutor-spec-v0.4.md`) is the source of truth. If
      this amends it, the commit message carries the reason -- `docs/spec/` is
      never edited without one.
- [ ] No error code was invented or renumbered. §13 is the only registry; if
      this needed a new `EXS-E` code, §13 was amended first and
      `compiler/x86_64/diag/codes.inc` was regenerated (`tools/gen-codes.py`),
      never hand-edited.
- [ ] The syscall allowlist (`read write close fstat lseek mmap munmap openat
      exit_group`) is unchanged, or the addition is argued here with a stated
      reason -- and `make audit` passes either way, on the real binary.
- [ ] The macro dialect (`compiler/x86_64/macros/`) is unchanged, or the
      change went through review as a whole-tree change per
      `docs/asm-conventions.md`. Every new global `proc`/`slot`/argument name
      was checked against §4.1's burned-name list before being added -- fasmg's
      one flat namespace means a collision fails silently, somewhere else.
- [ ] Every new fixture was proven non-vacuous by mutation: break the thing it
      claims to check, watch it fail, restore it. State the exit code you
      observed when it was broken.
- [ ] `UNIT_FIXTURE_FLOOR` in `tests/run.sh` was raised, in this commit, if
      fixtures were added -- adding fixtures never trips the floor; a
      discovery mechanism silently finding nothing is what it exists to catch.
- [ ] New and touched source is UTF-8, no BOM, LF line endings, NFC (§8.1) --
      the tool that rejects CRLF should not ship with CRLF in it. (Exceptions:
      `vendor/` and `tests/conformance/`, per `.gitattributes`.)
- [ ] Nothing new is ordered by a pointer value or by hash iteration (§9.3) --
      maps stay insertion-ordered (`rt/map.inc`); output is byte-identical
      across directories, times, locales and hostnames.
- [ ] What was run, with the numbers (not just that it was run):

      ```
      tests/run.sh:               <pass/fail count>
      make audit:                 <PASS or the failing syscall>
      make reproduce:              <PASS or where it diverged>
      tools/spec-check.sh:         <PASS or what drifted>
      ```
