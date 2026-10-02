; compiler/x86_64/ast/types.inc
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
; THE INTERNED TYPE TABLE. `docs/design/typed-ast.md` section 2.5: type
; EXPRESSIONS are nodes (`Ty*`, with spans, as written); TYPES are 16-byte
; `AstType` records in `Ast.types`, interned by content, id = first-use index.
; The same scheme as docs/design/ssa-ir.md 2.2, one field richer.
;
; ORDER AND PLACE ARE PART OF THE KEY. The whole key is the record's 16 bytes,
; `order` and `place` included, which is the mechanism behind spec §5.2's
; "byte order is in the type" and spec §5.5's "placement is in the type":
; `u32` and `u32:maior` differ in one byte, hash to different buckets and get
; different ids, with no rule anywhere else needed to keep them apart.
;
; THE KEY IS AN ARENA COPY, NOT A POINTER INTO `Ast.types`. This is not a
; style choice and it is not what typed-ast.md section 2.5's "a map on their
; bytes" suggests on a first reading: `rt/map.inc` BORROWS a key (it stores
; `key_ptr` and never copies), and `vec_push` MOVES its backing block when it
; grows. A map keyed on the type vec's own bytes would therefore hold
; dangling pointers from the first growth onward, and would answer lookups
; against whatever the arena had reused that memory for. Every miss allocates
; 16 arena bytes for a stable key; every hit allocates nothing. Recorded as a
; finding against that section rather than fixed silently.
;
; DETERMINISM. Ids come from `Ast.types`' own length -- insertion order --
; never from a bucket index or a hash. The map is queried and never walked,
; so nothing that reaches output can observe FNV's ordering (CLAUDE.md,
; "Determinism is not optional"; the same argument rt/map.inc's own header
; makes).
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §5.1-§5.5, §4.2, §4.4, §6.4, §7.1.
; -----------------------------------------------------------------------------

include 'node.inc'

; ---- zero a template -------------------------------------------------------
; MANDATORY before filling one. The key is all 16 bytes, so a field left as
; whatever was on the stack makes two logically identical types intern as two
; ids -- a bug that shows up as a spurious type mismatch a long way from
; here, which is why this exists as a routine rather than as a comment.
proc ast_type_clear, tn
	locals
	endl
	mov	rdi, [tn]
	mov	qword [rdi], 0
	mov	qword [rdi + 8], 0
	return
endp

; ---- intern -----------------------------------------------------------
; rax = the 1-based id of the type equal to the 16 bytes at `tn`, creating it
; if this is its first use.
proc ast_type_intern, tree, tn
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [tree]
	lea	rdi, [rbx + Ast.typemap]
	mov	rsi, [tn]
	mov	rdx, sizeof.AstType
	call	map_get
	test	rax, rax
	jnz	.found			; ids are >= 1, so 0 is unambiguously a
					; miss -- rt/map.inc's sentinel
					; ambiguity does not reach this caller

	; Miss. The key must outlive every future growth of `Ast.types`, so it
	; is its own arena allocation -- see this file's header.
	mov	rdi, [rbx + Ast.arena]
	mov	rsi, sizeof.AstType
	call	arena_alloc
	mov	r12, rax
	mov	rsi, [tn]
	mov	rax, [rsi]
	mov	[r12], rax
	mov	rax, [rsi + 8]
	mov	[r12 + 8], rax

	lea	rdi, [rbx + Ast.types]
	call	vec_push		; rax = the new slot; may have MOVED the
					; block, which is why nothing into it
					; was held across this call
	mov	rsi, [tn]
	mov	rcx, [rsi]
	mov	[rax], rcx
	mov	rcx, [rsi + 8]
	mov	[rax + 8], rcx

	mov	r13, [rbx + Ast.types.len]	; new length == the new id
	lea	rdi, [rbx + Ast.typemap]
	mov	rsi, r12
	mov	rdx, sizeof.AstType
	mov	rcx, r13
	call	map_insert
	mov	rax, r13
	return
  .found:
	return
