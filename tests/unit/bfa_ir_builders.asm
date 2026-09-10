; tests/unit/bfa_ir_builders.asm
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
; The builders docs/design/lowering.md section 5 asked ir.inc for, exercised
; with NO PARSER INVOLVED in the construction: bfa_sig_new, bfa_global_new,
; bfa_edge_push (+ bfa_edge_count/bfa_edge_nth), bfa_block_prepend_phi,
; bfa_inst_operand_next/bfa_inst_operand_set, and BFA_OP_NOP.
;
; THE POINT OF THIS FIXTURE IS THE EDGE ORDER. docs/design/ssa-ir.md section
; 2.4: "Predecessors are an append-only edge list in the order their
; terminators were emitted; phi operands follow that order." verify.inc's
; rule 3 used to RECONSTRUCT that list by scanning blocks in INDEX order,
; because nothing populated BfaBlock.first_pred (its own FINDING 1), and its
; header recorded the exact limitation: "A fixture claiming to show
; block-index order and edge-emission order diverging CANNOT be constructed
; through this tree's only builder." It can be constructed through THESE
; builders, and `@iungo` below is that fixture -- a join whose predecessors
; are (b3, b1) in emission order and (b1, b3) in index order, which the
; verifier accepts, and which it rejects with rule 3 the moment the phi's
; operands are put back into index order (check 8: the non-vacuity proof is
; in the fixture, not in a claim about it).
;
; A FINDING, MEASURED HERE RATHER THAN ASSERTED (check 10). Section 2.11's
; text form has no way to write an edge order that differs from block order:
; a parser reading top to bottom emits terminators in block order by
; construction. So `@iungo` PRINTS to text that RE-PARSES into a module whose
; edge order is (b1, b3) while its phi still says (b3, %5) (b1, %7) -- and
; the re-parsed function therefore fails rule 3. Round-trip of the TEXT is
; exact (check 9); round-trip of the EDGE ORDER is not, and cannot be until
; section 2.11 gains a way to spell it. Reported, not worked around.
;
; Exit 0 = every check passed. 11 = arena init. 12..21 = check 2..11.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/backend_fasmg/verify.inc'

; The constant lowering.md finding 10 and verify.inc FINDING 2 both asked
; for. Its VALUE is load-bearing (verify.inc rule 9 compares against it and
; every zeroed BfaInst must read as `nop`), so it is checked here at
; assembly time rather than assumed.
assert BFA_OP_NOP = 0

segment readable executable
  start:
	lea	rdi, [irb_ar]
	mov	rsi, 16777216
	call	arena_init
	jc	.fail1
	lea	rdi, [irb_scr]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail1
	lea	rdi, [irb_var]
	mov	rsi, 4194304
	call	arena_init
	jc	.fail1

	lea	rdi, [irb_mod1]
	lea	rsi, [irb_ar]
	call	bfa_module_init

	lea	rdi, [irb_mod1]
	mov	esi, BFA_TK_U
	xor	edx, edx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	mov	[irb_tyu64], eax

; ---- check 2: bfa_sig_new ---------------------------------------------
	mov	eax, [irb_tyu64]
	mov	[irb_ptypes + 0], eax
	mov	[irb_ptypes + 4], eax

	lea	rdi, [irb_mod1]
	mov	esi, [irb_tyu64]
	lea	rdx, [irb_ptypes]
	mov	ecx, 1
	call	bfa_sig_new
	test	eax, eax
	jnz	.fail2
	mov	[irb_sig0], eax

	lea	rdi, [irb_mod1]
	mov	esi, [irb_tyu64]
	lea	rdx, [irb_ptypes]
	mov	ecx, 2
	call	bfa_sig_new
	cmp	eax, 1
	jne	.fail2
	mov	[irb_sig1], eax

	mov	rdi, qword [irb_mod1 + BfaModule.sigs]
	mov	esi, 1
	call	vec_get
	mov	ecx, [rax + BfaSig.p_first]
	cmp	ecx, 1				; sig0 consumed sig_extra[0]
	jne	.fail2
	mov	ecx, [rax + BfaSig.p_count]
	cmp	ecx, 2
	jne	.fail2
	mov	ecx, [rax + BfaSig.ret_ty]
	cmp	ecx, [irb_tyu64]
	jne	.fail2

