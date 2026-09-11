; compiler/x86_64/driver/driver.inc
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
; The driver/ module's single aggregation point (docs/asm-conventions.md, "7.
; How to add a module", step 3). `compiler/x86_64/exsc.asm` includes THIS
; file, not the individual pieces.
;
; The include graph inside driver/ is a strict CHAIN, never a diamond, for
; the reason rt/vec.inc's header records empirically: fasmg has one flat
; namespace and a file that emits a label, a `struct` or a `proc` fails
; outright the second time it is processed.
;
;   msg.inc      exit statuses, escaping, the usage text
;     <- cli.inc      DrvCtx, the subcommand table, the flag parser
;        <- io.inc       reading the §12 unit's files into one buffer
;           <- dump.inc     `--emitte tokens|cst|ast` (spec 18.2)
;              <- run.inc      the subcommands, and drv_main
;                 <- driver.inc  (this file)
;
; ORDERING OBLIGATION ON THE CONSUMER, stated because it cannot be enforced
; from inside this chain. driver/ USES lexer/, diag/ and rt/ and INCLUDES
; none of them: `compiler/x86_64/lexer/lexer.inc` already pulls in all three,
; and pulling any of them in a second time from here would process a
; `struct`/`proc`-emitting file twice and fail. So exsc.asm must include
; lexer/lexer.inc BEFORE this file. That is not a preference -- `macros/
; struct.inc`, which `struct DrvCtx` needs, is reached exactly once in this
; tree, through rt/arena.inc, and driver/ deliberately does not become the
; second path to it.
;
; msg.inc does include macros/proc.inc and macros/assert.inc directly. Those
; two are macro-only and are already reached many times over (through diag/,
; through rt/, and once from each compiler/shared/unicode/ module); fasmg
; redefines a macro rather than erroring, and the existing tree proves it by
; assembling. They are named there so driver/ says what dialect it is written
; in rather than inheriting it silently.
;
; WHAT THIS MODULE GIVES THE REST OF THE COMPILER:
;   drv_main               -- argc/argv in, process exit status out
;   DrvCtx / DRV_EXIT_*    -- the context r15 points at, and the statuses
;   drv_parse / drv_env_add / drv_u64_parse / drv_ident_ok
;                          -- the command line, addressable by a fixture
;                             without going through a process boundary
;   drv_slurp_unit         -- every SOURCE of the §12 compilation unit, back
;                             to back, into one buffer in the compilation arena
;   drv_src_add / drv_src_ptr / DrvSrc / DRV_SOURCES_MAX
;                          -- the SOURCE list, in command-line order (§12)
;   drv_emit_diag          -- one Diag onto stderr, text or JSON Lines
;   drv_msg / drv_msgu / drv_say / drv_say_out / drv_usage
;                          -- the driver's prose, all of it escaped
;   drv_emit_dump / drv_dump_tokens
;                          -- `--emitte tokens|cst|ast` to stdout (spec
;                             18.2's stage-dump mitigation); `cst`/`ast`
;                             themselves dump through cst_dump/ast_dump,
;                             which already existed and were unreachable
;                             from a command line before this file's dump.inc
;
; WHAT IT DOES NOT GIVE, deliberately: anything that writes an object file.
; There is no backend (§9.2, §16 Stage 3); `aedifica` says so and exits 4
; rather than producing something and calling it a build.
;
; SYSCALLS: openat, fstat, read, close, write -- five of the nine on the
; closed allowlist, every one of them through rt/sys.inc's wrappers, and
; `exit_group` from exsc.asm itself. `--emitte`'s dump adds no syscall of its
; own: `drv_say_out` is the same `write(1)` already on the allowlist, called
; with a different fd literal than `drv_say`'s. No socket-family syscall
; appears anywhere under driver/ (§9.3), and `make audit` checks the
; assembled binary rather than taking this sentence's word for it.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1, §8.3, §9.1, §9.3, §9.5, §12,
; §18.2.
; -----------------------------------------------------------------------------

include 'run.inc'
