; tests/unit/cst_structlit.asm
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
; cst fixture: spec §8.6's struct literal and `ExprNS`, `[UNTESTED]` in the
; spec until this runs (docs/design/wire-codec.md D3), through the real lexer
; and `cst_parse`. Every row is one source; the row pins the diagnostic count,
; the first diagnostic's code and offset when there is one, and how many
; `STRUCT_LIT_SUFFIX` and `FIELD_INIT` nodes the tree's dump holds -- so a
; parser that read a literal where a block was meant, or a block where a
; literal was meant, changes a count even when it raises nothing.
;
;   row  source (inside `publica functio f() -> u8 { ... }`)     diag  lit init
;    1   x = S { a: 1, b: 2 };                                      0     1    2
;    2   x = S { };               (the grammar admits it; E0304
;                                  is the checker's)                0     1    0
;    3   x = S { a: T { b: 1 } }; (a literal inside a literal)      0     2    2
;    4   si x { redde 1; }        (ExprNS: `{` is the block)        0     0    0
;    5   dum x terminus n { perge; } per i in 0..n { perge; }
;        discerne x { casus 1 { } } sub alloc = a { }
;                                 (the other ExprNS positions)      0     0    0
;    6   si (S { a: 1 }).a eq 1 { redde 1; }
;                                 (parenthesised in ExprNS, and the
;                                  flag comes BACK for the block)   0     1    1
;    7   si g(S { a: 1 }) { redde 1; }
;                                 (a call's brackets admit it)      0     1    1
;    8   x = S { a: 1, };         (trailing comma: E0201 at `}`)    1@43  1    1
;    9   x = S { a: 1 b: 2 };     (commas are mandatory: E0201
;                                  at `b`, and more after it)      >=1@42 -    -
;   10   x = f(1) { a: 1 };       (not after a path segment: the
;                                  `{` is not a literal)           >=1    0    0
;
; Line 1 is 28 bytes, so the tab of line 2 is byte 28 and its first token
; byte 29; the offsets above are computed from there.
;
; Exit 0 = every row held; 10+N = row N failed (its dump is printed when the
; fixture is run with any argument); 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/cst/cst.inc'

CT_ARENA = 4 shl 20
CT_BUF   = 64 shl 10
CT_ROW   = 40		; dq src, len; dd ndiag, code, off, nlit, ninit, pad
CT_ANY   = -1		; ndiag: "at least one"; nlit/ninit/off: not checked

segment readable executable

  start:
	mov	rax, [rsp]
	mov	[ct_argc], rax
	call	ct_setup

	xor	r12, r12
  .row:
	cmp	r12, CT_NROWS
	jae	.done
	mov	rax, r12
	imul	rax, CT_ROW
	lea	r13, [ct_tab]
	add	r13, rax
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	call	ct_run
	call	ct_dump
	; the diagnostic count
	call	ct_ndiag
	mov	r14, rax
	mov	ecx, [r13 + 16]
	cmp	ecx, CT_ANY
	je	.atleast
	cmp	r14, rcx
	jne	.bad
	test	r14, r14
	jz	.counts
	jmp	.first
  .atleast:
	test	r14, r14
	jz	.bad
  .first:
	xor	edi, edi
	call	ct_diag
	mov	ecx, [r13 + 20]
	cmp	[rax + Diag.code_num], ecx
	jne	.bad
	mov	ecx, [r13 + 24]
	cmp	ecx, CT_ANY
	je	.counts
	cmp	[rax + Diag.span.start], ecx
	jne	.bad
  .counts:
	mov	ecx, [r13 + 28]
	cmp	ecx, CT_ANY
	je	.inits
	lea	rdi, [kw_lit]
	mov	rsi, KW_LIT_LEN
	call	ct_count
	mov	ecx, [r13 + 28]
	cmp	rax, rcx
	jne	.bad
  .inits:
	mov	ecx, [r13 + 32]
	cmp	ecx, CT_ANY
	je	.next
	lea	rdi, [kw_init]
	mov	rsi, KW_INIT_LEN
	call	ct_count
	mov	ecx, [r13 + 32]
	cmp	rax, rcx
	jne	.bad
  .next:
	inc	r12
	jmp	.row
  .bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .done:
	xor	edi, edi
	call	sys_exit_group

; ---- harness ---------------------------------------------------------------
; Plain labels, not `proc`, as in cst_error_tolerance.asm: a `proc` argument
; name is an unmangled global (macros/proc.inc's header). Every helper pushes
; an ODD number of registers so `rsp` is 16-aligned at the calls inside it.

; ct_setup -- two arenas and an interner. The interner's arena is never
; reset: leaf text lives in it.
  ct_setup:
	push	rbx
	lea	rdi, [ct_arena]
	mov	rsi, CT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [ct_iarena]
	mov	rsi, CT_ARENA
	call	arena_init
	jc	.boom
	lea	rdi, [ct_intern]
	lea	rsi, [ct_iarena]
	mov	rdx, 1024
	call	intern_init
	pop	rbx
	ret
  .boom:
	mov	edi, 99
	call	sys_exit_group

; ct_run(rdi = source bytes, rsi = length) -- lex and parse into a fresh tree.
  ct_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	lea	rdi, [ct_arena]
	call	arena_reset
	lea	rdi, [ct_toks]
	lea	rsi, [ct_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 64
	call	vec_init
	lea	rdi, [ct_diags]
	lea	rsi, [ct_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 8
	call	vec_init
	lea	rdi, [ct_lx]
	lea	rsi, [ct_arena]
	lea	rdx, [ct_intern]
	lea	rcx, [ct_toks]
	lea	r8,  [ct_diags]
	call	lex_init
	lea	rdi, [ct_lx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [ct_path]
	mov	r8d, CT_PATH_LEN
	call	lex_set_source
	lea	rdi, [ct_lx]
	call	lex_run
	jc	.boom
	lea	rdi, [ct_green]
	lea	rsi, [ct_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 64
	call	vec_init
	lea	rdi, [ct_work]
	lea	rsi, [ct_arena]
	mov	rdx, 4
	mov	rcx, 64
	call	vec_init
	lea	rdi, [ct_map]
	lea	rsi, [ct_arena]
	mov	rdx, 256
	call	map_init
	lea	rdi, [ct_tree]
	lea	rsi, [ct_arena]
	lea	rdx, [ct_intern]
	lea	rcx, [ct_green]
	lea	r8,  [ct_work]
	lea	r9,  [ct_map]
	call	cst_tree_init
	lea	rdi, [ct_p]
	lea	rsi, [ct_lx]
	lea	rdx, [ct_tree]
	call	cst_parse
	pop	r13
	pop	r12
	pop	rbx
	ret
  .boom:
	; a sample that fails spec §8.1 is a broken fixture, not a parse result
	mov	edi, 99
	call	sys_exit_group

; ct_dump -- the last tree's dump into ct_buf; ct_len = its length. Printed to
; stdout as well when the fixture was given an argument.
  ct_dump:
	push	rbx
	lea	rdi, [ct_out]
	lea	rsi, [ct_buf]
	mov	rdx, CT_BUF
	call	diag_out_init
	lea	rdi, [ct_tree]
	lea	rsi, [ct_out]
	xor	edx, edx
	call	cst_dump
	mov	rax, [ct_out + DiagOut.len]
	mov	[ct_len], rax
	cmp	qword [ct_argc], 1
	jbe	.quiet
	mov	rdi, 1
	lea	rsi, [ct_buf]
	mov	rdx, [ct_len]
	call	sys_write
  .quiet:
	pop	rbx
	ret

; ct_count(rdi = needle, rsi = needle length) -> rax = how many times the
; needle occurs in the last dump. A byte search, overlapping matches counted.
  ct_count:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	xor	eax, eax		; matches
	xor	ecx, ecx		; start position
  .pos:
	mov	rdx, rcx
	add	rdx, r13
	cmp	rdx, [ct_len]
	ja	.out
	xor	r8, r8
  .cmp:
	cmp	r8, r13
	jae	.hit
	lea	r9, [ct_buf]
	add	r9, rcx
	mov	r10b, [r9 + r8]
	cmp	r10b, [r12 + r8]
	jne	.miss
	inc	r8
	jmp	.cmp
  .hit:
	inc	rax
  .miss:
	inc	rcx
	jmp	.pos
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; ct_ndiag -> rax = diagnostics from the last ct_run (lexer's and parser's).
  ct_ndiag:
	mov	rax, [ct_diags + Vec.len]
	ret

; ct_diag(rdi = index) -> rax = that `Diag`.
  ct_diag:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [ct_diags]
	call	vec_get
	pop	rbx
	ret

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'
  ct_path:	db 'cst_structlit.exsc'
  CT_PATH_LEN = $ - ct_path

  macro ct_src name, line
	name: db 'publica functio f() -> u8 {', 10, 9, line, 10, '}', 10
	name#_LEN = $ - name
  end macro

  ct_src ct_s01, 'x = S { a: 1, b: 2 };'
  ct_src ct_s02, 'x = S { };'
  ct_src ct_s03, 'x = S { a: T { b: 1 } };'
  ct_src ct_s04, 'si x { redde 1; }'
  ct_src ct_s05, 'dum x terminus n { perge; } per i in 0..n { perge; } discerne x { casus 1 { } } sub alloc = a { }'
  ct_src ct_s06, 'si (S { a: 1 }).a eq 1 { redde 1; }'
  ct_src ct_s07, 'si g(S { a: 1 }) { redde 1; }'
  ct_src ct_s08, 'x = S { a: 1, };'
  ct_src ct_s09, 'x = S { a: 1 b: 2 };'
  ct_src ct_s10, 'x = f(1) { a: 1 };'

  kw_lit:	db 'STRUCT_LIT_SUFFIX@'
  KW_LIT_LEN = $ - kw_lit
  kw_init:	db 'FIELD_INIT@'
  KW_INIT_LEN = $ - kw_init

  ct_tab:
	dq ct_s01, ct_s01_LEN
	dd 0, 0, 0, 1, 2, 0
	dq ct_s02, ct_s02_LEN
	dd 0, 0, 0, 1, 0, 0
	dq ct_s03, ct_s03_LEN
	dd 0, 0, 0, 2, 2, 0
	dq ct_s04, ct_s04_LEN
	dd 0, 0, 0, 0, 0, 0
	dq ct_s05, ct_s05_LEN
	dd 0, 0, 0, 0, 0, 0
	dq ct_s06, ct_s06_LEN
	dd 0, 0, 0, 1, 1, 0
	dq ct_s07, ct_s07_LEN
	dd 0, 0, 0, 1, 1, 0
	dq ct_s08, ct_s08_LEN
	dd 1, 201, 43, 1, 1, 0
	dq ct_s09, ct_s09_LEN
	dd CT_ANY, 201, 42, CT_ANY, CT_ANY, 0
	dq ct_s10, ct_s10_LEN
	dd CT_ANY, 201, CT_ANY, 0, 0, 0
  CT_NROWS = ($ - ct_tab) / CT_ROW
  ; a row of the wrong width would shift every row after it -- which this
  ; fixture's first draft did (CT_ROW 48 for 40 bytes), and row 2 read an
  ; empty source
  assert ($ - ct_tab) mod CT_ROW = 0

segment readable writeable
  ct_argc:	rq 1
  ct_len:	rq 1
  ct_arena:	rb sizeof.Arena
  ct_iarena:	rb sizeof.Arena
  ct_intern:	rb sizeof.Interner
  ct_toks:	rb sizeof.Vec
  ct_diags:	rb sizeof.Vec
  ct_lx:	rb sizeof.Lexer
  ct_green:	rb sizeof.Vec
  ct_work:	rb sizeof.Vec
  ct_map:	rb sizeof.Map
  ct_tree:	rb sizeof.CstTree
  ct_p:		rb sizeof.CstParser
  ct_out:	rb sizeof.DiagOut
  ct_buf:	rb CT_BUF
