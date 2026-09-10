; tests/unit/ast_type_intern.asm
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
; ast fixture for compiler/x86_64/ast/types.inc -- the interned type table.
;
; The claim under test is docs/design/typed-ast.md section 2.5's, and it is a
; SPEC claim, not an implementation detail: spec §5.2 says byte order is "in
; the type" and spec §5.5 says placement is "in the type". This fixture is
; what makes those two sentences literally true rather than conventionally
; so -- `u32` and `u32:maior` and `u32 apud machina` are three different type
; ids because `order` and `place` are bytes of the interning key, and nothing
; anywhere else has to remember to keep them apart.
;
;   1. `ast_init` leaves exactly one type interned, and it is the error type
;      at id 1 (typed-ast.md 2.2's cascade-suppression reservation).
;   2. interning the same type twice gives one id -- the point of interning.
;   3. `u32` != `i32`               -- `sign` is in the key
;   4. `u32` != `u32:maior`         -- spec §5.2, `order` is in the key
;   5. `u32` != `u32 apud machina`  -- spec §5.5, `place` is in the key
;   6. `acies<f32,4>` != `acies<f32,8>` -- spec §5.4, the lane count is in
;      the type, so it can never be taken from the host
;   7. two `functio(u32) -> f32` types differing ONLY in their capability row
;      are different types -- spec §4.2, "the row is in the type", which is
;      what makes an escaped closure's authority travel with the value
;   8. ids are dense and assigned in FIRST-USE order (spec §9.3: an id is a
;      function of the input, never of a hash bucket or an address)
;   9. `ast_type_at` reads back what was interned
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
	mov	rsi, 1024 * 1024
	call	arena_init
	jc	.fail0
	lea	rdi, [fx_names]
	lea	rsi, [fx_arena]
	mov	rdx, 64
	call	intern_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init

	; ---- 1 ----
	lea	rdi, [fx_tree]
	call	ast_type_count
	cmp	rax, 1
	jne	.fail1
	lea	rdi, [fx_tree]
	mov	rsi, 1
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_ERROR
	jne	.fail1

	; ---- 2: u32, twice ----
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 32
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	cmp	rax, 2
	jne	.fail2
	mov	[fx_u32], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 32
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	cmp	rax, 2
	jne	.fail2
	lea	rdi, [fx_tree]
	call	ast_type_count
	cmp	rax, 2
	jne	.fail2

	; ---- 3: i32 ----
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_I
	mov	rdx, 32
	mov	rcx, AST_ORD_NATIVUS
	call	ast_type_int
	cmp	rax, 3
	jne	.fail3

	; ---- 4: u32:maior (spec §5.2) ----
	lea	rdi, [fx_tree]
	mov	rsi, AST_SIGN_U
	mov	rdx, 32
	mov	rcx, AST_ORD_MAIOR
	call	ast_type_int
	cmp	rax, 4
	jne	.fail4
	mov	rcx, [fx_u32]
	cmp	rax, rcx
	je	.fail4

	; ---- 5: u32 apud machina (spec §5.5) ----
	lea	rdi, [fx_t0]
	call	ast_type_clear
	mov	byte [fx_t0 + AstType.kind], AST_TY_INT
	mov	byte [fx_t0 + AstType.sign], AST_SIGN_U
	mov	byte [fx_t0 + AstType.order], AST_ORD_NATIVUS
	mov	byte [fx_t0 + AstType.place], AST_PLACE_MACHINA
	mov	dword [fx_t0 + AstType.width], 32
	lea	rdi, [fx_tree]
	lea	rsi, [fx_t0]
	call	ast_type_intern
	cmp	rax, 5
	jne	.fail5
	mov	rcx, [fx_u32]
	cmp	rax, rcx
	je	.fail5

	; ---- 6: acies<f32,4> and acies<f32,8> (spec §5.4) ----
	lea	rdi, [fx_tree]
	mov	rsi, 32
	call	ast_type_float
	cmp	rax, 6
	jne	.fail6
	mov	[fx_f32], rax
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_ACIES
	mov	rdx, [fx_f32]
	mov	rcx, 4
	call	ast_type_una
	cmp	rax, 7
	jne	.fail6
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_ACIES
	mov	rdx, [fx_f32]
	mov	rcx, 8
	call	ast_type_una
	cmp	rax, 8
	jne	.fail6
	lea	rdi, [fx_tree]
	mov	rsi, AST_TY_ACIES
	mov	rdx, [fx_f32]
	mov	rcx, 4
	call	ast_type_una
	cmp	rax, 7
	jne	.fail6

	; ---- 7: functio(u32) -> f32, with and without a row (spec §4.2) ----
	; The `extra` run is the parameter types followed by the result.
	lea	rdi, [fx_tree]
	mov	rsi, [fx_u32]
	call	ast_extra_push
	mov	[fx_off], rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_f32]
	call	ast_extra_push
	lea	rdi, [fx_tree]
	mov	rsi, [fx_off]
	mov	rdx, 1
	mov	rcx, 0			; no row
	call	ast_type_fn
	cmp	rax, 9
	jne	.fail7
	lea	rdi, [fx_tree]
	mov	rsi, [fx_off]
	mov	rdx, 1
	mov	rcx, 1			; row id 1 -- the same shape, more
					; authority, so a different type
	call	ast_type_fn
	cmp	rax, 10
	jne	.fail7

	; ---- 8: dense, first-use order ----
	lea	rdi, [fx_tree]
	call	ast_type_count
	cmp	rax, 10
	jne	.fail8

	; ---- 9: read it back ----
	lea	rdi, [fx_tree]
	mov	rsi, 4
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_INT
	jne	.fail9
	movzx	ecx, byte [rax + AstType.order]
	cmp	ecx, AST_ORD_MAIOR
	jne	.fail9
	mov	ecx, [rax + AstType.width]
	cmp	ecx, 32
	jne	.fail9
	lea	rdi, [fx_tree]
	mov	rsi, 7
	call	ast_type_at
	mov	ecx, [rax + AstType.width]
	cmp	ecx, 4
	jne	.fail9
	mov	ecx, [rax + AstType.a]
	mov	rdx, [fx_f32]
	cmp	rcx, rdx
	jne	.fail9

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
  .fail9:
	mov	rdi, 19
	call	sys_exit_group

include '../../compiler/x86_64/diag/diag.inc'
include '../../compiler/x86_64/ast/ast.inc'

segment readable writeable
  fx_arena:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_tree:	rb sizeof.Ast
  fx_t0:	rb sizeof.AstType
  fx_u32:	rq 1
  fx_f32:	rq 1
  fx_off:	rq 1