; ---- check 3: bfa_global_new ------------------------------------------
	lea	rdi, [irb_mod1]
	mov	esi, 4
	mov	edx, 1
	lea	rcx, [irb_g0bytes]
	mov	r8d, 4
	call	bfa_global_new
	test	eax, eax
	jnz	.fail3

	lea	rdi, [irb_mod1]
	mov	esi, 3
	mov	edx, 8
	lea	rcx, [irb_g1bytes]
	mov	r8d, 3
	call	bfa_global_new
	cmp	eax, 1
	jne	.fail3

	mov	rdi, qword [irb_mod1 + BfaModule.globals]
	mov	esi, 1
	call	vec_get
	mov	ecx, [rax + BfaGlobal.data_off]
	cmp	ecx, 4
	jne	.fail3
	mov	ecx, [rax + BfaGlobal.data_len]
	cmp	ecx, 3
	jne	.fail3
	mov	ecx, [rax + BfaGlobal.algn]
	cmp	ecx, 8
	jne	.fail3
	mov	rdi, qword [irb_mod1 + BfaModule.gdata]
	mov	esi, 4
	call	vec_get
	movzx	ecx, byte [rax]
	cmp	ecx, 0xDE
	jne	.fail3

; ---- check 4: @duo -- two blocks, TWO prepended phis, one edge ---------
;   functio @duo (u64) -> u64 {
;   b0:  %0 = param u64 0 ; jmp b1
;   b1:  %1 = phi u64 b0 %0 ; %2 = phi u64 b0 %0 ; ret %1
;   }
; The two phis are PREPENDED, so the list order is the reverse of the
; prepend order -- which is what section 2.1's O(1)-prepend case means and
; what bfa_block_link_phi (an append) does not do.
	lea	rdi, [irb_mod1]
	call	bfa_func_new
	mov	[irb_f1], rax
	mov	rdi, qword [irb_mod1 + BfaModule.names]
	lea	rsi, [irb_nameduo]
	mov	rdx, 3
	call	intern_id
	mov	rcx, [irb_f1]
	mov	[rcx + BfaFunc.name], eax
	mov	ecx, [irb_sig0]
	mov	rax, [irb_f1]
	mov	[rax + BfaFunc.sig], ecx

	mov	rdi, [irb_f1]
	call	bfa_block_new
	mov	[irb_d0], eax			; internal 1 = text b0
	mov	rdi, [irb_f1]
	call	bfa_block_new
	mov	[irb_d1], eax			; internal 2 = text b1

	mov	rdi, [irb_f1]
	mov	esi, BFA_OP_PARAM
	xor	edx, edx
	mov	ecx, [irb_tyu64]
	xor	r8, r8
	xor	r9, r9
	call	bfa_inst_push
	mov	[irb_vp], eax
	mov	rdi, [irb_f1]
	mov	esi, [irb_d0]
	mov	edx, [irb_vp]
	call	bfa_block_link_body

	mov	rdi, [irb_f1]
	mov	esi, BFA_OP_JMP
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [irb_d1]
	xor	r9, r9
	call	bfa_inst_push
	push	rax
	mov	rdi, [irb_f1]
	mov	esi, [irb_d0]
	pop	rdx
	call	bfa_block_link_body
	mov	rdi, [irb_f1]
	mov	esi, [irb_d0]
	mov	edx, [irb_d1]
	call	bfa_edge_push
	cmp	eax, 1				; edge indices are 1-based
	jne	.fail4

	; phi X, then phi Y, each one operand: (b0, %0)
	mov	rdi, [irb_f1]
	mov	esi, [irb_d0]
	call	bfa_extra_push
	mov	[irb_ex], eax
	mov	rdi, [irb_f1]
	mov	esi, [irb_vp]
	call	bfa_extra_push
	mov	rdi, [irb_f1]
	mov	esi, BFA_OP_PHI
	xor	edx, edx
	mov	ecx, [irb_tyu64]
	mov	r8d, [irb_ex]
	mov	r9d, 2
	call	bfa_inst_push
	mov	[irb_phix], eax
	mov	rdi, [irb_f1]
	mov	esi, [irb_d1]
	mov	edx, [irb_phix]
	call	bfa_block_prepend_phi

	mov	rdi, [irb_f1]
	mov	esi, [irb_d0]
	call	bfa_extra_push
	mov	[irb_ex], eax
	mov	rdi, [irb_f1]
	mov	esi, [irb_vp]
	call	bfa_extra_push
	mov	rdi, [irb_f1]
	mov	esi, BFA_OP_PHI
	xor	edx, edx
	mov	ecx, [irb_tyu64]
	mov	r8d, [irb_ex]
	mov	r9d, 2
	call	bfa_inst_push
	mov	[irb_phiy], eax
	mov	rdi, [irb_f1]
	mov	esi, [irb_d1]
	mov	edx, [irb_phiy]
	call	bfa_block_prepend_phi

	mov	rdi, [irb_f1]
	mov	esi, [irb_d1]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_phi]
	cmp	ecx, [irb_phiy]			; last prepended is first
	jne	.fail4
	mov	ecx, [rax + BfaBlock.last_phi]
	cmp	ecx, [irb_phix]			; and the first stays the tail
	jne	.fail4
	mov	rdi, [irb_f1]
	mov	esi, [irb_phiy]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	cmp	ecx, [irb_phix]
	jne	.fail4

	mov	rdi, [irb_f1]
	mov	esi, BFA_OP_RET
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [irb_phix]
	xor	r9, r9
	call	bfa_inst_push
	push	rax
	mov	rdi, [irb_f1]
	mov	esi, [irb_d1]
	pop	rdx
	call	bfa_block_link_body

	mov	rdi, [irb_f1]
	mov	esi, [irb_d1]
	call	bfa_edge_count
	cmp	eax, 1
	jne	.fail4
	mov	rdi, [irb_f1]
	mov	esi, [irb_d1]
	xor	edx, edx
	call	bfa_edge_nth
	cmp	eax, [irb_d0]
	jne	.fail4

	lea	rdi, [irb_mod1]
	mov	rsi, [irb_f1]
	lea	rdx, [irb_var]
	lea	rcx, [irb_oblk]
	lea	r8, [irb_oinst]
	call	bfa_verify_func
	test	eax, eax
	jnz	.fail4

