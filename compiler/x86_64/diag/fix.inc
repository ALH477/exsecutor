; compiler/x86_64/diag/fix.inc
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
; The machine-applicable fix payload. §8.3: "Capability and lexicon errors
; ship machine-applicable fixes; `exsc emenda` applies them. Both classes are
; unusually suited to auto-fix because the required edit is mechanically
; derivable."
;
; ONE EDIT SHAPE, NOT THREE. Every fix this compiler can express is
; "replace the bytes [start, start+len) of file `file_id` with `text`."
; Insertion is that with `len = 0`; deletion is that with `text_len = 0`.
; `kind` is carried anyway, and carried EXPLICITLY rather than inferred from
; which field happens to be zero, for one reason: the machine output (§8.3's
; whole point -- "tools match codes, never English prose") should not require
; a consumer to reconstruct the author's intent from a coincidence of
; lengths. A zero-length replacement of a zero-length span is a no-op under
; inference and an obvious bug under an explicit kind.
;
; THE EDIT SITE IS AN rt/span.inc `Span`, NOT A SECOND COORDINATE SYSTEM.
; A fix names a byte range in a file exactly the way a diagnostic names one,
; because a fix IS a diagnostic's span most of the time (replace the offending
; identifier) and is a nearby span the rest of the time (insert a `poscit`
; clause on the enclosing signature). Two independently-shaped range types
; would guarantee that one of them eventually drifted -- see rt/span.inc's
; own header on why `file_id` is an intern id and offsets are bytes.
;
; `text_ptr` IS NOT SOURCE TEXT and is never echoed through diag_escape's
; source path by accident: it is compiler-authored replacement text (a
; correctly-spelled morpheme, a capability atom's name). It is still escaped
; when rendered -- see render.inc -- because "escape everything echoed" is
; cheaper to hold than "escape everything echoed except the cases someone
; argued were safe," and §8.3's failure mode is precisely a call site that
; was believed trusted.
;
; WHICH CODES MUST CARRY A FIX. §8.3 names two classes -- "capability and
; lexicon errors" -- without listing their members. The spec is specific
; enough elsewhere to enumerate them rather than guess, and each membership
; is cited rather than inferred from the code's leading digits (which would
; put `E0500` in the type range and get it wrong):
;   - capability: §2.4's unrepresentability table has one row for ambient
;     authority, and it names exactly `EXS-E0421`, `E0500`, `E0501`, `E0510`.
;     §4.1 rule 7 is what puts `E0500`/`E0501` under §4 despite the numbering.
;   - lexicon: `EXS-E0601`, `E0602`, `E0603`, `E0610` -- §3's rules, one code
;     each (§3.4 for the suffix contract, §3.5 for the prefix law, §3.8 for
;     the two-affix composition bound). §16 writes the range as
;     "`EXS-E0601`-`EXS-E0610`" and says in as many words that it is where
;     `exsc emenda` coverage has to be widest.
; `EXS-E0520` is deliberately NOT in the set: §2.4 files it under "refcount
; race via FFI," not ambient authority, and §6.4's rule is not mechanically
; repairable by an edit.
;
; `diag_fix_required` is a PREDICATE, not an enforcement point. It is
; deliberately not `rassert`ed inside the renderer: the renderer is the code
; that runs when something has already gone wrong, and trapping there turns
; "the compiler produced a diagnostic missing its fix" into "the compiler
; died while reporting an error," which is strictly worse for whoever is
; trying to read the error. The check belongs at the CONSTRUCTION site -- in
; whichever pass raises E0421 -- and this predicate is what that site (and
; tests/unit/diag_fix_payload.asm) calls.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3, §13; §2.4, §4.1 and §15 for
; the two class memberships above.
; -----------------------------------------------------------------------------

include 'notes.inc'
include '../rt/span.inc'

DIAG_FIX_NONE    = 0
DIAG_FIX_REPLACE = 1
DIAG_FIX_INSERT  = 2
DIAG_FIX_DELETE  = 3

; 40 bytes. `pad0`/`pad1` are explicit, not incidental: the two `dq` fields
; must be 8-aligned, and a reader should be able to see the layout without
; adding up field widths (§5.2's own "implicit padding in a @transitus type"
; is EXS-E0322 for exactly this reason -- the compiler holds itself to the
; habit it enforces).
struct DiagFix
	field kind,     dd
	field pad0,     dd
	edit Span
	field pad1,     dd
	field text_ptr, dq
	field text_len, dq
