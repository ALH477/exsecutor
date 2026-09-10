; compiler/x86_64/ast/load.inc
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
; READING A DUMP BACK. The other half of ast/dump.inc, and the reason that
; file exists: `docs/design/ssa-ir.md` 2.11 checks its own textual format by
; "re-parsing the printer's output and comparing structures", and this is the
; same claim for this tree. Build a tree by hand through ast/build.inc's
; constructors, dump it, load the text into a second tree, and the two must
; compare equal AND dump to identical bytes. That round trip is what makes
; the AST testable with no CST in the repository.
;
; THE LOADER GOES THROUGH THE CONSTRUCTORS, not through `vec_push`. Every
; range check ast/build.inc does at a call site therefore applies to a
; hand-written dump too: a `.ast` file naming a child that is not smaller
; than its parent, or a list offset past the end of the pool, traps in the
; loader rather than producing a tree that only the verifier would reject.
; The cost is the section order (see ast/dump.inc's header), and one cycle to
; close by hand: `AstDecl.node` points FORWARD at a node, so the loader parks
; those values on `Ast.stage` -- the builder's own staging stack -- and
; applies them once the `n` section is in.
;
; The scanner is whitespace-delimited tokens and unsigned decimal, nothing
; else: no quoting, no escapes, no nesting, exactly as ssa-ir.md 2.11
; specifies for its own format. A malformed dump is a caller error, and
; caller errors here are `rassert`s -- this file reads a developer's test
; fixture, never a user's program, so no `EXS-E` code is involved and none is
; invented (CLAUDE.md).
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3.
; -----------------------------------------------------------------------------

include 'dump.inc'

struct AstScan
	field cur, dq
	field lim, dq		; one past the last byte -- NOT `end`, which is
				; a fasmg keyword and expands to `end dq ?`
end struct

proc ast_scan_init, sc, src, len
	locals
	endl
	mov	rdi, [sc]
	mov	rax, [src]
	mov	[rdi + AstScan.cur], rax
	add	rax, [len]
	mov	[rdi + AstScan.lim], rax
	return
endp

; rax = the token's first byte, rdx = its length; rdx = 0 at end of input.
proc __ast_s_tok, sc
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [sc]
	mov	r12, [rbx + AstScan.cur]
  .skip:
	mov	rax, [rbx + AstScan.lim]
	cmp	r12, rax
	jae	.eof
	movzx	eax, byte [r12]
	cmp	eax, 32
	je	.adv
	cmp	eax, 9
	je	.adv
	cmp	eax, 10
	je	.adv
	cmp	eax, 13
	je	.adv
	jmp	.start
  .adv:
	inc	r12
	jmp	.skip
  .eof:
	mov	[rbx + AstScan.cur], r12
	xor	eax, eax
	xor	edx, edx
	return
  .start:
	mov	r13, r12
  .scan:
	mov	rax, [rbx + AstScan.lim]
	cmp	r12, rax
	jae	.end
	movzx	eax, byte [r12]
	cmp	eax, 32
	je	.end
	cmp	eax, 9
	je	.end
	cmp	eax, 10
	je	.end
	cmp	eax, 13
	je	.end
	inc	r12
	jmp	.scan
  .end:
	mov	[rbx + AstScan.cur], r12
	mov	rdx, r12
	sub	rdx, r13
	mov	rax, r13
	return
endp

; rax = the next token read as an unsigned decimal. Every byte must be a
; digit: `48` subtracted from a non-digit is negative in 32 bits, which is
; why both bounds are checked rather than just the upper one.
proc __ast_s_num, sc
	uses	rbx, r12, r13
	locals
	endl
	mov	rdi, [sc]
	call	__ast_s_tok
	rassert	rdx ge 1
	mov	r12, rax
	mov	r13, rdx
	xor	rbx, rbx
  .digit:
	movzx	eax, byte [r12]
	sub	eax, 48
	rassert	eax ge 0
	rassert	eax le 9
	imul	rbx, rbx, 10
	add	rbx, rax
	inc	r12
	dec	r13
	jnz	.digit
	mov	rax, rbx
	return
endp

; rax = the 1-based id of the next token in `pool`. A name this file does not
; know is a malformed dump, not a recoverable condition.
proc __ast_s_word, sc, pool, poollen
	uses	rbx, r12
	locals
	endl
	mov	rdi, [sc]
	call	__ast_s_tok
	rassert	rdx ge 1
	mov	rbx, rax
	mov	r12, rdx
	mov	rdi, [pool]
	mov	rsi, [poollen]
	mov	rdx, rbx
	mov	rcx, r12
	call	ast_name_id
	rassert	rax ge 1
	return
endp

proc __ast_s_expect, sc, ptr, len
	uses	rbx, r12
	locals
	endl
	mov	rdi, [sc]
	call	__ast_s_tok
	mov	rbx, rax
	mov	rcx, [len]
	rassert	rdx eq rcx
	mov	rdi, rbx
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	__map_bytes_equal
	rassert	rax eq 1
	return
endp

proc __ast_s_tag, sc, ch
	uses	rbx
	locals
	endl
	mov	rdi, [sc]
	call	__ast_s_tok
	rassert	rdx eq 1
	movzx	eax, byte [rax]
	mov	rbx, [ch]
	rassert	rax eq rbx
	return
endp

; ---- the loader ------------------------------------------------------------
; `tree` must be freshly `ast_init`ed: this fills an empty tree, it does not
; merge into a populated one.
proc ast_load, tree, src, len
	uses	rbx, r12, r13, r14
	locals
		slot lo_sc,    16
		slot lo_sp,    12
		slot lo_pad,   4
		slot lo_t0,    16
		slot lo_nn,    dq
		slot lo_nd,    dq
		slot lo_nx,    dq
		slot lo_nt,    dq
		slot lo_root,  dq
		slot lo_kind,  dq
		slot lo_aux,   dq
		slot lo_ty,    dq
		slot lo_a,     dq
		slot lo_b,     dq
		slot lo_c,     dq
		slot lo_e,     dq
		slot lo_dbase, dq
		slot lo_flags, dq
		slot lo_name,  dq
		slot lo_node,  dq
		slot lo_parent, dq
	endl
	mov	rbx, [tree]
	mov	rax, [rbx + Ast.nodes.len]
	rassert	rax eq 0		; a fresh tree, per this routine's header

	lea	rdi, [lo_sc]
	mov	rsi, [src]
	mov	rdx, [len]
	call	ast_scan_init

	lea	rdi, [lo_sc]
	lea	rsi, [ast_dump_magic]
	mov	rdx, AST_DUMP_MAGIC_LEN
	call	__ast_s_expect
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	rcx, AST_DUMP_VERSION
	rassert	rax eq rcx

	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_nn], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_nd], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_nx], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_nt], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_root], rax

	xor	r12, r12
  .axis:
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rax
	call	ast_numeri_set
	inc	r12
	cmp	r12, 4
	jl	.axis

	; ---- x ----
	mov	r12, 1
  .extra:
	mov	rax, [lo_nx]
	cmp	r12, rax
	jg	.extra_done
	lea	rdi, [lo_sc]
	mov	rsi, 120		; 'x'
	call	__ast_s_tag
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	rcx, r12
	rassert	rax eq rcx		; the id is the line's position
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	rdi, rbx
	mov	rsi, rax
	call	ast_extra_push
	mov	rcx, r12
	rassert	rax eq rcx
	inc	r12
	jmp	.extra
  .extra_done:

	; ---- t ----
	mov	r12, 1
  .types:
	mov	rax, [lo_nt]
	cmp	r12, rax
	jg	.types_done
	lea	rdi, [lo_sc]
	mov	rsi, 116		; 't'
	call	__ast_s_tag
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	rcx, r12
	rassert	rax eq rcx
	lea	rdi, [lo_t0]
	call	ast_type_clear
	lea	rdi, [lo_sc]
	lea	rsi, [ast_tkind_names]
	mov	rdx, AST_TKIND_NAMES_LEN
	call	__ast_s_word
	mov	[lo_t0 + AstType.kind], al
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_t0 + AstType.sign], al
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_t0 + AstType.order], al
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_t0 + AstType.place], al
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_t0 + AstType.width], eax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_t0 + AstType.a], eax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_t0 + AstType.b], eax
	mov	rdi, rbx
	lea	rsi, [lo_t0]
	call	ast_type_intern
	mov	rcx, r12
	rassert	rax eq rcx		; interning in dump order must
					; reproduce the dumped ids exactly --
					; first-use order is the id (section
					; 2.5), so this is the determinism
					; claim, checked
	inc	r12
	jmp	.types
  .types_done:

	; ---- d ----
	; `AstDecl.node` points FORWARD at a node that does not exist yet, so
	; each one is parked on the staging stack and applied after the `n`
	; section.
	mov	rdi, rbx
	call	ast_list_mark
	mov	[lo_dbase], rax
	mov	r12, 1
  .decls:
	mov	rax, [lo_nd]
	cmp	r12, rax
	jg	.decls_done
	lea	rdi, [lo_sc]
	mov	rsi, 100		; 'd'
	call	__ast_s_tag
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	rcx, r12
	rassert	rax eq rcx
	lea	rdi, [lo_sc]
	lea	rsi, [ast_dkind_names]
	mov	rdx, AST_DKIND_NAMES_LEN
	call	__ast_s_word
	mov	[lo_kind], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_flags], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_aux], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_name], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_node], rax		; deferred -- see this section's note
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_ty], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_parent], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	r13, rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	r14, rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	lea	rdi, [lo_sp]
	mov	rsi, r13
	mov	rdx, r14
	mov	rcx, rax
	call	span_make
	mov	rdi, rbx
	mov	rsi, [lo_kind]
	mov	rdx, [lo_flags]
	mov	rcx, [lo_name]
	mov	r8, [lo_parent]
	lea	r9, [lo_sp]
	call	ast_decl
	mov	rcx, r12
	rassert	rax eq rcx
	mov	rdi, rbx
	mov	rsi, r12
	call	ast_decl_at
	mov	rcx, [lo_aux]
	mov	[rax + AstDecl.aux], cx
	mov	rcx, [lo_ty]
	mov	[rax + AstDecl.ty], ecx
	mov	rdi, rbx
	mov	rsi, [lo_node]
	call	ast_list_push
	inc	r12
	jmp	.decls
  .decls_done:

	; ---- n ----
	mov	r12, 1
  .nodes:
	mov	rax, [lo_nn]
	cmp	r12, rax
	jg	.nodes_done
	lea	rdi, [lo_sc]
	mov	rsi, 110		; 'n'
	call	__ast_s_tag
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	rcx, r12
	rassert	rax eq rcx
	lea	rdi, [lo_sc]
	lea	rsi, [ast_kind_names]
	mov	rdx, AST_KIND_NAMES_LEN
	call	__ast_s_word
	mov	[lo_kind], rax
	; The one kind-dependent field in the whole format: `Externus.aux` is
	; an ABI NAME, not a number (ast/dump.inc's header says why). The kind
	; is already read, so the branch costs one compare and the section
	; stays a single pass.
	mov	rax, [lo_kind]
	cmp	rax, AST_EXTERNUS
	je	.abi
	lea	rdi, [lo_sc]
	call	__ast_s_num
	jmp	.aux_done
  .abi:
	lea	rdi, [lo_sc]
	lea	rsi, [ast_abi_names]
	mov	rdx, AST_ABI_NAMES_LEN
	call	__ast_s_word
	dec	rax			; pool index is AST_ABI_* + 1
  .aux_done:
	mov	[lo_aux], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_ty], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_a], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_b], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_c], rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	[lo_e], rax		; d
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	r13, rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	mov	r14, rax
	lea	rdi, [lo_sc]
	call	__ast_s_num
	lea	rdi, [lo_sp]
	mov	rsi, r13
	mov	rdx, r14
	mov	rcx, rax
	call	span_make
	mov	rdi, rbx
	mov	rsi, [lo_kind]
	mov	rdx, [lo_aux]
	mov	rcx, [lo_a]
	mov	r8, [lo_b]
	lea	r9, [lo_sp]
	call	ast_node
	mov	rcx, r12
	rassert	rax eq rcx
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [lo_c]
	mov	rcx, [lo_e]
	call	ast_node_cd
	mov	rax, [lo_ty]
	test	rax, rax
	jz	.no_ty
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [lo_ty]
	call	ast_node_ty
  .no_ty:
	inc	r12
	jmp	.nodes
  .nodes_done:

	; ---- close the one forward reference ----
	mov	r12, 1
  .link:
	mov	rax, [lo_nd]
	cmp	r12, rax
	jg	.link_done
	lea	rdi, [rbx + Ast.stage]
	mov	rsi, [lo_dbase]
	lea	rsi, [rsi + r12 - 1]
	call	vec_get
	mov	eax, [rax]
	test	eax, eax
	jz	.link_next
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, rax
	call	ast_decl_node
  .link_next:
	inc	r12
	jmp	.link
  .link_done:
	mov	rax, [lo_dbase]
	mov	[rbx + Ast.stage.len], rax

	mov	rax, [lo_root]
	test	rax, rax
	jz	.no_root
	mov	rdi, rbx
	mov	rsi, rax
	call	ast_set_root
  .no_root:
	; Nothing may follow the last record.
	lea	rdi, [lo_sc]
	call	__ast_s_tok
	rassert	rdx eq 0
	return
