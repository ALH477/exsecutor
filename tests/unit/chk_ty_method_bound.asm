; tests/unit/chk_ty_method_bound.asm
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
; checker fixture -- A RECEIVER REACHED THROUGH AN INTERFACE IS NOT COMPARED BY
; IDENTITY. `checker/types/member.inc`'s `__chk_ty_call` `.args:`; spec §7.1,
; §8.6 decision 8; docs/design/checker.md sections 2.6 and 2.7.
;
; ONE CAUSE, TWO KINDS. `__chk_ty_method`'s `.bound:` arm falls through into
; `.iface:`, so a `T: Trait` receiver and a `dyn Trait` receiver both find the
; member on the INTERFACE declaration -- whose parameter 0 is spelled with the
; interface's own name, meaning "the implementing type" (§8.6 decision 8: there
; is no `self`, no `Self`, no receiver syntax; the first parameter is the
; receiver). So parameter 0's type is `Summable` while the receiver's is `T` or
; `dyn Summable`, and `__chk_ty_same` called that a mismatch: `EXS-E0303` at the
; receiver of EVERY generic method call, on code checker.md section 2.7 calls
; correctly resolved. §14 entry 24 -- §4.4's own worked example of the generic
; escape path -- carried it as a blocker.
;
; g01 AND g02 ARE THE FIX, and against the checker before this commit both are
; one `EXS-E0303` rather than zero diagnostics: a bound type parameter and a
; trait object, the same interface, the same call.
;
; b01 IS WHY THE FIX IS NOT "STOP CHECKING PARAMETER 0". The call's type has to
; be the member's RESULT, not the error type -- an error type is admitted
; everywhere (no cascade, typed-ast.md section 2.2), so "no diagnostic" alone
; would also be the answer of a checker that gave up on the call entirely.
; `-> u32` with `redde a.combine(b);` is `EXS-E0303` AT THE CALL, which is only
; reachable if the call typed as `f32`.
;
; b02 IS THE CONCRETE RECEIVER, STILL CHECKED BY IDENTITY. The skip is keyed on
; the receiver's own type KIND -- `AST_TY_PARAM` or `AST_TY_DYN` -- so an impl
; whose method writes the wrong type for parameter 0 (`adde(self: Aliud, …)` in
; an `interfacies Addenda in Punctum` block) is still `EXS-E0303` at the
; receiver. A fix that removed the comparison outright would pass g01 and g02
; and lose this.
;
; WHAT IS GIVEN UP, and it is a real loss rather than a rounding: a malformed
; INTERFACE member -- `combine(self: u32, …)` written inside the `interfacies`
; itself -- is no longer caught at the call site, because the call site is no
; longer where parameter 0's type is judged. It never should have been; the rule
; is a declaration-site one (parameter 0 of an `interfacies` member must be the
; interface's own name) and it is owed in `sig.inc`. docs/design/checker.md
; section 2.7 carries the named non-goal. No code is invented for it.
;
; Offsets are `exsc --diagnostica json`'s own `span.start` on these exact bytes,
; read off the real compiler and not counted by hand.
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 40		; dq src, len; dd count, code0, off0, code1, off1, pad

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

  fx_path	db 'chk_ty_method_bound.exsc'
  FX_PATH_LEN = $ - fx_path

  ; g01 -- §14 entry 24's call, with the ceiling violation removed so the only
  ; thing under test is the receiver: `a: T`, `T: Summable`, and the member
  ; found on the interface whose parameter 0 is spelled `Summable`.
  fx_g01:
	db 'interfacies Summable {', 10
	db '    functio combine(self: Summable, other: f32) -> f32', 10
	db '}', 10
	db 10
	db 'publica functio total<T: Summable>(a: T, b: f32) -> f32 {', 10
	db '    redde a.combine(b);', 10
	db '}', 10
  FX_G01_LEN = $ - fx_g01

  ; g02 -- the same member reached through a trait object. `.bound:` falls
  ; through into `.iface:`, so this is the same lookup and the same skip.
  fx_g02:
	db 'interfacies Summable {', 10
	db '    functio combine(self: Summable, other: f32) -> f32', 10
	db '}', 10
	db 10
	db 'publica functio per_dyn(a: dyn Summable, b: f32) -> f32 {', 10
	db '    redde a.combine(b);', 10
	db '}', 10
  FX_G02_LEN = $ - fx_g02

  ; b01 -- the call's TYPE is the member's result, not the error type: `-> u32`
  ; makes that visible as EXS-E0303 at the call itself
  fx_b01:
	db 'interfacies Summable {', 10
	db '    functio combine(self: Summable, other: f32) -> f32', 10
	db '}', 10
	db 10
	db 'publica functio total<T: Summable>(a: T, b: f32) -> u32 {', 10
	db '    redde a.combine(b);', 10
	db '}', 10
  FX_B01_LEN = $ - fx_b01

  ; b02 -- a CONCRETE receiver is still compared by identity: the impl method
  ; writes `Aliud` where `Punctum` belongs, and the call site still says so
  fx_b02:
	db 'structura Punctum { valor: f32 }', 10
	db 10
	db 'structura Aliud { valor: f32 }', 10
	db 10
	db 'interfacies Addenda {', 10
	db '    functio adde(self: Addenda, other: f32) -> f32', 10
	db '}', 10
	db 10
	db 'interfacies Addenda in Punctum {', 10
	db '    functio adde(self: Aliud, other: f32) -> f32 {', 10
	db '        redde other;', 10
	db '    }', 10
	db '}', 10
	db 10
	db 'publica functio summa(p: Punctum, b: f32) -> f32 {', 10
	db '    redde p.adde(b);', 10
	db '}', 10
  FX_B02_LEN = $ - fx_b02

  fx_tab:
	; a bound type parameter as the receiver: EXS-E0303 before this commit
	dq fx_g01, FX_G01_LEN
	dd 0, 0, 0, 0, 0, 0
	; a trait object as the receiver: the same lookup, the same answer
	dq fx_g02, FX_G02_LEN
	dd 0, 0, 0, 0, 0, 0
	; the call's type is the member's result -- EXS-E0303 AT THE CALL
	dq fx_b01, FX_B01_LEN
	dd 1, 303, 149, 0, 0, 0
	; a concrete receiver is still compared by identity -- at the RECEIVER
	dq fx_b02, FX_B02_LEN
	dd 1, 303, 317, 0, 0, 0
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