; ---- check 5: @iungo -- block index order != terminator emission order --
;   functio @iungo (u64 u64) -> u64 {
;   b0: %0 = param u64 0 ; %1 = param u64 1 ; %2 = cmp.lt u64 %0 %1
;       br %2 b1 b3
;   b1: %3 = addw u64 %1 %0 ; jmp b2
;   b2: %4 = phi u64 b3 %5 b1 %3 ; ret %4
;   b3: %5 = addw u64 %0 %1 ; jmp b2
;   }
; Blocks are CREATED b0,b1,b2,b3 (internal ids 1,2,3,4) but b3's terminator
; is EMITTED BEFORE b1's, so b2's predecessor list is (b3, b1) and not the
; (b1, b3) an index scan would produce.
	lea	rdi, [irb_mod1]
	call	bfa_func_new
	mov	[irb_f2], rax
	mov	rdi, qword [irb_mod1 + BfaModule.names]
	lea	rsi, [irb_nameiun]
	mov	rdx, 5
	call	intern_id
	mov	rcx, [irb_f2]
	mov	[rcx + BfaFunc.name], eax
	mov	ecx, [irb_sig1]
	mov	rax, [irb_f2]
	mov	[rax + BfaFunc.sig], ecx

	mov	rdi, [irb_f2]
	call	bfa_block_new
	mov	[irb_j0], eax
	mov	rdi, [irb_f2]
	call	bfa_block_new
	mov	[irb_j1], eax
	mov	rdi, [irb_f2]
	call	bfa_block_new
	mov	[irb_j2], eax
	mov	rdi, [irb_f2]
	call	bfa_block_new
	mov	[irb_j3], eax

	; b0
	mov	rdi, [irb_f2]
	mov	esi, BFA_OP_PARAM
	xor	edx, edx
	mov	ecx, [irb_tyu64]
	xor	r8, r8
	xor	r9, r9
	call	bfa_inst_push
	mov	[irb_p0], eax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j0]
	mov	edx, [irb_p0]
	call	bfa_block_link_body

	mov	rdi, [irb_f2]
	mov	esi, BFA_OP_PARAM
	xor	edx, edx
	mov	ecx, [irb_tyu64]
	mov	r8d, 1
	xor	r9, r9
	call	bfa_inst_push
	mov	[irb_p1], eax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j0]
	mov	edx, [irb_p1]
	call	bfa_block_link_body

	mov	rdi, [irb_f2]
	mov	esi, BFA_OP_CMP
	mov	edx, BFA_PRED_LT
	mov	ecx, [irb_tyu64]
	mov	r8d, [irb_p0]
	mov	r9d, [irb_p1]
	call	bfa_inst_push
	mov	[irb_vc], eax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j0]
	mov	edx, [irb_vc]
	call	bfa_block_link_body

	mov	rdi, [irb_f2]
	mov	esi, [irb_vc]
	call	bfa_extra_push
	mov	[irb_ex], eax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j1]
	call	bfa_extra_push
	mov	rdi, [irb_f2]
	mov	esi, [irb_j3]
	call	bfa_extra_push
	mov	rdi, [irb_f2]
	mov	esi, BFA_OP_BR
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [irb_ex]
	mov	r9d, 3
	call	bfa_inst_push
	push	rax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j0]
	pop	rdx
	call	bfa_block_link_body
	mov	rdi, [irb_f2]
	mov	esi, [irb_j0]
	mov	edx, [irb_j1]
	call	bfa_edge_push
	mov	rdi, [irb_f2]
	mov	esi, [irb_j0]
	mov	edx, [irb_j3]
	call	bfa_edge_push

	; b3's body and terminator -- EMITTED BEFORE b1's
	mov	rdi, [irb_f2]
	mov	esi, BFA_OP_ADDW
	xor	edx, edx
	mov	ecx, [irb_tyu64]
	mov	r8d, [irb_p0]
	mov	r9d, [irb_p1]
	call	bfa_inst_push
	mov	[irb_v3], eax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j3]
	mov	edx, [irb_v3]
	call	bfa_block_link_body

	mov	rdi, [irb_f2]
	mov	esi, BFA_OP_JMP
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [irb_j2]
	xor	r9, r9
	call	bfa_inst_push
	push	rax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j3]
	pop	rdx
	call	bfa_block_link_body
	mov	rdi, [irb_f2]
	mov	esi, [irb_j3]
	mov	edx, [irb_j2]
	call	bfa_edge_push

	; b1's body and terminator -- AFTER b3's
	mov	rdi, [irb_f2]
	mov	esi, BFA_OP_ADDW
	xor	edx, edx
	mov	ecx, [irb_tyu64]
	mov	r8d, [irb_p1]
	mov	r9d, [irb_p0]
	call	bfa_inst_push
	mov	[irb_v1], eax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j1]
	mov	edx, [irb_v1]
	call	bfa_block_link_body

	mov	rdi, [irb_f2]
	mov	esi, BFA_OP_JMP
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [irb_j2]
	xor	r9, r9
	call	bfa_inst_push
	push	rax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j1]
	pop	rdx
	call	bfa_block_link_body
	mov	rdi, [irb_f2]
	mov	esi, [irb_j1]
	mov	edx, [irb_j2]
	call	bfa_edge_push

	; the join's phi: operands in EDGE order, (b3 %5) then (b1 %3)
	mov	rdi, [irb_f2]
	mov	esi, [irb_j3]
	call	bfa_extra_push
	mov	[irb_phix2], eax		; base of the phi's extra range
	mov	rdi, [irb_f2]
	mov	esi, [irb_v3]
	call	bfa_extra_push
	mov	rdi, [irb_f2]
	mov	esi, [irb_j1]
	call	bfa_extra_push
	mov	rdi, [irb_f2]
	mov	esi, [irb_v1]
	call	bfa_extra_push
	mov	rdi, [irb_f2]
	mov	esi, BFA_OP_PHI
	xor	edx, edx
	mov	ecx, [irb_tyu64]
	mov	r8d, [irb_phix2]
	mov	r9d, 4
	call	bfa_inst_push
	mov	[irb_vphi], eax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j2]
	mov	edx, [irb_vphi]
	call	bfa_block_prepend_phi

	mov	rdi, [irb_f2]
	mov	esi, BFA_OP_RET
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [irb_vphi]
	xor	r9, r9
	call	bfa_inst_push
	push	rax
	mov	rdi, [irb_f2]
	mov	esi, [irb_j2]
	pop	rdx
	call	bfa_block_link_body

