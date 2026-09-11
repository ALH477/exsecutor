; tests/unit/lwr_ssa.asm
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
; THE SSA CORE, SCRIPTED, WITH NO AST AT ALL. docs/design/lowering.md section
; 5's first fixture: `lower/ssa.inc` takes var ids and block ids and never
; reads an `AstNode`, so a hand-driven sequence of writes, reads, seals and
; terminators exercises Braun et al.'s six procedures directly -- and it is
; this fixture, not the tree-walking one, that pins the PHI OPERAND ORDER.
;
; Three scripts, each its own `functio`, each verified by `bfa_verify_func`
; and printed by `bfa_print_module`:
;
;   1 @ansa   -- the loop of lowering.md section 5: write in b0, read in an
;                unsealed header, write in the body, seal at the back edge.
;                The whole printed function is compared byte for byte, so the
;                phi line is `%1 = phi u64 b0 %0 b2 %5` -- exactly two
;                operands, entry edge first, back edge second, which is the
;                order `bfa_edge_push` recorded and the order `verify.inc`
;                rule 3 reads.
;   2 @nidus  -- a variable unchanged across two nested loops. Every phi is
;                trivial and NONE survives. The count the DEFERRED SWEEP
;                (not the on-the-fly check in `addPhiOperands`) removed is
;                asserted to be 0, and that is a measurement, not a wish: on
;                this shape the local check does all the work, so the sweep
;                is *not* exercised here. Script 3 exists because of that.
;   3 @tardus -- a phi that is NOT trivial when it is sealed and becomes
;                trivial only when a LATER seal collapses one of its operands.
;                Braun re-tests it by walking the removed phi's users; this
;                pass has no use lists and finds it in the index-order sweep
;                instead (lowering.md section 2.3). The sweep count is
;                asserted to be exactly 1 -- delete the `lwr_sweep` call in
;                `lwr_fn_end` and this fixture fails twice over: the count is
;                0 and a `phi` survives into the printed text.
;
; WHAT THIS FIXTURE FOUND. lowering.md section 2.5 writes `seal(body1) at
; creation` for a `si` arm and `seal(exit) after the body` for a loop. "At
; creation" cannot be right for any block: `sealBlock` on a block with an
; EMPTY predecessor list sends the next read of it into
; `readVariableRecursive`'s sealed arm with zero predecessors, which is
; neither of Braun's two cases. A block must be sealed once the edge INTO it
; exists -- here, immediately after the terminator that made it -- which is
; the same point in the walk and not the same sentence. Recorded in the
; report.
;
; Exit 0 = every check passed. 10 = arena init. 11/12/13 = script 1/2/3's
; verifier verdict was not 0. 21 = script 1's text differs. 22 = a phi
; survived script 2. 23 = script 2's sweep count was not 0. 24 = a phi
; survived script 3. 25 = script 3's sweep count was not 1.
;
; Run with any argument to dump the printed module to stdout instead of
; comparing -- how the expected text below was produced and how a diff is
; read when one of the numbered exits fires.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/lower/cfg.inc'

