; compiler/x86_64/checker/types/prim.inc
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
; PASS 2, PART 1 -- THE PRIMITIVE TABLE, THE PRELUDE TABLE, AND TYPE-EXPRESSION
; INTERNING. `docs/design/checker.md` section 2.2's pass 2, finding 14:
;
;	"Raising `E0301` in type position would report every `i32` in every
;	 program, so pass 1 leaves a type-position miss silent and PASS 2 OWNS
;	 THE PRIMITIVE TABLE (its `int`/`float`/`textus` kinds ARE that table)."
;
; THE HEAD OF THIS MODULE'S CHAIN. `checker/verify.inc` states the pass chain
; -- resolve <- types <- rows <- lexicon <- verify. This file is where `types`
; joins it: it includes the PREVIOUS LINK (`../resolve/resolve.inc`) and
; nothing else from `checker/`, so the graph stays a chain and never a diamond
; (fasmg has one flat namespace; a `struct` or `proc` processed twice by two
; paths fails on the second). `rows/rowdefs.inc`'s `include` line moved from
; `../resolve/resolve.inc` to `../types/types.inc` in the same change, exactly
; as its own header instructed.
;
; It also includes `../../prelude/interface.inc`, which is PART A CONSTANTS
; ONLY unless the includer defines `EXS_IFACE_EMIT_DECLS` (that file's own
; header). Nothing else in the tree includes it, so this is not a second path
; to anything; and because `ast/kinds.inc` has been processed by the time it
; arrives, its `if defined AST_D_FN` block fires and cross-checks the two
; copies of the declaration kinds and the atom ordinals for free.
;
; =============================================================================
; 1. THE PRIMITIVE TABLE -- what a type-position name means when nothing
;    declares it
; =============================================================================
;
; `u1`-`u64` never reach here: spec §8.6 makes `u` + admitted digits a
; `BitType`, the parser builds `AST_TYBIT` with the width in `aux`, and `u12`
; does not parse at all (`EXS-E0201`, cst/parse.inc). Everything else in spec
; §5.1's and §5.2's vocabulary parses as `TyPath -> Path -> Seg` and NOTHING in
; this repository declares it, so a spelling is all there is to go on:
;
;	i1..i8 i16 i24 i32 i40 i48 i56 i64   `int`, sign = i, width = N
;	f32 f64                              `float`
;	mensura                              `mensura`, width = --hospes's pointer bits
;	textus octeti scalares grapha        four distinct kinds (spec §5.1)
;	unit                                 `unit`
;	refero<T> refero_communis<T>         `ref` / `refc` (spec §6.4)
;	eventus<T>                           `eventus`
;	acies<T, N>                          `acies`, lane count in the type (spec §5.4)
;
; THE SIGNED WIDTHS MIRROR §5.2'S UNSIGNED ONES, and that is a decision, not a
; reading. §8.6's `BitType` rule is written for `u` alone; §5.2's four bit-width
; rules are written for `uN`; nothing in the spec enumerates the signed widths
; at all, and typed-ast.md section 2.5 says only "`uN` `iN`, 1 <= N <= 64". Two
; choices were available -- admit every width 1..64, or admit exactly the set
; §5.2 rule 2 admits -- and the second is taken, because admitting `i12` while
; `u12` "does not parse" would make the two halves of one table disagree for
; no stated reason. `i12` is therefore an unknown type NAME and gets `EXS-E0301`
; here rather than `EXS-E0201` at the parser: a difference in code and in
; column that is recorded as a finding rather than smoothed over.
;
; `octetus` IS DELIBERATELY ABSENT. Spec §5.3's own example writes
; `functio strlen(s: *octetus) -> mensura`, and no section of the spec defines
; `octetus` as a type -- §5.1 defines `octeti`, the plural view. Guessing that
; it means `u8` would put a type in the language that nothing declares and no
; section names; the gap is reported instead, and a program that writes
; `*octetus` gets `EXS-E0301` at the segment, which is the honest answer.
;
; =============================================================================
; 2. THE PRELUDE TABLE -- the representation, and why the tag
; =============================================================================
;
; `docs/design/runtime.md` 2.2 and spec §12: the prelude's names are
; "pre-seeded declarations in a scope OUTSIDE the module", which a module's own
; declaration SHADOWS rather than colliding with. `prelude/interface.inc` part
; B serializes seven of them and says its serialization "is the checker agent's
; to accept or replace". It is ACCEPTED as the source of the facts (part A's
; offsets and part B's row order) and REPLACED as the storage: this file
; carries its own name pool and builds real `Ast.types` entries, because a
; `Decl` record in a second table would need a second set of accessors for
; every consumer.
;
; **THE REPRESENTATION IS A TAGGED `d` ON A `Seg`/`Path`, AND A SIDE TABLE ON A
; `Member`.** `Seg.d` and `Path.d` hold `CHK_TY_PRELUDE or k`, where `k` is the
; 1-based row index of `prelude/interface.inc` part B (`EXS_IFACE_D_* + 1`).
; `Member.d` holds **0** and `ChkTyWork.wpre[node]` holds `k`.
;
; THE SPLIT IS NOT A PREFERENCE; IT WAS MEASURED. `Member.d` began as a tagged
; value too, and `examples/initium.exsc` then trapped inside PASS 3:
; `__chk_row_contrib`'s `.named` arm (checker/rows/compute.inc, the line after
; `cmp ecx, AST_MEMBER`) reads a CALLEE's `d` and hands it straight to
; `ast_decl_at`, whose bounds `rassert` fired on `0x80000004` for
; `m.ambitus()`. That path is one of the five compute.inc's own header marks
; `[UNTESTED]`, and it is a defect this pass FOUND rather than caused -- but
; the fix is one line in another agent's file (test bit 31 before the
; dereference and fall to `.indirect`), so what is done HERE is to keep
; `Member.d` at 0, which sends that arm to `.indirect`, where it reads the
; member's TYPE and gets the same answer through the row in the `fn` type. The
; defect and its one-line fix are in the report; when it lands, `Member.d` can
; carry the tag and this side table can go.
;
; WHY NOT PUSH `AstDecl` RECORDS. Because `ast_verify`'s "the atoms are the
; last eleven declarations" must survive, and `ast_cap_base` (ast/build.inc) is
; a BOUNDS CHECK on exactly that -- `decls.len - 11 + 1`, kind-checked -- not a
; scan. Seven declarations appended after Stage 1 move the atoms off the end
; and `ast_cap_base` answers 0, which silently turns every capability lookup,
; every row item and the whole of pass 3 into a no-op. Appending a second copy
; of the eleven atoms after them does not help either: pass 1's `RowItem.d` and
; `Sub.d` already name the FIRST copy, and `__chk_row_masks` computes an atom's
; bit as `id - ast_cap_base(tree)`, which would then go negative and trap. So
; the atoms stay last and a prelude reference is tagged.
;
; WHY BIT 31 IS FREE. The same argument resolve.inc's `CHK_MEMB_AMBIG` makes:
; `Ast.decls` cannot reach 2^31 entries in a 16 MiB arena (`AST_ARENA_HINT`,
; ast/node.inc) -- one `AstDecl` is 32 bytes, so 2^31 of them is 64 GiB.
;
; WHAT THIS COSTS ELSEWHERE, stated exactly rather than left to be found:
;
;   - `chk_verify`'s `__chk_v_path` asserts that a filled `d` names a
;     declaration that exists. One line was added there -- `btr r13, 31`,
;     immediately after the `jz .unresolved` -- which drops the tag and leaves
;     the row index, a number in 1..7 that is always <= `ast_decl_count` (the
;     eleven atoms alone make the count >= 11). A one-segment `Path`'s
;     `rassert r14 eq r13` still compares two TAGGED values and is unaffected,
;     because it runs before that line. That edit is outside this agent's tree
;     and is reported.
;   - `chk_prelude_lookup` (resolve.inc) is the hook pass 1 left; its body now
;     forwards to `chk_ty_prelude_lookup` below. Also outside this tree, also
;     reported. WITHOUT IT, `Scriptor.ad_exitum(a)` in `examples/initium.exsc`
;     is `EXS-E0301` raised by PASS 1, and no amount of work in pass 2 can
;     retract a diagnostic another pass already buffered.
;   - Anything that dereferences a `Seg.d`/`Path.d`/`Member.d` must test bit 31
;     FIRST. Inside this module that is `__chk_ty_is_pre`. Pass 3 does not read
;     those slots (compute.inc's use-list header: "Re-scanning `Path` nodes
;     would double-count every draw"), which is why nothing there changed.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.1, §5.2, §5.3, §5.4, §5.5, §6.4,
; §8.6 (the `Type` grammar), §9.5 (`mensura` is `--hospes`'s), §12 (the prelude
; scope), §13; docs/design/checker.md sections 2.2, 2.4, 2.5, 2.6, finding 14;
; docs/design/runtime.md 2.2-2.5; docs/design/typed-ast.md section 2.5.
; -----------------------------------------------------------------------------

include '../resolve/resolve.inc'
include '../../prelude/interface.inc'

; ---- the prelude tag -------------------------------------------------------
CHK_TY_PRELUDE	= 0x80000000
CHK_TY_PRE_MASK	= 0x7FFFFFFF

; 1-based row indices into `prelude/interface.inc` part B, so that a tagged
; `d` is `CHK_TY_PRELUDE or CHK_TY_PRE_*`. Written as that file's own
; constants plus one rather than as literals: if a row is ever inserted there,
; this file follows without an edit.
CHK_TY_PRE_SCRIPTOR	= EXS_IFACE_D_SCRIPTOR + 1
CHK_TY_PRE_SCR_A	= EXS_IFACE_D_SCRIPTOR_A + 1
CHK_TY_PRE_SCR_FD	= EXS_IFACE_D_SCRIPTOR_DESCRIPTOR + 1
CHK_TY_PRE_AMBITUS	= EXS_IFACE_D_MUNDUS_AMBITUS + 1
CHK_TY_PRE_AD_EXITUM	= EXS_IFACE_D_AD_EXITUM + 1
CHK_TY_PRE_SCRIBE	= EXS_IFACE_D_SCRIBE + 1
CHK_TY_PRE_ALLOC	= EXS_IFACE_D_ALLOC_NOVUM + 1
CHK_TY_PRE_COUNT	= EXS_IFACE_DECL_COUNT

; ---- primitive pool indices ------------------------------------------------
CHK_TY_P_MENSURA	= 1
CHK_TY_P_TEXTUS		= 2
CHK_TY_P_OCTETI		= 3
CHK_TY_P_SCALARES	= 4
CHK_TY_P_GRAPHA		= 5
CHK_TY_P_UNIT		= 6
CHK_TY_P_F32		= 7
CHK_TY_P_F64		= 8
CHK_TY_P_REFERO		= 9
CHK_TY_P_REFC		= 10
CHK_TY_P_EVENTUS	= 11
CHK_TY_P_ACIES		= 12

; ---- the alias-resolution state, one byte per declaration ------------------
CHK_TY_A_NEW	= 0
CHK_TY_A_BUSY	= 1	; a `typus` currently being resolved -- a cycle
CHK_TY_A_DONE	= 2

; ---- definite assignment, one byte per declaration (spec §13's `E0307`) ----
; `CHK_TY_D_SET` is also what a REPORTED violation is left as, so that one
; uninitialised binding read three times is one diagnostic and not three.
CHK_TY_D_SET	= 0	; assigned, initialised, or not a binding at all
CHK_TY_D_UNSET	= 1	; declared with an annotation and no initializer

CHK_TY_IMPL_BUCKETS = 64

; ---- how deep a `TySuffix` chain or an alias chain may go ------------------
; Both are bounded rather than trusted: spec §8.6's `Type ::= ... (':' IDENT |
; 'apud' IDENT)*` admits any number of suffixes, and a `typus` chain is only
; acyclic because this pass checks it.
CHK_TY_DEPTH_MAX = 32

; =============================================================================
; the working state
; =============================================================================
; Allocated from `ChkCtx.arena` (the checker's scratch), exactly as
; `ChkRowWork` is, and for the same reason: `ChkCtx` is `checker-resolve`'s
; record and a pass does not grow another agent's struct to hold its own
; bookkeeping.
struct ChkTyWork
	field wcx,    dq	; -> ChkCtx
	field wtre,   dq	; -> Ast
	field wres,   dd	; the enclosing function's RESULT type id, 0 outside one
	field wfnd,   dd	; the enclosing `Fn`/`Lambda` DECLARATION, 0 at module level
	field wredd,  dd	; 1 if any `redde` was seen in the current body
	field wloop,  dd	; loop nesting depth (spec §8.5: `rumpe`/`perge`)
	field wcon,   dd	; nesting depth of loops CARRYING a `contrahe` (§5.4)
	field wext,   dd	; 1 while inside an `externus` signature (`EXS-E0520`)
	field wpb,    dd	; --hospes pointer width in bits; `mensura` needs it
	field wdep,   dd	; alias / suffix recursion depth
	field wnpar,  dd	; the enclosing `Sig`'s parameter count, for `sicut f`
	field wsigp,  dd	; that `Sig`'s parameter list `extra` offset
	field wnint,  dd	; the interner's live id count, for the bounds guard
	field winfer, dd	; 1 while a lambda's result type is being INFERRED
	field winf,   dd	; the type of that lambda's first `redde`
	field wpad,   dd	; so the `Map` and the three `Vec`s below start on
				; an eight-byte boundary: `map_init` and `vec_init`
				; write qwords, and this struct is byte-packed
				; (macros/struct.inc inserts nothing)
	wimpl  Map		; 8 key bytes (iface decl, target type id) -> impl decl
	field wikey,  dq	; stable key block for the map above
	field wikn,   dq	; the next free 8-byte key slot in it
	wast   Vec		; db per declaration -- CHK_TY_A_*
	wdef   Vec		; db per declaration -- CHK_TY_D_*, definite assignment
	wpre   Vec		; db per NODE -- the prelude row a `Member` resolved
				; to, 0 for every other node. See below.
end struct
; 16 + 14*4 + 32 + 16 + 3*40 = 240. Asserted rather than assumed, the way
; every other record in `checker/` is (resolve.inc's `ChkFrame`, `ChkDiag`,
; `ChkUse`; rows/rowdefs.inc's `ChkRowItem`).
assert sizeof.ChkTyWork = 240

; ---- the primitive name pool ----------------------------------------------
; `db LEN, 'name'` runs in pool-index order, walked by `ast_name_id`
; (ast/node.inc). A pool and a linear scan, for ast/kinds.inc's reason: twelve
; entries is nothing, and a scan is the only lookup whose order cannot depend
; on a hash (CLAUDE.md, "Determinism is not optional").
;
; Between two `proc` bodies and never fallen into -- the placement
; diag/render.inc, ast/kinds.inc and resolve.inc all use.
chk_ty_prim_names:
	db 7,'mensura'
	db 6,'textus'
	db 6,'octeti'
	db 8,'scalares'
	db 6,'grapha'
	db 4,'unit'
	db 3,'f32'
	db 3,'f64'
	db 6,'refero'
	db 15,'refero_communis'
	db 7,'eventus'
	db 5,'acies'
CHK_TY_PRIM_NAMES_LEN = $ - chk_ty_prim_names

; ---- the prelude name pool -------------------------------------------------
; SAME ORDER AS `prelude/interface.inc` PART B's rows, so the pool index IS
; `EXS_IFACE_D_* + 1` and a tagged `d` needs no second table to print. That
; correspondence is asserted, not assumed: `CHK_TY_PRE_ALLOC` is the last row
; and the pool has exactly that many entries.
chk_ty_pre_names:
	db 8,'Scriptor'
	db 1,'a'
	db 10,'descriptor'
	db 7,'ambitus'
	db 9,'ad_exitum'
	db 6,'scribe'
	db 5,'alloc'
CHK_TY_PRE_NAMES_LEN = $ - chk_ty_pre_names
assert CHK_TY_PRE_ALLOC = 7
assert CHK_TY_PRE_COUNT = 7

; `arborea` and `ordinata` -- spec §5.4's two reduction shapes, §8.6's
; `['forma' IDENT [INT]]`. Not keywords (lexer/keywords.inc does not know
; them); the shape is an `IDENT` and this is the table that gives it meaning.
chk_ty_forma_names:
	db 8,'ordinata'
	db 7,'arborea'
CHK_TY_FORMA_NAMES_LEN = $ - chk_ty_forma_names
CHK_TY_FORMA_ORDINATA	= 1
CHK_TY_FORMA_ARBOREA	= 2

; =============================================================================
; construction
; =============================================================================

; chk_ty_work_init(w, cx) -- fill a `ChkTyWork` the caller has allocated from
; `ChkCtx.arena`.
proc chk_ty_work_init, chkt_w, chkt_cx
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_w]
	mov	r12, [chkt_cx]
	mov	[rbx + ChkTyWork.wcx], r12
	mov	rax, [r12 + ChkCtx.tree]
	mov	[rbx + ChkTyWork.wtre], rax
	mov	dword [rbx + ChkTyWork.wres], 0
	mov	dword [rbx + ChkTyWork.wfnd], 0
	mov	dword [rbx + ChkTyWork.wredd], 0
	mov	dword [rbx + ChkTyWork.wloop], 0
	mov	dword [rbx + ChkTyWork.wcon], 0
	mov	dword [rbx + ChkTyWork.wext], 0
	mov	dword [rbx + ChkTyWork.wdep], 0
	mov	dword [rbx + ChkTyWork.wnpar], 0
	mov	dword [rbx + ChkTyWork.wsigp], 0
	mov	dword [rbx + ChkTyWork.winfer], 0
	mov	dword [rbx + ChkTyWork.winf], 0
	mov	dword [rbx + ChkTyWork.wpad], 0
	mov	eax, [r12 + ChkCtx.ptrbits]
	mov	[rbx + ChkTyWork.wpb], eax

	; the interner's live id count -- see `__chk_ty_bytes`
	xor	eax, eax
	mov	rcx, [rbx + ChkTyWork.wtre]
	mov	rcx, [rcx + Ast.names]
	test	rcx, rcx
	jz	.no_names
	mov	rcx, [rcx + Interner.map]
	mov	rcx, [rcx + Map.entries]
	mov	rax, [rcx + Vec.len]
  .no_names:
	mov	[rbx + ChkTyWork.wnint], eax

	; the impl map: (interface declaration, target type id) -> impl decl
	mov	r13, [r12 + ChkCtx.arena]
	lea	rdi, [rbx + ChkTyWork.wimpl]
	mov	rsi, r13
	mov	rdx, CHK_TY_IMPL_BUCKETS
	call	map_init
	; stable keys: `rt/map.inc` BORROWS a key and never copies it, so a key
	; may not live in a `Vec` that `vec_push` can move -- one flat
	; allocation, eight bytes per declaration, never grown. The same fix
	; ast/types.inc made for its type keys and resolve.inc for `ChkCtx.keys`.
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_decl_count
	inc	rax
	shl	rax, 3
	mov	rdi, r13
	mov	rsi, rax
	call	arena_alloc
	mov	[rbx + ChkTyWork.wikey], rax
	mov	[rbx + ChkTyWork.wikn], rax

	; one alias-resolution state byte per declaration, 1-based
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_decl_count
	mov	r12, rax
	inc	r12
	lea	rdi, [rbx + ChkTyWork.wast]
	mov	rsi, r13
	mov	rdx, 1
	mov	rcx, r12
	call	vec_init
	mov	[rbx + ChkTyWork.wast.len], r12
	mov	rdi, [rbx + ChkTyWork.wast.data]
	mov	rcx, r12
	xor	eax, eax
	cld
	rep	stosb

	; and one definite-assignment state byte, same shape
	lea	rdi, [rbx + ChkTyWork.wdef]
	mov	rsi, r13
	mov	rdx, 1
	mov	rcx, r12
	call	vec_init
	mov	[rbx + ChkTyWork.wdef.len], r12
	mov	rdi, [rbx + ChkTyWork.wdef.data]
	mov	rcx, r12
	xor	eax, eax
	cld
	rep	stosb

	; one byte per NODE for the prelude-member side table
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_node_count
	mov	r12, rax
	inc	r12
	lea	rdi, [rbx + ChkTyWork.wpre]
	mov	rsi, r13
	mov	rdx, 1
	mov	rcx, r12
	call	vec_init
	mov	[rbx + ChkTyWork.wpre.len], r12
	mov	rdi, [rbx + ChkTyWork.wpre.data]
	mov	rcx, r12
	xor	eax, eax
	cld
	rep	stosb
	return
endp

; =============================================================================
; small accessors
; =============================================================================

; __chk_ty_node(w, n) -> rax = a pointer to node `n`. Valid until the next push
; on `Ast.nodes`, which nothing in pass 2 does.
proc __chk_ty_node, chkt_w2, chkt_n2
	locals
	endl
	mov	rdi, [chkt_w2]
	mov	rdi, [rdi + ChkTyWork.wtre]
	mov	rsi, [chkt_n2]
	call	ast_node_at
	return
endp

; __chk_ty_kind(w, n) -> rax = node `n`'s kind, 0 for node 0.
proc __chk_ty_kind, chkt_w3, chkt_n3
	locals
	endl
	cmp	qword [chkt_n3], 0
	je	.none
	mov	rdi, [chkt_w3]
	mov	rsi, [chkt_n3]
	call	__chk_ty_node
	movzx	eax, word [rax + AstNode.kind]
	return
  .none:
	xor	eax, eax
	return
endp

; __chk_ty_setty(w, n, ty) -- annotate node `n`. Node 0 is a no-op, which is
; what makes every optional-child call site one line instead of three.
proc __chk_ty_setty, chkt_w4, chkt_n4, chkt_t4
	locals
	endl
	cmp	qword [chkt_n4], 0
	je	.out
	mov	rdi, [chkt_w4]
	mov	rdi, [rdi + ChkTyWork.wtre]
	mov	rsi, [chkt_n4]
	mov	rdx, [chkt_t4]
	call	ast_node_ty
  .out:
	return
endp

; __chk_ty_ty(w, n) -> rax = node `n`'s type id, 0 for node 0.
proc __chk_ty_ty, chkt_w5, chkt_n5
	locals
	endl
	cmp	qword [chkt_n5], 0
	je	.none
	mov	rdi, [chkt_w5]
	mov	rsi, [chkt_n5]
	call	__chk_ty_node
	mov	eax, [rax + AstNode.ty]
	return
  .none:
	xor	eax, eax
	return
endp

; __chk_ty_setd(w, n, d) -- write node `n`'s declaration slot. `ast_node_cd`
; writes `c` AND `d`, and `c` is a live operand on `Seg`, `Member` and `Path`,
; so this writes the one slot directly.
proc __chk_ty_setd, chkt_w6, chkt_n6, chkt_d6
	locals
	endl
	cmp	qword [chkt_n6], 0
	je	.out
	mov	rdi, [chkt_w6]
	mov	rsi, [chkt_n6]
	call	__chk_ty_node
	mov	ecx, [chkt_d6]
	mov	[rax + AstNode.d], ecx
  .out:
	return
endp

; __chk_ty_is_pre(d) -> eax = 1 if `d` is a TAGGED prelude reference.
; EVERY dereference of a `Seg.d` / `Path.d` / `Member.d` in this module goes
; through this first -- see this file's header.
proc __chk_ty_is_pre, chkt_d7
	locals
	endl
	mov	rax, [chkt_d7]
	test	rax, rax
	jz	.no
	mov	rcx, CHK_TY_PRELUDE
	test	rax, rcx
	jz	.no
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_raise(w, code, n) -> rax = the buffered `ChkDiag`, so a caller may
; attach a note or a related span. Every diagnostic this pass raises goes
; through `chk_raise` with `CHK_P_TYPES`, so that `chk_flush` can put the
; whole checker's output in checker.md section 4's order -- pass number first,
; node index second. Nothing here writes to a descriptor, formats a message,
; or names a code in English (spec §8.3: tools match codes).
proc __chk_ty_raise, chkt_w8, chkt_c8, chkt_n8
	locals
	endl
	mov	rdi, [chkt_w8]
	mov	rdi, [rdi + ChkTyWork.wcx]
	mov	rsi, CHK_P_TYPES
	mov	rdx, [chkt_c8]
	mov	rcx, [chkt_n8]
	call	chk_raise
	return
endp

; __chk_ty_bytes(w, nameid) -> rax = the interned bytes, rdx = the length;
; rdx = 0 when the id names nothing.
;
; THE BOUNDS TEST IS NOT DEFENSIVE PROGRAMMING, it is what keeps checker.md
; section 6's testing strategy usable. `intern_bytes` `rassert`s an id past
; the last one assigned, and that is right for the compiler -- a bad id from
; `exsc`'s own code is a contract violation. But section 6 says "hand-built
; trees through `ast/load.inc` make every pass testable without a parser", and
; a hand-written dump carries whatever name ids its author wrote while the
; fixture's interner holds only what the fixture interned. Answering "no
; bytes" for such an id degrades to `EXS-E0301` at that segment, which is a
; visible, diagnosable outcome; trapping would make five existing `chk_*`
; fixtures die at `ud2` the moment this pass was wired in.
proc __chk_ty_bytes, chkt_w9, chkt_nm9
	uses	rbx
	locals
	endl
	mov	rbx, [chkt_w9]
	mov	rax, [chkt_nm9]
	test	rax, rax
	jz	.none
	mov	ecx, [rbx + ChkTyWork.wnint]
	cmp	rax, rcx
	ja	.none
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rdi, [rdi + Ast.names]
	test	rdi, rdi
	jz	.none
	mov	rsi, rax
	call	intern_bytes
	return
  .none:
	xor	eax, eax
	xor	edx, edx
	return
endp

; =============================================================================
; the tables
; =============================================================================

; __chk_ty_width_ok(w) -> eax = 1 for one of spec §5.2 rule 2's admitted bit
; widths. The same set `cst/parse.inc`'s `__cst_width_ok` admits for `uN`,
; applied to `iN` -- see this file's header for why the two halves are one set.
proc __chk_ty_width_ok, chkt_wd
	locals
	endl
	mov	ecx, [chkt_wd]
	cmp	ecx, 8
	jbe	.small
	cmp	ecx, 64
	ja	.no
	test	ecx, 7
	jnz	.no
	mov	eax, 1
	return
  .small:
	xor	eax, eax
	test	ecx, ecx
	jz	.no
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_signed(w, nameid) -> eax = the width of `iN`, 0 if the spelling is
; not `i` followed by an admitted width. Reads the shape only; admissibility
; is `__chk_ty_width_ok`, so that `i12` is recognised as a signed integer
; spelling and REJECTED rather than silently read as an ordinary path name.
; At most three digits: `i4294967296` needs no more precision than "not one of
; them" (cst/parse.inc's `__cst_bitwidth` makes the same call for `u`).
proc __chk_ty_signed, chkt_wa, chkt_nma
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rdi, [chkt_wa]
	mov	rsi, [chkt_nma]
	call	__chk_ty_bytes
	mov	rbx, rax
	mov	r12, rdx
	cmp	r12, 2
	jb	.no
	cmp	r12, 4
	ja	.no
	cmp	byte [rbx], 'i'
	jne	.no
	xor	r13d, r13d
	mov	r14d, 1
  .digit:
	cmp	r14, r12
	jae	.done
	movzx	eax, byte [rbx + r14]
	cmp	al, '0'
	jb	.no
	cmp	al, '9'
	ja	.no
	sub	eax, '0'
	imul	r13d, r13d, 10
	add	r13d, eax
	inc	r14
	jmp	.digit
  .done:
	mov	edi, r13d
	call	__chk_ty_width_ok
	test	eax, eax
	jz	.no
	mov	eax, r13d
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_ty_pool(w, nameid, pool, poollen) -> rax = the 1-based pool index, or 0.
proc __chk_ty_pool, chkt_wb, chkt_nmb, chkt_pl, chkt_pn
	uses	rbx, r12
	locals
	endl
	mov	rdi, [chkt_wb]
	mov	rsi, [chkt_nmb]
	call	__chk_ty_bytes
	mov	rbx, rax
	mov	r12, rdx
	test	r12, r12
	jz	.no
	mov	rdi, [chkt_pl]
	mov	rsi, [chkt_pn]
	mov	rdx, rbx
	mov	rcx, r12
	call	ast_name_id
	return
  .no:
	xor	eax, eax
	return
endp

; chk_ty_prelude_lookup(cx, nameid) -> rax = a TAGGED prelude declaration
; reference for a MODULE-SCOPE prelude name, or 0.
;
; THIS IS THE BODY `chk_prelude_lookup` (checker/resolve/resolve.inc) LEFT AS A
; HOOK. That routine's header names two blockers -- what `Seg.d` would hold,
; and that the table has no entry for spec §5.1's type names -- and both are
; answered here: the tag (this file's header) and the primitive table (part 1).
;
; ONLY `Scriptor` IS IN MODULE SCOPE. The other six rows are members and
; associated functions of it, reached through `Member` and resolved against a
; receiver's TYPE (`__chk_ty_member`, types.inc), never by a bare name; and
; `ambitus` and `alloc` are spec §4.6 capability ATOMS before they are anything
; else, so `ast_cap_of_name` answers them in pass 1 and this table must not
; also claim the spelling. Spec §12: a module's own declaration SHADOWS the
; prelude's, which falls out of the order `__chk_lookup` already walks -- the
; module table is consulted before this routine, not after.
;
; TAKES `ChkCtx`, NOT `ChkTyWork`: it is called from pass 1, before pass 2's
; working state exists, so it builds what it needs on its own frame.
proc chk_ty_prelude_lookup, chkt_cx2, chkt_nm2
	uses	rbx
	locals
		slot chkt_w2a, sizeof.ChkTyWork
	endl
	mov	rbx, [chkt_cx2]
	test	rbx, rbx
	jz	.none
	; only the fields `__chk_ty_bytes` reads are needed, so this is three
	; stores rather than `chk_ty_work_init` (which allocates two tables).
	lea	rdi, [chkt_w2a]
	mov	[rdi + ChkTyWork.wcx], rbx
	mov	rax, [rbx + ChkCtx.tree]
	mov	[rdi + ChkTyWork.wtre], rax
	xor	ecx, ecx
	test	rax, rax
	jz	.no_names
	mov	rax, [rax + Ast.names]
	test	rax, rax
	jz	.no_names
	mov	rax, [rax + Interner.map]
	mov	rax, [rax + Map.entries]
	mov	rcx, [rax + Vec.len]
  .no_names:
	mov	[rdi + ChkTyWork.wnint], ecx

	lea	rdi, [chkt_w2a]
	mov	rsi, [chkt_nm2]
	lea	rdx, [chk_ty_pre_names]
	mov	rcx, CHK_TY_PRE_NAMES_LEN
	call	__chk_ty_pool
	cmp	rax, CHK_TY_PRE_SCRIPTOR
	jne	.none
	mov	rax, CHK_TY_PRELUDE or CHK_TY_PRE_SCRIPTOR
	return
  .none:
	xor	eax, eax
	return
endp

; =============================================================================
; interning the primitive and prelude types
; =============================================================================

; __chk_ty_mensura(w) -> rax = the id of `mensura`.
;
; CONCRETE, NOT ABSTRACT. Spec §9.5 forbids defaulting to the build platform,
; so the width comes from `--hospes` through `chk_set_target`, and a pass that
; reached this point with none set is a driver bug, not a program error: the
; `rassert` is the channel docs/asm-conventions.md 1.3 assigns to a violated
; internal contract. `mensura` keeps its OWN kind rather than becoming `uN`,
; because spec §5.2 closes with "Mixing it with a fixed-width type requires an
; explicit widening that names the target assumption" -- if it interned to the
; same id as `u64` there would be nothing left to require.
proc __chk_ty_mensura, chkt_wm
	uses	rbx
	locals
		slot chkt_tm, 16
	endl
	mov	rbx, [chkt_wm]
	mov	eax, [rbx + ChkTyWork.wpb]
	rassert	rax ne 0
	lea	rdi, [chkt_tm]
	call	ast_type_clear
	mov	byte [chkt_tm + AstType.kind], AST_TY_MENSURA
	mov	eax, [rbx + ChkTyWork.wpb]
	mov	[chkt_tm + AstType.width], eax
	mov	rdi, [rbx + ChkTyWork.wtre]
	lea	rsi, [chkt_tm]
	call	ast_type_intern
	return
endp

; __chk_ty_simple(w, kind) -> rax = the id of a field-less type kind.
proc __chk_ty_simple, chkt_ws, chkt_ks
	locals
	endl
	mov	rdi, [chkt_ws]
	mov	rdi, [rdi + ChkTyWork.wtre]
	mov	rsi, [chkt_ks]
	call	ast_type_simple
	return
endp

; __chk_ty_unit(w) -> rax = the id of `unit`.
proc __chk_ty_unit, chkt_wu
	locals
	endl
	mov	rdi, [chkt_wu]
	mov	rsi, AST_TY_UNIT
	call	__chk_ty_simple
	return
endp

; __chk_ty_err(w) -> rax = 1, the ERROR type (ast/types.inc: id 1 by
; construction). typed-ast.md section 2.2's cascade suppression: a node whose
; typing failed carries this and its operands raise nothing further.
proc __chk_ty_err, chkt_we
	locals
	endl
	mov	rdi, [chkt_we]
	mov	rdi, [rdi + ChkTyWork.wtre]
	call	ast_type_error
	return
endp

; __chk_ty_u1(w) -> rax = `u1`, spec §5.1's boolean.
proc __chk_ty_u1, chkt_wo
	locals
	endl
	mov	rdi, [chkt_wo]
	mov	rdi, [rdi + ChkTyWork.wtre]
	mov	rsi, AST_SIGN_U
	mov	rdx, 1
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	return
endp

; __chk_ty_kindof(w, tyid) -> rax = the `AstType.kind` of type id `tyid`, or 0.
proc __chk_ty_kindof, chkt_wk, chkt_tk
	locals
	endl
	mov	rsi, [chkt_tk]
	test	rsi, rsi
	jz	.none
	mov	rdi, [chkt_wk]
	mov	rdi, [rdi + ChkTyWork.wtre]
	mov	rax, [rdi + Ast.types.len]
	cmp	rsi, rax
	ja	.none
	call	ast_type_at
	movzx	eax, byte [rax + AstType.kind]
	return
  .none:
	xor	eax, eax
	return
endp

; __chk_ty_field(w, tyid, off) -> rax = the dword at byte offset `off` of type
; `tyid`'s record (`AstType.a`, `.b`, `.width`), 0 for type 0.
proc __chk_ty_field, chkt_wf, chkt_tf, chkt_of
	locals
	endl
	mov	rsi, [chkt_tf]
	test	rsi, rsi
	jz	.none
	mov	rdi, [chkt_wf]
	mov	rdi, [rdi + ChkTyWork.wtre]
	mov	rax, [rdi + Ast.types.len]
	cmp	rsi, rax
	ja	.none
	call	ast_type_at
	mov	rcx, [chkt_of]
	mov	eax, [rax + rcx]
	return
  .none:
	xor	eax, eax
	return
endp

; chk_ty_prelude_type(w, k) -> rax = the interned type of prelude row `k`
; (1-based, `CHK_TY_PRE_*`). The rows of `prelude/interface.inc` part B, built
; as real `Ast.types` entries so that a prelude value compares by id exactly as
; every other value does (checker.md section 1.5).
;
; `Scriptor` HAS NO `AstDecl`, so its nominal `struct` type cannot carry a
; declaration id in `a` the way a source struct does. It carries the TAGGED
; prelude reference instead -- the same tag `Seg.d` carries -- which keeps
; "nominal identity is one number" true and keeps the two `Scriptor`s in one
; program the same id. Nothing in Stage 2 dereferences `struct.a` as a
; declaration except pass 4's layout loop, which walks `Ast.decls` and
; therefore never sees this type at all.
proc chk_ty_prelude_type, chkt_wp, chkt_kp
	uses	rbx, r12
	locals
		slot chkt_ep, dq
		slot chkt_np, dq
	endl
	mov	rbx, [chkt_wp]
	mov	r12, [chkt_kp]
	cmp	r12, CHK_TY_PRE_SCRIPTOR
	je	.scriptor
	cmp	r12, CHK_TY_PRE_SCR_A
	je	.amb
	cmp	r12, CHK_TY_PRE_SCR_FD
	je	.i32
	cmp	r12, CHK_TY_PRE_AMBITUS
	je	.fn_ambitus
	cmp	r12, CHK_TY_PRE_AD_EXITUM
	je	.fn_adexitum
	cmp	r12, CHK_TY_PRE_SCRIBE
	je	.fn_scribe
	cmp	r12, CHK_TY_PRE_ALLOC
	je	.fn_alloc
	mov	rdi, rbx
	call	__chk_ty_err
	return

  .scriptor:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, AST_TY_STRUCT
	mov	rdx, CHK_TY_PRELUDE or CHK_TY_PRE_SCRIPTOR
	xor	rcx, rcx
	call	ast_type_una
	return

  .amb:
	; `Scriptor.a : ambitus` -- a capability-typed field (spec §4.3), which
	; is what makes `Scriptor` capability-bearing with mark {ambitus}.
	mov	rdi, rbx
	mov	rsi, AST_CAP_AMBITUS
	call	__chk_ty_atom
	return

  .i32:
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, AST_SIGN_I
	mov	rdx, 32
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	return

  .fn_ambitus:
	; functio ambitus(m: Mundus) -> ambitus
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_list_mark
	mov	[chkt_np], rax
	mov	rdi, rbx
	mov	rsi, AST_CAP_MUNDUS
	call	__chk_ty_atom
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, rbx
	mov	rsi, AST_CAP_AMBITUS
	call	__chk_ty_atom
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_np]
	call	ast_list_emit
	mov	[chkt_ep], rax
	mov	rdi, rbx
	mov	rsi, [chkt_ep]
	mov	rdx, 1
	call	__chk_ty_fn_empty
	return

  .fn_adexitum:
	; functio ad_exitum(a: ambitus) -> Scriptor  (associated, no receiver)
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_list_mark
	mov	[chkt_np], rax
	mov	rdi, rbx
	mov	rsi, AST_CAP_AMBITUS
	call	__chk_ty_atom
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, rbx
	mov	rsi, CHK_TY_PRE_SCRIPTOR
	call	chk_ty_prelude_type
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_np]
	call	ast_list_emit
	mov	[chkt_ep], rax
	mov	rdi, rbx
	mov	rsi, [chkt_ep]
	mov	rdx, 1
	call	__chk_ty_fn_empty
	return

  .fn_scribe:
	; functio scribe(s: Scriptor, t: textus) -> mensura
	; [OPEN] spec §11 as amended says this returns `eventus<mensura>`;
	; prelude/interface.inc row 5 is marked EXS_IFACE_F_APERTUM for exactly
	; that reason and returns the COUNT until `eventus` has syntax. This
	; table follows the row, not the amendment, so that the two cannot
	; disagree silently.
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_list_mark
	mov	[chkt_np], rax
	mov	rdi, rbx
	mov	rsi, CHK_TY_PRE_SCRIPTOR
	call	chk_ty_prelude_type
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, rbx
	mov	rsi, AST_TY_TEXTUS
	call	__chk_ty_simple
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, rbx
	call	__chk_ty_mensura
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_np]
	call	ast_list_emit
	mov	[chkt_ep], rax
	mov	rdi, rbx
	mov	rsi, [chkt_ep]
	mov	rdx, 2
	call	__chk_ty_fn_empty
	return

  .fn_alloc:
	; functio alloc(m: Mundus, n: mensura) -> alloc  -- [OPEN], as part B says
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_list_mark
	mov	[chkt_np], rax
	mov	rdi, rbx
	mov	rsi, AST_CAP_MUNDUS
	call	__chk_ty_atom
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, rbx
	call	__chk_ty_mensura
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, rbx
	mov	rsi, AST_CAP_ALLOC
	call	__chk_ty_atom
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, rax
	call	ast_list_push
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, [chkt_np]
	call	ast_list_emit
	mov	[chkt_ep], rax
	mov	rdi, rbx
	mov	rsi, [chkt_ep]
	mov	rdx, 2
	call	__chk_ty_fn_empty
	return
endp

; __chk_ty_fn_empty(w, off, nparams) -> rax = an `fn` type over an already
; emitted `extra` run, with THE EMPTY ROW.
;
; `chk_row_empty(tree)`, NEVER 0. rows/rowdefs.inc's "THE EMPTY ROW IS A REAL
; ROW WITH A NON-ZERO ID": `poscit {}` is spec §8.6's explicit empty row and
; checker.md section 2.1 rule 3 makes a bare `functio(A) -> B` the same row, so
; a bare fn type and one whose row slot was left 0 must not be the same id --
; IR 2.9's hidden-argument count is a function of the type.
proc __chk_ty_fn_empty, chkt_wn, chkt_on, chkt_pn2
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [chkt_wn]
	mov	r12, [chkt_on]
	mov	r13, [chkt_pn2]
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	chk_row_empty
	mov	rcx, rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, r12
	mov	rdx, r13
	call	ast_type_fn
	return
endp

; __chk_ty_atom(w, ord) -> rax = the `cap` type of spec §4.6 atom `ord`.
; `AST_TY_CAP` (ast/kinds.inc, checker.md finding 1) with `a` = the atom's own
; declaration, so `m: Mundus`, a `sub`'s `Decl.ty` and a capability-typed
; parameter all intern to ONE id per atom and compare like every other type.
proc __chk_ty_atom, chkt_wt, chkt_or
	uses	rbx, r12
	locals
	endl
	mov	rbx, [chkt_wt]
	mov	r12, [chkt_or]
	rassert	r12 ge 1
	mov	rax, AST_CAP_COUNT
	rassert	r12 le rax
	mov	rdi, [rbx + ChkTyWork.wtre]
	call	ast_cap_base
	test	rax, rax
	jz	.none
	lea	rdx, [rax + r12 - 1]	; the atom's declaration id
	mov	rdi, [rbx + ChkTyWork.wtre]
	mov	rsi, AST_TY_CAP
	xor	rcx, rcx
	call	ast_type_una
	return
  .none:
	; no atoms in the tree at all -- `ast_verify` admits all-eleven-or-none
	mov	rdi, rbx
	call	__chk_ty_err
	return
endp
