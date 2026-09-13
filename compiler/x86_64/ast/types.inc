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
