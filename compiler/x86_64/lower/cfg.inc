; compiler/x86_64/lower/cfg.inc
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
; Blocks, terminators and edges: docs/design/lowering.md section 2.4.
;
; A TERMINATOR IS THE ONLY THING THAT CREATES AN EDGE. Every one of the four
; goes through a helper here that pushes the instruction, links it into the
; current block's body, pushes one `bfa_edge_push(func, cur, target)` per
; successor IN OPERAND ORDER (`br`: the true target, then the false one), and
; then sets `cur` to 0 -- DEAD. Statement lowering on a dead `cur` is skipped
; entirely, so a block can never receive a second terminator and verifier rule
; 2 holds by shape rather than by check.
;
; Because `bfa_edge_push` records the order terminators were emitted, and
; `__lwr_add_phi_ops` walks that list, phi operand order IS predecessor order
; (docs/design/ssa-ir.md section 2.4) -- and verify.inc's rule 3 reads the same
; list, so the two cannot disagree.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md section 8.5.
; -----------------------------------------------------------------------------

include 'ssa.inc'

; lwr_blk_new(st) -> eax = a fresh, UNSEALED block id.
proc lwr_blk_new, lwrsp
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.func]
	call	bfa_block_new
	return
endp

; lwr_blk_new_sealed(st) -> eax = a fresh block, sealed immediately. For a
; block whose single predecessor's terminator is emitted right after it is
; created (every `si` arm and every `si` test) -- a read inside such a block
; never creates a phi.
proc lwr_blk_new_sealed, lwrsp
	uses	rbx
	locals
		slot lwrb, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	call	lwr_blk_new
	mov	[lwrb], eax
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_seal
	mov	eax, [lwrb]
	return
endp

; lwr_live(st) -> eax = 1 if there is a current block. Every statement and
; every expression entry point tests this first.
proc lwr_live, lwrsp
	locals
	endl
	mov	rdi, [lwrsp]
	xor	eax, eax
	cmp	dword [rdi + LwrState.cur], 0
	je	.no
	mov	eax, 1
  .no:
	return
endp

; lwr_emit(st, lwrop, lwraux, lwrty, lwra, lwrb) -> eax = the new value id,
; already linked into the current block's body list. The caller must have
; checked `lwr_live`; emitting into a dead block is a lowering bug and traps.
proc lwr_emit, lwrsp, lwrop, lwraux, lwrty, lwra, lwrb
	uses	rbx
	locals
		slot lwrid, dd
	endl
	mov	rbx, [lwrsp]
	mov	eax, [rbx + LwrState.cur]
	rassert	eax ne 0
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrop]
	mov	edx, [lwraux]
	mov	ecx, [lwrty]
	mov	r8d, [lwra]
	mov	r9d, [lwrb]
	call	bfa_inst_push
	mov	[lwrid], eax
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [rbx + LwrState.cur]
	mov	edx, eax
	call	bfa_block_link_body
	mov	eax, [lwrid]
	return
endp

; lwr_extra3(st, lwrx, lwry, lwrz) -> eax = the base index of a fresh
; three-word `extra` run. The shapes that need one -- `load`, `loadbits`,
; `index`, `br` -- all take exactly three.
proc lwr_extra3, lwrsp, lwrx, lwry, lwrz
	uses	rbx
	locals
		slot lwrbse, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrx]
	call	bfa_extra_push
	mov	[lwrbse], eax
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwry]
	call	bfa_extra_push
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrz]
	call	bfa_extra_push
	mov	eax, [lwrbse]
	return
endp

; lwr_extra4(st, lwrx, lwry, lwrz, lwrw) -> eax = base index. `store` and
; `storebits`.
proc lwr_extra4, lwrsp, lwrx, lwry, lwrz, lwrw
	uses	rbx
	locals
		slot lwrbse, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrx]
	mov	edx, [lwry]
	mov	ecx, [lwrz]
	call	lwr_extra3
	mov	[lwrbse], eax
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrw]
	call	bfa_extra_push
	mov	eax, [lwrbse]
	return
endp

