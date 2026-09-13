; tests/unit/chk_ty_param_write.asm
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
; checker fixture -- a write through a PARAMETER is `EXS-E0306` (ADR 0016
; decision 2; `docs/design/mutable-borrow.md` M1). This one closes a defect,
; not a gap, and the defect is worth stating precisely.
;
; WHAT WAS WRONG. `__chk_ty_rootmut` walked a place to its root binding and
; refused a write when that binding was `firma`. A PARAMETER root fell through
; its `AST_D_BINDING` test and was answered "writable". So:
;
;	publica functio imple(v: acies<u8, 4>) -> u8 { v[0] = 90; redde 0; }
;	firma b: acies<u8, 4> = [65; 4];	// 'A'
;	firma i = imple(b);
;	s.scribe_octeto(b[0]);			// printed 'Z'
;
; compiled, ran, and **mutated a `firma` binding** -- measured at `e8de1aa`,
; where it printed the 90. The direct write `b[0] = 90` on that same binding
; was already `EXS-E0306`, so immutability was enforced on the syntax of an
; assignment and not on the storage: any function taking an aggregate could
; launder a write to a `firma` binding through a call. The routine's own
; header had an `[OPEN]` describing the mechanism ("a PARAMETER root is still
; writable here") but nothing recorded that consequence, and no fixture
; exercised it in either direction. This is that fixture.
;
; WHICH RULE IS BEING ENFORCED, since six places in this tree get it wrong:
; `docs/design/ssa-ir.md` section 2.9's -- an aggregate parameter is a `ptr`
; to caller-owned storage the callee reads and never writes. NOT spec
; section 6.3 decision 3, which is one sentence about retains and says
; nothing about writing (ADR 0016).
;
; ROWS. Each compiles real source through the whole front end and checks the
; diagnostic count, and for a refusal the code of the first diagnostic:
;
;	1  write an element through a parameter		EXS-E0306
;	2  write a struct field through a parameter	EXS-E0306
;	3  write an element of a `firma` local		EXS-E0306  (unchanged)
;	4  write an element of a `mutabilis` local	accepted   (unchanged)
;	5  write a field of a `mutabilis` local		accepted   (unchanged)
;	6  READ an element through a parameter		accepted   (unchanged)
;	7  write through `&mutabilis acies`		accepted   -- the point
;	8  write through `&acies`			EXS-E0306
;	9  READ through `&acies`			accepted
;	10 write a field through `&mutabilis P`		accepted
;	11 write a field through `&P`			EXS-E0306
;
; Rows 8 and 11 closed a SECOND hole, found while landing row 7. This
; routine's "a dereference stops the walk and answers writable" is right for
; `*T` -- `firma p: *u8` is an immutable POINTER to a mutable place -- and was
; wrong for `&T`, so a write through an IMMUTABLE borrow was accepted and
; `&mutabilis` would have been decoration rather than a permission.
;
; Rows 3-6 are here because the interesting risk in this change is not that
; the refusal fails to fire, it is that it fires too widely: a routine that
; refused every element write would also pass rows 1 and 2. Row 6 in
; particular pins that a parameter is still READABLE, which is the whole of
; what an aggregate parameter is for.
;
; `&mutabilis T` (ADR 0016 decision 1) is the marked exception, and rows 7-11
; are it. What is NOT here is the CALL SITE: an argument of type `T` does not
; yet coerce to a `&mutabilis T` parameter, so no program can call one of
; these functions. That is M3, with `EXS-E0310`'s aliasing rule.
;
; Exit 0 = every row held; 10+N = row N's diagnostic count; 40+N = row N's
; code; 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 24		; dq src, srclen; dd count, code

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
	mov	ecx, [r13 + 16]		; expected diagnostic count
	cmp	r14, rcx
	jne	.bad_count
	test	r14, r14
	jz	.next
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	ecx, [r13 + 20]		; expected code of diagnostic 0
	cmp	[rax + Diag.code_num], ecx
	jne	.bad_code
  .next:
	inc	r12
	jmp	.row
  .bad_count:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .bad_code:
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
; chk_ty_mensura32.asm's shape, with the per-row target width dropped (every
; row here is 64-bit) and a code assertion added.

; fx_run(rdi = source, rsi = length) -> rax = the diagnostics `chk_run`
; appended, -1 if the front end said anything.
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
	mov	rcx, 512
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
	mov	rcx, 1024
	call	vec_init
	lea	rdi, [fx_work]
	lea	rsi, [fx_arena]
	mov	rdx, 4
	mov	rcx, 512
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
	mov	rsi, 64			; --hospes x86_64-linux (spec section 9.5)
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

  fx_path	db 'chk_ty_param_write.exsc'
  FX_PATH_LEN = $ - fx_path

  ; One `db` per source, in the shape chk_ty_hexlit.asm uses: fasmg's
  ; variadic macros are spelled differently from fasm's and a fixture is not
  ; the place to explore the difference (CLAUDE.md: the macro dialect is
  ; frozen).
  macro fx_src name, text
	name: db text
	name#_LEN = $ - name
  end macro

  ; 1 -- an element write through a parameter
  fx_src fx_elem, <'publica functio imple(v: acies<u8, 4>) -> u8 {', 10, \
	'    v[0] = 90;', 10, '    redde 0;', 10, '}', 10>

  ; 2 -- a struct field write through a parameter
  fx_src fx_field, <'publica structura Par {', 10, '    a: u8', 10, '}', 10, \
	'publica functio imple(p: Par) -> u8 {', 10, '    p.a = 77;', 10, \
	'    redde 0;', 10, '}', 10>

  ; 3 -- an element of a `firma` local: already refused, must stay refused
  fx_src fx_firma, <'publica functio f() -> u8 {', 10, \
	'    firma b: acies<u8, 4> = [0; 4];', 10, '    b[0] = 1;', 10, \
	'    redde 0;', 10, '}', 10>

  ; 4 -- an element of a `mutabilis` local: must stay accepted
  fx_src fx_mut, <'publica functio f() -> u8 {', 10, \
	'    mutabilis b: acies<u8, 4> = [0; 4];', 10, '    b[0] = 1;', 10, \
	'    redde 0;', 10, '}', 10>

  ; 5 -- a field of a `mutabilis` local: must stay accepted
  fx_src fx_mutfield, <'publica structura Par {', 10, '    a: u8', 10, '}', 10, \
	'publica functio f() -> u8 {', 10, '    mutabilis p = Par { a: 1 };', 10, \
	'    p.a = 2;', 10, '    redde 0;', 10, '}', 10>

  ; 6 -- a READ through a parameter: the whole point of one, must stay accepted
  fx_src fx_read, <'publica functio lege(v: acies<u8, 4>) -> u8 {', 10, \
	'    redde v[0];', 10, '}', 10>

  ; 7 -- `&mutabilis`: a write through a MUTABLE borrow is the point
  fx_src fx_mbw, <'publica functio f(v: &mutabilis acies<u8, 4>) -> u8 {', 10, \
	'    (*v)[0] = 90;', 10, '    redde 0;', 10, '}', 10>

  ; 8 -- `&`: a write through an IMMUTABLE borrow is refused. This one closed
  ;      a second hole: `__chk_ty_rootmut`'s "a dereference stops the walk and
  ;      answers writable" is right for `*T` -- `firma p: *u8` is an immutable
  ;      POINTER to a mutable place -- and was wrong for `&T`, so this was
  ;      ACCEPTED and `&mutabilis` was decoration rather than a permission.
  fx_src fx_ibw, <'publica functio f(v: &acies<u8, 4>) -> u8 {', 10, \
	'    (*v)[0] = 90;', 10, '    redde 0;', 10, '}', 10>

  ; 9 -- reading through an immutable borrow stays accepted, which is what one
  ;      is for
  fx_src fx_ibr, <'publica functio f(v: &acies<u8, 4>) -> u8 {', 10, \
	'    redde (*v)[0];', 10, '}', 10>

  ; 10 -- the same pair over a struct field, since `.f` and `[i]` reach
  ;       `__chk_ty_rootmut` by different arms
  fx_src fx_mbf, <'publica structura P {', 10, '    a: u8', 10, '}', 10, \
	'publica functio f(p: &mutabilis P) -> u8 {', 10, '    (*p).a = 7;', 10, \
	'    redde 0;', 10, '}', 10>

  ; 11 -- and its immutable twin
  fx_src fx_ibf, <'publica structura P {', 10, '    a: u8', 10, '}', 10, \
	'publica functio f(p: &P) -> u8 {', 10, '    (*p).a = 7;', 10, \
	'    redde 0;', 10, '}', 10>

  ; dq source, its length; dd the expected diagnostic count and the expected
  ; code of diagnostic 0 (read only when the count is nonzero)
  macro fx_row src, count, code
	dq src, src#_LEN
	dd count, code
  end macro

  fx_tab:
	fx_row fx_elem,     1, 306
	fx_row fx_field,    1, 306
	fx_row fx_firma,    1, 306
	fx_row fx_mut,      0, 0
	fx_row fx_mutfield, 0, 0
	fx_row fx_read,     0, 0
	fx_row fx_mbw,      0, 0
	fx_row fx_ibw,      1, 306
	fx_row fx_ibr,      0, 0
	fx_row fx_mbf,      0, 0
	fx_row fx_ibf,      1, 306
  FX_NROWS = ($ - fx_tab) / FX_ROW

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
