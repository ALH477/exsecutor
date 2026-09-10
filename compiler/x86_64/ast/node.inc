; compiler/x86_64/ast/node.inc
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
; THE RECORDS. `docs/design/typed-ast.md` section 2.1's four structures --
; `Node`, `Decl`, the `Ast` container, and (section 2.5) the interned
; `TypeNode` -- plus the read-only accessors over them.
;
; INDICES, NEVER POINTERS. Every cross-reference in this tree is a 1-based
; index into one `Vec`: 0 is "none", the sentinel rt/intern.inc and
; cst/green.inc already use. Two reasons, and only the second is about
; memory. (1) CLAUDE.md's "no ordering may depend on a pointer value": an id
; assigned in creation order is a function of the input bytes alone, and a
; pointer is a function of where mmap happened to land. (2) `vec_push` MOVES
; its backing block when it grows, so a pointer held across a push is a
; dangling pointer -- an accessor here returns a pointer valid until the next
; push and nothing longer, which is why every routine in ast/ that pushes
; re-derives its pointers afterwards rather than caching them.
;
; POSTORDER. A child's index is strictly less than its parent's, because the
; builder writes a node only once its children exist. There is no `next`
; chain and no patch-up pass: the AST has no phis (the contrast is with
; docs/design/ssa-ir.md 2.1, whose instruction lists do). ast/verify.inc
; checks it; ast/build.inc's `ast_node` checks the operands it is handed at
; the moment it is handed them, which turns "the tree came out malformed"
; into "this call site is wrong".
;
; WHAT `typed` MEANS BEFORE A CHECKER EXISTS (typed-ast.md section 2.2): the
; type slots are PRESENT and EMPTY. `AstNode.ty` is 0 everywhere after
; Stage 1 and non-zero on every expression and type node after Stage 2. Type
; id 1 is the ERROR type, reserved by `ast_init` interning it first, so a
; node whose typing failed can be marked without a second sentinel and no
; diagnostic is raised on an operand of it.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.3 (spans are not retrofittable),
; §8.6, §9.1, §9.3.
; -----------------------------------------------------------------------------

include 'kinds.inc'

; ---- the node record -------------------------------------------------------
; 36 bytes. `a b c d` are four anonymous operand slots whose meaning is
; per-kind; `ast_roles` (ast/kinds.inc) is the table that says which is which.
; `d` is the DECLARATION SLOT: on a declaring node the `AstDecl` it
; introduces (Stage 1), on a name-denoting node the one it resolves to
; (Stage 2), and otherwise a fourth operand.
struct AstNode
	field kind, dw		; AST_* (ast/kinds.inc); never 0 in a live node
	field aux,  dw		; kind-specific: a flag set, an operator, a width
	field ty,   dd		; `Ast.types` id -- 0 until Stage 2, never 1
				; unless typing FAILED here
	field a,    dd
	field b,    dd
	field c,    dd
	field d,    dd
	span Span		; rt/span.inc's 12 bytes, BY VALUE (spec §8.3)
end struct
assert sizeof.AstNode = 36

; ---- the declaration record ------------------------------------------------
; 32 bytes. One per name-introducing construct, pushed in SOURCE order, which
; is what makes a block's bindings the contiguous range
; `[Block.c, Block.c + Block.aux)` -- ssa-ir.md 2.5's "var id = binding
; order" and 2.8's scope-exit release walk both index that range.
struct AstDecl
	field kind,   db	; AST_D_* (ast/kinds.inc)
	field flags,  db	; AST_F_* -- all eight bits are spoken for
	field aux,    dw
	field name,   dd	; rt/intern.inc id
	field node,   dd	; the introducing node -- 0 until it is built
	field ty,     dd	; 0 until Stage 2
	field parent, dd	; enclosing declaration; 0 at module level
	span Span
end struct
assert sizeof.AstDecl = 32

; ---- the type node ---------------------------------------------------------
; 16 bytes, interned by content: id = first-use index. `order` and `place`
; are FIELDS, not annotations elsewhere, which is the whole point --
; `u32:maior` and `u32` differ in one byte here and therefore get different
; ids, making spec §5.2's "byte order is in the type" and spec §5.5's
; "placement is in the type" literally true rather than conventionally so.
struct AstType
	field kind,  db		; AST_TY_* (ast/kinds.inc)
	field sign,  db		; AST_SIGN_*  -- `int` only
	field order, db		; AST_ORD_*   -- spec §5.2
	field place, db		; AST_PLACE_* -- spec §5.5
	field width, dd		; bit width, lane count, or parameter count
	field a,     dd		; element type / declaration / `extra` offset
	field b,     dd		; row id
end struct
assert sizeof.AstType = 16

; ---- the capability row ----------------------------------------------------
; typed-ast.md section 2.6's `Row { items extra, count }`. Stage 1 NEVER
; fills this table: rows are `AST_ROW` nodes as written until Stage 2 sorts
; each item list on (kind, declaration index) and interns it, so `{alloc,
; rete}` and `{rete, alloc}` become one id. The sort key is a declaration
; index in both halves, so it is deterministic (spec §9.3).
struct AstRow
	field items, dd		; `Ast.extra` offset; pairs of (kind, id)
	field count, dd
end struct
assert sizeof.AstRow = 8

; ---- resolved layout -------------------------------------------------------
; 8 bytes, INDEXED BY DECLARATION ID (typed-ast.md section 2.8): a field's
; offset in its own entry, a struct's size and alignment in its. Filled by
; Stage 2, because ssa-ir.md 2.7 forbids the backend from choosing a layout
; and `EXS-E0321`/`EXS-E0322` are diagnostics with spans against a field.
; Field names avoid `byte`, `bit` and `align`, all of which fasmg's own
; dialect claims.
struct AstLayout
	field off,    dd	; byte offset
	field bitoff, db	; bit offset within that byte (spec §5.2 rule 1)
	field width,  db	; bits
	field order,  db	; AST_ORD_*
	field algn,   db	; alignment, on a struct's own entry
end struct
assert sizeof.AstLayout = 8

; ---- the tree --------------------------------------------------------------
; Every `Vec` is embedded BY VALUE rather than pointed at: one allocation,
; one `lea` to reach any of them, and nothing to keep in sync. The three
; Stage 2 side tables (`layout`, `own`, `konst`) are present but unallocated
; until `ast_side_alloc` runs -- they cost nothing until a checker exists.
struct Ast
	field arena, dq		; -> Arena. NOT reset while this tree is live
	field names, dq		; -> Interner (identifiers, literal text)
	nodes   Vec		; AstNode,   postorder
	decls   Vec		; AstDecl,   source order
	extra   Vec		; dd, append-only; index 0 is a dead sentinel
	types   Vec		; AstType,   interned, id 1 = the error type
	rows    Vec		; AstRow,    Stage 2
	stage   Vec		; dd, the builder's staging stack (see below)
	layout  Vec		; AstLayout, Stage 2, indexed by declaration id
	own     Vec		; db,        Stage 2, indexed by node id
	konst   Vec		; dq,        Stage 2, indexed by node id
	typemap Map		; 16 AstType bytes -> type id
	field root,   dd	; the `Module` node; 0 until the walk finishes
	field numeri, dd	; FOUR u8 axes in spec §5.4's order -- index
				; with AST_NUM_*. The module's, not any node's
				; (typed-ast.md section 2.7).
	field sides,  dd	; non-zero once ast_side_alloc has run
	field pad0,   dd
end struct

; ---- initial capacities ----------------------------------------------------
; rt/arena.inc DOES NOT GROW and `vec_push` doubles by allocating a fresh
; block from that arena WITHOUT freeing the old one, so a vec that reaches N
; elements has consumed about `2 * N * stride` arena bytes, not `N * stride`.
; These are first estimates for a small module and are `[OPEN]` exactly as
; typed-ast.md section 5 H1 says: nothing has parsed a real file, so no
; measurement backs any of them. Over-running the arena is an `rassert`, not
; a silent corruption.
AST_NODES_CAP0	= 512
AST_DECLS_CAP0	= 128
AST_EXTRA_CAP0	= 512
AST_TYPES_CAP0	= 64
AST_ROWS_CAP0	= 32
AST_STAGE_CAP0	= 128
AST_TYPE_BUCKETS = 1024		; MUST be a power of 2 (rt/map.inc)

; What `ast_init` alone consumes -- MEASURED, not estimated: 34648 bytes, by
; reading `Arena.cur - Arena.base` immediately after it returns, with the
; interner on a different arena so the figure is this routine's alone. The
; arithmetic that predicted it:
;   nodes  512 * 36 = 18432    decls 128 * 32 = 4096
;   extra  512 *  4 =  2048    types  64 * 16 = 1024
;   rows    32 *  8 =   256    stage 128 *  4 =  512
;   typemap: Map 32 + Vec 40 + 1024 buckets * 8 = 8264
;   the error type's interning key                  16
;                                              = 34648
; and the same probe on the 14-node tree of tests/unit/ast_build_roundtrip.asm
; reports 34648 again: a small module fits entirely inside the initial
; capacities and grows nothing.
;
; AST_ARENA_HINT is that plus room for a module that does not. It is `[OPEN]`
; in exactly the sense typed-ast.md section 5 H1 means: no REAL module has
; been parsed into this tree, because no parser hands it one yet, so the
; headroom is a guess even though the floor is a measurement. A caller that
; knows its input size should compute `2 * (nodes * 36 + decls * 32 + extra *
; 4)` -- the factor of two is `vec_push` doubling into a fresh block without
; freeing the old one -- and add the 34648 above.
AST_ARENA_HINT	= 16 * 1024 * 1024

