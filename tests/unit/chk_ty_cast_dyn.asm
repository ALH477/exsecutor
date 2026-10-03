; tests/unit/chk_ty_cast_dyn.asm
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
; checker fixture -- `e sicut dyn I` IS NOT AN AGGREGATE CAST.
; `checker/types/types.inc`'s `.cast:`; spec §4.4, §5.2, §8.6 decision 4.
;
; THE BLOCK'S OWN COMMENT SAID THIS AND THE CODE DID NOT DO IT. `.cast:`
; already stated that "a `sicut dyn I poscit {P}` cast's bound is pass 3's
; `EXS-E0510`" -- but the dispatch beneath it read the SOURCE's kind first, so
; a struct source sent EVERY `dyn` cast into `__chk_ty_castagg`, which knows
; exactly one aggregate pair (spec §5.2's `@transitus` struct and
; `acies<u8, sizeof>`) and refused everything else. The result was `EXS-E0305`
; at the very span `EXS-E0510` correctly fires at: two diagnostics for one
; construct, and the wrong one first. Measured on §14 entry 12 before and
; after: `{EXS-E0305 x2, EXS-E0510 x2}` -> `{EXS-E0305 x1, EXS-E0510 x2}`.
;
; ROW b01 IS THE WHOLE CLAIM: the code SET is exactly `{EXS-E0510}`. It appears
; twice -- once at the implementation head (§4.4's ceiling check) and once at
; the cast -- and both are the same verdict on the same escape, so the row pins
; two occurrences of one code rather than pretending there is one. Against the
; checker before this commit the row's COUNT is three, which is what makes it
; non-vacuous.
;
; THE ACCEPTED TWIN (g01) IS THE OTHER HALF. `Simplex` has no
; capability-typed field, so its mark is empty, the bare `dyn`'s bound is the
; empty row, and the cast is silent -- a checker that simply stopped
; diagnosing `dyn` casts would pass b01's code test and fail nothing, so the
; fixture needs a row that is clean for the right reason.
;
; SPEC §5.2's ONE ADMITTED PAIR STILL WORKS, BOTH WAYS (g02, g03): a
; `@transitus` struct to `acies<u8, sizeof>` and back. `__chk_ty_castagg` is
; not weakened, only bypassed for `dyn`, and these two rows are what says so.
;
; AND EVERY OTHER AGGREGATE `sicut` IS STILL `EXS-E0305` (b02, b03). b02 is
; struct-to-struct, the plain case. b03 is the one worth writing down: the
; `dyn` test is on the TARGET only, so `d sicut acies<u8, 2>` with
; `d: dyn Scriptura` is still refused. Casting a trait object to something
; else is not §4.4's construction rule, has no `EXS-E0510` to defer to, and
; `dyn` -> `acies<u8, sizeof>` is exactly the laundering §5.2's one admitted
; pair must not be stretched to cover. Exempting the source as well as the
; target would have opened it.
;
; Offsets are `exsc --diagnostica json`'s own `span.start` on these exact
; bytes, read off the real compiler and not counted by hand; each row's source
; below is byte-identical to the file that was measured.
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

  fx_path	db 'chk_ty_cast_dyn.exsc'
  FX_PATH_LEN = $ - fx_path

  ; b01 -- a capability-bearing implementation cast to a BARE `dyn`, §14
  ; entry 12's shape with the `textus` method call removed (that call is a
  ; separate gap and would add a second code). `{EXS-E0510}` and nothing else.
  fx_b01:
	db 'interfacies Scriptura {', 10
	db '    functio pone(s: Scriptura, n: mensura) -> mensura', 10
	db '}', 10
	db 10
	db 'structura ScriptorRetis { sock: rete }', 10
	db 10
	db 'interfacies Scriptura in ScriptorRetis {', 10
	db '    functio pone(s: ScriptorRetis, n: mensura) -> mensura {', 10
	db '        redde n;', 10
	db '    }', 10
	db '}', 10
	db 10
	db 'publica functio erade(s: ScriptorRetis) -> dyn Scriptura {', 10
	db '    redde s sicut dyn Scriptura;', 10
	db '}', 10
  FX_B01_LEN = $ - fx_b01

  ; g01 -- the same cast with a mark that does NOT exceed the bound: no
  ; capability-typed field, empty mark, empty bound, silent.
  fx_g01:
	db 'interfacies Scriptura {', 10
	db '    functio pone(s: Scriptura, n: mensura) -> mensura', 10
	db '}', 10
	db 10
	db 'structura Simplex { valor: mensura }', 10
	db 10
	db 'interfacies Scriptura in Simplex {', 10
	db '    functio pone(s: Simplex, n: mensura) -> mensura {', 10
	db '        redde n;', 10
	db '    }', 10
	db '}', 10
	db 10
	db 'publica functio erade(s: Simplex) -> dyn Scriptura {', 10
	db '    redde s sicut dyn Scriptura;', 10
	db '}', 10
  FX_G01_LEN = $ - fx_g01

  ; g02 -- spec §5.2's one admitted aggregate pair, struct to byte view
  fx_g02:
	db '@transitus', 10
	db 'structura Caput {', 10
	db '    signum: u8', 10
	db '    genus:  u8', 10
	db '}', 10
	db 10
	db 'publica functio ad_octetos(c: Caput) -> u16 {', 10
	db '    firma b = c sicut acies<u8, 2>;', 10
	db '    redde b[0] sicut u16;', 10
	db '}', 10
  FX_G02_LEN = $ - fx_g02

  ; g03 -- and the other direction, byte view to struct
  fx_g03:
	db '@transitus', 10
	db 'structura Caput {', 10
	db '    signum: u8', 10
	db '    genus:  u8', 10
	db '}', 10
	db 10
	db 'publica functio ex_octetis(b: acies<u8, 2>) -> u8 {', 10
	db '    firma c = b sicut Caput;', 10
	db '    redde c.signum;', 10
	db '}', 10
  FX_G03_LEN = $ - fx_g03

  ; b02 -- struct to struct: still `EXS-E0305`, and §5.2 admits no such pair
  fx_b02:
	db 'structura Unus { valor: mensura }', 10
	db 10
	db 'structura Duo { valor: mensura }', 10
	db 10
	db 'publica functio muta(u: Unus) -> Duo {', 10
	db '    redde u sicut Duo;', 10
	db '}', 10
  FX_B02_LEN = $ - fx_b02

  ; b03 -- a `dyn` SOURCE is not exempted: the test is on the target only
  fx_b03:
	db 'interfacies Scriptura {', 10
	db '    functio pone(s: Scriptura, n: mensura) -> mensura', 10
	db '}', 10
	db 10
	db 'publica functio muta(d: dyn Scriptura) -> acies<u8, 2> {', 10
	db '    redde d sicut acies<u8, 2>;', 10
	db '}', 10
  FX_B03_LEN = $ - fx_b03

  fx_tab:
	; the code SET is exactly {EXS-E0510}: at the impl head (§4.4's
	; ceiling) and at the cast. Three diagnostics before this commit
	dq fx_b01, FX_B01_LEN
	dd 2, 510, 143, 510, 317, 0
	; the accepted twin: an empty mark does not exceed an empty bound
	dq fx_g01, FX_G01_LEN
	dd 0, 0, 0, 0, 0, 0
	; §5.2's admitted pair, both directions, still accepted
	dq fx_g02, FX_G02_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_g03, FX_G03_LEN
	dd 0, 0, 0, 0, 0, 0
	; every other aggregate `sicut` is still EXS-E0305
	dq fx_b02, FX_B02_LEN
	dd 1, 305, 118, 0, 0, 0
	dq fx_b03, FX_B03_LEN
	dd 1, 305, 148, 0, 0, 0
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
