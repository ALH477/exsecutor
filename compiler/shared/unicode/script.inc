; compiler/shared/unicode/script.inc
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
; UAX #24 Script and Script_Extensions lookup: `script_of` (the primary
; Script property, one id per codepoint) and `script_resolve` (the codepoint's
; full script SET, which is Script_Extensions where that property is
; explicitly set and {Script} everywhere else).
;
; This is the data side of UTS #39 Moderately Restrictive, which spec §8.2
; requires and which `EXS-E0104` reports. The POLICY -- intersecting the
; resolved sets across an identifier, deciding what "mixed" means, choosing
; when to emit the diagnostic -- is the lexer's, not this file's
; (.claude/agents/unicode.md: "which feed both the mixed-script check
; (EXS-E0104, lexer's job) ... you only supply the tables").
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.2
; -----------------------------------------------------------------------------
;
; See `xid.inc`'s header for how to include this file, why `tables/tables.inc`
; is NOT included from here, and the addressing-mode fact every lookup below
; relies on. That header is the one place all three consumers document it.
;
; SCRIPT IDS. `tables/tables.inc` defines one `UNI_SCRIPT_xxxx` constant per
; registered ISO 15924-shaped short code (176 of them in UCD 17.0.0), numbered
; 0.. in alphabetical order of the short code -- a fixed, hash-free order, per
; CLAUDE.md "Determinism is not optional". Both routines here return ids from
; that space. Three matter to a caller by name:
;
;   UNI_SCRIPT_ZYYY   Common     -- e.g. digits, `_`, most punctuation
;   UNI_SCRIPT_ZINH   Inherited  -- e.g. combining marks
;   UNI_SCRIPT_ZZZZ   Unknown    -- the trie's default: unassigned codepoints
;
; UTS #39 treats Common and Inherited specially (a character in either is
; compatible with every script), so a caller implementing the mixed-script
; check needs to recognize those two ids rather than intersecting them
; literally. That rule is stated here because it is about the DATA; applying
; it is the caller's.
;
; MEASURED PROPERTIES of the `scriptext` table, all four checked directly
; against the checked-in blobs rather than assumed, because a caller will
; want to rely on them:
;   - `uni_scriptext_keys` is strictly ascending (669 entries) -- so the
;     binary search below is well founded.
;   - Every entry's id list is sorted ascending, with no duplicates. A caller
;     intersecting two resolved sets can therefore do a linear merge and does
;     not need to sort first.
;   - The longest list is 23 ids (`UNI_SCRIPTEXT_MAX_COUNT`), so 23 bytes is
;     the smallest `dstcap` that can never overflow.
;   - No entry contains Common or Inherited, and no entry is merely
;     [primary script] -- an explicit Script_Extensions entry always says
;     something the primary Script property does not.
; -----------------------------------------------------------------------------

include '../../x86_64/macros/proc.inc'
include '../../x86_64/macros/assert.inc'

; script_of(cp) -> eax = the primary Script property id (a UNI_SCRIPT_*
; constant) for cp. UNI_SCRIPT_ZZZZ (Unknown) for anything Scripts.txt does
; not cover, which is that file's own documented "@missing: 0000..10FFFF;
; Unknown" default and also what this routine returns for an out-of-range
; codepoint. Cannot fail; leaves CF clear.
;
; Same two-stage trie and the same rassert-plus-range-check pairing as
; `xid_flags` -- see xid.inc for why both are there.
proc script_of, cp
	locals
	endl
	mov	rcx, [cp]
	rassert	rcx lt UNI_MAX_CP
	cmp	rcx, UNI_MAX_CP
	jae	.unknown
	mov	eax, ecx
	shr	eax, UNI_BLOCK_SHIFT
	movzx	eax, word [uni_script_stage1 + rax*2]
	shl	eax, UNI_BLOCK_SHIFT
	and	ecx, UNI_BLOCK_SIZE - 1
	add	eax, ecx
	movzx	eax, byte [uni_script_stage2 + rax]
	return
  .unknown:
	mov	eax, UNI_SCRIPT_ZZZZ
	return
endp

; script_resolve(cp, dst, dstcap) -> eax = number of script ids written.
;
; Writes cp's resolved script SET to `dst` as consecutive BYTES (one
; UNI_SCRIPT_* id each), ascending, no duplicates. This is UAX #24's
; Script_Extensions where that property is explicitly set for cp, and the
; one-element set {script_of(cp)} everywhere else -- which is exactly the
; default Script_Extensions.txt itself documents, so the fallback is not an
; approximation.
;
; `dstcap` is a byte capacity. It is `rassert`ed against the actual count,
; not silently truncated: a caller passing a buffer too small for a set the
; table can produce is a compiler bug, not a source condition
; (docs/asm-conventions.md, "1.3 Error protocol: CF / eax"). A buffer of
; UNI_SCRIPTEXT_MAX_COUNT bytes can never trip it. Cannot otherwise fail;
; leaves CF clear.
;
; Binary search, not a hash: `tables/README.md` and CLAUDE.md both require it
; -- a hash table would make iteration order depend on the hash function,
; which spec §9.3 forbids for anything that can reach output.
proc script_resolve, cp, dst, dstcap
	uses	rbx, r12, r13
	locals
	endl
	mov	r12d, [cp]		; needle
	xor	ebx, ebx		; lo = 0
	mov	r13d, UNI_SCRIPTEXT_COUNT ; hi = count (exclusive)
  .search:
	cmp	ebx, r13d
	jae	.no_extension
	mov	eax, ebx
	add	eax, r13d
	shr	eax, 1			; mid = (lo + hi) / 2
	mov	ecx, [uni_scriptext_keys + rax*4]
	cmp	ecx, r12d
	jb	.go_high
	ja	.go_low
	jmp	.found
  .go_high:
	mov	ebx, eax
	inc	ebx			; lo = mid + 1
	jmp	.search
  .go_low:
	mov	r13d, eax		; hi = mid
	jmp	.search

	; Hit: vals[mid] packs (pool_byte_offset << 8) | count.
  .found:
	mov	eax, [uni_scriptext_vals + rax*4]
	mov	ecx, eax
	and	ecx, 0xFF		; count
	shr	eax, 8			; byte offset into the pool
	mov	rdx, [dstcap]
	rassert	rcx le rdx
	mov	rdi, [dst]
	xor	edx, edx		; i = 0
  .copy:
	cmp	edx, ecx
	jae	.copied
	mov	r8d, eax
	add	r8d, edx
	movzx	r9d, byte [uni_scriptext_pool + r8]
	mov	[rdi + rdx], r9b
	inc	edx
	jmp	.copy
  .copied:
	mov	eax, ecx
	return

	; Miss: the codepoint has no explicit Script_Extensions, so its script
	; set is the one-element set holding its primary Script property.
  .no_extension:
	mov	rdx, [dstcap]
	rassert	rdx ge 1
	mov	rdi, [cp]
	call	script_of
	mov	rcx, [dst]
	mov	[rcx], al
	mov	eax, 1
	return
endp
