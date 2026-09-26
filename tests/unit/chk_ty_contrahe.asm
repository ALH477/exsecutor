; tests/unit/chk_ty_contrahe.asm
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
; checker fixture -- a `contrahe` accumulator: what a contribution is typed
; against, and how long the accumulator lives. Spec §5.4, "Reduction shape is
; semantics", two sentences:
;
;   "Writing to it is the contribution: inside the body, `acc = e;`
;   contributes `e` under the operator `contrahe` declared, and is the only
;   statement that may name `acc`; any other read is `EXS-E0341`. After the
;   loop `acc` is an ordinary binding."
;
; WHY IT EXISTS. Every valid `contrahe` program was refused, by two separate
; defects, and nothing in the tree measured either (`docs/design/checker.md`
; finding 27; `docs/design/lowering.md` finding 20, which found them):
;
;   C1  the contribution was typed against the ACCUMULATOR'S OWN TYPE, which
;       is the `red` handle -- so `s = v[i];` was one `EXS-E0303` at `v[i]`,
;       in every program, always. What the right-hand side must match is F,
;       the reduction's element type, and F was recorded nowhere at all.
;   C2  the accumulator was bound in the frame the loop variable dies with,
;       so the read spec §5.4's "After the loop" sentence is about was
;       `EXS-E0301`. The result of a reduction was unreachable.
;
; WHERE F LIVES, pinned by the last two checks below and not only by the
; absence of diagnostics: in the `red` handle's own `AstType.b`, which is the
; row id on an `fn` and means nothing on a `red`. F is the FIRST
; contribution's right-hand type -- nothing annotates an accumulator -- and
; every later contribution is typed against it, so a disagreement is the
; ordinary `EXS-E0303` at its own right-hand side rather than a rule of its
; own. When the loop closes, the declaration's type BECOMES F: that is spec
; §5.4's "ordinary binding", and it is also what tells "inside its own
; iteration" from "after it" at the two places that must know -- `EXS-E0341`
; and whether a write is a contribution or an `EXS-E0306`.
;
; THE FIVE SHAPES the lowering fixture is written against are all here,
; accepted with ZERO diagnostics: `per` with `ordinata`, `quisque` with
; `arborea 4`, two accumulators in one head, two nested loops each with its
; own, and the read after the loop. The fourth is the one that a `wcon`-only
; reading of `EXS-E0341` gets wrong: the outer loop contributes the INNER
; loop's result, a read of an accumulator made while two `contrahe` loops
; are open.
;
; THE REJECTED ROWS PIN THE COUNT, THE CODE AND THE OFFSET, and every one of
; them is exactly ONE diagnostic -- spec §8.3, "one class each, never one per
; message". Two of the three cascades that used to follow an illegal read are
; suppressed on purpose: an `EXS-E0341` read answers the error type, and a
; contribution that did not settle records the error type as F.
;
; A `contrahe` WHOSE BODY NEVER CONTRIBUTES (row fx_a06) is ACCEPTED, and
; that is a decision, not an oversight. Neither spec §5.4 nor §8.5 says what
; such a loop means and §13 has no code that fits; inventing one here would
; be inventing a code (CLAUDE.md, "Error codes are permanent"). The
; accumulator keeps the handle, so a use of it after the loop is one
; `EXS-E0303` -- and the lowering still `rassert`s at the `ForHead`
; (`lower/stmt.inc`). Deciding it is a spec amendment.
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 41 = no accumulator declaration,
; 42 = its type after the loop is not `u32`, 43 = not exactly one settled
; `red` handle names it, 44 = that handle's F is not its type; 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 40		; dq src, len; dd count, code0, off0, code1, off1, pad
FX_ANY = -1		; "do not check this offset"

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99

	xor	r12, r12		; row index
  .row:
	cmp	r12, FX_NROWS
	jae	.rows_done
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
	cmp	r14, 2
	jb	.row_ok
	mov	edi, 1
	mov	esi, [r13 + 28]
	mov	edx, [r13 + 32]
	call	fx_diag_is
	test	eax, eax
	jz	.row_bad
  .row_ok:
	inc	r12
	jmp	.row
  .row_bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .rows_done:


	; ---- the accumulator is an ordinary binding of F afterwards ----------
	; Spec §5.4's "After the loop `acc` is an ordinary binding" read off the
	; tree, not inferred from silence: a checker that simply stopped raising
	; would leave the `red` handle on the declaration and pass every row
	; above while making the accumulator useless to everything downstream.
	lea	rdi, [fx_a01]
	mov	rsi, fx_a01_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail41
	call	fx_acc_decl
	test	rax, rax
	jz	.fail41
	mov	r12, rax			; the accumulator's declaration
	lea	rdi, [fx_tree]
	mov	rsi, r12
	call	ast_decl_at
	mov	r13d, [rax + AstDecl.ty]	; ... which must now be `u32`
	test	r13, r13
	jz	.fail42
	lea	rdi, [fx_tree]
	mov	rsi, r13
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_INT
	jne	.fail42
	cmp	byte [rax + AstType.sign], AST_SIGN_U
	jne	.fail42
	cmp	dword [rax + AstType.width], 32
	jne	.fail42

	; ---- and F is in the handle's `b` ------------------------------------
	; Exactly one SETTLED handle names this declaration. There are two `red`
	; entries in the table -- `__chk_ty_forhead` interns `b` = 0 before any
	; contribution has been seen -- and the settled one must carry F.
	xor	r14, r14			; how many settled handles
	mov	rbx, 1
  .ty:
	lea	rdi, [fx_tree]
	call	ast_type_count
	cmp	rbx, rax
	ja	.ty_done
	lea	rdi, [fx_tree]
	mov	rsi, rbx
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_RED
	jne	.ty_next
	cmp	dword [rax + AstType.a], r12d
	jne	.ty_next
	cmp	dword [rax + AstType.b], 0
	je	.ty_next
	inc	r14
	cmp	dword [rax + AstType.b], r13d
	jne	.fail44
  .ty_next:
	inc	rbx
	jmp	.ty
  .ty_done:
	cmp	r14, 1
	jne	.fail43

	xor	edi, edi
	call	sys_exit_group
  .fail41:
	mov	edi, 41
	call	sys_exit_group
  .fail42:
	mov	edi, 42
	call	sys_exit_group
  .fail43:
	mov	edi, 43
	call	sys_exit_group
  .fail44:
	mov	edi, 44
	call	sys_exit_group
  .fail99:
	mov	edi, 99
	call	sys_exit_group



include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; ---- harness ---------------------------------------------------------------
; Plain labels, as in tests/unit/chk_ty_discerne.asm, which this harness is
; copied from: a `proc` argument name is an unmangled global. Each helper
; pushes an odd number of registers, so `rsp` is 16-aligned at every call
; inside it.

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

; fx_diag_is(edi = i, esi = code, edx = span start or FX_ANY) -> eax = 1 if
; diagnostic `i` has that code at that offset.
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
	cmp	r13d, FX_ANY
	je	.yes
	cmp	[rax + Diag.span.start], r13d
	jne	.no
  .yes:
	mov	eax, 1
	jmp	.out
  .no:
	xor	eax, eax
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_acc_decl -> rax = the id of the first `AST_D_ACCUM` declaration in the
; last tree, 0 if there is none.
  fx_acc_decl:
	push	rbx
	push	r12
	push	r13
	mov	r13, 1
  .scan:
	lea	rdi, [fx_tree]
	call	ast_decl_count
	cmp	r13, rax
	ja	.none
	lea	rdi, [fx_tree]
	mov	rsi, r13
	call	ast_decl_at
	movzx	ecx, byte [rax + AstDecl.kind]
	cmp	ecx, AST_D_ACCUM
	je	.hit
	inc	r13
	jmp	.scan
  .hit:
	mov	rax, r13
	jmp	.out
  .none:
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

  fx_path	db 'chk_ty_contrahe.exsc'
  FX_PATH_LEN = $ - fx_path

  ; Every row shares one signature line, so an offset is the signature and
  ; its `\n` plus the body lines above the marked one. Computed, then
  ; confirmed against `exsc aedifica --diagnostica json` on the same source.
  ; `w` is there so that a wrong-typed contribution is available in every
  ; row without a second declaration changing the offsets.

  ; shape 1 -- `per`, `forma ordinata`, one accumulator, read after
  fx_a01:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        s = v[i];', 10
	db '    }', 10
	db '    redde s;', 10
	db '}', 10
  fx_a01_LEN = $ - fx_a01

  ; shape 2 -- `quisque`, `forma arborea 4`
  fx_a02:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    quisque i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma arborea 4', 10
	db '    {', 10
	db '        s = v[i];', 10
	db '    }', 10
	db '    redde s;', 10
	db '}', 10
  fx_a02_LEN = $ - fx_a02

  ; shape 3 -- two accumulators in one head, two handles, two F
  fx_a03:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe a: +', 10
	db '        contrahe b: *', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        a = v[i];', 10
	db '        b = v[i];', 10
	db '    }', 10
	db '    redde a +% b;', 10
	db '}', 10
  fx_a03_LEN = $ - fx_a03

  ; shape 4 -- nested, each its own accumulator; the outer contributes
  ; the INNER RESULT: a read made with `wcon` still 2
  fx_a04:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..2', 10
	db '        contrahe outer: +', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        per j in 0..8', 10
	db '            contrahe inner: +', 10
	db '            forma ordinata', 10
	db '        {', 10
	db '            inner = v[j];', 10
	db '        }', 10
	db '        outer = inner;', 10
	db '    }', 10
	db '    redde outer;', 10
	db '}', 10
  fx_a04_LEN = $ - fx_a04

  ; shape 5 -- after the loop `s` is an ordinary binding of F: read
  ; twice, in an operand position, against an ANNOTATION
  fx_a05:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        s = v[i];', 10
	db '    }', 10
	db '    firma t: u32 = s +% s;', 10
	db '    redde t;', 10
	db '}', 10
  fx_a05_LEN = $ - fx_a05

  ; a body that never contributes -- accepted, deliberately: see the header
  fx_a06:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        redde 0;', 10
	db '    }', 10
	db '    redde 1;', 10
	db '}', 10
  fx_a06_LEN = $ - fx_a06

  ; E0341 -- a read inside the body, ONE code
  fx_c01:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        s = s +% v[i];', 10
	db '    }', 10
	db '    redde s;', 10
	db '}', 10
  fx_c01_LEN = $ - fx_c01

  ; E0303 -- the one contribution fixes F = u8, so the mismatch is at the USE
  fx_c02:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        s = w[i];', 10
	db '    }', 10
	db '    redde s;', 10
	db '}', 10
  fx_c02_LEN = $ - fx_c02

  ; E0303 -- two contributions disagreeing, at the SECOND rhs
  fx_c03:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        s = v[i];', 10
	db '        s = w[i];', 10
	db '    }', 10
	db '    redde s;', 10
	db '}', 10
  fx_c03_LEN = $ - fx_c03

  ; E0301 -- a read BEFORE the loop: it does not exist there
  fx_c04:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    firma t: u32 = s;', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        s = v[i];', 10
	db '    }', 10
	db '    redde t;', 10
	db '}', 10
  fx_c04_LEN = $ - fx_c04

  ; E0343 -- `-` is not associative
  fx_c05:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: -', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        s = v[i];', 10
	db '    }', 10
	db '    redde s;', 10
	db '}', 10
  fx_c05_LEN = $ - fx_c05

  ; E0343 -- `arborea` with no width
  fx_c06:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma arborea', 10
	db '    {', 10
	db '        s = v[i];', 10
	db '    }', 10
	db '    redde s;', 10
	db '}', 10
  fx_c06_LEN = $ - fx_c06

  ; E0343 -- `ordinata` with a width
  fx_c07:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma ordinata 4', 10
	db '    {', 10
	db '        s = v[i];', 10
	db '    }', 10
	db '    redde s;', 10
	db '}', 10
  fx_c07_LEN = $ - fx_c07

  ; E0306 -- a write AFTER the loop: an ordinary IMMUTABLE binding
  fx_c08:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        s = v[i];', 10
	db '    }', 10
	db '    s = v[0];', 10
	db '    redde s;', 10
	db '}', 10
  fx_c08_LEN = $ - fx_c08

  ; E0342 -- `rumpe` under a `contrahe`, unchanged
  fx_c09:
	db 'publica functio f(v: acies<u32, 8>, w: acies<u8, 8>) -> u32 {', 10
	db '    per i in 0..8', 10
	db '        contrahe s: +', 10
	db '        forma ordinata', 10
	db '    {', 10
	db '        s = v[i];', 10
	db '        rumpe;', 10
	db '    }', 10
	db '    redde s;', 10
	db '}', 10
  fx_c09_LEN = $ - fx_c09

  fx_tab:
	; shape 1 -- `per`, `forma ordinata`, one accumulator, read after
	dq fx_a01, fx_a01_LEN
	dd 0, 0, 0, 0, 0, 0
	; shape 2 -- `quisque`, `forma arborea 4`
	dq fx_a02, fx_a02_LEN
	dd 0, 0, 0, 0, 0, 0
	; shape 3 -- two accumulators in one head, two handles, two F
	dq fx_a03, fx_a03_LEN
	dd 0, 0, 0, 0, 0, 0
	; shape 4 -- nested, each its own accumulator; the outer contributes
	; the INNER RESULT: a read made with `wcon` still 2
	dq fx_a04, fx_a04_LEN
	dd 0, 0, 0, 0, 0, 0
	; shape 5 -- after the loop `s` is an ordinary binding of F: read
	; twice, in an operand position, against an ANNOTATION
	dq fx_a05, fx_a05_LEN
	dd 0, 0, 0, 0, 0, 0
	; a body that never contributes -- accepted, deliberately: see the header
	dq fx_a06, fx_a06_LEN
	dd 0, 0, 0, 0, 0, 0
	; E0341 -- a read inside the body, ONE code
	dq fx_c01, fx_c01_LEN
	dd 1, 341, 143, 0, 0, 0
	; E0303 -- the one contribution fixes F = u8, so the mismatch is at the USE
	dq fx_c02, fx_c02_LEN
	dd 1, 303, 165, 0, 0, 0
	; E0303 -- two contributions disagreeing, at the SECOND rhs
	dq fx_c03, fx_c03_LEN
	dd 1, 303, 161, 0, 0, 0
	; E0301 -- a read BEFORE the loop: it does not exist there
	dq fx_c04, fx_c04_LEN
	dd 1, 301, 81, 0, 0, 0
	; E0343 -- `-` is not associative
	dq fx_c05, fx_c05_LEN
	dd 1, 343, 88, 0, 0, 0
	; E0343 -- `arborea` with no width
	dq fx_c06, fx_c06_LEN
	dd 1, 343, 66, 0, 0, 0
	; E0343 -- `ordinata` with a width
	dq fx_c07, fx_c07_LEN
	dd 1, 343, 66, 0, 0, 0
	; E0306 -- a write AFTER the loop: an ordinary IMMUTABLE binding
	dq fx_c08, fx_c08_LEN
	dd 1, 306, 159, 0, 0, 0
	; E0342 -- `rumpe` under a `contrahe`, unchanged
	dq fx_c09, fx_c09_LEN
	dd 1, 342, 157, 0, 0, 0
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
