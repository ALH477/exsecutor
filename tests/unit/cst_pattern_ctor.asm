; tests/unit/cst_pattern_ctor.asm
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
; cst + ast fixture: docs/design/sum-types.md D2's constructor patterns, parsed
; and built -- `Pattern ::= Literal | Path | Path '(' IDENT (',' IDENT)* ')'`.
;
;	typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u8);
;	functio f(m: modus) -> u32 {
;	    discerne m {
;	        casus ordinata { redde 0; }
;	        casus arborea(n) { redde 1; }
;	        casus par(a, b) { redde 2; }
;	    }
;	    redde 3;
;	}
;
;   1. Lexes and parses with NO diagnostics; `ast_from_cst` builds a tree
;      `ast_verify_stage1` accepts (the `Casus` row of `ast_roles` grew a
;      LIST/COUNT pair, and this is where the verifier walks it).
;   2. Exactly three `Casus` nodes, and their binding counts (`Casus.d`) in
;      node (= source) order are 0, 1, 2: a bare-path pattern has an empty
;      list, not a different shape.
;   3. Every binding is a `Binding` node with no annotation and no
;      initializer, whose `d` is an `AST_D_BINDING` declaration pointing back
;      at it -- three of them in all. Its NAME is not checked against text:
;      that `n` is `n` is the interner's business and pass 1's to resolve.
;   4. `casus arborea(alius(x))` -- a NESTED pattern -- is refused where its
;      inner `(` stands: the first diagnostic is `EXS-E0201`. D2 keeps
;      patterns flat on purpose (exhaustiveness stays a set-cover), so this
;      is a decision measured, not a limitation found.
;
; THIS FIXTURE STOPS AT THE AST: what the checker does with a constructor
; pattern is the next commit's (resolution against the scrutinee's type,
; D2's subtle half), and pass 1's scoping of the bindings is pinned by
; tests/unit/chk_pattern_scope.asm once measured.
;
; Exit 0 = all checks passed; 10+N = check N failed (tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 8 * 1024 * 1024
	call	arena_init
	jc	.fail0

	; ---- check 1 ----
	lea	rax, [fx_src]
	mov	[fx_sp], rax
	mov	qword [fx_sl], FX_SRC_LEN
	call	fx_parse
	cmp	qword [fx_ndiag], 0
	jne	.fail1
	cmp	qword [fx_root], 0
	je	.fail1

	lea	rdi, [fx_wr]
	lea	rsi, [fx_buf]
	mov	rdx, 65536
	call	diag_out_init
	lea	rdi, [fx_ast]
	lea	rsi, [fx_wr]
	call	ast_dump
	mov	rdi, 1
	lea	rsi, [fx_buf]
	mov	rdx, [fx_wr + DiagOut.len]
	call	sys_write

	; ---- check 2: three Casus, binding counts 0 1 2 ----
	xor	r13, r13			; Casus seen
	mov	r14, 1
  .scan2:
	lea	rdi, [fx_ast]
	call	ast_node_count
	cmp	r14, rax
	jg	.done2
	lea	rdi, [fx_ast]
	mov	rsi, r14
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_CASUS
	jne	.next2
	cmp	r13, 3
	jae	.fail2
	mov	edx, [rax + AstNode.d]		; binding count
	cmp	rdx, r13			; 0, 1, 2 in order
	jne	.fail2
	inc	r13
  .next2:
	inc	r14
	jmp	.scan2
  .done2:
	cmp	r13, 3
	jne	.fail2

	; ---- check 3: each binding is a bare Binding owning an AST_D_BINDING --
	xor	r13, r13			; bindings seen
	mov	r14, 1
  .scan3:
	lea	rdi, [fx_ast]
	call	ast_node_count
	cmp	r14, rax
	jg	.done3
	lea	rdi, [fx_ast]
	mov	rsi, r14
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_CASUS
	jne	.next3
	mov	ecx, [rax + AstNode.c]
	mov	[fx_off], rcx
	mov	ecx, [rax + AstNode.d]
	mov	[fx_cnt], rcx
	mov	qword [fx_i], 0
  .bind3:
	mov	rax, [fx_i]
	cmp	rax, [fx_cnt]
	jae	.next3
	lea	rdi, [fx_ast]
	mov	rsi, [fx_off]
	add	rsi, rax
	call	ast_extra_at
	mov	[fx_bn], rax
	lea	rdi, [fx_ast]
	mov	rsi, rax
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_BINDING
	jne	.fail3
	cmp	dword [rax + AstNode.a], 0
	jne	.fail3
	cmp	dword [rax + AstNode.b], 0
	jne	.fail3
	mov	ecx, [rax + AstNode.d]
	test	rcx, rcx
	jz	.fail3
	lea	rdi, [fx_ast]
	mov	rsi, rcx
	call	ast_decl_at
	cmp	byte [rax + AstDecl.kind], AST_D_BINDING
	jne	.fail3
	mov	ecx, [rax + AstDecl.node]
	cmp	rcx, [fx_bn]
	jne	.fail3
	inc	r13
	inc	qword [fx_i]
	jmp	.bind3
  .next3:
	inc	r14
	jmp	.scan3
  .done3:
	cmp	r13, 3
	jne	.fail3

	; ---- check 4: a nested pattern is refused, EXS-E0201 first ----
	lea	rax, [fx_bad]
	mov	[fx_sp], rax
	mov	qword [fx_sl], FX_BAD_LEN
	call	fx_parse
	cmp	qword [fx_ndiag], 1
	jb	.fail4
	lea	rdi, [fx_diags]
	xor	esi, esi
	call	vec_get
	mov	eax, [rax + Diag.code_num]
	cmp	eax, 201
	jne	.fail4

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
  .fail3:
	mov	rdi, 13
	call	sys_exit_group
  .fail4:
	mov	rdi, 14
	call	sys_exit_group

; ---------------------------------------------------------------------------
; The whole front end over [fx_sp]/[fx_sl], from a reset arena: lex, parse,
; build, verify. [fx_ndiag] = diagnostics from the lexer and parser together;
; [fx_root] = the module node, or 0. A parse that fails outright (`cst_parse`
; CF) counts as one diagnostic and no root.
  fx_parse:
	push	rbp
	mov	qword [fx_root], 0
	mov	qword [fx_ndiag], 0
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 256
	call	intern_init
	lea	rdi, [fx_toks]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 64
	call	vec_init
	lea	rdi, [fx_diags]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_toks]
	lea	r8,  [fx_diags]
	call	lex_init
	lea	rdi, [fx_lx]
	mov	rsi, [fx_sp]
	mov	rdx, [fx_sl]
	lea	rcx, [fx_path]
	mov	r8d, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	lea	rdi, [fx_green]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 64
	call	vec_init
	lea	rdi, [fx_work]
	lea	rsi, [fx_arena]
	mov	rdx, 4
	mov	rcx, 64
	call	vec_init
	lea	rdi, [fx_cmap]
	lea	rsi, [fx_arena]
	mov	rdx, 256
	call	map_init
	lea	rdi, [fx_ctree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_green]
	lea	r8,  [fx_work]
	lea	r9,  [fx_cmap]
	call	cst_tree_init
	lea	rdi, [fx_parser]
	lea	rsi, [fx_lx]
	lea	rdx, [fx_ctree]
	call	cst_parse
	jc	.broken
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	mov	[fx_ndiag], rax
	lea	rdi, [fx_ast]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rcx, [fx_lx]
	mov	edx, [rcx + Lexer.file_id]
	lea	rdi, [fx_ast]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	mov	[fx_root], rax
	test	rax, rax
	jz	.out
	lea	rdi, [fx_ast]
	call	ast_verify_stage1
  .out:
	pop	rbp
	ret
  .broken:
	mov	qword [fx_ndiag], 1
	pop	rbp
	ret

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

