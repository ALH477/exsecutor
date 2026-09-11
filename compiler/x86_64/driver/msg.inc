; compiler/x86_64/driver/msg.inc
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
; The driver's own voice: exit statuses, the usage text, and the two routines
; every non-diagnostic message in `exsc` goes through.
;
; WHY THIS IS NOT diag/. §13 registers `EXS-E` codes for THE PROGRAM BEING
; COMPILED. Spec §9.3's `--env`/`--epoch` subsection says so in as many words
; for the case that provoked this file: *"Neither is an `EXS-E` diagnostic ...
; a malformed flag is a usage error about the invocation, not about any
; source. `exsc` exits nonzero with a message and no code."* So a bad flag
; gets prose and a status, never a code -- and this file is deliberately
; incapable of emitting a code, because it never touches `Diag`.
;
; THE ONE THING IT DOES BORROW FROM diag/ IS THE ESCAPER, and that is not
; convenience. Every byte in these messages that came from outside the
; compiler -- an argv fragment, a path, a `--env` key -- goes through
; `diag_out_esc`. §8.3: *"All source echoed in a diagnostic is escaped. A
; diagnostic rendering raw bidi makes the error message the attack surface."*
; A usage message is not a diagnostic, but `exsc --hospes <U+202E...>` prints
; attacker bytes to a terminal for exactly the same reason, so it gets exactly
; the same treatment, through exactly the same routine. There is no second
; escaper here and there must never be one.
;
; EXIT STATUSES ARE A PUBLIC INTERFACE. tests/conformance/ (spec §14) has to
; tell "this source is clean" from "this source was rejected" from "the
; compiler broke" mechanically, and §14's entries are all of the form "must
; fail to compile". The five below are what this driver promises; they are
; documented again in the usage text so `exsc` with no arguments answers the
; question without a manual.
;
;   0  DRV_EXIT_OK      the requested work completed, nothing to report
;   1  DRV_EXIT_DIAG    at least one diagnostic was emitted
;   2  DRV_EXIT_USAGE   malformed invocation -- NOT an EXS-E code (§9.3)
;   3  DRV_EXIT_HOST    a syscall failed, or output could not be rendered
;   4  DRV_EXIT_TODO    the requested work is not implemented
;
; 132 is not in that list and is not this file's to give: `rassert` traps with
; `ud2`, the kernel raises SIGILL, and the shell reports 128+4. An internal
; contract violation is a compiler bug and must stay visibly distinct from
; DRV_EXIT_HOST, which is an ordinary environment failure (docs/asm-
; conventions.md, "1.3 Error protocol: CF / eax", the four failure channels).
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3 (escaping), §9.3 (usage errors
; carry no code), §12 (the invocation).
; -----------------------------------------------------------------------------

; Macro-only files, safe to re-process (proven by the existing tree: every
; rt/ and diag/ chain pulls both in, and fasmg redefines a macro rather than
; erroring). macros/struct.inc is deliberately NOT re-included -- it is
; reached exactly once today, through rt/arena.inc, and this file is not the
; place to find out whether a second pass is safe. driver/driver.inc's header
; records the resulting ordering obligation on the consumer.
include '../macros/proc.inc'
include '../macros/assert.inc'

DRV_EXIT_OK    = 0
DRV_EXIT_DIAG  = 1
DRV_EXIT_USAGE = 2
DRV_EXIT_HOST  = 3
DRV_EXIT_TODO  = 4

DRV_FD_ERR = 2			; diagnostics and messages, never stdout --
				; stdout stays free for an artifact
DRV_FD_OUT = 1			; driver/dump.inc's `--emitte` dump, and
				; nothing else -- see drv_say_out, below
DRV_MSG_CAP = 8192		; one message, escaped; DiagOut truncates
				; rather than overruns if a path is longer

