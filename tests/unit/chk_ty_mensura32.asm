; tests/unit/chk_ty_mensura32.asm
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
; checker fixture -- `mensura` at a 32-bit target width (spec §9.5's
; `mips64-none-o64` row; ADR 0015 decision 1). C4's first commit of record.
;
; WHY THIS EXISTS. `mensura` is the target's `usize`, and its width comes
; from `ChkCtx.ptrbits` by way of `chk_set_target` -- one call site, which
; until now has always been handed a literal 64, because both accepted
; `--hospes` rows are 64-bit. So the 32-bit path through the checker has
; never run anywhere in this tree, and `checker/types/types.inc`'s
; `__chk_ty_fits` has a `cmp r14, 64 / jae .yes` short-circuit above its
; `mensura` arm which means **no integer literal has ever been range-checked
; against `mensura` at all**. This fixture is what makes that arm run.
;
; It is deliberately not a test that 32 is "supported": no `--hospes` row
; accepts it yet (`driver/run.inc` refuses `mips64-none-o64` by name), and
; this fixture reaches `chk_set_target` directly rather than through the
; driver, exactly so that the checker's half can be landed and checked
; before the row that would expose it. ADR 0015's commit sequence turns on
; that: the row is accepted LAST, because accepting it fires every
; consequence at once.
;
; WHAT IT PINS. Each row compiles real source through the whole front end,
; then runs the checker at a stated target width, and checks the diagnostic
; count and (when the row is accepted) the literal's folded `konst`:
;
;	ptrbits  literal                   expected
;	32       4294967295                accepted, 2^32-1          -- the widest that fits
;	32       4294967296                EXS-E0308                 -- 2^32 does not
;	64       4294967296                accepted, 2^32            -- the SAME literal, other row
;	64       18446744073709551615      accepted, 2^64-1          -- 64 is unchanged
;	32       18446744073709551615      EXS-E0308
;	32       1024                      accepted, 1024            -- the ordinary case still works
;
; Rows 2 and 3 are the pair that matters: one literal, two target widths,
; two verdicts. They are what proves the width is read from the target and
; not baked into the literal -- and, by the same token, that `mensura`
; really is interned at 32 when the target says so, since a `mensura` still
; 64 bits wide would accept row 2.
;
; NOT PINNED HERE, and worth saying so: this fixture says nothing about
; whether `+` traps at 2^32 (that is a lowering and backend fact, ADR 0015
; decision 3), nor about aggregate layout at 32 -- `chk_row_layout_src.asm`
; exercises the layout pass, and its `wptr` comes from this same `ptrbits`.
; A `mensura` field inside a `@transitus` struct was a suspected hole and is
; not one: measured at 14a4001, it is already `EXS-E0321` unannotated and
; `EXS-E0309` with an order annotation, so the wire format cannot become
; target-dependent through this row (ADR 0015, context measurement 6).
;
; Exit 0 = every row held; 10+N = row N's diagnostic count; 40+N = row N's
; value; 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 32		; dq src, srclen; dd ptrbits, count; dq value

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99

	xor	r12, r12
  .row:
	cmp	r12, FX_NROWS
	jae	.done
	mov	rax, r12
	imul	rax, FX_ROW
	lea	r13, [fx_tab]
	add	r13, rax
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	mov	edx, [r13 + 16]
	call	fx_run
	mov	r14, rax
	xor	ebx, ebx
  .show:
	cmp	rbx, r14
	jae	.shown
	mov	rdi, rbx
	call	fx_code
	inc	rbx
	jmp	.show
  .shown:
	mov	ecx, [r13 + 20]		; expected diagnostic count
	cmp	r14, rcx
	jne	.bad_count
	test	r14, r14
	jnz	.next
	lea	rdi, [fx_tree]
	mov	rsi, [fx_lit]
	call	ast_konst_get
	cmp	rax, [r13 + 24]
	jne	.bad_value
  .next:
	inc	r12
	jmp	.row
  .bad_count:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .bad_value:
	lea	rdi, [r12 + 41]
	call	sys_exit_group
  .done:
	xor	edi, edi
	call	sys_exit_group
  .fail99:
	mov	edi, 99
	call	sys_exit_group


include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; ---- harness ---------------------------------------------------------------
; Plain labels (a `proc` argument name is an unmangled global); each helper
; pushes an odd number of registers. The shape is chk_ty_hexlit.asm's, with
; the hex-spelling indirection dropped -- every row here is ordinary source
; the lexer accepts -- and a per-row target width added.

