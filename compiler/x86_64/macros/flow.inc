; compiler/x86_64/macros/flow.inc
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
; Structured control flow: `flow_if`/`flow_elseif`/`flow_else`/`flow_endif`,
; `flow_while`/`flow_endw`, `flow_for`/`flow_endf`,
; `flow_switch`/`flow_case`/`flow_default`/`flow_endsw`. Each expands to
; ordinary `cmp`/`jcc` sequences over `local`-generated labels, so nested or
; repeated use never collides.
;
; TWO DEVIATIONS from docs/asm-conventions.md's flow section ("3.2
; macros/flow.inc -- structured control flow")'s "proposed -- not
; implemented" syntax, same evidence-discipline reasoning as macros/proc.inc
; records for itself -- that section's own preamble names it a hypothesis,
; not verified fasmg source. Both were found by running this, not by
; inspection; see this wave's report for the fuller account.
;
; 1. NO LEADING DOT. The proposal spelled these `.if`/`.elseif`/etc. This
;    does not work -- not "is risky", DOES NOT WORK -- once a construct is
;    used where every one of these constructs must be usable: inside a real
;    code segment, after `format`/`segment` have run. Verified by a minimal,
;    reproducible pair: a macro named `foo?` invoked bare as `foo` inside
;    `segment readable executable` assembles cleanly; the SAME macro,
;    renamed `.foo?` and invoked as `.foo`, fails with "illegal instruction"
;    at that exact statement -- with no other change. Outside a code segment
;    (plain top-level preprocessing, no `format`/`segment` yet run), a
;    dot-prefixed macro invokes fine; the failure appears specifically once
;    real code emission is live. Reading between fasmg's own error ("illegal
;    instruction", the same message a genuinely unrecognized mnemonic
;    produces): inside a segment, a token starting with `.` is claimed by
;    the core statement parser as a local-label reference before macro
;    lookup is ever consulted, not routed through it the way a plain
;    identifier is. `?`-suffixing the macro name (the usual safe-redefine
;    convention -- see macros/struct.inc, macros/assert.inc) does not change
;    this; the dot is claimed at the call site, not the definition site.
;    This is a real, load-bearing limit on the dialect, discovered exactly
;    because this wave tried to exercise the proposal before freezing it.
;    Named `flow_if` etc. instead -- plain identifiers, prefixed with this
;    file's module name per docs/asm-conventions.md's global-label-naming
;    rule, since `if`/`while`/`for` bare would shadow fasmg's own native
;    assemble-time directives (exactly what macros/assert.inc's header
;    explains `rassert` avoids doing to the native `assert`) and every
;    macro here needs `if`/`else if` internally to pick a nesting level.
; 2. Condition operators are WORDS (`eq ne lt le gt ge`), not the symbols
;    (`= <> < > <= >=`) the proposal showed. Tried the symbols first. `<`,
;    `>` and `<>` work fine as literal `match` pattern tokens; `=`, `<=` and
;    `>=` do not -- empirically, any pattern token containing a bare `=`
;    character collides with `match`'s own `=identifier?` literal-match
;    sigil and fails to resolve, or resolves to the wrong branch,
;    inconsistently across attempts (several spellings tried). Mixing
;    symbols for three operators and words for the other three is worse
;    than picking one convention, so all six are words. Comparisons are
;    **signed** (`jl`/`jle`/`jg`/`jge`); no unsigned variant exists yet --
;    `[UNIMPLEMENTED]` if rt/ code ever needs one (nothing in this wave
;    does).
;
; Nesting mechanism, verified empirically before this file was written:
;   - fasmg macros cannot call themselves by name (macros/proc.inc's header
;     records the same finding) -- so nesting is NOT recursive descent.
;   - A `local`-generated label's mangled name CAN be embedded, by ordinary
;     text substitution, into the body of a second macro defined alongside
;     it -- this is how one construct's `flow_elseif`/`flow_endif`/etc.
;     (separate, later top-level statements) find labels a `local` inside
;     `flow_if` created. Proven in macros/proc.inc for the `uses`
;     register-pop sequence; reused here for jump targets instead of
;     registers. NOTE: `local` rejects a dot-prefixed name outright
;     ("invalid argument") -- unrelated to deviation 1 above (that failure
;     is immediate, at the `local` statement itself, in any context, not
;     specific to code segments) -- so the generated labels themselves
;     (`Lpend`, `Lend`, `Ltop`, `Lbody`, `Lskip`) are plain identifiers, not
;     dot-locals. `local` already guarantees global uniqueness per
;     expansion, which is all these need; they simply do not use the
;     `.name`-scoped-to-enclosing-routine convention docs/asm-conventions.md
;     reserves for hand-written labels.
;   - A **fixed-name** helper macro redefined by a nested construct is GONE
;     for the outer construct once the inner one closes -- macros cannot
;     save and later restore "the previous definition" of another macro,
;     because invoking a saved name only ever resolves to whatever that name
;     CURRENTLY means, not what it meant when saved. Confirmed empirically
;     (reversed-order invocation test: a "level 2" body fired before a
;     "level 1" body defined earlier, proving definitions bind at call time,
;     never at definition time -- exactly the property that breaks a
;     save/restore-by-alias scheme). So per-level state needs a genuinely
;     separate name per level, not a shared name naively saved and restored.
;   - Pasting a **bare `=`-variable reference** into a name via `#` does not
;     use its current value -- `name#somevar` pastes the literal token
;     `somevar`, not the number it holds, even when `somevar` is routed
;     through a macro parameter first. Only a parameter bound to an already-
;     concrete token (a literal digit, or another parameter that is one)
;     pastes correctly. Confirmed empirically (multiple attempts).
;   - Combining the two previous findings: this file supports up to 6 levels
;     of nested `flow_if`/`flow_while`/`flow_for`/`flow_switch` (shared
;     counter and shared per-level slots across all four -- only one
;     construct is ever innermost at a given depth) via `__flow_depth` (an
;     assemble-time counter) plus an explicit 6-way `if`/`else if` chain,
;     written once, that turns "the current value of `__flow_depth`" into a
;     literal digit handed to a second macro -- which CAN paste it, because
;     by then it is a literal, not a variable reference.
;     `[UNIMPLEMENTED]` past depth 6; `err`s loudly rather than silently
;     reusing a level (see `__flow_push?`).
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3 ("Determinism is not optional",
; per CLAUDE.md) -- nesting depth is an assemble-time counter, never a
; pointer value, so construct expansion order cannot vary run to run.
; -----------------------------------------------------------------------------