segment readable executable
  start:
	mov	rbx, [rsp]			; argc, before anything moves rsp

	lea	rdi, [lsar]
	mov	rsi, 16 * 1024 * 1024
	call	arena_init
	jc	.bad0
	lea	rdi, [lsscr]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad0

	lea	rdi, [lsmod]
	lea	rsi, [lsar]
	call	bfa_module_init

	lea	rdi, [lsst]
	xor	rsi, rsi
	lea	rdx, [lsmod]
	lea	rcx, [lsar]
	lea	r8,  [lsscr]
	call	lwr_state_init

	call	ls_script1
	cmp	eax, 0
	jne	.bad1
	call	ls_script2
	cmp	eax, 0
	jne	.bad2
	call	ls_script3
	cmp	eax, 0
	jne	.bad3

	lea	rdi, [lsmod]
	lea	rsi, [lsar]
	call	bfa_print_module
	mov	[lsoutp], rax
	mov	[lsoutl], rdx

	cmp	rbx, 1
	jg	.dump

	; ---- 21: script 1's function, byte for byte ----
	mov	rax, [lsoutl]
	cmp	rax, lsexp.len
	jb	.bad21
	mov	rdi, [lsoutp]
	mov	rsi, lsexp.len
	lea	rdx, [lsexp]
	mov	rcx, lsexp.len
	call	__bfa_streq
	test	eax, eax
	jz	.bad21

	; ---- 22/24: no `phi` anywhere in the printed module ----
	; scripts 2 and 3 are the only sources of one, and script 1's is
	; asserted present by the byte comparison above, so this scan runs over
	; scripts 2 and 3's text only -- see ls_nophi.
	mov	rdi, [lsoutp]
	mov	rsi, [lsoutl]
	call	ls_nophi
	test	eax, eax
	jz	.bad22

	; ---- 23/25: the sweep counts ----
	cmp	dword [lssw2], 0
	jne	.bad23
	cmp	dword [lssw3], 1
	jne	.bad25

	xor	edi, edi
	call	sys_exit_group
  .dump:
	mov	edi, 1
	mov	rsi, [lsoutp]
	mov	rdx, [lsoutl]
	call	sys_write
	xor	edi, edi
	call	sys_exit_group
  .bad0:
	mov	rdi, 10
	call	sys_exit_group
  .bad1:
	mov	rdi, 11
	call	sys_exit_group
  .bad2:
	mov	rdi, 12
	call	sys_exit_group
  .bad3:
	mov	rdi, 13
	call	sys_exit_group
  .bad21:
	mov	edi, 2
	mov	rsi, [lsoutp]
	mov	rdx, [lsoutl]
	call	sys_write
	mov	rdi, 21
	call	sys_exit_group
  .bad22:
	mov	rdi, 22
	call	sys_exit_group
  .bad23:
	mov	rdi, 23
	call	sys_exit_group
  .bad25:
	mov	rdi, 25
	call	sys_exit_group

; ls_newfunc(lsnp, lsnl, lsrt) -> rax = a fresh BfaFunc named `lsnp`, one
; `u64` parameter, result `lsrt`, and the spec section 5.4 default `numeri`
; words so the printed header matches the canonical form.
proc ls_newfunc, lsnp, lsnl, lsrt
	uses	rbx, r12
	locals
		slot lssig, dd
	endl
	mov	eax, [lsst + LwrState.tu64]
	mov	[lsptys], eax
	lea	rdi, [lsmod]
	mov	esi, [lsrt]
	lea	rdx, [lsptys]
	mov	ecx, 1
	call	bfa_sig_new
	mov	[lssig], eax

	lea	rdi, [lsmod]
	call	bfa_func_new
	mov	rbx, rax
	mov	eax, [lssig]
	mov	[rbx + BfaFunc.sig], eax

	mov	rdi, [lsmod + BfaModule.names]
	mov	rsi, [lsnp]
	mov	rdx, [lsnl]
	call	intern_id
	mov	[rbx + BfaFunc.name], eax

	mov	rdi, [lsmod + BfaModule.names]
	lea	rsi, [lsn0]
	mov	rdx, lsn0.len
	call	intern_id
	mov	[rbx + BfaFunc.numeri0], eax
	mov	rdi, [lsmod + BfaModule.names]
	lea	rsi, [lsn1]
	mov	rdx, lsn1.len
	call	intern_id
	mov	[rbx + BfaFunc.numeri1], eax
	mov	rdi, [lsmod + BfaModule.names]
	lea	rsi, [lsn2]
	mov	rdx, lsn2.len
	call	intern_id
	mov	[rbx + BfaFunc.numeri2], eax
	mov	rdi, [lsmod + BfaModule.names]
	lea	rsi, [lsn3]
	mov	rdx, lsn3.len
	call	intern_id
	mov	[rbx + BfaFunc.numeri3], eax
	mov	rax, rbx
	return
endp

