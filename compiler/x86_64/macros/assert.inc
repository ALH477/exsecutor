; compiler/x86_64/macros/assert.inc
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 The Exsecutor authors.
;
; DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
;
; This code is free software; you can redistribute it and/or modify it under
; the terms of the GNU General Public License as published by the Free
; Software Foundation, either version 3 of the License, or (at your option)
; any later version.
;
; This code is distributed in the hope that it will be useful, but WITHOUT
; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
; FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
; more details.
;
; You should have received a copy of the GNU General Public License along
; with this code. If not, see <https://www.gnu.org/licenses/>.
;
; Code produced by this compiler is not covered by the GPL --
; see Exception A in LICENSE.EXCEPTION.
; -----------------------------------------------------------------------------
; rassert COND -- a runtime check that traps (via `ud2`) when COND is false,
; and expands to nothing at all -- not even COND's tokens are evaluated --
; when `RELEASE` is defined before this file is included.
;
; NAMED `rassert`, NOT `assert` (docs/asm-conventions.md's assert section,
; "3.4 macros/assert.inc -- assertions, compiled out in release", amended in
; this wave): fasmg has its OWN native, assemble-time `assert` directive --
; confirmed by direct use, with no include beyond fasmg itself: bare
; `assert 1 = 1` assembles cleanly with zero includes. It is also actually
; used, downstream, by vendor/fasmg-x86/format/elf64.inc and elf32.inc
; (`assert SECTION_INDEX <= SHN_LORESERVE`) -- files this project's own
; fixtures do not currently reach (`format ELF64 executable 3` routes
; through elfexe.inc, not elf64.inc), but `assert` is a property of the
; fasmg dialect itself, not of any one included file, so the collision risk
; is real regardless of which fixture happens to exercise it today. Also
; confirmed directly: fasmg lets a user `macro assert? cond` silently
; shadow the native directive with no warning -- the shadow just wins from
; that point on. Native `assert` is an ASSEMBLE-TIME condition (it either
; holds across every pass or assembly fails); this file's check is a
; RUNTIME trap with entirely different semantics (compiles to nothing in a
; release build). Shadowing the name would mean whichever file happens to
; `include` this one second silently redefines what `assert` means for
; every file after it -- exactly the footgun CLAUDE.md's naming discipline
; exists to prevent. `rassert` needs no shadow and creates no ambiguity.
;
; DUPLICATES macros/flow.inc's ~20-line condition parser (`lhs op rhs` with
; word operators `eq ne lt le gt ge`) rather than including that file, per
; this wave's brief: keeping every macro file independently assemblable
; shrinks the blast radius of a change to any one of them. The duplication
; is the point, not an oversight -- see macros/flow.inc's own header for why
; the operators are words (`=`/`<=`/`>=` do not survive `match` as literal
; pattern tokens) and why generated labels are plain, non-dot identifiers
; (`local` rejects a dot-prefixed name outright, in any context).
;
; `ud2` HAS NO SYMBOLIC MNEMONIC in vendor/fasmg-x86 -- confirmed by grep
; (no hit anywhere in the tree) and empirically (bare `ud2` fails to
; assemble, "illegal instruction", the same message fasmg gives any
; unrecognized statement). Emitted here as its well-defined raw encoding
; instead: `db 0x0F, 0x0B`. Verified this actually traps: assembled alone
; into a tiny freestanding ELF64 and run, it raises SIGILL --
; `128 + SIGILL(4) = 132`, exactly the exit code this wave's fixtures check
; for. `tools/syscall-audit.sh` already lists `ud2` among its
; NONWRITING_MNEMONICS (its disassembler, objdump, decodes `0F 0B` back to
; the mnemonic `ud2` even though fasmg has no assembler-side name for it),
; so a debug build with `rassert` in it still passes `make audit` -- no
; syscall site is added by this file, in either build mode.
;
; A condition must never contain a side effect (a call, an increment, a
; flag a later line depends on): code that only runs because `rassert`
; evaluated it is exactly the debug/release skew this macro exists to
; prevent. This file cannot enforce that; it is a discipline for callers.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3 ("Determinism is not
; optional", per CLAUDE.md) -- a `RELEASE` build must be byte-for-byte
; predictable from source, so the condition is elided as tokens, not merely
; skipped at runtime. Where `rassert` sits relative to the CF/eax error
; protocol (an internal contract violation traps here and never touches
; CF) is this wave's own failure-channel synthesis, not spec text and not
; yet written into docs/asm-conventions.md's "1.3 Error protocol: CF / eax"
; (out of this file's author's amendment scope this wave) -- see this
; wave's report for the table and the recommendation to add it there.
; -----------------------------------------------------------------------------

macro rassert? cond&
	if ~ defined RELEASE
		match lhs op rhs, cond
			local Lskip
			cmp	lhs, rhs
			match =eq?, op
				je	Lskip
			else match =ne?, op
				jne	Lskip
			else match =lt?, op
				jl	Lskip
			else match =le?, op
				jle	Lskip
			else match =gt?, op
				jg	Lskip
			else match =ge?, op
				jge	Lskip
			else
				err 'rassert: condition operator must be one of eq ne lt le gt ge'
			end match
			db	0x0F, 0x0B	; ud2 -- traps here iff cond was FALSE
		  Lskip:
		else
			err 'rassert: condition must be exactly "lhs op rhs" (3 tokens)'
		end match
	end if
end macro
