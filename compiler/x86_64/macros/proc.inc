; compiler/x86_64/macros/proc.inc
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
; proc / endp: SysV-conforming prologue and epilogue, named-argument home
; slots, named locals, and the CF/eax error protocol (docs/asm-conventions.md,
; "1.3 Error protocol: CF / eax") threaded through a single physical exit
; point.
;
; DEVIATION FROM docs/asm-conventions.md's proc.inc section ("3.1
; macros/proc.inc -- procedure declaration")'s "proposed -- not implemented"
; syntax, recorded here per CLAUDE.md evidence discipline ("never present a
; re-derivation as a restoration") -- that
; section's own preamble names it a hypothesis, not verified fasmg source,
; and invites exactly this correction. Two changes, both forced by empirical
; fasmg behavior (see the probes this file's author ran before writing it):
;
;   1. `uses` is a separate statement after `proc`, not inline in the proc
;      header. fasmg macro `match` can capture a keyword-prefixed remainder
;      or split comma-separated arguments, but not both at once without
;      fragile, unverified mid-stream parsing -- and this dialect freezes
;      after this wave, so the robust option won. `uses` also freely mixes
;      registers with argument names in the original proposal's own example
;      in a way a plain comma-separated parameter list cannot disambiguate.
;   2. The original example's success path was `clc` immediately followed by
;      `ret` -- but that `ret` is a bare instruction, not a jump to the
;      epilogue, so it would skip the very `uses`-register pops and stack
;      deallocation the epilogue exists to perform. That is a real bug in
;      unexercised, marked-hypothesis prose, not a restatement of working
;      code. Fixed here: a routine must never `ret` by hand. Success is
;      `return` (new; see below) or plain fall-through into `endp`; failure
;      is `fail CODE`. Both funnel through the one physical epilogue `endp`
;      emits, so callee-saved registers and the frame are always unwound.
;
; `locals` / `endl` is mandatory (write it empty if a routine has no locals):
; it is the single point where this routine's stack frame is sized and
; carved out, and making it unconditional means that finalization point
; never has to be inferred from which optional pieces happen to be present.
;
; Grammar, strict order:
;
;   proc    name, a1, a2, ..., a6        ; 0-6 plain argument names
;           uses    r1, r2, r3, r4       ; OPTIONAL, 0-4 callee-saved regs;
;                                        ; if present, must come before `locals`
;           locals                       ; MANDATORY, even if empty
;                   slot    slotname, dq ; or dd / dw / db, or a literal byte count
;                   slot    othername, dd
;           endl
;           ; ... body ...
;           ; a1..a6 and every `locals` slot are usable as `[name]` from here
;           fail    CODE                 ; sets eax=CODE, CF, jumps to epilogue
;           return                       ; success: clc, jumps to epilogue
;           ; OR just fall through with CF already in the desired state
;   endp
;
; THIRD DEVIATION, same evidence-discipline reasoning as the two above --
; `locals` fields use `slot name, decl` (struct.inc's `field` sugar, not its
; bare form) and NOT the bare struc-style `name dd ?` docs/asm-conventions.md's
; struct section ("3.3 macros/struct.inc") proposed example shows. This is
; not a style preference: it was
; tried, and it corrupts code silently. `struct`/`end struct` can accept a
; bare `name dd ?` because fasmg's native `struc` directive is genuinely
; special-cased by the assembler to capture "label, directive" pairs as
; struct fields. `dd`/`dq`/`dw`/`db` themselves are NOT macros and cannot be
; shadowed the way `assert` can (verified empirically: `macro dd? ...`
; defined immediately before a `name dd ?` line is silently never called --
; 0 bytes, no error, no display output). Outside an actual `struc`/`virtual`
; context -- which `locals` deliberately does not use, see above -- a bare
; `tmp dd ?` is therefore processed as an ordinary native data declaration:
; it silently emits real, live bytes at whatever `$` currently is. Tried
; inside a `locals` block between `uses`'s pushes and `endl`'s `sub rsp`,
; this splices zeroed bytes directly into the middle of the prologue's
; instruction stream -- confirmed by disassembly (a stray `add [rax],al`
; pair where a real instruction should be) -- and reliably segfaults at
; runtime while still assembling and running `make audit` cleanly, because
; nothing about it is an invalid instruction, just a wrong one. `slot`,
; being an ordinary macro invocation (not a directive-look-alike), has no
; such trap door. This is exactly the kind of silent, assembles-clean
; corruption the freeze is supposed to catch before it ships -- so it is
; reported here in the same file it was found in, not smoothed over.
;
; r15 (the r15 pin in docs/asm-conventions.md, "1.2 The r15 pin") is never in
; a `uses` list and this file never touches it -- it is already callee-saved
; under stock SysV and the pin adds a usage rule, not a save/restore
; obligation.
;
; No support for a 7th+ (stack-passed) argument in this wave -- everything
; rt/ needs fits in 6. `[UNIMPLEMENTED]` if that ever changes.
;
; Verified empirically (not assumed) before this file was written:
;   - `name = rbp - K` (a redefinable `=` constant holding a register-
;     relative expression) assembles and, used later as `[name]`, encodes
;     the correct `[rbp-K]` ModRM/disp8 -- confirmed by disassembly.
;   - `=` symbols may be freely REDEFINED later in the same file (each use
;     captures the then-current value) -- confirmed by disassembly of two
;     sequential redefinitions. This is what lets every `proc` reuse plain
;     argument/local names like `ctx` or `len` without collision.
;   - fasmg macros CANNOT call themselves by name from inside their own
;     body (every attempt reproducibly fails with "illegal instruction" at
;     the recursive call site) -- so this file uses fixed-arity positional
;     parameters (up to 6 args, up to 4 `uses` registers) with `match ,x`
;     emptiness checks, never recursion, for anything list-shaped.
;   - a macro CAN be redefined to hold a full statement (e.g. `pop rbx`) and
;     invoked later, bare, as its own statement line -- this is how the
;     `uses` reverse-order pop sequence is generated without ever needing to
;     store a register name as data. Confirmed by disassembly: pushes in
;     given order, pops in exact reverse order.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.3 (SysV AMD64 is the minimum-
; required `externus("C", ...)` ABI; docs/asm-conventions.md's "1. Calling
; convention" section makes it the internal convention too, and this file is
; what implements that decision in fasmg). CLAUDE.md "Determinism is not
; optional" (no construct here depends on a pointer value -- only on
; argument position and assemble-time counters).
; -----------------------------------------------------------------------------

; ---- reserved-name guard ----------------------------------------------
; A `proc` argument or `locals` `slot` name becomes a plain, unmangled,
; GLOBAL `=` symbol (see the empirical notes above) -- colliding with a
; real label used elsewhere fails far from the actual mistake, inside
; whatever unrelated directive next tries to resolve that name, with a
; message that says nothing about a naming collision. Caught for real:
; rt/str.inc's first draft named a `str_slice` argument `start`, which
; silently redefined the global `start` every fixture in this tree uses as
; its ELF entry label (`entry start` / `start:`) -- assembly failed deep
; inside `format ELF64 executable`'s own entry-address resolution, with no
; mention of `str_slice` at all, instead of at the actual declaration.
;
; This list is deliberately SMALL: names actually used as real, external
; labels by this tree's own convention, not every word that could
; theoretically collide with something eventually. `start` is the
; demonstrated case (the universal ELF entry label); `main` is reserved
; alongside it as the other conventional entry-point name, defensively,
; before any module actually uses it. Grow this list only when a real
; collision is found, the same way `start` was -- reserving speculatively
; just trades this file's own false positives for the bug it exists to
; catch.
;
; THIS BREAKS THE FROZEN DIALECT (CLAUDE.md: "The macro dialect is frozen
; after wave 1... a macro change is a whole-tree change") -- authorized
; explicitly, reason recorded here: the cost of this bug class grows with
; every module `proc`/`locals` gets used in, and this is the cheapest
; point at which to close it. Applied whole-tree in the same change: every
; `proc`/`slot` name already written anywhere in this repo was swept
; against this list before this guard was added;
; the one real hit (`str_slice`'s `start`) was renamed in the same pass.
macro __proc_reserved_check? tok
	match =start?, tok
		err 'proc/locals: name "start" is reserved -- it collides with the ELF entry label used throughout this tree (entry start / start:); macros/proc.inc binds argument/local names as unmangled globals, so this would silently redefine start instead of erroring at the real cause -- pick a different name'
	else match =main?, tok
		err 'proc/locals: name "main" is reserved -- a common alternate entry-point label name; macros/proc.inc binds argument/local names as unmangled globals -- pick a different name'
	end match
end macro

; ---- proc ------------------------------------------------------------------
; Up to 6 plain argument names, positional, no gaps. Emits the label, the
; SysV push-rbp/mov-rbp,rsp prologue head, spills every given argument to its
; own home slot immediately (argument registers are untouched by anything
; between here and the spill, so this is always safe), and reserves exactly
; enough 16-byte-aligned stack for them. `uses`/`locals` continue the frame
; from where this leaves off.
macro proc? name, a1:, a2:, a3:, a4:, a5:, a6:
	name:
	push	rbp
	mov	rbp, rsp

	; Reset per-proc state. Must happen before `uses` (if any) runs.
	__proc_uses_count = 0
	macro __proc_pop_1?
	end macro
	macro __proc_pop_2?
	end macro
	macro __proc_pop_3?
	end macro
	macro __proc_pop_4?
	end macro

	__proc_argbytes = 0
	match ,a1
	else
		__proc_reserved_check a1
		__proc_argbytes = 8
		a1 = rbp - 8
		mov	[a1], rdi
	end match
	match ,a2
	else
		__proc_reserved_check a2
		__proc_argbytes = 16
		a2 = rbp - 16
		mov	[a2], rsi
	end match
	match ,a3
	else
		__proc_reserved_check a3
		__proc_argbytes = 24
		a3 = rbp - 24
		mov	[a3], rdx
	end match
	match ,a4
	else
		__proc_reserved_check a4
		__proc_argbytes = 32
		a4 = rbp - 32
		mov	[a4], rcx
	end match
	match ,a5
	else
		__proc_reserved_check a5
		__proc_argbytes = 40
		a5 = rbp - 40
		mov	[a5], r8
	end match
	match ,a6
	else
		__proc_reserved_check a6
		__proc_argbytes = 48
		a6 = rbp - 48
		mov	[a6], r9
	end match

	__proc_args_padded = (__proc_argbytes + 15) and (not 15)
	if __proc_args_padded > 0
		sub	rsp, __proc_args_padded
	end if
	__proc_frame_off = __proc_args_padded
end macro

; ---- uses --------------------------------------------------------------
; Optional. Up to 4 callee-saved registers, comma-separated, pushed in the
; given order. Must appear (if at all) before `locals`/`endl`, which needs
; __proc_uses_count settled to compute correct stack-alignment padding.
macro uses? r1:, r2:, r3:, r4:
	match ,r1
	else
		push	r1
		__proc_uses_count = __proc_uses_count + 1
		macro __proc_pop_1?
			pop	r1
		end macro
	end match
	match ,r2
	else
		push	r2
		__proc_uses_count = __proc_uses_count + 1
		macro __proc_pop_2?
			pop	r2
		end macro
	end match
	match ,r3
	else
		push	r3
		__proc_uses_count = __proc_uses_count + 1
		macro __proc_pop_3?
			pop	r3
		end macro
	end match
	match ,r4
	else
		push	r4
		__proc_uses_count = __proc_uses_count + 1
		macro __proc_pop_4?
			pop	r4
		end macro
	end match
end macro

; ---- locals / endl ----------------------------------------------------
; MANDATORY pair (write `locals` immediately followed by `endl` if a routine
; has no locals). Between them, each line is `slot slotname, decl` where
; decl is one of dq/dd/dw/db, or a literal byte count for anything else (a
; small buffer, for instance) -- see the "THIRD DEVIATION" note above this
; macro pair for why the bare struc-style `slotname decl ?` form is NOT
; accepted here (it assembles and silently corrupts the prologue instead of
; erroring). Each slot becomes a plain `[slotname]`-addressable
; `=` constant, exactly like a `proc` argument. `endl` is the single point
; that sizes and carves out the rest of this routine's frame, padding for
; 16-byte alignment relative to however many `uses` registers were pushed
; (SysV requires rsp aligned immediately before any `call` the body makes).
macro locals?
	; FOURTH BUG CAUGHT BY RUNNING THIS (see the two in `endp?`, below, for
	; the others) -- and the nastiest of the four, because it corrupted data
	; silently rather than crashing or flipping a flag. `__proc_frame_off`
	; was left at `__proc_args_padded` here without ever accounting for the
	; bytes `uses` had already pushed, so the first `slot` landed AT rbp
	; minus (args bytes + its own size) -- overlapping the top of the
	; `uses`-pushed register region rather than starting below it. A local
	; actually written through (not just declared, which is why the
	; `uses`+`locals`+no-op-body fixture this file's own tests exercised
	; first did not catch it) silently clobbered part of a saved
	; callee-saved register on the stack. Caught by a fixture that writes a
	; local AND checks a `uses` register survives the call: it came back as
	; the local's value spliced into the saved register's high 32 bits --
	; confirmed byte-for-byte by dumping the full 8 bytes of the register on
	; the caller side. Fix: advance `__proc_frame_off` past the `uses`
	; region FIRST, so every `slot` this proc declares -- and the value this
	; macro hands `endp?` to reset rsp before popping -- both correctly
	; start below where `uses` actually put its data, not merely below the
	; arguments.
	__proc_frame_off = __proc_frame_off + 8*__proc_uses_count
	__proc_postuses_bytes = __proc_frame_off
	__proc_locals_start = __proc_frame_off
	macro slot? slotname, decl
		__proc_reserved_check slotname
		match =dq?, decl
			__slotsize = 8
		else match =dd?, decl
			__slotsize = 4
		else match =dw?, decl
			__slotsize = 2
		else match =db?, decl
			__slotsize = 1
		else
			__slotsize = decl
		end match
		__proc_frame_off = __proc_frame_off + __slotsize
		slotname = rbp - __proc_frame_off
	end macro
end macro

macro endl?
	purge slot?
	__proc_localbytes = __proc_frame_off - __proc_locals_start
	if (__proc_uses_count and 1) = 0
		__proc_local_padded = (__proc_localbytes + 15) and (not 15)
	else
		__proc_local_padded = ((__proc_localbytes + 7) and (not 15)) + 8
	end if
	if __proc_local_padded > 0
		sub	rsp, __proc_local_padded
	end if
end macro

; ---- fail / return -------------------------------------------------------
; The only two legal ways to leave a proc body (or fall through to `endp`
; with CF already correct). Both reach the same physical epilogue, so a
; `uses` register is always popped and the frame is always unwound. `fail`
; is a forward reference to a label `endp` defines later in the same
; routine -- an ordinary forward jump, nothing exotic.
macro fail? code
	mov	eax, code
	stc
	jmp	.__proc_epilogue
end macro

macro return?
	clc
	jmp	.__proc_epilogue
end macro

; ---- endp ---------------------------------------------------------------
; Emits the one physical epilogue: pop every `uses` register in exact
; reverse order (unused slots expand to nothing), unwind the frame via rbp,
; and return.
;
; TWO BUGS CAUGHT BY RUNNING THIS, not by inspection -- both real, both
; reported here rather than smoothed over (CLAUDE.md evidence discipline):
;
;   1. `endl` does a SECOND `sub rsp` (for locals) after `uses` already
;      pushed registers, so by the time control reaches this epilogue, rsp
;      sits below where those registers actually live -- a bare `pop` here
;      read garbage out of the locals region instead of the saved register
;      values. Confirmed by disassembly plus a runtime fixture with a known
;      caller-side sentinel in a `uses` register, which came back corrupted
;      before this fix. Fix: land rsp exactly on top of the last `uses`
;      push -- `__proc_postuses_bytes`, captured by `locals?` -- before
;      popping anything.
;   2. The first fix's own first draft used `mov rsp, rbp` then
;      `sub rsp, __proc_postuses_bytes` -- and `sub` sets CF from the
;      subtraction, silently overwriting the CF that `fail`/`return` had
;      just set as the actual return-value protocol one or two instructions
;      earlier. Confirmed by a runtime fixture: a `fail 99` path that
;      correctly computed eax=99 and CF=1 came back with CF clear at the
;      caller. `lea` computes the same address and touches no flags at all,
;      so it replaces the `mov`+`sub` pair below with a single
;      flags-transparent instruction. Every other instruction in this
;      epilogue (`pop`, the final `mov rsp,rbp`, `ret`) is already
;      flags-safe; `sub` was the one exception, now gone.
macro endp?
.__proc_epilogue:
	lea	rsp, [rbp - __proc_postuses_bytes]
	__proc_pop_4
	__proc_pop_3
	__proc_pop_2
	__proc_pop_1
	mov	rsp, rbp
	pop	rbp
	ret
end macro
