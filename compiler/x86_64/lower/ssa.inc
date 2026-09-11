; compiler/x86_64/lower/ssa.inc
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
; Braun et al. (CC 2013) "Simple and Efficient Construction of Static Single
; Assignment Form", mapped onto `rt/` exactly as docs/design/lowering.md
; section 2.3's table does: `currentDef` is one `rt/map.inc` map keyed on the
; 8 bytes (var id, block id); `sealedBlocks` is `BfaBlock.flags`'s SEALED bit;
; `incompletePhis` is a scratch Vec of (block, var, phi) records; there are no
; use lists and Braun's recursion into users is replaced by the deferred
; index-order sweep that section argues for.
;
; This file knows nothing about an `AstNode`. It takes var ids and block ids
; and calls `backend_fasmg/ir.inc`'s builders, which is what makes
; tests/unit/lwr_ssa.asm -- a scripted sequence of writes, reads and seals
; with no tree at all -- possible.
;
; ---------------------------------------------------------------------------
; THE INCLUDE CHAIN, AND THE TWO DEFECTS IN OTHER TREES IT HAD TO WORK AROUND.
; Read this before moving an `include` line.
;
; This pass sits between `checker/` and `backend_fasmg/`, so it is the first
; module in the repository that needs BOTH the `ast/` records and the `bfa_*`
; builders in one program. Neither combination assembles today:
;
;  D1. `backend_fasmg/ir.inc` line 69 is `include '../rt/intern.inc'`. Every
;      other module ABOVE rt/ -- `ast/`, `checker/`, `driver/` -- includes no
;      rt/ file at all and relies on the consumer having brought rt/ in first
;      (compiler/x86_64/exsc.asm, note 3: "ast/, checker/ and driver/ include
;      none of those and rely on cst/ having run first"). `backend_fasmg/` is
;      the exception, and because fasmg has one flat namespace and no include
;      guards, the SECOND path to rt/ fails -- measured, in
;      `struct Arena`'s own `end struct`:
;          Error: symbol 'sizeof?.Arena' is undefined or out of scope.
;      So `cst/cst.inc` (which brings diag/ -> rt/span.inc -> rt/intern.inc)
;      and any `backend_fasmg/` file cannot appear in one program. That is
;      why this pass CANNOT be driven from a real lexed/parsed/checked tree
;      and why its fixtures build their typed trees through `ast/build.inc`
;      directly. The request is in the report: guard rt/'s files, or drop
;      ir.inc's include and let the consumer bring rt/ as every other module
;      above rt/ already does.
;
;      The consequence for this file: it includes `backend_fasmg/verify.inc`
;      (the whole backend chain, and rt/ through it) and then `ast/build.inc`
;      -- and must supply `struct Span` itself, because `rt/span.inc` is the
;      one rt/ file the backend chain does NOT reach and including it would
;      be that fatal second path to `rt/intern.inc`. The definition below is
;      byte-identical to rt/span.inc's and is checked against `sizeof.Span`
;      12 immediately after it.
;
;  D2. `backend_fasmg/parse.inc` declares `slot al, dd` (lines 1876 and 3787).
;      `macros/proc.inc`'s `slot` expands to `slotname = rbp - N`, an ordinary
;      fasmg assignment in the one flat namespace -- so after that file is
;      included, the register name `al` is a stack offset and every LATER file
;      that writes `mov [x], al` fails with
;          Error: variable term used where not expected.
;      which names neither `al` nor parse.inc. `ast/types.inc` is the first
;      casualty. docs/asm-conventions.md section 4.1 warns about exactly this
;      for instruction mnemonics ("an argument named `cmp` assembles as the
;      real `CMP`"); a REGISTER name is the same trap and is not on that
;      section's burned list. The two `restore al` below undo it. The request
;      -- rename those two slots -- is in the report.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md section 9.1 (Stage 3 is Braun et
; al.), section 9.3 (every order below is a function of the walk).
; Design: docs/design/lowering.md sections 2.1-2.3, docs/design/ssa-ir.md
; section 2.5.
; -----------------------------------------------------------------------------

; The TOP of backend_fasmg/'s chain, not the middle: program.inc -> emit.inc
; -> verify.inc -> print.inc -> parse.inc -> ir.inc is one strict chain, so a
; consumer that includes lower/ has the whole backend -- the verifier this
; file calls, and the emitter the driver calls -- and must not include any of
; it a second time.
include '../backend_fasmg/program.inc'

; ---- D2, fixed upstream; the tripwire stays -------------------------------
; parse.inc's two `slot al, dd` are now `slot bfa_al, dd`, so nothing burns
; the register name and the two `restore al` this file used to carry are gone.
; The check stays: if any file below ever burns `al` again, `mov al, 1` stops
; assembling to the two bytes B0 01 and this file fails at the line that is
; wrong, instead of somewhere in ast/.
virtual at 0
	mov	al, 1
	assert $ = 2
end virtual

; ---- D1, fixed upstream --------------------------------------------------
; This file used to restate rt/span.inc's `struct Span`, because
; backend_fasmg/ir.inc included rt/intern.inc and a second path through that
; chain could not be taken. ir.inc no longer includes rt/ at all -- the
; consumer brings it, as it does for ast/, checker/, cst/, diag/, driver/ and
; lexer/ -- so `struct Span` is rt/span.inc's again, and restating it here
; would now be the redefinition rather than the workaround.
assert sizeof.Span = 12

; NO ast/ INCLUDE, for the reason D1 above gives about rt/: ast/ast.inc's
; chain reaches build.inc, so a consumer holding both ast/ and lower/ would
; process it twice and fail on the second. The consumer brings ast/ BEFORE
; lower/ -- exsc.asm's order, and every lwr_ fixture's.

; ============================================================================
; the state record
; ============================================================================

; What kind of storage a binding got (docs/design/lowering.md section 2.2).
LWR_VK_NONE = 0		; not a binding this function can see
LWR_VK_SSA  = 1		; scalar, not memory-resident: a Braun variable
LWR_VK_SLOT = 2		; `%s = slot n a`; `vval` is that value id
LWR_VK_AGGP = 3		; an aggregate parameter: `vval` is its `param` value

struct LwrVar
	field vkind, db
	field vpad,  db
	field vpd2,  dw
	field vty,   dd		; IR type id -- the phi type for an SSA var
	field vval,  dd
end struct

; One Braun "incomplete phi": a phi created by a read of an unsealed block.
struct LwrInc
	field iblk,  dd
	field ivar,  dd
	field iphi,  dd
	field idone, dd		; 1 once sealBlock has filled it
end struct

; A lexical scope. `fcapb` is the base index of this frame's ELEVEN capability
; slots in `LwrState.caps` (spec section 4.6's atom count); `frelb`/`freln`
; delimit its release list in `LwrState.rels`, in initialisation order.
LWR_CAP_N = 11
struct LwrFrame
	field fcapb, dd
	field frelb, dd
	field freln, dd
	field fpad,  dd
end struct

struct LwrLoop
	field lexit, dd		; `rumpe` target
	field lcont, dd		; `perge` target -- the header for `dum`, the
				; latch for `per`
	field ldep,  dd		; scopes.len at loop entry
	field lpad,  dd
end struct

struct LwrState
	field tree,  dq		; -> Ast
	field mdl,   dq		; -> BfaModule
	field func,  dq		; -> BfaFunc (the one being built), 0 between
	field iar,   dq		; the IR arena (module lifetime)
	field scrar, dq		; the scratch arena (reset per function)
	field defs,  dq		; -> Map, key (var,block) 8 bytes -> value id
	field incs,  dq		; -> Vec<LwrInc>
	field vars,  dq		; -> Vec<LwrVar>, indexed by var id (1-based)
	field scps,  dq		; -> Vec<LwrFrame>
	field lps,   dq		; -> Vec<LwrLoop>
	field caps,  dq		; -> Vec<dd>, 11 per frame
	field rels,  dq		; -> Vec<dd>, decl ids in initialisation order
	field tmpv,  dq		; -> Vec<dd>, phi-operand staging (stack
				;    discipline: record len, push, truncate)
	field cur,   dd		; current block id; 0 = DEAD
	field nbase, dd		; decls.len -- synthetic var ids start above it
	field synth, dd		; synthetic vars handed out in this function
	field tptr,  dd		; cached IR type ids
	field tu1,   dd
	field tu64,  dd
	field tu8,   dd
	field nswept, dd	; phis the deferred sweep removed (not the
				;    on-the-fly check) -- lowering.md 2.3's
				;    [UNTESTED] claim, made measurable
	field hidret, dd	; the hidden-return `param` value, 0 if none
	field fnd,   dd		; the function's own declaration id
	field nm0,   dd		; interned `numeri` words
	field nm1,   dd
	field nm2,   dd
	field nm3,   dd
	field nglob, dd		; globals emitted so far (ids are dense from 0)
	field signa, dd		; the running parameter count while a signature is
				;    being built (see lower.inc's __lwr_sig_put)
	field pref,  dq		; -> Vec<dd>, one entry per prelude/interface.inc row:
				;    0 unreferenced, 1 referenced, >= 2 the
				;    1-based Module.funcs index plus one
	field fidx,  dq		; -> Vec<dd>, declaration id -> 1-based
				;    Module.funcs index, 0 for anything that is
				;    not a function of this module
end struct

; ============================================================================
; initialisation
; ============================================================================

; lwr_state_init(st, tree, lwrmod, lwriar, lwrsar) -- module-lifetime fields
; only. Per-function state is `lwr_fn_begin`'s.
proc lwr_state_init, lwrsp, lwrtre, lwrmod, lwriar, lwrsar
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	rcx, sizeof.LwrState
	xor	rax, rax
	cld
	rep	stosb
	mov	rbx, [lwrsp]
	mov	rax, [lwrtre]
	mov	[rbx + LwrState.tree], rax
	mov	rax, [lwrmod]
	mov	[rbx + LwrState.mdl], rax
	mov	rax, [lwriar]
	mov	[rbx + LwrState.iar], rax
	mov	rax, [lwrsar]
	mov	[rbx + LwrState.scrar], rax

	mov	rdi, [rbx + LwrState.mdl]
	call	bfa_ty_ptr
	mov	[rbx + LwrState.tptr], eax
	mov	rdi, [rbx + LwrState.mdl]
	call	bfa_ty_u1
	mov	[rbx + LwrState.tu1], eax
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, BFA_TK_U
	xor	rdx, rdx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	mov	[rbx + LwrState.tu64], eax
	mov	rdi, [rbx + LwrState.mdl]
	mov	esi, BFA_TK_U
	xor	rdx, rdx
	mov	ecx, 8
	xor	r8, r8
	call	bfa_type_intern
	mov	[rbx + LwrState.tu8], eax
	return
endp

; __lwr_scratch_vec(st, stride, initial) -> rax = a fresh Vec on the scratch
; arena. Every per-function structure goes through here so the reset in
; `lwr_fn_begin` is the only place that has to know they exist.
proc __lwr_scratch_vec, lwrsp, lwrstr, lwrini
	uses	rbx
	locals
		slot lwrv, dq
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.scrar]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[lwrv], rax
	mov	rdi, rax
	mov	rsi, [rbx + LwrState.scrar]
	mov	rdx, [lwrstr]
	mov	rcx, [lwrini]
	call	vec_init
	mov	rax, [lwrv]
	return
endp

; lwr_fn_begin(st, lwrfn, lwrnv) -- start a function. `lwrfn` is the BfaFunc;
; `lwrnv` the number of variable slots to reserve (declaration count plus
; headroom for synthetics). The scratch arena is RESET first and every vec and
; the map are rebuilt on it -- `arena_reset` invalidates the Vec headers
; themselves, which is why they are allocated from the arena rather than held
; in this struct by value (ast/build.inc's header found the same thing).
proc lwr_fn_begin, lwrsp, lwrfn, lwrnv
	uses	rbx, r12
	locals
		slot lwrnb, dq
	endl
	mov	rbx, [lwrsp]
	mov	rax, [lwrfn]
	mov	[rbx + LwrState.func], rax
	mov	dword [rbx + LwrState.cur], 0
	mov	dword [rbx + LwrState.synth], 0
	mov	dword [rbx + LwrState.nswept], 0
	mov	dword [rbx + LwrState.hidret], 0

	mov	rdi, [rbx + LwrState.scrar]
	call	arena_reset

	; `defs` bucket count: the power of two at or above 2 x the variable
	; count, computed per function and never a constant -- lowering.md
	; section 4's own rule, and CHK section 5's lesson.
	mov	rax, [lwrnv]
	shl	rax, 1
	mov	r12, 16
  .bk:
	cmp	r12, rax
	jge	.bkdone
	shl	r12, 1
	cmp	r12, 0x100000
	jl	.bk
  .bkdone:
	mov	[lwrnb], r12
	mov	rdi, [rbx + LwrState.scrar]
	mov	rsi, sizeof.Map
	call	arena_alloc
	mov	[rbx + LwrState.defs], rax
	mov	rdi, rax
	mov	rsi, [rbx + LwrState.scrar]
	mov	rdx, [lwrnb]
	call	map_init

	mov	rdi, rbx
	mov	rsi, sizeof.LwrInc
	mov	rdx, 16
	call	__lwr_scratch_vec
	mov	[rbx + LwrState.incs], rax

	mov	rdi, rbx
	mov	rsi, sizeof.LwrVar
	mov	rdx, [lwrnv]
	call	__lwr_scratch_vec
	mov	[rbx + LwrState.vars], rax

	mov	rdi, rbx
	mov	rsi, sizeof.LwrFrame
	mov	rdx, 16
	call	__lwr_scratch_vec
	mov	[rbx + LwrState.scps], rax

	mov	rdi, rbx
	mov	rsi, sizeof.LwrLoop
	mov	rdx, 8
	call	__lwr_scratch_vec
	mov	[rbx + LwrState.lps], rax

	mov	rdi, rbx
	mov	rsi, 4
	mov	rdx, 64
	call	__lwr_scratch_vec
	mov	[rbx + LwrState.caps], rax

	mov	rdi, rbx
	mov	rsi, 4
	mov	rdx, 32
	call	__lwr_scratch_vec
	mov	[rbx + LwrState.rels], rax

	mov	rdi, rbx
	mov	rsi, 4
	mov	rdx, 32
	call	__lwr_scratch_vec
	mov	[rbx + LwrState.tmpv], rax

	; one zeroed LwrVar per reservable var id, so `vkind` reads as NONE
	; for anything the walk never declares
	xor	r12, r12
  .vloop:
	cmp	r12, [lwrnv]
	jae	.vdone
	mov	rdi, [rbx + LwrState.vars]
	call	vec_push
	mov	dword [rax + LwrVar.vkind], 0
	mov	dword [rax + LwrVar.vty], 0
	mov	dword [rax + LwrVar.vval], 0
	inc	r12
	jmp	.vloop
  .vdone:
	return
endp

; ============================================================================
; the variable table
; ============================================================================

; lwr_var_at(st, lwrvid) -> rax = the LwrVar record. Var ids are 1-based
; declaration ids; synthetics continue above `nbase` (lowering.md 2.2).
proc lwr_var_at, lwrsp, lwrvid
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rdi, [rdi + LwrState.vars]
	mov	rsi, [lwrvid]
	dec	rsi
	call	vec_get
	return
endp

; lwr_var_set(st, lwrvid, lwrk, lwrty, lwrval)
proc lwr_var_set, lwrsp, lwrvid, lwrk, lwrty, lwrval
	uses	rbx
	locals
	endl
	mov	rdi, [lwrsp]
	mov	rsi, [lwrvid]
	call	lwr_var_at
	mov	rbx, rax
	mov	eax, [lwrk]
	mov	[rbx + LwrVar.vkind], al
	mov	eax, [lwrty]
	mov	[rbx + LwrVar.vty], eax
	mov	eax, [lwrval]
	mov	[rbx + LwrVar.vval], eax
	return
endp

; lwr_synth_var(st, lwrty) -> eax = a fresh synthetic SSA variable id.
; `decls.len + k`, k counted per function in walk order (lowering.md 2.2), so
; it can never collide with a declaration id and is a function of the walk.
proc lwr_synth_var, lwrsp, lwrty
	uses	rbx
	locals
		slot lwrid, dd
	endl
	mov	rbx, [lwrsp]
	mov	eax, [rbx + LwrState.synth]
	inc	eax
	mov	[rbx + LwrState.synth], eax
	add	eax, [rbx + LwrState.nbase]
	mov	[lwrid], eax
	mov	rdi, rbx
	mov	esi, eax
	mov	edx, LWR_VK_SSA
	mov	ecx, [lwrty]
	xor	r8, r8
	call	lwr_var_set
	mov	eax, [lwrid]
	return
endp

; ============================================================================
; Braun's six procedures (docs/design/lowering.md section 2.3's table)
; ============================================================================

; lwr_resolve(st, lwrval) -> eax = `lwrval` with every `nop` link followed to
; the value that replaced it. docs/design/ssa-ir.md section 2.1: "A removed
; instruction becomes `nop` with `a` = its replacement; reads follow the
; chain." Every operand this pass writes is resolved at the point it is
; written, so an instruction emitted AFTER a removal never names a `nop`; one
; emitted before may, and `lwr_canon` rewrites it.
proc lwr_resolve, lwrsp, lwrval
	uses	rbx, r12
	locals
		slot lwrn, dd
	endl
	mov	rbx, [lwrsp]
	mov	r12d, [lwrval]
	test	r12d, r12d
	jz	.done
	; a chain longer than the instruction count is a cycle, which no
	; construction here can build -- bounded so a corrupted chain traps
	; instead of hanging
	mov	rdi, [rbx + LwrState.func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	inc	rax
	mov	[lwrn], eax
  .loop:
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_NOP
	jne	.done
	mov	r12d, [rax + BfaInst.a]
	rassert	r12d ne 0		; a `nop` with no replacement is undef
	mov	eax, [lwrn]
	dec	eax
	mov	[lwrn], eax
	rassert	eax gt 0
	jmp	.loop
  .done:
	mov	eax, r12d
	return
endp

; lwr_write_var(st, lwrv, lwrb, lwrval) -- Braun's `writeVariable`.
; `rt/map.inc` has no update: a second write to the same key APPENDS and
; shadows, and `map_get` finds the newest (that file's own contract). The key
; bytes are copied into the scratch arena first, because `map_insert` borrows
; its key and a stack buffer would be gone by the next call.
proc lwr_write_var, lwrsp, lwrv, lwrb, lwrval
	uses	rbx, r12
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.scrar]
	mov	rsi, 8
	call	arena_alloc
	mov	r12, rax
	mov	eax, [lwrv]
	mov	[r12], eax
	mov	eax, [lwrb]
	mov	[r12 + 4], eax
	mov	rdi, [rbx + LwrState.defs]
	mov	rsi, r12
	mov	rdx, 8
	mov	rcx, [lwrval]
	call	map_insert
	return
endp

; lwr_read_var(st, lwrv, lwrb) -> eax = Braun's `readVariable`, chain-followed.
proc lwr_read_var, lwrsp, lwrv, lwrb
	uses	rbx
	locals
		slot lwrk, 8
	endl
	mov	rbx, [lwrsp]
	mov	eax, [lwrv]
	mov	[lwrk], eax
	mov	eax, [lwrb]
	mov	[lwrk + 4], eax
	mov	rdi, [rbx + LwrState.defs]
	lea	rsi, [lwrk]
	mov	rdx, 8
	call	map_get
	test	rax, rax
	jz	.miss
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_resolve
	return
  .miss:
	mov	rdi, rbx
	mov	esi, [lwrv]
	mov	edx, [lwrb]
	call	__lwr_read_rec
	return
endp

; __lwr_new_phi(st, lwrb, lwrty) -> eax = an operandless `phi` PREPENDED to
; block `lwrb`'s phi list. `a`/`b` stay 0 until `__lwr_add_phi_ops` writes the
; one `extra` range; `bfa_block_prepend_phi` is the O(1) operation
; docs/design/ssa-ir.md section 2.1 describes and the reason
; `bfa_block_link_phi` (which appends) is not used here.
proc __lwr_new_phi, lwrsp, lwrb, lwrty
	uses	rbx
	locals
		slot lwrp, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.func]
	mov	esi, BFA_OP_PHI
	xor	rdx, rdx
	mov	ecx, [lwrty]
	xor	r8, r8
	xor	r9, r9
	call	bfa_inst_push
	mov	[lwrp], eax
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrb]
	mov	edx, [lwrp]
	call	bfa_block_prepend_phi
	mov	eax, [lwrp]
	return
endp

; __lwr_read_rec(st, lwrv, lwrb) -> eax -- Braun's `readVariableRecursive`,
; his Algorithm 2 unchanged: an unsealed block gets an operandless phi and an
; `incomplete` record; a sealed block with exactly one predecessor forwards;
; anything else gets a phi that is written to `defs` BEFORE its operands are
; filled, so a cycle through this block reads the phi instead of recursing
; forever.
proc __lwr_read_rec, lwrsp, lwrv, lwrb
	uses	rbx, r12
	locals
		slot lwrval, dd
		slot lwrnp, dd
		slot lwrp, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	mov	esi, [lwrv]
	call	lwr_var_at
	mov	r12d, [rax + LwrVar.vty]

	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrb]
	call	bfa_block_ptr
	test	dword [rax + BfaBlock.flags], BFA_BLOCK_FLAG_SEALED
	jnz	.sealed

	mov	rdi, rbx
	mov	esi, [lwrb]
	mov	edx, r12d
	call	__lwr_new_phi
	mov	[lwrval], eax
	mov	rdi, [rbx + LwrState.incs]
	call	vec_push
	mov	ecx, [lwrb]
	mov	[rax + LwrInc.iblk], ecx
	mov	ecx, [lwrv]
	mov	[rax + LwrInc.ivar], ecx
	mov	ecx, [lwrval]
	mov	[rax + LwrInc.iphi], ecx
	mov	dword [rax + LwrInc.idone], 0
	jmp	.finish

  .sealed:
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrb]
	call	bfa_edge_count
	mov	[lwrnp], eax
	cmp	eax, 1
	jne	.many
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrb]
	xor	rdx, rdx
	call	bfa_edge_nth
	mov	rdi, rbx
	mov	esi, [lwrv]
	mov	edx, eax
	call	lwr_read_var
	mov	[lwrval], eax
	jmp	.finish

  .many:
	; zero predecessors on a sealed block that is not the entry is a read
	; of a variable on an unreachable path -- a construction bug here, not
	; a program error (lowering.md constraint 4)
	rassert	eax ge 2
	mov	rdi, rbx
	mov	esi, [lwrb]
	mov	edx, r12d
	call	__lwr_new_phi
	mov	[lwrp], eax
	mov	rdi, rbx
	mov	esi, [lwrv]
	mov	edx, [lwrb]
	mov	ecx, [lwrp]
	call	lwr_write_var
	mov	rdi, rbx
	mov	esi, [lwrv]
	mov	edx, [lwrp]
	mov	ecx, [lwrb]
	call	__lwr_add_phi_ops
	mov	[lwrval], eax

  .finish:
	mov	rdi, rbx
	mov	esi, [lwrv]
	mov	edx, [lwrb]
	mov	ecx, [lwrval]
	call	lwr_write_var
	mov	eax, [lwrval]
	return