endp

; ---- structural comparison -------------------------------------------------
; rax = 1 if the two trees are structurally identical, 0 otherwise. Compares
; the live bytes of every vec plus the header fields; the arenas, the
; interner, the type map's bucket array and the staging stack are all
; representation and are deliberately NOT compared.
proc ast_equal, a, b
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [a]
	mov	r12, [b]

	mov	rax, [rbx + Ast.nodes.len]
	cmp	rax, [r12 + Ast.nodes.len]
	jne	.no
	mov	rax, [rbx + Ast.decls.len]
	cmp	rax, [r12 + Ast.decls.len]
	jne	.no
	mov	rax, [rbx + Ast.extra.len]
	cmp	rax, [r12 + Ast.extra.len]
	jne	.no
	mov	rax, [rbx + Ast.types.len]
	cmp	rax, [r12 + Ast.types.len]
	jne	.no
	mov	eax, [rbx + Ast.root]
	cmp	eax, [r12 + Ast.root]
	jne	.no
	mov	eax, [rbx + Ast.numeri]
	cmp	eax, [r12 + Ast.numeri]
	jne	.no

	mov	r13, [rbx + Ast.nodes.len]
	imul	r13, sizeof.AstNode
	mov	rdi, [rbx + Ast.nodes.data]
	mov	rsi, [r12 + Ast.nodes.data]
	mov	rdx, r13
	call	__map_bytes_equal
	test	eax, eax
	jz	.no

	mov	r13, [rbx + Ast.decls.len]
	imul	r13, sizeof.AstDecl
	mov	rdi, [rbx + Ast.decls.data]
	mov	rsi, [r12 + Ast.decls.data]
	mov	rdx, r13
	call	__map_bytes_equal
	test	eax, eax
	jz	.no

	mov	r13, [rbx + Ast.extra.len]
	shl	r13, 2
	mov	rdi, [rbx + Ast.extra.data]
	mov	rsi, [r12 + Ast.extra.data]
	mov	rdx, r13
	call	__map_bytes_equal
	test	eax, eax
	jz	.no

	mov	r13, [rbx + Ast.types.len]
	imul	r13, sizeof.AstType
	mov	rdi, [rbx + Ast.types.data]
	mov	rsi, [r12 + Ast.types.data]
	mov	rdx, r13
	call	__map_bytes_equal
	test	eax, eax
	jz	.no
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp
