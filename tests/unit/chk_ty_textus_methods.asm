; tests/unit/chk_ty_textus_methods.asm
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
; checker fixture -- spec §5.1's TEXT OPERATIONS resolve, and the RECEIVER'S
; KIND is what decides. `checker/types/prim.inc` prelude rows 12..15,
; `checker/types/member.inc`'s `__chk_ty_pre_member` `.views:` arm.
;
; WHY THERE WAS NOTHING TO RESOLVE. `textus` `octeti` `scalares` `grapha` are
; `AstType` KINDS with no fields (ast/kinds.inc), so they have no `AstDecl` and
; no `Impl`. `__chk_ty_method` resolves a method by scanning impls whose target
; is the receiver's type id; for these four there is nothing to scan, so every
; §5.1 text operation was `EXS-E0305`, "operation not defined on the type", and
; §14 entries 1 and 12 both carried that as a blocker. docs/design/checker.md
; section 2.6 said these were "resolved as every method is -- through impls";
; that could never have worked, and the lookup that does is keyed on the KIND.
;
; g01 IS §14 ENTRY 12'S OWN EXPRESSION: `t.octeti().numerus()`, spec §5.1's
; first code block, clean. b01 IS WHAT MAKES g01 MEAN SOMETHING: the same
; expression declared `-> u32` is `EXS-E0303` AT THE CALL, which is only
; reachable if the chain typed as `mensura`. An error type is admitted
; everywhere (no cascade, typed-ast.md section 2.2), so "no diagnostic" alone
; would also be the answer of a checker that gave the chain up.
;
; `mensura` AND NOT `u64`: §9.5 forbids defaulting to the build platform and
; §5.2 makes `mensura` the target's own width, so a count of elements in a
; host-side view is a `mensura` by construction. b01 is what pins that: against
; a row that answered `u64`, `-> u32` would still be `EXS-E0303` -- but g01's
; `-> mensura` would be `EXS-E0303` too, and g01 is clean.
;
; g03 IS THE CAPABILITY, PASSED AS A VALUE. §5.1's own code block writes
; `"I".plica_sermone(sermo)`, and §14 entry 1's whole subject is that a
; function with no `sermo` in scope has no value to pass -- which is what makes
; the CVE-2025-49003 class a signature error. So `sermo` is parameter 1, not a
; row, and g03 declares `poscit sermo` to have the value. b02 is its arity twin:
; `t.plica_sermone()` is `EXS-E0304`, one argument short, at the call.
;
; b03 AND b05 ARE THE KIND DECIDING, in both directions: `numerus` on a
; `textus` is not a member (§5.1 writes it on the VIEWS), and `octeti` on an
; `octeti` is not one either. A lookup that answered on the NAME alone would
; accept both. b04 is the same test by a different route: `scribe_octeto` is a
; real prelude row -- the `Scriptor` interface's -- and it is not a member of
; `octeti`, which is the rule this file's older rows already hold.
;
; b06 IS A NAMED NARROWING AND NOT A BUG. §5.1 writes `numerus` on all three
; views (`t.scalares().numerus()`, `t.grapha().numerus()`), and
; `t.scalares()` is still `EXS-E0305`: one prelude row is one `fn` type, a
; concrete receiver is compared by identity at the call site, so a single
; `numerus` row cannot serve three receiver types. Three rows per operation, or
; a receiver-polymorphic prelude form, is the fix; neither shipped, and this
; row is here so the omission is measured rather than assumed. It is also why
; §14 entry 2 stays deferred.
;
; NOTHING HERE CAN BE LOWERED. All four methods are refused by name at
; `lower/expr.inc`'s `__lwr_call_member` -- a `rassert`, not a diagnostic,
; because no registered code means "unimplemented" and §8.3 makes codes
; permanent. This fixture never lowers, and no `tests/programs/` directory may
; call these four.
;
; Offsets are `exsc --diagnostica json`'s own `span.start` on these exact
; bytes, read off the real compiler and not counted by hand.
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

  fx_path	db 'chk_ty_textus_methods.exsc'
  FX_PATH_LEN = $ - fx_path

  ; g01 -- spec §5.1's own first line, and §14 entry 12's own expression
  fx_g01:
	db 'publica functio longitudo(t: textus) -> mensura {', 10
	db '    redde t.octeti().numerus();', 10
	db '}', 10
  FX_G01_LEN = $ - fx_g01

  ; g02 -- `plica_unicode` answers `textus`: total, locale-free, empty row
  fx_g02:
	db 'publica functio plica(t: textus) -> textus {', 10
	db '    redde t.plica_unicode();', 10
	db '}', 10
  FX_G02_LEN = $ - fx_g02

  ; g03 -- `plica_sermone(sermo)`: the capability is the ARGUMENT (§5.1, §14
  ; entry 1), and `poscit sermo` is what gives this function the value to pass
  fx_g03:
	db 'publica functio plica_loc(t: textus) -> textus poscit sermo {', 10
	db '    redde t.plica_sermone(sermo);', 10
	db '}', 10
  FX_G03_LEN = $ - fx_g03

  ; b01 -- the chain's TYPE is `mensura`: `-> u32` is EXS-E0303 at the call
  fx_b01:
	db 'publica functio longitudo(t: textus) -> u32 {', 10
	db '    redde t.octeti().numerus();', 10
	db '}', 10
  FX_B01_LEN = $ - fx_b01

  ; b02 -- one argument short: EXS-E0304, the receiver counts as argument 0
  fx_b02:
	db 'publica functio plica_loc(t: textus) -> textus poscit sermo {', 10
	db '    redde t.plica_sermone();', 10
	db '}', 10
  FX_B02_LEN = $ - fx_b02

  ; b03 -- `numerus` is a method of the VIEWS, not of `textus`
  fx_b03:
	db 'publica functio quot(t: textus) -> mensura {', 10
	db '    redde t.numerus();', 10
	db '}', 10
  FX_B03_LEN = $ - fx_b03

  ; b04 -- `scribe_octeto` is a real prelude row, and not a member of `octeti`
  fx_b04:
	db 'publica functio scribe(b: octeti) -> mensura {', 10
	db '    redde b.scribe_octeto();', 10
	db '}', 10
  FX_B04_LEN = $ - fx_b04

  ; b05 -- and `octeti` is a method of `textus`, not of `octeti`
  fx_b05:
	db 'publica functio ipse(b: octeti) -> octeti {', 10
	db '    redde b.octeti();', 10
	db '}', 10
  FX_B05_LEN = $ - fx_b05

  ; b06 -- the named narrowing: `scalares` and `grapha` still have no methods
  fx_b06:
	db 'publica functio quot(t: textus) -> mensura {', 10
	db '    redde t.scalares().numerus();', 10
	db '}', 10
  FX_B06_LEN = $ - fx_b06

  fx_tab:
	; §5.1's own expression, and §14 entry 12's: clean
	dq fx_g01, FX_G01_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_g02, FX_G02_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_g03, FX_G03_LEN
	dd 0, 0, 0, 0, 0, 0
	; the chain's type is `mensura`, visible as EXS-E0303 at the call
	dq fx_b01, FX_B01_LEN
	dd 1, 303, 56, 0, 0, 0
	; wrong arity on a prelude method: EXS-E0304
	dq fx_b02, FX_B02_LEN
	dd 1, 304, 72, 0, 0, 0
	; the receiver's KIND decides -- `numerus` is the views', not `textus`'s
	dq fx_b03, FX_B03_LEN
	dd 1, 305, 55, 0, 0, 0
	; a real prelude row that is not a member of THIS receiver
	dq fx_b04, FX_B04_LEN
	dd 1, 305, 57, 0, 0, 0
	; and the same in the other direction
	dq fx_b05, FX_B05_LEN
	dd 1, 305, 54, 0, 0, 0
	; the named narrowing: no `scalares()`, no `grapha()`, `[OPEN]`
	dq fx_b06, FX_B06_LEN
	dd 1, 305, 55, 0, 0, 0
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