endp

; __lwr_add_phi_ops(st, lwrv, lwrphi, lwrb) -> eax = the phi, or the value that
; replaced it if the on-the-fly triviality check removed it.
;
; TWO PASSES, NOT ONE, and the reason is the arena: reading a predecessor can
; itself create phis and push into `func.extra`, so appending each operand as
; it is read would interleave two instructions' ranges. The operands are
; staged in `LwrState.tmpv` (stack discipline: remember its length, push,
; truncate) and then appended as one contiguous run, which is exactly
; docs/design/ssa-ir.md section 2.5's "collect into a scratch vec, then append
; once to extra".
;
; The edge list is walked in ORDER (`BfaBlock.first_pred` -> `nextp`), which
; `bfa_edge_push` built in the order terminators were emitted. That is
; predecessor order (IR 2.4) and therefore phi operand order, and it is what
; `verify.inc` rule 3 now reads.
proc __lwr_add_phi_ops, lwrsp, lwrv, lwrphi, lwrb
	uses	rbx, r12, r13
	locals
		slot lwrbase, dq
		slot lwre, dd
		slot lwrpb, dd
		slot lwrn, dd
		slot lwrfirst, dd
		slot lwri, dq
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [rdi + Vec.len]
	mov	[lwrbase], rax
	mov	dword [lwrn], 0

	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrb]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_pred]
	mov	[lwre], ecx
  .eloop:
	cmp	dword [lwre], 0
	je	.edone
	mov	rdi, [rbx + LwrState.func]
	mov	rdi, [rdi + BfaFunc.edges]
	mov	esi, [lwre]
	dec	esi
	call	vec_get
	mov	ecx, [rax + BfaEdge.pred]
	mov	[lwrpb], ecx
	mov	ecx, [rax + BfaEdge.nextp]
	mov	[lwre], ecx

	mov	rdi, rbx
	mov	esi, [lwrv]
	mov	edx, [lwrpb]
	call	lwr_read_var
	mov	r13d, eax
	mov	rdi, [rbx + LwrState.tmpv]
	call	vec_push
	mov	ecx, [lwrpb]
	mov	[rax], ecx
	mov	rdi, [rbx + LwrState.tmpv]
	call	vec_push
	mov	[rax], r13d
	mov	eax, [lwrn]
	inc	eax
	mov	[lwrn], eax
	jmp	.eloop
  .edone:

	; one contiguous run of 2 x npreds words
	mov	dword [lwrfirst], 0
	mov	rax, [lwrbase]
	mov	[lwri], rax
  .ploop:
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [rdi + Vec.len]
	cmp	[lwri], rax
	jae	.pdone
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rsi, [lwri]
	call	vec_get
	mov	r12d, [rax]
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	call	bfa_extra_push
	cmp	dword [lwrfirst], 0
	jne	.notfirst
	inc	eax			; store base+1 so 0 stays "unset"
	mov	[lwrfirst], eax
	dec	eax
  .notfirst:
	mov	rax, [lwri]
	inc	rax
	mov	[lwri], rax
	jmp	.ploop
  .pdone:
	mov	rdi, [rbx + LwrState.tmpv]
	mov	rax, [lwrbase]
	mov	[rdi + Vec.len], rax

	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrphi]
	call	bfa_inst_ptr
	mov	ecx, [lwrfirst]
	test	ecx, ecx
	jz	.noops
	dec	ecx
  .noops:
	mov	[rax + BfaInst.a], ecx
	mov	ecx, [lwrn]
	shl	ecx, 1			; `b` is the WORD count, not the pair
					; count (parse.inc's __bfa_ps_phi adds
					; 2 per pair)
	mov	[rax + BfaInst.b], ecx

	mov	rdi, rbx
	mov	esi, [lwrphi]
	call	__lwr_try_trivial
	return