endp

; ---- convenience constructors ---------------------------------------------
; Each zeroes a 16-byte template on its own frame, fills the fields its kind
; uses, and interns it. A kind whose shape is not covered here fills an
; `AstType` itself and calls `ast_type_intern` -- these are shorthand, not a
; gate.

; The error type. Id 1 by construction: `ast_init` interns it before anything
; else can be interned (typed-ast.md section 2.2, cascade suppression).
proc ast_type_error, tree
	locals
	endl
	mov	rdi, [tree]
	mov	rax, [rdi + Ast.types.len]
	rassert	rax ge 1
	mov	eax, 1
	return
endp

; `mensura` `textus` `octeti` `scalares` `grapha` `unit` -- kinds with no
; fields at all (spec §5.1: the four string types are DISTINCT and do not
; coerce, which is exactly why each is its own kind rather than a flag).
proc ast_type_simple, tree, kind
	locals
		slot t0, 16
	endl
	lea	rdi, [t0]
	call	ast_type_clear
	mov	rax, [kind]
	mov	[t0 + AstType.kind], al
	mov	rdi, [tree]
	lea	rsi, [t0]
	call	ast_type_intern
	return
endp

; `uN` / `iN` (1 <= N <= 64; `u1` is the boolean) and `f32`/`f64` share this,
; with `sign` ignored for a float. `place` is the host's; a `machina`-placed
; type is built through `ast_type_intern` directly.
proc ast_type_int, tree, sign, width, order
	locals
		slot t0, 16
	endl
	lea	rdi, [t0]
	call	ast_type_clear
	mov	byte [t0 + AstType.kind], AST_TY_INT
	mov	rax, [sign]
	mov	[t0 + AstType.sign], al
	mov	rax, [order]
	mov	[t0 + AstType.order], al
	mov	rax, [width]
	rassert	rax ge 1
	rassert	rax le 64
	mov	[t0 + AstType.width], eax
	mov	rdi, [tree]
	lea	rsi, [t0]
	call	ast_type_intern
	return
endp

proc ast_type_float, tree, width
	locals
		slot t0, 16
	endl
	lea	rdi, [t0]
	call	ast_type_clear
	mov	byte [t0 + AstType.kind], AST_TY_FLOAT
	mov	rax, [width]
	mov	[t0 + AstType.width], eax
	mov	rdi, [tree]
	lea	rsi, [t0]
	call	ast_type_intern
	return
endp

; One-operand kinds: `ptr` `ref` `refc` `borrow` `eventus` `red` take `a` = a
; type id and `width` 0; `acies` takes `a` = element type and `width` = the
; LANE COUNT, which spec §5.4 puts in the type so that it can never come from
; the host; `struct` `alias` `iface` `param` `brand` `cap` take `a` = a
; DECLARATION id (nominal identity) and `width` 0 -- for `cap` that
; declaration is one of spec §4.6's eleven atoms (AST_D_CAPATOM), so
; `m: Mundus` and a `sub`'s `Decl.ty` intern to one id per atom and
; capability types compare by id like every other type.
proc ast_type_una, tree, kind, a, width
	locals
		slot t0, 16
	endl
	lea	rdi, [t0]
	call	ast_type_clear
	mov	rax, [kind]
	mov	[t0 + AstType.kind], al
	mov	rax, [a]
	mov	[t0 + AstType.a], eax
	mov	rax, [width]
	mov	[t0 + AstType.width], eax
	mov	rdi, [tree]
	lea	rsi, [t0]
	call	ast_type_intern
	return
endp