end struct

; diag_fix_clear(fx) -- DIAG_FIX_NONE, every other field zeroed. Zeroing
; rather than leaving stale bytes matters: render.inc emits `"fix":null` on
; the strength of `kind` alone, and a half-populated record that is never
; rendered is still a record a later pass could read.
proc diag_fix_clear, fx
	locals
	endl
	mov	rdi, [fx]
	mov	dword [rdi + DiagFix.kind], DIAG_FIX_NONE
	mov	dword [rdi + DiagFix.pad0], 0
	mov	dword [rdi + DiagFix.edit.file_id], 0
	mov	dword [rdi + DiagFix.edit.start], 0
	mov	dword [rdi + DiagFix.edit.len], 0
	mov	dword [rdi + DiagFix.pad1], 0
	mov	qword [rdi + DiagFix.text_ptr], 0
	mov	qword [rdi + DiagFix.text_len], 0
	return
endp

; diag_fix_replace(fx, file_id, off, len, txt, tlen) -- replace the bytes
; [off, off+len) of `file_id` with the `tlen` bytes at `txt`.
proc diag_fix_replace, fx, file_id, off, len, txt, tlen
	locals
	endl
	mov	rdi, [fx]
	mov	dword [rdi + DiagFix.kind], DIAG_FIX_REPLACE
	mov	dword [rdi + DiagFix.pad0], 0
	mov	eax, [file_id]
	mov	[rdi + DiagFix.edit.file_id], eax
	mov	eax, [off]
	mov	[rdi + DiagFix.edit.start], eax
	mov	eax, [len]
	mov	[rdi + DiagFix.edit.len], eax
	mov	dword [rdi + DiagFix.pad1], 0
	mov	rax, [txt]
	mov	[rdi + DiagFix.text_ptr], rax
	mov	rax, [tlen]
	mov	[rdi + DiagFix.text_len], rax
	return
endp

; diag_fix_insert(fx, file_id, off, txt, tlen) -- insert `tlen` bytes at byte
; offset `off`. A zero-length replacement; `kind` is what distinguishes it.
proc diag_fix_insert, fx, file_id, off, txt, tlen
	locals
	endl
	mov	rdi, [fx]
	mov	dword [rdi + DiagFix.kind], DIAG_FIX_INSERT
	mov	dword [rdi + DiagFix.pad0], 0
	mov	eax, [file_id]
	mov	[rdi + DiagFix.edit.file_id], eax
	mov	eax, [off]
	mov	[rdi + DiagFix.edit.start], eax
	mov	dword [rdi + DiagFix.edit.len], 0
	mov	dword [rdi + DiagFix.pad1], 0
	mov	rax, [txt]
	mov	[rdi + DiagFix.text_ptr], rax
	mov	rax, [tlen]
	mov	[rdi + DiagFix.text_len], rax
	return
endp

; diag_fix_delete(fx, file_id, off, len) -- delete [off, off+len). A
; replacement with empty text.
proc diag_fix_delete, fx, file_id, off, len
	locals
	endl
	mov	rdi, [fx]
	mov	dword [rdi + DiagFix.kind], DIAG_FIX_DELETE
	mov	dword [rdi + DiagFix.pad0], 0
	mov	eax, [file_id]
	mov	[rdi + DiagFix.edit.file_id], eax
	mov	eax, [off]
	mov	[rdi + DiagFix.edit.start], eax
	mov	eax, [len]
	mov	[rdi + DiagFix.edit.len], eax
	mov	dword [rdi + DiagFix.pad1], 0
	mov	qword [rdi + DiagFix.text_ptr], 0
	mov	qword [rdi + DiagFix.text_len], 0
	return
endp

