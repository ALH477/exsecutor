; tests/unit/chk_ty_examples.asm
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
; checker fixture -- THE THREE `examples/*.exsc` THROUGH THE WHOLE FRONT END
; AND THE WHOLE OF STAGE 2. `docs/design/checker.md` section 6's load-bearing
; case for `checker-types`: real source, the real lexer, the real §8.6 parse,
; the real Stage 1 tree, then `chk_run` -- passes 0, 1, 2, 3, 4 and the Stage 2
; verifier, in one call, on a program a person wrote.
;
; The three files are `file`'d in, so this fixture cannot drift from them: edit
; an example and this either still passes or tells you it stopped.
;
; ---------------------------------------------------------------------------
; WHY THE THREE ARE CONCATENATED. Spec §8.6 decision 5: *there is no import
; statement*. `initium.exsc` calls `imprime_gutenbergio` and `saluta`, which
; are declared in the other two files, so as three separate modules the third
; is `EXS-E0301` twice and always will be. §12 lets `SOURCE` repeat and
; `examples/README.md`'s publish gate "compiles the three files together";
; the driver reads one file per invocation today, so concatenating their bytes
; is what "together" means here. That is a REQUEST to `driver/`, recorded in
; the report, not a workaround this fixture is hiding.
;
; ---------------------------------------------------------------------------
; WHAT CHECK 3 RECORDS, AND WHY IT IS NOT A PASS.
;
; `imprime.exsc` as written declares `poscit sicut s` with `s: Scriptor`, and
; `Scriptor` is a CONCRETE STRUCT (docs/design/runtime.md 2.4 says so in as
; many words: "spec §8.6 decision 4 makes trait objects explicit `dyn
; Scriptor`, so under the grammar as it stands `s: Scriptor` IS a concrete
; type"). Spec §4.2's substitution replaces `sicut f` with "the row carried by
; the actual argument's TYPE", and a struct type carries none -- `AstType.b` is
; a row only on an `fn` and a `dyn`. So the ordinal survives substitution,
; pass 3 finds it unaccounted for in `initium`'s declared (empty) row, and the
; call is `EXS-E0421`.
;
; Checker.md section 2.4's class L calls that exact case -- "`sicut` ... on a
; parameter whose type has no row" -- and gives it `EXS-E0423`, and
; checker/rows/intern.inc's own header says a caller "raises the class-L code
; (`EXS-E0423`) against the argument". `__chk_row_e0421` raises `E0421`
; instead. Both are findings; both are in the report. This fixture asserts
; what the code ACTUALLY does, so that changing it is a deliberate act.
;
; Checks 4 and 5 are the pair that shows the capability chain working end to
; end: with `poscit ambitus` -- the row `imprime.exsc`'s own prose argues for
; ("the standard streams are `ambitus`") -- the program is clean, and deleting
; `sub ambitus = a;` from `initium` makes it `EXS-E0421` at the call. Neither
; is vacuous: one edit separates them.
;
; ---------------------------------------------------------------------------
; WHAT HAS TO EXIST FOR ANY OF THIS TO TYPE-CHECK: the primitive table
; (`textus`, `mensura`, `u8`), the prelude's `Scriptor`, `m.ambitus()`,
; `Scriptor.ad_exitum` and `s.scribe` (checker/types/prim.inc), spec §8.6
; decision 8's first-parameter receiver, and `AST_TY_CAP` for `m: Mundus` and
; for the `sub`. Every one of those is checker.md finding 14's or finding 6's,
; and this is the case that says so.
;
; Exit 0 = every check passed; 10+N = check N failed.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail0

	; ---- 1: examples/saluta.exsc alone -- clean ----
	lea	rdi, [fx_saluta]
	mov	rsi, FX_SALUTA_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail1

	; ---- 2: examples/imprime.exsc alone -- clean ----
	lea	rdi, [fx_imprime]
	mov	rsi, FX_IMPRIME_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail2

	; ---- 3: all three together -- exactly one `EXS-E0421`, and it is a
	;         FINDING against the example, not a pass. See the header.
	lea	rdi, [fx_ex3]
	mov	rsi, FX_EX3_LEN
	call	fx_run
	cmp	rax, 1
	jne	.fail3
	xor	rdi, rdi
	call	fx_code
	cmp	rax, 421
	jne	.fail3

	; ---- 4: the same three with `poscit ambitus` -- CLEAN, all six passes --
	lea	rdi, [fx_ex4]
	mov	rsi, FX_EX4_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail4
	; and the tree really is typed: the side tables are allocated and the
	; type table has grown well past the reserved error type
	lea	rdi, [fx_tree]
	call	ast_type_count
	cmp	rax, 8
	jb	.fail4
	mov	eax, [fx_tree + Ast.sides]
	test	eax, eax
	jz	.fail4

	; ---- 5: ... minus `sub ambitus = a;` -- exactly one `EXS-E0421` -------
	; (check 6 follows, and reads the tree check 5 leaves)
	lea	rdi, [fx_ex5]
	mov	rsi, FX_EX5_LEN
	call	fx_run
	cmp	rax, 1
	jne	.fail5
	xor	rdi, rdi
	call	fx_code
	cmp	rax, 421
	jne	.fail5

	; ---- 6: examples/initium.exsc ALONE -- exactly TWO `EXS-E0301`, and
	;         `Scriptor` is not one of them.
	;
	; This is the state of the world today and the assertion is written to
	; SAY so rather than to pass. `aedifica` reads one SOURCE (driver/), so
	; `imprime_gutenbergio` and `saluta` are declared in files this module
	; cannot see and spec §8.6 decision 5 has no import -- two misses that
	; multi-SOURCE (§12, requested of `driver/`) will close and nothing in
	; the checker can. `Scriptor` used to be a THIRD one; it resolves now,
	; through `chk_prelude_lookup` -> `chk_ty_prelude_lookup`
	; (checker/types/prim.inc). When SOURCE repeats, this check becomes
	; "zero diagnostics" and the change is one number.
	lea	rdi, [fx_initium]
	mov	rsi, FX_INITIUM_LEN
	call	fx_run
	cmp	rax, 2
	jne	.fail6
	xor	rdi, rdi
	call	fx_code
	cmp	rax, 301
	jne	.fail6
	mov	rdi, 1
	call	fx_code
	cmp	rax, 301
	jne	.fail6

	; ---- 7: and the prelude reference is really there, tagged and typed --
	; Some `Path` in that tree carries bit 31 in `d` -- the prelude
	; representation prim.inc's header states -- and its type is the
	; `Scriptor` struct. A scan, because the node index would pin this
	; fixture to the example's current line count for no gain.
	call	fx_prelude_path
	test	rax, rax
	jz	.fail7
	mov	esi, eax
	lea	rdi, [fx_tree]
	call	ast_type_at
	movzx	ecx, byte [rax + AstType.kind]
	cmp	ecx, AST_TY_STRUCT
	jne	.fail7

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

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; fx_run(srcptr, srclen) -> rax = the number of diagnostics `chk_run` appended.
; The whole front end over one source: §8.1/§8.4 lexing, the §8.6 parse, the
; §9.1 Stage 1 tree, then Stage 2. Nothing here is hand-built, so every
; interner id the checker meets is the lexer's -- which is what lets pass 2's
; primitive table (checker/types/prim.inc) see real spellings.
proc fx_run, fxsrc, fxlen
	uses	rbx, r12
	locals
	endl
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
	mov	rsi, [fxsrc]
	mov	rdx, [fxlen]
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

	; anything the lexer or the parser said is not this fixture's subject
	mov	rbx, [fx_diags + Vec.len]
	test	rbx, rbx
	jnz	.gate

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
	mov	rsi, [fxsrc]
	mov	rdx, [fxlen]
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	return
  .gate:
	; a lex or parse diagnostic means the fixture's source is wrong, not
	; that the checker said something -- answer an impossible count
	mov	rax, -1
	return
endp

; fx_code(i) -> rax = diagnostic `i`'s numeric EXS-E code, and RENDERS it, so a
; failing run shows what was actually raised instead of only a number.
proc fx_code, fxi
	uses	rbx
	locals
	endl
	mov	rax, [fx_diags + Vec.len]
	cmp	rax, [fxi]
	jbe	.none
	lea	rdi, [fx_diags]
	mov	rsi, [fxi]
	call	vec_get
	mov	rbx, rax
	mov	rdi, 1
	mov	rsi, rbx
	lea	rdx, [fx_buf]
	mov	rcx, 8192
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit
	mov	eax, [rbx + Diag.code_num]
	return
  .none:
	xor	eax, eax
	return
endp

; fx_prelude_path -> rax = the TYPE id of the first `Path` in `fx_tree` whose
; `d` carries `CHK_TY_PRELUDE` (bit 31), or 0 if there is none. The tag is
; checker/types/prim.inc's prelude representation, and this is the one place a
; fixture looks at it directly.
proc fx_prelude_path
	uses	rbx, r12, r13
	locals
		slot fxn, dq
	endl
	lea	rdi, [fx_tree]
	call	ast_node_count
	mov	[fxn], rax
	mov	r12, 1
  .node:
	cmp	r12, [fxn]
	ja	.none
	lea	rdi, [fx_tree]
	mov	rsi, r12
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, AST_PATH
	jne	.next
	mov	r13d, [rax + AstNode.d]
	mov	ecx, [rax + AstNode.ty]
	bt	r13, 31
	jnc	.next
	mov	eax, ecx
	return
  .next:
	inc	r12
	jmp	.node
  .none:
	xor	eax, eax
	return
endp

; fx_setup -- the arenas and the interner, once.
proc fx_setup
	locals
	endl
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
	return
  .bad:
	mov	eax, 1
	return
endp

segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

segment readable writeable
  fx_path	db 'examples.exsc'
  FX_PATH_LEN = $ - fx_path

  ; The examples themselves, byte for byte.
  fx_saluta:	file '../../examples/saluta.exsc'
  FX_SALUTA_LEN = $ - fx_saluta
  fx_imprime:	file '../../examples/imprime.exsc'
  FX_IMPRIME_LEN = $ - fx_imprime
  fx_initium:	file '../../examples/initium.exsc'
  FX_INITIUM_LEN = $ - fx_initium

  ; all three, in the publish gate's order
  fx_ex3:	file '../../examples/saluta.exsc'
		file '../../examples/imprime.exsc'
		file '../../examples/initium.exsc'
  FX_EX3_LEN = $ - fx_ex3

  ; the same, with `imprime_gutenbergio`'s row written as the atom its own
  ; prose names ("the standard streams are `ambitus`") instead of `sicut s`
  fx_ex4:	file '../../examples/saluta.exsc'
		db 'publica functio imprime_gutenbergio(s: Scriptor, t: textus) -> mensura poscit ambitus {', 10
		db '    redde s.scribe(t);', 10
		db '}', 10
		file '../../examples/initium.exsc'
  FX_EX4_LEN = $ - fx_ex4

  ; ... and `initium` with its `sub ambitus = a;` deleted: the one edit that
  ; turns check 4 into a diagnostic
  fx_ex5:	file '../../examples/saluta.exsc'
		db 'publica functio imprime_gutenbergio(s: Scriptor, t: textus) -> mensura poscit ambitus {', 10
		db '    redde s.scribe(t);', 10
		db '}', 10
		db 'publica functio initium(m: Mundus) -> u8 {', 10
		db '    firma a = m.ambitus();', 10
		db '    firma s = Scriptor.ad_exitum(a);', 10
		db '    imprime_gutenbergio(s, saluta());', 10
		db '    redde 0;', 10
		db '}', 10
  FX_EX5_LEN = $ - fx_ex5

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
