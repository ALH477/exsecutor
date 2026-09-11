; compiler/x86_64/backend_fasmg/verify.inc
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
; The IR verifier: docs/design/ssa-ir.md section 3. Runs on complete IR after
; construction and before either backend. A failure here is malformed IR --
; a COMPILER bug, never a source-level `EXS-E` diagnostic (section 3's own
; words). CLAUDE.md, "Error codes are permanent": this file never invents or
; touches a code, and does not import diag/.
;
; RETURN A VERDICT, DO NOT `rassert` INSIDE THE WALK. `bfa_verify_func`
; returns a rule id (0 = ok) plus the offending block/instruction id in
; out-params, so a fixture can feed deliberately bad IR and observe exactly
; which rule fired. `bfa_verify_module` is the production entry point: it
; calls `bfa_verify_func` for every function and `rassert`s on the first
; nonzero verdict -- an internal contract violation, never `CF`/`eax`
; (docs/asm-conventions.md, "1.3 Error protocol"). Because `rassert`'s own
; condition is elided (tokens and all) under `RELEASE`
; (macros/assert.inc's header), a `RELEASE` build of `bfa_verify_module`
; still PERFORMS every `bfa_verify_func` call (they are ordinary statements,
; not inside the elided condition) but never traps on what they find --
; consistent with how `rassert` behaves everywhere else in this tree, not a
; decision specific to this file.
;
; RULE NUMBERING (BFA_VERIFY_R*, below) is this file's own invention --
; section 3 lists nine checks in prose, in this order, and the numbers here
; follow that order 1:1 so a fixture's expected verdict is legible against
; the spec text without a lookup table:
;   1 operand resolves, with the type its op requires
;   2 exactly one terminator per block, last
;   3 phis first, one operand per predecessor, in predecessor order
;   4 every use dominated by its definition
;   5 each `contrib` dominated by its `redinit`; each `redfin`
;     post-dominating every `contrib` of its handle
;   6 `retain`/`release` only on `ref`/`refc`
;   7 `red.F` only in its three ops (`redinit` produces it; `contrib`/
;     `redfin` read it as their handle operand -- nowhere else, as a
;     produced type OR as a used operand)
;   8 `numeri` equal across every direct `call`
;   9 no `nop`
; These are NOT `EXS-E` codes and must never be confused for one --
; CLAUDE.md's "never invent a code" is about spec section 13's registry,
; untouched by this file; these are this file's own internal verdict space.
;
; WHY A FIXTURE VIOLATING RULE K REPORTS EXACTLY K, NOT SOME EARLIER RULE:
; `bfa_verify_func` runs a fixed sequence of passes (see its own body,
; below) where later passes are able to TRUST invariants earlier passes
; already confirmed. Rule 1 (operand range/resolution) runs first, as its
; own complete whole-function pass, and nothing past it EVER calls
; `vec_get`/`bfa_block_ptr`/`bfa_inst_ptr` on a value or block id taken
; directly from instruction content without rule 1 having already bounded
; it -- `vec_get` (rt/vec.inc) `rassert`s out of range, which would crash
; this checker instead of returning a verdict, so rule 1 passing is a
; PRECONDITION for every later pass's safety, not just its own concern.
; Rules 6 and 7 are folded into the SAME per-instruction pass as rule 1
; (one shape-dispatch switch checking all three inline per operand,
; mirroring how parse.inc/print.inc each carry their own single shape
; switch rather than sharing one) since none of the three needs CFG
; structure; rules 2, 3, 4, 5, 8 each get their own pass after, in that
; order, each building only the scratch structure it needs (predecessor
; lists before rule 3; a forward dominator tree before rule 4/5's first
; half; a post-dominator tree before rule 5's second half). This ordering
; is a property of THIS FILE's implementation, not of section 3's prose
; (which lists the nine checks without prescribing a check order) --
; recorded here so a fixture author knows why, say, a rule-4 fixture must
; keep every OTHER rule clean rather than merely "mostly clean".
;
; DETERMINISM (CLAUDE.md; ssa-ir.md section 4's own row: "dominators
; (verifier) | reverse postorder over terminator-operand order"):
;   - Block iteration is always index order (1-based internal id 1..N,
;     i.e. Vec position order) -- never anything else.
;   - A block's successors are read directly off its TERMINATOR
;     instruction, in the order that instruction's own operands appear:
;     `jmp` has one; `br` has [true, false] (docs/asm-conventions.md
;     section 4.1 notwithstanding -- this is section 2.3's own operand
;     order, confirmed against emit.inc's `__bfa_emit_br`, which reads the
;     same two extra-pool slots in the same order to lower the same
;     branch). `ret`/`trap` have none. This is "successors in
;     terminator-operand order", exactly section 3's phrase.
;   - Predecessor lists (this file's own scratch structure -- see FINDING
;     1, below) are built by scanning blocks 1..N in that fixed order and
;     appending each block's own successors, in the order just described,
;     to the successor's predecessor list. This is a function of (block
;     index, terminator-operand order) alone, per section 4's table --
;     never a pointer, never a hash bucket.
;   - Reverse postorder for the forward dominator tree is a DFS from block
;     1 (the function's entry -- section 2.1's own convention: block ids
;     are dense from 1, and prescan/parse.inc always assigns text `b0` to
;     internal id 1) that visits successors in the same fixed order above,
;     pushes each block to a postorder list on the way OUT, then reverses
;     that list. A function of block index and terminator-operand order,
;     nothing else.
;   - The post-dominator tree (rule 5's second half) is the same
;     algorithm run on the REVERSED graph, rooted at one synthetic exit
;     node id `nblocks+1` whose out-edges (in the reversed graph) go to
;     every block whose terminator is `ret`/`trap`, enumerated in
;     ascending block-index order (this file's own, documented choice --
;     section 3/section 4 do not fix an order for synthetic exit edges
;     since nothing built a post-dominator tree before this file).
;
; FINDING 1 -- ir.inc's BfaEdge/BfaBlock.first_pred/last_pred are declared
; but nothing populates them. `bfa_block_link_body`/`bfa_block_link_phi`
; (ir.inc) never touch a predecessor list, and parse.inc's realpass never
; calls anything that would. Grepped before writing this file: zero writes
; to `BfaBlock.first_pred`, `BfaBlock.last_pred`, or `BfaFunc.edges`
; anywhere in ir.inc or parse.inc. This file does NOT populate them either
; (out of this file's write scope, and doing so would mutate IR the caller
; handed in as a read-only check, which "verifier" should not do) -- it
; builds its own, separate scratch predecessor lists in `varena` instead,
; by scanning terminators (see DETERMINISM, above). Reported for the
; ir.inc/parse.inc owner: either those fields should be populated where
; blocks/edges are actually built (parse.inc's realpass, and eventually
; section 2.5's construction pass, which will need a `bfa_edge_push`-
; shaped builder call at every `jmp`/`br`/fallthrough site it lowers --
; not this file's to add, reported here for whoever writes section 2.5),
; or they should be removed until something needs them -- an unpopulated
; field with a documented meaning is exactly the kind of drift CLAUDE.md's
; evidence discipline flags.
;
; PREDECESSOR ORDER, precisely, because ssa-ir.md section 2.4 defines it
; as "the order their terminators were emitted", NOT ascending block
; index, and the two are NOT the same thing in general (a Braun-style
; on-the-fly builder can create block B before block A's terminator is
; emitted, if the walk forks into B's arm first and only later comes back
; to finish A -- CLAUDE.md's evidence discipline says a plausible-looking
; block-index approximation is not a restoration of that spec sentence).
; This file's `__bfa_verify_build_predvecs` does NOT read a real edge
; list -- FINDING 1, just above, is exactly that no such list is ever
; populated for it to read. What it does instead: scan blocks 1..nblocks
; in INDEX order and append, for each block's own successors (in
; terminator-operand order), to that successor's predecessor list. For
; every function this tree can currently PRODUCE -- and today that is
; only `bfa_parse_module`'s text parser, section 2.5 not being built yet
; (this design contract's own status line) -- block index IS terminator-
; emission order, by construction, not by approximation: `parse.inc`'s
; prescan assigns internal block id N+1 to the (N+1)-th `bN:` header
; encountered reading top to bottom, and the realpass then emits each
; block's terminator while processing that same top-to-bottom pass, so
; the two orders are literally the same walk. A fixture claiming to show
; block-index order and edge-emission order diverging (e.g. a join whose
; predecessors are `b3` then `b1`) CANNOT be constructed through this
; tree's only builder: nothing here can make block 3's terminator run
; before block 1's, because nothing here can make block 3 exist before
; block 1 does. This is therefore not this file's approximation to
; document as `[OPEN]` so much as a fact about what `bfa_parse_module`
; can produce -- the `[OPEN]` risk is squarely in the FUTURE section 2.5
; builder FINDING 1 flags, and belongs on that builder's own verification
; once it exists, not simulated here against a builder this tree does not
; have. Re-examine this note the day section 2.5 lands.
;
; FINDING 2 -- ir.inc has no `BFA_OP_NOP` constant. Section 2.1 says "a
; removed instruction becomes `nop`, with `a` = its replacement", and
; section 3 requires "no `nop`" as its own rule, but nothing in
; `compiler/x86_64/backend_fasmg/ir.inc`'s BFA_OP_* enum (1..60, ADD..
; FCONST) reserves a value for it, and parse.inc's optab has no `nop`
; mnemonic. This file treats raw `Inst.op == 0` as "nop" -- 0 is already
; documented as an unused sentinel (print.inc's op-shape table: "Index 0
; (op id 0) is never a real opcode; its entry is unused padding"), so
; reading it as "this slot was zeroed / never assigned a real op" is the
; only interpretation consistent with what IS written down, but it is
; UNTESTED against any real instruction-removal pass, because none exists
; anywhere in this tree yet. `tests/unit/bfa_verify_rule9_bad.asm` proves
; this file's own `== 0` check fires by constructing the case directly
; through ir.inc's procs (there is no way to spell a `nop` through
; parse.inc's text grammar at all -- reported here rather than guessed
; around). The design owner should give `nop` a real `BFA_OP_NOP` constant
; in ir.inc; until then this file's choice is a documented inference, not
; a restoration of an existing fact.
;
; FINDING 3 -- section 2.3's table does not say which of a UN-shaped op's
; two types (source, destination) the single `T` token in `T %a` names for
; `zext`/`sext`/`trunc`/`fext`/`ftrunc`/`itof`/`ftoi`/`bitcast`. ir.inc's
; own header (`bfa_value_type`'s comment) resolves this much: `Inst.ty` is
; the PRODUCED value's type for every shape except cmp/fcmp/the three
; overflow predicates, so `T` in `zext T %a` is the RESULT type, and the
; SOURCE type is whatever `%a`'s own definition says -- never written a
; second time in the instruction. That leaves no textual basis for
; checking width/sign COMPATIBILITY between source and destination (e.g.
; that `zext`'s source is narrower than `T`, or that `trunc`'s is wider) --
; rule 1, for these eight ops, checks only that the operand resolves to a
; defined, non-void value; it does not check any relationship between the
; operand's own type and `T`. `[OPEN]`, and a real gap: a `zext u8 %a`
; where `%a` is itself `u64` (a truncation mislabeled as an extension)
; passes this file's rule 1 today. Flagged for the design owner: section
; 2.3 needs a sentence saying which type is written and what constraint
; the other one must satisfy.
;
; FINDING 4 -- `call`/`callind`'s declared return type and argument list
; are not cross-checked against the callee's `BfaSig` (module.sigs) or,
; for `callind`, against anything at all (an indirect callee's signature
; is not even recoverable from a `ptr`-typed function pointer -- only a
; `fn.S`-typed one names an S). Rule 1, for both call shapes, checks only
; that the callee (a function index for `call`, a resolved non-void `ptr`/
; `fn.S` value for `callind`) and every argument resolve to defined,
; non-void values. It does NOT check argument count or per-argument type
; against `BfaSig`, and does NOT check the call's own declared result type
; against the callee's declared return type. `[OPEN]`: section 3's own
; text ("every operand resolves, with the type its op requires") reads
; most naturally as requiring this for `call` too, and it is a bounded,
; well-defined check to add -- left out of this pass to keep the shape
; switch (already the largest single piece of this file) from growing
; further, not because it is hard. `ret` is the other end of the same
; question and IS now checked: its operand against the function's own
; declared return type, and a valueless `ret` only in a `void` function
; (__bfa_verify_pa_ret; wire-codec milestone M3, where narrow integers made
; a mismatch a wrong value rather than a pun). So is every constant against
; its type's canonical form -- `iconst` and inline immediates
; (__bfa_verify_pa_iconst, __bfa_verify_pa_imm).
;
; FINDING 5 -- "`rumpe` out of a reduction loop" (ssa-ir.md section 3's own
; parenthetical, `[OPEN]` there already) has NO representation in the
; current IR at all: section 2.3's instruction table has no `rumpe`-lowering
; op, and grepping ir.inc/parse.inc/print.inc/emit.inc for anything of the
; kind finds nothing. This file's rule 5 (redfin post-dominates every
; contrib of its handle) is therefore total and exact over every program
; the current IR can express -- there is no IR-level edge yet that could
; carry control out of a reduction body early, so nothing is being
; silently under-checked TODAY. The `[OPEN]` this file inherits from
; ssa-ir.md is about what happens once such a lowering exists (a `contrib`
; on a path that skips its `redfin` via an early exit would need this
; rule, or the lowering itself, to account for it) -- not resolved here,
; carried forward unchanged.
;
; SIZING (CLAUDE.md, ssa-ir.md section 5 "H1"): `rt/arena.inc` is FIXED
; CAPACITY, never auto-growing; `rt/vec.inc`'s `Vec`, in contrast, grows
; INSIDE whatever arena backs it (doubling, `arena_alloc`-backed) --
; ir.inc's own header states this Vec/arena distinction and this file
; relies on it. Every scratch array this file allocates is either a
; fixed-size block sized directly from `ctx.nblocks`/`ctx.ninsts` (the
; function's own, already-known bounds -- `predvecs`'s outer Vec, the
; `rpo_index`/`idom`/`prpo_index`/`pidom` arrays) or a `Vec` that grows on
; its own (each block's individual predecessor list). Every one of these
; allocations goes through `arena_alloc`/`vec_init`/`vec_push` on the
; caller-supplied `varena` -- so the single `rassert rcx le rdx` inside
; `rt/arena.inc`'s `arena_alloc` (its own header: "`arena_alloc` past the
; capacity given to `arena_init` `rassert`s") IS this file's overrun
; guard; nothing here duplicates that check; a caller sizing `varena` too
; small for a function's block/instruction count traps there, not here,
; and `bfa_verify_module`'s own fixture (`tests/unit/bfa_verify_*.asm`)
; sizes `varena` generously (see the report for the exact figure and why).
; -----------------------------------------------------------------------------

include 'print.inc'



; ============================================================================
; rule ids
; ============================================================================
BFA_VERIFY_OK              = 0
BFA_VERIFY_R1_OPERAND      = 1
BFA_VERIFY_R2_TERM         = 2
BFA_VERIFY_R3_PHI          = 3
BFA_VERIFY_R4_DOM          = 4
BFA_VERIFY_R5_REDUCTION    = 5
BFA_VERIFY_R6_ARC          = 6
BFA_VERIFY_R7_REDTYPE      = 7
BFA_VERIFY_R8_NUMERI       = 8
BFA_VERIFY_R9_NONOP        = 9

BFA_VERIFY_NOPOS = 0xFFFFFFFF	; rpo_index/prpo_index sentinel: unreached

; ============================================================================
; BfaVerifyCtx -- one per bfa_verify_func call, allocated from `varena`.
; ============================================================================
struct BfaVerifyCtx
	field module,      dq
	field func,         dq
	field varena,       dq
	field nblocks,      dd
	field ninsts,       dd
	field instblock,    dq	; -> Vec<dd>, index = instid-1 -> blockid (built
				;    after rule 1 passes -- see this file's header)
	field predvecs,     dq	; -> Vec<dq>, index = blockid-1 -> Vec<dd>* of
				;    predecessor block ids, append order =
				;    "order terminators were emitted" (see header)
	field rpo_index,    dq	; -> Vec<dd>, index = blockid-1 -> forward RPO
				;    position, or BFA_VERIFY_NOPOS if unreached
	field idom,         dq	; -> Vec<dd>, index = blockid-1 -> forward
				;    immediate dominator (1-based block id);
				;    idom[entry] = entry itself; 0 = unassigned
	field prpo_index,   dq	; -> Vec<dd>, index = blockid-1 -> reverse RPO
				;    position (root = synthetic exit), or
				;    BFA_VERIFY_NOPOS if it cannot reach an exit
	field pidom,        dq	; -> Vec<dd>, index = blockid-1 -> immediate
				;    POST-dominator; 0 = the synthetic exit
				;    node itself (a real, valid answer here,
				;    unlike the forward `idom` 0-sentinel --
				;    see __bfa_verify_build_postdom's header)
	field pidom_set,    dq	; -> Vec<db>, index = blockid-1 -> 1 iff pidom[]
				;    has been assigned (needed because reverse
				;    0 is a legal VALUE, so it cannot double as
				;    "unassigned" the way forward `idom` does)
	field out_blk,      dd	; scratch mirror of the failing block/inst,
	field out_inst,     dd	; set by whichever leaf check fails first --
				; see __bfa_verify_passA's header for the
				; "set before dispatch, trust only on failure"
				; convention this and every later pass share
end struct

; ============================================================================
; small range/resolution primitives -- EVERY block id or value id taken
; directly from instruction CONTENT (never from this file's own bookkeeping
; Vecs) goes through one of these before it is dereferenced. `vec_get`
; (rt/vec.inc) `rassert`s out of range; a verdict-returning checker must
; never reach that trap on malformed input, so the range check always comes
; first, in code, not just in comment.
; ============================================================================

; __bfa_verify_blk_ok(ctx, blkid) -> eax = 1 iff 1 <= blkid <= nblocks.
proc __bfa_verify_blk_ok, ctx, blkid
	locals
	endl
	mov	rax, [blkid]
	test	rax, rax
	jz	.no
	mov	rcx, [ctx]
	mov	ecx, dword [rcx + BfaVerifyCtx.nblocks]
	cmp	rax, rcx
	jg	.no
	mov	eax, 1
	return
  .no:
	mov	eax, 0
	return
endp

; __bfa_verify_is_void(func, instid) -> eax = 1 iff this instruction
; produces no value (a "void statement" -- section 2.11's own term).
; `call`/`callind` are void iff their own `ty` field is 0 (the `void`
; callee marker parse.inc's __bfa_ps_call/__bfa_ps_callind both use);
; every other shape's void-ness is fixed by the shape alone.
proc __bfa_verify_is_void, func, instid
	locals
		slot op, dd
		slot shp, dd
	endl
	mov	rdi, [func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	edi, ecx
	call	__bfa_op_shape
	mov	[shp], eax

	mov	eax, [shp]
	cmp	eax, BFA_SH_CONTRIB
	je	.void
	cmp	eax, BFA_SH_STORE
	je	.void
	cmp	eax, BFA_SH_STOREBITS
	je	.void
	cmp	eax, BFA_SH_COPY
	je	.void
	cmp	eax, BFA_SH_CHK
	je	.void
	cmp	eax, BFA_SH_RETAIN
	je	.void
	cmp	eax, BFA_SH_RET
	je	.void
	cmp	eax, BFA_SH_JMP
	je	.void
	cmp	eax, BFA_SH_BR
	je	.void
	cmp	eax, BFA_SH_TRAP
	je	.void
	cmp	eax, BFA_SH_CALL
	je	.checkty
	cmp	eax, BFA_SH_CALLIND
	je	.checkty
	mov	eax, 0
	return
  .void:
	mov	eax, 1
	return
  .checkty:
	mov	rdi, [func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.ty]
	test	ecx, ecx
	jnz	.notvoid
	mov	eax, 1
	return
  .notvoid:
	mov	eax, 0
	return
endp

; __bfa_verify_val_ok(ctx, valid) -> eax = 1 iff 1 <= valid <= ninsts AND
; the instruction it names is not void (section 3, "every operand
; resolves" -- a reference to a void statement does not resolve to a
; usable value any more than an out-of-range index does).
proc __bfa_verify_val_ok, ctx, valid
	locals
	endl
	mov	rax, [valid]
	test	rax, rax
	jz	.no
	mov	rcx, [ctx]
	mov	ecx, dword [rcx + BfaVerifyCtx.ninsts]
	cmp	rax, rcx
	jg	.no
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [valid]
	call	__bfa_verify_is_void
	test	eax, eax
	jnz	.no
	mov	eax, 1
	return
  .no:
	mov	eax, 0
	return
endp

; __bfa_verify_val_type(ctx, valid) -> eax = type id. Caller MUST have
; already confirmed __bfa_verify_val_ok(ctx, valid) -- this is a thin
; wrapper over bfa_value_type with no range check of its own.
proc __bfa_verify_val_type, ctx, valid
	locals
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [valid]
	call	bfa_value_type
	cmp	eax, BFA_VALUE_TYPE_IS_U1
	jne	.notu1
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	call	bfa_ty_u1
	return
  .notu1:
	return
endp

; __bfa_verify_ty_ok(ctx, tyid) -> eax = 1 iff 1 <= tyid <= module.types.len.
proc __bfa_verify_ty_ok, ctx, tyid
	locals
	endl
	mov	rax, [tyid]
	test	rax, rax
	jz	.no
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	mov	rdi, [rdi + BfaModule.types]
	mov	rcx, [rdi + Vec.len]
	cmp	rax, rcx
	jg	.no
	mov	eax, 1
	return
  .no:
	mov	eax, 0
	return
endp

; __bfa_verify_tykind(ctx, tyid) -> eax = BfaType.kind. Caller must have
; already confirmed __bfa_verify_ty_ok(ctx, tyid).
proc __bfa_verify_tykind, ctx, tyid
	locals
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	mov	rsi, [tyid]
	call	bfa_type_ptr
	movzx	eax, byte [rax + BfaType.kind]
	return
endp

; __bfa_verify_ptrty(ctx) -> eax = the `ptr` type id (interns it if needed
; -- harmless, bfa_type_intern dedups; every fixture already has `ptr`
; interned by the time any instruction using it was parsed).
proc __bfa_verify_ptrty, ctx
	locals
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	call	bfa_ty_ptr
	return
endp

; ============================================================================
; PASS A -- rules 1 (operand resolves, with the type its op requires),
; 6 (retain/release only on ref/refc) and 7 (red.F only in its three ops),
; one combined per-instruction shape switch, mirroring parse.inc/print.inc's
; own single-switch-per-file style rather than sharing one across files
; (this file's header explains why: none of the three needs CFG structure,
; and duplicating the switch a THIRD time, once per file, is this codebase's
; established price for keeping each file independently reasoned about).
; ============================================================================

; __bfa_verify_pa_checkval(ctx, valid, expected_ty) -> eax = 0 ok, 1 (rule 1:
; does not resolve, or resolves to the wrong type), 7 (rule 7: resolves to a
; `red.F`-kind value in a position other than `contrib`'s/`redfin`'s handle
; operand). `expected_ty` = 0 means "any type accepted" (still resolves +
; still red.F-checked) -- used where section 2.3 gives no second type token
; to check against (see FINDING 3, this file's header).
proc __bfa_verify_pa_checkval, ctx, valid, expected_ty
	locals
		slot ty, dd
		slot kind, dd
	endl
	mov	rdi, [ctx]
	mov	rsi, [valid]
	call	__bfa_verify_val_ok
	test	eax, eax
	jnz	.resolved
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .resolved:
	mov	rdi, [ctx]
	mov	rsi, [valid]
	call	__bfa_verify_val_type
	mov	[ty], eax
	mov	ecx, [expected_ty]
	test	ecx, ecx
	jz	.noexpect
	cmp	eax, ecx
	je	.noexpect
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .noexpect:
	mov	rdi, [ctx]
	mov	esi, [ty]
	call	__bfa_verify_tykind
	mov	[kind], eax
	cmp	dword [kind], BFA_TK_RED
	jne	.ok
	mov	eax, BFA_VERIFY_R7_REDTYPE
	return
  .ok:
	mov	eax, 0
	return
endp

; __bfa_verify_pa_checkval_noty(ctx, valid) -> eax = 0/1/7, same as above
; but with no expected-type check at all (FINDING 3/4: UN-shaped ops and
; call/callind arguments -- section 2.3 gives no second type to check them
; against).
proc __bfa_verify_pa_checkval_noty, ctx, valid
	locals
	endl
	mov	rdi, [ctx]
	mov	rsi, [valid]
	xor	edx, edx
	call	__bfa_verify_pa_checkval
	return
endp

; __bfa_verify_pa_checkval_permitted_red(ctx, valid) -> eax = 0 ok, 1
; (does not resolve, or resolves but is NOT `red.F`-kind). Used only for
; `contrib`'s and `redfin`'s own handle operand -- the one place a
; `red.F` value is required, not forbidden.
proc __bfa_verify_pa_checkval_permitted_red, ctx, valid
	locals
		slot ty, dd
	endl
	mov	rdi, [ctx]
	mov	rsi, [valid]
	call	__bfa_verify_val_ok
	test	eax, eax
	jnz	.resolved
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .resolved:
	mov	rdi, [ctx]
	mov	rsi, [valid]
	call	__bfa_verify_val_type
	mov	[ty], eax
	mov	rdi, [ctx]
	mov	esi, [ty]
	call	__bfa_verify_tykind
	cmp	eax, BFA_TK_RED
	je	.ok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .ok:
	mov	eax, 0
	return
endp

; __bfa_verify_pa_checkval_intkind(ctx, valid) -> eax = 0 ok, 1 (does not
; resolve, or its type is not `uN`/`iN`). Used for `index`'s index operand
; and `chk`'s two operands.
proc __bfa_verify_pa_checkval_intkind, ctx, valid
	locals
		slot ty, dd
		slot kind, dd
	endl
	mov	rdi, [ctx]
	mov	rsi, [valid]
	call	__bfa_verify_val_ok
	test	eax, eax
	jnz	.resolved
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .resolved:
	mov	rdi, [ctx]
	mov	rsi, [valid]
	call	__bfa_verify_val_type
	mov	[ty], eax
	mov	rdi, [ctx]
	mov	esi, [ty]
	call	__bfa_verify_tykind
	mov	[kind], eax
	cmp	dword [kind], BFA_TK_U
	je	.ok
	cmp	dword [kind], BFA_TK_I
	je	.ok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .ok:
	mov	eax, 0
	return
endp

; __bfa_verify_pa_checkblk(ctx, blkid) -> eax = 0 ok, 1 (out of range).
proc __bfa_verify_pa_checkblk, ctx, blkid
	locals
	endl
	mov	rdi, [ctx]
	mov	rsi, [blkid]
	call	__bfa_verify_blk_ok
	test	eax, eax
	jnz	.ok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .ok:
	mov	eax, 0
	return
endp

; ---- per-shape PASS A checkers. Each takes (ctx, instid), fetches its own
; BfaInst*, and returns eax = 0/1/6/7. `ctx.func` is read off ctx rather
; than threaded as a fourth argument -- every one of these needs `module`
; too (for type lookups), which already fills the six-argument budget on
; the shared checkval helpers above; reading both off ctx keeps every
; per-shape proc at exactly (ctx, instid).

proc __bfa_verify_pa_bin, ctx, instid
	locals
		slot ty, dd
		slot a, dd
		slot b, dd
		slot aux, dd
		slot r, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.ty]
	mov	[ty], ecx
	mov	ecx, [rax + BfaInst.a]
	mov	[a], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[b], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[aux], ecx

	mov	dword [r], 0
	test	dword [aux], 0x100
	jnz	.imma
	mov	rdi, [ctx]
	mov	esi, [a]
	mov	edx, [ty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	jmp	.skipa
  .imma:
	mov	rdi, [ctx]
	mov	esi, [ty]
	mov	edx, [a]
	call	__bfa_verify_pa_imm
	mov	[r], eax
	test	eax, eax
	jnz	.ret
  .skipa:
	test	dword [aux], 0x200
	jnz	.immb
	mov	rdi, [ctx]
	mov	esi, [b]
	mov	edx, [ty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
	jmp	.ret
  .immb:
	mov	rdi, [ctx]
	mov	esi, [ty]
	mov	edx, [b]
	call	__bfa_verify_pa_imm
	mov	[r], eax
  .ret:
	mov	eax, [r]
	return
endp

; __bfa_verify_pa_imm(ctx, ty, raw) -> eax = 0 ok, 1 (rule 1). An inline
; immediate operand (parse.inc's `__bfa_val_or_imm`) is the 32-bit field
; `raw`, which the emitter sign-extends; it must be a value of `ty` in
; canonical form (ssa-ir.md 2.2), the same rule `iconst` is held to.
; `addw u8 %x -1` and `cmp.lt u32 %x 4294967295` (whose field reads back
; as -1) are refused here rather than computed with the wrong constant.
proc __bfa_verify_pa_imm, ctx, ty, raw
	locals
		slot imv, dq
	endl
	mov	rdi, [ctx]
	mov	esi, [ty]
	call	__bfa_verify_ty_ok
	test	eax, eax
	jz	.bad
	movsxd	rax, dword [raw]
	mov	[imv], rax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	mov	esi, [ty]
	mov	rdx, [imv]
	call	bfa_type_canon
	cmp	rax, [imv]
	jne	.bad
	mov	eax, 0
	return
  .bad:
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
endp

; cmp.*/fcmp.* -- identical token/operand shape to `bin`, `ty` is the
; OPERAND type (the predicate lives in `aux`'s low byte, unused here).
proc __bfa_verify_pa_cmp, ctx, instid
	locals
	endl
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_bin
	return
endp

proc __bfa_verify_pa_un, ctx, instid
	locals
		slot a, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[a], ecx
	mov	rdi, [ctx]
	mov	esi, [a]
	call	__bfa_verify_pa_checkval_noty
	return
endp

proc __bfa_verify_pa_fma, ctx, instid
	locals
		slot ty, dd
		slot eidx, dd
		slot r, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.ty]
	mov	[ty], ecx
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	mov	edx, [ty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
	test	eax, eax
	jnz	.ret

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	mov	edx, [ty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
	test	eax, eax
	jnz	.ret

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	rdi, [ctx]
	mov	esi, eax
	mov	edx, [ty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
  .ret:
	mov	eax, [r]
	return
endp

proc __bfa_verify_pa_contrib, ctx, instid
	locals
		slot h, dd
		slot v, dd
		slot hty, dd
		slot innerf, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[h], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[v], ecx

	mov	rdi, [ctx]
	mov	esi, [h]
	call	__bfa_verify_pa_checkval_permitted_red
	test	eax, eax
	jnz	.ret

	mov	rdi, [ctx]
	mov	esi, [h]
	call	__bfa_verify_val_type
	mov	[hty], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	mov	esi, [hty]
	call	bfa_type_ptr
	mov	ecx, [rax + BfaType.ref]
	mov	[innerf], ecx

	mov	rdi, [ctx]
	mov	esi, [v]
	mov	edx, [innerf]
	call	__bfa_verify_pa_checkval
  .ret:
	return
endp

proc __bfa_verify_pa_redfin, ctx, instid
	locals
		slot ty, dd
		slot h, dd
		slot hty, dd
		slot innerf, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.ty]
	mov	[ty], ecx
	mov	ecx, [rax + BfaInst.a]
	mov	[h], ecx

	mov	rdi, [ctx]
	mov	esi, [h]
	call	__bfa_verify_pa_checkval_permitted_red
	test	eax, eax
	jnz	.ret

	mov	rdi, [ctx]
	mov	esi, [h]
	call	__bfa_verify_val_type
	mov	[hty], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	mov	esi, [hty]
	call	bfa_type_ptr
	mov	ecx, [rax + BfaType.ref]
	mov	[innerf], ecx
	cmp	ecx, [ty]
	je	.ok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .ok:
	mov	eax, 0
  .ret:
	return
endp

proc __bfa_verify_pa_load, ctx, instid
	locals
		slot eidx, dd
		slot vp, dd
		slot ordv, dd
		slot ptrty, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[vp], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[ordv], eax

	cmp	dword [ordv], BFA_ORDER_NATIVUS
	je	.ordok
	cmp	dword [ordv], BFA_ORDER_MAIOR
	je	.ordok
	cmp	dword [ordv], BFA_ORDER_MINOR
	je	.ordok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .ordok:
	mov	rdi, [ctx]
	call	__bfa_verify_ptrty
	mov	[ptrty], eax
	mov	rdi, [ctx]
	mov	esi, [vp]
	mov	edx, [ptrty]
	call	__bfa_verify_pa_checkval
	return
endp

proc __bfa_verify_pa_store, ctx, instid
	locals
		slot ty, dd
		slot eidx, dd
		slot vp, dd
		slot ordv, dd
		slot vv, dd
		slot ptrty, dd
		slot r, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.ty]
	mov	[ty], ecx
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[vp], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[ordv], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 3
	mov	esi, eax
	call	bfa_extra_get
	mov	[vv], eax

	cmp	dword [ordv], BFA_ORDER_NATIVUS
	je	.ordok
	cmp	dword [ordv], BFA_ORDER_MAIOR
	je	.ordok
	cmp	dword [ordv], BFA_ORDER_MINOR
	je	.ordok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .ordok:
	mov	rdi, [ctx]
	call	__bfa_verify_ptrty
	mov	[ptrty], eax
	mov	rdi, [ctx]
	mov	esi, [vp]
	mov	edx, [ptrty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	rdi, [ctx]
	mov	esi, [vv]
	mov	edx, [ty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
  .ret:
	mov	eax, [r]
	return
endp

proc __bfa_verify_pa_loadbits, ctx, instid
	locals
		slot eidx, dd
		slot vp, dd
		slot ptrty, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[vp], eax
	mov	rdi, [ctx]
	call	__bfa_verify_ptrty
	mov	[ptrty], eax
	mov	rdi, [ctx]
	mov	esi, [vp]
	mov	edx, [ptrty]
	call	__bfa_verify_pa_checkval
	return
endp

proc __bfa_verify_pa_storebits, ctx, instid
	locals
		slot ty, dd
		slot eidx, dd
		slot vp, dd
		slot vv, dd
		slot ptrty, dd
		slot r, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.ty]
	mov	[ty], ecx
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[vp], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 3
	mov	esi, eax
	call	bfa_extra_get
	mov	[vv], eax

	mov	rdi, [ctx]
	call	__bfa_verify_ptrty
	mov	[ptrty], eax
	mov	rdi, [ctx]
	mov	esi, [vp]
	mov	edx, [ptrty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	rdi, [ctx]
	mov	esi, [vv]
	mov	edx, [ty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
  .ret:
	mov	eax, [r]
	return
endp

proc __bfa_verify_pa_copy, ctx, instid
	locals
		slot eidx, dd
		slot vd, dd
		slot vs, dd
		slot ptrty, dd
		slot r, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[vd], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[vs], eax

	mov	rdi, [ctx]
	call	__bfa_verify_ptrty
	mov	[ptrty], eax
	mov	rdi, [ctx]
	mov	esi, [vd]
	mov	edx, [ptrty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	rdi, [ctx]
	mov	esi, [vs]
	mov	edx, [ptrty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
  .ret:
	mov	eax, [r]
	return
endp

proc __bfa_verify_pa_addr, ctx, instid
	locals
		slot vp, dd
		slot ptrty, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[vp], ecx
	mov	rdi, [ctx]
	call	__bfa_verify_ptrty
	mov	[ptrty], eax
	mov	rdi, [ctx]
	mov	esi, [vp]
	mov	edx, [ptrty]
	call	__bfa_verify_pa_checkval
	return
endp

proc __bfa_verify_pa_index, ctx, instid
	locals
		slot eidx, dd
		slot vp, dd
		slot vi, dd
		slot ptrty, dd
		slot r, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[vp], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[vi], eax

	mov	rdi, [ctx]
	call	__bfa_verify_ptrty
	mov	[ptrty], eax
	mov	rdi, [ctx]
	mov	esi, [vp]
	mov	edx, [ptrty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	rdi, [ctx]
	mov	esi, [vi]
	call	__bfa_verify_pa_checkval_intkind
	mov	[r], eax
  .ret:
	mov	eax, [r]
	return
endp

proc __bfa_verify_pa_gaddr, ctx, instid
	locals
		slot n, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[n], ecx
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	mov	rdi, [rdi + BfaModule.globals]
	mov	rax, [rdi + Vec.len]
	cmp	dword [n], eax
	jl	.ok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .ok:
	mov	eax, 0
	return
endp

proc __bfa_verify_pa_chk, ctx, instid
	locals
		slot vi, dd
		slot vn, dd
		slot r, dd
		slot tyi, dd
		slot tyn, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[vi], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[vn], ecx

	mov	rdi, [ctx]
	mov	esi, [vi]
	call	__bfa_verify_pa_checkval_intkind
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	rdi, [ctx]
	mov	esi, [vn]
	call	__bfa_verify_pa_checkval_intkind
	mov	[r], eax
	test	eax, eax
	jnz	.ret

	mov	rdi, [ctx]
	mov	esi, [vi]
	call	__bfa_verify_val_type
	mov	[tyi], eax
	mov	rdi, [ctx]
	mov	esi, [vn]
	call	__bfa_verify_val_type
	mov	[tyn], eax
	mov	eax, [tyi]
	cmp	eax, [tyn]
	je	.ok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .ok:
	mov	eax, 0
  .ret:
	return
endp

; retain/release -- rule 1 (resolve) then rule 6 (ref/refc only). A
; `red.F` operand here fails rule 6 (wrong ARC type), never rule 7 --
; documented precedence, this file's own choice (see this proc's own
; comment for why both COULD apply).
proc __bfa_verify_pa_retain, ctx, instid
	locals
		slot r, dd
		slot ty, dd
		slot kind, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[r], ecx

	mov	rdi, [ctx]
	mov	esi, [r]
	call	__bfa_verify_val_ok
	test	eax, eax
	jnz	.resolved
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .resolved:
	mov	rdi, [ctx]
	mov	esi, [r]
	call	__bfa_verify_val_type
	mov	[ty], eax
	mov	rdi, [ctx]
	mov	esi, [ty]
	call	__bfa_verify_tykind
	mov	[kind], eax
	cmp	dword [kind], BFA_TK_REF
	je	.ok
	cmp	dword [kind], BFA_TK_REFC
	je	.ok
	mov	eax, BFA_VERIFY_R6_ARC
	return
  .ok:
	mov	eax, 0
	return
endp

; `ret [%v]` -- %v's type must be the function's declared return type, and
; a valueless `ret` belongs to a `void` function only (an aggregate result
; is `void` here too: it travels through the hidden result `ptr`, ssa-ir.md
; 2.9). This used to check only that %v resolved, so a `u64` returned from
; a `-> u8` function passed -- and with narrow integers held in canonical
; form (ssa-ir.md 2.2) that is not a type pun but a wrong value: the
; caller's slot would hold bits above bit 7. Found by tests/ir/'s first
; fixtures (they declared `@initium -> u64` to stay honest around it).
proc __bfa_verify_pa_ret, ctx, instid
	locals
		slot vv, dd
		slot rty, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[vv], ecx

	mov	rax, [ctx]
	mov	rcx, [rax + BfaVerifyCtx.func]
	mov	esi, [rcx + BfaFunc.sig]
	mov	rdi, [rax + BfaVerifyCtx.module]
	mov	rdi, [rdi + BfaModule.sigs]
	call	vec_get
	mov	ecx, [rax + BfaSig.ret_ty]
	mov	[rty], ecx

	cmp	dword [vv], 0
	je	.novalue
	cmp	dword [rty], 0
	je	.bad
	mov	rdi, [ctx]
	mov	esi, [vv]
	mov	edx, [rty]
	call	__bfa_verify_pa_checkval
	return
  .novalue:
	cmp	dword [rty], 0
	jne	.bad
	mov	eax, 0
	return
  .bad:
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
endp

; `iconst T imm` -- the constant must be a value of T, in T's canonical
; form (ssa-ir.md 2.2): `iconst u8 256` and `iconst i8 200` are rule 1.
; Every other instruction may assume canonical operands; this is where a
; constant enters, so this is where it is checked. At 64 bits every bit
; pattern is a value and nothing is refused.
proc __bfa_verify_pa_iconst, ctx, instid
	locals
		slot eidx, dd
		slot ty, dd
		slot lo, dd
		slot cval, dq
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.ty]
	mov	[ty], ecx
	mov	rdi, [ctx]
	mov	esi, [ty]
	call	__bfa_verify_ty_ok
	test	eax, eax
	jz	.bad
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[lo], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	inc	esi
	call	bfa_extra_get
	shl	rax, 32
	mov	ecx, [lo]
	or	rax, rcx
	mov	[cval], rax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	mov	esi, [ty]
	mov	rdx, [cval]
	call	bfa_type_canon
	cmp	rax, [cval]
	jne	.bad
	mov	eax, 0
	return
  .bad:
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
endp

proc __bfa_verify_pa_jmp, ctx, instid
	locals
		slot tgt, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[tgt], ecx
	mov	rdi, [ctx]
	mov	esi, [tgt]
	call	__bfa_verify_pa_checkblk
	return
endp

proc __bfa_verify_pa_br, ctx, instid
	locals
		slot eidx, dd
		slot cnd, dd
		slot btgt, dd
		slot bf, dd
		slot u1ty, dd
		slot ty, dd
		slot r, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[cnd], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[btgt], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[bf], eax

	mov	rdi, [ctx]
	mov	esi, [cnd]
	call	__bfa_verify_val_ok
	test	eax, eax
	jnz	.condresolved
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .condresolved:
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	call	bfa_ty_u1
	mov	[u1ty], eax
	mov	rdi, [ctx]
	mov	esi, [cnd]
	call	__bfa_verify_val_type
	mov	[ty], eax
	cmp	eax, [u1ty]
	je	.tyok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .tyok:
	mov	rdi, [ctx]
	mov	esi, [btgt]
	call	__bfa_verify_pa_checkblk
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	rdi, [ctx]
	mov	esi, [bf]
	call	__bfa_verify_pa_checkblk
	mov	[r], eax
  .ret:
	mov	eax, [r]
	return
endp

proc __bfa_verify_pa_trap, ctx, instid
	locals
	endl
	mov	eax, 0
	return
endp

proc __bfa_verify_pa_phi, ctx, instid
	locals
		slot ty, dd
		slot eidx, dd
		slot cnt, dd
		slot k, dd
		slot bp, dd
		slot v, dd
		slot r, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.ty]
	mov	[ty], ecx
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[cnt], ecx

	test	dword [cnt], 1
	jz	.evenok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .evenok:
	mov	dword [k], 0
  .loop:
	mov	eax, [k]
	cmp	eax, [cnt]
	jge	.alldone

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, [k]
	mov	esi, eax
	call	bfa_extra_get
	mov	[bp], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, [k]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[v], eax

	mov	rdi, [ctx]
	mov	esi, [bp]
	call	__bfa_verify_pa_checkblk
	mov	[r], eax
	test	eax, eax
	jnz	.ret

	mov	rdi, [ctx]
	mov	esi, [v]
	mov	edx, [ty]
	call	__bfa_verify_pa_checkval
	mov	[r], eax
	test	eax, eax
	jnz	.ret

	mov	eax, [k]
	add	eax, 2
	mov	[k], eax
	jmp	.loop
  .alldone:
	mov	eax, 0
	return
  .ret:
	mov	eax, [r]
	return
endp

proc __bfa_verify_pa_call, ctx, instid
	locals
		slot eidx, dd
		slot cnt, dd
		slot fnidx, dd
		slot i, dd
		slot arg, dd
		slot r, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[cnt], ecx

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[fnidx], eax
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	rax, [rdi + Vec.len]
	mov	ecx, [fnidx]
	test	ecx, ecx
	jz	.badfn
	cmp	ecx, eax
	jg	.badfn
	jmp	.fnok
  .badfn:
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .fnok:
	mov	dword [i], 1
  .loop:
	mov	eax, [i]
	cmp	eax, [cnt]
	jge	.alldone
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, [i]
	mov	esi, eax
	call	bfa_extra_get
	mov	[arg], eax
	mov	rdi, [ctx]
	mov	esi, [arg]
	call	__bfa_verify_pa_checkval_noty
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.loop
  .alldone:
	mov	eax, 0
	return
  .ret:
	mov	eax, [r]
	return
endp

proc __bfa_verify_pa_callind, ctx, instid
	locals
		slot eidx, dd
		slot cnt, dd
		slot fp, dd
		slot ty, dd
		slot kind, dd
		slot i, dd
		slot arg, dd
		slot r, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[cnt], ecx

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[fp], eax

	mov	rdi, [ctx]
	mov	esi, [fp]
	call	__bfa_verify_val_ok
	test	eax, eax
	jnz	.fpresolved
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .fpresolved:
	mov	rdi, [ctx]
	mov	esi, [fp]
	call	__bfa_verify_val_type
	mov	[ty], eax
	mov	rdi, [ctx]
	mov	esi, [ty]
	call	__bfa_verify_tykind
	mov	[kind], eax
	cmp	dword [kind], BFA_TK_PTR
	je	.fpok
	cmp	dword [kind], BFA_TK_FN
	je	.fpok
	mov	eax, BFA_VERIFY_R1_OPERAND
	return
  .fpok:
	mov	dword [i], 1
  .loop:
	mov	eax, [i]
	cmp	eax, [cnt]
	jge	.alldone
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, [i]
	mov	esi, eax
	call	bfa_extra_get
	mov	[arg], eax
	mov	rdi, [ctx]
	mov	esi, [arg]
	call	__bfa_verify_pa_checkval_noty
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.loop
  .alldone:
	mov	eax, 0
	return
  .ret:
	mov	eax, [r]
	return
endp

proc __bfa_verify_pa_trivial, ctx, instid
	locals
	endl
	mov	eax, 0
	return
endp

; __bfa_verify_passA_inst(ctx, instid) -> eax = 0/1/6/7. Rule 7a (a
; non-void instruction other than `redinit` must not produce a `red.F`
; value) first, then the shape-specific operand switch.
proc __bfa_verify_passA_inst, ctx, instid
	locals
		slot op, dd
		slot shp, dd
		slot voidf, dd
		slot ty, dd
		slot kind, dd
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	edi, ecx
	call	__bfa_op_shape
	mov	[shp], eax

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.func]
	mov	rsi, [instid]
	call	__bfa_verify_is_void
	mov	[voidf], eax
	test	eax, eax
	jnz	.skipredcheck
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_val_type
	mov	[ty], eax
	mov	rdi, [ctx]
	mov	esi, [ty]
	call	__bfa_verify_tykind
	mov	[kind], eax
	cmp	dword [kind], BFA_TK_RED
	jne	.skipredcheck
	cmp	dword [op], BFA_OP_REDINIT
	je	.skipredcheck
	mov	eax, BFA_VERIFY_R7_REDTYPE
	return
  .skipredcheck:

	mov	eax, [shp]
	cmp	eax, BFA_SH_BIN
	je	.d_bin
	cmp	eax, BFA_SH_CMP
	je	.d_cmp
	cmp	eax, BFA_SH_FCMP
	je	.d_cmp
	cmp	eax, BFA_SH_UN
	je	.d_un
	cmp	eax, BFA_SH_FMA
	je	.d_fma
	cmp	eax, BFA_SH_REDINIT
	je	.d_trivial
	cmp	eax, BFA_SH_CONTRIB
	je	.d_contrib
	cmp	eax, BFA_SH_REDFIN
	je	.d_redfin
	cmp	eax, BFA_SH_SLOT
	je	.d_trivial
	cmp	eax, BFA_SH_LOAD
	je	.d_load
	cmp	eax, BFA_SH_STORE
	je	.d_store
	cmp	eax, BFA_SH_LOADBITS
	je	.d_loadbits
	cmp	eax, BFA_SH_STOREBITS
	je	.d_storebits
	cmp	eax, BFA_SH_COPY
	je	.d_copy
	cmp	eax, BFA_SH_ADDR
	je	.d_addr
	cmp	eax, BFA_SH_INDEX
	je	.d_index
	cmp	eax, BFA_SH_GADDR
	je	.d_gaddr
	cmp	eax, BFA_SH_CHK
	je	.d_chk
	cmp	eax, BFA_SH_RETAIN
	je	.d_retain
	cmp	eax, BFA_SH_CALL
	je	.d_call
	cmp	eax, BFA_SH_CALLIND
	je	.d_callind
	cmp	eax, BFA_SH_RET
	je	.d_ret
	cmp	eax, BFA_SH_JMP
	je	.d_jmp
	cmp	eax, BFA_SH_BR
	je	.d_br
	cmp	eax, BFA_SH_TRAP
	je	.d_trivial
	cmp	eax, BFA_SH_PHI
	je	.d_phi
	cmp	eax, BFA_SH_PARAM
	je	.d_trivial
	cmp	eax, BFA_SH_ICONST
	je	.d_iconst
	cmp	eax, BFA_SH_FCONST
	je	.d_trivial
	; unrecognised shape -- an internal contract violation: the shape
	; table (print.inc) and this switch must cover the same op set. A
	; compiler bug in THIS file, not a verdict about the IR under test.
	db	0x0F, 0x0B

  .d_bin:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_bin
	return
  .d_cmp:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_cmp
	return
  .d_un:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_un
	return
  .d_fma:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_fma
	return
  .d_contrib:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_contrib
	return
  .d_redfin:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_redfin
	return
  .d_load:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_load
	return
  .d_store:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_store
	return
  .d_loadbits:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_loadbits
	return
  .d_storebits:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_storebits
	return
  .d_copy:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_copy
	return
  .d_addr:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_addr
	return
  .d_index:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_index
	return
  .d_gaddr:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_gaddr
	return
  .d_chk:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_chk
	return
  .d_retain:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_retain
	return
  .d_call:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_call
	return
  .d_callind:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_callind
	return
  .d_ret:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_ret
	return
  .d_jmp:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_jmp
	return
  .d_br:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_br
	return
  .d_phi:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_phi
	return
  .d_iconst:
	mov	rdi, [ctx]
	mov	rsi, [instid]
	call	__bfa_verify_pa_iconst
	return
  .d_trivial:
	mov	eax, 0
	return
endp

; __bfa_verify_passA(ctx) -> eax = 0/1/6/7. Walks every block's phi list
; then body list, index order (DETERMINISM, this file's header). Sets
; ctx.out_blk/out_inst before each dispatch so a failing call's site is
; already recorded when it returns nonzero.
proc __bfa_verify_passA, ctx
	uses	rbx, r12
	locals
		slot bi, dd
		slot blkid, dd
		slot cur, dd
		slot r, dd
	endl
	mov	rbx, [ctx]
	mov	dword [bi], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.alldone
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	r12, rax
	mov	ecx, [r12 + BfaBlock.first_phi]
	mov	[cur], ecx
  .philoop:
	mov	eax, [cur]
	test	eax, eax
	jz	.bodystart
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	rdi, rbx
	mov	esi, [cur]
	call	__bfa_verify_passA_inst
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.philoop
  .bodystart:
	mov	ecx, [r12 + BfaBlock.first]
	mov	[cur], ecx
  .bodyloop:
	mov	eax, [cur]
	test	eax, eax
	jz	.nextblock
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	rdi, rbx
	mov	esi, [cur]
	call	__bfa_verify_passA_inst
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.bodyloop
  .nextblock:
	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.blockloop
  .alldone:
	mov	eax, 0
	return
  .ret:
	mov	eax, [r]
	return
endp

; ============================================================================
; rule 9 -- no `nop`. A raw whole-`insts`-Vec scan (FINDING 2, this file's
; header: `Inst.op == 0` is this file's inference for "removed"), so an
; instruction that was pushed but never linked into any block's phi/body
; list is still caught, not just ones a block walk would reach.
; ============================================================================
proc __bfa_verify_rule9, ctx
	uses	rbx
	locals
		slot i, dd
	endl
	mov	rbx, [ctx]
	mov	dword [i], 0
  .loop:
	mov	eax, [i]
	cmp	eax, [rbx + BfaVerifyCtx.ninsts]
	jge	.alldone
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [i]
	inc	eax
	mov	esi, eax
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_NOP
	jne	.notnop
	mov	dword [rbx + BfaVerifyCtx.out_blk], 0
	mov	eax, [i]
	inc	eax
	mov	[rbx + BfaVerifyCtx.out_inst], eax
	mov	eax, BFA_VERIFY_R9_NONOP
	return
  .notnop:
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.loop
  .alldone:
	mov	eax, 0
	return
endp

; ============================================================================
; bfa_verify_func -- top-level per-function entry point. See this file's
; header for the pass order and why it is fixed.
; ============================================================================

; __bfa_verify_ctx_init(ctx, module, func, varena) -- fills nblocks/ninsts;
; every other ctx field is left zero (BfaModule.arena-style convention:
; each later pass allocates and fills its own field only when it runs).
proc __bfa_verify_ctx_init, ctx, module, func, varena
	locals
	endl
	mov	rdi, [ctx]
	mov	rcx, sizeof.BfaVerifyCtx
	xor	rax, rax
	cld
	rep	stosb

	mov	rdi, [ctx]
	mov	rax, [module]
	mov	[rdi + BfaVerifyCtx.module], rax
	mov	rax, [func]
	mov	[rdi + BfaVerifyCtx.func], rax
	mov	rax, [varena]
	mov	[rdi + BfaVerifyCtx.varena], rax

	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.blocks]
	mov	rax, [rdi + Vec.len]
	mov	rdi, [ctx]
	mov	[rdi + BfaVerifyCtx.nblocks], eax

	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	mov	rdi, [ctx]
	mov	[rdi + BfaVerifyCtx.ninsts], eax
	return
endp

; bfa_verify_func(module, func, varena, out_blk, out_inst) -> eax = rule id
; (0 = ok). `out_blk`/`out_inst` are caller-owned dd pointers, written
; unconditionally (0 on success).
proc bfa_verify_func, module, func, varena, out_blk, out_inst
	uses	rbx, r12, r13
	locals
		slot ctx, dq
		slot r, dd
	endl
	; A function with no blocks is a DECLARATION -- ssa-ir.md section 2.11's
	; `;' form, "nothing is emitted for it" -- and has nothing to verify.
	; Falling through built a ctx whose idom vec is sized by nblocks, so
	; rule 4's first vec_get(idom, 0) trapped and rule 4's convergence bound
	; was `1 le 0`. Every bodiless `externus` the lowering emits is this
	; shape (found by compiler/x86_64/lower/, which had to skip three
	; functions and say so in its fixture).
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.blocks]
	test	rdi, rdi
	jz	.no_blocks
	mov	rax, [rdi + Vec.len]
	test	rax, rax
	jnz	.has_blocks
  .no_blocks:
	mov	rdi, [out_blk]
	mov	dword [rdi], 0
	mov	rdi, [out_inst]
	mov	dword [rdi], 0
	xor	eax, eax
	return
  .has_blocks:

	mov	rdi, [varena]
	mov	rsi, sizeof.BfaVerifyCtx
	call	arena_alloc
	mov	[ctx], rax
	mov	r12, rax

	mov	rdi, r12
	mov	rsi, [module]
	mov	rdx, [func]
	mov	rcx, [varena]
	call	__bfa_verify_ctx_init

	mov	rdi, r12
	call	__bfa_verify_rule9
	mov	[r], eax
	test	eax, eax
	jnz	.fail

	mov	rdi, r12
	call	__bfa_verify_passA
	mov	[r], eax
	test	eax, eax
	jnz	.fail

	mov	rdi, r12
	call	__bfa_verify_rule2
	mov	[r], eax
	test	eax, eax
	jnz	.fail

	mov	rdi, r12
	call	__bfa_verify_build_predvecs

	mov	rdi, r12
	call	__bfa_verify_rule3
	mov	[r], eax
	test	eax, eax
	jnz	.fail

	mov	rdi, r12
	call	__bfa_verify_build_instblock

	mov	rdi, r12
	call	__bfa_verify_build_domtree

	mov	rdi, r12
	call	__bfa_verify_rule4
	mov	[r], eax
	test	eax, eax
	jnz	.fail

	mov	rdi, r12
	call	__bfa_verify_rule5a
	mov	[r], eax
	test	eax, eax
	jnz	.fail

	mov	rdi, r12
	call	__bfa_verify_build_postdom

	mov	rdi, r12
	call	__bfa_verify_rule5b
	mov	[r], eax
	test	eax, eax
	jnz	.fail

	mov	rdi, r12
	call	__bfa_verify_rule8
	mov	[r], eax
	test	eax, eax
	jnz	.fail

	mov	dword [r], 0
	mov	rax, [out_blk]
	mov	dword [rax], 0
	mov	rax, [out_inst]
	mov	dword [rax], 0
	mov	eax, 0
	return
  .fail:
	mov	rax, [out_blk]
	mov	ecx, [r12 + BfaVerifyCtx.out_blk]
	mov	[rax], ecx
	mov	rax, [out_inst]
	mov	ecx, [r12 + BfaVerifyCtx.out_inst]
	mov	[rax], ecx
	mov	eax, [r]
	return
endp

; ============================================================================
; rule 2 -- exactly one terminator per block, and it is last.
; ============================================================================

; __bfa_verify_is_terminator(op) -> eax = 1 iff op is jmp/br/ret/trap.
proc __bfa_verify_is_terminator, op
	locals
	endl
	mov	eax, [op]
	cmp	eax, BFA_OP_JMP
	je	.yes
	cmp	eax, BFA_OP_BR
	je	.yes
	cmp	eax, BFA_OP_RET
	je	.yes
	cmp	eax, BFA_OP_TRAP
	je	.yes
	mov	eax, 0
	return
  .yes:
	mov	eax, 1
	return
endp

proc __bfa_verify_rule2, ctx
	uses	rbx, r12
	locals
		slot bi, dd
		slot blkid, dd
		slot cur, dd
		slot lastid, dd
		slot op, dd
	endl
	mov	rbx, [ctx]
	mov	dword [bi], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.alldone
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	r12, rax
	mov	ecx, [r12 + BfaBlock.first]
	test	ecx, ecx
	jnz	.havebody
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	dword [rbx + BfaVerifyCtx.out_inst], 0
	mov	eax, BFA_VERIFY_R2_TERM
	return
  .havebody:
	mov	ecx, [r12 + BfaBlock.last]
	mov	[lastid], ecx
	mov	ecx, [r12 + BfaBlock.first]
	mov	[cur], ecx
  .bodyloop:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	edi, [op]
	call	__bfa_verify_is_terminator
	mov	ecx, [cur]
	cmp	ecx, [lastid]
	jne	.notlast
	test	eax, eax
	jnz	.nextinst
	jmp	.viol
  .notlast:
	test	eax, eax
	jz	.nextinst
  .viol:
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	eax, BFA_VERIFY_R2_TERM
	return
  .nextinst:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	test	ecx, ecx
	jnz	.bodyloop

	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.blockloop
  .alldone:
	mov	eax, 0
	return
endp

; ============================================================================
; successors + predecessor lists. ONLY called after rule 1 AND rule 2 have
; both passed -- see this file's header: rule 1 bounds every block id a
; terminator names, rule 2 confirms `BfaBlock.last` really is the
; terminator, so reading successors off it here never reaches an
; `rassert` this file cannot recover from.
; ============================================================================

; __bfa_verify_succs(ctx, blockid) -> eax = successor count (0/1/2),
; ecx = first successor (if count >= 1), edx = second (if count == 2).
; "Successors in terminator-operand order" (section 3's own phrase): `jmp`
; has one operand; `br` has [true, false], read off the SAME extra-pool
; slots emit.inc's own `__bfa_emit_br` reads in the same order to lower
; the same branch (this file's header cites that cross-check).
proc __bfa_verify_succs, ctx, blockid
	uses	rbx
	locals
		slot termid, dd
		slot op, dd
		slot eidx, dd
	endl
	mov	rbx, [ctx]
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [blockid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.last]
	mov	[termid], ecx

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [termid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx

	cmp	dword [op], BFA_OP_JMP
	jne	.notjmp
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [termid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	eax, 1
	xor	edx, edx
	return
  .notjmp:
	cmp	dword [op], BFA_OP_BR
	jne	.notbr
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [termid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	push	rax
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	edx, eax
	pop	rax
	mov	ecx, eax
	mov	eax, 2
	return
  .notbr:
	mov	eax, 0
	xor	ecx, ecx
	xor	edx, edx
	return
endp

; __bfa_verify_build_predvecs(ctx) -- allocates ctx.predvecs (Vec<dq>,
; nblocks entries, each -> Vec<dd>*), filled by WALKING EACH BLOCK'S OWN
; EDGE LIST (BfaBlock.first_pred -> BfaEdge.nextp), which ir.inc's
; `bfa_edge_push` builds in the order terminators were emitted. That is
; docs/design/ssa-ir.md section 2.4's definition of predecessor order, and
; this file's header (FINDING 1, and the PREDECESSOR ORDER note) records
; what stood here before: a scan of blocks 1..N in INDEX order reading each
; terminator's successors, because nothing populated the edge list. Now
; parse.inc does, at every terminator it emits, so the approximation is
; gone and rule 3 checks the real thing. For text this parser produces the
; two orders coincide (that note explains why); for IR a Braun-style
; on-the-fly builder produces they do not, and this is the order that is
; correct then.
proc __bfa_verify_build_predvecs, ctx
	uses	rbx
	locals
		slot outer, dq
		slot i, dd
		slot pb, dd
		slot e, dd
		slot enext, dd
		slot pv, dq
	endl
	mov	rbx, [ctx]
	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[outer], rax
	mov	[rbx + BfaVerifyCtx.predvecs], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 8
	mov	ecx, [rbx + BfaVerifyCtx.nblocks]
	call	vec_init

	mov	dword [i], 0
  .allocloop:
	mov	eax, [i]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.allocdone
	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[pv], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 4
	xor	rcx, rcx
	call	vec_init
	mov	rdi, [outer]
	call	vec_push
	mov	rcx, [pv]
	mov	[rax], rcx
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.allocloop
  .allocdone:

	mov	dword [i], 0
  .scanloop:
	mov	eax, [i]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.scandone
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [i]
	inc	eax
	mov	esi, eax
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_pred]
	mov	[e], ecx
  .edgeloop:
	cmp	dword [e], 0
	je	.nextscan
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	rdi, [rdi + BfaFunc.edges]
	mov	esi, [e]
	dec	esi
	call	vec_get
	mov	ecx, [rax + BfaEdge.pred]
	mov	[pb], ecx
	mov	ecx, [rax + BfaEdge.nextp]
	mov	[enext], ecx
	mov	rdi, [outer]
	mov	esi, [i]
	call	vec_get
	mov	rdi, [rax]
	call	vec_push
	mov	ecx, [pb]
	mov	[rax], ecx
	mov	ecx, [enext]
	mov	[e], ecx
	jmp	.edgeloop

  .nextscan:
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.scanloop
  .scandone:
	return
endp

; ============================================================================
; rule 3 -- phis first, one operand per predecessor, in predecessor order.
; ============================================================================
proc __bfa_verify_rule3, ctx
	uses	rbx, r12
	locals
		slot bi, dd
		slot blkid, dd
		slot cur, dd
		slot predvec, dq
		slot npreds, dd
		slot cnt, dd
		slot k, dd
		slot eidx, dd
		slot declpred, dd
		slot actualpred, dd
	endl
	mov	rbx, [ctx]
	mov	dword [bi], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.alldone
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	r12, rax

	mov	ecx, [r12 + BfaBlock.first_phi]
	mov	[cur], ecx
  .phimemloop:
	mov	eax, [cur]
	test	eax, eax
	jz	.phimemdone
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_PHI
	je	.phimemok
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	eax, BFA_VERIFY_R3_PHI
	return
  .phimemok:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.phimemloop
  .phimemdone:

	mov	ecx, [r12 + BfaBlock.first]
	mov	[cur], ecx
  .bodymemloop:
	mov	eax, [cur]
	test	eax, eax
	jz	.bodymemdone
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_PHI
	jne	.bodymemok
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	eax, BFA_VERIFY_R3_PHI
	return
  .bodymemok:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.bodymemloop
  .bodymemdone:

	mov	rdi, [rbx + BfaVerifyCtx.predvecs]
	mov	eax, [blkid]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	rcx, [rax]
	mov	[predvec], rcx
	mov	rax, [predvec]
	mov	rax, [rax + Vec.len]
	mov	[npreds], eax

	mov	ecx, [r12 + BfaBlock.first_phi]
	mov	[cur], ecx
  .phiorderloop:
	mov	eax, [cur]
	test	eax, eax
	jz	.nextblock
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[cnt], ecx

	mov	eax, [npreds]
	add	eax, eax
	cmp	eax, [cnt]
	je	.arityok
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	eax, BFA_VERIFY_R3_PHI
	return
  .arityok:
	mov	dword [k], 0
  .pairloop:
	mov	eax, [k]
	cmp	eax, [npreds]
	jge	.pairdone
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	mov	ecx, [k]
	add	eax, ecx
	add	eax, ecx
	mov	esi, eax
	call	bfa_extra_get
	mov	[declpred], eax

	mov	rdi, [predvec]
	mov	esi, [k]
	call	vec_get
	mov	ecx, [rax]
	mov	[actualpred], ecx

	mov	eax, [declpred]
	cmp	eax, [actualpred]
	je	.pairok
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	eax, BFA_VERIFY_R3_PHI
	return
  .pairok:
	mov	eax, [k]
	inc	eax
	mov	[k], eax
	jmp	.pairloop
  .pairdone:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.phiorderloop

  .nextblock:
	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.blockloop
  .alldone:
	mov	eax, 0
	return
endp

; ============================================================================
; instblock -- instid -> owning blockid. Safe to build any time after rules
; 1-3 pass (walks only phi/body linked lists, internal bookkeeping).
; ============================================================================
proc __bfa_verify_build_instblock, ctx
	uses	rbx, r12
	locals
		slot bi, dd
		slot blkid, dd
		slot cur, dd
	endl
	mov	rbx, [ctx]
	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaVerifyCtx.instblock], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 4
	mov	ecx, [rbx + BfaVerifyCtx.ninsts]
	call	vec_init
	mov	dword [bi], 0
  .initloop:
	mov	eax, [bi]
	cmp	eax, [rbx + BfaVerifyCtx.ninsts]
	jge	.initdone
	mov	rdi, [rbx + BfaVerifyCtx.instblock]
	call	vec_push
	mov	dword [rax], 0
	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.initloop
  .initdone:

	mov	dword [bi], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.alldone
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	r12, rax
	mov	ecx, [r12 + BfaBlock.first_phi]
	mov	[cur], ecx
  .philoop:
	mov	eax, [cur]
	test	eax, eax
	jz	.bodystart
	mov	rdi, [rbx + BfaVerifyCtx.instblock]
	mov	eax, [cur]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [blkid]
	mov	[rax], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.philoop
  .bodystart:
	mov	ecx, [r12 + BfaBlock.first]
	mov	[cur], ecx
  .bodyloop:
	mov	eax, [cur]
	test	eax, eax
	jz	.nextblock
	mov	rdi, [rbx + BfaVerifyCtx.instblock]
	mov	eax, [cur]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [blkid]
	mov	[rax], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.bodyloop
  .nextblock:
	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.blockloop
  .alldone:
	return
endp

; ============================================================================
; forward dominator tree -- Cooper-Harvey-Kennedy over reverse postorder,
; rooted at block 1. See this file's header, DETERMINISM.
; ============================================================================

; __bfa_verify_dfs_forward(ctx, visited, postorder, blockid) -- recursive
; postorder DFS, visiting successors in terminator-operand order.
proc __bfa_verify_dfs_forward, ctx, visited, postorder, blockid
	uses	rbx, r12, r13
	locals
		slot cnt, dd
		slot s0, dd
		slot s1, dd
	endl
	mov	rbx, [ctx]
	mov	r12, [visited]
	mov	r13, [postorder]

	mov	rdi, r12
	mov	eax, [blockid]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	byte [rax], 1

	mov	rdi, rbx
	mov	esi, [blockid]
	call	__bfa_verify_succs
	mov	[cnt], eax
	mov	[s0], ecx
	mov	[s1], edx

	cmp	dword [cnt], 0
	je	.nokids
	mov	rdi, r12
	mov	eax, [s0]
	dec	eax
	mov	esi, eax
	call	vec_get
	movzx	eax, byte [rax]
	test	eax, eax
	jnz	.skips0
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	mov	ecx, [s0]
	call	__bfa_verify_dfs_forward
  .skips0:
	cmp	dword [cnt], 2
	jne	.nokids
	mov	rdi, r12
	mov	eax, [s1]
	dec	eax
	mov	esi, eax
	call	vec_get
	movzx	eax, byte [rax]
	test	eax, eax
	jnz	.nokids
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	mov	ecx, [s1]
	call	__bfa_verify_dfs_forward
  .nokids:
	mov	rdi, r13
	call	vec_push
	mov	ecx, [blockid]
	mov	[rax], ecx
	return
endp

; __bfa_verify_dominates(ctx, a, b) -> eax = 1 iff block `a` dominates
; block `b` (a == b counts). If `b` is unreached from the entry, returns
; 0 unless a == b -- uses inside unreached (dead) code are not this file's
; concern (documented design choice, this file's header/report).
proc __bfa_verify_dominates, ctx, a, b
	uses	rbx
	locals
		slot cur, dd
	endl
	mov	rbx, [ctx]
	mov	eax, [a]
	cmp	eax, [b]
	jne	.notequal
	mov	eax, 1
	return
  .notequal:
	mov	rdi, [rbx + BfaVerifyCtx.rpo_index]
	mov	eax, [b]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	cmp	ecx, BFA_VERIFY_NOPOS
	jne	.reached
	mov	eax, 0
	return
  .reached:
	mov	eax, [b]
	mov	[cur], eax
  .loop:
	mov	eax, [cur]
	cmp	eax, [a]
	je	.yes
	mov	rdi, [rbx + BfaVerifyCtx.idom]
	mov	eax, [cur]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	cmp	ecx, [cur]
	jne	.advance
	mov	eax, 0
	return
  .advance:
	mov	[cur], ecx
	jmp	.loop
  .yes:
	mov	eax, 1
	return
endp

; __bfa_verify_domintersect(ctx, x0, y0) -> eax = the CHK "finger"
; intersection of forward-reachable blocks x0, y0 (both already assigned
; an idom).
; __bfa_verify_rpopos(ctx, blockid) -> eax = rpo_index[blockid]. A tiny
; wrapper so every caller below stores the result to a LOCAL SLOT before
; making a second `vec_get`-family call -- `vec_get` (rt/vec.inc) has no
; `uses` clause and freely clobbers `rcx` (it reads `Vec.len` through it),
; so a value merely left in `ecx`/`edx` across a SECOND such call is
; caller-saved and gone. FOUND BY RUNNING, NOT READING: an earlier version
; of `__bfa_verify_domintersect` read `rpo_index[x]` into `ecx`, then
; immediately called `vec_get` again for `rpo_index[y]` before comparing --
; the second call silently clobbered `ecx` with whatever `vec_get`'s own
; scratch happened to leave there, and the resulting bogus comparison hung
; `bfa_verify_func` in an infinite loop on the very first multi-predecessor
; fixture tried against it (`@dot`, four blocks). Confirmed by bisecting
; with syscall checkpoints (see the report) before this fix; every
; multi-`vec_get`/multi-`bfa_*`-call site in this file was re-audited
; against the same pattern afterward.
proc __bfa_verify_rpopos, ctx, blockid
	locals
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.rpo_index]
	mov	eax, [blockid]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	eax, [rax]
	return
endp

; __bfa_verify_idomof(ctx, blockid) -> eax = idom[blockid] (forward).
proc __bfa_verify_idomof, ctx, blockid
	locals
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.idom]
	mov	eax, [blockid]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	eax, [rax]
	return
endp

proc __bfa_verify_domintersect, ctx, x0, y0
	uses	rbx
	locals
		slot x, dd
		slot y, dd
		slot rx, dd
		slot ry, dd
	endl
	mov	rbx, [ctx]
	mov	eax, [x0]
	mov	[x], eax
	mov	eax, [y0]
	mov	[y], eax
  .outer:
	mov	eax, [x]
	cmp	eax, [y]
	je	.done
  .innerx:
	mov	rdi, rbx
	mov	esi, [x]
	call	__bfa_verify_rpopos
	mov	[rx], eax
	mov	rdi, rbx
	mov	esi, [y]
	call	__bfa_verify_rpopos
	mov	[ry], eax
	mov	eax, [rx]
	cmp	eax, [ry]
	jle	.innery
	mov	rdi, rbx
	mov	esi, [x]
	call	__bfa_verify_idomof
	mov	[x], eax
	jmp	.innerx
  .innery:
	mov	rdi, rbx
	mov	esi, [y]
	call	__bfa_verify_rpopos
	mov	[ry], eax
	mov	rdi, rbx
	mov	esi, [x]
	call	__bfa_verify_rpopos
	mov	[rx], eax
	mov	eax, [ry]
	cmp	eax, [rx]
	jle	.outer
	mov	rdi, rbx
	mov	esi, [y]
	call	__bfa_verify_idomof
	mov	[y], eax
	jmp	.innery
  .done:
	mov	eax, [x]
	return
endp

; __bfa_verify_build_domtree(ctx) -- fills ctx.rpo_index and ctx.idom.
proc __bfa_verify_build_domtree, ctx
	uses	rbx
	locals
		slot visited, dq
		slot postorder, dq
		slot order, dq
		slot i, dd
		slot pos, dd
		slot changed, dd
		slot b, dd
		slot npreds, dd
		slot predvec, dq
		slot k, dd
		slot p, dd
		slot newidom, dd
		slot havecand, dd
		slot iters, dd
		slot boundv, dd
	endl
	mov	rbx, [ctx]

	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaVerifyCtx.rpo_index], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 4
	mov	ecx, [rbx + BfaVerifyCtx.nblocks]
	call	vec_init
	mov	dword [i], 0
  .initrpo:
	mov	eax, [i]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.initrpodone
	mov	rdi, [rbx + BfaVerifyCtx.rpo_index]
	call	vec_push
	mov	dword [rax], BFA_VERIFY_NOPOS
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.initrpo
  .initrpodone:

	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaVerifyCtx.idom], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 4
	mov	ecx, [rbx + BfaVerifyCtx.nblocks]
	call	vec_init
	mov	dword [i], 0
  .initidom:
	mov	eax, [i]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.initidomdone
	mov	rdi, [rbx + BfaVerifyCtx.idom]
	call	vec_push
	mov	dword [rax], 0
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.initidom
  .initidomdone:

	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[visited], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 1
	mov	ecx, [rbx + BfaVerifyCtx.nblocks]
	call	vec_init
	mov	dword [i], 0
  .initvis:
	mov	eax, [i]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.initvisdone
	mov	rdi, [visited]
	call	vec_push
	mov	byte [rax], 0
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.initvis
  .initvisdone:

	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[postorder], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 4
	mov	ecx, [rbx + BfaVerifyCtx.nblocks]
	call	vec_init

	mov	rdi, rbx
	mov	rsi, [visited]
	mov	rdx, [postorder]
	mov	ecx, 1
	call	__bfa_verify_dfs_forward

	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[order], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 4
	mov	ecx, [rbx + BfaVerifyCtx.nblocks]
	call	vec_init

	mov	rax, [postorder]
	mov	rax, [rax + Vec.len]
	mov	[i], eax
	mov	dword [pos], 0
  .rporeverse:
	mov	eax, [i]
	test	eax, eax
	jz	.rpodone
	dec	eax
	mov	rdi, [postorder]
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	mov	[b], ecx
	mov	rdi, [rbx + BfaVerifyCtx.rpo_index]
	mov	eax, [b]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [pos]
	mov	[rax], ecx
	mov	rdi, [order]
	call	vec_push
	mov	ecx, [b]
	mov	[rax], ecx
	mov	eax, [pos]
	inc	eax
	mov	[pos], eax
	mov	eax, [i]
	dec	eax
	mov	[i], eax
	jmp	.rporeverse
  .rpodone:
	mov	rdi, [rbx + BfaVerifyCtx.idom]
	xor	esi, esi
	call	vec_get
	mov	dword [rax], 1

	; CHK's fixpoint converges in O(nblocks) passes (each pass either
	; changes at least one idom or the loop ends); `nblocks * nblocks`
	; is a generous, cheap-to-state upper bound. Loaded into a register
	; before the `rassert` per rt/arena.inc's own header ("load the
	; memory operand first rather than risk `[..]` inside the
	; condition"). This is a real internal-contract check, not
	; decoration: an earlier version of this proc's `.innerx`/`.innery`
	; loop (in `__bfa_verify_domintersect`) had a caller-saved-register
	; clobber bug (see that proc's own comment) that made the fixpoint
	; genuinely NOT converge -- it hung `bfa_verify_func` indefinitely on
	; the very first multi-predecessor fixture tried against it, with
	; nothing here to catch it before this assert was added. `rassert`
	; catches a repeat of that class of bug (or any other that breaks
	; convergence) as a trap instead of a silent hang.
	mov	eax, [rbx + BfaVerifyCtx.nblocks]
	imul	eax, eax
	mov	[boundv], eax
	mov	dword [iters], 0
  .fixloop:
	mov	eax, [iters]
	inc	eax
	mov	[iters], eax
	mov	edx, [boundv]
	rassert	eax le edx
	mov	dword [changed], 0
	mov	dword [pos], 1
  .fixblk:
	mov	rax, [order]
	mov	rcx, [rax + Vec.len]
	mov	eax, [pos]
	cmp	eax, ecx
	jge	.fixdonepass

	mov	rdi, [order]
	mov	esi, [pos]
	call	vec_get
	mov	ecx, [rax]
	mov	[b], ecx

	mov	rdi, [rbx + BfaVerifyCtx.predvecs]
	mov	eax, [b]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	rcx, [rax]
	mov	[predvec], rcx
	mov	rax, [predvec]
	mov	rax, [rax + Vec.len]
	mov	[npreds], eax

	mov	dword [havecand], 0
	mov	dword [newidom], 0
	mov	dword [k], 0
  .predloop:
	mov	eax, [k]
	cmp	eax, [npreds]
	jge	.preddone
	mov	rdi, [predvec]
	mov	esi, [k]
	call	vec_get
	mov	ecx, [rax]
	mov	[p], ecx

	mov	rdi, [rbx + BfaVerifyCtx.idom]
	mov	eax, [p]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	test	ecx, ecx
	jz	.prednext

	cmp	dword [havecand], 0
	jne	.havecandidate
	mov	eax, [p]
	mov	[newidom], eax
	mov	dword [havecand], 1
	jmp	.prednext
  .havecandidate:
	mov	rdi, rbx
	mov	esi, [newidom]
	mov	edx, [p]
	call	__bfa_verify_domintersect
	mov	[newidom], eax
  .prednext:
	mov	eax, [k]
	inc	eax
	mov	[k], eax
	jmp	.predloop
  .preddone:

	cmp	dword [havecand], 0
	je	.nextpos
	mov	rdi, [rbx + BfaVerifyCtx.idom]
	mov	eax, [b]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	cmp	ecx, [newidom]
	je	.nextpos
	mov	ecx, [newidom]
	mov	[rax], ecx
	mov	dword [changed], 1
  .nextpos:
	mov	eax, [pos]
	inc	eax
	mov	[pos], eax
	jmp	.fixblk
  .fixdonepass:
	cmp	dword [changed], 0
	jnz	.fixloop
	return
endp

; ============================================================================
; rule 4 -- every use dominated by its definition. `contrib`'s and
; `redfin`'s own handle operand are EXCLUDED here (they get rule 5 --
; see this proc's shape switch, below) even though the underlying check
; is the same dominance test; every other value operand of every shape
; goes through one of the two helpers below.
; ============================================================================

; __bfa_verify_check_dom_ordinary(ctx, defval, use_blockid, use_instid)
; -> eax = 1 ok, 0 violation. Same-block same-instruction-linked-list
; ordering: a phi always dominates every other instruction in its own
; block (phis execute "at the top"); otherwise def's internal id must be
; strictly less than use's (construction/parse order == program order
; within one block -- this file's header, DETERMINISM). Cross-block: the
; def's block must dominate the use's block. Uses inside an unreached
; block are skipped (vacuously ok) -- this file's header/report.
proc __bfa_verify_check_dom_ordinary, ctx, defval, use_blockid, use_instid
	uses	rbx
	locals
		slot defblk, dd
		slot defop, dd
	endl
	mov	rbx, [ctx]
	mov	rdi, [rbx + BfaVerifyCtx.rpo_index]
	mov	eax, [use_blockid]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	cmp	ecx, BFA_VERIFY_NOPOS
	jne	.usereached
	mov	eax, 1
	return
  .usereached:
	mov	rdi, [rbx + BfaVerifyCtx.instblock]
	mov	eax, [defval]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	mov	[defblk], ecx

	mov	eax, [defblk]
	cmp	eax, [use_blockid]
	jne	.crossblock
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [defval]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[defop], ecx
	cmp	dword [defop], BFA_OP_PHI
	je	.ok
	mov	eax, [defval]
	cmp	eax, [use_instid]
	jl	.ok
	mov	eax, 0
	return
  .crossblock:
	mov	rdi, rbx
	mov	esi, [defblk]
	mov	edx, [use_blockid]
	call	__bfa_verify_dominates
	return
  .ok:
	mov	eax, 1
	return
endp

; __bfa_verify_check_dom_phi(ctx, defval, predblock) -> eax = 1 ok, 0
; violation. A phi's i-th incoming value must be dominated by (or defined
; in) the i-th PREDECESSOR block, not by the phi's own block -- standard
; SSA phi-dominance (the value "arrives" at the end of the predecessor).
proc __bfa_verify_check_dom_phi, ctx, defval, predblock
	uses	rbx
	locals
		slot defblk, dd
	endl
	mov	rbx, [ctx]
	mov	rdi, [rbx + BfaVerifyCtx.rpo_index]
	mov	eax, [predblock]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	cmp	ecx, BFA_VERIFY_NOPOS
	jne	.predreached
	mov	eax, 1
	return
  .predreached:
	mov	rdi, [rbx + BfaVerifyCtx.instblock]
	mov	eax, [defval]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	mov	[defblk], ecx
	mov	rdi, rbx
	mov	esi, [defblk]
	mov	edx, [predblock]
	call	__bfa_verify_dominates
	return
endp

; __bfa_verify_r4_operand(ctx, defval, blockid, instid) -> eax = 0/4.
proc __bfa_verify_r4_operand, ctx, defval, blockid, instid
	locals
	endl
	mov	rdi, [ctx]
	mov	esi, [defval]
	mov	edx, [blockid]
	mov	ecx, [instid]
	call	__bfa_verify_check_dom_ordinary
	test	eax, eax
	jnz	.ok
	mov	eax, BFA_VERIFY_R4_DOM
	return
  .ok:
	mov	eax, 0
	return
endp

; __bfa_verify_r4_inst(ctx, blockid, instid) -> eax = 0/4. Per-shape
; switch, mirroring Pass A's but simpler: extract value operands, check
; dominance, skip everything Pass A already proved is either an
; immediate, a raw integer, or a block/global/function index (no def to
; dominate). `contrib`'s/`redfin`'s handle operand is deliberately
; skipped -- rule 5, below, owns it.
proc __bfa_verify_r4_inst, ctx, blockid, instid
	uses	rbx, r12
	locals
		slot op, dd
		slot shp, dd
		slot a, dd
		slot b, dd
		slot aux, dd
		slot eidx, dd
		slot cnt, dd
		slot k, dd
		slot v, dd
		slot r, dd
	endl
	mov	rbx, [ctx]
	mov	r12, [blockid]
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	mov	edi, ecx
	call	__bfa_op_shape
	mov	[shp], eax

	mov	eax, [shp]
	cmp	eax, BFA_SH_BIN
	je	.bin
	cmp	eax, BFA_SH_CMP
	je	.bin
	cmp	eax, BFA_SH_FCMP
	je	.bin
	cmp	eax, BFA_SH_UN
	je	.un
	cmp	eax, BFA_SH_FMA
	je	.fma
	cmp	eax, BFA_SH_CONTRIB
	je	.contrib
	cmp	eax, BFA_SH_REDFIN
	je	.redfin
	cmp	eax, BFA_SH_LOAD
	je	.load
	cmp	eax, BFA_SH_STORE
	je	.store
	cmp	eax, BFA_SH_LOADBITS
	je	.loadbits
	cmp	eax, BFA_SH_STOREBITS
	je	.storebits
	cmp	eax, BFA_SH_COPY
	je	.copy
	cmp	eax, BFA_SH_ADDR
	je	.addr
	cmp	eax, BFA_SH_INDEX
	je	.index
	cmp	eax, BFA_SH_CHK
	je	.chk
	cmp	eax, BFA_SH_RETAIN
	je	.un
	cmp	eax, BFA_SH_CALL
	je	.call
	cmp	eax, BFA_SH_CALLIND
	je	.callind
	cmp	eax, BFA_SH_RET
	je	.ret
	cmp	eax, BFA_SH_BR
	je	.br
	cmp	eax, BFA_SH_PHI
	je	.phi
	; slot/gaddr/jmp/trap/param/iconst/fconst/redinit -- no value operand
	mov	eax, 0
	return

  .bin:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[a], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[b], ecx
	movzx	ecx, word [rax + BfaInst.aux]
	mov	[aux], ecx
	test	dword [aux], 0x100
	jnz	.bin_skipa
	mov	rdi, rbx
	mov	esi, [a]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
  .bin_skipa:
	test	dword [aux], 0x200
	jnz	.ret0
	mov	rdi, rbx
	mov	esi, [b]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
	jmp	.ret0

  .un:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[a], ecx
	mov	rdi, rbx
	mov	esi, [a]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .fma:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	dword [k], 0
  .fmaloop:
	mov	eax, [k]
	cmp	eax, 3
	jge	.ret0
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, [k]
	mov	esi, eax
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
	mov	eax, [k]
	inc	eax
	mov	[k], eax
	jmp	.fmaloop

  .contrib:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.b]
	mov	[v], ecx
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .redfin:
	; `redfin`'s own handle operand gets the ORDINARY "def dominates use"
	; check here, same as any other operand (its def -- the `redinit` --
	; must dominate the `redfin` itself). This is DIFFERENT from, and
	; additional to, rule 5's "redfin post-dominates every contrib of
	; its handle" (a property of the redfin's block versus every
	; CONTRIB's block, not of the redfin's own operand) -- the two
	; together are both required for a well-formed reduction, and an
	; earlier version of this switch skipped this check entirely on the
	; mistaken assumption rule 5 already covered it (it does not: rule
	; 5a only scans `contrib`s). `contrib`'s OWN handle operand keeps its
	; rule-5-tagged check, immediately above.
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[v], ecx
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .load:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .store:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 3
	mov	esi, eax
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .loadbits:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .storebits:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 3
	mov	esi, eax
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .copy:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, 2
	mov	esi, eax
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .addr:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[v], ecx
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .index:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .chk:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[a], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[b], ecx
	mov	rdi, rbx
	mov	esi, [a]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
	mov	rdi, rbx
	mov	esi, [b]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .call:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[cnt], ecx
	mov	dword [k], 1
  .callloop:
	mov	eax, [k]
	cmp	eax, [cnt]
	jge	.ret0
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, [k]
	mov	esi, eax
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
	mov	eax, [k]
	inc	eax
	mov	[k], eax
	jmp	.callloop

  .callind:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[cnt], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
	mov	dword [k], 1
  .callindloop:
	mov	eax, [k]
	cmp	eax, [cnt]
	jge	.ret0
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, [k]
	mov	esi, eax
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	mov	[r], eax
	test	eax, eax
	jnz	.ret_r
	mov	eax, [k]
	inc	eax
	mov	[k], eax
	jmp	.callindloop

  .ret:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[v], ecx
	cmp	dword [v], 0
	je	.ret0
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .br:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[v], eax
	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, r12d
	mov	ecx, [instid]
	call	__bfa_verify_r4_operand
	return

  .phi:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [instid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[eidx], ecx
	mov	ecx, [rax + BfaInst.b]
	mov	[cnt], ecx
	mov	dword [k], 0
  .philoop:
	mov	eax, [k]
	cmp	eax, [cnt]
	jge	.ret0
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, [k]
	mov	esi, eax
	call	bfa_extra_get
	mov	[a], eax			; predecessor block id
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [eidx]
	add	eax, [k]
	inc	eax
	mov	esi, eax
	call	bfa_extra_get
	mov	[v], eax			; incoming value id

	mov	rdi, rbx
	mov	esi, [v]
	mov	edx, [a]
	call	__bfa_verify_check_dom_phi
	test	eax, eax
	jnz	.phiok
	mov	eax, BFA_VERIFY_R4_DOM
	return
  .phiok:
	mov	eax, [k]
	add	eax, 2
	mov	[k], eax
	jmp	.philoop

  .ret0:
	mov	eax, 0
	return
  .ret_r:
	mov	eax, [r]
	return
endp

; __bfa_verify_rule4(ctx) -> eax = 0/4. Whole-function walk, phi list then
; body list, block index order.
proc __bfa_verify_rule4, ctx
	uses	rbx, r12
	locals
		slot bi, dd
		slot blkid, dd
		slot cur, dd
		slot r, dd
	endl
	mov	rbx, [ctx]
	mov	dword [bi], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.alldone
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	r12, rax
	mov	ecx, [r12 + BfaBlock.first_phi]
	mov	[cur], ecx
  .philoop:
	mov	eax, [cur]
	test	eax, eax
	jz	.bodystart
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	rdi, rbx
	mov	esi, [blkid]
	mov	edx, [cur]
	call	__bfa_verify_r4_inst
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.philoop
  .bodystart:
	mov	ecx, [r12 + BfaBlock.first]
	mov	[cur], ecx
  .bodyloop:
	mov	eax, [cur]
	test	eax, eax
	jz	.nextblock
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	rdi, rbx
	mov	esi, [blkid]
	mov	edx, [cur]
	call	__bfa_verify_r4_inst
	mov	[r], eax
	test	eax, eax
	jnz	.ret
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.bodyloop
  .nextblock:
	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.blockloop
  .alldone:
	mov	eax, 0
	return
  .ret:
	mov	eax, [r]
	return
endp

; ============================================================================
; rule 5 -- each `contrib` dominated by its `redinit` (forward dominance,
; reusing rule 4's machinery); each `redfin` POST-dominating every
; `contrib` of its own handle. Post-dominance: CHK on the REVERSED graph,
; rooted at one synthetic exit node, id = nblocks+1 -- this file's header,
; DETERMINISM, and this section's own comments below, say exactly how its
; edges are built and in what order. `rumpe` out of a reduction loop has
; no IR representation yet (FINDING 5, this file's header): nothing here
; is under-checking a real program today, only silent about a future one.
; ============================================================================

; __bfa_verify_is_exit_block(ctx, blockid) -> eax = 1 iff this block's
; terminator is `ret`/`trap` (zero forward successors).
proc __bfa_verify_is_exit_block, ctx, blockid
	locals
	endl
	mov	rdi, [ctx]
	mov	esi, [blockid]
	call	__bfa_verify_succs
	test	eax, eax
	jnz	.no
	mov	eax, 1
	return
  .no:
	mov	eax, 0
	return
endp

; __bfa_verify_pdfs(ctx, visited, postorder, id) -- recursive postorder
; DFS over the REVERSED graph, node space {1..nblocks+1}, id = nblocks+1
; is the synthetic exit E. `visited`/`postorder` are indexed 0..nblocks
; (id-1), i.e. sized nblocks+1. E's reverse-successors are every forward
; EXIT block, ascending block-index order (this file's own, documented
; choice -- ssa-ir.md fixes no order for a node it never needed). A real
; block b's reverse-successors are its FORWARD PREDECESSORS
; (`ctx.predvecs[b-1]`) -- reversing a forward edge P->b gives b->P.
proc __bfa_verify_pdfs, ctx, visited, postorder, id
	uses	rbx, r12, r13
	locals
		slot nblocks, dd
		slot i, dd
		slot predvec, dq
		slot npreds, dd
		slot k, dd
		slot p, dd
	endl
	mov	rbx, [ctx]
	mov	r12, [visited]
	mov	r13, [postorder]
	mov	eax, [rbx + BfaVerifyCtx.nblocks]
	mov	[nblocks], eax

	mov	rdi, r12
	mov	eax, [id]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	byte [rax], 1

	mov	eax, [id]
	cmp	eax, [nblocks]
	jle	.realnode
	; id is E -- visit every forward exit block, ascending index
	mov	dword [i], 0
  .eloop:
	mov	eax, [i]
	cmp	eax, [nblocks]
	jge	.done
	mov	rdi, rbx
	mov	eax, [i]
	inc	eax
	mov	esi, eax
	call	__bfa_verify_is_exit_block
	test	eax, eax
	jz	.enext
	mov	rdi, r12
	mov	eax, [i]
	mov	esi, eax
	call	vec_get
	movzx	eax, byte [rax]
	test	eax, eax
	jnz	.enext
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	mov	eax, [i]
	inc	eax
	mov	ecx, eax
	call	__bfa_verify_pdfs
  .enext:
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.eloop

  .realnode:
	mov	rdi, [rbx + BfaVerifyCtx.predvecs]
	mov	eax, [id]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	rcx, [rax]
	mov	[predvec], rcx
	mov	rax, [predvec]
	mov	rax, [rax + Vec.len]
	mov	[npreds], eax
	mov	dword [k], 0
  .ploop:
	mov	eax, [k]
	cmp	eax, [npreds]
	jge	.done
	mov	rdi, [predvec]
	mov	esi, [k]
	call	vec_get
	mov	ecx, [rax]
	mov	[p], ecx
	mov	rdi, r12
	mov	eax, [p]
	dec	eax
	mov	esi, eax
	call	vec_get
	movzx	eax, byte [rax]
	test	eax, eax
	jnz	.pnext
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, r13
	mov	ecx, [p]
	call	__bfa_verify_pdfs
  .pnext:
	mov	eax, [k]
	inc	eax
	mov	[k], eax
	jmp	.ploop

  .done:
	mov	rdi, r13
	call	vec_push
	mov	ecx, [id]
	mov	[rax], ecx
	return
endp

; __bfa_verify_build_postdom(ctx) -- fills ctx.prpo_index/ctx.pidom, both
; sized nblocks+1 (index 0..nblocks-1 = real blocks 1..nblocks; index
; nblocks = the synthetic exit E, id nblocks+1). `pidom[E's slot]` is
; always E itself (root, self-loop, exactly like the forward tree's
; entry) and is never read by `__bfa_verify_postdominates` (a real block
; never equals E).
proc __bfa_verify_build_postdom, ctx
	uses	rbx
	locals
		slot visited, dq
		slot postorder, dq
		slot order, dq
		slot i, dd
		slot pos, dd
		slot changed, dd
		slot b, dd
		slot exitid, dd
		slot iters, dd
		slot boundv, dd
		slot npreds, dd
		slot k, dd
		slot p, dd
		slot havecand, dd
		slot newidom, dd
		slot cnt, dd
		slot s0, dd
		slot s1, dd
	endl
	mov	rbx, [ctx]
	mov	eax, [rbx + BfaVerifyCtx.nblocks]
	inc	eax
	mov	[exitid], eax

	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaVerifyCtx.prpo_index], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 4
	mov	ecx, [exitid]
	call	vec_init
	mov	dword [i], 0
  .initrpo:
	mov	eax, [i]
	cmp	eax, [exitid]
	jge	.initrpodone
	mov	rdi, [rbx + BfaVerifyCtx.prpo_index]
	call	vec_push
	mov	dword [rax], BFA_VERIFY_NOPOS
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.initrpo
  .initrpodone:

	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaVerifyCtx.pidom], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 4
	mov	ecx, [exitid]
	call	vec_init
	mov	dword [i], 0
  .initpidom:
	mov	eax, [i]
	cmp	eax, [exitid]
	jge	.initpidomdone
	mov	rdi, [rbx + BfaVerifyCtx.pidom]
	call	vec_push
	mov	dword [rax], BFA_VERIFY_NOPOS
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.initpidom
  .initpidomdone:

	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[visited], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 1
	mov	ecx, [exitid]
	call	vec_init
	mov	dword [i], 0
  .initvis:
	mov	eax, [i]
	cmp	eax, [exitid]
	jge	.initvisdone
	mov	rdi, [visited]
	call	vec_push
	mov	byte [rax], 0
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.initvis
  .initvisdone:

	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[postorder], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 4
	mov	ecx, [exitid]
	call	vec_init

	mov	rdi, rbx
	mov	rsi, [visited]
	mov	rdx, [postorder]
	mov	ecx, [exitid]
	call	__bfa_verify_pdfs

	mov	rdi, [rbx + BfaVerifyCtx.varena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[order], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaVerifyCtx.varena]
	mov	rdx, 4
	mov	ecx, [exitid]
	call	vec_init

	mov	rax, [postorder]
	mov	rax, [rax + Vec.len]
	mov	[i], eax
	mov	dword [pos], 0
  .rporeverse:
	mov	eax, [i]
	test	eax, eax
	jz	.rpodone
	dec	eax
	mov	rdi, [postorder]
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	mov	[b], ecx
	mov	rdi, [rbx + BfaVerifyCtx.prpo_index]
	mov	eax, [b]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [pos]
	mov	[rax], ecx
	mov	rdi, [order]
	call	vec_push
	mov	ecx, [b]
	mov	[rax], ecx
	mov	eax, [pos]
	inc	eax
	mov	[pos], eax
	mov	eax, [i]
	dec	eax
	mov	[i], eax
	jmp	.rporeverse
  .rpodone:

	; pidom[E] = E itself.
	mov	rdi, [rbx + BfaVerifyCtx.pidom]
	mov	eax, [exitid]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [exitid]
	mov	[rax], ecx

	mov	eax, [exitid]
	imul	eax, eax
	mov	[boundv], eax
	mov	dword [iters], 0
  .fixloop:
	mov	eax, [iters]
	inc	eax
	mov	[iters], eax
	mov	edx, [boundv]
	rassert	eax le edx
	mov	dword [changed], 0
	mov	dword [pos], 1
  .fixblk:
	mov	rax, [order]
	mov	rcx, [rax + Vec.len]
	mov	eax, [pos]
	cmp	eax, ecx
	jge	.fixdonepass

	mov	rdi, [order]
	mov	esi, [pos]
	call	vec_get
	mov	ecx, [rax]
	mov	[b], ecx

	; reverse-predecessors(b): forward-successors(b), plus E if b is a
	; forward exit block. b is never E here (E sits at rpo position 0,
	; loop starts at pos=1).
	mov	dword [havecand], 0
	mov	dword [newidom], 0

	mov	rdi, rbx
	mov	esi, [b]
	call	__bfa_verify_is_exit_block
	test	eax, eax
	jz	.notexit
	mov	eax, [exitid]
	mov	[newidom], eax
	mov	dword [havecand], 1
  .notexit:

	mov	rdi, rbx
	mov	esi, [b]
	call	__bfa_verify_succs
	mov	[cnt], eax
	mov	[s0], ecx
	mov	[s1], edx

	cmp	dword [cnt], 0
	je	.predsdone
	mov	rdi, rbx
	mov	esi, [s0]
	lea	rdx, [havecand]
	lea	rcx, [newidom]
	call	__bfa_verify_pd_maybecand
	cmp	dword [cnt], 2
	jne	.predsdone
	mov	rdi, rbx
	mov	esi, [s1]
	lea	rdx, [havecand]
	lea	rcx, [newidom]
	call	__bfa_verify_pd_maybecand
  .predsdone:

	cmp	dword [havecand], 0
	je	.nextpos
	mov	rdi, [rbx + BfaVerifyCtx.pidom]
	mov	eax, [b]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	cmp	ecx, [newidom]
	je	.nextpos
	mov	ecx, [newidom]
	mov	[rax], ecx
	mov	dword [changed], 1
  .nextpos:
	mov	eax, [pos]
	inc	eax
	mov	[pos], eax
	jmp	.fixblk
  .fixdonepass:
	cmp	dword [changed], 0
	jnz	.fixloop
	return

  ; local "macro-ish" helper block -- see __bfa_verify_pd_maybecand below;
  ; kept as a separate proc (fasmg procs cannot nest) called with `ctx`
  ; implicit via `rbx`/`havecand`/`newidom`/`p` all being THIS proc's own
  ; locals is not possible across a real `call` boundary, so those three
  ; are re-threaded explicitly -- see the proc.
endp

; __bfa_verify_pd_maybecand(ctx, p, havecand_ptr, newidom_ptr) -- if
; reverse-predecessor `p` (a real block, already known via `__bfa_verify_
; succs` to be a forward successor of `b`) has a PROCESSED post-idom
; (`pidom[p] != BFA_VERIFY_NOPOS`), folds it into `*newidom_ptr` exactly
; like the forward fixpoint's inline candidate logic -- pulled out to its
; own proc only because `__bfa_verify_build_postdom` above needed the
; same three-line "is this predecessor real (not E) and already assigned"
; test twice (successor 0, successor 1) and inlining it twice, by hand,
; is exactly the kind of copy-paste this file has already gotten wrong
; once (`__bfa_verify_domintersect`'s clobber bug, this file's own
; comment there).
proc __bfa_verify_pd_maybecand, ctx, p, havecand_ptr, newidom_ptr
	uses	rbx
	locals
	endl
	mov	rbx, [ctx]
	mov	rdi, [rbx + BfaVerifyCtx.pidom]
	mov	eax, [p]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	cmp	ecx, BFA_VERIFY_NOPOS
	je	.skip
	mov	rax, [havecand_ptr]
	cmp	dword [rax], 0
	jne	.intersect
	mov	rax, [newidom_ptr]
	mov	ecx, [p]
	mov	[rax], ecx
	mov	rax, [havecand_ptr]
	mov	dword [rax], 1
	return
  .intersect:
	mov	rax, [newidom_ptr]
	mov	ecx, [rax]
	push	rcx
	mov	rdi, rbx
	pop	rsi
	mov	edx, [p]
	call	__bfa_verify_postintersect
	mov	rcx, [newidom_ptr]
	mov	[rcx], eax
	return
  .skip:
	return
endp

; __bfa_verify_prpopos(ctx, id) -> eax = prpo_index[id] (id in 1..nblocks+1).
proc __bfa_verify_prpopos, ctx, id
	locals
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.prpo_index]
	mov	eax, [id]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	eax, [rax]
	return
endp

; __bfa_verify_pidomof(ctx, id) -> eax = pidom[id].
proc __bfa_verify_pidomof, ctx, id
	locals
	endl
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaVerifyCtx.pidom]
	mov	eax, [id]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	eax, [rax]
	return
endp

; __bfa_verify_postintersect(ctx, x0, y0) -> eax -- CHK finger algorithm
; on the reversed graph (prpo_index/pidom), identical in structure to
; __bfa_verify_domintersect (see that proc's own comment for why every
; intermediate value is flushed to a local slot before the NEXT call,
; never left sitting in a caller-saved register).
proc __bfa_verify_postintersect, ctx, x0, y0
	uses	rbx
	locals
		slot x, dd
		slot y, dd
		slot rx, dd
		slot ry, dd
	endl
	mov	rbx, [ctx]
	mov	eax, [x0]
	mov	[x], eax
	mov	eax, [y0]
	mov	[y], eax
  .outer:
	mov	eax, [x]
	cmp	eax, [y]
	je	.done
  .innerx:
	mov	rdi, rbx
	mov	esi, [x]
	call	__bfa_verify_prpopos
	mov	[rx], eax
	mov	rdi, rbx
	mov	esi, [y]
	call	__bfa_verify_prpopos
	mov	[ry], eax
	mov	eax, [rx]
	cmp	eax, [ry]
	jle	.innery
	mov	rdi, rbx
	mov	esi, [x]
	call	__bfa_verify_pidomof
	mov	[x], eax
	jmp	.innerx
  .innery:
	mov	rdi, rbx
	mov	esi, [y]
	call	__bfa_verify_prpopos
	mov	[ry], eax
	mov	rdi, rbx
	mov	esi, [x]
	call	__bfa_verify_prpopos
	mov	[rx], eax
	mov	eax, [ry]
	cmp	eax, [rx]
	jle	.outer
	mov	rdi, rbx
	mov	esi, [y]
	call	__bfa_verify_pidomof
	mov	[y], eax
	jmp	.innery
  .done:
	mov	eax, [x]
	return
endp

; __bfa_verify_postdominates(ctx, a, b) -> eax = 1 iff real block `a`
; post-dominates real block `b` (a == b counts). If `b` cannot reach the
; synthetic exit (no path to any `ret`/`trap`), returns 0 -- this file's
; own, documented choice (a `contrib` that can never complete a reduction
; is not validly post-dominated by anything, including its own `redfin`).
proc __bfa_verify_postdominates, ctx, a, b
	uses	rbx
	locals
		slot cur, dd
		slot exitid, dd
	endl
	mov	rbx, [ctx]
	mov	eax, [a]
	cmp	eax, [b]
	jne	.notequal
	mov	eax, 1
	return
  .notequal:
	mov	eax, [rbx + BfaVerifyCtx.nblocks]
	inc	eax
	mov	[exitid], eax

	mov	rdi, rbx
	mov	esi, [b]
	call	__bfa_verify_prpopos
	cmp	eax, BFA_VERIFY_NOPOS
	jne	.reached
	mov	eax, 0
	return
  .reached:
	mov	eax, [b]
	mov	[cur], eax
  .loop:
	mov	rdi, rbx
	mov	esi, [cur]
	call	__bfa_verify_pidomof
	cmp	eax, [exitid]
	je	.noexitmatch
	cmp	eax, [a]
	je	.yes
	mov	[cur], eax
	jmp	.loop
  .noexitmatch:
	mov	eax, 0
	return
  .yes:
	mov	eax, 1
	return
endp

; ---- rule 5a: each `contrib` dominated by its `redinit` (forward). ----
proc __bfa_verify_rule5a, ctx
	uses	rbx, r12
	locals
		slot bi, dd
		slot blkid, dd
		slot cur, dd
		slot op, dd
		slot h, dd
		slot hblk, dd
	endl
	mov	rbx, [ctx]
	mov	dword [bi], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.alldone
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	r12, rax
	mov	ecx, [r12 + BfaBlock.first]
	mov	[cur], ecx
  .bodyloop:
	mov	eax, [cur]
	test	eax, eax
	jz	.nextblock
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	cmp	dword [op], BFA_OP_CONTRIB
	jne	.notcontrib

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[h], ecx

	mov	rdi, [rbx + BfaVerifyCtx.instblock]
	mov	eax, [h]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	mov	[hblk], ecx

	; unreached contrib block -- vacuous (dead code, this file's policy)
	mov	rdi, [rbx + BfaVerifyCtx.rpo_index]
	mov	eax, [blkid]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	cmp	ecx, BFA_VERIFY_NOPOS
	jne	.checkdom
	jmp	.notcontrib
  .checkdom:
	mov	rdi, rbx
	mov	esi, [hblk]
	mov	edx, [blkid]
	call	__bfa_verify_dominates
	test	eax, eax
	jnz	.notcontrib
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	eax, BFA_VERIFY_R5_REDUCTION
	return
  .notcontrib:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.bodyloop
  .nextblock:
	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.blockloop
  .alldone:
	mov	eax, 0
	return
endp

; ---- rule 5b: each `redfin` post-dominates every `contrib` of its own
; handle. For each `redfin`, an O(ninsts) scan for `contrib`s sharing its
; handle value id is simple and correct at the sizes this pass runs at
; (test fixtures); a production-scale verifier would index contribs by
; handle instead -- [UNTESTED] at any size beyond this tree's fixtures.
proc __bfa_verify_rule5b, ctx
	uses	rbx, r12
	locals
		slot bi, dd
		slot blkid, dd
		slot cur, dd
		slot op, dd
		slot h, dd
		slot rfblk, dd
		slot j, dd
		slot cop, dd
		slot ch, dd
		slot cblk, dd
	endl
	mov	rbx, [ctx]
	mov	dword [bi], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.alldone
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	r12, rax
	mov	ecx, [r12 + BfaBlock.first]
	mov	[cur], ecx
  .bodyloop:
	mov	eax, [cur]
	test	eax, eax
	jz	.nextblock
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[op], ecx
	cmp	dword [op], BFA_OP_REDFIN
	jne	.notredfin

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[h], ecx
	mov	eax, [blkid]
	mov	[rfblk], eax

	mov	dword [j], 0
  .contribscan:
	mov	eax, [j]
	cmp	eax, [rbx + BfaVerifyCtx.ninsts]
	jge	.notredfin
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [j]
	inc	eax
	mov	esi, eax
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	mov	[cop], ecx
	cmp	dword [cop], BFA_OP_CONTRIB
	jne	.scannext
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	eax, [j]
	inc	eax
	mov	esi, eax
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	[ch], ecx
	mov	eax, [ch]
	cmp	eax, [h]
	jne	.scannext

	; this contrib shares our handle -- find its block, check post-dom
	mov	rdi, [rbx + BfaVerifyCtx.instblock]
	mov	eax, [j]
	mov	esi, eax
	call	vec_get
	mov	ecx, [rax]
	mov	[cblk], ecx

	mov	rdi, rbx
	mov	eax, [rfblk]
	mov	esi, eax
	mov	edx, [cblk]
	call	__bfa_verify_postdominates
	test	eax, eax
	jnz	.scannext
	mov	ecx, [cblk]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	eax, [j]
	inc	eax
	mov	[rbx + BfaVerifyCtx.out_inst], eax
	mov	eax, BFA_VERIFY_R5_REDUCTION
	return
  .scannext:
	mov	eax, [j]
	inc	eax
	mov	[j], eax
	jmp	.contribscan

  .notredfin:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.bodyloop
  .nextblock:
	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.blockloop
  .alldone:
	mov	eax, 0
	return
endp

; ============================================================================
; rule 8 -- `numeri` equal across every direct `call` (never `callind`:
; an indirect callee's `numeri` is not statically known -- this file's
; header, FINDING 4's neighbourhood).
; ============================================================================
proc __bfa_verify_rule8, ctx
	uses	rbx, r12, r13
	locals
		slot bi, dd
		slot blkid, dd
		slot cur, dd
		slot op, dd
		slot eidx, dd
		slot fnidx, dd
		slot callee, dq
	endl
	mov	rbx, [ctx]
	mov	dword [bi], 0
  .blockloop:
	mov	eax, [bi]
	cmp	eax, [rbx + BfaVerifyCtx.nblocks]
	jge	.alldone
	mov	eax, [bi]
	inc	eax
	mov	[blkid], eax

	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [blkid]
	call	bfa_block_ptr
	mov	r12, rax
	mov	ecx, [r12 + BfaBlock.first]
	mov	[cur], ecx
  .bodyloop:
	mov	eax, [cur]
	test	eax, eax
	jz	.nextblock
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	r13, rax
	movzx	ecx, word [r13 + BfaInst.op]
	mov	[op], ecx
	cmp	dword [op], BFA_OP_CALL
	jne	.notcall

	mov	eax, [r13 + BfaInst.a]
	mov	[eidx], eax
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [eidx]
	call	bfa_extra_get
	mov	[fnidx], eax

	mov	rdi, [rbx + BfaVerifyCtx.module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	eax, [fnidx]
	dec	eax
	mov	esi, eax
	call	vec_get
	mov	rcx, [rax]
	mov	[callee], rcx

	mov	rax, [rbx + BfaVerifyCtx.func]
	mov	ecx, [rax + BfaFunc.numeri0]
	mov	rdx, [callee]
	cmp	ecx, [rdx + BfaFunc.numeri0]
	jne	.mismatch
	mov	rax, [rbx + BfaVerifyCtx.func]
	mov	ecx, [rax + BfaFunc.numeri1]
	mov	rdx, [callee]
	cmp	ecx, [rdx + BfaFunc.numeri1]
	jne	.mismatch
	mov	rax, [rbx + BfaVerifyCtx.func]
	mov	ecx, [rax + BfaFunc.numeri2]
	mov	rdx, [callee]
	cmp	ecx, [rdx + BfaFunc.numeri2]
	jne	.mismatch
	mov	rax, [rbx + BfaVerifyCtx.func]
	mov	ecx, [rax + BfaFunc.numeri3]
	mov	rdx, [callee]
	cmp	ecx, [rdx + BfaFunc.numeri3]
	jne	.mismatch
	jmp	.notcall

  .mismatch:
	mov	ecx, [blkid]
	mov	[rbx + BfaVerifyCtx.out_blk], ecx
	mov	ecx, [cur]
	mov	[rbx + BfaVerifyCtx.out_inst], ecx
	mov	eax, BFA_VERIFY_R8_NUMERI
	return

  .notcall:
	mov	rdi, [rbx + BfaVerifyCtx.func]
	mov	esi, [cur]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.nextp]
	mov	[cur], ecx
	jmp	.bodyloop
  .nextblock:
	mov	eax, [bi]
	inc	eax
	mov	[bi], eax
	jmp	.blockloop
  .alldone:
	mov	eax, 0
	return
endp

; ============================================================================
; bfa_verify_module -- the production entry point (this file's header:
; "the caller decides, do not rassert inside the walk" is about
; bfa_verify_func; this is the one place that DOES trap). Calls
; bfa_verify_func for every function in `module`, in Vec order (index 0..
; funcs.len-1 -- declaration order, ssa-ir.md section 4's own table),
; and `rassert`s the verdict is 0 immediately after each call. Because
; `rassert`'s condition is elided under `RELEASE` (macros/assert.inc's
; header) but the CALL that produces `eax` is an ordinary statement
; outside that elided span, a `RELEASE` build of this proc still calls
; `bfa_verify_func` for every function -- it just never traps on what it
; finds, exactly like every other `rassert` call site in this tree
; (this file's header says so up front).
; ============================================================================
proc bfa_verify_module, module, varena
	uses	rbx, r12, r13
	locals
		slot i, dd
		slot n, dd
		slot funcptr, dq
		slot vmod_blk, dd
		slot vmod_inst, dd
		slot verdict, dd
	endl
	mov	rbx, [module]
	mov	r12, [varena]
	mov	r13, [rbx + BfaModule.funcs]
	mov	rax, [r13 + Vec.len]
	mov	[n], eax
	mov	dword [i], 0
  .loop:
	mov	eax, [i]
	cmp	eax, [n]
	jge	.done
	mov	rdi, r13
	mov	esi, [i]
	call	vec_get
	mov	rcx, [rax]
	mov	[funcptr], rcx

	mov	rdi, rbx
	mov	rsi, [funcptr]
	mov	rdx, r12
	lea	rcx, [vmod_blk]
	lea	r8, [vmod_inst]
	call	bfa_verify_func
	mov	[verdict], eax
	mov	eax, [verdict]
	rassert	eax eq 0

	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.loop
  .done:
	return
endp