; ---- stable kind tokens for the machine-readable output --------------------
; ASCII, lowercase, no spaces: these go into the JSON `kind` field, so they
; are part of the machine contract alongside the codes themselves and are as
; permanent as a code is. Never translated (§8.3: translation, if ever, is a
; lookup keyed on the code -- it does not reach the structured payload).
diag_fixkind_none    db 'none'
DIAG_FIXKIND_NONE_LEN    = $ - diag_fixkind_none
diag_fixkind_replace db 'replace'
DIAG_FIXKIND_REPLACE_LEN = $ - diag_fixkind_replace
diag_fixkind_insert  db 'insert'
DIAG_FIXKIND_INSERT_LEN  = $ - diag_fixkind_insert
diag_fixkind_delete  db 'delete'
DIAG_FIXKIND_DELETE_LEN  = $ - diag_fixkind_delete

; diag_fix_kind_str(kind) -> rax = pointer, rdx = length. An unknown `kind`
; traps unconditionally, for the same reason diag_lookup's miss does
; (util.inc's header): the result feeds machine-readable output, and there is
; no benign value to fall through to.
proc diag_fix_kind_str, kind
	locals
	endl
	mov	eax, [kind]
	cmp	eax, DIAG_FIX_REPLACE
	je	.replace
	cmp	eax, DIAG_FIX_INSERT
	je	.insert
	cmp	eax, DIAG_FIX_DELETE
	je	.delete
	cmp	eax, DIAG_FIX_NONE
	jne	.bad
	lea	rax, [diag_fixkind_none]
	mov	rdx, DIAG_FIXKIND_NONE_LEN
	return
  .replace:
	lea	rax, [diag_fixkind_replace]
	mov	rdx, DIAG_FIXKIND_REPLACE_LEN
	return
  .insert:
	lea	rax, [diag_fixkind_insert]
	mov	rdx, DIAG_FIXKIND_INSERT_LEN
	return
  .delete:
	lea	rax, [diag_fixkind_delete]
	mov	rdx, DIAG_FIXKIND_DELETE_LEN
	return
  .bad:
	db	0x0F, 0x0B		; ud2 -- see this proc's comment
endp

; ---- which codes §8.3 promises a fix for -----------------------------------
; Numeric parts (the CF/eax values), in §13's own table order. A plain array
; rather than a compare chain so the membership is readable as data and so
; adding a class member after a §13 amendment is a one-line edit.
;
; EXS-E0831 is the one `certus` profile code with a fix, and the reason is
; worth keeping: §5.4 already DEFINES the numeri defaults (ad_parem, vetita,
; explicita, conservata) and the profile's rule 14 requires only that they be
; written down rather than inherited. So the edit is exactly those four lines,
; and §8.3's "mechanically derivable" test is met. The other seven 08xx codes
; are absent on purpose -- moving an allocation, unwinding recursion, bounding
; a loop or dropping a capability each change what the program computes, which
; a fix-it must never do quietly.
diag_fixreq_codes:
	dd	421			; EXS-E0421  undeclared capability
	dd	422			; EXS-E0422  capability bound twice -- delete the second binding
	dd	423			; EXS-E0423  row item malformed -- delete the item
					; EXS-E0424 (entry point malformed) is absent on purpose: which
					;   function should be `initium` is not derivable. Neither are
					;   030x (typing), 0343 (shape) or 0511 (interface mismatch):
					;   every one of those edits changes what the program computes.
	dd	500			; EXS-E0500  module-level mutable state
	dd	501			; EXS-E0501  capability in module-level state
	dd	510			; EXS-E0510  capability escapes a dyn bound
	dd	601			; EXS-E0601  name does not decompose
	dd	602			; EXS-E0602  suffix contract disagrees
	dd	603			; EXS-E0603  prefix signature law violated
	dd	610			; EXS-E0610  composition depth exceeded
	dd	831			; EXS-E0831  numeri not fully declared (certus)
DIAG_FIXREQ_COUNT = ($ - diag_fixreq_codes) / 4

; diag_fix_required(code_num) -> eax = 1 if §8.3 promises this code ships a
; machine-applicable fix, else 0. See this file's header: a predicate for
; construction sites and tests, deliberately not enforced inside the
; renderer.
proc diag_fix_required, code_num
	locals
	endl
	mov	edx, [code_num]
	xor	ecx, ecx
  .loop:
	cmp	ecx, DIAG_FIXREQ_COUNT
	je	.no
	mov	eax, [diag_fixreq_codes + ecx*4]
	cmp	eax, edx
	je	.yes
	inc	ecx
	jmp	.loop
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp
