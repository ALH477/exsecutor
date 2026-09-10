; tests/unit/ast_from_cst_abi.asm
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
; ast fixture for `Externus.aux` as spec §5.3's CLOSED ABI ENUMERATION.
;
; THIS REPLACES tests/unit/ast_from_cst_abi_overflow.asm, WHOSE PREMISE THE
; SPEC SUPERSEDED. That fixture preseeded the interner with 65536 entries and
; required `ast_from_cst` to TRAP, because `Externus.aux` held the ABI
; identifier's interner id -- unbounded -- in sixteen bits, and trapping was
; the honest interim behaviour (ast/ast.inc, finding 10). §5.3 now reads:
; *"their spellings ... are `sysv_amd64`, `aapcs64` and `lp64d`, and the set
; is closed: the AST stores the ABI as a small enumeration, not an interned
; name, so an unknown one is `EXS-E0309` at the `externus` head rather than a
; value that overflows a 16-bit slot"*. There is no overflow left to prove:
; the widest value is 3. The old fixture is deleted rather than adapted --
; it asserted a trap that must no longer happen.
;
; Four whole-front-end runs, one per row of `fx_abi_tab`: §8.1 policy, §8.4
; tokens, §8.6 parse, §9.1 CST, then the Stage 1 walk. Each asserts the ABI
; the walker recorded:
;
;	externus("libc", abi: sysv_amd64) { }	-> 1  AST_ABI_SYSV_AMD64
;	externus("libc", abi: aapcs64) { }	-> 2  AST_ABI_AAPCS64
;	externus("libc", abi: lp64d) { }	-> 3  AST_ABI_LP64D
;	externus("libc", abi: quidlibet) { }	-> 0  AST_ABI_UNKNOWN
;
; THE FOURTH ROW IS THE ONE WITH A DECISION IN IT. An unrecognised spelling
; is `EXS-E0309`, and this walker does not raise it: `AstWalk` has no
; diagnostic sink and the walk resolves nothing (ast/from_cst.inc's header).
; It records 0 and says nothing; Stage 2 raises the code at this node's span,
; and since diag/render.inc quotes the source line, the tree does not have to
; keep the spelling. So the fourth row must lex, parse, WALK and VERIFY
; cleanly -- it is not a negative fixture, and `ast_verify_stage1` accepting
; it is part of the claim.
;
; Check 5 is the textual form: the dump prints the ABI NAME (`sysv_amd64`),
; not the integer 1, and `ast_load` reads that name back to the same tree.
; Both halves are asserted, because a dump and a load that were both reverted
; to integers would still round-trip -- only the byte search catches that.
;
; Exit 0 = all checks passed. 21..24 = row 1..4 of the table disagreed;
; 15 = the dump/load round trip; 16 = the dump did not contain the spelling;
; 99 = setup (a diagnostic from the lexer or the parser, or no `Externus`
; node in the tree at all).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	lea	rdi, [fx_arena]
	mov	rsi, 16 * 1024 * 1024
	call	arena_init
	jc	.setup

	mov	r12, 1
  .row:
	mov	rax, [fx_abi_n]
	cmp	r12, rax
	jg	.rows_done
	mov	rcx, r12
	dec	rcx
	shl	rcx, 4			; three qwords, padded to 32 bytes
	shl	rcx, 1
	lea	r13, [fx_abi_tab]
	add	r13, rcx
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	call	fx_abi
	jc	.setup
	cmp	rax, [r13 + 16]
	jne	.row_bad
	inc	r12
	jmp	.row
  .row_bad:
	lea	rdi, [r12 + 20]		; 21..24 names the row that disagreed
	call	sys_exit_group
  .rows_done:

	; ---- check 5: the dump prints the NAME, and the load reads it ----
	; The tree left behind by the last row is the `quidlibet` one, so run
	; the first row again to dump a recognised ABI.
	mov	rdi, [fx_abi_tab]
	mov	rsi, [fx_abi_tab + 8]
	call	fx_abi
	jc	.setup

	lea	rdi, [fx_wr]
	lea	rsi, [fx_buf]
	mov	rdx, 16384
	call	diag_out_init
	lea	rdi, [fx_ast]
	lea	rsi, [fx_wr]
	call	ast_dump
	mov	eax, [fx_wr + DiagOut.trunc]
	test	eax, eax
	jnz	.fail5
	mov	rdi, 1
	lea	rsi, [fx_buf]
	mov	rdx, [fx_wr + DiagOut.len]
	call	sys_write

	; the spelling is in the bytes -- a plain scan, no parser
	xor	r12, r12
  .scan:
	mov	rax, [fx_wr + DiagOut.len]
	sub	rax, 10			; length of `sysv_amd64`
	cmp	r12, rax
	jg	.fail6
	lea	rdi, [fx_buf]
	add	rdi, r12
	lea	rsi, [fx_sysv]
	mov	rdx, 10
	call	__map_bytes_equal
	test	eax, eax
	jnz	.found
	inc	r12
	jmp	.scan
  .found:

	lea	rdi, [fx_arena2]
	mov	rsi, 8 * 1024 * 1024
	call	arena_init
	jc	.setup
	lea	rdi, [fx_ast2]
	lea	rsi, [fx_arena2]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_ast2]
	lea	rsi, [fx_buf]
	mov	rdx, [fx_wr + DiagOut.len]
	call	ast_load
	lea	rdi, [fx_ast2]
	call	ast_verify
	lea	rdi, [fx_ast]
	lea	rsi, [fx_ast2]
	call	ast_equal
	cmp	rax, 1
	jne	.fail5

	xor	edi, edi
	call	sys_exit_group
  .setup:
	mov	rdi, 99
	call	sys_exit_group
  .fail5:
	mov	rdi, 15
	call	sys_exit_group
  .fail6:
	mov	rdi, 16
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'