; A borrow. `mut` is 1 for `&mutabilis T` and 0 for `&T`, and it travels in
; the `sign` byte, so the two intern as distinct type ids (node.inc says why
; that byte and not a distinct kind). Width stays 0, as `ast_type_una` leaves
; it: nothing gives a borrow a slot of its own today, and the zero is a
; recorded hazard rather than a number anyone reads.
proc ast_type_borrow, tree, a, mut
	locals
		slot t0, 16
	endl
	lea	rdi, [t0]
	call	ast_type_clear
	mov	byte [t0 + AstType.kind], AST_TY_BORROW
	mov	rax, [mut]
	mov	[t0 + AstType.sign], al
	mov	rax, [a]
	mov	[t0 + AstType.a], eax
	mov	rdi, [tree]
	lea	rsi, [t0]
	call	ast_type_intern
	return
endp

; A function type. `off` is an `Ast.extra` offset holding `nparams` parameter
; type ids followed by the RESULT type id -- `nparams + 1` entries -- and
; `row` is a row id or 0. Spec §4.2: the row is IN THE TYPE, which is what
; makes an escaped closure's authority travel with the value.
proc ast_type_fn, tree, off, nparams, row
	locals
		slot t0, 16
	endl
	lea	rdi, [t0]
	call	ast_type_clear
	mov	byte [t0 + AstType.kind], AST_TY_FN
	mov	rax, [off]
	mov	[t0 + AstType.a], eax
	mov	rax, [nparams]
	mov	[t0 + AstType.width], eax
	mov	rax, [row]
	mov	[t0 + AstType.b], eax
	mov	rdi, [tree]
	lea	rsi, [t0]
	call	ast_type_intern
	return
endp

; A trait object (spec §4.4). `a` = the interface's declaration id, `b` = the
; bound row -- the row `EXS-E0510` is checked against.
proc ast_type_dyn, tree, iface, row
	locals
		slot t0, 16
	endl
	lea	rdi, [t0]
	call	ast_type_clear
	mov	byte [t0 + AstType.kind], AST_TY_DYN
	mov	rax, [iface]
	mov	[t0 + AstType.a], eax
	mov	rax, [row]
	mov	[t0 + AstType.b], eax
	mov	rdi, [tree]
	lea	rsi, [t0]
	call	ast_type_intern
	return
endp