endp

; __lwr_try_trivial(st, lwrphi) -> eax = the phi if it is not trivial, or the
; single value that replaces it. Braun's `tryRemoveTrivialPhi` WITHOUT the
; recursion into users: there are no use lists here, and lowering.md section
; 2.3's argument is that an index-order sweep to a fixpoint reaches the same
; result. `lwr_sweep` runs that sweep; this is the on-the-fly local check that
; removes the common case immediately.
;
; A phi whose only operand is itself is Braun's *undef*: a read of a binding on
; a path that never wrote it. That is a definite-assignment failure the checker
; owns (spec section 13 has no code for it -- lowering.md finding 7), so it is
; an `rassert` here and never a diagnostic.
proc __lwr_try_trivial, lwrsp, lwrphi
	uses	rbx, r12, r13
	locals
		slot lwrsame, dd
		slot lwri, dd
		slot lwrbse, dd
		slot lwrcnt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrphi]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_PHI
	jne	.notphi
	mov	ecx, [rax + BfaInst.a]
	mov	[lwrbse], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[lwrcnt], ecx

	mov	dword [lwrsame], 0
	mov	dword [lwri], 1		; the odd words are the values
  .loop:
	mov	eax, [lwri]
	cmp	eax, [lwrcnt]
	jge	.done
	mov	rdi, [rbx + LwrState.func]
	mov	eax, [lwrbse]
	add	eax, [lwri]
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, rbx
	mov	esi, eax
	call	lwr_resolve
	mov	r12d, eax
	cmp	r12d, [lwrphi]
	je	.next
	cmp	r12d, [lwrsame]
	je	.next
	cmp	dword [lwrsame], 0
	jne	.notrivial
	mov	[lwrsame], r12d
  .next:
	mov	eax, [lwri]
	add	eax, 2
	mov	[lwri], eax
	jmp	.loop
  .done:
	mov	eax, [lwrsame]
	rassert	eax ne 0		; Braun's undef -- see this proc's header
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrphi]
	call	bfa_inst_ptr
	mov	word [rax + BfaInst.op], BFA_OP_NOP
	mov	ecx, [lwrsame]
	mov	[rax + BfaInst.a], ecx
	mov	dword [rax + BfaInst.b], 0
	mov	eax, [lwrsame]
	return
  .notrivial:
	mov	eax, [lwrphi]
	return
  .notphi:
	; already removed by an earlier pass of the sweep
	mov	rdi, rbx
	mov	esi, [lwrphi]
	call	lwr_resolve
	return
