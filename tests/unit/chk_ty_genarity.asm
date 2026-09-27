; tests/unit/chk_ty_genarity.asm
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 The Exsecutor authors.
;
; DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
;
; This code is free software; you can redistribute it and/or modify it under
; the terms of the GNU General Public License as published by the GNU
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
; checker fixture -- a PRIMITIVE's generic arguments are counted at both ends.
; `EXS-E0304` (wrong number of arguments), `checker/types/sig.inc`'s
; `__chk_ty_genarity`.
;
; WHAT THIS PINS DID NOT HOLD BEFORE THE COMMIT THAT ADDED THIS FILE, and the
; whole table is the measurement. `__chk_ty_genarg` compared the written count
; against the INDEX it was asked for with `jbe` -- a lower bound -- so every
; one-argument spelling accepted any number above one, `.simple:` read the
; clause not at all, and a MISSING clause took the `.none:` arm, which returns
; the error type without raising. Measured on the tree before the fix, every
; row below exited 0 except `acies<f32>` and bare `acies`:
;
;   spelling                     before      after
;   refero<u32, u32>             accepted    EXS-E0304
;   refero_communis<u32, u32>    accepted    EXS-E0304
;   eventus<mensura, u32>        accepted    EXS-E0304
;   eventus                      accepted    EXS-E0304
;   acies<f32>                   EXS-E0304   EXS-E0304
;   acies<f32, 4, 4>             accepted    EXS-E0304
;   acies                        EXS-E0304   EXS-E0304
;   textus<u8>                   accepted    EXS-E0304
;   unit<u32>                    accepted    EXS-E0304
;   mensura<u8>                  accepted    EXS-E0304
;
; Bare `eventus` is the row that matters most: spec §5.1's own code block
; spells the type that way, and until this commit the checker took it silently.
;
; EXACTLY ONE DIAGNOSTIC PER ROW, and that is a rule and not an accident.
; `__chk_ty_raise` does not deduplicate (checker/types/prim.inc), and
; `__chk_ty_genarg`/`__chk_ty_genlit` raise `304` themselves when asked for an
; index the count does not cover -- so `acies<f32>` would report twice if
; `__chk_ty_prim`'s arms did not guard on the count `__chk_ty_genarity`
; returns. Each row asserts the COUNT first, so a second diagnostic fails the
; row even though its code is the one expected.
;
; THE OFFSET IS PART OF THE CLAIM. `EXS-E0304` lands on the `GenericArgs` node
; when the clause is written -- offset 26/27/28, the `<` -- and on the `Seg`
; when it is not: offset 21, the spelling itself. A missing clause has no node
; of its own, and the segment is the token a reader has to look at. Every
; offset below was read off `exsc --diagnostica json`'s own `span.start` on
; these exact sources, not counted by hand.
;
; A BARE `refero` IS NOT A ROW HERE, and the reason is the lexer, not the
; checker: `refero` is a KEYWORD (lexer/keywords.inc, "value flow"), so
; `a: refero` is `EXS-E0201` at the parse and the checker never sees it. A
; front-end diagnostic makes `fx_run` answer -1 rather than testing this pass,
; exactly as chk_ty_bitops.asm's bare bitwise chain is excluded. `eventus` and
; `acies` are ordinary identifiers and do reach the checker with no clause,
; which is why the none-at-all rows are theirs.
;
; `mensura`, `f32` and `f64` TAKE NO ARGUMENTS EITHER, and the `mensura<u8>`
; row is there because the fix covers them: those three arms read the clause
; no more than `.simple:` did. What is still NOT counted here is a generic
; USER type's arity -- `member.inc`'s instantiation loop keeps its own `304`,
; one argument at a time, and unifying the two is a separate change
; (`__chk_ty_genarity`'s header says so).
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 32		; dq src, len; dd count, code, off, pad

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99

	xor	r12, r12		; row index
  .row:
	cmp	r12, FX_NROWS
	jae	.done
	mov	rax, r12
	imul	rax, FX_ROW
	lea	r13, [fx_tab]
	add	r13, rax
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	call	fx_run
	mov	r14, rax
	; render whatever the row produced, so a failure shows it
	xor	ebx, ebx
  .show:
	cmp	rbx, r14
	jae	.shown
	mov	rdi, rbx
	call	fx_code
	inc	rbx
	jmp	.show
  .shown:
	mov	ecx, [r13 + 16]
	cmp	r14, rcx
	jne	.row_bad
	test	r14, r14
	jz	.row_ok
	xor	edi, edi
	mov	esi, [r13 + 20]
	mov	edx, [r13 + 24]
	call	fx_diag_is
	test	eax, eax
	jz	.row_bad
  .row_ok:
	inc	r12
	jmp	.row
  .row_bad:
	lea	rdi, [r12 + 11]
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
; The same plain-label harness tests/unit/chk_ty_bitops.asm uses, and for its
; reason: a `proc` argument name is an unmangled global, so two fixtures in one
; build would collide. Each helper pushes an odd number of registers, so `rsp`
; is 16-aligned at every call inside it.

; fx_run(rdi = source, rsi = length) -> rax = the diagnostics `chk_run`
; appended; -1 if the lexer or the parser said anything (the row's source is
; then wrong, and no count can match it).
  fx_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
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

; fx_diag_is(edi = i, esi = code, edx = span start) -> eax = 1 if diagnostic
; `i` has that code at that offset.
  fx_diag_is:
	push	rbx
	push	r12
	push	r13
	mov	r12d, esi
	mov	r13d, edx
	mov	esi, edi
	lea	rdi, [fx_diags]
	call	vec_get
	cmp	[rax + Diag.code_num], r12d
	jne	.no
	cmp	[rax + Diag.span.start], r13d
	jne	.no
	mov	eax, 1
	jmp	.out
  .no:
	xor	eax, eax
  .out:
	pop	r13
	pop	r12
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

  fx_path	db 'chk_ty_genarity.exsc'
  FX_PATH_LEN = $ - fx_path

  ; ONE SHAPE, ONE VARYING TOKEN: the type spelling in parameter position.
  ; `publica functio f(a: ` is 21 bytes, so a spelling with no clause is
  ; diagnosed at offset 21 and one with a clause at 21 + the spelling's
  ; length.
  macro fx_src name, ty
	name: db 'publica functio f(a: ', ty, ') -> u32 {', 10
	      db '    redde 0;', 10, '}', 10
	name#_LEN = $ - name
  end macro

  fx_src fx_r01, 'refero<u32, u32>'
  fx_src fx_r02, 'refero_communis<u32, u32>'
  fx_src fx_r03, 'eventus<mensura, u32>'
  fx_src fx_r04, 'eventus'
  fx_src fx_r05, 'acies<f32>'
  fx_src fx_r06, 'acies<f32, 4, 4>'
  fx_src fx_r07, 'acies'
  fx_src fx_r08, 'textus<u8>'
  fx_src fx_r09, 'unit<u32>'
  fx_src fx_r10, 'mensura<u8>'

  fx_src fx_a01, 'refero<u32>'
  fx_src fx_a02, 'refero_communis<u32>'
  fx_src fx_a03, 'eventus<mensura>'
  fx_src fx_a04, 'acies<f32, 4>'
  fx_src fx_a05, 'textus'
  fx_src fx_a06, 'mensura'

  fx_tab:
	; ---- one argument too many, on every one-parameter spelling --------
	; `refero` reads argument 0 and stopped; the second was never looked at
	dq fx_r01, fx_r01_LEN
	dd 1, 304, 27, 0
	dq fx_r02, fx_r02_LEN
	dd 1, 304, 36, 0
	; §5.1's `eventus`, the spelling D4 proposes two parameters for
	dq fx_r03, fx_r03_LEN
	dd 1, 304, 28, 0
	; ---- no clause at all: the diagnostic lands on the `Seg` -----------
	; bare `eventus` -- spec §5.1's own spelling, silently accepted before
	dq fx_r04, fx_r04_LEN
	dd 1, 304, 21, 0
	; ---- `acies`, the one spelling that takes TWO (§5.4) ---------------
	; one too few: the lane count is missing, and `__chk_ty_genlit` must
	; not be asked for it as well
	dq fx_r05, fx_r05_LEN
	dd 1, 304, 26, 0
	; one too many
	dq fx_r06, fx_r06_LEN
	dd 1, 304, 26, 0
	; none at all
	dq fx_r07, fx_r07_LEN
	dd 1, 304, 21, 0
	; ---- the zero-argument spellings ----------------------------------
	; a §5.1 view takes no parameter; `.simple:` never read the clause
	dq fx_r08, fx_r08_LEN
	dd 1, 304, 27, 0
	dq fx_r09, fx_r09_LEN
	dd 1, 304, 25, 0
	; `mensura` has its own arm, and it was uncounted too (§9.5)
	dq fx_r10, fx_r10_LEN
	dd 1, 304, 28, 0
	; ---- the accepted twins: the right count, silently -----------------
	dq fx_a01, fx_a01_LEN
	dd 0, 0, 0, 0
	dq fx_a02, fx_a02_LEN
	dd 0, 0, 0, 0
	dq fx_a03, fx_a03_LEN
	dd 0, 0, 0, 0
	dq fx_a04, fx_a04_LEN
	dd 0, 0, 0, 0
	dq fx_a05, fx_a05_LEN
	dd 0, 0, 0, 0
	dq fx_a06, fx_a06_LEN
	dd 0, 0, 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW
  ; a row of the wrong width would shift every row after it
  assert ($ - fx_tab) mod FX_ROW = 0

segment readable writeable
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
