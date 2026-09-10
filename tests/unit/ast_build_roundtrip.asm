; tests/unit/ast_build_roundtrip.asm
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
; ast fixture for compiler/x86_64/ast/. Builds a real tree BY HAND through the
; constructors -- there is no CST and no parser, which is the whole reason
; ast/dump.inc exists -- then verifies it, dumps it, loads the dump back into
; a second tree, and requires the two to be equal three ways.
;
; The tree is `publica functio dot(a: f32) -> f32 { redde a; }` in postorder:
;
;   n1  Seg  "f32"        n8  Sig  params=[n4] result=n7
;   n2  Path [n1]         n9  Seg  "a"
;   n3  TyPath n2         n10 Path [n9]
;   n4  Param "a" : n3    n11 Redde n10
;   n5  Seg  "f32"        n12 Block [n11]
;   n6  Path [n5]         n13 Fn   sig=n8 body=n12 decl=d1
;   n7  TyPath n6         n14 Module [n13]
;
; with d1 = the function's declaration and d2 = the parameter's, and
; `d2.parent == d1`. Every child index is smaller than its parent's, which is
; postorder and is what ast/verify.inc checks.
;
; WHAT EACH CHECK PROVES, and why it is not the same check twice:
;   1. `ast_init` reserves type id 1 for the error type (typed-ast.md 2.2).
;   2. the constructors return the ids they are expected to -- creation order
;      IS the id, so this is the determinism claim at its smallest.
;   3. `ast_verify_stage1` accepts the finished tree, and the tree really is
;      at Stage 1 (every `ty` slot empty, no side table allocated).
;   4. the dump is produced whole -- `DiagOut.trunc` clear.
;   5. the dump LOADS: every constructor range check in ast/build.inc is
;      re-run against the text, and the loaded tree verifies too.
;   6. `ast_equal` -- the two trees agree byte for byte in every vec.
;   7. the SECOND dump is byte-identical to the first. 6 and 7 are different
;      claims: 6 could hold for a dump that lost a field the loader defaults,
;      and 7 could hold for a dump that is lossy in the same way twice; only
;      together do they say the text is a faithful encoding.
;   8. the side tables allocate, and read back as zeroes.
;
; The dump is written to stdout as well, so a reader of this fixture's output
; sees the tree it is asserting about rather than only its exit code.
;
; Exit 0 = every check passed; 10+N = check N failed (tests/README.md). A
; violated INVARIANT does not come back here at all: ast/verify.inc traps
; (exit 132), which is what the three ast_verify_* negative fixtures are for.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	; ---- an arena and an interner, neither ever reset ----
	lea	rdi, [fx_arena]
	mov	rsi, 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_arena2]
	mov	rsi, 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 64
	call	intern_init

	lea	rdi, [fx_names]
	lea	rsi, [fx_t_dot]
	mov	rdx, 3
	call	intern_id
	mov	[fx_id_dot], rax
	lea	rdi, [fx_names]
	lea	rsi, [fx_t_a]
	mov	rdx, 1
	call	intern_id
	mov	[fx_id_a], rax
	lea	rdi, [fx_names]
	lea	rsi, [fx_t_f32]
	mov	rdx, 3
	call	intern_id
	mov	[fx_id_f32], rax

	; ---- check 1: the tree, and the reserved error type ----
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	call	ast_type_error
	cmp	rax, 1
	jne	.fail1
	lea	rdi, [fx_tree]
	call	ast_type_count
	cmp	rax, 1
	jne	.fail1

	; ---- check 2: declarations and nodes, in order ----
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_FN
	mov	rdx, AST_F_PUBLICA
	mov	rcx, [fx_id_dot]
	xor	r8, r8
	lea	r9, [fx_sp]
	call	fx_decl
	cmp	rax, 1
	jne	.fail2
	lea	rdi, [fx_tree]
	mov	rsi, AST_D_PARAM
	xor	rdx, rdx
	mov	rcx, [fx_id_a]
	mov	r8, 1
	lea	r9, [fx_sp]
	call	fx_decl
	cmp	rax, 2
	jne	.fail2

	; n1 Seg "f32"
	mov	rdi, AST_SEG
	xor	rsi, rsi
	mov	rdx, [fx_id_f32]
	xor	rcx, rcx
	mov	r8, 11
	call	fx_node
	cmp	rax, 1
	jne	.fail2
	; n2 Path [n1]
	mov	rdi, 1
	call	fx_list1
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, AST_PATH
	xor	rsi, rsi
	mov	rdx, r12
	mov	rcx, r13
	mov	r8, 11
	call	fx_node
	cmp	rax, 2
	jne	.fail2
	; n3 TyPath n2
	mov	rdi, AST_TYPATH
	xor	rsi, rsi
	mov	rdx, 2
	xor	rcx, rcx
	mov	r8, 11
	call	fx_node
	cmp	rax, 3
	jne	.fail2
	; n4 Param "a" : n3, declaring d2
	mov	rdi, AST_PARAM
	xor	rsi, rsi
	mov	rdx, [fx_id_a]
	mov	rcx, 3
	mov	r8, 8
	call	fx_node
	cmp	rax, 4
	jne	.fail2
	lea	rdi, [fx_tree]
	mov	rsi, 4
	xor	rdx, rdx
	mov	rcx, 2
	call	ast_node_cd
	; n5 Seg "f32" (the result type -- a separate node, same interned name)
	mov	rdi, AST_SEG
	xor	rsi, rsi
	mov	rdx, [fx_id_f32]
	xor	rcx, rcx
	mov	r8, 20
	call	fx_node
	cmp	rax, 5
	jne	.fail2
	; n6 Path [n5]
	mov	rdi, 5
	call	fx_list1
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, AST_PATH
	xor	rsi, rsi
	mov	rdx, r12
	mov	rcx, r13
	mov	r8, 20
	call	fx_node
	cmp	rax, 6
	jne	.fail2
	; n7 TyPath n6
	mov	rdi, AST_TYPATH
	xor	rsi, rsi
	mov	rdx, 6
	xor	rcx, rcx
	mov	r8, 20
	call	fx_node
	cmp	rax, 7
	jne	.fail2
	; n8 Sig params=[n4] result=n7
	mov	rdi, 4
	call	fx_list1
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, AST_SIG
	xor	rsi, rsi
	mov	rdx, r12
	mov	rcx, r13
	mov	r8, 0
	call	fx_node
	cmp	rax, 8
	jne	.fail2
	lea	rdi, [fx_tree]
	mov	rsi, 8
	mov	rdx, 7
	xor	rcx, rcx
	call	ast_node_cd
	; n9 Seg "a"
	mov	rdi, AST_SEG
	xor	rsi, rsi
	mov	rdx, [fx_id_a]
	xor	rcx, rcx
	mov	r8, 34
	call	fx_node
	cmp	rax, 9
	jne	.fail2
	; n10 Path [n9]
	mov	rdi, 9
	call	fx_list1
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, AST_PATH
	xor	rsi, rsi
	mov	rdx, r12
	mov	rcx, r13
	mov	r8, 34
	call	fx_node
	cmp	rax, 10
	jne	.fail2
	; n11 Redde n10
	mov	rdi, AST_REDDE
	xor	rsi, rsi
	mov	rdx, 10
	xor	rcx, rcx
	mov	r8, 28
	call	fx_node
	cmp	rax, 11
	jne	.fail2
	; n12 Block [n11] -- no bindings, so the declaration range is empty and
	; its offset is canonically 0 (ast/verify.inc's __ast_v_block)
	mov	rdi, 11
	call	fx_list1
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, AST_BLOCK
	xor	rsi, rsi
	mov	rdx, r12
	mov	rcx, r13
	mov	r8, 26
	call	fx_node
	cmp	rax, 12
	jne	.fail2
	; n13 Fn sig=n8 body=n12, publica, declaring d1
	mov	rdi, AST_FN
	mov	rsi, AST_FN_PUBLICA
	mov	rdx, 8
	mov	rcx, 12
	mov	r8, 0
	call	fx_node
	cmp	rax, 13
	jne	.fail2
	lea	rdi, [fx_tree]
	mov	rsi, 13
	xor	rdx, rdx
	mov	rcx, 1
	call	ast_node_cd
	; n14 Module [n13]
	mov	rdi, 13
	call	fx_list1
	mov	r12, rax
	mov	r13, rdx
	mov	rdi, AST_MODULE
	xor	rsi, rsi
	mov	rdx, r12
	mov	rcx, r13
	mov	r8, 0
	call	fx_node
	cmp	rax, 14
	jne	.fail2

	lea	rdi, [fx_tree]
	mov	rsi, 14
	call	ast_set_root
	lea	rdi, [fx_tree]
	mov	rsi, 1
	mov	rdx, 13
	call	ast_decl_node
	lea	rdi, [fx_tree]
	mov	rsi, 2
	mov	rdx, 4
	call	ast_decl_node

	lea	rdi, [fx_tree]
	call	ast_node_count
	cmp	rax, 14
	jne	.fail2
	lea	rdi, [fx_tree]
	call	ast_decl_count
	cmp	rax, 2
	jne	.fail2
	lea	rdi, [fx_tree]
	call	ast_extra_count
	cmp	rax, 7			; six live entries plus the sentinel
	jne	.fail2

	; ---- check 3: the verifier accepts it ----
	lea	rdi, [fx_tree]
	call	ast_verify_stage1

	; ---- check 4: dump it, whole ----
	lea	rdi, [fx_wr]
	lea	rsi, [fx_buf]
	mov	rdx, 8192
	call	diag_out_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_wr]
	call	ast_dump
	mov	eax, [fx_wr + DiagOut.trunc]
	test	eax, eax
	jnz	.fail4
	mov	rax, [fx_wr + DiagOut.len]
	test	rax, rax
	jz	.fail4

	; echo it, so the fixture's output shows the tree it asserts about
	mov	rdi, 1
	lea	rsi, [fx_buf]
	mov	rdx, [fx_wr + DiagOut.len]
	call	sys_write

	; ---- check 5: load it back ----
	lea	rdi, [fx_tree2]
	lea	rsi, [fx_arena2]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree2]
	lea	rsi, [fx_buf]
	mov	rdx, [fx_wr + DiagOut.len]
	call	ast_load
	lea	rdi, [fx_tree2]
	call	ast_verify_stage1
	lea	rdi, [fx_tree2]
	call	ast_node_count
	cmp	rax, 14
	jne	.fail5

	; ---- check 6: structurally equal ----
	lea	rdi, [fx_tree]
	lea	rsi, [fx_tree2]
	call	ast_equal
	cmp	rax, 1
	jne	.fail6

	; ---- check 7: and the second dump is byte-identical ----
	lea	rdi, [fx_wr2]
	lea	rsi, [fx_buf2]
	mov	rdx, 8192
	call	diag_out_init
	lea	rdi, [fx_tree2]
	lea	rsi, [fx_wr2]
	call	ast_dump
	mov	rax, [fx_wr + DiagOut.len]
	cmp	rax, [fx_wr2 + DiagOut.len]
	jne	.fail7
	lea	rdi, [fx_buf]
	lea	rsi, [fx_buf2]
	mov	rdx, [fx_wr + DiagOut.len]
	call	__map_bytes_equal
	cmp	eax, 1
	jne	.fail7

	; ---- check 8: the Stage 2 side tables ----
	lea	rdi, [fx_tree]
	call	ast_side_alloc
	lea	rdi, [fx_tree]
	mov	rsi, 11
	call	ast_own_get
	test	eax, eax
	jnz	.fail8
	lea	rdi, [fx_tree]
	mov	rsi, 11
	mov	rdx, 1
	call	ast_own_set
	lea	rdi, [fx_tree]
	mov	rsi, 11
	call	ast_own_get
	cmp	eax, 1
	jne	.fail8
	lea	rdi, [fx_tree]
	mov	rsi, 11
	call	ast_konst_get
	test	rax, rax
	jnz	.fail8
	lea	rdi, [fx_tree]
	mov	rsi, 2
	call	ast_layout_at
	mov	ecx, [rax + AstLayout.off]
	test	ecx, ecx
	jnz	.fail8

	xor	edi, edi
	call	sys_exit_group
  .fail0:
	mov	rdi, 10
	call	sys_exit_group
  .fail1:
	mov	rdi, 11
	call	sys_exit_group
  .fail2:
	mov	rdi, 12
	call	sys_exit_group
  .fail4:
	mov	rdi, 14
	call	sys_exit_group
  .fail5:
	mov	rdi, 15
	call	sys_exit_group
  .fail6:
	mov	rdi, 16
	call	sys_exit_group
  .fail7:
	mov	rdi, 17
	call	sys_exit_group
  .fail8:
	mov	rdi, 18
	call	sys_exit_group