; ---- check 6: the edge list really is (b3, b1), not (b1, b3) ----------
	mov	rdi, [irb_f2]
	mov	esi, [irb_j2]
	call	bfa_edge_count
	cmp	eax, 2
	jne	.fail6
	mov	rdi, [irb_f2]
	mov	esi, [irb_j2]
	xor	edx, edx
	call	bfa_edge_nth
	cmp	eax, [irb_j3]			; emission order: b3 first
	jne	.fail6
	mov	rdi, [irb_f2]
	mov	esi, [irb_j2]
	mov	edx, 1
	call	bfa_edge_nth
	cmp	eax, [irb_j1]
	jne	.fail6
	; and the two orders really do differ: b1 < b3 by index
	mov	eax, [irb_j1]
	cmp	eax, [irb_j3]
	jge	.fail6

; ---- check 7: the verifier ACCEPTS the phi in edge order --------------
	lea	rdi, [irb_mod1]
	mov	rsi, [irb_f2]
	lea	rdx, [irb_var]
	lea	rcx, [irb_oblk]
	lea	r8, [irb_oinst]
	call	bfa_verify_func
	test	eax, eax
	jnz	.fail7

; ---- check 8: and REJECTS it in block-index order (rule 3) ------------
; The mutation is two bfa_inst_operand_set-shaped writes through
; bfa_extra_set: swap the (b3 %5) and (b1 %3) pairs. If rule 3 still read
; a block-index scan, THIS is the order it would accept and the one above
; is the one it would reject -- so the pair of checks pins the direction.
	mov	rdi, [irb_f2]
	mov	esi, [irb_phix2]
	mov	edx, [irb_j1]
	call	bfa_extra_set
	mov	rdi, [irb_f2]
	mov	eax, [irb_phix2]
	inc	eax
	mov	esi, eax
	mov	edx, [irb_v1]
	call	bfa_extra_set
	mov	rdi, [irb_f2]
	mov	eax, [irb_phix2]
	add	eax, 2
	mov	esi, eax
	mov	edx, [irb_j3]
	call	bfa_extra_set
	mov	rdi, [irb_f2]
	mov	eax, [irb_phix2]
	add	eax, 3
	mov	esi, eax
	mov	edx, [irb_v3]
	call	bfa_extra_set

	lea	rdi, [irb_mod1]
	mov	rsi, [irb_f2]
	lea	rdx, [irb_var]
	lea	rcx, [irb_oblk]
	lea	r8, [irb_oinst]
	call	bfa_verify_func
	cmp	eax, BFA_VERIFY_R3_PHI
	jne	.fail8

	; put it back
	mov	rdi, [irb_f2]
	mov	esi, [irb_phix2]
	mov	edx, [irb_j3]
	call	bfa_extra_set
	mov	rdi, [irb_f2]
	mov	eax, [irb_phix2]
	inc	eax
	mov	esi, eax
	mov	edx, [irb_v3]
	call	bfa_extra_set
	mov	rdi, [irb_f2]
	mov	eax, [irb_phix2]
	add	eax, 2
	mov	esi, eax
	mov	edx, [irb_j1]
	call	bfa_extra_set
	mov	rdi, [irb_f2]
	mov	eax, [irb_phix2]
	add	eax, 3
	mov	esi, eax
	mov	edx, [irb_v1]
	call	bfa_extra_set

; ---- check 9: the operand-slot iterator ------------------------------
; `%2 = cmp.lt u64 %0 %1`: two inline value operands, then the end.
	mov	rdi, [irb_f2]
	mov	esi, [irb_vc]
	mov	edx, 1
	call	bfa_inst_operand_next
	cmp	ecx, BFA_OSLOT_A
	jne	.fail9
	cmp	edx, [irb_p0]
	jne	.fail9
	cmp	eax, 2
	jne	.fail9

	mov	rdi, [irb_f2]
	mov	esi, [irb_vc]
	mov	edx, 2
	call	bfa_inst_operand_next
	cmp	ecx, BFA_OSLOT_B
	jne	.fail9
	cmp	edx, [irb_p1]
	jne	.fail9

	mov	rdi, [irb_f2]
	mov	esi, [irb_vc]
	mov	edx, 3
	call	bfa_inst_operand_next
	test	eax, eax
	jnz	.fail9

; the phi: the BLOCK words are skipped, the two VALUE words are not.
	mov	rdi, [irb_f2]
	mov	esi, [irb_vphi]
	mov	edx, 1
	call	bfa_inst_operand_next
	mov	[irb_curs], eax
	cmp	edx, [irb_v3]
	jne	.fail9
	mov	eax, [irb_phix2]
	add	eax, 1 + BFA_OSLOT_EXTRA0
	cmp	ecx, eax
	jne	.fail9

	mov	rdi, [irb_f2]
	mov	esi, [irb_vphi]
	mov	edx, [irb_curs]
	call	bfa_inst_operand_next
	mov	[irb_curs], eax
	cmp	edx, [irb_v1]
	jne	.fail9
	mov	eax, [irb_phix2]
	add	eax, 3 + BFA_OSLOT_EXTRA0
	cmp	ecx, eax
	jne	.fail9

	mov	rdi, [irb_f2]
	mov	esi, [irb_vphi]
	mov	edx, [irb_curs]
	call	bfa_inst_operand_next
	test	eax, eax
	jnz	.fail9

; bfa_inst_operand_set writes where the iterator pointed.
	mov	rdi, [irb_f2]
	mov	esi, [irb_vc]
	mov	edx, BFA_OSLOT_B
	mov	ecx, [irb_p0]
	call	bfa_inst_operand_set
	mov	rdi, [irb_f2]
	mov	esi, [irb_vc]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.b]
	cmp	ecx, [irb_p0]
	jne	.fail9
	mov	rdi, [irb_f2]
	mov	esi, [irb_vc]
	mov	edx, BFA_OSLOT_B
	mov	ecx, [irb_p1]
	call	bfa_inst_operand_set

; ---- check 10: print -> parse -> print is byte-identical --------------
	lea	rdi, [irb_mod1]
	lea	rsi, [irb_ar]
	call	bfa_print_module
	mov	[irb_t1p], rax
	mov	[irb_t1l], rdx

	lea	rdi, [irb_ar]
	lea	rsi, [irb_scr]
	mov	rdx, [irb_t1p]
	mov	rcx, [irb_t1l]
	call	bfa_parse_module
	mov	[irb_mod2], rax

	mov	rdi, [irb_mod2]
	lea	rsi, [irb_ar]
	call	bfa_print_module
	mov	[irb_t2p], rax
	mov	[irb_t2l], rdx

	mov	rdi, [irb_t1p]
	mov	rsi, [irb_t1l]
	mov	rdx, [irb_t2p]
	mov	rcx, [irb_t2l]
	call	__bfa_streq
	test	eax, eax
	jz	.fail10

; ---- check 11: the re-parsed @iungo FAILS rule 3 ----------------------
; Not a bug in the parser and not one in the verifier: section 2.11's text
; cannot express an edge order that differs from block order, so the
; re-parse necessarily rebuilds (b1, b3) under a phi that says (b3, b1).
; Asserted here so the limitation is measured. See this file's header.
	mov	rdi, [irb_mod2]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, 1
	call	vec_get
	mov	rax, [rax]
	mov	[irb_f2b], rax

	mov	rdi, [irb_mod2]
	mov	rsi, [irb_f2b]
	lea	rdx, [irb_var]
	lea	rcx, [irb_oblk]
	lea	r8, [irb_oinst]
	call	bfa_verify_func
	cmp	eax, BFA_VERIFY_R3_PHI
	jne	.fail11

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1: mov eax,231
	mov edi,11
	syscall
  .fail2: mov eax,231
	mov edi,12
	syscall
  .fail3: mov eax,231
	mov edi,13
	syscall
  .fail4: mov eax,231
	mov edi,14
	syscall
  .fail6: mov eax,231
	mov edi,16
	syscall
  .fail7: mov eax,231
	mov edi,17
	syscall
  .fail8: mov eax,231
	mov edi,18
	syscall
  .fail9: mov eax,231
	mov edi,19
	syscall
  .fail10: mov eax,231
	mov edi,20
	syscall
  .fail11: mov eax,231
	mov edi,21
	syscall

segment readable
  irb_nameduo	db "duo"
  irb_nameiun	db "iungo"
  irb_g0bytes	db 0x01, 0x02, 0x03, 0x04
  irb_g1bytes	db 0xDE, 0xAD, 0xBE

segment readable writeable
  irb_ar	rb sizeof.Arena
  irb_scr	rb sizeof.Arena
  irb_var	rb sizeof.Arena
  irb_mod1	rb sizeof.BfaModule
  irb_mod2	dq 0
  irb_f1	dq 0
  irb_f2	dq 0
  irb_f2b	dq 0
  irb_tyu64	dd 0
  irb_sig0	dd 0
  irb_sig1	dd 0
  irb_ptypes	dd 0, 0
  irb_d0	dd 0
  irb_d1	dd 0
  irb_vp	dd 0
  irb_phix	dd 0
  irb_phiy	dd 0
  irb_ex	dd 0
  irb_j0	dd 0
  irb_j1	dd 0
  irb_j2	dd 0
  irb_j3	dd 0
  irb_p0	dd 0
  irb_p1	dd 0
  irb_vc	dd 0
  irb_v1	dd 0
  irb_v3	dd 0
  irb_phix2	dd 0
  irb_vphi	dd 0
  irb_curs	dd 0
  irb_oblk	dd 0
  irb_oinst	dd 0
  irb_t1p	dq 0
  irb_t1l	dq 0
  irb_t2p	dq 0
  irb_t2l	dq 0