; `slot wr, 32` is diag/render.inc's own spelling for a DiagOut frame. Pinned
; against the struct rather than trusted, with fasmg's NATIVE assemble-time
; `assert` (not macros/assert.inc's runtime `rassert`), so a future field
; added to DiagOut fails the build here instead of corrupting a stack frame.
assert sizeof.DiagOut = 32

; ---- literal fragments -----------------------------------------------------
; NUL-terminated, so a call site names a message in one token and this file
; needs no parallel length table to keep in sync. Placed between two `proc`
; bodies, exactly as diag/render.inc places its own: every `proc` leaves
; through `endp`'s `ret`, so control never falls into them.
drv_s_prefix   db 'exsc: '
DRV_S_PREFIX_LEN = $ - drv_s_prefix
drv_s_nl       db 10
DRV_S_NL_LEN     = $ - drv_s_nl

; drv_cstrlen(ptr) -> rax = bytes before the first NUL. Used for this file's
; own literals and for argv, which the kernel hands over NUL-terminated.
; Cannot fail; leaves CF clear.
proc drv_cstrlen, ptr
	locals
	endl
	mov	rdx, [ptr]
	xor	rax, rax
  .loop:
	cmp	byte [rdx + rax], 0
	je	.done
	inc	rax
	jmp	.loop
  .done:
	return
endp

; drv_say(ptr, len) -- write `len` bytes to stderr, looping over short
; writes. A failed write to stderr is swallowed: there is nowhere left to
; report it, and turning it into an exit status would let a closed fd 2
; change what `exsc` returns. A zero-byte return also stops the loop rather
; than spinning. Cannot fail; leaves CF clear.
proc drv_say, ptr, len
	uses	rbx, r12
	locals
	endl
	mov	rbx, [ptr]
	mov	r12, [len]
  .loop:
	test	r12, r12
	jz	.done
	mov	rdi, DRV_FD_ERR
	mov	rsi, rbx
	mov	rdx, r12
	call	sys_write
	jc	.done
	test	rax, rax
	jz	.done
	add	rbx, rax
	sub	r12, rax
	jmp	.loop
  .done:
	return
endp

; drv_say_out(ptr, len) -- the same as `drv_say`, above, but to STDOUT
; (`DRV_FD_OUT`, fd 1) rather than stderr. `driver/dump.inc`'s `--emitte`
; dump is the only caller and the only thing this driver ever writes to fd 1:
; every diagnostic and every other message stays on fd 2 through `drv_say`,
; so a dump and the diagnostics that came with it are separable by
; redirection alone. Same short-write loop, same swallow-on-failure
; reasoning as `drv_say`. Cannot fail; leaves CF clear.
proc drv_say_out, ptr, len
	uses	rbx, r12
	locals
	endl
	mov	rbx, [ptr]
	mov	r12, [len]
  .loop:
	test	r12, r12
	jz	.done
	mov	rdi, DRV_FD_OUT
	mov	rsi, rbx
	mov	rdx, r12
	call	sys_write
	jc	.done
	test	rax, rax
	jz	.done
	add	rbx, rax
	sub	r12, rax
	jmp	.loop
  .done:
	return
endp

; __drv_msg(lit1, argp, argn, lit2, value, wantnum) -- the single place a
; driver message is built. Emits
;
;	exsc: <lit1><escaped argp[0..argn]><lit2><value if wantnum>\n
;
; `argn` 0 skips the middle, `lit2` 0 skips the tail, `wantnum` 0 skips the
; number. `lit1`/`lit2` are this file's own NUL-terminated literals and are
; NOT escaped -- they are compiler-authored ASCII. `argp`/`argn` is the only
; untrusted part and is the only part that is escaped, which is the property
; a reviewer can check by reading this one routine.
;
; Cannot fail; leaves CF clear. Truncation is silent and bounded: DiagOut
; refuses an append that would overrun and sets its own sticky flag, so a
; 4 KiB path escaped six-fold costs a short message, not a smashed stack.
proc __drv_msg, lit1, argp, argn, lit2, value, wantnum
	uses	rbx, r12
	locals
		slot wr,     32
		slot drvbuf, DRV_MSG_CAP
	endl
	lea	rdi, [wr]
	lea	rsi, [drvbuf]
	mov	rdx, DRV_MSG_CAP
	call	diag_out_init

	lea	rdi, [wr]
	lea	rsi, [drv_s_prefix]
	mov	rdx, DRV_S_PREFIX_LEN
	call	diag_out_bytes

	mov	rdi, [lit1]
	call	drv_cstrlen
	mov	rbx, rax
	lea	rdi, [wr]
	mov	rsi, [lit1]
	mov	rdx, rbx
	call	diag_out_bytes

	cmp	qword [argn], 0
	je	.no_arg
	lea	rdi, [wr]
	mov	rsi, [argp]
	mov	rdx, [argn]
	call	diag_out_esc		; THE escaping path -- see this file's header
  .no_arg:

	cmp	qword [lit2], 0
	je	.no_tail
	mov	rdi, [lit2]
	call	drv_cstrlen
	mov	rbx, rax
	lea	rdi, [wr]
	mov	rsi, [lit2]
	mov	rdx, rbx
	call	diag_out_bytes
  .no_tail:

	cmp	qword [wantnum], 0
	je	.no_num
	lea	rdi, [wr]
	mov	esi, [value]
	call	diag_out_u32
  .no_num:

	lea	rdi, [wr]
	lea	rsi, [drv_s_nl]
	mov	rdx, DRV_S_NL_LEN
	call	diag_out_bytes

	lea	rdi, [drvbuf]
	mov	rsi, [wr + DiagOut.len]
	call	drv_say
	return
endp

; drv_msg(lit1, argp, argn, lit2) -- "exsc: <lit1><esc(arg)><lit2>\n".
proc drv_msg, lit1, argp, argn, lit2
	locals
	endl
	mov	rdi, [lit1]
	mov	rsi, [argp]
	mov	rdx, [argn]
	mov	rcx, [lit2]
	xor	r8, r8
	xor	r9, r9
	call	__drv_msg
	return
endp

; drv_msgu(lit1, argp, argn, lit2, value) -- the same, with `value` appended
; as unsigned decimal. `value` is a u32: its callers are an errno and a
; count, both of which fit, and diag_out_u32 takes a dword.
proc drv_msgu, lit1, argp, argn, lit2, value
	locals
	endl
	mov	rdi, [lit1]
	mov	rsi, [argp]
	mov	rdx, [argn]
	mov	rcx, [lit2]
	mov	r8d, [value]
	mov	r9, 1
	call	__drv_msg
	return
endp

; ---- the usage text --------------------------------------------------------
; Written raw, without the `exsc: ` prefix and without escaping, because
; every byte of it is in this file. It answers, in order: what the invocation
; is (§12), which subcommands exist and which one works, what each option
; means and which spec rule forces it, and what each exit status means.
drv_s_usage:
	db 'usage: exsc SUBCOMMAND [OPTION]... SOURCE...', 10
	db 10
	db '  exsc aedifica --hospes TRIPLE SOURCE... -o OUT     (spec 12)', 10
	db 10
	db 'subcommands (spec 12):', 10
	db '  aedifica proba forma lsp ego emenda documenta novum lexicon curre', 10
	db '  Only `aedifica` does anything. Every other one exits 4 and says so.', 10
	db '  `aedifica` runs the whole pipeline: spec 8.1 gate, tokenizer, parser,', 10
	db '  typed AST, checker, and -- given -o -- the lowering and the reference', 10
	db '  backend (spec 16 Stage 3).', 10
	db 10
	db 'options, all of them accepted only by `aedifica`:', 10
	db '  --hospes TRIPLE      target triple. REQUIRED: spec 9.5 makes an absent', 10
	db '                       --hospes an error. There is no default-to-build-platform.', 10
	db '                       This build only targets x86_64-linux (exit 4 on any', 10
	db '                       other triple): the reference backend is x86-64 fasmg', 10
	db '                       text and there is no other backend to pick.', 10
	db '  SOURCE               the file to compile. Positional, and REPEATABLE:', 10
	db '                       spec 12 says the files named form one compilation', 10
	db '                       unit -- one module, one artifact -- and that their', 10
	db '                       order on the command line is the order of their', 10
	db '                       items, so it is part of the input and reordering', 10
	db '                       them is a different build. The same path twice is', 10
	db '                       an error; at most 256 files.', 10
	db '  -o OUT               the artifact to write, and it is TEXT: fasmg source', 10
	db '                       for the reference backend (spec 12). exsc never', 10
	db '                       assembles it -- run `fasmg OUT BIN` yourself, with', 10
	db '                       vendor/fasmg-x86 on the INCLUDE path. Optional: with', 10
	db '                       it a clean unit is compiled and written (exit 0);', 10
	db '                       without it the front end and the checker run alone', 10
	db '                       and no artifact is asked for or produced.', 10
	db '  --env KEY=VALUE      supply a value (spec 9.3). Repeatable. A repeated KEY', 10
	db '                       is an ERROR, not a last-wins, so --env order is not', 10
	db '                       observable. exsc never reads the environment.', 10
	db '  --epoch N            decimal seconds since the Unix epoch. Optional,', 10
	db '                       defaults to 0. There is no implicit clock (spec 9.3).', 10
	db '  --diagnostica FORMA  textus (default) or json. json emits one JSON object', 10
	db '                       per line on stderr; match on its "code" member, never', 10
	db '                       on its English text (spec 8.3).', 10
	db '  --emitte GRADUS      dump a pipeline stage to stdout and nothing else on', 10
	db '                       stdout (spec 18.2): tokens, cst, or ast. Diagnostics', 10
	db '                       still go to stderr, so `exsc ... --emitte ast SRC', 10
	db '                       >tree.txt` puts only the tree in tree.txt. `ast` dumps', 10
	db '                       after the checker runs, so its `d` slots show what', 10
	db '                       the checker resolved (0 where it found nothing).', 10
	db 10
	db 'exit status:', 10
	db '  0  the requested work completed and there was nothing to report', 10
	db '  1  diagnostics were emitted', 10
	db '  2  usage error -- a malformed invocation, which carries no EXS-E code', 10
	db '  3  a host or internal failure (a syscall failed, or output was truncated)', 10
	db '  4  the requested work is not implemented', 10
	db '  132  an internal contract violation trapped (SIGILL). That is a bug.', 10
drv_s_usage_end:
DRV_S_USAGE_LEN = drv_s_usage_end - drv_s_usage

; drv_usage -- the block above, to stderr. Cannot fail; leaves CF clear.
proc drv_usage
	locals
	endl
	lea	rdi, [drv_s_usage]
	mov	rsi, DRV_S_USAGE_LEN
	call	drv_say
	return
endp