; ---- accessors -------------------------------------------------------------
; Each returns a pointer INTO a vec's backing block, valid only until the next
; push on that vec. Out-of-range is an internal contract violation and traps
; (docs/asm-conventions.md, "1.3 Error protocol": a compiler bug is never a
; `CF` an unlucky caller could swallow).

proc ast_node_at, tree, id
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [id]
	rassert	rsi ge 1
	dec	rsi
	lea	rdi, [rdi + Ast.nodes]
	call	vec_get
	return
endp

proc ast_decl_at, tree, id
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [id]
	rassert	rsi ge 1
	dec	rsi
	lea	rdi, [rdi + Ast.decls]
	call	vec_get
	return
endp

proc ast_type_at, tree, id
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [id]
	rassert	rsi ge 1
	dec	rsi
	lea	rdi, [rdi + Ast.types]
	call	vec_get
	return
endp

proc ast_row_at, tree, id
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [id]
	rassert	rsi ge 1
	dec	rsi
	lea	rdi, [rdi + Ast.rows]
	call	vec_get
	return
endp

; `Ast.extra` index 0 is a dead sentinel pushed by `ast_init`, so a live list
; never starts at 0 and "0 = none" holds for list offsets exactly as it does
; for ids. Returns the VALUE, zero-extended, not a pointer -- the element is
; four bytes and a pointer to it would be invalidated by the next push.
proc ast_extra_at, tree, index
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [index]
	rassert	rsi ge 1
	lea	rdi, [rdi + Ast.extra]
	call	vec_get
	mov	eax, [rax]
	return
endp

proc ast_node_count, tree
	locals
	endl
	mov	rdi, [tree]
	mov	rax, [rdi + Ast.nodes.len]
	return
endp

proc ast_decl_count, tree
	locals
	endl
	mov	rdi, [tree]
	mov	rax, [rdi + Ast.decls.len]
	return
endp

proc ast_type_count, tree
	locals
	endl
	mov	rdi, [tree]
	mov	rax, [rdi + Ast.types.len]
	return
endp

; The count INCLUDES the dead sentinel at index 0, so the last live index is
; `ast_extra_count - 1` and a range `[off, off+n)` is in bounds iff
; `off >= 1` and `off + n <= ast_extra_count`.
proc ast_extra_count, tree
	locals
	endl
	mov	rdi, [tree]
	mov	rax, [rdi + Ast.extra.len]
	return
endp

; ---- the operand-role lookup ----------------------------------------------
; rax = the role byte for slot `slot` (0..3) of kind `kind`.
proc ast_role_of, kind, slotno
	locals
	endl
	mov	rax, [kind]
	rassert	rax ge 1
	mov	rcx, AST_KIND_MAX
	rassert	rax le rcx
	mov	rcx, [slotno]
	rassert	rcx le 3
	dec	rax
	shl	rax, 2
	add	rax, rcx
	lea	rdx, [ast_roles]
	movzx	eax, byte [rdx + rax]
	return
endp

; ---- name-pool lookup ------------------------------------------------------
; The pools are `db LEN, 'Name'` runs (ast/kinds.inc). `ast_name_at` walks to
; the `n`-th (1-based) entry and returns rax = its bytes, rdx = its length;
; `ast_name_id` scans for a match and returns its 1-based index, or 0.
; Both are linear on purpose: see ast/kinds.inc's note on why nothing here
; hashes.

proc ast_name_at, pool, n
	locals
	endl
	mov	rdi, [pool]
	mov	rcx, [n]
	rassert	rcx ge 1
  .walk:
	dec	rcx
	jz	.here
	movzx	eax, byte [rdi]
	lea	rdi, [rdi + rax + 1]
	jmp	.walk
  .here:
	movzx	edx, byte [rdi]
	lea	rax, [rdi + 1]
	return
endp

proc ast_name_id, pool, poollen, ptr, len
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [pool]
	mov	r12, rbx
	add	r12, [poollen]		; one past the pool
	mov	r13, 1			; 1-based index of the entry at rbx
  .scan:
	cmp	rbx, r12
	jae	.miss
	movzx	eax, byte [rbx]
	cmp	rax, [len]
	jne	.next
	push	rax
	lea	rdi, [rbx + 1]
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	__map_bytes_equal	; rt/map.inc: plain byte compare, no hash
	pop	rcx
	test	eax, eax
	jnz	.hit
	mov	rax, rcx
  .next:
	lea	rbx, [rbx + rax + 1]
	inc	r13
	jmp	.scan
  .hit:
	mov	rax, r13
	return
  .miss:
	xor	eax, eax
	return
endp