; lwr_load(st, lwrty, lwrp, lwroff, lwrord) -> eax -- `%v = load T %p off
; ORDER`. `lwrord` is a `BFA_ORDER_*`; every caller but a `@transitus` field
; read passes `BFA_ORDER_NATIVUS` (0), and a field read passes the field's
; own `AstLayout.order` -- the two enumerations are one numbering, asserted
; below rather than converted.
;
; THE ORDER USED TO BE HARD-CODED HERE, and that was the bug the wire-codec
; plan's M1 guard stood in front of: every field access handed this routine an
; offset and nothing else, so a `u16:maior` field became a `nativus` load.
assert AST_ORD_NATIVUS = BFA_ORDER_NATIVUS
assert AST_ORD_MAIOR   = BFA_ORDER_MAIOR
assert AST_ORD_MINOR   = BFA_ORDER_MINOR
proc lwr_load, lwrsp, lwrty, lwrp, lwroff, lwrord
	uses	rbx
	locals
		slot lwrbse, dd
	endl
	mov	rbx, [lwrsp]
	mov	eax, [lwrord]
	rassert	eax le BFA_ORDER_MINOR
	mov	rdi, rbx
	mov	esi, [lwrp]
	mov	edx, [lwroff]
	mov	ecx, [lwrord]
	call	lwr_extra3
	mov	[lwrbse], eax
	mov	rdi, rbx
	mov	esi, BFA_OP_LOAD
	xor	rdx, rdx
	mov	ecx, [lwrty]
	mov	r8d, [lwrbse]
	mov	r9d, 3
	call	lwr_emit
	return
endp

; lwr_store(st, lwrty, lwrp, lwroff, lwrv, lwrord) -- `store T %p off ORDER
; %v`, `lwrord` as for `lwr_load`.
proc lwr_store, lwrsp, lwrty, lwrp, lwroff, lwrv, lwrord
	uses	rbx
	locals
		slot lwrbse, dd
	endl
	mov	rbx, [lwrsp]
	mov	eax, [lwrord]
	rassert	eax le BFA_ORDER_MINOR
	mov	rdi, rbx
	mov	esi, [lwrp]
	mov	edx, [lwroff]
	mov	ecx, [lwrord]
	mov	r8d, [lwrv]
	call	lwr_extra4
	mov	[lwrbse], eax
	mov	rdi, rbx
	mov	esi, BFA_OP_STORE
	xor	rdx, rdx
	mov	ecx, [lwrty]
	mov	r8d, [lwrbse]
	mov	r9d, 4
	call	lwr_emit
	return
endp

; lwr_loadbits(st, lwrty, lwrp, lwrbyte, lwrbit) -> eax -- `%v = loadbits T
; %p byte bit`: a field of at most eight bits that is not a whole byte at a
; byte boundary (docs/design/ssa-ir.md 2.7).
;
; THE BIT OPERAND COUNTS FROM THE MOST SIGNIFICANT BIT OF BYTE `byte`: bit 0 is
; the byte's top bit, and the field occupies bits `bit` .. `bit + width - 1`
; in that numbering -- spec §5.2 rule 1's "the first-declared field occupies
; the high bits", and exactly `AstLayout.bitoff` as checker/rows/layout.inc
; computes it (`bits and 7` of an offset that ACCUMULATES in declaration
; order). So `DeModFrame`'s `versio` is `loadbits u4 %p 1 0` and `genus` is
; `loadbits u4 %p 1 4`, and ssa-ir.md 2.7's own example (`loadbits u4 %p 1 4
; ; u4 at byte 1, bits 4..7, MSB-first`) is the same convention read the same
; way. The value is `(byte deorsum (8 - bit - width))` masked to `width` bits
; when the field lies in one byte.
proc lwr_loadbits, lwrsp, lwrty, lwrp, lwrbyte, lwrbit
	uses	rbx
	locals
		slot lwrbse, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrp]
	mov	edx, [lwrbyte]
	mov	ecx, [lwrbit]
	call	lwr_extra3
	mov	[lwrbse], eax
	mov	rdi, rbx
	mov	esi, BFA_OP_LOADBITS
	xor	rdx, rdx
	mov	ecx, [lwrty]
	mov	r8d, [lwrbse]
	mov	r9d, 3
	call	lwr_emit
	return
endp

; lwr_storebits(st, lwrty, lwrp, lwrbyte, lwrbit, lwrv) -- `storebits T %p
; byte bit %v`, the bit operand as for `lwr_loadbits`. A read-modify-write of
; the bytes the field touches is the backend's (M4); the IR says only where.
proc lwr_storebits, lwrsp, lwrty, lwrp, lwrbyte, lwrbit, lwrv
	uses	rbx
	locals
		slot lwrbse, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrp]
	mov	edx, [lwrbyte]
	mov	ecx, [lwrbit]
	mov	r8d, [lwrv]
	call	lwr_extra4
	mov	[lwrbse], eax
	mov	rdi, rbx
	mov	esi, BFA_OP_STOREBITS
	xor	rdx, rdx
	mov	ecx, [lwrty]
	mov	r8d, [lwrbse]
	mov	r9d, 4
	call	lwr_emit
	return
endp

; lwr_iconst(st, lwrty, lwrlo, lwrhi) -> eax -- `%v = iconst T imm`, the
; immediate as the two `extra` words docs/design/ssa-ir.md section 2.1
; specifies for a 64-bit value.
proc lwr_iconst, lwrsp, lwrty, lwrlo, lwrhi
	uses	rbx
	locals
		slot lwrbse, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrlo]
	call	bfa_extra_push
	mov	[lwrbse], eax
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrhi]
	call	bfa_extra_push
	mov	rdi, rbx
	mov	esi, BFA_OP_ICONST
	xor	rdx, rdx
	mov	ecx, [lwrty]
	mov	r8d, [lwrbse]
	mov	r9d, 2
	call	lwr_emit
	return
endp

; ============================================================================
; terminators -- the only edge builders
; ============================================================================

; lwr_jmp(st, lwrt) -- `jmp bN`, one edge, `cur` goes dead.
proc lwr_jmp, lwrsp, lwrt
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lwrsp]
	mov	r12d, [rbx + LwrState.cur]
	rassert	r12d ne 0
	mov	rdi, rbx
	mov	esi, BFA_OP_JMP
	xor	rdx, rdx
	xor	rcx, rcx
	mov	r8d, [lwrt]
	xor	r9, r9
	call	lwr_emit
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	mov	edx, [lwrt]
	call	bfa_edge_push
	mov	dword [rbx + LwrState.cur], 0
	return
endp

; lwr_br(st, lwrc, lwrbt, lwrbf) -- `br %c bT bF`, TWO edges in operand order
; (true target first), `cur` goes dead.
proc lwr_br, lwrsp, lwrc, lwrbt, lwrbf
	uses	rbx, r12
	locals
		slot lwrbse, dd
	endl
	mov	rbx, [lwrsp]
	mov	r12d, [rbx + LwrState.cur]
	rassert	r12d ne 0
	mov	rdi, rbx
	mov	esi, [lwrc]
	mov	edx, [lwrbt]
	mov	ecx, [lwrbf]
	call	lwr_extra3
	mov	[lwrbse], eax
	mov	rdi, rbx
	mov	esi, BFA_OP_BR
	xor	rdx, rdx
	xor	rcx, rcx
	mov	r8d, [lwrbse]
	mov	r9d, 3
	call	lwr_emit
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	mov	edx, [lwrbt]
	call	bfa_edge_push
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	mov	edx, [lwrbf]
	call	bfa_edge_push
	mov	dword [rbx + LwrState.cur], 0
	return
endp

; lwr_ret(st, lwrv) -- `ret [%v]`; `lwrv` = 0 for a valueless return. No edge.
proc lwr_ret, lwrsp, lwrv
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	eax, [rbx + LwrState.cur]
	rassert	eax ne 0
	mov	rdi, rbx
	mov	esi, BFA_OP_RET
	xor	rdx, rdx
	xor	rcx, rcx
	mov	r8d, [lwrv]
	xor	r9, r9
	call	lwr_emit
	mov	dword [rbx + LwrState.cur], 0
	return
endp

; lwr_trap(st, lwrk) -- `trap KIND`, `lwrk` the interned kind word. No edge
; and no releases (docs/design/ssa-ir.md section 2.8: a trap aborts).
proc lwr_trap, lwrsp, lwrk
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	eax, [rbx + LwrState.cur]
	rassert	eax ne 0
	mov	rdi, rbx
	mov	esi, BFA_OP_TRAP
	mov	edx, [lwrk]
	xor	rcx, rcx
	xor	r8, r8
	xor	r9, r9
	call	lwr_emit
	mov	dword [rbx + LwrState.cur], 0
	return
endp

; lwr_goto(st, lwrt) -- `jmp` to a block that may not have been entered yet,
; used where the walk creates the join lazily: if `cur` is dead nothing is
; emitted and no edge appears, which is how "a join with no edge is never
; created" (lowering.md section 2.4) is enforced at every arm's tail.
proc lwr_goto, lwrsp, lwrt
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	cmp	dword [rbx + LwrState.cur], 0
	je	.dead
	mov	rdi, rbx
	mov	esi, [lwrt]
	call	lwr_jmp
  .dead:
	return
endp