endp

; lwr_seal(st, lwrb) -- Braun's `sealBlock`. Fills every incomplete phi
; recorded for this block, IN RECORD ORDER (which is creation order, which is
; walk order), then sets the SEALED bit. Sealing last is Braun's own order:
; the phi is already in `defs`, so a read that comes back round to this block
; while its operands are being filled finds the phi rather than making a
; second one.
;
; A SCAN, NOT A THREADED LIST. lowering.md section 2.3 writes "records are
; threaded by `next` from a per-block head, so this is a list walk, not a
; scan". This is a scan of the `incomplete` vec, whose length is bounded by
; (variables x blocks unsealed at once) and is a handful in every function
; this pass has lowered. The head-per-block would need a field on `BfaBlock`
; that ir.inc does not have, or a parallel vec indexed by block; recorded as a
; deviation rather than taken silently.
proc lwr_seal, lwrsp, lwrb
	uses	rbx, r12
	locals
		slot lwri, dq
		slot lwrph, dd
		slot lwrvr, dd
	endl
	mov	rbx, [lwrsp]
	mov	qword [lwri], 0
  .loop:
	mov	rdi, [rbx + LwrState.incs]
	mov	rax, [rdi + Vec.len]
	cmp	[lwri], rax
	jae	.done
	mov	rdi, [rbx + LwrState.incs]
	mov	rsi, [lwri]
	call	vec_get
	mov	ecx, [rax + LwrInc.iblk]
	cmp	ecx, [lwrb]
	jne	.next
	cmp	dword [rax + LwrInc.idone], 0
	jne	.next
	mov	dword [rax + LwrInc.idone], 1
	mov	ecx, [rax + LwrInc.iphi]
	mov	[lwrph], ecx
	mov	ecx, [rax + LwrInc.ivar]
	mov	[lwrvr], ecx
	mov	rdi, rbx
	mov	esi, [lwrvr]
	mov	edx, [lwrph]
	mov	ecx, [lwrb]
	call	__lwr_add_phi_ops
	; the variable's definition in this block is whatever survived
	mov	rdi, rbx
	mov	esi, [lwrvr]
	mov	edx, [lwrb]
	mov	ecx, eax
	call	lwr_write_var
  .next:
	mov	rax, [lwri]
	inc	rax
	mov	[lwri], rax
	jmp	.loop
  .done:
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrb]
	call	bfa_block_ptr
	or	dword [rax + BfaBlock.flags], BFA_BLOCK_FLAG_SEALED
	return
endp

; ============================================================================
; the deferred sweep, canonicalisation, compaction
; ============================================================================

; lwr_sweep(st) -> eax = how many phis this sweep removed.
;
; Braun recurses into a removed phi's USERS to re-test them. There are no use
; lists here (one `{user, next}` record per operand of every instruction, in a
; pool that cannot be freed before the function resets, plus a head field the
; 32-byte `Inst` has nowhere to put). docs/design/lowering.md section 2.3
; argues the replacement is equivalent: removing a trivial phi can only ever
; SHRINK another phi's chain-resolved operand set, never grow it, so "remove
; trivial phis until none remain" has one fixed point whatever the visiting
; order. That argument is an argument; the count this proc returns is what
; tests/unit/lwr_ssa.asm asserts on, so the claim is measured on a real nested
; loop rather than assumed.
proc lwr_sweep, lwrsp
	uses	rbx, r12, r13
	locals
		slot lwrn, dd
		slot lwrch, dd
		slot lwrtot, dd
	endl
	mov	rbx, [lwrsp]
	mov	dword [lwrtot], 0
  .round:
	mov	dword [lwrch], 0
	mov	rdi, [rbx + LwrState.func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	mov	[lwrn], eax
	mov	r12d, 1
  .scan:
	cmp	r12d, [lwrn]
	ja	.scandone
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_PHI
	jne	.nextscan
	mov	rdi, rbx
	mov	esi, r12d
	call	__lwr_try_trivial
	cmp	eax, r12d
	je	.nextscan
	mov	eax, [lwrch]
	inc	eax
	mov	[lwrch], eax
	mov	eax, [lwrtot]
	inc	eax
	mov	[lwrtot], eax
  .nextscan:
	inc	r12d
	jmp	.scan
  .scandone:
	cmp	dword [lwrch], 0
	jne	.round
	mov	eax, [lwrtot]
	mov	[rbx + LwrState.nswept], eax
	return
endp

; lwr_canon(st) -- rewrite every VALUE operand of every instruction to its
; chain-resolved value, through ir.inc's exported operand iterator
; (`bfa_inst_operand_next`/`_set`), so that this pass does not carry a second
; copy of print.inc's per-shape table. Only slots the iterator reports as
; holding a value id are touched: a `jmp`'s block id, a `gaddr`'s global id,
; a `phi`'s predecessor block ids and a `slot`'s size are not operands and are
; not rewritten.
proc lwr_canon, lwrsp
	uses	rbx, r12, r13
	locals
		slot lwrn, dd
		slot lwrcur, dd
		slot lwrslot, dd
		slot lwrval, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	mov	[lwrn], eax
	mov	r12d, 1
  .inst:
	cmp	r12d, [lwrn]
	ja	.done
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_NOP
	je	.nextinst
	mov	dword [lwrcur], 1
  .opd:
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	mov	edx, [lwrcur]
	call	bfa_inst_operand_next
	test	eax, eax
	jz	.nextinst
	mov	[lwrcur], eax
	mov	[lwrslot], ecx
	mov	[lwrval], edx
	mov	rdi, rbx
	mov	esi, [lwrval]
	call	lwr_resolve
	cmp	eax, [lwrval]
	je	.opd
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	mov	edx, [lwrslot]
	mov	ecx, eax
	call	bfa_inst_operand_set
	jmp	.opd
  .nextinst:
	inc	r12d
	jmp	.inst
  .done:
	return
endp

; lwr_compact(st) -- delete every `nop` record from `func.insts` and renumber
; densely, 1-based, in the surviving records' existing order.
;
; WHY IT IS NOT OPTIONAL. `verify.inc`'s rule 9 is a raw scan of the whole
; `insts` Vec ("no `nop`"), so an unlinked record would still fail it, and
; `emit.inc` gives every internal value id its own 8-byte stack slot, so a
; dead record would cost frame bytes in the emitted program. The renumbering
; is a pure function of the pre-compaction ids, so docs/design/lowering.md
; section 2.10's determinism table survives it.
;
; FIVE PHASES, IN THIS ORDER, and the order is the whole difficulty: the
; block lists are threaded through `BfaInst.nextp` of records that are about
; to MOVE, so the lists are snapshotted as new ids before anything moves and
; rebuilt from the snapshot afterwards.
;
;   A  old id -> new id (0 for a removed record)
;   B  snapshot each block's phi list and body list as runs of NEW ids
;   C  rewrite every value operand through the table
;   D  move the surviving records down
;   E  rebuild both lists of every block from the snapshot
;
; Runs AFTER `lwr_canon`, so no surviving operand names a `nop` and phase C is
; a table lookup rather than a second chain walk.
proc lwr_compact, lwrsp
	uses	rbx, r12, r13
	locals
		slot lwrn, dd
		slot lwrnb, dd
		slot lwrmap, dq
		slot lwrsnap, dq	; -> Vec<dd>, the runs
		slot lwrruns, dq	; -> 4 dd per block: base, count x2
		slot lwrnew, dd
		slot lwrcur, dd
		slot lwrslot, dd
		slot lwrval, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [rbx + LwrState.func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	mov	[lwrn], eax
	mov	rdi, [rbx + LwrState.func]
	mov	rdi, [rdi + BfaFunc.blocks]
	mov	rax, [rdi + Vec.len]
	mov	[lwrnb], eax

	; ---- A ----
	mov	rdi, [rbx + LwrState.scrar]
	mov	esi, [lwrn]
	add	esi, 2
	shl	rsi, 2
	call	arena_alloc
	mov	[lwrmap], rax
	xor	r12d, r12d
	mov	dword [lwrnew], 0
  .assign:
	inc	r12d
	cmp	r12d, [lwrn]
	ja	.adone
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	rdx, [lwrmap]
	cmp	ecx, BFA_OP_NOP
	je	.anop
	mov	eax, [lwrnew]
	inc	eax
	mov	[lwrnew], eax
	mov	[rdx + r12*4], eax
	jmp	.assign
  .anop:
	mov	dword [rdx + r12*4], 0
	jmp	.assign
  .adone:

	; ---- B ----
	mov	rdi, rbx
	mov	rsi, 4
	mov	edx, [lwrn]
	inc	edx
	call	__lwr_scratch_vec
	mov	[lwrsnap], rax
	mov	rdi, [rbx + LwrState.scrar]
	mov	esi, [lwrnb]
	inc	esi
	shl	rsi, 4			; 4 dd per block
	call	arena_alloc
	mov	[lwrruns], rax
	mov	r12d, 1
  .snapblk:
	cmp	r12d, [lwrnb]
	ja	.snapdone
	mov	rdi, rbx
	mov	esi, r12d
	xor	rdx, rdx
	mov	rcx, [lwrmap]
	mov	r8, [lwrsnap]
	call	__lwr_snap_list
	mov	rcx, [lwrruns]
	lea	rcx, [rcx + r12*8]
	lea	rcx, [rcx + r12*8]
	mov	[rcx], eax		; body base
	mov	[rcx + 4], edx		; body count
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, 1
	mov	rcx, [lwrmap]
	mov	r8, [lwrsnap]
	call	__lwr_snap_list
	mov	rcx, [lwrruns]
	lea	rcx, [rcx + r12*8]
	lea	rcx, [rcx + r12*8]
	mov	[rcx + 8], eax		; phi base
	mov	[rcx + 12], edx		; phi count
	inc	r12d
	jmp	.snapblk
  .snapdone:

	; ---- C ----
	mov	r12d, 1
  .rw:
	cmp	r12d, [lwrn]
	ja	.rwdone
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_NOP
	je	.rwnext
	mov	dword [lwrcur], 1
  .rwopd:
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	mov	edx, [lwrcur]
	call	bfa_inst_operand_next
	test	eax, eax
	jz	.rwnext
	mov	[lwrcur], eax
	mov	[lwrslot], ecx
	mov	[lwrval], edx
	mov	rcx, [lwrmap]
	mov	eax, [lwrval]
	mov	eax, [rcx + rax*4]
	rassert	eax ne 0		; an operand still naming a removed
					; record means lwr_canon did not run
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	mov	edx, [lwrslot]
	mov	ecx, eax
	call	bfa_inst_operand_set
	jmp	.rwopd
  .rwnext:
	inc	r12d
	jmp	.rw
  .rwdone:

	; ---- D ----
	mov	r12d, 1
  .mv:
	cmp	r12d, [lwrn]
	ja	.mvdone
	mov	rcx, [lwrmap]
	mov	r13d, [rcx + r12*4]
	test	r13d, r13d
	jz	.mvnext
	cmp	r13d, r12d
	je	.mvnext
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	call	bfa_inst_ptr
	push	rax
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r13d
	call	bfa_inst_ptr
	pop	rsi
	mov	rdi, rax
	mov	rcx, sizeof.BfaInst
	cld
	rep	movsb
  .mvnext:
	inc	r12d
	jmp	.mv
  .mvdone:
	mov	rdi, [rbx + LwrState.func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	eax, [lwrnew]
	mov	[rdi + Vec.len], rax

	; ---- E ----
	mov	r12d, 1
  .relink:
	cmp	r12d, [lwrnb]
	ja	.done
	mov	rcx, [lwrruns]
	lea	rcx, [rcx + r12*8]
	lea	rcx, [rcx + r12*8]
	mov	rdi, rbx
	mov	esi, r12d
	xor	rdx, rdx
	mov	r8d, [rcx]
	mov	r9d, [rcx + 4]
	mov	rcx, [lwrsnap]
	call	__lwr_relink_list
	mov	rcx, [lwrruns]
	lea	rcx, [rcx + r12*8]
	lea	rcx, [rcx + r12*8]
	mov	rdi, rbx
	mov	esi, r12d
	mov	edx, 1
	mov	r8d, [rcx + 8]
	mov	r9d, [rcx + 12]
	mov	rcx, [lwrsnap]
	call	__lwr_relink_list
	inc	r12d
	jmp	.relink
  .done:
	return
endp

; __lwr_snap_list(st, lwrb, lwrphil, lwrmp, lwrsn) -> eax = base index in
; `lwrsn`, edx = count. Walks one of block `lwrb`'s two lists in its current
; (pre-move) form and appends the NEW id of every surviving member.
; `lwrphil` = 0 body, 1 phi.
proc __lwr_snap_list, lwrsp, lwrb, lwrphil, lwrmp, lwrsn
	uses	rbx, r12, r13
	locals
		slot lwrcur, dd
		slot lwrbse, dd
		slot lwrcnt, dd
	endl
	mov	rbx, [lwrsp]
	mov	rdi, [lwrsn]
	mov	rax, [rdi + Vec.len]
	mov	[lwrbse], eax
	mov	dword [lwrcnt], 0
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrb]
	call	bfa_block_ptr
	cmp	dword [lwrphil], 0
	je	.body
	mov	ecx, [rax + BfaBlock.first_phi]
	jmp	.head
  .body:
	mov	ecx, [rax + BfaBlock.first]
  .head:
	mov	[lwrcur], ecx
  .loop:
	cmp	dword [lwrcur], 0
	je	.done
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrcur]
	call	bfa_inst_ptr
	mov	r12d, [rax + BfaInst.nextp]
	mov	rcx, [lwrmp]
	mov	eax, [lwrcur]
	mov	r13d, [rcx + rax*4]
	test	r13d, r13d
	jz	.skip
	mov	rdi, [lwrsn]
	call	vec_push
	mov	[rax], r13d
	mov	eax, [lwrcnt]
	inc	eax
	mov	[lwrcnt], eax
  .skip:
	mov	[lwrcur], r12d
	jmp	.loop
  .done:
	mov	eax, [lwrbse]
	mov	edx, [lwrcnt]
	return
endp

; __lwr_relink_list(st, lwrb, lwrphil, lwrsn, lwrbse, lwrcn) -- write one of
; block `lwrb`'s two lists back from the snapshot run [lwrbse, lwrbse+lwrcn).
proc __lwr_relink_list, lwrsp, lwrb, lwrphil, lwrsn, lwrbse, lwrcn
	uses	rbx, r12, r13
	locals
		slot lwri, dd
		slot lwrhd, dd
		slot lwrtl, dd
	endl
	mov	rbx, [lwrsp]
	mov	dword [lwrhd], 0
	mov	dword [lwrtl], 0
	mov	dword [lwri], 0
  .loop:
	mov	eax, [lwri]
	cmp	eax, [lwrcn]
	jge	.done
	mov	rdi, [lwrsn]
	mov	eax, [lwrbse]
	add	eax, [lwri]
	mov	esi, eax
	call	vec_get
	mov	r12d, [rax]
	cmp	dword [lwrhd], 0
	jne	.chain
	mov	[lwrhd], r12d
	jmp	.settail
  .chain:
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrtl]
	call	bfa_inst_ptr
	mov	[rax + BfaInst.nextp], r12d
  .settail:
	mov	[lwrtl], r12d
	mov	rdi, [rbx + LwrState.func]
	mov	esi, r12d
	call	bfa_inst_ptr
	mov	dword [rax + BfaInst.nextp], 0
	mov	eax, [lwri]
	inc	eax
	mov	[lwri], eax
	jmp	.loop
  .done:
	mov	rdi, [rbx + LwrState.func]
	mov	esi, [lwrb]
	call	bfa_block_ptr
	cmp	dword [lwrphil], 0
	je	.wbody
	mov	ecx, [lwrhd]
	mov	[rax + BfaBlock.first_phi], ecx
	mov	ecx, [lwrtl]
	mov	[rax + BfaBlock.last_phi], ecx
	return
  .wbody:
	mov	ecx, [lwrhd]
	mov	[rax + BfaBlock.first], ecx
	mov	ecx, [lwrtl]
	mov	[rax + BfaBlock.last], ecx
	return
endp

; lwr_fn_end(st) -- the three finishing passes, in the only order that works:
; sweep (may remove more phis), canonicalise (no operand names a `nop`),
; compact (no `nop` record survives). After this the function satisfies
; verifier rule 9 and every id is dense and 1-based, which is what
; `emit.inc`'s frame arithmetic assumes.
proc lwr_fn_end, lwrsp
	uses	rbx
	locals
	endl
	mov	rbx, [lwrsp]
	mov	rdi, rbx
	call	lwr_sweep
	mov	rdi, rbx
	call	lwr_canon
	mov	rdi, rbx
	call	lwr_compact
	return
endp
