; tests/unit/chk_ty_hexlit.asm
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
; checker fixture -- the VALUE of a hexadecimal integer literal (spec §8.4 as
; amended, which cites this fixture; docs/design/wire-codec.md D6), as
; checker/types/sig.inc's `chk_ty_litval` computes it and `__chk_ty_fits`
; then checks it against the width the literal lands in.
;
; THE TOKEN IS NOT BUILT BY THE LEXER HERE, deliberately. Each row's source
; carries a decimal placeholder, the front end runs normally, and then the
; ONE `Lit` node's text id is re-pointed at the interned hex spelling before
; `chk_run` -- exactly the tree the lexer's `INT` token produces, since
; `Lit.a` is an interner id and the checker reads nothing else of the token
; (typed-ast.md section 2.3: `Lit` keeps text).
;
; This was written while the lexer's half of D6 was landing separately, and
; said the indirection would be deleted once it had. It has landed
; (lexer/lex.inc lexes `0x[0-9a-fA-F]+` as one INT), and the indirection
; STAYS, for a reason that only became visible then: three of the rows
; below -- `0x`, `0x1g`, `0X1` -- are malformed spellings the lexer now
; rejects as `EXS-E0210` before the checker ever sees them. Written as
; source they would test the lexer and stop testing `chk_ty_litval`'s own
; refusal, which is this fixture's subject; the well-formed rows are also
; covered from real source by chk_ty_structlit.asm (`0x1234`) and
; lwr_forma.asm (`0xdeadbeef`, `0xab12cd`).
;
; Rows, each checking the diagnostic count and, when accepted, the `konst`:
;
;	0xd3                  -> u8    accepted, 211     (the DeModFrame signum)
;	0xD3                  -> u8    accepted, 211     (upper case is a digit)
;	0xAbC                 -> u16   accepted, 2748    (mixed case)
;	0x100                 -> u8    EXS-E0308         (fits u16, not u8)
;	0xffffffffffffffff    -> u64   accepted, 2^64-1  (sixteen digits)
;	0x10000000000000000   -> u64   EXS-E0308         (seventeen: past 64 bits)
;	0x0000000000000000001 -> u8    accepted, 1       (leading zeros are free)
;	0x                    -> u8    EXS-E0308         (no digit)
;	0x1g                  -> u8    EXS-E0308         (not a hex digit)
;	0X1                   -> u8    EXS-E0308         (one spelling of the prefix)
;	211                   -> u8    accepted, 211     (decimal is unchanged)
;
; Exit 0 = every row held; 10+N = row N's diagnostic count; 40+N = row N's
; value; 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 40		; dq src, srclen, hex, hexlen, value -- the value is read
			; only when the expected count (in `hex`'s row) is 0

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
	mov	rdx, [r13 + 16]
	mov	ecx, [r13 + 24]
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
	mov	ecx, [r13 + 28]		; expected diagnostic count
	cmp	r14, rcx
	jne	.bad_count
	test	r14, r14
	jnz	.next
	lea	rdi, [fx_tree]
	mov	rsi, [fx_lit]
	call	ast_konst_get
	cmp	rax, [r13 + 32]
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
; pushes an odd number of registers.

; fx_run(rdi = source, rsi = length, rdx = hex text, ecx = its length) -> rax
; = the diagnostics `chk_run` appended, -1 if the front end said anything.
; `fx_lit` is left holding the one `Lit` node's id.
  fx_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	mov	[fx_hexp], rdx
	mov	[fx_hexn], rcx
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

	; ---- the one `Lit`, re-pointed at the hex spelling -------------------
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
	cmp	qword [fx_hexn], 0
	je	.check
	lea	rdi, [fx_names]
	mov	rsi, [fx_hexp]
	mov	rdx, [fx_hexn]
	call	intern_id
	mov	rbx, rax
	lea	rdi, [fx_tree]
	mov	rsi, [fx_lit]
	call	ast_node_at
	mov	[rax + AstNode.a], ebx

  .check:
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chk]
	mov	rsi, 64			; --hospes x86_64-linux (spec §9.5)
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

  fx_path	db 'chk_ty_hexlit.exsc'
  FX_PATH_LEN = $ - fx_path

  fx_u8:	db 'publica functio f() -> u8 {', 10, '    redde 7;', 10, '}', 10
  fx_u8_LEN = $ - fx_u8
  fx_u16:	db 'publica functio f() -> u16 {', 10, '    redde 7;', 10, '}', 10
  fx_u16_LEN = $ - fx_u16
  fx_u64:	db 'publica functio f() -> u64 {', 10, '    redde 7;', 10, '}', 10
  fx_u64_LEN = $ - fx_u64
  fx_d211:	db 'publica functio f() -> u8 {', 10, '    redde 211;', 10, '}', 10
  fx_d211_LEN = $ - fx_d211

  macro fx_hex name, text
	name: db text
	name#_LEN = $ - name
  end macro
  fx_hex fx_h01, '0xd3'
  fx_hex fx_h02, '0xD3'
  fx_hex fx_h03, '0xAbC'
  fx_hex fx_h04, '0x100'
  fx_hex fx_h05, '0xffffffffffffffff'
  fx_hex fx_h06, '0x10000000000000000'
  fx_hex fx_h07, '0x0000000000000000001'
  fx_hex fx_h08, '0x'
  fx_hex fx_h09, '0x1g'
  fx_hex fx_h10, '0X1'

  ; dq source, its length, hex text, and then (as dd) its length and the
  ; expected diagnostic count, then dq the expected `konst`
  macro fx_row src, hex, count, value
	dq src, src#_LEN, hex
	dd hex#_LEN, count
	dq value
  end macro
  fx_nohex:
  fx_nohex_LEN = 0

  fx_tab:
	fx_row fx_u8,   fx_h01, 0, 0xD3
	fx_row fx_u8,   fx_h02, 0, 0xD3
	fx_row fx_u16,  fx_h03, 0, 0xABC
	fx_row fx_u8,   fx_h04, 1, 0
	fx_row fx_u64,  fx_h05, 0, 0xFFFFFFFFFFFFFFFF
	fx_row fx_u64,  fx_h06, 1, 0
	fx_row fx_u8,   fx_h07, 0, 1
	fx_row fx_u8,   fx_h08, 1, 0
	fx_row fx_u8,   fx_h09, 1, 0
	fx_row fx_u8,   fx_h10, 1, 0
	fx_row fx_d211, fx_nohex, 0, 211
  FX_NROWS = ($ - fx_tab) / FX_ROW

segment readable writeable
  fx_lit:	rq 1
  fx_hexp:	rq 1
  fx_hexn:	rq 1
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