include '../../compiler/x86_64/diag/diag.inc'
include '../../compiler/x86_64/ast/ast.inc'

; ---- fixture helpers -------------------------------------------------------
; A node with a one-byte-wide span at `start` in file 7. Spans are exercised
; by tests/unit/span_ops.asm; what matters here is that every node HAS one and
; that it survives the round trip.
proc fx_node, kind, aux, a, b, soff
	uses	rbx
	locals
	endl
	lea	rdi, [fx_sp]
	mov	rsi, 7
	mov	rdx, [soff]
	mov	rcx, 3
	call	span_make
	lea	rdi, [fx_tree]
	mov	rsi, [kind]
	mov	rdx, [aux]
	mov	rcx, [a]
	mov	r8, [b]
	lea	r9, [fx_sp]
	call	ast_node
	return
endp

proc fx_decl, tree, kind, flags, name, parent, span
	locals
	endl
	lea	rdi, [fx_sp]
	mov	rsi, 7
	mov	rdx, 0
	mov	rcx, 3
	call	span_make
	mov	rdi, [tree]
	mov	rsi, [kind]
	mov	rdx, [flags]
	mov	rcx, [name]
	mov	r8, [parent]
	lea	r9, [fx_sp]
	call	ast_decl
	return
endp

; A one-element `extra` list: rax = offset, rdx = count.
proc fx_list1, v
	uses	rbx
	locals
	endl
	lea	rdi, [fx_tree]
	call	ast_list_mark
	mov	rbx, rax
	lea	rdi, [fx_tree]
	mov	rsi, [v]
	call	ast_list_push
	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_list_emit
	return
endp

segment readable writeable
  fx_t_dot:	db 'dot'
  fx_t_a:	db 'a'
  fx_t_f32:	db 'f32'
  fx_id_dot:	rq 1
  fx_id_a:	rq 1
  fx_id_f32:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_arena2:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_tree2:	rb sizeof.Ast
  fx_sp:	rb sizeof.Span
  fx_wr:	rb sizeof.DiagOut
  fx_wr2:	rb sizeof.DiagOut
  fx_buf:	rb 8192
  fx_buf2:	rb 8192