; ls_begin(lsfp) -- start a function on the shared LwrState: 32 variable
; slots, synthetic ids above 16, variable 1 typed u64 and SSA.
proc ls_begin, lsfp
	locals
	endl
	lea	rdi, [lsst]
	mov	rsi, [lsfp]
	mov	rdx, 32
	call	lwr_fn_begin
	mov	dword [lsst + LwrState.nbase], 16
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, LWR_VK_SSA
	mov	ecx, [lsst + LwrState.tu64]
	xor	r8, r8
	call	lwr_var_set
	return
endp

; ls_verify(lsfp) -> eax = bfa_verify_func's verdict (0 = ok).
proc ls_verify, lsfp
	locals
		slot lsvb, dd
		slot lsvi, dd
	endl
	lea	rdi, [lsmod]
	mov	rsi, [lsfp]
	lea	rdx, [lsar]
	lea	rcx, [lsvb]
	lea	r8,  [lsvi]
	call	bfa_verify_func
	return
endp

; ls_cmpu1(lsa, lsb) -> eax = a fresh `cmp.eq u64 %a %b` value (a u1).
proc ls_cmpu1, lsa, lsb
	locals
	endl
	lea	rdi, [lsst]
	mov	esi, BFA_OP_CMP
	mov	edx, BFA_PRED_EQ
	mov	ecx, [lsst + LwrState.tu64]
	mov	r8d, [lsa]
	mov	r9d, [lsb]
	call	lwr_emit
	return
endp

; ls_param0 -> eax = `%v = param u64 0`, emitted into the current block.
proc ls_param0
	locals
	endl
	lea	rdi, [lsst]
	mov	esi, BFA_OP_PARAM
	xor	rdx, rdx
	mov	ecx, [lsst + LwrState.tu64]
	xor	r8, r8
	xor	r9, r9
	call	lwr_emit
	return
endp

; ============================================================================
; script 1 -- @ansa: the loop of docs/design/lowering.md section 5
; ============================================================================
proc ls_script1
	uses	rbx, r12, r13
	locals
		slot ls1f, dq
		slot ls1b0, dd
		slot ls1b1, dd
		slot ls1b2, dd
		slot ls1b3, dd
		slot ls1v, dd
		slot ls1c, dd
	endl
	lea	rdi, [lsnansa]
	mov	rsi, lsnansa.len
	mov	edx, [lsst + LwrState.tu64]
	call	ls_newfunc
	mov	[ls1f], rax
	mov	rdi, rax
	call	ls_begin

	; ---- b0, sealed at creation: it has no predecessors, which is the one
	;      block for which "seal at creation" is right.
	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls1b0], eax
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, eax
	call	lwr_seal

	call	ls_param0
	mov	[ls1v], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls1b0]
	mov	ecx, [ls1v]
	call	lwr_write_var

	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls1b1], eax
	lea	rdi, [lsst]
	mov	esi, eax
	call	lwr_jmp

	; ---- b1, the header: UNSEALED, so the read makes an operandless phi
	mov	eax, [ls1b1]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls1b1]
	call	lwr_read_var
	mov	[ls1v], eax

	lea	rdi, [lsst]
	mov	esi, [lsst + LwrState.tu64]
	mov	edx, 10
	xor	rcx, rcx
	call	lwr_iconst
	mov	r12d, eax
	lea	rdi, [lsst]
	mov	esi, BFA_OP_CMP
	mov	edx, BFA_PRED_LT
	mov	ecx, [lsst + LwrState.tu64]
	mov	r8d, [ls1v]
	mov	r9d, r12d
	call	lwr_emit
	mov	[ls1c], eax

	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls1b2], eax
	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls1b3], eax
	lea	rdi, [lsst]
	mov	esi, [ls1c]
	mov	edx, [ls1b2]
	mov	ecx, [ls1b3]
	call	lwr_br
	; sealed HERE, not at creation: `lwr_br` is what put the edge in
	lea	rdi, [lsst]
	mov	esi, [ls1b2]
	call	lwr_seal
	lea	rdi, [lsst]
	mov	esi, [ls1b3]
	call	lwr_seal

	; ---- b2, the body
	mov	eax, [ls1b2]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls1b2]
	call	lwr_read_var
	mov	r13d, eax
	lea	rdi, [lsst]
	mov	esi, [lsst + LwrState.tu64]
	mov	edx, 1
	xor	rcx, rcx
	call	lwr_iconst
	mov	r12d, eax
	lea	rdi, [lsst]
	mov	esi, BFA_OP_ADDW
	xor	rdx, rdx
	mov	ecx, [lsst + LwrState.tu64]
	mov	r8d, r13d
	mov	r9d, r12d
	call	lwr_emit
	mov	[ls1v], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls1b2]
	mov	ecx, [ls1v]
	call	lwr_write_var
	lea	rdi, [lsst]
	mov	esi, [ls1b1]
	call	lwr_jmp

	; ---- the back edge exists: NOW the header's predecessor set is final
	lea	rdi, [lsst]
	mov	esi, [ls1b1]
	call	lwr_seal

	; ---- b3, the exit
	mov	eax, [ls1b3]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls1b3]
	call	lwr_read_var
	mov	[ls1v], eax
	lea	rdi, [lsst]
	mov	esi, [ls1v]
	call	lwr_ret

	lea	rdi, [lsst]
	call	lwr_fn_end
	mov	rdi, [ls1f]
	call	ls_verify
	return
endp

; ============================================================================
; script 2 -- @nidus: one variable, two nested loops, every phi trivial
; ============================================================================
; b1 entry -> b2 outer header -> b3 outer body -> b5 inner header -> b6 inner
; body -> b5 (back), b7 inner exit -> b2 (back), b4 outer exit.
; Both headers' phis are removed by `addPhiOperands`'s own triviality check at
; their seals, so the DEFERRED SWEEP has nothing left to do and reports 0.
; That is the measurement docs/design/lowering.md section 2.3 asks for on this
; shape; script 3 is the shape where the sweep is the only thing that works.
proc ls_script2
	uses	rbx, r12
	locals
		slot ls2f, dq
		slot ls2b0, dd
		slot ls2h1, dd
		slot ls2b1, dd
		slot ls2x, dd
		slot ls2h2, dd
		slot ls2b2, dd
		slot ls2e2, dd
		slot ls2v, dd
		slot ls2c, dd
	endl
	lea	rdi, [lsnnidus]
	mov	rsi, lsnnidus.len
	mov	edx, [lsst + LwrState.tu64]
	call	ls_newfunc
	mov	[ls2f], rax
	mov	rdi, rax
	call	ls_begin

	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls2b0], eax
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, eax
	call	lwr_seal
	call	ls_param0
	mov	[ls2v], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls2b0]
	mov	ecx, [ls2v]
	call	lwr_write_var
	mov	edi, [ls2v]
	mov	esi, [ls2v]
	call	ls_cmpu1
	mov	[ls2c], eax

	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls2h1], eax
	lea	rdi, [lsst]
	mov	esi, eax
	call	lwr_jmp

	; ---- outer header, unsealed
	mov	eax, [ls2h1]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls2h1]
	call	lwr_read_var
	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls2b1], eax
	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls2x], eax
	lea	rdi, [lsst]
	mov	esi, [ls2c]
	mov	edx, [ls2b1]
	mov	ecx, [ls2x]
	call	lwr_br
	lea	rdi, [lsst]
	mov	esi, [ls2b1]
	call	lwr_seal
	lea	rdi, [lsst]
	mov	esi, [ls2x]
	call	lwr_seal

	; ---- outer body: straight into the inner header
	mov	eax, [ls2b1]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls2h2], eax
	lea	rdi, [lsst]
	mov	esi, eax
	call	lwr_jmp

	; ---- inner header, unsealed
	mov	eax, [ls2h2]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls2h2]
	call	lwr_read_var
	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls2b2], eax
	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls2e2], eax
	lea	rdi, [lsst]
	mov	esi, [ls2c]
	mov	edx, [ls2b2]
	mov	ecx, [ls2e2]
	call	lwr_br
	lea	rdi, [lsst]
	mov	esi, [ls2b2]
	call	lwr_seal
	lea	rdi, [lsst]
	mov	esi, [ls2e2]
	call	lwr_seal

	; ---- inner body: the back edge, then the inner header is complete
	mov	eax, [ls2b2]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls2b2]
	call	lwr_read_var
	lea	rdi, [lsst]
	mov	esi, [ls2h2]
	call	lwr_jmp
	lea	rdi, [lsst]
	mov	esi, [ls2h2]
	call	lwr_seal

	; ---- inner exit: the outer back edge, then the outer header
	mov	eax, [ls2e2]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, [ls2h1]
	call	lwr_jmp
	lea	rdi, [lsst]
	mov	esi, [ls2h1]
	call	lwr_seal

	; ---- outer exit
	mov	eax, [ls2x]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls2x]
	call	lwr_read_var
	mov	[ls2v], eax
	lea	rdi, [lsst]
	mov	esi, [ls2v]
	call	lwr_ret

	lea	rdi, [lsst]
	call	lwr_fn_end
	mov	eax, [lsst + LwrState.nswept]
	mov	[lssw2], eax
	mov	rdi, [ls2f]
	call	ls_verify
	return
endp

; ============================================================================
; script 3 -- @tardus: the phi only the sweep can remove
; ============================================================================
; b1 entry -> (b2, b3); b2 -> b4; b3 -> b4; b4 -> (b5, b6); b5 -> b3.
; b3's predecessor set is complete only after b5 exists, so b4 is sealed while
; b3 still is not: b4's phi is filled with {%0, <b3's phi>}, two distinct
; values, and survives its own triviality check. When b3 is sealed its phi
; turns out to be {%0, %0} and collapses -- and b4's phi is now trivial with
; nothing left to notice it. Braun walks the removed phi's users; this pass
; has no use lists and the index-order sweep is what finds it.
proc ls_script3
	uses	rbx, r12
	locals
		slot ls3f, dq
		slot ls3b0, dd
		slot ls3a, dd
		slot ls3b, dd
		slot ls3h, dd
		slot ls3c, dd
		slot ls3x, dd
		slot ls3v, dd
		slot ls3p, dd
	endl
	lea	rdi, [lsntardus]
	mov	rsi, lsntardus.len
	mov	edx, [lsst + LwrState.tu64]
	call	ls_newfunc
	mov	[ls3f], rax
	mov	rdi, rax
	call	ls_begin

	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls3b0], eax
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, eax
	call	lwr_seal
	call	ls_param0
	mov	[ls3v], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls3b0]
	mov	ecx, [ls3v]
	call	lwr_write_var
	mov	edi, [ls3v]
	mov	esi, [ls3v]
	call	ls_cmpu1
	mov	[ls3p], eax

	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls3a], eax
	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls3b], eax
	lea	rdi, [lsst]
	mov	esi, [ls3p]
	mov	edx, [ls3a]
	mov	ecx, [ls3b]
	call	lwr_br
	; only `a` is sealed: `b` is still waiting for the edge from `c`
	lea	rdi, [lsst]
	mov	esi, [ls3a]
	call	lwr_seal

	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls3h], eax

	mov	eax, [ls3a]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, [ls3h]
	call	lwr_jmp

	mov	eax, [ls3b]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls3b]
	call	lwr_read_var		; b is unsealed: an incomplete phi
	lea	rdi, [lsst]
	mov	esi, [ls3h]
	call	lwr_jmp

	; ---- h's predecessors are complete; b's are not
	lea	rdi, [lsst]
	mov	esi, [ls3h]
	call	lwr_seal
	mov	eax, [ls3h]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls3h]
	call	lwr_read_var		; sealed, two preds: a phi, filled now,
					; {%0, b's phi} -- NOT trivial
	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls3c], eax
	lea	rdi, [lsst]
	call	lwr_blk_new
	mov	[ls3x], eax
	lea	rdi, [lsst]
	mov	esi, [ls3p]
	mov	edx, [ls3c]
	mov	ecx, [ls3x]
	call	lwr_br
	lea	rdi, [lsst]
	mov	esi, [ls3c]
	call	lwr_seal
	lea	rdi, [lsst]
	mov	esi, [ls3x]
	call	lwr_seal

	; ---- c writes the same value again and closes b's predecessor set
	mov	eax, [ls3c]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls3c]
	mov	ecx, [ls3v]
	call	lwr_write_var
	lea	rdi, [lsst]
	mov	esi, [ls3b]
	call	lwr_jmp
	lea	rdi, [lsst]
	mov	esi, [ls3b]
	call	lwr_seal		; b's phi collapses to %0 here

	mov	eax, [ls3x]
	mov	[lsst + LwrState.cur], eax
	lea	rdi, [lsst]
	mov	esi, 1
	mov	edx, [ls3x]
	call	lwr_read_var
	mov	[ls3v], eax
	lea	rdi, [lsst]
	mov	esi, [ls3v]
	call	lwr_ret

	lea	rdi, [lsst]
	call	lwr_fn_end
	mov	eax, [lsst + LwrState.nswept]
	mov	[lssw3], eax
	mov	rdi, [ls3f]
	call	ls_verify
	return