segment readable
  ; The UCD blobs, emitted exactly once and AFTER every `proc` in this file
  ; (tests/unit/ast_from_cst.asm's header records why).
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path:	db 'fixture.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_src:	db 'typus modus = casus ordinata, casus arborea(mensura), casus par(u8, u8);', 10
		db 'functio f(m: modus) -> u32 {', 10
		db '    discerne m {', 10
		db '        casus ordinata { redde 0; }', 10
		db '        casus arborea(n) { redde 1; }', 10
		db '        casus par(a, b) { redde 2; }', 10
		db '    }', 10
		db '    redde 3;', 10
		db '}', 10
  FX_SRC_LEN = $ - fx_src
  fx_bad:	db 'typus modus = casus ordinata, casus arborea(mensura);', 10
		db 'functio g(m: modus) -> u32 {', 10
		db '    discerne m {', 10
		db '        casus arborea(alius(x)) { redde 1; }', 10
		db '    }', 10
		db '    redde 0;', 10
		db '}', 10
  FX_BAD_LEN = $ - fx_bad
  fx_sp:	rq 1
  fx_sl:	rq 1
  fx_root:	rq 1
  fx_ndiag:	rq 1
  fx_off:	rq 1
  fx_cnt:	rq 1
  fx_i:		rq 1
  fx_bn:	rq 1
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_toks:	rb sizeof.Vec
  fx_diags:	rb sizeof.Vec
  fx_lx:	rb sizeof.Lexer
  fx_green:	rb sizeof.Vec
  fx_work:	rb sizeof.Vec
  fx_cmap:	rb sizeof.Map
  fx_ctree:	rb sizeof.CstTree
  fx_parser:	rb sizeof.CstParser
  fx_ast:	rb sizeof.Ast
  fx_wr:	rb sizeof.DiagOut
  fx_buf:	rb 65536