; =============================================================================
; generic sum instances (docs/design/sum-types.md D5)
; =============================================================================
; `eventus<mensura, erratum>` is the declaration's sum applied to two type
; arguments. It is ONE `AST_TY_SUM` record whose `b` is an `AST_TY_ARGS`
; list (ast/kinds.inc says why a cons list of interned cells and not an
; `extra` run: equal argument lists must be one id), so two spellings of the
; same instance are one id and every type comparison in the checker -- which
; is id equality -- holds for instances with no special case.
;
; THE DECLARATION'S OWN TYPE STAYS `b` = 0. Its variants' payload nodes are
; typed in it (a payload `T` is `AST_TY_PARAM` over the generic parameter's
; declaration), and an instance's payload element types are computed from
; them on demand by `ast_sum_subst` -- never stored -- so the layout pass and
; the lowering, which both need them and cannot share a checker routine
; (lower/ssa.inc's header, D1), read one rule from here.

; `ast_type_args(tree, head, tail)` -> rax = the `args` cell (head . tail).
proc ast_type_args, tree, ta_head, ta_tail
	locals
		slot t0, 16
	endl
	lea	rdi, [t0]
	call	ast_type_clear
	mov	byte [t0 + AstType.kind], AST_TY_ARGS
	mov	rax, [ta_head]
	mov	[t0 + AstType.a], eax
	mov	rax, [ta_tail]
	mov	[t0 + AstType.b], eax
	mov	rdi, [tree]
	lea	rsi, [t0]
	call	ast_type_intern
	return
endp

; `ast_type_sum(tree, decl, args)` -> rax = the sum over `typus` declaration
; `decl`, applied to the `args` list (0 for none).
proc ast_type_sum, tree, ts_decl, ts_args
	locals
		slot t0, 16
	endl
	lea	rdi, [t0]
	call	ast_type_clear
	mov	byte [t0 + AstType.kind], AST_TY_SUM
	mov	rax, [ts_decl]
	mov	[t0 + AstType.a], eax
	mov	rax, [ts_args]
	mov	[t0 + AstType.b], eax
	mov	rdi, [tree]
	lea	rsi, [t0]
	call	ast_type_intern
	return
endp

; `ast_sum_body(tree, decl)` -> rax = the `SumBody` node of sum `typus`
; declaration `decl`, rdx = its variant count; rax = 0 when `decl` is not a
; sum (an alias, or a declaration with no node).
proc ast_sum_body, tree, sb_decl
	uses	rbx, r12
	locals
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	mov	rsi, [sb_decl]
	call	ast_decl_at
	cmp	byte [rax + AstDecl.kind], AST_D_TYPUS
	jne	.no
	mov	esi, [rax + AstDecl.node]
	test	esi, esi
	jz	.no
	mov	rdi, rbx
	call	ast_node_at
	mov	r12d, [rax + AstNode.a]		; `Typus.a`
	test	r12d, r12d
	jz	.no
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	cmp	word [rax + AstNode.kind], AST_SUMBODY
	jne	.no
	mov	edx, [rax + AstNode.b]
	mov	rax, r12
	return
  .no:
	xor	eax, eax
	xor	edx, edx
	return
endp

; `ast_sum_tagw(nvariants)` -> rax = the tag's width in BYTES: the smallest of
; 1, 2, 4 that holds the variant count (D6). The same arithmetic as
; checker/rows/layout.inc's `__chk_lay_sum`, which writes it into every
; variant's `Ast.layout` entry; this is for a reader with no layout.
proc ast_sum_tagw, tw_n
	locals
	endl
	mov	rax, [tw_n]
	cmp	rax, 256
	jbe	.one
	cmp	rax, 65536
	jbe	.two
	mov	eax, 4
	return
  .one:
	mov	eax, 1
	return
  .two:
	mov	eax, 2
	return
endp

; `ast_sum_genidx(tree, sumdecl, gpdecl)` -> rax = the 0-based position of
; generic parameter declaration `gpdecl` in sum `typus` `sumdecl`'s
; `Generics` list, or -1 when it is not one of them.
proc ast_sum_genidx, tree, sg_sum, sg_gp
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	mov	rsi, [sg_sum]
	call	ast_decl_at
	mov	esi, [rax + AstDecl.node]
	test	esi, esi
	jz	.no
	mov	rdi, rbx
	call	ast_node_at
	mov	esi, [rax + AstNode.c]		; `Typus.c` = `Generics` or 0
	test	esi, esi
	jz	.no
	mov	rdi, rbx
	call	ast_node_at
	mov	r12d, [rax + AstNode.a]		; the `GenericParam` list
	mov	r13d, [rax + AstNode.b]		; its count
	xor	r14d, r14d
  .scan:
	cmp	r14d, r13d
	jae	.no
	mov	rdi, rbx
	lea	rsi, [r12 + r14]
	call	ast_extra_at
	mov	rdi, rbx
	mov	rsi, rax
	call	ast_node_at
	mov	ecx, [rax + AstNode.d]
	cmp	rcx, [sg_gp]
	je	.hit
	inc	r14d
	jmp	.scan
  .hit:
	mov	eax, r14d
	return
  .no:
	mov	rax, -1
	return
endp

; `ast_sum_subst(tree, sumty, ty)` -> rax = type `ty` with every generic
; parameter of `sumty`'s declaration replaced by `sumty`'s argument in that
; position. `ty` is a type written in the declaration (a payload element);
; `sumty` is the instance it is read through. A sum with no arguments, a
; parameter belonging to something else, and every kind that names no type
; come back unchanged.
;
; THE RECORD IS COPIED BEFORE ANY RECURSION, because `ast_type_intern` may
; grow `Ast.types` and move it (this file's header), and a recursive call
; interns. Kinds whose `a` is a type id (`ptr` `ref` `refc` `borrow` `acies`
; `eventus` `red`) substitute through `a`; a sum substitutes through its own
; argument list; an `args` cell through both halves. A `fn` type's parameter
; run is an `extra` offset and is NOT rebuilt: a function-typed payload of a
; generic sum keeps its parameters as written -- `[OPEN]`, and nothing in the
; tree has one.
proc ast_sum_subst, tree, su_sum, su_ty
	uses	rbx, r12, r13
	locals
		slot su_t, 16
		slot su_x, dq
	endl
	mov	rbx, [tree]
	mov	rax, [su_ty]
	test	rax, rax
	jz	.same
	mov	rax, [su_sum]
	test	rax, rax
	jz	.same
	mov	rdi, rbx
	mov	rsi, [su_sum]
	call	ast_type_at
	mov	r12d, [rax + AstType.b]		; the argument list
	test	r12d, r12d
	jz	.same
	mov	r13d, [rax + AstType.a]		; the sum declaration
	mov	rdi, rbx
	mov	rsi, [su_ty]
	call	ast_type_at
	mov	rcx, [rax]
	mov	[su_t], rcx
	mov	rcx, [rax + 8]
	mov	[su_t + 8], rcx
	movzx	ecx, byte [su_t + AstType.kind]
	cmp	ecx, AST_TY_PARAM
	je	.param
	cmp	ecx, AST_TY_PTR
	je	.via_a
	cmp	ecx, AST_TY_REF
	je	.via_a
	cmp	ecx, AST_TY_REFC
	je	.via_a
	cmp	ecx, AST_TY_BORROW
	je	.via_a
	cmp	ecx, AST_TY_ACIES
	je	.via_a
	cmp	ecx, AST_TY_EVENTUS
	je	.via_a
	cmp	ecx, AST_TY_RED
	je	.via_a
	cmp	ecx, AST_TY_SUM
	je	.via_b
	cmp	ecx, AST_TY_ARGS
	je	.via_ab
  .same:
	mov	rax, [su_ty]
	return

  .param:
	mov	rdi, rbx
	mov	esi, r13d
	mov	edx, [su_t + AstType.a]
	call	ast_sum_genidx
	test	rax, rax
	js	.same
	mov	rcx, rax			; walk the list that many cells
  .walk:
	test	r12d, r12d
	jz	.same				; fewer arguments than parameters:
						; the checker's `EXS-E0304` case
	mov	[su_x], rcx
	mov	rdi, rbx
	mov	esi, r12d
	call	ast_type_at
	mov	rcx, [su_x]
	test	rcx, rcx
	jz	.head
	mov	r12d, [rax + AstType.b]
	dec	rcx
	jmp	.walk
  .head:
	mov	eax, [rax + AstType.a]
	return

  .via_a:
	mov	rdi, rbx
	mov	rsi, [su_sum]
	mov	edx, [su_t + AstType.a]
	call	ast_sum_subst
	mov	[su_t + AstType.a], eax
	jmp	.intern
  .via_b:
	mov	edx, [su_t + AstType.b]
	test	edx, edx
	jz	.same
	mov	rdi, rbx
	mov	rsi, [su_sum]
	call	ast_sum_subst
	mov	[su_t + AstType.b], eax
	jmp	.intern
  .via_ab:
	mov	rdi, rbx
	mov	rsi, [su_sum]
	mov	edx, [su_t + AstType.a]
	call	ast_sum_subst
	mov	[su_t + AstType.a], eax
	mov	edx, [su_t + AstType.b]
	test	edx, edx
	jz	.intern
	mov	rdi, rbx
	mov	rsi, [su_sum]
	call	ast_sum_subst
	mov	[su_t + AstType.b], eax
  .intern:
	mov	rdi, rbx
	lea	rsi, [su_t]
	call	ast_type_intern
	return
endp