endp

; ls_nophi(lsp, lslen) -> eax = 1 if the three bytes `phi` appear NOWHERE after
; the first function's text. Scripts 2 and 3 must leave none; script 1's is
; inside `lsexp`, which the byte comparison above already covered, so the scan
; starts past it.
proc ls_nophi, lsp, lslen
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lsp]
	add	rbx, lsexp.len
	mov	r12, [lslen]
	sub	r12, lsexp.len
  .loop:
	cmp	r12, 3
	jb	.clean
	cmp	byte [rbx], 'p'
	jne	.next
	cmp	byte [rbx + 1], 'h'
	jne	.next
	cmp	byte [rbx + 2], 'i'
	jne	.next
	xor	eax, eax
	return
  .next:
	inc	rbx
	dec	r12
	jmp	.loop
  .clean:
	mov	eax, 1
	return
endp

segment readable writeable
  lsar:		rb sizeof.Arena
  lsscr:	rb sizeof.Arena
  lsmod:	rb sizeof.BfaModule
  lsst:		rb sizeof.LwrState
  lsptys:	dd 0
  lsoutp:	dq 0
  lsoutl:	dq 0
  lssw2:	dd 0
  lssw3:	dd 0

  lsn0		db 'ad_parem'
  .len = $ - lsn0
  lsn1		db 'vetita'
  .len = $ - lsn1
  lsn2		db 'explicita'
  .len = $ - lsn2
  lsn3		db 'conservata'
  .len = $ - lsn3
  lsnansa	db 'ansa'
  .len = $ - lsnansa
  lsnnidus	db 'nidus'
  .len = $ - lsnnidus
  lsntardus	db 'tardus'
  .len = $ - lsntardus

  ; ---- script 1's function, byte for byte, as `bfa_print_module` writes it.
  ; The phi line is the assertion docs/design/lowering.md section 5 asks for:
  ; two operands, `b0` (the entry edge, emitted first) before `b2` (the back
  ; edge). Swap the two `bfa_edge_push` calls in `lwr_br`/`lwr_jmp` and this
  ; line, and verifier rule 3, both change.
  lsexp:
  db "functio @ansa (u64) -> u64 numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param u64 0", 10
  db "jmp b1", 10
  db "b1:", 10
  db "%1 = phi u64 b0 %0 b2 %5", 10
  db "%2 = iconst u64 10", 10
  db "%3 = cmp.lt u64 %1 %2", 10
  db "br %3 b2 b3", 10
  db "b2:", 10
  db "%4 = iconst u64 1", 10
  db "%5 = addw u64 %1 %4", 10
  db "jmp b1", 10
  db "b3:", 10
  db "ret %1", 10
  db "}", 10
  .len = $ - lsexp
