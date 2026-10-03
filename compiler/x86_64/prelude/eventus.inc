; compiler/x86_64/prelude/eventus.inc
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 DeMoD LLC.
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
; THE PRELUDE'S `eventus` AND `erratum` -- docs/design/sum-types.md D5:
;
;     typus eventus<T, E> =
;         casus prosperum(T),
;         casus adversum(E);
;
;     structura erratum { numerus: u16 }
;
; `erratum` carries the syscall's own error number, a `u16` -- "the shape the
; `ambitus` atom's syscalls actually return, and nothing ambient (§9.3: no
; errno global, the value is the return the syscall gave)". The field's name,
; `numerus`, is this file's choice: D5 names the type and its width and not the
; field.
;
; THEY ARE DECLARATIONS, NOT A SECOND TYPE SYSTEM. The two items are built as
; ordinary `Typus` and `Struct` subtrees -- exactly the nodes and declarations
; `ast_from_cst` builds for that source -- and appended to the module's item
; list, so resolution, typing, layout and lowering treat them as they treat a
; module's own sum and struct, with no `eventus` case anywhere downstream.
; That is why the builtin one-argument `eventus` (`CHK_TY_P_EVENTUS`) is
; unreachable from source now: a declaration is found before the primitive
; table (`__chk_ty_path_type`'s order).
;
; NOT IN `prelude.asm`, `prelude_data.asm` OR `interface.inc`, deliberately:
; those are the runtime's blob and its pre-seeded call table, and nothing here
; is called -- a sum and a struct have no code. This file is all the prelude
; edit `eventus` needs.
;
; THE SPAN IS ZERO -- file 0, offset 0, length 0 -- on every node and
; declaration, as `ast_cap_push` gives an atom: they are written nowhere. It
; is also their mark (`ast_decl_synth`): pass 1 lets a module's own
; declaration of any of these names SHADOW the prelude's rather than collide
; with it (spec §12), which `examples/onus/onus.exsc`'s `functio eventus`
; needs.
;
; MATERIALIZED ON MENTION. The items are pushed only when some `Path` segment
; in the module already names `eventus`, `erratum`, `prosperum` or `adversum`
; -- or `Directorium`, `Lectorium` or `Scriptorium` (ADR 0017), whose prelude
; rows return `eventus<_, erratum>`.
; A module that names none of them cannot observe the declarations -- nothing
; else can reach them -- so this changes no output but the `--emitte ast` dump,
; and the dump of every module that does not mention them stays byte-identical
; to what it was. The test is on the TREE, not on the interner's contents: an
; interner shared by several compilations (every `tests/unit/` fixture runs
; many rows through one) would otherwise make one row's tree depend on another
; row's source.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.6, §11, §12;
; docs/design/sum-types.md D1, D5.
; -----------------------------------------------------------------------------

; The names, in the order `ast_prelude_push` interns them.
ast_pre_names:
	db 7,'eventus'
	db 1,'T'
	db 1,'E'
	db 9,'prosperum'
	db 8,'adversum'
	db 7,'erratum'
	db 7,'numerus'
	db 11,'Directorium'
	db 9,'Lectorium'
	db 11,'Scriptorium'
AST_PRE_NAMES_LEN = $ - ast_pre_names
AST_PRE_N_EVENTUS	= 1
AST_PRE_N_T		= 2
AST_PRE_N_E		= 3
AST_PRE_N_PROSPERUM	= 4
AST_PRE_N_ADVERSUM	= 5
AST_PRE_N_ERRATUM	= 6
AST_PRE_N_NUMERUS	= 7
AST_PRE_N_DIRECTORIUM	= 8	; ADR 0017's three prelude types: never
AST_PRE_N_LECTORIUM	= 9	; interned by `ast_prelude_push`, only LOOKED
AST_PRE_N_SCRIPTORIUM	= 10	; for, as triggers
; the seven that trigger materialization are names 1, 4, 5, 6 -- and 8, 9 and
; 10: a module that names `Directorium`, `Lectorium` or `Scriptorium` reaches
; prelude rows whose results are `eventus<_, erratum>`
; (checker/types/prim.inc's `__chk_ty_pre_eventus`), so it needs the two
; declarations even if it never spells `eventus` itself.
AST_PRE_TRIGGERS	= 7

; `__ast_pre_name(tree, k, intern)` -> rax = name `k`'s interner id. With
; `intern` 0 it only LOOKS -- `map_get` on the interner's own map, no insert --
; and answers 0 for a name the interner has never seen.
proc __ast_pre_name, tree, apn_k, apn_ins
	uses	rbx
	locals
	endl
	mov	rbx, [tree]
	lea	rdi, [ast_pre_names]
	mov	rsi, [apn_k]
	call	ast_name_at			; rax = bytes, rdx = length
	cmp	qword [apn_ins], 0
	je	.look
	mov	rdi, [rbx + Ast.names]
	mov	rsi, rax
	call	intern_id
	return
  .look:
	mov	rdi, [rbx + Ast.names]
	mov	rdi, [rdi + Interner.map]
	mov	rsi, rax
	call	map_get
	return
endp

; `ast_prelude_wanted(tree)` -> eax = 1 if any `Seg` already in the tree names
; one of the four triggering names.
proc ast_prelude_wanted, tree
	uses	rbx, r12, r13, r14
	locals
		slot apw_ids, 4 * AST_PRE_TRIGGERS
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_EVENTUS
	xor	edx, edx
	call	__ast_pre_name
	mov	[apw_ids], eax
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_PROSPERUM
	xor	edx, edx
	call	__ast_pre_name
	mov	[apw_ids + 4], eax
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_ADVERSUM
	xor	edx, edx
	call	__ast_pre_name
	mov	[apw_ids + 8], eax
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_ERRATUM
	xor	edx, edx
	call	__ast_pre_name
	mov	[apw_ids + 12], eax
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_DIRECTORIUM
	xor	edx, edx
	call	__ast_pre_name
	mov	[apw_ids + 16], eax
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_LECTORIUM
	xor	edx, edx
	call	__ast_pre_name
	mov	[apw_ids + 20], eax
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_SCRIPTORIUM
	xor	edx, edx
	call	__ast_pre_name
	mov	[apw_ids + 24], eax
	mov	r13, [rbx + Ast.nodes.len]
	mov	r12, 1
  .scan:
	cmp	r12, r13
	ja	.no
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_node_at
	cmp	word [rax + AstNode.kind], AST_SEG
	jne	.next
	mov	ecx, [rax + AstNode.a]
	test	ecx, ecx
	jz	.next
	xor	r14d, r14d
  .id:
	cmp	r14d, AST_PRE_TRIGGERS
	jae	.next
	cmp	ecx, [apw_ids + r14 * 4]
	je	.yes
	inc	r14d
	jmp	.id
  .next:
	inc	r12
	jmp	.scan
  .yes:
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; `__ast_pre_node(tree, kind, aux, a, b, span)` is `ast_node`; these two
; wrappers keep the builder below to one line per node.
; `__ast_pre_list1(tree, x)` / `__ast_pre_list2(tree, x, y)` -> rax = the
; `extra` offset of a one- or two-element list.
proc __ast_pre_list1, tree, apl_x
	uses	rbx, r12
	locals
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	call	ast_list_mark
	mov	r12, rax
	mov	rdi, rbx
	mov	rsi, [apl_x]
	call	ast_list_push
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_list_emit
	return
endp

proc __ast_pre_list2, tree, apl_x, apl_y
	uses	rbx, r12
	locals
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	call	ast_list_mark
	mov	r12, rax
	mov	rdi, rbx
	mov	rsi, [apl_x]
	call	ast_list_push
	mov	rdi, rbx
	mov	rsi, [apl_y]
	call	ast_list_push
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_list_emit
	return
endp

; `__ast_pre_typath(tree, nameid, span)` -> rax = `TyPath(Path(Seg nameid))`.
proc __ast_pre_typath, tree, apt_nm, apt_sp
	uses	rbx, r12
	locals
	endl
	mov	rbx, [tree]
	mov	rdi, rbx
	mov	rsi, AST_SEG
	xor	edx, edx
	mov	rcx, [apt_nm]
	xor	r8, r8
	mov	r9, [apt_sp]
	call	ast_node
	mov	rdi, rbx
	mov	rsi, rax
	call	__ast_pre_list1
	mov	rdi, rbx
	mov	rsi, AST_PATH
	xor	edx, edx
	mov	rcx, rax
	mov	r8, 1
	mov	r9, [apt_sp]
	call	ast_node
	mov	rdi, rbx
	mov	rsi, AST_TYPATH
	xor	edx, edx
	mov	rcx, rax
	xor	r8, r8
	mov	r9, [apt_sp]
	call	ast_node
	return
endp

; `__ast_pre_decl(tree, kind, nameid, parent, span)` -> rax = a declaration,
; no flags.
proc __ast_pre_decl, tree, apd_k, apd_nm, apd_par, apd_sp
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [apd_k]
	xor	edx, edx
	mov	rcx, [apd_nm]
	mov	r8, [apd_par]
	mov	r9, [apd_sp]
	call	ast_decl
	return
endp

; `__ast_pre_bind(tree, node, c, decl)` -- `node.c` = `c`, `node.d` = `decl`,
; and `decl.node` = `node`: the two halves of one declaring node.
proc __ast_pre_bind, tree, apb_n, apb_c, apb_d
	locals
	endl
	mov	rdi, [tree]
	mov	rsi, [apb_n]
	mov	rdx, [apb_c]
	mov	rcx, [apb_d]
	call	ast_node_cd
	mov	rdi, [tree]
	mov	rsi, [apb_d]
	mov	rdx, [apb_n]
	call	ast_decl_node
	return
endp

; `ast_prelude_push(tree)` -> rax = the `Typus` node of `eventus`, rdx = the
; `Struct` node of `erratum`: the two items, built in postorder (every child
; before its parent, typed-ast.md section 2.1) and with their declarations in
; the order `ast_from_cst` would push them for the source in this file's
; header. The caller appends both to the module's item list.
proc ast_prelude_push, tree
	uses	rbx, r12, r13, r14
	locals
		slot app_sp,  sizeof.Span
		slot app_ev,  dq	; declarations
		slot app_dT,  dq
		slot app_dE,  dq
		slot app_dP,  dq
		slot app_dA,  dq
		slot app_dR,  dq
		slot app_dN,  dq
		slot app_gT,  dq	; nodes
		slot app_gE,  dq
		slot app_gs,  dq
		slot app_vP,  dq
		slot app_vA,  dq
		slot app_ty,  dq
		slot app_fl,  dq
		slot app_nm,  dq
	endl
	mov	rbx, [tree]
	lea	rdi, [app_sp]
	mov	rcx, sizeof.Span
	xor	eax, eax
	cld
	rep	stosb

	; ---- typus eventus<T, E> = casus prosperum(T), casus adversum(E); ----
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_EVENTUS
	mov	edx, 1
	call	__ast_pre_name
	mov	rdi, rbx
	mov	rsi, AST_D_TYPUS
	mov	rdx, rax
	xor	ecx, ecx
	lea	r8, [app_sp]
	call	__ast_pre_decl
	mov	[app_ev], rax

	; the generic parameters
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_T
	mov	edx, 1
	call	__ast_pre_name
	mov	[app_nm], rax
	mov	rdi, rbx
	mov	rsi, AST_D_GENPARAM
	mov	rdx, rax
	mov	rcx, [app_ev]
	lea	r8, [app_sp]
	call	__ast_pre_decl
	mov	[app_dT], rax
	mov	rdi, rbx
	mov	rsi, AST_GENPARAM
	xor	edx, edx
	mov	rcx, [app_nm]
	xor	r8, r8
	lea	r9, [app_sp]
	call	ast_node
	mov	[app_gT], rax
	mov	rdi, rbx
	mov	rsi, rax
	xor	edx, edx
	mov	rcx, [app_dT]
	call	__ast_pre_bind

	mov	rdi, rbx
	mov	rsi, AST_PRE_N_E
	mov	edx, 1
	call	__ast_pre_name
	mov	[app_nm], rax
	mov	rdi, rbx
	mov	rsi, AST_D_GENPARAM
	mov	rdx, rax
	mov	rcx, [app_ev]
	lea	r8, [app_sp]
	call	__ast_pre_decl
	mov	[app_dE], rax
	mov	rdi, rbx
	mov	rsi, AST_GENPARAM
	xor	edx, edx
	mov	rcx, [app_nm]
	xor	r8, r8
	lea	r9, [app_sp]
	call	ast_node
	mov	[app_gE], rax
	mov	rdi, rbx
	mov	rsi, rax
	xor	edx, edx
	mov	rcx, [app_dE]
	call	__ast_pre_bind

	mov	rdi, rbx
	mov	rsi, [app_gT]
	mov	rdx, [app_gE]
	call	__ast_pre_list2
	mov	rdi, rbx
	mov	rsi, AST_GENERICS
	xor	edx, edx
	mov	rcx, rax
	mov	r8, 2
	lea	r9, [app_sp]
	call	ast_node
	mov	[app_gs], rax

	; casus prosperum(T)
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_PROSPERUM
	mov	edx, 1
	call	__ast_pre_name
	mov	[app_nm], rax
	mov	rdi, rbx
	mov	rsi, AST_D_VARIANT
	mov	rdx, rax
	mov	rcx, [app_ev]
	lea	r8, [app_sp]
	call	__ast_pre_decl
	mov	[app_dP], rax
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_T
	mov	edx, 1
	call	__ast_pre_name
	mov	rdi, rbx
	mov	rsi, rax
	lea	rdx, [app_sp]
	call	__ast_pre_typath
	mov	rdi, rbx
	mov	rsi, rax
	call	__ast_pre_list1
	mov	rdi, rbx
	mov	rsi, AST_VARIANT
	xor	edx, edx
	mov	rcx, [app_nm]
	mov	r8, rax
	lea	r9, [app_sp]
	call	ast_node
	mov	[app_vP], rax
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, 1
	mov	rcx, [app_dP]
	call	__ast_pre_bind

	; casus adversum(E)
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_ADVERSUM
	mov	edx, 1
	call	__ast_pre_name
	mov	[app_nm], rax
	mov	rdi, rbx
	mov	rsi, AST_D_VARIANT
	mov	rdx, rax
	mov	rcx, [app_ev]
	lea	r8, [app_sp]
	call	__ast_pre_decl
	mov	[app_dA], rax
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_E
	mov	edx, 1
	call	__ast_pre_name
	mov	rdi, rbx
	mov	rsi, rax
	lea	rdx, [app_sp]
	call	__ast_pre_typath
	mov	rdi, rbx
	mov	rsi, rax
	call	__ast_pre_list1
	mov	rdi, rbx
	mov	rsi, AST_VARIANT
	xor	edx, edx
	mov	rcx, [app_nm]
	mov	r8, rax
	lea	r9, [app_sp]
	call	ast_node
	mov	[app_vA], rax
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, 1
	mov	rcx, [app_dA]
	call	__ast_pre_bind

	mov	rdi, rbx
	mov	rsi, [app_vP]
	mov	rdx, [app_vA]
	call	__ast_pre_list2
	mov	rdi, rbx
	mov	rsi, AST_SUMBODY
	xor	edx, edx
	mov	rcx, rax
	mov	r8, 2
	lea	r9, [app_sp]
	call	ast_node
	mov	rdi, rbx
	mov	rsi, AST_TYPUS
	xor	edx, edx
	mov	rcx, rax
	xor	r8, r8
	lea	r9, [app_sp]
	call	ast_node
	mov	[app_ty], rax
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, [app_gs]
	mov	rcx, [app_ev]
	call	__ast_pre_bind

	; ---- structura erratum { numerus: u16 } ----
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_ERRATUM
	mov	edx, 1
	call	__ast_pre_name
	mov	rdi, rbx
	mov	rsi, AST_D_STRUCT
	mov	rdx, rax
	xor	ecx, ecx
	lea	r8, [app_sp]
	call	__ast_pre_decl
	mov	[app_dR], rax
	mov	rdi, rbx
	mov	rsi, AST_PRE_N_NUMERUS
	mov	edx, 1
	call	__ast_pre_name
	mov	[app_nm], rax
	mov	rdi, rbx
	mov	rsi, AST_D_FIELD
	mov	rdx, rax
	mov	rcx, [app_dR]
	lea	r8, [app_sp]
	call	__ast_pre_decl
	mov	[app_dN], rax
	mov	rdi, rbx
	mov	rsi, AST_TYBIT
	mov	edx, 16				; `u16`: the width is the aux
	xor	ecx, ecx
	xor	r8, r8
	lea	r9, [app_sp]
	call	ast_node
	mov	rdi, rbx
	mov	rsi, AST_FIELD
	xor	edx, edx
	mov	rcx, [app_nm]
	mov	r8, rax
	lea	r9, [app_sp]
	call	ast_node
	mov	[app_fl], rax
	mov	rdi, rbx
	mov	rsi, rax
	xor	edx, edx
	mov	rcx, [app_dN]
	call	__ast_pre_bind
	mov	rdi, rbx
	mov	rsi, [app_fl]
	call	__ast_pre_list1
	mov	rdi, rbx
	mov	rsi, AST_STRUCT
	xor	edx, edx
	mov	rcx, rax
	mov	r8, 1
	lea	r9, [app_sp]
	call	ast_node
	mov	r12, rax
	mov	rdi, rbx
	mov	rsi, rax
	xor	edx, edx
	mov	rcx, [app_dR]
	call	__ast_pre_bind

	mov	rax, [app_ty]
	mov	rdx, r12
	return
endp