; fx_run(rdi = source, rsi = length, edx = ptrbits) -> rax = the diagnostics
; `chk_run` appended, -1 if the front end said anything. `fx_lit` is left
; holding the one `Lit` node's id.
  fx_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	mov	[fx_bits], edx
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_scratch]
	call	arena_reset
	lea	rdi, [fx_toks]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_diags]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 32
	call	vec_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_toks]
	lea	r8,  [fx_diags]
	call	lex_init
	lea	rdi, [fx_lx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	jc	.gate
	lea	rdi, [fx_green]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 512
	call	vec_init
	lea	rdi, [fx_work]
	lea	rsi, [fx_arena]
	mov	rdx, 4
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_cmap]
	lea	rsi, [fx_arena]
	mov	rdx, 1024
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
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	cmp	qword [fx_diags + Vec.len], 0
	jne	.gate

	; ---- the one `Lit` ---------------------------------------------------
	mov	qword [fx_lit], 0
	mov	rbx, 1
  .scan:
	lea	rdi, [fx_tree]
	call	ast_node_count
	cmp	rbx, rax
	ja	.scanned
	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_node_at
	cmp	word [rax + AstNode.kind], AST_LIT
	jne	.scan_next
	cmp	qword [fx_lit], 0
	jne	.gate			; two literals: the row is malformed
	mov	[fx_lit], rbx
  .scan_next:
	inc	rbx
	jmp	.scan
  .scanned:
	cmp	qword [fx_lit], 0
	je	.gate

	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chk]
	mov	esi, [fx_bits]		; the row's target width -- 64 is
					; x86_64-linux/riscv64-linux, 32 is
					; mips64-none-o64 (spec §9.5)
	call	chk_set_target
	lea	rdi, [fx_chk]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	pop	r13
	pop	r12
	pop	rbx
	ret
  .gate:
	mov	rax, -1
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_code(rdi = i) -- render diagnostic `i` to stdout.
  fx_code:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [fx_diags]
	call	vec_get
	mov	rbx, rax
	mov	rdi, 1
	mov	rsi, rbx
	lea	rdx, [fx_buf]
	mov	rcx, 8192
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit
	pop	rbx
	ret

; fx_setup -> eax = 0 once the arenas and the interner exist.
  fx_setup:
	push	rbx
	lea	rdi, [fx_arena]
	mov	rsi, 32 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_iarena]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_scratch]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_names]
	lea	rsi, [fx_iarena]
	mov	rdx, 1024
	call	intern_init
	xor	eax, eax
	pop	rbx
	ret
  .bad:
	mov	eax, 1
	pop	rbx
	ret

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

  fx_path	db 'chk_ty_mensura32.exsc'
  FX_PATH_LEN = $ - fx_path

  macro fx_src name, text
	name: db 'publica functio f() -> mensura {', 10, '    redde ', text, ';', 10, '}', 10
	name#_LEN = $ - name
  end macro

  fx_src fx_max32,  '4294967295'		; 2^32 - 1
  fx_src fx_over32, '4294967296'		; 2^32
  fx_src fx_max64,  '18446744073709551615'	; 2^64 - 1
  fx_src fx_small,  '1024'

  ; dq source, its length; dd the target width and the expected diagnostic
  ; count; dq the expected `konst` (read only when the count is 0)
  macro fx_row src, bits, count, value
	dq src, src#_LEN
	dd bits, count
	dq value
  end macro

  fx_tab:
	fx_row fx_max32,  32, 0, 4294967295
	fx_row fx_over32, 32, 1, 0
	fx_row fx_over32, 64, 0, 4294967296
	fx_row fx_max64,  64, 0, 0FFFFFFFFFFFFFFFFh
	fx_row fx_max64,  32, 1, 0
	fx_row fx_small,  32, 0, 1024
  FX_NROWS = ($ - fx_tab) / FX_ROW

segment readable writeable
  fx_lit:	rq 1
  fx_bits:	rd 1
  fx_arena:	rb sizeof.Arena
  fx_iarena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_toks:	rb sizeof.Vec
  fx_diags:	rb sizeof.Vec
  fx_lx:	rb sizeof.Lexer
  fx_green:	rb sizeof.Vec
  fx_work:	rb sizeof.Vec
  fx_cmap:	rb sizeof.Map
  fx_ctree:	rb sizeof.CstTree
  fx_parser:	rb sizeof.CstParser
  fx_tree:	rb sizeof.Ast
  fx_chk:	rb sizeof.ChkCtx
  fx_buf:	rb 8192