__flow_depth = 0

; ---- per-level dispatch primitives (private to this file) -----------------
;
; __flow_def   role, body    -- define role at the CURRENT level: invoking
;                                `role` later (via __flow_call) runs `body`.
; __flow_call  role           -- run whatever `role` currently means at the
;                                CURRENT level.
; __flow_defp  role, param, body  -- same as __flow_def, but the stored
;                                macro takes one parameter (named `param`
;                                inside `body`).
; __flow_callp role, arg      -- invoke a `__flow_defp`-defined role with one
;                                argument.
;
; All four turn "the current value of __flow_depth" into a literal digit via
; the 6-way chain below, then paste role#N via a second macro -- see the
; header comment above for why this two-step indirection is required.
macro __flow_push?
	__flow_depth = __flow_depth + 1
	if __flow_depth > 6
		err 'flow_if/flow_while/flow_for/flow_switch: nesting deeper than 6 levels is not supported'
	end if
end macro

macro __flow_pop?
	__flow_depth = __flow_depth - 1
end macro

macro __flow_def? role, body&
	if __flow_depth = 1
		__flow_def_n role, 1, body
	else if __flow_depth = 2
		__flow_def_n role, 2, body
	else if __flow_depth = 3
		__flow_def_n role, 3, body
	else if __flow_depth = 4
		__flow_def_n role, 4, body
	else if __flow_depth = 5
		__flow_def_n role, 5, body
	else if __flow_depth = 6
		__flow_def_n role, 6, body
	end if
end macro

macro __flow_def_n? role, n, body&
	macro role#n?
		body
	end macro
end macro

macro __flow_call? role
	if __flow_depth = 1
		__flow_call_n role, 1
	else if __flow_depth = 2
		__flow_call_n role, 2
	else if __flow_depth = 3
		__flow_call_n role, 3
	else if __flow_depth = 4
		__flow_call_n role, 4
	else if __flow_depth = 5
		__flow_call_n role, 5
	else if __flow_depth = 6
		__flow_call_n role, 6
	end if
end macro

macro __flow_call_n? role, n
	role#n
end macro

macro __flow_defp? role, param, body&
	if __flow_depth = 1
		__flow_defp_n role, 1, param, body
	else if __flow_depth = 2
		__flow_defp_n role, 2, param, body
	else if __flow_depth = 3
		__flow_defp_n role, 3, param, body
	else if __flow_depth = 4
		__flow_defp_n role, 4, param, body
	else if __flow_depth = 5
		__flow_defp_n role, 5, param, body
	else if __flow_depth = 6
		__flow_defp_n role, 6, param, body
	end if
end macro

macro __flow_defp_n? role, n, param, body&
	macro role#n? param
		body
	end macro
end macro

macro __flow_callp? role, arg
	if __flow_depth = 1
		__flow_callp_n role, 1, arg
	else if __flow_depth = 2
		__flow_callp_n role, 2, arg
	else if __flow_depth = 3
		__flow_callp_n role, 3, arg
	else if __flow_depth = 4
		__flow_callp_n role, 4, arg
	else if __flow_depth = 5
		__flow_callp_n role, 5, arg
	else if __flow_depth = 6
		__flow_callp_n role, 6, arg
	end if
end macro

macro __flow_callp_n? role, n, arg
	role#n arg
end macro

; `__flow_arm_endjmp` -- private to flow_switch/flow_case. A near-copy of
; __flow_def's own dispatch, but for exactly one job: point THIS level's
; `endjmp` role at THIS level's `jumpend` role (see the "TWO BUGS" comment
; on flow_switch, below, for why flow_case needs this instead of the more
; obvious `__flow_def endjmp, __flow_call jumpend`). Both names get the same
; literal digit pasted, so the stored `endjmp#n?` invokes `jumpend#n`
; DIRECTLY -- never `__flow_call` again. That distinction matters: a role
; invoked via `__flow_call` that itself calls `__flow_call` (even for a
; different role name) is `__flow_call` re-entering itself through the
; stored body, and fails exactly like the direct self-recursion
; macros/proc.inc's header documents -- confirmed empirically here before
; this helper existed (see flow_switch's own header comment).
macro __flow_arm_endjmp?
	if __flow_depth = 1
		__flow_arm_endjmp_n 1
	else if __flow_depth = 2
		__flow_arm_endjmp_n 2
	else if __flow_depth = 3
		__flow_arm_endjmp_n 3
	else if __flow_depth = 4
		__flow_arm_endjmp_n 4
	else if __flow_depth = 5
		__flow_arm_endjmp_n 5
	else if __flow_depth = 6
		__flow_arm_endjmp_n 6
	end if
end macro

macro __flow_arm_endjmp_n? n
	macro endjmp#n?
		jumpend#n
	end macro
end macro

; ---- condition parser (private to this file) -------------------------------
; `target` first (not last), so the greedy `cond&` can only be the final
; parameter. `cond` is exactly 3 tokens: `lhs op rhs`. Emits `cmp lhs, rhs`
; then the INVERTED jcc -- i.e. the jump taken when the condition is FALSE,
; which is what every construct below needs (skip the body / exit the loop).
macro __flow_jif? target, cond&
	match lhs op rhs, cond
		cmp	lhs, rhs
		match =eq?, op
			jne	target
		else match =ne?, op
			je	target
		else match =lt?, op
			jge	target
		else match =le?, op
			jg	target
		else match =gt?, op
			jle	target
		else match =ge?, op
			jl	target
		else
			err 'flow: condition operator must be one of eq ne lt le gt ge'
		end match
	else
		err 'flow: condition must be exactly "lhs op rhs" (3 tokens)'
	end match
end macro

; ---- flow_if / flow_elseif / flow_else / flow_endif -------------------------
macro flow_if? cond&
	__flow_push
	local Lpend
	local Lend
	__flow_jif Lpend, cond
	__flow_def pend, Lpend:
	__flow_def endjmp, jmp Lend
	__flow_def enddef, Lend:
end macro

macro flow_elseif? cond&
	__flow_call endjmp
	__flow_call pend
	local Lpend
	__flow_jif Lpend, cond
	__flow_def pend, Lpend:
end macro

macro flow_else?
	__flow_call endjmp
	__flow_call pend
	__flow_def pend
end macro

macro flow_endif?
	__flow_call endjmp
	__flow_call pend
	__flow_call enddef
	__flow_pop
end macro

; ---- flow_while / flow_endw --------------------------------------------------
macro flow_while? cond&
	__flow_push
	local Ltop
	local Lend
  Ltop:
	__flow_jif Lend, cond
	__flow_def endjmp, jmp Ltop
	__flow_def enddef, Lend:
end macro

macro flow_endw?
	__flow_call endjmp
	__flow_call enddef
	__flow_pop
end macro

; ---- flow_for / flow_endf -----------------------------------------------------
; `flow_for reg, start, endval` -- `reg := start`; while `reg lt endval`:
; body; `reg := reg + 1`. Signed compare, `inc`-by-1 only (no custom step in
; this wave -- `[UNIMPLEMENTED]`, nothing in rt/ needs one).
macro flow_for? reg, start, endval
	__flow_push
	mov	reg, start
	local Ltop
	local Lend
  Ltop:
	cmp	reg, endval
	jge	Lend
	__flow_def forinc, inc reg
	__flow_def endjmp, jmp Ltop
	__flow_def enddef, Lend:
end macro

macro flow_endf?
	__flow_call forinc
	__flow_call endjmp
	__flow_call enddef
	__flow_pop
end macro

; ---- flow_switch / flow_case / flow_default / flow_endsw ----------------------
; Sequential cmp/je chain, not a jump table -- simplicity over speed, and
; this compiler's `flow_switch` uses (diagnostic codes, small enumerations)
; are not hot paths. Up to 4 values per `flow_case` (same
; fixed-arity-over-recursion reasoning as macros/proc.inc's `uses`/argument
; lists -- fasmg macros cannot recurse, see that file's header).
; THREE BUGS CAUGHT BY RUNNING THIS, not by inspection:
;
;   1. `flow_switch` must NOT define `endjmp` as a real "jmp Lend" up front.
;      `flow_case`/`flow_default` treat `endjmp`/`pend` as "close whatever
;      the PREVIOUS case left open", exactly like `flow_elseif` does for
;      `flow_if` -- but unlike `flow_if` (which itself opens the first real
;      conditional branch), `flow_switch` opens nothing; there is no
;      "previous case" before the first one. Defining `endjmp` as a live
;      `jmp Lend` here meant the very first `flow_case` unconditionally
;      jumped to the end of the switch before comparing anything, skipping
;      every case body including a matching one -- confirmed by a fixture
;      that dumped the selected branch's value directly and observed
;      neither the matching case's result nor `flow_default`'s. Fixed by
;      leaving `endjmp`/`pend` genuinely empty until the first case has run
;      once.
;   2. The first fix's own first draft moved `__flow_def endjmp, jmp Lend`
;      into `flow_case` (redefined there, for the NEXT case to use) --
;      which then failed differently: "symbol 'Lend' is undefined". `local`
;      substitutes a generated name for the REST OF THE SAME macro
;      expansion that declared it, not globally -- `Lend`, `local`-declared
;      inside `flow_switch`, is a live substitution only there. Writing the
;      bare word `Lend` inside `flow_case`'s body (a different, later
;      expansion) is just the ordinary identifier "Lend", never connected
;      to `flow_switch`'s generated label. `flow_if`/`flow_elseif` never hit
;      this because `flow_if` stores `jmp Lend` itself, in its own scope,
;      exactly once (see that macro, above); `flow_switch`'s per-case
;      re-arming has no equivalent single scope-correct place to do it more
;      than once. Fixed with one more level of indirection: `flow_switch`
;      defines `jumpend` (scope-correct, `jmp Lend` written where `Lend` is
;      valid) once; `flow_case` closes a previous case by re-arming
;      `endjmp` to invoke `jumpend` -- a call by NAME, carrying no
;      dependency on where the text `Lend` was lexically written.
;   3. That fix's own first draft re-armed `endjmp` with
;      `__flow_def endjmp, __flow_call jumpend` -- reasonable-looking, and
;      wrong the same way direct self-recursion is wrong: by the time
;      `endjmp#n?` (stored body: `__flow_call jumpend`) runs, it is being
;      invoked FROM INSIDE `__flow_call`'s own expansion (`flow_case` calls
;      `__flow_call endjmp`, which dispatches to `endjmp#n`, whose body then
;      calls `__flow_call` again) -- `__flow_call` re-entering itself
;      through a stored role, not by literally writing its own name twice,
;      but the same failure macros/proc.inc's header records for direct
;      recursion ("illegal instruction" at the inner call). `__flow_arm_
;      endjmp` (below the shared dispatch primitives, above) exists
;      entirely to avoid this: it pastes the SAME literal depth digit onto
;      both `endjmp` and `jumpend`, so the stored body is the bare
;      statement `jumpend#n` -- a normal macro invocation, not another
;      `__flow_call`.
macro flow_switch? reg
	__flow_push
	local Lend
	__flow_defp switchcmp, val, cmp reg, val
	__flow_def enddef, Lend:
	__flow_def jumpend, jmp Lend
	__flow_def endjmp
	__flow_def pend
end macro

macro flow_case? v1, v2:, v3:, v4:
	__flow_call endjmp
	__flow_call pend
	local Lbody
	local Lskip
	__flow_callp switchcmp, v1
	je	Lbody
	match ,v2
	else
		__flow_callp switchcmp, v2
		je	Lbody
	end match
	match ,v3
	else
		__flow_callp switchcmp, v3
		je	Lbody
	end match
	match ,v4
	else
		__flow_callp switchcmp, v4
		je	Lbody
	end match
	jmp	Lskip
  Lbody:
	__flow_arm_endjmp
	__flow_def pend, Lskip:
end macro

macro flow_default?
	__flow_call endjmp
	__flow_call pend
	__flow_def pend
end macro

macro flow_endsw?
	__flow_call endjmp
	__flow_call pend
	__flow_call enddef
	__flow_pop
end macro
