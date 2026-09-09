; compiler/shared/unicode/xid.inc
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
; UAX #31 identifier classification over the generated `xid` trie:
; `XID_Start` / `XID_Continue`, plus `_` (U+005F) as §8.2 requires.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.2 (identifiers are UAX #31
; XID_Start/XID_Continue plus `_`).
; -----------------------------------------------------------------------------
;
; HOW TO USE THIS FILE (and the two beside it, script.inc and nfc.inc)
; -------------------------------------------------------------------
; The raw tables live in `tables/tables.inc` and are NOT included from here.
; A consumer includes them EXACTLY ONCE, itself, inside whatever data segment
; it wants them in:
;
;	include '<path>/compiler/shared/unicode/xid.inc'	; code
;	...
;	segment readable					; data
;	  include '<path>/compiler/shared/unicode/tables/tables.inc'
;
; Two reasons, both empirical:
;
;   1. `tables/tables.inc` defines labels and emits ~120 KB of `file` data.
;      Processing it twice is fatal ("symbol already defined"), so it cannot
;      be included from each of xid.inc/script.inc/nfc.inc -- a consumer
;      wanting two of the three would double-include it. The repository-wide
;      rule this is an instance of is in `compiler/x86_64/rt/str.inc`'s
;      header: includes must form a strictly linear chain, never a diamond.
;   2. Only the consumer knows which segment the blobs belong in. They are
;      read-only, so `segment readable` (no `writeable`, no `executable`) is
;      right; putting the `file` directives here would force them into
;      whatever segment happened to be open at the include site, which for a
;      file full of `proc`s is the executable one.
;
; Forward references are fine: fasmg resolves `[uni_xid_stage1 + rax*2]` to a
; 32-bit absolute displacement across passes whether the label is defined
; before or after this file. VERIFIED by running -- tests/unit/xid_props.asm
; includes the tables AFTER this file and passes.
;
; The macro files (`../../x86_64/macros/*.inc`) are the exception to the
; linearity rule and are included directly below. VERIFIED by running: they
; define only macros, and fasmg permits macro redefinition silently, so
; including `proc.inc`, `assert.inc` or `flow.inc` twice via two paths
; assembles and runs correctly. (Probed directly: a file that includes all
; three twice, then uses `proc`/`rassert`/`flow_if`, assembles in 3 passes
; and exits 0.) That is what makes it safe for xid.inc, script.inc and
; nfc.inc each to declare their own macro dependencies rather than chaining
; through one another in a fixed order.
;
; ADDRESSING MODE, recorded because every table lookup in these three files
; depends on it and the previous attempt at this wave died before writing the
; answer down: `[bare_label + reg*scale]` DOES work, for both `file` (incbin)
; data and ordinary declared data, with scale 1/2/4/8. These are non-PIE
; ELF64 executables (`format ELF64 executable 3`, base 0x8048000), so the
; label becomes a 32-bit absolute displacement and the encoding is a plain
; disp32+index ModRM/SIB. Probed directly before this file was written, and
; exercised by every fixture beside it. No `lea`-then-index dance is needed.
; -----------------------------------------------------------------------------

include '../../x86_64/macros/proc.inc'
include '../../x86_64/macros/assert.inc'

; U+005F LOW LINE. §8.2's "plus `_`".
;
; MEASURED, not assumed: in UCD 17.0.0 the `xid` trie gives U+005F the value
; 2 -- XID_Continue set, XID_Start clear. So `_` genuinely has to be added to
; the Start side; on the Continue side the special case below is redundant
; today. It is written anyway, and deliberately: §8.2 states the rule as
; "XID_Start/XID_Continue plus `_`", and a rule stated in two places that
; happens to be satisfied in one of them by the data is not the same thing as
; a rule enforced in both. The cost is one `cmp`.
UNI_LOW_LINE = 0x5F

; Trie value bits, from tables/README ("bit 0 = XID_Start, bit 1 =
; XID_Continue"). Named here so no caller has to write the literal.
UNI_XID_START_BIT    = 1
UNI_XID_CONTINUE_BIT = 2

; xid_flags(cp) -> eax = raw `xid` trie value for cp (bit 0 XID_Start,
; bit 1 XID_Continue). `_` is NOT folded in here -- this is the table's own
; answer, so a caller that wants the §8.2 rule calls xid_start/xid_continue
; below instead. Cannot fail; leaves CF clear.
;
; Two-stage trie, exactly as tables/README describes it:
;   stage2[stage1[cp >> 7] * 128 + (cp & 127)]
;
; OUT-OF-RANGE cp: `rassert`ed AND range-checked. Both, on purpose. The
; assert names the bug -- a codepoint at or above U+110000 can only reach
; here from a UTF-8 decoder that failed to reject an over-long or over-large
; sequence, which is an internal contract violation, not a source-level
; condition (docs/asm-conventions.md, "1.3 Error protocol: CF / eax": a
; compiler bug traps, it does not set CF). The range check bounds the damage
; in a RELEASE build, where `rassert` compiles to nothing: without it, a cp
; of, say, 0x1FFFFF indexes past the end of stage 1, reads whatever follows
; as a block number, and dereferences up to 8 MB past stage 2 -- a segfault
; rather than a wrong answer. One predictable, never-taken branch is a cheap
; price for that.
proc xid_flags, cp
	locals
	endl
	mov	rcx, [cp]
	rassert	rcx lt UNI_MAX_CP
	cmp	rcx, UNI_MAX_CP
	jae	.outside
	mov	eax, ecx
	shr	eax, UNI_BLOCK_SHIFT
	movzx	eax, word [uni_xid_stage1 + rax*2]
	shl	eax, UNI_BLOCK_SHIFT
	and	ecx, UNI_BLOCK_SIZE - 1
	add	eax, ecx
	movzx	eax, byte [uni_xid_stage2 + rax]
	return
  .outside:
	xor	eax, eax
	return
endp

; xid_start(cp) -> eax = 1 if cp may begin an Exsecutor identifier, else 0.
; §8.2: XID_Start, plus `_`. Cannot fail; leaves CF clear.
proc xid_start, cp
	locals
	endl
	mov	rdi, [cp]
	cmp	rdi, UNI_LOW_LINE
	je	.yes
	call	xid_flags
	and	eax, UNI_XID_START_BIT	; bit 0 -- already 0 or 1, no shift
	return
  .yes:
	mov	eax, 1
	return
endp

; xid_continue(cp) -> eax = 1 if cp may continue an Exsecutor identifier,
; else 0. §8.2: XID_Continue, plus `_` (redundant in UCD 17.0.0 -- see
; UNI_LOW_LINE above). Cannot fail; leaves CF clear.
proc xid_continue, cp
	locals
	endl
	mov	rdi, [cp]
	cmp	rdi, UNI_LOW_LINE
	je	.yes
	call	xid_flags
	and	eax, UNI_XID_CONTINUE_BIT
	shr	eax, 1
	return
  .yes:
	mov	eax, 1
	return
endp