; One source through the whole front end; rax = the `Externus` node's `aux`.
; `fail 99` (CF set) for anything that stopped it getting there -- a lexer or
; parser diagnostic, a walk with no root, or a tree with no `Externus` node --
; so the caller's `jc` tells the two kinds of failure apart.
proc fx_abi, src, len
	uses	rbx, r12, r13
	locals
	endl
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
	mov	rsi, [src]
	mov	rdx, [len]
	lea	rcx, [fx_path]
	mov	r8d, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	jc	.bad
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	test	rax, rax
	jnz	.bad

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
	jc	.bad
	lea	rax, [fx_diags]
	mov	rax, [rax + Vec.len]
	test	rax, rax
	jnz	.bad

	lea	rdi, [fx_ast]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rcx, [fx_lx]
	mov	edx, [rcx + Lexer.file_id]
	lea	rdi, [fx_ast]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	test	rax, rax
	jz	.bad
	lea	rdi, [fx_ast]
	call	ast_verify_stage1	; an unknown ABI is still a VALID tree

	mov	r12, 1
  .node:
	lea	rdi, [fx_ast]
	call	ast_node_count
	cmp	r12, rax
	jg	.bad
	lea	rdi, [fx_ast]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_EXTERNUS
	je	.here
	inc	r12
	jmp	.node
  .here:
	movzx	eax, word [rax + AstNode.aux]
	return
  .bad:
	fail	99
endp

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

  fx_s_sysv	db 'externus("libc", abi: sysv_amd64) {', 10, '}', 10
  FX_S_SYSV_LEN = $ - fx_s_sysv
  fx_s_aapcs	db 'externus("libc", abi: aapcs64) {', 10, '}', 10
  FX_S_AAPCS_LEN = $ - fx_s_aapcs
  fx_s_lp64d	db 'externus("libc", abi: lp64d) {', 10, '}', 10
  FX_S_LP64D_LEN = $ - fx_s_lp64d
  fx_s_other	db 'externus("libc", abi: quidlibet) {', 10, '}', 10
  FX_S_OTHER_LEN = $ - fx_s_other
  fx_sysv	db 'sysv_amd64'

fx_abi_tab:
	dq fx_s_sysv,  FX_S_SYSV_LEN,  AST_ABI_SYSV_AMD64, 0
	dq fx_s_aapcs, FX_S_AAPCS_LEN, AST_ABI_AAPCS64,    0
	dq fx_s_lp64d, FX_S_LP64D_LEN, AST_ABI_LP64D,      0
	dq fx_s_other, FX_S_OTHER_LEN, AST_ABI_UNKNOWN,    0
fx_abi_tab_end:
  fx_abi_n	dq (fx_abi_tab_end - fx_abi_tab) / 32
  ; A table that measured short would make the loop pass by not running.
  ; fasmg's NATIVE assemble-time `assert`, not `rassert`.
  assert (fx_abi_tab_end - fx_abi_tab) / 32 = 4

segment readable writeable
  fx_path:	db 'fixture.exsc'
  FX_PATH_LEN = $ - fx_path
  fx_arena:	rb sizeof.Arena
  fx_arena2:	rb sizeof.Arena
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
  fx_ast2:	rb sizeof.Ast
  fx_wr:	rb sizeof.DiagOut
  fx_buf:	rb 16384
